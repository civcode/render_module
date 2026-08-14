#ifndef RENDER_MODULE_IMAGE_VIEW_HPP_
#define RENDER_MODULE_IMAGE_VIEW_HPP_

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include "render_module/canvas.hpp"

struct NVGcontext;

namespace render_module {

// Source formats accepted by ImageFrame::Copy(). ImageFrame normalizes the
// copied data to tightly packed RGBA so render callbacks never depend on the
// lifetime, row stride, or channel order of an OpenCV cv::Mat.
enum class ImagePixelFormat {
    Gray8,
    RGB8,
    BGR8,
    RGBA8,
    BGRA8
};

class ImageFrame {
public:
    using Clock = std::chrono::steady_clock;

    // Copies source pixels immediately. sourceRowStrideBytes may be zero for a
    // tightly packed image. The resulting frame is immutable and safe to pass
    // from a producer thread to the render thread.
    static std::shared_ptr<const ImageFrame> Copy(
        int width,
        int height,
        ImagePixelFormat sourceFormat,
        const void* sourcePixels,
        std::size_t sourceRowStrideBytes = 0,
        std::uint64_t sequence = 0,
        Clock::time_point timestamp = Clock::now());

    // Takes ownership of an already tightly packed RGBA8 buffer.
    static std::shared_ptr<const ImageFrame> FromRGBA(
        int width,
        int height,
        std::vector<std::uint8_t> rgbaPixels,
        std::uint64_t sequence = 0,
        Clock::time_point timestamp = Clock::now());

    int Width() const noexcept { return width_; }
    int Height() const noexcept { return height_; }
    Vec2 Size() const noexcept {
        return {static_cast<float>(width_), static_cast<float>(height_)};
    }
    const std::uint8_t* PixelsRGBA() const noexcept { return rgba_.data(); }
    std::size_t RowStrideBytes() const noexcept {
        return static_cast<std::size_t>(width_) * 4u;
    }
    std::uint64_t Sequence() const noexcept { return sequence_; }
    Clock::time_point Timestamp() const noexcept { return timestamp_; }

private:
    ImageFrame(int width,
               int height,
               std::vector<std::uint8_t> rgba,
               std::uint64_t sequence,
               Clock::time_point timestamp) noexcept;

    int width_ = 0;
    int height_ = 0;
    std::vector<std::uint8_t> rgba_;
    std::uint64_t sequence_ = 0;
    Clock::time_point timestamp_{};
};

// A single-slot, latest-wins handoff. Publish() may be called by a camera or
// processing thread while Get() is called from RenderModule's render thread.
class LatestImage {
public:
    void Publish(std::shared_ptr<const ImageFrame> frame) noexcept;
    std::shared_ptr<const ImageFrame> Get() const noexcept;
    void Clear() noexcept;

private:
    mutable std::shared_ptr<const ImageFrame> latest_;
};

enum class ImageFit {
    Contain,
    ActualPixels
};

enum class ImageSampling {
    Linear,
    Nearest
};

struct ImageViewColor {
    float r = 0.075f;
    float g = 0.085f;
    float b = 0.105f;
    float a = 1.0f;
};

struct ImageViewOptions {
    bool enablePan = true;
    bool enableZoom = true;
    bool showStatus = true;
    bool clipOverlayToImage = true;
    bool resetViewOnImageSizeChange = true;

    MouseButton panButton = MouseButton::Middle;
    ImageFit initialFit = ImageFit::Contain;
    ImageSampling sampling = ImageSampling::Linear;

    // Zoom is relative to the fitted or actual-pixel initial view.
    float minZoom = 0.05f;
    float maxZoom = 100.0f;
    float zoomStep = 1.25f;
    ImageViewColor background{};
};

// Image coordinates follow OpenCV convention: (0, 0) is the upper-left pixel,
// +X points right, and +Y points down. CanvasInput members retain RenderModule's
// lower-left, +Y-up canvas convention.
struct ImageInput : CanvasInput {
    bool overImage = false;
    Vec2 imagePosition{};
    Vec2 imageDelta{};
};

namespace detail {
struct ImageCanvasAccess;
}

class ImageCanvas {
public:
    struct Data;

    NVGcontext* Graphics() const noexcept;
    Vec2 CanvasSize() const noexcept;
    Vec2 ImageSize() const noexcept;
    const ImageInput& Input() const noexcept;
    std::shared_ptr<const ImageFrame> Frame() const noexcept;

    // These return canvas-local screen coordinates: lower-left origin, +Y up.
    Vec2 ImageToCanvas(Vec2 imagePosition) const noexcept;
    Vec2 CanvasToImage(Vec2 canvasPosition) const noexcept;
    Vec2 ImageToScreen(Vec2 imagePosition) const noexcept {
        return ImageToCanvas(imagePosition);
    }
    Vec2 ScreenToImage(Vec2 screenPosition) const noexcept {
        return CanvasToImage(screenPosition);
    }

    // True only when the ImGui item is hovered and the pointer is over the
    // displayed image, not merely over a letterboxed part of the window.
    bool IsHovered() const noexcept;
    Vec2 MouseImagePosition() const noexcept;

    // Zoom is relative to the initial fit. Pan is measured in logical canvas
    // pixels, with +Y up.
    float Zoom() const noexcept;
    Vec2 Pan() const noexcept;
    void SetZoom(float zoom);
    void SetPan(Vec2 pan) noexcept;
    void ResetView() noexcept;

    // Call while an application gesture owns a mouse drag. Image navigation is
    // suppressed until every mouse button has been released.
    void CapturePointer() noexcept;
    bool HasPointerCapture() const noexcept;

private:
    explicit ImageCanvas(Data* data) noexcept : data_(data) {}
    Data* data_ = nullptr;

    friend struct detail::ImageCanvasAccess;
};

using ImageProvider = std::function<std::shared_ptr<const ImageFrame>()>;
using ImageViewCallback = std::function<void(ImageCanvas&)>;

} // namespace render_module

#endif // RENDER_MODULE_IMAGE_VIEW_HPP_
