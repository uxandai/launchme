// Included after LauncherUiTest so this focused regression shares its friend
// access and mock executor, without starting any native scanner/client/game.
int LauncherUiTest::battlenet(HWND window) {
    for (bool xmb : {false, true}) {
        Launcher app;
        app.hwnd_ = window; app.execute_ = fakeExecute;
        if (!app.renderer_.initialize(window)) return 400;
        app.settings_.home_layout = xmb ? AppSettings::HomeLayout::xmb : AppSettings::HomeLayout::shelves;
        app.settings_.enabled_pc_sources.insert("battlenet");
        app.settings_.show_uninstalled = false;
        const auto client = userConfigDirectory() / L"fixture-Battle.net.exe";
        std::ofstream(client) << "fixture";
        const auto capture = [&](const wchar_t* name) {
            wchar_t folder[4096]{};
            if (xmb || !GetEnvironmentVariableW(L"LAUNCHME_UI_CAPTURE_DIR", folder, 4096)) return true;
            InvalidateRect(window, nullptr, FALSE);
            return app.renderer_.captureForTest(std::filesystem::path(folder) / name, [&] { app.paint(); });
        };
        auto steam = std::make_unique<LibraryMessage>(); steam->account = 1;
        Game recent{8800, L"Steam fixture"}; recent.last_played = 100; steam->games.push_back(recent);
        app.onLibrary(std::move(steam));
        Game game; game.kind = GameKind::store; game.source = GameSource::battlenet;
        game.source_id = "WTCG"; game.name = L"Hearthstone"; game.installed = false;
        game.installation.phase = InstallPhase::staging;
        configureStoreActions(game, client.wstring(), L"C:\\fixture\\Hearthstone");
        Game hidden = game; hidden.source_id = "Fen"; hidden.visibility = GameVisibility::private_game;
        const auto send = [&](const Game& visible, const Game& secret, bool stale = false) {
            auto pc = std::make_unique<PcLibraryMessage>(); pc->generation = app.pc_token_->load() + (stale ? 1 : 0);
            pc->library.visible.push_back(visible); pc->library.private_games.push_back(secret);
            pc->library.clients.push_back({GameSource::battlenet, client});
            app.onPcLibrary(std::move(pc));
        };
        send(game, hidden);
        if (app.categories_[0].games.empty() || gameKey(app.categories_[0].games[0]) != "battlenet:wtcg" ||
            std::any_of(app.public_library_.begin(), app.public_library_.end(), [](const Game& g) { return g.source_id == "Fen"; })) return 401;
        app.showCategory(0);
        for (int i = 0; i < 40; ++i) { app.last_frame_ -= std::chrono::milliseconds(50); app.tick(); }
        if (!capture(L"battlenet-install-dashboard.png")) return 402;
        app.openPcLibrary(GameSource::battlenet);
        if (app.cards_.size() != 1 || app.cards_[0].game.installed || app.cards_[0].game.installation.percent()) return 403;
        app.renderer_.setCover(0, pixel(), {});
        const auto selected = app.selected_; const auto scroll = app.grid_scroll_;
        game.installation.phase = InstallPhase::unknown;
        send(game, hidden);
        if (app.selected_ != selected || app.grid_scroll_ != scroll || !app.renderer_.hasCover(0) ||
            app.cards_[0].game.installation.phase != InstallPhase::unknown || app.actionLabel(app.cards_[0].game) != L"Otwórz klienta") return 404;
        // Injected counters test the shared UI contract only. The native reader
        // supplies no percentage until a current-operation source is verified.
        game.installation = {InstallPhase::downloading, 37, 100, true}; send(game, hidden);
        game.installation.done = 49; send(game, hidden);
        if (!app.renderer_.hasCover(0) || app.selected_ != selected || app.grid_scroll_ != scroll ||
            app.cards_[0].game.installation.percent() != 49) return 417;
        // Unchanged polls also must not drop the bitmap or derived local history.
        app.cards_[0].game.last_played = 123456;
        send(game, hidden);
        if (!app.renderer_.hasCover(0) || app.cards_[0].game.last_played != 123456) return 405;
        game.installation = {InstallPhase::staging}; send(game, hidden);
        if (!capture(L"battlenet-install-library.png")) return 406;
        app.openDetails(app.cards_[0].game);
        if (app.actionLabel(app.cards_[0].game) != L"Instalacja Battle.net") return 407;
        const auto count = dispatched.size();
        app.launch(); app.finishLaunch();
        if (dispatched.size() != count + 1 || dispatched.back() != client.wstring() || !app.game_sessions_.empty()) return 408;
        if (!capture(L"battlenet-install-details.png")) return 409;
        game.installed = true; game.installation = {}; send(game, hidden, true);
        if (app.cards_[0].game.installed) return 410; // Stale generation cannot finish an installation.
        send(game, hidden);
        if (!app.cards_[0].game.installed || app.actionLabel(app.cards_[0].game) != L"Graj" ||
            !app.completed_pc_installs_.contains("battlenet:wtcg") || !app.library_state_.last_played.empty()) return 411;
        if (!capture(L"battlenet-install-completed.png")) return 412;
        auto privacy = std::make_unique<PcLibraryMessage>(); privacy->generation = app.pc_token_->load();
        privacy->library.clients.push_back({GameSource::battlenet, client});
        game.visibility = GameVisibility::private_game; privacy->library.private_games = {game, hidden};
        app.onPcLibrary(std::move(privacy));
        if (app.screen_ == ScreenMode::details || std::any_of(app.public_library_.begin(), app.public_library_.end(), [](const Game& g) {
            return g.source == GameSource::battlenet;
        })) return 413;
        app.openSettings(); app.settings_state_.selectCategory(static_cast<int>(settings_ui::Category::library)); app.platforms_all_ = true; app.buildSettingsRows();
        const auto row = std::find_if(app.settings_rows_.begin(), app.settings_rows_.end(), [](const auto& row) {
            return row.id == settings_ui::Id::pc_platform && row.source == GameSource::battlenet;
        });
        if (row == app.settings_rows_.end() || !row->description.starts_with(L"Lokalne instalacje:") || row->value != L"Opcje") return 414;
        if (!capture(L"battlenet-local-settings.png")) return 415;
        app.settings_.enabled_pc_sources.erase("battlenet"); app.buildShelves();
        if (!app.private_category_.games.empty()) return 416;
    }
    return 0;
}
