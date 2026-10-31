// The store overview never grows when accounts connect. Each cog owns one page;
// async callbacks keep their own input, and returning restores the overview focus.
int LauncherUiTest::pcSettings(HWND window) {
    using namespace settings_ui;
    Launcher app; app.hwnd_ = window; app.execute_ = fakeExecute;
    if (!app.renderer_.initialize(window)) return 2000;
    auto gog_service = std::make_unique<FakeGog>(); auto* gog = gog_service.get(); app.gog_service_ = std::move(gog_service);
    auto epic_service = std::make_unique<FakeEpic>(); auto* epic = epic_service.get(); app.epic_service_ = std::move(epic_service);
    auto steam = std::make_unique<LibraryMessage>(); steam->account = 1; steam->games = {Game{620, L"Portal 2"}};
    app.onLibrary(std::move(steam));
    for (const auto source : pc_sources) {
        Game game; game.kind = GameKind::store; game.source = source; game.source_id = "fixture"; game.name = L"Local game";
        app.store_library_.push_back(game); app.settings_.enabled_pc_sources.insert(sourceSlug(source));
    }
    app.gog_state_.state = gog::State::connected; app.gog_state_.account = "gog-fixture";
    app.gog_state_.games = {app.store_library_[0]}; app.gog_state_.synced_at = 1790000000;
    app.epic_state_.state = epic::State::connected; app.epic_state_.account = "epic-fixture";
    app.epic_state_.games = {app.store_library_[4]}; app.epic_state_.synced_at = 1790000000;
    app.ea_import_.snapshot = web_import::Snapshot{"ea-fixture", 1790000000, {app.store_library_[2]}};
    app.battlenet_import_.snapshot = web_import::Snapshot{"battlenet", 1790000000, {app.store_library_[1]}};
    app.ubisoft_account_ = true;
    app.openSettings(); app.settings_state_.selectCategory(static_cast<int>(settings_ui::Category::library));
    app.pc_scanning_ = false; app.buildSettingsRows();
    // Biblioteka opens with its "Platformy" section: DRM-free folders first, then the stores, then
    // the library's other rows.
    // The DRM-free folders, Emulacja's tile, Steam (its own section), then the stores by their names
    // (Launcher::platformOrder; the tile stands after its first entry).
    const auto order = app.platformOrder();
    const auto rowOf = [](size_t i) { return static_cast<int>(i == 0 ? 0 : i + 1); };
    const auto position = [&](GameSource source) { return rowOf(static_cast<size_t>(std::find(order.begin(), order.end(), source) - order.begin())); };
    const auto overview = [&] {
        if (app.settings_state_.platform() || app.settings_rows_.size() <= order.size() + 1 || order.size() != pc_sources.size() + 2 ||
            order[0] != GameSource::drmfree || order[1] != GameSource::steam || !optionsTile(app.settings_rows_[1]) ||
            app.settings_rows_[0].section.empty() || app.settings_rows_[2].section.empty() || app.settings_rows_[order.size() + 1].id == Id::pc_platform) return false;
        for (size_t i = 0; i < order.size(); ++i)
            if (app.settings_rows_[rowOf(i)].id != Id::pc_platform || app.settings_rows_[rowOf(i)].source != order[i]) return false;
        for (size_t i = 3; i < order.size(); ++i)
            if (CompareStringEx(LOCALE_NAME_USER_DEFAULT, LINGUISTIC_IGNORECASE, sourceName(order[i - 1]), -1, sourceName(order[i]), -1, nullptr, nullptr, 0) == CSTR_GREATER_THAN) return false;
        return true;
    };    const auto select = [&](Id id) {
        const auto row = std::find_if(app.settings_rows_.begin(), app.settings_rows_.end(), [&](const auto& r) { return r.id == id; });
        if (row == app.settings_rows_.end()) return false;
        app.settings_state_.selected[app.settings_state_.category] = static_cast<int>(row - app.settings_rows_.begin());
        app.settings_state_.options = true; app.settings_state_.editing = false; app.ensureSettingVisible(); return true;
    };
    const auto capture = [&](const wchar_t* name) {
        wchar_t folder[32768]{};
        if (!GetEnvironmentVariableW(L"LAUNCHME_UI_CAPTURE_DIR", folder, static_cast<DWORD>(std::size(folder)))) return true;
        app.settings_motion_.valid = false;
        return app.renderer_.captureForTest(std::filesystem::path(folder) / name, [&] { app.paint(); });
    };
    if (!overview() || !capture(L"pc-settings-overview.png")) return 2001;
    // GOG sits below the fold once Steam leads the list: bring it into view before clicking its cog.
    app.settings_state_.selected[app.settings_state_.category] = position(GameSource::gog); app.ensureSettingVisible();
    app.settings_motion_.valid = false; app.paint();
    const auto view = app.renderer_.viewport(); const auto layout = Layout::of(view.width, view.height, app.settings_state_, app.settings_rows_);
    const auto cog = layout.pluginCog(position(GameSource::gog), app.settings_state_.offset());
    const auto dispatched_before = dispatched.size();
    app.settingsClick((cog.left + cog.right) * 0.5f, (cog.top + cog.bottom) * 0.5f);
    if (app.settings_state_.platform() != GameSource::gog) return 2002;
    if (app.settings_rows_.size() != 4) return 2025;
    if (!gog->commands.empty() || dispatched.size() != dispatched_before) return 2026;
    for (const auto& row : app.settings_rows_) if (row.source != GameSource::gog || row.title.find(L"GOG") != row.title.npos) return 2003;
    if (!capture(L"pc-settings-gog.png") || !select(Id::gog_refresh)) return 2004;
    app.settingsActivate();
    if (gog->commands != std::vector<gog::Command>{gog::Command::sync} || !epic->commands.empty()) return 2005;
    app.retryStore();
    if (gog->commands.size() != 2 || !epic->commands.empty()) return 2006; // F5 refreshes only the open account.
    if (!select(Id::gog_logout)) return 2007;
    app.settingsActivate();
    if (!app.gog_state_.account.empty() || app.settings_rows_.size() != 3 || app.settings_state_.platform() != GameSource::gog) return 2008;
    app.back(); if (!overview() || app.settings_state_.row() != position(GameSource::gog)) return 2009;
    // Pad/keyboard uses Right -> cog -> Enter, and Back restores a scrolled list.
    app.settingsMove(position(GameSource::xbox) - position(GameSource::gog)); const auto parent_scroll = app.settings_state_.offset();
    if (!capture(L"pc-settings-overview-end.png")) return 2027; // The last stores with their marks (Xbox).
    app.settingsAdjust(1); app.settingsActivate();
    // Signed out: the account helper, Konto, Biblioteka, the (locked) own install, excluded games, the Xbox app.
    if (app.settings_state_.platform() != GameSource::xbox || app.settings_rows_.size() != 6 || app.settings_rows_[0].id != Id::xbox_helper ||
        !select(Id::pc_client)) return 2010;
    if (!capture(L"pc-settings-xbox.png")) return 2011;
    const auto client_actions = dispatched.size(); app.settingsActivate();
    if (dispatched.size() != client_actions + 1 || dispatched.back() != L"ms-windows-store://pdp/?ProductId=9MV0B5HZVK9Z") return 2024;
    app.back(); if (!overview() || app.settings_state_.row() != position(GameSource::xbox) || app.settings_state_.offset() != parent_scroll) return 2012;
    // Each remaining page contains only its own actions, not all connected accounts.
    for (const auto source : {GameSource::epic, GameSource::ea, GameSource::battlenet, GameSource::ubisoft}) {
        app.openPlatformSettings(source);
        for (const auto& row : app.settings_rows_) if (row.source != source || row.id == Id::pc_platform) return 2013;
        const auto filename = std::wstring(L"pc-settings-") + text_encoding::wide(sourceSlug(source)) + L".png";
        if (!capture(filename.c_str())) return 2014;
        app.back(); if (!overview()) return 2015;
    }
    // A callback stays selected across background scans; Back cancels it, then returns to the overview.
    app.openPlatformSettings(GameSource::gog); app.gogCommand(gog::Command::connect);
    gog::Snapshot signing; signing.state = gog::State::signing_in; signing.browser_url = L"https://example.invalid/signin";
    gog->result = signing; app.pollGog();
    if (!app.editingSecret() || app.settings_rows_[app.settings_state_.row()].id != Id::gog_callback) return 2016;
    app.settings_secret_input_ = L"masked-fixture"; app.buildSettingsRows();
    if (!app.editingSecret() || app.settings_secret_input_ != L"masked-fixture") return 2017;
    app.back();
    if (app.editingSecret() || !app.settings_secret_input_.empty() || app.settings_state_.platform() != GameSource::gog || gog->commands.back() != gog::Command::cancel) return 2018;
    app.back(); if (!overview()) return 2019;
    // Late GOG results must not clear or steal the Epic input on a different page.
    app.openPlatformSettings(GameSource::epic); app.epic_state_.state = epic::State::signing_in; app.gog_state_.state = gog::State::signing_in;
    app.buildSettingsRows(); if (!select(Id::epic_callback)) return 2020;
    app.settings_state_.editing = true; app.settings_secret_input_ = L"epic-input";
    gog::Snapshot ready; ready.state = gog::State::connected; ready.account = "restored-gog";
    gog->result = ready; app.pollGog();
    if (!app.editingSecret() || app.settings_state_.platform() != GameSource::epic || app.settings_secret_input_ != L"epic-input" ||
        app.settings_rows_[app.settings_state_.row()].id != Id::epic_callback) return 2021;
    app.back(); app.back(); if (!overview()) return 2022;
    // Changing the left category rail leaves the platform subpage without stale headings/selection.
    app.openPlatformSettings(GameSource::ubisoft);
    const auto rail = layout.rail(static_cast<int>(settings_ui::Category::library));
    app.settingsClick(rail.left + 10, rail.top + 10);
    if (app.settings_state_.platform() || app.settings_state_.category != static_cast<int>(settings_ui::Category::library)) return 2023;
    return 0;
}
