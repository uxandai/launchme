// Included after LauncherUiTest: "Znajdź" in Edytuj metadane (src/app/metadata_find.cpp). The button in the
// dialog's heading opens a pane that searches the stores and databases with the game's title and its Switch
// title ID (from the file name), lists what they found, and "Wstaw" copies the ticked details into the dialog;
// only Zapisz makes them the player's own. Tryb TV (the pad, the on-screen keyboard) and the desktop window
// (keys, the mouse) share it. The sources are an injected fake: nothing reaches the network. Captures: meta-find-*.png.
int LauncherUiTest::metadataFind(HWND window) {
    namespace mf = meta_find;
    using desktop_ui::MetaItem;
    std::vector<std::wstring> asked; // Every request the fake answered.
    const auto reply = [](std::string body, uint32_t status = 200) {
        HttpResponse response; response.status = status; response.body.assign(body.begin(), body.end());
        return response;
    };
    Launcher app;
    app.hwnd_ = window; app.execute_ = fakeExecute;
    app.settings_.launch_animation = false; app.settings_.tour_done = true;
    app.settings_.glance = app.settings_.companion = false;
    app.boundary_feedback_ = [] {};
    app.metadata_find_get_ = [&](const std::wstring& url, const std::wstring&, size_t) {
        asked.push_back(url);
        if (url.find(L"nintendo-europe") != std::wstring::npos)
            return reply(R"({"response":{"docs":[{"title":"Star Fox","application_id_s":"0400e88025b1a000","excerpt":"Fox McCloud and his wingmen are back.","publisher":"Nintendo","dates_released_dts":["2026-06-25T00:00:00Z"],"pretty_game_categories_txt":["Shooter"],"system_names_txt":["Nintendo Switch 2"]}]}})");
        if (url.find(L"storesearch") != std::wstring::npos) return reply(R"({"total":1,"items":[{"type":"app","name":"Zelda Fixture","id":4242}]})");
        if (url.find(L"appids=4242") != std::wstring::npos)
            return reply(R"({"4242":{"success":true,"data":{"name":"Zelda Fixture","short_description":"A Steam adventure.","developers":["Dev Studio"],"publishers":["Pub House"],"genres":[{"id":"1","description":"Adventure"}],"release_date":{"coming_soon":false,"date":"24 Feb, 2022"}}}})");
        return reply("{}", 404);
    };
    if (!app.renderer_.initialize(window)) return 11000;
    const auto capture = [&](Launcher& target, const wchar_t* name) {
        wchar_t folder[4096]{};
        const auto count = GetEnvironmentVariableW(L"LAUNCHME_UI_CAPTURE_DIR", folder, 4096);
        if (!count || count >= 4096) { target.paint(); return true; }
        target.notifications_.clear();
        return target.renderer_.captureForTest(std::filesystem::path(folder) / name, [&] { target.paint(); });
    };
    Game star; star.kind = GameKind::nonsteam; star.shortcut_id = 9101; star.name = L"Star Fox"; star.platform = L"Nintendo Switch 2";
    star.rom = L"K:\\Emulation\\roms\\switch2\\Star Fox [0400E88025B1A000].nup";
    auto library = std::make_unique<LibraryMessage>(); library->account = 1;
    library->games.push_back(star);
    app.onLibrary(std::move(library)); app.showCategory(0);

    // ---- Tryb TV: Znajdź is the first row; A opens it, the file name's title ID goes to the eShop.
    app.pad_input_ = true;
    app.openMetadata(star);
    auto& meta = app.desktop_meta_;
    if (!app.metadataOpen() || meta.find.open || meta.focus != 0) return 11001;
    app.metadataKey(VK_UP, false);
    if (meta.focus != static_cast<int>(MetaItem::find)) return 11002;
    app.metadataKey(VK_RETURN, false);
    auto& find = meta.find;
    if (!find.open || find.query != L"Star Fox" || find.busy || find.results.size() != 1) return 11003;
    if (find.results[0].title != L"Star Fox" || find.results[0].source != L"Nintendo eShop" || find.results[0].note != L"Nintendo Switch 2") return 11004;
    if (std::none_of(asked.begin(), asked.end(), [](const std::wstring& url) { return url.find(L"application_id_s:0400e88025b1a000") != std::wstring::npos; })) return 11005;
    if (find.results[0].values[0].find(L"Fox McCloud") != 0 || find.results[0].values[2] != L"Nintendo" || find.results[0].values[3] != L"2026-06-25" ||
        find.results[0].values[4] != L"Shooter" || !find.results[0].values[1].empty()) return 11006; // No developer from the eShop.
    if (find.canTake(1) || !find.canTake(0) || !find.take[0] || find.take[1] || !find.take[2] || !find.take[3] || !find.take[4]) return 11007;
    if (!capture(app, L"meta-find-tv.png")) return 11008;
    // The pad: Down to the result, to the details (the missing developer is skipped), A unticks Opis.
    app.metadataKey(VK_DOWN, false);
    if (find.focus != mf::item::result_first) return 11009;
    app.metadataKey(VK_DOWN, false);
    if (find.focus != mf::item::take_first) return 11010;
    app.metadataKey(VK_RETURN, false);
    if (find.take[0]) return 11011;
    app.metadataKey(VK_DOWN, false);
    if (find.focus != mf::item::take_first + 2) return 11012; // Wydawca: Producent has nothing to take.
    for (int i = 0; i < 3; ++i) app.metadataKey(VK_DOWN, false);
    if (find.focus != mf::item::insert) return 11013;
    // Nothing is in the dialog's fields until Wstaw.
    if (meta.fields[3] == L"Nintendo") return 11014;
    app.metadataKey(VK_RETURN, false);
    if (find.open || !meta.open || meta.fields[3] != L"Nintendo" || meta.fields[4] != L"2026-06-25" || meta.fields[5] != L"Shooter") return 11015;
    if (meta.fields[1].find(L"Fox McCloud") != std::wstring::npos) return 11016; // Opis was unticked.
    if (meta.focus != 3 || app.metadataOverride(star)) return 11017; // Not saved yet.
    if (!capture(app, L"meta-find-tv-inserted.png")) return 11018;
    // B in the pane goes back to the fields, B again closes the dialog.
    app.metadataKey(VK_UP, false); app.metadataKey(VK_UP, false); app.metadataKey(VK_UP, false); app.metadataKey(VK_UP, false);
    if (meta.focus != static_cast<int>(MetaItem::find)) return 11019;
    app.metadataKey(VK_RETURN, false);
    if (!find.open) return 11020;
    app.metadataKey(VK_ESCAPE, false);
    if (find.open || !meta.open || meta.fields[3] != L"Nintendo") return 11021;
    app.metadataKey(VK_ESCAPE, false);
    if (app.metadataOpen()) return 11022;

    // The title typed with the on-screen keyboard: only that search runs, without the file's title ID.
    app.openMetadata(star);
    app.metadataKey(VK_UP, false); app.metadataKey(VK_RETURN, false);
    if (!meta.find.open) return 11023;
    asked.clear();
    app.metadataKey(VK_RETURN, false); // A on the title: the keyboard.
    if (!app.keyboard_.open || app.keyboardPreview() != L"Star Fox") return 11024;
    for (int i = 0; i < 8; ++i) app.keyboardBackspace();
    for (const wchar_t c : std::wstring(L"Zelda")) app.onCharacter(c);
    if (meta.find.query != L"Zelda") return 11025;
    app.keyboardDone();
    if (app.keyboard_.open || !meta.open || !meta.find.open) return 11026;
    if (asked.empty() || std::any_of(asked.begin(), asked.end(), [](const std::wstring& url) { return url.find(L"application_id_s") != std::wstring::npos; })) return 11027;
    bool steam_listed = false;
    for (const auto& result : meta.find.results) steam_listed = steam_listed || (result.source == L"Steam" && result.title == L"Zelda Fixture" && result.values[1] == L"Dev Studio");
    if (!steam_listed) return 11028;
    app.desktopMetaClose();
    if (app.metadataOpen() || meta.find.open) return 11029;

    // ---- The desktop window: the button, the keys, the mouse.
    app.pad_input_ = false;
    app.desktop_mode_ = true; app.audio_.setDesktop(true);
    app.buildShelves(); app.showCategory(0);
    for (int i = 0; i < 20; ++i) { app.last_frame_ -= std::chrono::milliseconds(50); app.tick(); }
    app.desktopMetaOpen(star);
    if (!meta.open || meta.find.open) return 11030;
    app.paint();
    const auto view = app.renderer_.desktopViewport();
    const float scale = view.scale;
    const desktop_ui::MetaLayout dialog{view.width, view.height};
    const mf::Layout layout{view.width, view.height};
    const auto click = [&](settings_ui::Rect r, bool press = true) {
        return app.desktopMetaPointer((r.left + r.right) * 0.5f * scale, (r.top + r.bottom) * 0.5f * scale, press);
    };
    // The button in the heading.
    click(dialog.findButton(), false);
    if (meta.hover != static_cast<int>(MetaItem::find)) return 11031;
    if (!capture(app, L"meta-find-desktop-dialog.png")) return 11032;
    click(dialog.findButton());
    if (!meta.find.open || meta.find.results.size() != 1 || meta.find.focus != mf::item::query) return 11033;
    if (!capture(app, L"meta-find-desktop.png")) return 11034;
    if (app.desktopCursor((layout.field().left + 40) * scale, (layout.field().top + 10) * scale) != IDC_IBEAM ||
        app.desktopCursor((layout.result(0).left + 40) * scale, (layout.result(0).top + 10) * scale) != IDC_HAND) return 11035;
    // Typing changes the title; Enter searches it, without the file's title ID.
    asked.clear();
    const auto generation = meta.find.generation;
    for (const wchar_t c : std::wstring(L" 2")) app.onCharacter(c);
    if (meta.find.query != L"Star Fox 2") return 11036;
    app.desktopMetaKey(VK_RETURN);
    if (meta.find.generation == generation || std::any_of(asked.begin(), asked.end(), [](const std::wstring& url) { return url.find(L"application_id_s") != std::wstring::npos; })) return 11037;
    // Tab walks the pane; Esc leaves it for the fields, the dialog stays.
    app.desktopMetaKey(VK_TAB);
    if (meta.find.focus != mf::item::search) return 11038;
    app.desktopMetaKey(VK_ESCAPE);
    if (meta.find.open || !meta.open) return 11039;
    // Back with the game's own title: the mouse chooses, ticks and inserts.
    app.desktopMetaActivate(static_cast<int>(MetaItem::find));
    if (!meta.find.open || meta.find.results.size() != 1) return 11040;
    click(layout.result(0));
    if (meta.find.focus != mf::item::result_first || meta.find.chosen != 0) return 11041;
    click(layout.take(3)); // Data premiery: unticked, then ticked again.
    if (meta.find.take[3]) return 11042;
    click(layout.take(3));
    if (!meta.find.take[3]) return 11043;
    click(layout.take(1)); // Producent: the eShop has none; the click changes nothing.
    if (meta.find.take[1]) return 11044;
    click(layout.insertButton());
    if (meta.find.open || meta.fields[1].find(L"Fox McCloud") != 0 || meta.fields[2] != L"" || meta.fields[3] != L"Nintendo" || meta.fields[4] != L"2026-06-25") return 11045;
    if (app.metadataOverride(star)) return 11046; // Still only in the dialog.
    app.desktopMetaActivate(static_cast<int>(MetaItem::save));
    const auto* own = app.metadataOverride(star);
    if (meta.open || !own || own->publisher != L"Nintendo" || own->release_date != L"2026-06-25" || own->genres != L"Shooter" || own->description.find(L"Fox McCloud") != 0) return 11047;
    app.desktop_mode_ = false; app.audio_.setDesktop(false);
    return 0;
}
