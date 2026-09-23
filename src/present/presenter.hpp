#pragma once

namespace render_module::detail {

class IPresenter {
public:
    virtual ~IPresenter() = default;
    // Headless Phase 2 has no final composition target, even if EGL uses a pbuffer.
    virtual bool UsesDefaultFramebuffer() const = 0;
    virtual void Present() = 0; // Render thread only.
};

} // namespace render_module::detail
