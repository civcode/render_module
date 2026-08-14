#include "render_module/render_module.hpp"

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

#include <imgui_internal.h>

namespace {

using render_module::ImageCanvas;
using render_module::ImageFrame;
using render_module::ImagePixelFormat;
using render_module::LatestImage;
using render_module::MouseButton;
using render_module::Vec2;

struct Selection {
    bool dragging = false;
    bool hasRectangle = false;
    bool cursorOverImage = false;
    Vec2 start{};
    Vec2 end{};
    Vec2 cursor{};
};

Vec2 ClampToImage(Vec2 point, Vec2 size) {
    return {
        std::clamp(point.x, 0.0f, std::max(0.0f, size.x - 1.0f)),
        std::clamp(point.y, 0.0f, std::max(0.0f, size.y - 1.0f))
    };
}

cv::Mat MakeSyntheticCameraFrame(std::uint64_t sequence) {
    constexpr int width = 800;
    constexpr int height = 520;
    cv::Mat image(height, width, CV_8UC3);

    for (int y = 0; y < height; ++y) {
        cv::Vec3b* row = image.ptr<cv::Vec3b>(y);
        for (int x = 0; x < width; ++x) {
            row[x] = cv::Vec3b(
                static_cast<unsigned char>(35 + 90 * x / width),
                static_cast<unsigned char>(30 + 100 * y / height),
                static_cast<unsigned char>(55 + 70 * (x + y) / (width + height)));
        }
    }

    for (int x = 0; x < width; x += 80) {
        cv::line(image, {x, 0}, {x, height - 1}, {80, 80, 80}, 1);
    }
    for (int y = 0; y < height; y += 80) {
        cv::line(image, {0, y}, {width - 1, y}, {80, 80, 80}, 1);
    }

    const double time = static_cast<double>(sequence) / 30.0;
    const cv::Point movingPoint{
        static_cast<int>(width * (0.5 + 0.35 * std::sin(time * 0.9))),
        static_cast<int>(height * (0.5 + 0.30 * std::cos(time * 1.2)))
    };
    cv::circle(image, movingPoint, 34, {40, 190, 255}, -1, cv::LINE_AA);
    cv::circle(image, movingPoint, 38, {245, 245, 245}, 2, cv::LINE_AA);

    cv::putText(image,
                "OpenCV producer -> LatestImage -> RenderModule",
                {24, 42}, cv::FONT_HERSHEY_SIMPLEX, 0.75,
                {245, 245, 245}, 2, cv::LINE_AA);
    cv::putText(image,
                "frame " + std::to_string(sequence),
                {24, height - 24}, cv::FONT_HERSHEY_SIMPLEX, 0.65,
                {225, 235, 245}, 2, cv::LINE_AA);
    return image;
}

void PublishBGR(const cv::Mat& image,
                std::uint64_t sequence,
                const std::shared_ptr<LatestImage>& destination) {
    if (image.empty() || image.type() != CV_8UC3) return;
    destination->Publish(ImageFrame::Copy(
        image.cols,
        image.rows,
        ImagePixelFormat::BGR8,
        image.data,
        image.step,
        sequence));
}

void ProducerLoop(const std::shared_ptr<LatestImage>& destination,
                  std::atomic<bool>& stop,
                  int cameraIndex) {
    cv::VideoCapture camera;
    if (cameraIndex >= 0 && !camera.open(cameraIndex)) {
        std::fprintf(stderr,
                     "ImageViewDemo: camera %d could not be opened; using synthetic frames.\n",
                     cameraIndex);
    }

    std::uint64_t sequence = 0;
    auto nextSyntheticFrame = std::chrono::steady_clock::now();
    while (!stop.load(std::memory_order_relaxed)) {
        cv::Mat image;
        if (camera.isOpened()) {
            if (!camera.read(image)) continue;
        } else {
            image = MakeSyntheticCameraFrame(sequence);
        }

        PublishBGR(image, ++sequence, destination);

        if (!camera.isOpened()) {
            nextSyntheticFrame += std::chrono::milliseconds(33);
            std::this_thread::sleep_until(nextSyntheticFrame);
        }
    }
}

void DrawCrosshair(ImageCanvas& view, Vec2 imagePoint) {
    NVGcontext* vg = view.Graphics();
    const Vec2 point = view.ImageToCanvas(imagePoint);
    constexpr float radius = 8.0f;

    nvgBeginPath(vg);
    nvgMoveTo(vg, point.x - radius, point.y);
    nvgLineTo(vg, point.x + radius, point.y);
    nvgMoveTo(vg, point.x, point.y - radius);
    nvgLineTo(vg, point.x, point.y + radius);
    nvgStrokeWidth(vg, 1.5f);
    nvgStrokeColor(vg, nvgRGBA(255, 225, 65, 245));
    nvgStroke(vg);
}

void DrawSelection(ImageCanvas& view, const Selection& selection) {
    if (!selection.dragging && !selection.hasRectangle) return;

    NVGcontext* vg = view.Graphics();
    const Vec2 a = view.ImageToCanvas(selection.start);
    const Vec2 b = view.ImageToCanvas(selection.end);
    const float x = std::min(a.x, b.x);
    const float y = std::min(a.y, b.y);
    const float width = std::abs(b.x - a.x);
    const float height = std::abs(b.y - a.y);

    nvgBeginPath(vg);
    nvgRect(vg, x, y, width, height);
    nvgFillColor(vg, nvgRGBA(55, 155, 255, 45));
    nvgFill(vg);
    nvgStrokeWidth(vg, 2.0f);
    nvgStrokeColor(vg, nvgRGBA(70, 180, 255, 255));
    nvgStroke(vg);
}

} // namespace

