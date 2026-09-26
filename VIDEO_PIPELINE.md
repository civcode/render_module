# Phase 6 — software realtime video

**Transport boundary: owned H.264 access units, not networking.** The optional
[Phase 7 adapter](WEBRTC.md) consumes this unchanged interface for WebRTC media.
JPEG/WebSocket remains the explicit diagnostic transport. PBOs, hardware encoders
and congestion adaptation are not implemented.

```text
RootFramebuffer → synchronous CPU Frame Capture (render thread, one flip)
                              /                 \
                      JPEG prototype        VideoFrame (owned RGBA)
                                                    ↓ latest raw slot
                                           encoder worker: libyuv
                                                    ↓ I420
                                             IVideoEncoder
                                                    ↓ OpenH264Encoder
                                            EncodedFrame (Annex-B)
                                                    ↓
                                            [optional Phase 7 WebRTC adapter]
```

## Build and dependencies

```sh
cmake -S . -B build-video -DCMAKE_BUILD_TYPE=Release \
  -DRENDER_MODULE_ENABLE_HEADLESS=ON -DRENDER_MODULE_ENABLE_DESKTOP=OFF \
  -DRENDER_MODULE_ENABLE_VIDEO=ON -DRENDER_MODULE_ENABLE_OPENH264=ON \
  -DRENDER_MODULE_BUILD_TESTS=ON
cmake --build build-video --parallel 4
env -u DISPLAY -u WAYLAND_DISPLAY ctest --test-dir build-video -L video --output-on-failure
```

`RENDER_MODULE_ENABLE_VIDEO` defaults OFF. `RENDER_MODULE_ENABLE_OPENH264`
selects the codec only when video is enabled; disabling it leaves conversion,
frame types and the pipeline interface available (the OpenH264 factory returns
null). Desktop, plain Headless and Web/JPEG-only builds fetch neither dependency.
Web and video can be independently enabled together.

Immutable source pins in `cmake/Video.cmake` (OpenH264 archive verified by SHA-256;
libyuv checked out by full Git object ID because Gitiles archive metadata varies):

| Dependency | Tested version / immutable revision |
|---|---|
| OpenH264 | **2.6.0**, `652bdb7719f30b52b08e506645a7322ff1b2cc6f` |
| libyuv | **1972**, `41a6e684a68950f07912b7e6f57ba3fa2e10fb65` |

OpenH264 builds with its upstream GNU Make build, out of source. The adapter
currently supports **native Linux** builds; cross-compilation is rejected rather
than silently choosing host tools. NASM is optional, strongly recommended for
optimized x86 encoding (`RENDER_MODULE_NASM` can specify its path); otherwise
OpenH264 uses its scalar path. libyuv retains runtime SIMD dispatch in normal
builds. Neither encoder executables nor FFmpeg are required by the library.

The separately exported **`RenderModule::Video`** shared library has no GL,
ImGui, Web, Boost or input dependency. Codec/converter archives link privately.
Installed headers expose only project/C++ types; installed builds carry the
upstream notices. See [VIDEO_NOTICE.md](third_party/VIDEO_NOTICE.md).

## API and ownership

Public API: `include/render_module/video.hpp`. Link `RenderModule::Video` when
using its types/functions, and `RenderModule::RenderModule` for root capture.

```cpp
auto video = std::make_shared<render_module::video::VideoPipeline>();
render_module::video::EncoderConfig settings; // 1280x720, 30 fps, 6 Mbps
if (!video->Configure(settings)) return 1;
// After RenderModule::Init(), on the render thread:
if (!RenderModule::SetVideoOutput(video)) return 1;
// A separate CPU consumer repeatedly calls video->TryReceive(frame).
// Consume frame.storage->data()[0 .. frame.size), then release the frame.
// Register/render the existing UI normally; no video-specific Canvas/View3D code.
```

A new pipeline should be used for each RenderModule Init/lifetime. Detach using
`SetVideoOutput(nullptr)`. `VideoCapture` is the sole render-side adapter; all
codec/conversion work occurs on one worker. A worker may outlive the GL context:
its submitted frames contain only owned CPU bytes, never borrowed GL handles.
Video failure detaches this optional output and leaves normal rendering running.

`VideoFrame` describes format, plane offsets/strides, dimensions, frame ID,
framebuffer generation, orientation, color metadata and signed microsecond PTS.
`RgbaFramePool` offers four preallocated slots. Its writable pointer is exclusive
producer access **only until submission**; submitted storage must not be mutated.
Acquire fails rather than allocate another buffer when every slot is retained.
`I420Converter` owns reusable storage; its returned view is for synchronous use
until the next conversion/configuration. The worker finishes Encode before reuse.

