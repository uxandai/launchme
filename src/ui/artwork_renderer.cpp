#include "renderer.hpp"
#include "renderer_internal.hpp"

namespace {
D2D1_COLOR_F shade(float value, float alpha = 1) { return D2D1::ColorF(value, value, value, alpha); }
using render_detail::box;
}

void Renderer::resetArtworkThumbnails(size_t count) {
    artwork_thumbnails_.clear();
    artwork_thumbnails_.resize(count);
}

void Renderer::setArtworkThumbnail(size_t index, std::vector<uint8_t> bytes, bool logo) {
    if (index >= artwork_thumbnails_.size()) return;
    auto& image = artwork_thumbnails_[index];
    image = {};
    image.hero = true;
    image.logo = logo;
    if (!bytes.empty()) image.pixels = std::make_shared<const std::vector<uint8_t>>(std::move(bytes));
    createBitmap(image);
}

void Renderer::drawArtworkBrowser() {
    target_->Clear(D2D1::ColorF(0.022f, 0.024f, 0.030f));
    if (!context_->artwork_browser) return;
    const auto& state = *context_->artwork_browser;
    // The wallpaper (or, in the wallpaper picker, the focused animated one's preview) dimmed behind the gallery.
    if ((wallpaper_places_.artwork || state.wallpaper) && drawWallpaperBackdrop(1.0f)) {
        brush(D2D1::ColorF(0.012f, 0.014f, 0.018f, state.wallpaper ? 0.58f : 0.74f));
        target_->FillRectangle(D2D1::RectF(0, 0, viewport_.width, viewport_.height), solid_.Get());
    }
    const auto layout = state.layout(viewport_.width, viewport_.height);
    const auto base = layout.base();
    const auto fill = [&](settings_ui::Rect r, D2D1_COLOR_F color, float radius = 12) {
        brush(color);
        target_->FillRoundedRectangle(D2D1::RoundedRect(box(r), radius, radius), solid_.Get());
    };
    const auto outline = [&](settings_ui::Rect r, D2D1_COLOR_F color, float thickness = 1) {
        brush(color);
        target_->DrawRoundedRectangle(D2D1::RoundedRect(box(r), 12, 12), solid_.Get(), thickness);
    };
    // Rail entries, list rows and the search field: pills without an outline.
    const auto fillPill = [&](settings_ui::Rect r, D2D1_COLOR_F color) {
        brush(color);
        target_->FillRoundedRectangle(pill(box(r)), solid_.Get());
    };
    // Same header, rail and spacing as Settings (settings_ui::Layout).
    const auto center = [](settings_ui::Rect r) { return (r.top + r.bottom) * 0.5f; };
    textGroup(state.game.name, {}, layout.left(), settings_ui::Layout::heading_cap_top, layout.right() - layout.left(),
              settings_heading_font_.Get(), shade(0.95f), settings_body_font_.Get(), shade(0.64f), 1, true);
    const auto search = layout.searchBox(), search_button = layout.searchButton(), clear = layout.clearButton();
    fillPill(search, shade(1, state.search_focus ? 0.08f : 0.04f));
    if (state.search_focus) { // The accent outline only while it takes typing.
        brush(uiAccent(0.9f));
        target_->DrawRoundedRectangle(pill(box(search)), solid_.Get(), 1.5f);
    }
    const float text_left = search.left + 16, text_right = (state.query.empty() ? search.right : clear.left) - 12;
    // The typed text's extent: a caret after it, or a highlight when the next key replaces it.
    float ink = 0;
    if (!state.query.empty()) {
        DWRITE_TEXT_METRICS metrics{};
        if (const auto measured = text_layouts_.get(dwrite_.Get(), state.query, menu_font_.Get(), 4096, 40); measured && SUCCEEDED(measured->GetMetrics(&metrics)))
            ink = std::min(metrics.widthIncludingTrailingWhitespace, text_right - text_left);
    }
    if (state.search_focus && state.query_selected && ink > 0)
        fill({text_left - 3, center(search) - 15, text_left + ink + 3, center(search) + 15}, uiAccent(0.35f), 4);
    const std::wstring placeholder = state.wallpaper ? tr(L"Szukaj tapet, np. nature, cyberpunk, space") : tr(L"Nazwa gry, Steam AppID lub link SteamGridDB");
    textLine(state.query.empty() ? placeholder : state.query, text_left, center(search),
             text_right - text_left, menu_font_.Get(), shade(state.query.empty() ? 0.45f : 0.92f));
    if (state.search_focus && !state.query_selected) line(text_left + ink + 2, center(search) - 14, text_left + ink + 2, center(search) + 14, shade(0.92f), 1.6f);
    if (!state.query.empty()) {
        const auto clear_ink = drawButton(box(clear), 0, 1, false);
        textLine(tr(L"Wyczyść"), clear.left, center(clear), clear.right - clear.left, button_font_.Get(), clear_ink);
    }
    const auto search_ink = drawButton(box(search_button), 0, 1, false);
    textLine(tr(L"Szukaj"), search_button.left, center(search_button), search_button.right - search_button.left, button_font_.Get(), search_ink);
    // "Kadruj" (covers and backgrounds): the crop editor, white with the focus.
    if (state.croppable()) {
        const auto crop = layout.cropButton();
        const auto crop_ink = drawButton(box(crop), state.crop_focus ? 1.0f : 0.0f, 1, false);
        textLine(tr(L"Kadruj"), crop.left, center(crop), crop.right - crop.left, button_font_.Get(), crop_ink, DWRITE_TEXT_ALIGNMENT_CENTER);
    }
    // "Źródło: …" at the row's right end (emulated games): a dropdown, white with the focus.
    if (state.providerChoice()) {
        const auto source = layout.providerButton();
        const auto source_ink = drawButton(box(source), state.provider_focus ? 1.0f : 0.0f, 1, false);
        const float arrow = source.right - 30, cy = center(source), d = state.provider_open ? -1.0f : 1.0f;
        textLine(std::wstring(tr(L"Źródło")) + L": " + artwork_ui::providerName(state.activeProvider()), source.left + 22, cy, arrow - source.left - 34,
                 button_font_.Get(), source_ink);
        line(arrow - 6, cy - 3 * d, arrow, cy + 3 * d, source_ink, 1.8f);
        line(arrow, cy + 3 * d, arrow + 6, cy - 3 * d, source_ink, 1.8f);
    }
    // The rail: the types, then "Z dysku…" as a glass pill; one white pill glides to the focused entry
    // (ViewContext::artwork_glide). Back: the small circle with a dot at the top left, white with the focus.
    const bool rail_focus = state.railFocus();
    const bool type_focus = rail_focus && !state.railButton();
    const auto back = layout.backButton();
    {
        const auto circle = backCircle(back.left, back.top, back.right, back.bottom);
        const auto back_ink = drawButton(circle, rail_focus && state.back_focus ? 1.0f : 0.0f, 1, false);
        brush(back_ink);
        target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F((circle.left + circle.right) * 0.5f, (circle.top + circle.bottom) * 0.5f), 3.5f, 3.5f), solid_.Get());
    }
    if (!type_focus) fillPill(base.rail(state.category), shade(1, 0.07f)); // The type in view.
    drawButtonGlass(box(layout.localButton()), 1, false);
    if (rail_focus && !state.back_focus && !state.page_focus) {
        const auto target = state.local_focus ? layout.localButton() : base.rail(state.category);
        auto r = box(target);
        if (context_->artwork_glide) { const auto& g = *context_->artwork_glide; r = D2D1::RectF(g.left, g.top, g.right, g.bottom); }
        drawButtonFocus(r);
    }
    for (int i = 0; i < state.railCount(); ++i) {
        const auto r = base.rail(i);
        const bool active = state.category == i, focus = active && type_focus;
        textLine(tr(state.railName(i)), r.left + 25, center(r), r.right - r.left - 50,
                 category_font_.Get(), focus ? shade(0.08f) : active ? shade(0.95f) : shade(0.57f));
    }
    // "Z dysku…" opens the type/file panel.
    const auto caption = [&](settings_ui::Rect r, const std::wstring& label, bool focus) {
        textLine(label, r.left + 25, center(r), r.right - r.left - 50, menu_font_.Get(), buttonInk(rail_focus && focus ? 1.0f : 0.0f));
    };
    caption(layout.localButton(), tr(L"Z dysku…"), state.local_focus);
    if (!state.results_mode && (state.page > 0 || state.more)) {
        for (bool next : {false, true}) {
            const auto r = layout.pageButton(next);
            const bool enabled = next ? state.more : state.page > 0;
            const bool focused = rail_focus && state.page_focus == (next ? 1 : -1); // The pad/keyboard focus.
            const auto glyph = drawButton(box(r), focused ? 1.0f : 0.0f, 1, false, false, enabled);
            const float cx = (r.left + r.right) * 0.5f, cy = (r.top + r.bottom) * 0.5f;
            const float d = next ? 1.0f : -1.0f;
            line(cx - 4 * d, cy - 6, cx + 3 * d, cy, glyph, 1.8f);
            line(cx + 3 * d, cy, cx - 4 * d, cy + 6, glyph, 1.8f);
        }
        const float middle = (layout.pageButton(false).right + layout.pageButton(true).left) * 0.5f;
        textLine(std::to_wstring(state.page + 1), middle - 26, center(layout.pageButton(true)), 52, button_font_.Get(), shade(0.58f));
    }
    if (!state.status.empty()) textLine(state.status, layout.left(), search.bottom + 24, layout.right() - layout.left(),
                                        settings_body_font_.Get(), shade(0.64f));
    target_->PushAxisAlignedClip(D2D1::RectF(layout.left() - 4, layout.top - 4, layout.right() + 4, layout.bottom()),
                                 D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    if (state.results_mode) {
        for (size_t i = 0; i < state.games.size(); ++i) {
            const auto r = layout.gameRow(i, state.scroll);
            if (r.bottom < layout.top || r.top >= layout.bottom()) continue;
            fillPill(r, shade(1, state.gallery && state.selected == i ? 0.12f : 0.04f));
            // Same-titled games differ by their year ("Revenant (1999)" / "Revenant (2024)").
            const auto& game = state.games[i];
            text(game.year ? game.name + L" (" + std::to_wstring(game.year) + L")" : game.name,
                 r.left + 18, r.top + 12, r.right - r.left - 36, 36, menu_font_.Get(), shade(0.91f));
        }
        target_->PopAxisAlignedClip(); return;
    }
    for (size_t i = 0; i < state.assets.size(); ++i) {
        const auto r = layout.cell(i, state.scroll);
        if (r.bottom < layout.top || r.top >= layout.bottom()) continue;
        const auto& asset = state.assets[i];
        fill(r, shade(0.06f));
        const bool selected = state.gallery && state.selected == i;
        if (i < artwork_thumbnails_.size() && artwork_thumbnails_[i].bitmap) {
            const auto& image = artwork_thumbnails_[i];
            const auto size = image.bitmap->GetPixelSize();
            const auto source = image.has_ink_bounds ? image.ink_bounds :
                D2D1::RectF(0, 0, static_cast<float>(size.width), static_cast<float>(size.height));
            const float margin = state.kind() == ArtworkKind::logo ? 24.0f : 5.0f;
            const float scale = std::min((r.right - r.left - margin * 2) / (source.right - source.left),
                                         (r.bottom - r.top - margin * 2) / (source.bottom - source.top));
            const float w = (source.right - source.left) * scale, h = (source.bottom - source.top) * scale;
            const float x = (r.left + r.right - w) * 0.5f, y = (r.top + r.bottom - h) * 0.5f;
            target_->DrawBitmap(image.bitmap.Get(), D2D1::RectF(x, y, x + w, y + h), state.applying && !selected ? 0.55f : 1.0f,
                                 D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, &source);
        }
        outline(r, selected ? shade(0.95f, 0.8f) : shade(1, 0.12f), selected ? 2.0f : 1.0f);
        if (asset.id == state.applied_id) {
            const float x = r.right - 20, y = r.top + 20;
            brush(shade(0.95f));
            target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(x, y), 11, 11), solid_.Get());
            line(x - 5, y, x - 1, y + 4, shade(0.10f), 1.7f);
            line(x - 1, y + 4, x + 6, y - 4, shade(0.10f), 1.7f);
        }
        // ScreenScraper / ES-DE media carry no dimensions: their source label alone.
        const auto size = asset.width && asset.height ? std::to_wstring(asset.width) + L" × " + std::to_wstring(asset.height) : std::wstring{};
        text(size.empty() ? asset.author : size + (asset.author.empty() ? L"" : L" · " + asset.author), r.left + 3, r.bottom + 8,
             r.right - r.left - 6, 27, toast_sub_font_.Get(), shade(selected ? 0.72f : 0.44f));
    }
    target_->PopAxisAlignedClip();
    const float maximum = layout.maxScroll(state.assets.size());
    if (maximum > 0) {
        const float length = layout.bottom() - layout.top;
        const float thumb = std::max(30.0f, length * length / (length + maximum));
        const float y = layout.top + (length - thumb) * state.scroll / maximum;
        fill({layout.right() + 15, y, layout.right() + 18, y + thumb}, shade(1, 0.30f), 1.5f);
    }
    // "Źródło"'s open list over the gallery: the focused row lit, the one in use checked.
    if (state.provider_open && state.providerChoice()) {
        const auto list = layout.providerList();
        fill(list, shade(0.09f, 0.98f), 16);
        if (panel_borders_) { brush(shade(1, 0.16f)); target_->DrawRoundedRectangle(D2D1::RoundedRect(box(list), 16, 16), solid_.Get(), 1.0f); }
        for (int i = 0; i < static_cast<int>(state.choices.size()); ++i) {
            const auto r = layout.providerRow(i);
            const bool focused = state.provider_row == i;
            if (focused) fillPill(r, shade(0.95f));
            const auto provider = state.choices[static_cast<size_t>(i)];
            textLine(artwork_ui::providerName(provider), r.left + 18, center(r), r.right - r.left - 60, menu_font_.Get(), focused ? shade(0.08f) : shade(0.91f));
            if (provider == state.activeProvider()) {
                const float y = center(r);
                const auto mark = focused ? shade(0.08f) : uiAccent();
                line(r.right - 31, y, r.right - 26, y + 5, mark, 2);
                line(r.right - 26, y + 5, r.right - 17, y - 5, mark, 2);
            }
        }
    }
    if (!state.local_open) return;
    // "Z dysku…" panel: the artwork type (check mark), then "Wskaż plik…".
    brush(shade(0, 0.78f));
    target_->FillRectangle(D2D1::RectF(0, 0, viewport_.width, viewport_.height), solid_.Get());
    const auto panel = layout.localPanel();
    fill(panel, shade(0.07f), 18);
    if (panel_borders_) {
        brush(shade(1, 0.16f)); // Panel corners (18), not the 12 px of outline().
        target_->DrawRoundedRectangle(D2D1::RoundedRect(box(panel), 18, 18), solid_.Get(), 1.0f);
    }
    textGroup(tr(L"Grafika z dysku"), {}, panel.left + 28, panel.top + 28, panel.right - panel.left - 56,
              category_font_.Get(), shade(0.95f), settings_body_font_.Get(), shade(0.62f), 1, true);
    for (int i = 0; i < static_cast<int>(artwork_ui::kinds.size()); ++i) {
        const auto r = layout.localChoice(i);
        if (state.local_row == i) fillPill(r, shade(1, 0.10f));
        textLine(tr(artwork_ui::names[i]), r.left + 18, center(r), r.right - r.left - 66, settings_body_font_.Get(), shade(0.91f));
        if (state.local_kind == i) {
            const float y = center(r);
            line(r.right - 31, y, r.right - 26, y + 5, uiAccent(), 2);
            line(r.right - 26, y + 5, r.right - 17, y - 5, uiAccent(), 2);
        }
    }
    const auto pick = layout.localPick();
    const bool pick_focus = state.local_row == artwork_ui::State::local_pick_row;
    const auto pick_ink = drawButton(box(pick), pick_focus ? 1.0f : 0.0f, 1, false);
    textLine(tr(L"Wskaż plik…"), pick.left, center(pick), pick.right - pick.left, button_font_.Get(), pick_ink);
}
