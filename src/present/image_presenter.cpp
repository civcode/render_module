#include "image_presenter.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <new>

// Reuse the public-domain PNG writer already shipped with pinned NanoVG.
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

namespace render_module::detail {
namespace {
bool ImageSize(int width, int height, std::size_t& bytes) {
    // Diagnostic captures are bounded at 256 MiB. This also keeps the writer's
    // internal signed-int stride/compression calculations safely in range.
    if (width <= 0 || height <= 0 || width > 65536 || height > 65536) return false;
    const std::uint64_t size = std::uint64_t(width) * height * 4;
    if (size > 256u * 1024u * 1024u) return false;
    bytes = static_cast<std::size_t>(size);
    return true;
}
}

bool ImagePresenter::Read(const PresentedFrame& frame, ImageRgba& image) {
    std::size_t bytes = 0;
    if (!frame.IsValid() || !ImageSize(frame.width, frame.height, bytes)) return false;
    ImageRgba next;
    next.width = frame.width;
    next.height = frame.height;
    try { next.pixels.resize(bytes); }
    catch (const std::bad_alloc&) { return false; }
    if (!ReadInto(frame, next.pixels.data(), next.pixels.size(), frame.width*4)) return false;
    image = std::move(next); return true;
}

bool ImagePresenter::ReadInto(const PresentedFrame& frame, unsigned char* data, std::size_t capacity, int stride) {
    std::size_t bytes = 0;
    if (!frame.IsValid() || !ImageSize(frame.width,frame.height,bytes) || !data ||
        stride < frame.width*4 || stride%4 || std::uint64_t(stride)*frame.height > capacity) return false;
    GLint read = 0, buffer = 0, packBuffer = 0;
    GLint alignment = 0, rowLength = 0, skipRows = 0, skipPixels = 0;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &read);
    glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &packBuffer);
    glGetIntegerv(GL_PACK_ALIGNMENT, &alignment);
    glGetIntegerv(GL_PACK_ROW_LENGTH, &rowLength);
    glGetIntegerv(GL_PACK_SKIP_ROWS, &skipRows);
    glGetIntegerv(GL_PACK_SKIP_PIXELS, &skipPixels);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, frame.Framebuffer());
    glGetIntegerv(GL_READ_BUFFER, &buffer);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    // Never interpret the CPU pointer as a caller's pixel-pack-buffer offset.
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glPixelStorei(GL_PACK_ROW_LENGTH, stride/4);
    glPixelStorei(GL_PACK_SKIP_ROWS, 0);
    glPixelStorei(GL_PACK_SKIP_PIXELS, 0);
    glReadPixels(0, 0, frame.width, frame.height, GL_RGBA, GL_UNSIGNED_BYTE, data);
    const GLenum error = glGetError();
    glReadBuffer(buffer);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, read);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, packBuffer);
    glPixelStorei(GL_PACK_ALIGNMENT, alignment);
    glPixelStorei(GL_PACK_ROW_LENGTH, rowLength);
    glPixelStorei(GL_PACK_SKIP_ROWS, skipRows);
    glPixelStorei(GL_PACK_SKIP_PIXELS, skipPixels);
    if (error != GL_NO_ERROR) {
        std::fprintf(stderr, "RenderModule: screenshot readback failed (GL 0x%x).\n", error);
        return false;
    }

    // The ONE output flip: GL bottom-origin -> CPU image top-origin.
    // PNG writing and Desktop blitting perform no further orientation changes.
    for (int y = 0; y < frame.height/2; ++y) {
        auto first = data + std::size_t(y)*stride;
        auto last = data + std::size_t(frame.height - 1 - y)*stride;
        std::swap_ranges(first, first + frame.width*4, last);
    }
    return true;
}

bool ImagePresenter::WritePng(const std::string& path, const ImageRgba& image) {
    std::size_t bytes = 0;
    if (path.empty() || !ImageSize(image.width, image.height, bytes) || image.pixels.size() != bytes)
        return false;
    int length = 0;
    unsigned char* png = stbi_write_png_to_mem(const_cast<unsigned char*>(image.pixels.data()),
        image.width*4, image.width, image.height, 4, &length);
    if (!png) return false;
    FILE* file = std::fopen(path.c_str(), "wb");
    bool result = false;
    if (file) {
        result = std::fwrite(png, 1, length, file) == static_cast<std::size_t>(length);
        result = (std::fclose(file) == 0) && result;
    }
    std::free(png);
    if (!result) std::fprintf(stderr, "RenderModule: could not write PNG '%s'.\n", path.c_str());
    return result;
}

bool ImagePresenter::Present(const PresentedFrame& frame) {
    if (!frame.IsValid()) return false;
    if (path_.empty()) {
        glFlush();
        return true;
    }
    ImageRgba image;
    return Read(frame, image) && WritePng(path_, image);
}

} // namespace render_module::detail
