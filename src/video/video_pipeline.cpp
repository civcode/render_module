#include "render_module/video.hpp"
#include <algorithm>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <stdexcept>

namespace render_module::video {
#ifndef RENDER_MODULE_HAS_OPENH264
std::unique_ptr<IVideoEncoder> CreateOpenH264Encoder() { return {}; }
#endif
namespace {
using Clock = std::chrono::steady_clock;
std::uint64_t Micros(Clock::time_point start) {
    return std::chrono::duration_cast<std::chrono::microseconds>(Clock::now()-start).count();
}
void Add(Timing& timing, std::uint64_t us) {
    ++timing.samples; timing.totalUs += us; timing.worstUs = std::max(timing.worstUs, us);
}
}
struct VideoPipeline::Impl {
    mutable std::mutex mutex;
    std::condition_variable wake;
    std::unique_ptr<IVideoEncoder> encoder;
    I420Converter converter;
    EncoderConfig desired;
    VideoFrame pending;
    EncodedFrame output;
    VideoMetrics metrics;
    std::uint64_t epoch = 0, applied = 0, lastId = 0;
    std::int64_t lastPts = -1;
    bool stop = false, busy = false, failed = false, key = false, bitrate = false;
    std::thread worker;
    explicit Impl(std::unique_ptr<IVideoEncoder> e) : encoder(std::move(e)), worker([this] { Run(); }) {}
    void Run() {
        std::unique_lock<std::mutex> lock(mutex);
        try {
            while (!stop) {
                wake.wait(lock, [&] { return stop || epoch != applied || key || bitrate ||
                    (!failed && pending.storage && !output.storage); });
                if (stop) break;
                const auto version = epoch;
                if (epoch != applied) {
                    const auto config = desired; busy = true;
                    lock.unlock();
                    const bool ok = encoder && converter.Configure(config.width,config.height) && encoder->Configure(config);
                    lock.lock(); busy = false; applied = version;
                    if (version != epoch) continue;
                    failed = !ok;
                    if (failed) { ++metrics.errors; if (pending.storage) { ++metrics.framesDropped; pending = {}; } }
                    else metrics.effectiveBitrate = encoder->TargetBitrate();
                    wake.notify_all();
                }
                if (!epoch || failed) { key = bitrate = false; continue; }
                const bool force = key, change = bitrate; const auto bps = desired.bitrate;
                key = bitrate = false;
                if (force || change) {
                    busy = true; lock.unlock();
                    if (force) encoder->ForceKeyframe();
                    const bool ok = !change || encoder->SetTargetBitrate(bps);
                    const auto effective = encoder->TargetBitrate();
                    lock.lock(); busy = false;
                    if (!ok) ++metrics.errors;
                    metrics.effectiveBitrate = effective;
                    if (version != epoch) continue;
                }
                if (!pending.storage || output.storage) { wake.notify_all(); continue; }
                VideoFrame input = std::move(pending); pending = {}; busy = true;
                lock.unlock();
                VideoFrame i420; const auto conversionStart = Clock::now();
                const bool converted = converter.Convert(input,i420);
                const auto conversionUs = Micros(conversionStart);
                const auto encodeStartUs = Clock::now();
                const auto result = converted ? encoder->Encode(i420) : EncodeResult::Invalid;
                const auto encodeUs = Micros(encodeStartUs);
                EncodedFrame encoded;
                if (result == EncodeResult::Produced && !encoder->TryReceive(encoded))
                    throw std::runtime_error("encoder output contract");
                lock.lock(); busy = false;
                Add(metrics.rgbaToI420,conversionUs);
                if (converted && result != EncodeResult::Backpressure && result != EncodeResult::Invalid)
                    Add(metrics.encode,encodeUs);
                if (result == EncodeResult::Produced) {
                    ++metrics.framesEncoded; metrics.bytesEncoded += encoded.size;
                    metrics.keyframes += encoded.keyframe;
                }
                if (version != epoch) ++metrics.framesDropped;
                else if (result == EncodeResult::Produced) output = std::move(encoded);
                else if (result == EncodeResult::Backpressure) {
                    if (!pending.storage) pending = std::move(input); else ++metrics.framesDropped;
                    // A consumer may retain all codec output pool slots. Retry without
                    // spinning, and let newer raw submissions replace this input.
                    wake.wait_for(lock,std::chrono::milliseconds(5));
                } else {
                    ++metrics.framesDropped;
                    if (result != EncodeResult::Skipped) ++metrics.errors;
                }
                wake.notify_all();
            }
        } catch (...) {
            if (!lock.owns_lock()) lock.lock();
            ++metrics.errors; failed = true; busy = false;
            if (pending.storage) { ++metrics.framesDropped; pending = {}; }
            stop = true; wake.notify_all();
        }
    }
};
VideoPipeline::VideoPipeline(std::unique_ptr<IVideoEncoder> e) : impl_(std::make_unique<Impl>(std::move(e))) {}
VideoPipeline::~VideoPipeline() {
    { std::lock_guard<std::mutex> lock(impl_->mutex); impl_->stop = true; impl_->pending = {}; }
    impl_->wake.notify_all(); impl_->worker.join();
}
bool VideoPipeline::Configure(const EncoderConfig& c) {
    if (!Validate(c)) return false;
    auto& s = *impl_; std::lock_guard<std::mutex> lock(s.mutex);
    if (s.stop || !s.encoder) return false;
    if (s.pending.storage) ++s.metrics.framesDropped;
    if (s.output.storage) ++s.metrics.framesDropped;
    s.pending = {}; s.output = {}; s.desired = c; ++s.epoch; s.failed = false;
    s.wake.notify_all(); return true;
}
EncoderConfig VideoPipeline::Configuration() const {
    std::lock_guard<std::mutex> lock(impl_->mutex); return impl_->desired;
}
bool VideoPipeline::Submit(VideoFrame f) {
    if (!Validate(f) || f.format != PixelFormat::RGBA8) return false;
    auto& s = *impl_; std::lock_guard<std::mutex> lock(s.mutex);
    if (!s.epoch || s.stop || s.failed || f.framebufferGeneration != s.desired.framebufferGeneration ||
        f.width < s.desired.width || f.width > s.desired.width+1 ||
        f.height < s.desired.height || f.height > s.desired.height+1 ||
        f.ptsUs <= s.lastPts || f.frameId <= s.lastId) return false;
    s.lastPts = f.ptsUs; s.lastId = f.frameId; ++s.metrics.framesSubmitted;
    if (s.pending.storage) ++s.metrics.framesDropped;
    s.pending = std::move(f); s.wake.notify_all(); return true;
}
bool VideoPipeline::TryReceive(EncodedFrame& frame) {
    auto& s = *impl_; std::lock_guard<std::mutex> lock(s.mutex);
    if (!s.output.storage) return false;
    frame = std::move(s.output); s.output = {}; s.wake.notify_all(); return true;
}
void VideoPipeline::SetTargetBitrate(std::uint32_t bps) {
    auto& s = *impl_; std::lock_guard<std::mutex> lock(s.mutex);
    s.desired.bitrate = std::clamp(bps,s.desired.minBitrate,s.desired.maxBitrate);
    s.bitrate = true; s.wake.notify_all();
}
void VideoPipeline::ForceKeyframe() {
    std::lock_guard<std::mutex> lock(impl_->mutex); impl_->key = true; impl_->wake.notify_all();
}
bool VideoPipeline::Flush(std::chrono::milliseconds timeout) {
    auto& s = *impl_; std::unique_lock<std::mutex> lock(s.mutex);
    return s.wake.wait_for(lock,timeout,[&] { return s.stop || s.failed ||
        (!s.pending.storage && !s.busy && s.epoch == s.applied && !s.key && !s.bitrate); }) && !s.failed && !s.stop;
}
VideoMetrics VideoPipeline::Metrics() const {
    std::lock_guard<std::mutex> lock(impl_->mutex); auto result = impl_->metrics;
    result.encoderQueueDepth = bool(impl_->pending.storage);
    result.outputQueueDepth = bool(impl_->output.storage); return result;
}
void VideoPipeline::RecordReadback(std::uint64_t us, bool dropped) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (dropped) ++impl_->metrics.framesDropped; else Add(impl_->metrics.readback,us);
}
} // namespace render_module::video
