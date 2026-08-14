#ifndef ZOOM_VIEW_CONTEXT_HPP_
#define ZOOM_VIEW_CONTEXT_HPP_

#include <imgui.h>

#include "render_module/canvas.hpp"

// Compatibility helpers for existing RenderModule::ZoomView callbacks. New
// code should accept a render_module::Viewport& through Canvas::DrawViewport.
namespace ZoomView {

enum Flags {
    kNone = 0,
    kOnceOnly = 1 << 5
};

ImVec2 GetOffset();
void SetOffset(const ImVec2& offset, Flags flags = kNone);
float GetScale();
void SetScale(float scale, Flags flags = kNone);

ImVec2 CanvasToView(const ImVec2& canvasPos);
ImVec2 ViewToCanvas(const ImVec2& viewPos);

template <typename T>
inline void CanvasToView(T& x, T& y) {
    const ImVec2 converted = CanvasToView(ImVec2(static_cast<float>(x), static_cast<float>(y)));
    x = static_cast<T>(converted.x);
    y = static_cast<T>(converted.y);
}

template <typename T>
inline void CanvasToView(T& distance) {
    distance = static_cast<T>(distance / GetScale());
}

template <typename T>
inline void ViewToCanvas(T& x, T& y) {
    const ImVec2 converted = ViewToCanvas(ImVec2(static_cast<float>(x), static_cast<float>(y)));
    x = static_cast<T>(converted.x);
    y = static_cast<T>(converted.y);
}

template <typename T>
inline void ViewToCanvas(T& distance) {
    distance = static_cast<T>(distance * GetScale());
}

const render_module::ViewportInput* GetInput() noexcept;
void CapturePointer() noexcept;

} // namespace ZoomView

#endif // ZOOM_VIEW_CONTEXT_HPP_
