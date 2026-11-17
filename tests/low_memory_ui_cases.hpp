// System → Wydajność → Tryb niskiego zużycia RAM (src/app/memory.cpp): on by default; covers decoded at
// their size on screen and as BC1 where the GPU samples it, their bytes let go once uploaded (the same
// picture bound again, or read again, keeps its bitmap), raised when drawn larger; background
// pictures only around the focus; everything given back while the launcher is hidden, and back again.
int LauncherUiTest::lowMemory(HWND window) {
    Launcher app;
    app.hwnd_ = window; app.execute_ = fakeExecute;
    app.settings_.launch_animation = false;
    if (!app.renderer_.initialize(window)) return 9600;
    if (!AppSettings{}.low_memory || !app.settings_.low_memory) return 9601;
    app.applyLowMemory();
    if (!app.renderer_.lowMemory() || app.art_.budget() != Launcher::low_memory_art_budget) return 9602;
    const auto capture = [&](const wchar_t* name) {
        wchar_t folder[4096]{};
        const auto count = GetEnvironmentVariableW(L"LAUNCHME_UI_CAPTURE_DIR", folder, 4096);
        if (!count || count >= 4096) { app.paint(); return true; }
        InvalidateRect(window, nullptr, FALSE);
        return app.renderer_.captureForTest(std::filesystem::path(folder) / name, [&] { app.paint(); });
    };

    // The window's own (GPU) frame, where BC1 bitmaps are sampled: shown, painted and copied (DWM).
    const auto captureWindow = [&](const wchar_t* name) {
        wchar_t folder[4096]{};
        const auto count = GetEnvironmentVariableW(L"LAUNCHME_UI_CAPTURE_DIR", folder, 4096);
        if (!count || count >= 4096) return true;
        ShowWindow(window, SW_SHOWNOACTIVATE); app.paint(); app.paint(); Sleep(120);
        RECT r{}; GetClientRect(window, &r);
        const HDC screen = GetDC(window), memory = CreateCompatibleDC(screen);
        BITMAPINFO info{}; info.bmiHeader = {sizeof(BITMAPINFOHEADER), r.right, -r.bottom, 1, 32, BI_RGB};
        void* bits = nullptr;
        const HBITMAP bitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
        const auto old = SelectObject(memory, bitmap);
        const bool copied = PrintWindow(window, memory, PW_CLIENTONLY | PW_RENDERFULLCONTENT) != FALSE;
        bool saved = false;
        if (copied && bits) {
            auto* pixels = static_cast<uint8_t*>(bits);
            for (LONG i = 0; i < r.right * r.bottom; ++i) pixels[i * 4 + 3] = 255;
            Microsoft::WRL::ComPtr<IWICImagingFactory> wic; Microsoft::WRL::ComPtr<IWICStream> stream;
            Microsoft::WRL::ComPtr<IWICBitmapEncoder> encoder; Microsoft::WRL::ComPtr<IWICBitmapFrameEncode> frame;
            auto format = GUID_WICPixelFormat32bppBGRA;
            const auto file = (std::filesystem::path(folder) / name).wstring();
            saved = SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(wic.GetAddressOf()))) &&
                SUCCEEDED(wic->CreateStream(stream.GetAddressOf())) && SUCCEEDED(stream->InitializeFromFilename(file.c_str(), GENERIC_WRITE)) &&
                SUCCEEDED(wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, encoder.GetAddressOf())) && SUCCEEDED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache)) &&
                SUCCEEDED(encoder->CreateNewFrame(frame.GetAddressOf(), nullptr)) && SUCCEEDED(frame->Initialize(nullptr)) &&
                SUCCEEDED(frame->SetSize(r.right, r.bottom)) && SUCCEEDED(frame->SetPixelFormat(&format)) &&
                SUCCEEDED(frame->WritePixels(r.bottom, r.right * 4, r.right * r.bottom * 4, pixels)) && SUCCEEDED(frame->Commit()) && SUCCEEDED(encoder->Commit());
        }
        SelectObject(memory, old); DeleteObject(bitmap); DeleteDC(memory); ReleaseDC(window, screen);
        ShowWindow(window, SW_HIDE);
        return saved;
    };

    // Settings → System → Wydajność: the row, on, and switched off and on again.
    app.openSettings(); app.settings_state_.category = static_cast<int>(settings_ui::Category::system); app.buildSettingsRows();
    const auto row = std::find_if(app.settings_rows_.begin(), app.settings_rows_.end(), [](const auto& r) { return r.id == settings_ui::Id::low_memory; });
    if (row == app.settings_rows_.end() || !row->checked || row->control != settings_ui::Control::toggle) return 9603;
    app.settings_state_.options = true; app.settings_state_.selected[app.settings_state_.category] = static_cast<int>(row - app.settings_rows_.begin());
    app.ensureSettingVisible();
    if (!capture(L"low-memory-setting.png")) return 9604;
    // Kompresja grafik (BC1) right under it, on by default, and with or without the low memory mode.
    const size_t low_row = static_cast<size_t>(row - app.settings_rows_.begin());
    const auto has_bc1 = [&] { return low_row + 1 < app.settings_rows_.size() && app.settings_rows_[low_row + 1].id == settings_ui::Id::texture_compression; };
