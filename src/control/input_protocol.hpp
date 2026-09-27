#pragma once
#include "input/remote_input_events.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace render_module::detail::dc {
constexpr std::uint8_t Major=1;
constexpr std::size_t HeaderSize=16, MaxPacketSize=512, KeyBytes=16;
constexpr std::uint32_t Capabilities=1; // Bit 0: absolute-pointer fences and snapshots.
constexpr const char* FastLabel="input-fast-v1";
constexpr const char* ControlLabel="control-v1";
constexpr const char* Subprotocol="render-module-input-v1";
enum class Type : std::uint8_t {
    ClientHello=1, PointerMove=2, MouseButton=3, MouseWheel=4, Key=5, TextUtf8=6,
    Focus=7, Snapshot=8, ViewportRequest=9, AcquireControl=10, ReleaseControl=11,
    BrowserStats=12, Ping=13, Pong=14,
    ServerHello=128, ViewportAccepted=129, ControlState=130, ResyncRequired=131,
    StreamState=132, Error=133
};
enum class Lane { Fast, Control };
enum class Direction { Client, Server };
struct Snapshot {
    float x=0, y=0;
    std::uint32_t fence=0;
    std::uint32_t epoch=0; // Server-issued control/resync generation. Prevents queued old snapshots restoring input.
    std::uint8_t buttons=0, modifiers=0, source=0;
    bool focused=false;
    std::array<std::uint8_t,KeyBytes> keys{}; // Bit n represents protocol key ID n; bit 0 is reserved.
};
struct Packet {
    Type type=Type::Ping;
    std::uint32_t sequence=0;
    std::uint64_t timestampUs=0;
    Snapshot state{};
    float horizontal=0, vertical=0, dpr=1;
    std::uint32_t capabilities=Capabilities, fastBuffered=0, controlBuffered=0;
    std::uint16_t minor=0, width=0, height=0, key=0, code=0;
    std::uint8_t button=0, modifiers=0;
    bool down=false, control=false, enabled=false;
    TextUtf8 text{};
    std::uint64_t echoUs=0;
};
// Output is unchanged on ANY failure. No input state mutation or heap allocation.
bool Decode(const std::uint8_t*,std::size_t,Lane,Direction,Packet&) noexcept;
std::vector<std::uint8_t> Encode(const Packet&,Direction); // Empty if invalid.
std::optional<RenderKey> DecodeKey(std::uint16_t id) noexcept;
std::optional<std::uint16_t> EncodeKey(RenderKey key) noexcept;
InputStateSnapshot InputSnapshot(const Snapshot&);
bool Newer(std::uint32_t value,std::uint32_t reference) noexcept;
class FastFence {
public:
    bool Accept(std::uint32_t value) noexcept;
    void Advance(std::uint32_t fence) noexcept;
    void Reset() noexcept { floor_.reset(); }
private:
    std::optional<std::uint32_t> floor_;
};
} // namespace render_module::detail::dc
