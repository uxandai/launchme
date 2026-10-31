// Desktop mode's frame around the library: the top bar (tabs, search, tools, window buttons) and
// the sidebar (sections, platforms, collections and her card).
#include "renderer.hpp"
#include "desktop_render_internal.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

using Microsoft::WRL::ComPtr;
using namespace desktop_ui;
using namespace desktop_draw;

// ---- Top bar ---------------------------------------------------------------------------------

void Renderer::drawDesktopTitlebar(const Layout& layout, const View& view) {
    const float ink_alpha = view.active_window ? 1.0f : 0.55f;
    brush(side_bg);
    target_->FillRectangle(D2D1::RectF(0, 0, viewport_.width, title_h), solid_.Get());
    brush(gray(1, 0.06f));
    target_->FillRectangle(D2D1::RectF(0, title_h - 1, viewport_.width, title_h), solid_.Get());
    // The sidebar's toggle (a panel with its left column), then the logo: symbol and wordmark while
    // the sidebar is wide, the symbol alone when it is narrow, crossfading with its animation.
    const float cy = title_h * 0.5f;
    {
        const auto toggle = layout.sidebarToggle();
        const bool hover = view.hover.target == Target::sidebar_toggle, pressed = view.pressed.target == Target::sidebar_toggle;
        if (hover || pressed) desktopFill(toggle, 8, gray(1, pressed ? 0.11f : 0.075f));
        const float cx = (toggle.left + toggle.right) * 0.5f, w = 16, h = 13;
        const auto frame = D2D1::RectF(cx - w * 0.5f, cy - h * 0.5f, cx + w * 0.5f, cy + h * 0.5f);
        const auto ink = hover ? palette::text(ink_alpha) : palette::muted(ink_alpha);
        brush(ink);
        target_->DrawRoundedRectangle(D2D1::RoundedRect(frame, 2.5f, 2.5f), solid_.Get(), 1.4f);
        const float column = 6.0f - 2.5f * view.sidebar_narrow; // Narrower with the sidebar.
        target_->FillRectangle(D2D1::RectF(frame.left + 2.5f, frame.top + 2.5f, frame.left + 2.5f + column, frame.bottom - 2.5f), solid_.Get());
        if (view.keyboard && view.hover.target == Target::sidebar_toggle) desktopFocusRing(toggle, 8);
    }
    const float narrow = std::clamp(view.sidebar_narrow, 0.0f, 1.0f);
    if (narrow < 1) drawLaunchmeLogo(layout.logoLeft(), cy, desktop_ui::logo_height, palette::text(ink_alpha * (1 - narrow)));
    if (narrow > 0) drawLaunchmeLogo(layout.logoLeft(), cy, desktop_ui::logo_height, palette::text(ink_alpha * narrow), false, true);
    // Search.
    const auto search = layout.search();
    const bool search_hover = view.hover.target == Target::search || view.hover.target == Target::search_clear;
    desktopFill(search, 9, gray(1, view.search_focus ? 0.075f : search_hover ? 0.06f : 0.045f));
    if (view.search_focus) desktopStroke(inflate(search, 1), 10, uiAccent(0.9f), 1.5f);
    else desktopStroke(search, 9, gray(1, search_hover ? 0.14f : 0.08f));
    desktopGlyph(glyph::search, search.left + 18, cy, 14, palette::muted(ink_alpha));
    const float text_left = search.left + 36, text_right = search.right - (view.query.empty() ? 70 : 36);
    const auto metrics = typeMetrics(desktop_font_.Get());
    const float text_top = cy - metrics.line * 0.5f;
    if (view.query.empty()) {
        textLine(tr(L"Szukaj w bibliotece"), text_left, cy, text_right - text_left, desktop_font_.Get(), palette::muted(0.85f * ink_alpha));
        if (view.search_focus && view.caret_on) text(L"│", text_left - 1, text_top, 8, metrics.line, desktop_font_.Get(), palette::text());
        else if (!view.search_focus) {
            const Rect key{search.right - 62, cy - 10, search.right - 10, cy + 10};
            desktopStroke(key, 5, gray(1, 0.12f));
            // Ctrl+K: the quick search over everything (Ctrl+F still types here).
            textLine(L"Ctrl+K", key.left, cy, key.right - key.left, desktop_small_font_.Get(), palette::muted(ink_alpha), DWRITE_TEXT_ALIGNMENT_CENTER);
        }
    } else {
        if (view.search_focus) desktopSelection(view.query, text_left, cy, text_right - text_left, desktop_font_.Get());
        fieldText(view.query, view.caret, view.search_focus && view.caret_on, text_left, text_top, text_right - text_left, metrics.line,
                  desktop_font_.Get(), palette::text(ink_alpha));
        const auto clear = layout.searchClear();
        if (view.hover.target == Target::search_clear) desktopFill(clear, 6, gray(1, 0.08f));
        desktopGlyph(glyph::close, (clear.left + clear.right) * 0.5f, cy, 10, palette::muted(ink_alpha));
    }
    // Shortcuts.
    for (int i = 0; i < static_cast<int>(view.tools.size()); ++i) {
        const auto r = layout.tool(i);
        const auto tool = view.tools[static_cast<size_t>(i)];
        const bool hover = view.hover == Hit{Target::tool, i};
        const bool pressed = view.pressed == Hit{Target::tool, i};
        const bool open = tool == Tool::downloads && view.downloads_open;
        if (hover || open) desktopFill(r, 8, gray(1, pressed ? 0.12f : open ? 0.1f : 0.07f));
        const float x = (r.left + r.right) * 0.5f, y = (r.top + r.bottom) * 0.5f;
        const auto ink = gray(hover || open ? 0.98f : 0.80f, ink_alpha);
        switch (tool) {
        case Tool::glance: drawGlanceGlyph(x, y, 15, ink); break;
        case Tool::tarot: drawTarotGlyph(x, y, 16, ink); break;
        case Tool::companion: desktopGlyph(glyph::people, x, y, 16, ink); break;
        case Tool::store: desktopGlyph(glyph::shop, x, y, 16, ink); break;
        case Tool::friends: { // Widżety: three tiles and a turned one, as a widgets icon.
            brush(ink);
            const float s = 6.4f, g = 2.8f, l = x - s - g * 0.5f, t = y - s - g * 0.5f;
            for (const auto& [tx, ty] : {std::pair{l, t}, std::pair{l, t + s + g}, std::pair{l + s + g, t + s + g}})
                target_->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(tx, ty, tx + s, ty + s), 1.6f, 1.6f), solid_.Get(), 1.4f);
            const float dx = l + s + g + s * 0.5f, dy = t + s * 0.5f, d = s * 0.62f;
            ComPtr<ID2D1PathGeometry> diamond; ComPtr<ID2D1GeometrySink> sink;
            if (SUCCEEDED(factory_->CreatePathGeometry(diamond.GetAddressOf())) && SUCCEEDED(diamond->Open(sink.GetAddressOf()))) {
                sink->BeginFigure(D2D1::Point2F(dx, dy - d), D2D1_FIGURE_BEGIN_HOLLOW);
                sink->AddLine(D2D1::Point2F(dx + d, dy)); sink->AddLine(D2D1::Point2F(dx, dy + d)); sink->AddLine(D2D1::Point2F(dx - d, dy));
                sink->EndFigure(D2D1_FIGURE_END_CLOSED);
                if (SUCCEEDED(sink->Close())) target_->DrawGeometry(diamond.Get(), solid_.Get(), 1.4f);
            }
            break;
        }
        case Tool::tv: desktopGlyph(glyph::tv, x, y, 17, ink); break;
        case Tool::settings: desktopGlyph(glyph::settings, x, y, 16, ink); break;
        case Tool::update: {
            // A newer Launchme: an accent arrow with a dot; downloading, its ring fills.
            desktopGlyph(glyph::download, x, y, 15, uiAccent(ink_alpha));
            if (view.update_progress >= 0) {
                desktopArc(x, y, 13, 0, 2 * std::numbers::pi_v<float>, gray(1, 0.1f), 2);
                desktopArc(x, y, 13, -std::numbers::pi_v<float> * 0.5f, std::max(0.08f, view.update_progress) * 2 * std::numbers::pi_v<float>, uiAccent(ink_alpha), 2);
            } else {
                brush(uiAccent(ink_alpha));
                target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(r.right - 8, r.top + 8), 4, 4), solid_.Get());
            }
            break;
        }
        case Tool::downloads: {
            desktopGlyph(glyph::download, x, y, 15, view.downloads_moving ? uiAccent(ink_alpha) : ink);
            if (view.downloads_moving) {
                // Its ring: the transfers' share, or a turning segment while none is known.
                desktopArc(x, y, 13, 0, 2 * std::numbers::pi_v<float>, gray(1, 0.1f), 2);
                if (view.downloads_fraction >= 0)
                    desktopArc(x, y, 13, -std::numbers::pi_v<float> * 0.5f, std::max(0.12f, view.downloads_fraction) * 2 * std::numbers::pi_v<float>, uiAccent(ink_alpha), 2);
                else desktopArc(x, y, 13, elapsed_ * 4.0f, 1.4f, uiAccent(ink_alpha), 2);
                const auto count = std::to_wstring(view.downloads_moving);
                const Rect badge{r.right - 15, r.top + 1, r.right - 1, r.top + 15};
                desktopFill(badge, 7, uiAccent(ink_alpha));
                const auto accent = uiAccent();
                const float luminance = 0.2126f * accent.r + 0.7152f * accent.g + 0.0722f * accent.b;
                textLine(count, badge.left, (badge.top + badge.bottom) * 0.5f, badge.right - badge.left, desktop_label_font_.Get(),
                         luminance > 0.6f ? gray(0.06f) : gray(0.98f), DWRITE_TEXT_ALIGNMENT_CENTER);
            }
            break;
        }
        }
    }
    // Window buttons.
    for (int i = 0; i < 3; ++i) {
        const auto r = layout.windowButton(i);
        const Target target = i == 0 ? Target::minimize : i == 1 ? Target::maximize : Target::close;
        const bool hover = view.hover.target == target || (i == 1 && view.max_hover);
        if (hover) desktopFill(r, 0, i == 2 ? rgb(0.77f, 0.17f, 0.11f) : gray(1, 0.08f));
        const wchar_t g = i == 0 ? glyph::minimize : i == 1 ? (view.maximized ? glyph::restore : glyph::maximize) : glyph::window_close;
        desktopGlyph(g, (r.left + r.right) * 0.5f, cy, 10, gray(hover ? 1.0f : 0.82f, hover ? 1.0f : ink_alpha));
    }
}

