// Desktop mode's dialogs, part three (src/ui/desktop_sheet.hpp): Pobierania (every transfer with its
// cover, state and progress; a row opens its store's client or plugin as in Tryb TV) and Glance (its
// sections as tabs, the feed as cards with their pictures; an article opens in the user's browser).
// Measured while drawn: every control records the area that answers for it (desktopHotspots).
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
float middle(Rect r) { return (r.top + r.bottom) * 0.5f; }
}

// ---- Pobierania ----------------------------------------------------------------------------------

void Renderer::drawDesktopDownloadsPage() {
    const auto& view = *context_->desktop_sheet;
    desktop_hotspots_.clear();
    const auto sheet = detailsSheet(viewport_.width, viewport_.height, sheetDock());
    drawDesktopSheetFrame(sheet, view, tr(L"Pobierania"), false);
    desktopHot({Kind::close}, sheet.close());
    const auto p = sheet.panel(), h = sheet.header();
    const auto* state = context_->downloads;
    if (!state) return;
    const float left = p.left + 24, right = p.right - 24, s = view.scroll;
    target_->PushAxisAlignedClip(D2D1::RectF(p.left + 1, h.bottom, p.right - 1, p.bottom - 1), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    float y = h.bottom + 16 - s;
    const auto hidden = [&](size_t i) { return i < view.download_hidden.size() && view.download_hidden[i]; };
    bool any = false, clearable = false;
    for (size_t i = 0; i < state->rows.size(); ++i) if (!hidden(i)) { any = true; clearable |= !state->rows[i].moving; }
    // Wyczyść listę, beside ✕: puts away what is not moving.
    if (clearable) {
        const auto label = std::wstring(tr(L"Wyczyść listę"));
        const float w = textAdvance(label, desktop_button_font_.Get()) + 30;
        const Rect r{p.right - 56 - w, p.top + 12, p.right - 56, p.top + 44};
        desktopButton(r, 1, view.hover == SheetHit{Kind::action, 1} ? 1.0f : 0.0f, view.pressed == SheetHit{Kind::action, 1}, true, 0, label);
        desktopHot({Kind::action, 1}, r);
    }
    if (!any) {
        const float cx = (p.left + p.right) * 0.5f, cy = (h.bottom + p.bottom) * 0.42f;
        brush(gray(1, 0.05f));
        target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), 30, 30), solid_.Get());
        desktopGlyph(glyph::download, cx, cy, 22, gray(0.65f));
        textLine(tr(L"Nic się teraz nie pobiera"), p.left + 24, cy + 52, p.right - p.left - 48, desktop_title_font_.Get(), palette::text(), DWRITE_TEXT_ALIGNMENT_CENTER);
        textLine(tr(L"Instalacje ze sklepów pojawią się tutaj."), p.left + 24, cy + 78, p.right - p.left - 48, desktop_font_.Get(), palette::muted(),
                 DWRITE_TEXT_ALIGNMENT_CENTER);
        desktop_sheet_height_ = 0;
        target_->PopAxisAlignedClip();
        return;
    }
    const float row_height = 84;
    for (size_t i = 0; i < state->rows.size(); ++i) {
        if (hidden(i)) continue;
        const auto& row = state->rows[i];
        const Rect r{left, y, right, y + row_height - 8};
        const SheetHit hit{Kind::row, static_cast<int>(i)};
        const float hover = view.hover == hit ? 1.0f : 0.0f;
        if (r.bottom > h.bottom && r.top < p.bottom) desktopHot(hit, r); // Before its button: the button stays on top.
        desktopFill(r, 10, gray(1, 0.035f + 0.03f * hover));
        desktopStroke(r, 10, gray(1, 0.05f + 0.06f * hover));
        // The game's cover (the cards of this screen hold its games).
        const Rect cover{r.left + 12, r.top + 10, r.left + 12 + (r.bottom - r.top - 20), r.bottom - 10};
        desktopFill(cover, 8, rgb(0.12f, 0.13f, 0.15f));
        if (i < covers_.size() && covers_[i].bitmap) fillImage(covers_[i], box(cover), 8, reveal(covers_[i]));
        const float text_left = cover.right + 16;
        const std::wstring name = cards_ && i < cards_->size() ? (*cards_)[i].game.name : row.store;
        float meta_right = r.right - 16;
        // Its button: Anuluj (the launcher's own install) or Otwórz (the store's client, the plugin).
        if (i < view.download_actions.size()) {
            const SheetHit action{Kind::tile_action, static_cast<int>(i)};
            const float w = std::max(92.0f, textAdvance(view.download_actions[i], desktop_button_font_.Get()) + 28);
            const Rect b{r.right - 14 - w, r.top + 12, r.right - 14, r.top + 42};
            desktopButton(b, 1, view.hover == action ? 1.0f : 0.0f, view.pressed == action, true, 0, view.download_actions[i]);
            if (r.bottom > h.bottom && r.top < p.bottom) desktopHot(action, b);
            meta_right = b.left - 12;
        }
        if (row.fraction) {
            const auto percent = std::to_wstring(static_cast<int>(std::lround(*row.fraction * 100))) + L"%";
            const float w = textAdvance(percent, desktop_strong_font_.Get());
            textLine(percent, meta_right - w, r.top + 26, w + 2, desktop_strong_font_.Get(), row.moving ? palette::text() : gray(0.7f));
            meta_right -= w + 16;
        }
        textLine(name, text_left, r.top + 26, meta_right - text_left, desktop_strong_font_.Get(), palette::text());
        std::wstring meta = row.store;
        if (!row.status.empty()) meta += (meta.empty() ? L"" : L"  ·  ") + row.status;
        if (!row.amount.empty()) meta += (meta.empty() ? L"" : L"  ·  ") + row.amount;
        textLine(meta, text_left, r.top + 46, r.right - 16 - text_left, desktop_small_font_.Get(), palette::muted());
        // Progress: its share, a gliding segment while none is known; still when paused.
        const float bar_l = text_left, bar_r = r.right - 16, by = r.bottom - 14;
        desktopFill({bar_l, by - 2, bar_r, by + 2}, 2, gray(1, 0.1f));
        if (row.fraction) desktopFill({bar_l, by - 2, bar_l + (bar_r - bar_l) * std::clamp(state->bar(i, row), 0.0f, 1.0f), by + 2}, 2,
                                      row.moving ? uiAccent() : gray(0.55f));
        else if (row.moving) {
            const float span = (bar_r - bar_l) * 0.25f, phase = std::fmod(elapsed_ * 0.6f, 1.0f), x0 = bar_l + (bar_r - bar_l + span) * phase - span;
            desktopFill({std::max(bar_l, x0), by - 2, std::min(bar_r, x0 + span), by + 2}, 2, uiAccent());
        }
        y += row_height;
    }
    desktop_sheet_height_ = y + s - h.bottom + 8;
    target_->PopAxisAlignedClip();
}

