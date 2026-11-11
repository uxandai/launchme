// The Stores' built-in tabs: Launchery (tiles, an installed one opens, one click installs through a fake
// backend, the vendor's installer handed over, the Linux question with its prefixes) and Promocje (a
// fake feed, a tile opens its store page), in Tryb TV and in the desktop window; the setting hides both.
int LauncherUiTest::storeHub(HWND window) {
    using launchers::Method;
    struct World {
        std::set<std::string> installed{"steam"};
        std::vector<std::wstring> log;
        std::optional<unsigned long> winget = 0ul;
        std::mutex mutex;
    };
    const auto world = std::make_shared<World>();
    const auto prepare = [&](Launcher& app, launchers::Machine machine) {
        app.hwnd_ = window; app.execute_ = fakeExecute;
        app.browse_ = [](const std::wstring& url) { dispatched.push_back(url); };
        app.settings_.launch_animation = false; app.settings_.glance = app.settings_.companion = false;
        app.launcher_windows_ = machine.windows;
        app.launcher_machine_ = [machine] { return machine; };
        app.launcher_probe_ = [world, machine] {
            launchers::Probe probe;
            probe.windows = machine.windows;
            probe.variable = [](std::wstring_view name) { return L"C:\\" + std::wstring(name); };
            // Installed: the first known place of each launcher the world holds.
            probe.exists = [world](const std::filesystem::path& file) {
                std::lock_guard lock(world->mutex);
                for (const auto& entry : launchers::catalog())
                    if (world->installed.contains(std::string(entry.id)) && !entry.programs[0].empty() &&
                        file.filename().wstring() == std::wstring(entry.programs[0].substr(entry.programs[0].rfind(L'\\') + 1))) return true;
                return false;
            };
            probe.prefix_users = {L"steamuser"};
            probe.wine_prefix = [world](std::string_view id) {
                std::lock_guard lock(world->mutex);
                return world->installed.contains(std::string(id)) ? std::filesystem::path(L"C:\\prefixes\\made") : std::filesystem::path{};
            };
            return probe;
        };
        app.launcher_prefixes_ = [] { return std::vector<std::filesystem::path>{L"C:\\prefixes\\default", L"C:\\prefixes\\battle-net"}; };
        app.launcher_new_prefix_ = [](const std::wstring&) { return std::filesystem::path(L"C:\\prefixes\\made"); };
        app.launcher_backend_ = [world] {
            launcher_install::Backend backend;
            backend.winget = L"winget.exe"; backend.flatpak = L"flatpak";
            backend.run = [world](const std::filesystem::path& program, const std::vector<std::wstring>& arguments, const std::function<bool()>&) {
                std::lock_guard lock(world->mutex);
                world->log.push_back(program.wstring() + L' ' + arguments.front() + L' ' + arguments.back());
                if (world->winget && *world->winget == 0 && arguments.size() > 2)
                    for (const auto& entry : launchers::catalog()) {
                        if (entry.winget.empty() || std::wstring(entry.winget.begin(), entry.winget.end()) != arguments[2]) continue;
                        if (arguments.front() == L"uninstall") world->installed.erase(std::string(entry.id)); else world->installed.insert(std::string(entry.id));
                    }
                return world->winget;
            };
            backend.download = [world](const std::wstring& url, const std::filesystem::path&, const std::function<bool()>&) {
                std::lock_guard lock(world->mutex); world->log.push_back(L"download " + url); return true;
            };
            backend.trusted = [](const std::filesystem::path&) { return true; };
            backend.open = [world](const std::filesystem::path& file) { std::lock_guard lock(world->mutex); world->log.push_back(L"open " + file.filename().wstring()); return true; };
            backend.wine = [world](const launchers::Entry& entry, const std::filesystem::path&, const std::filesystem::path& prefix, const std::function<bool()>&) -> std::optional<unsigned long> {
                std::lock_guard lock(world->mutex);
                world->log.push_back(L"wine " + std::wstring(entry.name) + L' ' + prefix.wstring());
                world->installed.insert(std::string(entry.id));
                return 0ul;
            };
            return backend;
        };
        app.store_hub_image_ = [](const std::string& image) {
            if (image.starts_with("logo:") || image.starts_with("system:")) return pngFixture(255, 160, 64, true);
            return jpegFixture(320, 180, static_cast<uint32_t>(std::hash<std::string>{}(image)));
        };
        app.promos_region_ = [] { promos::Region region; region.country = "PL"; region.currency = "PLN"; region.polish = true; return region; };
        app.promos_get_ = [](const std::wstring& url) {
            const int64_t soon = static_cast<int64_t>(std::time(nullptr)) + 5 * 86400;
            const auto day = [](int64_t time) {
                const std::time_t value = static_cast<std::time_t>(time); std::tm utc{}; gmtime_s(&utc, &value);
                char text[40]; strftime(text, sizeof(text), "%Y-%m-%dT%H:%M:%S.000Z", &utc); return std::string(text);
            };
            std::string body;
            if (url.find(L"epicgames") != url.npos)
                body = R"({"data":{"Catalog":{"searchStore":{"elements":[{"title":"Latarnia na Krańcu","catalogNs":{"mappings":[{"pageSlug":"latarnia"}]},"keyImages":[{"type":"OfferImageWide","url":"https://cdn1.epicgames.com/latarnia.jpg"}],"price":{"totalPrice":{"discountPrice":0,"originalPrice":14200,"currencyCode":"PLN"}},"promotions":{"promotionalOffers":[{"promotionalOffers":[{"startDate":"2020-01-01T16:00:00.000Z","endDate":")" +
                       day(soon) + R"(","discountSetting":{"discountType":"PERCENTAGE","discountPercentage":0}}]}]}},{"title":"Miasto Mgieł","urlSlug":"miasto","keyImages":[{"type":"OfferImageWide","url":"https://cdn1.epicgames.com/miasto.jpg"}],"price":{"totalPrice":{"discountPrice":6900,"originalPrice":6900,"currencyCode":"PLN"}},"promotions":{"upcomingPromotionalOffers":[{"promotionalOffers":[{"startDate":")" +
                       day(soon) + R"(","endDate":")" + day(soon + 7 * 86400) + R"(","discountSetting":{"discountType":"PERCENTAGE","discountPercentage":0}}]}]}}]}}}})";
            else if (url.find(L"steampowered") != url.npos)
                body = R"({"specials":{"items":[{"id":10,"type":0,"name":"Gwiezdny Kurier","discount_percent":75,"original_price":10000,"final_price":2500,"currency":"PLN","large_capsule_image":"https://shared.akamai.steamstatic.com/a.jpg","discount_expiration":)" +
                       std::to_string(soon) + R"(},{"id":11,"type":0,"name":"Ogród Zegarów","discount_percent":40,"original_price":8000,"final_price":4800,"currency":"PLN","large_capsule_image":"https://shared.akamai.steamstatic.com/b.jpg"},{"id":12,"type":0,"name":"Rzeka Snów","discount_percent":20,"original_price":5000,"final_price":4000,"currency":"PLN","large_capsule_image":"https://shared.akamai.steamstatic.com/c.jpg"}]}})";
            else if (url.find(L"gog.com") != url.npos)
                body = "{\"products\":[{\"title\":\"Kroniki Mg\\u0142y\",\"storeLink\":\"https://www.gog.com/pl/game/kroniki\",\"coverHorizontal\":\"https://images.gog-statics.com/aaaaaaaaaaaa.png\","
                       "\"price\":{\"final\":\"9,99\\u00a0z\\u0142\",\"base\":\"39,99\\u00a0z\\u0142\",\"discount\":\"-75%\",\"finalMoney\":{\"amount\":\"9.99\"}}},"
                       "{\"title\":\"Ostatnia Latarnia\",\"storeLink\":\"https://www.gog.com/pl/game/ostatnia\",\"coverHorizontal\":\"https://images.gog-statics.com/bbbbbbbbbbbb.png\","
                       "\"price\":{\"final\":\"0\",\"base\":\"19,99\\u00a0z\\u0142\",\"discount\":\"-100%\",\"finalMoney\":{\"amount\":\"0.00\"}}}]}";
            else
                body = R"([{"title":"Miasto Kości (itch.io) Giveaway","worth":"$4.99","image":"https://www.gamerpower.com/offers/1b/kosci.jpg","open_giveaway_url":"https://www.gamerpower.com/open/miasto-kosci","type":"Game","platforms":"PC, Itch.io, DRM-Free","end_date":"N/A","status":"Active"}])";
            return HttpResponse{200, {body.begin(), body.end()}};
        };
    };
    const auto pumpFor = [&](Launcher& app, const std::function<bool()>& done) {
        for (int i = 0; i < 1000; ++i) {
            MSG message{};
            while (PeekMessageW(&message, window, remote_message, remote_message, PM_REMOVE))
                app.onRemote(std::unique_ptr<RemoteMessage>(reinterpret_cast<RemoteMessage*>(message.lParam)));
            if (done()) return true;
            Sleep(5);
        }
        return false;
    };
    const auto capture = [&](Launcher& app, const wchar_t* name) {
        wchar_t folder[4096]{};
        if (!GetEnvironmentVariableW(L"LAUNCHME_UI_CAPTURE_DIR", folder, 4096)) { app.paint(); return true; }
        return app.renderer_.captureForTest(std::filesystem::path(folder) / name, [&] { app.paint(); });
    };
    const auto rowOf = [](Launcher& app, std::string_view id) {
        for (size_t i = 0; i < app.plugin_page_.rows.size(); ++i) if (app.plugin_page_.rows[i].id == id) return static_cast<int>(i);
        return -1;
    };
    const auto command = [](Launcher& app, std::string_view name) {
        for (size_t i = 0; i < app.plugin_page_.rows.size(); ++i) if (app.plugin_page_.rows[i].command == name) return static_cast<int>(i);
        return -1;
    };
    // A tile opens its card (a dialog); `choice` is then the row picked on it. False when either is missing.
    const auto pick = [&](Launcher& app, std::string_view id, std::string_view choice) {
        if (!app.plugin_page_.dialog) {
            const int tile = rowOf(app, id);
            if (tile < 0) return false;
            app.plugin_page_.focus = tile; app.pluginPageAccept();
        }
        if (!app.plugin_page_.dialog || app.store_hub_.choosing != id) return false;
        const int row = command(app, choice);
        if (row < 0) return false;
        app.plugin_page_.focus = row; app.pluginPageAccept();
        return true;
    };
    const auto pictures = [](Launcher& app) {
        return app.store_hub_.image_pending.empty() && std::all_of(app.plugin_page_.rows.begin(), app.plugin_page_.rows.end(), [&](const auto& row) {
            return !row.tile || row.image.empty() || app.store_hub_.images.contains(row.image); });
    };
    const auto logged = [&](std::wstring_view text) {
        std::lock_guard lock(world->mutex);
        return std::any_of(world->log.begin(), world->log.end(), [&](const auto& line) { return line.find(text) != line.npos; });
    };

    {
        Launcher app;
        prepare(app, {true, true, false, false});
        if (!app.renderer_.initialize(window)) return 12300;
        {
            auto library = std::make_unique<LibraryMessage>(); library->account = 1;
            for (uint32_t i = 0; i < 4; ++i) library->games.push_back(Game{12300 + i, L"Gra " + std::to_wstring(i + 1)});
            app.onLibrary(std::move(library)); app.showCategory(0);
        }
        // On by default; the Stores entry opens Launchery at once, with no plugin installed, under the two tabs.
        if (!app.settings_.store_hub || !loadAppSettings().store_hub || app.storeTabCount() != 2) return 12301;
        app.openStores();
        if (app.screen_ != ScreenMode::plugin_page || !app.storeTabsShown() || !app.storeHubPage() || app.storeTabIndex() != 0 || !app.plugin_page_.tiled ||
            app.plugin_page_.title != tr(L"Launchery") || app.plugin_page_.columns < 2) return 12302;
        ViewContext context; app.inputHints(context);
        if (!pumpFor(app, [&] { return app.store_hub_.machine_known && !app.store_hub_.detecting && pictures(app); })) return 12303;
        const int steam = rowOf(app, "steam"), gog = rowOf(app, "gog"), epic = rowOf(app, "epic"), esde = rowOf(app, "esde");
        if (steam < 0 || gog < 0 || epic < 0 || esde < 0 || rowOf(app, "lutris") >= 0) return 12304; // Lutris is Linux's.
        if (app.plugin_page_.rows[steam].badge != tr(L"Zainstalowany") || !app.plugin_page_.rows[gog].badge.empty() || app.plugin_page_.focus != steam) return 12305;
        if (!capture(app, L"stores-launchers.png")) return 12306;
        // The grid: right along the line, down to the next one, left of the first column the back button.
        app.pluginPageSide(1); if (app.plugin_page_.focus != steam + 1) return 12307;
        app.changeCategory(1); if (app.plugin_page_.rows[app.plugin_page_.focus].top <= app.plugin_page_.rows[steam].top) return 12308;
        app.changeCategory(-1); app.pluginPageSide(-1); app.pluginPageSide(-1);
        if (app.plugin_page_.focus != -1) return 12309;
        app.pluginPageSide(1); if (app.plugin_page_.focus != steam) return 12310;
        // No line of text stands under the tabs once the launchers are known.
        if (!app.plugin_page_.status.empty()) return 12341;
        // A tile never acts by itself: it opens the launcher's card. Installed: open, uninstall, its page.
        dispatched.clear();
        app.pluginPageAccept();
        if (!app.plugin_page_.dialog || app.store_hub_.choosing != "steam" || !dispatched.empty() || !app.store_hub_.jobs.empty()) return 12311;
        if (command(app, "open") < 0 || command(app, "uninstall") < 0 || command(app, "page") < 0 || command(app, "cancel") < 0 || command(app, "install") >= 0 ||
            app.plugin_page_.focus != command(app, "open") || app.plugin_page_.status != tr(launchers::find("steam")->about)) return 12342;
        if (!capture(app, L"stores-launcher-card.png")) return 12343;
        if (!pick(app, "steam", "open") || app.plugin_page_.dialog || dispatched.size() != 1 || !dispatched[0].ends_with(L"steam.exe")) return 12344;
        // Back closes a card onto its tab.
        app.plugin_page_.focus = steam; app.pluginPageAccept(); app.back();
        if (app.plugin_page_.dialog || !app.storeHubPage() || app.plugin_page_.focus != steam) return 12345;
        // Not installed: the card offers the installation and says how; only its row starts it.
        app.plugin_page_.focus = gog; app.pluginPageAccept();
        if (!app.plugin_page_.dialog || command(app, "install") < 0 || command(app, "open") >= 0 || command(app, "uninstall") >= 0 || !app.store_hub_.jobs.empty() ||
            app.plugin_page_.rows[command(app, "install")].subtitle != L"winget install GOG.Galaxy") return 12346;
        if (!capture(app, L"stores-launcher-card-install.png")) return 12347;
        if (!pick(app, "gog", "install") || app.plugin_page_.dialog) return 12348;
        if (!app.store_hub_.jobs.contains("gog") || !app.plugin_page_.rows[gog].busy) return 12312;
        // Its card while it installs: nothing to pick but closing.
        app.plugin_page_.focus = gog; app.pluginPageAccept();
        if (!app.plugin_page_.dialog || command(app, "install") >= 0 || command(app, "open") >= 0 || !pick(app, "gog", "cancel")) return 12349;
        if (!pumpFor(app, [&] { return app.store_hub_.jobs.empty() && !app.store_hub_.detecting && app.store_hub_.found.contains("gog"); })) return 12313;
        if (!logged(L"winget.exe install --disable-interactivity") || app.plugin_page_.rows[gog].badge != tr(L"Zainstalowany") || app.plugin_page_.rows[gog].busy) return 12314;
        { std::lock_guard lock(world->mutex); if (world->log.size() != 1) return 12315; }
        // Uninstalling asks once more, opening on "Anuluj"; Anuluj returns to the card, the answer removes it.
        { std::lock_guard lock(world->mutex); world->log.clear(); }
        if (!pick(app, "gog", "uninstall") || !app.plugin_page_.dialog || command(app, "uninstall-yes") < 0 || app.plugin_page_.focus != command(app, "back") ||
            !app.store_hub_.jobs.empty()) return 12350;
        if (!capture(app, L"stores-launcher-card-uninstall.png")) return 12351;
        app.pluginPageAccept();
        if (!app.plugin_page_.dialog || command(app, "uninstall") < 0 || !app.store_hub_.jobs.empty()) return 12352;
        if (!pick(app, "gog", "uninstall") || !pick(app, "gog", "uninstall-yes") || app.plugin_page_.dialog || !app.store_hub_.jobs.contains("gog") ||
            app.plugin_page_.rows[gog].badge != tr(L"Odinstalowywanie…")) return 12353;
        if (!pumpFor(app, [&] { return app.store_hub_.jobs.empty() && !app.store_hub_.detecting; })) return 12354;
        if (!logged(L"winget.exe uninstall --disable-interactivity") || app.store_hub_.found.contains("gog") || !app.plugin_page_.rows[gog].badge.empty()) return 12355;
        // winget fails: the vendor's installer is opened and the tile waits for the user to finish it.
        { std::lock_guard lock(world->mutex); world->winget = 5ul; world->log.clear(); }
        if (!pick(app, "epic", "install")) return 12356;
        if (!pumpFor(app, [&] { return app.store_hub_.jobs.empty() && !app.store_hub_.detecting; })) return 12316;
        if (!logged(L"open EpicGamesLauncherInstaller.msi") || !app.store_hub_.handed.contains("epic") || !app.plugin_page_.rows[epic].busy ||
            app.plugin_page_.rows[epic].badge != tr(L"Instalator otwarty")) return 12317;
        if (!pumpFor(app, [&] { return pictures(app); }) || !capture(app, L"stores-launchers-installing.png")) return 12318;
        { std::lock_guard lock(world->mutex); world->installed.insert("epic"); }
        app.store_hub_.detect_at = 0; app.pollStoreHub();
        if (!pumpFor(app, [&] { return app.store_hub_.handed.empty() && app.store_hub_.found.contains("epic"); })) return 12319;
        // A launcher nothing installs here: its card has only the download page.
        dispatched.clear();
        app.plugin_page_.focus = esde; app.pluginPageAccept();
        if (!app.plugin_page_.dialog || command(app, "install") >= 0 || !pick(app, "esde", "page")) return 12357;
        if (dispatched != std::vector<std::wstring>{L"https://es-de.org/#Download"} || !app.store_hub_.jobs.empty()) return 12320;

        // Promocje: RB; the feed arrives, tiles by section; a tile opens its store page.
        app.switchStoreTab(1);
        if (app.storeTabIndex() != 1 || app.plugin_page_.plugin != store_hub::promos_id || !app.plugin_page_.tiled) return 12321;
        if (!pumpFor(app, [&] { return app.store_hub_.feed_loaded && pictures(app); })) return 12322;
        const auto& feed = app.store_hub_.feed;
        if (feed.free_now.size() != 3 || feed.free_soon.size() != 1 || feed.deals.size() != 4 || app.store_hub_.offers.size() != 8) return 12323;
        const auto first = app.plugin_page_.focus;
        if (first < 0 || app.plugin_page_.rows[first].title != L"Latarnia na Krańcu" || app.plugin_page_.rows[first].badge != tr(L"Za darmo") ||
            !app.plugin_page_.rows[first].badge_strong || app.plugin_page_.rows[first].subtitle.find(L"142,00 zł") == std::wstring::npos) return 12324;
        if (!app.plugin_page_.status.empty()) return 12358;
        if (std::none_of(app.plugin_page_.rows.begin(), app.plugin_page_.rows.end(), [](const auto& row) { return row.subtitle.find(L"GamerPower") != std::wstring::npos; })) return 12359;
        if (!capture(app, L"stores-promos.png")) return 12325;
        dispatched.clear();
        app.pluginPageAccept();
        if (dispatched != std::vector<std::wstring>{L"https://store.epicgames.com/pl/p/latarnia"}) return 12326;
        const auto deal = std::find_if(app.plugin_page_.rows.begin(), app.plugin_page_.rows.end(), [](const auto& row) { return row.title == L"Gwiezdny Kurier"; });
        if (deal == app.plugin_page_.rows.end() || deal->badge != L"-75%" || deal->subtitle.find(L"25,00 zł") == std::wstring::npos) return 12327;
        // A click on a tile is the same; LB returns to Launchery, which is as it was left.
        const auto view = app.renderer_.viewport();
        const plugin_ui::Layout layout{view.width, view.height};
        // Down the sections to it: the page scrolls it into view.
        for (int guard = 0; guard < 12 && app.plugin_page_.focus != static_cast<int>(deal - app.plugin_page_.rows.begin()); ++guard) app.changeCategory(1);
        if (app.plugin_page_.focus != static_cast<int>(deal - app.plugin_page_.rows.begin()) || app.plugin_page_.scroll <= 0) return 12340;
        const auto box = layout.tile(app.plugin_page_, static_cast<size_t>(deal - app.plugin_page_.rows.begin()));
        dispatched.clear();
        app.click((box.left + 20) * view.scale, (box.top + 20) * view.scale, false);
        if (dispatched != std::vector<std::wstring>{L"https://store.steampowered.com/app/10"}) return 12328;
        app.switchStoreTab(-1);
        if (app.storeTabIndex() != 0 || app.plugin_page_.plugin != store_hub::launchers_id || app.plugin_page_.rows[rowOf(app, "epic")].badge != tr(L"Zainstalowany")) return 12329;
        app.back();
        if (app.screen_ == ScreenMode::plugin_page) return 12330;

        // The desktop window: the same tabs in the store's sheet.
        app.desktop_mode_ = true; app.buildDesktopSidebar();
        app.openStores();
        if (!app.desktopStorePage() || !app.plugin_page_.tiled) return 12331;
        if (!pumpFor(app, [&] { return !app.store_hub_.detecting && pictures(app); }) || !capture(app, L"stores-launchers-desktop.png")) return 12332;
        app.switchStoreTab(1);
        if (!pumpFor(app, [&] { return pictures(app); }) || !capture(app, L"stores-promos-desktop.png")) return 12333;
        app.back(); app.desktop_mode_ = false;

        // The home screen's Launchery row: listed among the rows, hidden until turned on; then a tile per
        // installed launcher, which opens it (no game session, no editor).
        {
            const auto listed = [&] { const auto rows = app.homeRowEntries(); return std::any_of(rows.begin(), rows.end(), [](const auto& row) { return row.id == "launchers"; }); };
            if (app.settings_.show_launchers || loadAppSettings().show_launchers || !listed() || app.homeRowOnHome("launchers") || app.findCategory("launchers") ||
                app.homeRowPlace("launchers") != tr(L"Ukryty") || app.homeRowHasTab("launchers")) return 12390;
            app.setHomeRow("launchers", true, false); app.saveSettings();
            if (!app.settings_.show_launchers || !loadAppSettings().show_launchers || !app.homeRowOnHome("launchers")) return 12391;
            const auto* row = app.findCategory("launchers");
            if (!row || row->title != tr(L"Launchery") || row->games.size() != app.store_hub_.found.size() || row->games.empty() ||
                !launcherTile(row->games[0]) || row->games[0].name != L"Steam" || app.launchEditable(row->games[0])) return 12392;
            const auto index = static_cast<size_t>(row - app.categories_.data());
            app.settings_.tile_entrance = AppSettings::TileEntrance::none; // The capture shows the row at rest.
            app.showCategory(index);
            for (int i = 0; i < 40; ++i) { app.last_frame_ -= std::chrono::milliseconds(50); app.tick(); }
            if (app.cards_.empty() || !launcherTile(app.cards_[0].game) || !activePlatformName(app.cards_[0].game).empty()) return 12393;
            // Its picture is the mark of its Stores tile, fitted inside the tile like a platform's.
            // (Handed over here: the test's worker has no shipped marks to read.)
            if (app.requestArt(app.cards_[0].game, CoverMessage::Kind::hero) || !logoTile(app.cards_[0].game)) return 12403; // Its mark alone.
            for (size_t i = 0; i < app.cards_.size(); ++i) {
                auto message = std::make_unique<CoverMessage>();
                message->key = gameKey(app.cards_[i].game); message->generation = app.jobs_->generation.load();
                message->kind = CoverMessage::Kind::cover; message->bytes = pngFixture(96, 40, static_cast<uint32_t>(60 + 90 * i));
                app.onCover(std::move(message));
            }
            if (!app.renderer_.hasCover(0)) return 12404;
            for (int i = 0; i < 20; ++i) { app.last_frame_ -= std::chrono::milliseconds(50); app.tick(); app.paint(); Sleep(10); }
            if (!capture(app, L"home-launchers-row.png")) return 12394;
            // Its details are the launcher's card in the Stores.
            if (app.actionLabel(app.cards_[0].game) != tr(L"Otwórz")) return 12398;
            app.openDetails(app.cards_[0].game);
            if (app.screen_ != ScreenMode::plugin_page || !app.plugin_page_.dialog || app.store_hub_.choosing != "steam" || command(app, "open") < 0) return 12399;
            app.back(); app.back();
            if (app.screen_ != ScreenMode::shelves) return 12400;
            app.showCategory(index);
            if (app.cards_.empty() || !launcherTile(app.cards_[0].game)) return 12401;
            dispatched.clear();
            app.launchGame(app.cards_[0].game);
            if (dispatched.size() != 1 || !dispatched[0].ends_with(L"steam.exe") || app.pending_launch_ || !app.game_sessions_.empty()) return 12395;
            // Uninstalled launchers leave the row; turned off, the row is gone.
            if (!pumpFor(app, [&] { return !app.store_hub_.detecting; })) return 12402; // The look the Stores started.
            { std::lock_guard lock(world->mutex); world->installed.erase("epic"); }
            app.storeHubDetect();
            if (!pumpFor(app, [&] { return !app.store_hub_.detecting; }) || !app.findCategory("launchers") ||
                app.findCategory("launchers")->games.size() != app.store_hub_.found.size() || app.store_hub_.found.contains("epic")) return 12396;
            app.setHomeRow("launchers", false, false);
            if (app.settings_.show_launchers || app.findCategory("launchers")) return 12397;
            app.saveSettings(); app.showCategory(0);
        }
        // Settings → System → Funkcje turns both tabs off: the Stores are the plugins' alone again.
        app.openSettings(); app.settings_state_.category = static_cast<int>(settings_ui::Category::system); app.buildSettingsRows();
        const auto row = std::find_if(app.settings_rows_.begin(), app.settings_rows_.end(), [](const auto& r) { return r.id == settings_ui::Id::store_hub; });
        if (row == app.settings_rows_.end() || !row->checked) return 12334;
        app.settings_state_.options = true; app.settings_state_.selected[app.settings_state_.category] = static_cast<int>(row - app.settings_rows_.begin());
        app.settingsActivate(); app.saveSettings();
        if (app.settings_.store_hub || loadAppSettings().store_hub || app.storeTabCount() != 0) return 12335;
        app.back(); app.showCategory(0);
        app.openStores();
        if (app.storeTabsShown() || app.storeHubPage() || app.plugin_page_.title != tr(L"Sklepy")) return 12336;
        app.settings_.store_hub = true; app.saveSettings();
    }
    {
        // Linux: a question first. The native Flatpak, or the Windows build in a new or an existing prefix.
        { std::lock_guard lock(world->mutex); world->installed.clear(); world->log.clear(); world->winget = 0ul; }
        Launcher app;
        prepare(app, {false, false, true, true});
        if (!app.renderer_.initialize(window)) return 12350;
        app.openStores();
        if (!pumpFor(app, [&] { return app.store_hub_.machine_known && !app.store_hub_.detecting; })) return 12351;
        if (rowOf(app, "lutris") < 0 || rowOf(app, "steam") < 0 || rowOf(app, "gog") < 0) return 12352;
        app.plugin_page_.focus = rowOf(app, "steam");
        app.pluginPageAccept();
        if (!app.plugin_page_.dialog || app.store_hub_.choosing != "steam" || !app.store_hub_.jobs.empty()) return 12363;
        if (command(app, "native") < 0 || command(app, "prefix-new") < 0 || command(app, "prefix-0") < 0 || command(app, "prefix-1") < 0 || command(app, "prefix-2") >= 0 ||
            command(app, "page") < 0 || command(app, "install") >= 0) return 12364;
        if (!capture(app, L"stores-launcher-linux.png")) return 12365;
        // Back closes the card onto the tab; nothing was installed.
        app.back();
        if (app.plugin_page_.dialog || !app.storeHubPage() || !app.store_hub_.jobs.empty()) return 12366;
        // Native: Flathub for this user.
        if (!pick(app, "steam", "native") || app.plugin_page_.dialog || !app.store_hub_.jobs.contains("steam")) return 12367;
        if (!pumpFor(app, [&] { return app.store_hub_.jobs.empty() && !app.store_hub_.detecting; }) || !logged(L"flatpak install com.valvesoftware.Steam")) return 12368;
        // GOG Galaxy has no native build: only prefixes are offered; a new one is made and remembered.
        app.plugin_page_.focus = rowOf(app, "gog"); app.pluginPageAccept();
        if (!app.plugin_page_.dialog || command(app, "native") >= 0 || command(app, "prefix-new") < 0) return 12369;
        if (!pick(app, "gog", "prefix-new")) return 12370;
        if (!pumpFor(app, [&] { return app.store_hub_.jobs.empty() && !app.store_hub_.detecting && app.store_hub_.found.contains("gog"); })) return 12371;
        if (!logged(L"wine GOG Galaxy C:\\prefixes\\made") || launchers::winePrefix(userConfigDirectory(), "gog") != L"C:\\prefixes\\made" ||
            app.store_hub_.found["gog"].prefix != L"C:\\prefixes\\made") return 12372;
        // An existing prefix by its row.
        if (!pick(app, "battlenet", "prefix-1")) return 12373;
        if (!pumpFor(app, [&] { return app.store_hub_.jobs.empty() && !app.store_hub_.detecting; }) || !logged(L"wine Battle.net C:\\prefixes\\battle-net")) return 12374;
        // A Windows build opens through its prefix; its card offers no removal (the prefix is the user's).
        dispatched.clear();
        app.plugin_page_.focus = rowOf(app, "gog"); app.pluginPageAccept();
        if (!app.plugin_page_.dialog || command(app, "uninstall") >= 0 || command(app, "apps") >= 0 || !pick(app, "gog", "open")) return 12375;
        if (dispatched.size() != 1 || !dispatched[0].ends_with(L"GalaxyClient.exe")) return 12376;
    }
    return 0;
}

