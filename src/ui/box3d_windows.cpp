// Box3D (box3d.hpp): Windows only; Linux compiles this file empty.
#include "box3d.hpp"
#ifdef _WIN32
#include <d2d1_1helper.h>
#include <d3dcompiler.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace {
// World units are the frame's pixels: x right, y down, z away from the viewer (perspective.hpp's).
// The camera sits `eye` in front of the case's middle; the vertex shader projects by hand.
constexpr char shader_source[] = R"(
cbuffer Constants : register(b0) {
    row_major float4x4 model;
    float4 projection; // x, y: pixels to clip space (with the eye); z, w: depth = z * w' + w
    float4 camera;     // x: eye, y: the lamp's drift
    float4 plastic;
    float4 params;     // x: printed (1) or plain plastic (0), y: gloss
};
Texture2D print_ : register(t0);
SamplerState sampler_ : register(s0);
struct In { float3 position : POSITION; float3 normal : NORMAL; float2 uv : TEXCOORD0; };
struct Out { float4 position : SV_Position; float3 world : TEXCOORD1; float3 normal : TEXCOORD2; float2 uv : TEXCOORD0; };
Out vs(In input) {
    Out output;
    float3 world = mul(float4(input.position, 1), model).xyz;
    float distance = world.z + camera.x;
    output.position = float4(world.x * projection.x, -world.y * projection.y, distance * projection.z + projection.w, distance);
    output.world = world;
    output.normal = mul(float4(input.normal, 0), model).xyz;
    output.uv = input.uv;
    return output;
}
float3 turned(float3 v, float a) { float c = cos(a), s = sin(a); return float3(v.x * c - v.z * s, v.y, v.x * s + v.z * c); }
float4 ps(Out input) : SV_Target {
    float3 n = normalize(input.normal);
    float3 v = normalize(float3(0, 0, -camera.x) - input.world);
    if (dot(n, v) < 0) n = -n;
    float3 base = params.x > 0.5 ? print_.Sample(sampler_, input.uv).rgb : plastic.rgb;
    // A key lamp above and to the left, a softer fill from the right, a cool rim from behind.
    float3 key = normalize(turned(float3(-0.42, -0.70, -0.58), camera.y));
    float3 fill = normalize(turned(float3(0.75, -0.10, -0.65), camera.y));
    float3 rim = normalize(float3(0.25, -0.55, 0.80));
    float lit = 0.46 + 0.56 * saturate(dot(n, key)) + 0.20 * saturate(dot(n, fill));
    float3 colour = base * lit;
    float gloss = params.y;
    // The clear sleeve: a tight highlight of the key lamp, a broad one of the fill, a softbox overhead
    // in the reflection, and the edges brightening at grazing angles.
    float s1 = pow(saturate(dot(n, normalize(key + v))), 80) * 0.55;
    float s2 = pow(saturate(dot(n, normalize(fill + v))), 24) * 0.12;
    float3 r = reflect(-v, n);
    float softbox = smoothstep(0.25, 0.85, -r.y) * smoothstep(-0.9, -0.2, r.x) * 0.10;
    float fresnel = pow(1 - saturate(dot(n, v)), 4) * 0.30;
    colour += gloss * (s1 + s2 + softbox) + gloss * fresnel * float3(0.80, 0.86, 1.0) + saturate(dot(n, rim)) * 0.10 * float3(0.75, 0.82, 1.0);
    return float4(saturate(colour), 1);
}
)";

struct Vertex { float x, y, z, nx, ny, nz, u, v; };
struct Constants {
    float model[16];
    float projection[4];
    float camera[4];
    float plastic[4];
    float params[4];
};

ComPtr<ID3DBlob> compile(const char* entry, const char* target) {
    ComPtr<ID3DBlob> code, errors;
    if (FAILED(D3DCompile(shader_source, sizeof(shader_source) - 1, "box3d", nullptr, nullptr, entry, target,
                          D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, code.GetAddressOf(), errors.GetAddressOf()))) return {};
    return code;
}
}

bool Box3D::supported(ID3D11Device* device) {
    if (!device) return false;
    if (device != device_) { reset(); if (!create(device)) failed_ = true; }
    return !failed_;
}

