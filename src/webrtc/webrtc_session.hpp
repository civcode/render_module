#pragma once
#include "render_module/config.hpp"
#include "render_module/video.hpp"
#include <atomic>
#include <memory>
#include <string>
#include <vector>

namespace render_module::detail {
constexpr std::size_t WebRtcSdpLimit=32768, WebRtcCandidateLimit=1024, WebRtcMessageLimit=65536;
struct RtcCounters {
    std::atomic<std::uint64_t> packets{0}, bytes{0}, nacks{0}, retransmits{0}, plis{0};
    std::atomic<std::uint64_t> rembBps{0}, rembTimeMs{0}, submitted{0}, rejected{0}, invalidRtcp{0}, testDropped{0};
};
struct RtcSignal { std::string type, text, mid; };
struct RtcSessionSnapshot {
    std::string id, iceState, candidateType;
    std::uint32_t ssrc=0;
    bool connected=false, failed=false;
    unsigned pending=0;
    std::shared_ptr<RtcCounters> counters;
};
void SetWebRtcTestPacketLoss(unsigned percent);
void SetWebRtcTestSlowViewer(unsigned milliseconds);
bool ValidateIceConfiguration(const WebConfig& config);
bool ValidateRtcAnswer(const std::string& sdp);
bool ValidateRtcCandidate(const std::string& candidate,const std::string& mid);
class VideoStreamController;
class WebRtcSession {
public:
    WebRtcSession(std::string id,const WebConfig&,std::shared_ptr<VideoStreamController>,
                  std::shared_ptr<std::atomic<std::uint64_t>> failures);
    ~WebRtcSession();
    std::string id() const;
    bool SetRemoteDescription(const std::string& answer);
    bool AddRemoteCandidate(const std::string& candidate,const std::string& mid);
    bool RemoteIceComplete();
    void SendVideo(const video::EncodedFrame&);
    bool Poll(RtcSignal&);
    bool Healthy();
    RtcSessionSnapshot Snapshot() const;
    void Close();
    void RejectStream(); // Cross-thread failure notification; teardown stays on signaling owner.
    void SetTestSendDelay(unsigned milliseconds);
private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
};
class EncodedFrameHub {
public:
    EncodedFrameHub(std::shared_ptr<video::VideoPipeline>,WebConfig);
    ~EncodedFrameHub();
    std::shared_ptr<WebRtcSession> Create(const std::string& id);
    void Remove(const std::string& id);
    std::vector<RtcSessionSnapshot> Snapshot() const;
    std::uint64_t Failures() const;
    video::VideoMetrics EncoderMetrics() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace render_module::detail
