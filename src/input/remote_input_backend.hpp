#pragma once

#include "input_backend.hpp"
#include "remote_input_queue.hpp"
#include <imgui.h>

namespace render_module::detail {

ImGuiKey MapRenderKey(RenderKey key); // The only RenderKey -> ImGui translation.

class RemoteInputBackend final : public IInputBackend {
public:
    ~RemoteInputBackend() override { Shutdown(); }
    bool Init() override;
    void BeginFrame(ImGuiIO& io, int width, int height, double deltaTime) override;
    void Shutdown() override;
    std::shared_ptr<RemoteInputQueue> EventQueue() override { return queue_; }
private:
    void Modifiers(ImGuiIO& io);
    void Release(ImGuiIO& io, bool loseFocus);
    void Apply(ImGuiIO& io, const RemoteInputEvent& event);
    void Position(ImGuiIO& io, float nx, float ny);
    std::shared_ptr<RemoteInputQueue> queue_;
    std::bitset<KeyCount> keys_;
    std::bitset<MouseButtonCount> buttons_;
    float nx_ = 0, ny_ = 0;
    int width_ = 0, height_ = 0;
    bool initialized_ = false, focused_ = true, positioned_ = false, reproject_ = false;
};

} // namespace render_module::detail
