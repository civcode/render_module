# Architecture

## Component boundaries

The module has a small set of one-way-owned visualization layers:

1. **RenderModule** owns private platform, input, and presenter interfaces, plus ImGui, the application loop, and registered windows.
2. Each registered **Canvas** owns a 2D framebuffer, ImGui hit target, raw mouse snapshot, and persistent 2D viewport states.
3. A 2D **Viewport** owns one world-to-canvas transform and arbitrates navigation versus content gestures.
4. Each registered **View3D** owns persistent camera/input state and named dynamic GPU meshes while sharing the module-wide Magnum renderer.
5. Tools such as **PoseDragTool** or application-side 3D picking code consume the corresponding viewport input and produce application data.

The important rule is that ImGui input is sampled once by the same
component that creates the canvas hit target. Lower layers never query window
coordinates on their own. This removes the former implicit dependency between
render_module.cpp and zoom_view.cpp.

## Platform backends (Phases 1–2)

- `src/platform/platform_backend.hpp` defines `IGraphicsContext` and
  `IPlatformBackend`: current-context/proc loading, lifetime, events, close state,
  logical window size, and physical framebuffer size.
- `GlfwDesktopBackend` owns GLFW and the window/context. The unchanged public
  `Init(width, height, fps, title)` selects this backend.
- `src/input/` adapts `imgui_impl_glfw` behind `IInputBackend`; it installs native
  callbacks and supplies ImGui display size, framebuffer scale, and frame timing.
- `src/present/` implements `IPresenter` with `DesktopPresenter`, which swaps the
  GLFW default framebuffer after ImGui renders. No root FBO is introduced here.

The core contains no GLFW calls or window pointer. Input and presentation borrow
backend resources and are destroyed before the platform. Initialization, `Run()`,
and `Shutdown()` make the owned GL context current; NanoVG, ImGui, and Magnum GPU
resources are released before context destruction. Call these APIs on the same
main/render thread. Existing Magnum external-state boundaries are unchanged.

### Linux headless EGL

The additive `Init(Config)` overload selects `GlfwNullEgl` or `NativeEgl`
explicitly; neither falls back to Desktop or another provider. `Initialize()`,
`MakeCurrent()`, `GetProcAddress()`, diagnostics, and `Shutdown()` remain private
platform responsibilities. Public headers contain no GLFW/EGL/Magnum types.

- `GlfwNullEglBackend` uses GLFW 3.5.1's Null platform and EGL context API.
  GLFW uses a Mesa surfaceless **display platform** with a pbuffer surface.
- `NativeEglBackend` prefers `EGL_EXT_platform_device` and an enumerated device
  (first device by default, or a strict explicit index). Only when device
  enumeration is unavailable/empty may it use Mesa's surfaceless display
  platform. It never calls `eglGetDisplay(EGL_DEFAULT_DISPLAY)`.
- Native EGL requests desktop OpenGL 3.3 Core and prefers a surfaceless context.
  If unsupported or creation/binding fails, it logs the reason and tries a 1×1
  pbuffer on the same device. Pbuffer use may also be explicitly requested.
  Failed device selection/initialization never silently selects another GPU.
- Headless frame metrics supply only size, scale, and monotonic delta time to
  ImGui; they are **not** a remote input backend. There are no input events,
  clipboard/cursor hooks, or remote queues.
- Existing Canvas and View3D FBOs render normally. The headless presenter flushes
  their GPU work but has no final output target. The core skips framebuffer-0
  clearing and final ImGui rendering, even on the pbuffer path. **There is no root
  UI FBO, screenshot API, final headless composition, or Phase 3 implementation.**

### Magnum loader

Magnum's stock `GlxContext` and `EglContext` libraries define the same loader
symbols; linking both does not provide runtime dispatch. Headless-enabled builds
instead use `RenderModuleMagnumContext`: the pinned upstream function table
initializer plus a single provider-driven resolver. GLAD and Magnum both resolve
through the context most recently made current on the render thread. This lets
one process switch Desktop → EGL → Desktop after orderly shutdown.

