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

    bool Init(int width, int height, const char* title) override {
        if (IsInitialized()) return true;
        if (width <= 0 || height <= 0) return false;
        glfwSetErrorCallback([](int code, const char* message) {
            std::fprintf(stderr, "GLFW error %d: %s\n", code, message);
        });
        if (!glfwInit()) return false;
        glfwInitialized_ = true;

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
        MakeCurrent();
        glfwSwapInterval(0);
        return true;
    }

    bool IsInitialized() const override { return window_ != nullptr; }

    void MakeCurrent() override {
        if (window_) glfwMakeContextCurrent(window_);
    }

    ProcAddressLoader GetProcAddressLoader() const override {
        return &LoadProcAddress;
    }

    void Shutdown() override {
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
    // Lookup uses the current context. Keep GLFW's function-pointer conversion
    // confined here rather than coupling GLAD or the render loop to GLFW.
    static void* LoadProcAddress(const char* name) {
        return reinterpret_cast<void*>(glfwGetProcAddress(name));
    }

    GLFWwindow* window_ = nullptr;
    bool glfwInitialized_ = false;
};

} // namespace

std::unique_ptr<IPlatformBackend> CreateGlfwDesktopBackend() {
    return std::make_unique<GlfwDesktopBackend>();
}

} // namespace render_module::detail
