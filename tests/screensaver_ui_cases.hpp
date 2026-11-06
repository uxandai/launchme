// Included after LauncherUiTest: the screensaver. Fixture artwork is written into the
// test's disk cache, the real worker scans and reads it, and its messages are pumped
// here. The clock of the show advances in fixed 50 ms frames, so snapshots are stable.
#include "../src/artwork/cover_cache.hpp"

#include <fstream>

namespace {
// A gradient with a bright disc and diagonal bands: pan, zoom, crop and spin show clearly.
std::vector<uint8_t> screensaverFixture(UINT width, UINT height, int variant, bool poster = false) {
    using Microsoft::WRL::ComPtr;
    static constexpr float palette[][6] = {{0.10f, 0.24f, 0.47f, 0.90f, 0.55f, 0.24f}, {0.35f, 0.08f, 0.27f, 0.98f, 0.78f, 0.35f},
                                           {0.04f, 0.35f, 0.31f, 0.78f, 0.90f, 0.98f}, {0.24f, 0.16f, 0.08f, 0.94f, 0.39f, 0.47f}};
    const auto& p = palette[variant % 4];
    std::vector<uint8_t> pixels(static_cast<size_t>(width) * height * 4);
    const float unit = static_cast<float>(std::min(width, height));
    for (UINT y = 0; y < height; ++y)
        for (UINT x = 0; x < width; ++x) {
            const float t = static_cast<float>(y) / height;
            float r = std::lerp(p[0], p[3], t), g = std::lerp(p[1], p[4], t), b = std::lerp(p[2], p[5], t);
            const float dx = (x - width * 0.62f) / unit, dy = (y - height * 0.38f) / unit;
            if (dx * dx + dy * dy < 0.05f) { r = std::min(1.0f, r + 0.35f); g = std::min(1.0f, g + 0.35f); b = std::min(1.0f, b + 0.3f); }
            if (((x + y) / static_cast<UINT>(unit / 9 + 1)) % 2) { r *= 0.86f; g *= 0.86f; b *= 0.86f; }
            if (poster && y > height * 0.07f && y < height * 0.16f && x > width * 0.1f && x < width * 0.9f) { r = g = b = 0.95f; }
            const size_t i = (static_cast<size_t>(y) * width + x) * 4;
            pixels[i] = static_cast<uint8_t>(b * 255); pixels[i + 1] = static_cast<uint8_t>(g * 255);
            pixels[i + 2] = static_cast<uint8_t>(r * 255); pixels[i + 3] = 255;
        }
    ComPtr<IWICImagingFactory> factory;
    ComPtr<IWICBitmap> bitmap;
    ComPtr<IStream> stream;
    ComPtr<IWICBitmapEncoder> encoder;
    ComPtr<IWICBitmapFrameEncode> frame;
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(factory.GetAddressOf()))) ||
        FAILED(factory->CreateBitmapFromMemory(width, height, format, width * 4, static_cast<UINT>(pixels.size()), pixels.data(), bitmap.GetAddressOf())) ||
        FAILED(CreateStreamOnHGlobal(nullptr, TRUE, stream.GetAddressOf())) ||
        FAILED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, encoder.GetAddressOf())) ||
        FAILED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache)) ||
        FAILED(encoder->CreateNewFrame(frame.GetAddressOf(), nullptr)) || FAILED(frame->Initialize(nullptr)) ||
        FAILED(frame->SetSize(width, height)) || FAILED(frame->SetPixelFormat(&format)) ||
        FAILED(frame->WriteSource(bitmap.Get(), nullptr)) || FAILED(frame->Commit()) || FAILED(encoder->Commit())) return {};
    STATSTG stats{};
    HGLOBAL memory = nullptr;
    if (FAILED(stream->Stat(&stats, STATFLAG_NONAME)) || FAILED(GetHGlobalFromStream(stream.Get(), &memory))) return {};
    const auto* data = static_cast<const uint8_t*>(GlobalLock(memory));
    if (!data) return {};
    std::vector<uint8_t> result(data, data + stats.cbSize.QuadPart);
    GlobalUnlock(memory);
    return result;
}
bool writeFixture(const std::filesystem::path& file, const std::vector<uint8_t>& bytes) {
    std::error_code error;
    std::filesystem::create_directories(file.parent_path(), error);
    std::ofstream output(file, std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return !bytes.empty() && static_cast<bool>(output);
}
LPARAM point(float x, float y) { return MAKELPARAM(static_cast<WORD>(static_cast<short>(x)), static_cast<WORD>(static_cast<short>(y))); }
} // namespace

