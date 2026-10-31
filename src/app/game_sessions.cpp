#include "launcher.hpp"
#include "../platform/unix_time.hpp"
#include "../platform/game_activity.hpp"
#ifndef _WIN32
#include "../library/steam_registry.hpp"
#endif

#include <algorithm>
#include <cwctype>
#include <iterator>

void Launcher::initializeGameSessions() {
    work_.start([window = hwnd_, alive = alive_, queue = game_activity_jobs_] {
        // Per-game process handles survive between polls: a running game costs a
        // kernel wait, not a system-wide snapshot every 500 ms.
        std::map<std::string, ProcessWatch> watches;
        while (alive->load()) {
            std::map<std::string, TrackedGame> active; uint32_t generation = 0;
            std::string focus_key;
            uint64_t focus_id = 0, focus_until = 0;
            {
                std::unique_lock lock(queue->mutex);
                queue->wake.wait(lock, [&] { return !alive->load() || !queue->active.empty(); });
                if (!alive->load()) return;
                active = queue->active; generation = queue->generation.load();
                focus_key = queue->focus_key; focus_id = queue->focus_id; focus_until = queue->focus_until;
            }
            // One failed read (a process gone mid-query, a denied handle) must not end session
            // tracking for the rest of the run: music would stay off and play time stop counting.
            try {
                std::erase_if(watches, [&](const auto& item) { return !active.contains(item.first); });
                auto message = std::make_unique<GameActivityMessage>(); message->generation = generation;
                for (const auto& [key, tracked] : active) {
                    if (!alive->load() || generation != queue->generation.load()) break;
                    // A suspended game still exists; its exact processes decide when it ends.
                    auto activity = tracked.processes.empty() ? watches[key].read(tracked.game) :
                        process_freeze::alive(tracked.processes) ? GameActivity::running : GameActivity::stopped;
                    // Started here as a program or script: its process tree also proves the game
                    // running, and its end ends a session path telemetry could not judge.
                    if (tracked.job && activity != GameActivity::running) {
                        const auto launched = readLaunchJob(tracked.job);
                        if (launched == GameActivity::running || activity == GameActivity::unknown) activity = launched;
                    }
                    message->activities.emplace_back(key, activity);
                    if (key == focus_key && GetTickCount64() < focus_until) {
                        message->focus_request = focus_id;
                        message->focus_processes = tracked.processes.empty() ? findGameProcesses(tracked.game) : tracked.processes;
                    }
                }
                message->observed_at = GetTickCount64();
                if (alive->load() && generation == queue->generation.load() && PostMessageW(window, game_activity_message, 0, reinterpret_cast<LPARAM>(message.get()))) message.release();
            } catch (...) {
                diagnostics::log("error", "Game activity read failed: " + diagnostics::describe(std::current_exception()));
                watches.clear();
            }
            std::unique_lock lock(queue->mutex);
            queue->wake.wait_for(lock, std::chrono::milliseconds(500), [&] { return !alive->load() || queue->generation.load() != generation; });
        }
    });
    // Games started elsewhere (Steam, a store client, before the launcher) are looked for when
    // something can have started one, never on a timer: another window came to the foreground
    // (a starting game takes it; the WinEvent hook in main.cpp), Steam's per-game registry
    // (Apps\<id>\Running) changed, the launcher got focus back, or the library changed.
    // One shared process snapshot covers all stores, including Steam EXEs started directly.
    // A result is posted when it changes and while games run (a session that ended in a relaunch
    // gap is taken up again).
    work_.start([alive = alive_, queue = running_scan_, wake = steam_apps_wake_] {
        ChangeWatch watch;
        watch.wake(wake->event);
#ifdef _WIN32
        const auto arm = [&] { watch.clear(); watch.registry(HKEY_CURRENT_USER, L"Software\\Valve\\Steam\\Apps"); };
#else
        // Steam keeps Apps/<id>/Running in ~/.steam/registry.vdf, rewritten as games start and end.
        const auto arm = [&] { watch.clear(); watch.directory(steam_registry::file().parent_path()); };
#endif
        arm();
        while (alive->load()) {
            // The key appears when Steam first runs a game; look for it again every few minutes until then.
            const auto result = watch.wait(watch.empty() ? 300000 : INFINITE);
            if (!alive->load()) return;
            if (watch.empty()) { arm(); continue; }
            if (result != ChangeWatch::Result::changed) continue;
            watch.settle(300, 2000);
            std::lock_guard lock(queue->mutex);
            queue->poke = true;
            queue->wake.notify_one();
        }
    });
    work_.start([window = hwnd_, alive = alive_, queue = running_scan_] {
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL); // Yield CPU to the game, never scan on UI.
        std::vector<Game> candidates;
        uint32_t version = 0;
        std::vector<std::string> reported;
        bool report = false;
        while (alive->load()) {
            {
                std::unique_lock lock(queue->mutex);
                queue->wake.wait(lock, [&] { return !alive->load() || queue->version; });
                if (!alive->load()) return;
                if (queue->version != version) { candidates = queue->candidates; version = queue->version; report = true; }
                queue->poke = false;
            }
            try {
                auto message = std::make_unique<RunningGamesMessage>();
                message->version = version;
                std::vector<std::string> keys;
                for (const auto index : findRunningGames(candidates)) {
                    message->games.push_back(candidates[index]);
                    keys.push_back(gameKey(candidates[index]));
                }
                std::sort(keys.begin(), keys.end());
                if ((report || !keys.empty() || keys != reported) && alive->load() &&
                    PostMessageW(window, running_games_message, 0, reinterpret_cast<LPARAM>(message.get()))) {
                    message.release(); reported = std::move(keys); report = false;
                }
            } catch (...) { diagnostics::log("error", "Running games scan failed: " + diagnostics::describe(std::current_exception())); }
            std::unique_lock lock(queue->mutex);
            queue->wake.wait(lock, [&] { return !alive->load() || queue->poke || queue->version != version; });
        }
    });
}

