#include "renderer.hpp"
#include "renderer_internal.hpp"
#include "controller_picture.hpp"

#include <algorithm>
#include <cmath>

namespace {
using palette::gray;
using render_detail::box;
// How far the gliding focus frame has reached a button (1 without the frame): the button's white
// pill fades in as the frame arrives. audio_renderer.cpp has the same.
float arrival(const std::optional<ViewContext::GlideBox>& glide, settings_ui::Rect r) {
    if (!glide) return 1;
    const float off = std::abs(glide->left - r.left) + std::abs(glide->top - r.top) + std::abs(glide->right - r.right) +
                      std::abs(glide->bottom - r.bottom);
    return std::clamp(1 - off / (2 * (r.bottom - r.top)), 0.0f, 1.0f);
}
// The focused item when it is a button (Back, "Sparuj kontroler" without pads, Audio's mute or a
// footer button), as focus_motion.cpp picks it; rows, the slider and tiles are not.
std::optional<settings_ui::Rect> focusedButton(const device_ui::State& state, float width, float height) {
    const device_ui::Layout layout{width, height, state.panel};
    if (state.selected < 0) return layout.back();
    if (state.selected >= static_cast<int>(state.rows.size())) return std::nullopt;
    if (state.panel == devices::Panel::audio) {
        if (state.rows[state.selected].kind == device_ui::Kind::mute || state.audioFooter(state.selected))
            return device_ui::AudioLayout{width, height}.row(state, state.selected);
        return std::nullopt;
    }
    if (!state.gallery() && state.panel == devices::Panel::controllers && state.data.controllers.empty()) return layout.controllerEmptyButton();
    return std::nullopt;
}
// Focus highlight corners: a pill while button-sized, 16 px on rows and tiles (blended between, so
// the gliding frame does not snap as it changes size).
D2D1_ROUNDED_RECT focusShape(D2D1_RECT_F r) {
    const float height = r.bottom - r.top, half = std::min(r.right - r.left, height) * 0.5f;
    const float radius = std::lerp(half, std::min(half, 16.0f), std::clamp((height - 58) / 8, 0.0f, 1.0f));
    return D2D1::RoundedRect(r, radius, radius);
}
}

