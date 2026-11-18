// Included after LauncherUiTest: Opcje uruchamiania → Skrypty. The panel's "Skrypty" tab (Tryb TV and
// the desktop window) holds a game's commands before its launch, after its start and after its exit,
// each with "Testuj"; they are kept with the library state. A launch waits for the script before it,
// the session's start and end run the others. The runner is an injected fake: nothing is started.
// Captures: launch-scripts-*.png.
int LauncherUiTest::launchScripts(HWND window) {
    using custom_game_ui::Item;
    namespace ui = custom_game_ui;
    Launcher app;
    app.hwnd_ = window; app.execute_ = fakeExecute;
    app.settings_.launch_animation = false; app.settings_.tour_done = true;
    app.settings_.glance = app.settings_.companion = false;
    app.boundary_feedback_ = [] {};
    app.settings_.steam_silent_launch = false;
    app.steam_executable_ = [] { return std::filesystem::path(L"C:\\Steam\\steam.exe"); };
    if (!app.renderer_.initialize(window)) return 8700;
    struct Run { std::wstring command; launch_scripts::Context context; };
    std::mutex mutex;
    std::vector<Run> runs;
    std::atomic_int exit_code{0};
    app.launch_script_run_ = [&](std::wstring command, launch_scripts::Context context) {
        { std::lock_guard lock(mutex); runs.push_back({command, context}); }
        launch_scripts::Result result;
        result.started = result.finished = true;
        result.exit_code = exit_code.load();
        result.output = "Ustawiono rozdzielczość 1920x1080\r\n";
        return result;
    };
    const auto pump = [&] {
        MSG m{}; while (PeekMessageW(&m, window, remote_message, remote_message, PM_REMOVE))
            app.onRemote(std::unique_ptr<RemoteMessage>(reinterpret_cast<RemoteMessage*>(m.lParam)));
    };
    const auto wait = [&](const auto& ready) {
        for (const auto until = GetTickCount64() + 5000; GetTickCount64() < until;) { pump(); if (ready()) return true; Sleep(5); }
        return false;
    };
    const auto ran = [&] { std::lock_guard lock(mutex); return runs.size(); };
    const auto capture = [&](const wchar_t* name) {
        wchar_t folder[4096]{};
        const auto count = GetEnvironmentVariableW(L"LAUNCHME_UI_CAPTURE_DIR", folder, 4096);
        app.notifications_.clear();
        if (!count || count >= 4096) { app.paint(); return true; }
        std::filesystem::create_directories(folder);
        return app.renderer_.captureForTest(std::filesystem::path(folder) / name, [&] { app.paint(); });
    };
    auto library = std::make_unique<LibraryMessage>(); library->account = 1;
    Game steam{8700, L"Cyberpunk 2077"}; steam.kind = GameKind::steam; steam.installed = true;
    library->games.push_back(steam);
    app.onLibrary(std::move(library));
    const auto key = gameKey(steam);
    auto& editor = app.custom_editor_;

    // Tryb TV: the tabs above the field; Up from it reaches them, Right shows "Skrypty".
    app.openLaunchEditor(steam);
    if (!app.customEditorOpen() || !editor.tabs() || editor.scripts() || editor.focus != Item::arguments || editor.shows(ui::before_field)) return 8701;
    const auto panel = ui::Layout::of(editor, app.renderer_.viewport().width, app.renderer_.viewport().height).panel();
    app.customEditorKey(VK_UP, false);
    if (editor.focus != Item::tab_launch) return 8702;
    app.customEditorKey(VK_RIGHT, false);
    if (!editor.scripts() || editor.focus != Item::tab_scripts || editor.shows(ui::arguments_field) || !editor.shows(ui::exited_field) || editor.toggles()) return 8703;
    const auto scripts_panel = ui::Layout::of(editor, app.renderer_.viewport().width, app.renderer_.viewport().height).panel();
    if (scripts_panel.top != panel.top || scripts_panel.bottom != panel.bottom) return 8704; // The panel keeps its size.
    app.customEditorKey(VK_DOWN, false);
    if (editor.focus != Item::before) return 8705;
    app.customEditorInsert(L"C:\\Narzędzia\\Rozdzielczość.bat 1920 1080");
    app.customEditorKey(VK_RIGHT, false); // At the text's end: on to "Testuj".
    if (editor.focus != Item::test_before) return 8706;
    app.customEditorAccept();
    if (!editor.testing || !wait([&] { return !editor.testing; })) return 8707;
    if (ran() != 1 || runs[0].command != L"C:\\Narzędzia\\Rozdzielczość.bat 1920 1080" || runs[0].context.phase != launch_scripts::Phase::before ||
        runs[0].context.game_key != key || runs[0].context.game_name != L"Cyberpunk 2077" || editor.note_failed ||
        editor.note.find(L"Ustawiono rozdzielczość 1920x1080") == std::wstring::npos) return 8708;
    app.customEditorKey(VK_DOWN, false);
    if (editor.focus != Item::test_started) return 8709; // Down keeps the column.
    app.customEditorAccept(); // An empty field: nothing runs.
    if (ran() != 1 || editor.testing || editor.note.empty()) return 8710;
    app.customEditorKey(VK_LEFT, false); app.customEditorInsert(L"Start-Process \"C:\\Trainer\\trainer.exe\"");
    app.customEditorKey(VK_DOWN, false);
    if (editor.focus != Item::exited) return 8711;
    app.customEditorInsert(L"C:\\Narzędzia\\Rozdzielczość.bat 3840 2160");
    // A failing test is red.
    exit_code = 2;
    app.customEditorFocus(Item::test_exited); app.customEditorAccept();
    if (!wait([&] { return !editor.testing; }) || !editor.note_failed || editor.note.find(L"2") == std::wstring::npos) return 8712;
    exit_code = 0;
    if (!capture(L"launch-scripts-tv.png")) return 8713;
    app.desktop_mode_ = true; editor.desktop = true;
    const bool desktop_shot = capture(L"launch-scripts-desktop.png");
    app.desktop_mode_ = false; editor.desktop = false;
    if (!desktop_shot) return 8714;
    // LB: back to "Uruchamianie" (its option and switches), the scripts kept as typed.
    app.customEditorTab(ui::Tab::launch);
    if (editor.scripts() || !editor.shows(ui::arguments_field) || editor.fields[ui::before_field].empty()) return 8715;
    editor.note.clear();
    if (!capture(L"launch-scripts-tv-launch-tab.png")) return 8715;
    app.customEditorSave();
    const auto* saved = app.launchScripts(key);
    if (app.customEditorOpen() || !saved || saved->before != L"C:\\Narzędzia\\Rozdzielczość.bat 1920 1080" ||
        saved->started != L"Start-Process \"C:\\Trainer\\trainer.exe\"" || saved->exited != L"C:\\Narzędzia\\Rozdzielczość.bat 3840 2160") return 8716;
    // Opened again: the fields hold them.
    app.openLaunchEditor(steam);
    if (editor.fields[ui::started_field] != saved->started) return 8717;
    app.closeCustomEditor();

    // A launch: the script before it is waited for, the game starts after it, then the one after the start;
    // the session's end runs the one after the exit.
    { std::lock_guard lock(mutex); runs.clear(); }
    dispatched.clear();
    app.launchGame(steam);
    if (!app.pending_launch_ || !app.launch_script_wait_) return 8718;
    for (int i = 0; i < 20; ++i) { app.last_frame_ -= std::chrono::milliseconds(50); app.tick(); }
    if (!dispatched.empty()) return 8719; // Not before its script ended.
    if (!wait([&] { return !app.launch_script_wait_; }) || ran() != 1 || runs[0].context.phase != launch_scripts::Phase::before) return 8720;
    for (int i = 0; i < 20 && app.pending_launch_; ++i) { app.last_frame_ -= std::chrono::milliseconds(50); app.tick(); }
    if (dispatched.size() != 1 || app.pending_launch_) return 8721;
    if (!wait([&] { return ran() == 2; }) || runs[1].context.phase != launch_scripts::Phase::started || !app.script_sessions_.contains(key)) return 8722;
    pump();
    app.launchFlagsEnd(key, L"Ended"); // As onGameActivity does when the session ends.
    if (!wait([&] { return ran() == 3; }) || runs[2].context.phase != launch_scripts::Phase::exited ||
        runs[2].command != L"C:\\Narzędzia\\Rozdzielczość.bat 3840 2160" || app.script_sessions_.contains(key)) return 8723;
    pump();
    // Emptied fields: nothing of the game stays in the library state.
    app.openLaunchEditor(steam);
    app.customEditorTab(ui::Tab::scripts);
    for (const int field : {ui::before_field, ui::started_field, ui::exited_field}) editor.fields[static_cast<size_t>(field)].clear();
    editor.fields[ui::exited_field] = L"   ";
    app.customEditorSave();
    if (app.customEditorOpen() || app.launchScripts(key) || app.library_state_.launch_scripts.contains(key)) return 8724;
    // A script longer than library-state.ini's field: its tab and field say so.
    app.openLaunchEditor(steam);
    editor.fields[ui::started_field] = std::wstring(600, L'ż');
    app.customEditorSave();
    if (!app.customEditorOpen() || !editor.scripts() || editor.focus != Item::started || editor.error.empty()) return 8725;
    app.closeCustomEditor();
    // A new game added by hand: the tabs too (Up from the pills reaches them); its scripts are saved under
    // the key it gets, and its test already uses that key.
    app.openCustomEditor({});
    const auto new_key = gameKey(app.customEditorGame());
    if (!editor.tabs() || !editor.pills() || editor.focus != Item::pick_file) return 8726;
    if (!capture(L"launch-scripts-new-game-launch.png")) return 8733;
    app.customEditorKey(VK_UP, false);
    if (editor.focus != Item::tab_launch) return 8727;
    app.customEditorKey(VK_RIGHT, false);
    if (!editor.scripts() || editor.pills() || editor.shows(ui::name_field) || !editor.shows(ui::before_field)) return 8728;
    if (!capture(L"launch-scripts-new-game.png")) return 8729;
    editor.fields[ui::exited_field] = L"Stop-Process -Name trener";
    app.customEditorFocus(Item::test_exited); app.customEditorAccept();
    if (!wait([&] { return !editor.testing; })) return 8730;
    {
        std::lock_guard lock(mutex);
        if (new_key.empty() || runs.back().context.game_key != new_key) return 8731;
    }
    app.customEditorTab(ui::Tab::launch);
    editor.fields[ui::name_field] = L"Nowa gra ze skryptem";
    editor.fields[ui::file_field] = (userConfigDirectory() / L"nowa.exe").wstring();
    app.customEditorSave();
    const auto* fresh = app.launchScripts(new_key);
    if (app.customEditorOpen() || !fresh || fresh->exited != L"Stop-Process -Name trener") return 8732;
    return 0;
}
