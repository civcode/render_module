#include "render_module/image_view.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <utility>

#include <nanovg.h>

#include "image_view_internal.hpp"
#include "render_module/render_context.hpp"

namespace render_module {
namespace {

std::size_t SourceBytesPerPixel(ImagePixelFormat format) {
    switch (format) {
        case ImagePixelFormat::Gray8: return 1;
        case ImagePixelFormat::RGB8:
        case ImagePixelFormat::BGR8: return 3;
        case ImagePixelFormat::RGBA8:
        case ImagePixelFormat::BGRA8: return 4;
    }
    throw std::invalid_argument("ImageFrame: unsupported pixel format");
}

std::size_t CheckedRGBAByteCount(int width, int height) {
    if (width <= 0 || height <= 0) {
        throw std::invalid_argument("ImageFrame: image dimensions must be positive");
    }

    const std::size_t w = static_cast<std::size_t>(width);
    const std::size_t h = static_cast<std::size_t>(height);
    if (w > std::numeric_limits<std::size_t>::max() / 4u / h) {
        throw std::overflow_error("ImageFrame: image dimensions are too large");
    }
    return w * h * 4u;
}

bool IsFinite(Vec2 value) {
    return std::isfinite(value.x) && std::isfinite(value.y);
}

} // namespace

ImageFrame::ImageFrame(int width,
                       int height,
                       std::vector<std::uint8_t> rgba,
                       std::uint64_t sequence,
                       Clock::time_point timestamp) noexcept
    : width_(width),
      height_(height),
      rgba_(std::move(rgba)),
      sequence_(sequence),
      timestamp_(timestamp) {}

std::shared_ptr<const ImageFrame> ImageFrame::Copy(
    int width,
    int height,
    ImagePixelFormat sourceFormat,
    const void* sourcePixels,
    std::size_t sourceRowStrideBytes,
    std::uint64_t sequence,
    Clock::time_point timestamp) {
    const std::size_t rgbaByteCount = CheckedRGBAByteCount(width, height);
    if (!sourcePixels) {
        throw std::invalid_argument("ImageFrame: sourcePixels must not be null");
    }

    const std::size_t sourceBytesPerPixel = SourceBytesPerPixel(sourceFormat);
    const std::size_t packedSourceStride =
        static_cast<std::size_t>(width) * sourceBytesPerPixel;
    if (sourceRowStrideBytes == 0) sourceRowStrideBytes = packedSourceStride;
    if (sourceRowStrideBytes < packedSourceStride) {
        throw std::invalid_argument("ImageFrame: source row stride is too small");
    }

    std::vector<std::uint8_t> rgba(rgbaByteCount);
    const auto* source = static_cast<const std::uint8_t*>(sourcePixels);
    for (int y = 0; y < height; ++y) {
        const std::uint8_t* sourceRow =
            source + static_cast<std::size_t>(y) * sourceRowStrideBytes;
        std::uint8_t* destination =
            rgba.data() + static_cast<std::size_t>(y) *
                              static_cast<std::size_t>(width) * 4u;

        if (sourceFormat == ImagePixelFormat::RGBA8) {
            std::memcpy(destination, sourceRow,
                        static_cast<std::size_t>(width) * 4u);
            continue;
        }

        for (int x = 0; x < width; ++x) {
            const std::uint8_t* pixel =
                sourceRow + static_cast<std::size_t>(x) * sourceBytesPerPixel;
            switch (sourceFormat) {
                case ImagePixelFormat::Gray8:
                    destination[0] = pixel[0];
                    destination[1] = pixel[0];
                    destination[2] = pixel[0];
                    destination[3] = 255;
                    break;
                case ImagePixelFormat::RGB8:
                    destination[0] = pixel[0];
                    destination[1] = pixel[1];
                    destination[2] = pixel[2];
                    destination[3] = 255;
                    break;
                case ImagePixelFormat::BGR8:
                    destination[0] = pixel[2];
                    destination[1] = pixel[1];
                    destination[2] = pixel[0];
                    destination[3] = 255;
                    break;
                case ImagePixelFormat::BGRA8:
                    destination[0] = pixel[2];
                    destination[1] = pixel[1];
                    destination[2] = pixel[0];
                    destination[3] = pixel[3];
                    break;
                case ImagePixelFormat::RGBA8:
                    break;
            }
            destination += 4;
        }
    }

    return std::shared_ptr<const ImageFrame>(
        new ImageFrame(width, height, std::move(rgba), sequence, timestamp));
}

std::shared_ptr<const ImageFrame> ImageFrame::FromRGBA(
    int width,
    int height,
    std::vector<std::uint8_t> rgbaPixels,
    std::uint64_t sequence,
    Clock::time_point timestamp) {
    const std::size_t expectedSize = CheckedRGBAByteCount(width, height);
    if (rgbaPixels.size() != expectedSize) {
        throw std::invalid_argument(
            "ImageFrame: RGBA buffer size does not match image dimensions");
    }
    return std::shared_ptr<const ImageFrame>(
        new ImageFrame(width, height, std::move(rgbaPixels), sequence, timestamp));
}

void LatestImage::Publish(std::shared_ptr<const ImageFrame> frame) noexcept {
    std::atomic_store_explicit(&latest_, std::move(frame), std::memory_order_release);
}

std::shared_ptr<const ImageFrame> LatestImage::Get() const noexcept {
    return std::atomic_load_explicit(&latest_, std::memory_order_acquire);
}

void LatestImage::Clear() noexcept {
    Publish({});
}

namespace detail {

struct ImageViewState {
    float zoom = 1.0f;
    Vec2 pan{};
    bool pointerCaptured = false;
};

} // namespace detail

struct ImageCanvas::Data {
    NVGcontext* graphics = nullptr;
    Vec2 canvasSize{};
    std::shared_ptr<const ImageFrame> frame;
    ImageInput input{};
    ImageViewOptions options{};
    detail::ImageViewState* state = nullptr;
    float baseScale = 1.0f;
    float displayScale = 1.0f;
    Vec2 imageOrigin{};

