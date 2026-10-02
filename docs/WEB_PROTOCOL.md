# Web backend — control protocol v2 / image protocol v2

The Web backend exposes one embedded frontend with a **per-session stream mode**:

```text
JPEG  -> binary JPEG images over the authenticated WebSocket
PNG   -> binary PNG images over the authenticated WebSocket
H.264 -> WebRTC H.264 media, signaled over that same WebSocket
```

The WebSocket is the lifetime/session backbone in every mode. It carries HTTP-upgrade
authentication, stream control and WebRTC signaling for the whole connection. JPEG/PNG
also use it for ordinary JSON input; H.264 uses the WebRTC DataChannels documented in
[INPUT_PROTOCOL.md](INPUT_PROTOCOL.md). Switching stream mode does not reload the page,
replace the WebSocket, change the session ID or relinquish the controller lease.

The initial stream is configured with `WebConfig::initialStream` or
`--web-stream jpeg|png|h264`. It is only the mode assigned to a newly connected browser;
it does not disable the other available stream modes. Builds without WebRTC advertise
JPEG and PNG only.

## Dependencies and packaging

Boost.Beast/Asio provide asynchronous HTTP and WebSocket support and Boost.JSON handles
strict JSON/UTF-8 parsing. Boost >=1.75 is required. `JPEG::JPEG` supplies JPEG encoding.
PNG reuses the public-domain `stb_image_write` already pinned through NanoVG; no temporary
files or external processes are used. See [WEB_NOTICE.md](../third_party/WEB_NOTICE.md).

All HTTP-library types remain private to `src/web/`. Assets in `web/` are embedded by
CMake for source and installed builds; Node/npm are test-only dependencies.

## Threads and lifetime

- **Render thread:** GL rendering, root resize, image readback/encoding, and
  `VideoCapture::Submit()` when H.264 has subscribers.
- **Network thread:** HTTP/WebSocket, session state, stream switching, controller
  ownership, image delivery/backpressure, WebRTC signaling and per-session media lifetime.
- **Cross-thread state:** latest JPEG packet, latest PNG packet, viewport mailbox,
  stream-demand counters and bounded metrics.

`WebPresenter::Present()` queries demand each cadence. With no image viewers it does no
image readback/encode; with no H.264 viewers it does no H.264 capture. JPEG and PNG active
at the same time share one RGBA readback and are encoded at most once each for that frame.
The initial implementation intentionally keeps H.264 capture as a separate readback when
image and H.264 viewers coexist.

Shutdown stops accepting, releases input, closes WebRTC session state and WebSockets, and
forces remaining sockets closed after the existing bounded shutdown deadline.

## Endpoints and security

| Endpoint | Policy |
|---|---|
| `GET /`, `/app.js`, `/style.css` | Public static login/client assets |
| `GET /healthz` | Public `{"ok":true}`; independent of render progress |
| `GET /api/version` | Authenticated when a token is configured; capability/session/counter diagnostics |
| `POST /api/login` | Exact allowed Origin + bearer credential; creates HttpOnly cookie |
| WebSocket `/api/ws` | Exact allowed Origin + bearer header or login cookie |

Default bind is **127.0.0.1:8080**. Missing, duplicate or unlisted browser Origins fail.
Non-loopback binds require explicit allowed origins plus authentication unless
`allowUnauthenticatedPublicBind=true`. Tokens remain 16–128 URL-safe ASCII characters and
are never logged or placed in URLs. Session IDs are independent random 128-bit hex values.
The server is plaintext HTTP/WS; use trusted TLS termination/tunneling for remote access.

Changing stream mode changes only the requesting session. It never grants controller
authority, and one session cannot signal or select a stream for another session.

## Stream modes and capability discovery

The canonical public type is:

```cpp
enum class WebStreamMode { Jpeg, Png, H264 };

struct WebConfig {
    WebStreamMode initialStream = WebStreamMode::Jpeg;
    // ...
};
```

