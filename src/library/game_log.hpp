#pragma once
// A game's own logs, when "Zapisuj logi" is on in its Opcje uruchamiania:
//   <data>/logs/games/<slug>/<YYYY-MM-DD_HH-MM-SS>.log, one file per launch
// with what Launchme started (the target, its options, folder, HDR), the result and the session's
// end. "Zapisuj log Aurelii" writes <slug>/aurelia/<YYYY-MM-DD_HH-MM-SS>.log: Aurelia's own output (-vv)
// of a launch through it, the saves' syncs included. The last max_files of each folder are kept.
// Portable; thread-safe appends.
#include <filesystem>
#include <string>
#include <string_view>

namespace game_log {
inline constexpr size_t max_files = 10;
// Folder-safe name: the title in lowercase ASCII words joined by '-' (diacritics dropped), else the
// key's, else "game"; at most 64 characters.
std::string slug(std::wstring_view title, std::string_view key);
// The game's folder; kind: a folder of its own inside it ("aurelia"), or none (the launches' logs).
std::filesystem::path folder(const std::filesystem::path& data, std::wstring_view title, std::string_view key, std::wstring_view kind = {});
// A new launch's file in that folder (made), the oldest ones there beyond max_files removed; empty on failure.
std::filesystem::path begin(const std::filesystem::path& data, std::wstring_view title, std::string_view key, std::wstring_view kind = {});
// One line with the time in front; never throws.
void write(const std::filesystem::path& file, std::wstring_view line);
// Raw bytes (a program's own output), as they come.
void append(const std::filesystem::path& file, std::string_view bytes);
}
