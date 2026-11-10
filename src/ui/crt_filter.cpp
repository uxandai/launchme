// Tryb TV's CRT filter: a Direct2D custom effect (one draw transform, two inputs) over the whole frame.
//
// The pixel shader is Timothy Lottes' "PUBLIC DOMAIN CRT STYLED SCAN-LINE SHADER" (libretro's
// crt/shaders/crt-lottes.glsl: "Please take and use, change, or whatever."), adapted:
// - the source is the launcher's own frame at the screen's resolution, not a low-resolution game: each
//   line is sampled sharp along its length, so text stays legible; only the lines are emulated
//   (about 360 of them, crt_view::linePitch);
// - its curvature (Warp), scanline weights (Scan/Tri), bloom and the four shadow masks are Lottes';
// - the phosphor's afterglow is ours: the previous filtered frame, fading, under the new one.
#ifdef _WIN32
#include "crt_filter.hpp"

#include <d2d1effectauthor.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <atomic>
#include <mutex>
#include <vector>

namespace crt_filter {
namespace {

using Microsoft::WRL::ComPtr;

// {6C1B2E7A-3F0D-4E2B-9A61-2D8C4B7E5F13}
constexpr GUID effect_id = {0x6c1b2e7a, 0x3f0d, 0x4e2b, {0x9a, 0x61, 0x2d, 0x8c, 0x4b, 0x7e, 0x5f, 0x13}};
// {A4D2C9E1-58B7-4F30-8E2A-71C6D3B9F042}
constexpr GUID shader_id = {0xa4d2c9e1, 0x58b7, 0x4f30, {0x8e, 0x2a, 0x71, 0xc6, 0xd3, 0xb9, 0xf0, 0x42}};

constexpr char shader_source[] = R"(
Texture2D Source : register(t0);
Texture2D History : register(t1);
SamplerState SourceSampler : register(s0);
SamplerState HistorySampler : register(s1);
cbuffer Constants : register(b0) {
    float2 size; float2 warp;
    float mask; float mask_dark; float mask_light; float mask_scale;
    float hard_scan; float pitch; float glow; float boost;
    float decay; float history; float scan_mix; float pad;
};

static float4 here;   // TEXCOORD0: this pixel in the input's texture, and one pixel's size there.
static float2 origin; // This pixel in scene pixels.

float3 toLinear(float3 c) { return pow(max(c, 0.0), 2.2); }
float3 toSrgb(float3 c) { return pow(max(c, 0.0), 1.0 / 2.2); }

// The frame at a scene position (sharp along a line: bilinear at the screen's own resolution).
float3 fetch(float2 p) {
    p = clamp(p, 0.5, size - 0.5);
    return toLinear(Source.SampleLevel(SourceSampler, here.xy + (p - origin) * here.zw, 0).rgb);
}

float gaus(float d, float scale) { return exp2(scale * d * d); }

// Lottes' Warp: where the bent screen's pixel samples the picture.
float2 bend(float2 p) {
    float2 c = p / size * 2.0 - 1.0;
    c *= float2(1.0 + c.y * c.y * warp.x, 1.0 + c.x * c.x * warp.y);
    return (c * 0.5 + 0.5) * size;
}

// Lottes' Mask: 1 compressed TV shadow mask, 2 aperture grille, 3 stretched VGA, 4 VGA.
float3 maskAt(float2 pos) {
    float3 m = float3(mask_dark, mask_dark, mask_dark);
    if (mask < 1.5) {
        float row = mask_light, odd = 0.0;
        if (frac(pos.x * 0.166666666) < 0.5) odd = 1.0;
        if (frac((pos.y + odd) * 0.5) < 0.5) row = mask_dark;
        pos.x = frac(pos.x * 0.333333333);
        if (pos.x < 0.333) m.r = mask_light; else if (pos.x < 0.666) m.g = mask_light; else m.b = mask_light;
        m *= row;
    } else if (mask < 2.5) {
        pos.x = frac(pos.x * 0.333333333);
        if (pos.x < 0.333) m.r = mask_light; else if (pos.x < 0.666) m.g = mask_light; else m.b = mask_light;
    } else if (mask < 3.5) {
        pos.x += pos.y * 3.0;
        pos.x = frac(pos.x * 0.166666666);
        if (pos.x < 0.333) m.r = mask_light; else if (pos.x < 0.666) m.g = mask_light; else m.b = mask_light;
    } else {
        pos.xy = floor(pos.xy * float2(1.0, 0.5));
        pos.x += pos.y * 3.0;
        pos.x = frac(pos.x * 0.166666666);
        if (pos.x < 0.333) m.r = mask_light; else if (pos.x < 0.666) m.g = mask_light; else m.b = mask_light;
    }
    return m;
}

float4 main(float4 position : SV_POSITION, float4 scene : SCENE_POSITION, float4 uv0 : TEXCOORD0, float4 uv1 : TEXCOORD1) : SV_TARGET {
    here = uv0; origin = scene.xy;
    float2 q = bend(scene.xy);
    float3 color = float3(0.0, 0.0, 0.0);
    if (q.x >= 0.0 && q.y >= 0.0 && q.x <= size.x && q.y <= size.y) {
        // The nearest three lines (Lottes' Tri), each a Gaussian across its centre.
        float at = q.y / pitch, base = floor(at), d = 0.5 - (at - base);
        float3 lines = float3(0.0, 0.0, 0.0);
        [unroll] for (int o = -1; o <= 1; ++o)
            lines += fetch(float2(q.x, (base + o + 0.5) * pitch)) * gaus(d + o, hard_scan);
        color = lerp(fetch(q), lines, scan_mix);
        // Lottes' small bloom: five lines, seven line-sized steps along each.
        if (glow > 0.0) {
            float column = q.x / pitch, left = floor(column), dx = 0.5 - (column - left);
            float3 bloom = float3(0.0, 0.0, 0.0);
            [unroll] for (int y = -2; y <= 2; ++y) {
                float3 row = float3(0.0, 0.0, 0.0); float weight = 0.0;
                [unroll] for (int x = -3; x <= 3; ++x) {
                    float w = gaus(dx + x, -1.5);
                    row += fetch(float2((left + x + 0.5) * pitch, (base + y + 0.5) * pitch)) * w;
                    weight += w;
                }
                bloom += row / weight * gaus(d + y, -2.0);
            }
            color += bloom * glow;
        }
        if (mask > 0.5) color *= maskAt(floor(scene.xy / mask_scale) + 0.5);
        color *= boost;
        // The tube's edge: two pixels of fade instead of a cut.
        float2 edge = min(q, size - q);
        color *= saturate(min(edge.x, edge.y) * 0.5);
    }
    float3 shown = toSrgb(color);
    if (history > 0.5) shown = max(shown, History.SampleLevel(HistorySampler, uv1.xy, 0).rgb * decay);
    return float4(saturate(shown), 1.0);
}
)";

std::mutex bytecode_lock;
std::vector<char> bytecode;

bool compiled() {
    std::lock_guard lock(bytecode_lock);
    if (!bytecode.empty()) return true;
    ComPtr<ID3DBlob> code, errors;
    if (FAILED(D3DCompile(shader_source, sizeof(shader_source) - 1, "crt", nullptr, nullptr, "main", "ps_4_0",
                          D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, code.GetAddressOf(), errors.GetAddressOf())))
        return false;
    const auto* bytes = static_cast<const char*>(code->GetBufferPointer());
    bytecode.assign(bytes, bytes + code->GetBufferSize());
    return true;
}

class Effect final : public ID2D1EffectImpl, public ID2D1DrawTransform {
public:
    Constants constants;

