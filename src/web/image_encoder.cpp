#include "image_encoder.hpp"
#include "jpeg_encode.h"
#include <cstdlib>
#include <memory>
#include <new>

namespace render_module::detail {
ImageCodec ImageEncoder::Codec() const {
    return codec_==WebSocketImageCodec::Png?ImageCodec::Png:ImageCodec::Jpeg;
}
const char* ImageCodecName(ImageCodec codec) noexcept {
    return codec==ImageCodec::Png?"png":"jpeg";
}
const char* ImageCodecMime(ImageCodec codec) noexcept {
    return codec==ImageCodec::Png?"image/png":"image/jpeg";
}
bool ImageEncoder::Encode(const ImageRgba& image,std::uint64_t frameId,EncodedImage& output) const {
    if(!frameId||image.width<=0||image.height<=0||image.width>2048||image.height>2048||!limit_||
       image.pixels.size()!=std::size_t(image.width)*image.height*4) return false;
    EncodedImage next; next.codec=Codec(); next.frameId=frameId;
    next.width=std::uint32_t(image.width); next.height=std::uint32_t(image.height);
    if(next.codec==ImageCodec::Png) {
        if(!ImagePresenter::EncodePng(image,next.bytes,limit_)) return false;
    } else {
        unsigned char* bytes=nullptr; unsigned long size=0;
        if(!rm_encode_jpeg(image.pixels.data(),image.width,image.height,jpegQuality_,
                           static_cast<unsigned long>(limit_),&bytes,&size)) return false;
        std::unique_ptr<unsigned char,decltype(&std::free)> owned(bytes,&std::free);
        try { next.bytes.assign(bytes,bytes+size); } catch(const std::bad_alloc&) { return false; }
    }
    if(next.bytes.empty() || next.bytes.size()>limit_) return false;
    output=std::move(next); return true;
}
} // namespace render_module::detail
