#include "platform_backend.hpp"
#include "headless_adapters.hpp"

#include <cstdio>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_EGL
#include <GLFW/glfw3native.h>

#if GLFW_VERSION_MAJOR < 3 || (GLFW_VERSION_MAJOR == 3 && \
    (GLFW_VERSION_MINOR < 5 || (GLFW_VERSION_MINOR == 5 && GLFW_VERSION_REVISION < 1)))
#error GLFW Null EGL requires GLFW 3.5.1 or newer
#endif

namespace render_module::detail {
namespace {
class GlfwNullEglBackend final : public IPlatformBackend {
public:
    ~GlfwNullEglBackend() override { Shutdown(); }
    bool Initialize(int width, int height, const char* title) override {
        if (IsInitialized()) return true;
        if (width <= 0 || height <= 0) return false;
        glfwSetErrorCallback([](int code, const char* message) {
            std::fprintf(stderr, "GLFW Null/EGL error %d: %s\n", code, message);
        });
        glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_NULL);
        if (!glfwInit()) return false;
        glfwInitialized_ = true;
        glfwDefaultWindowHints();
        glfwWindowHint(GLFW_CONTEXT_CREATION_API, GLFW_EGL_CONTEXT_API);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        window_ = glfwCreateWindow(width, height, title, nullptr, nullptr);
        if (!window_ || !MakeCurrent()) {
            std::fprintf(stderr, "RenderModule: GLFW Null/EGL initialization failed. "
                "This provider requires Mesa surfaceless EGL; select NativeEgl explicitly "
                "for other drivers. No desktop fallback was attempted.\n");
            Shutdown();
            return false;
        }
        size_ = {width, height};
        const EGLDisplay display = glfwGetEGLDisplay();
        const char* vendor = eglQueryString(display, EGL_VENDOR);
        const char* version = eglQueryString(display, EGL_VERSION);
        if (!vendor || !version) {
            std::fprintf(stderr, "RenderModule: GLFW EGL diagnostics failed (EGL 0x%x).\n",
                         eglGetError());
            Shutdown();
            return false;
        }
        diagnostics_ = {"GLFW 3.5.1+ Null + EGL (pbuffer)",
                        "Mesa surfaceless platform; driver-selected device", vendor, version};
        return true;
    }
    bool IsInitialized() const override { return window_ != nullptr; }
    bool MakeCurrent() override {
        if (!window_) return false;
        glfwGetError(nullptr); // Prior errors have already been logged by the callback.
        glfwMakeContextCurrent(window_);
        if (glfwGetError(nullptr) != GLFW_NO_ERROR || glfwGetCurrentContext() != window_)
            return false;
        MarkCurrent();
        return true;
    }
    void* GetProcAddress(const char* name) const override {
        return reinterpret_cast<void*>(glfwGetProcAddress(name));
    }
    GraphicsDiagnostics Diagnostics() const override { return diagnostics_; }
    void Shutdown() override {
        ClearCurrent();
        if (window_) glfwDestroyWindow(window_);
        window_ = nullptr;
        if (glfwInitialized_) glfwTerminate();
        glfwInitialized_ = false;
        size_ = {};
        diagnostics_ = {};
    }
    void PollEvents() override {} // No OS events or window system.
    bool ShouldClose() const override { return !window_ || glfwWindowShouldClose(window_); }
    void RequestClose() override {
        if (window_) glfwSetWindowShouldClose(window_, GLFW_TRUE);
    }
    PlatformSize GetWindowSize() const override { return size_; }
    PlatformSize GetFramebufferSize() const override { return size_; }
    std::unique_ptr<IInputBackend> CreateInputBackend() override {
        return window_ ? CreateHeadlessFrameInput(size_) : nullptr;
    }
    std::unique_ptr<IPresenter> CreatePresenter() override {
        return window_ ? CreateHeadlessPresenter() : nullptr;
    }
private:
    GLFWwindow* window_ = nullptr;
    bool glfwInitialized_ = false;
    PlatformSize size_;
    GraphicsDiagnostics diagnostics_;
};
} // namespace

std::unique_ptr<IPlatformBackend> CreateGlfwNullEglBackend() {
    return std::make_unique<GlfwNullEglBackend>();
}
} // namespace render_module::detail
