#include "renderer.hpp"
#include "renderer_internal.hpp"

#include <chrono>
#include <cmath>

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
    if (state.manual) {
        // The provider: a glass pill that cycles Wszyscy dostawcy → RAWG → ScreenScraper (click, ←/→, F2).
        const auto source = layout.source();
        using Source = identify_ui::State::Source;
        const auto* provider = identify_ui::sourceName(state.source);
        const std::wstring source_label = (provider ? std::wstring(provider) : std::wstring(tr(L"Wszyscy dostawcy"))) + L" ⇄";
        const auto source_ink = drawButton(box(source), 0, 1, false);
        const float source_inset = (source.bottom - source.top) * 0.5f - 6;
        textLine(source_label, source.left + source_inset, (source.top + source.bottom) * 0.5f, source.right - source.left - 2 * source_inset, settings_body_font_.Get(), source_ink);
    }
    // Back: a glass circle with a dot, white while it has the focus (Up from the search field).
    const auto back = layout.back();
    brush(drawButton(box(back), state.back_focus ? 1.0f : 0.0f, 1, false));
    target_->FillEllipse(D2D1::Ellipse({(back.left + back.right) * 0.5f, (back.top + back.bottom) * 0.5f}, 3, 3), solid_.Get());
    if (!state.manual) {
        // The lookup: a spinner, what is happening, and the way to choose by hand.
        const float cx = (panel.left + panel.right) * 0.5f, cy = (panel.top + panel.bottom) * 0.5f - 30;
        drawSpinner(cx, cy, 26, uiAccent());
        if (!state.status.empty()) textLine(state.status, panel.left + 28, cy + 64, panel.right - panel.left - 56, menu_font_.Get(), gray(0.8f), DWRITE_TEXT_ALIGNMENT_CENTER);
        if (!state.applying) {
            const auto manual = layout.manualButton();
            const auto ink = drawButton(box(manual), 1.0f, 1.0f, false);
            textLine(tr(L"Dopasuj ręcznie"), manual.left + 12, (manual.top + manual.bottom) * 0.5f, manual.right - manual.left - 24, menu_font_.Get(), ink, DWRITE_TEXT_ALIGNMENT_CENTER);
        }
        return;
    }
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
    if (state.menu) {
        // The provider dropdown, over everything: the current one marked, the highlighted one filled.
        const auto list = identify_ui::providers(state.emulated);
        const auto box_rect = layout.menuPanel(static_cast<int>(list.size()));
        brush(gray(0.1f)); target_->FillRoundedRectangle(D2D1::RoundedRect(box(box_rect), 14, 14), solid_.Get());
        brush(gray(1, 0.18f)); target_->DrawRoundedRectangle(D2D1::RoundedRect(box(box_rect), 14, 14), solid_.Get());
        for (int i = 0; i < static_cast<int>(list.size()); ++i) {
            const auto row = layout.menuRow(i);
            if (state.menu_hover == i) { brush(gray(1, 0.12f)); target_->FillRoundedRectangle(D2D1::RoundedRect(box(row), 10, 10), solid_.Get()); }
            const auto* name = identify_ui::sourceName(list[static_cast<size_t>(i)]);
            textLine(name ? std::wstring(name) : std::wstring(tr(L"Wszyscy dostawcy")), row.left + 16, (row.top + row.bottom) * 0.5f, row.right - row.left - 48,
                     settings_body_font_.Get(), gray(0.95f));
            if (list[static_cast<size_t>(i)] == state.source) textLine(L"✓", row.right - 30, (row.top + row.bottom) * 0.5f, 20, settings_body_font_.Get(), uiAccent());
        }
    }
}

void Renderer::drawSpinner(float cx, float cy, float radius, D2D1_COLOR_F color) {
    using clock = std::chrono::steady_clock;
    skeleton_at_ = clock::now(); // animating() stays true while this is drawn: the next frame is asked for.
    const float turn = static_cast<float>(std::chrono::duration_cast<std::chrono::milliseconds>(clock::now().time_since_epoch()).count() % 1000) / 1000.0f;
    constexpr int dots = 10;
    constexpr float two_pi = 6.2831853f;
    for (int i = 0; i < dots; ++i) {
        const float angle = static_cast<float>(i) / dots * two_pi - two_pi * 0.25f;
        float behind = turn - static_cast<float>(i) / dots; // 0 at the head, up to 1 at its tail.
        if (behind < 0) behind += 1;
        const float fade = 1.0f - behind;
        brush(D2D1::ColorF(color.r, color.g, color.b, color.a * (0.12f + 0.88f * fade * fade)));
        target_->FillEllipse(D2D1::Ellipse({cx + std::cos(angle) * radius, cy + std::sin(angle) * radius}, radius * 0.15f + radius * 0.07f * fade, radius * 0.15f + radius * 0.07f * fade), solid_.Get());
    }
}
