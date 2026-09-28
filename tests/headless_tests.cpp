#include <catch2/catch_test_macros.hpp>
#include <glad/glad.h>
#include <EGL/egl.h>

#include "render_module/render_module.hpp"
#include "platform/platform_backend.hpp"
#include "input/input_backend.hpp"
#include "present/presenter.hpp"
#include "core/root_framebuffer.hpp"
#include "core/render_output.hpp"
#include "render_module_test_guard.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace {

struct Target {
    GLint fbo = 0;
    GLint viewport[4] = {};
    void Capture() {
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fbo);
        glGetIntegerv(GL_VIEWPORT, viewport);
        REQUIRE((fbo != 0));
        REQUIRE((glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE));
    }
    void CheckPixel(bool green) const {
        REQUIRE((fbo != 0 && viewport[2] > 0 && viewport[3] > 0));
        glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(fbo));
        unsigned char rgba[4] = {};
        glReadPixels(viewport[2]/2, viewport[3]/2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
        std::fprintf(stderr, "Headless %s target center RGBA: %u %u %u %u\n",
                     green ? "Magnum" : "NanoVG", rgba[0], rgba[1], rgba[2], rgba[3]);
        REQUIRE((rgba[green ? 1 : 0] > 200));
        REQUIRE((rgba[green ? 0 : 1] < 60 && rgba[2] < 60));
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        REQUIRE((glGetError() == GL_NO_ERROR));
    }
};

void CheckProvider(const render_module::Config& config) {
    auto platform = render_module::detail::CreatePlatformBackend(config);
    REQUIRE((platform));
    REQUIRE((!platform->MakeCurrent()));
    REQUIRE((!platform->IsInitialized()));
    REQUIRE((!platform->CreatePresenter()));
    REQUIRE((!platform->CreateInputBackend()));
    REQUIRE((!platform->Initialize(0, 240, "Invalid")));
    REQUIRE((platform->Initialize(320, 240, "Headless context test")));
    REQUIRE((platform->MakeCurrent()));
    REQUIRE((platform->GetProcAddress("glGetString")));
    REQUIRE((platform->GetProcAddress("glCreateShader")));
    REQUIRE((gladLoadGLLoader(platform->GetProcAddressLoader())));
    REQUIRE((render_module::detail::PrintGraphicsDiagnostics(*platform)));
    REQUIRE((GLAD_GL_VERSION_3_3));
    REQUIRE((eglGetCurrentDisplay() != EGL_NO_DISPLAY));
    REQUIRE((eglGetCurrentContext() != EGL_NO_CONTEXT));
    const auto diagnostics = platform->Diagnostics();
    REQUIRE((!diagnostics.eglVendor.empty() && !diagnostics.eglVersion.empty()));
    if (config.headlessContext == render_module::HeadlessContext::GlfwNullEgl ||
        config.nativeEgl.forcePbuffer)
        REQUIRE((eglGetCurrentSurface(EGL_DRAW) != EGL_NO_SURFACE));
    else {
        // Both supported native paths are valid, and the log states which ran.
        REQUIRE((diagnostics.provider.find(eglGetCurrentSurface(EGL_DRAW) == EGL_NO_SURFACE
              ? "surfaceless" : "pbuffer") != std::string::npos));
    }
    REQUIRE((platform->GetWindowSize().width == 320));
    REQUIRE((platform->GetFramebufferSize().height == 240));
    auto presenter = platform->CreatePresenter();
    REQUIRE((presenter));
    render_module::detail::RootFramebuffer root;
    REQUIRE((root.Resize(32, 24)));
    root.BeginFrame();
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    const auto now = std::chrono::steady_clock::now();
    REQUIRE((presenter->Present(root.Complete(1, now, now))));
    root.Destroy();
    REQUIRE((glGetError() == GL_NO_ERROR));
    presenter.reset();
    platform->PollEvents();
    platform->RequestClose();
    REQUIRE((platform->ShouldClose()));
    platform->Shutdown();
    platform->Shutdown();
    REQUIRE((!platform->IsInitialized()));
    REQUIRE((eglGetCurrentContext() == EGL_NO_CONTEXT));
    REQUIRE((platform->GetProcAddressLoader()("glGetString") == nullptr));
    REQUIRE((platform->Initialize(320, 240, "Reinitialize")));
    REQUIRE((!platform->ShouldClose()));
}

