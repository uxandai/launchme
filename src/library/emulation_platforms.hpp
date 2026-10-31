#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

// The single list of emulated platforms. Adding a platform here adds its
// shortcut inference, shelf tile, colour, logo, background and ES-DE media.
// Order matters three ways: shelf order (by maker, then by release: Nintendo,
// PlayStation, Sega, Xbox, Amiga, Arcade, DOS, Retro, PC), shortcut inference priority (first match
// wins: keep Wii U before Switch for "cemu ... switch") and the tile identity
// `emulation_tile_base + index` (a saved place on a shifted tile falls back
// safely; PC stays last as the fallback). The disk scanner (emulation_scan.cpp) files a ROM folder
// under the platform naming it in `esde`; one nobody names goes to Retro.
struct EmulationPlatform {
    std::wstring_view name;                     // Display name and Game::platform value.
    std::wstring_view slug;                     // platforms/backgrounds/<slug>.(png|jpg)
    std::wstring_view logo;                     // HVR88 Monochrome-Gaming-Logos file name.
    std::array<std::wstring_view, 2> esde;      // ES-DE system folders, most specific first.
    std::array<std::wstring_view, 4> markers;   // Lower-case text in exe/options/tags/title.
    // Lower-case Steam ROM Manager collection names meaning this system, matched whole after
    // the " - <emulator>" suffix is cut ("Nintendo Switch - Eden" -> "nintendo switch").
    std::array<std::wstring_view, 4> collections;
    float red, green, blue;                     // Tile base colour.
};

inline constexpr uint32_t emulation_tile_base = 0xF0000000u;

