// Desktop mode's dialogs (src/ui/desktop_sheet.hpp): the sheet over the frosted backdrop, below the
// window's top bar, and Ustawienia inside it: categories on the left, the rows as quiet cards with
// their controls on the right (switches, lists, sliders, buttons), the home layout previews, sound
// sources, controllers and companions as tiles, then the open list and the key or colour dialog.
#include "renderer.hpp"
#include "renderer_internal.hpp"
#include "brand_icons.hpp"
#include "../net/companion_characters.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

using Microsoft::WRL::ComPtr;
using namespace desktop_ui;
using settings_ui::Control;

namespace {
using palette::gray;
D2D1_COLOR_F rgb(float r, float g, float b, float a = 1) { return D2D1::ColorF(r, g, b, a); }
D2D1_COLOR_F hex(uint32_t color, float a = 1) {
    return D2D1::ColorF(((color >> 16) & 255) / 255.0f, ((color >> 8) & 255) / 255.0f, (color & 255) / 255.0f, a);
}
using render_detail::box;
using render_detail::upper;
Rect inflate(Rect r, float by) { return {r.left - by, r.top - by, r.right + by, r.bottom + by}; }
float middle(Rect r) { return (r.top + r.bottom) * 0.5f; }
const D2D1_COLOR_F side_bg = rgb(0.056f, 0.061f, 0.071f);
const D2D1_COLOR_F panel_bg = rgb(0.071f, 0.076f, 0.089f);
const D2D1_COLOR_F popup_bg = rgb(0.094f, 0.1f, 0.115f);
bool lightAccent(D2D1_COLOR_F c) { return 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b > 0.6f; }
}

// ---- The frame ---------------------------------------------------------------------------------

void Renderer::captureDesktopBackdrop() {
    if (!std::exchange(desktop_backdrop_armed_, false)) { desktop_backdrop_fresh_ = false; return; } // In use again: it changed.
    const auto pixels = target_->GetPixelSize();
    if (!pixels.width || !pixels.height) return;
    if (!desktop_backdrop_ || desktop_backdrop_->GetPixelSize().width != pixels.width || desktop_backdrop_->GetPixelSize().height != pixels.height) {
        desktop_backdrop_.Reset();
        if (FAILED(target_->CreateBitmap(pixels, D2D1::BitmapProperties(target_->GetPixelFormat(), 96, 96), desktop_backdrop_.GetAddressOf()))) return;
    }
    desktop_backdrop_fresh_ = SUCCEEDED(desktop_backdrop_->CopyFromRenderTarget(nullptr, target_.Get(), nullptr));
}

