// Her cards: the glow and focus of a companion card, the picker of companions, the affinity hearts
// and level, and the look previews in Settings -> Asystent.
#include "renderer.hpp"
#include "renderer_internal.hpp"

#include <algorithm>
#include <cmath>

using Microsoft::WRL::ComPtr;

namespace {
using palette::gray;
using render_detail::box;
using render_detail::smoothFade;
}

namespace {
D2D1_COLOR_F affinityPink(float alpha) { return D2D1::ColorF(1.0f, 0.47f, 0.64f, alpha); }
}

void Renderer::drawCompanionCardGlow(D2D1_RECT_F rect, companion::Look look, float alpha) {
    const auto tint = companion_ui::tint(look);
    const float w = rect.right - rect.left, h = rect.bottom - rect.top;
    const D2D1_GRADIENT_STOP stops[] = {
        {0, D2D1::ColorF(tint.r, tint.g, tint.b, 0.42f * alpha)},
        {0.48f, D2D1::ColorF(tint.r, tint.g, tint.b, 0.18f * alpha)},
        {1, D2D1::ColorF(tint.r, tint.g, tint.b, 0)}};
    ComPtr<ID2D1GradientStopCollection> collection;
    ComPtr<ID2D1RadialGradientBrush> glow;
    if (SUCCEEDED(target_->CreateGradientStopCollection(stops, 3, collection.GetAddressOf())) &&
        SUCCEEDED(target_->CreateRadialGradientBrush(D2D1::RadialGradientBrushProperties(
            D2D1::Point2F(rect.left + w * 0.5f, rect.top + h * 0.48f), {}, w * 0.62f, h * 0.58f), collection.Get(), glow.GetAddressOf())))
        target_->FillRoundedRectangle(D2D1::RoundedRect(rect, 12, 12), glow.Get());
}

// A companion card: the interface's tile shape with the compact corners of the frame around it.
ComPtr<ID2D1Geometry> Renderer::companionCardShape(D2D1_RECT_F rect) {
    const float corner = squircle_corner_, reference = corner_reference_;
    squircle_corner_ = 0.4f; corner_reference_ = 64;
    auto shape = tileShape(rect, tile_shape_);
    squircle_corner_ = corner; corner_reference_ = reference;
    return shape;
}

void Renderer::drawCompanionCardFocus(D2D1_RECT_F rect, int surface, float alpha) {
    auto& frame = companion_card_focus_[static_cast<size_t>(surface)];
    auto& previous = companion_card_frame_[static_cast<size_t>(surface)];
    const float dt = elapsed_ - previous;
    if (previous < 0 || dt < 0 || dt > 0.2f || screen_changed_)
        frame.snap(rect.left, rect.top, rect.right, rect.bottom);
    else frame.step(rect.left, rect.top, rect.right, rect.bottom, std::min(dt, 0.05f), spring::glide);
    previous = elapsed_;
    // Around the card's own shape (companionCardShape), its corners grown by the same distance so
    // the two stay concentric; the focus gap moves it further out.
    const float out = 3 + focusOutset(1.8f);
    const auto current = D2D1::RectF(frame.left.value - out, frame.top.value - out, frame.right.value + out, frame.bottom.value + out);
    const float corner = squircle_corner_, reference = corner_reference_;
    squircle_corner_ = 0.4f; corner_reference_ = 64 + out / 0.4f;
    ComPtr<ID2D1Geometry> shape = tileShape(current, tile_shape_);
    squircle_corner_ = corner; corner_reference_ = reference;
    if (surface == 1) {
        // The desktop window's cards keep its own 10 px corners: the frame follows them.
        ComPtr<ID2D1RoundedRectangleGeometry> rounded;
        if (SUCCEEDED(factory_->CreateRoundedRectangleGeometry(D2D1::RoundedRect(current, 10 + out, 10 + out), rounded.GetAddressOf()))) shape = rounded;
    }
    if (!shape) return;
    // As a focused cover's (Interfejs → Animacja poświaty): the travelling highlight on one thin
    // outline, else the outline with its glow, pulsing when the glow pulses.
    if (glow_trace_) {
        brush(uiAccent(0.72f * alpha));
        target_->DrawGeometry(shape.Get(), solid_.Get(), 1.8f);
        drawOrbitOutline(shape.Get(), uiAccent(), alpha, 2.0f);
    } else drawCoverOutline(shape.Get(), uiAccent(), alpha, true);
}

