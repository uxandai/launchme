// PSfive home layout (src/app/psfive.cpp, geometry in psfive_layout.hpp), after the PS5 home
// screen: the focused game's background over the whole screen, the top bar (categories, Menu, the
// clock), the row of square covers (drawn as the shelves' cards), its platform badge (black on
// white) and name beside the focused tile, and low on the left the game's logo over frosted
// "Graj" and "…" buttons.
#include "renderer.hpp"
#include "renderer_internal.hpp"
#include "psfive_layout.hpp"
#include "../i18n/i18n.hpp"

#include <algorithm>
#include <cmath>

namespace {
using palette::gray;
using render_detail::box;
using render_detail::smoothFade;
}

void Renderer::drawPsfive() {
    const auto& cards = *cards_;
    const float width = viewport_.width, height = viewport_.height;
    // The buttons and the logo rise over the quick bar while it opens.
    const float lift = smoothFade(context_->quick_progress);
    drawBackground();
    if (psfive_shade_) {
        psfive_shade_->SetEndPoint(D2D1::Point2F(0, height));
        target_->FillRectangle(D2D1::RectF(0, 0, width, height), psfive_shade_.Get());
    }
    if (context_->psfive_dim > 0.003f) { // Settings → PSfive → Przyciemnienie tła.
        brush(D2D1::ColorF(0.008f, 0.012f, 0.016f, context_->psfive_dim * 0.8f));
        target_->FillRectangle(D2D1::RectF(0, 0, width, height), solid_.Get());
    }
    beginScreenForeground();
    drawPsfiveTopBar();
    drawPsfiveLabel();
    // The start-up intro (psfive_layout::intro): the logo and buttons fade in after the row, without
    // their frosted glass meanwhile (it copies the target, which a pushed layer does not allow).
    const float chrome = std::clamp(context_->psfive_intro, 0.0f, 1.0f);
    if (chrome >= 0.999f) {
        drawPsfiveLogo(lift);
        drawPsfiveButtons(); // Last before the shade: its glass blurs everything drawn so far.
    } else if (chrome > 0.004f) {
        target_->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), nullptr, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
                                                 D2D1::IdentityMatrix(), chrome), nullptr);
        drawPsfiveLogo(lift);
        drawPsfiveButtons(false);
        target_->PopLayer();
    }
    drawQuickShade();
    const auto row_clip = D2D1::RectF(psfive_layout::rowClipLeft(), 0, width, height);
    // Every shadow first: the tiles sit close, so none may fall over a neighbour (the focused
    // tile, drawn last, would darken the edges of the tiles beside it). The shadows reach past the
    // row's clip on the left (the first tile's was cut off there, issue #23); a tile sliding out
    // past the row's start takes its shadow with it.
    constexpr float shadow_reach = 20;
    target_->PushAxisAlignedClip(D2D1::RectF(psfive_layout::rowClipLeft() - shadow_reach, 0, width, height), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    for (size_t i = 0; i < cards.size(); ++i) {
        const auto& card = cards[i];
        if (card.opacity < 0.01f || card.size <= 0 || card.x > width + 45 || card.x + card.size < -45) continue;
        const bool focused = i == selected_;
        const float zoom = focused ? 1.0f + 0.13f * smoothFade(context_->launch_progress) : 1.0f;
        const float size = card.size * zoom;
        const float leaving = std::clamp(1.0f - (psfive_layout::rowClipLeft() - card.x) / shadow_reach, 0.0f, 1.0f);
        if (leaving <= 0.001f) continue;
        drawSquareShadow(card.x - (size - card.size) * 0.5f, card.y - (size - card.size) * 0.5f, size, focused, card.opacity * leaving);
    }
    target_->PopAxisAlignedClip();
    target_->PushAxisAlignedClip(row_clip, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    for (size_t i = 0; i < cards.size(); ++i) if (i != selected_) drawCard(cards[i], i);
    target_->PopAxisAlignedClip();
    const float launch = smoothFade(context_->launch_progress);
    if (launch > 0.001f) {
        brush(D2D1::ColorF(0.008f, 0.012f, 0.016f, launch * 0.82f));
        target_->FillRectangle(D2D1::RectF(0, 0, width, height), solid_.Get());
    }
    target_->PushAxisAlignedClip(row_clip, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    if (selected_ < cards.size()) drawCard(cards[selected_], selected_);
    target_->PopAxisAlignedClip();
    drawPsfiveDetails(); // Szczegóły pod rzędem: the sheet over the row (psfive_details_renderer.cpp).
}

float Renderer::drawPsfiveBadge(const std::wstring& platform, float x, float center_y, float alpha, bool draw) {
    constexpr float pad = 10, ink_height = 16, ink_width = 84;
    const auto found = platform_badges_.find(platform);
    const Cover* logo = found != platform_badges_.end() && found->second.bitmap && !render_detail::wordOnlyBadge(platform) ? &found->second : nullptr;
    D2D1_RECT_F source{};
    float w = 0, h = 0;
    if (logo) {
        const auto pixels = logo->bitmap->GetPixelSize();
        source = logo->has_ink_bounds ? logo->ink_bounds : D2D1::RectF(0, 0, static_cast<float>(pixels.width), static_cast<float>(pixels.height));
        const float ink_w = source.right - source.left, ink_h = source.bottom - source.top;
        if (ink_w < 1 || ink_h < 1) logo = nullptr;
        else {
            const float factor = std::min(ink_width / ink_w, ink_height / ink_h);
            w = ink_w * factor; h = ink_h * factor;
        }
    }
    if (!logo) w = std::min(150.0f, textAdvance(platform, button_font_.Get()));
    const float half = psfive_layout::badge_height * 0.5f, badge_width = w + 2 * pad;
    if (!draw) return badge_width;
    brush(gray(0.97f, alpha));
    target_->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(x, center_y - half, x + badge_width, center_y + half), 5, 5), solid_.Get());
    const auto ink = D2D1::ColorF(0.02f, 0.022f, 0.026f, alpha);
    if (!logo) {
        textLine(platform, x, center_y, badge_width, button_font_.Get(), ink);
        return badge_width;
    }
    // The monochrome logo's alpha as the ink: black on the white badge. FillOpacityMask needs
    // aliased drawing; the mask keeps its own smooth edges.
    const auto mode = target_->GetAntialiasMode();
    target_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
    brush(D2D1::ColorF(ink.r, ink.g, ink.b, alpha * reveal(*logo)));
    const auto destination = D2D1::RectF(x + pad, center_y - h * 0.5f, x + pad + w, center_y + h * 0.5f);
    target_->FillOpacityMask(logo->bitmap.Get(), solid_.Get(), D2D1_OPACITY_MASK_CONTENT_GRAPHICS, &destination, &source);
    target_->SetAntialiasMode(mode);
    return badge_width;
}

