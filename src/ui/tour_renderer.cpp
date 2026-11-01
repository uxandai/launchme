// First-run walkthrough over the home screen (src/app/tour.cpp): everything dimmed except the
// panel the step opened, a bubble beside that panel and the card with the stepper.
#include "renderer.hpp"
#include "renderer_internal.hpp"
#include "snitch_layout.hpp"
#include "category_menu.hpp"
#include "category_rail.hpp"
#include "context_menu.hpp"
#include "friends_layout.hpp"
#include "quick_menu.hpp"
#include "tour_view.hpp"

#include <algorithm>
#include <cmath>

using Microsoft::WRL::ComPtr;

namespace {
using palette::gray;
using render_detail::box;
using render_detail::smoothFade;
D2D1_RECT_F unite(D2D1_RECT_F a, D2D1_RECT_F b) {
    return D2D1::RectF(std::min(a.left, b.left), std::min(a.top, b.top), std::max(a.right, b.right), std::max(a.bottom, b.bottom));
}
D2D1_RECT_F inflate(D2D1_RECT_F r, float by) { return D2D1::RectF(r.left - by, r.top - by, r.right + by, r.bottom + by); }
// Where the bubble sits relative to its anchor; its pointer is on the opposite edge.
enum Side { right_of, left_of, above, below };
constexpr float bubble_width = 400, bubble_padding = 20, pointer_length = 12, pointer_half = 10, bubble_radius = 16;
constexpr float spot_radius = 18, spot_gap = 14;
}

