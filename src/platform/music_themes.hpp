#pragma once
#include <array>
#include <string_view>
#include "../i18n/i18n.hpp"

// The built-in music themes: only what ships in assets/ (CMakeLists.txt), plus "Brak". A theme is
// saved by its id (settings.ini: music_track), so the list may grow or shrink without moving a
// saved choice. Files of the chosen sound folder follow these in the settings list.
struct MusicTheme { const wchar_t* name; const wchar_t* file; const char* id; };
inline constexpr std::array<MusicTheme, 4> music_themes{{
    {trKey(L"Brak"), nullptr, "none"},
    {L"Launchme \u2014 Horizons", L"HorizonsTheme.mp3", "horizons"},
    {L"Launchme \u2014 Exploratory Silence", L"ExploratorySilenceTheme.mp3", "exploratory-silence"},
    {L"Launchme — Quiet Exploration", L"QuietExplorationTheme.mp3", "quiet-exploration"},
}};
inline constexpr int default_music_theme = 1; // Launchme — Horizons.
inline int musicThemeIndex(std::string_view id) {
    for (size_t i = 0; i < music_themes.size(); ++i) if (id == music_themes[i].id) return static_cast<int>(i);
    return -1;
}
// The old numbered list (music_theme=0…11): ten themes that were never shipped, "Brak" at 10 and
// Horizons at 11. A removed theme falls back to the default one.
inline int legacyMusicTheme(int index) { return index == 10 ? 0 : index == 11 ? 1 : default_music_theme; }
inline constexpr size_t music_pcm_limit = 64 * 1024 * 1024;
