// Renderer: the power menu's picker, the exit prompt and a game's context menu.
#include "renderer.hpp"
#include "renderer_internal.hpp"
#include "context_menu.hpp"
#include "exit_prompt.hpp"
#include "power_menu.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

using Microsoft::WRL::ComPtr;
using namespace render_detail;

// Power menu uses the Settings choice-picker geometry.
void Renderer::drawPicker(const std::wstring& title, const std::vector<std::wstring>& labels, int selected) {
    const int count = static_cast<int>(labels.size());
    const settings_ui::Layout layout{viewport_.width, viewport_.height};
    const auto panel = layout.picker(count);
    const auto shape = [](settings_ui::Rect r, float radius) { return D2D1::RoundedRect(D2D1::RectF(r.left, r.top, r.right, r.bottom), radius, radius); };
    brush(color(0, 0, 0, 0.78f));
    target_->FillRectangle(D2D1::RectF(0, 0, viewport_.width, viewport_.height), solid_.Get());
    // Capture the dimmed scene: otherwise the blur brings the bright background back inside the menu.
    const auto glass = glassPanel(shape(panel, 18));
    glassFill(glass, color(0.035f, 0.035f, 0.035f));
    if (panel_borders_) {
        brush(color(1, 1, 1, 0.16f));
        target_->DrawRoundedRectangle(shape(panel, 18), solid_.Get(), 1.0f);
    }
    textGroup(title, {}, panel.left + 28, panel.top + 28, panel.right - panel.left - 56, category_font_.Get(),
              color(0.95f, 0.95f, 0.95f), settings_body_font_.Get(), color(0.62f, 0.62f, 0.62f), 1, true);
    if (count > 0) {
        // The highlight glides between rows (ViewContext::picker_glide, a fractional row).
        const float position = context_->picker_glide.value_or(static_cast<float>(selected));
        auto r = layout.choice(0, count);
        const float offset = (layout.choice(count > 1 ? 1 : 0, count).top - r.top) * position;
        r.top += offset; r.bottom += offset;
        brush(color(1, 1, 1, 0.13f)); // A pill, no outline (the choice pickers of Settings).
        target_->FillRoundedRectangle(shape(r, (r.bottom - r.top) * 0.5f), solid_.Get());
    }
    for (int i = 0; i < count; ++i) {
        const auto r = layout.choice(i, count);
        textLine(labels[i], r.left + 18, (r.top + r.bottom) * 0.5f, r.right - r.left - 36, menu_font_.Get(), color(0.91f, 0.91f, 0.91f));
    }
}