void CheckRendering(render_module::Config config) {
    for (int cycle = 0; cycle < 2; ++cycle) {
        config.width = 640;
        config.height = 480;
        config.fps = 0;
        REQUIRE((RenderModule::Init(config)));
        REQUIRE((RenderModule::IsInitialized()));
        REQUIRE((RenderModule::GetNanoVGContext()));
        REQUIRE((glGetError() == GL_NO_ERROR));
        auto& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        REQUIRE((std::strcmp(io.BackendPlatformName, "render_module_remote_input") == 0));
        REQUIRE((io.BackendPlatformUserData != nullptr));
        REQUIRE((!(io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)));
        Target canvasTarget, viewTarget;
        int frames = 0, canvases = 0, views = 0;
        RenderModule::RegisterImGuiCallback([&] {
            REQUIRE((io.DisplaySize.x == 640 && io.DisplaySize.y == 480));
            REQUIRE((io.DisplayFramebufferScale.x == 1 && io.DisplayFramebufferScale.y == 1));
            REQUIRE((io.DeltaTime > 0));
            REQUIRE((glGetError() == GL_NO_ERROR));
            for (const char* name : {"Canvas", "View3D"}) {
                ImGui::SetNextWindowPos({10, 10}, ImGuiCond_Always);
                ImGui::SetNextWindowSize({300, 300}, ImGuiCond_Always);
                ImGui::Begin(name);
                ImGui::End();
            }
            if (++frames == 3) RenderModule::RequestClose();
        });
        RenderModule::RegisterCanvas("Canvas", [&](render_module::Canvas& canvas) {
            ++canvases;
            canvasTarget.Capture();
            nvgBeginPath(canvas.Graphics());
            nvgRect(canvas.Graphics(), 0, 0, canvas.Size().x, canvas.Size().y);
            nvgFillColor(canvas.Graphics(), nvgRGB(255, 0, 0));
            nvgFill(canvas.Graphics());
        });
        RenderModule::Register3DView("View3D", [&](render_module::View3D& view) {
            ++views;
            viewTarget.Capture();
            view.Camera().LookAt({0, 0, 3}, {0, 0, 0}, {0, 1, 0});
            view.TriangleMesh("triangle", {{-1, -1, -0.1f}, {1, -1, -0.1f}, {0, 1, -0.1f}},
                              {0, 1, 2}, {1, 0, 0, 1});
            // Flat-shaded points give a deterministic pixel independent of lighting.
            view.PointCloud("points", {{0, 0, 0}}, {0, 1, 0, 1}, 15);
        });
        RenderModule::Run();
        REQUIRE((frames == 3 && canvases > 0 && views > 0));
        REQUIRE((RenderModule::GetFPS() > 0 && RenderModule::GetDeltaTime() > 0));
        REQUIRE((glGetError() == GL_NO_ERROR));
        GLint bound = -1;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &bound);
        const auto frame = render_module::detail::CompletedFrame();
        REQUIRE((frame.IsValid() && bound == static_cast<GLint>(frame.Framebuffer())));
        REQUIRE((frame.width == 640 && frame.height == 480 && frame.frameId == 3));
        REQUIRE((frame.renderCompleted >= frame.renderStarted));
        canvasTarget.CheckPixel(false);
        viewTarget.CheckPixel(true);
        RenderModule::Run();
        REQUIRE((frames == 3));
        // Exercise MakeCurrent on shutdown before deleting Magnum/NanoVG resources.
        const auto display = eglGetCurrentDisplay();
        REQUIRE((eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT)));
        RenderModule::Shutdown();
        REQUIRE((!RenderModule::IsInitialized()));
        REQUIRE((RenderModule::GetNanoVGContext() == nullptr));
        REQUIRE((ImGui::GetCurrentContext() == nullptr));
        REQUIRE((eglGetCurrentContext() == EGL_NO_CONTEXT));
        RenderModule::Shutdown();
    }
}

void CheckFailures(const render_module::Config& config) {
    auto invalid = config;
    invalid.width = 0;
    REQUIRE((!RenderModule::Init(invalid)));
    invalid = config;
    invalid.backend = render_module::Backend::Headless;
    invalid.headlessContext = render_module::HeadlessContext::NativeEgl;
    invalid.nativeEgl.deviceIndex = std::numeric_limits<int>::max();
    REQUIRE((!RenderModule::Init(invalid)));
    REQUIRE((!RenderModule::IsInitialized()));
    REQUIRE((eglGetCurrentContext() == EGL_NO_CONTEXT));
    RenderModule::Shutdown();
    // The old overload must remain Desktop, even after Null platform hints were used.
    REQUIRE((!RenderModule::Init(320, 240)));
    RenderModule::Shutdown();
    REQUIRE((!RenderModule::IsInitialized()));
}
void RunProvider(render_module::HeadlessContext context, bool forcePbuffer = false) {
    RenderModuleTestGuard cleanup;
    REQUIRE((std::getenv("DISPLAY") == nullptr));
    REQUIRE((std::getenv("WAYLAND_DISPLAY") == nullptr));
    render_module::Config config;
    config.backend = render_module::Backend::Headless;
    config.headlessContext = context;
    config.nativeEgl.forcePbuffer = forcePbuffer;
    CheckProvider(config);
    CheckRendering(config);
    CheckFailures(config);
    // Failure must not poison a later successful context initialization.
    REQUIRE((RenderModule::Init(config)));
    RenderModule::Shutdown();
}
} // namespace

TEST_CASE("Headless GLFW Null EGL provider", "[headless][provider-glfw]") {
    RunProvider(render_module::HeadlessContext::GlfwNullEgl);
}

TEST_CASE("Headless native EGL provider", "[headless][provider-native]") {
    RunProvider(render_module::HeadlessContext::NativeEgl);
}

TEST_CASE("Headless native EGL pbuffer provider", "[headless][provider-pbuffer]") {
    RunProvider(render_module::HeadlessContext::NativeEgl, true);
}