// Beside the focused tile, under the small ones: the platform badge and the game's name (the
// library: its number of games; a category's "Wszystkie": the category). It follows the tile
// while the row glides and fades in with every new focus.
void Renderer::drawPsfiveLabel() {
    if (selected_ >= cards_->size()) return;
    const auto& card = (*cards_)[selected_];
    const auto& game = card.game;
    const float alpha = smoothFade(detail_ * 2.5f) * card.opacity * std::clamp(context_->psfive_intro, 0.0f, 1.0f);
    if (alpha < 0.01f || !psfive_name_font_) return;
    const float center = psfive_layout::labelCenter(context_->psfive_scale) + (card.y - psfive_layout::row_top);
    const float right = viewport_.width - psfive_layout::bar_margin;
    float x = card.x + card.width() + psfive_layout::focus_gap * context_->psfive_scale; // Where the next tile starts.
    if (isGame(game)) {
        const auto platform = activePlatformName(game);
        if (!platform.empty()) x += drawPsfiveBadge(platform, x, center, alpha) + 14;
    }
    const std::wstring name = game.kind == GameKind::all ? std::wstring(tr(L"Biblioteka")) : game.name;
    // A soft dark copy under the light name keeps it legible over a bright full-screen picture.
    textLine(name, x + 1, center + 1.5f, std::max(40.0f, right - x), psfive_name_font_.Get(), D2D1::ColorF(0, 0, 0, 0.6f * alpha));
    textLine(name, x, center, std::max(40.0f, right - x), psfive_name_font_.Get(), gray(0.98f, alpha));
    if (isGame(game) || context_->psfive_caption.empty()) return; // Library, categories, consoles: how many games.
    const float after = x + textAdvance(name, psfive_name_font_.Get()) + 16;
    if (after < right - 40) textLine(context_->psfive_caption, after, center, right - after, menu_font_.Get(), gray(0.72f, alpha));
}

