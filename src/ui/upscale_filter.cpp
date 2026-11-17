// Tryb TV's background upscaler on Windows: a two-pass Direct2D custom effect (EASU -> RCAS)
// implementing AMD FidelityFX Super Resolution 1.0 (MIT License, Copyright (c) 2021 Advanced Micro
// Devices, Inc.) adapted to Direct2D's ps_4_0 effect pipeline.
//
// Pass 1 (EASU): samples a 12-texel cross of the source bitmap around the mapped subpixel position,
// estimates local edge direction and anisotropy, and evaluates an elliptically stretched Lanczos2
// kernel clamped to the local 2x2 neighbourhood (deringing).
// Pass 2 (RCAS): samples a 5-pixel cross of the EASU output and applies contrast-adaptive sharpening
// with high-frequency noise attenuation (suppressing JPEG block ringing on flat sky gradients).
#ifdef _WIN32
#include "upscale_filter.hpp"

#include <d2d1effectauthor.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <mutex>
#include <vector>

namespace upscale_filter {
namespace {

using Microsoft::WRL::ComPtr;

// {9B4E71D2-6C38-4A19-B52E-8F0D3A91C47E}
constexpr GUID effect_id = {0x9b4e71d2, 0x6c38, 0x4a19, {0xb5, 0x2e, 0x8f, 0x0d, 0x3a, 0x91, 0xc4, 0x7e}};
// {3E81A5C4-91F2-4D67-82B0-5C7E19F4A201}
constexpr GUID easu_shader_id = {0x3e81a5c4, 0x91f2, 0x4d67, {0x82, 0xb0, 0x5c, 0x7e, 0x19, 0xf4, 0xa2, 0x01}};
// {7F24D8B9-15A3-4E08-9C64-2B81E0D5F312}
constexpr GUID rcas_shader_id = {0x7f24d8b9, 0x15a3, 0x4e08, {0x9c, 0x64, 0x2b, 0x81, 0xe0, 0xd5, 0xf3, 0x12}};

constexpr char easu_shader_source[] = R"(
Texture2D Source : register(t0);
SamplerState SourceSampler : register(s0);
cbuffer Constants : register(b0) {
    float2 src_size; float2 out_size;
    float2 crop_origin; float2 crop_size;
    float rcas_con; float opacity; float2 pad;
};

static float4 here;
static float2 origin;

float3 fetch(float2 texel) {
    float2 p = clamp(texel + 0.5, 0.5, src_size - 0.5);
    return Source.SampleLevel(SourceSampler, here.xy + (p - origin) * here.zw, 0).rgb;
}

float luma(float3 c) { return 0.5 * c.b + (0.5 * c.r + c.g); }

void easuSet(inout float2 dir, inout float len, float2 pp, bool biS, bool biT, bool biU, bool biV,
             float lA, float lB, float lC, float lD, float lE) {
    float w = 0.0;
    if (biS) w = (1.0 - pp.x) * (1.0 - pp.y);
    if (biT) w = pp.x * (1.0 - pp.y);
    if (biU) w = (1.0 - pp.x) * pp.y;
    if (biV) w = pp.x * pp.y;
    float dc = lD - lC;
    float cb = lC - lB;
    float lenX = 1.0 / max(abs(dc), max(abs(cb), 1e-5));
    float dirX = lD - lB;
    dir.x += dirX * w;
    lenX = saturate(abs(dirX) * lenX);
    lenX *= lenX;
    len += lenX * w;
    float ec = lE - lC;
    float ca = lC - lA;
    float lenY = 1.0 / max(abs(ec), max(abs(ca), 1e-5));
    float dirY = lE - lA;
    dir.y += dirY * w;
    lenY = saturate(abs(dirY) * lenY);
    lenY *= lenY;
    len += lenY * w;
}

void easuTap(inout float3 aC, inout float aW, float2 off, float2 dir, float2 len2, float lob, float clp, float3 c) {
    float2 v;
    v.x = (off.x * dir.x) + (off.y * dir.y);
    v.y = (off.x * (-dir.y)) + (off.y * dir.x);
    v *= len2;
    float d2 = min(dot(v, v), clp);
    float wB = (2.0 / 5.0) * d2 - 1.0;
    float wA = lob * d2 - 1.0;
    wB *= wB;
    wA *= wA;
    wB = (25.0 / 16.0) * wB - (25.0 / 16.0 - 1.0);
    float w = wB * wA;
    aC += c * w;
    aW += w;
}

float4 main(float4 position : SV_POSITION, float4 scene : SCENE_POSITION, float4 uv0 : TEXCOORD0) : SV_TARGET {
    here = uv0;
    origin = scene.xy;
    float2 pp = crop_origin + (scene.xy / max(out_size, 1.0)) * crop_size - 0.5;
    float2 fp = floor(pp);
    pp -= fp;

    float3 b = fetch(fp + float2( 0.0, -1.0));
    float3 c = fetch(fp + float2( 1.0, -1.0));
    float3 e = fetch(fp + float2(-1.0,  0.0));
    float3 f = fetch(fp + float2( 0.0,  0.0));
    float3 g = fetch(fp + float2( 1.0,  0.0));
    float3 h = fetch(fp + float2( 2.0,  0.0));
    float3 i = fetch(fp + float2(-1.0,  1.0));
    float3 j = fetch(fp + float2( 0.0,  1.0));
    float3 k = fetch(fp + float2( 1.0,  1.0));
    float3 l = fetch(fp + float2( 2.0,  1.0));
    float3 n = fetch(fp + float2( 0.0,  2.0));
    float3 o = fetch(fp + float2( 1.0,  2.0));

    float bL = luma(b), cL = luma(c);
    float eL = luma(e), fL = luma(f), gL = luma(g), hL = luma(h);
    float iL = luma(i), jL = luma(j), kL = luma(k), lL = luma(l);
    float nL = luma(n), oL = luma(o);

    float2 dir = float2(0.0, 0.0);
    float len = 0.0;
    easuSet(dir, len, pp, true,  false, false, false, bL, eL, fL, gL, jL);
    easuSet(dir, len, pp, false, true,  false, false, cL, fL, gL, hL, kL);
    easuSet(dir, len, pp, false, false, true,  false, fL, iL, jL, kL, nL);
    easuSet(dir, len, pp, false, false, false, true,  gL, jL, kL, lL, oL);

    float dirR = dot(dir, dir);
    if (dirR < (1.0 / 32768.0)) dir = float2(1.0, 0.0);
    else dir *= rsqrt(dirR);

    len *= 0.5;
    len *= len;
    float stretch = dot(dir, dir) / max(abs(dir.x), abs(dir.y));
    float2 len2 = float2(1.0 + (stretch - 1.0) * len, 1.0 - 0.5 * len);
    float lob = 0.5 + ((0.25 - 0.04) - 0.5) * len;
    float clp = 1.0 / lob;

    float3 min4 = min(min(f, g), min(j, k));
    float3 max4 = max(max(f, g), max(j, k));

    float3 aC = float3(0.0, 0.0, 0.0);
    float aW = 0.0;
    easuTap(aC, aW, float2( 0.0, -1.0) - pp, dir, len2, lob, clp, b);
    easuTap(aC, aW, float2( 1.0, -1.0) - pp, dir, len2, lob, clp, c);
    easuTap(aC, aW, float2(-1.0,  0.0) - pp, dir, len2, lob, clp, e);
    easuTap(aC, aW, float2( 0.0,  0.0) - pp, dir, len2, lob, clp, f);
    easuTap(aC, aW, float2( 1.0,  0.0) - pp, dir, len2, lob, clp, g);
    easuTap(aC, aW, float2( 2.0,  0.0) - pp, dir, len2, lob, clp, h);
    easuTap(aC, aW, float2(-1.0,  1.0) - pp, dir, len2, lob, clp, i);
    easuTap(aC, aW, float2( 0.0,  1.0) - pp, dir, len2, lob, clp, j);
    easuTap(aC, aW, float2( 1.0,  1.0) - pp, dir, len2, lob, clp, k);
    easuTap(aC, aW, float2( 2.0,  1.0) - pp, dir, len2, lob, clp, l);
    easuTap(aC, aW, float2( 0.0,  2.0) - pp, dir, len2, lob, clp, n);
    easuTap(aC, aW, float2( 1.0,  2.0) - pp, dir, len2, lob, clp, o);

    float3 color = clamp(aC / max(aW, 1e-5), min4, max4);
    return float4(saturate(color), 1.0);
}
)";

constexpr char rcas_shader_source[] = R"(
Texture2D Source : register(t0);
SamplerState SourceSampler : register(s0);
cbuffer Constants : register(b0) {
    float2 src_size; float2 out_size;
    float2 crop_origin; float2 crop_size;
    float rcas_con; float opacity; float2 pad;
};

static float4 here;
static float2 origin;

float3 fetch(float2 p) {
    p = clamp(p, 0.5, out_size - 0.5);
    return Source.SampleLevel(SourceSampler, here.xy + (p - origin) * here.zw, 0).rgb;
}

float4 main(float4 position : SV_POSITION, float4 scene : SCENE_POSITION, float4 uv0 : TEXCOORD0) : SV_TARGET {
    here = uv0;
    origin = scene.xy;

    float3 b = fetch(scene.xy + float2( 0.0, -1.0));
    float3 d = fetch(scene.xy + float2(-1.0,  0.0));
    float3 e = fetch(scene.xy);
    float3 f = fetch(scene.xy + float2( 1.0,  0.0));
    float3 h = fetch(scene.xy + float2( 0.0,  1.0));

    float bL = 0.5 * b.b + (0.5 * b.r + b.g);
    float dL = 0.5 * d.b + (0.5 * d.r + d.g);
    float eL = 0.5 * e.b + (0.5 * e.r + e.g);
    float fL = 0.5 * f.b + (0.5 * f.r + f.g);
    float hL = 0.5 * h.b + (0.5 * h.r + h.g);

    float nz = 0.25 * (bL + dL + fL + hL) - eL;
    float rangeL = max(max(eL, max(bL, dL)), max(fL, hL)) - min(min(eL, min(bL, dL)), min(fL, hL));
    nz = saturate(abs(nz) / max(rangeL, 1e-5));
    nz = 1.0 - 0.5 * nz;

    float3 mn4 = min(min(b, d), min(f, h));
    float3 mx4 = max(max(b, d), max(f, h));
    float3 hitMin = min(mn4, e) / (4.0 * mx4 + 1e-5);
    float3 hitMax = (1.0 - max(mx4, e)) / (4.0 * mn4 - 4.0 - 1e-5);
    float3 lobeRGB = max(-hitMin, hitMax);
    float lobe = max(-0.25 + (1.0 / 16.0), min(max(lobeRGB.r, max(lobeRGB.g, lobeRGB.b)), 0.0)) * rcas_con * nz;

    float3 pix = saturate((lobe * (b + d + f + h) + e) / (4.0 * lobe + 1.0));
    float a = saturate(opacity);
    return float4(pix * a, a);
}
)";

