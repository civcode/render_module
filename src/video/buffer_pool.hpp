#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <new>
#include <stdexcept>
#include <vector>

namespace render_module::video::detail {
// Fixed buffer AND shared_ptr control-block storage. Availability is explicit,
// synchronized, and tied to control-block destruction (including weak owners),
// never inferred from shared_ptr::use_count(). No per-lease heap allocation.
class FrameBufferPool {
    static constexpr std::size_t ControlBytes = 256;
    struct Slot {
        std::vector<std::uint8_t> bytes;
        alignas(std::max_align_t) std::array<std::byte,ControlBytes> control;
        bool reserved = false;
    };
    struct State {
        std::mutex mutex;
        std::array<Slot,4> slots;
        unsigned count;
        State(unsigned n,std::size_t bytes) : count(n) {
            if (!n || n>slots.size()) throw std::invalid_argument("video pool size");
            for(unsigned i=0;i<n;++i) slots[i].bytes.resize(bytes);
        }
    };
    template<class T> struct Allocator {
        using value_type = T;
        std::shared_ptr<State> state;
        unsigned index;
        Allocator(std::shared_ptr<State> s,unsigned i) : state(std::move(s)),index(i) {}
        template<class U> Allocator(const Allocator<U>& a) noexcept : state(a.state),index(a.index) {}
        T* allocate(std::size_t n) {
            static_assert(sizeof(T)<=ControlBytes && alignof(T)<=alignof(std::max_align_t),
                          "Standard library shared_ptr control block exceeds fixed video pool slot");
            if(n!=1) throw std::bad_alloc();
            return reinterpret_cast<T*>(state->slots[index].control.data());
        }
        void deallocate(T*,std::size_t) noexcept {
            // Allocator's shared State keeps both bytes and control storage alive
            // through the final weak release, even if the pool/encoder was destroyed.
            std::lock_guard<std::mutex> lock(state->mutex);
            state->slots[index].reserved=false;
        }
        template<class U> bool operator==(const Allocator<U>& b) const noexcept {
            return state==b.state && index==b.index;
        }
        template<class U> bool operator!=(const Allocator<U>& b) const noexcept { return !(*this==b); }
    };
public:
    FrameBufferPool(unsigned count,std::size_t bytes) : state_(std::make_shared<State>(count,bytes)) {}
    std::shared_ptr<const std::vector<std::uint8_t>> Acquire(std::uint8_t*& writable) {
        writable=nullptr;
        auto state=state_; unsigned index=state->count;
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            for(unsigned i=0;i<state->count;++i) if(!state->slots[i].reserved) {
                state->slots[i].reserved=true; index=i; break;
            }
        }
        if(index==state->count) return {};
        try {
            auto& bytes=state->slots[index].bytes;
            std::shared_ptr<const std::vector<std::uint8_t>> lease(&bytes,
                [](const std::vector<std::uint8_t>*) noexcept {}, Allocator<std::uint8_t>{state,index});
            writable=bytes.data(); return lease;
        } catch(...) {
            std::lock_guard<std::mutex> lock(state->mutex); state->slots[index].reserved=false; throw;
        }
    }
private:
    std::shared_ptr<State> state_;
};
} // namespace render_module::video::detail
