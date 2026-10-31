#include "renderer.hpp"
#include "renderer_internal.hpp"
#include "grid_layout.hpp"
#include "../library/steam_stats.hpp"

#include <algorithm>
#include <cmath>

namespace {
using palette::gray;
using render_detail::box;
using render_detail::smoothFade;
using render_detail::upper;
std::wstring libraryStatus(const Game& game, SessionState session) {
    return session == SessionState::frozen ? tr(L"Wstrzymana") : session == SessionState::running ? tr(L"W grze") :
        game.installation.active() ? installationText(game.installation) : game.installed ? tr(L"Zainstalowane") :
        localFileGame(game.source) ? missingFileLabel(game) : tr(L"Do instalacji");
}
}

void Renderer::drawSearchGlyph(float cx, float cy, float radius, D2D1_COLOR_F ink, float stroke) {
    brush(ink);
    target_->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx - radius * 0.3f, cy - radius * 0.3f), radius, radius), solid_.Get(), stroke);
    const float reach = radius * 0.72f;
    line(cx + reach * 0.62f, cy + reach * 0.62f, cx + radius * 1.25f, cy + radius * 1.25f, ink, stroke * 1.15f);
}

// The library's top bar opacity: it fades out as the grid scrolls unless pinned or in use.
float Renderer::libraryBarAlpha() const {
    if (!context_->library_controls) return 1;
    const auto& state = *context_->library_controls;
    return state.focus >= 0 || state.open() || state.search || library_view::pinnedHeader(context_->library_mode) || context_->screen == ScreenMode::platform_games ?
           1.0f : std::clamp(1 - context_->grid_scroll / 165, 0.0f, 1.0f);
}

// Top bar of the library screens, right of the category tabs: the filters button (current order,
// and "Zainstalowane" while that filter is on), then the magnifier. Glass pills; one white pill
// glides between Back, the filters and the magnifier (ViewContext::library_bar_glide).
void Renderer::drawLibraryControls() {
    if (!context_->library_controls) return;
    const auto& state = *context_->library_controls;
    const library_ui::Layout layout{viewport_.width};
    const float alpha = libraryBarAlpha();
    const bool with_search = context_->screen == ScreenMode::library; // Not in a console's games.
    if (alpha >= 0.01f) {
        drawButtonGlass(box(layout.filters()), alpha);
        if (with_search) {
            drawButtonGlass(box(layout.search()), alpha);
            drawButtonGlass(box(layout.views()), alpha);
        }
        const bool bar_focus = state.focus >= 0 && !state.open();
        if (bar_focus) {
            auto r = box(layout.button(state.focus));
            if (context_->library_bar_glide) { const auto& g = *context_->library_bar_glide; r = D2D1::RectF(g.left, g.top, g.right, g.bottom); }
            drawButtonFocus(r, alpha);
        }
        const auto ink = [&](int button) { return buttonInk(bar_focus && state.focus == button ? 1.0f : 0.0f, true, alpha); };
        if (bar_focus) { // Back's dot over the pill (drawGrid drew its glass).
            brush(ink(library_ui::back_button));
            target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(34, 53), 3, 3), solid_.Get());
        }
        if (with_search) {
            const auto r = layout.search();
            // The magnifier takes the accent while its field is open.
            drawSearchGlyph((r.left + r.right) * 0.5f, (r.top + r.bottom) * 0.5f, 8.5f,
                            state.search && state.focus != library_ui::search_button ? uiAccent(alpha) : ink(library_ui::search_button), 2.2f);
            const auto v = layout.views();
            const float cx = (v.left + v.right) * 0.5f, cy = (v.top + v.bottom) * 0.5f;
            brush(state.panel == library_ui::Panel::views ? uiAccent(alpha) : ink(library_ui::views_button));
            for (int cell = 0; cell < 4; ++cell) {
                const float x = cx + (cell % 2 ? 2.0f : -10.0f), y = cy + (cell / 2 ? 2.0f : -10.0f);
                target_->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(x, y, x + 8, y + 8), 2, 2), solid_.Get(), 1.8f);
            }
        }
        const auto r = layout.filters();
        const auto filter_ink = ink(library_ui::filters_button);
        const float cy = (r.top + r.bottom) * 0.5f, inset = (r.bottom - r.top) * 0.5f - 6; // Content keeps clear of the rounded ends.
        // Funnel: three bars narrowing downwards.
        const float fx = r.left + inset + 8;
        line(fx - 9, cy - 6, fx + 9, cy - 6, filter_ink, 2.0f);
        line(fx - 5.5f, cy, fx + 5.5f, cy, filter_ink, 2.0f);
        line(fx - 2, cy + 6, fx + 2, cy + 6, filter_ink, 2.0f);
        std::wstring title = library_view::label(state.options.order);
        if (state.options.installed_only) title += std::wstring(L" · ") + tr(L"Zainstalowane");
        text(title, fx + 14, r.top, r.right - inset - 24 - (fx + 14), r.bottom - r.top, action_font_.Get(), filter_ink);
        const float chevron = r.right - inset - 6;
        line(chevron - 6, cy - 3, chevron, cy + 3, filter_ink, 1.5f);
        line(chevron, cy + 3, chevron + 6, cy - 3, filter_ink, 1.5f);
    }
    drawFilterMenu();
    drawViewDrawer();
}

// Sort orders and the installed filter, in the game context menu's style.
void Renderer::drawFilterMenu() {
    const float alpha = smoothFade(context_->library_menu_progress);
    if (alpha < 0.005f || context_->library_panel != library_ui::Panel::filters || !context_->library_controls) return;
    const auto& state = *context_->library_controls;
    const library_ui::Layout layout{viewport_.width};
    const float dy = (1.0f - alpha) * -12.0f;
    const auto panel = box(layout.menu(), dy);
    // Frosted like the game menu; dense enough that the platform badge beneath stays a blur.
    glassFill(glassPanel(D2D1::RoundedRect(panel, 23, 23)), D2D1::ColorF(0.022f, 0.024f, 0.027f, 0.93f), alpha);
    if (panel_borders_) {
        brush(gray(0.9f, 0.63f * alpha));
        target_->DrawRoundedRectangle(D2D1::RoundedRect(panel, 23, 23), solid_.Get(), 1.4f);
    }
    for (int i = 0; i < library_ui::filter_rows; ++i) {
        const auto r = box(layout.choice(i), dy);
        if (state.open() && state.choice == i) {
            brush(gray(0.86f, 0.14f * alpha));
            target_->FillRoundedRectangle(pill(r), solid_.Get());
        }
        const bool order = i < library_view::order_count;
        const bool active = order ? static_cast<int>(state.options.order) == i : state.options.installed_only;
        const std::wstring label = order ? library_view::label(static_cast<library_view::Order>(i)) : tr(L"Tylko zainstalowane");
        textLine(label, r.left + 20, (r.top + r.bottom) * 0.5f, r.right - r.left - 70, menu_font_.Get(), gray(0.96f, alpha));
        const float cx = r.right - 30, cy = (r.top + r.bottom) * 0.5f;
        if (active) {
            const auto ink = uiAccent(alpha);
            line(cx - 7, cy, cx - 2, cy + 5, ink, 2.4f);
            line(cx - 2, cy + 5, cx + 8, cy - 6, ink, 2.4f);
        } else if (!order) {
            brush(gray(1, 0.35f * alpha));
            target_->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), 8, 8), solid_.Get(), 1.4f);
        }
    }
}