// EVENT_SYSTEM_FOREGROUND (main.cpp): another window came to the front. Bursts (Alt+Tab
// cycling, a splash screen then the game) become one scan a second later.
void Launcher::foregroundChanged(HWND window) {
    foreground_seen_ = window; foreground_seen_at_ = GetTickCount64();
    if (closing_ || !runtime_started_) return;
    const auto now = GetTickCount64();
    if (now < foreground_scan_at_) return;
    foreground_scan_at_ = now + 1000;
    SetTimer(hwnd_, foreground_scan_timer, 1000, nullptr);
}

void Launcher::foregroundScanDue() {
    KillTimer(hwnd_, foreground_scan_timer);
    pokeRunningScan();
}

void Launcher::updateRunningCandidates() {
    std::vector<Game> candidates;
    std::vector<std::string> keys;
    std::unordered_set<std::string> listed; // Ctrl+H copies the private games into public_library_ too.
    const auto add = [&](const Game& game) {
        // findRunningGames checks the rest; this only keeps the copy small.
        if (!isGame(game) || !game.installed || game.installation.active() || game.kind == GameKind::nonsteam || game.kind == GameKind::catalog) return;
        if (game.source != GameSource::steam && !game.executable.is_absolute() && !game.install_directory.is_absolute()) return;
        auto key = gameKey(game);
        if (key.empty() || !listed.insert(key).second) return;
        candidates.push_back(game); keys.push_back(std::move(key));
    };
    for (const auto& game : public_library_) add(game);
    for (const auto& game : private_category_.games) add(game);
    std::lock_guard lock(running_scan_->mutex);
    // An unchanged ID can point to a moved/edited installation. The snapshot
    // also supplies names/privacy/account data when an external game is adopted.
    // Play-history updates are deliberately excluded: adopting a session must
    // not invalidate the scan merely because it updated last_played.
    const auto same = [](const Game& a, const Game& b) {
        return a.executable == b.executable && a.install_directory == b.install_directory &&
            a.name == b.name && a.visibility == b.visibility && a.account_id == b.account_id;
    };
    if (keys == running_candidate_keys_ && std::equal(candidates.begin(), candidates.end(),
        running_scan_->candidates.begin(), running_scan_->candidates.end(), same)) return;
    running_candidate_keys_ = std::move(keys);
    overlay_monitor_.candidates(candidates);
    running_scan_->candidates = std::move(candidates);
    if (!++running_scan_->version) ++running_scan_->version; // 0 means "no library yet".
    running_version_ = running_scan_->version;
    running_scan_->wake.notify_one();
}

