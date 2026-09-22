#pragma once

namespace render_module::detail {

// Called only while the module's ImGui context is alive, on the render thread.
class IInputBackend {
public:
    virtual ~IInputBackend() = default;
    virtual bool Init() = 0;
    virtual void NewFrame() = 0;
    virtual void Shutdown() = 0;
};

} // namespace render_module::detail
