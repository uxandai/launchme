// Szczegóły → Zrzuty ekranu i podobne gry (Interfejs, on by default): the game's page goes on below
// its cards, in Tryb TV and in the desktop window: the Steam store's screenshots of the game and
// similar games from the library, as PSfive's sheet under the row shows them. Both are loaded and
// kept by src/app/psfive_details.cpp (psfive_shot_full_urls_, psfive_similar_games_ and the
// renderer's psfive slots); a screenshot opens full screen in the same viewer.
// Tryb TV: Down from the buttons or the cards scrolls the page up to the strips
// (details_layout::Layout::scroll); Back there returns to the buttons. Desktop: two more sections of
// the scrolling sheet (src/ui/details_media_renderer.cpp).
#include "launcher.hpp"
#include "../ui/game_details_layout.hpp"

#include <algorithm>
#include <cmath>

bool Launcher::detailsMedia() const {
    return settings_.details_media && screen_ == ScreenMode::details && !cards_.empty() && isGame(cards_[0].game);
}

bool Launcher::detailsMediaReady() const { return detailsMedia() && gameKey(cards_[0].game) == psfive_details_key_; }

details_layout::Layout Launcher::detailsLayout() const {
    const auto view = renderer_.viewport();
    return {view.width, view.height, details_scroll_};
}

void Launcher::detailsMediaActions(details_layout::Actions& actions) const {
    if (!detailsMediaReady()) return;
    actions.shots = psfive_shot_full_urls_.size();
    actions.similar = psfive_similar_games_.size();
    actions.shots_loading = psfive_shots_loading_;
    actions.shots_scroll = psfive_shots_scroll_;
    actions.similar_scroll = psfive_similar_scroll_;
}

bool Launcher::detailsMediaFocused() const { return details_layout::shotAction(detail_action_) || details_layout::similarAction(detail_action_); }

void Launcher::detailsMediaOpened() {
    details_scroll_ = 0;
    psfive_shot_view_ = false;
    if (detailsMedia()) psfiveLoadDetails(cards_[0].game);
}

// Enter (or a click) on a strip's item; Enter in the viewer closes it. True when it was one of them.
bool Launcher::detailsMediaAccept() {
    if (screen_ != ScreenMode::details) return false;
    if (psfive_shot_view_) { detailsCloseShot(); audio_.context(); return true; }
    if (!detailsMediaReady()) return false;
    if (details_layout::shotAction(detail_action_)) {
        const auto index = static_cast<size_t>(detail_action_ - details_layout::shot_action);
        if (index >= psfive_shot_full_urls_.size()) return true;
        psfive_shot_ = index;
        psfiveViewShot();
        return true;
    }
    if (details_layout::similarAction(detail_action_)) {
        const auto index = static_cast<size_t>(detail_action_ - details_layout::similar_action);
        if (index < psfive_similar_games_.size()) { const auto game = psfive_similar_games_[index]; openDetails(game); }
        return true;
    }
    return false;
}

