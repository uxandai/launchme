// Gry emulowane from the ROM folder (src/library/emulation_scan.cpp): the built-in parsers' globs,
// titles, the setup found on disk and the commands. With "--live" it lists what this computer's
// EmuDeck / ES-DE setup holds (a diagnostic run, nothing is checked).
#include "../src/library/emulation_scan.hpp"
#include "../src/platform/text_encoding.hpp"

#include <fstream>
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
    // Emulators found and set by hand (src/library/emulators.hpp), in a fixture folder only.
    {
        const auto root = std::filesystem::temp_directory_path() / L"launchme-emulators-fixture";
        std::error_code error;
        std::filesystem::remove_all(root, error);
        const auto write = [](const std::filesystem::path& file, const std::string& text) {
            std::filesystem::create_directories(file.parent_path());
            std::ofstream(file, std::ios::binary) << text;
        };
        const auto pcsx2 = root / L"Programs" / L"PCSX2" / L"pcsx2-qtx64.exe";
        write(pcsx2, "exe");
        write(root / L"Emulation" / L"tools" / L"launchers" / L"pcsx2.ps1",
              "$emulatorFile = \"" + text_encoding::utf8(pcsx2.generic_wstring()) + "\"\r\nemulatorInit\r\n");
        write(root / L"Emulation" / L"roms" / L"ps2" / L"Game.iso", "iso");
        write(root / L"Program Files" / L"Dolphin" / L"Dolphin.exe", "exe");
        const auto manual = root / L"Elsewhere" / L"duck.exe";
        write(manual, "exe");
        Environment environment;
        environment.windows = true; environment.registry = false;
        environment.home = root; environment.app_data = root / L"AppData";
        environment.system_root = L"C:\\Windows";
        environment.program_dirs = {root / L"Program Files"};
        const auto roms = root / L"Emulation" / L"roms";
        auto setup = load(environment, roms);
        const auto* script = setup.found(L"pcsx2");
        if (!script || script->via != emulators::Via::emudeck_script || script->target != pcsx2.lexically_normal()) return 40;
        const auto* dolphin = setup.found(L"dolphin");
        if (!dolphin || dolphin->via != emulators::Via::known_path) return 41;
        if (setup.found(L"duckstation") == nullptr || setup.found(L"duckstation")->found()) return 42;
        const auto play = action(*setup.forSystem(L"ps2"), setup, roms / L"ps2" / L"Game.iso");
        if (!play.hidden || play.arguments.find(L"pcsx2.ps1") == std::wstring::npos) return 43;
        // By hand: DuckStation's program and arguments (%ROM% placed), and PS1 started with it.
        emulators::Choices choices;
        choices.overrides["duckstation"] = {manual, L"-batch %ROM% -fullscreen"};
        choices.systems["psx"] = "duckstation";
        setup = load(environment, roms, choices);
        const auto* duck = setup.found(L"duckstation");
        if (!duck || duck->via != emulators::Via::manual) return 44;
        const auto ps1 = action(*setup.forSystem(L"psx"), setup, L"D:\\r.chd");
        if (ps1.target != manual.lexically_normal().wstring() || ps1.arguments != L"-batch \"D:\\r.chd\" -fullscreen") return 45;
        // A system chosen for an emulator that is not found has no command.
        choices.systems["ps2"] = "retroarch";
        setup = load(environment, roms, choices);
        if (!action(*setup.forSystem(L"ps2"), setup, L"D:\\a.iso").target.empty()) return 46;
        // A ROM folder: two system folders with games.
        write(roms / L"snes" / L"Mario.sfc", "rom");
        if (!romFolder(roms) || romFolder(root / L"Programs")) return 47;
        // PS4 (shadPS4), PS Vita (Vita3K) and ScummVM, as EmuDeck's parsers and launchers have them.
        {
            const auto emulators = root / L"AppData" / L"EmuDeck" / L"Emulators";
            write(emulators / L"shadps4-qt" / L"shadPS4QtLauncher.exe", "exe");
            write(emulators / L"vita3k" / L"Vita3K.exe", "exe");
            write(root / L"Emulation" / L"tools" / L"launchers" / L"scummvm.ps1", "$emulatorFile = \"\"\r\n");
            write(roms / L"ps4" / L"shortcuts" / L"Bloodborne.lnk", "lnk");
            write(roms / L"ps4" / L"CUSA00900" / L"eboot.bin", "elf");
            write(roms / L"psvita" / L"PCSE00000" / L"eboot.bin", "elf");
            write(roms / L"scummvm" / L"Monkey Island" / L"monkey.scummvm", "monkey");
            choices = {};
            setup = load(environment, roms, choices);
            const auto result = scan(setup);
            const auto game = [&](std::wstring_view platform, std::wstring_view title) -> const Game* {
                for (const auto& entry : result.games) if (entry.platform == platform && entry.name == title) return &entry;
                return nullptr;
            };
            const auto* bloodborne = game(L"PS4", L"Bloodborne");
            const auto* cusa = game(L"PS4", L"CUSA00900");
            const auto* vita = game(L"PS Vita", L"PCSE00000");
            const auto* monkey = game(L"Retro", L"monkey");
            if (!bloodborne || !cusa || !vita || !monkey) return 48;
            // No EmuDeck launcher for shadPS4: its shortcut starts itself; the game's folder goes to the program.
            if (bloodborne->play_action.target != (roms / L"ps4" / L"shortcuts" / L"Bloodborne.lnk").wstring()) return 49;
            if (cusa->play_action.target.find(L"shadPS4QtLauncher.exe") == std::wstring::npos ||
                cusa->play_action.arguments.find(L"eboot.bin") == std::wstring::npos) return 50;
            // Vita3K by its title ID; ScummVM through EmuDeck's launcher with the file in its --path word.
            if (vita->play_action.arguments != L"-F -r \"PCSE00000\"") return 51;
            if (!monkey->play_action.hidden || monkey->play_action.arguments.find(L"'--path=" + (roms / L"scummvm" / L"Monkey Island" / L"monkey.scummvm").wstring() + L"'") ==
                std::wstring::npos) return 52;
            // RetroArch without the system's core: missing; with it: not.
            const auto retroarch = emulators / L"RetroArch" / L"retroarch.exe";
            write(retroarch, "exe");
            choices.systems["snes"] = "retroarch";
            setup = load(environment, roms, choices);
            const auto& snes = *setup.forSystem(L"snes");
            if (core(snes, setup) != retroarch.parent_path() / L"cores" / L"snes9x_libretro.dll" || !coreMissing(snes, setup)) return 53;
            write(retroarch.parent_path() / L"cores" / L"snes9x_libretro.dll", "dll");
            if (coreMissing(snes, setup) || !core(*setup.forSystem(L"ps2"), setup).empty()) return 54;
        }
        std::filesystem::remove_all(root, error);
    }
    std::cout << "emulation_scan passed\n";
    return 0;
}
