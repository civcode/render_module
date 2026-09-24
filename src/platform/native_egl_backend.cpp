#include "platform_backend.hpp"
#include "headless_adapters.hpp"

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <dlfcn.h>
#include <cstdio>
#include <cstring>
#include <vector>

namespace render_module::detail {
namespace {

bool HasExtension(const char* extensions, const char* name) {
    if (!extensions) return false;
    const auto length = std::strlen(name);
    for (const char* found = std::strstr(extensions, name); found;
         found = std::strstr(found + length, name)) {
        if ((found == extensions || found[-1] == ' ') &&
            (found[length] == '\0' || found[length] == ' ')) return true;
    }
    return false;
}

void EglError(const char* operation) {
    std::fprintf(stderr, "RenderModule Native EGL: %s failed (EGL error 0x%04x).\n",
                 operation, eglGetError());
}

class NativeEglBackend final : public IPlatformBackend {
public:
    explicit NativeEglBackend(NativeEglConfig config) : config_(config) {}
    ~NativeEglBackend() override { Shutdown(); }

    bool Initialize(int width, int height, const char*) override {
        if (IsInitialized()) return true;
        if (width <= 0 || height <= 0 || config_.deviceIndex < -1) {
            std::fprintf(stderr, "RenderModule Native EGL: invalid dimensions or device index.\n");
            return false;
        }
        if (!SelectDisplay()) {
            Shutdown();
            return false;
        }
        EGLint major = 0, minor = 0;
        if (!eglInitialize(display_, &major, &minor)) {
            EglError("eglInitialize on selected device/platform");
            Shutdown();
            return false;
        }
        displayInitialized_ = true;
        const char* vendor = eglQueryString(display_, EGL_VENDOR);
        const char* version = eglQueryString(display_, EGL_VERSION);
        const char* extensions = eglQueryString(display_, EGL_EXTENSIONS);
        if (!vendor || !version || !extensions) {
            EglError("eglQueryString");
            Shutdown();
            return false;
        }
        diagnostics_.eglVendor = vendor;
        diagnostics_.eglVersion = version;
        const bool egl15 = major > 1 || (major == 1 && minor >= 5);
        if (!egl15 && !HasExtension(extensions, "EGL_KHR_create_context")) {
            std::fprintf(stderr, "RenderModule Native EGL: EGL 1.5 or "
                "EGL_KHR_create_context is required for OpenGL 3.3 Core.\n");
            Shutdown();
            return false;
        }
        if (!eglBindAPI(EGL_OPENGL_API)) {
            EglError("eglBindAPI(EGL_OPENGL_API)");
            Shutdown();
            return false;
        }
        const bool surfaceless = !config_.forcePbuffer &&
            (egl15 || HasExtension(extensions, "EGL_KHR_surfaceless_context"));
        bool ready = CreateContext(!surfaceless);
        if (!ready && surfaceless) {
            std::fprintf(stderr, "RenderModule Native EGL: surfaceless context failed; "
                "trying a 1x1 pbuffer on the SAME selected device.\n");
            DestroyContextObjects();
            ready = CreateContext(true);
        }
        if (!ready) {
            Shutdown();
            return false;
        }
        // EGL 1.4 may not expose core GL symbols via eglGetProcAddress(). Prefer
        // GLVND's API library; this is symbol lookup, not a change of EGL device.
        glLibrary_ = dlopen("libOpenGL.so.0", RTLD_LAZY | RTLD_LOCAL);
        if (!glLibrary_) glLibrary_ = dlopen("libGL.so.1", RTLD_LAZY | RTLD_LOCAL);
        size_ = {width, height};
        initialized_ = true;
        closeRequested_ = false;
        return true;
    }

