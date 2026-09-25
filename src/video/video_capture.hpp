#pragma once
#include "render_module/video.hpp"
#include "present/image_presenter.hpp"

namespace render_module::detail {
// Render-thread-only bridge. The transport-independent video library has no GL dependency.
class VideoCapture {
public:
    explicit VideoCapture(std::shared_ptr<video::VideoPipeline> pipeline) : pipeline_(std::move(pipeline)) {}
    bool Submit(const PresentedFrame& frame) {
        if (!frame.IsValid()) return false;
        if (generation_ != frame.framebufferGeneration || width_ != frame.width || height_ != frame.height) {
            auto config = pipeline_->Configuration();
            if (!video::NormalizeSize(frame.width,frame.height,config.width,config.height)) return false;
            config.framebufferGeneration = frame.framebufferGeneration;
            if (!pool_.Configure(frame.width,frame.height) || !pipeline_->Configure(config)) return false;
            generation_ = frame.framebufferGeneration; width_ = frame.width; height_ = frame.height;
        }
        video::VideoFrame cpu; std::uint8_t* data = nullptr;
        if (!pool_.Acquire(cpu,data)) { pipeline_->RecordReadback(0,true); return true; }
        const auto started = std::chrono::steady_clock::now();
        if (!ImagePresenter::ReadInto(frame,data,cpu.storage->size(),cpu.planes[0].stride)) return false;
        pipeline_->RecordReadback(std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now()-started).count());
        cpu.frameId = frame.frameId; cpu.framebufferGeneration = frame.framebufferGeneration; cpu.ptsUs = clock_.Next();
        return pipeline_->Submit(std::move(cpu));
    }
private:
    std::shared_ptr<video::VideoPipeline> pipeline_;
    video::RgbaFramePool pool_;
    video::VideoClock clock_;
    std::uint64_t generation_ = 0;
    int width_ = 0, height_ = 0;
};
} // namespace render_module::detail
