#include "platform_backend.hpp"
#include <Magnum/Platform/Implementation/OpenGLFunctionLoader.h>

// Magnum's stock EGL and GLX context libraries define these same symbols.
// Build exactly one implementation, resolving through the current provider.
// The matching, pinned upstream flextGLPlatform.cpp is adapted by CMake to
// reload GL 1.0/1.1 too (including the NVIDIA EGL workaround) on every context.
namespace Magnum::Platform::Implementation {

OpenGLFunctionLoader::OpenGLFunctionLoader() = default;
OpenGLFunctionLoader::~OpenGLFunctionLoader() = default;

OpenGLFunctionLoader::FunctionPointer OpenGLFunctionLoader::load(const char* name) {
    return reinterpret_cast<FunctionPointer>(
        render_module::detail::IGraphicsContext::LoadCurrentProcAddress(name));
}

} // namespace Magnum::Platform::Implementation