std::mutex bytecode_lock;
std::vector<char> easu_bytecode, rcas_bytecode;

bool compileShader(const char* source, size_t length, const char* name, std::vector<char>& out) {
    if (!out.empty()) return true;
    ComPtr<ID3DBlob> code, errors;
    if (FAILED(D3DCompile(source, length, name, nullptr, nullptr, "main", "ps_4_0",
                          D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, code.GetAddressOf(), errors.GetAddressOf())))
        return false;
    const auto* bytes = static_cast<const char*>(code->GetBufferPointer());
    out.assign(bytes, bytes + code->GetBufferSize());
    return true;
}

bool compiled() {
    std::lock_guard lock(bytecode_lock);
    return compileShader(easu_shader_source, sizeof(easu_shader_source) - 1, "fsr1_easu", easu_bytecode) &&
           compileShader(rcas_shader_source, sizeof(rcas_shader_source) - 1, "fsr1_rcas", rcas_bytecode);
}

class PassNode final : public ID2D1DrawTransform {
public:
    PassNode(const Constants* constants, const GUID& shader, bool easu)
        : constants_(constants), shader_(shader), easu_(easu) {}

    HRESULT uploadConstants() {
        return info_ ? info_->SetPixelShaderConstantBuffer(reinterpret_cast<const BYTE*>(constants_), sizeof(Constants)) : E_FAIL;
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
        if (id == __uuidof(ID2D1DrawTransform) || id == __uuidof(IUnknown)) *out = static_cast<ID2D1DrawTransform*>(this);
        else if (id == __uuidof(ID2D1Transform)) *out = static_cast<ID2D1Transform*>(this);
        else if (id == __uuidof(ID2D1TransformNode)) *out = static_cast<ID2D1TransformNode*>(this);
        else { *out = nullptr; return E_NOINTERFACE; }
        AddRef();
        return S_OK;
    }

