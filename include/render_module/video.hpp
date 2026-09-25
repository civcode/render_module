#pragma once
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace render_module::video {
enum class VideoCodec { H264, VP8 };
enum class PixelFormat { RGBA8, I420, NV12 };
enum class Primaries { Bt709 };
enum class Transfer { Bt709 };
enum class Matrix { Rgb, Bt709 };
enum class Range { Full, Limited };
enum class Orientation { TopDown, BottomUp };
enum class H264Format { AnnexB };
struct ColorInfo {
    Primaries primaries = Primaries::Bt709;
    Transfer transfer = Transfer::Bt709;
    Matrix matrix = Matrix::Bt709;
    Range range = Range::Limited;
    bool operator==(const ColorInfo& b) const;
    bool operator!=(const ColorInfo& b) const { return !(*this == b); }
    static ColorInfo Rgba() { return {Primaries::Bt709, Transfer::Bt709, Matrix::Rgb, Range::Full}; }
};
struct Plane { std::size_t offset = 0; int stride = 0; };
struct VideoFrame {
    std::uint64_t frameId = 0, framebufferGeneration = 0;
    int width = 0, height = 0;
    PixelFormat format = PixelFormat::RGBA8;
    std::int64_t ptsUs = 0;
    ColorInfo color = ColorInfo::Rgba();
    Orientation orientation = Orientation::TopDown;
    std::array<Plane, 3> planes{};
    std::shared_ptr<const std::vector<std::uint8_t>> storage;
    const std::uint8_t* Data(unsigned plane = 0) const;
};
// Pooled immutable lease: release strong AND weak storage references promptly.
struct EncodedFrame {
    std::uint64_t frameId = 0, framebufferGeneration = 0;
    VideoCodec codec = VideoCodec::H264;
    int width = 0, height = 0;
    std::int64_t ptsUs = 0;
    ColorInfo color;
    bool keyframe = false;
    H264Format format = H264Format::AnnexB;
    std::shared_ptr<const std::vector<std::uint8_t>> storage;
    std::size_t size = 0; // Access unit prefix of pooled storage, not storage->size().
};
struct EncoderConfig {
    int width = 1280, height = 720;
    double fps = 30;
    std::uint32_t bitrate = 6000000, minBitrate = 250000, maxBitrate = 12000000;
    int keyframeIntervalFrames = 60;
    bool allowFrameSkip = true;
    VideoCodec codec = VideoCodec::H264;
    PixelFormat inputFormat = PixelFormat::I420;
    ColorInfo color;
    std::uint64_t framebufferGeneration = 1;
};
bool Validate(const EncoderConfig& config);
bool Validate(const VideoFrame& frame);
// Crop at most the last column/row: round down to even, never upscale.
bool NormalizeSize(int requestedWidth, int requestedHeight, int& width, int& height);
class VideoClock {
public:
    std::int64_t Next(); // steady-clock microseconds since construction; strictly increasing.
private:
    std::chrono::steady_clock::time_point origin_ = std::chrono::steady_clock::now();
    std::int64_t last_ = -1;
};
// Render-thread pool. Acquire supplies exclusive writable access until submission.
// Never retain/use the writable pointer after transferring the frame to a consumer.
// Strong and weak storage references both reserve slots; release them promptly.
class RgbaFramePool {
public:
    bool Configure(int width, int height);
    bool Acquire(VideoFrame& frame, std::uint8_t*& writable);
private:
    int width_ = 0, height_ = 0, stride_ = 0;
    struct Storage;
    std::shared_ptr<Storage> pool_;
};
// Worker-thread converter. Reuses storage; returned view is valid until next Convert/Configure.
class I420Converter {
public:
    bool Configure(int width, int height);
    bool Convert(const VideoFrame& rgba, VideoFrame& i420);
private:
    VideoFrame frame_;
    std::shared_ptr<std::vector<std::uint8_t>> buffer_;
};
enum class EncodeResult { Produced, Skipped, Backpressure, Invalid, Error };
class IVideoEncoder {
public:
    virtual ~IVideoEncoder() = default;
    virtual bool Configure(const EncoderConfig&) = 0;
    virtual EncodeResult Encode(const VideoFrame&) = 0;
    virtual bool TryReceive(EncodedFrame&) = 0;
    virtual bool SetTargetBitrate(std::uint32_t bitsPerSecond) = 0;
    virtual std::uint32_t TargetBitrate() const = 0;
    virtual void ForceKeyframe() = 0;
    virtual void Flush() = 0; // No reordering: all available output stays receivable.
};
// Returns null if OpenH264 was disabled. No codec headers/types cross this boundary.
std::unique_ptr<IVideoEncoder> CreateOpenH264Encoder();
struct NalUnit { const std::uint8_t* data = nullptr; std::size_t size = 0; unsigned type = 0; };
// Allocation-free Annex-B iterator. cursor begins at zero; NAL excludes start code.
bool NextAnnexBNal(const std::uint8_t* bytes, std::size_t size, std::size_t& cursor, NalUnit& nal);
struct Timing { std::uint64_t samples = 0, totalUs = 0, worstUs = 0; double MeanMs() const; };
struct VideoMetrics {
    std::uint64_t framesSubmitted = 0, framesEncoded = 0, framesDropped = 0;
    std::uint64_t bytesEncoded = 0, keyframes = 0, errors = 0;
    unsigned encoderQueueDepth = 0, outputQueueDepth = 0;
    std::uint32_t effectiveBitrate = 0;
    Timing readback, rgbaToI420, encode;
};
// One worker, one replaceable raw slot, one encoded output slot. No worker GL calls.
// Methods are thread-safe; destruction requires callers to stop submitting first.
class VideoPipeline {
public:
    explicit VideoPipeline(std::unique_ptr<IVideoEncoder> encoder = CreateOpenH264Encoder());
    ~VideoPipeline();
    VideoPipeline(const VideoPipeline&) = delete;
    VideoPipeline& operator=(const VideoPipeline&) = delete;
    bool Configure(const EncoderConfig&); // Async command; discards stale pending/output frames.
    EncoderConfig Configuration() const;
    bool Submit(VideoFrame frame); // Transferred immutable CPU storage; never waits for encoding.
    bool TryReceive(EncodedFrame&);
    void SetTargetBitrate(std::uint32_t);
    void ForceKeyframe();
    bool Flush(std::chrono::milliseconds timeout); // Waits for submitted work; caller must drain output.
    VideoMetrics Metrics() const;
    void RecordReadback(std::uint64_t microseconds, bool dropped = false);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace render_module::video