void Launcher::pokeRunningScan() {
    std::lock_guard lock(running_scan_->mutex);
    running_scan_->poke = true;
    running_scan_->wake.notify_one();
}

void Launcher::onRunningGames(std::unique_ptr<RunningGamesMessage> message) {
    if (message->version != running_version_) return;
    const auto now = GetTickCount64();
    bool adopted = false, history_changed = false;
    std::vector<Game> in_play;
    {
        std::lock_guard lock(game_activity_jobs_->mutex);
        for (const auto& [key, tracked] : game_activity_jobs_->active)
            if (game_sessions_.contains(key)) in_play.push_back(tracked.game);
    }
    for (const auto& game : message->games) {
        const auto key = gameKey(game);
        // Started here (or already taken up): its own session goes on.
        if (key.empty() || game_sessions_.contains(key)) continue;
        // Another entry for the same installation (e.g. "Moje gry" pointing to a GOG game's
        // .exe) is the one in play: its process is no second game, nor its play history.
        if (std::any_of(in_play.begin(), in_play.end(), [&](const Game& other) { return sharesProcesses(game, other); })) continue;
        in_play.push_back(game);
        // Already running: no start-up grace period and no "not confirmed" notice.
        game_sessions_.try_emplace(key, now).first->second.observe(GameActivity::running, now);
        trackSession(key, game, {});
        ensurePluginPlaytime(game); // Sample Steam before the user opens the widget later in this session.
        companionSessionStarted(game, false);
        const auto history_key = recent_games::historyKey(game, account_, gog_state_.account, epic_state_.account);
        if (!history_key.empty() && game.visibility == GameVisibility::public_game) {
            library_state_.last_played[history_key] = unix_time::now();
            library_state_dirty_ = history_changed = true;
        }
        adopted = true;
    }
    if (!adopted) return;
    audio_.stopMusic(); // As for a game started here; it comes back after the last one ends.
    if (history_changed || !categories_.empty()) refreshRecentGames(); // Games in play lead Recent.
    InvalidateRect(hwnd_, nullptr, FALSE);
}
void Launcher::beginGameSession(const Game& game, uint64_t focus_ms, std::shared_ptr<void> job) {
    const auto key = gameKey(game);
    game_sessions_.try_emplace(key, GetTickCount64());
    launched_here_.insert(key); // Its end brings the launcher back (returnFromGame).
    audio_.stopMusic();
    trackSession(key, game, {}, std::move(job));
    // Steam shortcuts can hand off through PowerShell/EmuDeck. The script is not the
    // game; the watch latches the emulator (ROM on its command line, or a process
    // that appears after this launch) and counts playtime until that process exits.
    if (game.kind == GameKind::nonsteam && game.visibility == GameVisibility::public_game) {
        const auto history_key = recent_games::historyKey(game, account_, gog_state_.account, epic_state_.account);
        if (!history_key.empty()) {
            library_state_.last_played[history_key] = unix_time::now();
            library_state_dirty_ = true;
            saveLibraryPreferences();
        }
    }
    ensurePluginPlaytime(game);
    companionSessionStarted(game, true);
    requestGameFocus(game, focus_ms);
}