    static HRESULT __stdcall make(IUnknown** effect) {
        auto* created = new (std::nothrow) Effect();
        if (!created) return E_OUTOFMEMORY;
        *effect = static_cast<ID2D1EffectImpl*>(created);
        last_ = created;
        return S_OK;
    }
    static Effect* last_; // The instance CreateEffect just made (the UI thread's only).
    // Direct2D calls PrepareForRender (which uploads `constants`) only after a property changed:
    // the renderer sets this one whenever the constants did.
    UINT32 generation = 0;
    static HRESULT CALLBACK setGeneration(IUnknown* effect, const BYTE* data, UINT32 size) {
        if (size != sizeof(UINT32)) return E_INVALIDARG;
        static_cast<Effect*>(reinterpret_cast<ID2D1EffectImpl*>(effect))->generation = *reinterpret_cast<const UINT32*>(data);
        return S_OK;
    }
    static HRESULT CALLBACK getGeneration(const IUnknown* effect, BYTE* data, UINT32 size, UINT32* actual) {
        if (actual) *actual = sizeof(UINT32);
        if (!data) return S_OK;
        if (size < sizeof(UINT32)) return E_NOT_SUFFICIENT_BUFFER;
        *reinterpret_cast<UINT32*>(data) = static_cast<const Effect*>(reinterpret_cast<const ID2D1EffectImpl*>(effect))->generation;
        return S_OK;
    }

    // IUnknown
    IFACEMETHODIMP_(ULONG) AddRef() override { return ++refs_; }
    IFACEMETHODIMP_(ULONG) Release() override {
        const ULONG left = --refs_;
        if (!left) delete this;
        return left;
    }
    IFACEMETHODIMP QueryInterface(REFIID id, void** out) override {
        if (!out) return E_POINTER;
        if (id == __uuidof(ID2D1EffectImpl) || id == __uuidof(IUnknown)) *out = static_cast<ID2D1EffectImpl*>(this);
        else if (id == __uuidof(ID2D1DrawTransform)) *out = static_cast<ID2D1DrawTransform*>(this);
        else if (id == __uuidof(ID2D1Transform)) *out = static_cast<ID2D1Transform*>(this);
        else if (id == __uuidof(ID2D1TransformNode)) *out = static_cast<ID2D1TransformNode*>(this);
        else { *out = nullptr; return E_NOINTERFACE; }
        AddRef();
        return S_OK;
    }

