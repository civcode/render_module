# FPNGE AArch64 / NEON Fork Plan

## Purpose

This document defines the engineering plan for a temporary fork of
[FPNGE](https://github.com/veluca93/fpnge) that adds a production-quality AArch64
implementation based on ARM NEON / Advanced SIMD while preserving the existing x86
implementations.

The intended end state is not a permanent RenderModule-specific fork. The work should be
structured so it can be proposed upstream to FPNGE with minimal project-specific baggage.
RenderModule may pin the fork while the work is under development or review. If the changes
are accepted upstream, RenderModule should return to an upstream FPNGE pin.

At the time this plan was written, upstream FPNGE is centered on x86 SIMD: it builds around
SSE4.1 and optionally AVX2, with x86-specific checksum acceleration. RenderModule therefore
uses FPNGE only on supported x86-64 builds and falls back to FPNG elsewhere. On AArch64,
including Raspberry Pi-class systems, this leaves PNG encoding on the slower fallback path.

## Objectives

The fork has four primary objectives:

1. Add native AArch64/NEON support without changing the public FPNGE API.
2. Preserve PNG correctness and FPNGE behavior across architectures.
3. Achieve a meaningful speedup over RenderModule's current FPNG fallback on ARM64.
4. Keep the implementation reviewable and maintainable enough to be a realistic upstream
   contribution.

The project should optimize for maintainability before chasing isolated benchmark wins.
Architecture-specific code should be narrow, explicit, tested independently, and separated
from the higher-level PNG algorithm wherever practical.

## Non-goals

The first ARM64 effort should not:

- redesign the FPNGE compression algorithm;
- add RenderModule-specific APIs or image assumptions to FPNGE;
- specialize the library only for RGBA8 even though that is RenderModule's main use case;
- remove or materially rewrite the existing SSE4.1 or AVX2 implementations;
- require sse2neon or another translation library as a permanent runtime/build dependency;
- make optional ARM extensions such as CRC32 or PMULL mandatory for all AArch64 users;
- change FPNGE's public API unless an upstream review establishes a compelling need;
- optimize RenderModule readback, vertical flipping, packet construction, or WebSocket
  delivery as part of this fork.

Those RenderModule pipeline costs should be measured separately after ARM PNG encoding is
no longer the obvious bottleneck.

## Compatibility contract

The ARM implementation should preserve the existing FPNGE API:

```cpp
FPNGEEncode(...)
FPNGEFillOptions(...)
FPNGEOutputAllocSize(...)
```

It should support the same externally documented combinations as upstream, including:

- 8-bit and 16-bit samples;
- 1, 2, 3, and 4 channels;
- RGB and BGR channel order;
- all supported predictors and compression levels;
- additional PNG chunks and color-space options supported by upstream.

RenderModule must remain free to use only its RGBA8 subset, but the fork itself should not
be designed around that subset.

The hard correctness contract is that every generated PNG is valid and decodes exactly to
the intended pixels and metadata semantics. Byte-identical output across x86 and ARM is
highly desirable as a development invariant, because it makes differential testing simple,
but it should not become a new public API promise unless upstream explicitly wants that
guarantee.

## Fork and branch policy

The working repository should be a fork of the canonical upstream repository:

- upstream: https://github.com/veluca93/fpnge
- fork remote: implementation owner namespace
- long-lived development branch: `arm64-neon`
- upstream tracking remote: `upstream`

Keep the fork rebased or merged from upstream frequently enough that the eventual upstream
pull request is easy to review. Avoid unrelated formatting changes, generated files, or
RenderModule-specific commits.

Before a large refactor, open an upstream issue describing the intended architecture and
asking whether maintainers prefer a particular internal abstraction. Upstream's
`CONTRIBUTING.md` currently requires a Google CLA for contributions.

## Proposed implementation architecture

### High-level structure

The PNG algorithm should remain shared. Architecture-specific code should implement a small
set of SIMD and checksum primitives.

A target structure is conceptually:

```text
fpnge.cc
fpnge.h

internal/
    simd_x86.h
    simd_neon.h
    crc_x86.h
    crc_arm.h
```

Exact filenames should follow upstream maintainer preference. The important design
constraint is the separation of concerns, not the directory layout.

The shared encoder should express operations in terms of the limited primitives it needs
rather than expose architecture-specific intrinsic types throughout the file.

### SIMD backends

The implementation should retain:

```text
x86-64
  +-- SSE4.1, 128-bit
  +-- AVX2,   256-bit

AArch64
  +-- NEON / Advanced SIMD, 128-bit
```

The existing 128-bit SSE implementation is the closest structural reference for NEON.
The initial prototype may use sse2neon to establish feasibility and performance quickly,
but the upstream-quality implementation should use native ARM intrinsics so the mapping,
costs, dependencies, and supported instructions are explicit.

The SIMD abstraction should be deliberately small. Candidate primitives include:

- unaligned load/store;
- broadcast/set operations;
- byte add/subtract/xor;
- comparisons;
- byte-table shuffle;
- lane extraction/insertion;
- shifts;
- mask or movemask-equivalent operations;
- vector-width and mask constants.

Do not build a general-purpose SIMD library inside FPNGE.

### Checksum backends

Checksum acceleration should be separable from the first working NEON encoder.

The desired progression is:

```text
Phase A: NEON encoder + portable checksum
Phase B: ARM CRC32 acceleration
Phase C: PMULL acceleration if profiling justifies it
```

Optional ARM extensions must be isolated so a binary intended for a wider AArch64 target
does not accidentally execute unsupported instructions. Depending on the final build model,
this may use target-specific translation units, function target attributes, compile-time
selection, or Linux runtime feature detection.

The generic AArch64 path must remain valid when optional CRC32/PMULL acceleration is not
available.

## Phased delivery

### Phase 0 — Baseline and upstream coordination

Before changing implementation code:

- fork FPNGE and add the upstream remote;
- record the exact upstream commit used as the baseline;
- build the existing x86 SSE4.1 and AVX2 variants;
- run upstream tests unchanged;
- add a reproducible standalone benchmark harness;
- collect baseline RenderModule FPNG and x86 FPNGE numbers;
- open an upstream issue proposing AArch64/NEON support and the intended abstraction.

The benchmark corpus should be committed or reproducibly generated.

### Phase 1 — Strengthen correctness tests

Improve tests before refactoring SIMD code.

Add:

- independent PNG decode verification;
- deterministic randomized images;
- widths and strides around SIMD boundaries;
- alignment-offset tests;
- all supported channel/sample combinations;
- all predictor/compression modes;
- checksum differential tests;
- sanitizer builds where supported.

Existing upstream corpus tests remain useful but are not sufficient by themselves for a new
architecture backend.

### Phase 2 — Isolate the existing x86 SIMD boundary

Refactor the current x86 implementation so SSE4.1/AVX2 operations are expressed through a
small internal interface.

This phase must intentionally contain no ARM implementation.

Acceptance criteria:

- existing x86 tests pass;
- encoded output remains unchanged for the regression corpus unless a deliberate,
  documented reason prevents it;
- x86 performance does not regress materially;
- compiler output or benchmark data shows the abstraction is effectively inlined.

This is a good candidate for an independent upstream PR because it reduces the review size
of the subsequent ARM patch.

### Phase 3 — Prototype AArch64

Create a fast feasibility build on real ARM hardware. A temporary sse2neon-based build is
acceptable in this phase.

The purpose is to answer:

- does the FPNGE algorithm retain a strong advantage over FPNG on Cortex-A-class CPUs?
- which translated operations dominate the profile?
- are checksum operations or predictor/Huffman SIMD operations the larger opportunity?
- are any current x86 assumptions fundamentally awkward on NEON?

Do not treat translation-layer performance as the final optimization ceiling.

### Phase 4 — Native NEON implementation

Implement the 128-bit backend using native AArch64 NEON intrinsics.

For every nontrivial primitive, maintain a scalar/reference test or differential test.
Pay particular attention to:

- byte shuffles/table lookups;
- movemask-equivalent operations;
- signed versus unsigned comparisons;
- narrowing/widening;
- shifts and lane order;
- tail handling;
- unaligned access;
- endianness assumptions.

The shared FPNGE algorithm should not need ARM-specific branches outside the internal SIMD
boundary except where architecture differences make that clearly simpler and measurable.

### Phase 5 — ARM checksum acceleration

Profile the native NEON encoder first. If CRC/checksum work is significant, introduce
optional ARM acceleration.

Provide a simple trusted reference implementation and compare every optimized path against
it across deterministic and randomized buffer lengths, offsets, and block boundaries.

Optional features must never leak unsupported instructions into the generic path.

### Phase 6 — CI, native hardware, and soak testing

Correctness CI should cover x86 and AArch64 builds. Emulation is useful for compilation and
functional coverage but must not be used for performance decisions.

Native performance validation should include at least:

- Raspberry Pi 4 / Cortex-A72;
- Raspberry Pi 5 / Cortex-A76;

and preferably one non-Raspberry-Pi AArch64 Linux environment.

Long-running tests should check memory stability, crashes, invalid output, thermal behavior,
and sustained encode latency.

### Phase 7 — Upstream submission

Prepare the upstream change as reviewable commits or PRs:

1. test/benchmark hardening;
2. SIMD abstraction with x86 behavior preserved;
3. AArch64 NEON implementation;
4. optional checksum acceleration;
5. CI/documentation.

The exact split should follow upstream maintainer guidance.

Include measured before/after results, supported toolchains, test hardware, and clear notes
about optional ARM feature detection.

## Correctness test plan

### Differential image tests

For each input:

```text
source pixels
    +-- reference/upstream FPNGE -> PNG A
    +-- AArch64 NEON FPNGE       -> PNG B

PNG A -> independent decoder -> pixels A
PNG B -> independent decoder -> pixels B
```

Require:

- `pixels A == source pixels`;
- `pixels B == source pixels`;
- `pixels A == pixels B`.

Where practical, also compare PNG A and PNG B byte-for-byte. Treat byte equality as a
strong regression signal rather than the fundamental PNG correctness contract.

### SIMD boundary dimensions

Exercise dimensions that straddle common vector and block boundaries, including:

```text
1, 2, 3, 4,
7, 8, 9,
15, 16, 17,
31, 32, 33,
47, 48, 49,
63, 64, 65,
127, 128, 129,
255, 256, 257
```

Also cover application-relevant sizes such as 640x480, 1000x700, 1280x720,
1920x1080, and 2048x2048.

### Alignment and stride

Run equivalent images with:

- input pointer offsets across at least 0..31 bytes;
- tight row stride;
- row stride plus 1..31 bytes of padding;
- nonzero/random padding contents.

The logical decoded image must be identical in all cases.

### Image families

Random noise alone is not representative. The corpus should include:

- solid colors;
- horizontal and vertical gradients;
- checkerboards;
- text/UI-like high-contrast shapes;
- repeated scanlines;
- sparse changes between rows;
- alpha gradients;
- mostly transparent images;
- RGB noise with constant alpha;
- fully random data;
- real RenderModule frames.

### Independent decoders

At least one independent standards-compliant decoder must validate every regression image.
For stronger coverage, use more than one implementation in CI or extended tests, such as
libpng plus a second decoder/tool.

### Fuzzing and sanitizers

Add a fuzz/property harness whose generated input controls dimensions, channels,
bytes-per-sample, stride, alignment, options, and pixel data.

Use ASan and UBSan builds on supported compilers. Small images should dominate fuzzing so
millions of cases can exercise vector tails and option combinations.

Special tests should guard output boundaries to detect writes beyond
`FPNGEOutputAllocSize()`.

## Performance methodology

Performance testing must separate codec speed from RenderModule's OpenGL readback and Web
transport.

### Standalone codec benchmark

Measure raw pixels -> PNG only.

Record:

- encode milliseconds;
- megapixels/second;
- encoded byte size;
- cycles/pixel where PMU access is available;
- instructions/pixel;
- branch and cache statistics where useful.

Benchmark at least:

```text
FPNG fallback
FPNGE x86 reference, on x86 hosts
FPNGE AArch64 NEON
FPNGE AArch64 NEON + optional checksum acceleration
```

### Native ARM benchmark controls

For repeatable Raspberry Pi measurements:

- use adequate active cooling;
- use a stable power supply;
- record CPU model and kernel;
- pin the benchmark to a CPU when appropriate;
- control or record CPU frequency/governor;
- warm up before recording;
- record thermal/throttling state;
- run multiple repetitions;
- report median and tail latency, not only the best result.

### Representative corpus

Use both synthetic and real application frames:

- static ImGui-heavy UI;
- ImPlot graph;
- NanoVG drawing;
- View3D scene with UI;
- high-entropy/noisy worst case.

Run at 640x480, 1000x700, 1280x720, 1920x1080, and 2048x2048 where memory limits allow.

## Performance goals and merge gates

Correctness is mandatory; speed targets are goals rather than reasons to weaken safety.

Initial targets:

- materially outperform the current FPNG fallback on Pi 4 and Pi 5 for representative
  RGBA8 RenderModule frames;
- target at least 1.5x FPNG encode throughput at 1280x720 and 1920x1080 on representative
  UI workloads, subject to measurement and output-size tradeoffs;
- preserve x86 FPNGE performance after the SIMD abstraction, with no statistically
  meaningful regression beyond normal benchmark noise;
- avoid meaningful encoded-size regression relative to the equivalent FPNGE compression
  mode;
- avoid sustained allocation growth or thermal-performance collapse in soak tests.

If the 1.5x ARM target is not reached, profiling should determine whether the remaining
cost is inherent to the algorithm, an inefficient SIMD primitive, checksum work, memory
bandwidth, or compiler code generation before deciding whether to continue.

## RenderModule integration

RenderModule should remain a consumer of FPNGE rather than the home of the ARM port.

During development:

1. pin FetchContent to the fork commit;
2. permit FPNGE to build on supported AArch64 configurations;
3. keep FPNG as a fallback and rescue encoder;
4. report the selected backend clearly in diagnostics;
5. run existing PNG correctness tests;
6. run `tests/web/image_benchmark.mjs` on ARM;
7. separately measure image readback, row flip, PNG encode, and packet construction if
   end-to-end performance remains insufficient.

The desired long-term backend structure is:

```text
RenderModule PngEncoder
    +-- FPNGE
    |     +-- x86 SSE4.1 / AVX2 internally
    |     +-- AArch64 NEON internally
    +-- FPNG fallback
```

RenderModule should not need separate `fpnge_backend_avx2.cpp` and
`fpnge_backend_neon.cpp` implementations permanently if upstream FPNGE can own the
architecture selection cleanly. Temporary integration code is acceptable while the fork
is being proven.

When upstream merges the ARM implementation:

- update the FPNGE pin to the upstream commit;
- remove fork-only URLs and temporary compatibility code;
- retain RenderModule ARM regression/benchmark coverage.

## Upstreamability rules

Every implementation choice should be evaluated against these questions:

- Does this make FPNGE more portable rather than more RenderModule-specific?
- Can the x86 maintainer review the ARM change without knowing RenderModule?
- Is the architecture-specific surface small?
- Are optional CPU features isolated and testable?
- Can correctness be demonstrated independently of benchmark results?
- Can x86 behavior and performance be shown to remain stable?
- Is the dependency footprint unchanged or smaller?
- Is the commit history easy to review and bisect?

If the answer to several of these is no, restructure before the fork diverges further.

## Definition of done

The ARM64/NEON effort is considered technically complete when:

- upstream FPNGE API compatibility is preserved;
- all supported image modes pass deterministic round-trip coverage;
- randomized/property tests show zero pixel mismatches;
- sanitizer runs show no encoder memory/undefined-behavior failures;
- optional CPU-feature dispatch cannot execute unsupported instructions;
- x86 SSE4.1/AVX2 regressions remain green;
- native Pi 4 and Pi 5 benchmarks show a meaningful advantage over FPNG;
- long-running native ARM encode/RenderModule stream tests remain stable;
- the fork contains no RenderModule-specific encoder behavior;
- the changes are packaged in a form suitable for an upstream pull request.

## Immediate next steps

1. Create the FPNGE fork and `arm64-neon` branch.
2. Record the exact upstream base commit.
3. Open an upstream design issue before committing to a large source refactor.
4. Add stronger codec correctness and benchmark tests to the fork.
5. Establish x86 and ARM baseline numbers.
6. Refactor the x86 SIMD boundary without changing behavior.
7. Build a quick AArch64 feasibility prototype.
8. Replace the prototype with native NEON intrinsics.
9. Profile and add optional ARM checksum acceleration only where justified.
10. Pin the proven fork in RenderModule and run end-to-end ARM validation.
11. Prepare the upstream contribution and remove the fork pin once merged.
