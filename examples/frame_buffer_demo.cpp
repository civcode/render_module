#include "render_module/render_module.hpp"

#include <iostream>
#include <vector>
#include <string>
#include <streambuf>

// #include <glad/glad.h>
// #include "render_module/glad_wrapper.hpp"
// #include <GLFW/glfw3.h>
// #include <imgui.h>
// #include <backends/imgui_impl_glfw.h>
// #include <backends/imgui_impl_opengl3.h>
// #include <implot.h>
// #define NANOVG_GL3_IMPLEMENTATION
// #include "nanovg.h"
// #include "nanovg_gl.h"
// #include "nanovg_gl.h"
// #include "nanovg_gl_utils.h"

int main() {
    RenderModule::Init(980, 720, 10.0);

    NVGcontext* vg = RenderModule::GetNanoVGContext();
    
    /* Create a FB and draw to it */
    int width = 200;
    int height = 200;
    
    // NVGLUframebuffer* fb = nvgluCreateFramebuffer(vg, width, height, NVG_IMAGE_REPEATX | NVG_IMAGE_REPEATY | NVG_IMAGE_FLIPY);
    nvg::SetContext(vg);
    NVGLUframebuffer* fb = nvg::GLUtilsCreateFramebuffer(width, height, NVG_IMAGE_REPEATX | NVG_IMAGE_REPEATY | NVG_IMAGE_FLIPY);
    if (!fb) {
        std::cerr << "Failed to create framebuffer." << std::endl;
        return -1;
    }
    // nvgImageSize(vg, fb->image, &width, &height);
    // nvgluBindFramebuffer(fb);
    nvg::GLUtilsBindFramebuffer(fb);
    glad::glViewport(0, 0, width, height);
    glad::glClearColor(0, 0, 0, 0);
    glad::glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    // glViewport(0, 0, width, height);
    // glClearColor(0, 0, 0, 0);
    // glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

    nvgBeginFrame(vg, width, height, 1.0f);
    nvgBeginPath(vg);
    nvgCircle(vg, width/2+50, height/2+50, 50);
    nvgFillColor(vg, nvgRGBA(0, 0, 255, 255));
    nvgFill(vg);
    nvgEndFrame(vg);
    
    nvgluBindFramebuffer(NULL);
        
    // NVGpaint img_paint = nvgImagePattern(vg, 0, 0, 200, 200, 0, fb->image, 1.0f);
    NVGpaint img_paint = nvg::ImagePattern(0, 0, 200, 200, 0, fb->image, 1.0f);
    if (img_paint.image == 0) {
        std::cerr << "Failed to create image pattern." << std::endl;
        return -1;
    }

    RenderModule::RegisterNanoVGCallback("Red Circle", [&](NVGcontext* vg) {
        nvgBeginPath(vg);
        nvgRect(vg, 0, 0, 200, 200);
        nvgFillPaint(vg, img_paint);
        nvgFill(vg);
        nvgClosePath(vg);


        // nvgluBindFramebuffer(fb);
        // nvgBeginFrame(vg, fb->image, 200, 200, 1.0f);
        nvgBeginPath(vg);
        nvgCircle(vg, 10, 10, 5);
        nvgFillColor(vg, nvgRGBA(255, 0, 150, 155));
        nvgFill(vg);
        // nvgEndFrame(vg);
        // nvgluBindFramebuffer(NULL);
    },
        [&](NVGcontext* vg) {
            RenderModule::IsolatedFrameBuffer([&]() {
                nvg::GLUtilsBindFramebuffer(fb);
                glad::glViewport(0, 0, width, height);
                // glad::glClearColor(0, 0, 0, 0);
                // glad::glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
                nvgBeginFrame(vg, width, height, 1.0f);
                nvgBeginPath(vg);
                static float cx = 10;
                static float cy = 10;
                static float dx = 0.5f;
                nvgCircle(vg, cx, cy, 5);
                cx += dx;
                cy += dx;
                nvgFillColor(vg, nvgRGBA(255, 150, 0, 155));
                nvgFill(vg);
                nvgClosePath(vg);
                nvgEndFrame(vg);
                nvgluBindFramebuffer(NULL);
            });
        });
        


    RenderModule::RegisterImGuiCallback([&]() {

        // ImGui::Begin("Main Window", nullptr, ImGuiWindowFlags_MenuBar);
        ImGui::Begin("Main Text Window", nullptr);
        ImGui::TextWrapped(
            "CJK text will only appear if the font was loaded with the appropriate CJK character ranges. "
            "Call io.Fonts->AddFontFromFileTTF() manually to load extra character ranges. "
            "Read docs/FONTS.md for details.");
        ImGui::Text("Hiragana: \xe3\x81\x8b\xe3\x81\x8d\xe3\x81\x8f\xe3\x81\x91\xe3\x81\x93 (kakikukeko)");
        ImGui::Text("Kanjis: \xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e (nihongo)");
        static char buf[32] = "\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e";
        ImGui::InputText("UTF-8 input", buf, IM_ARRAYSIZE(buf));
        ImGui::End();


        // RenderModule::IsolatedFrameBuffer([&]() {
        //     nvg::GLUtilsBindFramebuffer(fb);
        //     glad::glViewport(0, 0, width, height);
        //     // glad::glClearColor(0, 0, 0, 0);
        //     // glad::glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
        //     nvgBeginFrame(vg, width, height, 1.0f);
        //     nvgBeginPath(vg);
        //     static float cx = 10;
        //     static float cy = 10;
        //     static float dx = 0.5f;
        //     nvgCircle(vg, cx, cy, 5);
        //     cx += dx;
        //     cy += dx;
        //     nvgFillColor(vg, nvgRGBA(255, 150, 0, 155));
        //     nvgFill(vg);
        //     nvgClosePath(vg);
        //     nvgEndFrame(vg);
        //     nvgluBindFramebuffer(NULL);
        
        // });

    });

    
    RenderModule::Run();
    RenderModule::Shutdown();
    return 0;
}