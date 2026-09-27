#include "web_presenter.hpp"
#include "image_presenter.hpp"
#include "core/render_output.hpp"
#include "web/web_server.hpp"
#include "web/image_encoder.hpp"
#ifdef RENDER_MODULE_ENABLE_WEBRTC
#include "video/video_capture.hpp"
#endif
#include <chrono>
#include <cstdio>
#include <cstdlib>

namespace render_module::detail {
namespace {
std::shared_ptr<video::VideoPipeline> MakeWebVideo(const Config& config) {
#ifdef RENDER_MODULE_ENABLE_WEBRTC
    if(config.web.transport==WebTransport::WebRtc) {
        auto pipeline=std::make_shared<video::VideoPipeline>();
        video::EncoderConfig c; c.fps=config.web.fps;
        if(!video::NormalizeSize(config.width,config.height,c.width,c.height) || !pipeline->Configure(c)) return {};
        return pipeline;
    }
#else
    (void)config;
#endif
    return {};
}
class WebPresenter final : public IPresenter {
public:
    WebPresenter(const Config& config, std::shared_ptr<RemoteInputQueue> input)
        : pipeline_(MakeWebVideo(config)), server_(config.web, std::move(input), pipeline_),
          transport_(config.web.transport), encoder_(config.web.imageCodec,config.web.jpegQuality,WebFrameLimit), fps_(config.web.fps) {
#ifdef RENDER_MODULE_ENABLE_WEBRTC
        if(pipeline_) capture_=std::make_unique<VideoCapture>(pipeline_);
#endif
    }
    bool Start(const Config& config) {
        if(transport_==WebTransport::WebRtc && (config.width<16 || config.height<16 ||
           (config.width&1) || (config.height&1))) return false;
        if (config.width > config.web.maxWidth || config.height > config.web.maxHeight || !server_.Start()) return false;
        server_.ObserveViewport({config.width, config.height});
        const std::string host = config.web.bindAddress.find(':') == std::string::npos ?
            config.web.bindAddress : "[" + config.web.bindAddress + "]";
        std::fprintf(stderr,"RenderModule Web backend (%s; input over %s)\n"
            "  Size : %dx%d\n  HTTP : http://%s:%u/\n  Auth : %s\n  WebSocket image codec : %s\n",
            transport_==WebTransport::WebRtc?"WebRTC H.264":"WebSocket image",
            transport_==WebTransport::WebRtc?"DataChannels":"WebSocket",
            config.width,config.height,host.c_str(),server_.Port(),
            config.web.authToken.empty()?"disabled":"enabled",
            transport_==WebTransport::WebRtc?"inactive":(encoder_.Codec()==ImageCodec::Png?"PNG":"JPEG"));
        return true;
    }
    bool PrepareFrame() override {
        WebSize size;
        return !server_.TakeViewport(size) || RequestVirtualDisplaySize(size.width, size.height);
    }
    bool Present(const PresentedFrame& frame) override {
        if (!frame.IsValid()) return false;
        ++server_.counters.rendered;
        server_.ObserveViewport({frame.width, frame.height});
        const auto now = Clock::now();
        if (!server_.HasViewers() || now < nextEncode_) { glFlush(); return true; }
        const auto period = std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(1.0/fps_));
        nextEncode_ += period;
        if (nextEncode_ <= now) nextEncode_ = now + period; // Fixed cadence, no catch-up bursts.
#ifdef RENDER_MODULE_ENABLE_WEBRTC
        if(transport_==WebTransport::WebRtc) return capture_ && capture_->Submit(frame);
#endif
        ImageRgba image;
        if(!ImagePresenter::Read(frame,image)) return false; // Sole Phase 3 GL→top-down flip.
        EncodedImage encoded;
        if(!encoder_.Encode(image,frame.frameId,encoded)) {
            ++server_.counters.dropped; return true; // Includes the explicit 16 MiB encoded limit.
        }
        const auto payloadBytes=encoded.bytes.size();
        auto packet=std::make_shared<const std::vector<unsigned char>>(PackImage(encoded));
        if(packet->empty()) { ++server_.counters.dropped; return true; }
        ++server_.counters.encoded; server_.counters.encodedBytes+=payloadBytes;
        server_.counters.encodeMicros+=std::chrono::duration_cast<std::chrono::microseconds>(Clock::now()-now).count();
        server_.Publish(std::move(packet),frame.frameId);
        return true;
    }
private:
    using Clock = std::chrono::steady_clock;
    std::shared_ptr<video::VideoPipeline> pipeline_;
    WebServer server_;
#ifdef RENDER_MODULE_ENABLE_WEBRTC
    std::unique_ptr<VideoCapture> capture_;
#endif
    WebTransport transport_;
    ImageEncoder encoder_;
    double fps_;
    Clock::time_point nextEncode_{};
};
} // namespace
std::unique_ptr<IPresenter> CreateWebPresenter(const Config& config, std::shared_ptr<RemoteInputQueue> input) {
    auto presenter = std::make_unique<WebPresenter>(config, std::move(input));
    if (!presenter->Start(config)) {
        std::fprintf(stderr, "RenderModule Web: invalid configuration or server startup failed.\n");
        return nullptr;
    }
    return presenter;
}
} // namespace render_module::detail
