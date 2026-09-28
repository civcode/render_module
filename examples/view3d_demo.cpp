#include "render_module/render_module.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include <cxxopts.hpp>
#include <imgui_internal.h>

namespace {
constexpr float kPi = 3.14159265358979323846f;
}

int main(int argc, char** argv) {
    render_module::Config config;
    config.width = 1200;
    config.height = 800;
    config.fps = 60.0;
    config.title = "RenderModule 3D Demo";
    int frameLimit = 0;
    std::string screenshot;

    cxxopts::Options options(argv[0], "RenderModule 3D demo");
    options.add_options("General")
        ("h,help", "Show this help and exit")
        ("frames", "Stop after N rendered frames; N must be greater than zero",
            cxxopts::value<int>(), "N")
        ("screenshot", "Save the final root framebuffer to a PNG file",
            cxxopts::value<std::string>(), "PATH");
    options.add_options("Rendering")
        ("render-backend", "Rendering backend: desktop, headless, or web",
            cxxopts::value<std::string>(), "BACKEND")
        ("headless-context", "Headless GL context provider: glfw-null-egl or native-egl",
            cxxopts::value<std::string>(), "CONTEXT");
    options.add_options("Web")
        ("web-transport", "Web transport: jpeg or webrtc",
            cxxopts::value<std::string>(), "TRANSPORT")
        ("web-image-codec", "WebSocket image codec: jpeg or png",
            cxxopts::value<std::string>(), "CODEC")
        ("web-bind", "Web server bind address",
            cxxopts::value<std::string>(), "ADDRESS")
        ("web-port", "Web server port in the range 0..65535",
            cxxopts::value<std::uint32_t>(), "PORT")
        ("web-origin", "Allowed web origin; may be specified more than once",
            cxxopts::value<std::vector<std::string>>(), "ORIGIN");

    try {
        const auto result = options.parse(argc, argv);

        if (result.count("help") != 0) {
            const std::string help = options.help();
            std::fprintf(stdout, "%s\n", help.c_str());
            return 0;
        }
        if (!result.unmatched().empty()) {
            std::fprintf(stderr, "Unexpected positional argument: %s\n",
                result.unmatched().front().c_str());
            return 1;
        }

        if (result.count("render-backend") != 0) {
            const std::string value = result["render-backend"].as<std::string>();
            if (value == "desktop")
                config.backend = render_module::Backend::Desktop;
            else if (value == "headless")
                config.backend = render_module::Backend::Headless;
            else if (value == "web")
                config.backend = render_module::Backend::Web;
            else {
                std::fprintf(stderr,
                    "Invalid --render-backend '%s'; expected desktop, headless, or web.\n",
                    value.c_str());
                return 1;
            }
        }

        if (result.count("headless-context") != 0) {
            const std::string value = result["headless-context"].as<std::string>();
            if (value == "glfw-null-egl")
                config.headlessContext = render_module::HeadlessContext::GlfwNullEgl;
            else if (value == "native-egl")
                config.headlessContext = render_module::HeadlessContext::NativeEgl;
            else {
                std::fprintf(stderr,
                    "Invalid --headless-context '%s'; expected glfw-null-egl or native-egl.\n",
                    value.c_str());
                return 1;
            }
        }

        if (result.count("web-transport") != 0) {
            const std::string value = result["web-transport"].as<std::string>();
            if (value == "jpeg")
                config.web.transport = render_module::WebTransport::JpegWebSocket;
            else if (value == "webrtc")
                config.web.transport = render_module::WebTransport::WebRtc;
            else {
                std::fprintf(stderr,
                    "Invalid --web-transport '%s'; expected jpeg or webrtc.\n",
                    value.c_str());
                return 1;
            }
        }

        if (result.count("web-image-codec") != 0) {
            const std::string value = result["web-image-codec"].as<std::string>();
            if (value == "jpeg")
                config.web.imageCodec = render_module::WebSocketImageCodec::Jpeg;
            else if (value == "png")
                config.web.imageCodec = render_module::WebSocketImageCodec::Png;
            else {
                std::fprintf(stderr,
                    "Invalid --web-image-codec '%s'; expected jpeg or png.\n",
                    value.c_str());
                return 1;
            }
        }

        if (result.count("web-bind") != 0) {
            const std::string value = result["web-bind"].as<std::string>();
            if (value.empty()) {
                std::fprintf(stderr, "--web-bind requires a non-empty address.\n");
                return 1;
            }
            config.web.bindAddress = value;
        }

        if (result.count("web-port") != 0) {
            const std::uint32_t port = result["web-port"].as<std::uint32_t>();
            if (port > 65535) {
                std::fprintf(stderr, "--web-port must be in the range 0..65535.\n");
                return 1;
            }
            config.web.port = static_cast<std::uint16_t>(port);
        }

        if (result.count("web-origin") != 0) {
            const auto origins = result["web-origin"].as<std::vector<std::string>>();
            for (const std::string& origin : origins) {
                if (origin.empty()) {
                    std::fprintf(stderr, "--web-origin requires a non-empty origin.\n");
                    return 1;
                }
                config.web.allowedOrigins.emplace_back(origin);
            }
        }

        if (result.count("screenshot") != 0) {
            screenshot = result["screenshot"].as<std::string>();
            if (screenshot.empty()) {
                std::fprintf(stderr, "--screenshot requires a non-empty path.\n");
                return 1;
            }
        }

        if (result.count("frames") != 0) {
            frameLimit = result["frames"].as<int>();
            if (frameLimit <= 0) {
                std::fprintf(stderr, "--frames requires a positive integer.\n");
                return 1;
            }
        }
    } catch (const cxxopts::exceptions::parsing& error) {
        std::fprintf(stderr, "Command-line error: %s\n\n", error.what());
        const std::string help = options.help();
        std::fprintf(stderr, "%s\n", help.c_str());
        return 1;
    }
    if (config.backend == render_module::Backend::Web) {
        config.fps = 30;
        if (const char* token = std::getenv("RENDER_MODULE_WEB_TOKEN")) config.web.authToken = token;
    }
    if (!RenderModule::Init(config)) return 1;
    if (config.backend != render_module::Backend::Desktop)
        ImGui::GetIO().IniFilename = nullptr; // Deterministic offscreen demo layout.
    RenderModule::EnableRootWindowDocking();
    int frames = 0;
    int renderedViews = 0;

    bool resetCameraRequested = false;
    bool topCameraRequested = false;
    render_module::Pose3D cameraPose{};

    RenderModule::RegisterImGuiCallback([&] {
        if (frameLimit > 0 && ++frames == frameLimit) RenderModule::RequestClose();

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
        if (screenshot.empty()) ImGui::Text("FPS: %.1f", RenderModule::GetFPS());
        else ImGui::TextUnformatted("Full UI screenshot mode");
        ImGui::End();
    });

    render_module::View3DOptions viewOptions;
    viewOptions.showStatus = true;

    RenderModule::Register3DView("Localization 3D", [&](render_module::View3D& view) {
        using namespace render_module;
        ++renderedViews;

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
    }, viewOptions);

    RenderModule::Run();
    const bool captured = screenshot.empty() || RenderModule::SaveScreenshot(screenshot);
    RenderModule::Shutdown();
    if (!captured) {
        std::fprintf(stderr, "Could not save screenshot '%s'.\n", screenshot.c_str());
        return 1;
    }
    if (frameLimit > 0 && renderedViews == 0) {
        std::fprintf(stderr, "3D demo did not render its scene.\n");
        return 1;
    }
    if (config.backend == render_module::Backend::Headless)
        std::fprintf(stderr, "Headless 3D demo composed %d scene frames into the root UI framebuffer.\n", renderedViews);
    return 0;
}