    void RecomputeTransform() noexcept {
        const Vec2 imageSize = frame ? frame->Size() : Vec2{};
        if (!state || imageSize.x <= 0.0f || imageSize.y <= 0.0f) {
            baseScale = 1.0f;
            displayScale = 1.0f;
            imageOrigin = canvasSize * 0.5f;
            return;
        }

        if (options.initialFit == ImageFit::Contain) {
            baseScale = std::min(canvasSize.x / imageSize.x,
                                 canvasSize.y / imageSize.y);
        } else {
            baseScale = 1.0f;
        }
        baseScale = std::max(baseScale, 0.000001f);
        displayScale = baseScale * state->zoom;
        imageOrigin = canvasSize * 0.5f - imageSize * (displayScale * 0.5f) +
                      state->pan;
    }

    Vec2 ToCanvas(Vec2 imagePosition) const noexcept {
        if (!frame || displayScale <= 0.0f) return {};
        return {
            imageOrigin.x + imagePosition.x * displayScale,
            imageOrigin.y +
                (static_cast<float>(frame->Height()) - imagePosition.y) *
                    displayScale
        };
    }

    Vec2 ToImage(Vec2 canvasPosition) const noexcept {
        if (!frame || displayScale <= 0.0f) return {};
        return {
            (canvasPosition.x - imageOrigin.x) / displayScale,
            static_cast<float>(frame->Height()) -
                (canvasPosition.y - imageOrigin.y) / displayScale
        };
    }

    void RefreshImageInput() noexcept {
        input.imagePosition = ToImage(input.position);
        input.imageDelta = {input.delta.x / displayScale,
                            -input.delta.y / displayScale};
        input.overImage = input.hovered && frame &&
            input.imagePosition.x >= 0.0f && input.imagePosition.y >= 0.0f &&
            input.imagePosition.x < static_cast<float>(frame->Width()) &&
            input.imagePosition.y < static_cast<float>(frame->Height());
    }

