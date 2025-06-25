#include "render_module/render_module.hpp"

#include <iostream>

int main() {
    RenderModule::Init(980, 720, 10.0);
    RenderModule::EnableRootWindowDocking();

    NVGcontext* vg = RenderModule::GetNanoVGContext();
    
    /* Create a FB and draw to it */
    int width = 200;
    int height = 200;
    
    /* Create Frame Buffer */
    nvg::SetContext(vg);
    NVGLUframebuffer* fb = nvg::GLUtilsCreateFramebuffer(width, height, NVG_IMAGE_REPEATX | NVG_IMAGE_REPEATY | NVG_IMAGE_FLIPY);
    if (!fb) {
        std::cerr << "Failed to create framebuffer." << std::endl;
        return -1;
    }
    /* Bind FB and draw to it */
    nvg::GLUtilsBindFramebuffer(fb);
    glad::glViewport(0, 0, width, height);
    // glad::glClearColor(0, 0, 0, 0);
    // glad::glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

    nvg::BeginFrame(width, height, 1.0f);
    nvg::BeginPath();
    nvg::Circle(width/2+50, height/2+50, 50);
    nvg::FillColor(nvgRGBA(0, 0, 255, 255));
    nvg::Fill();
    nvg::EndFrame();
    
    nvg::GLUtilsBindFramebuffer(nullptr);
       
    NVGpaint img_paint = nvg::ImagePattern(0, 0, 200, 200, 0, fb->image, 1.0f);
    if (img_paint.image == 0) {
        std::cerr << "Failed to create image pattern." << std::endl;
        return -1;
    }

    RenderModule::RegisterNanoVGCallback("Red Circle", 
        /* Render callback */ 
        [&](NVGcontext* vg) {
            RenderModule::ZoomView([&](NVGcontext* vg) {
                /* Render the image created from the FB */
                nvg::BeginPath();
                nvg::Rect(0, 0, 200, 200);
                nvg::FillPaint(img_paint);
                nvg::Fill();

                /* Draw on the same gl context */
                nvg::BeginPath();
                nvg::Circle(10, 10, 5);
                nvg::FillColor(nvgRGBA(255, 0, 150, 155));
                nvg::Fill();
            });
        },
        /* Offscreen callback */
        [&](NVGcontext* vg) {
            RenderModule::IsolatedFrameBuffer([&](NVGcontext* vg) {
                // nvg::SetContext(vg);
                nvg::GLUtilsBindFramebuffer(fb);
                glad::glViewport(0, 0, width, height);
                // glad::glClearColor(0, 0, 0, 0);
                // glad::glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
                nvg::BeginFrame(width, height, 1.0f);
                nvg::BeginPath();
                static float cx = 10;
                static float cy = 10;
                static float dx = 0.5f;
                nvg::Circle(cx, cy, 5);
                cx += dx;
                cy += dx;
                nvg::FillColor(nvgRGBA(255, 150, 0, 155));
                nvg::Fill();
                nvg::EndFrame();
                nvg::GLUtilsBindFramebuffer(NULL);
            });    
        }
    );
        

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
    });
    
    RenderModule::Run();
    RenderModule::Shutdown();
    return 0;
}