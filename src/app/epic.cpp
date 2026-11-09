#include "launcher.hpp"
#include "../platform/text_encoding.hpp"
#include <algorithm>
#include <shlobj.h>

void Launcher::initializeEpic() {
    const auto root = userConfigDirectory();
    epic_service_ = epic::makeService(epic::makeBackend(), epic::makeVault(root / L"accounts" / L"epic.session"), epic::makeRunner(root),
        [root] { return epic::prepareConfig(root / L"accounts" / L"epic-client.bin"); });
    epic_service_->submit(epic::Command::restore);
    std::lock_guard lock(store_gateway_->mutex); store_gateway_->epic = epic_service_.get(); // Plugins' store requests.
}
void Launcher::epicCommand(epic::Command command) {
    if (!epic_service_) return;
    if (command == epic::Command::sync && epic_service_->busy()) return;
    if (command == epic::Command::connect && (gog_state_.state == gog::State::preparing || gog_state_.state == gog::State::signing_in || gog_state_.state == gog::State::verifying)) gogCommand(gog::Command::cancel);
    if (command == epic::Command::complete) {
        epic_service_->submit(command, std::move(settings_secret_input_)); settings_secret_input_.clear();
        settings_state_.editing = false; epic_state_.state = epic::State::verifying;
    } else {
        epic_service_->submit(command);
        if (command == epic::Command::connect || command == epic::Command::logout) {
            epic_enable_on_connect_ = command == epic::Command::connect;
            epic_state_ = {}; epic_launch_pending_ = false;
            if (gog_install_ui_.game.source == GameSource::epic) { gog_install_ui_.snapshot.state = gog::InstallState::none; applyGogInstallProgress(); gog_install_ui_ = {}; }
            for (auto* games : {&store_library_, &store_private_library_})
                std::erase_if(*games, [](const Game& game) { return game.source == GameSource::epic && !game.account_id.empty(); });
            restore_pending_ = false; library_state_.bookmark.reset(); library_state_dirty_ = true;
            const auto epic_game = [](const Game& g) { return g.source == GameSource::epic && !g.account_id.empty(); };
            closeEditorsFor(epic_game);
            if (screen_ == ScreenMode::details && !detail_games_.empty() && epic_game(detail_games_[0])) { suspendDetails(); showCategory(0); }
            if (pending_game_.source == GameSource::epic) pending_launch_ = false;
            {
                std::lock_guard lock(jobs_->mutex); ++jobs_->generation;
                jobs_->jobs.clear(); jobs_->secondary.clear(); jobs_->prefetch.clear(); jobs_->requested.clear();
            }
            pc_token_->fetch_add(1); buildShelves();
            if (screen_ == ScreenMode::search) updateSearch();
            else if (screen_ == ScreenMode::library || screen_ == ScreenMode::shelves) rebuildCards(false);
            if (command == epic::Command::connect) epic_state_.state = epic::State::preparing;
            refreshPcLibraries();
        } else if (command == epic::Command::sync) epic_state_.state = epic::State::syncing;
        else if (command == epic::Command::cancel) {
            epic_enable_on_connect_ = false;
            clearSecretInput();
            settings_state_.editing = false; epic_state_.state = epic_state_.account.empty() ? epic::State::disconnected : epic::State::offline;
        }
    }
    buildSettingsRows();
}
void Launcher::pollEpic() {
    if (!epic_service_) return;
    if (auto next = epic_service_->take()) {
        const bool changed = !epic_initial_ready_ || next->account != epic_state_.account || next->synced_at != epic_state_.synced_at || next->games.size() != epic_state_.games.size();
        epic_initial_ready_ = true;
        if (epic_state_.state == epic::State::signing_in && next->state != epic::State::signing_in && editingSecret() && settings_rows_[settings_state_.row()].id == settings_ui::Id::epic_callback) {
            clearSecretInput(); settings_state_.editing = false;
        }
        epic_state_ = std::move(*next);
        const bool open_callback = !epic_state_.browser_url.empty();
        if (open_callback) {
            const auto url = std::move(epic_state_.browser_url); epic_state_.browser_url.clear();
            if (execute_(hwnd_, {url, {}, {}, ActionKind::open_client}) <= 32) {
                epicCommand(epic::Command::cancel); showNotification(NotificationKind::error, L"Epic Games", tr(L"Nie można otworzyć przeglądarki."));
            }
        }
        if (epic_enable_on_connect_ && !epic_state_.account.empty()) {
            epic_enable_on_connect_ = false;
            if (settings_.enabled_pc_sources.insert("epic").second) settingsChanged(true);
        }
        if (changed) { pc_token_->fetch_add(1); refreshPcLibraries(); }
        if (settingsOpen()) {
            buildSettingsRows();
            if (open_callback && epic_state_.state == epic::State::signing_in && settings_state_.category == static_cast<int>(settings_ui::Category::library) && settings_state_.platform() == GameSource::epic) {
                const auto row = std::find_if(settings_rows_.begin(), settings_rows_.end(), [](const auto& r) { return r.id == settings_ui::Id::epic_callback; });
                if (row != settings_rows_.end()) {
                    settings_state_.selected[settings_state_.category] = static_cast<int>(row - settings_rows_.begin());
                    settings_state_.editing = settings_state_.options = true; clearSecretInput(); settings_secret_selected_ = true; ensureSettingVisible();
                }
            }
        }
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
    if (auto next = epic_service_->takeInstall(); next && gog_install_ui_.game.source == GameSource::epic) {
        if (next->product.empty()) { gog_install_ui_.snapshot.state = gog::InstallState::none; applyGogInstallProgress(); gog_install_ui_ = {}; }
        else if (next->product == gog_install_ui_.game.source_id) {
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
    if (auto result = epic_service_->takeLaunch()) {
        const bool requested = epic_launch_pending_; epic_launch_pending_ = false;
        if (!requested) return;
        // Opcje uruchamiania's switches; the action's arguments hold Epic's token: not logged.
        if (result->action) launchFlagsBegin(result->game, nullptr);
        if (result->action && execute_(hwnd_, result->action) > 32) {
            if (settings_.launch_animation) startLaunchAnimation(result->game);
            else audio_.launch();
            audio_.stopMusic(); beginGameSession(result->game);
            if (settings_.quick_resume) freezeOthers(gameKey(result->game));
        } else {
            launchFlagsEnd(gameKey(result->game), L"Not started");
            showNotification(NotificationKind::error, L"Epic Games", tr(L"Nie udało się uruchomić gry. Sprawdź połączenie i konto Epic."));
        }
        SecureZeroMemory(result->action.arguments.data(), result->action.arguments.size() * sizeof(wchar_t));
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}
void Launcher::openEpicInstall(const Game& game) {
    if (!epic_service_ || epic_state_.account.empty()) return;
    if ((gog_service_ && gog_service_->installBusy()) || (epic_service_->busy() && (gog_install_ui_.game.source != GameSource::epic || gog_install_ui_.game.source_id != game.source_id))) {
        showNotification(NotificationKind::error, L"Epic Games", tr(L"Najpierw dokończ lub anuluj bieżące pobieranie.")); return;
    }
    dismissMenu(); closeQuick(); closeFriends(); closeCategoryMenu(); cancelEnterHold();
    if (gog_install_ui_.game.source != GameSource::epic || gog_install_ui_.game.source_id != game.source_id) {
        gog_install_ui_ = {}; gog_install_ui_.game = game;
        PWSTR path = nullptr; std::filesystem::path root = userConfigDirectory();
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Profile, 0, nullptr, &path))) { root = path; CoTaskMemFree(path); }
        auto& request = gog_install_ui_.request; request.product = game.source_id;
        request.install_directory = root / L"Games" / (L"Epic-" + text_encoding::wide(game.source_id));
        request.owner_window = reinterpret_cast<uintptr_t>(hwnd_);
        gog_install_ui_.snapshot.product = game.source_id; gog_install_ui_.snapshot.state = gog::InstallState::ready;
        if (!game.epic_partner.empty()) { gog_install_ui_.snapshot.state = gog::InstallState::error; gog_install_ui_.snapshot.error = gog::InstallError::unavailable; }
        gog_install_ui_.row = 1;
    }
    gog_install_ui_.open = true; InvalidateRect(hwnd_, nullptr, FALSE);
}
void Launcher::epicInstallAccept() {
    if (!epic_service_) return;
    auto& ui = gog_install_ui_;
    if (ui.snapshot.state == gog::InstallState::installing) return;
    if (ui.snapshot.busy()) epic_service_->cancelOperation();
    else if (ui.snapshot.state == gog::InstallState::completed) gogInstallKey(VK_ESCAPE, false);
    else if (ui.game.epic_partner.empty() && epic_service_->install(ui.game, ui.request)) {
        downloadQueued(ui.game.name);
        ui.snapshot.state = gog::InstallState::loading; ui.snapshot.error = gog::InstallError::none;
    }
}
void Launcher::startEpicGame(const Game& game) {
    if (!epic_service_ || epic_state_.account.empty()) { showNotification(NotificationKind::error, L"Epic Games", tr(L"Połącz konto Epic w Ustawieniach → Biblioteka.")); return; }
    if (epic_launch_pending_ || !epic_service_->launch(game)) { showNotification(NotificationKind::error, L"Epic Games", tr(L"Najpierw dokończ lub anuluj bieżące pobieranie.")); return; }
    epic_launch_pending_ = true; dismissMenu(); showNotification(NotificationKind::launch, game.name, tr(L"Przygotowanie uruchomienia…"));
}
