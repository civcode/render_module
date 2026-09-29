#pragma once

#include <cstddef>
#include <vector>

namespace render_module::detail {

bool EncodeFpng(const unsigned char* rgba,
                int width,
                int height,
                std::vector<unsigned char>& output,
                std::size_t limit);

} // namespace render_module::detail
