#ifndef RENDER_MODULE_VIEW3D_HPP_
#define RENDER_MODULE_VIEW3D_HPP_

#include <cstdint>
#include <string>
#include <vector>

#include "render_module/canvas.hpp"

namespace render_module {

// Public 3D API intentionally uses small RenderModule-owned math types so users
// don't have to expose Magnum types in application code.
struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

inline Vec3 operator+(Vec3 lhs, Vec3 rhs) { return {lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z}; }
inline Vec3 operator-(Vec3 lhs, Vec3 rhs) { return {lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z}; }
inline Vec3 operator-(Vec3 value) { return {-value.x, -value.y, -value.z}; }
inline Vec3 operator*(Vec3 value, float scale) { return {value.x * scale, value.y * scale, value.z * scale}; }
inline Vec3 operator*(float scale, Vec3 value) { return value * scale; }
inline Vec3 operator/(Vec3 value, float scale) { return {value.x / scale, value.y / scale, value.z / scale}; }

struct Color3D {
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
    float a = 1.0f;
};

// Unit quaternion, scalar-first. Non-unit values passed to drawing/camera APIs
// are normalized internally.
struct Quaternion {
    float w = 1.0f;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;

    static Quaternion FromAxisAngle(Vec3 axis, float radians) noexcept;
    static Quaternion FromEulerXYZ(float rollRadians,
                                   float pitchRadians,
                                   float yawRadians) noexcept;
};

// Right-handed world pose. RenderModule's default engineering convention is
// +X/+Y in the ground plane and +Z up.
struct Pose3D {
    Vec3 position{};
    Quaternion orientation{};
};

struct Ray3D {
    Vec3 origin{};
    Vec3 direction{0.0f, 0.0f, -1.0f};
};

struct View3DOptions {
    bool enableOrbit = true;
    bool enablePan = true;
    bool enableZoom = true;
    bool showStatus = true;

    MouseButton orbitButton = MouseButton::Right;
    MouseButton panButton = MouseButton::Middle;

    float orbitSensitivity = 0.008f; // radians per logical pixel
    float panSensitivity = 1.0f;
    float zoomStep = 1.20f;
    float minDistance = 0.02f;
    float maxDistance = 10000.0f;

    Color3D background{0.075f, 0.085f, 0.105f, 1.0f};
};

namespace detail {
struct View3DStorage;
struct View3DAccess;
}

class Camera3D {
public:
    // Pose is T_world_camera. Camera local -Z is forward and local +Y is up.
    Pose3D Pose() const noexcept;
    void SetPose(const Pose3D& pose) noexcept;

    Vec3 Position() const noexcept;
    Vec3 Target() const noexcept;
    Vec3 Up() const noexcept;

    void LookAt(Vec3 eye, Vec3 target, Vec3 up = {0.0f, 0.0f, 1.0f}) noexcept;
    void SetTarget(Vec3 target) noexcept;

    float Distance() const noexcept;
    void SetDistance(float distance) noexcept;

    float FieldOfViewDegrees() const noexcept;
    void SetFieldOfViewDegrees(float degrees) noexcept;
    void SetClipPlanes(float nearPlane, float farPlane) noexcept;

    void Reset() noexcept;

private:
    explicit Camera3D(detail::View3DStorage* storage) noexcept : storage_(storage) {}
    detail::View3DStorage* storage_ = nullptr;

    friend class View3D;
};

class View3D {
public:
    struct Data;

    Vec2 Size() const noexcept;
    const CanvasInput& Input() const noexcept;
    Camera3D Camera() noexcept;

    // Suppress camera navigation while an application interaction owns a drag.
    // Capture is released automatically when all mouse buttons are up.
    void CapturePointer() noexcept;
    bool HasPointerCapture() const noexcept;

    // Ray through the current mouse position. Mouse coordinates are the same
    // lower-left, +Y-up coordinates used by CanvasInput.
    Ray3D MouseRay() const noexcept;
    bool IntersectGroundPlane(Vec3& hit, float z = 0.0f) const noexcept;

    // Dynamic point cloud. Calling with the same id updates/reuses the same GPU mesh/buffer object.
    void PointCloud(const std::string& id,
                    const std::vector<Vec3>& points,
                    Color3D color = {0.90f, 0.92f, 0.96f, 1.0f},
                    float pointSize = 2.0f);

    // Dynamic triangle mesh. Indices are groups of three uint32 vertex indices.
    // Flat face normals are generated automatically for simple shaded geometry.
    void TriangleMesh(const std::string& id,
                      const std::vector<Vec3>& vertices,
                      const std::vector<std::uint32_t>& indices,
                      Color3D color = {0.72f, 0.75f, 0.82f, 1.0f});

    // Simple procedural primitives supplied by Magnum::Primitives.
    void Box(const std::string& id,
             const Pose3D& pose,
             Vec3 size,
             Color3D color = {0.25f, 0.65f, 0.95f, 1.0f});
    void Sphere(const std::string& id,
                Vec3 center,
                float radius,
                Color3D color = {0.95f, 0.55f, 0.20f, 1.0f});
    void Cylinder(const std::string& id,
                  const Pose3D& pose,
                  float radius,
                  float height,
                  Color3D color = {0.35f, 0.80f, 0.45f, 1.0f});

    // Engineering visualization helpers.
    void Line(Vec3 a,
              Vec3 b,
              Color3D color = {1.0f, 1.0f, 1.0f, 1.0f},
              float width = 1.0f);
    void Polyline(const std::vector<Vec3>& points,
                  Color3D color = {1.0f, 1.0f, 1.0f, 1.0f},
                  float width = 1.0f);
    void Axes(const std::string& id, const Pose3D& pose, float length = 1.0f, float width = 2.0f);
    void Grid(float extent = 10.0f,
              float spacing = 1.0f,
              Color3D color = {0.38f, 0.41f, 0.47f, 1.0f},
              float width = 1.0f,
              float z = 0.0f);

private:
    explicit View3D(Data* data) noexcept : data_(data) {}
    Data* data_ = nullptr;

    friend struct detail::View3DAccess;
};

} // namespace render_module

#endif // RENDER_MODULE_VIEW3D_HPP_
