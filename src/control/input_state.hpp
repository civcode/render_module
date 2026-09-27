#pragma once
#include "input_protocol.hpp"
#include "input/remote_input_queue.hpp"
#include <functional>

namespace render_module::detail::dc {
struct Counters {
    std::uint64_t fastReceived=0, controlReceived=0, accepted=0, rejected=0, staleFast=0, queueFull=0, resyncs=0;
};
// Signaling-owner-thread only. Network callbacks never mutate controller or input state.
class InputState {
public:
    using Reply=std::function<void(Packet)>;
    InputState(RemoteInputQueue& queue,Reply reply):queue_(queue),reply_(std::move(reply)) {}
    bool Handle(const Packet&); // False is a protocol/state-machine violation: close the owning session.
    void SetController(bool);
    void RequireResync();
    void Close();
    bool Greeted() const { return greeted_; }
    bool Enabled() const { return enabled_; }
    bool Controller() const { return controller_; }
    std::uint32_t Epoch() const { return epoch_; }
    Counters counters;
private:
    RemoteInputQueue& queue_;
    Reply reply_;
    InputStateSnapshot held_{};
    FastFence fence_;
    std::optional<std::uint32_t> reliableSequence_;
    RemoteMouseSource source_=RemoteMouseSource::Mouse;
    bool greeted_=false, controller_=false, enabled_=false, closed_=false;
    std::uint32_t epoch_=1;
    bool Push(RemoteInputEvent);
    void Clear();
    void StateReply();
};
}
