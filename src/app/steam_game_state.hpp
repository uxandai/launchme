#pragma once
// Linux: the page of a Steam game's own Proton settings (src/app/steam_game.cpp) in Launcher.
#ifndef _WIN32
#include "../library/steam_game_config.hpp"
#include "../model/game.hpp"

#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace launcher_state {
struct SteamGameSettings {
    enum class Mode {
        unknown,
        live,    // Steam runs with its CEF port: changes go through it at once.
        waiting, // Steam runs without it: changes wait until Steam closes (or it restarts with the port).
        files,   // Steam is closed: its files take them.
    };
    Game game;
    uint32_t app = 0;
    Mode mode = Mode::unknown;
    bool loading = false, failed = false;
    bool saving = false; // A change is on its way: the next waits for its answer (built on what Steam then has).
    bool answered = false; // The prompt was answered once this session: later changes simply wait.
    bool flag_created = false; // .cef-enable-remote-debugging exists (it takes effect when Steam restarts).
    steam_game_config::Settings settings;
    std::optional<steam_game_config::Pending> pending; // This game's change waiting for Steam to close.
    uint64_t generation = 0;
    // The change the prompt asks about (PromptKind::steam_game_save).
    steam_game_config::Pending asked;
    // One worker waits for Steam to close while changes are pending.
    std::shared_ptr<std::atomic<bool>> watching = std::make_shared<std::atomic<bool>>(false);
};
}
#endif
