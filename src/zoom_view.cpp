#include "render_module/zoom_view.hpp"
#include <cmath>
#include <unordered_map>
#include <sstream>
#include <iomanip>

namespace ZoomView {

struct State {
    float zoom = 1.0f;
    ImVec2 offset = {0.0f, 0.0f};
};

static std::unordered_map<std::string, State> stateMap;

// void Draw(const std::string& label, NVGcontext* vg, std::function<void(NVGcontext*)> drawCallback) {
//     State& state = stateMap[label];

//     ImVec2 canvasPos = ImGui::GetCursorScreenPos();
//     ImVec2 canvasSize = ImGui::GetContentRegionAvail();

//     // ImGui::InvisibleButton(("##" + label).c_str(), canvasSize,
//     //     ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);

//     bool hovering = ImGui::IsItemHovered();
//     ImVec2 mouse = ImGui::GetIO().MousePos;
//     ImVec2 delta = ImGui::GetIO().MouseDelta;

//     if (hovering) {
//         float wheel = ImGui::GetIO().MouseWheel;
//         if (wheel != 0.0f) {
//             float zoomFactor = 1.1f;
//             float prevZoom = state.zoom;
//             state.zoom *= (wheel > 0) ? zoomFactor : (1.0f / zoomFactor);
//             ImVec2 mouseInCanvas = ImVec2(mouse.x - canvasPos.x, mouse.y - canvasPos.y);
//             state.offset.x -= mouseInCanvas.x * (state.zoom - prevZoom) / state.zoom;
//             state.offset.y -= mouseInCanvas.y * (state.zoom - prevZoom) / state.zoom;
//         }
//     }

//     if (hovering && ImGui::IsMouseDragging(ImGuiMouseButton_Right)) {
//         state.offset.x += delta.x;
//         state.offset.y += delta.y;
//     }

//     // Actual rendering with zoom and pan
//     // nvgBeginFrame(vg, canvasSize.x, canvasSize.y, 1.0f);
//     nvgSave(vg);
//     nvgTranslate(vg, state.offset.x, state.offset.y);
//     nvgScale(vg, state.zoom, state.zoom);
//     drawCallback(vg);
//     nvgRestore(vg);
//     // nvgEndFrame(vg);
// }

// void Draw(const std::string& label, NVGcontext* vg, std::function<void(NVGcontext*)> drawCallback) {
//     State& state = stateMap[label];

//     ImVec2 canvasPos = ImGui::GetCursorScreenPos();
//     ImVec2 canvasSize = ImGui::GetContentRegionAvail();
//     if (canvasSize.x <= 0.0f || canvasSize.y <= 0.0f)
//         return;

//     // Create isolated, non-scrolling child region
//     ImGui::BeginChild(("CanvasChild_" + label).c_str(), canvasSize, false,
//                     ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);    

//     // Save current ImGui cursor position
//     ImVec2 cursorBackup = ImGui::GetCursorPos();

//     // Set screen position and create an invisible button for input handling
//     ImGui::SetCursorScreenPos(canvasPos);
//     ImGui::InvisibleButton(("##ZoomBtn_" + label).c_str(), canvasSize,
//         ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);

//     // Restore cursor position so layout isn't broken
//     ImGui::SetCursorPos(cursorBackup);

//     // Handle interaction
//     // bool hovering = ImGui::IsItemHovered();
//     bool hovering = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
//     ImVec2 mouse = ImGui::GetIO().MousePos;
//     ImVec2 delta = ImGui::GetIO().MouseDelta;

//     if (hovering && (ImGui::IsMouseDragging(ImGuiMouseButton_Right) || ImGui::IsMouseDragging(ImGuiMouseButton_Left))) {
//         float wheel = ImGui::GetIO().MouseWheel;
//         if (wheel != 0.0f) {
//             float zoomFactor = 1.1f;
//             float prevZoom = state.zoom;
//             state.zoom *= (wheel > 0) ? zoomFactor : (1.0f / zoomFactor);
//             ImVec2 mouseInCanvas = ImVec2(mouse.x - canvasPos.x, mouse.y - canvasPos.y);
//             state.offset.x -= mouseInCanvas.x * (state.zoom - prevZoom) / state.zoom;
//             state.offset.y -= mouseInCanvas.y * (state.zoom - prevZoom) / state.zoom;
//         }

//         if (ImGui::IsMouseDragging(ImGuiMouseButton_Right)) {
//             state.offset.x += delta.x;
//             state.offset.y += delta.y;
//         }
//     }

//     ImGui::SetCursorScreenPos(canvasPos);
//     ImGui::Text("Zoom: %.2f", state.zoom);
//     ImGui::Text("Offset: (%.1f, %.1f)", state.offset.x, state.offset.y);


//     // Apply zoom/pan transform to NanoVG
//     nvgSave(vg);
//     nvgTranslate(vg, state.offset.x, state.offset.y);
//     nvgScale(vg, state.zoom, state.zoom);
//     drawCallback(vg);
//     nvgRestore(vg);
// }

void Draw(const std::string& label, NVGcontext* vg, std::function<void(NVGcontext*)> drawCallback) {
    State& state = stateMap[label];

    ImVec2 canvasPos = ImGui::GetCursorScreenPos();
    ImVec2 canvasSize = ImGui::GetContentRegionAvail();

    if (canvasSize.x <= 0.0f || canvasSize.y <= 0.0f)
        return;


    ImVec2 cursorBackup = ImGui::GetCursorPos();

    // Create isolated, non-scrolling child region
    // ImGui::BeginChild(("CanvasChild_" + label).c_str(), canvasSize, false,
    //                 ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    // Setup for interaction
    ImGui::SetCursorScreenPos(canvasPos);
    ImGui::InvisibleButton(("##ZoomBtn_" + label).c_str(), canvasSize,
        ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);

    ImGui::SetCursorPos(cursorBackup);

    bool hovering = ImGui::IsItemHovered();
    bool active = ImGui::IsItemActive();  
    // bool hovering = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
    ImVec2 delta = ImGui::GetIO().MouseDelta;

    // if (hovering) {
    //     float wheel = ImGui::GetIO().MouseWheel;
    //     if (wheel != 0.0f) {
    //         float zoomFactor = 1.1f;
    //         float prevZoom = state.zoom;
    //         state.zoom *= (wheel > 0) ? zoomFactor : (1.0f / zoomFactor);
    //         ImVec2 mouse = ImGui::GetIO().MousePos;
    //         // ImVec2 mouseInCanvas = ImVec2(mouse.x - canvasPos.x, mouse.y - canvasPos.y);
    //         float mouseX = mouse.x - canvasPos.x;
    //         float mouseY = canvasSize.y - (mouse.y - canvasPos.y);  // Flip Y
    //         ImVec2 mouseInCanvas(mouseX, mouseY);
    //         // state.offset.x -= mouseInCanvas.x * (state.zoom - prevZoom) / state.zoom;
    //         // state.offset.y -= mouseInCanvas.y * (state.zoom - prevZoom) / state.zoom;
    //         // state.offset.x -= mouseInCanvas.x * (state.zoom - prevZoom) / state.zoom;
    //         // state.offset.y -= mouseInCanvas.y * (state.zoom - prevZoom) / state.zoom;
    //         state.offset.x -= mouseInCanvas.x * (1.0f - prevZoom / state.zoom);
    //         state.offset.y -= mouseInCanvas.y * (1.0f - prevZoom / state.zoom);
    //         //print zoom and offset to cli for debugging
    //         printf("CanvasPos: (%.1f, %.1f)\n", canvasPos.x, canvasPos.y);
    //         printf("Mouse: (%.1f, %.1f), MouseInCanvas: (%.1f, %.1f)\n", mouse.x, mouse.y, mouseInCanvas.x, mouseInCanvas.y);
    //         printf("Zoom: %.2f, Offset: (%.1f, %.1f)\n", state.zoom, state.offset.x, state.offset.y);


    //     }

    //     if (ImGui::IsMouseDragging(ImGuiMouseButton_Right)) {
    //         state.offset.x += delta.x;
    //         state.offset.y += delta.y;
    //         printf("Dragging: Offset: (%.1f, %.1f)\n", state.offset.x, state.offset.y);
    //     }
    // }
    // if (hovering && ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem)) {
    //     float wheel = ImGui::GetIO().MouseWheel;
    //     if (wheel != 0.0f) {
    //         float zoomFactor = 1.1f;
    //         float prevZoom = state.zoom;
    //         float newZoom = (wheel > 0) ? state.zoom * zoomFactor : state.zoom / zoomFactor;

    //         ImVec2 mouse = ImGui::GetIO().MousePos;
    //         ImVec2 canvasTopLeft = ImGui::GetCursorScreenPos();
    //         ImVec2 canvasSize = ImGui::GetContentRegionAvail();

    //         // Position of mouse inside canvas in screen coords
    //         ImVec2 mouseInCanvas = ImVec2(mouse.x - canvasTopLeft.x, mouse.y - canvasTopLeft.y);

    //         // Convert to content space (unscaled)
    //         ImVec2 contentPosBefore = ImVec2(
    //             (mouseInCanvas.x - state.offset.x) / prevZoom,
    //             (mouseInCanvas.y - state.offset.y) / prevZoom
    //         );

    //         // Update zoom
    //         state.zoom = newZoom;

    //         // Compute new offset to keep contentPosBefore under the mouse
    //         state.offset.x = mouseInCanvas.x - contentPosBefore.x * state.zoom;
    //         state.offset.y = mouseInCanvas.y - contentPosBefore.y * state.zoom;

    //         printf("Zoom: %.2f, Offset: (%.1f, %.1f)\n", state.zoom, state.offset.x, state.offset.y);
    //     }
    // }

    if (hovering && ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem)) {
        float wheel = ImGui::GetIO().MouseWheel;
        if (wheel != 0.0f) {
            float zoomFactor = 1.3f;
            float prevZoom = state.zoom;
            float newZoom = (wheel > 0) ? state.zoom * zoomFactor : state.zoom / zoomFactor;
            newZoom = std::max(0.27f, newZoom); // Prevent zooming out too much
            // newZoom = std::max(0.09f, newZoom); // Prevent zooming out too much

            ImVec2 mouse = ImGui::GetIO().MousePos;
            ImVec2 canvasTopLeft = ImGui::GetCursorScreenPos();
            ImVec2 canvasSize = ImGui::GetContentRegionAvail();

            // Get mouse position inside the canvas (flipping Y for NanoVG's origin)
            float mouseX = mouse.x - canvasTopLeft.x;
            float mouseY = canvasSize.y - (mouse.y - canvasTopLeft.y);  // <- Y flip
            // float mouseY = mouse.y - canvasTopLeft.y;  // <- Y flip

            // Convert to unscaled content coordinates
            ImVec2 contentPosBefore = ImVec2(
                (mouseX - state.offset.x) / prevZoom,
                (mouseY - state.offset.y) / prevZoom
            );

            state.zoom = newZoom;

            // Compute offset so that the content under the cursor stays fixed
            state.offset.x = std::floor(mouseX - contentPosBefore.x * state.zoom);
            state.offset.y = std::floor(mouseY - contentPosBefore.y * state.zoom);

            // printf("CanvasPos: (%.1f, %.1f)\n", canvasPos.x, canvasPos.y);
            // printf("CanvasSize: (%.1f, %.1f)\n", canvasSize.x, canvasSize.y);
            // printf("CanvasTopLeft: (%.1f, %.1f)\n", canvasTopLeft.x, canvasTopLeft.y);
            // printf("Zoom: %.2f, Offset: (%.1f, %.1f)\n", state.zoom, state.offset.x, state.offset.y);
        }
    }

    if (hovering && active && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        state.offset.x += std::floor(delta.x);
        state.offset.y -= std::floor(delta.y); // Flip Y for NanoVG
        // printf("Dragging Offset: (%.1f, %.1f)\n", state.offset.x, state.offset.y);
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

    // ImGui::EndChild(); // End canvas child
}
} // namespace ZoomView



