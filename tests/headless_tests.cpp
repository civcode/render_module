#include <glad/glad.h>
#include <EGL/egl.h>

#include "render_module/render_module.hpp"
#include "platform/platform_backend.hpp"
#include "input/input_backend.hpp"
#include "present/presenter.hpp"
#include "core/root_framebuffer.hpp"
#include "core/render_output.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace {
#define CHECK(condition) do { if (!(condition)) \
    throw std::runtime_error("Check failed: " #condition); } while (false)

struct Target {
    GLint fbo = 0;
    GLint viewport[4] = {};
    void Capture() {
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fbo);
        glGetIntegerv(GL_VIEWPORT, viewport);
        CHECK(fbo != 0);
        CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
    }
    void CheckPixel(bool green) const {
        CHECK(fbo != 0 && viewport[2] > 0 && viewport[3] > 0);
        glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(fbo));
        unsigned char rgba[4] = {};
        glReadPixels(viewport[2]/2, viewport[3]/2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
        std::fprintf(stderr, "Headless %s target center RGBA: %u %u %u %u\n",
                     green ? "Magnum" : "NanoVG", rgba[0], rgba[1], rgba[2], rgba[3]);
        CHECK(rgba[green ? 1 : 0] > 200);
        CHECK(rgba[green ? 0 : 1] < 60 && rgba[2] < 60);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        CHECK(glGetError() == GL_NO_ERROR);
    }
};

void CheckProvider(const render_module::Config& config) {
    auto platform = render_module::detail::CreatePlatformBackend(config);
    CHECK(platform);
    CHECK(!platform->MakeCurrent());
    CHECK(!platform->IsInitialized());
    CHECK(!platform->CreatePresenter());
    CHECK(!platform->CreateInputBackend());
    CHECK(!platform->Initialize(0, 240, "Invalid"));
    CHECK(platform->Initialize(320, 240, "Headless context test"));
    CHECK(platform->MakeCurrent());
    CHECK(platform->GetProcAddress("glGetString"));
    CHECK(platform->GetProcAddress("glCreateShader"));
    CHECK(gladLoadGLLoader(platform->GetProcAddressLoader()));
    CHECK(render_module::detail::PrintGraphicsDiagnostics(*platform));
    CHECK(GLAD_GL_VERSION_3_3);
    CHECK(eglGetCurrentDisplay() != EGL_NO_DISPLAY);
    CHECK(eglGetCurrentContext() != EGL_NO_CONTEXT);
    const auto diagnostics = platform->Diagnostics();
    CHECK(!diagnostics.eglVendor.empty() && !diagnostics.eglVersion.empty());
    if (config.headlessContext == render_module::HeadlessContext::GlfwNullEgl ||
        config.nativeEgl.forcePbuffer)
        CHECK(eglGetCurrentSurface(EGL_DRAW) != EGL_NO_SURFACE);
    else {
        // Both supported native paths are valid, and the log states which ran.
        CHECK(diagnostics.provider.find(eglGetCurrentSurface(EGL_DRAW) == EGL_NO_SURFACE
              ? "surfaceless" : "pbuffer") != std::string::npos);
    }
    CHECK(platform->GetWindowSize().width == 320);
    CHECK(platform->GetFramebufferSize().height == 240);
    auto presenter = platform->CreatePresenter();
    CHECK(presenter);
    render_module::detail::RootFramebuffer root;
    CHECK(root.Resize(32, 24));
    root.BeginFrame();
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    const auto now = std::chrono::steady_clock::now();
    CHECK(presenter->Present(root.Complete(1, now, now)));
    root.Destroy();
    CHECK(glGetError() == GL_NO_ERROR);
    presenter.reset();
    platform->PollEvents();
    platform->RequestClose();
    CHECK(platform->ShouldClose());
    platform->Shutdown();
    platform->Shutdown();
    CHECK(!platform->IsInitialized());
    CHECK(eglGetCurrentContext() == EGL_NO_CONTEXT);
    CHECK(platform->GetProcAddressLoader()("glGetString") == nullptr);
    CHECK(platform->Initialize(320, 240, "Reinitialize"));
    CHECK(!platform->ShouldClose());
}

