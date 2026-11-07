// Flix home layout (src/app/flix.cpp): the focused game's background, its logo above the
// resting first row, then the rows of capsules and posters with their titles.
#include "renderer.hpp"
#include "renderer_internal.hpp"
#include "flix_layout.hpp"

#include <algorithm>
#include <cmath>

using Microsoft::WRL::ComPtr;

namespace {
using palette::gray;
using render_detail::smoothFade;
constexpr float tile_radius = 12;
}

void Renderer::drawFlix() {
    const auto& cards = *cards_;
    const auto& rows = context_->flix_rows;
    const float width = viewport_.width, height = viewport_.height;
    drawBackground();
    // The page rises as a lower row takes focus: the background dims under the posters.
    const float first_top = rows.empty() || rows[0].begin >= cards.size() ? 0.0f : cards[rows[0].begin].y - flix_layout::header;
    const float scale = context_->flix_scale;
    const float lift = flix_layout::lift(first_top, height, scale);
    brush(D2D1::ColorF(0.008f, 0.011f, 0.015f, 0.18f + 0.55f * smoothFade(lift)));
    target_->FillRectangle(D2D1::RectF(0, 0, width, height), solid_.Get());
    beginScreenForeground();
    // The focused game's logo (or title) above the resting first row.
    const float rest_alpha = (1.0f - smoothFade(lift * 3.0f)) * (1.0f - smoothFade(context_->quick_progress)) * smoothFade(detail_) *
                             (1.0f - smoothFade(context_->category_menu_progress));
    if (rest_alpha > 0.01f && selected_ < cards.size() && !gameKey(cards[selected_].game).empty()) {
        const float bottom = first_top - 34;
        const auto& logo = logos_[selected_];
        if (logo.bitmap) {
            const auto size = logo.bitmap->GetPixelSize();
            const auto source = logo.has_ink_bounds ? logo.ink_bounds : D2D1::RectF(0, 0, static_cast<float>(size.width), static_cast<float>(size.height));
            const float ink_w = source.right - source.left, ink_h = source.bottom - source.top;
            const float factor = std::min(520.0f / ink_w, 170.0f / ink_h);
            target_->DrawBitmap(logo.bitmap.Get(), D2D1::RectF(flix_layout::leftEdge(), bottom - ink_h * factor, flix_layout::leftEdge() + ink_w * factor, bottom),
                                rest_alpha * reveal(logo), D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, &source);
        } else if (!context_->logo_pending && !logo.decoding)
            text(cards[selected_].game.name, flix_layout::leftEdge(), bottom - 80, width - 2 * flix_layout::leftEdge(), 80, hero_font_.Get(), gray(0.95f, rest_alpha));
    }
    drawQuickShade();
    const auto measure = [&](const std::wstring& value) {
        Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
        DWRITE_TEXT_METRICS metrics{};
        if (SUCCEEDED(dwrite_->CreateTextLayout(value.c_str(), static_cast<UINT32>(value.size()), category_font_.Get(), width, 48, layout.GetAddressOf())) &&
            SUCCEEDED(layout->GetMetrics(&metrics))) return metrics.widthIncludingTrailingWhitespace;
        return 0.0f;
    };
    const float focus_zone = flix_layout::focus_top + flix_layout::header + 60;
    for (size_t r = 0; r < rows.size(); ++r) {
        const auto& row = rows[r];
        if (row.begin >= cards.size() || !row.count) continue;
        const float tile_top = cards[row.begin].y, tile_height = flix_layout::tileHeight(row.landscape, scale);
        if (tile_top > height + 10 || tile_top + tile_height < -flix_layout::header) continue;
        // Rows scrolled above the focused one fade away under the top edge.
        const float alpha = smoothFade((tile_top + tile_height - 30) / focus_zone);
        if (alpha < 0.01f) continue;
        const bool focused_row = r == context_->flix_row;
        const float header_y = tile_top - flix_layout::header;
        textLine(row.title, flix_layout::leftEdge(), header_y + 22, width * 0.5f, category_font_.Get(), gray(focused_row ? 0.93f : 0.72f, alpha));
        if (focused_row && selected_ < cards.size() && isGame(cards[selected_].game)) {
            const float offset = flix_layout::leftEdge() + std::min(width * 0.5f, measure(row.title)) + 14;
            brush(uiAccent(0.8f * alpha));
            target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(offset, header_y + 22), 2.8f, 2.8f), solid_.Get());
            textLine(cards[selected_].game.name, offset + 16, header_y + 22, width - offset - 16 - flix_layout::right_margin,
                     category_font_.Get(), gray(0.98f, alpha));
        }
        // The unfolded tile is drawn over its row (a folding one too, after the focus moved on).
        const size_t unfolded = context_->flix_unfold > 0.0005f ? context_->flix_unfold_index : static_cast<size_t>(-1);
        const bool in_row = unfolded >= row.begin && unfolded < row.begin + row.count;
        for (size_t i = row.begin; i < row.begin + row.count && i < cards.size(); ++i)
            if (i != selected_ && i != unfolded) drawRectangularCard(cards[i], i, alpha);
        if (in_row && unfolded != selected_ && unfolded < cards.size()) drawFlixUnfolded(cards[unfolded], unfolded, alpha);
        if (focused_row && selected_ >= row.begin && selected_ < row.begin + row.count) {
            const float focus = smoothFade(context_->launch_progress);
            if (focus > 0.001f) {
                brush(D2D1::ColorF(0.008f, 0.012f, 0.016f, focus * 0.82f));
                target_->FillRectangle(D2D1::RectF(0, 0, width, height), solid_.Get());
            }
            if (selected_ == unfolded) drawFlixUnfolded(cards[selected_], selected_, alpha);
            else drawRectangularCard(cards[selected_], selected_, alpha);
        }
    }
    // More rows below the resting first one: a small chevron under it.
    if (rows.size() > 1 && rest_alpha > 0.01f) {
        const float cx = width * 0.5f, cy = height - 18;
        line(cx - 9, cy - 4, cx, cy + 3, gray(0.85f, 0.6f * rest_alpha), 2.0f);
        line(cx, cy + 3, cx + 9, cy - 4, gray(0.85f, 0.6f * rest_alpha), 2.0f);
    }
}