void Launcher::detailsCloseShot() {
    if (!psfive_shot_view_) return;
    psfive_shot_view_ = false;
    // The focus on the picture last shown (the viewer walks the strip with Left and Right).
    if (!desktop_mode_ && psfive_shot_ < psfive_shot_full_urls_.size()) detail_action_ = details_layout::shot_action + static_cast<int>(psfive_shot_);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

// The viewer's Left and Right: the strip's previous or next picture.
void Launcher::detailsShotStep(int amount) {
    if (!psfive_shot_view_ || !amount) return;
    const int count = static_cast<int>(psfive_shot_full_urls_.size());
    const int next = std::clamp(static_cast<int>(psfive_shot_) + (amount < 0 ? -1 : 1), 0, std::max(0, count - 1));
    if (next == static_cast<int>(psfive_shot_)) { navigationBoundary(); return; }
    psfive_shot_ = static_cast<size_t>(next);
    if (!desktop_mode_) detail_action_ = details_layout::shot_action + next;
    psfiveViewShot();
    audio_.navigate();
}

// Back from a strip: up to the buttons (the page scrolls back) instead of leaving the game.
bool Launcher::detailsMediaBack() {
    if (screen_ != ScreenMode::details) return false;
    if (psfive_shot_view_) { detailsCloseShot(); audio_.navigate(); return true; }
    if (!detailsMediaFocused()) return false;
    const auto row = detailsLayout().buttons(detailsActions());
    detail_action_ = std::any_of(row.begin(), row.end(), [&](const details_layout::Item& item) { return item.action == detail_column_; }) ? detail_column_ : 0;
    audio_.navigate();
    InvalidateRect(hwnd_, nullptr, FALSE);
    return true;
}

// The wheel on a page with strips: between the buttons, the strips (as Up and Down), not along the buttons.
bool Launcher::detailsMediaWheel(int amount) {
    if (!detailsMediaReady() || psfive_shot_view_ || !amount) return psfive_shot_view_;
    details_layout::Actions actions;
    detailsMediaActions(actions);
    if (!actions.shots && !actions.similar) return false;
    detailsMove(0, amount < 0 ? -1 : 1);
    return true;
}

// The similar games' covers from the artwork cache, asked for once when missing (PSfive's sheet and Szczegóły).
void Launcher::bindSimilarCovers(bool& needs_paint) {
    for (size_t i = 0; i < psfive_similar_games_.size() && i < psfive_similar_bound_.size(); ++i) {
        const auto& game = psfive_similar_games_[i];
        const auto* art = art_.find(gameKey(game));
        if (psfive_similar_bound_[i] && renderer_.hasPsfiveSimilar(i)) {
            if (art && art->cover) renderer_.setPsfiveSimilar(i, art->cover, art->focus); // A crop chosen or found later.
            continue;
        }
        if (art && art->cover) { renderer_.setPsfiveSimilar(i, art->cover, art->focus); psfive_similar_bound_[i] = true; needs_paint = true; }
        else if (!psfive_similar_bound_[i]) { requestArt(game, CoverMessage::Kind::cover); psfive_similar_bound_[i] = true; }
    }
}

void Launcher::tickDetailsMedia(float dt, bool& needs_paint) {
    if (!detailsMedia()) {
        if (screen_ != ScreenMode::details) {
            details_scroll_ = 0;
            if (psfive_shot_view_ && !psfiveDetailsShown()) psfive_shot_view_ = false;
        }
        return;
    }
    if (gameKey(cards_[0].game) != psfive_details_key_) psfiveLoadDetails(cards_[0].game);
    const auto view = renderer_.viewport();
    const details_layout::Layout layout{view.width, view.height};
    const auto actions = detailsActions();
    // A focus gone with the strips (another game's, fewer pictures): the primary button.
    if (detailsMediaFocused() && !layout.find(layout.items(actions), detail_action_)) detail_action_ = 0;
    const auto ease = [&](float& value, float wanted, float rate) {
        if (std::abs(wanted - value) > 0.3f) { value += (wanted - value) * (1.0f - std::exp(-dt * rate)); needs_paint = true; }
        else value = wanted;
    };
    ease(details_scroll_, detailsMediaFocused() ? layout.mediaScroll(actions) : 0.0f, 12.0f);
    if (!desktop_mode_) {
        const float shot_step = details_layout::Layout::shot_width + details_layout::Layout::strip_gap;
        const float similar_step = details_layout::Layout::similar_size + details_layout::Layout::strip_gap;
        const auto shot = details_layout::shotAction(detail_action_) ? static_cast<size_t>(detail_action_ - details_layout::shot_action) : 0;
        const auto similar = details_layout::similarAction(detail_action_) ? static_cast<size_t>(detail_action_ - details_layout::similar_action) : 0;
        ease(psfive_shots_scroll_, layout.stripScroll(shot, actions.shots, shot_step), 14.0f);
        ease(psfive_similar_scroll_, layout.stripScroll(similar, actions.similar, similar_step), 14.0f);
    }
    bindSimilarCovers(needs_paint);
}
