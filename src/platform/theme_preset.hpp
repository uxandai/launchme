#pragma once

#include "user_config.hpp"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

// Saved looks (Settings → Interfejs → Dostosuj): the appearance part of AppSettings kept under a name in
// <config>\themes\<name>.ini, in the format of settings.ini. Nothing but appearance is copied in or out.
namespace theme_preset {

// Copies every appearance setting of `from` into `to` (layout and its options, tiles, colours, fonts,
// wallpaper, glass, motion); everything else in `to` stays.
void copyLook(const AppSettings& from, AppSettings& to);
// True when both have the same look.
bool sameLook(const AppSettings& a, const AppSettings& b);

// A name fit for a file: trimmed, one line, no path or reserved characters, at most 40 characters. Empty: unusable.
std::wstring cleanName(std::wstring_view name);

std::filesystem::path folder();
std::filesystem::path fileFor(std::wstring_view name, const std::filesystem::path& base);

// Names of the saved looks, alphabetical; `base` is the themes folder.
std::vector<std::wstring> list(const std::filesystem::path& base);
bool save(std::wstring_view name, const AppSettings& settings, const std::filesystem::path& base);
// The saved look over `settings`; false when there is none or it cannot be read.
bool load(std::wstring_view name, AppSettings& settings, const std::filesystem::path& base);
bool remove(std::wstring_view name, const std::filesystem::path& base);

inline std::vector<std::wstring> list() { return list(folder()); }
inline bool save(std::wstring_view name, const AppSettings& settings) { return save(name, settings, folder()); }
inline bool load(std::wstring_view name, AppSettings& settings) { return load(name, settings, folder()); }
inline bool remove(std::wstring_view name) { return remove(name, folder()); }

} // namespace theme_preset
