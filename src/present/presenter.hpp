#pragma once

namespace render_module::detail {

class IPresenter {
public:
    virtual ~IPresenter() = default;
    // Phase 1 presents the already-rendered default framebuffer, on the render thread.
    virtual void Present() = 0;
};

} // namespace render_module::detail
