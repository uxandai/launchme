// A game package's front (package_look.hpp), shared by Półka's shelf and the open case: the plastic
// (or cardboard) outline, the insert under its clear sleeve, the console's band (or stripe, or corner
// tile) with its logo, and the light: a lamp above (brighter top, darker foot), the turn away from it, a
// gloss band and the lit rim.
#include "renderer.hpp"
#include "package_look.hpp"
#include "renderer_internal.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

using Microsoft::WRL::ComPtr;
using palette::gray;

namespace {
ComPtr<ID2D1PathGeometry> closedPath(ID2D1Factory* factory, const std::vector<D2D1_POINT_2F>& points, D2D1_RECT_F& bounds) {
    ComPtr<ID2D1PathGeometry> path;
    ComPtr<ID2D1GeometrySink> sink;
    if (points.size() < 3 || FAILED(factory->CreatePathGeometry(path.GetAddressOf())) || FAILED(path->Open(sink.GetAddressOf()))) return {};
    bounds = {points[0].x, points[0].y, points[0].x, points[0].y};
    for (const auto& p : points) {
        bounds.left = std::min(bounds.left, p.x); bounds.top = std::min(bounds.top, p.y);
        bounds.right = std::max(bounds.right, p.x); bounds.bottom = std::max(bounds.bottom, p.y);
    }
    sink->BeginFigure(points[0], D2D1_FIGURE_BEGIN_FILLED);
    sink->AddLines(points.data() + 1, static_cast<UINT32>(points.size() - 1));
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    return SUCCEEDED(sink->Close()) ? path : ComPtr<ID2D1PathGeometry>{};
}
D2D1_COLOR_F withAlpha(D2D1_COLOR_F colour, float alpha) { colour.a = alpha; return colour; }
}

