// Metro home layout (src/app/metro.cpp, geometry in metro_layout.hpp): lower-case page tabs over
// pages of tiles. Games show their art with a caption band; Biblioteka, Ustawienia, Glance and the
// store and console shortcuts are solid tiles of their own colour with a drawn glyph; Towarzyszka's
// tile shows her close-up, and her line in a bubble beside it.
#include "renderer.hpp"
#include "renderer_internal.hpp"
#include "metro_layout.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

namespace {
using palette::gray;
using render_detail::smoothFade;
D2D1_COLOR_F mix(D2D1_COLOR_F a, D2D1_COLOR_F b, float t, float alpha) {
    return D2D1::ColorF(std::lerp(a.r, b.r, t), std::lerp(a.g, b.g, t), std::lerp(a.b, b.b, t), alpha);
}
}

void Renderer::drawMetro() {
    const auto& cards = *cards_;
    drawBackground();
    // The tiles carry the page: the focused game's background stays behind a dark veil (Jasny motyw:
    // a white one, the background barely showing through).
    brush(metro_light_ ? D2D1::ColorF(0.955f, 0.958f, 0.965f, 0.9f) : D2D1::ColorF(0.010f, 0.012f, 0.016f, 0.62f));
    target_->FillRectangle(D2D1::RectF(0, 0, viewport_.width, viewport_.height), solid_.Get());
    beginScreenForeground();
    drawMetroTabs();
    drawQuickShade();
    companion_tile_mouth_.reset();
    for (size_t i = 0; i < cards.size(); ++i) if (i != selected_) drawMetroTile(cards[i], i);
    const float focus = smoothFade(context_->launch_progress);
    if (focus > 0.001f) {
        brush(D2D1::ColorF(0.008f, 0.012f, 0.016f, focus * 0.82f));
        target_->FillRectangle(D2D1::RectF(0, 0, viewport_.width, viewport_.height), solid_.Get());
    }
    if (selected_ < cards.size()) drawMetroTile(cards[selected_], selected_);
    // Tytuł w lewym dolnym rogu: the focused game's name and its platform's mark on the hints' line.
    if (context_->metro_title_corner && selected_ < cards.size() && isGame(cards[selected_].game) && !context_->metro_tabs_focus) {
        const auto& game = cards[selected_].game;
        const float cy = hintsCenterY(), right = hintsLeft() - 28;
        const auto platform = activePlatformName(game);
        const float mark_room = platform.empty() ? 0.0f : 150.0f;
        const float name_width = std::min(std::max(0.0f, right - 57 - mark_room), std::ceil(textAdvance(game.name, category_font_.Get())));
        const auto ink = metro_light_ ? gray(0.08f) : gray(0.96f);
        textLine(game.name, 57, cy, name_width + 2, category_font_.Get(), ink);
        if (!platform.empty()) {
            const float x = 57 + name_width + 22;
            brush(metro_light_ ? gray(0, 0.35f) : gray(1, 0.28f));
            target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(x - 11, cy + 1), 2.2f, 2.2f), solid_.Get());
            drawPlatformMark(platform, x, cy, 22, std::max(0.0f, std::min(120.0f, right - x)), 1.0f, toast_font_.Get());
        }
    }
    // Her line beside her tile while it is on screen (left of it, over Biblioteka and Ustawienia).
    if (companion_tile_mouth_ && focus < 0.001f) {
        const auto& [tile, mouth, alpha] = *companion_tile_mouth_;
        if (tile.right > 0 && tile.left < viewport_.width) drawCompanionTileBubble(tile, mouth, alpha, true);
    }
}

