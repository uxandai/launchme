#pragma once
// Linux only: what this port of Direct2D (src/linux/gfx) adds for drawing that the Direct2D way
// would make expensive on Impeller.
#include "d2d1.h"

// Frosted glass without copying the target: on Impeller, CopyFromRenderTarget renders everything
// drawn so far into a texture. LaunchmeFrostCapture remembers what `target` holds now (no clip or
// layer pushed, as CopyFromRenderTarget asks); LaunchmeFrostDraw lays that over, blurred by
// `sigma` device pixels, within `bounds` (device pixels) and the clips and layers pushed since,
// at `opacity`. Both false when the target cannot (then the copy is the way).
bool LaunchmeFrostCapture(ID2D1RenderTarget* target);
bool LaunchmeFrostDraw(ID2D1RenderTarget* target, const D2D1_RECT_F& bounds, float sigma, float opacity);
// Tryb TV's 5-tap Catmull-Rom + CAS background upscaler (src/linux/impeller/shaders/upscale.frag).
bool LaunchmeUpscaleDraw(ID2D1RenderTarget* target, ID2D1Bitmap* bitmap, const D2D1_RECT_F& dest, float opacity);
