#include "render_module/render_module.hpp"

#include <iostream>
#include <stack>
#include <thread>

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>
#include <implot.h>
// #define NANOVG_GL3_IMPLEMENTATION
#include "nanovg_gl.h"
#include "nanovg_gl_utils.h"

#include "render_module/zoom_view.hpp"

DebugConsole RenderModule::console_;

namespace {
    thread_local std::stack<std::string> label_stack;
}

struct PaintWindow {
    std::string name;
    std::function<void(NVGcontext*)> callback;
    std::function<void(NVGcontext*)> offscreenCallback;
    GLuint fbo = 0;
    GLuint texture = 0;
    GLuint rbo = 0;
    int width = 0, height = 0;
};

static struct {
    GLFWwindow* window;
    NVGcontext* vg;
    std::vector<std::function<void()>> imguiCallbacks;
    std::vector<PaintWindow> paintWindows;
    double fps_setpoint;
    double fps_current = 0.0;

} ctx;

static struct {
    bool rootWindowDockingEnabled = false;
    bool renderDebugConsole = false;
} settings;

static struct LowPassFilter {
    double alpha = 0.005; // Smoothing factor
    double value = 0.0;

    void Update(double new_value) {
        value = (1.0 - alpha) * value + alpha * new_value;
    }

    double GetValue() const {
        return value;
    }
} fps_filter;

