int LauncherUiTest::hltbDetails(HWND window) {
    Launcher app; app.hwnd_ = window; app.execute_ = fakeExecute; app.settings_.launch_animation = false;
    if (!app.renderer_.initialize(window)) return 3300;
    plugins::Plugin plugin;
    plugin.manifest = plugins::parseManifest(R"({"apiVersion":1,"architecture":"windows-x64","id":"test.hltb","version":"1.0.0","entry":"fixture.dll","name":"HowLongToBeat","iconText":"HLT","contributions":[
        {"id":"times","kind":"details","title":"HowLongToBeat","command":"lookup"},
        {"id":"active","kind":"widget","title":"HowLongToBeat","command":"lookup","activeGame":true}]})");
    plugin.enabled = true; app.plugins_.plugins.push_back(plugin); app.refreshPluginContributions();
    Game steam{620, L"Portal 2"}, local; local.source = GameSource::custom; local.source_id = "fixture"; local.kind = GameKind::store; local.name = L"Active local game";
    app.custom_library_ = {local};
    auto library = std::make_unique<LibraryMessage>(); library->account = 42; library->games = {steam}; app.onLibrary(std::move(library));
    app.openDetails(steam); app.details_loading_ = false;
    app.details_.description = L"A description long enough to wrap across several lines. The completion-time comparison must sit below the playtime and the achievements, without overlapping either the description or its progress bar.";
    app.details_.developer = L"Valve"; app.details_.playtime_minutes = 120;
    app.details_.achievements_unlocked = 12; app.details_.achievements_total = 50;
    const auto pop = [&] { auto job = app.plugin_jobs_->jobs.front(); app.plugin_jobs_->jobs.pop_front(); return job; };
    app.runtime_started_ = true; app.requestGameComparisons(steam); app.runtime_started_ = false;
    if (app.plugin_jobs_->jobs.size() != 2) return 3301;
    auto stats = pop(); auto details = pop();
    if (stats.kind != plugins::Job::Kind::playtime || details.kind != plugins::Job::Kind::details || details.game.app_id != 620) return 3302;
    auto sample = std::make_unique<PluginsMessage>(); sample->job = stats; sample->job.playtime_seconds = 7200; app.onPlugins(std::move(sample));
    const auto reply = [&](plugins::Job job, uint32_t reference) {
        auto message = std::make_unique<PluginsMessage>(); message->job = std::move(job);
        message->response = *json::parse("{\"comparison\":{\"gameKey\":" + plugins::wire::quote(gameKey(message->job.game)) +
            ",\"mainSeconds\":" + std::to_string(reference) + ",\"extraSeconds\":43200,\"completionSeconds\":72000}}");
        app.onPlugins(std::move(message));
    };
    reply(details, 28800);
    if (!app.pluginComparison("test.hltb", steam) || app.pluginPlaytimeSeconds(steam) != 7200) return 3303;
    const auto capture = [&](const wchar_t* name) {
        wchar_t folder[32768]{};
        if (!GetEnvironmentVariableW(L"LAUNCHME_UI_CAPTURE_DIR", folder, static_cast<DWORD>(std::size(folder)))) { app.paint(); return true; }
        return app.renderer_.captureForTest(std::filesystem::path(folder) / name, [&] { app.paint(); });
    };
    app.renderer_.setPlatformBadge(L"Steam", pngFixture(230, 100, 40));
    app.detail_action_ = app.pluginDetailAction();
    if (!capture(L"hltb-details.png")) return 3304;
    const auto comparison = app.renderer_.detailsComparison(), achievements = app.renderer_.detailsAchievements();
    const auto view = app.renderer_.viewport(); const details_layout::Layout layout{view.width, view.height};
    // One row of equal cards: Osiągnięcia, then Czas gry with the comparison.
    if (comparison.left <= achievements.right || std::abs(comparison.top - achievements.top) > 0.5f || comparison.bottom > view.height - 10 ||
        std::abs((comparison.right - comparison.left) - (achievements.right - achievements.left)) > 0.5f) return 3305;
    // The cards sit under the buttons, the logo (or title) over the line of facts.
    const auto actions = app.detailsActions();
    if (achievements.top <= layout.buttonsTop(actions) + layout.button_height || layout.logo(actions).bottom >= layout.factsCenter(actions)) return 3306;
    app.click((comparison.left + 30) * view.scale, (comparison.top + 25) * view.scale, false);
    if (app.plugin_jobs_->jobs.empty() || app.plugin_jobs_->jobs.back().kind != plugins::Job::Kind::command || app.plugin_jobs_->jobs.back().game.app_id != 620) return 3307;
    auto command = pop(); app.plugin_command_pending_ = false;
    // Steam: baseline already includes all time before its sample. Add only later session time.
    app.game_sessions_.emplace(gameKey(steam), GameSession(1000)); app.game_sessions_.at(gameKey(steam)).observe(GameActivity::running, 1000); app.trackSession(gameKey(steam), steam, {});
    app.runtime_started_ = true; app.ensurePluginPlaytime(steam); app.runtime_started_ = false;
    if (app.plugin_jobs_->jobs.size() != 1) return 3308;
    auto session_sample = std::make_unique<PluginsMessage>(); session_sample->job = pop(); session_sample->job.playtime_seconds = 7200; app.onPlugins(std::move(session_sample));
    for (uint64_t now = 2000; now <= 61000; now += 1000) app.game_sessions_.at(gameKey(steam)).observe(GameActivity::running, now);
    app.game_sessions_.at(gameKey(steam)).takePlaytime();
    if (app.pluginPlaytimeSeconds(steam) != 7260) return 3309;
    app.game_sessions_.at(gameKey(steam)).freeze({{1, 1}}); app.game_sessions_.at(gameKey(steam)).observe(GameActivity::running, 62000);
    if (app.pluginPlaytimeSeconds(steam) != 7260) return 3310;
    // A different game is active while Steam details remain selected.
    app.game_sessions_.emplace(gameKey(local), GameSession(100000)); app.game_sessions_.at(gameKey(local)).observe(GameActivity::running, 100000); app.trackSession(gameKey(local), local, {});
    const auto history = recent_games::historyKey(local, 42, {}); app.library_state_.playtime_ms[history] = 3600 * 1000;
    app.refreshPluginWidgets(true);
    if (app.plugin_jobs_->jobs.size() != 1) return 3311;
    auto widget = pop(); if (gameKey(widget.game) != gameKey(local) || !widget.active_game) return 3312;
    reply(widget, 7200);
    if (app.pluginWidgetText(0).find(L"50%") == std::wstring::npos) return 3313;
    app.runtime_started_ = true; app.requestGameComparisons(local); app.runtime_started_ = false;
    if (app.plugin_jobs_->jobs.size() != 1 || app.plugin_jobs_->jobs.front().kind != plugins::Job::Kind::details) return 3319;
    auto fresh = pop(); reply(widget, 7200);
    if (!app.pluginComparison("test.hltb", local)->pending) return 3320; // A cache-only widget cannot finish a pending provider refresh.
    reply(fresh, 7200);
    app.library_state_.playtime_ms[history] = 10800 * 1000;
    if (app.pluginWidgetText(0).find(L"150%") == std::wstring::npos) return 3314;
    app.friends_open_ = true; app.friends_progress_ = 1;
    app.settings_.widget_layout = {{"test.hltb/active", widgets::Size::wide}};
    if (!capture(L"hltb-active-widget.png")) return 3315;
    // Overlay ownership wins over other sessions; a late widget response cannot switch it back.
    app.overlay_target_ = GameWindow{window, {}, gameKey(steam)};
    app.refreshPluginWidgets(true); app.plugin_jobs_->jobs.clear(); reply(widget, 1);
    if (!app.pluginActiveGame() || app.pluginActiveGame()->app_id != 620 || app.pluginWidgetText(0).find(L"25%") == std::wstring::npos) return 3316;
    app.overlay_target_.reset(); app.friends_open_ = false;
    // Commands in the open matching drawer retain its game even when the active game changed.
    auto page = std::make_unique<PluginsMessage>(); page->job = command; page->job.generation = app.plugin_command_generation_;
    page->response = *json::parse(R"({"drawer":{"title":"HowLongToBeat","sections":[{"title":"Portal 2","items":[{"title":"Choose","command":"pick.123"}]}]}})");
    app.onPlugins(std::move(page)); app.pluginPageAccept();
    if (app.plugin_jobs_->jobs.empty() || app.plugin_jobs_->jobs.back().game.app_id != 620) return 3317;
    app.plugin_jobs_->jobs.clear(); app.plugin_command_pending_ = false; app.closeFriends();
    app.plugins_.plugins[0].enabled = false; app.refreshPluginContributions(); reply(details, 1); app.paint();
    if (app.pluginDetailProvider() || !app.plugin_widgets_.empty() || app.renderer_.detailsComparison().bottom != 0) return 3318;
    return 0;
}

