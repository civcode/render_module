#include "render_module/view3d.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <unordered_map>
#include <vector>

#include <Corrade/Containers/ArrayView.h>
#include <Magnum/GL/Buffer.h>
#include <Magnum/GL/Context.h>
#include <Magnum/GL/Mesh.h>
#include <Magnum/GL/Renderer.h>
#include <Magnum/Math/Angle.h>
#include <Magnum/Math/Color.h>
#include <Magnum/Math/Matrix4.h>
#include <Magnum/Math/Quaternion.h>
#include <Magnum/Math/Vector3.h>
#include <Magnum/MeshTools/Compile.h>
#include <Magnum/Platform/GLContext.h>
#include <Magnum/Primitives/Cube.h>
#include <Magnum/Primitives/Cylinder.h>
#include <Magnum/Primitives/UVSphere.h>
#include <Magnum/Shaders/FlatGL.h>
#include <Magnum/Shaders/PhongGL.h>
#include <Magnum/Trade/MeshData.h>

#include "render_module/render_context.hpp"
#include "view3d_internal.hpp"

namespace render_module {
namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kEpsilon = 1.0e-6f;

float Dot(Vec3 a, Vec3 b) noexcept {
    return a.x*b.x + a.y*b.y + a.z*b.z;
}

Vec3 Cross(Vec3 a, Vec3 b) noexcept {
    return {
        a.y*b.z - a.z*b.y,
        a.z*b.x - a.x*b.z,
        a.x*b.y - a.y*b.x
    };
}

float Length(Vec3 v) noexcept {
    return std::sqrt(std::max(0.0f, Dot(v, v)));
}

Vec3 Normalize(Vec3 v, Vec3 fallback = {1.0f, 0.0f, 0.0f}) noexcept {
    const float length = Length(v);
    if (!std::isfinite(length) || length < kEpsilon) return fallback;
    return v / length;
}

Quaternion Normalize(Quaternion q) noexcept {
    const float norm = std::sqrt(std::max(0.0f,
        q.w*q.w + q.x*q.x + q.y*q.y + q.z*q.z));
    if (!std::isfinite(norm) || norm < kEpsilon) return {};
    return {q.w/norm, q.x/norm, q.y/norm, q.z/norm};
}

Quaternion Multiply(Quaternion a, Quaternion b) noexcept {
    return {
        a.w*b.w - a.x*b.x - a.y*b.y - a.z*b.z,
        a.w*b.x + a.x*b.w + a.y*b.z - a.z*b.y,
        a.w*b.y - a.x*b.z + a.y*b.w + a.z*b.x,
        a.w*b.z + a.x*b.y - a.y*b.x + a.z*b.w
    };
}

Vec3 Rotate(Quaternion input, Vec3 v) noexcept {
    const Quaternion q = Normalize(input);
    const Vec3 qv{q.x, q.y, q.z};
    const Vec3 t = 2.0f * Cross(qv, v);
    return v + q.w*t + Cross(qv, t);
}

Quaternion QuaternionFromBasis(Vec3 xAxis, Vec3 yAxis, Vec3 zAxis) noexcept {
    // Rotation matrix columns are the world-space directions of local X/Y/Z.
    const float m00 = xAxis.x, m01 = yAxis.x, m02 = zAxis.x;
    const float m10 = xAxis.y, m11 = yAxis.y, m12 = zAxis.y;
    const float m20 = xAxis.z, m21 = yAxis.z, m22 = zAxis.z;
    Quaternion q;
    const float trace = m00 + m11 + m22;
    if (trace > 0.0f) {
        const float s = std::sqrt(trace + 1.0f) * 2.0f;
        q.w = 0.25f * s;
        q.x = (m21 - m12) / s;
        q.y = (m02 - m20) / s;
        q.z = (m10 - m01) / s;
    } else if (m00 > m11 && m00 > m22) {
        const float s = std::sqrt(1.0f + m00 - m11 - m22) * 2.0f;
        q.w = (m21 - m12) / s;
        q.x = 0.25f * s;
        q.y = (m01 + m10) / s;
        q.z = (m02 + m20) / s;
    } else if (m11 > m22) {
        const float s = std::sqrt(1.0f + m11 - m00 - m22) * 2.0f;
        q.w = (m02 - m20) / s;
        q.x = (m01 + m10) / s;
        q.y = 0.25f * s;
        q.z = (m12 + m21) / s;
    } else {
        const float s = std::sqrt(1.0f + m22 - m00 - m11) * 2.0f;
        q.w = (m10 - m01) / s;
        q.x = (m02 + m20) / s;
        q.y = (m12 + m21) / s;
        q.z = 0.25f * s;
    }
    return Normalize(q);
}

Quaternion LookOrientation(Vec3 eye, Vec3 target, Vec3 upHint) noexcept {
    const Vec3 forward = Normalize(target - eye, {0.0f, 1.0f, 0.0f});
    Vec3 right = Cross(forward, Normalize(upHint, {0.0f, 0.0f, 1.0f}));
    if (Length(right) < kEpsilon) {
        right = Cross(forward, std::fabs(forward.z) < 0.9f
            ? Vec3{0.0f, 0.0f, 1.0f}
            : Vec3{0.0f, 1.0f, 0.0f});
    }
    right = Normalize(right, {1.0f, 0.0f, 0.0f});
    const Vec3 cameraUp = Normalize(Cross(right, forward), {0.0f, 0.0f, 1.0f});
    // Camera local forward is -Z, so local +Z points backward.
    return QuaternionFromBasis(right, cameraUp, -forward);
}

Magnum::Vector3 ToMagnum(Vec3 v) noexcept {
    return {v.x, v.y, v.z};
}

Magnum::Color4 ToMagnum(Color3D c) noexcept {
    return {
        std::clamp(c.r, 0.0f, 1.0f),
        std::clamp(c.g, 0.0f, 1.0f),
        std::clamp(c.b, 0.0f, 1.0f),
        std::clamp(c.a, 0.0f, 1.0f)
    };
}

Magnum::Matrix4 PoseMatrix(const Pose3D& pose) noexcept {
    const Quaternion q = Normalize(pose.orientation);
    const Magnum::Quaternion rotation{{q.x, q.y, q.z}, q.w};
    return Magnum::Matrix4::from(rotation.toMatrix(), ToMagnum(pose.position));
}

std::vector<Magnum::Vector3> ToMagnumPoints(const std::vector<Vec3>& points) {
    std::vector<Magnum::Vector3> result;
    result.reserve(points.size());
    for (Vec3 p : points) result.emplace_back(p.x, p.y, p.z);
    return result;
}

} // namespace