    // ID2D1EffectImpl
    IFACEMETHODIMP Initialize(ID2D1EffectContext* context, ID2D1TransformGraph* graph) override {
        if (!compiled()) return E_FAIL;
        const HRESULT loaded = context->LoadPixelShader(shader_id, reinterpret_cast<const BYTE*>(bytecode.data()), static_cast<UINT32>(bytecode.size()));
        if (FAILED(loaded)) return loaded;
        return graph->SetSingleTransformNode(this);
    }
    IFACEMETHODIMP PrepareForRender(D2D1_CHANGE_TYPE) override {
        return info_ ? info_->SetPixelShaderConstantBuffer(reinterpret_cast<const BYTE*>(&constants), sizeof(constants)) : E_FAIL;
    }
    IFACEMETHODIMP SetGraph(ID2D1TransformGraph*) override { return E_NOTIMPL; }

    // ID2D1DrawTransform
    IFACEMETHODIMP SetDrawInfo(ID2D1DrawInfo* info) override {
        info_ = info;
        info->SetInputDescription(0, {D2D1_FILTER_MIN_MAG_MIP_LINEAR, 0});
        info->SetInputDescription(1, {D2D1_FILTER_MIN_MAG_MIP_POINT, 0});
        return info->SetPixelShader(shader_id, D2D1_PIXEL_OPTIONS_NONE);
    }
    // Every output pixel may sample anywhere in the frame (the bend): each input is needed whole.
    IFACEMETHODIMP MapOutputRectToInputRects(const D2D1_RECT_L*, D2D1_RECT_L* inputs, UINT32 count) const override {
        for (UINT32 i = 0; i < count && i < 2; ++i) inputs[i] = inputs_[i];
        return S_OK;
    }
    IFACEMETHODIMP MapInputRectsToOutputRect(const D2D1_RECT_L* inputs, const D2D1_RECT_L*, UINT32 count,
                                             D2D1_RECT_L* output, D2D1_RECT_L* opaque) override {
        for (UINT32 i = 0; i < count && i < 2; ++i) inputs_[i] = inputs[i];
        *output = inputs_[0];
        *opaque = inputs_[0]; // Every pixel is written opaque.
        return S_OK;
    }
    IFACEMETHODIMP MapInvalidRect(UINT32, D2D1_RECT_L, D2D1_RECT_L* invalid) const override {
        *invalid = inputs_[0]; // A change anywhere can move any pixel.
        return S_OK;
    }
    IFACEMETHODIMP_(UINT32) GetInputCount() const override { return 2; }

private:
    std::atomic<ULONG> refs_{1};
    ComPtr<ID2D1DrawInfo> info_;
    D2D1_RECT_L inputs_[2]{};
};

Effect* Effect::last_ = nullptr;

constexpr wchar_t effect_xml[] = LR"(<?xml version='1.0'?>
<Effect>
    <Property name='DisplayName' type='string' value='launchme CRT'/>
    <Property name='Author' type='string' value='launchme'/>
    <Property name='Category' type='string' value='Stylize'/>
    <Property name='Description' type='string' value='A CRT screen: scanlines, shadow mask, curvature, bloom and afterglow.'/>
    <Inputs>
        <Input name='Source'/>
        <Input name='History'/>
    </Inputs>
    <Property name='Generation' type='uint32'>
        <Property name='DisplayName' type='string' value='Generation'/>
        <Property name='Default' type='uint32' value='0'/>
    </Property>
</Effect>)";

} // namespace

bool registerEffect(ID2D1Factory1* factory) {
    const D2D1_PROPERTY_BINDING bindings[] = {{L"Generation", &Effect::setGeneration, &Effect::getGeneration}};
    return factory && compiled() && SUCCEEDED(factory->RegisterEffectFromString(effect_id, effect_xml, bindings, 1, &Effect::make));
}

HRESULT create(ID2D1DeviceContext* context, ID2D1Effect** effect, Constants** constants) {
    Effect::last_ = nullptr;
    const HRESULT made = context->CreateEffect(effect_id, effect);
    if (FAILED(made)) return made;
    if (!Effect::last_) { (*effect)->Release(); *effect = nullptr; return E_UNEXPECTED; }
    *constants = &Effect::last_->constants;
    Effect::last_ = nullptr;
    return S_OK;
}

} // namespace crt_filter
#endif
