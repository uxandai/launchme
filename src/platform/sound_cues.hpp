#pragma once

#include <array>
#include <string_view>

#include "../i18n/i18n.hpp"

// Built-in interface sounds: nothing here names a file that does not ship with Launchme (assets/).
// Minimalny: UI SFX's Minimal pack (res/uisfx, CC0): its focus cue for a move, with open, back and
// start beside it (Launcher::requestSound, uiSoundPack); Syntetyczny: the tone Audio::play makes.
// Notifications are silent by default; the start sound is a file of the sound folder.
struct SoundOption { const wchar_t* name; const wchar_t* file; };

inline constexpr std::array<SoundOption, 2> ui_sounds{{
    {trKey(L"Minimalny (UI SFX)"), L"uisfx/minimal/focus.mp3"},
    {trKey(L"Syntetyczny"), nullptr},
}};
// The pack's other cues, next to its focus file (assets/uisfx/minimal/).
inline bool uiSoundPack(const SoundOption* option) {
    return option && option->file && std::wstring_view(option->file).starts_with(L"uisfx/");
}

inline constexpr std::array<SoundOption, 2> toast_sounds{{
    {trKey(L"Brak"), nullptr},
    {trKey(L"Minimalny (UI SFX)"), L"uisfx/minimal/notification.mp3"},
}};

inline constexpr std::array<SoundOption, 1> boot_sounds{{
    {trKey(L"Brak"), nullptr},
}};
