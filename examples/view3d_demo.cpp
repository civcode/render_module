#include "render_module/render_module.hpp"

#include <cmath>
#include <cstdint>
#include <vector>

#include <imgui_internal.h>

namespace {
constexpr float kPi = 3.14159265358979323846f;
}

int main() {
    if (!RenderModule::Init(1200, 800, 60.0, "RenderModule 3D Demo")) return 1;
    RenderModule::EnableRootWindowDocking();

    bool resetCameraRequested = false;
    bool topCameraRequested = false;
    render_module::Pose3D cameraPose{};

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
            ImGui::DockBuilderDockWindow("3D Controls", centerNode);
            ImGui::DockBuilderDockWindow("Localization 3D", leftNode);
            ImGui::DockBuilderFinish(dockspaceId);
        }

        ImGui::Begin("3D Controls");
        ImGui::TextUnformatted("3D viewport controls:");
        ImGui::BulletText("Right-drag: orbit");
        ImGui::BulletText("Middle-drag: pan");
        ImGui::BulletText("Mouse wheel: zoom");
        ImGui::BulletText("Left-click: place the yellow marker on z=0");
        ImGui::Separator();
        if (ImGui::Button("Reset camera")) resetCameraRequested = true;
        ImGui::SameLine();
        if (ImGui::Button("Top view")) topCameraRequested = true;
        ImGui::Text("Camera T_world_camera position: (%.2f, %.2f, %.2f)",
                    cameraPose.position.x, cameraPose.position.y, cameraPose.position.z);
        ImGui::Text("FPS: %.1f", RenderModule::GetFPS());
        ImGui::End();
    });

    render_module::View3DOptions options;
    options.showStatus = true;

    RenderModule::Register3DView("Localization 3D", [&](render_module::View3D& view) {
        using namespace render_module;

        if (resetCameraRequested) {
            view.Camera().Reset();
            resetCameraRequested = false;
        }
        if (topCameraRequested) {
            view.Camera().LookAt({0.0f, 0.0f, 8.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f});
            topCameraRequested = false;
        }

        static std::vector<Vec3> cloud = [] {
            std::vector<Vec3> result;
            result.reserve(2200);
            for (int ring = 0; ring < 22; ++ring) {
                const float radius = 0.25f + ring*0.10f;
                for (int i = 0; i < 100; ++i) {
                    const float a = 2.0f*kPi*i/100.0f;
                    const float ripple = 0.10f*std::sin(4.0f*a + ring*0.25f);
                    result.push_back({radius*std::cos(a),
                                      radius*std::sin(a),
                                      0.45f + ripple + ring*0.012f});
                }
            }
            return result;
        }();

        static Vec3 picked{1.5f, 1.0f, 0.0f};
        if (view.Input().Button(MouseButton::Left).pressed) {
            Vec3 hit;
            if (view.IntersectGroundPlane(hit)) picked = hit;
        }

        view.Grid(6.0f, 0.5f);
        view.Axes("world", Pose3D{}, 1.0f, 2.0f);
        view.PointCloud("scan", cloud, {0.80f, 0.88f, 1.0f, 1.0f}, 2.5f);

        Pose3D robot;
        robot.position = {0.0f, 0.0f, 0.25f};
        robot.orientation = Quaternion::FromEulerXYZ(0.0f, 0.0f, 0.35f);
        view.Box("robot", robot, {1.0f, 0.55f, 0.30f}, {0.15f, 0.55f, 0.95f, 1.0f});
        view.Axes("robot_pose", robot, 0.65f, 2.0f);

        view.Sphere("landmark", {1.4f, -0.8f, 0.35f}, 0.35f,
                    {0.95f, 0.35f, 0.25f, 1.0f});

        Pose3D cylinderPose;
        cylinderPose.position = {-1.25f, -0.75f, 0.65f};
        cylinderPose.orientation = Quaternion::FromEulerXYZ(0.15f, 0.35f, 0.20f);
        view.Cylinder("cylinder", cylinderPose, 0.25f, 1.3f,
                      {0.30f, 0.80f, 0.45f, 1.0f});

        // A small custom procedural triangle mesh (a pyramid) to demonstrate
        // View3D::TriangleMesh in addition to Magnum's primitive helpers.
        static const std::vector<Vec3> pyramidVertices{
            {-0.45f, -0.45f, 0.0f},
            { 0.45f, -0.45f, 0.0f},
            { 0.45f,  0.45f, 0.0f},
            {-0.45f,  0.45f, 0.0f},
            { 0.00f,  0.00f, 0.85f}
        };
        static const std::vector<std::uint32_t> pyramidIndices{
            0, 1, 4, 1, 2, 4, 2, 3, 4, 3, 0, 4,
            0, 2, 1, 0, 3, 2
        };
        static std::vector<Vec3> translatedPyramid = [] {
            std::vector<Vec3> result = pyramidVertices;
            for (Vec3& p : result) {
                p.x += 1.7f;
                p.y += 1.4f;
            }
            return result;
        }();
        view.TriangleMesh("pyramid", translatedPyramid, pyramidIndices,
                          {0.70f, 0.35f, 0.85f, 1.0f});

        view.Sphere("picked", {picked.x, picked.y, 0.08f}, 0.08f,
                    {1.0f, 0.85f, 0.10f, 1.0f});
        view.Line({picked.x, picked.y, 0.0f}, {picked.x, picked.y, 0.8f},
                  {1.0f, 0.85f, 0.10f, 1.0f}, 2.0f);

        cameraPose = view.Camera().Pose();
    }, options);

    RenderModule::Run();
    RenderModule::Shutdown();
    return 0;
}