// View drawer from the left edge (collection menu style).
void Renderer::drawViewDrawer() {
    const float progress = smoothFade(context_->library_views_progress);
    if (progress < 0.004f || context_->library_panel != library_ui::Panel::views || !context_->library_controls) return;
    const auto& state = *context_->library_controls;
    constexpr float width = library_ui::views::width;
    const float height = viewport_.height, x = library_ui::views::offset(context_->library_views_progress);
    const float unfold = library_ui::views::eased(context_->library_dropdown_progress);
    const bool close_focus = state.view_row == library_ui::views::closeRow(state.options.view);
    const auto shifted = [&](settings_ui::Rect r) { return D2D1::RectF(r.left + x, r.top, r.right + x, r.bottom); };
    const bool frosted = frostPane(D2D1::RectF(0, 0, x + width, height), progress);
    if (category_backdrop_) {
        category_backdrop_->SetStartPoint(D2D1::Point2F(x + width, 0));
        category_backdrop_->SetEndPoint(D2D1::Point2F(x + width + viewport_.width * drawer_style_.fade_fraction, 0));
        category_backdrop_->SetOpacity(progress * drawer_style_.opacity * (frosted ? 1.0f - 0.3f * glass_level_ : 1.0f));
        target_->FillRectangle(D2D1::RectF(0, 0, viewport_.width, height), category_backdrop_.Get());
    }
    brush(gray(0.022f, (frosted ? 0.62f : 0.92f) * progress));
    target_->FillRectangle(D2D1::RectF(0, 0, x + width, height), solid_.Get());
    const auto close = library_ui::views::close(height);
    const auto close_ink = drawButton(shifted(close), close_focus ? 1.0f : 0.0f, progress, true);
    textLine(tr(L"Zamknij"), close.left + x + 24, (close.top + close.bottom) * 0.5f, close.right - close.left - 48,
             action_font_.Get(), close_ink, DWRITE_TEXT_ALIGNMENT_CENTER);
    const auto selector = library_ui::views::selector();
    const bool selector_focus = state.view_row == 0 && !state.view_dropdown;
    brush(gray(1, 0.07f * progress));
    target_->FillRoundedRectangle(pill(shifted(selector)), solid_.Get());
    if (selector_focus) drawButtonFocus(shifted(selector), progress);
    const auto selector_ink = buttonInk(selector_focus ? 1.0f : 0.0f, true, progress);
    textLine(library_view::label(state.options.view), selector.left + x + 24, (selector.top + selector.bottom) * 0.5f,
             selector.right - selector.left - 76, category_font_.Get(), selector_ink);
    const float chevron_x = selector.right + x - 24, chevron_y = (selector.top + selector.bottom) * 0.5f;
    const float flip = 1 - 2 * unfold;
    line(chevron_x - 5, chevron_y - 2 * flip, chevron_x, chevron_y + 3 * flip, selector_ink, 1.7f);
    line(chevron_x, chevron_y + 3 * flip, chevron_x + 5, chevron_y - 2 * flip, selector_ink, 1.7f);
    const float options_alpha = progress * (1 - smoothFade(unfold * 4));
    if (options_alpha > 0.004f) {
        using library_view::View;
        if (!library_ui::views::optionCount(state.options.view))
            textLine(tr(L"Brak opcji widoku"), library_ui::views::padding + x, (selector.bottom + close.top) * 0.5f,
                width - 2 * library_ui::views::padding, settings_body_font_.Get(), palette::muted(options_alpha), DWRITE_TEXT_ALIGNMENT_CENTER);
        for (int i = 0; i < library_ui::views::optionCount(state.options.view); ++i) {
            auto r = library_ui::views::option(i);
            r.top += unfold * 12; r.bottom += unfold * 12;
            if (state.view_row == i + 1) {
                brush(gray(1, 0.09f * options_alpha)); target_->FillRoundedRectangle(D2D1::RoundedRect(shifted(r), 14, 14), solid_.Get());
            }
            const wchar_t* label = L""; std::wstring value;
            bool toggle = false, checked = false;
            switch (state.options.view) {
            case View::grid:
                label = tr(L"Okładki");
                value = state.options.grid_landscape ? tr(L"Poziome kafle") : state.options.grid_posters ? tr(L"Pionowe plakaty") : tr(L"Kwadratowe");
                break;
            case View::list:
                label = i == 0 ? tr(L"Rok") : i == 1 ? tr(L"Czas gry") : tr(L"Ocena"); toggle = true;
                checked = i == 0 ? state.options.list_year : i == 1 ? state.options.list_playtime : state.options.list_rating; break;
            case View::cinematic:
                if (i == 0) {
                    label = tr(L"Przewijanie");
                    value = state.options.cinematic_column ? tr(L"Jedna kolumna") : state.options.cinematic_horizontal ? tr(L"W bok") : tr(L"W dół");
                } else {
                    // Beside the year in the sideways and column synopses.
                    label = i == 1 ? tr(L"Platforma") : i == 2 ? tr(L"Ostatnio grane") : tr(L"Czas gry"); toggle = true;
                    checked = i == 1 ? state.options.cinematic_platform : i == 2 ? state.options.cinematic_played : state.options.cinematic_playtime;
                }
                break;
            case View::overview: label = tr(L"Liczba kolumn"); value = std::to_wstring(state.options.overview_columns); break;
            case View::coverflow: label = tr(L"Opakowanie"); value = state.options.coverflow_cd ? tr(L"Piracka płyta") : L"Steelbook"; break;
            case View::list_plus: label = tr(L"Efekt steelbooka"); toggle = true; checked = state.options.list_plus_steelbook; break;
            case View::bookshelf:
                label = i == 0 ? tr(L"Szare pudełka z boku") : tr(L"Platforma na grzbiecie"); toggle = true;
                checked = i == 0 ? state.options.bookshelf_gray : state.options.bookshelf_platform; break;
            default: break;
            }
            if (toggle) {
                textLine(label, r.left + x + 24, (r.top + r.bottom) * 0.5f, r.right - r.left - 98, menu_font_.Get(), palette::text(options_alpha));
                const float tx = r.right + x - 60, ty = (r.top + r.bottom) * 0.5f;
                brush(checked ? uiAccent(options_alpha * 0.7f) : gray(1, 0.12f * options_alpha));
                target_->FillRoundedRectangle(pill(D2D1::RectF(tx, ty - 11, tx + 36, ty + 11)), solid_.Get());
                brush(checked ? gray(0.06f, options_alpha) : gray(0.7f, options_alpha));
                target_->FillEllipse(D2D1::Ellipse({tx + (checked ? 25.0f : 11.0f), ty}, 7, 7), solid_.Get());
            } else {
                textLine(label, r.left + x + 24, r.top + 22, r.right - r.left - 48, friend_status_font_.Get(), palette::muted(options_alpha));
                if (state.options.view == View::overview) {
                    textLine(value, r.right + x - 54, r.top + 22, 30, menu_font_.Get(), palette::text(options_alpha), DWRITE_TEXT_ALIGNMENT_TRAILING);
                    const auto bar = library_ui::views::track();
                    const float knob = bar.left + (bar.right - bar.left) * (state.options.overview_columns - 2) / 3;
                    line(bar.left + x, bar.top + 2, bar.right + x, bar.top + 2, gray(1, 0.16f * options_alpha), 4);
                    line(bar.left + x, bar.top + 2, knob + x, bar.top + 2, uiAccent(options_alpha), 4);
                    brush(palette::text(options_alpha)); target_->FillEllipse(D2D1::Ellipse({knob + x, bar.top + 2}, 7, 7), solid_.Get());
                } else textLine(value + L"  ‹ ›", r.left + x + 24, r.top + 54, r.right - r.left - 48, menu_font_.Get(), palette::text(options_alpha));
            }
        }
    }
    if (unfold < 0.004f) return;
    const float top = library_ui::views::top(height), step = library_ui::views::rowHeight(height);
    const float dropdown_alpha = progress * unfold;
    target_->PushAxisAlignedClip(D2D1::RectF(x + library_ui::views::padding, top, x + width - library_ui::views::padding,
        library_ui::views::revealedBottom(height, context_->library_dropdown_progress)), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    const float choice = context_->library_choice_glide.value_or(static_cast<float>(state.choice));
    const float highlight = top + choice * step;
    if (!close_focus) {
        brush(gray(0.86f, 0.14f * dropdown_alpha));
        target_->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(x + library_ui::views::padding, highlight + 4,
            x + width - library_ui::views::padding, highlight + step - 4), 12, 12), solid_.Get());
    }
    for (int i = 0; i < library_view::view_count; ++i) {
        const auto row_view = library_view::drawerView(i); // Drawer order, not the saved numbers.
        const float y = top + i * step;
        const auto ink = gray(0.75f + (close_focus ? 0 : 0.22f * std::max(0.0f, 1 - std::abs(choice - i))), dropdown_alpha);
        // Glyph: a 2x2 grid or three list rows.
        const float gx = x + library_ui::views::padding + 32, gy = y + step * 0.5f;
        brush(ink);
        if (row_view == library_view::View::grid) {
            for (int cell = 0; cell < 4; ++cell) {
                const float cx = gx + (cell % 2 ? 2.0f : -11.0f), cy = gy + (cell / 2 ? 2.0f : -11.0f);
                target_->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(cx, cy, cx + 9, cy + 9), 2.5f, 2.5f), solid_.Get(), 1.8f);
            }
        } else if (row_view == library_view::View::cinematic) {
            target_->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(gx - 13, gy - 9, gx + 13, gy + 7), 3, 3), solid_.Get(), 1.8f);
            line(gx - 7, gy + 12, gx + 7, gy + 12, ink, 1.8f);
        } else if (row_view == library_view::View::list_plus) {
            target_->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(gx - 14, gy - 11, gx - 5, gy + 11), 2, 2), solid_.Get(), 1.6f);
            for (int bar = -1; bar <= 1; ++bar) line(gx - 1, gy + bar * 7.0f, gx + 5, gy + bar * 7.0f, ink, 1.6f);
            target_->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(gx + 9, gy - 11, gx + 16, gy - 3), 1.5f, 1.5f), solid_.Get(), 1.4f);
            line(gx + 9, gy + 4, gx + 16, gy + 4, ink, 1.4f);
        } else if (row_view == library_view::View::detailed) {
            target_->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(gx - 13, gy - 11, gx + 13, gy + 11), 3, 3), solid_.Get(), 1.8f);
            line(gx - 4, gy - 11, gx - 4, gy + 11, ink, 1.8f);
            line(gx + 1, gy + 3, gx + 8, gy + 3, ink, 1.5f);
            line(gx + 1, gy + 7, gx + 8, gy + 7, ink, 1.5f);
        } else if (row_view == library_view::View::floppy) {
            target_->DrawRectangle(D2D1::RectF(gx - 12, gy - 12, gx + 12, gy + 12), solid_.Get(), 1.8f);
            target_->DrawRectangle(D2D1::RectF(gx - 4, gy - 12, gx + 7, gy - 4), solid_.Get(), 1.5f);
            target_->DrawRectangle(D2D1::RectF(gx - 7, gy + 1, gx + 7, gy + 11), solid_.Get(), 1.5f);
        } else if (row_view == library_view::View::cartridge_switch) {
            target_->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(gx - 9, gy - 13, gx + 9, gy + 13), 3, 3), solid_.Get(), 1.8f);
            target_->DrawRectangle(D2D1::RectF(gx - 5, gy - 8, gx + 5, gy + 8), solid_.Get(), 1.5f);
        } else if (row_view == library_view::View::coverflow) {
            target_->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(gx - 14, gy - 12, gx, gy + 12), 2, 2), solid_.Get(), 1.8f);
            for (int edge = 0; edge < 3; ++edge) line(gx + 5 + edge * 5.0f, gy - 9 + edge, gx + 5 + edge * 5.0f, gy + 9 - edge, ink, 1.8f);
        } else if (row_view == library_view::View::bookshelf) {
            // Cover Flow: one cover facing front between two turned in perspective, over a floor line.
            target_->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(gx - 5, gy - 11, gx + 5, gy + 5), 1.5f, 1.5f), solid_.Get(), 1.6f);
            for (const float side : {-1.0f, 1.0f}) {
                const D2D1_POINT_2F quad[] = {{gx + side * 8, gy - 9}, {gx + side * 14, gy - 12}, {gx + side * 14, gy + 8}, {gx + side * 8, gy + 3}};
                for (int edge = 0; edge < 4; ++edge) line(quad[edge].x, quad[edge].y, quad[(edge + 1) % 4].x, quad[(edge + 1) % 4].y, ink, 1.4f);
            }
            line(gx - 4, gy + 9, gx + 4, gy + 9, gray(ink.r, ink.a * 0.45f), 1.4f);
        } else if (row_view == library_view::View::overview) {
            // A tall panel beside a grid of wide tiles.
            target_->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(gx - 14, gy - 12, gx - 5, gy + 12), 2, 2), solid_.Get(), 1.8f);
            for (int tile = 0; tile < 4; ++tile) {
                const float tx = gx - 1 + (tile % 2) * 8.0f, ty = gy - 11 + (tile / 2) * 12.0f;
                target_->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(tx, ty, tx + 6.5f, ty + 9), 1.5f, 1.5f), solid_.Get(), 1.5f);
            }
        } else {
            for (int bar = -1; bar <= 1; ++bar) {
                target_->FillRectangle(D2D1::RectF(gx - 11, gy + bar * 7.0f - 2, gx - 7, gy + bar * 7.0f + 2), solid_.Get());
                line(gx - 3, gy + bar * 7.0f, gx + 11, gy + bar * 7.0f, ink, 2.0f);
            }
        }
        textLine(library_view::label(row_view), x + library_ui::views::padding + 68, gy,
                 width - 2 * library_ui::views::padding - 92, category_font_.Get(), ink);
    }
    target_->PopAxisAlignedClip();
}

