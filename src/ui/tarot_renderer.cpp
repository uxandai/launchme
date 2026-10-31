// Renderer: Tarot (src/app/tarot.cpp, geometry in tarot_view.hpp). The home dimmed to a quiet grey;
// plain grey cards in Launchme's own type, each with its arcana's numeral, symbol,
// name and meaning; their flight from the deck, the turn face up, the chosen card's rise; the
// shuffling deck while the cards are read; and the reading itself: up to three cards turning over
// to show the games' posters. Every card is laid out at 240 units wide and scaled, so its type
// grows and shrinks with it.
#include "renderer.hpp"
#include "renderer_internal.hpp"

#include <cmath>
#include <numbers>

using Microsoft::WRL::ComPtr;

namespace {
using tarot_ui::Phase;
constexpr float pi = std::numbers::pi_v<float>;
constexpr float card_units = 240; // A card's width in its own units.

D2D1_COLOR_F rgba(float r, float g, float b, float a) { return D2D1::ColorF(r, g, b, std::clamp(a, 0.0f, 1.0f)); }
// Launchme's own muted greys (Tryb TV's panels), the brightest for what has the focus.
D2D1_COLOR_F gold(float a) { return rgba(0.78f, 0.78f, 0.80f, a); }
D2D1_COLOR_F brightGold(float a) { return rgba(0.97f, 0.97f, 0.98f, a); }
D2D1_COLOR_F parchment(float a) { return rgba(0.92f, 0.92f, 0.93f, a); }
D2D1_COLOR_F lavender(float a) { return rgba(0.60f, 0.60f, 0.63f, a); }
using render_detail::box;
D2D1_RECT_F inflate(D2D1_RECT_F r, float by) { return D2D1::RectF(r.left - by, r.top - by, r.right + by, r.bottom + by); }
float lerp(float a, float b, float t) { return a + (b - a) * t; }
std::wstring roman(int value) {
    static constexpr const wchar_t* numerals[] = {L"0", L"I", L"II", L"III", L"IV", L"V", L"VI", L"VII", L"VIII", L"IX", L"X", L"XI",
        L"XII", L"XIII", L"XIV", L"XV", L"XVI", L"XVII", L"XVIII", L"XIX", L"XX", L"XXI"};
    return numerals[std::clamp(value, 0, 21)];
}
// The major arcana a symbol recalls (the Magician's wand I, the Wheel X, the Tower XVI…).
int arcana(tarot::Symbol symbol) {
    using S = tarot::Symbol;
    switch (symbol) {
    case S::dice: return 0;
    case S::wand: return 1;
    case S::eye: return 2;
    case S::pentacle: return 3;
    case S::crown: return 4;
    case S::book: return 5;
    case S::heart: case S::fellowship: return 6;
    case S::arrow: return 7;
    case S::sword: case S::swords: return 8;
    case S::lantern: case S::figure: return 9;
    case S::wheel: return 10;
    case S::scales: return 11;
    case S::hourglass: return 12;
    case S::skull: return 13;
    case S::cup: return 14;
    case S::eclipse: case S::mirror: return 15;
    case S::tower: case S::lightning: return 16;
    case S::star: case S::spark: return 17;
    case S::moon: return 18;
    case S::sun: return 19;
    case S::dawn: return 20;
    case S::infinity: return 21;
    default: return (static_cast<int>(symbol) * 7 + 3) % 22;
    }
}
std::wstring upper(const std::wstring& text) {
    if (text.empty()) return text;
    std::wstring result(text.size() * 2 + 4, L'\0');
    const int length = LCMapStringEx(i18n::localeName(), LCMAP_UPPERCASE, text.c_str(), static_cast<int>(text.size()),
                                     result.data(), static_cast<int>(result.size()), nullptr, nullptr, 0);
    if (length <= 0) return text;
    result.resize(static_cast<size_t>(length));
    return result;
}
}

void Renderer::setTarotImage(size_t index, ImageBytes pixels, bool poster) {
    if (index >= tarot_images_.size() || !pixels || pixels->empty()) return;
    if (tarot_posters_[index] && !poster) return; // A poster is never replaced by a cover.
    auto& image = tarot_images_[index];
    tarot_posters_[index] = poster;
    if (image.pixels == pixels) return;
    image = {};
    image.pixels = std::move(pixels);
    createBitmap(image);
}

bool Renderer::createTarotFonts() {
    if (tarot_title_font_ && tarot_name_font_ && tarot_numeral_font_ && tarot_question_font_ && tarot_body_font_ &&
        tarot_small_font_ && tarot_result_font_ && tarot_stroke_) return true;
    if (!dwrite_ || !factory_) return false;
    ComPtr<IDWriteFontCollection> system;
    dwrite_->GetSystemFontCollection(system.GetAddressOf());
    const auto installed = [&](const wchar_t* family) {
        UINT32 index = 0; BOOL exists = FALSE;
        return system && SUCCEEDED(system->FindFamilyName(family, &index, &exists)) && exists;
    };
    const auto make = [&](ComPtr<IDWriteTextFormat>& out, std::initializer_list<const wchar_t*> families, float size,
                          DWRITE_FONT_WEIGHT weight, DWRITE_FONT_STYLE style, bool wrap) {
        out.Reset();
        for (const auto* family : families)
            if (installed(family) && SUCCEEDED(dwrite_->CreateTextFormat(family, nullptr, weight, style, DWRITE_FONT_STRETCH_NORMAL,
                                                                         size, L"pl-PL", out.GetAddressOf()))) break;
        if (!out && !font(out, size, weight)) return false;
        out->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        out->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
        out->SetWordWrapping(wrap ? DWRITE_WORD_WRAPPING_WRAP : DWRITE_WORD_WRAPPING_NO_WRAP);
        if (!wrap) {
            DWRITE_TRIMMING trimming{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
            ComPtr<IDWriteInlineObject> sign;
            if (SUCCEEDED(dwrite_->CreateEllipsisTrimmingSign(out.Get(), sign.GetAddressOf()))) out->SetTrimming(&trimming, sign.Get());
        }
        return true;
    };
    // Launchme's own typeface everywhere (Interfejs → Czcionka), as the rest of Tryb TV.
    const std::initializer_list<const wchar_t*> own = {};
    if (!make(tarot_title_font_, own, 56, DWRITE_FONT_WEIGHT_SEMI_LIGHT, DWRITE_FONT_STYLE_NORMAL, false) ||
        !make(tarot_name_font_, own, 17, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, false) ||
        !make(tarot_numeral_font_, own, 15, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, false) ||
        !make(tarot_question_font_, own, 28, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, true) ||
        !make(tarot_body_font_, own, 15, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, true) ||
        !make(tarot_small_font_, own, 15, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, false) ||
        !make(tarot_result_font_, own, 21, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, true)) return false;
    if (!tarot_stroke_)
        factory_->CreateStrokeStyle(D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND,
                                                                D2D1_LINE_JOIN_ROUND), nullptr, 0, tarot_stroke_.GetAddressOf());
    return tarot_stroke_ != nullptr;
}

