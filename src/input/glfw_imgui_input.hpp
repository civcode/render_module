#pragma once

#include <memory>
#include "input_backend.hpp"

struct GLFWwindow;

namespace render_module::detail {
std::unique_ptr<IInputBackend> CreateGlfwImGuiInput(GLFWwindow* window);
} // namespace render_module::detail
