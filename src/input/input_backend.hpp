#pragma once

#include <memory>
struct ImGuiIO;

namespace render_module::detail {
class RemoteInputQueue;

// Called only while the module's ImGui context is alive, on the render thread.
class IInputBackend {
public:
    virtual ~IInputBackend() = default;
    virtual bool Init() = 0;
    virtual void BeginFrame(ImGuiIO& io, int displayWidth, int displayHeight, double deltaTime) = 0;
    // Obtain on the render thread; the returned handle is safe on producers and
    // after module shutdown. Desktop has no remote queue.
    virtual std::shared_ptr<RemoteInputQueue> EventQueue() { return {}; }
    virtual void Shutdown() = 0;
};

} // namespace render_module::detail
