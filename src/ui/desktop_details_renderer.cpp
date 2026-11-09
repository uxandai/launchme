// Desktop mode's dialogs, part two (src/ui/desktop_sheet.hpp): Szczegóły (the game's picture, its
// actions, description, facts, numbers, achievements and the plugin's comparison) and Zmień grafikę
// (artwork types, search, the gallery, pages and "Z dysku") as sheets over the frosted backdrop.
// Both are measured while drawn: every control records the area that answers for it.
#include "renderer.hpp"
#include "renderer_internal.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

using Microsoft::WRL::ComPtr;
using namespace desktop_ui;
using Kind = desktop_ui::SheetHit::Kind;

namespace {
using palette::gray;
D2D1_COLOR_F rgb(float r, float g, float b, float a = 1) { return D2D1::ColorF(r, g, b, a); }
using render_detail::box;
using render_detail::upper;
float middle(Rect r) { return (r.top + r.bottom) * 0.5f; }
const D2D1_COLOR_F panel_bg = rgb(0.071f, 0.076f, 0.089f);
const D2D1_COLOR_F popup_bg = rgb(0.094f, 0.1f, 0.115f);
const D2D1_COLOR_F card_bg = rgb(1, 1, 1, 0.035f);
}

// ---- Szczegóły ---------------------------------------------------------------------------------

