# Phase 7 — H.264 WebRTC video

`RootFramebuffer → VideoCapture → Phase 6 VideoPipeline → EncodedFrame →
per-viewer libdatachannel H264RtpPacketizer → DTLS/SRTP → browser <video>`.

Input, authentication, controller election and viewport requests **remain on the
Phase 5 WebSocket**. No DataChannel, audio, hardware encoding, PBO, adaptation,
simulcast or SFU is created. **Phase 8 has not started.** Phase 6 public interfaces
and encoding/conversion implementation are unchanged.

## Build and select

In addition to the [Web](WEB_PROTOCOL.md) and [Video](VIDEO_PIPELINE.md) dependencies,
install CMake >=3.21, OpenSSL development files and libnice >=0.1.19 development
files (including GLib/GIO). Tested: libnice 0.1.21, OpenSSL 3.0.13, Linux x86-64.

```sh
cmake -S . -B build-rtc -DCMAKE_BUILD_TYPE=Release \
  -DRENDER_MODULE_ENABLE_DESKTOP=OFF -DRENDER_MODULE_ENABLE_HEADLESS=ON \
  -DRENDER_MODULE_ENABLE_WEB=ON -DRENDER_MODULE_ENABLE_VIDEO=ON \
  -DRENDER_MODULE_ENABLE_OPENH264=ON -DRENDER_MODULE_ENABLE_WEBRTC=ON
cmake --build build-rtc --parallel
export RENDER_MODULE_WEB_TOKEN="$(openssl rand -hex 16)"
env -u DISPLAY -u WAYLAND_DISPLAY build-rtc/Release/bin/RenderModule3DDemo \
  --render-backend web --headless-context native-egl --web-transport webrtc
```

Or set `config.backend = Backend::Web` and
`config.web.transport = WebTransport::WebRtc` before `RenderModule::Init(config)`.
The default remains `WebTransport::JpegWebSocket`; `--web-transport jpeg` selects
that explicitly. These are separate modes, not simultaneous video transports.
WebRTC requires even initial dimensions >=16, maxima <=1920×1080 and 1–30 fps.
Viewport requests are fitted to the maxima, floored to even dimensions, minimum
16 per axis. This avoids a cropped-video/root-input coordinate mismatch.
Capture defaults to 20 fps; render cadence is independent. No viewers means no
capture. No automatic downgrade to JPEG, resolution ladder or bitrate adaptation.

`WEBRTC=OFF` neither fetches nor links libdatachannel. JPEG-only Web builds do not
need VIDEO or OpenH264. `RenderModuleWebRtc` is a private adapter DSO; installed
RenderModule headers expose no libdatachannel types. See [licenses](third_party/WEBRTC_NOTICE.md).

## Dependency and ICE policy

libdatachannel **v0.24.5**, immutable commit
`443f6934d9007eb7076ab7825ba330f355fcbead`, media enabled, upstream WebSockets and
examples/tests disabled. ICE uses **libnice**, DTLS uses **OpenSSL**, SRTP uses
pinned bundled libsrtp. usrsctp is an upstream build dependency but no SCTP
application m-line or DataChannel is negotiated.

A source-hash-checked build-tree patch in `cmake/LibDataChannelNiceRelay.cmake`
sets libnice's `force-relay` property for `TransportPolicy::Relay`. Upstream only
filters advertised candidates and can otherwise select a host pair! The checkout
stays pristine; the modified MPL-covered source is installed with its license.

```cpp
config.web.iceServers = {
    {"stun:stun.example.org:3478", "", ""},
    {"turn:turn.example.org:3478?transport=udp", username, credential},
    {"turn:turn.example.org:3478?transport=tcp", username, credential}
};
config.web.iceRelayOnly = true; // Requires a TURN entry; applies to both peers.
// config.web.iceTcp = true;   // Optional ICE-TCP; distinct from TURN-over-TCP.
```

No public STUN service is hardcoded. Empty servers select directly routed host
connectivity. Up to eight URIs, 512 bytes each; separate credentials <=256 bytes.
The selected pair's candidate type and ICE state are exposed, not its addresses.
**`turns:` and `?transport=tls` are rejected.** libnice 0.1.21's `TURN_TLS` enum
does not implement actual TLS in RFC5245 mode; accepting it would silently use
plain TCP. OpenSSL DTLS/SRTP does not fix that separate TURN transport limitation.
Use UDP/TCP TURN or a separately secured deployment; do not claim TURN/TLS support.

