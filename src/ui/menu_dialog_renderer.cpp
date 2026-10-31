// Okna i podpowiedzi → Duże menu gry: the game menu of the home screen (every layout) as a large
// dialog in the middle of the screen, the game's cover and name in its header and an icon beside
// each option; a long menu scrolls with the selection. Rows and actions are the menu's own
// (context_menu.hpp); geometry context_menu::Dialog, shared with pointer.cpp.
#include "renderer.hpp"
#include "renderer_internal.hpp"
#include "context_menu.hpp"
#include "desktop_view.hpp"
#include "../i18n/i18n.hpp"

#include <algorithm>

using namespace render_detail;

namespace {
// The desktop window's game menu icons (desktop_menus.cpp), Segoe Fluent / MDL2 code points.
wchar_t icon(context_menu::Item item, bool hidden) {
    namespace glyph = desktop_ui::glyph;
    switch (item) {
    case context_menu::Item::primary: return glyph::play;
    case context_menu::Item::suspend: return glyph::pause;
    case context_menu::Item::plugin: return glyph::open;
    case context_menu::Item::details: return glyph::info;
    case context_menu::Item::artwork: return glyph::picture;
    case context_menu::Item::retry: return glyph::refresh;
    case context_menu::Item::close: return glyph::stop;
    case context_menu::Item::folder: return glyph::folder_open;
    case context_menu::Item::hide: return hidden ? glyph::view : glyph::hide;
    case context_menu::Item::exclude: return glyph::remove;
    case context_menu::Item::uninstall: return glyph::remove;
    case context_menu::Item::launch: return glyph::settings;
    case context_menu::Item::metadata: return glyph::edit;
    case context_menu::Item::aurelia: return glyph::edit;
    case context_menu::Item::steam_account: return glyph::people;
    case context_menu::Item::remove: return glyph::remove;
    case context_menu::Item::compat: return glyph::settings;
    case context_menu::Item::shortcuts: return glyph::open;
    }
    return glyph::more;
}
}