void Renderer::drawRectangularCard(const GameCard& card, size_t index, float alpha) {
    const float w = card.width(), h = card.size;
    if (card.opacity < 0.01f || card.x > viewport_.width + 20 || card.x + w < -20 || card.y > viewport_.height + 20 || card.y + h < -20) return;
    const bool focused = index == selected_ && (!context_->library_controls ||
        (context_->library_controls->focus < 0 && context_->library_controls->action < 0 && !context_->library_controls->open()));
    // The quick bar takes the focus: the focused tile settles to the others' size and loses its
    // outline while the bar is open, and comes back as it closes.
    // `card.emphasis` eases toward the selection (src/app/launcher.cpp), so a new focus grows its
    // tile while the previous one settles back, instead of both snapping in one frame.
    // Landscape capsules fill their tile (Kinowy, Przegląd, Siatka's and the search's landscape tiles).
    const bool cinematic = (library_view::landscape(context_->library_mode) && context_->screen == ScreenMode::library) ||
        (gridScreen(context_->screen) && context_->library_mode == library_view::View::grid && context_->library_options.grid_landscape);
    const bool xmb = context_->xmb && !cinematic;
    const float emphasis = xmb ? (focused ? 1.0f - smoothFade(context_->quick_progress) : 0.0f) : card.emphasis;
    const float launch = focused ? smoothFade(context_->launch_progress) : 0.0f;
    const float scale = xmb ? 1.0f + 0.13f * launch : 1.0f + 0.045f * emphasis + 0.08f * launch;
    const float tile_w = w * scale, tile_h = h * scale;
    const float x = card.x - (tile_w - w) * 0.5f, y = card.y - (tile_h - h) * 0.5f;
    // The tile's own edge: shadow, focus outline and plain edge.
    const auto frame_rect = D2D1::RectF(x, y, x + tile_w, y + tile_h);
    const auto frame_shape = tileShape(frame_rect, tile_shape_);
    if (!frame_shape) return;
    // "Grafika do środka" (focus gap inside): the focused picture shrinks inside its outline, an empty
    // band between them; everything else fills the whole tile.
    const float inner = focused ? focusInset(3.2f) * emphasis : 0.0f;
    const auto rect = inner > 0.05f ? D2D1::RectF(x + inner, y + inner, x + tile_w - inner, y + tile_h - inner) : frame_rect;
    ComPtr<ID2D1Geometry> shape = frame_shape;
    if (inner > 0.05f) if (auto inside = tileShape(rect, tile_shape_)) shape = inside;
    const float transparency = card.opacity * alpha * (context_->xmb ? 1.0f : 0.9f + 0.1f * emphasis);
    const auto& game = card.game;
    for (int layer = 3; layer >= 1; --layer) {
        const float spread = layer * (3.0f + 2.0f * emphasis);
        brush(D2D1::ColorF(0, 0, 0, (0.045f + 0.025f * emphasis) * transparency));
        const auto shadow = tileShape(D2D1::RectF(x - spread, y + 8 - spread, x + tile_w + spread, y + tile_h + 8 + spread), tile_shape_);
        if (shadow) target_->FillGeometry(shadow.Get(), solid_.Get());
    }
    const bool platform_tile = game.kind == GameKind::platform || game.kind == GameKind::pc_platform;
    constexpr float palette[4][3] = {{0.13f, 0.21f, 0.24f}, {0.20f, 0.20f, 0.30f}, {0.28f, 0.20f, 0.17f}, {0.15f, 0.24f, 0.20f}};
    const int tone = static_cast<int>(game.app_id % 4);
    if (inner > 0.05f) { // The empty band: almost black, under the picture.
        brush(D2D1::ColorF(0.012f, 0.014f, 0.018f, 0.94f * transparency));
        target_->FillGeometry(frame_shape.Get(), solid_.Get());
    }
    brush(platform_tile ? platformTileFill(game, transparency) :
          cinematic ? D2D1::ColorF(0.045f, 0.05f, 0.06f, transparency) :
          isGame(game) ? D2D1::ColorF(palette[tone][0], palette[tone][1], palette[tone][2], transparency) :
          D2D1::ColorF(0.07f, 0.08f, 0.09f, 0.94f * transparency));
    target_->FillGeometry(shape.Get(), solid_.Get());
    const auto session = sessionOf(game);
    const float status_fade = game.installation.active() ? 1.0f : statusFade(game);
    const bool frozen = session == SessionState::frozen;
    Cover* poster = index < posters_.size() && posters_[index].bitmap ? &posters_[index] : nullptr;
    Cover* cover = index < covers_.size() && covers_[index].bitmap ? &covers_[index] : nullptr;
    D2D1_COLOR_F accent = cover_accent_ ? D2D1::ColorF(((cover_accent_ >> 16) & 255) / 255.0f, ((cover_accent_ >> 8) & 255) / 255.0f, (cover_accent_ & 255) / 255.0f) :
                          poster ? poster->accent : cover ? cover->accent : D2D1::ColorF(0.94f, 0.96f, 0.97f);
    if (frozen) accent = D2D1::ColorF(0.80f, 0.82f, 0.84f);
    const bool perfect = perfectGame(game);
    if (perfect && !frozen) accent = perfect_gold; // Every achievement unlocked.
    if (platform_tile) {
        if (cover) {
            // Store and console logos, fitted inside the tile.
            const auto pixels = cover->bitmap->GetPixelSize();
            const auto source = cover->has_ink_bounds ? cover->ink_bounds : D2D1::RectF(0, 0, static_cast<float>(pixels.width), static_cast<float>(pixels.height));
            const float factor = std::min(tile_w * 0.7f / (source.right - source.left), tile_h * 0.4f / (source.bottom - source.top));
            const float lw = (source.right - source.left) * factor, lh = (source.bottom - source.top) * factor;
            target_->DrawBitmap(cover->bitmap.Get(), D2D1::RectF(x + (tile_w - lw) * 0.5f, y + (tile_h - lh) * 0.5f, x + (tile_w + lw) * 0.5f, y + (tile_h + lh) * 0.5f),
                                transparency * card.cover_opacity * reveal(*cover), D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, &source);
        } else if (!covers_[index].decoding)
            text(game.name, x + 12, y + tile_h * 0.5f - 24, tile_w - 24, 48, category_font_.Get(), gray(0.94f, transparency));
    } else if (game.kind == GameKind::all || game.kind == GameKind::category) {
        // "Wszystkie gry" and a row's "Wszystkie": the quick bar's 2x2 glyph and the caption under
        // it; a row's tile sits on a random background of its category, dimmed for the caption.
        if (poster) {
            fillImage(*poster, rect, tile_radius, transparency * reveal(*poster), false, shape.Get());
            brush(D2D1::ColorF(0.008f, 0.011f, 0.015f, 0.46f * transparency * reveal(*poster)));
            target_->FillGeometry(shape.Get(), solid_.Get());
        }
        const float cx = x + tile_w * 0.5f, cy = y + tile_h * 0.44f, cell = std::min(tile_w, tile_h) * 0.11f, gap = cell * 0.3f;
        brush(gray(0.94f, 0.9f * transparency));
        for (int tile = 0; tile < 4; ++tile) {
            const float left = tile % 2 ? cx + gap * 0.5f : cx - gap * 0.5f - cell, top = tile / 2 ? cy + gap * 0.5f : cy - gap * 0.5f - cell;
            target_->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(left, top, left + cell, top + cell), cell * 0.25f, cell * 0.25f), solid_.Get(), 2.2f);
        }
        text(game.name, x + 12, cy + cell + 18, tile_w - 24, 30, button_font_.Get(), gray(0.9f, transparency));
    } else if (game.kind == GameKind::glance || game.kind == GameKind::companion || game.kind == GameKind::tarot || game.kind == GameKind::add) {
        // Glance, Towarzyszka, Tarot and Dodaj grę as tiles of the first row: their glyph and name.
        const float side = std::min(tile_w, tile_h) * 0.72f;
        drawXmbTileIcon(card, index, x + (tile_w - side) * 0.5f, y + (tile_h - side) * 0.38f, side, transparency);
        text(game.name, x + 12, y + tile_h - 42, tile_w - 24, 30, button_font_.Get(), gray(0.9f, transparency));
    } else if (poster || cover) {
        // A stand-in cover of another shape (square, or a wide header) would lose its sides and often
        // its title to the tile's crop: shown whole over a dimmed fill of itself instead.
        const auto ratioOf = [](const Cover& image) {
            const auto pixels = image.bitmap->GetPixelSize();
            return static_cast<float>(pixels.width) / static_cast<float>(std::max(1u, pixels.height));
        };
        // Of the picture's own rectangle: the inner focus border (focus gap "inside") makes it a little wider.
        const float tile_ratio = (rect.right - rect.left) / std::max(1.0f, rect.bottom - rect.top);
        // Landscape tiles: the picture always spans the tile's width. A narrower one (a 616x353
        // capsule, a 16:9 screenshot) loses some of its top and bottom (by its crop focus), a wider
        // one (a hero) leaves bands above and below; the sides are never cut or banded.
        const auto fillWidth = [&](Cover& image, float opacity) {
            fillImage(image, rect, tile_radius, opacity, frozen, shape.Get(), ratioOf(image) >= tile_ratio);
        };
        const auto standIn = [&](Cover& image, float opacity) {
            const float ratio = ratioOf(image);
            if (std::abs(ratio / tile_ratio - 1) < 0.12f || (cinematic && ratio >= 1.5f)) {
                if (cinematic) fillWidth(image, opacity); else fillImage(image, rect, tile_radius, opacity, frozen, shape.Get());
                return;
            }
            // A square or portrait cover waiting for its capsule: whole, over a dimmed fill of itself.
            fillImage(image, rect, tile_radius, opacity, frozen, shape.Get());
            brush(D2D1::ColorF(0.008f, 0.011f, 0.015f, 0.62f * opacity)); target_->FillGeometry(shape.Get(), solid_.Get());
            fillImage(image, rect, tile_radius, opacity, frozen, shape.Get(), true);
        };
        const float shown = poster ? reveal(*poster) : 0.0f;
        // The poster fades in over the cover it replaces, never over the bare tile.
        if (cover && shown < 1) standIn(*cover, transparency * card.cover_opacity * reveal(*cover));
        if (poster) {
            if (cinematic) fillWidth(*poster, transparency * shown);
            else fillImage(*poster, rect, tile_radius, transparency * shown, frozen, shape.Get());
        }
    } else if (!(index < posters_.size() && posters_[index].decoding) && !(index < covers_.size() && covers_[index].decoding)) {
        text(game.name, x + 14, y + tile_h * 0.5f - 24, tile_w - 28, 48, button_font_.Get(), gray(0.92f, transparency));
    }
    // Kinowy: the focused tile turns into the game's video once it plays (Filmy w tle).
    if (focused && isGame(game) && context_->screen == ScreenMode::library && context_->library_mode == library_view::View::cinematic)
        drawVideoIn(rect, shape.Get(), transparency);
    if (isGame(game)) {
        if (frozen) {
            brush(D2D1::ColorF(0.015f, 0.018f, 0.022f, 0.34f * transparency));
            target_->FillGeometry(shape.Get(), solid_.Get());
        }
        if (!game.installed && game.kind != GameKind::catalog) {
            brush(D2D1::ColorF(0, 0, 0, 0.32f * transparency));
            target_->FillGeometry(shape.Get(), solid_.Get());
        }
    }
    if (game.installation.active() || session != SessionState::none)
        drawCardStatusShade(x, y, tile_w, tile_h, transparency * status_fade, shape.Get());
    // The accent outline fades into the plain edge of the other tiles as `emphasis` drops; with the
    // focus gap it stands an empty ring away from the tile.
    const float ring = context_->focus_ring ? 0.0f : emphasis;
    ComPtr<ID2D1Geometry> outline = frame_shape;
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
        brush(gray(1, (1.0f - ring) * 0.16f * transparency));
        target_->DrawGeometry(frame_shape.Get(), solid_.Get(), 1.0f);
    }
    if (perfect) {
        brush(D2D1::ColorF(perfect_gold.r, perfect_gold.g, perfect_gold.b, (0.92f - 0.5f * ring) * transparency));
        target_->DrawGeometry(frame_shape.Get(), solid_.Get(), 2.4f);
    }
    // Experimental spring focus: the outline on its own rectangle, zoomed like the tile.
    if (focused && context_->focus_ring && emphasis > 0.004f) drawFocusRing(accent, emphasis * transparency, scale, false);
    if (game.installation.active()) {
        const auto& install = game.installation;
        const float radius = std::clamp(std::min(tile_w, tile_h) * 0.064f, 11.0f, 22.0f);
        drawInstallDisc(x + tile_w - radius - 17, y + radius + 17, radius, transparency, install.moving());
        const auto percent = install.percent();
        drawCardStatus(x, y, tile_w, tile_h, transparency, installationText(install), percent ? *percent / 100.0f : 0.0f, uiAccent(transparency));
    } else if (session != SessionState::none) {
        drawCardBadge(x, y, tile_w, tile_h, transparency * status_fade, frozen);
        drawCardStatus(x, y, tile_w, tile_h, transparency * status_fade, frozen ? tr(L"Wstrzymana") : tr(L"W grze"));
    } else if (emphasis > 0.004f && isGame(game) && !game.installed && game.kind != GameKind::catalog && !cinematic) // Library tiles: the dimming says it.
        text(localFileGame(game.source) ? missingFileBadge(game) : tr(L"DO INSTALACJI"), x + 8, y + tile_h - 36, tile_w - 16, 28,
             button_font_.Get(), gray(0.95f, emphasis * transparency));
}

