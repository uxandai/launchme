#include "../src/platform/theme_preset.hpp"
// Included after LauncherUiTest: Settings → Interfejs → Dostosuj wygląd. The studio's tabs, its cards,
// switches and sliders applied at once to the running launcher, Cofnij, and saving, loading and deleting looks.
int LauncherUiTest::themeStudio(HWND window) {
    using namespace settings_ui;
    Launcher app; app.hwnd_ = window;
    app.boundary_feedback_ = [] {};
    if (!app.renderer_.initialize(window)) return 8100;
    const auto capture = [&](const wchar_t* name) {
        wchar_t folder[4096]{};
        if (!GetEnvironmentVariableW(L"LAUNCHME_UI_CAPTURE_DIR", folder, 4096)) return true;
        app.launch_animation_started_.reset();
        app.theme_.appear = 1; app.theme_.tab_fade = 1; app.theme_.tab_pos = static_cast<float>(app.theme_.tab); app.theme_.shown_scroll = app.theme_.scroll; // Settled.
        for (auto& option : app.theme_.options) { option.on_shown = option.on ? 1.0f : 0.0f; option.level_shown = option.level; }
        return app.renderer_.captureForTest(std::filesystem::path(folder) / name, [&] { app.paint(); });
    };
    const auto key = [&](WPARAM vk) { app.themeStudioKey(vk, false); };
    const auto find = [&](Id id) {
        for (int i = 0; i < static_cast<int>(app.theme_.options.size()); ++i) if (app.theme_.options[static_cast<size_t>(i)].id == id) return i;
        return -1;
    };
    auto library = std::make_unique<LibraryMessage>(); library->account = 1;
    for (uint32_t i = 0; i < 12; ++i) { Game game{9300 + i, L"Studio fixture " + std::to_wstring(i)}; game.installed = true; game.last_played = 1000 + i; library->games.push_back(game); }
    app.onLibrary(std::move(library));
    app.settings_.home_layout = AppSettings::HomeLayout::shelves;

    // Interfejs: the short list ends in the studio's door; the classic list is a page behind a tile.
    app.openSettings(); app.settings_state_.selectCategory(0); app.buildSettingsRows();
    const auto has = [&](Id id) {
        return std::any_of(app.settings_rows_.begin(), app.settings_rows_.end(), [&](const Row& row) { return row.id == id; });
    };
    if (!has(Id::theme_studio) || !has(Id::home_layout) || !has(Id::launch_tv_mode) || has(Id::font) || has(Id::tile_shape)) return 8101;
    if (!capture(L"settings-interface-short.png")) return 8102;
    app.revealSetting(Id::theme_studio);
    app.settingsActivate();
    if (!app.themeStudioOpen() || app.topModal() != Launcher::Modal::theme_studio) return 8103;
    if (!capture(L"theme-studio-layout.png")) return 8104;

    // Layout tab: the layout cards apply at once (Right = the next one), and its own options follow it.
    if (find(Id::home_layout) != 0 || app.theme_.options[0].cards.size() != static_cast<size_t>(home_layout_count)) return 8105;
    const auto first = app.settings_.home_layout;
    key(VK_RIGHT);
    if (app.settings_.home_layout == first || app.theme_.options[0].current != static_cast<int>(app.settings_.home_layout)) return 8106;
    key(VK_RIGHT); key(VK_RIGHT); // XMB, Flix, Metro: its options differ.
    if (app.settings_.home_layout != AppSettings::HomeLayout::metro || find(Id::metro_light) < 0 || find(Id::flix_rail) >= 0) return 8107;
    key(VK_LEFT); key(VK_LEFT); key(VK_LEFT); key(VK_LEFT); // Back to the shelves.
    if (app.settings_.home_layout != AppSettings::HomeLayout::shelves || find(Id::shelves_posters) < 0) return 8108;
    // A switch: Down to it, Enter flips it, Left and Right set it.
    key(VK_DOWN);
    if (app.theme_.options[static_cast<size_t>(app.theme_.row)].kind != theme_ui::Kind::toggle) return 8109;
    const auto toggled = app.theme_.options[static_cast<size_t>(app.theme_.row)].id;
    const bool before = toggled == Id::show_categories ? app.settings_.show_categories : false;
    key(VK_RETURN);
    if (toggled == Id::show_categories && app.settings_.show_categories == before) return 8110;

    // Tiles tab: LB/RB (PgUp/PgDn) change the tab; the shape cards and a slider apply at once.
    key(VK_NEXT); key(VK_NEXT);
    if (app.theme_.tab != static_cast<int>(theme_ui::Tab::tiles) || find(Id::tile_shape) < 0) return 8111;
    app.theme_.row = find(Id::tile_shape); app.theme_.card = app.theme_.options[static_cast<size_t>(app.theme_.row)].current;
    key(VK_RIGHT); key(VK_RIGHT);
    if (app.settings_.tile_shape != AppSettings::TileShape::square) return 8112;
    app.theme_.row = find(Id::glow_level);
    const float glow = app.settings_.glow_level;
    key(VK_LEFT);
    if (!(app.settings_.glow_level < glow)) return 8113;
    app.theme_.row = find(Id::glow_motion);
    app.theme_.card = app.theme_.options[static_cast<size_t>(app.theme_.row)].current;
    key(VK_RIGHT); key(VK_RIGHT);
    if (!app.settings_.glow_trace || app.settings_.glow_pulse) return 8114;
    if (!capture(L"theme-studio-tiles.png")) return 8115;

    // Colours and fonts.
    app.themeSetTab(static_cast<int>(theme_ui::Tab::colors));
    if (find(Id::ui_accent) < 0 || find(Id::button_color) < 0 || find(Id::accent) < 0) return 8116;
    app.theme_.row = find(Id::ui_accent); app.theme_.card = app.theme_.options[static_cast<size_t>(app.theme_.row)].current;
    key(VK_RIGHT);
    if (app.settings_.ui_accent_rgb != ui_accents[1]) return 8117;
    if (!capture(L"theme-studio-colors.png")) return 8118;
    app.themeSetTab(static_cast<int>(theme_ui::Tab::fonts));
    app.theme_.row = 0; app.theme_.card = app.theme_.options[0].current;
    key(VK_RIGHT); key(VK_RIGHT);
    if (app.settings_.font_family != fonts[2]) return 8119;
    if (!capture(L"theme-studio-fonts.png")) return 8120;
    app.themeSetTab(static_cast<int>(theme_ui::Tab::wallpaper));
    if (find(Id::glass) < 0 || find(Id::background_grid_style) < 0 || find(Id::wallpaper_pick) < 0) return 8121;
    app.settings_.wallpaper = AppSettings::WallpaperKind::picture; app.themeBuildOptions(); // The bin appears beside Tapeta.
    if (!app.theme_.options[static_cast<size_t>(find(Id::wallpaper_pick))].trash || find(Id::wallpaper_clear) >= 0) return 8140;
    app.theme_.row = find(Id::wallpaper_pick); key(VK_LEFT); // Left reaches the bin.
    if (app.theme_.card != 1) return 8141;
    if (!capture(L"theme-studio-wallpaper.png")) return 8122;
    key(VK_RETURN); // Bin: the wallpaper goes.
    if (app.settings_.wallpaper != AppSettings::WallpaperKind::none || app.theme_.options[static_cast<size_t>(find(Id::wallpaper_pick))].trash) return 8142;
    app.themeSetTab(static_cast<int>(theme_ui::Tab::other));
    if (find(Id::panel_borders) < 0 || find(Id::search_art) < 0) return 8123;
    if (!capture(L"theme-studio-other.png")) return 8124;

    // Save this look, change it, load it back; delete it (twice: the first press asks).
    const AppSettings saved_look = app.settings_;
    // Down from the last option goes nowhere (the preview is not navigated); the Wyglądy tab holds the rest.
    const int last = static_cast<int>(app.theme_.options.size()) - 1;
    app.theme_.row = last; key(VK_DOWN);
    if (app.theme_.row != last || app.theme_.bar) return 8125;
    app.themeSetTab(static_cast<int>(theme_ui::Tab::looks));
    if (!app.theme_.bar || app.theme_.item != theme_ui::State::undo_item) return 8125;
    key(VK_RIGHT); key(VK_RETURN); // Zapisz wygląd…
    if (!app.theme_.naming || app.theme_.name.empty()) return 8126;
    for (int i = 0; i < 20; ++i) key(VK_BACK);
    for (const wchar_t ch : std::wstring(L"Neon: 1/x")) app.themeStudioChar(ch); // The file-name characters are left out.
    if (app.theme_.name != L"Neon 1x" || !capture(L"theme-studio-naming.png")) return 8127;
    key(VK_RETURN);
    if (app.theme_.naming || app.theme_.saved != std::vector<std::wstring>{L"Neon 1x"} || app.theme_.active != 0) return 8128;
    app.themeSetTab(static_cast<int>(theme_ui::Tab::tiles)); // Tab change keeps the saved list.
    if (app.theme_.bar) return 8139;
    app.settings_.tile_shape = AppSettings::TileShape::rounded; app.settings_.font_family = L"Arial"; app.settingsChanged();
    app.themeSetTab(static_cast<int>(theme_ui::Tab::looks)); app.theme_.item = theme_ui::State::first_saved;
    key(VK_RETURN); // Load.
    if (!theme_preset::sameLook(app.settings_, saved_look) || app.settings_.font_family != fonts[2] || app.theme_.active != 0) return 8129;
    if (!capture(L"theme-studio-saved.png")) return 8130;
    key(VK_DELETE);
    if (!app.theme_.delete_armed || app.theme_.saved.size() != 1) return 8131;
    key(VK_DELETE);
    if (!app.theme_.saved.empty() || app.theme_.active != -1) return 8132;

    // Cofnij zmiany restores the look the studio opened with; Back leaves it with the changes kept on disk.
    app.theme_.item = theme_ui::State::undo_item;
    key(VK_RETURN);
    if (app.settings_.font_family != L"SST" || app.settings_.tile_shape != AppSettings::TileShape::squircle) return 8133;
    app.settings_.glass = 0.35f; app.settingsChanged();
    key(VK_ESCAPE);
    if (app.themeStudioOpen() || app.topModal() != Launcher::Modal::none && app.topModal() != Launcher::Modal::theme_studio) return 8134;
    if (std::abs(loadAppSettings().glass - 0.35f) > 0.001f) return 8135;

    // The pointer: the tabs, a card and the close button.
    app.themeStudio(true);
    const auto view = app.renderer_.viewport();
    const theme_ui::Layout layout{view.width, view.height};
    const auto tab = layout.tab(2);
    app.themeStudioClick((tab.left + tab.right) * 0.5f, (tab.top + tab.bottom) * 0.5f);
    if (app.theme_.tab != 2) return 8136;
    const int shape = find(Id::tile_shape);
    const auto card = layout.card(app.theme_.options, shape, 1, app.theme_.scroll);
    app.themeStudioClick((card.left + card.right) * 0.5f, (card.top + card.bottom) * 0.5f);
    if (app.settings_.tile_shape != AppSettings::TileShape::rounded || app.theme_.options[static_cast<size_t>(shape)].current != 1) return 8137;
    const auto close = layout.close();
    app.themeStudioClick((close.left + close.right) * 0.5f, (close.top + close.bottom) * 0.5f);
    if (app.themeStudioOpen()) return 8138;
    return 0;
}
