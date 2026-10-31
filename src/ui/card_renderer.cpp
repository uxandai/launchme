// Renderer: a game card (square cover, poster or platform tile) with its shadow, shade, focus
// outline (steady, pulsing or orbiting) and status (running, suspended, installing), and the
// image fill shared by the layouts.
#include "renderer.hpp"
#include "renderer_internal.hpp"
#include "carousel.hpp"
#include "category_rail.hpp"

#include <d2d1effects.h>

#include <algorithm>
#include <cmath>
#include <numbers>

using Microsoft::WRL::ComPtr;
using namespace render_detail;

D2D1_COLOR_F Renderer::platformTileFill(const Game& game, float alpha) const {
    if (game.kind == GameKind::platform && console_colors_ && !studioPlatform(game.platform)) return platformColor(game.platform, alpha);
    return color(0.045f, 0.05f, 0.06f, alpha);
}

void Renderer::drawSquareShadow(float x, float y, float size, bool selected, float transparency) {
    const auto base = canvasBase();
    for (int layer = 4; layer >= 1; --layer) {
        const float spread = static_cast<float>(layer) * (selected ? 4.0f : 2.0f);
        target_->SetTransform(D2D1::Matrix3x2F::Scale(size + spread * 2, size + spread * 2) *
                              D2D1::Matrix3x2F::Translation(x - spread, y + 10.0f - spread) * base);
        brush(color(0, 0, 0, selected ? 0.048f * transparency : 0.028f * transparency));
        fillSquircle(solid_.Get());
    }
    target_->SetTransform(base);
}

bool Renderer::perfectGame(const Game& game) const {
    return context_ && context_->perfect_games && isGame(game) && context_->perfect_games->contains(gameKey(game));
}

