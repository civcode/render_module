#pragma once
#include "input/remote_input_events.hpp"
#include "render_module/config.hpp"
#include "web/image_encoder.hpp"
#include <string_view>
#include <optional>
#include <vector>

namespace render_module::detail {
constexpr unsigned WebProtocolVersion = 2;
constexpr unsigned WebImageProtocolVersion = 2;
constexpr std::size_t WebMessageLimit = 8192;
constexpr std::size_t WebSignalingLimit = 65536;
// Encoded payload limit. Supports worst-case RGBA PNG at configured 1920x1080;
// pathological 2048x2048 incompressible images fail closed rather than growing arbitrarily.
constexpr std::size_t WebFrameLimit = 16*1024*1024;
struct WebSize { int width = 0, height = 0; };
struct WebInputState {
    InputStateSnapshot input;
    std::optional<RemoteMouseSource> source;
};
struct WebMessage {
    enum class Kind { Input, Release, Viewport, FrameAck, SetStream, RtcHello, RtcAnswer, RtcCandidate, RtcComplete } kind = Kind::Input;
    std::string session, sdp, candidate, mid;
    std::uint64_t sequence = 0, frameId = 0;
    WebStreamMode stream = WebStreamMode::Jpeg;
    WebSize viewport;
    WebInputState state;
    std::vector<RemoteInputEvent> events;
};
bool ParseWebMessage(std::string_view json, const WebConfig& config,
                     const WebInputState& state, WebMessage& result);
WebSize ClampWebViewport(int width, int height, const WebConfig& config);
// Big-endian image protocol v2: "RMIM", u16 version=2, u16 codec (1 JPEG,
// 2 PNG), u64 frameId, u32 width, u32 height, u32 payload bytes. Header is 28 bytes.
std::vector<unsigned char> PackImage(const EncodedImage&);
} // namespace render_module::detail
