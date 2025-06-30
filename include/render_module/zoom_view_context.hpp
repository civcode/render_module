#ifndef ZOOM_VIEW_CONTEXT_HPP_
#define ZOOM_VIEW_CONTEXT_HPP_

#include <sstream>
#include <unordered_map>

#include <imgui.h>

namespace ZoomView {

struct State {
    float zoom = 1.0f;
    ImVec2 offset = {0.0f, 0.0f};
};


extern State* currentState;

extern std::unordered_map<std::string, State> stateMap;

inline ImVec2 GetOffset() {
    if (currentState) {
        return currentState->offset;
    }
    return ImVec2(0.0f, 0.0f);
}

inline float GetScale() {
    if (currentState) {
        return currentState->zoom;
    }
    return 1.0f;    
}

inline ImVec2 CanvasToZoomView(const ImVec2& canvasPos) {
    if (currentState) {
        return ImVec2(
            (canvasPos.x - currentState->offset.x) / currentState->zoom,
            (canvasPos.y - currentState->offset.y) / currentState->zoom
        );
    }
    return canvasPos;
}

template <typename T>
inline void CanvasToZoomView(T& x, T& y) {
    if (currentState) {
        x = (x - currentState->offset.x) / currentState->zoom;
        y = (y - currentState->offset.y) / currentState->zoom;
    }
}

inline ImVec2 ZoomViewToCanvas(const ImVec2& zoomViewPos) {
    if (currentState) {
        return ImVec2(
            zoomViewPos.x * currentState->zoom + currentState->offset.x,
            zoomViewPos.y * currentState->zoom + currentState->offset.y
        );
    }
    return zoomViewPos;
}

template <typename T>
inline void ZoomViewToCanvas(T& x, T& y) {
    if (currentState) {
        x = x * currentState->zoom + currentState->offset.x;
        y = y * currentState->zoom + currentState->offset.y;
    }
}

} // namespace ZoomView

#endif // ZOOM_VIEW_CONTEXT_HPP_