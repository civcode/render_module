# Phase 5 Web backend — protocol v1

**TEMPORARY DEVELOPMENT TRANSPORT.** JPEG/WebSocket frames prove remote UI
interaction. WebRTC replaces frame delivery in **Phase 7**, which has not started.
[Phase 6 software video](VIDEO_PIPELINE.md) is a separate optional output; this
JPEG protocol is unchanged. DataChannels, asynchronous PBOs and hardware encoding
are not present.

## Dependencies and packaging

Boost.Beast/Asio provide asynchronous HTTP and WebSocket support: maintained,
permissively licensed, explicit upgrade inspection, binary messages, size limits,
and clear asynchronous ownership. Boost.JSON handles strict JSON/UTF-8 parsing.
Boost >=1.75 is required (tested: Ubuntu Boost 1.83). `JPEG::JPEG` supplies the
libjpeg encoder API (tested: libjpeg-turbo 2.1.5). See
[WEB_NOTICE.md](third_party/WEB_NOTICE.md). No external encoder process is used.

All HTTP-library types are confined to `src/web/`. The three files in `web/`
are embedded into the library by CMake, including installed builds. No disk asset
root, Node.js, npm, or browser framework is needed at runtime.

## Threads and lifetime

- **Render thread:** all GL/ImGui/Canvas/View3D calls, root resize, synchronous
  `ImagePresenter::Read()`, JPEG encoding and immutable packet publication.
- **One network thread:** HTTP, WebSocket, JSON validation, controller ownership
  and enqueue into the existing Phase 4 `RemoteInputQueue`. No GL/ImGui calls.
- Shared bridges are a latest-frame mailbox, latest-viewport-request mailbox,
  and atomic counters. No callback is posted per rendered frame.
- `WebPresenter::PrepareFrame()` consumes the viewport mailbox at the frame
  boundary using the existing transactional resize request. `Present()` only
  accepts a valid `PresentedFrame`; raw GL handles are never retained or sent
  across threads. Width/height and pixels come from that same completed frame.
- Shutdown stops accepting, releases input, attempts WebSocket close, and forces
  remaining sockets closed after 500 ms. The network thread is joined before
  root/other GPU resources are destroyed.

## Endpoints and security

| Endpoint | Policy |
|---|---|
| `GET /`, `/app.js`, `/style.css` | Public static login/client assets |
| `GET /healthz` | Public `{"ok":true}`; independent of render progress |
| `GET /api/version` | Authenticated when token configured; version/build/protocol/backend + counters |
| `POST /api/login` | Exact allowed Origin + bearer credential; creates HttpOnly cookie |
| WebSocket `/api/ws` | Exact allowed Origin + bearer header or login cookie |

Default bind: **127.0.0.1:8080**. Without explicit `allowedOrigins`, loopback
binds accept only `http://127.0.0.1:PORT`, `http://localhost:PORT`, and
`http://[::1]:PORT` (default port 80 is omitted). Missing, `null`, duplicate or
unlisted WebSocket Origins fail. No wildcard Origin or CORS permission is added.

Non-loopback binds require explicit allowed origins, plus either a token or
`allowUnauthenticatedPublicBind=true`. Tokens must be 16–128 characters from
`A–Z a–z 0–9 _ -`; generate random credentials, not memorable passwords.
Credentials are compared server-side and never logged or put in URLs. The login
cookie is `HttpOnly; SameSite=Strict; Path=/api` (also `Secure` for an allowed
HTTPS login Origin). Its value is the configured credential; it expires with the
browser session. Session IDs are separate random 128-bit hex identifiers from
Linux `getrandom()`, never authentication credentials.

This server speaks **plaintext HTTP/WS**. Loopback is the safe development
default. Use a trusted TLS tunnel/terminating proxy before exposing authenticated
traffic outside the host, and explicitly allow its HTTPS origin. This is not an
identity system or production Internet-facing service. Tokens rotate on restart.
Origin checking prevents browser cross-site access; it is not authentication
against non-browser clients. Security headers include `nosniff`, `no-referrer`,
`X-Frame-Options: DENY`, and a self-only script/style/connect CSP with no framing.

## Frames and bounds

Binary message header: **28 bytes, all integers big-endian**, followed immediately
by JPEG bytes. No base64 or JSON image payloads.

