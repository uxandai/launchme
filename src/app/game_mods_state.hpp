#pragma once
// Opcje uruchamiania → Mody (src/app/game_mods.cpp, game_mods_panel.cpp): what each game's folder holds
// (scanned on a worker, kept for the session) and the one install or removal that may run at a time.
#include "../library/game_mods.hpp"
#include "../library/game_mods_install.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <string>

struct GameModsState {
    // A game's folder as last scanned, by gameKey: where its mods go and what is there.
    struct Scanned {
        game_mods::Target target;
        game_mods::State state;
        bool emulator = false;
        std::wstring emulator_name; // An emulated game: its emulator's name ("PCSX2").
    };
    std::map<std::string, Scanned> scanned;
    std::set<std::string> scanning;
    uint64_t scan_generation = 0; // Bumped when every scan is stale (an install, a new folder).

    // The panel's game and its choices per mod (game_mods::all_mods order).
    std::string key;
    std::array<std::string, game_mods::mod_count> methods;
    int anticheat_row = -1; // The row whose install was refused for anti-cheat: the next press installs.
    int remove_row = -1;    // The row whose "Usuń" was pressed once: the next press removes.
    bool emulator = false;  // The panel's target is an emulator: ReShade alone, its folder fixed.
    bool adopted = false;   // The installed mods' methods were taken over from the first scan.
    bool busy = false;
    uint64_t job_generation = 0;
    std::string job_key;    // The game the running job is for (its panel may have closed).
    std::shared_ptr<std::atomic_bool> cancel;
};