Quaternion Quaternion::FromAxisAngle(Vec3 axis, float radians) noexcept {
    axis = Normalize(axis, {1.0f, 0.0f, 0.0f});
    const float half = 0.5f * radians;
    const float s = std::sin(half);
    return Normalize({std::cos(half), axis.x*s, axis.y*s, axis.z*s});
}

Quaternion Quaternion::FromEulerXYZ(float rollRadians,
                                    float pitchRadians,
                                    float yawRadians) noexcept {
    const Quaternion qx = FromAxisAngle({1.0f, 0.0f, 0.0f}, rollRadians);
    const Quaternion qy = FromAxisAngle({0.0f, 1.0f, 0.0f}, pitchRadians);
    const Quaternion qz = FromAxisAngle({0.0f, 0.0f, 1.0f}, yawRadians);
    return Normalize(Multiply(Multiply(qz, qy), qx));
}

namespace detail {

struct DynamicMesh {
    Magnum::GL::Buffer vertexBuffer;
    Magnum::GL::Mesh mesh;

    explicit DynamicMesh(Magnum::MeshPrimitive primitive) {
        mesh.setPrimitive(primitive)
            .addVertexBuffer(vertexBuffer, 0, Magnum::Shaders::FlatGL3D::Position{});
    }

    void Update(const std::vector<Magnum::Vector3>& vertices) {
        vertexBuffer.setData(
            Corrade::Containers::ArrayView<const Magnum::Vector3>{vertices.data(), vertices.size()},
            Magnum::GL::BufferUsage::DynamicDraw);
        mesh.setCount(static_cast<Magnum::Int>(vertices.size()));
    }
};

struct LitVertex {
    Magnum::Vector3 position;
    Magnum::Vector3 normal;
};

struct DynamicLitMesh {
    Magnum::GL::Buffer vertexBuffer;
    Magnum::GL::Mesh mesh;

    DynamicLitMesh() {
        mesh.setPrimitive(Magnum::MeshPrimitive::Triangles)
            .addVertexBuffer(vertexBuffer, 0,
                             Magnum::Shaders::PhongGL::Position{},
                             Magnum::Shaders::PhongGL::Normal{});
    }

