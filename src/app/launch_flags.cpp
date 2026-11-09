// Opcje uruchamiania's switches of any game (LibraryState::launch_flags): "Zapisuj logi" begins a
// log per launch in logs/games/<slug> (src/library/game_log.hpp), "Włącz HDR na czas gry" (Windows)
// switches HDR on for the display Launchme is on before the game starts and back off once the last
// such game has ended. A display already in HDR is left as it is.
#include "launcher.hpp"
#include "../library/game_log.hpp"
#include "../platform/diagnostics.hpp"

#include <algorithm>

namespace {
std::wstring duration(uint64_t ms) {
    const uint64_t minutes = ms / 60000;
    if (minutes >= 60) return std::to_wstring(minutes / 60) + L" h " + std::to_wstring(minutes % 60) + L" min";
    if (minutes) return std::to_wstring(minutes) + L" min";
    return std::to_wstring(ms / 1000) + L" s";
}
}

unsigned Launcher::launchFlags(const Game& game) const {
    const auto found = library_state_.launch_flags.find(gameKey(game));
    return found == library_state_.launch_flags.end() ? 0u : found->second;
}

void Launcher::setLaunchFlags(const std::string& key, unsigned flags) {
    if (key.empty()) return;
    flags &= launch_flag::all;
#ifndef _WIN32
    flags &= ~launch_flag::hdr; // Windows only.
#endif
    const auto found = library_state_.launch_flags.find(key);
    if ((found == library_state_.launch_flags.end() ? 0u : found->second) == flags) return;
    if (flags) library_state_.launch_flags[key] = flags; else library_state_.launch_flags.erase(key);
    library_state_dirty_ = true;
    saveLibraryPreferences();
}

std::filesystem::path Launcher::launchLog(const std::string& key) const {
    const auto found = launch_runs_.find(key);
    return found == launch_runs_.end() ? std::filesystem::path{} : found->second.log;
}

void Launcher::launchFlagsNote(const std::string& key, std::wstring_view line) {
    if (const auto log = launchLog(key); !log.empty()) game_log::write(log, line);
}

void Launcher::launchFlagsBegin(const Game& game, const GameAction* action) {
    const auto key = gameKey(game);
    const unsigned flags = launchFlags(game);
    if (key.empty() || !flags) return;
    if (launch_runs_.contains(key)) launchFlagsEnd(key, L"Launched again before the previous launch ended");
    LaunchRun run;
    run.started = GetTickCount64();
    if (flags & launch_flag::log) {
        run.log = game_log::begin(userConfigDirectory(), game.name, key);
        if (run.log.empty()) diagnostics::log("warning", L"Game log could not be created for " + game.name);
    }
    launch_runs_[key] = run;
    const auto note = [&](std::wstring_view line) { launchFlagsNote(key, line); };
    note(L"Launchme " + diagnostics::version() + L": " + game.name + L" (" + text_encoding::wideLenient(key) + L", " + sourceName(game.source) + L")");
    if (action) {
        note(L"Target: " + action->target);
        if (!action->arguments.empty()) note(L"Arguments: " + action->arguments);
        if (!action->working_directory.empty()) note(L"Working directory: " + action->working_directory.wstring());
        for (const auto& [name, value] : action->environment) note(L"Environment: " + name);
    }
#ifdef _WIN32
    if (flags & launch_flag::hdr) {
        const auto display = hdr_.query(hwnd_);
        if (!display) note(L"HDR: the display's state could not be read");
        else if (!display->supported) {
            note(L"HDR: " + display->device + L" does not support HDR");
            showNotification(NotificationKind::error, tr(L"HDR niedostępny"), game.name + L" · " + tr(L"Ten ekran nie obsługuje HDR."));
        } else if (display->enabled) note(L"HDR: already on (" + display->device + L")");
        else if (hdr_.set(display->device, true)) {
            hdr_device_ = display->device;
            launch_runs_[key].hdr = true;
            note(L"HDR: switched on for " + display->device);
            diagnostics::log("info", L"HDR on for " + game.name + L" (" + display->device + L")");
        } else {
            note(L"HDR: Windows refused to switch it on");
            diagnostics::log("warning", L"HDR could not be switched on for " + game.name);
        }
    }
#endif
}

void Launcher::launchFlagsEnd(const std::string& key, std::wstring_view how) {
    const auto found = launch_runs_.find(key);
    if (found == launch_runs_.end()) return;
    const auto run = found->second;
    launchFlagsNote(key, std::wstring(how) + L" (" + duration(GetTickCount64() - run.started) + L" after the launch)");
    launch_runs_.erase(found);
    if (!run.hdr || hdr_device_.empty()) return;
    // Another game that wanted HDR still runs: it stays on.
    if (std::any_of(launch_runs_.begin(), launch_runs_.end(), [](const auto& item) { return item.second.hdr; })) return;
    const bool off = hdr_.set(hdr_device_, false);
    if (!run.log.empty()) game_log::write(run.log, off ? L"HDR: switched back off" : L"HDR: could not be switched back off");
    diagnostics::log(off ? "info" : "warning", off ? L"HDR off after the game" : L"HDR could not be switched back off");
    hdr_device_.clear();
}
