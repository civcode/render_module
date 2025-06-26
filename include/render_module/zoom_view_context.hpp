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

ImVec2 GetOffset() {
    if (currentState) {
        return currentState->offset;
    }
    return ImVec2(0.0f, 0.0f);
}

float GetZoom() {
    if (currentState) {
        return currentState->zoom;
    }
    return 1.0f;    
}
}

#endif // ZOOM_VIEW_CONTEXT_HPP_