void Renderer::drawCard(const GameCard& card, size_t index) {
    // Siatka (and the search results) of posters or landscape capsules: shallower corners than a square.
    if ((context_->screen == ScreenMode::library || context_->screen == ScreenMode::search) && context_->library_mode == library_view::View::grid &&
        (context_->library_options.grid_posters || context_->library_options.grid_landscape)) {
        const float previous = squircle_corner_; squircle_corner_ = context_->library_options.grid_landscape ? 0.4f : 0.2f;
        drawRectangularCard(card, index, 1); squircle_corner_ = previous; return;
    }
    if (context_->xmb && context_->xmb_posters && isGame(card.game)) {
        drawRectangularCard(card, index, xmbTransparency(index, index == selected_));
        return;
    }
    if (context_->shelf_posters && context_->screen == ScreenMode::shelves && std::abs(card.aspect - 1.0f) > 0.01f) {
        // Shelves of posters: the same fade with distance from the focused card as the squares.
        const bool focused = index == selected_;
        drawRectangularCard(card, index, focused ? 1.0f : std::max(0.42f, 0.91f - 0.105f * std::abs(distance(index))));
        return;
    }
    const float view_width = viewport_.width;
    if (card.opacity < 0.01f || card.x + card.size < -45 || card.x > view_width + 45 ||
        card.y + card.size < -45 || card.y > viewport_.height + 45) return;
    const bool selected = index == selected_ && context_->screen != ScreenMode::details && context_->screen != ScreenMode::crop &&
        !context_->category_rail_focus &&
        (!context_->library_controls || context_->library_controls->focus < 0);
    const bool platform_tile = card.game.kind == GameKind::platform || card.game.kind == GameKind::pc_platform;
    const float focus_zoom = selected ? 1.0f + 0.13f * smoothFade(context_->launch_progress) : 1.0f;
    const float size = card.size * focus_zoom;
    const float x = card.x - (size - card.size) * 0.5f;
    const float y = card.y - (size - card.size) * 0.5f;
    const auto base = canvasBase();
    float rail_alpha = 1;
    bool rail_layer = false;
    if (context_->category_rail) {
        const float clip = category_rail::Layout{viewport_.width, viewport_.height}.contentClip();
        const float fade_end = clip + 32; // The resting selected cover starts here, fully opaque.
        rail_alpha = smoothFade((card.x + card.size - fade_end) / card.size);
        if (rail_alpha < 0.001f) return;
        if (rail_alpha < 0.999f && category_rail_mask_) {
            if (!category_rail_layer_) target_->CreateLayer(category_rail_layer_.GetAddressOf());
            if (category_rail_layer_) {
                // Fade the whole composite (art, shade, outline and shadow), with a soft
                // left edge so the viewport clip is never visible during the slide.
                category_rail_mask_->SetStartPoint(D2D1::Point2F(clip, 0));
                category_rail_mask_->SetEndPoint(D2D1::Point2F(fade_end, 0));
                target_->SetTransform(base);
                target_->PushLayer(D2D1::LayerParameters(D2D1::RectF(x - 20, y - 20, x + size + 20, y + size + 30),
                    nullptr, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE, D2D1::IdentityMatrix(), rail_alpha,
                    category_rail_mask_.Get()), category_rail_layer_.Get());
                rail_layer = true;
            }
        }
    }
    // PSfive: every tile of its row opaque over the bright background (no fade with distance).
    const float transparency = card.opacity * (rail_layer ? 1.0f : rail_alpha) *
                               (context_->psfive || context_->snitch ? 1.0f :
                               context_->xmb && context_->screen == ScreenMode::shelves ? xmbTransparency(index, selected) :
                               context_->screen == ScreenMode::details || context_->screen == ScreenMode::crop ? 1.0f : context_->screen != ScreenMode::shelves ?
                               (selected ? 1.0f : 0.86f) :
                               selected ? 1.0f : std::max(0.42f, 0.91f - 0.105f * std::abs(distance(index))));
    // PSfive casts every shadow first, under the whole row (drawPsfive): the focused tile's own
    // must not fall on its neighbours, which sit closer than on the shelves.
    if (!context_->psfive && !context_->snitch) drawSquareShadow(x, y, size, selected, transparency);
    // "Grafika do środka" (focus gap inside): the focused cover shrinks inside its outline, an empty
    // band between them (PSfive keeps its own frame outside the tile). It follows card.emphasis, which
    // eases with the focus: the new cover shrinks in while the previous one grows back to the edge
    // (as the rectangular tiles do) instead of both snapping in one frame.
    const bool may_inset = !context_->psfive && context_->screen != ScreenMode::details && context_->screen != ScreenMode::crop &&
        !context_->category_rail_focus && (!context_->library_controls || context_->library_controls->focus < 0);
    const float inner = may_inset ? focusInset(3.2f) * std::clamp(card.emphasis, 0.0f, 1.0f) * (1.0f - smoothFade(context_->quick_progress)) : 0.0f;
    if (inner > 0.05f) {
        target_->SetTransform(D2D1::Matrix3x2F::Scale(size, size) * D2D1::Matrix3x2F::Translation(x, y) * base);
        brush(color(0.012f, 0.014f, 0.018f, 0.94f * transparency));
        fillSquircle(solid_.Get());
    }
    target_->SetTransform(D2D1::Matrix3x2F::Scale(size - 2 * inner, size - 2 * inner) *
                          D2D1::Matrix3x2F::Translation(x + inner, y + inner) * base);
    const int palette = static_cast<int>(card.game.app_id % 4);
    constexpr float background[4][3] = {
        {0.13f, 0.21f, 0.24f}, {0.20f, 0.20f, 0.30f},
        {0.28f, 0.20f, 0.17f}, {0.15f, 0.24f, 0.20f}
    };
    brush(platform_tile ? platformTileFill(card.game, transparency) : isGame(card.game) ?
          color(background[palette][0], background[palette][1], background[palette][2], transparency) :
          color(0.007f, 0.010f, 0.013f, 0.90f * transparency));
    fillSquircle(solid_.Get());
    auto& cover = covers_[index];
    const auto session = sessionOf(card.game);
    if (cover.brush && cover.bitmap) {
        const float cover_opacity = card.cover_opacity * reveal(cover);
        const auto dimensions = cover.bitmap->GetPixelSize();
        if (platform_tile) {
            const auto source = cover.has_ink_bounds ? cover.ink_bounds :
                D2D1::RectF(0, 0, static_cast<float>(dimensions.width), static_cast<float>(dimensions.height));
            const float factor = std::min(0.78f / (source.right - source.left),
                                          0.51f / (source.bottom - source.top));
            const float w = (source.right - source.left) * factor;
            const float h = (source.bottom - source.top) * factor;
            target_->DrawBitmap(cover.bitmap.Get(),
                D2D1::RectF((1 - w) * 0.5f, (1 - h) * 0.5f,
                            (1 + w) * 0.5f, (1 + h) * 0.5f),
                transparency * cover_opacity, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, &source);
        } else {
            const float zoom = std::max(1.0f / dimensions.width, 1.0f / dimensions.height);
            const float w = dimensions.width * zoom, h = dimensions.height * zoom;
            // Cover/fill: the bitmap always spans 100% of the square in both axes.
            const float left = std::clamp(0.5f - cover.focus.x * w, 1.0f - w, 0.0f);
            const float top = std::clamp(0.5f - cover.focus.y * h, 1.0f - h, 0.0f);
            // A suspended game is shown in grayscale, like a paused console title.
            auto* fill = session == SessionState::frozen ? grayBrush(cover) : nullptr;
            if (!fill) fill = cover.brush.Get();
            fill->SetTransform(D2D1::Matrix3x2F::Scale(zoom, zoom) *
                               D2D1::Matrix3x2F::Translation(left, top));
            fill->SetOpacity(transparency * cover_opacity);
            fillSquircle(fill);
            if (shade_ && !context_->psfive && !context_->snitch) { // Clean artwork in the console rows.
                shade_->SetOpacity((selected ? 0.56f : 0.28f) * cover_opacity);
                fillSquircle(shade_.Get());
            }
            if (session == SessionState::frozen) {
                brush(color(0.015f, 0.018f, 0.022f, 0.34f * transparency * cover_opacity));
                fillSquircle(solid_.Get());
            }
            if (!card.game.installed && card.game.kind != GameKind::catalog && install_vignette_) {
                install_vignette_->SetOpacity(cover_opacity * transparency);
                fillSquircle(install_vignette_.Get());
            }
        }
    } else if (cover.decoding) {
        // Decoding takes a frame or two: keep the plain tile instead of flashing the title text.
    } else {
        // A missing cover is still a usable game, not a permanent loading skeleton.
        if (card.game.kind == GameKind::category || card.game.kind == GameKind::settings ||
            card.game.kind == GameKind::glance || card.game.kind == GameKind::companion ||
             ((context_->xmb || context_->psfive || context_->snitch) && card.game.kind == GameKind::all)) {
            drawXmbTileIcon(card, index, x, y, size, transparency);
            target_->SetTransform(D2D1::Matrix3x2F::Scale(size, size) * D2D1::Matrix3x2F::Translation(x, y) * base);
        } else if (card.game.kind == GameKind::all || card.game.kind == GameKind::back) {
            brush(color(0.96f, 0.96f, 0.96f, transparency * 0.88f));
            target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(0.5f, 0.5f), 0.033f, 0.033f),
                                 solid_.Get());
        } else {
            target_->SetTransform(base);
            text(card.game.name, x + 10, y + size * (card.game.kind == GameKind::platform ? 0.34f : 0.42f),
                 size - 20, 55, card.game.kind == GameKind::platform ? category_font_.Get() : button_font_.Get(),
                 color(0.94f, 0.94f, 0.94f, transparency));
            target_->SetTransform(D2D1::Matrix3x2F::Scale(size, size) *
                                  D2D1::Matrix3x2F::Translation(x, y) * base);
        }
    }
    if (card.game.installation.active() || session != SessionState::none) {
        target_->SetTransform(base);
        drawCardStatusShade(x, y, size, size, transparency);
        target_->SetTransform(D2D1::Matrix3x2F::Scale(size, size) * D2D1::Matrix3x2F::Translation(x, y) * base);
    }
    // The outline and the plain edge follow the tile's own edge again (not the inset picture).
    target_->SetTransform(D2D1::Matrix3x2F::Scale(size, size) * D2D1::Matrix3x2F::Translation(x, y) * base);
    const float pulse = glowPulse();
    const float color_reveal = card.cover_opacity;
    const auto tint = session == SessionState::frozen ? color(0.80f, 0.82f, 0.84f) : cover_accent_ ?
        color(((cover_accent_ >> 16) & 255) / 255.0f,
              ((cover_accent_ >> 8) & 255) / 255.0f,
              (cover_accent_ & 255) / 255.0f) :
        perfectGame(card.game) ? perfect_gold :
        card.game.kind == GameKind::platform && !studioPlatform(card.game.platform) ?
        (card.game.platform == L"PS3" ? color(0.66f, 0.66f, 0.70f) :
         platformColor(card.game.platform)) :
        color(std::lerp(0.94f, cover.accent.r, color_reveal),
              std::lerp(0.96f, cover.accent.g, color_reveal),
              std::lerp(0.97f, cover.accent.b, color_reveal));
    // The quick bar takes the focus: the selected cover keeps its place (its size is the layout's)
    // but its outline fades into the plain edge while the bar is open.
    // PSfive: the eased focus weight, which also lets the outline go while "Graj" / "…" have the focus.
    const float emphasis = context_->psfive ? (selected ? card.emphasis : 0.0f) :
                           selected ? 1.0f - smoothFade(context_->quick_progress) : 0.0f;
    // Experimental spring focus: the card keeps a plain edge, the outline is drawn on its own rectangle.
    // PSfive frames the focused tile outside its edge instead (drawPsfiveFrame) and has no plain edge.
    const float edge = context_->focus_ring || context_->psfive ? 0.0f : emphasis;
    const float outset = focusOutset(3.2f); // The focus gap: the outline an empty ring away.
    if (edge > 0.004f && (glow_trace_ || outset > 0)) {
        target_->SetTransform(base);
        const auto shape = tileShape(D2D1::RectF(x - outset, y - outset, x + size + outset, y + size + outset), tile_shape_);
        if (glow_trace_) drawOrbitOutline(shape.Get(), tint, edge * transparency);
        else drawCoverOutline(shape.Get(), tint, edge * transparency, true);
        target_->SetTransform(D2D1::Matrix3x2F::Scale(size, size) * D2D1::Matrix3x2F::Translation(x, y) * base);
    } else if (edge > 0.004f) {
        if (glowAlpha() > 0.004f) {
            brush(color(tint.r, tint.g, tint.b, std::min(1.0f, edge * transparency * glowAlpha() * (0.07f + pulse * 0.09f))));
            strokeSquircle(solid_.Get(), glowWidth(13.0f) / size);
            brush(color(tint.r, tint.g, tint.b, std::min(1.0f, edge * transparency * glowAlpha() * (0.20f + pulse * 0.21f))));
            strokeSquircle(solid_.Get(), glowWidth(7.0f) / size);
        }
        brush(color(tint.r, tint.g, tint.b, edge * transparency * (0.73f + pulse * 0.27f)));
        strokeSquircle(solid_.Get(), 3.2f / size);
    }
    if (edge < 0.996f && !context_->psfive) {
        brush(color(1, 1, 1, (1.0f - edge) * 0.30f * transparency));
        strokeSquircle(solid_.Get(), 1.0f / size);
    }
    if (perfectGame(card.game)) { // Every achievement unlocked: a gold edge.
        brush(color(perfect_gold.r, perfect_gold.g, perfect_gold.b, (0.92f - 0.5f * edge) * transparency));
        strokeSquircle(solid_.Get(), 2.4f / size);
    }
    target_->SetTransform(base);
    if (selected && context_->focus_ring && emphasis > 0.004f) drawFocusRing(tint, emphasis * transparency, focus_zoom, true);
    else if (selected && context_->psfive && emphasis > 0.004f) drawPsfiveFrame(x, y, size, tint, emphasis * transparency);
    if (card.game.installation.active()) {
        const auto& install = card.game.installation;
        const float radius = std::clamp(size * 0.064f, 11.0f, 22.0f);
        const float cx = x + size - radius - 17, cy = y + radius + 17;
        drawInstallDisc(cx, cy, radius, transparency, install.moving());
        const auto percent = install.percent();
        drawCardStatus(x, y, size, size, transparency, installationText(install), percent ? *percent / 100.0f : 0.0f, uiAccent(transparency));
    } else if (session != SessionState::none) {
        // Same language as a download: a corner badge and text over the bottom fade.
        const bool frozen = session == SessionState::frozen;
        drawCardBadge(x, y, size, size, transparency, frozen);
        drawCardStatus(x, y, size, size, transparency, frozen ? tr(L"Wstrzymana") : tr(L"W grze"));
    } else if (selected && !card.game.installed && card.game.kind != GameKind::catalog && !context_->snitch)
        text(localFileGame(card.game.source) ? missingFileBadge(card.game) : tr(L"DO INSTALACJI"), x + 8, y + size - 33, size - 16, 28,
             button_font_.Get(), color(0.95f, 0.95f, 0.95f, transparency));
    if (rail_layer) target_->PopLayer();
}

