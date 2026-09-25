#pragma once

#include "presenter.hpp"
#include <string>
#include <utility>
#include <vector>

namespace render_module::detail {

struct ImageRgba {
    int width = 0, height = 0;
    std::vector<unsigned char> pixels; // Tightly packed RGBA8, TOP row first.
};

// Diagnostic synchronous readback only. A path-less presenter merely submits
// GPU work; captures are explicit, avoiding per-frame readback when not requested.
class ImagePresenter final : public IPresenter {
public:
    explicit ImagePresenter(std::string path = {}) : path_(std::move(path)) {}
    bool Present(const PresentedFrame& frame) override;
    static bool Read(const PresentedFrame& frame, ImageRgba& image);
    // Caller-owned reusable storage, same one-flip/top-down and GL-state policy.
    static bool ReadInto(const PresentedFrame& frame, unsigned char* data, std::size_t capacity, int stride);
    static bool WritePng(const std::string& path, const ImageRgba& image);
private:
    std::string path_;
};

} // namespace render_module::detail