Use HTTPS/WSS via a trusted proxy outside loopback; signaling carries credentials
and authenticates the DTLS fingerprint. Browser capability APIs also need a secure
context (localhost is suitable). Server logs never print SDP, candidates, passwords
or tokens; upstream logging is disabled. Keep external libnice debug tracing off
unless deliberately diagnosing a private test environment.

## Negotiation and media contract

One server-offered, send-only video m-line, MID `video`, H.264 payload type **96**.
Exact offered fmtp:

```text
profile-level-id=42c028;packetization-mode=1;level-asymmetry-allowed=1
```

This is a **Level 4.0 ceiling**, not a claim that every SPS has a fixed level.
OpenH264 chooses the actual SPS level; the adapter validates Constrained Baseline,
level <=4.0, complete Annex-B access units, SPS/PPS with every IDR, and <=2 MiB/AU.
Native tests verify the actual encoder at 1280×720, 1600×900 and 1920×1080.
Default encoder bitrate remains 6 Mbps, bounded by Phase 6's 12 Mbps maximum.

Both tested browsers default their answer to `42e01f` (equivalent Constrained
Baseline, Level 3.1). This alone **does not permit 1080p**. The client queries
`MediaCapabilities.decodingInfo({type:'webrtc', video:{contentType:
'video/H264;packetization-mode=1;profile-level-id=42e028', width:1920,
height:1080, bitrate:12000000, framerate:30}})`. Only a supported result adds the
RFC 6184 receive declaration `max-recv-level=e028` to the signaled answer.
Native codec/profile and ICE/DTLS fields remain unchanged. Firefox drops unknown
fmtp fields during local SDP serialization, so the declaration is attached on the
signaling wire. The server requires Level >=4.0 or that explicit higher receive
level; `level-asymmetry-allowed=1` alone is insufficient. An unavailable/negative
capability query fails visibly; use the explicit JPEG mode on older browsers.
No SPS rewriting or fictitious decoder capability is used.

The browser uses `<video autoplay playsinline muted>`, with a manual Play button
if autoplay fails. Mapping follows `videoWidth/videoHeight`, not CSS box dimensions.
Ordinary root resizes issue fresh SPS/PPS/IDR on the same PeerConnection, without
SDP renegotiation. `getStats()` samples once/second for diagnostics only: received,
decoded/dropped frames, fps, bytes/packets/loss, jitter/buffer delay, RTT and candidate
types when available. Missing browser fields are omitted, not synthesized.

## RTP, feedback, fanout and bounds

- 90 kHz clock, from original nonnegative monotonic microsecond PTS:
  `base + uint32_t((ptsUs/1000000)*90000 + (ptsUs%1000000)*9/100)`.
  Flooring is per original PTS, not accumulated frame increments; wrap is natural.
- Each client has its own PeerConnection, SSRC, random initial sequence/timestamp
  base, packetizer, Sender Report state and **1024-packet NACK history**. SSRCs use
  a process-random starting counter, skip zero, and are not recycled on reconnect
  before 32-bit wrap. SPS/PPS are in-band. Library FU-A packetization uses a 1160-byte
  fragment limit and 1280-byte configured MTU. No custom H.264 packetizer or RTCP SR.
- Handler chain: H264RtpPacketizer → RtcpSrReporter → bounded RtcpNackResponder
  wrapper → PliHandler → RembHandler → validation/metrics. Incoming RTCP traverses
  it in reverse; length/compound validation precedes upstream feedback parsing.
  RTCP is <=4096 bytes; NACK <=64 fields; feedback budget <=2048 requests/second.
- Ready-track and PLI requests pass through **one VideoStreamController** to
  `VideoPipeline::ForceKeyframe()`, coalesced at 200 ms. A viewer awaiting an IDR
  retries requests while rejecting dependent frames, so coalescing cannot strand it.
- REMB records bitrate/time only, never calls `SetBitrate()`. Library pacing was
  evaluated and **deferred**: its unbounded queue conflicts with interactive latency.
- One encoder feeds a fanout hub. One bounded-size AU copy releases the Phase 6
  pool lease; immutable bytes are shared between viewers. Per viewer: one active
  send + one pending AU. Overflow invalidates the reference chain; wait for/request
  a fresh IDR rather than replacing a dependent P frame and continuing blindly.
  Each sender has its own worker; no viewer blocks render/capture or other senders.
- Signaling/session mutations and close are owned by the WebSocket thread. Hub
  failures only mark sessions failed; they do not race signaling teardown. Callback
  captures are weak; shutdown wakes/joins senders before closing peers. Failed
  negotiation (15 seconds), disconnected ICE (3 seconds), or terminal errors close
  the owning WebSocket and invoke the existing controller/input release-all path.
