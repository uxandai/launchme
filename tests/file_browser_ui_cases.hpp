// Included after LauncherUiTest: the launcher's own file picker (keyboard, mouse, filters,
// hidden files, folder mode, image preview) and the colour/glow settings. Everything works on
// fixture folders under the test's config directory; no Windows dialog is opened.

int LauncherUiTest::fileBrowserAndColors(HWND window) {
    using namespace settings_ui;
    using file_browser::Kind;
    using file_browser::Focus;
    const auto root = userConfigDirectory() / L"browse-fixtures";
    const auto browse = root / L"Browse";
    std::filesystem::create_directories(browse / L"Alpha" / L"Inner");
    std::filesystem::create_directories(browse / L"beta");
    for (const auto* name : {L"Game 10.exe", L"Game 2.exe", L"notes.txt", L"hidden.exe"}) std::ofstream(browse / name) << "fixture";
    SetFileAttributesW((browse / L"hidden.exe").c_str(), FILE_ATTRIBUTE_HIDDEN);
    { const auto png = pngFixture(90, 64, 40); std::ofstream(browse / L"picture.png", std::ios::binary).write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size())); }

    Launcher app;
    app.hwnd_ = window;
    app.settings_.launch_animation = false;
    if (!app.renderer_.initialize(window)) return 3100;
    const auto capture = [&](const wchar_t* name) {
        wchar_t folder[4096]{};
        const auto count = GetEnvironmentVariableW(L"LAUNCHME_UI_CAPTURE_DIR", folder, 4096);
        if (!count || count >= 4096) return true;
        InvalidateRect(window, nullptr, FALSE);
        return app.renderer_.captureForTest(std::filesystem::path(folder) / name, [&] { app.paint(); });
    };
    const auto names = [&] {
        std::vector<std::wstring> result;
        for (const auto& entry : app.file_browser_.entries) result.push_back(entry.kind == Kind::up ? L".." : entry.name);
        return result;
    };
    const auto selectRow = [&](Category category, Id id) {
        app.settings_state_.selectCategory(static_cast<int>(category));
        app.buildSettingsRows();
        return app.revealSetting(id); // On the category's list or one of its pages.
    };
    auto library = std::make_unique<LibraryMessage>(); library->account = 1;
    library->games.push_back(Game{9200, L"Steam fixture"});
    app.onLibrary(std::move(library));
    app.openSettings();

    // Settings → Library → "Dodaj grę" → "Wybierz…": the picker opens in the last used folder.
    app.settings_.file_browser_folder = browse;
    app.settings_state_.selectCategory(static_cast<int>(Category::library)); app.buildSettingsRows();
    app.openPlatformSettings(GameSource::drmfree); // DRM-free's page: "Dodaj grę" first.
    if (app.settings_rows_.empty() || app.settings_rows_[0].id != Id::custom_add) return 3101;
    app.settings_state_.options = true; app.settings_state_.selected[app.settings_state_.category] = 0;
    app.settingsActivate();
    if (!app.customEditorOpen() || app.custom_editor_.focus != custom_game_ui::Item::pick_file) return 3102;
    app.customEditorKey(VK_RETURN, false);
    auto& ui = app.file_browser_;
    if (!app.fileBrowserOpen() || ui.folder != browse || ui.folderMode() || ui.focus != Focus::list) return 3103;
    // Folders first, numbers in order, only programs, no hidden files; the first folder is highlighted.
    if (names() != std::vector<std::wstring>{L"..", L"Alpha", L"beta", L"Game 2.exe", L"Game 10.exe"} || ui.selected != 1) return 3104;
    if (!capture(L"file-browser-programs.png")) return 3140;
    // Modal over the editor: Guide/Start and the drawer stay closed, Tab only moves the focus.
    app.openGuideMenu(); app.toggleFriendsDrawer();
    if (!app.fileBrowserOpen() || app.quick_open_ || app.friends_open_ || !app.customEditorOpen()) return 3105;
    // Type-ahead: "g" jumps to the first game, again "g" to the next one.
    app.onCharacter(L'g');
    if (ui.selected != 3) return 3106;
    app.onCharacter(L'g');
    if (ui.selected != 4) return 3107;
    // Down past the last row reaches the buttons; Up comes back.
    app.fileBrowserKey(VK_DOWN, false);
    if (ui.focus != Focus::buttons || ui.button != 0) return 3108;
    app.fileBrowserKey(VK_UP, false);
    if (ui.focus != Focus::list || ui.selected != 4) return 3109;
    // Enter picks the file: the editor gets its path, name and folder.
    app.fileBrowserKey(VK_RETURN, false);
    if (app.fileBrowserOpen() || app.custom_editor_.fields[custom_game_ui::file_field] != (browse / L"Game 10.exe").native() ||
        app.custom_editor_.fields[custom_game_ui::name_field] != L"Game 10" ||
        app.custom_editor_.fields[custom_game_ui::directory_field] != browse.native()) return 3110;

    // Again: the chosen file is highlighted. "Wszystkie pliki" and hidden files widen the list.
    app.customEditorFocus(custom_game_ui::Item::pick_file);
    app.customEditorKey(VK_RETURN, false);
    if (!app.fileBrowserOpen() || ui.current() == nullptr || ui.current()->name != L"Game 10.exe") return 3111;
    const auto buttons = ui.buttons();
    if (buttons != std::vector<file_browser::Button>{file_browser::Button::file_type, file_browser::Button::hidden, file_browser::Button::system_dialog}) return 3112;
    app.fileBrowserButton(file_browser::Button::file_type);
    const auto every = names();
    if (std::find(every.begin(), every.end(), L"notes.txt") == every.end() || ui.current()->name != L"Game 10.exe") return 3113;
    app.fileBrowserButton(file_browser::Button::hidden);
    const auto all = names();
    if (std::find(all.begin(), all.end(), L"hidden.exe") == all.end() || std::find(all.begin(), all.end(), L"picture.png") == all.end()) return 3114;
    app.fileBrowserButton(file_browser::Button::hidden);
    // Backspace goes one folder up with the folder it came from highlighted; Esc cancels.
    app.fileBrowserKey(VK_BACK, false);
    if (ui.folder != root || ui.current() == nullptr || ui.current()->name != L"Browse") return 3115;
    // Left reaches the places rail, Down past it the Back button, Right the bottom buttons.
    app.fileBrowserKey(VK_LEFT, false);
    if (ui.focus != Focus::places || ui.places.empty() || ui.places.front().path != std::filesystem::path{}) return 3116;
    // "Ten komputer": the drives; Backspace there closes the picker like Esc.
    ui.place = 0; app.fileBrowserKey(VK_RETURN, false);
    if (!ui.root() || ui.focus != Focus::list || ui.entries.empty() ||
        std::any_of(ui.entries.begin(), ui.entries.end(), [](const auto& entry) { return entry.kind != Kind::drive; })) return 3117;
    if (!capture(L"file-browser-drives.png")) return 3141;
    app.fileBrowserKey(VK_BACK, false);
    if (app.fileBrowserOpen() || !app.customEditorOpen() ||
        app.custom_editor_.fields[custom_game_ui::file_field] != (browse / L"Game 10.exe").native()) return 3118;
    app.closeCustomEditor();

    // Mouse: a click highlights, a double click opens a folder; Back cancels.
    app.openCustomEditor({});
    app.customEditorFocus(custom_game_ui::Item::pick_file);
    app.customEditorKey(VK_RETURN, false);
    if (!app.fileBrowserOpen() || ui.folder != browse) return 3119;
    {
        const auto view = app.renderer_.viewport();
        const file_browser::Layout layout{view.width, view.height};
        const auto row = layout.row(2, ui.scroll); // "beta"
        const float x = (row.left + row.right) * 0.5f * view.scale, y = (row.top + row.bottom) * 0.5f * view.scale;
        app.mouse_transition_until_ = 0;
        app.click(x, y, false);
        if (ui.selected != 2 || ui.folder != browse) return 3120;
        app.click(x, y, true);
        if (ui.folder != browse / L"beta") return 3121;
        const auto back = layout.back();
        app.click((back.left + back.right) * 0.5f * view.scale, (back.top + back.bottom) * 0.5f * view.scale, false);
        if (app.fileBrowserOpen() || !app.customEditorOpen()) return 3122;
    }
    app.closeCustomEditor();

    // Folder mode (Dźwięk → Folder dźwięków): only folders; "Wybierz ten folder" picks the open one.
    if (!selectRow(Category::audio, Id::sound_folder)) return 3123;
    app.settingsActivate();
    if (!app.fileBrowserOpen() || !ui.folderMode() || ui.folder != browse ||
        names() != std::vector<std::wstring>{L"..", L"Alpha", L"beta"}) return 3124;
    if (ui.buttons().front() != file_browser::Button::choose_folder) return 3125;
    app.fileBrowserKey(VK_RETURN, false); // Into "Alpha".
    if (ui.folder != browse / L"Alpha" || names() != std::vector<std::wstring>{L"..", L"Inner"}) return 3126;
    if (!capture(L"file-browser-folder.png")) return 3142;
    app.fileBrowserButton(file_browser::Button::choose_folder);
    if (app.fileBrowserOpen() || app.settings_.sound_folder != browse / L"Alpha" || app.settings_.file_browser_folder != browse / L"Alpha") return 3127;

    // Images ("Z dysku…" uses the same request): PNG/JPEG only, with a preview pane.
    {
        file_browser::Request request;
        request.title = L"Grafika";
        request.extensions = {L".png", L".jpg", L".jpeg"};
        request.images = true;
        request.initial = browse / L"picture.png";
        std::filesystem::path chosen;
        app.openFileBrowser(std::move(request), [&](const std::filesystem::path& path) { chosen = path; });
        if (names() != std::vector<std::wstring>{L"..", L"Alpha", L"beta", L"picture.png"} || ui.current()->name != L"picture.png") return 3128;
        app.file_browser_preview_at_ = 1; // Due now.
        app.pollFileBrowserPreview();
        if (!app.renderer_.fileBrowserPreviewForTest()) return 3129;
        if (!capture(L"file-browser-images.png")) return 3143;
        app.fileBrowserKey(VK_RETURN, false);
        if (chosen != browse / L"picture.png" || app.fileBrowserOpen() || app.renderer_.fileBrowserPreviewForTest()) return 3130;
    }

    // Colours: white is the default; the console presets follow, "Inny…" last.
    if (!selectRow(Category::appearance, Id::ui_accent)) return 3150;
    {
        const auto& row = app.settings_rows_[app.settings_state_.row()];
        if (row.choices.size() != ui_accents.size() + 1 || row.choice != 0 || row.choices[0].color != 0xEEEEEE ||
            row.choices[2].label != L"Xbox Green" || row.choices[3].label != L"PlayStation Blue" || row.choices[4].label != L"Switch Red" ||
            !row.choices.back().custom) return 3151;
    }
    app.settingsActivate(); // The picker.
    app.settings_state_.choice = static_cast<int>(ui_accents.size());
    app.settingsActivate(); // "Inny…".
    auto& editor = app.color_editor_;
    if (!editor.open || editor.target != Id::ui_accent || app.settings_state_.editing || editor.rgb() != 0xEEEEEE) return 3152;
    // Saturation up from white, then a hue: the interface shows it live, B restores the saved white.
    app.settingsMove(1);
    if (editor.focus != 1) return 3153;
    for (int i = 0; i < 20; ++i) app.settingsAdjust(1);
    if (editor.saturation != 1.0f) return 3154;
    app.settingsMove(-1);
    for (int i = 0; i < 20; ++i) app.settingsAdjust(1); // 120°: green.
    if (std::abs(editor.hue - 120) > 0.5f || app.renderer_.uiAccentForTest() != editor.rgb()) return 3155;
    if (!capture(L"color-editor.png")) return 3144;
    app.back();
    if (editor.open || app.settings_.ui_accent_rgb != 0xEEEEEE || app.renderer_.uiAccentForTest() != 0xEEEEEE) return 3156;
    // Again, saved with A: the row lists it as its own colour.
    app.settingsActivate(); app.settings_state_.choice = static_cast<int>(ui_accents.size()); app.settingsActivate();
    for (int i = 0; i < 2; ++i) app.settingsMove(1);
    for (int i = 0; i < 6; ++i) app.settingsAdjust(-1); // Darker.
    app.settingsMove(-1);
    for (int i = 0; i < 20; ++i) app.settingsAdjust(1); // Saturation 100%.
    const uint32_t picked = editor.rgb();
    app.settingsActivate();
    wchar_t hex[16]{}; swprintf_s(hex, L"#%06X", picked);
    if (editor.open || app.settings_.ui_accent_rgb != picked || picked == 0xEEEEEE || editor.saturation != 1.0f ||
        app.settings_rows_[app.settings_state_.row()].value.find(hex) == std::wstring::npos) return 3157;
    // Mouse: a drag along the hue track. The saved colour is listed before "Inny…" now.
    app.settingsActivate(); app.settings_state_.choice = static_cast<int>(app.settings_rows_[app.settings_state_.row()].choices.size()) - 1; app.settingsActivate();
    {
        const auto view = app.renderer_.viewport();
        const auto track = Layout{view.width, view.height}.colorTrack(0);
        app.click((track.left + (track.right - track.left) * 0.5f) * view.scale, (track.top + track.bottom) * 0.5f * view.scale, false);
        if (!editor.dragging || std::abs(editor.hue - 180) > 1) return 3158;
        app.pointerUp();
        const auto cancel = Layout{view.width, view.height}.colorCancel();
        app.click((cancel.left + cancel.right) * 0.5f * view.scale, (cancel.top + cancel.bottom) * 0.5f * view.scale, false);
        if (editor.open || app.settings_.ui_accent_rgb != picked) return 3159;
    }

    // Glow: a 0-200% slider in steps of 10% and the pulse switch; both reach the renderer.
    if (!selectRow(Category::appearance, Id::glow_level)) return 3160;
    if (app.settings_rows_[app.settings_state_.row()].value != L"100%") return 3161;
    app.settingsActivate();
    app.settingsAdjust(-1); app.settingsAdjust(-1);
    if (std::abs(app.settings_.glow_level - 0.8f) > 0.001f || std::abs(app.renderer_.glowLevelForTest() - 0.8f) > 0.001f ||
        app.settings_rows_[app.settings_state_.row()].value != L"80%") return 3162;
    app.settingsActivate();
    // The glow's animation: still, pulsing or a travelling outline, never two at once.
    if (!selectRow(Category::appearance, Id::glow_motion)) return 3163;
    if (app.settings_rows_[app.settings_state_.row()].choices.size() != 3) return 3171;
    const auto motion = [&](int choice) { app.settingsActivate(); app.settings_state_.choice = choice; app.settingsActivate(); };
    motion(0);
    if (app.settings_.glow_pulse || app.settings_.glow_trace || app.renderer_.glowPulseForTest() || app.renderer_.glowTraceForTest() ||
        app.settings_rows_[app.settings_state_.row()].choice != 0) return 3164;
    if (!capture(L"glow-settings.png")) return 3145;
    motion(2);
    if (!app.settings_.glow_trace || !app.renderer_.glowTraceForTest() || app.settings_.glow_pulse ||
        app.renderer_.glowPulseForTest() || app.settings_rows_[app.settings_state_.row()].choice != 2) return 3172;
    motion(1);
    if (!app.settings_.glow_pulse || !app.renderer_.glowPulseForTest() || app.settings_.glow_trace ||
        app.renderer_.glowTraceForTest() || app.settings_rows_[app.settings_state_.row()].choice != 1) return 3174;
    motion(0);
    if (app.settings_.glow_trace || app.settings_.glow_pulse || app.renderer_.glowTraceForTest() || app.renderer_.glowPulseForTest()) return 3176;
    // Background dot grid: a 0-100% slider in steps of 5% that reaches the renderer.
    if (!selectRow(Category::appearance, Id::background_grid)) return 3165;
    if (app.settings_rows_[app.settings_state_.row()].value != L"50%") return 3166;
    app.settingsActivate();
    app.settingsAdjust(-1); app.settingsAdjust(-1);
    if (std::abs(app.settings_.background_grid - 0.4f) > 0.001f || std::abs(app.renderer_.backgroundGridForTest() - 0.4f) > 0.001f ||
        app.settings_rows_[app.settings_state_.row()].value != L"40%") return 3167;
    app.settingsActivate();
    // Its pattern: dots, horizontal lines or a mesh, a choice right under the slider.
    if (!selectRow(Category::appearance, Id::background_grid_style)) return 3168;
    if (app.settings_rows_[app.settings_state_.row() - 1].id != Id::background_grid ||
        app.settings_rows_[app.settings_state_.row()].choices.size() != 3) return 3169;
    app.settingsActivate(); app.settingsMove(1); app.settingsActivate();
    if (app.settings_.background_grid_style != AppSettings::GridStyle::lines) return 3170;
    return 0;
}