// A console's cover band: its colour, its logo in the band's ink (the platform's monochrome badge, else
// its name). `ratio`: the band's length : thickness. Across: the logo at its left (or in its middle);
// down (PS1's inlay stripe): the same band turned, the logo at its head reading downwards; square
// (Switch's corner tile): the logo in the middle, a mark (an icon) over the console's name.
ID2D1Bitmap* Renderer::packageBand(const game_case::Kind& kind, const std::wstring& platform, float ratio, package_look::Band shape) {
    using package_look::Band;
    if (!dwrite_ || ratio < 1) return nullptr;
    const auto look = package_look::of(kind);
    const auto found_logo = platform_badges_.find(platform);
    Cover* logo = found_logo != platform_badges_.end() && found_logo->second.bitmap ? &found_logo->second : nullptr;
    const std::wstring key = std::to_wstring(static_cast<int>(kind.style)) + L"\x1f" + platform + (logo ? L"\x1f" L"logo" : L"") + L"\x1f" +
                             std::to_wstring(std::lround(ratio * 100)) + L"\x1f" + std::to_wstring(static_cast<int>(shape));
    if (const auto found = package_bands_.find(key); found != package_bands_.end()) return found->second.Get();
    if (package_bands_.size() >= 24) package_bands_.clear();
    const UINT32 length = shape == Band::square ? 512 : 900;
    const auto thickness = shape == Band::square ? length : std::max<UINT32>(24, static_cast<UINT32>(std::lround(length / ratio)));
    const float w = static_cast<float>(length), h = static_cast<float>(thickness);
    ComPtr<ID2D1BitmapRenderTarget> surface;
    ComPtr<ID2D1SolidColorBrush> ink;
    ComPtr<ID2D1Bitmap> bitmap;
    const auto surface_size = shape == Band::down ? D2D1::SizeU(thickness, length) : D2D1::SizeU(length, thickness);
    if (FAILED(target_->CreateCompatibleRenderTarget(D2D1::SizeF(static_cast<float>(surface_size.width), static_cast<float>(surface_size.height)), surface_size,
                                                     surface.GetAddressOf())) ||
        FAILED(surface->CreateSolidColorBrush(gray(1), ink.GetAddressOf()))) return nullptr;
    surface->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    surface->BeginDraw();
    // A Switch 2 card's band is printed a lighter, rosier red than its box's.
    const auto band_colour = shape == Band::card && kind.style == game_case::Style::switch2 ? D2D1::ColorF(0.87f, 0.17f, 0.21f) : look.band_colour;
    surface->Clear(band_colour);
    // Down the stripe: the band's start (its logo) at the head, reading downwards.
    if (shape == Band::down) surface->SetTransform(D2D1::Matrix3x2F::Rotation(90, {0, 0}) * D2D1::Matrix3x2F::Translation(h, 0));
    // Printed ink is flat; a faint lighter top and a darker foot give the card its gloss lamination
    // (a white band: a soft grey line under it, as the consoles print it).
    if (look.band_gloss) {
        ink->SetColor(gray(0, 0.06f)); surface->FillRectangle(D2D1::RectF(0, h * 0.70f, w, h), ink.Get());
        ink->SetColor(gray(0, 0.22f)); surface->FillRectangle(D2D1::RectF(0, h * 0.93f, w, h), ink.Get());
    } else {
        ink->SetColor(gray(1, 0.07f)); surface->FillRectangle(D2D1::RectF(0, 0, w, h * 0.42f), ink.Get());
        ink->SetColor(gray(0, 0.10f)); surface->FillRectangle(D2D1::RectF(0, h * 0.86f, w, h), ink.Get());
    }
    ink->SetColor(look.band_ink);
    const bool centred = shape == Band::square || shape == Band::card || look.band_centred;
    const float left = w * 0.035f;
    // The console's name (centred: in lines, as printed under the mark).
    const auto name = [&](D2D1_RECT_F box, float size) {
        if (!*look.band_text) return;
        ComPtr<IDWriteTextFormat> format;
        if (!font(format, size, DWRITE_FONT_WEIGHT_BOLD, centred ? DWRITE_TEXT_ALIGNMENT_CENTER : DWRITE_TEXT_ALIGNMENT_LEADING)) return;
        format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        std::wstring label = look.band_text;
        if (shape != Band::square) std::replace(label.begin(), label.end(), L'\n', L' ');
        surface->DrawText(label.c_str(), static_cast<UINT32>(label.size()), format.Get(), box, ink.Get());
    };
    bool drawn = false;
    if (shape == Band::card && logo) {
        // A game card: the console's logo in the band's middle. The Switch card sets its mark beside
        // the wordmark (the tall badge cut in two: the mark above, NINTENDO SWITCH under it); the
        // others show their badge whole.
        const auto pixels = logo->bitmap->GetPixelSize();
        const auto ink_box = logo->has_ink_bounds ? logo->ink_bounds : D2D1::RectF(0, 0, static_cast<float>(pixels.width), static_cast<float>(pixels.height));
        const float iw = ink_box.right - ink_box.left, ih = ink_box.bottom - ink_box.top;
        surface->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
        const auto mask = [&](D2D1_RECT_F destination, D2D1_RECT_F source) {
            surface->FillOpacityMask(logo->bitmap.Get(), ink.Get(), D2D1_OPACITY_MASK_CONTENT_GRAPHICS, &destination, &source);
        };
        if (iw >= 1 && ih >= 1 && kind.style == game_case::Style::switch1 && ih > iw * 0.9f) {
            const auto part = [&](float l, float t, float r, float b) {
                return D2D1::RectF(ink_box.left + iw * l, ink_box.top + ih * t, ink_box.left + iw * r, ink_box.top + ih * b);
            };
            const auto mark = part(0.167f, 0, 0.84f, 0.63f), word = part(0, 0.724f, 1, 1);
            const float mark_h = h * 0.64f, mark_w = mark_h * (mark.right - mark.left) / (mark.bottom - mark.top);
            const float word_h = h * 0.42f, word_w = word_h * (word.right - word.left) / (word.bottom - word.top);
            const float gap = h * 0.16f, x = (w - mark_w - gap - word_w) * 0.5f;
            mask(D2D1::RectF(x, (h - mark_h) * 0.5f, x + mark_w, (h + mark_h) * 0.5f), mark);
            mask(D2D1::RectF(x + mark_w + gap, (h - word_h) * 0.5f, x + mark_w + gap + word_w, (h + word_h) * 0.5f), word);
            drawn = true;
        } else if (iw >= 1 && ih >= 1) {
            const float factor = std::min(h * 0.74f / ih, w * 0.80f / iw);
            const float dw = iw * factor, dh = ih * factor;
            mask(D2D1::RectF((w - dw) * 0.5f, (h - dh) * 0.5f, (w + dw) * 0.5f, (h + dh) * 0.5f), ink_box);
            drawn = true;
        }
        surface->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    }
    if (logo && !drawn && shape != Band::card) {
        const auto pixels = logo->bitmap->GetPixelSize();
        const auto source = logo->has_ink_bounds ? logo->ink_bounds : D2D1::RectF(0, 0, static_cast<float>(pixels.width), static_cast<float>(pixels.height));
        const float ink_w = source.right - source.left, ink_h = source.bottom - source.top;
        if (ink_w >= 1 && ink_h >= 1) {
            // An icon-like mark (about square) in a tile gets the console's name under it.
            const bool icon = shape == Band::square && ink_w / ink_h < 1.6f;
            const float room_w = shape == Band::square ? w * 0.74f : centred ? w * 0.60f : w * 0.46f;
            const float room_h = shape == Band::square ? (icon ? h * 0.42f : h * 0.50f) : centred ? h * 0.62f : h * 0.50f;
            const float factor = std::min(room_h / ink_h, room_w / ink_w);
            const float dw = ink_w * factor, dh = ink_h * factor;
            const float x = centred ? (w - dw) * 0.5f : left;
            const float y = icon ? h * 0.13f + (room_h - dh) * 0.5f : (h - dh) * 0.5f;
            const auto destination = D2D1::RectF(x, y, x + dw, y + dh);
            surface->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
            surface->FillOpacityMask(logo->bitmap.Get(), ink.Get(), D2D1_OPACITY_MASK_CONTENT_GRAPHICS, &destination, &source);
            surface->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
            if (icon) name(D2D1::RectF(0, h * 0.60f, w, h * 0.92f), h * 0.115f);
            drawn = true;
        }
    }
    if (!drawn) {
        if (shape == Band::square) name(D2D1::RectF(0, 0, w, h), h * 0.15f);
        else if (shape == Band::card) name(D2D1::RectF(0, 0, w, h), std::min(h * 0.34f, w * 0.9f / (0.68f * std::max<size_t>(1, std::wcslen(look.band_text)))));
        else name(D2D1::RectF(centred ? 0 : left, 0, w, h), h * 0.40f);
    }
    if (FAILED(surface->EndDraw()) || FAILED(surface->GetBitmap(bitmap.GetAddressOf()))) return nullptr;
    return package_bands_.emplace(key, std::move(bitmap)).first->second.Get();
}

