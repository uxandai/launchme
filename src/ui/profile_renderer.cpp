// Profil (src/ui/profile_view.hpp; run by src/app/profile.cpp): the player's page over the launcher.
// A dark, quiet page in the interface's accent: the header with the player's picture, this week's
// tiles and its chart, the most played games, the latest achievements as a sliding row, the year
// of achievements, the games closest to 100 % and the platforms.
#include "renderer.hpp"
#include "renderer_internal.hpp"
#include "brand_icons.hpp"

#include <algorithm>
#include <cmath>
#include <tuple>

using Microsoft::WRL::ComPtr;
using profile_ui::Focus;
using profile_ui::Section;

namespace {
using palette::gray;
D2D1_COLOR_F rgba(float r, float g, float b, float a) { return D2D1::ColorF(r, g, b, std::clamp(a, 0.0f, 1.0f)); }
using render_detail::box;
D2D1_RECT_F shifted(settings_ui::Rect r, float dy) { return D2D1::RectF(r.left, r.top + dy, r.right, r.bottom + dy); }
const D2D1_COLOR_F gold_ink = D2D1::ColorF(1.0f, 0.80f, 0.38f, 1);
const D2D1_COLOR_F good_ink = D2D1::ColorF(0.52f, 0.86f, 0.62f, 1);
} // namespace

void Renderer::setProfileImage(size_t index, ImageBytes pixels) {
    if (index >= profile_images_.size() || !pixels || pixels->empty()) return;
    auto& image = profile_images_[index];
    if (image.pixels == pixels) return;
    image = {};
    image.short_edge = 256; // Thumbnails and the player's picture: never large.
    image.pixels = std::move(pixels);
    createBitmap(image);
}

bool Renderer::createProfileFonts() {
    if (profile_fonts_family_ == font_family_ && profile_name_font_ && profile_value_font_ && profile_big_font_ && profile_title_font_ &&
        profile_body_font_ && profile_small_font_) return true;
    profile_fonts_family_ = font_family_;
    profile_name_font_.Reset(); profile_value_font_.Reset(); profile_big_font_.Reset();
    profile_title_font_.Reset(); profile_body_font_.Reset(); profile_small_font_.Reset();
    return font(profile_name_font_, 40, DWRITE_FONT_WEIGHT_SEMI_BOLD) && font(profile_value_font_, 30, DWRITE_FONT_WEIGHT_SEMI_BOLD) &&
           font(profile_big_font_, 36, DWRITE_FONT_WEIGHT_LIGHT) && font(profile_title_font_, 17, DWRITE_FONT_WEIGHT_SEMI_BOLD) &&
           font(profile_body_font_, 15, DWRITE_FONT_WEIGHT_NORMAL) && font(profile_small_font_, 13, DWRITE_FONT_WEIGHT_NORMAL);
}

// A card of the page: a faint panel; hover lightens it, the focus adds the accent outline and glow.
void Renderer::drawProfileCard(D2D1_RECT_F r, float alpha, float focus, float hover) {
    const auto rounded = D2D1::RoundedRect(r, 16, 16);
    if (focus > 0.01f) {
        for (int i = 3; i >= 1; --i) { // A soft halo in the accent.
            const float spread = static_cast<float>(i) * 3.5f;
            brush(uiAccent(0.05f * focus * alpha));
            target_->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(r.left - spread, r.top - spread, r.right + spread, r.bottom + spread), 16 + spread, 16 + spread),
                                          solid_.Get());
        }
    }
    brush(gray(1, (0.045f + 0.035f * hover + 0.03f * focus) * alpha));
    target_->FillRoundedRectangle(rounded, solid_.Get());
    brush(focus > 0.01f ? uiAccent(0.9f * focus * alpha) : gray(1, 0.075f * alpha));
    target_->DrawRoundedRectangle(rounded, solid_.Get(), focus > 0.01f ? 2.0f : 1.0f);
}

