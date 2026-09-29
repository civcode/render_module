# Phase 8 — DataChannel input/control v1

A session committed to the H.264 stream uses two **server-created** channels on its
PeerConnection, created before the offer. The browser never creates a channel.
Authentication, cookie/Origin validation, runtime stream control and offer/answer/ICE
remain on the persistent `/api/ws` connection. `welcome.inputTransport` /
`stream_changed.inputTransport` select `datachannel-v1` for H.264 or
`websocket-json-v1` for JPEG/PNG. H.264 sessions reject ordinary WS input and frame ACKs.
When switching back to JPEG/PNG the server disables DataChannel authority and releases
held state before WS input becomes authoritative, so there is no duplicate delivery.

| Label | Ordering | Reliability | Messages |
|---|---|---|---|
| `input-fast-v1` | unordered | `maxRetransmits=0`; no lifetime | absolute pointer motion only, client → server |
| `control-v1` | ordered | reliable; neither retransmit nor lifetime limit | all other input/control, both directions |

Both use DCEP subprotocol `render-module-input-v1`. Unexpected remote-created
channels, text messages, wrong-lane/direction messages, invalid packets and control
sequence violations fail the owning session. Losing either required channel fails
the session; control-channel loss releases input independently of final keyups.
There is one bundled application m-line, MID `0` (the pinned library's first unused
numeric MID), alongside video MID `video`. Audio/additional m-lines are rejected.
SCTP defaults, H.264, RTP/RTCP, ICE/TURN, Phase 4 queue/backend and Phase 6 encoder
interfaces/implementation are unchanged. No Phase 9, PBO, codec or adaptation work.

## Wire format

Every packet is one binary DataChannel message, <=512 bytes, with exact length:

| Offset | Type | Field |
|---:|---|---|
| 0 | u8 | major version, exactly 1 |
| 1 | u8 | message type below |
| 2 | u16 | payload bytes, excluding this header |
| 4 | u32 | sequence, separately incremented per lane |
| 8 | u64 | sender monotonic timestamp, microseconds |

All integers are **big-endian**; floats are IEEE-754 binary32, explicitly encoded
big-endian. No native struct layout, padding or host endianness is transmitted.
Booleans are exactly 0/1. Unknown types/keys, reserved snapshot bits, nonfinite or
out-of-range floats, invalid UTF-8 and trailing/truncated data are rejected before
input state mutation. The parser is allocation-free and leaves output unchanged
on failure. All following lengths are payload lengths (add 16 for packet length).

Abbreviations, in wire order:

- **P** (16 bytes): `f32 x, f32 y, u32 fastFence, u32 epoch`.
  Coordinates are normalized top-left, each in [0,1].
- **V** (8): `u16 CSS width, u16 CSS height, f32 DPR`.
  Sizes 1–16384, DPR [0.25,8]; DPR is not multiplied into viewport requests.
- **S** (36): `P, u8 source, u8 buttonMask, u8 modifierMask, bool focused,
  u8 keys[16]`. Source 0/1/2 = mouse/touch/pen. Button bits 0–4 =
  left/right/middle/extra1/extra2. Modifier bits 0–3 = Ctrl/Shift/Alt/Super.
  Key ID n uses bit `(n % 8)` of byte `(n / 8)`; ID 0 and unused IDs are reserved.
  Unfocused snapshots clear all held state. Epoch is a server-issued control/resync
  generation; stale snapshots cannot restore a former controller's keys/buttons.