bool Box3D::create(ID3D11Device* device) {
    device_ = device;
    failed_ = false;
    if (device->GetFeatureLevel() < D3D_FEATURE_LEVEL_10_0) return false;
    ComPtr<ID3D11Device1> device1;
    ComPtr<ID3D11DeviceContext> immediate;
    device->GetImmediateContext(immediate.GetAddressOf());
    if (FAILED(device->QueryInterface(IID_PPV_ARGS(device1.GetAddressOf()))) || FAILED(immediate.As(&immediate_))) return false;
    // Its own pipeline state: Direct2D's (and video's) are never touched.
    const D3D_FEATURE_LEVEL level = device->GetFeatureLevel();
    if (FAILED(device1->CreateDeviceContextState(0, &level, 1, D3D11_SDK_VERSION, __uuidof(ID3D11Device1), nullptr, state_.GetAddressOf()))) return false;
    const auto vs = compile("vs", "vs_4_0"), ps = compile("ps", "ps_4_0");
    if (!vs || !ps ||
        FAILED(device->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, vertex_shader_.GetAddressOf())) ||
        FAILED(device->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, pixel_shader_.GetAddressOf()))) return false;
    const D3D11_INPUT_ELEMENT_DESC elements[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    if (FAILED(device->CreateInputLayout(elements, 3, vs->GetBufferPointer(), vs->GetBufferSize(), layout_.GetAddressOf()))) return false;
    D3D11_BUFFER_DESC constants{};
    constants.ByteWidth = sizeof(Constants);
    constants.Usage = D3D11_USAGE_DEFAULT;
    constants.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    if (FAILED(device->CreateBuffer(&constants, nullptr, constants_.GetAddressOf()))) return false;
    D3D11_SAMPLER_DESC sampler{};
    sampler.Filter = D3D11_FILTER_ANISOTROPIC;
    sampler.MaxAnisotropy = 8;
    sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler.MaxLOD = D3D11_FLOAT32_MAX;
    if (FAILED(device->CreateSamplerState(&sampler, sampler_.GetAddressOf()))) return false;
    // A closed shape: both sides are drawn and the depth test sorts them (no winding to keep).
    D3D11_RASTERIZER_DESC rasterizer{};
    rasterizer.FillMode = D3D11_FILL_SOLID;
    rasterizer.CullMode = D3D11_CULL_NONE;
    rasterizer.DepthClipEnable = TRUE;
    rasterizer.MultisampleEnable = TRUE;
    rasterizer.AntialiasedLineEnable = FALSE;
    if (FAILED(device->CreateRasterizerState(&rasterizer, rasterizer_.GetAddressOf()))) return false;
    D3D11_DEPTH_STENCIL_DESC depth{};
    depth.DepthEnable = TRUE;
    depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    depth.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
    if (FAILED(device->CreateDepthStencilState(&depth, depth_state_.GetAddressOf()))) return false;
    D3D11_BLEND_DESC blend{};
    blend.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    if (FAILED(device->CreateBlendState(&blend, blend_.GetAddressOf()))) return false;
    return true;
}

void Box3D::reset() {
    bitmap_.Reset();
    front_view_.Reset(); back_view_.Reset(); spine_view_.Reset();
    front_bitmap_ = back_bitmap_ = spine_bitmap_ = nullptr;
    target_view_.Reset(); depth_view_.Reset();
    target_.Reset(); depth_.Reset(); resolved_.Reset();
    vertices_.Reset(); indices_.Reset();
    constants_.Reset(); sampler_.Reset(); rasterizer_.Reset(); depth_state_.Reset(); blend_.Reset();
    layout_.Reset(); vertex_shader_.Reset(); pixel_shader_.Reset();
    state_.Reset(); immediate_.Reset();
    device_ = nullptr;
    failed_ = false;
    width_ = height_ = 0;
    samples_ = 1;
    std::memset(mesh_key_, 0, sizeof(mesh_key_));
    drawn_valid_ = false;
}

