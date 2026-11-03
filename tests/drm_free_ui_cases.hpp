// Included after LauncherUiTest: DRM-free folders. Settings → Platformy PC lists them first;
// their page adds folders, scans and corrects games. The folder/file dialogs and ShellExecute
// are fakes, and the scan worker's messages are delivered by hand (no folder walk, no RAWG).
namespace {
std::vector<GameAction> drm_launches;
INT_PTR drmExecute(HWND, const GameAction& action) { drm_launches.push_back(action); return 33; }
} // namespace

int LauncherUiTest::drmFree(HWND window) {
    using namespace settings_ui;
    Launcher app; app.hwnd_ = window; app.execute_ = drmExecute; drm_launches.clear();
    app.boundary_feedback_ = [] {};
    if (!app.renderer_.initialize(window)) return 2100;
    const auto root = userConfigDirectory() / L"drm-fixture" / L"Gry";
    std::filesystem::create_directories(root);
    app.pick_games_folder_ = [&](HWND) { return root; };
    const auto capture = [&](const wchar_t* name) {
        wchar_t folder[4096]{};
        if (!GetEnvironmentVariableW(L"LAUNCHME_UI_CAPTURE_DIR", folder, 4096)) return true;
        app.settings_motion_.valid = false;
        return app.renderer_.captureForTest(std::filesystem::path(folder) / name, [&] { app.paint(); });
    };
    const auto select = [&](Id id, const std::string& key = {}) {
        const auto row = std::find_if(app.settings_rows_.begin(), app.settings_rows_.end(), [&](const Row& r) { return r.id == id && (key.empty() || r.key == key); });
        if (row == app.settings_rows_.end()) return false;
        app.settings_state_.selected[app.settings_state_.category] = static_cast<int>(row - app.settings_rows_.begin());
        app.settings_state_.options = true; app.settings_state_.editing = false; app.ensureSettingVisible(); return true;
    };
    const auto deliver = [&](DrmFreeMessage::Kind kind, drm_free::Catalog catalog = {}, drm_free::Progress progress = {}) {
        auto message = std::make_unique<DrmFreeMessage>();
        message->generation = app.drm_generation_; message->kind = kind; message->catalog = std::move(catalog); message->progress = progress;
        app.onDrmFree(std::move(message));
    };
    const auto pcs = [&]() -> const Launcher::Category* {
        const auto found = std::find_if(app.categories_.begin(), app.categories_.end(), [](const auto& item) { return item.id == "pc-platforms"; });
        return found == app.categories_.end() ? nullptr : &*found;
    };
    auto steam = std::make_unique<LibraryMessage>(); steam->account = 1; steam->games = {Game{620, L"Portal 2"}};
    app.onLibrary(std::move(steam));

    // The overview starts with the DRM-free folders, then Emulacja's tile, Steam and the stores by name.
    app.openSettings(); app.settings_state_.selectCategory(static_cast<int>(Category::library));
    app.pc_scanning_ = false; app.buildSettingsRows();
    const auto rowOf = [&](Id id) -> const Row* {
        const auto row = std::find_if(app.settings_rows_.begin(), app.settings_rows_.end(), [&](const Row& r) { return r.id == id; });
        return row == app.settings_rows_.end() ? nullptr : &*row;
    };
    if (app.settings_rows_.size() <= pc_sources.size() + 3 || app.settings_rows_[0].id != Id::pc_platform ||
        app.settings_rows_[0].source != GameSource::drmfree || app.settings_rows_[0].title != L"Katalog DRM-free" || app.settings_rows_[0].checked ||
        !optionsTile(app.settings_rows_[1]) || app.settings_rows_[2].source != GameSource::steam || app.settings_rows_[3].source != app.platformOrder()[2]) return 2101;
    if (!capture(L"drm-free-overview.png")) return 2102;
    const auto view = app.renderer_.viewport();
    const auto layout = Layout::of(view.width, view.height, app.settings_state_, app.settings_rows_);
    const auto cog = layout.pluginCog(0, 0);
    app.settingsClick((cog.left + cog.right) * 0.5f, (cog.top + cog.bottom) * 0.5f);
    // Its page: Dodaj grę, the scan and its background switch, the folders' tile and the games added by hand.
    if (app.settings_state_.platform() != GameSource::drmfree || app.settings_rows_.size() != 5 || app.settings_rows_[0].id != Id::custom_add ||
        app.settings_rows_[1].id != Id::drm_scan || app.settings_rows_[2].id != Id::drm_background || !app.settings_rows_[2].checked ||
        app.settings_rows_[3].id != Id::drm_roots || app.settings_rows_[4].id != Id::custom_games) return 2103;
    for (const auto& row : app.settings_rows_) if (row.source != GameSource::drmfree) return 2104;

    // "Katalogi" → "Dodaj katalog": the folder is saved, the source turns on and a full scan starts.
    if (!select(Id::drm_roots)) return 2105;
    app.settingsActivate();
    if (!app.settings_state_.at(Page::drm_roots) || app.settings_rows_.size() != 1 || !select(Id::drm_add_root)) return 2105;
    app.settingsActivate();
    if (app.drm_catalog_.roots != std::vector<std::filesystem::path>{root} || !app.settings_.enabled_pc_sources.contains("drmfree") ||
        !app.drm_scanning_ || app.settings_rows_.size() != 2 || app.settings_rows_[1].id != Id::drm_root || app.settings_rows_[1].title != root.native()) return 2106;
    if (!capture(L"drm-free-folders.png")) return 2106;
    app.back();
    if (app.settings_state_.at(Page::drm_roots) || !rowOf(Id::drm_scan) || rowOf(Id::drm_scan)->value != L"Zatrzymaj" ||
        rowOf(Id::drm_roots)->value != L"1") return 2106;    // The same folder, or one inside it, is refused.
    app.pick_games_folder_ = [&](HWND) { return root / L"Indie"; };
    app.drmAddRoot();
    if (app.drm_catalog_.roots.size() != 1 || app.notifications_.empty()) return 2107;
    app.notifications_.clear();
    deliver(DrmFreeMessage::Kind::progress, {}, {3, 7});
    if (rowOf(Id::drm_scan)->description.find(L"3 / 7") == std::wstring::npos) return 2108;

    // The scanned catalog: two games, saved and on the shelves.
    const auto entry = [&](const wchar_t* folder_name, const wchar_t* exe, const wchar_t* name) {
        drm_free::Entry value; value.folder = root / folder_name; value.id = drm_free::folderId(value.folder);
        value.executable = value.folder / exe; value.title = value.name = name; value.found = 1;
        return value;
    };
    auto scanned = app.drm_catalog_;
    scanned.games = {entry(L"Celeste", L"Celeste.exe", L"Celeste"), entry(L"Hollow_Knight", L"hollow_knight.exe", L"Hollow Knight")};
    scanned.scanned = 1790000000;
    const auto stale_generation = app.drm_generation_;
    deliver(DrmFreeMessage::Kind::scanned, scanned);
    if (app.drm_catalog_.games.size() != 2 || app.drm_library_.size() != 2 || app.notifications_.empty() ||
        drm_free::load(userConfigDirectory() / L"drm-free.json").catalog != app.drm_catalog_) return 2109;
    deliver(DrmFreeMessage::Kind::finished);
    if (app.drm_scanning_ || rowOf(Id::drm_scan)->value != L"Skanuj teraz") return 2110;
    // A message of an earlier scan never replaces the catalog of a newer one.
    app.startDrmScan(true);
    auto late = std::make_unique<DrmFreeMessage>(); late->generation = stale_generation; late->kind = DrmFreeMessage::Kind::scanned;
    app.onDrmFree(std::move(late));
    if (app.drm_catalog_.games.size() != 2 || !app.drm_scanning_) return 2111;
    deliver(DrmFreeMessage::Kind::finished);
    const auto celeste_id = app.drm_catalog_.games[0].id, hollow_id = app.drm_catalog_.games[1].id;
    // The page shows one "Znalezione gry" row with the total, not the games themselves.
    if (app.settings_rows_.size() != 6 || app.settings_rows_[5].id != Id::drm_games || app.settings_rows_[5].control != Control::count ||
        app.settings_rows_[5].value != L"2" || app.settings_rows_[5].title != L"Znalezione gry") return 2112;
    if (!capture(L"drm-free-page.png")) return 2113;
    const auto* shelf = pcs();
    if (!shelf || std::none_of(shelf->games.begin(), shelf->games.end(), [](const Game& tile) { return tile.source == GameSource::drmfree; }) ||
        std::count_if(app.public_library_.begin(), app.public_library_.end(), [](const Game& game) { return game.source == GameSource::drmfree; }) != 2) return 2114;
    // Entering it: thin rows, one per game.
    if (!select(Id::drm_games)) return 2132;
    app.settingsActivate();
    const auto list_layout = Layout::of(view.width, view.height, app.settings_state_);
    if (!app.settings_state_.at(settings_ui::Page::drm_games) || app.settings_rows_.size() != 2 || app.settings_rows_[0].key != celeste_id ||
        app.settings_rows_[0].description != L"Celeste.exe" || app.settings_rows_[1].title != L"Hollow Knight" || !list_layout.compact ||
        list_layout.rowHeight(0) >= Layout::row_height || app.settings_state_.row() != 0) return 2133;
    if (!capture(L"drm-free-games.png")) return 2134;
    {
        // A long path and its notes stay on one line of the thin row (ellipsis), in both faces.
        const auto saved = app.settings_rows_[1].description;
        app.settings_rows_[1].description = L"Games\\Team Cherry\\Hollow Knight Voidheart Edition\\Binaries\\Win64\\Shipping\\hollow_knight_voidheart_edition.exe"
                                            L" · wybrany plik · nie rozpoznano w RAWG · Brak pliku";
        if (!capture(L"drm-free-games-long.png")) return 2190;
        app.settings_rows_[1].description = saved;
    }
    // A click on the second thin row hits that row (geometry shared with drawing).
    const auto second = list_layout.row(1, app.settings_state_.offset());
    if (list_layout.hitRow(second.left + 30, second.top + 10, 2, app.settings_state_.offset()) != 1) return 2135;

    // Game options: "Dopasuj w RAWG…" opens identification over Settings.
    if (!select(Id::drm_game, celeste_id)) return 2115;
    app.settingsActivate();
    if (!app.settings_state_.editing || app.settings_rows_[app.settings_state_.row()].choices.size() != 4 ||
        app.settings_rows_[app.settings_state_.row()].choices[0].label != L"Dopasuj w RAWG…") return 2116;
    if (!capture(L"drm-free-game-options.png")) return 2117;
    app.settings_state_.choice = 0; app.settingsActivate();
    if (!app.identifyOpen() || gameKey(app.identify_state_.game) != "drmfree:" + celeste_id || app.identify_state_.query != L"Celeste" ||
        !app.settingsOpen()) return 2136;
    if (!capture(L"drm-free-identify.png")) return 2137;
    app.back();
    if (app.identifyOpen() || !app.settingsOpen() || !app.settings_state_.at(settings_ui::Page::drm_games)) return 2138;
    // "Zmień plik gry…" opens the picker in the game's folder.
    const auto other = root / L"Celeste" / L"Celeste-OpenGL.exe";
    std::filesystem::path initial;
    app.pick_path_ = [&](HWND, bool folder, const std::filesystem::path& start) { initial = start; return folder ? std::filesystem::path{} : other; };
    if (!select(Id::drm_game, celeste_id)) return 2139;
    app.settingsActivate(); app.settings_state_.choice = 1; app.settingsActivate();
    const auto& celeste = app.drm_catalog_.games[0];
    if (app.settings_state_.editing || initial != root / L"Celeste" || celeste.executable != other || !celeste.manual_executable ||
        app.settings_rows_[app.settings_state_.row()].key != celeste_id || app.settings_rows_[app.settings_state_.row()].choices.size() != 5) return 2118;
    // "Ignoruj" takes Hollow Knight out of the library; it moves to the end of the list.
    if (!select(Id::drm_game, hollow_id)) return 2119;
    app.settingsActivate(); app.settings_state_.choice = 2; app.settingsActivate();
    if (!app.drm_catalog_.games[1].ignored || app.drm_library_.size() != 1 || app.settings_rows_.back().description != L"Ignorowana") return 2120;
    // A scan made on an older snapshot keeps both corrections.
    app.startDrmScan(true);
    deliver(DrmFreeMessage::Kind::scanned, scanned);
    deliver(DrmFreeMessage::Kind::finished);
    if (!app.drm_catalog_.games[1].ignored || app.drm_catalog_.games[0].executable != other || app.drm_library_.size() != 1) return 2121;
    // Back returns to the page with "Znalezione gry" focused; its total counts playable games.
    app.back();
    if (app.settings_state_.at(settings_ui::Page::drm_games) || app.settings_state_.platform() != GameSource::drmfree ||
        app.settings_rows_[app.settings_state_.row()].id != Id::drm_games || app.settings_rows_[app.settings_state_.row()].value != L"1" ||
        app.settings_rows_[app.settings_state_.row()].description.find(L"Ignorowane: 1") == std::wstring::npos) return 2140;

    // The DRM-free tile opens its library; launching runs the chosen file from its folder.
    for (int i = 0; i < 4 && app.settingsOpen(); ++i) app.back();
    if (app.settingsOpen()) return 2122;
    app.openPcLibrary(GameSource::drmfree);
    if (app.screen_ != ScreenMode::library || app.cards_.size() != 1 || app.cards_[0].game.name != L"Celeste") return 2123;
    app.launch(); app.finishLaunch();
    if (drm_launches.size() != 1 || drm_launches[0].target != other.native() || drm_launches[0].working_directory != other.parent_path()) return 2124;
    // "Zidentyfikuj grę" renames it for good: later scans keep the user's choice.
    rawg::Metadata identified; identified.id = 9; identified.name = L"Celeste (2018)";
    app.drmIdentified(app.cards_[0].game, identified);
    if (app.drm_catalog_.games[0].name != L"Celeste (2018)" || !app.drm_catalog_.games[0].manual_identity || app.cards_[0].game.name != L"Celeste (2018)") return 2125;
    app.startDrmScan(false);
    deliver(DrmFreeMessage::Kind::scanned, scanned);
    deliver(DrmFreeMessage::Kind::finished);
    if (app.drm_catalog_.games[0].name != L"Celeste (2018)" || app.drm_catalog_.games[0].rawg_id != 9) return 2126;

    // "Zmień grafikę → Z dysku…": type from the list, then a PNG/JPEG file becomes that artwork.
    const auto image = root / L"Celeste" / L"cover.png";
    { const auto bytes = pngFixture(90, 300, 200, true); std::ofstream(image, std::ios::binary).write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())); }
    std::vector<int> picks;
    app.pick_image_ = [&](HWND) { picks.push_back(1); return image; };
    const auto celeste_game = app.cards_[0].game;
    app.openArtworkBrowser(celeste_game);
    auto& art = app.artwork_state_;
    art.category = static_cast<int>(artwork_ui::kinds.size()) - 1; // "Kafel poziomy", the last type.
    app.artworkMove(0, 1);
    if (!art.local_focus || art.local_open) return 2141;
    app.artworkAccept();
    if (!art.local_open || art.local_kind != art.category || !picks.empty()) return 2142;
    app.launch_animation_started_.reset();
    if (!capture(L"artwork-from-disk.png")) return 2143;
    app.artworkMove(0, -1); app.artworkMove(-1, 0); app.artworkMove(-1, 0); // The board is 3 x 2: up a row, two tiles left: "Okładka".
    app.artworkAccept();                                  // Chosen; focus moves to "Wskaż plik…".
    if (art.local_kind != 0 || art.local_row != artwork_ui::State::local_pick_row || !art.local_open) return 2144;
    app.back();                                           // Back closes only the panel.
    if (art.local_open || !app.artworkOpen()) return 2145;
    app.artworkAccept(); app.artworkMove(0, -10); app.artworkMove(-1, 0); app.artworkMove(-1, 0); app.artworkAccept(); app.artworkAccept();
    if (picks.size() != 1 || art.local_open || !art.applying || art.category != 0) return 2146;
    {
        std::lock_guard lock(app.artwork_jobs_->mutex);
        const auto& queued = app.artwork_jobs_->jobs.front();
        if (queued.type != ArtworkBrowserMessage::Type::local || queued.kind != ArtworkKind::cover || queued.query != image.native()) return 2147;
    }
    // The worker's part: the file stored as a variant of this game, then its answer.
    const uint32_t local_id = 0x80000001u;
    if (!cacheArtworkVariant(celeste_game, ArtworkKind::cover, local_id, pngFixture(90, 300, 200, true), coverCacheDirectory())) return 2148;
    auto applied = std::make_unique<ArtworkBrowserMessage>();
    applied->type = ArtworkBrowserMessage::Type::local; applied->generation = app.artwork_jobs_->generation.load();
    applied->kind = ArtworkKind::cover; applied->asset_id = local_id; applied->bytes = pngFixture(90, 300, 200, true);
    app.onArtworkBrowser(std::move(applied));
    const auto* bound = app.art_.find(gameKey(celeste_game));
    if (art.applying || art.status != L"Zastosowano grafikę z dysku." || !bound || !bound->cover ||
        selectedArtworkId(celeste_game, ArtworkKind::cover, coverCacheDirectory()) != local_id) return 2149;
    // The added file is a variant of the game's own "Lokalne" source: shown there, set (a filled dot).
    if (!hasLocalArtwork(celeste_game, coverCacheDirectory()) || localArtworkIds(celeste_game, ArtworkKind::cover, coverCacheDirectory()) != std::vector<uint32_t>{local_id}) return 2290;
    if (art.provider != ArtworkProvider::local || !art.hasLocal() || !art.custom[0] || art.custom[1]) return 2291;
    if (std::find(art.choices.begin(), art.choices.end(), ArtworkProvider::local) == art.choices.end()) return 2292;
    app.launch_animation_started_.reset();
    if (!capture(L"artwork-local-source.png")) return 2293;

    // Back sits above the types (top left): Up from the first type reaches it, Down returns; Right
    // enters the search field, whose selected text is replaced by typing.
    art.local_focus = false; art.gallery = false; art.category = 0;
    app.artworkMove(0, -1);
    if (!art.back_focus || app.editingArtworkSearch()) return 2150;
    app.artworkMove(0, 1);
    if (art.back_focus || art.category != 0) return 2150;
    app.artworkMove(1, 0);
    if (!app.editingArtworkSearch() || !art.query_selected) return 2150;
    for (const auto ch : std::wstring(L"Revenant")) app.onCharacter(ch);
    if (art.query != L"Revenant" || !art.results_mode || art.query_selected) return 2151;
    // Down without searching leaves the field and brings the gallery back.
    app.artworkMove(0, 1);
    if (app.editingArtworkSearch() || art.results_mode || !art.loading || art.query != L"Revenant") return 2152;
    // "Wyczyść" (also Delete) empties the field and keeps the focus there.
    if (art.gallery) app.artworkMove(0, -1); else app.artworkMove(1, 0); // The gallery's top row or the rail: the field.
    if (!app.editingArtworkSearch()) return 2153;
    app.clearArtworkQuery();
    if (!art.query.empty() || !app.editingArtworkSearch()) return 2154;
    // Same-titled games: the year known for this game (RAWG) decides the order; rows show years.
    app.rawg_metadata_[gameKey(celeste_game)].details.release_date = L"1999-10-26";
    for (const auto ch : std::wstring(L"Revenant")) app.onCharacter(ch);
    app.searchArtwork();
    auto found = std::make_unique<ArtworkBrowserMessage>();
    found->type = ArtworkBrowserMessage::Type::search; found->generation = app.artwork_jobs_->generation.load();
    found->search.games = {{{5389392, false}, L"Revenant", 2024}, {{5100000, false}, L"Revenant Kingdom", 2020},
                           {{5247473, false}, L"Revenant", 1999}, {{5300000, false}, L"Revenant", 2023}};
    app.onArtworkBrowser(std::move(found));
    if (art.games.size() != 4 || art.games[0].source.id != 5247473 || art.games[1].source.id != 5300000 ||
        art.games[3].source.id != 5100000 || !art.gallery || art.selected != 0) return 2155;
    if (!capture(L"artwork-search-years.png")) return 2156;
    app.closeArtworkBrowser();
    // Emulated shortcuts: "Źródło" right of the search (SteamGridDB, ScreenScraper, ES-DE), reached
    // with the pad from any type: Right to the field, Right again; A opens its list, A picks.
    {
        Game emulated; emulated.kind = GameKind::nonsteam; emulated.app_id = 0xF1000001u;
        emulated.shortcut_id = (uint64_t{0xF1000001u} << 32) | 0x02000000ULL;
        emulated.name = L"Desert Strike"; emulated.platform = L"Amiga";
        app.openArtworkBrowser(emulated);
        if (!art.emulated() || art.provider != ArtworkProvider::steamgriddb) return 2157;
        art.category = artwork_ui::category(ArtworkKind::landscape); // Disc artwork has one fixed provider.
        app.artworkMove(1, 0); // The rail -> the search field…
        if (!art.search_focus || art.provider_focus) return 2158;
        app.artworkMove(1, 0); // …-> "Źródło".
        if (!art.provider_focus || art.search_focus || art.railFocus()) return 2158;
        app.artworkAccept(); // Its list, on the one in use.
        if (!art.provider_open || art.provider_row != 0) return 2158;
        app.artworkMove(0, 1);
        app.artworkAccept(); // ScreenScraper.
        {
            std::lock_guard lock(app.artwork_jobs_->mutex);
            if (app.artwork_jobs_->jobs.empty() || app.artwork_jobs_->jobs.front().type != ArtworkBrowserMessage::Type::list ||
                app.artwork_jobs_->jobs.front().provider != ArtworkProvider::screenscraper) return 2159;
        }
        auto listed = std::make_unique<ArtworkBrowserMessage>();
        listed->type = ArtworkBrowserMessage::Type::list; listed->generation = app.artwork_jobs_->generation.load();
        listed->page.provider = ArtworkProvider::screenscraper;
        listed->page.assets = {{0x40000001u, L"https://neoclone.screenscraper.fr/api2/mediaJeu.php?media=fanart",
                                L"https://neoclone.screenscraper.fr/api2/mediaJeu.php?media=fanart", L"ScreenScraper · fanart"}};
        app.onArtworkBrowser(std::move(listed));
        if (art.provider != ArtworkProvider::screenscraper || art.assets.size() != 1 || !art.provider_focus || art.provider_open) return 2160;
        const auto loading = app.launch_animation_started_;
        app.launch_animation_started_.reset(); // The earlier launch animation would cover the page.
        if (!capture(L"artwork-source-screenscraper.png")) return 2161;
        app.artworkAccept(); app.artworkMove(0, 1);
        if (!art.provider_open || art.provider_row != 2 || !capture(L"artwork-source-list.png")) return 2161;
        app.launch_animation_started_ = loading;
        app.artworkAccept();
        if (art.provider != ArtworkProvider::esde || art.provider_open) return 2162;
        app.artworkAccept(); app.back(); // Back closes the list, nothing changes.
        if (art.provider_open || !art.provider_focus || art.provider != ArtworkProvider::esde) return 2163;
        app.artworkAccept(); app.artworkMove(0, -1); app.artworkMove(0, -1); app.artworkAccept();
        if (art.provider != ArtworkProvider::steamgriddb) return 2163;
        app.artworkMove(-1, 0); // Left: the search field again.
        if (!art.search_focus || art.provider_focus) return 2163;
        app.closeArtworkBrowser();
        // Other games have no "Źródło": Down from the last type reaches "Z dysku…" and stops there; Back
        // (the small circle at the top left) is above the first type, and Enter presses it.
        app.openArtworkBrowser(celeste_game);
        art.category = static_cast<int>(artwork_ui::kinds.size()) - 1;
        app.artworkMove(0, 1);
        if (art.emulated() || art.provider_focus || !art.local_focus) return 2164;
        app.artworkMove(0, 1); app.artworkMove(0, 1);
        if (art.back_focus || !art.local_focus) return 2177;
        app.artworkMove(0, -1); art.category = 0; app.artworkMove(0, -1);
        if (!art.back_focus || art.local_focus || art.crop_focus) return 2177;
        app.artworkAccept();
        if (app.artworkOpen()) return 2178;
        // "Kadruj" right of "Szukaj", for the cover and the background only: Left returns to the field,
        // Enter opens the crop editor (on Tło: the background's).
        app.openArtworkBrowser(celeste_game);
        art.category = artwork_ui::category(ArtworkKind::logo);
        app.artworkMove(1, 0); app.artworkMove(1, 0); // The field, then past it (nothing to crop on a logo): "Źródło" when it has sources.
        if (art.crop_focus || (!art.search_focus && !art.provider_focus)) return 2179;
        app.closeArtworkBrowser(); app.openArtworkBrowser(celeste_game);
        art.category = artwork_ui::category(ArtworkKind::hero);
        app.artworkMove(1, 0); app.artworkMove(1, 0);
        if (!art.crop_focus || art.search_focus || art.railFocus()) return 2180;
        app.artworkMove(-1, 0);
        if (!art.search_focus || art.crop_focus) return 2180;
        app.artworkMove(1, 0); app.artworkAccept();
        if (!app.cropOpen() || !app.crop_state_.background) return 2181;
        app.closeCropEditor();
        if (!app.artworkOpen()) return 2181;
        app.closeArtworkBrowser();
        // Inspect both presentations and route actual crop/back hit targets. No artwork service runs.
        app.launch_animation_started_.reset(); app.notifications_.clear();
        app.openArtworkBrowser(emulated);
        art.category = artwork_ui::category(ArtworkKind::cover);
        app.artworkMove(1, 0); app.artworkMove(1, 0);
        if (!art.crop_focus || art.search_focus) return 2182;
        if (!capture(L"artwork-square-crop-tv.png")) return 2183;
        app.artworkMove(1, 0);
        if (!art.provider_focus || art.crop_focus) return 2184;
        app.artworkMove(-1, 0);
        if (!art.crop_focus || art.provider_focus) return 2185;
        app.artworkMove(-1, 0);
        if (!art.search_focus || art.crop_focus) return 2186;
        app.artworkMove(-1, 0);
        art.category = artwork_ui::category(ArtworkKind::hero);
        app.artworkMove(1, 0); app.artworkMove(1, 0);
        if (!capture(L"artwork-background-crop-tv.png")) return 2187;
        const auto canvas = app.renderer_.viewport();
        const artwork_ui::Layout browser{canvas.width, canvas.height, art.kind(), art.providerChoice()};
        if (browser.searchButton().right >= browser.cropButton().left || browser.cropButton().right >= browser.providerButton().left) return 2188;
        const auto crop = browser.cropButton();
        if (!app.hoverButton((crop.left + crop.right) * 0.5f * canvas.scale, (crop.top + crop.bottom) * 0.5f * canvas.scale)) return 2189;
        app.artworkClick((crop.left + crop.right) * 0.5f, (crop.top + crop.bottom) * 0.5f, false);
        if (!app.cropOpen() || !app.crop_state_.background) return 2190;
        app.closeCropEditor();
        art.crop_focus = false; art.back_focus = true;
        app.artwork_glide_.valid = false;
        if (!capture(L"artwork-back-circle-tv.png")) return 2191;
        const auto back = browser.backButton();
        if (!app.hoverButton((back.left + back.right) * 0.5f * canvas.scale, (back.top + back.bottom) * 0.5f * canvas.scale)) return 2192;
        app.artworkClick((back.left + back.right) * 0.5f, (back.top + back.bottom) * 0.5f, false);
        if (app.artworkOpen()) return 2193;
        app.desktop_mode_ = true;
        app.openArtworkBrowser(celeste_game);
        art.category = artwork_ui::category(ArtworkKind::cover);
        if (!capture(L"artwork-square-crop-desktop.png")) return 2194;
        app.openLocalArtwork();   // The "Z dysku" board on the desktop: types as pictures, a dot for each one set.
        if (!art.local_open || !capture(L"artwork-from-disk-desktop.png")) return 2294;
        app.back();
        if (art.local_open) return 2295;
        const auto crop_hotspot = [&] {
            app.paint();
            const auto& hot = app.renderer_.desktopHotspots();
            return std::any_of(hot.begin(), hot.end(), [](const auto& item) { return item.hit.kind == desktop_ui::SheetHit::Kind::toggle; });
        };
        if (!crop_hotspot()) return 2195;
        art.category = artwork_ui::category(ArtworkKind::hero);
        if (!capture(L"artwork-background-crop-desktop.png") || !crop_hotspot()) return 2196;
        app.desktopPageActivate({desktop_ui::SheetHit::Kind::toggle}, false);
        if (!app.cropOpen() || !app.crop_state_.background) return 2197;
        app.closeCropEditor();
        art.category = artwork_ui::category(ArtworkKind::logo);
        if (!capture(L"artwork-logo-no-crop-desktop.png") || crop_hotspot()) return 2198;
        app.closeArtworkBrowser(); app.desktop_mode_ = false;
    }

    // Removing the folder takes two presses and empties the library.
    app.openSettings(); app.openPlatformSettings(GameSource::drmfree);
    if (!select(Id::drm_roots)) return 2127;
    app.settingsActivate();
    if (!select(Id::drm_root)) return 2127;
    app.settingsActivate();
    if (!app.drm_catalog_.roots.size() || app.settings_rows_[app.settings_state_.row()].value != L"Potwierdź") return 2128;
    app.settingsActivate();
    if (!app.drm_catalog_.roots.empty() || !app.drm_catalog_.games.empty() || !app.drm_library_.empty() || app.settings_rows_.size() != 1) return 2129;
    const auto* emptied = pcs();
    if (emptied && std::any_of(emptied->games.begin(), emptied->games.end(), [](const Game& tile) { return tile.source == GameSource::drmfree; })) return 2130;
    app.back(); app.back();
    if (app.settings_state_.platform() || app.settings_state_.row() != 0) return 2131; // Its row, the first.
    // Media i dane → "ScreenScraper" opens its own page of API settings; Back returns to that row.
    app.settings_state_.selectCategory(static_cast<int>(settings_ui::Category::media)); app.buildSettingsRows();
    // ScreenScraper is one level further, in "Klucze usług" (the services first on the page).
    if (!select(Id::game_videos)) return 2165;
    {
        const auto keys = std::find_if(app.settings_rows_.begin(), app.settings_rows_.end(), [](const auto& row) { return row.page == settings_ui::Page::service_keys; });
        if (keys == app.settings_rows_.end() || keys - app.settings_rows_.begin() > 0) return 2165;
        app.settings_state_.selected[app.settings_state_.category] = 0; app.settings_state_.options = true; app.settingsActivate();
        if (!app.settings_state_.at(settings_ui::Page::service_keys) || !select(Id::screenscraper_open)) return 2165;
    }
    const int opener = app.settings_state_.row();
    app.settingsActivate();
    if (!app.settings_state_.at(settings_ui::Page::screenscraper) || app.settings_rows_.size() != screenscraper::fields.size() + 2 || app.settings_rows_.back().id != Id::screenscraper_clear ||
        app.settings_rows_[0].id != Id::screenscraper || app.settings_rows_[0].key != "dev_id") return 2166;
    app.launch_animation_started_.reset(); // The earlier launch animation would cover the page.
    if (!capture(L"settings-screenscraper.png")) return 2167;
    app.back();
    if (app.settings_state_.at(settings_ui::Page::screenscraper) || app.settings_state_.row() != opener || app.settings_rows_[opener].id != Id::screenscraper_open) return 2168;
    // "Zidentyfikuj grę" on an emulated game switches between RAWG and ScreenScraper; other games only link to RAWG.
    {
        Game emulated; emulated.kind = GameKind::nonsteam; emulated.app_id = 0xF1000002u;
        emulated.shortcut_id = (uint64_t{0xF1000002u} << 32) | 0x02000000ULL;
        emulated.name = L"Paper Mario"; emulated.platform = L"Switch";
        app.openIdentifyFor(emulated);
        const auto before = app.identify_state_.source;
        if (!app.identify_state_.emulated) return 2169;
        app.identifySwitchSource();
        if (app.identify_state_.source == before) return 2170;
        if (app.identify_state_.source != identify_ui::State::Source::screenscraper) app.identifySwitchSource();
        if (!capture(L"identify-screenscraper.png")) return 2171;
        app.closeIdentify();
        app.openIdentifyFor(celeste_game);
        if (app.identify_state_.emulated || app.identify_state_.source != identify_ui::State::Source::rawg) return 2172;
        app.closeIdentify();
    }
    // Biblioteka → Folder filmów: downloaded videos in the old folder → asked whether they move along.
    {
        const auto media = userConfigDirectory() / L"media" / L"steam" / L"videos";
        std::filesystem::create_directories(media);
        { std::ofstream video(media / L"620.mp4", std::ios::binary); video << std::string(3 * 1024 * 1024, 'v'); }
        const auto chosen = root / L"Filmy";
        app.changeVideoFolder(chosen);
        if (!app.exit_prompt_ || app.prompt_kind_ != Launcher::PromptKind::move_videos || app.video_move_bytes_ != 3 * 1024 * 1024 ||
            !app.settings_.video_folder.empty()) return 2173;
        app.launch_animation_started_.reset();
        if (!capture(L"move-videos-prompt.png")) return 2174;
        app.exitPromptMove(1); // "Zostaw w starym folderze": only the setting changes.
        app.exitPromptAccept();
        if (app.exit_prompt_ || app.settings_.video_folder != chosen || !std::filesystem::exists(media / L"620.mp4")) return 2175;
        app.changeVideoFolder({}); // Back to the default: nothing of Launchme's in the chosen folder, no question.
        if (app.exit_prompt_ || !app.settings_.video_folder.empty()) return 2176;
    }
    return 0;
}
