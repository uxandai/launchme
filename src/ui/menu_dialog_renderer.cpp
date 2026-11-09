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
#include <cmath>

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
    case context_menu::Item::identify: return glyph::search;
    case context_menu::Item::forget_recent: return glyph::hide;
    case context_menu::Item::studio_earlier: return L'\uE70E'; // Chevron up.
    case context_menu::Item::studio_later: return glyph::chevron_down;
    case context_menu::Item::studio_hide: return glyph::hide;
    case context_menu::Item::emulator: return glyph::game;
    }
    return glyph::more;
}
}

void Renderer::drawContextMenuDialog() {
    const auto& active = (*cards_)[selected_];
    const float alpha = smoothFade(context_->menu_progress);
    auto rows = context_->menu_rows; // As Launcher::menuRows built them (identify, the studio's rows, the emulator...).
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
    textLine(active.game.name, title_left, panel.top + 64, panel.right - title_left - 30, psfive_name_font_.Get(), color(0.97f, 0.97f, 0.97f, alpha));
    textLine(tr(L"Menu gry"), title_left, panel.top + 93, panel.right - title_left - 30, settings_body_font_.Get(), color(0.6f, 0.6f, 0.6f, alpha));
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
        case context_menu::Item::identify: return tr(L"Zidentyfikuj grę…");
        case context_menu::Item::forget_recent: return tr(L"Usuń z ostatnio granych");
        case context_menu::Item::studio_earlier: return tr(L"Przesuń wcześniej");
        case context_menu::Item::studio_later: return tr(L"Przesuń dalej");
        case context_menu::Item::studio_hide: return tr(L"Ukryj");
        case context_menu::Item::emulator: return context_->menu_emulator;
        }
        return {};
    };
    // The white button glides between rows (ViewContext::menu_glide, a fractional row) and a long list
    // slides under it, as the other menus do; the text darkens as the button reaches its row.
    const float glide = std::clamp(context_->menu_glide.value_or(static_cast<float>(context_->menu_selected)), 0.0f, static_cast<float>(std::max(0, count - 1)));
    const float first = count <= dialog.visible ? 0.0f :
        std::clamp(glide - static_cast<float>(dialog.visible / 2), 0.0f, static_cast<float>(count - dialog.visible));
    const auto rowBox = [&](float index) {
        const float y = panel.top + context_menu::Dialog::header + (index - first) * context_menu::Dialog::row_step;
        return D2D1::RectF(panel.left + context_menu::Dialog::padding, y, panel.right - context_menu::Dialog::padding, y + context_menu::Dialog::row_height);
    };
    const float list_top = panel.top + context_menu::Dialog::header - 6;
    const float list_bottom = panel.top + context_menu::Dialog::header + dialog.visible * context_menu::Dialog::row_step;
    target_->PushAxisAlignedClip(D2D1::RectF(panel.left, list_top, panel.right, list_bottom), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    const auto dark = drawButton(rowBox(glide), 1.0f, alpha, false);
    const int from = std::max(0, static_cast<int>(std::floor(first))), to = std::min(count, static_cast<int>(std::ceil(first)) + dialog.visible + 1);
    for (int row = from; row < to; ++row) {
        const auto box = rowBox(static_cast<float>(row));
        const auto entry = context_menu::item(row, rows);
        const float focus = std::clamp(1.0f - std::abs(static_cast<float>(row) - glide), 0.0f, 1.0f);
        const bool destructive = entry == context_menu::Item::close || entry == context_menu::Item::uninstall || entry == context_menu::Item::remove;
        const bool enabled = entry != context_menu::Item::primary || context_->primary_enabled;
        // The others: a faint glass row, fading where the button passes.
        if (focus < 0.999f) {
            brush(color(1, 1, 1, 0.045f * alpha * (1.0f - focus)));
            target_->FillRoundedRectangle(pill(box), solid_.Get());
        }
        const auto mix = [&](D2D1_COLOR_F light) {
            return color(std::lerp(light.r, dark.r, focus), std::lerp(light.g, dark.g, focus), std::lerp(light.b, dark.b, focus), std::lerp(light.a, dark.a, focus));
        };
        const auto tone = !enabled ? color(0.53f, 0.53f, 0.53f, 0.75f * alpha) : mix(destructive ? palette::danger(alpha) : color(0.95f, 0.95f, 0.95f, alpha));
        // The icon in a small disc on the left.
        const float cx = box.left + 44, cy = (box.top + box.bottom) * 0.5f;
        brush(focus > 0.5f ? color(0, 0, 0, 0.08f * alpha) : color(1, 1, 1, 0.07f * alpha));
        target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), 23, 23), solid_.Get());
        desktopGlyph(icon(entry, context_->menu_hidden), cx, cy, 20, tone);
        textLine(label(entry, row), box.left + 88, cy, box.right - box.left - 110, category_font_.Get(), tone);
    }
    target_->PopAxisAlignedClip();
    // A scroll bar when not every row fits.
    if (count > dialog.visible) {
        const float track_top = panel.top + context_menu::Dialog::header, track = dialog.visible * context_menu::Dialog::row_step - 8;
        const float thumb = std::max(36.0f, track * dialog.visible / count);
        const float y = track_top + (track - thumb) * first / static_cast<float>(std::max(1, count - dialog.visible));
        brush(color(1, 1, 1, 0.28f * alpha));
        target_->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(panel.right - 12, y, panel.right - 8, y + thumb), 2, 2), solid_.Get());
    }
}
