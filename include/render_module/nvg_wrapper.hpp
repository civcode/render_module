#ifndef NVG_WRAPPER_HPP_
#define NVG_WRAPPER_HPP_

#include <glad/glad.h>
#include "nanovg.h"
// #include "nanovg_gl.h"
#include "nanovg_gl_utils.h"

namespace nvg {

// Context
void SetContext(NVGcontext* ctx);

void BeginFrame(int width, int height, float pixelRatio);
void EndFrame();

// Drawing
void BeginPath();
void Circle(float cx, float cy, float r);
void Rect(float x, float y, float w, float h);
void Ellipse(float cx, float cy, float rx, float ry);
void RoundedRect(float x, float y, float w, float h, float r);

void MoveTo(float x, float y);
void LineTo(float x, float y);
void ArcTo(float x1, float y1, float x2, float y2, float radius);
void BezierTo(float c1x, float c1y, float c2x, float c2y, float x, float y);
void ClosePath();
void FillColor(NVGcolor color);
void StrokeColor(NVGcolor color);
void StrokeWidth(float size);
void Fill();
void Stroke();

void SetShapeAntiAlias(bool enabled);

// Transform
void Translate(float x, float y);
void Rotate(float angle);
void Scale(float x, float y);
void ResetTransform();

// State
void Save();
void Restore();
void Reset();

// Paints
NVGpaint ImagePattern(float cx, float cy, float w, float h, float angle, int image, float alpha);
NVGpaint LinearGradient(float sx, float sy, float ex, float ey, NVGcolor icol, NVGcolor ocol);
NVGpaint RadialGradient(float cx, float cy, float inr, float outr, NVGcolor icol, NVGcolor ocol);
NVGpaint BoxGradient(float x, float y, float w, float h, float r, float f, NVGcolor icol, NVGcolor ocol);
void FillPaint(NVGpaint paint);
void StrokePaint(NVGpaint paint);

// Fonts & Text
void FontSize(float size);
void FontFace(const char* name);
void TextAlign(int align);
void Text(float x, float y, const char* text);
void TextBox(float x, float y, float breakWidth, const char* text);

// Clipping
void Scissor(float x, float y, float w, float h);
void IntersectScissor(float x, float y, float w, float h);
void ResetScissor();

// Utilities
NVGcolor RGB(unsigned char r, unsigned char g, unsigned char b);
NVGcolor RGB(int r, int g, int b); 
NVGcolor RGBf(float r, float g, float b);
NVGcolor RGBA(unsigned char r, unsigned char g, unsigned char b, unsigned char a);
NVGcolor RGBA(int r, int g, int b, int a); 
NVGcolor RGBAf(float r, float g, float b, float a);

template<typename T>
NVGcolor RGBA(T, T, T, T) {
    static_assert(sizeof(T) == 0, "Use RGBAf() for float or RGBA() for uchar");
    return NVGcolor(); 
}


/* OpenGL utilities */
NVGLUframebuffer*  GLUtilsCreateFramebuffer(int width, int height, int image_flags);
void GLUtilsBindFramebuffer(NVGLUframebuffer* fb);

} // namespace nvg

#endif // NVG_WRAPPER_HPP_