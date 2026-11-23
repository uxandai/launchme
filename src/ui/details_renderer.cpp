// The game's page (Szczegóły), after the PS5 game hub: its picture over the whole screen under a
// shade, its logo (or title) with a line of facts and the rating, one row of pill buttons with a
// white pill gliding to the focus, and frosted cards at the bottom (description, facts,
// achievements, the plugin's comparison). Geometry: ui/game_details_layout.hpp.
#include "renderer.hpp"
#include "renderer_internal.hpp"
#include "game_details_layout.hpp"
#include "../library/steam_stats.hpp"

#include <algorithm>
#include <cmath>
#include <cwctype>
#include <numbers>

using Microsoft::WRL::ComPtr;

namespace {
using palette::gray;
using render_detail::box;
using render_detail::upper;
// The year of a release date in any format ("12 Mar, 2019", "2019-03-12"); the date itself otherwise.
std::wstring releaseYear(const std::wstring& date) {
    for (size_t i = date.size(); i >= 4; --i) {
        const auto start = i - 4;
        bool digits = true;
        for (size_t j = start; j < i; ++j) digits = digits && std::iswdigit(date[j]);
        const bool alone = (start == 0 || !std::iswdigit(date[start - 1])) && (i == date.size() || !std::iswdigit(date[i]));
        if (digits && alone && (date[start] == L'1' || date[start] == L'2')) return date.substr(start, 4);
    }
    return date;
}
// The first `count` genres of a comma-separated list.
std::wstring firstGenres(const std::wstring& genres, int count) {
    size_t end = 0;
    for (int i = 0; i < count && end != std::wstring::npos; ++i) end = genres.find(L',', end ? end + 1 : 0);
    return end == std::wstring::npos ? genres : genres.substr(0, end);
}
}