void Renderer::drawDesktopDetails() {
    const auto& view = *context_->desktop_sheet;
    desktop_hotspots_.clear();
    const auto sheet = detailsSheet(viewport_.width, viewport_.height, sheetDock());
    const auto* achievements = context_->achievements;
    drawDesktopSheetFrame(sheet, view, achievements ? std::wstring(tr(L"Osiągnięcia")) : std::wstring(tr(L"Szczegóły")), achievements != nullptr);
    desktopHot({Kind::close}, sheet.close());
    if (achievements) desktopHot({Kind::back}, sheet.back());
    const auto p = sheet.panel(), h = sheet.header();
    if (!cards_ || cards_->empty()) return;
    const auto& game = (*cards_)[0].game;
    const float left = p.left + 28, right = p.right - 28, s = view.scroll;
    target_->PushAxisAlignedClip(D2D1::RectF(p.left + 1, h.bottom, p.right - 1, p.bottom - 1), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    const float top = h.bottom + 20 - s;
    if (achievements) {
        drawDesktopAchievements(*achievements, left, right, top);
        target_->PopAxisAlignedClip();
        if (achievements->card && achievements->selected >= 0 && achievements->selected < static_cast<int>(achievements->items.size()))
            drawDesktopAchievementCard(*achievements);
        return;
    }
    const auto hover = [&](SheetHit hit) { return view.hover == hit ? 1.0f : 0.0f; };
    const auto pressed = [&](SheetHit hit) { return view.pressed == hit; };
    // The picture: the game's background, else its cover blurred, under a shade; its logo on it.
    const Rect banner{left, top, right, top + std::clamp((right - left) * 0.3f, 180.0f, 250.0f)};
    {
        ComPtr<ID2D1RoundedRectangleGeometry> clip;
        factory_->CreateRoundedRectangleGeometry(D2D1::RoundedRect(box(banner), 12, 12), clip.GetAddressOf());
        desktopFill(banner, 12, rgb(0.11f, 0.12f, 0.14f));
        if (clip) {
            target_->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), clip.Get()), nullptr);
            if (!heroes_.empty() && heroes_[0].bitmap) desktopBitmapFill(heroes_[0].bitmap.Get(), box(banner), reveal(heroes_[0]));
            else if (!covers_.empty() && covers_[0].bitmap) if (auto* blur = blurredArt(covers_[0])) target_->DrawBitmap(blur, box(banner), 0.9f);
            ComPtr<ID2D1GradientStopCollection> stops;
            ComPtr<ID2D1LinearGradientBrush> shade;
            const D2D1_GRADIENT_STOP shade_stops[] = {{0, gray(0, 0)}, {0.55f, gray(0, 0.15f)}, {1, gray(0, 0.7f)}};
            if (SUCCEEDED(target_->CreateGradientStopCollection(shade_stops, 3, stops.GetAddressOf())) &&
                SUCCEEDED(target_->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(D2D1::Point2F(0, banner.top), D2D1::Point2F(0, banner.bottom)),
                                                            stops.Get(), shade.GetAddressOf())))
                target_->FillRectangle(box(banner), shade.Get());
            target_->PopLayer();
        }
        desktopStroke(banner, 12, gray(1, 0.06f));
        if (!logos_.empty() && logos_[0].bitmap) {
            const auto& logo = logos_[0];
            const auto pixels = logo.bitmap->GetPixelSize();
            const auto source = logo.has_ink_bounds ? logo.ink_bounds : D2D1::RectF(0, 0, static_cast<float>(pixels.width), static_cast<float>(pixels.height));
            const float w = source.right - source.left, hh = source.bottom - source.top;
            if (w > 0 && hh > 0) {
                const float k = std::min(std::min(320.0f, (right - left) * 0.42f) / w, (banner.bottom - banner.top) * 0.42f / hh);
                const auto r = D2D1::RectF(banner.left + 24, banner.bottom - 22 - hh * k, banner.left + 24 + w * k, banner.bottom - 22);
                target_->DrawBitmap(logo.bitmap.Get(), r, reveal(logo), D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, &source);
            }
        }
    }
    float y = banner.bottom + 30;
    textLine(game.name, left, y, right - left, desktop_heading_font_.Get(), palette::text());
    y += 28;
    // Platform, year, developer — and the rating, which opens its source.
    const auto* details = context_->game_details;
    std::wstring meta = activePlatformName(game);
    if (details) {
        if (const auto year = gameReleaseYear(details->release_date); !year.empty()) meta += (meta.empty() ? L"" : L"  ·  ") + year;
        if (!details->developer.empty()) meta += (meta.empty() ? L"" : L"  ·  ") + details->developer;
    }
    float meta_right = left + std::min(textAdvance(meta, desktop_font_.Get()), right - left - 200);
    textLine(meta, left, y, meta_right - left + 2, desktop_font_.Get(), palette::muted());
    if (context_->detail_rating >= 0 && !context_->rating_label.empty()) {
        const SheetHit hit{Kind::action, context_->detail_rating};
        const auto label = context_->rating_label + L"  \u2197";
        const float x = meta.empty() ? left : meta_right + 18, w = std::min(textAdvance(label, desktop_font_.Get()) + 2, right - x);
        textLine(label, x, y, w, desktop_font_.Get(), uiAccent(hover(hit) > 0 ? 1.0f : 0.85f));
        if (hover(hit) > 0) { brush(uiAccent(0.6f)); target_->FillRectangle(D2D1::RectF(x, y + 9, x + w, y + 10), solid_.Get()); }
        desktopHot(hit, {x - 4, y - 12, x + w + 4, y + 12});
    }
    y += 26;
    // The actions in one row (wrapping when narrow).
    {
        float x = left, row_top = y;
        const float bh = 40;
        const auto place = [&](float w) {
            if (x + w > right && x > left) { x = left; row_top += bh + 10; }
            const Rect r{x, row_top, x + w, row_top + bh};
            x += w + 10;
            return r;
        };
        const bool session = context_->menu_session;
        {
            const SheetHit hit{Kind::action, 0};
            const auto& install = game.installation;
            const bool installing = install.active() && !session;
            const auto label = installing ? installationText(install) : context_->primary_action;
            const auto r = place(std::max(installing ? 220.0f : 180.0f, textAdvance(label, desktop_button_font_.Get()) + 64));
            if (installing) { // Its share fills the button, as in the pane.
                const auto percent = install.percent();
                desktopInstallButton(r, percent ? *percent / 100.0f : -1.0f, label);
            } else
                desktopButton(r, 2, hover(hit), pressed(hit), context_->primary_enabled,
                              !game.installed && !session ? desktop_ui::glyph::download : desktop_ui::glyph::play, context_->primary_action);
            if (context_->primary_enabled) desktopHot(hit, r);
        }
        if (context_->detail_favorite_action >= 0) {
            const SheetHit hit{Kind::action, context_->detail_favorite_action};
            const auto r = place(bh);
            desktopButton(r, 1, hover(hit), pressed(hit), true, 0, {});
            const float fav_scale = 1.0f + (context_->detail_favorite ? 0.32f : -0.18f) * std::sin(context_->detail_favorite_pulse * std::numbers::pi_v<float>);
            desktopGlyph(context_->detail_favorite ? desktop_ui::glyph::star_fill : desktop_ui::glyph::star, (r.left + r.right) * 0.5f, middle(r),
                         15 * fav_scale, context_->detail_favorite ? rgb(1.0f, 0.78f, 0.28f) : gray(0.85f));
            desktopHot(hit, r);
        }
        const auto outline = [&](int action, wchar_t icon, const std::wstring& label) {
            if (action < 0) return;
            const SheetHit hit{Kind::action, action};
            const auto r = place(textAdvance(label, desktop_button_font_.Get()) + 58);
            desktopButton(r, 1, hover(hit), pressed(hit), true, icon, label);
            desktopHot(hit, r);
        };
        // While the game runs: Zamknij grę right after Wróć (the same close as its menu's).
        outline(context_->detail_close_action, desktop_ui::glyph::stop, tr(L"Zamknij grę"));
        outline(context_->detail_collections_action, desktop_ui::glyph::folder, tr(L"Kolekcje"));
        outline(context_->detail_versions_action, desktop_ui::glyph::sync, tr(L"Wersje"));
        outline(context_->detail_identify, desktop_ui::glyph::tag, tr(L"Zidentyfikuj"));
        const auto extra = [&](DetailsExtra which, wchar_t icon, const std::wstring& label, bool enabled) {
            const SheetHit hit{Kind::tile, -1, static_cast<int>(which)};
            const auto r = place(textAdvance(label, desktop_button_font_.Get()) + 58);
            desktopButton(r, 1, enabled ? hover(hit) : 0.0f, pressed(hit), enabled, icon, label);
            if (enabled) desktopHot(hit, r);
        };
        extra(DetailsExtra::artwork, desktop_ui::glyph::picture, tr(L"Grafika"), true);
        extra(DetailsExtra::folder, desktop_ui::glyph::folder_open, tr(L"Folder"), view.folder);
        if (view.hideable) extra(DetailsExtra::hide, view.hidden ? desktop_ui::glyph::view : desktop_ui::glyph::hide, view.hidden ? tr(L"Pokaż") : tr(L"Ukryj"), true);
        // Cloud saves of a game Aurelia manages: its state, a click opens its page.
        outline(context_->detail_cloud_action, 0xE753, context_->aurelia_cloud.empty() ? std::wstring(tr(L"Zapisy w chmurze")) : context_->aurelia_cloud);
        y = row_top + bh + 28;
    }
    // Two columns: the description and facts; the numbers and cards.
    const float side_w = std::clamp((right - left) * 0.33f, 260.0f, 320.0f), main_right = right - side_w - 28, side_left = main_right + 28;
    float main_y = y, side_y = y;
    textLine(upper(tr(L"Opis")), left, main_y, main_right - left, desktop_label_font_.Get(), gray(0.52f));
    main_y += 14;
    if (details && !details->description.empty())
        main_y += paragraph(details->description, left, main_y, main_right - left, desktop_font_.Get(), gray(0.82f), 200) + 24;
    else {
        if (context_->details_loading) { drawSkeleton(D2D1::RectF(left, main_y, main_right, main_y + 88), 4, 22); main_y += 100; }
        else {
            textLine(tr(L"Opis gry nie jest dostępny."), left, main_y + 9, main_right - left, desktop_font_.Get(), palette::muted());
            main_y += 40;
        }
    }
    if (details) {
        const auto released = render_detail::releaseText(details->release_date);
        const std::pair<const wchar_t*, const std::wstring*> facts[] = {
            {trKey(L"Developer"), &details->developer}, {trKey(L"Wydawca"), &details->publisher}, {trKey(L"Premiera"), &released},
            {trKey(L"Gatunek"), &details->genres}, {trKey(L"Gracze"), &details->players}, {trKey(L"Ocena"), &details->score}};
        bool heading = false;
        for (const auto& [label, value] : facts) {
            if (value->empty()) continue;
            if (!heading) { textLine(upper(tr(L"Informacje")), left, main_y, main_right - left, desktop_label_font_.Get(), gray(0.52f)); main_y += 14; heading = true; }
            brush(gray(1, 0.05f));
            target_->FillRectangle(D2D1::RectF(left, main_y, main_right, main_y + 1), solid_.Get());
            const float label_w = std::min(130.0f, (main_right - left) * 0.32f);
            textLine(tr(label), left, main_y + 18, label_w, desktop_font_.Get(), palette::muted());
            const auto m = typeMetrics(desktop_font_.Get());
            const float hh = paragraph(*value, left + label_w + 12, main_y + 18 - m.baseline + m.cap * 0.5f, main_right - left - label_w - 12,
                                       desktop_font_.Get(), gray(0.9f), 3);
            main_y += std::max(36.0f, hh + 14);
        }
    }
    // Numbers.
    {
        const Rect card{side_left, side_y, right, side_y + 76};
        desktopFill(card, 10, card_bg);
        desktopStroke(card, 10, gray(1, 0.05f));
        const float half = (card.right - card.left) * 0.5f;
        const auto stat = [&](float x, const wchar_t* label, const std::wstring& value) {
            textLine(upper(tr(label)), x, card.top + 24, half - 28, desktop_label_font_.Get(), palette::muted());
            textLine(value.empty() ? L"—" : value, x, card.top + 50, half - 28, desktop_strong_font_.Get(), palette::text());
        };
        stat(card.left + 16, trKey(L"Czas gry"), view.playtime);
        stat(card.left + half + 8, trKey(L"Ostatnio"), view.last_played);
        brush(gray(1, 0.06f));
        target_->FillRectangle(D2D1::RectF(card.left + half - 4, card.top + 14, card.left + half - 3, card.bottom - 14), solid_.Get());
        side_y = card.bottom + 14;
    }
    // Achievements: the share, then the list.
    if (context_->detail_achievements >= 0 && details) {
        const SheetHit hit{Kind::action, context_->detail_achievements};
        const Rect card{side_left, side_y, right, side_y + 118};
        desktopFill(card, 10, hover(hit) > 0 ? rgb(1, 1, 1, 0.06f) : card_bg);
        desktopStroke(card, 10, gray(1, 0.05f + 0.08f * hover(hit)));
        textLine(upper(tr(L"Osiągnięcia")), card.left + 16, card.top + 24, card.right - card.left - 32, desktop_label_font_.Get(), palette::muted());
        const auto total = details->achievements_total, got = details->achievements_unlocked;
        textLine(total ? std::to_wstring(got) + L" / " + std::to_wstring(total) : std::wstring(L"—"), card.left + 16, card.top + 52,
                 card.right - card.left - 32, desktop_title_font_.Get(), palette::text());
        const float bar_l = card.left + 16, bar_r = card.right - 16, by = card.top + 76;
        desktopFill({bar_l, by - 2, bar_r, by + 2}, 2, gray(1, 0.1f));
        if (total) desktopFill({bar_l, by - 2, bar_l + (bar_r - bar_l) * std::clamp(static_cast<float>(got) / static_cast<float>(total), 0.0f, 1.0f), by + 2}, 2, uiAccent());
        textLine(tr(L"Pokaż wszystkie"), card.left + 16, card.top + 100, card.right - card.left - 50, desktop_small_font_.Get(), uiAccent(0.9f));
        desktopGlyph(desktop_ui::glyph::chevron_right, card.right - 22, card.top + 100, 9, uiAccent(0.9f));
        desktopHot(hit, card);
        side_y = card.bottom + 14;
    }
    // The plugin's comparison (HowLongToBeat): its times and how far the player got.
    if (context_->detail_comparison_action >= 0 && context_->detail_comparison) {
        const auto& value = *context_->detail_comparison;
        const SheetHit hit{Kind::action, context_->detail_comparison_action};
        const float line_h = 26;
        const int rows = value.main_seconds || value.extra_seconds || value.complete_seconds ? 3 : 0;
        const Rect card{side_left, side_y, right, side_y + 50 + rows * line_h + (rows ? 30 : 20)};
        desktopFill(card, 10, hover(hit) > 0 ? rgb(1, 1, 1, 0.06f) : card_bg);
        desktopStroke(card, 10, gray(1, 0.05f + 0.08f * hover(hit)));
        textLine(upper(context_->detail_comparison_title), card.left + 16, card.top + 24, card.right - card.left - 32, desktop_label_font_.Get(), palette::muted());
        float ry = card.top + 50;
        if (!rows) textLine(value.status, card.left + 16, ry, card.right - card.left - 32, desktop_font_.Get(), palette::muted());
        else {
            const std::pair<const wchar_t*, uint32_t> times[] = {{trKey(L"Główny wątek"), value.main_seconds}, {trKey(L"Z dodatkami"), value.extra_seconds},
                                                                 {trKey(L"Na 100%"), value.complete_seconds}};
            for (const auto& [label, seconds] : times) {
                textLine(tr(label), card.left + 16, ry, card.right - card.left - 120, desktop_font_.Get(), gray(0.8f));
                textLine(plugins::compactTime(seconds ? std::optional<uint64_t>(seconds) : std::nullopt), card.right - 116, ry, 100, desktop_strong_font_.Get(),
                         palette::text(), DWRITE_TEXT_ALIGNMENT_TRAILING);
                ry += line_h;
            }
            if (value.main_seconds)
                textLine(std::wstring(tr(L"Twój postęp: ")) + plugins::percentLabel(value, context_->detail_playtime_seconds), card.left + 16, ry + 6,
                         card.right - card.left - 32, desktop_small_font_.Get(), uiAccent(0.9f));
        }
        desktopHot(hit, card);
        side_y = card.bottom + 14;
    }
    desktop_sheet_height_ = std::max(main_y, side_y) + s - h.bottom + 12;
    target_->PopAxisAlignedClip();
}

