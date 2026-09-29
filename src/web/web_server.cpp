#include "web_server.hpp"
#ifdef RENDER_MODULE_ENABLE_WEBRTC
#include "webrtc/webrtc_session.hpp"
#include "control/input_state.hpp"
#endif
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <boost/json.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <deque>
#include <list>
#include <mutex>
#include <thread>
#include <sys/random.h>
#include <cerrno>
#include <cstdio>

namespace render_module::detail {
namespace net = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
using tcp = net::ip::tcp;
using Clock = std::chrono::steady_clock;
namespace {
std::string RandomId() {
    unsigned char bytes[16]; std::size_t offset = 0;
    while (offset != sizeof(bytes)) {
        const auto n = getrandom(bytes+offset, sizeof(bytes)-offset, 0);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) throw std::runtime_error("session randomness unavailable");
        offset += std::size_t(n);
    }
    std::string id; id.reserve(32);
    for (auto b : bytes) { id += "0123456789abcdef"[b>>4]; id += "0123456789abcdef"[b&15]; }
    return id;
}
bool EqualToken(std::string_view a, std::string_view b) {
    unsigned difference = unsigned(a.size() ^ b.size());
    for (std::size_t i = 0; i < std::max(a.size(), b.size()); ++i)
        difference |= unsigned(i < a.size() ? a[i] : 0) ^ unsigned(i < b.size() ? b[i] : 0);
    return difference == 0;
}
std::string Json(boost::json::object object) { object["v"] = WebProtocolVersion; return boost::json::serialize(object); }
void Headers(http::response<http::string_body>& response) {
    response.set("X-Content-Type-Options", "nosniff");
    response.set("Referrer-Policy", "no-referrer");
    response.set("X-Frame-Options", "DENY");
    response.set("Content-Security-Policy", "default-src 'none'; script-src 'self'; style-src 'self'; connect-src 'self'; img-src 'self' blob:; media-src 'self' blob:; frame-ancestors 'none'; base-uri 'none'; form-action 'self'");
    response.set(http::field::cache_control, "no-store");
}
bool IsImageStream(WebStreamMode stream) { return stream==WebStreamMode::Jpeg || stream==WebStreamMode::Png; }
const char* StreamName(WebStreamMode stream) {
    switch(stream) {
        case WebStreamMode::Jpeg: return "jpeg";
        case WebStreamMode::Png: return "png";
        case WebStreamMode::H264: return "h264";
    }
    return "unknown";
}
const char* MediaTransport(WebStreamMode stream) { return IsImageStream(stream)?"websocket":"webrtc"; }
const char* InputTransport(WebStreamMode stream) { return IsImageStream(stream)?"websocket-json-v1":"datachannel-v1"; }
} // namespace

