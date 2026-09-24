#pragma once

#include "remote_input_events.hpp"
#include <mutex>
#include <optional>

namespace render_module::detail {

enum class EnqueueStatus { Accepted, Coalesced, Full, Invalid, Stale, Closed };
struct EnqueueResult {
    EnqueueStatus status;
    std::uint64_t sequence = 0; // Assigned at acceptance under the mutex.
    bool Accepted() const { return status == EnqueueStatus::Accepted || status == EnqueueStatus::Coalesced; }
};
struct QueuedInput {
    std::uint64_t sequence = 0;
    RemoteInputEvent event = MouseMove{};
};

// Multi-producer, single render-thread consumer. No ImGui or GL dependency.
class RemoteInputQueue {
public:
    static constexpr std::size_t Capacity = 256;
    [[nodiscard]] EnqueueResult Enqueue(RemoteInputEvent event,
                          std::optional<std::uint64_t> sourceSequence = {});
    // Explicit cancellation barrier, not a normal overflow policy. Always
    // available while open, even when full. Cancels older queued input; the
    // consumer clears its ImGui backlog and synthesizes all releases next frame.
    // Input accepted after this call is retained, processed AFTER the reset frame.
    bool ReleaseAllInput();
    bool TakeReleaseAll();
    // Consumer may leave position-bearing events normalized in this queue
    // until ImGui has consumed all earlier transitions (including across resize).
    bool TryPop(QueuedInput& event, bool allowPosition = true);
    std::size_t Size() const;
    void Close(); // Retained producer handles subsequently return Closed.
private:
    mutable std::mutex mutex_;
    std::array<QueuedInput, Capacity> events_{};
    std::size_t head_ = 0, size_ = 0;
    std::uint64_t nextSequence_ = 0;
    std::optional<std::uint64_t> lastSourceSequence_;
    bool releaseAll_ = false, closed_ = false;
};

} // namespace render_module::detail
