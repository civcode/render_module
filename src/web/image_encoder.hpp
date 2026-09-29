#pragma once
#include "present/image_presenter.hpp"
#include <cstddef>
#include <cstdint>
#include <vector>

namespace render_module::detail {
enum class ImageCodec : std::uint16_t { Jpeg=1, Png=2 };
struct EncodedImage {
    ImageCodec codec=ImageCodec::Jpeg;
    std::uint64_t frameId=0;
    std::uint32_t width=0,height=0;
    std::vector<unsigned char> bytes;
};
class ImageEncoder {
public:
    ImageEncoder(ImageCodec codec,int jpegQuality,std::size_t limit)
        :codec_(codec),jpegQuality_(jpegQuality),limit_(limit) {}
    bool Encode(const ImageRgba&,std::uint64_t frameId,EncodedImage&) const;
    ImageCodec Codec() const;
private:
    ImageCodec codec_;
    int jpegQuality_;
    std::size_t limit_;
};
const char* ImageCodecName(ImageCodec) noexcept;
const char* ImageCodecMime(ImageCodec) noexcept;
} // namespace render_module::detail
