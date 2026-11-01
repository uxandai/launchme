// Renderer: fonts (DirectWrite formats in the chosen family) and the text primitives every
// screen draws with: lines, clipped text, measured advance, input fields, type metrics,
// centred single lines and title/description groups.
#include "renderer.hpp"
#include "renderer_internal.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

using Microsoft::WRL::ComPtr;
using namespace render_detail;

bool Renderer::font(ComPtr<IDWriteTextFormat>& destination, float size, DWRITE_FONT_WEIGHT weight,
                    DWRITE_TEXT_ALIGNMENT alignment) {
    HRESULT hr = E_FAIL;
    if (sst_fonts_ && font_family_ == L"SST") hr = dwrite_->CreateTextFormat(L"SST", sst_fonts_.Get(), weight,
                                                   DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                                   size, L"pl-PL", destination.GetAddressOf());
    else if (font_family_ != L"SST")
        hr = dwrite_->CreateTextFormat(font_family_.c_str(), nullptr, weight,
                                      DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                      size, L"pl-PL", destination.GetAddressOf());
    if (FAILED(hr)) hr = dwrite_->CreateTextFormat(L"Segoe UI Variable Display", nullptr, weight,
                                                  DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                                  size, L"pl-PL", destination.GetAddressOf());
    if (FAILED(hr)) hr = dwrite_->CreateTextFormat(L"Segoe UI", nullptr, weight,
                                                   DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                                   size, L"pl-PL", destination.GetAddressOf());
    if (FAILED(hr)) return false;
    destination->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    destination->SetTextAlignment(alignment);
    DWRITE_TRIMMING trimming{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
    ComPtr<IDWriteInlineObject> sign;
    if (SUCCEEDED(dwrite_->CreateEllipsisTrimmingSign(destination.Get(), sign.GetAddressOf())))
        destination->SetTrimming(&trimming, sign.Get());
    return true;
}

bool Renderer::createFonts() {
    type_metrics_.clear();
    shelf_spines_.clear(); // Printed in the interface's font.
    text_layouts_.clear();
    cd_pen_font_.Reset();
    if (FAILED(dwrite_->CreateTextFormat(L"Segoe Print", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL, 23, L"pl-PL", cd_pen_font_.GetAddressOf())) && !font(cd_pen_font_, 23, DWRITE_FONT_WEIGHT_NORMAL)) return false;
    return font(hero_font_, 53, DWRITE_FONT_WEIGHT_LIGHT) &&
           font(label_font_, 12, DWRITE_FONT_WEIGHT_SEMI_BOLD) &&
           font(button_font_, 15, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_CENTER) &&
           font(action_font_, 19, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_CENTER) &&
           SUCCEEDED(action_font_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER)) &&
            font(category_font_, 23, DWRITE_FONT_WEIGHT_SEMI_BOLD) &&
            font(menu_font_, 19, DWRITE_FONT_WEIGHT_SEMI_BOLD) &&
             font(sidebar_font_, 26, DWRITE_FONT_WEIGHT_MEDIUM) &&
             font(category_active_font_, 30, DWRITE_FONT_WEIGHT_BOLD) &&
            font(friend_name_font_, 20, DWRITE_FONT_WEIGHT_MEDIUM) &&
            font(friend_status_font_, 14, DWRITE_FONT_WEIGHT_SEMI_BOLD) &&
            font(friend_section_font_, 14, DWRITE_FONT_WEIGHT_BOLD) &&
           font(toast_font_, 17, DWRITE_FONT_WEIGHT_SEMI_BOLD) &&
           font(toast_sub_font_, 13, DWRITE_FONT_WEIGHT_NORMAL) &&
           font(settings_heading_font_, 38, DWRITE_FONT_WEIGHT_SEMI_BOLD) &&
           font(settings_body_font_, 16, DWRITE_FONT_WEIGHT_NORMAL) &&
           font(details_body_font_, 18, DWRITE_FONT_WEIGHT_NORMAL) &&
           font(clock_font_, 64, DWRITE_FONT_WEIGHT_LIGHT) &&
           font(metro_tab_font_, 40, DWRITE_FONT_WEIGHT_LIGHT) &&
           font(psfive_name_font_, 29, DWRITE_FONT_WEIGHT_LIGHT) &&
           font(psfive_clock_font_, 29, DWRITE_FONT_WEIGHT_LIGHT) &&
           font(psfive_tab_font_, 30, DWRITE_FONT_WEIGHT_SEMI_BOLD) &&
           font(psfive_tab_idle_font_, 30, DWRITE_FONT_WEIGHT_NORMAL) &&
           font(psfive_button_font_, 27, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_CENTER) &&
           SUCCEEDED(psfive_button_font_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER)) &&
           font(clock_date_font_, 19, DWRITE_FONT_WEIGHT_NORMAL) &&
           font(tile_value_font_, 20, DWRITE_FONT_WEIGHT_SEMI_BOLD) &&
           SUCCEEDED(tile_value_font_->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP)) &&
           font(index_font_, 230, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_CENTER) &&
           SUCCEEDED(index_font_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER)) &&
           createDesktopFonts();
}

