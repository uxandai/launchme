// Renderer: the home's chrome: the empty state, the footer, platform marks and the platform
// badge, the category rail and the category menu.
#include "renderer.hpp"
#include "renderer_internal.hpp"
#include "category_menu.hpp"
#include "category_rail.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

using Microsoft::WRL::ComPtr;
using namespace render_detail;

void Renderer::drawEmptyState() {
    const float inset = context_->category_rail ? category_rail::Layout{viewport_.width, viewport_.height}.right() : 0;
    const float middle = (viewport_.width + inset) * 0.5f;
    const float top = 312;
    // The startup scan usually ends within a second: until then only the background shows, so a
    // quick start never flashes an empty tile before the library. A slow scan fades it in.
    float shown = 1;
    if (scanning_) {
        const float waited = std::chrono::duration<float>(std::chrono::steady_clock::now() - scanning_since_).count();
        shown = smoothFade(std::clamp((waited - 1.2f) / 0.4f, 0.0f, 1.0f));
        if (shown <= 0.001f) return;
    }
    if (!scanning_) { drawEmptyLibrary(); return; }
    const auto accent = uiAccent();
    brush(color(0.12f + accent.r * 0.05f, 0.12f + accent.g * 0.05f, 0.12f + accent.b * 0.05f, 0.54f * shown));
    target_->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(middle - 140, top, middle + 140,
                                                                 top + 280), 32, 32), solid_.Get());
    brush(uiAccent(0.17f * shown));
    target_->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(middle - 140, top, middle + 140,
                                                                 top + 280), 32, 32), solid_.Get());
    const float pulse = 0.38f + 0.16f * std::sin(elapsed_ * 3.1f);
    brush(uiAccent(pulse * shown));
    target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(middle, top + 140), 12, 12), solid_.Get());
    text(scanning_ ? tr(L"ŁADUJĘ BIBLIOTEKĘ") : tr(L"NIE ZNALEZIONO GIER"),
         middle - 280, top + 315, 560, 36,
         button_font_.Get(), color(0.91f, 0.91f, 0.91f, shown));
    if (!scanning_)
        text(tr(L"Sprawdź Steam i konfigurację bibliotek sklepów."), middle - 300, top + 353, 600, 34, button_font_.Get(),
             palette::muted(shown));
}

// No games after the scan: a frosted card saying why (no Steam here, Steam without an account, or
// nothing installed) over three buttons — Dodaj grę, Platformy, Odśwież — with the focus pill on
// the pad's/keyboard's button and a lighter glass under the pointer.
void Renderer::drawEmptyLibrary() {
    using namespace empty_library;
    const Layout layout{viewport_.width, viewport_.height,
                        context_->category_rail ? category_rail::Layout{viewport_.width, viewport_.height}.right() : 0.0f};
    const auto p = layout.panel();
    const auto panel = D2D1::RoundedRect(D2D1::RectF(p.left, p.top, p.right, p.bottom), 28, 28);
    const auto glass = glassPanel(panel, 18);
    glassFill(glass, color(0.035f, 0.04f, 0.05f, 0.82f));
    brush(color(1, 1, 1, panel_borders_ ? 0.16f : 0.07f));
    target_->DrawRoundedRectangle(panel, solid_.Get(), 1.0f);
    // The glyph: no Steam or no account shows the pad, an empty library the 2x2 grid of all games.
    const float cx = layout.middle(), icon_y = p.top + 70;
    brush(color(1, 1, 1, 0.07f));
    target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, icon_y), 40, 40), solid_.Get());
    drawQuickGlyph(context_->empty_reason == Reason::empty ? quick_menu::Action::library : quick_menu::Action::controllers,
                   cx, icon_y, color(0.92f, 0.93f, 0.95f), 0, false);
    const float text_width = p.right - p.left - 120;
    textLine(title(context_->empty_reason), cx - text_width * 0.5f, p.top + 140, text_width, category_font_.Get(), palette::text(), DWRITE_TEXT_ALIGNMENT_CENTER);
    desktopCenteredText(body(context_->empty_reason), cx, p.top + 162, text_width, settings_body_font_.Get(), palette::muted(), 3);
    for (int i = 0; i < action_count; ++i) {
        const auto r = layout.button(i);
        const auto rect = D2D1::RectF(r.left, r.top, r.right, r.bottom);
        const bool focused = context_->empty_actions && context_->empty_focus == i;
        const auto ink = drawButton(rect, focused ? 1.0f : 0.0f, context_->empty_actions ? 1.0f : 0.55f, false, context_->empty_hover == i);
        textLine(label(i), r.left + 12, (r.top + r.bottom) * 0.5f, r.right - r.left - 24, action_font_.Get(), ink, DWRITE_TEXT_ALIGNMENT_CENTER);
    }
}

