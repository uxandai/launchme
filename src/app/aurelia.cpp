#include "launcher.hpp"
#include "../library/visibility.hpp"
#include "../platform/atomic_file.hpp"
#include "../platform/text_encoding.hpp"
#include "../platform/updater.hpp"
#include <algorithm>

namespace {
using C = aurelia::Command;
using A = aurelia_ui::Action;
using P = aurelia_ui::Page;
std::filesystem::path helperRoot() { return userConfigDirectory() / L"helpers" / L"aurelia"; }
}

void Launcher::initializeAurelia() {
    aurelia_active_ = settings_.aurelia;
    aurelia_installation_ = aurelia::installed(helperRoot());
    aurelia_version_ = aurelia_installation_.version;
#ifndef LAUNCHME_UI_TEST
    aurelia_library_source_ = aurelia_active_ && findSteamRoot().empty();
    if (!aurelia_installation_.executable.empty()) aurelia_transport_ = aurelia::transport({aurelia_installation_.executable, userConfigDirectory() / L"aurelia"});
    aurelia_get_ = httpGet;
    aurelia_installer_ = [](const auto& release, const auto& cancel, const auto& progress) { return aurelia::installRelease(helperRoot(), release, cancel, progress); };
#endif
}

void Launcher::toggleAurelia() {
    settings_.aurelia = !settings_.aurelia;
    settings_dirty_ = true; settings_save_failed_ = false; settings_edited_ = std::chrono::steady_clock::now();
    saveSettings();
    aurelia_setup_status_ = settings_.aurelia == aurelia_active_ ? L"" : tr(L"Wymaga ponownego uruchomienia Launchme");
    if (settingsOpen()) buildSettingsRows();
    if (aurelia_ui_.open) aureliaRows();
    audio_.context(); InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::aureliaRequest(aurelia::Request request, std::function<void(Launcher&, aurelia::Reply)> done,
                              std::function<void(Launcher&, json::Value)> progress, std::shared_ptr<std::atomic_bool> cancel) {
    if (!aurelia_transport_) { done(*this, {false, false, {}, tr(L"Najpierw zainstaluj Aurelię")}); return; }
    const auto send = aurelia_transport_; const auto alive = alive_; const auto hwnd = hwnd_;
    const auto epoch = aurelia_epoch_; const auto generation = epoch->load();
    const auto finish = [hwnd, alive, epoch, generation, done](aurelia::Reply reply) {
        if (!alive->load() || epoch->load() != generation) return;
        auto message = std::make_unique<RemoteMessage>();
        message->run = [epoch, generation, done, reply = std::move(reply)](Launcher& app) mutable {
            if (epoch->load() == generation) done(app, std::move(reply));
        };
        if (PostMessageW(hwnd, remote_message, 0, reinterpret_cast<LPARAM>(message.get()))) message.release();
    };
    if (!work_.start([=] {
        try {
            const auto stopped = [=] { return !alive->load() || epoch->load() != generation || (cancel && cancel->load()); };
            const aurelia::Event emit = [=](const json::Value& value) {
                if (!progress || stopped()) return;
                auto message = std::make_unique<RemoteMessage>();
                message->run = [=](Launcher& app) { if (epoch->load() == generation && (!cancel || !cancel->load())) progress(app, value); };
                if (PostMessageW(hwnd, remote_message, 0, reinterpret_cast<LPARAM>(message.get()))) message.release();
            };
            finish(request.command == C::standalone ? aurelia::launchStandalone(send, request, stopped, emit) : aurelia::checked(request, send(request, stopped, emit)));
        } catch (...) { finish({false, false, {}, tr(L"Polecenie Aurelii nie powiodło się")}); }
    })) done(*this, {false, false, {}, tr(L"Nie można uruchomić zadania Aurelii")});
}

void Launcher::installAurelia() {
    if (aurelia_helper_busy_ || !aurelia_get_ || !aurelia_installer_) return;
    // Changing a daemon's binary during a transfer restarts it. The new binary is used on restart.
    aurelia_helper_busy_ = true; aurelia_setup_status_ = tr(L"Sprawdzanie najnowszej wersji…");
    aureliaRows();
    const auto alive = alive_; const auto hwnd = hwnd_; const auto get = aurelia_get_; const auto install = aurelia_installer_;
    const auto done = [alive, hwnd](aurelia::Reply reply, std::wstring version) {
        if (!alive->load()) return;
        auto message = std::make_unique<RemoteMessage>();
        message->run = [reply = std::move(reply), version = std::move(version)](Launcher& app) {
            app.aurelia_helper_busy_ = false; app.aurelia_ui_.progress = -1;
            if (reply.ok) {
                app.aurelia_installation_ = aurelia::installed(helperRoot());
                app.aurelia_version_ = version;
                app.aurelia_setup_status_ = aurelia::flag(reply.data, "already_current") ? tr(L"Aurelia jest aktualna") : tr(L"Aurelia jest zainstalowana. Uruchom ponownie Launchme, aby użyć tej wersji.");
                // A first installation can sign in now, before restarting into the enabled backend.
#ifndef LAUNCHME_UI_TEST
                if (!app.aurelia_transport_ && !app.aurelia_installation_.executable.empty())
                    app.aurelia_transport_ = aurelia::transport({app.aurelia_installation_.executable, userConfigDirectory() / L"aurelia"});
#endif
            } else app.aurelia_setup_status_ = reply.error;
            if (app.aurelia_ui_.open) app.aureliaRows();
            if (app.settingsOpen()) app.buildSettingsRows();
        };
        if (PostMessageW(hwnd, remote_message, 0, reinterpret_cast<LPARAM>(message.get()))) message.release();
    };
    if (!work_.start([=] {
        try {
            const auto reply = get(aurelia::latest_url, L"Accept: application/vnd.github+json\r\n", 2 * 1024 * 1024);
            const auto release = reply.status == 200 && !reply.truncated ? aurelia::parseRelease({reinterpret_cast<const char*>(reply.body.data()), reply.body.size()}) : std::nullopt;
            if (!release) { done({false, false, {}, tr(L"Nie znaleziono wydania Windows x64 z sumą SHA-256")}, {}); return; }
            const auto result = install(*release, [alive] { return !alive->load(); }, [alive, hwnd, last = uint64_t{}](uint64_t received, uint64_t total) mutable {
                const auto now = GetTickCount64(); if (!alive->load() || (now - last < 150 && received != total)) return; last = now;
                auto m = std::make_unique<RemoteMessage>(); m->run = [received, total](Launcher& app) {
                    app.aurelia_ui_.progress = total ? static_cast<float>(static_cast<double>(received) / total) : -1;
                    app.aurelia_setup_status_ = tr(L"Pobieranie i weryfikacja Aurelii…"); app.aureliaRows();
                };
                if (PostMessageW(hwnd, remote_message, 0, reinterpret_cast<LPARAM>(m.get()))) m.release();
            });
            done(result, release->version);
        } catch (...) { done({false, false, {}, tr(L"Nie udało się pobrać lub zweryfikować Aurelii")}, {}); }
    })) { aurelia_helper_busy_ = false; aurelia_setup_status_ = tr(L"Nie można uruchomić zadania Aurelii"); aureliaRows(); }
}

void Launcher::startAureliaLibraryWorker() {
    const auto invoke = aurelia_transport_; const auto hwnd = hwnd_; const auto alive = alive_;
    const bool source = aurelia_library_source_; const auto wake = source ? steam_rescan_ : aurelia_rescan_;
    const auto epoch = aurelia_epoch_;
    work_.start([=] {
        ChangeWatch watch; watch.wake(wake->event);
        // Not the library source: only the session (account, sign-in) is followed here, and the games the
        // account borrows through Steam Family Sharing (every 10 minutes; the Steam client has the rest).
        bool first = true;
        uint64_t family_at = 0, family_account = 0;
        while (alive->load()) {
            const auto generation = epoch->load();
            const auto cancel = [=] { return !alive->load() || epoch->load() != generation; };
            try {
                auto health = invoke ? invoke({C::health}, cancel, {}) : aurelia::Reply{false, false, {}, tr(L"Najpierw zainstaluj Aurelię")};
                if (health.ok) {
                    const auto* logged = health.data.get("logged_in");
                    if (!logged || logged->type != json::Value::Type::boolean || (logged->flag && !aurelia::integer(health.data.get("steam_id")).value_or(0))) {
                        health.ok = false; health.error = tr(L"Nieprawidłowa odpowiedź Aurelii");
                    }
                }
                const uint64_t account = health.ok ? aurelia::integer(health.data.get("steam_id")).value_or(0) : 0;
                const bool authenticated = health.ok && aurelia::flag(health.data, "logged_in");
                auto message = std::make_unique<LibraryMessage>(); message->account = account; message->refresh = !first;
                bool valid = health.ok || !invoke;
                if (account && source) {
                    const auto cache = userConfigDirectory() / L"aurelia-library";
                    auto reply = authenticated ? invoke({C::library}, cancel, {}) : aurelia::Reply{};
                    bool cached = false;
                    if (reply.ok && aurelia::library(reply.data, account)) aurelia::saveLibraryCache(cache, account, reply.data);
                    else if (const auto saved = aurelia::loadLibraryCache(cache, account)) { reply = {true, false, *saved}; cached = true; health.error = tr(L"Biblioteka Aurelii z pamięci podręcznej · offline"); }
                    auto games = reply.ok ? aurelia::library(reply.data, account) : std::nullopt;
                    if (!games) { valid = false; health.error = reply.error.empty() ? tr(L"Nieprawidłowa odpowiedź Aurelii") : reply.error; }
                    else {
                        message->games = std::move(*games);
                        if (cached) for (auto& game : message->games) {
                            std::error_code error;
                            game.installed = game.installed && game.install_directory.is_absolute() && std::filesystem::is_directory(game.install_directory, error);
                        }
                        // Local visibility/shortcuts remain useful; only the matching Steam account's
                        // data may supplement Aurelia's identity. No client IPC or Web API is used.
                        const auto root = findSteamRoot();
                        if (activeSteamId(root) == account) {
                            message->excluded = excludedSteamApps(root); message->collections = readSteamCollections(root);
                            message->shortcuts = scanSteamShortcuts(root); applyCollectionPlatforms(message->shortcuts, message->collections);
                            for (auto& g : message->games) if (message->excluded.contains(g.app_id)) { g.visibility = GameVisibility::private_game; message->private_games.push_back(g); }
                            std::erase_if(message->games, [&](const Game& g) { return message->excluded.contains(g.app_id); });
                        }
                    }
                }
                std::optional<std::vector<Game>> family;
                if (!source && authenticated && account && (account != family_account || GetTickCount64() >= family_at)) {
                    if (const auto reply = invoke({C::library}, cancel, {}); reply.ok)
                        if (auto games = aurelia::library(reply.data, account)) {
                            family.emplace();
                            for (auto& game : *games) if (game.family_shared) family->push_back(std::move(game));
                            family_account = account; family_at = GetTickCount64() + 10 * 60 * 1000;
                        }
                } else if (!source && !authenticated) { family.emplace(); family_account = 0; } // Signed out: none borrowed.
                if (cancel()) continue;
                auto snapshot = std::shared_ptr<LibraryMessage>(message.release());
                auto state = std::make_unique<RemoteMessage>();
                state->run = [account, health, authenticated, epoch, generation, snapshot, valid, source, family](Launcher& app) {
                    if (epoch->load() != generation) return;
                    const bool changed = health.ok && app.aurelia_account_ != account;
                    if (changed) {
                        ++*epoch; // Reject every reply captured under the previous account.
                        ++app.aurelia_page_generation_;
                        app.aurelia_cloud_.clear(); app.aurelia_downloads_.clear();
                        app.aurelia_operations_.clear(); app.aurelia_playing_.clear(); app.aurelia_download_poll_ = false;
                        app.aurelia_ui_.busy = false;
                        app.closeAchievements();
                        if (app.aurelia_ui_.open && app.aurelia_ui_.page != P::setup) app.closeAurelia();
                        app.closeFriendChat();
                        app.friend_chat_drafts_.clear(); // closeFriendChat has just saved the old conversation's draft.
                    }
                    if (health.ok) {
                        app.aurelia_account_ = account; app.aurelia_authenticated_ = account != 0 && authenticated;
                        app.aurelia_account_name_ = text_encoding::wideLenient(aurelia::text(health.data, "account"));
                    }
                    app.aurelia_session_known_ = health.ok;
                    // A changed borrowed list: the Steam library is read again and takes it in (onLibrary).
                    if (family) {
                        const auto ids = [](const std::vector<Game>& games) { std::set<uint32_t> result; for (const auto& g : games) result.insert(g.app_id); return result; };
                        const bool different = ids(*family) != ids(app.aurelia_family_);
                        app.aurelia_family_ = *family;
                        if (different) app.steam_rescan_->request();
                    }
                    if (app.aurelia_ui_.login == aurelia_ui::State::Login::none) app.aurelia_setup_status_ = health.error; // A sign-in's own messages stay.
                    if (source && (valid || changed)) app.onLibrary(std::make_unique<LibraryMessage>(*snapshot));
                    else if (source && app.scanning_) { app.scanning_ = false; InvalidateRect(app.hwnd_, nullptr, FALSE); }
                    if (app.aurelia_ui_.open && app.aurelia_ui_.page == P::setup) app.aureliaRows();
                };
                if (PostMessageW(hwnd, remote_message, 0, reinterpret_cast<LPARAM>(state.get()))) state.release();
                if (valid) first = false;
            } catch (...) {
                auto m = std::make_unique<RemoteMessage>(); m->run = [](Launcher& app) { app.scanning_ = false; app.aurelia_setup_status_ = tr(L"Nie udało się odświeżyć biblioteki Aurelii"); };
                if (alive->load() && PostMessageW(hwnd, remote_message, 0, reinterpret_cast<LPARAM>(m.get()))) m.release();
            }
            watch.wait(60000); wake->take(); // Explicit refresh wakes it immediately; no per-frame queries.
        }
    });
}

// The Steam library read from the Steam client (Aurelia only follows the session): the games the same
// account borrows through Steam Family Sharing join it, not installed, and the installed borrowed ones
// are marked so; they install and start through the client.
void Launcher::mergeAureliaFamily(LibraryMessage& library) const {
    if (aurelia_family_.empty() || !library.account || library.account != aurelia_account_) return;
    std::set<uint32_t> borrowed, known;
    for (const auto& game : aurelia_family_) borrowed.insert(game.app_id);
    for (auto* games : {&library.games, &library.private_games})
        for (auto& game : *games) {
            if (game.source != GameSource::steam || game.kind != GameKind::steam) continue;
            known.insert(game.app_id);
            if (borrowed.contains(game.app_id)) { game.family_shared = true; game.steam_owner = 0; } // This account plays it.
        }
    for (const auto& game : aurelia_family_) {
        if (known.contains(game.app_id) || library.excluded.contains(game.app_id)) continue;
        auto copy = game;
        copy.installed = false; copy.install_directory.clear();
        copy.account_id = std::to_string(library.account);
        library.games.push_back(std::move(copy));
    }
}

void Launcher::aureliaApplyDownloads(const json::Value& data) {
    const auto list = aurelia::downloads(data); if (!list) return;
    std::map<uint32_t, aurelia::Download> next;
    for (auto d : *list) {
        if (const auto previous = aurelia_downloads_.find(d.app); previous != aurelia_downloads_.end()) { d.speed = previous->second.speed; d.eta = previous->second.eta; }
        next.emplace(d.app, std::move(d));
    }
    // Keep a just-started operation until its terminal result, even before the daemon registers it.
    for (const auto app : aurelia_operations_) if (!next.contains(app)) {
        if (const auto old = aurelia_downloads_.find(app); old != aurelia_downloads_.end()) next.emplace(*old);
    }
    std::set<uint32_t> released;
    for (const auto& item : aurelia_downloads_) if (!next.contains(item.first)) released.insert(item.first);
    const bool completed = !released.empty();
    aurelia_downloads_ = std::move(next);
    aureliaSyncInstallations(released);
    refreshDownloads();
    if (completed) aureliaRescan();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::aureliaPoll(uint64_t now) {
    // Zamknij grę through Aurelia: `play` has not returned by the deadline, so the game still runs.
    for (auto it = aurelia_stopping_.begin(); it != aurelia_stopping_.end();) {
        if (!it->second || now < it->second) { ++it; continue; }
        const auto game = std::find_if(installed_library_.begin(), installed_library_.end(), [&](const Game& g) { return g.app_id == it->first; });
        showNotification(NotificationKind::error, tr(L"Nie można zamknąć gry"), game == installed_library_.end() ? std::wstring{} : game->name);
        it = aurelia_stopping_.erase(it);
    }
    if (!aurelia_active_ || !aurelia_transport_ || !aurelia_authenticated_) return;
    if (!aurelia_download_poll_ && now >= aurelia_poll_at_) {
        aurelia_poll_at_ = now + (aurelia_downloads_.empty() && aurelia_operations_.empty() && !downloadsOpen() ? 15000 : 1200);
        aurelia_download_poll_ = true;
        aureliaRequest({C::downloads}, [](Launcher& app, aurelia::Reply reply) {
            app.aurelia_download_poll_ = false;
            if (reply.ok) app.aureliaApplyDownloads(reply.data);
        });
    }
    if (settings_.aurelia_cloud && now >= aurelia_cloud_at_) {
        aurelia_cloud_at_ = now + 2000;
        const Game* game = screen_ == ScreenMode::details && !detail_games_.empty() ? &detail_games_[0] :
                           selected_ < cards_.size() ? &cards_[selected_].game : nullptr;
        if (game) aureliaCheckCloud(*game);
    }
}

// Aurelia's transfers over the scanned games. Only its own: a game the Steam client downloads keeps the
// progress its manifest gives (clearing it here, every poll, made the download vanish and come back,
// with a new "queued" notice each time). `released`: transfers Aurelia has just finished or dropped.
void Launcher::aureliaSyncInstallations(const std::set<uint32_t>& released) {
    if (!aurelia_active_ || !account_) return;
    LibraryMessage snapshot; snapshot.account = account_; snapshot.installation_update = true; snapshot.excluded = excluded_ids_;
    snapshot.games = installed_library_;
    for (const auto& g : private_category_.games) if (aurelia::validGame(g) && g.visibility == GameVisibility::private_game) snapshot.private_games.push_back(g);
    for (auto* games : {&snapshot.games, &snapshot.private_games}) for (auto& game : *games) {
        if (const auto found = aurelia_downloads_.find(game.app_id); found != aurelia_downloads_.end()) {
            game.installation = found->second.progress;
            if (game.installation.active()) game.installation.update = game.installed;
        } else if (released.contains(game.app_id)) game.installation = {};
    }
    updateSteamInstallations(snapshot);
}

void Launcher::aureliaCheckCloud(const Game& game, bool force) {
    if (!aureliaCloudFor(game) || !aurelia_authenticated_) return;
    auto& known = aurelia_cloud_[game.app_id];
    if (known.state == aurelia::CloudState::checking || known.state == aurelia::CloudState::syncing ||
        (!force && known.checked && GetTickCount64() - known.checked < 5 * 60 * 1000)) return;
    // A previously observed conflict/incomplete sync stays visible until the user syncs again.
    if (known.state == aurelia::CloudState::conflicts || known.state == aurelia::CloudState::incomplete || known.state == aurelia::CloudState::synced) return;
    known.state = aurelia::CloudState::checking; known.checked = GetTickCount64();
    aureliaRequest({C::cloud_list, game.app_id}, [id = game.app_id](Launcher& app, aurelia::Reply reply) {
        auto& c = app.aurelia_cloud_[id];
        if (c.state != aurelia::CloudState::checking) return; // A sync started while this listing was in flight.
        c = aurelia::cloud(reply, false); c.checked = GetTickCount64();
        if (app.aurelia_ui_.open && app.aurelia_ui_.game.app_id == id) app.aureliaRows();
        InvalidateRect(app.hwnd_, nullptr, FALSE);
    });
    if (aurelia_ui_.open && aurelia_ui_.game.app_id == game.app_id) aureliaRows();
}
std::wstring Launcher::aureliaCloudText(const Game& game) const {
    if (!aureliaCloudFor(game)) return {};
    const auto found = aurelia_cloud_.find(game.app_id);
    return aurelia::cloudLabel(found == aurelia_cloud_.end() ? aurelia::CloudState::unknown : found->second.state);
}

void Launcher::aureliaOperation(aurelia::Request request, const Game& game) {
    if (!aurelia_authenticated_ || !aurelia_active_) {
        // Said where the user is (the game's page, its cloud): why nothing happens and what to do.
        if (aurelia_ui_.open && aurelia_ui_.page != P::setup) {
            aurelia_ui_.status = !aurelia_active_ ? tr(L"Włącz rozszerzoną integrację Aurelia") :
                                 !aurelia_setup_status_.empty() && !aurelia_session_known_ ? aurelia_setup_status_ :
                                 tr(L"Najpierw zaloguj się w Aurelii: Ustawienia → Biblioteka → Steam → Zarządzaj Aurelią");
            aureliaRows(); return;
        }
        openAurelia(); return;
    }
    if (aurelia_operations_.contains(request.app) || aurelia_operations_.contains(game.app_id) || aurelia_downloads_.contains(request.app)) {
        showNotification(NotificationKind::error, L"Aurelia", tr(L"Operacja dla tej gry już trwa")); return;
    }
    aurelia_operations_.insert(request.app); aurelia_operations_.insert(game.app_id);
    const bool transfer = request.command == C::install || request.command == C::update || request.command == C::verify || request.command == C::downgrade || request.command == C::move;
    if (transfer) {
        aurelia::Download download; download.app = request.app; download.name = game.name; download.progress.phase = InstallPhase::allocating;
        download.progress.update = game.installed; aurelia_downloads_[request.app] = std::move(download);
        aureliaSyncInstallations(); refreshDownloads();
    }
    if (request.command == C::cloud_sync) aurelia_cloud_[request.app].state = aurelia::CloudState::syncing;
    aurelia_ui_.busy = true; aurelia_ui_.status = tr(L"Wykonywanie polecenia…");
    const auto page_generation = aurelia_page_generation_;
    aureliaRequest(request, [request, game, transfer, page_generation](Launcher& app, aurelia::Reply reply) {
        app.aurelia_operations_.erase(request.app); app.aurelia_operations_.erase(game.app_id);
        if (transfer) { app.aurelia_downloads_.erase(request.app); app.aureliaSyncInstallations({request.app}); app.refreshDownloads(); }
        if (request.command == C::cloud_sync) {
            auto& c = app.aurelia_cloud_[request.app]; c = aurelia::cloud(reply, true); c.checked = GetTickCount64();
            if (c.state != aurelia::CloudState::synced) { reply.ok = false; if (reply.error.empty()) reply.error = aurelia::cloudLabel(c.state); }
        }
        if (reply.ok && transfer) app.downloadFinished(game.name, &game);
        else if (!reply.ok && !reply.cancelled) app.showNotification(NotificationKind::error, L"Aurelia · " + game.name, reply.error);
        if (aurelia::flag(reply.data, "steam_restart_required")) app.showNotification(NotificationKind::launch, L"Aurelia", tr(L"Uruchom ponownie klienta Steam, aby zobaczył zmianę DLC."));
        app.aureliaRescan();
        if (app.aurelia_ui_.open && page_generation == app.aurelia_page_generation_) {
            app.aurelia_ui_.busy = false;
            if (reply.ok) app.aureliaPage(app.aurelia_ui_.page);
            else { app.aurelia_ui_.status = reply.error; app.aureliaRows(); }
        }
        InvalidateRect(app.hwnd_, nullptr, FALSE);
    }, [id = request.app, transfer](Launcher& app, json::Value event) {
        if (!transfer || aurelia::text(event, "event") != "progress") return;
        auto& d = app.aurelia_downloads_[id]; d.app = id; d.progress.live = true;
        d.progress.done = aurelia::integer(event.get("bytes_downloaded")).value_or(0);
        d.progress.total = aurelia::integer(event.get("total_bytes")).value_or(0);
        const auto phase = aurelia::text(event, "state");
        d.progress.phase = phase == "verifying" ? InstallPhase::verifying : phase == "queued" ? InstallPhase::queued : phase == "moving" ? InstallPhase::staging : InstallPhase::downloading;
        d.status = text_encoding::wideLenient(phase);
        d.speed = aurelia::integer(event.get("speed_bps")).value_or(0); d.eta = aurelia::integer(event.get("eta_seconds"));
        app.aureliaSyncInstallations(); app.refreshDownloads(); InvalidateRect(app.hwnd_, nullptr, FALSE);
    });
}

// Zamknij grę when Launchme found none of its processes yet: Aurelia ends the process it recorded
// for its `play`, at once (`stop --force`, run outside the daemon). Its reply is not the end of the
// game: `play` returning is. Still playing after the deadline: aureliaPoll says it failed.
void Launcher::aureliaStop(const Game& game) {
    if (!aurelia_transport_ || !aurelia_playing_.contains(game.app_id) || aurelia_stopping_.contains(game.app_id)) return;
    aurelia_stopping_[game.app_id] = 0;
    aurelia::Request request{C::stop, game.app_id}; request.force = true;
    aureliaRequest(request, [id = game.app_id, name = game.name](Launcher& app, aurelia::Reply reply) {
        if (!app.aurelia_stopping_.contains(id)) return; // `play` has already returned.
        if (!reply.ok) {
            app.aurelia_stopping_.erase(id);
            app.showNotification(NotificationKind::error, tr(L"Nie można zamknąć gry"), name + L" · " + reply.error);
            return;
        }
        app.aurelia_stopping_[id] = GetTickCount64() + 15000;
    });
}

void Launcher::aureliaLaunch(const Game& game) {
    if (!aurelia_authenticated_ || !aurelia_transport_) { openAurelia(); return; }
    if (aurelia_playing_.contains(game.app_id)) return;
    if (aurelia_operations_.contains(game.app_id)) { showNotification(NotificationKind::error, L"Aurelia", tr(L"Operacja dla tej gry już trwa")); return; }
    if (!game.installed) { openAurelia(&game); aurelia_library_action_ = A::install; aureliaPage(P::libraries); return; }
    if (aurelia_downloads_.contains(game.app_id)) { openAurelia(&game); return; }
    aurelia_playing_.insert(game.app_id);
    aurelia::Request request{C::standalone, game.app_id}; request.steam = false;
    // Its Steam Cloud saves: pulled before the game starts and pushed after it ends, unless turned off for it.
    request.auto_cloud = aureliaCloudEnabled() && !settings_.aurelia_cloud_skip.contains(std::to_string(game.app_id));
    // Managed only through "Domyślnie zarządzaj przez Aurelię": a game that cannot run without the
    // Steam client here goes to the client instead (said so); one chosen for Aurelia reports it.
    const bool by_default = settings_.aurelia_default;
    dismissMenu(); closeAurelia();
    showNotification(NotificationKind::launch, game.name, tr(L"Sprawdzanie uruchomienia przez Aurelię…"));
    // play waits for the game to exit. Closing Launchme only detaches its CLI; it never kills the game.
    aureliaRequest(request, [game, by_default](Launcher& app, aurelia::Reply reply) {
        app.aurelia_playing_.erase(game.app_id);
        app.aurelia_stopping_.erase(game.app_id);
        if (app.launch_artwork_key_ == gameKey(game)) app.stopLaunchAnimation();
        if (const auto* outcome = reply.data.get("cloud_result")) {
            auto& state = app.aurelia_cloud_[game.app_id]; state = aurelia::cloud({true, false, *outcome}, true); state.checked = GetTickCount64();
        } else app.aurelia_cloud_.erase(game.app_id);
        const bool to_steam = !reply.ok && !reply.cancelled && by_default && aurelia::flag(reply.data, "steam_required");
        if (!reply.ok && !reply.cancelled && !to_steam) {
            app.showNotification(NotificationKind::error, aurelia::flag(reply.data, "launch_finished") ? tr(L"Synchronizacja zapisów nie powiodła się") : tr(L"Nie udało się uruchomić gry"), reply.error);
            if (reply.data.get("cloud_result") && app.topModal() == Modal::none) { app.openAurelia(&game); app.aureliaPage(P::cloud); }
        }
        // The CLI has ended. Let the normal activity observer account for any actually running
        // process, but a rejected preflight must not leave a phantom starting session behind.
        const auto key = gameKey(game);
        if (const auto it = app.game_sessions_.find(key); it != app.game_sessions_.end() && !it->second.running()) {
            app.game_sessions_.erase(it);
            app.launched_here_.erase(key);
            std::lock_guard lock(app.game_activity_jobs_->mutex); app.game_activity_jobs_->active.erase(key);
        }
        if (!app.anyActiveSession()) app.audio_.resumeMusic();
        app.aureliaRescan();
        if (to_steam) {
            app.showNotification(NotificationKind::launch, game.name, tr(L"Uruchamianie przez klienta Steam"));
            app.launchGame(game, true);
        }
    }, [game](Launcher& app, json::Value event) {
        const auto kind = aurelia::text(event, "event");
        if (kind == "launchme-cloud-down" || kind == "launchme-cloud-up") {
            app.aurelia_cloud_[game.app_id].state = aurelia::CloudState::syncing;
        } else if (kind == "launchme-starting") {
            app.startLaunchAnimation(game); app.audio_.launch(); app.audio_.stopMusic(); app.beginGameSession(game, default_focus_ms);
            app.aurelia_cloud_.erase(game.app_id); // The game may change its saves.
        }
        InvalidateRect(app.hwnd_, nullptr, FALSE);
    });
}
