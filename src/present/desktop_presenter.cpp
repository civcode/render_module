#include "desktop_presenter.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

namespace render_module::detail {

bool BlitToDefaultFramebuffer(const PresentedFrame& frame, int width, int height) {
    if (!frame.IsValid()) return false;
    if (width <= 0 || height <= 0) return true; // Minimized; no drawable surface.
    GLint read = 0, draw = 0, readBuffer = 0, drawBuffer = 0;
    GLboolean mask[4];
    GLfloat clear[4];
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &read);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &draw);
    glGetBooleanv(GL_COLOR_WRITEMASK, mask);
    glGetFloatv(GL_COLOR_CLEAR_VALUE, clear);
    const bool scissor = glIsEnabled(GL_SCISSOR_TEST);
    const bool srgb = glIsEnabled(GL_FRAMEBUFFER_SRGB);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_FRAMEBUFFER_SRGB); // Preserve the root's RGBA8 values, no conversion.
    glBindFramebuffer(GL_READ_FRAMEBUFFER, frame.Framebuffer());
    glGetIntegerv(GL_READ_BUFFER, &readBuffer);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glGetIntegerv(GL_DRAW_BUFFER, &drawBuffer);
    glDrawBuffer(GL_BACK);

    // Normally 1:1 physical pixels. If the window changes between render and
    // swap, fit with centered black letterboxing, never change the aspect ratio.
    const double scale = std::min(double(width)/frame.width, double(height)/frame.height);
    const int targetWidth = std::max(1, int(std::lround(frame.width*scale)));
    const int targetHeight = std::max(1, int(std::lround(frame.height*scale)));
    const int x = (width - targetWidth)/2, y = (height - targetHeight)/2;
    if (targetWidth != width || targetHeight != height) {
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);
    }
    glBlitFramebuffer(0, 0, frame.width, frame.height,
        x, y, x + targetWidth, y + targetHeight, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    const auto error = glGetError();
    glReadBuffer(readBuffer);
    glDrawBuffer(drawBuffer);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, read);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, draw);
    glColorMask(mask[0], mask[1], mask[2], mask[3]);
    glClearColor(clear[0], clear[1], clear[2], clear[3]);
    if (scissor) glEnable(GL_SCISSOR_TEST);
    if (srgb) glEnable(GL_FRAMEBUFFER_SRGB);
    if (error != GL_NO_ERROR)
        std::fprintf(stderr, "RenderModule: desktop blit failed (GL 0x%x).\n", error);
    return error == GL_NO_ERROR;
}

namespace {
class DesktopPresenter final : public IPresenter {
public:
    explicit DesktopPresenter(GLFWwindow* window) : window_(window) {}
    bool Present(const PresentedFrame& frame) override {
        int width = 0, height = 0;
        glfwGetFramebufferSize(window_, &width, &height);
        if (!BlitToDefaultFramebuffer(frame, width, height)) return false;
        if (width > 0 && height > 0) glfwSwapBuffers(window_);
        return true;
    }
private:
    GLFWwindow* window_;
};
}

std::unique_ptr<IPresenter> CreateDesktopPresenter(GLFWwindow* window) {
    return std::make_unique<DesktopPresenter>(window);
}
} // namespace render_module::detail