void Renderer::drawOrbitOutline(ID2D1Geometry* shape, D2D1_COLOR_F tint, float alpha, float width) {
    if (!shape || alpha < 0.004f) return;
    brush(color(tint.r, tint.g, tint.b, 0.16f * alpha));
    target_->DrawGeometry(shape, solid_.Get(), width * 0.45f);
    float perimeter = 0;
    if (FAILED(shape->ComputeLength(nullptr, &perimeter)) || perimeter <= 0) return;
    // A long, continuously tapered ribbon instead of stacked dashes with visible opacity steps.
    // Sampling by arc length keeps the motion and taper smooth through corners and the seam.
    constexpr float span = 0.44f, period = 12.0f;
    const float phase = std::fmod(elapsed_ / period, 1.0f);
    constexpr int samples = 128;
    std::array<D2D1_POINT_2F, samples + 1> outside{}, inside{};
    for (int i = 0; i <= samples; ++i) {
        const float t = static_cast<float>(i) / samples;
        const float at = std::fmod(phase + 1.0f - span + span * t, 1.0f) * perimeter;
        D2D1_POINT_2F point{}, tangent{};
        if (FAILED(shape->ComputePointAtLength(at, nullptr, &point, &tangent))) return;
        const float half = width * 0.95f * smoothFade(t / 0.7f) * smoothFade((1.0f - t) / 0.2f);
        outside[i] = D2D1::Point2F(point.x - tangent.y * half, point.y + tangent.x * half);
        inside[i] = D2D1::Point2F(point.x + tangent.y * half, point.y - tangent.x * half);
    }
    ComPtr<ID2D1PathGeometry> ribbon;
    ComPtr<ID2D1GeometrySink> sink;
    if (FAILED(factory_->CreatePathGeometry(ribbon.GetAddressOf())) || FAILED(ribbon->Open(sink.GetAddressOf()))) return;
    sink->BeginFigure(outside[0], D2D1_FIGURE_BEGIN_FILLED);
    for (int i = 1; i <= samples; ++i) sink->AddLine(outside[i]);
    for (int i = samples; i >= 0; --i) sink->AddLine(inside[i]);
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    if (FAILED(sink->Close())) return;
    D2D1_RECT_F bounds{};
    if (FAILED(shape->GetBounds(nullptr, &bounds))) return;
    // The crest catches more light as it turns a corner, then settles along a straight side.
    // Position-based smooth falloff also works for square tiles, whose tangent changes abruptly.
    constexpr int crest = samples * 3 / 4;
    const float cx = (outside[crest].x + inside[crest].x) * 0.5f;
    const float cy = (outside[crest].y + inside[crest].y) * 0.5f;
    const float dx = std::min(std::abs(cx - bounds.left), std::abs(bounds.right - cx));
    const float dy = std::min(std::abs(cy - bounds.top), std::abs(bounds.bottom - cy));
    const float reach = std::max(1.0f, std::min(bounds.right - bounds.left, bounds.bottom - bounds.top) * 0.42f);
    const float corner = smoothFade(1.0f - std::hypot(dx, dy) / reach);
    const float brightness = 0.82f + 0.46f * corner;
    const float white = 0.5f + 0.18f * corner;
    const auto light = color(std::lerp(tint.r, 1.0f, white), std::lerp(tint.g, 1.0f, white), std::lerp(tint.b, 1.0f, white),
                             std::min(1.0f, 0.86f * alpha * brightness));
    const auto fallback = [&] { brush(light); target_->FillGeometry(ribbon.Get(), solid_.Get()); };
    if (!realizer_) { fallback(); return; }

    // Render only the light into a reusable transparent surface, then apply actual Gaussian
    // blur. The separate target also works while the main scene has a tile/row clip pushed.
    constexpr float padding = 32;
    const auto origin = D2D1::Point2F(std::floor(bounds.left - padding), std::floor(bounds.top - padding));
    const float needed_w = std::ceil((bounds.right + padding - origin.x) / 64) * 64;
    const float needed_h = std::ceil((bounds.bottom + padding - origin.y) / 64) * 64;
    const auto available = orbit_target_ ? orbit_target_->GetSize() : D2D1::SizeF(0, 0);
    if (available.width < needed_w || available.height < needed_h) {
        orbit_ink_.Reset(); orbit_target_.Reset();
        if (FAILED(target_->CreateCompatibleRenderTarget(D2D1::SizeF(std::max(available.width, needed_w),
                std::max(available.height, needed_h)), orbit_target_.GetAddressOf()))) { fallback(); return; }
    }
    if (!orbit_ink_ && FAILED(orbit_target_->CreateSolidColorBrush(light, orbit_ink_.GetAddressOf()))) { fallback(); return; }
    if (!orbit_blur_ && FAILED(realizer_->CreateEffect(CLSID_D2D1GaussianBlur, orbit_blur_.GetAddressOf()))) { fallback(); return; }
    orbit_ink_->SetColor(light);
    orbit_target_->BeginDraw();
    orbit_target_->Clear(color(0, 0, 0, 0));
    orbit_target_->SetTransform(D2D1::Matrix3x2F::Translation(-origin.x, -origin.y));
    orbit_target_->FillGeometry(ribbon.Get(), orbit_ink_.Get());
    const auto rendered = orbit_target_->EndDraw();
    ComPtr<ID2D1Bitmap> bitmap;
    if (FAILED(rendered) || FAILED(orbit_target_->GetBitmap(bitmap.GetAddressOf()))) { fallback(); return; }
    orbit_blur_->SetInput(0, bitmap.Get());
    orbit_blur_->SetValue(D2D1_GAUSSIANBLUR_PROP_BORDER_MODE, D2D1_BORDER_MODE_SOFT);
    if (glowAlpha() > 0.004f) {
        orbit_blur_->SetValue(D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION, 7.0f);
        realizer_->PushLayer(D2D1::LayerParameters1(D2D1::InfiniteRect(), nullptr, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
            D2D1::IdentityMatrix(), std::min(1.0f, glowAlpha() * 0.6f)), nullptr);
        realizer_->DrawImage(orbit_blur_.Get(), origin);
        realizer_->PopLayer();
    }
    orbit_blur_->SetValue(D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION, 2.0f);
    realizer_->DrawImage(orbit_blur_.Get(), origin);
    orbit_blur_->SetInput(0, nullptr);
    // A thin, antialiased core gives the soft ribbon definition. It shares the continuous
    // taper and centreline, so it never turns into a hard-ended dash or a second offset rim.
    ComPtr<ID2D1PathGeometry> core;
    ComPtr<ID2D1GeometrySink> core_sink;
    if (FAILED(factory_->CreatePathGeometry(core.GetAddressOf())) || FAILED(core->Open(core_sink.GetAddressOf()))) return;
    const auto inset = [&](int i, float mix) {
        return D2D1::Point2F(std::lerp(outside[i].x, inside[i].x, mix), std::lerp(outside[i].y, inside[i].y, mix));
    };
    core_sink->BeginFigure(inset(0, 0.35f), D2D1_FIGURE_BEGIN_FILLED);
    for (int i = 1; i <= samples; ++i) core_sink->AddLine(inset(i, 0.35f));
    for (int i = samples; i >= 0; --i) core_sink->AddLine(inset(i, 0.65f));
    core_sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    if (FAILED(core_sink->Close())) return;
    const float core_white = 0.7f + 0.13f * corner;
    brush(color(std::lerp(tint.r, 1.0f, core_white), std::lerp(tint.g, 1.0f, core_white), std::lerp(tint.b, 1.0f, core_white),
                std::min(1.0f, 0.64f * alpha * brightness)));
    target_->FillGeometry(core.Get(), solid_.Get());
}

