#pragma once

#include "present/presented_frame.hpp"

namespace render_module::detail {

// RGBA8, bottom-origin, no depth/stencil. Owns GL objects; all operations,
// including destruction, require the owning context current on the render thread.
class RootFramebuffer {
public:
    ~RootFramebuffer() { Destroy(); }
    RootFramebuffer() = default;
    RootFramebuffer(const RootFramebuffer&) = delete;
    RootFramebuffer& operator=(const RootFramebuffer&) = delete;

    static bool ValidSize(int width, int height);
    bool Resize(int width, int height); // Transactional; same size is a no-op.
    void Destroy();
    void BeginFrame(); // Invalidates the previous completed frame and binds root.
    PresentedFrame Complete(std::uint64_t id,
        std::chrono::steady_clock::time_point started,
        std::chrono::steady_clock::time_point completed);
    const PresentedFrame& Frame() const { return frame_; }
    GLuint Framebuffer() const { return framebuffer_; }
    GLuint ColorTexture() const { return colorTexture_; }
    int Width() const { return width_; }
    int Height() const { return height_; }
    std::uint64_t Generation() const { return generation_; }

private:
    GLuint framebuffer_ = 0, colorTexture_ = 0;
    int width_ = 0, height_ = 0;
    std::uint64_t generation_ = 0;
    std::shared_ptr<FrameValidity> validity_;
    PresentedFrame frame_;
};

} // namespace render_module::detail
