#include "fpng_backend.hpp"

#include <fpng.h>
#include <cstdint>
#include <mutex>
#include <new>
#include <utility>

namespace render_module::detail {

bool EncodeFpng(const ImageRgba& image,
                std::vector<unsigned char>& output,
                std::size_t limit) {
    static std::once_flag init;
    std::call_once(init, [] { fpng::fpng_init(); });

    std::vector<unsigned char> next;
    try {
        if (!fpng::fpng_encode_image_to_memory(
                image.pixels.data(),
                static_cast<std::uint32_t>(image.width),
                static_cast<std::uint32_t>(image.height),
                4,
                next,
                0)) {
            return false;
        }
    } catch (const std::bad_alloc&) {
        return false;
    }
    if (next.empty() || next.size() > limit) return false;
    output = std::move(next);
    return true;
}

} // namespace render_module::detail