`EncodedFrame` owns a shared immutable lease from three preallocated codec output
buffers. **Only `frame.size` bytes are valid AU data**, not the entire backing
vector's size. Consumers must release leases promptly; retaining all three causes
backpressure, not another allocation. Retained frames remain readable after codec
destruction. Allocation occurs during configuration/dimension changes, not for
every capture/conversion/access-unit copy. Caller-retained buffers from old
configurations remain the caller's memory responsibility.

Both pools reserve slots under a mutex and preallocate shared-pointer control
blocks. A slot returns only after **all strong and weak references** are released;
retaining expired weak references therefore also causes bounded backpressure.
No reuse decision relies on `use_count()==1`: it provides neither synchronization
with former readers nor exclusion of new weak locks ([C++ ownership observers](https://eel.is/c++draft/util.smartptr.shared.obs)).
The control-block allocator keeps its arena alive through the final weak release,
even after the pool or codec has been destroyed.

## Pixels, color and orientation

- Input is **R G B A byte memory**, full-range conventional nonlinear SDR RGB,
  BT.709 primaries/transfer policy. Alpha is discarded. No HDR/EOTF transform.
- Conversion uses `libyuv::ARGBToI420Matrix(..., &kAbgrH709Constants, ...)`:
  libyuv's little-endian **ABGR** naming means RGBA byte memory. The explicit
  coefficients are **BT.709 limited range**, not the default BT.601 conversion.
- Output metadata and H.264 VUI signal primaries **1**, transfer **1**, matrix
  **1**, full-range flag **false**. Nominal Y range 16–235, Cb/Cr 16–240.
- I420 planes: Y `w×h`, U/V `ceil(w/2)×ceil(h/2)`. Each stride rounds up to 64
  bytes; offsets and strides are explicit. Conversion tests include padded and
  odd layouts. NV12 and VP8 are enum placeholders, not implemented codecs/formats.
- Capture reuses `ImagePresenter::ReadInto()` and its **one CPU row flip**.
  libyuv receives positive height and upright pixels: no second inversion.
- Encoded dimensions are even, **rounded down** by cropping only the final odd
  column/row. RootFramebuffer/JPEG dimensions are not modified. Encoder sizes
  must be 16–2048 on each axis; raw/converter layouts allow 1–2048. Oversized
  roots are rejected, not silently scaled/cropped to a different viewport.

The original ImagePresenter allocation/read API still serves JPEG and PNG. Video
uses its new caller-owned readback overload. **Simultaneously enabling JPEG and
video currently performs separate synchronous readbacks** at their respective
cadences. This deliberately avoids rewriting the Phase 5 presenter; the capture
implementation/state restoration/orientation are shared, not a cross-presenter
cache. No asynchronous readback was introduced.

## H.264 configuration and controls

OpenH264 receives I420 with CAMERA_VIDEO_REAL_TIME, LOW_COMPLEXITY,
**Baseline profile 66 with constrained-baseline flags verified in SPS**, CAVLC,
one spatial/temporal layer, one reference, one codec thread, single slice,
no LTR, no denoising, no prefix NAL/SSEI, and no B/reordered frames. Level selection
is automatic. Rate control is RC_BITRATE_MODE; frame skipping defaults ON
(deterministic codec tests turn it OFF). Periodic IDR defaults to 60 frames.

Default target: 30 fps / 6 Mbps, clamped between 250 kbps and 12 Mbps. FPS is the
codec's nominal rate-control rate, not a worker pacing clock; set the render
producer's `Config.fps` accordingly (30 in tests). Validation
also bounds FPS to 1–60, bitrate limits to 10 kbps–50 Mbps, and IDR interval to
1–3600. Invalid dimensions, metadata, codecs, formats and non-finite FPS fail.

`SetTargetBitrate()` applies OpenH264's ENCODER_OPTION_BITRATE/SBitrateInfo without
restarting. The effective value is queried back; pipeline metrics expose it.
`ForceKeyframe()` remains pending across backpressure/skips until an actual IDR
NAL is observed. Initialization, reset/recreation and successful configuration
force IDR. Keyframe truth comes from returned NAL type 5, not the request flag.

One `EncodedFrame` is one **Annex-B access unit**, possibly multiple NALs, including
all emitted SPS/PPS. `H264Format::AnnexB` is explicit. `NextAnnexBNal()` is an
allocation-free debug/test iterator recognizing three/four-byte start codes and
NAL types, not a full H.264 syntax decoder or RTP packetizer.

`VideoClock` uses `steady_clock`, with strictly increasing microseconds since
construction. Encoded PTS and frame IDs are preserved exactly. OpenH264's internal
millisecond timestamp is derived from PTS; that rounding does not alter the public
microsecond value. No RTP timestamp appears in this API. Duplicate/decreasing
IDs or PTS are rejected. Configuration changes preserve timestamp continuity;
a fresh encoder/pipeline starts a new stream.

## Queues, configuration and shutdown

`VideoPipeline` has one active encode, **one replaceable latest raw slot**, and
**one encoded output slot**. Slow encoding/consumption replaces only pending raw
images. It does **not** arbitrarily discard dependent H.264 P access units: a full
output slot blocks further encoding while producers keep replacing raw input.
Retained codec output leases also apply backpressure. This preserves the reference
chain without an unbounded queue or unnecessary IDRs.

Configure validates synchronously, switches the accepted dimensions/generation
immediately, and coalesces an asynchronous worker command. It discards pending
raw/output data; an already active old encode may finish but cannot publish across
the configuration epoch. The worker recreates the codec, resizes conversion/output
buffers, then resumes with IDR. Root generation is carried into every frame. Codec
initialization failures surface through metrics and reject submissions until a
new configuration. Previously received external leases cannot be recalled.

Submit never waits for conversion/encoding. Shutdown cancels pending work, wakes
the worker and joins it after at most its current codec/configuration operation;
no worker touches GL. `IVideoEncoder::Flush()` has no delayed frames to drain in
this synchronous no-reordering implementation. Pipeline Flush waits for submitted
work with an explicit timeout; consumers must continue draining output. It is not
an encoder reset and never silently discards a queued access unit.

Metrics are bounded counters/accumulators: submitted/encoded/dropped frames, bytes,
keyframes, errors, pending input/output depths, effective bitrate, and separate
readback/conversion/encode timing (mean/worst/sample count). Drop accounting includes
raw replacement, codec skips, configuration cancellation and pre-submit pool
exhaustion. No indefinite per-frame metrics history is retained.

## Tests and benchmarks

`video_config`, `video_color`, `video_pool`, `video_codec`, `video_queue` cover configuration,
strides/odd sizes, six colors, orientation, stable storage/zero warmed-up C++
allocations, SPS/PPS/IDR/P slices, timestamps, forced/periodic IDR, repeated
6→2→8 Mbps changes, strong/weak lease exhaustion, 5,000 concurrent pool handoffs,
rapid reconfiguration and shutdown.

When system **test-only libavcodec/libavutil** development files are available,
`video_roundtrip` decodes with FFmpeg's independent native H.264 decoder and checks
VUI plus RGB errors; it is not the OpenH264 decoder. Native library tests/builds
remain possible without this tooling. `video_root_benchmark` and `video_root_resize`
encode actual ImGui/ImPlot/NanoVG/View3D root frames, save `.h264`, `.png`, `.json`,
and independently decode/check every AU when that tooling is available. Fine
text/lines use lossy tolerances, not byte-identical encoded goldens.

`web_chromium_video` runs the unchanged browser/JPEG interactions while a test-only
CPU sink drains H.264; no H.264 goes onto WebSocket. Original Chromium tests still
run without video output. Artifacts live in `build*/test-artifacts/video/` and
`web-video/`.

For ASan, use a **separate build directory** with `RENDER_MODULE_VIDEO_ASAN=ON`.
Build `RenderModuleVideoTests`, then run:

```sh
ASAN_OPTIONS=detect_leaks=1 env -u DISPLAY -u WAYLAND_DISPLAY \
  ctest --test-dir build-video-asan -R '^video_(config|color|pool|codec|queue|roundtrip)$' \
  --repeat until-fail:3 --output-on-failure
```

This instruments the video library, libyuv and OpenH264 scalar paths (assembly
is disabled for coverage). The test decoder remains external tooling. Normal
Release builds exercise optimized SIMD. The benchmarks use 60 frames per size,
30 fps target and a static full UI; report mean/worst separately and retain the
first IDR in timing. This is not a high-motion or hardware-encoder benchmark.

## Phase 6 verification snapshot

Native x86-64 Linux, Ryzen 9 9950X, Mesa 25.2.8 llvmpipe; optimized Release codec
with NASM. Both display variables were unset for all headless/video/Web tests.
Desktop tests used Xvfb; `TMPDIR=/tmp` avoids the harness's nonexistent temp path.

| Configuration / suite | Passed |
|---|---:|
| Desktop + Headless + Web + video | 31/31 |
| Desktop only, video/Web OFF | 7/7 |
| Plain Headless, video/Web OFF | 12/12 |
| Headless + Web/JPEG, video OFF | 17/17 |
| Headless + video, Web OFF (fresh source pins) | 20/20 |
| Converter-only, OpenH264 OFF (CPU subset) | 3/3 |
| ASan + leak detection (CPU subset) | 6/6, three repetitions |

The combined suite includes eight video tests plus JPEG/video Chromium coexistence.
All six existing examples were built in each full configuration. Independent
libavcodec decoding passed both synthetic and actual UI streams; the synthetic
RGB mean error was 0.829 code values. The Phase 3 PNG golden had zero error;
Phase 4 input and Phase 5 browser tests pass. Clean fetching verified the Git
libyuv pin and OpenH264 archive checksum without video-source overrides. Installed
core-only and core+video consumers configured, linked and ran without source hints.
Video library dependency/symbol checks found no GL, Web, FFmpeg or exposed vendor
API dependency. `git diff --check` passed. An additional ThreadSanitizer pool run
was attempted, but its runtime exited before testing with `unexpected memory
mapping`; this is **not** a TSan-clean claim.

Final static-UI sample: 60 submitted/encoded frames per size, **zero drops**,
30 fps target, one IDR included. Timing is **mean / worst**, milliseconds:

| Size | Readback | RGBA→I420 | Encode | Mean AU bytes | Bitrate at 30 fps | UI RGB mean error |
|---|---:|---:|---:|---:|---:|---:|
| 1000×700 | 0.469 / 6.236 | 0.167 / 4.124 | 2.206 / 27.025 | 896 | 0.215 Mbps | 1.305 |
| 1280×720 | 0.274 / 3.357 | 0.144 / 1.192 | 1.305 / 6.979 | 1015 | 0.244 Mbps | 1.223 |
| 1920×1080 | 0.609 / 0.939 | 0.397 / 2.414 | 3.622 / 13.891 | 1607 | 0.386 Mbps | 1.089 |

These are a single static-UI run, with cold-start/scheduling outliers included,
not a prediction of high-motion output size or a monotonic scaling benchmark. The requested
6 Mbps is a rate-control target, not a requirement to pad a static stream to CBR.
Full JSON, PNG and Annex-B samples: `build/phase2/test-artifacts/video/`.

Key commands actually used, from the workspace parent (build directories retain
the backend flags above; `$display` was an allocated Xvfb display):

```sh
cmake -S render_module -B render_module/build/phase6-converter -DRENDER_MODULE_BUILD_EXAMPLES=ON
cmake --build render_module/build/phase6-converter --parallel 4
TMPDIR=/tmp DISPLAY=":$display" ctest --test-dir render_module/build/phase2 --output-on-failure
TMPDIR=/tmp DISPLAY=":$display" ctest --test-dir render_module/build/phase2-desktop-only --output-on-failure
env -u DISPLAY -u WAYLAND_DISPLAY ctest --test-dir render_module/build/phase6-converter --output-on-failure
env -u DISPLAY -u WAYLAND_DISPLAY ASAN_OPTIONS=detect_leaks=1 \
  ctest --test-dir render_module/build/phase6-asan \
  -R '^video_(config|color|pool|codec|queue|roundtrip)$' --repeat until-fail:3 --output-on-failure
cmake --install render_module/build/phase6-converter --prefix "$PWD/render_module/build/phase6-clean-installed"
cmake -S render_module/tests/installed_video -B render_module/build/phase6-clean-consumer \
  -DCMAKE_PREFIX_PATH="$PWD/render_module/build/phase6-clean-installed"
cmake --build render_module/build/phase6-clean-consumer --parallel 2
env -u DISPLAY -u WAYLAND_DISPLAY render_module/build/phase6-clean-consumer/InstalledCoreSmoke
env -u DISPLAY -u WAYLAND_DISPLAY render_module/build/phase6-clean-consumer/InstalledVideoSmoke
```

No tests require NVIDIA hardware; hardware encoders and cross-platform/cross-compiled
OpenH264 remain outside this phase. Phase 7 transport verification is in [WEBRTC.md](WEBRTC.md).

Primary references: [OpenH264 2.6 API](https://github.com/cisco/openh264/blob/v2.6.0/codec/api/wels/codec_api.h),
[encoder parameters/VUI](https://github.com/cisco/openh264/blob/v2.6.0/codec/api/wels/codec_app_def.h),
[libyuv format naming](https://chromium.googlesource.com/libyuv/libyuv/+/41a6e684a68950f07912b7e6f57ba3fa2e10fb65/docs/formats.md),
[explicit RGB matrices](https://chromium.googlesource.com/libyuv/libyuv/+/41a6e684a68950f07912b7e6f57ba3fa2e10fb65/include/libyuv/convert_from_argb.h),
[independent decoding API](https://ffmpeg.org/doxygen/6.1/decode__video_8c_source.html).
