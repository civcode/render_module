#include "platform_backend.hpp"

#include <cstdio>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include "input/glfw_imgui_input.hpp"
#include "present/desktop_presenter.hpp"

namespace render_module::detail {
namespace {

class GlfwDesktopBackend final : public IPlatformBackend {
public:
    ~GlfwDesktopBackend() override { Shutdown(); }

    bool Initialize(int width, int height, const char* title) override {
        if (IsInitialized()) return true;
        if (width <= 0 || height <= 0) return false;
        glfwSetErrorCallback([](int code, const char* message) {
            std::fprintf(stderr, "GLFW error %d: %s\n", code, message);
        });
        // Initialization hints survive glfwTerminate(); never inherit Null mode.
        glfwInitHint(GLFW_PLATFORM, GLFW_ANY_PLATFORM);
        if (!glfwInit()) return false;
        glfwInitialized_ = true;

        glfwDefaultWindowHints();
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
        glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif
        glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
        window_ = glfwCreateWindow(width, height, title, nullptr, nullptr);
        if (!window_) {
            Shutdown();
            return false;
        }
        if (!MakeCurrent()) {
            Shutdown();
            return false;
        }
        glfwSwapInterval(0);
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

    GraphicsDiagnostics Diagnostics() const override {
        const char* provider = "GLFW Desktop (native context)";
        switch (glfwGetPlatform()) {
            case GLFW_PLATFORM_X11: provider = "GLFW Desktop / GLX"; break;
            case GLFW_PLATFORM_WAYLAND: provider = "GLFW Desktop / Wayland EGL"; break;
            case GLFW_PLATFORM_WIN32: provider = "GLFW Desktop / WGL"; break;
            case GLFW_PLATFORM_COCOA: provider = "GLFW Desktop / CGL"; break;
        }
        return {provider, "desktop default", {}, {}};
    }

    void Shutdown() override {
        ClearCurrent();
        if (window_) {
            glfwDestroyWindow(window_);
            window_ = nullptr;
        }
        if (glfwInitialized_) {
            glfwTerminate();
            glfwInitialized_ = false;
        }
    }

    void PollEvents() override { glfwPollEvents(); }
    bool ShouldClose() const override {
        return !window_ || glfwWindowShouldClose(window_);
    }
    void RequestClose() override {
        if (window_) glfwSetWindowShouldClose(window_, GLFW_TRUE);
    }
    PlatformSize GetWindowSize() const override {
        PlatformSize size;
        if (window_) glfwGetWindowSize(window_, &size.width, &size.height);
        return size;
    }
    PlatformSize GetFramebufferSize() const override {
        PlatformSize size;
        if (window_) glfwGetFramebufferSize(window_, &size.width, &size.height);
        return size;
    }
    std::unique_ptr<IInputBackend> CreateInputBackend() override {
        return window_ ? CreateGlfwImGuiInput(window_) : nullptr;
    }
    std::unique_ptr<IPresenter> CreatePresenter() override {
        return window_ ? CreateDesktopPresenter(window_) : nullptr;
    }

private:
    GLFWwindow* window_ = nullptr;
    bool glfwInitialized_ = false;
};

} // namespace

std::unique_ptr<IPlatformBackend> CreateGlfwDesktopBackend() {
    return std::make_unique<GlfwDesktopBackend>();
}

} // namespace render_module::detail
