#include "../src/artwork/platform_logo.hpp"
#include "../src/library/emulation_platforms.hpp"
#include "../src/ui/metro_layout.hpp"

// The logo rows (Platformy PC, Emulacja, Wydawcy, Developerzy) with more entries than a Metro
// category page holds: every logo has its tile on Metro, the studio logos fit their tiles, the
// default home names none of them under the row, and the studios' tiles are dark like the stores'.
// Nothing reaches the network. Captures: studio-tiles-*.png.
int LauncherUiTest::studioTiles(HWND window) {
    using metro_layout::Role;
    const auto config = userConfigDirectory();
    Launcher app;
    app.hwnd_ = window; app.execute_ = fakeExecute;
    app.settings_.launch_animation = false; app.settings_.tour_done = true;
    app.settings_.glance = app.settings_.companion = false;
    app.settings_.shelves_category_all = true;
    app.settings_.show_emulation = true;
    app.settings_.enabled_pc_sources.insert("gog");
    if (!app.renderer_.initialize(window)) return 9900;
    const auto capture = [&](const wchar_t* name) {
        wchar_t folder[4096]{};
        const auto count = GetEnvironmentVariableW(L"LAUNCHME_UI_CAPTURE_DIR", folder, 4096);
        if (!count || count >= 4096) { app.paint(); return true; }
        app.notifications_.clear();
        return app.renderer_.captureForTest(std::filesystem::path(folder) / name, [&] { app.paint(); });
    };
    const auto settle = [&] { for (int i = 0; i < 40; ++i) { app.last_frame_ -= std::chrono::milliseconds(50); app.tick(); } };
    // Fourteen games, each published and made by another studio with a logo.
    const char* names[]{"Capcom", "Sega", "Ubisoft", "Konami", "Atari", "Activision", "Bethesda", "Blizzard",
                        "Codemasters", "Eidos", "Interplay", "Koei", "Maxis", "Namco"};
    constexpr size_t studio_count = std::size(names);
    for (size_t i = 0; i < studio_count; ++i) {
        const auto id = 9900 + static_cast<uint32_t>(i);
        const std::string developer = names[(i + 1) % studio_count];
        const std::string json = "{\"" + std::to_string(id) + R"(":{"success":true,"data":{"short_description":"Opis.","developers":[")" +
            developer + R"("],"publishers":[")" + names[i] + R"("]}}})";
        atomic_file::write(config / L"metadata" / (std::to_wstring(id) + L".details.json"), json);
    }
    auto library = std::make_unique<LibraryMessage>(); library->account = 1;
    for (size_t i = 0; i < studio_count; ++i) {
        Game game{9900 + static_cast<uint32_t>(i), L"Studio game " + std::to_wstring(i)}; game.installed = true;
        game.last_played = i < 3 ? 1'760'000'000 + i : 0;
        library->games.push_back(game);
    }
    app.onLibrary(std::move(library));
    // Every emulated console: one shortcut each.
    for (size_t i = 0; i < emulation_platforms.size(); ++i) {
        Game rom{0x90000100u + static_cast<uint32_t>(i), L"ROM " + std::to_wstring(i)};
        rom.kind = GameKind::nonsteam; rom.platform = std::wstring(emulation_platforms[i].name); rom.installed = true;
        app.shortcut_library_.push_back(rom);
    }
    app.buildShelves(); app.showCategory(0);
    app.scanning_ = false;
    app.startGameIndex();
    if (!app.game_index_) return 9901;
    const auto pump = [&] {
        MSG message{};
        while (PeekMessageW(&message, window, game_index_message, game_index_message, PM_REMOVE))
            app.onGameIndex(std::unique_ptr<GameIndexMessage>(reinterpret_cast<GameIndexMessage*>(message.lParam)));
    };
    app.syncGameIndex();
    app.game_index_changed_ = std::chrono::steady_clock::now() - std::chrono::seconds(5);
    app.tickGameIndex();
    const auto& publishers = app.studio_groups_[static_cast<size_t>(studios::Role::publisher)];
    const auto& developers = app.studio_groups_[static_cast<size_t>(studios::Role::developer)];
    for (const auto end = GetTickCount64() + 5000; (publishers.size() < studio_count || developers.size() < studio_count) && GetTickCount64() < end; Sleep(5)) pump();
    if (publishers.size() != studio_count || developers.size() != studio_count) return 9902;
    const auto row = [&](const char* id) -> int {
        for (size_t i = 0; i < app.categories_.size(); ++i) if (app.categories_[i].id == id) return static_cast<int>(i);
        return -1;
    };
    if (row("emulation") < 0 || app.categories_[static_cast<size_t>(row("emulation"))].games.size() != emulation_platforms.size()) return 9903;
    // Logos: the studios' own, a light stand-in for the consoles and stores.
    const auto logoOf = [&](const Game& game) {
        if (game.kind == GameKind::platform && (game.platform.starts_with(L"publisher:") || game.platform.starts_with(L"developer:")))
            return platformLogoCached(game.platform, {});
        return pngFixture(250, 96, 40, true, WICRect{4, 6, 88, 28});
    };
    const auto bindLogos = [&] {
        for (size_t i = 0; i < app.cards_.size(); ++i) {
            const auto& game = app.cards_[i].game;
            if (game.kind == GameKind::platform || game.kind == GameKind::pc_platform) {
                app.renderer_.setPlatformCover(i, logoOf(game)); app.cards_[i].cover_opacity = 1;
            }
        }
    };

    // Studios on the stores' dark fill in every view; consoles keep their colour.
    const auto& capcom = app.categories_[static_cast<size_t>(row("publishers"))].games[0];
    const auto& ps2 = *std::find_if(app.categories_[static_cast<size_t>(row("emulation"))].games.begin(),
                                    app.categories_[static_cast<size_t>(row("emulation"))].games.end(), [](const Game& game) { return game.platform == L"PS2"; });
    const auto dark = app.renderer_.platformTileFillForTest(capcom), console = app.renderer_.platformTileFillForTest(ps2);
    if (!capcom.platform.starts_with(L"publisher:") || dark.r > 0.06f || dark.g > 0.06f || dark.b > 0.07f || console.b < 0.5f) return 9910;

    // ---- The default home: each logo row focused on its second tile.
    for (const auto* id : {"pc-platforms", "emulation", "publishers", "developers"}) {
        app.showCategory(static_cast<size_t>(row(id)));
        bindLogos();
        app.focusCard(std::min<size_t>(2, app.cards_.size() - 1));
        settle();
        const auto file = L"studio-tiles-home-" + text_encoding::wide(id) + L".png";
        if (!capture(file.c_str())) return 9904;
    }
    // The boards.
    for (const auto* id : {"publishers", "emulation"}) {
        app.openCategoryLibrary(id);
        bindLogos(); settle();
        const auto file = L"studio-tiles-board-" + text_encoding::wide(id) + L".png";
        if (!capture(file.c_str())) return 9905;
        app.back();
    }

    // ---- Metro: every logo of each row on its page.
    app.settings_.home_layout = AppSettings::HomeLayout::metro;
    app.buildShelves(); app.showCategory(0);
    if (!app.metroActive()) return 9906;
    for (const auto* id : {"pc-platforms", "emulation", "publishers", "developers"}) {
        std::optional<size_t> page;
        for (size_t p = 0; p < app.metro_pages_.size(); ++p) if (app.categories_[app.metro_pages_[p].category].id == id) page = p;
        if (!page) return 9907;
        const auto& p = app.metro_pages_[*page];
        // Every logo of the row (more than a poster page's twelve), each in a logo cell.
        const auto& listed = app.categories_[p.category].games;
        if (p.count != listed.size() || (std::string_view(id) != "pc-platforms" && p.count <= metro_layout::category_tiles)) return 9911;
        for (size_t i = p.begin; i < p.begin + p.count; ++i)
            if (app.metro_tiles_[i].role != Role::logo || app.metro_tiles_[i].rect.height() != metro_layout::cell_h) return 9912;
        app.metroFocus(p.begin);
        bindLogos(); settle();
        const auto file = L"studio-tiles-metro-" + text_encoding::wide(id) + L".png";
        if (!capture(file.c_str())) return 9908;
        app.metroFocus(p.begin + p.count - 1);
        bindLogos(); settle();
        const auto last = L"studio-tiles-metro-" + text_encoding::wide(id) + L"-end.png";
        if (!capture(last.c_str())) return 9909;
        // The page scrolled to its last logo: on screen, inside the right margin.
        const auto& end = app.cards_[p.begin + p.count - 1];
        if (end.x < metro_layout::left - 1 || end.x + end.width() > app.renderer_.viewport().width - metro_layout::right_margin + 1) return 9913;
    }
    // ---- The other home layouts: the publishers' row on the stores' dark fill.
    for (const auto layout : {AppSettings::HomeLayout::xmb, AppSettings::HomeLayout::flix, AppSettings::HomeLayout::psfive}) {
        app.settings_.home_layout = layout;
        app.buildShelves();
        const int publishers_row = row("publishers");
        if (publishers_row < 0) return 9914;
        app.showCategory(static_cast<size_t>(publishers_row));
        bindLogos(); settle();
        const auto file = L"studio-tiles-layout-" + std::to_wstring(static_cast<int>(layout)) + L".png";
        if (!capture(file.c_str())) return 9915;
    }
    app.settings_.home_layout = AppSettings::HomeLayout::shelves;
    app.buildShelves(); app.showCategory(0);
    return 0;
}