// The strip glides with the pages: the active tab always starts at the grid's left edge, the
// previous ones fade out to the left, the next ones follow in grey.
void Renderer::drawMetroTabs() {
    const auto& tabs = context_->metro_tabs;
    // "Pasek kategorii": the library's strip in the middle at the top takes the tabs' place (drawHomeTabs).
    if (!context_->home_tabs.empty()) { metro_tab_rects_.clear(); return; }
    if (tabs.empty() || !metro_tab_font_) return;
    std::vector<float> lefts(tabs.size()), widths(tabs.size());
    float at = 0;
    const auto icon = [&](size_t i) -> wchar_t { return i < context_->metro_tab_glyphs.size() ? context_->metro_tab_glyphs[i] : 0; };
    for (size_t i = 0; i < tabs.size(); ++i) {
        DWRITE_TEXT_METRICS metrics{};
        const auto measured = icon(i) ? nullptr : text_layouts_.get(dwrite_.Get(), tabs[i], metro_tab_font_.Get(), 4000, 80);
        widths[i] = icon(i) ? 44.0f : measured && SUCCEEDED(measured->GetMetrics(&metrics)) ? metrics.widthIncludingTrailingWhitespace : tabs[i].size() * 18.0f;
        lefts[i] = at;
        at += widths[i] + metro_layout::tab_gap;
    }
    const float position = std::clamp(context_->metro_tab, 0.0f, static_cast<float>(tabs.size() - 1));
    const auto below = static_cast<size_t>(std::floor(position));
    const auto above = std::min(below + 1, tabs.size() - 1);
    const float offset = std::lerp(lefts[below], lefts[above], position - static_cast<float>(below));
    // The quick bar lifts the tiles into the strip's band: the tabs give way completely.
    const float quiet = 1.0f - smoothFade(context_->quick_progress);
    metro_tab_rects_.assign(tabs.size(), {});
    for (size_t i = 0; i < tabs.size(); ++i) {
        const float left = metro_layout::left + lefts[i] - offset;
        metro_tab_rects_[i] = {left - 8, metro_layout::tabs_center - 34, left + widths[i] + 8, metro_layout::tabs_center + 30};
        if (left > viewport_.width || left + widths[i] < 0) continue;
        const float active = std::max(0.0f, 1.0f - std::abs(static_cast<float>(i) - position));
        // Previous tabs fade out before the screen's edge could cut them ("ny pc").
        const float alpha = (left < metro_layout::left ? smoothFade(left / std::max(1.0f, metro_layout::left)) : 1.0f) * quiet;
        if (alpha < 0.01f) continue;
        const auto ink = metro_light_ ? gray(std::lerp(0.62f, 0.06f, active), alpha) : gray(std::lerp(0.42f, 0.97f, active), alpha);
        if (icon(i)) desktopGlyph(icon(i), left + widths[i] * 0.5f, metro_layout::tabs_center - 2, 34.0f, ink); // Ikony zamiast nazw.
        else textLine(tabs[i], left, metro_layout::tabs_center, widths[i] + 8, metro_tab_font_.Get(), ink);
        // The strip has the focus: an accent bar under the current tab, gliding with it.
        if (context_->metro_tabs_focus && active > 0.01f) {
            brush(uiAccent(active * alpha));
            target_->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(left, metro_layout::tabs_center + 24,
                left + widths[i] - 4, metro_layout::tabs_center + 28), 2, 2), solid_.Get());
        }
    }
}