// Right to left: the clock (optional), Menu (the quick bar) and Widgets (the right drawer).
void Renderer::drawPsfiveTopBar() {
    using namespace psfive_layout;
    const float width = viewport_.width;
    const float opacity = context_->psfive_top_opacity * std::clamp(context_->psfive_intro, 0.0f, 1.0f);
    const bool with_clock = context_->psfive_clock_shown;
    // One opacity for the whole row. Only the focused tab is bold while navigating;
    // otherwise the shown category is bold, including a named overflow category.
    // "Pasek kategorii": the library's strip in the middle (drawHomeTabs) instead of these tabs.
    static const std::vector<std::wstring> none;
    const auto& tabs = context_->home_tabs.empty() ? context_->psfive_tabs : none;
    psfive_tab_rects_.assign(tabs.size(), {});
    float x = tabs_left;
    const float available = (with_clock ? clock(width).left - clock_gap : width - bar_margin) - 48 - tabs_left;
    const float max_tab = tabs.empty() ? 0 : std::max(40.0f, (available - (tabs.size() - 1) * tab_gap) / tabs.size());
    for (size_t i = 0; i < tabs.size() && psfive_tab_font_ && psfive_tab_idle_font_; ++i) {
        const bool shown = static_cast<int>(i) == context_->psfive_tab;
        const bool focused = static_cast<int>(i) == context_->psfive_tab_focus;
        const bool bold = context_->psfive_tab_focus >= 0 ? focused : shown;
        auto* font = bold ? psfive_tab_font_.Get() : psfive_tab_idle_font_.Get();
        const wchar_t icon = i < context_->psfive_tab_glyphs.size() ? context_->psfive_tab_glyphs[i] : 0;
        const float w = icon ? 34.0f : std::min(max_tab, textAdvance(tabs[i], psfive_tab_font_.Get()));
        psfive_tab_rects_[i] = {x - 16, top_center - 25, x + w + 16, top_center + 25};
        if (icon) { // Ikony zamiast nazw: the bold one in full, the others dimmed as the idle weight reads.
            desktopGlyph(icon, x + w * 0.5f, top_center, 28.0f, gray(0.97f, opacity * (bold ? 1.0f : 0.62f)));
            if (shown) { // A small dot under the shown category's icon (the dimming alone is easy to miss).
                brush(gray(0.97f, opacity));
                target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(x + w * 0.5f, top_center + 25), 2.6f, 2.6f), solid_.Get());
            }
        } else if (i == context_->psfive_visible_tabs && textAdvance(tabs[i], font) > w) {
            // Keep the overflow affordance visible even when a long category name is clipped.
            const float dots = textAdvance(L"...", font);
            textLine(tabs[i].substr(0, tabs[i].size() - 3), x, top_center, w - dots, font, gray(0.97f, opacity));
            textLine(L"...", x + w - dots, top_center, dots + 4, font, gray(0.97f, opacity));
        } else textLine(tabs[i], x, top_center, w + 4, font, gray(0.97f, opacity));
        x += w + tab_gap;
    }
    if (with_clock && !context_->psfive_clock.empty() && psfive_clock_font_) {
        const auto r = clock(width);
        textLine(context_->psfive_clock, r.left, top_center, r.right - r.left, psfive_clock_font_.Get(), gray(0.96f, opacity), DWRITE_TEXT_ALIGNMENT_TRAILING);
    }
    for (int i = 0; i < top_count; ++i) {
        const auto r = topItem(i, width, with_clock);
        const float cx = (r.left + r.right) * 0.5f, cy = (r.top + r.bottom) * 0.5f;
        const bool focused = context_->psfive_top == i;
        if (focused) {
            brush(gray(0.95f, opacity));
            target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), icon_radius, icon_radius), solid_.Get());
        }
        const auto ink = focused ? gray(0.06f, opacity) : gray(0.95f, opacity);
        brush(ink);
        if (i == top_menu) {
            Microsoft::WRL::ComPtr<ID2D1TransformedGeometry> icon;
            if (SUCCEEDED(factory_->CreateTransformedGeometry(squircle_.Get(),
                    D2D1::Matrix3x2F::Scale(24, 24) * D2D1::Matrix3x2F::Translation(cx - 12, cy - 12), icon.GetAddressOf())))
                target_->DrawGeometry(icon.Get(), solid_.Get(), 2.4f);
            target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), 3.2f, 3.2f), solid_.Get());
        } else {
            // A tall widget beside two small ones: the dashboard drawer.
            target_->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(cx - 12, cy - 11, cx - 2, cy + 11), 2.5f, 2.5f), solid_.Get(), 2);
            for (int row = 0; row < 2; ++row) {
                const float top = cy - 11 + row * 13.0f;
                target_->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(cx + 3, top, cx + 12, top + 9), 2, 2), solid_.Get(), 2);
            }
        }
    }
}

