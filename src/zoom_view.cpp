#include "render_module/zoom_view.hpp"
#include <cmath>
// #include <unordered_map>
// #include <sstream>
#include <iomanip>

#include "render_module/render_context.hpp"
#include "render_module/zoom_view_context.hpp"

namespace ZoomView {

// struct State {
//     float zoom = 1.0f;
//     ImVec2 offset = {0.0f, 0.0f};
// };

State* currentState = nullptr;

std::unordered_map<std::string, State> stateMap;

void Draw(const std::string& label, NVGcontext* vg, std::function<void(NVGcontext*)> drawCallback) {
    State& state = stateMap[label];
    currentState = &state;

    ImVec2 canvasPos = ImGui::GetCursorScreenPos();
    ImVec2 canvasSize = ImGui::GetContentRegionAvail();

    if (canvasSize.x <= 0.0f || canvasSize.y <= 0.0f)
        return;

    ImVec2 cursorBackup = ImGui::GetCursorPos();

    // Setup for interaction
    ImGui::SetCursorScreenPos(canvasPos);
    ImGui::InvisibleButton(("##ZoomBtn_" + label).c_str(), canvasSize,
        ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);

    ImGui::SetCursorPos(cursorBackup);

    bool hovering = ImGui::IsItemHovered();
    bool active = ImGui::IsItemActive();  
    ImVec2 delta = ImGui::GetIO().MouseDelta;

    if (hovering && ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem)) {
        float wheel = ImGui::GetIO().MouseWheel;
        if (wheel != 0.0f) {
            float zoomFactor = 1.3f;
            float prevZoom = state.zoom;
            float newZoom = (wheel > 0) ? state.zoom * zoomFactor : state.zoom / zoomFactor;
            newZoom = std::max(0.27f, newZoom); // Prevent zooming out too much

            ImVec2 mouse = ImGui::GetIO().MousePos;
            ImVec2 canvasTopLeft = ImGui::GetCursorScreenPos();
            ImVec2 canvasSize = ImGui::GetContentRegionAvail();

            // Get mouse position inside the canvas (flipping Y for NanoVG's origin)
            float mouseX = mouse.x - canvasTopLeft.x;
            float mouseY = canvasSize.y - (mouse.y - canvasTopLeft.y);  // <- Y flip

            // Convert to unscaled content coordinates
            ImVec2 contentPosBefore = ImVec2(
                (mouseX - state.offset.x) / prevZoom,
                (mouseY - state.offset.y) / prevZoom
            );

            if (!RenderContext::Instance().disableViewportControls) {
                state.zoom = newZoom;

                // Compute offset so that the content under the cursor stays fixed
                state.offset.x = std::floor(mouseX - contentPosBefore.x * state.zoom);
                state.offset.y = std::floor(mouseY - contentPosBefore.y * state.zoom);
            }
        }
    }

    if (hovering && active && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        if (!RenderContext::Instance().disableViewportControls) {
            state.offset.x += std::floor(delta.x);
            state.offset.y -= std::floor(delta.y); // Flip Y for NanoVG
        }
    }

    // Apply transforms and draw
    // nvg::SetContext(vg);
    nvgSave(vg);
    nvgTranslate(vg, std::floor(state.offset.x), std::floor(state.offset.y));
    nvgScale(vg, state.zoom, state.zoom);

    drawCallback(vg);

    // Write zoom and offset info to the canvas with ImGui
    nvgScale(vg, 1/state.zoom, -1/state.zoom);
    nvgFontFace(vg, "mono");
    nvgFontSize(vg, 14.0f);
    float x0 = std::floor(-state.offset.x); 
    float y0 = std::floor(state.offset.y - canvasSize.y); 
    std::ostringstream oss;
    oss << std::setprecision(2) << std::fixed << "Scale " << state.zoom; 
    nvgFillColor(vg, nvgRGBA(255, 255, 255, 200)); // Semi-transparent background
    nvgText(vg, x0+3, y0+15, oss.str().c_str(), nullptr);
    oss.str("");
    oss << std::setprecision(0) << std::fixed << "Offset (" << state.offset.x << ", " << state.offset.y << ")";
    nvgText(vg, x0+3, y0+30, oss.str().c_str(), nullptr);

    nvgRestore(vg);

    currentState = nullptr;
    // ImGui::EndChild(); // End canvas child
}
} // namespace ZoomView