void Renderer::drawFooter() {
    if (context_->screen != ScreenMode::shelves || context_->xmb || context_->flix || context_->metro || context_->psfive || context_->snitch) return;
    const float y = viewport_.height - 65;
    if (context_->category_rail) {
        if (cards_ && selected_ < cards_->size()) {
            const auto& card = (*cards_)[selected_];
            const float left = card.x + card.size + 24;
            const float center = card.y + card.size - category_rail::Layout::compact_lift * 0.5f;
            textLine(card.game.name, left, center, viewport_.width - left - 57,
                     category_font_.Get(), color(0.96f, 0.96f, 0.96f, smoothFade(detail_) * card.opacity));
        }
        return;
    }
    if (context_->main_row) {
        if (cards_ && selected_ < cards_->size() && (*cards_)[selected_].game.kind != GameKind::all)
            text((*cards_)[selected_].game.name, 57, y, viewport_.width - 224, 48,
                 category_font_.Get(), color(0.96f, 0.96f, 0.96f));
        return;
    }
    const float label_width = std::max(80.0f, viewport_.width * 0.34f);
    ComPtr<IDWriteTextLayout> layout;
    float measured = label_width;
    if (SUCCEEDED(dwrite_->CreateTextLayout(context_->category.c_str(),
            static_cast<UINT32>(context_->category.size()), category_font_.Get(),
            label_width, 48, layout.GetAddressOf()))) {
        DWRITE_TEXT_METRICS metrics{};
        if (SUCCEEDED(layout->GetMetrics(&metrics))) measured = std::min(label_width, metrics.widthIncludingTrailingWhitespace);
    }
    text(context_->category, 57, y, label_width, 48, category_font_.Get(),
         color(0.92f, 0.92f, 0.92f, 0.66f));
    const float separator = 57 + measured + 15;
    text(L"·", separator, y, 23, 48, category_font_.Get(), color(0.9f, 0.9f, 0.9f, 0.66f));
    if (cards_ && !cards_->empty())
        text((*cards_)[selected_].game.name, separator + 30, y,
             std::max(0.0f, viewport_.width - separator - 195), 48,
             category_font_.Get(), color(0.96f, 0.96f, 0.96f));
}

bool Renderer::hasPlatformMark(const std::wstring& platform) const {
    const auto found = platform_badges_.find(platform);
    return found != platform_badges_.end() && found->second.bitmap;
}

// The width drawPlatformMark gives a logo at this height (0 without one).
float Renderer::platformMarkWidth(const std::wstring& platform, float height, float max_width) const {
    const auto found = platform_badges_.find(platform);
    if (found == platform_badges_.end() || !found->second.bitmap) return 0;
    const auto& badge = found->second;
    const auto size = badge.bitmap->GetPixelSize();
    const auto source = badge.has_ink_bounds ? badge.ink_bounds : D2D1::RectF(0, 0, static_cast<float>(size.width), static_cast<float>(size.height));
    const float ink_w = source.right - source.left, ink_h = source.bottom - source.top;
    if (ink_w < 1 || ink_h < 1) return 0;
    return ink_w * std::min(max_width / ink_w, height / ink_h);
}

