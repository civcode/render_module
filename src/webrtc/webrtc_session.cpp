#include "webrtc_session.hpp"
#include "webrtc_media.hpp"
#include <algorithm>
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <sstream>
#include <thread>

namespace render_module::detail {
namespace {
using Clock=std::chrono::steady_clock;
#ifdef RENDER_MODULE_WEBRTC_TESTING
std::atomic<unsigned> slowViewerMs{0};
#endif
bool Clean(const std::string& s,std::size_t max,bool empty=false) {
    return (empty || !s.empty()) && s.size()<=max &&
        std::none_of(s.begin(),s.end(),[](unsigned char c){return c<32 || c==127;});
}
std::uint32_t NextSsrc() {
    static std::atomic<std::uint32_t> next{RtcRandom()};
    auto value=next.fetch_add(1); if(!value) value=next.fetch_add(1); return value;
}
std::string IceName(rtc::PeerConnection::IceState s) {
    using S=rtc::PeerConnection::IceState;
    switch(s) {
    case S::New:return "New"; case S::Checking:return "Checking";
    case S::Connected:return "Connected"; case S::Completed:return "Completed";
    case S::Disconnected:return "Disconnected"; case S::Failed:return "Failed"; case S::Closed:return "Closed";
    }
    return "Failed";
}
std::string CandidateName(rtc::Candidate::Type t) {
    using T=rtc::Candidate::Type;
    switch(t) { case T::Host:return "host"; case T::ServerReflexive:return "srflx";
    case T::PeerReflexive:return "prflx"; case T::Relayed:return "relay"; default:return "unknown"; }
}
}
bool ValidateIceConfiguration(const WebConfig& c) {
    if(c.iceServers.size()>8 || c.maxWidth>1920 || c.maxHeight>1080 || c.maxWidth<16 || c.maxHeight<16) return false;
    bool turn=false;
    try {
        for(const auto& s:c.iceServers) {
            if(!Clean(s.urls,512) || s.urls.find_first_of("@# ")!=std::string::npos ||
               !Clean(s.username,256,true) || !Clean(s.credential,256,true)) return false;
            // libnice's TURN_TLS enum is NOT real TLS in RFC5245 mode.
            // Reject turns: rather than silently sending plain TURN/TCP.
            if(s.urls.rfind("stun:",0) && s.urls.rfind("turn:",0)) return false;
            rtc::IceServer server(s.urls);
            if(server.hostname.empty() || !server.port || server.relayType==rtc::IceServer::RelayType::TurnTls) return false;
            const auto query=s.urls.find('?');
            if(query!=std::string::npos && (server.type!=rtc::IceServer::Type::Turn ||
               (s.urls.substr(query)!="?transport=udp" && s.urls.substr(query)!="?transport=tcp"))) return false;
            if(server.type==rtc::IceServer::Type::Turn) {
                if(s.username.empty() || s.credential.empty()) return false;
                turn=true;
            } else if(!s.username.empty() || !s.credential.empty()) return false;
        }
    } catch(...) { return false; }
    return !c.iceRelayOnly || turn;
}
bool ValidateRtcCandidate(const std::string& text,const std::string& mid) {
    if(!Clean(text,WebRtcCandidateLimit) || mid!="video" || text.rfind("candidate:",0)) return false;
    try {
        std::istringstream fields(text); std::string foundation,protocol,host,typeWord,type;
        unsigned component=0,priority=0,port=0;
        if(!(fields>>foundation>>component>>protocol>>priority>>host>>port>>typeWord>>type) ||
           component!=1 || !port || port>65535 || typeWord!="typ") return false;
        rtc::Candidate c(text,mid);
        return c.type()!=rtc::Candidate::Type::Unknown && c.transportType()!=rtc::Candidate::TransportType::Unknown;
    } catch(...) { return false; }
}
bool ValidateRtcAnswer(const std::string& sdp) {
    if(sdp.empty() || sdp.size()>WebRtcSdpLimit || sdp.find('\0')!=std::string::npos) return false;
    try {
        rtc::Description d(sdp,"answer");
        if(d.hasApplication() || d.mediaCount()!=1 || !d.iceUfrag() || !d.icePwd() || !d.fingerprint()) return false;
        auto entry=d.media(0); auto p=std::get_if<rtc::Description::Media*>(&entry);
        if(!p || (*p)->type()!="video" || (*p)->mid()!="video" || (*p)->isRemoved() ||
           (*p)->direction()!=rtc::Description::Direction::RecvOnly) return false;
        const auto* map=(*p)->rtpMap(WebRtcPayloadType);
        if(!map || map->format!="H264" || map->clockRate!=90000) return false;
        std::map<std::string,std::string> params;
        for(const auto& fmtp:map->fmtps) {
            std::istringstream stream(fmtp); std::string part;
            while(std::getline(stream,part,';')) {
                const auto a=part.find_first_not_of(' '), eq=part.find('=');
                if(a==std::string::npos || eq==std::string::npos) return false;
                if(!params.emplace(part.substr(a,eq-a),part.substr(eq+1)).second) return false;
            }
        }
        const auto profile=params["profile-level-id"];
        if(const auto it=params.find("level-asymmetry-allowed");it!=params.end() && it->second!="0" && it->second!="1") return false;
        if(params["packetization-mode"]!="1" || profile.size()!=6 ||
           profile.find_first_not_of("0123456789abcdefABCDEF")!=std::string::npos) return false;
        const auto value=std::stoul(profile,nullptr,16);
        // Equivalent Constrained Baseline encodings (42c0 / 42e0). A lower
        // default level needs an explicit RFC 6184 higher receive declaration;
        // level-asymmetry-allowed alone NEVER grants a higher receiving level.
        if((value>>16)!=66 || ((value>>8)&0x4f)!=0x40) return false;
        unsigned level=value&255;
        if(const auto it=params.find("max-recv-level");it!=params.end()) {
            if(it->second.size()!=4 || it->second.find_first_not_of("0123456789abcdefABCDEF")!=std::string::npos) return false;
            const unsigned receiveLevel=std::stoul(it->second,nullptr,16)&255;
            if(receiveLevel<=level) return false;
            level=receiveLevel;
        }
        if(level<40 || (level!=40 && level!=41 && level!=42 && level!=50 && level!=51 && level!=52 && level!=60 && level!=61 && level!=62)) return false;
        unsigned candidates=0; std::istringstream lines(sdp); std::string line;
        while(std::getline(lines,line)) {
            if(!line.empty() && line.back()=='\r') line.pop_back();
            if(line.size()>WebRtcCandidateLimit*2) return false;
            if(line.rfind("a=candidate:",0)==0 && (++candidates>64 || !ValidateRtcCandidate(line.substr(2),"video"))) return false;
        }
        return true;
    } catch(...) { return false; }
}
class VideoStreamController {
    std::weak_ptr<video::VideoPipeline> pipeline;
    std::mutex mutex;
    Clock::time_point last{};
public:
    explicit VideoStreamController(std::shared_ptr<video::VideoPipeline> p):pipeline(std::move(p)) {}
    void Request() {
        std::lock_guard<std::mutex> lock(mutex); const auto now=Clock::now();
        if(now-last<std::chrono::milliseconds(200)) return;
        if(auto p=pipeline.lock()) { last=now; p->ForceKeyframe(); }
    }
};
struct WebRtcSession::Impl : std::enable_shared_from_this<Impl> {
    std::string sessionId;
    const std::uint32_t ssrc=NextSsrc();
    std::shared_ptr<RtcCounters> stats=std::make_shared<RtcCounters>();
    std::shared_ptr<std::atomic<std::uint64_t>> failures;
    std::shared_ptr<VideoStreamController> controller;
    std::shared_ptr<rtc::PeerConnection> pc;
    std::shared_ptr<rtc::Track> track;
    std::unique_ptr<RtpSender> sender;
    mutable std::mutex mutex;
    std::mutex closing;
    std::condition_variable wake;
    std::thread worker;
    std::deque<RtcSignal> events;
    std::vector<std::pair<std::string,std::string>> earlyCandidates;
    video::EncodedFrame pending;
    std::atomic<bool> ready{false}, failed{false}, stopped{false};
#ifdef RENDER_MODULE_WEBRTC_TESTING
    std::atomic<unsigned> sendDelayMs{0};
#endif
    bool answered=false, iceComplete=false, waitingIdr=true, closed=false;
    unsigned candidateCount=0, consecutiveDrops=0;
    std::string ice="New";
    const Clock::time_point created=Clock::now();
    Clock::time_point disconnected{};
    Impl(std::string id,std::shared_ptr<VideoStreamController> c,std::shared_ptr<std::atomic<std::uint64_t>> f)
        :sessionId(std::move(id)),failures(std::move(f)),controller(std::move(c)) {}
    void Fail() { if(!failed.exchange(true)) ++*failures; ready=false; wake.notify_all(); }
    void Event(RtcSignal event) {
        std::lock_guard<std::mutex> lock(mutex);
        if(stopped) return;
        if(events.size()>=96) { Fail(); return; }
        events.push_back(std::move(event));
    }
    void Start(const WebConfig& c) {
        static std::once_flag logger;
        std::call_once(logger,[]{ rtc::InitLogger(rtc::LogLevel::None); }); // Never leak library SDP/credentials.
        rtc::Configuration config; config.disableAutoNegotiation=true; config.forceMediaTransport=true;
        config.enableIceTcp=c.iceTcp; config.mtu=1280;
        config.iceTransportPolicy=c.iceRelayOnly?rtc::TransportPolicy::Relay:rtc::TransportPolicy::All;
        for(const auto& item:c.iceServers) {
            rtc::IceServer server(item.urls); server.username=item.username; server.password=item.credential;
            config.iceServers.push_back(std::move(server));
        }
        pc=std::make_shared<rtc::PeerConnection>(config); auto weak=weak_from_this();
        pc->onLocalDescription([weak](rtc::Description d) {
            if(auto self=weak.lock()) self->Event({"offer",std::string(d),{}});
        });
        pc->onLocalCandidate([weak](rtc::Candidate c) {
            if(auto self=weak.lock()) self->Event({"ice-candidate",c.candidate(),c.mid()});
        });
        pc->onGatheringStateChange([weak](auto s) {
            if(s==rtc::PeerConnection::GatheringState::Complete)
                if(auto self=weak.lock()) self->Event({"ice-complete",{}, {}});
        });
        pc->onIceStateChange([weak](auto s) {
            if(auto self=weak.lock()) {
                const auto name=IceName(s);
                { std::lock_guard<std::mutex> lock(self->mutex); self->ice=name;
                  self->disconnected=s==rtc::PeerConnection::IceState::Disconnected?Clock::now():Clock::time_point{}; }
                self->Event({"webrtc-state",name,{}});
                if(s==rtc::PeerConnection::IceState::Failed) self->Fail();
            }
        });
        pc->onStateChange([weak](auto s) {
            if(s==rtc::PeerConnection::State::Failed)
                if(auto self=weak.lock()) self->Fail();
        });
        rtc::Description::Video media("video",rtc::Description::Direction::SendOnly);
        media.addH264Codec(WebRtcPayloadType,WebRtcFmtp);
        media.addSSRC(ssrc,"render-module","render-module-"+sessionId,"video");
        track=pc->addTrack(media);
        sender=std::make_unique<RtpSender>(ssrc,RtcRandom(),std::uint16_t(RtcRandom()),stats,
            [control=controller]{control->Request();});
        track->setMediaHandler(sender->handler);
        track->onOpen([weak] { if(auto self=weak.lock()) {
            if(!self->stopped && !self->failed) { self->ready=true; self->controller->Request(); self->wake.notify_all(); }
        } });
        track->onError([weak](std::string) { if(auto self=weak.lock()) self->Fail(); });
        worker=std::thread([this]{ Run(); });
        pc->setLocalDescription(rtc::Description::Type::Offer);
    }
    void Run() {
        for(;;) {
            video::EncodedFrame frame;
            {
                std::unique_lock<std::mutex> lock(mutex);
                wake.wait(lock,[&]{return stopped || failed || pending.storage;});
                if(stopped || failed) break;
                frame=std::move(pending); pending={};
            }
#ifdef RENDER_MODULE_WEBRTC_TESTING
            if(const auto delay=sendDelayMs.load()) {
                std::unique_lock<std::mutex> lock(mutex);
                if(wake.wait_for(lock,std::chrono::milliseconds(delay),[&]{return stopped || failed;})) break;
            }
#endif
            try {
                if(!track->isOpen() || track->bufferedAmount()>256*1024) { Fail(); break; }
                track->sendFrame(reinterpret_cast<const rtc::byte*>(frame.storage->data()),frame.size,
                                 rtc::FrameInfo(RtpTimestamp(sender->timestampBase,frame.ptsUs)));
            } catch(...) { Fail(); break; }
        }
    }
    void Close() {
        std::lock_guard<std::mutex> once(closing);
        if(closed) return; closed=true; stopped=true; ready=false; wake.notify_all();
        if(worker.joinable()) worker.join();
        if(track) { track->resetCallbacks(); track->close(); }
        if(pc) { pc->resetCallbacks(); pc->close(); }
        std::lock_guard<std::mutex> lock(mutex); pending={}; events.clear(); earlyCandidates.clear(); ice="Closed";
    }
};
WebRtcSession::WebRtcSession(std::string id,const WebConfig& c,std::shared_ptr<VideoStreamController> control,
                             std::shared_ptr<std::atomic<std::uint64_t>> failures)
    :impl_(std::make_shared<Impl>(std::move(id),std::move(control),std::move(failures))) {
    try { impl_->Start(c); } catch(...) { impl_->Close(); throw; }
}
WebRtcSession::~WebRtcSession() { Close(); }
std::string WebRtcSession::id() const { return impl_->sessionId; }
bool WebRtcSession::SetRemoteDescription(const std::string& sdp) {
    auto& s=*impl_; if(s.stopped || s.answered || !ValidateRtcAnswer(sdp)) return false;
    // Inline candidates and earlier/later trickle share ONE session budget.
    unsigned inlineCandidates=0; std::istringstream lines(sdp); std::string line;
    while(std::getline(lines,line)) if(line.rfind("a=candidate:",0)==0) ++inlineCandidates;
    if(s.candidateCount+inlineCandidates>64) return false;
    s.candidateCount+=inlineCandidates;
    try {
        s.pc->setRemoteDescription(rtc::Description(sdp,"answer")); s.answered=true;
        for(const auto& c:s.earlyCandidates) s.pc->addRemoteCandidate(rtc::Candidate(c.first,c.second));
        s.earlyCandidates.clear(); return true;
    } catch(...) { s.Fail(); return false; }
}
bool WebRtcSession::AddRemoteCandidate(const std::string& text,const std::string& mid) {
    auto& s=*impl_;
    if(s.stopped || s.iceComplete || ++s.candidateCount>64 || !ValidateRtcCandidate(text,mid)) return false;
    try {
        if(s.answered) s.pc->addRemoteCandidate(rtc::Candidate(text,mid));
        else s.earlyCandidates.emplace_back(text,mid);
        return true;
    } catch(...) { s.Fail(); return false; }
}
bool WebRtcSession::RemoteIceComplete() {
    auto& s=*impl_; if(s.stopped || s.iceComplete) return false;
    s.iceComplete=true; return true; // No remote end-of-candidates API in libdatachannel 0.24.5.
}
void WebRtcSession::SendVideo(const video::EncodedFrame& frame) {
    auto& s=*impl_; if(s.stopped || s.failed || !s.ready) { ++s.stats->rejected; return; }
    bool request=false;
    {
        std::lock_guard<std::mutex> lock(s.mutex);
        if(s.pending.storage) {
            ++s.stats->rejected; s.pending={}; s.waitingIdr=true; request=true;
            if(++s.consecutiveDrops>=30) s.Fail();
        } else if(s.waitingIdr && !frame.keyframe) { ++s.stats->rejected; request=true; }
        else {
            s.waitingIdr=false; s.consecutiveDrops=0; s.pending=frame; ++s.stats->submitted; s.wake.notify_one();
        }
    }
    if(request) s.controller->Request();
}
bool WebRtcSession::Poll(RtcSignal& signal) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if(impl_->events.empty()) return false;
    signal=std::move(impl_->events.front()); impl_->events.pop_front(); return true;
}
bool WebRtcSession::Healthy() {
    auto& s=*impl_; const auto now=Clock::now();
    { std::lock_guard<std::mutex> lock(s.mutex);
      if((!s.ready && now-s.created>std::chrono::seconds(15)) ||
         (s.disconnected!=Clock::time_point{} && now-s.disconnected>std::chrono::seconds(3))) s.Fail(); }
    return !s.failed && !s.stopped;
}
RtcSessionSnapshot WebRtcSession::Snapshot() const {
    auto& s=*impl_; RtcSessionSnapshot out;
    { std::lock_guard<std::mutex> lock(s.mutex); out={s.sessionId,s.ice,"unknown",s.ssrc,s.ready.load(),s.failed.load(),unsigned(bool(s.pending.storage)),s.stats}; }
    rtc::Candidate local,remote;
    if(!s.stopped && s.pc->getSelectedCandidatePair(&local,&remote)) out.candidateType=CandidateName(local.type());
    return out;
}
void WebRtcSession::Close() { if(impl_) impl_->Close(); }
void WebRtcSession::RejectStream() { impl_->Fail(); }
void SetWebRtcTestSlowViewer(unsigned milliseconds) {
#ifdef RENDER_MODULE_WEBRTC_TESTING
    if(milliseconds>1000) throw std::invalid_argument("test delay limit");
    slowViewerMs=milliseconds;
#else
    (void)milliseconds; throw std::logic_error("WebRTC test hooks disabled");
#endif
}
void WebRtcSession::SetTestSendDelay(unsigned milliseconds) {
#ifdef RENDER_MODULE_WEBRTC_TESTING
    impl_->sendDelayMs=milliseconds;
#else
    (void)milliseconds; throw std::logic_error("WebRTC test hooks disabled");
#endif
}
struct EncodedFrameHub::Impl {
    std::shared_ptr<video::VideoPipeline> pipeline;
    std::shared_ptr<VideoStreamController> controller;
    std::shared_ptr<std::atomic<std::uint64_t>> failures=std::make_shared<std::atomic<std::uint64_t>>(0);
    WebConfig config;
    mutable std::mutex mutex;
    std::map<std::string,std::shared_ptr<WebRtcSession>> sessions;
    std::atomic<bool> stopped{false};
    std::thread worker;
    Impl(std::shared_ptr<video::VideoPipeline> p,WebConfig c):pipeline(std::move(p)),
        controller(std::make_shared<VideoStreamController>(pipeline)),config(std::move(c)),worker([this]{Run();}) {}
    void Run() {
        while(!stopped) {
            video::EncodedFrame frame;
            if(!pipeline->TryReceive(frame)) { std::this_thread::sleep_for(std::chrono::milliseconds(1)); continue; }
            std::vector<std::shared_ptr<WebRtcSession>> viewers;
            { std::lock_guard<std::mutex> lock(mutex); for(const auto& pair:sessions) viewers.push_back(pair.second); }
            if(viewers.empty()) continue;
            if(!CompatibleAccessUnit(frame)) { for(auto& s:viewers) s->RejectStream(); continue; }
            // One bounded-size copy releases the Phase 6 pool before fanout. A
            // slow viewer can retain only its active + pending copy, never starve
            // the encoder's three leases or block other viewers' send workers.
            try {
                frame.storage=std::make_shared<const std::vector<std::uint8_t>>(frame.storage->begin(),frame.storage->begin()+frame.size);
                for(auto& s:viewers) s->SendVideo(frame);
            } catch(...) { for(auto& s:viewers) s->RejectStream(); }
        }
    }
};
EncodedFrameHub::EncodedFrameHub(std::shared_ptr<video::VideoPipeline> p,WebConfig c):impl_(std::make_unique<Impl>(std::move(p),std::move(c))) {}
EncodedFrameHub::~EncodedFrameHub() {
    auto& s=*impl_; s.stopped=true; s.worker.join();
    for(auto& pair:s.sessions) pair.second->Close();
}
std::shared_ptr<WebRtcSession> EncodedFrameHub::Create(const std::string& id) {
    auto& s=*impl_; std::lock_guard<std::mutex> lock(s.mutex);
    if(s.sessions.size()>=s.config.maxClients || s.sessions.count(id)) return {};
    auto session=std::make_shared<WebRtcSession>(id,s.config,s.controller,s.failures);
#ifdef RENDER_MODULE_WEBRTC_TESTING
    if(!s.sessions.empty()) session->SetTestSendDelay(slowViewerMs.load());
#endif
    s.sessions.emplace(id,session); return session;
}
void EncodedFrameHub::Remove(const std::string& id) {
    std::shared_ptr<WebRtcSession> session;
    { std::lock_guard<std::mutex> lock(impl_->mutex); auto it=impl_->sessions.find(id);
      if(it==impl_->sessions.end()) return; session=it->second; impl_->sessions.erase(it); }
    session->Close();
}
std::vector<RtcSessionSnapshot> EncodedFrameHub::Snapshot() const {
    std::lock_guard<std::mutex> lock(impl_->mutex); std::vector<RtcSessionSnapshot> out;
    for(const auto& pair:impl_->sessions) out.push_back(pair.second->Snapshot()); return out;
}
std::uint64_t EncodedFrameHub::Failures() const { return impl_->failures->load(); }
video::VideoMetrics EncodedFrameHub::EncoderMetrics() const { return impl_->pipeline->Metrics(); }
} // namespace render_module::detail