void Renderer::drawFocusRing(D2D1_COLOR_F tint, float transparency, float zoom, bool middle_glow) {
    const auto& ring = *context_->focus_ring;
    const float out = focusOutset(3.2f); // The focus gap: an empty ring between the outline and the tile.
    const float w = ring.width * zoom + 2 * out, h = ring.height * zoom + 2 * out;
    if (w < 4 || h < 4 || transparency < 0.004f) return;
    const float x = ring.x - (w - ring.width) * 0.5f, y = ring.y - (h - ring.height) * 0.5f;
    const auto shape = tileShape(D2D1::RectF(x, y, x + w, y + h), tile_shape_);
    if (!shape) return;
    if (ring.glow > 0.004f && ring.glow_direction) {
        // Inner glow from the side of the hit edge, fading a third of the way in; the gradient
        // clamps to transparent, so filling the whole shape clips it to the outline.
        const float depth_x = w * 0.34f, depth_y = h * 0.34f;
        D2D1_POINT_2F from{}, to{};
        switch (ring.glow_direction) {
        case 1: from = {x + w, y}; to = {x + w - depth_x, y}; break;
        case -1: from = {x, y}; to = {x + depth_x, y}; break;
        case 2: from = {x, y + h}; to = {x, y + h - depth_y}; break;
        default: from = {x, y}; to = {x, y + depth_y}; break;
        }
        const float strength = smoothFade(ring.glow) * transparency;
        const auto light = color(std::lerp(tint.r, 1.0f, 0.5f), std::lerp(tint.g, 1.0f, 0.5f), std::lerp(tint.b, 1.0f, 0.5f));
        const D2D1_GRADIENT_STOP stops[] = {{0.0f, color(light.r, light.g, light.b, 0.78f * strength)},
                                            {0.45f, color(light.r, light.g, light.b, 0.2f * strength)},
                                            {1.0f, color(light.r, light.g, light.b, 0.0f)}};
        ComPtr<ID2D1GradientStopCollection> collection;
        ComPtr<ID2D1LinearGradientBrush> glow;
        if (SUCCEEDED(target_->CreateGradientStopCollection(stops, 3, collection.GetAddressOf())) &&
            SUCCEEDED(target_->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(from, to), collection.Get(), glow.GetAddressOf())))
            target_->FillGeometry(shape.Get(), glow.Get());
    }
    drawCoverOutline(shape.Get(), tint, transparency, middle_glow);
}