#ifdef _WIN32
    constexpr bool bc1_row = true;
    if (!AppSettings{}.texture_compression || !has_bc1() || !app.settings_rows_[low_row + 1].checked) return 9640;
#else
    constexpr bool bc1_row = false; // Linux keeps artwork as BGRA: no such row.
    if (has_bc1()) return 9640;
#endif
    app.settingsActivate(); app.saveSettings();
    if (app.settings_.low_memory || app.renderer_.lowMemory() || app.art_.budget() != Launcher::art_budget || loadAppSettings().low_memory) return 9605;
    if (has_bc1() != bc1_row) return 9641; // Not only under the low memory mode.
    app.settings_state_.selected[app.settings_state_.category] = static_cast<int>(low_row);
    app.settingsActivate(); app.saveSettings();
    if (!app.settings_.low_memory || !app.renderer_.lowMemory() || !loadAppSettings().low_memory || has_bc1() != bc1_row) return 9606;
#ifdef _WIN32
    // Switched off and on again; an older settings file's low_memory_bc1=0 keeps the new default.
    app.settings_state_.selected[app.settings_state_.category] = static_cast<int>(low_row + 1);
    app.settingsActivate(); app.saveSettings();
    if (app.settings_.texture_compression || loadAppSettings().texture_compression || app.settings_rows_[low_row + 1].checked) return 9642;
    app.settingsActivate(); app.saveSettings();
    if (!app.settings_.texture_compression || !loadAppSettings().texture_compression || !app.settings_rows_[low_row + 1].checked) return 9645;
    if (!parseAppSettings("low_memory_bc1=0\n").texture_compression || parseAppSettings("texture_compression=0\n").texture_compression) return 9646;
    app.ensureSettingVisible();
    if (!capture(L"low-memory-bc1-setting.png")) return 9644;
