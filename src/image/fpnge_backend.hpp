#pragma once

#include <cstddef>
#include <vector>

namespace render_module::detail {

bool EncodeFpnge(const unsigned char* rgba,
                 int width,
                 int height,
                 int level,
                 std::vector<unsigned char>& scratch,
                 std::vector<unsigned char>& output,
                 std::size_t limit);

} // namespace render_module::detail