    void SetZoomAround(float requestedZoom, Vec2 canvasAnchor) noexcept {
        if (!state || !frame || !std::isfinite(requestedZoom)) return;
        const Vec2 imageAnchor = ToImage(canvasAnchor);
        state->zoom = std::clamp(requestedZoom,
                                 options.minZoom,
                                 options.maxZoom);
        RecomputeTransform();

        const Vec2 imageSize = frame->Size();
        const Vec2 centeredOrigin =
            canvasSize * 0.5f - imageSize * (displayScale * 0.5f);
        const Vec2 desiredOrigin = {
            canvasAnchor.x - imageAnchor.x * displayScale,
            canvasAnchor.y -
                (imageSize.y - imageAnchor.y) * displayScale
        };
        state->pan = desiredOrigin - centeredOrigin;
        RecomputeTransform();
        RefreshImageInput();
    }
};

NVGcontext* ImageCanvas::Graphics() const noexcept {
    return data_ ? data_->graphics : nullptr;
}

Vec2 ImageCanvas::CanvasSize() const noexcept {
    return data_ ? data_->canvasSize : Vec2{};
}

Vec2 ImageCanvas::ImageSize() const noexcept {
    return data_ && data_->frame ? data_->frame->Size() : Vec2{};
}

const ImageInput& ImageCanvas::Input() const noexcept {
    static const ImageInput empty;
    return data_ ? data_->input : empty;
}

std::shared_ptr<const ImageFrame> ImageCanvas::Frame() const noexcept {
    return data_ ? data_->frame : nullptr;
}

Vec2 ImageCanvas::ImageToCanvas(Vec2 imagePosition) const noexcept {
    return data_ ? data_->ToCanvas(imagePosition) : Vec2{};
}

Vec2 ImageCanvas::CanvasToImage(Vec2 canvasPosition) const noexcept {
    return data_ ? data_->ToImage(canvasPosition) : Vec2{};
}

bool ImageCanvas::IsHovered() const noexcept {
    return data_ && data_->input.overImage;
}

Vec2 ImageCanvas::MouseImagePosition() const noexcept {
    return data_ ? data_->input.imagePosition : Vec2{};
}

float ImageCanvas::Zoom() const noexcept {
    return data_ && data_->state ? data_->state->zoom : 1.0f;
}

Vec2 ImageCanvas::Pan() const noexcept {
    return data_ && data_->state ? data_->state->pan : Vec2{};
}

void ImageCanvas::SetZoom(float zoom) {
    if (!data_) return;
    data_->SetZoomAround(zoom, data_->canvasSize * 0.5f);
}

void ImageCanvas::SetPan(Vec2 pan) noexcept {
    if (!data_ || !data_->state || !IsFinite(pan)) return;
    data_->state->pan = pan;
    data_->RecomputeTransform();
    data_->RefreshImageInput();
}

void ImageCanvas::ResetView() noexcept {
    if (!data_ || !data_->state) return;
    data_->state->zoom = 1.0f;
    data_->state->pan = {};
    data_->RecomputeTransform();
    data_->RefreshImageInput();
}

void ImageCanvas::CapturePointer() noexcept {
    if (data_ && data_->state) data_->state->pointerCaptured = true;
}

bool ImageCanvas::HasPointerCapture() const noexcept {
    return data_ && data_->state && data_->state->pointerCaptured;
}

namespace detail {
namespace {

class ImageViewRenderer {
public:
    ImageViewRenderer(ImageProvider provider,
                      ImageViewCallback overlayCallback,
                      ImageViewOptions requestedOptions)
        : provider_(std::move(provider)),
          overlayCallback_(std::move(overlayCallback)),
          options_(requestedOptions) {
        options_.minZoom = std::max(options_.minZoom, 0.0001f);
        options_.maxZoom = std::max(options_.maxZoom, options_.minZoom);
        options_.zoomStep = std::max(options_.zoomStep, 1.001f);
    }

    ~ImageViewRenderer() {
        if (graphics_ && imageHandle_ >= 0) {
            nvgDeleteImage(graphics_, imageHandle_);
        }
    }

    void Draw(Canvas& canvas) {
        NVGcontext* vg = canvas.Graphics();
        if (!vg) return;
        graphics_ = vg;

        std::shared_ptr<const ImageFrame> frame = provider_ ? provider_() : nullptr;
        UpdateTexture(vg, frame);

        bool anyButtonDown = false;
        for (const MouseButtonState& button : canvas.Input().buttons) {
            anyButtonDown = anyButtonDown || button.down;
        }
        if (!anyButtonDown) state_.pointerCaptured = false;

        ImageCanvas::Data data;
        data.graphics = vg;
        data.canvasSize = canvas.Size();
        data.frame = std::move(frame);
        data.options = options_;
        data.state = &state_;
        static_cast<CanvasInput&>(data.input) = canvas.Input();
        data.RecomputeTransform();
        data.RefreshImageInput();

        ApplyNavigation(data);
        DrawBackground(vg, data.canvasSize);
        DrawImage(vg, data);

        ImageCanvas imageCanvas = ImageCanvasAccess::Make(data);
        if (overlayCallback_) {
            nvgSave(vg);
            if (options_.clipOverlayToImage && data.frame) {
                const Vec2 imageSize = data.frame->Size();
                nvgIntersectScissor(vg,
                                    data.imageOrigin.x,
                                    data.imageOrigin.y,
                                    imageSize.x * data.displayScale,
                                    imageSize.y * data.displayScale);
            }
            try {
                overlayCallback_(imageCanvas);
            } catch (...) {
                nvgRestore(vg);
                throw;
            }
            nvgRestore(vg);
        }

        if (options_.showStatus) canvas.StatusText(StatusText(data));
    }

private:
    void UpdateTexture(NVGcontext* vg,
                       const std::shared_ptr<const ImageFrame>& frame) {
        if (!frame || frame.get() == uploadedFrame_.get()) return;

        const bool dimensionsChanged = !uploadedFrame_ ||
            uploadedFrame_->Width() != frame->Width() ||
            uploadedFrame_->Height() != frame->Height();

        if (dimensionsChanged || imageHandle_ < 0) {
            if (imageHandle_ >= 0) nvgDeleteImage(vg, imageHandle_);
            int flags = NVG_IMAGE_FLIPY;
            if (options_.sampling == ImageSampling::Nearest) {
                flags |= NVG_IMAGE_NEAREST;
            }
            imageHandle_ = nvgCreateImageRGBA(vg,
                                               frame->Width(),
                                               frame->Height(),
                                               flags,
                                               frame->PixelsRGBA());
            if (options_.resetViewOnImageSizeChange) {
                state_.zoom = 1.0f;
                state_.pan = {};
            }
        } else {
            nvgUpdateImage(vg, imageHandle_, frame->PixelsRGBA());
        }
        uploadedFrame_ = frame;
    }

