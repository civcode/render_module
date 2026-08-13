#ifndef RENDER_MODULE_POSE_TOOL_HPP_
#define RENDER_MODULE_POSE_TOOL_HPP_

#include <cstdint>

#include "render_module/canvas.hpp"

namespace render_module {

struct Color {
    std::uint8_t r = 255;
    std::uint8_t g = 255;
    std::uint8_t b = 255;
    std::uint8_t a = 255;
};

struct Pose2D {
    Vec2 position{};
    float yaw = 0.0f;
};

enum class PoseDragEvent {
    None,
    Started,
    Changed,
    Finished
};

struct PoseToolStyle {
    Color color{35, 180, 90, 255};
    Color centerColor{255, 255, 255, 255};
    float centerRadiusPixels = 5.0f;
    float arrowLengthPixels = 42.0f;
    float arrowHeadPixels = 10.0f;
    float strokeWidthPixels = 2.5f;
};

// RViz-style pose placement: press to set position, drag to set heading,
// and release to commit. The tool consumes the configured mouse button only.
class PoseDragTool {
public:
    explicit PoseDragTool(MouseButton button = MouseButton::Left) noexcept;

    PoseDragEvent Update(Viewport& viewport);
    void Draw(Viewport& viewport, const PoseToolStyle& style = {}) const;

    bool HasPose() const noexcept { return hasPose_; }
    bool IsDragging() const noexcept { return dragging_; }
    const Pose2D& Pose() const noexcept { return pose_; }
    void SetPose(const Pose2D& pose) noexcept;
    void Clear() noexcept;

private:
    MouseButton button_ = MouseButton::Left;
    Pose2D pose_{};
    Vec2 dragStartCanvas_{};
    float minimumHeadingDragPixels_ = 3.0f;
    bool hasPose_ = false;
    bool dragging_ = false;
};

} // namespace render_module

#endif // RENDER_MODULE_POSE_TOOL_HPP_
