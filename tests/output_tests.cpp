#include <glad/glad.h>
#include <imgui_internal.h>
#include <stb_image.h> // Existing NanoVG decoder; no second implementation.

#include "render_module/render_module.hpp"
#include "core/root_framebuffer.hpp"
#include "core/render_output.hpp"
#include "platform/platform_backend.hpp"
#include "present/image_presenter.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <string>

namespace {
using namespace render_module;
using namespace render_module::detail;
#define CHECK(condition) do { if (!(condition)) \
    throw std::runtime_error("Check failed: " #condition); } while (false)

GLint Integer(GLenum name) { GLint value = 0; glGetIntegerv(name, &value); return value; }
GLenum APIENTRY IncompleteFramebuffer(GLenum) { return GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT; }
void Pixel(const ImageRgba& image, int x, int y, int r, int g, int b) {
    const auto offset = (std::size_t(y)*image.width + x)*4;
    CHECK(std::abs(int(image.pixels[offset]) - r) <= 2);
    CHECK(std::abs(int(image.pixels[offset+1]) - g) <= 2);
    CHECK(std::abs(int(image.pixels[offset+2]) - b) <= 2);
}
ImageRgba Decode(const std::string& path) {
    ImageRgba result;
    int components = 0;
    auto* pixels = stbi_load(path.c_str(), &result.width, &result.height, &components, 4);
    CHECK(pixels != nullptr);
    result.pixels.assign(pixels, pixels + std::size_t(result.width)*result.height*4);
    stbi_image_free(pixels);
    return result;
}

void RootTests(const Config& config) {
    auto platform = CreatePlatformBackend(config);
    CHECK(platform && platform->Initialize(128, 96, "Root tests"));
    CHECK(gladLoadGLLoader(platform->GetProcAddressLoader()));
    {
        RootFramebuffer root;
        CHECK(!root.Resize(0, 10) && !root.Resize(-1, 10));
        CHECK(root.Framebuffer() == 0 && root.Generation() == 0);
        GLuint sentinel = 0, texture = 0, unpack = 0;
        glGenFramebuffers(1, &sentinel);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, sentinel);
        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);
        glGenBuffers(1, &unpack);
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, unpack);
        glBufferData(GL_PIXEL_UNPACK_BUFFER, 4, nullptr, GL_STREAM_DRAW);
        CHECK(root.Resize(64, 48));
        CHECK(Integer(GL_READ_FRAMEBUFFER_BINDING) == GLint(sentinel));
        CHECK(Integer(GL_DRAW_FRAMEBUFFER_BINDING) == 0);
        CHECK(Integer(GL_TEXTURE_BINDING_2D) == GLint(texture));
        CHECK(Integer(GL_PIXEL_UNPACK_BUFFER_BINDING) == GLint(unpack));
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
        root.BeginFrame();
        CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
        glBindTexture(GL_TEXTURE_2D, root.ColorTexture());
        GLint format = 0;
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_INTERNAL_FORMAT, &format);
        CHECK(format == GL_RGBA8);
        GLint depthType = -1;
        glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                              GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE, &depthType);
        CHECK(depthType == GL_NONE);
        const auto started = std::chrono::steady_clock::now();
        glClearColor(1, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glEnable(GL_SCISSOR_TEST);
        glScissor(0, 0, 64, 24);
        glClearColor(0, 0, 1, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glDisable(GL_SCISSOR_TEST);
        const auto frame = root.Complete(1, started, std::chrono::steady_clock::now());
        CHECK(frame.IsValid() && frame.framebufferGeneration == 1);
        CHECK(frame.renderCompleted >= frame.renderStarted);
        const GLuint oldFbo = root.Framebuffer(), oldTexture = root.ColorTexture();
        CHECK(root.Resize(64, 48) && root.Generation() == 1 && frame.IsValid());
        CHECK(!root.Resize(Integer(GL_MAX_TEXTURE_SIZE) + 1, 48));
        CHECK(root.Framebuffer() == oldFbo && frame.IsValid());
        // Exercise an actual allocation/completeness failure without exhausting
        // GPU memory. Restore GLAD's dispatch immediately after the operation.
        const auto checkStatus = glad_glCheckFramebufferStatus;
        glad_glCheckFramebufferStatus = &IncompleteFramebuffer;
        const bool resized = root.Resize(80, 60);
        glad_glCheckFramebufferStatus = checkStatus;
        CHECK(!resized && root.Framebuffer() == oldFbo && root.ColorTexture() == oldTexture);
        CHECK(root.Generation() == 1 && frame.IsValid());
        CHECK(Integer(GL_READ_FRAMEBUFFER_BINDING) == GLint(oldFbo));
        CHECK(Integer(GL_DRAW_FRAMEBUFFER_BINDING) == GLint(oldFbo));
        CHECK(glGetError() == GL_NO_ERROR);

        GLuint pack = 0;
        glGenBuffers(1, &pack);
        glBindBuffer(GL_PIXEL_PACK_BUFFER, pack);
        glBufferData(GL_PIXEL_PACK_BUFFER, 4, nullptr, GL_STREAM_READ);
        glPixelStorei(GL_PACK_ALIGNMENT, 8);
        glPixelStorei(GL_PACK_ROW_LENGTH, 999);
        glPixelStorei(GL_PACK_SKIP_ROWS, 2);
        glPixelStorei(GL_PACK_SKIP_PIXELS, 3);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, sentinel);
        ImageRgba image;
        CHECK(ImagePresenter::Read(frame, image));
        CHECK(Integer(GL_READ_FRAMEBUFFER_BINDING) == GLint(sentinel));
        CHECK(Integer(GL_DRAW_FRAMEBUFFER_BINDING) == GLint(oldFbo));
        CHECK(Integer(GL_PIXEL_PACK_BUFFER_BINDING) == GLint(pack));
        CHECK(Integer(GL_PACK_ALIGNMENT) == 8 && Integer(GL_PACK_ROW_LENGTH) == 999);
        CHECK(Integer(GL_PACK_SKIP_ROWS) == 2 && Integer(GL_PACK_SKIP_PIXELS) == 3);
        Pixel(image, 10, 0, 255, 0, 0); // Top row, exactly one flip.
        Pixel(image, 10, 47, 0, 0, 255);
        const std::string path = std::string(RENDER_MODULE_ARTIFACT_DIR) +
            (config.backend == Backend::Headless ? "/orientation-headless.png" : "/orientation-desktop.png");
        CHECK(ImagePresenter::WritePng(path, image));
        const auto decoded = Decode(path);
        CHECK(decoded.width == 64 && decoded.height == 48 && decoded.pixels == image.pixels);
        CHECK(!ImagePresenter::WritePng("/nonexistent-render-module-directory/test.png", image));
        glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
        glPixelStorei(GL_PACK_ALIGNMENT, 4);
        glPixelStorei(GL_PACK_ROW_LENGTH, 0);
        glPixelStorei(GL_PACK_SKIP_ROWS, 0);
        glPixelStorei(GL_PACK_SKIP_PIXELS, 0);

        glBindFramebuffer(GL_FRAMEBUFFER, oldFbo);
        CHECK(root.Resize(80, 60));
        CHECK(root.Generation() == 2 && !frame.IsValid());
        CHECK(frame.Framebuffer() == 0 && frame.ColorTexture() == 0);
        CHECK(!glIsFramebuffer(oldFbo) && !glIsTexture(oldTexture));
        CHECK(Integer(GL_READ_FRAMEBUFFER_BINDING) == GLint(root.Framebuffer()));
        CHECK(Integer(GL_DRAW_FRAMEBUFFER_BINDING) == GLint(root.Framebuffer()));
        CHECK(Integer(GL_TEXTURE_BINDING_2D) == GLint(root.ColorTexture()));
        CHECK(!ImagePresenter::Read(frame, image));
        root.BeginFrame();
        const auto next = root.Complete(2, started, std::chrono::steady_clock::now());
        root.BeginFrame();
        CHECK(!next.IsValid());
        const auto last = root.Complete(3, started, std::chrono::steady_clock::now());
        const auto finalFbo = root.Framebuffer();
        root.Destroy();
        root.Destroy();
        CHECK(!last.IsValid() && last.Framebuffer() == 0 && !glIsFramebuffer(finalFbo));
        CHECK(root.Width() == 0 && root.Height() == 0);
        glDeleteBuffers(1, &pack);
        glDeleteBuffers(1, &unpack);
        glDeleteTextures(1, &texture);
        glDeleteFramebuffers(1, &sentinel);
        CHECK(glGetError() == GL_NO_ERROR);
    }
    platform->Shutdown();
}