    // ID2D1DrawTransform
    IFACEMETHODIMP SetDrawInfo(ID2D1DrawInfo* info) override {
        info_ = info;
        info->SetInputDescription(0, {D2D1_FILTER_MIN_MAG_MIP_POINT, 0});
        return info->SetPixelShader(shader_, D2D1_PIXEL_OPTIONS_NONE);
    }
    IFACEMETHODIMP MapOutputRectToInputRects(const D2D1_RECT_L*, D2D1_RECT_L* inputs, UINT32 count) const override {
        if (count >= 1) inputs[0] = input_;
        return S_OK;
    }
    IFACEMETHODIMP MapInputRectsToOutputRect(const D2D1_RECT_L* inputs, const D2D1_RECT_L*, UINT32 count,
                                             D2D1_RECT_L* output, D2D1_RECT_L* opaque) override {
        if (count >= 1) input_ = inputs[0];
        if (easu_) {
            const LONG w = std::max<LONG>(1, static_cast<LONG>(std::lround(constants_->out_width)));
            const LONG h = std::max<LONG>(1, static_cast<LONG>(std::lround(constants_->out_height)));
            output_ = {0, 0, w, h};
            *output = output_;
            *opaque = output_;
        } else {
            output_ = input_;
            *output = output_;
            *opaque = constants_->opacity >= 0.999f ? output_ : D2D1_RECT_L{0, 0, 0, 0};
        }
        return S_OK;
    }
    IFACEMETHODIMP MapInvalidRect(UINT32, D2D1_RECT_L, D2D1_RECT_L* invalid) const override {
        *invalid = output_;
        return S_OK;
    }
    IFACEMETHODIMP_(UINT32) GetInputCount() const override { return 1; }

private:
    std::atomic<ULONG> refs_{1};
    const Constants* constants_;
    GUID shader_;
    bool easu_;
    ComPtr<ID2D1DrawInfo> info_;
    D2D1_RECT_L input_{}, output_{};
};