float Renderer::drawPlatformMark(const std::wstring& platform, float x, float center_y, float height, float max_width, float alpha,
                                 IDWriteTextFormat* fallback) {
    if (platform.empty() || max_width < 4) return 0;
    const auto found = platform_badges_.find(platform);
    if (found == platform_badges_.end() || !found->second.bitmap) {
        // No logo (yet): the name, as before.
        textLine(platform, x, center_y, max_width, fallback, color(0.66f, 0.68f, 0.70f, alpha));
        return std::min(max_width, textAdvance(platform, fallback));
    }
    const auto& badge = found->second;
    const auto size = badge.bitmap->GetPixelSize();
    const auto source = badge.has_ink_bounds ? badge.ink_bounds : D2D1::RectF(0, 0, static_cast<float>(size.width), static_cast<float>(size.height));
    const float ink_w = source.right - source.left, ink_h = source.bottom - source.top;
    if (ink_w < 1 || ink_h < 1) return 0;
    const float factor = std::min(max_width / ink_w, height / ink_h);
    const float width = ink_w * factor, tall = ink_h * factor;
    target_->DrawBitmap(badge.bitmap.Get(), D2D1::RectF(x, center_y - tall * 0.5f, x + width, center_y + tall * 0.5f),
                        0.9f * alpha * reveal(badge), D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, &source);
    return width;
}

void Renderer::drawPlatformBadge() {
    if (context_->screen == ScreenMode::glance || context_->screen == ScreenMode::companion || context_->snitch) return;
    if (context_->screen == ScreenMode::plugin_page || context_->screen == ScreenMode::downloads || context_->psfive) return; // PSfive: beside the game's name.
    // The libraries, Search and the Store: their top bar holds the tabs and controls, no badge.
    if (gridScreen(context_->screen)) return;
    if (!context_->hero_facts.empty()) return; // The default home's facts under the logo show the platform.
    // Szczegóły: the logo is in its line of facts.
    if (!cards_ || selected_ >= cards_->size() || context_->screen == ScreenMode::settings || context_->screen == ScreenMode::devices ||
        context_->screen == ScreenMode::crop || context_->screen == ScreenMode::details) return;
    const auto name = activePlatformName((*cards_)[selected_].game);
    if (name.empty()) return;
    const auto found = platform_badges_.find(name);
    const bool grid = gridScreen(context_->screen);
    const float top = context_->screen == ScreenMode::library ? 106.0f :
                      grid || context_->flix || context_->metro ? 42.0f : viewport_.height - 68.0f - (context_->hints.empty() ? 0.0f : 22.0f);
    const float right = viewport_.width - 57.0f;
    if (found == platform_badges_.end() || !found->second.bitmap) {
        // A studio's mark until its logo arrives: its name, never its key ("publisher:capcom").
        text(render_detail::studioPlatform(name) ? (*cards_)[selected_].game.name : name, right - 110, top + 7, 110, 32,
             button_font_.Get(), color(0.87f, 0.87f, 0.87f));
        return;
    }
    const auto& badge = found->second;
    const auto size = badge.bitmap->GetPixelSize();
    const auto source = badge.has_ink_bounds ? badge.ink_bounds :
        D2D1::RectF(0, 0, static_cast<float>(size.width), static_cast<float>(size.height));
    const float factor = std::min(86.0f / (source.right - source.left), 34.0f / (source.bottom - source.top));
    const float width = (source.right - source.left) * factor, height = (source.bottom - source.top) * factor;
    target_->DrawBitmap(badge.bitmap.Get(), D2D1::RectF(right - width, top + (40 - height) * 0.5f,
        right, top + (40 + height) * 0.5f), 0.88f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, &source);
}

