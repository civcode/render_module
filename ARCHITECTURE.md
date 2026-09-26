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
- `src/input/` provides `IInputBackend`. Desktop's `GlfwInputBackend` wraps
  unchanged `imgui_impl_glfw` callbacks, display metrics, and timing; both
  headless providers use `RemoteInputBackend` (see Phase 4 below).
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
- Headless input has no GLFW window/input dependency. The Null/EGL provider
  still uses GLFW for **context creation only**. Native clipboard, OS cursor and
  IME hooks are not provided; the optional Web client handles committed text.
- Existing Canvas and View3D FBOs feed the same root UI composition as Desktop.
  Headless dimensions come from `Config`, never the native pbuffer size.
  The optional Web presenter adds HTTP/JPEG delivery without changing rendering.

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
    ├ ImagePresenter   → optional synchronous readback → PNG
    └ WebPresenter     → synchronous readback → JPEG/WS (diagnostic) or VideoPipeline/WebRTC
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
accessors and are rejected by presenters. No presenter calls ImGui.
WebPresenter consumes this same descriptor and publishes only CPU-owned bytes.

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
loop with diagnostics and retains old storage/size. WebPresenter bridges a bounded
viewport mailbox to this API; input events remain independent of resize requests.

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

## Input backends (Phase 4)

```text
IInputBackend
    ├── GlfwInputBackend → existing ImGui GLFW platform backend
    └── RemoteInputBackend
             ↓ drains RemoteInputQueue (producers enqueue only)
        ImGuiIO Add*Event()
             ↓ ImGui::NewFrame()
        widgets / ReadCanvasInput() → CanvasInput → existing View3D navigation
```

The core calls `ImGui_ImplOpenGL3_NewFrame()`, then
`input->BeginFrame(io, width, height, deltaTime)`, then `ImGui::NewFrame()`.
It does not branch on input implementation. Remote metrics use root virtual
pixels, scale `(1,1)`, and positive steady-clock delta (sanitized to 1µs–1s).
Desktop continues obtaining logical/physical metrics and time from GLFW.

**Only the render thread calls ImGui.** The private `input/input_access.hpp`
hook obtains a `shared_ptr<RemoteInputQueue>` on the render thread after Init;
Desktop returns null. The Web network thread retains that handle and
only enqueue. Shutdown closes the queue under its mutex before destroying the
backend; retained producer handles safely return `Closed`. No input API was
added to installed public headers.

The private event variant contains `MouseMove`, `MouseButton`, `MouseWheel`,
`Key`, `TextUtf8`, `Focus`, `MouseSource`, `InputStateSnapshot`, and `ReleaseAll`.
It has no ImGui types and is **not a serialized wire format**:

- Pointer coordinates have a top-left origin, remain normalized in `[0,1]`,
  reject NaN/Inf, and clamp finite out-of-range values. Conversion to root
  pixels happens on consumption. Positions behind transition barriers stay in
  the producer queue until ImGui drains earlier input. On resize the current
  normalized anchor is reprojected before pending transitions, without losing
  fractional normalized precision to ImGui's pixel flooring.
- Five mouse buttons and already-normalized horizontal/vertical wheel units
  are supported. Mouse/Touch/Pen map to `AddMouseSourceEvent`; ImGui latches the
  source onto the next actual mouse event, not an independent IO transition.
- `RenderKey` covers letters, digits, navigation/editing, F1–F12, eight sided
  modifiers, punctuation, locks/menu, and numpad. One checked mapping table
  translates to ImGui keys. Physical left/right modifier state is ORed into
  aggregate Ctrl/Shift/Alt/Super events before the associated key transition.
- **Keys never generate text.** Committed UTF-8 is a separate event, at most
  256 bytes, with fixed inline storage. Invalid UTF-8, embedded NUL, surrogates,
  overlong sequences, and oversized payloads are rejected, never truncated.
  Split larger commits at UTF-8 boundaries and retry on backpressure. The build
  exports `IMGUI_USE_WCHAR32` through the CMake target so supplementary-plane
  characters survive InputText; consumers must rebuild with the same definition.

