#include "render_module/render_module.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>
#include <implot.h>
#define NANOVG_GL3_IMPLEMENTATION
#include "nanovg_gl.h"
#include "nanovg_gl_utils.h"

#include "canvas_internal.hpp"
#include "view3d_internal.hpp"
#include "render_module/zoom_view.hpp"

DebugConsole RenderModule::console_;

namespace {

struct PaintWindow {
    std::string name;
    RenderModule::CanvasCallback callback;
    std::function<void(NVGcontext*)> offscreenCallback;
    render_module::detail::CanvasStorage canvas;
    GLuint fbo = 0;
    GLuint texture = 0;
    GLuint depthStencil = 0;
    int pixelWidth = 0;
    int pixelHeight = 0;
};

struct View3DWindow {
    std::string name;
    RenderModule::View3DCallback callback;
    render_module::View3DOptions options{};
    std::shared_ptr<render_module::detail::View3DStorage> storage;
    GLuint fbo = 0;
    GLuint texture = 0;
    GLuint depthStencil = 0;
    int pixelWidth = 0;
    int pixelHeight = 0;
};

struct AppContext {
    GLFWwindow* window = nullptr;
    NVGcontext* vg = nullptr;
    std::vector<std::function<void()>> imguiCallbacks;
    std::vector<PaintWindow> paintWindows;
    std::vector<View3DWindow> view3DWindows;
    double fpsSetpoint = 30.0;
    double fpsCurrent = 0.0;
    double deltaTime = 0.0;
    bool glfwInitialized = false;
    bool imguiGlfwInitialized = false;
    bool imguiOpenGLInitialized = false;
    bool initialized = false;
};

struct Settings {
    bool rootWindowDockingEnabled = false;
    bool renderDebugConsole = false;
};

AppContext ctx;
Settings settings;

template<class WindowT>
void DestroyFBO(WindowT& window) {
    if (window.depthStencil) glDeleteRenderbuffers(1, &window.depthStencil);
    if (window.texture) glDeleteTextures(1, &window.texture);
    if (window.fbo) glDeleteFramebuffers(1, &window.fbo);
    window.depthStencil = 0;
    window.texture = 0;
    window.fbo = 0;
    window.pixelWidth = 0;
    window.pixelHeight = 0;
}

template<class WindowT>
bool CreateFBO(WindowT& window, int pixelWidth, int pixelHeight) {
    DestroyFBO(window);

    glGenFramebuffers(1, &window.fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, window.fbo);

    glGenTextures(1, &window.texture);
    glBindTexture(GL_TEXTURE_2D, window.texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, pixelWidth, pixelHeight, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, window.texture, 0);

    // A packed depth/stencil attachment supports both NanoVG stencil strokes and 3D depth testing.
    glGenRenderbuffers(1, &window.depthStencil);
    glBindRenderbuffer(GL_RENDERBUFFER, window.depthStencil);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, pixelWidth, pixelHeight);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                              GL_RENDERBUFFER, window.depthStencil);

    const bool complete =
        glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glBindRenderbuffer(GL_RENDERBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    if (!complete) {
        std::fprintf(stderr, "RenderModule: framebuffer creation failed for '%s'.\n",
                     window.name.c_str());
        DestroyFBO(window);
        return false;
    }

    window.pixelWidth = pixelWidth;
    window.pixelHeight = pixelHeight;
    return true;
}

void AddFontDirectory(std::vector<std::string>& directories, std::string directory) {
    if (directory.empty()) return;
    while (directory.size() > 1 && directory.back() == '/') directory.pop_back();
    if (std::find(directories.begin(), directories.end(), directory) == directories.end()) {
        directories.push_back(std::move(directory));
    }
}