void Renderer::drawDetails() {
    details_achievements_ = {};
    details_comparison_ = {};
    if (!cards_ || cards_->empty()) return;
    drawBackground(); // The game's picture over the whole screen (see drawBackground).
    const float width = viewport_.width, height = viewport_.height;
    // Shade: dark behind the texts on the left and the cards at the bottom, the picture clear at the top right.
    {
        Microsoft::WRL::ComPtr<ID2D1GradientStopCollection> stops;
        Microsoft::WRL::ComPtr<ID2D1LinearGradientBrush> across, down;
        const D2D1_GRADIENT_STOP left_stops[] = {{0.0f, D2D1::ColorF(0.006f, 0.008f, 0.012f, 0.88f)}, {0.45f, D2D1::ColorF(0.006f, 0.008f, 0.012f, 0.62f)},
                                                 {1.0f, D2D1::ColorF(0.006f, 0.008f, 0.012f, 0.22f)}};
        if (SUCCEEDED(target_->CreateGradientStopCollection(left_stops, 3, stops.GetAddressOf())) &&
            SUCCEEDED(target_->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(D2D1::Point2F(0, 0), D2D1::Point2F(width, 0)),
                                                         stops.Get(), across.GetAddressOf())))
            target_->FillRectangle(D2D1::RectF(0, 0, width, height), across.Get());
        stops.Reset();
        const D2D1_GRADIENT_STOP bottom_stops[] = {{0.0f, D2D1::ColorF(0.006f, 0.008f, 0.012f, 0)}, {1.0f, D2D1::ColorF(0.006f, 0.008f, 0.012f, 0.9f)}};
        if (SUCCEEDED(target_->CreateGradientStopCollection(bottom_stops, 2, stops.GetAddressOf())) &&
            SUCCEEDED(target_->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(D2D1::Point2F(0, height * 0.4f), D2D1::Point2F(0, height)),
                                                         stops.Get(), down.GetAddressOf())))
            target_->FillRectangle(D2D1::RectF(0, height * 0.4f, width, height), down.Get());
    }
    const auto* media = context_->details_media ? &*context_->details_media : nullptr;
    // Scrolled down to the strips: the picture blurred and a little darker behind them (as PSfive's details).
    if (media && media->scroll > 0.5f) {
        const float deep = std::min(1.0f, media->scroll / 400.0f);
        ComPtr<ID2D1RectangleGeometry> all;
        if (SUCCEEDED(factory_->CreateRectangleGeometry(D2D1::RectF(0, 0, width, height), all.GetAddressOf())) && frostCapture(all.Get(), 18))
            frostDraw(all.Get(), deep);
        brush(D2D1::ColorF(0.006f, 0.008f, 0.012f, 0.35f * deep));
        target_->FillRectangle(D2D1::RectF(0, 0, width, height), solid_.Get());
    }
    beginScreenForeground();
    const auto& game = (*cards_)[0].game;
    const auto* info = context_->game_details;
    const details_layout::Layout layout{width, height, media ? media->scroll : 0.0f};
    details_layout::Actions actions;
    actions.identify = context_->detail_identify;
    actions.favorite = context_->detail_favorite_action;
    actions.collections = context_->detail_collections_action;
    actions.versions = context_->detail_versions_action;
    actions.achievements = context_->detail_achievements;
    actions.rating = context_->rating_label.empty() ? -2 : context_->detail_rating;
    actions.plugin = context_->detail_comparison ? context_->detail_comparison_action : -2;
    actions.close = context_->detail_close_action;
    actions.cloud = context_->detail_cloud_action;
    if (media) {
        actions.shots = media->shots; actions.similar = media->similar_names.size(); actions.shots_loading = media->loading;
        actions.shots_scroll = media->shots_scroll; actions.similar_scroll = media->similar_scroll;
    }
    actions.badges = !context_->mod_badges.empty();
    // One line of facts: the platform's logo, the year, the genres, the playtime, then the rating
    // as a pill that opens its source (its rectangle goes to the hit tests: ratingLink).
    const float facts_y = layout.factsCenter(actions);
    std::wstring facts;
    {
        const auto add = [&](const std::wstring& value) { if (!value.empty()) facts += (facts.empty() ? L"" : L"   ·   ") + value; };
        if (info) {
            add(releaseYear(info->release_date));
            add(firstGenres(info->genres, 2));
            if (info->playtime_minutes && *info->playtime_minutes) add(formatPlaytime(*info->playtime_minutes));
        }
    }
    const auto platform = activePlatformName(game);
    float facts_x = layout.left();
    if (!platform.empty()) facts_x += drawPlatformMark(platform, facts_x, facts_y, 24, 180, 1, menu_font_.Get()) + 18;
    const float facts_width = facts.empty() ? 0 : std::min(std::ceil(textAdvance(facts, menu_font_.Get())) + 2, layout.right() - facts_x - 340);
    if (actions.rating >= 0) {
        const std::wstring label = context_->rating_label + L"  ↗";
        const float x = facts_x + (facts_width > 0 ? facts_width + 22 : 0);
        const float w = std::min(std::ceil(textAdvance(label, settings_body_font_.Get())) + 40, layout.right() - x);
        rating_link_ = {x, facts_y - 17, x + w, facts_y + 17};
        actions.rating_rect = rating_link_;
    }
    const auto all = layout.items(actions);
    const auto* focus = layout.find(all, context_->detail_action);
    const auto focused = [&](int action) { return focus && focus->action == action ? 1.0f : 0.0f; };

    // The logo, bottom-left in its box; the title in large type without one.
    const auto logo_box = box(layout.logo(actions));
    if (!logos_.empty() && logos_[0].bitmap) {
        const auto& logo = logos_[0];
        const auto pixels = logo.bitmap->GetPixelSize();
        const auto source = logo.has_ink_bounds ? logo.ink_bounds : D2D1::RectF(0, 0, static_cast<float>(pixels.width), static_cast<float>(pixels.height));
        const float w = source.right - source.left, h = source.bottom - source.top;
        if (w > 0 && h > 0) {
            const float scale = std::min((logo_box.right - logo_box.left) / w, (logo_box.bottom - logo_box.top) / h);
            target_->DrawBitmap(logo.bitmap.Get(), D2D1::RectF(logo_box.left, logo_box.bottom - h * scale, logo_box.left + w * scale, logo_box.bottom),
                                reveal(logo), D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, &source);
        }
    } else { // The title while there is no logo (yet): the page is never without the game's name.
        const float w = logo_box.right - logo_box.left;
        const float h = paragraph(game.name, logo_box.left, 0, w, hero_font_.Get(), gray(0.97f), 2, false);
        paragraph(game.name, logo_box.left, logo_box.bottom - h, w, hero_font_.Get(), gray(0.97f), 2);
    }

    if (facts_width > 0) textLine(facts, facts_x, facts_y, facts_width + 4, menu_font_.Get(), gray(0.86f));
    if (actions.badges) drawModBadges(context_->mod_badges, layout.left(), layout.badgesCenter(actions), layout.right(), settings_body_font_.Get(), 34);
    // The focused ✕ or cloud says what it is (Tryb TV has no tooltips), above the cards.
    if (focus && focus->kind == details_layout::Kind::close)
        textLine(tr(L"Zamknij grę"), layout.left(), layout.cardsTop() - 19, layout.right() - layout.left(), settings_body_font_.Get(), gray(0.86f));
    else if (focus && focus->kind == details_layout::Kind::cloud && !context_->aurelia_cloud.empty())
        textLine(context_->aurelia_cloud, layout.left(), layout.cardsTop() - 19, layout.right() - layout.left(), settings_body_font_.Get(), gray(0.86f));

    // Drawn after the cards: their glass copies the scene without the buttons.
    const auto controls = [&] {
    if (actions.rating >= 0) drawButtonGlass(box(layout.rating(actions)));
    // The buttons: glass pills, then the white pill gliding to the focus, then their captions.
    const auto row = layout.buttons(actions);
    drawButtonGlass(box(layout.back()));
    for (const auto& item : row) drawButtonGlass(box(item.rect), 1, true, false, item.kind != details_layout::Kind::primary || context_->primary_enabled);
    if (focus && !focus->card() && !focus->media()) {
        auto r = box(focus->rect);
        if (context_->details_glide) { const auto& g = *context_->details_glide; r = D2D1::RectF(g.left, g.top, g.right, g.bottom); }
        drawButtonFocus(r);
    }
    {
        const auto back = layout.back();
        brush(buttonInk(focused(-1)));
        target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F((back.left + back.right) * 0.5f, (back.top + back.bottom) * 0.5f), 3.5f, 3.5f), solid_.Get());
    }
    if (actions.rating >= 0) {
        const auto r = layout.rating(actions);
        textLine(context_->rating_label + L"  ↗", r.left + 20, (r.top + r.bottom) * 0.5f, r.right - r.left - 36, settings_body_font_.Get(),
                 focused(actions.rating) > 0 ? buttonInk(1) : gray(0.95f));
    }
    for (const auto& item : row) {
        using details_layout::Kind;
        const wchar_t* label = L"";
        std::wstring primary;
        switch (item.kind) {
        case Kind::primary: primary = context_->primary_action; break;
        case Kind::more: label = L"…"; break;
        case Kind::collections: label = tr(L"Kolekcje"); break;
        case Kind::favorite: label = context_->detail_favorite ? L"★" : L"☆"; break;
        case Kind::versions: label = tr(L"Wybierz wersję…"); break;
        case Kind::identify: label = tr(L"Zidentyfikuj grę"); break;
        default: break;
        }
        const auto& r = item.rect;
        const float inset = std::min(22.0f, (r.bottom - r.top) * 0.5f - 4);
        const bool enabled = item.kind != Kind::primary || context_->primary_enabled;
        if (item.kind == Kind::close) { // ✕: two strokes on the button's centre.
            const auto ink = buttonInk(focused(item.action), enabled);
            const float cx = (r.left + r.right) * 0.5f, cy = (r.top + r.bottom) * 0.5f, d = 7.5f;
            line(cx - d, cy - d, cx + d, cy + d, ink, 2.4f); line(cx - d, cy + d, cx + d, cy - d, ink, 2.4f);
            continue;
        }
        if (item.kind == Kind::cloud) { drawCloudGlyph((r.left + r.right) * 0.5f, (r.top + r.bottom) * 0.5f, buttonInk(focused(item.action), enabled), context_->detail_cloud); continue; }
        if (item.kind == Kind::more) {
            // Three drawn dots on the button's exact centre (the "…" glyph sat on the baseline, low).
            brush(buttonInk(focused(item.action), enabled));
            const float cx = (r.left + r.right) * 0.5f, cy = (r.top + r.bottom) * 0.5f;
            for (int dot = -1; dot <= 1; ++dot)
                target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx + dot * 10.5f, cy), 3.3f, 3.3f), solid_.Get());
            continue;
        }
        D2D1_MATRIX_3X2_F saved{};
        const bool pulse_star = item.kind == Kind::favorite && context_->detail_favorite_pulse > 0.001f;
        if (pulse_star) {
            target_->GetTransform(&saved);
            const float scale = 1.0f + (context_->detail_favorite ? 0.30f : -0.16f) * std::sin(context_->detail_favorite_pulse * std::numbers::pi_v<float>);
            target_->SetTransform(D2D1::Matrix3x2F::Scale(scale, scale, D2D1::Point2F((r.left + r.right) * 0.5f, (r.top + r.bottom) * 0.5f)) * saved);
        }
        if (item.kind == Kind::favorite) {
            // A drawn five-pointed star on the button's centre (the font's ☆/★ came out small and low):
            // an outline, filled once the game is a favourite.
            const float cx = (r.left + r.right) * 0.5f, cy = (r.top + r.bottom) * 0.5f + 1, outer = 11.5f, inner = 5.0f;
            ComPtr<ID2D1PathGeometry> star;
            ComPtr<ID2D1GeometrySink> sink;
            if (SUCCEEDED(factory_->CreatePathGeometry(star.GetAddressOf())) && SUCCEEDED(star->Open(sink.GetAddressOf()))) {
                for (int point = 0; point < 10; ++point) {
                    const float angle = -std::numbers::pi_v<float> * 0.5f + point * std::numbers::pi_v<float> / 5;
                    const float radius = point % 2 ? inner : outer;
                    const auto at = D2D1::Point2F(cx + std::cos(angle) * radius, cy + std::sin(angle) * radius);
                    if (point == 0) sink->BeginFigure(at, D2D1_FIGURE_BEGIN_FILLED); else sink->AddLine(at);
                }
                sink->EndFigure(D2D1_FIGURE_END_CLOSED);
                if (SUCCEEDED(sink->Close())) {
                    brush(buttonInk(focused(item.action), enabled));
                    if (context_->detail_favorite) target_->FillGeometry(star.Get(), solid_.Get());
                    else target_->DrawGeometry(star.Get(), solid_.Get(), 1.9f);
                }
            }
        } else if (item.kind != Kind::primary || // An installation: its share on the button.
                   !drawInstallProgress(game, box(r), buttonInk(focused(item.action), true),
                                        focused(item.action) > 0.5f ? gray(0.05f, 0.15f) : gray(1, 0.2f), settings_body_font_.Get()))
            text(item.kind == Kind::primary ? primary : std::wstring(label), r.left + inset, r.top, r.right - r.left - 2 * inset, r.bottom - r.top,
                 action_font_.Get(), buttonInk(focused(item.action), enabled));
        if (pulse_star) target_->SetTransform(saved);
    }
    };

    // Cards: frosted panels; the focused one lighter (its highlight glides in from the buttons).
    const auto card = [&](settings_ui::Rect r, const std::wstring& heading, bool trophy = false) {
        glassFill(glassPanel(D2D1::RoundedRect(box(r), 20, 20), 18), D2D1::ColorF(0.05f, 0.055f, 0.07f, 0.66f));
        float x = r.left + 24;
        if (trophy) {
            drawTrophy(x + 8, r.top + 30, 1.0f);
            x += 26;
        }
        textLine(upper(heading), x, r.top + 30, r.right - x - 24, friend_section_font_.Get(), gray(0.6f));
    };
    const auto chevron = [&](settings_ui::Rect r, D2D1_COLOR_F ink) {
        const float cx = r.right - 26, cy = r.top + 30;
        line(cx - 3, cy - 6, cx + 3, cy, ink, 1.8f);
        line(cx + 3, cy, cx - 3, cy + 6, ink, 1.8f);
    };
    // Scrolled to the strips, the cards have left the screen (only their bottom edge would stay).
    const bool cards_shown = layout.cardsBottom() > 70;
    // Description.
    if (cards_shown) {
        const auto r = layout.description(actions);
        card(r, tr(L"Opis"));
        if (focus && focus->kind == details_layout::Kind::description) { // Focused: lighter, as the other cards (Enter: all of it).
            auto h = box(focus->rect);
            if (context_->details_glide) { const auto& g = *context_->details_glide; h = D2D1::RectF(g.left, g.top, g.right, g.bottom); }
            brush(gray(1, 0.12f));
            target_->FillRoundedRectangle(D2D1::RoundedRect(h, 20, 20), solid_.Get());
        }
        const auto description = context_->details_loading ? std::wstring(tr(L"Pobieranie opisu gry…")) :
            info && !info->description.empty() ? info->description :
            info && !info->error.empty() ? info->error : std::wstring(tr(L"Opis gry nie jest dostępny."));
        const auto metrics = typeMetrics(details_body_font_.Get());
        const int lines = std::max(1, static_cast<int>((r.bottom - r.top - 56 - 20) / std::max(1.0f, metrics.line)));
        if (context_->details_loading) drawSkeleton(D2D1::RectF(r.left + 24, r.top + 52, r.right - 24, r.bottom - 20), std::min(lines, 5), metrics.line);
        else paragraph(description, r.left + 24, r.top + 52, r.right - r.left - 48, details_body_font_.Get(), D2D1::ColorF(0.80f, 0.82f, 0.85f), lines);
    }
    // Facts table: label and value on one line each.
    if (cards_shown) {
        const auto r = layout.facts(actions);
        card(r, tr(L"Informacje"));
        std::vector<std::pair<const wchar_t*, std::wstring>> rows;
        if (info) {
            rows = {{tr(L"Developer"), info->developer}, {tr(L"Wydawca"), info->publisher}, {tr(L"Premiera"), render_detail::releaseText(info->release_date)},
                    {tr(L"Gatunek"), info->genres}};
            if (!info->players.empty()) rows.push_back({tr(L"Gracze"), info->players});
            if (!info->score.empty()) rows.push_back({tr(L"Ocena"), info->score});
        }
        const float top = r.top + 66, step = rows.empty() ? 0 : std::min(34.0f, (r.bottom - 22 - top) / rows.size());
        const float label_width = std::min(130.0f, (r.right - r.left - 48) * 0.36f);
        for (size_t i = 0; i < rows.size(); ++i) {
            const float y = top + step * (static_cast<float>(i) + 0.5f) - 4;
            textLine(rows[i].first, r.left + 24, y, label_width, settings_body_font_.Get(), palette::muted());
            textLine(rows[i].second.empty() ? L"—" : rows[i].second, r.left + 24 + label_width + 12, y, r.right - r.left - 60 - label_width,
                     settings_body_font_.Get(), gray(0.95f));
        }
    }
    // The focused card's highlight: the glide from the buttons, a lighter fill once there.
    const auto progressHighlight = [&](int action) {
        if (!focus || focus->action != action) return;
        auto r = box(focus->rect);
        if (context_->details_glide) { const auto& g = *context_->details_glide; r = D2D1::RectF(g.left, g.top, g.right, g.bottom); }
        brush(gray(1, 0.12f));
        target_->FillRoundedRectangle(D2D1::RoundedRect(r, std::min(20.0f, (r.bottom - r.top) * 0.5f), std::min(20.0f, (r.bottom - r.top) * 0.5f)), solid_.Get());
    };
    // Achievements: unlocked of all, the share and its bar; it opens the list.
    if (cards_shown && actions.achievements >= 0 && info && info->achievements_total) {
        const auto r = layout.progressCard(actions, false);
        details_achievements_ = r;
        card(r, tr(L"Osiągnięcia"), true);
        progressHighlight(actions.achievements);
        chevron(r, gray(0.8f));
        drawAchievementRing(r, info->achievements_unlocked, info->achievements_total, 1.0f);
    }
    // Czas gry: the play time, when last played and the details plugin's comparison (HowLongToBeat)
    // in one card; with the plugin it is a target and opens the plugin's page.
    if (cards_shown && layout.timeCard(actions)) {
        const auto r = layout.progressCard(actions, true);
        if (actions.plugin >= 0) details_comparison_ = r;
        card(r, tr(L"Czas gry"));
        if (actions.plugin >= 0) { progressHighlight(actions.plugin); chevron(r, gray(0.8f)); }
        std::wstring playtime;
        if (info && info->playtime_minutes && *info->playtime_minutes) playtime = formatPlaytime(*info->playtime_minutes);
        else if (game.store_playtime_minutes) playtime = formatPlaytime(game.store_playtime_minutes);
        drawPlaytimeBody(r, playtime, context_->detail_last_played, context_->detail_comparison ? &*context_->detail_comparison : nullptr,
                         context_->detail_playtime_seconds, 1.0f);
    }
    if (actions.media()) drawDetailsMedia(layout, actions, context_->detail_action);
    controls();
    if (media && media->viewer) drawShotViewer(media->shot, media->shots);
    if (context_->detail_text_view) drawPsfiveTextView(game.name, info ? info->description : std::wstring{});
}

