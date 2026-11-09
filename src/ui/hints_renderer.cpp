// Bottom-right button hints (ViewContext::hints): glyph + label pairs on a small frosted pill.
// Pads by position: A/Cross/B(Nintendo) is the bottom face button, the launcher's accept.
#include "renderer.hpp"

#include <algorithm>
#include <cmath>

namespace {
using palette::gray;
// The pill keeps clear of the screen's edges (TVs crop a few percent with overscan): its bottom
// 15 px above the lower edge, its right 42 px from the right one.
constexpr float glyph_size = 24, gap = 8, spacing = 22, margin = 56, pill_height = 38, center_from_bottom = 34;
}

// Keycap captions for the keyboard; empty: the hint is drawn as a pad glyph.
static std::wstring keyCaption(HintButton button) {
    switch (button) {
    case HintButton::accept: return L"Enter";
    case HintButton::back: return L"Esc";
    case HintButton::drawer: return L"Tab";
    case HintButton::home: return L"Home";
    case HintButton::shoulders: return L"PgUp PgDn";
    case HintButton::type: return L"A–Z";
    case HintButton::menu: return L"Enter";
    case HintButton::search: return L"F";
    case HintButton::stick_press: return L"O";
    }
    return {};
}

static std::wstring shoulderCaption(HintDevice device) {
    return device == HintDevice::playstation ? L"L1   R1" : device == HintDevice::nintendo ? L"L   R" : L"LB   RB";
}

float Renderer::drawHintGlyph(const InputHint& hint, float x, float cy, float alpha) {
    const auto device = context_->hint_device;
    const auto ink = keyCapInk(alpha); // Light on glass key caps, dark on white ones.
    // Key caps and pad buttons: pills (circles) of light glass, no outline.
    const auto pill = [&](float width, const std::wstring& caption) {
        drawKeyCap(D2D1::RectF(x, cy - 11.5f, x + width, cy + 11.5f), alpha);
        if (!caption.empty()) textLine(caption, x, cy, width, friend_section_font_.Get(), ink, DWRITE_TEXT_ALIGNMENT_CENTER);
        return width;
    };
    if (device == HintDevice::keyboard) {
        const auto caption = hint.key.empty() ? keyCaption(hint.button) : hint.key;
        return pill(std::max(glyph_size + 6, textAdvance(caption, friend_section_font_.Get()) + 18), caption);
    }
    if (hint.button == HintButton::shoulders) {
        const auto caption = shoulderCaption(device);
        return pill(textAdvance(caption, friend_section_font_.Get()) + 18, caption);
    }
    if (hint.button == HintButton::stick_press) return pill(std::max(glyph_size + 6, textAdvance(L"R3", friend_section_font_.Get()) + 18), L"R3");
    const float cx = x + glyph_size * 0.5f, r = glyph_size * 0.5f;
    if (hint.button == HintButton::drawer) {
        // Menu / Options / +: a small pill with three lines (a plus on Nintendo).
        const float width = 30;
        pill(width, {});
        const float mx = x + width * 0.5f;
        if (device == HintDevice::nintendo) { line(mx - 5, cy, mx + 5, cy, ink, 1.8f); line(mx, cy - 5, mx, cy + 5, ink, 1.8f); }
        else for (int bar = -1; bar <= 1; ++bar) line(mx - 6, cy + bar * 4.0f, mx + 6, cy + bar * 4.0f, ink, 1.5f);
        return width;
    }
    drawKeyCap(D2D1::RectF(cx - r, cy - r, cx + r, cy + r), alpha);
    if (hint.button == HintButton::home) {
        // Guide / PS / Home: a small house.
        line(cx - 6, cy, cx, cy - 6, ink, 1.6f); line(cx, cy - 6, cx + 6, cy, ink, 1.6f);
        line(cx - 4, cy - 1, cx - 4, cy + 6, ink, 1.6f); line(cx + 4, cy - 1, cx + 4, cy + 6, ink, 1.6f);
        line(cx - 4, cy + 6, cx + 4, cy + 6, ink, 1.6f);
        return glyph_size;
    }
    // Face buttons by position: bottom accept, right back, top menu, left search.
    const int position = hint.button == HintButton::accept ? 0 : hint.button == HintButton::back ? 1 :
                         hint.button == HintButton::menu ? 2 : 3;
    if (device == HintDevice::playstation) {
        brush(ink);
        if (position == 0) { line(cx - 5, cy - 5, cx + 5, cy + 5, ink, 1.7f); line(cx + 5, cy - 5, cx - 5, cy + 5, ink, 1.7f); }
        else if (position == 1) target_->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), 5.5f, 5.5f), solid_.Get(), 1.7f);
        else if (position == 2) {
            line(cx, cy - 6, cx + 6, cy + 4.5f, ink, 1.6f); line(cx + 6, cy + 4.5f, cx - 6, cy + 4.5f, ink, 1.6f);
            line(cx - 6, cy + 4.5f, cx, cy - 6, ink, 1.6f);
        } else target_->DrawRectangle(D2D1::RectF(cx - 5, cy - 5, cx + 5, cy + 5), solid_.Get(), 1.6f);
        return glyph_size;
    }
    static constexpr const wchar_t* xbox[] = {L"A", L"B", L"Y", L"X"};
    static constexpr const wchar_t* nintendo[] = {L"B", L"A", L"X", L"Y"};
    textLine(device == HintDevice::nintendo ? nintendo[position] : xbox[position], cx - r, cy, glyph_size,
             friend_section_font_.Get(), ink, DWRITE_TEXT_ALIGNMENT_CENTER);
    return glyph_size;
}