void Renderer::drawDesktopAchievements(const achievements_ui::State& list, float left, float right, float top) {
    const auto& view = *context_->desktop_sheet;
    float y = top;
    textLine(list.source.empty() ? list.title : list.title + L" · " + list.source, left, y + 10, right - left - 200, desktop_title_font_.Get(), palette::text());
    if (list.total) {
        const auto count = std::to_wstring(list.unlocked) + L" / " + std::to_wstring(list.total);
        textLine(count, right - 190, y + 10, 190, desktop_strong_font_.Get(), palette::text(), DWRITE_TEXT_ALIGNMENT_TRAILING);
        desktopFill({left, y + 32, right, y + 36}, 2, gray(1, 0.1f));
        desktopFill({left, y + 32, left + (right - left) * std::clamp(static_cast<float>(list.unlocked) / static_cast<float>(list.total), 0.0f, 1.0f), y + 36}, 2, uiAccent());
    }
    y += 56;
    if (list.items.empty()) {
        textLine(list.loading ? std::wstring(tr(L"Wczytywanie…")) : !list.message.empty() ? list.message : std::wstring(tr(L"Lista osiągnięć nie jest dostępna.")), left, y + 10, right - left, desktop_font_.Get(), palette::muted());
        desktop_sheet_height_ = y + 40 - top + view.scroll;
        return;
    }
    const float item_h = 60;
    const auto sheet = detailsSheet(viewport_.width, viewport_.height, sheetDock());
    const auto p = sheet.panel();
    const float visible_top = sheet.header().bottom;
    for (size_t i = 0; i < list.items.size(); ++i) {
        const Rect r{left, y, right, y + item_h - 6};
        if (r.bottom >= p.top && r.top <= p.bottom) {
            const auto& item = list.items[i];
            const SheetHit hit{Kind::row, static_cast<int>(i)}; // A click opens its card.
            desktopFill(r, 8, item.achieved ? rgb(1, 1, 1, 0.045f) : rgb(1, 1, 1, 0.022f));
            if (!list.card && view.hover == hit) desktopFill(r, 8, gray(1, 0.04f));
            if (r.bottom > visible_top) desktopHot(hit, {r.left, std::max(r.top, visible_top), r.right, std::min(r.bottom, p.bottom)});
            const Rect icon{r.left + 8, r.top + 7, r.left + 48, r.bottom - 7};
            if (i < artwork_thumbnails_.size() && artwork_thumbnails_[i].bitmap)
                fillImage(artwork_thumbnails_[i], box(icon), 6, (item.achieved ? 1.0f : 0.55f) * reveal(artwork_thumbnails_[i]), !item.achieved);
            else desktopFill(icon, 6, gray(1, 0.06f));
            const float text_left = icon.right + 14, detail_w = std::min(200.0f, (r.right - r.left) * 0.28f);
            textLine(item.name, text_left, r.top + 17, r.right - text_left - detail_w - 16, desktop_strong_font_.Get(), item.achieved ? palette::text() : gray(0.7f));
            textLine(item.description, text_left, r.top + 36, r.right - text_left - detail_w - 16, desktop_small_font_.Get(), palette::muted());
            textLine(item.detail, r.right - 12 - detail_w, middle(r), detail_w, desktop_small_font_.Get(), item.achieved ? uiAccent(0.9f) : gray(0.55f),
                     DWRITE_TEXT_ALIGNMENT_TRAILING);
        }
        y += item_h;
    }
    if (!list.complete && list.total > list.items.size()) {
        textLine(std::wstring(tr(L"Pozostałe osiągnięcia: ")) + std::to_wstring(list.total - list.items.size()), left + 8, y + 14, right - left,
                 desktop_font_.Get(), palette::muted());
        y += 40;
    }
    desktop_sheet_height_ = y - top + 20;
}

