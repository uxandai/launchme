#pragma once

#include "../model/game_details.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

// ES-DE's local scrape results (ScreenScraper/TheGamesDB, fetched by ES-DE with its own
// credentials): gamelists/<system>/gamelist.xml and downloaded_media/<system>/<kind>/<stem>.*.
// Read-only; Launchme never writes into ES-DE's folders and never calls ScreenScraper itself.
namespace esde {
struct Entry {
    std::wstring path; // As written by ES-DE: "./Game (Europe).chd", relative to the system's ROM folder.
    std::wstring name;
    GameDetails details; // desc, releasedate (YYYY-MM-DD), developer, publisher, genre.
    std::wstring players;
    double rating = -1; // ScreenScraper score as ES-DE stores it, 0..1; -1 = none.
    bool hidden = false; // <hidden>true</hidden>: ES-DE leaves it out of its lists.
    std::wstring stem() const; // Media file name: the ROM file name without its extension.
};
using Gamelist = std::vector<Entry>;

Gamelist parseGamelist(std::string_view xml);
Gamelist loadGamelist(const std::filesystem::path& file);
// Lower-case alphanumerics up to the "(Region)" suffix: "Resident Evil - CV (Europe)" -> "residentevilcv".
std::wstring cleanedTitle(std::wstring_view title);
// ES-DE identifies a game by its ROM file: the longest <path> the ROM ends with (or, for
// folder games such as PS3 *.ps3, lies inside) wins. Only without a ROM match is the
// display name compared, cleaned as above.
const Entry* match(const Gamelist& entries, const std::filesystem::path& rom, const std::wstring& title);

// EmuDeck's ES-DE (%APPDATA%\EmuDeck\EmulationStation-DE\ES-DE), else %USERPROFILE%\ES-DE.
std::filesystem::path root();
std::filesystem::path mediaDirectory(const std::filesystem::path& root); // es_settings.xml MediaDirectory.
// ES-DE system folders to search: the platform's own (emulation_platforms.hpp), then the
// ROM's folder names ("roms\snes\Game.sfc" -> "snes"), so RetroArch's "Retro" shortcuts resolve too.
std::vector<std::wstring> systems(const std::wstring& platform, const std::filesystem::path& rom);

struct Match {
    std::wstring system;
    Entry entry;
    bool by_rom = false; // Matched by the ROM file (exact), not by the title.
};
// Parsed gamelists of one pass, keyed by system folder; avoids re-reading gamelist.xml per game.
class Library {
public:
    explicit Library(std::filesystem::path gamelists_root) : root_(std::move(gamelists_root)) {}
    std::optional<Match> find(const std::wstring& platform, const std::filesystem::path& rom, const std::wstring& title);
private:
    std::filesystem::path root_;
    std::unordered_map<std::wstring, Gamelist> lists_;
};
}
