# Render Module

Render Module is a small C++17 visualization layer for engineering applications. It combines:

- ImGui for windows, controls, and docking
- ImPlot for conventional plots
- NanoVG for custom 2D graphics
- a canvas/viewport input API for world-coordinate mouse interaction

The **PathPlanningDemo** shows RViz-style pose placement: left-click to set a
position, drag to set orientation, and release to commit.

## Build

On Ubuntu/Debian, install GLFW and the normal OpenGL development packages:

~~~sh
sudo apt-get install libglfw3-dev
~~~

Then configure and build:

~~~sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
./build/PathPlanningDemo
~~~

Installation is optional and respects the standard CMake prefix:

~~~sh
cmake --install build --prefix "$HOME/.local"
~~~

Do not run the install as root when targeting your home directory.

To omit examples, configure with **-DRENDER_MODULE_BUILD_EXAMPLES=OFF**.

## Canvas interaction

**RegisterCanvas** provides the raw canvas and **DrawViewport** adds a persistent
world transform. Canvas and world coordinates use a lower-left origin with +Y
up, which is usually the least surprising convention for engineering plots.

~~~cpp
#include "render_module/pose_tool.hpp"
#include "render_module/render_module.hpp"

render_module::PoseDragTool goal;

RenderModule::RegisterCanvas("Map", [&](render_module::Canvas& canvas) {
    canvas.DrawViewport("world", [&](render_module::Viewport& view) {
        goal.Update(view);

        // Draw the map here with NanoVG. The active NanoVG transform is already
        // world -> canvas.
        goal.Draw(view);

        if (goal.HasPose()) {
            const auto& pose = goal.Pose();
            // pose.position and pose.yaw are ready for the planner.
        }
    });
});
~~~

Default controls:

- left drag: available to content tools such as **PoseDragTool**
- middle drag: pan
- mouse wheel: zoom around the cursor

Use **Viewport::Input()** for custom tools. It reports edge-triggered
pressed/released states plus canvas and world positions. A tool that owns a
multi-frame gesture should call **Viewport::CapturePointer()**; the supplied
pose tool does this automatically.

## ImGui and ImPlot

Conventional plots and control panels remain simple callbacks:

~~~cpp
RenderModule::RegisterImGuiCallback([] {
    ImGui::Begin("Signals");
    if (ImPlot::BeginPlot("Response")) {
        ImPlot::PlotLine("y", xs, ys, count);
        ImPlot::EndPlot();
    }
    ImGui::End();
});
~~~

## Using the installed package

~~~cmake
cmake_minimum_required(VERSION 3.14)
project(MyVisualization LANGUAGES CXX)

find_package(RenderModule REQUIRED)
add_executable(MyVisualization main.cpp)
target_link_libraries(MyVisualization PRIVATE RenderModule::RenderModule)
~~~

If CMake cannot find a user-local install, configure your application with:

~~~sh
cmake -S . -B build -DCMAKE_PREFIX_PATH="$HOME/.local"
~~~

NanoVG fonts are discovered in the build assets, the configured install data
directory, user-local and system data directories, and the original v0.1
**fonts/roboto** layout. Set **RENDER_MODULE_FONT_DIR** to override discovery at
runtime. Viewport status text uses ImGui and therefore remains visible even when
optional NanoVG fonts cannot be found.

## Compatibility

**RegisterNanoVGCallback**, **RenderModule::ZoomView**, and the **ZoomView**
conversion helpers remain available for existing code. New code should use
**RegisterCanvas** and **Canvas::DrawViewport**, because they make input
ownership and coordinate conversion explicit.

See [ARCHITECTURE.md](ARCHITECTURE.md) for component boundaries and extension
guidance.