// A cup with two handles on a stem and a base: the achievements' pictogram.
void Renderer::drawTrophy(float cx, float cy, float alpha) {
    const auto ink = D2D1::ColorF(0.96f, 0.78f, 0.32f, alpha);
    Microsoft::WRL::ComPtr<ID2D1PathGeometry> cup;
    Microsoft::WRL::ComPtr<ID2D1GeometrySink> sink;
    if (SUCCEEDED(factory_->CreatePathGeometry(cup.GetAddressOf())) && SUCCEEDED(cup->Open(sink.GetAddressOf()))) {
        sink->BeginFigure(D2D1::Point2F(cx - 6, cy - 8), D2D1_FIGURE_BEGIN_FILLED);
        sink->AddLine(D2D1::Point2F(cx + 6, cy - 8));
        sink->AddLine(D2D1::Point2F(cx + 6, cy - 3));
        sink->AddArc(D2D1::ArcSegment(D2D1::Point2F(cx - 6, cy - 3), D2D1::SizeF(6, 6), 0, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
        if (SUCCEEDED(sink->Close())) { brush(ink); target_->FillGeometry(cup.Get(), solid_.Get()); }
    }
    brush(ink);
    target_->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx - 7, cy - 4.5f), 2.6f, 3), solid_.Get(), 1.5f);
    target_->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx + 7, cy - 4.5f), 2.6f, 3), solid_.Get(), 1.5f);
    target_->FillRectangle(D2D1::RectF(cx - 1.2f, cy + 2, cx + 1.2f, cy + 6), solid_.Get());
    target_->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(cx - 5, cy + 5.5f, cx + 5, cy + 8.5f), 1.2f, 1.2f), solid_.Get());
}

