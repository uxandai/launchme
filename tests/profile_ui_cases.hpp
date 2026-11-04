// Profil (src/app/profile.cpp, src/ui/profile_view.hpp): the drawer's profile widget opens the
// player's page; this week from the play journal, the latest achievements and twelve months from
// the achievement history, the games closest to 100 % and every platform; keys, pad, mouse and the
// wheel; a game opens Szczegóły; a platform its library. The desktop window's first top-bar icon
// opens the widgets themselves; Edytuj metadane → Czas gry counts on from the typed value;
// MultiMC / Prism is a PC platform with its own path row. Nothing reaches the network or Steam.
// Captures: profile-*.png, desktop-widgets-*.png, desktop-meta-playtime.png.
int LauncherUiTest::profilePage(HWND window) {
    using Modal = Launcher::Modal;
    using profile_ui::Section;
    namespace board = friends_layout::dashboard;
    Launcher app;
    app.hwnd_ = window; app.execute_ = fakeExecute;
    app.overlay_backend_.foreground = [window] { return window; };
    app.settings_.launch_animation = false; app.settings_.tour_done = true;
    app.settings_.glance = app.settings_.companion = false;
    app.read_sensors_ = [] { return sensors::Reading{true, 48, 61}; };
    app.discord_.running = [] { return false; };
    app.steam_accounts_ = [] { return std::vector<steam_accounts::Account>{{1, "fixture", L"Kasia Pikselowa", true, true}}; };
    if (!app.renderer_.initialize(window)) return 9700;
    const auto capture = [&](const wchar_t* name) {
        wchar_t folder[4096]{};
        const auto count = GetEnvironmentVariableW(L"LAUNCHME_UI_CAPTURE_DIR", folder, 4096);
        if (!count || count >= 4096) { app.paint(); return true; }
        app.notifications_.clear();
        return app.renderer_.captureForTest(std::filesystem::path(folder) / name, [&] { app.paint(); });
    };
    const auto settle = [&](float seconds) { for (float t = 0; t < seconds; t += 0.05f) app.tickProfile(0.05f); };
    const auto pump = [&] {
        MSG message{};
        while (PeekMessageW(&message, window, remote_message, remote_message, PM_REMOVE))
            app.onRemote(std::unique_ptr<RemoteMessage>(reinterpret_cast<RemoteMessage*>(message.lParam)));
    };

    // The library: six Steam games and a Prism instance with its own play time.
    app.settings_.enabled_pc_sources.insert("multimc");
    Game minecraft; minecraft.source = GameSource::multimc; minecraft.kind = GameKind::store; minecraft.source_id = "survival";
    minecraft.name = L"Survival 1.21"; minecraft.installed = true; minecraft.store_playtime_minutes = 95;
    minecraft.play_action = {L"C:\\Fixture\\prismlauncher.exe", L"--launch \"survival\""};
    app.store_library_ = {minecraft};
    auto library = std::make_unique<LibraryMessage>(); library->account = 1;
    const wchar_t* names[] = {L"Kroniki Mgły", L"Gwiezdny Kurier", L"Ogród Zegarów", L"Ostatnia Latarnia", L"Miasto Kości", L"Rzeka Snów"};
    for (uint32_t i = 0; i < 6; ++i) { Game game{9700 + i, names[i]}; game.installed = true; game.last_played = 1700000000 - i; library->games.push_back(game); }
    app.onLibrary(std::move(library)); app.showCategory(0);
    const auto key = [](uint32_t app_id) { return "steam:" + std::to_string(app_id); };

    // This week and the one before, as followed sessions left them.
    app.loadProfileData();
    const uint64_t now = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count());
    const int today = play_journal::dateOf(now);
    const uint64_t minute = 60000;
    const int plan[7][3] = {{0, 0, 0}, {70, 0, 25}, {130, 40, 0}, {0, 0, 0}, {45, 95, 0}, {180, 20, 60}, {50, 0, 35}};
    for (int day = 0; day < 7; ++day)
        for (int game = 0; game < 3; ++game)
            if (plan[day][game]) {
                const int date = play_journal::addDays(today, day - 6);
                const auto k = game == 2 ? gameKey(minecraft) : key(9700 + static_cast<uint32_t>(game));
                play_journal::launched(app.journal_, date, k, game == 2 ? minecraft.name : names[game]);
                play_journal::played(app.journal_, date, k, game == 2 ? minecraft.name : names[game], static_cast<uint64_t>(plan[day][game]) * minute);
            }
    play_journal::played(app.journal_, play_journal::addDays(today, -9), key(9703), names[3], 300 * minute);
    // Achievements: recent ones with icons and rarity, older ones over the year.
    using achievement_history::Unlock;
    std::vector<Unlock> mist, courier, garden;
    const wchar_t* titles[] = {L"Pierwsze kroki", L"Mistrz mgły", L"Bez śladu", L"Latarnik", L"Kartograf", L"Nocny marek", L"Kolekcjoner", L"Do świtu"};
    for (int i = 0; i < 8; ++i) {
        Unlock unlock{{}, "mist_" + std::to_string(i), titles[i], L"Opis osiągnięcia", L"C:\\Fixture\\icon" + std::to_wstring(i % 3) + L".png",
                      now - static_cast<uint64_t>(i) * 86400 * (i < 4 ? 1 : 9) - 600, i == 1 ? 2.4f : 30.0f + i * 5};
        (i % 2 ? courier : mist).push_back(unlock);
    }
    for (int month = 1; month <= 10; ++month)
        for (int n = 0; n < month % 4 + 1; ++n)
            garden.push_back({{}, "garden_" + std::to_string(month) + "_" + std::to_string(n), L"Zegarmistrz", {}, {}, now - static_cast<uint64_t>(month) * 30 * 86400 - n * 3600, -1});
    achievement_history::merge(app.history_, key(9700), names[0], mist, {38, 40});
    achievement_history::merge(app.history_, key(9701), names[1], courier, {12, 50});
    achievement_history::merge(app.history_, key(9702), names[2], garden, {25, 25});
    achievement_history::merge(app.history_, key(9704), names[4], {}, {7, 30});
    // The background read: the Steam picture and the achievements' icons (no Steam files here).
    std::atomic_int reads{0};
    app.profile_reader_ = [&reads](const ProfileScanRequest& request, const std::function<bool()>&) {
        ++reads;
        ProfileScanResult result;
        result.scanned = request.scanned;
        result.avatar = pngFixture(200, 64, 64);
        for (const auto& [url, folder] : request.icons) { (void)folder; result.icons[url] = pngFixture(static_cast<uint8_t>(60 + url.back() * 3), 32, 32); }
        return result;
    };

    // The drawer: the profile widget, wide, last; this week at a glance.
    app.tabDown(false); app.tabUp();
    if (!app.friendsOpen()) return 9701;
    const auto layout = app.dashboardLayout();
    const auto tile = std::find_if(layout.items.begin(), layout.items.end(), [](const auto& item) { return item.id == "profile"; });
    if (tile == layout.items.end() || tile->size != widgets::Size::wide) return 9702;
    DrawerDashboard glance; app.profileTile(glance);
    if (glance.profile_name != L"Kasia Pikselowa" || glance.profile_initials != L"KP" || glance.profile_value.find(L"w tym tygodniu") == std::wstring::npos ||
        glance.profile_week[5] != 1.0f || glance.profile_week[0] != 0) return 9703;
    app.friends_progress_ = 1;
    app.dashboard_tile_ = static_cast<int>(board::Tile::profile); app.dashboardEnsureVisible();
    if (!capture(L"profile-drawer-tile.png")) return 9704;

    // A on it: the page over the launcher, the drawer gone; a modal layer without hints.
    app.dashboardActivate(static_cast<int>(board::Tile::profile));
    if (!app.profile_.open || app.friendsOpen() || app.topModal() != Modal::profile || app.homeOverlayAvailable()) return 9705;
    app.friends_progress_ = 0; // The drawer has slid away (no frames tick here).
    ViewContext hints; app.inputHints(hints); if (!hints.hints.empty()) return 9706;
    auto& p = app.profile_;
    if (p.name != L"Kasia Pikselowa" || p.stats[1].value != L"3" || p.top.size() != 3 || p.top.front().key != key(9700) ||
        p.recent.empty() || p.recent.front().key != key(9700) || p.completion.empty() || p.platforms.size() != 2) return 9707;
    if (p.platforms[0].name != L"Steam" || p.platforms[0].account != L"Kasia Pikselowa" || !p.platforms[0].connected ||
        p.platforms[1].source != GameSource::multimc || p.platforms[1].brand != "minecraft") return 9708;
    if (p.history[2].value != L"1" || p.stats[3].value != L"4") return 9709; // Ogród Zegarów at 100 %; four unlocks this week.
    // A week-on-week change: 600 + 120 minutes this week against 300 the week before.
    if (p.stats[0].note.find(L"+") != 0 || !p.stats[0].good) return 9710;
    if (std::any_of(p.recent.begin(), p.recent.end(), [](const auto& u) { return u.icon; })) return 9711; // Icons come with the read.
    // Unlocks with an icon show it; Ogród Zegarów's have none and keep the game's cover.
    const auto withIcon = [&](const profile_ui::Unlock& u) { return u.icon == (u.key != key(9702)); };
    for (const auto end = GetTickCount64() + 5000; (p.loading || !p.avatar) && GetTickCount64() < end; Sleep(5)) pump();
    for (const auto end = GetTickCount64() + 5000; !std::all_of(p.recent.begin(), p.recent.end(), withIcon) && GetTickCount64() < end; Sleep(5)) pump();
    if (!p.avatar || p.loading || reads.load() != 1 || !std::all_of(p.recent.begin(), p.recent.end(), withIcon)) return 9712;
    if (!p.recent[1].rare || p.recent[1].rarity.find(L"2,4") == std::wstring::npos) return 9713;
    settle(2.2f);
    if (p.alpha != 1 || !capture(L"profile-top.png")) return 9714;

    // Keys: from Back to the week's games, down to the achievements; the row slides.
    if (p.focus.section != Section::close) return 9715;
    app.modalKey(VK_DOWN, false);
    if (p.focus.section != Section::top || p.focus.item != 0) return 9716;
    app.modalKey(VK_DOWN, false); app.modalKey(VK_DOWN, false);
    if (p.focus.section != Section::top || p.focus.item != 2) return 9717;
    app.modalKey(VK_DOWN, false);
    if (p.focus.section != Section::recent || p.focus.item != 0) return 9718;
    const int shown = profile_ui::Layout{app.renderer_.viewport().width, app.renderer_.viewport().height}.unlockColumns();
    for (int i = 0; i < shown; ++i) app.modalKey(VK_RIGHT, false);
    if (p.focus.item != shown || p.recent_first != 1) return 9719;
    settle(1.0f);
    if (!capture(L"profile-achievements.png")) return 9720;
    // Further down: the completion grid, then the platforms; past the last the page's end shows.
    app.modalKey(VK_DOWN, false);
    if (p.focus.section != Section::completion) return 9721;
    while (p.focus.section == Section::completion) app.modalKey(VK_DOWN, false);
    if (p.focus.section != Section::platforms) return 9722;
    app.modalKey(VK_DOWN, false); settle(1.0f);
    const profile_ui::Layout page{app.renderer_.viewport().width, app.renderer_.viewport().height};
    if (p.scroll != page.maxScroll(p) || p.focus.section != Section::platforms) return 9723;
    if (!capture(L"profile-bottom.png")) return 9724;
    // Home goes back to the top; the wheel scrolls (main.cpp: profileScroll).
    app.modalKey(VK_HOME, false); settle(1.0f);
    if (p.focus.section != Section::close || p.scroll != 0) return 9725;
    app.profileScroll(300); settle(1.0f);
    if (p.scroll != 300) return 9726;

    // The pointer: a hover focuses without an outline; above the bar nothing is hit.
    const auto view = app.renderer_.viewport();
    const auto row = page.item(p, {Section::top, 1});
    const float x = (row.left + row.right) * 0.5f, y = (row.top + row.bottom) * 0.5f - p.scroll;
    if (y > 82) { // At this scroll the second row is still under the bar or in view; either is fine.
        app.mouseMove(x * view.scale, y * view.scale);
        if (!p.pointer || p.focus != profile_ui::Focus{Section::top, 1}) return 9727;
    }
    // A click on a game: the page closes, Szczegóły opens on it.
    app.profileScroll(-1000); settle(1.0f);
    const auto first = page.item(p, {Section::top, 0});
    app.click((first.left + first.right) * 0.5f * view.scale, ((first.top + first.bottom) * 0.5f - p.scroll) * view.scale, false);
    if (p.open || app.screen_ != ScreenMode::details || app.detail_games_.empty() || gameKey(app.detail_games_.front()) != key(9700)) return 9728;
    settle(1.0f);
    if (p.alpha != 0) return 9729;
    app.back();

    // The pad: B closes it; it opens again from the drawer with the pad and A on a platform opens its library.
    app.openProfile(); settle(1.0f);
    PadReading pad{true, L"profile-pad"}; app.controller(pad);
    const auto press = [&](WORD button) { pad.buttons = button; app.controller(pad); pad.buttons = 0; app.controller(pad); };
    press(XINPUT_GAMEPAD_B);
    if (p.open || app.topModal() != Modal::none) return 9730;
    app.openProfile(); settle(1.0f);
    p.focus = {Section::platforms, 1};
    press(XINPUT_GAMEPAD_A);
    if (p.open || app.screen_ != ScreenMode::library || app.pc_source_ != GameSource::multimc || app.pc_library_.size() != 1) return 9731;
    app.back(); app.showCategory(0);

    // English: the same page.
    i18n::setLanguage(i18n::Language::en);
    app.openProfile(); settle(2.2f);
    const bool english = p.stats[0].label == L"Play time" && capture(L"profile-en.png");
    app.closeProfile(); settle(1.0f);
    i18n::setLanguage(i18n::Language::pl);
    if (!english) return 9732;

    // ---- The desktop window: the widgets icon comes first and opens the drawer itself.
    app.desktop_mode_ = true; app.audio_.setDesktop(true);
    app.buildShelves(); app.showCategory(0);
    for (int i = 0; i < 20; ++i) { app.last_frame_ -= std::chrono::milliseconds(50); app.tick(); }
    const auto tools = app.desktopMoreTools(); // Behind the title bar's "…".
    if (tools.empty() || tools.front() != desktop_ui::Tool::friends || app.desktopTools().back() != desktop_ui::Tool::more) return 9733;
    app.desktopToolPick(desktop_ui::Tool::friends);
    if (!app.friendsOpen()) return 9734;
    app.friends_progress_ = 1;
    if (!capture(L"desktop-widgets-drawer.png")) return 9735;
    app.dashboardActivate(static_cast<int>(board::Tile::profile));
    if (!p.open || app.friendsOpen()) return 9736;
    app.friends_progress_ = 0;
    for (const auto end = GetTickCount64() + 5000; p.loading && GetTickCount64() < end; Sleep(5)) pump();
    settle(2.2f);
    if (!capture(L"desktop-profile.png")) return 9737;
    // A platform card: the desktop library's own section for it.
    p.focus = {Section::platforms, 1}; app.modalKey(VK_RETURN, false);
    if (p.open || app.desktop_section_ != "source:multimc") return 9738;
    settle(1.0f);

    // ---- Edytuj metadane → Czas gry: Prism said 95 minutes; typing 10:30 counts on from there.
    if (app.gamePlaytime(minecraft, std::nullopt) != 95u) return 9739;
    app.desktopMetaOpen(minecraft);
    auto& meta = app.desktop_meta_;
    if (!meta.open || meta.fields[7] != metadata::playtimeText(95) || meta.playtime_note.find(L"MultiMC") == std::wstring::npos) return 9740;
    if (!capture(L"desktop-meta-playtime.png")) return 9741;
    meta.focus = static_cast<int>(desktop_ui::MetaItem::playtime); meta.fields[7] = L"zła"; meta.caret = 3;
    app.desktopMetaActivate(static_cast<int>(desktop_ui::MetaItem::save));
    if (!meta.open || app.notifications_.empty()) return 9742; // Not a time: it says so and stays.
    meta.fields[7] = L"10:30";
    app.desktopMetaActivate(static_cast<int>(desktop_ui::MetaItem::save));
    if (meta.open || app.gamePlaytime(minecraft, std::nullopt) != 630u) return 9743;
    // Prism counts a session on: the shown time grows with it.
    auto played = minecraft; played.store_playtime_minutes = 125;
    if (app.gamePlaytime(played, std::nullopt) != 660u) return 9744;
    // Emptied: automatic again.
    app.desktopMetaOpen(minecraft); meta.fields[7].clear();
    app.desktopMetaActivate(static_cast<int>(desktop_ui::MetaItem::save));
    if (app.gamePlaytime(minecraft, std::nullopt) != 95u) return 9745;
    app.desktop_mode_ = false; app.audio_.setDesktop(false);

    // ---- MultiMC / Prism in Platformy PC: its path row under the platform's switch.
    app.openSettings(); app.openPlatformSettings(GameSource::multimc);
    if (app.settings_state_.platform() != GameSource::multimc ||
        std::any_of(app.settings_rows_.begin(), app.settings_rows_.end(), [](const auto& r) { return r.source != GameSource::multimc; })) return 9749;
    const auto path_row = std::find_if(app.settings_rows_.begin(), app.settings_rows_.end(), [](const auto& r) { return r.id == settings_ui::Id::multimc_path; });
    if (path_row == app.settings_rows_.end()) return 9746;
    if (!capture(L"profile-settings-multimc.png")) return 9747;
    app.notifications_.clear();
    app.setMultimcPath(L"C:\\Fixture\\notepad.exe"); // Not a MultiMC: it says so, nothing changes.
    if (!app.settings_.multimc_path.empty() || app.notifications_.empty()) return 9750;
    const std::filesystem::path prism = L"C:\\Fixture\\PrismLauncher\\prismlauncher.exe";
    app.setMultimcPath(prism); app.saveSettings();
    if (app.settings_.multimc_path != prism || loadAppSettings().multimc_path != prism) return 9748;
    app.setMultimcPath({}); app.saveSettings();
    if (!loadAppSettings().multimc_path.empty()) return 9751;
    app.back();
    return 0;
}
