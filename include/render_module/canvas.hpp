#ifndef RENDER_MODULE_CANVAS_HPP_
#define RENDER_MODULE_CANVAS_HPP_

#include <array>
#include <cstddef>
#include <functional>
#include <string>

struct NVGcontext;

namespace render_module {

struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;
};

inline Vec2 operator+(Vec2 lhs, Vec2 rhs) { return {lhs.x + rhs.x, lhs.y + rhs.y}; }
inline Vec2 operator-(Vec2 lhs, Vec2 rhs) { return {lhs.x - rhs.x, lhs.y - rhs.y}; }
inline Vec2 operator*(Vec2 value, float scale) { return {value.x * scale, value.y * scale}; }
inline Vec2 operator/(Vec2 value, float scale) { return {value.x / scale, value.y / scale}; }

enum class MouseButton : std::size_t {
    Left = 0,
    Right = 1,
    Middle = 2
};

struct MouseButtonState {
    bool down = false;
    bool pressed = false;
    bool released = false;
    bool dragging = false;
};

// Positions use canvas coordinates: (0, 0) is the lower-left corner and +Y is up.
struct CanvasInput {
    bool hovered = false;
    bool active = false;
    Vec2 position{};
    Vec2 delta{};
    float wheel = 0.0f;
    std::array<MouseButtonState, 3> buttons{};

    const MouseButtonState& Button(MouseButton button) const noexcept {
        return buttons[static_cast<std::size_t>(button)];
    }
};

struct ViewportOptions {
    bool enablePan = true;
    bool enableZoom = true;
    bool showStatus = true;
    MouseButton panButton = MouseButton::Middle;
    float minScale = 0.05f;
    float maxScale = 100.0f;
    float zoomStep = 1.25f;
};

struct ViewportInput : CanvasInput {
    Vec2 worldPosition{};
    Vec2 worldDelta{};
};

namespace detail {
struct CanvasStorage;
struct CanvasAccess;
} // namespace detail

class Viewport {
public:
    NVGcontext* Graphics() const noexcept;
    const ViewportInput& Input() const noexcept;

    Vec2 WorldToCanvas(Vec2 world) const noexcept;
    Vec2 CanvasToWorld(Vec2 canvas) const noexcept;
    float Scale() const noexcept;
    Vec2 Offset() const noexcept;

    void SetScale(float scale);
    void SetOffset(Vec2 offset) noexcept;
    bool SetScaleOnce(float scale);
    bool SetOffsetOnce(Vec2 offset) noexcept;
    void ResetView() noexcept;

    // Call while a content tool owns a drag. Navigation remains suppressed until
    // all mouse buttons are released.
    void CapturePointer() noexcept;
    bool HasPointerCapture() const noexcept;

private:
    struct Data;
    explicit Viewport(Data* data) noexcept;
    Data* data_ = nullptr;

    friend class Canvas;
};

class Canvas {
public:
    using ViewportCallback = std::function<void(Viewport&)>;

    NVGcontext* Graphics() const noexcept;
    Vec2 Size() const noexcept;
    const CanvasInput& Input() const noexcept;

    // Adds an ImGui-rendered status panel above the completed NanoVG canvas.
    // Position is relative to the canvas's top-left corner in screen pixels.
    void StatusText(std::string text, Vec2 position = {6.0f, 10.0f});

    // Draws into a persistent, zoomable world view occupying this canvas.
    // The id only needs to be unique within this canvas window.
    void DrawViewport(const std::string& id,
                      const ViewportCallback& callback,
                      const ViewportOptions& options = {});

private:
    explicit Canvas(detail::CanvasStorage* storage) noexcept;
    detail::CanvasStorage* storage_ = nullptr;

    friend struct detail::CanvasAccess;
};

} // namespace render_module

#endif // RENDER_MODULE_CANVAS_HPP_