void Renderer::drawCoverOutline(ID2D1Geometry* shape, D2D1_COLOR_F tint, float transparency, bool middle_glow) {
    if (!shape) return;
    if (glow_trace_) { drawOrbitOutline(shape, tint, transparency); return; }
    const float pulse = glowPulse();
    if (glowAlpha() > 0.004f) {
        brush(color(tint.r, tint.g, tint.b, std::min(1.0f, transparency * glowAlpha() * (0.07f + pulse * 0.09f))));
        target_->DrawGeometry(shape, solid_.Get(), glowWidth(13.0f));
        if (middle_glow) {
            brush(color(tint.r, tint.g, tint.b, std::min(1.0f, transparency * glowAlpha() * (0.20f + pulse * 0.21f))));
            target_->DrawGeometry(shape, solid_.Get(), glowWidth(7.0f));
        }
    }
    brush(color(tint.r, tint.g, tint.b, transparency * (0.73f + pulse * 0.27f)));
    target_->DrawGeometry(shape, solid_.Get(), 3.2f);
}

SessionState Renderer::sessionOf(const Game& game) const {
    if (!context_->sessions || context_->sessions->empty() || !isGame(game)) return SessionState::none;
    const auto state = sessionState(*context_->sessions, gameKey(game));
    return context_->sessions_frozen_only && state != SessionState::frozen ? SessionState::none : state;
}

