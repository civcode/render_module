#include "input_protocol.hpp"
#include <cmath>
#include <cstring>
#include <limits>

namespace render_module::detail::dc {
namespace {
struct KeyEntry { std::uint16_t id; RenderKey key; };
constexpr KeyEntry keys[]={
#include "protocol_keys.inc"
};
static_assert(sizeof(keys)/sizeof(*keys)==KeyCount);
static_assert(sizeof(float)==4 && std::numeric_limits<float>::is_iec559);
struct Reader {
    const std::uint8_t* p; std::size_t left; bool ok=true;
    std::uint64_t Number(unsigned n) {
        if(n>left) { ok=false; return 0; }
        std::uint64_t v=0; while(n--) { v=(v<<8)|*p++; --left; } return v;
    }
    void U8(std::uint8_t& v) { v=std::uint8_t(Number(1)); }
    void U16(std::uint16_t& v) { v=std::uint16_t(Number(2)); }
    void U32(std::uint32_t& v) { v=std::uint32_t(Number(4)); }
    void U64(std::uint64_t& v) { v=Number(8); }
    void Float(float& v) { std::uint32_t bits=std::uint32_t(Number(4)); std::memcpy(&v,&bits,4); }
    void Bool(bool& v) { auto n=Number(1); ok&=n<=1; v=n==1; }
    void Text(TextUtf8& text) {
        if(left>TextUtf8::MaxBytes) { ok=false; return; }
        text.size=left; for(std::size_t i=0;i<text.size;++i) text.bytes[i]=char(Number(1));
    }
};
struct Writer {
    std::vector<std::uint8_t> bytes; bool ok=true;
    void Number(std::uint64_t v,unsigned n) { while(n--) bytes.push_back(std::uint8_t(v>>(8*n))); }
    void U8(std::uint8_t& v) { Number(v,1); } void U16(std::uint16_t& v) { Number(v,2); }
    void U32(std::uint32_t& v) { Number(v,4); } void U64(std::uint64_t& v) { Number(v,8); }
    void Float(float& v) { std::uint32_t bits; std::memcpy(&bits,&v,4); Number(bits,4); }
    void Bool(bool& v) { Number(v?1:0,1); }
    void Text(TextUtf8& v) {
        if(v.size>TextUtf8::MaxBytes) { ok=false; return; }
        bytes.insert(bytes.end(),v.bytes.begin(),v.bytes.begin()+v.size);
    }
};
bool Range(float n,float lo,float hi) { return std::isfinite(n) && n>=lo && n<=hi; }
bool Utf8(const TextUtf8& t) {
    if(!t.size || t.size>TextUtf8::MaxBytes) return false;
    for(std::size_t i=0;i<t.size;) {
        unsigned c=std::uint8_t(t.bytes[i++]), extra=0, minimum=0;
        if(c<0x80) { if(!c) return false; continue; }
        if(c>=0xc2 && c<=0xdf) { c&=31; extra=1; minimum=0x80; }
        else if(c>=0xe0 && c<=0xef) { c&=15; extra=2; minimum=0x800; }
        else if(c>=0xf0 && c<=0xf4) { c&=7; extra=3; minimum=0x10000; }
        else return false;
        if(extra>t.size-i) return false;
        while(extra--) { const unsigned b=std::uint8_t(t.bytes[i++]); if((b&0xc0)!=0x80) return false; c=(c<<6)|(b&63); }
        if(c<minimum || c>0x10ffff || (c>=0xd800 && c<=0xdfff)) return false;
    }
    return true;
}
template<class IO> void Position(IO& io,Snapshot& s) {
    io.Float(s.x); io.Float(s.y); io.U32(s.fence); io.U32(s.epoch);
    io.ok &= Range(s.x,0,1) && Range(s.y,0,1);
}
template<class IO> void State(IO& io,Snapshot& s) {
    Position(io,s); io.U8(s.source); io.U8(s.buttons); io.U8(s.modifiers); io.Bool(s.focused);
    io.ok &= s.source<=2 && s.buttons<32 && s.modifiers<16;
    for(auto& b:s.keys) io.U8(b);
    for(unsigned id=0;id<KeyBytes*8;++id)
        if((s.keys[id/8]&(1u<<(id%8))) && !DecodeKey(std::uint16_t(id))) io.ok=false;
}
template<class IO> void Viewport(IO& io,Packet& p) {
    io.U16(p.width); io.U16(p.height); io.Float(p.dpr);
    io.ok &= p.width>=1 && p.width<=16384 && p.height>=1 && p.height<=16384 && Range(p.dpr,.25f,8);
}
bool Allowed(Type type,Direction direction) {
    if(type==Type::Ping || type==Type::Pong) return true;
    const auto n=std::uint8_t(type);
    return direction==Direction::Client ? n>=1 && n<=12 : n>=128 && n<=133;
}
template<class IO> void Fields(IO& io,Packet& p,Direction direction) {
    if(!Allowed(p.type,direction)) { io.ok=false; return; }
    switch(p.type) {
    case Type::ClientHello:
        io.U16(p.minor); io.U32(p.capabilities); Viewport(io,p); State(io,p.state);
        io.ok &= (p.capabilities&Capabilities)==Capabilities; break;
    case Type::PointerMove:
        io.U32(p.state.fence); io.Float(p.state.x); io.Float(p.state.y); io.U8(p.state.source); io.U32(p.state.epoch);
        io.ok &= p.sequence==p.state.fence && Range(p.state.x,0,1) && Range(p.state.y,0,1) && p.state.source<=2; break;
    case Type::MouseButton:
        io.U8(p.button); io.Bool(p.down); Position(io,p.state); io.U8(p.state.source);
        io.ok &= p.button<5 && p.state.source<=2; break;
    case Type::MouseWheel:
        io.Float(p.horizontal); io.Float(p.vertical); Position(io,p.state); io.U8(p.state.source);
        io.ok &= Range(p.horizontal,-1000,1000) && Range(p.vertical,-1000,1000) && p.state.source<=2; break;
    case Type::Key:
        io.U16(p.key); io.Bool(p.down); io.U8(p.modifiers); io.U32(p.state.epoch);
        io.ok &= DecodeKey(p.key).has_value() && p.modifiers<16; break;
    case Type::TextUtf8: io.U32(p.state.epoch); io.Text(p.text); io.ok &= Utf8(p.text); break;
    case Type::Focus: io.Bool(p.state.focused); io.U32(p.state.epoch); break;
    case Type::Snapshot: State(io,p.state); break;
    case Type::ViewportRequest: Viewport(io,p); break;
    case Type::AcquireControl: case Type::ReleaseControl: break;
    case Type::BrowserStats: io.U32(p.fastBuffered); io.U32(p.controlBuffered); break;
    case Type::Ping: case Type::Pong: io.U64(p.echoUs); break;
    case Type::ServerHello:
        io.U16(p.minor); io.U32(p.capabilities); io.U32(p.state.epoch); io.Bool(p.control); io.U16(p.code);
        io.ok &= p.code>=HeaderSize && p.code<=MaxPacketSize; break;
    case Type::ViewportAccepted:
        io.U16(p.width); io.U16(p.height); io.ok &= p.width>0 && p.height>0 && p.width<=2048 && p.height<=2048; break;
    case Type::ControlState:
        io.Bool(p.control); io.Bool(p.enabled); io.U32(p.state.epoch); io.ok &= !p.enabled || p.control; break;
    case Type::ResyncRequired: io.U32(p.state.epoch); break;
    case Type::StreamState: io.U16(p.code); io.ok &= p.code<=2; break;
    case Type::Error: io.U16(p.code); io.ok &= p.code>=1 && p.code<=8; break;
    }
}
}
std::optional<RenderKey> DecodeKey(std::uint16_t id) noexcept {
    for(const auto& entry:keys) if(entry.id==id) return entry.key;
    return {};
}
std::optional<std::uint16_t> EncodeKey(RenderKey key) noexcept {
    for(const auto& entry:keys) if(entry.key==key) return entry.id;
    return {};
}
bool Decode(const std::uint8_t* data,std::size_t size,Lane lane,Direction direction,Packet& output) noexcept {
    if(!data || size<HeaderSize || size>MaxPacketSize) return false;
    Reader in{data,size}; Packet p; std::uint8_t version=0,type=0; std::uint16_t length=0;
    in.U8(version); in.U8(type); in.U16(length); in.U32(p.sequence); in.U64(p.timestampUs); p.type=Type(type);
    if(version!=Major || length!=size-HeaderSize || !Allowed(p.type,direction) ||
       ((p.type==Type::PointerMove)!=(lane==Lane::Fast))) return false;
    Fields(in,p,direction);
    if(!in.ok || in.left) return false;
    output=p; return true;
}
std::vector<std::uint8_t> Encode(const Packet& value,Direction direction) {
    Packet p=value; Writer out; out.bytes.reserve(MaxPacketSize);
    out.Number(Major,1); out.Number(std::uint8_t(p.type),1); out.Number(0,2);
    out.U32(p.sequence); out.U64(p.timestampUs); Fields(out,p,direction);
    if(!out.ok || out.bytes.size()>MaxPacketSize) return {};
    const auto n=out.bytes.size()-HeaderSize; out.bytes[2]=std::uint8_t(n>>8); out.bytes[3]=std::uint8_t(n);
    return std::move(out.bytes);
}
InputStateSnapshot InputSnapshot(const Snapshot& s) {
    InputStateSnapshot input; input.nx=s.x; input.ny=s.y; input.focused=s.focused;
    for(const auto& entry:keys) if(s.keys[entry.id/8]&(1u<<(entry.id%8))) input.keys.set(std::size_t(entry.key));
    for(unsigned n=0;n<5;++n) input.mouseButtons[n]=bool(s.buttons&(1u<<n));
    input.ctrl=s.modifiers&1; input.shift=s.modifiers&2; input.alt=s.modifiers&4; input.super=s.modifiers&8;
    return input;
}
bool Newer(std::uint32_t a,std::uint32_t b) noexcept { const auto delta=std::uint32_t(a-b); return delta && delta<0x80000000u; }
bool FastFence::Accept(std::uint32_t value) noexcept {
    if(floor_ && !Newer(value,*floor_)) return false;
    floor_=value; return true;
}
void FastFence::Advance(std::uint32_t value) noexcept { if(!floor_ || Newer(value,*floor_)) floor_=value; }
} // namespace render_module::detail::dc
