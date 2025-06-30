#ifndef PLOTAPP_HPP_
#define PLOTAPP_HPP_

#include <functional>
#include <vector>
#include <string>

#include <imgui.h>
#include <imgui_internal.h>
#include <implot.h>
#include <implot_internal.h>
#include "nanovg.h"

#include "render_module/nvg_wrapper.hpp"
#include "render_module/glad_wrapper.hpp"
#include "render_module/render_context.hpp"
#include "render_module/zoom_view_context.hpp"

struct NVGcontext;

class RenderModule {
public:
    static void Init(int width, int height, double fps = 30.0, const char* title = "RenderModule");
    static void EnableRootWindowDocking();
    static void RegisterImGuiCallback(std::function<void()> callback);
    static void RegisterNanoVGCallback(const std::string& name, 
                                        std::function<void(NVGcontext*)> callback,
                                        std::function<void(NVGcontext*)> offscreenCallback = nullptr);
    static void ZoomView(std::function<void(NVGcontext*)> callback);
    static void IsolatedFrameBuffer(std::function<void(NVGcontext*)> userFramebufferRender); 
    static void Run();
    static void Shutdown();
    static ImVec2ih GetGLFWWindowSize();
    static NVGcontext* GetNanoVGContext();
    static double GetFPS();
};

#endif // PLOTAPP_HPP_