// A picture of the page (a cover, an icon, the player's face); without one a tinted square with a
// letter, so the layout never shows holes while pictures load.
void Renderer::drawProfileImage(size_t index, D2D1_RECT_F r, float radius, float alpha, const std::wstring& fallback) {
    auto* image = index < profile_images_.size() && profile_images_[index].bitmap ? &profile_images_[index] : nullptr;
    if (image) {
        brush(gray(0.08f, alpha)); target_->FillRoundedRectangle(D2D1::RoundedRect(r, radius, radius), solid_.Get());
        fillImage(*image, r, radius, alpha * reveal(*image));
        return;
    }
    ComPtr<ID2D1GradientStopCollection> stops;
    ComPtr<ID2D1LinearGradientBrush> fill;
    const D2D1_GRADIENT_STOP colors[] = {{0, uiAccent(0.36f * alpha)}, {1, gray(0.1f, 0.9f * alpha)}};
    if (SUCCEEDED(target_->CreateGradientStopCollection(colors, 2, stops.GetAddressOf())) &&
        SUCCEEDED(target_->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(D2D1::Point2F(r.left, r.top), D2D1::Point2F(r.right, r.bottom)),
                                                     stops.Get(), fill.GetAddressOf())))
        target_->FillRoundedRectangle(D2D1::RoundedRect(r, radius, radius), fill.Get());
    if (!fallback.empty()) {
        const float h = r.bottom - r.top;
        auto* format = h >= 90 ? profile_name_font_.Get() : profile_title_font_.Get();
        textLine(fallback, r.left, (r.top + r.bottom) * 0.5f, r.right - r.left, format, gray(0.96f, alpha), DWRITE_TEXT_ALIGNMENT_CENTER);
    }
}

// Desktop mode: in the library's place beside the sidebar (ViewContext::profile_dock_*), at the
// proportions of Tryb TV's layouts in a desktop window (desktopModalViewport); else over the screen.
void Renderer::drawProfile() {
    const auto* state = context_ ? context_->profile : nullptr;
    if (!state || state->alpha <= 0.003f) return;
    const float dock_w = context_->profile_dock_right - context_->profile_dock_left, dock_h = context_->profile_dock_bottom - context_->profile_dock_top;
    if (!context_->desktop || dock_w <= 0 || dock_h <= 0) { drawProfilePage(); return; }
    constexpr float compact = 0.86f;
    const auto saved = viewport_;
    D2D1_MATRIX_3X2_F transform;
    target_->GetTransform(&transform);
    const auto desk = desktopViewport();
    Viewport docked;
    docked.scale = desk.scale * compact;
    docked.width = dock_w / compact;
    docked.height = dock_h / compact;
    viewport_ = docked;
    target_->SetTransform(D2D1::Matrix3x2F::Scale(docked.scale, docked.scale) *
                          D2D1::Matrix3x2F::Translation(context_->profile_dock_left * desk.scale, context_->profile_dock_top * desk.scale));
    target_->PushAxisAlignedClip(D2D1::RectF(0, 0, docked.width, docked.height), D2D1_ANTIALIAS_MODE_ALIASED);
    drawProfilePage();
    target_->PopAxisAlignedClip();
    target_->SetTransform(transform);
    viewport_ = saved;
}

