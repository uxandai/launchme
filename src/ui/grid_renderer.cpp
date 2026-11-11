// Renderer: the grid screens (libraries, Search, the Store) and the quick-scroll letter.
#include "renderer.hpp"
#include "plugin_page.hpp"
#include "renderer_internal.hpp"
#include "desktop_view.hpp"
#include "grid_layout.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

using Microsoft::WRL::ComPtr;
using namespace render_detail;

void Renderer::drawGrid() {
    const float width = viewport_.width;
    const bool bookshelf = context_->library_mode == library_view::View::bookshelf;
    brush(bookshelf ? color(0.008f, 0.009f, 0.012f) : color(0.012f, 0.016f, 0.021f, 0.92f));
    target_->FillRectangle(D2D1::RectF(0, 0, width, viewport_.height), solid_.Get());
    if (context_->library_mode == library_view::View::cinematic) {
        drawBackground();
        // Let the artwork show behind the spotlight, then fade to black under the capsules.
        if (status_shade_ && context_->library_options.cinematic_column) {
            // One column: the picture stays clear behind the capsules and darkens under the synopsis.
            status_shade_->SetStartPoint(D2D1::Point2F(width * 0.78f, 0));
            status_shade_->SetEndPoint(D2D1::Point2F(width * 0.12f, 0));
            status_shade_->SetOpacity(0.96f);
            target_->FillRectangle(D2D1::RectF(0, 0, width, viewport_.height), status_shade_.Get());
            status_shade_->SetStartPoint(D2D1::Point2F(0, viewport_.height * 0.45f));
            status_shade_->SetEndPoint(D2D1::Point2F(0, viewport_.height));
            status_shade_->SetOpacity(0.7f);
            target_->FillRectangle(D2D1::RectF(0, 0, width, viewport_.height), status_shade_.Get());
        } else if (status_shade_) {
            status_shade_->SetStartPoint(D2D1::Point2F(0, 120));
            status_shade_->SetEndPoint(D2D1::Point2F(0, cinematic_layout::list_top + 220));
            status_shade_->SetOpacity(1);
            target_->FillRectangle(D2D1::RectF(0, 0, width, viewport_.height), status_shade_.Get());
        }
    }
    // List view: the focused game's background, faint, behind the header, rows and panel. Półka:
    // only a hint of it above the black stage (its floor reflects the covers).
    if (library_view::preview(context_->library_mode) && context_->library_mode != library_view::View::cinematic)
        drawLibraryAmbient(context_->library_mode == library_view::View::detailed ? 0.12f :
            context_->library_mode == library_view::View::list ? 0.18f : context_->library_mode == library_view::View::list_plus ? 0.08f :
            bookshelf ? 0.14f : 0.32f);
    beginScreenForeground();
    // Back: a glass circle with the white dot (white with a dark dot while it has the focus; in
    // the library the bar's white pill glides onto it: drawLibraryControls).
    const bool store = context_->screen == ScreenMode::store;
    if (store) { // The stores share one frame (plugin_ui::Layout): the same back circle as a plugin page's.
        const auto box = plugin_ui::Layout::backBox(viewport_.width);
        const auto ink = drawButton(D2D1::RectF(box.left, box.top, box.right, box.bottom), context_->grid_back ? 1.0f : 0.0f, 1, false);
        brush(ink); target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F((box.left + box.right) * 0.5f, (box.top + box.bottom) * 0.5f), 3, 3), solid_.Get());
    } else {
    if (context_->library_controls) {
        drawButtonGlass(D2D1::RectF(16, 35, 52, 71), 1, false);
        brush(buttonInk(0));
    } else brush(drawButton(D2D1::RectF(16, 35, 52, 71), context_->grid_back ? 1.0f : 0.0f, 1, false));
    target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(34, 53), 3, 3), solid_.Get());
    }
    const bool searching = context_->screen == ScreenMode::search || store;
    // The library's own search field: the header stays put while it is open, like the search screen.
    const auto* library = context_->screen == ScreenMode::library ? context_->library_controls : nullptr;
    const bool filtering = library && library->search;
    const float unfold = smoothFade(context_->library_search_progress);
    const bool pinned = searching || filtering || library_view::pinnedHeader(context_->library_mode);
    const float header_alpha = pinned ? 1.0f :
        smoothFade(1.0f - context_->grid_scroll / 165.0f);
    const float lift = pinned ? 0.0f : context_->grid_scroll * 0.24f;
    // The title, unless the category tabs take its place in the top bar.
    if (header_alpha > 0.004f && context_->library_tabs.empty() && !store)
        text(store ? std::wstring(tr(L"SKLEP")) : searching ? std::wstring(tr(L"SZUKAJ GIER")) : context_->library_title, 68,
             37 - lift, context_->library_controls ? library_ui::Layout{width}.filters().left - 92 : width - 136, 45,
             category_font_.Get(), color(0.78f, 0.78f, 0.78f, header_alpha));
    if (store) {
        const plugin_ui::Layout frame{viewport_.width, viewport_.height};
        const auto box = frame.field();
        drawStoreField(box.left, box.top, box.right, box.bottom, context_->search_query, tr(L"Wpisz tytuł gry…"), true, context_->search_caret);
    } else if (searching) {
        const auto& query = context_->search_query;
        if (query.empty())
            text(store ? tr(L"Szukaj tytułu lub AppID…") : tr(L"Wpisz tytuł gry…"), 66, 99, width - 130, 71, hero_font_.Get(), color(0.53f, 0.53f, 0.53f));
        else fieldText(query, context_->search_caret, std::sin(elapsed_ * 4.0f) >= 0, 66, 99, width - 130, 71, hero_font_.Get(), color(0.96f, 0.96f, 0.96f));
    } else {
        if (library && unfold > 0.004f) {
            const auto& query = library->query;
            const bool typing = library->typing();
            const float field_right = library_ui::Layout{width}.field().right;
            if (query.empty())
                text(tr(L"Tytuł, gatunek albo tag…"), 66, 99 + (1.0f - unfold) * 14, field_right - 66, 71, hero_font_.Get(), color(0.53f, 0.53f, 0.53f, unfold));
            else fieldText(query, library->caret(), typing && std::sin(elapsed_ * 4.0f) >= 0, 66, 99 + (1.0f - unfold) * 14, field_right - 66, 71,
                           hero_font_.Get(), color(0.96f, 0.96f, 0.96f, (typing ? 1.0f : 0.72f) * unfold));
            if (!query.empty() && cards_ && cards_->empty())
                text(tr(L"Brak wyników. Spróbuj innego tytułu."), 68, 200, width - 136, 34, menu_font_.Get(), color(0.62f, 0.62f, 0.62f, unfold));
        }
    }
    if (store) {
        text(context_->store_status, plugin_ui::Layout::columnLeft(width), plugin_ui::Layout::status_top, width - 2 * plugin_ui::Layout::columnLeft(width), 28, toast_sub_font_.Get(), D2D1::ColorF(0.65f, 0.65f, 0.65f));
    }
    drawLibraryTabs();
    if (!context_->library_note.empty() && header_alpha > 0.004f)
        text(context_->library_note, 68, 104 - lift, width - 136, 30, menu_font_.Get(), color(0.62f, 0.62f, 0.62f, header_alpha));
    if (!cards_ || cards_->empty()) {
        // An empty result says so instead of leaving only the header. The Store reports its own
        // state in store_status; the library's typed query has its line under the field above.
        if (searching && !store && context_->search_query.empty() && !context_->recent_searches.empty()) {
            // Ostatnie wyszukiwania: a short list under the empty field; the pad's row a white pill.
            textLine(tr(L"Ostatnie wyszukiwania"), 68, 214, width - 136, toast_sub_font_.Get(), palette::muted());
            const float row_w = std::min(640.0f, width - 136);
            for (size_t i = 0; i < context_->recent_searches.size(); ++i) {
                const float top = 236 + static_cast<float>(i) * 54, cy = top + 25;
                const bool focused = context_->recent_search_focus == static_cast<int>(i);
                if (focused) {
                    brush(palette::gray(0.96f));
                    target_->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(52, top, 52 + row_w, top + 50), 25, 25), solid_.Get());
                }
                const auto ink = focused ? palette::gray(0.06f) : palette::gray(0.82f);
                desktopGlyph(desktop_ui::glyph::history, 82, cy, 20, focused ? ink : palette::muted());
                textLine(context_->recent_searches[i], 108, cy, row_w - 72, menu_font_.Get(), ink);
            }
            return;
        }
        if (scanning_ || store) return;
        const bool library_query = library && !library->query.empty();
        if (searching && !context_->search_query.empty())
            text(tr(L"Brak wyników. Spróbuj innego tytułu."), 68, 200, width - 136, 34, menu_font_.Get(), palette::stateLabel());
        else if (library_query && unfold <= 0.004f)
            text(tr(L"Brak wyników. Spróbuj innego tytułu."), 68, 200, width - 136, 34, menu_font_.Get(), palette::stateLabel());
        else if (library && !library_query) {
            // An empty view (a tab or filters with no games): in the middle of the screen, both ways.
            const float middle = viewport_.height * 0.5f;
            textLine(tr(L"Brak gier w tym widoku"), 68, middle - 20, width - 136, menu_font_.Get(), palette::gray(0.82f),
                     DWRITE_TEXT_ALIGNMENT_CENTER);
            textLine(tr(L"Zmień zakładkę lub filtry."), 68, middle + 22, width - 136, settings_body_font_.Get(), palette::muted(),
                     DWRITE_TEXT_ALIGNMENT_CENTER);
        }
        return;
    }
    // The Widok drawer covers the left of the screen, where most views keep their focused game: the
    // view makes way beside it (scaled into the remaining width, centred vertically), so the options
    // it changes live stay in sight. Coverflow and Półka keep their size (the case, its shadow and the
    // title as they are): they only move right by half the drawer. The header above stays put;
    // pointer input goes to the drawer.
    const float drawer = context_->library_panel == library_ui::Panel::views && context_->library_controls ?
        std::max(0.0f, library_ui::views::offset(context_->library_views_progress) + library_ui::views::width) : 0.0f;
    D2D1_MATRIX_3X2_F frame{};
    target_->GetTransform(&frame);
    if (drawer > 0.5f && library_view::carousel(context_->library_mode)) {
        target_->SetTransform(D2D1::Matrix3x2F::Translation(drawer * 0.5f, 0) * frame);
    } else if (drawer > 0.5f && width > drawer + 200) {
        const float scale = (width - drawer - 16 * drawer / library_ui::views::width) / width;
        target_->SetTransform(D2D1::Matrix3x2F::Scale(scale, scale) *
            D2D1::Matrix3x2F::Translation(width - width * scale, viewport_.height * (1 - scale) * 0.5f) * frame);
    }
    const bool carousel = library_view::carousel(context_->library_mode);
    if (context_->library_mode == library_view::View::cinematic) drawLibraryCinematic();
    else if (context_->library_mode == library_view::View::overview) drawLibraryOverview();
    else if (carousel) drawLibraryCoverflow();
    else if (context_->library_list) drawLibraryList();
    else {
        // Under the pinned header (from the search field's foot): the first row's focused tile grows a
        // little and keeps its whole outline.
        if (pinned)
            target_->PushAxisAlignedClip(D2D1::RectF(0, store ? 188.0f : 174.0f, width, viewport_.height), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        for (size_t i = 0; i < cards_->size(); ++i) {
            if (i == selected_) continue;
            drawCard((*cards_)[i], i);
        }
        drawCard((*cards_)[selected_], selected_);
        // Captions (17 px, readable from the sofa): a square fallback tile that already prints its
        // name does not repeat it under itself.
        const bool squares = !context_->library_options.grid_posters && !context_->library_options.grid_landscape;
        for (size_t i = 0; i < cards_->size(); ++i) {
            const auto& card = (*cards_)[i];
            if (card.y < -180 || card.y > viewport_.height - 30) continue;
            if (squares && i < covers_.size() && !covers_[i].bitmap && !covers_[i].decoding && isGame(card.game)) continue;
            // A name longer than its tile fades out on the right; the focused one glides to its end and back.
            fadingLine(card.game.name, card.x, card.y + card.size + 20, card.width(), toast_font_.Get(),
                       color(0.92f, 0.92f, 0.92f, (i == selected_ ? 1.0f : 0.74f) * card.opacity), i == selected_, DWRITE_TEXT_ALIGNMENT_CENTER);
        }
        if (pinned) target_->PopAxisAlignedClip();
    }
    target_->SetTransform(frame);
    if (!carousel) drawScrollLetter();
}

// Quick scrolling: the screen dims and the focused game's initial stands in the middle.
void Renderer::drawScrollLetter() {
    const float alpha = smoothFade(context_->scroll_letter_alpha);
    if (alpha < 0.004f || context_->scroll_letter.empty()) return;
    brush(color(0.005f, 0.008f, 0.012f, 0.7f * alpha));
    target_->FillRectangle(D2D1::RectF(0, 0, viewport_.width, viewport_.height), solid_.Get());
    const float cx = viewport_.width * 0.5f, cy = viewport_.height * 0.5f;
    text(context_->scroll_letter, cx - 200, cy - 208, 400, 400, index_font_.Get(), color(0.97f, 0.97f, 0.97f, alpha));
}