// By hand (launcher_ui_tests --store-hub-live): the real machine, the real stores and the real pictures,
// read only. Nothing is installed or started: it looks, downloads one installer to check its signature
// and captures both tabs.
int LauncherUiTest::storeHubLive(HWND window) {
    const auto machine = launcher_install::machine();
    std::wcout << L"winget: " << machine.winget << L'\n';
    const auto probe = launchers::systemProbe(userConfigDirectory());
    for (const auto& entry : launchers::catalog())
        if (const auto found = launchers::detect(entry, probe)) std::wcout << L"installed: " << entry.name << L" -> " << found->program.wstring() << L'\n';
    const auto backend = launcher_install::systemBackend();
    if (machine.winget) {
        const auto code = backend.run(backend.winget, {L"--version"}, {});
        std::wcout << L"winget --version: " << (code ? std::to_wstring(*code) : std::wstring(L"did not start")) << L'\n';
        if (!code || *code != 0) return 12380;
    }
    // The vendor's installer: downloaded and its signature checked; never opened.
    const auto& steam = *launchers::find("steam");
    const auto file = launcher_install::installerFile(userConfigDirectory(), steam);
    if (!backend.download(std::wstring(steam.installer), file, {})) return 12381;
    std::error_code ec;
    const auto size = std::filesystem::file_size(file, ec);
    const bool trusted = backend.trusted(file);
    std::wcout << L"SteamSetup.exe: " << size << L" bytes, signature " << (trusted ? L"valid" : L"INVALID") << L'\n';
    // The same check refuses a file that is not signed.
    const auto plain = file.parent_path() / L"unsigned.exe";
    { std::ofstream out(plain, std::ios::binary); out << "MZ not an installer"; }
    const bool refused = !backend.trusted(plain);
    std::filesystem::remove(file, ec); std::filesystem::remove(plain, ec);
    if (!trusted || !refused || size < 100000) return 12382;

    Launcher app;
    app.hwnd_ = window; app.execute_ = fakeExecute;
    app.browse_ = [](const std::wstring& url) { dispatched.push_back(url); };
    app.settings_.launch_animation = false; app.settings_.glance = app.settings_.companion = false;
    if (!app.renderer_.initialize(window)) return 12383;
    const auto pumpFor = [&](const std::function<bool()>& done) {
        for (int i = 0; i < 6000; ++i) {
            MSG message{};
            while (PeekMessageW(&message, window, remote_message, remote_message, PM_REMOVE))
                app.onRemote(std::unique_ptr<RemoteMessage>(reinterpret_cast<RemoteMessage*>(message.lParam)));
            if (done()) return true;
            Sleep(5);
        }
        return false;
    };
    const auto pictures = [&] {
        return app.store_hub_.image_pending.empty() && std::all_of(app.plugin_page_.rows.begin(), app.plugin_page_.rows.end(), [&](const auto& row) {
            return !row.tile || row.image.empty() || app.store_hub_.images.contains(row.image) || app.store_hub_.image_failed.contains(row.image); });
    };
    const auto capture = [&](const wchar_t* name) {
        wchar_t folder[4096]{};
        if (!GetEnvironmentVariableW(L"LAUNCHME_UI_CAPTURE_DIR", folder, 4096)) { app.paint(); return true; }
        for (int i = 0; i < 20; ++i) { app.paint(); Sleep(20); } // Pictures decode off the UI thread.
        return app.renderer_.captureForTest(std::filesystem::path(folder) / name, [&] { app.paint(); });
    };
    app.openStores();
    if (!pumpFor([&] { return app.store_hub_.machine_known && !app.store_hub_.detecting && pictures(); })) return 12384;
    std::wcout << L"launcher pictures failed: " << app.store_hub_.image_failed.size() << L'\n';
    if (!capture(L"stores-launchers-live.png")) return 12385;
    app.switchStoreTab(1);
    if (!pumpFor([&] { return app.store_hub_.feed_loaded && pictures(); })) return 12386;
    const auto& feed = app.store_hub_.feed;
    std::wcout << L"free now " << feed.free_now.size() << L", free soon " << feed.free_soon.size() << L", deals " << feed.deals.size()
               << L", pictures failed " << app.store_hub_.image_failed.size() << (feed.unavailable ? L" (unavailable)" : L"") << L'\n';
    for (const auto& offer : feed.free_now) std::wcout << L"  free: " << offer.title << L" [" << offer.where << L"] " << offer.url << L'\n';
    if (!capture(L"stores-promos-live.png")) return 12387;
    app.changeCategory(1); app.changeCategory(1); app.changeCategory(1);
    if (!capture(L"stores-promos-live-2.png")) return 12388;
    return feed.unavailable ? 12389 : 0;
}
