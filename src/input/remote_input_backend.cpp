#include "remote_input_backend.hpp"

#include <algorithm>
#include <cmath>
#include <type_traits>
// Pinned-version dependency, isolated here: don't append another bounded batch
// while ImGui is still trickling the previous one. Otherwise its queue grows
// without bound even though the producer queue is bounded.
#include <imgui_internal.h>

namespace render_module::detail {

ImGuiKey MapRenderKey(RenderKey key) {
    static constexpr ImGuiKey mapping[] = {
        ImGuiKey_A, ImGuiKey_B, ImGuiKey_C, ImGuiKey_D, ImGuiKey_E, ImGuiKey_F,
        ImGuiKey_G, ImGuiKey_H, ImGuiKey_I, ImGuiKey_J, ImGuiKey_K, ImGuiKey_L,
        ImGuiKey_M, ImGuiKey_N, ImGuiKey_O, ImGuiKey_P, ImGuiKey_Q, ImGuiKey_R,
        ImGuiKey_S, ImGuiKey_T, ImGuiKey_U, ImGuiKey_V, ImGuiKey_W, ImGuiKey_X, ImGuiKey_Y, ImGuiKey_Z,
        ImGuiKey_0, ImGuiKey_1, ImGuiKey_2, ImGuiKey_3, ImGuiKey_4, ImGuiKey_5,
        ImGuiKey_6, ImGuiKey_7, ImGuiKey_8, ImGuiKey_9,
        ImGuiKey_Escape, ImGuiKey_Enter, ImGuiKey_Tab, ImGuiKey_Backspace, ImGuiKey_Delete,
        ImGuiKey_Insert, ImGuiKey_Space, ImGuiKey_LeftArrow, ImGuiKey_RightArrow,
        ImGuiKey_UpArrow, ImGuiKey_DownArrow, ImGuiKey_Home, ImGuiKey_End, ImGuiKey_PageUp, ImGuiKey_PageDown,
        ImGuiKey_F1, ImGuiKey_F2, ImGuiKey_F3, ImGuiKey_F4, ImGuiKey_F5, ImGuiKey_F6,
        ImGuiKey_F7, ImGuiKey_F8, ImGuiKey_F9, ImGuiKey_F10, ImGuiKey_F11, ImGuiKey_F12,
        ImGuiKey_LeftShift, ImGuiKey_RightShift, ImGuiKey_LeftCtrl, ImGuiKey_RightCtrl,
        ImGuiKey_LeftAlt, ImGuiKey_RightAlt, ImGuiKey_LeftSuper, ImGuiKey_RightSuper,
        ImGuiKey_Apostrophe, ImGuiKey_Comma, ImGuiKey_Minus, ImGuiKey_Period, ImGuiKey_Slash,
        ImGuiKey_Semicolon, ImGuiKey_Equal, ImGuiKey_LeftBracket, ImGuiKey_Backslash,
        ImGuiKey_RightBracket, ImGuiKey_GraveAccent, ImGuiKey_CapsLock, ImGuiKey_ScrollLock,
        ImGuiKey_NumLock, ImGuiKey_PrintScreen, ImGuiKey_Pause, ImGuiKey_Menu,
        ImGuiKey_Keypad0, ImGuiKey_Keypad1, ImGuiKey_Keypad2, ImGuiKey_Keypad3, ImGuiKey_Keypad4,
        ImGuiKey_Keypad5, ImGuiKey_Keypad6, ImGuiKey_Keypad7, ImGuiKey_Keypad8, ImGuiKey_Keypad9,
        ImGuiKey_KeypadDecimal, ImGuiKey_KeypadDivide, ImGuiKey_KeypadMultiply, ImGuiKey_KeypadSubtract,
        ImGuiKey_KeypadAdd, ImGuiKey_KeypadEnter, ImGuiKey_KeypadEqual
    };
    static_assert(sizeof(mapping)/sizeof(*mapping) == KeyCount);
    const auto index = static_cast<std::size_t>(key);
    return index < KeyCount ? mapping[index] : ImGuiKey_None;
}
namespace {
std::size_t Index(RenderKey key) { return static_cast<std::size_t>(key); }
bool Either(const std::bitset<KeyCount>& keys, RenderKey left, RenderKey right) {
    return keys[Index(left)] || keys[Index(right)];
}
void ReconcileModifier(std::bitset<KeyCount>& keys, RenderKey left, RenderKey right, bool down) {
    if (!down) { keys.reset(Index(left)); keys.reset(Index(right)); }
    else if (!Either(keys, left, right)) keys.set(Index(left));
}
} // namespace

bool RemoteInputBackend::Init() {
    if (initialized_) return true;
    auto& io = ImGui::GetIO();
    if (io.BackendPlatformUserData) return false;
    queue_ = std::make_shared<RemoteInputQueue>();
    keys_.reset(); buttons_.reset();
    focused_ = true;
    positioned_ = reproject_ = false;
    width_ = height_ = 0;
    io.BackendPlatformName = "render_module_remote_input";
    io.BackendPlatformUserData = this;
    io.ConfigFlags &= ~ImGuiConfigFlags_ViewportsEnable;
    // Required to retain down/up transitions when several events arrive together.
    io.ConfigInputTrickleEventQueue = true;
    initialized_ = true;
    return true;
}
void RemoteInputBackend::Modifiers(ImGuiIO& io) {
    io.AddKeyEvent(ImGuiMod_Ctrl, Either(keys_, RenderKey::LeftCtrl, RenderKey::RightCtrl));
    io.AddKeyEvent(ImGuiMod_Shift, Either(keys_, RenderKey::LeftShift, RenderKey::RightShift));
    io.AddKeyEvent(ImGuiMod_Alt, Either(keys_, RenderKey::LeftAlt, RenderKey::RightAlt));
    io.AddKeyEvent(ImGuiMod_Super, Either(keys_, RenderKey::LeftSuper, RenderKey::RightSuper));
}
void RemoteInputBackend::Release(ImGuiIO& io, bool loseFocus) {
    keys_.reset();
    Modifiers(io);
    // Also release events already consumed by ImGui if our submission backlog
    // was explicitly canceled. Duplicate ups are filtered by ImGui itself.
    for (std::size_t i = 0; i < KeyCount; ++i) io.AddKeyEvent(MapRenderKey(static_cast<RenderKey>(i)), false);
    for (std::size_t i = 0; i < MouseButtonCount; ++i) io.AddMouseButtonEvent(static_cast<int>(i), false);
    buttons_.reset();
    if (loseFocus) {
        focused_ = false;
        positioned_ = false;
        io.AddFocusEvent(false);
    }
}
void RemoteInputBackend::Position(ImGuiIO& io, float nx, float ny) {
    nx_ = nx; ny_ = ny; positioned_ = true;
    io.AddMousePosEvent(nx * width_, ny * height_);
}
void RemoteInputBackend::Apply(ImGuiIO& io, const RemoteInputEvent& event) {
    std::visit([&](const auto& value) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, MouseMove>) { if (focused_) Position(io, value.nx, value.ny); }
        else if constexpr (std::is_same_v<T, MouseButton>) {
            if (!focused_ && value.down) return;
            const auto index = static_cast<std::size_t>(value.button);
            buttons_[index] = value.down;
            io.AddMouseButtonEvent(static_cast<int>(index), value.down);
        } else if constexpr (std::is_same_v<T, MouseWheel>) {
            if (focused_) io.AddMouseWheelEvent(value.horizontal, value.vertical);
        } else if constexpr (std::is_same_v<T, Key>) {
            if (!focused_ && value.down) return;
            keys_[Index(value.key)] = value.down;
            Modifiers(io); // Submit aggregate mods before the corresponding key.
            io.AddKeyEvent(MapRenderKey(value.key), value.down);
        } else if constexpr (std::is_same_v<T, TextUtf8>) {
            if (focused_) io.AddInputCharactersUTF8(value.bytes.data());
        } else if constexpr (std::is_same_v<T, Focus>) {
            if (!value.focused) Release(io, true);
            else { focused_ = true; io.AddFocusEvent(true); }
        } else if constexpr (std::is_same_v<T, MouseSource>) {
            const auto source = value.source == RemoteMouseSource::Mouse ? ImGuiMouseSource_Mouse :
                value.source == RemoteMouseSource::Touch ? ImGuiMouseSource_TouchScreen : ImGuiMouseSource_Pen;
            io.AddMouseSourceEvent(source);
        } else if constexpr (std::is_same_v<T, ReleaseAll>) Release(io, value.loseFocus);
        else if constexpr (std::is_same_v<T, InputStateSnapshot>) {
            if (!value.focused) { Release(io, true); return; }
            focused_ = true;
            io.AddFocusEvent(true);
            Position(io, value.nx, value.ny);
            auto desired = value.keys;
            ReconcileModifier(desired, RenderKey::LeftCtrl, RenderKey::RightCtrl, value.ctrl);
            ReconcileModifier(desired, RenderKey::LeftShift, RenderKey::RightShift, value.shift);
            ReconcileModifier(desired, RenderKey::LeftAlt, RenderKey::RightAlt, value.alt);
            ReconcileModifier(desired, RenderKey::LeftSuper, RenderKey::RightSuper, value.super);
            const auto previous = keys_;
            keys_ = desired;
            Modifiers(io);
            for (std::size_t i = 0; i < KeyCount; ++i)
                if (previous[i] != keys_[i]) io.AddKeyEvent(MapRenderKey(static_cast<RenderKey>(i)), keys_[i]);
            for (std::size_t i = 0; i < MouseButtonCount; ++i)
                if (buttons_[i] != value.mouseButtons[i]) io.AddMouseButtonEvent(static_cast<int>(i), value.mouseButtons[i]);
            buttons_ = value.mouseButtons;
        }
    }, event);
}
void RemoteInputBackend::BeginFrame(ImGuiIO& io, int width, int height, double deltaTime) {
    if (!initialized_) return;
    reproject_ |= width_ != width || height_ != height;
    width_ = width; height_ = height;
    io.DisplaySize = {float(width), float(height)};
    io.DisplayFramebufferScale = {1, 1};
    io.DeltaTime = std::isfinite(deltaTime) && deltaTime > 0 ?
        static_cast<float>(std::clamp(deltaTime, 1.0e-6, 1.0)) : 1.0f/60.0f;
    io.ConfigInputTrickleEventQueue = true;
    if (queue_->TakeReleaseAll()) {
        io.ClearEventsQueue(); // Explicit cancellation, never an overflow shortcut.
        Release(io, true);
        return; // Focus loss must be observed before any subsequent gain.
    }
    auto& imguiQueue = ImGui::GetCurrentContext()->InputEventsQueue;
    const bool hadBacklog = !imguiQueue.empty();
    if (reproject_) {
        if (positioned_) {
            // Position-bearing events only leave our queue when ImGui is empty,
            // so their position is always consumed in that NewFrame. Remaining
            // trickled events cannot contain future positions. Reproject the
            // current normalized anchor BEFORE those transitions on resize.
            const int count = imguiQueue.Size;
            Position(io, nx_, ny_);
            if (imguiQueue.Size > count)
                std::rotate(imguiQueue.begin(), imguiQueue.end()-1, imguiQueue.end());
        }
        reproject_ = false;
    }
    if (hadBacklog) return;
    // A resize-only position prefix is not a transition barrier: a fresh move
    // may supersede it in this frame, before any buttons/keys are submitted.
    const int resizePrefix = imguiQueue.Size;
    // Bounded handoff as well as bounded producer storage: at most 64*256
    // codepoints in ImGui. Positions stay normalized behind transition barriers.
    QueuedInput pending;
    for (int n = 0; n < 64 && queue_->TryPop(pending, imguiQueue.Size == resizePrefix); ++n) {
        Apply(io, pending.event);
        if (std::holds_alternative<ReleaseAll>(pending.event) ||
            (std::holds_alternative<Focus>(pending.event) && !std::get<Focus>(pending.event).focused) ||
            (std::holds_alternative<InputStateSnapshot>(pending.event) && !std::get<InputStateSnapshot>(pending.event).focused))
            break;
    }
}
void RemoteInputBackend::Shutdown() {
    if (queue_) queue_->Close();
    if (!initialized_) return;
    auto& io = ImGui::GetIO();
    io.ClearEventsQueue();
    Release(io, true);
    io.BackendPlatformName = nullptr;
    io.BackendPlatformUserData = nullptr;
    initialized_ = false;
}
} // namespace render_module::detail
