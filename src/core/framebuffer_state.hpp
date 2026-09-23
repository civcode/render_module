#pragma once
#include <glad/glad.h>

namespace render_module::detail {
// Keep independent read/draw bindings and viewport across external renderers.
class FramebufferState {
public:
    FramebufferState() {
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &read_);
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &draw_);
        glGetIntegerv(GL_VIEWPORT, viewport_);
    }
    ~FramebufferState() {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, read_);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, draw_);
        glViewport(viewport_[0], viewport_[1], viewport_[2], viewport_[3]);
    }
    FramebufferState(const FramebufferState&) = delete;
    FramebufferState& operator=(const FramebufferState&) = delete;
private:
    GLint read_ = 0, draw_ = 0, viewport_[4] = {};
};
} // namespace render_module::detail
