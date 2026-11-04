// Tarot: off by default; T, View or the desktop top bar opens it over the home; a modal layer that
// takes keys, pad buttons and clicks; a library reading from the cards to the results and Details;
// a store reading through a fake Steam search to the store page; Back takes cards back.
int LauncherUiTest::tarotReading(HWND window) {
    using Modal = Launcher::Modal;
    using tarot_ui::Phase;
    Launcher app;
    app.hwnd_ = window; app.execute_ = fakeExecute;
    app.browse_ = [](const std::wstring& url) { dispatched.push_back(url); };
    app.overlay_backend_.foreground = [window] { return window; };
    app.settings_.launch_animation = false;
    if (!app.renderer_.initialize(window)) return 8400;
    auto library = std::make_unique<LibraryMessage>(); library->account = 1;
    const wchar_t* names[] = {L"Kroniki Mgły", L"Gwiezdny Kurier", L"Ogród Zegarów", L"Ostatnia Latarnia", L"Miasto Kości", L"Rzeka Snów"};
    for (uint32_t i = 0; i < 6; ++i) { Game game{8400 + i, names[i]}; game.last_played = i % 2 ? 0 : 1700000000 - i; library->games.push_back(game); }
    app.onLibrary(std::move(library)); app.showCategory(0);
    app.snapCardsForTransition(); app.detail_ = 1;
    const auto capture = [&](const wchar_t* name) {
        wchar_t folder[4096]{};
        if (!GetEnvironmentVariableW(L"LAUNCHME_UI_CAPTURE_DIR", folder, 4096)) { app.paint(); return true; }
        return app.renderer_.captureForTest(std::filesystem::path(folder) / name, [&] { app.paint(); });
    };
    const auto settle = [&](float seconds) { for (float t = 0; t < seconds; t += 0.05f) app.tickTarot(0.05f); };
    const auto spread = [&] { return app.tarot_.reading.current(); };
    const auto cardWith = [&](int value) {
        const auto* current = spread();
        if (!current) return -1;
        const auto cards = tarot::question(current->facet).cards;
        for (size_t i = 0; i < current->cards.size(); ++i) if (cards[static_cast<size_t>(current->cards[i])].value == value) return static_cast<int>(i);
        return -1;
    };

    // Off by default: no shortcut opens it, the desktop top bar has no icon.
    if (app.settings_.tarot || loadAppSettings().tarot || app.tarotAvailable()) return 8401;
    app.openTarot(); if (app.tarotOpen() || app.topModal() != Modal::none) return 8402;
    // Settings → System → Funkcje: right under Glance; turning it on.
    app.openSettings(); app.settings_state_.category = static_cast<int>(settings_ui::Category::system); app.buildSettingsRows();
    const auto row = std::find_if(app.settings_rows_.begin(), app.settings_rows_.end(), [](const auto& r) { return r.id == settings_ui::Id::tarot; });
    if (row == app.settings_rows_.end() || row == app.settings_rows_.begin() || std::prev(row)->id != settings_ui::Id::glance || row->checked) return 8403;
    app.settings_state_.options = true; app.settings_state_.selected[app.settings_state_.category] = static_cast<int>(row - app.settings_rows_.begin());
    app.settingsActivate(); app.saveSettings();
    if (!app.settings_.tarot || !loadAppSettings().tarot) return 8404;
    app.back(); app.showCategory(0);
    if (!app.tarotAvailable()) return 8405;
    const auto tools = app.desktopMoreTools();
    if (std::find(tools.begin(), tools.end(), desktop_ui::Tool::tarot) == tools.end()) return 8406;

    // T opens it (main.cpp); here the same call. A modal layer: the home's widgets and hints step aside.
    app.openTarot();
    if (!app.tarotOpen() || app.topModal() != Modal::tarot || app.homeOverlayAvailable() || app.tarotAvailable()) return 8407;
    ViewContext hints; app.inputHints(hints); if (!hints.hints.empty()) return 8408;
    if (!spread() || spread()->facet != tarot::Facet::intent || spread()->cards.size() != 2 || app.tarot_.steps() != 1) return 8409;
    app.tickTarot(0.12f);
    if (!capture(L"tarot-deal.png")) return 8410;
    settle(1.2f);
    if (app.tarot_.alpha != 1 || !capture(L"tarot-intent.png")) return 8411;
    // Moving between the cards; a click outside them changes nothing; the wheel is the reading's.
    app.modalKey(VK_RIGHT, false); if (app.tarot_.focus != 1) return 8412;
    app.modalKey(VK_RIGHT, false); if (app.tarot_.focus != 1) return 8413; // The last card: a boundary.
    const auto view = app.renderer_.viewport();
    app.click(4 * view.scale, view.height * 0.95f * view.scale, false);
    if (!app.tarotOpen() || app.tarot_.phase != Phase::question) return 8414;

    // Back on the first card closes it; View on the pad opens it again.
    app.modalKey(VK_BACK, false); settle(0.5f);
    if (app.tarotOpen() || app.topModal() != Modal::none || app.tarot_.alpha != 0) return 8415;
    PadReading pad{true, L"tarot-pad"}; app.controller(pad);
    const auto press = [&](WORD button) { pad.buttons = button; app.controller(pad); pad.buttons = 0; app.controller(pad); };
    press(XINPUT_GAMEPAD_BACK);
    if (!app.tarotOpen()) return 8416;
    settle(1.0f);

    // The library: the Hermit. A click picks the card; it rises, the others fall, the next spread deals.
    const int hermit = cardWith(1);
    if (hermit < 0) return 8417;
    const tarot_ui::Layout layout{view.width, view.height};
    const auto hermit_card = layout.card(hermit, 2);
    app.click((hermit_card.left + hermit_card.right) * 0.5f * view.scale, (hermit_card.top + hermit_card.bottom) * 0.5f * view.scale, false);
    if (app.tarot_.phase != Phase::leaving || app.tarot_.chosen != hermit) return 8418;
    app.tickTarot(0.3f);
    if (!capture(L"tarot-leaving.png")) return 8419;
    settle(0.6f);
    if (app.tarot_.phase != Phase::question || app.tarot_.reading.pool() != tarot::Pool::library || app.tarot_.steps() < 5 ||
        app.tarot_.steps() > 8 || app.tarot_.step() != 1) return 8420;
    settle(1.0f);
    if (!capture(L"tarot-question.png")) return 8421;
    // Backspace takes the last card back (the pad's B too); the intent is asked again.
    app.modalKey(VK_BACK, false);
    if (app.tarot_.step() != 0 || !spread() || spread()->facet != tarot::Facet::intent) return 8422;
    settle(1.0f);
    app.tarot_.focus = cardWith(1); app.modalKey(VK_RETURN, false); settle(0.8f);
    if (app.tarot_.step() != 1) return 8423;
    // Every other spread: the first card, by keyboard and by pad.
    for (int guard = 0; guard < 10 && app.tarot_.phase == Phase::question; ++guard) {
        settle(0.8f);
        if (guard % 2) press(XINPUT_GAMEPAD_A); else app.modalKey(VK_RETURN, false);
        settle(0.8f);
    }
    if (app.tarot_.phase != Phase::divining || !capture(L"tarot-divining.png")) return 8424;
    settle(2.0f);
    if (app.tarot_.phase != Phase::reading || app.tarot_.results.empty() || app.tarot_.results.size() > 3) return 8425;
    for (const auto& result : app.tarot_.results)
        if (result.url.size() || std::none_of(app.public_library_.begin(), app.public_library_.end(), [&](const Game& game) { return gameKey(game) == result.key; })) return 8426;
    // Posters: each result's image slot; a fixture stands in for the downloaded art.
    if (!app.renderer_.tarotPoster(0) && app.renderer_.tarotImage(0)) return 8427;
    for (size_t i = 0; i < app.tarot_.results.size(); ++i)
        app.tarotArtArrived(app.tarot_.results[i].key, CoverMessage::Kind::poster, shareImage(pngFixture(static_cast<uint8_t>(60 + i * 50), 120, 180)));
    if (!app.renderer_.tarotPoster(0)) return 8428;
    app.tarotArtArrived(app.tarot_.results[0].key, CoverMessage::Kind::cover, shareImage(pngFixture(10, 64, 64)));
    if (!app.renderer_.tarotPoster(0)) return 8429; // A cover never replaces a poster.
    settle(2.0f);
    if (!capture(L"tarot-reading.png")) return 8430;
    // Down to the buttons, Deal again starts over; then a new reading's results, opening Details.
    app.modalKey(VK_DOWN, false); if (app.tarot_.focus != tarot_ui::focus_again) return 8431;
    app.modalKey(VK_RIGHT, false); if (app.tarot_.focus != tarot_ui::focus_close) return 8432;
    app.modalKey(VK_LEFT, false); app.modalKey(VK_RETURN, false);
    if (!app.tarotOpen() || app.tarot_.phase != Phase::question || app.tarot_.step() != 0 || app.tarot_.results.size()) return 8433;
    settle(1.0f);
    app.tarot_.focus = cardWith(1); app.modalKey(VK_RETURN, false); settle(0.8f);
    for (int guard = 0; guard < 10 && app.tarot_.phase == Phase::question; ++guard) { settle(0.8f); app.modalKey(VK_RETURN, false); settle(0.8f); }
    settle(2.5f);
    if (app.tarot_.phase != Phase::reading || app.tarot_.results.empty()) return 8434;
    const auto picked = app.tarot_.results[0].key;
    app.tarot_.focus = 0; app.modalKey(VK_RETURN, false);
    if (app.tarotOpen() || app.screen_ != ScreenMode::details || app.detail_games_.empty() || gameKey(app.detail_games_[0]) != picked) return 8435;
    app.back(); settle(0.5f);
    if (app.screen_ != ScreenMode::shelves || !app.tarotAvailable()) return 8436;

    // The store: a fake Steam search. Owned games never come back; the result opens its store page.
    std::string rows;
    const auto escape = [](const std::string& text) { std::string out; for (const char c : text) { if (c == '"' || c == '\\') out += '\\'; out += c; } return out; };
    const char* titles[] = {"Latarnia Na Krańcu", "Sad Zegarmistrza", "Kosmiczny Ogród", "Owned Twin", "Zatoka Cieni"};
    const uint32_t ids[] = {9101, 9102, 9103, 8400, 9105};
    for (int i = 0; i < 5; ++i)
        rows += "<a href=\"https://store.steampowered.com/app/" + std::to_string(ids[i]) + "/x/\" data-ds-appid=\"" + std::to_string(ids[i]) +
                "\" data-ds-tagids=\"[19,21,122,9,1664,599,3942,4182]\" class=\"search_result_row\"><img src=\"https://shared.akamai.steamstatic.com/a.jpg\">"
                "<span class=\"title\">" + titles[i] + "</span><div class=\"search_released\">" + std::to_string(5 + i) + " Mar, " + std::to_string(2008 + i * 4) +
                "</div><span class=\"search_review_summary\" data-tooltip-html=\"Very Positive&lt;br&gt;9" + std::to_string(i) + "% of the 4,321 user reviews\"></span></a>";
    const std::string search = "{\"success\":1,\"start\":0,\"total_count\":5,\"results_html\":\"" + escape(rows) + "\"}";
    const DWORD ui_thread = GetCurrentThreadId();
    const auto wrong_thread = std::make_shared<std::atomic_bool>(false);
    const auto asked = std::make_shared<std::atomic_int>(0);
    app.tarot_get_ = [search, ui_thread, wrong_thread, asked](const std::wstring& url, const std::wstring&, size_t) {
        if (GetCurrentThreadId() == ui_thread) wrong_thread->store(true);
        if (url.find(L"store.steampowered.com/search/results/") == std::wstring::npos) return HttpResponse{};
        ++*asked;
        return HttpResponse{200, {search.begin(), search.end()}};
    };
    const auto pump = [&] {
        MSG message{};
        while (PeekMessageW(&message, window, remote_message, remote_message, PM_REMOVE))
            app.onRemote(std::unique_ptr<RemoteMessage>(reinterpret_cast<RemoteMessage*>(message.lParam)));
    };
    app.openTarot(); settle(1.0f);
    app.tarot_.focus = cardWith(0); app.modalKey(VK_RETURN, false); settle(0.8f);
    if (app.tarot_.reading.pool() != tarot::Pool::store) return 8437;
    for (int guard = 0; guard < 10 && app.tarot_.phase == Phase::question; ++guard) { settle(0.8f); app.modalKey(VK_RETURN, false); settle(0.8f); }
    if (app.tarot_.phase != Phase::divining) return 8438;
    const auto end = GetTickCount64() + 5000;
    while (app.tarot_.waiting && GetTickCount64() < end) { pump(); Sleep(5); }
    if (app.tarot_.waiting || wrong_thread->load() || asked->load() < 1) return 8439;
    settle(2.0f);
    if (app.tarot_.phase != Phase::reading || app.tarot_.results.empty() || app.tarot_.failed) return 8440;
    for (const auto& result : app.tarot_.results)
        if (result.game.app_id == 8400 || result.url != L"https://store.steampowered.com/app/" + std::to_wstring(result.game.app_id) + L"/" ||
            result.game.kind != GameKind::catalog) return 8441;
    settle(2.0f);
    if (!capture(L"tarot-store.png")) return 8442;
    // The mouse: hovering a result focuses it, a click opens its page (the reading stays under it).
    const int count = static_cast<int>(app.tarot_.results.size());
    const auto poster = layout.poster(count - 1, count);
    app.mouseMove((poster.left + poster.right) * 0.5f * view.scale, (poster.top + poster.bottom) * 0.5f * view.scale);
    if (app.tarot_.focus != count - 1 || app.tarot_.hover != count - 1) return 8443;
    app.click((poster.left + poster.right) * 0.5f * view.scale, (poster.top + poster.bottom) * 0.5f * view.scale, false);
    if (dispatched.empty() || dispatched.back() != app.tarot_.results[static_cast<size_t>(count - 1)].url || !app.tarotOpen()) return 8444;
    // The search came from Steam once and was kept for half an hour.
    if (!std::filesystem::exists(userConfigDirectory() / L"tarot") || std::filesystem::is_empty(userConfigDirectory() / L"tarot")) return 8451;
    // Steam unreachable and nothing kept: the cards are silent, with Deal again focused.
    std::filesystem::remove_all(userConfigDirectory() / L"tarot");
    app.tarot_get_ = [](const std::wstring&, const std::wstring&, size_t) { return HttpResponse{}; };
    app.tarotAgain(); settle(1.0f);
    app.tarot_.focus = cardWith(0); app.modalKey(VK_RETURN, false); settle(0.8f);
    for (int guard = 0; guard < 10 && app.tarot_.phase == Phase::question; ++guard) { settle(0.8f); app.modalKey(VK_RETURN, false); settle(0.8f); }
    const auto offline_end = GetTickCount64() + 5000;
    while (app.tarot_.waiting && GetTickCount64() < offline_end) { pump(); Sleep(5); }
    settle(2.0f);
    if (app.tarot_.phase != Phase::reading || !app.tarot_.failed || !app.tarot_.results.empty() || app.tarot_.focus != tarot_ui::focus_again) return 8445;
    if (!capture(L"tarot-silent.png")) return 8446;
    // Esc closes; turning the setting off removes it again.
    app.modalKey(VK_ESCAPE, false); settle(0.5f);
    if (app.tarotOpen() || app.topModal() != Modal::none) return 8447;
    // With captures on: the whole deck, four cards at a time.
    if (GetEnvironmentVariableW(L"LAUNCHME_UI_CAPTURE_DIR", nullptr, 0)) {
        app.openTarot(); settle(1.0f);
        int sheet = 0;
        for (const auto& question : tarot::questions())
            for (size_t first = 0; first < question.cards.size(); first += 4) {
                tarot::Spread cards{question.facet, {}};
                for (size_t i = first; i < std::min(first + 4, question.cards.size()); ++i) cards.cards.push_back(static_cast<int>(i));
                app.tarot_.reading.spreads = {cards}; app.tarot_.reading.answers.clear(); app.tarot_.reading.total = 0;
                app.tarot_.phase = Phase::question; app.tarot_.phase_age = 5; app.tarot_.focus = tarot_ui::focus_back; app.tarot_.lift.fill(0);
                if (!capture((L"tarot-deck-" + std::to_wstring(sheet++) + L".png").c_str())) return 8450;
            }
        app.closeTarot(); settle(0.5f);
    }
    app.openSettings(); app.settings_state_.category = static_cast<int>(settings_ui::Category::system); app.buildSettingsRows();
    const auto off = std::find_if(app.settings_rows_.begin(), app.settings_rows_.end(), [](const auto& r) { return r.id == settings_ui::Id::tarot; });
    if (off == app.settings_rows_.end()) return 8448;
    app.settings_state_.options = true; app.settings_state_.selected[app.settings_state_.category] = static_cast<int>(off - app.settings_rows_.begin());
    app.settingsActivate(); app.saveSettings(); app.back(); app.showCategory(0);
    if (app.settings_.tarot || app.tarotAvailable()) return 8449;
    app.settings_dirty_ = false;
    return 0;
}