bool Renderer::createDesktopFonts() {
    // The icon font is optional: without it the glyphs are simply left out.
    desktop_icon_font_.Reset();
    ComPtr<IDWriteFontCollection> system;
    if (SUCCEEDED(dwrite_->GetSystemFontCollection(system.GetAddressOf()))) {
        for (const auto* family : {L"Segoe Fluent Icons", L"Segoe MDL2 Assets"}) {
            UINT32 index = 0; BOOL exists = FALSE;
            if (FAILED(system->FindFamilyName(family, &index, &exists)) || !exists) continue;
            if (SUCCEEDED(dwrite_->CreateTextFormat(family, nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                                                    DWRITE_FONT_STRETCH_NORMAL, 16, L"", desktop_icon_font_.GetAddressOf()))) {
                desktop_icon_font_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                desktop_icon_font_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                desktop_icon_font_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
                break;
            }
        }
    }
    return font(desktop_font_, 13.5f, DWRITE_FONT_WEIGHT_NORMAL) &&
           font(desktop_strong_font_, 13.5f, DWRITE_FONT_WEIGHT_SEMI_BOLD) &&
           font(desktop_small_font_, 12, DWRITE_FONT_WEIGHT_NORMAL) &&
           font(desktop_label_font_, 12, DWRITE_FONT_WEIGHT_SEMI_BOLD) &&
           font(desktop_title_font_, 20, DWRITE_FONT_WEIGHT_SEMI_BOLD) &&
           font(desktop_heading_font_, 24, DWRITE_FONT_WEIGHT_SEMI_BOLD) &&
           font(desktop_button_font_, 14, DWRITE_FONT_WEIGHT_SEMI_BOLD);
}

void Renderer::setFontFamily(std::wstring family) {
    if (family == font_family_) return;
    font_family_ = std::move(family);
    hero_font_.Reset(); label_font_.Reset(); button_font_.Reset(); action_font_.Reset(); category_font_.Reset();
    menu_font_.Reset(); sidebar_font_.Reset(); category_active_font_.Reset(); friend_name_font_.Reset();
    friend_status_font_.Reset(); friend_section_font_.Reset(); toast_font_.Reset(); toast_sub_font_.Reset();
    settings_heading_font_.Reset(); settings_body_font_.Reset();
    details_body_font_.Reset();
    clock_font_.Reset(); clock_date_font_.Reset(); tile_value_font_.Reset();
    index_font_.Reset(); metro_tab_font_.Reset(); psfive_name_font_.Reset(); psfive_clock_font_.Reset();
    psfive_tab_font_.Reset(); psfive_tab_idle_font_.Reset(); psfive_button_font_.Reset();
    desktop_font_.Reset(); desktop_strong_font_.Reset(); desktop_small_font_.Reset(); desktop_label_font_.Reset();
    desktop_title_font_.Reset(); desktop_heading_font_.Reset(); desktop_button_font_.Reset();
    // Made on the first reading in the chosen family too: made again on the next.
    tarot_title_font_.Reset(); tarot_name_font_.Reset(); tarot_numeral_font_.Reset(); tarot_question_font_.Reset();
    tarot_body_font_.Reset(); tarot_small_font_.Reset(); tarot_result_font_.Reset();
    createFonts();
}

void Renderer::line(float x1, float y1, float x2, float y2, D2D1_COLOR_F ink, float width) {
    brush(ink);
    target_->DrawLine(D2D1::Point2F(x1, y1), D2D1::Point2F(x2, y2), solid_.Get(), width);
}