std::vector<std::string> FontDirectories() {
    std::vector<std::string> directories;
    if (const char* configured = std::getenv("RENDER_MODULE_FONT_DIR")) {
        AddFontDirectory(directories, configured);
    }
#ifdef RENDER_MODULE_BUILD_FONT_DIR
    AddFontDirectory(directories, RENDER_MODULE_BUILD_FONT_DIR);
#endif
#ifdef RENDER_MODULE_DEFAULT_FONT_DIR
    AddFontDirectory(directories, RENDER_MODULE_DEFAULT_FONT_DIR);
#endif
    if (const char* home = std::getenv("HOME")) {
        const std::string userFonts = std::string(home) + "/.local/share/render-module/fonts";
        AddFontDirectory(directories, userFonts);
        AddFontDirectory(directories, userFonts + "/roboto"); // v0.1 install layout.
    }
    AddFontDirectory(directories, "/usr/local/share/render-module/fonts");
    AddFontDirectory(directories, "/usr/share/render-module/fonts");
    return directories;
}

bool LoadFontFace(NVGcontext* vg,
                  const char* alias,
                  const std::vector<std::string>& directories,
                  std::initializer_list<const char*> fileNames) {
    for (const std::string& directory : directories) {
        for (const char* fileName : fileNames) {
            const std::string path = directory + "/" + fileName;
            if (FILE* file = std::fopen(path.c_str(), "rb")) {
                std::fclose(file);
                if (nvgCreateFont(vg, alias, path.c_str()) != -1) return true;
            }
        }
    }
    return false;
}

void LoadFonts(NVGcontext* vg) {
    const std::vector<std::string> directories = FontDirectories();
    bool regularLoaded = LoadFontFace(vg, "sans", directories, {"Roboto-Regular.ttf"});
    LoadFontFace(vg, "sans-bold", directories, {"Roboto-Bold.ttf", "Roboto-Regular.ttf"});
    LoadFontFace(vg, "sans-italic", directories, {"Roboto-Italic.ttf", "Roboto-Regular.ttf"});
    LoadFontFace(vg, "sans-bold-italic", directories,
                 {"Roboto-BoldItalic.ttf", "Roboto-Bold.ttf", "Roboto-Regular.ttf"});
    const bool monoLoaded = LoadFontFace(vg, "mono", directories,
                                        {"RobotoMono-Regular.ttf", "Roboto-Regular.ttf"});
    LoadFontFace(vg, "mono-bold", directories,
                 {"RobotoMono-Bold.ttf", "Roboto-Bold.ttf", "Roboto-Regular.ttf"});
    LoadFontFace(vg, "mono-italic", directories,
                 {"RobotoMono-Italic.ttf", "Roboto-Italic.ttf", "Roboto-Regular.ttf"});
    LoadFontFace(vg, "mono-bold-italic", directories,
                 {"RobotoMono-BoldItalic.ttf", "RobotoMono-Bold.ttf", "Roboto-Regular.ttf"});

    if (!regularLoaded || !monoLoaded) {
        std::fprintf(stderr,
                     "RenderModule: NanoVG fonts were not found. Set "
                     "RENDER_MODULE_FONT_DIR to a directory containing Roboto-Regular.ttf "
                     "and RobotoMono-Regular.ttf.\n");
    }
}

render_module::CanvasInput ReadCanvasInput(ImVec2 topLeft, ImVec2 size) {
    const ImGuiIO& io = ImGui::GetIO();
    render_module::CanvasInput input;
    input.hovered = ImGui::IsItemHovered();
    input.active = ImGui::IsItemActive();
    input.position = {
        io.MousePos.x - topLeft.x,
        size.y - (io.MousePos.y - topLeft.y)
    };
    input.delta = {io.MouseDelta.x, -io.MouseDelta.y};
    input.wheel = input.hovered ? io.MouseWheel : 0.0f;

    for (int button = 0; button < 3; ++button) {
        render_module::MouseButtonState& state = input.buttons[button];
        state.down = ImGui::IsMouseDown(button);
        state.pressed = input.hovered && ImGui::IsMouseClicked(button);
        state.released = ImGui::IsMouseReleased(button);
        state.dragging = ImGui::IsMouseDragging(button);
    }
    return input;
}

void DrawRootDockSpace() {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

    constexpr ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDocking |
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoNavFocus;

    ImGui::Begin("##RenderModuleDockSpace", nullptr, flags);
    ImGui::PopStyleVar(3);
    const ImGuiID dockspaceId = ImGui::GetID("RenderModuleDockSpace");
    ImGui::DockSpace(dockspaceId, ImVec2(0.0f, 0.0f),
                     ImGuiDockNodeFlags_PassthruCentralNode);
    ImGui::End();
}