| ID | Message | Direction | Payload | Bytes |
|---:|---|---|---|---:|
| 1 | ClientHello | C→S | `u16 minor, u32 capabilities, V, S` | 50 |
| 2 | PointerMove | C→S, fast only | `u32 fastSequence, f32 x, f32 y, u8 source, u32 epoch` | 17 |
| 3 | MouseButton | C→S | `u8 button, bool down, P, u8 source` | 19 |
| 4 | MouseWheel | C→S | `f32 horizontal, f32 vertical, P, u8 source` | 25 |
| 5 | Key | C→S | `u16 keyID, bool down, u8 modifiers, u32 epoch` | 8 |
| 6 | TextUtf8 | C→S | `u32 epoch, UTF-8 bytes` | 5–260 |
| 7 | Focus | C→S | `bool focused, u32 epoch` | 5 |
| 8 | Snapshot | C→S | `S` | 36 |
| 9 | ViewportRequest | C→S | `V` | 8 |
| 10 | AcquireControl | C→S | empty | 0 |
| 11 | ReleaseControl | C→S | empty | 0 |
| 12 | BrowserStats | C→S | `u32 fastBuffered, u32 controlBuffered` | 8 |
| 13,14 | Ping, Pong | either, control | `u64 echoUs` | 8 |
| 128 | ServerHello | S→C | `u16 minor, u32 capabilities, u32 epoch, bool controller, u16 maxPacket` | 13 |
| 129 | ViewportAccepted | S→C | `u16 width, u16 height` | 4 |
| 130 | ControlState | S→C | `bool controller, bool inputEnabled, u32 epoch` | 6 |
| 131 | ResyncRequired | S→C | `u32 epoch` | 4 |
| 132 | StreamState | S→C | `u16 state`: 0 waiting, 1 ready, 2 terminal | 2 |
| 133 | Error | S→C | `u16 code`, reserved codes 1–8 | 2 |

Minor is currently 0. Capability bit 0 (fences/snapshots) is required; additional
client capability bits are ignored, and ServerHello selects the supported set.
PointerMove's payload sequence must equal its header sequence. Wheel values are
finite [-1000,1000]. Text has **1–256 bytes**, strict Unicode scalar UTF-8: no NUL,
overlong encoding, surrogate or code point above U+10FFFF. Current terminal failures
use the existing bounded WS error/close path; Error and terminal StreamState are
reserved wire vocabulary, not a promise of delivery after a channel fails.

The explicit stable IDs and physical DOM code mapping have one source of truth:
[`web/protocol_keys.mjs`](web/protocol_keys.mjs). CMake generates the private C++
lookup from that table, never from RenderKey/ImGui/GLFW enum ordinals. IDs 1–26 are
A–Z, 27–36 digits, 37–51 navigation, 52–63 F1–F12, 64–71 left/right modifiers,
72–82 punctuation, 83–88 locks/system keys, 89–105 keypad. IDs must never be renumbered.
Physical autorepeat does not send additional key-down edges. Committed text remains
separate, using the existing hidden textarea, composition-end/input handling and
UTF-8 chunking. IME preedit stays local; no new clipboard synchronization is added.

## Handshake, authority and ordering

1. Control opens → ClientHello with viewport, capabilities and initial state.
2. ServerHello selects v1/capabilities, controller status and epoch. The hello's
   initial state is not injected into Phase 4.
3. Browser clears local held state and sends a fresh authoritative Snapshot.
4. Server enqueues it through the existing Phase 4 queue, then ControlState enables
   input. Ordinary edges before this acknowledgment are ignored, not replayed.

FirstConnected remains the server policy. ReleaseControl withdraws that session
from automatic election; Request control opts it back in but never steals an
active lease. Every grant/regrant requires a new epoch and fresh snapshot. Viewers
may negotiate video and exchange diagnostics but cannot inject input or resize.
Disconnect, focus loss, lease loss, channel failure and shutdown invoke the existing
ReleaseAll cancellation barrier. Focus regain sends a snapshot, not just a focus edge.
Pending viewport requests are canceled when the owning controller relinquishes.

Sequences use `delta = uint32_t(new - old)`, newer iff `0 < delta < 2^31`, including
32-bit wrap. Motion must be newer than the per-session accepted floor and match
the current epoch. Reliable button/wheel messages advance the floor and reassert
source/absolute position **before** enqueuing the edge. Snapshots also advance the
floor. Older reliable fences never move it backward. There is no assumed arrival
order between channels. Late, duplicate or reordered old moves are discarded.
Phase 4 assigns its own queue sequence; independent wire sequences are not passed
as its single `sourceSequence`.

## Backpressure and ownership

- Browser fast path: **one** latest unsent motion. Send only while `bufferedAmount`
  <=4096; low threshold 1024, plus animation-loop retry. No accumulated move list.
  A send may exceed the threshold by one 33-byte packet. Reliable fences include
  generated but coalesced/unsent motion, so those moves cannot rewind an edge.
