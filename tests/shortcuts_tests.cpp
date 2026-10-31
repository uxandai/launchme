#include "../src/library/shortcuts.hpp"
#include "../src/library/visibility.hpp"
#include "../src/library/emulation_platforms.hpp"
#include "../src/library/esde_gamelist.hpp"

#include <Windows.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <tuple>

namespace {
size_t systemsFor(std::wstring_view name) {
    const auto* platform = findEmulationPlatform(name);
    return platform ? static_cast<size_t>(std::count_if(platform->esde.begin(), platform->esde.end(), [](auto s) { return !s.empty(); })) : 0;
}
}

int main(int argc, char** argv) {
    if (argc == 2) {
        const auto root = std::filesystem::path(argv[1]);
        const auto account = activeSteamId(root);
        const auto file = root / "userdata" / std::to_wstring(account - 76561197960265728ULL) /
                          "config" / "shortcuts.vdf";
        std::error_code ec;
        const auto size = std::filesystem::file_size(file, ec);
        size_t parse_error = 0;
        const auto shortcuts = scanSteamShortcuts(root, &parse_error);
        size_t hidden = 0;
        for (const auto& game : shortcuts) if (game.hidden) ++hidden;
        std::map<std::wstring, int> platforms;
        for (const auto& game : shortcuts) ++platforms[game.game.platform];
        std::cout << "shortcut_file=" << !ec << " bytes=" << (ec ? 0 : size)
                  << " shortcuts=" << shortcuts.size() << " hidden=" << hidden
                  << " parse_error_at=" << parse_error << '\n';
        for (const auto& [platform, count] : platforms) std::wcout << platform << L"=" << count << L' ';
        std::cout << '\n';
        // ES-DE identification of each emulated shortcut (by ROM file, else title).
        if (const auto esde_root = esde::root(); !esde_root.empty()) {
            esde::Library library(esde_root / L"gamelists");
            size_t with_rom = 0, by_rom = 0, by_title = 0;
            for (const auto& shortcut : shortcuts) {
                const auto& game = shortcut.game;
                if (game.platform == L"PC" && game.rom.empty()) continue;
                with_rom += !game.rom.empty();
                const auto found = library.find(game.platform, game.rom, game.name);
                const bool rom_match = found && !game.rom.empty() && found->entry.stem() == game.rom.stem().wstring();
                if (found) ++(rom_match ? by_rom : by_title);
                std::wcout << game.platform << L" | " << game.name << L" | rom=" << game.rom.wstring() << L" | "
                           << (found ? found->system + L'/' + found->entry.path + (rom_match ? L" (rom)" : L" (title)") : L"-") << L'\n';
            }
            std::cout << "esde_with_rom=" << with_rom << " by_rom=" << by_rom << " by_title=" << by_title << '\n';
        }
        return 0;
    }
    namespace fs = std::filesystem;
    const auto root = fs::temp_directory_path() /
                      (L"launchme-shortcuts-" + std::to_wstring(GetCurrentProcessId()));
    struct Cleanup { fs::path path; ~Cleanup() { std::error_code ec; fs::remove_all(path, ec); } } cleanup{root};
    fs::create_directories(root / "config");
    const auto folder = root / "userdata" / "39734314" / "config";
    fs::create_directories(folder);
    { std::ofstream login(root / "config" / "loginusers.vdf");
      login << "\"users\"\n{\n\"76561198000000042\"\n{\n\"AutoLogin\" \"1\"\n}\n}\n"; }
    std::ofstream file(folder / "shortcuts.vdf", std::ios::binary);
    const auto byte = [&](uint8_t value) { file.put(static_cast<char>(value)); };
    const auto string = [&](const char* value) { while (*value) file.put(*value++); byte(0); };
    const auto integer = [&](uint32_t value) { for (int i = 0; i < 4; ++i) byte(static_cast<uint8_t>(value >> (8*i))); };
    byte(0); string("shortcuts");
    byte(0); string("0");
    byte(2); string("appid"); integer(0xFEDCBA98u);
    byte(1); string("AppName"); string("Nazwa gry");
    byte(1); string("Exe"); string("C:\\Emu\\Cemu.exe");
    byte(2); string("LastPlayTime"); integer(1700000000u);
    byte(0); string("tags"); byte(1); string("0"); string("Wii U"); byte(8);
    byte(8);
    byte(0); string("1");
    byte(2); string("appid"); integer(0xF1234567u);
    byte(1); string("AppName"); string("Prywatna");
    byte(1); string("LaunchOptions"); string("--pcsx2");
    byte(2); string("IsHidden"); integer(1);
    byte(8); byte(8); file.close();
    const auto games = scanSteamShortcuts(root);
    if (games.size() != 2 || games[0].game.kind != GameKind::nonsteam ||
        games[0].game.app_id != 0xFEDCBA98u || games[0].game.platform != L"Wii U" ||
        games[0].game.last_played != 1700000000u || games[1].game.last_played != 0 ||
        games[1].game.platform != L"PS2" || !games[1].hidden ||
        games[0].game.shortcut_id != ((uint64_t{0xFEDCBA98u} << 32) | 0x02000000ULL)) {
        std::cerr << "Steam binary shortcuts or emulator classification failed\n";
        return 1;
    }
    // The shared platform table keeps inference priority and tile identities stable.
    const std::pair<const wchar_t*, const wchar_t*> inferred[] = {
        {L"C:\\Emu\\Cemu\\Cemu.exe", L"Wii U"}, {L"D:\\ryujinx.exe", L"Switch"}, {L"pcsx2-qt.exe", L"PS2"},
        {L"Dolphin.exe", L"Wii / GameCube"}, {L"rpcs3.exe", L"PS3"}, {L"shadPS4.exe", L"PS4"},
        {L"emu playstation 5", L"PS5"}, {L"PPSSPPWindows64.exe", L"PSP"}, {L"retroarch.exe", L"Retro"},
        {L"C:\\Games\\game.exe", L"PC"}, {L"Cemu.exe --switch", L"Wii U"},
        {L"C:\\Emu\\duckstation-qt-x64-ReleaseLTCG.exe", L"PS1"}, {L"K:\\roms\\psx\\game.chd", L"PS1"},
        // EmuDeck launchers are PowerShell scripts: ".ps1" is no PlayStation marker.
        {L"K:\\Emulation\\tools\\launchers\\es-de\\EmulationStationDE.ps1", L"PC"},
        // EmuDeck/SRM Amiga: RetroArch with the PUAE core, not the generic Retro tile.
        {L"powershell.exe -Command \"& {K:\\Emulation\\tools\\launchers\\retroarch.ps1 -L 'cores\\puae_libretro.dll' "
         L"'K:\\Emulation\\roms\\amiga\\CannonFodder_v1.2_CD32.lha'}\"", L"Amiga"}};
    for (const auto& [executable, platform] : inferred)
        if (shortcutPlatform(executable, L"", L"", L"") != platform) { std::wcerr << L"Platform inference: " << executable << L'\n'; return 2; }
    // Tiles by maker, then by release.
    const wchar_t* order[] = {L"NES", L"Game Boy", L"SNES", L"N64", L"Game Boy Color", L"Game Boy Advance", L"GameCube", L"DS", L"Wii",
        L"Wii / GameCube", L"3DS", L"Wii U", L"Switch", L"PS1", L"PS2", L"PS3", L"PS4", L"PS5", L"PSP", L"PS Vita", L"Master System",
        L"Mega Drive", L"Saturn", L"Dreamcast", L"Xbox", L"Xbox 360", L"Amiga", L"Arcade", L"DOS", L"Retro", L"PC"};
    if (std::size(order) != emulation_platforms.size()) return 3;
    for (size_t i = 0; i < std::size(order); ++i) {
        const auto* platform = findEmulationPlatform(order[i]);
        if (!platform || emulationPlatformIndex(*platform) != i || platform->slug.empty() || platform->logo.empty()) return 3;
    }
    if (findEmulationPlatform(L"Steam") || systemsFor(L"Wii / GameCube") != 2 || systemsFor(L"Retro") != 0 || systemsFor(L"PS1") != 1) return 4;
    // Steam ROM Manager collection names: whole system names, the " - emulator" part cut off.
    const std::pair<const wchar_t*, const wchar_t*> named[] = {
        {L"Sony PlayStation", L"PS1"}, {L"Sony Playstation 2", L"PS2"}, {L"Nintendo Wii", L"Wii"},
        {L"Nintendo Wii U", L"Wii U"}, {L"Nintendo Switch - Eden", L"Switch"}, {L"Nintendo GameCube (Dolphin)", L"GameCube"},
        {L"Nintendo Wii - Dolphin", L"Wii"}, {L"GC", L"GameCube"}, {L"NGC (Dolphin)", L"GameCube"},
        {L"Wii U games", L"Wii U"}, {L"Wii games", L"Wii"}, {L"GameCube games", L"GameCube"},
        {L"Wii / GameCube", L"Wii / GameCube"}, {L"Dolphin", L"Wii / GameCube"},
        {L"Sony PlayStation Portable", L"PSP"}, {L"Switch games", L"Switch"},
        {L"Amiga", L"Amiga"}, {L"Commodore Amiga - PUAE", L"Amiga"},
        {L"Emulation", L""}, {L"Sony PlayStation Vita", L"PS Vita"}, {L"Microsoft Xbox 360", L"Xbox 360"}, {L"Sega Dreamcast", L"Dreamcast"}, {L"  ", L""}};
    for (const auto& [name, platform] : named)
        if (collectionPlatform(name) != platform) { std::wcerr << L"Collection platform: " << name << L'\n'; return 5; }
    // Only SRM collections move shortcuts, the first one naming a platform wins, Steam games stay.
    std::vector<ShortcutGame> assigned(3);
    assigned[0].game.app_id = 0x90000001u; assigned[0].game.platform = L"PC";
    assigned[1].game.app_id = 0x90000002u; assigned[1].game.platform = L"PC";
    assigned[2].game.app_id = 0x90000003u; assigned[2].game.platform = L"Wii U";
    std::vector<SteamCollection> collections{
        {L"Emulation", {0x90000001u}, "user-collections.srm-RW11bGF0aW9u"},
        {L"Sony Playstation 2", {0x90000001u}, "user-collections.srm-ps2"},
        {L"Sony PlayStation", {0x90000001u, 0x90000002u, 620}, "user-collections.srm-ps1"},
        {L"Nintendo Switch", {0x90000003u}, "user-collections.uc-user-made"}};
    applyCollectionPlatforms(assigned, collections);
    if (assigned[0].game.platform != L"PS2" || assigned[1].game.platform != L"PS1" || assigned[2].game.platform != L"Wii U") return 6;
    // Shared .iso/.rvz extensions say nothing about the console: folders/tags must win over Dolphin.
    struct DolphinCase { const wchar_t* exe; const wchar_t* options; const wchar_t* title; const wchar_t* tags; const wchar_t* platform; };
    const DolphinCase dolphin[] = {
        {L"Dolphin.exe", L"-b -e \"K:\\roms\\gc\\Metroid Prime.rvz\"", L"Metroid Prime", L"", L"GameCube"},
        {L"Dolphin.exe", L"-e \"K:/roms/GC/Metroid Prime.iso\"", L"Metroid Prime", L"", L"GameCube"},
        {L"Dolphin.exe", L"-e \"K:\\roms\\GameCube\\Zelda.rvz\"", L"Zelda", L"", L"GameCube"},
        {L"powershell.exe", L"-File K:/launchers/dolphin.ps1 K:/roms/ngc/Zelda.rvz", L"Zelda", L"", L"GameCube"},
        {L"Dolphin.exe", L"-e \"K:\\roms\\wii\\Zelda.rvz\"", L"Zelda", L"", L"Wii"},
        {L"powershell.exe", L"-File K:/launchers/dolphin.ps1 K:/roms/WII/Zelda.iso", L"Zelda", L"", L"Wii"},
        {L"Dolphin.exe", L"-e K:/roms/Zelda.rvz", L"Zelda", L"Emulation Nintendo GameCube", L"GameCube"},
        {L"Dolphin.exe", L"-e K:/roms/Zelda.rvz", L"Zelda", L"Emulation Nintendo Wii", L"Wii"},
        {L"Dolphin.exe", L"-e K:/roms/Sports.rvz", L"Wii Sports", L"", L"Wii"},
        {L"Dolphin.exe", L"-e K:/roms/Zelda.rvz", L"Zelda", L"", L"Wii / GameCube"},
        {L"Cemu.exe", L"-g K:/roms/wiiu/Zelda.wua", L"Zelda", L"Nintendo Wii U", L"Wii U"},
        {L"game.exe", L"", L"Twilight", L"", L"PC"},
    };
    for (const auto& test : dolphin)
        if (shortcutPlatform(test.exe, test.options, test.title, test.tags) != test.platform) {
            std::wcerr << L"Dolphin console inference: " << test.options << L" / " << test.tags << L'\n'; return 7;
        }
    // The ROM a shortcut starts: ES-DE identifies games by this file, not by the shortcut's title.
    const std::tuple<const wchar_t*, const wchar_t*, const wchar_t*> roms[] = {
        {L"\"C:\\Emu\\pcsx2-qt.exe\"", L"-batch -fullscreen -- \"K:\\Emulation\\roms\\ps2\\Ratchet & Clank (Europe).chd\"",
         L"K:\\Emulation\\roms\\ps2\\Ratchet & Clank (Europe).chd"},
        {L"\"K:\\Emulation\\tools\\launchers\\pcsx2.ps1\"", L"'K:/Emulation/roms/ps2/Game.iso'", L"K:\\Emulation\\roms\\ps2\\Game.iso"},
        {L"powershell.exe -Command \"& {K:\\Emulation\\tools\\launchers\\retroarch.ps1 -L 'cores\\puae_libretro.dll' "
         L"'K:\\Emulation\\roms\\amiga\\CannonFodder_v1.2_CD32.lha'}\"", L"", L"K:\\Emulation\\roms\\amiga\\CannonFodder_v1.2_CD32.lha"},
        {L"powershell.exe", L"-File K:/launchers/dolphin.ps1 K:/roms/WII/Zelda.iso", L"K:\\roms\\WII\\Zelda.iso"},
        {L"C:\\Emu\\RetroArch\\retroarch.exe", L"-L \"C:\\Emu\\RetroArch\\cores\\snes9x_libretro.dll\" \"\\\\nas\\roms\\snes\\Super Metroid.sfc\"",
         L"\\\\nas\\roms\\snes\\Super Metroid.sfc"},
        {L"C:\\Emu\\rpcs3.exe", L"--no-gui \"D:\\roms\\ps3\\God of War.ps3\\PS3_GAME\\USRDIR\\EBOOT.BIN\"",
         L"D:\\roms\\ps3\\God of War.ps3\\PS3_GAME\\USRDIR\\EBOOT.BIN"},
        {L"\"C:\\Games\\Game\\game.exe\"", L"-windowed --config \"C:\\Games\\Game\\settings.ini\"", L""},
        {L"C:\\Emu\\Cemu.exe", L"-g relative\\game.wua", L""},
    };
    for (const auto& [exe, options, rom] : roms)
        if (shortcutRom(exe, options).wstring() != rom) { std::wcerr << L"Shortcut ROM: " << exe << L' ' << options << L'\n'; return 10; }
    const auto* gc = findEmulationPlatform(L"GameCube");
    const auto* wii = findEmulationPlatform(L"Wii");
    if (!gc || !wii || gc->logo != L"nintendo_gamecube.png" || wii->logo != L"nintendo_wii.png" ||
        gc->slug == wii->slug || systemsFor(L"GameCube") != 1 || gc->esde[0] != L"gc" ||
        systemsFor(L"Wii") != 1 || wii->esde[0] != L"wii") return 8;
    assigned.resize(4);
    for (size_t i = 0; i < assigned.size(); ++i) {
        assigned[i].game.app_id = 0x90000001u + static_cast<uint32_t>(i);
        assigned[i].game.platform = i == 3 ? L"GameCube" : L"Wii / GameCube";
    }
    collections = {
        {L"Dolphin", {0x90000001u, 0x90000002u, 0x90000003u, 0x90000004u}, "user-collections.srm-dolphin"},
        {L"Nintendo GameCube (Dolphin)", {0x90000001u}, "user-collections.srm-gc"},
        {L"Nintendo Wii - Dolphin", {0x90000002u}, "user-collections.srm-wii"},
    };
    applyCollectionPlatforms(assigned, collections);
    if (assigned[0].game.platform != L"GameCube" || assigned[1].game.platform != L"Wii" ||
        assigned[2].game.platform != L"Wii / GameCube" || assigned[3].game.platform != L"GameCube") return 9;
    return 0;
}
