#include "render_module/render_module.hpp"
#include "input/input_access.hpp"
#include "core/render_output.hpp"
#include "render_module_test_guard.hpp"
#include <catch2/catch_test_macros.hpp>
#include <glad/glad.h>
#include <imgui_internal.h>
#ifdef RENDER_MODULE_TEST_DESKTOP
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#endif
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <thread>

using namespace render_module::detail;
namespace {
float Distance(render_module::Vec3 a, render_module::Vec3 b) {
    return std::sqrt((a.x-b.x)*(a.x-b.x)+(a.y-b.y)*(a.y-b.y)+(a.z-b.z)*(a.z-b.z));
}
ImVec2 CenterOfItem() {
    const auto a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
    return {(a.x+b.x)*.5f, (a.y+b.y)*.5f};
}
struct Injector {
    bool desktop;
    std::shared_ptr<RemoteInputQueue> queue;
#ifdef RENDER_MODULE_TEST_DESKTOP
    GLFWwindow* window = nullptr;
    GLFWcursorposfun cursor;
    GLFWmousebuttonfun mouse;
    GLFWscrollfun scroll;
    GLFWkeyfun key;
    GLFWcharfun character;
#endif
    explicit Injector(bool isDesktop) : desktop(isDesktop), queue(RemoteInputQueueHandle()) {
        if (!desktop) { REQUIRE((queue)); return; }
        REQUIRE((!queue));
#ifdef RENDER_MODULE_TEST_DESKTOP
        window = glfwGetCurrentContext(); REQUIRE((window));
        cursor = glfwSetCursorPosCallback(window, nullptr); glfwSetCursorPosCallback(window, cursor);
        mouse = glfwSetMouseButtonCallback(window, nullptr); glfwSetMouseButtonCallback(window, mouse);
        scroll = glfwSetScrollCallback(window, nullptr); glfwSetScrollCallback(window, scroll);
        key = glfwSetKeyCallback(window, nullptr); glfwSetKeyCallback(window, key);
        character = glfwSetCharCallback(window, nullptr); glfwSetCharCallback(window, character);
        REQUIRE((cursor && mouse && scroll && key && character));
        glfwFocusWindow(window);
#endif
    }
    void Send(RemoteInputEvent event) {
        EnqueueResult result{EnqueueStatus::Invalid};
        std::thread producer([&] { result = queue->Enqueue(std::move(event)); });
        producer.join();
        REQUIRE((result.Accepted()));
    }
    void Move(ImVec2 position) {
        if (!desktop) { Send(MouseMove{position.x/640, position.y/480}); return; }
#ifdef RENDER_MODULE_TEST_DESKTOP
        glfwSetCursorPos(window, position.x, position.y);
        cursor(window, position.x, position.y);
#endif
    }
    void Button(RemoteMouseButton button, bool down) {
        if (!desktop) { Send(MouseButton{button, down}); return; }
#ifdef RENDER_MODULE_TEST_DESKTOP
        mouse(window, static_cast<int>(button), down ? GLFW_PRESS : GLFW_RELEASE, 0);
#endif
    }
    void Wheel() {
        if (!desktop) { Send(MouseWheel{0, 1}); return; }
#ifdef RENDER_MODULE_TEST_DESKTOP
        scroll(window, 0, 1);
#endif
    }
    void Text() {
        if (!desktop) { Send(TextUtf8{u8"Remote äöüÄÖÜß é 日本 🙂"}); return; }
#ifdef RENDER_MODULE_TEST_DESKTOP
        for (unsigned c : {'R','e','m','o','t','e',' '}) character(window, c);
        for (unsigned c : {0xe4u,0xf6u,0xfcu,0xc4u,0xd6u,0xdcu,0xdfu,32u,0xe9u,32u,0x65e5u,0x672cu,32u,0x1f642u})
            character(window, c);
#endif
    }
    void Backspace() {
        if (!desktop) { Send(Key{RenderKey::Backspace, true}); Send(Key{RenderKey::Backspace, false}); return; }
#ifdef RENDER_MODULE_TEST_DESKTOP
        key(window, GLFW_KEY_BACKSPACE, 0, GLFW_PRESS, 0);
        key(window, GLFW_KEY_BACKSPACE, 0, GLFW_RELEASE, 0);
#endif
    }
};

void Run(const char* mode) {
    const bool desktop = std::strcmp(mode, "desktop") == 0;
    if (!desktop) { REQUIRE((!std::getenv("DISPLAY"))); REQUIRE((!std::getenv("WAYLAND_DISPLAY"))); }
    render_module::Config config;
    config.width = 640; config.height = 480; config.fps = 0;
    config.backend = desktop ? render_module::Backend::Desktop : render_module::Backend::Headless;
    config.headlessContext = std::strcmp(mode, "glfw") == 0 ? render_module::HeadlessContext::GlfwNullEgl :
        render_module::HeadlessContext::NativeEgl;
    REQUIRE((RenderModule::Init(config)));
    ImGui::GetIO().IniFilename = nullptr;
    Injector inject(desktop);
    RenderModule::EnableRootWindowDocking();
    int frames = 0, clicks = 0, canvasPresses = 0, canvasReleases = 0;
    ImVec2 button{}, edit{}, viewCenter{}, canvasCenter{};
    char text[256] = {};
    render_module::Vec3 position{}, target{}, orbitPosition{}, orbitTarget{}, panPosition{}, panTarget{};
    float distance = 0, originalDistance = 0, beforeZoom = 0;
    GLint rootHandle = 0;
    RenderModule::RegisterImGuiCallback([&] {
        ++frames;
        GLint current; glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &current);
        if (frames == 1) rootHandle = current;
        REQUIRE((rootHandle != 0));
        if (frames <= 100) REQUIRE((current == rootHandle)); // Input must not recreate root.
        if (desktop && frames > 100) {
            REQUIRE((frames < 200));
            if (ImGui::GetIO().DisplaySize.x == 800 && ImGui::GetIO().DisplaySize.y == 600) {
                REQUIRE((current != rootHandle)); // Only an actual resize recreates storage.
                REQUIRE((RenderModule::GetWindowSize().x == 800));
                RenderModule::RequestClose();
            }
        }
        REQUIRE((glGetError() == GL_NO_ERROR));
        const auto dock = RenderModule::GetRootDockspaceID();
        if (frames == 1) {
            ImGui::DockBuilderRemoveNode(dock);
            ImGui::DockBuilderAddNode(dock, ImGuiDockNodeFlags_DockSpace);
            ImGui::DockBuilderSetNodeSize(dock, {640, 480});
            ImGuiID left, right, top, bottom;
            ImGui::DockBuilderSplitNode(dock, ImGuiDir_Left, .45f, &left, &right);
            ImGui::DockBuilderSplitNode(right, ImGuiDir_Up, .6f, &top, &bottom);
            ImGui::DockBuilderDockWindow("Controls", left);
            ImGui::DockBuilderDockWindow("3D", top);
            ImGui::DockBuilderDockWindow("Canvas", bottom);
            ImGui::DockBuilderFinish(dock);
        }
        ImGui::Begin("Controls");
        if (ImGui::Button("Click", {120, 40})) ++clicks;
        button = CenterOfItem();
        ImGui::InputText("Text", text, sizeof(text)); edit = CenterOfItem();
        ImGui::End();
        if (frames >= 8) {
            REQUIRE((ImGui::FindWindowByName("Controls")->DockId != 0));
            REQUIRE((ImGui::FindWindowByName("3D")->DockId != 0));
        }
        switch (frames) {
            case 4: inject.Move(button); inject.Button(RemoteMouseButton::Left, true);
                    inject.Button(RemoteMouseButton::Left, false); break;
            case 8: REQUIRE((clicks == 1)); break;
            case 12: inject.Move(edit); inject.Button(RemoteMouseButton::Left, true);
                     inject.Button(RemoteMouseButton::Left, false); break;
            case 16: inject.Text(); break;
            case 20: REQUIRE((std::strcmp(text, u8"Remote äöüÄÖÜß é 日本 🙂") == 0)); break;
            case 24: inject.Backspace(); break;
            case 28: REQUIRE((std::strcmp(text, u8"Remote äöüÄÖÜß é 日本 ") == 0)); break;
            case 32: if (!desktop) {
                inject.Send(Key{RenderKey::LeftCtrl, true}); inject.Send(Key{RenderKey::A, true});
                inject.Send(Key{RenderKey::A, false}); inject.Send(Key{RenderKey::LeftCtrl, false});
            } break;
            case 36: if (!desktop) inject.Send(TextUtf8{u8"Replacement ß"}); break;
            case 40: if (!desktop) REQUIRE((std::strcmp(text, u8"Replacement ß") == 0));
                     orbitPosition = position; orbitTarget = target; originalDistance = distance; break;
            case 44: inject.Move(viewCenter); break;
            case 48: inject.Button(RemoteMouseButton::Right, true); break;
            case 52: inject.Move({viewCenter.x + 60, viewCenter.y + 30}); break;
            case 56: inject.Button(RemoteMouseButton::Right, false); break;
            case 60: REQUIRE((Distance(position, orbitPosition) > .1f));
                     REQUIRE((Distance(target, orbitTarget) < .001f));
                     REQUIRE((std::abs(distance - originalDistance) < .001f));
                     panPosition = position; panTarget = target; break;
            case 64: inject.Move(viewCenter); break;
            case 68: inject.Button(RemoteMouseButton::Middle, true); break;
            case 72: inject.Move({viewCenter.x + 40, viewCenter.y - 25}); break;
            case 76: inject.Button(RemoteMouseButton::Middle, false); break;
            case 80: REQUIRE((Distance(position, panPosition) > .1f));
                     REQUIRE((Distance(target, panTarget) > .1f));
                     REQUIRE((std::abs(Distance(position, panPosition) - Distance(target, panTarget)) < .001f));
                     REQUIRE((std::abs(distance - originalDistance) < .001f));
                     beforeZoom = distance; break;
            case 84: inject.Wheel(); break;
            case 88: REQUIRE((std::abs(distance - beforeZoom/1.2f) < .001f)); break;
            case 92: inject.Move(canvasCenter); inject.Button(RemoteMouseButton::Left, true);
                     inject.Button(RemoteMouseButton::Left, false); break;
            case 96: REQUIRE((canvasPresses == 1 && canvasReleases >= 1)); break;
            case 100:
                if (!desktop) RenderModule::RequestClose();
#ifdef RENDER_MODULE_TEST_DESKTOP
                else glfwSetWindowSize(inject.window, 800, 600);
#endif
                break;
        }
    });
    RenderModule::RegisterCanvas("Canvas", [&](render_module::Canvas& canvas) {
        canvasCenter = CenterOfItem();
        const auto& buttonState = canvas.Input().Button(render_module::MouseButton::Left);
        if (buttonState.pressed) ++canvasPresses;
        if (buttonState.released && canvas.Input().hovered) ++canvasReleases;
    });
    RenderModule::Register3DView("3D", [&](render_module::View3D& view) {
        viewCenter = CenterOfItem();
        position = view.Camera().Position(); target = view.Camera().Target(); distance = view.Camera().Distance();
        view.Grid(4, 1); view.Box("box", {}, {1, 1, 1});
    });
    RenderModule::Run();
    REQUIRE((desktop ? frames > 100 && frames < 200 : frames == 100));
    const auto frame = CompletedFrame();
    REQUIRE((frame.IsValid() && frame.framebufferGeneration == (desktop ? 2u : 1u)));
    REQUIRE((glGetError() == GL_NO_ERROR));
    auto handle = inject.queue;
    RenderModule::Shutdown();
    if (handle) REQUIRE((handle->Enqueue(Focus{true}).status == EnqueueStatus::Closed));
    std::puts("Application input passed: button, UTF-8 InputText, keyboard editing, Canvas, docking, View3D orbit/pan/zoom, stable root (Desktop resize also checked).");
}
} // namespace

#ifdef RENDER_MODULE_TEST_DESKTOP
TEST_CASE("Desktop input integration", "[input][integration][desktop]") {
    RenderModuleTestGuard cleanup;
    Run("desktop");
}
#endif

TEST_CASE("Native EGL input integration", "[input][integration][native]") {
    RenderModuleTestGuard cleanup;
    Run("native");
}

TEST_CASE("GLFW Null EGL input integration", "[input][integration][glfw]") {
    RenderModuleTestGuard cleanup;
    Run("glfw");
}