class Effect final : public ID2D1EffectImpl {
public:
    Constants constants;

    static HRESULT __stdcall make(IUnknown** effect) {
        auto* created = new (std::nothrow) Effect();
        if (!created) return E_OUTOFMEMORY;
        *effect = static_cast<ID2D1EffectImpl*>(created);
        last_ = created;
        return S_OK;
    }
    static Effect* last_;

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
        else { *out = nullptr; return E_NOINTERFACE; }
        AddRef();
        return S_OK;
    }

    // ID2D1EffectImpl
    IFACEMETHODIMP Initialize(ID2D1EffectContext* context, ID2D1TransformGraph* graph) override {
        if (!compiled()) return E_FAIL;
        HRESULT hr = context->LoadPixelShader(easu_shader_id, reinterpret_cast<const BYTE*>(easu_bytecode.data()), static_cast<UINT32>(easu_bytecode.size()));
        if (FAILED(hr)) return hr;
        hr = context->LoadPixelShader(rcas_shader_id, reinterpret_cast<const BYTE*>(rcas_bytecode.data()), static_cast<UINT32>(rcas_bytecode.size()));
        if (FAILED(hr)) return hr;
        easu_.Attach(new (std::nothrow) PassNode(&constants, easu_shader_id, true));
        rcas_.Attach(new (std::nothrow) PassNode(&constants, rcas_shader_id, false));
        if (!easu_ || !rcas_) return E_OUTOFMEMORY;
        hr = graph->AddNode(easu_.Get());
        if (FAILED(hr)) return hr;
        hr = graph->AddNode(rcas_.Get());
        if (FAILED(hr)) return hr;
        hr = graph->ConnectToEffectInput(0, easu_.Get(), 0);
        if (FAILED(hr)) return hr;
        hr = graph->ConnectNode(easu_.Get(), rcas_.Get(), 0);
        if (FAILED(hr)) return hr;
        return graph->SetOutputNode(rcas_.Get());
    }
    IFACEMETHODIMP PrepareForRender(D2D1_CHANGE_TYPE) override {
        if (!easu_ || !rcas_) return E_FAIL;
        const HRESULT hr = easu_->uploadConstants();
        return SUCCEEDED(hr) ? rcas_->uploadConstants() : hr;
    }
    IFACEMETHODIMP SetGraph(ID2D1TransformGraph*) override { return E_NOTIMPL; }

private:
    std::atomic<ULONG> refs_{1};
    ComPtr<PassNode> easu_, rcas_;
};

Effect* Effect::last_ = nullptr;

constexpr wchar_t effect_xml[] = LR"(<?xml version='1.0'?>
<Effect>
    <Property name='DisplayName' type='string' value='launchme FSR1 Upscale'/>
    <Property name='Author' type='string' value='launchme'/>
    <Property name='Category' type='string' value='Scale'/>
    <Property name='Description' type='string' value='AMD FSR 1.0 (EASU + RCAS) spatial upscaler for non-4K backgrounds.'/>
    <Inputs>
        <Input name='Source'/>
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
    if (!context || !effect || !constants) return E_INVALIDARG;
    Effect::last_ = nullptr;
    const HRESULT result = context->CreateEffect(effect_id, effect);
    if (FAILED(result)) return result;
    if (!Effect::last_) { (*effect)->Release(); *effect = nullptr; return E_UNEXPECTED; }
    *constants = &Effect::last_->constants;
    Effect::last_ = nullptr;
    return S_OK;
}

} // namespace upscale_filter
#endif