`cmake/RuntimeMagnumContext.cmake` generates a build-tree copy of the upstream
initializer, expanding its NVIDIA EGL GL-1.0/1.1 reload block to every provider.
That avoids stale core pointers across context changes and avoids EGL queries on
GLX. The exact source guard is checked at configure time; upstream source is not
modified. Corrade/Magnum are pinned to the inspected commits because this adapter
uses Magnum's internal loader interface. Desktop-only builds retain the stock
GLX/CGL/WGL loader. View3D's Magnum `EnterExternal`/`ExitExternal` boundaries and
rendering implementation are unchanged.

References: [GLFW 3.5 release notes](https://www.glfw.org/docs/latest/news.html),
[Magnum platform support](https://magnum.graphics/doc/magnum/platform.html),
[EGL surfaceless contexts](https://registry.khronos.org/EGL/extensions/KHR/EGL_KHR_surfaceless_context.txt),
[optional DRM render-node metadata](https://registry.khronos.org/EGL/extensions/EXT/EGL_EXT_device_drm_render_node.txt).

## Frame flow

For every visible 2D canvas window:

1. Reserve one ImGui **InvisibleButton** for the complete content region.
2. Snapshot hover, button edges, drag state, mouse delta, and wheel input.
3. Allocate or resize a color plus depth/stencil framebuffer at the display pixel ratio.
4. Run the canvas callback inside a NanoVG frame.
5. Let each viewport apply navigation and expose world-coordinate input.
6. Display the completed framebuffer through the already-reserved ImGui item.
7. Draw diagnostic overlays with ImGui after the texture, keeping status text
   independent of optional NanoVG font files.

The framebuffer has a stencil attachment because NanoVG stencil strokes need
one. OpenGL framebuffer and viewport state are restored after every canvas.

### 3D frame flow

A `Register3DView()` window follows the same ImGui/FBO ownership pattern but skips
NanoVG:

1. Reserve one ImGui **InvisibleButton** for the full 3D content region.
2. Snapshot the same lower-left, +Y-up `CanvasInput` used by 2D canvases.
3. Allocate or resize a color plus depth/stencil framebuffer at the display pixel ratio.
4. Apply orbit/pan/zoom to persistent camera state unless the application captured the pointer.
5. Temporarily hand GL state ownership to Magnum, render points/meshes/primitives with depth testing, then return GL state ownership to the external renderer.
6. Display the FBO in ImGui with vertically flipped texture coordinates and draw camera status as an ImGui overlay.

Magnum does **not** own GLFW, ImGui, the event loop, or the framebuffer. It is
attached to the already-current OpenGL context through `Platform::GLContext`.
Public headers avoid Magnum types so it remains an implementation dependency.

## Gesture policy

2D navigation defaults to the middle button. 3D navigation defaults to right-drag
for orbit and middle-drag for pan. In both cases left drag is reserved for content.
This is a deliberate separation rather than a timing-dependent global lock.

Custom tools should:

- begin only from **input.Button(...).pressed && input.hovered**;
- keep their own small state machine across frames;
- call **viewport.CapturePointer()** until the gesture finishes;
- commit on **released**, even when the cursor has moved outside the canvas.

**PoseDragTool** is a reference implementation of this pattern.

## Threading and data ownership

All rendering callbacks run on the main/render thread. Long-running engineering
calculations should run elsewhere and publish immutable snapshots (or swap a
buffer under a short lock) for the render callback. Do not let the renderer
iterate a vector while a worker is resizing it.

The debug console now protects its log buffer, so worker threads may append log
messages. ImGui and NanoVG calls must still remain on the render thread.

## Extension points

- Add another 2D interaction as a self-contained tool with Update(Viewport&) and Draw(Viewport&).
- Add 3D localization tools on top of View3D::MouseRay(), IntersectGroundPlane(), and CapturePointer().
- Add new engineering primitives behind View3D without exposing Magnum types publicly.
- Add plot convenience functions above ImPlot without coupling them to canvas input.
- Add callback handles if applications need dynamic window registration/removal.
- Add a command queue if worker threads need to create or remove visualization windows.

Avoid adding raw GLFW callbacks to tools. That would recreate multiple input
owners and make docking, high-DPI scaling, and gesture capture inconsistent.
