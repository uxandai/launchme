// Xbox account: the sign-in, library sync and installs of Launchme's Xodus-based helper, which is
// downloaded only when asked (Biblioteka → Xbox, or "Połącz"). Games the Xbox app installed keep
// working without any of it.
#include "launcher.hpp"
#include "../platform/text_encoding.hpp"
#include "../platform/unix_time.hpp"
#include <algorithm>
#ifdef _WIN32
#include <shlobj.h>
#else
#include "../library/xdg_paths.hpp"
#endif

void Launcher::initializeXbox() {
    if (!xbox_service_) xbox_service_ = xbox_account::makeService(userConfigDirectory());
    xbox_state_.helper = xbox_account::helperInstalled(userConfigDirectory());
    xbox_service_->submit(xbox_account::Command::restore);
}

bool Launcher::xboxOwnInstall(const Game& game) const {
    return settings_.xbox_own_install && game.source == GameSource::xbox && game.xbox_own_install && !game.account_id.empty() &&
           xbox_account::validProductId(game.xbox_product);
}

void Launcher::xboxCommand(xbox_account::Command command) {
    if (!xbox_service_) return;
    if (command == xbox_account::Command::sync && xbox_service_->busy()) return;
    xbox_service_->submit(command);
    if (command == xbox_account::Command::connect || command == xbox_account::Command::download) {
        xbox_enable_on_connect_ = command == xbox_account::Command::connect;
        xbox_state_.state = xbox_account::State::preparing; xbox_state_.failure = xbox_account::Failure::none;
    } else if (command == xbox_account::Command::sync) xbox_state_.state = xbox_account::State::syncing;
    else if (command == xbox_account::Command::cancel) {
        xbox_enable_on_connect_ = false;
        xbox_state_.state = xbox_state_.account.empty() ? xbox_account::State::disconnected : xbox_account::State::connected;
    } else if (command == xbox_account::Command::offline_status || command == xbox_account::Command::offline_on || command == xbox_account::Command::offline_off) {
        xbox_state_.offline_busy = true;
    } else if (command == xbox_account::Command::logout) {
        xbox_enable_on_connect_ = false; xbox_offline_asked_ = false;
        if (gog_install_ui_.game.source == GameSource::xbox && !gog_install_ui_.snapshot.busy()) gog_install_ui_ = {};
    }
    buildSettingsRows();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::pollXbox() {
    if (!xbox_service_) return;
    if (auto next = xbox_service_->take()) {
        const bool changed = next->account != xbox_state_.account || next->synced_at != xbox_state_.synced_at || next->games != xbox_state_.games ||
                             next->activity != xbox_state_.activity || next->gamepass_recent != xbox_state_.gamepass_recent ||
                             next->updates != xbox_state_.updates || next->offline.games != xbox_state_.offline.games ||
                             next->addons != xbox_state_.addons;
        const bool activity = next->activity != xbox_state_.activity;
        xbox_state_ = std::move(*next);
        if (activity) {
            // The played titles' achievement counts for the card and the previews (the list on request).
            for (const auto* list : {&store_library_, &store_private_library_}) for (const auto& game : *list) {
                if (game.source != GameSource::xbox || !game.xbox_title_id) continue;
                const auto found = xbox_state_.activity.find(game.xbox_title_id);
                if (found == xbox_state_.activity.end() || !found->second.total) continue;
                game_index::Patch counts;
                counts.achievements_unlocked = found->second.unlocked; counts.achievements_total = found->second.total;
                gameIndexMerge(gameKey(game), game_index::Source::achievements, std::move(counts));
            }
        }
        // Once per sign-in: whether this PC is the account's offline PC (Biblioteka → Xbox shows it).
        if (!xbox_state_.account.empty() && !xbox_offline_asked_ && xbox_state_.state == xbox_account::State::connected &&
            xbox_state_.helper && unix_time::now() - xbox_state_.offline.checked_at > 24 * 3600) {
            xbox_offline_asked_ = true;
            xbox_service_->submit(xbox_account::Command::offline_status);
        }
        if (xbox_enable_on_connect_ && !xbox_state_.account.empty()) {
            xbox_enable_on_connect_ = false;
            if (settings_.enabled_pc_sources.insert(sourceSlug(GameSource::xbox)).second) settingsChanged(true);
        }
        if (xbox_state_.state == xbox_account::State::error && xbox_state_.failure == xbox_account::Failure::expired)
            showNotification(NotificationKind::error, L"Xbox", xbox_account::statusText(xbox_state_));
        if (changed) { pc_token_->fetch_add(1); refreshPcLibraries(); }
        if (settingsOpen()) buildSettingsRows();
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
    while (auto answer = xbox_service_->takeAchievements()) receiveXboxAchievements(std::move(*answer));
    if (auto next = xbox_service_->takeInstall(); next && gog_install_ui_.game.source == GameSource::xbox) {
        if (next->product == gog_install_ui_.game.source_id) {
            const auto previous_state = gog_install_ui_.snapshot.state;
            gog_install_ui_.snapshot = std::move(*next); applyGogInstallProgress();
            const auto& status = gog_install_ui_.snapshot;
            if (status.state == gog::InstallState::completed) {
                completed_pc_installs_.insert(gameKey(gog_install_ui_.game));
                if (previous_state != gog::InstallState::completed) downloadFinished(gog_install_ui_.game.name, &gog_install_ui_.game);
            }
            if (!status.busy()) { pc_token_->fetch_add(1); refreshPcLibraries(); }
        }
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

void Launcher::openXboxInstall(const Game& game) {
    if (!xbox_service_ || !xboxConnected() || !xboxOwnInstall(game)) return;
    const bool other = gog_install_ui_.game.source != GameSource::xbox || gog_install_ui_.game.source_id != game.source_id;
    if ((gog_service_ && gog_service_->installBusy()) || (epic_service_ && epic_service_->busy() && gog_install_ui_.game.source == GameSource::epic) ||
        (amazon_service_ && amazon_service_->busy() && gog_install_ui_.game.source == GameSource::amazon) || (gog_install_ui_.snapshot.busy() && other)) {
        showNotification(NotificationKind::error, L"Xbox", tr(L"Najpierw dokończ lub anuluj bieżące pobieranie.")); return;
    }
    dismissMenu(); closeQuick(); closeFriends(); closeCategoryMenu(); cancelEnterHold();
    if (other || gog_install_ui_.snapshot.state == gog::InstallState::none) {
        gog_install_ui_ = {}; gog_install_ui_.game = game;
        auto& request = gog_install_ui_.request; request.product = game.source_id;
#ifdef _WIN32
        PWSTR path = nullptr; std::filesystem::path root = userConfigDirectory();
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Profile, 0, nullptr, &path))) { root = path; CoTaskMemFree(path); }
        request.install_directory = root / L"Games" / (L"Xbox-" + xbox_account::folderName(game.name, game.xbox_product));
#else
        // Linux: ~/Games/Xbox/<title>.
        auto root = xdg_paths::home(); if (root.empty()) root = userConfigDirectory();
        request.install_directory = root / L"Games" / L"Xbox" / xbox_account::folderName(game.name, game.xbox_product);
#endif
        request.owner_window = reinterpret_cast<uintptr_t>(hwnd_);
        gog_install_ui_.snapshot.product = game.source_id; gog_install_ui_.snapshot.state = gog::InstallState::ready;
        gog_install_ui_.row = 1;
    }
    gog_install_ui_.open = true; InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::xboxInstallAccept() {
    if (!xbox_service_) return;
    auto& ui = gog_install_ui_;
    if (ui.snapshot.state == gog::InstallState::verifying) return;
    if (ui.snapshot.busy()) xbox_service_->cancelOperation();
    else if (ui.snapshot.state == gog::InstallState::completed) gogInstallKey(VK_ESCAPE, false);
    else if (xbox_service_->install(ui.game, ui.request)) {
        downloadQueued(ui.game.name);
        ui.snapshot.state = gog::InstallState::loading; ui.snapshot.error = gog::InstallError::none;
    }
}

bool Launcher::xboxAchievements(const Game& game) const {
    return xbox_service_ && xboxConnected() && game.source == GameSource::xbox && game.xbox_title_id && isGame(game) && game.kind != GameKind::catalog;
}

void Launcher::requestXboxAchievements(const Game& game, bool force) {
    const auto key = gameKey(game);
    if (key.empty()) return;
    const auto now = GetTickCount64();
    auto& cache = plugin_achievements_[key];
    const bool same = cache.plugin == "xbox" && cache.name == game.name;
    // As a plugin's: one question at a time, an answer good for ten minutes (a lost one after a minute).
    if (!force && same && ((cache.pending && now - cache.checked_at < 60000) || (!cache.pending && cache.answered && now - cache.checked_at < 10 * 60000))) return;
    if (!same) { cache = {}; cache.plugin = "xbox"; cache.name = game.name; }
    cache.pending = true; cache.checked_at = now; cache.generation = ++plugin_achievements_generation_;
    xbox_achievement_keys_[game.xbox_title_id] = key;
    xbox_service_->achievements(game.xbox_title_id);
}

void Launcher::receiveXboxAchievements(xbox_account::AchievementsAnswer answer) {
    const auto asked = xbox_achievement_keys_.find(answer.title_id);
    if (asked == xbox_achievement_keys_.end()) return;
    const auto key = asked->second;
    xbox_achievement_keys_.erase(asked);
    const auto found = plugin_achievements_.find(key);
    if (found == plugin_achievements_.end() || found->second.plugin != "xbox") return;
    auto& cache = found->second;
    cache.pending = false; cache.checked_at = GetTickCount64();
    if (answer.failed) {
        if (!cache.answered) cache.message = tr(L"Osiągnięcia chwilowo niedostępne."); // A passing failure keeps the last list.
    } else {
        cache.answered = true; cache.list = std::move(answer.list); cache.source = L"Xbox"; cache.message.clear();
        game_index::Patch counts;
        counts.achievements_unlocked = cache.list.unlocked; counts.achievements_total = cache.list.total;
        gameIndexMerge(key, game_index::Source::achievements, std::move(counts));
        recordAchievements(key, cache.name, cache.list);
    }
    achievementsArrived(key);
}

bool Launcher::xboxUpdateAvailable(const Game& game) const {
    return game.source == GameSource::xbox && game.installed && xboxConnected() && xbox_account::ownInstall(game) &&
           xbox_state_.updates.contains(game.xbox_product) && !game.install_directory.empty();
}

void Launcher::openXboxUpdate(const Game& game) {
    if (!xboxUpdateAvailable(game)) return;
    // The account's copy of the game (the installed entry may carry only the folder's facts).
    auto account_game = game;
    if (!xboxOwnInstall(account_game)) { showNotification(NotificationKind::error, L"Xbox", tr(L"Włącz instalację przez Launchme w Biblioteka → Xbox.")); return; }
    openXboxInstall(account_game);
    // The same folder: the helper downloads only the files that changed.
    if (gog_install_ui_.open && gog_install_ui_.game.source_id == game.source_id && !gog_install_ui_.snapshot.busy())
        gog_install_ui_.request.install_directory = game.install_directory;
}

uint64_t Launcher::xboxOfflineUntil(const Game& game) const {
    if (game.source != GameSource::xbox || game.xbox_product.empty() || xbox_state_.account.empty()) return 0;
    return xbox_account::offlineUntil(xbox_state_.offline, game.xbox_product, unix_time::now());
}

void Launcher::xboxOfflineAction() {
    if (!xbox_service_ || !xboxConnected() || xbox_state_.offline_busy) return;
    const auto& offline = xbox_state_.offline;
    if (!offline.enabled) { drm_confirm_.clear(); xboxCommand(xbox_account::Command::offline_status); return; }
    // A change counts against Microsoft's yearly limit: the second press does it.
    if (drm_confirm_ != "xbox-offline") { drm_confirm_ = "xbox-offline"; buildSettingsRows(); audio_.context(); InvalidateRect(hwnd_, nullptr, FALSE); return; }
    drm_confirm_.clear();
    xboxCommand(*offline.enabled ? xbox_account::Command::offline_off : xbox_account::Command::offline_on);
}

std::vector<std::wstring> Launcher::xboxBadges(const Game& game) const {
    std::vector<std::wstring> badges;
    if (game.source != GameSource::xbox) return badges;
    if (game.xbox_game_pass) badges.push_back(L"Game Pass"); // Played through the subscription, not bought.
    if (const auto until = xboxOfflineUntil(game)) badges.push_back(tr(L"Offline do ") + xbox_account::offlineDate(until));
    if (xboxUpdateAvailable(game)) badges.push_back(tr(L"Dostępna aktualizacja"));
    // The account's add-ons for it (listed; Launchme does not install them yet).
    if (const auto found = xbox_state_.addons.find(game.xbox_product); found != xbox_state_.addons.end() && !xbox_state_.account.empty())
        badges.push_back(tr(L"Dodatki: ") + std::to_wstring(found->second.size()));
    return badges;
}