    void Update(const std::vector<LitVertex>& vertices) {
        vertexBuffer.setData(
            Corrade::Containers::ArrayView<const LitVertex>{vertices.data(), vertices.size()},
            Magnum::GL::BufferUsage::DynamicDraw);
        mesh.setCount(static_cast<Magnum::Int>(vertices.size()));
    }
};

struct View3DStorage {
    Vec3 cameraPosition{4.0f, -4.0f, 3.0f};
    Vec3 cameraTarget{0.0f, 0.0f, 0.0f};
    Vec3 cameraUp{0.0f, 0.0f, 1.0f};
    float fovDegrees = 45.0f;
    float nearPlane = 0.01f;
    float farPlane = 1000.0f;
    bool pointerCaptured = false;

    Vec2 size{};
    CanvasInput input{};
    View3DOptions options{};

    std::unordered_map<std::string, std::unique_ptr<DynamicMesh>> pointClouds;
    std::unordered_map<std::string, std::unique_ptr<DynamicLitMesh>> triangleMeshes;
};

struct Renderer3D {
    Magnum::Shaders::FlatGL3D flatShader;
    Magnum::Shaders::PhongGL phongShader;
    Magnum::GL::Mesh cube;
    Magnum::GL::Mesh sphere;
    Magnum::GL::Mesh cylinder;
    Magnum::GL::Buffer lineBuffer;
    Magnum::GL::Mesh lineMesh;

    Renderer3D()
        : cube{Magnum::MeshTools::compile(Magnum::Primitives::cubeSolid())},
          sphere{Magnum::MeshTools::compile(Magnum::Primitives::uvSphereSolid(16, 32))},
          cylinder{Magnum::MeshTools::compile(Magnum::Primitives::cylinderSolid(
              1, 32, 0.5f, Magnum::Primitives::CylinderFlag::CapEnds))} {
        lineMesh.setPrimitive(Magnum::MeshPrimitive::Lines)
            .addVertexBuffer(lineBuffer, 0, Magnum::Shaders::FlatGL3D::Position{});
    }

    void DrawFlat(Magnum::GL::Mesh& mesh,
                  const Magnum::Matrix4& projection,
                  const Magnum::Matrix4& view,
                  const Magnum::Matrix4& model,
                  Color3D color) {
        flatShader.setColor(ToMagnum(color))
            .setTransformationProjectionMatrix(projection*view*model)
            .draw(mesh);
    }

    void DrawLit(Magnum::GL::Mesh& mesh,
                 const Magnum::Matrix4& projection,
                 const Magnum::Matrix4& view,
                 const Magnum::Matrix4& model,
                 Color3D color) {
        const Magnum::Matrix4 transformation = view*model;
        const Magnum::Color4 diffuse = ToMagnum(color);
        phongShader
            .setAmbientColor(ToMagnum({0.20f*color.r, 0.20f*color.g,
                                       0.20f*color.b, color.a}))
            .setDiffuseColor(diffuse)
            .setSpecularColor({0.16f, 0.16f, 0.16f, 0.0f})
            .setShininess(48.0f)
            .setTransformationMatrix(transformation)
            .setNormalMatrix(transformation.normalMatrix())
            .setProjectionMatrix(projection)
            .draw(mesh);
    }

    void DrawLines(const std::vector<Vec3>& vertices,
                   const Magnum::Matrix4& projection,
                   const Magnum::Matrix4& view,
                   Color3D color,
                   float width) {
        if (vertices.size() < 2) return;
        const std::vector<Magnum::Vector3> positions = ToMagnumPoints(vertices);
        lineBuffer.setData(
            Corrade::Containers::ArrayView<const Magnum::Vector3>{positions.data(), positions.size()},
            Magnum::GL::BufferUsage::DynamicDraw);
        lineMesh.setCount(static_cast<Magnum::Int>(positions.size()));
        Magnum::GL::Renderer::setLineWidth(std::max(1.0f, width));
        flatShader.setColor(ToMagnum(color))
            .setTransformationProjectionMatrix(projection*view)
            .draw(lineMesh);
    }
};

struct Backend3D {
    std::unique_ptr<Magnum::Platform::GLContext> context;
    std::unique_ptr<Renderer3D> renderer;
};

std::unique_ptr<Backend3D> backend;

bool Initialize3DBackend() {
    if (backend) return true;

    auto next = std::make_unique<Backend3D>();
    next->context = std::make_unique<Magnum::Platform::GLContext>(Magnum::NoCreate);
    if (!next->context->tryCreate()) {
        std::fprintf(stderr, "RenderModule: Magnum OpenGL context initialization failed.\n");
        return false;
    }

    next->renderer = std::make_unique<Renderer3D>();
    backend = std::move(next);

    // RenderModule uses raw OpenGL through GLAD, ImGui and NanoVG outside of
    // View3D. Tell Magnum its state cache is leaving Magnum-managed rendering.
    Magnum::GL::Context::current().resetState(Magnum::GL::Context::State::EnterExternal);
    return true;
}

void Shutdown3DBackend() noexcept {
    if (!backend) return;
    // We are currently in external/raw-GL mode. Re-enter Magnum before
    // destroying its GPU resources and context tracker.
    Magnum::GL::Context::current().resetState(Magnum::GL::Context::State::ExitExternal);
    backend.reset();
}

std::shared_ptr<View3DStorage> CreateView3DStorage() {
    return std::make_shared<View3DStorage>();
}

} // namespace detail

