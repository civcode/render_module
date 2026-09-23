#pragma once

#include "platform_backend.hpp"
#include "input/input_backend.hpp"
#include "present/presenter.hpp"

namespace render_module::detail {
// Frame metrics only: no GLFW input, remote events, clipboard, or cursor support.
std::unique_ptr<IInputBackend> CreateHeadlessFrameInput(PlatformSize size);
// Image presenter consumes complete root frames; readback is on-demand.
std::unique_ptr<IPresenter> CreateHeadlessPresenter();
} // namespace render_module::detail
