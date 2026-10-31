// Included after LauncherUiTest: user-added games ("Moje gry") through the Settings →
// Library editor. The Windows file dialog and ShellExecute are injected fakes, so this
// test never opens a dialog or starts anything.
#include "../src/library/search.hpp"

namespace {
std::vector<GameAction> custom_launches;
INT_PTR customExecute(HWND, const GameAction& action) {
    custom_launches.push_back(action);
    return 33;
}
// GetKeyState() follows the thread keyboard state, so Ctrl+A goes through the real key path.
void pressWithControl(Launcher& app, WPARAM key) {
    BYTE keys[256]{};
    GetKeyboardState(keys);
    const BYTE previous = keys[VK_CONTROL];
    keys[VK_CONTROL] = 0x80; SetKeyboardState(keys);
    app.customEditorKey(key, false);
    keys[VK_CONTROL] = previous; SetKeyboardState(keys);
}
} // namespace

int LauncherUiTest::customGames(HWND window) {
    using custom_game_ui::Item;
    namespace ui = custom_game_ui;
    const auto root = userConfigDirectory() / L"custom-fixtures";
    const auto tools = root / L"Tools";
    std::filesystem::create_directories(tools);
    const auto exe = root / L"Space Pirates.exe";
    std::ofstream(exe) << "fixture";
    const auto list_file = userConfigDirectory() / L"custom-games.json";
    Launcher app;
    app.hwnd_ = window; app.execute_ = customExecute;
    app.settings_.launch_animation = false;
    std::vector<bool> picks; // Folder flag of every "Wybierz…" request.
    app.pick_path_ = [&](HWND, bool folder, const std::filesystem::path&) { picks.push_back(folder); return folder ? tools : exe; };
    if (!app.renderer_.initialize(window)) return 600;
    const auto capture = [&](const wchar_t* name) {
        wchar_t folder[4096]{};
        const auto count = GetEnvironmentVariableW(L"LAUNCHME_UI_CAPTURE_DIR", folder, 4096);
        if (!count || count >= 4096) return true;
        InvalidateRect(window, nullptr, FALSE);
        return app.renderer_.captureForTest(std::filesystem::path(folder) / name, [&] { app.paint(); });
    };
    const auto settle = [&] { for (int i = 0; i < 40; ++i) { app.last_frame_ -= std::chrono::milliseconds(50); app.tick(); } };
    const auto category = [&](const char* id) {
        return std::find_if(app.categories_.begin(), app.categories_.end(), [&](const auto& item) { return item.id == id; });
    };
    const auto findRow = [&](settings_ui::Id id, const std::string& key = {}) {
        const auto row = std::find_if(app.settings_rows_.begin(), app.settings_rows_.end(), [&](const settings_ui::Row& item) {
            return item.id == id && (key.empty() || item.key == key);
        });
        return row == app.settings_rows_.end() ? -1 : static_cast<int>(row - app.settings_rows_.begin());
    };
    // Settings → Library → DRM-free: "Dodaj grę" on top, one "Gry dodane ręcznie" total; its page holds the games.
    const auto settingsRow = [&](settings_ui::Id id, const std::string& key = {}) {
        app.openSettings();
        app.settings_state_.selectCategory(static_cast<int>(settings_ui::Category::library));
        app.buildSettingsRows();
        app.openPlatformSettings(GameSource::drmfree);
        const int total = findRow(settings_ui::Id::custom_games);
        if (total < 0 || app.settings_rows_[total].control != settings_ui::Control::count ||
            app.settings_rows_[total].value != std::to_wstring(app.custom_games_.size()) ||
            findRow(settings_ui::Id::custom_add) != 0 || findRow(settings_ui::Id::custom_game) >= 0) return false;
        app.settings_state_.enter(static_cast<int>(app.settings_rows_.size()));
        if (id == settings_ui::Id::custom_add) { app.settings_state_.selected[app.settings_state_.category] = 0; return true; }
        app.settings_state_.selected[app.settings_state_.category] = total;
        app.settingsActivate();
        if (!app.settings_state_.at(settings_ui::Page::custom_games) || app.settings_state_.row() != 0) return false;
        const int row = findRow(id, key);
        if (row < 0) return false;
        app.settings_state_.selected[app.settings_state_.category] = row;
        return true;
    };
    auto library = std::make_unique<LibraryMessage>(); library->account = 1;
    Game steam{9100, L"Steam fixture"}; steam.last_played = 100; library->games.push_back(steam);
    app.onLibrary(std::move(library));
    // No store enabled and no user-added game: no "Platformy PC" shelf yet.
    if (category("pc-platforms") != app.categories_.end()) return 601;

    // Settings → Library → DRM-free → "Dodaj grę" opens the panel at "Wskaż plik".
    if (!settingsRow(settings_ui::Id::custom_add)) return 602;
    app.settingsActivate();
    auto& editor = app.custom_editor_;
    if (!app.customEditorOpen() || editor.editing() || editor.focus != Item::pick_file) return 603;
    // D-pad/arrows reach Save; saving an empty form shows the inline error at the name.
    for (int i = 0; i < 5; ++i) app.customEditorKey(VK_DOWN, false);
    if (editor.focus != Item::save) return 604;
    app.customEditorKey(VK_RETURN, false);
    if (!app.customEditorOpen() || editor.error.empty() || editor.focus != Item::name || !app.custom_games_.empty()) return 605;
    if (!capture(L"custom-game-editor-error.png")) return 643;
    // "Wybierz…": the injected picker stands in for IFileOpenDialog. Name and folder follow the file.
    app.customEditorFocus(Item::pick_file);
    app.customEditorKey(VK_RETURN, false);
    if (picks != std::vector<bool>{false} || editor.fields[ui::file_field] != exe.native() || editor.fields[ui::name_field] != L"Space Pirates" ||
        editor.fields[ui::directory_field] != root.native() || editor.focus != Item::save || !editor.error.empty()) return 606;
    // Keyboard typing into the launch options, with caret editing.
    app.customEditorKey(VK_UP, false); app.customEditorKey(VK_UP, false);
    if (editor.focus != Item::arguments) return 607;
    for (const wchar_t ch : std::wstring(L"-windowedXY")) app.onCharacter(ch);
    app.customEditorKey(VK_BACK, false); app.customEditorKey(VK_BACK, false);
    app.customEditorInsert(L" -skipintro\r\n"); // Pasted text stays one line.
    app.customEditorKey(VK_HOME, false);
    for (const wchar_t ch : std::wstring(L"-v ")) app.onCharacter(ch);
    if (editor.fields[ui::arguments_field] != L"-v -windowed -skipintro") return 608;
    app.customEditorKey(VK_HOME, false);
    for (int i = 0; i < 3; ++i) app.customEditorKey(VK_DELETE, false);
    if (editor.fields[ui::arguments_field] != L"-windowed -skipintro" || editor.caret != 0) return 609;
    // Working directory: at the end of the text Right moves to its "Wybierz…" (folder picker).
    app.customEditorKey(VK_DOWN, false);
    app.customEditorKey(VK_RIGHT, false);
    if (editor.focus != Item::directory_browse) return 610;
    app.customEditorKey(VK_RETURN, false);
    if (picks != std::vector<bool>{false, true} || editor.fields[ui::directory_field] != tools.native()) return 611;
    app.customEditorFocus(Item::arguments);
    if (!capture(L"custom-game-editor.png")) return 612;
    app.customEditorFocus(Item::save);
    app.customEditorKey(VK_RETURN, false);
    if (app.customEditorOpen() || app.custom_games_.size() != 1) return 613;
    const auto id = app.custom_games_[0].id;
    const auto key = "custom:" + id;
    const auto stored = custom_games::load(list_file);
    if (!stored.readable || stored.games.size() != 1 || stored.games[0].id != id || stored.games[0].path != exe ||
        stored.games[0].arguments != L"-windowed -skipintro" || stored.games[0].directory != tools) return 614;
    // Its settings row is focused: name, file name + options, "Edytuj".
    const auto& saved_row = app.settings_rows_[app.settings_state_.row()];
    if (saved_row.id != settings_ui::Id::custom_game || saved_row.key != id || saved_row.title != L"Space Pirates" ||
        saved_row.description != L"Space Pirates.exe  -windowed -skipintro" || saved_row.value != L"Edytuj") return 615;
    if (!capture(L"custom-games-settings.png")) return 616;

    // Back: from the "Moje gry" page to its total on the Library page, now 1.
    app.back();
    if (!app.settingsOpen() || app.settings_state_.at(settings_ui::Page::custom_games) || app.settings_state_.row() != findRow(settings_ui::Id::custom_games) ||
        app.settings_rows_[app.settings_state_.row()].value != L"1" || findRow(settings_ui::Id::custom_game, id) >= 0) return 645;
    // "Moje gry" tile after the store tiles in "Platformy PC"; it opens a store-like library.
    app.back(); app.back(); app.back();
    if (app.settingsOpen()) return 617;
    const auto pcs = category("pc-platforms");
    if (pcs == app.categories_.end() || pcs->games.back().kind != GameKind::pc_platform ||
        pcs->games.back().source != GameSource::custom || pcs->games.back().name != L"Gry dodane ręcznie") return 618;
    const auto tile = pcs->games.size() - 1;
    app.showCategory(static_cast<size_t>(pcs - app.categories_.begin()));
    app.focusCard(tile);
    settle();
    if (!capture(L"custom-games-shelf.png")) return 642;
    app.launch();
    if (app.screen_ != ScreenMode::library || app.pc_source_ != GameSource::custom || app.cards_.size() != 1 ||
        gameKey(app.cards_[0].game) != key || app.actionLabel(app.cards_[0].game) != L"Graj") return 619;
    settle();
    if (!capture(L"custom-games-library.png")) return 620;
    // Launch: ShellExecute gets the file, the options and the working directory.
    app.launch(); app.finishLaunch();
    if (custom_launches.size() != 1 || custom_launches[0].target != exe.native() || custom_launches[0].arguments != L"-windowed -skipintro" ||
        custom_launches[0].working_directory != tools || custom_launches[0].kind != ActionKind::play || !app.game_sessions_.contains(key)) return 621;
    // Once its process is seen, history uses the stable key: "Ostatnio grane", "Wszystkie gry", search.
    auto activity = std::make_unique<GameActivityMessage>();
    activity->generation = app.game_activity_jobs_->generation.load();
    activity->observed_at = GetTickCount64();
    activity->activities.push_back({key, GameActivity::running});
    app.onGameActivity(std::move(activity));
    const auto listed = [&](const std::vector<Game>& games) {
        return std::any_of(games.begin(), games.end(), [&](const Game& game) { return gameKey(game) == key; });
    };
    if (!app.library_state_.last_played.contains("local/" + key) || !listed(app.categories_[0].games) ||
        !listed(app.public_library_) || !listed(searchPublicGames(app.public_library_, L"pirates"))) return 622;

    // Edit from Settings: Ctrl+A replaces the options, a new name keeps the ID and history.
    app.art_.at(key).cover = shareImage(pixel()); app.art_.resize(key);
    if (!settingsRow(settings_ui::Id::custom_game, id)) return 623;
    app.settingsActivate();
    if (!app.customEditorOpen() || !editor.editing() || editor.focus != Item::name ||
        editor.fields[ui::arguments_field] != L"-windowed -skipintro") return 624;
    pressWithControl(app, 'A');
    for (const wchar_t ch : std::wstring(L"Space Pirates II")) app.onCharacter(ch);
    app.customEditorKey(VK_TAB, false); app.customEditorKey(VK_TAB, false); app.customEditorKey(VK_TAB, false);
    if (editor.focus != Item::arguments) return 625;
    pressWithControl(app, 'A');
    for (const wchar_t ch : std::wstring(L"-dx12")) app.onCharacter(ch);
    app.customEditorKey(VK_ESCAPE, false); // Cancel keeps the saved definition.
    if (app.customEditorOpen() || app.custom_games_[0].arguments != L"-windowed -skipintro") return 626;
    app.settingsActivate();
    pressWithControl(app, 'A');
    for (const wchar_t ch : std::wstring(L"Space Pirates II")) app.onCharacter(ch);
    app.customEditorFocus(Item::arguments);
    pressWithControl(app, 'A');
    for (const wchar_t ch : std::wstring(L"-dx12")) app.onCharacter(ch);
    app.customEditorFocus(Item::save); app.customEditorAccept();
    if (app.customEditorOpen() || app.custom_games_.size() != 1 || app.custom_games_[0].id != id ||
        app.custom_games_[0].name != L"Space Pirates II" || app.custom_games_[0].arguments != L"-dx12" ||
        app.art_.contains(key) || !app.library_state_.last_played.contains("local/" + key)) return 627;
    app.back(); app.back(); app.back(); app.back();
    if (app.settingsOpen()) return 646;
    app.openPcLibrary(GameSource::custom);
    // While the first start is still pending, Graj does not start a second copy.
    app.launch();
    if (app.pending_launch_ || custom_launches.size() != 1) return 647;
    app.game_sessions_.clear(); // That run ended.
    app.launch(); app.finishLaunch();
    if (custom_launches.size() != 2 || custom_launches[1].arguments != L"-dx12" || custom_launches[1].working_directory != tools ||
        app.cards_.empty() || app.cards_[0].game.name != L"Space Pirates II") return 628;
    // Started from its library: Home, the game focused at the front of Recent.
    if (app.screen_ != ScreenMode::shelves || gameKey(app.cards_[app.selected_].game) != key) return 644;
    app.openPcLibrary(GameSource::custom);

    // A missing file (reported by the worker) stays listed without a launch action.
    auto check = std::make_unique<CustomGamesMessage>();
    check->generation = app.custom_token_->load();
    check->installed[id] = false;
    app.onCustomGames(std::move(check));
    if (app.cards_.size() != 1 || app.cards_[0].game.installed || app.actionLabel(app.cards_[0].game) != L"Brak pliku") return 629;
    app.notifications_.clear();
    app.game_sessions_.clear(); // The run from above ended.
    app.launch();
    if (app.pending_launch_ || custom_launches.size() != 2 || app.notifications_.empty() ||
        app.notifications_.back().title != L"Nie znaleziono pliku gry") return 630;
    check = std::make_unique<CustomGamesMessage>();
    check->generation = app.custom_token_->load();
    check->installed[id] = true;
    app.onCustomGames(std::move(check));
    if (!app.cards_[0].game.installed) return 631;

    // The top bar holds the stores of Platformy PC as tabs (this one shown); LB/RB move to the
    // neighbouring store in the same view.
    app.buildShelves(); app.openPcLibrary(GameSource::custom);
    {
        const auto tabs = app.libraryTabs();
        const int at = app.libraryTabIndex(tabs);
        if (!app.libraryTabsShown() || tabs.empty() || tabs[static_cast<size_t>(at)].id != sourceSlug(GameSource::custom)) return 648;
        if (tabs.size() > 1) {
            const auto view = app.libraryView();
            const int step = at > 0 ? -1 : 1;
            app.switchLibraryTab(step);
            if (!app.pc_source_ || tabs[static_cast<size_t>(at + step)].id != sourceSlug(*app.pc_source_) || app.libraryView() != view) return 649;
            app.openPcLibrary(GameSource::custom);
        }
    }

    // Another launcher instance reads the same list.
    {
        Launcher other;
        other.hwnd_ = window; other.execute_ = customExecute;
        other.loadCustomGames();
        other.buildShelves();
        const auto other_pcs = std::find_if(other.categories_.begin(), other.categories_.end(), [](const auto& item) { return item.id == "pc-platforms"; });
        if (other.custom_library_.size() != 1 || gameKey(other.custom_library_[0]) != key || other_pcs == other.categories_.end()) return 632;
    }

    // English: the library and the editor.
    i18n::setLanguage(i18n::Language::en);
    app.notifications_.clear();
    app.buildShelves(); app.openPcLibrary(GameSource::custom); settle();
    if (!capture(L"custom-games-library-english.png")) return 633;
    if (!settingsRow(settings_ui::Id::custom_game, id)) return 634;
    app.settingsActivate();
    app.customEditorFocus(Item::remove);
    app.customEditorAccept(); // First press only arms "Remove".
    if (!editor.confirm_remove || app.custom_games_.size() != 1) return 635;
    if (!capture(L"custom-game-editor-english.png")) return 636;
    i18n::setLanguage(i18n::Language::pl);
    app.customEditorKey(VK_RIGHT, false); // Moving away disarms it.
    if (editor.confirm_remove || editor.focus != Item::cancel) return 637;
    // Mouse: Cancel closes; the panel for a new game has no "Usuń".
    const auto layout = custom_game_ui::Layout::of(editor, app.renderer_.viewport().width, app.renderer_.viewport().height);
    const auto cancel = layout.button(Item::cancel), remove = layout.button(Item::remove);
    if (layout.hit((remove.left + remove.right) * 0.5f, (remove.top + remove.bottom) * 0.5f, false) ||
        layout.hit((remove.left + remove.right) * 0.5f, (remove.top + remove.bottom) * 0.5f, true) != Item::remove) return 641;
    app.click((cancel.left + cancel.right) * 0.5f * app.renderer_.viewport().scale, (cancel.top + cancel.bottom) * 0.5f * app.renderer_.viewport().scale, false);
    if (app.customEditorOpen() || app.custom_games_.size() != 1) return 638;

    // Remove: two presses; the tile, the shelf, the list entry and the history go.
    app.settingsActivate();
    app.customEditorFocus(Item::remove);
    app.customEditorKey(VK_RETURN, false);
    app.customEditorKey(VK_RETURN, false);
    if (app.customEditorOpen() || !app.custom_games_.empty() || !custom_games::load(list_file).games.empty() ||
        app.library_state_.last_played.contains("local/" + key) || category("pc-platforms") != app.categories_.end() ||
        listed(app.public_library_)) return 639;
    // The emptied page keeps "Dodaj grę"; Back leaves it, the Library page and Settings.
    if (!app.settings_state_.at(settings_ui::Page::custom_games) || app.settings_rows_.size() != 1 || app.settings_rows_[0].id != settings_ui::Id::custom_add) return 647;
    app.back(); app.back(); app.back(); app.back();
    if (app.settingsOpen()) return 640;

    // "Opcje uruchamiania…" of a Steam game: only its options (kept with the library state),
    // passed through steam.exe -applaunch. Stores started through their clients' links have none.
    {
        Game steam_game = steam; steam_game.kind = GameKind::steam; steam_game.installed = true;
        Game gog = steam_game; gog.source = GameSource::gog; gog.kind = GameKind::store; gog.source_id = "1";
        if (!app.launchEditable(steam_game) || app.launchEditable(gog)) return 650;
        app.openLaunchEditor(steam_game);
        if (!app.customEditorOpen() || editor.target != ui::Target::steam || editor.focus != Item::arguments || editor.editing() ||
            editor.shows(ui::file_field) || editor.shows(ui::name_field) || !editor.shows(ui::arguments_field)) return 651;
        app.customEditorKey(VK_UP, false);
        if (editor.focus != Item::arguments) return 652; // Nothing above it.
        app.customEditorInsert(L"-dx11 -skipintro");
        if (!capture(L"launch-options-steam.png")) return 653;
        app.customEditorKey(VK_DOWN, false);
        if (editor.focus != Item::save) return 654;
        app.customEditorKey(VK_RETURN, false);
        if (app.customEditorOpen() || app.launchArguments(steam_game) != L"-dx11 -skipintro" || !app.launchArguments(gog).empty()) return 655;
        custom_launches.clear();
        app.steam_executable_ = [] { return std::filesystem::path(L"C:\\Steam\\steam.exe"); };
        app.pending_game_ = steam_game; app.pending_launch_ = true; app.finishLaunch();
        if (custom_launches.size() != 1 || custom_launches[0].target != L"C:\\Steam\\steam.exe" ||
            custom_launches[0].arguments != L"-applaunch 9100 -dx11 -skipintro") return 656;
        // Emptied, the game starts through its plain link again.
        app.openLaunchEditor(steam_game);
        if (editor.fields[ui::arguments_field] != L"-dx11 -skipintro") return 657;
        pressWithControl(app, 'A'); app.customEditorKey(VK_BACK, false);
        app.customEditorSave();
        if (app.customEditorOpen() || app.library_state_.launch_arguments.contains(gameKey(steam_game))) return 658;
    }
    // A DRM-free game: its file, options and working directory; the choice survives scans.
    {
        drm_free::Entry entry;
        entry.folder = root / L"DRM Pirates"; std::filesystem::create_directories(entry.folder);
        entry.id = drm_free::folderId(entry.folder); entry.name = L"Space Pirates DRM"; entry.title = entry.name;
        entry.executable = exe; entry.arguments = L"-gog";
        app.drm_catalog_.roots.push_back(root);
        app.drm_catalog_.games.push_back(entry);
        app.rebuildDrmLibrary();
        const auto game = drm_free::makeGame(entry);
        if (!app.launchEditable(game)) return 659;
        app.openLaunchEditor(game);
        if (!app.customEditorOpen() || editor.target != ui::Target::drm_free || editor.focus != Item::file || editor.shows(ui::name_field) ||
            !editor.shows(ui::directory_field) || editor.fields[ui::arguments_field] != L"-gog") return 660;
        app.customEditorKey(VK_DOWN, false);
        if (editor.focus != Item::arguments) return 661;
        app.customEditorKey(VK_END, false); app.customEditorInsert(L" -windowed");
        if (!capture(L"launch-options-drm-free.png")) return 662;
        app.customEditorSave();
        const auto& saved = app.drm_catalog_.games.back();
        if (app.customEditorOpen() || saved.arguments != L"-gog -windowed" || !saved.manual_executable ||
            drm_free::action(saved).arguments != L"-gog -windowed" || drm_free::load(userConfigDirectory() / L"drm-free.json").catalog.games.empty()) return 663;
        // The file's "Wybierz…" keeps the options typed beside it.
        app.openLaunchEditor(drm_free::makeGame(saved));
        app.customEditorFocus(Item::file_browse); app.customEditorAccept();
        if (editor.fields[ui::arguments_field] != L"-gog -windowed" || editor.fields[ui::file_field] != exe.native()) return 664;
        app.closeCustomEditor();
    }
    return 0;
}
