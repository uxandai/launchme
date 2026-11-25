// Amazon Games: the account (Nile's sign-in), its library sync, installs and Nile itself, which is
// downloaded only when asked (Biblioteka → Amazon Games, or "Połącz").
#include "launcher.hpp"
#include "../platform/text_encoding.hpp"
#include <algorithm>
#ifdef _WIN32
#include <shlobj.h>
#else
#include "../library/xdg_paths.hpp"
#endif

void Launcher::initializeAmazon() {
    if (!amazon_service_) amazon_service_ = amazon::makeService(userConfigDirectory());
    amazon_state_.helper = amazon::helperInstalled(userConfigDirectory());
    amazon_service_->submit(amazon::Command::restore);
}

void Launcher::amazonCommand(amazon::Command command) {
    if (!amazon_service_) return;
    if (command == amazon::Command::sync && amazon_service_->busy()) return;
    if (command == amazon::Command::complete) {
        amazon_service_->submit(command, std::move(settings_secret_input_)); settings_secret_input_.clear();
        settings_state_.editing = false; amazon_state_.state = amazon::State::verifying;
    } else {
        amazon_service_->submit(command);
        if (command == amazon::Command::connect || command == amazon::Command::download) {
            amazon_enable_on_connect_ = command == amazon::Command::connect;
            amazon_state_.state = amazon::State::preparing; amazon_state_.failure = amazon::Failure::none;
        } else if (command == amazon::Command::sync) amazon_state_.state = amazon::State::syncing;
        else if (command == amazon::Command::cancel) {
            amazon_enable_on_connect_ = false; clearSecretInput(); settings_state_.editing = false;
            amazon_state_.state = amazon_state_.account.empty() ? amazon::State::disconnected : amazon::State::connected;
        } else if (command == amazon::Command::logout) {
            amazon_enable_on_connect_ = false;
            if (gog_install_ui_.game.source == GameSource::amazon && !gog_install_ui_.snapshot.busy()) gog_install_ui_ = {};
        }
    }
    buildSettingsRows();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::pollAmazon() {
    if (!amazon_service_) return;
    if (auto next = amazon_service_->take()) {
        const bool changed = next->account != amazon_state_.account || next->synced_at != amazon_state_.synced_at || next->games != amazon_state_.games;
        if (amazon_state_.state == amazon::State::signing_in && next->state != amazon::State::signing_in && editingSecret() &&
            settings_rows_[settings_state_.row()].id == settings_ui::Id::amazon_callback) { clearSecretInput(); settings_state_.editing = false; }
        amazon_state_ = std::move(*next);
        const bool open_callback = !amazon_state_.browser_url.empty();
        if (open_callback) {
            const auto url = std::move(amazon_state_.browser_url); amazon_state_.browser_url.clear();
            if (execute_(hwnd_, {url, {}, {}, ActionKind::open_client}) <= 32) {
                amazonCommand(amazon::Command::cancel);
                showNotification(NotificationKind::error, L"Amazon Games", tr(L"Nie można otworzyć przeglądarki."));
            }
        }
        if (amazon_enable_on_connect_ && !amazon_state_.account.empty()) {
            amazon_enable_on_connect_ = false;
            if (settings_.enabled_pc_sources.insert(sourceSlug(GameSource::amazon)).second) settingsChanged(true);
        }
        if (changed) { pc_token_->fetch_add(1); refreshPcLibraries(); }
        if (settingsOpen()) {
            buildSettingsRows();
            // The browser shows Amazon's page: the field for the address it lands on takes the focus.
            if (open_callback && amazon_state_.state == amazon::State::signing_in && settings_state_.category == static_cast<int>(settings_ui::Category::library) &&
                settings_state_.platform() == GameSource::amazon) {
                const auto row = std::find_if(settings_rows_.begin(), settings_rows_.end(), [](const auto& r) { return r.id == settings_ui::Id::amazon_callback; });
                if (row != settings_rows_.end()) {
                    settings_state_.selected[settings_state_.category] = static_cast<int>(row - settings_rows_.begin());
                    settings_state_.editing = settings_state_.options = true; clearSecretInput(); settings_secret_selected_ = true; ensureSettingVisible();
                }
            }
        }
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
    if (auto next = amazon_service_->takeInstall(); next && gog_install_ui_.game.source == GameSource::amazon) {
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

void Launcher::openAmazonInstall(const Game& game) {
    if (!amazon_service_ || amazon_state_.account.empty()) return;
    const bool other = gog_install_ui_.game.source != GameSource::amazon || gog_install_ui_.game.source_id != game.source_id;
    if ((gog_service_ && gog_service_->installBusy()) || (epic_service_ && epic_service_->busy() && gog_install_ui_.game.source == GameSource::epic) ||
        (gog_install_ui_.snapshot.busy() && other)) {
        queueInstall(game); return; // Its turn comes (install_queue.cpp).
    }
    dismissMenu(); closeQuick(); closeFriends(); closeCategoryMenu(); cancelEnterHold();
    if (other || gog_install_ui_.snapshot.state == gog::InstallState::none) {
        gog_install_ui_ = {}; gog_install_ui_.game = game;
        auto& request = gog_install_ui_.request; request.product = game.source_id;
#ifdef _WIN32
        PWSTR path = nullptr; std::filesystem::path root = userConfigDirectory();
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Profile, 0, nullptr, &path))) { root = path; CoTaskMemFree(path); }
        request.install_directory = root / L"Games" / (L"Amazon-" + amazon::folderName(game.name, game.source_id));
#else
        // Linux: ~/Games/Amazon/<title>, next to what Heroic and Lutris keep in ~/Games.
        auto root = xdg_paths::home(); if (root.empty()) root = userConfigDirectory();
        request.install_directory = root / L"Games" / L"Amazon" / amazon::folderName(game.name, game.source_id);
#endif
        request.owner_window = reinterpret_cast<uintptr_t>(hwnd_);
        gog_install_ui_.snapshot.product = game.source_id; gog_install_ui_.snapshot.state = gog::InstallState::ready;
        gog_install_ui_.row = 1;
    }
    gog_install_ui_.open = true; InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::amazonInstallAccept() {
    if (!amazon_service_) return;
    auto& ui = gog_install_ui_;
    if (ui.snapshot.state == gog::InstallState::verifying) return;
    if (ui.snapshot.busy()) amazon_service_->cancelOperation();
    else if (ui.snapshot.state == gog::InstallState::completed) gogInstallKey(VK_ESCAPE, false);
    else if (amazon_service_->install(ui.game, ui.request)) {
        downloadQueued(ui.game.name);
        ui.snapshot.state = gog::InstallState::loading; ui.snapshot.error = gog::InstallError::none;
    }
}
