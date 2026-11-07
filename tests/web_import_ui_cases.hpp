// Included after LauncherUiTest. EA and Battle.net libraries pasted from the store's website:
// Settings opens the browser (mock executor) and focuses the paste field; the worker's result
// is injected as its message, the scan result as the PC library message. No network, no client.
int LauncherUiTest::webImports(HWND window) {
    Launcher app;
    app.hwnd_ = window; app.execute_ = fakeExecute;
    if (!app.renderer_.initialize(window)) return 1900;
    app.settings_.show_uninstalled = true;
    const auto capture = [&](const wchar_t* name) {
        wchar_t folder[4096]{};
        if (!GetEnvironmentVariableW(L"LAUNCHME_UI_CAPTURE_DIR", folder, 4096)) return true;
        InvalidateRect(window, nullptr, FALSE);
        return app.renderer_.captureForTest(std::filesystem::path(folder) / name, [&] { app.paint(); });
    };
    auto steam = std::make_unique<LibraryMessage>(); steam->account = 1;
    Game recent{8800, L"Steam fixture"}; recent.last_played = 100; steam->games.push_back(recent);
    app.onLibrary(std::move(steam));
    const auto find = [&](settings_ui::Id id, GameSource source = GameSource::steam) {
        return std::find_if(app.settings_rows_.begin(), app.settings_rows_.end(), [&](const auto& row) {
            return row.id == id && (id != settings_ui::Id::pc_platform || row.source == source);
        });
    };
    const auto select = [&](auto row) {
        app.settings_state_.options = true;
        app.settings_state_.selected[app.settings_state_.category] = static_cast<int>(row - app.settings_rows_.begin());
    };
    app.openSettings(); app.settings_state_.selectCategory(static_cast<int>(settings_ui::Category::library));
    app.platforms_all_ = true; app.buildSettingsRows(); // The stores that are off: "Więcej platform" open.

    // EA: Połącz opens ea.com's sign-in, then the silent token page, and focuses the paste field.
    auto ea_row = find(settings_ui::Id::pc_platform, GameSource::ea);
    if (ea_row == app.settings_rows_.end() || ea_row->value != L"Opcje") return 1901;
    select(ea_row); app.settings_state_.editing = true;
    app.settingsActivate();
    if (app.settings_state_.platform() != GameSource::ea || app.settings_rows_[0].id != settings_ui::Id::pc_connect) return 1916;
    const auto opened_before = dispatched.size();
    app.settingsActivate(); // Connect within EA, not from the store overview.
    if (dispatched.size() != opened_before + 2 || dispatched[dispatched.size() - 2] != ea::login_url || dispatched.back() != ea::token_url ||
        !app.ea_import_.awaiting || !app.editingSecret() ||
        app.settings_rows_[app.settings_state_.row()].id != settings_ui::Id::ea_callback) return 1902;
    if (!capture(L"ea-import-paste.png")) return 1903;
    // The worker's result: a saved list with one owned game.
    Game owned; owned.source = GameSource::ea; owned.kind = GameKind::store; owned.installed = false;
    owned.source_id = "1026480"; owned.name = L"Dead Space"; owned.account_id = "1000123456789";
    web_import::Snapshot snapshot{"1000123456789", 1790000000, {owned}};
    app.cancelWebImport(GameSource::ea);
    app.ea_import_.busy = true; app.ea_import_.generation = 7;
    auto stale = std::make_unique<StoreImportMessage>(); stale->source = GameSource::ea; stale->generation = 6; stale->snapshot = snapshot;
    app.onStoreImport(std::move(stale));
    if (!app.ea_import_.busy || app.ea_import_.snapshot) return 1904; // An older import cannot land.
    auto done = std::make_unique<StoreImportMessage>(); done->source = GameSource::ea; done->generation = 7; done->snapshot = snapshot;
    app.onStoreImport(std::move(done));
    if (app.ea_import_.busy || !app.ea_import_.snapshot || !app.settings_.enabled_pc_sources.contains("ea")) return 1905;
    // The scan merges the saved list (as refreshPcLibraries' worker does).
    auto pc = std::make_unique<PcLibraryMessage>(); pc->generation = app.pc_token_->load();
    pc->library = mergeImportedLibrary({}, snapshot.games); pc->ea_import = snapshot;
    app.onPcLibrary(std::move(pc));
    app.buildSettingsRows();
    ea_row = find(settings_ui::Id::pc_connect, GameSource::ea);
    if (ea_row == app.settings_rows_.end() || ea_row->value != L"Zmień konto" || ea_row->description != L"Biblioteka: 1 · Zainstalowane: 0" ||
        find(settings_ui::Id::ea_refresh) == app.settings_rows_.end() || find(settings_ui::Id::ea_remove) == app.settings_rows_.end()) return 1906;
    if (!capture(L"ea-import-settings.png")) return 1907;
    app.openPcLibrary(GameSource::ea);
    if (app.cards_.size() != 1 || app.actionLabel(app.cards_[0].game) != L"Instaluj") return 1908;
    app.openDetails(app.cards_[0].game);
    auto count = dispatched.size();
    app.launch(); app.finishLaunch();
    if (dispatched.size() != count + 1 || dispatched.back() != L"origin2://game/launch?offerIds=1026480&autoDownload=1" || !app.game_sessions_.empty()) return 1909;

    // Battle.net: Importuj opens the sign-in and the game list pages, then the paste field.
    app.openSettings(); app.settings_state_.selectCategory(static_cast<int>(settings_ui::Category::library)); app.buildSettingsRows();
    app.openPlatformSettings(GameSource::battlenet);
    auto import_row = find(settings_ui::Id::battlenet_import);
    if (import_row == app.settings_rows_.end() || import_row->value != L"Importuj") return 1910;
    select(import_row);
    count = dispatched.size();
    app.settingsActivate();
    if (dispatched.size() != count + 2 || dispatched.back() != L"https://account.battle.net/api/games-and-subs" || !app.battlenet_import_.awaiting ||
        !app.editingSecret() || app.settings_rows_[app.settings_state_.row()].id != settings_ui::Id::battlenet_callback) return 1911;
    if (!capture(L"battlenet-import-paste.png")) return 1912;
    app.cancelWebImport(GameSource::battlenet);
    if (app.battlenet_import_.awaiting || find(settings_ui::Id::battlenet_callback) != app.settings_rows_.end()) return 1913;
    // A pasted page that is not the list is reported, nothing is saved.
    app.battlenet_import_.busy = true; app.battlenet_import_.generation = 3;
    auto invalid = std::make_unique<StoreImportMessage>(); invalid->source = GameSource::battlenet; invalid->generation = 3; invalid->invalid = true;
    app.onStoreImport(std::move(invalid));
    if (app.battlenet_import_.busy || app.battlenet_import_.snapshot || app.notifications_.empty()) return 1914;
    // Removing the EA list asks the worker; its result clears the saved state.
    app.ea_import_.busy = true; app.ea_import_.generation = 8;
    auto cleared = std::make_unique<StoreImportMessage>(); cleared->source = GameSource::ea; cleared->generation = 8; cleared->cleared = true;
    app.onStoreImport(std::move(cleared));
    app.openPlatformSettings(GameSource::ea); app.buildSettingsRows();
    if (app.ea_import_.snapshot || find(settings_ui::Id::ea_remove) != app.settings_rows_.end()) return 1915;
    return 0;
}
