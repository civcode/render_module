#include "webrtc/webrtc_media.hpp"
#include <cstring>
#include <iostream>
#include <limits>
#include <thread>
#include <stdexcept>
using namespace render_module;
using namespace render_module::detail;
#define CHECK(x) do { if(!(x)) throw std::runtime_error("line "+std::to_string(__LINE__)+": " #x); } while(false)
namespace {
void ClockTest() {
    for(std::int64_t us:{0LL,1LL,11LL,12LL,33333LL,1000000LL,1000000000000LL})
        CHECK(RtpTimestamp(0xfffffff0,us)==std::uint32_t(0xfffffff0ULL+std::uint64_t(us)*9/100));
    CHECK(RtpTimestamp(1,std::numeric_limits<std::int64_t>::max())==
        std::uint32_t(1+(std::uint64_t(std::numeric_limits<std::int64_t>::max())/100)*9+
        (std::uint64_t(std::numeric_limits<std::int64_t>::max())%100)*9/100));
    for(std::int64_t n=1;n<10000;++n) CHECK(RtpTimestamp(73,n*1000000/30)==73+std::uint32_t((n*1000000/30)*9/100));
}
video::EncodedFrame Synthetic(bool key=true) {
    auto bytes=std::make_shared<std::vector<std::uint8_t>>();
    if(key) *bytes={0,0,0,1,0x67,66,0xc0,40,1,2,0,0,1,0x68,3,4,0,0,0,1,0x65};
    else *bytes={0,0,1,0x41};
    bytes->insert(bytes->end(),key?7000:20,0x55);
    video::EncodedFrame f;f.storage=bytes;f.size=bytes->size();f.keyframe=key;f.width=1280;f.height=720;f.ptsUs=33333;return f;
}
rtc::message_vector Packetize(RtpSender& sender,const video::EncodedFrame& f,rtc::message_vector& control) {
    const auto* p=reinterpret_cast<const rtc::byte*>(f.storage->data());
    rtc::message_vector packets{rtc::make_message(p,p+f.size,std::make_shared<rtc::FrameInfo>(RtpTimestamp(sender.timestampBase,f.ptsUs)))};
    sender.handler->outgoingChain(packets,[&](auto m){control.push_back(std::move(m));});return packets;
}
void PacketTest() {
    auto a=std::make_shared<RtcCounters>(),b=std::make_shared<RtcCounters>();
    RtpSender one(111,123,65534,a,[]{}),two(222,321,19,b,[]{});
    auto frame=Synthetic();CHECK(CompatibleAccessUnit(frame)); rtc::message_vector control;
    const auto packets=Packetize(one,frame,control),other=Packetize(two,frame,control);
    CHECK(packets.size()>6 && packets.size()==other.size() && a->packets==packets.size());
    std::vector<std::uint8_t> recovered;unsigned starts=0,ends=0;
    for(std::size_t i=0;i<packets.size();++i) {
        const auto& p=packets[i];const auto* h=reinterpret_cast<const rtc::RtpHeader*>(p->data());
        CHECK(h->ssrc()==111 && h->seqNumber()==std::uint16_t(65534+i));
        CHECK(h->timestamp()==RtpTimestamp(123,frame.ptsUs) && p->size()<=WebRtcFragmentSize+12);
        const auto* k=reinterpret_cast<const rtc::RtpHeader*>(other[i]->data());CHECK(k->ssrc()==222 && k->seqNumber()==19+i && k->timestamp()!=h->timestamp());
        const auto* data=reinterpret_cast<const std::uint8_t*>(p->data())+12;
        if(i==0) CHECK((data[0]&31)==7); else if(i==1) CHECK((data[0]&31)==8);
        else { CHECK((data[0]&31)==28 && (data[1]&31)==5);starts+=bool(data[1]&128);ends+=bool(data[1]&64);
            recovered.insert(recovered.end(),data+2,data+p->size()-12); }
        CHECK(bool(h->marker())==(i==packets.size()-1));
    }
    CHECK(starts==1 && ends==1 && recovered==std::vector<std::uint8_t>(7000,0x55));
    CHECK(!control.empty());bool report=false;
    for(const auto& m:control) {const auto* h=reinterpret_cast<const rtc::RtcpHeader*>(m->data());if(h->payloadType()==200)report=true;}
    CHECK(report);
    frame=Synthetic(false);frame.ptsUs=66666;
    const auto p=Packetize(one,frame,control);CHECK(p.size()==1 && (std::to_integer<unsigned>((*p[0])[12])&31)==1);
}
void FeedbackTest() {
    auto counters=std::make_shared<RtcCounters>();unsigned force=0;
    RtpSender sender(1234,456,100,counters,[&]{++force;});rtc::message_vector control;
    auto packets=Packetize(sender,Synthetic(),control);
    auto nack=rtc::make_message(rtc::RtcpNack::Size(1),rtc::Message::Control);
    auto* n=reinterpret_cast<rtc::RtcpNack*>(nack->data());n->preparePacket(1234,1);n->parts[0].setPid(100);n->parts[0].setBlp(0);
    rtc::message_vector incoming{nack},resent;
    sender.handler->incomingChain(incoming,[&](auto m){resent.push_back(m);});
    CHECK(counters->nacks==1 && counters->retransmits==1 && resent.size()==1 && *resent[0]==*packets[0]);
    auto pli=rtc::make_message(rtc::RtcpPli::Size(),rtc::Message::Control);
    reinterpret_cast<rtc::RtcpPli*>(pli->data())->preparePacket(1234);incoming={pli};
    sender.handler->incomingChain(incoming,[](auto){});CHECK(force==1 && counters->plis==1);
    auto remb=rtc::make_message(rtc::RtcpRemb::SizeWithSSRCs(1),rtc::Message::Control);
    auto* r=reinterpret_cast<rtc::RtcpRemb*>(remb->data());r->preparePacket(99,1,2000000);r->setSSRC(0,1234);
    incoming={remb};sender.handler->incomingChain(incoming,[](auto){});CHECK(counters->rembBps==2000000 && counters->rembTimeMs>0);
    auto invalid=rtc::make_message(4,rtc::Message::Control);(*invalid)[0]=rtc::byte{0x81};(*invalid)[1]=rtc::byte{205};
    incoming={invalid};sender.handler->incomingChain(incoming,[](auto){});CHECK(incoming.empty() && counters->invalidRtcp==1);
    for(unsigned i=0;i<WebRtcNackHistory+1;++i) {auto f=Synthetic(false);f.ptsUs=100000+i*33333;Packetize(sender,f,control);}
    incoming={nack};sender.handler->incomingChain(incoming,[](auto){});CHECK(counters->retransmits==1); // History evicted.
}
void SdpTest() {
    WebConfig c; CHECK(ValidateIceConfiguration(c));c.iceRelayOnly=true;CHECK(!ValidateIceConfiguration(c));
    for(const auto* url:{"turn:localhost:3478","turn:localhost:3478?transport=tcp"}) {
        c.iceServers={{url,"user","password"}};CHECK(ValidateIceConfiguration(c));
    }
    c.iceServers={{"turns:localhost:5349","user","password"}};CHECK(!ValidateIceConfiguration(c));
    c.iceServers={{"turn:localhost:5349?transport=tls","user","password"}};CHECK(!ValidateIceConfiguration(c));
    c.iceServers={{"turn:user:password@localhost","user","password"}};CHECK(!ValidateIceConfiguration(c));
    CHECK(ValidateRtcCandidate("candidate:1 1 UDP 2122260223 127.0.0.1 50000 typ host","video"));
    CHECK(!ValidateRtcCandidate("candidate:1 1 UDP 1 127.0.0.1 70000 typ host","video"));
    CHECK(!ValidateRtcCandidate("garbage","video"));CHECK(!ValidateRtcAnswer("garbage"));
    CHECK(!ValidateRtcAnswer(std::string(WebRtcSdpLimit+1,'a')));
    const auto answer=[](std::string fmtp) {
        return "v=0\r\no=- 1 2 IN IP4 127.0.0.1\r\ns=-\r\nt=0 0\r\na=group:BUNDLE video\r\n"
            "m=video 9 UDP/TLS/RTP/SAVPF 96\r\nc=IN IP4 0.0.0.0\r\na=mid:video\r\na=recvonly\r\na=rtcp-mux\r\n"
            "a=ice-ufrag:test\r\na=ice-pwd:012345678901234567890123\r\na=setup:active\r\n"
            "a=fingerprint:sha-256 00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00\r\n"
            "a=rtpmap:96 H264/90000\r\na=fmtp:96 packetization-mode=1;"+fmtp+"\r\n";
    };
    CHECK(ValidateRtcAnswer(answer("profile-level-id=42c028")));
    CHECK(ValidateRtcAnswer(answer("profile-level-id=42e01f;max-recv-level=e028")));
    CHECK(!ValidateRtcAnswer(answer("profile-level-id=42e01f;level-asymmetry-allowed=1")));
    CHECK(!ValidateRtcAnswer(answer("profile-level-id=42e01f;max-recv-level=e01f")));
    CHECK(!ValidateRtcAnswer(answer("profile-level-id=42e01f;max-recv-level=ffff")));
    CHECK(!ValidateRtcAnswer(answer("profile-level-id=640028")));
    CHECK(!ValidateRtcAnswer(answer("profile-level-id=42c028;profile-level-id=42c028")));
    CHECK(!ValidateRtcAnswer(answer("profile-level-id=42c028;packetization-mode=0")));
    auto badDirection=answer("profile-level-id=42c028");badDirection.replace(badDirection.find("recvonly"),8,"sendrecv");
    CHECK(!ValidateRtcAnswer(badDirection));
    auto encoder=video::CreateOpenH264Encoder();video::RgbaFramePool pool;video::I420Converter converter;
    std::uint64_t id=0;
    for(const auto size:{std::pair<int,int>{1280,720},{1600,900},{1920,1080},{1280,720}}) {
        video::EncoderConfig cfg;cfg.width=size.first;cfg.height=size.second;cfg.framebufferGeneration=++id;
        CHECK(encoder->Configure(cfg) && pool.Configure(cfg.width,cfg.height) && converter.Configure(cfg.width,cfg.height));
        video::VideoFrame rgba,yuv;std::uint8_t* data;CHECK(pool.Acquire(rgba,data));std::memset(data,127,rgba.storage->size());
        rgba.frameId=id;rgba.framebufferGeneration=id;rgba.ptsUs=id*33333;CHECK(converter.Convert(rgba,yuv));
        CHECK(encoder->Encode(yuv)==video::EncodeResult::Produced);video::EncodedFrame f;CHECK(encoder->TryReceive(f));
        CHECK(CompatibleAccessUnit(f));
    }
}
void LifecycleTest() {
    auto pipeline=std::make_shared<video::VideoPipeline>();CHECK(pipeline->Configure({}));
    EncodedFrameHub hub(pipeline,{});
    for(int n=0;n<8;++n) {
        auto s=hub.Create(std::string(31,'a')+char('a'+n));CHECK(s);
        RtcSignal signal;bool offer=false;
        for(int i=0;i<200 && !offer;++i) {while(s->Poll(signal))if(signal.type=="offer")offer=true;std::this_thread::sleep_for(std::chrono::milliseconds(5));}
        CHECK(offer);hub.Remove(s->id());
        CHECK(!s->AddRemoteCandidate("candidate:1 1 UDP 1 127.0.0.1 50000 typ host","video"));
        CHECK(!s->SetRemoteDescription("garbage"));CHECK(!s->RemoteIceComplete());
    }
    CHECK(hub.Snapshot().empty());
    for(int n=0;n<3;++n) CHECK(hub.Create(std::string(31,'b')+char('a'+n))); // Destruct while negotiating.
}
}
int main(int argc,char** argv) {
    try {
        CHECK(argc==2);const std::string mode=argv[1];
        if(mode=="clock")ClockTest();else if(mode=="packetizer")PacketTest();else if(mode=="feedback")FeedbackTest();
        else if(mode=="sdp")SdpTest();else if(mode=="lifecycle")LifecycleTest();else CHECK(false);
        std::cout<<"WebRTC "<<mode<<" passed\n";return 0;
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