void Renderer::drawTarotGlyph(float x, float y, float size, D2D1_COLOR_F ink) {
    const float h = size, w = size * 0.66f;
    const auto card = D2D1::RoundedRect(D2D1::RectF(x - w * 0.5f, y - h * 0.5f, x + w * 0.5f, y + h * 0.5f), 2.2f, 2.2f);
    brush(ink);
    target_->DrawRoundedRectangle(card, solid_.Get(), 1.4f);
    const float r = w * 0.32f;
    const D2D1_POINT_2F star[] = {{x, y - r}, {x + r * 0.28f, y - r * 0.28f}, {x + r, y}, {x + r * 0.28f, y + r * 0.28f},
                                  {x, y + r}, {x - r * 0.28f, y + r * 0.28f}, {x - r, y}, {x - r * 0.28f, y - r * 0.28f}};
    ComPtr<ID2D1PathGeometry> shape; ComPtr<ID2D1GeometrySink> sink;
    if (FAILED(factory_->CreatePathGeometry(shape.GetAddressOf())) || FAILED(shape->Open(sink.GetAddressOf()))) return;
    sink->BeginFigure(star[0], D2D1_FIGURE_BEGIN_FILLED);
    sink->AddLines(star + 1, static_cast<UINT32>(std::size(star) - 1));
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    if (SUCCEEDED(sink->Close())) target_->FillGeometry(shape.Get(), solid_.Get());
}

