// Included after LauncherUiTest: Ustawienia → Biblioteka → Emulacja (src/app/emulation_settings.cpp)
// and Dodaj grę's "Wskaż folder" (src/app/add_game.cpp). The ROM scan reads a fixture computer only
// (emulation_environment_: EmuDeck's launchers and Emulators, a ROM folder, the registry off); the
// file dialogs are injected and nothing is started. Captures: emulation-*.png (Tryb TV and desktop).
int LauncherUiTest::emulationSettings(HWND window) {
    using namespace settings_ui;
    namespace ui = custom_game_ui;
    const auto root = userConfigDirectory() / L"emulation-fixture";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    const auto write = [](const std::filesystem::path& file, const std::string& text) {
        std::filesystem::create_directories(file.parent_path());
        std::ofstream(file, std::ios::binary) << text;
    };
    // The fixture computer: PCSX2 behind EmuDeck's launcher, RetroArch without the SNES core, Vita3K.
    const auto pcsx2 = root / L"Programs" / L"PCSX2" / L"pcsx2-qtx64.exe";
    write(pcsx2, "exe");
    write(root / L"Emulation" / L"tools" / L"launchers" / L"pcsx2.ps1",
          "$emulatorFile = \"" + text_encoding::utf8(pcsx2.generic_wstring()) + "\"\r\nemulatorInit\r\n");
    const auto emulators_folder = root / L"AppData" / L"EmuDeck" / L"Emulators";
    write(emulators_folder / L"RetroArch" / L"retroarch.exe", "exe");
    write(emulators_folder / L"vita3k" / L"Vita3K.exe", "exe");
    const auto roms = root / L"Emulation" / L"roms";
    write(roms / L"ps2" / L"Okami.iso", "iso");
    write(roms / L"snes" / L"Super Metroid.sfc", "rom");
    write(roms / L"psvita" / L"PCSE00000" / L"eboot.bin", "elf");
    const auto manual = root / L"Elsewhere" / L"pcsx2-nightly.exe";
    write(manual, "exe");
    emulation_scan::Environment environment;
    environment.windows = true; environment.registry = false;
    environment.home = root; environment.app_data = root / L"AppData";
    environment.system_root = L"C:\\Windows";
    environment.program_dirs = {root / L"Program Files"};

    Launcher app; app.hwnd_ = window; app.execute_ = fakeExecute;
    app.boundary_feedback_ = [] {};
    app.settings_.launch_animation = false; app.settings_.tour_done = true;
    app.settings_.glance = app.settings_.companion = false;
    app.emulation_environment_ = [environment] { return environment; };
    std::vector<std::filesystem::path> picked;
    std::filesystem::path answer = manual;
    app.pick_path_ = [&](HWND, bool, const std::filesystem::path& initial) { picked.push_back(initial); return answer; };
    if (!app.renderer_.initialize(window)) return 9950;
    const auto capture = [&](const wchar_t* name) {
        wchar_t folder[4096]{};
        const auto count = GetEnvironmentVariableW(L"LAUNCHME_UI_CAPTURE_DIR", folder, 4096);
        if (!count || count >= 4096) { app.paint(); return true; }
        app.notifications_.clear();
        app.settings_motion_.valid = false;
        return app.renderer_.captureForTest(std::filesystem::path(folder) / name, [&] { app.paint(); });
    };
    const auto settle = [&] { for (int i = 0; i < 30; ++i) { app.last_frame_ -= std::chrono::milliseconds(50); app.tick(); } };
    // The scan's worker answers through the window's queue.
    const auto scanned = [&] {
        const auto end = GetTickCount64() + 10000;
        do {
            MSG message{};
            while (PeekMessageW(&message, window, remote_message, remote_message, PM_REMOVE))
                app.onRemote(std::unique_ptr<RemoteMessage>(reinterpret_cast<RemoteMessage*>(message.lParam)));
            if (app.emulation_setup_ && !app.rom_scanning_) return true;
            Sleep(5);
        } while (GetTickCount64() < end);
        return false;
    };
    const auto rowOf = [&](Id id, const std::string& key = {}) -> const Row* {
        const auto row = std::find_if(app.settings_rows_.begin(), app.settings_rows_.end(), [&](const Row& r) { return r.id == id && (key.empty() || r.key == key); });
        return row == app.settings_rows_.end() ? nullptr : &*row;
    };
    const auto select = [&](Id id, const std::string& key = {}) {
        const auto* row = rowOf(id, key);
        if (!row) return false;
        app.settings_state_.selected[app.settings_state_.category] = static_cast<int>(row - app.settings_rows_.data());
        app.settings_state_.options = true; app.settings_state_.editing = false; app.ensureSettingVisible();
        return true;
    };
    auto library = std::make_unique<LibraryMessage>(); library->account = 1; library->games = {Game{620, L"Portal 2"}};
    app.onLibrary(std::move(library));
    app.settings_.emulation_roms = roms;
    app.settings_.emulation_source = AppSettings::EmulationSource::roms;
    app.startRomScan();
    if (!scanned() || app.emulation_setup_->paths.roms != roms || app.rom_library_.size() != 3) return 9951;

    // Biblioteka → the Emulacja tile: games and emulators found; its page lists the systems with games.
    app.openSettings(); app.settings_state_.selectCategory(static_cast<int>(Category::library));
    app.pc_scanning_ = false; app.buildSettingsRows();
    const auto* tile = std::find_if(app.settings_rows_.data(), app.settings_rows_.data() + app.settings_rows_.size(),
                                    [](const Row& r) { return r.id == Id::page && r.page == Page::emulation; });
    if (tile == app.settings_rows_.data() + app.settings_rows_.size() || tile->value.find(L"Gry: 3") == std::wstring::npos ||
        tile->value.find(L"Emulatory: 3") == std::wstring::npos) return 9952;
    app.settings_state_.selected[app.settings_state_.category] = static_cast<int>(tile - app.settings_rows_.data());
    app.settings_state_.options = true; app.settings_state_.editing = false;
    app.settingsActivate();
    if (!app.settings_state_.at(Page::emulation) || !rowOf(Id::emulation) || !rowOf(Id::emulation_source) || !rowOf(Id::emulation_folder)) return 9953;
    const auto* ps2 = rowOf(Id::emulation_system, "ps2");
    const auto* snes = rowOf(Id::emulation_system, "snes");
    const auto* vita = rowOf(Id::emulation_system, "psvita");
    if (!ps2 || !snes || !vita || ps2->value != L"PCSX2" || ps2->description.find(L"Skrypt EmuDecka (pcsx2.ps1)") == std::wstring::npos ||
        vita->value != L"Vita3K" || !rowOf(Id::emulation_more)) return 9954;
    // RetroArch without snes9x_libretro: the system says the core is missing.
    if (snes->value != L"Brak rdzenia" || snes->description.find(L"brak rdzenia snes9x_libretro") == std::wstring::npos) return 9955;
    if (rowOf(Id::emulation_folder)->description.find(roms.wstring()) == std::wstring::npos) return 9956;
    settle();
    if (!capture(L"emulation-page.png")) return 9957;
    if (!select(Id::emulation_system, "snes")) return 9957; // Down to the systems.
    settle();
    if (!capture(L"emulation-page-systems.png")) return 9957;
    // The other systems fold below; unfolded, PS4 and ScummVM are there too.
    if (!select(Id::emulation_more)) return 9958;
    app.settingsActivate();
    if (!rowOf(Id::emulation_system, "ps4") || !rowOf(Id::emulation_system, "scummvm") || rowOf(Id::emulation_system, "ps4")->indent != 1) return 9958;
    if (!select(Id::emulation_system, "ps4")) return 9958;
    settle();
    if (!capture(L"emulation-page-other.png")) return 9958;

    // SNES's page: RetroArch, where it is and the core it lacks, the command with the core.
    if (!select(Id::emulation_system, "snes")) return 9959;
    app.settingsActivate();
    if (!app.settings_state_.at(Page::emulation_system) || !rowOf(Id::emulation_detected) ||
        rowOf(Id::emulation_detected)->title.find(L"brak rdzenia snes9x_libretro") == std::wstring::npos ||
        rowOf(Id::emulation_detected)->description.find(L"Pobierz go w RetroArch") == std::wstring::npos ||
        rowOf(Id::emulation_detected)->description.find(L"snes9x_libretro.dll") == std::wstring::npos ||
        rowOf(Id::emulation_preview)->description.find(L"snes9x_libretro.dll") == std::wstring::npos) return 9960;
    settle();
    if (!capture(L"emulation-system-core.png")) return 9961;
    // With the core it is RetroArch again.
    write(emulators_folder / L"RetroArch" / L"cores" / L"snes9x_libretro.dll", "dll");
    app.buildSettingsRows();
    if (rowOf(Id::emulation_detected)->title.find(L"brak rdzenia") != std::wstring::npos ||
        rowOf(Id::emulation_detected)->description.find(L"Pobierz go") != std::wstring::npos) return 9962;
    app.back();
    if (!app.settings_state_.at(Page::emulation) || app.settings_state_.at(Page::emulation_system) || rowOf(Id::emulation_system, "snes")->value != L"RetroArch") return 9963;

    // PS2's page: the emulator, how it was found, Program, the arguments' help and the command.
    if (!select(Id::emulation_system, "ps2")) return 9964;
    app.settingsActivate();
    const auto* choice = rowOf(Id::emulation_emulator);
    if (!choice || choice->choices.size() != 2 || choice->choices[1].key != L"pcsx2" || choice->choices[1].label != L"PCSX2 · znaleziony" ||
        !rowOf(Id::emulation_program) || rowOf(Id::emulation_program_reset) || !rowOf(Id::emulation_arguments)) return 9965;
    const auto* arguments = rowOf(Id::emulation_arguments);
    if (arguments->value != L"Domyślne" || arguments->description.find(L"Domyślnie: bez parametrów") == std::wstring::npos ||
        arguments->description.find(L"%ROM% = plik gry") == std::wstring::npos || arguments->description.find(L"%CORE%") != std::wstring::npos) return 9966;
    if (rowOf(Id::emulation_preview)->description.find(L"powershell.exe") == std::wstring::npos ||
        rowOf(Id::emulation_preview)->description.find(L"pcsx2.ps1") == std::wstring::npos) return 9967;
    settle();
    if (!capture(L"emulation-system.png")) return 9968;
    // Program → Wybierz…: the picker starts at the program found; the one picked is kept and found by hand.
    if (!select(Id::emulation_program)) return 9969;
    app.settingsActivate();
    if (picked.size() != 1 || picked.back() != pcsx2.lexically_normal() || app.settings_.emulator_overrides["pcsx2"].program != manual) return 9970;
    if (!scanned() || !rowOf(Id::emulation_program_reset) || rowOf(Id::emulation_program)->description != manual.wstring() ||
        rowOf(Id::emulation_detected)->title.find(L"Wskazany ręcznie") == std::wstring::npos) return 9971;
    const auto ps2_game = std::find_if(app.rom_library_.begin(), app.rom_library_.end(), [](const Game& g) { return g.platform == L"PS2"; });
    if (ps2_game == app.rom_library_.end() || ps2_game->play_action.target != manual.lexically_normal().wstring() ||
        ps2_game->play_action.arguments.find(L"-batch -fullscreen") == std::wstring::npos) return 9972;
    if (!capture(L"emulation-system-manual.png")) return 9973;
    // Przywróć wykrywanie: EmuDeck's launcher again.
    if (!select(Id::emulation_program_reset)) return 9974;
    app.settingsActivate();
    if (!scanned() || app.settings_.emulator_overrides.contains("pcsx2") || rowOf(Id::emulation_program_reset) ||
        rowOf(Id::emulation_detected)->title.find(L"Skrypt EmuDecka") == std::wstring::npos) return 9975;
    app.back();

    // PS1's emulator chosen by hand: RetroArch (not found: its page says so), then automatic again.
    if (!select(Id::emulation_system, "psx")) {
        if (!select(Id::emulation_more)) return 9976;
        app.settingsActivate();
        if (!select(Id::emulation_system, "psx")) return 9976;
    }
    app.settingsActivate();
    if (!select(Id::emulation_emulator)) return 9977;
    app.settings_state_.editing = true; app.settings_state_.choice = 2; // DuckStation, RetroArch: the second.
    if (!app.emulationSettingsChoose(*rowOf(Id::emulation_emulator)) || app.settings_.system_emulator["psx"] != "retroarch") return 9978;
    if (!scanned()) return 9979;
    app.buildSettingsRows();
    if (rowOf(Id::emulation_emulator)->value != L"RetroArch") return 9979;
    app.settings_state_.editing = true; app.settings_state_.choice = 0;
    app.emulationSettingsChoose(*rowOf(Id::emulation_emulator));
    if (app.settings_.system_emulator.contains("psx") || !scanned()) return 9980;
    for (int i = 0; i < 8 && app.settingsOpen(); ++i) app.back();
    if (app.settingsOpen()) return 9980;

    // Dodaj grę → Wskaż folder: one game's folder, a ROM folder, a folder of games.
    const auto ps3 = root / L"Games" / L"Demon's Souls";
    write(ps3 / L"PS3_GAME" / L"USRDIR" / L"EBOOT.BIN", "elf");
    app.openCustomEditor({}, ui::Item::pick_folder);
    if (!app.customEditorOpen() || app.custom_editor_.focus != ui::Item::pick_folder) return 9981;
    if (!capture(L"emulation-add-folder.png")) return 9982;
    answer = ps3;
    app.customEditorAccept();
    if (!app.customEditorOpen() || app.custom_editor_.fields[ui::file_field].find(L"EBOOT.BIN") == std::wstring::npos ||
        app.custom_editor_.fields[ui::platform_field] != L"PS3" || app.custom_editor_.fields[ui::name_field] != L"Demon's Souls") return 9983;
    if (!capture(L"emulation-add-folder-ps3.png")) return 9984;
    app.closeCustomEditor();
    // A ROM folder: it becomes the ROM folder of Emulacja, scanned at once.
    const auto other_roms = root / L"Other" / L"roms";
    write(other_roms / L"gba" / L"Metroid Fusion.gba", "rom");
    write(other_roms / L"n64" / L"Mario 64.z64", "rom");
    app.openCustomEditor({}, ui::Item::pick_folder);
    answer = other_roms;
    app.customEditorAccept();
    if (app.customEditorOpen() || app.settings_.emulation_roms != other_roms || app.settings_.emulation_source != AppSettings::EmulationSource::roms) return 9985;
    if (!scanned() || app.rom_library_.size() != 2 || app.emulation_setup_->paths.roms != other_roms) return 9986;
    // A folder of games (two programs in their own folders): the DRM-free catalogue keeps it.
    wchar_t system[MAX_PATH]{};
    GetSystemDirectoryW(system, MAX_PATH);
    const auto games = root / L"PC Games";
    for (const auto* name : {L"Alpha", L"Beta"}) {
        std::filesystem::create_directories(games / name);
        std::filesystem::copy_file(std::filesystem::path(system) / L"where.exe", games / name / (std::wstring(name) + L".exe"), error);
    }
    app.openCustomEditor({}, ui::Item::pick_folder);
    answer = games;
    app.customEditorAccept();
    if (app.customEditorOpen() || std::find(app.drm_catalog_.roots.begin(), app.drm_catalog_.roots.end(), games) == app.drm_catalog_.roots.end()) return 9987;
    if (app.drm_stop_) app.drm_stop_->store(true);
    // A folder with nothing in it: the editor stays and says why.
    const auto empty = root / L"Empty";
    std::filesystem::create_directories(empty);
    app.openCustomEditor({}, ui::Item::pick_folder);
    answer = empty;
    app.customEditorAccept();
    if (!app.customEditorOpen() || app.custom_editor_.error != L"W tym folderze nie ma gry ani ROM-ów.") return 9988;
    app.closeCustomEditor();

    // The desktop window: the same page and a system's page as its own dialog.
    app.settings_.emulation_roms = roms;
    app.startRomScan();
    if (!scanned()) return 9989;
    app.desktop_mode_ = true;
    app.audio_.setDesktop(true);
    app.openSettings(); app.settings_state_.selectCategory(static_cast<int>(Category::library));
    app.buildSettingsRows();
    app.openSettingsPage(Page::emulation, std::to_string(static_cast<int>(Page::emulation)));
    if (!app.settings_state_.at(Page::emulation) || !rowOf(Id::emulation_system, "ps2")) return 9990;
    settle();
    if (!capture(L"emulation-page-desktop.png")) return 9991;
    app.openSettingsPage(Page::emulation_system, "ps2");
    if (!app.settings_state_.at(Page::emulation_system) || !rowOf(Id::emulation_preview)) return 9992;
    settle();
    if (!capture(L"emulation-system-desktop.png")) return 9993;
    for (int i = 0; i < 8 && app.settingsOpen(); ++i) app.back();
    app.desktop_mode_ = false;
    std::filesystem::remove_all(root, error);
    return 0;
}
