// Desktop mode's pane beside the grid: the bulk actions for marked games and the selected game's
// banner, buttons, stats, chips, ratings and description.
#include "renderer.hpp"
#include "desktop_render_internal.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <numbers>

using Microsoft::WRL::ComPtr;
using namespace desktop_ui;
using namespace desktop_draw;

// ---- Pane ------------------------------------------------------------------------------------

void Renderer::drawDesktopBulk(const Layout& layout, const View& view) {
    const auto p = layout.pane();
    textLine(std::wstring(tr(L"Zaznaczone: ")) + std::to_wstring(view.pane.marked), p.left + 20, title_h + 44, p.right - p.left - 40,
             desktop_title_font_.Get(), palette::text());
    textLine(tr(L"Akcje dla wszystkich zaznaczonych gier"), p.left + 20, title_h + 72, p.right - p.left - 40, desktop_font_.Get(), palette::muted());
    const wchar_t glyphs[] = {glyph::folder, glyph::star, glyph::refresh, glyph::hide, glyph::close};
    const wchar_t* labels[] = {trKey(L"Dodaj do kolekcji…"), trKey(L"Dodaj do ulubionych"), trKey(L"Pobierz grafiki ponownie"), trKey(L"Ukryj zaznaczone"),
                               trKey(L"Wyczyść zaznaczenie")};
    for (int i = 0; i < Layout::bulk_count; ++i) {
        const auto r = layout.paneBulk(i);
        const float hover = view.hover == Hit{Target::bulk, i} ? 1.0f : 0.0f;
        desktopButton(r, i == 0 ? 2 : 1, hover, view.pressed == Hit{Target::bulk, i}, true, glyphs[i], tr(labels[i]));
        if (view.keyboard && view.zone == 2 && view.pane_focus == i) desktopFocusRing(r, 8);
    }
}