void Renderer::drawMetroGlyph(metro_layout::Role role, float cx, float cy, float size, D2D1_COLOR_F ink) {
    using metro_layout::Role;
    const float stroke = std::max(2.0f, size * 0.07f);
    brush(ink);
    switch (role) {
    case Role::library:
    case Role::all: { // The quick bar's 2x2 glyph.
        const float cell = size * 0.42f, gap = size * 0.16f;
        for (int tile = 0; tile < 4; ++tile) {
            const float left = tile % 2 ? cx + gap * 0.5f : cx - gap * 0.5f - cell, top = tile / 2 ? cy + gap * 0.5f : cy - gap * 0.5f - cell;
            target_->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(left, top, left + cell, top + cell), cell * 0.25f, cell * 0.25f), solid_.Get(), stroke);
        }
        break;
    }
    case Role::settings: { // A cog: rim, hub and eight teeth.
        const float rim = size * 0.34f;
        target_->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), rim, rim), solid_.Get(), stroke);
        target_->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), rim * 0.36f, rim * 0.36f), solid_.Get(), stroke * 0.85f);
        for (int tooth = 0; tooth < 8; ++tooth) {
            const float a = tooth * std::numbers::pi_v<float> / 4;
            line(cx + std::cos(a) * rim, cy + std::sin(a) * rim, cx + std::cos(a) * rim * 1.42f, cy + std::sin(a) * rim * 1.42f, ink, stroke * 1.5f);
        }
        break;
    }
    case Role::pc_platforms: { // A monitor on its stand.
        const float w = size * 0.95f, h = size * 0.62f, top = cy - h * 0.62f;
        target_->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(cx - w * 0.5f, top, cx + w * 0.5f, top + h), size * 0.06f, size * 0.06f), solid_.Get(), stroke);
        line(cx, top + h, cx, top + h + size * 0.18f, ink, stroke);
        line(cx - size * 0.24f, top + h + size * 0.2f, cx + size * 0.24f, top + h + size * 0.2f, ink, stroke);
        break;
    }
    case Role::emulation: {
        const float span = size * 1.15f;
        drawControllerIcon(cx - span * 0.5f, cy - span * 0.5f, span, ink);
        break;
    }
    case Role::glance: drawGlanceGlyph(cx, cy, size * 0.95f, ink); break;
    case Role::tarot: drawTarotGlyph(cx, cy, size * 1.1f, ink); break;
    case Role::add: { // Dodaj grę: a plus in a ring.
        target_->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), size * 0.5f, size * 0.5f), solid_.Get(), stroke);
        line(cx - size * 0.24f, cy, cx + size * 0.24f, cy, ink, stroke * 1.2f);
        line(cx, cy - size * 0.24f, cx, cy + size * 0.24f, ink, stroke * 1.2f);
        break;
    }
    default: break;
    }
}