struct View3D::Data {
    detail::View3DStorage* storage = nullptr;
    detail::Renderer3D* renderer = nullptr;
    Magnum::Matrix4 projection;
    Magnum::Matrix4 view;
};

namespace detail {
struct View3DAccess {
    static View3D Make(View3D::Data* data) noexcept { return View3D(data); }
};
} // namespace detail

namespace {

float CameraDistance(const detail::View3DStorage& storage) noexcept {
    return std::max(Length(storage.cameraPosition - storage.cameraTarget), kEpsilon);
}

void ClampCameraDistance(detail::View3DStorage& storage) noexcept {
    const float minDistance = std::max(1.0e-4f, storage.options.minDistance);
    const float maxDistance = std::max(minDistance, storage.options.maxDistance);
    const Vec3 offset = storage.cameraPosition - storage.cameraTarget;
    const float distance = std::clamp(Length(offset), minDistance, maxDistance);
    storage.cameraPosition = storage.cameraTarget + Normalize(offset, {1.0f, -1.0f, 0.75f})*distance;
}

void UpdateCameraNavigation(detail::View3DStorage& storage) {
    bool anyButtonDown = false;
    for (const MouseButtonState& button : storage.input.buttons) {
        anyButtonDown = anyButtonDown || button.down;
    }
    if (!anyButtonDown) storage.pointerCaptured = false;

    const bool navigationEnabled = !RenderContext::Instance().disableViewportControls;
    if (!navigationEnabled || storage.pointerCaptured) return;

    const View3DOptions& options = storage.options;
    const CanvasInput& input = storage.input;

    if (options.enableZoom && input.hovered && input.wheel != 0.0f) {
        const float minDistance = std::max(1.0e-4f, options.minDistance);
        const float maxDistance = std::max(minDistance, options.maxDistance);
        const float step = std::max(options.zoomStep, 1.001f);
        const Vec3 direction = Normalize(storage.cameraPosition - storage.cameraTarget,
                                         {1.0f, -1.0f, 0.75f});
        float distance = CameraDistance(storage) / std::pow(step, input.wheel);
        distance = std::clamp(distance, minDistance, maxDistance);
        storage.cameraPosition = storage.cameraTarget + direction*distance;
    }

    const MouseButtonState& orbit = input.Button(options.orbitButton);
    if (options.enableOrbit && input.active && orbit.dragging && !orbit.pressed) {
        const Vec3 offset = storage.cameraPosition - storage.cameraTarget;
        const float radius = std::max(Length(offset), std::max(options.minDistance, 1.0e-4f));
        float yaw = std::atan2(offset.y, offset.x);
        float pitch = std::asin(std::clamp(offset.z/radius, -1.0f, 1.0f));
        yaw -= input.delta.x * options.orbitSensitivity;
        pitch -= input.delta.y * options.orbitSensitivity;
        pitch = std::clamp(pitch, -0.49f*kPi, 0.49f*kPi);
        const float cp = std::cos(pitch);
        storage.cameraPosition = storage.cameraTarget + Vec3{
            radius*cp*std::cos(yaw),
            radius*cp*std::sin(yaw),
            radius*std::sin(pitch)
        };
        storage.cameraUp = {0.0f, 0.0f, 1.0f};
    }

    const MouseButtonState& pan = input.Button(options.panButton);
    if (options.enablePan && input.active && pan.dragging && !pan.pressed) {
        const Vec3 forward = Normalize(storage.cameraTarget - storage.cameraPosition,
                                       {0.0f, 1.0f, 0.0f});
        Vec3 right = Normalize(Cross(forward, storage.cameraUp), {1.0f, 0.0f, 0.0f});
        const Vec3 up = Normalize(Cross(right, forward), {0.0f, 0.0f, 1.0f});
        const float logicalHeight = std::max(storage.size.y, 1.0f);
        const float worldPerPixel =
            2.0f*CameraDistance(storage)*std::tan(storage.fovDegrees*kPi/360.0f) /
            logicalHeight * std::max(options.panSensitivity, 0.0f);
        const Vec3 shift = right*(-input.delta.x*worldPerPixel) +
                           up*(-input.delta.y*worldPerPixel);
        storage.cameraPosition = storage.cameraPosition + shift;
        storage.cameraTarget = storage.cameraTarget + shift;
    }

    ClampCameraDistance(storage);
}

Magnum::Matrix4 ProjectionMatrix(const detail::View3DStorage& storage) {
    const float width = std::max(storage.size.x, 1.0f);
    const float height = std::max(storage.size.y, 1.0f);
    const float aspect = width/height;
    const float nearPlane = std::max(storage.nearPlane, 1.0e-5f);
    const float farPlane = std::max(storage.farPlane, nearPlane + 1.0e-3f);
    return Magnum::Matrix4::perspectiveProjection(
        Magnum::Deg{std::clamp(storage.fovDegrees, 1.0f, 179.0f)},
        aspect, nearPlane, farPlane);
}

Magnum::Matrix4 ViewMatrix(const detail::View3DStorage& storage) {
    return Magnum::Matrix4::lookAt(
        ToMagnum(storage.cameraPosition),
        ToMagnum(storage.cameraTarget),
        ToMagnum(Normalize(storage.cameraUp, {0.0f, 0.0f, 1.0f}))).invertedRigid();
}

} // namespace

