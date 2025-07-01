#ifndef ZOOM_VIEW_CONTEXT_HPP_
#define ZOOM_VIEW_CONTEXT_HPP_

#include <sstream>
#include <unordered_map>

#include <imgui.h>

namespace ZoomView {

enum Flags {
    kNone = 0,
    // kDisableZoom = 1 << 0,
    // kDisablePan = 1 << 1,
    // kDisableMouseWheel = 1 << 2,
    // kDisableKeyboard = 1 << 3,
    // kDisableDrag = 1 << 4,
    kOnceOnly = 1 << 5
};

struct State {
    float zoom = 1.0f;
    ImVec2 offset = {0.0f, 0.0f};
    float minZoom = 0.50f;
    float maxZoom = 16.0f;
    float zoomFactor = 2.0f;
    Flags offset_flags = Flags::kNone;
    Flags scale_flags = Flags::kNone;
};


extern State* currentState;

extern std::unordered_map<std::string, State> stateMap;

inline ImVec2 GetOffset() {
    if (currentState) {
        return currentState->offset;
    }
    return ImVec2(0.0f, 0.0f);
}

inline void SetOffset(const ImVec2& offset, Flags flags = Flags::kNone) {
    if ((flags & Flags::kOnceOnly) == Flags::kOnceOnly) {
        if (currentState && currentState->offset_flags & Flags::kOnceOnly) {
            return; // Do not change offset if already set once
        }
    }
    if (currentState) {
        currentState->offset = offset;
        currentState->offset_flags = flags;
    }
}

inline float GetScale() {
    if (currentState) {
        return currentState->zoom;
    }
    return 1.0f;    
}

inline void SetScale(float scale, Flags flags = Flags::kNone) {
    if ((flags & Flags::kOnceOnly) == Flags::kOnceOnly) {
        if (currentState && currentState->scale_flags & Flags::kOnceOnly) {
            return; // Do not change scale if already set once
        }
    }
    if (currentState) {
        currentState->zoom = scale;
        currentState->scale_flags = flags;
    }
}

inline ImVec2 CanvasToView(const ImVec2& canvasPos) {
    if (currentState) {
        return ImVec2(
            (canvasPos.x - currentState->offset.x) / currentState->zoom,
            (canvasPos.y - currentState->offset.y) / currentState->zoom
        );
    }
    return canvasPos;
}

template <typename T>
inline void CanvasToView(T& x, T& y) {
    if (currentState) {
        x = (x - currentState->offset.x) / currentState->zoom;
        y = (y - currentState->offset.y) / currentState->zoom;
    }
}

template <typename T>
inline void CanvasToView(T& distance) {
    if (currentState) {
        distance /= currentState->zoom;
    }
}

inline ImVec2 ViewToCanvas(const ImVec2& zoomViewPos) {
    if (currentState) {
        return ImVec2(
            zoomViewPos.x * currentState->zoom + currentState->offset.x,
            zoomViewPos.y * currentState->zoom + currentState->offset.y
        );
    }
    return zoomViewPos;
}

template <typename T>
inline void ViewToCanvas(T& x, T& y) {
    if (currentState) {
        x = x * currentState->zoom + currentState->offset.x;
        y = y * currentState->zoom + currentState->offset.y;
    }
}

template <typename T>
inline void ViewToCanvas(T& distance) {
    if (currentState) {
        distance *= currentState->zoom;
    }
}

} // namespace ZoomView

#endif // ZOOM_VIEW_CONTEXT_HPP_