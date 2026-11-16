#include "launcher.hpp"
#include <algorithm>

void Launcher::initializeGog() {
    gog_service_ = gog::makeService(gog::makeBackend(), gog::makeVault(userConfigDirectory() / L"accounts" / L"gog.session"));
    gog_service_->submit(gog::Command::restore);
    std::lock_guard lock(store_gateway_->mutex); store_gateway_->gog = gog_service_.get(); // Plugins' store requests.
}
void Launcher::gogCommand(gog::Command command) {
    if (!gog_service_) return;
    if (command == gog::Command::sync && gog_service_->installBusy()) return;
    if (command == gog::Command::connect && (epic_state_.state == epic::State::preparing || epic_state_.state == epic::State::signing_in || epic_state_.state == epic::State::verifying)) epicCommand(epic::Command::cancel);
    if (command == gog::Command::complete) {
        gog_service_->submit(command, std::move(settings_secret_input_)); settings_secret_input_.clear();
        settings_state_.editing = false; gog_state_.state = gog::State::verifying;
    } else {
        gog_service_->submit(command);
        if (command == gog::Command::connect || command == gog::Command::logout) {
            stopGogComet(); // It holds the old account's session.
            gog_enable_on_connect_ = command == gog::Command::connect;
            gog_state_ = {};
            // Invalidate old membership and queued scans before the worker can
            // finish. Local GOG games remain available during account changes.
            for (auto* games : {&store_library_, &store_private_library_})
                std::erase_if(*games, [](const Game& game) { return game.source == GameSource::gog && !game.account_id.empty(); });
            restore_pending_ = false;
            library_state_.bookmark.reset(); library_state_dirty_ = true;
            // Cancel work tied to the old account view, including returning from
            // an artwork/crop/details screen after its membership disappeared.
            const auto gog_game = [](const Game& g) { return g.source == GameSource::gog && !g.account_id.empty(); };
            closeEditorsFor(gog_game);
            if (screen_ == ScreenMode::details && !detail_games_.empty() && gog_game(detail_games_[0])) { suspendDetails(); showCategory(0); }
            {
                std::lock_guard lock(jobs_->mutex);
                ++jobs_->generation; jobs_->jobs.clear(); jobs_->secondary.clear(); jobs_->prefetch.clear(); jobs_->requested.clear();
            }
            pc_token_->fetch_add(1);
            buildShelves();
            if (screen_ == ScreenMode::library || screen_ == ScreenMode::shelves || screen_ == ScreenMode::search) rebuildCards(false);
            if (command == gog::Command::connect) gog_state_.state = gog::State::preparing;
            refreshPcLibraries();
        } else if (command == gog::Command::sync) gog_state_.state = gog::State::syncing;
        else if (command == gog::Command::cancel) {
            gog_enable_on_connect_ = false;
            clearSecretInput(); settings_state_.editing = false;
            gog_state_.state = gog_state_.account.empty() ? gog::State::disconnected : gog::State::offline;
        }
    }
    buildSettingsRows();
}
void Launcher::pollGog() {
    if (!gog_service_) return;
    auto result = gog_service_->take();
    if (!result) return;
    const bool changed = !gog_initial_ready_ || result->account != gog_state_.account || result->synced_at != gog_state_.synced_at ||
        result->games.size() != gog_state_.games.size() || !std::equal(result->games.begin(), result->games.end(), gog_state_.games.begin(), gog_state_.games.end(),
            [](const Game& a, const Game& b) { return gameKey(a) == gameKey(b) && a.name == b.name && a.visibility == b.visibility; });
    gog_initial_ready_ = true;
    const bool open_callback = result->state == gog::State::signing_in && !result->browser_url.empty();
    if (gog_state_.state == gog::State::signing_in && result->state != gog::State::signing_in && editingSecret() &&
        settings_rows_[settings_state_.row()].id == settings_ui::Id::gog_callback) {
        clearSecretInput();
        settings_state_.editing = false;
    }
    gog_state_ = std::move(*result);
    if (!gog_state_.browser_url.empty()) {
        const auto url = std::move(gog_state_.browser_url); gog_state_.browser_url.clear();
        if (execute_(hwnd_, {url, {}, {}, ActionKind::open_client}) <= 32) {
            gogCommand(gog::Command::cancel);
            showNotification(NotificationKind::error, L"GOG", tr(L"Nie można otworzyć przeglądarki."));
        }
    }
    if (gog_enable_on_connect_ && !gog_state_.account.empty() &&
        (gog_state_.state == gog::State::connected || gog_state_.state == gog::State::syncing)) {
        gog_enable_on_connect_ = false;
        if (!settings_.enabled_pc_sources.contains("gog")) { settings_.enabled_pc_sources.insert("gog"); settingsChanged(true); }
    }
    if (changed) { pc_token_->fetch_add(1); refreshPcLibraries(); }
    if (settingsOpen()) {
        buildSettingsRows();
        if (open_callback && gog_state_.state == gog::State::signing_in &&
            settings_state_.category == static_cast<int>(settings_ui::Category::library) && settings_state_.platform() == GameSource::gog) {
            const auto callback = std::find_if(settings_rows_.begin(), settings_rows_.end(), [](const auto& row) {
                return row.id == settings_ui::Id::gog_callback;
            });
            if (callback != settings_rows_.end()) {
                settings_state_.selected[settings_state_.category] = static_cast<int>(callback - settings_rows_.begin());
                settings_state_.options = settings_state_.editing = true;
                clearSecretInput(); settings_secret_selected_ = true;
                ensureSettingVisible();
            }
        }
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}