static void CreateFBO(PaintWindow& win, int width, int height) {
    if (win.texture) {
        glDeleteTextures(1, &win.texture);
        glDeleteFramebuffers(1, &win.fbo);
    }

    glGenTextures(1, &win.texture);
    glBindTexture(GL_TEXTURE_2D, win.texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glGenFramebuffers(1, &win.fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, win.fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, win.texture, 0);

    win.width = width;
    win.height = height;
}

static void LoadFonts(NVGcontext* vg) {
    const char* home = std::getenv("HOME");
    if (!home) {
        std::fprintf(stderr, "Error: HOME environment variable is not set.\n");
    }

    std::string base_path = std::string(home) + "/.local/share/render-module/fonts/roboto/";

    std::vector<std::pair<std::string, std::string>> fonts = {
        {"sans", "Roboto-Regular.ttf"},
        {"sans-bold", "Roboto-Bold.ttf"},
        {"sans-italic", "Roboto-Italic.ttf"},
        {"sans-bold-italic", "Roboto-BoldItalic.ttf"},
        {"mono", "RobotoMono-Regular.ttf"},
        {"mono-bold", "RobotoMono-Bold.ttf"},
        {"mono-italic", "RobotoMono-Italic.ttf"},
        {"mono-bold-italic", "RobotoMono-BoldItalic.ttf"}
    };

    printf("Loading fonts from '%s'\n", base_path.c_str());
    for (const auto& font : fonts) {
        std::string alias = font.first;
        std::string full_path = base_path + font.second;

        int font_id = nvgCreateFont(vg, alias.c_str(), full_path.c_str());
        if (font_id == -1) {
            fprintf(stderr, "Could not load font '%s' from '%s'.\n", alias.c_str(), full_path.c_str());
            continue;
        } else {
            // printf("Loaded font '%s' from '%s'.\n", alias.c_str(), font.second.c_str());
        }
    }
}

void RenderModule::Init(int width, int height, double fps, const char* title) {
    ctx.fps_setpoint = fps;

    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    ctx.window = glfwCreateWindow(width, height, title, nullptr, nullptr);
    glfwMakeContextCurrent(ctx.window);
    gladLoadGLLoader((GLADloadproc)glfwGetProcAddress);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiStyle& style = ImGui::GetStyle();
    // style.WindowRounding = 8.0f;
    // style.FrameRounding = 8.0f;
    // style.GrabRounding = 6.0f;
    // style.ScrollbarRounding = 8.0f;
    style.WindowRounding = 5.0f;
    style.FrameRounding = 5.0f;
    style.GrabRounding = 5.0f;
    style.ScrollbarRounding = 5.0f;
    ImVec4 bg = ImVec4(0.95f, 0.95f, 0.98f, 1.0f);
    style.Colors[ImGuiCol_WindowBg] = bg;

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable; 
    // io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;       
    // io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;         
    // io.Fonts->AddFontFromFileTTF("/home/chris/.local/share/render-module/fonts/roboto/Roboto-Regulat.ttf", 18.0f);

    ImPlot::CreateContext();
    ImGui::StyleColorsLight();
    ImGui_ImplGlfw_InitForOpenGL(ctx.window, true);
    ImGui_ImplOpenGL3_Init("#version 330");


    

    ctx.vg = nvgCreateGL3(NVG_ANTIALIAS | NVG_STENCIL_STROKES);
    if (!ctx.vg) {
        fprintf(stderr, "Could not init NanoVG.\n");
        return;
    }
    LoadFonts(ctx.vg);
}

void RenderModule::EnableRootWindowDocking() {
    settings.rootWindowDockingEnabled = true;
}

void RenderModule::RegisterImGuiCallback(std::function<void()> callback) {
    // ctx.imguiCallback = callback;
    ctx.imguiCallbacks.push_back(callback);
}

void RenderModule::RegisterNanoVGCallback(const std::string& name, std::function<void(NVGcontext*)> callback,
                                          std::function<void(NVGcontext*)> offscreenCallback) {
    ctx.paintWindows.push_back(PaintWindow{
        name, 
        [name, callback](NVGcontext* vg) {
            label_stack.push(name);
            callback(vg);
            label_stack.pop();
        },
        offscreenCallback
    });

}

void RenderModule::ZoomView(std::function<void(NVGcontext *)> callback)
{
    if (label_stack.empty()) {
        std::cerr << "Error: ZoomView called without an active label." << std::endl;
        return;
    }

    const std::string& label = label_stack.top() + "_zoom_view";
    ZoomView::Draw(label, ctx.vg, callback);
}

void RenderModule::Run() {
    double prev_frame_time = glfwGetTime();
    while (!glfwWindowShouldClose(ctx.window)) {

        glfwPollEvents();
        int display_w, display_h;
        glfwGetFramebufferSize(ctx.window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.5f, 0.5f, 0.5f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        if (settings.rootWindowDockingEnabled) {
            /* Enable docking w.r.t. the parent GLFW window */
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
            // ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
            ImGui::Begin("Main DockSpace Window", nullptr, 
                // ImGuiWindowFlags_MenuBar | 
                ImGuiWindowFlags_NoDocking |
                ImGuiWindowFlags_NoTitleBar |
                ImGuiWindowFlags_NoCollapse |
                ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_NoMove |
                ImGuiWindowFlags_NoBringToFrontOnFocus |
                ImGuiWindowFlags_NoNavFocus);
            // ImGui::PopStyleColor();
            ImGui::SetWindowPos(ImVec2(0, 0));
            ImGui::SetWindowSize(ImGui::GetIO().DisplaySize);

            ImGuiID dockspace_id = ImGui::GetID("MyDockSpace");
            ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_PassthruCentralNode);
            ImGui::PopStyleVar(2);
            ImGui::End();
            /* === */
        }


        if (0) {
            ImGui::Begin("Dockable Window", nullptr, ImGuiWindowFlags_NoCollapse);
            // Get GLFW window size
            int winWidth, winHeight;
            glfwGetWindowSize(ctx.window, &winWidth, &winHeight);

            // Get ImGui window position and size
            ImVec2 pos = ImGui::GetWindowPos();
            ImVec2 size = ImGui::GetWindowSize();

            // Threshold for edge snapping (in pixels)
            float snapThreshold = 20.0f;

            // Check if window is hovered and being released after drag
            if (ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows) && 
                ImGui::IsMouseReleased(0)) {

                bool snapLeft   = pos.x <= snapThreshold;
                bool snapRight  = (pos.x + size.x) >= (winWidth - snapThreshold);
                bool snapTop    = pos.y <= snapThreshold;
                bool snapBottom = (pos.y + size.y) >= (winHeight - snapThreshold);

                if ((snapLeft && snapRight) || (snapTop && snapBottom) || 
                    (snapLeft && snapTop) || (snapRight && snapTop)) {
                    // Snap to full screen or top-half, etc. Based on your logic
                    ImGui::SetWindowPos(ImVec2(0, 0));
                    ImGui::SetWindowSize(ImVec2((float)winWidth, (float)winHeight));
                }
            }
            ImGui::Text("Drag this window near an edge to snap.");
            ImGui::End();
        }



        if (settings.renderDebugConsole) {
            RenderModule::Console().Render("Debug Console");
        }

        for (auto& cb : ctx.imguiCallbacks)
            cb();

        for (auto& win : ctx.paintWindows) {
            if (win.offscreenCallback) {
                // RenderModule::IsolatedFrameBuffer(win.offscreenCallback());
                win.offscreenCallback(ctx.vg);
            }

            ImGui::Begin(win.name.c_str());
            ImVec2 size = ImGui::GetContentRegionAvail();
            // int w = static_cast<int>(size.x);
            // int h = static_cast<int>(size.y);
            int w = std::max(64, static_cast<int>(size.x));
            int h = std::max(64, static_cast<int>(size.y));

            if (w != win.width || h != win.height) {
                CreateFBO(win, w, h);
            }

            glBindFramebuffer(GL_FRAMEBUFFER, win.fbo);
            glViewport(0, 0, w, h);
            // glClearColor(0, 0, 0, 0);
            glClearColor(0.5f, 0.5f, 0.5f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);

            nvgBeginFrame(ctx.vg, w, h, 1.0f);
            win.callback(ctx.vg);
            nvgEndFrame(ctx.vg);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);

            ImTextureID tex_id = (ImTextureID)(uintptr_t)win.texture;
            ImGui::Image(tex_id, ImVec2((float)w, (float)h));

            ImGui::End();
        }

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        // if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
            // GLFWwindow* backup_current_context = glfwGetCurrentContext();
            // ImGui::UpdatePlatformWindows();
            // ImGui::RenderPlatformWindowsDefault();
            // glfwMakeContextCurrent(backup_current_context);
        // }

        glfwSwapBuffers(ctx.window);

        double delta_time = glfwGetTime() - prev_frame_time;
        ctx.fps_current = 1.0 / delta_time;
        fps_filter.Update(ctx.fps_current);
        prev_frame_time += delta_time;
        if (ctx.fps_setpoint != 0 && delta_time < 1.0 / ctx.fps_setpoint) {
            double sleep_time = 1.0 / ctx.fps_setpoint - delta_time;
            /* glfwWaitEventsTimeout makes the app more respoinsive */
            /* the render loop only sleeps if there are no events, e.g. mouse movement */
            glfwWaitEventsTimeout(sleep_time);
            // std::this_thread::sleep_for(std::chrono::duration<double>(sleep_time));
        }
    }
}