Pose3D Camera3D::Pose() const noexcept {
    if (!storage_) return {};
    return {
        storage_->cameraPosition,
        LookOrientation(storage_->cameraPosition, storage_->cameraTarget, storage_->cameraUp)
    };
}

void Camera3D::SetPose(const Pose3D& pose) noexcept {
    if (!storage_) return;
    const float distance = CameraDistance(*storage_);
    const Quaternion q = Normalize(pose.orientation);
    const Vec3 forward = Normalize(Rotate(q, {0.0f, 0.0f, -1.0f}), {0.0f, 1.0f, 0.0f});
    const Vec3 up = Normalize(Rotate(q, {0.0f, 1.0f, 0.0f}), {0.0f, 0.0f, 1.0f});
    storage_->cameraPosition = pose.position;
    storage_->cameraTarget = pose.position + forward*distance;
    storage_->cameraUp = up;
    ClampCameraDistance(*storage_);
}

Vec3 Camera3D::Position() const noexcept {
    return storage_ ? storage_->cameraPosition : Vec3{};
}

Vec3 Camera3D::Target() const noexcept {
    return storage_ ? storage_->cameraTarget : Vec3{};
}

Vec3 Camera3D::Up() const noexcept {
    return storage_ ? storage_->cameraUp : Vec3{0.0f, 0.0f, 1.0f};
}

void Camera3D::LookAt(Vec3 eye, Vec3 target, Vec3 up) noexcept {
    if (!storage_) return;
    if (Length(target - eye) < kEpsilon) target = eye + Vec3{0.0f, 1.0f, 0.0f};
    storage_->cameraPosition = eye;
    storage_->cameraTarget = target;
    storage_->cameraUp = Normalize(up, {0.0f, 0.0f, 1.0f});
    ClampCameraDistance(*storage_);
}

void Camera3D::SetTarget(Vec3 target) noexcept {
    if (!storage_) return;
    storage_->cameraTarget = target;
    ClampCameraDistance(*storage_);
}

float Camera3D::Distance() const noexcept {
    return storage_ ? CameraDistance(*storage_) : 0.0f;
}

void Camera3D::SetDistance(float distance) noexcept {
    if (!storage_ || !std::isfinite(distance)) return;
    const float minDistance = std::max(1.0e-4f, storage_->options.minDistance);
    const float maxDistance = std::max(minDistance, storage_->options.maxDistance);
    distance = std::clamp(distance, minDistance, maxDistance);
    const Vec3 direction = Normalize(storage_->cameraPosition - storage_->cameraTarget,
                                     {1.0f, -1.0f, 0.75f});
    storage_->cameraPosition = storage_->cameraTarget + direction*distance;
}

float Camera3D::FieldOfViewDegrees() const noexcept {
    return storage_ ? storage_->fovDegrees : 45.0f;
}