// The selected achievement's card over the dimmed page: a click beside it, Wstecz or Esc return to
// the list; Left/Right show the neighbours (as in Tryb TV).
void Renderer::drawDesktopAchievementCard(const achievements_ui::State& state) {
    const auto& view = *context_->desktop_sheet;
    const auto sheet = detailsSheet(viewport_.width, viewport_.height, sheetDock());
    const auto p = sheet.panel();
    const Rect area{p.left + 1, sheet.header().bottom, p.right - 1, p.bottom - 1};
    const auto index = static_cast<size_t>(state.selected);
    const auto& item = state.items[index];
    target_->PushAxisAlignedClip(box(area), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    brush(rgb(0, 0, 0, 0.55f));
    target_->FillRectangle(box(area), solid_.Get());
    target_->PopAxisAlignedClip();
    desktopHot({Kind::dialog_cancel}, area);
    const float w = std::min(600.0f, area.right - area.left - 64), h = std::min(400.0f, area.bottom - area.top - 48);
    const float cx = (area.left + area.right) * 0.5f, cy = (area.top + area.bottom) * 0.5f;
    const Rect c{std::round(cx - w * 0.5f), std::round(cy - h * 0.5f), std::round(cx + w * 0.5f), std::round(cy + h * 0.5f)};
    desktopFill(c, 14, rgb(0.085f, 0.092f, 0.11f));
    desktopStroke(c, 14, gray(1, 0.09f));
    desktopHot({Kind::dialog}, c);
    // The icon, larger; the name, state and description beside it.
    const float size = 96;
    const Rect icon{c.left + 28, c.top + 28, c.left + 28 + size, c.top + 28 + size};
    desktopFill(icon, 12, gray(1, 0.06f));
    if (index < artwork_thumbnails_.size() && artwork_thumbnails_[index].bitmap)
        fillImage(artwork_thumbnails_[index], box(icon), 12, (item.achieved ? 1.0f : 0.55f) * reveal(artwork_thumbnails_[index]), !item.achieved);
    if (item.achieved) desktopStroke(icon, 12, uiAccent(0.85f), 2);
    const float text_left = icon.right + 22, text_width = c.right - 28 - text_left;
    std::wstring where = std::to_wstring(index + 1) + L" / " + std::to_wstring(state.items.size());
    if (!state.source.empty()) where = state.source + L"  ·  " + where;
    textLine(where, text_left, icon.top + 8, text_width, desktop_small_font_.Get(), palette::muted());
    textLine(item.name, text_left, icon.top + 36, text_width, desktop_title_font_.Get(), item.achieved ? palette::text() : gray(0.8f));
    const auto status = item.achieved ? (item.earned.empty() ? std::wstring(tr(L"Zdobyte")) : tr(L"Zdobyte ") + item.earned) : std::wstring(tr(L"Niezdobyte"));
    textLine(status, text_left, icon.top + 64, text_width, desktop_font_.Get(), item.achieved ? uiAccent() : gray(0.55f));
    const auto description = item.description.empty() && !item.achieved ? std::wstring(tr(L"Opis pojawi się po zdobyciu.")) : item.description;
    const float rarity_top = c.bottom - 128;
    if (!description.empty()) {
        desktop_font_->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
        text(description, c.left + 28, icon.bottom + 18, c.right - c.left - 56, rarity_top - 12 - (icon.bottom + 18), desktop_font_.Get(),
             item.description.empty() ? palette::muted() : gray(0.86f));
        desktop_font_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    }
    if (item.percent >= 0) {
        const float left = c.left + 28, right = c.right - 28;
        desktopFill({left, rarity_top, right, rarity_top + 1}, 0, gray(1, 0.07f));
        std::wstring heading = tr(L"Rzadkość");
        CharUpperBuffW(heading.data(), static_cast<DWORD>(heading.size()));
        textLine(heading, left, rarity_top + 20, right - left, desktop_label_font_.Get(), palette::muted());
        textLine(std::wstring(achievements_ui::rarity(item.percent)) + L"  ·  " + item.share, left, rarity_top + 42, right - left,
                 desktop_strong_font_.Get(), palette::text());
        desktopFill({left, rarity_top + 60, right, rarity_top + 64}, 2, gray(1, 0.1f));
        desktopFill({left, rarity_top + 60, left + std::max(4.0f, (right - left) * std::clamp(item.percent / 100.0f, 0.0f, 1.0f)), rarity_top + 64}, 2, uiAccent());
    }
    const SheetHit back{Kind::dialog_cancel, 1};
    const float bw = textAdvance(tr(L"Wstecz"), desktop_button_font_.Get()) + 40;
    const Rect button{c.right - 20 - bw, c.bottom - 52, c.right - 20, c.bottom - 18};
    desktopButton(button, 1, view.hover == back ? 1.0f : 0.0f, view.pressed == back, true, 0, tr(L"Wstecz"));
    desktopHot(back, button);
}

// ---- Zmień grafikę ------------------------------------------------------------------------------

void Renderer::drawDesktopArtwork() {
    const auto& view = *context_->desktop_sheet;
    desktop_hotspots_.clear();
    const auto* state = context_->artwork_browser;
    const auto sheet = artworkSheet(viewport_.width, viewport_.height, sheetDock());
    drawDesktopSheetFrame(sheet, view, std::wstring(tr(L"Zmień grafikę")) + (state ? L"  ·  " + state->game.name : L""), false);
    desktopHot({Kind::close}, sheet.close());
    if (!state) return;
    const auto p = sheet.panel(), h = sheet.header();
    const auto hover = [&](SheetHit hit) { return view.hover == hit ? 1.0f : 0.0f; };
    const auto pressed = [&](SheetHit hit) { return view.pressed == hit; };
    // The types, then "Z dysku", the source and the crop.
    const float nav_right = p.left + 212;
    brush(gray(1, 0.06f));
    target_->FillRectangle(D2D1::RectF(nav_right, h.bottom, nav_right + 1, p.bottom - 1), solid_.Get());
    if (!state->results_mode) {
        const float nav = view.nav_glide.value_or(static_cast<float>(state->category));
        const float ay = h.bottom + 10 + nav * 38;
        const Rect active_rect{p.left + 12, ay, nav_right - 12, ay + 36};
        desktopFill(active_rect, 8, gray(1, 0.085f));
        desktopFill({active_rect.left, active_rect.top + 9, active_rect.left + 3, active_rect.bottom - 9}, 1.5f, uiAccent(0.95f));
    }
    float ny = h.bottom + 10;
    for (int i = 0; i < static_cast<int>(artwork_ui::kinds.size()); ++i) {
        const Rect r{p.left + 12, ny, nav_right - 12, ny + 36};
        const SheetHit hit{Kind::nav, i};
        const bool active = state->category == i && !state->results_mode;
        if (!active && hover(hit) > 0) desktopFill(r, 8, gray(1, 0.045f));
        textLine(tr(artwork_ui::names[static_cast<size_t>(i)]), r.left + 16, middle(r), r.right - r.left - 48, active ? desktop_strong_font_.Get() : desktop_font_.Get(),
                 active ? palette::text() : gray(0.8f));
        drawSetDot(r.right - 20, middle(r), state->custom[static_cast<size_t>(i)], active ? palette::text() : gray(0.75f)); // Set: filled.
        if (!state->applying) desktopHot(hit, r);
        ny += 38;
    }
    ny += 12;
    brush(gray(1, 0.06f));
    target_->FillRectangle(D2D1::RectF(p.left + 20, ny, nav_right - 20, ny + 1), solid_.Get());
    ny += 12;
    const auto side_button = [&](SheetHit hit, wchar_t icon, const std::wstring& label) {
        const Rect r{p.left + 12, ny, nav_right - 12, ny + 36};
        if (hover(hit) > 0) desktopFill(r, 8, gray(1, 0.06f));
        desktopGlyph(icon, r.left + 20, middle(r), 14, gray(0.75f));
        textLine(label, r.left + 40, middle(r), r.right - r.left - 48, desktop_font_.Get(), gray(0.85f));
        if (!state->applying) desktopHot(hit, r);
        ny += 38;
    };
    side_button({Kind::tile_action}, desktop_ui::glyph::folder_open, tr(L"Z dysku…"));
    if (state->providerChoice())
        side_button({Kind::cog}, desktop_ui::glyph::sync, std::wstring(tr(L"Źródło")) + L": " + artwork_ui::providerName(state->provider));
    if (state->croppable()) side_button({Kind::toggle}, desktop_ui::glyph::edit, tr(L"Kadruj")); // Covers and backgrounds.
    // Search.
    const float left = nav_right + 24, right = p.right - 24;
    const Rect field{left, h.bottom + 14, right - 112, h.bottom + 52};
    const bool focus = state->search_focus;
    desktopFill(field, 9, gray(1, focus ? 0.07f : 0.045f + 0.02f * hover({Kind::field})));
    if (focus) desktopStroke({field.left - 1, field.top - 1, field.right + 1, field.bottom + 1}, 10, uiAccent(0.9f), 1.5f);
    else desktopStroke(field, 9, gray(1, 0.1f));
    desktopGlyph(desktop_ui::glyph::search, field.left + 18, middle(field), 13, palette::muted());
    const auto m = typeMetrics(desktop_font_.Get());
    const float text_left = field.left + 36, text_right = field.right - (state->query.empty() ? 12 : 36);
    if (state->query.empty())
        textLine(tr(L"Nazwa gry, Steam AppID lub link SteamGridDB"), text_left, middle(field), text_right - text_left, desktop_font_.Get(), palette::muted(0.85f));
    else {
        if (state->query_selected && focus) desktopFill({text_left - 2, middle(field) - 10, text_left + std::min(textAdvance(state->query, desktop_font_.Get()), text_right - text_left) + 2,
                                                         middle(field) + 10}, 3, uiAccent(0.35f));
        fieldText(state->query, state->query.size(), focus && (GetTickCount64() / 530) % 2 == 0, text_left, middle(field) - m.line * 0.5f,
                  text_right - text_left, m.line, desktop_font_.Get(), palette::text());
        const Rect clear{field.right - 32, field.top + 5, field.right - 6, field.bottom - 5};
        if (hover({Kind::action, 1}) > 0) desktopFill(clear, 6, gray(1, 0.08f));
        desktopGlyph(desktop_ui::glyph::close, (clear.left + clear.right) * 0.5f, middle(clear), 10, palette::muted());
        desktopHot({Kind::action, 1}, clear);
    }
    desktopHot({Kind::field}, {field.left, field.top, field.right - (state->query.empty() ? 0 : 34), field.bottom});
    const Rect search{right - 100, field.top, right, field.bottom};
    desktopButton(search, 1, hover({Kind::action, 0}), pressed({Kind::action, 0}), !state->applying, 0, tr(L"Szukaj"));
    desktopHot({Kind::action, 0}, search);
    // Status, the gallery and the foot.
    float y = field.bottom + 14;
    const std::wstring status = state->loading ? std::wstring(tr(L"Wczytywanie…")) : state->status;
    if (!status.empty()) { textLine(status, left, y + 8, right - left, desktop_small_font_.Get(), palette::muted()); y += 24; }
    const float foot = 60, list_top = y + 4, list_bottom = p.bottom - foot;
    const float s = view.scroll;
    target_->PushAxisAlignedClip(D2D1::RectF(left - 6, list_top, right + 6, list_bottom), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    float content = 0;
    if (state->results_mode) {
        // Games SteamGridDB found for the query: one opens its artwork.
        for (size_t i = 0; i < state->games.size(); ++i) {
            const Rect r{left, list_top + 6 + static_cast<float>(i) * 50 - s, right, list_top + 6 + static_cast<float>(i) * 50 + 44 - s};
            const SheetHit hit{Kind::row, static_cast<int>(i)};
            desktopFill(r, 8, gray(1, hover(hit) > 0 ? 0.07f : 0.035f));
            const auto& item = state->games[i];
            textLine(item.name, r.left + 16, middle(r), r.right - r.left - 120, desktop_strong_font_.Get(), palette::text());
            if (item.year) textLine(std::to_wstring(item.year), r.right - 96, middle(r), 80, desktop_font_.Get(), palette::muted(), DWRITE_TEXT_ALIGNMENT_TRAILING);
            if (r.bottom > list_top && r.top < list_bottom) desktopHot(hit, r);
        }
        content = 12 + static_cast<float>(state->games.size()) * 50;
    } else {
        // Cells shaped like the type, as many per row as fit.
        const auto kind = state->kind();
        float aspect = kind == ArtworkKind::hero ? 3.1f : kind == ArtworkKind::landscape ? 2.14f : kind == ArtworkKind::poster ? 2.0f / 3.0f :
                       kind == ArtworkKind::logo ? 2.0f : 1.0f;
        const float target_w = kind == ArtworkKind::hero ? 300.0f : kind == ArtworkKind::landscape ? 230.0f : kind == ArtworkKind::logo ? 210.0f :
                               kind == ArtworkKind::poster ? 140.0f : 158.0f;
        const float gap = 14, width = right - left;
        const int columns = std::max(1, static_cast<int>((width + gap) / (target_w + gap)));
        const float cell_w = (width - gap * static_cast<float>(columns - 1)) / static_cast<float>(columns), cell_h = std::round(cell_w / aspect);
        const float step = cell_h + 34;
        for (size_t i = 0; i < state->assets.size(); ++i) {
            const auto& asset = state->assets[i];
            const float x = left + static_cast<float>(i % static_cast<size_t>(columns)) * (cell_w + gap);
            const float cy = list_top + 8 + static_cast<float>(i / static_cast<size_t>(columns)) * step - s;
            const Rect r{x, cy, x + cell_w, cy + cell_h};
            if (r.bottom + 30 < list_top || r.top > list_bottom) continue;
            const SheetHit hit{Kind::tile, -1, static_cast<int>(i)};
            const bool selected = state->gallery && state->selected == i, applied = asset.id && asset.id == state->applied_id;
            desktopFill(r, 8, kind == ArtworkKind::logo ? rgb(0.16f, 0.17f, 0.2f) : rgb(0.11f, 0.12f, 0.14f));
            if (i < artwork_thumbnails_.size() && artwork_thumbnails_[i].bitmap) {
                ComPtr<ID2D1RoundedRectangleGeometry> clip;
                factory_->CreateRoundedRectangleGeometry(D2D1::RoundedRect(box(r), 8, 8), clip.GetAddressOf());
                const bool contain = kind == ArtworkKind::logo || kind == ArtworkKind::disc;
                const float inset = kind == ArtworkKind::logo ? 14.0f : 0.0f;
                fillImage(artwork_thumbnails_[i], D2D1::RectF(r.left + inset, r.top + inset, r.right - inset, r.bottom - inset), 8,
                          reveal(artwork_thumbnails_[i]), false, contain ? nullptr : clip.Get(), contain);
            } else {
                const float pulse = 0.03f + 0.02f * (0.5f + 0.5f * std::sin(elapsed_ * 2.6f - static_cast<float>(i) * 0.3f));
                desktopFill(r, 8, gray(1, pulse));
            }
            const float hv = hover(hit);
            desktopStroke(r, 8, selected ? uiAccent() : gray(1, 0.08f + 0.14f * hv), selected ? 2.5f : 1.0f);
            if (applied) {
                brush(uiAccent());
                target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(r.right - 16, r.top + 16), 11, 11), solid_.Get());
                const auto a = uiAccent();
                desktopGlyph(desktop_ui::glyph::check, r.right - 16, r.top + 16, 10,
                             0.2126f * a.r + 0.7152f * a.g + 0.0722f * a.b > 0.6f ? gray(0.06f) : gray(0.98f));
            }
            std::wstring caption = asset.author;
            if (asset.width && asset.height) caption += (caption.empty() ? L"" : L"  ·  ") + std::to_wstring(asset.width) + L"×" + std::to_wstring(asset.height);
            textLine(caption, r.left + 2, r.bottom + 14, r.right - r.left - 4, desktop_small_font_.Get(), selected ? palette::text() : palette::muted());
            if (r.bottom > list_top && r.top < list_bottom && !state->applying) desktopHot(hit, {r.left, r.top, r.right, r.bottom + 24});
        }
        const size_t rows = (state->assets.size() + static_cast<size_t>(columns) - 1) / static_cast<size_t>(columns);
        content = 16 + static_cast<float>(rows) * step;
        if (state->assets.empty() && !state->loading)
            textLine(tr(L"Brak grafik tego typu. Wyszukaj grę albo wskaż plik z dysku."), left, list_top + 30, right - left, desktop_font_.Get(), palette::muted());
    }
    target_->PopAxisAlignedClip();
    desktop_sheet_height_ = content;
    const float view_h = list_bottom - list_top;
    if (content > view_h + 1) {
        const float thumb = std::max(30.0f, view_h * view_h / content), ty = list_top + (view_h - thumb) * std::clamp(s / (content - view_h), 0.0f, 1.0f);
        desktopFill({p.right - 9, ty, p.right - 5, ty + thumb}, 2, gray(1, 0.22f));
    }
    // Pages and Zastosuj.
    brush(gray(1, 0.06f));
    target_->FillRectangle(D2D1::RectF(nav_right + 1, list_bottom, p.right - 1, list_bottom + 1), solid_.Get());
    const float fy = list_bottom + foot * 0.5f;
    if (!state->results_mode) {
        const Rect prev{left, fy - 18, left + 36, fy + 18}, next{left + 92, fy - 18, left + 128, fy + 18};
        const bool can_prev = state->page > 0 && !state->loading, can_next = state->more && !state->loading;
        desktopButton(prev, 1, can_prev ? hover({Kind::up}) : 0.0f, pressed({Kind::up}), can_prev, 0xE76B, {});
        desktopButton(next, 1, can_next ? hover({Kind::down}) : 0.0f, pressed({Kind::down}), can_next, glyph::chevron_right, {});
        textLine(std::to_wstring(state->page + 1), prev.right, fy, next.left - prev.right, desktop_strong_font_.Get(), gray(0.8f), DWRITE_TEXT_ALIGNMENT_CENTER);
        if (can_prev) desktopHot({Kind::up}, prev);
        if (can_next) desktopHot({Kind::down}, next);
        const bool can_apply = state->gallery && state->selected < state->assets.size() && !state->applying;
        const Rect apply{right - 150, fy - 19, right, fy + 19};
        desktopButton(apply, 2, can_apply ? hover({Kind::dialog_save}) : 0.0f, pressed({Kind::dialog_save}), can_apply, glyph::check,
                      state->applying ? std::wstring(tr(L"Zapisywanie…")) : std::wstring(tr(L"Zastosuj")));
        if (can_apply) desktopHot({Kind::dialog_save}, apply);
        if (can_apply) {
            const auto& asset = state->assets[state->selected];
            std::wstring about = asset.author.empty() ? std::wstring{} : std::wstring(tr(L"Autor: ")) + asset.author;
            textLine(about, next.right + 24, fy, apply.left - next.right - 48, desktop_small_font_.Get(), palette::muted());
        }
    }
    // "Z dysku": which type the picked file becomes, then the file.
    if (state->local_open) {
        desktopFill(p, 14, gray(0, 0.45f));
        const auto d = sheet.dialog(560, 352);
        desktopShadow(d, 12, 1.2f);
        desktopFill(d, 12, popup_bg);
        desktopStroke(d, 12, gray(1, 0.1f));
        desktopHot({Kind::dialog}, d);
        textLine(tr(L"Grafika z dysku"), d.left + 24, d.top + 32, d.right - d.left - 48, desktop_title_font_.Get(), palette::text());
        // A board of the six types as small pictures in their proportions; a filled dot marks one already set.
        const float gap = 12, tile_w = (d.right - d.left - 48 - 2 * gap) / 3, tile_h = 100;
        for (int i = 0; i < static_cast<int>(artwork_ui::kinds.size()); ++i) {
            const float tx = d.left + 24 + static_cast<float>(i % 3) * (tile_w + gap), ty = d.top + 58 + static_cast<float>(i / 3) * (tile_h + gap);
            const Rect r{tx, ty, tx + tile_w, ty + tile_h};
            const SheetHit hit{Kind::dropdown_item, i};
            const bool chosen = state->local_kind == i;
            desktopFill(r, 10, gray(1, chosen ? 0.09f : hover(hit) > 0 ? 0.06f : 0.035f));
            if (chosen) desktopStroke(r, 10, uiAccent(0.9f), 1.5f);
            drawKindIllustration(artwork_ui::kinds[static_cast<size_t>(i)], r.left + 16, r.top + 10, r.right - 16, r.bottom - 30, chosen);
            textLine(tr(artwork_ui::names[static_cast<size_t>(i)]), r.left + 6, r.bottom - 15, r.right - r.left - 12, desktop_small_font_.Get(), palette::text(), DWRITE_TEXT_ALIGNMENT_CENTER);
            drawSetDot(r.right - 14, r.top + 14, state->custom[static_cast<size_t>(i)], gray(0.9f));
            desktopHot(hit, r);
        }
        const Rect pick{d.right - 24 - 150, d.bottom - 24 - 36, d.right - 24, d.bottom - 24}, cancel{pick.left - 120, pick.top, pick.left - 10, pick.bottom};
        desktopButton(cancel, 1, hover({Kind::dialog_cancel}), pressed({Kind::dialog_cancel}), true, 0, tr(L"Anuluj"));
        desktopButton(pick, 2, hover({Kind::dialog_save}), pressed({Kind::dialog_save}), true, glyph::folder_open, tr(L"Wskaż plik…"));
        desktopHot({Kind::dialog_cancel}, cancel);
        desktopHot({Kind::dialog_save}, pick);
    }
}