void Renderer::drawCompanionPicker() {
    const auto* view = context_->companion;
    if (!view || !view->picker) return;
    const auto& picker = *view->picker;
    using L = companion_ui::PickerLayout;
    const L layout{viewport_.width, viewport_.height};
    const auto panel = box(layout.panel(picker));
    const auto shape = D2D1::RoundedRect(panel, desktop_style_ ? 14.0f : 22.0f, desktop_style_ ? 14.0f : 22.0f);
    const auto glass = glassPanel(shape);
    brush(gray(0, 0.6f)); target_->FillRectangle(D2D1::RectF(0, 0, viewport_.width, viewport_.height), solid_.Get());
    glassFill(glass, D2D1::ColorF(0.035f, 0.038f, 0.05f, 0.97f));
    brush(gray(1, 0.16f)); target_->DrawRoundedRectangle(shape, solid_.Get(), 1);
    textLine(tr(L"Wybierz towarzyszkę"), panel.left + L::padding, panel.top + 37, panel.right - panel.left - 2 * L::padding,
             settings_heading_font_.Get(), gray(0.97f));
    const int first = layout.first(picker), count = static_cast<int>(picker.choices.size());
    for (int i = first; i < std::min(first + L::page_size, count); ++i) {
        const auto look = picker.choices[static_cast<size_t>(i)];
        const auto* character = companion::character(look);
        if (!character) continue;
        const auto r = box(layout.tile(picker, i));
        const bool chosen = look == view->look, focused = !picker.cancel_focus && picker.focus == i, hovered = picker.hover == i;
        const auto tint = companion_ui::tint(look);
        const D2D1_COLOR_F color = D2D1::ColorF(tint.r, tint.g, tint.b);
        // The interface's tile shape, concentric with the gliding focus frame (drawCompanionCardFocus).
        const auto card_shape = companionCardShape(r);
        if (!card_shape) continue;
        brush(gray(1, focused ? 0.12f : hovered ? 0.09f : 0.045f));
        target_->FillGeometry(card_shape.Get(), solid_.Get());
        target_->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), card_shape.Get()), nullptr);
        drawCompanionCardGlow(r, look);
        target_->PopLayer();
        const auto thumb = D2D1::RectF(r.left + 4, r.top + 4, r.right - 4, r.bottom - 60);
        target_->PushAxisAlignedClip(thumb, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        const int index = static_cast<int>(look);
        const avatar::Pose pose = focused && index >= 0 && index < static_cast<int>(view->previews.size()) ? view->previews[static_cast<size_t>(index)] : avatar::Pose{};
        const int costume = index >= 0 && index < static_cast<int>(view->preview_costumes.size()) ? view->preview_costumes[static_cast<size_t>(index)] : 0;
        drawCompanionFigure(look, pose, (r.left + r.right) * 0.5f, thumb.bottom + 10, 176, 1, costume);
        target_->PopAxisAlignedClip();
        textLine(companion::name(look), r.left + 12, r.bottom - 39, r.right - r.left - 24, settings_body_font_.Get(), color, DWRITE_TEXT_ALIGNMENT_CENTER);
        textLine(chosen ? tr(L"Wybrana") : companion::personaName(character->persona), r.left + 12, r.bottom - 17, r.right - r.left - 24,
                 friend_status_font_.Get(), chosen ? color : gray(0.6f), DWRITE_TEXT_ALIGNMENT_CENTER);
        if (!focused) {
            brush(gray(1, hovered ? 0.25f : 0.08f));
            target_->DrawGeometry(card_shape.Get(), solid_.Get(), 1);
        }
    }
    if (!picker.cancel_focus && count) drawCompanionCardFocus(box(layout.tile(picker, picker.focus)), 2);
    const auto cancel = box(layout.button(picker, L::cancel));
    const auto ink = drawButton(cancel, picker.cancel_focus ? 1.0f : 0.0f, 1, false, picker.hover == L::cancel);
    textLine(tr(L"Anuluj"), cancel.left, (cancel.top + cancel.bottom) * 0.5f, cancel.right - cancel.left, settings_body_font_.Get(), ink, DWRITE_TEXT_ALIGNMENT_CENTER);
    if (count > L::page_size) {
        for (const int button : {L::previous, L::next}) {
            const auto r = box(layout.button(picker, button));
            const bool enabled = button == L::previous ? first > 0 : first + L::page_size < count;
            const auto color = drawButton(r, 0, 1, false, picker.hover == button, enabled);
            textLine(button == L::previous ? L"‹" : L"›", r.left, (r.top + r.bottom) * 0.5f, r.right - r.left,
                     settings_heading_font_.Get(), color, DWRITE_TEXT_ALIGNMENT_CENTER);
        }
        textLine(std::to_wstring(first / L::page_size + 1) + L" / " + std::to_wstring((count + L::page_size - 1) / L::page_size),
                 panel.left + L::padding + 134, panel.bottom - 44, 120, friend_status_font_.Get(), gray(0.65f));
    }
}

