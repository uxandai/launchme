// Gry emulowane from the ROM folder (src/library/emulation_scan.cpp): the built-in parsers' globs,
// titles, the setup found on disk and the commands. With "--live" it lists what this computer's
// EmuDeck / ES-DE setup holds (a diagnostic run, nothing is checked).
#include "../src/library/emulation_scan.hpp"
#include "../src/platform/text_encoding.hpp"

#include <iostream>
#include <string_view>

using namespace emulation_scan;

int main(int argc, char** argv) {
    if (argc == 2 && std::string_view(argv[1]) == "--live") {
        const auto setup = load(systemEnvironment(), {});
        const auto out = [](const std::wstring& text) { std::cout << text_encoding::utf8Lenient(text); };
        out(L"roms: " + setup.paths.roms.wstring() + L" (" + setup.paths.origin + L")\ntools: " + setup.paths.tools.wstring() +
            L"\nemulators: " + setup.paths.emulators.wstring() + L"\n");
        const auto result = scan(setup);
        out(L"games: " + std::to_wstring(result.games.size()) + L", hidden " + std::to_wstring(result.hidden) + L", excluded " + std::to_wstring(result.excluded) + L"\n");
        for (const auto& game : result.games)
            out(game.platform + L" | " + game.name + L" | " + game.rom.wstring() + L"\n    " + game.play_action.target + L" " + game.play_action.arguments +
                L"  [" + game.play_action.working_directory.wstring() + L"]\n");
        return 0;
    }
    // Globs as Steam ROM Manager reads them.
    if (match(L"**/${title}@(.bin|.chd|.iso)", L"Game (Europe).chd") != L"Game (Europe)") return 1;
    if (match(L"**/${title}@(.bin|.chd|.iso)", L"sub/Game.ISO") != L"Game") return 2;
    if (match(L"**/${title}@(.bin|.chd|.iso)", L"Game.txt")) return 3;
    if (match(L"**/${title}/PS3_GAME/USRDIR/@(eboot.bin|iso.bin.edat)", L"Demon's Souls/PS3_GAME/USRDIR/EBOOT.BIN") != L"Demon's Souls") return 4;
    if (match(L"${title}/**/*.rpx", L"Zelda BotW/code/U-King.rpx") != L"Zelda BotW") return 5;
    if (match(L"${title}@(.wux|.wua)", L"sub/Game.wux")) return 6; // No "**/": the system folder's top only.
    if (match(L"{genesis/**/!(homebrew),megadrive/**}/${title}@(.md)", L"genesis/homebrew/x.md")) return 7;
    if (match(L"{genesis/**/!(homebrew),megadrive/**}/${title}@(.md)", L"genesis/eu/Sonic.md") != L"Sonic") return 8;
    // Titles as a shelf shows them.
    if (displayTitle(L"Legend of Zelda, The - The Wind Waker (Europe) (En,Fr,De,Es,It)") != L"The Legend of Zelda - The Wind Waker") return 10;
    if (displayTitle(L"Final Fantasy Resonance [0100606026F12000][v0]") != L"Final Fantasy Resonance") return 11;
    if (displayTitle(L"CannonFodder_v1.2_CD32") != L"Cannon Fodder") return 12;
    if (displayTitle(L"Yoshis Crafted World v1.0.1") != L"Yoshis Crafted World") return 13;
    // Systems and platforms.
    if (platformForSystem(L"ps2") != L"PS2" || platformForSystem(L"psx") != L"PS1" || platformForSystem(L"snes") != L"SNES" ||
        platformForSystem(L"fbneo") != L"Arcade" || platformForSystem(L"tic80") != L"Retro") return 20;
    if (systemOf(L"K:\\Emulation\\roms\\ps2\\Game.iso") != L"ps2" || systemOf(L"K:\\x\\Game.iso", L"K:\\x") != L"") return 21;
    if (!romFile(L"a.nsp") || !romFile(L"a.chd") || romFile(L"a.exe") || romFile(L"a.txt")) return 22;
    // A typed command.
    const auto typed = commandAction(L"\"C:\\Emu\\pcsx2.exe\" -fullscreen %ROM%", L"D:\\Games\\a b.iso");
    if (typed.target != L"C:\\Emu\\pcsx2.exe" || typed.arguments != L"-fullscreen \"D:\\Games\\a b.iso\"") return 30;
    if (commandAction(L"\"C:\\Emu\\x.exe\"", L"D:\\r.bin").arguments != L"\"D:\\r.bin\"") return 31;
    std::cout << "emulation_scan passed\n";
    return 0;
}