void Renderer::drawMetroTile(const GameCard& card, size_t index) {
    using metro_layout::Role;
    if (index >= context_->metro_tiles.size()) return;
    const auto& info = context_->metro_tiles[index];
    const float w = card.width(), h = card.size;
    if (card.opacity < 0.01f || card.x > viewport_.width + 20 || card.x + w < -20 || card.y > viewport_.height + 20 || card.y + h < -20) return;
    const bool focused = index == selected_;
    // `card.emphasis` eases with the selection: the focused tile grows a little, the previous settles.
    const float emphasis = card.emphasis;
    const float launch = focused ? smoothFade(context_->launch_progress) : 0.0f;
    const float scale = 1.0f + 0.035f * emphasis + 0.06f * launch;
    const float tile_w = w * scale, tile_h = h * scale;
    const float x = card.x - (tile_w - w) * 0.5f, y = card.y - (tile_h - h) * 0.5f;
    const auto frame_rect = D2D1::RectF(x, y, x + tile_w, y + tile_h);
    const auto frame_shape = tileShape(frame_rect, tile_shape_);
    if (!frame_shape) return;
    // "Grafika do środka" (focus gap inside): the focused tile's content shrinks inside its outline.
    const float inner = focused ? focusInset(3.2f) * emphasis : 0.0f;
    const auto rect = inner > 0.05f ? D2D1::RectF(x + inner, y + inner, x + tile_w - inner, y + tile_h - inner) : frame_rect;
    auto shape = frame_shape;
    if (inner > 0.05f) if (auto inside = tileShape(rect, tile_shape_)) shape = inside;
    // The pages beside the current one recede (dimmed with the eased page position), so it is clear
    // which page the arrows act on even without the 3D effect.
    const float away = std::clamp(std::abs(static_cast<float>(info.page) - context_->metro_tab), 0.0f, 1.0f);
    const float transparency = card.opacity * (0.9f + 0.1f * emphasis) * (1.0f - 0.55f * away);
    for (int layer = 6; layer >= 1; --layer) { // Half steps: wide ones showed as bands on bright pictures (#23).
        const float spread = layer * 0.5f * (3.0f + 2.0f * emphasis);
        brush(D2D1::ColorF(0, 0, 0, 0.5f * (0.045f + 0.025f * emphasis) * transparency * (metro_light_ ? 0.45f : 1.0f)));
        const auto shadow = tileShape(D2D1::RectF(x - spread, y + 8 - spread, x + tile_w + spread, y + tile_h + 8 + spread), tile_shape_);
        if (shadow) target_->FillGeometry(shadow.Get(), solid_.Get());
    }
    if (inner > 0.05f) { // The empty band: almost black (white on the light page), under the content.
        brush(metro_light_ ? D2D1::ColorF(0.97f, 0.97f, 0.975f, 0.94f * transparency) : D2D1::ColorF(0.012f, 0.014f, 0.018f, 0.94f * transparency));
        target_->FillGeometry(frame_shape.Get(), solid_.Get());
    }
    const auto& game = card.game;
    const auto role = info.role;
    const auto session = sessionOf(game);
    const float status_fade = game.installation.active() ? 1.0f : statusFade(game);
    const bool frozen = session == SessionState::frozen;
    Cover* poster = index < posters_.size() && posters_[index].bitmap ? &posters_[index] : nullptr;
    Cover* cover = index < covers_.size() && covers_[index].bitmap ? &covers_[index] : nullptr;
    const bool decoding = (index < posters_.size() && posters_[index].decoding) || (index < covers_.size() && covers_[index].decoding);
    D2D1_COLOR_F accent = cover_accent_ ? D2D1::ColorF(((cover_accent_ >> 16) & 255) / 255.0f, ((cover_accent_ >> 8) & 255) / 255.0f, (cover_accent_ & 255) / 255.0f) :
                          poster ? poster->accent : cover ? cover->accent : D2D1::ColorF(0.94f, 0.96f, 0.97f);
    if (frozen) accent = D2D1::ColorF(0.80f, 0.82f, 0.84f);
    // Small tiles keep their glyph higher, clear of the caption and count under it.
    const float glyph_size = std::min(tile_w, tile_h) * (tile_h < 200 ? 0.26f : 0.3f);
    const float glyph_y = y + tile_h * (tile_h < 200 ? 0.34f : 0.42f);
    bool caption = true;
    if (metro_layout::systemTile(role)) {
        // Shortcut tiles: a diagonal gradient of their own colour, lighter at the top-left corner,
        // with a soft sheen while focused.
        const size_t gradient = role == Role::library ? 0 : role == Role::pc_platforms ? 1 : role == Role::emulation ? 2 :
                                role == Role::glance ? 4 : role == Role::companion ? 5 : role == Role::tarot ? 2 : 3;
        if (auto* fill = metro_gradients_[gradient].Get()) {
            fill->SetStartPoint(D2D1::Point2F(x, y));
            fill->SetEndPoint(D2D1::Point2F(x + tile_w, y + tile_h));
            fill->SetOpacity(transparency);
            target_->FillGeometry(shape.Get(), fill);
        } else {
            brush(mix(D2D1::ColorF(0.05f, 0.06f, 0.07f), uiAccent(), 0.42f, transparency));
            target_->FillGeometry(shape.Get(), solid_.Get());
        }
        if (emphasis > 0.004f) {
            brush(D2D1::ColorF(1, 1, 1, 0.08f * emphasis * transparency));
            target_->FillGeometry(shape.Get(), solid_.Get());
        }
        if (role == Role::companion && context_->companion) {
            // Her close-up on the tile's right, the caption on the left; she stays inside the tile.
            const float figure = tile_h * 1.14f;
            const float mouth = drawCompanionInTile(shape.Get(), x + tile_w - figure * 0.52f, y + tile_h + figure * 0.1f, figure, transparency);
            companion_tile_mouth_ = TileMouth{rect, mouth, transparency};
        } else drawMetroGlyph(role, x + tile_w * 0.5f, glyph_y, glyph_size, gray(0.97f, transparency));
        accent = D2D1::ColorF(0.97f, 0.97f, 0.97f);
    } else if (role == Role::logo) {
        brush(platformTileFill(game, transparency));
        target_->FillGeometry(shape.Get(), solid_.Get());
        if (cover) {
            const auto pixels = cover->bitmap->GetPixelSize();
            const auto source = cover->has_ink_bounds ? cover->ink_bounds : D2D1::RectF(0, 0, static_cast<float>(pixels.width), static_cast<float>(pixels.height));
            const float factor = std::min(tile_w * 0.66f / (source.right - source.left), tile_h * 0.42f / (source.bottom - source.top));
            const float lw = (source.right - source.left) * factor, lh = (source.bottom - source.top) * factor;
            target_->DrawBitmap(cover->bitmap.Get(), D2D1::RectF(x + (tile_w - lw) * 0.5f, y + (tile_h - lh) * 0.5f, x + (tile_w + lw) * 0.5f, y + (tile_h + lh) * 0.5f),
                                transparency * card.cover_opacity * reveal(*cover), D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, &source);
        } else if (!decoding)
            text(game.name, x + 12, y + tile_h * 0.5f - 24, tile_w - 24, 48, category_font_.Get(), gray(0.94f, transparency));
        caption = false; // The logo is the caption.
    } else if (role == Role::all) {
        // A category's "Wszystkie": a random game's background under the glyph.
        brush(D2D1::ColorF(0.06f, 0.07f, 0.08f, transparency));
        target_->FillGeometry(shape.Get(), solid_.Get());
        if (poster) {
            fillImage(*poster, rect, 0, transparency * reveal(*poster), false, shape.Get());
            brush(D2D1::ColorF(0.008f, 0.011f, 0.015f, 0.5f * transparency * reveal(*poster)));
            target_->FillGeometry(shape.Get(), solid_.Get());
        }
        drawMetroGlyph(Role::all, x + tile_w * 0.5f, glyph_y, glyph_size * 0.8f, gray(0.95f, transparency));
    } else {
        constexpr float palette[4][3] = {{0.13f, 0.21f, 0.24f}, {0.20f, 0.20f, 0.30f}, {0.28f, 0.20f, 0.17f}, {0.15f, 0.24f, 0.20f}};
        const int tone = static_cast<int>(game.app_id % 4);
        brush(D2D1::ColorF(palette[tone][0], palette[tone][1], palette[tone][2], transparency));
        target_->FillGeometry(shape.Get(), solid_.Get());
        // The shaped art (landscape, poster or the background meanwhile) fades in over the cover.
        const float shown = poster ? reveal(*poster) : 0.0f;
        if (cover && shown < 1) fillImage(*cover, rect, 0, transparency * card.cover_opacity * reveal(*cover), frozen, shape.Get());
        if (poster) fillImage(*poster, rect, 0, transparency * shown, frozen, shape.Get());
        if (!poster && !cover && !decoding)
            text(game.name, x + 14, y + tile_h * 0.5f - 24, tile_w - 28, 48, button_font_.Get(), gray(0.92f, transparency));
        if (frozen) {
            brush(D2D1::ColorF(0.015f, 0.018f, 0.022f, 0.34f * transparency));
            target_->FillGeometry(shape.Get(), solid_.Get());
        }
        if (!game.installed && game.kind != GameKind::catalog && !game.installation.active()) {
            brush(D2D1::ColorF(0, 0, 0, 0.32f * transparency));
            target_->FillGeometry(shape.Get(), solid_.Get());
        }
    }
    const bool status = game.installation.active() || session != SessionState::none;
    if (status) drawCardStatusShade(x, y, tile_w, tile_h, transparency * status_fade, shape.Get());
    if (game.installation.active()) {
        const auto& install = game.installation;
        const float radius = std::clamp(std::min(tile_w, tile_h) * 0.064f, 11.0f, 22.0f);
        drawInstallDisc(x + tile_w - radius - 17, y + radius + 17, radius, transparency, install.moving());
        const auto percent = install.percent();
        drawCardStatus(x, y, tile_w, tile_h, transparency, installationText(install), percent ? *percent / 100.0f : 0.0f, uiAccent(transparency));
        // The start page's download tile names its game above the progress.
        if (role == Role::install || metro_layout::landscape(role))
            textLine(info.caption, x + 17, y + tile_h - 82, tile_w - 34, menu_font_.Get(), gray(0.98f, transparency));
    } else if (session != SessionState::none) {
        drawCardBadge(x, y, tile_w, tile_h, transparency * status_fade, frozen);
        drawCardStatus(x, y, tile_w, tile_h, transparency * status_fade, frozen ? tr(L"Wstrzymana") : tr(L"W grze"));
    } else if (caption) {
        // Posters show their name only in focus; the other tiles always carry it.
        const bool art_tile = !metro_layout::systemTile(role) && role != Role::all;
        const bool cornered = context_->metro_title_corner && art_tile && isGame(game);
        const float shown = cornered ? (role == Role::game ? 0.0f : 1.0f - emphasis) : role == Role::game ? emphasis : 1.0f;
        if (shown > 0.01f) {
            const bool art = !metro_layout::systemTile(role) && role != Role::all;
            if (art) drawCardStatusShade(x, y, tile_w, tile_h, transparency * shown, shape.Get());
            const float left = x + 16, width = tile_w - 32, bottom = y + tile_h - 16;
            auto* title_font = role == Role::recent_large ? category_font_.Get() : menu_font_.Get();
            if (!info.sub.empty()) {
                textLine(info.sub, left, bottom - 6, width, settings_body_font_.Get(), gray(0.82f, transparency * shown));
                textLine(info.caption, left, bottom - 32, width, title_font, gray(0.98f, transparency * shown));
            } else textLine(info.caption, left, bottom - 8, width, title_font, gray(0.98f, transparency * shown));
            if (!game.installed && isGame(game) && game.kind != GameKind::catalog && role == Role::game)
                textLine(localFileGame(game.source) ? missingFileBadge(game) : tr(L"DO INSTALACJI"), left, y + 24, width, button_font_.Get(), gray(0.95f, transparency * shown));
        }
    }
    // The accent outline fades into the plain edge of the other tiles as `emphasis` drops; with the
    // focus gap it stands an empty ring away from the tile.
    // On the light page a white outline would vanish: it turns dark.
    if (metro_light_ && 0.2126f * accent.r + 0.7152f * accent.g + 0.0722f * accent.b > 0.75f) accent = D2D1::ColorF(0.08f, 0.09f, 0.11f);
    const float ring = context_->focus_ring ? 0.0f : emphasis;
    auto outline = frame_shape;
    if (ring > 0.004f) if (const float out = focusOutset(3.2f); out > 0)
        if (auto wider = tileShape(D2D1::RectF(frame_rect.left - out, frame_rect.top - out, frame_rect.right + out, frame_rect.bottom + out), tile_shape_)) outline = wider;
    if (ring > 0.004f && glow_trace_) {
        drawOrbitOutline(outline.Get(), accent, ring * transparency);
    } else if (ring > 0.004f) {
        const float pulse = glowPulse();
        if (glowAlpha() > 0.004f) {
            brush(D2D1::ColorF(accent.r, accent.g, accent.b, std::min(1.0f, ring * transparency * glowAlpha() * (0.045f + pulse * 0.14f))));
            target_->DrawGeometry(outline.Get(), solid_.Get(), glowWidth(13.0f));
        }
        brush(D2D1::ColorF(accent.r, accent.g, accent.b, ring * transparency * std::min(1.0f, 0.665f + pulse * 0.40f)));
        target_->DrawGeometry(outline.Get(), solid_.Get(), 3.2f);
    }
    if (ring < 0.996f) {
        brush(metro_light_ ? gray(0, (1.0f - ring) * 0.12f * transparency) : gray(1, (1.0f - ring) * 0.14f * transparency));
        target_->DrawGeometry(frame_shape.Get(), solid_.Get(), 1.0f);
    }
    if (focused && context_->focus_ring && emphasis > 0.004f) drawFocusRing(accent, emphasis * transparency, scale, false);
}