struct WebServer::Impl {
    struct Session;
    WebServer& owner;
    WebConfig config;
    std::shared_ptr<RemoteInputQueue> input;
    std::shared_ptr<video::VideoPipeline> video;
#ifdef RENDER_MODULE_ENABLE_WEBRTC
    std::unique_ptr<EncodedFrameHub> hub;
#endif
    net::io_context io{1};
    tcp::acceptor acceptor{io};
    net::steady_timer timer{io}, shutdownTimer{io};
    std::thread thread;
    std::list<std::shared_ptr<Session>> sessions;
    Session* controller = nullptr;
    bool stopping = false;
    unsigned port = 0;
    std::mutex mailbox;
    WebSize requested, actual;
    bool resizePending = false;
    struct PublishedImage { std::shared_ptr<const std::vector<unsigned char>> packet; std::uint64_t id=0; };
    PublishedImage latestJpeg, latestPng;
    std::atomic<unsigned> jpegSessions{0}, pngSessions{0}, h264Sessions{0};
    std::string pngEncoder = "fpng";
    bool pngFpngeCompiled = false;
    bool pngFpngeCpuSupported = false;
    Impl(WebServer& owner_, WebConfig config_, std::shared_ptr<RemoteInputQueue> input_, std::shared_ptr<video::VideoPipeline> video_)
        : owner(owner_), config(std::move(config_)), input(std::move(input_)), video(std::move(video_)) {}
    void Accept();
    void Tick();
    void Elect();
    void Remove(Session* session);
    void Relinquish(Session* session);
    bool Available(WebStreamMode stream) const {
        if(stream==WebStreamMode::Jpeg || stream==WebStreamMode::Png) return true;
#ifdef RENDER_MODULE_ENABLE_WEBRTC
        return stream==WebStreamMode::H264 && bool(hub) && config.maxWidth>=16 && config.maxHeight>=16;
#else
        return false;
#endif
    }
    WebSize ConstrainViewport(WebSize size) const {
        if(!size.width || !size.height) return size;
        if(h264Sessions.load()!=0) {
            size.width=std::max(16,size.width);
            size.height=std::max(16,size.height);
        }
        return size;
    }
    boost::json::array AvailableStreams() const {
        boost::json::array streams; streams.push_back("jpeg"); streams.push_back("png");
        if(Available(WebStreamMode::H264)) streams.push_back("h264");
        return streams;
    }
    void AddDemand(WebStreamMode stream, int delta) {
        auto adjust=[delta](std::atomic<unsigned>& value) { if(delta>0) value.fetch_add(unsigned(delta)); else value.fetch_sub(unsigned(-delta)); };
        if(stream==WebStreamMode::Jpeg) adjust(jpegSessions);
        else if(stream==WebStreamMode::Png) adjust(pngSessions);
        else if(stream==WebStreamMode::H264) adjust(h264Sessions);
    }
    void DropImage(WebStreamMode stream) {
        ++owner.counters.dropped;
        if(stream==WebStreamMode::Jpeg) ++owner.counters.jpegDropped;
        else if(stream==WebStreamMode::Png) ++owner.counters.pngDropped;
    }
    bool Origin(const http::request<http::string_body>& request) const {
        if (request.count(http::field::origin) != 1) return false;
        const std::string origin(request[http::field::origin]);
        return std::find(config.allowedOrigins.begin(), config.allowedOrigins.end(), origin) != config.allowedOrigins.end();
    }
    bool Auth(const http::request<http::string_body>& request) const {
        if (config.authToken.empty()) return true;
        std::string_view bearer(request[http::field::authorization].data(), request[http::field::authorization].size());
        if (bearer.substr(0, 7) == "Bearer " && EqualToken(bearer.substr(7), config.authToken)) return true;
        std::string_view cookies(request[http::field::cookie].data(), request[http::field::cookie].size());
        while (!cookies.empty()) {
            auto end = cookies.find(';'); auto part = cookies.substr(0, end);
            while (!part.empty() && part.front() == ' ') part.remove_prefix(1);
            if (part.substr(0, 8) == "rm_auth=" && EqualToken(part.substr(8), config.authToken)) return true;
            if (end == std::string_view::npos) break;
            cookies.remove_prefix(end+1);
        }
        return false;
    }
    std::string Metrics() {
        auto& c = owner.counters;
        WebSize size; { std::lock_guard<std::mutex> lock(mailbox); size = actual; }
        const auto jpegFrames=c.jpegEncoded.load(), pngFrames=c.pngEncoded.load();
        boost::json::object result{{"version",RENDER_MODULE_WEB_VERSION},{"build",RENDER_MODULE_WEB_BUILD_ID},
            {"protocol",WebProtocolVersion},{"image_protocol",WebImageProtocolVersion},{"backend","web"},
            {"initial_stream",StreamName(config.initialStream)},{"available_streams",AvailableStreams()},
            {"sessions",c.sessions.load()},{"controller",c.controller.load()},
            {"stream_sessions",boost::json::object{{"jpeg",jpegSessions.load()},{"png",pngSessions.load()},{"h264",h264Sessions.load()}}},
            {"framesRendered",c.rendered.load()},{"image_frames_encoded",c.encoded.load()},{"image_frames_dropped",c.dropped.load()},
            {"image_bytes_encoded",c.encodedBytes.load()},{"jpegEncoded",jpegFrames},{"jpegDropped",c.jpegDropped.load()},
            {"pngEncoded",pngFrames},{"pngDropped",c.pngDropped.load()},
            {"png_encoder",pngEncoder},{"png_fpnge_compiled",pngFpngeCompiled},
            {"png_fpnge_cpu_supported",pngFpngeCpuSupported},
            {"png_fpnge_frames",c.pngFpngeFrames.load()},{"png_fpng_frames",c.pngFpngFrames.load()},
            {"png_fallback_frames",c.pngFallbackFrames.load()},
            {"jpeg_encode_ms",jpegFrames?double(c.jpegEncodeMicros.load())/jpegFrames/1000:0},
            {"png_encode_ms",pngFrames?double(c.pngEncodeMicros.load())/pngFrames/1000:0},
            {"bytesTransmitted",c.bytes.load()},{"inputAccepted",c.accepted.load()},{"inputRejected",c.rejected.load()},
            {"inputQueueFull",c.full.load()},{"encodeMicros",c.encodeMicros.load()},
            {"streamSwitchRequests",c.streamSwitchRequests.load()},{"streamSwitches",c.streamSwitches.load()},
            {"streamSwitchFailures",c.streamSwitchFailures.load()},{"width",size.width},{"height",size.height}};
#ifdef RENDER_MODULE_ENABLE_WEBRTC
        if(hub) {
            boost::json::array clients; unsigned connected=0;
            std::uint64_t packets=0,bytes=0,nacks=0,retransmits=0,plis=0,submitted=0,rejected=0,dropped=0;
            for(const auto& s:hub->Snapshot()) {
                const auto& m=*s.counters; connected+=s.connected;
                packets+=m.packets; bytes+=m.bytes; nacks+=m.nacks; retransmits+=m.retransmits; plis+=m.plis;
                submitted+=m.submitted; rejected+=m.rejected; dropped+=m.testDropped;
                clients.push_back({{"session",s.id},{"ssrc",s.ssrc},{"ice_state",s.iceState},{"ice_candidate_type",s.candidateType},
                    {"pending",s.pending},{"rtp_packets_sent",m.packets.load()},{"rtp_bytes_sent",m.bytes.load()},
                    {"rtcp_nacks",m.nacks.load()},{"rtp_retransmits",m.retransmits.load()},{"rtcp_plis",m.plis.load()},
                    {"latest_remb_bps",m.rembBps.load()},{"remb_time_ms",m.rembTimeMs.load()},
                    {"video_frames_submitted",m.submitted.load()},{"video_frames_rejected",m.rejected.load()},
                    {"input_transport","datachannel"},{"input_fast_open",m.fastOpen.load()},{"control_open",m.controlOpen.load()},
                    {"input_enabled",m.inputEnabled.load()},{"input_fast_received",m.fastReceived.load()},
                    {"input_control_received",m.controlReceived.load()},{"input_accepted",m.inputAccepted.load()},
                    {"input_rejected",m.inputRejected.load()},{"input_stale_fast",m.staleFast.load()},
                    {"input_queue_full",m.inputQueueFull.load()},{"input_resyncs",m.inputResyncs.load()},
                    {"input_invalid",m.invalidInput.load()},{"input_fast_coalesced",m.fastCoalesced.load()},
                    {"control_buffered",m.controlBuffered.load()},{"browser_fast_buffered",m.browserFastBuffered.load()},
                    {"browser_control_buffered",m.browserControlBuffered.load()},{"data_max_message_size",m.maxMessageSize.load()},
                    {"input_rtt_ms",double(m.inputRttUs.load())/1000}});
            }
            result["webrtc_sessions"]=clients.size(); result["webrtc_connected"]=connected; result["webrtc_failed"]=hub->Failures();
            result["rtp_packets_sent"]=packets; result["rtp_bytes_sent"]=bytes; result["rtcp_nacks"]=nacks;
            result["rtp_retransmits"]=retransmits; result["rtcp_plis"]=plis; result["video_frames_submitted"]=submitted;
            result["video_frames_rejected_per_client"]=rejected; result["test_packets_dropped"]=dropped;
            result["webrtc_clients"]=std::move(clients);
            const auto e=hub->EncoderMetrics();
            result["encoder"]={{"frames",e.framesEncoded},{"dropped",e.framesDropped},{"errors",e.errors},
                {"readback_ms",e.readback.MeanMs()},{"conversion_ms",e.rgbaToI420.MeanMs()},{"encode_ms",e.encode.MeanMs()},
                {"pending",e.encoderQueueDepth},{"output",e.outputQueueDepth}};
        }
#endif
        return Json(std::move(result));
    }
};
struct WebServer::Impl::Session : std::enable_shared_from_this<Session> {
    Impl& server;
    websocket::stream<beast::tcp_stream> ws;
    beast::flat_buffer buffer;
#ifdef RENDER_MODULE_ENABLE_WEBRTC
    std::shared_ptr<WebRtcSession> media;
    std::unique_ptr<dc::InputState> dataInput;
    Clock::time_point lastPing{}, lastControl=Clock::now(), syncDeadline{};
    std::uint64_t pingUs=0;
    std::uint32_t syncEpoch=0;
    int streamState=-1;
#endif
    bool wantsControl=true;
    Clock::time_point connectedAt=Clock::now(), mediaStartedAt=connectedAt;
    unsigned signalingMessages=0;
    http::request_parser<http::string_body> parser;
    http::response<http::string_body> response;
    std::string id;
    WebInputState state;
    WebStreamMode stream=WebStreamMode::Jpeg, previousImageStream=WebStreamMode::Jpeg;
    std::optional<WebStreamMode> requestedStream;
    std::uint64_t sequence = 0, sentId = 0, offeredId = 0;
    bool ready = false, dead = false, writing = false, awaitingAck = false, demandCounted=false;
    bool closeRequested = false;
    std::deque<std::shared_ptr<const std::string>> controls;
    std::shared_ptr<const std::vector<unsigned char>> pending, inFlight;
    std::uint64_t pendingId = 0;
    WebSize observed;
    Clock::time_point ackDeadline{}, writeDeadline{}, rateStart = Clock::now();
    unsigned messages = 0;
    Session(Impl& s, tcp::socket socket) : server(s), ws(std::move(socket)),
        buffer(WebSignalingLimit+16384), id(RandomId()), stream(s.config.initialStream),
        previousImageStream(IsImageStream(s.config.initialStream)?s.config.initialStream:WebStreamMode::Jpeg) {
        parser.header_limit(8192); parser.body_limit(WebMessageLimit);
        state.input.focused = false;
    }
    void Start() {
        beast::get_lowest_layer(ws).expires_after(std::chrono::seconds(5));
        http::async_read(beast::get_lowest_layer(ws), buffer, parser,
            [self=shared_from_this()](beast::error_code error, std::size_t) {
                if (error) { self->Finish(); return; }
                self->Request();
            });
    }
    void Reply(http::status status, std::string body, const char* contentType = "application/json", bool cookie = false) {
        response = {status, 11}; response.keep_alive(false); Headers(response);
        response.set(http::field::content_type, contentType);
        if (cookie && !server.config.authToken.empty()) {
            std::string value = "rm_auth=" + server.config.authToken + "; Path=/api; HttpOnly; SameSite=Strict";
            if (parser.get()[http::field::origin].starts_with("https://")) value += "; Secure";
            response.set(http::field::set_cookie, value);
        }
        response.body() = std::move(body); response.prepare_payload();
        http::async_write(beast::get_lowest_layer(ws), response,
            [self=shared_from_this()](beast::error_code, std::size_t) { self->Finish(); });
    }
    void Request() {
        const auto& req = parser.get();
        const std::string path(req.target());
        if (path == "/api/ws" && websocket::is_upgrade(req)) {
            if (!server.Origin(req)) { Reply(http::status::forbidden, "{}"); return; }
            if (!server.Auth(req)) { Reply(http::status::unauthorized, "{}"); return; }
            if (server.owner.counters.sessions >= server.config.maxClients ||
                (!server.config.allowMultipleViewers && server.owner.counters.sessions != 0)) {
                Reply(http::status::service_unavailable, "{}"); return;
            }
            // Count pending upgrades too; no parallel handshakes can bypass the cap.
            ready = true; ++server.owner.counters.sessions;
            beast::get_lowest_layer(ws).expires_never();
            ws.read_message_max(WebSignalingLimit);
            ws.set_option(websocket::stream_base::timeout{std::chrono::seconds(3), std::chrono::seconds(15), true});
            ws.async_accept(req, [self=shared_from_this()](beast::error_code error) {
                if (error) { self->Finish(); return; }
                self->demandCounted=true; self->server.AddDemand(self->stream,1);
                self->mediaStartedAt=Clock::now();
                self->server.Elect();
                self->Control(Json({{"type", "welcome"}, {"session", self->id}, {"control", self->server.controller == self.get()},
                    {"stream",StreamName(self->stream)},{"availableStreams",self->server.AvailableStreams()},
                    {"mediaTransport",MediaTransport(self->stream)},{"inputTransport",InputTransport(self->stream)}}));
                self->Read();
            });
            return;
        }
        if (path == "/api/login" && req.method() == http::verb::post) {
            if (!server.Origin(req)) Reply(http::status::forbidden, "{}");
            else if (!server.Auth(req)) Reply(http::status::unauthorized, "{}");
            else Reply(http::status::ok, "{}", "application/json", true);
            return;
        }
        if (req.method() != http::verb::get) { Reply(http::status::method_not_allowed, "{}"); return; }
        if (path == "/healthz") { Reply(http::status::ok, "{\"ok\":true}"); return; }
        if (path == "/api/version") {
            if (!server.Auth(req)) Reply(http::status::unauthorized, "{}");
            else Reply(http::status::ok, server.Metrics());
            return;
        }
        const auto asset = WebAsset(path);
        if (asset.empty()) { Reply(http::status::not_found, "{}"); return; }
        Reply(http::status::ok, std::string(asset), path == "/" ? "text/html; charset=utf-8" :
            (path == "/app.js" || path == "/input_protocol.mjs" || path == "/protocol_keys.mjs") ? "text/javascript; charset=utf-8" : "text/css; charset=utf-8");
    }
    void Control(std::string message) {
        if (dead || closeRequested) return;
        if (controls.size() >= 16) { Finish(); return; }
        controls.push_back(std::make_shared<const std::string>(std::move(message))); Write();
    }
    void StreamChanged() {
        Control(Json({{"type","stream_changed"},{"stream",StreamName(stream)},
            {"mediaTransport",MediaTransport(stream)},{"inputTransport",InputTransport(stream)},
            {"control",server.controller==this}}));
    }
    void StreamError(WebStreamMode target, const char* code) {
        ++server.owner.counters.streamSwitchFailures;
        Control(Json({{"type","stream_error"},{"stream",StreamName(target)},{"code",code}}));
    }
    void StopMedia() {
#ifdef RENDER_MODULE_ENABLE_WEBRTC
        if(dataInput) { dataInput->Close(); dataInput.reset(); }
        if(server.hub) server.hub->Remove(id);
        media.reset(); streamState=-1; syncEpoch=0; pingUs=0;
#endif
    }
    void CommitStream(WebStreamMode target) {
        requestedStream.reset();
        if(target==stream) { StreamChanged(); return; }
        if(!server.Available(target)) { StreamError(target,"unavailable"); return; }
        if(target==WebStreamMode::H264) {
            std::lock_guard<std::mutex> lock(server.mailbox);
            if((server.actual.width && server.actual.width<16) || (server.actual.height && server.actual.height<16)) {
                StreamError(target,"transition_failed"); return;
            }
        }
        const bool transportChange=IsImageStream(stream)!=IsImageStream(target);
        if(transportChange) { server.input->ReleaseAllInput(); state={}; state.input.focused=false; }
        if(stream==WebStreamMode::H264) StopMedia();
        pending.reset(); inFlight.reset(); awaitingAck=false;
        if(demandCounted) { server.AddDemand(stream,-1); server.AddDemand(target,1); }
        stream=target; offeredId=0; sentId=0;
        if(IsImageStream(stream)) previousImageStream=stream;
        else {
            mediaStartedAt=Clock::now(); signalingMessages=0;
#ifdef RENDER_MODULE_ENABLE_WEBRTC
            lastControl=mediaStartedAt;
#endif
        }
        ++server.owner.counters.streamSwitches;
        StreamChanged();
    }
    void RequestStream(WebStreamMode target) {
        ++server.owner.counters.streamSwitchRequests;
        if(!server.Available(target)) { StreamError(target,"unavailable"); return; }
        if(requestedStream) { StreamError(target,"transition_in_progress"); return; }
        if(target==stream) { StreamChanged(); return; }
        pending.reset();
        if(IsImageStream(stream) && awaitingAck) { requestedStream=target; return; }
        CommitStream(target);
    }
    void FailH264(const char* code) {
        if(stream!=WebStreamMode::H264) return;
        StreamError(WebStreamMode::H264,code);
        CommitStream(previousImageStream);
    }
    void Offer(std::shared_ptr<const std::vector<unsigned char>> packet, std::uint64_t frameId) {
        if (dead || closeRequested || !ready || !IsImageStream(stream) || requestedStream || frameId <= offeredId) return;
        offeredId = frameId;
        if (pending) server.DropImage(stream);
        pending = std::move(packet); pendingId = frameId; Write();
    }
    void Write() {
        if (dead || writing || !ready || !ws.is_open()) return;
        if (closeRequested) {
            writeDeadline = Clock::now() + std::chrono::seconds(3);
            writing = true;
            ws.async_close(websocket::close_code::going_away,
                [self=shared_from_this()](beast::error_code) { self->Finish(); });
            return;
        }
        writeDeadline = Clock::now() + std::chrono::seconds(5);
        if (!controls.empty()) {
            auto data = controls.front(); controls.pop_front(); writing = true; ws.text(true);
            ws.async_write(net::buffer(*data), [self=shared_from_this(), data](beast::error_code error, std::size_t bytes) {
                self->writing = false;
                if (error) self->Finish(); else { self->server.owner.counters.bytes += bytes; self->Write(); }
            });
        } else if (pending && !awaitingAck) {
            inFlight = std::move(pending); sentId = pendingId; awaitingAck = true; writing = true; ws.binary(true);
            ackDeadline = Clock::now() + std::chrono::seconds(5);
            auto data = inFlight;
            ws.async_write(net::buffer(*data), [self=shared_from_this(), data](beast::error_code error, std::size_t bytes) {
                self->writing = false; self->inFlight.reset();
                if (error) self->Finish(); else { self->server.owner.counters.bytes += bytes; self->Write(); }
            });
        }
    }
    void Read() {
        ws.async_read(buffer, [self=shared_from_this()](beast::error_code error, std::size_t) {
            if (error) { self->Finish(); return; }
            if (self->dead || self->closeRequested) return;
            const auto now = Clock::now();
            if (now - self->rateStart >= std::chrono::seconds(1)) { self->messages = 0; self->signalingMessages=0; self->rateStart = now; }
            if (!self->ws.got_text() || ++self->messages > 1000) { self->Reject(); return; }
            const auto text = beast::buffers_to_string(self->buffer.data());
            self->buffer.consume(self->buffer.size());
            WebMessage message;
            if (!ParseWebMessage(text, self->server.config, self->state, message) || message.sequence <= self->sequence) {
                self->Reject(); return;
            }
            self->sequence = message.sequence;
            if (message.kind == WebMessage::Kind::SetStream) {
                self->RequestStream(message.stream);
            } else if (message.kind >= WebMessage::Kind::RtcHello) {
                if(self->stream!=WebStreamMode::H264 || self->requestedStream || ++self->signalingMessages>128 ||
                   message.session!=self->id || !self->Signal(message)) { self->Reject(); return; }
            } else if (message.kind == WebMessage::Kind::FrameAck) {
                if (!IsImageStream(self->stream) || !self->awaitingAck || message.frameId != self->sentId) { self->Reject(); return; }
                self->awaitingAck = false;
                if(self->requestedStream) { const auto target=*self->requestedStream; self->CommitStream(target); }
                self->Write();
            } else if (!IsImageStream(self->stream)) {
                // H.264 uses the DataChannel for ordinary input; the WebSocket remains control/signaling only.
                self->Reject(); return;
            } else if (self->server.controller != self.get()) {
                ++self->server.owner.counters.rejected;
                self->Control(Json({{"type", "view_only"}}));
            } else self->Input(message);
            if (!self->dead && !self->closeRequested) self->Read();
        });
    }
    bool Signal(const WebMessage& message) {
#ifdef RENDER_MODULE_ENABLE_WEBRTC
        if(stream!=WebStreamMode::H264 || !server.hub) return false;
        try {
            if(message.kind==WebMessage::Kind::RtcHello) {
                if(media) return false;
                boost::json::array ice;
                for(const auto& s:server.config.iceServers) ice.push_back({{"urls",s.urls},{"username",s.username},{"credential",s.credential}});
                Control(Json({{"type","hello"},{"session",id},{"iceServers",std::move(ice)},
                    {"iceTransportPolicy",server.config.iceRelayOnly?"relay":"all"}}));
                media=server.hub->Create(id);
                if(!media) { FailH264("transition_failed"); return true; }
                dataInput=std::make_unique<dc::InputState>(*server.input,[this](dc::Packet packet) {
                    if(media && !media->SendControl(packet)) media->RejectStream();
                });
                dataInput->SetController(server.controller==this);
                mediaStartedAt=lastControl=Clock::now();
                return true;
            }
            if(!media) return false;
            if(message.kind==WebMessage::Kind::RtcAnswer) return media->SetRemoteDescription(message.sdp);
            if(message.kind==WebMessage::Kind::RtcCandidate) return media->AddRemoteCandidate(message.candidate,message.mid);
            if(message.kind==WebMessage::Kind::RtcComplete) return media->RemoteIceComplete();
        } catch(...) { FailH264("transition_failed"); return true; }
#else
        (void)message;
#endif
        return false;
    }
    void MediaTick() {
#ifdef RENDER_MODULE_ENABLE_WEBRTC
        if(stream!=WebStreamMode::H264) return;
        if(!server.hub) { FailH264("unavailable"); return; }
        if(!media) { if(Clock::now()-mediaStartedAt>std::chrono::seconds(10)) FailH264("transition_failed"); return; }
        if(!media->Healthy()) { FailH264("transition_failed"); return; }
        if(dataInput) {
            const auto before=dataInput->counters;
            const auto stats=media->Snapshot().counters;
            dc::Packet packet;
            for(unsigned n=0;n<65 && media->PollInput(packet);++n) {
                if(!dataInput->Handle(packet)) { Reject(); return; }
                if(packet.type!=dc::Type::PointerMove) lastControl=Clock::now();
                if(packet.type==dc::Type::ClientHello) syncEpoch=0;
                if(packet.type==dc::Type::AcquireControl || packet.type==dc::Type::ReleaseControl) {
                    wantsControl=packet.type==dc::Type::AcquireControl;
                    if(!wantsControl) server.Relinquish(this);
                    server.Elect();
                    dc::Packet reply; reply.type=dc::Type::ControlState; reply.control=server.controller==this;
                    reply.enabled=dataInput->Enabled(); reply.state.epoch=dataInput->Epoch(); media->SendControl(reply);
                } else if((packet.type==dc::Type::ClientHello || packet.type==dc::Type::ViewportRequest) && server.controller==this) {
                    std::lock_guard<std::mutex> lock(server.mailbox);
                    server.requested=server.ConstrainViewport(ClampWebViewport(packet.width,packet.height,server.config)); server.resizePending=true;
                } else if(packet.type==dc::Type::BrowserStats) {
                    stats->browserFastBuffered=packet.fastBuffered; stats->browserControlBuffered=packet.controlBuffered;
                } else if(packet.type==dc::Type::Pong && pingUs && packet.echoUs==pingUs) {
                    stats->inputRttUs=std::chrono::duration_cast<std::chrono::microseconds>(Clock::now()-lastPing).count(); pingUs=0;
                }
            }
            const auto& after=dataInput->counters;
            server.owner.counters.accepted+=after.accepted-before.accepted;
            server.owner.counters.rejected+=after.rejected-before.rejected;
            server.owner.counters.full+=after.queueFull-before.queueFull;
            stats->fastReceived+=after.fastReceived-before.fastReceived; stats->controlReceived+=after.controlReceived-before.controlReceived;
            stats->inputAccepted+=after.accepted-before.accepted; stats->inputRejected+=after.rejected-before.rejected;
            stats->inputQueueFull+=after.queueFull-before.queueFull; stats->inputResyncs+=after.resyncs-before.resyncs;
            stats->staleFast+=after.staleFast-before.staleFast; stats->inputEnabled=dataInput->Enabled();
            if(!dataInput->Greeted() && Clock::now()-mediaStartedAt>std::chrono::seconds(15)) { FailH264("transition_failed"); return; }
            if(dataInput->Greeted() && Clock::now()-lastControl>std::chrono::seconds(5)) { FailH264("transition_failed"); return; }
            if(dataInput->Epoch()!=syncEpoch) { syncEpoch=dataInput->Epoch(); syncDeadline=Clock::now()+std::chrono::seconds(5); }
            if(dataInput->Greeted() && dataInput->Controller() && !dataInput->Enabled() && Clock::now()>syncDeadline) { FailH264("transition_failed"); return; }
            const int currentStream=media->Snapshot().connected?1:0;
            if(dataInput->Greeted() && streamState!=currentStream) {
                streamState=currentStream; dc::Packet state; state.type=dc::Type::StreamState; state.code=currentStream; media->SendControl(state);
            }
            if(dataInput->Greeted() && Clock::now()-lastPing>=std::chrono::seconds(1) &&
               (!pingUs || Clock::now()-lastPing>=std::chrono::seconds(5))) {
                lastPing=Clock::now(); pingUs=std::chrono::duration_cast<std::chrono::microseconds>(lastPing.time_since_epoch()).count();
                dc::Packet ping; ping.type=dc::Type::Ping; ping.echoUs=pingUs; media->SendControl(ping);
            }
        }
        RtcSignal signal;
        // Bound the WS control queue even if all interfaces finish ICE together.
        while(controls.size()<8 && media->Poll(signal)) {
            boost::json::object out{{"type",signal.type},{"session",id}};
            if(signal.type=="offer") out["sdp"]=signal.text;
            else if(signal.type=="ice-candidate") { out["candidate"]=signal.text; out["mid"]=signal.mid; }
            else if(signal.type=="webrtc-state") out["state"]=signal.text;
            Control(Json(std::move(out)));
        }
#endif
    }
    void Input(const WebMessage& message) {
        auto& c = server.owner.counters;
        state = message.state;
        if (message.kind == WebMessage::Kind::Viewport) {
            std::lock_guard<std::mutex> lock(server.mailbox);
            server.requested = server.ConstrainViewport(message.viewport); server.resizePending = true;
        } else if (message.kind == WebMessage::Kind::Release) {
            server.input->ReleaseAllInput();
            state = {}; state.input.focused = false; ++c.accepted;
        } else {
            for (const auto& event : message.events) {
                const auto result = server.input->Enqueue(event);
                if (result.Accepted()) { ++c.accepted; continue; }
                ++c.rejected;
                if (result.status == EnqueueStatus::Full) {
                    ++c.full;
                    const bool movementOnly = std::all_of(message.events.begin(), message.events.end(), [](const auto& e) {
                        return std::holds_alternative<MouseMove>(e) || std::holds_alternative<MouseSource>(e);
                    });
                    if (movementOnly && std::holds_alternative<MouseMove>(event)) continue;
                }
                server.input->ReleaseAllInput();
                state = {}; state.input.focused = false;
                Control(Json({{"type", "input_reset"}})); break;
            }
        }
    }
    void NotifyViewport(WebSize size) {
        if(!size.width || (observed.width==size.width && observed.height==size.height)) return;
#ifdef RENDER_MODULE_ENABLE_WEBRTC
        if(stream==WebStreamMode::H264) {
            if(!dataInput || !dataInput->Greeted() || !media) return;
            dc::Packet packet; packet.type=dc::Type::ViewportAccepted; packet.width=size.width; packet.height=size.height;
            if(!media->SendControl(packet)) return;
        } else
#endif
            Control(Json({{"type","viewport_accepted"},{"width",size.width},{"height",size.height}}));
        observed=size;
    }
    void Reject() { ++server.owner.counters.rejected; Close(); }
    void Close() {
        if (dead || closeRequested) return;
        closeRequested = true;
        StopMedia();
        if (server.controller == this) { server.Relinquish(this); server.Elect(); }
        pending.reset(); controls.clear(); Write();
        if (!ready) Finish();
    }
    void Finish() {
        if (dead) return;
        dead = true; StopMedia();
        beast::error_code error;
        beast::get_lowest_layer(ws).socket().shutdown(tcp::socket::shutdown_both, error);
        beast::get_lowest_layer(ws).socket().close(error);
        if(demandCounted) { server.AddDemand(stream,-1); demandCounted=false; }
        if (ready) { ready = false; --server.owner.counters.sessions; }
        server.Remove(this);
    }
};
void WebServer::Impl::Relinquish(Session* session) {
    if (controller != session) return;
#ifdef RENDER_MODULE_ENABLE_WEBRTC
    if(session->dataInput) session->dataInput->SetController(false);
#endif
    controller = nullptr; input->ReleaseAllInput();
    std::lock_guard<std::mutex> lock(mailbox);
    resizePending = false; // Unconsumed requests cannot outlive their controller lease.
}
void WebServer::Impl::Remove(Session* session) {
    Relinquish(session);
    sessions.remove_if([&](const auto& s) { return s.get() == session; });
    Elect();
}
void WebServer::Impl::Elect() {
    if (!controller && !stopping) {
        for (const auto& session : sessions) if (session->ready && session->ws.is_open() && session->wantsControl && !session->dead && !session->closeRequested) {
            controller = session.get(); input->ReleaseAllInput();
#ifdef RENDER_MODULE_ENABLE_WEBRTC
            if(session->stream==WebStreamMode::H264) { if(session->dataInput) session->dataInput->SetController(true); }
            else
#endif
                session->Control(Json({{"type", "lease"}, {"control", true}}));
            break;
        }
    }
    owner.counters.controller = controller != nullptr;
}
void WebServer::Impl::Accept() {
    acceptor.async_accept([this](beast::error_code error, tcp::socket socket) {
        if (stopping) return;
        if (!error && sessions.size() < config.maxClients*4) {
            try {
                socket.set_option(net::socket_base::send_buffer_size(65536));
                auto session = std::make_shared<Session>(*this, std::move(socket));
                sessions.push_back(session); session->Start();
            } catch (const std::exception&) { /* Bounded rejection; never log request/token contents. */ }
        }
        Accept();
    });
}
void WebServer::Impl::Tick() {
    timer.expires_after(std::chrono::milliseconds(20));
    timer.async_wait([this](beast::error_code error) {
        if (error || stopping) return;
        PublishedImage jpeg,png; WebSize size;
        {
            std::lock_guard<std::mutex> lock(mailbox);
            jpeg=std::move(latestJpeg); png=std::move(latestPng); size=actual;
            latestJpeg={}; latestPng={};
        }
        auto live = sessions; // Bounded snapshot: callbacks may remove sessions.
        for (const auto& s : live) {
            if (!s->ready || s->dead || !s->ws.is_open()) continue;
            if ((s->writing && Clock::now() > s->writeDeadline) ||
                (s->awaitingAck && Clock::now() > s->ackDeadline)) { s->Finish(); continue; }
            s->NotifyViewport(size);
            s->MediaTick();
            if(s->stream==WebStreamMode::Jpeg && jpeg.packet) s->Offer(jpeg.packet,jpeg.id);
            else if(s->stream==WebStreamMode::Png && png.packet) s->Offer(png.packet,png.id);
        }
        Tick();
    });
}
WebServer::WebServer(WebConfig config, std::shared_ptr<RemoteInputQueue> input, std::shared_ptr<video::VideoPipeline> video)
    : impl_(std::make_unique<Impl>(*this, std::move(config), std::move(input), std::move(video))) {}
