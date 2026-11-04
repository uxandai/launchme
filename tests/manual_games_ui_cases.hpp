// Included after LauncherUiTest: games added by hand (issue #13 and the editor around it). A game
// saved without its file reads "Wskaż plik" and opens its editor at the file; the editor matches a
// poster by the typed name a moment after typing stops and identifies the game over itself; "Usuń z
// biblioteki" is the game menu's last, red row in Tryb TV and the desktop window, after a question;
// Edytuj metadane offers Zidentyfikuj. Artwork lookups are an injected fake: nothing reaches the
// network. Captures: manual-game-*.png.
int LauncherUiTest::manualGames(HWND window) {
    using custom_game_ui::Item;
    namespace ui = custom_game_ui;
    const auto list_file = userConfigDirectory() / L"custom-games.json";
    std::error_code ec;
    std::filesystem::remove(list_file, ec);
    std::vector<std::pair<std::string, bool>> asked; // Poster lookups: the game's key, and whether it was its own artwork.
    const auto setup = [&](Launcher& app) {
        app.hwnd_ = window; app.execute_ = fakeExecute;
        app.settings_.launch_animation = false; app.settings_.tour_done = true;
        app.settings_.glance = app.settings_.companion = false;
        app.boundary_feedback_ = [] {};
        app.custom_poster_fetch_ = [&](const Game& game, bool own) {
            asked.emplace_back(gameKey(game), own);
            return jpegFixture(600, 900, static_cast<uint32_t>(asked.size()));
        };
        return app.renderer_.initialize(window);
    };
    Launcher app;
    if (!setup(app)) return 9500;
    dispatched.clear();
    const auto capture = [&](Launcher& target, const wchar_t* name) {
        // The poster decodes on the renderer's workers: paint until it is on screen.
        for (int i = 0; i < 400 && target.customEditorOpen() && (target.paint(), target.renderer_.customPosterPending()); ++i) Sleep(5);
        wchar_t folder[4096]{};
        const auto count = GetEnvironmentVariableW(L"LAUNCHME_UI_CAPTURE_DIR", folder, 4096);
        if (!count || count >= 4096) { target.paint(); return true; }
        target.notifications_.clear();
        return target.renderer_.captureForTest(std::filesystem::path(folder) / name, [&] { target.paint(); });
    };
    const auto settle = [&](Launcher& target) { for (int i = 0; i < 40; ++i) { target.last_frame_ -= std::chrono::milliseconds(50); target.tick(); } };
    auto library = std::make_unique<LibraryMessage>(); library->account = 1;
    Game steam{9500, L"Steam fixture"}; steam.last_played = 100; library->games.push_back(steam);
    app.onLibrary(std::move(library));
    auto& editor = app.custom_editor_;

    // Dodaj grę: no name, no poster yet; the panel shows where it will be.
    app.openCustomEditor("");
    if (!app.customEditorOpen() || editor.editing() || !editor.posterShown() || editor.poster != ui::Poster::none || !asked.empty() ||
        !custom_games::validId(editor.pending_id)) return 9501;
    const auto id = editor.pending_id;
    const auto key = "custom:" + id;
    // Typed: nothing is looked up while the name changes, then once it rests (500 ms).
    app.customEditorFocus(Item::name);
    for (const wchar_t ch : std::wstring(L"FL_2025")) app.onCharacter(ch);
    app.pollCustomEditorPoster();
    if (!asked.empty() || editor.poster != ui::Poster::loading) return 9502;
    Sleep(560);
    app.pollCustomEditorPoster();
    if (asked.size() != 1 || asked[0].second || !asked[0].first.starts_with("custom:preview-") || editor.poster != ui::Poster::shown || !app.custom_poster_) return 9503;
    app.pollCustomEditorPoster(); // The same name is not looked up again.
    if (asked.size() != 1) return 9504;
    if (!capture(app, L"manual-game-editor-poster.png")) return 9505;

    // Saved without a file (issue #13): listed, under the ID it was opened with, not playable.
    app.customEditorFocus(Item::save); app.customEditorAccept();
    if (app.customEditorOpen() || app.custom_games_.size() != 1 || app.custom_games_[0].id != id || !app.custom_games_[0].path.empty()) return 9506;
    {
        const auto stored = custom_games::load(list_file);
        if (!stored.readable || stored.games.size() != 1 || stored.games[0].id != id || !stored.games[0].path.empty() || stored.games[0].name != L"FL_2025") return 9507;
    }
    app.openPcLibrary(GameSource::custom); settle(app);
    if (app.cards_.size() != 1 || gameKey(app.cards_[0].game) != key || app.cards_[0].game.installed || !fileNotChosen(app.cards_[0].game) ||
        app.actionLabel(app.cards_[0].game) != L"Wskaż plik" || !app.actionEnabled(app.cards_[0].game)) return 9508;
    if (!capture(app, L"manual-game-library-choose-file.png")) return 9509;
    // "Wskaż plik": its editor at the file's "Wybierz…" (one press picks it); nothing is started.
    app.focusCard(0); app.launch();
    if (!app.customEditorOpen() || editor.id != id || editor.focus != Item::file_browse || app.pending_launch_ || !dispatched.empty()) return 9510;
    // Its saved name: its own artwork.
    if (asked.size() != 2 || asked[1].first != key || !asked[1].second) return 9511;

    // Right at the end of the name reaches "Zidentyfikuj grę" under the poster; Left goes back.
    app.customEditorFocus(Item::name); app.customEditorKey(VK_END, false); app.customEditorKey(VK_RIGHT, false);
    if (editor.focus != Item::identify) return 9512;
    app.customEditorKey(VK_LEFT, false);
    if (editor.focus != Item::name) return 9513;
    app.customEditorFocus(Item::file_browse); app.customEditorKey(VK_RIGHT, false);
    if (editor.focus != Item::identify) return 9514;
    app.customEditorKey(VK_DOWN, false);
    if (editor.focus != Item::save) return 9515;
    app.customEditorFocus(Item::identify);
    if (!capture(app, L"manual-game-editor-identify.png")) return 9516;
    // Zidentyfikuj grę opens over the panel, which waits beneath it.
    app.customEditorKey(VK_RETURN, false);
    if (!app.identifyOpen() || !app.customEditorOpen() || app.topModal() != Launcher::Modal::identify || app.identify_state_.query != L"FL_2025" ||
        gameKey(app.identify_state_.game) != key) return 9517;
    app.identifyManual();
    auto search = std::make_unique<IdentifyMessage>(); search->generation = app.identify_token_->load();
    search->search.games = {{9501, L"Football League 2025", L"2025-03-01", L"PC"}, {9502, L"FL 2025 Manager", L"2025-06-01", L"PC"}};
    app.onIdentify(std::move(search));
    if (!capture(app, L"manual-game-identify-over-editor.png")) return 9518;
    app.launch(); // The first match.
    if (!app.identify_state_.applying) return 9519;
    auto identified = std::make_unique<IdentifyMessage>(); identified->generation = app.identify_token_->load(); identified->identified = true;
    rawg::Metadata metadata; metadata.id = 9501; metadata.name = L"Football League 2025";
    metadata.details.developer = L"Fixture Studio";
    identified->metadata = metadata;
    app.onIdentify(std::move(identified));
    // The panel again, its poster now the identified game's own artwork.
    if (app.identifyOpen() || !app.customEditorOpen() || app.topModal() != Launcher::Modal::custom_editor || editor.art_name != L"FL_2025" ||
        !app.rawg_metadata_.contains(key) || asked.size() != 3 || asked[2].first != key || !asked[2].second || editor.poster != ui::Poster::shown) return 9520;
    // The removal in the panel is named for what it does.
    app.customEditorFocus(Item::remove);
    if (!capture(app, L"manual-game-editor-remove.png")) return 9521;
    app.customEditorKey(VK_ESCAPE, false);
    if (app.customEditorOpen() || app.custom_games_.size() != 1 || !app.rawg_metadata_.contains(key)) return 9522;

    // A new game identified and then cancelled leaves nothing under its key.
    app.openCustomEditor("");
    const auto ghost = "custom:" + editor.pending_id;
    app.customEditorFocus(Item::name);
    for (const wchar_t ch : std::wstring(L"Ghost game")) app.onCharacter(ch);
    app.customEditorFocus(Item::identify); app.customEditorAccept();
    if (!app.identifyOpen()) return 9523;
    app.identifyManual();
    search = std::make_unique<IdentifyMessage>(); search->generation = app.identify_token_->load();
    search->search.games = {{9503, L"Ghost Game", L"2024-01-01", L"PC"}};
    app.onIdentify(std::move(search));
    app.launch();
    identified = std::make_unique<IdentifyMessage>(); identified->generation = app.identify_token_->load(); identified->identified = true;
    metadata.id = 9503; metadata.name = L"Ghost Game"; identified->metadata = metadata;
    app.onIdentify(std::move(identified));
    if (!editor.identified_new || !app.rawg_metadata_.contains(ghost)) return 9524;
    app.customEditorKey(VK_ESCAPE, false);
    if (app.customEditorOpen() || app.rawg_metadata_.contains(ghost) || app.custom_games_.size() != 1) return 9525;
    // A name typed without a lookup yet: an empty one asks for nothing.
    app.openCustomEditor("");
    app.customEditorFocus(Item::identify); app.customEditorAccept();
    if (app.identifyOpen() || editor.error.empty() || editor.focus != Item::name) return 9526;
    app.closeCustomEditor();

    // The game's menu: "Usuń z biblioteki" last (red), never Odinstaluj; Details' "…" holds it too.
    const auto game = app.custom_library_.front();
    app.openPcLibrary(GameSource::custom); settle(app);
    app.focusCard(0); app.openContextMenu(); settle(app);
    auto rows = app.menuRows();
    if (!app.menu_open_ || !rows.remove || rows.uninstall || context_menu::row(context_menu::Item::remove, rows) != context_menu::rows(rows) - 1) return 9527;
    if (!capture(app, L"manual-game-menu-remove.png")) return 9528;
    app.dismissMenu(); app.openDetails(game);
    app.detail_action_ = 1; app.enterDown(false); app.enterUp(); settle(app);
    rows = app.menuRows();
    if (!app.menu_open_ || !rows.detail_options || !rows.remove || !rows.launch || !rows.metadata ||
        context_menu::row(context_menu::Item::remove, rows) != context_menu::rows(rows) - 1) return 9529;
    if (!capture(app, L"manual-game-details-options.png")) return 9530;
    // It asks first, defaulting to its editor; Esc keeps the game.
    app.menu_selected_ = context_menu::row(context_menu::Item::remove, rows); app.menuAccept();
    if (!app.exit_prompt_ || *app.exit_prompt_ != 1 || app.prompt_kind_ != Launcher::PromptKind::remove || app.menu_open_) return 9531;
    if (!capture(app, L"manual-game-remove-prompt.png")) return 9532;
    app.exitPromptKey(VK_ESCAPE, false);
    if (app.exit_prompt_ || app.prompt_kind_ != Launcher::PromptKind::exit || app.custom_games_.size() != 1) return 9533;
    // The middle answer opens its editor.
    app.askRemoveFromLibrary(game); app.exitPromptAccept();
    if (!app.customEditorOpen() || editor.id != id || app.custom_games_.size() != 1) return 9534;
    app.closeCustomEditor();
    // "Opcje uruchamiania" of a game added by hand is its whole editor ("Edytuj grę" in the menu).
    if (!app.launchEditable(game)) return 9535;

    // Edytuj metadane (Tryb TV): Zidentyfikuj, in place of the dialog. A Steam game has none.
    app.openMetadata(game);
    if (!app.metadataOpen() || !app.desktop_meta_.identify) return 9536;
    app.desktop_meta_.focus = static_cast<int>(desktop_ui::MetaItem::identify);
    if (!capture(app, L"manual-game-metadata-identify.png")) return 9537;
    app.metadataKey(VK_RETURN, false);
    if (app.metadataOpen() || !app.identifyOpen() || gameKey(app.identify_state_.game) != key) return 9538;
    app.closeIdentify();
    app.openMetadata(steam);
    if (!app.metadataOpen() || app.desktop_meta_.identify) return 9539;
    app.desktopMetaClose();
    // Saved from Szczegóły in Tryb TV: the screen shows the new name at once, as the desktop window does.
    app.openDetails(game); settle(app);
    app.openMetadata(game);
    if (!app.metadataOpen()) return 9590;
    app.desktop_meta_.fields[0] = L"Renamed by hand";
    if (!capture(app, L"manual-game-metadata-tv.png")) return 9591;
    app.desktopMetaSave();
    if (app.metadataOpen() || app.detail_games_.empty() || app.detail_games_[0].name != L"Renamed by hand") return 9592;
    // Another title keeps the game's identification (Zidentyfikuj: metadata\<key>.rawg.json); only what was
    // looked up by the old title goes (HowLongToBeat's answer).
    {
        const auto config = userConfigDirectory();
        const auto cache = text_encoding::wideLenient(gameCacheKey(game));
        const auto identification = config / L"metadata" / (cache + L".rawg.json");
        const auto hltb = config / L"plugins" / L"data" / L"launchme.howlongtobeat" / (cache + L".json");
        std::filesystem::create_directories(identification.parent_path()); std::filesystem::create_directories(hltb.parent_path());
        { std::ofstream(identification) << "{}"; std::ofstream(hltb) << "{}"; }
        app.rawg_metadata_[key] = {};
        app.openMetadata(game);
        app.desktop_meta_.fields[0] = L"Renamed again";
        app.desktopMetaSave();
        std::error_code missing;
        if (app.metadataOpen() || !std::filesystem::exists(identification, missing) || std::filesystem::exists(hltb, missing) ||
            !app.rawg_metadata_.contains(key) || app.detail_games_[0].name != L"Renamed again") return 9593;
        std::filesystem::remove(identification, missing);
        app.rawg_metadata_.erase(key);
    }
    app.back();

    // Usuń z biblioteki: the entry, its artwork and history go; its file (none here) is never touched.
    app.askRemoveFromLibrary(game); app.exitPromptMove(-1); app.exitPromptAccept();
    if (!app.custom_games_.empty() || !custom_games::load(list_file).games.empty() || app.notifications_.empty() ||
        app.notifications_.back().title != L"Usunięto z biblioteki" || app.canRemoveFromLibrary(game)) return 9540;

    // The desktop window: the game's menu, its question, the editor with its poster and Edytuj metadane.
    {
        custom_games::Entry entry; entry.id = "95009500aaaabbbb"; entry.name = L"Desktop wishlist game";
        if (!custom_games::save({entry}, list_file)) return 9541;
        Launcher desk;
        if (!setup(desk)) return 9542;
        desk.desktop_mode_ = true;
        desk.audio_.setDesktop(true);
        desk.loadCustomGames();
        auto games = std::make_unique<LibraryMessage>(); games->account = 1; games->games.push_back(steam);
        desk.onLibrary(std::move(games));
        desk.showCategory(0); desk.desktopShowSection("all"); settle(desk);
        // Games added by hand are DRM-free's: its section lists them, there is no section of their own.
        {
            desk.buildDesktopSidebar();
            if (desk.desktopEntryIndex("source:drmfree") < 0 || desk.desktopEntryIndex("source:custom") >= 0) return 9594;
            desk.desktopShowSection("source:drmfree"); settle(desk);
            if (std::none_of(desk.cards_.begin(), desk.cards_.end(), [&](const GameCard& card) { return card.game.source_id == entry.id; })) return 9595;
            desk.desktopShowSection("all"); settle(desk);
        }
        const auto at = std::find_if(desk.cards_.begin(), desk.cards_.end(), [&](const GameCard& card) { return card.game.source_id == entry.id; });
        if (!desk.desktopActive() || at == desk.cards_.end()) return 9543;
        desk.desktopFocus(static_cast<size_t>(at - desk.cards_.begin()));
        const auto wished = at->game;
        desk.desktopOpenGameMenu(400, 300, false);
        const auto item = [&](desktop_ui::Action action) -> const desktop_ui::MenuItem* {
            if (!desk.desktop_menu_) return nullptr;
            for (const auto& each : desk.desktop_menu_->items) if (each.action == action) return &each;
            // The game's groups are submenus: look inside them too.
            static desktop_ui::MenuItem found;
            for (size_t i = 0; i < desk.desktop_menu_->items.size(); ++i) {
                if (desk.desktop_menu_->items[i].action != desktop_ui::Action::group) continue;
                for (const auto& each : desk.desktopGroupItems(desk.desktopGamesOf(desk.desktop_menu_->games).front(), desk.desktop_menu_->items[i].value))
                    if (each.action == action) { found = each; return &found; }
            }
            return nullptr;
        };
        const auto* remove = item(desktop_ui::Action::remove_game);
        const auto* edit = item(desktop_ui::Action::launch_options);
        const auto* play = item(desktop_ui::Action::play);
        if (!remove || !remove->danger || &desk.desktop_menu_->items.back() != remove || !edit || edit->label != L"Edytuj grę…" ||
            !play || play->label != L"Wskaż plik" || !play->enabled || item(desktop_ui::Action::uninstall)) return 9544;
        if (!capture(desk, L"manual-game-desktop-menu.png")) return 9545;
        desk.desktopMenuPick(*remove, false);
        if (!desk.exit_prompt_ || desk.prompt_kind_ != Launcher::PromptKind::remove) return 9546;
        if (!capture(desk, L"manual-game-desktop-remove-prompt.png")) return 9547;
        desk.exitPromptKey(VK_ESCAPE, false);
        // Wskaż plik: the editor at the file field, the poster beside it.
        desk.desktopLaunchSelected();
        if (!desk.customEditorOpen() || desk.custom_editor_.focus != Item::file || desk.custom_editor_.poster != ui::Poster::shown) return 9548;
        if (!capture(desk, L"manual-game-desktop-editor.png")) return 9549;
        // A click on "Zidentyfikuj grę" (as the dialog recorded it) opens it over the editor.
        desk.paint();
        const auto& spots = desk.renderer_.desktopModalHotspots();
        const auto identify = std::find_if(spots.begin(), spots.end(), [](const auto& spot) {
            return spot.hit == desktop_ui::SheetHit{desktop_ui::SheetHit::Kind::action, static_cast<int>(Item::identify)};
        });
        if (identify == spots.end()) return 9550;
        const float scale = desk.renderer_.desktopViewport().scale;
        desk.desktopModalClick((identify->rect.left + identify->rect.right) * 0.5f * scale, (identify->rect.top + identify->rect.bottom) * 0.5f * scale, false);
        if (!desk.identifyOpen() || !desk.customEditorOpen()) return 9551;
        desk.closeIdentify(); desk.closeCustomEditor();
        desk.desktopMetaOpen(wished);
        if (!desk.desktop_meta_.open || !desk.desktop_meta_.identify) return 9552;
        if (!capture(desk, L"manual-game-desktop-metadata.png")) return 9553;
        desk.desktopMetaActivate(static_cast<int>(desktop_ui::MetaItem::identify));
        if (desk.desktop_meta_.open || !desk.identifyOpen()) return 9554;
        desk.closeIdentify();
    }
    std::filesystem::remove(list_file, ec);
    dispatched.clear();
    return 0;
}