// One symbol, `size` across, centred on (cx, cy), in unit coordinates u, v ∈ [-1, 1] (v down).
void Renderer::drawTarotSymbol(tarot::Symbol symbol, float cx, float cy, float size, D2D1_COLOR_F ink, float stroke) {
    using S = tarot::Symbol;
    const float s = size * 0.5f;
    auto* style = tarot_stroke_.Get();
    const auto P = [&](float u, float v) { return D2D1::Point2F(cx + u * s, cy + v * s); };
    const auto shade = [&](float share) { auto c = ink; c.a *= share; return c; };
    const auto points = [&](const std::vector<D2D1_POINT_2F>& list, bool closed, float fill = 0) {
        if (list.size() < 2) return;
        ComPtr<ID2D1PathGeometry> geometry; ComPtr<ID2D1GeometrySink> sink;
        if (FAILED(factory_->CreatePathGeometry(geometry.GetAddressOf())) || FAILED(geometry->Open(sink.GetAddressOf()))) return;
        sink->BeginFigure(list[0], fill > 0 ? D2D1_FIGURE_BEGIN_FILLED : D2D1_FIGURE_BEGIN_HOLLOW);
        sink->AddLines(list.data() + 1, static_cast<UINT32>(list.size() - 1));
        sink->EndFigure(closed ? D2D1_FIGURE_END_CLOSED : D2D1_FIGURE_END_OPEN);
        if (FAILED(sink->Close())) return;
        if (fill > 0) { brush(shade(fill)); target_->FillGeometry(geometry.Get(), solid_.Get()); }
        brush(ink); target_->DrawGeometry(geometry.Get(), solid_.Get(), stroke, style);
    };
    const auto poly = [&](std::initializer_list<D2D1_POINT_2F> list, bool closed, float fill = 0) { points(std::vector<D2D1_POINT_2F>(list), closed, fill); };
    const auto line = [&](float u1, float v1, float u2, float v2, float width = 1) {
        brush(ink); target_->DrawLine(P(u1, v1), P(u2, v2), solid_.Get(), stroke * width, style);
    };
    const auto circle = [&](float u, float v, float r, float fill = 0, float width = 1) {
        const auto ellipse = D2D1::Ellipse(P(u, v), r * s, r * s);
        if (fill > 0) { brush(shade(fill)); target_->FillEllipse(ellipse, solid_.Get()); }
        if (fill < 1) { brush(ink); target_->DrawEllipse(ellipse, solid_.Get(), stroke * width, style); }
    };
    // Points along an ellipse from angle a0 to a1 (radians, 0 = right, clockwise as v grows down).
    const auto arc = [&](float u, float v, float ru, float rv, float a0, float a1, int segments = 28) {
        std::vector<D2D1_POINT_2F> list;
        for (int i = 0; i <= segments; ++i) {
            const float a = lerp(a0, a1, static_cast<float>(i) / segments);
            list.push_back(P(u + std::cos(a) * ru, v + std::sin(a) * rv));
        }
        return list;
    };
    const auto star = [&](float u, float v, float outer, float inner, int tips, float fill, float turn = -pi / 2) {
        std::vector<D2D1_POINT_2F> list;
        for (int i = 0; i < tips * 2; ++i) {
            const float a = turn + pi * i / tips, r = i % 2 ? inner : outer;
            list.push_back(P(u + std::cos(a) * r, v + std::sin(a) * r));
        }
        points(list, true, fill);
    };
    const auto flame = [&](float u, float v, float scale, float fill) {
        std::vector<D2D1_POINT_2F> list;
        for (int i = 0; i < 40; ++i) {
            const float t = static_cast<float>(i) / 40 * 2 * pi;
            // A teardrop: a point above (t = 0), round below, its tip leaning a little to the right.
            const float y = -std::cos(t);
            const float x = std::sin(t) * 0.62f * std::pow((1 - std::cos(t)) * 0.5f, 0.9f);
            const float lean = std::pow((1 - y) * 0.5f, 3.0f) * 0.18f;
            list.push_back(P(u + (x + lean) * scale, v + (y < 0 ? y * 1.15f : y * 0.62f) * scale));
        }
        points(list, true, fill);
    };
    const auto person = [&](float u, float v, float scale) {
        circle(u, v - 0.5f * scale, 0.24f * scale, 0.35f);
        auto body = arc(u, v + 0.62f * scale, 0.58f * scale, 0.78f * scale, pi, 2 * pi, 20);
        points(body, true, 0.18f);
    };
    switch (symbol) {
    case S::pentacle:
        circle(0, 0, 0.95f, 0.08f);
        {
            std::vector<D2D1_POINT_2F> list;
            for (int i = 0; i < 5; ++i) { const float a = -pi / 2 + i * 4 * pi / 5; list.push_back(P(std::cos(a) * 0.86f, std::sin(a) * 0.86f)); }
            points(list, true);
        }
        break;
    case S::lantern:
        circle(0, -0.8f, 0.14f);
        line(-0.3f, -0.62f, 0.3f, -0.62f, 1.3f);
        poly({P(-0.42f, -0.52f), P(0.42f, -0.52f), P(0.55f, 0.55f), P(-0.55f, 0.55f)}, true, 0.12f);
        line(-0.65f, 0.66f, 0.65f, 0.66f, 1.3f);
        flame(0, 0.08f, 0.32f, 0.85f);
        break;
    case S::sword:
        poly({P(-0.09f, 0.42f), P(-0.09f, -0.8f), P(0, -1), P(0.09f, -0.8f), P(0.09f, 0.42f)}, true, 0.25f);
        line(-0.5f, 0.45f, 0.5f, 0.45f, 1.5f);
        line(0, 0.47f, 0, 0.84f, 1.6f);
        circle(0, 0.93f, 0.08f, 1);
        break;
    case S::wand:
        line(-0.62f, 0.95f, 0.3f, -0.38f, 1.6f);
        poly({P(0.45f, -0.92f), P(0.62f, -0.62f), P(0.45f, -0.32f), P(0.28f, -0.62f)}, true, 0.9f);
        star(-0.28f, -0.62f, 0.2f, 0.05f, 4, 0.9f);
        star(0.78f, -0.08f, 0.14f, 0.04f, 4, 0.9f);
        star(-0.6f, -0.05f, 0.1f, 0.03f, 4, 0.9f);
        break;
    case S::crown:
        poly({P(-0.85f, 0.45f), P(-0.85f, -0.35f), P(-0.42f, 0.05f), P(0, -0.62f), P(0.42f, 0.05f), P(0.85f, -0.35f), P(0.85f, 0.45f)}, true, 0.2f);
        line(-0.85f, 0.64f, 0.85f, 0.64f, 1.4f);
        circle(-0.85f, -0.46f, 0.08f, 1); circle(0, -0.74f, 0.09f, 1); circle(0.85f, -0.46f, 0.08f, 1);
        circle(0, 0.18f, 0.1f, 0.7f);
        break;
    case S::mountain:
        poly({P(-1, 0.72f), P(-0.35f, -0.42f), P(-0.05f, 0.08f), P(0.32f, -0.78f), P(1, 0.72f)}, true, 0.15f);
        poly({P(0.13f, -0.45f), P(0.3f, -0.32f), P(0.47f, -0.47f)}, false);
        circle(-0.62f, -0.72f, 0.15f, 0.6f);
        break;
    case S::tower:
        poly({P(-0.36f, -0.42f), P(0.36f, -0.42f), P(0.42f, 0.92f), P(-0.42f, 0.92f)}, true, 0.15f);
        poly({P(-0.46f, -0.42f), P(-0.46f, -0.7f), P(-0.24f, -0.7f), P(-0.24f, -0.56f), P(-0.08f, -0.56f), P(-0.08f, -0.7f),
              P(0.08f, -0.7f), P(0.08f, -0.56f), P(0.24f, -0.56f), P(0.24f, -0.7f), P(0.46f, -0.7f), P(0.46f, -0.42f)}, false);
        poly({P(-0.1f, 0.25f), P(-0.1f, 0.0f), P(0, -0.1f), P(0.1f, 0.0f), P(0.1f, 0.25f)}, true, 0.6f);
        poly({P(0.95f, -1), P(0.5f, -0.48f), P(0.74f, -0.44f), P(0.3f, 0.1f)}, false);
        break;
    case S::wheel:
        circle(0, 0, 0.9f, 0.06f, 1.2f);
        circle(0, 0, 0.62f);
        circle(0, 0, 0.18f, 0.8f);
        for (int i = 0; i < 8; ++i) {
            const float a = i * pi / 4;
            line(std::cos(a) * 0.18f, std::sin(a) * 0.18f, std::cos(a) * 0.9f, std::sin(a) * 0.9f);
            circle(std::cos(a + pi / 8) * 0.76f, std::sin(a + pi / 8) * 0.76f, 0.05f, 1);
        }
        break;
    case S::hammer:
        line(-0.62f, 0.9f, 0.2f, -0.12f, 1.8f);
        poly({P(-0.02f, -0.56f), P(0.34f, -0.92f), P(0.88f, -0.38f), P(0.52f, -0.02f)}, true, 0.35f);
        line(0.38f, -0.2f, 0.62f, -0.44f);
        break;
    case S::eye: {
        std::vector<D2D1_POINT_2F> lens;
        for (int i = 0; i <= 24; ++i) { const float t = static_cast<float>(i) / 24; lens.push_back(P(-1 + 2 * t, -0.58f * std::sin(pi * t))); }
        for (int i = 23; i >= 1; --i) { const float t = static_cast<float>(i) / 24; lens.push_back(P(-1 + 2 * t, 0.58f * std::sin(pi * t))); }
        points(lens, true, 0.1f);
        circle(0, 0, 0.34f, 0.25f);
        circle(0, 0, 0.13f, 1);
        for (int i = 0; i < 5; ++i) { const float a = -pi / 2 + (i - 2) * 0.38f; line(std::cos(a) * 0.75f, std::sin(a) * 0.75f, std::cos(a) * 0.98f, std::sin(a) * 0.98f); }
        break;
    }
    case S::arrow:
        line(-0.9f, 0.05f, 0.82f, 0.05f, 1.5f);
        poly({P(0.42f, -0.32f), P(0.92f, 0.05f), P(0.42f, 0.42f)}, true, 0.7f);
        line(-0.95f, -0.38f, -0.35f, -0.38f); line(-0.8f, 0.48f, -0.28f, 0.48f);
        circle(-0.2f, 0.62f, 0.2f); circle(0.35f, 0.62f, 0.2f);
        break;
    case S::star:
        star(0, 0, 1.0f, 0.36f, 8, 0.25f, -pi / 2);
        circle(0, 0, 0.12f, 1);
        break;
    case S::hourglass:
        line(-0.7f, -0.88f, 0.7f, -0.88f, 1.5f); line(-0.7f, 0.88f, 0.7f, 0.88f, 1.5f);
        poly({P(-0.52f, -0.84f), P(0.52f, -0.84f), P(0.08f, 0), P(0.52f, 0.84f), P(-0.52f, 0.84f), P(-0.08f, 0)}, true, 0.08f);
        poly({P(-0.32f, 0.84f), P(0.32f, 0.84f), P(0, 0.42f)}, true, 0.8f);
        poly({P(-0.3f, -0.58f), P(0.3f, -0.58f), P(0, -0.3f)}, true, 0.8f);
        break;
    case S::sun:
        circle(0, 0, 0.42f, 0.3f, 1.2f);
        for (int i = 0; i < 12; ++i) {
            const float a = i * pi / 6, end = i % 2 ? 0.78f : 1.0f;
            line(std::cos(a) * 0.56f, std::sin(a) * 0.56f, std::cos(a) * end, std::sin(a) * end);
        }
        break;
    case S::dawn:
        line(-1, 0.38f, 1, 0.38f, 1.3f);
        line(-0.62f, 0.62f, 0.62f, 0.62f); line(-0.3f, 0.84f, 0.3f, 0.84f);
        points(arc(0, 0.38f, 0.48f, 0.48f, pi, 2 * pi), true, 0.3f);
        for (int i = 0; i < 7; ++i) {
            const float a = pi + pi * (i + 1) / 8;
            line(std::cos(a) * 0.62f, 0.38f + std::sin(a) * 0.62f, std::cos(a) * 0.95f, 0.38f + std::sin(a) * 0.95f);
        }
        break;
    case S::infinity: {
        std::vector<D2D1_POINT_2F> list;
        for (int i = 0; i < 64; ++i) {
            const float t = 2 * pi * i / 64, d = 1 + std::sin(t) * std::sin(t);
            list.push_back(P(0.95f * std::cos(t) / d, 1.1f * std::sin(t) * std::cos(t) / d));
        }
        points(list, true, 0.08f);
        circle(0, 0, 0.98f, 0, 0.5f);
        break;
    }
    case S::moon: {
        // A crescent opening to the right: the outer disc's rim from its upper meeting point round
        // the left to the lower one, then the inner disc's rim back up, also round its left.
        auto list = arc(0, 0, 0.82f, 0.82f, -1.332f, -5.346f, 32);
        const auto inner = arc(0.25f, -0.05f, 0.75f, 0.75f, 1.251f, 4.637f, 32);
        list.insert(list.end(), inner.begin(), inner.end());
        points(list, true, 0.35f);
        star(0.5f, -0.3f, 0.16f, 0.05f, 4, 0.9f);
        star(0.6f, 0.28f, 0.1f, 0.03f, 4, 0.9f);
        break;
    }
    case S::keyhole:
        circle(0, 0, 0.95f, 0.05f, 0.8f);
        circle(0, -0.22f, 0.3f, 0.6f);
        poly({P(-0.12f, -0.02f), P(0.12f, -0.02f), P(0.28f, 0.66f), P(-0.28f, 0.66f)}, true, 0.6f);
        break;
    case S::dice: {
        std::vector<D2D1_POINT_2F> hexagon;
        for (int i = 0; i < 6; ++i) { const float a = -pi / 2 + i * pi / 3; hexagon.push_back(P(std::cos(a) * 0.92f, std::sin(a) * 0.92f)); }
        points(hexagon, true, 0.08f);
        line(0, 0, 0, 0.92f); line(0, 0, std::cos(-pi / 6) * 0.92f, std::sin(-pi / 6) * 0.92f); line(0, 0, std::cos(pi * 7 / 6) * 0.92f, std::sin(pi * 7 / 6) * 0.92f);
        circle(0, -0.46f, 0.09f, 1);
        circle(-0.55f, 0.08f, 0.08f, 1); circle(-0.3f, 0.45f, 0.08f, 1);
        circle(0.3f, 0.05f, 0.08f, 1); circle(0.45f, 0.3f, 0.08f, 1); circle(0.6f, 0.55f, 0.08f, 1);
        break;
    }
    case S::spark:
        star(-0.1f, 0.08f, 0.95f, 0.16f, 4, 0.5f);
        star(0.6f, -0.62f, 0.3f, 0.07f, 4, 0.9f);
        star(-0.65f, -0.6f, 0.16f, 0.05f, 4, 0.9f);
        break;
    case S::flame:
        flame(0, 0.1f, 0.85f, 0.18f);
        flame(0, 0.38f, 0.42f, 0.7f);
        break;
    case S::blaze:
        flame(-0.48f, 0.22f, 0.5f, 0.25f);
        flame(0.48f, 0.22f, 0.5f, 0.25f);
        flame(0, 0.02f, 0.78f, 0.2f);
        line(-0.75f, 0.95f, 0.75f, 0.7f, 1.5f); line(-0.75f, 0.7f, 0.75f, 0.95f, 1.5f);
        break;
    case S::cup: {
        auto bowl = arc(0, -0.55f, 0.62f, 0.62f, 0, pi, 20);
        points(bowl, true, 0.2f);
        line(0, 0.08f, 0, 0.62f, 1.4f);
        line(-0.42f, 0.78f, 0.42f, 0.78f, 1.4f); line(-0.16f, 0.62f, 0.16f, 0.62f);
        poly({P(-0.32f, -0.72f), P(-0.22f, -0.88f), P(-0.12f, -0.72f), P(-0.02f, -0.88f), P(0.08f, -0.72f), P(0.18f, -0.88f), P(0.28f, -0.72f)}, false);
        break;
    }
    case S::lightning:
        poly({P(0.22f, -1), P(-0.42f, 0.12f), P(0.0f, 0.12f), P(-0.25f, 1), P(0.5f, -0.22f), P(0.08f, -0.22f), P(0.42f, -1)}, true, 0.35f);
        break;
    case S::heart: {
        std::vector<D2D1_POINT_2F> list;
        for (int i = 0; i < 48; ++i) {
            const float t = 2 * pi * i / 48;
            const float x = 16 * std::pow(std::sin(t), 3.0f);
            const float y = 13 * std::cos(t) - 5 * std::cos(2 * t) - 2 * std::cos(3 * t) - std::cos(4 * t);
            list.push_back(P(x / 17.0f, -y / 17.0f + 0.05f));
        }
        points(list, true, 0.3f);
        star(0, -0.05f, 0.18f, 0.05f, 4, 0.9f);
        break;
    }
    case S::figure:
        person(0, 0.12f, 1.0f);
        star(0.62f, -0.75f, 0.12f, 0.04f, 4, 0.9f);
        break;
    case S::fellowship:
        person(-0.56f, 0.28f, 0.62f);
        person(0.56f, 0.28f, 0.62f);
        person(0, 0.05f, 0.78f);
        break;
    case S::swords:
        for (const float side : {-1.0f, 1.0f}) {
            line(-0.8f * side, 0.82f, 0.72f * side, -0.72f, 1.4f);
            line(-0.75f * side, 0.42f, -0.4f * side, 0.78f, 1.4f);
            circle(-0.86f * side, 0.88f, 0.07f, 1);
        }
        break;
    case S::castle:
        poly({P(-0.85f, 0.88f), P(-0.85f, -0.55f), P(-0.72f, -0.55f), P(-0.72f, -0.68f), P(-0.58f, -0.68f), P(-0.58f, -0.55f), P(-0.45f, -0.55f),
              P(-0.45f, -0.05f), P(-0.25f, -0.05f), P(-0.25f, -0.82f), P(-0.12f, -0.82f), P(-0.12f, -0.68f), P(0.12f, -0.68f), P(0.12f, -0.82f),
              P(0.25f, -0.82f), P(0.25f, -0.05f), P(0.45f, -0.05f), P(0.45f, -0.55f), P(0.58f, -0.55f), P(0.58f, -0.68f), P(0.72f, -0.68f),
              P(0.72f, -0.55f), P(0.85f, -0.55f), P(0.85f, 0.88f)}, true, 0.12f);
        points(arc(0, 0.62f, 0.22f, 0.26f, pi, 2 * pi, 12), false);
        line(-0.22f, 0.62f, -0.22f, 0.88f); line(0.22f, 0.62f, 0.22f, 0.88f);
        line(0, -0.82f, 0, -1.05f); poly({P(0, -1.05f), P(0.26f, -0.98f), P(0, -0.9f)}, true, 0.8f);
        break;
    case S::planet: {
        circle(0, 0, 0.46f, 0.25f, 1.2f);
        std::vector<D2D1_POINT_2F> ring;
        for (int i = 0; i <= 40; ++i) {
            const float t = 2 * pi * i / 40, x = std::cos(t) * 0.98f, y = std::sin(t) * 0.26f, tilt = -0.35f;
            ring.push_back(P(x * std::cos(tilt) - y * std::sin(tilt), x * std::sin(tilt) + y * std::cos(tilt)));
        }
        points(ring, true);
        circle(-0.72f, -0.72f, 0.05f, 1); circle(0.78f, 0.7f, 0.04f, 1); circle(0.6f, -0.85f, 0.035f, 1);
        break;
    }
    case S::book:
        poly({P(0, -0.52f), P(-0.92f, -0.68f), P(-0.92f, 0.6f), P(0, 0.76f)}, true, 0.12f);
        poly({P(0, -0.52f), P(0.92f, -0.68f), P(0.92f, 0.6f), P(0, 0.76f)}, true, 0.12f);
        for (int i = 0; i < 4; ++i) {
            const float y = -0.32f + i * 0.24f;
            line(-0.76f, y - 0.03f, -0.16f, y + 0.06f, 0.7f);
            line(0.16f, y + 0.06f, 0.76f, y - 0.03f, 0.7f);
        }
        break;
    case S::skull: {
        auto head = arc(0, -0.12f, 0.72f, 0.7f, pi * 0.82f, pi * 2.18f, 30);
        head.push_back(P(0.42f, 0.62f)); head.push_back(P(-0.42f, 0.62f));
        points(head, true, 0.12f);
        circle(-0.28f, -0.05f, 0.18f, 1); circle(0.28f, -0.05f, 0.18f, 1);
        poly({P(0, 0.18f), P(-0.08f, 0.34f), P(0.08f, 0.34f)}, true, 1);
        for (int i = -2; i <= 2; ++i) line(i * 0.14f, 0.5f, i * 0.14f, 0.74f, 0.8f);
        line(-0.36f, 0.76f, 0.36f, 0.76f);
        break;
    }
    case S::mosaic: {
        static constexpr bool pattern[16] = {true, false, true, true, false, true, true, false, true, true, false, true, false, true, false, true};
        for (int i = 0; i < 16; ++i) {
            const float u = -0.84f + (i % 4) * 0.44f, v = -0.84f + (i / 4) * 0.44f;
            poly({P(u, v), P(u + 0.36f, v), P(u + 0.36f, v + 0.36f), P(u, v + 0.36f)}, true, pattern[i] ? 0.85f : 0.0f);
        }
        break;
    }
    case S::mirror:
        circle(0, -0.25f, 0.62f, 0.1f, 1.3f);
        points(arc(0, -0.25f, 0.48f, 0.48f, 0, 2 * pi, 28), true);
        line(0, 0.37f, 0, 0.96f, 1.6f);
        line(-0.2f, 0.62f, 0.2f, 0.62f);
        line(-0.25f, -0.5f, -0.05f, -0.68f, 0.9f); line(-0.22f, -0.3f, 0.08f, -0.58f, 0.9f);
        break;
    case S::quill: {
        poly({P(-0.62f, 0.92f), P(0.62f, -0.95f)}, false);
        poly({P(-0.3f, 0.45f), P(-0.25f, -0.15f), P(0.1f, -0.65f), P(0.62f, -0.95f), P(0.42f, -0.4f), P(0.1f, 0.05f), P(-0.3f, 0.45f)}, true, 0.2f);
        for (int i = 1; i < 5; ++i) { const float t = i / 5.0f; line(lerp(-0.4f, 0.5f, t), lerp(0.62f, -0.8f, t), lerp(-0.16f, 0.6f, t) , lerp(0.58f, -0.62f, t), 0.7f); }
        circle(0.55f, 0.72f, 0.12f, 1);
        break;
    }
    case S::eclipse:
        for (int i = 0; i < 16; ++i) { const float a = i * pi / 8; line(std::cos(a) * 0.72f, std::sin(a) * 0.72f, std::cos(a) * (i % 2 ? 0.86f : 1.0f), std::sin(a) * (i % 2 ? 0.86f : 1.0f), 0.8f); }
        circle(0, 0, 0.58f, 0.45f, 1.2f);
        // The shadow passing over the sun: a dark disc with a gold rim.
        brush(rgba(0.09f, 0.09f, 0.10f, ink.a));
        target_->FillEllipse(D2D1::Ellipse(P(0.2f, -0.12f), 0.5f * s, 0.5f * s), solid_.Get());
        circle(0.2f, -0.12f, 0.5f, 0, 0.8f);
        break;
    case S::gem:
        poly({P(-0.85f, -0.28f), P(-0.48f, -0.72f), P(0.48f, -0.72f), P(0.85f, -0.28f), P(0, 0.9f)}, true, 0.18f);
        line(-0.85f, -0.28f, 0.85f, -0.28f);
        poly({P(-0.48f, -0.72f), P(-0.2f, -0.28f), P(0, 0.9f)}, false);
        poly({P(0.48f, -0.72f), P(0.2f, -0.28f), P(0, 0.9f)}, false);
        poly({P(-0.2f, -0.28f), P(0, -0.72f), P(0.2f, -0.28f)}, false);
        star(0.62f, -0.82f, 0.16f, 0.05f, 4, 0.9f);
        break;
    case S::gift:
        poly({P(-0.72f, -0.12f), P(0.72f, -0.12f), P(0.72f, 0.88f), P(-0.72f, 0.88f)}, true, 0.15f);
        poly({P(-0.84f, -0.42f), P(0.84f, -0.42f), P(0.84f, -0.12f), P(-0.84f, -0.12f)}, true, 0.3f);
        line(0, -0.42f, 0, 0.88f, 1.3f);
        points(arc(-0.24f, -0.6f, 0.24f, 0.16f, 0, 2 * pi, 18), true, 0.3f);
        points(arc(0.24f, -0.6f, 0.24f, 0.16f, 0, 2 * pi, 18), true, 0.3f);
        break;
    case S::scales:
        line(0, -0.78f, 0, 0.82f, 1.4f);
        line(-0.42f, 0.84f, 0.42f, 0.84f, 1.4f);
        line(-0.82f, -0.55f, 0.82f, -0.55f, 1.3f);
        circle(0, -0.86f, 0.08f, 1);
        for (const float side : {-1.0f, 1.0f}) {
            line(0.82f * side, -0.55f, (0.82f - 0.22f) * side, 0.08f, 0.8f);
            line(0.82f * side, -0.55f, (0.82f + 0.18f) * side, 0.08f, 0.8f);
            points(arc(0.8f * side, 0.08f, 0.26f, 0.2f, 0, pi, 12), true, 0.35f);
        }
        break;
    case S::gate: {
        auto outer = arc(0, -0.15f, 0.78f, 0.78f, pi, 2 * pi, 24);
        outer.insert(outer.begin(), P(-0.78f, 0.92f));
        outer.push_back(P(0.78f, 0.92f));
        points(outer, false);
        auto inner = arc(0, -0.1f, 0.5f, 0.52f, pi, 2 * pi, 20);
        inner.insert(inner.begin(), P(-0.5f, 0.92f));
        inner.push_back(P(0.5f, 0.92f));
        points(inner, true, 0.3f);
        line(-1, 0.92f, 1, 0.92f, 1.3f);
        star(0, -0.15f, 0.2f, 0.06f, 4, 0.9f);
        break;
    }
    case S::road:
        line(-1, -0.5f, 1, -0.5f);
        line(-0.92f, 0.92f, -0.08f, -0.5f, 1.3f);
        line(0.92f, 0.92f, 0.08f, -0.5f, 1.3f);
        line(0, 0.82f, 0, 0.55f, 1.2f); line(0, 0.32f, 0, 0.12f, 1.0f); line(0, -0.04f, 0, -0.18f, 0.8f);
        star(0, -0.78f, 0.22f, 0.06f, 4, 0.9f);
        break;
    }
}

