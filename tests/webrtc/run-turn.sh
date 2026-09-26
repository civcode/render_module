#!/usr/bin/env bash
# Run INSIDE the disposable test container/network, never on a production TURN host.
set -euo pipefail
build=$(realpath "${1:?build directory required}")
transport=${2:-udp}
[[ "$transport" == udp || "$transport" == tcp ]] || exit 2
root=$(cd "$(dirname "$0")/../.." && pwd)
work=$(mktemp -d)
turn_pid=
trap '[[ -z "$turn_pid" ]] || { kill "$turn_pid" 2>/dev/null || true; wait "$turn_pid" 2>/dev/null || true; }; rm -rf "$work"' EXIT
ip=$(hostname -I | awk '{print $1}')
# Disposable test credentials. Loopback peers are unsafe on public deployments.
turnserver -n --no-cli --cli-password=test-only --no-tls --no-dtls \
    --listening-ip="$ip" --relay-ip="$ip" --listening-port=3478 \
    --min-port=49160 --max-port=49220 --realm=render-module.test --lt-cred-mech \
    --user=phase7:phase7-test-only --allow-loopback-peers --no-multicast-peers \
    --relay-threads=2 --pidfile="$work/pid" --log-file="$work/turn.log" >"$work/stdout" 2>&1 &
turn_pid=$!
sleep 1
kill -0 "$turn_pid"
export RENDER_MODULE_TEST_TURN="turn:$ip:3478?transport=$transport"
export RENDER_MODULE_TEST_TURN_USER=phase7 RENDER_MODULE_TEST_TURN_PASSWORD=phase7-test-only
export RENDER_MODULE_TEST_LOSS=0
export PLAYWRIGHT_BROWSERS_PATH="${PLAYWRIGHT_BROWSERS_PATH:-$build/browser-cache}"
"${NODE:-node}" "$root/tests/web/webrtc_test.mjs" "$build/Release/bin/RenderModuleWebFixture" "$build/test-artifacts/webrtc-turn-$transport"