### Queue and recovery policy

The mutex-protected ring has **256 fixed slots**, with no per-event allocation.
Acceptance order is FIFO, with an internal monotonic sequence number assigned
under the mutex. Only **adjacent unconsumed MouseMove events** coalesce; clicks,
wheel, keys, text, source changes, focus, and snapshots are barriers. Optional
source sequence numbers reject values not greater than the last accepted value
(`Stale`); transport/session sequencing belongs to the Web adapter, not this queue.

`Enqueue()` returns `Accepted`, `Coalesced`, `Full`, `Invalid`, `Stale`, or
`Closed`. A full queue never evicts a reliable event or silently drops a release.
**The producer must retry `Full` (including key/button ups), or explicitly cancel
input using `ReleaseAllInput()`**. Rejected events do not advance sequencing.

Handoff to ImGui is bounded to 64 typed events per batch, and waits for ImGui's
previous batch to drain. Otherwise ImGui's trickling queue could grow without
bound despite the bounded producer ring. Two isolated pinned-ImGui-internal
operations inspect pending events and place a resize reprojection before them;
all input generation uses Add*Event APIs. Trickling remains enabled to preserve
quick down/up clicks. A worst-case text batch has at most 64×256 codepoints.

Ordered `Focus(false)` and `ReleaseAll` synthesize releases for every key,
button, and aggregate modifier. Focus loss ends the batch so a subsequent gain
cannot cancel the defensive release. Downs/text/wheel/movement are ignored
while unfocused; `Focus(true)` or a focused snapshot restores input acceptance.
`ReleaseAll{false}` releases controls without changing focus.

The separate **`ReleaseAllInput()` cancellation barrier is always available**
while open, including saturation. It explicitly cancels older producer input
and ImGui's deferred backlog, then synthesizes all releases and focus loss on
the next render frame. Newer enqueues are retained until after that reset frame.
This is the disconnect/controller-replacement escape hatch, not an automatic
or silent overflow shortcut.

Snapshots reconcile missing and stale keys/buttons, normalized position, and
focus through transitions. Aggregate modifier flags are authoritative: false
clears both physical sides; true preserves supplied sides or supplies the left
side if none is given. An unfocused snapshot releases everything. Snapshots
provide authoritative recovery after lost events; the input backend itself has no
network timers/transmission.

Tests exercise concurrent producers, FIFO/overflow/cancellation, transitions,
UTF-8, resize during trickling, and real widgets/Canvas/View3D with stable root
storage. The Desktop interaction test uses installed GLFW callbacks. No new
camera controller or remote Canvas path exists; Web adds only a presenter and
network-to-existing-queue adapter.

