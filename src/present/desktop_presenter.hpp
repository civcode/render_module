#pragma once

#include <memory>
#include "presenter.hpp"

struct GLFWwindow;

namespace render_module::detail {
std::unique_ptr<IPresenter> CreateDesktopPresenter(GLFWwindow* window);
// Separate from swap so tests can compare the exact backbuffer before presentation.
bool BlitToDefaultFramebuffer(const PresentedFrame& frame, int width, int height);
} // namespace render_module::detail