void Renderer::text(const std::wstring& value, float x, float y, float width, float height,
                    IDWriteTextFormat* format, D2D1_COLOR_F ink) {
    brush(ink);
    // Match the original rectangle's effective extents, including float rounding
    // during card animation. The cache changes shaping work, not layout or clipping.
    if (auto* layout = text_layouts_.get(dwrite_.Get(), value, format, (x + width) - x, (y + height) - y)) {
        target_->DrawTextLayout(D2D1::Point2F(x, y), layout, solid_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
        return;
    }
    target_->DrawTextW(value.c_str(), static_cast<UINT32>(value.size()), format,
                       D2D1::RectF(x, y, x + width, y + height), solid_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

float Renderer::textAdvance(const std::wstring& value, IDWriteTextFormat* format) {
    if (value.empty() || !format) return 0;
    ComPtr<IDWriteTextLayout> created;
    DWRITE_TEXT_METRICS metrics{};
    const auto unwrapped = [](IDWriteTextLayout* layout) { layout->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP); };
    auto* layout = text_layouts_.get(dwrite_.Get(), value, format, 100000.0f, 1000.0f, text_variant_advance, unwrapped);
    if (!layout) {
        if (FAILED(dwrite_->CreateTextLayout(value.c_str(), static_cast<UINT32>(value.size()), format, 100000.0f, 1000.0f, created.GetAddressOf()))) return 0;
        unwrapped(created.Get()); layout = created.Get();
    }
    if (FAILED(layout->GetMetrics(&metrics))) return 0;
    return metrics.widthIncludingTrailingWhitespace;
}

void Renderer::fieldText(const std::wstring& value, size_t caret, bool show_caret, float x, float y, float width, float height,
                         IDWriteTextFormat* format, D2D1_COLOR_F ink) {
    text(value, x, y, width, height, format, ink);
    if (!show_caret) return;
    const float at = textAdvance(value.substr(0, std::min(caret, value.size())), format);
    // At the end the caret follows the text as before; inside, it sits on the gap between letters.
    const float bar = caret >= value.size() ? 0.0f : textAdvance(L"│", format) * 0.5f;
    if (at - bar <= width) text(L"│", x + at - bar, y, std::max(1.0f, width - at + bar), height, format, ink);
}

Renderer::TypeMetrics Renderer::typeMetrics(IDWriteTextFormat* format) {
    if (!format) return {};
    if (const auto found = type_metrics_.find(format); found != type_metrics_.end()) return found->second;
    const float size = format->GetFontSize();
    TypeMetrics result{size * 1.3f, size * 1.07f, size * 0.7f};
    ComPtr<IDWriteTextLayout> layout;
    DWRITE_LINE_METRICS line{};
    UINT32 lines = 0;
    if (SUCCEEDED(dwrite_->CreateTextLayout(L"Hg", 2, format, 1000, 1000, layout.GetAddressOf())) &&
        SUCCEEDED(layout->GetLineMetrics(&line, 1, &lines)) && lines) {
        result.line = line.height;
        result.baseline = line.baseline;
    }
    ComPtr<IDWriteFontCollection> collection;
    ComPtr<IDWriteFontFamily> family;
    ComPtr<IDWriteFont> face;
    wchar_t name[128]{};
    UINT32 index = 0;
    BOOL exists = FALSE;
    if (SUCCEEDED(format->GetFontCollection(collection.GetAddressOf())) && collection &&
        SUCCEEDED(format->GetFontFamilyName(name, static_cast<UINT32>(std::size(name)))) &&
        SUCCEEDED(collection->FindFamilyName(name, &index, &exists)) && exists &&
        SUCCEEDED(collection->GetFontFamily(index, family.GetAddressOf())) &&
        SUCCEEDED(family->GetFirstMatchingFont(format->GetFontWeight(), format->GetFontStretch(),
                                               format->GetFontStyle(), face.GetAddressOf()))) {
        DWRITE_FONT_METRICS metrics{};
        face->GetMetrics(&metrics);
        if (metrics.designUnitsPerEm && metrics.capHeight)
            result.cap = static_cast<float>(metrics.capHeight) * size / metrics.designUnitsPerEm;
    }
    type_metrics_[format] = result;
    return result;
}

void Renderer::textLine(const std::wstring& value, float x, float center_y, float width, IDWriteTextFormat* format,
                        D2D1_COLOR_F ink, std::optional<DWRITE_TEXT_ALIGNMENT> alignment) {
    if (value.empty() || width <= 0 || !format) return;
    const auto metrics = typeMetrics(format);
    // Cached per alignment override: one variant per value, so a centred and a leading copy of
    // the same label never share (and re-align) one layout.
    const auto adjust = [&](IDWriteTextLayout* layout) {
        layout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
        if (alignment) layout->SetTextAlignment(*alignment);
    };
    const uint32_t variant = text_variant_line + (alignment ? 1 + static_cast<uint32_t>(*alignment) : 0);
    ComPtr<IDWriteTextLayout> created;
    auto* layout = text_layouts_.get(dwrite_.Get(), value, format, width, metrics.line, variant, adjust);
    if (!layout) {
        if (FAILED(dwrite_->CreateTextLayout(value.c_str(), static_cast<UINT32>(value.size()), format, width, metrics.line,
                                             created.GetAddressOf()))) return;
        adjust(created.Get()); layout = created.Get();
    }
    brush(ink);
    target_->DrawTextLayout(D2D1::Point2F(x, center_y - (metrics.baseline - metrics.cap * 0.5f)), layout,
                            solid_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

float Renderer::textGroup(const std::wstring& title, const std::wstring& description, float x, float y, float width,
                          IDWriteTextFormat* title_font, D2D1_COLOR_F title_ink, IDWriteTextFormat* body_font,
                          D2D1_COLOR_F body_ink, int max_lines, bool top_aligned, float gap) {
    const auto head = typeMetrics(title_font), body = typeMetrics(body_font);
    width = std::max(0.0f, width);
    ComPtr<IDWriteTextLayout> paragraph;
    int lines = 0;
    if (!description.empty() && max_lines > 0 && width > 0 &&
        SUCCEEDED(dwrite_->CreateTextLayout(description.c_str(), static_cast<UINT32>(description.size()), body_font,
                                            width, body.line * max_lines + 0.5f, paragraph.GetAddressOf()))) {
        paragraph->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
        paragraph->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
        DWRITE_TEXT_METRICS metrics{};
        if (SUCCEEDED(paragraph->GetMetrics(&metrics)))
            lines = std::clamp(static_cast<int>(metrics.lineCount), 1, max_lines);
    }
    // Ink extent measured from the title's line box top: its capitals down to the last baseline.
    const float ink_bottom = lines ? head.line + gap + (lines - 1) * body.line + body.baseline : head.baseline;
    const float top = top_aligned ? y - head.capTop() : y - (head.capTop() + ink_bottom) * 0.5f;
    text(title, x, top, width, head.line, title_font, title_ink);
    if (!lines) return top + head.line;
    const float description_top = top + head.line + gap;
    brush(body_ink);
    target_->DrawTextLayout(D2D1::Point2F(x, description_top), paragraph.Get(), solid_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
    return description_top + lines * body.line;
}

void Renderer::fadingLine(const std::wstring& value, float x, float center_y, float width, IDWriteTextFormat* format, D2D1_COLOR_F ink, bool moving,
                          std::optional<DWRITE_TEXT_ALIGNMENT> alignment) {
    const float advance = format ? textAdvance(value, format) : 0;
    constexpr float speed = 46, hold = 2.0f;
    const float fade = std::min(34.0f, width * 0.2f); // Narrow boxes (a category pill) keep most of the name visible.
    if (advance <= width || width <= 2 * fade || !format) { textLine(value, x, center_y, width, format, ink, alignment); return; }
    float offset = 0;
    if (moving) {
        const auto now = std::chrono::steady_clock::now();
        auto& marquee = marquees_.try_emplace(value, Marquee{now, true}).first->second;
        marquee.used = true;
        // Far enough for its last letter to clear the fade; there and back, resting at both ends.
        const float travel = advance - width + fade, glide = travel / speed, period = 2 * (hold + glide);
        const float t = std::fmod(std::chrono::duration<float>(now - marquee.since).count(), period);
        const auto eased = [](float v) { v = std::clamp(v, 0.0f, 1.0f); return v * v * (3 - 2 * v); };
        offset = t < hold ? 0.0f : t < hold + glide ? travel * eased((t - hold) / glide) :
                 t < 2 * hold + glide ? travel : travel * (1 - eased((t - 2 * hold - glide) / glide));
        marquee_active_ = true;
    }
    // Opaque in the middle, transparent at the right edge (and at the left one once it has moved).
    const float left_fade = std::min(fade, offset) / width, right_fade = 1 - fade / width;
    const D2D1_GRADIENT_STOP stops[] = {{0, palette::gray(1, offset > 0.5f ? 0.0f : 1.0f)}, {std::max(0.001f, left_fade), palette::gray(1, 1)},
                                        {right_fade, palette::gray(1, 1)}, {1, palette::gray(1, 0)}};
    ComPtr<ID2D1GradientStopCollection> collection;
    ComPtr<ID2D1LinearGradientBrush> mask;
    const auto metrics = typeMetrics(format);
    const auto bounds = D2D1::RectF(x, center_y - metrics.line, x + width, center_y + metrics.line);
    if (FAILED(target_->CreateGradientStopCollection(stops, 4, collection.GetAddressOf())) ||
        FAILED(target_->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties({x, center_y}, {x + width, center_y}), collection.Get(),
                                                  mask.GetAddressOf()))) { textLine(value, x, center_y, width, format, ink); return; }
    target_->PushLayer(D2D1::LayerParameters(bounds, nullptr, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE, D2D1::IdentityMatrix(), 1, mask.Get()), nullptr);
    textLine(value, x - offset, center_y, advance + 8, format, ink);
    target_->PopLayer();
}

std::wstring Renderer::elideStart(const std::wstring& value, float width, IDWriteTextFormat* format) {
    if (!format || value.empty() || textAdvance(value, format) <= width) return value;
    size_t low = 1, high = value.size(); // The fewest leading characters to drop.
    while (low < high) {
        const size_t mid = (low + high) / 2;
        if (textAdvance(L"\u2026" + value.substr(mid), format) <= width) high = mid; else low = mid + 1;
    }
    return L"\u2026" + value.substr(low);
}