bool Renderer::drawDesktopBackdrop(float dim) {
    if (!desktop_backdrop_ || !desktop_backdrop_fresh_) return false;
    const auto pixels = target_->GetPixelSize(), size = desktop_backdrop_->GetPixelSize();
    if (size.width != pixels.width || size.height != pixels.height) return false; // Resized since: frosted instead.
    D2D1_MATRIX_3X2_F saved;
    target_->GetTransform(&saved);
    target_->SetTransform(D2D1::Matrix3x2F::Identity());
    const auto dips = target_->GetSize();
    target_->DrawBitmap(desktop_backdrop_.Get(), D2D1::RectF(0, 0, dips.width, dips.height), 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
    target_->SetTransform(saved);
    brush(rgb(0, 0, 0, dim));
    target_->FillRectangle(D2D1::RectF(0, 0, viewport_.width, viewport_.height), solid_.Get());
    return true;
}

void Renderer::drawDesktopWindowBar(const SheetView& view, bool background) {
    const float width = viewport_.width, cy = title_h * 0.5f;
    const float alpha = view.active_window ? 1.0f : 0.55f;
    if (background) {
        brush(rgb(side_bg.r, side_bg.g, side_bg.b, 0.92f));
        target_->FillRectangle(D2D1::RectF(0, 0, width, title_h), solid_.Get());
        brush(gray(1, 0.06f));
        target_->FillRectangle(D2D1::RectF(0, title_h - 1, width, title_h), solid_.Get());
        drawLaunchmeLogo(22, cy, desktop_ui::logo_height, palette::text(alpha));
    }
    Layout layout;
    layout.width = width; layout.height = viewport_.height;
    for (int i = 0; i < 3; ++i) {
        const auto r = layout.windowButton(i);
        const bool hover = view.window_hover == i || (i == 1 && view.max_hover);
        if (!background && !hover) continue; // The library's own bar shows through.
        if (hover) desktopFill(r, 0, i == 2 ? rgb(0.77f, 0.17f, 0.11f) : gray(1, 0.08f));
        const wchar_t g = i == 0 ? glyph::minimize : i == 1 ? (view.maximized ? glyph::restore : glyph::maximize) : glyph::window_close;
        desktopGlyph(g, (r.left + r.right) * 0.5f, cy, 10, gray(hover ? 1.0f : 0.82f, hover ? 1.0f : alpha));
    }
}

void Renderer::drawDesktopSheetFrame(const Sheet& sheet, const SheetView& view, const std::wstring& title, bool back) {
    if (sheet.dock > 0 && context_->desktop_chrome) {
        // Docked (Ustawienia): the library's sidebar and top bar stay, live; the page takes the rest.
        const auto& chrome = *context_->desktop_chrome;
        const auto layout = desktopLayoutOf(chrome);
        target_->Clear(panel_bg);
        drawDesktopSidebar(layout, chrome);
        drawDesktopTitlebar(layout, chrome);
        beginScreenForeground();
        const auto p = sheet.panel();
        const auto h = sheet.header();
        brush(gray(1, 0.06f));
        target_->FillRectangle(D2D1::RectF(p.left, h.bottom - 1, p.right, h.bottom), solid_.Get());
        float title_left = p.left + 24;
        if (back) {
            const auto b = sheet.back();
            const bool hover = view.hover.kind == SheetHit::Kind::back;
            if (hover) desktopFill(b, 8, gray(1, 0.08f));
            desktopGlyph(0xE72B, (b.left + b.right) * 0.5f, middle(b), 14, gray(hover ? 0.98f : 0.8f));
            title_left = b.right + 10;
        }
        textLine(title, title_left, middle(h), p.right - title_left - 400, desktop_title_font_.Get(), palette::text());
        const auto c = sheet.close();
        const bool close_hover = view.hover.kind == SheetHit::Kind::close;
        if (close_hover) desktopFill(c, 8, gray(1, 0.08f));
        desktopGlyph(glyph::close, (c.left + c.right) * 0.5f, middle(c), 12, gray(close_hover ? 0.98f : 0.75f));
        return;
    }
    // The library just left, barely dimmed, as behind a desktop app's dialog; before there is one (or
    // after a resize) the last game's picture, frosted. The window's bar either way.
    target_->Clear(rgb(0.043f, 0.047f, 0.055f));
    if (drawDesktopBackdrop(0.36f)) drawDesktopWindowBar(view, false);
    else {
        drawBackground();
        const bool style = std::exchange(desktop_style_, false); // The picture stays frosted behind the sheet.
        drawFrostedBackdrop(1);
        desktop_style_ = style;
        brush(rgb(0.02f, 0.022f, 0.027f, 0.3f));
        target_->FillRectangle(D2D1::RectF(0, 0, viewport_.width, viewport_.height), solid_.Get());
        drawDesktopWindowBar(view);
    }
    beginScreenForeground(); // The backdrop and bar stay; the sheet enters with the screen transition.
    const auto p = sheet.panel();
    desktopShadow(p, 14, 1.5f);
    desktopFill(p, 14, panel_bg);
    desktopStroke(p, 14, gray(1, 0.09f));
    const auto h = sheet.header();
    brush(gray(1, 0.06f));
    target_->FillRectangle(D2D1::RectF(p.left + 1, h.bottom - 1, p.right - 1, h.bottom), solid_.Get());
    float title_left = p.left + 24;
    if (back) {
        const auto b = sheet.back();
        const bool hover = view.hover.kind == SheetHit::Kind::back;
        if (hover) desktopFill(b, 8, gray(1, 0.08f));
        desktopGlyph(0xE72B, (b.left + b.right) * 0.5f, middle(b), 14, gray(hover ? 0.98f : 0.8f));
        title_left = b.right + 10;
    }
    textLine(title, title_left, middle(h), p.right - title_left - 400, desktop_title_font_.Get(), palette::text());
    const auto c = sheet.close();
    const bool close_hover = view.hover.kind == SheetHit::Kind::close;
    if (close_hover) desktopFill(c, 8, gray(1, 0.08f));
    desktopGlyph(glyph::close, (c.left + c.right) * 0.5f, middle(c), 12, gray(close_hover ? 0.98f : 0.75f));
}

// ---- Controls ----------------------------------------------------------------------------------

void Renderer::desktopSwitch(Rect r, float on, bool locked, float hover) {
    on = std::clamp(on, 0.0f, 1.0f);
    const float radius = (r.bottom - r.top) * 0.5f;
    const auto off_bg = gray(1, locked ? 0.07f : 0.14f + 0.05f * hover);
    const auto on_bg = uiAccent(locked ? 0.45f : 1.0f);
    desktopFill(r, radius, D2D1::ColorF(std::lerp(off_bg.r, on_bg.r, on), std::lerp(off_bg.g, on_bg.g, on),
                                        std::lerp(off_bg.b, on_bg.b, on), std::lerp(off_bg.a, on_bg.a, on)));
    if (on < 0.99f) desktopStroke(r, radius, gray(1, 0.1f * (1.0f - on)));
    const float knob = radius - 3, cx = std::lerp(r.left + radius, r.right - radius, on);
    const float off_ink = locked ? 0.45f : 0.86f;
    const float on_ink = lightAccent(uiAccent()) ? 0.08f : 0.98f;
    brush(gray(std::lerp(off_ink, on_ink, on)));
    target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, middle(r)), knob, knob), solid_.Get());
}

void Renderer::desktopField(Rect r, const std::wstring& value, wchar_t glyph, float hover, bool open, uint32_t swatch, bool has_swatch) {
    desktopFill(r, 8, gray(1, 0.045f + 0.03f * hover));
    if (open) desktopStroke(inflate(r, 1), 9, uiAccent(0.9f), 1.5f);
    else desktopStroke(r, 8, gray(1, 0.1f + 0.06f * hover));
    float left = r.left + 12;
    if (has_swatch) {
        brush(swatch ? hex(swatch) : gray(0.5f));
        target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(left + 7, middle(r)), 7, 7), solid_.Get());
        brush(gray(1, 0.25f));
        target_->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(left + 7, middle(r)), 7, 7), solid_.Get(), 1);
        left += 22;
    }
    textLine(value, left, middle(r), r.right - left - 30, desktop_font_.Get(), gray(0.93f));
    if (glyph) desktopGlyph(glyph, r.right - 16, middle(r) + (glyph == glyph::chevron_down ? 1.0f : 0.0f), glyph == glyph::chevron_down ? 9.0f : 12.0f, gray(0.65f));
}

// ---- Ustawienia --------------------------------------------------------------------------------

