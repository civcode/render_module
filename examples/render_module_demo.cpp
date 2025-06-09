#include "render_module/render_module.hpp"

#include <math.h>
#include <iostream>
// #include <imgui.h>
// #include <implot.h>
// #include "nanovg.h"

// shared state
float g_speed = 1.0f;

struct PaintState {
    float x = 0.0f;
    float speed = 1.0f;

    void update() {
        x += speed;
        if (x > 300.0f) x = 0.0f;
    }

    void draw(NVGcontext* vg) {
        update();

        nvgBeginPath(vg);
        nvgCircle(vg, x, 100, 40);
        nvgFillColor(vg, nvgRGBA(255, 100, 0, 255));
        nvgFill(vg);
    }
};

int main() {
    RenderModule::Init(980, 720);

    // Combined ImGui callback that creates multiple windows
    RenderModule::RegisterImGuiCallback([]() {

         // This is where the DockSpace UI should be placed
        // ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(150, 150, 150, 255)); 
        // ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.5f, 0.5f, 0.5f, 1.0f)); 
        // ImGui::PushStyleColor(ImGuiCol_DockingEmptyBg, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(1.0f, 1.0f, 1.0f, 1.0f)); 
        ImGui::PushStyleColor(ImGuiCol_DockingEmptyBg, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
        ImGui::Begin("Main Window", nullptr, ImGuiWindowFlags_MenuBar);
        ImGuiID dockspace_id = ImGui::GetID("MyDockSpace");
        ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);
        ImGui::End();
        ImGui::PopStyleColor(2);

        static bool initialized = false;
        if (!initialized)
        {
            initialized = true;

            ImGui::DockBuilderRemoveNode(dockspace_id);                  // Clear any previous layout
            ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
            ImGui::DockBuilderSetNodeSize(dockspace_id, ImGui::GetMainViewport()->Size);

            // Split into left and right
            ImGuiID dock_main_id = dockspace_id;
            ImGuiID dock_id_left = ImGui::DockBuilderSplitNode(dock_main_id, ImGuiDir_Left, 0.25f, nullptr, &dock_main_id);
            ImGuiID dock_id_right = dock_main_id;

            // Dock specific windows to nodes
            ImGui::DockBuilderDockWindow("Sine Plot", dock_id_left);
            ImGui::DockBuilderDockWindow("Control Panel", dock_id_right);

            ImGui::DockBuilderFinish(dockspace_id);
        }

        // First window: Sine plot
        ImGui::Begin("Sine Plot");
        if (ImPlot::BeginPlot("Sine Wave")) {
            static float x[100], y[100];
            for (int i = 0; i < 100; ++i) {
                x[i] = i * 0.1f;
                y[i] = sinf(x[i]);
            }
            ImPlot::PlotLine("sin(x)", x, y, 100);
            ImPlot::EndPlot();
        }
        ImGui::End();

        // Second window: Control panel
        ImGui::Begin("Control Panel");
        // ImGui::SliderFloat("Speed", &g_speed, 0.1f, 10.0f);
        ImGui::SliderFloat("Speed", &g_speed, 0.1f, 10.0f);
        if (ImGui::IsItemHovered() && ImGui::GetIO().MouseWheel != 0) {
            g_speed += ImGui::GetIO().MouseWheel * 0.1f; // Adjust speed and scale as needed
            g_speed = std::clamp(g_speed, 0.3f, 10.0f);  // Clamp to min/max
        }
        ImGui::End();
    });

    // Paint window: animated NanoVG circle
    RenderModule::RegisterNanoVGCallback("NanoVG Canvas", [](NVGcontext* vg) {
        static float x = 0.0f;
        x += g_speed;
        if (x > 300.0f) x = 0.0f;

        nvg::SetContext(vg);
        nvg::BeginPath();
        nvg::Circle(x, 100, 40);
        nvg::FillColor(nvgRGBA(255, 100, 0, 255));
        nvg::Fill();
        nvg::ClosePath();

        nvg::BeginPath();
        nvg::Circle(x, 200, 20);
        nvg::FillColor(nvgRGBA(0, 255, 0, 255));
        nvg::Fill();

        nvg::BeginPath();
        nvg::Text(10, 100, "bHello, NanoVG!");
        nvg::Text(10, 50, "aHello, NanoVG!");
        nvg::ClosePath();
        nvg::ClosePath();
    });


    // NanoVG paint window with persistent state
    static PaintState paint;

        // Register the paint window using state
    RenderModule::RegisterNanoVGCallback("NanoVG Canvas 2", [](NVGcontext* vg) {
        paint.draw(vg);
    });
    
    RenderModule::RegisterNanoVGCallback("NanoVG Canvas 3", [](NVGcontext* vg) {
        ZoomView::Draw("Zoomable NanoVG Canvas", vg, [](NVGcontext* vg) {

            nvg::SetContext(vg);
            static int cnt;
            for (int i = 0; i < 30; ++i) {
                float angle = (i+(cnt++)/100.0f) * 2.0f * M_PI / 100.0f;
                float x = 100 + cosf(angle) * 100;
                float y = 100 + sinf(angle) * 100;

                nvg::BeginPath();
                // nvg::SetShapeAntiAlias(true);
                nvg::Circle(x, y, 3);
                nvg::StrokeColor(nvg::RGBA(255, 0, 0, 255));
                nvg::StrokeWidth(0.5);
                nvg::Stroke();
                // nvg::FillColor(nvgRGBA(0, 255, 0, 255));
                // nvg::Fill();
                nvg::ClosePath();

                nvg::BeginPath();
                nvg::MoveTo(200, 200);
                nvg::LineTo(x, y);
                nvg::StrokeColor(nvg::RGBA(0, 255, 0, 255));
                nvg::Stroke();
                nvg::ClosePath();
            }
        });    
    });
        
    RenderModule::RegisterNanoVGCallback("Zoomable NanoVG", [](NVGcontext* vg) {
        ZoomView::Draw("Zoomable View", vg, [](NVGcontext* vg) {
            nvg::SetContext(vg);
            nvg::BeginPath();
            nvg::Rect(0, 0, 200, 200);
            nvg::FillColor(nvg::RGBA(0, 0, 255, 255));
            nvg::Fill();
            nvg::ClosePath();

            nvg::BeginPath();
            nvg::MoveTo(0, 0);
            nvg::LineTo(200, 0);
            nvg::LineTo(200, 200);
            nvg::LineTo(0, 0);
            nvg::StrokeColor(nvg::RGBA(255, 255, 0, 255));
            nvg::StrokeWidth(2.0f);
            nvg::Stroke();
            nvg::FillColor(nvg::RGBA(255, 0, 0, 255));
            nvg::Fill();
            nvg::ClosePath();

            nvg::BeginPath();
            nvg::Rect(0, 220, 5, 5);
            nvg::FillColor(nvg::RGBA(255, 255, 255, 200)); // Semi-transparent background
            nvg::Fill();
            nvg::ClosePath();

            nvg::BeginPath();
            nvg::Text(0, 180, "Hello, NanoVG!");
            nvg::ClosePath();
        });
    });

    RenderModule::RegisterNanoVGCallback("Struct NanoVG", [](NVGcontext* vg) {
        ZoomView::Draw("Struct View", vg, [](NVGcontext* vg) {
            nvg::SetContext(vg);
            nvg::BeginPath();
            nvg::MoveTo(0, 0);
            nvg::LineTo(200, 0);
            nvg::LineTo(200, 200);
            nvg::LineTo(0, 0);
            nvg::MoveTo(200, 0);
            nvg::LineTo(400, 200);
            nvg::LineTo(200, 200);
            nvg::MoveTo(200, 200);
            nvg::LineTo(400, 0);
            nvg::LineTo(400, 200);
            nvg::StrokeColor(nvg::RGBA(255, 255, 0, 255));
            nvg::StrokeWidth(2.0f);
            nvg::Stroke();
            // nvg::FillColor(nvg::RGBA(255, 0, 0, 255));
            // nvg::Fill();
            nvg::ClosePath();

            // nvg::BeginPath();
            // nvg::Rect(0, 220, 5, 5);
            // nvg::FillColor(nvg::RGBA(255, 255, 255, 200)); // Semi-transparent background
            // nvg::Fill();
            // nvg::ClosePath();

            // nvg::BeginPath();
            // nvg::Text(0, 180, "Hello, NanoVG!");
            // nvg::ClosePath();
        });
    });

    // RenderModule::RegisterNanoVGCallback("Debug", [](NVGcontext* vg) {
    //     nvgBeginPath(vg);
    //     nvgRect(vg, 0, 0, 200, 200);
    //     nvgFillColor(vg, nvgRGB(0, 255, 0));
    //     nvgFill(vg);
    // });


    // RenderModule::RegisterNanoVGCallback("Zoomable Random Dots", [](NVGcontext* vg) {
    //     ZoomView::Draw("Zoomable random dot View", vg, [](NVGcontext* vg) {
    //         nvg::SetContext(vg);
    //         nvg::BeginPath();
    //         nvg::Rect(0, 0, 200, 200);
    //         nvg::FillColor(nvg::RGBA(0, 0, 255, 255));
    //         nvg::Fill();
    //         nvg::ClosePath();

    //         struct Point {
    //             float x, y;
    //         };
    //         static std::vector<Point> points;
    //         for (int i = 0; i < 100; ++i) {
    //             float x = rand() % 200;
    //             float y = rand() % 200;
    //             points.push_back({x, y});
    //         }
    //         for (const auto& p : points) {
    //             nvg::BeginPath();
    //             nvg::Rect(p.x, p.y, 1, 1);
    //             nvg::FillColor(nvg::RGBA(255, 0, 0, 255));
    //             nvg::Fill();
    //             nvg::ClosePath();
    //         }
    //         std::cout << points.size() << std::endl;
    //     });
    // });
    
    RenderModule::Run();
    RenderModule::Shutdown();
    return 0;
}
