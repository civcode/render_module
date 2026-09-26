# WebRTC integration tests

Enable WEB, VIDEO, OPENH264, WEBRTC, `RENDER_MODULE_BUILD_TESTS` and
`RENDER_MODULE_BUILD_BROWSER_TESTS`; install the locked `tests/web` npm dependencies
and Playwright Chromium. No Node/browser dependency is introduced at runtime.
Run `ctest --test-dir BUILD -L webrtc --output-on-failure`. Every test unsets
DISPLAY/WAYLAND_DISPLAY. ICE needs a routed network interface: this harness's
loopback-only namespace cannot gather libnice host candidates, so use Docker's
bridge network or a normal development host.

Native tests: original-PTS clock/wrap, actual library FU-A reconstruction/marker/
sequence/SSRC separation, Sender Reports, NACK/eviction, PLI, REMB, malformed RTCP,
SDP/ICE validation and real encoder SPS compatibility, repeated negotiating-peer
teardown, authenticated signaling rejection/ownership/bounds/reconnect/shutdown.
Browser tests: real decoded colors and controls, Unicode, orbit, four resolutions
on one peer, two viewers/one encoder, independent SSRCs, viewer input rejection,
held-key reconnect, refresh, getStats, injected loss and slow-sender isolation.

`webrtc_chromium_loss1/3` drop respectively 1/3 initial RTP packets per 100,
after NACK storage; retransmissions are not dropped. This is deterministic
application injection, not random netem loss. `webrtc_chromium_slow_viewer` delays
only the additional viewer's send worker by 400 ms; waits are shutdown-interruptible.
Both hooks are compiled only with BUILD_TESTS. Browser failure artifacts contain
bounded, credential-redacted diagnostics, never full SDP/ICE bodies.

## Optional Firefox

Install Playwright Firefox and enable `RENDER_MODULE_BUILD_FIREFOX_TESTS=ON`.
Fresh automation profiles need the separate OpenH264 GMP plugin. Tested Firefox
155.0 with the Linux x86-64 binary/hash from [Mozilla's manifest](https://github.com/mozilla-firefox/firefox/blob/main/toolkit/content/gmp-sources/openh264.json):

```sh
export MOZ_GMP_PATH="$PWD/build/test-tools/gmp-gmpopenh264/2.6.0"
mkdir -p "$MOZ_GMP_PATH"
curl -fL https://ciscobinary.openh264.org/openh264-linux64-652bdb7719f30b52b08e506645a7322ff1b2cc6f.zip \
  -o "$MOZ_GMP_PATH/openh264.zip"
printf '%s  %s\n' \
  f5246bf14d038adf4ce0c4360262ab722bc3de4220f047c3d542b4c564074b4877dc8659e3125c5171c749e7ce93f20cc63777eb5e1539e960670cbc5f30ac85 \
  "$MOZ_GMP_PATH/openh264.zip" | sha512sum -c -
unzip -o "$MOZ_GMP_PATH/openh264.zip" -d "$MOZ_GMP_PATH"
ctest --test-dir BUILD -R '^webrtc_firefox$' --output-on-failure
```

The plugin is test tooling, not a RenderModule install artifact. Do not substitute
it for the Phase 6 source-built encoder or conflate their distribution terms.

## Disposable Docker / forced relay

`Dockerfile` supplies Ubuntu 24.04 runtime/browser/Mesa/coturn dependencies. Mount
the repository **at its original absolute path**, since CMake embeds paths. For
the tested standalone Node distribution, mount its directory at the same path;
`NODE_HOME` below must point to that distribution (not the whole system `/usr`).
Configure `RENDER_MODULE_NODE` to its `bin/node` when using this recipe.

```sh
docker build -t render-module-webrtc-tests:phase7 tests/webrtc
# Set NODE_HOME, BUILD (absolute build directory), and MOZ_GMP_PATH if using Firefox.
docker run --rm --init --user "$(id -u):$(id -g)" --shm-size=1g \
  -v "$PWD:$PWD" -v "$NODE_HOME:$NODE_HOME:ro" -w "$PWD" \
  -e NODE="$NODE_HOME/bin/node" -e MOZ_GMP_PATH \
  render-module-webrtc-tests:phase7 ctest --test-dir "$BUILD" -L webrtc --output-on-failure
# Same docker options, replace ctest command with either:
# bash tests/webrtc/run-turn.sh "$BUILD" udp
# bash tests/webrtc/run-turn.sh "$BUILD" tcp
```

`run-turn.sh` starts/stops its own coturn, uses disposable credentials/relay ports,
and forces relay on **both** server and browser. It asserts server and browser
selected candidate types, then runs the full decoded-video/input/resize/reconnect
suite. Artifacts are `BUILD/test-artifacts/webrtc-turn-{udp,tcp}`. No host port
publication, privileged container, host-network mode, or host qdisc change is needed.
The permissive loopback-peer option is **test-only, unsafe for public TURN**.
TLS is deliberately rejected by this libnice integration; there is no fake TLS test.

For the full Desktop+headless matrix, start Xvfb inside the same container command,
then run all CTest tests; only Desktop uses DISPLAY. Use `--init` (xvfb-run as PID 1
can wait indefinitely for its startup signal). For example:
`bash -c 'Xvfb :99 -screen 0 1280x1024x24 -nolisten tcp -ac & sleep 1; DISPLAY=:99 ctest --test-dir BUILD --output-on-failure'`.

See [WEBRTC.md](../../WEBRTC.md) for measurements, scope and limitations. Real OS
IME/mobile browsers, WAN jitter/congestion, TURN/TLS and end-to-end input latency
are not claimed by these tests.