// The unfolded tile: over its upper part the game's background with its logo (its title without
// one), below on a dark panel its primary action, facts and genres. While it opens, the poster
// gives way to the background and the panel rises from the bottom edge.
void Renderer::drawFlixUnfolded(const GameCard& card, size_t index, float alpha) {
    const float w = card.width(), h = card.size;
    if (card.opacity < 0.01f || card.x > viewport_.width + 20 || card.x + w < -20 || card.y > viewport_.height + 20 || card.y + h < -20) return;
    const float open = smoothFade(context_->flix_unfold), transparency = card.opacity * alpha;
    const float x = card.x, y = card.y;
    const auto frame = D2D1::RectF(x, y, x + w, y + h);
    const auto shape = tileShape(frame, tile_shape_);
    if (!shape) return;
    const bool focused = index == selected_;
    const float emphasis = focused ? card.emphasis : 0.0f;
    for (int layer = 3; layer >= 1; --layer) { // A deeper shadow than the posters': it stands above them.
        const float spread = layer * (4.0f + 3.0f * open);
        brush(D2D1::ColorF(0, 0, 0, (0.06f + 0.04f * open) * transparency));
        if (const auto shadow = tileShape(D2D1::RectF(x - spread, y + 10 - spread, x + w + spread, y + h + 10 + spread), tile_shape_))
            target_->FillGeometry(shadow.Get(), solid_.Get());
    }
    brush(D2D1::ColorF(0.075f, 0.08f, 0.09f, transparency));
    target_->FillGeometry(shape.Get(), solid_.Get());
    Cover* poster = index < posters_.size() && posters_[index].bitmap ? &posters_[index] : nullptr;
    Cover* cover = index < covers_.size() && covers_[index].bitmap ? &covers_[index] : nullptr;
    Cover* hero = index < heroes_.size() && heroes_[index].bitmap ? &heroes_[index] : nullptr;
    Cover* logo = index < logos_.size() && logos_[index].bitmap ? &logos_[index] : nullptr;
    const float image_h = h * (1.0f - (1.0f - flix_layout::unfold_image) * open);
    const auto image = D2D1::RectF(x, y, x + w, y + image_h);
    target_->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), shape.Get()), nullptr);
    // The poster (or its square stand-in) first, the background over it as the card opens.
    Cover* still = poster ? poster : cover;
    const float background = hero ? open * reveal(*hero) : 0.0f;
    if (still && background < 0.999f) fillImage(*still, image, 0, transparency * reveal(*still));
    if (hero) fillImage(*hero, image, 0, transparency * background);
    // The picture's lower edge darkens into the panel, under the logo.
    ComPtr<ID2D1GradientStopCollection> stops;
    ComPtr<ID2D1LinearGradientBrush> shade;
    const D2D1_GRADIENT_STOP shade_stops[] = {{0, D2D1::ColorF(0.075f, 0.08f, 0.09f, 0)}, {1, D2D1::ColorF(0.075f, 0.08f, 0.09f, 0.92f * transparency * open)}};
    if (SUCCEEDED(target_->CreateGradientStopCollection(shade_stops, 2, stops.GetAddressOf())) &&
        SUCCEEDED(target_->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(D2D1::Point2F(0, y + image_h * 0.45f), D2D1::Point2F(0, y + image_h)),
                                                    stops.Get(), shade.GetAddressOf())))
        target_->FillRectangle(D2D1::RectF(x, y + image_h * 0.45f, x + w, y + image_h + 1), shade.Get());
    // The logo (or title) bottom-left on the picture, appearing with the card.
    const float content = transparency * smoothFade((open - 0.35f) / 0.65f);
    const float pad = 18;
    if (content > 0.01f) {
        if (logo) {
            const auto pixels = logo->bitmap->GetPixelSize();
            const auto source = logo->has_ink_bounds ? logo->ink_bounds : D2D1::RectF(0, 0, static_cast<float>(pixels.width), static_cast<float>(pixels.height));
            const float ink_w = source.right - source.left, ink_h = source.bottom - source.top;
            if (ink_w > 0 && ink_h > 0) {
                const float factor = std::min((w - 2 * pad) * 0.62f / ink_w, image_h * 0.42f / ink_h);
                const float bottom = y + image_h - 12;
                target_->DrawBitmap(logo->bitmap.Get(), D2D1::RectF(x + pad, bottom - ink_h * factor, x + pad + ink_w * factor, bottom),
                                    content * reveal(*logo), D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, &source);
            }
        } else textLine(card.game.name, x + pad, y + image_h - 30, w - 2 * pad, category_font_.Get(), gray(0.97f, content), DWRITE_TEXT_ALIGNMENT_LEADING);
    }
    target_->PopLayer();

    // The panel: primary action, facts, genres.
    if (content > 0.01f) {
        const float row1 = y + image_h + 26, row2 = row1 + 34, row3 = row2 + 26;
        const float r = 15, cx = x + pad + r;
        // What Enter / A does is the filled circle: the action (left) or the details (right),
        // so the card agrees with the button hint.
        const bool details = context_->flix_unfold_details;
        const D2D1_COLOR_F dark = D2D1::ColorF(0.05f, 0.055f, 0.06f, content);
        const D2D1_COLOR_F glyph_ink = details ? gray(0.95f, content) : dark;
        if (details) { brush(gray(1, 0.55f * content)); target_->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, row1), r, r), solid_.Get(), 1.6f); }
        else { brush(gray(0.97f, content)); target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, row1), r, r), solid_.Get()); }
        ComPtr<ID2D1PathGeometry> glyph;
        ComPtr<ID2D1GeometrySink> sink;
        if (SUCCEEDED(factory_->CreatePathGeometry(glyph.GetAddressOf())) && SUCCEEDED(glyph->Open(sink.GetAddressOf()))) {
            if (context_->flix_unfold_play) { // ▶
                sink->BeginFigure(D2D1::Point2F(cx - 4.5f, row1 - 7), D2D1_FIGURE_BEGIN_FILLED);
                sink->AddLine(D2D1::Point2F(cx + 7.5f, row1)); sink->AddLine(D2D1::Point2F(cx - 4.5f, row1 + 7));
            } else { // ↓: install
                sink->BeginFigure(D2D1::Point2F(cx - 7, row1 + 0.5f), D2D1_FIGURE_BEGIN_FILLED);
                sink->AddLine(D2D1::Point2F(cx + 7, row1 + 0.5f)); sink->AddLine(D2D1::Point2F(cx, row1 + 8));
            }
            sink->EndFigure(D2D1_FIGURE_END_CLOSED);
            if (SUCCEEDED(sink->Close())) {
                brush(glyph_ink);
                target_->FillGeometry(glyph.Get(), solid_.Get());
                if (!context_->flix_unfold_play) line(cx, row1 - 8, cx, row1 + 2, glyph_ink, 3.0f);
            }
        }
        textLine(context_->flix_unfold_action, cx + r + 12, row1, w * 0.5f, toast_font_.Get(), gray(0.95f, content), DWRITE_TEXT_ALIGNMENT_LEADING);
        // Right: a ring with a chevron, the way to its details.
        const float dx = x + w - pad - r;
        const D2D1_COLOR_F chevron = details ? dark : gray(0.95f, content);
        if (details) { brush(gray(0.97f, content)); target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(dx, row1), r, r), solid_.Get()); }
        else { brush(gray(1, 0.55f * content)); target_->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(dx, row1), r, r), solid_.Get(), 1.6f); }
        line(dx - 6, row1 - 2.5f, dx, row1 + 3.5f, chevron, 2.0f);
        line(dx, row1 + 3.5f, dx + 6, row1 - 2.5f, chevron, 2.0f);
        // Rating (in green), then year, playtime and platform.
        float fx = x + pad;
        if (!context_->flix_unfold_rating.empty()) {
            textLine(context_->flix_unfold_rating, fx, row2, w - 2 * pad, settings_body_font_.Get(), D2D1::ColorF(0.33f, 0.80f, 0.45f, content), DWRITE_TEXT_ALIGNMENT_LEADING);
            fx += textAdvance(context_->flix_unfold_rating, settings_body_font_.Get()) + 14;
        }
        std::wstring facts;
        for (const auto& fact : context_->flix_unfold_facts) facts += (facts.empty() ? L"" : L"   ") + fact;
        if (!facts.empty() && fx < x + w - pad) textLine(facts, fx, row2, x + w - pad - fx, settings_body_font_.Get(), gray(0.86f, content), DWRITE_TEXT_ALIGNMENT_LEADING);
        if (!context_->flix_unfold_genres.empty())
            textLine(context_->flix_unfold_genres, x + pad, row3, w - 2 * pad, settings_body_font_.Get(), gray(0.66f, content), DWRITE_TEXT_ALIGNMENT_LEADING);
    }

    // The focus outline in the accent, as on the other tiles; a plain edge otherwise.
    if (emphasis > 0.004f && !context_->focus_ring) {
        brush(uiAccent(emphasis * transparency * 0.9f));
        target_->DrawGeometry(shape.Get(), solid_.Get(), 3.0f);
    } else {
        brush(gray(1, 0.16f * transparency));
        target_->DrawGeometry(shape.Get(), solid_.Get(), 1.0f);
    }
}