void DrawStatusOverlay(ImDrawList* drawList, ImVec2 topLeft, const std::string& text) {
    if (!drawList || text.empty()) return;
    const ImVec2 textSize = ImGui::CalcTextSize(text.c_str());
    const ImVec2 panelMin{topLeft.x + 6.0f, topLeft.y + 10.0f};
    const ImVec2 panelMax{panelMin.x + textSize.x + 14.0f,
                          panelMin.y + textSize.y + 10.0f};
    drawList->AddRectFilled(panelMin, panelMax, IM_COL32(20, 25, 32, 210), 4.0f);
    drawList->AddText(ImVec2(panelMin.x + 7.0f, panelMin.y + 5.0f),
                      IM_COL32(245, 247, 250, 255), text.c_str());
}

void Render3DWindow(View3DWindow& window) {
    const bool visible = ImGui::Begin(window.name.c_str());
    if (!visible) {
        ImGui::End();
        return;
    }

    const ImVec2 size = ImGui::GetContentRegionAvail();
    if (size.x < 1.0f || size.y < 1.0f || !window.storage) {
        ImGui::End();
        return;
    }

    const ImVec2 topLeft = ImGui::GetCursorScreenPos();
    const std::string itemId = "##view3d_" + window.name;
    ImGui::InvisibleButton(
        itemId.c_str(), size,
        ImGuiButtonFlags_MouseButtonLeft |
        ImGuiButtonFlags_MouseButtonRight |
        ImGuiButtonFlags_MouseButtonMiddle);
    const render_module::CanvasInput input = ReadCanvasInput(topLeft, size);

    const ImVec2 framebufferScale = ImGui::GetIO().DisplayFramebufferScale;
    const float pixelRatio = std::max(1.0f, (framebufferScale.x + framebufferScale.y)*0.5f);
    const int pixelWidth = std::max(1, static_cast<int>(std::ceil(size.x*pixelRatio)));
    const int pixelHeight = std::max(1, static_cast<int>(std::ceil(size.y*pixelRatio)));
    if ((pixelWidth != window.pixelWidth || pixelHeight != window.pixelHeight) &&
        !CreateFBO(window, pixelWidth, pixelHeight)) {
        ImGui::End();
        return;
    }

    GLint previousFBO = 0;
    GLint previousViewport[4] = {};
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFBO);
    glGetIntegerv(GL_VIEWPORT, previousViewport);

    glBindFramebuffer(GL_FRAMEBUFFER, window.fbo);
    glViewport(0, 0, pixelWidth, pixelHeight);
    // glClear() honors write masks. External renderers may have changed them,
    // so make the 3D framebuffer clear deterministic before handing state to Magnum.
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_TRUE);
    glStencilMask(0xffu);
    glClearColor(window.options.background.r,
                 window.options.background.g,
                 window.options.background.b,
                 window.options.background.a);
    glClearStencil(0);
    glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    std::string status;
    try {
        status = render_module::detail::RenderView3D(
            *window.storage, {size.x, size.y}, input, window.options, window.callback);
    } catch (...) {
        glBindFramebuffer(GL_FRAMEBUFFER, previousFBO);
        glViewport(previousViewport[0], previousViewport[1],
                   previousViewport[2], previousViewport[3]);
        ImGui::End();
        throw;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, previousFBO);
    glViewport(previousViewport[0], previousViewport[1],
               previousViewport[2], previousViewport[3]);

    // OpenGL renders FBO textures bottom-up relative to ImGui screen space.
    // Flip V so the 3D view appears upright while input remains +Y-up.
    const ImVec2 bottomRight{topLeft.x + size.x, topLeft.y + size.y};
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddImage(
        static_cast<ImTextureID>(window.texture), topLeft, bottomRight,
        ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
    DrawStatusOverlay(drawList, topLeft, status);
    ImGui::End();
}

