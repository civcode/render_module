#include "headless_adapters.hpp"
#include "input/remote_input_backend.hpp"
#include "present/image_presenter.hpp"

namespace render_module::detail {
std::unique_ptr<IInputBackend> CreateHeadlessInput() {
    // Metrics are supplied by the core from the root virtual display each frame.
    return std::make_unique<RemoteInputBackend>();
}
std::unique_ptr<IPresenter> CreateHeadlessPresenter() {
    return std::make_unique<ImagePresenter>();
}
} // namespace render_module::detail