- libnice media sockets are nonblocking; UDP/kernel buffers remain OS-managed.
  TURN/TCP still has transport head-of-line behavior; the application does not
  promise Internet latency bounds or manage a congestion controller.

Authenticated `/api/version` adds `webrtc_sessions`, `webrtc_connected`, cumulative
`webrtc_failed`, active-client RTP/NACK/retransmit/PLI/submitted/rejected totals,
per-client SSRC/ICE/queue/REMB details, and a separate encoder metrics object.
Traffic totals are **active-client snapshots**, so disconnects can lower totals.
RTP bytes are measured before SRTP/IP/TURN overhead and represent send attempts,
not wire-rate accounting; retransmissions are included. Test drops are separate.

## Verification

See [test recipes](tests/webrtc/README.md). Chromium 153.0.8010.12 and Firefox 155.0
(with Mozilla-manifest-verified Cisco OpenH264 GMP 2.6.0) both decoded real video,
passed ImGui/NanoVG/Magnum pixel checks, click/Unicode/orbit input, repeated reconnect
and held-key release, and **1280×720 → 1600×900 → 1920×1080 → 1280×720 on one PC**.
Two viewers had distinct SSRCs but one ~20 fps encoder. Viewer input was rejected.
A separate 400 ms slow-sender test exercised frame rejection/IDR recovery while the
controller remained >=15 fps and each pending queue remained <=1 (66 rejected
slow-viewer frames in the final two-viewer snapshot).

Loss injection is test-build-only, deterministic **1 or 3 initial packets per 100**,
after NACK storage and before SRTP send; retransmissions are not dropped. It is not
random netem/WAN loss. Both full Chromium tests recovered with continued decoding:

| Scenario | NACK messages | Retransmitted packets | PLIs | Injected drops |
|---|---:|---:|---:|---:|
| 0% | 0 | 0 | 0 | 0 |
| 1% | 3 | 3 | 0 | 3 |
| 3% | 4 | 12 | 3 | 14 |

Counts are the controller snapshot before reconnect, not all historical peers.
Forced-relay coturn tests pass over UDP and TCP: server-selected local candidate
and browser local/remote selected candidates are **relay**. These are local Docker
bridge tests, not claims about arbitrary NAT/firewall deployments. TLS is untested
and deliberately unsupported by this backend.

Representative 3-second, two-viewer static-UI samples at 1280×720, Ryzen 9 9950X,
Mesa llvmpipe: ~20 encoder fps, ~0.152 Mbps aggregate RTP, server CPU 33.6–36.3%
of one logical CPU (browser CPU excluded). Loss0 mean readback/conversion/encode:
0.657/0.253/2.559 ms. The artificial slow viewer raises aggregate RTP to 1.332 Mbps
because shared IDR recovery is frequent; it does not create a second encoder. Static UI is not a high-motion/CBR benchmark; no end-to-end
input latency measurement is claimed. Artifacts: `build/phase2/test-artifacts/webrtc-*`.

Final regression results: combined **42/42**, headless/Web/video/WebRTC **36/36**,
JPEG-only **17/17**, video **20/20**, plain headless **12/12**, Desktop **7/7**,
converter-only **3/3**. The six existing CPU-video ASan tests also pass three
repetitions with leak detection (this is not WebRTC-wide sanitizer coverage).
All six example targets build. The Phase 3 full-root PNG
comparison remains mean=0/max=0. Installed core/video consumers and the complete
Chromium E2E against a separately installed **BUILD_TESTS=OFF** library pass.
JPEG-only/Desktop dependency checks show no WebRTC linkage. `git diff --check`
passes. Logs are in `build/phase7-verification/`; browser/relay artifacts include
both two-viewer and pre-reconnect snapshots. TSan cleanliness and WAN/long-duration
soak coverage are not claimed.

References: [libdatachannel pin](https://github.com/paullouisageneau/libdatachannel/tree/443f6934d9007eb7076ab7825ba330f355fcbead),
[RFC 6184](https://www.rfc-editor.org/rfc/rfc6184.html#section-8.1),
[Media Capabilities](https://www.w3.org/TR/media-capabilities/),
[libnice force-relay](https://libnice.freedesktop.org/libnice/NiceAgent.html),
[libnice 0.1.21 TURN implementation](https://github.com/libnice/libnice/blob/0.1.21/agent/agent.c),
[libnice nonblocking TCP sends](https://github.com/libnice/libnice/blob/0.1.21/socket/tcp-bsd.c).
