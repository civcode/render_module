#ifndef ZOOM_VIEW_HPP_
#define ZOOM_VIEW_HPP_
 
#include <nanovg.h>
#include <imgui.h>
#include <string>
#include <functional>

namespace ZoomView {

void Draw(const std::string& label, NVGcontext* vg, std::function<void(NVGcontext*)> drawCallback);

} // namespace ZoomView

#endif // ZOOM_VIEW_HPP_