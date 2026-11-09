// Included after LauncherUiTest: desktop mode (src/app/desktop*.cpp, src/ui/desktop_view.hpp). The
// library as sections and a poster grid, search, sorting, the installed filter, marking several
// games, favourites and collections by dragging, the pane's data, Ustawienia as a desktop dialog
// (its rows, a switch, a list and Esc), the power menu's mode row and the window strip.
// Nothing reaches the network. Captures: desktop-*.png.

int LauncherUiTest::desktop(HWND window) {
    using namespace desktop_ui;
    Launcher app;
    app.settings_.launch_animation = false;
    app.settings_.tour_done = true;
    app.settings_.glance = app.settings_.companion = false;
    app.hwnd_ = window; app.execute_ = fakeExecute;
    app.overlay_backend_.foreground = [window] { return window; };
    if (!app.renderer_.initialize(window)) return 7600;
    const auto capture = [&](const wchar_t* name) {
        wchar_t folder[4096]{};
        const auto count = GetEnvironmentVariableW(L"LAUNCHME_UI_CAPTURE_DIR", folder, 4096);
        if (!count || count >= 4096) { app.paint(); return true; }
        app.notifications_.clear();
        return app.renderer_.captureForTest(std::filesystem::path(folder) / name, [&] { app.paint(); });
    };
    const auto settle = [&] { for (int i = 0; i < 40; ++i) { app.last_frame_ -= std::chrono::milliseconds(50); app.tick(); } };
    app.desktop_mode_ = true;
    app.audio_.setDesktop(true);

    auto library = std::make_unique<LibraryMessage>();
    library->account = 1;
    for (uint32_t i = 0; i < 18; ++i) {
        Game game{7600 + i, (i % 3 == 0 ? L"Alpha " : i % 3 == 1 ? L"Beta " : L"Gamma ") + std::to_wstring(i)};
        game.last_played = i < 5 ? 1000 + i : 0;
        library->games.push_back(std::move(game));
    }
    SteamCollection favorites{L"Ulubione", {7600, 7601}};
    favorites.id = "user-collections.favorite";
    SteamCollection shooters{L"Strzelanki", {7602}};
    shooters.id = "desktop-shooters";
    library->collections.push_back(std::move(favorites));
    library->collections.push_back(std::move(shooters));
    app.onLibrary(std::move(library));
    app.showCategory(0);
    settle();
    if (!app.desktopActive() || app.desktopMode() != true) return 7601;

    // Sidebar: the library's sections, a store and the collection; every game in the grid.
    const auto entry = [&](const std::string& id) { return app.desktopEntryIndex(id); };
    if (entry("all") < 0 || entry("profile") < 0 || entry("favorites") < 0 || entry("source:steam") < 0 || entry("collection:desktop-shooters") < 0) return 7602;
    if (app.cards_.size() != 18 || app.desktop_entries_[static_cast<size_t>(entry("favorites"))].count != 2) return 7603;
    if (!capture(L"desktop-library.png")) return 7604;
    {
        // After the delay on a game (Filmy w tle) its video plays in the pane's picture.
        std::vector<uint8_t> video(64 * 36 * 4, 0); for (size_t p = 0; p < video.size(); p += 4) { video[p] = 40; video[p + 1] = 200; video[p + 2] = 90; video[p + 3] = 255; }
        wchar_t folder[4096]{};
        const auto count = GetEnvironmentVariableW(L"LAUNCHME_UI_CAPTURE_DIR", folder, 4096);
        // The capture's own target: the frame is uploaded there, already faded in.
        if (count && count < 4096 && !app.renderer_.captureForTest(std::filesystem::path(folder) / L"desktop-library-video.png", [&] {
                app.renderer_.setVideoFrame(video.data(), 64, 36); app.renderer_.settleVideoForTest(); app.paint(); })) return 2791;
        app.renderer_.clearVideo();
    }

    // Ostatnio grane: the sidebar's three latest games (and Kontynuuj grę's); Ulubione keeps its two.
    if (app.desktop_recent_games_.size() != 3 || app.desktop_recent_games_[0].app_id != 7604 || entry("game:" + library_view::selectionKey(app.desktop_recent_games_[0])) < 0) return 7605;
    // Kontynuuj grę stays out while the grid itself is sorted by Ostatnio grane (the same games first).
    {
        const int sort = app.settings_.desktop_sort;
        app.settings_.desktop_sort = library_view::persistedOrder(library_view::Order::last_played);
        if (app.desktopContinueShown()) return 7694;
        app.settings_.desktop_sort = library_view::persistedOrder(library_view::Order::alphabetical);
        if (!app.desktopContinueShown()) return 7695;
        app.settings_.desktop_sort = sort;
    }
    app.desktopShowSection("favorites"); settle();
    if (app.cards_.size() != 2) return 7606;
    app.desktopShowSection("all"); settle();

    // Search filters at once after its pause; the installed filter hides the rest.
    for (const wchar_t c : std::wstring(L"beta")) app.desktopCharacter(c);
    if (!app.desktop_search_focus_) return 7607;
    app.desktopApplyQuery();
    if (app.cards_.size() != 6) return 7608;
    app.desktopKey(VK_ESCAPE, false); // Clears the text, the field keeps the focus…
    if (!app.desktop_query_.empty() || app.cards_.size() != 18 || !app.desktop_search_focus_) return 7609;
    app.desktopKey(VK_ESCAPE, false); // …the second Esc leaves it.
    if (app.desktop_search_focus_ || app.desktop_zone_ != 1) return 7609;
    app.settings_.desktop_installed = true; app.rebuildCards(true);
    if (app.cards_.size() != 18) return 7610; // Every game here is installed.
    app.settings_.desktop_installed = false; app.rebuildCards(true);

    // Keys move the selection along the grid's rows; Ctrl/Shift mark several for the pane's bulk actions.
    app.desktopFocus(0);
    app.desktopMove(1, 0);
    if (app.selected_ != 1) return 7611;
    app.desktopMarkRange(1, 3);
    {
        desktop_ui::View view;
        app.desktopView(view);
        if (view.marked_count != 3 || view.pane.marked != 3) return 7612;
    }
    if (!capture(L"desktop-marked.png")) return 7613;
    {
        // The selection's menu: its artwork asked for again, or the missing covers of these games.
        app.desktopOpenGameMenu(400, 300, false);
        const auto has = [&](desktop_ui::Action action) {
            return app.desktop_menu_ && std::any_of(app.desktop_menu_->items.begin(), app.desktop_menu_->items.end(), [&](const auto& item) { return item.action == action; });
        };
        if (!has(desktop_ui::Action::retry_art) || !has(desktop_ui::Action::missing_art)) return 7690;
        if (!capture(L"desktop-marked-menu.png")) return 7691;
        app.desktop_menu_.reset();
    }
    app.desktop_marked_.clear();

    // Dropping a game on a collection adds it there; on Ulubione, to the favourites.
    app.desktopFocus(5);
    app.desktopDrop(app.desktop_entries_[static_cast<size_t>(entry("collection:desktop-shooters"))], {app.desktop_keys_[5]});
    app.desktopDrop(app.desktop_entries_[static_cast<size_t>(entry("favorites"))], {app.desktop_keys_[5]});
    {
        desktop_ui::View view;
        app.desktopView(view);
        if (view.drop_flash_entry != entry("favorites") || view.drop_flash <= 0.0f) return 7614;
    }
    settle();
    {
        const auto& game = app.cards_[5].game;
        const auto* shooters_collection = app.desktopCollection("desktop-shooters");
        if (!shooters_collection || !app.desktopMember(game, *shooters_collection) || !app.isFavorite(game)) return 7614;
        desktop_ui::View view;
        app.desktopView(view);
        if (!view.pane.game || !view.pane.favorite || view.pane.collections.empty() || view.drop_flash != 0.0f) return 7615;
    }
    // Micro-interactions: favourite star pulse, refresh spin, section collapse chevron and sidebar active glide.
    app.desktopActivate({Target::pane_favorite, 0});
    {
        desktop_ui::View view;
        app.desktopView(view);
        if (view.pane.favorite || view.pane.favorite_pulse <= 0.0f) return 7615;
    }
    app.desktopActivate({Target::pane_favorite, 0});
    app.desktopRefreshLibraries();
    app.last_frame_ -= std::chrono::milliseconds(50);
    app.tick();
    {
        desktop_ui::View view;
        app.desktopView(view);
        if (!view.refreshing || view.refresh_angle <= 0.0f) return 7615;
    }
    app.desktopShowSection("favorites");
    {
        desktop_ui::View view;
        app.desktopView(view);
        if (!view.active_y) return 7615;
    }
    app.desktopShowSection("all");
    app.desktopActivate({Target::sidebar_row, 0}); // Fold Biblioteka, then unfold.
    app.last_frame_ -= std::chrono::milliseconds(50);
    app.tick();
    {
        desktop_ui::View view;
        app.desktopView(view);
        if (view.collapse_progress[0] <= 0.0f || view.collapse_progress[0] >= 1.0f) return 7615;
    }
    app.desktopActivate({Target::sidebar_row, 0});
    settle();

    // Artwork: a poster in hand needs no square cover; one the disk lane misses gets its cover as a
    // stand-in. Queued requests for rows left behind are dropped. Paints between frames wait for
    // the next frame while frames run.
    {
        using Kind = CoverMessage::Kind;
        app.desktopFocus(0);
        const auto queued = [&](const Game& game, Kind kind) {
            std::lock_guard lock(app.jobs_->mutex);
            const auto key = artRequestKey(game, kind);
            for (const auto* queue : {&app.jobs_->jobs, &app.jobs_->secondary})
                if (std::any_of(queue->begin(), queue->end(), [&](const auto& job) { return job.key == key; })) return true;
            return false;
        };
        const auto& with_poster = app.cards_[1].game;
        const auto& missing = app.cards_[2].game;
        if (!queued(with_poster, Kind::poster) || queued(with_poster, Kind::cover) || queued(missing, Kind::cover)) return 7640;
        auto poster = std::make_unique<CoverMessage>(); poster->key = gameKey(with_poster); poster->generation = app.jobs_->generation.load();
        poster->kind = Kind::poster; poster->bytes = pngFixture(60, 60, 90);
        app.onCover(std::move(poster));
        auto miss = std::make_unique<CoverMessage>(); miss->key = gameKey(missing); miss->generation = app.jobs_->generation.load();
        miss->kind = Kind::poster; miss->cache_miss = true;
        app.onCover(std::move(miss));
        if (!app.renderer_.hasPoster(1) || queued(with_poster, Kind::cover) || !queued(missing, Kind::cover)) return 7641;
        // Ordered from the view's middle rows outwards (the pane's own requests first).
        {
            const auto order = app.desktopArtRange().order;
            std::vector<std::string> expected;
            for (const size_t i : order) if (queued(app.cards_[i].game, Kind::poster)) expected.push_back(artRequestKey(app.cards_[i].game, Kind::poster));
            std::vector<std::string> lane;
            std::lock_guard lock(app.jobs_->mutex);
            for (const auto& job : app.jobs_->jobs) if (job.kind == Kind::poster) lane.push_back(job.key);
            if (lane.empty() || lane != expected) return 7642;
        }
        app.last_tick_ms_ = GetTickCount64();
        if (!app.deferPaint() || !app.paint_deferred_) return 7643;
        app.frame_painting_ = true;
        if (app.deferPaint()) return 7644; // The frame's own paint.
        app.frame_painting_ = false; app.paint_deferred_ = false;
        app.last_tick_ms_ = GetTickCount64() - 1000;
        if (app.deferPaint()) return 7645; // No frames run (a modal loop): paint at once.
    }

    // Zmień grafikę from the grid opens on the art the grid shows (Plakat, not the square Okładka
    // of Tryb TV): the picked poster is the card's on return, and Ponów grafiki asks for posters too.
    {
        using Kind = CoverMessage::Kind;
        app.desktopFocus(1);
        const auto game = app.cards_[1].game;
        const auto key = gameKey(game);
        app.openArtworkBrowser(game);
        if (!app.artworkOpen() || app.artwork_state_.kind() != ArtworkKind::poster) return 7646;
        // SteamGridDB knows no game by this exact title: its search runs by itself (once), not an empty gallery.
        {
            const auto unmatched = [&] {
                auto list = std::make_unique<ArtworkBrowserMessage>();
                list->generation = app.artwork_jobs_->generation.load();
                list->type = ArtworkBrowserMessage::Type::list; list->kind = ArtworkKind::poster;
                list->page.error = tr(L"Nie znaleziono tej gry w SteamGridDB.");
                app.onArtworkBrowser(std::move(list));
            };
            unmatched();
            if (!app.artwork_state_.auto_searched || !app.artwork_state_.results_mode || !app.artwork_state_.loading) return 7692;
            app.loadArtworkPage();
            unmatched();
            if (app.artwork_state_.results_mode || app.artwork_state_.loading) return 7693; // Not again in the same visit.
        }
        const auto picked = pngFixture(200, 40, 60);
        constexpr uint32_t picked_id = 0x80000123u;
        if (!cacheArtworkVariant(game, ArtworkKind::poster, picked_id, picked, coverCacheDirectory())) return 7647;
        app.artwork_state_.applying = true; // A file from disk, as "Z dysku…" applies it.
        auto applied = std::make_unique<ArtworkBrowserMessage>();
        applied->generation = app.artwork_jobs_->generation.load();
        applied->type = ArtworkBrowserMessage::Type::local; applied->kind = ArtworkKind::poster;
        applied->asset_id = picked_id; applied->bytes = picked;
        app.onArtworkBrowser(std::move(applied));
        if (const auto* art = app.art_.peek(key); !art || !art->poster || *art->poster != picked ||
            selectedArtworkId(game, ArtworkKind::poster, coverCacheDirectory()) != picked_id) return 7648;
        app.closeArtworkBrowser();
        if (!app.desktopActive() || app.selected_ >= app.cards_.size() || gameKey(app.cards_[app.selected_].game) != key ||
            !app.renderer_.hasPoster(app.selected_)) return 7649;
        const auto poster_request = artRequestKey(game, Kind::poster);
        const auto before = app.art_revisions_[poster_request];
        app.retryGameArtwork(game, false);
        if (app.art_revisions_[poster_request] == before) return 7650;
        settle();
    }

    // Enter on a card picks it the default game action's way: its details (Szczegóły), not a launch.
    {
        app.desktopFocus(0); app.desktop_zone_ = 1;
        const auto picked = app.cards_[0].game.app_id;
        app.desktopKey(VK_RETURN, false);
        if (app.screen_ != ScreenMode::details || app.pending_launch_ || app.detail_games_.empty() ||
            app.detail_games_.front().app_id != picked) return 7651;
        app.desktopPageActivate({SheetHit::Kind::close}, false);
        if (app.screen_ != ScreenMode::shelves || !app.desktopActive()) return 7652;
        settle();
    }

    // Ustawienia opens as a desktop dialog: its rows, a switch flipped from its row, Esc closes it.
    app.openSettings(); settle();
    if (!app.desktopSheet() || app.screen_ != ScreenMode::settings) return 7616;
    if (!capture(L"desktop-settings.png")) return 7617;
    app.settings_state_.selectCategory(static_cast<int>(settings_ui::Category::appearance));
    app.buildSettingsRows();
    const auto row = [&](settings_ui::Id id) {
        for (int i = 0; i < static_cast<int>(app.settings_rows_.size()); ++i) if (app.settings_rows_[static_cast<size_t>(i)].id == id) return i;
        return -1;
    };
    const int tv_row = row(settings_ui::Id::launch_tv_mode);
    if (tv_row < 0 || app.settings_.launch_tv_mode) return 7618;
    app.desktopSettingsActivate({SheetHit::Kind::row, tv_row});
    if (!app.settings_.launch_tv_mode) return 7619;
    app.desktopSettingsActivate({SheetHit::Kind::toggle, tv_row});
    if (app.settings_.launch_tv_mode) return 7620;
    // A choice opens its list under the field; a pick applies it.
    app.openSettingsPage(settings_ui::Page::appearance_all); // The classic list (no tile since 0.7.6).
    app.revealSetting(settings_ui::Id::font);
    const int font_row = row(settings_ui::Id::font);
    if (font_row < 0) return 7621;
    app.desktopSettingsActivate({SheetHit::Kind::field, font_row});
    if (!app.settingsPicking()) return 7622;
    if (!capture(L"desktop-settings-list.png")) return 7623;
    app.back();
    if (app.settingsPicking()) return 7624;
    app.desktopSettingsActivate({SheetHit::Kind::close});
    if (app.screen_ != ScreenMode::shelves || !app.desktopActive()) return 7625;

    // Szczegóły as a desktop dialog, then back.
    app.desktopFocus(0);
    app.openDetails(app.cards_[0].game); settle();
    if (!app.desktopSheet() || app.screen_ != ScreenMode::details) return 7626;
    if (!capture(L"desktop-details.png")) return 7627;
    { // An installation fills the primary button with its share, its state the label.
        auto& install = app.cards_[0].game.installation;
        const auto saved = install;
        install = {InstallPhase::downloading, 37, 100, true};
        if (!capture(L"desktop-details-download.png")) return 7659;
        install = saved;
    }
    { // "…" after Ukryj: the game's menu drops down over the page; Esc closes it, the page stays.
        const SheetHit more{SheetHit::Kind::tile, -1, static_cast<int>(desktop_ui::DetailsExtra::more)};
        app.paint();
        const auto& spots = app.renderer_.desktopHotspots();
        const auto spot = std::find_if(spots.begin(), spots.end(), [&](const auto& item) { return item.hit == more; });
        if (spot == spots.end()) return 7696;
        app.desktopPageActivate(more, false);
        if (!app.desktop_menu_ || app.desktop_menu_->kind != desktop_ui::Menu::Kind::game || app.desktop_menu_->games.size() != 1 ||
            app.desktop_menu_->games[0] != library_view::selectionKey(app.cards_[0].game)) return 7697;
        if (!capture(L"desktop-details-menu.png")) return 7698;
        app.desktopKey(VK_ESCAPE, false);
        if (app.desktop_menu_ || app.screen_ != ScreenMode::details) return 7698;
    }
    app.desktopPageActivate({SheetHit::Kind::close}, false);
    if (app.screen_ != ScreenMode::shelves) return 7628;

    // Motyw okna: the window's surfaces in another tint; Kolorowy obrys i poświata: the selected
    // poster outlined in the cover accent with its art's glow behind it, the plain ring without it.
    {
        const auto saved = app.settings_;
        app.desktopFocus(0); app.desktop_zone_ = 1;
        desktop_ui::View view;
        app.desktopView(view);
        if (!view.poster_glow) return 7684;
        app.settings_.desktop_theme = 1;
        app.settings_.cover_accent_rgb = 0xFF3D7A;
        app.renderer_.setDesktopTheme(app.settings_.desktop_theme);
        app.renderer_.setCoverAccent(app.settings_.cover_accent_rgb);
        // Kolor akcentu and Kolor przycisku Graj: the desktop window's own pair, not Tryb TV's.
        app.settings_.desktop_accent_rgb = 0xFF8A2A; app.settings_.desktop_button_rgb = 0x3B82F6;
        app.applyAccentColors();
        if (app.renderer_.uiAccentForTest() != 0xFF8A2A || app.settings_.ui_accent_rgb == 0xFF8A2A) return 7685;
        const auto grafit = desktop_theme::make(0), granat = desktop_theme::current();
        if (granat.app.b <= grafit.app.b || granat.side.b <= granat.app.b || granat.popup.b <= granat.card.b) return 7685;
        settle();
        if (!capture(L"desktop-theme-navy.png")) return 7686;
        // Widgets in the theme: the drawer in its colours.
        app.desktopToolPick(desktop_ui::Tool::friends);
        app.friends_progress_ = 1;
        if (!app.friendsOpen() || !capture(L"desktop-theme-widgets.png")) return 7686;
        app.toggleFriendsDrawer(); app.friends_progress_ = 0; settle();
        if (app.friendsOpen()) return 7686;
        app.settings_.desktop_theme = 3; app.settings_.cover_accent_rgb = 0;
        app.renderer_.setDesktopTheme(app.settings_.desktop_theme);
        app.renderer_.setCoverAccent(0);
        app.desktopFocus(1); // A poster with its art: the outline takes its colour, the art glows behind.
        settle();
        if (!capture(L"desktop-theme-sand.png")) return 7687;
        app.settings_.desktop_poster_glow = false;
        app.desktopView(view);
        if (view.poster_glow) return 7688;
        settle();
        if (!capture(L"desktop-theme-sand-plain.png")) return 7689;
        app.settings_ = saved;
        app.renderer_.setDesktopTheme(app.settings_.desktop_theme);
        app.renderer_.setCoverAccent(app.settings_.cover_accent_rgb);
        if (desktop_theme::current().app.r != grafit.app.r) return 7699;
    }

    // Czy to u mnie zadziała: above Opis, grey without data, then a verdict that unfolds into a table.
    {
        app.desktopFocus(0); app.desktop_zone_ = 1;
        desktop_ui::View view;
        app.desktopView(view);
        if (!view.pane.hardware.shown || view.pane.hardware.level != 0 || view.pane.hardware.summary != tr(L"Brak danych o wymaganiach")) return 8350;
        Launcher::HardwareData data;
        sysreq::Spec minimum, recommended;
        minimum.cpu = L"Core i7-6700 or Ryzen 5 1600"; minimum.gpu = L"GeForce GTX 1060 6GB or Radeon RX 580 8GB"; minimum.ram = L"12 GB RAM"; minimum.ram_mb = 12288;
        minimum.storage = L"70 GB available space"; minimum.storage_mb = 70 * 1024;
        recommended.cpu = L"Core i5-8400 / Ryzen 5 2600"; recommended.gpu = L"GeForce GTX 1660 / Radeon RX 5500 XT"; recommended.ram = L"16 GB RAM"; recommended.ram_mb = 16384;
        data.needs.minimum = minimum; data.needs.recommended = recommended;
        data.machine.cpu = L"AMD Ryzen 5 3600 6-Core Processor"; data.machine.gpu = L"NVIDIA GeForce RTX 3070";
        data.machine.ram_mb = 32768; data.machine.vram_mb = 8192; data.machine.free_mb = 400 * 1024;
        app.hardwareArrived(gameKey(app.cards_[0].game), data);
        app.desktopView(view);
        const auto& check = view.pane.hardware;
        // An installed game skips the disk row: processor, graphics card, RAM.
        if (check.level != 1 || check.summary != tr(L"Powinna działać płynnie") || check.rows.size() != (app.cards_[0].game.installed ? 3u : 4u)) return 8351;
        if (check.rows[1].yours.find(L"RTX 3070") == std::wstring::npos) return 8351;
        app.paint();
        const auto chip = app.renderer_.desktopHardware();
        const float x = (chip.left + chip.right) * 0.5f, y = (chip.top + chip.bottom) * 0.5f;
        const auto hit = app.desktopHit(x, y);
        if (chip.right <= chip.left || hit.target != Target::pane_hardware || app.desktopCursor(x * app.desktopScale(), y * app.desktopScale()) != IDC_HAND) return 8352;
        if (!capture(L"desktop-pane-hardware.png")) return 8353;
        app.desktopActivate(hit);
        if (!app.desktop_hardware_open_) return 8354;
        app.paint();
        if (!capture(L"desktop-pane-hardware-open.png")) return 8355;
        // A weaker PC turns it red; unfolded or not, the verdict stays.
        data.machine.ram_mb = 8192;
        app.hardwareArrived(gameKey(app.cards_[0].game), data);
        app.desktopView(view);
        if (view.pane.hardware.level != 3 || !capture(L"desktop-pane-hardware-red.png")) return 8356;
        app.desktopActivate(app.desktopHit(x, y));
        if (app.desktop_hardware_open_) return 8357;
    }

    // The pane's achievements number opens Szczegóły with its list; with none, nothing to click.
    {
        const float scale = app.desktopScale();
        app.desktopFocus(0); app.desktop_zone_ = 1;
        app.paint();
        if (app.renderer_.desktopAchievements().right > app.renderer_.desktopAchievements().left) return 7653;
        const auto app_id = app.cards_[0].game.app_id;
        PreviewMessage known; known.app_id = app_id; known.generation = app.preview_.generation();
        known.details.achievements_total = 10; known.details.achievements_unlocked = 4;
        if (!app.preview_.accept(std::move(known))) return 7654;
        app.paint();
        const auto r = app.renderer_.desktopAchievements();
        const float x = (r.left + r.right) * 0.5f, y = (r.top + r.bottom) * 0.5f;
        const auto hit = app.desktopHit(x, y);
        if (r.right <= r.left || hit.target != Target::pane_achievements || app.desktopTooltip(hit).empty() ||
            app.desktopCursor(x * scale, y * scale) != IDC_HAND) return 7655;
        app.desktop_hover_ = hit;
        if (!capture(L"desktop-pane-achievements.png")) return 7656;
        app.desktop_hover_ = {};
        app.desktopActivate(hit);
        if (app.screen_ != ScreenMode::details || !app.achievementsOpen() || app.achievements_.app_id != app_id) return 7657;
        // A row opens the achievement's card over the page; Wstecz (or a click beside it) closes it.
        {
            SteamAchievements list; list.complete = true; list.total = 2; list.unlocked = 1;
            SteamAchievement earned; earned.id = "A"; earned.name = L"First blood"; earned.description = L"Win the first fight of the campaign without losing a unit.";
            earned.achieved = true; earned.unlocked_at = 1709634030; earned.global_percent = 3.2f;
            SteamAchievement locked; locked.id = "B"; locked.name = L"Collector"; locked.description = L"Find every relic."; locked.global_percent = 27.0f;
            list.items = {earned, locked};
            app.showAchievementList(list, true);
            app.paint();
            const auto& spots = app.renderer_.desktopHotspots();
            const auto first = std::find_if(spots.begin(), spots.end(), [](const SheetHotspot& s) { return s.hit.kind == SheetHit::Kind::row && s.hit.index == 0; });
            if (first == spots.end()) return 7660;
            const float rx = (first->rect.left + first->rect.right) * 0.5f, ry = (first->rect.top + first->rect.bottom) * 0.5f;
            const auto first_hit = app.desktopHotspotHit(rx, ry);
            if (first_hit.kind != SheetHit::Kind::row || first_hit.index != 0) return 7661;
            app.desktopPageActivate(first_hit, false);
            if (!app.achievements_.card || app.achievements_.selected != 0) return 7662;
            if (!capture(L"desktop-achievement-card.png")) return 7663;
            app.navigate(1);
            if (!app.achievements_.card || app.achievements_.selected != 1) return 7664;
            app.desktopPageActivate({SheetHit::Kind::dialog_cancel, 1}, false);
            if (app.achievements_.card || !app.achievementsOpen()) return 7665;
            app.desktopPageActivate(first_hit, false);
            app.back(); // Esc: the card first, then the list.
            if (app.achievements_.card || !app.achievementsOpen()) return 7666;
        }
        app.closeAchievements();
        app.desktopPageActivate({SheetHit::Kind::close}, false);
        if (app.screen_ != ScreenMode::shelves || !app.desktopActive()) return 7658;
        settle();
    }

    // The panes: the sidebar narrow (its icons, no headings; the toggle by the logo, animated) and
    // the right pane wider by its edge (fewer columns); both kept in the settings.
    {
        const float scale = app.desktopScale();
        const auto press = [&](float x, float y, bool twice = false) {
            app.click(x * scale, y * scale, false);
            if (twice) app.click(x * scale, y * scale, true);
            app.desktopPointerUp(x * scale, y * scale); app.pointerUp();
        };
        const auto wide = app.desktopLayout();
        const int columns = wide.columns();
        const auto toggle = wide.sidebarToggle();
        const float tx = (toggle.left + toggle.right) * 0.5f, ty = (toggle.top + toggle.bottom) * 0.5f;
        if (app.desktopHit(tx, ty).target != Target::sidebar_toggle || app.desktopCursor(tx * scale, ty * scale) != IDC_HAND) return 7820;
        press(tx, ty);
        app.last_frame_ -= std::chrono::milliseconds(50); app.tick();
        if (!app.settings_.desktop_narrow || app.desktop_narrow_anim_ <= 0.02f || app.desktop_narrow_anim_ >= 0.98f) return 7821; // On its way.
        settle();
        const auto narrow = app.desktopLayout();
        const auto rows = app.desktopRows();
        if (narrow.sidebarRight() != sidebar_narrow || narrow.columns() <= columns ||
            std::any_of(rows.begin(), rows.end(), [](const Row& row) { return row.heading; }) || narrow.companion_card) return 7822;
        // Collections leave the icons (each a folder alone); they are back when it is wide.
        if (std::any_of(rows.begin(), rows.end(), [](const Row& row) { return row.group == Group::collections; }) ||
            std::none_of(app.desktop_entries_.begin(), app.desktop_entries_.end(), [](const Entry& e) { return e.group == Group::collections; })) return 7833;
        const auto first = narrow.sidebarRow(rows[0], app.desktop_sidebar_scroll_);
        const auto title = app.desktopTooltip(app.desktopHit((first.left + first.right) * 0.5f, (first.top + first.bottom) * 0.5f));
        if (title.empty() || title != app.desktop_entries_[static_cast<size_t>(rows[0].entry)].title) return 7823;
        if (!capture(L"desktop-narrow.png")) return 7824;
        // The pane's edge: ↔, dragged 150 to the left, a wider pane and fewer columns.
        const auto edge = narrow.paneEdge();
        const float ex = (edge.left + edge.right) * 0.5f, ey = 400;
        if (app.desktopHit(ex, ey).target != Target::pane_resize || app.desktopCursor(ex * scale, ey * scale) != IDC_SIZEWE) return 7825;
        const float before = narrow.paneWidth();
        app.click(ex * scale, ey * scale, false);
        app.mouseMove((ex - 150) * scale, ey * scale);
        if (app.desktopCursor((ex - 150) * scale, ey * scale) != IDC_SIZEWE) return 7826;
        app.desktopPointerUp((ex - 150) * scale, ey * scale); app.pointerUp();
        const auto widened = app.desktopLayout();
        if (std::abs(widened.paneWidth() - (before + 150)) > 1.5f || std::abs(app.settings_.desktop_pane - (before + 150)) > 1.5f || widened.columns() >= narrow.columns()) return 7827;
        // Never past the grid's minimum.
        app.click((widened.paneLeft()) * scale, ey * scale, false);
        app.mouseMove(10 * scale, ey * scale);
        app.desktopPointerUp(10 * scale, ey * scale); app.pointerUp();
        if (app.desktopLayout().mainWidth() < main_min - 1) return 7828;
        if (!capture(L"desktop-pane-wide.png")) return 7829;
        app.saveSettings();
        const auto kept = loadAppSettings();
        if (!kept.desktop_narrow || kept.desktop_pane != app.settings_.desktop_pane || kept.desktop_pane <= 0) return 7830;
        // A double-click on the edge: back to automatic; the toggle again: wide.
        const auto reset = app.desktopLayout().paneEdge();
        press((reset.left + reset.right) * 0.5f, ey, true);
        if (app.settings_.desktop_pane != 0) return 7831;
        press(tx, ty); settle();
        if (app.settings_.desktop_narrow || app.desktopLayout().sidebarRight() != wide.sidebarRight() || app.desktopLayout().columns() != columns) return 7832;
    }

    // Kadrowanie in its sheet (#3): the cover and both buttons fit the window, and the mouse clicks
    // "Automatyczny" and "Zapisz kadr" where they are drawn.
    {
        const auto crop_file = userConfigDirectory() / L"desktop-crop-fixture.png";
        const auto portrait = pngFixture(40, 64, 96);
        { std::ofstream file(crop_file, std::ios::binary); file.write(reinterpret_cast<const char*>(portrait.data()), static_cast<std::streamsize>(portrait.size())); }
        const auto open = [&] {
            app.openCropEditor(app.cards_[0].game);
            auto message = std::make_unique<CropMessage>();
            message->generation = app.crop_token_->load(); message->image = crop_file; message->bytes = portrait;
            message->width = 64; message->height = 96; message->automatic = {0.5f, 0.5f}; message->focus = {0.5f, 0.2f};
            app.onCrop(std::move(message));
            app.paint();
        };
        open();
        if (!app.cropOpen() || !app.desktopEmbedded()) return 7800;
        if (!capture(L"desktop-crop.png")) return 7801;
        // Window pixels of a point in the editor's canvas, as the sheet draws it.
        const auto at = [&](settings_ui::Rect r) {
            const auto origin = app.renderer_.desktopEmbedOrigin();
            const float scale = app.renderer_.desktopEmbedViewport().scale;
            return std::pair{origin.x + (r.left + r.right) * 0.5f * scale, origin.y + (r.top + r.bottom) * 0.5f * scale};
        };
        const auto canvas = app.renderer_.desktopEmbedViewport();
        const crop_ui::Layout layout{canvas.width, canvas.height, true};
        // The cover is drawn where the mouse finds it (it was drawn at the window's corner, #3):
        // the preview's centre shows the colourful fixture, the space between it and the buttons does not.
        {
            Microsoft::WRL::ComPtr<IWICImagingFactory> wic;
            if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(wic.GetAddressOf())))) return 7806;
            const auto shot = std::filesystem::temp_directory_path() / L"launchme-desktop-crop-probe.png";
            if (!app.renderer_.captureForTest(shot, [&] { app.paint(); })) return 7806;
            std::ifstream input(shot, std::ios::binary);
            const std::vector<uint8_t> bytes(std::istreambuf_iterator<char>{input}, {});
            input.close(); std::error_code ignored; std::filesystem::remove(shot, ignored);
            const auto image = decodeImage(wic.Get(), bytes, {});
            if (!image.valid()) return 7806;
            const auto vivid = [&](std::pair<float, float> point) {
                const auto x = static_cast<uint32_t>(point.first), y = static_cast<uint32_t>(point.second);
                if (x >= image.width || y >= image.height) return false;
                const size_t i = (static_cast<size_t>(y) * image.width + x) * 4;
                const int high = std::max({image.pixels[i], image.pixels[i + 1], image.pixels[i + 2]}), low = std::min({image.pixels[i], image.pixels[i + 1], image.pixels[i + 2]});
                return high - low > 50;
            };
            const auto p = layout.preview();
            if (!vivid(at(p)) || vivid(at({p.right + 20, p.top, p.right + 40, p.bottom}))) return 7807;
        }
        const auto sheet = desktop_ui::embedSheet(app.renderer_.desktopViewport().width, app.renderer_.desktopViewport().height);
        const auto body = desktop_ui::embedBody(sheet);
        // Everything inside the sheet's body.
        const auto inside = [&](settings_ui::Rect r) {
            const auto origin = app.renderer_.desktopEmbedOrigin();
            const float scale = canvas.scale, dips = app.desktopScale();
            const float left = (origin.x + r.left * scale) / dips, right = (origin.x + r.right * scale) / dips;
            const float top = (origin.y + r.top * scale) / dips, bottom = (origin.y + r.bottom * scale) / dips;
            return left >= body.left - 0.5f && right <= body.right + 0.5f && top >= body.top - 0.5f && bottom <= body.bottom + 0.5f;
        };
        if (!inside(layout.preview()) || !inside(layout.button(0)) || !inside(layout.button(1))) return 7802;
        const auto automatic = at(layout.button(0));
        app.mouseMove(automatic.first, automatic.second);
        if (!app.hoverButton(automatic.first, automatic.second)) return 7803;
        app.click(automatic.first, automatic.second, false);
        if (!app.crop_state_.automatic_selected || std::abs(app.crop_state_.focus.y - 0.5f) > 0.001f) return 7804;
        app.crop_state_.focus = {0.5f, 0.2f}; app.crop_state_.automatic_selected = false;
        const auto save = at(layout.button(1));
        app.click(save.first, save.second, false);
        const auto saved = manualTextCrop(crop_file);
        if (app.cropOpen() || !saved || std::abs(saved->y - 0.2f) > 0.01f) return 7805;
    }

    // The power menu's mode row says where it goes; Tryb TV and back keep the library.
    app.openPowerMenu();
    if (app.power_items_.size() < 3 || app.power_items_[0] != power_menu::Item::tv_mode) return 7629;
    app.closePowerMenu();
    app.setDesktopMode(false);
    if (app.desktopMode() || app.desktopActive()) return 7630;
    settle();
    app.setDesktopMode(true);
    settle();
    if (!app.desktopActive() || app.cards_.size() != 18) return 7631;
    return 0;
}