// Bounded handoff for delayed game windows (Steam/client splash screens). Stop
// as soon as the user switches apps, interacts with Launchme, opens Guide, or
// the game receives focus. External-game discovery never requests activation.
void Launcher::requestGameFocus(const Game& game, uint64_t focus_ms) {
    launch_focus_key_ = gameKey(game);
    launch_focus_until_ = GetTickCount64() + focus_ms;
    if (!++launch_focus_id_) ++launch_focus_id_;
    {
        std::lock_guard lock(game_activity_jobs_->mutex);
        game_activity_jobs_->focus_key = launch_focus_key_;
        game_activity_jobs_->focus_id = launch_focus_id_;
        game_activity_jobs_->focus_until = launch_focus_until_;
    }
    game_activity_jobs_->wake.notify_one();
}
void Launcher::cancelGameFocus() {
    if (launch_focus_key_.empty()) return;
    launch_focus_key_.clear(); launch_focus_until_ = 0;
    std::lock_guard lock(game_activity_jobs_->mutex);
    game_activity_jobs_->focus_key.clear(); game_activity_jobs_->focus_until = 0;
}
void Launcher::pollGameFocus() {
    if (launch_focus_key_.empty()) return;
    const auto foreground = overlay_backend_.foreground();
    if (GetTickCount64() >= launch_focus_until_ || gameOverlayOpen() || (foreground && foreground != hwnd_)) cancelGameFocus();
}
void Launcher::applyGameFocus(const GameActivityMessage& message) {
    pollGameFocus();
    if (launch_focus_key_.empty() || message.focus_request != launch_focus_id_ || message.focus_processes.empty()) return;
    const auto session = game_sessions_.find(launch_focus_key_);
    if (session == game_sessions_.end() || session->second.frozen()) { cancelGameFocus(); return; }
    if (overlay_backend_.foreground() == hwnd_ && quick_resume_.focus(message.focus_processes)) cancelGameFocus();
}
void Launcher::trackSession(const std::string& key, const Game& game, std::vector<GameProcess> processes, std::shared_ptr<void> job) {
    std::lock_guard lock(game_activity_jobs_->mutex);
    const auto previous = game_activity_jobs_->active.find(key);
    auto history_key = previous == game_activity_jobs_->active.end() ? recent_games::historyKey(game, account_, gog_state_.account, epic_state_.account) : previous->second.history_key;
    if (!job && previous != game_activity_jobs_->active.end()) job = previous->second.job; // Kept through a suspend/resume.
    game_activity_jobs_->active[key] = TrackedGame{game, std::move(processes), std::move(history_key), std::move(job)};
    game_activity_jobs_->generation.fetch_add(1); game_activity_jobs_->wake.notify_one();
}
void Launcher::onGameActivity(std::unique_ptr<GameActivityMessage> message) {
    if (message->generation != game_activity_jobs_->generation.load()) return;
    std::vector<std::string> finished;
    std::vector<std::pair<Game, uint64_t>> played; // Ended sessions and their play time, for the companion.
    bool frozen_finished = false;
    bool history_changed = false;
    bool state_changed = false; // "W grze" and its buttons: only a change repaints.
    for (const auto& [key, activity] : message->activities) {
        const auto found = game_sessions_.find(key);
        if (found == game_sessions_.end()) continue;
        const bool was_running = found->second.running();
        // The play journal (the profile's week) hears of every public game played here, Steam's too.
        std::optional<std::wstring> journal_name;
        if (activity == GameActivity::running && !found->second.running()) {
            std::lock_guard lock(game_activity_jobs_->mutex);
            const auto tracked = game_activity_jobs_->active.find(key);
            if (tracked != game_activity_jobs_->active.end() && !tracked->second.history_key.empty() &&
                tracked->second.game.visibility == GameVisibility::public_game) {
                // A client handoff, installer or failed start is not playtime.
                library_state_.last_played[tracked->second.history_key] = unix_time::now();
                library_state_dirty_ = history_changed = true;
                journal_name = tracked->second.game.name;
            }
        }
        if (journal_name) journalLaunched(key, *journal_name);
        const auto result = found->second.observe(activity, message->observed_at);
        state_changed |= was_running != found->second.running();
        if (const auto elapsed = found->second.takePlaytime()) {
            std::optional<std::wstring> played_name;
            {
                std::lock_guard lock(game_activity_jobs_->mutex);
                const auto tracked = game_activity_jobs_->active.find(key);
                if (tracked != game_activity_jobs_->active.end() && tracked->second.game.visibility == GameVisibility::public_game) {
                    played_name = tracked->second.game.name;
                    if (!tracked->second.history_key.empty() && !steamArtwork(tracked->second.game)) {
                        auto& total = library_state_.playtime_ms[tracked->second.history_key];
                        total += std::min(elapsed, UINT64_MAX - total);
                        playtime_dirty_ = true;
                    }
                }
            }
            if (played_name) journalPlayed(key, *played_name, elapsed);
        }
        if (result == SessionResult::waiting) continue;
        if (result == SessionResult::start_failed) {
            std::wstring name;
            {
                std::lock_guard lock(game_activity_jobs_->mutex);
                if (const auto tracked = game_activity_jobs_->active.find(key); tracked != game_activity_jobs_->active.end()) name = tracked->second.game.name;
            }
            showNotification(NotificationKind::error, tr(L"Nie potwierdzono uruchomienia gry"),
                (name.empty() ? std::wstring{} : name + L" · ") + tr(L"Sprawdź okno klienta sklepu; Graj spróbuje ponownie."));
        } else {
            std::lock_guard lock(game_activity_jobs_->mutex);
            if (const auto tracked = game_activity_jobs_->active.find(key); tracked != game_activity_jobs_->active.end())
                played.emplace_back(tracked->second.game, found->second.playedMs());
        }
        frozen_finished = frozen_finished || found->second.frozen();
        finished.push_back(key); game_sessions_.erase(found);
    }
    // An ended session no longer pins its game to the front of Recent.
    if (history_changed || (!finished.empty() && !categories_.empty())) refreshRecentGames();
    if (playtime_dirty_ && (!finished.empty() || message->observed_at >= next_playtime_save_)) {
        library_state_dirty_ = true; playtime_dirty_ = false;
        next_playtime_save_ = message->observed_at + 30000;
    }
    if (journal_dirty_ && (!finished.empty() || message->observed_at >= next_journal_save_)) {
        saveJournal();
        next_journal_save_ = message->observed_at + 60000;
    }
    // "W grze" badges change here; the idle launcher no longer repaints on its own, and a game
    // that simply keeps running costs no repaint of the launcher behind it.
    invalidateGameOverlay(); // Live playtime comparisons while the Guide drawer is visible.
    applyGameFocus(*message);
    // While a game runs (not suspended) the computer neither sleeps nor blanks its screen: a pad's
    // input is not the system's idle input.
    const bool playing = std::any_of(game_sessions_.begin(), game_sessions_.end(),
                                     [](const auto& entry) { return entry.second.running() && !entry.second.frozen(); });
    if (playing != sleep_inhibited_) {
        sleep_inhibited_ = playing;
        SetThreadExecutionState(playing ? ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED : ES_CONTINUOUS);
    }
    if (state_changed || !finished.empty() || history_changed) InvalidateRect(hwnd_, nullptr, FALSE);
    if (finished.empty()) return;
    // The started game ended (or never came): its picture, resting behind it, goes with it.
    if (launchAnimationActive() && std::find(finished.begin(), finished.end(), launch_artwork_key_) != finished.end()) endLaunchAnimation();
    {
        std::lock_guard lock(game_activity_jobs_->mutex);
        for (const auto& key : finished) game_activity_jobs_->active.erase(key);
        game_activity_jobs_->generation.fetch_add(1); game_activity_jobs_->wake.notify_one();
    }
    if (frozen_finished) persistFrozenGames();
    for (const auto& [game, ms] : played) companionSessionEnded(game, ms);
    // Kopie zapisów: Ludusavi backs up what the game just saved (quietly when it knows no saves).
    if (settings_.save_backup_after_game)
        for (const auto& [game, ms] : played) if (ms >= 60 * 1000) gameSaves(game, false, true);
    // The index learns the new play time and achievements once Steam has written them.
    for (const auto& [game, ms] : played) game_index_later_.push_back(gameKey(game));
    game_index_later_at_ = GetTickCount64() + 8000;
    // Alt+Tab has no role here. Resume only after the last observed game stops,
    // and never between dispatching two games in quick succession. A suspended
    // game makes no sound, so it does not keep the menu music off.
    const bool another_play = pending_launch_ && primaryGameAction(pending_game_).kind == ActionKind::play;
    bool returned = false;
    for (const auto& key : finished) returned |= launched_here_.erase(key) > 0;
    if (!anyActiveSession() && !another_play) {
        audio_.resumeMusic();
        // The last game started here has ended: the launcher comes back, as a console's home
        // screen does, instead of leaving a pad user in the store client that started it.
        // Hidden in the tray while it ran: the window is back in the taskbar either way.
        if (returned) returnFromGame(message->observed_at);
        else leaveTray();
    }
}

