#include "render_module/render_module.hpp"
#ifdef VIDEO_COEXISTENCE_TEST
#include "render_module/video.hpp"
#endif
#ifdef WEBRTC_TEST
#include "webrtc/webrtc_session.hpp"
#endif
#include <boost/json.hpp>
#include <glad/glad.h>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
namespace { volatile std::sig_atomic_t stopped = 0; void Stop(int) { stopped = 1; } }
int main(int argc, char** argv) {
    if (argc != 2) return 1;
    std::signal(SIGTERM, Stop); std::signal(SIGINT, Stop);
    render_module::Config config; config.backend = render_module::Backend::Web;
    config.headlessContext = render_module::HeadlessContext::NativeEgl;
    config.width = 640; config.height = 480; config.fps = 30; config.web.port = 0;
    config.web.authToken = "0123456789abcdef0123456789abcdef";
#if defined(WEBRTC_TEST) || defined(INSTALLED_WEBRTC_TEST)
    if(std::getenv("RENDER_MODULE_TEST_WEBRTC")) {
        config.web.transport=render_module::WebTransport::WebRtc;
#ifdef WEBRTC_TEST
        if(std::getenv("RENDER_MODULE_TEST_SLOW_VIEWER")) render_module::detail::SetWebRtcTestSlowViewer(400);
        if(const auto* loss=std::getenv("RENDER_MODULE_TEST_LOSS")) render_module::detail::SetWebRtcTestPacketLoss(unsigned(std::atoi(loss)));
#endif
        if(const auto* url=std::getenv("RENDER_MODULE_TEST_TURN")) {
            const auto* user=std::getenv("RENDER_MODULE_TEST_TURN_USER"); const auto* pass=std::getenv("RENDER_MODULE_TEST_TURN_PASSWORD");
            if(!user || !pass) return 4;
            config.web.iceServers.push_back({url,user,pass}); config.web.iceRelayOnly=true;
        }
    }
#endif
    if (!RenderModule::Init(config)) return 2;
    ImGui::GetIO().IniFilename = nullptr;
#ifdef VIDEO_COEXISTENCE_TEST
    std::shared_ptr<render_module::video::VideoPipeline> video;
    if (std::getenv("RENDER_MODULE_TEST_VIDEO")) {
        video = std::make_shared<render_module::video::VideoPipeline>();
        if (!RenderModule::SetVideoOutput(video)) return 3;
    }
#endif
    int frame = 0, clicks = 0, generation = 0; GLint oldRoot = 0;
    char text[1024] = {}; ImVec2 button{}, edit{}, viewCenter{}, canvasCenter{};
    RenderModule::RegisterImGuiCallback([&] {
        if (++frame > 6000 || stopped) RenderModule::RequestClose();
        GLint root; glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &root);
        if (root != oldRoot) { oldRoot = root; ++generation; }
        ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always); ImGui::SetNextWindowSize({300, 230}, ImGuiCond_Always);
        ImGui::Begin("Controls", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);
        if (ImGui::Button("Known button", {140, 40})) ++clicks;
        auto a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax(); button = {(a.x+b.x)/2, (a.y+b.y)/2};
        ImGui::InputText("Text", text, sizeof(text));
        a = ImGui::GetItemRectMin(); b = ImGui::GetItemRectMax(); edit = {(a.x+b.x)/2, (a.y+b.y)/2};
        ImGui::Text("Clicks: %d", clicks); ImGui::TextUnformatted("Fixed UI / NanoVG / Magnum fixture"); ImGui::End();
        ImGui::SetNextWindowPos({0, 230}, ImGuiCond_Always); ImGui::SetNextWindowSize({300, 250}, ImGuiCond_Always);
        ImGui::Begin("Canvas", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize); ImGui::End();
        ImGui::SetNextWindowPos({300, 0}, ImGuiCond_Always); ImGui::SetNextWindowSize({340, 480}, ImGuiCond_Always);
        ImGui::Begin("Scene", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize); ImGui::End();
    });
    RenderModule::RegisterCanvas("Canvas", [&](render_module::Canvas& canvas) {
        const auto a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax(); canvasCenter = {(a.x+b.x)/2, (a.y+b.y)/2};
        nvgBeginPath(canvas.Graphics()); nvgRect(canvas.Graphics(), 0, 0, canvas.Size().x, canvas.Size().y);
        nvgFillColor(canvas.Graphics(), nvgRGB(245, 20, 20)); nvgFill(canvas.Graphics());
    });
    RenderModule::Register3DView("Scene", [&](render_module::View3D& view) {
        if (frame == 1) view.Camera().LookAt({0, 0, 5}, {0, 0, 0}, {0, 1, 0});
        const auto a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax(); viewCenter = {(a.x+b.x)/2, (a.y+b.y)/2};
        view.Grid(4, 1); view.Box("box", {}, {1, 1, 1}); view.PointCloud("green", {{0,0,1}}, {0,1,0,1}, 35);
        const auto p = view.Camera().Position(); const auto& io = ImGui::GetIO();
        boost::json::object state{{"frame", frame}, {"clicks", clicks}, {"text", text}, {"generation", generation},
            {"width", io.DisplaySize.x}, {"height", io.DisplaySize.y}, {"button", {button.x, button.y}},
            {"edit", {edit.x, edit.y}}, {"view", {viewCenter.x, viewCenter.y}}, {"canvas", {canvasCenter.x, canvasCenter.y}},
            {"camera", {p.x,p.y,p.z}}, {"keyHeld", ImGui::IsKeyDown(ImGuiKey_A)}, {"ctrl", io.KeyCtrl},
            {"mouseHeld", ImGui::IsMouseDown(0)}, {"glError", glGetError()}};
#ifdef VIDEO_COEXISTENCE_TEST
        if (video) {
            render_module::video::EncodedFrame encoded;
            video->TryReceive(encoded); // Test-only sink; H.264 never enters WebSocket.
            const auto metrics = video->Metrics();
            state["videoEncoded"] = metrics.framesEncoded; state["videoErrors"] = metrics.errors;
        }
#endif
        const std::string path = argv[1], temp = path + ".tmp";
        { std::ofstream file(temp); file << boost::json::serialize(state); }
        std::rename(temp.c_str(), path.c_str());
    });
    RenderModule::Run(); RenderModule::Shutdown(); return 0;
}
