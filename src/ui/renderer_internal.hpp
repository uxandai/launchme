#pragma once
// Small drawing helpers shared by the Renderer's *_renderer.cpp files: colours, geometry, easing
// and text case. One definition each; a file uses them through `using render_detail::name;` (or
// `using namespace render_detail;`).
#include "renderer.hpp"
#include "../library/emulation_platforms.hpp"

#include <d2d1helper.h>

#include <algorithm>
#include <string>
#include <string_view>

namespace render_detail {
inline D2D1_COLOR_F color(float r, float g, float b, float a = 1.0f) { return D2D1::ColorF(r, g, b, a); }
// A publisher's or developer's tile ("publisher:capcom"): a logo on the stores' dark fill.
inline bool studioPlatform(std::wstring_view platform) { return platform.starts_with(L"publisher:") || platform.starts_with(L"developer:"); }
inline D2D1_COLOR_F platformColor(const std::wstring& platform, float alpha = 1.0f) {
    if (const auto* found = findEmulationPlatform(platform)) return color(found->red, found->green, found->blue, alpha);
    return color(0.18f, 0.25f, 0.33f, alpha);
}
// Smoothstep on 0..1 (clamped): the easing of fades and glides.
inline float smoothFade(float value) {
    const float t = std::clamp(value, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}
// A layout rectangle (settings_ui::Rect, also desktop_ui's) as Direct2D's, optionally moved down.
inline D2D1_RECT_F box(const settings_ui::Rect& r, float dy = 0.0f) { return D2D1::RectF(r.left, r.top + dy, r.right, r.bottom + dy); }
// Uppercase in the user's locale rules (CharUpperBuffW keeps the length): headings and labels.
inline std::wstring upper(std::wstring value) {
    if (!value.empty()) CharUpperBuffW(value.data(), static_cast<DWORD>(value.size()));
    return value;
}
// The same picture, also from another load of the same file (a new shared buffer).
inline bool samePicture(const ImageBytes& a, const ImageBytes& b) {
    return a == b || (a && b && a->size() == b->size() && *a == *b);
}
}
