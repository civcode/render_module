#include "render_module/video.hpp"
#include "buffer_pool.hpp"
#include <libyuv/convert.h>
#include <libyuv/convert_from_argb.h>
#include <algorithm>
#include <cmath>
#include <limits>

namespace render_module::video {
bool ColorInfo::operator==(const ColorInfo& b) const {
    return primaries == b.primaries && transfer == b.transfer && matrix == b.matrix && range == b.range;
}
namespace {
bool Dimensions(int w, int h) { return w > 0 && h > 0 && w <= 2048 && h <= 2048; }
int Stride(int width) { return (width+63)&~63; }
bool PlaneValid(const VideoFrame& f, unsigned n, int bytes, int rows) {
    const auto& p = f.planes[n];
    if (!f.storage || p.stride < bytes || p.offset > f.storage->size()) return false;
    const auto available = f.storage->size()-p.offset;
    return std::uint64_t(p.stride)*(rows-1) + bytes <= available;
}
}
bool Validate(const EncoderConfig& c) {
    return Dimensions(c.width,c.height) && c.width >= 16 && c.height >= 16 &&
        !(c.width&1) && !(c.height&1) &&
        std::isfinite(c.fps) && c.fps >= 1 && c.fps <= 60 && c.minBitrate >= 10000 &&
        c.minBitrate <= c.bitrate && c.bitrate <= c.maxBitrate && c.maxBitrate <= 50000000 &&
        c.keyframeIntervalFrames >= 1 && c.keyframeIntervalFrames <= 3600 &&
        c.codec == VideoCodec::H264 && c.inputFormat == PixelFormat::I420 &&
        c.color == ColorInfo{} && c.framebufferGeneration != 0;
}
bool Validate(const VideoFrame& f) {
    if (!Dimensions(f.width,f.height) || !f.frameId || !f.framebufferGeneration || f.ptsUs < 0 ||
        f.orientation != Orientation::TopDown) return false;
    if (f.format == PixelFormat::RGBA8)
        return f.color == ColorInfo::Rgba() && PlaneValid(f,0,f.width*4,f.height);
    if (f.format != PixelFormat::I420 || f.color != ColorInfo{}) return false;
    return PlaneValid(f,0,f.width,f.height) && PlaneValid(f,1,(f.width+1)/2,(f.height+1)/2) &&
        PlaneValid(f,2,(f.width+1)/2,(f.height+1)/2);
}
const std::uint8_t* VideoFrame::Data(unsigned plane) const {
    return storage && plane < planes.size() && planes[plane].offset < storage->size() ?
        storage->data()+planes[plane].offset : nullptr;
}
bool NormalizeSize(int w, int h, int& width, int& height) {
    if (!Dimensions(w,h) || w < 2 || h < 2) return false;
    width = w&~1; height = h&~1; return true;
}
std::int64_t VideoClock::Next() {
    auto now = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-origin_).count();
    return last_ = std::max(last_+1, now);
}
struct RgbaFramePool::Storage : detail::FrameBufferPool { using FrameBufferPool::FrameBufferPool; };
bool RgbaFramePool::Configure(int w, int h) {
    if (!Dimensions(w,h)) return false;
    if (width_ == w && height_ == h) return true;
    std::shared_ptr<Storage> next;
    try { next = std::make_shared<Storage>(4,std::size_t(Stride(w*4))*h); }
    catch (const std::bad_alloc&) { return false; }
    pool_ = std::move(next); width_ = w; height_ = h; stride_ = Stride(w*4); return true;
}
bool RgbaFramePool::Acquire(VideoFrame& frame, std::uint8_t*& writable) {
    writable = nullptr;
    if (!pool_) return false;
    auto lease = pool_->Acquire(writable);
    if (!lease) return false;
    frame = {}; frame.width = width_; frame.height = height_;
    frame.planes[0].stride = stride_; frame.storage = std::move(lease); return true;
}
bool I420Converter::Configure(int w, int h) {
    if (!Dimensions(w,h)) return false;
    if (frame_.width == w && frame_.height == h) return true;
    const int yStride = Stride(w), uvStride = Stride((w+1)/2);
    const std::size_t ySize = std::size_t(yStride)*h, uvSize = std::size_t(uvStride)*((h+1)/2);
    std::shared_ptr<std::vector<std::uint8_t>> next;
    try { next = std::make_shared<std::vector<std::uint8_t>>(ySize+uvSize*2); }
    catch (const std::bad_alloc&) { return false; }
    buffer_ = std::move(next); frame_ = {}; frame_.width = w; frame_.height = h;
    frame_.format = PixelFormat::I420; frame_.color = {}; frame_.storage = buffer_;
    frame_.planes = {{{0,yStride},{ySize,uvStride},{ySize+uvSize,uvStride}}}; return true;
}
bool I420Converter::Convert(const VideoFrame& rgba, VideoFrame& i420) {
    if (!buffer_ || !Validate(rgba) || rgba.format != PixelFormat::RGBA8 ||
        rgba.width < frame_.width || rgba.width > frame_.width+1 ||
        rgba.height < frame_.height || rgba.height > frame_.height+1) return false;
    // RGBA byte order is libyuv's little-endian ABGR, NOT ARGB. Explicit BT.709 limited.
    // Capture already flipped to top-down: positive height, no second flip.
    if (libyuv::ARGBToI420Matrix(rgba.Data(), rgba.planes[0].stride,
        buffer_->data(), frame_.planes[0].stride,
        buffer_->data()+frame_.planes[1].offset, frame_.planes[1].stride,
        buffer_->data()+frame_.planes[2].offset, frame_.planes[2].stride,
        &libyuv::kAbgrH709Constants, frame_.width, frame_.height)) return false;
    frame_.frameId = rgba.frameId; frame_.framebufferGeneration = rgba.framebufferGeneration;
    frame_.ptsUs = rgba.ptsUs; i420 = frame_; return true;
}
bool NextAnnexBNal(const std::uint8_t* data, std::size_t size, std::size_t& cursor, NalUnit& nal) {
    const auto prefix = [&](std::size_t i) -> unsigned {
        if (i+3 <= size && data[i] == 0 && data[i+1] == 0) {
            if (data[i+2] == 1) return 3;
            if (i+4 <= size && data[i+2] == 0 && data[i+3] == 1) return 4;
        }
        return 0;
    };
    nal = {};
    if (!data || cursor >= size) return false;
    auto start = cursor; while (start < size && !prefix(start)) ++start;
    if (start == size) { cursor = size; return false; }
    start += prefix(start);
    auto end = start; while (end < size && !prefix(end)) ++end;
    cursor = end;
    if (end == start || (data[start]&0x80)) return false;
    nal = {data+start, end-start, unsigned(data[start]&31)}; return true;
}
double Timing::MeanMs() const { return samples ? double(totalUs)/samples/1000 : 0; }
} // namespace render_module::video
