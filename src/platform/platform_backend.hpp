#pragma once

#include <memory>

namespace render_module::detail {

class IInputBackend;
class IPresenter;

struct PlatformSize {
    int width = 0;
    int height = 0;
};

// Private interfaces. All operations belong to the main/render thread.
class IGraphicsContext {
public:
    // Matches GLAD's loader signature without exposing a GL loader or window API.
    using ProcAddressLoader = void* (*)(const char*);
    virtual ~IGraphicsContext() = default;
    virtual void MakeCurrent() = 0;
    virtual ProcAddressLoader GetProcAddressLoader() const = 0;
};

class IPlatformBackend : public IGraphicsContext {
public:
    virtual bool Init(int width, int height, const char* title) = 0;
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

} // namespace render_module::detail
