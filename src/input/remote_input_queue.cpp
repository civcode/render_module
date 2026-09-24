#include "remote_input_queue.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <type_traits>
#include <utility>

namespace render_module::detail {
namespace {
bool Coordinates(float& x, float& y) {
    if (!std::isfinite(x) || !std::isfinite(y)) return false;
    x = std::clamp(x, 0.0f, 1.0f);
    y = std::clamp(y, 0.0f, 1.0f);
    return true;
}
bool Utf8(const TextUtf8& text) {
    if (text.size > TextUtf8::MaxBytes || text.bytes[text.size] != '\0') return false;
    std::size_t i = 0;
    while (i < text.size) {
        const auto first = static_cast<unsigned char>(text.bytes[i++]);
        if (!first) return false;
        if (first < 0x80) continue;
        int continuation = 0;
        std::uint32_t code = 0, minimum = 0;
        if (first >= 0xc2 && first <= 0xdf) { continuation = 1; code = first & 0x1f; minimum = 0x80; }
        else if (first >= 0xe0 && first <= 0xef) { continuation = 2; code = first & 0x0f; minimum = 0x800; }
        else if (first >= 0xf0 && first <= 0xf4) { continuation = 3; code = first & 0x07; minimum = 0x10000; }
        else return false;
        while (continuation--) {
            if (i == text.size) return false;
            const auto byte = static_cast<unsigned char>(text.bytes[i++]);
            if ((byte & 0xc0) != 0x80) return false;
            code = (code << 6) | (byte & 0x3f);
        }
        if (code < minimum || code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff)) return false;
    }
    return true;
}
} // namespace

bool ValidateInputEvent(RemoteInputEvent& event) {
    return std::visit([](auto& value) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, MouseMove> || std::is_same_v<T, InputStateSnapshot>)
            return Coordinates(value.nx, value.ny);
        else if constexpr (std::is_same_v<T, MouseButton>)
            return static_cast<std::size_t>(value.button) < MouseButtonCount;
        else if constexpr (std::is_same_v<T, Key>)
            return static_cast<std::size_t>(value.key) < KeyCount;
        else if constexpr (std::is_same_v<T, MouseWheel>)
            return std::isfinite(value.horizontal) && std::isfinite(value.vertical);
        else if constexpr (std::is_same_v<T, MouseSource>)
            return value.source == RemoteMouseSource::Mouse || value.source == RemoteMouseSource::Touch ||
                   value.source == RemoteMouseSource::Pen;
        else if constexpr (std::is_same_v<T, TextUtf8>) return Utf8(value);
        else return true;
    }, event);
}

EnqueueResult RemoteInputQueue::Enqueue(RemoteInputEvent event,
                                        std::optional<std::uint64_t> sourceSequence) {
    if (!ValidateInputEvent(event)) return {EnqueueStatus::Invalid};
    std::lock_guard<std::mutex> lock(mutex_);
    if (closed_) return {EnqueueStatus::Closed};
    if (sourceSequence && lastSourceSequence_ && *sourceSequence <= *lastSourceSequence_)
        return {EnqueueStatus::Stale};
    const auto tail = (head_ + size_ + Capacity - 1) % Capacity;
    const bool coalesce = size_ && std::holds_alternative<MouseMove>(event) &&
                          std::holds_alternative<MouseMove>(events_[tail].event);
    if ((!coalesce && size_ == Capacity) || nextSequence_ == std::numeric_limits<std::uint64_t>::max())
        return {EnqueueStatus::Full};
    const auto sequence = ++nextSequence_;
    if (sourceSequence) lastSourceSequence_ = sourceSequence;
    if (coalesce) events_[tail] = {sequence, std::move(event)};
    else { events_[(head_ + size_) % Capacity] = {sequence, std::move(event)}; ++size_; }
    return {coalesce ? EnqueueStatus::Coalesced : EnqueueStatus::Accepted, sequence};
}
bool RemoteInputQueue::ReleaseAllInput() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (closed_) return false;
    head_ = size_ = 0;
    releaseAll_ = true;
    return true;
}
bool RemoteInputQueue::TakeReleaseAll() {
    std::lock_guard<std::mutex> lock(mutex_);
    const bool result = releaseAll_;
    releaseAll_ = false;
    return result;
}
bool RemoteInputQueue::TryPop(QueuedInput& event, bool allowPosition) {
    std::lock_guard<std::mutex> lock(mutex_);
    // A concurrently requested safety barrier is observed before any newer input.
    if (releaseAll_ || !size_) return false;
    if (!allowPosition && (std::holds_alternative<MouseMove>(events_[head_].event) ||
                           std::holds_alternative<InputStateSnapshot>(events_[head_].event))) return false;
    event = std::move(events_[head_]);
    head_ = (head_ + 1) % Capacity;
    --size_;
    return true;
}
std::size_t RemoteInputQueue::Size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return size_;
}
void RemoteInputQueue::Close() {
    std::lock_guard<std::mutex> lock(mutex_);
    closed_ = true;
    head_ = size_ = 0;
    releaseAll_ = false;
}
} // namespace render_module::detail