// List view: rows (cover thumbnail, title, state) on the left, the focused game beside them.
void Renderer::drawLibraryList() {
    const float width = viewport_.width, height = viewport_.height;
    const auto& cards = *cards_;
    const bool detailed = context_->library_mode == library_view::View::detailed;
    const bool plus = context_->library_mode == library_view::View::list_plus;
    const float left = library_layout::rowLeft(context_->library_mode, width, height);
    const float right = library_layout::rowRight(context_->library_mode, width, height);
    const list_layout::Columns columns{width, context_->library_options};
    if (!detailed && !plus && context_->library_search_progress < 0.005f) {
        const auto heading = [&](const wchar_t* title, settings_ui::Rect r) {
            textLine(upper(title), r.left, list_layout::list_top - 26, r.right - r.left, label_font_.Get(), palette::muted(), DWRITE_TEXT_ALIGNMENT_TRAILING);
        };
        if (context_->library_options.list_year) heading(tr(L"Rok"), columns.year());
        if (context_->library_options.list_playtime) heading(tr(L"Czas gry"), columns.playtime());
        if (context_->library_options.list_rating) heading(tr(L"Ocena"), columns.rating());
    }
    const float bottom = height - list_layout::bottom_margin;
    // A quiet surface keeps the browser legible even over very bright artwork.
    brush(gray(0.025f, 0.48f));
    target_->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(left - 12, list_layout::list_top - 12, right + 12, bottom + 8), 22, 22), solid_.Get());
    target_->PushAxisAlignedClip(D2D1::RectF(0, list_layout::list_top - 10, width, bottom), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    const bool row_focus = !context_->library_controls || (context_->library_controls->focus < 0 &&
        context_->library_controls->action < 0 && !context_->library_controls->open());
    if (selected_ < cards.size()) {
        const auto& card = cards[selected_];
        const float top = list_layout::rowTop(card.y);
        auto row = D2D1::RectF(left, top, right, top + list_layout::row_height);
        if (row_focus && context_->library_row_glide) {
            const auto& r = *context_->library_row_glide; row = D2D1::RectF(r.left, r.top, r.right, r.bottom);
        }
        // A lighter card, no outline (as the rows of every menu).
        brush(gray(1, (row_focus ? (plus ? 0.92f : 0.13f) : 0.06f) * card.opacity));
        target_->FillRoundedRectangle(row.bottom - row.top <= 64 ? pill(row) : D2D1::RoundedRect(row, 16, 16), solid_.Get());
        if (!plus) {
            // Keep the selected game identifiable while its actions or the top bar have focus.
            brush(uiAccent((row_focus ? 0.95f : 0.4f) * card.opacity));
            target_->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(left - 7, row.top + 22, left - 4, row.bottom - 22), 1.5f, 1.5f), solid_.Get());
        }
    }
    for (size_t i = 0; i < cards.size(); ++i) {
        const auto& card = cards[i];
        const float top = list_layout::rowTop(card.y);
        if (top > bottom || top + list_layout::row_height < list_layout::list_top - 10 || card.opacity < 0.01f) continue;
        const bool focused = i == selected_ && (!context_->library_controls || (context_->library_controls->focus < 0 &&
            context_->library_controls->action < 0 && !context_->library_controls->open()));
        if (plus) {
            const auto found = context_->library_rows.find(i);
            const std::wstring time = found != context_->library_rows.end() && !found->second.playtime.empty() ? found->second.playtime : L"—";
            const auto ink = focused ? gray(0.06f, card.opacity) : palette::text(card.opacity * 0.82f);
            textLine(card.game.name, left + 18, top + list_layout::row_height * 0.5f, right - left - 138, menu_font_.Get(), ink);
            textLine(time, right - 110, top + list_layout::row_height * 0.5f, 92, settings_body_font_.Get(),
                     focused ? gray(0.22f, card.opacity) : palette::muted(card.opacity), DWRITE_TEXT_ALIGNMENT_TRAILING);
            continue;
        }
        const auto thumb = D2D1::RectF(card.x, card.y, card.x + card.size, card.y + card.size);
        const auto shape = tileShape(thumb, tile_shape_);
        brush(D2D1::ColorF(0.13f, 0.17f, 0.2f, card.opacity));
        if (shape) target_->FillGeometry(shape.Get(), solid_.Get());
        if (shape && i < covers_.size()) fillImage(covers_[i], thumb, 9, card.opacity * card.cover_opacity * reveal(covers_[i]), sessionOf(card.game) == SessionState::frozen, shape.Get());
        const auto& game = card.game;
        const auto session = sessionOf(game);
        // Only what is happening to the game: no platform and no "Do instalacji".
        const std::wstring status = session == SessionState::frozen ? std::wstring(tr(L"Wstrzymana")) :
                                    session == SessionState::running ? std::wstring(tr(L"W grze")) :
                                    game.installation.active() ? installationText(game.installation) :
                                    !game.installed && localFileGame(game.source) ? std::wstring(missingFileLabel(game)) : std::wstring();
        const float text_left = card.x + card.size + 18;
        const float title_right = detailed ? right - 20 : columns.titleRight();
        // A name too long for its row fades out on the right; the focused one glides to its end and back.
        if (status.empty() && textAdvance(game.name, menu_font_.Get()) > title_right - text_left)
            fadingLine(game.name, text_left, card.y + card.size * 0.5f, title_right - text_left, menu_font_.Get(),
                       gray(focused ? 0.98f : 0.86f, card.opacity), focused && i == selected_);
        else textGroup(game.name, status, text_left, card.y + card.size * 0.5f, title_right - text_left, menu_font_.Get(),
                       gray(focused ? 0.98f : 0.86f, card.opacity), settings_body_font_.Get(), gray(focused ? 0.66f : 0.5f, card.opacity), 1);
        if (!detailed) {
            const auto found = context_->library_rows.find(i);
            const auto* facts = found == context_->library_rows.end() ? nullptr : &found->second;
            const auto value = [&](const std::wstring& text, settings_ui::Rect r, bool rating = false) {
                textLine(text.empty() ? L"—" : text, r.left, top + list_layout::row_height * 0.5f, r.right - r.left,
                         settings_body_font_.Get(), text.empty() ? gray(0.35f, card.opacity) : rating ? uiAccent(card.opacity) : gray(focused ? 0.88f : 0.65f, card.opacity),
                         DWRITE_TEXT_ALIGNMENT_TRAILING);
            };
            if (context_->library_options.list_year) value(facts ? facts->year : L"", columns.year());
            if (context_->library_options.list_playtime) value(facts ? facts->playtime : L"", columns.playtime());
            if (context_->library_options.list_rating) value(facts ? facts->rating : L"", columns.rating(), true);
        }
    }
    target_->PopAxisAlignedClip();
    if (plus) drawLibraryListPlus(); else if (detailed) drawLibraryDetailed(); else drawLibraryPreview();
}