void Renderer::drawTarotCardBack(D2D1_RECT_F r, float alpha) {
    const float w = r.right - r.left, cx = (r.left + r.right) * 0.5f, cy = (r.top + r.bottom) * 0.5f;
    const float radius = w * 0.07f;
    // A plain grey card, as Tryb TV's tiles: a soft gradient, one thin frame and a single mark.
    ComPtr<ID2D1GradientStopCollection> stops;
    ComPtr<ID2D1LinearGradientBrush> fill;
    const D2D1_GRADIENT_STOP colors[] = {{0, rgba(0.20f, 0.20f, 0.22f, alpha)}, {1, rgba(0.09f, 0.09f, 0.10f, alpha)}};
    if (SUCCEEDED(target_->CreateGradientStopCollection(colors, 2, stops.GetAddressOf())) &&
        SUCCEEDED(target_->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(D2D1::Point2F(cx, r.top), D2D1::Point2F(cx, r.bottom)),
                                                     stops.Get(), fill.GetAddressOf())))
        target_->FillRoundedRectangle(D2D1::RoundedRect(r, radius, radius), fill.Get());
    brush(rgba(1, 1, 1, 0.16f * alpha));
    target_->DrawRoundedRectangle(D2D1::RoundedRect(inflate(r, -1), radius, radius), solid_.Get(), 1.2f);
    brush(rgba(1, 1, 1, 0.10f * alpha));
    target_->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), w * 0.26f, w * 0.26f), solid_.Get(), 1.0f);
    drawTarotSymbol(tarot::Symbol::star, cx, cy, w * 0.26f, rgba(0.85f, 0.85f, 0.87f, 0.55f * alpha), 1.3f);
}
void Renderer::drawTarotCard(const TarotCardLook& look, const tarot::Card* face, Cover* poster, const std::wstring& numeral) {
    if (look.alpha <= 0.003f || look.width <= 1) return;
    D2D1_MATRIX_3X2_F saved{};
    target_->GetTransform(&saved);
    const float turn = std::clamp(look.turn, 0.0f, 1.0f);
    const float squeeze = std::max(0.015f, std::abs(std::cos(turn * pi))); // Its width, edge-on halfway.
    const bool front = turn >= 0.5f;
    const float scale = look.width / card_units;
    const float units_h = look.height / scale;
    const float alpha = look.alpha;
    target_->SetTransform(D2D1::Matrix3x2F::Scale(scale * squeeze, scale) * D2D1::Matrix3x2F::Rotation(look.rotation) *
                          D2D1::Matrix3x2F::Translation(look.cx, look.cy - look.lift) * D2D1::Matrix3x2F::ReinterpretBaseType(&saved)[0]);
    const auto r = D2D1::RectF(-card_units * 0.5f, -units_h * 0.5f, card_units * 0.5f, units_h * 0.5f);
    const float radius = card_units * 0.07f;
    // Its shadow, falling further while it is lifted; its glow while chosen or focused.
    for (int i = 1; i <= 6; ++i) {
        brush(rgba(0, 0, 0, 0.055f * alpha));
        const float spread = static_cast<float>(i) * 2.4f;
        auto shadow = inflate(r, spread);
        shadow.top += 8 + look.lift * 0.4f; shadow.bottom += 8 + look.lift * 0.4f;
        target_->FillRoundedRectangle(D2D1::RoundedRect(shadow, radius + spread, radius + spread), solid_.Get());
    }
    if (look.glow > 0.01f) {
        for (int i = 1; i <= 7; ++i) {
            brush(rgba(1, 1, 1, 0.03f * look.glow * alpha));
            const float spread = static_cast<float>(i) * 3.6f;
            target_->FillRoundedRectangle(D2D1::RoundedRect(inflate(r, spread), radius + spread, radius + spread), solid_.Get());
        }
    }
    if (!front) drawTarotCardBack(r, alpha);
    else if (!face) {
        // A game: its poster in a gold frame; the card's pattern under it until the image comes.
        drawTarotCardBack(r, alpha);
        const auto picture = inflate(r, -9);
        const float shown = poster && poster->bitmap ? reveal(*poster) : 0.0f;
        if (shown > 0) {
            brush(rgba(0.06f, 0.06f, 0.07f, alpha * shown));
            target_->FillRoundedRectangle(D2D1::RoundedRect(picture, radius * 0.6f, radius * 0.6f), solid_.Get());
            fillImage(*poster, picture, radius * 0.6f, alpha * shown);
        }
        brush(rgba(1, 1, 1, 0.18f * alpha));
        target_->DrawRoundedRectangle(D2D1::RoundedRect(inflate(r, -1), radius, radius), solid_.Get(), 1.2f);
    } else {
        // The face: a grey card like Tryb TV's panels, the numeral, the symbol, the name and its meaning.
        ComPtr<ID2D1GradientStopCollection> stops;
        ComPtr<ID2D1LinearGradientBrush> fill;
        const D2D1_GRADIENT_STOP colors[] = {{0, rgba(0.19f, 0.19f, 0.21f, alpha)}, {1, rgba(0.09f, 0.09f, 0.10f, alpha)}};
        if (SUCCEEDED(target_->CreateGradientStopCollection(colors, 2, stops.GetAddressOf())) &&
            SUCCEEDED(target_->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(D2D1::Point2F(0, r.top), D2D1::Point2F(0, r.bottom)),
                                                         stops.Get(), fill.GetAddressOf())))
            target_->FillRoundedRectangle(D2D1::RoundedRect(r, radius, radius), fill.Get());
        brush(rgba(1, 1, 1, 0.16f * alpha));
        target_->DrawRoundedRectangle(D2D1::RoundedRect(inflate(r, -1), radius, radius), solid_.Get(), 1.2f);
        const float numeral_y = r.top + 30;
        textLine(numeral, r.left, numeral_y, card_units, tarot_numeral_font_.Get(), lavender(alpha), DWRITE_TEXT_ALIGNMENT_CENTER);
        // The symbol on a faint disc.
        const float symbol_y = r.top + units_h * 0.43f;
        brush(rgba(1, 1, 1, 0.05f * alpha));
        target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(0, symbol_y), 74, 74), solid_.Get());
        drawTarotSymbol(face->symbol, 0, symbol_y, 96, brightGold(alpha), 2.2f);
        // The name, the meaning under it.
        const float band_top = r.bottom - units_h * 0.27f;
        textLine(upper(tr(face->name)), r.left + 14, band_top + 24, card_units - 28, tarot_name_font_.Get(), brightGold(alpha), DWRITE_TEXT_ALIGNMENT_CENTER);
        drawGlanceText(tr(face->meaning), D2D1::RectF(r.left + 18, band_top + 42, r.right - 18, r.bottom - 14), tarot_body_font_.Get(), parchment(0.86f * alpha));
    }
    // A sheen sweeping over it as it turns.
    if (turn > 0.02f && turn < 0.98f) {
        brush(rgba(1, 1, 1, 0.16f * (1 - squeeze) * alpha));
        target_->FillRoundedRectangle(D2D1::RoundedRect(r, radius, radius), solid_.Get());
    }
    target_->SetTransform(saved);
}

