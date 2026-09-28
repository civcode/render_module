#include "control/input_protocol.hpp"
#include "control/input_state.hpp"
#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <iostream>
#include <random>
#include <stdexcept>
#include <cstring>
#include <iomanip>
using namespace render_module::detail;
namespace d=render_module::detail::dc;

TEST_CASE("Binary input protocol", "[input][protocol]") {
    std::vector<std::vector<std::uint8_t>> corpus;
    for(unsigned id=0;id<128;++id) {
        auto key=d::DecodeKey(std::uint16_t(id)); REQUIRE((bool(key)==(id>=1 && id<=105)));
        if(key) REQUIRE((d::EncodeKey(*key)==id));
    }
    for(unsigned t=1;t<256;++t) {
        d::Packet p; p.type=d::Type(t); p.sequence=0x12345678; p.timestampUs=0x0102030405060708ULL;
        p.state.fence=p.sequence; p.state.x=.25f; p.state.y=.75f; p.state.focused=true;
        p.width=1280; p.height=720; p.key=1; p.text=TextUtf8{"hé😀"}; p.code=t==128?512:1;
        auto direction=t<128?d::Direction::Client:d::Direction::Server;
        auto bytes=d::Encode(p,direction); const bool known=t<=14 || (t>=128 && t<=133);
        REQUIRE((!bytes.empty()==known)); if(!known) continue;
        REQUIRE((bytes[0]==1 && bytes[1]==t && bytes[4]==0x12 && bytes[7]==0x78 && bytes[8]==1 && bytes[15]==8));
        const auto lane=t==2?d::Lane::Fast:d::Lane::Control;
        d::Packet out; REQUIRE((d::Decode(bytes.data(),bytes.size(),lane,direction,out)));
        REQUIRE((out.type==p.type && out.sequence==p.sequence && out.timestampUs==p.timestampUs));
        REQUIRE((d::Encode(out,direction)==bytes)); corpus.push_back(bytes);
        for(std::size_t n=0;n<bytes.size();++n) REQUIRE((!d::Decode(bytes.data(),n,lane,direction,out)));
        bytes.push_back(0); REQUIRE((!d::Decode(bytes.data(),bytes.size(),lane,direction,out))); bytes.pop_back();
        bytes[0]=2; REQUIRE((!d::Decode(bytes.data(),bytes.size(),lane,direction,out))); bytes[0]=1;
        REQUIRE((!d::Decode(bytes.data(),bytes.size(),lane==d::Lane::Fast?d::Lane::Control:d::Lane::Fast,direction,out)));
        if(t!=13 && t!=14) REQUIRE((!d::Decode(bytes.data(),bytes.size(),lane,direction==d::Direction::Client?d::Direction::Server:d::Direction::Client,out)));
    }
    for(const auto& text:{std::string(257,'a'),std::string("\xc0\x80",2),std::string("\xed\xa0\x80",3),
                         std::string("\xf4\x90\x80\x80",4),std::string("\xe2\x82",2),std::string("\0",1)}) {
        d::Packet p; p.type=d::Type::TextUtf8; p.text=TextUtf8{text}; REQUIRE((d::Encode(p,d::Direction::Client).empty()));
    }
    d::Packet p; p.type=d::Type::TextUtf8; p.text=TextUtf8{std::string(256,'x')};
    REQUIRE((d::Encode(p,d::Direction::Client).size()==276));
    p.type=d::Type::PointerMove; p.state.x=std::numeric_limits<float>::quiet_NaN(); REQUIRE((d::Encode(p,d::Direction::Client).empty()));
    p.state.x=std::numeric_limits<float>::infinity(); REQUIRE((d::Encode(p,d::Direction::Client).empty()));
    p.state.x=-.01f; REQUIRE((d::Encode(p,d::Direction::Client).empty()));
    d::FastFence f; REQUIRE((f.Accept(0xfffffffe))); REQUIRE((!f.Accept(0xfffffffe))); REQUIRE((f.Accept(0xffffffff)));
    REQUIRE((f.Accept(0))); REQUIRE((f.Accept(1))); REQUIRE((!f.Accept(0xffffffff))); REQUIRE((!f.Accept(0x80000001)));
    f.Advance(4); REQUIRE((!f.Accept(3))); f.Advance(2); REQUIRE((!f.Accept(4))); REQUIRE((f.Accept(5)));
    RemoteInputQueue q; std::vector<d::Packet> replies;
    d::InputState state(q,[&](auto r){replies.push_back(r);}); state.SetController(true);
    unsigned seq=0;
    auto send=[&](d::Packet v) { v.sequence=++seq; REQUIRE((state.Handle(v))); };
    p={}; p.type=d::Type::ClientHello; p.width=1280; p.height=720; send(p); REQUIRE((!state.Enabled()));
    REQUIRE((replies.back().type==d::Type::ServerHello));
    p={}; p.type=d::Type::Snapshot; p.state.epoch=state.Epoch(); p.state.focused=true; p.state.fence=100; send(p);
    REQUIRE((state.Enabled())); QueuedInput event; while(q.TryPop(event)) {}
    p={}; p.type=d::Type::MouseButton; p.state.epoch=state.Epoch(); p.state.fence=102; p.state.x=.8f; p.state.y=.4f; p.down=true; send(p);
    REQUIRE((q.TryPop(event) && std::get<MouseMove>(event.event).nx==.8f));
    REQUIRE((q.TryPop(event) && std::get<MouseButton>(event.event).down));
    p.type=d::Type::PointerMove; p.sequence=p.state.fence=101; p.state.x=.1f; REQUIRE((state.Handle(p))); REQUIRE((!q.TryPop(event)));
    p.sequence=p.state.fence=103; REQUIRE((state.Handle(p))); REQUIRE((q.TryPop(event) && std::get<MouseMove>(event.event).nx==.1f));
    for(unsigned n=0;n<RemoteInputQueue::Capacity;++n) REQUIRE((q.Enqueue(Key{RenderKey::A,bool(n&1)}).Accepted()));
    p.type=d::Type::Key; p.key=1; p.down=true; send(p);
    REQUIRE((!state.Enabled() && state.counters.queueFull==1 && q.Size()==0 && q.TakeReleaseAll()));
    REQUIRE((replies.back().type==d::Type::ResyncRequired));
    p.type=d::Type::MouseButton; send(p); REQUIRE((q.Size()==0)); // Old epoch cannot restore edges.
    p.type=d::Type::Snapshot; p.state.epoch=state.Epoch(); p.state.focused=true; send(p); REQUIRE((state.Enabled()));
    while(q.TryPop(event)) {}
    state.SetController(false); REQUIRE((q.TakeReleaseAll())); p.type=d::Type::Key; send(p); REQUIRE((q.Size()==0));
    state.SetController(true); REQUIRE((!state.Enabled())); p.type=d::Type::Snapshot; send(p); REQUIRE((!state.Enabled()));
    p.state.epoch=state.Epoch(); send(p); REQUIRE((state.Enabled())); state.Close(); REQUIRE((q.TakeReleaseAll()));
    // Bounded deterministic parser fuzz. Every accepted random packet must round-trip.
    std::mt19937 random(0x5a17); std::array<std::uint8_t,600> bytes{};
    for(unsigned n=0;n<100000;++n) {
        for(auto& b:bytes) b=std::uint8_t(random());
        auto size=random()%bytes.size();
        if(n&1) {
            const auto& seed=corpus[random()%corpus.size()]; size=seed.size();
            std::copy(seed.begin(),seed.end(),bytes.begin());
            for(unsigned m=0;m<3;++m) bytes[random()%size]^=std::uint8_t(1u<<(random()%8));
            if(n&2) size=random()%size;
            if(size>=16) { bytes[2]=std::uint8_t((size-16)>>8); bytes[3]=std::uint8_t(size-16); }
        }
        d::Packet out; out.sequence=0xdeadbeef;
        if(!d::Decode(bytes.data(),size,d::Lane(n&1),d::Direction((n>>1)&1),out)) REQUIRE((out.sequence==0xdeadbeef));
        else REQUIRE((!d::Encode(out,d::Direction((n>>1)&1)).empty()));
    }
}