void Renderer::drawHints() {
    if (context_->hints.empty() || context_->launch_animation_seconds >= 0) return;
    // The bottom bar (including its closing animation) and dialogs leave the list empty.
    const float alpha = 1.0f;
    const auto label_font = settings_body_font_.Get();
    // Measure right to left, then draw left to right from the pill's left edge.
    std::vector<float> glyphs, labels;
    float total = 0;
    for (const auto& hint : context_->hints) {
        float glyph = glyph_size;
        if (context_->hint_device == HintDevice::keyboard) {
            const auto caption = hint.key.empty() ? keyCaption(hint.button) : hint.key;
            glyph = std::max(glyph_size + 6, textAdvance(caption, friend_section_font_.Get()) + 18);
        } else if (hint.button == HintButton::shoulders) glyph = textAdvance(shoulderCaption(context_->hint_device), friend_section_font_.Get()) + 18;
        else if (hint.button == HintButton::stick_press) glyph = std::max(glyph_size + 6, textAdvance(L"R3", friend_section_font_.Get()) + 18);
        else if (hint.button == HintButton::drawer) glyph = 30;
        const float label = std::ceil(textAdvance(hint.label, label_font));
        glyphs.push_back(glyph); labels.push_back(label);
        total += glyph + gap + label;
    }
    total += spacing * static_cast<float>(context_->hints.size() - 1);
    const float cy = viewport_.height - center_from_bottom;
    const float right = viewport_.width - margin, left = right - total;
    const auto shape = D2D1::RoundedRect(D2D1::RectF(left - 14, cy - pill_height * 0.5f, right + 14, cy + pill_height * 0.5f),
                                         pill_height * 0.5f, pill_height * 0.5f);
    const auto glass = glassPanel(shape, 14);
    glassFill(glass, D2D1::ColorF(0.04f, 0.045f, 0.055f, 0.62f), alpha);
    float x = left;
    for (size_t i = 0; i < context_->hints.size(); ++i) {
        drawHintGlyph(context_->hints[i], x, cy, alpha);
        x += glyphs[i] + gap;
        textLine(context_->hints[i].label, x, cy, labels[i] + 4, label_font, gray(0.86f, alpha));
        x += labels[i] + spacing;
    }
}
