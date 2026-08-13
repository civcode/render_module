#ifndef RENDER_MODULE_HPP_
#define RENDER_MODULE_HPP_

#include <functional>
#include <vector>
#include <string>

#include <imgui.h>
#include <implot.h>
#include "nanovg.h"

#include "render_module/canvas.hpp"
#include "render_module/view3d.hpp"
#include "render_module/nvg_wrapper.hpp"
#include "render_module/render_context.hpp"
#include "render_module/zoom_view_context.hpp"
#include "render_module/debug_console.hpp"

struct NVGcontext;

static ImGuiID GetRootDockspaceID();

class RenderModule {
public:
    using CanvasCallback = std::function<void(render_module::Canvas&)>;
    using View3DCallback = std::function<void(render_module::View3D&)>;

    // Returns false and prints a diagnostic when initialization fails.
    static bool Init(int width, int height, double fps = 30.0, const char* title = "RenderModule");
    static void EnableRootWindowDocking();
    static void RegisterImGuiCallback(std::function<void()> callback);
    static void RegisterCanvas(const std::string& name,
                               CanvasCallback callback,
                               std::function<void(NVGcontext*)> offscreenCallback = nullptr);
    static void Register3DView(const std::string& name,
                               View3DCallback callback,
                               const render_module::View3DOptions& options = {});

    // Backward-compatible API. New code should prefer RegisterCanvas.
    static void RegisterNanoVGCallback(const std::string& name, 
                                        std::function<void(NVGcontext*)> callback,
                                        std::function<void(NVGcontext*)> offscreenCallback = nullptr);
    static void ZoomView(std::function<void(NVGcontext*)> callback);
    static void IsolatedFrameBuffer(std::function<void(NVGcontext*)> userFramebufferRender); 
    static void Run();
    static void RequestClose();
    static void Shutdown();
    static render_module::Vec2 GetWindowSize();
    static ImVec2 GetGLFWWindowSize(); // Compatibility alias.
    static NVGcontext* GetNanoVGContext();
    static double GetFPS();
    static double GetDeltaTime();
    static bool IsInitialized();
    static ImGuiID GetRootDockspaceID();

    static DebugConsole& Console();
    static void EnableDebugConsole();

private:
    static DebugConsole console_;
};

#endif // RENDER_MODULE_HPP_
