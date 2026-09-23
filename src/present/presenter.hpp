#pragma once

#include "presented_frame.hpp"

namespace render_module::detail {

class IPresenter {
public:
    virtual ~IPresenter() = default;
    // Synchronous borrowed frame; never retain its GL handles. No ImGui calls.
    virtual bool Present(const PresentedFrame& frame) = 0;
};

} // namespace render_module::detail