void Camera3D::SetFieldOfViewDegrees(float degrees) noexcept {
    if (!storage_ || !std::isfinite(degrees)) return;
    storage_->fovDegrees = std::clamp(degrees, 1.0f, 179.0f);
}

void Camera3D::SetClipPlanes(float nearPlane, float farPlane) noexcept {
    if (!storage_ || !std::isfinite(nearPlane) || !std::isfinite(farPlane)) return;
    storage_->nearPlane = std::max(nearPlane, 1.0e-5f);
    storage_->farPlane = std::max(farPlane, storage_->nearPlane + 1.0e-3f);
}

void Camera3D::Reset() noexcept {
    if (!storage_) return;
    storage_->cameraPosition = {4.0f, -4.0f, 3.0f};
    storage_->cameraTarget = {0.0f, 0.0f, 0.0f};
    storage_->cameraUp = {0.0f, 0.0f, 1.0f};
    storage_->fovDegrees = 45.0f;
    storage_->nearPlane = 0.01f;
    storage_->farPlane = 1000.0f;
}

Vec2 View3D::Size() const noexcept {
    return data_ && data_->storage ? data_->storage->size : Vec2{};
}

const CanvasInput& View3D::Input() const noexcept {
    static const CanvasInput empty;
    return data_ && data_->storage ? data_->storage->input : empty;
}

Camera3D View3D::Camera() noexcept {
    return Camera3D(data_ ? data_->storage : nullptr);
}

void View3D::CapturePointer() noexcept {
    if (data_ && data_->storage) data_->storage->pointerCaptured = true;
}

bool View3D::HasPointerCapture() const noexcept {
    return data_ && data_->storage && data_->storage->pointerCaptured;
}

Ray3D View3D::MouseRay() const noexcept {
    if (!data_ || !data_->storage) return {};
    const detail::View3DStorage& storage = *data_->storage;
    const float width = std::max(storage.size.x, 1.0f);
    const float height = std::max(storage.size.y, 1.0f);
    const float nx = 2.0f*storage.input.position.x/width - 1.0f;
    const float ny = 2.0f*storage.input.position.y/height - 1.0f;
    const float aspect = width/height;
    const float tanHalf = std::tan(storage.fovDegrees*kPi/360.0f);

    const Vec3 forward = Normalize(storage.cameraTarget - storage.cameraPosition,
                                   {0.0f, 1.0f, 0.0f});
    Vec3 right = Normalize(Cross(forward, storage.cameraUp), {1.0f, 0.0f, 0.0f});
    const Vec3 up = Normalize(Cross(right, forward), {0.0f, 0.0f, 1.0f});
    const Vec3 direction = Normalize(forward + right*(nx*aspect*tanHalf) + up*(ny*tanHalf),
                                     forward);
    return {storage.cameraPosition, direction};
}

bool View3D::IntersectGroundPlane(Vec3& hit, float z) const noexcept {
    const Ray3D ray = MouseRay();
    if (std::fabs(ray.direction.z) < kEpsilon) return false;
    const float t = (z - ray.origin.z)/ray.direction.z;
    if (t < 0.0f || !std::isfinite(t)) return false;
    hit = ray.origin + ray.direction*t;
    return true;
}

void View3D::PointCloud(const std::string& id,
                        const std::vector<Vec3>& points,
                        Color3D color,
                        float pointSize) {
    if (!data_ || !data_->storage || !data_->renderer || id.empty() || points.empty()) return;
    auto& slot = data_->storage->pointClouds[id];
    if (!slot) slot = std::make_unique<detail::DynamicMesh>(Magnum::MeshPrimitive::Points);
    const std::vector<Magnum::Vector3> positions = ToMagnumPoints(points);
    slot->Update(positions);
    Magnum::GL::Renderer::setPointSize(std::max(1.0f, pointSize));
    data_->renderer->DrawFlat(slot->mesh, data_->projection, data_->view, Magnum::Matrix4{}, color);
}

