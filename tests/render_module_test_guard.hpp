#pragma once

#include "render_module/render_module.hpp"

struct RenderModuleTestGuard {
    RenderModuleTestGuard() = default;
    RenderModuleTestGuard(const RenderModuleTestGuard&) = delete;
    RenderModuleTestGuard& operator=(const RenderModuleTestGuard&) = delete;

    ~RenderModuleTestGuard() noexcept {
        RenderModule::Shutdown();
    }
};