void Renderer::drawProfilePage() {
    const auto* state = context_ ? context_->profile : nullptr;
    if (!state || state->alpha <= 0.003f || !createProfileFonts()) return;
    const auto& p = *state;
    const float alpha = p.alpha * p.alpha * (3 - 2 * p.alpha);
    const float w = viewport_.width, h = viewport_.height;
    const profile_ui::Layout layout{w, h};
    const float left = layout.left(), right = layout.right();
    const float grow = profile_ui::ease(p.time / 0.9f); // The charts rise once, as the page opens.
    const auto focusOf = [&](Section section, int item) {
        const Focus here{section, item};
        const bool focused = p.focus == here && !p.pointer;
        const bool hovered = p.hover && *p.hover == here;
        return std::pair<float, float>{focused ? 1.0f : 0.0f, hovered ? 1.0f : 0.0f};
    };

    // The page: almost black, with the accent glowing from two corners.
    brush(rgba(0.016f, 0.019f, 0.025f, 0.985f * alpha));
    target_->FillRectangle(D2D1::RectF(0, 0, w, h), solid_.Get());
    for (const auto& [cx, cy, radius, strength] : {std::tuple{w * 0.12f, -h * 0.05f, w * 0.55f, 0.16f}, std::tuple{w * 0.95f, h * 1.05f, w * 0.5f, 0.09f}}) {
        ComPtr<ID2D1GradientStopCollection> stops;
        ComPtr<ID2D1RadialGradientBrush> glow;
        const D2D1_GRADIENT_STOP colors[] = {{0, uiAccent(strength * alpha)}, {1, uiAccent(0)}};
        if (SUCCEEDED(target_->CreateGradientStopCollection(colors, 2, stops.GetAddressOf())) &&
            SUCCEEDED(target_->CreateRadialGradientBrush(D2D1::RadialGradientBrushProperties(D2D1::Point2F(cx, cy), {}, radius, radius * 0.8f),
                                                         stops.Get(), glow.GetAddressOf())))
            target_->FillRectangle(D2D1::RectF(0, 0, w, h), glow.Get());
    }
    // Everything else fades as one.
    ComPtr<ID2D1Layer> fade;
    const bool faded = alpha < 0.999f && SUCCEEDED(target_->CreateLayer(nullptr, fade.GetAddressOf()));
    if (faded) target_->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), nullptr, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE, D2D1::IdentityMatrix(), alpha), fade.Get());

    const float bar_bottom = 88;
    target_->PushAxisAlignedClip(D2D1::RectF(0, bar_bottom - 6, w, h), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    const float dy = -p.scroll;
    const auto label = [&](const std::wstring& text, float top, const std::wstring& note = {}) {
        capsLine(text, left + 2, top - layout.label_h * 0.5f - 2, right - left - 300, gray(0.62f));
        if (!note.empty()) textLine(note, right - 420, top - layout.label_h * 0.5f - 2, 418, profile_small_font_.Get(), palette::muted(), DWRITE_TEXT_ALIGNMENT_TRAILING);
    };

    // ---- Header: the player.
    {
        const auto avatar = shifted(layout.avatar(), dy);
        const float size = avatar.bottom - avatar.top, radius = size * 0.5f;
        // A ring in the accent around the picture.
        brush(uiAccent(0.85f));
        target_->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(avatar.left + radius, avatar.top + radius), radius + 4, radius + 4), solid_.Get(), 2.2f);
        drawProfileImage(p.avatar ? 0 : profile_ui::no_image, avatar, radius, 1, p.initials);
        const float x = avatar.right + 28, cy = (avatar.top + avatar.bottom) * 0.5f;
        textLine(p.name, x, cy - 14, right - x - 240, profile_name_font_.Get(), palette::text());
        textLine(p.subtitle, x, cy + 26, right - x - 240, profile_body_font_.Get(), palette::muted());
        if (p.loading) {
            const float pulse = 0.55f + 0.35f * std::sin(p.time * 4.0f + static_cast<float>(GetTickCount64() % 100000) / 260.0f);
            textLine(tr(L"Wczytywanie osiągnięć…"), right - 240, cy + 26, 240, profile_small_font_.Get(), gray(0.8f, pulse), DWRITE_TEXT_ALIGNMENT_TRAILING);
        }
    }

    // ---- This week.
    label(tr(L"Ten tydzień"), layout.weekTop() + dy);
    for (int i = 0; i < 4; ++i) {
        const auto r = shifted(layout.stat(i), dy);
        drawProfileCard(r, 1, 0, 0);
        const auto& stat = p.stats[static_cast<size_t>(i)];
        const float inner = r.right - r.left - 40;
        capsLine(stat.label, r.left + 20, r.top + 26, inner, gray(0.6f));
        textLine(stat.value, r.left + 20, r.top + 60, inner, profile_value_font_.Get(), palette::text());
        textLine(stat.note, r.left + 20, r.top + 92, inner, profile_small_font_.Get(), stat.good ? good_ink : stat.bad ? palette::danger(0.9f) : palette::muted());
    }
    // The seven days.
    {
        const auto card = shifted(layout.chart(), dy);
        drawProfileCard(card, 1, 0, 0);
        capsLine(tr(L"Ostatnie 7 dni"), card.left + 22, card.top + 28, 260, gray(0.6f));
        textLine(p.week_total, card.right - 320, card.top + 28, 298, profile_small_font_.Get(), palette::muted(), DWRITE_TEXT_ALIGNMENT_TRAILING);
        const float top = card.top + 64, bottom = card.bottom - 46, slot = (card.right - card.left - 44) / 7;
        // Faint guides.
        for (int g = 0; g <= 2; ++g) {
            const float y = bottom - (bottom - top) * g / 2.0f;
            line(card.left + 22, y, card.right - 22, y, gray(1, g ? 0.04f : 0.08f), 1);
        }
        for (size_t i = 0; i < 7; ++i) {
            const float cx = card.left + 22 + slot * (static_cast<float>(i) + 0.5f), bar = std::min(46.0f, slot * 0.5f);
            const bool today = static_cast<int>(i) == p.today;
            const float share = p.week[i] * grow, height = std::max(share > 0 ? 6.0f : 3.0f, (bottom - top - 22) * share);
            const auto rect = D2D1::RectF(cx - bar * 0.5f, bottom - height, cx + bar * 0.5f, bottom);
            if (p.week[i] > 0) {
                ComPtr<ID2D1GradientStopCollection> stops;
                ComPtr<ID2D1LinearGradientBrush> fill;
                const D2D1_GRADIENT_STOP colors[] = {{0, uiAccent(today ? 1.0f : 0.78f)}, {1, uiAccent(today ? 0.55f : 0.28f)}};
                if (SUCCEEDED(target_->CreateGradientStopCollection(colors, 2, stops.GetAddressOf())) &&
                    SUCCEEDED(target_->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(D2D1::Point2F(0, rect.top), D2D1::Point2F(0, rect.bottom)),
                                                                 stops.Get(), fill.GetAddressOf())))
                    target_->FillRoundedRectangle(D2D1::RoundedRect(rect, 7, 7), fill.Get());
                if (!p.week_times[i].empty())
                    textLine(p.week_times[i], cx - slot * 0.5f, rect.top - 14, slot, profile_small_font_.Get(), gray(today ? 0.95f : 0.7f, grow), DWRITE_TEXT_ALIGNMENT_CENTER);
            } else {
                brush(gray(1, 0.08f));
                target_->FillRoundedRectangle(D2D1::RoundedRect(rect, 1.5f, 1.5f), solid_.Get());
            }
            textLine(p.week_days[i], cx - slot * 0.5f, bottom + 22, slot, today ? profile_title_font_.Get() : profile_body_font_.Get(),
                     today ? uiAccent(1) : gray(0.62f), DWRITE_TEXT_ALIGNMENT_CENTER);
        }
    }
    // The most played this week.
    {
        const auto card = shifted(layout.topCard(), dy);
        drawProfileCard(card, 1, 0, 0);
        capsLine(tr(L"Najczęściej grane"), card.left + 22, card.top + 28, card.right - card.left - 44, gray(0.6f));
        if (p.top.empty())
            paragraph(tr(L"Zagraj w coś w tym tygodniu, a pojawi się tutaj."), card.left + 24, card.top + 70, card.right - card.left - 48,
                      profile_body_font_.Get(), palette::muted(), 3);
        for (size_t i = 0; i < p.top.size(); ++i) {
            const auto& game = p.top[i];
            const auto row = shifted(layout.topRow(static_cast<int>(i)), dy);
            const auto [focus, hover] = focusOf(Section::top, static_cast<int>(i));
            if (focus > 0 || hover > 0) {
                brush(gray(1, 0.06f + 0.03f * hover));
                target_->FillRoundedRectangle(D2D1::RoundedRect(row, 12, 12), solid_.Get());
                if (focus > 0) { brush(uiAccent(0.9f)); target_->DrawRoundedRectangle(D2D1::RoundedRect(row, 12, 12), solid_.Get(), 2); }
            }
            const float size = row.bottom - row.top - 12;
            const auto cover = D2D1::RectF(row.left + 8, row.top + 6, row.left + 8 + size, row.bottom - 6);
            drawProfileImage(game.image, cover, 10, 1, game.name.substr(0, 1));
            const float x = cover.right + 14, inner = row.right - x - 12, cy = (row.top + row.bottom) * 0.5f;
            textLine(game.name, x, cy - 9, inner - 110, profile_title_font_.Get(), palette::text());
            textLine(game.time, row.right - 120, cy - 9, 108, profile_body_font_.Get(), gray(0.8f), DWRITE_TEXT_ALIGNMENT_TRAILING);
            const auto track = D2D1::RectF(x, cy + 12, row.right - 12, cy + 16);
            brush(gray(1, 0.08f)); target_->FillRoundedRectangle(D2D1::RoundedRect(track, 2, 2), solid_.Get());
            brush(uiAccent(i == 0 ? 0.95f : 0.6f));
            target_->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(x, track.top, x + (track.right - x) * game.share * grow, track.bottom), 2, 2), solid_.Get());
        }
    }

    // ---- The latest achievements: a row that slides.
    { // "1–3 / 12": the cards in view of the row's.
        const size_t first = static_cast<size_t>(std::max(0, p.recent_first)), last = std::min(p.recent.size(), first + static_cast<size_t>(layout.unlockColumns()));
        label(tr(L"Ostatnie osiągnięcia"), layout.recentTop() + dy, p.recent.size() <= static_cast<size_t>(layout.unlockColumns()) ? std::wstring{} :
              std::to_wstring(first + 1) + L"\u2013" + std::to_wstring(last) + L" / " + std::to_wstring(p.recent.size()));
    }
    if (p.recent.empty()) {
        const auto r = D2D1::RectF(left, layout.recentTop() + dy, right, layout.recentTop() + dy + layout.recentHeight(0));
        drawProfileCard(r, 1, 0, 0);
        textLine(tr(L"Tu pojawią się zdobyte osiągnięcia: Steam od razu, GOG, Epic i RetroAchievements z wtyczką osiągnięć."),
                 left + 24, (r.top + r.bottom) * 0.5f, right - left - 48, profile_body_font_.Get(), palette::muted());
    } else {
        target_->PushAxisAlignedClip(D2D1::RectF(left - 8, layout.recentTop() + dy - 10, right + 8, layout.recentTop() + dy + layout.unlock_h + 10),
                                     D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        for (size_t i = 0; i < p.recent.size(); ++i) {
            const auto r = shifted(layout.unlock(static_cast<int>(i), p.recent_offset), dy);
            if (r.right < left - 8 || r.left > right + 8) continue;
            const auto& unlock = p.recent[i];
            const auto [focus, hover] = focusOf(Section::recent, static_cast<int>(i));
            drawProfileCard(r, 1, focus, hover);
            const float size = r.bottom - r.top - 32;
            const auto picture = D2D1::RectF(r.left + 16, r.top + 16, r.left + 16 + size, r.bottom - 16);
            drawProfileImage(unlock.image, picture, unlock.icon ? 10.0f : 12.0f, 1, unlock.game.substr(0, 1));
            if (!unlock.icon) { // A cover stands in: a small trophy badge marks it as an achievement.
                const D2D1_POINT_2F badge{picture.right - 4, picture.bottom - 4};
                brush(gray(0.06f)); target_->FillEllipse(D2D1::Ellipse(badge, 13, 13), solid_.Get());
                brush(unlock.rare ? gold_ink : uiAccent(1)); target_->FillEllipse(D2D1::Ellipse(badge, 10, 10), solid_.Get());
                brush(gray(0.06f));
                target_->FillRectangle(D2D1::RectF(badge.x - 4.5f, badge.y - 5, badge.x + 4.5f, badge.y + 0.5f), solid_.Get());
                target_->FillRectangle(D2D1::RectF(badge.x - 1, badge.y, badge.x + 1, badge.y + 3.5f), solid_.Get());
                target_->FillRectangle(D2D1::RectF(badge.x - 3.5f, badge.y + 3.5f, badge.x + 3.5f, badge.y + 5), solid_.Get());
            }
            const float x = picture.right + 16, inner = r.right - x - 16;
            textLine(unlock.name, x, r.top + 32, inner, profile_title_font_.Get(), palette::text());
            textLine(unlock.game, x, r.top + 56, inner, profile_small_font_.Get(), gray(0.72f));
            float rx = x;
            if (unlock.rare) { // "Rzadkie": a gold pill.
                const auto text = std::wstring(tr(L"Rzadkie"));
                const float pill_w = textAdvance(text, profile_small_font_.Get()) + 18;
                const auto tag = D2D1::RectF(x, r.top + 74, x + pill_w, r.top + 96);
                brush(rgba(gold_ink.r, gold_ink.g, gold_ink.b, 0.16f)); target_->FillRoundedRectangle(pill(tag), solid_.Get());
                textLine(text, tag.left, (tag.top + tag.bottom) * 0.5f, pill_w, profile_small_font_.Get(), gold_ink, DWRITE_TEXT_ALIGNMENT_CENTER);
                rx = tag.right + 10;
            }
            textLine(unlock.when + (unlock.rarity.empty() ? std::wstring{} : L"  \u00b7  " + unlock.rarity), rx, r.top + 85, r.right - rx - 16,
                     profile_small_font_.Get(), palette::muted());
        }
        target_->PopAxisAlignedClip();
        // More cards to either side: a chevron in the margin says the row goes on.
        const int shown = layout.unlockColumns();
        const float middle = layout.recentTop() + dy + layout.unlock_h * 0.5f;
        const auto chevron = [&](float x, float direction) {
            brush(gray(1, 0.55f));
            target_->DrawLine(D2D1::Point2F(x - 4 * direction, middle - 9), D2D1::Point2F(x + 4 * direction, middle), solid_.Get(), 2.4f);
            target_->DrawLine(D2D1::Point2F(x + 4 * direction, middle), D2D1::Point2F(x - 4 * direction, middle + 9), solid_.Get(), 2.4f);
        };
        if (p.recent_offset > 0.05f) chevron(left - 22, -1);
        if (p.recent_offset + shown < static_cast<float>(p.recent.size()) - 0.05f) chevron(right + 22, 1);
    }

    // ---- The year of achievements.
    {
        const auto top = layout.historyTop(p.recent.size()) + dy;
        label(tr(L"Historia osiągnięć"), top, tr(L"Ostatnie 12 miesięcy"));
        const auto card = shifted(layout.historyCard(p.recent.size()), dy);
        drawProfileCard(card, 1, 0, 0);
        const float side = std::min(300.0f, (card.right - card.left) * 0.3f), split = card.right - side;
        const float chart_left = card.left + 24, chart_right = split - 24, base = card.bottom - 44, peak = card.top + 40;
        const float slot = (chart_right - chart_left) / 12;
        line(chart_left, base, chart_right, base, gray(1, 0.08f), 1);
        for (size_t i = 0; i < 12; ++i) {
            const float cx = chart_left + slot * (static_cast<float>(i) + 0.5f), bar = std::min(34.0f, slot * 0.56f);
            const float share = p.months[i] * grow, height = std::max(share > 0 ? 5.0f : 2.0f, (base - peak - 20) * share);
            const auto rect = D2D1::RectF(cx - bar * 0.5f, base - height, cx + bar * 0.5f, base);
            const bool current = i == 11;
            brush(p.months[i] > 0 ? uiAccent(current ? 0.95f : 0.35f + 0.45f * p.months[i]) : gray(1, 0.08f));
            target_->FillRoundedRectangle(D2D1::RoundedRect(rect, p.months[i] > 0 ? 5.0f : 1.0f, p.months[i] > 0 ? 5.0f : 1.0f), solid_.Get());
            if (!p.month_counts[i].empty())
                textLine(p.month_counts[i], cx - slot * 0.5f, rect.top - 13, slot, profile_small_font_.Get(), gray(current ? 0.95f : 0.7f, grow), DWRITE_TEXT_ALIGNMENT_CENTER);
            textLine(p.month_labels[i], cx - slot * 0.5f, base + 20, slot, profile_small_font_.Get(), current ? uiAccent(1) : gray(0.55f), DWRITE_TEXT_ALIGNMENT_CENTER);
        }
        line(split, card.top + 22, split, card.bottom - 22, gray(1, 0.07f), 1);
        const float row = (card.bottom - card.top - 32) / 3;
        for (size_t i = 0; i < p.history.size(); ++i) {
            const auto& stat = p.history[i];
            const float y = card.top + 16 + row * static_cast<float>(i);
            capsLine(stat.label, split + 26, y + 18, side - 44, gray(0.58f));
            const float value_w = textAdvance(stat.value, profile_value_font_.Get());
            textLine(stat.value, split + 26, y + 46, side - 44, profile_value_font_.Get(), stat.good ? gold_ink : palette::text());
            textLine(stat.note, split + 34 + value_w, y + 48, side - 52 - value_w, profile_small_font_.Get(), palette::muted());
        }
    }

    // ---- Closest to 100 %.
    {
        const auto top = layout.completionTop(p.recent.size()) + dy;
        label(tr(L"Najbardziej ukończone"), top);
        if (p.completion.empty()) {
            const auto r = D2D1::RectF(left, top, right, top + layout.completionHeight(0));
            drawProfileCard(r, 1, 0, 0);
            textLine(tr(L"Gry z osiągnięciami pojawią się tutaj, ułożone od najbliższych 100%."), left + 24, (r.top + r.bottom) * 0.5f,
                     right - left - 48, profile_body_font_.Get(), palette::muted());
        }
        for (size_t i = 0; i < p.completion.size(); ++i) {
            const auto& game = p.completion[i];
            const auto r = shifted(layout.completion(p.recent.size(), static_cast<int>(i)), dy);
            if (r.bottom < bar_bottom - 6 || r.top > h) continue;
            const auto [focus, hover] = focusOf(Section::completion, static_cast<int>(i));
            drawProfileCard(r, 1, focus, hover);
            const bool perfect = game.share >= 0.999f;
            const float size = r.bottom - r.top - 20;
            const auto cover = D2D1::RectF(r.left + 10, r.top + 10, r.left + 10 + size, r.bottom - 10);
            drawProfileImage(game.image, cover, 10, 1, game.name.substr(0, 1));
            const float x = cover.right + 16, cy = (r.top + r.bottom) * 0.5f;
            textLine(std::to_wstring(i + 1) + L".", x, cy - 10, 30, profile_small_font_.Get(), gray(0.5f));
            textLine(game.name, x + 26, cy - 10, r.right - x - 26 - 170, profile_title_font_.Get(), palette::text());
            textLine(game.count, r.right - 186, cy - 10, 170, profile_body_font_.Get(), perfect ? gold_ink : gray(0.78f), DWRITE_TEXT_ALIGNMENT_TRAILING);
            const auto track = D2D1::RectF(x, cy + 13, r.right - 16, cy + 18);
            brush(gray(1, 0.08f)); target_->FillRoundedRectangle(D2D1::RoundedRect(track, 2.5f, 2.5f), solid_.Get());
            brush(perfect ? gold_ink : uiAccent(0.9f));
            target_->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(x, track.top, x + (track.right - x) * game.share * grow, track.bottom), 2.5f, 2.5f), solid_.Get());
        }
    }

    // ---- Platforms.
    {
        const auto top = layout.platformsTop(p.recent.size(), p.completion.size()) + dy;
        label(tr(L"Platformy"), top);
        for (size_t i = 0; i < p.platforms.size(); ++i) {
            const auto& item = p.platforms[i];
            const auto r = shifted(layout.platform(p.recent.size(), p.completion.size(), static_cast<int>(i)), dy);
            if (r.bottom < bar_bottom - 6 || r.top > h) continue;
            const auto [focus, hover] = focusOf(Section::platforms, static_cast<int>(i));
            drawProfileCard(r, 1, focus, hover);
            const D2D1_POINT_2F mark{r.left + 46, r.top + 46};
            brush(gray(1, 0.07f)); target_->FillEllipse(D2D1::Ellipse(mark, 28, 28), solid_.Get());
            if (item.brand.empty() || !desktopBrand(item.brand, mark.x, mark.y, 28, gray(0.95f)))
                textLine(item.name.substr(0, 1), mark.x - 28, mark.y, 56, profile_title_font_.Get(), gray(0.95f), DWRITE_TEXT_ALIGNMENT_CENTER);
            const float x = r.left + 90, inner = r.right - x - 16;
            textLine(item.name, x, r.top + 34, inner, profile_title_font_.Get(), palette::text());
            if (item.connected) { brush(good_ink); target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(x + 4, r.top + 59), 3.5f, 3.5f), solid_.Get()); }
            textLine(item.account, x + (item.connected ? 14 : 0), r.top + 59, inner - (item.connected ? 14 : 0), profile_small_font_.Get(),
                     item.connected ? gray(0.85f) : palette::muted());
            line(r.left + 16, r.top + 86, r.right - 16, r.top + 86, gray(1, 0.06f), 1);
            textLine(item.games, r.left + 20, r.top + 108, (r.right - r.left) * 0.4f, profile_body_font_.Get(), gray(0.88f));
            textLine(item.hours, r.left + (r.right - r.left) * 0.4f, r.top + 108, (r.right - r.left) * 0.6f - 20, profile_body_font_.Get(),
                     gray(0.7f), DWRITE_TEXT_ALIGNMENT_TRAILING);
        }
    }
    target_->PopAxisAlignedClip();

    // ---- The bar: Back and the title, over the page as it scrolls under them.
    {
        ComPtr<ID2D1GradientStopCollection> stops;
        ComPtr<ID2D1LinearGradientBrush> shade;
        const float under = std::clamp(p.scroll / 40.0f, 0.0f, 1.0f);
        const D2D1_GRADIENT_STOP colors[] = {{0, rgba(0.016f, 0.019f, 0.025f, 0.96f * under)}, {0.7f, rgba(0.016f, 0.019f, 0.025f, 0.8f * under)},
                                             {1, rgba(0.016f, 0.019f, 0.025f, 0)}};
        if (under > 0 && SUCCEEDED(target_->CreateGradientStopCollection(colors, 3, stops.GetAddressOf())) &&
            SUCCEEDED(target_->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(D2D1::Point2F(0, 0), D2D1::Point2F(0, bar_bottom + 30)),
                                                         stops.Get(), shade.GetAddressOf())))
            target_->FillRectangle(D2D1::RectF(0, 0, w, bar_bottom + 30), shade.Get());
        const auto back = box(layout.close());
        const auto [focus, hover] = focusOf(Section::close, 0);
        drawButtonGlass(back, 1, false, hover > 0);
        drawButtonFocus(back, focus);
        const auto ink = buttonInk(focus);
        const float cx = (back.left + back.right) * 0.5f, cy = (back.top + back.bottom) * 0.5f;
        line(cx - 7, cy, cx + 7, cy, ink, 2);
        line(cx - 7, cy, cx - 1, cy - 6, ink, 2);
        line(cx - 7, cy, cx - 1, cy + 6, ink, 2);
        textLine(tr(L"Profil"), back.right + 18, cy, 400, profile_title_font_.Get(), palette::text());
        // How far down the page is: a thin track on the right.
        const float maximum = layout.maxScroll(p);
        if (maximum > 1) {
            const float track = h - bar_bottom - 24, thumb = std::max(40.0f, track * h / (h + maximum));
            const float y = bar_bottom + (track - thumb) * std::clamp(p.scroll / maximum, 0.0f, 1.0f);
            brush(gray(1, 0.18f));
            target_->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(w - 10, y, w - 6, y + thumb), 2, 2), solid_.Get());
        }
    }
    if (faded) target_->PopLayer();
}
