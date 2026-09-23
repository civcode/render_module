#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <glad/glad.h>

namespace render_module::detail {

// Not a GPU owner. Tokens expire on resize/destruction and are invalid while
// rendering or after a newer frame is completed. Render-thread-only, synchronous.
struct FrameValidity {
    std::uint64_t frameId = 0;
};

class RootFramebuffer;
struct PresentedFrame {
    int width = 0;
    int height = 0;
    std::uint64_t frameId = 0;
    std::uint64_t framebufferGeneration = 0;
    std::chrono::steady_clock::time_point renderStarted;
    std::chrono::steady_clock::time_point renderCompleted;

    bool IsValid() const {
        const auto token = validity_.lock();
        return token && frameId != 0 && token->frameId == frameId;
    }
    // Invalidated descriptors cannot yield deleted or recycled GL handles.
    GLuint Framebuffer() const { return IsValid() ? framebuffer_ : 0; }
    GLuint ColorTexture() const { return IsValid() ? colorTexture_ : 0; }

private:
    GLuint framebuffer_ = 0;
    GLuint colorTexture_ = 0;
    std::weak_ptr<const FrameValidity> validity_;
    friend class RootFramebuffer;
};

} // namespace render_module::detail
