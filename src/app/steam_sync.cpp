#include "launcher.hpp"
#include "../library/steam_sync_state.hpp"
#include <algorithm>

namespace {
bool sameCollections(const std::vector<SteamCollection>& a, const std::vector<SteamCollection>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) if (a[i].id != b[i].id || a[i].name != b[i].name || a[i].app_ids != b[i].app_ids) return false;
    return true;
}
}
void Launcher::startSteamSync() {
    if (aurelia_active_ && aurelia_library_source_) return; // No Steam client here to write to.
    const auto queue = steam_sync_queue_; const auto alive = alive_; const auto hwnd = hwnd_;
    work_.start([queue, alive, hwnd] {
        const auto root = findSteamRoot();
        size_t offset = 0;
        while (alive->load()) {
            uint64_t account = 0;
            std::vector<steam_sync::Change> pending;
            {
                std::unique_lock lock(queue->mutex);
                queue->wake.wait_for(lock, std::chrono::seconds(5), [&] { return !alive->load() || queue->dirty; });
                if (!alive->load()) return;
                queue->dirty = false; account = queue->account;
                const auto size = queue->pending.size();
                for (size_t i = 0; i < std::min<size_t>(size, 16); ++i) pending.push_back(queue->pending[(offset + i) % size]);
                offset += pending.size();
            }
            if (!account) continue;
            auto message = std::make_unique<SteamSyncMessage>();
            try { message->result = steam_sync::exchange(root, account, pending); }
            catch (...) { message->result.account = account; message->result.error = "unavailable"; }
            if (!alive->load()) return;
            if (PostMessageW(hwnd, steam_sync_message, 0, reinterpret_cast<LPARAM>(message.get()))) message.release();
        }
    });
    requestSteamSync(true);
}
void Launcher::requestSteamSync(bool force) {
    if (aurelia_active_ && aurelia_library_source_) { if (force) steam_rescan_->wake(); return; }
    if (!library_state_readable_) return;
    auto pending = steam_sync::pending(library_state_, account_);
    std::lock_guard lock(steam_sync_queue_->mutex);
    if (!force && steam_sync_queue_->account == account_ && steam_sync_queue_->pending == pending) return;
    steam_sync_queue_->account = account_; steam_sync_queue_->pending = std::move(pending); steam_sync_queue_->dirty = true;
    steam_sync_queue_->wake.notify_one();
}
bool Launcher::hiddenInSteam(const Game& game) const {
    return game.source == GameSource::steam && steam_sync_snapshot_ && steam_sync_snapshot_->account == account_ &&
        steam_sync_snapshot_->hidden.contains(game.app_id);
}
std::wstring Launcher::steamSyncStatus() const {
    if (!steam_sync_error_.empty()) {
        if (steam_sync_error_ == "unavailable") return tr(L"Połączenie niedostępne. Steam wymaga opcji -cef-enable-debugging (port 8080).");
        if (steam_sync_error_ == "collection-missing" || steam_sync_error_ == "name-conflict") return tr(L"Konflikt kolekcji w Steam. Zmiany pozostają w kolejce.");
        if (steam_sync_error_ == "not-ready") return tr(L"Steam jeszcze wczytuje bibliotekę. Zmiany pozostają w kolejce.");
        if (steam_sync_error_ == "account-changed") return tr(L"W Steam zalogowane jest inne konto. Zmiany pozostają w kolejce.");
        if (steam_sync_error_ == "privacy-unavailable") return tr(L"Steam nie wczytał jeszcze prywatności gier. Zmiany pozostają w kolejce.");
        if (steam_sync_error_ == "unsupported" || steam_sync_error_ == "invalid-reply")
            return tr(L"Ta wersja Steam nie przyjmuje zmian z launchme. Zmiany pozostają w kolejce.");
        return tr(L"Steam nie potwierdził zapisu. Zmiany pozostają w kolejce.");
    }
    if (!steam_sync::pending(library_state_, account_).empty()) return tr(L"Oczekiwanie na zapis w Steam…");
    if (steam_sync_snapshot_ && steam_sync_snapshot_->account == account_) {
        if (steam_sync_cloud_pending_) return tr(L"Zapisano w Steam. Oczekiwanie na chmurę.");
        return steam_sync::unsyncedCount(library_state_, account_)
            ? tr(L"Połączono ze Steam. Zmiany gier, których Steam nie zna (np. usunięte dema), zostają tylko w launchme.")
            : tr(L"Połączono ze Steam");
    }
    return tr(L"Oczekiwanie na Steam…");
}
void Launcher::onSteamSync(std::unique_ptr<SteamSyncMessage> message) {
    auto& result = message->result;
    if (result.account != account_) return;
    const bool status_changed = steam_sync_error_ != result.error || steam_sync_cloud_pending_ != result.cloud_pending;
    steam_sync_error_ = result.error; steam_sync_cloud_pending_ = result.cloud_pending;
    if (!result.snapshot) { if (status_changed) { buildSettingsRows(); InvalidateRect(hwnd_, nullptr, FALSE); } return; }
    const bool collections_changed = !sameCollections(collections_source_, result.snapshot->collections);
    const bool visibility_changed = !steam_sync_snapshot_ || steam_sync_snapshot_->hidden != result.snapshot->hidden ||
        steam_sync_snapshot_->private_apps != result.snapshot->private_apps;
    bool applied = false;
    for (const auto& change : result.applied) applied |= steam_sync::acknowledge(library_state_, account_, change);
    // Apps Steam has no record of: the edit stays here only and no longer waits for Steam.
    bool kept = false;
    for (const auto& change : result.unavailable) kept |= steam_sync::keepLocal(library_state_, account_, change);
    kept |= steam_sync::pruneUnsynced(library_state_, account_);
    applied |= kept;
    library_state_dirty_ |= applied;
    steam_sync_snapshot_ = std::move(result.snapshot);
    collections_source_ = steam_sync_snapshot_->collections;
    if (collections_changed) rebuildUninstalledSteamGames();
    if (collections_changed || visibility_changed || applied) {
        refreshRecentGames();
        if (visibility_changed || applied) steam_rescan_->request();
    } else if (status_changed) { buildSettingsRows(); InvalidateRect(hwnd_, nullptr, FALSE); }
    requestSteamSync();
}
