#pragma once
#include "input/remote_input_events.hpp"
#include "render_module/config.hpp"
#include <string_view>
#include <optional>
#include <vector>

namespace render_module::detail {
constexpr unsigned WebProtocolVersion = 1;
constexpr std::size_t WebMessageLimit = 8192;
constexpr std::size_t WebFrameLimit = 16*1024*1024;
struct WebSize { int width = 0, height = 0; };
struct WebInputState {
    InputStateSnapshot input;
    std::optional<RemoteMouseSource> source;
};
struct WebMessage {
    enum class Kind { Input, Release, Viewport, FrameAck } kind = Kind::Input;
    std::uint64_t sequence = 0, frameId = 0;
    WebSize viewport;
    WebInputState state;
    std::vector<RemoteInputEvent> events;
};
bool ParseWebMessage(std::string_view json, const WebConfig& config,
                     const WebInputState& state, WebMessage& result);
WebSize ClampWebViewport(int width, int height, const WebConfig& config);
// Big-endian: "RMJP", u16 version=1, u16 type=1, u64 frameId,
// u32 width, u32 height, u32 payload bytes, then JPEG. Header is 28 bytes.
std::vector<unsigned char> PackJpeg(std::uint64_t id, int width, int height,
                                   const unsigned char* jpeg, std::size_t size);
} // namespace render_module::detail