void Renderer::drawPsfiveMoreMenu() {
    if (!context_->categories || context_->categories->size() <= context_->psfive_visible_tabs) return;
    const float alpha = smoothFade(context_->category_menu_progress);
    if (alpha < 0.004f) return;
    const auto layout = psfiveMoreMenu(context_->categories->size(), context_->psfive_visible_tabs);
    const auto r = layout.revealed(context_->category_menu_progress);
    const auto shape = D2D1::RoundedRect(D2D1::RectF(r.left, r.top, r.right, r.bottom), 18, 18);
    const auto glass = glassPanel(shape);
    glassFill(glass, D2D1::ColorF(0.08f, 0.085f, 0.10f, 0.94f), alpha);
    const float bottom = r.bottom - layout.padding;
    target_->PushAxisAlignedClip(D2D1::RectF(r.left + 6, layout.top(), r.right - 6, bottom), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    const float selected = std::clamp(context_->psfive_menu_glide.value_or(static_cast<float>(context_->category_cursor)),
        static_cast<float>(context_->psfive_visible_tabs), static_cast<float>(context_->categories->size() - 1));
    const float highlight = layout.top() + (selected - static_cast<float>(context_->psfive_visible_tabs)) * layout.row_height - context_->category_scroll;
    brush(gray(1, 0.16f * alpha));
    target_->FillRoundedRectangle(pill(D2D1::RectF(r.left + 8, highlight + 2, r.right - 8, highlight + layout.row_height - 2)), solid_.Get());
    for (size_t i = context_->psfive_visible_tabs; i < context_->categories->size(); ++i) {
        const float y = layout.rowTop(static_cast<int>(i), context_->category_scroll);
        if (y + layout.row_height <= layout.top() || y >= bottom) continue;
        const bool focused = static_cast<int>(i) == context_->category_cursor;
        if (i == context_->active_category) {
            brush(gray(0.97f, alpha));
            target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(r.left + 18, y + layout.row_height * 0.5f), 3, 3), solid_.Get());
        }
        textLine((*context_->categories)[i], r.left + 30, y + layout.row_height * 0.5f, r.right - r.left - 48,
                 menu_font_.Get(), gray(focused ? 0.98f : 0.76f, alpha));
    }
    target_->PopAxisAlignedClip();
    const float cx = r.right - 20;
    if (context_->category_scroll > 0.5f) {
        line(cx - 4, r.top + 6, cx, r.top + 3, gray(0.9f, alpha), 1.5f);
        line(cx, r.top + 3, cx + 4, r.top + 6, gray(0.9f, alpha), 1.5f);
    }
    if (alpha > 0.98f && context_->category_scroll < layout.maxScroll() - 0.5f) {
        line(cx - 4, r.bottom - 6, cx, r.bottom - 3, gray(0.9f, alpha), 1.5f);
        line(cx, r.bottom - 3, cx + 4, r.bottom - 6, gray(0.9f, alpha), 1.5f);
    }
}

// Over "Graj" / "…", left-aligned with them: the game's logo, or its title in large type when it
// has none. It rises with the buttons over the quick bar and fades with a new focus.
void Renderer::drawPsfiveLogo(float lift) {
    if (selected_ >= cards_->size() || !isGame((*cards_)[selected_].game)) return;
    const float width = viewport_.width, bottom = psfive_layout::logoBottom(viewport_.height, lift);
    const float left = psfive_layout::anchorX(context_->psfive_scale);
    if (auto box = cornerLogoRect(std::min(psfive_layout::logo_width, width * 0.4f), psfive_layout::logo_height, left, bottom)) {
        const float w = box->right - box->left;
        drawCornerLogo(D2D1::RectF(left, box->top, left + w, box->bottom)); // Left-aligned instead of right.
        return;
    }
    const auto& logo = logos_[selected_];
    if (logo.bitmap || logo.decoding || context_->logo_pending) return; // Wait for the lookup and the decode.
    const float fade = smoothFade(detail_) * (1.0f - smoothFade(context_->quick_progress));
    if (fade < 0.01f) return;
    text((*cards_)[selected_].game.name, left, bottom - 76 + (1.0f - fade) * 10.0f, std::min(width * 0.5f, width - left - 64), 76,
         hero_font_.Get(), gray(0.97f, fade));
}