void ResizeTests(const Config& config) {
    CHECK(RenderModule::Init(config));
    ImGui::GetIO().IniFilename = nullptr;
    CHECK(!RenderModule::SaveScreenshot("before-frame.png"));
    CHECK(!RequestVirtualDisplaySize(0, 20));
    int frames = 0, canvasFrames = 0;
    GLint expectedRoot = 0;
    RenderModule::RegisterImGuiCallback([&] {
        ++frames;
        expectedRoot = Integer(GL_DRAW_FRAMEBUFFER_BINDING);
        CHECK(expectedRoot != 0);
        const int width = frames < 3 ? config.width : 800;
        const int height = frames < 3 ? config.height : 600;
        CHECK(ImGui::GetIO().DisplaySize.x == width && ImGui::GetIO().DisplaySize.y == height);
        CHECK(RenderModule::GetWindowSize().x == width);
        ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
        ImGui::SetNextWindowSize({float(width), float(height)}, ImGuiCond_Always);
        ImGui::Begin("Resize canvas"); ImGui::End();
        if (frames == 2) CHECK(RequestVirtualDisplaySize(800, 600));
        if (frames == 5) RenderModule::RequestClose();
        ImGui::GetForegroundDrawList()->AddCallback([](const ImDrawList*, const ImDrawCmd* cmd) {
            CHECK(Integer(GL_DRAW_FRAMEBUFFER_BINDING) == *static_cast<GLint*>(cmd->UserCallbackData));
        }, &expectedRoot);
    });
    RenderModule::RegisterCanvas("Resize canvas", [&](Canvas& canvas) {
        ++canvasFrames;
        if (frames >= 3) CHECK(canvas.Size().x > config.width);
        nvgBeginPath(canvas.Graphics());
        nvgRect(canvas.Graphics(), 0, 0, 50, 50);
        nvgFillColor(canvas.Graphics(), nvgRGB(255, 0, 0));
        nvgFill(canvas.Graphics());
    });
    RenderModule::RegisterCanvas("Restoration probe", [](Canvas&) {}, [&](NVGcontext*) {
        // Includes first-time Canvas FBO allocation and resize, not only reuse.
        CHECK(Integer(GL_DRAW_FRAMEBUFFER_BINDING) == expectedRoot);
        CHECK(Integer(GL_READ_FRAMEBUFFER_BINDING) == expectedRoot);
        RenderModule::IsolatedFrameBuffer([](NVGcontext*) { glBindFramebuffer(GL_FRAMEBUFFER, 0); });
        CHECK(Integer(GL_DRAW_FRAMEBUFFER_BINDING) == expectedRoot);
    });
    RenderModule::Run();
    CHECK(frames == 5 && canvasFrames > 0);
    const auto frame = CompletedFrame();
    CHECK(frame.IsValid() && frame.width == 800 && frame.height == 600);
    CHECK(frame.framebufferGeneration == 2 && frame.frameId == 5);
    CHECK(RenderModule::SaveScreenshot(std::string(RENDER_MODULE_ARTIFACT_DIR) + "/resized.png"));
    CHECK(glGetError() == GL_NO_ERROR);
    RenderModule::Shutdown();
    CHECK(!frame.IsValid() && frame.Framebuffer() == 0);
}

