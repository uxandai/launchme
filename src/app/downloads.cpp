// Pobierania (the quick bar's tab after the plugins' pages): every game downloading, updating,
// waiting or paused, from the sources that report progress: Steam (its manifests and live
// counters), GOG and Epic (Launchme's own installer slot) and Battle.net. The list follows their
// progress while it is open; a row opens the place that manages it (Steam's downloads, the
// GOG/Epic install dialog, the Battle.net client). src/ui/downloads_view.hpp, downloads_renderer.cpp.
#include "launcher.hpp"
#include "../platform/text_encoding.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {
Game pluginDownloadGame(const std::string& owner, const plugins::Download& d) {
    Game game; game.source = GameSource::custom; game.source_id = "plugin-download:" + owner + ':' + d.id;
    game.name = d.title; game.platform = d.platform; game.installed = false;
    return game;
}
// "1,2 / 3,4 GB" (a comma in Polish), or MB below a gigabyte.
std::wstring amountText(uint64_t done, uint64_t total) {
    const bool giga = total >= 1024ull * 1024 * 1024;
    const double unit = giga ? 1024.0 * 1024 * 1024 : 1024.0 * 1024;
    wchar_t text[64]{};
    swprintf_s(text, giga ? L"%.1f / %.1f GB" : L"%.0f / %.0f MB", done / unit, total / unit);
    std::wstring value = text;
    if (i18n::language() != i18n::Language::en) std::replace(value.begin(), value.end(), L'.', L',');
    return value;
}
downloads_ui::Row rowOf(const Game& game, const Installation& install, bool epic) {
    downloads_ui::Row row;
    row.store = sourceName(game.source);
    row.status = std::wstring(install.update ? std::wstring(tr(L"Aktualizacja")) + L" · " : L"") + installationLabel(install.phase);
    if (install.total && install.done <= install.total && (install.done || install.live)) {
        row.fraction = static_cast<float>(static_cast<double>(install.done) / install.total);
        // Epic reports hundredths of a percent, not bytes; below a megabyte the sizes say nothing.
        if (!epic && install.total >= 1024 * 1024) row.amount = amountText(install.done, install.total);
    }
    row.moving = install.moving();
    row.destination = game.install_directory.wstring();
    return row;
}
}

namespace {
constexpr uint64_t download_announce_quiet_ms = 60000; // Gone from Pobierania this long: a new download.
}

void Launcher::downloadQueued(const std::wstring& title) {
    const auto now = GetTickCount64();
    if (const auto found = download_announced_.find(title); found != download_announced_.end() && now - found->second < download_announce_quiet_ms) {
        found->second = now;
        return;
    }
    download_announced_[title] = now;
    showNotification(NotificationKind::launch, tr(L"Dodano do kolejki pobierania"), title);
}

void Launcher::downloadFinished(const std::wstring& title, const Game* game) {
    download_announced_.erase(title); // The next download of it is news again.
    showNotification(NotificationKind::download, tr(L"Pobieranie zakończone"), title);
    // Reuse available artwork only. Ambiguous titles don't borrow another game's picture.
    const Game* matched = game;
    if (!matched) for (const auto& candidate : public_library_) {
        if (candidate.name != title || candidate.visibility != GameVisibility::public_game) continue;
        if (matched) { matched = nullptr; break; }
        matched = &candidate;
    }
    if (matched) {
        requestDownloadArt(*matched);
        const auto key = downloadArtTarget(*matched).key();
        auto& notice = notifications_.back(); notice.art_key = key;
        if (const auto* art = art_.find(key); art && art->cover) notice.cover = art->cover;
    }
}

void Launcher::receivePluginDownloads(const std::string& owner, const json::Value& response) {
    auto next = plugins::parseDownloads(response);
    const std::string prefix = "custom:plugin-download:" + owner + ':';
    std::erase_if(download_art_targets_, [&](const auto& entry) { return entry.first.starts_with(prefix); });
    for (const auto& task : next) {
        const auto game = pluginDownloadGame(owner, task);
        download_art::Target target;
        target.name = task.artwork_name.empty() ? task.title : task.artwork_name;
        target.platform = task.platform; target.title_id = task.title_id; target.pc = download_art::pcPlatform(task.platform);
        download_art_targets_[gameKey(game)] = std::move(target);
        requestDownloadArt(game); // Warm the thumbnail as soon as a task enters the queue.
    }
    const auto previous = plugin_downloads_.find(owner);
    if (previous != plugin_downloads_.end()) for (const auto& task : next) {
        const auto old = std::find_if(previous->second.begin(), previous->second.end(), [&](const auto& d) { return d.id == task.id; });
        if (task.phase == "done" && old != previous->second.end() && old->phase != "done") {
            const auto game = pluginDownloadGame(owner, task);
            downloadFinished(task.title, &game);
        }
    }
    plugin_downloads_[owner] = std::move(next);
    refreshDownloads();
}

