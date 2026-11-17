#pragma once
// Tryb TV's background upscaler on Windows: a Direct2D custom effect implementing AMD FidelityFX
// Super Resolution 1.0 (EASU 12-tap edge-adaptive spatial upsampling followed by RCAS 5-tap
// contrast-adaptive sharpening, MIT-licensed algorithm). Linux uses a 5-tap Catmull-Rom + CAS
// fragment shader in Impeller (src/linux/impeller/shaders/upscale.frag, launchme_d2d.h).
#ifdef _WIN32
#include <d2d1_1.h>

namespace upscale_filter {

// Constant buffer shared by EASU and RCAS (three 16-byte float4 rows).
struct Constants {
    float src_width = 1, src_height = 1, out_width = 1, out_height = 1;
    float crop_x = 0, crop_y = 0, crop_width = 1, crop_height = 1;
    float rcas_con = 0.8408964f; // exp2(-0.25f): 0.25 stops attenuation.
    float opacity = 1.0f, pad0 = 0, pad1 = 0;
};

// Registers the effect with `factory` (once per factory). False when it cannot be.
bool registerEffect(ID2D1Factory1* factory);
// A new instance on `context`; `constants` points at its block. After changing it, set property 0
// (a UINT32 generation) to a new value so Direct2D re-queries output bounds and uploads constants.
HRESULT create(ID2D1DeviceContext* context, ID2D1Effect** effect, Constants** constants);

} // namespace upscale_filter
#endif
