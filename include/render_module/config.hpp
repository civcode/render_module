#pragma once

#include <string>
#include <cstdint>
#include <vector>

namespace render_module {

enum class Backend { Desktop, Headless, Web };
enum class HeadlessContext { GlfwNullEgl, NativeEgl };

struct NativeEglConfig {
    // -1 selects the first enumerated device, or Mesa's surfaceless display
    // platform if device enumeration is unavailable. Nonnegative indices are
    // strict: an unavailable device is an error, not a fallback to another GPU.
    int deviceIndex = -1;
    // Explicit compatibility/testing path; otherwise prefer a surfaceless context.
    bool forcePbuffer = false;
};

// TEMPORARY DEVELOPMENT TRANSPORT: JPEG over WebSocket, not realtime video.
struct WebConfig {
    std::string bindAddress = "127.0.0.1";
    std::uint16_t port = 8080;
    // 16–128 URL-safe ASCII characters. Never printed. Use TLS outside loopback.
    std::string authToken;
    std::vector<std::string> allowedOrigins;
    bool allowUnauthenticatedPublicBind = false;
    bool allowMultipleViewers = true;
    unsigned maxClients = 8;
    int jpegQuality = 80;
    int maxWidth = 1920, maxHeight = 1080;
    double fps = 20.0;
};

struct Config {
    Backend backend = Backend::Desktop;
    HeadlessContext headlessContext = HeadlessContext::GlfwNullEgl;
    int width = 1280;
    int height = 720;
    double fps = 30.0;
    std::string title = "RenderModule";
    NativeEglConfig nativeEgl;
    WebConfig web;
};

} // namespace render_module
