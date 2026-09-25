#include "render_module/video.hpp"
#include "buffer_pool.hpp"
#include <wels/codec_api.h>
#include <algorithm>
#include <cstring>
#include <limits>

namespace render_module::video {
namespace {
class OpenH264Encoder final : public IVideoEncoder {
public:
    ~OpenH264Encoder() override { Destroy(); }
    bool Configure(const EncoderConfig& c) override {
        if (!Validate(c)) return false;
        // Build a replacement before discarding the old codec. Old output never
        // crosses the successful configuration boundary; timestamps do not reset.
        ISVCEncoder* next = nullptr;
        if (WelsCreateSVCEncoder(&next) || !next) return false;
        SEncParamExt p{};
        if (next->GetDefaultParams(&p)) { WelsDestroySVCEncoder(next); return false; }
        p.iUsageType = CAMERA_VIDEO_REAL_TIME; p.iComplexityMode = LOW_COMPLEXITY;
        p.iPicWidth = c.width; p.iPicHeight = c.height; p.fMaxFrameRate = float(c.fps);
        p.iTargetBitrate = int(c.bitrate); p.iMaxBitrate = int(c.maxBitrate);
        p.iRCMode = RC_BITRATE_MODE; p.bEnableFrameSkip = c.allowFrameSkip;
        p.iSpatialLayerNum = 1; p.iTemporalLayerNum = 1; p.iNumRefFrame = 1;
        p.uiIntraPeriod = unsigned(c.keyframeIntervalFrames);
        p.iEntropyCodingModeFlag = 0; p.iMultipleThreadIdc = 1;
        p.bEnableLongTermReference = false; p.bEnableDenoise = false;
        p.bPrefixNalAddingCtrl = false; p.bEnableSSEI = false;
        p.eSpsPpsIdStrategy = CONSTANT_ID;
        auto& layer = p.sSpatialLayers[0];
        layer.iVideoWidth = c.width; layer.iVideoHeight = c.height; layer.fFrameRate = float(c.fps);
        layer.iSpatialBitrate = int(c.bitrate); layer.iMaxSpatialBitrate = int(c.maxBitrate);
        layer.uiProfileIdc = PRO_BASELINE; layer.sSliceArgument.uiSliceMode = SM_SINGLE_SLICE;
        layer.bVideoSignalTypePresent = true; layer.uiVideoFormat = 5; layer.bFullRange = false;
        layer.bColorDescriptionPresent = true;
        layer.uiColorPrimaries = 1; layer.uiTransferCharacteristics = 1; layer.uiColorMatrix = 1;
        int trace = WELS_LOG_QUIET; next->SetOption(ENCODER_OPTION_TRACE_LEVEL, &trace);
        if (next->InitializeExt(&p)) { WelsDestroySVCEncoder(next); return false; }
        std::shared_ptr<detail::FrameBufferPool> buffers;
        try {
            buffers = std::make_shared<detail::FrameBufferPool>(3,std::size_t(c.width)*c.height*4+65536);
        } catch (const std::bad_alloc&) { next->Uninitialize(); WelsDestroySVCEncoder(next); return false; }
        Destroy(); encoder_ = next; config_ = c; buffers_ = std::move(buffers);
        force_ = true; return true;
    }
    EncodeResult Encode(const VideoFrame& f) override {
        if (!encoder_ || !Validate(f) || f.format != PixelFormat::I420 ||
            f.width != config_.width || f.height != config_.height || f.color != config_.color ||
            f.framebufferGeneration != config_.framebufferGeneration ||
            f.ptsUs <= lastPts_ || f.frameId <= lastId_) return EncodeResult::Invalid;
        if (output_.storage) return EncodeResult::Backpressure;
        std::uint8_t* writable = nullptr;
        auto bytes = buffers_->Acquire(writable);
        if (!bytes) return EncodeResult::Backpressure;
        if (force_ && encoder_->ForceIntraFrame(true)) return EncodeResult::Error;
        SSourcePicture picture{}; picture.iColorFormat = videoFormatI420;
        picture.iPicWidth = f.width; picture.iPicHeight = f.height;
        // OpenH264's source timestamp is milliseconds; public PTS remains exact us.
        picture.uiTimeStamp = f.ptsUs/1000;
        for (unsigned i = 0; i != 3; ++i) {
            picture.iStride[i] = f.planes[i].stride;
            picture.pData[i] = const_cast<unsigned char*>(f.Data(i));
        }
        SFrameBSInfo info{};
        if (encoder_->EncodeFrame(&picture, &info)) { force_ = true; return EncodeResult::Error; }
        lastPts_ = f.ptsUs; lastId_ = f.frameId;
        if (info.eFrameType == videoFrameTypeSkip) return EncodeResult::Skipped;
        std::size_t length = 0;
        for (int l = 0; l < info.iLayerNum; ++l) {
            const auto& layer = info.sLayerInfo[l];
            std::size_t layerSize = 0;
            for (int n = 0; n < layer.iNalCount; ++n) {
                if (layer.pNalLengthInByte[n] <= 0 || std::size_t(layer.pNalLengthInByte[n]) > bytes->size()-length-layerSize) {
                    force_ = true; return EncodeResult::Error;
                }
                layerSize += std::size_t(layer.pNalLengthInByte[n]);
            }
            std::memcpy(writable+length, layer.pBsBuf, layerSize); length += layerSize;
        }
        bool idr = false, slice = false; std::size_t cursor = 0; NalUnit nal;
        while (NextAnnexBNal(bytes->data(), length, cursor, nal)) {
            idr |= nal.type == 5; slice |= nal.type == 1 || nal.type == 5;
        }
        if (!slice || (force_ && !idr)) { force_ = true; return EncodeResult::Error; }
        force_ = false;
        output_ = {f.frameId,f.framebufferGeneration,VideoCodec::H264,f.width,f.height,f.ptsUs,
                   f.color,idr,H264Format::AnnexB,std::move(bytes),length};
        return EncodeResult::Produced;
    }
    bool TryReceive(EncodedFrame& output) override {
        if (!output_.storage) return false;
        output = std::move(output_); output_ = {}; return true;
    }
    bool SetTargetBitrate(std::uint32_t value) override {
        if (!encoder_) return false;
        value = std::clamp(value, config_.minBitrate, config_.maxBitrate);
        SBitrateInfo bitrate{SPATIAL_LAYER_ALL,int(value)};
        if (encoder_->SetOption(ENCODER_OPTION_BITRATE, &bitrate)) return false;
        SBitrateInfo actual{SPATIAL_LAYER_ALL,0};
        if (encoder_->GetOption(ENCODER_OPTION_BITRATE, &actual)) return false;
        config_.bitrate = std::uint32_t(actual.iBitrate); return true;
    }
    std::uint32_t TargetBitrate() const override { return encoder_ ? config_.bitrate : 0; }
    void ForceKeyframe() override { force_ = true; }
    void Flush() override {} // Synchronous, no B pictures or delayed access units.
private:
    void Destroy() {
        output_ = {};
        if (encoder_) { encoder_->Uninitialize(); WelsDestroySVCEncoder(encoder_); encoder_ = nullptr; }
    }
    ISVCEncoder* encoder_ = nullptr;
    EncoderConfig config_;
    bool force_ = true;
    std::int64_t lastPts_ = -1;
    std::uint64_t lastId_ = 0;
    std::shared_ptr<detail::FrameBufferPool> buffers_;
    EncodedFrame output_;
};
}
std::unique_ptr<IVideoEncoder> CreateOpenH264Encoder() { return std::make_unique<OpenH264Encoder>(); }
} // namespace render_module::video
