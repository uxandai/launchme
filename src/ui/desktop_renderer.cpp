// Desktop mode's library (src/ui/desktop_view.hpp): the drawing helpers every part uses, the frame
// and the brand marks. Its parts: desktop_chrome_renderer.cpp (top bar, sidebar),
// desktop_grid_renderer.cpp (posters), desktop_pane_renderer.cpp (the selected game); popups:
// desktop_popups_renderer.cpp. Dark neutral surfaces with hairline borders and the interface
// accent, the same artwork, tile shapes and typeface as Tryb TV.
#include "renderer.hpp"
#include "brand_icons.hpp"
#include "desktop_render_internal.hpp"

#include <d2d1_2helper.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <numbers>

using Microsoft::WRL::ComPtr;
using namespace desktop_ui;
using namespace desktop_draw;

// ---- Helpers ---------------------------------------------------------------------------------

void Renderer::desktopGlyph(wchar_t glyph, float cx, float cy, float size, D2D1_COLOR_F ink, float angle) {
    if (!desktop_icon_font_ || !glyph || ink.a <= 0.003f) return;
    D2D1_MATRIX_3X2_F base{};
    target_->GetTransform(&base);
    const float k = size / 16.0f;
    auto transform = D2D1::Matrix3x2F::Scale(k, k, D2D1::Point2F(cx, cy));
    if (std::abs(angle) > 0.01f) transform = transform * D2D1::Matrix3x2F::Rotation(angle, D2D1::Point2F(cx, cy));
    target_->SetTransform(transform * base);
    brush(ink);
    // One cached layout per glyph (the icon font is centred both ways): the same pixels as DrawText.
    if (auto* layout = text_layouts_.get(dwrite_.Get(), std::wstring(1, glyph), desktop_icon_font_.Get(), 32, 32, text_variant_glyph))
        target_->DrawTextLayout(D2D1::Point2F(cx - 16, cy - 16), layout, solid_.Get());
    else {
        const wchar_t text[2] = {glyph, 0};
        target_->DrawText(text, 1, desktop_icon_font_.Get(), D2D1::RectF(cx - 16, cy - 16, cx + 16, cy + 16), solid_.Get());
    }
    target_->SetTransform(base);
}

void Renderer::desktopFill(Rect r, float radius, D2D1_COLOR_F ink) {
    if (ink.a <= 0.002f || r.right <= r.left || r.bottom <= r.top) return;
    brush(ink);
    target_->FillRoundedRectangle(D2D1::RoundedRect(box(r), radius, radius), solid_.Get());
}

void Renderer::desktopStroke(Rect r, float radius, D2D1_COLOR_F ink, float width) {
    if (ink.a <= 0.002f || r.right <= r.left || r.bottom <= r.top) return;
    brush(ink);
    const float half = width * 0.5f;
    target_->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(r.left + half, r.top + half, r.right - half, r.bottom - half), radius, radius), solid_.Get(), width);
}

void Renderer::desktopShadow(Rect r, float radius, float strength) {
    if (strength <= 0.003f) return;
    for (int layer = 4; layer >= 1; --layer) {
        const float spread = static_cast<float>(layer) * 3.5f;
        desktopFill({r.left - spread, r.top - spread + 6, r.right + spread, r.bottom + spread + 8}, radius + spread, gray(0, 0.07f * strength));
    }
}

// A vertical two-stop gradient from y0 to y1, created once per device (its colours are fixed per
// slot) and moved each frame, rather than a new stop collection and brush on every paint.
ID2D1LinearGradientBrush* Renderer::desktopGradient(ComPtr<ID2D1LinearGradientBrush>& cached, D2D1_COLOR_F from, D2D1_COLOR_F to, float y0, float y1) {
    if (!cached) {
        ComPtr<ID2D1GradientStopCollection> stops;
        const D2D1_GRADIENT_STOP gradient_stops[] = {{0, from}, {1, to}};
        if (FAILED(target_->CreateGradientStopCollection(gradient_stops, 2, stops.GetAddressOf())) ||
            FAILED(target_->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(D2D1::Point2F(0, y0), D2D1::Point2F(0, y1)), stops.Get(),
                                                      cached.GetAddressOf()))) return nullptr;
    }
    cached->SetStartPoint(D2D1::Point2F(0, y0));
    cached->SetEndPoint(D2D1::Point2F(0, y1));
    return cached.Get();
}

