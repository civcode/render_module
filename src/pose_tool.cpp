#include "render_module/pose_tool.hpp"

#include <cmath>

#include <nanovg.h>

namespace render_module {
namespace {
NVGcolor ToNanoVG(Color color) {
    return nvgRGBA(color.r, color.g, color.b, color.a);
}
} // namespace

PoseDragTool::PoseDragTool(MouseButton button) noexcept : button_(button) {}

PoseDragEvent PoseDragTool::Update(Viewport& viewport) {
    const ViewportInput& input = viewport.Input();
    const MouseButtonState& button = input.Button(button_);

    if (button.pressed && input.hovered) {
        pose_.position = input.worldPosition;
        dragStartCanvas_ = input.position;
        hasPose_ = true;
        dragging_ = true;
        viewport.CapturePointer();
        return PoseDragEvent::Started;
    }

    if (!dragging_) return PoseDragEvent::None;

    const Vec2 canvasDrag = input.position - dragStartCanvas_;
    const float canvasDistance = std::hypot(canvasDrag.x, canvasDrag.y);
    PoseDragEvent event = PoseDragEvent::None;
    if (canvasDistance >= minimumHeadingDragPixels_) {
        const Vec2 heading = input.worldPosition - pose_.position;
        pose_.yaw = std::atan2(heading.y, heading.x);
        event = PoseDragEvent::Changed;
    }

    if (button.released || !button.down) {
        dragging_ = false;
        return PoseDragEvent::Finished;
    }
    viewport.CapturePointer();
    return event;
}

void PoseDragTool::Draw(Viewport& viewport, const PoseToolStyle& style) const {
    if (!hasPose_) return;

    NVGcontext* vg = viewport.Graphics();
    const float inverseScale = 1.0f / viewport.Scale();
    const float radius = style.centerRadiusPixels * inverseScale;
    const float arrowLength = style.arrowLengthPixels * inverseScale;
    const float arrowHead = style.arrowHeadPixels * inverseScale;
    const float strokeWidth = style.strokeWidthPixels * inverseScale;

    const Vec2 tip{
        pose_.position.x + std::cos(pose_.yaw) * arrowLength,
        pose_.position.y + std::sin(pose_.yaw) * arrowLength
    };
    constexpr float headAngle = 2.60f;
    const Vec2 headA{
        tip.x + std::cos(pose_.yaw + headAngle) * arrowHead,
        tip.y + std::sin(pose_.yaw + headAngle) * arrowHead
    };
    const Vec2 headB{
        tip.x + std::cos(pose_.yaw - headAngle) * arrowHead,
        tip.y + std::sin(pose_.yaw - headAngle) * arrowHead
    };

    nvgSave(vg);
    nvgLineCap(vg, NVG_ROUND);
    nvgLineJoin(vg, NVG_ROUND);
    nvgStrokeWidth(vg, strokeWidth);
    nvgStrokeColor(vg, ToNanoVG(style.color));
    nvgBeginPath(vg);
    nvgMoveTo(vg, pose_.position.x, pose_.position.y);
    nvgLineTo(vg, tip.x, tip.y);
    nvgMoveTo(vg, headA.x, headA.y);
    nvgLineTo(vg, tip.x, tip.y);
    nvgLineTo(vg, headB.x, headB.y);
    nvgStroke(vg);

    nvgBeginPath(vg);
    nvgCircle(vg, pose_.position.x, pose_.position.y, radius);
    nvgFillColor(vg, ToNanoVG(style.centerColor));
    nvgFill(vg);
    nvgStrokeColor(vg, ToNanoVG(style.color));
    nvgStrokeWidth(vg, strokeWidth);
    nvgStroke(vg);
    nvgRestore(vg);
}

void PoseDragTool::SetPose(const Pose2D& pose) noexcept {
    pose_ = pose;
    hasPose_ = true;
    dragging_ = false;
}

void PoseDragTool::Clear() noexcept {
    pose_ = {};
    hasPose_ = false;
    dragging_ = false;
}

} // namespace render_module
