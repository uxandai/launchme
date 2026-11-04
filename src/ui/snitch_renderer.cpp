#include "renderer.hpp"
#include "renderer_internal.hpp"
#include "snitch_layout.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

using namespace render_detail;

void Renderer::drawSnitch() {
    using palette::gray;
    const float width = viewport_.width, height = viewport_.height;
    brush(gray(0.175f));
    target_->FillRectangle(D2D1::RectF(0, 0, width, height), solid_.Get());
    // Tapeta in place of the plain grey, darkened enough for the white icons and the title.
    if (wallpaper_places_.snitch && drawWallpaperBackdrop(1.0f)) {
        brush(gray(0.05f, 0.42f));
        target_->FillRectangle(D2D1::RectF(0, 0, width, height), solid_.Get());
    }
    beginScreenForeground();

    // Profile shortcut: the same Widgets destination as the dock, available without downloaded art.
    const auto profile = snitch_layout::profile();
    const float cx = (profile.left + profile.right) * 0.5f, cy = (profile.top + profile.bottom) * 0.5f;
    // The Steam account's picture when the client has it; else the silhouette. Focused (Up from the row):
    // a lift, a halo in the accent and a pulsing white ring, like the real console's user icon.
    if (snitch_avatar_.pixels != context_->snitch_avatar) {
        snitch_avatar_ = {};
        snitch_avatar_.short_edge = 256;
        snitch_avatar_.pixels = context_->snitch_avatar;
    }
    if (snitch_avatar_.loaded() && !snitch_avatar_.bitmap) createBitmap(snitch_avatar_);
    // Eased like the dock's focus (quick_progress): the picture lifts and the dock's ring, with its
    // light travelling clockwise, fades in around it.
    const float focus = smoothFade(context_->snitch_profile_progress);
    const float lift = 1.0f + 0.14f * focus, r = 32 * lift;
    if (focus > 0.004f) {
        const float ring_radius = r + 4.0f;
        const auto ring = D2D1::Ellipse({cx, cy}, ring_radius, ring_radius);
        if (quick_ring_) {
            const float pi = std::numbers::pi_v<float>;
            const float angle = std::fmod(std::max(0.0f, context_->quick_ring_time) / 4.0f, 1.0f) * 2 * pi - pi * 0.5f;
            const float dx = std::cos(angle) * ring_radius, dy = std::sin(angle) * ring_radius;
            quick_ring_->SetStartPoint(D2D1::Point2F(cx + dx, cy + dy));
            quick_ring_->SetEndPoint(D2D1::Point2F(cx - dx, cy - dy));
            quick_ring_->SetOpacity(focus);
            target_->DrawEllipse(ring, quick_ring_.Get(), 2.5f);
        } else {
            brush(gray(0.93f, 0.85f * focus));
            target_->DrawEllipse(ring, solid_.Get(), 2.5f);
        }
    }
    brush(gray(0.30f)); target_->FillEllipse(D2D1::Ellipse({cx, cy}, r, r), solid_.Get());
    if (snitch_avatar_.bitmap) {
        fillImage(snitch_avatar_, D2D1::RectF(cx - r, cy - r, cx + r, cy + r), r, reveal(snitch_avatar_));
    } else {
        brush(gray(0.96f)); target_->DrawEllipse(D2D1::Ellipse({cx, cy - 7 * lift}, 8 * lift, 8 * lift), solid_.Get(), 2);
        target_->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(cx - 15 * lift, cy + 6 * lift, cx + 15 * lift, cy + 19 * lift), 6 * lift, 6 * lift), solid_.Get(), 2);
    }

    float right = width - 80;
    if (context_->snitch_battery >= 0) {
        const float x = right - 42;
        brush(gray(0.95f));
        target_->DrawRectangle(D2D1::RectF(x, 70, right - 4, 90), solid_.Get(), 2);
        target_->FillRectangle(D2D1::RectF(right - 3, 75, right, 85), solid_.Get());
        if (context_->snitch_battery > 0)
            target_->FillRectangle(D2D1::RectF(x + 4, 74, x + 4 + 30 * context_->snitch_battery / 100.0f, 86), solid_.Get());
        const auto percent = std::to_wstring(context_->snitch_battery) + L"%" + (context_->snitch_charging ? L" +" : L"");
        textLine(percent, x - 92, 80, 80, menu_font_.Get(), gray(0.95f), DWRITE_TEXT_ALIGNMENT_TRAILING);
        right = x - 112;
    }
    textLine(context_->snitch_clock, right - 190, 80, 190, psfive_clock_font_.Get(), gray(0.97f), DWRITE_TEXT_ALIGNMENT_TRAILING);
    drawQuickPanel(); // Part of the home scene: game menus and dialogs are painted above the dock.

    if (scanning_ || cards_->empty()) { drawEmptyState(); return; }
    const auto& focused = (*cards_)[selected_];
    auto tint = selected_ < covers_.size() && covers_[selected_].loaded() ? covers_[selected_].accent : gray(0.96f);
    if (cover_accent_) tint = color(((cover_accent_ >> 16) & 255) / 255.0f, ((cover_accent_ >> 8) & 255) / 255.0f, (cover_accent_ & 255) / 255.0f);
    tint.a = smoothFade(detail_ * 2.5f) * focused.opacity;
    // A dark cover accent would sink into the grey: the title keeps a readable brightness.
    const float luma = 0.2126f * tint.r + 0.7152f * tint.g + 0.0722f * tint.b;
    if (luma < 0.55f) {
        const float t = std::clamp((0.55f - luma) / 0.55f, 0.0f, 1.0f) * 0.85f;
        tint.r = std::lerp(tint.r, 1.0f, t); tint.g = std::lerp(tint.g, 1.0f, t); tint.b = std::lerp(tint.b, 1.0f, t);
    }
    const float title_y = snitch_layout::rowTop(height) - 42;
    if (isGame(focused.game)) {
        // Like the reference's cartridge before the title: solid when installed, otherwise its outline.
        const float x = snitch_layout::left - 46;
        const auto cartridge = D2D1::RoundedRect(D2D1::RectF(x, title_y - 16, x + 26, title_y + 16), 1.5f, 1.5f);
        brush(tint);
        if (focused.game.installed) {
            target_->FillRoundedRectangle(cartridge, solid_.Get());
            const auto cutout = gray(0.175f, tint.a);
            line(x + 8, title_y + 6, x + 13, title_y + 10, cutout, 2);
            line(x + 13, title_y + 10, x + 18, title_y + 6, cutout, 2);
        } else target_->DrawRoundedRectangle(cartridge, solid_.Get(), 2);
    }
    textLine(focused.game.name, snitch_layout::left, title_y, width - snitch_layout::left - 80,
             psfive_name_font_.Get(), tint);
    if (isGame(focused.game)) {
        // Beside the title, in words: its state (the cartridge glyph alone was easy to miss) and platform.
        const auto& game = focused.game;
        const auto session = sessionOf(game);
        std::wstring status = session == SessionState::frozen ? std::wstring(tr(L"Wstrzymana")) :
            session == SessionState::running ? std::wstring(tr(L"W grze")) :
            game.installation.active() ? installationText(game.installation) :
            !game.installed && game.kind != GameKind::catalog ? std::wstring(localFileGame(game.source) ? missingFileLabel(game) : tr(L"Do instalacji")) : std::wstring();
        if (const auto platform = activePlatformName(game); !platform.empty() && !studioPlatform(platform))
            status += (status.empty() ? L"" : L"   ·   ") + platform;
        const float after = snitch_layout::left + textAdvance(game.name, psfive_name_font_.Get()) + 24;
        if (!status.empty() && after < width - 200)
            textLine(status, after, title_y, width - after - 80, menu_font_.Get(), gray(0.66f, tint.a));
    }
    const auto clip = D2D1::RectF(snitch_layout::clip_left, snitch_layout::rowTop(height) - 28, width, snitch_layout::dockY(height) - 90);
    target_->PushAxisAlignedClip(clip, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    for (size_t i = 0; i < cards_->size(); ++i) if (i != selected_) drawCard((*cards_)[i], i);
    target_->PopAxisAlignedClip();
    const float launch = smoothFade(context_->launch_progress);
    if (launch > 0.001f) {
        brush(color(0.008f, 0.012f, 0.016f, launch * 0.82f));
        target_->FillRectangle(D2D1::RectF(0, 0, width, height), solid_.Get());
    }
    target_->PushAxisAlignedClip(clip, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    drawCard(focused, selected_);
    target_->PopAxisAlignedClip();
}
