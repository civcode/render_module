#include "webrtc_media.hpp"
#include <algorithm>
#include <chrono>
#include <cerrno>
#include <cstring>
#include <mutex>
#include <stdexcept>
#include <sys/random.h>

namespace render_module::detail {
namespace {
std::atomic<unsigned> lossPercent{0};
std::uint64_t NowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
unsigned U16(const rtc::byte* p) { return (std::to_integer<unsigned>(p[0])<<8)|std::to_integer<unsigned>(p[1]); }
// Last in outgoing chain = first in incoming chain. Bound/validate RTCP lengths
// before handing untrusted compounds to the library's feedback handlers.
class PacketMonitor final : public rtc::MediaHandler {
    std::shared_ptr<RtcCounters> stats;
    std::mutex mutex;
    std::uint64_t rateStart=0, packetIndex=0;
    unsigned feedbackBudget=0;
public:
    explicit PacketMonitor(std::shared_ptr<RtcCounters> s):stats(std::move(s)) {}
    void incoming(rtc::message_vector& messages,const rtc::message_callback&) override {
        std::lock_guard<std::mutex> lock(mutex);
        const auto now=NowMs(); if(now-rateStart>=1000) { rateStart=now; feedbackBudget=0; }
        messages.erase(std::remove_if(messages.begin(),messages.end(),[&](const auto& m) {
            bool valid=m->type==rtc::Message::Control && m->size()>=4 && m->size()<=4096;
            unsigned requests=0,nacks=0; std::size_t pos=0;
            while(valid && pos<m->size()) {
                if(m->size()-pos<4) { valid=false; break; }
                const auto* p=m->data()+pos; const auto flags=std::to_integer<unsigned>(p[0]);
                const auto pt=std::to_integer<unsigned>(p[1]), fmt=flags&31;
                const auto size=std::size_t(U16(p+2)+1)*4;
                if((flags>>6)!=2 || (flags&32) || size<4 || size>m->size()-pos) { valid=false; break; }
                if(pt==205 && fmt==1) {
                    if(size<16 || size>12+64*4) { valid=false; break; }
                    ++nacks;
                    for(std::size_t i=12;i<size;i+=4) {
                        unsigned bits=U16(p+i+2); ++requests;
                        while(bits) { requests+=bits&1; bits>>=1; }
                    }
                }
                if(pt==206 && size<12) valid=false;
                if(pt==206 && fmt==15 && size>=20 && std::memcmp(p+12,"REMB",4)==0) {
                    if(size<20+4*std::to_integer<unsigned>(p[16])) valid=false;
                    // Bound exponent to the unsigned bitrate accepted by the library API.
                    if((std::to_integer<unsigned>(p[17])>>2)>14) valid=false;
                }
                pos+=size;
            }
            if(!valid || feedbackBudget+requests+1>2048) { ++stats->invalidRtcp; return true; }
            feedbackBudget+=requests+1; stats->nacks+=nacks; return false;
        }),messages.end());
    }
    void outgoing(rtc::message_vector& messages,const rtc::message_callback&) override {
        messages.erase(std::remove_if(messages.begin(),messages.end(),[&](const auto& m) {
            if(m->type==rtc::Message::Control) return false;
#ifdef RENDER_MODULE_WEBRTC_TESTING
            const auto loss=lossPercent.load();
            if(loss && (++packetIndex%100)<loss) { ++stats->testDropped; return true; }
#endif
            ++stats->packets; stats->bytes+=m->size(); return false;
        }),messages.end());
    }
};
class NackResponder final : public rtc::MediaHandler {
    rtc::RtcpNackResponder responder{WebRtcNackHistory};
    std::shared_ptr<RtcCounters> stats;
public:
    explicit NackResponder(std::shared_ptr<RtcCounters> s):stats(std::move(s)) {}
    void incoming(rtc::message_vector& m,const rtc::message_callback& send) override {
        responder.incoming(m,[&](rtc::message_ptr packet) {
            ++stats->retransmits; ++stats->packets; stats->bytes+=packet->size(); send(std::move(packet));
        });
    }
    void outgoing(rtc::message_vector& m,const rtc::message_callback& send) override { responder.outgoing(m,send); }
};
}
std::uint32_t RtcRandom() {
    std::uint32_t value; auto* p=reinterpret_cast<unsigned char*>(&value); std::size_t done=0;
    while(done<sizeof(value)) {
        const auto n=getrandom(p+done,sizeof(value)-done,0);
        if(n<0 && errno==EINTR) continue;
        if(n<=0) throw std::runtime_error("WebRTC randomness unavailable");
        done+=std::size_t(n);
    }
    return value;
}
std::uint32_t RtpTimestamp(std::uint32_t base,std::int64_t ptsUs) {
    if(ptsUs<0) throw std::invalid_argument("negative video PTS");
    const auto us=std::uint64_t(ptsUs);
    return base+std::uint32_t((us/1000000)*90000+(us%1000000)*9/100);
}
bool CompatibleAccessUnit(const video::EncodedFrame& f) {
    if(f.codec!=video::VideoCodec::H264 || f.format!=video::H264Format::AnnexB ||
       !f.storage || !f.size || f.size>f.storage->size() || f.size>WebRtcAccessUnitLimit || f.ptsUs<0 ||
       f.width<16 || f.height<16 || f.width>1920 || f.height>1080) return false;
    std::size_t cursor=0; video::NalUnit nal; bool sps=false,pps=false,idr=false,slice=false;
    while(video::NextAnnexBNal(f.storage->data(),f.size,cursor,nal)) {
        if(nal.type==7) {
            if(nal.size<4 || nal.data[1]!=66 || (nal.data[2]&0xc0)!=0xc0 || nal.data[3]>40) return false;
            sps=true;
        }
        pps|=nal.type==8; idr|=nal.type==5; slice|=nal.type==1 || nal.type==5;
    }
    return slice && idr==f.keyframe && (!idr || (sps && pps));
}
RtpSender::RtpSender(std::uint32_t ssrc,std::uint32_t base,std::uint16_t sequence,
                     std::shared_ptr<RtcCounters> stats,std::function<void()> keyframe):timestampBase(base) {
    config=std::make_shared<rtc::RtpPacketizationConfig>(ssrc,"render-module",WebRtcPayloadType,90000);
    config->startTimestamp=base; config->timestamp=base; config->sequenceNumber=sequence;
    handler=std::make_shared<rtc::H264RtpPacketizer>(rtc::NalUnit::Separator::StartSequence,config,WebRtcFragmentSize);
    handler->addToChain(std::make_shared<rtc::RtcpSrReporter>(config));
    handler->addToChain(std::make_shared<NackResponder>(stats));
    handler->addToChain(std::make_shared<rtc::PliHandler>([stats,keyframe] { ++stats->plis; keyframe(); }));
    handler->addToChain(std::make_shared<rtc::RembHandler>([stats](unsigned bps) {
        stats->rembBps=bps; stats->rembTimeMs=NowMs(); // Observation only, NEVER encoder bitrate control.
    }));
    handler->addToChain(std::make_shared<PacketMonitor>(std::move(stats)));
}
void SetWebRtcTestPacketLoss(unsigned percent) {
#ifdef RENDER_MODULE_WEBRTC_TESTING
    if(percent>3) throw std::invalid_argument("test loss limit");
    lossPercent=percent;
#else
    (void)percent; throw std::logic_error("WebRTC test hooks disabled");
#endif
}
} // namespace render_module::detail