float Renderer::drawCompanionAffinity(float left, float cy, float alpha, bool draw, int shown_level, float shown_progress) {
    const auto* view = context_->companion;
    const bool own = shown_level <= 0; // The host's badge: her streak and level-up glow too.
    if (own && (!view || !view->affinity)) return 0;
    const int level_value = own ? view->affinity_level : shown_level;
    const float progress = own ? view->affinity_progress : shown_progress;
    const int streak = own ? view->streak : 0;
    const float pulse = own ? smoothFade(view->affinity_pulse) : 0.0f;
    // Her card for the player (companionBoundary), still live: yellow, then red.
    const int card = own ? view->affinity_card : 0;
    const float card_pulse = own && card ? smoothFade(view->affinity_card_pulse) : 0.0f;
    const auto cardColor = [&](float a) { return card == 2 ? D2D1::ColorF(0.94f, 0.24f, 0.24f, a) : D2D1::ColorF(1.0f, 0.82f, 0.2f, a); };
    auto* font = friend_status_font_.Get();
    const std::wstring level = L"Lv " + std::to_wstring(level_value);
    // A streak of days in a row (from two): a flame and the count.
    const std::wstring flame = streak >= 2 ? L"  \U0001F525" + std::to_wstring(streak) : L"";
    const float level_w = textAdvance(level, font), flame_w = flame.empty() ? 0.0f : textAdvance(flame, font);
    constexpr float padding = 12, height = 30, card_gap = 8, card_w = 9, card_h = 13;
    const float card_room = card ? card_gap + card_w : 0.0f;
    const float width = padding * 2 + level_w + flame_w + card_room;
    if (!draw || alpha <= 0.003f) return width;
    const auto rect = D2D1::RectF(left, cy - height * 0.5f, left + width, cy + height * 0.5f);
    // A new level: a pink glow swells and fades around the badge; her card: the same in its colour.
    if (pulse > 0.003f) {
        const float grow = (1 - pulse) * 10 + 2;
        brush(affinityPink(0.45f * pulse * alpha));
        target_->FillRoundedRectangle(pill(D2D1::RectF(rect.left - grow, rect.top - grow, rect.right + grow, rect.bottom + grow)), solid_.Get());
    }
    if (card_pulse > 0.003f) {
        const float grow = (1 - card_pulse) * 10 + 2;
        brush(cardColor(0.4f * card_pulse * alpha));
        target_->FillRoundedRectangle(pill(D2D1::RectF(rect.left - grow, rect.top - grow, rect.right + grow, rect.bottom + grow)), solid_.Get());
    }
    // A glass pill as the interface's buttons (frosted, translucent); a new level still lights its edge.
    drawButtonGlass(rect, alpha, true, false, true);
    if (pulse > 0.003f) { brush(affinityPink(0.9f * pulse * alpha)); target_->DrawRoundedRectangle(pill(rect), solid_.Get(), 1.0f + pulse); }
    const bool bar = progress >= 0;
    const float text_y = bar ? cy - 2 : cy;
    textLine(level, rect.left + padding, text_y, level_w + 4, font, buttonInk(0, true, alpha));
    if (!flame.empty()) textLine(flame, rect.left + padding + level_w, text_y, flame_w + 4, font, D2D1::ColorF(1.0f, 0.66f, 0.3f, alpha));
    if (card) { // A referee's card, a little tilted, after the level and the streak.
        const float x = rect.left + padding + level_w + flame_w + card_gap + card_w * 0.5f;
        const auto shape = D2D1::RoundedRect(D2D1::RectF(x - card_w * 0.5f, text_y - card_h * 0.5f, x + card_w * 0.5f, text_y + card_h * 0.5f), 1.6f, 1.6f);
        D2D1_MATRIX_3X2_F transform{};
        target_->GetTransform(&transform);
        target_->SetTransform(D2D1::Matrix3x2F::Rotation(12, D2D1::Point2F(x, text_y)) * transform);
        brush(cardColor(alpha)); target_->FillRoundedRectangle(shape, solid_.Get());
        brush(gray(1, 0.35f * alpha)); target_->DrawRoundedRectangle(shape, solid_.Get(), 0.8f);
        target_->SetTransform(transform);
    }
    if (!bar) return width;
    // Her sympathy towards the next level: a thin bar along the badge's bottom.
    const float bar_left = rect.left + padding, bar_right = rect.right - padding, bar_y = rect.bottom - 6;
    brush(gray(1, 0.16f * alpha)); target_->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(bar_left, bar_y - 1.5f, bar_right, bar_y + 1.5f), 1.5f, 1.5f), solid_.Get());
    const float filled = bar_left + (bar_right - bar_left) * std::clamp(progress, 0.0f, 1.0f);
    if (filled > bar_left + 0.5f) {
        brush(affinityPink(alpha));
        target_->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(bar_left, bar_y - 1.5f, filled, bar_y + 1.5f), 1.5f, 1.5f), solid_.Get());
    }
    return width;
}