namespace {
// What a game leaves in front when it ends: the store client that started it, the desktop, a
// crash reporter, or nothing at all.
bool leftByGame(HWND window) {
    if (!window) return true;
#ifdef _WIN32
    wchar_t name[256]{};
    if (GetClassNameW(window, name, static_cast<int>(std::size(name))) &&
        (std::wstring_view(name) == L"Progman" || std::wstring_view(name) == L"WorkerW" || std::wstring_view(name) == L"Shell_TrayWnd")) return true;
    DWORD pid = 0;
    GetWindowThreadProcessId(window, &pid);
    const HANDLE process = pid ? OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid) : nullptr;
    wchar_t path[MAX_PATH * 2]{}; DWORD size = static_cast<DWORD>(std::size(path));
    const bool known = process && QueryFullProcessImageNameW(process, 0, path, &size);
    if (process) CloseHandle(process);
    if (!known) return false;
    auto file = std::filesystem::path(path).filename().wstring();
    std::transform(file.begin(), file.end(), file.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    static constexpr std::wstring_view clients[]{L"steam.exe", L"steamwebhelper.exe", L"galaxyclient.exe", L"battle.net.exe",
        L"epicgameslauncher.exe", L"ubisoftconnect.exe", L"upc.exe", L"eadesktop.exe", L"xboxpcapp.exe", L"xboxpcappce.exe",
        L"explorer.exe", L"crashreportclient.exe", L"unitycrashhandler64.exe", L"werfault.exe"};
    return std::find(std::begin(clients), std::end(clients), file) != std::end(clients);
#else
    return false;
#endif
}

#ifndef LAUNCHME_UI_TEST
// SetForegroundWindow is refused to a background process unless, among others, it provided the
// last input or nothing holds the foreground; a refusal only flashes the taskbar button. Joining
// the foreground thread's input covers a window in front; when nothing is in front, or the lock
// after the user's last keypress in the game still holds, an empty mouse event (no move, no
// button: nothing happens) makes this process the last input's provider (as PowerToys Run does).
bool takeForegroundWindow(HWND window) {
    const auto attempt = [window] {
        const HWND front = GetForegroundWindow();
        if (front == window) return true;
        const DWORD self = GetCurrentThreadId();
        const DWORD other = front ? GetWindowThreadProcessId(front, nullptr) : 0;
        const bool attached = other && other != self && AttachThreadInput(self, other, TRUE);
        SetForegroundWindow(window);
        BringWindowToTop(window);
        if (attached) AttachThreadInput(self, other, FALSE);
        return GetForegroundWindow() == window;
    };
    if (attempt()) return true;
#ifdef _WIN32
    INPUT input{};
    input.type = INPUT_MOUSE;
    return SendInput(1, &input, sizeof(input)) == 1 && attempt();
#else
    return false;
#endif
}
#endif
}