float Renderer::paragraph(const std::wstring& value, float x, float top, float width, IDWriteTextFormat* format,
                          D2D1_COLOR_F ink, int max_lines, bool draw) {
    if (value.empty() || width <= 0 || max_lines <= 0 || !format) return 0;
    const auto metrics = typeMetrics(format);
    ComPtr<IDWriteTextLayout> layout;
    if (FAILED(dwrite_->CreateTextLayout(value.c_str(), static_cast<UINT32>(value.size()), format, width,
                                         metrics.line * max_lines + 0.5f, layout.GetAddressOf()))) return 0;
    layout->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
    layout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    DWRITE_TEXT_METRICS measured{};
    if (FAILED(layout->GetMetrics(&measured))) return 0;
    const int lines = std::clamp(static_cast<int>(measured.lineCount), 1, max_lines);
    if (draw) {
        brush(ink);
        target_->DrawTextLayout(D2D1::Point2F(x, top), layout.Get(), solid_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
    }
    return lines * metrics.line;
}

std::optional<D2D1_RECT_F> Renderer::tourSpot(const TourView& tour, float& opened, D2D1_POINT_2F& anchor, int& side) const {
    const float width = viewport_.width, height = viewport_.height;
    opened = 1;
    switch (tour.target) {
    case TourView::Target::category_menu: {
        const float progress = smoothFade(context_->category_menu_progress);
        if (progress < 0.01f) return std::nullopt;
        constexpr float drawer = 350;
        opened = progress;
        anchor = D2D1::Point2F(drawer * progress + spot_gap,
                               std::clamp(context_->category_highlight_y + category_menu::row_height * 0.5f, 140.0f, height - 140));
        side = right_of;
        return D2D1::RectF(-40, -40, drawer * progress, height + 40);
    }
    case TourView::Target::category_rail: {
        if (!context_->categories || context_->categories->empty()) return std::nullopt;
        const category_rail::Layout layout{width, height, context_->categories->size(), context_->active_category};
        const float top = layout.top(), bottom = top + layout.visible() * category_rail::Layout::row_height;
        const auto spot = D2D1::RectF(14, top - spot_gap, layout.right() + 30, bottom + spot_gap);
        anchor = D2D1::Point2F(spot.right + spot_gap, layout.rowTop(context_->active_category) + category_rail::Layout::row_height * 0.5f);
        side = right_of;
        return spot;
    }
    case TourView::Target::xmb_row: {
        if (!cards_ || !context_->xmb || context_->xmb_top == 0 || cards_->size() < context_->xmb_top) return std::nullopt;
        float top = height, bottom = 0;
        for (size_t i = 0; i < context_->xmb_top; ++i) {
            const auto& card = (*cards_)[i];
            if (card.opacity < 0.05f || card.x > width || card.x + card.width() < 0) continue;
            top = std::min(top, card.y);
            bottom = std::max(bottom, card.y + card.size);
        }
        if (bottom <= top) return std::nullopt;
        const auto& column = (*cards_)[std::min(context_->xmb_column, context_->xmb_top - 1)];
        // The caption and count under the focused tile belong to the row.
        const auto spot = D2D1::RectF(-40, top - spot_gap, width + 40, std::max(bottom + spot_gap, column.y + column.size + 58));
        anchor = D2D1::Point2F(column.x + column.width() * 0.5f + 120, spot.bottom + spot_gap);
        side = below;
        return spot;
    }
    case TourView::Target::drawer: {
        const float progress = smoothFade(context_->friends_progress);
        if (progress < 0.01f) return std::nullopt;
        opened = progress;
        const float left = width - friends_layout::drawer_width * progress;
        anchor = D2D1::Point2F(left - spot_gap, height * 0.42f);
        side = left_of;
        return D2D1::RectF(left, -40, width + 40, height + 40);
    }
    case TourView::Target::quick_bar: {
        const float progress = smoothFade(context_->quick_progress);
        if (progress < 0.01f) return std::nullopt;
        opened = progress;
        const int plugins = static_cast<int>(context_->plugin_menu.size());
        if (context_->snitch) {
            const float y = snitch_layout::dockY(height), scale = snitch_layout::dockScale(width, plugins);
            anchor = D2D1::Point2F(snitch_layout::dockX(width, context_->quick_selected, plugins), y - 50 * scale - spot_gap);
            side = above;
            return D2D1::RectF(snitch_layout::dockX(width, 0, plugins) - 58 * scale, y - 50 * scale,
                snitch_layout::dockX(width, quick_menu::count(plugins) - 1, plugins) + 58 * scale, y + 80 * scale);
        }
        const float y = quick_menu::centerY(height, context_->quick_progress, false);
        const float circle = context_->quick_circle_x.value_or(quick_menu::center(width, context_->quick_selected, plugins));
        anchor = D2D1::Point2F(circle, y - 50 - spot_gap);
        side = above;
        return D2D1::RectF(quick_menu::center(width, 0, plugins) - 58, y - 50,
                           quick_menu::center(width, quick_menu::count(plugins) - 1, plugins) + 58, y + 70);
    }
    case TourView::Target::menu: {
        const float progress = smoothFade(context_->menu_progress);
        if (progress < 0.01f || !cards_ || selected_ >= cards_->size() || !isGame((*cards_)[selected_].game)) return std::nullopt;
        opened = progress;
        const auto& active = (*cards_)[selected_];
        const bool beside = context_->menu_beside;
        context_menu::Rows rows{context_->menu_suspend, static_cast<int>(context_->menu_plugins.size()), context_->menu_session};
        rows.secondary_only = context_->psfive;
        const float x = context_menu::left(active, beside, width), y = context_menu::top(active, rows, beside, height);
        const auto menu = D2D1::RectF(x, y, x + context_menu::width(active, beside), y + context_menu::height(rows));
        const auto card = D2D1::RectF(active.x, active.y, active.x + active.width(), active.y + active.size);
        const auto spot = inflate(unite(menu, card), spot_gap);
        const bool room = spot.right + spot_gap + pointer_length + bubble_width <= width - 16;
        anchor = D2D1::Point2F(room ? spot.right + spot_gap : spot.left - spot_gap, (menu.top + menu.bottom) * 0.5f);
        side = room ? right_of : left_of;
        return spot;
    }
    default: return std::nullopt;
    }
}

void Renderer::drawTour() {
    const auto* tour = context_->tour;
    if (!tour || tour->alpha < 0.004f) return;
    const float width = viewport_.width, height = viewport_.height;
    // Opening and closing fade the whole overlay as one picture.
    const float alpha = smoothFade(tour->alpha);
    ComPtr<ID2D1Layer> layer;
    const bool fading = alpha < 0.996f && SUCCEEDED(target_->CreateLayer(layer.GetAddressOf()));
    if (fading) target_->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), nullptr, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
                                                         D2D1::IdentityMatrix(), alpha), layer.Get());
    float opened = 0;
    D2D1_POINT_2F anchor{};
    int side = right_of;
    const auto spot = tourSpot(*tour, opened, anchor, side);
    const float dim = tour->layouts ? 0.88f : 0.74f;
    // The spotlight clears with the step's fade-in and with its panel sliding in.
    const float clear = smoothFade(tour->fade) * opened;
    const auto screen = D2D1::RectF(-10, -10, width + 10, height + 10);
    ComPtr<ID2D1RoundedRectangleGeometry> hole;
    ComPtr<ID2D1RectangleGeometry> whole;
    ComPtr<ID2D1GeometryGroup> outside;
    if (spot && SUCCEEDED(factory_->CreateRoundedRectangleGeometry(D2D1::RoundedRect(*spot, spot_radius, spot_radius), hole.GetAddressOf())) &&
        SUCCEEDED(factory_->CreateRectangleGeometry(screen, whole.GetAddressOf()))) {
        ID2D1Geometry* parts[] = {whole.Get(), hole.Get()};
        factory_->CreateGeometryGroup(D2D1_FILL_MODE_ALTERNATE, parts, 2, outside.GetAddressOf());
    }
    brush(D2D1::ColorF(0, 0, 0, dim));
    if (outside) {
        target_->FillGeometry(outside.Get(), solid_.Get());
        if (clear < 0.996f) {
            brush(D2D1::ColorF(0, 0, 0, dim * (1 - clear)));
            target_->FillGeometry(hole.Get(), solid_.Get());
        }
        if (panel_borders_) {
            brush(uiAccent(0.55f * clear));
            target_->DrawGeometry(hole.Get(), solid_.Get(), 1.5f);
        }
    } else target_->FillRectangle(screen, solid_.Get());
    if (tour->layouts) {
        // A slider: the chosen layout in view, the tile cut at an edge says there are more.
        const tour::Layout layout{width, height, tour->card_bottom, glideLayoutStrip(tour::Layout::firstFor(tour->layout_choice))};
        const auto band = layout.tileBand();
        target_->PushAxisAlignedClip(box(band), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        for (int option = 0; option < settings_ui::home_layout_count; ++option) {
            const auto tile = layout.tile(option);
            if (tile.right <= band.left || tile.left >= band.right) continue;
            drawLayoutTile(option, box(tile), tour->layout_choice == option,
                           tour->hover_tile >= 0 ? tour->hover_tile == option : tour->focus_tile == option, // As in Settings: the mouse takes the highlight.
                           tour->preview_option == option ? tour->preview_seconds : 0, tour->layout_descriptions[option]);
        }
        target_->PopAxisAlignedClip();
    }
    if (spot && !tour->bubble_body.empty() && clear > 0.01f) drawTourBubble(*tour, anchor, side, clear);
    drawTourCard(*tour);
    if (fading) target_->PopLayer();
}

