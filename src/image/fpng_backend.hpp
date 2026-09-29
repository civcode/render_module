#pragma once

#include "present/image_presenter.hpp"
#include <cstddef>
#include <vector>

namespace render_module::detail {

bool EncodeFpng(const ImageRgba& image,
                std::vector<unsigned char>& output,
                std::size_t limit);

} // namespace render_module::detail
