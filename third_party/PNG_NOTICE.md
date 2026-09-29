# PNG encoding and test decoding notices

RenderModule's production PNG path uses two pinned encoders behind one internal
runtime-selected interface.

## FPNG fallback encoder

Repository: https://github.com/richgel999/fpng  
Pinned commit: `925796543b9d26b8edfcdcecd94c1dac280f29fc`

RenderModule uses `src/fpng.cpp` and `src/fpng.h` as the portable PNG encoder
fallback. The upstream project explicitly places these files in the public
domain under the Unlicense. The fallback is built with `FPNG_NO_SSE=1` and
`FPNG_USE_UNALIGNED_LOADS=0` so it does not inherit the CPU requirements of
the accelerated backend. FPNG output contains the private ancillary `fdEC`
chunk; normal PNG readers ignore ancillary chunks they do not recognize.

## FPNGE accelerated encoder

Repository: https://github.com/veluca93/fpnge  
Pinned commit: `097ebccf3d3db443d9a8f238b1deed010a82f961`

RenderModule uses FPNGE only as an optional x86-64 accelerated encoder. The
upstream code is licensed under Apache License 2.0; the complete license text is
installed alongside this notice as `FPNGE_LICENSE.txt`.

The FPNGE object is compiled separately with AVX2/PCLMUL enabled and IPO/LTO
disabled. Runtime CPU checks happen in baseline code before any FPNGE function
is entered. Unsupported CPUs, unsupported platforms, and builds configured with
`RENDER_MODULE_ENABLE_FPNGE=OFF` use FPNG instead.

FPNGE owns no application state. RenderModule supplies a reusable caller-owned
scratch buffer sized from FPNGE's documented allocation helper/formula, validates
dimensions and output size, and checks a red zone in debug/sanitizer builds.
If an FPNGE frame fails to encode or exceeds the configured Web frame limit,
RenderModule attempts FPNG once before dropping the frame.

## Common behavior

Both encoders receive the same tightly packed RGBA8, top-origin image produced by
`ImagePresenter::Read()`. The single GL bottom-origin to CPU top-origin flip
happens before PNG encoding. WebSocket PNG framing, MIME type, alpha semantics,
and the encoded payload limit are unchanged by backend selection.

The runtime backend can be forced for diagnostics and rollback with:

```text
RENDER_MODULE_PNG_ENCODER=auto
RENDER_MODULE_PNG_ENCODER=fpnge
RENDER_MODULE_PNG_ENCODER=fpng
```

FPNGE compression levels 1 through 5 can be selected for benchmarking with
`RENDER_MODULE_FPNGE_LEVEL`; the default is level 4.

## Independent PNG test decoder

RenderModule keeps NanoVG's bundled `example/stb_image.h` as an independent
decoder for regression tests and trusted generated golden images. It is not the
production PNG encoder. NanoVG remains pinned to commit
`ce3bf745eb2d2dbc14a50bf2446783f691ac4353`.
