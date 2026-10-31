// Settings 2.0: tiled rows (sound sources, controller gallery) and the category icons of the rail.
// Geometry: settings_ui::Layout::tile (shared with clicks and the focus).
#include "renderer.hpp"
#include "renderer_internal.hpp"
#include "controller_picture.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace {
using palette::gray;
using render_detail::box;
}

void Renderer::drawControllerPicture(devices::Picture picture, D2D1_RECT_F area) {
    const float w = area.right - area.left, h = area.bottom - area.top;
    if (w < 4 || h < 4) return;
    if (picture == devices::Picture::none) picture = devices::Picture::generic;
    const auto found = platform_badges_.find(L"controller:" + std::to_wstring(static_cast<int>(picture)));
    if (found != platform_badges_.end() && found->second.bitmap) {
        // The downloaded photo, trimmed to the pad itself.
        const auto size = found->second.bitmap->GetSize();
        const auto source = controllerPictureSource(picture, size,
            found->second.has_ink_bounds ? found->second.ink_bounds : D2D1::RectF(0, 0, size.width, size.height));
        const float sw = source.right - source.left, sh = source.bottom - source.top;
        if (sw <= 0 || sh <= 0) return;
        const float scale = std::min(w / sw, h / sh), iw = sw * scale, ih = sh * scale;
        const float x = area.left + (w - iw) * 0.5f, y = area.top + (h - ih) * 0.5f;
        target_->DrawBitmap(found->second.bitmap.Get(), D2D1::RectF(x, y, x + iw, y + ih), 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, source);
        return;
    }
    // Every family has drawn art (a neutral pad for unknown devices); keep its 440 x 300 proportions.
    const float aspect = 440.0f / 300.0f;
    const float dw = std::min(w, h * aspect), dh = dw / aspect;
    const float x = area.left + (w - dw) * 0.5f, y = area.top + (h - dh) * 0.5f;
    drawControllerArt(picture, D2D1::RectF(x, y, x + dw, y + dh));
}