void Renderer::drawDevices() {
    if (!context_->device_panel) return;
    const auto& state = *context_->device_panel;
    const device_ui::Layout layout{viewport_.width, viewport_.height, state.panel};
    drawBackground();
    drawFrostedBackdrop(1.1f); // As Settings: the background frosted under the panel.
    const auto white = palette::text(), muted = palette::muted();
    const auto back = layout.back();
    // With a gliding focus frame (ViewContext::device_glide) it lies under the items, which keep
    // their resting look; a button turns white as the frame reaches it. Buttons are glass pills
    // over the frosted backdrop.
    const bool frame = context_->device_glide.has_value();
    drawDeviceFocus(state.panel == devices::Panel::audio && state.audio_edit_column >= 0);
    const auto back_ink = drawButton(box(back), state.selected == -1 ? arrival(context_->device_glide, back) : 0.0f, 1, false);
    const float back_y = (back.top + back.bottom) * 0.5f;
    brush(back_ink); target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F((back.left + back.right) * 0.5f, back_y), 3.5f, 3.5f), solid_.Get());
    const wchar_t* title = state.panel == devices::Panel::controllers ? tr(L"Kontrolery") : state.panel == devices::Panel::audio ? tr(L"Dźwięk systemowy") : L"Bluetooth";
    // The heading sits on the back button's centre line.
    textLine(title, layout.left() + 88, back_y, layout.panelWidth() - 88, settings_heading_font_.Get(), white);
    if (state.panel == devices::Panel::audio) { drawAudioDevices(); return; }
    const auto draw_status = [&] {
        settings_body_font_->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
        text(state.data.status, layout.left(), viewport_.height - 86, viewport_.width - 2 * layout.left(), 70, settings_body_font_.Get(), muted);
        settings_body_font_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    };
    if (state.panel == devices::Panel::controllers && state.data.controllers.empty()) {
        const auto area = layout.controllerEmpty(), button = layout.controllerEmptyButton();
        drawControllerIcon(viewport_.width * 0.5f - 42, area.top + 12, 84, D2D1::ColorF(0.66f, 0.70f, 0.73f));
        textLine(tr(L"Brak podłączonych kontrolerów"), area.left, area.top + 135, area.right - area.left, category_font_.Get(), white,
                 DWRITE_TEXT_ALIGNMENT_CENTER);
        const auto ink = drawButton(box(button), state.selected == 0 ? arrival(context_->device_glide, button) : 0.0f, 1, false);
        const float inset = (button.bottom - button.top) * 0.5f - 6; // Clear of the rounded ends.
        textLine(tr(L"Sparuj kontroler"), button.left + inset, (button.top + button.bottom) * 0.5f, button.right - button.left - 2 * inset, action_font_.Get(), ink);
        draw_status(); return;
    }
    if (state.gallery()) { drawControllerGallery(); draw_status(); return; }
    target_->PushAxisAlignedClip(D2D1::RectF(layout.left() - 3, layout.list_top - 2, layout.left() + layout.listWidth() + 3, layout.bottom() + 2), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    for (int i = 0; i < static_cast<int>(state.rows.size()); ++i) {
        const auto r = layout.row(i, state.scroll);
        if (r.bottom < layout.list_top || r.top > layout.bottom()) continue;
        const auto& row = state.rows[i];
        const bool focus = i == state.selected && !frame;
        brush(gray(1, focus ? 0.12f : 0.025f)); // The focus without the frame: lighter, no outline.
        target_->FillRoundedRectangle(D2D1::RoundedRect(box(r), 16, 16), solid_.Get());
        const auto button = layout.controllerAction(i, state.scroll);
        const float text_right = row.action ? button.left - 16 : r.right - 64;
        const float y = (r.top + r.bottom) * 0.5f;
        textGroup(row.title, row.subtitle, r.left + 24, y, text_right - r.left - 24, menu_font_.Get(), white,
                  settings_body_font_.Get(), muted, 1);
        if (row.action) { // Lighter glass on the focused row.
            const auto ink = drawButton(box(button), 0, 1, false, i == state.selected, !state.data.busy);
            const float inset = (button.bottom - button.top) * 0.5f - 6;
            textLine(row.actionLabel(), button.left + inset, y, button.right - button.left - 2 * inset,
                action_font_.Get(), ink, DWRITE_TEXT_ALIGNMENT_CENTER);
        }
        if (row.checked) {
            const float x = r.right - 36;
            line(x - 7, y, x - 2, y + 5, white, 2);
            line(x - 2, y + 5, x + 8, y - 6, white, 2);
        }
    }
    target_->PopAxisAlignedClip();
    const auto preview = layout.preview();
    const float x = preview.left, w = preview.right - preview.left;
    if (state.panel == devices::Panel::controllers) {
        const auto* pad = state.selectedController();
        if (pad) {
            const auto key = L"controller:" + std::to_wstring(static_cast<int>(pad->picture));
            const auto found = platform_badges_.find(key);
            if (found != platform_badges_.end() && found->second.bitmap) {
                const auto size = found->second.bitmap->GetSize();
                const auto source = controllerPictureSource(pad->picture, size,
                    found->second.has_ink_bounds ? found->second.ink_bounds : D2D1::RectF(0, 0, size.width, size.height));
                const float sw = source.right - source.left, sh = source.bottom - source.top;
                const float scale = std::min(w / sw, 300.0f / sh);
                const float iw = sw * scale, ih = sh * scale;
                const float top = preview.top + (preview.bottom - preview.top - ih) * 0.5f;
                target_->DrawBitmap(found->second.bitmap.Get(), D2D1::RectF(x + (w - iw) * 0.5f, top, x + (w + iw) * 0.5f, top + ih),
                    1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, source);
            } else {
                // Drawn family picture: every model without a photo, the offline
                // fallback for the downloaded ones, and a neutral pad for unknown devices.
                const float height = std::min(300.0f, preview.bottom - preview.top);
                const float top = preview.top + (preview.bottom - preview.top - height) * 0.5f;
                drawControllerArt(pad->picture, D2D1::RectF(x, top, x + w, top + height));
            }
        }
    } else if (state.panel == devices::Panel::bluetooth && state.data.bluetooth.empty()) {
        details_body_font_->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
        text(tr(L"Wyszukiwanie urządzeń…"), layout.left() + 24, layout.list_top + layout.row_step + 24, layout.listWidth() - 48, 100, details_body_font_.Get(), muted);
        details_body_font_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    }
    draw_status();
}