// The tile shape at the origin for a width × height poster (squircle_corner_/corner_reference_ as
// the caller set them): its fill and outline tessellated once for the current scale, and its soft
// shadow as a bitmap. Without ID2D1DeviceContext1 the fill and outline use the geometry.
const Renderer::PosterShape* Renderer::posterShape(float width, float height) {
    if (width <= 0 || height <= 0) return nullptr;
    D2D1_MATRIX_3X2_F base{};
    target_->GetTransform(&base);
    const float scale = std::sqrt(base._11 * base._11 + base._12 * base._12);
    const auto key = (static_cast<uint64_t>(std::lround(width * 16)) & 0xFFFFF) << 44 | (static_cast<uint64_t>(std::lround(height * 16)) & 0xFFFFF) << 24 |
                     (static_cast<uint64_t>(std::lround(scale * 100)) & 0x3FFFFF) << 2 | static_cast<uint64_t>(tile_shape_);
    if (auto found = poster_shapes_.find(key); found != poster_shapes_.end()) { found->second.used = ++poster_shape_clock_; return &found->second; }
    if (poster_shapes_.size() >= 24) // Sizes pass while the slider or the window changes: drop the stalest.
        poster_shapes_.erase(std::min_element(poster_shapes_.begin(), poster_shapes_.end(), [](const auto& a, const auto& b) { return a.second.used < b.second.used; }));
    PosterShape shape;
    shape.geometry = tileShape(D2D1::RectF(0, 0, width, height), tile_shape_);
    if (!shape.geometry) return nullptr;
    if (realizer_) {
        const float tolerance = D2D1::ComputeFlatteningTolerance(base);
        realizer_->CreateFilledGeometryRealization(shape.geometry.Get(), tolerance, shape.fill.GetAddressOf());
        realizer_->CreateStrokedGeometryRealization(shape.geometry.Get(), tolerance, 1, nullptr, shape.outline.GetAddressOf());
    }
    // desktopShadow's four layers drawn once into a bitmap at device pixels: one DrawBitmap per
    // poster instead of four large blended fills (about 1 ms per 4K frame for a dozen posters).
    // Drawn at strength s it is s × the full-strength layers (within a few percent of stacking
    // four layers of 0.07 × s each).
    constexpr float reach = 14, drop = 6;
    shape.shadow_box = D2D1::RectF(-reach, -reach + drop, width + reach, height + reach + drop + 2);
    const UINT pixel_width = static_cast<UINT>(std::ceil((shape.shadow_box.right - shape.shadow_box.left) * scale));
    const UINT pixel_height = static_cast<UINT>(std::ceil((shape.shadow_box.bottom - shape.shadow_box.top) * scale));
    ComPtr<ID2D1BitmapRenderTarget> layers;
    ComPtr<ID2D1SolidColorBrush> ink;
    if (scale > 0 && pixel_width && pixel_height && pixel_width <= 4096 && pixel_height <= 4096 &&
        SUCCEEDED(target_->CreateCompatibleRenderTarget(D2D1::SizeF(static_cast<float>(pixel_width), static_cast<float>(pixel_height)),
                                                        D2D1::SizeU(pixel_width, pixel_height), layers.GetAddressOf())) &&
        SUCCEEDED(layers->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, 0.07f), ink.GetAddressOf()))) {
        layers->BeginDraw();
        layers->Clear(D2D1::ColorF(0, 0, 0, 0));
        layers->SetTransform(D2D1::Matrix3x2F::Translation(-shape.shadow_box.left, -shape.shadow_box.top) * D2D1::Matrix3x2F::Scale(scale, scale));
        for (int layer = 4; layer >= 1; --layer) {
            const float spread = static_cast<float>(layer) * 3.5f, radius = 12 + spread;
            layers->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(-spread, -spread + 6, width + spread, height + spread + 8), radius, radius), ink.Get());
        }
        if (SUCCEEDED(layers->EndDraw())) layers->GetBitmap(shape.shadow.GetAddressOf());
    }
    shape.used = ++poster_shape_clock_;
    return &poster_shapes_.emplace(key, std::move(shape)).first->second;
}

void Renderer::desktopFocusRing(Rect r, float radius) {
    desktopStroke(inflate(r, 3), radius + 3, uiAccent(0.95f), 2);
}

