#include "web_protocol.hpp"
#include <boost/json.hpp>
#include <algorithm>
#include <cmath>
#include <charconv>
#include <stdexcept>

namespace render_module::detail {
namespace {
using Object = boost::json::object;
void Require(bool yes) { if (!yes) throw std::invalid_argument("invalid message"); }
std::uint64_t Integer(const boost::json::value& v) {
    if (v.is_uint64()) return v.as_uint64();
    Require(v.is_int64() && v.as_int64() >= 0);
    return static_cast<std::uint64_t>(v.as_int64());
}
double Number(const boost::json::value& v, double lo, double hi) {
    Require(v.is_number());
    const double n = v.to_number<double>();
    Require(std::isfinite(n) && n >= lo && n <= hi);
    return n;
}
std::string_view String(const boost::json::value& v) {
    const auto& s = v.as_string(); return {s.data(), s.size()};
}
RenderKey KeyName(std::string_view name) {
#define KEY(n) {#n, RenderKey::n}
    static constexpr struct { std::string_view name; RenderKey key; } names[] = {
        KEY(A),KEY(B),KEY(C),KEY(D),KEY(E),KEY(F),KEY(G),KEY(H),KEY(I),KEY(J),KEY(K),KEY(L),KEY(M),
        KEY(N),KEY(O),KEY(P),KEY(Q),KEY(R),KEY(S),KEY(T),KEY(U),KEY(V),KEY(W),KEY(X),KEY(Y),KEY(Z),
        KEY(Digit0),KEY(Digit1),KEY(Digit2),KEY(Digit3),KEY(Digit4),KEY(Digit5),KEY(Digit6),KEY(Digit7),KEY(Digit8),KEY(Digit9),
        KEY(Escape),KEY(Enter),KEY(Tab),KEY(Backspace),KEY(Delete),KEY(Insert),KEY(Space),
        KEY(Left),KEY(Right),KEY(Up),KEY(Down),KEY(Home),KEY(End),KEY(PageUp),KEY(PageDown),
        KEY(F1),KEY(F2),KEY(F3),KEY(F4),KEY(F5),KEY(F6),KEY(F7),KEY(F8),KEY(F9),KEY(F10),KEY(F11),KEY(F12),
        KEY(LeftShift),KEY(RightShift),KEY(LeftCtrl),KEY(RightCtrl),KEY(LeftAlt),KEY(RightAlt),KEY(LeftSuper),KEY(RightSuper),
        KEY(Apostrophe),KEY(Comma),KEY(Minus),KEY(Period),KEY(Slash),KEY(Semicolon),KEY(Equal),KEY(LeftBracket),
        KEY(Backslash),KEY(RightBracket),KEY(GraveAccent),KEY(CapsLock),KEY(ScrollLock),KEY(NumLock),KEY(PrintScreen),KEY(Pause),KEY(Menu),
        KEY(Keypad0),KEY(Keypad1),KEY(Keypad2),KEY(Keypad3),KEY(Keypad4),KEY(Keypad5),KEY(Keypad6),KEY(Keypad7),KEY(Keypad8),KEY(Keypad9),
        KEY(KeypadDecimal),KEY(KeypadDivide),KEY(KeypadMultiply),KEY(KeypadSubtract),KEY(KeypadAdd),KEY(KeypadEnter),KEY(KeypadEqual)
    };
#undef KEY
    static_assert(sizeof(names)/sizeof(*names) == KeyCount);
    for (auto item : names) if (item.name == name) return item.key;
    throw std::invalid_argument("invalid key");
}
RemoteMouseButton ButtonName(std::string_view name) {
    static constexpr std::string_view names[] = {"left", "right", "middle", "extra1", "extra2"};
    for (std::size_t i = 0; i < MouseButtonCount; ++i)
        if (names[i] == name) return static_cast<RemoteMouseButton>(i);
    throw std::invalid_argument("invalid button");
}
void Modifiers(const Object& o, InputStateSnapshot& state, std::optional<RenderKey> released = {}) {
    state.ctrl = o.at("ctrl").as_bool(); state.shift = o.at("shift").as_bool();
    state.alt = o.at("alt").as_bool(); state.super = o.at("super").as_bool();
    const auto pair = [&](RenderKey l, RenderKey r, bool down) {
        const auto left = std::size_t(l), right = std::size_t(r);
        if (!down) { state.keys.reset(left); state.keys.reset(right); }
        else if (!state.keys[left] && !state.keys[right]) state.keys.set(released == l ? right : left);
    };
    pair(RenderKey::LeftCtrl, RenderKey::RightCtrl, state.ctrl);
    pair(RenderKey::LeftShift, RenderKey::RightShift, state.shift);
    pair(RenderKey::LeftAlt, RenderKey::RightAlt, state.alt);
    pair(RenderKey::LeftSuper, RenderKey::RightSuper, state.super);
}
void Position(const Object& o, InputStateSnapshot& state) {
    state.nx = float(Number(o.at("x"), 0, 1)); state.ny = float(Number(o.at("y"), 0, 1));
}
} // namespace
WebSize ClampWebViewport(int w, int h, const WebConfig& config) {
    if (w < 1 || h < 1 || w > 16384 || h > 16384) return {};
    const double scale = std::min({1.0, double(config.maxWidth)/w, double(config.maxHeight)/h});
    return {std::max(1, int(std::floor(w*scale))), std::max(1, int(std::floor(h*scale)))};
}
bool ParseWebMessage(std::string_view text, const WebConfig& config,
                     const WebInputState& previous, WebMessage& result) {
    try {
        Require(text.size() <= WebMessageLimit);
        boost::json::parse_options options; options.max_depth = 8;
        auto value = boost::json::parse(text, {}, options);
        const auto& o = value.as_object();
        Require(Integer(o.at("v")) == WebProtocolVersion);
        WebMessage next; next.state = previous;
        next.sequence = Integer(o.at("seq"));
        Require(next.sequence > 0 && next.sequence <= 9007199254740991ULL);
        const auto type = String(o.at("type"));
        auto& s = next.state.input;
        if (type == "frame_ack") {
            next.kind = WebMessage::Kind::FrameAck;
            const auto& frame = o.at("frameId");
            if (frame.is_string()) {
                const auto s = String(frame);
                Require(!s.empty() && s.size() <= 20);
                const auto parsed = std::from_chars(s.data(), s.data()+s.size(), next.frameId);
                Require(parsed.ec == std::errc{} && parsed.ptr == s.data()+s.size());
            } else next.frameId = Integer(frame);
            Require(next.frameId != 0);
        } else if (type == "viewport") {
            next.kind = WebMessage::Kind::Viewport;
            const auto w = Integer(o.at("width")), h = Integer(o.at("height"));
            Require(w > 0 && h > 0 && w <= 16384 && h <= 16384);
            Number(o.at("devicePixelRatio"), .25, 8); // Validated, NOT multiplied into CSS size.
            next.viewport = ClampWebViewport(int(w), int(h), config);
        } else if (type == "mouse_move") {
            Position(o, s);
            const auto source = String(o.at("source"));
            Require(source == "mouse" || source == "touch" || source == "pen");
            next.state.source = source == "mouse" ? RemoteMouseSource::Mouse :
                source == "touch" ? RemoteMouseSource::Touch : RemoteMouseSource::Pen;
            if (next.state.source != previous.source) next.events.emplace_back(MouseSource{*next.state.source});
            next.events.emplace_back(MouseMove{s.nx, s.ny});
        } else if (type == "mouse_button") {
            const auto button = ButtonName(String(o.at("button")));
            const bool down = o.at("down").as_bool();
            s.mouseButtons[std::size_t(button)] = down;
            // A preceding high-rate move may have been rejected under pressure.
            // Reassert its position reliably before delivering the button edge.
            next.events.emplace_back(MouseMove{s.nx, s.ny});
            next.events.emplace_back(MouseButton{button, down});
        } else if (type == "wheel") {
            next.events.emplace_back(MouseMove{s.nx, s.ny});
            next.events.emplace_back(MouseWheel{float(Number(o.at("horizontal"), -1000, 1000)),
                                                float(Number(o.at("vertical"), -1000, 1000))});
        } else if (type == "key") {
            const auto key = KeyName(String(o.at("key")));
            const bool down = o.at("down").as_bool();
            o.at("repeat").as_bool(); // ImGui generates repeat; repeated downs are idempotent.
            s.keys[std::size_t(key)] = down;
            Modifiers(o.at("mods").as_object(), s, down ? std::optional<RenderKey>{} : key);
            // Reconcile modifier edges first, then the ordinary physical key.
            for (std::size_t i = std::size_t(RenderKey::LeftShift); i <= std::size_t(RenderKey::RightSuper); ++i)
                if (s.keys[i] != previous.input.keys[i]) next.events.emplace_back(Key{RenderKey(i), s.keys[i]});
            if (key < RenderKey::LeftShift || key > RenderKey::RightSuper)
                next.events.emplace_back(Key{key, down});
        } else if (type == "text") {
            RemoteInputEvent event = TextUtf8{String(o.at("text"))};
            Require(ValidateInputEvent(event));
            // Committed text (notably Ctrl/Cmd+V paste) must not be suppressed by
            // ImGui's shortcut modifiers. Ordered edges restore physical state;
            // ImGui trickles the text/shortcut boundaries across frames.
            const RenderKey shortcuts[] = {RenderKey::LeftCtrl, RenderKey::RightCtrl, RenderKey::LeftSuper, RenderKey::RightSuper};
            for (auto key : shortcuts) if (s.keys[std::size_t(key)]) next.events.emplace_back(Key{key, false});
            next.events.push_back(std::move(event));
            for (auto key : shortcuts) if (s.keys[std::size_t(key)]) next.events.emplace_back(Key{key, true});
        } else if (type == "focus") {
            s.focused = o.at("focused").as_bool();
            if (!s.focused) next.kind = WebMessage::Kind::Release;
            else next.events.emplace_back(Focus{true});
        } else if (type == "release_all") next.kind = WebMessage::Kind::Release;
        else if (type == "snapshot") {
            s = {}; Position(o, s);
            s.focused = o.at("focused").as_bool();
            const auto& keys = o.at("keys").as_array();
            const auto& buttons = o.at("buttons").as_array();
            Require(keys.size() <= KeyCount && buttons.size() <= MouseButtonCount);
            for (const auto& k : keys) {
                const auto index = std::size_t(KeyName(String(k))); Require(!s.keys[index]); s.keys.set(index);
            }
            for (const auto& b : buttons) {
                const auto index = std::size_t(ButtonName(String(b))); Require(!s.mouseButtons[index]); s.mouseButtons.set(index);
            }
            Modifiers(o.at("mods").as_object(), s);
            next.events.emplace_back(s);
        } else Require(false);
        if (next.kind == WebMessage::Kind::Release) { s.keys.reset(); s.mouseButtons.reset(); s.focused = false; }
        result = std::move(next); return true;
    } catch (const std::exception&) { return false; }
}
std::vector<unsigned char> PackJpeg(std::uint64_t id, int w, int h, const unsigned char* data, std::size_t size) {
    if (!id || w <= 0 || h <= 0 || !data || !size || size > WebFrameLimit) return {};
    std::vector<unsigned char> out; out.reserve(28+size);
    const auto put = [&](std::uint64_t value, unsigned bytes) {
        for (unsigned i = bytes; i; --i) out.push_back(static_cast<unsigned char>(value >> ((i-1)*8)));
    };
    put(0x524d4a50, 4); put(WebProtocolVersion, 2); put(1, 2); put(id, 8);
    put(unsigned(w), 4); put(unsigned(h), 4); put(size, 4);
    out.insert(out.end(), data, data+size); return out;
}
} // namespace render_module::detail
