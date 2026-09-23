#pragma once

#include <string>

namespace render_module {

enum class Backend { Desktop, Headless };
enum class HeadlessContext { GlfwNullEgl, NativeEgl };

struct NativeEglConfig {
    // -1 selects the first enumerated device, or Mesa's surfaceless display
    // platform if device enumeration is unavailable. Nonnegative indices are
    // strict: an unavailable device is an error, not a fallback to another GPU.
    int deviceIndex = -1;
    // Explicit compatibility/testing path; otherwise prefer a surfaceless context.
    bool forcePbuffer = false;
};

struct Config {
    Backend backend = Backend::Desktop;
    HeadlessContext headlessContext = HeadlessContext::GlfwNullEgl;
    int width = 1280;
    int height = 720;
    double fps = 30.0;
    std::string title = "RenderModule";
    NativeEglConfig nativeEgl;
};

} // namespace render_module
