#pragma once
#include "presenter.hpp"
#include "render_module/config.hpp"
#include <memory>
namespace render_module::detail {
class RemoteInputQueue;
std::unique_ptr<IPresenter> CreateWebPresenter(const Config& config, std::shared_ptr<RemoteInputQueue> input);
} // namespace render_module::detail
