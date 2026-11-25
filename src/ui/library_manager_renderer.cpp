#include "renderer.hpp"
#include "../library/versions.hpp"

void Renderer::drawLibraryManager() {
    if (!context_->library_manager) return;
    if (desktopDialogs()) { drawDesktopCollectionsDialog(); return; } // Desktop mode: desktop_dialogs_renderer.cpp.
    const auto& ui = *context_->library_manager;
    const auto layout = library_manager_ui::Layout::of(viewport_.width, viewport_.height, ui);
    const auto rect = [](settings_ui::Rect r) { return D2D1::RectF(r.left, r.top, r.right, r.bottom); };
    const D2D1_COLOR_F white = palette::text(), muted = palette::muted();
    const auto p = layout.panel();
    const auto glass = glassPanel(D2D1::RoundedRect(rect(p), 18, 18)); // The scene before the dim, frosted in the panel.
    brush(D2D1::ColorF(0, 0, 0, 0.8f)); target_->FillRectangle(D2D1::RectF(0, 0, viewport_.width, viewport_.height), solid_.Get());
    glassFill(glass, D2D1::ColorF(0.06f, 0.07f, 0.08f));
    if (panel_borders_) { brush(D2D1::ColorF(1, 1, 1, 0.16f)); target_->DrawRoundedRectangle(D2D1::RoundedRect(rect(p), 18, 18), solid_.Get()); }
    // The focus glides (ViewContext::manager_glide): a white pill on Back, a lighter fill on a row,
    // a version card or "Zapamiętaj wybór". The text field keeps its accent outline instead.
    const bool versions = ui.page == library_manager_ui::Page::versions;
    const auto highlight = [&](settings_ui::Rect target, bool card) {
        auto r = rect(target);
        if (context_->manager_glide) { const auto& g = *context_->manager_glide; r = D2D1::RectF(g.left, g.top, g.right, g.bottom); }
        brush(palette::gray(1, card ? 0.10f : 0.12f));
        target_->FillRoundedRectangle(card ? D2D1::RoundedRect(r, 16, 16) : pill(r), solid_.Get());
    };
    // Back: a glass circle with a dot (the panel is frosted already), white while focused.
    const auto back = layout.back();
    const auto circle = backCircle(back.left, back.top, back.right, back.bottom);
    drawButtonGlass(circle, 1, false);
    if (ui.focus == -1) {
        auto r = circle;
        if (context_->manager_glide) { const auto& g = *context_->manager_glide; r = D2D1::RectF(g.left, g.top, g.right, g.bottom); }
        drawButtonFocus(r);
    }
    brush(buttonInk(ui.focus == -1 ? 1.0f : 0.0f));
    target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F((circle.left + circle.right) / 2, (back.top + back.bottom) / 2), 3, 3), solid_.Get());
    if (versions && ui.focus >= 0) {
        if (ui.focus == static_cast<int>(ui.versions.size())) highlight(layout.remember(), false);
        else if (ui.focus >= ui.scroll && ui.focus < ui.scroll + layout.versionCapacity()) highlight(layout.version(ui.focus - ui.scroll), true);
    } else if (!versions && ui.focus >= ui.scroll && ui.focus < std::min(static_cast<int>(ui.rows.size()), ui.scroll + layout.capacity(ui)))
        highlight(layout.row(ui, ui.focus), false);
    textLine(ui.title, p.left + 108, p.top + 46, p.right - p.left - 140, category_font_.Get(), white);
    if (ui.page == library_manager_ui::Page::versions) {
        for (int i = ui.scroll; i < std::min(static_cast<int>(ui.versions.size()), ui.scroll + layout.versionCapacity()); ++i) {
            const auto& game = ui.versions[i]; const auto r = layout.version(i - ui.scroll);
            // Its card; the focus is the gliding fill drawn above.
            brush(palette::gray(1, 0.035f));
            target_->FillRoundedRectangle(D2D1::RoundedRect(rect(r), 16, 16), solid_.Get());
            // The copy's store or, for a ROM, its console: the logo when there is one, else the name.
            const auto platform = versions::emulatedCopy(game) ? game.platform : sourceName(game.source);
            const auto found = hasPlatformMark(platform) ? platform_badges_.find(platform) : platform_badges_.end();
            if (found != platform_badges_.end()) {
                const auto& badge = found->second; const auto size = badge.bitmap->GetPixelSize();
                const auto source = badge.has_ink_bounds ? badge.ink_bounds : D2D1::RectF(0, 0, static_cast<float>(size.width), static_cast<float>(size.height));
                const float scale = std::min((r.right - r.left - 48) / (source.right - source.left), 44.0f / (source.bottom - source.top));
                const float w = (source.right - source.left) * scale, h = (source.bottom - source.top) * scale, x = (r.left + r.right - w) / 2;
                target_->DrawBitmap(badge.bitmap.Get(), D2D1::RectF(x, r.top + 20, x + w, r.top + 20 + h), 1, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, &source);
            } else textLine(platform, r.left + 24, r.top + 42, r.right - r.left - 48, category_font_.Get(), white, DWRITE_TEXT_ALIGNMENT_CENTER);
            textLine(game.installed ? tr(L"Graj") : primaryActionLabel(game), r.left + 24, r.bottom - 28, r.right - r.left - 48,
                settings_body_font_.Get(), muted, DWRITE_TEXT_ALIGNMENT_CENTER);
        }
        const auto r = layout.remember();
        if (ui.focus != static_cast<int>(ui.versions.size())) { brush(palette::gray(1, 0.035f)); target_->FillRoundedRectangle(pill(rect(r)), solid_.Get()); }
        textLine((ui.remember ? L"☑  " : L"☐  ") + std::wstring(tr(L"Zapamiętaj wybór")), r.left + 18, (r.top + r.bottom) / 2,
            r.right - r.left - 36, settings_body_font_.Get(), white);
        return;
    }
    if (ui.textField()) {
        // A pill field; the accent outline only while it takes typing.
        const auto r = layout.input(); brush(D2D1::ColorF(0.02f, 0.03f, 0.04f)); target_->FillRoundedRectangle(pill(rect(r)), solid_.Get());
        if (ui.focus == -2) { brush(uiAccent()); target_->DrawRoundedRectangle(pill(rect(r)), solid_.Get(), 1.5f); }
        if (ui.selected && ui.focus == -2) { brush(uiAccent(0.20f)); target_->FillRoundedRectangle(pill(rect(r)), solid_.Get()); }
        const auto placeholder = ui.placeholder();
        textLine(ui.input.empty() ? std::wstring(placeholder) : ui.input + (ui.focus == -2 && !ui.selected ? L"│" : L""), r.left + 16,
            (r.top + r.bottom) / 2, r.right - r.left - 32, settings_body_font_.Get(), ui.input.empty() ? muted : white);
    }
    for (int i = ui.scroll; i < std::min(static_cast<int>(ui.rows.size()), ui.scroll + layout.capacity(ui)); ++i) {
        const auto r = layout.row(ui, i); const auto& row = ui.rows[i]; // Its focus: the gliding fill drawn above.
        if (row.toggle) {
            const float x = r.right - 28, y = (r.top + r.bottom) / 2;
            brush(row.checked ? uiAccent() : muted);
            if (row.radio) {
                target_->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(x, y), 11, 11), solid_.Get(), 1.5f);
                if (row.checked) target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(x, y), 6, 6), solid_.Get());
            } else {
                target_->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(x - 12, y - 12, x + 12, y + 12), 4, 4), solid_.Get(), 1.5f);
                if (row.checked) { line(x - 6, y, x - 1, y + 5, white, 2); line(x - 1, y + 5, x + 7, y - 5, white, 2); }
            }
        }
        const float room = r.right - r.left - (row.toggle ? 76 : 32);
        if (row.subtitle.empty()) textLine(row.title, r.left + 16, (r.top + r.bottom) / 2, room, settings_body_font_.Get(), white);
        else {
            textLine(row.title, r.left + 16, r.top + 19, room, settings_body_font_.Get(), white);
            textLine(row.subtitle, r.left + 16, r.top + 43, room, friend_status_font_.Get(), muted);
        }
    }
    if (ui.page == library_manager_ui::Page::excluded && ui.rows.empty())
        textLine(ui.input.empty() ? tr(L"Brak wykluczonych gier") : tr(L"Brak wyników"), p.left + 44, layout.listTop(ui) + 30,
                 p.right - p.left - 88, settings_body_font_.Get(), muted);
    if (!ui.error.empty()) textLine(ui.error, p.left + 28, p.bottom - 32, p.right - p.left - 56, settings_body_font_.Get(), palette::danger());
}