void Renderer::drawContextMenuDialog() {
    const auto& active = (*cards_)[selected_];
    const float alpha = smoothFade(context_->menu_progress);
    context_menu::Rows rows{context_->menu_suspend, static_cast<int>(context_->menu_plugins.size()), context_->menu_session,
                            context_->menu_detail_options, context_->menu_folder, context_->menu_exclude, context_->menu_hide, context_->psfive};
    rows.uninstall = context_->menu_uninstall;
    rows.remove = context_->menu_remove;
    rows.launch = context_->menu_launch;
    rows.compat = context_->menu_compat;
    rows.shortcuts = context_->menu_shortcuts;
    rows.metadata = context_->menu_metadata;
    rows.aurelia = context_->menu_aurelia;
    rows.steam_account = !context_->menu_steam_account.empty();
    const int count = context_menu::rows(rows);
    const context_menu::Dialog dialog{viewport_.width, viewport_.height, count};
    // The home dims under it, as under the other dialogs.
    brush(color(0, 0, 0, 0.62f * alpha));
    target_->FillRectangle(D2D1::RectF(0, 0, viewport_.width, viewport_.height), solid_.Get());
    const float rise = (1.0f - alpha) * 24.0f;
    const auto panel = D2D1::RectF(dialog.panel.left, dialog.panel.top + rise, dialog.panel.right, dialog.panel.bottom + rise);
    glassFill(glassPanel(D2D1::RoundedRect(panel, 26, 26)), color(0.022f, 0.024f, 0.027f, 0.95f), alpha);
    if (panel_borders_) {
        brush(color(0.90f, 0.90f, 0.90f, 0.5f * alpha));
        target_->DrawRoundedRectangle(D2D1::RoundedRect(panel, 26, 26), solid_.Get(), 1.4f);
    }
    // Header: the cover (when decoded) and the game's name.
    const float header_left = panel.left + context_menu::Dialog::padding;
    float title_left = header_left;
    if (selected_ < covers_.size() && covers_[selected_].bitmap) {
        const auto& cover = covers_[selected_];
        const auto box = D2D1::RectF(header_left, panel.top + 26, header_left + 96, panel.top + 122);
        const auto size = cover.bitmap->GetSize();
        const float side = std::min(size.width, size.height);
        const auto source = D2D1::RectF((size.width - side) * 0.5f, (size.height - side) * 0.5f, (size.width + side) * 0.5f, (size.height + side) * 0.5f);
        if (const auto shape = tileShape(box, tile_shape_)) {
            target_->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), shape.Get()), nullptr);
            target_->DrawBitmap(cover.bitmap.Get(), box, alpha, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, &source);
            target_->PopLayer();
        }
        title_left = box.right + 24;
    }
    textLine(active.game.name, title_left, panel.top + 62, panel.right - title_left - 30, psfive_name_font_.Get(), color(0.97f, 0.97f, 0.97f, alpha));
    textLine(tr(L"Menu gry"), title_left, panel.top + 100, panel.right - title_left - 30, settings_body_font_.Get(), color(0.6f, 0.6f, 0.6f, alpha));
    const std::wstring account_label = tr(L"Konto Steam: ") + context_->menu_steam_account;
    const auto label = [&](context_menu::Item item, int row) -> std::wstring {
        switch (item) {
        case context_menu::Item::primary: return context_->primary_action;
        case context_menu::Item::suspend: return tr(L"Wstrzymaj");
        case context_menu::Item::plugin: {
            const int index = row - context_menu::row(context_menu::Item::plugin, rows);
            return index >= 0 && index < static_cast<int>(context_->menu_plugins.size()) ? context_->menu_plugins[static_cast<size_t>(index)] : std::wstring{};
        }
        case context_menu::Item::details: return tr(L"Szczegóły");
        case context_menu::Item::artwork: return tr(L"Zmień grafikę");
        case context_menu::Item::retry: return tr(L"Ponów grafiki");
        case context_menu::Item::close: return tr(L"Zamknij grę");
        case context_menu::Item::folder: return tr(L"Otwórz folder");
        case context_menu::Item::hide: return context_->menu_hidden ? tr(L"Pokaż") : tr(L"Ukryj");
        case context_menu::Item::exclude: return tr(L"Wyklucz");
        case context_menu::Item::uninstall: return tr(L"Odinstaluj");
        case context_menu::Item::launch: return context_->menu_edit_game ? tr(L"Edytuj grę") : tr(L"Opcje uruchamiania");
        case context_menu::Item::remove: return tr(L"Usuń z biblioteki");
        case context_menu::Item::metadata: return tr(L"Edytuj metadane");
        case context_menu::Item::aurelia: return tr(L"Zarządzaj · Aurelia");
        case context_menu::Item::steam_account: return account_label;
        case context_menu::Item::compat: return L"Proton / Wine";
        case context_menu::Item::shortcuts: return tr(L"Skróty");
        }
        return {};
    };
    const int first = dialog.first(context_->menu_selected);
    for (int row = first; row < std::min(count, first + dialog.visible); ++row) {
        auto r = dialog.row(row, first);
        r.top += rise; r.bottom += rise;
        const auto box = D2D1::RectF(r.left, r.top, r.right, r.bottom);
        const auto entry = context_menu::item(row, rows);
        const bool focused = row == context_->menu_selected;
        const bool destructive = entry == context_menu::Item::close || entry == context_menu::Item::uninstall || entry == context_menu::Item::remove;
        const bool enabled = entry != context_menu::Item::primary || context_->primary_enabled;
        // The focused row: the white button; the others a faint glass row.
        D2D1_COLOR_F ink = color(0.95f, 0.95f, 0.95f, alpha);
        if (focused) ink = drawButton(box, 1.0f, alpha, false);
        else {
            brush(color(1, 1, 1, 0.045f * alpha));
            target_->FillRoundedRectangle(pill(box), solid_.Get());
        }
        const auto tone = destructive && !focused ? palette::danger(alpha) : !enabled ? color(0.53f, 0.53f, 0.53f, 0.75f * alpha) : ink;
        // The icon in a small disc on the left.
        const float cx = r.left + 44, cy = (r.top + r.bottom) * 0.5f;
        brush(focused ? color(0, 0, 0, 0.08f * alpha) : color(1, 1, 1, 0.07f * alpha));
        target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), 23, 23), solid_.Get());
        desktopGlyph(icon(entry, context_->menu_hidden), cx, cy, 20, tone);
        textLine(label(entry, row), r.left + 88, cy, r.right - r.left - 110, category_font_.Get(), tone);
    }
    // A scroll bar when not every row fits.
    if (count > dialog.visible) {
        const float track_top = panel.top + context_menu::Dialog::header, track = dialog.visible * context_menu::Dialog::row_step - 8;
        const float thumb = std::max(36.0f, track * dialog.visible / count);
        const float y = track_top + (track - thumb) * first / std::max(1, count - dialog.visible);
        brush(color(1, 1, 1, 0.28f * alpha));
        target_->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(panel.right - 12, y, panel.right - 8, y + thumb), 2, 2), solid_.Get());
    }
}
