#pragma once
// A game's own logs, when "Zapisuj logi" is on in its Opcje uruchamiania:
//   <data>/logs/games/<slug>/<YYYY-MM-DD_HH-MM-SS>.log, one file per launch
// with what Launchme started (the target, its options, folder, HDR), the result and the session's
// end; a game Aurelia runs also gets Aurelia's own output (play -vv). The last max_files launches
// of a game are kept. Portable; thread-safe appends.
#include <filesystem>
#include <string>
#include <string_view>

namespace game_log {
inline constexpr size_t max_files = 10;
// Folder-safe name: the title in lowercase ASCII words joined by '-' (diacritics dropped), else the
// key's, else "game"; at most 64 characters.
std::string slug(std::wstring_view title, std::string_view key);
std::filesystem::path folder(const std::filesystem::path& data, std::wstring_view title, std::string_view key);
// A new launch's file in the game's folder (made), the oldest ones beyond max_files removed; empty on failure.
std::filesystem::path begin(const std::filesystem::path& data, std::wstring_view title, std::string_view key);
// One line with the time in front; never throws.
void write(const std::filesystem::path& file, std::wstring_view line);
// Raw bytes (a program's own output), as they come.
void append(const std::filesystem::path& file, std::string_view bytes);
}