void Renderer::desktopArc(float cx, float cy, float radius, float start, float sweep, D2D1_COLOR_F ink, float width) {
    if (std::abs(sweep) < 0.01f) return;
    sweep = std::clamp(sweep, -6.27f, 6.27f);
    ComPtr<ID2D1PathGeometry> path;
    ComPtr<ID2D1GeometrySink> sink;
    if (FAILED(factory_->CreatePathGeometry(path.GetAddressOf())) || FAILED(path->Open(sink.GetAddressOf()))) return;
    const auto point = [&](float angle) { return D2D1::Point2F(cx + std::cos(angle) * radius, cy + std::sin(angle) * radius); };
    sink->BeginFigure(point(start), D2D1_FIGURE_BEGIN_HOLLOW);
    sink->AddArc(D2D1::ArcSegment(point(start + sweep), D2D1::SizeF(radius, radius), 0, D2D1_SWEEP_DIRECTION_CLOCKWISE,
                                  sweep > std::numbers::pi_v<float> ? D2D1_ARC_SIZE_LARGE : D2D1_ARC_SIZE_SMALL));
    sink->EndFigure(D2D1_FIGURE_END_OPEN);
    if (FAILED(sink->Close())) return;
    brush(ink);
    target_->DrawGeometry(path.Get(), solid_.Get(), width);
}