void Renderer::drawLibraryListPlus() {
    if (selected_ >= cards_->size()) return;
    const list_plus_layout::Layout layout{viewport_.width, viewport_.height};
    const auto& game = (*cards_)[selected_].game;
    const auto* info = context_->preview;
    const float fade = smoothFade(detail_), left = layout.infoLeft(), width = layout.infoWidth();
    const auto hero = box(layout.hero());
    brush(gray(0.05f));
    target_->FillRoundedRectangle(D2D1::RoundedRect(hero, 12, 12), solid_.Get());
    auto* image = selected_ < heroes_.size() && heroes_[selected_].bitmap ? &heroes_[selected_] :
        selected_ < covers_.size() ? &covers_[selected_] : nullptr;
    if (image) fillImage(*image, hero, 12, fade * reveal(*image), false);
    // The game's video (Filmy w tle) takes the picture's place once it plays.
    if (videoShown()) {
        Microsoft::WRL::ComPtr<ID2D1RoundedRectangleGeometry> rounded;
        if (SUCCEEDED(factory_->CreateRoundedRectangleGeometry(D2D1::RoundedRect(hero, 12, 12), rounded.GetAddressOf()))) drawVideoIn(hero, rounded.Get(), fade);
    }
    float y = hero.bottom + 20;
    y += paragraph(game.name, left, y, width, category_font_.Get(), palette::text(fade), 2) + 12;
    std::wstring facts;
    const auto add = [&](const std::wstring& value) { if (!value.empty()) facts += (facts.empty() ? L"" : L"   ·   ") + value; };
    if (info) {
        add(gameReleaseYear(info->release_date));
        if (info->playtime_minutes) add(std::wstring(tr(L"Czas gry: ")) + formatPlaytime(*info->playtime_minutes));
    }
    if (!facts.empty()) { textLine(facts, left, y + 8, width, settings_body_font_.Get(), gray(0.8f, fade)); y += 28; }
    const float rating = drawRating(left - 6, y, width + 6, fade);
    y += rating;
    if (!rating && info && !info->score.empty()) {
        textLine(std::wstring(tr(L"Ocena")) + L": " + info->score, left, y + 8, width, settings_body_font_.Get(), uiAccent(fade)); y += 28;
    }
    if (info && info->achievements_total) {
        textLine(std::wstring(tr(L"Osiągnięcia: ")) + std::to_wstring(info->achievements_unlocked) + L" / " + std::to_wstring(info->achievements_total),
                 left, y + 8, width, settings_body_font_.Get(), palette::muted(fade));
        const float share = std::clamp(static_cast<float>(info->achievements_unlocked) / info->achievements_total, 0.0f, 1.0f);
        brush(gray(1, 0.12f * fade));
        target_->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(left, y + 26, left + width, y + 30), 2, 2), solid_.Get());
        if (share > 0) {
            brush(uiAccent(fade));
            target_->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(left, y + 26, left + width * share, y + 30), 2, 2), solid_.Get());
        }
        y += 46;
    }
    if (info && !info->genres.empty()) {
        textLine(info->genres, left, y + 8, width, settings_body_font_.Get(), palette::muted(fade)); y += 30;
    }
    const auto description = info && !info->description.empty() ? info->description : std::wstring(tr(L"Opis gry nie jest dostępny."));
    const int lines = static_cast<int>((layout.bottom() - y) / typeMetrics(details_body_font_.Get()).line);
    if (!info && context_->preview_waiting) drawSkeleton(D2D1::RectF(left, y, left + width, layout.bottom()), std::min(lines, 5), typeMetrics(details_body_font_.Get()).line, fade);
    else paragraph(description, left, y, width, details_body_font_.Get(), gray(0.72f, fade), lines);
    drawListPlusPoster(); // Launch zoom and dimming stay anchored to the large poster.
}

