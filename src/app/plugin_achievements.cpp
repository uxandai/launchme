// Achievements of games outside Steam from a plugin (kind "achievements", docs/plugins/API.md;
// the bundled one is plugins/achievements: GOG, Epic, RetroAchievements, RPCS3, shadPS4). Details
// asks the provider for a game's list; the plugin may first ask for store requests, which the
// plugin worker makes with the signed-in GOG or Epic account (the account worker adds the token)
// and sends back. The answer feeds the Details card, the achievements screen (achievements.cpp)
// and the game index's counts, so the previews and the desktop pane show them as for Steam.
#include "launcher.hpp"
#include "../plugins/achievements.hpp"

#include <algorithm>
#include <future>

using namespace plugins::wire;

json::Value pluginAchievementRounds(plugins::Manager& manager, const plugins::Job& job, launcher_queues::StoreGateway& gateway) {
    auto params = job.value;
    for (size_t round = 0;; ++round) {
        auto response = manager.invoke(job.plugin, "achievements", params, job.locale);
        auto answer = plugins::parseAchievements(response);
        if (answer.kind != plugins::AchievementsAnswer::Kind::requests) return response;
        if (round + 1 >= plugins::max_store_rounds) throw std::runtime_error("Too many store request rounds");
        std::vector<std::pair<std::string, std::optional<HttpResponse>>> answers;
        for (auto& request : answer.requests) {
            std::future<std::optional<HttpResponse>> pending;
            {
                std::lock_guard lock(gateway.mutex);
                if (request.account == "gog" && gateway.gog) pending = gateway.gog->request(request.request);
                else if (request.account == "epic" && gateway.epic) pending = gateway.epic->request(request.request);
            }
            std::optional<HttpResponse> reply;
            if (pending.valid() && pending.wait_for(std::chrono::seconds(25)) == std::future_status::ready) {
                try { reply = pending.get(); } catch (...) {} // The account service closed.
            }
            answers.emplace_back(request.id, std::move(reply));
        }
        params.properties["responses"] = plugins::storeResponses(answers);
        params.properties["state"] = answer.state;
    }
}

std::optional<plugins::Action> Launcher::pluginAchievementsProvider(const Game& game) const {
    // Steam games keep the client's own achievements (achievements.cpp).
    if (plugins_busy_ || !isGame(game) || steamArtwork(game) || game.kind == GameKind::catalog) return {};
    if (localAchievements(game)) return {}; // Local achievements (local_achievements.cpp).
    const auto providers = plugins_.contributions("achievements", &game);
    return providers.empty() ? std::nullopt : std::optional<plugins::Action>(providers.front());
}

const Launcher::PluginAchievements* Launcher::pluginAchievements(const Game& game) const {
    const auto found = plugin_achievements_.find(gameKey(game));
    return found == plugin_achievements_.end() || !found->second.answered ? nullptr : &found->second;
}

void Launcher::requestFocusedAchievements(const Game& game) {
    const auto key = gameKey(game);
    if (key.empty() || key == plugin_achievements_focus_) return;
    plugin_achievements_focus_ = key;
    requestLocalAchievements(game);
    requestPluginAchievements(game, false, true);
}