float Renderer::desktopCenteredText(const std::wstring& value, float cx, float top, float width, IDWriteTextFormat* format,
                                    D2D1_COLOR_F ink, int max_lines) {
    if (value.empty() || width <= 0 || !format) return 0;
    const auto metrics = typeMetrics(format);
    const auto adjust = [](IDWriteTextLayout* layout) {
        layout->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
        layout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        layout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    };
    // Cached: a placeholder title is drawn on every frame until the poster arrives.
    ComPtr<IDWriteTextLayout> created;
    auto* layout = text_layouts_.get(dwrite_.Get(), value, format, width, metrics.line * max_lines + 0.5f, text_variant_centered, adjust);
    if (!layout) {
        if (FAILED(dwrite_->CreateTextLayout(value.c_str(), static_cast<UINT32>(value.size()), format, width, metrics.line * max_lines + 0.5f,
                                             created.GetAddressOf()))) return 0;
        adjust(created.Get()); layout = created.Get();
    }
    DWRITE_TEXT_METRICS measured{};
    layout->GetMetrics(&measured);
    brush(ink);
    target_->DrawTextLayout(D2D1::Point2F(cx - width * 0.5f, top), layout, solid_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
    return std::min(static_cast<float>(std::max(1u, measured.lineCount)), static_cast<float>(max_lines)) * metrics.line;
}

// A picture cropped to fill `box` around its centre.
void Renderer::desktopBitmapFill(ID2D1Bitmap* bitmap, D2D1_RECT_F target, float opacity) {
    if (!bitmap || opacity <= 0.003f) return;
    const auto size = bitmap->GetSize();
    const float w = target.right - target.left, h = target.bottom - target.top;
    if (size.width < 1 || size.height < 1 || w < 1 || h < 1) return;
    const float zoom = std::max(w / size.width, h / size.height);
    const float sw = w / zoom, sh = h / zoom;
    const auto source = D2D1::RectF((size.width - sw) * 0.5f, (size.height - sh) * 0.3f, (size.width + sw) * 0.5f, (size.height - sh) * 0.3f + sh);
    target_->DrawBitmap(bitmap, target, opacity, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, &source);
}

// style 0: ghost (no fill until hovered), 1: outline, 2: primary (accent fill, dark or light ink).
void Renderer::desktopButton(Rect r, int style, float hover, bool pressed, bool enabled, wchar_t glyph, const std::wstring& label) {
    const auto accent = uiAccent();
    const float luminance = 0.2126f * accent.r + 0.7152f * accent.g + 0.0722f * accent.b;
    const float radius = 8;
    D2D1_COLOR_F ink = enabled ? palette::text() : gray(0.5f);
    if (style == 2 && enabled) {
        desktopFill(r, radius, uiAccent(pressed ? 0.82f : 1.0f));
        if (hover > 0.01f) desktopFill(r, radius, luminance > 0.6f ? gray(0, 0.08f * hover) : gray(1, 0.10f * hover));
        ink = luminance > 0.6f ? gray(0.06f) : gray(0.98f);
    } else {
        if (style == 1 || !enabled) desktopFill(r, radius, gray(1, enabled ? 0.045f : 0.025f));
        if (enabled && hover > 0.01f) desktopFill(r, radius, gray(1, (pressed ? 0.11f : 0.065f) * hover));
        if (style == 1) desktopStroke(r, radius, gray(1, 0.085f + 0.05f * hover));
    }
    const float cy = (r.top + r.bottom) * 0.5f;
    const float label_width = label.empty() ? 0 : std::min(textAdvance(label, desktop_button_font_.Get()), r.right - r.left - (glyph ? 48 : 20));
    const float glyph_width = glyph ? 16.0f : 0.0f, gap = glyph && !label.empty() ? 9.0f : 0.0f;
    const float start = (r.left + r.right - (glyph_width + gap + label_width)) * 0.5f;
    if (glyph) desktopGlyph(glyph, start + 8, cy, 15, ink);
    if (!label.empty()) textLine(label, start + glyph_width + gap, cy, label_width + 2, desktop_button_font_.Get(), ink);
}

void Renderer::desktopInstallButton(Rect r, float fraction, const std::wstring& label) {
    desktopFill(r, 8, gray(1, 0.08f));
    const float span = r.right - r.left;
    if (fraction >= 0) desktopFill({r.left, r.top, r.left + span * std::clamp(fraction, 0.0f, 1.0f), r.bottom}, 8, uiAccent(0.32f));
    else {
        const float strip = span * 0.3f, phase = std::fmod(elapsed_ * 0.6f, 1.0f);
        const float x0 = r.left + (span + strip) * phase - strip;
        desktopFill({std::max(r.left, x0), r.top, std::min(r.right, x0 + strip), r.bottom}, 8, uiAccent(0.28f));
    }
    desktopStroke(r, 8, uiAccent(0.6f));
    textLine(label, r.left, (r.top + r.bottom) * 0.5f, span, desktop_button_font_.Get(), palette::text(), DWRITE_TEXT_ALIGNMENT_CENTER);
}

// ---- The frame -------------------------------------------------------------------------------

Layout Renderer::desktopLayoutOf(const View& view) const {
    Layout layout;
    layout.width = viewport_.width; layout.height = viewport_.height;
    layout.poster_width = view.poster;
    layout.tools = static_cast<int>(view.tools.size());
    layout.companion_card = view.companion_card;
    layout.start_card = view.start_card;
    layout.narrow = view.sidebar_narrow; layout.pane_width = view.pane_width;
    layout.pane_open = view.pane_open;
    layout.pane_t = view.pane_t;
    layout.cell_aspect = view.cell_view == 1 ? 1.0f : view.cell_view == 2 ? 0.5625f : 1.5f;
    layout.list = view.cell_view == 3;
    layout.continue_count = static_cast<int>(std::min<size_t>(3, view.continue_tiles.size()));
    return layout;
}

void Renderer::drawDesktop() {
    const auto& view = *context_->desktop;
    const auto layout = desktopLayoutOf(view);
    desktop_chips_.clear(); desktop_chip_add_ = {}; desktop_rating_ = {}; desktop_protondb_ = {}; desktop_hardware_ = {}; desktop_achievements_ = {}; desktop_cloud_ = {}; desktop_studios_ = {};
    desktop_hotspots_.clear(); // Widżety record their controls here.
    companion_bubble_ = {};
    brush(app_bg);
    target_->FillRectangle(D2D1::RectF(0, 0, viewport_.width, viewport_.height), solid_.Get());
    // The selected game's colours as a faint glow behind the grid, as the TV mode's backgrounds.
    if (selected_ < heroes_.size() && heroes_[selected_].bitmap)
        if (auto* tint = blurredArt(heroes_[selected_])) {
            const auto m = layout.main();
            target_->DrawBitmap(tint, D2D1::RectF(m.left - 60, m.top - 60, m.right + 60, m.top + (m.bottom - m.top) * 0.7f), 0.11f,
                                D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
            if (auto* fade = desktopGradient(desktop_hero_fade_, rgb(app_bg.r, app_bg.g, app_bg.b, 0), app_bg, m.top + 120, m.top + (m.bottom - m.top) * 0.7f))
                target_->FillRectangle(D2D1::RectF(m.left, m.top, m.right, m.bottom), fade);
        }
    // No screen transition here: back from a dialog or page, the library is simply there again (the
    // TV mode's zoom and bow would distort the whole window).
    drawDesktopGrid(layout, view);
    if (layout.pane_open) {
        drawDesktopPane(layout, view);
        // ✕: closes the column until another game is chosen.
        const auto close = layout.paneClose();
        const bool over = view.hover == Hit{Target::pane_close, 0};
        const float cx = (close.left + close.right) * 0.5f, cy = (close.top + close.bottom) * 0.5f;
        brush(gray(0, over ? 0.7f : 0.45f));
        target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), 15, 15), solid_.Get());
        desktopGlyph(glyph::close, cx, cy, 14, gray(over ? 1.0f : 0.8f));
    }
    drawDesktopSidebar(layout, view);
    drawDesktopTitlebar(layout, view);
    if (view.downloads_open) drawDesktopDownloads(layout, view);
    if (view.menu) drawDesktopMenu(layout, *view.menu);
    if (view.dragging) drawDesktopDrag(layout, view);
    drawDesktopTooltip(layout, view);
    drawDesktopQuick(view); // Ctrl+K over everything.
    drawDesktopMeta(view.meta);  // Edytuj metadane.
    // A restored window: a hairline along its top edge, where its frame no longer draws one.
    if (!view.maximized) { brush(gray(1, 0.07f)); target_->FillRectangle(D2D1::RectF(0, 0, viewport_.width, 1.0f / viewport_.scale), solid_.Get()); }
}

