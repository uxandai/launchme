#pragma once
// Linux only: the contracts between the Direct2D (d2d_*.cpp), DirectWrite (dwrite_*.cpp) and WIC
// (wic_*.cpp) ports, all drawing through Skia, and the window layer that owns the GPU context
// and shows a window's frames (src/linux/window/gpu.cpp). Everything else talks through the
// COM-shaped interfaces in src/linux/win32.
#include "../win32/win32_base.hpp"

#include "include/core/SkRefCnt.h"

#include <cstdint>
#include <memory>

class GrDirectContext;
class SkCanvas;
class SkPaint;
class SkSurface;
struct IDWriteTextFormat;
struct IDWriteTextLayout;

namespace lm_gfx {
// ---- Window layer (src/linux/window/gpu.cpp) ----------------------------------------------
// The window's drawable size in pixels (0 x 0 for an unknown window).
void windowPixelSize(HWND window, UINT32& width, UINT32& height);
// The GPU context every render target on the UI thread shares (Vulkan, else OpenGL), or null:
// no GPU (drawing stays on the CPU), or a call from another thread (GPU work belongs to the
// thread that created the first window).
GrDirectContext* gpu();
// The surface a window's next frame is drawn into, `width` x `height` pixels; kept from frame
// to frame (GPU-backed with a GPU context, else in memory). Null when the window is gone.
sk_sp<SkSurface> windowSurface(HWND window, int width, int height);
// Shows what was drawn into the window's surface. False when the window is gone.
bool presentWindow(HWND window);
// An optional Impeller command canvas; null when this process selected Skia.
std::shared_ptr<SkCanvas> windowCanvas(HWND window, int width, int height);

// ---- DirectWrite → Direct2D (implemented in dwrite_*.cpp) --------------------------------
// Draws `layout` with its box's top-left corner at (x, y) in the canvas's current space, in
// `paint` (the target's brush). A range with a drawing effect (SetDrawingEffect, an
// ID2D1Brush) is painted with brushPaint's paint. `aliased`: no antialiasing.
void drawTextLayout(SkCanvas* canvas, IDWriteTextLayout* layout, float x, float y, const SkPaint& paint, bool aliased);
// DrawText: `text` laid out in `format` within the box, drawn as drawTextLayout would.
// clip: D2D1_DRAW_TEXT_OPTIONS_CLIP (nothing outside the box).
void drawText(SkCanvas* canvas, const wchar_t* text, UINT32 length, IDWriteTextFormat* format, float left, float top,
              float right, float bottom, bool clip, const SkPaint& paint, bool aliased);

// ---- Direct2D → DirectWrite (implemented in d2d_*.cpp) -----------------------------------
// `brush` (an ID2D1Brush passed as a drawing effect) as a paint in the target's world space,
// for drawing on `canvas`. False when it is not one of this port's brushes.
bool brushPaint(IUnknown* brush, SkCanvas* canvas, SkPaint& paint);
} // namespace lm_gfx
