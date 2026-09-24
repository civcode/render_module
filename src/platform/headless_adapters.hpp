#pragma once

#include "platform_backend.hpp"
#include "input/input_backend.hpp"
#include "present/presenter.hpp"

namespace render_module::detail {
// Both headless context providers use RemoteInputBackend, never ImGui GLFW input.
std::unique_ptr<IInputBackend> CreateHeadlessInput();
// Image presenter consumes complete root frames; readback is on-demand.
std::unique_ptr<IPresenter> CreateHeadlessPresenter();
} // namespace render_module::detail