void Renderer::drawSettingsTiles(const settings_ui::Layout& layout, const settings_ui::Row& row, int index, float scroll, bool focused) {
    using namespace settings_ui;
    const auto& state = context_->settings_state;
    const int count = static_cast<int>(row.tiles.size());
    if (!count) return;
    const int focus = std::clamp(state.tile, 0, count - 1);
    const auto fill = [&](Rect r, D2D1_COLOR_F ink, float radius) {
        brush(ink); target_->FillRoundedRectangle(D2D1::RoundedRect(box(r), radius, radius), solid_.Get());
    };
    const auto outline = [&](Rect r, D2D1_COLOR_F ink, float thickness, float radius) {
        brush(ink); target_->DrawRoundedRectangle(D2D1::RoundedRect(box(r), radius, radius), solid_.Get(), thickness);
    };
    const auto chevron = [&](float x, float y, int direction, D2D1_COLOR_F ink) {
        line(x - 4 * direction, y - 6, x + 2 * direction, y, ink, 1.8f);
        line(x + 2 * direction, y, x - 4 * direction, y + 6, ink, 1.8f);
    };
    const auto accent = uiAccent();
    const auto body = settings_body_font_.Get();
    const int first = layout.firstTile(row.control, count, focus), end = std::min(count, first + layout.visibleTiles(row.control, count));
    for (int which = first; which < end; ++which) {
        const auto& tile = row.tiles[which];
        const auto t = layout.tile(index, scroll, row.control, which, count, focus);
        const bool lit = focused && which == focus;
        if (row.control == Control::sources) {
            // A card: the focused one lighter (accent-tinted while editing), without a focus outline.
            fill(t, lit ? (state.editing ? uiAccent(0.16f) : gray(1, 0.14f)) : gray(1, 0.042f), 16);
            outline(t, gray(1, 0.08f), 1.0f, 16);
            // Left: the direction's icon in a disc; the caption over the default device; its volume below.
            const float cx = t.left + 60, cy = t.top + 66;
            brush(gray(1, 0.07f)); target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), 32, 32), solid_.Get());
            D2D1_MATRIX_3X2_F previous{}; target_->GetTransform(&previous);
            target_->SetTransform(D2D1::Matrix3x2F::Scale(1.5f, 1.5f, D2D1::Point2F(cx, cy)) * previous);
            drawAudioIcon(cx, cy, tile.input, tile.muted, tile.muted ? accent : gray(0.9f));
            target_->SetTransform(previous);
            const float text_left = t.left + 116, text_right = t.right - (tile.action.empty() ? 24 : 128);
            textLine(tile.caption, text_left, t.top + 38, text_right - text_left, body, gray(0.55f));
            textGroup(tile.title, tile.value, text_left, t.top + 56, text_right - text_left,
                      category_font_.Get(), gray(0.96f), toast_sub_font_.Get(), gray(0.52f), 1, true);
            if (!tile.action.empty()) {
                const Rect change{t.right - 116, t.top + 22, t.right - 20, t.top + 62};
                // A glass pill (Settings is frosted already), white while its tile has the focus.
                const auto ink = drawButton(box(change), lit && !state.editing ? 1.0f : 0.0f, 1, false);
                textLine(tile.action, change.left + 14, (change.top + change.bottom) * 0.5f, change.right - change.left - 40, body, ink);
                chevron(change.right - 18, (change.top + change.bottom) * 0.5f, 1, ink);
            }
            const float y = t.bottom - 34, bar_left = t.left + 26, bar_right = t.right - 128;
            fill({bar_left, y - 2, bar_right, y + 2}, gray(1, tile.level >= 0 ? 0.14f : 0.06f), 2);
            if (tile.level >= 0) {
                const float end_x = std::lerp(bar_left, bar_right, std::clamp(tile.level, 0.0f, 1.0f));
                if (end_x > bar_left) fill({bar_left, y - 2, end_x, y + 2}, tile.muted ? gray(0.45f) : accent, 2);
                brush(gray(0.95f)); target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(end_x, y), 5, 5), solid_.Get());
            }
            const std::wstring level = tile.level < 0 ? L"—" : tile.muted ? std::wstring(tr(L"Wyciszone")) :
                                       std::to_wstring(static_cast<int>(std::lround(tile.level * 100))) + L"%";
            textLine(level, bar_right + 12, y, t.right - bar_right - 32, body, gray(0.8f), DWRITE_TEXT_ALIGNMENT_TRAILING);
            continue;
        }
        drawPadTile(tile, t, lit);
    }
    if (row.control == Control::pads) {
        // More pads than fit: chevrons at the gallery's edges.
        const auto r = layout.row(index, scroll);
        const float y = (r.top + r.bottom) * 0.5f;
        if (first > 0) chevron(r.left - 18, y, -1, gray(0.75f));
        if (end < count) chevron(r.right + 12, y, 1, gray(0.75f));
    }
}

