# Optional software video dependencies

## OpenH264 2.6.0

Source revision: `652bdb7719f30b52b08e506645a7322ff1b2cc6f`.
Copyright (c) 2013, Cisco Systems. **BSD-2-Clause**.

The build uses source, not Cisco's downloadable binary. The source copyright
license does **not** imply coverage under Cisco's separate binary-distribution
patent/royalty arrangement. Review applicable H.264 patent/licensing obligations
for your deployment. No claim of patent clearance is made here.

The unmodified upstream `LICENSE` is installed as `OpenH264-LICENSE`. Sources:
[license](https://github.com/cisco/openh264/blob/v2.6.0/LICENSE),
[project/binary licensing information](https://github.com/cisco/openh264/blob/v2.6.0/README.md).

## libyuv 1972

Source revision: `41a6e684a68950f07912b7e6f57ba3fa2e10fb65`.
Copyright 2011 The LibYuv Project Authors. **BSD-3-Clause**, with the additional
patent grant and conditions in its `PATENTS` file. Both files are installed under
`licenses/libyuv/`; preserve them when redistributing the statically linked code.

[LICENSE](https://chromium.googlesource.com/libyuv/libyuv/+/41a6e684a68950f07912b7e6f57ba3fa2e10fb65/LICENSE),
[PATENTS](https://chromium.googlesource.com/libyuv/libyuv/+/41a6e684a68950f07912b7e6f57ba3fa2e10fb65/PATENTS).

## Test tooling

System libavcodec/libavutil are optionally linked into test executables for an
independent H.264 decoder. They are **not linked into RenderModule/RenderModuleVideo**,
not required for baseline encoding, and not installed by this project. FFmpeg
licensing depends on its build configuration; consult the system package's notices
when redistributing test tooling. No FFmpeg encoder or executable is invoked.
