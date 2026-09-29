#include "fpnge_backend.hpp"

#include <fpnge.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <new>
#include <utility>

namespace render_module::detail {
namespace {

constexpr std::array<unsigned char, 8> PngSignature{
    0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a
};

bool OutputCapacity(int imageWidth, int imageHeight, std::size_t& capacity) noexcept {
    if (imageWidth <= 0 || imageHeight <= 0) return false;
    const auto max = std::numeric_limits<std::size_t>::max();
    const auto width = static_cast<std::size_t>(imageWidth);
    const auto height = static_cast<std::size_t>(imageHeight);
    if (width > max / 4) return false;
    const auto row = width * 4;
    if (row > (max - 1) / 2) return false;
    const auto perRow = row * 2 + 1;
    if (height > (max - 1024) / perRow) return false;
    capacity = 1024 + perRow * height;
    return capacity != 0;
}

} // namespace

bool EncodeFpnge(const unsigned char* rgba,
                 int width,
                 int height,
                 int level,
                 std::vector<unsigned char>& scratch,
                 std::vector<unsigned char>& output,
                 std::size_t limit) {
    std::size_t capacity = 0;
    if (!rgba || !limit || !OutputCapacity(width, height, capacity)) return false;

    // FPNGE's API accepts an output pointer but no capacity. Keep permanent
    // headroom beyond its documented conservative bound; debug builds also
    // verify that this headroom remains untouched.
    constexpr std::size_t StoragePadding = 128;
#ifndef NDEBUG
    constexpr std::size_t RedZone = StoragePadding;
#else
    constexpr std::size_t RedZone = 0;
#endif
    if (capacity > std::numeric_limits<std::size_t>::max() - StoragePadding) return false;
    const auto required = capacity + StoragePadding;

    try {
        if (scratch.size() < required) scratch.resize(required);
    } catch (const std::bad_alloc&) {
        return false;
    }

#ifndef NDEBUG
    std::fill(scratch.begin() + static_cast<std::ptrdiff_t>(capacity),
              scratch.begin() + static_cast<std::ptrdiff_t>(required), 0xa5);
#endif

    FPNGEOptions options;
    FPNGEFillOptions(&options, level, FPNGE_CICP_NONE);
    std::size_t size = 0;
    try {
        size = FPNGEEncode(
            1,
            4,
            rgba,
            static_cast<std::size_t>(width),
            static_cast<std::size_t>(width) * 4,
            static_cast<std::size_t>(height),
            scratch.data(),
            &options);
    } catch (const std::bad_alloc&) {
        return false;
    }

#ifndef NDEBUG
    if (!std::all_of(scratch.begin() + static_cast<std::ptrdiff_t>(capacity),
                     scratch.begin() + static_cast<std::ptrdiff_t>(required),
                     [](unsigned char value) { return value == 0xa5; })) {
        return false;
    }
#endif

    if (!size || size > capacity || size > limit || size < PngSignature.size() ||
        std::memcmp(scratch.data(), PngSignature.data(), PngSignature.size()) != 0) {
        return false;
    }

    std::vector<unsigned char> next;
    try {
        next.assign(scratch.begin(), scratch.begin() + static_cast<std::ptrdiff_t>(size));
    } catch (const std::bad_alloc&) {
        return false;
    }
    output = std::move(next);
    return true;
}

} // namespace render_module::detail