int main(int argc, char** argv) {
    using namespace render_module;

    int cameraIndex = -1;
    if (argc == 3 && std::string(argv[1]) == "--camera") {
        cameraIndex = std::atoi(argv[2]);
    }

    if (!RenderModule::Init(1200, 780, 60.0, "RenderModule ImageView demo")) {
        return 1;
    }
    RenderModule::EnableRootWindowDocking();

    auto latestImage = std::make_shared<LatestImage>();
    std::atomic<bool> stopProducer{false};
    std::thread producer(ProducerLoop,
                         latestImage,
                         std::ref(stopProducer),
                         cameraIndex);

    Selection selection;
    bool resetViewRequested = false;

    ImageViewOptions imageOptions;
    imageOptions.initialFit = ImageFit::Contain;
    imageOptions.sampling = ImageSampling::Linear;
    imageOptions.panButton = MouseButton::Middle;
    imageOptions.showStatus = true;

    RenderModule::RegisterImageView(
        "Camera image",
        latestImage,
        [&](ImageCanvas& view) {
            if (resetViewRequested) {
                view.ResetView();
                resetViewRequested = false;
            }

            const ImageInput& input = view.Input();
            const auto& left = input.Button(MouseButton::Left);
            selection.cursorOverImage = view.IsHovered();
            selection.cursor = view.MouseImagePosition();

            if (left.pressed && view.IsHovered()) {
                selection.start = ClampToImage(view.MouseImagePosition(), view.ImageSize());
                selection.end = selection.start;
                selection.dragging = true;
                selection.hasRectangle = false;
                view.CapturePointer();
            }

            if (selection.dragging) {
                view.CapturePointer();
                selection.end = ClampToImage(view.MouseImagePosition(), view.ImageSize());
                if (left.released) {
                    selection.dragging = false;
                    selection.hasRectangle = true;
                }
            }

            if (view.IsHovered()) DrawCrosshair(view, selection.cursor);
            DrawSelection(view, selection);
        },
        imageOptions);

    RenderModule::RegisterImGuiCallback([&] {
        static bool layoutInitialized = false;
        const ImGuiID dockspace = RenderModule::GetRootDockspaceID();
        if (!layoutInitialized && dockspace != 0) {
            layoutInitialized = true;
            ImGui::DockBuilderRemoveNode(dockspace);
            ImGui::DockBuilderAddNode(dockspace, ImGuiDockNodeFlags_DockSpace);
            ImGui::DockBuilderSetNodeSize(
                dockspace, ImGui::GetMainViewport()->WorkSize);
            ImGuiID imageNode = dockspace;
            const ImGuiID controlsNode = ImGui::DockBuilderSplitNode(
                imageNode, ImGuiDir_Right, 0.27f, nullptr, &imageNode);
            ImGui::DockBuilderDockWindow("Camera image", imageNode);
            ImGui::DockBuilderDockWindow("Image controls", controlsNode);
            ImGui::DockBuilderFinish(dockspace);
        }

        ImGui::Begin("Image controls");
        ImGui::TextWrapped(
            "The producer runs outside the render loop and publishes immutable "
            "frames through LatestImage.");
        ImGui::Separator();
        ImGui::BulletText("Left drag: select an image rectangle");
        ImGui::BulletText("Middle drag: pan");
        ImGui::BulletText("Mouse wheel: zoom around cursor");
        if (ImGui::Button("Reset image view")) resetViewRequested = true;
        ImGui::SameLine();
        if (ImGui::Button("Clear selection")) {
            selection.dragging = false;
            selection.hasRectangle = false;
        }

        if (selection.cursorOverImage) {
            ImGui::Text("Cursor pixel: %.1f, %.1f",
                        selection.cursor.x, selection.cursor.y);
        }
        if (selection.dragging || selection.hasRectangle) {
            const float x = std::min(selection.start.x, selection.end.x);
            const float y = std::min(selection.start.y, selection.end.y);
            const float width = std::abs(selection.end.x - selection.start.x);
            const float height = std::abs(selection.end.y - selection.start.y);
            ImGui::Text("Selection: x %.1f, y %.1f, w %.1f, h %.1f",
                        x, y, width, height);
        }
        ImGui::Separator();
        ImGui::Text("Render rate: %.1f FPS", RenderModule::GetFPS());
        if (cameraIndex < 0) {
            ImGui::TextWrapped(
                "Synthetic OpenCV frames are active. Run with --camera 0 to "
                "use camera device zero.");
        } else {
            ImGui::Text("Requested camera device: %d", cameraIndex);
        }
        ImGui::End();
    });

    RenderModule::Run();

    stopProducer.store(true, std::memory_order_relaxed);
    if (producer.joinable()) producer.join();
    RenderModule::Shutdown();
    return 0;
}