void Renderer::drawCategoryRail() {
    if (!context_->category_rail || !context_->categories) return;
    const category_rail::Layout layout{viewport_.width, viewport_.height, context_->categories->size(), context_->active_category};
    const float alpha = 1.0f - 0.65f * smoothFade(context_->quick_progress);
    for (size_t i = layout.first(); i < layout.first() + layout.visible(); ++i) {
        const bool active = i == context_->active_category;
        const float center = layout.rowTop(i) + category_rail::Layout::row_height * 0.5f;
        const float light = active ? 0.97f : 0.52f;
        textLine((*context_->categories)[i], layout.left, center, layout.right() - layout.left,
                 active ? category_active_font_.Get() : sidebar_font_.Get(), color(light, light, light, alpha), DWRITE_TEXT_ALIGNMENT_TRAILING);
        if (active && context_->category_rail_focus) {
            brush(uiAccent(alpha));
            target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(layout.right() + 12, center), 3, 3), solid_.Get());
        }
    }
    // More categories above/below: the active item always remains fully visible.
    const float x = layout.right() - 10;
    const auto ink = color(0.7f, 0.7f, 0.7f, alpha * 0.7f);
    if (layout.first()) {
        const float y = layout.top() - 15;
        line(x - 5, y + 3, x, y - 2, ink, 1.5f); line(x, y - 2, x + 5, y + 3, ink, 1.5f);
    }
    if (layout.first() + layout.visible() < layout.count) {
        const float y = layout.top() + layout.visible() * layout.row_height + 15;
        line(x - 5, y - 3, x, y + 2, ink, 1.5f); line(x, y + 2, x + 5, y - 3, ink, 1.5f);
    }
}

void Renderer::drawCategoryMenu() {
    if (context_->snitch) return;
    if (context_->psfive) { drawPsfiveMoreMenu(); return; }
    if (context_->category_rail) return;
    const float progress = smoothFade(context_->category_menu_progress);
    if (progress < 0.004f || !context_->categories) return;
    constexpr float width = 350;
    const float height = viewport_.height;
    const float x = width * (progress - 1.0f);
    const bool frosted = frostPane(D2D1::RectF(0, 0, x + width, height), progress);
    if (category_backdrop_) {
        category_backdrop_->SetStartPoint(D2D1::Point2F(width, 0));
        category_backdrop_->SetEndPoint(D2D1::Point2F(width + viewport_.width * drawer_style_.fade_fraction, 0));
        category_backdrop_->SetOpacity(progress * drawer_style_.opacity * (frosted ? 1.0f - 0.3f * glass_level_ : 1.0f));
        target_->FillRectangle(D2D1::RectF(0, 0, viewport_.width, height), category_backdrop_.Get());
    }
    const float top = category_menu::top(height, context_->categories->size());
    target_->PushAxisAlignedClip(D2D1::RectF(0, 40, x + width, height - 15),
                                 D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    brush(color(0.86f, 0.86f, 0.86f, 0.12f));
    target_->FillRoundedRectangle(pill(
        D2D1::RectF(x + 18, context_->category_highlight_y + 8,
                    x + width - 16, context_->category_highlight_y + category_menu::row_height - 8)), solid_.Get());
    for (size_t i = 0; i < context_->categories->size(); ++i) {
        const float y = top + i * category_menu::row_height - context_->category_scroll;
        if (y > height || y + category_menu::row_height < 40) continue;
        text((*context_->categories)[i], x + 27, y + 17, width - 57, 42,
             sidebar_font_.Get(), color(i == static_cast<size_t>(context_->category_cursor) ? 0.97f : 0.75f,
                                      i == static_cast<size_t>(context_->category_cursor) ? 0.97f : 0.75f,
                                      i == static_cast<size_t>(context_->category_cursor) ? 0.97f : 0.75f));
    }
    target_->PopAxisAlignedClip();
}
