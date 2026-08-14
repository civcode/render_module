# Render Module

Render Module is a small C++17 visualization layer for engineering applications. It combines:

- ImGui for windows, controls, and docking
- ImPlot for conventional plots
- NanoVG for custom 2D graphics
- Magnum for lightweight OpenGL 3D rendering
- persistent 2D and 3D viewport input APIs for engineering interaction
- thread-safe, zoomable image views for OpenCV and camera pipelines

The 3D layer is intentionally integrated into RenderModule instead of owning a second window or event loop. RenderModule keeps GLFW, ImGui, the OpenGL context, framebuffer ownership, and mouse input; Magnum supplies meshes, shaders, math, and GPU rendering internally.

## Build

A system GLFW development package is used when available. If `pkg-config` cannot find GLFW, CMake fetches GLFW automatically. Corrade, Magnum, GLAD, ImGui, ImPlot, NanoVG, and the bundled fonts are also fetched by CMake, so the first configure requires network access.

On Ubuntu/Debian, installing the normal GLFW/OpenGL development packages is recommended:

~~~sh
sudo apt-get install libglfw3-dev
~~~

Then configure and build:

~~~sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
./build/RenderModule3DDemo
~~~

The existing 2D demo remains available as well:

~~~sh
./build/PathPlanningDemo
~~~

When OpenCV development files are available, CMake also builds the interactive
image example:

~~~sh
./build/RenderModuleImageViewDemo
./build/RenderModuleImageViewDemo --camera 0
~~~

The first command generates an animated OpenCV image sequence. The second reads
camera device zero. If OpenCV is unavailable, only this optional example is
skipped; the RenderModule library itself has no OpenCV dependency.

Installation is optional and respects the standard CMake prefix:

~~~sh
cmake --install build --prefix "$HOME/.local"
~~~

To omit examples, configure with `-DRENDER_MODULE_BUILD_EXAMPLES=OFF`.

## Interactive 3D views

`RenderModule::Register3DView()` creates a dockable ImGui render window backed by its own color + depth/stencil framebuffer. Camera state persists from frame to frame.

~~~cpp
#include "render_module/render_module.hpp"

std::vector<render_module::Vec3> scan = /* lidar / point cloud */;
render_module::Pose3D robotPose = /* T_world_robot */;

RenderModule::Register3DView("Localization", [&](render_module::View3D& view) {
    view.Grid(20.0f, 1.0f);
    view.PointCloud("scan", scan, {0.8f, 0.9f, 1.0f, 1.0f}, 2.0f);

    view.Box("robot", robotPose, {0.8f, 0.5f, 0.25f});
    view.Axes("robot_pose", robotPose, 0.5f);
    view.Sphere("landmark", {2.0f, 1.0f, 0.3f}, 0.3f);

    if (view.Input().Button(render_module::MouseButton::Left).pressed) {
        render_module::Vec3 hit;
        if (view.IntersectGroundPlane(hit)) {
            // Use hit as a localization goal / picked world position.
        }
    }
});
~~~

Default 3D controls:

- right drag: orbit around the camera target
- middle drag: pan
- mouse wheel: zoom/dolly
- left mouse: reserved for application interaction and picking

A tool that owns a multi-frame gesture can call `View3D::CapturePointer()` to temporarily suppress camera navigation.

### Camera poses

The public 3D API deliberately does not expose Magnum types. `Camera3D::Pose()` and `SetPose()` use a RenderModule `Pose3D` and define the returned pose as `T_world_camera`. The camera's local `-Z` axis points forward and local `+Y` points up. The default engineering world convention is right-handed with `+Z` up.

~~~cpp
auto camera = view.Camera();
camera.LookAt({4.0f, -4.0f, 3.0f}, {0.0f, 0.0f, 0.0f});
render_module::Pose3D T_world_camera = camera.Pose();

// Restore a previously saved camera pose:
camera.SetPose(T_world_camera);
~~~

### 3D drawing API

The initial 3D API includes:

- `PointCloud()` for dynamic point sets
- `TriangleMesh()` for arbitrary triangle geometry with automatic flat normals
- `Box()`, `Sphere()`, and `Cylinder()` using Magnum procedural primitives
- `Line()` and `Polyline()`
- `Axes()` for pose / coordinate-frame visualization
- `Grid()` for a Z-up engineering reference plane
- `MouseRay()` and `IntersectGroundPlane()` for localization/picking interactions

