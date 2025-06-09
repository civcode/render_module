#include "render_module/render_module.hpp"

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>
#include <implot.h>
// #define NANOVG_GL3_IMPLEMENTATION
#include "nanovg.h"
#include "nanovg_gl.h"

struct PaintWindow {
    std::string name;
    std::function<void(NVGcontext*)> callback;
    GLuint fbo = 0;
    GLuint texture = 0;
    GLuint rbo = 0;
    int width = 0, height = 0;
};

static struct {
    GLFWwindow* window;
    NVGcontext* vg;
    std::function<void()> imguiCallback;
    std::vector<PaintWindow> paintWindows;
} ctx;

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

    std::string base_path = std::string(home) + "/.local/share/render-module/";

    std::vector<std::pair<std::string, std::string>> fonts = {
        {"sans", "fonts/roboto/Roboto-Regular.ttf"},
        {"sans-bold", "fonts/roboto/Roboto-Bold.ttf"},
        {"sans-italic", "fonts/roboto/Roboto-Italic.ttf"},
        {"sans-bold-italic", "fonts/roboto/Roboto-BoldItalic.ttf"},
        {"mono", "fonts/roboto/RobotoMono-Regular.ttf"},
        {"mono-bold", "fonts/roboto/RobotoMono-Bold.ttf"},
        {"mono-italic", "fonts/roboto/RobotoMono-Italic.ttf"},
        {"mono-bold-italic", "fonts/roboto/RobotoMono-BoldItalic.ttf"}
    };

    for (const auto& font : fonts) {
        std::string alias = font.first;
        std::string full_path = base_path + font.second;

        int font_id = nvgCreateFont(vg, alias.c_str(), full_path.c_str());
        if (font_id == -1) {
            fprintf(stderr, "Could not load font '%s' from '%s'.\n", alias.c_str(), full_path.c_str());
            continue;
        } else {
            printf("Loaded font '%s' from '%s'.\n", alias.c_str(), full_path.c_str());
        }
    }
}

void RenderModule::Init(int width, int height, const char* title) {
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    ctx.window = glfwCreateWindow(width, height, title, nullptr, nullptr);
    glfwMakeContextCurrent(ctx.window);
    gladLoadGLLoader((GLADloadproc)glfwGetProcAddress);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 8.0f;
    style.FrameRounding = 8.0f;
    style.GrabRounding = 6.0f;
    style.ScrollbarRounding = 8.0f;
    ImVec4 bg = ImVec4(0.95f, 0.95f, 0.98f, 1.0f);
    style.Colors[ImGuiCol_WindowBg] = bg;

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable; 

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

void RenderModule::RegisterImGuiCallback(std::function<void()> callback) {
    ctx.imguiCallback = callback;
}

void RenderModule::RegisterNanoVGCallback(const std::string& name, std::function<void(NVGcontext*)> callback) {
    ctx.paintWindows.push_back(PaintWindow{name, callback});
}

void RenderModule::Run() {
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

        if (ctx.imguiCallback)
            ctx.imguiCallback();

        for (auto& win : ctx.paintWindows) {
            ImGui::Begin(win.name.c_str());
            ImVec2 size = ImGui::GetContentRegionAvail();
            // int w = static_cast<int>(size.x);
            // int h = static_cast<int>(size.y);
            int w = std::max(16, static_cast<int>(size.x));
            int h = std::max(16, static_cast<int>(size.y));

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
        glfwSwapBuffers(ctx.window);
    }
}

void RenderModule::Shutdown() {
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
