#pragma once
// Import z Playnite (Ustawienia → System → Kopia i przenoszenie): Playnite 10's library read from its
// folder (%AppData%\Playnite\library, or a portable copy's) or from its backup ZIP (the only way on Linux),
// matched to Launchme and staged like a backup's import (profile_transfer.hpp): applied at the next start,
// merged, never replacing what is here.
// - Store games (Steam, GOG, Epic, Battle.net, EA, Ubisoft, Xbox, Amazon) keep their place: play time, last
//   played, added, favourite, hidden, categories and tags as collections, cover and background as picks.
// - Games without a store with a program to start become "Gry dodane ręcznie"; emulated ones (a ROM and a
//   Playnite platform) become own games of their console. Links and scripts are skipped.
#include "../platform/library_state.hpp"
#include "custom_games.hpp"
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace playnite {
struct Action { std::string type; std::wstring path, arguments, working_dir; bool play = false; };
struct Game {
    std::string id, name, plugin, game_id, completion;
    uint64_t playtime = 0, play_count = 0, last_activity = 0, added = 0; // Seconds; Unix milliseconds.
    bool favorite = false, hidden = false, installed = false;
    std::vector<std::string> categories, tags, platforms;
    std::wstring install_dir, cover, background;
    std::vector<Action> actions;
    std::vector<std::wstring> roms;
};
struct Platform { std::wstring name; std::string specification; };
struct Library {
    std::vector<Game> games;
    std::map<std::string, std::wstring> categories, tags, completion;
    std::map<std::string, Platform> platforms;
    std::filesystem::path files; // Its "files" folder: covers and backgrounds by relative path.
};

// Where Playnite keeps its library on this computer (Windows, installed); empty when there is none.
std::filesystem::path findLibrary();
// `source`: the library folder (with games.db), Playnite's own folder, or its backup ZIP. A ZIP is unpacked
// (databases and the pictures they name) into `scratch`.
std::optional<Library> read(const std::filesystem::path& source, const std::filesystem::path& scratch, std::wstring& error);

struct Options {
    bool playtime = true, favorites = true, hidden = true, categories = true, tags = true, completion = false;
    bool artwork = true, own_games = true, emulated = true;
};
// The accounts history keys belong to (recent_games::historyKey): the running launcher's.
struct Accounts { uint64_t steam = 0; std::string gog, epic; };
struct Picture { std::string key; std::wstring kind; std::filesystem::path file; }; // kind: poster / hero.
struct Plan {
    LibraryState state;
    std::vector<custom_games::Entry> games;
    std::vector<Picture> pictures;
    size_t store = 0, own = 0, emulated = 0, collections = 0;
    std::vector<std::wstring> skipped; // Names of games left out (no store Launchme knows, links, scripts).
};
// `existing`: Launchme's own games now (an entry with the same file is not added twice).
Plan plan(const Library& library, const Options& options, const Accounts& accounts, const std::vector<custom_games::Entry>& existing);
// The game key Launchme gives a Playnite store game; empty for one it does not know.
std::string storeKey(const Game& game);
// A Launchme console (emulation_platforms) for a Playnite platform; empty: none fits.
std::wstring consoleFor(const Platform& platform);

// Writes the plan as a staged import (<config>/import-pending): applied, merged, at the next start.
bool stage(const Plan& plan, const std::filesystem::path& config, std::wstring& error);
}
