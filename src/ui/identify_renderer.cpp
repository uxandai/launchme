#include "renderer.hpp"
#include "renderer_internal.hpp"

namespace {
using render_detail::box;
using palette::gray;
}
float Renderer::drawRating(float x, float y, float width, float alpha) {
    if (context_->rating_label.empty()) return 0;
    rating_link_ = {x, y, x + width, y + 30};
    const bool focused = context_->screen == ScreenMode::details && context_->detail_action == context_->detail_rating;
    // Focused like a button: the white pill with dark ink; otherwise an accent link.
    if (focused) drawButtonFocus(box(rating_link_), alpha);
    textLine(context_->rating_label + L"  ↗", x + 6, y + 15, width - 12, settings_body_font_.Get(), focused ? buttonInk(1, true, alpha) : uiAccent(alpha));
    return 36;
}
void Renderer::drawIdentify() {
    if (desktopDialogs()) { drawDesktopIdentify(); return; } // Desktop mode: desktop_dialogs_renderer.cpp.
    const auto& state = *context_->identify;
    const identify_ui::Layout layout{viewport_.width, viewport_.height};
    const auto panel = layout.panel();
    brush(gray(0, 0.82f)); target_->FillRectangle(D2D1::RectF(0, 0, viewport_.width, viewport_.height), solid_.Get());
    brush(gray(0.055f)); target_->FillRoundedRectangle(D2D1::RoundedRect(box(panel), 18, 18), solid_.Get());
    if (panel_borders_) { brush(gray(1, 0.18f)); target_->DrawRoundedRectangle(D2D1::RoundedRect(box(panel), 18, 18), solid_.Get()); }
    // The title, and under it what the game is on disk (its ROM, program or folder), cut at the start.
    const auto path = identify_ui::gamePath(state.game);
    const float title_y = path.empty() ? panel.top + 42 : panel.top + 34;
    textLine(tr(L"Zidentyfikuj grę"), panel.left + 28, title_y, panel.right - panel.left - 250, category_font_.Get(), gray(0.96f));
    if (!path.empty()) {
        const float path_width = layout.source().left - panel.left - 40;
        textLine(elideStart(path, path_width, toast_sub_font_.Get()), panel.left + 28, panel.top + 66, path_width, toast_sub_font_.Get(), gray(0.55f));
    }
    const auto source = layout.source();
    // Emulated games: a switch between RAWG and ScreenScraper (click, ←/→, F2); others link to RAWG.
    const std::wstring source_label = !state.emulated ? L"RAWG ↗" :
        state.source == identify_ui::State::Source::screenscraper ? L"ScreenScraper ⇄" : L"RAWG ⇄";
    // The switch is a glass pill (on the opaque panel, no frosting); its caption keeps clear of the round ends.
    const auto source_ink = state.emulated ? drawButton(box(source), 0, 1, false) : uiAccent();
    const float source_inset = state.emulated ? (source.bottom - source.top) * 0.5f - 6 : 10.0f;
    textLine(source_label, source.left + source_inset, (source.top + source.bottom) * 0.5f, source.right - source.left - 2 * source_inset, settings_body_font_.Get(), source_ink);
    // Back: a glass circle with a dot, white while it has the focus (Up from the search field).
    const auto back = layout.back();
    brush(drawButton(box(back), state.back_focus ? 1.0f : 0.0f, 1, false));
    target_->FillEllipse(D2D1::Ellipse({(back.left + back.right) * 0.5f, (back.top + back.bottom) * 0.5f}, 3, 3), solid_.Get());
    // A pill field; the accent outline only while it takes typing.
    const auto search = layout.search();
    brush(gray(1, state.query_focus && state.select_all ? 0.13f : 0.05f)); target_->FillRoundedRectangle(pill(box(search)), solid_.Get());
    if (state.query_focus) { brush(uiAccent()); target_->DrawRoundedRectangle(pill(box(search)), solid_.Get(), 1.5f); }
    textLine(state.query, search.left + 16, (search.top + search.bottom) * 0.5f, search.right - search.left - 84, menu_font_.Get(), gray(0.95f));
    drawSearchGlyph(search.right - 28, (search.top + search.bottom) * 0.5f, 8, gray(state.busy ? 0.3f : 0.9f), 1.8f);
    for (int i = layout.first(state); i < static_cast<int>(state.games.size()) && i < layout.first(state) + identify_ui::Layout::visible; ++i) {
        const auto row = layout.row(i, state); const auto& game = state.games[i];
        const bool focused = !state.query_focus && !state.back_focus && state.selected == i;
        brush(gray(1, focused ? 0.10f : 0.035f)); target_->FillRoundedRectangle(D2D1::RoundedRect(box(row), 16, 16), solid_.Get());
        auto subtitle = game.released;
        if (!subtitle.empty() && !game.platforms.empty()) subtitle += L" · ";
        subtitle += game.platforms;
        textGroup(game.name, subtitle, row.left + 16, (row.top + row.bottom) * 0.5f, row.right - row.left - 32,
                  menu_font_.Get(), gray(0.95f), settings_body_font_.Get(), gray(0.57f), 1);
    }
    if (!state.status.empty()) textLine(state.status, panel.left + 28, panel.bottom - 42, panel.right - panel.left - 56, settings_body_font_.Get(), gray(0.68f));
}
