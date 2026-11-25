// Guide over a game, Przełącz grę: the running and suspended games in a list rising from its button, each
// with its cover. One picked is brought to the front (a suspended one resumes; with quick resume on, the
// game left behind is suspended, as Graj on a running game does); the game in front just returns to it.
#include "launcher.hpp"
#include "../library/search.hpp"

#include <algorithm>

std::vector<Game> Launcher::switcherGames() const {
    const auto current = overlay_target_ ? overlay_target_->key : std::string{};
    const auto find = [&](const std::string& key) {
        auto game = trackedGame(key);
        if (gameKey(game) == key && !game.name.empty()) return game;
        for (const auto* games : {&public_library_, &private_category_.games}) {
            const auto found = std::find_if(games->begin(), games->end(), [&](const Game& value) { return gameKey(value) == key; });
            if (found != games->end()) return *found;
        }
        return game;
    };
    std::vector<Game> games, running, frozen;
    for (const auto& [key, session] : game_sessions_) {
        if (!session.running() && !session.frozen()) continue; // Still starting: nothing to switch to yet.
        auto game = find(key);
        if (gameKey(game) != key) continue;
        if (key == current) games.insert(games.begin(), std::move(game));
        else (session.frozen() ? frozen : running).push_back(std::move(game));
    }
    // The game in front first (nearest the bar), then the ones running behind it, then the suspended ones.
    const auto by_name = [](const Game& a, const Game& b) { return compareTitles(a.name, b.name) < 0; };
    std::sort(running.begin(), running.end(), by_name);
    std::sort(frozen.begin(), frozen.end(), by_name);
    games.insert(games.end(), running.begin(), running.end());
    games.insert(games.end(), frozen.begin(), frozen.end());
    if (games.size() > static_cast<size_t>(quick_menu::switcher::max_rows)) games.resize(static_cast<size_t>(quick_menu::switcher::max_rows));
    return games;
}

void Launcher::openGameSwitcher() {
    switcher_games_ = switcherGames();
    switcher_release_ = false; // The A that opened it is not a pick when it comes up.
    if (switcher_games_.empty()) { navigationBoundary(); return; }
    const bool front = overlay_target_ && gameKey(switcher_games_.front()) == overlay_target_->key;
    // The first other game is chosen: one press of A switches between two games.
    game_switcher_ = front && switcher_games_.size() > 1 ? 1 : 0;
    // Covers let go while a game runs (or never loaded): asked for again; onCover repaints the list.
    for (const auto& game : switcher_games_) {
        const auto* art = art_.peek(gameKey(game));
        if (!art || (!art->cover && !art->poster && !art->landscape)) requestArt(game, CoverMessage::Kind::cover);
    }
    audio_.context();
    invalidateGameOverlay();
}

void Launcher::closeGameSwitcher() {
    game_switcher_.reset();
    switcher_release_ = false;
    switcher_games_.clear();
}

void Launcher::gameSwitcherKey(WPARAM key) {
    if (!game_switcher_) return;
    const int count = static_cast<int>(switcher_games_.size());
    if (key == VK_ESCAPE || key == VK_BACK || key == VK_LEFT || key == VK_RIGHT) { closeGameSwitcher(); audio_.context(); return; }
    if (key == VK_RETURN || key == VK_SPACE) { gameSwitcherAccept(); return; }
    // The list rises from the bar: Up goes away from it, Down back towards it and, below the first row, out.
    if (key == VK_UP || key == VK_DOWN) {
        const int next = *game_switcher_ + (key == VK_UP ? 1 : -1);
        if (next < 0) { closeGameSwitcher(); audio_.context(); return; }
        if (next >= count) { navigationBoundary(); return; }
        game_switcher_ = next; audio_.navigate();
    }
}

void Launcher::gameSwitcherAccept() {
    if (!game_switcher_ || *game_switcher_ < 0 || *game_switcher_ >= static_cast<int>(switcher_games_.size())) return;
    const auto game = switcher_games_[static_cast<size_t>(*game_switcher_)];
    closeGameSwitcher();
    if (overlay_target_ && gameKey(game) == overlay_target_->key) { dismissGameOverlay(); return; } // Back to it.
    // The picked game is brought in front while the overlay still holds the foreground (Windows refuses
    // it to a process in the background); only then does the overlay go, without handing the foreground
    // back to the game it covered.
    const bool switched = switchToGame(game);
    closeGameOverlay(false);
    if (!switched) showNotification(NotificationKind::error, tr(L"Nie można przełączyć gry"), game.name);
}

void Launcher::gameSwitcherContext(ViewContext& context) {
    if (!game_switcher_) return;
    for (const auto& game : switcher_games_) {
        const auto key = gameKey(game);
        const auto session = game_sessions_.find(key);
        ViewContext::SwitcherRow row{game.name, {}, {}, overlay_target_ && key == overlay_target_->key};
        row.status = row.current ? tr(L"Teraz grasz") : session != game_sessions_.end() && session->second.frozen() ? tr(L"Wstrzymana") : tr(L"Działa w tle");
        if (const auto* art = art_.find(key)) row.cover = art->cover ? art->cover : art->poster ? art->poster : art->landscape;
        context.game_switcher.push_back(std::move(row));
    }
    context.game_switcher_selected = *game_switcher_;
}
