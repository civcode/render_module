#include "web_presenter.hpp"
#include "image_presenter.hpp"
#include "core/render_output.hpp"
#include "web/web_server.hpp"
#include "web/jpeg_encode.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>

namespace render_module::detail {
namespace {
class WebPresenter final : public IPresenter {
public:
    WebPresenter(const Config& config, std::shared_ptr<RemoteInputQueue> input)
        : server_(config.web, std::move(input)), quality_(config.web.jpegQuality), fps_(config.web.fps) {}
    bool Start(const Config& config) {
        if (config.width > config.web.maxWidth || config.height > config.web.maxHeight || !server_.Start()) return false;
        server_.ObserveViewport({config.width, config.height});
        const std::string host = config.web.bindAddress.find(':') == std::string::npos ?
            config.web.bindAddress : "[" + config.web.bindAddress + "]";
        std::fprintf(stderr, "RenderModule Web backend (TEMPORARY JPEG/WebSocket transport)\n"
            "  Size : %dx%d\n  HTTP : http://%s:%u/\n  Auth : %s\n",
            config.width, config.height, host.c_str(), server_.Port(),
            config.web.authToken.empty() ? "disabled" : "enabled");
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
        ImageRgba image;
        if (!ImagePresenter::Read(frame, image)) return false; // Reuses the sole Phase 3 output flip.
        unsigned char* bytes = nullptr; unsigned long size = 0;
        if (!rm_encode_jpeg(image.pixels.data(), image.width, image.height, quality_, WebFrameLimit, &bytes, &size)) return false;
        std::unique_ptr<unsigned char, decltype(&std::free)> owned(bytes, &std::free);
        auto packet = std::make_shared<const std::vector<unsigned char>>(
            PackJpeg(frame.frameId, image.width, image.height, bytes, size));
        if (packet->empty()) { ++server_.counters.dropped; return true; }
        ++server_.counters.encoded;
        server_.counters.encodeMicros += std::chrono::duration_cast<std::chrono::microseconds>(Clock::now()-now).count();
        server_.Publish(std::move(packet), frame.frameId);
        return true;
    }
private:
    using Clock = std::chrono::steady_clock;
    WebServer server_;
    int quality_;
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