void CheckRendering(render_module::Config config) {
    for (int cycle = 0; cycle < 2; ++cycle) {
        config.width = 640;
        config.height = 480;
        config.fps = 0;
        CHECK(RenderModule::Init(config));
        CHECK(RenderModule::IsInitialized());
        CHECK(RenderModule::GetNanoVGContext());
        CHECK(glGetError() == GL_NO_ERROR);
        auto& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        CHECK(std::strcmp(io.BackendPlatformName, "render_module_headless_frame_metrics") == 0);
        CHECK(io.BackendPlatformUserData == nullptr);
        CHECK(!(io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable));
        Target canvasTarget, viewTarget;
        int frames = 0, canvases = 0, views = 0;
        RenderModule::RegisterImGuiCallback([&] {
            CHECK(io.DisplaySize.x == 640 && io.DisplaySize.y == 480);
            CHECK(io.DisplayFramebufferScale.x == 1 && io.DisplayFramebufferScale.y == 1);
            CHECK(io.DeltaTime > 0);
            CHECK(glGetError() == GL_NO_ERROR);
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
        CHECK(frames == 3 && canvases > 0 && views > 0);
        CHECK(RenderModule::GetFPS() > 0 && RenderModule::GetDeltaTime() > 0);
        CHECK(glGetError() == GL_NO_ERROR);
        GLint bound = -1;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &bound);
        const auto frame = render_module::detail::CompletedFrame();
        CHECK(frame.IsValid() && bound == static_cast<GLint>(frame.Framebuffer()));
        CHECK(frame.width == 640 && frame.height == 480 && frame.frameId == 3);
        CHECK(frame.renderCompleted >= frame.renderStarted);
        canvasTarget.CheckPixel(false);
        viewTarget.CheckPixel(true);
        RenderModule::Run();
        CHECK(frames == 3);
        // Exercise MakeCurrent on shutdown before deleting Magnum/NanoVG resources.
        const auto display = eglGetCurrentDisplay();
        CHECK(eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT));
        RenderModule::Shutdown();
        CHECK(!RenderModule::IsInitialized());
        CHECK(RenderModule::GetNanoVGContext() == nullptr);
        CHECK(ImGui::GetCurrentContext() == nullptr);
        CHECK(eglGetCurrentContext() == EGL_NO_CONTEXT);
        RenderModule::Shutdown();
    }
}

void CheckFailures(const render_module::Config& config) {
    auto invalid = config;
    invalid.width = 0;
    CHECK(!RenderModule::Init(invalid));
    invalid = config;
    invalid.backend = render_module::Backend::Headless;
    invalid.headlessContext = render_module::HeadlessContext::NativeEgl;
    invalid.nativeEgl.deviceIndex = std::numeric_limits<int>::max();
    CHECK(!RenderModule::Init(invalid));
    CHECK(!RenderModule::IsInitialized());
    CHECK(eglGetCurrentContext() == EGL_NO_CONTEXT);
    RenderModule::Shutdown();
    // The old overload must remain Desktop, even after Null platform hints were used.
    CHECK(!RenderModule::Init(320, 240));
    RenderModule::Shutdown();
    CHECK(!RenderModule::IsInitialized());
}
} // namespace

int main(int argc, char** argv) {
    try {
        CHECK(argc == 2);
        CHECK(std::getenv("DISPLAY") == nullptr);
        CHECK(std::getenv("WAYLAND_DISPLAY") == nullptr);
        render_module::Config config;
        config.backend = render_module::Backend::Headless;
        if (std::strcmp(argv[1], "native") == 0)
            config.headlessContext = render_module::HeadlessContext::NativeEgl;
        else if (std::strcmp(argv[1], "pbuffer") == 0) {
            config.headlessContext = render_module::HeadlessContext::NativeEgl;
            config.nativeEgl.forcePbuffer = true;
        } else CHECK(std::strcmp(argv[1], "glfw") == 0);
        CheckProvider(config);
        CheckRendering(config);
        CheckFailures(config);
        // Failure must not poison a later successful context initialization.
        CHECK(RenderModule::Init(config));
        RenderModule::Shutdown();
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        RenderModule::Shutdown();
        return 1;
    }
}
