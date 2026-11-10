#pragma once

// Emulacja's system logos: white wordmarks shipped in assets/systems (res/systems, from ordovice/es-theme-logos).
// A system's file by its ROM folder first (genesis, segacd, fds…), then by its platform; RetroArch's otherwise.

#include <algorithm>
#include <cwctype>
#include <string>
#include <string_view>
#include <utility>

namespace system_logos {

inline constexpr std::wstring_view fallback = L"retroarch.png";

inline std::wstring file(std::wstring_view system, std::wstring_view platform) {
    std::wstring folder(system);
    std::transform(folder.begin(), folder.end(), folder.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    // ROM folders with a logo of their own (or another folder's name for the same console).
    static constexpr std::pair<std::wstring_view, std::wstring_view> folders[] = {
        {L"nes", L"nes"}, {L"famicom", L"nes"}, {L"fds", L"fds"}, {L"gb", L"gb"}, {L"sgb", L"gb"}, {L"gbc", L"gbc"}, {L"gba", L"gba"},
        {L"snes", L"snes"}, {L"sfc", L"snes"}, {L"snesna", L"snes"}, {L"n64", L"n64"}, {L"gc", L"gc"}, {L"gamecube", L"gc"},
        {L"nds", L"nds"}, {L"wii", L"wii"}, {L"wiiu", L"wiiu"}, {L"psx", L"psx"}, {L"ps1", L"psx"}, {L"ps2", L"ps2"},
        {L"ps3", L"ps3"}, {L"psp", L"psp"}, {L"mastersystem", L"mastersystem"}, {L"megadrive", L"megadrive"},
        {L"megadrivejp", L"megadrive"}, {L"genesis", L"genesis"}, {L"genesiswide", L"genesis"}, {L"segacd", L"segacd"},
        {L"megacd", L"segacd"}, {L"megacdjp", L"segacd"}, {L"saturn", L"saturn"}, {L"saturnjp", L"saturn"},
        {L"dreamcast", L"dreamcast"}, {L"amiga", L"amiga"}, {L"arcade", L"arcade"}, {L"mame", L"mame"},
        {L"neogeo", L"neogeo"}, {L"dos", L"pc"},
        {L"psvita", L"psvita"}, {L"ps4", L"ps4"}, {L"ps5", L"ps5"}, {L"switch", L"switch"}, {L"switch2", L"switch2"},
        {L"n3ds", L"n3ds"}, {L"3ds", L"n3ds"}, {L"xbox", L"xbox"}, {L"xbox360", L"xbox360"}, {L"primehack", L"wii"},
        {L"amigacd32", L"amiga"}, {L"amiga600", L"amiga"}, {L"amiga1200", L"amiga"}, {L"fbneo", L"fbneo"}, {L"fba", L"fbneo"},
        {L"cps3", L"cps3"}, {L"c64", L"c64"}, {L"zxspectrum", L"zxspectrum"}, {L"atari2600", L"atari2600"},
        {L"atarijaguar", L"atarijaguar"}, {L"atarilynx", L"atarilynx"}, {L"scummvm", L"scummvm"}, {L"pcengine", L"pcengine"},
        {L"pcenginecd", L"pcenginecd"}, {L"tg16", L"tg16"}, {L"gamegear", L"gamegear"}, {L"sega32x", L"sega32x"},
        {L"3do", L"3do"}, {L"intellivision", L"intellivision"}, {L"virtualboy", L"virtualboy"}, {L"wonderswan", L"wonderswan"},
        {L"wonderswancolor", L"wonderswancolor"}, {L"ngp", L"ngpc"}, {L"ngpc", L"ngpc"},
    };
    for (const auto& [name, logo] : folders)
        if (folder == name) return std::wstring(logo) + L".png";
    // Platforms (src/library/emulation_platforms.hpp) whose consoles have one.
    static constexpr std::pair<std::wstring_view, std::wstring_view> platforms[] = {
        {L"NES", L"nes"}, {L"Game Boy", L"gb"}, {L"SNES", L"snes"}, {L"N64", L"n64"}, {L"Game Boy Color", L"gbc"},
        {L"Game Boy Advance", L"gba"}, {L"GameCube", L"gc"}, {L"DS", L"nds"}, {L"Wii", L"wii"}, {L"Wii / GameCube", L"wii"},
        {L"Wii U", L"wiiu"}, {L"PS1", L"psx"}, {L"PS2", L"ps2"}, {L"PS3", L"ps3"}, {L"PSP", L"psp"},
        {L"Master System", L"mastersystem"}, {L"Mega Drive", L"megadrive"}, {L"Saturn", L"saturn"},
        {L"Dreamcast", L"dreamcast"}, {L"Amiga", L"amiga"}, {L"Arcade", L"arcade"}, {L"DOS", L"pc"},
        {L"3DS", L"n3ds"}, {L"Switch", L"switch"}, {L"Switch 2", L"switch2"}, {L"PS4", L"ps4"}, {L"PS5", L"ps5"},
        {L"PS Vita", L"psvita"}, {L"Xbox", L"xbox"}, {L"Xbox 360", L"xbox360"},
    };
    for (const auto& [name, logo] : platforms)
        if (platform == name) return std::wstring(logo) + L".png";
    return std::wstring(fallback);
}

} // namespace system_logos
