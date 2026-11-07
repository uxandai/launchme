#pragma once
// Helpers shared by the Launcher translation units (src/app/*.cpp). Not part of the
// Launcher interface; main.cpp and the tests never include this header.
#include "launcher.hpp"
#include "../artwork/platform_logo.hpp"
#include "../platform/ascii_text.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <string>
#include <unordered_set>
#include <vector>

// The share of the remaining way an animation covers in seconds at speed (per second):
// exponential easing that never overshoots, frame-rate independent.
inline float ease(float seconds, float speed) { return 1.0f - std::exp(-seconds * speed); }

// "1 gra", "3 gry", "12 gier" (English: games).
inline std::wstring gamesLabel(size_t n) {
    return std::to_wstring(n) + (n == 1 ? tr(L" gra") : n % 10 >= 2 && n % 10 <= 4 && (n % 100 < 12 || n % 100 > 14) ? tr(L" gry") : tr(L" gier"));
}
// A search query without the spaces around it.
inline std::wstring trimmedQuery(const std::wstring& text) { return std::wstring(ascii_text::trim(text, L" ")); }
// A key held right now (Ctrl, Shift with a click or an arrow).
inline bool keyDown(int key) { return (GetKeyState(key) & 0x8000) != 0; }

// A store client, the shell or a crash reporter (or no window at all): what a game leaves in front
// when it ends and what stands in front while a client still prepares one (src/app/game_sessions.cpp).
bool leftByGame(HWND window);

// The home rows after Ostatnio grane in the player's order (AppSettings::home_row_order): the saved ids
// that still exist, each new row after the one it follows in the built-in order (natural). Collections
// fill the collection places in their own order (natural lists them by collection_order), so moving one
// in the desktop window's menu still works.
inline std::vector<std::string> orderedHomeRows(const std::vector<std::string>& natural, const std::vector<std::string>& saved,
                                                const std::function<bool(const std::string&)>& collection) {
    const std::unordered_set<std::string> present(natural.begin(), natural.end());
    std::unordered_set<std::string> placed;
    std::vector<std::string> merged;
    for (const auto& id : saved) if (present.contains(id) && placed.insert(id).second) merged.push_back(id);
    for (size_t i = 0; i < natural.size(); ++i) {
        if (placed.contains(natural[i])) continue;
        auto at = merged.begin();
        for (size_t j = i; j-- > 0;)
            if (const auto found = std::find(merged.begin(), merged.end(), natural[j]); found != merged.end()) { at = found + 1; break; }
        merged.insert(at, natural[i]);
        placed.insert(natural[i]);
    }
    std::vector<std::string> collections;
    for (const auto& id : natural) if (collection(id)) collections.push_back(id);
    size_t next = 0;
    for (auto& id : merged) if (collection(id) && next < collections.size()) id = collections[next++];
    return merged;
}

// Holding Enter at least this long on a game opens its context menu instead of launching.
inline constexpr float enter_hold_seconds = 0.48f;

// One key per artwork request, shared by the UI-side queue bookkeeping and the download
// workers. Platform badges are keyed by logo file, so games of one platform share one fetch.
inline std::string artRequestKey(const Game& game, CoverMessage::Kind kind) {
    if (kind == CoverMessage::Kind::badge) {
        const auto filename = platformLogoFilename(activePlatformName(game));
        std::string key = "badge:";
        for (wchar_t ch : filename) key += static_cast<char>(ch); // Fixed ASCII filenames.
        return key;
    }
    return gameKey(game) + ':' + std::to_string(static_cast<int>(kind));
}

// The cached file kind behind a game artwork request (badges are not game artwork).
inline ArtworkKind artworkKind(CoverMessage::Kind kind) {
    switch (kind) {
    case CoverMessage::Kind::logo: return ArtworkKind::logo;
    case CoverMessage::Kind::hero: return ArtworkKind::hero;
    case CoverMessage::Kind::poster: return ArtworkKind::poster;
    case CoverMessage::Kind::landscape: return ArtworkKind::landscape;
    case CoverMessage::Kind::disc: return ArtworkKind::disc;
    default: return ArtworkKind::cover;
    }
}
