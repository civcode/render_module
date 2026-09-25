#include <render_module/render_module.hpp>
#ifdef INSTALLED_VIDEO
#include <render_module/video.hpp>
#include <cstring>
#endif
int main() {
    if (RenderModule::IsInitialized()) return 1;
#ifdef INSTALLED_VIDEO
    using namespace render_module::video;
    auto encoder=CreateOpenH264Encoder(); EncoderConfig config; config.width=config.height=32;
    if (!encoder || !encoder->Configure(config)) return 2;
    RgbaFramePool pool; VideoFrame rgba,i420; std::uint8_t* bytes;
    if (!pool.Configure(32,32) || !pool.Acquire(rgba,bytes)) return 3;
    std::memset(bytes,255,rgba.storage->size()); rgba.frameId=1; rgba.framebufferGeneration=1;
    I420Converter converter;
    if (!converter.Configure(32,32) || !converter.Convert(rgba,i420)) return 4;
    if (encoder->Encode(i420)!=EncodeResult::Produced) return 5;
    EncodedFrame output;
    if (!encoder->TryReceive(output) || !output.keyframe || output.format!=H264Format::AnnexB) return 6;
#endif
    return 0;
}