    bool IsInitialized() const override { return initialized_; }
    bool MakeCurrent() override {
        if (context_ == EGL_NO_CONTEXT) return false;
        if (!eglMakeCurrent(display_, surface_, surface_, context_)) {
            EglError("eglMakeCurrent");
            return false;
        }
        MarkCurrent();
        return true;
    }
    void* GetProcAddress(const char* name) const override {
        if (auto proc = eglGetProcAddress(name)) return reinterpret_cast<void*>(proc);
        return glLibrary_ ? dlsym(glLibrary_, name) : nullptr;
    }
    GraphicsDiagnostics Diagnostics() const override { return diagnostics_; }
    void Shutdown() override {
        ClearCurrent();
        DestroyContextObjects();
        if (displayInitialized_ && !eglTerminate(display_)) EglError("eglTerminate");
        displayInitialized_ = false;
        display_ = EGL_NO_DISPLAY;
        if (glLibrary_) dlclose(glLibrary_);
        glLibrary_ = nullptr;
        size_ = {};
        diagnostics_ = {};
        initialized_ = false;
        closeRequested_ = false;
    }
    void PollEvents() override {}
    bool ShouldClose() const override { return !initialized_ || closeRequested_; }
    void RequestClose() override { closeRequested_ = true; }
    PlatformSize GetWindowSize() const override { return size_; }
    PlatformSize GetFramebufferSize() const override { return size_; }
    std::unique_ptr<IInputBackend> CreateInputBackend() override {
        return initialized_ ? CreateHeadlessInput() : nullptr;
    }
    std::unique_ptr<IPresenter> CreatePresenter() override {
        return initialized_ ? CreateHeadlessPresenter() : nullptr;
    }

private:
    bool SelectDisplay() {
        const char* extensions = eglQueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS);
        if (!extensions) {
            EglError("query EGL client extensions (no display server will be tried)");
            return false;
        }
        const auto getDisplayExt = reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(
            eglGetProcAddress("eglGetPlatformDisplayEXT"));
        const auto getDisplayCore = reinterpret_cast<PFNEGLGETPLATFORMDISPLAYPROC>(
            eglGetProcAddress("eglGetPlatformDisplay"));
        if (!getDisplayExt && !getDisplayCore) {
            std::fprintf(stderr, "RenderModule Native EGL: no platform display API.\n");
            return false;
        }
        const auto getDisplay = [&](EGLenum platform, void* device) {
            return getDisplayExt ? getDisplayExt(platform, device, nullptr)
                                 : getDisplayCore(platform, device, nullptr);
        };
        const bool devicesSupported = HasExtension(extensions, "EGL_EXT_platform_device") &&
            (HasExtension(extensions, "EGL_EXT_device_enumeration") ||
             HasExtension(extensions, "EGL_EXT_device_base"));
        if (devicesSupported) {
            const auto queryDevices = reinterpret_cast<PFNEGLQUERYDEVICESEXTPROC>(
                eglGetProcAddress("eglQueryDevicesEXT"));
            EGLint count = 0;
            if (!queryDevices || !queryDevices(0, nullptr, &count)) {
                EglError("eglQueryDevicesEXT(count)");
                return false;
            }
            if (count > 0) {
                std::vector<EGLDeviceEXT> devices(static_cast<std::size_t>(count));
                if (!queryDevices(count, devices.data(), &count)) {
                    EglError("eglQueryDevicesEXT(devices)");
                    return false;
                }
                const int index = config_.deviceIndex < 0 ? 0 : config_.deviceIndex;
                if (index >= count) {
                    std::fprintf(stderr, "RenderModule Native EGL: device index %d out of range "
                        "(%d devices). No fallback attempted.\n", index, count);
                    return false;
                }
                diagnostics_.device = "EGL device index " + std::to_string(index);
                const auto queryDeviceString = reinterpret_cast<PFNEGLQUERYDEVICESTRINGEXTPROC>(
                    eglGetProcAddress("eglQueryDeviceStringEXT"));
                if (queryDeviceString && (HasExtension(extensions, "EGL_EXT_device_query") ||
                                         HasExtension(extensions, "EGL_EXT_device_base"))) {
                    const char* deviceExtensions = queryDeviceString(devices[index], EGL_EXTENSIONS);
                    if (!deviceExtensions) {
                        EglError("eglQueryDeviceStringEXT(extensions)");
                        return false;
                    }
                    if (HasExtension(deviceExtensions, "EGL_MESA_device_software"))
                        diagnostics_.device += " (software)";
                    if (HasExtension(deviceExtensions, "EGL_EXT_device_drm_render_node")) {
                        const char* node = queryDeviceString(devices[index], EGL_DRM_RENDER_NODE_FILE_EXT);
                        if (node) {
                            diagnostics_.device += std::string("; ") + node;
                        } else {
                            // EXT_device_drm_render_node explicitly permits NULL
                            // for devices with no node (e.g. Mesa software devices).
                            const EGLint error = eglGetError();
                            if (error != EGL_SUCCESS) {
                                std::fprintf(stderr, "RenderModule Native EGL: render node query "
                                    "failed (EGL error 0x%04x).\n", error);
                                return false;
                            }
                            diagnostics_.device += "; no DRM render node";
                        }
                    }
                }
                std::fprintf(stderr, "RenderModule Native EGL: selecting %s of %d devices.\n",
                             diagnostics_.device.c_str(), count);
                display_ = getDisplay(EGL_PLATFORM_DEVICE_EXT, devices[index]);
                if (display_ == EGL_NO_DISPLAY) {
                    EglError("get selected device display (no other device will be tried)");
                    return false;
                }
            }
        }
        if (display_ == EGL_NO_DISPLAY) {
            // Never call eglGetDisplay(EGL_DEFAULT_DISPLAY): it may use X11/Wayland.
            if (config_.deviceIndex >= 0) {
                std::fprintf(stderr, "RenderModule Native EGL: requested device is unavailable.\n");
                return false;
            }
            if (!HasExtension(extensions, "EGL_MESA_platform_surfaceless")) {
                std::fprintf(stderr, "RenderModule Native EGL: neither a device display nor "
                    "EGL_MESA_platform_surfaceless is available. No desktop fallback.\n");
                return false;
            }
            std::fprintf(stderr, "RenderModule Native EGL: no enumerated device; "
                "using Mesa surfaceless display platform.\n");
            display_ = getDisplay(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY);
            diagnostics_.device = "Mesa surfaceless platform; driver-selected device";
        }
        if (display_ == EGL_NO_DISPLAY) {
            EglError("get platform display");
            return false;
        }
        return true;
    }