// ---- Sidebar ---------------------------------------------------------------------------------

void Renderer::drawDesktopSidebar(const Layout& layout, const View& view) {
    const auto side = layout.sidebar();
    brush(side_bg);
    target_->FillRectangle(box(side), solid_.Get());
    brush(gray(1, 0.06f));
    target_->FillRectangle(D2D1::RectF(side.right - 1, side.top, side.right, side.bottom), solid_.Get());
    const float top = layout.sidebarTop(), bottom = layout.sidebarBottom();
    target_->PushAxisAlignedClip(D2D1::RectF(0, top, side.right - 1, bottom), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    const auto* entries = view.entries;
    constexpr const wchar_t* groups[] = {trKey(L"Biblioteka"), trKey(L"Platformy"), trKey(L"Emulacja"), trKey(L"Kolekcje"), trKey(L"Wydawcy"),
                                         trKey(L"Developerzy")};
    // The active section's pill and left bar glide between entries on a spring.
    if (view.active_y) {
        bool drop_on_active = false;
        for (int i = 0; i < static_cast<int>(view.rows.size()); ++i)
            if (!view.rows[static_cast<size_t>(i)].heading && view.rows[static_cast<size_t>(i)].entry == view.active && view.drop_row == i)
                drop_on_active = true;
        if (!drop_on_active) {
            const float ay = layout.sidebarTop() + *view.active_y - view.sidebar_scroll;
            const Rect active_rect{10, ay, layout.sidebarRight() - 10, ay + row_h};
            if (active_rect.bottom >= top && active_rect.top <= bottom) {
                desktopFill(active_rect, 8, gray(1, 0.085f));
                desktopFill({active_rect.left, active_rect.top + 8, active_rect.left + 3, active_rect.bottom - 8}, 1.5f, uiAccent(0.95f));
            }
        }
    }
    for (int i = 0; i < static_cast<int>(view.rows.size()); ++i) {
        const auto& row = view.rows[static_cast<size_t>(i)];
        const auto r = layout.sidebarRow(row, view.sidebar_scroll);
        if (r.bottom < top || r.top > bottom) continue;
        const float cy = (r.top + r.bottom) * 0.5f;
        const bool hover = view.hover_row == i;
        const bool focused = view.keyboard && view.zone == 0 && view.focus_row == i;
        if (row.heading) {
            const bool collapsed = view.collapsed[static_cast<size_t>(row.group)];
            const float collapse_t = view.collapse_progress[static_cast<size_t>(row.group)];
            textLine(upper(tr(groups[static_cast<int>(row.group)])), r.left + 10, cy + 2, r.right - r.left - 60, desktop_label_font_.Get(),
                     gray(hover ? 0.78f : 0.55f));
            // Always shown (faint at rest), so a group visibly folds; brighter under the pointer.
            desktopGlyph(glyph::chevron_down,
                         r.right - (row.group == Group::collections ? 40 : row.group == Group::library ? 68 : 14), cy + 2, 9,
                         gray(0.6f, hover || collapsed || collapse_t > 0.01f ? 1.0f : 0.45f), -90.0f * collapse_t);
            if (row.group == Group::library) {
                // "+": a game from disk; the arrows: read the libraries again.
                const auto add = layout.headingAdd(row, view.sidebar_scroll), refresh = layout.headingRefresh(row, view.sidebar_scroll);
                const bool add_hover = view.hover == Hit{Target::library_add, i}, refresh_hover = view.hover == Hit{Target::library_refresh, i};
                if (add_hover) desktopFill(add, 6, gray(1, 0.08f));
                if (refresh_hover) desktopFill(refresh, 6, gray(1, 0.08f));
                desktopGlyph(glyph::add, (add.left + add.right) * 0.5f, cy + 1, 11, gray(add_hover ? 0.98f : 0.62f));
                desktopGlyph(glyph::refresh, (refresh.left + refresh.right) * 0.5f, cy + 1, 11,
                             view.refreshing ? uiAccent(0.95f) : gray(refresh_hover ? 0.98f : 0.62f),
                             view.refreshing ? view.refresh_angle * 180.0f / std::numbers::pi_v<float> : 0.0f);
            }
            if (row.group == Group::collections) {
                const auto add = layout.headingAdd(row, view.sidebar_scroll);
                const bool add_hover = view.hover == Hit{Target::sidebar_add, i};
                if (add_hover) desktopFill(add, 6, gray(1, 0.08f));
                desktopGlyph(glyph::add, (add.left + add.right) * 0.5f, cy + 1, 11, gray(add_hover ? 0.98f : 0.62f));
            }
            if (focused) desktopFocusRing(r, 8);
            continue;
        }
        if (!entries || row.entry < 0 || row.entry >= static_cast<int>(entries->size())) continue;
        const auto& entry = (*entries)[static_cast<size_t>(row.entry)];
        const bool active = row.entry == view.active;
        const bool drop = view.drop_row == i;
        const float flash = row.entry == view.drop_flash_entry ? smoothFade(view.drop_flash) : 0.0f;
        const bool lifted = view.reorder_row == i;
        if (drop) { desktopFill(r, 8, uiAccent(0.16f)); desktopStroke(r, 8, uiAccent(0.9f), 1.5f); }
        else if (active && !view.active_y) desktopFill(r, 8, gray(1, 0.085f));
        else if (hover && !active) desktopFill(r, 8, gray(1, 0.045f));
        if (flash > 0.003f && !drop) {
            desktopFill(r, 8, uiAccent(0.22f * flash));
            desktopStroke(r, 8, uiAccent(0.85f * flash), 1.5f);
        }
        if (active && !drop && !view.active_y) desktopFill({r.left, r.top + 8, r.left + 3, r.bottom - 8}, 1.5f, uiAccent(0.95f));
        const float alpha = lifted ? 0.45f : 1.0f;
        // Narrow: the icon moves to the row's middle and the title and count fade out.
        const float narrow = std::clamp(view.sidebar_narrow, 0.0f, 1.0f);
        const float icon_x = r.left + 20 + ((r.left + r.right) * 0.5f - r.left - 20) * narrow;
        if (!desktopBrand(entry.brand, icon_x, cy, 15, active ? palette::text(alpha) : gray(0.78f, alpha)))
            desktopGlyph(entry.kind == EntryKind::favorites && active ? glyph::star_fill : entry.icon, icon_x, cy, 14,
                         active ? palette::text(alpha) : gray(0.62f, alpha));
        if (focused) desktopFocusRing(r, 8);
        if (narrow > 0.98f) continue;
        const float text_alpha = alpha * (1 - narrow);
        const auto ink = active || drop || flash > 0.2f ? palette::text(text_alpha) : gray(0.80f, text_alpha);
        target_->PushAxisAlignedClip(D2D1::RectF(r.left, r.top, r.right, r.bottom), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        struct Unclip { ID2D1RenderTarget* target; ~Unclip() { target->PopAxisAlignedClip(); } } unclip{target_.Get()};
        const auto count = std::to_wstring(entry.count);
        const float count_width = textAdvance(count, desktop_small_font_.Get());
        textLine(entry.title, r.left + 38, cy, std::max(1.0f, r.right - r.left - 50 - count_width), active ? desktop_strong_font_.Get() : desktop_font_.Get(), ink);
        if (flash > 0.003f) {
            D2D1_MATRIX_3X2_F base{};
            target_->GetTransform(&base);
            const float scale = 1.0f + 0.35f * std::sin(view.drop_flash * std::numbers::pi_v<float>);
            const float cx = r.right - 10 - count_width * 0.5f;
            target_->SetTransform(D2D1::Matrix3x2F::Scale(scale, scale, D2D1::Point2F(cx, cy)) * base);
            textLine(count, r.right - 10 - count_width, cy, count_width + 2, desktop_strong_font_.Get(), uiAccent(text_alpha));
            target_->SetTransform(base);
        } else textLine(count, r.right - 10 - count_width, cy, count_width + 2, desktop_small_font_.Get(), palette::muted(text_alpha));
    }
    if (view.reorder_y >= 0) {
        brush(uiAccent());
        target_->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(16, view.reorder_y - 1, side.right - 16, view.reorder_y + 1), 1, 1), solid_.Get());
        target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(16, view.reorder_y), 3, 3), solid_.Get());
    }
    target_->PopAxisAlignedClip();
    // More rows above or below: the list fades into the panel.
    const float max_scroll = layout.sidebarMaxScroll(view.rows);
    const auto fade = [&](ComPtr<ID2D1LinearGradientBrush>& cached, float y0, float y1) {
        if (auto* gradient = desktopGradient(cached, side_bg, rgb(side_bg.r, side_bg.g, side_bg.b, 0), y0, y1))
            target_->FillRectangle(D2D1::RectF(0, std::min(y0, y1), side.right - 1, std::max(y0, y1)), gradient);
    };
    if (view.sidebar_scroll > 1) fade(desktop_fade_top_, top, top + 18);
    if (view.sidebar_scroll < max_scroll - 1) fade(desktop_fade_bottom_, bottom, bottom - 18);
    drawDesktopCompanion(layout, view);
}

