#include "render_module/video.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <atomic>
#include <cstdlib>
#include <new>
#include <mutex>
// Test the C++ allocation path after pool/codec warm-up (not decoder tooling).
std::atomic<std::uint64_t> videoTestAllocations{0};
void* operator new(std::size_t size) {
    if (void* p=std::malloc(size ? size : 1)) { ++videoTestAllocations; return p; }
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p,std::size_t) noexcept { std::free(p); }
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete[](void* p,std::size_t) noexcept { std::free(p); }
#ifdef VIDEO_TEST_DECODER
extern "C" {
#include <libavcodec/avcodec.h>
}
#endif
using namespace render_module::video;
#define CHECK(x) do { if (!(x)) throw std::runtime_error("line " + std::to_string(__LINE__) + ": " #x); } while (false)
namespace {
template<class F> void Wait(F f) {
    auto end = std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while (!f()) { CHECK(std::chrono::steady_clock::now()<end); std::this_thread::sleep_for(std::chrono::milliseconds(2)); }
}
const int colors[6][3] = {{255,0,0},{0,255,0},{0,0,255},{255,255,255},{0,0,0},{128,128,128}};
VideoFrame Pattern(RgbaFramePool& pool, int w=384, int h=256, std::uint64_t id=1, std::uint64_t gen=1) {
    CHECK(pool.Configure(w,h)); VideoFrame f; std::uint8_t* data;
    CHECK(pool.Acquire(f,data)); f.frameId=id; f.framebufferGeneration=gen; f.ptsUs=std::int64_t(id)*33333;
    for (int y=0;y<h;++y) for (int x=0;x<w;++x) {
        auto* p=data+std::size_t(y)*f.planes[0].stride+x*4;
        const auto& c=colors[std::min(2,x*3/w)+3*std::min(1,y*2/h)];
        for (int n=0;n<3;++n) p[n]=c[n]; p[3]=255;
    }
    // Fine UI-like strokes and small bitmap 'UI', away from sample centers.
    const unsigned glyph[] = {17,17,17,17,14, 31,4,4,4,31};
    for (int x=8;x<w-8;++x) for (int y : {8,h-8}) {
        auto* p=data+std::size_t(y)*f.planes[0].stride+x*4; p[0]=p[1]=p[2]=0;
    }
    for (int y=10;y<h-10;++y) { auto* p=data+std::size_t(y)*f.planes[0].stride+8*4; p[0]=p[1]=p[2]=255; }
    for(int g=0;g<2;++g) for(int y=0;y<5;++y) for(int x=0;x<5;++x) if(glyph[g*5+y]&(1u<<x)) {
        auto* p=data+std::size_t(y+20)*f.planes[0].stride+(x+20+g*7)*4; p[0]=p[1]=p[2]=255;
    }
    return f;
}
EncoderConfig Config(int w=384,int h=256) {
    EncoderConfig c; c.width=w; c.height=h; c.allowFrameSkip=false; return c;
}
bool Has(const EncodedFrame& f,unsigned type) {
    std::size_t cursor=0; NalUnit nal;
    while(NextAnnexBNal(f.storage->data(),f.size,cursor,nal)) if(nal.type==type) return true;
    return false;
}
void ConfigTest() {
    auto c=Config(); CHECK(Validate(c));
    for (int n : {0,-1,1,2,14,2049}) { auto bad=c; bad.width=n; CHECK(!Validate(bad)); }
    for (double n : {0.,-1.,61.,std::nan(""),double(INFINITY)}) { auto bad=c; bad.fps=n; CHECK(!Validate(bad)); }
    auto bad=c; bad.bitrate=0; CHECK(!Validate(bad)); bad=c; bad.minBitrate=bad.maxBitrate+1; CHECK(!Validate(bad));
    bad=c; bad.maxBitrate=50000001; CHECK(!Validate(bad)); bad=c; bad.keyframeIntervalFrames=0; CHECK(!Validate(bad));
    bad=c; bad.inputFormat=PixelFormat::NV12; CHECK(!Validate(bad)); bad=c; bad.codec=VideoCodec::VP8; CHECK(!Validate(bad));
    bad=c; bad.color.range=Range::Full; CHECK(!Validate(bad)); bad=c; bad.framebufferGeneration=0; CHECK(!Validate(bad));
    int w,h; CHECK(NormalizeSize(1001,701,w,h) && w==1000 && h==700);
    CHECK(NormalizeSize(2047,2047,w,h) && w==2046 && h==2046); CHECK(!NormalizeSize(1,700,w,h));
    VideoClock clock; auto t=clock.Next(); CHECK(clock.Next()>t);
    RgbaFramePool pool; std::array<VideoFrame,4> leases;
    CHECK(pool.Configure(130,98)); std::uint8_t* bytes;
    for(auto& f:leases) CHECK(pool.Acquire(f,bytes)); VideoFrame extra; CHECK(!pool.Acquire(extra,bytes));
    leases[0]={}; CHECK(pool.Acquire(extra,bytes));
    const std::uint8_t stream[]={0,0,0,1,0x67,66,0xc0,31,0,0,1,0x68,1,0,0,0,1,0x65,2,0,0,1,0x41,3};
    std::size_t cursor=0; NalUnit nal;
    for(unsigned type:{7,8,5,1}) { CHECK(NextAnnexBNal(stream,sizeof(stream),cursor,nal)); CHECK(nal.type==type); }
    CHECK(!NextAnnexBNal(stream,sizeof(stream),cursor,nal));
}
void PoolTest() {
    RgbaFramePool pool; CHECK(pool.Configure(32,32));
    std::array<VideoFrame,4> leases; std::uint8_t* bytes;
    for(auto& f:leases) { CHECK(pool.Acquire(f,bytes)); std::memset(bytes,7,f.storage->size()); }
    std::weak_ptr<const std::vector<std::uint8_t>> weak=leases[0].storage;
    std::atomic<bool> locked{false},finish{false},correct{true};
    std::thread reader([&] {
        auto lease=weak.lock(); if(!lease) correct=false;
        locked=true;
        while(!finish) {
            if(lease && !std::all_of(lease->begin(),lease->end(),[](auto b){return b==7;})) correct=false;
            std::this_thread::yield();
        }
    });
    while(!locked) std::this_thread::yield();
    leases[0]={}; VideoFrame extra; bool unavailable=true;
    for(int n=0;n<1000;++n) if(pool.Acquire(extra,bytes)) { unavailable=false; break; }
    const bool alive=!weak.expired(); finish=true; reader.join();
    CHECK(alive && unavailable && correct && weak.expired());
    CHECK(!weak.lock() && !pool.Acquire(extra,bytes)); // Weak control block still reserves its slot.
    weak.reset(); CHECK(pool.Acquire(extra,bytes)); extra={}; leases={};

    std::mutex mailbox; VideoFrame pending; std::atomic<bool> done{false},started{false};
    std::atomic<unsigned> received{0};
    std::thread consumer([&] {
        std::weak_ptr<const std::vector<std::uint8_t>> prior;
        started=true;
        for(;;) {
            VideoFrame f;
            { std::lock_guard<std::mutex> lock(mailbox); f=std::move(pending); pending={}; }
            if(!f.storage) { if(done) break; std::this_thread::yield(); continue; }
            const auto value=std::uint8_t(f.frameId%251);
            if(!std::all_of(f.storage->begin(),f.storage->end(),[&](auto b){return b==value;})) correct=false;
            prior=f.storage; ++received; // Retain one weak lease through the next handoff.
        }
    });
    while(!started) std::this_thread::yield();
    const auto allocations=videoTestAllocations.load(); bool timely=true;
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
    for(unsigned n=1;n<=5000;++n) {
        VideoFrame f;
        while(!pool.Acquire(f,bytes)) {
            if(std::chrono::steady_clock::now()>deadline) { timely=false; break; }
            std::this_thread::yield();
        }
        if(!timely) break;
        f.frameId=n; std::memset(bytes,n%251,f.storage->size());
        std::lock_guard<std::mutex> lock(mailbox); pending=std::move(f);
    }
    done=true; consumer.join();
    CHECK(timely && correct && received>0 && videoTestAllocations==allocations);
}
void ColorTest() {
    RgbaFramePool pool; auto rgba=Pattern(pool); I420Converter converter; VideoFrame yuv;
    CHECK(converter.Configure(rgba.width,rgba.height)); CHECK(converter.Convert(rgba,yuv));
    const int expected[6][3]={{63,102,240},{173,42,26},{32,240,118},{235,128,128},{16,128,128},{126,128,128}};
    for(int n=0;n<6;++n) {
        int x=(n%3)*128+64,y=(n/3)*128+64;
        for(unsigned p=0;p<3;++p) {
            auto value=yuv.Data(p)[(y>>(p!=0))*yuv.planes[p].stride+(x>>(p!=0))];
            CHECK(std::abs(int(value)-expected[n][p])<=2);
        }
    }
    auto* stable=yuv.Data(); const auto allocations=videoTestAllocations.load();
    for(int n=0;n<100;++n) { CHECK(converter.Convert(rgba,yuv)); CHECK(stable==yuv.Data()); }
    CHECK(videoTestAllocations==allocations);
    rgba=Pattern(pool,131,99,2); CHECK(converter.Configure(131,99)); CHECK(converter.Convert(rgba,yuv));
    CHECK(yuv.planes[0].stride>=131 && yuv.planes[1].stride>=66);
    CHECK(yuv.storage->size()==std::size_t(yuv.planes[0].stride)*99+2*std::size_t(yuv.planes[1].stride)*50);
    CHECK(converter.Configure(130,98)); CHECK(converter.Convert(rgba,yuv)); CHECK(yuv.width==130 && yuv.height==98);
    auto invalid=rgba; invalid.planes[0].stride=1; CHECK(!converter.Convert(invalid,yuv));
    invalid=rgba; invalid.planes[0].offset=invalid.storage->size(); CHECK(!Validate(invalid));
    invalid=rgba; invalid.orientation=Orientation::BottomUp; CHECK(!converter.Convert(invalid,yuv));
    // Source survives its producer's replacement; storage is immutable/ref-counted.
    auto saved=rgba; CHECK(pool.Configure(640,480)); CHECK(saved.Data()[0]==255);
}
void CodecTest() {
    auto encoder=CreateOpenH264Encoder(); CHECK(encoder); auto c=Config(); CHECK(encoder->Configure(c));
    RgbaFramePool pool; auto rgba=Pattern(pool); I420Converter convert; CHECK(convert.Configure(c.width,c.height));
    VideoFrame i420; EncodedFrame e;
    for(std::uint64_t id=1;id<=35;++id) {
        rgba.frameId=id; rgba.ptsUs=id*33333; CHECK(convert.Convert(rgba,i420));
        if(id==4) encoder->ForceKeyframe();
        const std::uint32_t rates[]={6000000,2000000,8000000};
        CHECK(encoder->SetTargetBitrate(rates[(id-1)%3])); CHECK(encoder->TargetBitrate()==rates[(id-1)%3]);
        const auto allocations=videoTestAllocations.load();
        CHECK(encoder->Encode(i420)==EncodeResult::Produced);
        if(id>5) CHECK(videoTestAllocations==allocations);
        CHECK(encoder->Encode(i420)==EncodeResult::Invalid); // Duplicate PTS/id cannot be reused.
        CHECK(encoder->TryReceive(e)); CHECK(e.frameId==id && e.ptsUs==i420.ptsUs && e.width==384 && e.height==256);
        CHECK(e.format==H264Format::AnnexB && e.size>4 && e.storage->at(0)==0 && e.storage->at(1)==0);
        CHECK(e.keyframe==Has(e,5));
        if(id==1 || id==4) { CHECK(e.keyframe && Has(e,7) && Has(e,8));
            std::size_t cursor=0; NalUnit nal;
            while(NextAnnexBNal(e.storage->data(),e.size,cursor,nal)) if(nal.type==7) {
                CHECK(nal.size>3 && nal.data[1]==66 && (nal.data[2]&0xc0)==0xc0);
            }
        } else CHECK(!e.keyframe && Has(e,1));
        e={};
    }
    CHECK(encoder->SetTargetBitrate(0) && encoder->TargetBitrate()==c.minBitrate);
    CHECK(encoder->SetTargetBitrate(UINT32_MAX) && encoder->TargetBitrate()==c.maxBitrate);
    encoder->Flush(); CHECK(!encoder->TryReceive(e));
    for(unsigned n=0;n<20;++n) {
        c=Config(n%2?640:128, n%2?360:96); c.framebufferGeneration=n+2;
        CHECK(encoder->Configure(c)); CHECK(encoder->Encode(i420)==EncodeResult::Invalid);
        rgba=Pattern(pool,c.width,c.height,100+n,c.framebufferGeneration);
        CHECK(convert.Configure(c.width,c.height) && convert.Convert(rgba,i420));
        CHECK(encoder->Encode(i420)==EncodeResult::Produced && encoder->TryReceive(e));
        CHECK(e.keyframe && e.width==c.width && e.height==c.height && e.framebufferGeneration==n+2); e={};
    }
    for(int n=0;n<5;++n) { encoder=CreateOpenH264Encoder(); CHECK(encoder->Configure(c));
        CHECK(encoder->Encode(i420)==EncodeResult::Produced && encoder->TryReceive(e) && e.keyframe); e={}; }
    // Periodic IDR, without a force command.
    c=Config(); c.keyframeIntervalFrames=3; encoder=CreateOpenH264Encoder(); CHECK(encoder->Configure(c));
    CHECK(convert.Configure(c.width,c.height)); rgba=Pattern(pool);
    for(int n=1;n<=7;++n) { rgba.frameId=n; rgba.ptsUs=n*33333; CHECK(convert.Convert(rgba,i420));
        CHECK(encoder->Encode(i420)==EncodeResult::Produced && encoder->TryReceive(e)); CHECK(e.keyframe==((n-1)%3==0)); e={}; }
    std::array<EncodedFrame,3> held;
    for(unsigned n=0;n<3;++n) { i420.frameId=8+n; i420.ptsUs=(8+n)*33333;
        CHECK(encoder->Encode(i420)==EncodeResult::Produced && encoder->TryReceive(held[n])); }
    i420.frameId=11; i420.ptsUs=11*33333; encoder->ForceKeyframe();
    CHECK(encoder->Encode(i420)==EncodeResult::Backpressure);
    std::weak_ptr<const std::vector<std::uint8_t>> weak=held[0].storage;
    held[0]={}; CHECK(weak.expired() && !weak.lock());
    CHECK(encoder->Encode(i420)==EncodeResult::Backpressure);
    weak.reset(); CHECK(encoder->Encode(i420)==EncodeResult::Produced && encoder->TryReceive(e) && e.keyframe);
    const auto first=e.storage->at(0); encoder.reset(); CHECK(e.storage->at(0)==first); // Owns bytes past codec lifetime.
    weak=e.storage; e={}; held={}; CHECK(weak.expired() && !weak.lock());
    weak.reset(); // Final control block releases its arena after the codec and all strong leases are gone.
}
void QueueTest() {
    VideoPipeline pipeline; auto c=Config(); CHECK(pipeline.Configure(c)); RgbaFramePool pool;
    CHECK(pipeline.Submit(Pattern(pool))); Wait([&]{return pipeline.Metrics().outputQueueDepth==1;});
    for(unsigned id : {100,101,102}) CHECK(pipeline.Submit(Pattern(pool,384,256,id)));
    CHECK(pipeline.Metrics().encoderQueueDepth==1 && pipeline.Metrics().framesDropped==2);
    CHECK(!pipeline.Flush(std::chrono::milliseconds(10))); EncodedFrame e;
    CHECK(pipeline.TryReceive(e) && e.frameId==1); e={};
    Wait([&]{return pipeline.TryReceive(e);}); CHECK(e.frameId==102 && !e.keyframe); e={};
    CHECK(pipeline.Flush(std::chrono::seconds(1)));
    for(unsigned n=0;n<25;++n) {
        c=Config(n%2?640:128,n%2?360:96); c.framebufferGeneration=n+2;
        CHECK(pipeline.Configure(c)); CHECK(!pipeline.TryReceive(e));
        CHECK(!pipeline.Submit(Pattern(pool,384,256,200+n,1)));
        CHECK(pipeline.Submit(Pattern(pool,c.width,c.height,200+n,n+2)));
        Wait([&]{return pipeline.TryReceive(e);}); CHECK(e.framebufferGeneration==n+2 && e.keyframe); e={};
        pipeline.SetTargetBitrate(n%2?2000000:8000000); CHECK(pipeline.Flush(std::chrono::seconds(1)));
    }
    // Reconfigure while initialization/conversion/encoding may still be running.
    for(unsigned n=0;n<40;++n) {
        c=Config(n%2?192:128,96); c.framebufferGeneration=100+n;
        CHECK(pipeline.Configure(c)); CHECK(!pipeline.TryReceive(e));
        CHECK(pipeline.Submit(Pattern(pool,c.width,c.height,500+n,100+n)));
    }
    Wait([&]{return pipeline.TryReceive(e);}); CHECK(e.frameId==539 && e.framebufferGeneration==139 && e.keyframe); e={};
    pipeline.ForceKeyframe(); CHECK(pipeline.Submit(Pattern(pool,c.width,c.height,540,139)));
    Wait([&]{return pipeline.TryReceive(e);}); CHECK(e.keyframe); e={};
    CHECK(pipeline.Metrics().errors==0 && pipeline.Metrics().encoderQueueDepth==0);
    for(unsigned n=0;n<5;++n) {
        VideoPipeline ephemeral; CHECK(ephemeral.Configure(Config()));
        CHECK(ephemeral.Submit(Pattern(pool,384,256,1,1))); // Destroy with work pending/in flight.
    }
}
#ifdef VIDEO_TEST_DECODER
void RoundtripTest() {
    const auto* codec=avcodec_find_decoder(AV_CODEC_ID_H264); CHECK(codec);
    auto* context=avcodec_alloc_context3(codec); auto* frame=av_frame_alloc(); auto* packet=av_packet_alloc();
    CHECK(context && frame && packet); context->thread_count=1; CHECK(avcodec_open2(context,codec,nullptr)>=0);
    auto encoder=CreateOpenH264Encoder(); CHECK(encoder->Configure(Config()));
    RgbaFramePool pool; auto rgba=Pattern(pool); I420Converter convert; VideoFrame yuv;
    CHECK(convert.Configure(384,256) && convert.Convert(rgba,yuv));
    CHECK(encoder->Encode(yuv)==EncodeResult::Produced); EncodedFrame e; CHECK(encoder->TryReceive(e));
    CHECK(av_new_packet(packet,int(e.size))>=0); std::memcpy(packet->data,e.storage->data(),e.size);
    CHECK(avcodec_send_packet(context,packet)>=0 && avcodec_receive_frame(context,frame)>=0);
    CHECK(frame->width==384 && frame->height==256 && frame->format==AV_PIX_FMT_YUV420P);
    CHECK(frame->color_range==AVCOL_RANGE_MPEG && frame->colorspace==AVCOL_SPC_BT709);
    CHECK(frame->color_primaries==AVCOL_PRI_BT709 && frame->color_trc==AVCOL_TRC_BT709);
    double total=0; int worst=0;
    for(int y=0;y<256;++y) for(int x=0;x<384;++x) {
        const double yy=(frame->data[0][y*frame->linesize[0]+x]-16)*255./219.;
        const double u=(frame->data[1][(y/2)*frame->linesize[1]+x/2]-128)*255./224.;
        const double v=(frame->data[2][(y/2)*frame->linesize[2]+x/2]-128)*255./224.;
        const double rgb[]={yy+1.5748*v, yy-0.187324*u-0.468124*v, yy+1.8556*u};
        const auto* source=rgba.Data()+y*rgba.planes[0].stride+x*4;
        for(int ch=0;ch<3;++ch) { int error=std::abs(int(std::clamp(rgb[ch],0.,255.))-source[ch]); total+=error; worst=std::max(worst,error); }
        if(x%128==64 && y%128==64) for(int ch=0;ch<3;++ch) CHECK(std::abs(std::clamp(rgb[ch],0.,255.)-source[ch])<12);
    }
    CHECK(total/(384*256*3)<8); // Thin colored edges have expected 4:2:0 loss.
    std::cout<<"Independent H.264 roundtrip RGB mean="<<total/(384*256*3)<<" worst="<<worst<<"\n";
    av_packet_free(&packet); av_frame_free(&frame); avcodec_free_context(&context);
}
#endif
}
int main(int argc,char** argv) {
    try {
        CHECK(argc==2); const std::string mode=argv[1];
        if(mode=="config") ConfigTest(); else if(mode=="color") ColorTest(); else if(mode=="pool") PoolTest();
        else if(mode=="codec") CodecTest(); else if(mode=="queue") QueueTest();
#ifdef VIDEO_TEST_DECODER
        else if(mode=="roundtrip") RoundtripTest();
#endif
        else CHECK(false);
        std::cout<<"Video "<<mode<<" passed\n"; return 0;
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
