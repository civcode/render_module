#include "render_module/nvg_wrapper.hpp"

namespace nvg {

static NVGcontext* g_ctx = nullptr;

void SetContext(NVGcontext* ctx) { g_ctx = ctx; }

// Drawing
void BeginPath() { nvgBeginPath(g_ctx); }
void Circle(float cx, float cy, float r) { nvgCircle(g_ctx, cx, cy, r); }
void Rect(float x, float y, float w, float h) { nvgRect(g_ctx, x, y, w, h); }
void Ellipse(float cx, float cy, float rx, float ry) { nvgEllipse(g_ctx, cx, cy, rx, ry); }
void RoundedRect(float x, float y, float w, float h, float r) { nvgRoundedRect(g_ctx, x, y, w, h, r); }
void MoveTo(float x, float y) { nvgMoveTo(g_ctx, x, y); }
void LineTo(float x, float y) { nvgLineTo(g_ctx, x, y); }
void ArcTo(float x1, float y1, float x2, float y2, float radius) { nvgArcTo(g_ctx, x1, y1, x2, y2, radius); }
void BezierTo(float c1x, float c1y, float c2x, float c2y, float x, float y) { nvgBezierTo(g_ctx, c1x, c1y, c2x, c2y, x, y); }
void ClosePath() { nvgClosePath(g_ctx); }
void FillColor(NVGcolor color) { nvgFillColor(g_ctx, color); }
void StrokeColor(NVGcolor color) { nvgStrokeColor(g_ctx, color); }
void StrokeWidth(float size) { nvgStrokeWidth(g_ctx, size); }
void Fill() { nvgFill(g_ctx); }
void Stroke() { nvgStroke(g_ctx); }

void SetShapeAntiAlias(bool enabled) { nvgShapeAntiAlias(g_ctx, enabled ? 1 : 0); }

// Transform
void Translate(float x, float y) { nvgTranslate(g_ctx, x, y); }
void Rotate(float angle) { nvgRotate(g_ctx, angle); }
void Scale(float x, float y) { nvgScale(g_ctx, x, y); }
void ResetTransform() { nvgResetTransform(g_ctx); }

// State
void Save() { nvgSave(g_ctx); }
void Restore() { nvgRestore(g_ctx); }
void Reset() { nvgReset(g_ctx); }

// Paints
NVGpaint ImagePattern(float cx, float cy, float w, float h, float angle, int image, float alpha) {
    return nvgImagePattern(g_ctx, cx, cy, w, h, angle, image, alpha);
}

NVGpaint LinearGradient(float sx, float sy, float ex, float ey, NVGcolor icol, NVGcolor ocol) {
    return nvgLinearGradient(g_ctx, sx, sy, ex, ey, icol, ocol);
}
NVGpaint RadialGradient(float cx, float cy, float inr, float outr, NVGcolor icol, NVGcolor ocol) {
    return nvgRadialGradient(g_ctx, cx, cy, inr, outr, icol, ocol);
}
NVGpaint BoxGradient(float x, float y, float w, float h, float r, float f, NVGcolor icol, NVGcolor ocol) {
    return nvgBoxGradient(g_ctx, x, y, w, h, r, f, icol, ocol);
}
void FillPaint(NVGpaint paint) { nvgFillPaint(g_ctx, paint); }
void StrokePaint(NVGpaint paint) { nvgStrokePaint(g_ctx, paint); }

// Fonts & Text
void FontSize(float size) { nvgFontSize(g_ctx, size); }
void FontFace(const char* name) { nvgFontFace(g_ctx, name); }
void TextAlign(int align) { nvgTextAlign(g_ctx, align); }
void Text(float x, float y, const char* text) { 
    nvgSave(g_ctx);
    nvgScale(g_ctx, 1.0f, -1.0f); // Flip Y for NanoVG
    nvgText(g_ctx, x, -y, text, nullptr); 
    nvgRestore(g_ctx);
}
void TextBox(float x, float y, float breakWidth, const char* text) {
    nvgTextBox(g_ctx, x, y, breakWidth, text, nullptr);
}

// Clipping
void Scissor(float x, float y, float w, float h) { nvgScissor(g_ctx, x, y, w, h); }
void IntersectScissor(float x, float y, float w, float h) { nvgIntersectScissor(g_ctx, x, y, w, h); }
void ResetScissor() { nvgResetScissor(g_ctx); }

// Colors
NVGcolor RGB(unsigned char r, unsigned char g, unsigned char b) {
    return nvgRGB(r, g, b);
}
NVGcolor RGB(int r, int g, int b) {
    return nvgRGB(static_cast<unsigned char>(r), static_cast<unsigned char>(g), static_cast<unsigned char>(b));
}
NVGcolor RGBf(float r, float g, float b) {
    return nvgRGBf(r, g, b);
}
NVGcolor RGBA(unsigned char r, unsigned char g, unsigned char b, unsigned char a) {
    return nvgRGBA(r, g, b, a);
}
NVGcolor RGBA(int r, int g, int b, int a) {
    return nvgRGBA(static_cast<unsigned char>(r), static_cast<unsigned char>(g), static_cast<unsigned char>(b), static_cast<unsigned char>(a));
}
NVGcolor RGBAf(float r, float g, float b, float a) {
    return nvgRGBAf(r, g, b, a);
}

NVGLUframebuffer *GLUtilsCreateFramebuffer(int width, int height, int image_flags)
{
    return nvgluCreateFramebuffer(g_ctx, width, height, image_flags);
}
void GLUtilsBindFramebuffer(NVGLUframebuffer *fb)
{
    nvgluBindFramebuffer(fb);
}
} // namespace nvg