#pragma once
// Tryb TV's CRT filter on Windows: a Direct2D custom effect whose pixel shader is Timothy Lottes'
// public domain CRT shader, adapted to a full-resolution interface (crt_filter.cpp). Two inputs: the
// frame and the previous filtered frame (the phosphor's afterglow).
#ifdef _WIN32
#include <d2d1_1.h>

namespace crt_filter {

// The shader's constant buffer (16-byte rows).
struct Constants {
    float width = 1, height = 1, warp_x = 0, warp_y = 0;
    float mask = 0, mask_dark = 1, mask_light = 1, mask_scale = 1;
    float hard_scan = -8, pitch = 3, glow = 0, boost = 1;
    float decay = 0, history = 0, scan_mix = 0, pad = 0;
};

// Registers the effect with `factory` (once per factory). False when it cannot be.
bool registerEffect(ID2D1Factory1* factory);
// A new instance on `context`; `constants` points at its block. After changing it, set property 0
// (a UINT32 generation) to a new value: Direct2D uploads the block only after a property change.
HRESULT create(ID2D1DeviceContext* context, ID2D1Effect** effect, Constants** constants);

} // namespace crt_filter
#endif