void Launcher::returnFromGame([[maybe_unused]] uint64_t ended_at) { // Linux: other programs are never seen.
    if (closing_ || !hwnd_) return;
    const HWND front = frontWindow();
    if (front == hwnd_) { leaveTray(); return; }
#ifndef _WIN32
    // Linux: the display server names only the launcher's own windows (another one of them,
    // a sign-in page say, has the user's attention); other programs answer nullptr.
    if (front) { leaveTray(); return; }
#else
    if (!leftByGame(front)) {
        // Another program in front. Hidden in the tray, the launcher could not be the window Windows
        // activates when the game's goes away: the next one in line was (a browser, a terminal), just
        // as the game ended. One in front since well before the end, or not seen arriving (the
        // launcher's own windows, a sign-in page say), is the user's: it keeps the screen.
        DWORD pid = 0;
        GetWindowThreadProcessId(front, &pid);
        const bool arrived_with_end = front == foreground_seen_ && foreground_seen_at_ + game_exit_window_ms >= ended_at;
        if (pid == GetCurrentProcessId() || !arrived_with_end) { leaveTray(front); return; }
    }
#endif
    bringToFront();
}

bool Launcher::takeForeground() {
    if (take_foreground_) return take_foreground_(hwnd_);
#ifdef LAUNCHME_UI_TEST
    return frontWindow() == hwnd_; // UI tests never take the user's real foreground.
#else
    return takeForegroundWindow(hwnd_);
#endif
}