// Game lengths for the index, quietly: one owned game at a time through the HowLongToBeat plugin,
// installed and recently played first, never while the player's own plugin work waits, never
// during a game, with a long pause after a run of lookups without a time; no toasts.
int LauncherUiTest::gameLengths(HWND window) {
    Launcher app; app.hwnd_ = window; app.execute_ = fakeExecute; app.settings_.launch_animation = false;
    plugins::Plugin plugin;
    plugin.manifest = plugins::parseManifest(R"({"apiVersion":1,"architecture":"windows-x64","id":"launchme.howlongtobeat","version":"1.0.0","entry":"fixture.dll","name":"HowLongToBeat","iconText":"HLT","contributions":[
        {"id":"times","kind":"details","title":"HowLongToBeat","command":"lookup"}]})");
    plugin.enabled = true; app.plugins_.plugins.push_back(plugin); app.refreshPluginContributions();
    auto library = std::make_unique<LibraryMessage>(); library->account = 1;
    Game early{3401, L"Installed Earlier"}; early.last_played = 50;
    Game remote{3402, L"Not Installed"}; remote.installed = false; remote.last_played = 900;
    Game recent{3403, L"Installed Recently"}; recent.last_played = 200;
    library->games = {early, remote, recent};
    app.onLibrary(std::move(library)); app.scanning_ = false;
    app.startGameIndex();
    if (!app.game_index_) return 3400;
    for (const auto end = GetTickCount64() + 5000; !app.game_index_->loaded() && GetTickCount64() < end;) Sleep(5);
    app.game_index_->library(app.gameIndexGames(), 1);
    for (const auto end = GetTickCount64() + 5000; !app.game_index_->idle() && GetTickCount64() < end;) Sleep(5);
    const auto lengthOf = [&](uint32_t id) {
        return app.game_index_->read([&](const game_index::Index& index) {
            const auto* record = index.get("steam:" + std::to_string(id));
            return record && record->hltb_main ? *record->hltb_main : 0u;
        });
    };
    const auto pop = [&] { auto job = app.plugin_jobs_->jobs.front(); app.plugin_jobs_->jobs.pop_front(); return job; };
    const auto reply = [&](plugins::Job job, uint32_t main) {
        auto message = std::make_unique<PluginsMessage>(); message->job = std::move(job); message->job.epoch = app.plugin_epoch_;
        message->response = *json::parse("{\"comparison\":{\"gameKey\":" + plugins::wire::quote(gameKey(message->job.game)) +
            (main ? ",\"mainSeconds\":" + std::to_string(main) : std::string{}) + "}}");
        app.onPlugins(std::move(message));
    };
    // Only once the start has settled (tickGameIndex: 20 s, no game running).
    app.runtime_started_ = true;
    app.fillGameLengths();
    if (!app.plugin_jobs_->jobs.empty()) return 3401;
    app.game_index_filling_ = true;
    app.fillGameLengths();
    if (app.plugin_jobs_->jobs.size() != 1) return 3402;
    auto first = pop();
    if (first.kind != plugins::Job::Kind::details || !first.background || first.game.app_id != 3403 || first.plugin != "launchme.howlongtobeat") return 3403;
    // Spaced out: not again at once; and never over a job the player is waiting for.
    app.fillGameLengths();
    if (!app.plugin_jobs_->jobs.empty()) return 3404;
    app.length_fill_at_ = 0;
    plugins::Job waiting; waiting.kind = plugins::Job::Kind::command; waiting.plugin = "launchme.howlongtobeat";
    app.plugin_jobs_->jobs.push_back(waiting);
    app.fillGameLengths();
    if (app.plugin_jobs_->jobs.size() != 1) return 3405;
    app.plugin_jobs_->jobs.clear(); app.length_fill_at_ = 0;
    // A time goes into the index (a background answer opens nothing on screen).
    reply(first, 36000);
    for (const auto end = GetTickCount64() + 5000; lengthOf(3403) != 600 && GetTickCount64() < end;) Sleep(5);
    if (lengthOf(3403) != 600 || app.screen_ != ScreenMode::shelves) return 3406;
    // The next: installed before a more recently played one that is not.
    app.fillGameLengths();
    if (app.plugin_jobs_->jobs.size() != 1 || app.plugin_jobs_->jobs.front().game.app_id != 3401) return 3407;
    auto second = pop(); app.length_fill_at_ = 0;
    app.fillGameLengths();
    if (app.plugin_jobs_->jobs.size() != 1 || app.plugin_jobs_->jobs.front().game.app_id != 3402) return 3408;
    auto third = pop(); app.length_fill_at_ = 0;
    // A run of lookups without a time: a long pause.
    app.length_fill_misses_ = Launcher::length_fill_pause_after - 2;
    reply(second, 0);
    if (app.length_fill_at_ != 0) return 3409;
    reply(third, 0);
    if (app.length_fill_at_ < GetTickCount64() + 9 * 60 * 1000 || app.length_fill_misses_ != 0) return 3410;
    // A failing background lookup says nothing.
    auto failed = std::make_unique<PluginsMessage>(); failed->job = third; failed->job.epoch = app.plugin_epoch_; failed->error = "fixture failure"; failed->snapshot = app.plugins_;
    app.onPlugins(std::move(failed));
    if (!app.notifications_.empty()) return 3411;
    // Every game asked once this start: nothing more, a look again in ten minutes.
    app.length_fill_at_ = 0;
    app.fillGameLengths();
    if (!app.plugin_jobs_->jobs.empty() || app.length_fill_at_ < GetTickCount64() + 9 * 60 * 1000) return 3412;
    return 0;
}