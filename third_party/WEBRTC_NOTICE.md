# Optional WebRTC dependency notices

Only `RENDER_MODULE_ENABLE_WEBRTC=ON` fetches/builds this stack. Vendor types stay
out of installed RenderModule headers. Original license texts are installed under
`share/render-module/licenses/`.

| Component | Immutable revision | License |
|---|---|---|
| [libdatachannel](https://github.com/paullouisageneau/libdatachannel) 0.24.5 | `443f6934d9007eb7076ab7825ba330f355fcbead` | MPL-2.0 |
| bundled [libsrtp](https://github.com/cisco/libsrtp) | `24b3bf8f19b6f5ab4cd2bcceb4f4064efca86fd5` | BSD-3-Clause |
| bundled [usrsctp](https://github.com/sctplab/usrsctp) | `fec583d54493f879d2ae44a743423bf8a04371ab` | BSD-3-Clause |
| bundled [plog](https://github.com/SergiusTheBest/plog) | `94899e0b926ac1b0f4750bfbd495167b4a6ae9ef` | MIT |

The commit pins the submodule objects. libjuice and nlohmann/json submodules are
not used. Upstream examples, tests and WebSockets are disabled. usrsctp is an
upstream link dependency; RenderModule Phase 7 creates no DataChannels.

RenderModule modifies libdatachannel's `src/impl/icetransport.cpp` to map relay-only
policy to libnice's `force-relay` property. `cmake/LibDataChannelNiceRelay.cmake`
checks the exact original SHA-256, generates a build-tree copy and adjusts internal
include paths for that copy. The checkout is not modified. The full modified
MPL-covered file is supplied in `licenses/libdatachannel-modified/`; its copyright
and MPL notice remain intact. Unmodified covered source is available at the pinned
repository above. Distributors must preserve applicable source availability and
license obligations; this notice does not replace the original licenses.

System dependencies are dynamically linked, not downloaded by this integration:
libnice (tested 0.1.21; its LGPL/MPL notices apply), GLib/GIO and dependencies, and
OpenSSL (tested 3.0.13, Apache-2.0). Packagers must retain their platform package
notices when redistributing those libraries. OpenSSL supplies WebRTC DTLS; it does
not add TURN/TLS support to libnice. `turns:` is rejected rather than downgraded.

The separate [VIDEO_NOTICE.md](VIDEO_NOTICE.md) remains authoritative for OpenH264
and libyuv. Building OpenH264 from source is not equivalent to Cisco's separately
distributed royalty-covered binary program. No patent-clearance claim is made.
The Firefox test-only GMP plugin is downloaded separately from Cisco using Mozilla's
manifest and hash; it is not shipped in the RenderModule installation.