void Renderer::drawDesktopPane(const Layout& layout, const View& view) {
    const auto p = layout.pane();
    brush(side_bg);
    target_->FillRectangle(box(p), solid_.Get());
    brush(gray(1, 0.06f));
    target_->FillRectangle(D2D1::RectF(p.left, p.top, p.left + 1, p.bottom), solid_.Get());
    // Its edge under the pointer or dragged: the accent, as wide as the grip.
    if (view.hover.target == Target::pane_resize || view.pressed.target == Target::pane_resize) {
        brush(uiAccent(view.pressed.target == Target::pane_resize ? 0.95f : 0.6f));
        target_->FillRectangle(D2D1::RectF(p.left - 1, p.top, p.left + 2, p.bottom), solid_.Get());
    }
    target_->PushAxisAlignedClip(D2D1::RectF(p.left + 1, p.top, p.right, p.bottom), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    const auto& pane = view.pane;
    const float left = p.left + 20, width = p.right - p.left - 40;
    if (pane.marked > 1) { drawDesktopBulk(layout, view); target_->PopAxisAlignedClip(); desktop_pane_height_ = 0; return; }
    if (!pane.game || !cards_ || selected_ >= cards_->size()) {
        const float cx = (p.left + p.right) * 0.5f, cy = (p.top + p.bottom) * 0.42f;
        brush(gray(1, 0.045f));
        target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), 30, 30), solid_.Get());
        desktopGlyph(glyph::game, cx, cy, 22, gray(0.6f));
        textLine(view.scanning ? tr(L"Wczytywanie biblioteki…") : view.library_empty ? tr(L"Biblioteka jest pusta") : tr(L"Wybierz grę"),
                 p.left + 20, cy + 52, width, desktop_strong_font_.Get(), gray(0.8f),
                 DWRITE_TEXT_ALIGNMENT_CENTER);
        target_->PopAxisAlignedClip();
        desktop_pane_height_ = 0;
        return;
    }
    const float s = view.pane_scroll;
    // The picture: the game's background (cross-faded on a new selection), else its blurred cover.
    const auto banner = box(layout.banner(s));
    ID2D1Bitmap* hero = selected_ < heroes_.size() ? heroes_[selected_].bitmap.Get() : nullptr;
    if (hero != desktop_hero_.Get()) {
        desktop_hero_previous_ = desktop_hero_;
        desktop_hero_ = hero;
        desktop_hero_changed_ = std::chrono::steady_clock::now();
    }
    const float fade = smoothFade(std::chrono::duration<float>(std::chrono::steady_clock::now() - desktop_hero_changed_).count() / 0.28f);
    if (!hero && selected_ < covers_.size() && covers_[selected_].bitmap)
        if (auto* tint = blurredArt(covers_[selected_])) target_->DrawBitmap(tint, banner, 0.85f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    if (desktop_hero_previous_ && fade < 1) desktopBitmapFill(desktop_hero_previous_.Get(), banner, 1 - fade);
    if (hero) desktopBitmapFill(hero, banner, fade);
    // After the delay on one game (Filmy w tle), its video plays in the picture's place.
    drawVideoIn(banner, nullptr, 1);
    {
        ComPtr<ID2D1GradientStopCollection> stops;
        ComPtr<ID2D1LinearGradientBrush> shade;
        const D2D1_GRADIENT_STOP shade_stops[] = {{0, rgb(side_bg.r, side_bg.g, side_bg.b, 0.15f)}, {0.45f, rgb(side_bg.r, side_bg.g, side_bg.b, 0.05f)},
                                                  {1, side_bg}};
        if (SUCCEEDED(target_->CreateGradientStopCollection(shade_stops, 3, stops.GetAddressOf())) &&
            SUCCEEDED(target_->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(D2D1::Point2F(0, banner.top), D2D1::Point2F(0, banner.bottom)),
                                                        stops.Get(), shade.GetAddressOf())))
            target_->FillRectangle(banner, shade.Get());
    }
    // Its logo over the picture's foot.
    if (selected_ < logos_.size() && logos_[selected_].bitmap) {
        const auto& logo = logos_[selected_];
        const auto pixels = logo.bitmap->GetPixelSize();
        const auto source = logo.has_ink_bounds ? logo.ink_bounds : D2D1::RectF(0, 0, static_cast<float>(pixels.width), static_cast<float>(pixels.height));
        const float w = source.right - source.left, h = source.bottom - source.top;
        if (w > 0 && h > 0) {
            const float max_w = std::min(width * 0.72f, 260.0f), max_h = std::min(76.0f, (banner.bottom - banner.top) * 0.4f);
            const float k = std::min(max_w / w, max_h / h);
            const auto r = D2D1::RectF(left, banner.bottom - 16 - h * k, left + w * k, banner.bottom - 16);
            target_->DrawBitmap(logo.bitmap.Get(), r, reveal(logo), D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, &source);
        }
    }
    // Name, origin and state.
    textLine(pane.name, left, layout.paneY(22, s), width, desktop_title_font_.Get(), palette::text());
    float x = left;
    const float meta_y = layout.paneY(50, s);
    if (!pane.platform.empty()) {
        const float w = std::min(textAdvance(pane.platform, desktop_small_font_.Get()) + 18, width * 0.6f);
        const Rect pill{x, meta_y - 11, x + w, meta_y + 11};
        desktopStroke(pill, 11, gray(1, 0.16f));
        textLine(pane.platform, pill.left + 9, meta_y, w - 16, desktop_small_font_.Get(), gray(0.85f));
        x = pill.right + 10;
    }
    const auto state_ink = pane.running ? rgb(0.36f, 0.86f, 0.52f) : pane.progress || pane.launching ? uiAccent() : palette::muted();
    if (pane.running || pane.frozen) {
        brush(state_ink);
        target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(x + 4, meta_y), 3.5f, 3.5f), solid_.Get());
        x += 13;
    }
    textLine(pane.launching ? std::wstring(tr(L"Uruchamianie…")) : pane.status, x, meta_y, left + width - x, desktop_small_font_.Get(), state_ink);
    // Buttons.
    const auto primary = layout.panePrimary(s);
    const float primary_hover = view.hover.target == Target::pane_primary ? 1.0f : 0.0f;
    const bool pressed = view.pressed.target == Target::pane_primary;
    if (pane.progress) {
        desktopInstallButton(primary, *pane.progress, pane.primary);
    } else {
        const wchar_t g = !pane.installed && !pane.running && !pane.frozen ? glyph::download : pane.frozen ? glyph::play : pane.running ? glyph::open : glyph::play;
        desktopButton(primary, 2, primary_hover, pressed, pane.primary_enabled, g, pane.primary);
    }
    const auto favorite = layout.paneFavorite(s);
    if (layout.favorite) { // Ulubione switched off: no star.
        desktopButton(favorite, 1, view.hover.target == Target::pane_favorite ? 1.0f : 0.0f, view.pressed.target == Target::pane_favorite, true, 0, {});
        const float fav_scale = 1.0f + (pane.favorite ? 0.32f : -0.18f) * std::sin(pane.favorite_pulse * std::numbers::pi_v<float>);
        desktopGlyph(pane.favorite ? glyph::star_fill : glyph::star, (favorite.left + favorite.right) * 0.5f, (favorite.top + favorite.bottom) * 0.5f,
                     16 * fav_scale, pane.favorite ? rgb(1.0f, 0.78f, 0.28f) : gray(0.85f));
    }
    const auto more = layout.paneMore(s);
    const bool more_open = view.menu && view.menu->align_right && view.menu->kind == Menu::Kind::game;
    desktopButton(more, 1, view.hover.target == Target::pane_more || more_open ? 1.0f : 0.0f, view.pressed.target == Target::pane_more, true, glyph::more, {});
    if (view.keyboard && view.zone == 2) {
        if (view.pane_focus == 0) desktopFocusRing(primary, 8);
        else if (view.pane_focus == 1) desktopFocusRing(favorite, 8);
        else if (view.pane_focus == 2) desktopFocusRing(more, 8);
    }
    // Numbers.
    const auto stats = layout.paneStats(s);
    desktopFill(stats, 10, card_bg);
    desktopStroke(stats, 10, gray(1, 0.05f));
    const float column = (stats.right - stats.left) / 3;
    const auto stat = [&](int i, const wchar_t* label, const std::wstring& value, float share, bool link = false) {
        const float cx = stats.left + column * static_cast<float>(i) + 14;
        if (link) { // The number opens its list: hover, press and a chevron.
            const Rect cell{cx - 10, stats.top + 4, stats.left + column * static_cast<float>(i + 1) - 4, stats.bottom - 4};
            const bool hover = view.hover.target == Target::pane_achievements;
            if (hover) desktopFill(cell, 7, gray(1, view.pressed.target == Target::pane_achievements ? 0.04f : 0.07f));
            desktopGlyph(glyph::chevron_right, cell.right - 12, stats.top + 43, 9, gray(hover ? 0.95f : 0.55f));
            desktop_achievements_ = cell;
        }
        textLine(upper(tr(label)), cx, stats.top + 20, column - 20, desktop_label_font_.Get(), palette::muted());
        textLine(value.empty() ? L"—" : value, cx, stats.top + 43, column - (link ? 36 : 20), desktop_strong_font_.Get(), palette::text());
        if (share >= 0) {
            desktopFill({cx, stats.bottom - 9, cx + column - 28, stats.bottom - 6}, 1.5f, gray(1, 0.1f));
            desktopFill({cx, stats.bottom - 9, cx + (column - 28) * share, stats.bottom - 6}, 1.5f, uiAccent());
        }
        if (i) { brush(gray(1, 0.06f)); target_->FillRectangle(D2D1::RectF(cx - 14, stats.top + 12, cx - 13, stats.bottom - 12), solid_.Get()); }
    };
    stat(0, trKey(L"Czas gry"), pane.playtime, -1);
    stat(1, trKey(L"Ostatnio"), pane.last_played, -1);
    stat(2, trKey(L"Osiągnięcia"), pane.achievements, pane.achievements_share, pane.achievements_link);
    // Quick actions.
    const wchar_t quick_glyphs[] = {glyph::info, glyph::picture, glyph::folder_open, pane.hidden ? glyph::view : glyph::hide};
    const std::wstring quick_labels[] = {tr(L"Szczegóły"), tr(L"Grafika"), tr(L"Folder"), pane.hidden ? std::wstring(tr(L"Pokaż")) : std::wstring(tr(L"Ukryj"))};
    for (int i = 0; i < Layout::quick_count; ++i) {
        const auto r = layout.paneQuick(i, s);
        const bool enabled = (i != 2 || pane.folder) && (i != 3 || pane.hideable);
        const bool hover = enabled && view.hover == Hit{Target::pane_quick, i};
        desktopFill(r, 9, gray(1, hover ? 0.075f : 0.035f));
        if (hover && view.pressed == Hit{Target::pane_quick, i}) desktopFill(r, 9, gray(1, 0.04f));
        const auto ink = enabled ? gray(hover ? 0.98f : 0.82f) : gray(0.38f);
        desktopGlyph(quick_glyphs[i], (r.left + r.right) * 0.5f, r.top + 20, 15, ink);
        textLine(quick_labels[i], r.left + 4, r.top + 40, r.right - r.left - 8, desktop_small_font_.Get(), ink, DWRITE_TEXT_ALIGNMENT_CENTER);
        if (view.keyboard && view.zone == 2 && view.pane_focus == 3 + i) desktopFocusRing(r, 9);
    }
    // Collections, as chips that open them; "+" adds the game to one.
    float y = layout.paneContentTop(s);
    if (!context_->aurelia_cloud.empty()) {
        // Cloud saves (a game Aurelia manages): the state, and a click opens its page.
        const float w = std::min(width, textAdvance(context_->aurelia_cloud, desktop_small_font_.Get()) + 44);
        const Rect chip{left - 6, y - 13, left - 6 + w, y + 13};
        const bool hover = view.hover.target == Target::pane_cloud;
        if (hover) desktopFill(chip, 7, gray(1, view.pressed.target == Target::pane_cloud ? 0.04f : 0.07f));
        desktopGlyph(0xE753, chip.left + 16, y, 14, gray(hover ? 0.98f : 0.78f)); // Segoe MDL2 "Cloud".
        textLine(context_->aurelia_cloud, chip.left + 32, y, w - 38, desktop_small_font_.Get(), gray(hover ? 0.98f : 0.78f));
        desktop_cloud_ = chip;
        y += 30;
    }
    textLine(upper(tr(L"Kolekcje")), left, y, width, desktop_label_font_.Get(), palette::muted());
    y += 16;
    float cx = left;
    const float chip_h = 26;
    for (int i = 0; i < static_cast<int>(pane.collections.size()); ++i) {
        const auto& name = pane.collections[static_cast<size_t>(i)].first;
        const float w = std::min(textAdvance(name, desktop_small_font_.Get()) + 22, width);
        if (cx + w > left + width && cx > left) { cx = left; y += chip_h + 6; }
        const Rect chip{cx, y, cx + w, y + chip_h};
        const bool hover = view.hover == Hit{Target::pane_chip, i};
        desktopFill(chip, chip_h * 0.5f, gray(1, hover ? 0.11f : 0.06f));
        desktopStroke(chip, chip_h * 0.5f, gray(1, 0.09f));
        textLine(name, chip.left + 11, (chip.top + chip.bottom) * 0.5f, w - 20, desktop_small_font_.Get(), gray(0.9f));
        desktop_chips_.push_back(chip);
        cx = chip.right + 6;
    }
    {
        const auto label = pane.collections.empty() ? std::wstring(tr(L"Dodaj do kolekcji")) : std::wstring(tr(L"Dodaj"));
        const float w = textAdvance(label, desktop_small_font_.Get()) + 36;
        if (cx + w > left + width && cx > left) { cx = left; y += chip_h + 6; }
        const Rect chip{cx, y, cx + w, y + chip_h};
        const bool hover = view.hover.target == Target::pane_chip_add;
        desktopFill(chip, chip_h * 0.5f, hover ? uiAccent(0.16f) : gray(1, 0.0f));
        desktopStroke(chip, chip_h * 0.5f, hover ? uiAccent(0.8f) : gray(1, 0.16f));
        desktopGlyph(glyph::add, chip.left + 14, (chip.top + chip.bottom) * 0.5f, 10, hover ? uiAccent() : gray(0.8f));
        textLine(label, chip.left + 25, (chip.top + chip.bottom) * 0.5f, w - 30, desktop_small_font_.Get(), hover ? palette::text() : gray(0.85f));
        desktop_chip_add_ = chip;
        y += chip_h + 22;
    }
    // The rating link, the description and the facts.
    if (!pane.rating.empty()) {
        const auto label = pane.rating + L"  \u2197";
        const float w = std::min(textAdvance(label, desktop_font_.Get()) + 4, width);
        const bool hover = view.hover.target == Target::pane_rating;
        textLine(label, left, y + 8, w, desktop_font_.Get(), uiAccent(hover ? 1.0f : 0.85f));
        if (hover) { brush(uiAccent(0.6f)); target_->FillRectangle(D2D1::RectF(left, y + 17, left + w, y + 18), solid_.Get()); }
        desktop_rating_ = {left, y - 2, left + w, y + 20};
        y += 34;
    }
    if (!pane.protondb.empty()) { // Linux: how it runs through Proton, and on the Steam Deck (a line each when narrow).
        std::vector<std::wstring> lines{pane.protondb + L"  \u2197"};
        if (const auto dot = pane.protondb.find(L" \u00B7 "); dot != std::wstring::npos && textAdvance(lines[0], desktop_font_.Get()) + 4 > width)
            lines = {pane.protondb.substr(0, dot), pane.protondb.substr(dot + 3) + L"  \u2197"};
        const bool hover = view.hover.target == Target::pane_protondb;
        float widest = 0;
        for (size_t i = 0; i < lines.size(); ++i) {
            const float w = std::min(textAdvance(lines[i], desktop_font_.Get()) + 4, width);
            const float line_y = y + 8 + static_cast<float>(i) * 22;
            textLine(lines[i], left, line_y, w, desktop_font_.Get(), uiAccent(hover ? 1.0f : 0.85f));
            if (hover) { brush(uiAccent(0.6f)); target_->FillRectangle(D2D1::RectF(left, line_y + 9, left + w, line_y + 10), solid_.Get()); }
            widest = std::max(widest, w);
        }
        desktop_protondb_ = {left, y - 2, left + widest, y + 20 + static_cast<float>(lines.size() - 1) * 22};
        y += 34 + static_cast<float>(lines.size() - 1) * 22;
    }
    textLine(upper(tr(L"Opis")), left, y, width, desktop_label_font_.Get(), palette::muted());
    y += 14;
    const auto& details = pane.details;
    if (!details.description.empty()) y += paragraph(details.description, left, y, width, desktop_font_.Get(), gray(0.80f), 60) + 18;
    else {
        if (pane.details_loading) drawSkeleton(D2D1::RectF(left, y, left + width, y + 66), 3, 22);
        else textLine(tr(L"Opis gry nie jest dostępny."), left, y + 9, width, desktop_font_.Get(), palette::muted());
        y += 36;
    }
    std::vector<std::pair<const wchar_t*, const std::wstring*>> facts;
    const auto released = render_detail::releaseText(details.release_date);
    const std::pair<const wchar_t*, const std::wstring*> all_facts[] = {
        {trKey(L"Developer"), &details.developer}, {trKey(L"Wydawca"), &details.publisher}, {trKey(L"Premiera"), &released},
        {trKey(L"Gatunek"), &details.genres}, {trKey(L"Gracze"), &details.players}};
    for (const auto& row : all_facts) if (!row.second->empty()) facts.push_back(row);
    if (!facts.empty()) {
        textLine(upper(tr(L"Informacje")), left, y, width, desktop_label_font_.Get(), palette::muted());
        y += 14;
        const float label_width = std::min(96.0f, width * 0.34f);
        for (const auto& [label, value] : facts) {
            textLine(tr(label), left, y + 12, label_width, desktop_font_.Get(), palette::muted());
            // Developer and Wydawca: links to the studio's games (its section, or the search for it).
            const int studio = value == &details.developer ? 0 : value == &details.publisher ? 1 : -1;
            const bool hover = studio >= 0 && view.hover == Hit{Target::pane_studio, studio};
            const auto ink = studio < 0 ? gray(0.88f) : uiAccent(hover ? 1.0f : 0.85f);
            const float value_left = left + label_width + 10, value_top = y + 12 - typeMetrics(desktop_font_.Get()).baseline + typeMetrics(desktop_font_.Get()).cap * 0.5f;
            const float h = paragraph(*value, value_left, value_top, width - label_width - 10, desktop_font_.Get(), ink, 3);
            if (studio >= 0) {
                const float w = std::min(textAdvance(*value, desktop_font_.Get()) + 2, width - label_width - 10);
                desktop_studios_[static_cast<size_t>(studio)] = {value_left - 2, y, value_left + w + 2, y + std::max(24.0f, h + 4)};
                if (h < 30) { // Underlined: it opens the studio's games (stronger under the pointer).
                    brush(hover ? uiAccent(0.7f) : gray(1, 0.22f));
                    target_->FillRectangle(D2D1::RectF(value_left, y + 21, value_left + w, y + 22), solid_.Get());
                }
            }
            y += std::max(26.0f, h + 6);
        }
    }
    desktop_pane_height_ = y + s - title_h + 28;
    target_->PopAxisAlignedClip();
    // A long page shows how far it has scrolled.
    const float page = layout.height - title_h;
    if (desktop_pane_height_ > page + 1) {
        const float track = page - 12, length = std::max(30.0f, track * page / desktop_pane_height_);
        const float top = title_h + 6 + (track - length) * std::clamp(s / (desktop_pane_height_ - page), 0.0f, 1.0f);
        desktopFill({p.right - 6, top, p.right - 3, top + length}, 1.5f, gray(1, 0.14f));
    }
}
