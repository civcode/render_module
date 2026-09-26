#pragma once
#include "webrtc_session.hpp"
#include <rtc/rtc.hpp>
#include <atomic>
#include <functional>
#include <memory>

namespace render_module::detail {
constexpr unsigned WebRtcPayloadType = 96;
constexpr std::size_t WebRtcNackHistory = 1024;
constexpr std::size_t WebRtcFragmentSize = 1160;
constexpr std::size_t WebRtcAccessUnitLimit = 2*1024*1024;
// Maximum send level, not an assertion that every smaller SPS uses level 4.0.
// Phase 7 limits capture to <=1920x1080, <=30 fps and Phase 6's <=12 Mbps.
constexpr const char* WebRtcFmtp = "profile-level-id=42c028;packetization-mode=1;level-asymmetry-allowed=1";
std::uint32_t RtpTimestamp(std::uint32_t base,std::int64_t ptsUs);
bool CompatibleAccessUnit(const video::EncodedFrame& frame);
std::uint32_t RtcRandom();
class RtpSender {
public:
    RtpSender(std::uint32_t ssrc,std::uint32_t base,std::uint16_t sequence,
              std::shared_ptr<RtcCounters> counters,std::function<void()> keyframe);
    std::shared_ptr<rtc::MediaHandler> handler;
    std::shared_ptr<rtc::RtpPacketizationConfig> config;
    const std::uint32_t timestampBase;
};
// Test builds only: deterministic loss AFTER NACK history, before initial SRTP send.
// Retransmissions are not dropped. No network namespace/host qdisc is modified.
void SetWebRtcTestPacketLoss(unsigned percent);
} // namespace render_module::detail
