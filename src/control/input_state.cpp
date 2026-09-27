#include "input_state.hpp"
#include <utility>
namespace render_module::detail::dc {
namespace {
void Modifiers(InputStateSnapshot& s) {
    const auto pair=[&](RenderKey left,RenderKey right,bool held) {
        const auto l=std::size_t(left), r=std::size_t(right);
        if(!held) { s.keys[l]=false; s.keys[r]=false; }
        else if(!s.keys[l] && !s.keys[r]) s.keys[l]=true;
    };
    pair(RenderKey::LeftCtrl,RenderKey::RightCtrl,s.ctrl);
    pair(RenderKey::LeftShift,RenderKey::RightShift,s.shift);
    pair(RenderKey::LeftAlt,RenderKey::RightAlt,s.alt);
    pair(RenderKey::LeftSuper,RenderKey::RightSuper,s.super);
}
}
void InputState::Clear() {
    held_={}; held_.focused=false; enabled_=false;
    if(++epoch_==0) ++epoch_;
}
void InputState::StateReply() {
    if(!greeted_ || closed_) return;
    Packet p; p.type=Type::ControlState; p.control=controller_; p.enabled=enabled_; p.state.epoch=epoch_; reply_(p);
}
void InputState::SetController(bool value) {
    if(closed_ || controller_==value) return;
    if(controller_) queue_.ReleaseAllInput();
    controller_=value; Clear(); StateReply();
}
void InputState::Close() {
    if(closed_) return;
    if(controller_) queue_.ReleaseAllInput();
    closed_=true; controller_=false; Clear();
}
void InputState::RequireResync() {
    if(closed_ || !controller_) return;
    queue_.ReleaseAllInput(); Clear(); ++counters.resyncs;
    if(greeted_) { Packet p; p.type=Type::ResyncRequired; p.state.epoch=epoch_; reply_(p); }
}
bool InputState::Push(RemoteInputEvent event) {
    const auto result=queue_.Enqueue(std::move(event));
    if(result.Accepted()) { ++counters.accepted; return true; }
    ++counters.rejected;
    if(result.status==EnqueueStatus::Full) ++counters.queueFull;
    RequireResync(); return false;
}
bool InputState::Handle(const Packet& p) {
    if(closed_) return false;
    if(p.type==Type::PointerMove) ++counters.fastReceived;
    else {
        ++counters.controlReceived;
        if(reliableSequence_ && !Newer(p.sequence,*reliableSequence_)) return false;
        reliableSequence_=p.sequence;
    }
    if(p.type==Type::ClientHello) {
        if(greeted_) return false;
        greeted_=true;
        Packet hello; hello.type=Type::ServerHello; hello.minor=0; hello.code=MaxPacketSize;
        hello.control=controller_; hello.state.epoch=epoch_; reply_(hello); return true;
    }
    if(!greeted_) { if(p.type==Type::PointerMove) { ++counters.rejected; return true; } return false; }
    if(p.type==Type::Ping) { Packet pong; pong.type=Type::Pong; pong.echoUs=p.echoUs; reply_(pong); return true; }
    if(p.type==Type::Pong || p.type==Type::BrowserStats || p.type==Type::AcquireControl || p.type==Type::ReleaseControl) return true;
    if(!controller_) { ++counters.rejected; if(p.type==Type::Snapshot) StateReply(); return true; }
    if(p.type==Type::ViewportRequest) return true; // Caller publishes only for the authorized controller.
    if(p.state.epoch!=epoch_) { ++counters.rejected; if(p.type==Type::PointerMove) ++counters.staleFast; return true; }
    if(p.type==Type::Snapshot) {
        fence_.Advance(p.state.fence);
        auto next=InputSnapshot(p.state); Modifiers(next);
        if(!next.focused) { next.keys.reset(); next.mouseButtons.reset(); next.ctrl=next.shift=next.alt=next.super=false; }
        if(!Push(MouseSource{RemoteMouseSource(p.state.source)}) || !Push(next)) return true;
        source_=RemoteMouseSource(p.state.source); held_=next; enabled_=true; StateReply(); return true;
    }
    if(p.type==Type::Focus && !p.state.focused) { RequireResync(); return true; }
    if(!enabled_ || !held_.focused) { ++counters.rejected; return true; }
    if(p.type==Type::PointerMove) {
        if(!fence_.Accept(p.state.fence)) { ++counters.staleFast; return true; }
        if(source_!=RemoteMouseSource(p.state.source) && !Push(MouseSource{RemoteMouseSource(p.state.source)})) return true;
        source_=RemoteMouseSource(p.state.source);
        held_.nx=p.state.x; held_.ny=p.state.y; Push(MouseMove{held_.nx,held_.ny}); return true;
    }
    if(p.type==Type::MouseButton || p.type==Type::MouseWheel) {
        fence_.Advance(p.state.fence); held_.nx=p.state.x; held_.ny=p.state.y;
        if(source_!=RemoteMouseSource(p.state.source) && !Push(MouseSource{RemoteMouseSource(p.state.source)})) return true;
        source_=RemoteMouseSource(p.state.source);
        if(!Push(MouseMove{held_.nx,held_.ny})) return true;
        if(p.type==Type::MouseButton) {
            held_.mouseButtons[p.button]=p.down; Push(MouseButton{RemoteMouseButton(p.button),p.down});
        } else Push(MouseWheel{p.horizontal,p.vertical});
        return true;
    }
    if(p.type==Type::Key) {
        const auto key=*DecodeKey(p.key);
        held_.keys[std::size_t(key)]=p.down;
        held_.ctrl=p.modifiers&1; held_.shift=p.modifiers&2; held_.alt=p.modifiers&4; held_.super=p.modifiers&8;
        // Keep local physical modifier state consistent with Phase 4 reconciliation.
        Modifiers(held_); Push(held_); return true;
    }
    if(p.type==Type::TextUtf8) {
        // Retain Phase 5 committed-text behavior: shortcuts must not swallow paste/IME text.
        const RenderKey shortcuts[]={RenderKey::LeftCtrl,RenderKey::RightCtrl,RenderKey::LeftSuper,RenderKey::RightSuper};
        for(auto key:shortcuts) if(held_.keys[std::size_t(key)] && !Push(Key{key,false})) return true;
        if(!Push(p.text)) return true;
        for(auto key:shortcuts) if(held_.keys[std::size_t(key)] && !Push(Key{key,true})) return true;
        return true;
    }
    if(p.type==Type::Focus) return true; // Regaining focus requires a snapshot, not an edge.
    return false;
}
}
