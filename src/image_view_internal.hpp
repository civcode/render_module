#ifndef RENDER_MODULE_IMAGE_VIEW_INTERNAL_HPP_
#define RENDER_MODULE_IMAGE_VIEW_INTERNAL_HPP_

#include <functional>

#include "render_module/image_view.hpp"

namespace render_module::detail {

struct ImageCanvasAccess {
    static ImageCanvas Make(ImageCanvas::Data& data) noexcept {
        return ImageCanvas(&data);
    }
};

std::function<void(Canvas&)> MakeImageViewRenderer(
    ImageProvider provider,
    ImageViewCallback overlayCallback,
    ImageViewOptions options);

} // namespace render_module::detail

#endif // RENDER_MODULE_IMAGE_VIEW_INTERNAL_HPP_