void Launcher::pollPluginDownloads() {
    if (plugins_busy_ || GetTickCount64() < plugin_download_poll_at_) return;
    plugin_download_poll_at_ = GetTickCount64() + 1000;
    for (const auto& provider : plugins_.contributions("downloads")) {
        if (const auto p = plugin_download_pending_.find(provider.plugin); p != plugin_download_pending_.end() && p->second == plugin_epoch_) continue;
        plugins::Job job; job.kind = plugins::Job::Kind::downloads; job.plugin = provider.plugin;
        job.value = plugins::wire::object(); job.value.properties["command"] = plugins::wire::string(provider.item.command);
        plugin_download_pending_[provider.plugin] = plugin_epoch_;
        submitPlugin(std::move(job));
    }
}

std::vector<Game> Launcher::collectDownloads(std::vector<downloads_ui::Row>& rows) const {
    std::vector<std::pair<Game, downloads_ui::Row>> found;
    const auto add = [&](const Game& game, const Installation& install, bool epic = false) {
        if (!install.active() || install.phase == InstallPhase::unknown) return; // Unknown: the client did not say.
        if (std::any_of(found.begin(), found.end(), [&](const auto& item) { return gameKey(item.first) == gameKey(game); })) return;
        Game shown = game; shown.installation = install;
        found.push_back({std::move(shown), rowOf(game, install, epic)});
    };
    if (aurelia_active_) for (const auto& [id, transfer] : aurelia_downloads_) {
        Game game; game.app_id = id; game.name = transfer.name.empty() ? std::to_wstring(id) : transfer.name; game.installed = false;
        for (const auto& known : installed_library_) if (known.app_id == id && aurelia::validGame(known)) { game = known; break; }
        if ((!private_shelf_ && excluded_ids_.contains(id)) || locallyHidden(game)) continue;
        add(game, transfer.progress);
        if (!found.empty()) {
            auto& row = found.back().second; row.store = L"Aurelia";
            if (transfer.speed) {
                wchar_t rate[48]{}; swprintf_s(rate, L" · %.1f MB/s", static_cast<double>(transfer.speed) / (1024.0 * 1024)); row.amount += rate;
            }
            if (transfer.eta) row.amount += L" · ETA " + std::to_wstring(*transfer.eta / 60) + L":" + (*transfer.eta % 60 < 10 ? L"0" : L"") + std::to_wstring(*transfer.eta % 60);
        }
    }
    for (const auto& game : installed_library_) if (game.source == GameSource::steam && !locallyHidden(game)) add(game, game.installation);
    if (private_shelf_) for (const auto& game : private_category_.games) if (game.source == GameSource::steam) add(game, game.installation);
    // GOG and Epic share Launchme's one installer slot.
    if (!gog_install_ui_.game.name.empty() && (gog_install_ui_.snapshot.busy())) {
        auto install = gog_install_ui_.snapshot.progress();
        if (gog_install_ui_.snapshot.state == gog::InstallState::loading) install.phase = InstallPhase::allocating; // Asking for the files.
        add(gog_install_ui_.game, install, gog_install_ui_.game.source == GameSource::epic);
        for (auto& item : found) if (gameKey(item.first) == gameKey(gog_install_ui_.game)) {
            item.second.destination = (gog_install_ui_.game.source == GameSource::epic || gog_install_ui_.game.source == GameSource::amazon ||
                gog_install_ui_.game.source == GameSource::xbox ||
                gog_install_ui_.snapshot.state == gog::InstallState::installing ?
                gog_install_ui_.request.install_directory : gog_install_ui_.request.download_root).wstring();
        }
    }
    for (const auto& game : store_library_) if (game.source == GameSource::battlenet) add(game, game.installation);
    for (const auto& [id, downloads] : plugin_downloads_) {
        const auto* owner = plugins_.find(id);
        if (!owner || !owner->enabled || !owner->error.empty()) continue;
        for (const auto& download : downloads) {
            auto game = pluginDownloadGame(id, download);
            downloads_ui::Row row; row.store = owner->manifest.name.localized(); row.status = download.status;
            row.destination = download.destination; row.fraction = download.fraction; row.moving = download.moving();
            row.plugin = id; row.command = download.command;
            found.emplace_back(std::move(game), std::move(row));
        }
    }
    // Transferring first, then waiting and paused ones, each in their sources' order.
    std::stable_sort(found.begin(), found.end(), [](const auto& a, const auto& b) { return a.second.moving && !b.second.moving; });
    std::vector<Game> games;
    rows.clear();
    for (auto& [game, row] : found) { games.push_back(std::move(game)); rows.push_back(std::move(row)); }
    return games;
}