void Renderer::drawTourBubble(const TourView& tour, D2D1_POINT_2F anchor, int side, float alpha) {
    const float width = viewport_.width, height = viewport_.height;
    const float text_width = bubble_width - 2 * bubble_padding;
    const auto head = typeMetrics(menu_font_.Get()), body = typeMetrics(settings_body_font_.Get());
    const float body_height = paragraph(tour.bubble_body, 0, 0, text_width, settings_body_font_.Get(), {}, 4, false);
    const int lines = std::max(1, static_cast<int>(std::lround(body_height / std::max(1.0f, body.line))));
    // From the first capitals to the last baseline, then the same padding as above them.
    const bool titled = !tour.bubble_title.empty();
    const float title_block = titled ? head.line - head.capTop() + text_group_gap + body.capTop() : 0;
    const float ink = title_block + (lines - 1) * body.line + body.cap;
    const float bubble_height = bubble_padding * 2 + ink + 4;
    float left = 0, top = 0;
    switch (side) {
    case right_of: left = anchor.x + pointer_length; top = anchor.y - bubble_height * 0.5f; break;
    case left_of: left = anchor.x - pointer_length - bubble_width; top = anchor.y - bubble_height * 0.5f; break;
    case above: left = anchor.x - bubble_width * 0.5f; top = anchor.y - pointer_length - bubble_height; break;
    default: left = anchor.x - bubble_width * 0.5f; top = anchor.y + pointer_length; break;
    }
    left = std::clamp(left, 16.0f, std::max(16.0f, width - 16 - bubble_width));
    top = std::clamp(top, 16.0f, std::max(16.0f, height - 16 - bubble_height));
    const auto frame = D2D1::RectF(left, top, left + bubble_width, top + bubble_height);
    const auto fill = D2D1::ColorF(0.075f, 0.08f, 0.09f, 0.98f * alpha);
    const auto edge = uiAccent(0.6f * alpha);
    brush(fill);
    target_->FillRoundedRectangle(D2D1::RoundedRect(frame, bubble_radius, bubble_radius), solid_.Get());
    if (panel_borders_) {
        brush(edge);
        target_->DrawRoundedRectangle(D2D1::RoundedRect(frame, bubble_radius, bubble_radius), solid_.Get(), 1.3f);
    }
    // Pointer: a triangle from the edge toward the anchor; its base covers the outline under it.
    const float reach = bubble_radius + pointer_half;
    D2D1_POINT_2F a{}, b{}, tip{};
    if (side == right_of || side == left_of) {
        const float y = std::clamp(anchor.y, top + reach, top + bubble_height - reach);
        const float x = side == right_of ? left + 1 : left + bubble_width - 1;
        const float out = side == right_of ? -pointer_length - 1 : pointer_length + 1;
        a = D2D1::Point2F(x, y - pointer_half); b = D2D1::Point2F(x, y + pointer_half); tip = D2D1::Point2F(x + out, y);
    } else {
        const float x = std::clamp(anchor.x, left + reach, left + bubble_width - reach);
        const float y = side == above ? top + bubble_height - 1 : top + 1;
        const float out = side == above ? pointer_length + 1 : -pointer_length - 1;
        a = D2D1::Point2F(x - pointer_half, y); b = D2D1::Point2F(x + pointer_half, y); tip = D2D1::Point2F(x, y + out);
    }
    ComPtr<ID2D1PathGeometry> pointer;
    ComPtr<ID2D1GeometrySink> sink;
    if (SUCCEEDED(factory_->CreatePathGeometry(pointer.GetAddressOf())) && SUCCEEDED(pointer->Open(sink.GetAddressOf()))) {
        sink->BeginFigure(a, D2D1_FIGURE_BEGIN_FILLED);
        sink->AddLine(tip);
        sink->AddLine(b);
        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
        if (SUCCEEDED(sink->Close())) {
            brush(fill);
            target_->FillGeometry(pointer.Get(), solid_.Get());
            if (panel_borders_) {
                line(a.x, a.y, tip.x, tip.y, edge, 1.3f);
                line(tip.x, tip.y, b.x, b.y, edge, 1.3f);
            }
        }
    }
    const float caps = top + bubble_padding;
    if (titled) textGroup(tour.bubble_title, tour.bubble_body, left + bubble_padding, caps, text_width, menu_font_.Get(),
                          gray(0.97f, alpha), settings_body_font_.Get(), gray(0.76f, alpha), 4, true);
    else paragraph(tour.bubble_body, left + bubble_padding, caps - body.capTop(), text_width, settings_body_font_.Get(), gray(0.86f, alpha), 4);
}