    void ApplyNavigation(ImageCanvas::Data& data) {
        if (!data.frame || state_.pointerCaptured ||
            RenderContext::Instance().disableViewportControls) {
            return;
        }

        if (options_.enableZoom && data.input.overImage &&
            data.input.wheel != 0.0f) {
            const float requestedZoom =
                state_.zoom * std::pow(options_.zoomStep, data.input.wheel);
            data.SetZoomAround(requestedZoom, data.input.position);
        }

        const MouseButtonState& pan = data.input.Button(options_.panButton);
        if (options_.enablePan && data.input.active && pan.dragging && !pan.pressed) {
            state_.pan = state_.pan + data.input.delta;
            data.RecomputeTransform();
            data.RefreshImageInput();
        }
    }

    void DrawBackground(NVGcontext* vg, Vec2 size) const {
        const auto channel = [](float value) {
            return static_cast<unsigned char>(
                std::round(std::clamp(value, 0.0f, 1.0f) * 255.0f));
        };
        nvgBeginPath(vg);
        nvgRect(vg, 0.0f, 0.0f, size.x, size.y);
        nvgFillColor(vg, nvgRGBA(channel(options_.background.r),
                                 channel(options_.background.g),
                                 channel(options_.background.b),
                                 channel(options_.background.a)));
        nvgFill(vg);
    }

    void DrawImage(NVGcontext* vg, const ImageCanvas::Data& data) const {
        if (!data.frame || imageHandle_ < 0) return;
        const Vec2 imageSize = data.frame->Size();
        const float width = imageSize.x * data.displayScale;
        const float height = imageSize.y * data.displayScale;
        const NVGpaint paint = nvgImagePattern(vg,
                                               data.imageOrigin.x,
                                               data.imageOrigin.y,
                                               width,
                                               height,
                                               0.0f,
                                               imageHandle_,
                                               1.0f);
        nvgBeginPath(vg);
        nvgRect(vg, data.imageOrigin.x, data.imageOrigin.y, width, height);
        nvgFillPaint(vg, paint);
        nvgFill(vg);
    }

    std::string StatusText(const ImageCanvas::Data& data) const {
        if (!data.frame) return "waiting for image";

        char text[192];
        if (data.input.overImage) {
            std::snprintf(text, sizeof(text),
                          "%dx%d  frame %llu  zoom %.3gx  pixel (%.1f, %.1f)",
                          data.frame->Width(), data.frame->Height(),
                          static_cast<unsigned long long>(data.frame->Sequence()),
                          state_.zoom,
                          data.input.imagePosition.x,
                          data.input.imagePosition.y);
        } else {
            std::snprintf(text, sizeof(text),
                          "%dx%d  frame %llu  zoom %.3gx",
                          data.frame->Width(), data.frame->Height(),
                          static_cast<unsigned long long>(data.frame->Sequence()),
                          state_.zoom);
        }
        return text;
    }

    ImageProvider provider_;
    ImageViewCallback overlayCallback_;
    ImageViewOptions options_{};
    ImageViewState state_{};
    NVGcontext* graphics_ = nullptr;
    int imageHandle_ = -1;
    std::shared_ptr<const ImageFrame> uploadedFrame_;
};

} // namespace

std::function<void(Canvas&)> MakeImageViewRenderer(
    ImageProvider provider,
    ImageViewCallback overlayCallback,
    ImageViewOptions options) {
    auto renderer = std::make_shared<ImageViewRenderer>(
        std::move(provider), std::move(overlayCallback), options);
    return [renderer = std::move(renderer)](Canvas& canvas) {
        renderer->Draw(canvas);
    };
}

} // namespace detail
} // namespace render_module
