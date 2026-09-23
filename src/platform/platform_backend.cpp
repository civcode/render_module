#include "platform_backend.hpp"

#include <cstdio>
#include <glad/glad.h>

namespace render_module::detail {
namespace {
thread_local IGraphicsContext* currentContext = nullptr;
}

void IGraphicsContext::MarkCurrent() { currentContext = this; }
void IGraphicsContext::ClearCurrent() {
    if (currentContext == this) currentContext = nullptr;
}
void* IGraphicsContext::LoadCurrentProcAddress(const char* name) {
    return currentContext ? currentContext->GetProcAddress(name) : nullptr;
}
IGraphicsContext::ProcAddressLoader IGraphicsContext::GetProcAddressLoader() const {
    return &LoadCurrentProcAddress;
}

std::unique_ptr<IPlatformBackend> CreatePlatformBackend(const Config& config) {
    switch (config.backend) {
        case Backend::Desktop:
#ifdef RENDER_MODULE_ENABLE_DESKTOP
            return CreateGlfwDesktopBackend();
#else
            std::fprintf(stderr, "RenderModule: Desktop support was disabled at build time.\n");
            return nullptr;
#endif
        case Backend::Headless:
#ifdef RENDER_MODULE_ENABLE_HEADLESS
            switch (config.headlessContext) {
                case HeadlessContext::GlfwNullEgl:
                    if (config.nativeEgl.deviceIndex != -1 || config.nativeEgl.forcePbuffer) {
                        std::fprintf(stderr, "RenderModule: native EGL options require NativeEgl.\n");
                        return nullptr;
                    }
                    return CreateGlfwNullEglBackend();
                case HeadlessContext::NativeEgl:
                    return CreateNativeEglBackend(config.nativeEgl);
            }
            std::fprintf(stderr, "RenderModule: invalid headless context provider.\n");
#else
            std::fprintf(stderr, "RenderModule: Headless support was disabled at build time.\n");
#endif
            return nullptr;
    }
    std::fprintf(stderr, "RenderModule: invalid graphics backend.\n");
    return nullptr;
}

bool PrintGraphicsDiagnostics(const IGraphicsContext& context) {
    const auto info = context.Diagnostics();
    const auto string = [](GLenum name) {
        const auto* value = glGetString(name);
        return value ? reinterpret_cast<const char*>(value) : "(unavailable)";
    };
    std::fprintf(stderr,
        "RenderModule graphics:\n  Context provider: %s\n  Device: %s\n"
        "  OpenGL vendor: %s\n  OpenGL renderer: %s\n"
        "  OpenGL version: %s\n  GLSL version: %s\n",
        info.provider.c_str(), info.device.c_str(), string(GL_VENDOR),
        string(GL_RENDERER), string(GL_VERSION), string(GL_SHADING_LANGUAGE_VERSION));
    if (!info.eglVendor.empty())
        std::fprintf(stderr, "  EGL vendor: %s\n  EGL version: %s\n",
                     info.eglVendor.c_str(), info.eglVersion.c_str());
    GLint major = 0, minor = 0, profile = 0;
    glGetIntegerv(GL_MAJOR_VERSION, &major);
    glGetIntegerv(GL_MINOR_VERSION, &minor);
    if (major > 3 || (major == 3 && minor >= 2))
        glGetIntegerv(GL_CONTEXT_PROFILE_MASK, &profile);
    if ((major < 3 || (major == 3 && minor < 3)) ||
        !(profile & GL_CONTEXT_CORE_PROFILE_BIT)) {
        std::fprintf(stderr, "RenderModule: OpenGL 3.3 Core or newer is required.\n");
        return false;
    }
    return true;
}

} // namespace render_module::detail