void RenderPaintWindow(PaintWindow& window) {
    if (window.offscreenCallback) {
        window.offscreenCallback(ctx.vg);
    }

    const bool visible = ImGui::Begin(window.name.c_str());
    if (!visible) {
        ImGui::End();
        return;
    }

    const ImVec2 size = ImGui::GetContentRegionAvail();
    if (size.x < 1.0f || size.y < 1.0f) {
        ImGui::End();
        return;
    }

    const ImVec2 topLeft = ImGui::GetCursorScreenPos();
    const std::string itemId = "##canvas_" + window.name;
    ImGui::InvisibleButton(
        itemId.c_str(), size,
        ImGuiButtonFlags_MouseButtonLeft |
        ImGuiButtonFlags_MouseButtonRight |
        ImGuiButtonFlags_MouseButtonMiddle);

    window.canvas.graphics = ctx.vg;
    window.canvas.size = {size.x, size.y};
    window.canvas.input = ReadCanvasInput(topLeft, size);
    window.canvas.overlays.clear();

    const ImVec2 framebufferScale = ImGui::GetIO().DisplayFramebufferScale;
    const float pixelRatio = std::max(1.0f, (framebufferScale.x + framebufferScale.y) * 0.5f);
    const int pixelWidth = std::max(1, static_cast<int>(std::ceil(size.x * pixelRatio)));
    const int pixelHeight = std::max(1, static_cast<int>(std::ceil(size.y * pixelRatio)));
    if ((pixelWidth != window.pixelWidth || pixelHeight != window.pixelHeight) &&
        !CreateFBO(window, pixelWidth, pixelHeight)) {
        ImGui::End();
        return;
    }

    GLint previousFBO = 0;
    GLint previousViewport[4] = {};
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFBO);
    glGetIntegerv(GL_VIEWPORT, previousViewport);

    glBindFramebuffer(GL_FRAMEBUFFER, window.fbo);
    glViewport(0, 0, pixelWidth, pixelHeight);
    glClearColor(0.16f, 0.18f, 0.21f, 1.0f);
    // glClearColor(0.5f, 0.5f, 0.5f, 1.0f);
    glClearStencil(0);
    glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    nvgBeginFrame(ctx.vg, size.x, size.y, pixelRatio);
    nvg::SetContext(ctx.vg);
    render_module::Canvas canvas =
        render_module::detail::CanvasAccess::Make(window.canvas);
    render_module::Canvas* previousCanvas = render_module::detail::CurrentCanvas();
    render_module::detail::SetCurrentCanvas(&canvas);
    try {
        window.callback(canvas);
    } catch (...) {
        render_module::detail::SetCurrentCanvas(previousCanvas);
        nvgEndFrame(ctx.vg);
        glBindFramebuffer(GL_FRAMEBUFFER, previousFBO);
        glViewport(previousViewport[0], previousViewport[1],
                   previousViewport[2], previousViewport[3]);
        ImGui::End();
        throw;
    }
    render_module::detail::SetCurrentCanvas(previousCanvas);
    nvgEndFrame(ctx.vg);

    glBindFramebuffer(GL_FRAMEBUFFER, previousFBO);
    glViewport(previousViewport[0], previousViewport[1],
               previousViewport[2], previousViewport[3]);

    // Deliberately leave the texture UVs unflipped. This makes NanoVG's FBO
    // coordinates appear as engineering-style +Y-up canvas coordinates.
    const ImVec2 bottomRight{topLeft.x + size.x, topLeft.y + size.y};
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddImage(
        static_cast<ImTextureID>(window.texture), topLeft, bottomRight,
        ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f));
    for (const render_module::detail::CanvasOverlay& overlay : window.canvas.overlays) {
        const ImVec2 textSize = ImGui::CalcTextSize(overlay.text.c_str());
        const ImVec2 panelMin{topLeft.x + overlay.position.x,
                              topLeft.y + overlay.position.y};
        const ImVec2 panelMax{panelMin.x + textSize.x + 14.0f,
                              panelMin.y + textSize.y + 10.0f};
        drawList->AddRectFilled(panelMin, panelMax, IM_COL32(20, 25, 32, 210), 4.0f);
        drawList->AddText(ImVec2(panelMin.x + 7.0f, panelMin.y + 5.0f),
                          IM_COL32(245, 247, 250, 255), overlay.text.c_str());
    }
    ImGui::End();
}

} // namespace

