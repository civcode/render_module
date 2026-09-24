#pragma once

#include "presented_frame.hpp"

namespace render_module::detail {

class IPresenter {
public:
    virtual ~IPresenter() = default;
    // Render-thread frame boundary: consume optional presentation-side requests.
    virtual bool PrepareFrame() { return true; }
    // Synchronous borrowed frame; never retain its GL handles. No ImGui calls.
    virtual bool Present(const PresentedFrame& frame) = 0;
};

} // namespace render_module::detail
