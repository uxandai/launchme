#pragma once
// The injectable backends of Launcher: UI tests replace them, so they never touch real processes,
// a real Discord client or the user's foreground window.
#include "../model/frozen_games.hpp"
#include "../model/game.hpp"
#include "../platform/discord.hpp"
#include "../platform/game_activity.hpp"
#include "../platform/game_overlay_monitor.hpp"
#include "../platform/hdr_display.hpp"
#include "../platform/process_freeze.hpp"
#include "../ui/overlay_window.hpp"

#include <filesystem>
#include <functional>
#include <optional>
#include <vector>

// Experimental quick resume. Injectable, so UI tests never touch real processes.
struct QuickResumeBackend {
    std::function<std::vector<GameProcess>(const Game&)> find = findGameProcesses;
    std::function<process_freeze::Result(std::vector<GameProcess>&)> suspend = process_freeze::suspend;
    std::function<process_freeze::Result(const std::vector<GameProcess>&)> resume = process_freeze::resume;
    std::function<process_freeze::Result(const std::vector<GameProcess>&)> terminate = process_freeze::terminate;
    std::function<bool(const std::vector<GameProcess>&)> alive = process_freeze::alive;
    std::function<bool(const std::vector<GameProcess>&)> focus = process_freeze::focus;
    std::function<bool(const std::filesystem::path&, const std::vector<FrozenGame>&)> save = process_freeze::save;
};

// Windows HDR for the games that switch it on (Opcje uruchamiania). UI tests replace it: they never
// change a real display.
struct HdrBackend {
#ifdef LAUNCHME_UI_TEST
    std::function<std::optional<hdr_display::State>(HWND)> query = [](HWND) { return std::optional<hdr_display::State>{}; };
    std::function<bool(const std::wstring&, bool)> set = [](const std::wstring&, bool) { return false; };
#else
    std::function<std::optional<hdr_display::State>(HWND)> query = [](HWND window) { return hdr_display::query(window); };
    std::function<bool(const std::wstring&, bool)> set = hdr_display::set;
#endif
};

// Discord dashboard tile: launch/focus only. Injectable, so UI tests never touch a real client.
// `running` is called on the dashboard worker; `focus` and `locate` on the UI thread.
struct DiscordBackend {
    std::function<bool()> running = discord::running;
    std::function<bool()> focus = discord::focus;
    std::function<std::optional<GameAction>()> locate = discord::launchAction;
};

// Window actions are injectable: UI regression tests never steal the user's
// foreground, activate a real game, or start performance tracing.
struct GameOverlayBackend {
    GameOverlayBackend();
    std::function<bool(const GameWindow&)> valid;
    std::function<HWND()> foreground;
    std::function<bool(OverlayWindow&, const RECT&)> show;
    std::function<void(HWND)> focus;
    std::function<GameOverlayMonitor::Snapshot()> read;
};
