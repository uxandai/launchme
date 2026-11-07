#include "launcher.hpp"
#include <algorithm>
#include <map>

void Launcher::updateSteamInstallations(const LibraryMessage& library) {
    if (library.account != account_) return;
    std::map<uint32_t, const Game*> current;
    std::map<uint32_t, Game> previous;
    for (const auto* list : {&library.games, &library.private_games}) for (const auto& game : *list) current.emplace(game.app_id, &game);
    for (const auto* list : {&installed_library_, &private_category_.games}) for (const auto& game : *list)
        if (game.source == GameSource::steam && game.kind == GameKind::steam && game.visibility != GameVisibility::hidden)
            previous.emplace(game.app_id, game); // Compare Steam's source snapshot, not our derived hidden copies.
    bool structure = current.size() != previous.size() || library.excluded != excluded_ids_;
    bool changed = structure;
    for (const auto& [id, game] : current) {
        const auto found = previous.find(id);
        if (found == previous.end()) { structure = changed = true; continue; }
        const auto& old = found->second;
        if (old.installation.active() && old.installation.phase != InstallPhase::unknown && !game->installation.active() &&
            (!old.installation.update || game->last_updated > old.last_updated) && game->installed && game->visibility == GameVisibility::public_game)
            downloadFinished(game->name, game);
        else if (!old.installation.active() && game->installation.active() && game->installation.phase != InstallPhase::unknown && game->visibility == GameVisibility::public_game)
            downloadQueued(game->name);
        // A finished update is no new game: Recent keeps it where its play history puts it.
        if (old.installation.active() && !old.installation.update && game->installed && game->visibility == GameVisibility::public_game)
            completed_installs_.insert(id);
        if (game->installed != old.installed || game->installation.active() != old.installation.active() ||
            game->visibility != old.visibility || game->name != old.name || game->last_played != old.last_played || game->last_updated != old.last_updated ||
            game->added_at != old.added_at)
            structure = changed = true;
        if (game->installation != old.installation) changed = true;
    }
    if (!changed) return;
    const auto selected = cards_.empty() ? std::string{} : gameKey(cards_[selected_].game);
    const auto category_id = categories_.empty() ? std::string{} : categories_[category_].id;
    const auto update = [&](Game& game) {
        if (game.source != GameSource::steam || (game.kind != GameKind::steam && game.kind != GameKind::catalog)) return;
        if (const auto found = current.find(game.app_id); found != current.end()) {
            const bool catalog = game.kind == GameKind::catalog;
            const auto available = game.catalog_available;
            const auto group = game.version_group;
            const auto added = game.added_at;
            game = *found->second;
            game.version_group = group;
            if (!structure && !group.empty()) game.added_at = added; // Progress alone must not discard the group's other copies.
            // Edytuj metadane: the player's title survives a progress or ownership update.
            if (const auto* value = metadataOverride(game); value && !value->title.empty()) game.name = value->title;
            if (game.visibility == GameVisibility::public_game && locallyHidden(game)) game.visibility = GameVisibility::hidden;
            if (catalog) { game.kind = GameKind::catalog; game.catalog_available = available; }
        } else { game.installed = false; game.installation = {}; }
    };
    for (auto* list : {&public_library_, &grouped_library_, &pc_library_, &search_results_, &detail_games_, &store_results_, &uninstalled_library_})
        for (auto& game : *list) update(game);
    for (auto* list : {&categories_, &library_categories_}) for (auto& category : *list) for (auto& game : category.games) update(game);
    for (auto& card : cards_) update(card.game);
    if (pending_launch_) update(pending_game_);
    installed_library_ = library.games;
    if (structure) {
        std::erase_if(private_category_.games, [](const Game& game) { return game.source == GameSource::steam && game.kind == GameKind::steam; });
        private_category_.games.insert(private_category_.games.end(), library.private_games.begin(), library.private_games.end());
    } else for (auto& game : private_category_.games) update(game);
    excluded_ids_ = library.excluded;
    if (structure) {
        if (!private_shelf_) {
            closeEditorsFor([&](const Game& game) { return game.source == GameSource::steam && excluded_ids_.contains(game.app_id); });
            if (pending_launch_ && pending_game_.source == GameSource::steam && excluded_ids_.contains(pending_game_.app_id)) { pending_launch_ = false; launch_progress_ = 0; }
        }
        for (const auto& shortcut : shortcut_library_) if (excluded_ids_.contains(shortcut.app_id)) {
            auto hidden = shortcut; hidden.visibility = GameVisibility::private_game; private_category_.games.push_back(std::move(hidden));
        }
        std::erase_if(shortcut_library_, [&](const Game& game) { return excluded_ids_.contains(game.app_id); });
        // Keep collection names and offline membership, replacing only installed
        // facts. Progress-only snapshots never recreate bitmaps or move focus.
        for (const auto& [id, old] : previous) {
            if (current.contains(id) || old.visibility != GameVisibility::public_game) continue;
            bool member = false;
            for (const auto& collection : settings_collections_)
                if (collectionContains(library_state_.collections, account_, collection, old)) member = true;
            if (member && !excluded_ids_.contains(id)) {
                Game game = old; game.installed = false; game.installation = {}; uninstalled_library_.push_back(std::move(game));
            }
        }
        std::erase_if(uninstalled_library_, [&](const Game& game) { return current.contains(game.app_id) || excluded_ids_.contains(game.app_id); });
        rebuildUninstalledSteamGames();
        buildShelves();
        const auto category = std::find_if(categories_.begin(), categories_.end(), [&](const Category& item) { return item.id == category_id; });
        if (category != categories_.end()) category_ = static_cast<size_t>(category - categories_.begin());
        if (screen_ == ScreenMode::search) updateSearch();
        else if (screen_ == ScreenMode::library || screen_ == ScreenMode::shelves) rebuildCards();
        const auto found = std::find_if(cards_.begin(), cards_.end(), [&](const GameCard& card) { return gameKey(card.game) == selected; });
        if (found != cards_.end()) focusCard(static_cast<size_t>(found - cards_.begin()), false);
        if (screen_ == ScreenMode::details && !detail_games_.empty() && excluded_ids_.contains(detail_games_[0].app_id) && !private_shelf_) {
            suspendDetails(); showCategory(0);
        }
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}