bool RenderModule::Init(int width, int height, double fps, const char* title) {
    if (ctx.initialized) return true;
    if (width <= 0 || height <= 0) {
        std::fprintf(stderr, "RenderModule: window dimensions must be positive.\n");
        return false;
    }

    ctx.fpsSetpoint = std::max(0.0, fps);
    glfwSetErrorCallback([](int code, const char* message) {
        std::fprintf(stderr, "GLFW error %d: %s\n", code, message);
    });
    if (!glfwInit()) return false;
    ctx.glfwInitialized = true;

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    ctx.window = glfwCreateWindow(width, height, title, nullptr, nullptr);
    if (!ctx.window) {
        Shutdown();
        return false;
    }
    glfwMakeContextCurrent(ctx.window);
    glfwSwapInterval(0);

    if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress))) {
        std::fprintf(stderr, "RenderModule: failed to load OpenGL functions.\n");
        Shutdown();
        return false;
    }

    if (!render_module::detail::Initialize3DBackend()) {
        Shutdown();
        return false;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    ImGui::StyleColorsLight();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 5.0f;
    style.FrameRounding = 5.0f;
    style.GrabRounding = 5.0f;
    style.ScrollbarRounding = 5.0f;

    if (!ImGui_ImplGlfw_InitForOpenGL(ctx.window, true)) {
        std::fprintf(stderr, "RenderModule: ImGui GLFW backend initialization failed.\n");
        Shutdown();
        return false;
    }
    ctx.imguiGlfwInitialized = true;
    if (!ImGui_ImplOpenGL3_Init("#version 330")) {
        std::fprintf(stderr, "RenderModule: ImGui OpenGL backend initialization failed.\n");
        Shutdown();
        return false;
    }
    ctx.imguiOpenGLInitialized = true;

    ctx.vg = nvgCreateGL3(NVG_ANTIALIAS | NVG_STENCIL_STROKES);
    if (!ctx.vg) {
        std::fprintf(stderr, "RenderModule: NanoVG initialization failed.\n");
        Shutdown();
        return false;
    }
    nvg::SetContext(ctx.vg);
    LoadFonts(ctx.vg);
    ctx.initialized = true;
    return true;
}

void RenderModule::EnableRootWindowDocking() {
    settings.rootWindowDockingEnabled = true;
}

void RenderModule::RegisterImGuiCallback(std::function<void()> callback) {
    if (callback) ctx.imguiCallbacks.push_back(std::move(callback));
}

void RenderModule::RegisterCanvas(
    const std::string& name,
    CanvasCallback callback,
    std::function<void(NVGcontext*)> offscreenCallback) {
    if (name.empty() || !callback) return;
    PaintWindow window;
    window.name = name;
    window.callback = std::move(callback);
    window.offscreenCallback = std::move(offscreenCallback);
    ctx.paintWindows.push_back(std::move(window));
}

void RenderModule::Register3DView(
    const std::string& name,
    View3DCallback callback,
    const render_module::View3DOptions& options) {
    if (name.empty() || !callback) return;
    View3DWindow window;
    window.name = name;
    window.callback = std::move(callback);
    window.options = options;
    window.storage = render_module::detail::CreateView3DStorage();
    ctx.view3DWindows.push_back(std::move(window));
}

void RenderModule::RegisterNanoVGCallback(
    const std::string& name,
    std::function<void(NVGcontext*)> callback,
    std::function<void(NVGcontext*)> offscreenCallback) {
    if (!callback) return;
    RegisterCanvas(
        name,
        [callback = std::move(callback)](render_module::Canvas& canvas) {
            callback(canvas.Graphics());
        },
        std::move(offscreenCallback));
}

void RenderModule::ZoomView(std::function<void(NVGcontext*)> callback) {
    if (!callback) return;
    ZoomView::Draw("default", ctx.vg, std::move(callback));
}