void View3D::TriangleMesh(const std::string& id,
                          const std::vector<Vec3>& vertices,
                          const std::vector<std::uint32_t>& indices,
                          Color3D color) {
    if (!data_ || !data_->storage || !data_->renderer || id.empty() || vertices.empty()) return;

    std::vector<detail::LitVertex> expanded;
    auto appendTriangle = [&](std::uint32_t ia, std::uint32_t ib, std::uint32_t ic) {
        if (ia >= vertices.size() || ib >= vertices.size() || ic >= vertices.size()) return;
        const Vec3 a = vertices[ia];
        const Vec3 b = vertices[ib];
        const Vec3 c = vertices[ic];
        const Vec3 normal = Normalize(Cross(b - a, c - a), {0.0f, 0.0f, 1.0f});
        const Magnum::Vector3 n = ToMagnum(normal);
        expanded.push_back({ToMagnum(a), n});
        expanded.push_back({ToMagnum(b), n});
        expanded.push_back({ToMagnum(c), n});
    };

    if (indices.empty()) {
        expanded.reserve(vertices.size() - vertices.size()%3);
        for (std::size_t i = 0; i + 2 < vertices.size(); i += 3) {
            appendTriangle(static_cast<std::uint32_t>(i + 0),
                           static_cast<std::uint32_t>(i + 1),
                           static_cast<std::uint32_t>(i + 2));
        }
    } else {
        expanded.reserve(indices.size() - indices.size()%3);
        for (std::size_t i = 0; i + 2 < indices.size(); i += 3) {
            appendTriangle(indices[i + 0], indices[i + 1], indices[i + 2]);
        }
    }
    if (expanded.empty()) return;

    auto& slot = data_->storage->triangleMeshes[id];
    if (!slot) slot = std::make_unique<detail::DynamicLitMesh>();
    slot->Update(expanded);
    data_->renderer->DrawLit(slot->mesh, data_->projection, data_->view, Magnum::Matrix4{}, color);
}

void View3D::Box(const std::string& id, const Pose3D& pose, Vec3 size, Color3D color) {
    (void)id;
    if (!data_ || !data_->renderer) return;
    const Magnum::Matrix4 model = PoseMatrix(pose)*Magnum::Matrix4::scaling(
        {0.5f*std::fabs(size.x), 0.5f*std::fabs(size.y), 0.5f*std::fabs(size.z)});
    data_->renderer->DrawLit(data_->renderer->cube, data_->projection, data_->view, model, color);
}

void View3D::Sphere(const std::string& id, Vec3 center, float radius, Color3D color) {
    (void)id;
    if (!data_ || !data_->renderer || !std::isfinite(radius) || radius <= 0.0f) return;
    const Magnum::Matrix4 model = Magnum::Matrix4::translation(ToMagnum(center))*
                                  Magnum::Matrix4::scaling({radius, radius, radius});
    data_->renderer->DrawLit(data_->renderer->sphere, data_->projection, data_->view, model, color);
}

void View3D::Cylinder(const std::string& id,
                      const Pose3D& pose,
                      float radius,
                      float height,
                      Color3D color) {
    (void)id;
    if (!data_ || !data_->renderer || radius <= 0.0f || height <= 0.0f) return;
    // Magnum's cylinder is radius 1, length 1 along local Y. Public View3D
    // convention uses local +Z as the cylinder axis.
    const Magnum::Matrix4 model = PoseMatrix(pose)*
        Magnum::Matrix4::rotationX(Magnum::Deg{90.0f})*
        Magnum::Matrix4::scaling({radius, height, radius});
    data_->renderer->DrawLit(data_->renderer->cylinder, data_->projection, data_->view, model, color);
}

void View3D::Line(Vec3 a, Vec3 b, Color3D color, float width) {
    if (!data_ || !data_->renderer) return;
    data_->renderer->DrawLines({a, b}, data_->projection, data_->view, color, width);
}

void View3D::Polyline(const std::vector<Vec3>& points, Color3D color, float width) {
    if (!data_ || !data_->renderer || points.size() < 2) return;
    std::vector<Vec3> segments;
    segments.reserve((points.size() - 1)*2);
    for (std::size_t i = 1; i < points.size(); ++i) {
        segments.push_back(points[i - 1]);
        segments.push_back(points[i]);
    }
    data_->renderer->DrawLines(segments, data_->projection, data_->view, color, width);
}

void View3D::Axes(const std::string& id, const Pose3D& pose, float length, float width) {
    (void)id;
    if (length <= 0.0f) return;
    const Vec3 origin = pose.position;
    const Vec3 x = origin + Rotate(pose.orientation, {length, 0.0f, 0.0f});
    const Vec3 y = origin + Rotate(pose.orientation, {0.0f, length, 0.0f});
    const Vec3 z = origin + Rotate(pose.orientation, {0.0f, 0.0f, length});
    Line(origin, x, {0.95f, 0.20f, 0.18f, 1.0f}, width);
    Line(origin, y, {0.20f, 0.85f, 0.30f, 1.0f}, width);
    Line(origin, z, {0.20f, 0.45f, 1.00f, 1.0f}, width);
}