| Offset | Field |
|---:|---|
| 0 | u32 magic `0x524d4a50` (`RMJP`) |
| 4 | u16 version `1` |
| 6 | u16 message type `1` (JPEG) |
| 8 | u64 monotonically increasing root frame ID |
| 16 | u32 width |
| 20 | u32 height |
| 24 | u32 JPEG payload size |

RGBA8 readback reuses Phase 3's **single CPU vertical flip**. The encoder drops
alpha and consumes top-origin RGB rows; browser decoding does not flip again.
JPEG is lossy and does not replace the Phase 3 PNG golden test.

- Defaults: JPEG quality 80, capture ceiling 20 fps, max viewport 1920×1080,
  eight WebSocket clients. Configuration permits 1–30 fps, quality 1–100,
  dimensions <=2048 per axis, and 1–32 clients. Initial `Config.width/height`
  must fit the configured maxima. Port 0 is supported for ephemeral test servers.
- No viewers: no readback/JPEG encoding. The core may keep rendering normally.
- One global replaceable packet; per session **one in-flight write plus one
  replaceable latest pending frame**. Packet storage is shared and immutable.
  The encoder's owned destination grows only up to 16 MiB; it safely rejects
  overflow even after reallocation. Already-transmitting frames cannot be
  unsent; queued older frames are replaced by the latest available ID.
- Client must acknowledge a decoded/displayed frame with `frame_ack`. Only one
  unacknowledged frame is transmitted per client. Five-second ACK/write deadlines
  evict slow clients. At most 16 small control messages queue per session; overflow
  closes it. Socket send buffers are requested at 64 KiB (OS bookkeeping varies).
- Browser stores one active decode and one replaceable pending frame. It checks
  magic/version/type, exact length, dimensions, monotonic ID and decoded JPEG
  dimensions. Browser WebSocket output above 64 KiB triggers reconnect/recovery.
- Incoming messages: 8 KiB, JSON depth <=8, at most 1000 messages/second/session.
  HTTP headers/bodies each <=8 KiB, request deadline five seconds, TCP connections
  <=4×`maxClients`. WebSocket upgrades count toward the client limit while pending.

## Control protocol

Every JSON message has `v:1`, `type`, and a strictly increasing positive integer
`seq` <=2^53−1, scoped to that connection. New connections start a new sequence.
Frame IDs in browser ACKs are decimal **strings**, preserving the entire u64
range; non-browser clients may also send exact unsigned JSON integers.

```json
{"v":1,"seq":1,"type":"viewport","width":1200,"height":800,"devicePixelRatio":2}
{"v":1,"seq":2,"type":"focus","focused":true}
{"v":1,"seq":3,"type":"mouse_move","x":0.5,"y":0.4,"source":"mouse"}
{"v":1,"seq":4,"type":"mouse_button","button":"left","down":true}
{"v":1,"seq":5,"type":"text","text":"äöüÄÖÜß 🙂"}
{"v":1,"seq":6,"type":"frame_ack","frameId":"42"}
```

Additional types: `wheel` (`horizontal`, `vertical`, finite and within ±1000),
`key` (`key`, `down`, `repeat`, `mods`), `snapshot` (`x`, `y`, `keys`, `buttons`,
`mods`, `focused`), and `release_all`. `mods` contains booleans `ctrl`, `shift`,
`alt`, `super`. Key names use RenderModule's private vocabulary (`A`, `Digit0`,
`LeftCtrl`, `KeypadEnter`, etc.), not browser/native keycodes. Buttons are `left`,
`right`, `middle`, `extra1`, `extra2`; source is `mouse`, `touch`, or `pen`.
Malformed, oversized, stale-sequence or invalid-enum/UTF-8 messages close the
connection without logging their contents. Unknown fields grant no authority.

**Lease: FirstConnected.** The first authenticated, upgraded connection controls;
others only receive frames. When it disconnects, release-all occurs and the
oldest remaining live viewer is promoted. `allowMultipleViewers=false` rejects
additional upgrades. Authorization is enforced on the server, not the UI.
Server control messages include `welcome` (session/control), `lease`,
`view_only`, `input_reset`, and `viewport_accepted` (actual width/height).