void Compare(const ImageRgba& actual, const ImageRgba& golden, const std::string& path) {
    CHECK(actual.width == golden.width && actual.height == golden.height);
    ImageRgba difference = actual;
    double totalError = 0;
    std::size_t differing = 0;
    int maximum = 0;
    for (std::size_t i = 0; i < actual.pixels.size(); i += 4) {
        int pixelError = 0;
        for (int channel = 0; channel < 4; ++channel) {
            const int error = std::abs(int(actual.pixels[i+channel]) - golden.pixels[i+channel]);
            totalError += error;
            maximum = std::max(maximum, error);
            pixelError = std::max(pixelError, error);
            difference.pixels[i+channel] = channel == 3 ? 255 : std::min(255, error*4);
        }
        differing += pixelError > 12;
    }
    const double mean = totalError / actual.pixels.size();
    const double fraction = double(differing) / (actual.width*actual.height);
    std::fprintf(stderr, "Full-root comparison: mean=%.6f max=%d pixels>12=%.4f%%\n",
                 mean, maximum, fraction*100);
    if (mean > 1.0 || fraction > 0.005) {
        ImagePresenter::WritePng(path + ".diff.png", difference);
        throw std::runtime_error("Root image differs from golden; inspect " + path + " and .diff.png");
    }
}