void Renderer::drawTarotBackdrop(float alpha, float center_y, float radius) {
    const float w = viewport_.width, h = viewport_.height, cx = w * 0.5f;
    // The home dimmed to a quiet grey, a little lighter behind the cards: no sky, no sigil.
    brush(rgba(0.035f, 0.035f, 0.04f, 0.93f * alpha));
    target_->FillRectangle(D2D1::RectF(0, 0, w, h), solid_.Get());
    ComPtr<ID2D1GradientStopCollection> stops;
    ComPtr<ID2D1RadialGradientBrush> light;
    const D2D1_GRADIENT_STOP colors[] = {{0, rgba(0.22f, 0.22f, 0.24f, 0.45f * alpha)}, {0.6f, rgba(0.12f, 0.12f, 0.13f, 0.18f * alpha)}, {1, rgba(0, 0, 0, 0)}};
    if (SUCCEEDED(target_->CreateGradientStopCollection(colors, 3, stops.GetAddressOf())) &&
        SUCCEEDED(target_->CreateRadialGradientBrush(D2D1::RadialGradientBrushProperties(D2D1::Point2F(cx, center_y), {}, std::max(radius * 2.4f, w * 0.45f), h * 0.62f),
                                                     stops.Get(), light.GetAddressOf())))
        target_->FillRectangle(D2D1::RectF(0, 0, w, h), light.Get());
}
void Renderer::drawTarot() {
    const auto* state = context_ ? context_->tarot : nullptr;
    if (!state || state->alpha <= 0.003f || !createTarotFonts()) return;
    const auto& t = *state;
    const float alpha = tarot_ui::smooth(t.alpha);
    const tarot_ui::Layout layout{viewport_.width, viewport_.height};
    const float w = viewport_.width, h = viewport_.height, cx = w * 0.5f;
    const bool reading = t.phase == Phase::reading;
    drawTarotBackdrop(alpha, reading ? layout.postersTop() + layout.posterHeight() * 0.5f : layout.cardsCenter(),
                      reading ? layout.posterHeight() * 0.62f : layout.cardHeight() * 0.85f);

    // Back (top left), as Glance's.
    const auto back = layout.back();
    const float back_focus = t.focus == tarot_ui::focus_back ? 1.0f : 0.0f;
    drawButtonGlass(box(back), alpha, false, t.hover == tarot_ui::focus_back);
    drawButtonFocus(box(back), back_focus * alpha);
    {
        const auto ink = buttonInk(back_focus);
        const auto faded = rgba(ink.r, ink.g, ink.b, ink.a * alpha);
        const float y = (back.top + back.bottom) * 0.5f;
        line(back.left + 22, y, back.left + 34, y, faded, 2);
        line(back.left + 22, y, back.left + 28, y - 6, faded, 2);
        line(back.left + 22, y, back.left + 28, y + 6, faded, 2);
    }

    // The title and its marks: one diamond per card of the reading, the answered ones filled.
    textLine(L"Tarot", 0, 76, w, tarot_title_font_.Get(), parchment(alpha), DWRITE_TEXT_ALIGNMENT_CENTER);
    const size_t steps = t.steps();
    if (!reading && steps) {
        const float gap = 22, first = cx - gap * static_cast<float>(steps - 1) * 0.5f;
        for (size_t i = 0; i < steps; ++i) {
            const float x = first + gap * static_cast<float>(i), y = 134;
            const bool done = i < t.step(), current = i == t.step();
            const float r = current ? 6.5f + 1.2f * std::sin(t.time * 3.2f) : 5;
            const D2D1_POINT_2F diamond[] = {{x, y - r}, {x + r, y}, {x, y + r}, {x - r, y}};
            ComPtr<ID2D1PathGeometry> shape; ComPtr<ID2D1GeometrySink> sink;
            if (FAILED(factory_->CreatePathGeometry(shape.GetAddressOf())) || FAILED(shape->Open(sink.GetAddressOf()))) continue;
            sink->BeginFigure(diamond[0], D2D1_FIGURE_BEGIN_FILLED);
            sink->AddLines(diamond + 1, 3);
            sink->EndFigure(D2D1_FIGURE_END_CLOSED);
            if (FAILED(sink->Close())) continue;
            if (done) { brush(gold(0.9f * alpha)); target_->FillGeometry(shape.Get(), solid_.Get()); }
            brush(current ? brightGold(alpha) : gold((done ? 0.9f : 0.4f) * alpha));
            target_->DrawGeometry(shape.Get(), solid_.Get(), 1.2f);
        }
    }

    // The line of hints: keys, or the pad's buttons by position (%1 the bottom face button, %2 the
    // right one, %3 View/Create/−) as the bottom-right hints name them.
    const auto device = context_->hint_device;
    const bool pad = device != HintDevice::keyboard;
    const auto hint = [&](const wchar_t* keys, const wchar_t* buttons) {
        std::wstring text = tr(pad ? buttons : keys);
        const wchar_t* names[] = {device == HintDevice::playstation ? L"✕" : device == HintDevice::nintendo ? L"B" : L"A",
                                  device == HintDevice::playstation ? L"○" : device == HintDevice::nintendo ? L"A" : L"B",
                                  device == HintDevice::playstation ? L"Create" : device == HintDevice::nintendo ? L"−" : L"View"};
        for (int i = 0; i < 3; ++i)
            for (size_t at = 0; (at = text.find(L"%" + std::to_wstring(i + 1), at)) != std::wstring::npos;) { text.replace(at, 2, names[i]); at += wcslen(names[i]); }
        textLine(text, 0, h - 40, w, tarot_small_font_.Get(), lavender(0.62f * alpha), DWRITE_TEXT_ALIGNMENT_CENTER);
    };

    if (t.phase == Phase::question || t.phase == Phase::leaving) {
        const auto* spread = t.spread();
        if (!spread) return;
        const auto& question = tarot::question(spread->facet);
        const bool leaving = t.phase == Phase::leaving;
        const float question_alpha = alpha * (leaving ? 1 - tarot_ui::ease(t.phase_age / 0.35f) : tarot_ui::ease(t.phase_age / 0.45f));
        drawGlanceText(tr(question.prompt), D2D1::RectF(120, 160, w - 120, 250), tarot_question_font_.Get(), parchment(question_alpha));
        const int count = static_cast<int>(spread->cards.size());
        const float card_w = layout.cardWidth(), card_h = layout.cardHeight();
        const float deck_x = cx, deck_y = h + card_h * 0.75f;
        const float dealt_age = leaving ? 100.0f : t.phase_age;
        std::vector<int> order;
        for (int i = 0; i < count; ++i) if (i != t.focus && i != t.chosen) order.push_back(i);
        for (const int i : {t.focus, t.chosen}) if (i >= 0 && i < count && std::find(order.begin(), order.end(), i) == order.end()) order.push_back(i);
        for (const int i : order) {
            const auto rest = layout.card(i, count);
            const float rest_x = (rest.left + rest.right) * 0.5f, rest_y = (rest.top + rest.bottom) * 0.5f;
            const float local = dealt_age - static_cast<float>(i) * tarot_ui::deal_stagger;
            if (local <= 0) continue;
            const float flight = tarot_ui::ease(local / tarot_ui::deal_time);
            const float fan = (static_cast<float>(i) - (count - 1) * 0.5f) * 2.4f;
            TarotCardLook look;
            look.width = card_w; look.height = card_h;
            look.cx = lerp(deck_x, rest_x, flight);
            look.cy = lerp(deck_y, rest_y, flight) - std::sin(flight * pi) * card_h * 0.16f;
            look.rotation = lerp(-24.0f + static_cast<float>(i) * 13.0f, fan, flight);
            look.turn = tarot_ui::smooth((local - tarot_ui::deal_time * 0.4f) / (tarot_ui::deal_time * 0.75f));
            look.lift = t.lift[static_cast<size_t>(std::clamp(i, 0, static_cast<int>(tarot_ui::max_cards) - 1))] * 24;
            look.glow = look.lift / 24 * 0.8f;
            look.alpha = alpha * std::min(1.0f, local / 0.12f);
            if (leaving) {
                const float age = t.phase_age;
                if (i == t.chosen) {
                    const float rise = tarot_ui::ease(age / 0.45f);
                    look.cy -= rise * card_h * 0.12f + tarot_ui::smooth((age - 0.4f) / 0.32f) * card_h * 0.35f;
                    look.width *= 1 + 0.07f * rise; look.height *= 1 + 0.07f * rise;
                    look.rotation = lerp(fan, 0, rise);
                    look.glow = 1 + rise;
                    look.lift = 24;
                    look.alpha *= 1 - tarot_ui::smooth((age - 0.42f) / 0.3f);
                } else {
                    const float fall = tarot_ui::ease(age / 0.55f);
                    look.cy += fall * card_h * 0.75f;
                    look.rotation += (i < t.chosen ? -1.0f : 1.0f) * 22 * fall;
                    look.alpha *= 1 - tarot_ui::smooth(age / 0.45f);
                    look.lift = 0; look.glow = 0;
                }
            }
            const int card = spread->cards[static_cast<size_t>(i)];
            const auto& face = question.cards[static_cast<size_t>(card)];
            drawTarotCard(look, &face, nullptr, roman(arcana(face.symbol)));
        }
        if (!leaving) hint(trKey(L"Wybierz kartę: strzałki i Enter albo kliknięcie · Backspace cofa · Esc zamyka"), trKey(L"Wybierz kartę: %1 wybiera · %2 cofa · %3 zamyka"));
        return;
    }

    if (t.phase == Phase::divining) {
        // The deck shuffles: face-down cards circling and crossing over.
        const float card_w = layout.cardWidth() * 0.82f, card_h = layout.cardHeight() * 0.82f, cy = layout.cardsCenter();
        const float shown = alpha * tarot_ui::ease(t.phase_age / 0.4f);
        for (int k = 0; k < 6; ++k) {
            const float a = t.time * 2.1f + k * 2 * pi / 6;
            const float sweep = 0.55f + 0.45f * std::sin(t.time * 1.6f + k);
            TarotCardLook look;
            look.width = card_w; look.height = card_h;
            look.cx = cx + std::cos(a) * card_w * 1.25f * sweep;
            look.cy = cy + std::sin(a) * card_h * 0.12f;
            look.rotation = std::sin(a) * 14 + std::cos(t.time * 0.9f + k) * 6;
            look.turn = 0;
            look.alpha = shown * (0.55f + 0.45f * (0.5f + 0.5f * std::sin(a)));
            look.glow = 0.25f;
            drawTarotCard(look, nullptr, nullptr, {});
        }
        const int dots = static_cast<int>(t.time * 2.5f) % 4;
        textLine(tr(L"Karty szepczą") + std::wstring(static_cast<size_t>(dots), L'.'), 0, cy + layout.cardHeight() * 0.62f, w,
                 tarot_question_font_.Get(), parchment(0.9f * shown), DWRITE_TEXT_ALIGNMENT_CENTER);
        return;
    }

    // The reading.
    const bool store = t.pool == tarot::Pool::store;
    const int count = static_cast<int>(t.results.size());
    const float intro = alpha * tarot_ui::ease(t.phase_age / 0.5f);
    if (!count) {
        textLine(tr(L"Karty milczą"), 0, h * 0.4f, w, tarot_question_font_.Get(), gold(intro), DWRITE_TEXT_ALIGNMENT_CENTER);
        const auto* reason = t.failed ? tr(L"Nie udało się zapytać Steam. Sprawdź połączenie i rozłóż karty ponownie.") :
                             store ? tr(L"Na Steam nie znalazło się nic, czego jeszcze nie masz. Spróbuj innych kart.") :
                                     tr(L"Żadna gra z biblioteki nie pasuje do tej wróżby. Spróbuj innych kart.");
        drawGlanceText(reason, D2D1::RectF(160, h * 0.46f, w - 160, h * 0.6f), tarot_body_font_.Get(), parchment(0.85f * intro));
    } else {
        textLine(store ? tr(L"Do kupienia na Steam") : tr(L"Z Twojej biblioteki"), 0, 134, w, tarot_small_font_.Get(), lavender(0.85f * intro), DWRITE_TEXT_ALIGNMENT_CENTER);
        // The cards that made this reading, in the order they were chosen.
        std::wstring drawn;
        for (size_t i = 0; i < t.reading.answers.size() && i < t.reading.spreads.size(); ++i)
            if (const auto* card = t.reading.answer(t.reading.spreads[i].facet)) drawn += (drawn.empty() ? L"" : L"  ·  ") + std::wstring(tr(card->name));
        textLine(drawn, 60, 158, w - 120, tarot_numeral_font_.Get(), gold(0.55f * intro), DWRITE_TEXT_ALIGNMENT_CENTER);
        static constexpr const wchar_t* positions[] = {trKey(L"Przeznaczenie"), trKey(L"Pokusa"), trKey(L"Dzika karta")};
        for (int i = 0; i < count; ++i) {
            const auto rest = layout.poster(i, count);
            const float local = t.phase_age - tarot_ui::reveal_delay - static_cast<float>(i) * tarot_ui::reveal_stagger;
            const float rise = tarot_ui::ease((t.phase_age - static_cast<float>(i) * 0.08f) / 0.5f);
            TarotCardLook look;
            look.width = rest.right - rest.left; look.height = rest.bottom - rest.top;
            look.cx = (rest.left + rest.right) * 0.5f;
            look.cy = (rest.top + rest.bottom) * 0.5f + (1 - rise) * look.height * 0.35f;
            look.turn = tarot_ui::smooth(local / tarot_ui::flip_time);
            const bool focused = t.focus == i;
            look.lift = focused ? 14.0f : t.hover == i ? 7.0f : 0.0f;
            look.glow = focused ? 0.9f + 0.25f * std::sin(t.time * 2.6f) : 0.15f;
            look.alpha = alpha * rise;
            const auto index = static_cast<size_t>(i);
            Cover* image = index < tarot_images_.size() && tarot_images_[index].bitmap ? &tarot_images_[index] : nullptr;
            drawTarotCard(look, nullptr, image, {});
            const float words = alpha * tarot_ui::smooth((local - tarot_ui::flip_time * 0.55f) / 0.45f);
            const float label_y = rest.top - 26;
            textLine(roman(i + 1) + L" · " + upper(tr(positions[std::min(i, 2)])), rest.left - 30, label_y, rest.right - rest.left + 60,
                     tarot_numeral_font_.Get(), gold(words), DWRITE_TEXT_ALIGNMENT_CENTER);
            const auto& result = t.results[index];
            const float text_w = rest.right - rest.left + 60;
            const float title_height = drawGlanceText(result.title, D2D1::RectF(rest.left - 30, rest.bottom + 20, rest.left - 30 + text_w, rest.bottom + 78),
                                                      tarot_result_font_.Get(), parchment((focused ? 1.0f : 0.88f) * words));
            textLine(result.detail, rest.left - 30, rest.bottom + 32 + title_height, text_w, tarot_small_font_.Get(), lavender(0.82f * words), DWRITE_TEXT_ALIGNMENT_CENTER);
        }
    }
    const float buttons = alpha * tarot_ui::ease((t.phase_age - (count ? 0.6f : 0.2f)) / 0.4f);
    for (const int which : {tarot_ui::focus_again, tarot_ui::focus_close}) {
        const auto r = box(layout.button(which));
        const auto ink = drawButton(r, t.focus == which ? 1.0f : 0.0f, buttons, false, t.hover == which);
        textLine(which == tarot_ui::focus_again ? tr(L"Rozłóż ponownie") : tr(L"Zamknij"), r.left, (r.top + r.bottom) * 0.5f, r.right - r.left,
                 settings_body_font_.Get(), rgba(ink.r, ink.g, ink.b, ink.a * buttons), DWRITE_TEXT_ALIGNMENT_CENTER);
    }
    if (count) hint(store ? trKey(L"Enter lub kliknięcie otwiera stronę gry w sklepie · Esc zamyka") : trKey(L"Enter lub kliknięcie otwiera szczegóły gry · Esc zamyka"),
                    store ? trKey(L"%1 otwiera stronę gry w sklepie · %3 zamyka") : trKey(L"%1 otwiera szczegóły gry · %3 zamyka"));
}