// ---- Glance -------------------------------------------------------------------------------------

void Renderer::drawDesktopGlance() {
    const auto& view = *context_->desktop_sheet;
    desktop_hotspots_.clear();
    const auto sheet = artworkSheet(viewport_.width, viewport_.height, sheetDock());
    drawDesktopSheetFrame(sheet, view, L"Glance", false);
    desktopHot({Kind::close}, sheet.close());
    const auto* state = context_->glance;
    if (!state) return;
    const auto p = sheet.panel(), h = sheet.header();
    const auto hover = [&](SheetHit hit) { return view.hover == hit ? 1.0f : 0.0f; };
    // Refresh, beside ✕.
    {
        const Rect r{p.right - 92, p.top + 10, p.right - 56, p.top + 46};
        const SheetHit hit{Kind::action, -3};
        if (hover(hit) > 0) desktopFill(r, 8, gray(1, 0.08f));
        if (state->loading) desktopArc((r.left + r.right) * 0.5f, middle(r), 7, elapsed_ * 5.0f, 4.2f, gray(0.8f), 1.6f);
        else desktopGlyph(glyph::refresh, (r.left + r.right) * 0.5f, middle(r), 13, gray(hover(hit) > 0 ? 0.98f : 0.72f));
        if (!state->loading) desktopHot(hit, r);
    }
    // The sections as tabs; the active pill glides between them on a spring.
    float tx = p.left + 20;
    const float ty = h.bottom + 12;
    std::array<Rect, glance::section_count> tab_rects{};
    std::array<std::wstring, glance::section_count> tab_labels{};
    int active_tab = 0;
    for (int tab = 0; tab < glance::section_count; ++tab) {
        const int section = glance_ui::sectionAt(tab);
        tab_labels[static_cast<size_t>(tab)] = glance_ui::title(section);
        // Wide enough for the label in bold too (the active one): never cut ("Your ga…").
        const auto& label = tab_labels[static_cast<size_t>(tab)];
        const float w = std::ceil(std::max(textAdvance(label, desktop_font_.Get()), textAdvance(label, desktop_strong_font_.Get()))) + 32;
        tab_rects[static_cast<size_t>(tab)] = {tx, ty, tx + w, ty + 34};
        if (state->section == section) active_tab = tab;
        tx += w + 6;
    }
    {
        const float nav = std::clamp(view.nav_glide.value_or(static_cast<float>(active_tab)), 0.0f, static_cast<float>(glance::section_count - 1));
        const size_t i0 = static_cast<size_t>(std::floor(nav)), i1 = std::min<size_t>(glance::section_count - 1, i0 + 1);
        const float t = nav - static_cast<float>(i0);
        const Rect pill{std::lerp(tab_rects[i0].left, tab_rects[i1].left, t), ty,
                        std::lerp(tab_rects[i0].right, tab_rects[i1].right, t), ty + 34};
        desktopFill(pill, 17, gray(1, 0.1f));
    }
    for (int tab = 0; tab < glance::section_count; ++tab) {
        const int section = glance_ui::sectionAt(tab);
        const auto& r = tab_rects[static_cast<size_t>(tab)];
        const auto& label = tab_labels[static_cast<size_t>(tab)];
        const SheetHit hit{Kind::nav, section};
        const bool active = state->section == section;
        if (!active && hover(hit) > 0) desktopFill(r, 17, gray(1, 0.05f));
        textLine(label, r.left + 14, middle(r), r.right - r.left - 24, active ? desktop_strong_font_.Get() : desktop_font_.Get(), active ? palette::text() : gray(0.72f));
        if (view.keyboard && state->focus == -1 && active) desktopFocusRing(r, 17);
        desktopHot(hit, r);
    }
    const float list_top = ty + 48, list_bottom = p.bottom - 12, left = p.left + 20, right = p.right - 20, s = view.scroll;
    target_->PushAxisAlignedClip(D2D1::RectF(left - 6, list_top, right + 6, list_bottom), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    const auto& items = state->feed.items;
    const float gap = 16, width = right - left;
    const int columns = std::max(1, static_cast<int>((width + gap) / (250 + gap)));
    const float cell_w = (width - gap * static_cast<float>(columns - 1)) / static_cast<float>(columns), image_h = std::round(cell_w * 9 / 16);
    const float step = image_h + 92;
    const size_t shown = items.empty() && state->loading ? static_cast<size_t>(columns) * 2 : items.size();
    for (size_t i = 0; i < shown; ++i) {
        const float x = left + static_cast<float>(i % static_cast<size_t>(columns)) * (cell_w + gap);
        const float y = list_top + 8 + static_cast<float>(i / static_cast<size_t>(columns)) * step - s;
        const Rect card{x, y, x + cell_w, y + step - gap};
        if (card.bottom < list_top || card.top > list_bottom) continue;
        const Rect image{card.left, card.top, card.right, card.top + image_h};
        if (items.empty()) { // Loading: the cards' shape, pulsing.
            const float pulse = 0.03f + 0.02f * (0.5f + 0.5f * std::sin(elapsed_ * 2.6f - static_cast<float>(i) * 0.35f));
            desktopFill(image, 10, gray(1, pulse));
            desktopFill({card.left, image.bottom + 14, card.left + cell_w * 0.85f, image.bottom + 26}, 4, gray(1, pulse));
            continue;
        }
        const auto& item = items[i];
        const SheetHit hit{Kind::tile, -1, static_cast<int>(i)};
        const float hv = hover(hit);
        desktopShadow(image, 10, 0.4f + 0.6f * hv);
        desktopFill(image, 10, rgb(0.11f, 0.12f, 0.14f));
        if (i < glance_thumbnails_.size() && glance_thumbnails_[i].bitmap) {
            ComPtr<ID2D1RoundedRectangleGeometry> clip;
            factory_->CreateRoundedRectangleGeometry(D2D1::RoundedRect(box(image), 10, 10), clip.GetAddressOf());
            fillImage(glance_thumbnails_[i], box(image), 10, reveal(glance_thumbnails_[i]), false, clip.Get());
        } else desktopGlyph(glyph::picture, (image.left + image.right) * 0.5f, middle(image), 22, gray(0.3f));
        desktopStroke(image, 10, gray(1, 0.06f + 0.14f * hv));
        if (state->focus == static_cast<int>(i)) desktopFocusRing(image, 10);
        paragraph(item.title, card.left + 2, image.bottom + 10, cell_w - 4, desktop_strong_font_.Get(), palette::text(), 2);
        std::wstring meta = item.source;
        if (!item.date.empty()) meta += (meta.empty() ? L"" : L"  ·  ") + item.date;
        textLine(meta, card.left + 2, card.bottom - 10, cell_w - 4, desktop_small_font_.Get(), palette::muted());
        desktopHot(hit, card);
    }
    if (items.empty() && !state->loading)
        textLine(tr(L"Brak danych"), left, list_top + 40, width, desktop_font_.Get(), palette::muted(),
                 DWRITE_TEXT_ALIGNMENT_CENTER);
    target_->PopAxisAlignedClip();
    const size_t rows = (shown + static_cast<size_t>(columns) - 1) / static_cast<size_t>(columns);
    desktop_sheet_height_ = 16 + static_cast<float>(rows) * step;
    const float view_h = list_bottom - list_top;
    if (desktop_sheet_height_ > view_h + 1) {
        const float thumb = std::max(30.0f, view_h * view_h / desktop_sheet_height_);
        const float y = list_top + (view_h - thumb) * std::clamp(s / (desktop_sheet_height_ - view_h), 0.0f, 1.0f);
        desktopFill({p.right - 9, y, p.right - 5, y + thumb}, 2, gray(1, 0.22f));
    }
}

// ---- Urządzenia, kadrowanie and plugin pages in a sheet ----------------------------------------------

// The part of the sheet's body the screen is drawn in: kadrowanie keeps room around its cover.
settings_ui::Rect Renderer::desktopEmbedArea() const {
    const auto view = desktopViewport();
    const auto body = embedBody(embedSheet(view.width, view.height));
    if (painted_screen_ != ScreenMode::crop) return body;
    return {body.left + 36, body.top + 28, body.right - 36, body.bottom - 28};
}

Viewport Renderer::desktopEmbedViewport() const {
    const auto view = desktopViewport();
    const auto body = desktopEmbedArea();
    const float w = std::max(1.0f, body.right - body.left), h = std::max(1.0f, body.bottom - body.top);
    // Their layouts want about 1400 × 820 canvas units; a smaller body shrinks them a little.
    const float k = std::clamp(std::min(w / 1400.0f, h / 820.0f), 0.5f, 1.0f);
    Viewport embed;
    embed.scale = view.scale * k;
    embed.width = w / k;
    embed.height = h / k;
    return embed;
}

D2D1_POINT_2F Renderer::desktopEmbedOrigin() const {
    const auto view = desktopViewport();
    const auto body = desktopEmbedArea();
    return D2D1::Point2F(body.left * view.scale, body.top * view.scale);
}

void Renderer::drawDesktopEmbedded() {
    const auto& view = *context_->desktop_embed;
    const auto sheet = embedSheet(viewport_.width, viewport_.height);
    drawDesktopSheetFrame(sheet, view, context_->desktop_embed_title, false);
    const auto body = embedBody(sheet);
    target_->PushAxisAlignedClip(box(body), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    // The screen's own drawing on its canvas; it stays the viewport for the layers above it and for
    // the launcher's hit tests (which subtract desktopEmbedOrigin first).
    viewport_ = desktopEmbedViewport();
    const auto origin = desktopEmbedOrigin();
    canvas_offset_ = D2D1::Matrix3x2F::Translation(origin.x, origin.y); // Kadrowanie's cover sets its own transform from it.
    target_->SetTransform(canvasBase());
    switch (context_->screen) {
    case ScreenMode::devices: drawDevices(); break;
    case ScreenMode::crop: drawCropEditor(); break;
    case ScreenMode::plugin_page: drawPluginPage(); break;
    default: break;
    }
    target_->SetTransform(canvasBase());
    canvas_offset_ = D2D1::Matrix3x2F::Identity();
    target_->PopAxisAlignedClip();
}

// ---- Sklep ---------------------------------------------------------------------------------------

// The frame every store shares on the desktop (the catalog and a store plugin page): the tabs in the
// header, the search field with its clear button and Szukaj, and the state line below it.
void Renderer::drawDesktopStoreChrome(const desktop_ui::Sheet& sheet, const desktop_ui::SheetView& view, const std::wstring& query, size_t caret,
                                      const std::wstring& placeholder, const std::wstring& state, bool field_shown) {
    const bool tabbed = !context_->store_tabs.empty();
    drawDesktopSheetFrame(sheet, view, tabbed ? std::wstring{} : std::wstring(tr(L"Sklep")), false);
    desktopHot({Kind::close}, sheet.close());
    const auto p = sheet.panel(), h = sheet.header();
    const auto hovered = [&](SheetHit hit) { return view.hover == hit ? 1.0f : 0.0f; };
    // The Stores' tabs in the header, where the title was: one pill per store plugin (the active one filled).
    if (tabbed) {
        float tx = p.left + 20;
        const float ty = middle(h) - 17;
        for (size_t i = 0; i < context_->store_tabs.size(); ++i) {
            const auto& label = context_->store_tabs[i].title;
            const bool active = static_cast<int>(i) == context_->store_tab;
            const float w = std::ceil(textAdvance(label, active ? desktop_strong_font_.Get() : desktop_font_.Get())) + 32;
            const Rect r{tx, ty, tx + w, ty + 34};
            const SheetHit hit{Kind::nav, static_cast<int>(i)};
            if (active) desktopFill(r, 17, gray(1, 0.1f));
            else if (hovered(hit) > 0) desktopFill(r, 17, gray(1, 0.05f));
            textLine(label, r.left + 14, middle(r), r.right - r.left - 28, active ? desktop_strong_font_.Get() : desktop_font_.Get(), active ? palette::text() : gray(0.72f));
            desktopHot(hit, r);
            tx += w + 6;
        }
    }
    const float left = p.left + 24, right = p.right - 24;
    const Rect field{left, h.bottom + 14, right - 112, h.bottom + 52};
    if (field_shown) {
        const bool blink = (GetTickCount64() / 530) % 2 == 0;
        desktopFill(field, 9, gray(1, 0.07f));
        desktopStroke({field.left - 1, field.top - 1, field.right + 1, field.bottom + 1}, 10, uiAccent(0.9f), 1.5f);
        desktopGlyph(glyph::search, field.left + 18, middle(field), 13, palette::muted());
        const auto m = typeMetrics(desktop_font_.Get());
        const float text_left = field.left + 36, text_right = field.right - (query.empty() ? 12 : 36);
        if (query.empty()) {
            if (blink) desktopFill({text_left, middle(field) - 9, text_left + 1.5f, middle(field) + 9}, 0, palette::text());
            textLine(placeholder, text_left + 5, middle(field), text_right - text_left - 5, desktop_font_.Get(), palette::muted(0.85f));
        } else {
            fieldText(query, std::min(caret, query.size()), blink, text_left, middle(field) - m.line * 0.5f, text_right - text_left, m.line,
                      desktop_font_.Get(), palette::text());
            const Rect clear{field.right - 32, field.top + 5, field.right - 6, field.bottom - 5};
            if (hovered({Kind::action, 1}) > 0) desktopFill(clear, 6, gray(1, 0.08f));
            desktopGlyph(glyph::close, (clear.left + clear.right) * 0.5f, middle(clear), 10, palette::muted());
            desktopHot({Kind::action, 1}, clear);
        }
        desktopHot({Kind::field}, {field.left, field.top, field.right - (query.empty() ? 0 : 34), field.bottom});
        const Rect search{right - 100, field.top, right, field.bottom};
        desktopButton(search, 1, hovered({Kind::action, 0}), view.pressed == SheetHit{Kind::action, 0}, !query.empty(), 0, tr(L"Szukaj"));
        desktopHot({Kind::action, 0}, search);
    }
    if (!state.empty()) textLine(state, left + 2, field.bottom + 20, right - left - 4, desktop_small_font_.Get(), palette::muted());
}

// A store plugin page in the store's sheet: the shared frame, then its rows as a native list.
void Renderer::drawDesktopStorePage() {
    const auto& view = *context_->desktop_sheet;
    desktop_hotspots_.clear();
    const auto sheet = artworkSheet(viewport_.width, viewport_.height, sheetDock());
    const auto& page = *context_->plugin_page;
    const bool search = !page.rows.empty() && page.rows.front().input;
    drawDesktopStoreChrome(sheet, view, page.query, page.query.size(), search ? page.rows.front().title : std::wstring{}, page.status, search);
    const auto p = sheet.panel();
    const auto grid = storeGrid(sheet);
    const float left = p.left + 24, right = p.right - 24;
    target_->PushAxisAlignedClip(D2D1::RectF(p.left + 1, grid.top - 10, p.right - 1, p.bottom - 1), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    for (size_t i = 0; i < page.rows.size(); ++i) {
        const auto& row = page.rows[i];
        if (row.input) continue;
        const float y = grid.top + row.top - view.scroll, h = row.height();
        if (y + h < grid.top - 10 || y > p.bottom) continue;
        if (row.heading) {
            textLine(row.title, left + 4, y + h * 0.5f + 6, right - left - 8, desktop_strong_font_.Get(), palette::muted());
            continue;
        }
        const SheetHit hit{Kind::tile, static_cast<int>(i)};
        const Rect r{left, y, right, y + h - 6};
        const bool focused = view.keyboard && page.focus == static_cast<int>(i);
        const bool hover = row.actionable() && view.hover == hit;
        if (hover || focused) desktopFill(r, 10, gray(1, focused ? 0.10f : 0.06f));
        if (focused) desktopStroke(r, 10, uiAccent(), 2.0f);
        float x = left + 14;
        if (!row.game_key.empty() || !row.download_id.empty()) {
            const float size = !row.download_id.empty() ? 88.0f : 62.0f, top = r.top + ((r.bottom - r.top) - size) * 0.5f;
            const Rect art{x, top, x + size, top + size};
            desktopFill(art, 8, rgb(0.12f, 0.15f, 0.18f));
            if (i < covers_.size()) fillImage(covers_[i], box(art), 8, reveal(covers_[i]), false);
            x += size + 16;
        }
        const auto ink = row.enabled ? palette::text() : gray(0.45f);
        if (row.hasSubtitle()) {
            textLine(row.title, x, middle(r) - 11, right - x - 14, desktop_font_.Get(), ink);
            textLine(row.subtitle, x, middle(r) + 12, right - x - 14, desktop_small_font_.Get(), palette::muted());
        } else textLine(row.title, x, middle(r), right - x - 14, desktop_font_.Get(), ink);
        if (row.actionable()) desktopHot(hit, r);
    }
    target_->PopAxisAlignedClip();
    desktop_sheet_height_ = page.total + 12;
}

void Renderer::drawDesktopStore() {
    const auto& view = *context_->desktop_sheet;
    desktop_hotspots_.clear();
    const auto sheet = artworkSheet(viewport_.width, viewport_.height, sheetDock());
    const auto p = sheet.panel();
    const auto& query = context_->search_query;
    const size_t count = cards_ ? cards_->size() : 0;
    // The state line: searching, found, the store's error.
    std::wstring state = context_->store_status;
    if (state.empty() && count && !view.store_loading) state = std::wstring(tr(L"Wyniki")) + L": " + std::to_wstring(count);
    drawDesktopStoreChrome(sheet, view, query, context_->search_caret, tr(L"Szukaj tytułu lub AppID…"), state, true);
    const auto hovered = [&](SheetHit hit) { return view.hover == hit ? 1.0f : 0.0f; };
    const float left = p.left + 24, right = p.right - 24;
    (void)right;
    const auto grid = storeGrid(sheet);
    target_->PushAxisAlignedClip(D2D1::RectF(p.left + 1, grid.top - 10, p.right - 1, p.bottom - 1), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    if (!count) {
        desktop_sheet_height_ = 0;
        if (view.store_loading) { // Placeholders where the results will be.
            const float pulse = 0.035f + 0.025f * (0.5f + 0.5f * std::sin(elapsed_ * 4.0f));
            for (size_t i = 0; i < static_cast<size_t>(grid.columns) * 2; ++i) {
                const auto r = grid.poster(i, 0);
                if (r.top > p.bottom) break;
                desktopFill(r, 10, gray(1, pulse));
                desktopFill({r.left, r.bottom + 10, r.left + (r.right - r.left) * 0.7f, r.bottom + 22}, 4, gray(1, pulse));
            }
        } else {
            const float cx = (p.left + p.right) * 0.5f, cy = grid.top + (p.bottom - grid.top) * 0.34f;
            brush(gray(1, 0.05f));
            target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), 30, 30), solid_.Get());
            desktopGlyph(glyph::search, cx, cy, 22, gray(0.65f));
            const bool none = !query.empty() && context_->store_status.empty();
            textLine(none ? tr(L"Brak wyników. Spróbuj innego tytułu.") : tr(L"Szukaj gier w sklepie"), p.left + 24, cy + 52, p.right - p.left - 48,
                     desktop_title_font_.Get(), palette::text(), DWRITE_TEXT_ALIGNMENT_CENTER);
            if (!none)
                textLine(tr(L"Wpisz tytuł albo Steam AppID — wyniki pojawią się tutaj."), p.left + 24, cy + 78, p.right - p.left - 48, desktop_font_.Get(),
                         palette::muted(), DWRITE_TEXT_ALIGNMENT_CENTER);
        }
        target_->PopAxisAlignedClip();
        return;
    }
    constexpr float tones[4][3] = {{0.13f, 0.21f, 0.24f}, {0.20f, 0.20f, 0.30f}, {0.28f, 0.20f, 0.17f}, {0.15f, 0.24f, 0.20f}};
    for (size_t i = 0; i < count; ++i) {
        const auto cell = grid.poster(i, view.scroll);
        if (cell.bottom + StoreGrid::caption < grid.top - 10 || cell.top > p.bottom) continue;
        const auto& card = (*cards_)[i];
        const SheetHit hit{Kind::tile, static_cast<int>(i)};
        const float hover = hovered(hit);
        const float lift = 3 * hover;
        const Rect r{cell.left, cell.top - lift, cell.right, cell.bottom - lift};
        const auto rect = box(r);
        desktopShadow(r, 10, 0.3f + 0.5f * hover);
        const int tone = static_cast<int>(card.game.app_id % 4);
        desktopFill(r, 10, rgb(tones[tone][0] * 0.7f, tones[tone][1] * 0.7f, tones[tone][2] * 0.7f));
        Cover* art = i < posters_.size() && posters_[i].bitmap ? &posters_[i] : nullptr;
        Cover* cover = i < covers_.size() && covers_[i].bitmap ? &covers_[i] : nullptr;
        const float shown = art ? reveal(*art) : 0.0f;
        if (cover && shown < 1) { // A cover in a poster's frame: whole, over a dimmed fill of itself.
            const float opacity = reveal(*cover);
            fillImage(*cover, rect, 10, opacity);
            desktopFill(r, 10, rgb(0.008f, 0.011f, 0.015f, 0.62f * opacity));
            fillImage(*cover, rect, 10, opacity, false, nullptr, true);
        }
        if (art) fillImage(*art, rect, 10, shown);
        if (!art && !cover)
            desktopCenteredText(card.game.name, (r.left + r.right) * 0.5f, r.top + (r.bottom - r.top) * 0.38f, r.right - r.left - 20,
                                desktop_title_font_.Get(), gray(0.92f), 4);
        desktopStroke(r, 10, gray(1, 0.07f + 0.16f * hover));
        if (i == selected_ && view.keyboard)
            desktopStroke({r.left - 3.5f, r.top - 3.5f, r.right + 3.5f, r.bottom + 3.5f}, 13, uiAccent(), 2.5f);
        textLine(card.game.name, r.left + 2, cell.bottom + 17, r.right - r.left - 4, desktop_font_.Get(), gray(hover > 0 ? 1.0f : 0.88f));
        desktopHot(hit, {r.left, r.top, r.right, cell.bottom + StoreGrid::caption - 4});
    }
    desktop_sheet_height_ = grid.height(count);
    target_->PopAxisAlignedClip();
}