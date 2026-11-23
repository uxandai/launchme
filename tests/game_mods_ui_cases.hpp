// Included after LauncherUiTest: Opcje uruchamiania → Mody. A Windows game with a folder gets a third tab:
// its program's folder, then a row per mod with its method and install / update / remove; an emulated game's
// mods go into its emulator's folder (ReShade alone). What is installed shows as badges on the game's page
// and in the desktop window's pane; Ustawienia → Uruchamianie → Mody gier holds the preferred methods.
// The network and the shell are injected (game_mods::Services): nothing is downloaded or started.
// Captures: game-mods-*.png.
#include "../src/library/emulation_scan.hpp"
#include "../src/library/game_mods_sources.hpp"
#include "../src/platform/file_io.hpp"
#include "../src/platform/zip_archive.hpp"

int LauncherUiTest::gameMods(HWND window) {
    using custom_game_ui::Item;
    namespace ui = custom_game_ui;
    namespace fs = std::filesystem;
    Launcher app;
    app.hwnd_ = window; app.execute_ = fakeExecute;
    app.settings_.launch_animation = false; app.settings_.tour_done = true;
    app.settings_.glance = app.settings_.companion = false;
    app.boundary_feedback_ = [] {};
    app.steam_executable_ = [] { return std::filesystem::path(L"C:\\Steam\\steam.exe"); };
    if (!app.renderer_.initialize(window)) return 23000;
    const auto pump = [&] {
        MSG m{}; while (PeekMessageW(&m, window, remote_message, remote_message, PM_REMOVE))
            app.onRemote(std::unique_ptr<RemoteMessage>(reinterpret_cast<RemoteMessage*>(m.lParam)));
    };
    const auto wait = [&](const auto& ready) {
        for (const auto until = GetTickCount64() + 5000; GetTickCount64() < until;) { pump(); if (ready()) return true; Sleep(5); }
        return false;
    };
    const auto capture = [&](const wchar_t* name) {
        wchar_t folder[4096]{};
        const auto count = GetEnvironmentVariableW(L"LAUNCHME_UI_CAPTURE_DIR", folder, 4096);
        app.notifications_.clear();
        if (!count || count >= 4096) { app.paint(); return true; }
        std::filesystem::create_directories(folder);
        return app.renderer_.captureForTest(std::filesystem::path(folder) / name, [&] { app.paint(); });
    };
    const auto write = [](const fs::path& file, std::string_view bytes) {
        fs::create_directories(file.parent_path());
        std::ofstream(file, std::ios::binary).write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    };
    // A game's installation: Unreal's layout, its own DLSS; the "servers": ReShade's page, its setup (a zip
    // as the real one carries), the shaders.
    const auto root = userConfigDirectory() / L"mods-fixture";
    std::error_code ec;
    fs::remove_all(root, ec);
    const auto install = root / L"steamapps" / L"common" / L"Ćma Nocna";
    const auto binaries = install / L"CmaNocna" / L"Binaries" / L"Win64";
    write(binaries / L"CmaNocna-Win64-Shipping.exe", std::string(4096, 'x'));
    write(install / L"CmaNocna.exe", "launcher stub");
    write(install / L"Engine" / L"Plugins" / L"DLSS" / L"nvngx_dlss.dll", "dlss");
    const auto server = root / L"server";
    const auto zip = [&](const fs::path& file, const std::vector<std::pair<std::string, std::string>>& entries) {
        fs::create_directories(file.parent_path());
        zip_archive::Writer writer;
        if (!writer.open(file)) return false;
        for (const auto& [name, bytes] : entries) if (!writer.add(name, bytes)) return false;
        return writer.finish();
    };
    // A program with the archive appended, as ReShade's setup is.
    if (!zip(server / L"setup.zip", {{"ReShade64.dll", "reshade 64"}, {"ReShade32.dll", "reshade 32"}})) return 23001;
    {
        const auto archive = file_io::readText(server / L"setup.zip", 1024 * 1024);
        std::ofstream(server / L"setup.exe", std::ios::binary) << "MZ" << std::string(4096, '\x90') << archive;
    }
    if (!zip(server / L"slim.zip", {{"reshade-shaders-slim/Shaders/ReShade.fxh", "fxh"}})) return 23002;
    std::atomic_int fetches{0};
    app.mod_services_ = [&](std::shared_ptr<std::atomic_bool>) {
        game_mods::Services services;
        services.fetch = [&](const std::wstring& url) {
            ++fetches;
            HttpResponse response;
            if (url != game_mods::sources::reshade_page) { response.status = 404; return response; }
            const std::string page = R"(<a href="downloads/ReShade_Setup_6.8.0_Addon.exe">)";
            response.status = 200; response.body.assign(page.begin(), page.end());
            return response;
        };
        services.download = [&](const std::wstring& url, const fs::path& file) {
            const auto from = url == game_mods::sources::reshade_shaders ? server / L"slim.zip" :
                              url.ends_with(L"ReShade_Setup_6.8.0_Addon.exe") ? server / L"setup.exe" : fs::path{};
            if (from.empty()) return false;
            std::error_code copy;
            fs::create_directories(file.parent_path(), copy);
            fs::copy_file(from, file, fs::copy_options::overwrite_existing, copy);
            return !copy;
        };
        services.extract7z = [](const fs::path&, const fs::path&) { return false; };
        services.shell = [](std::wstring_view) { return launch_scripts::Result{}; };
        services.progress = [](std::wstring_view) {};
        services.cancelled = [] { return false; };
        return services;
    };
    app.mods_wait_ = true; // The install runs at once, on this thread.

    auto library = std::make_unique<LibraryMessage>(); library->account = 1;
    Game steam{23000, L"Ćma Nocna"}; steam.kind = GameKind::steam; steam.installed = true; steam.install_directory = install; steam.last_played = 100;
    library->games.push_back(steam);
    app.onLibrary(std::move(library));
    const auto settle = [&] { for (int i = 0; i < 40; ++i) { app.last_frame_ -= std::chrono::milliseconds(50); app.tick(); } };
    app.settings_.home_layout = AppSettings::HomeLayout::shelves;
    app.showCategory(0); settle();
    const auto key = gameKey(steam);
    auto& editor = app.custom_editor_;

    // Tryb TV: three tabs; RB twice reaches Mody, its rows come with the folder's scan.
    app.openLaunchEditor(steam);
    if (!app.customEditorOpen() || !editor.mods_offered || editor.tabCount() != 3) return 23003;
    app.customEditorTabStep(1); app.customEditorTabStep(1);
    if (!editor.mods() || editor.focus != Item::tab_mods || editor.shows(ui::arguments_field) || editor.optionCount()) return 23004;
    if (editor.modCount() != game_mods::mod_count) return 23005;
    if (!wait([&] { return app.mods_.scanned.contains(key) && editor.mod_rows[0].available; })) return 23006;
    if (editor.mod_folder != binaries.wstring()) return 23007; // The Shipping program's folder, not the installation's.
    const auto rows = app.modsPanelMods();
    const auto row_of = [&](game_mods::Mod mod) { return static_cast<int>(std::find(rows.begin(), rows.end(), mod) - rows.begin()); };
    if (!editor.mod_rows[static_cast<size_t>(row_of(game_mods::Mod::dlss))].available) return 23008; // Its own DLSS was found under the root.
    if (!editor.mod_rows[static_cast<size_t>(row_of(game_mods::Mod::dlss))].installed) return 23009; // Shown as what the game runs with.
    // Down: the folder's "Zmień…", then ReShade's install; Left: its method, Right steps it.
    app.customEditorKey(VK_DOWN, false);
    if (editor.focus != Item::mods_folder) return 23010;
    app.customEditorKey(VK_DOWN, false);
    if (editor.focus != Item::mod_action || editor.mod_row != 0) return 23011;
    app.customEditorKey(VK_LEFT, false);
    if (editor.focus != Item::mod_method) return 23012;
    app.customEditorKey(VK_RIGHT, false);
    if (app.mods_.methods[0] != "d3d11.dll" || editor.mod_rows[0].method.find(L"d3d11.dll") == std::wstring::npos) return 23013;
    app.customEditorKey(VK_LEFT, false);
    if (app.mods_.methods[0] != "dxgi.dll") return 23014;
    if (!capture(L"game-mods-tv.png")) return 23015;
    // A: the methods' list (DX11-12, OpenGL…); A picks one and goes on to the install; A again installs (the
    // injected servers), the row and the folder show it.
    app.customEditorAccept();
    if (!editor.dropdown.open || editor.dropdown.owner != Item::mod_method || editor.dropdown.items.size() < 2) return 23043;
    if (!capture(L"game-mods-tv-methods.png")) return 23044;
    app.customEditorKey(VK_RETURN, false);
    if (editor.dropdown.open || editor.focus != Item::mod_action || app.mods_.methods[0] != "dxgi.dll") return 23016;
    app.customEditorAccept();
    if (app.mods_.busy || editor.note_failed || editor.note.find(L"ReShade 6.8.0") == std::wstring::npos) return 23017;
    if (file_io::readText(binaries / L"dxgi.dll", 1024) != "reshade 64" || !file_io::isFile(binaries / L"ReShade.ini") ||
        !file_io::isFile(binaries / L"reshade-shaders" / L"Shaders" / L"ReShade.fxh")) return 23018;
    if (!wait([&] { return app.mods_.scanned.contains(key) && editor.mod_rows[0].removable; })) return 23019;
    if (editor.mod_rows[0].action != tr(L"Aktualizuj") || editor.mod_rows[0].status.find(L"6.8.0") == std::wstring::npos) return 23020;
    if (!capture(L"game-mods-tv-installed.png")) return 23021;
    // The badges: the game's page in Tryb TV (a line under the facts), the desktop window's pane.
    app.closeCustomEditor();
    app.setLaunchFlags(key, launch_flag::hdr); // Opcje uruchamiania's HDR: a badge beside the mods'.
    const auto badges = app.modBadges(steam);
    if (std::find(badges.begin(), badges.end(), L"ReShade") == badges.end() || badges.back() != L"HDR") return 23023;
    app.showCategory(0); settle();
    const auto card = std::find_if(app.cards_.begin(), app.cards_.end(), [&](const auto& c) { return gameKey(c.game) == key; });
    if (card == app.cards_.end()) return 23036;
    app.openDetails(card->game); settle();
    if (app.screen_ != ScreenMode::details) return 23037;
    if (!capture(L"game-mods-details-tv.png")) return 23024;
    app.desktop_mode_ = true;
    const bool desktop_details = capture(L"game-mods-details-desktop.png");
    app.desktop_mode_ = false;
    if (!desktop_details) return 23038;
    app.settings_.mod_badges = false;
    if (!app.modBadges(steam).empty()) return 23025; // Off in Ustawienia → Uruchamianie.
    app.settings_.mod_badges = true;
    app.back();
    // "Usuń": asked twice, then the game's files are as before.
    app.openLaunchEditor(steam);
    app.customEditorTab(ui::Tab::mods);
    if (!wait([&] { return app.mods_.scanned.contains(key) && editor.mod_rows[0].removable; })) return 23026;
    app.desktop_mode_ = true; editor.desktop = true;
    const bool desktop_shot = capture(L"game-mods-desktop.png");
    app.desktop_mode_ = false; editor.desktop = false;
    if (!desktop_shot) return 23022;
    editor.mod_row = 0;
    app.customEditorFocus(Item::mod_action);
    app.customEditorKey(VK_LEFT, false); // "Usuń" stands before the install.
    if (editor.focus != Item::mod_remove) return 23027;
    app.customEditorAccept();
    if (!editor.mod_rows[0].confirm_remove || !fs::exists(binaries / L"dxgi.dll")) return 23028; // Not yet.
    app.customEditorAccept();
    if (editor.note_failed || fs::exists(binaries / L"dxgi.dll") || fs::exists(binaries / L"reshade-shaders")) return 23029;
    if (!wait([&] { return app.mods_.scanned.contains(key) && !editor.mod_rows[0].installed; })) return 23030;
    // "Zmień…": another folder is kept for the game (library state); the program's own drops it again.
    app.pick_path_ = [&](HWND, bool, const fs::path&) { return install; };
    app.customEditorFocus(Item::mods_folder);
    app.customEditorAccept();
    if (app.library_state_.mod_folders[key] != install.wstring()) return 23031;
    if (!wait([&] { return app.mods_.scanned.contains(key) && editor.mod_folder == install.wstring(); })) return 23032;
    app.closeCustomEditor();
    app.pick_path_ = {};

    // The desktop game menu: a group with one entry (Opcje gry: only Opcje uruchamiania…) is no submenu.
    app.desktop_mode_ = true;
    const auto at = std::find_if(app.cards_.begin(), app.cards_.end(), [&](const auto& c) { return gameKey(c.game) == key; });
    if (at == app.cards_.end()) return 23039;
    app.desktopFocus(static_cast<size_t>(at - app.cards_.begin()));
    app.desktopOpenGameMenu(300, 300, true);
    if (!app.desktop_menu_ || std::none_of(app.desktop_menu_->items.begin(), app.desktop_menu_->items.end(), [](const auto& item) {
            return item.action == desktop_ui::Action::launch_options && item.label == tr(L"Opcje uruchamiania…"); })) return 23040;
    app.desktop_menu_.reset();
    app.desktop_mode_ = false;
    // A game found in a ROM folder (RPCS3's PS3_GAME): Opcje uruchamiania with its switches and scripts.
    GameAction run; run.kind = ActionKind::play;
    const auto rom = emulation_scan::makeGame(L"Gra z PS3", L"PS3", root / L"roms" / L"BLES00001" / L"PS3_GAME" / L"USRDIR" / L"EBOOT.BIN", run);
    if (!app.launchEditable(rom)) return 23041;
    app.openLaunchEditor(rom);
    if (!app.customEditorOpen() || editor.target != ui::Target::other || !editor.flags || editor.key != gameKey(rom) ||
        editor.managed.empty() || editor.focus != Item::option) return 23042;
    app.closeCustomEditor();

    // Settings → Uruchamianie → Mody gier: the badges' switch and a method per mod.
    const auto rows_settings = app.gameModsSettingsRows();
    if (rows_settings.size() != 6 || rows_settings[0].id != settings_ui::Id::mod_badges || rows_settings[1].key != "reshade" ||
        rows_settings[1].choices.size() != game_mods::methods(game_mods::Mod::reshade).size()) return 23033;
    app.settings_state_.choice = 2;
    app.gameModsSettingsChoose(rows_settings[1]);
    if (app.preferredModMethod(game_mods::Mod::reshade) != "d3d12.dll") return 23034;
    app.openLaunchEditor(Game{23001, L"Bez folderu"}); // Not installed: no Mody.
    if (editor.mods_offered) return 23035;
    app.closeCustomEditor();
    fs::remove_all(root, ec);
    return 0;
}
