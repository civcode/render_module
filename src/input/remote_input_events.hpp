#pragma once

#include <array>
#include <bitset>
#include <cstdint>
#include <string_view>
#include <variant>

namespace render_module::detail {

// Private RenderModule vocabulary, not browser codes or ImGui enum values.
enum class RenderKey : std::uint16_t {
    A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
    Digit0, Digit1, Digit2, Digit3, Digit4, Digit5, Digit6, Digit7, Digit8, Digit9,
    Escape, Enter, Tab, Backspace, Delete, Insert, Space,
    Left, Right, Up, Down, Home, End, PageUp, PageDown,
    F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
    LeftShift, RightShift, LeftCtrl, RightCtrl, LeftAlt, RightAlt, LeftSuper, RightSuper,
    Apostrophe, Comma, Minus, Period, Slash, Semicolon, Equal, LeftBracket,
    Backslash, RightBracket, GraveAccent,
    CapsLock, ScrollLock, NumLock, PrintScreen, Pause, Menu,
    Keypad0, Keypad1, Keypad2, Keypad3, Keypad4, Keypad5, Keypad6, Keypad7, Keypad8,
    Keypad9, KeypadDecimal, KeypadDivide, KeypadMultiply, KeypadSubtract,
    KeypadAdd, KeypadEnter, KeypadEqual,
    Count
};
constexpr std::size_t KeyCount = static_cast<std::size_t>(RenderKey::Count);
enum class RemoteMouseButton : std::uint8_t { Left, Right, Middle, Extra1, Extra2, Count };
constexpr std::size_t MouseButtonCount = static_cast<std::size_t>(RemoteMouseButton::Count);
enum class RemoteMouseSource : std::uint8_t { Mouse, Touch, Pen };

struct MouseMove { float nx = 0, ny = 0; }; // Normalized top-left origin; clamped on enqueue.
struct MouseButton { RemoteMouseButton button; bool down; };
struct MouseWheel { float horizontal = 0, vertical = 0; }; // Logical wheel units.
struct Key { RenderKey key; bool down; }; // Physical key transitions, never text.
struct TextUtf8 {
    static constexpr std::size_t MaxBytes = 256;
    std::array<char, MaxBytes + 1> bytes{};
    std::size_t size = 0;
    explicit TextUtf8(std::string_view text = {}) : size(text.size()) {
        if (size <= MaxBytes) {
            for (std::size_t i = 0; i < size; ++i) bytes[i] = text[i];
        }
    }
};
struct Focus { bool focused; };
struct MouseSource { RemoteMouseSource source; };
struct InputStateSnapshot {
    float nx = 0, ny = 0;
    std::bitset<MouseButtonCount> mouseButtons;
    std::bitset<KeyCount> keys;
    // Aggregate flags are authoritative: false clears both physical sides;
    // true with neither side specified supplies the left side.
    bool ctrl = false, shift = false, alt = false, super = false;
    bool focused = true;
};
struct ReleaseAll { bool loseFocus = true; };
using RemoteInputEvent = std::variant<MouseMove, MouseButton, MouseWheel, Key,
    TextUtf8, Focus, MouseSource, InputStateSnapshot, ReleaseAll>;

// Validates enums, finite floats and strict UTF-8 (no NUL/overlong/surrogate
// encodings). Clamps pointer coordinates. No ImGui calls or heap allocations.
bool ValidateInputEvent(RemoteInputEvent& event);

} // namespace render_module::detail
