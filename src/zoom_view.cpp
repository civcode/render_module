#include "render_module/zoom_view.hpp"

#include <iostream>

#include "canvas_internal.hpp"
#include "render_module/zoom_view_context.hpp"

namespace ZoomView {

void Draw(const std::string& label,
          NVGcontext*,
          std::function<void(NVGcontext*)> drawCallback) {
    render_module::Canvas* canvas = render_module::detail::CurrentCanvas();
    if (!canvas) {
        std::cerr << "ZoomView::Draw must be called from a RenderModule canvas callback.\n";
        return;
    }

    canvas->DrawViewport(label, [&](render_module::Viewport& viewport) {
        drawCallback(viewport.Graphics());
    });
}

ImVec2 GetOffset() {
    const render_module::Viewport* viewport = render_module::detail::CurrentViewport();
    if (!viewport) return ImVec2(0.0f, 0.0f);
    const render_module::Vec2 offset = viewport->Offset();
    return ImVec2(offset.x, offset.y);
}

void SetOffset(const ImVec2& offset, Flags flags) {
    render_module::Viewport* viewport = render_module::detail::CurrentViewport();
    if (!viewport) return;
    const render_module::Vec2 value{offset.x, offset.y};
    if ((flags & kOnceOnly) == kOnceOnly) viewport->SetOffsetOnce(value);
    else viewport->SetOffset(value);
}

float GetScale() {
    const render_module::Viewport* viewport = render_module::detail::CurrentViewport();
    return viewport ? viewport->Scale() : 1.0f;
}

void SetScale(float scale, Flags flags) {
    render_module::Viewport* viewport = render_module::detail::CurrentViewport();
    if (!viewport) return;
    if ((flags & kOnceOnly) == kOnceOnly) viewport->SetScaleOnce(scale);
    else viewport->SetScale(scale);
}

ImVec2 CanvasToView(const ImVec2& canvasPos) {
    const render_module::Viewport* viewport = render_module::detail::CurrentViewport();
    if (!viewport) return canvasPos;
    const render_module::Vec2 value = viewport->CanvasToWorld({canvasPos.x, canvasPos.y});
    return ImVec2(value.x, value.y);
}

ImVec2 ViewToCanvas(const ImVec2& viewPos) {
    const render_module::Viewport* viewport = render_module::detail::CurrentViewport();
    if (!viewport) return viewPos;
    const render_module::Vec2 value = viewport->WorldToCanvas({viewPos.x, viewPos.y});
    return ImVec2(value.x, value.y);
}

const render_module::ViewportInput* GetInput() noexcept {
    const render_module::Viewport* viewport = render_module::detail::CurrentViewport();
    return viewport ? &viewport->Input() : nullptr;
}

void CapturePointer() noexcept {
    render_module::Viewport* viewport = render_module::detail::CurrentViewport();
    if (viewport) viewport->CapturePointer();
}

} // namespace ZoomView
