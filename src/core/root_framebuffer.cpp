#include "root_framebuffer.hpp"

#include <cstdio>
#include <new>

namespace render_module::detail {

bool RootFramebuffer::ValidSize(int width, int height) {
    if (width <= 0 || height <= 0) return false;
    GLint textureLimit = 0, viewportLimit[2] = {};
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &textureLimit);
    glGetIntegerv(GL_MAX_VIEWPORT_DIMS, viewportLimit);
    return width <= textureLimit && height <= textureLimit &&
           width <= viewportLimit[0] && height <= viewportLimit[1];
}

bool RootFramebuffer::Resize(int width, int height) {
    if (!ValidSize(width, height)) {
        std::fprintf(stderr, "RenderModule: invalid root framebuffer size %dx%d.\n", width, height);
        return false;
    }
    if (framebuffer_ && width == width_ && height == height_) return true;
    std::shared_ptr<FrameValidity> nextValidity;
    try { nextValidity = std::make_shared<FrameValidity>(); }
    catch (const std::bad_alloc&) { return false; }

    GLint read = 0, draw = 0, texture = 0, unpack = 0;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &read);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &draw);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture);
    glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &unpack);
    GLuint nextFbo = 0, nextTexture = 0;
    glGenFramebuffers(1, &nextFbo);
    glGenTextures(1, &nextTexture);
    glBindFramebuffer(GL_FRAMEBUFFER, nextFbo);
    glBindTexture(GL_TEXTURE_2D, nextTexture);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, nextTexture, 0);
    const auto status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    const auto error = glGetError();
    const bool complete = nextFbo && nextTexture && status == GL_FRAMEBUFFER_COMPLETE && error == GL_NO_ERROR;
    if (complete) {
        // Invalidate borrowed frames BEFORE deleting their storage. Remap current
        // bindings if the old root was bound, rather than restoring deleted names.
        if (framebuffer_ && read == static_cast<GLint>(framebuffer_)) read = nextFbo;
        if (framebuffer_ && draw == static_cast<GLint>(framebuffer_)) draw = nextFbo;
        if (colorTexture_ && texture == static_cast<GLint>(colorTexture_)) texture = nextTexture;
        Destroy();
        framebuffer_ = nextFbo;
        colorTexture_ = nextTexture;
        width_ = width;
        height_ = height;
        ++generation_;
        validity_ = std::move(nextValidity);
    } else {
        glDeleteTextures(1, &nextTexture);
        glDeleteFramebuffers(1, &nextFbo);
        std::fprintf(stderr, "RenderModule: root allocation failed (FBO 0x%x, GL 0x%x); old storage retained.\n",
                     status, error);
    }
    glBindFramebuffer(GL_READ_FRAMEBUFFER, read);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, draw);
    glBindTexture(GL_TEXTURE_2D, texture);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, unpack);
    return complete;
}

void RootFramebuffer::Destroy() {
    frame_ = {};
    validity_.reset();
    if (colorTexture_) glDeleteTextures(1, &colorTexture_);
    if (framebuffer_) glDeleteFramebuffers(1, &framebuffer_);
    colorTexture_ = framebuffer_ = 0;
    width_ = height_ = 0;
}

void RootFramebuffer::BeginFrame() {
    frame_ = {};
    if (validity_) validity_->frameId = 0;
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_);
}

PresentedFrame RootFramebuffer::Complete(std::uint64_t id,
    std::chrono::steady_clock::time_point started,
    std::chrono::steady_clock::time_point completed) {
    if (!validity_ || !id || completed < started) return {};
    validity_->frameId = id;
    frame_.framebuffer_ = framebuffer_;
    frame_.colorTexture_ = colorTexture_;
    frame_.validity_ = validity_;
    frame_.width = width_;
    frame_.height = height_;
    frame_.frameId = id;
    frame_.framebufferGeneration = generation_;
    frame_.renderStarted = started;
    frame_.renderCompleted = completed;
    return frame_;
}

} // namespace render_module::detail