// The list's compact game summary: cover and title, then separate progress and description cards.
void Renderer::drawLibraryPreview() {
    if (selected_ >= cards_->size()) return;
    const auto& card = (*cards_)[selected_];
    const auto& game = card.game;
    const float width = viewport_.width, height = viewport_.height;
    const float left = list_layout::panelLeft(width), right = list_layout::panelRight(width);
    if (right - left < 240) return;
    const float fade = smoothFade(detail_);
    const float size = std::min(240.0f, (right - left) * 0.32f), top = list_layout::list_top;
    // The cover is drawn last: while a game launches everything else dims beneath it.
    const auto drawCover = [&] {
        const float launch = smoothFade(context_->launch_progress);
        if (launch > 0.001f) {
            brush(D2D1::ColorF(0.008f, 0.012f, 0.016f, launch * 0.82f));
            target_->FillRectangle(D2D1::RectF(0, 0, width, height), solid_.Get());
        }
        const float grow = size * (0.08f * launch - 0.035f * (1 - fade)), lift = 16 * (1 - fade);
        const auto cover = D2D1::RectF(left - grow, top - grow + lift, left + size + grow, top + size + grow + lift);
        const auto shape = tileShape(cover, tile_shape_);
        if (!shape) return;
        brush(D2D1::ColorF(0.13f, 0.17f, 0.2f));
        target_->FillGeometry(shape.Get(), solid_.Get());
        if (selected_ < covers_.size()) fillImage(covers_[selected_], cover, 22, fade * card.cover_opacity * reveal(covers_[selected_]), false, shape.Get());
        brush(launch > 0.001f ? uiAccent(launch) : gray(1, 0.18f));
        target_->DrawGeometry(shape.Get(), solid_.Get(), launch > 0.001f ? 3.0f : 1.0f);
    };
    const float info_left = left + size + 28, info_width = right - info_left;
    const auto* details = context_->preview;
    float facts_bottom = top;
    if (info_width > 120) {
        const float title_height = paragraph(game.name, info_left, top - 6, info_width, settings_heading_font_.Get(), palette::text(fade), 2);
        float y = top + title_height + 12;
        const auto session = sessionOf(game);
        textLine(libraryStatus(game, session), info_left, y + 8, info_width, friend_status_font_.Get(),
                 session != SessionState::none || game.installation.active() ? uiAccent(fade) : palette::muted(fade));
        y += 36;
        const auto fact = [&](const std::wstring& value) {
            if (value.empty()) return;
            text(value, info_left, y, info_width, 28, settings_body_font_.Get(), gray(0.66f, fade));
            y += 28;
        };
        if (details) {
            fact(details->genres); fact(details->developer); fact(details->release_date);
        } else if (context_->preview_waiting) { drawSkeleton(D2D1::RectF(info_left, y, info_left + std::min(info_width, 360.0f), y + 84), 3, 28, fade); y += 84; }
        y += drawRating(info_left, y + 4, info_width, fade);
        facts_bottom = y;
    }
    const float stats_top = std::max(top + size, facts_bottom) + 22;
    drawLibraryFacts({left, stats_top, right, stats_top + 96}, fade, false);
    drawLibraryDescription({left, stats_top + 112, right, height - list_layout::bottom_margin}, fade);
    drawCover();
}

// The same frosted information cards as the game's page, sized for an inline preview.
void Renderer::drawLibraryDescription(settings_ui::Rect rect, float alpha) {
    if (rect.bottom - rect.top < 90) return;
    glassFill(glassPanel(D2D1::RoundedRect(box(rect), 20, 20), 18), gray(0.04f, 0.78f), alpha);
    textLine(upper(tr(L"Opis")), rect.left + 24, rect.top + 30, rect.right - rect.left - 48,
             friend_section_font_.Get(), palette::muted(alpha));
    const auto* info = context_->preview;
    const auto description = info && !info->description.empty() ? info->description : std::wstring(tr(L"Opis gry nie jest dostępny."));
    const int lines = static_cast<int>((rect.bottom - rect.top - 76) / typeMetrics(details_body_font_.Get()).line);
    if (!info && context_->preview_waiting)
        drawSkeleton(D2D1::RectF(rect.left + 24, rect.top + 54, rect.right - 24, rect.bottom - 22), std::min(lines, 4), typeMetrics(details_body_font_.Get()).line, alpha);
    else paragraph(description, rect.left + 24, rect.top + 54, rect.right - rect.left - 48, details_body_font_.Get(), gray(0.82f, alpha), lines);
}

void Renderer::drawLibraryFacts(settings_ui::Rect rect, float alpha, bool metadata) {
    glassFill(glassPanel(D2D1::RoundedRect(box(rect), 20, 20), 18), gray(0.04f, 0.78f), alpha);
    const float left = rect.left + 24, width = rect.right - rect.left - 48;
    if (metadata) textLine(upper(tr(L"Informacje")), left, rect.top + 30, width, friend_section_font_.Get(), palette::muted(alpha));
    const auto* info = context_->preview;
    const bool achievements = info && info->achievements_total;
    const float stat_width = achievements ? (width - 24) * 0.5f : width;
    const float top = rect.top + (metadata ? 66 : 24);
    const auto stat = [&](const wchar_t* label, const std::wstring& value, float x) {
        textLine(upper(label), x, top, stat_width, friend_section_font_.Get(), palette::muted(alpha));
        textLine(value, x, top + 28, stat_width, category_font_.Get(), palette::text(alpha));
    };
    stat(tr(L"Czas gry"), info && info->playtime_minutes ? formatPlaytime(*info->playtime_minutes) : L"—", left);
    if (achievements) {
        const float x = left + stat_width + 24;
        stat(tr(L"Osiągnięcia"), std::to_wstring(info->achievements_unlocked) + L" / " + std::to_wstring(info->achievements_total), x);
        const float share = std::clamp(static_cast<float>(info->achievements_unlocked) / info->achievements_total, 0.0f, 1.0f);
        const auto bar = D2D1::RectF(x, top + 49, x + stat_width, top + 53);
        brush(gray(1, 0.12f * alpha)); target_->FillRoundedRectangle(D2D1::RoundedRect(bar, 2, 2), solid_.Get());
        if (share > 0) {
            brush(uiAccent(alpha));
            target_->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(x, bar.top, x + stat_width * share, bar.bottom), 2, 2), solid_.Get());
        }
    }
    if (!metadata || !info) return;
    std::vector<std::pair<const wchar_t*, std::wstring>> rows;
    for (const auto& row : {std::pair{tr(L"Developer"), info->developer}, {tr(L"Wydawca"), info->publisher},
                           {tr(L"Premiera"), info->release_date}, {tr(L"Gatunek"), info->genres},
                           {tr(L"Gracze"), info->players}, {tr(L"Ocena"), info->score}})
        if (!row.second.empty()) rows.push_back(row);
    if (rows.empty()) return;
    const float start = top + 90, step = std::min(34.0f, (rect.bottom - 20 - start) / rows.size());
    const float label_width = std::min(96.0f, width * 0.3f);
    for (size_t i = 0; i < rows.size(); ++i) {
        const float y = start + step * (static_cast<float>(i) + 0.5f);
        textLine(rows[i].first, left, y, label_width, settings_body_font_.Get(), palette::muted(alpha));
        textLine(rows[i].second, left + label_width + 12, y, width - label_width - 12, settings_body_font_.Get(), palette::text(alpha));
    }
}

void Renderer::drawLibraryLogo(D2D1_RECT_F rect, float alpha) {
    if (selected_ >= cards_->size()) return;
    if (selected_ < logos_.size() && logos_[selected_].bitmap) {
        const auto& logo = logos_[selected_];
        const auto pixels = logo.bitmap->GetPixelSize();
        const auto source = logo.has_ink_bounds ? logo.ink_bounds : D2D1::RectF(0, 0, static_cast<float>(pixels.width), static_cast<float>(pixels.height));
        const float w = source.right - source.left, h = source.bottom - source.top;
        if (w <= 0 || h <= 0) return;
        const float scale = std::min((rect.right - rect.left) / w, (rect.bottom - rect.top) / h);
        target_->DrawBitmap(logo.bitmap.Get(), D2D1::RectF(rect.left, rect.bottom - h * scale, rect.left + w * scale, rect.bottom),
            alpha * reveal(logo), D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, &source);
    } else if (!context_->logo_pending && (selected_ >= logos_.size() || !logos_[selected_].decoding)) {
        settings_heading_font_->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
        text((*cards_)[selected_].game.name, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top,
             settings_heading_font_.Get(), gray(0.97f, alpha));
        settings_heading_font_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    }
}

void Renderer::drawLibraryActions() {
    if (!context_->library_controls || !library_view::actions(context_->library_mode) || selected_ >= cards_->size()) return;
    const library_presentation::Layout layout{viewport_.width, viewport_.height};
    const auto& state = *context_->library_controls;
    // PSfive's pair: glass pills, the white one gliding to the focused button.
    const bool on_actions = state.action >= 0 && !state.open() && state.focus < 0;
    for (int i = 0; i < library_presentation::action_count; ++i)
        drawButtonGlass(box(layout.action(i)), 1, true, false, i != 0 || context_->primary_enabled);
    if (on_actions) {
        auto r = box(layout.action(state.action));
        if (context_->library_action_glide) { const auto& g = *context_->library_action_glide; r = D2D1::RectF(g.left, g.top, g.right, g.bottom); }
        drawButtonFocus(r);
    }
    for (int i = 0; i < library_presentation::action_count; ++i) {
        const auto r = layout.action(i);
        const auto ink = buttonInk(on_actions && state.action == i ? 1.0f : 0.0f, i != 0 || context_->primary_enabled);
        if (i == 0) {
            const bool focused = on_actions && state.action == 0;
            if (!drawInstallProgress((*cards_)[selected_].game, box(r), ink, focused ? gray(0.05f, 0.15f) : gray(1, 0.2f), settings_body_font_.Get()))
                text(context_->primary_action, r.left + 20, r.top, r.right - r.left - 40, r.bottom - r.top, action_font_.Get(), ink);
        } else {
            brush(ink);
            for (int dot = -1; dot <= 1; ++dot)
                target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F((r.left + r.right) * 0.5f + dot * 8.5f, (r.top + r.bottom) * 0.5f), 2.7f, 2.7f), solid_.Get());
        }
    }
}

