// Local achievements of DRM-free games (and "Moje gry"): unlocks kept in files on this computer
// (src/library/local_achievements.hpp). The achievements worker (achievements.cpp) finds them from
// the game folder, reads them and a schema (the game's own, the Steam client's, or the Web API's
// with the user's key), then the public shares of players. The answer feeds the Details card, the
// achievements screen, the desktop pane and the game index's counts, as an achievements plugin's
// does; a game with local achievements does not ask the plugin.
#include "launcher.hpp"

namespace {
constexpr uint64_t fresh_ms = 10 * 60000, lost_ms = 60000;
}

std::filesystem::path Launcher::localAchievementsFolder(const Game& game) const {
    if (!isGame(game) || !localFileGame(game.source)) return {};
    std::filesystem::path folder = game.source == GameSource::drmfree && !game.install_directory.empty() ? game.install_directory :
                                   game.executable.empty() ? std::filesystem::path{} : game.executable.parent_path();
    // A program straight on a drive ("Moje gry") is not a game folder worth walking.
    if (folder.empty() || !folder.is_absolute() || folder == folder.root_path()) return {};
    return folder;
}

const Launcher::LocalAchievements* Launcher::localAchievements(const Game& game) const {
    const auto found = local_achievements_.find(gameKey(game));
    return found == local_achievements_.end() || !found->second.answered || !found->second.found ? nullptr : &found->second;
}

void Launcher::requestLocalAchievements(const Game& game, bool force) {
    if (!runtime_started_) return;
    const auto folder = localAchievementsFolder(game);
    const auto key = gameKey(game);
    if (folder.empty() || key.empty()) return;
    auto& cache = local_achievements_[key];
    const auto now = GetTickCount64();
    if (!force && ((cache.pending && now - cache.checked_at < lost_ms) || (!cache.pending && cache.answered && now - cache.checked_at < fresh_ms))) return;
    cache.pending = true;
    cache.checked_at = now;
    {
        std::lock_guard lock(achievement_jobs_->mutex);
        AchievementsJob job;
        job.generation = achievement_jobs_->generation.load();
        job.local = true;
        job.game_folder = folder;
        job.key = key;
        job.folder = text_encoding::wideLenient(gameCacheKey(game));
        std::erase_if(achievement_jobs_->jobs, [&](const AchievementsJob& queued) { return queued.local && queued.key == key; });
        achievement_jobs_->jobs.push_back(std::move(job));
    }
    achievement_jobs_->wake.notify_one();
}

void Launcher::receiveLocalAchievements(std::unique_ptr<AchievementsMessage> message) {
    const auto& key = message->key;
    auto& cache = local_achievements_[key];
    cache.pending = !message->final;
    cache.answered = true;
    cache.checked_at = GetTickCount64();
    cache.found = message->found;
    cache.app_id = message->app_id;
    const bool shown = screen_ == ScreenMode::details && !detail_games_.empty() && gameKey(detail_games_[0]) == key;
    if (cache.found) {
        cache.list = std::move(message->list);
        if (cache.list.total) {
            game_index::Patch counts;
            counts.achievements_unlocked = cache.list.unlocked; counts.achievements_total = cache.list.total;
            gameIndexMerge(key, game_index::Source::achievements, std::move(counts));
            if (shown) recordAchievements(key, detail_games_[0].name, cache.list); // The profile's history learns its unlocks.
        }
    } else cache.list = {};
    if (shown && cache.found) {
        details_.achievements_unlocked = cache.list.unlocked; details_.achievements_total = cache.list.total;
        desktop_details_key_.clear(); // The desktop pane reads them again.
    }
    if (shown && achievementsOpen() && achievements_.key == key) {
        if (cache.found && cache.list.total) showAchievementList(cache.list, message->final);
        else if (message->final) { achievements_.loading = false; achievements_.message = tr(L"Ta gra nie ma osiągnięć."); }
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}
