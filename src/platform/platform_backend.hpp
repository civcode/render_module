#pragma once

#include <memory>
#include <string>

#include "render_module/config.hpp"

namespace render_module::detail {

class IInputBackend;
class IPresenter;

struct PlatformSize {
    int width = 0;
    int height = 0;
};

struct GraphicsDiagnostics {
    std::string provider;
    std::string device;
    std::string eglVendor;
    std::string eglVersion;
};

// Private interfaces. All operations belong to the main/render thread.
class IGraphicsContext {
public:
    // Matches GLAD's loader signature without exposing a GL loader or window API.
    using ProcAddressLoader = void* (*)(const char*);
    virtual ~IGraphicsContext() = default;
    virtual bool MakeCurrent() = 0;
    virtual void* GetProcAddress(const char* name) const = 0;
    virtual GraphicsDiagnostics Diagnostics() const = 0;
    ProcAddressLoader GetProcAddressLoader() const;

    // Shared by GLAD and Magnum; dispatches to the provider made current on this thread.
    static void* LoadCurrentProcAddress(const char* name);

protected:
    void MarkCurrent();
    void ClearCurrent();
};

class IPlatformBackend : public IGraphicsContext {
public:
    virtual bool Initialize(int width, int height, const char* title) = 0;
    virtual bool IsInitialized() const = 0;
    virtual void Shutdown() = 0;
    virtual void PollEvents() = 0;
    virtual bool ShouldClose() const = 0;
    virtual void RequestClose() = 0;
    virtual PlatformSize GetWindowSize() const = 0; // Logical coordinates.
    virtual PlatformSize GetFramebufferSize() const = 0; // Physical pixels.

    // Adapters borrow platform resources and must be destroyed before Shutdown().
    virtual std::unique_ptr<IInputBackend> CreateInputBackend() = 0;
    virtual std::unique_ptr<IPresenter> CreatePresenter() = 0;
};

std::unique_ptr<IPlatformBackend> CreateGlfwDesktopBackend();
std::unique_ptr<IPlatformBackend> CreateGlfwNullEglBackend();
std::unique_ptr<IPlatformBackend> CreateNativeEglBackend(const NativeEglConfig& config);
std::unique_ptr<IPlatformBackend> CreatePlatformBackend(const Config& config);
// Call after GL loading, with the selected context current. Checks GL 3.3 core.
bool PrintGraphicsDiagnostics(const IGraphicsContext& context);

} // namespace render_module::detail
