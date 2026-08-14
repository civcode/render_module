# Architecture

## Component boundaries

The module has a small set of one-way-owned visualization layers:

1. **RenderModule** owns GLFW, the OpenGL context, ImGui, the application loop, and registered windows.
2. Each registered **Canvas** owns a 2D framebuffer, ImGui hit target, raw mouse snapshot, and persistent 2D viewport states.
3. A 2D **Viewport** owns one world-to-canvas transform and arbitrates navigation versus content gestures.
4. An **ImageView** consumes immutable `ImageFrame` snapshots, owns one persistent NanoVG image, and exposes OpenCV-compatible pixel coordinates through `ImageCanvas`.
5. Each registered **View3D** owns persistent camera/input state and named dynamic GPU meshes while sharing the module-wide Magnum renderer.
6. Tools such as **PoseDragTool**, image selection code, or application-side 3D picking code consume the corresponding input and produce application data.

The important rule is that GLFW/ImGui input is sampled once by the same
component that creates the canvas hit target. Lower layers never query window
coordinates on their own. This removes the former implicit dependency between
render_module.cpp and zoom_view.cpp.

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

### Image frame flow

`RegisterImageView()` is implemented above the existing Canvas path rather than
introducing another ImGui or GLFW input owner:

1. A producer calls `ImageFrame::Copy()` outside the render thread. Source row
   stride and Gray/RGB/BGR/RGBA/BGRA channel order are normalized to owned RGBA.
2. `LatestImage::Publish()` atomically replaces its single immutable snapshot.
3. The Canvas callback atomically loads the newest snapshot and updates its
   persistent NanoVG image only when the shared snapshot identity changes.
4. Contain/actual-pixel fit, zoom, and pan produce one image-to-canvas transform.
5. `ImageCanvas` converts OpenCV-style top-left pixel coordinates to the
   lower-left NanoVG canvas for overlays and maps mouse input back to pixels.

The mailbox is deliberately latest-wins: display lag never causes a camera-frame
queue to grow. Applications that require every frame for computation should use
a separate bounded processing queue and publish only visualization snapshots to
`LatestImage`.

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

`LatestImage` is the concrete image version of this rule. Its producer-side
copy also prevents a `cv::VideoCapture` buffer from being reused while the GPU
upload reads it. NanoVG image creation, update, and deletion occur only in the
registered render callback.

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
