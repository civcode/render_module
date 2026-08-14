#ifndef RENDER_MODULE_CANVAS_INTERNAL_HPP_
#define RENDER_MODULE_CANVAS_INTERNAL_HPP_

#include <unordered_map>
#include <vector>

#include "render_module/canvas.hpp"

namespace render_module::detail {

struct ViewState {
    float scale = 1.0f;
    Vec2 offset{};
    bool pointerCaptured = false;
    bool scaleSetOnce = false;
    bool offsetSetOnce = false;
};

struct CanvasOverlay {
    // Screen-space position relative to the canvas's top-left corner.
    Vec2 position{};
    std::string text;
};

struct CanvasStorage {
    NVGcontext* graphics = nullptr;
    Vec2 size{};
    CanvasInput input{};
    std::unordered_map<std::string, ViewState> views;
    std::vector<CanvasOverlay> overlays;
};

struct CanvasAccess {
    static Canvas Make(CanvasStorage& storage) noexcept { return Canvas(&storage); }
};

Canvas* CurrentCanvas() noexcept;
Viewport* CurrentViewport() noexcept;
void SetCurrentCanvas(Canvas* canvas) noexcept;
void SetCurrentViewport(Viewport* viewport) noexcept;

} // namespace render_module::detail

#endif // RENDER_MODULE_CANVAS_INTERNAL_HPP_