void RenderModule::IsolatedFrameBuffer(std::function<void(NVGcontext*)> userFramebufferRender)
{
    GLint prevFBO;
    GLint viewport[4];
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFBO);
    glGetIntegerv(GL_VIEWPORT, viewport);

    // Execute user's framebuffer binding + NanoVG draw
    userFramebufferRender(ctx.vg);

    // Unbind framebuffer
    nvgluBindFramebuffer(nullptr);

    // Restore previous state
    glBindFramebuffer(GL_FRAMEBUFFER, prevFBO);
    glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
}
void RenderModule::Shutdown()
{
    for (auto& win : ctx.paintWindows) {
        if (win.texture) glDeleteTextures(1, &win.texture);
        if (win.fbo) glDeleteFramebuffers(1, &win.fbo);
    }
    nvgDeleteGL3(ctx.vg);
    ImPlot::DestroyContext();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(ctx.window);
    glfwTerminate();
}

ImVec2ih RenderModule::GetGLFWWindowSize() {
    int width, height;
    glfwGetWindowSize(ctx.window, &width, &height);
    return ImVec2ih(width, height);
}

NVGcontext *RenderModule::GetNanoVGContext()
{
    return ctx.vg;
}

double RenderModule::GetFPS()
{
    return ctx.fps_current;;
    // return fps_filter.GetValue();
}

DebugConsole& RenderModule::Console() {
    return console_;
}

void RenderModule::EnableDebugConsole()
{
    settings.renderDebugConsole = true;
}
