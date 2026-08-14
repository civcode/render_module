#include "render_module/pose_tool.hpp"
#include "render_module/render_module.hpp"

#include <imgui_internal.h>

#include <cmath>

namespace {

enum class ActiveTool {
    None,
    Start,
    Goal
};

} // namespace

int main() {
    using namespace render_module;

    if (!RenderModule::Init(1100, 760, 60.0, "Path planning interaction")) {
        return 1;
    }
    RenderModule::EnableRootWindowDocking();

    PoseDragTool startTool;
    PoseDragTool goalTool;
    startTool.SetPose({{-120.0f, -60.0f}, 0.25f});
    goalTool.SetPose({{140.0f, 90.0f}, 2.6f});

    ActiveTool activeTool = ActiveTool::Start;
    bool resetView = true;

    RenderModule::RegisterImGuiCallback([&] {

        static bool initialized = false;
        const ImGuiID dockspaceId = RenderModule::GetRootDockspaceID();
        if (!initialized && dockspaceId != 0) {
            initialized = true;
            // const ImGuiViewport* viewport = ImGui::GetMainViewport();
            ImGui::DockBuilderRemoveNode(dockspaceId);
            ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
            ImGui::DockBuilderSetNodeSize(dockspaceId, ImGui::GetMainViewport()->WorkSize);
            ImGuiID centerNode = dockspaceId;
            ImGuiID leftNode = ImGui::DockBuilderSplitNode(centerNode, ImGuiDir_Left, 0.7f, nullptr, &centerNode);
            ImGui::DockBuilderDockWindow("Path tools", centerNode);
            ImGui::DockBuilderDockWindow("Path planning map", leftNode);
            ImGui::DockBuilderFinish(dockspaceId);
        }

        ImGui::Begin("Path tools");
        ImGui::TextUnformatted("Choose a tool, then left-click and drag in the map.");
        if (ImGui::RadioButton("Set start pose", activeTool == ActiveTool::Start)) {
            activeTool = ActiveTool::Start;
        }
        if (ImGui::RadioButton("Set goal pose", activeTool == ActiveTool::Goal)) {
            activeTool = ActiveTool::Goal;
        }
        if (ImGui::RadioButton("Inspect only", activeTool == ActiveTool::None)) {
            activeTool = ActiveTool::None;
        }
        if (ImGui::Button("Reset camera")) resetView = true;
        ImGui::SameLine();
        if (ImGui::Button("Clear poses")) {
            startTool.Clear();
            goalTool.Clear();
        }

        ImGui::Separator();
        ImGui::BulletText("Left drag: place position and heading");
        ImGui::BulletText("Middle drag: pan");
        ImGui::BulletText("Mouse wheel: zoom at cursor");
        if (startTool.HasPose()) {
            const Pose2D& pose = startTool.Pose();
            ImGui::Text("Start: (%.2f, %.2f), yaw %.1f deg",
                        pose.position.x, pose.position.y, pose.yaw * 180.0f / 3.14159265f);
        }
        if (goalTool.HasPose()) {
            const Pose2D& pose = goalTool.Pose();
            ImGui::Text("Goal:  (%.2f, %.2f), yaw %.1f deg",
                        pose.position.x, pose.position.y, pose.yaw * 180.0f / 3.14159265f);
        }
        ImGui::End();
    });

    RenderModule::RegisterCanvas("Path planning map", [&](Canvas& canvas) {
        canvas.DrawViewport("map", [&](Viewport& viewport) {
            if (resetView) {
                viewport.SetScale(1.4f);
                viewport.SetOffset({canvas.Size().x * 0.5f, canvas.Size().y * 0.5f});
                resetView = false;
            }

            const Vec2 lowerLeft = viewport.CanvasToWorld({0.0f, 0.0f});
            const Vec2 upperRight = viewport.CanvasToWorld(canvas.Size());
            const float scale = viewport.Scale();
            const float minorStep = scale < 0.4f ? 100.0f : (scale < 0.9f ? 50.0f : 25.0f);
            const int x0 = static_cast<int>(std::floor(lowerLeft.x / minorStep));
            const int x1 = static_cast<int>(std::ceil(upperRight.x / minorStep));
            const int y0 = static_cast<int>(std::floor(lowerLeft.y / minorStep));
            const int y1 = static_cast<int>(std::ceil(upperRight.y / minorStep));

            NVGcontext* vg = viewport.Graphics();
            nvgStrokeWidth(vg, 1.0f / scale);
            for (int x = x0; x <= x1; ++x) {
                const float worldX = x * minorStep;
                nvgBeginPath(vg);
                nvgMoveTo(vg, worldX, lowerLeft.y);
                nvgLineTo(vg, worldX, upperRight.y);
                nvgStrokeColor(vg, x == 0 ? nvgRGBA(105, 120, 145, 230)
                                          : nvgRGBA(75, 82, 94, 150));
                nvgStroke(vg);
            }
            for (int y = y0; y <= y1; ++y) {
                const float worldY = y * minorStep;
                nvgBeginPath(vg);
                nvgMoveTo(vg, lowerLeft.x, worldY);
                nvgLineTo(vg, upperRight.x, worldY);
                nvgStrokeColor(vg, y == 0 ? nvgRGBA(105, 120, 145, 230)
                                          : nvgRGBA(75, 82, 94, 150));
                nvgStroke(vg);
            }

            if (activeTool == ActiveTool::Start) {
                if (startTool.Update(viewport) == PoseDragEvent::Finished) {
                    activeTool = ActiveTool::None;
                }
            } else if (activeTool == ActiveTool::Goal) {
                if (goalTool.Update(viewport) == PoseDragEvent::Finished) {
                    activeTool = ActiveTool::None;
                }
            }

            if (startTool.HasPose() && goalTool.HasPose()) {
                nvgBeginPath(vg);
                nvgMoveTo(vg, startTool.Pose().position.x, startTool.Pose().position.y);
                nvgLineTo(vg, goalTool.Pose().position.x, goalTool.Pose().position.y);
                nvgStrokeWidth(vg, 2.0f / scale);
                nvgStrokeColor(vg, nvgRGBA(100, 170, 245, 190));
                nvgStroke(vg);
            }

            PoseToolStyle startStyle;
            startStyle.color = {40, 205, 105, 255};
            startTool.Draw(viewport, startStyle);
            PoseToolStyle goalStyle;
            goalStyle.color = {235, 85, 70, 255};
            goalTool.Draw(viewport, goalStyle);
        });
    });

    RenderModule::Run();
    RenderModule::Shutdown();
    return 0;
}