int Launcher::downloadsMoving() const {
    std::vector<downloads_ui::Row> rows;
    const auto games = collectDownloads(rows);
    return static_cast<int>(std::count_if(rows.begin(), rows.end(), [](const auto& row) { return row.moving; }));
}

void Launcher::openDownloads() {
    plugin_download_poll_at_ = 0; pollPluginDownloads();
    desktopLeaveLibrary(); rememberPlace();
    closeFriends(); closeQuick(); dismissMenu(); closeCategoryMenu();
    downloads_state_ = {};
    downloads_games_ = collectDownloads(downloads_state_.rows);
    downloads_state_.back = downloads_games_.empty();
    downloads_glide_.valid = false;
    screen_ = ScreenMode::downloads;
    rebuildCards(true);
    selected_ = 0;
    requestDownloadsArt();
    audio_.quick();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

// Follows the sources while the screen is open: the same games update in place (focus and
// scroll stay); a game finished or added rebuilds the list around the focused one.
void Launcher::refreshDownloads() {
    if (!downloadsOpen()) return;
    std::vector<downloads_ui::Row> rows;
    auto games = collectDownloads(rows);
    const bool same = games.size() == downloads_games_.size() &&
        std::equal(games.begin(), games.end(), downloads_games_.begin(), [](const Game& a, const Game& b) { return gameKey(a) == gameKey(b); });
    if (same) {
        bool changed = false;
        for (size_t i = 0; i < games.size(); ++i) {
            const auto& row = rows[i]; const auto& old = downloads_state_.rows[i];
            if (row.status != old.status || row.amount != old.amount || row.fraction != old.fraction || row.moving != old.moving ||
                row.destination != old.destination || games[i].name != downloads_games_[i].name) changed = true;
            if (i < cards_.size()) cards_[i].game.name = games[i].name;
            if (i < cards_.size()) cards_[i].game.installation = games[i].installation;
        }
        downloads_games_ = std::move(games);
        downloads_state_.rows = std::move(rows);
        if (changed) InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    const auto focused = !downloads_state_.back && selected_ < downloads_games_.size() ? gameKey(downloads_games_[selected_]) : std::string{};
    downloads_games_ = std::move(games);
    downloads_state_.rows = std::move(rows);
    rebuildCards(false);
    selected_ = 0;
    for (size_t i = 0; i < downloads_games_.size(); ++i) if (gameKey(downloads_games_[i]) == focused) selected_ = i;
    if (downloads_games_.empty()) downloads_state_.back = true;
    const auto view = renderer_.viewport();
    const downloads_ui::Layout layout{view.width, view.height};
    downloads_state_.scroll = downloads_games_.empty() ? 0 :
        layout.reveal(static_cast<int>(selected_), static_cast<int>(downloads_games_.size()), downloads_state_.scroll);
    requestDownloadsArt();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

// Every frame while Pobierania is open: each bar glides to its reported share instead of jumping
// when the store's next sample arrives (a second or more apart). A new row starts where it is.
void Launcher::tickDownloadBars(float dt, bool& needs_paint) {
    if (!downloadsOpen()) { download_bars_.clear(); downloads_state_.bars.clear(); return; }
    auto& state = downloads_state_;
    state.bars.assign(state.rows.size(), -1.0f);
    std::unordered_map<std::string, float> next;
    const float rate = 1.0f - std::exp(-dt * 2.2f);
    for (size_t i = 0; i < state.rows.size() && i < downloads_games_.size(); ++i) {
        if (!state.rows[i].fraction) continue;
        const float target = std::clamp(*state.rows[i].fraction, 0.0f, 1.0f);
        const auto key = gameKey(downloads_games_[i]);
        const auto found = download_bars_.find(key);
        float shown = found == download_bars_.end() || target < found->second - 0.25f ? target : found->second; // A restart: at once.
        // At least a little speed, so the last bit does not crawl; never past the share.
        if (std::abs(target - shown) > 0.0005f) {
            const float step = (target - shown) * rate;
            shown += step > 0 ? std::max(step, std::min(target - shown, dt * 0.01f)) : step;
            needs_paint = true;
        } else shown = target;
        next[key] = shown;
        state.bars[i] = shown;
    }
    download_bars_ = std::move(next);
}

void Launcher::requestDownloadsArt() {
    for (size_t i = 0; i < downloads_games_.size() && i < cards_.size(); ++i) {
        requestDownloadArt(downloads_games_[i]);
        const auto key = downloadArtTarget(downloads_games_[i]).key();
        if (const auto* art = art_.find(key); art && art->cover) { renderer_.setCover(i, art->cover, art->focus); cards_[i].cover_opacity = 1; }
    }
}

download_art::Target Launcher::downloadArtTarget(const Game& game) const {
    if (const auto found = download_art_targets_.find(gameKey(game)); found != download_art_targets_.end()) return found->second;
    return download_art::forGame(game);
}

void Launcher::requestDownloadArt(const Game& game) {
    auto target = downloadArtTarget(game);
    const auto key = target.key();
    if (const auto* art = art_.find(key); art && art->cover) return;
    const Game* local = &game;
    if (game.source_id.starts_with("plugin-download:")) {
        local = nullptr;
        for (const auto& candidate : public_library_) {
            if (candidate.visibility != GameVisibility::public_game || download_art::cleanName(candidate.name) != download_art::cleanName(target.name)) continue;
            const auto identity = download_art::forGame(candidate);
            if (identity.pc != target.pc || (!target.pc && download_art::normalizePlatform(identity.platform) != download_art::normalizePlatform(target.platform))) continue;
            if (local) { local = nullptr; break; }
            local = &candidate;
        }
    }
    if (local) if (const auto* art = art_.find(gameKey(*local)); art && art->cover) {
        const auto bytes = art->cover;
        art_.at(key).cover = bytes; art_.resize(key);
        return;
    }
    if (key == download_art_active_ || download_art_queue_.contains(key)) return;
    if (const auto retry = download_art_retry_at_.find(key); retry != download_art_retry_at_.end() && GetTickCount64() < retry->second) return;
    if (download_art_queue_.size() >= 64) return;
    download_art_queue_.emplace(key, std::make_pair(std::move(target), local ? *local : Game{}));
}

void Launcher::pollDownloadArt() {
    if (!runtime_started_ && !download_art_fetch_) return;
    const auto now = GetTickCount64();
    if (now >= download_art_poll_at_) {
        download_art_poll_at_ = now + 1000;
        std::vector<downloads_ui::Row> rows;
        std::set<std::string> wanted;
        for (const auto& game : collectDownloads(rows)) {
            wanted.insert(downloadArtTarget(game).key()); requestDownloadArt(game);
            if (const auto found = download_announced_.find(game.name); found != download_announced_.end()) found->second = now;
        }
        std::erase_if(download_announced_, [&](const auto& entry) { return now - entry.second >= download_announce_quiet_ms; });
        for (const auto& notice : notifications_) if (!notice.art_key.empty()) wanted.insert(notice.art_key);
        std::erase_if(download_art_queue_, [&](const auto& entry) { return !wanted.contains(entry.first); });
        std::erase_if(download_art_retry_at_, [&](const auto& entry) { return !wanted.contains(entry.first); });
    }
    if (!download_art_active_.empty() || download_art_queue_.empty()) return;
    const auto next = download_art_queue_.begin();
    const auto key = next->first;
    const auto [target, local] = next->second;
    download_art_queue_.erase(next); download_art_active_ = key;
    const auto language = i18n::language() == i18n::Language::pl ? L"pl" : L"en";
    if (!work_.start([key, target, local, language, read = download_art_fetch_, alive = alive_, hwnd = hwnd_] {
        auto message = std::make_unique<DownloadArtMessage>(); message->key = key;
        try {
            if (read) message->bytes = read(target);
            else {
                HttpBudget budget(30000);
                const auto cache = coverCacheDirectory();
                if (!gameKey(local).empty()) message->bytes = cachedArtwork(local, ArtworkKind::cover, cache);
                if (message->bytes.empty()) message->bytes = download_art::fetch(target, cache, userConfigDirectory() / L"metadata",
                    userSecret(L"STEAMGRIDDB_API_KEY", L"steamgriddb_api_key.txt"), screenscraper::load(), language);
            }
        } catch (...) { message->bytes.clear(); }
        if (alive->load() && PostMessageW(hwnd, download_art_message, 0, reinterpret_cast<LPARAM>(message.get()))) message.release();
    })) {
        download_art_active_.clear(); download_art_retry_at_[key] = now + 60000;
    }
}

void Launcher::onDownloadArt(std::unique_ptr<DownloadArtMessage> message) {
    if (message->key == download_art_active_) download_art_active_.clear();
    if (message->bytes.empty()) { download_art_retry_at_[message->key] = GetTickCount64() + 60000; return; }
    download_art_retry_at_.erase(message->key);
    const auto image = std::make_shared<const std::vector<uint8_t>>(std::move(message->bytes));
    art_.at(message->key).cover = image; art_.resize(message->key);
    for (auto& notice : notifications_) if (notice.art_key == message->key) notice.cover = image;
    if (downloadsOpen()) requestDownloadsArt(); // Bind by identity after reordering, never by a saved row number.
    if (screen_ == ScreenMode::plugin_page || (friends_open_ && plugin_drawer_active_)) requestPluginPageArt();
    invalidateGameOverlay(); InvalidateRect(hwnd_, nullptr, FALSE);
    trimArt();
}

// Up past the first row: Back; Down from Back: the first row.
void Launcher::downloadsMove(int amount) {
    if (!downloadsOpen() || !amount) return;
    auto& state = downloads_state_;
    const int count = static_cast<int>(downloads_games_.size());
    if (state.back) {
        if (amount > 0 && count) { state.back = false; selected_ = 0; audio_.navigate(); }
        else { navigationBoundary(); return; }
    } else {
        const int next = static_cast<int>(selected_) + (amount > 0 ? 1 : -1);
        if (next < 0) state.back = true;
        else if (next >= count) { navigationBoundary(); return; }
        else selected_ = static_cast<size_t>(next);
        audio_.navigate();
    }
    if (!state.back && count) {
        const auto view = renderer_.viewport();
        state.scroll = downloads_ui::Layout{view.width, view.height}.reveal(static_cast<int>(selected_), count, state.scroll);
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::downloadsAccept() {
    if (!downloadsOpen()) return;
    if (downloads_state_.back || selected_ >= downloads_games_.size()) { back(); return; }
    openDownloadSource(downloads_games_[selected_], downloads_state_.rows[selected_]);
}

// A transfer's own place: its plugin's command, Launchme's GOG/Epic installer (with Przerwij), else the
// store's client, where it can be paused or stopped.
void Launcher::openDownloadSource(const Game& game, const downloads_ui::Row& row) {
    if (!row.plugin.empty()) {
        for (auto action : plugins_.contributions("downloads")) if (action.plugin == row.plugin) {
            action.item.command = row.command; pluginAction(action); break;
        }
        return;
    }
    audio_.context();
    switch (game.source) {
    case GameSource::steam:
        if (aurelia_active_) { openAurelia(&game); break; }
        if (execute_(hwnd_, GameAction{L"steam://nav/downloads", {}, {}, ActionKind::open_client}) <= 32)
            showNotification(NotificationKind::error, tr(L"Nie można otworzyć klienta"), L"Steam");
        break;
    case GameSource::gog: openGogInstall(game); break;
    case GameSource::epic: openEpicInstall(game); break;
    case GameSource::amazon: openAmazonInstall(game); break;
    case GameSource::xbox:
        if (xboxOwnInstall(game) && xboxConnected()) openXboxInstall(game); else launchGame(game);
        break;
    default: launchGame(game); break; // Battle.net: its client.
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::downloadsClick(float x, float y) {
    const auto view = renderer_.viewport();
    const downloads_ui::Layout layout{view.width, view.height};
    if (layout.back().contains(x, y)) { back(); return; }
    const int row = layout.hit(x, y, static_cast<int>(downloads_games_.size()), downloads_state_.scroll);
    if (row < 0) return;
    downloads_state_.back = false; selected_ = static_cast<size_t>(row);
    downloadsAccept();
}

bool Launcher::downloadsHover(float x, float y) const {
    const auto view = renderer_.viewport();
    const downloads_ui::Layout layout{view.width, view.height};
    return layout.back().contains(x, y) || layout.hit(x, y, static_cast<int>(downloads_games_.size()), downloads_state_.scroll) >= 0;
}
