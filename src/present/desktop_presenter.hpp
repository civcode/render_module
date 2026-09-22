#pragma once

#include <memory>
#include "presenter.hpp"

struct GLFWwindow;

namespace render_module::detail {
std::unique_ptr<IPresenter> CreateDesktopPresenter(GLFWwindow* window);
} // namespace render_module::detail
