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
- `src/present/` consumes already-completed root frames. `DesktopPresenter`
  blits to the GLFW default framebuffer and swaps; `ImagePresenter` provides
  on-demand synchronous PNG capture.

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
- Existing Canvas and View3D FBOs feed the same root UI composition as Desktop.
  Headless dimensions come from `Config`, never the native pbuffer size.
  No remote input, networking, encoding, or Phase 4 implementation is present.

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

## Root output and presentation (Phase 3)

```text
Rendering core (ImGui + ImPlot + Canvas + View3D + docking/overlays/console)
    ↓ final ImGui draw list, rendered exactly once by the core
RootFramebuffer (RGBA8)
    ↓ PresentedFrame
Presenter
    ├ DesktopPresenter → blit to default framebuffer → swap
    └ ImagePresenter   → optional synchronous readback → PNG
```

`src/core/root_framebuffer.*` owns a single-sample `GL_RGBA8` color texture/FBO,
without depth/stencil. Creation checks completeness. Resize validates positive
sizes against texture/viewport limits, allocates replacement storage first, and
commits only on success. Same-size resize is a no-op; successful recreation
increments generation. The old completed frame survives failed resize.

`PresentedFrame` contains physical dimensions, frame ID, storage generation, and
steady-clock start/completion timestamps. It is a synchronous borrowed view, not
a GL owner. Its validity token expires on resize/destruction and is invalidated
when the next frame begins; stale descriptors return zero from GL handle
accessors and are rejected by both presenters. Neither presenter calls ImGui.
A future WebPresenter will consume this same descriptor, but is not implemented.

The core binds root before callbacks and rebinds it for final composition.
Per-window FBO creation/rendering and `IsolatedFrameBuffer()` restore independent
read/draw bindings and viewport even on exceptions. Final composition establishes
viewport, color-write mask, scissor-disabled clear, and disabled framebuffer-sRGB
conversion. Existing NanoVG/View3D texture UV conventions are unchanged.

Desktop normally uses **1:1 physical framebuffer pixels**, retaining logical
ImGui sizes and GLFW framebuffer scale for HiDPI. If a window changes size between
render and presentation, nearest-neighbor blitting uniformly fits the root into
centered black letterboxing (no aspect-ratio stretching). Blit disables scissor
and sRGB conversion and restores GL state. Zero-sized/minimized surfaces skip
rendering/presentation without allocating zero-sized root storage.

Headless uses explicit virtual dimensions and framebuffer scale 1. The internal
`RequestVirtualDisplaySize()` validates requests, then commits root storage and
frame metrics together at the next frame boundary. Allocation failure stops the
loop with diagnostics and retains old storage/size. There is no browser resize
protocol or input event queue.

`ImagePresenter` uses synchronous `glReadPixels(GL_RGBA, GL_UNSIGNED_BYTE)` and
restores read-buffer, framebuffer, and pixel-pack state. It temporarily unbinds
any caller-owned pack buffer; it does not create or use PBOs. **The sole output
vertical flip** reverses CPU rows immediately after readback, from GL bottom-origin
to top-origin RGBA. PNG encoding does not flip; Desktop blitting does not flip.
A path-less headless presenter only flushes work—readback occurs on explicit
capture, not every frame. The PNG writer is reused from pinned NanoVG; see
[PNG_NOTICE.md](third_party/PNG_NOTICE.md).

Shutdown, with the context current: presenter → root → Canvas/View3D GPU storage
→ Magnum renderer/tracker → NanoVG → ImGui renderer/platform and ImPlot/ImGui
contexts → native graphics context. Screenshot handles never escape the public
API; `SaveScreenshot(path)` is a narrow diagnostic addition.

Reference: Khronos [framebuffer blits](https://registry.khronos.org/OpenGL-Refpages/gl4/html/glBlitFramebuffer.xhtml)
and [pixel readback](https://registry.khronos.org/OpenGL-Refpages/gl4/html/glReadPixels.xhtml).

## Per-window frame flow

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
