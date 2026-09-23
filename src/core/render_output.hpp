#pragma once

#include "present/presented_frame.hpp"

namespace render_module::detail {
// Diagnostic/internal only. A copy remains usable until the next frame begins,
// a successful resize, or shutdown; its GL accessors then return zero.
PresentedFrame CompletedFrame();
// Headless render-thread request. Validated now, applied atomically at the next
// frame boundary. No browser protocol and no input event are involved.
bool RequestVirtualDisplaySize(int width, int height);
} // namespace render_module::detail
