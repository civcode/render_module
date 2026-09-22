#include <glad/glad.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include "render_module/render_module.hpp"
#include "platform/platform_backend.hpp"
#include "input/input_backend.hpp"
#include "present/presenter.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <thread>

namespace {

// Unlike assert(), checks remain active in Release builds.
#define CHECK(condition) do { if (!(condition)) \
    throw std::runtime_error("Check failed: " #condition); } while (false)

void CheckStopped() {
    CHECK(!RenderModule::IsInitialized());
    CHECK(RenderModule::GetNanoVGContext() == nullptr);
    CHECK(ImGui::GetCurrentContext() == nullptr);
    CHECK(ImPlot::GetCurrentContext() == nullptr);
    CHECK(RenderModule::GetWindowSize().x == 0);
    CHECK(RenderModule::GetGLFWWindowSize().y == 0);
    CHECK(RenderModule::GetFPS() == 0);
    CHECK(RenderModule::GetDeltaTime() == 0);
}

void CheckNoDisplay() {
    RenderModule::RequestClose();
    RenderModule::Run();
    RenderModule::Shutdown();
    CheckStopped();
    CHECK(!RenderModule::Init(0, 240));
    CHECK(!RenderModule::Init(320, -1));
    for (int attempt = 0; attempt < 2; ++attempt) {
        CHECK(!RenderModule::Init(320, 240));
        CheckStopped();
        RenderModule::RequestClose();
        RenderModule::Shutdown();
        RenderModule::Shutdown();
    }
}

void CheckBackend() {
    auto platform = render_module::detail::CreateGlfwDesktopBackend();
    CHECK(!platform->IsInitialized());
    CHECK(platform->ShouldClose());
    CHECK(!platform->CreateInputBackend());
    CHECK(!platform->CreatePresenter());
    CHECK(platform->GetWindowSize().width == 0);
    CHECK(platform->GetFramebufferSize().height == 0);
    platform->RequestClose();
    platform->Shutdown();
    CHECK(!platform->Init(0, 240, "Invalid"));

    CHECK(platform->Init(320, 240, "Desktop backend regression"));
    CHECK(platform->IsInitialized());
    CHECK(!platform->ShouldClose());
    GLFWwindow* window = glfwGetCurrentContext();
    CHECK(window != nullptr);
    CHECK(glfwGetWindowAttrib(window, GLFW_CONTEXT_VERSION_MAJOR) >= 3);
    CHECK(glfwGetWindowAttrib(window, GLFW_OPENGL_PROFILE) == GLFW_OPENGL_CORE_PROFILE);
    CHECK(glfwGetWindowAttrib(window, GLFW_RESIZABLE) == GLFW_TRUE);
    glfwMakeContextCurrent(nullptr);
    platform->MakeCurrent();
    CHECK(glfwGetCurrentContext() == window);
    CHECK(platform->GetProcAddressLoader()("glGetString") != nullptr);
    CHECK(gladLoadGLLoader(platform->GetProcAddressLoader()));
    CHECK(GLAD_GL_VERSION_3_3);

    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    auto input = platform->CreateInputBackend();
    auto presenter = platform->CreatePresenter();
    CHECK(input && presenter);
    CHECK(input->Init());
    CHECK(input->Init());
    CHECK(std::strcmp(io.BackendPlatformName, "imgui_impl_glfw") == 0);
    unsigned char* pixels = nullptr;
    int width = 0, height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

    glfwSetWindowSize(window, 400, 300);
    for (int attempt = 0; attempt < 100; ++attempt) {
        platform->PollEvents();
        if (platform->GetWindowSize().width == 400 &&
            platform->GetWindowSize().height == 300) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    const auto logical = platform->GetWindowSize();
    const auto framebuffer = platform->GetFramebufferSize();
    CHECK(logical.width == 400 && logical.height == 300);
    CHECK(framebuffer.width > 0 && framebuffer.height > 0);
    input->NewFrame();
    CHECK(io.DisplaySize.x == logical.width && io.DisplaySize.y == logical.height);
    CHECK(std::abs(io.DisplayFramebufferScale.x -
        float(framebuffer.width)/logical.width) < 0.001f);
    CHECK(std::abs(io.DisplayFramebufferScale.y -
        float(framebuffer.height)/logical.height) < 0.001f);
    CHECK(io.DeltaTime > 0);
    ImGui::NewFrame();
    ImGui::EndFrame();

    // Exercise the callbacks actually installed by the adapter. No OS input
    // injection is needed, and this also verifies callback installation/cleanup.
    const auto keyCallback = glfwSetKeyCallback(window, nullptr);
    const auto mouseCallback = glfwSetMouseButtonCallback(window, nullptr);
    CHECK(keyCallback && mouseCallback);
    glfwSetKeyCallback(window, keyCallback);
    glfwSetMouseButtonCallback(window, mouseCallback);
    io.AddFocusEvent(true);
    keyCallback(window, GLFW_KEY_A, 0, GLFW_PRESS, 0);
    mouseCallback(window, GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS, 0);
    input->NewFrame();
    ImGui::NewFrame();
    CHECK(ImGui::IsKeyDown(ImGuiKey_A));
    CHECK(ImGui::IsMouseDown(ImGuiMouseButton_Left));
    ImGui::EndFrame();
    keyCallback(window, GLFW_KEY_A, 0, GLFW_RELEASE, 0);
    mouseCallback(window, GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE, 0);
    input->NewFrame();
    ImGui::NewFrame();
    CHECK(!ImGui::IsKeyDown(ImGuiKey_A));
    CHECK(!ImGui::IsMouseDown(ImGuiMouseButton_Left));
    ImGui::EndFrame();

    glViewport(0, 0, framebuffer.width, framebuffer.height);
    glClearColor(0.25f, 0.5f, 0.75f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    presenter->Present();
    CHECK(glGetError() == GL_NO_ERROR);
    platform->RequestClose();
    CHECK(platform->ShouldClose());
    input->Shutdown();
    input->Shutdown();
    CHECK(io.BackendPlatformUserData == nullptr);
    CHECK(glfwSetKeyCallback(window, nullptr) == nullptr);
    input.reset();
    ImGui::DestroyContext();
    presenter.reset();
    platform->Shutdown();
    platform->Shutdown();
    CHECK(!platform->IsInitialized());
    CHECK(glfwGetCurrentContext() == nullptr);
    CHECK(platform->Init(320, 240, "Desktop reinitialization"));
    CHECK(!platform->ShouldClose());
}

void CheckRender() {
    RenderModule::EnableRootWindowDocking();
    for (int cycle = 0; cycle < 2; ++cycle) {
        CHECK(RenderModule::Init(640, 480, 0.0, "Desktop rendering regression"));
        CHECK(RenderModule::IsInitialized());
        CHECK(RenderModule::GetNanoVGContext());
        ImGui::GetIO().IniFilename = nullptr;
        CHECK(RenderModule::Init(123, 456)); // Existing Init is idempotent.
        const auto size = RenderModule::GetWindowSize();
        const auto legacySize = RenderModule::GetGLFWWindowSize();
        CHECK(size.x == 640 && size.y == 480);
        CHECK(size.x == legacySize.x && size.y == legacySize.y);
        CHECK(ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_DockingEnable);
        int frames = 0, canvases = 0, legacyCanvases = 0, offscreens = 0, views = 0;
        RenderModule::RegisterImGuiCallback([&] {
            ++frames;
            CHECK(ImGui::GetIO().DisplaySize.x == size.x);
            CHECK(RenderModule::GetRootDockspaceID() != 0);
            ImGui::SetNextWindowPos({10, 30}, ImGuiCond_Always);
            ImGui::SetNextWindowSize({280, 180}, ImGuiCond_Always);
            ImGui::Begin("Controls");
            ImGui::TextUnformatted("Desktop regression");
            if (ImPlot::BeginPlot("Plot", {220, 100})) {
                const double values[] = {0, 1, 0};
                ImPlot::PlotLine("line", values, 3);
                ImPlot::EndPlot();
            }
            ImGui::End();
            // Keep the render windows visible without depending on saved docking state.
            for (const char* name : {"Canvas", "Legacy", "3D"}) {
                ImGui::SetNextWindowPos({300, 30}, ImGuiCond_Always);
                ImGui::SetNextWindowSize({280, 180}, ImGuiCond_Always);
                ImGui::Begin(name);
                ImGui::End();
            }
            if (frames == 3) RenderModule::RequestClose();
        });
        RenderModule::RegisterCanvas("Canvas", [&](render_module::Canvas& canvas) {
            ++canvases;
            CHECK(canvas.Size().x > 0);
            nvgBeginPath(canvas.Graphics());
            nvgRect(canvas.Graphics(), 10, 10, 50, 50);
            nvgFillColor(canvas.Graphics(), nvgRGB(255, 0, 0));
            nvgFill(canvas.Graphics());
        });
        RenderModule::RegisterNanoVGCallback("Legacy", [&](NVGcontext* vg) {
            ++legacyCanvases;
            CHECK(vg == RenderModule::GetNanoVGContext());
        }, [&](NVGcontext*) { ++offscreens; });
        RenderModule::Register3DView("3D", [&](render_module::View3D& view) {
            ++views;
            view.Grid(4.0f, 1.0f);
            view.Box("box", {}, {1, 1, 1});
            view.PointCloud("points", {{0, 0, 0}, {1, 1, 1}});
        });
        glfwMakeContextCurrent(nullptr);
        RenderModule::Run();
        CHECK(frames == 3);
        CHECK(canvases > 0 && legacyCanvases > 0 && offscreens == 3 && views > 0);
        CHECK(RenderModule::GetFPS() > 0 && RenderModule::GetDeltaTime() > 0);
        CHECK(glGetError() == GL_NO_ERROR);
        RenderModule::Run(); // A closed platform must not start another frame.
        CHECK(frames == 3);
        glfwMakeContextCurrent(nullptr);
        RenderModule::Shutdown(); // Must make current before deleting GPU resources.
        CheckStopped();
        CHECK(glfwGetCurrentContext() == nullptr);
        RenderModule::Shutdown();
    }
}

} // namespace

int main(int argc, char** argv) {
    try {
        CHECK(argc == 2);
        if (std::strcmp(argv[1], "--no-display") == 0) CheckNoDisplay();
        else if (std::strcmp(argv[1], "--backend") == 0) CheckBackend();
        else if (std::strcmp(argv[1], "--render") == 0) CheckRender();
        else CHECK(false);
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