// Kontrolery with pads present: the same tiles as Settings → Kontrolery, larger; the pads first,
// "Sparuj kontroler" last. The gliding focus frame (drawDeviceFocus) moves between the tiles.
void Renderer::drawControllerGallery() {
    const auto& state = *context_->device_panel;
    const device_ui::Layout layout{viewport_.width, viewport_.height, state.panel};
    const int count = static_cast<int>(state.rows.size()), focus = layout.focusTile(state);
    const int first = layout.firstTile(count, focus), end = std::min(count, first + layout.visibleTiles());
    for (int which = first; which < end; ++which) {
        const int index = state.rowOf(which);
        const auto& row = state.rows[index];
        const auto t = layout.tile(which, count, focus);
        const bool focused = state.selected == index;
        // The tile's card and its state's dot are drawPadTile's; its focus frame drawPadFocus's below.
        settings_ui::Tile tile;
        if (row.kind == device_ui::Kind::bluetooth) {
            tile.pair = true; tile.title = row.title; tile.caption = L"Bluetooth"; tile.action = tr(L"Sparuj");
        } else {
            tile.title = row.title;
            for (const auto& pad : state.data.controllers) {
                if (row.id != pad.id && row.id != L"controller:" + pad.bluetooth_id) continue;
                tile.caption = pad.connection; tile.picture = pad.picture; tile.connected = pad.connected;
                tile.battery = pad.connected ? pad.battery : std::nullopt; tile.charging = pad.charging;
                tile.value = pad.connected ? devices::batteryLabel(pad) : std::wstring(tr(L"Rozłączono"));
                break;
            }
            if (row.action) tile.action = row.actionLabel();
        }
        drawPadTile(tile, t, focused);
    }
    // Only the focused tile is framed, as in Settings → Kontrolery (Back or a footer button: none).
    for (int which = first; which < end; ++which)
        if (state.selected == state.rowOf(which)) drawPadFocus(layout.tile(which, count, focus));
    // More pads than fit: chevrons beside the gallery.
    const float y = layout.list_top + device_ui::Layout::tile_height * 0.5f;
    const auto chevron = [&](float x, int direction) {
        const auto ink = D2D1::ColorF(0.75f, 0.75f, 0.77f);
        line(x - 5 * direction, y - 8, x + 3 * direction, y, ink, 2); line(x + 3 * direction, y, x - 5 * direction, y + 8, ink, 2);
    };
    if (first > 0) chevron(layout.left() - 22, -1);
    if (end < count) chevron(layout.left() + layout.panelWidth() + 18, 1);
}

// The devices screen's gliding focus frame (ViewContext::device_glide), drawn before the items:
// a translucent fill without an outline under the item it reaches (Back, a row, a tile, or in
// Audio the slider, the mute button, an output/input or a footer button), fainter while the
// slider is edited. On a button it fades out as the button's own white pill fades in.
void Renderer::drawDeviceFocus(bool editing) {
    if (!context_->device_glide || !context_->device_panel) return;
    const auto& glide = *context_->device_glide;
    // The controller tiles show their focus themselves (a brighter border): no grey fill under them.
    if (context_->device_panel->gallery() && context_->device_panel->selected >= 0) return;
    if (glide.right - glide.left < 2 || glide.bottom - glide.top < 2) return;
    const auto button = focusedButton(*context_->device_panel, viewport_.width, viewport_.height);
    const float alpha = (editing ? 0.05f : 0.12f) * (button ? 1 - arrival(context_->device_glide, *button) : 1.0f);
    if (alpha < 0.004f) return;
    brush(gray(1, alpha));
    target_->FillRoundedRectangle(focusShape(D2D1::RectF(glide.left, glide.top, glide.right, glide.bottom)), solid_.Get());
}