// A plain black disc with a white state glyph: play while running, pause bars while suspended.
void Renderer::drawCardBadge(float x, float y, float width, float height, float transparency, bool frozen) {
    const float radius = std::clamp(std::min(width, height) * 0.064f, 11.0f, 22.0f);
    const float cx = x + width - radius - 17, cy = y + radius + 17;
    brush(color(0.015f, 0.02f, 0.03f, transparency * 0.84f));
    target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), radius + 5, radius + 5), solid_.Get());
    brush(color(0.96f, 0.96f, 0.96f, transparency));
    if (frozen) {
        const float bar = radius * 0.2f, gap = radius * 0.14f, half = radius * 0.42f;
        for (const float left : {cx - gap - bar, cx + gap})
            target_->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(left, cy - half, left + bar, cy + half),
                                                            bar * 0.3f, bar * 0.3f), solid_.Get());
        return;
    }
    if (!play_glyph_) return;
    const auto previous = [&] { D2D1_MATRIX_3X2_F matrix; target_->GetTransform(&matrix); return matrix; }();
    target_->SetTransform(D2D1::Matrix3x2F::Scale(radius * 0.84f, radius * 0.84f) * D2D1::Matrix3x2F::Translation(cx, cy) * previous);
    target_->FillGeometry(play_glyph_.Get(), solid_.Get());
    target_->SetTransform(previous);
}