// Kinowy's line of facts: the platform's mark, the year, two genres, last played and playtime (the
// mark and the last two are view options). False: nothing to show.
bool Renderer::drawCinematicFacts(float x, float center_y, float width, float alpha) {
    float left = x;
    if (!context_->cinematic_platform.empty()) {
        const float mark = drawPlatformMark(context_->cinematic_platform, left, center_y, 24, 120, alpha, menu_font_.Get());
        if (mark > 0) left += mark + 20;
    }
    std::wstring facts;
    const auto add = [&](const std::wstring& part) { if (!part.empty()) facts += (facts.empty() ? L"" : L"   ·   ") + part; };
    if (const auto* info = context_->preview) {
        add(gameReleaseYear(info->release_date));
        if (!info->genres.empty()) {
            const auto first = info->genres.find(L',');
            const auto second = first == std::wstring::npos ? first : info->genres.find(L',', first + 1);
            add(info->genres.substr(0, second));
        }
    }
    add(context_->cinematic_played);
    add(context_->cinematic_playtime);
    if (!facts.empty()) textLine(facts, left, center_y, x + width - left, menu_font_.Get(), gray(0.82f, alpha));
    return !facts.empty() || left > x;
}

void Renderer::drawLibraryCinematic() {
    const float width = viewport_.width, height = viewport_.height;
    const float fade = smoothFade(detail_);
    const bool column = context_->library_options.cinematic_column;
    const float list_top = column ? cinematic_column::list_top :
                           context_->library_options.cinematic_horizontal ? cinematic_row::top(width, height) : cinematic_layout::list_top;
    if (column) {
        // One column of capsules on the right; the game's logo, facts, description and details on the left.
        const auto area = cinematic_column::summary(width, height);
        const float w = area.right - area.left, lift = 8 * (1 - fade);
        const auto logo = D2D1::RectF(area.left, area.top + 24 + lift, area.left + std::min(w, 620.0f), area.top + 174 + lift);
        if (selected_ < logos_.size() && logos_[selected_].bitmap) drawLibraryLogo(logo, fade);
        else {
            const auto& title = (*cards_)[selected_].game.name;
            const float h = paragraph(title, logo.left, 0, logo.right - logo.left, hero_font_.Get(), palette::text(fade), 2, false);
            paragraph(title, logo.left, logo.bottom - h, logo.right - logo.left, hero_font_.Get(), palette::text(fade), 2);
        }
        float y = logo.bottom + 30;
        if (drawCinematicFacts(area.left, y + 12, w, fade)) y += 46;
        if (const auto* info = context_->preview) {
            std::vector<std::pair<const wchar_t*, std::wstring>> rows;
            for (const auto& row : {std::pair{tr(L"Developer"), info->developer}, {tr(L"Wydawca"), info->publisher},
                                   {tr(L"Premiera"), info->release_date}, {tr(L"Gatunek"), info->genres}})
                if (!row.second.empty()) rows.push_back(row);
            constexpr float row_step = 34;
            const float rows_top = area.bottom - rows.size() * row_step;
            const float line = typeMetrics(details_body_font_.Get()).line;
            const int lines = line > 0 ? std::clamp(static_cast<int>((rows_top - 24 - y) / line), 0, 9) : 0;
            if (lines > 0) paragraph(info->description, area.left, y, w, details_body_font_.Get(), gray(0.86f, fade), lines);
            const float label_width = std::min(120.0f, w * 0.3f);
            for (size_t i = 0; i < rows.size(); ++i) {
                const float row_y = rows_top + row_step * (static_cast<float>(i) + 0.5f);
                textLine(rows[i].first, area.left, row_y, label_width, settings_body_font_.Get(), palette::muted(fade));
                textLine(rows[i].second, area.left + label_width + 12, row_y, w - label_width - 12, settings_body_font_.Get(), palette::text(fade));
            }
        }
    } else if (context_->library_options.cinematic_horizontal) {
        // A short, left-aligned game synopsis leaves the right side to the artwork, like a streaming-service spotlight.
        const auto area = cinematic_row::summary(width, height);
        const float w = area.right - area.left, lift = 8 * (1 - fade);
        if (selected_ < logos_.size() && logos_[selected_].bitmap)
            drawLibraryLogo(D2D1::RectF(area.left, 168 + lift, area.right, 308 + lift), fade);
        else {
            const auto& title = (*cards_)[selected_].game.name;
            const float h = paragraph(title, area.left, 0, w, hero_font_.Get(), palette::text(fade), 2, false);
            paragraph(title, area.left, 308 - h + lift, w, hero_font_.Get(), palette::text(fade), 2);
        }
        float y = area.top + lift;
        if (drawCinematicFacts(area.left, y + 12, w, fade)) y += 40;
        if (const auto* info = context_->preview) {
            const int lines = std::clamp(static_cast<int>((area.bottom - y) / typeMetrics(details_body_font_.Get()).line), 0, 4);
            paragraph(info->description, area.left, y, w, details_body_font_.Get(), gray(0.85f, fade), lines);
        }
    } else drawLibraryLogo(D2D1::RectF(68, 168, std::min(width - 68, 900.0f), 308), fade);
    // Capsules scroll only below the spotlight (beside the synopsis in one column); their obscured
    // portions cannot be clicked.
    const float clip_left = column ? cinematic_column::x(width) - 24 : 0;
    target_->PushAxisAlignedClip(D2D1::RectF(clip_left, list_top - 12, width, height), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    const float previous_corner = squircle_corner_;
    squircle_corner_ = 0.4f; // The compact corners shared by capsules and posters.
    const auto rowAlpha = [&](const GameCard& card) {
        const float top = smoothFade((card.y + card.size - list_top - 12) / 65);
        return column ? top * smoothFade((height - 12 - card.y) / 90) : top;
    };
    for (size_t i = 0; i < cards_->size(); ++i) if (i != selected_) drawRectangularCard((*cards_)[i], i, rowAlpha((*cards_)[i]));
    const float launch = smoothFade(context_->launch_progress);
    if (launch > 0.001f) {
        brush(D2D1::ColorF(0.008f, 0.012f, 0.016f, launch * 0.82f));
        target_->FillRectangle(D2D1::RectF(0, list_top - 12, width, height), solid_.Get());
    }
    drawRectangularCard((*cards_)[selected_], selected_, rowAlpha((*cards_)[selected_]));
    squircle_corner_ = previous_corner;
    for (size_t i = 0; i < cards_->size() && !column; ++i) { // One column: the name is in the synopsis.
        const auto& card = (*cards_)[i];
        if (card.y > height || card.y + card.size + 48 < list_top - 12) continue;
        text(card.game.name, card.x, card.y + card.size + 14, card.width(), 36, menu_font_.Get(), gray(i == selected_ ? 0.96f : 0.67f, card.opacity * rowAlpha(card)));
    }
    target_->PopAxisAlignedClip();
}

// Lista+: the fixed 2:3 poster on the left; with its option, Coverflow's steelbook finish (no disc).
void Renderer::drawListPlusPoster() {
    if (selected_ >= cards_->size()) return;
    const auto& card = (*cards_)[selected_];
    const auto area = list_plus_layout::Layout{viewport_.width, viewport_.height}.poster();
    const float launch = smoothFade(context_->launch_progress);
    if (launch > 0.001f) {
        brush(D2D1::ColorF(0.008f, 0.012f, 0.016f, launch * 0.82f));
        target_->FillRectangle(D2D1::RectF(0, 0, viewport_.width, viewport_.height), solid_.Get());
    }
    const float fade = smoothFade(detail_), lift = 18 * (1 - fade);
    const float grow = (area.bottom - area.top) * (0.025f * launch - 0.025f * (1 - fade));
    const auto rect = D2D1::RectF(area.left - grow * (2.0f / 3.0f), area.top - grow + lift,
                                area.right + grow * (2.0f / 3.0f), area.bottom + grow + lift);
    const float previous_corner = squircle_corner_;
    squircle_corner_ = 0.2f;
    const auto shape = tileShape(rect, tile_shape_);
    squircle_corner_ = previous_corner;
    if (!shape) return;
    brush(D2D1::ColorF(0.045f, 0.05f, 0.06f));
    target_->FillGeometry(shape.Get(), solid_.Get());
    auto* image = selected_ < posters_.size() && posters_[selected_].bitmap ? &posters_[selected_] :
                  selected_ < covers_.size() && covers_[selected_].bitmap ? &covers_[selected_] : nullptr;
    const bool frozen = sessionOf(card.game) == SessionState::frozen;
    if (image) fillPoster(*image, rect, 16, fade * reveal(*image), frozen, shape.Get());
    else text(card.game.name, area.left + 24, (area.top + area.bottom) * 0.5f - 40,
              area.right - area.left - 48, 80, menu_font_.Get(), gray(0.76f, fade));
    if (image && !frozen && context_->library_options.list_plus_steelbook) {
        // As in Coverflow: plain print, then the finish grows on every selection (card.steelbook).
        // The poster stands still, so the lamp's reflection slides in while the finish appears.
        const float finish = card.steelbook * card.steelbook * (3 - 2 * card.steelbook);
        drawSteelbook(*image, rect, shape.Get(), fade * reveal(*image) * finish, 1.1f * (1 - finish));
    }
    brush(launch > 0.001f ? uiAccent(launch) : gray(1, 0.16f));
    target_->DrawGeometry(shape.Get(), solid_.Get(), launch > 0.001f ? 2.5f : 1.0f);
}

void Renderer::drawLibraryAmbient(float opacity) {
    if (!cards_ || selected_ >= cards_->size()) return;
    auto* candidate = selected_ < heroes_.size() ? heroes_[selected_].bitmap.Get() : nullptr;
    if (!candidate && selected_ < posters_.size()) candidate = posters_[selected_].bitmap.Get();
    if (!candidate && library_view::media(context_->library_mode) && selected_ < covers_.size()) candidate = covers_[selected_].bitmap.Get();
    const float fade = stepCinematicFade(candidate);
    const CropFocus focus = selected_ < heroes_.size() && heroes_[selected_].bitmap.Get() == cinematic_hero_.Get() ? heroes_[selected_].focus : CropFocus{};
    drawWallpapers({cinematic_previous_hero_.Get(), opacity * (1 - fade)}, {cinematic_hero_.Get(), opacity * fade, focus});
}

void Renderer::drawLibraryDetailed() {
    if (selected_ >= cards_->size()) return;
    const library_presentation::Layout layout{viewport_.width, viewport_.height};
    const auto& game = (*cards_)[selected_].game;
    const float fade = smoothFade(detail_), left = layout.left(), width = layout.right() - left;
    const auto hero = box(layout.hero());
    const float previous_corner = squircle_corner_;
    squircle_corner_ = 0.2f; // The wide preview banner needs shallower corners than a cover.
    const auto shape = tileShape(hero, tile_shape_);
    squircle_corner_ = previous_corner;
    brush(gray(0.045f));
    if (shape) {
        target_->FillGeometry(shape.Get(), solid_.Get());
        auto* image = selected_ < heroes_.size() && heroes_[selected_].bitmap ? &heroes_[selected_] :
                       selected_ < covers_.size() ? &covers_[selected_] : nullptr;
        if (image) fillImage(*image, hero, 14, fade * reveal(*image), false, shape.Get());
        // Game-page shading: strong behind the logo and actions, clear artwork on the right.
        if (status_shade_) {
            status_shade_->SetStartPoint(D2D1::Point2F(hero.right, 0));
            status_shade_->SetEndPoint(D2D1::Point2F(hero.left, 0));
            status_shade_->SetOpacity(0.8f);
            target_->FillGeometry(shape.Get(), status_shade_.Get());
            status_shade_->SetStartPoint(D2D1::Point2F(0, hero.top));
            status_shade_->SetEndPoint(D2D1::Point2F(0, hero.bottom));
            status_shade_->SetOpacity(0.9f);
            target_->FillGeometry(shape.Get(), status_shade_.Get());
        }
    }
    const auto logo = box(layout.logo(), 8 * (1 - fade));
    if (selected_ < logos_.size() && logos_[selected_].bitmap) drawLibraryLogo(logo, fade);
    else {
        // Like the full game page, an inline detail view always identifies its game while art loads.
        const float title_height = paragraph(game.name, logo.left, 0, logo.right - logo.left, settings_heading_font_.Get(), palette::text(fade), 2, false);
        paragraph(game.name, logo.left, logo.bottom - title_height, logo.right - logo.left, settings_heading_font_.Get(), palette::text(fade), 2);
    }
    const auto* info = context_->preview;
    std::wstring facts = libraryStatus(game, sessionOf(game));
    if (info && !info->release_date.empty()) facts += L"   ·   " + info->release_date;
    if (info && !info->genres.empty()) facts += L"   ·   " + info->genres;
    textLine(facts, left + 28, layout.factsCenter(), width - 56, settings_body_font_.Get(), gray(0.82f, fade));
    drawLibraryDescription(layout.description(), fade);
    drawLibraryFacts(layout.facts(), fade, true);
    // Glass captures the scene before any buttons are painted, as on the full details page.
    drawLibraryActions();
    const float rating_left = layout.action(1).right + 24;
    drawRating(rating_left, layout.actionTop() + 9, layout.right() - 28 - rating_left, fade);
}

// The category tabs in the top bar, in place of the title (every view). The first active tab
// starts at the left; its glass pill glides to the middle with the next selection. Key hints
// are in the bottom bar. The strip fades with the bar as the grid scrolls.
void Renderer::drawLibraryTabs() {
    const auto& tabs = context_->library_tabs;
    const float bar = libraryBarAlpha();
    if (tabs.empty() || bar < 0.01f) return;
    const float width = viewport_.width, slot = library_tabs::slot(width), cx = library_tabs::center(width);
    const float glide = context_->library_tab_glide.value_or(static_cast<float>(context_->library_tab));
    const float cy = library_tabs::top + library_tabs::height * 0.5f;
    const auto pill = library_tabs::tab(0, width, glide);
    const auto shape = D2D1::RoundedRect(box(pill), library_tabs::height * 0.5f, library_tabs::height * 0.5f);
    glassFill(glassPanel(shape, 12), D2D1::ColorF(0.16f, 0.17f, 0.19f, 0.78f), bar);
    target_->PushAxisAlignedClip(D2D1::RectF(cx - 1.5f * slot, library_tabs::top - 2, cx + 1.5f * slot, library_tabs::top + library_tabs::height + 2),
                                 D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    for (size_t i = 0; i < tabs.size(); ++i) {
        const float offset = static_cast<float>(i) - glide;
        const float position = offset + library_tabs::focusOffset(glide);
        if (std::abs(position) > 2.0f) continue;
        const float focus = std::clamp(1 - std::abs(offset), 0.0f, 1.0f);
        const float alpha = std::clamp(2.0f - std::abs(position), 0.0f, 1.0f) * bar;
        const auto r = library_tabs::tab(offset, width, glide);
        if (!tabs[i].mark.empty() && hasPlatformMark(tabs[i].mark)) {
            // A studio's tab: its logo, with no name beside it.
            const float logo_w = platformMarkWidth(tabs[i].mark, 24, r.right - r.left - 40);
            drawPlatformMark(tabs[i].mark, (r.left + r.right) * 0.5f - logo_w * 0.5f, cy, 24, r.right - r.left - 40, alpha * std::lerp(0.5f, 1.0f, focus), button_font_.Get());
            continue;
        }
        std::wstring title = tabs[i].title;
        if (!title.empty()) CharUpperBuffW(title.data(), static_cast<DWORD>(title.size()));
        const std::wstring count = std::to_wstring(tabs[i].count);
        const float count_width = std::ceil(textAdvance(count, toast_sub_font_.Get())) + 14;
        const float room = r.right - r.left - 24 - count_width - 8;
        const float title_width = std::min(room, std::ceil(textAdvance(title, button_font_.Get())) + 2);
        const float x = (r.left + r.right) * 0.5f - (title_width + 8 + count_width) * 0.5f;
        textLine(title, x, cy, title_width, button_font_.Get(), gray(std::lerp(0.6f, 0.98f, focus), alpha), DWRITE_TEXT_ALIGNMENT_LEADING);
        const float pill_left = x + title_width + 8;
        brush(gray(1, (0.08f + 0.08f * focus) * alpha));
        target_->FillRoundedRectangle(Renderer::pill(D2D1::RectF(pill_left, cy - 10, pill_left + count_width, cy + 10)), solid_.Get());
        textLine(count, pill_left, cy, count_width, toast_sub_font_.Get(), gray(std::lerp(0.55f, 0.9f, focus), alpha), DWRITE_TEXT_ALIGNMENT_CENTER);
    }
    target_->PopAxisAlignedClip();
}

// Przegląd: the landscape tiles beside the focused game's panel.
void Renderer::drawLibraryOverview() {
    const float width = viewport_.width, height = viewport_.height;
    const float grid_left = overview_layout::gridLeft(width);
    // Rows scrolled up fade out under the top bar instead of being cut off at the list's top edge.
    const float clip_top = context_->library_controls && context_->library_controls->search ? overview_layout::list_top - 6 : 92;
    target_->PushAxisAlignedClip(D2D1::RectF(grid_left - 14, clip_top, width, height - overview_layout::bottom_margin + 10),
                                 D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    // Fully shown at the list's top, gone once its bottom passes the tabs.
    const auto fadeIn = [&](const GameCard& card) {
        return smoothFade((card.y + card.size - clip_top) / std::max(1.0f, overview_layout::list_top + card.size - clip_top));
    };
    const float previous_corner = squircle_corner_;
    squircle_corner_ = 0.4f;
    for (size_t i = 0; i < cards_->size(); ++i) {
        const auto& card = (*cards_)[i];
        if (i == selected_ || card.y > height || card.y + card.size < clip_top) continue;
        drawRectangularCard(card, i, fadeIn(card));
    }
    const float launch = smoothFade(context_->launch_progress);
    if (launch > 0.001f) {
        brush(D2D1::ColorF(0.008f, 0.012f, 0.016f, launch * 0.82f));
        target_->FillRectangle(D2D1::RectF(0, 0, width, height), solid_.Get());
    }
    drawRectangularCard((*cards_)[selected_], selected_, fadeIn((*cards_)[selected_]));
    squircle_corner_ = previous_corner;
    target_->PopAxisAlignedClip();
    drawOverviewPanel();
}

void Renderer::drawOverviewPanel() {
    if (selected_ >= cards_->size()) return;
    const float width = viewport_.width, height = viewport_.height;
    const overview_layout::Panel panel{width, height};
    const float left = overview_layout::left, panel_width = overview_layout::panelWidth(width);
    const auto& card = (*cards_)[selected_];
    const auto& game = card.game;
    const auto* info = context_->preview;
    const float fade = smoothFade(detail_), lift = 14 * (1 - fade);
    // The square cover is independent of the landscape art used by the tiles.
    const auto area = panel.cover();
    const auto cover_rect = D2D1::RectF(area.left, area.top + lift, area.right, area.bottom + lift);
    const float previous_corner = squircle_corner_;
    squircle_corner_ = 0.2f;
    const auto shape = tileShape(cover_rect, tile_shape_);
    squircle_corner_ = previous_corner;
    if (shape) {
        brush(D2D1::ColorF(0.045f, 0.05f, 0.06f));
        target_->FillGeometry(shape.Get(), solid_.Get());
        const bool frozen = sessionOf(game) == SessionState::frozen;
        if (selected_ < covers_.size() && covers_[selected_].bitmap)
            fillImage(covers_[selected_], cover_rect, 14, fade * card.cover_opacity * reveal(covers_[selected_]), frozen, shape.Get());
        brush(gray(1, 0.16f)); target_->DrawGeometry(shape.Get(), solid_.Get(), 1.0f);
    }
    float y = area.bottom + 18;
    textLine(game.name, left, y + 12, panel_width, category_font_.Get(), gray(0.97f, fade));
    y += 34;
    if (const auto platform = activePlatformName(game); !platform.empty()) {
        // The platform's logo under the name (its name until the logo arrives).
        drawPlatformMark(platform, left, y + 12, 22, std::min(panel_width, 200.0f), fade, toast_sub_font_.Get());
        y += 34;
    }
    // Facts in two columns: playtime and achievements.
    struct Fact { std::wstring label, value; float share = -1; };
    std::vector<Fact> facts;
    facts.push_back({tr(L"Czas gry"), info && info->playtime_minutes && *info->playtime_minutes ? formatPlaytime(*info->playtime_minutes) : std::wstring(L"—")});
    if (info && info->achievements_total)
        facts.push_back({tr(L"Osiągnięcia"), std::to_wstring(info->achievements_unlocked) + L" / " + std::to_wstring(info->achievements_total),
                         std::clamp(static_cast<float>(info->achievements_unlocked) / info->achievements_total, 0.0f, 1.0f)});
    const float box_width = (panel_width - 8) * 0.5f, box_height = 52;
    for (size_t i = 0; i < facts.size(); ++i) {
        const float x = left + (i % 2) * (box_width + 8), top = y + (i / 2) * (box_height + 8);
        brush(gray(1, 0.05f * fade));
        target_->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(x, top, x + box_width, top + box_height), 14, 14), solid_.Get());
        std::wstring label = facts[i].label;
        if (!label.empty()) CharUpperBuffW(label.data(), static_cast<DWORD>(label.size()));
        textLine(label, x + 12, top + 16, box_width - 24, friend_section_font_.Get(), gray(0.55f, fade));
        textLine(facts[i].value, x + 12, top + 35, box_width - 24, settings_body_font_.Get(), gray(0.95f, fade));
        if (facts[i].share >= 0) {
            brush(gray(1, 0.12f * fade)); target_->FillRectangle(D2D1::RectF(x + 12, top + box_height - 6, x + box_width - 12, top + box_height - 4), solid_.Get());
            brush(uiAccent(fade));
            target_->FillRectangle(D2D1::RectF(x + 12, top + box_height - 6, x + 12 + (box_width - 24) * facts[i].share, top + box_height - 4), solid_.Get());
        }
    }
    y += ((facts.size() + 1) / 2) * (box_height + 8) + 6;
    // The description fills what is left above the buttons.
    const auto details_button = panel.button(0), play_button = panel.button(1);
    if (info && !info->description.empty() && details_button.top - 14 - y > 40) {
        details_body_font_->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
        text(info->description, left, y, panel_width, details_button.top - 14 - y, details_body_font_.Get(), gray(0.72f, fade));
        details_body_font_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    }
    // PSfive's pills on frosted glass; Left from the tiles focuses them (library_controls->action:
    // 0 "Szczegóły", 1 the primary action), the white pill gliding between them.
    const auto* controls = context_->library_controls;
    const int focused = controls && !controls->open() && controls->focus < 0 ? controls->action : -1;
    drawButtonGlass(box(details_button), 1, true);
    drawButtonGlass(box(play_button), 1, true, false, context_->primary_enabled);
    if (focused >= 0) {
        auto r = box(panel.button(focused));
        if (context_->library_action_glide) { const auto& g = *context_->library_action_glide; r = D2D1::RectF(g.left, g.top, g.right, g.bottom); }
        drawButtonFocus(r);
    }
    const auto caption = [&](settings_ui::Rect r, const std::wstring& label, int index, bool enabled) {
        const float inset = (r.bottom - r.top) * 0.5f;
        textLine(label, r.left + inset, (r.top + r.bottom) * 0.5f, r.right - r.left - 2 * inset, action_font_.Get(), // Semibold, as the other buttons.
                 buttonInk(focused == index ? 1.0f : 0.0f, enabled), DWRITE_TEXT_ALIGNMENT_CENTER);
    };
    caption(details_button, tr(L"Szczegóły"), 0, true);
    if (!drawInstallProgress(game, box(play_button), buttonInk(focused == 1 ? 1.0f : 0.0f, true),
                             focused == 1 ? gray(0.05f, 0.15f) : gray(1, 0.2f), settings_body_font_.Get()))
        caption(play_button, context_->primary_action, 1, context_->primary_enabled);
}
