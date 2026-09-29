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
const char* StreamName(WebStreamMode stream) {
    switch(stream) {
        case WebStreamMode::Jpeg: return "JPEG";
        case WebStreamMode::Png: return "PNG";
        case WebStreamMode::H264: return "H.264";
    }
    return "unknown";
}
std::shared_ptr<video::VideoPipeline> MakeWebVideo(const Config& config) {
#ifdef RENDER_MODULE_ENABLE_WEBRTC
    auto pipeline=std::make_shared<video::VideoPipeline>();
    video::EncoderConfig c; c.fps=config.web.fps;
    if(!video::NormalizeSize(config.width,config.height,c.width,c.height) || !pipeline->Configure(c)) return {};
    return pipeline;
#else
    (void)config;
    return {};
#endif
}
class WebPresenter final : public IPresenter {
public:
    WebPresenter(const Config& config, std::shared_ptr<RemoteInputQueue> input)
        : pipeline_(MakeWebVideo(config)), server_(config.web, std::move(input), pipeline_),
          jpegEncoder_(ImageCodec::Jpeg,config.web.jpegQuality,WebFrameLimit),
          pngEncoder_(ImageCodec::Png,config.web.jpegQuality,WebFrameLimit), fps_(config.web.fps) {
        server_.SetPngEncoderInfo(pngEncoder_.PngEncoderBackendName(),
                                  PngFpngeCompiled(), PngFpngeCpuSupported());
#ifdef RENDER_MODULE_ENABLE_WEBRTC
        if(pipeline_) capture_=std::make_unique<VideoCapture>(pipeline_);
#endif
    }
    bool Start(const Config& config) {
        if (config.width > config.web.maxWidth || config.height > config.web.maxHeight || !server_.Start()) return false;
        server_.ObserveViewport({config.width, config.height});
        const std::string host = config.web.bindAddress.find(':') == std::string::npos ?
            config.web.bindAddress : "[" + config.web.bindAddress + "]";
        std::fprintf(stderr,"RenderModule Web backend\n"
            "  Size            : %dx%d\n  HTTP            : http://%s:%u/\n  Auth            : %s\n"
            "  Initial stream  : %s\n  Streams         : JPEG, PNG%s\n  JPEG quality    : %d\n"
            "  PNG encoder     : %s\n",
            config.width,config.height,host.c_str(),server_.Port(),
            config.web.authToken.empty()?"disabled":"enabled", StreamName(config.web.initialStream),
            server_.HasH264()?", H.264":"", config.web.jpegQuality,
            pngEncoder_.PngEncoderBackendName());
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
        const auto demand = server_.Demand();
        if ((!demand.jpeg && !demand.png && !demand.h264) || now < nextEncode_) { glFlush(); return true; }
        const auto period = std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(1.0/fps_));
        nextEncode_ += period;
        if (nextEncode_ <= now) nextEncode_ = now + period; // Fixed cadence, no catch-up bursts.
#ifdef RENDER_MODULE_ENABLE_WEBRTC
        if(demand.h264 && (!capture_ || !capture_->Submit(frame))) return false;
#endif
        if(!demand.jpeg && !demand.png) return true;
        ImageRgba image;
        if(!ImagePresenter::Read(frame,image)) return false; // One image readback shared by JPEG and PNG.
        if(demand.jpeg) EncodeAndPublish(jpegEncoder_,image,frame.frameId);
        if(demand.png) EncodeAndPublish(pngEncoder_,image,frame.frameId);
        return true;
    }
private:
    bool EncodeAndPublish(const ImageEncoder& encoder, const ImageRgba& image, std::uint64_t frameId) {
        const auto started=Clock::now();
        EncodedImage encoded;
        if(!encoder.Encode(image,frameId,encoded)) {
            ++server_.counters.dropped;
            if(encoder.Codec()==ImageCodec::Jpeg) ++server_.counters.jpegDropped;
            else ++server_.counters.pngDropped;
            return false;
        }
        const auto elapsed=std::chrono::duration_cast<std::chrono::microseconds>(Clock::now()-started).count();
        const auto payloadBytes=encoded.bytes.size();
        auto packet=std::make_shared<const std::vector<unsigned char>>(PackImage(encoded));
        if(packet->empty()) {
            ++server_.counters.dropped;
            if(encoder.Codec()==ImageCodec::Jpeg) ++server_.counters.jpegDropped;
            else ++server_.counters.pngDropped;
            return false;
        }
        ++server_.counters.encoded; server_.counters.encodedBytes+=payloadBytes; server_.counters.encodeMicros+=elapsed;
        if(encoder.Codec()==ImageCodec::Jpeg) {
            ++server_.counters.jpegEncoded; server_.counters.jpegEncodedBytes+=payloadBytes; server_.counters.jpegEncodeMicros+=elapsed;
        } else {
            ++server_.counters.pngEncoded; server_.counters.pngEncodedBytes+=payloadBytes; server_.counters.pngEncodeMicros+=elapsed;
            if(encoded.pngBackend==PngBackend::Fpnge) ++server_.counters.pngFpngeFrames;
            else ++server_.counters.pngFpngFrames;
            if(encoded.pngFellBack) ++server_.counters.pngFallbackFrames;
        }
        server_.PublishImage(encoder.Codec(),std::move(packet),frameId);
        return true;
    }
    using Clock = std::chrono::steady_clock;
    std::shared_ptr<video::VideoPipeline> pipeline_;
    WebServer server_;
#ifdef RENDER_MODULE_ENABLE_WEBRTC
    std::unique_ptr<VideoCapture> capture_;
#endif
    ImageEncoder jpegEncoder_, pngEncoder_;
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
