#pragma once
// Helpers shared by the Launcher translation units (src/app/*.cpp). Not part of the
// Launcher interface; main.cpp and the tests never include this header.
#include "launcher.hpp"
#include "../artwork/platform_logo.hpp"
#include "../platform/ascii_text.hpp"

#include <cmath>
#include <string>

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