void VisualTest(const Config& config, bool updateGolden) {
    CHECK(RenderModule::Init(config));
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigWindowsResizeFromEdges = false;
    ImGui::GetStyle().AntiAliasedLines = true;
    RenderModule::EnableRootWindowDocking();
    int frames = 0, canvasFrames = 0, viewFrames = 0;
    RenderModule::RegisterImGuiCallback([&] {
        ++frames;
        io.DeltaTime = 1.0f/60.0f;
        ImGui::GetCurrentContext()->Time = frames/60.0;
        if (frames == 1) {
            const ImGuiID root = RenderModule::GetRootDockspaceID();
            ImGui::DockBuilderRemoveNode(root);
            ImGui::DockBuilderAddNode(root, ImGuiDockNodeFlags_DockSpace);
            ImGui::DockBuilderSetNodeSize(root, io.DisplaySize);
            ImGuiID left, right, controls, plot, canvas, view;
            ImGui::DockBuilderSplitNode(root, ImGuiDir_Left, 0.5f, &left, &right);
            ImGui::DockBuilderSplitNode(left, ImGuiDir_Up, 0.46f, &controls, &plot);
            ImGui::DockBuilderSplitNode(right, ImGuiDir_Up, 0.46f, &canvas, &view);
            ImGui::DockBuilderDockWindow("Controls", controls);
            ImGui::DockBuilderDockWindow("Plot", plot);
            ImGui::DockBuilderDockWindow("Canvas", canvas);
            ImGui::DockBuilderDockWindow("View3D", view);
            ImGui::DockBuilderFinish(root);
        }
        ImGui::Begin("Controls");
        ImGui::TextUnformatted("Deterministic root UI");
        ImGui::Button("Static button", {150, 26});
        bool checked = true;
        ImGui::Checkbox("Composition enabled", &checked);
        float value = 0.375f;
        ImGui::SliderFloat("Value", &value, 0, 1);
        ImGui::BeginChild("Clipped", {180, 38}, true);
        ImGui::TextUnformatted("This text is deliberately clipped at the child boundary");
        ImGui::EndChild();
        ImGui::End();
        ImGui::Begin("Plot");
        if (ImPlot::BeginPlot("Fixed data", {-1, -1}, ImPlotFlags_NoInputs | ImPlotFlags_NoLegend)) {
            ImPlot::SetupAxes("x", "y", ImPlotAxisFlags_NoHighlight, ImPlotAxisFlags_NoHighlight);
            ImPlot::SetupAxesLimits(0, 4, 0, 1, ImGuiCond_Always);
            const double y[] = {0.2, 0.8, 0.4, 0.7, 0.3};
            ImPlot::SetNextLineStyle({0.95f, 0.25f, 0.1f, 1}, 2);
            ImPlot::PlotLine("series", y, 5);
            ImPlot::EndPlot();
        }
        ImGui::End();
        auto* overlay = ImGui::GetForegroundDrawList();
        overlay->AddRectFilled({4, 4}, {20, 12}, IM_COL32(255, 0, 0, 255));
        overlay->AddRectFilled({4, 464}, {20, 476}, IM_COL32(0, 0, 255, 255));
        overlay->AddRectFilled({490, 30}, {625, 52}, IM_COL32(25, 30, 40, 220));
        overlay->AddText({496, 34}, IM_COL32(255, 255, 255, 255), "Root overlay");
        if (frames == 5) RenderModule::RequestClose();
    });
    RenderModule::RegisterCanvas("Canvas", [&](Canvas& canvas) {
        ++canvasFrames;
        auto* vg = canvas.Graphics();
        nvgBeginPath(vg); nvgRect(vg, 12, 12, 85, 48);
        nvgFillColor(vg, nvgRGB(230, 35, 45)); nvgFill(vg);
        nvgBeginPath(vg); nvgCircle(vg, canvas.Size().x*0.60f, canvas.Size().y*0.62f, 34);
        nvgFillColor(vg, nvgRGB(255, 200, 30)); nvgFill(vg);
        nvgBeginPath(vg); nvgMoveTo(vg, 15, canvas.Size().y-15);
        nvgLineTo(vg, 100, canvas.Size().y-40);
        nvgStrokeColor(vg, nvgRGB(30, 220, 150)); nvgStrokeWidth(vg, 4); nvgStroke(vg);
    });
    View3DOptions options;
    options.showStatus = false;
    RenderModule::Register3DView("View3D", [&](View3D& view) {
        ++viewFrames;
        view.Camera().LookAt({3, -4, 3}, {0, 0, 0});
        view.Grid(2, 0.5f);
        view.Box("fixed", {}, {1.2f, 1.2f, 1.2f}, {0.15f, 0.65f, 0.95f, 1});
        view.Axes("axes", {}, 1.2f);
    }, options);
    RenderModule::Run();
    CHECK(frames == 5 && canvasFrames >= 4 && viewFrames >= 4);
    const auto frame = CompletedFrame();
    CHECK(frame.IsValid());
    ImageRgba actual;
    CHECK(ImagePresenter::Read(frame, actual));
    const std::string path = std::string(RENDER_MODULE_ARTIFACT_DIR) + "/root-ui-actual.png";
    CHECK(RenderModule::SaveScreenshot(path)); // Keep diagnostics even if a semantic assertion fails.
    CHECK(frame.width == 640 && frame.height == 480 && frame.frameId == 5);
    Pixel(actual, 6, 6, 255, 0, 0);
    Pixel(actual, 6, 474, 0, 0, 255);
    // Large saturated regions additionally guard against a missing Canvas/View3D.
    int red = 0, yellow = 0, cyan = 0;
    for (int y = 24; y < 475; ++y) for (int x = 330; x < 630; ++x) {
        const auto i = (y*640 + x)*4;
        const int r = actual.pixels[i], g = actual.pixels[i+1], b = actual.pixels[i+2];
        red += r > 180 && g < 70 && b < 90;
        yellow += r > 220 && g > 160 && b < 80;
        cyan += r < 100 && g > 100 && b > 150;
    }
    CHECK(red > 2000 && yellow > 2000 && cyan > 1000);
    const auto decoded = Decode(path);
    CHECK(decoded.width == 640 && decoded.height == 480 && decoded.pixels == actual.pixels);
    if (updateGolden) CHECK(ImagePresenter::WritePng(RENDER_MODULE_GOLDEN_PATH, actual));
    else Compare(actual, Decode(RENDER_MODULE_GOLDEN_PATH), path);
    CHECK(glGetError() == GL_NO_ERROR);
    RenderModule::Shutdown();
    CHECK(!frame.IsValid());
}
} // namespace

int main(int argc, char** argv) {
    try {
        CHECK(argc >= 3);
        std::filesystem::create_directories(RENDER_MODULE_ARTIFACT_DIR);
        Config config;
        config.width = 640; config.height = 480; config.fps = 0;
        if (std::string(argv[2]) == "headless") {
            CHECK(std::getenv("DISPLAY") == nullptr && std::getenv("WAYLAND_DISPLAY") == nullptr);
            config.backend = Backend::Headless;
            config.headlessContext = HeadlessContext::NativeEgl;
        } else CHECK(std::string(argv[2]) == "desktop");
        const std::string test = argv[1];
        if (test == "root") RootTests(config);
        else if (test == "resize") ResizeTests(config);
        else if (test == "visual") VisualTest(config, argc == 4 && std::string(argv[3]) == "--update-golden");
        else CHECK(false);
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        RenderModule::Shutdown();
        return 1;
    }
}