void Launcher::requestPluginAchievements(const Game& game, bool force, bool background) {
    if (!runtime_started_) return;
    const auto provider = pluginAchievementsProvider(game);
    if (!provider) return;
    const auto key = gameKey(game);
    const std::string locale = i18n::language() == i18n::Language::pl ? "pl" : "en";
    const auto now = GetTickCount64();
    const auto achievementsJob = [](const plugins::Job& j) { return j.kind == plugins::Job::Kind::achievements; };
    {
        std::lock_guard lock(plugin_jobs_->mutex);
        for (auto& j : plugin_jobs_->jobs) // Asked for in Szczegóły: the pane's queued question stays.
            if (!background && achievementsJob(j) && gameKey(j.game) == key) j.background = false;
        // The pane moved on: the previous selection's unanswered question leaves the queue.
        if (background) std::erase_if(plugin_jobs_->jobs, [&](const plugins::Job& j) {
            if (!achievementsJob(j) || !j.background || gameKey(j.game) == key) return false;
            if (const auto found = plugin_achievements_.find(gameKey(j.game)); found != plugin_achievements_.end() && found->second.generation == j.generation)
                found->second.pending = false;
            return true;
        });
    }
    auto& cache = plugin_achievements_[key];
    const bool same = cache.plugin == provider->plugin && cache.locale == locale && cache.name == game.name;
    // One question at a time per game; an answer is good for ten minutes (a lost one after a minute).
    if (!force && same && ((cache.pending && now - cache.checked_at < 60000) || (!cache.pending && cache.answered && now - cache.checked_at < 10 * 60000))) return;
    if (!same) { cache = {}; cache.plugin = provider->plugin; cache.locale = locale; cache.name = game.name; }
    cache.pending = true; cache.checked_at = now; cache.generation = ++plugin_achievements_generation_;
    plugins::Job job; job.kind = plugins::Job::Kind::achievements; job.plugin = provider->plugin; job.key = provider->item.id; job.game = game;
    job.generation = cache.generation; job.background = background;
    job.value = object();
    job.value.properties["game"] = *json::parse(plugins::achievementGameJson(game));
    job.value.properties["command"] = string(provider->item.command);
    job.value.properties["refresh"] = plugins::wire::boolean(force);
    // The accounts the host can ask with: the plugin never sees who they are or their tokens.
    auto accounts = object();
    accounts.properties["gog"] = plugins::wire::boolean(gog_service_ && !gog_state_.account.empty());
    accounts.properties["epic"] = plugins::wire::boolean(epic_service_ && !epic_state_.account.empty());
    job.value.properties["accounts"] = std::move(accounts);
    submitPlugin(std::move(job));
}

bool Launcher::receivePluginAchievements(const plugins::Result& message) {
    const auto& job = message.job;
    const auto key = gameKey(job.game);
    const auto found = plugin_achievements_.find(key);
    if (found == plugin_achievements_.end() || found->second.generation != job.generation) return false;
    auto& cache = found->second;
    cache.pending = false; cache.checked_at = GetTickCount64();
    if (!message.error.empty()) {
        if (!cache.answered) cache.message = tr(L"Osiągnięcia chwilowo niedostępne.");
    } else {
        plugins::AchievementsAnswer answer;
        try { answer = plugins::parseAchievements(message.response); } catch (...) { answer.kind = plugins::AchievementsAnswer::Kind::message; }
        using Kind = plugins::AchievementsAnswer::Kind;
        if (answer.kind == Kind::message && cache.answered) {} // A passing failure keeps the last list.
        else {
            cache.answered = true;
            cache.list = answer.kind == Kind::list ? std::move(answer.list) : SteamAchievements{};
            cache.source = answer.source;
            cache.message = answer.kind == Kind::message ? (answer.message.empty() ? std::wstring(tr(L"Osiągnięcia chwilowo niedostępne.")) : answer.message) : std::wstring{};
            if (answer.kind != Kind::message) { // The counts for the card, the previews and the desktop pane.
                game_index::Patch counts;
                counts.achievements_unlocked = cache.list.unlocked; counts.achievements_total = cache.list.total;
                gameIndexMerge(key, game_index::Source::achievements, std::move(counts));
                recordAchievements(key, job.game.name, cache.list); // The profile's history: its unlocks with their times.
            }
        }
    }
    const bool shown = screen_ == ScreenMode::details && !detail_games_.empty() && gameKey(detail_games_[0]) == key;
    if (shown && cache.answered) {
        details_.achievements_unlocked = cache.list.unlocked; details_.achievements_total = cache.list.total;
        desktop_details_key_.clear(); // The desktop pane reads them again.
    }
    if (shown && achievementsOpen() && achievements_.key == key) {
        if (cache.answered && cache.list.total) showAchievementList(cache.list, true);
        else { achievements_.loading = false; achievements_.message = cache.message.empty() ? std::wstring(tr(L"Ta gra nie ma osiągnięć.")) : cache.message; }
        achievements_.source = cache.source;
    }
    return true;
}
