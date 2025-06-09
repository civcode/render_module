#ifndef NVG_WRAPPER_HPP_
#define NVG_WRAPPER_HPP_

#include "nanovg.h"

namespace nvg {

// Context
void SetContext(NVGcontext* ctx);

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
NVGcolor RGBA(unsigned char r, unsigned char g, unsigned char b, unsigned char a);

} // namespace nvg

#endif // NVG_WRAPPER_HPP_