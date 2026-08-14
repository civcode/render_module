#ifndef RENDER_MODULE_VIEW3D_INTERNAL_HPP_
#define RENDER_MODULE_VIEW3D_INTERNAL_HPP_

#include <functional>
#include <memory>
#include <string>

#include "render_module/view3d.hpp"

namespace render_module::detail {

struct View3DStorage;

bool Initialize3DBackend();
void Shutdown3DBackend() noexcept;
std::shared_ptr<View3DStorage> CreateView3DStorage();

std::string RenderView3D(View3DStorage& storage,
                         Vec2 logicalSize,
                         const CanvasInput& input,
                         const View3DOptions& options,
                         const std::function<void(View3D&)>& callback);

} // namespace render_module::detail

#endif // RENDER_MODULE_VIEW3D_INTERNAL_HPP_
