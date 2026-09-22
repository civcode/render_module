#include "desktop_presenter.hpp"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

namespace render_module::detail {
namespace {

class DesktopPresenter final : public IPresenter {
public:
    explicit DesktopPresenter(GLFWwindow* window) : window_(window) {}
    void Present() override { glfwSwapBuffers(window_); }

private:
    GLFWwindow* window_;
};

} // namespace

std::unique_ptr<IPresenter> CreateDesktopPresenter(GLFWwindow* window) {
    return std::make_unique<DesktopPresenter>(window);
}

} // namespace render_module::detail