// "Graj" (the game's primary action) and "…" (its menu) on grey frosted glass: the background
// under them blurred and greyed. The focused one is white with dark ink, the white pill gliding
// between them (ViewContext::psfive_action_glide). Both rise over the quick bar while it is open.
void Renderer::drawPsfiveButtons(bool frost) {
    if (selected_ >= cards_->size()) return;
    const auto& game = (*cards_)[selected_].game;
    if (!isGame(game) || game.kind == GameKind::catalog) return;
    const float height = viewport_.height, scale = context_->psfive_scale;
    const float lift = smoothFade(context_->quick_progress), rise = psfive_layout::quick_lift * lift;
    // The quick bar takes the focus: the white pill fades while it is open (and returns after).
    const int focus = lift < 0.5f ? context_->psfive_action : -1;
    const float pill_alpha = 1.0f - lift;
    const auto play_rect = psfive_layout::action(0, height, scale, lift), more_rect = psfive_layout::action(1, height, scale, lift);
    // One copy and blur for both (a second copy would already hold the first button's glass).
    const D2D1_ROUNDED_RECT pills[] = {pill(box(play_rect)), pill(box(more_rect))};
    Microsoft::WRL::ComPtr<ID2D1RoundedRectangleGeometry> glass[2];
    Microsoft::WRL::ComPtr<ID2D1GeometryGroup> both;
    if (frost && glass_level_ > 0.01f && SUCCEEDED(factory_->CreateRoundedRectangleGeometry(pills[0], glass[0].GetAddressOf())) &&
        SUCCEEDED(factory_->CreateRoundedRectangleGeometry(pills[1], glass[1].GetAddressOf()))) {
        ID2D1Geometry* shapes[] = {glass[0].Get(), glass[1].Get()};
        if (SUCCEEDED(factory_->CreateGeometryGroup(D2D1_FILL_MODE_WINDING, shapes, 2, both.GetAddressOf())) && frostCapture(both.Get(), 12))
            frostDraw(both.Get());
    }
    for (int i = 0; i < psfive_layout::action_count; ++i) {
        // Grey over the blur (alone where effects are missing): light on a dark picture, darker on a bright one.
        brush(D2D1::ColorF(0.25f, 0.255f, 0.27f, 0.56f));
        target_->FillRoundedRectangle(pills[i], solid_.Get());
    }
    if (context_->psfive_action >= 0 && pill_alpha > 0.004f) {
        auto r = psfive_layout::action(context_->psfive_action, height, scale, lift);
        if (context_->psfive_action_glide) {
            const auto& glide = *context_->psfive_action_glide;
            r = {glide.left, glide.top - rise, glide.right, glide.bottom - rise};
        }
        drawButtonFocus(box(r), pill_alpha);
    }
    const auto ink = [&](int i, bool enabled) {
        if (!enabled) return gray(focus == i ? 0.42f : 0.56f);
        return focus == i ? gray(0.05f) : gray(0.97f);
    };
    const auto play = psfive_layout::action(0, height, scale, lift);
    // In place of the large "Pobieranie w Steam" label: a small status above a thin track.
    if (!drawInstallProgress(game, box(play), ink(0, true), focus == 0 ? gray(0.05f, 0.15f) : gray(1, 0.20f), settings_body_font_.Get()))
        text(context_->primary_action, play.left + 18, play.top, play.right - play.left - 36, play.bottom - play.top,
             psfive_button_font_.Get(), ink(0, context_->primary_enabled));
    const auto more = psfive_layout::action(1, height, scale, lift);
    brush(ink(1, true));
    const float cx = (more.left + more.right) * 0.5f, cy = (more.top + more.bottom) * 0.5f;
    for (int dot = -1; dot <= 1; ++dot)
        target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx + dot * 14.0f, cy), 3.3f, 3.3f), solid_.Get());
}

void Renderer::drawPsfiveFrame(float x, float y, float size, D2D1_COLOR_F tint, float alpha) {
    const float out = psfive_layout::frame_gap + psfive_layout::frame_width * 0.5f;
    const auto shape = tileShape(D2D1::RectF(x - out, y - out, x + size + out, y + size + out), tile_shape_);
    if (!shape) return;
    // Nearly white, with a touch of the cover's colour.
    if (glow_trace_) {
        drawOrbitOutline(shape.Get(), D2D1::ColorF(std::lerp(tint.r, 1.0f, 0.7f), std::lerp(tint.g, 1.0f, 0.7f), std::lerp(tint.b, 1.0f, 0.7f)), alpha, psfive_layout::frame_width);
        return;
    }
    brush(D2D1::ColorF(std::lerp(tint.r, 1.0f, 0.7f), std::lerp(tint.g, 1.0f, 0.7f), std::lerp(tint.b, 1.0f, 0.7f), 0.95f * alpha));
    target_->DrawGeometry(shape.Get(), solid_.Get(), psfive_layout::frame_width);
}