bool Box3D::frame(uint32_t width, uint32_t height, ID2D1DeviceContext* context) {
    if (bitmap_ && width == width_ && height == height_) return true;
    bitmap_.Reset(); target_view_.Reset(); depth_view_.Reset(); target_.Reset(); depth_.Reset(); resolved_.Reset();
    drawn_valid_ = false;
    width_ = width; height_ = height;
    constexpr DXGI_FORMAT format = DXGI_FORMAT_B8G8R8A8_UNORM;
    samples_ = 1;
    for (const UINT count : {8u, 4u, 2u}) {
        UINT quality = 0;
        if (SUCCEEDED(device_->CheckMultisampleQualityLevels(format, count, &quality)) && quality > 0) {
            UINT depth_quality = 0;
            if (SUCCEEDED(device_->CheckMultisampleQualityLevels(DXGI_FORMAT_D24_UNORM_S8_UINT, count, &depth_quality)) && depth_quality > 0) { samples_ = count; break; }
        }
    }
    D3D11_TEXTURE2D_DESC description{};
    description.Width = width; description.Height = height;
    description.MipLevels = 1; description.ArraySize = 1;
    description.Format = format;
    description.SampleDesc = {1, 0};
    description.Usage = D3D11_USAGE_DEFAULT;
    description.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(device_->CreateTexture2D(&description, nullptr, resolved_.GetAddressOf()))) return false;
    if (samples_ > 1) {
        description.SampleDesc = {samples_, 0};
        description.BindFlags = D3D11_BIND_RENDER_TARGET;
        if (FAILED(device_->CreateTexture2D(&description, nullptr, target_.GetAddressOf()))) return false;
    } else {
        target_ = resolved_;
    }
    if (FAILED(device_->CreateRenderTargetView(target_.Get(), nullptr, target_view_.GetAddressOf()))) return false;
    description.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    description.SampleDesc = {samples_, 0};
    description.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    if (FAILED(device_->CreateTexture2D(&description, nullptr, depth_.GetAddressOf())) ||
        FAILED(device_->CreateDepthStencilView(depth_.Get(), nullptr, depth_view_.GetAddressOf()))) return false;
    ComPtr<IDXGISurface> surface;
    const auto properties = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_NONE, D2D1::PixelFormat(format, D2D1_ALPHA_MODE_PREMULTIPLIED), 96, 96);
    if (FAILED(resolved_.As(&surface)) || FAILED(context->CreateBitmapFromDxgiSurface(surface.Get(), &properties, bitmap_.GetAddressOf()))) {
        bitmap_.Reset();
        return false;
    }
    return true;
}

bool Box3D::mesh(const Pose& pose) {
    const float key[5]{pose.width, pose.height, pose.depth, pose.corner, pose.bevel};
    if (vertices_ && std::equal(std::begin(key), std::end(key), std::begin(mesh_key_))) return true;
    std::copy(std::begin(key), std::end(key), std::begin(mesh_key_));
    vertices_.Reset(); indices_.Reset();
    const float hw = pose.width * 0.5f, hh = pose.height * 0.5f, hd = pose.depth * 0.5f;
    const float bevel = std::clamp(pose.bevel, 0.0f, std::min({hd * 0.9f, hw * 0.3f, hh * 0.3f}));
    const float corner = std::clamp(pose.corner, bevel + 0.01f, std::min(hw, hh) * 0.5f);
    // The front's outline: four quarter circles round inner centres, joined by the straight edges.
    constexpr int arc = 8, steps = 6; // Segments a corner; a bevel's steps.
    struct Edge { float cx, cy, dx, dy; };
    std::vector<Edge> outline;
    const float cxs[4]{hw - corner, -(hw - corner), -(hw - corner), hw - corner};
    const float cys[4]{-(hh - corner), -(hh - corner), hh - corner, hh - corner};
    for (int k = 0; k < 4; ++k)
        for (int s = 0; s <= arc; ++s) {
            const float a = -(static_cast<float>(k) + static_cast<float>(s) / arc) * 1.57079633f;
            outline.push_back({cxs[k], cys[k], std::cos(a), std::sin(a)});
        }
    const size_t count = outline.size();
    // The spine: the straight left edge, from the top-left corner's end to the bottom-left one's start.
    const size_t spine_from = 1 * (arc + 1) + arc, spine_to = 2 * (arc + 1);
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices, spine_indices, plastic_indices;
    const auto point = [&](const Edge& e, float radius, float z, float out, float nz, bool back) {
        const float x = e.cx + e.dx * radius, y = e.cy + e.dy * radius;
        const float u = back ? (hw - x) / pose.width : (x + hw) / pose.width;
        return Vertex{x, y, z, e.dx * out, e.dy * out, nz, u, (y + hh) / pose.height};
    };
    // One face (front or back): its flat cap and the rounded bevel down to the sides.
    const auto face = [&](bool back) {
        const float sign = back ? 1.0f : -1.0f;
        const auto first = static_cast<uint32_t>(vertices.size());
        vertices.push_back({0, 0, sign * hd, 0, 0, sign, 0.5f, 0.5f});
        std::vector<uint32_t> rings;
        for (int j = 0; j <= steps; ++j) {
            const float theta = static_cast<float>(j) / steps * 1.57079633f;
            const float radius = corner - bevel + bevel * std::sin(theta);
            const float z = sign * (hd - bevel + bevel * std::cos(theta));
            rings.push_back(static_cast<uint32_t>(vertices.size()));
            for (const auto& e : outline) vertices.push_back(point(e, radius, z, std::sin(theta), sign * std::cos(theta), back));
        }
        for (size_t i = 0; i < count; ++i) {
            const auto next = static_cast<uint32_t>((i + 1) % count);
            indices.insert(indices.end(), {first, rings[0] + static_cast<uint32_t>(i), rings[0] + next});
        }
        for (int j = 0; j < steps; ++j)
            for (size_t i = 0; i < count; ++i) {
                const auto a = rings[j] + static_cast<uint32_t>(i), b = rings[j] + static_cast<uint32_t>((i + 1) % count);
                const auto c = rings[j + 1] + static_cast<uint32_t>(i), d = rings[j + 1] + static_cast<uint32_t>((i + 1) % count);
                indices.insert(indices.end(), {a, b, c, b, d, c});
            }
    };
    face(false);
    front_ = {0, static_cast<uint32_t>(indices.size())};
    face(true);
    back_ = {front_.count, static_cast<uint32_t>(indices.size()) - front_.count};
    // The sides between the two bevels; the spine's strip printed, the whole label over the flat side
    // (across: from the front's edge, as the shelf prints it; down: from the top corner's end to the bottom one's).
    for (size_t i = 0; i < count; ++i) {
        const auto& e0 = outline[i];
        const auto& e1 = outline[(i + 1) % count];
        const bool spine = i == spine_from && (i + 1) % count == spine_to;
        const auto base = static_cast<uint32_t>(vertices.size());
        const float z0 = -(hd - bevel), z1 = hd - bevel;
        for (const auto* e : {&e0, &e1})
            for (const float z : {z0, z1}) {
                auto v = point(*e, corner, z, 1, 0, false);
                if (spine) { v.u = (z - z0) / (z1 - z0); v.v = (v.y + (hh - corner)) / (2 * (hh - corner)); }
                vertices.push_back(v);
            }
        auto& list = spine ? spine_indices : plastic_indices;
        list.insert(list.end(), {base, base + 1, base + 2, base + 1, base + 3, base + 2});
    }
    spine_ = {static_cast<uint32_t>(indices.size()), static_cast<uint32_t>(spine_indices.size())};
    indices.insert(indices.end(), spine_indices.begin(), spine_indices.end());
    plastic_ = {static_cast<uint32_t>(indices.size()), static_cast<uint32_t>(plastic_indices.size())};
    indices.insert(indices.end(), plastic_indices.begin(), plastic_indices.end());

    D3D11_BUFFER_DESC description{};
    description.Usage = D3D11_USAGE_IMMUTABLE;
    description.ByteWidth = static_cast<UINT>(vertices.size() * sizeof(Vertex));
    description.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA data{vertices.data(), 0, 0};
    if (FAILED(device_->CreateBuffer(&description, &data, vertices_.GetAddressOf()))) return false;
    description.ByteWidth = static_cast<UINT>(indices.size() * sizeof(uint32_t));
    description.BindFlags = D3D11_BIND_INDEX_BUFFER;
    data.pSysMem = indices.data();
    if (FAILED(device_->CreateBuffer(&description, &data, indices_.GetAddressOf()))) { vertices_.Reset(); return false; }
    return true;
}

