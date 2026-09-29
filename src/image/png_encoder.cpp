#include "png_encoder.hpp"
#include "fpng_backend.hpp"
#ifdef RENDER_MODULE_HAVE_FPNGE
#include "fpnge_backend.hpp"
#endif

#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <mutex>

namespace render_module::detail {
namespace {

bool ValidImage(const ImageRgba& image) noexcept {
    if (image.width <= 0 || image.height <= 0 ||
        image.width > 65536 || image.height > 65536) return false;
    const auto width = static_cast<std::uint64_t>(image.width);
    const auto height = static_cast<std::uint64_t>(image.height);
    const auto bytes = width * height * 4;
    return bytes <= 256u * 1024u * 1024u &&
           image.pixels.size() == static_cast<std::size_t>(bytes);
}

void WarnInvalidBackend(const char* value) {
    static std::once_flag once;
    std::call_once(once, [value] {
        std::fprintf(stderr,
            "RenderModule PNG: ignoring invalid RENDER_MODULE_PNG_ENCODER='%s'.\n",
            value ? value : "");
    });
}

void WarnUnavailableFpnge() {
    static std::once_flag once;
    std::call_once(once, [] {
        std::fprintf(stderr,
            "RenderModule PNG: FPNGE requested but unavailable; falling back to FPNG.\n");
    });
}

PngBackend Override(PngBackend preference) {
    if (preference != PngBackend::Auto) return preference;
    const char* value = std::getenv("RENDER_MODULE_PNG_ENCODER");
    if (!value || !*value || std::strcmp(value, "auto") == 0) return PngBackend::Auto;
    if (std::strcmp(value, "fpnge") == 0) return PngBackend::Fpnge;
    if (std::strcmp(value, "fpng") == 0) return PngBackend::Fpng;
    WarnInvalidBackend(value);
    return PngBackend::Auto;
}

int FpngeLevel() {
    const char* value = std::getenv("RENDER_MODULE_FPNGE_LEVEL");
    if (!value || !*value) return 4;
    char* end = nullptr;
    const long level = std::strtol(value, &end, 10);
    if (end && !*end && level >= 1 && level <= 5) return static_cast<int>(level);
    static std::once_flag once;
    std::call_once(once, [value] {
        std::fprintf(stderr,
            "RenderModule PNG: ignoring invalid RENDER_MODULE_FPNGE_LEVEL='%s'; using 4.\n",
            value);
    });
    return 4;
}

} // namespace

const char* PngBackendName(PngBackend backend) noexcept {
    switch (backend) {
        case PngBackend::Fpnge: return "fpnge-avx2";
        case PngBackend::Fpng: return "fpng";
        case PngBackend::Auto: return "auto";
    }
    return "unknown";
}

bool PngFpngeCompiled() noexcept {
#ifdef RENDER_MODULE_HAVE_FPNGE
    return true;
#else
    return false;
#endif
}

bool PngFpngeCpuSupported() noexcept {
#if defined(RENDER_MODULE_HAVE_FPNGE) && \
    (defined(__x86_64__) || defined(__amd64__)) && \
    (defined(__GNUC__) || defined(__clang__))
    __builtin_cpu_init();
    return __builtin_cpu_supports("avx2") &&
           __builtin_cpu_supports("pclmul");
#else
    return false;
#endif
}

PngEncoder::PngEncoder(PngBackend preference) {
    preference = Override(preference);
    const bool accelerated = PngFpngeCompiled() && PngFpngeCpuSupported();
    if (preference == PngBackend::Fpng) {
        backend_ = PngBackend::Fpng;
    } else if (preference == PngBackend::Fpnge) {
        if (!accelerated) WarnUnavailableFpnge();
        backend_ = accelerated ? PngBackend::Fpnge : PngBackend::Fpng;
    } else {
        backend_ = accelerated ? PngBackend::Fpnge : PngBackend::Fpng;
    }
    if (backend_ == PngBackend::Fpnge) fpngeLevel_ = FpngeLevel();
}

const char* PngEncoder::BackendName() const noexcept {
    return PngBackendName(backend_);
}

PngEncodeResult PngEncoder::Encode(const ImageRgba& image,
                                   std::vector<unsigned char>& output,
                                   std::size_t limit) {
    if (!limit || !ValidImage(image)) return {};

#ifdef RENDER_MODULE_HAVE_FPNGE
    if (backend_ == PngBackend::Fpnge) {
        if (EncodeFpnge(image.pixels.data(), image.width, image.height,
                        fpngeLevel_, fpngeScratch_, output, limit))
            return {true, PngBackend::Fpnge, false};

        std::vector<unsigned char> fallback;
        if (EncodeFpng(image.pixels.data(), image.width, image.height, fallback, limit)) {
            output = std::move(fallback);
            return {true, PngBackend::Fpng, true};
        }
        return {};
    }
#endif

    if (!EncodeFpng(image.pixels.data(), image.width, image.height, output, limit)) return {};
    return {true, PngBackend::Fpng, false};
}

} // namespace render_module::detail