WebServer::~WebServer() { Stop(); }
bool WebServer::Start() {
    auto& s = *impl_; auto& c = s.config;
    try {
        if(c.initialStream!=WebStreamMode::Jpeg && c.initialStream!=WebStreamMode::Png && c.initialStream!=WebStreamMode::H264) return false;
#ifdef RENDER_MODULE_ENABLE_WEBRTC
        if(!ValidateIceConfiguration(c)) return false;
        if(s.video) {
            try { s.hub=std::make_unique<EncodedFrameHub>(s.video,c); }
            catch(...) { s.hub.reset(); }
        }
        if(c.initialStream==WebStreamMode::H264 && !s.hub) return false;
#else
        if(c.initialStream==WebStreamMode::H264) return false;
#endif
        const auto address = net::ip::make_address(c.bindAddress);
        if (!s.input || c.maxClients < 1 || c.maxClients > 32 || c.maxWidth < 1 || c.maxHeight < 1 ||
            c.maxWidth > 2048 || c.maxHeight > 2048 || c.jpegQuality<1 || c.jpegQuality>100 ||
            !std::isfinite(c.fps) || c.fps < 1 || c.fps > 30) return false;
        if (!c.authToken.empty() && (c.authToken.size() < 16 || c.authToken.size() > 128 ||
            c.authToken.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_") != std::string::npos)) return false;
        if (!address.is_loopback() && ((c.authToken.empty() && !c.allowUnauthenticatedPublicBind) || c.allowedOrigins.empty())) return false;
        s.acceptor.open(address.is_v4() ? tcp::v4() : tcp::v6());
        s.acceptor.set_option(net::socket_base::reuse_address(true));
        s.acceptor.bind({address, c.port}); s.acceptor.listen(32);
        s.port = s.acceptor.local_endpoint().port();
        if (c.allowedOrigins.empty()) {
            for (const auto* host : {"127.0.0.1", "localhost", "[::1]"})
                c.allowedOrigins.push_back(std::string("http://") + host + (s.port == 80 ? "" : ":" + std::to_string(s.port)));
        }
        for (const auto& origin : c.allowedOrigins)
            if (origin.size() > 512 || (origin.rfind("http://", 0) && origin.rfind("https://", 0)) ||
                origin.find_first_of("\r\n* \t") != std::string::npos) return false;
        s.Accept(); s.Tick();
        s.thread = std::thread([&s] {
            try { s.io.run(); } catch (const std::exception&) {
                s.stopping = true; s.input->ReleaseAllInput();
                while (!s.sessions.empty()) { auto session = s.sessions.front(); session->Finish(); }
                beast::error_code error; s.acceptor.close(error); s.io.stop();
                std::fprintf(stderr, "RenderModule Web: network loop failed.\n");
            }
        });
        return true;
    } catch (const std::exception&) { return false; }
}
void WebServer::Stop() {
    auto& s = *impl_;
    if (!s.thread.joinable()) return;
    net::post(s.io, [&s] {
        s.stopping = true; s.input->ReleaseAllInput();
        beast::error_code error; s.acceptor.close(error); s.timer.cancel();
        auto live = s.sessions;
        for (const auto& session : live) session->Close();
        s.shutdownTimer.expires_after(std::chrono::milliseconds(500));
        s.shutdownTimer.async_wait([&s](beast::error_code) {
            auto remaining = s.sessions;
            for (const auto& session : remaining) session->Finish();
            s.io.stop();
        });
    });
    s.thread.join();
    s.sessions.clear();
}
unsigned WebServer::Port() const { return impl_->port; }
WebStreamDemand WebServer::Demand() const {
    return {impl_->jpegSessions.load()!=0,impl_->pngSessions.load()!=0,impl_->h264Sessions.load()!=0};
}
bool WebServer::HasH264() const { return impl_->Available(WebStreamMode::H264); }
bool WebServer::TakeViewport(WebSize& size) {
    std::lock_guard<std::mutex> lock(impl_->mailbox);
    if (!impl_->resizePending) return false;
    size = impl_->requested; impl_->resizePending = false; return true;
}
void WebServer::ObserveViewport(WebSize size) {
    std::lock_guard<std::mutex> lock(impl_->mailbox); impl_->actual = size;
}
void WebServer::SetPngEncoderInfo(std::string name, bool fpngeCompiled, bool fpngeCpuSupported) {
    auto& s = *impl_;
    if (s.thread.joinable()) return;
    s.pngEncoder = std::move(name);
    s.pngFpngeCompiled = fpngeCompiled;
    s.pngFpngeCpuSupported = fpngeCpuSupported;
}
void WebServer::PublishImage(ImageCodec codec, std::shared_ptr<const std::vector<unsigned char>> packet, std::uint64_t id) {
    if (!packet || packet->size() > WebFrameLimit+28 || (codec!=ImageCodec::Jpeg && codec!=ImageCodec::Png)) return;
    std::lock_guard<std::mutex> lock(impl_->mailbox);
    auto& latest=codec==ImageCodec::Jpeg?impl_->latestJpeg:impl_->latestPng;
    if (id <= latest.id) return;
    if (latest.packet) {
        ++counters.dropped;
        if(codec==ImageCodec::Jpeg) ++counters.jpegDropped; else ++counters.pngDropped;
    }
    latest.packet=std::move(packet); latest.id=id;
}

} // namespace render_module::detail
