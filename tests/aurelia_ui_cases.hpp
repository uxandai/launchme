// End-to-end native UI with an injected Aurelia protocol, no service calls or real account actions.
int LauncherUiTest::aureliaIntegration(HWND window) {
    using C = aurelia::Command; using A = aurelia_ui::Action; using P = aurelia_ui::Page;
    Launcher app; app.hwnd_ = window; app.execute_ = fakeExecute;
    // Aurelia's roster has no avatars: they are looked up per friend (here without the network).
    auto avatar_requests = std::make_shared<std::atomic_int>(0);
    app.fetch_friend_avatar_ = [avatar_requests](const SteamFriend& person) {
        if (person.avatar_url.empty()) ++*avatar_requests;
        return std::vector<uint8_t>{};
    };
    app.settings_.companion = app.settings_.glance = app.settings_.launch_animation = false;
    app.settings_.tour_done = true; app.settings_.aurelia = false;
    const auto settings = parseAppSettings("aurelia=1\naurelia_intro_done=1\naurelia_standalone=902\naurelia_standalone=../evil\n");
    if (!settings.aurelia || !settings.aurelia_intro_done || settings.aurelia_standalone.size() != 1 || !settings.aurelia_standalone.contains("902")) return 10350;
    app.overlay_backend_.foreground = [window] { return window; };
    if (!app.renderer_.initialize(window)) return 10300;
    const auto capture = [&](const wchar_t* name) {
        wchar_t folder[4096]{}; const auto count = GetEnvironmentVariableW(L"LAUNCHME_UI_CAPTURE_DIR", folder, 4096);
        app.notifications_.clear();
        if (!count || count >= 4096) { app.paint(); return true; }
        std::filesystem::create_directories(folder);
        return app.renderer_.captureForTest(std::filesystem::path(folder) / name, [&] { app.paint(); });
    };
    const auto pump = [&] {
        MSG m{}; while (PeekMessageW(&m, window, remote_message, remote_message, PM_REMOVE))
            app.onRemote(std::unique_ptr<RemoteMessage>(reinterpret_cast<RemoteMessage*>(m.lParam)));
    };
    const auto wait = [&](const auto& ready) {
        for (const auto until = GetTickCount64() + 5000; GetTickCount64() < until;) { pump(); if (ready()) return true; Sleep(5); }
        return false;
    };
    struct Fake {
        std::mutex mutex; std::vector<aurelia::Request> calls;
        std::atomic_bool bare_friends{false}, send_failure{false}, download{false}, stop_download{false}, helper_failure{false}, stop_failure{false};
        std::atomic_int helper_calls{0};
        std::atomic_bool logged_in{true}, bad_health{false}, qr_approve{false};
        std::atomic_uint64_t account{76561198000000001ULL};
        int count(C c) { std::lock_guard lock(mutex); return static_cast<int>(std::count_if(calls.begin(), calls.end(), [c](const auto& r) { return r.command == c; })); }
        aurelia::Request last(C c) { std::lock_guard lock(mutex); for (auto it = calls.rbegin(); it != calls.rend(); ++it) if (it->command == c) return *it; return {}; }
    };
    auto fake = std::make_shared<Fake>();
    app.aurelia_transport_ = [fake](const aurelia::Request& r, const aurelia::Cancel& cancel, const aurelia::Event& event) {
        { std::lock_guard lock(fake->mutex); fake->calls.push_back(r); }
        const auto ok = [](std::string_view s) { return aurelia::Reply{true, false, *json::parse(s)}; };
        switch (r.command) {
        case C::health: return fake->bad_health ? ok(R"({"unexpected":true})") : ok(std::string("{\"logged_in\":") +
            (fake->logged_in ? "true" : "false") + ",\"steam_id\":" + std::to_string(fake->account.load()) + ",\"account\":\"Aurelia fixture\"}");
        case C::library: return ok(R"([{"app_id":902,"name":"The Moonlit Archive","is_installed":true,"is_owned":true,"install_path":"D:\\SteamLibrary\\Fixture","playtime_forever_minutes":125},{"app_id":906,"name":"Library worker fixture","is_installed":false,"is_owned":true}])");
        // Steam's collections as Aurelia keeps them: Ulubione, Ukryte, one of the player's, a dynamic one (no list).
        case C::collections_list: return ok(R"({"namespace_version":7,"collections":[{"id":"favorite","name":"","kind":"built-in","dynamic":false,"count":1},{"id":"hidden","name":"","kind":"built-in","dynamic":false,"count":1},{"id":"uc-moon","name":"Moonlit","kind":"static","dynamic":false,"count":1},{"id":"uc-dyn","name":"Recent RPGs","kind":"dynamic","dynamic":true,"count":0}]})");
        case C::collections_show:
            return r.text == L"favorite" ? ok(R"({"id":"favorite","app_ids":[902]})") : r.text == L"hidden" ? ok(R"({"id":"hidden","app_ids":[907]})") :
                   r.text == L"uc-moon" ? ok(R"({"id":"uc-moon","app_ids":[902,906]})") : aurelia::Reply{false, false, {}, L"Unknown collection"};
        case C::available: return ok(R"({"app_id":902,"available":true,"pinned":true})");
        case C::login_qr:
            // Steam's challenge, then (once the test "scans" it) the approval; a cancel line ends it.
            if (event) event(*json::parse(R"({"event":"qr_challenge","url":"https://s.team/q/1/2394052957912650983"})"));
            while (!cancel() && !fake->qr_approve) {
                if (r.input) for (const auto& line : r.input->take()) if (line == "cancel") return aurelia::Reply{false, true, *json::parse(R"({"event":"result","ok":false,"type":"cancelled","error":"cancelled"})")};
                Sleep(5);
            }
            if (cancel()) return aurelia::Reply{false, true};
            if (event) event(*json::parse(R"({"event":"qr_scanned"})"));
            return ok(R"({"event":"result","schema":1,"ok":true,"type":"approved","account":"Aurelia fixture","steam_id":76561198000000001})");
        case C::login_password: {
            if (r.text != L"fixture" || r.secret != L"hunter2") return aurelia::Reply{false, false, *json::parse(R"({"error":"Invalid password","type":"auth_required"})"), L"Invalid password"};
            if (event) event(*json::parse(R"({"event":"guard_required","type":"email"})"));
            for (const auto until = GetTickCount64() + 4000; GetTickCount64() < until && !cancel(); Sleep(5))
                if (const auto lines = r.input ? r.input->take() : std::vector<std::string>{}; !lines.empty())
                    return lines.front() == "ABCDE" ? ok(R"({"logged_in":true,"account":"Aurelia fixture"})") :
                                                     aurelia::Reply{false, false, *json::parse(R"({"error":"Bad code","type":"auth_required"})"), L"Bad code"};
            return aurelia::Reply{false, true};
        }
        case C::launch_targets: return ok(R"({"app_id":902,"targets":[{"platform":"windows","steam_mode":"none","available":true}]})");
        case C::play: return ok(R"({"app_id":902,"status":"finished"})");
        case C::downgrade: return ok(R"({"app_id":902,"status":"downgraded","pinned":true})");
        case C::cloud_list: return ok(R"({"app_id":902,"files":[{"filename":"save/slot-1.dat","size":8192},{"filename":"settings.ini","size":123}]})");
        case C::cloud_sync: return r.text.empty() ? ok(R"({"status":"conflicts","conflicts":[{"filename":"slot-1.dat"}]})") : ok(R"({"status":"ok","conflicts":[],"skipped":[],"failed":[]})");
        case C::dlc: return ok(R"({"dlc":[{"app_id":903,"name":"The Moonlit Archive","owned":true,"installed":true,"disabled":false},{"app_id":904,"name":"Beyond the Fog","owned":true,"installed":false,"disabled":false},{"app_id":905,"name":"Soundtrack","owned":false,"installed":false,"disabled":false}]})");
        case C::manifests: return ok(R"([{"depot_id":903,"depot_name":"Windows content","branch":"public","manifest_id":8593343465227540543}])");
        case C::branches: return ok(R"({"branches":["public","beta"]})");
        case C::libraries: return ok(R"({"libraries":[{"path":"\\\\?\\D:\\SteamLibrary","free_bytes":900000000000}]})");
        case C::stop: return fake->stop_failure ? aurelia::Reply{false, false, {}, L"Fixture refused"} : ok(R"({"app_id":902,"status":"stopped","forced":true})");
        case C::workshop_browse: return ok(R"({"items":[{"id":12345678901234567,"title":"Moonlit paths"}]})");
        case C::friends:
            if (fake->bare_friends) return ok(R"([{"steam_id":76561198000000002,"persona_name":null,"persona_state":null},{"steam_id":76561198000000004,"persona_name":null,"persona_state":null}])");
            return ok(R"([{"steam_id":76561198000000002,"persona_name":"Luna","persona_state":1,"game_name":"The Moonlit Archive"}])");
        case C::chat_history: return ok(R"([{"timestamp":100,"from_self":false,"message":"Got time for a new adventure?"},{"timestamp":101,"from_self":true,"message":"Yes! Just checking my cloud saves."}])");
        case C::chat_live:
            if (event) { event(*json::parse(R"({"event":"ready"})")); event(*json::parse(R"({"event":"message","timestamp":102,"text":"Great. See you in a minute!"})")); }
            while (!cancel()) Sleep(5);
            return aurelia::Reply{false, true};
        case C::chat_send: return fake->send_failure ? aurelia::Reply{false, false, {}, L"Fixture offline"} : ok(R"({"sent":true,"server_timestamp":103})");
        case C::install:
            fake->download = true;
            if (event) event(*json::parse(R"({"event":"progress","state":"downloading","bytes_downloaded":500,"total_bytes":1000})"));
            while (!cancel() && !fake->stop_download) Sleep(5);
            fake->download = false;
            return aurelia::Reply{false, true, {}, L"Cancelled"};
        case C::cancel_install: fake->stop_download = true; return ok(R"({"event":"stopping"})");
        case C::downloads: return fake->download ? ok(R"([{"app_id":902,"name":"The Moonlit Archive","downloaded_bytes":500,"total_bytes":1000,"is_downloading":true}])") : ok("[]");
        default: return ok(R"({"status":"ok"})");
        }
    };
    app.aurelia_version_ = L"v0.1.40";
    app.aurelia_get_ = [](const std::wstring& url, const std::wstring&, size_t) {
        if (url != aurelia::latest_url) return HttpResponse{};
        const auto body = std::string(R"({"tag_name":"v0.1.40","assets":[{"name":"aurelia_windows_x86_64.exe","browser_download_url":"https://github.com/Drackrath/Aurelia/releases/download/v0.1.40/aurelia_windows_x86_64.exe","size":8693760,"digest":"sha256:)") + std::string(64, 'a') + R"("}]})";
        return HttpResponse{200, std::vector<uint8_t>(body.begin(), body.end())};
    };
    app.aurelia_installer_ = [fake](const aurelia::Release& r, const aurelia::Cancel&, const std::function<void(uint64_t, uint64_t)>& progress) {
        ++fake->helper_calls;
        if (r.version != L"v0.1.40" || r.size != 8693760) return aurelia::Reply{false, false, {}, L"Bad release"};
        progress(r.size, r.size);
        return fake->helper_failure ? aurelia::Reply{false, false, {}, L"Fixture digest mismatch"} : aurelia::Reply{true};
    };
    Game game{902, L"The Moonlit Archive"}; game.installed = true; game.last_played = 100;
    auto library = std::make_unique<LibraryMessage>(); library->account = 76561198000000001ULL; library->games = {game};
    app.onLibrary(std::move(library)); app.showCategory(0); app.arrived_ = 2; app.detail_ = 1;
    for (auto& card : app.cards_) card.opacity = 1;
    app.openAurelia(nullptr, true);
    if (!wait([&] { return !app.aurelia_ui_.busy; }) || app.topModal() != Launcher::Modal::aurelia) return 10301;
    if (!capture(L"aurelia-onboarding-tv.png")) return 10302;
    dispatched.clear(); app.aureliaActivate({{}, {}, {}, A::repository});
    if (dispatched != std::vector<std::wstring>{L"https://github.com/Drackrath/Aurelia"}) return 10351;
    app.installAurelia();
    if (!wait([&] { return !app.aurelia_helper_busy_; }) || fake->helper_calls != 1 || app.aurelia_version_ != L"v0.1.40") return 10347;
    fake->helper_failure = true; app.installAurelia();
    if (!wait([&] { return !app.aurelia_helper_busy_; }) || app.aurelia_setup_status_ != L"Fixture digest mismatch" || app.aurelia_version_ != L"v0.1.40") return 10348;
    app.aureliaActivate({{}, {}, {}, A::toggle});
    if (!app.settings_.aurelia || app.aurelia_active_ || app.aurelia_ui_.status.empty()) return 10303; // Restart, no half-switched services.
    app.closeAurelia();
    if (!app.settings_.aurelia_intro_done || app.topModal() != Launcher::Modal::none) return 10304;
    // One sign-in page: Steam's QR code for the Steam Mobile app and the account's name and password together.
    {
        using L = aurelia_ui::State::Login;
        fake->logged_in = false; app.aurelia_authenticated_ = false; app.openAurelia(nullptr);
        if (!wait([&] { return !app.aurelia_ui_.busy; })) return 10406;
        const auto row = [&](A action) -> const aurelia_ui::Row* {
            for (const auto& item : app.aurelia_ui_.rows) if (item.action == action) return &item;
            return nullptr;
        };
        // The account first: nobody signed in, one way to sign in.
        if (app.aurelia_ui_.rows.empty() || app.aurelia_ui_.rows.front().action != A::login_start ||
            app.aurelia_ui_.rows.front().value != tr(L"Nie zalogowano") || row(A::login) || row(A::logout)) return 10392;
        if (!capture(L"aurelia-signed-out-tv.png")) return 10412;
        app.aureliaActivate(*row(A::login_start));
        if (!wait([&] { return app.aurelia_ui_.qr_size > 0 && !app.aurelia_ui_.qr.empty(); }) || app.aurelia_ui_.login != L::form) return 10393;
        if (app.aurelia_ui_.qr.size() != static_cast<size_t>(app.aurelia_ui_.qr_size * app.aurelia_ui_.qr_size)) return 10394;
        // The page: the QR code, the field for the account's name, Aurelia's console and the way out.
        if (app.aurelia_ui_.input_hint != tr(L"Nazwa konta Steam") || app.aurelia_ui_.input_secret || app.aurelia_ui_.rows.size() != 2 ||
            !row(A::login) || !row(A::login_cancel)) return 10407;
        if (!capture(L"aurelia-login-qr-tv.png")) return 10395;
        app.desktop_mode_ = true; const bool desktop_shot = capture(L"aurelia-login-qr-desktop.png"); app.desktop_mode_ = false;
        if (!desktop_shot) return 10396;
        // Back leaves it: a cancel line goes to the CLI and the page's choices return.
        app.aureliaKey(VK_ESCAPE, false);
        if (app.aurelia_ui_.login != L::none || !app.aurelia_ui_.qr.empty() || !row(A::login_start)) return 10397;
        // Scanned and approved: signed in; the daemon takes the new session; the account at the top.
        fake->logged_in = true; fake->qr_approve = true;
        app.aureliaActivate(*row(A::login_start));
        if (!wait([&] { return app.aurelia_ui_.login == L::none && fake->count(C::reconnect) > 0 && app.aurelia_authenticated_; })) return 10398;
        if (!wait([&] { return !app.aurelia_ui_.busy && !app.aurelia_ui_.rows.empty() && app.aurelia_ui_.rows.front().action == A::reconnect &&
                               app.aurelia_ui_.rows.front().value == L"Aurelia fixture" && row(A::logout) && !row(A::login_start); })) return 10408;
        if (!capture(L"aurelia-signed-in-tv.png")) return 10413;
        // The password on the same page, while the QR code waits: the name, the password (dots), a wrong
        // one said, then the e-mailed Steam Guard code on stdin.
        fake->qr_approve = false; fake->logged_in = false; app.aurelia_authenticated_ = false; app.aureliaPage(P::setup);
        if (!wait([&] { return !app.aurelia_ui_.busy && row(A::login_start); })) return 10399;
        app.aureliaActivate(*row(A::login_start));
        if (!wait([&] { return !app.aurelia_ui_.qr.empty(); })) return 10400;
        app.aurelia_ui_.input = L"-h"; app.aureliaSubmit();
        if (!app.aurelia_ui_.login_account.empty() || app.aurelia_ui_.input != L"-h") return 10414; // Never an option for the CLI.
        app.aurelia_ui_.input = L"fixture"; app.aureliaSubmit();
        if (app.aurelia_ui_.login != L::form || !app.aurelia_ui_.input_secret || app.aurelia_ui_.login_account != L"fixture" || app.aurelia_ui_.qr.empty()) return 10401;
        app.aurelia_ui_.input = L"wrong"; app.aureliaSubmit();
        if (!wait([&] { return app.aurelia_ui_.login == L::form && !app.aurelia_ui_.busy && !app.aurelia_ui_.qr.empty() && app.aurelia_ui_.input_secret &&
                               app.aurelia_setup_status_ == tr(L"Nieprawidłowa nazwa konta, hasło lub kod Steam Guard."); })) return 10409;
        app.aurelia_ui_.input = L"hunter2";
        if (!capture(L"aurelia-login-password-tv.png")) return 10402;
        app.desktop_mode_ = true; const bool desktop_password = capture(L"aurelia-login-password-desktop.png"); app.desktop_mode_ = false;
        if (!desktop_password) return 10415;
        app.aureliaSubmit();
        if (!app.aurelia_ui_.input.empty() || !wait([&] { return app.aurelia_ui_.login == L::guard && app.aurelia_ui_.guard_email; })) return 10403;
        fake->logged_in = true; app.aurelia_ui_.input = L"ABCDE"; app.aureliaSubmit();
        if (!wait([&] { return app.aurelia_ui_.login == L::none && app.aurelia_authenticated_; })) return 10404;
        const auto sent = fake->last(C::login_password);
        if (sent.text != L"fixture" || sent.secret != L"hunter2" || !sent.input) return 10405;
        // Back above the rows: Up from the first row reaches it, Enter takes it.
        app.aurelia_ui_.focus = 0; app.aureliaKey(VK_UP, false);
        if (!app.aurelia_ui_.back_focus) return 10410;
        app.aureliaKey(VK_DOWN, false);
        if (app.aurelia_ui_.back_focus || app.aurelia_ui_.focus != 0) return 10416;
        app.aureliaKey(VK_UP, false); app.aureliaKey(VK_RETURN, false);
        if (app.aurelia_ui_.open) return 10411;
        fake->qr_approve = false;
    }
    app.aurelia_active_ = app.aurelia_authenticated_ = true; app.aurelia_account_ = app.account_;
    if (!app.menuRows().aurelia) return 10305;
    // A Steam game the client manages: the dialog offers only the choice; nothing goes through Aurelia.
    app.openAurelia(&game); if (!wait([&] { return !app.aurelia_ui_.busy; })) return 10306;
    if (app.aureliaManaged(game) || app.aurelia_ui_.rows.size() != 1 || app.aurelia_ui_.rows[0].action != A::steam || !app.aurelia_ui_.rows[0].toggle) return 10370;
    app.aureliaActivate(app.aurelia_ui_.rows[0]);
    if (!app.settings_.aurelia_standalone.contains("902") || !app.aureliaManaged(game) || app.aurelia_ui_.rows.size() < 5) return 10308;
    if (!capture(L"aurelia-game-tv.png")) return 10307;
    // The global default inverts the per-game choice; its exception list is separate.
    app.settings_.aurelia_default = true; app.aureliaRows();
    if (!app.aureliaManaged(game) || app.aurelia_ui_.rows[0].checked) return 10371;
    app.aureliaActivate(app.aurelia_ui_.rows[0]);
    if (app.aureliaManaged(game) || !app.settings_.aurelia_steam.contains("902")) return 10372;
    app.aureliaActivate(app.aurelia_ui_.rows[0]); app.settings_.aurelia_default = false; app.aureliaRows();
    if (!app.aureliaManaged(game)) return 10373;
    // Cloud: its state from the listing, no file list; a sync detects the conflict.
    app.aureliaPage(P::cloud);
    if (!wait([&] { return app.aurelia_cloud_[902].state == aurelia::CloudState::files; })) return 10310;
    for (const auto& row : app.aurelia_ui_.rows) if (row.action == A::none) return 10374;
    app.aureliaActivate({{}, {}, {}, A::sync});
    if (!wait([&] { return !app.aurelia_ui_.busy; }) || app.aurelia_cloud_[902].state != aurelia::CloudState::conflicts) return 10311;
    if (!capture(L"aurelia-cloud-conflict-tv.png")) return 10312;
    app.aureliaActivate({L"Use cloud saves", L"Replace local saves", {}, A::resolve_cloud});
    if (!app.aurelia_ui_.confirmation || fake->count(C::cloud_sync) != 1) return 10313;
    app.modalKey(VK_ESCAPE, false); if (app.aurelia_ui_.confirmation || fake->count(C::cloud_sync) != 1 || !app.aurelia_ui_.open) return 10314;
    app.aureliaActivate({L"Use cloud saves", {}, {}, A::resolve_cloud}, true);
    if (!wait([&] { return !app.aurelia_ui_.busy; }) || app.aurelia_cloud_[902].state != aurelia::CloudState::synced) return 10315;
    // Not signed in (or Aurelia not answering): Synchronizuj teraz says so where it is, never jumps to the setup page.
    app.aurelia_authenticated_ = false; app.aureliaActivate({{}, {}, {}, A::sync});
    if (!app.aurelia_ui_.open || app.aurelia_ui_.page != P::cloud || app.aurelia_ui_.status.empty() || fake->count(C::cloud_sync) != 2) return 10417;
    app.aurelia_authenticated_ = true;
    // The cloud toggle: no state shown or checked while off.
    app.aureliaPage(P::setup); app.aureliaActivate({{}, {}, {}, A::cloud_toggle});
    if (app.settings_.aurelia_cloud || !app.aureliaCloudText(game).empty() || !app.aurelia_cloud_.empty()) return 10375;
    app.aureliaActivate({{}, {}, {}, A::cloud_toggle});
    if (!app.settings_.aurelia_cloud) return 10376;
    app.aureliaPage(P::dlc); app.aurelia_ui_.game = game;
    app.aureliaPage(P::dlc); if (!wait([&] { return !app.aurelia_ui_.busy; })) return 10316;
    if (app.aurelia_ui_.rows.size() != 3 || app.aurelia_ui_.rows[2].action != A::none || !app.aurelia_ui_.rows[2].locked) return 10317;
    if (!capture(L"aurelia-dlc-tv.png")) return 10318;
    app.aureliaActivate(app.aurelia_ui_.rows[0]);
    if (!wait([&] { return !app.aurelia_ui_.busy; }) || fake->last(C::disable_dlc).app != 903) return 10319;
    app.aureliaPage(P::versions); if (!wait([&] { return !app.aurelia_ui_.busy; })) return 10320;
    app.aurelia_ui_.input = L"903 8593343465227540543"; app.aureliaSubmit();
    if (!app.aurelia_ui_.confirmation || fake->count(C::downgrade)) return 10321;
    if (!capture(L"aurelia-downgrade-confirm-tv.png")) return 10322;
    app.aurelia_ui_.confirm_focus = true; app.modalKey(VK_RETURN, false);
    if (!wait([&] { return !app.aurelia_ui_.busy; }) || fake->last(C::downgrade).id != 8593343465227540543ULL) return 10323;
    app.aureliaPage(P::versions); if (!wait([&] { return !app.aurelia_ui_.busy; }) || !capture(L"aurelia-versions-tv.png")) return 10324;
    app.aureliaPage(P::workshop); if (!wait([&] { return !app.aurelia_ui_.busy; })) return 10325;
    app.aureliaActivate(app.aurelia_ui_.rows[0]);
    if (!wait([&] { return !app.aurelia_ui_.busy; }) || fake->last(C::workshop_subscribe).id != 12345678901234567ULL) return 10326;
    // Libraries: Aurelia's canonical \\?\ paths shown and passed plainly; another folder through the picker.
    app.aurelia_library_action_ = A::install; app.aureliaPage(P::libraries); if (!wait([&] { return !app.aurelia_ui_.busy; })) return 10377;
    if (app.aurelia_ui_.rows.size() != 2 || app.aurelia_ui_.rows[0].title != L"D:\\SteamLibrary" || app.aurelia_ui_.rows[1].action != A::browse_library ||
        !app.aurelia_ui_.input_hint.empty()) return 10378;
    if (!capture(L"aurelia-libraries-tv.png")) return 10379;
    app.aureliaPage(P::game); if (!wait([&] { return !app.aurelia_ui_.busy; })) return 10327;
    app.aureliaOperation({C::install, 902}, game);
    if (!wait([&] { return fake->download && app.aurelia_downloads_.contains(902) && app.aurelia_downloads_[902].progress.percent() == 50; })) return 10328;
    app.closeAurelia(); std::vector<downloads_ui::Row> rows;
    const auto downloading = app.collectDownloads(rows);
    if (downloading.size() != 1 || rows.front().store != L"Aurelia") return 10329;
    // Its own label, not Steam's: Aurelia downloads it.
    if (std::wstring(primaryActionLabel(downloading.front())) != L"Pobieranie w Aurelii") return 10418;
    app.openAurelia(&game); if (!wait([&] { return !app.aurelia_ui_.busy; })) return 10330;
    app.aureliaActivate({{}, {}, {}, A::cancel});
    if (!wait([&] { return app.aurelia_operations_.empty(); }) || !fake->stop_download) return 10331;
    // A stale page result cannot reopen or replace the next page.
    app.aureliaPage(P::dlc); app.aureliaPage(P::versions);
    if (!wait([&] { return !app.aurelia_ui_.busy; }) || app.aurelia_ui_.page != P::versions) return 10341;
    app.closeAurelia();
    {
        // A download in the Steam client is not Aurelia's: its polls keep the manifest's progress, so the
        // row does not vanish and come back, and "queued" is announced once per download.
        if (app.installed_library_.empty()) return 10420;
        const auto saved = app.installed_library_.front();
        auto& steam_side = app.installed_library_.front();
        app.aurelia_downloads_.erase(steam_side.app_id);
        steam_side.installation = {InstallPhase::downloading, 10, 100, true, false};
        app.aureliaSyncInstallations();
        if (!app.installed_library_.front().installation.active()) return 10421;
        app.aureliaSyncInstallations({app.installed_library_.front().app_id}); // Aurelia's own, finished: cleared.
        if (app.installed_library_.front().installation.active()) return 10422;
        const auto announced = [&] {
            return std::count_if(app.notifications_.begin(), app.notifications_.end(),
                [](const auto& n) { return n.title == tr(L"Dodano do kolejki pobierania") && n.subtitle == L"Queued once"; });
        };
        app.notifications_.clear();
        app.downloadQueued(L"Queued once");
        app.notifications_.clear(); // Shown and gone; the same download drops out and back a moment later.
        app.downloadQueued(L"Queued once");
        if (announced() != 0) return 10423;
        app.downloadFinished(L"Queued once", nullptr); app.notifications_.clear();
        app.downloadQueued(L"Queued once"); // Downloaded again: news again.
        if (announced() != 1) return 10424;
        app.notifications_.clear();
        app.installed_library_.front() = saved;
    }
    // Odinstaluj from the game's menu: the question alone, through Aurelia only for a managed game.
    app.uninstallGame(game);
    if (!app.aurelia_ui_.open || !app.aurelia_ui_.confirmation || !app.aurelia_ui_.confirm_only) return 10380;
    app.modalKey(VK_ESCAPE, false); if (app.aurelia_ui_.open || fake->count(C::uninstall)) return 10381;
    // Steam has sent no names or statuses yet: a note instead of "everyone offline", asked again twice, then after a reconnect.
    {
        fake->bare_friends = true;
        app.toggleFriendsDrawer(); app.friends_list_ = true;
        const std::chrono::steady_clock::time_point none{};
        if (!wait([&] { return !app.friends_loading_ && app.friends_.no_presence; }) || app.friends_.error.empty() || app.friends_retry_at_ == none) return 10440;
        const auto asked = fake->count(C::friends), reconnects = fake->count(C::reconnect);
        for (int attempt = 1; attempt <= 2; ++attempt) {
            app.friends_retry_at_ = std::chrono::steady_clock::now() - std::chrono::seconds(1);
            app.tick();
            if (!wait([&] { return fake->count(C::friends) == asked + attempt && !app.friends_loading_ && app.friends_.no_presence; }) || app.friends_retry_at_ == none) return 10441;
        }
        fake->bare_friends = false; // Steam answers after the reconnect.
        app.friends_retry_at_ = std::chrono::steady_clock::now() - std::chrono::seconds(1);
        app.tick();
        if (!wait([&] { return fake->count(C::reconnect) == reconnects + 1 && !app.friends_loading_ && app.friends_.friends.size() == 1; }) ||
            app.friends_.no_presence || !app.friends_.error.empty() || app.friends_retry_at_ != none) return 10442;
        app.toggleFriendsDrawer();
        if (app.friends_open_) return 10443;
    }
    // Widżety → Znajomi Steam → a friend: the conversation in the drawer, never the Steam client.
    app.toggleFriendsDrawer(); app.friends_list_ = true;
    if (!wait([&] { return !app.friends_loading_ && !app.friends_.friends.empty(); })) return 10332;
    if (!wait([&] { return avatar_requests->load() > 0; })) return 10391;
    app.friends_cursor_ = 0; dispatched.clear(); app.friendsAccept();
    if (!wait([&] { return app.friend_chat_.live && app.friend_chat_.messages.size() == 3; }) || app.friend_chat_.name != L"Luna" ||
        !dispatched.empty() || app.aurelia_ui_.open) return 10333;
    app.friend_chat_.input = L"--help & hello, Luna!"; app.friendChatSend();
    if (!wait([&] { return !app.friend_chat_.sending; }) || !app.friend_chat_.input.empty() || fake->last(C::chat_send).text != L"--help & hello, Luna!") return 10334;
    app.friends_progress_ = 1;
    if (!capture(L"friend-chat-tv.png")) return 10335;
    fake->send_failure = true; app.friend_chat_.input = L"Keep my draft"; app.friendChatSend();
    if (!wait([&] { return !app.friend_chat_.sending; }) || app.friend_chat_.input != L"Keep my draft") return 10336;
    // Typing reaches the conversation only; the pad keyboard uses the same field.
    const auto selection = app.selected_; app.onCharacter(L'!');
    if (app.friend_chat_.input.back() != L'!' || app.selected_ != selection || !app.textFieldActive()) return 10337;
    app.openKeyboard(); if (!app.keyboard_.open) return 10338;
    app.keyboardBackspace(); app.closeKeyboard();
    if (app.friend_chat_.input != L"Keep my draft") return 10339;
    // Back: the list again, its cursor kept, the listener ended, the draft kept for this account.
    const auto listener = app.friend_chat_cancel_;
    app.friendsBack();
    if (app.friend_chat_.open || !app.friends_open_ || !app.friends_list_ || app.friends_cursor_ != 0 || !listener->load()) return 10340;
    app.friendsAccept();
    if (!wait([&] { return app.friend_chat_.live; }) || app.friend_chat_.input != L"Keep my draft") return 10354;
    app.closeFriends();
    // Zamknij grę of a game Aurelia launched, before Launchme has seen its process: Aurelia stops
    // the recorded one (forced, outside the daemon); the game counts as closed only when play returns.
    app.aurelia_playing_.insert(902); app.closeGame(game);
    if (!wait([&] { return app.aurelia_stopping_.contains(902) && app.aurelia_stopping_[902] != 0; }) || !fake->last(C::stop).force) return 10382;
    if (app.closeDetailAction() != -2) return 10383; // Not on the game's page.
    app.aurelia_stopping_[902] = 1; app.aureliaPoll(GetTickCount64());
    if (app.aurelia_stopping_.contains(902) || app.notifications_.empty()) return 10384; // Still running past the deadline: said so.
    fake->stop_failure = true; app.closeGame(game);
    if (!wait([&] { return !app.aurelia_stopping_.contains(902); })) return 10385;
    fake->stop_failure = false; app.aurelia_playing_.erase(902);
    // Szczegóły: ✕ beside Wróć while it runs; the cloud after "…" for a managed game.
    app.openDetails(game); app.details_loading_ = false;
    if (app.closeDetailAction() != -2 || app.cloudDetailAction() < 0) return 10386;
    app.aurelia_playing_.insert(902);
    if (app.closeDetailAction() < 0 || app.cloudDetailAction() <= app.closeDetailAction()) return 10387;
    app.aurelia_cloud_[902] = {aurelia::CloudState::conflicts, 2, 1};
    if (!capture(L"aurelia-details-close-cloud-tv.png")) return 10345;
    app.aurelia_playing_.erase(902);
    app.desktop_mode_ = true; app.screen_ = ScreenMode::shelves; app.buildDesktopSidebar(); app.rebuildCards(true);
    app.openAurelia(); if (!wait([&] { return !app.aurelia_ui_.busy; }) || !capture(L"aurelia-settings-desktop.png")) return 10342;
    app.openAurelia(&game); if (!wait([&] { return !app.aurelia_ui_.busy; }) || !capture(L"aurelia-game-desktop.png")) return 10343;
    app.closeAurelia();
    if (!capture(L"aurelia-cloud-card-desktop.png")) return 10344;
    app.toggleFriendsDrawer(); app.friends_list_ = true;
    if (!wait([&] { return !app.friends_loading_ && !app.friends_.friends.empty(); })) return 10388;
    app.friends_cursor_ = 0; app.friendsAccept();
    app.friends_progress_ = 1;
    if (!wait([&] { return app.friend_chat_.live; }) || !capture(L"friend-chat-desktop.png")) return 10389;
    app.closeFriends(); app.desktop_mode_ = false;
    // Disabling is persisted, but the running session remains coherent until restart.
    app.toggleAurelia(); if (app.settings_.aurelia || !app.aurelia_active_) return 10346;
    // No Steam client signed in here (the fixture's STEAM_DIR is empty): the real decision makes Aurelia's
    // list the library, with its account, collections (Ulubione, Ukryte; a dynamic one has no list) and play
    // time; "Aurelia jako zamiennik Steam" does the same with a Steam client. Only pull, list and show are used.
    if (app.aurelia_steam_client_ || !aurelia::librarySource(app.aurelia_active_, false, app.aurelia_steam_client_) ||
        aurelia::librarySource(app.aurelia_active_, false, true) || !aurelia::librarySource(app.aurelia_active_, true, true) ||
        aurelia::librarySource(false, true, false)) return 10419;
    app.aurelia_library_source_ = aurelia::librarySource(app.aurelia_active_, app.settings_.aurelia_default, app.aurelia_steam_client_);
    app.startAureliaLibraryWorker();
    if (!wait([&] { return app.installed_library_.size() == 2 && fake->count(C::library) > 0 && !app.settings_collections_.empty(); })) return 10349;
    {
        const auto collection = [&](const char* id) -> const SteamCollection* {
            for (const auto& c : app.settings_collections_) if (c.id == id) return &c;
            return nullptr;
        };
        const auto* moon = collection("user-collections.uc-moon");
        if (fake->count(C::collections_pull) < 1 || !moon || moon->name != L"Moonlit" || moon->app_ids != std::vector<uint32_t>{902, 906} ||
            !collection("user-collections.favorite") || collection("user-collections.uc-dyn") || collection("user-collections.hidden") ||
            !app.excluded_ids_.contains(907)) return 10425;
        for (const auto& call : fake->calls)
            if (call.command == C::collections_show && call.text != L"favorite" && call.text != L"hidden" && call.text != L"uc-moon") return 10426;
        const auto moonlit = std::find_if(app.installed_library_.begin(), app.installed_library_.end(), [](const Game& g) { return g.app_id == 902; });
        if (moonlit == app.installed_library_.end() || app.gamePlaytime(*moonlit, std::nullopt) != 125u || app.account_ != 76561198000000001ULL) return 10427;
    }
    // Without a Steam client every game goes through Aurelia, chosen for it or not: never steam://. With a
    // Steam client (and Aurelia not replacing it) only the games chosen for Aurelia do.
    dispatched.clear(); Game routed{902, L"Routing fixture"}; routed.installed = true;
    app.settings_.aurelia_standalone.clear();
    app.aurelia_steam_client_ = true;
    if (app.aureliaManaged(routed)) return 10352;
    app.aurelia_steam_client_ = false;
    if (!app.aureliaManaged(routed)) return 10352;
    app.settings_.aurelia_cloud_skip.insert("902"); // Its saves are not synced around play (on by default).
    app.launchGame(routed);
    if (!wait([&] { return !app.aurelia_playing_.contains(902); }) || !dispatched.empty() || fake->last(C::play).steam) return 10353;
    app.settings_.aurelia_standalone.insert("902");
    app.settings_.aurelia_cloud_skip.erase("902");
    const auto before_play = fake->count(C::play);
    app.launchGame(routed);
    if (!wait([&] { return !app.aurelia_playing_.contains(902) && !app.aurelia_ui_.busy; }) || fake->count(C::play) != before_play ||
        app.aurelia_cloud_[902].state != aurelia::CloudState::conflicts) return 10355;
    app.closeAurelia();
    fake->logged_in = false; app.aureliaRescan();
    if (!wait([&] { return !app.aurelia_authenticated_ && app.aurelia_setup_status_.find(L"offline") != std::wstring::npos; }) || app.installed_library_.size() != 2) return 10356;
    fake->bad_health = true; app.aureliaRescan();
    if (!wait([&] { return app.aurelia_setup_status_ == tr(L"Nieprawidłowa odpowiedź Aurelii"); }) || app.account_ != 76561198000000001ULL || app.installed_library_.size() != 2) return 10357;
    fake->bad_health = false; fake->logged_in = true; app.aureliaRescan();
    if (!wait([&] { return app.aurelia_authenticated_; })) return 10358;
    app.toggleFriendsDrawer(); if (!app.friends_open_) app.toggleFriendsDrawer();
    app.friends_list_ = true;
    if (!wait([&] { return !app.friends_loading_ && !app.friends_.friends.empty(); })) return 10390;
    app.friends_cursor_ = 0; app.friendsAccept();
    if (!wait([&] { return app.friend_chat_.live; })) return 10359;
    app.friend_chat_.input = L"Private account draft";
    fake->account = 76561198000000003ULL; app.aureliaRescan();
    if (!wait([&] { return app.account_ == 76561198000000003ULL; }) || !app.friend_chat_drafts_.empty() || app.friend_chat_.open) return 10360;
    // Steam's page: "Aurelia jako zamiennik Steam" says what it takes over, in four lines.
    {
        app.closeFriendChat(); app.closeFriends();
        app.settings_.aurelia = true;
        app.openSettings(); app.openPlatformSettings(GameSource::steam);
        const auto row = std::find_if(app.settings_rows_.begin(), app.settings_rows_.end(), [](const auto& r) { return r.id == settings_ui::Id::aurelia_default; });
        if (row == app.settings_rows_.end() || row->title != L"Aurelia jako zamiennik Steam" || row->lines != 4 ||
            row->description.find(L"Family Sharing") == std::wstring::npos) return 10428;
        app.settings_state_.options = true;
        app.settings_state_.selected[app.settings_state_.category] = static_cast<int>(row - app.settings_rows_.begin()); app.ensureSettingVisible();
        for (int i = 0; i < 20; ++i) { app.last_frame_ -= std::chrono::milliseconds(50); app.tick(); }
        if (!capture(L"aurelia-steam-replacement-settings.png")) return 10429;
        app.back();
    }
    return 0;
}