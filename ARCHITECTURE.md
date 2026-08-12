# Architecture

## Component boundaries

The module now has four layers with one-way ownership:

1. **RenderModule** owns GLFW, OpenGL, ImGui, the application loop, and registered windows.
2. Each registered **Canvas** owns its framebuffer, ImGui hit target, raw mouse snapshot, and persistent viewport states.
3. A **Viewport** owns one world-to-canvas transform and arbitrates navigation versus content gestures.
4. Tools such as **PoseDragTool** consume **ViewportInput** and produce application data.

The important rule is that GLFW/ImGui input is sampled once by the same
component that creates the canvas hit target. Lower layers never query window
coordinates on their own. This removes the former implicit dependency between
render_module.cpp and zoom_view.cpp.

## Frame flow

For every visible canvas window:

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

## Gesture policy

Navigation defaults to the middle button; left drag is reserved for content.
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

- Add another interaction as a self-contained tool with Update(Viewport&) and Draw(Viewport&).
- Add plot convenience functions above ImPlot without coupling them to canvas input.
- Add callback handles if applications need dynamic window registration/removal.
- Add a command queue if worker threads need to create or remove visualization windows.

Avoid adding raw GLFW callbacks to tools. That would recreate multiple input
owners and make docking, high-DPI scaling, and gesture capture inconsistent.