Named point clouds and triangle meshes keep their GPU mesh/buffer objects and update them on subsequent calls with the same ID. Built-in primitives are created once and shared by all 3D views.

See `examples/view3d_demo.cpp` for a complete point-cloud/localization-style example with robot and sensor poses, custom triangle geometry, camera controls, and ground-plane picking.

## Canvas interaction (2D)

`RegisterCanvas` provides the raw canvas and `DrawViewport` adds a persistent world transform. Canvas and world coordinates use a lower-left origin with +Y up, which is usually the least surprising convention for engineering plots.

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

Default 2D controls:

- left drag: available to content tools such as `PoseDragTool`
- middle drag: pan
- mouse wheel: zoom around the cursor

Use `Viewport::Input()` for custom tools. It reports edge-triggered pressed/released states plus canvas and world positions. A tool that owns a multi-frame gesture should call `Viewport::CapturePointer()`; the supplied pose tool does this automatically.

## OpenCV images and camera streams

`RegisterImageView()` displays frames from a thread-safe `LatestImage` mailbox.
The producer copies an OpenCV BGR matrix into an immutable `ImageFrame`; the
render thread owns the NanoVG texture and uploads a frame only when the mailbox
contains a new snapshot. A slow display therefore drops obsolete frames instead
of building latency.

~~~cpp
auto images = std::make_shared<render_module::LatestImage>();

// Camera / processing thread:
cv::Mat bgr;
camera.read(bgr);
images->Publish(render_module::ImageFrame::Copy(
    bgr.cols, bgr.rows,
    render_module::ImagePixelFormat::BGR8,
    bgr.data, bgr.step, sequence));

// Register before RenderModule::Run():
RenderModule::RegisterImageView(
    "Camera",
    images,
    [](render_module::ImageCanvas& image) {
        if (image.IsHovered() &&
            image.Input().Button(render_module::MouseButton::Left).pressed) {
            const render_module::Vec2 pixel = image.MouseImagePosition();
            image.CapturePointer();
            // Start an image-space selection at pixel.
        }

        // NanoVG overlay coordinates remain canvas-local and +Y-up.
        const render_module::Vec2 marker =
            image.ImageToCanvas({320.0f, 240.0f});
        nvgBeginPath(image.Graphics());
        nvgCircle(image.Graphics(), marker.x, marker.y, 5.0f);
        nvgFillColor(image.Graphics(), nvgRGBA(255, 80, 40, 255));
        nvgFill(image.Graphics());
    });
~~~

Image-space coordinates deliberately match OpenCV: the origin is the top-left
pixel, +X points right, and +Y points down. `ImageToCanvas()` and
`CanvasToImage()` bridge this convention to RenderModule's lower-left, +Y-up
NanoVG canvas. `ImageToScreen()` and `ScreenToImage()` are aliases for these
canvas-local screen conversions.

Default image controls:

- left drag: reserved for application selection and measurement tools
- middle drag: pan
- mouse wheel: zoom around the image pixel under the cursor

`ImageFrame::Copy()` accepts Gray, RGB, BGR, RGBA, and BGRA 8-bit sources,
including padded OpenCV row strides. Conversion and ownership transfer happen
on the producer thread. All NanoVG/OpenGL creation, update, and deletion remain
on the RenderModule thread.

See `examples/image_view_demo.cpp` for rectangle selection, cursor crosshairs,
zoom reset, a synthetic image sequence, and optional live camera capture.

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

NanoVG fonts are discovered in the build assets, the configured install data directory, user-local and system data directories, and the original v0.1 `fonts/roboto` layout. Set `RENDER_MODULE_FONT_DIR` to override discovery at runtime. Viewport status text uses ImGui and therefore remains visible even when optional NanoVG fonts cannot be found.

## Compatibility

`RegisterNanoVGCallback`, `RenderModule::ZoomView`, and the `ZoomView` conversion helpers remain available for existing code. New 2D code should prefer `RegisterCanvas` and `Canvas::DrawViewport`. New 3D code should use `Register3DView`.

See [ARCHITECTURE.md](ARCHITECTURE.md) for component boundaries and extension guidance.