// One controller tile (Settings → Kontrolery and the Kontrolery screen): no fill, a border in the
// pad's state (green connected, red not: a light travelling round it, brighter with the focus) and
// a dot of that colour in the corner; the picture (photo or drawn family; a "+" for pairing), battery,
// name and status, then its action button. `lit` marks the focused tile.
void Renderer::drawPadTile(const settings_ui::Tile& tile, settings_ui::Rect t, bool lit) {
    using namespace settings_ui;
    const auto accent = uiAccent();
    {
        const auto green = D2D1::ColorF(0.30f, 0.82f, 0.42f), red = D2D1::ColorF(0.93f, 0.33f, 0.30f);
        const auto state = tile.connected ? green : red;
        Microsoft::WRL::ComPtr<ID2D1RoundedRectangleGeometry> shape;
        if (tile.pair) {
            brush(gray(1, lit ? 0.5f : 0.14f));
            target_->DrawRoundedRectangle(D2D1::RoundedRect(box(t), 16, 16), solid_.Get(), lit ? 1.6f : 1.0f);
        } else if (SUCCEEDED(factory_->CreateRoundedRectangleGeometry(D2D1::RoundedRect(box(t), 16, 16), shape.GetAddressOf()))) {
            brush(D2D1::ColorF(state.r, state.g, state.b, lit ? 0.55f : 0.28f));
            target_->DrawGeometry(shape.Get(), solid_.Get(), lit ? 1.6f : 1.0f);
            drawOrbitOutline(shape.Get(), state, lit ? 1.0f : 0.55f, lit ? 3.0f : 2.2f);
        }
        if (!tile.pair) {
            const float dx = t.left + 22, dy = t.top + 22;
            brush(D2D1::ColorF(state.r, state.g, state.b, 0.25f)); target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(dx, dy), 9, 9), solid_.Get());
            brush(state); target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(dx, dy), 5, 5), solid_.Get());
        }
    }
    // Larger tiles give the picture the extra height; the texts and the button keep their place below it.
    const float picture_bottom = t.top + std::max(106.0f, (t.bottom - t.top) - 130);
    const D2D1_RECT_F picture = D2D1::RectF(t.left + 18, t.top + 16, t.right - 18, picture_bottom);
    if (tile.pair) {
        const float cx = (picture.left + picture.right) * 0.5f, cy = (picture.top + picture.bottom) * 0.5f;
        const float radius = std::min(56.0f, (picture.bottom - picture.top) * 0.4f);
        brush(gray(1, 0.06f)); target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), radius, radius), solid_.Get());
        brush(gray(1, lit ? 0.7f : 0.3f)); target_->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), radius, radius), solid_.Get(), 1.4f);
        const float arm = radius * 0.32f;
        line(cx - arm, cy, cx + arm, cy, gray(0.92f), 2.6f);
        line(cx, cy - arm, cx, cy + arm, gray(0.92f), 2.6f);
    } else {
        // Disconnected or not, the pad is shown as it is: the border and the dot tell its state.
        drawControllerPicture(tile.picture, picture);
        if (tile.battery) {
            // Battery outline with its level, top-right over the picture.
            const float bx = t.right - 50, by = t.top + 16;
            brush(gray(0.85f)); target_->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(bx, by, bx + 26, by + 13), 3, 3), solid_.Get(), 1.3f);
            target_->FillRectangle(D2D1::RectF(bx + 26.5f, by + 4, bx + 29, by + 9), solid_.Get());
            const float level = std::clamp(*tile.battery / 100.0f, 0.0f, 1.0f);
            brush(*tile.battery <= 15 ? D2D1::ColorF(0.95f, 0.42f, 0.36f) : tile.charging ? accent : gray(0.9f));
            if (level > 0) target_->FillRectangle(D2D1::RectF(bx + 2.5f, by + 2.5f, bx + 2.5f + 21 * level, by + 10.5f), solid_.Get());
        }
    }
    const float text_left = t.left + 18, text_width = t.right - t.left - 36;
    textLine(tile.title, text_left, picture_bottom + 22, text_width, menu_font_.Get(), gray(0.96f));
    const std::wstring status = tile.pair ? tile.value : tile.caption.empty() ? tile.value : tile.caption + L" · " + tile.value;
    if (!status.empty()) textLine(status, text_left, picture_bottom + 48, text_width, toast_sub_font_.Get(), gray(0.55f));
    if (!tile.action.empty()) {
        const Rect action{t.left + 16, t.bottom - 58, t.right - 16, t.bottom - 16};
        // A glass pill (its panel is frosted already), white on the focused tile.
        const auto ink = drawButton(box(action), lit ? 1.0f : 0.0f, 1, false);
        const float inset = (action.bottom - action.top) * 0.5f; // Clear of the rounded ends.
        textLine(tile.action, action.left + inset, (action.top + action.bottom) * 0.5f, action.right - action.left - 2 * inset,
                 settings_body_font_.Get(), ink, DWRITE_TEXT_ALIGNMENT_CENTER);
    }
}