void RenderModule::Run() {
    if (!ctx.initialized || !ctx.window) {
        std::fprintf(stderr, "RenderModule::Run called before successful Init.\n");
        return;
    }

    while (!glfwWindowShouldClose(ctx.window)) {
        const double frameStart = glfwGetTime();
        glfwPollEvents();

        int displayWidth = 0;
        int displayHeight = 0;
        glfwGetFramebufferSize(ctx.window, &displayWidth, &displayHeight);
        glViewport(0, 0, displayWidth, displayHeight);
        glClearColor(0.35f, 0.36f, 0.39f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        if (settings.rootWindowDockingEnabled) DrawRootDockSpace();
        if (settings.renderDebugConsole) Console().Render("Debug Console");

        for (const auto& callback : ctx.imguiCallbacks) callback();
        for (PaintWindow& window : ctx.paintWindows) RenderPaintWindow(window);
        for (View3DWindow& window : ctx.view3DWindows) Render3DWindow(window);

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(ctx.window);

        const double workTime = glfwGetTime() - frameStart;
        if (ctx.fpsSetpoint > 0.0) {
            const double remaining = 1.0 / ctx.fpsSetpoint - workTime;
            if (remaining > 0.0) glfwWaitEventsTimeout(remaining);
        }

        ctx.deltaTime = std::max(glfwGetTime() - frameStart, 1.0e-9);
        ctx.fpsCurrent = 1.0 / ctx.deltaTime;
    }
}

void RenderModule::RequestClose() {
    if (ctx.window) glfwSetWindowShouldClose(ctx.window, GLFW_TRUE);
}

void RenderModule::IsolatedFrameBuffer(
    std::function<void(NVGcontext*)> userFramebufferRender) {
    if (!ctx.vg || !userFramebufferRender) return;

    GLint previousFBO = 0;
    GLint previousViewport[4] = {};
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFBO);
    glGetIntegerv(GL_VIEWPORT, previousViewport);

    userFramebufferRender(ctx.vg);
    nvgluBindFramebuffer(nullptr);
    glBindFramebuffer(GL_FRAMEBUFFER, previousFBO);
    glViewport(previousViewport[0], previousViewport[1],
               previousViewport[2], previousViewport[3]);
}

void RenderModule::Shutdown() {
    Console().SetCoutRedirect(false);
    for (PaintWindow& window : ctx.paintWindows) DestroyFBO(window);
    ctx.paintWindows.clear();
    for (View3DWindow& window : ctx.view3DWindows) DestroyFBO(window);
    ctx.view3DWindows.clear(); // Releases Magnum-owned dynamic GPU meshes.
    ctx.imguiCallbacks.clear();

    render_module::detail::Shutdown3DBackend();

    if (ctx.vg) {
        nvgDeleteGL3(ctx.vg);
        ctx.vg = nullptr;
        nvg::SetContext(nullptr);
    }
    if (ImPlot::GetCurrentContext()) ImPlot::DestroyContext();
    if (ctx.imguiOpenGLInitialized) {
        ImGui_ImplOpenGL3_Shutdown();
        ctx.imguiOpenGLInitialized = false;
    }
    if (ctx.imguiGlfwInitialized) {
        ImGui_ImplGlfw_Shutdown();
        ctx.imguiGlfwInitialized = false;
    }
    if (ImGui::GetCurrentContext()) ImGui::DestroyContext();
    if (ctx.window) {
        glfwDestroyWindow(ctx.window);
        ctx.window = nullptr;
    }
    if (ctx.glfwInitialized) {
        glfwTerminate();
        ctx.glfwInitialized = false;
    }

    ctx.initialized = false;
    ctx.fpsCurrent = 0.0;
    ctx.deltaTime = 0.0;
}

render_module::Vec2 RenderModule::GetWindowSize() {
    if (!ctx.window) return {};
    int width = 0;
    int height = 0;
    glfwGetWindowSize(ctx.window, &width, &height);
    return {static_cast<float>(width), static_cast<float>(height)};
}

ImVec2 RenderModule::GetGLFWWindowSize() {
    const render_module::Vec2 size = GetWindowSize();
    return ImVec2(size.x, size.y);
}

NVGcontext* RenderModule::GetNanoVGContext() {
    return ctx.vg;
}

double RenderModule::GetFPS() {
    return ctx.fpsCurrent;
}

double RenderModule::GetDeltaTime() {
    return ctx.deltaTime;
}

bool RenderModule::IsInitialized() {
    return ctx.initialized;
}

DebugConsole& RenderModule::Console() {
    return console_;
}

void RenderModule::EnableDebugConsole() {
    settings.renderDebugConsole = true;
}