#endif
    app.back();

    auto library = std::make_unique<LibraryMessage>(); library->account = 1;
    for (uint32_t i = 0; i < 12; ++i) { Game game{9600 + i, L"Low memory game " + std::to_wstring(i)}; game.last_played = 1700000000 - i; library->games.push_back(game); }
    app.onLibrary(std::move(library)); app.showCategory(0);
    app.snapCardsForTransition(); app.detail_ = 1;
    const auto deliver = [&](const Game& game, CoverMessage::Kind kind, std::vector<uint8_t> bytes) {
        auto message = std::make_unique<CoverMessage>();
        message->key = gameKey(game); message->generation = app.jobs_->generation.load();
        message->kind = kind; message->bytes = std::move(bytes);
        app.onCover(std::move(message));
    };
    std::vector<size_t> games;
    for (size_t i = 0; i < app.cards_.size(); ++i) if (isGame(app.cards_[i].game)) games.push_back(i);
    if (games.size() < 8) return 9607;
    // LAUNCHME_PERF_COVER_FILE: real covers for the GPU capture (low-memory-shelf-gpu.png).
    std::vector<uint8_t> real_cover;
    if (wchar_t file[4096]{}; GetEnvironmentVariableW(L"LAUNCHME_PERF_COVER_FILE", file, 4096)) {
        std::ifstream input(file, std::ios::binary); real_cover.assign(std::istreambuf_iterator<char>{input}, {});
    }
    for (const size_t i : games) {
        deliver(app.cards_[i].game, CoverMessage::Kind::cover, real_cover.empty() ? jpegFixture(600, 900, static_cast<uint32_t>(i)) : real_cover);
        deliver(app.cards_[i].game, CoverMessage::Kind::hero, jpegFixture(1280, 720, 100 + static_cast<uint32_t>(i)));
    }
    app.paint(); app.paint(); // The window's target: decodes uploaded.
    const size_t first = games.front();
    // Decoded at the size drawn: 320 DIPs at this window's scale, not the 1024 px cap (the fixture is 600x900).
    const uint32_t edge = app.renderer_.coverEdgeForTest();
    auto* bitmap = app.renderer_.coverBitmapForTest(first);
    if (edge >= 600 || !bitmap) return 9608;
    const auto size = bitmap->GetPixelSize();
    if (std::min(size.width, size.height) != edge || size.width % 4 || size.height % 4) return 9609;
    // Uploaded: the bytes went (the artwork cache keeps them); as BC1 where the GPU samples it.
    if (!app.renderer_.coverBytesDroppedForTest(first) || !app.renderer_.hasCover(first) || !app.art_.contains(gameKey(app.cards_[first].game))) return 9610;
    if (app.renderer_.bc1ForTest() != app.renderer_.coverCompressedForTest(first)) return 9611;
    std::cout << "Low memory: window target BC1=" << app.renderer_.bc1ForTest() << "; cover " << size.width << 'x' << size.height
              << " (edge " << edge << "); card bitmaps " << app.renderer_.textureStats().bytes / 1024 << " KB\n";
    if (app.renderer_.textureStats().bytes >= static_cast<size_t>(size.width) * size.height * 4 * games.size()) return 9612;
    app.snapCardsForTransition(); app.detail_ = 1; // Settled: the focused game's background shown.
    if (!capture(L"low-memory-shelf.png") || !captureWindow(L"low-memory-shelf-gpu.png")) return 9618;
    app.paint(); // A capture made its own target: the bitmaps were built again from the cache's bytes.
    bitmap = app.renderer_.coverBitmapForTest(first);
    if (!bitmap || !app.renderer_.coverBytesDroppedForTest(first)) return 9619;
    // Bound again (the same buffer, or the same bytes read from disk again): the same bitmap, no fade.
    const auto* art = app.art_.find(gameKey(app.cards_[first].game));
    app.renderer_.setCover(first, art->cover, art->focus);
    if (app.renderer_.coverBitmapForTest(first) != bitmap) return 9613;
    app.renderer_.setCover(first, shareImage(std::vector<uint8_t>(*art->cover)), art->focus);
    if (app.renderer_.coverBitmapForTest(first) != bitmap || !app.renderer_.hasCover(first)) return 9614;
    // Drawn twice as large (the launch zoom, Szczegóły): decoded again larger, uploaded with the next frame.
    app.renderer_.wantEdgeForTest(first, 2.0f);
    if (app.renderer_.coverBitmapForTest(first) != bitmap) return 9615; // Never while it is being drawn.
    app.paint();
    auto* larger = app.renderer_.coverBitmapForTest(first);
    if (!larger || std::min(larger->GetPixelSize().width, larger->GetPixelSize().height) <= edge) return 9616;
    // Another picture replaces it.
    app.renderer_.setCover(first, shareImage(jpegFixture(600, 900, 777)), art->focus);
    app.paint();
    // (At the usual size again: the old bitmap's address may be reused, so its size tells them apart.)
    const auto* replaced = app.renderer_.coverBitmapForTest(first);
    if (!replaced || std::min(replaced->GetPixelSize().width, replaced->GetPixelSize().height) != edge) return 9617;

    // Background pictures: the focused card and its neighbours only.
    for (const size_t i : games) app.renderer_.setHero(i, app.art_.find(gameKey(app.cards_[i].game))->hero);
    app.paint();
    app.focusCard(games[4]); app.requestVisibleArt(); app.paint();
    if (!app.renderer_.heroBitmapForTest(games[4]) || app.renderer_.heroBitmapForTest(games[0]) || app.renderer_.heroBitmapForTest(games[7])) return 9633;

    // The device goes while the cache no longer has the bytes: the slot is empty, the launcher asked
    // to bind it again (requested from the disk lane).
    app.art_.erase(gameKey(app.cards_[games[1]].game));
    app.renderer_.discardTargetForTest(); app.paint();
    if (app.renderer_.hasCover(games[1]) || !app.renderer_.coverReadyForTest(games[2])) return 9620;
    app.pollMemory();
    {
        std::lock_guard lock(app.jobs_->mutex);
        if (!app.jobs_->requested.contains(artRequestKey(app.cards_[games[1]].game, CoverMessage::Kind::cover))) return 9621;
    }

    // Minimized: after the delay everything is given back, nothing painted meanwhile; restored, built again.
    ShowWindow(window, SW_SHOWMINNOACTIVE);
    if (!app.launcherHidden()) return 9622;
    app.pollMemory();
    if (app.memory_released_) return 9623; // Not at once: a short switch away keeps everything.
    app.memory_hidden_since_ = GetTickCount64() - Launcher::memory_release_delay_ms - 1;
    app.pollMemory();
    if (!app.memory_released_ || app.renderer_.targetForTest() || app.renderer_.hasCover(first) || !app.art_.empty()) return 9624;
    app.paint();
    if (app.renderer_.targetForTest()) return 9625;
    {
        std::lock_guard lock(app.jobs_->mutex);
        if (app.jobs_->requested.contains(artRequestKey(app.cards_[first].game, CoverMessage::Kind::cover))) return 9626;
    }
    ShowWindow(window, SW_RESTORE); ShowWindow(window, SW_HIDE);
    if (app.launcherHidden()) return 9627;
    app.pollMemory();
    if (app.memory_released_) return 9628;
    {
        std::lock_guard lock(app.jobs_->mutex); // The visible covers are asked for again.
        if (!app.jobs_->requested.contains(artRequestKey(app.cards_[app.selected_].game, CoverMessage::Kind::cover))) return 9629;
    }
    for (const size_t i : games) deliver(app.cards_[i].game, CoverMessage::Kind::cover, jpegFixture(600, 900, static_cast<uint32_t>(i)));
    app.paint();
    if (!app.renderer_.targetForTest() || !app.renderer_.coverReadyForTest(app.selected_)) return 9630;
    // A game in front covering the monitor hides the launcher too; a window beside it does not.
    app.game_sessions_.try_emplace("test-session", GetTickCount64());
    const HWND cover = CreateWindowExW(0, L"LaunchmeUiRegression", L"Game", WS_POPUP, 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN),
                                       nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    const HWND beside = CreateWindowExW(0, L"LaunchmeUiRegression", L"Beside", WS_POPUP, 0, 0, 400, 300, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    app.foreground_window_ = [&] { return cover; };
    const bool covered = app.anyActiveSession() && app.launcherHidden();
    app.foreground_window_ = [&] { return beside; };
    const bool alongside = app.launcherHidden();
    app.foreground_window_ = [window] { return window; };
    const bool front = app.launcherHidden();
    // Oszczędzaj zasoby za grą (opt-in, off by default): behind the game the launcher yields only with it.
    {
        app.foreground_window_ = [&] { return cover; };
        if (AppSettings{}.background_yield || app.settings_.background_yield) return 9646;
        app.background_since_ = GetTickCount64() - Launcher::background_delay_ms - 1;
        app.pollMemory();
        if (app.background_mode_) return 9647;
        app.settings_.background_yield = true;
        app.pollMemory(); app.background_since_ = GetTickCount64() - Launcher::background_delay_ms - 1; app.pollMemory();
        if (!app.background_mode_) return 9648;
        app.settings_.background_yield = false;
        app.pollMemory();
        if (app.background_mode_) return 9649;
        app.foreground_window_ = [window] { return window; };
    }
    DestroyWindow(cover); DestroyWindow(beside);
    app.game_sessions_.erase("test-session");
    if (!covered || alongside || front) return 9631;
    // Without Kompresja grafik a new picture is uploaded as BGRA (still at the size drawn).
    app.settings_.texture_compression = false; app.applyLowMemory();
    app.renderer_.setCover(app.selected_, shareImage(jpegFixture(600, 900, 4242)), {});
    app.paint();
    if (!app.renderer_.coverReadyForTest(app.selected_) || app.renderer_.coverCompressedForTest(app.selected_)) return 9643;
    // Switched off: nothing is given back.
    app.settings_.low_memory = false; app.applyLowMemory();
    ShowWindow(window, SW_SHOWMINNOACTIVE);
    app.memory_hidden_since_ = GetTickCount64() - Launcher::memory_release_delay_ms - 1;
    app.pollMemory(); app.pollMemory();
    ShowWindow(window, SW_RESTORE); ShowWindow(window, SW_HIDE);
    if (app.memory_released_) return 9632;
    return 0;
}
