#include "headless_adapters.hpp"
#include "present/image_presenter.hpp"

#include <algorithm>
#include <chrono>
#include <glad/glad.h>
#include <imgui.h>

namespace render_module::detail {
namespace {
class HeadlessFrameInput final : public IInputBackend {
public:
    explicit HeadlessFrameInput(PlatformSize size) : size_(size) {}
    ~HeadlessFrameInput() override { Shutdown(); }
    bool Init() override {
        if (initialized_) return true;
        auto& io = ImGui::GetIO();
        io.BackendPlatformName = "render_module_headless_frame_metrics";
        io.ConfigFlags &= ~ImGuiConfigFlags_ViewportsEnable;
        previous_ = Clock::now();
        initialized_ = true;
        return true;
    }
    void SetDisplaySize(int width, int height) override { size_ = {width, height}; }
    void NewFrame() override {
        auto& io = ImGui::GetIO();
        const auto now = Clock::now();
        io.DisplaySize = {float(size_.width), float(size_.height)};
        io.DisplayFramebufferScale = {1.0f, 1.0f};
        io.DeltaTime = std::max(1.0e-6f, std::chrono::duration<float>(now - previous_).count());
        previous_ = now;
    }
    void Shutdown() override {
        if (!initialized_) return;
        ImGui::GetIO().BackendPlatformName = nullptr;
        initialized_ = false;
    }
private:
    using Clock = std::chrono::steady_clock;
    PlatformSize size_;
    Clock::time_point previous_;
    bool initialized_ = false;
};

} // namespace

std::unique_ptr<IInputBackend> CreateHeadlessFrameInput(PlatformSize size) {
    return std::make_unique<HeadlessFrameInput>(size);
}
std::unique_ptr<IPresenter> CreateHeadlessPresenter() {
    return std::make_unique<ImagePresenter>();
}
} // namespace render_module::detail