void Launcher::bringToFront() {
    if (closing_ || !hwnd_) return;
    leaveTray(); // Hidden while a game runs (src/app/tray.cpp): shown first.
    const HWND front = frontWindow();
    if (front == hwnd_ && !IsIconic(hwnd_)) { return_focus_until_ = 0; return; }
    if (IsIconic(hwnd_)) ShowWindow(hwnd_, SW_RESTORE);
    if (takeForeground()) return_focus_until_ = 0;
    else {
        // Refused (or the next window's activation still under way): tried again for a moment.
        const auto now = GetTickCount64();
        return_focus_from_ = front;
        return_focus_until_ = now + return_focus_ms;
        return_focus_next_ = now + return_focus_retry_ms;
    }
    if (coversMonitor()) displayChanged(); // A game may have left the display in another mode.
    InvalidateRect(hwnd_, nullptr, FALSE);
}

// Each tick while a refused return lasts: another attempt every 250 ms, until the launcher holds
// the foreground. It gives up after return_focus_ms, when a game starts, or when another program
// comes forward (the user went there) instead of what the game left.
void Launcher::pollReturnFocus() {
    if (!return_focus_until_) return;
    const auto now = GetTickCount64();
    const HWND front = frontWindow();
    if (closing_ || !hwnd_ || front == hwnd_ || now >= return_focus_until_ || pending_launch_ || anyActiveSession() ||
        (front != return_focus_from_ && !leftByGame(front))) {
        return_focus_until_ = 0;
        return;
    }
    if (now < return_focus_next_) return;
    return_focus_next_ = now + return_focus_retry_ms;
    if (takeForeground()) {
        return_focus_until_ = 0;
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

// Czas gry as shown everywhere: Steam's own count for Steam games (unknown until read), else the
// larger of Launchme's count and the store's (a launcher like Prism counts every session too);
// "Edytuj metadane" sets a value counted on from there.
std::optional<uint32_t> Launcher::automaticPlaytime(const Game& game, std::optional<uint32_t> steam) const {
    return steamArtwork(game) ? steam : std::optional<uint32_t>(std::max(localPlaytime(game), game.store_playtime_minutes));
}
std::optional<uint32_t> Launcher::gamePlaytime(const Game& game, std::optional<uint32_t> steam) const {
    const auto automatic = automaticPlaytime(game, steam);
    if (const auto* own = metadataOverride(game); own && own->playtime) return metadata::playtime(*own, automatic.value_or(own->playtime_base));
    return automatic;
}

uint32_t Launcher::localPlaytime(const Game& game) const {
    const auto key = recent_games::historyKey(game, account_, gog_state_.account, epic_state_.account);
    const auto found = library_state_.playtime_ms.find(key);
    return found == library_state_.playtime_ms.end() ? 0 : static_cast<uint32_t>(std::min<uint64_t>(UINT32_MAX, found->second / 60000));
}