// Zasilanie, as the large game menu (menu_dialog_renderer.cpp): a wide glass panel, its icon and
// title in the header, tall rows with an icon each in three groups with a band between them; the
// white button glides between the rows; the PC's own power actions in red.
void Renderer::drawPowerMenu() {
    const auto& labels = context_->power_labels;
    const int count = static_cast<int>(labels.size());
    power_menu::Geometry layout{viewport_.width, viewport_.height, context_->power_gaps, context_->power_wide ? 150.0f : 0.0f};
    if (static_cast<int>(layout.gaps.size()) != count) layout.gaps.assign(static_cast<size_t>(count), false);
    const auto p = layout.panel();
    const auto panel = D2D1::RectF(p.left, p.top, p.right, p.bottom);
    const auto danger = [&](int i) { return i >= 0 && i < static_cast<int>(context_->power_danger.size()) && context_->power_danger[static_cast<size_t>(i)]; };
    const auto rect = [](settings_ui::Rect r) { return D2D1::RectF(r.left, r.top, r.right, r.bottom); };
    brush(color(0, 0, 0, 0.62f));
    target_->FillRectangle(D2D1::RectF(0, 0, viewport_.width, viewport_.height), solid_.Get());
    glassFill(glassPanel(D2D1::RoundedRect(panel, 26, 26)), color(0.022f, 0.024f, 0.027f, 0.95f));
    if (panel_borders_) {
        brush(color(0.90f, 0.90f, 0.90f, 0.5f));
        target_->DrawRoundedRectangle(D2D1::RoundedRect(panel, 26, 26), solid_.Get(), 1.4f);
    }
    // Header: the power sign in a disc, the title and what it is for.
    const float header_left = panel.left + power_menu::Geometry::padding;
    {
        const float cx = header_left + 40, cy = panel.top + 66;
        brush(color(1, 1, 1, 0.08f)); target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), 40, 40), solid_.Get());
        desktopGlyph(context_->power_header_icon ? context_->power_header_icon : power_menu::icon(power_menu::Item::shutdown), cx, cy, 34, color(0.95f, 0.95f, 0.95f));
        const float title_left = header_left + 104;
        textLine(context_->power_title.empty() ? std::wstring(tr(L"Zasilanie")) : context_->power_title, title_left, panel.top + 52, panel.right - title_left - 30, psfive_name_font_.Get(), color(0.97f, 0.97f, 0.97f));
        textLine(context_->power_subtitle.empty() ? std::wstring(tr(L"Launcher i komputer")) : context_->power_subtitle, title_left, panel.top + 81, panel.right - title_left - 30, settings_body_font_.Get(), color(0.6f, 0.6f, 0.6f));
    }
    // A hairline in the middle of each band separates the groups.
    for (int i = 1; i < count; ++i) {
        if (!layout.gaps[static_cast<size_t>(i)]) continue;
        const float y = layout.row(i).top - (layout.rowStep() - layout.rowHeight() + power_menu::Geometry::band) * 0.5f;
        line(panel.left + 36, y, panel.right - 36, y, color(1, 1, 1, 0.08f), 1);
    }
    if (count <= 0) return;
    // The white button glides between rows (ViewContext::picker_glide, a fractional row); the text
    // darkens as it reaches its row.
    const float glide = std::clamp(context_->picker_glide.value_or(static_cast<float>(context_->power_menu)), 0.0f, static_cast<float>(count - 1));
    const auto dark = drawButton(rect(layout.rowAt(glide)), 1.0f, 1.0f, false);
    for (int i = 0; i < count; ++i) {
        const auto box = rect(layout.row(i));
        const float focus = std::clamp(1.0f - std::abs(static_cast<float>(i) - glide), 0.0f, 1.0f);
        if (focus < 0.999f) { // The others: a faint glass row, fading where the button passes.
            brush(color(1, 1, 1, 0.045f * (1.0f - focus)));
            target_->FillRoundedRectangle(pill(box), solid_.Get());
        }
        const auto light = danger(i) ? palette::danger() : color(0.95f, 0.95f, 0.95f);
        const auto tone = color(std::lerp(light.r, dark.r, focus), std::lerp(light.g, dark.g, focus), std::lerp(light.b, dark.b, focus), std::lerp(light.a, dark.a, focus));
        const float cy = (box.top + box.bottom) * 0.5f, radius = std::min(23.0f, (box.bottom - box.top) * 0.5f - 6);
        const float cx = box.left + 21 + radius;
        brush(focus > 0.5f ? color(0, 0, 0, 0.08f) : color(1, 1, 1, 0.07f));
        target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), radius, radius), solid_.Get());
        if (i < static_cast<int>(context_->power_icons.size())) desktopGlyph(context_->power_icons[static_cast<size_t>(i)], cx, cy, radius * 0.87f, tone);
        // The widget options: a switch on the right, or the value between two arrows.
        const bool has_switch = i < static_cast<int>(context_->power_switches.size()) && context_->power_switches[static_cast<size_t>(i)] >= 0;
        const std::wstring value = i < static_cast<int>(context_->power_values.size()) ? context_->power_values[static_cast<size_t>(i)] : std::wstring{};
        float reserve = 0;
        if (has_switch) {
            reserve = 84;
            drawRowSwitch(box.right - 28, cy, context_->power_switches[static_cast<size_t>(i)] == 1);
        } else if (!value.empty()) {
            const float width = textAdvance(value, category_font_.Get());
            const float right = box.right - 48;
            reserve = width + 96;
            textLine(value, right - width, cy, width + 2, category_font_.Get(), tone);
            const auto ink = color(std::lerp(0.78f, dark.r, focus), std::lerp(0.78f, dark.g, focus), std::lerp(0.78f, dark.b, focus), 0.9f);
            line(right - width - 20, cy - 7, right - width - 27, cy, ink, 2); line(right - width - 27, cy, right - width - 20, cy + 7, ink, 2);
            line(right + 18, cy - 7, right + 25, cy, ink, 2); line(right + 25, cy, right + 18, cy + 7, ink, 2);
        }
        textLine(labels[static_cast<size_t>(i)], cx + radius + 21, cy, box.right - cx - radius - 43 - reserve, category_font_.Get(), tone);
    }
    // Rows beyond the window: a small chevron at its edge.
    if (context_->power_more_above) { line(panel.right - 60, layout.row(0).top - 14, panel.right - 52, layout.row(0).top - 20, color(1, 1, 1, 0.5f), 2); line(panel.right - 52, layout.row(0).top - 20, panel.right - 44, layout.row(0).top - 14, color(1, 1, 1, 0.5f), 2); }
    if (context_->power_more_below) { const float y = layout.row(count - 1).bottom + 14; line(panel.right - 60, y, panel.right - 52, y + 6, color(1, 1, 1, 0.5f), 2); line(panel.right - 52, y + 6, panel.right - 44, y, color(1, 1, 1, 0.5f), 2); }
}