void Renderer::drawSettingsIcon(int category, float cx, float cy, D2D1_COLOR_F ink) {
    using settings_ui::Category;
    brush(ink);
    const auto rounded = [&](float l, float t, float r, float b, float radius, float width = 1.8f) {
        target_->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(cx + l, cy + t, cx + r, cy + b), radius, radius), solid_.Get(), width);
    };
    switch (static_cast<Category>(category)) {
    case Category::appearance: // A screen split into a sidebar and a content area.
        rounded(-11, -9, 11, 9, 3);
        line(cx - 4, cy - 9, cx - 4, cy + 9, ink, 1.8f);
        line(cx, cy - 2, cx + 7, cy - 2, ink, 1.6f);
        line(cx, cy + 3, cx + 5, cy + 3, ink, 1.6f);
        break;
    case Category::library: // 2 x 2 tiles, as the quick bar's library.
        for (int tile = 0; tile < 4; ++tile) {
            const float x = (tile % 2 ? 1.5f : -10.0f), y = (tile / 2 ? 1.5f : -10.0f);
            rounded(x, y, x + 8.5f, y + 8.5f, 2.2f, 1.7f);
        }
        break;
    case Category::media: { // A frame with a play triangle.
        rounded(-11, -8, 11, 8, 3);
        line(cx - 3, cy - 4, cx - 3, cy + 4, ink, 1.7f);
        line(cx - 3, cy - 4, cx + 4, cy, ink, 1.7f);
        line(cx + 4, cy, cx - 3, cy + 4, ink, 1.7f);
        break;
    }
    case Category::audio: // A speaker with two waves.
        target_->DrawRectangle(D2D1::RectF(cx - 10, cy - 4, cx - 5, cy + 4), solid_.Get(), 1.7f);
        line(cx - 5, cy - 4, cx + 1, cy - 10, ink, 1.7f); line(cx + 1, cy - 10, cx + 1, cy + 10, ink, 1.7f);
        line(cx + 1, cy + 10, cx - 5, cy + 4, ink, 1.7f);
        for (int wave = 0; wave < 2; ++wave) {
            const float left = cx + 5 + wave * 4.0f, reach = 3 + wave * 1.5f, half = 4 + wave * 3.0f;
            line(left, cy - half, left + reach, cy, ink, 1.6f);
            line(left + reach, cy, left, cy + half, ink, 1.6f);
        }
        break;
    case Category::controllers: // A pad: body, D-pad and two buttons.
        rounded(-12, -7, 12, 8, 6);
        line(cx - 8, cy, cx - 2, cy, ink, 1.6f); line(cx - 5, cy - 3, cx - 5, cy + 3, ink, 1.6f);
        target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx + 4, cy - 1.5f), 1.6f, 1.6f), solid_.Get());
        target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx + 8, cy + 2), 1.6f, 1.6f), solid_.Get());
        break;
    case Category::plugins: // A puzzle piece: a square with a knob on top and on the right.
        rounded(-10, -6, 6, 10, 2.5f);
        target_->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx - 2, cy - 8.5f), 3, 3), solid_.Get(), 1.6f);
        target_->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx + 8.5f, cy + 2), 3, 3), solid_.Get(), 1.6f);
        break;
    case Category::companion: // A chat bubble.
        rounded(-11, -9, 11, 6, 4);
        line(cx - 6, cy + 6, cx - 6, cy + 11, ink, 1.8f);
        line(cx - 6, cy + 11, cx, cy + 6, ink, 1.8f);
        for (int dot = -1; dot <= 1; ++dot)
            target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx + dot * 5.0f, cy - 1.5f), 1.3f, 1.3f), solid_.Get());
        break;
#ifndef _WIN32
    case Category::compat: // A program window over another: Windows programs on Linux.
        rounded(-11, -4, 5, 10, 2.5f);
        line(cx - 11, cy, cx + 5, cy, ink, 1.6f);
        line(cx - 5, cy - 4, cx - 5, cy - 9, ink, 1.7f); line(cx - 5, cy - 9, cx + 11, cy - 9, ink, 1.7f);
        line(cx + 11, cy - 9, cx + 11, cy + 5, ink, 1.7f); line(cx + 11, cy + 5, cx + 5, cy + 5, ink, 1.7f);
        break;
#endif
    case Category::system: // A cog.
        target_->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), 7, 7), solid_.Get(), 1.7f);
        target_->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), 2.6f, 2.6f), solid_.Get(), 1.5f);
        for (int spoke = 0; spoke < 8; ++spoke) {
            const float angle = spoke * std::numbers::pi_v<float> / 4;
            line(cx + std::cos(angle) * 8, cy + std::sin(angle) * 8, cx + std::cos(angle) * 11.5f, cy + std::sin(angle) * 11.5f, ink, 2.2f);
        }
        break;
    }
}
