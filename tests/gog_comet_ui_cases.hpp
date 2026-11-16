// GOG achievements without Galaxy (src/app/gog_comet.cpp): a GOG game's start asks the account for
// comet, its unlocks become the achievement card, and it ends after the game's session.
int LauncherUiTest::gogComet(HWND window) {
    struct FakeComet : gog::Comet {
        bool alive = true;
        bool running() const override { return alive; }
    };
    struct CometGog : FakeGog {
        std::vector<gog::CometRequest> requests;
        std::shared_ptr<FakeComet> next;
        std::future<std::shared_ptr<gog::Comet>> comet(gog::CometRequest request) override {
            requests.push_back(std::move(request));
            std::promise<std::shared_ptr<gog::Comet>> answer; answer.set_value(std::exchange(next, nullptr)); return answer.get_future();
        }
    };
    Launcher app; app.hwnd_ = window; app.execute_ = fakeExecute;
    auto service = std::make_unique<CometGog>(); auto* gog = service.get(); app.gog_service_ = std::move(service);
    int prepared = 0;
    app.gog_comet_prepare_ = [&prepared](const std::filesystem::path&, const std::function<bool()>&) { ++prepared; return std::filesystem::path(L"C:/fixture/comet.exe"); };
    app.gog_state_.state = gog::State::connected; app.gog_state_.account = "1";
    Game game; game.source = GameSource::gog; game.kind = GameKind::store; game.source_id = "123"; game.name = L"Comet fixture";
    game.installed = true; game.play_action = {L"C:\\Fixture\\game.exe"};
    const auto pump = [&](const auto& condition) {
        const auto end = GetTickCount64() + 5000;
        do {
            MSG message{};
            while (PeekMessageW(&message, window, remote_message, remote_message, PM_REMOVE))
                app.onRemote(std::unique_ptr<RemoteMessage>(reinterpret_cast<RemoteMessage*>(message.lParam)));
            if (condition()) return true;
            Sleep(2);
        } while (GetTickCount64() < end);
        return false;
    };
    // Not for: the switch off, a game not installed, no linked account.
    app.settings_.gog_comet = false; app.startGogComet(game);
    app.settings_.gog_comet = true;
    auto uninstalled = game; uninstalled.installed = false; app.startGogComet(uninstalled);
    app.gog_state_.account.clear(); app.startGogComet(game); app.gog_state_.account = "1";
    auto steam = game; steam.source = GameSource::steam; app.startGogComet(steam);
    auto galaxy = game; galaxy.play_action = {L"C:\\Program Files (x86)\\GOG Galaxy\\GalaxyClient.exe", L"/command=runGame"}; app.startGogComet(galaxy);
    if (app.gog_comet_generation_ != 0) return 21100;

    // Started: the prepared binary, its folders in the profile; the game's unlock slides in.
    auto first = std::make_shared<FakeComet>(); std::weak_ptr<FakeComet> first_alive = first;
    gog->next = first; first.reset();
    app.startGogComet(game);
    if (!pump([&] { return app.gog_comet_ != nullptr; }) || prepared != 1 || gog->requests.size() != 1) return 21101;
    if (gog->requests[0].exe != std::filesystem::path(L"C:/fixture/comet.exe") ||
        gog->requests[0].data != userConfigDirectory() / L"gog" / L"comet" || !gog->requests[0].unlocked) return 21102;
    std::thread([unlocked = gog->requests[0].unlocked] { unlocked(L"Pierwsza krew"); }).join(); // Comet's reader thread.
    if (!pump([&] { return app.toast_current_.name == L"Pierwsza krew" ||
                           std::any_of(app.achievement_toasts_.begin(), app.achievement_toasts_.end(), [](const auto& t) { return t.name == L"Pierwsza krew"; }); }))
        return 21103;

    // Kept while its game runs; after the session a grace (comet's own idle wait), then ended.
    app.game_sessions_.emplace(gameKey(game), GameSession(GetTickCount64()));
    app.tickGogComet();
    if (!app.gog_comet_ || app.gog_comet_retire_at_) return 21104;
    app.game_sessions_.erase(gameKey(game));
    app.tickGogComet();
    if (!app.gog_comet_ || !app.gog_comet_retire_at_ || app.gog_comet_retire_at_ <= GetTickCount64()) return 21105;
    app.gog_comet_retire_at_ = 1; app.tickGogComet();
    if (app.gog_comet_ || !first_alive.expired()) return 21106;

    // A comet that ended by itself is let go at once; a late answer for an older start is dropped.
    auto second = std::make_shared<FakeComet>(); gog->next = second;
    app.startGogComet(game);
    if (!pump([&] { return app.gog_comet_ != nullptr; })) return 21107;
    second->alive = false; second.reset();
    app.game_sessions_.emplace(gameKey(game), GameSession(GetTickCount64()));
    app.tickGogComet();
    if (app.gog_comet_) return 21108;
    auto stale = std::make_shared<FakeComet>(); std::weak_ptr<FakeComet> stale_alive = stale;
    app.gogCometStarted(app.gog_comet_generation_ - 1, std::move(stale));
    if (app.gog_comet_ || !stale_alive.expired()) return 21109;

    // No binary (offline, no build): no request to the account.
    app.gog_comet_prepare_ = [](const std::filesystem::path&, const std::function<bool()>&) { return std::filesystem::path{}; };
    const auto asked = gog->requests.size();
    app.startGogComet(game);
    const auto generation = app.gog_comet_generation_;
    pump([] { return false; }); // Let the worker answer (nothing must come of it).
    if (gog->requests.size() != asked || app.gog_comet_ || app.gog_comet_generation_ != generation) return 21110;

    // Signing out ends it: it holds that account's session.
    app.gog_comet_prepare_ = [](const std::filesystem::path&, const std::function<bool()>&) { return std::filesystem::path(L"C:/fixture/comet.exe"); };
    auto third = std::make_shared<FakeComet>(); std::weak_ptr<FakeComet> third_alive = third; gog->next = third; third.reset();
    app.startGogComet(game);
    if (!pump([&] { return app.gog_comet_ != nullptr; })) return 21111;
    app.gogCommand(gog::Command::logout);
    if (app.gog_comet_ || !third_alive.expired()) return 21112;
    app.game_sessions_.clear();

    // Usługa zastępcza Galaxy (Windows): Zainstaluj / Usuń on a worker (UAC there), never the real one here.
    {
        using settings_ui::Id;
        auto state = gog::GalaxyServiceState::missing;
        std::vector<bool> changes;
        app.gog_galaxy_service_query_ = [&state] { return state; };
        app.gog_galaxy_service_change_ = [&](const std::filesystem::path&, bool install) {
            changes.push_back(install); state = install ? gog::GalaxyServiceState::launchme : gog::GalaxyServiceState::missing; return true;
        };
        std::vector<settings_ui::Row> rows;
        app.gogGalaxyServiceRow(rows);
        if (rows.size() != 1 || rows[0].id != Id::gog_galaxy_service || rows[0].locked || rows[0].value != tr(L"Zainstaluj")) return 21113;
        app.gogGalaxyServiceActivate();
        if (!app.gog_galaxy_service_busy_) return 21114;
        if (!pump([&] { return !app.gog_galaxy_service_busy_; }) || changes != std::vector<bool>{true}) return 21115;
        rows.clear(); app.gogGalaxyServiceRow(rows);
        if (rows[0].value != tr(L"Usuń")) return 21116;
        app.gogGalaxyServiceActivate();
        if (!pump([&] { return !app.gog_galaxy_service_busy_; }) || changes != std::vector<bool>{true, false}) return 21117;
        state = gog::GalaxyServiceState::galaxy; rows.clear(); app.gogGalaxyServiceRow(rows);
        if (!rows[0].locked) return 21118; // GOG Galaxy's own service: nothing to do, never removed.
    }
    // GOG Galaxy's overlay: a switch (comet's switch first), fetched once; the game's session attaches it,
    // and in that game Shift+Tab is GOG's.
    {
        using settings_ui::Id;
        bool present = false; int downloads = 0;
        std::vector<std::pair<uint32_t, std::string>> starts;
        auto overlay = std::make_shared<FakeComet>();
        app.gog_overlay_present_ = [&present](const std::filesystem::path&) { return present; };
        app.gog_overlay_download_ = [&](const std::filesystem::path& comet, const std::filesystem::path&, const std::function<bool()>&) {
            ++downloads; present = comet == std::filesystem::path(L"C:/fixture/comet.exe"); return present;
        };
        app.gog_overlay_start_ = [&](const std::filesystem::path&, uint32_t pid, std::string_view product, std::wstring_view) {
            starts.emplace_back(pid, std::string(product)); return std::shared_ptr<gog::Comet>(overlay);
        };
        std::vector<settings_ui::Row> rows;
        app.settings_.gog_comet = false; app.gogOverlayRow(rows);
        if (rows.size() != 1 || rows[0].id != Id::gog_overlay || !rows[0].locked) return 21120;
        app.settings_.gog_comet = true; app.settings_.gog_overlay = false;
        app.gogOverlayActivate();
        if (!app.settings_.gog_overlay || !app.gog_overlay_busy_) return 21121;
        if (!pump([&] { return !app.gog_overlay_busy_; }) || downloads != 1 || !app.gogOverlayReady()) return 21122;
        rows.clear(); app.gogOverlayRow(rows);
        if (rows[0].locked || !rows[0].checked || rows[0].description.find(tr(L"Pobrana. W grze Shift+Tab otwiera nakładkę GOG, a nakładkę Launchme przycisk Guide.")) == std::wstring::npos) return 21123;
        // The game's session (comet's log, reader thread): GalaxyOverlay.exe for its process and product.
        app.gog_state_.state = gog::State::connected; app.gog_state_.account = "1"; // Signed out above.
        auto fourth = std::make_shared<FakeComet>(); gog->next = fourth;
        app.startGogComet(game);
        if (!pump([&] { return app.gog_comet_ != nullptr; })) return 21124;
        std::thread([session = gog->requests.back().session] { session(4321); }).join();
        if (!pump([&] { return app.gog_overlay_ != nullptr; }) || starts.size() != 1 || starts[0] != std::pair<uint32_t, std::string>{4321, "123"}) return 21125;
        if (!app.gogOverlayOwnsShiftTab()) return 21126;
        // An older comet's session is not attached; the switch off, nothing either.
        app.gogOverlaySession(app.gog_comet_generation_ - 1, 99);
        app.settings_.gog_overlay = false; app.gogOverlaySession(app.gog_comet_generation_, 98);
        if (starts.size() != 1 || app.gogOverlayOwnsShiftTab()) return 21127;
        app.settings_.gog_overlay = true;
        // Ends with comet; the overlay switched off again is not downloaded.
        app.stopGogComet();
        if (app.gog_overlay_ || app.gogOverlayOwnsShiftTab()) return 21128;
        app.gogOverlayActivate();
        if (app.settings_.gog_overlay || app.gog_overlay_busy_ || downloads != 1) return 21129;
        fourth.reset();
    }
    // Linux prefixes: the registry block names the service, started on demand.
    const auto block = gog::prefixServiceKey(1700000000);
    if (block.find("[System\\\\CurrentControlSet\\\\Services\\\\GalaxyCommunication] 1700000000") == std::string::npos ||
        block.find("\"Start\"=dword:00000003") == std::string::npos) return 21119;
    return 0;
}
