#pragma once
// Linux only: the contracts between the Direct2D (d2d_*.cpp), DirectWrite (dwrite_*.cpp) and WIC
// (wic_*.cpp) ports, all drawing through the Impeller kernel (src/linux/impeller), and the window
// layer that owns the GPU context and shows a window's frames (src/linux/window/gpu.cpp).
// Everything else talks through the COM-shaped interfaces in src/linux/win32.
#include "../win32/win32_base.hpp"
#include "../impeller/kernel.hpp"

#include <cstdint>
#include <memory>

struct IDWriteTextFormat;
struct IDWriteTextLayout;

namespace lm_gfx {
// ---- Window layer (src/linux/window/gpu.cpp) ----------------------------------------------
// The window's drawable size in pixels (0 x 0 for an unknown window).
void windowPixelSize(HWND window, UINT32& width, UINT32& height);
// The canvas a window's next frame is recorded into, `width` x `height` pixels; null when the
// window is gone or there is no GPU.
std::shared_ptr<lm_impeller::Canvas> windowCanvas(HWND window, int width, int height);
// Shows what was recorded into the window's canvas. False when the window is gone.
bool presentWindow(HWND window);

// ---- DirectWrite -> Direct2D (implemented in dwrite_*.cpp) --------------------------------
// Draws `layout` with its box's top-left corner at (x, y) in the canvas's current space, in
// `paint` (the target's brush). A range with a drawing effect (SetDrawingEffect, an
// ID2D1Brush) is painted with that brush. `aliased`: no antialiasing.
void drawTextLayout(lm_impeller::Canvas* canvas, IDWriteTextLayout* layout, float x, float y, const lm_impeller::Paint& paint, bool aliased);
// DrawText: `text` laid out in `format` within the box, drawn as drawTextLayout would.
// clip: D2D1_DRAW_TEXT_OPTIONS_CLIP (nothing outside the box).
void drawText(lm_impeller::Canvas* canvas, const wchar_t* text, UINT32 length, IDWriteTextFormat* format, float left, float top,
              float right, float bottom, bool clip, const lm_impeller::Paint& paint, bool aliased);

// ---- Direct2D -> DirectWrite (implemented in d2d_*.cpp) -----------------------------------
// `brush` (an ID2D1Brush passed as a drawing effect) as a paint in the target's world space.
// False when it is not one of this port's brushes.
bool brushPaint(IUnknown* brush, lm_impeller::Paint& paint);
} // namespace lm_gfx
