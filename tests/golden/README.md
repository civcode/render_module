# Full-root UI regression

`root-ui.png` is the reviewed 640×480 baseline for `headless_output_visual`.
It contains fixed ImGui text/widgets, a clipped child, an ImPlot line plot,
NanoVG primitives, a fixed-camera Magnum box/grid/axes, a four-panel dockspace,
and foreground overlays. Red TOP and blue BOTTOM markers test output orientation.
There is no animation, randomness, wall-clock text, or FPS display. The real
DebugConsole is disabled in this golden because its existing UI includes FPS.
The ImGui built-in font is used; no OS font rasterization is involved.

Baseline: Mesa 25.2.8 llvmpipe (LLVM 20.1.2), OpenGL 4.5 Core, native EGL device 0
with a surfaceless context. ImGui v1.91.9b-docking and ImPlot v0.16 are pinned.

Comparison, over **all RGBA channels of the entire root frame**:

- Exact dimensions: 640×480.
- Mean absolute channel error ≤ **1.0** on the 0–255 scale.
- At most **0.5%** of pixels may have any channel error > **12**.
- Maximum channel error is reported, not independently capped: a few shifted
  anti-aliased edge pixels can differ strongly across drivers.
- Independent colored-region checks ensure Canvas and View3D are present;
  exact-color markers check top/bottom orientation.
- PNG decoding must reproduce the captured RGBA bytes exactly (codec test,
  separate from the tolerant cross-renderer comparison).

This tolerates small rasterization differences, not layout/font/API changes.
A new driver exceeding the thresholds needs investigation, not automatic golden
replacement. The current llvmpipe runs compare with zero pixel error.

Every visual run writes `test-artifacts/root-ui-actual.png` in the build directory
before semantic/comparison checks. Pixel-comparison failures also write
`root-ui-actual.png.diff.png` (absolute RGB error amplified 4×). Dimension errors
still leave the actual image. Other tests save resized and orientation PNGs there.

Run (no X11, Wayland, or Xvfb needed):

```sh
env -u DISPLAY -u WAYLAND_DISPLAY ctest --test-dir build -R headless_output_visual --output-on-failure
```

Intentional baseline updates are **manual**, never part of CTest:

```sh
env -u DISPLAY -u WAYLAND_DISPLAY ./build/Release/bin/RenderModuleOutputTests visual headless --update-golden
```

Inspect the resulting image and review the diff before accepting it. Do not use
this flag to hide an unexplained regression.