- Browser control: at most 64 queued packets (<=32 KiB), and <=16 KiB submitted to
  the browser's visible transport buffer; low threshold 4096. Overflow fails the
  session rather than silently losing a key/button release. Resync clears unsent
  old commands. Every send observes `pc.sctp.maxMessageSize` independently of the
  application's 512-byte limit.
- Server library callbacks validate then place at most 64 reliable packets plus
  one latest fast packet in an inbox. Limits: 1000 control and 2000 fast messages
  per second/session. Overflow/invalid input fails only that peer. Callback captures
  are weak; no raw session or ImGui/GL access crosses into callback threads.
- The WS owner thread drains control before latest motion, checks authority and
  enqueues existing Phase 4 events. No input worker or alternate camera/UI path.
  Any Phase 4 enqueue failure causes emergency ReleaseAll; Full increments its
  counter, disables ordinary input, advances the epoch and sends ResyncRequired.
  Only a matching authoritative snapshot re-enables input; no edge is silently lost.
- Server control replies use a 64 KiB visible-buffer cap and negotiated message
  size. libdatachannel `send()==false` is not retried: it can mean already buffered.
  These are application/visible-buffer bounds, not claims about all SCTP/kernel
  allocations. No global SCTP buffer or congestion tuning is introduced.
- ClientHello deadline 15 s from session creation; fresh-snapshot deadline 5 s;
  after hello, 5 s without reliable client activity closes the session. Ping/Pong
  keeps idle channels alive. Browser also detects 5 s without server control traffic.
  Existing ICE-disconnect and WS shutdown limits remain in force.

The remote browser channel is already `open` during `datachannel`, before its later
`open` event. Startup is idempotent, preventing duplicate ClientHello.

## Diagnostics and verification

Authenticated `/api/version` per-client fields include channel-open/input-enabled
state, accepted/rejected/processed messages, stale/coalesced motion, queue-full and
resync counts, invalid packets, visible browser/server buffering, negotiated maximum
message size and `input_rtt_ms`. Existing video/encoder metrics remain separate.
Browser `#diagnostics.dataset.input` contains transport, queue/buffer counts and RTT.
Ping echoes the sender's own monotonic token: RTT includes application queuing and
server polling; **no cross-clock one-way or input-to-photon latency is inferred**.
Active-client counters disappear with their session, as with Phase 7 traffic metrics.

Native and JS tests share byte-for-byte vectors in both directions. Native tests
cover malformed fields/UTF-8/lengths, wrong lane/direction, key mapping, wrap, fences,
controller transfer, saturation and 100,000 deterministic random/structured parser
mutations. ASan+UBSan runs cover that CPU parser/state/Phase 4 queue test, not the
entire WebRTC stack. Browser tests exercise real decoded H.264 and DataChannel
click/text/orbit/resize, two viewers, forged viewer input/resize, explicit lease
transfer, control-channel loss, reconnect, live H.264/image switching and H.264 signaling-only WS traffic.

The browser harness independently drops 1%/3% of fast **application send attempts**
and delays one move at a time by 100 ms. It verifies continued reliable clicks and
stale-motion rejection. This is separate from the existing RTP loss test; neither
is a claim about random WAN loss. Fake-channel tests verify one-slot coalescing and
reliable saturation; no synthetic client queue limits are substituted in production.
See [test recipes](tests/webrtc/README.md) and the retained media tests in [WEBRTC.md](WEBRTC.md).
Real OS/mobile IME, WAN congestion, long soak, TSan cleanliness and end-to-end latency
remain outside the verified scope.

Sources: [RFC 8831](https://www.rfc-editor.org/rfc/rfc8831.html),
[RFC 8832 DCEP](https://www.rfc-editor.org/rfc/rfc8832.html),
[W3C channel announcement/buffering](https://www.w3.org/TR/webrtc/#announcing-a-data-channel-instance),
[pinned libdatachannel API/source](https://github.com/paullouisageneau/libdatachannel/tree/443f6934d9007eb7076ab7825ba330f355fcbead).