JPEG and PNG are always available when the Web backend starts. H.264 is advertised only
when the WebRTC/video capability initialized successfully. Explicitly selecting H.264 as
the initial stream when that capability is unavailable is a startup error; an image-mode
server may still start and simply omit `h264` from `availableStreams`.

Each upgraded connection receives a `welcome` describing its own committed stream:

```json
{
  "v": 2,
  "type": "welcome",
  "session": "0123456789abcdef0123456789abcdef",
  "control": true,
  "stream": "jpeg",
  "availableStreams": ["jpeg", "png", "h264"],
  "mediaTransport": "websocket",
  "inputTransport": "websocket-json-v1"
}
```

For H.264 the last three values become `"h264"`, `"webrtc"`, and
`"datachannel-v1"` respectively. `mediaTransport` is derived state, not a second user
configuration axis.

## Runtime stream switching

The browser requests a stream with:

```json
{"v":2,"seq":17,"type":"set_stream","stream":"png"}
```

Valid values are `jpeg`, `png`, and `h264`. A successful committed change returns:

```json
{
  "v": 2,
  "type": "stream_changed",
  "stream": "png",
  "mediaTransport": "websocket",
  "inputTransport": "websocket-json-v1",
  "control": true
}
```

A recoverable failure keeps the WebSocket alive and reports, for example:

```json
{"v":2,"type":"stream_error","stream":"h264","code":"unavailable"}
```

Current error codes include `unavailable`, `transition_in_progress`, and
`transition_failed`. A request for the already-active stable stream is idempotent and
returns `stream_changed` immediately. The first implementation permits only one switch
transaction at a time.

### JPEG <-> PNG

If no image frame is outstanding, the change commits immediately. If the session is
waiting for `frame_ack`, the server stops offering new frames, waits for that exact ACK,
then changes the per-session mode. This preserves the one-unacknowledged-frame invariant
and prevents a late ACK from becoming invalid merely because a switch was requested.

### JPEG/PNG -> H.264

The server drains any outstanding image ACK, releases held input state, commits H.264 and
sends `stream_changed`. The browser then sends the existing WebRTC `hello` over the same
WebSocket. During negotiation ordinary WebSocket input is disabled; there may briefly be
no authoritative input path, which is preferable to dual authority.

### H.264 -> JPEG/PNG

The server disables/removes that session's DataChannel/WebRTC state, releases held input,
changes demand counters and commits the image mode. The browser closes its peer only after
receiving `stream_changed`, displays the canvas and resumes WebSocket JSON input. The
controller lease itself is preserved.

Recoverable WebRTC setup/media failure sends `stream_error` and reverts the session to its
last image stream. Malformed protocol/security input remains a fatal session error.

## Image frames and backpressure

Image protocol **v2** is a 28-byte big-endian header followed by encoded bytes:

| Offset | Field |
|---:|---|
| 0 | u32 magic `0x524d494d` (`RMIM`) |
| 4 | u16 version `2` |
| 6 | u16 codec: `1` JPEG, `2` PNG |
| 8 | u64 root frame ID |
| 16 | u32 width |
| 20 | u32 height |
| 24 | u32 payload size |

RGBA8 readback uses the existing single CPU vertical flip. JPEG drops alpha and is lossy;
PNG preserves RGBA8. Encoded payloads are capped at 16 MiB.

Publication is codec-specific: the server has one replaceable latest JPEG mailbox and one
replaceable latest PNG mailbox. Each packet is immutable and shared by every session using
that codec. A slow JPEG session therefore does not block PNG or H.264 sessions and vice
versa. Per image session there is one in-flight write and one replaceable latest pending
frame, plus one unacknowledged displayed frame. Five-second write/ACK deadlines bound slow
clients.

The browser validates image protocol/version/codec/length/dimensions and rejects a binary
image whose codec does not match its currently committed stream.

## Control and input protocol

Every browser-to-server JSON message uses control protocol **v2**, a `type`, and a strictly
increasing positive `seq` <=2^53-1 scoped to that WebSocket connection. New connections
start a new sequence. Image frame IDs in ACKs are sent as decimal strings to preserve the
full u64 range.