int LauncherUiTest::screensaver(HWND window) {
    using namespace std::chrono_literals;
    Launcher app;
    app.hwnd_ = window; app.execute_ = fakeExecute;
    bool watched = true;
    app.foreground_ = [&](HWND) { return watched; };
    app.saver_seed_ = 20260929;
    if (!app.renderer_.initialize(window)) return 800;
    // Always painted (into the config folder unless LAUNCHME_UI_CAPTURE_DIR is set), so the
    // screensaver drawing code runs in every test run.
    const auto capture = [&](const wchar_t* name) {
        wchar_t folder[4096]{};
        const auto count = GetEnvironmentVariableW(L"LAUNCHME_UI_CAPTURE_DIR", folder, 4096);
        const auto target = count && count < 4096 ? std::filesystem::path(folder) : userConfigDirectory();
        app.notifications_.clear();
        return app.renderer_.captureForTest(target / name, [&] { app.paint(); });
    };
    auto library = std::make_unique<LibraryMessage>(); library->account = 1;
    const std::vector<std::wstring> names{L"Aurora Drift", L"Hollow Signal", L"Marble Coast", L"Northwind"};
    for (uint32_t i = 0; i < names.size(); ++i) {
        Game game{9100 + i, names[i]};
        game.last_played = 100 + i;
        library->games.push_back(std::move(game));
    }
    app.onLibrary(std::move(library));
    app.showCategory(0);
    // Cached artwork: heroes for three games, posters for two, covers for all four.
    const auto cache = coverCacheDirectory();
    const auto fixture = [&](int i) {
        const auto found = std::find_if(app.public_library_.begin(), app.public_library_.end(),
                                        [&](const Game& game) { return game.app_id == 9100u + static_cast<uint32_t>(i); });
        return found == app.public_library_.end() ? Game{} : *found;
    };
    for (int i = 0; i < 4; ++i) {
        const auto game = fixture(i);
        if (!game.app_id) return 801;
        if (i < 3 && !writeFixture(gameArtworkPath(game, ArtworkKind::hero, cache), screensaverFixture(1280, 720, i))) return 801;
        if (i < 2 && !writeFixture(gameArtworkPath(game, ArtworkKind::poster, cache), screensaverFixture(300, 450, i, true))) return 802;
        if (!writeFixture(gameArtworkPath(game, ArtworkKind::cover, cache), screensaverFixture(256, 256, i + 1))) return 803;
    }
    if (!cachedArtworkAvailable(fixture(1), ArtworkKind::poster, cache) || cachedArtworkAvailable(fixture(3), ArtworkKind::hero, cache) ||
        cachedArtworkAvailable(fixture(3), ArtworkKind::poster, cache) || cachedArtwork(fixture(3), ArtworkKind::cover, cache).empty() ||
        gameArtworkPath(fixture(0), ArtworkKind::poster, cache).parent_path().filename() != L"posters") return 804;
    const auto pump = [&] {
        MSG message{};
        while (PeekMessageW(&message, window, screensaver_message, screensaver_message, PM_REMOVE))
            app.onScreensaver(std::unique_ptr<ScreensaverMessage>(reinterpret_cast<ScreensaverMessage*>(message.lParam)));
    };
    // Waits for the worker without moving the show's clock.
    const auto wait = [&](const auto& done) {
        for (int i = 0; i < 2000 && !done(); ++i) {
            pump(); app.updateScreensaver(std::chrono::steady_clock::now(), 0);
            if (!done()) Sleep(2);
        }
        return done();
    };
    const auto advance = [&](float seconds) {
        for (int frame = 0; frame < static_cast<int>(std::lround(seconds / 0.05f)); ++frame) {
            pump(); app.updateScreensaver(std::chrono::steady_clock::now(), 0.05f);
        }
    };
    const auto shown = [&] { return app.saver_.current.shown_at >= 0; };
    const auto preloaded = [&] { return app.saver_.upcoming.loaded; };
    // Runs the show until the next game is current (9 s per game once it is preloaded).
    const auto next_item = [&] {
        if (!wait(preloaded)) return false;
        const auto before = app.saver_.current.key;
        for (int frame = 0; frame < 400 && app.saver_.current.key == before; ++frame) advance(0.05f);
        return app.saver_.current.key != before;
    };
    // A key press as the window procedure passes it: true = swallowed.
    const auto key = [&](WPARAM vk) { return app.screensaverInput(WM_KEYDOWN, vk, 0); };

    // Settings: the category replaced "Szuflady"; its sliders moved to Appearance.
    app.openSettings();
    app.settings_state_.selectCategory(static_cast<int>(settings_ui::Category::appearance));
    app.buildSettingsRows();
    if (!revealClassicSetting(app, settings_ui::Id::opacity) || !app.settings_state_.at(settings_ui::Page::backdrop)) return 805; // Wszystkie opcje wyglądu → "Tło i szkło".
    const auto slider = std::find_if(app.settings_rows_.begin(), app.settings_rows_.end(), [](const auto& row) { return row.id == settings_ui::Id::opacity; });
    if (slider == app.settings_rows_.end() || std::none_of(app.settings_rows_.begin(), app.settings_rows_.end(),
            [](const auto& row) { return row.id == settings_ui::Id::fade; }) || app.settings_rows_.size() < 2 ||
        std::next(slider, 1)->id != settings_ui::Id::fade) return 805;
    app.settings_state_.enter(static_cast<int>(app.settings_rows_.size()));
    app.settings_state_.selected[app.settings_state_.category] = static_cast<int>(slider - app.settings_rows_.begin());
    app.settingsSetLevel(0.5f);
    app.saveSettings();
    if (std::abs(app.settings_.drawer.opacity - 0.49f) > 0.001f || std::abs(loadAppSettings().drawer.opacity - 0.49f) > 0.001f) return 806;
    // System → "Wygaszacz ekranu": a tile whose page has four rows.
    const int saver_category = static_cast<int>(settings_ui::Category::system);
    app.settings_state_.selectCategory(saver_category);
    app.buildSettingsRows();
    using settings_ui::Id;
    if (!app.revealSetting(Id::screensaver) || !app.settings_state_.at(settings_ui::Page::screensaver) || app.settings_rows_.size() != 4) return 807;
    const auto saver_row = std::find_if(app.settings_rows_.begin(), app.settings_rows_.end(), [](const auto& row) { return row.id == Id::screensaver; });
    if (saver_row == app.settings_rows_.end() || app.settings_rows_.end() - saver_row < 4) return 807;
    const int base = static_cast<int>(saver_row - app.settings_rows_.begin());
    if (std::wstring(tr(settings_ui::categories[saver_category].title)) != L"System" || base != 0 ||
        app.settings_rows_[base].id != Id::screensaver || !app.settings_rows_[base].checked ||
        app.settings_rows_[base + 1].id != Id::screensaver_style || app.settings_rows_[base + 1].value != L"Tła gier" || app.settings_rows_[base + 1].choices.size() != 3 ||
        app.settings_rows_[base + 2].id != Id::screensaver_minutes || app.settings_rows_[base + 2].value != L"1 min" || app.settings_rows_[base + 2].choices.size() != 4 ||
        app.settings_rows_[base + 3].id != Id::screensaver_start || app.settings_rows_[base + 3].title != L"Uruchom teraz") return 807;
    app.settings_state_.enter(static_cast<int>(app.settings_rows_.size()));
    app.settings_state_.selected[saver_category] = base; app.ensureSettingVisible();
    if (!capture(L"settings-screensaver.png")) return 808;
    // "Po czasie" picker: 5 min; "Styl" picker: 3D boxes (no network: the runtime never started).
    app.settings_state_.selected[saver_category] = base + 2;
    app.settingsActivate(); app.settingsMove(2); app.settingsActivate();
    app.settings_state_.selected[saver_category] = base + 1;
    app.settingsActivate(); app.settingsMove(1); app.settingsActivate();
    app.saveSettings();
    const auto stored = loadAppSettings();
    if (app.settings_.screensaver_minutes != 5 || stored.screensaver_minutes != 5 ||
        stored.screensaver_style != AppSettings::ScreensaverStyle::boxes || app.settings_rows_[base + 2].value != L"5 min" ||
        app.settings_rows_[base + 1].value != L"Pudełka 3D") return 809;
    {
        std::lock_guard lock(app.jobs_->mutex);
        if (!app.jobs_->prefetch.empty()) return 810; // Poster prefetch waits for the running launcher.
    }
    // With the launcher running (no download workers exist in this test), boxes queue one background
    // poster job per library game (SteamGridDB's own spacing paces them); another style drops them again.
    app.runtime_started_ = true;
    app.prefetchPosters();
    {
        std::lock_guard lock(app.jobs_->mutex);
        const auto& lane = app.jobs_->prefetch;
        if (lane.size() != 4 || std::any_of(lane.begin(), lane.end(), [](const auto& job) { return job.kind != CoverMessage::Kind::poster || !job.prefetch; }) ||
            lane.front().ready_at != std::chrono::steady_clock::time_point{}) return 860;
    }
    app.prefetchPosters(); // Already queued: nothing twice.
    if (app.jobs_->prefetch.size() != 4) return 861;
    {
        // The background lane takes covers and posters (posters first for poster tiles) before
        // landscapes, backgrounds and logos, whatever order they were queued in.
        Launcher::JobQueue queue;
        const auto order = [&](bool posters_first) {
            queue.prefetch.clear(); queue.posters_first = posters_first;
            for (const auto kind : {CoverMessage::Kind::logo, CoverMessage::Kind::hero, CoverMessage::Kind::landscape, CoverMessage::Kind::poster, CoverMessage::Kind::cover}) {
                Launcher::Job job{app.public_library_[0], kind}; job.prefetch = true; queue.prefetch.push_back(job);
            }
            std::vector<CoverMessage::Kind> taken;
            while (auto job = queue.takeReady(true, false, std::chrono::steady_clock::now())) taken.push_back(job->kind);
            return taken;
        };
        using K = CoverMessage::Kind;
        if (order(false) != std::vector<K>{K::cover, K::poster, K::landscape, K::hero, K::logo} ||
            order(true) != std::vector<K>{K::poster, K::cover, K::landscape, K::hero, K::logo}) return 869;
    }
    app.settings_.screensaver_style = AppSettings::ScreensaverStyle::discs;
    app.prefetchPosters();
    if (!app.jobs_->prefetch.empty() || std::any_of(app.jobs_->requested.begin(), app.jobs_->requested.end(),
            [](const std::string& queued) { return queued.ends_with(":4"); })) return 862;
    app.runtime_started_ = false;
    app.settings_.screensaver_style = AppSettings::ScreensaverStyle::backgrounds;
    app.settings_.screensaver_minutes = 1;
    app.buildSettingsRows();

    // "Uruchom teraz" (Enter) starts it from Settings; Enter's release and character are not a wake.
    app.settings_state_.selected[saver_category] = base + 3;
    const auto settings_state = app.settings_state_;
    app.enterDown(false);
    if (!app.screensaverActive() || !app.settingsOpen() || !app.jobs_->quiet) return 811; // Downloads hold, posters aside.
    if (!app.screensaverInput(WM_CHAR, L'\r', 0) || !app.screensaverInput(WM_KEYUP, VK_RETURN, 0) ||
        !app.screensaverInput(WM_KEYDOWN, VK_RETURN, 1LL << 30) || !app.screensaverActive()) return 812;
    // Before any game is scanned the screen is simply dimmed; then the first game fades in.
    if (!wait([&] { return app.saver_.scanned; }) || app.saver_.playlist.size() != 3) return 813;
    advance(0.4f);
    if (!capture(L"screensaver-intro.png")) return 814;
    // Down arrow: ends it and is swallowed, like the main window procedure does.
    if (!key(VK_DOWN) || app.screensaverActive() || app.jobs_->quiet) return 815;
    if (!app.screensaverInput(WM_KEYDOWN, VK_DOWN, 1LL << 30) || !app.screensaverInput(WM_KEYUP, VK_DOWN, 0)) return 816;
    if (!app.settingsOpen() || app.settings_state_.category != settings_state.category || app.settings_state_.row() != base + 3 ||
        !app.settings_state_.options || app.settings_state_.editing) return 817;
    if (app.screensaverInput(WM_KEYDOWN, VK_DOWN, 0)) return 818; // The next press navigates again.
    // A right-click ends it without opening a menu on release; a mouse wheel ends it too.
    app.startScreensaver();
    if (!app.screensaverInput(WM_RBUTTONDOWN, MK_RBUTTON, point(20, 20)) || app.screensaverActive() ||
        !app.screensaverInput(WM_RBUTTONUP, 0, point(20, 20))) return 819;
    app.startScreensaver();
    if (!app.screensaverInput(WM_MOUSEWHEEL, MAKEWPARAM(0, static_cast<WORD>(-WHEEL_DELTA)), 0) || app.screensaverActive()) return 820;
    // Mouse: a small nudge stays, a real move (> 8 px) ends it.
    app.startScreensaver();
    const auto anchor = app.saver_gate_.anchor().value_or(std::pair{400.0f, 300.0f});
    if (!app.saver_gate_.anchor()) app.screensaverInput(WM_MOUSEMOVE, 0, point(anchor.first, anchor.second));
    if (!app.screensaverInput(WM_MOUSEMOVE, 0, point(anchor.first + 4, anchor.second + 3)) || !app.screensaverActive()) return 821;
    if (!app.screensaverInput(WM_MOUSEMOVE, 0, point(anchor.first + 30, anchor.second)) || app.screensaverActive()) return 822;
    app.settings_state_.closeAll(); // Off its page, then out of Settings.
    app.settings_state_.options = false;
    app.back();
    if (app.settingsOpen() || app.screen_ != ScreenMode::shelves) return 823;

    // Idle timer (injected last input): one minute, only while watched, no game, no text entry.
    const auto screen = app.screen_;
    const auto selected = app.selected_;
    const auto category = app.category_;
    app.last_input_ = std::chrono::steady_clock::now() - 59s;
    app.tick();
    if (app.screensaverActive()) return 824;
    app.game_sessions_.try_emplace("steam:9100", GetTickCount64());
    app.last_input_ = std::chrono::steady_clock::now() - 61s;
    app.tick();
    if (app.screensaverActive()) return 825;
    app.game_sessions_.clear();
    app.settings_.screensaver = false;
    app.tick();
    if (app.screensaverActive()) return 826;
    app.settings_.screensaver = true;
    watched = false;
    app.tick();
    if (app.screensaverActive()) return 827;
    watched = true; // Back in front: the idle period starts over.
    app.tick();
    if (app.screensaverActive() || std::chrono::steady_clock::now() - app.last_input_ > 5s) return 828;
    app.settings_.screensaver_minutes = 3;
    app.last_input_ = std::chrono::steady_clock::now() - 150s;
    app.tick();
    if (app.screensaverActive()) return 829;
    app.last_input_ = std::chrono::steady_clock::now() - 181s;
    app.tick();
    if (!app.screensaverActive() || app.saver_.style != AppSettings::ScreensaverStyle::backgrounds) return 830;

    // Game backgrounds: only the three games with a cached hero, shuffled, never twice in a row.
    if (!wait([&] { return app.saver_.scanned; }) || app.saver_.playlist.size() != 3 || app.saver_.games.contains("steam:9103")) return 831;
    if (!wait(shown) || !app.saver_.current.hero || app.saver_.current.title.empty()) return 832;
    advance(4.0f);
    if (!wait(preloaded) || app.saver_.upcoming.key == app.saver_.current.key) return 833;
    if (!capture(L"screensaver-backgrounds.png")) return 834;
    const auto first = app.saver_.current.key, second = app.saver_.upcoming.key;
    advance(5.6f); // 9 s per game, then a 1.2 s crossfade.
    const auto view = app.screensaverView();
    if (app.saver_.current.key != second || app.saver_.previous.key != first || view.previous.slot < 0 ||
        view.current.alpha < 0.3f || view.current.alpha > 0.7f || view.previous.title == view.current.title) return 835;
    if (!capture(L"screensaver-backgrounds-crossfade.png")) return 836;
    std::string last = app.saver_.current.key;
    for (int item = 0; item < 6; ++item) {
        if (!next_item()) return 837;
        if (app.saver_.current.key == last || app.saver_.current.key == app.saver_.upcoming.key) return 838;
        last = app.saver_.current.key;
    }
    // Losing the foreground ends it quietly; nothing of the screen beneath changed.
    watched = false;
    app.tick();
    if (app.screensaverActive() || app.screen_ != screen || app.selected_ != selected || app.category_ != category) return 839;
    watched = true;
    app.tick();

    // 3D boxes: posters where cached, a square cover otherwise.
    app.settings_.screensaver_style = AppSettings::ScreensaverStyle::boxes;
    app.startScreensaver();
    if (!wait([&] { return app.saver_.scanned; }) || app.saver_.playlist.size() != 4) return 840;
    if (!wait(shown)) return 841;
    const auto boxes = [&](bool poster) {
        for (int item = 0; item < 12 && app.saver_.current.poster != poster; ++item) if (!next_item()) return false;
        advance(2.0f); // Past the crossfade.
        return app.saver_.current.poster == poster;
    };
    if (!boxes(true)) return 842;
    advance(1.2f);
    if (!capture(L"screensaver-boxes.png")) return 843;
    const bool capture_perspective = app.renderer_.perspectiveBoxes();
    app.renderer_.flat_boxes_for_test = true;
    if (!capture(L"screensaver-boxes-flat.png") || app.renderer_.perspectiveBoxes()) return 844;
    app.renderer_.flat_boxes_for_test = false;
    // The real window (HWND render target) turns the box through ID2D1DeviceContext.
    InvalidateRect(window, nullptr, FALSE);
    app.paint();
    if (!app.renderer_.perspectiveBoxes()) return 845;
    std::cout << "Screensaver boxes: window perspective=1, capture target perspective=" << capture_perspective << '\n';
    if (!boxes(false)) return 846;
    if (!capture(L"screensaver-boxes-cover.png")) return 847;
    // Any key ends it from the home screen too, without moving the selection.
    if (!key(VK_RIGHT) || app.screensaverActive() || app.selected_ != selected || app.screen_ != screen) return 848;
    app.screensaverInput(WM_KEYUP, VK_RIGHT, 0);

    // Discs: the four games with a cached cover.
    app.settings_.screensaver_style = AppSettings::ScreensaverStyle::discs;
    app.startScreensaver();
    if (!wait([&] { return app.saver_.scanned; }) || app.saver_.playlist.size() != 4 || !wait(shown)) return 849;
    advance(2.5f);
    if (!capture(L"screensaver-discs.png")) return 850;
    if (!key(VK_SPACE) || app.screensaverActive()) return 851;
    app.screensaverInput(WM_KEYUP, VK_SPACE, 0);

    // Nothing qualifies (no hero on disk): a dim screen with a slow accent glow.
    for (int i = 0; i < 4; ++i) {
        std::error_code error;
        std::filesystem::remove(gameArtworkPath(fixture(i), ArtworkKind::hero, cache), error);
    }
    app.settings_.screensaver_style = AppSettings::ScreensaverStyle::backgrounds;
    app.startScreensaver();
    if (!wait([&] { return app.saver_.scanned; }) || !app.screensaverView().empty) return 852;
    advance(3.0f);
    if (!capture(L"screensaver-empty.png")) return 853;
    if (!key(VK_ESCAPE) || app.screensaverActive() || app.screen_ != screen) return 854;
    return 0;
}
