// Ustawienia → System → Kopia i przenoszenie: the export's parts with their sizes, the password a sealed
// part asks for, a real export into the test's data folder with its result row, the bar of a running job,
// the backup's import page and its staged import, and Playnite's library (tests/fixtures/playnite) read,
// previewed and staged. Staged imports are discarded at the end: nothing restarts.
int LauncherUiTest::transferSettings(HWND window) {
    using namespace settings_ui;
    using TJob = launcher_state::TransferState::Job;
    Launcher app; app.hwnd_ = window; app.execute_ = fakeExecute;
    if (!app.renderer_.initialize(window)) return 11000;
    const auto config = userConfigDirectory();
    std::filesystem::create_directories(config / L"themes");
    { std::ofstream(config / L"themes" / L"mine.json") << "{\"name\":\"mine\"}"; }
    { std::ofstream(config / L"test_api_key.txt") << "not-a-real-key"; } // A sealed part with something in it.
    const auto capture = [&](const wchar_t* name) {
        wchar_t folder[4096]{}; const auto count = GetEnvironmentVariableW(L"LAUNCHME_UI_CAPTURE_DIR", folder, 4096);
        app.notifications_.clear();
        if (!count || count >= 4096) { app.paint(); return true; }
        std::filesystem::create_directories(folder);
        app.settings_motion_.valid = false;
        return app.renderer_.captureForTest(std::filesystem::path(folder) / name, [&] { app.paint(); });
    };
    const auto pump = [&] {
        MSG m{}; while (PeekMessageW(&m, window, remote_message, remote_message, PM_REMOVE))
            app.onRemote(std::unique_ptr<RemoteMessage>(reinterpret_cast<RemoteMessage*>(m.lParam)));
    };
    const auto idle = [&] {
        for (const auto until = GetTickCount64() + 20000; GetTickCount64() < until;) { pump(); if (app.transfer_.job == TJob::none) return true; Sleep(5); }
        return false;
    };
    const auto find = [&](Id id, const std::string& key = {}) {
        const auto row = std::find_if(app.settings_rows_.begin(), app.settings_rows_.end(), [&](const Row& r) { return r.id == id && (key.empty() || r.key == key); });
        return row == app.settings_rows_.end() ? -1 : static_cast<int>(row - app.settings_rows_.begin());
    };
    const auto activate = [&](int row) {
        app.settings_state_.options = true; app.settings_state_.editing = false;
        app.settings_state_.selected[app.settings_state_.category] = row;
        app.settingsActivate();
    };

    // System: the page's row; on it every part, the sizes measured in the background.
    app.openSettings();
    app.settings_state_.selectCategory(static_cast<int>(Category::system)); app.buildSettingsRows();
    const auto entry = std::find_if(app.settings_rows_.begin(), app.settings_rows_.end(), [](const Row& r) { return r.control == Control::page && r.page == Page::transfer; });
    if (entry == app.settings_rows_.end()) return 11001;
    app.openSettingsPage(Page::transfer);
    if (app.transfer_.job != TJob::measuring || !idle() || app.transfer_.sizes.size() != profile_transfer::parts.size() ||
        app.transfer_.sizes.at(profile_transfer::Part::settings).files == 0) return 11002;
    if (find(Id::transfer_part, "settings") < 0 || find(Id::transfer_part, "companion") < 0 || find(Id::transfer_export) < 0 ||
        find(Id::transfer_import) < 0 || find(Id::transfer_password) >= 0 || app.settings_rows_[find(Id::transfer_export)].locked ||
        app.settings_rows_[find(Id::transfer_part, "settings")].description.find(L"Pliki: 1 · ") == std::wstring::npos) return 11003;
    if (!capture(L"settings-transfer.png")) return 11004;
    app.desktop_mode_ = true;
    if (!capture(L"desktop-settings-transfer.png")) return 11004;
    app.desktop_mode_ = false;

    // A sealed part: the password row, and no export without it.
    activate(find(Id::transfer_part, "keys"));
    if (!app.transfer_.export_parts.contains(profile_transfer::Part::keys) || find(Id::transfer_password) < 0 ||
        !app.settings_rows_[find(Id::transfer_export)].locked) return 11005;
    activate(find(Id::transfer_password));
    if (!app.settings_state_.editing) return 11006;
    app.settings_secret_input_ = L"hasło testowe";
    app.settingsActivate();
    if (app.transfer_.password != L"hasło testowe" || app.settings_rows_[find(Id::transfer_export)].locked) return 11007;

    // A running job's bar with Anuluj.
    app.transfer_.job = TJob::exporting; app.transfer_.done = 40ull << 20; app.transfer_.total = 100ull << 20;
    app.buildSettingsRows();
    {
        const int status = find(Id::transfer_status);
        if (status != 0 || std::abs(app.settings_rows_[status].progress - 0.4f) > 0.001f || app.settings_rows_[status].value != L"Anuluj") return 11008;
    }
    app.settings_state_.selected[app.settings_state_.category] = 0; app.ensureSettingVisible();
    if (!capture(L"settings-transfer-progress.png")) return 11009;
    app.desktop_mode_ = true;
    if (!capture(L"desktop-settings-transfer-progress.png")) return 11009;
    app.desktop_mode_ = false;
    app.transfer_.job = TJob::none;

    // The export itself: one ZIP, its result in the status row.
    const auto exports = config / L"exports-test";
    std::filesystem::create_directories(exports);
    app.transferExport(exports);
    if (app.transfer_.job != TJob::exporting || !idle()) return 11010;
    std::filesystem::path zip;
    for (const auto& item : std::filesystem::directory_iterator(exports)) zip = item.path();
    if (zip.empty() || !zip.filename().wstring().starts_with(L"launchme_backup_") || app.transfer_.status_error ||
        app.transfer_.status.find(zip.filename().wstring()) == std::wstring::npos || find(Id::transfer_status) != 0) return 11011;

    // Its import page: the parts it has, merge by default, staged with the password.
    std::wstring error;
    app.transfer_.zip = zip;
    app.transfer_.manifest = profile_transfer::inspect(zip, error);
    if (!app.transfer_.manifest || !app.transfer_.manifest->sealed) return 11012;
    app.transfer_.import_parts.clear();
    for (const auto& [part, info] : app.transfer_.manifest->parts) app.transfer_.import_parts.insert(part);
    app.transfer_.status.clear();
    app.openSettingsPage(Page::transfer_import);
    if (find(Id::transfer_part, "keys") < 0 || find(Id::transfer_part, "companion") >= 0 || find(Id::transfer_mode) < 0 ||
        find(Id::transfer_paths) < 0 || find(Id::transfer_password) < 0 || app.settings_rows_[find(Id::transfer_apply)].locked) return 11013;
    if (!capture(L"settings-transfer-import.png")) return 11014;
    app.transferStage();
    if (!idle() || !app.transfer_.restart_ready || !profile_transfer::pending(config)) return 11015;
    if (find(Id::transfer_status) != 0 || app.settings_rows_[0].value != L"Uruchom ponownie") return 11016;
    profile_transfer::discardPending(config);
    app.transfer_.restart_ready = false;
    app.closeSettingsPage();

    // Playnite: its library read in the background, the preview, its options, staged.
    app.openSettingsPage(Page::playnite);
    if (find(Id::playnite_source) < 0 || find(Id::playnite_source, "zip") < 0 || find(Id::playnite_option) >= 0) return 11017;
    app.transferReadPlaynite(std::filesystem::path(__FILE__).parent_path() / L"fixtures" / L"playnite" / L"library");
    if (!idle() || !app.transfer_.playnite || app.transfer_.playnite->games.size() != 7) return 11018;
    {
        const int summary = find(Id::transfer_status);
        if (summary < 0 || !app.settings_rows_[summary].locked || app.settings_rows_[summary].description.find(L"Ze sklepów: 3") != 0 ||
            find(Id::playnite_option, "categories") < 0 || find(Id::playnite_apply) < 0) return 11019;
    }
    activate(find(Id::playnite_option, "tags"));
    if (app.transfer_.playnite_options.tags || app.settings_rows_[find(Id::playnite_option, "tags")].checked) return 11020;
    if (!capture(L"settings-playnite.png")) return 11021;
    app.desktop_mode_ = true;
    if (!capture(L"desktop-settings-playnite.png")) return 11021;
    app.desktop_mode_ = false;
    activate(find(Id::playnite_apply));
    if (!idle() || !app.transfer_.restart_ready || !profile_transfer::pending(config) || app.transfer_.playnite) return 11022;
    profile_transfer::discardPending(config);
    std::error_code ec;
    std::filesystem::remove_all(exports, ec);
    std::filesystem::remove_all(config / L"themes", ec);
    std::filesystem::remove(config / L"test_api_key.txt", ec);
    return 0;
}