Moves may be dropped on input-queue saturation. Button/wheel events reassert the
latest position reliably so a dropped move cannot misdirect interaction. Reliable overflow
never spins: it invokes Phase 4 emergency release-all and reports `input_reset`.
The client clears local held state and requires fresh focus. Accepted earlier
input may be explicitly canceled during this recovery; large text commits are
not transactional and should be retried by the user if recovery is reported.

## Browser mapping and resize

The focusable Pointer Events surface fits **the actual image content rectangle**,
not the whole letterboxed container. Only captured drags clamp out-of-image
positions. Pointer capture is released on up/cancel. Wheel conversion: 100 CSS
pixels, three lines, or 0.1 pages per logical unit; DOM sign is inverted. Page
scroll is prevented only for controller-owned interaction inside the image.

`KeyboardEvent.code` maps once to protocol keys; physical keys never generate
text. A real, tiny textarea captures committed `input`/composition/paste. Commits
are bounded to 16 KiB and split at UTF-8 boundaries into <=256-byte events. Ctrl/
Super modifiers are temporarily released around committed text on the server so
ImGui does not suppress clipboard paste as shortcut input; physical state is
restored through ordered Phase 4 events. IME preedit stays in the browser.
Fonts still determine visible glyph coverage; UTF-8 storage does not add fonts.

Blur, visibility loss, actual control-element blur, lease loss and disconnect
clear input. Server disconnect handling is independent of final browser keyups.
Snapshots every 750 ms reconcile state rather than replace normal edge events.
Reconnect backoff is 250 ms, 500 ms, 1 s, 2 s, 4 s, then 8 s maximum. New session,
lease, viewport and snapshot are established; old authority is never assumed.

`ResizeObserver` requests are debounced 150 ms. CSS dimensions must be integers
1–16384, DPR finite in [0.25,8]. The server **does not multiply by DPR**: it uniformly
fits CSS dimensions into configured maxima, flooring to pixels (minimum one).
Only the controller may resize. Unconsumed requests are canceled when its lease
ends. Requests coalesce before transactional root resize; `viewport_accepted` reports observed completed-root dimensions, not an
uncommitted allocation. Frame headers always describe their own JPEG. A prior
in-flight image may finish during resize; mapping follows the displayed image.

## Diagnostics and tests

Authenticated `/api/version` exposes connected sessions/controller presence,
rendered/encoded/dropped frames, WebSocket payload bytes, accepted/rejected input,
queue-full count, cumulative readback/encode/pack microseconds and current viewport.
Drop counts include global/per-viewer replacement (not unique lost frame IDs).
No full metrics system or adaptive scheduling is implemented.

C++ tests cover HTTP/auth/origin/limits, controller denial/promotion, saturation,
rapid resize mailboxes/connect-disconnect, slow-client latest-frame behavior and
bounded shutdown; JPEG tests decode orientation, reject expired root frames and
force output-cap failures before/after buffer growth. Readback/codec failures stop
the render loop with diagnostics rather than publish partial packets.
Chromium E2E checks real ImGui/Canvas/View3D pixels, clicks, orbit/zoom, Unicode,
clipboard paste, synthetic CJK composition, capture, resizing, viewers, blur and
reconnect without a final keyup. Synthetic composition is not OS-IME coverage.
Artifacts include `browser.png`/`failure.png`, fixture state, server log and
`metrics.json`. Firefox and real OS/mobile IMEs remain unverified in this phase.

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
Phase 4 queue/input and Desktop presentation regressions pass. Server, JPEG and
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
NVIDIA hardware, Firefox and real OS IME remain unverified. These counts describe
the Phase 5 checkpoint; Phase 6 validation is documented separately.

References: [Beast async server example](https://github.com/boostorg/beast/blob/boost-1.83.0/example/websocket/server/async/websocket_server_async.cpp),
[write ownership](https://www.boost.org/doc/libs/1_83_0/libs/beast/doc/html/beast/ref/boost__beast__websocket__stream/async_write.html),
[libjpeg API](https://github.com/libjpeg-turbo/libjpeg-turbo/blob/main/doc/libjpeg.txt),
[stock memory destination ownership](https://github.com/libjpeg-turbo/libjpeg-turbo/blob/2.1.5.1/jdatadst.c)
(the custom destination avoids its finish-only publication of grown buffers).