// A switch like Settings' (52 x 28), its right edge at `right`.
void Renderer::drawRowSwitch(float right, float center_y, bool on) {
    const auto accent = uiAccent();
    const auto off = color(0.23f, 0.23f, 0.23f, 1.0f);
    const float t = on ? 1.0f : 0.0f;
    brush(D2D1::ColorF(std::lerp(off.r, accent.r, t), std::lerp(off.g, accent.g, t), std::lerp(off.b, accent.b, t), 1.0f));
    target_->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(right - 52, center_y - 14, right, center_y + 14), 14, 14), solid_.Get());
    { const float knob = on ? 0.08f : 0.67f; brush(color(knob, knob, knob, 1.0f)); }
    target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(right - (on ? 14.0f : 38.0f), center_y), 10, 10), solid_.Get());
}

void Renderer::drawExitPrompt() {
    // Desktop mode: both as desktop dialogs (desktop_dialogs_renderer.cpp).
    if (desktopDialogs() && (context_->power_menu >= 0 || context_->exit_prompt >= 0)) { drawDesktopPrompts(); return; }
    if (context_->power_menu >= 0) drawPowerMenu();
    if (context_->exit_prompt < 0) return;
    const exit_prompt::Layout layout{viewport_.width, viewport_.height};
    const auto panel = layout.panel();
    const auto shape = [](settings_ui::Rect r, float radius) {
        return D2D1::RoundedRect(D2D1::RectF(r.left, r.top, r.right, r.bottom), radius, radius);
    };
    const auto glass = glassPanel(shape(panel, 18)); // The scene before the dim, frosted in the panel.
    brush(color(0, 0, 0, 0.78f));
    target_->FillRectangle(D2D1::RectF(0, 0, viewport_.width, viewport_.height), solid_.Get());
    glassFill(glass, color(0.07f, 0.07f, 0.07f));
    if (panel_borders_) {
        brush(color(1, 1, 1, 0.16f));
        target_->DrawRoundedRectangle(shape(panel, 18), solid_.Get(), 1.0f);
    }
    textLine(context_->prompt_question.empty() ? std::wstring(tr(exit_prompt::question)) : context_->prompt_question,
        panel.left + 24, panel.top + 42, panel.right - panel.left - 48,
        category_font_.Get(), color(0.95f, 0.95f, 0.95f), DWRITE_TEXT_ALIGNMENT_CENTER);
    // What the answer changes (Wyklucz grę: where the game goes), under the panel.
    if (!context_->prompt_body.empty())
        paragraph(context_->prompt_body, panel.left + 12, panel.bottom + 22, panel.right - panel.left - 24, prompt_body_font_.Get(), color(0.86f, 0.86f, 0.86f), 3);
    // PSfive's pills inside the glass panel; the white one glides to the chosen answer.
    const auto rect = [](settings_ui::Rect r) { return D2D1::RectF(r.left, r.top, r.right, r.bottom); };
    for (int i = 0; i < exit_prompt::count; ++i) drawButtonGlass(rect(layout.button(static_cast<float>(i))), 1, false);
    drawButtonFocus(rect(layout.button(context_->picker_glide.value_or(static_cast<float>(context_->exit_prompt)))));
    for (int i = 0; i < exit_prompt::count; ++i) {
        const auto r = layout.button(static_cast<float>(i));
        const bool focused = i == context_->exit_prompt;
        // A destructive first answer (Usuń kolekcję) in red, darker on the white focus pill.
        const auto ink = context_->prompt_danger && i == 0 ? (focused ? palette::dangerStrong() : palette::danger()) : buttonInk(focused ? 1.0f : 0.0f);
        textLine(context_->prompt_question.empty() ? std::wstring(tr(exit_prompt::labels[i])) : context_->prompt_labels[static_cast<size_t>(i)],
            r.left + 22, (r.top + r.bottom) * 0.5f, r.right - r.left - 44,
            settings_body_font_.Get(), ink, DWRITE_TEXT_ALIGNMENT_CENTER);
    }
}

