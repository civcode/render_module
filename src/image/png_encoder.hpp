#pragma once

#include "present/image_presenter.hpp"
#include <cstddef>
#include <vector>

namespace render_module::detail {

enum class PngBackend {
    Auto,
    Fpnge,
    Fpng
};

struct PngEncodeResult {
    bool ok = false;
    PngBackend backend = PngBackend::Fpng;
    bool fellBack = false;
};

class PngEncoder {
public:
    explicit PngEncoder(PngBackend preference = PngBackend::Auto);

    PngBackend Backend() const noexcept { return backend_; }
    const char* BackendName() const noexcept;
    int FpngeLevel() const noexcept { return fpngeLevel_; }

    PngEncodeResult Encode(const ImageRgba& image,
                           std::vector<unsigned char>& output,
                           std::size_t limit);

private:
    PngBackend backend_ = PngBackend::Fpng;
    int fpngeLevel_ = 4;
    std::vector<unsigned char> fpngeScratch_;
};

const char* PngBackendName(PngBackend backend) noexcept;
bool PngFpngeCompiled() noexcept;
bool PngFpngeCpuSupported() noexcept;

} // namespace render_module::detail