void Renderer::drawPlaytimeBody(settings_ui::Rect r, const std::wstring& playtime, const std::wstring& last_played,
                                const plugins::TimeComparison* comparison, std::optional<uint64_t> played, float alpha) {
    const float inner = r.right - r.left - 48;
    textLine(playtime.empty() ? std::wstring(L"\u2014") : playtime, r.left + 24, r.top + 80, inner, settings_heading_font_.Get(), gray(0.97f, alpha));
    if (!last_played.empty())
        textLine(std::wstring(tr(L"Ostatnio")) + L" \u00b7 " + last_played, r.left + 24, r.top + 114, inner, settings_body_font_.Get(), palette::muted(alpha));
    if (!comparison) return;
    if (comparison->main_seconds) {
        const float y = r.bottom - 76;
        textLine(tr(L"Twój czas / główna fabuła"), r.left + 24, y, inner, settings_body_font_.Get(), palette::muted(alpha));
        textLine(plugins::comparisonLabel(*comparison, played), r.left + 24, y + 26, inner, menu_font_.Get(), gray(0.97f, alpha));
        if (const auto share = plugins::timePercent(*comparison, played)) {
            const float bar = r.bottom - 26;
            brush(gray(1, 0.14f * alpha));
            target_->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(r.left + 24, bar, r.right - 24, bar + 6), 3, 3), solid_.Get());
            if (*share > 0) {
                brush(uiAccent(alpha));
                target_->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(r.left + 24, bar,
                    r.left + 24 + inner * static_cast<float>(std::min(*share / 100, 1.0)), bar + 6), 3, 3), solid_.Get());
            }
        }
    } else if (!comparison->status.empty()) textLine(comparison->status, r.left + 24, r.bottom - 40, inner, settings_body_font_.Get(), palette::muted(alpha));
}

float Renderer::drawModBadges(const std::vector<std::wstring>& badges, float x, float cy, float right, IDWriteTextFormat* font, float height) {
    const float radius = height * 0.5f;
    for (const auto& label : badges) {
        const float w = std::ceil(textAdvance(label, font)) + height;
        if (x + w > right) break;
        const auto r = D2D1::RoundedRect(D2D1::RectF(x, cy - radius, x + w, cy + radius), radius, radius);
        brush(uiAccent(0.20f));
        target_->FillRoundedRectangle(r, solid_.Get());
        brush(uiAccent(0.65f));
        target_->DrawRoundedRectangle(r, solid_.Get(), 1.0f);
        textLine(label, x + radius, cy, w - height + 2, font, D2D1::ColorF(0.96f, 0.96f, 0.96f, 1.0f));
        x += w + 10;
    }
    return x;
}