```json
{"v":2,"seq":1,"type":"viewport","width":1200,"height":800,"devicePixelRatio":2}
{"v":2,"seq":2,"type":"focus","focused":true}
{"v":2,"seq":3,"type":"mouse_move","x":0.5,"y":0.4,"source":"mouse"}
{"v":2,"seq":4,"type":"mouse_button","button":"left","down":true}
{"v":2,"seq":5,"type":"text","text":"äöüÄÖÜß 🙂"}
{"v":2,"seq":6,"type":"frame_ack","frameId":"42"}
```

Additional image-mode input types remain `wheel`, `key`, `snapshot`, and `release_all`.
Malformed, oversized, stale-sequence or invalid-enum/UTF-8 messages close the connection.
JSON input/control is limited to 8 KiB; signaling envelopes may use up to 64 KiB.

**Input authority invariant:**

```text
JPEG / PNG -> websocket-json-v1
H.264      -> datachannel-v1
```

Ordinary WebSocket input is rejected while H.264 is committed. DataChannel state is torn
down before WebSocket input becomes authoritative again. Stream selection itself does not
require controller authority: viewers may choose how they receive pixels without gaining
input or resize permission.

FirstConnected controller election remains unchanged. Controller loss/disconnect invokes
release-all and promotes the oldest eligible viewer. A successful stream switch does not
re-elect or relinquish the controller.

## WebRTC signaling

WebRTC is an H.264 stream capability, not a process-wide Web transport mode. Signaling uses
the same authenticated `/api/ws` connection and is accepted only while that session is
committed to H.264.

| Type | Direction | Fields/action |
|---|---|---|
| `hello` | client -> server | Starts one H.264 peer for this session |
| `hello` | server -> client | ICE servers and transport policy |
| `offer` | server -> client | send-only H.264 + SCTP application m-line |
| `answer` | client -> server | validated answer SDP |
| `ice-candidate` | either | candidate + `mid` |
| `ice-complete` | either | end of candidates in that direction |
| `webrtc-state` | server -> client | bounded peer state diagnostic |

SDP remains capped at 32 KiB, candidates at 1024 bytes and signaling at 128 messages per
second within the total WebSocket rate limit. Signaling messages carry and are checked
against the owning 32-hex session ID. A JPEG/PNG session cannot create a peer by sending
`hello` directly.

The browser uses a peer-generation token so async SDP/ICE completion from a peer that was
closed during a switch cannot mutate a later peer or image stream.

See [WEBRTC.md](WEBRTC.md) for RTP/H.264 details and [INPUT_PROTOCOL.md](INPUT_PROTOCOL.md)
for the binary DataChannel protocol.

## Browser mapping and resize

The focusable Pointer Events surface fits the active media content rectangle (canvas for
JPEG/PNG, video for H.264), not the whole letterboxed container. Only captured drags clamp
out-of-image positions. Pointer capture is released on up/cancel. Wheel conversion and
keyboard/text/IME handling are unchanged from the existing Web input path.

`ResizeObserver` requests remain debounced. The server does not multiply CSS dimensions
by DPR: it uniformly fits requested dimensions into configured maxima. Root viewport
clamping no longer changes based on a process-wide transport. H.264 normalization may crop
the final odd row/column to legal encoder dimensions without forcing every image viewer to
use an even root viewport.

## Diagnostics and tests

Authenticated `/api/version` now describes mixed sessions rather than pretending the
server has one global codec. Important fields include:

```json
{
  "backend": "web",
  "initial_stream": "jpeg",
  "available_streams": ["jpeg", "png", "h264"],
  "sessions": 3,
  "stream_sessions": {"jpeg": 1, "png": 1, "h264": 1}
}
```

JPEG and PNG have independent encoded/dropped/encode-time counters. Existing WebRTC/RTP/
DataChannel metrics remain present when the H.264 capability exists. Stream switch request,
success and failure counters are also exported.