// Her card at the bottom of the sidebar: her face, name and character; her remarks pop out of it.
void Renderer::drawDesktopCompanion(const Layout& layout, const View& view) {
    if (!view.companion_card || !context_->companion) return;
    const auto& her = *context_->companion;
    const auto card = layout.companionCard();
    const bool hover = view.hover.target == Target::companion_card || view.hover.target == Target::companion_bubble;
    desktopFill(card, 12, card_bg);
    if (hover) desktopFill(card, 12, gray(1, 0.04f));
    desktopStroke(card, 12, gray(1, hover ? 0.14f : 0.06f));
    const float cy = (card.top + card.bottom) * 0.5f, face = 24, cx = card.left + 12 + face;
    drawCompanionFace(her.look, her.pose, cx, cy, face, 1, her.costume);
    // Her name and character only: her level's badge lives in her chat (no bar in the sidebar).
    const float text_left = cx + face + 12;
    const auto more = layout.companionMore();
    textLine(her.name, text_left, cy - 9, more.left - text_left - 4, desktop_strong_font_.Get(), palette::text());
    textLine(her.persona, text_left, cy + 10, more.left - text_left - 4, desktop_small_font_.Get(), palette::muted());
    if (view.hover.target == Target::companion_more) desktopFill(more, 8, gray(1, 0.09f));
    desktopGlyph(glyph::more, (more.left + more.right) * 0.5f, cy, 16, palette::text());
    // Her line: a bubble beside the card, over the grid, pointing at her face.
    if (view.companion_bubble && her.bubble_alpha > 0.003f) {
        const auto bubble = drawSpeechBubble(her.bubble, her.bubble_waiting && her.bubble.empty(), card.right + 12, cy - 26,
                                             std::min(380.0f, layout.paneLeft() - card.right - 40), card.right - 6, cy, her.bubble_alpha,
                                             title_h + 8, false);
        companion_bubble_ = {bubble.left, bubble.top, bubble.right, bubble.bottom};
    }
}