void View3D::Grid(float extent,
                  float spacing,
                  Color3D color,
                  float width,
                  float z) {
    if (!data_ || !data_->renderer || extent <= 0.0f || spacing <= 0.0f) return;
    const int count = std::min(1000, static_cast<int>(std::floor(extent/spacing)));
    std::vector<Vec3> lines;
    lines.reserve(static_cast<std::size_t>((2*count + 1)*4));
    for (int i = -count; i <= count; ++i) {
        const float v = i*spacing;
        lines.push_back({-extent, v, z});
        lines.push_back({ extent, v, z});
        lines.push_back({v, -extent, z});
        lines.push_back({v,  extent, z});
    }
    data_->renderer->DrawLines(lines, data_->projection, data_->view, color, width);
}

namespace detail {

std::string RenderView3D(View3DStorage& storage,
                         Vec2 logicalSize,
                         const CanvasInput& input,
                         const View3DOptions& requestedOptions,
                         const std::function<void(View3D&)>& callback) {
    if (!backend || !backend->renderer || !callback) return {};

    storage.size = logicalSize;
    storage.input = input;
    storage.options = requestedOptions;
    storage.options.orbitSensitivity = std::max(storage.options.orbitSensitivity, 0.0f);
    storage.options.panSensitivity = std::max(storage.options.panSensitivity, 0.0f);
    storage.options.zoomStep = std::max(storage.options.zoomStep, 1.001f);
    storage.options.minDistance = std::max(storage.options.minDistance, 1.0e-4f);
    storage.options.maxDistance = std::max(storage.options.maxDistance, storage.options.minDistance);

    UpdateCameraNavigation(storage);

    // Raw GL / ImGui / NanoVG own the state between View3D calls. Enter Magnum
    // rendering and force its cache to query/setup state from a known boundary.
    Magnum::GL::Context::current().resetState(Magnum::GL::Context::State::ExitExternal);
    // NanoVG and ImGui both touch GL state (notably scissor/stencil). Explicitly
    // establish the small state set expected by this 3D pass after invalidating
    // Magnum's cache, otherwise a previous external pass could clip all 3D draws.
    Magnum::GL::Renderer::enable(Magnum::GL::Renderer::Feature::DepthTest);
    Magnum::GL::Renderer::setDepthFunction(Magnum::GL::Renderer::DepthFunction::LessOrEqual);
    Magnum::GL::Renderer::setDepthMask(true);
    Magnum::GL::Renderer::setColorMask(true, true, true, true);
    Magnum::GL::Renderer::disable(Magnum::GL::Renderer::Feature::Blending);
    Magnum::GL::Renderer::disable(Magnum::GL::Renderer::Feature::ScissorTest);
    Magnum::GL::Renderer::disable(Magnum::GL::Renderer::Feature::StencilTest);
    Magnum::GL::Renderer::disable(Magnum::GL::Renderer::Feature::FaceCulling);

    View3D::Data data;
    data.storage = &storage;
    data.renderer = backend->renderer.get();
    data.projection = ProjectionMatrix(storage);
    data.view = ViewMatrix(storage);
    View3D view = View3DAccess::Make(&data);

    try {
        callback(view);
    } catch (...) {
        Magnum::GL::Renderer::setLineWidth(1.0f);
        Magnum::GL::Renderer::setPointSize(1.0f);
        Magnum::GL::Renderer::disable(Magnum::GL::Renderer::Feature::DepthTest);
        Magnum::GL::Context::current().resetState(Magnum::GL::Context::State::EnterExternal);
        throw;
    }

    Magnum::GL::Renderer::setLineWidth(1.0f);
    Magnum::GL::Renderer::setPointSize(1.0f);
    Magnum::GL::Renderer::disable(Magnum::GL::Renderer::Feature::DepthTest);
    Magnum::GL::Context::current().resetState(Magnum::GL::Context::State::EnterExternal);

    if (!storage.options.showStatus) return {};
    char text[256];
    const Vec3 p = storage.cameraPosition;
    const Vec3 t = storage.cameraTarget;
    std::snprintf(text, sizeof(text),
                  "camera (%.2f, %.2f, %.2f)  target (%.2f, %.2f, %.2f)  dist %.2f",
                  p.x, p.y, p.z, t.x, t.y, t.z, CameraDistance(storage));
    return text;
}

} // namespace detail
} // namespace render_module
