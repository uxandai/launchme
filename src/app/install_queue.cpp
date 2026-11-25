// Kolejka instalacji: GOG, Epic, Amazon and Xbox share Launchme's one installer slot (gog_install_ui_).
// A game asked for while another downloads waits here instead of being refused; when the slot is free
// the next one starts with the dialog's default folders, as if Zainstaluj had been pressed in it.
#include "launcher.hpp"

#include <algorithm>

namespace {
bool queuedSource(GameSource source) {
    return source == GameSource::gog || source == GameSource::epic || source == GameSource::amazon || source == GameSource::xbox;
}
}

bool Launcher::storeInstallBusy() const {
    return (gog_service_ && gog_service_->installBusy()) || (epic_service_ && epic_service_->busy()) ||
           (amazon_service_ && amazon_service_->busy()) || (xbox_service_ && xbox_service_->busy()) || gog_install_ui_.snapshot.busy();
}

int Launcher::installQueuePosition(const Game& game) const {
    const auto key = gameKey(game);
    const auto found = std::find_if(install_queue_.begin(), install_queue_.end(), [&](const Game& queued) { return gameKey(queued) == key; });
    return found == install_queue_.end() ? 0 : static_cast<int>(found - install_queue_.begin()) + 1;
}

void Launcher::queueInstall(const Game& game) {
    if (!queuedSource(game.source)) return;
    dismissMenu(); closeQuick(); closeCategoryMenu(); cancelEnterHold();
    if (int position = installQueuePosition(game)) {
        showNotification(NotificationKind::launch, tr(L"Już w kolejce instalacji"), game.name + L" · " + std::to_wstring(position));
        return;
    }
    install_queue_.push_back(game);
    showNotification(NotificationKind::launch, tr(L"Dodano do kolejki instalacji"),
                     game.name + L" · " + std::to_wstring(install_queue_.size()) + L". " + tr(L"w kolejce"));
    refreshDownloads();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

bool Launcher::unqueueInstall(const Game& game) {
    const auto key = gameKey(game);
    const auto before = install_queue_.size();
    std::erase_if(install_queue_, [&](const Game& queued) { return gameKey(queued) == key; });
    if (install_queue_.size() == before) return false;
    showNotification(NotificationKind::launch, tr(L"Usunięto z kolejki instalacji"), game.name);
    refreshDownloads();
    InvalidateRect(hwnd_, nullptr, FALSE);
    return true;
}

void Launcher::openStoreInstall(const Game& game) {
    switch (game.source) {
    case GameSource::gog: openGogInstall(game); break;
    case GameSource::epic: openEpicInstall(game); break;
    case GameSource::amazon: openAmazonInstall(game); break;
    case GameSource::xbox: openXboxInstall(game); break;
    default: break;
    }
}

void Launcher::pumpInstallQueue() {
    auto& ui = gog_install_ui_;
    if (queue_starting_) {
        // The slot was prepared for the queued game: once it is ready (GOG asks for its installers first),
        // Zainstaluj, with the dialog left as the player had it (shut, unless they opened it meanwhile).
        if (gameKey(ui.game) != *queue_starting_ || ui.steam) { queue_starting_.reset(); return; }
        if (ui.snapshot.state == gog::InstallState::loading) return;
        queue_starting_.reset();
        if (ui.snapshot.state != gog::InstallState::ready) {
            showNotification(NotificationKind::error, tr(L"Nie można zainstalować z kolejki"), ui.game.name);
            return;
        }
        const bool was_open = ui.open;
        ui.open = true; ui.row = 3;
        gogInstallAccept();
        if (!was_open) ui.open = false;
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (install_queue_.empty() || storeInstallBusy()) return;
    // The player is choosing something in the dialog for another game: that comes first.
    if (gogInstallOpen() && !ui.snapshot.busy() && ui.snapshot.state != gog::InstallState::completed &&
        ui.snapshot.state != gog::InstallState::cancelled && ui.snapshot.state != gog::InstallState::error) return;
    const auto game = install_queue_.front();
    install_queue_.pop_front();
    const bool was_open = gogInstallOpen();
    ui.open = false;
    queue_starting_ = gameKey(game);
    openStoreInstall(game);
    if (!was_open) ui.open = false; // Prepared behind the scenes; Pobierania and the tile show its progress.
    if (gameKey(ui.game) != *queue_starting_) { queue_starting_.reset(); return; } // Its store went away (signed out).
    pumpInstallQueue(); // Epic, Amazon and Xbox are ready at once.
    refreshDownloads();
}