Native tests cover parser v2, per-session JPEG/PNG routing, ACK-drained switching,
controller preservation, unavailable H.264 and signaling-state gating. Browser tests cover
live JPEG <-> PNG switching without a WebSocket reconnect; WebRTC browser tests additionally
cycle H.264 -> JPEG -> H.264 on one session and verify peer teardown/recreation.

### Phase 5 validation snapshot

Mesa 25.2.8 llvmpipe / LLVM 20.1.2; Chromium 153.0.8010.12 via pinned Playwright
1.63.0; Release builds. Desktop tests used Xvfb; headless/Web tests explicitly
unset both `DISPLAY` and `WAYLAND_DISPLAY`.

| Configuration | Result |
|---|---:|
| Desktop + Headless + Web | 22/22 |
| Desktop-only, Web disabled | 7/7 |
| Headless + Web, Desktop disabled | 17/17 |
| Web-specific subset (included above) | 5/5 |

All six examples build in each configuration. Web demo smoke runs succeed with
both headless providers. The Phase 3 PNG golden has zero channel/pixel error;
Phase 4 queue/input and Desktop presentation regressions pass. Server, JPEG, PNG and
Chromium tests also passed three consecutive repeats. The JPEG C wrapper was
additionally AddressSanitizer-instrumented for its existing success/cap-failure
suite (leak detection disabled for the GL integration process).

Installed-library Chromium E2E passes from outside the source tree; embedded
assets need no runtime asset directory. Headless-only linkage has no X11/GLX/
Wayland or ImGui GLFW input-backend symbols; Desktop-only has no Boost/JPEG
runtime dependency. `git diff --check` passes.

A two-second fixture observation at 1000×696: **19.97 JPEG fps**, **30.96% of one
CPU core** for the server/renderer process, **2.05 ms** average synchronous
readback/encode/pack. This is a diagnostic sample, not a general performance
benchmark; browser CPU is excluded and fixture state-file reporting is included.
NVIDIA hardware, Firefox and real OS IME were unverified at that checkpoint. These
counts describe Phase 5; Phase 6 validation is documented separately.

### JPEG/PNG comparison

Release, Chromium 153, Mesa llvmpipe on Ryzen 9 9950X; 1.5-second samples, browser
CPU excluded. `meanEncodeMs` includes synchronous readback, encoding and packet
construction. Static and representative ImGui/ImPlot/NanoVG/View3D scenes were
measured; representative values are:

| Size | JPEG fps / ms / bytes | PNG fps / ms / bytes |
|---|---:|---:|
| 1000×700 | 19.97 / 1.93 / 37,739 | 19.96 / 39.79 / 37,189 |
| 1280×720 | 20.63 / 2.66 / 41,089 | 17.32 / 54.93 / 46,150 |
| 1920×1080 | 19.98 / 5.16 / 59,329 | 7.99 / 122.87 / 91,621 |

Static PNG sizes were 33,625 / 42,487 / 87,993 bytes; JPEG 30,391 / 33,702 /
51,942. PNG used ~102.5%, 112.6%, 107.8% of one logical CPU for the representative
sizes versus JPEG ~30.0%, 33.9%, 45.3%. These low-motion samples are not throughput
ceilings; PNG is deliberately not the realtime default. Raw artifacts are
`test-artifacts/web-image-benchmark/metrics.json`.

References: [Beast async server example](https://github.com/boostorg/beast/blob/boost-1.83.0/example/websocket/server/async/websocket_server_async.cpp),
[write ownership](https://www.boost.org/doc/libs/1_83_0/libs/beast/doc/html/beast/ref/boost__beast__websocket__stream/async_write.html),
[libjpeg API](https://github.com/libjpeg-turbo/libjpeg-turbo/blob/main/doc/libjpeg.txt),
[stock memory destination ownership](https://github.com/libjpeg-turbo/libjpeg-turbo/blob/2.1.5.1/jdatadst.c)
(the custom destination avoids its finish-only publication of grown buffers).