Renderer::Cover* Renderer::packageFrame(const package_look::Frame& frame) {
    const std::wstring key = std::wstring(L"case:") + frame.file;
    if (const auto found = platform_badges_.find(key); found != platform_badges_.end()) {
        auto& cover = found->second;
        if (!cover.bitmap && cover.loaded()) createBitmap(cover); // Picks up its decoded pixels.
        return cover.bitmap ? &cover : nullptr;
    }
    // Beside the program (or in ../share/launchme on Linux). An empty entry: none, not asked again.
    wchar_t program[32768]{};
    GetModuleFileNameW(nullptr, program, static_cast<DWORD>(std::size(program)));
    const auto folder = std::filesystem::path(program).parent_path();
    std::vector<uint8_t> bytes;
    for (const auto& place : {folder / L"assets" / L"cases", folder / L".." / L"share" / L"launchme" / L"assets" / L"cases"}) {
        std::ifstream file(place / frame.file, std::ios::binary);
        if (!file) continue;
        bytes.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
        if (!bytes.empty()) break;
    }
    platform_badges_[key]; // Kept even when missing.
    if (!bytes.empty()) setPlatformBadge(key, std::move(bytes));
    return nullptr;
}

void Renderer::drawPackageFront(const PackageFront& p) {
    if (!p.at || p.alpha < 0.003f || !createShelfResources()) return;
    const auto look = package_look::of(p.kind);
    const float alpha = p.alpha;
    D2D1_RECT_F bounds{};
    std::vector<D2D1_POINT_2F> rim;
    for (const auto& point : package_look::outline(look.corner, look.corner * p.aspect)) rim.push_back(p.at(point.x, point.y));
    const auto shape = closedPath(factory_.Get(), rim, bounds);
    if (!shape) return;
    // The plastic (or the cardboard's own colour under its print).
    brush(withAlpha(look.rim, alpha)); target_->FillGeometry(shape.Get(), solid_.Get());
    const float bs = look.border, bt = look.border * p.aspect;
    D2D1_RECT_F window_bounds{};
    const auto* frame_info = package_look::frameOf(p.kind.style);
    Cover* frame = frame_info && p.picture ? packageFrame(*frame_info) : nullptr;
    ComPtr<ID2D1PathGeometry> window;
    if (frame) {
        // The console's real front: the art under its banner, the plastic and the banner over it, in
        // two slices (the banner keeps its proportions on a wider or narrower case).
        using package_look::frame_width, package_look::frame_height, package_look::frame_rim;
        const auto& f = *frame_info;
        const float side = frame_rim / frame_width;
        const float top = package_look::frameRow(f, f.art_top, p.aspect), bottom = package_look::frameRow(f, frame_height - frame_rim, p.aspect);
        D2D1_RECT_F art_bounds{};
        if (const auto art = closedPath(factory_.Get(), {p.at(side, top), p.at(1 - side, top), p.at(1 - side, bottom), p.at(side, bottom)}, art_bounds)) {
            brush(gray(0.07f, alpha)); target_->FillGeometry(art.Get(), solid_.Get());
        }
        if (p.print) p.print(side, top, 1 - side, bottom, p.aspect * (1 - 2 * side) / std::max(0.01f, bottom - top), alpha);
        const auto size = frame->bitmap->GetSize();
        const float split = package_look::frameRow(f, f.banner, p.aspect), split_px = f.banner * size.height / frame_height;
        const float shown = alpha * reveal(*frame);
        p.picture(0, 0, 1, split, frame->bitmap.Get(), D2D1::RectF(0, 0, size.width, split_px), shown);
        p.picture(0, split, 1, 1, frame->bitmap.Get(), D2D1::RectF(0, split_px, size.width, size.height), shown);
    } else {
        window = closedPath(factory_.Get(), {p.at(bs, bt), p.at(1 - bs, bt), p.at(1 - bs, 1 - bt), p.at(bs, 1 - bt)}, window_bounds);
    }
    if (window) {
        // The insert: the art (Switch: beneath its head band), the clear sleeve's veil over it.
        target_->PushLayer(D2D1::LayerParameters(window_bounds, window.Get()), nullptr);
        brush(gray(0.07f, alpha)); target_->FillGeometry(window.Get(), solid_.Get());
        const float band = look.band * (1 - 2 * bt);
        const float stripe = look.stripe * (1 - 2 * bs);
        const float art_top = bt + band, art_left = bs + stripe;
        const float art_aspect = p.aspect * (1 - bs - art_left) / std::max(0.01f, 1 - bt - art_top);
        if (p.print) p.print(art_left, art_top, 1 - bs, 1 - bt, art_aspect, alpha);
        using package_look::Band;
        if (band > 0 && p.picture) {
            const float ratio = p.aspect * (1 - 2 * bs) / band;
            if (auto* strip = packageBand(p.kind, p.platform, ratio, Band::across)) {
                const auto size = strip->GetSize();
                p.picture(bs, bt, 1 - bs, art_top, strip, D2D1::RectF(0, 0, size.width, size.height), alpha);
            }
        }
        if (stripe > 0 && p.picture) {
            const float ratio = (1 - 2 * bt) / std::max(0.001f, stripe * p.aspect);
            if (auto* strip = packageBand(p.kind, p.platform, ratio, Band::down)) {
                const auto size = strip->GetSize();
                p.picture(bs, bt, art_left, 1 - bt, strip, D2D1::RectF(0, 0, size.width, size.height), alpha);
            }
        }
        if (look.mark > 0 && p.picture) {
            // A square on the front: its height in face fractions is its width times the front's aspect.
            const float side = look.mark * (1 - 2 * bs);
            if (auto* tile = packageBand(p.kind, p.platform, 1, Band::square)) {
                const auto size = tile->GetSize();
                p.picture(bs, bt, bs + side, bt + side * p.aspect, tile, D2D1::RectF(0, 0, size.width, size.height), alpha);
            }
        }
        if (look.haze > 0) { brush(gray(1, look.haze * alpha)); target_->FillGeometry(window.Get(), solid_.Get()); }
        target_->PopLayer();
    }
    if (p.dim) { brush(gray(0, 0.26f * alpha)); target_->FillGeometry(shape.Get(), solid_.Get()); }
    // The lamp above: a little brighter at the top, the foot in a soft shade.
    if (!package_light_) {
        const D2D1_GRADIENT_STOP stops[] = {{0, gray(1, 0.075f)}, {0.30f, gray(1, 0.0f)}, {0.70f, gray(0, 0.0f)}, {1, gray(0, 0.20f)}};
        ComPtr<ID2D1GradientStopCollection> collection;
        if (SUCCEEDED(target_->CreateGradientStopCollection(stops, 4, collection.GetAddressOf())))
            target_->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties({0, 0}, {0, 1}), collection.Get(), package_light_.GetAddressOf());
    }
    if (package_light_) {
        package_light_->SetStartPoint(p.at(0.5f, 0)); package_light_->SetEndPoint(p.at(0.5f, 1));
        package_light_->SetOpacity(alpha); target_->FillGeometry(shape.Get(), package_light_.Get());
    }
    // Turned from the lamp: darkest at the far edge.
    if (p.shade > 0.003f) {
        const auto mid = [&](float s) { const auto a = p.at(s, 0), b = p.at(s, 1); return D2D1::Point2F((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f); };
        shelf_shade_->SetStartPoint(p.shade_right ? mid(1) : mid(0));
        shelf_shade_->SetEndPoint(p.shade_right ? mid(0) : mid(1));
        shelf_shade_->SetOpacity(std::min(1.0f, p.shade) * alpha);
        target_->FillGeometry(shape.Get(), shelf_shade_.Get());
    }
    if (p.reflection) return;
    // Glossy plastic: the lamp's band across the front's diagonal, then the rim's light.
    const auto a = p.at(0, 0), b = p.at(1, 1);
    const auto diagonal = [&](float t) { return D2D1::Point2F(std::lerp(a.x, b.x, t), std::lerp(a.y, b.y, t)); };
    shelf_gloss_->SetStartPoint(diagonal(p.gloss - 0.42f)); shelf_gloss_->SetEndPoint(diagonal(p.gloss + 0.42f));
    shelf_gloss_->SetOpacity(p.gloss_strength * look.sleeve_gloss * alpha);
    target_->FillGeometry(shape.Get(), shelf_gloss_.Get());
    const float stroke = 0.9f * p.scale;
    if (window && look.border > 0) {
        // The sleeve's seam around the insert: in shade at its top and left, lit along its foot.
        brush(gray(0, 0.32f * alpha)); target_->DrawGeometry(window.Get(), solid_.Get(), 0.8f * p.scale);
        const auto foot_l = p.at(bs, 1 - bt), foot_r = p.at(1 - bs, 1 - bt);
        line(foot_l.x, foot_l.y + 0.8f * p.scale, foot_r.x, foot_r.y + 0.8f * p.scale, gray(1, 0.16f * alpha), 0.8f * p.scale);
    }
    // The rim: lit along its top, a faint edge all round.
    {
        const auto l = p.at(look.corner + 0.01f, 0.004f), r = p.at(1 - look.corner - 0.01f, 0.004f);
        line(l.x, l.y + stroke * 0.5f, r.x, r.y + stroke * 0.5f, gray(1, 0.30f * look.sleeve_gloss * alpha), stroke);
    }
    brush(gray(1, (look.border > 0 && look.rim.r > 0.5f ? 0.30f : 0.10f) * alpha)); target_->DrawGeometry(shape.Get(), solid_.Get(), stroke);
    if (look.rim.r > 0.5f && look.border > 0) { brush(gray(0, 0.18f * alpha)); target_->DrawGeometry(shape.Get(), solid_.Get(), stroke * 0.6f); }
}
