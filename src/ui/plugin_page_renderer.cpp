#include "renderer.hpp"
#include "friends_layout.hpp"
#include <d2d1helper.h>

// The stores' search field, one look for a plugin page and for the catalog: the global search's big text
// with its caret, no box (the typed end stays in view; the caret blinks while it takes typing).
void Renderer::drawStoreField(float left, float top, float right, float bottom, const std::wstring& query, const std::wstring& placeholder, bool typing, size_t caret) {
    const float y = (top + bottom) * 0.5f - 35.5f, x = left + 6, room = right - x - 12;
    if (query.empty()) text(placeholder, x, y, room, 71, hero_font_.Get(), D2D1::ColorF(1, 1, 1, typing ? 0.5f : 0.25f)); // Half white while it takes typing, a quarter otherwise; as the global search: no caret before the first letter.
    else fieldText(query, std::min(caret, query.size()), typing && std::sin(elapsed_ * 4.0f) >= 0, x, y, room, 71, hero_font_.Get(), D2D1::ColorF(0.96f, 0.96f, 0.96f));
}

void Renderer::drawPluginPage(float origin, bool drawer) {
    const auto* content = drawer ? context_->plugin_drawer : context_->plugin_page;
    if (!content) return;
    const auto& page = *content;
    const float width = drawer ? friends_layout::drawer_width : viewport_.width;
    const plugin_ui::Layout layout{width, viewport_.height, drawer, page.dialog};
    if (layout.dialog) {
        brush(palette::gray(0, 0.65f)); target_->FillRectangle(D2D1::RectF(0, 0, width, viewport_.height), solid_.Get());
        brush(palette::gray(0.075f));
        target_->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(layout.left - 32, layout.panelTop(), width - layout.left + 32, layout.bottom() + 24), 24, 24), solid_.Get());
    }
    // A store page enters like the catalog beside it: its foreground eases in over the same backdrop.
    if (!drawer && !layout.dialog && !context_->store_tabs.empty()) beginScreenForeground();
    const auto white = D2D1::ColorF(0.95f, 0.95f, 0.95f);
    const float back_size = drawer ? 36.0f : 48.0f;
    const bool framed = layout.framed();
    const auto back = D2D1::RectF(origin + layout.backLeft(), layout.backTop(), origin + layout.backLeft() + back_size, layout.backTop() + back_size);
    // Back: a round glass button with a dot (the drawer is frosted already), white with the focus.
    const auto back_ink = drawButton(back, page.focus < 0 ? 1.0f : 0.0f, 1, false);
    brush(back_ink); target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F((back.left + back.right) * 0.5f, (back.top + back.bottom) * 0.5f), 3, 3), solid_.Get());
    if (!drawer && !context_->store_tabs.empty())
        drawCategoryStrip(context_->store_tabs, static_cast<float>(context_->store_tab), true, 1.0f, false, plugin_ui::Layout::tabs_drop, true); // The Stores' tabs take the title's place.
    else textLine(page.title, back.right + 14, (back.top + back.bottom) * 0.5f, origin + width - layout.left - back.right - 14, category_font_.Get(), white);
    text(page.status, origin + layout.left, layout.dialog ? layout.panelTop() + 94 : drawer ? 82.0f : plugin_ui::Layout::status_top, width - 2 * layout.left, 28, toast_sub_font_.Get(), D2D1::ColorF(0.65f, 0.65f, 0.65f));
    if (framed && !page.rows.empty() && page.rows.front().input) {
        const auto box = layout.field();
        drawStoreField(origin + box.left, box.top, origin + box.right, box.bottom, page.query, tr(L"Wpisz tytuł gry…"), true, page.query.size()); // Typing anywhere reaches it: always the active look.
    }
    target_->PushAxisAlignedClip(D2D1::RectF(origin + layout.left - 8, layout.top, origin + width - layout.left + 8, layout.bottom()), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    auto& images = drawer ? plugin_thumbnails_ : covers_;
    for (size_t i = 0; i < page.rows.size(); ++i) {
        const auto& row = page.rows[i]; const float y = layout.rowTop(page, i);
        if (y + row.height() < layout.top || y > layout.bottom()) continue;
        if (row.heading) {
            text(row.title, origin + layout.left + 6, y + 10, width - 2 * layout.left - 12, 36,
                 drawer ? friend_section_font_.Get() : friend_name_font_.Get(), D2D1::ColorF(0.66f, 0.75f, 0.73f)); continue;
        }
        if (row.input) {
            if (framed) continue; // Drawn once, in the frame, below.
            const bool typing = page.focus == static_cast<int>(i);
            const auto field = D2D1::RectF(origin + layout.left - 6, y + 4, origin + width - layout.left + 6, y + row.height() - 10);
            drawStoreField(field.left, field.top, field.right, field.bottom, page.query, row.title, typing, page.query.size());
            continue;
        }
        if (page.focus == static_cast<int>(i)) {
            // A translucent highlight: a pill on short rows, 16 px corners on taller ones.
            brush(palette::gray(1, 0.10f));
            const auto box = D2D1::RectF(origin + layout.left - 6, y, origin + width - layout.left + 6, y + row.height() - 6);
            target_->FillRoundedRectangle(box.bottom - box.top <= 64 ? pill(box) : D2D1::RoundedRect(box, 16, 16), solid_.Get());
        }
        float x = origin + layout.left + 6;
        if (!row.game_key.empty() || !row.download_id.empty()) {
            const float size = drawer ? 52.0f : !row.download_id.empty() ? 104.0f : 70.0f, top = y + (row.height() - size) * 0.5f - 3;
            const auto box = D2D1::RectF(x, top, x + size, top + size);
            const float radius = size * 0.1f;
            brush(D2D1::ColorF(0.12f, 0.15f, 0.18f)); target_->FillRoundedRectangle(D2D1::RoundedRect(box, radius, radius), solid_.Get());
            if (i < images.size()) fillImage(images[i], box, radius, reveal(images[i]), false);
            x += size + 18;
        }
        textGroup(row.title, row.hasSubtitle() ? row.subtitle : std::wstring{}, x, y + (row.height() - 6) * 0.5f,
                  origin + width - layout.left - 8 - x, drawer ? friend_name_font_.Get() : menu_font_.Get(), row.enabled ? white : D2D1::ColorF(0.45f, 0.45f, 0.45f),
                  friend_status_font_.Get(), D2D1::ColorF(0.6f, 0.6f, 0.6f), 1);
    }
    target_->PopAxisAlignedClip();
    const auto maximum = layout.maxScroll(page);
    if (maximum > 0) {
        const float track = layout.bottom() - layout.top, thumb = std::max(24.0f, track * track / (maximum + track));
        const float y = layout.top + (track - thumb) * std::clamp(page.scroll / maximum, 0.0f, 1.0f);
        brush(D2D1::ColorF(1, 1, 1, 0.25f));
        target_->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(origin + width - 7, y, origin + width - 4, y + thumb), 1.5f, 1.5f), solid_.Get());
    }
}