void Renderer::drawContextMenu() {
    if (cards_->empty() || context_->menu_progress < 0.005f) return;
    const auto& active = (*cards_)[selected_];
    if (!isGame(active.game) && !context_->menu_rows.studio) return; // A studio's logo has its menu too.
    // Duże menu gry: the home screen's menu as the large dialog (menu_dialog_renderer.cpp).
    if (context_->menu_dialog) { drawContextMenuDialog(); return; }
    const float alpha = smoothFade(context_->menu_progress);
    const bool beside = context_->menu_beside;
    auto rows = context_->menu_rows; // As Launcher::menuRows built them (identify, the studio's rows, the emulator...).
    rows.uninstall = context_->menu_uninstall;
    rows.remove = context_->menu_remove;
    rows.launch = context_->menu_launch;
    rows.compat = context_->menu_compat;
    rows.shortcuts = context_->menu_shortcuts;
    rows.metadata = context_->menu_metadata;
    rows.aurelia = context_->menu_aurelia;
    rows.steam_account = !context_->menu_steam_account.empty();
    const auto bounds = context_menu::bounds(active, rows, beside, viewport_.width, viewport_.height, context_->menu_anchor);
    const float x = bounds.left, width = bounds.right - bounds.left;
    const float y = bounds.top + (1.0f - alpha) * (context_->menu_anchor && bounds.top > context_->menu_anchor->top ? -19.0f : 19.0f);
    const float height = context_menu::height(rows);
    D2D1_MATRIX_3X2_F menu_base; target_->GetTransform(&menu_base);
    if (context_->menu_anchor) {
        const auto& anchor = *context_->menu_anchor;
        const float sx = std::lerp((anchor.right - anchor.left) / width, 1.0f, alpha);
        const float sy = std::lerp((anchor.bottom - anchor.top) / height, 1.0f, alpha);
        const float left = std::lerp(anchor.left, x, alpha), top = std::lerp(anchor.top, y, alpha);
        target_->SetTransform(D2D1::Matrix3x2F::Scale(sx, sy, D2D1::Point2F(x, y)) *
            D2D1::Matrix3x2F::Translation(left - x, top - y) * menu_base);
    }
    // Frosted glass: the scene under the menu, blurred (it grows from its button with the transform).
    glassFill(glassPanel(D2D1::RoundedRect(D2D1::RectF(x, y, x + width, y + height), 23, 23)), color(0.022f, 0.024f, 0.027f, 0.93f), alpha);
    if (panel_borders_) {
        brush(color(0.90f, 0.90f, 0.90f, 0.63f * alpha));
        target_->DrawRoundedRectangle(D2D1::RoundedRect(
            D2D1::RectF(x, y, x + width, y + height), 23, 23), solid_.Get(), 1.4f);
    }
    const std::wstring account_label = tr(L"Konto Steam: ") + context_->menu_steam_account;
    const auto label = [&](context_menu::Item item) -> const wchar_t* {
        switch (item) {
        case context_menu::Item::primary: return context_->primary_action.c_str();
        case context_menu::Item::suspend: return tr(L"Wstrzymaj");
        case context_menu::Item::plugin: return L"";
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
        case context_menu::Item::steam_account: return account_label.c_str();
        case context_menu::Item::compat: return L"Proton / Wine";
        case context_menu::Item::shortcuts: return tr(L"Skróty");
        case context_menu::Item::identify: return rows.identified ? tr(L"Zidentyfikuj ponownie…") : tr(L"Zidentyfikuj grę…");
        case context_menu::Item::forget_recent: return tr(L"Usuń z ostatnio granych");
        case context_menu::Item::studio_earlier: return tr(L"Przesuń wcześniej");
        case context_menu::Item::studio_later: return tr(L"Przesuń dalej");
        case context_menu::Item::studio_hide: return tr(L"Ukryj");
        case context_menu::Item::emulator: return context_->menu_emulator.c_str();
        case context_menu::Item::more: return tr(L"Więcej opcji");
        }
        return L"";
    };
    // The highlight glides between rows (ViewContext::menu_glide, a fractional row).
    const float highlight_y = y + 8 + context_->menu_glide.value_or(static_cast<float>(context_->menu_selected)) * 53.0f;
    brush(color(0.86f, 0.86f, 0.86f, 0.14f * alpha));
    target_->FillRoundedRectangle(pill(D2D1::RectF(x + 12, highlight_y, x + width - 12, highlight_y + 48)), solid_.Get());
    for (int row = 0; row < context_menu::rows(rows); ++row) {
        const float row_y = y + 8 + row * 53.0f;
        const auto entry = context_menu::item(row, rows);
        // "Zamknij grę" and "Odinstaluj" are destructive: a soft red, as used for save errors in Settings.
        const bool destructive = entry == context_menu::Item::close || entry == context_menu::Item::uninstall || entry == context_menu::Item::remove;
        text(entry == context_menu::Item::plugin ? context_->menu_plugins[row - context_menu::row(context_menu::Item::plugin, rows)].c_str() : label(entry), x + 32, row_y + 11, width - 50, 35,
             menu_font_.Get(), destructive ? palette::danger(alpha) :
             (entry != context_menu::Item::primary || context_->primary_enabled) ? color(0.96f, 0.96f, 0.96f, alpha) :
             color(0.53f, 0.53f, 0.53f, 0.75f * alpha));
    }
    if (context_->menu_anchor) target_->SetTransform(menu_base);
}