ComPtr<ID3D11ShaderResourceView> Box3D::view(ID2D1Bitmap1* bitmap, ComPtr<ID3D11ShaderResourceView>& cached, ID2D1Bitmap1*& cached_bitmap) {
    if (!bitmap) { cached.Reset(); cached_bitmap = nullptr; return {}; }
    if (cached && cached_bitmap == bitmap) return cached;
    cached.Reset(); cached_bitmap = nullptr;
    ComPtr<IDXGISurface> surface;
    ComPtr<ID3D11Texture2D> texture;
    if (FAILED(bitmap->GetSurface(surface.GetAddressOf())) || FAILED(surface.As(&texture)) ||
        FAILED(device_->CreateShaderResourceView(texture.Get(), nullptr, cached.GetAddressOf()))) { cached.Reset(); return {}; }
    cached_bitmap = bitmap;
    return cached;
}

ID2D1Bitmap1* Box3D::render(ID3D11Device* device, ID2D1DeviceContext* context, const Faces& faces, const Pose& pose,
                            uint32_t frame_width, uint32_t frame_height, uint64_t faces_version) {
    if (!context || !frame_width || !frame_height || !supported(device)) return nullptr;
    if (!frame(frame_width, frame_height, context) || !mesh(pose)) return nullptr;
    const auto front = view(faces.front, front_view_, front_bitmap_);
    const auto back = view(faces.back, back_view_, back_bitmap_);
    const auto spine = view(faces.spine, spine_view_, spine_bitmap_);
    if (drawn_valid_ && faces_version == drawn_faces_ && std::memcmp(&pose, &drawn_, sizeof(Pose)) == 0) return bitmap_.Get();

    // The faces were just drawn by Direct2D: its work goes to the device first.
    context->Flush();
    ComPtr<ID3DDeviceContextState> previous;
    immediate_->SwapDeviceContextState(state_.Get(), previous.GetAddressOf());
    const float clear[4]{0, 0, 0, 0};
    immediate_->ClearRenderTargetView(target_view_.Get(), clear);
    immediate_->ClearDepthStencilView(depth_view_.Get(), D3D11_CLEAR_DEPTH, 1, 0);
    immediate_->OMSetRenderTargets(1, target_view_.GetAddressOf(), depth_view_.Get());
    immediate_->OMSetDepthStencilState(depth_state_.Get(), 0);
    immediate_->OMSetBlendState(blend_.Get(), nullptr, 0xffffffff);
    const D3D11_VIEWPORT viewport{0, 0, static_cast<float>(frame_width), static_cast<float>(frame_height), 0, 1};
    immediate_->RSSetViewports(1, &viewport);
    immediate_->RSSetState(rasterizer_.Get());
    const UINT stride = sizeof(Vertex), offset = 0;
    immediate_->IASetVertexBuffers(0, 1, vertices_.GetAddressOf(), &stride, &offset);
    immediate_->IASetIndexBuffer(indices_.Get(), DXGI_FORMAT_R32_UINT, 0);
    immediate_->IASetInputLayout(layout_.Get());
    immediate_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    immediate_->VSSetShader(vertex_shader_.Get(), nullptr, 0);
    immediate_->PSSetShader(pixel_shader_.Get(), nullptr, 0);
    immediate_->VSSetConstantBuffers(0, 1, constants_.GetAddressOf());
    immediate_->PSSetConstantBuffers(0, 1, constants_.GetAddressOf());
    immediate_->PSSetSamplers(0, 1, sampler_.GetAddressOf());

    Constants constants{};
    // Turned round its upright axis, then tipped towards or away from the viewer; scaled so the front
    // at rest projects to exactly width x height.
    const float eye = std::max(pose.eye, pose.height);
    const float scale = eye / (eye + pose.depth * 0.5f);
    const float cy = std::cos(pose.yaw), sy = std::sin(pose.yaw), cp = std::cos(pose.pitch), sp = std::sin(pose.pitch);
    // Row vectors: p' = p * Ry * Rx.
    const float m[16] = {
        scale * cy, scale * (sy * sp), scale * (sy * cp), 0,
        0, scale * cp, scale * -sp, 0,
        scale * -sy, scale * (cy * sp), scale * (cy * cp), 0,
        0, 0, 0, 1,
    };
    std::memcpy(constants.model, m, sizeof(m));
    const float radius = 0.5f * std::sqrt(pose.width * pose.width + pose.height * pose.height + pose.depth * pose.depth);
    const float near_plane = std::max(1.0f, eye - radius * 1.3f), far_plane = eye + radius * 1.3f;
    constants.projection[0] = eye / (frame_width * 0.5f);
    constants.projection[1] = eye / (frame_height * 0.5f);
    constants.projection[2] = far_plane / (far_plane - near_plane);
    constants.projection[3] = -near_plane * far_plane / (far_plane - near_plane);
    constants.camera[0] = eye;
    constants.camera[1] = pose.light;
    std::copy(std::begin(pose.plastic), std::end(pose.plastic), constants.plastic);
    constants.plastic[3] = 1;
    const auto draw = [&](const Part& part, ID3D11ShaderResourceView* print) {
        if (!part.count) return;
        constants.params[0] = print ? 1.0f : 0.0f;
        constants.params[1] = print ? pose.gloss : pose.gloss * 0.6f;
        immediate_->UpdateSubresource(constants_.Get(), 0, nullptr, &constants, 0, 0);
        immediate_->PSSetShaderResources(0, 1, &print);
        immediate_->DrawIndexed(part.count, part.first, 0);
    };
    draw(front_, front.Get());
    draw(back_, back.Get());
    draw(spine_, spine.Get());
    draw(plastic_, nullptr);
    ID3D11ShaderResourceView* none = nullptr;
    immediate_->PSSetShaderResources(0, 1, &none);
    immediate_->OMSetRenderTargets(0, nullptr, nullptr);
    if (samples_ > 1) immediate_->ResolveSubresource(resolved_.Get(), 0, target_.Get(), 0, DXGI_FORMAT_B8G8R8A8_UNORM);
    immediate_->SwapDeviceContextState(previous.Get(), nullptr);
    drawn_ = pose;
    drawn_faces_ = faces_version;
    drawn_valid_ = true;
    return bitmap_.Get();
}
#endif