References: pinned Dear ImGui [input event processing](https://github.com/ocornut/imgui/blob/v1.91.9b-docking/imgui.cpp),
[GLFW backend](https://github.com/ocornut/imgui/blob/v1.91.9b-docking/backends/imgui_impl_glfw.cpp),
and [Unicode configuration](https://github.com/ocornut/imgui/blob/v1.91.9b-docking/imconfig.h).

## Embedded Web UI (Phase 5)

```text
RootFramebuffer → WebPresenter → ImagePresenter::Read → libjpeg → immutable packet
                                                                  ↓
Browser canvas ← JPEG binary WebSocket ← WebServer (one Asio network thread)
Browser input → JSON WebSocket → controller/validation → RemoteInputQueue
                                                            ↓ render thread
                                                    RemoteInputBackend → ImGui
```

`Backend::Web` selects an existing headless provider, the existing input backend,
and `WebPresenter`. `IPresenter::PrepareFrame()` consumes the latest viewport
request at a render boundary; normal rendering is otherwise unchanged. JPEG
readback/encoding is synchronous and capped independently of render fps. No GL
work or borrowed framebuffer handle crosses into the network thread.

Boost.Beast/Asio and Boost.JSON stay private in `src/web/`; the presenter does not
expose their types. Vanilla assets are compiled into the library. HTTP/WebSocket
sessions have bounded parsing, write/control/frame queues and shutdown deadlines.
FirstConnected controller authority, exact Origin checks and token/cookie
validation are server-enforced. Controller loss/saturation invokes Phase 4's
release-all cancellation barrier; no alternate ImGui or camera path is introduced.

See [WEB_PROTOCOL.md](WEB_PROTOCOL.md) for protocol v1, explicit bounds, security,
viewport negotiation, input details, diagnostics and test coverage. **JPEG/WS is
an explicit diagnostic transport**; Phase 7 adds optional WebRTC media. Phase 6
software video remains transport-independent. PBOs, hardware encoding and
DataChannels are not implemented.

## Software realtime video (Phase 6)

```text
RootFramebuffer → CPU Frame Capture (render thread; synchronous, one flip)
                      /                         \
                 JPEG prototype            owned RGBA VideoFrame
                                                    ↓ latest slot
                                             VideoConverter (worker)
                                                    ↓ I420 / BT.709 limited
                                               IVideoEncoder
                                                    ↓ OpenH264Encoder
                                          EncodedFrame / H.264 Annex-B
                                                    ↓
                                          Phase 7 WebRTC adapter (optional)
```

`RenderModule::Video` is a separate optional CPU-only library. Its public header
contains no GL, ImGui, OpenH264, libyuv or Web types. Only `OpenH264Encoder` knows
codec types. `VideoCapture` bridges the core's completed frame into a four-slot
CPU pool using `ImagePresenter::ReadInto()`, then transfers immutable ownership.
The worker converts into reusable I420 storage, encodes synchronously and publishes
one access unit including SPS/PPS. The framebuffer generation follows every frame.

One pending raw frame is replaceable; one encoded output slot applies backpressure.
Dependent P access units are not arbitrarily dropped. Resolution commands invalidate
old pending/output data and suppress old in-flight completion before recreating the
codec and producing an IDR. All GL remains on the render thread; shutdown needs no
GL context on the worker. JPEG's transport/presenter remains unchanged; when both
outputs are active, their synchronous readbacks currently remain separate.

See [VIDEO_PIPELINE.md](VIDEO_PIPELINE.md) for precise color/framing/timebase,
configuration, ownership/drop and control contracts. H.264 bytes are never sent
through WebSocket.

## WebRTC media (Phase 7)

`WebPresenter` selects either JPEG or an owned Phase 6 pipeline/capture bridge.
A CPU fanout worker drains complete access units, copies each once to release the
encoder pool, then shares immutable storage across sessions. Every session owns a
PeerConnection, send-only H.264 track, packetizer, SSRC, RTP clock/sequence, bounded
NACK history and RTCP state. One active/one pending AU per independent sender worker
isolates slow viewers; dropped dependency chains wait for a fresh IDR. Ready/PLI
requests share one coalescing `VideoStreamController`. REMB is observation only;
no unbounded pacing queue is introduced.

The existing authenticated WebSocket carries bounded versioned offer/answer/ICE
signaling and all Phase 5 input/viewport messages. Signaling and close are serialized
on its I/O thread; library callbacks use weak ownership and bounded event queues.
Media failure/disconnect tears down that session and invokes existing controller
release-all recovery. Render/GL ownership and Phase 6 public interfaces are unchanged.

The browser's video dimensions drive image/input mapping; root resize changes the
in-band SPS/PPS/IDR without renegotiating a healthy peer. Dependencies and the two
libnice caveats (relay-policy build patch, unsupported TURN/TLS) are documented in
[WEBRTC.md](WEBRTC.md). **Phase 8/DataChannel input has not started.**

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