inline constexpr std::array<EmulationPlatform, 31> emulation_platforms{{
    // "mesen_libretro", not "mesen": Mesen-S plays the SNES.
    {L"NES", L"nes", L"nintendo_nes.png", {L"nes", L"famicom"}, {L"mesen_libretro", L"nestopia", L"fceumm", L"\\nes\\"},
     {L"nintendo entertainment system", L"nes", L"famicom", L"nintendo famicom"}, 0.62f, 0.12f, 0.14f},
    // Gambatte plays both Game Boys: the folders tell them apart.
    {L"Game Boy", L"gb", L"nintendo_gameboy.png", {L"gb"}, {L"\\gb\\", L"sameboy"},
     {L"nintendo game boy", L"game boy", L"gb"}, 0.36f, 0.42f, 0.22f},
    {L"SNES", L"snes", L"nintendo_snes.png", {L"snes", L"sfc"}, {L"snes9x", L"bsnes", L"\\snes\\", L"\\sfc\\"},
     {L"nintendo snes", L"super nintendo", L"snes", L"super famicom"}, 0.36f, 0.30f, 0.58f},
    {L"N64", L"n64", L"nintendo_64.png", {L"n64"}, {L"mupen64", L"parallel_n64", L"project64", L"\\n64\\"},
     {L"nintendo 64", L"n64"}, 0.12f, 0.42f, 0.30f},
    {L"Game Boy Color", L"gbc", L"nintendo_gameboy_color.png", {L"gbc"}, {L"\\gbc\\"},
     {L"nintendo game boy color", L"game boy color", L"gbc"}, 0.50f, 0.20f, 0.52f},
    {L"Game Boy Advance", L"gba", L"nintendo_gameboy_advance.png", {L"gba"}, {L"mgba", L"\\gba\\"},
     {L"nintendo game boy advance", L"game boy advance", L"gba"}, 0.30f, 0.22f, 0.56f},
    {L"GameCube", L"gamecube", L"nintendo_gamecube.png", {L"gc"}, {L"gamecube", L"\\gc\\", L"\\ngc\\"},
     {L"nintendo gamecube", L"gamecube", L"gc", L"ngc"}, 0.29f, 0.25f, 0.55f},
    {L"DS", L"nds", L"nintendo_ds.png", {L"nds"}, {L"melonds", L"desmume", L"\\nds\\"},
     {L"nintendo ds", L"nds"}, 0.40f, 0.42f, 0.46f},
    {L"Wii", L"wii", L"nintendo_wii.png", {L"wii"}, {L"wii"},
     {L"nintendo wii", L"wii"}, 0.18f, 0.48f, 0.72f},
    // Dolphin alone cannot tell which console a game belongs to. Only unresolved shortcuts use this tile.
    {L"Wii / GameCube", L"wii", L"nintendo_wii.png", {L"wii", L"gc"}, {L"dolphin"},
     {L"wii / gamecube", L"gamecube / wii", L"nintendo wii / gamecube", L"dolphin"}, 0.18f, 0.48f, 0.72f},
    {L"3DS", L"3ds", L"nintendo_3ds.png", {L"n3ds"}, {L"azahar", L"citra", L"lime3ds", L"\\n3ds\\"},
     {L"nintendo 3ds", L"3ds"}, 0.72f, 0.16f, 0.20f},
    {L"Wii U", L"wiiu", L"nintendo_wiiu.png", {L"wiiu"}, {L"cemu", L"wii u"}, {L"nintendo wii u", L"wii u"}, 0.11f, 0.49f, 0.72f},
    {L"Switch", L"switch", L"nintendo_switch.png", {L"switch"}, {L"ryujinx", L"yuzu", L"sudachi", L"switch"},
     {L"nintendo switch", L"switch"}, 0.85f, 0.14f, 0.17f},
    // No "ps1" marker: EmuDeck launchers are PowerShell scripts (*.ps1) for every emulator.
    {L"PS1", L"ps1", L"playstation_flat.png", {L"psx"}, {L"duckstation", L"epsxe", L"pcsxr", L"\\psx\\"},
     {L"sony playstation", L"playstation", L"psx", L"ps1"}, 0.29f, 0.30f, 0.33f},
    {L"PS2", L"ps2", L"playstation_ps2.png", {L"ps2"}, {L"pcsx2", L"ps2", L"playstation 2"},
     {L"sony playstation 2", L"playstation 2", L"ps2"}, 0.12f, 0.25f, 0.54f},
    {L"PS3", L"ps3", L"playstation3_flat.png", {L"ps3"}, {L"rpcs3", L"playstation 3"},
     {L"sony playstation 3", L"playstation 3", L"ps3"}, 0.045f, 0.05f, 0.07f},
    // Leading spaces: "ps4"/"ps5" alone also occur inside unrelated emulator names.
    {L"PS4", L"ps4", L"playstation4_flat.png", {L"ps4"}, {L"shadps4", L"playstation 4", L" ps4"},
     {L"sony playstation 4", L"playstation 4", L"ps4"}, 0.08f, 0.29f, 0.64f},
    {L"PS5", L"ps5", L"playstation5_flat.png", {L"ps5"}, {L"playstation 5", L" ps5"},
     {L"sony playstation 5", L"playstation 5", L"ps5"}, 0.10f, 0.32f, 0.64f},
    {L"PSP", L"psp", L"playstation_psp.png", {L"psp"}, {L"ppsspp", L"psp"},
     {L"sony playstation portable", L"playstation portable", L"psp"}, 0.18f, 0.25f, 0.33f},
    {L"PS Vita", L"psvita", L"playstation_vita.png", {L"psvita"}, {L"vita3k", L"\\psvita\\", L"playstation vita"},
     {L"sony playstation vita", L"playstation vita", L"psvita", L"ps vita"}, 0.14f, 0.22f, 0.40f},
    {L"Master System", L"mastersystem", L"sega_master_system.png", {L"mastersystem"}, {L"\\mastersystem\\"},
     {L"sega master system", L"master system"}, 0.55f, 0.12f, 0.12f},
    // Genesis Plus GX plays every 8- and 16-bit Sega console: only the folders say which.
    {L"Mega Drive", L"megadrive", L"sega_megadrive.png", {L"megadrive", L"genesis"}, {L"\\megadrive\\", L"\\genesis\\"},
     {L"sega genesis/mega drive", L"sega mega drive", L"sega genesis", L"mega drive"}, 0.16f, 0.18f, 0.24f},
    {L"Saturn", L"saturn", L"sega_saturn.png", {L"saturn"}, {L"\\saturn\\", L"mednafen_saturn", L"yabasanshiro"},
     {L"sega saturn", L"saturn"}, 0.22f, 0.24f, 0.30f},
    {L"Dreamcast", L"dreamcast", L"sega_dreamcast.png", {L"dreamcast"}, {L"\\dreamcast\\", L"redream"},
     {L"sega dreamcast", L"dreamcast"}, 0.74f, 0.36f, 0.12f},
    {L"Xbox", L"xbox", L"xbox_original.png", {L"xbox"}, {L"xemu", L"\\xbox\\"},
     {L"microsoft xbox", L"xbox"}, 0.18f, 0.42f, 0.16f},
    {L"Xbox 360", L"xbox360", L"xbox_360.png", {L"xbox360"}, {L"xenia", L"\\xbox360\\"},
     {L"microsoft xbox 360", L"xbox 360"}, 0.24f, 0.52f, 0.18f},
    // Before Retro: EmuDeck runs Amiga games through RetroArch's PUAE core.
    {L"Amiga", L"amiga", L"commodore_amiga.png", {L"amiga", L"amigacd32"},
     {L"puae", L"winuae", L"fs-uae", L"\\amiga\\"},
     {L"commodore amiga", L"amiga", L"amiga cd32", L"commodore amiga cd32"}, 0.62f, 0.16f, 0.20f},
    {L"Arcade", L"arcade", L"arcade.png", {L"arcade", L"mame"}, {L"\\arcade\\", L"\\mame\\", L"\\fbneo\\", L"\\naomi\\"},
     {L"arcade", L"mame"}, 0.52f, 0.30f, 0.10f},
    {L"DOS", L"dos", L"ms-dos.png", {L"dos", L"pc"}, {L"dosbox_pure", L"\\dos\\"},
     {L"dos", L"ms-dos"}, 0.20f, 0.20f, 0.22f},
    {L"Retro", L"retro", L"playlist_classics.png", {}, {L"retroarch"}, {}, 0.18f, 0.25f, 0.33f},
    // Fallback for every shortcut nothing else claims; it has no markers.
    {L"PC", L"pc", L"windows.png", {}, {}, {}, 0.18f, 0.25f, 0.33f},
}};

inline constexpr const EmulationPlatform* findEmulationPlatform(std::wstring_view name) {
    for (const auto& platform : emulation_platforms) if (platform.name == name) return &platform;
    return nullptr;
}
inline constexpr size_t emulationPlatformIndex(const EmulationPlatform& platform) {
    return static_cast<size_t>(&platform - emulation_platforms.data());
}
