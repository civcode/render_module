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
#include "render_module/zoom_view.hpp"

struct NVGcontext;

class RenderModule {
public:
    static void Init(int width, int height, const char* title = "RenderModule");
    static void RegisterImGuiCallback(std::function<void()> callback);
    static void RegisterNanoVGCallback(const std::string& name, std::function<void(NVGcontext*)> callback);
    static void Run();
    static void Shutdown();
};

#endif // PLOTAPP_HPP_