// Settings → Asystent → "Postać": only the chosen card animates.
void Renderer::drawCompanionPreviews(const settings_ui::Layout& layout, const settings_ui::Row& row, int index, float scroll, bool focused) {
    const auto r = layout.row(index, scroll);
    textGroup(row.title, row.description, r.left + 24, r.top + 22, r.right - r.left - 48, category_font_.Get(), gray(0.93f),
              settings_body_font_.Get(), gray(0.52f), 1, true);
    const auto& list = companion::characters();
    const int count = static_cast<int>(list.size());
    const int first = settings_ui::Layout::avatarFirst(row.choice, count);
    // The slider glides to its new first card, like the tiles of a shelf, instead of jumping.
    {
        const float dt = elapsed_ - companion_strip_time_;
        if (companion_strip_time_ < 0 || dt < 0 || dt > 0.2f || screen_changed_) companion_strip_first_ = static_cast<float>(first);
        else companion_strip_first_ += (first - companion_strip_first_) * (1 - std::exp(-std::min(dt, 0.05f) * 13.0f));
        if (std::abs(companion_strip_first_ - first) < 0.002f) companion_strip_first_ = static_cast<float>(first);
        companion_strip_time_ = elapsed_;
    }
    const float shown_first = companion_strip_first_;
    const auto* view = context_->companion;
    const auto at = [](const std::vector<int>& values, size_t i) { return i < values.size() ? values[i] : 0; };
    const auto band = layout.row(index, scroll);
    target_->PushAxisAlignedClip(box(settings_ui::Rect{band.left + 18, band.top + 70, band.right - 18, band.bottom - 8}), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    for (int option = 0; option < count; ++option) {
        const auto rect = layout.avatarSlide(index, scroll, option, count, shown_first);
        if (rect.right <= rect.left) continue; // Out of view: the slider shows it when it comes near.
        const auto tile = box(rect);
        const auto& character = list[static_cast<size_t>(option)];
        const auto look = static_cast<companion::Look>(option);
        const bool chosen = row.choice == option;
        // The cards take the interface's tile shape (squircle, rounded or square), as their frame does.
        const auto clip = companionCardShape(tile);
        if (!clip) continue;
        brush(gray(1, chosen ? 0.085f : 0.04f)); target_->FillGeometry(clip.Get(), solid_.Get());
        {
            target_->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), clip.Get()), nullptr);
            drawCompanionCardGlow(tile, look, character.soon ? 0.4f : 1.0f);
            const float height = (tile.bottom - tile.top) * 1.02f;
            const float figure_x = tile.right - (tile.right - tile.left) * 0.3f;
            if (character.soon) {
                // No pictures yet: her colour's glow and a question mark where she will stand.
                textLine(L"?", figure_x - 60, tile.bottom - height * 0.45f, 120, category_font_.Get(), D2D1::ColorF(character.r, character.g, character.b, 0.7f),
                         DWRITE_TEXT_ALIGNMENT_CENTER);
            } else {
                const avatar::Pose pose = view && static_cast<size_t>(option) < view->previews.size() ? view->previews[static_cast<size_t>(option)] : avatar::Pose{};
                drawCompanionFigure(look, pose, figure_x, tile.bottom + height * 0.05f, height, 1, view ? at(view->preview_costumes, static_cast<size_t>(option)) : 0);
            }
            // A dark band under her name, so the caption reads over her picture on a narrow card.
            D2D1_GRADIENT_STOP stops[2]{{0, D2D1::ColorF(0, 0, 0, 0)}, {1, D2D1::ColorF(0, 0, 0, 0.72f)}};
            ComPtr<ID2D1GradientStopCollection> collection;
            ComPtr<ID2D1LinearGradientBrush> fade;
            if (SUCCEEDED(target_->CreateGradientStopCollection(stops, 2, collection.GetAddressOf())) &&
                SUCCEEDED(target_->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(D2D1::Point2F(0, tile.bottom - 96), D2D1::Point2F(0, tile.bottom)),
                                                             collection.Get(), fade.GetAddressOf())))
                target_->FillRectangle(D2D1::RectF(tile.left, tile.bottom - 96, tile.right, tile.bottom), fade.Get());
            target_->PopLayer();
        }
        // Gamification on: each one's level in the card's corner (who likes the player more?).
        if (const int level = view && !character.soon ? at(view->preview_levels, static_cast<size_t>(option)) : 0; level > 0)
            drawCompanionAffinity(tile.left + 16, tile.top + 28, 1, true, level, -1);
        // Her name and, under it, her fixed character (Tsundere, Kuudere…).
        textGroup(character.soon ? std::wstring(tr(L"Wkrótce")) : character.name, companion::personaName(character.persona), tile.left + 20, tile.bottom - 38,
                  tile.right - tile.left - 40, category_font_.Get(), character.soon ? gray(0.6f) : gray(0.97f), settings_body_font_.Get(), gray(0.78f), 1);
    }
    target_->PopAxisAlignedClip();
    if (count) {
        const auto chosen = layout.avatarSlide(index, scroll, row.choice, count, shown_first);
        if (chosen.right > chosen.left) drawCompanionCardFocus(box(chosen), 0, focused ? 1.0f : 0.75f);
    }
    // More cards than fit: the slider's arrows on the sides that have more.
    if (count > settings_ui::Layout::avatar_visible) {
        const float cy = (band.top + 84 + band.bottom - 20) * 0.5f;
        if (first > 0) textLine(L"\u2039", band.left + 2, cy, 20, category_font_.Get(), gray(0.8f), DWRITE_TEXT_ALIGNMENT_CENTER);
        if (first + settings_ui::Layout::avatar_visible < count) textLine(L"\u203A", band.right - 22, cy, 20, category_font_.Get(), gray(0.8f), DWRITE_TEXT_ALIGNMENT_CENTER);
    }
}
