// Included after LauncherUiTest. Ubisoft Connect products read from the client's cache are
// listed with Instaluj (handed to the client via uplay://install) and the Settings row reports
// the library. Mock executor only: no client, installer or game is started.
int LauncherUiTest::ubisoft(HWND window) {
    Launcher app;
    app.hwnd_ = window; app.execute_ = fakeExecute;
    if (!app.renderer_.initialize(window)) return 1800;
    app.settings_.enabled_pc_sources.insert("ubisoft");
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
    Game owned; owned.kind = GameKind::store; owned.source = GameSource::ubisoft;
    owned.source_id = "5555"; owned.name = L"Owned Ubisoft"; owned.installed = false; owned.account_id = "account";
    configureStoreActions(owned, {}, {});
    Game installed = owned; installed.source_id = "1234"; installed.name = L"Installed Ubisoft";
    installed.installed = true; installed.account_id.clear(); installed.last_played = 200;
    configureStoreActions(installed, {}, L"C:\\fixture\\Ubisoft");
    auto pc = std::make_unique<PcLibraryMessage>(); pc->generation = app.pc_token_->load();
    pc->library.visible = {owned, installed}; pc->library.ubisoft_account = true;
    app.onPcLibrary(std::move(pc));
    if (app.categories_[0].games.empty() || gameKey(app.categories_[0].games[0]) != "ubisoft:1234") return 1801;
    app.openPcLibrary(GameSource::ubisoft);
    const auto card = std::find_if(app.cards_.begin(), app.cards_.end(), [](const GameCard& entry) { return entry.game.source_id == "5555"; });
    if (app.cards_.size() != 2 || card == app.cards_.end()) return 1802;
    if (app.actionLabel(card->game) != L"Instaluj" || !app.actionEnabled(card->game)) return 1803;
    for (int i = 0; i < 40; ++i) { app.last_frame_ -= std::chrono::milliseconds(50); app.tick(); }
    if (!capture(L"ubisoft-library.png")) return 1804;
    app.openDetails(card->game);
    const auto count = dispatched.size();
    app.launch(); app.finishLaunch();
    if (dispatched.size() != count + 1 || dispatched.back() != L"uplay://install/5555" || !app.game_sessions_.empty()) return 1805;
    app.openSettings(); app.settings_state_.selectCategory(static_cast<int>(settings_ui::Category::library)); app.platforms_all_ = true; app.buildSettingsRows();
    const auto row = std::find_if(app.settings_rows_.begin(), app.settings_rows_.end(), [](const auto& entry) {
        return entry.id == settings_ui::Id::pc_platform && entry.source == GameSource::ubisoft;
    });
    if (row == app.settings_rows_.end() || row->description != L"Biblioteka: 2 · Zainstalowane: 1" || row->value != L"Opcje") return 1806;
    if (!capture(L"ubisoft-settings.png")) return 1807;
    // Without the client cache the row falls back to the local installation count.
    auto local = std::make_unique<PcLibraryMessage>(); local->generation = app.pc_token_->load();
    local->library.visible = {installed};
    app.onPcLibrary(std::move(local));
    app.buildSettingsRows();
    const auto plain = std::find_if(app.settings_rows_.begin(), app.settings_rows_.end(), [](const auto& entry) {
        return entry.id == settings_ui::Id::pc_platform && entry.source == GameSource::ubisoft;
    });
    if (plain == app.settings_rows_.end() || plain->description != L"Zainstalowane: 1") return 1808;
    return 0;
}
