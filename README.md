# Render Module

Render Module is a small C++17 visualization layer for engineering applications. It combines:

- ImGui for windows, controls, and docking
- ImPlot for conventional plots
- NanoVG for custom 2D graphics
- Magnum for lightweight OpenGL 3D rendering
- persistent 2D and 3D viewport input APIs for engineering interaction

The 3D layer is intentionally integrated into RenderModule instead of owning a second window or event loop. RenderModule keeps GLFW, ImGui, the OpenGL context, framebuffer ownership, and mouse input; Magnum supplies meshes, shaders, math, and GPU rendering internally.

## Build

Requires CMake 3.16+, a C/C++17 toolchain, and OpenGL development files. GLFW is
fetched at **3.5.1**; older system GLFW installations are not used. Corrade and
Magnum are pinned to tested commits. GLAD, ImGui, ImPlot, NanoVG, and fonts are
also fetched, so the first configure requires network access.

Linux Desktop builds additionally need X11/Wayland development packages (the
corresponding GLFW build options select them). Headless builds need EGL
headers/libraries and an OpenGL driver; Mesa/llvmpipe supplies a software path.

Then configure and build:

~~~sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
./build/Release/bin/RenderModule3DDemo
~~~

The existing 2D demo remains available as well:

~~~sh
./build/Release/bin/PathPlanningDemo
~~~

Installation is optional and respects the standard CMake prefix:

~~~sh
cmake --install build --prefix "$HOME/.local"
~~~

To omit examples, configure with `-DRENDER_MODULE_BUILD_EXAMPLES=OFF`.

## Linux headless rendering (Phase 2)

~~~sh
cmake -S . -B build-headless -DCMAKE_BUILD_TYPE=Release \
  -DRENDER_MODULE_ENABLE_HEADLESS=ON -DRENDER_MODULE_ENABLE_DESKTOP=OFF \
  -DRENDER_MODULE_BUILD_TESTS=ON
cmake --build build-headless --parallel
env -u DISPLAY -u WAYLAND_DISPLAY ctest --test-dir build-headless -L headless --output-on-failure
~~~

This configuration compiles GLFW without X11/Wayland and does not require a
display server, Xvfb, or desktop session. Leave `RENDER_MODULE_ENABLE_DESKTOP=ON`
(the default) to build both backends into one library. Headless support defaults
to OFF to preserve Desktop-only dependency requirements.

Select a provider explicitly using the additive overload:

~~~cpp
render_module::Config config;
config.backend = render_module::Backend::Headless;
config.headlessContext = render_module::HeadlessContext::GlfwNullEgl;
// Alternatively: HeadlessContext::NativeEgl
// config.nativeEgl.deviceIndex = 0;       // Strict EGL device index; default -1.
// config.nativeEgl.forcePbuffer = true;  // Native EGL compatibility/testing path.
if (!RenderModule::Init(config)) return 1;
// Register existing callbacks and call Run(); use RequestClose() to terminate.
~~~

The existing 3D demo can render its full scene offscreen for a finite smoke run:

~~~sh
env -u DISPLAY -u WAYLAND_DISPLAY ./build-headless/Release/bin/RenderModule3DDemo \
  --render-backend headless --headless-context glfw-null-egl --frames 3
# Use --headless-context native-egl for the native provider.
~~~

No arguments retain the normal interactive Desktop demo.

The original `Init(width, height, fps, title)` always selects Desktop. Neither
headless provider silently falls back to Desktop or to another provider/GPU.
GLFW Null/EGL requires Mesa's surfaceless display platform. Native EGL supports
device enumeration, surfaceless contexts, and a logged same-device pbuffer
fallback; it is the path intended for non-Mesa drivers such as NVIDIA. Startup
prints provider/device, GL vendor/renderer/version, GLSL, and EGL vendor/version.

**Phase 2 limitation:** Canvas/View3D render into their existing FBOs, but final
ImGui output is not rendered or presented in headless mode. No root UI FBO,
screenshot/stream output, remote input, browser functionality, or OSMesa provider
is implemented. No window-system events close a headless run automatically.

### Regression tests

~~~sh
cmake -S . -B build -DRENDER_MODULE_BUILD_TESTS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
~~~

Desktop tests need a display and OpenGL 3.3+. On Linux CI, run the full suite
under `xvfb-run -a ctest --test-dir build --output-on-failure` with Xvfb/Mesa
installed. Xvfb is only for Desktop tests: each headless test explicitly unsets
both display variables. Enable headless support at configure time to include
`headless_glfw`, `headless_native`, and `headless_pbuffer`, plus two full-scene
3D demo smoke tests when examples are enabled.

Tests cover context/proc loading, input callbacks, resize/scale, presentation,
existing 2D/3D APIs, close/reinitialization, invalid device selection, and
Desktop/EGL transitions. Headless tests verify actual NanoVG and Magnum pixels
read from existing per-window FBOs, not a root framebuffer or screenshot API.
The `desktop_no_display` test verifies that the legacy overload still fails
cleanly without a display.

Tested on Mesa 25.2.8 llvmpipe (LLVM 20.1.2), GL 4.5 Core / GLSL 4.50 / EGL 1.5:
GLFW Null + Mesa surfaceless display + pbuffer; native EGL device 0 (software)
with both surfaceless binding and an explicitly requested 1×1 pbuffer. Software
EGL devices may advertise DRM-node querying yet return no node; this is valid.
No working NVIDIA GPU/driver was available, so proprietary NVIDIA support remains
unverified. See [ARCHITECTURE.md](ARCHITECTURE.md) for the Magnum loader integration.

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