void Renderer::drawDesktopSettings() {
    using L_t = settings_sheet::Layout;
    using Kind = SheetHit::Kind;
    const auto& view = *context_->desktop_sheet;
    static const std::vector<settings_ui::Row> no_rows;
    const auto& rows = context_->settings_rows ? *context_->settings_rows : no_rows;
    const auto& state = context_->settings_state;
    const auto L = L_t::of(viewport_.width, viewport_.height, state, rows, view.dock);
    const int category = std::clamp(state.category, 0, static_cast<int>(settings_ui::categories.size()) - 1);
    const bool subpage = state.subpage();
    const std::wstring page_title = context_->settings_title.empty() ? std::wstring(tr(settings_ui::categories[category].title)) : context_->settings_title;
    drawDesktopSheetFrame(L, view, subpage ? page_title : std::wstring(tr(L"Ustawienia")), subpage);
    const auto p = L.panel();
    if (context_->settings_save_failed) {
        const auto r = L.saveStatus();
        const bool hover = view.hover.kind == Kind::save_status;
        textLine(tr(L"Nie zapisano zmian · kliknij, aby ponowić"), r.left, middle(r), r.right - r.left, desktop_small_font_.Get(),
                 hover ? palette::dangerStrong() : palette::danger(), DWRITE_TEXT_ALIGNMENT_TRAILING);
    }
    // Categories: the active pill and its left bar glide on a spring.
    brush(gray(1, 0.06f));
    target_->FillRectangle(D2D1::RectF(L.navRight(), p.top + sheet_header, L.navRight() + 1, p.bottom - 1), solid_.Get());
    {
        const float nav = view.nav_glide.value_or(static_cast<float>(category));
        const float ay = p.top + sheet_header + 8 + nav * (settings_sheet::nav_row + 2);
        const Rect active_rect{p.left + 12, ay, L.navRight() - 12, ay + settings_sheet::nav_row};
        desktopFill(active_rect, 8, gray(1, 0.085f));
        desktopFill({active_rect.left, active_rect.top + 9, active_rect.left + 3, active_rect.bottom - 9}, 1.5f, uiAccent(0.95f));
    }
    for (int i = 0; i < static_cast<int>(settings_ui::categories.size()); ++i) {
        const auto r = L.navRow(i);
        const bool active = category == i, hover = view.hover == SheetHit{Kind::nav, i};
        if (!active && hover) desktopFill(r, 8, gray(1, 0.045f));
        const float cy = middle(r);
        if (i == static_cast<int>(settings_ui::Category::emulation)) { // Tryb TV's cartridge, at the glyphs' size.
            D2D1_MATRIX_3X2_F base{};
            target_->GetTransform(&base);
            target_->SetTransform(D2D1::Matrix3x2F::Scale(0.68f, 0.68f, D2D1::Point2F(r.left + 20, cy)) * base);
            drawSettingsIcon(i, r.left + 20, cy, active ? palette::text() : gray(0.62f));
            target_->SetTransform(base);
        } else desktopGlyph(settings_sheet::categoryGlyph(i), r.left + 20, cy, 15, active ? palette::text() : gray(0.62f));
        textLine(tr(settings_ui::categories[i].title), r.left + 42, cy, r.right - r.left - 50,
                 active ? desktop_strong_font_.Get() : desktop_font_.Get(), active ? palette::text() : gray(0.8f));
        if (view.keyboard && !state.options && active) desktopFocusRing(r, 8);
    }
    // The page's heading.
    const float left = L.contentLeft(), right = L.contentRight();
    textLine(page_title, left, L.headingY(), right - left, desktop_heading_font_.Get(), palette::text());
    if (!subpage && *settings_ui::categories[category].description)
        textLine(tr(settings_ui::categories[category].description), left, L.headingY() + 26, right - left, desktop_small_font_.Get(), palette::muted());
    // Rows.
    const float s = view.scroll, top = L.listTop(), bottom = L.listBottom();
    // At rest the first section's label sits above the list; scrolled, nothing passes under the heading.
    target_->PushAxisAlignedClip(D2D1::RectF(left - 4, s > 2 ? top - 4 : top - 30, right + 4, bottom), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    const bool picking = state.editing && !rows.empty() && state.row() < static_cast<int>(rows.size()) && rows[static_cast<size_t>(state.row())].picker();
    for (int i = 0; i < static_cast<int>(rows.size()) && i < L.count(); ++i) {
        const auto& item = rows[static_cast<size_t>(i)];
        auto r = L.row(i, s);
        if (r.bottom < top - 40 || r.top > bottom) continue;
        if (!L.labels[static_cast<size_t>(i)].empty())
            textLine(upper(L.labels[static_cast<size_t>(i)]), r.left + 2, r.top - 15, right - left, desktop_label_font_.Get(), gray(0.52f));
        const bool selected = state.options && state.row() == i;
        const bool hover_row = view.hover.index == i && view.hover.kind != Kind::nav && view.hover.kind != Kind::none;
        const auto hovered = [&](Kind kind, int sub = -1) { return view.hover == SheetHit{kind, i, sub} ? 1.0f : 0.0f; };
        const bool tall = settings_sheet::tall(item.control);
        // An open accordion section's rows sit inset under its heading, joined by a thin line.
        if (item.indent > 0) {
            desktopFill({r.left + 9, r.top - settings_sheet::row_gap, r.left + 11, r.bottom}, 1, gray(1, 0.07f));
            r.left += 24.0f * static_cast<float>(item.indent);
        }
        const bool fold = item.control == Control::fold;
        desktopFill(r, 10, gray(1, fold ? (hover_row ? 0.085f : 0.06f) : hover_row && !tall ? 0.05f : 0.03f));
        desktopStroke(r, 10, gray(1, fold ? 0.07f : 0.045f));
        if (selected && view.keyboard && !picking && item.control != Control::avatar) desktopFocusRing(r, 10);
        if (selected && state.editing && item.control == Control::slider) desktopStroke(r, 10, uiAccent(0.8f), 1.5f);
        if (item.lifted) { // Being moved: a soft glow, then the accent frame.
            for (int ring = 3; ring >= 1; --ring) {
                const float spread = 2.5f * static_cast<float>(ring);
                desktopStroke({r.left - spread, r.top - spread, r.right + spread, r.bottom + spread}, 10 + spread, uiAccent(0.09f * (4 - ring) / 3.0f), 4.0f);
            }
            desktopFill(r, 10, uiAccent(0.10f)); desktopStroke(r, 10, uiAccent(), 2.5f);
        } else if (selected && context_->settings_hold > 0) // Enter held: the frame charges up to the lift.
            desktopStroke(r, 10, uiAccent(context_->settings_hold), 1.0f + 1.5f * context_->settings_hold);
        float text_left = r.left + 16;
        const float cy = L.controlY(i, s);
        // Platforms: the store's mark in a fixed-width column (as Tryb TV), so every name starts at one line.
        if (item.control == Control::platform && item.id == settings_ui::Id::pc_platform) {
            if (!desktopBrand(brand_icons::sourceKey(item.source), text_left + 14, cy, 24, gray(item.locked ? 0.5f : 0.9f)))
                desktopGlyph(glyph::game, text_left + 14, cy, 16, gray(0.6f));
            text_left += 40;
        } else if (settings_ui::optionsTile(item)) {
            desktopBrand("retroarch", text_left + 14, cy, 24, gray(0.9f));
            text_left += 40;
        }
        // The text ends before the row's controls (and its badge).
        const float controls_left = L.controlsLeft(item, i, s);
        const float text_width = (tall ? right - 16 : controls_left - 20 - (item.badge.empty() ? 0.0f : L_t::badge_width + 14)) - text_left;
        const auto title_ink = item.locked || item.faded ? gray(0.55f) : gray(0.94f);
        if (!item.badge.empty()) {
            const auto b = L.badge(item, i, s);
            const bool resettable = !item.reset.empty();
            const float h = resettable ? hovered(Kind::badge) : 0.0f;
            desktopFill(b, 12, uiAccent(0.14f + 0.14f * h));
            if (h > 0) desktopStroke(b, 12, uiAccent(0.7f));
            float label_left = b.left + 10;
            if (resettable) { desktopGlyph(glyph::refresh, b.left + 16, cy, 10, uiAccent(0.95f)); label_left = b.left + 26; }
            textLine(item.badge, label_left, cy, b.right - 8 - label_left, desktop_small_font_.Get(), uiAccent(0.98f),
                     resettable ? DWRITE_TEXT_ALIGNMENT_LEADING : DWRITE_TEXT_ALIGNMENT_CENTER);
        }
        if (item.dense && !selected) {
            // A dense row without the focus: the title, then what it sets in one dim line.
            const float title_y = item.detail.empty() ? cy : r.top + 15;
            textLine(item.title, text_left, title_y, text_width, desktop_strong_font_.Get(), title_ink);
            if (!item.detail.empty()) textLine(item.detail, text_left, r.top + 32, text_width, desktop_small_font_.Get(), gray(0.5f));
        } else if (tall || !item.description.empty()) {
            // Thin rows (DRM-free games) hold the title and one line of description, centred.
            const bool thin = !tall && state.at(settings_ui::Page::drm_games);
            const float title_y = tall ? r.top + 30 : thin ? r.top + 17 : r.top + 24;
            textLine(item.title, text_left, title_y, text_width, desktop_strong_font_.Get(), title_ink);
            if (!item.description.empty()) {
                const auto m = typeMetrics(desktop_small_font_.Get());
                paragraph(item.description, text_left, title_y + 10 - (m.baseline - m.cap) + 2, text_width, desktop_small_font_.Get(), palette::muted(),
                          tall || thin ? 1 : std::max(2, item.lines));
            }
        } else textLine(item.title, text_left, cy, text_width, desktop_strong_font_.Get(), title_ink);
        const float toggle_on = i < static_cast<int>(context_->settings_toggles.size()) ?
                                context_->settings_toggles[static_cast<size_t>(i)] : (item.checked ? 1.0f : 0.0f);
        switch (item.control) {
        case Control::toggle: {
            const float toggle_right = L.toggleRight(item);
            desktopSwitch({toggle_right - 40, cy - 11, toggle_right, cy + 11}, toggle_on, item.locked, hover_row ? 1.0f : 0.0f);
            break;
        }
        case Control::platform: {
            const float toggle_right = L.toggleRight(item);
            desktopSwitch({toggle_right - 40, cy - 11, toggle_right, cy + 11}, toggle_on, false, hovered(Kind::toggle));
            if (settings_ui::hasOptionsButton(item.id)) {
                const auto c = L.cog(i, s);
                if (hovered(Kind::cog) > 0) desktopFill(c, 8, gray(1, 0.08f));
                desktopStroke(c, 8, gray(1, 0.1f));
                desktopGlyph(glyph::settings, (c.left + c.right) * 0.5f, middle(c), 14, gray(0.85f));
                if (view.keyboard && selected && state.editing) desktopFocusRing(c, 8);
            } else {
                const auto a = L.action(i, s);
                desktopButton(a, 1, hovered(Kind::action), view.pressed == SheetHit{Kind::action, i}, true, 0, item.value);
                if (view.keyboard && selected && state.editing) desktopFocusRing(a, 8);
            }
            break;
        }
        case Control::choice: case Control::secret: case Control::text: {
            const auto f = L.field(i, s);
            // Colour rows show their colour ("Z okładki" has none of its own).
            const bool color_row = (item.id == settings_ui::Id::accent && item.choice != 0) || item.id == settings_ui::Id::ui_accent ||
                                   item.id == settings_ui::Id::button_color;
            uint32_t swatch = color_row && item.choice >= 0 && item.choice < static_cast<int>(item.choices.size()) ?
                              item.choices[static_cast<size_t>(item.choice)].color : 0;
            if (item.id == settings_ui::Id::button_color && !swatch) swatch = ui_accent_; // As the interface accent.
            desktopField(f, item.value, item.control == Control::choice ? glyph::chevron_down : glyph::edit,
                         std::max(hovered(Kind::field), hover_row ? 0.5f : 0.0f), selected && picking, swatch ? swatch : 0x010101u, color_row);
            break;
        }
        case Control::action: {
            if (item.locked && item.value.empty()) break; // Information only (a status line): no button.
            const auto a = L.action(i, s);
            desktopButton(a, 1, std::max(hovered(Kind::action), hover_row ? 0.4f : 0.0f), view.pressed.index == i, !item.locked, 0,
                          item.value.empty() ? std::wstring(tr(L"Otwórz")) : item.value);
            break;
        }
        case Control::count: {
            const auto a = L.action(i, s);
            const float chevron_x = a.right - 8;
            desktopGlyph(glyph::chevron_right, chevron_x, cy, 10, hover_row ? palette::text() : gray(0.6f));
            textLine(item.value, a.left, cy, a.right - a.left - 24, desktop_title_font_.Get(), palette::text(), DWRITE_TEXT_ALIGNMENT_TRAILING);
            break;
        }
        case Control::page: {
            const auto a = L.action(i, s);
            if (settings_ui::optionsTile(item)) {
                // Emulacja among the platforms: its summary, then the platforms' cog.
                const auto c = L.cog(i, s);
                if (hover_row) desktopFill(c, 8, gray(1, 0.08f));
                desktopStroke(c, 8, gray(1, 0.1f));
                desktopGlyph(glyph::settings, (c.left + c.right) * 0.5f, middle(c), 14, gray(0.85f));
                textLine(item.value, a.left - 120, cy, c.left - 12 - (a.left - 120), desktop_font_.Get(), gray(0.78f), DWRITE_TEXT_ALIGNMENT_TRAILING);
                break;
            }
            // A page of its own: its summary, then a chevron (a row being moved: the reorder mark).
            if (item.lifted) desktopGlyph(glyph::sort, a.right - 8, cy, 14, uiAccent());
            else desktopGlyph(glyph::chevron_right, a.right - 8, cy, 10, hover_row ? palette::text() : gray(0.6f));
            textLine(item.value, a.left - 60, cy, a.right - a.left + 60 - 24, desktop_font_.Get(), item.lifted ? uiAccent() : gray(0.78f), DWRITE_TEXT_ALIGNMENT_TRAILING);
            break;
        }
        case Control::fold: {
            // An accordion heading: its summary, then a chevron pointing down while it is open.
            const auto a = L.action(i, s);
            desktopGlyph(glyph::chevron_down, a.right - 8, cy + 1, 9, hover_row ? palette::text() : gray(0.7f), item.checked ? 0.0f : -90.0f);
            textLine(item.value, a.left - 60, cy, a.right - a.left + 60 - 24, desktop_font_.Get(), item.value.empty() ? gray(0.6f) : uiAccent(0.95f),
                     DWRITE_TEXT_ALIGNMENT_TRAILING);
            break;
        }
        case Control::slider: {
            const auto t = L.track(i, s);
            const float level = std::clamp(item.level, 0.0f, 1.0f), x = std::lerp(t.left, t.right, level);
            desktopFill({t.left, cy - 2, t.right, cy + 2}, 2, gray(1, 0.14f));
            if (x > t.left) desktopFill({t.left, cy - 2, x, cy + 2}, 2, uiAccent());
            const int steps = settings_ui::sizeSteps(item.id);
            if (steps > 1) for (int step = 0; step < steps; ++step) {
                const float tick = std::lerp(t.left, t.right, static_cast<float>(step) / static_cast<float>(steps - 1));
                desktopFill({tick - 1, cy - 5, tick + 1, cy + 5}, 1, gray(0.7f));
            }
            const float knob = hovered(Kind::track) > 0 || (selected && state.editing) ? 8.0f : 7.0f;
            brush(gray(0.97f));
            target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(x, cy), knob, knob), solid_.Get());
            textLine(item.value, t.right + 12, cy, right - 16 - t.right - 12, desktop_font_.Get(), gray(0.85f), DWRITE_TEXT_ALIGNMENT_TRAILING);
            break;
        }
        case Control::layout: {
            // A slider: the chosen layout in view, the tile cut at an edge says there are more.
            const float first = settings_ui::layoutFirst(item.choice, L.layout_visible);
            const auto band = L.layoutBand(i, s);
            target_->PushAxisAlignedClip(box(band), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
            for (int option = 0; option < settings_ui::home_layout_count; ++option) {
                const auto t = L.layoutTile(i, s, option, first);
                if (t.right <= band.left || t.left >= band.right) continue;
                const bool chosen = item.choice == option;
                const float h = hovered(Kind::layout, option);
                const float time = context_->settings_preview_option == option ? context_->settings_preview_seconds : 0.0f;
                drawLayoutScene(option, box(t), time, chosen || h > 0 ? 1.0f : 0.0f);
                desktopStroke(t, 8, chosen ? uiAccent() : gray(1, 0.1f + 0.1f * h), chosen ? 2.0f : 1.0f);
                if (view.keyboard && selected && chosen) desktopFocusRing(t, 8);
                textLine(tr(settings_ui::home_layouts[static_cast<size_t>(option)]), t.left, t.bottom + 16, t.right - t.left,
                         chosen ? desktop_strong_font_.Get() : desktop_font_.Get(), chosen ? palette::text() : gray(0.72f), DWRITE_TEXT_ALIGNMENT_CENTER);
            }
            target_->PopAxisAlignedClip();
            break;
        }
        case Control::avatar: {
            const int count = companion::lookCount(), first = L_t::avatarFirst(item.choice, count);
            const auto* her = context_->companion;
            for (int option = first; option < std::min(count, first + L_t::avatar_visible); ++option) {
                const auto t = L.avatarCard(i, s, option, count, first);
                const auto look = static_cast<companion::Look>(option);
                const auto* character = companion::character(look);
                if (!character) continue;
                const bool chosen = item.choice == option, soon = character->soon;
                const float h = hovered(Kind::avatar, option);
                ComPtr<ID2D1RoundedRectangleGeometry> clip;
                factory_->CreateRoundedRectangleGeometry(D2D1::RoundedRect(box(t), 10, 10), clip.GetAddressOf());
                desktopFill(t, 10, gray(0, 0.14f));
                drawCompanionCardGlow(box(t), look, soon ? 0.4f : 1.0f);
                if (clip && her && static_cast<size_t>(option) < her->previews.size()) {
                    target_->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), clip.Get()), nullptr);
                    const int costume = static_cast<size_t>(option) < her->preview_costumes.size() ? her->preview_costumes[static_cast<size_t>(option)] : 0;
                    const float figure = (t.bottom - t.top) * 1.05f;
                    drawCompanionFigure(look, her->previews[static_cast<size_t>(option)], (t.left + t.right) * 0.5f, t.bottom + figure * 0.18f, figure,
                                        soon ? 0.35f : 1.0f, costume);
                    target_->PopLayer();
                }
                // Her name on a soft shade at the foot of the card.
                desktopFill({t.left, t.bottom - 48, t.right, t.bottom}, 10, gray(0, 0.55f));
                textLine(character->name, t.left + 12, t.bottom - 31, t.right - t.left - 24, desktop_strong_font_.Get(), palette::text(soon ? 0.6f : 1.0f));
                textLine(soon ? std::wstring(tr(L"Wkrótce")) : character->card(i18n::english()), t.left + 12, t.bottom - 14,
                         t.right - t.left - 24, desktop_small_font_.Get(), palette::muted());
                if (!chosen) desktopStroke(t, 10, gray(1, 0.08f + 0.12f * h));
            }
            if (count) drawCompanionCardFocus(box(L.avatarCard(i, s, item.choice, count, first)), 1);
            break;
        }
        case Control::sources: case Control::pads: {
            const int count = static_cast<int>(item.tiles.size());
            const int first = L.firstTile(item.control, count, state.tile), end = std::min(count, first + L.visibleTiles(item.control, count));
            for (int t_index = first; t_index < end; ++t_index) {
                const auto& tile = item.tiles[static_cast<size_t>(t_index)];
                const auto t = L.tile(i, s, item.control, t_index, count, state.tile);
                const float h = std::max(hovered(Kind::tile, t_index), hovered(Kind::tile_action, t_index));
                desktopFill(t, 10, gray(1, 0.04f + 0.03f * h));
                desktopStroke(t, 10, gray(1, 0.08f + 0.08f * h));
                if (view.keyboard && selected && state.tile == t_index) desktopFocusRing(t, 10);
                if (item.control == Control::sources) {
                    textLine(upper(tile.caption), t.left + 14, t.top + 18, t.right - t.left - 28, desktop_label_font_.Get(), gray(0.52f));
                    desktopGlyph(tile.input ? 0xE720 : 0xE767, t.left + 22, t.top + 46, 15, tile.muted ? uiAccent() : gray(0.85f));
                    textLine(tile.title, t.left + 42, t.top + 46, t.right - t.left - 80, desktop_strong_font_.Get(), palette::text());
                    desktopGlyph(glyph::chevron_down, t.right - 20, t.top + 47, 9, gray(0.65f));
                    const float bar_left = t.left + 14, bar_right = t.right - 94, y = t.bottom - 22;
                    desktopFill({bar_left, y - 2, bar_right, y + 2}, 2, gray(1, tile.level >= 0 ? 0.14f : 0.06f));
                    if (tile.level >= 0) desktopFill({bar_left, y - 2, std::lerp(bar_left, bar_right, std::clamp(tile.level, 0.0f, 1.0f)), y + 2}, 2,
                                                     tile.muted ? gray(0.45f) : uiAccent());
                    const std::wstring level = tile.level < 0 ? L"—" : tile.muted ? std::wstring(tr(L"Wyciszone")) :
                                               std::to_wstring(static_cast<int>(std::lround(tile.level * 100))) + L"%";
                    textLine(level, bar_right + 16, y, t.right - bar_right - 30, desktop_small_font_.Get(), gray(0.75f), DWRITE_TEXT_ALIGNMENT_TRAILING);
                    if (picking && selected && state.tile == t_index) desktopStroke(inflate(t, 1), 11, uiAccent(0.9f), 1.5f);
                } else {
                    const Rect picture{t.left + 24, t.top + 10, t.right - 24, t.bottom - 92};
                    if (tile.pair) desktopGlyph(glyph::add, (picture.left + picture.right) * 0.5f, middle(picture), 28, gray(0.7f));
                    else drawControllerPicture(tile.picture, box(picture));
                    textLine(tile.title, t.left + 12, t.bottom - 78, t.right - t.left - 24, desktop_strong_font_.Get(), palette::text(), DWRITE_TEXT_ALIGNMENT_CENTER);
                    std::wstring status = tile.value;
                    if (tile.battery) status += (status.empty() ? L"" : L" · ") + std::to_wstring(*tile.battery) + L"%" + (tile.charging ? L" ⚡" : L"");
                    textLine(status, t.left + 12, t.bottom - 58, t.right - t.left - 24, desktop_small_font_.Get(), palette::muted(), DWRITE_TEXT_ALIGNMENT_CENTER);
                    if (!tile.action.empty())
                        desktopButton(L_t::tileAction(t), 1, hovered(Kind::tile_action, t_index), view.pressed == SheetHit{Kind::tile_action, i, t_index}, true, 0, tile.action);
                }
            }
            if (first > 0 || end < count) {
                const auto r_row = L.row(i, s);
                textLine(std::to_wstring(state.tile + 1) + L" / " + std::to_wstring(count), r_row.right - 90, r_row.top + 30, 74,
                         desktop_small_font_.Get(), palette::muted(), DWRITE_TEXT_ALIGNMENT_TRAILING);
            }
            break;
        }
        }
    }
    // System closes with the logo, below its last row.
    if (L.footer_top >= 0) {
        const float cy = L.footerY(s);
        if (cy > top - L_t::footer_logo && cy < bottom + L_t::footer_logo)
            drawLaunchmeLogo((left + right) * 0.5f, cy, L_t::footer_logo, palette::text(0.5f), true);
    }
    if (rows.empty()) {
        const Rect card{left, top, right, top + 74};
        desktopFill(card, 10, gray(1, 0.03f));
        textLine(tr(L"Brak nazwanych kolekcji"), left + 16, top + 24, right - left - 32, desktop_strong_font_.Get(), gray(0.85f));
        textLine(tr(L"Utwórz kolekcje w Steam. Pojawią się tutaj po odświeżeniu biblioteki."), left + 16, top + 46, right - left - 32,
                 desktop_small_font_.Get(), palette::muted());
    }
    target_->PopAxisAlignedClip();
    // Scrolled: a soft edge under the heading, and the bar.
    if (s > 2) {
        ComPtr<ID2D1GradientStopCollection> stops;
        ComPtr<ID2D1LinearGradientBrush> shade;
        const D2D1_GRADIENT_STOP shade_stops[] = {{0, panel_bg}, {1, rgb(panel_bg.r, panel_bg.g, panel_bg.b, 0)}};
        if (SUCCEEDED(target_->CreateGradientStopCollection(shade_stops, 2, stops.GetAddressOf())) &&
            SUCCEEDED(target_->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(D2D1::Point2F(0, top - 4), D2D1::Point2F(0, top + 14)),
                                                        stops.Get(), shade.GetAddressOf())))
            target_->FillRectangle(D2D1::RectF(left - 4, top - 4, right + 4, top + 14), shade.Get());
    }
    if (L.maxScroll() > 0) {
        const float view_h = bottom - top, thumb = std::max(30.0f, view_h * view_h / (L.content_height));
        const float y = top + (view_h - thumb) * std::clamp(s / L.maxScroll(), 0.0f, 1.0f);
        desktopFill({p.right - 9, y, p.right - 5, y + thumb}, 2, gray(1, 0.22f));
    }
    // The open list of a choice or sound source.
    if (picking) {
        const auto& item = rows[static_cast<size_t>(state.row())];
        const auto& list = item.pickList(state.tile);
        const int count = static_cast<int>(list.size()), picked = item.picked(state.tile);
        Rect anchor = L.field(state.row(), s);
        if (item.control == Control::sources && !item.tiles.empty())
            anchor = L.tile(state.row(), s, item.control, std::clamp(state.tile, 0, static_cast<int>(item.tiles.size()) - 1),
                            static_cast<int>(item.tiles.size()), state.tile);
        const auto d = L.dropdown(anchor, count);
        desktopShadow(d, 10, 1);
        desktopFill(d, 10, popup_bg);
        desktopStroke(d, 10, gray(1, 0.1f));
        const int first = L_t::firstChoice(count, state.choice);
        for (int slot = 0; slot < std::min(count - first, settings_sheet::dropdown_visible); ++slot) {
            const int index = first + slot;
            const auto& choice = list[static_cast<size_t>(index)];
            const auto r = L.dropdownItem(d, slot);
            if (index == state.choice) desktopFill(r, 6, gray(1, 0.08f));
            float x = r.left + 32;
            if (index == picked) desktopGlyph(glyph::check, r.left + 15, middle(r), 11, uiAccent());
            if (choice.color || choice.custom) {
                brush(choice.custom ? gray(0.4f) : hex(choice.color));
                target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(x + 6, middle(r)), 6, 6), solid_.Get());
                x += 20;
            }
            textLine(choice.label, x, middle(r), r.right - x - 10, desktop_font_.Get(), index == state.choice ? palette::text() : gray(0.85f));
        }
        if (count > settings_sheet::dropdown_visible) {
            const float track = d.bottom - d.top - 12, length = std::max(24.0f, track * settings_sheet::dropdown_visible / count);
            const float y = d.top + 6 + (track - length) * static_cast<float>(first) / static_cast<float>(count - settings_sheet::dropdown_visible);
            desktopFill({d.right - 6, y, d.right - 3, y + length}, 1.5f, gray(1, 0.25f));
        }
    }
    // The key / text dialog.
    if (state.editing && !rows.empty() && state.row() < static_cast<int>(rows.size()) &&
        (rows[static_cast<size_t>(state.row())].control == Control::secret || rows[static_cast<size_t>(state.row())].control == Control::text)) {
        const auto& item = rows[static_cast<size_t>(state.row())];
        desktopFill(p, 14, gray(0, 0.45f));
        const auto d = L.secretDialog();
        desktopShadow(d, 12, 1.2f);
        desktopFill(d, 12, popup_bg);
        desktopStroke(d, 12, gray(1, 0.1f));
        textLine(item.title, d.left + 24, d.top + 34, d.right - d.left - 48, desktop_title_font_.Get(), palette::text());
        textLine(item.control == Control::secret ? tr(L"Wklej klucz (Ctrl+V) albo wpisz go. Enter zapisuje.") : tr(L"Enter zapisuje, Esc anuluje."),
                 d.left + 24, d.top + 62, d.right - d.left - 48, desktop_small_font_.Get(), palette::muted());
        const auto f = L.secretField();
        desktopFill(f, 8, gray(0, 0.25f));
        desktopStroke(inflate(f, 1), 9, uiAccent(0.9f), 1.5f);
        const std::wstring shown = item.control == Control::text ? context_->settings_text :
                                   std::wstring(std::min<size_t>(context_->secret_length, 64), L'•');
        const auto m = typeMetrics(desktop_font_.Get());
        const float text_top = middle(f) - m.line * 0.5f;
        if (context_->secret_selected && !shown.empty()) {
            const float w = std::min(textAdvance(shown, desktop_font_.Get()), f.right - f.left - 24);
            desktopFill({f.left + 11, middle(f) - 11, f.left + 13 + w, middle(f) + 11}, 3, uiAccent(0.35f));
        }
        fieldText(shown, shown.size(), (GetTickCount64() / 530) % 2 == 0, f.left + 12, text_top, f.right - f.left - 24, m.line, desktop_font_.Get(), palette::text());
        if (L.import_help) paragraph(settings_ui::companionImportHelp(), d.left + 24, f.bottom + 20, d.right - d.left - 48,
                                     desktop_small_font_.Get(), palette::muted(), 12);
        const bool cancel_focus = context_->secret_cancel;
        if (L.import_help)
            desktopButton(L.secretBrowse(), 1, view.hover.kind == Kind::dialog_browse ? 1.0f : 0.0f, view.pressed.kind == Kind::dialog_browse, true, 0, tr(L"Przeglądaj…"));
        desktopButton(L.secretCancel(), 1, view.hover.kind == Kind::dialog_cancel ? 1.0f : 0.0f, view.pressed.kind == Kind::dialog_cancel, true, 0, tr(L"Anuluj"));
        desktopButton(L.secretSave(), 2, view.hover.kind == Kind::dialog_save ? 1.0f : 0.0f, view.pressed.kind == Kind::dialog_save, true, 0, tr(L"Zapisz"));
        if (view.keyboard) desktopFocusRing(L.import_help && context_->secret_browse ? L.secretBrowse() : cancel_focus ? L.secretCancel() : L.secretSave(), 8);
    }
    // "Inny…": hue, saturation and brightness.
    if (const auto* editor = context_->color_editor) {
        desktopFill(p, 14, gray(0, 0.45f));
        const auto d = L.colorDialog();
        desktopShadow(d, 12, 1.2f);
        desktopFill(d, 12, popup_bg);
        desktopStroke(d, 12, gray(1, 0.1f));
        textLine(editor->title, d.left + 24, d.top + 34, d.right - d.left - 48, desktop_title_font_.Get(), palette::text());
        const uint32_t current = editor->rgb();
        const auto sw = L.colorSwatch();
        desktopFill(sw, 12, hex(current));
        desktopStroke(sw, 12, gray(1, 0.18f));
        wchar_t code[16]{};
        swprintf_s(code, L"#%06X", current & 0xFFFFFF);
        textLine(code, sw.left, sw.bottom + 18, sw.right - sw.left, desktop_font_.Get(), gray(0.85f), DWRITE_TEXT_ALIGNMENT_CENTER);
        const wchar_t* names[] = {trKey(L"Odcień"), trKey(L"Nasycenie"), trKey(L"Jasność")};
        for (int slider = 0; slider < settings_ui::ColorEditor::sliders; ++slider) {
            const auto t = L.colorTrack(slider);
            const float cy = middle(t);
            textLine(tr(names[slider]), sw.right + 24, cy, t.left - sw.right - 32, desktop_font_.Get(), gray(0.85f));
            // The gradient the slider runs along.
            std::vector<D2D1_GRADIENT_STOP> stops;
            if (slider == 0) for (int k = 0; k <= 6; ++k)
                stops.push_back({k / 6.0f, hex(settings_ui::ColorEditor::rgb(k * 60.0f, 1, std::max(0.35f, editor->value)))});
            else if (slider == 1) stops = {{0, hex(settings_ui::ColorEditor::rgb(editor->hue, 0, editor->value))}, {1, hex(settings_ui::ColorEditor::rgb(editor->hue, 1, editor->value))}};
            else stops = {{0, gray(0)}, {1, hex(settings_ui::ColorEditor::rgb(editor->hue, editor->saturation, 1))}};
            ComPtr<ID2D1GradientStopCollection> collection;
            ComPtr<ID2D1LinearGradientBrush> gradient;
            const Rect bar{t.left, cy - 5, t.right, cy + 5};
            if (SUCCEEDED(target_->CreateGradientStopCollection(stops.data(), static_cast<UINT32>(stops.size()), collection.GetAddressOf())) &&
                SUCCEEDED(target_->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(D2D1::Point2F(bar.left, cy), D2D1::Point2F(bar.right, cy)),
                                                            collection.Get(), gradient.GetAddressOf())))
                target_->FillRoundedRectangle(D2D1::RoundedRect(box(bar), 5, 5), gradient.Get());
            const float x = std::lerp(t.left, t.right, editor->level(slider));
            brush(gray(0.98f));
            target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(x, cy), 8, 8), solid_.Get());
            brush(gray(0, 0.4f));
            target_->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(x, cy), 8, 8), solid_.Get(), 1);
            const int value = slider == 0 ? static_cast<int>(std::lround(editor->hue)) : static_cast<int>(std::lround(editor->level(slider) * 100));
            textLine(std::to_wstring(value) + (slider == 0 ? L"°" : L"%"), t.right + 12, cy, d.right - 24 - t.right - 12, desktop_font_.Get(), gray(0.8f),
                     DWRITE_TEXT_ALIGNMENT_TRAILING);
            if (view.keyboard && editor->focus == slider) desktopFocusRing({t.left - 6, cy - 12, t.right + 6, cy + 12}, 10);
        }
        desktopButton(L.colorCancel(), 1, view.hover.kind == Kind::dialog_cancel ? 1.0f : 0.0f, view.pressed.kind == Kind::dialog_cancel, true, 0, tr(L"Anuluj"));
        desktopButton(L.colorSave(), 2, view.hover.kind == Kind::dialog_save ? 1.0f : 0.0f, view.pressed.kind == Kind::dialog_save, true, 0, tr(L"Zapisz"));
        if (view.keyboard && editor->focus == settings_ui::ColorEditor::save) desktopFocusRing(L.colorSave(), 8);
        if (view.keyboard && editor->focus == settings_ui::ColorEditor::cancel) desktopFocusRing(L.colorCancel(), 8);
    }
}