void Renderer::drawCardStatusShade(float x, float y, float width, float height, float transparency, ID2D1Geometry* shape) {
    if (!status_shade_ || width <= 0 || height <= 0) return;
    const auto owned_shape = shape ? ComPtr<ID2D1Geometry>{} : tileShape(D2D1::RectF(x, y, x + width, y + height), tile_shape_);
    if (!shape) shape = owned_shape.Get();
    if (!shape) return;
    // A minimum reach keeps the caption readable on small cards; the upper art
    // of a tall poster stays clear. Fill the actual cover shape, never its bounds.
    const float reach = std::min(height, std::clamp(height * 0.55f, 116.0f, 180.0f));
    status_shade_->SetStartPoint(D2D1::Point2F(x, y + height - reach));
    status_shade_->SetEndPoint(D2D1::Point2F(x, y + height));
    status_shade_->SetOpacity(transparency);
    target_->FillGeometry(shape, status_shade_.Get());
}

// Without a fraction this is a plain caption: no progress track, text centred.
void Renderer::drawCardStatus(float x, float y, float width, float height, float transparency, const std::wstring& label,
                              std::optional<float> fraction, D2D1_COLOR_F bar) {
    const float left = x + 17, right = x + width - 17, bottom = y + height - 17;
    text(label, left, bottom - (fraction ? 45.0f : 31.0f), right - left, 32, button_font_.Get(), color(0.96f, 0.96f, 0.96f, transparency));
    if (!fraction) return;
    line(left, bottom - 3, right, bottom - 3, color(1, 1, 1, 0.20f * transparency), 4);
    if (*fraction > 0) line(left, bottom - 3, left + (right - left) * std::min(*fraction, 1.0f), bottom - 3, bar, 4);
}