void Renderer::drawTourCard(const TourView& tour) {
    using tour::Layout;
    const Layout layout{viewport_.width, viewport_.height, tour.card_bottom};
    const float content = smoothFade(tour.fade);
    const float text_left = layout.cardLeft() + Layout::padding, text_width = layout.cardRight() - Layout::padding - text_left;
    const int max_lines = tour.card_bottom ? Layout::bottom_body_lines : Layout::body_lines;
    const float body = paragraph(tour.body, 0, 0, text_width, settings_body_font_.Get(), {}, max_lines, false);
    // A tag (e.g. "Eksperymentalne") gets its own line above the text: the title row holds the buttons.
    constexpr float tag_height = 26, tag_gap = 10;
    const float tag_space = tour.tag.empty() ? 0.0f : tag_height + tag_gap;
    const auto card = box(layout.card(body + tag_space));
    const auto rounded = [](D2D1_RECT_F r, float radius) { return D2D1::RoundedRect(r, radius, radius); };
    brush(gray(0.07f, 0.98f));
    target_->FillRoundedRectangle(rounded(card, 20), solid_.Get());
    if (panel_borders_) {
        brush(gray(1, 0.16f));
        target_->DrawRoundedRectangle(rounded(card, 20), solid_.Get(), 1.0f);
    }
    // Stepper: finished steps white, the current one filling with the accent, the rest faint.
    for (int i = 0; i < tour.steps; ++i) {
        const auto segment = box(layout.segment(i, tour.steps));
        brush(gray(1, i < tour.step ? 0.55f : 0.14f));
        target_->FillRoundedRectangle(rounded(segment, 2), solid_.Get());
        if (i != tour.step) continue;
        auto filled = segment;
        filled.right = std::lerp(segment.left, segment.right, std::clamp(tour.progress, 0.0f, 1.0f) * (0.35f + 0.65f * content));
        brush(uiAccent());
        target_->FillRoundedRectangle(rounded(filled, 2), solid_.Get());
    }
    // Title on the buttons' row.
    const float row = layout.rowMiddle();
    const float title_right = layout.button(tour.back ? tour::back : tour::skip).left - 18;
    textLine(tour.title, text_left, row, title_right - text_left, category_font_.Get(), gray(0.96f, content));
    if (!tour.tag.empty()) {
        ComPtr<IDWriteTextLayout> measured;
        DWRITE_TEXT_METRICS metrics{};
        const float ink = SUCCEEDED(dwrite_->CreateTextLayout(tour.tag.c_str(), static_cast<UINT32>(tour.tag.size()), friend_status_font_.Get(),
                                                              text_width, 80, measured.GetAddressOf())) &&
                          SUCCEEDED(measured->GetMetrics(&metrics)) ? metrics.widthIncludingTrailingWhitespace : 120.0f;
        const float top = card.top + Layout::body_top, width = std::min(text_width, ink + 24);
        const auto chip = D2D1::RectF(text_left, top, text_left + width, top + tag_height);
        brush(uiAccent(0.16f * content));
        target_->FillRoundedRectangle(rounded(chip, tag_height * 0.5f), solid_.Get());
        textLine(tour.tag, text_left + 12, top + tag_height * 0.5f, width - 24, friend_status_font_.Get(), uiAccent(content));
    }
    // Pills on the card (no frosting: the card is opaque and a fade layer may be pushed); Back a circle.
    for (int i = tour.back ? tour::back : tour::skip; i < tour::button_count; ++i) {
        const auto button = box(layout.button(i));
        const bool focused = tour.button == i, hovered = tour.hover_button == i;
        const auto ink = drawButton(button, focused ? 1.0f : 0.0f, 1, false, hovered);
        const float middle = (button.top + button.bottom) * 0.5f;
        if (i == tour::back) {
            brush(ink);
            target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F((button.left + button.right) * 0.5f, middle), 3.5f, 3.5f), solid_.Get());
            continue;
        }
        textLine(i == tour::skip ? tr(L"Pomiń") : tour.last ? tr(L"Gotowe") : tr(L"Dalej"), button.left + 10, middle,
                 button.right - button.left - 20, settings_body_font_.Get(), ink, DWRITE_TEXT_ALIGNMENT_CENTER);
    }
    if (body > 0) paragraph(tour.body, text_left, card.top + Layout::body_top + tag_space, text_width, settings_body_font_.Get(), gray(0.74f, content), max_lines);
}