// A store's or console's mark, white like the icons, centred at (cx, cy) in a size × size box; false
// when there is none (or it cannot be built), so the caller draws its icon.
bool Renderer::desktopBrand(std::string_view key, float cx, float cy, float size, D2D1_COLOR_F ink) {
    if (key.empty()) return false;
    auto found = brand_icons_.find(std::string(key));
    if (found == brand_icons_.end()) {
        ComPtr<ID2D1Geometry> geometry;
        if (const auto* icon = brand_icons::find(key)) brand_icons::build(factory_.Get(), *icon, geometry.GetAddressOf());
        found = brand_icons_.emplace(std::string(key), geometry).first; // A failed build stays empty: the icon shows.
    }
    if (!found->second) return false;
    const float scale = size / 24.0f;
    fillBrandGeometry(found->first, found->second.Get(), D2D1::Matrix3x2F::Scale(scale, scale) * D2D1::Matrix3x2F::Translation(cx - size * 0.5f, cy - size * 0.5f), ink);
    return true;
}

void Renderer::fillBrandGeometry(const std::string& key, ID2D1Geometry* geometry, const D2D1_MATRIX_3X2_F& placement, D2D1_COLOR_F ink) {
    D2D1_MATRIX_3X2_F base{};
    target_->GetTransform(&base);
    const auto transform = D2D1::Matrix3x2F::ReinterpretBaseType(&placement)[0] * D2D1::Matrix3x2F::ReinterpretBaseType(&base)[0];
    target_->SetTransform(transform);
    brush(ink);
    bool drawn = false;
    if (realizer_) {
        // The mark tessellated once per device scale (1/96 steps), not on every frame.
        const float device = std::sqrt(transform._11 * transform._11 + transform._12 * transform._12);
        const auto realization_key = key + '@' + std::to_string(std::lround(device * 96));
        if (brand_realizations_.size() >= 256 && !brand_realizations_.contains(realization_key)) brand_realizations_.clear();
        auto& realization = brand_realizations_[realization_key];
        if (!realization) realizer_->CreateFilledGeometryRealization(geometry, D2D1::ComputeFlatteningTolerance(transform), realization.GetAddressOf());
        if (realization) { realizer_->DrawGeometryRealization(realization.Get(), solid_.Get()); drawn = true; }
    }
    if (!drawn) target_->FillGeometry(geometry, solid_.Get());
    target_->SetTransform(base);
}

float Renderer::drawLaunchmeLogo(float left, float cy, float height, D2D1_COLOR_F ink, bool centered, bool mark_only) {
    // Not brand_icons keys, so they never meet a store's mark.
    static const std::string full = "launchme_logo", mark = "launchme_logo_mark";
    const auto& key = mark_only ? mark : full;
    auto found = brand_icons_.find(key);
    if (found == brand_icons_.end()) {
        ComPtr<ID2D1Geometry> geometry;
        if (mark_only) brand_icons::buildLogoMark(factory_.Get(), geometry.GetAddressOf());
        else brand_icons::buildLogo(factory_.Get(), geometry.GetAddressOf());
        found = brand_icons_.emplace(key, geometry).first; // A failed build stays empty: nothing is drawn.
    }
    if (!found->second || height <= 0 || ink.a <= 0) return 0;
    D2D1_RECT_F bounds{};
    if (FAILED(found->second->GetBounds(nullptr, &bounds)) || bounds.bottom <= bounds.top) return 0;
    const float scale = height / (bounds.bottom - bounds.top), width = (bounds.right - bounds.left) * scale;
    if (centered) left -= width * 0.5f;
    fillBrandGeometry(key, found->second.Get(), D2D1::Matrix3x2F::Translation(-bounds.left, -bounds.top) * D2D1::Matrix3x2F::Scale(scale, scale) *
                                                    D2D1::Matrix3x2F::Translation(left, cy - height * 0.5f), ink);
    return width;
}