bool Renderer::drawInstallProgress(const Game& game, D2D1_RECT_F button, D2D1_COLOR_F ink, D2D1_COLOR_F track, IDWriteTextFormat* font) {
    const auto session = sessionOf(game);
    if (!game.installation.active() || session == SessionState::running || session == SessionState::frozen) return false;
    const auto& install = game.installation;
    const auto percent = install.percent();
    std::wstring status = installationLabel(install.phase);
    if (percent) status += L" " + std::to_wstring(*percent) + L"%";
    else if (install.moving()) status += L"...";
    const float h = button.bottom - button.top;
    const float pad = std::clamp(h * 0.37f, 14.0f, 22.0f);
    const float left = button.left + pad, right = button.right - pad;
    const float bar_height = h >= 56 ? 6.0f : 4.0f;
    const float top = button.top + h * 0.63f;
    textLine(status, left, button.top + h * 0.34f, right - left, font, ink);
    brush(track);
    target_->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(left, top, right, top + bar_height), bar_height * 0.5f, bar_height * 0.5f), solid_.Get());
    float begin = left, end = left;
    if (percent) end += (right - left) * static_cast<float>(*percent) / 100.0f;
    else if (install.moving()) {
        // No byte totals yet (Steam's first seconds): activity without an invented share.
        const float segment = (right - left) * 0.22f;
        begin += (right - left - segment) * (0.5f + 0.5f * std::sin(elapsed_ * 2.5f));
        end = begin + segment;
    }
    if (end > begin) {
        brush(ink);
        target_->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(begin, top, end, top + bar_height), bar_height * 0.5f, bar_height * 0.5f), solid_.Get());
    }
    return true;
}

void Renderer::fillImage(Cover& image, D2D1_RECT_F box, float radius, float opacity, bool gray, ID2D1Geometry* clip, bool contain,
                         ID2D1GeometryRealization* realized) {
    if (!image.bitmap || !image.brush || opacity <= 0.001f) return;
    ID2D1BitmapBrush* fill = gray ? grayBrush(image) : nullptr;
    if (!fill) fill = image.brush.Get();
    const auto pixels = image.bitmap->GetPixelSize();
    const float w = box.right - box.left, h = box.bottom - box.top;
    const float sx = w / static_cast<float>(pixels.width), sy = h / static_cast<float>(pixels.height);
    const float zoom = contain ? std::min(sx, sy) : std::max(sx, sy);
    const float image_w = pixels.width * zoom, image_h = pixels.height * zoom;
    if (low_memory_) { // Drawn larger than decoded (low memory caps covers at their usual size): decode it at this size.
        D2D1_MATRIX_3X2_F transform{};
        target_->GetTransform(&transform);
        wantEdge(image, zoom * std::sqrt(std::abs(transform._11 * transform._22 - transform._12 * transform._21)));
    }
    // Rounding can leave w - image_w a hair above 0: clamp's bounds must stay ordered.
    const float left = box.left + (contain ? (w - image_w) * 0.5f : std::clamp(w * 0.5f - image.focus.x * image_w, std::min(w - image_w, 0.0f), 0.0f));
    const float top = box.top + (contain ? (h - image_h) * 0.5f : std::clamp(h * 0.5f - image.focus.y * image_h, std::min(h - image_h, 0.0f), 0.0f));
    fill->SetTransform(D2D1::Matrix3x2F::Scale(zoom, zoom) * D2D1::Matrix3x2F::Translation(left, top));
    fill->SetOpacity(opacity);
    // Bitmap brushes clamp their edge pixels; restrict the fill to the fitted image so
    // differing source proportions leave clean margins rather than stretched edge bands.
    if (contain) target_->PushAxisAlignedClip(D2D1::RectF(left, top, left + image_w, top + image_h), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    if (realized && realizer_) realizer_->DrawGeometryRealization(realized, fill);
    else if (clip) target_->FillGeometry(clip, fill);
    else target_->FillRoundedRectangle(D2D1::RoundedRect(box, radius, radius), fill);
    if (contain) target_->PopAxisAlignedClip();
}

void Renderer::fillPoster(Cover& image, D2D1_RECT_F box, float radius, float opacity, bool gray, ID2D1Geometry* clip) {
    if (!image.bitmap) return;
    const auto pixels = image.bitmap->GetPixelSize();
    const float ratio = static_cast<float>(pixels.width) / static_cast<float>(std::max(1u, pixels.height));
    const float frame = (box.right - box.left) / std::max(1.0f, box.bottom - box.top);
    fillImage(image, box, radius, opacity, gray, clip);
    if (std::abs(ratio / frame - 1) < 0.25f) return;
    brush(color(0.008f, 0.011f, 0.015f, 0.62f * opacity));
    if (clip) target_->FillGeometry(clip, solid_.Get());
    else target_->FillRoundedRectangle(D2D1::RoundedRect(box, radius, radius), solid_.Get());
    fillImage(image, box, radius, opacity, gray, clip, true);
}