#include "glfw_imgui_input.hpp"

#include <backends/imgui_impl_glfw.h>

namespace render_module::detail {
namespace {

class GlfwImGuiInput final : public IInputBackend {
public:
    explicit GlfwImGuiInput(GLFWwindow* window) : window_(window) {}
    ~GlfwImGuiInput() override { Shutdown(); }

    bool Init() override {
        if (!initialized_ && window_)
            initialized_ = ImGui_ImplGlfw_InitForOpenGL(window_, true);
        return initialized_;
    }

    void NewFrame() override {
        if (initialized_) ImGui_ImplGlfw_NewFrame();
    }

    void Shutdown() override {
        if (!initialized_) return;
        ImGui_ImplGlfw_Shutdown();
        initialized_ = false;
    }

private:
    GLFWwindow* window_;
    bool initialized_ = false;
};

} // namespace

std::unique_ptr<IInputBackend> CreateGlfwImGuiInput(GLFWwindow* window) {
    return std::make_unique<GlfwImGuiInput>(window);
}

} // namespace render_module::detail