    bool CreateContext(bool pbuffer) {
        const EGLint configAttributes[] = {
            EGL_SURFACE_TYPE, pbuffer ? EGL_PBUFFER_BIT : 0,
            EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
            EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
            EGL_NONE
        };
        EGLConfig config = nullptr;
        EGLint count = 0;
        if (!eglChooseConfig(display_, configAttributes, &config, 1, &count)) {
            EglError("eglChooseConfig");
            return false;
        }
        if (!count) {
            std::fprintf(stderr, "RenderModule Native EGL: no RGBA8 OpenGL %s config.\n",
                         pbuffer ? "pbuffer" : "surfaceless");
            return false;
        }
        const EGLint contextAttributes[] = {
            EGL_CONTEXT_MAJOR_VERSION_KHR, 3, EGL_CONTEXT_MINOR_VERSION_KHR, 3,
            EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR,
            EGL_NONE
        };
        context_ = eglCreateContext(display_, config, EGL_NO_CONTEXT, contextAttributes);
        if (context_ == EGL_NO_CONTEXT) {
            EglError("eglCreateContext(OpenGL 3.3 Core)");
            return false;
        }
        if (pbuffer) {
            const EGLint attributes[] = {EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE};
            surface_ = eglCreatePbufferSurface(display_, config, attributes);
            if (surface_ == EGL_NO_SURFACE) {
                EglError("eglCreatePbufferSurface");
                return false;
            }
        }
        if (!MakeCurrent()) return false;
        diagnostics_.provider = pbuffer ? "Native EGL (1x1 pbuffer)" : "Native EGL (surfaceless context)";
        return true;
    }

    void DestroyContextObjects() {
        if (context_ != EGL_NO_CONTEXT && eglGetCurrentContext() == context_) {
            if (!eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT))
                EglError("release current context");
        }
        if (surface_ != EGL_NO_SURFACE && !eglDestroySurface(display_, surface_))
            EglError("eglDestroySurface");
        surface_ = EGL_NO_SURFACE;
        if (context_ != EGL_NO_CONTEXT && !eglDestroyContext(display_, context_))
            EglError("eglDestroyContext");
        context_ = EGL_NO_CONTEXT;
    }

    NativeEglConfig config_;
    EGLDisplay display_ = EGL_NO_DISPLAY;
    EGLContext context_ = EGL_NO_CONTEXT;
    EGLSurface surface_ = EGL_NO_SURFACE;
    void* glLibrary_ = nullptr;
    bool displayInitialized_ = false;
    bool initialized_ = false;
    bool closeRequested_ = false;
    PlatformSize size_;
    GraphicsDiagnostics diagnostics_;
};
} // namespace

std::unique_ptr<IPlatformBackend> CreateNativeEglBackend(const NativeEglConfig& config) {
    return std::make_unique<NativeEglBackend>(config);
}
} // namespace render_module::detail
