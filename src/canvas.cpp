#include "render_module/canvas.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <utility>

#include <nanovg.h>

#include "canvas_internal.hpp"
#include "render_module/nvg_wrapper.hpp"
#include "render_module/render_context.hpp"

namespace render_module {

struct Viewport::Data {
    detail::CanvasStorage* canvas = nullptr;
    detail::ViewState* state = nullptr;
    ViewportInput input{};
    ViewportOptions options{};
};

namespace detail {
namespace {
thread_local Canvas* currentCanvas = nullptr;
thread_local Viewport* currentViewport = nullptr;
} // namespace

Canvas* CurrentCanvas() noexcept { return currentCanvas; }
Viewport* CurrentViewport() noexcept { return currentViewport; }
void SetCurrentCanvas(Canvas* canvas) noexcept { currentCanvas = canvas; }
void SetCurrentViewport(Viewport* viewport) noexcept { currentViewport = viewport; }
} // namespace detail

Viewport::Viewport(Data* data) noexcept : data_(data) {}

NVGcontext* Viewport::Graphics() const noexcept {
    return data_ ? data_->canvas->graphics : nullptr;
}

const ViewportInput& Viewport::Input() const noexcept {
    static const ViewportInput empty;
    return data_ ? data_->input : empty;
}

Vec2 Viewport::WorldToCanvas(Vec2 world) const noexcept {
    if (!data_) return world;
    return world * data_->state->scale + data_->state->offset;
}

Vec2 Viewport::CanvasToWorld(Vec2 canvas) const noexcept {
    if (!data_) return canvas;
    return (canvas - data_->state->offset) / data_->state->scale;
}

float Viewport::Scale() const noexcept {
    return data_ ? data_->state->scale : 1.0f;
}

Vec2 Viewport::Offset() const noexcept {
    return data_ ? data_->state->offset : Vec2{};
}

void Viewport::SetScale(float scale) {
    if (!data_ || !std::isfinite(scale)) return;
    data_->state->scale = std::clamp(scale, data_->options.minScale, data_->options.maxScale);
    data_->state->scaleSetOnce = false;
}

void Viewport::SetOffset(Vec2 offset) noexcept {
    if (!data_) return;
    data_->state->offset = offset;
    data_->state->offsetSetOnce = false;
}

bool Viewport::SetScaleOnce(float scale) {
    if (!data_ || data_->state->scaleSetOnce) return false;
    if (!std::isfinite(scale)) return false;
    data_->state->scale = std::clamp(scale, data_->options.minScale, data_->options.maxScale);
    data_->state->scaleSetOnce = true;
    return true;
}

bool Viewport::SetOffsetOnce(Vec2 offset) noexcept {
    if (!data_ || data_->state->offsetSetOnce) return false;
    data_->state->offset = offset;
    data_->state->offsetSetOnce = true;
    return true;
}

void Viewport::ResetView() noexcept {
    if (!data_) return;
    data_->state->scale = 1.0f;
    data_->state->offset = {};
    data_->state->scaleSetOnce = false;
    data_->state->offsetSetOnce = false;
}

void Viewport::CapturePointer() noexcept {
    if (data_) data_->state->pointerCaptured = true;
}

bool Viewport::HasPointerCapture() const noexcept {
    return data_ && data_->state->pointerCaptured;
}

Canvas::Canvas(detail::CanvasStorage* storage) noexcept : storage_(storage) {}

NVGcontext* Canvas::Graphics() const noexcept {
    return storage_ ? storage_->graphics : nullptr;
}

Vec2 Canvas::Size() const noexcept {
    return storage_ ? storage_->size : Vec2{};
}

const CanvasInput& Canvas::Input() const noexcept {
    static const CanvasInput empty;
    return storage_ ? storage_->input : empty;
}

void Canvas::StatusText(std::string text, Vec2 position) {
    if (!storage_ || text.empty()) return;
    storage_->overlays.push_back({position, std::move(text)});
}

void Canvas::DrawViewport(const std::string& id,
                          const ViewportCallback& callback,
                          const ViewportOptions& requestedOptions) {
    if (!storage_ || !storage_->graphics || !callback) return;
    if (id.empty()) throw std::invalid_argument("Canvas::DrawViewport requires a non-empty id");

    ViewportOptions options = requestedOptions;
    options.minScale = std::max(options.minScale, 0.0001f);
    options.maxScale = std::max(options.maxScale, options.minScale);
    options.zoomStep = std::max(options.zoomStep, 1.001f);

    detail::ViewState& state = storage_->views[id];
    state.scale = std::clamp(state.scale, options.minScale, options.maxScale);

    bool anyButtonDown = false;
    for (const MouseButtonState& button : storage_->input.buttons) {
        anyButtonDown = anyButtonDown || button.down;
    }
    if (!anyButtonDown) state.pointerCaptured = false;

    const bool navigationEnabled = !RenderContext::Instance().disableViewportControls;
    if (navigationEnabled && options.enableZoom && storage_->input.hovered &&
        storage_->input.wheel != 0.0f && !state.pointerCaptured) {
        const Vec2 worldUnderMouse = (storage_->input.position - state.offset) / state.scale;
        const float factor = std::pow(options.zoomStep, storage_->input.wheel);
        state.scale = std::clamp(state.scale * factor, options.minScale, options.maxScale);
        state.offset = storage_->input.position - worldUnderMouse * state.scale;
    }

    const MouseButtonState& pan = storage_->input.Button(options.panButton);
    if (navigationEnabled && options.enablePan && !state.pointerCaptured &&
        storage_->input.active && pan.dragging && !pan.pressed) {
        state.offset = state.offset + storage_->input.delta;
    }

    Viewport::Data data;
    data.canvas = storage_;
    data.state = &state;
    data.options = options;
    static_cast<CanvasInput&>(data.input) = storage_->input;
    data.input.worldPosition = (storage_->input.position - state.offset) / state.scale;
    data.input.worldDelta = storage_->input.delta / state.scale;

    Viewport viewport(&data);
    Viewport* previousViewport = detail::CurrentViewport();
    detail::SetCurrentViewport(&viewport);

    NVGcontext* vg = storage_->graphics;
    nvg::SetContext(vg);
    nvgSave(vg);
    nvgTranslate(vg, state.offset.x, state.offset.y);
    nvgScale(vg, state.scale, state.scale);
    try {
        callback(viewport);
    } catch (...) {
        nvgRestore(vg);
        detail::SetCurrentViewport(previousViewport);
        throw;
    }
    nvgRestore(vg);
    detail::SetCurrentViewport(previousViewport);

    if (options.showStatus) {
        char status[160];
        std::snprintf(status, sizeof(status),
                      "scale %.3g  offset (%.1f, %.1f)  cursor (%.2f, %.2f)",
                      state.scale, state.offset.x, state.offset.y,
                      data.input.worldPosition.x, data.input.worldPosition.y);
        storage_->overlays.push_back({{6.0f, 10.0f}, status});
    }
}

} // namespace render_module
