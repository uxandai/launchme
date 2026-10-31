#include "../src/app/launcher.hpp"
#include "../src/app/launcher_internal.hpp"
#include "../src/library/screenscraper_config.hpp"
#include "../src/ui/game_details_layout.hpp"
#include "../src/ui/context_menu.hpp"
#include "../src/ui/exit_prompt.hpp"
#include "../src/ui/xmb_layout.hpp"
#include "../src/ui/quick_menu.hpp"
#include "../src/ui/tour_view.hpp"
#include "../src/store/game_details.hpp"
#include "image_fixture.hpp"
#include "../src/ui/carousel.hpp"
#include "../src/platform/music_themes.hpp"
#include "../src/ui/grid_layout.hpp"
#include "../src/platform/frame_clock.hpp"
#include "../src/platform/steam_controller.hpp"
#include "../src/artwork/failure_cache.hpp"
#include "../src/platform/atomic_file.hpp"
#include "../src/platform/memory_usage.hpp"
#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#include <miniz.h>
#include <fstream>
#include <iostream>
#include <numeric>
#include <psapi.h>

namespace {
std::vector<std::wstring> dispatched;
class FakeDevices : public DeviceService {
public:
    devices::Snapshot data;
    std::vector<devices::Command> actions;
    void open(devices::Panel) override {}
    void close() override { data.busy = false; }
    void submit(devices::Command command) override { actions.push_back(std::move(command)); }
    devices::Snapshot snapshot() override { return data; }
};
class FakeGog : public gog::Service {
public:
    std::optional<gog::Snapshot> result;
    std::vector<gog::Command> commands;
    void submit(gog::Command command, std::wstring = {}) override { commands.push_back(command); result.reset(); }
    std::optional<gog::Snapshot> take() override { auto value = std::move(result); result.reset(); return value; }
    std::optional<gog::InstallSnapshot> installation;
    std::vector<gog::InstallRequest> installs;
    int cancellations = 0;
    bool install(gog::InstallRequest request) override { installs.push_back(std::move(request)); return true; }
    void cancelInstall() override { ++cancellations; }
    std::optional<gog::InstallSnapshot> takeInstall() override { auto value = std::move(installation); installation.reset(); return value; }
};
INT_PTR fakeExecute(HWND, const GameAction& action) {
    dispatched.push_back(action.target);
    return 33;
}
// Benchmarks: one line of this process's memory (private, working set, GPU dedicated and shared).
std::string memoryReport(const char* label) {
    const auto usage = currentMemoryUsage();
    const auto mb = [](size_t bytes) { return std::to_string(static_cast<int>(bytes / 1048576.0 + 0.5)); };
    return std::string("memory ") + label + ": private=" + mb(usage.private_bytes) + " MB; working set=" + mb(usage.working_set) +
        " MB; GPU local=" + mb(usage.gpu_local) + " MB; GPU shared=" + mb(usage.gpu_shared) + " MB\n";
}
std::vector<uint8_t> pixel() {
    std::vector<uint8_t> bmp(58, 0);
    bmp[0] = 'B'; bmp[1] = 'M'; bmp[2] = 58; bmp[10] = 54; bmp[14] = 40;
    bmp[18] = bmp[22] = bmp[26] = 1; bmp[28] = 32;
    bmp[54] = bmp[55] = bmp[56] = bmp[57] = 255;
    return bmp;
}
}

class LauncherUiTest {
public:
    static bool waitPlugins(Launcher& app) {
        const auto end = GetTickCount64() + 5000;
        do {
            MSG msg{};
            while (PeekMessageW(&msg, app.hwnd_, plugins_message, plugins_message, PM_REMOVE))
                app.onPlugins(std::unique_ptr<PluginsMessage>(reinterpret_cast<PluginsMessage*>(msg.lParam)));
            if (!app.plugins_busy_ && !app.plugin_command_pending_) return true;
            Sleep(5);
        } while (GetTickCount64() < end);
        return false;
    }
#if __has_include("local_ui_members.hpp") // Cases for extras built only on this machine (local/).
#include "local_ui_members.hpp"
#endif
    static int pluginLibrarySupplements(HWND window);
    static int settingsDesign(HWND window);
    static int settingsDevices(HWND window);
    static int pcSettings(HWND window);
    static int pcRefresh(HWND window);
    static int drmFree(HWND window);
    static int xmb(HWND window);
    static int quickResume(HWND window);
    static int exitTransition(HWND window);
    static int screenTransitions(HWND window);
    static int quickResumeFailures(HWND window);
    static int runningCandidates(HWND window);
    static int gameOverlay(HWND window);
    static int gameFocus(HWND window);
    static int trayAndBackgroundCrop(HWND window); // tray_ui_cases.hpp
    static int recentSessions(HWND window);
    static int onscreenKeyboard(HWND window);
    static int battlenet(HWND window);
    static int ubisoft(HWND window);
    static int webImports(HWND window);
    static int epicAccount(HWND window);
    static int power(HWND window);
    static int modalLayers(HWND window);
    static int language(HWND window);
    static int customGames(HWND window);
    static int manualGames(HWND window); // manual_games_ui_cases.hpp
    static int fileBrowserAndColors(HWND window);
    static int dashboard(HWND window);
    static int screensaver(HWND window);
    static int controllerFamilies(HWND window);
    static int controllerConnections(HWND window);
    static int headphoneConnections(HWND window);
    static int startupAudio(HWND window);
    static int libraryViews(HWND window);
    static int artworkScheduling(HWND window);
    static int libraryStateRecovery(HWND window);
    static int flix(HWND window);
    static int metro(HWND window);
    static int psfive(HWND window);
    static int snitch(HWND window);
    static int glanceHub(HWND window, bool live = false);
    static int tarotReading(HWND window);
    static int profilePage(HWND window);
    static int aureliaIntegration(HWND window);
    static int amazonGames(HWND window);
    static int xboxAccount(HWND window);
    static int studioLibraries(HWND window);
    static int updates(HWND window);
    static int gameLengths(HWND window);
    static int companionChat(HWND window);
    static int companionSocialDynamics(HWND window);
    static int companionSemanticMemory(HWND window);
    static int companionAppearance(HWND window);
    static int companionReplyLength(HWND window);
    static int companionLinks(HWND window);
    static int companionPaste(HWND window);
    static int companionRemote(HWND window);
    static int gameKnowledge(HWND window);
    static int homeWidgets(HWND window);
    static int uiRefresh(HWND window);
    static int desktop(HWND window);
    static int categoryRail(HWND window);
    static int boundaryAndShapes(HWND window);
    static int focusMotion(HWND window);
    static int rawgFlow(HWND window);
    static int privateGames(HWND window);
    static int steamVisibility(HWND window);
    static int bulkCovers(HWND window);
    static int tour(HWND window);
    static int launchAnimation(HWND window);
    static int organization(HWND window);
    static int excludedGames(HWND window);
    static int uninstallGames(HWND window);
    static int hltbDetails(HWND window);
    static int pluginAchievements(HWND window);
    static int lowMemory(HWND window); // low_memory_ui_cases.hpp
    static int profileDesktop(HWND window, int seconds); // LAUNCHME_PERF_DESKTOP=1 (desktop_perf_case.hpp).
    static int profile(HWND window, int seconds) {
        const auto enabled = [](const wchar_t* name) {
            wchar_t value[4]{};
            return GetEnvironmentVariableW(name, value, 4) == 1 && value[0] == L'1';
        };
        if (enabled(L"LAUNCHME_PERF_DESKTOP")) return profileDesktop(window, seconds);
        const bool uncapped = enabled(L"LAUNCHME_PERF_UNCAPPED");
        const bool profile_xmb = enabled(L"LAUNCHME_PERF_XMB");
        if (uncapped) SetEnvironmentVariableW(L"LAUNCHME_PRESENT_IMMEDIATE", L"1");
        Launcher app;
        app.hwnd_ = window; app.execute_ = fakeExecute;
        app.foreground_ = [](HWND) { return true; }; // Animate deterministically even if Windows denies focus.
        if (profile_xmb) {
            app.settings_.home_layout = AppSettings::HomeLayout::xmb;
            app.settings_.xmb_posters = true;
            app.settings_.xmb_size = AppSettings::XmbSize::large;
        }
        if (!app.renderer_.initialize(window)) return 1;
        app.renderer_.startDecoderForTest(); // Off-thread decoding, as in the app.
        app.settings_.low_memory = !enabled(L"LAUNCHME_PERF_FULL_RAM"); // The default (low memory), or as before.
        // Kompresja grafik (BC1) as the app has it (on), or LAUNCHME_PERF_BC1=0/1.
        if (wchar_t bc1[4]{}; GetEnvironmentVariableW(L"LAUNCHME_PERF_BC1", bc1, 4) == 1 && (bc1[0] == L'0' || bc1[0] == L'1'))
            app.settings_.texture_compression = bc1[0] == L'1';
        app.applyLowMemory();
        app.initializeLibraryPreferences();
        app.profiler_.configure();
        if (!app.profiler_.overlay()) app.profiler_.toggle();
        app.renderer_.profile(true);
        app.controller_input_.initialize(window);
        app.real_pads_ = true; // Profiled with the machine's own pad.
        auto library = std::make_unique<LibraryMessage>(); library->account = 1;
        // LAUNCHME_PERF_GAMES: library size (default 80); LAUNCHME_PERF_LIBRARY=1: the "all games" grid.
        wchar_t games_value[16]{};
        const uint32_t game_count = GetEnvironmentVariableW(L"LAUNCHME_PERF_GAMES", games_value, 16) ?
            static_cast<uint32_t>(std::clamp(_wtoi(games_value), 8, 20000)) : 80u;
        wchar_t library_scene[4]{};
        const bool profile_library = GetEnvironmentVariableW(L"LAUNCHME_PERF_LIBRARY", library_scene, 4) == 1 && library_scene[0] == L'1';
        for (uint32_t i = 0; i < game_count; ++i) { Game game{1000 + i, L"Performance fixture"}; game.last_played = 100; library->games.push_back(game); }
        wchar_t install_animation[4]{};
        if (GetEnvironmentVariableW(L"LAUNCHME_PERF_INSTALL", install_animation, 4) == 1 && install_animation[0] == L'1') {
            library->games[0].installed = false;
            library->games[0].installation = {InstallPhase::downloading, 37, 100};
        }
        app.onLibrary(std::move(library));
        wchar_t battle_scene[4]{};
        const bool profile_battle = GetEnvironmentVariableW(L"LAUNCHME_PERF_BATTLENET", battle_scene, 4) == 1 && battle_scene[0] == L'1';
        if (profile_battle) {
            app.settings_.enabled_pc_sources.insert("battlenet");
            Game game; game.source = GameSource::battlenet; game.kind = GameKind::store;
            game.source_id = "WTCG"; game.name = L"Battle.net installation fixture"; game.installed = false;
            game.installation.phase = InstallPhase::staging;
            configureStoreActions(game, L"C:\\fixture\\Battle.net.exe", L"C:\\fixture\\game");
            auto pc = std::make_unique<PcLibraryMessage>(); pc->generation = app.pc_token_->load();
            pc->library.visible.push_back(game); app.onPcLibrary(std::move(pc)); app.showCategory(0);
        }
        const auto image = [](const wchar_t* variable) {
            wchar_t file[4096]{};
            if (!GetEnvironmentVariableW(variable, file, 4096)) return pngFixture();
            std::ifstream input(file, std::ios::binary);
            return std::vector<uint8_t>(std::istreambuf_iterator<char>{input}, {});
        };
        const auto cover = image(L"LAUNCHME_PERF_COVER_FILE"), hero = image(L"LAUNCHME_PERF_HERO_FILE");
        if (cover.empty() || hero.empty()) return 2;
        // Artwork arrives as in the app: one message (own bytes) per game and kind.
        const auto deliver = [&](const Game& game, CoverMessage::Kind kind, const std::vector<uint8_t>& bytes) {
            auto message = std::make_unique<CoverMessage>();
            message->key = gameKey(game); message->generation = app.jobs_->generation.load();
            message->kind = kind; message->bytes = bytes;
            app.onCover(std::move(message));
        };
        for (size_t i = 0; i < app.cards_.size(); ++i) {
            const auto& game = app.cards_[i].game;
            if (!isGame(game)) continue;
            deliver(game, CoverMessage::Kind::cover, cover); deliver(game, CoverMessage::Kind::hero, hero);
            if (profile_xmb) deliver(game, CoverMessage::Kind::poster, cover);
            app.renderer_.setHero(i, app.art_.at(gameKey(game)).hero); // Every row hero decoded up front.
        }
        if (profile_library) {
            for (size_t i = 0; i < 64 && i < app.public_library_.size(); ++i) deliver(app.public_library_[i], CoverMessage::Kind::cover, cover);
            app.openLibrary();
        }
        wchar_t drawer_scene[4]{};
        if (GetEnvironmentVariableW(L"LAUNCHME_PERF_DRAWER", drawer_scene, 4) == 1 && drawer_scene[0] == L'1') {
            // Right drawer dashboard (clock, tiles) over the shelf; sensors/Discord are fixtures.
            app.read_sensors_ = [] { return sensors::Reading{true, 48, 61}; };
            app.discord_.running = [] { return false; };
            app.tabDown(false); app.tabUp();
        }
        const auto private_mb = [] {
            PROCESS_MEMORY_COUNTERS_EX memory{}; memory.cb = sizeof(memory);
            return K32GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory), sizeof(memory)) ?
                memory.PrivateUsage / (1024.0 * 1024.0) : 0.0;
        };
        wchar_t audio_scene[4]{};
        const bool profile_audio = GetEnvironmentVariableW(L"LAUNCHME_PERF_AUDIO", audio_scene, 4) == 1 && audio_scene[0] == L'1';
        if (profile_audio) {
            auto backend = std::make_unique<FakeDevices>();
            backend->data.audio[0] = {0.4f, true, false, L"out"}; backend->data.audio[1] = {0.7f, true, false, L"in"};
            for (bool input : {false, true}) for (int i = 0; i < 20; ++i)
                backend->data.endpoints.push_back({std::wstring(input ? L"in" : L"out") + std::to_wstring(i), L"Audio endpoint fixture " + std::to_wstring(i), input, i == 0});
            app.device_service_ = std::move(backend); app.openDevices(devices::Panel::audio); app.pollDevices();
            app.showNotification(NotificationKind::controller, L"Kontroler połączony", L"DualSense");
        }
        wchar_t gog_scene[4]{};
        const bool profile_gog = GetEnvironmentVariableW(L"LAUNCHME_PERF_GOG_INSTALL", gog_scene, 4) == 1 && gog_scene[0] == L'1';
        if (profile_gog) {
            app.gog_install_ui_.open = true;
            app.gog_install_ui_.game.name = L"GOG installation fixture";
            app.gog_install_ui_.snapshot.state = gog::InstallState::installing;
            app.gog_install_ui_.snapshot.choices = {{"windows_en", L"English", L"1.0", 1000000000}};
            app.gog_install_ui_.row = 4;
        }
        MONITORINFOEXW monitor{}; monitor.cbSize = sizeof(monitor);
        DEVMODEW mode{}; mode.dmSize = sizeof(mode);
        if (GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor) &&
            EnumDisplaySettingsW(monitor.szDevice, ENUM_CURRENT_SETTINGS, &mode)) app.refresh_hz_ = static_cast<int>(mode.dmDisplayFrequency);
        RECT bounds{}; GetClientRect(window, &bounds);
        std::cout << "Viewport=" << bounds.right << 'x' << bounds.bottom << "; display Hz=" << app.refresh_hz_ << '\n';
        std::cout << "Pacing=" << (uncapped ? "uncapped / immediate present" : "display frame clock")
                  << "; scene=" << (profile_xmb ? "XMB large posters" : profile_library ? "library grid" : "shelf") << '\n';
        ShowWindow(window, SW_SHOW); UpdateWindow(window);
        FrameClock frame_clock;
        const auto start = GetTickCount64(); auto next_navigation = start + 1000;
        using Clock = std::chrono::steady_clock;
        const auto measured_start = Clock::now() + std::chrono::seconds(2);
        Clock::time_point last_paint{};
        std::vector<double> measured_frames;
        measured_frames.reserve(static_cast<size_t>(seconds) * 2000);
        const auto paint = [&] {
            app.paint();
            const auto now = Clock::now();
            if (last_paint >= measured_start)
                measured_frames.push_back(std::chrono::duration<double, std::milli>(now - last_paint).count());
            last_paint = now;
        };
        double navigate_total = 0, navigate_max = 0; int navigations = 0;
        const double setup_mb = private_mb();
        while (GetTickCount64() - start < static_cast<ULONGLONG>(seconds) * 1000) {
            MSG message{};
            while (PeekMessageW(&message, window, 0, 0, PM_REMOVE)) {
                if (message.message == WM_PAINT) {
                    if (uncapped) ValidateRect(window, nullptr); // One paint below per unlimited iteration.
                    else paint();
                }
                else { TranslateMessage(&message); DispatchMessageW(&message); }
            }
            // Benchmark the foreground cadence deterministically, even when
            // Windows denies focus to a process started by the automation host.
            frame_clock.update(window, true);
            if (uncapped || frame_clock.ready()) {
                if (!uncapped) frame_clock.consume();
                app.setFrameRate(uncapped ? 0 : frame_clock.targetHz(), frame_clock.refreshHz());
                app.tick();
                if (uncapped || GetUpdateRect(window, nullptr, FALSE)) paint();
            }
            if (GetTickCount64() >= next_navigation) {
                if (profile_audio) { app.deviceColumn(app.device_state_.audio_column ? -1 : 1); app.deviceMove(1); app.showVolume(0.64f, false); }
                else {
                    const auto began = Clock::now();
                    app.navigate(app.selected_ >= 7 ? -7 : 1, false);
                    const double spent = std::chrono::duration<double, std::milli>(Clock::now() - began).count();
                    navigate_total += spent; navigate_max = std::max(navigate_max, spent); ++navigations;
                }
                next_navigation += 1000;
            }
            if (!uncapped) frame_clock.wait();
        }
        KillTimer(window, 1);
        if (!measured_frames.empty()) {
            const double mean = std::accumulate(measured_frames.begin(), measured_frames.end(), 0.0) / measured_frames.size();
            std::sort(measured_frames.begin(), measured_frames.end());
            std::cout << "Measured (2 s warmup excluded): frames=" << measured_frames.size() << "; FPS=" << 1000.0 / mean
                      << "; mean=" << mean << " ms; p95=" << measured_frames[static_cast<size_t>((measured_frames.size() - 1) * 0.95)]
                      << " ms; p99=" << measured_frames[static_cast<size_t>((measured_frames.size() - 1) * 0.99)] << " ms\n";
        }
        const auto stats = app.profiler_.summary();
        std::cout << "Recent FPS=" << stats.fps << "; frame p95=" << stats.frame_p95 << "; p99=" << stats.frame_p99
                  << "; update avg=" << stats.tick_ms << "; update max=" << stats.tick_max << "; input=" << stats.input_ms
                  << "; system=" << stats.system_ms << "; draw=" << stats.draw_ms << "; EndDraw=" << stats.present_ms
                  << "; bitmap=" << stats.upload_ms << " ms\n";
        const double frames_mb = private_mb();
        std::cout << memoryReport("after frames");
        const auto& decoder = app.renderer_.decoderForTest();
        const auto textures = app.renderer_.textureStats();
        std::cout << "decoded images=" << decoder.completed() << "; decode total=" << decoder.decodeMilliseconds() << " ms; card bitmaps="
                  << textures.bitmaps << " (" << textures.bytes / 1048576.0 << " MB); art_ RAM=" << app.art_.bytes() / 1048576.0 << " MB\n";
        if (profile_library) {
            // Scroll the whole grid row by row; every cover the view asks for "arrives from disk"
            // (a CoverMessage with its own bytes), as the artwork worker would deliver it.
            const auto view = app.renderer_.viewport();
            const auto columns = static_cast<size_t>(grid_layout::columns(view.width));
            size_t delivered = 0, peak_bitmaps = 0;
            const auto began = Clock::now();
            for (size_t index = 0; index < app.cards_.size(); index += columns) {
                app.focusCard(index);
                const auto [first, end] = grid_layout::visibleRange(app.cards_.size(), view.width, view.height, app.grid_scroll_);
                for (size_t i = first; i < end; ++i)
                    if (!app.renderer_.hasCover(i)) { deliver(app.cards_[i].game, CoverMessage::Kind::cover, cover); ++delivered; }
                app.paint(); // Uploads finished decodes, as the frame loop would.
                peak_bitmaps = std::max(peak_bitmaps, app.renderer_.textureStats().bitmaps);
            }
            const double scroll_ms = std::chrono::duration<double, std::milli>(Clock::now() - began).count();
            for (int i = 0; i < 500 && app.renderer_.decoderForTest().pending(); ++i) Sleep(10);
            app.paint();
            const auto after = app.renderer_.textureStats();
            std::cout << "grid scroll: " << app.cards_.size() / columns << " rows in " << scroll_ms << " ms; covers delivered=" << delivered
                      << "; card bitmaps now=" << after.bitmaps << " (" << after.bytes / 1048576.0 << " MB), peak=" << peak_bitmaps
                      << "; art_ RAM=" << app.art_.bytes() / 1048576.0 << " MB in " << app.art_.size() << " entries; private MB=" << private_mb() << '\n';
            std::cout << memoryReport("after grid scroll");
        }
        // Card rebuild (category switch / screen change) outside the measured frames: CPU only, no paint.
        double rebuild_total = 0;
        constexpr int rebuilds = 20;
        for (int i = 0; i < rebuilds; ++i) {
            const auto began = Clock::now();
            app.rebuildCards();
            rebuild_total += std::chrono::duration<double, std::milli>(Clock::now() - began).count();
        }
        std::cout << "navigate avg=" << (navigations ? navigate_total / navigations : 0) << "; navigate max=" << navigate_max
                  << "; rebuildCards avg=" << rebuild_total / rebuilds << " ms; cards=" << app.cards_.size()
                  << "; private MB setup=" << setup_mb << "; after frames=" << frames_mb << "; after rebuilds=" << private_mb() << '\n';
        std::cout << memoryReport("after rebuilds");
        if (app.settings_.low_memory) {
            // A game in front of the launcher (memory.cpp gives everything back), then the launcher
            // in front again: two seconds of frames with the artwork arriving as in the app.
            app.releaseMemory();
            Sleep(300);
            std::cout << memoryReport("behind a game") << std::flush;
            app.restoreMemory();
            const auto back = GetTickCount64();
            std::unordered_set<std::string> served; // As the disk lane: each requested image arrives once.
            while (GetTickCount64() - back < 2000) {
                for (size_t i = 0; i < app.cards_.size(); ++i) {
                    const auto& game = app.cards_[i].game;
                    if (!isGame(game)) continue;
                    for (const auto kind : {CoverMessage::Kind::cover, CoverMessage::Kind::hero}) {
                        const auto key = artRequestKey(game, kind);
                        bool wanted = false;
                        { std::lock_guard lock(app.jobs_->mutex); wanted = app.jobs_->requested.contains(key); }
                        if (wanted && served.insert(key).second) deliver(game, kind, kind == CoverMessage::Kind::cover ? cover : hero);
                    }
                }
                MSG message{};
                while (PeekMessageW(&message, window, 0, 0, PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
                app.tick(); app.paint(); Sleep(16);
            }
            std::cout << memoryReport("back in front");
        }
        return 0;
    }
    static int run(HWND window) {
        Launcher app;
        app.settings_.launch_animation = false; // This flow tests immediate navigation; the launch animation has its own case.
        app.hwnd_ = window;
        app.execute_ = fakeExecute; // This test can never start a game/client or install anything.
        if (!app.renderer_.initialize(window)) return 1;
        app.onLibraryWorkerError(std::make_unique<LibraryWorkerErrorMessage>());
        if (app.scanning_ || app.notifications_.empty()) return 1500;
        app.scanning_ = true;
        auto artwork_error = std::make_unique<LibraryWorkerErrorMessage>(); artwork_error->artwork = true;
        app.onLibraryWorkerError(std::move(artwork_error));
        if (!app.scanning_ || app.notifications_.size() != 2) return 1501;
        app.notifications_.clear();
        // Errors: a longer life, not queued twice, and kept while plain toasts overflow the queue.
        app.showNotification(NotificationKind::error, L"E", L"x");
        app.showNotification(NotificationKind::error, L"E", L"x");
        for (int i = 0; i < 3; ++i) app.showNotification(NotificationKind::launch, L"N" + std::to_wstring(i), L"");
        if (app.notifications_.size() != 3 || app.notifications_.front().kind != NotificationKind::error ||
            app.notifications_.back().title != L"N2" ||
            notificationLifetime(NotificationKind::error) <= notificationLifetime(NotificationKind::launch)) return 5101;
        app.notifications_.clear();
        const auto capture = [&](const wchar_t* name) {
            wchar_t folder[4096]{};
            const auto count = GetEnvironmentVariableW(L"LAUNCHME_UI_CAPTURE_DIR", folder, 4096);
            if (!count || count >= 4096) return true;
            InvalidateRect(window, nullptr, FALSE);
            return app.renderer_.captureForTest(std::filesystem::path(folder) / name, [&] { app.paint(); });
        };
        auto library = std::make_unique<LibraryMessage>();
        library->account = 1;
        for (uint32_t i = 0; i < 80; ++i) {
            Game game{1000 + i, L"Synthetic game " + std::to_wstring(i)};
            game.last_played = 100;
            library->games.push_back(std::move(game));
        }
        for (int i = 0; i < 25; ++i) {
            SteamCollection collection{L"Synthetic collection " + std::to_wstring(i), {1000}};
            collection.id = "collection-" + std::to_string(i);
            library->collections.push_back(std::move(collection));
        }
        app.onLibrary(std::move(library));
        // Optional native snapshots of the All games background at two animation
        // phases and on return to a game. No real artwork/network is needed.
        wchar_t backdrop_folder[4096]{};
        if (GetEnvironmentVariableW(L"LAUNCHME_UI_CAPTURE_DIR", backdrop_folder, 4096)) {
            app.focusCard(app.cards_.size() - 1);
            for (int frame = 0; frame < 35; ++frame) { app.last_frame_ -= std::chrono::milliseconds(50); app.tick(); }
            app.ambient_time_ = 0; app.detail_ = 1;
            if (!capture(L"all-games-backdrop-0.png")) return 1950;
            app.ambient_time_ = 6;
            if (!capture(L"all-games-backdrop-6.png")) return 1951;
            app.focusCard(0);
            for (int frame = 0; frame < 35; ++frame) { app.last_frame_ -= std::chrono::milliseconds(50); app.tick(); }
            if (!capture(L"all-games-backdrop-exit.png")) return 1952;
            // Error toast: red badge, longer life, a long path wrapped at its folders.
            app.notifications_.clear();
            app.showNotification(NotificationKind::error, L"Nie można otworzyć folderu",
                L"C:\\Users\\Player\\Games\\A Very Long Studio Name\\A Game With A Rather Long Title\\Binaries\\Win64");
            app.notifications_.back().age = 1;
            if (!capture(L"error-toast.png")) return 5102;
            app.notifications_.clear();
            // Search without a match says so under the field.
            app.openSearch(); app.search_query_ = L"zzzz-no-such-game"; app.updateSearch();
            if (!app.cards_.empty() || !capture(L"search-no-results.png")) return 5103;
            app.back();
        }
        app.openLibrary();
        app.onLibraryWorkerError(std::make_unique<LibraryWorkerErrorMessage>());
        if (app.scanning_ || app.installed_library_.size() != 80 || app.cards_.size() != 80) return 1502;
        app.notifications_.clear();
        app.navigate(63, false);
        auto cover = std::make_unique<CoverMessage>();
        cover->key = gameKey(app.cards_[63].game);
        cover->generation = app.jobs_->generation.load();
        cover->bytes = pixel();
        app.onCover(std::move(cover));
        if (!app.renderer_.hasCover(63)) return 2;
        app.openSettings();
        app.back();
        app.openLibrary();
        if (app.renderer_.hasCover(63)) return 3;
        app.navigate(63, false);
        if (!app.renderer_.hasCover(63)) return 4; // Rebind from RAM after resource reset, without another download.

        app.openSettings();
        if (!capture(L"settings-native-appearance.png")) return 90;
        app.settingsActivate(); // Enter right pane, not change a value.
        // Czcionka belongs to the "Wygląd" section of Interfejs, opened by the interface accent.
        const auto font_row = std::find_if(app.settings_rows_.begin(), app.settings_rows_.end(),
                                           [](const auto& row) { return row.id == settings_ui::Id::font; });
        if (font_row == app.settings_rows_.end() || font_row == app.settings_rows_.begin() || app.settings_rows_.front().section.empty()) return 6;
        {
            auto heading = font_row;
            while (heading != app.settings_rows_.begin() && heading->section.empty()) --heading;
            if (heading->id != settings_ui::Id::ui_accent || heading->section != tr(L"Wygląd")) return 6;
        }
        app.settings_state_.selected[app.settings_state_.category] = static_cast<int>(font_row - app.settings_rows_.begin());
        app.ensureSettingVisible();
        if (!capture(L"settings-native-sections.png")) return 6;
        app.settingsActivate(); // Open font picker.
        if (!capture(L"settings-native-font.png")) return 91;
        app.settingsMove(3);
        app.back(); // Cancel must retain SST.
        if (app.settings_.font_family != L"SST") return 5;
        app.settingsActivate();
        app.settingsMove(3);
        app.settingsActivate();
        app.saveSettings();
        if (loadAppSettings().font_family != L"Arial") return 6;
        // Biblioteka → Kolekcje: the collections' own page.
        app.settings_state_.selectCategory(static_cast<int>(settings_ui::Category::library));
        app.buildSettingsRows();
        app.settings_.collection_order.push_back("2:other-account");
        {
            const auto open = std::find_if(app.settings_rows_.begin(), app.settings_rows_.end(),
                                           [](const auto& row) { return row.id == settings_ui::Id::collections_open; });
            if (open == app.settings_rows_.end()) return 7001;
            app.settingsActivate();
            app.settings_state_.selected[app.settings_state_.category] = static_cast<int>(open - app.settings_rows_.begin());
            app.settingsActivate();
            // "Zarządzaj kolekcjami" leads the page; the collections follow it.
            if (!app.settings_state_.at(settings_ui::Page::collections) || app.settings_state_.row() != 0 ||
                app.settings_rows_.front().id != settings_ui::Id::collection_manage || app.settings_rows_[1].id != settings_ui::Id::collection) return 7002;
        }
        app.settingsMove(24);
        const auto selected_collection_key = collectionKey(app.account_, app.settings_collections_[app.settings_state_.row() - 1]);
        if (!capture(L"settings-native-collections.png")) return 92;
        if (app.settings_state_.offset() <= 0) return 7003;
        app.settingsAdjust(1);
        if (app.settings_state_.subcontrol != 1 || app.settings_state_.row() != 24) return 47;
        app.settingsActivate();
        if (app.settings_state_.row() != 23 ||
            std::find(app.settings_.collection_order.begin(), app.settings_.collection_order.end(), "2:other-account") == app.settings_.collection_order.end()) return 8;
        app.settingsAdjust(1);
        if (app.settings_state_.subcontrol != 2) return 48;
        app.settingsActivate();
        if (app.settings_state_.row() != 24) return 49;
        app.settingsAdjust(-1); app.settingsActivate();
        app.settingsAdjust(-1); app.settingsActivate();
        if (!app.settings_.hidden_collections.contains(selected_collection_key)) return 9;
        // Steam ROM Manager collections: while Emulacja lists their games by platform they are no
        // home row, and their settings row stays off and locked; with Emulacja off they are ordinary.
        {
            Game srm{0x90000002u, L"SRM fixture"};
            srm.kind = GameKind::nonsteam; srm.platform = L"PS1";
            app.shortcut_library_.push_back(srm);
            app.collections_source_.push_back(SteamCollection{L"Sony PlayStation", {0x90000002u}, "user-collections.srm-U29ueSBQbGF5U3RhdGlvbg=="});
            const auto key = collectionKey(app.account_, app.collections_source_.back());
            const auto row = [&] {
                for (size_t i = 0; i < app.settings_rows_.size(); ++i) if (app.settings_rows_[i].title == L"Sony PlayStation") return static_cast<int>(i);
                return -1;
            };
            const auto shelf = [&] {
                return std::any_of(app.categories_.begin(), app.categories_.end(), [](const auto& item) { return item.title == L"Sony PlayStation"; });
            };
            app.settings_.show_emulation = true; app.buildShelves();
            int index = row();
            if (shelf() || index < 0 || !app.settings_rows_[index].locked || app.settings_rows_[index].checked) return 240;
            app.settings_state_.selected[app.settings_state_.category] = index; app.settings_state_.subcontrol = 0;
            app.settingsActivate();
            if (app.settings_.hidden_collections.contains(key) || !app.settings_.show_emulation) return 241;
            app.settings_.show_emulation = false; app.buildShelves();
            index = row();
            if (!shelf() || index < 0 || app.settings_rows_[index].locked || !app.settings_rows_[index].checked) return 242;
            app.settings_.show_emulation = true;
            app.collections_source_.pop_back(); app.shortcut_library_.pop_back();
            app.buildShelves();
            if (row() >= 0) return 243;
        }
        // An own collection: its trash after ↑ ↓ (a Steam collection has none), red with the focus, and a
        // question first with Anuluj focused; hiding it on the home screen is the gentler answer.
        // The desktop window's settings scrolled to a row, as Settings opened at a row does (desktop_settings.cpp).
        const auto desktopScroll = [&](int row) {
            const auto L = app.desktopSettingsLayout();
            app.desktop_sheet_scroll_ = app.desktop_sheet_scroll_target_ = std::clamp(L.tops[static_cast<size_t>(row)] - 60.0f, 0.0f, L.maxScroll());
            app.desktop_sheet_page_ = app.desktopSettingsPage(); app.desktop_sheet_row_ = row;
        };
        {
            const SteamCollection own{L"Moja kolekcja", {}, "local:fixture-own"};
            const auto key = collectionKey(app.account_, own);
            app.library_state_.collections.names[key] = own.name;
            app.collectionChanged(); app.buildSettingsRows();
            const auto row = [&] {
                for (size_t i = 0; i < app.settings_rows_.size(); ++i) if (app.settings_rows_[i].key == key) return static_cast<int>(i);
                return -1;
            };
            int index = row();
            if (index < 0 || !app.settings_rows_[index].removable || app.settings_rows_[1].removable) return 244;
            // A Steam collection's focus stops at ↓.
            app.settings_state_.selected[app.settings_state_.category] = 1; app.settings_state_.subcontrol = 2;
            app.settingsAdjust(1);
            if (app.settings_state_.subcontrol != 2) return 245;
            app.settings_state_.selected[app.settings_state_.category] = index; app.ensureSettingVisible();
            app.settingsAdjust(1);
            if (app.settings_state_.subcontrol != 3) return 246;
            if (!capture(L"settings-collection-trash.png")) return 247;
            app.desktop_mode_ = true; app.desktop_sheet_keyboard_ = true; desktopScroll(index);
            const bool desktop_trash = capture(L"settings-collection-trash-desktop.png");
            app.desktop_mode_ = false;
            if (!desktop_trash) return 248;
            app.settingsActivate();
            if (app.exit_prompt_ != 2 || app.prompt_kind_ != Launcher::PromptKind::delete_collection) return 249;
            if (!capture(L"settings-collection-delete-tv.png")) return 250;
            app.desktop_mode_ = true; const bool desktop_prompt = capture(L"settings-collection-delete-desktop.png"); app.desktop_mode_ = false;
            if (!desktop_prompt) return 251;
            app.exitPromptMove(-1); app.exitPromptAccept(); // Ukryj na ekranie głównym.
            if (!app.settings_.hidden_collections.contains(key) || row() < 0 || app.exit_prompt_) return 252;
            // Delete (and the pad's X) asks the same; Usuń kolekcję deletes it.
            index = row(); app.settings_state_.selected[app.settings_state_.category] = index; app.settings_state_.subcontrol = 0;
            if (!app.settingsResetFocused() || app.exit_prompt_ != 2) return 253;
            app.exitPromptMove(-2); app.exitPromptAccept();
            if (row() >= 0 || app.library_state_.collections.names.contains(key) || app.settings_.hidden_collections.contains(key)) return 254;
        }
        // Interfejs → Widoki biblioteki w trybie TV, at the very bottom: the view a console's, a collection's or a
        // publisher's library opens in until it has its own; each row's Zresetuj gives it to all of them again.
        {
            using settings_ui::Id;
            app.settings_state_.selectCategory(static_cast<int>(settings_ui::Category::appearance));
            app.buildSettingsRows();
            app.settings_state_.options = true;
            const auto& rows = app.settings_rows_;
            const int last = static_cast<int>(rows.size()) - 1;
            if (last < 3 || rows[last - 2].id != Id::default_view_console || rows[last - 1].id != Id::default_view_collection ||
                rows[last].id != Id::default_view_studio || rows[last - 2].section != tr(L"Widoki biblioteki w trybie TV") ||
                rows[last - 1].reset != "views:collection" || rows[last - 1].badge.empty()) return 7101;
            app.settings_state_.selected[app.settings_state_.category] = last - 1; app.ensureSettingVisible();
            if (!capture(L"settings-library-views.png")) return 7102;
            app.desktop_mode_ = true; app.desktop_sheet_keyboard_ = true; desktopScroll(last - 2);
            const bool desktop_views = capture(L"settings-library-views-desktop.png"); app.desktop_mode_ = false;
            if (!desktop_views) return 7103;
            app.settingsActivate(); // The picker,
            app.settings_state_.choice = library_view::drawerRow(library_view::View::list);
            app.settingsActivate(); // Lista.
            if (app.settings_.default_view_collection != static_cast<int>(library_view::View::list) || app.settings_state_.editing) return 7104;
            // A collection without its own view opens in Lista, one with its own keeps it; Zresetuj: Lista for all, consoles keep theirs.
            const auto category = app.library_category_;
            app.library_category_ = "collection-3";
            const auto scope = app.libraryScope(), console = std::to_string(app.account_) + ":console:PS2";
            if (app.libraryOptions().view != library_view::View::list) return 7105;
            const int collections = app.libraryViewOverrides("collection"), consoles = app.libraryViewOverrides("console");
            app.library_state_.options[scope].view = library_view::View::cinematic;
            app.library_state_.options[console].view = library_view::View::bookshelf;
            if (app.libraryOptions().view != library_view::View::cinematic || app.libraryViewOverrides("collection") != collections + 1 ||
                app.libraryViewOverrides("console") != consoles + 1) return 7106;
            app.buildSettingsRows();
            if (!app.settingsResetRow(app.settings_rows_[static_cast<size_t>(last - 1)])) return 7107;
            if (app.libraryOptions().view != library_view::View::list || app.library_state_.options.contains(scope) ||
                app.libraryViewOverrides("collection") != 0 || !app.library_state_.options.contains(console)) return 7108;
            app.library_state_.options.erase(console);
            app.library_category_ = category; app.settings_.default_view_collection = 0;
        }
        // Plugins: the repository bundles HowLongToBeat. Packages built outside the repository are
        // removed first, so this flow sees the same plugin set everywhere.
        app.initializePlugins();
        if (!waitPlugins(app)) return 1000;
        for (const auto& plugin : std::vector<plugins::Plugin>(app.plugins_.plugins)) {
            if (plugin.manifest.id == "launchme.howlongtobeat") continue;
            plugins::Job extra; extra.kind = plugins::Job::Kind::remove; extra.plugin = plugin.manifest.id;
            app.submitPlugin(std::move(extra));
            if (!waitPlugins(app)) return 1000;
        }
        if (app.plugins_.plugins.size() != 1 || !app.plugins_.plugins[0].enabled) return 1000;
        app.settings_state_.selectCategory(5);
        app.buildSettingsRows();
        app.settingsActivate();
        if (std::none_of(app.settings_rows_.begin(), app.settings_rows_.end(), [](const auto& row) {
            return row.id == settings_ui::Id::plugin_enabled && row.key == "launchme.howlongtobeat"; })) return 10;
        if (!capture(L"settings-native-plugins.png")) return 1004;
        {
            // A long description: the row grows so its lines sit above the tags, never under them.
            const auto plugin = std::find_if(app.settings_rows_.begin(), app.settings_rows_.end(), [](const auto& row) {
                return row.id == settings_ui::Id::plugin_enabled && row.key == "launchme.howlongtobeat"; });
            const int index = static_cast<int>(plugin - app.settings_rows_.begin());
            plugin->description = L"Czas głównej fabuły, fabuły z dodatkami i ukończenia na 100% z HowLongToBeat, porównanie z Twoim czasem gry "
                                  L"w Szczegółach, widżet aktywnej gry i szuflada z najkrótszymi grami z Twojej biblioteki do ukończenia w weekend.";
            app.renderer_.fitSettingsDescriptions(app.settings_rows_, app.settings_state_, false);
            const auto view = app.renderer_.viewport();
            const auto layout = settings_ui::Layout::of(view.width, view.height, app.settings_state_, app.settings_rows_);
            if (app.settings_rows_[index].lines < 3 || layout.rowHeight(index) <= settings_ui::Layout::row_height) return 1041;
            if (!capture(L"settings-plugins-long.png")) return 1042;
            app.renderer_.fitSettingsDescriptions(app.settings_rows_, app.settings_state_, true); // The desktop window: two lines, no tags.
            if (app.settings_rows_[index].lines) return 1043;
            app.buildSettingsRows();
            if (app.settings_rows_[index].lines) return 1043; // The manifest's one line again.
        }
        app.back();
        if (app.settingsOpen()) app.back();
        if (app.settingsOpen()) return 10;
        // A game started from its details: Home, the game first in Recent and focused.
        {
            LibraryMessage installation; installation.account = app.account_;
            installation.games = app.installed_library_; installation.games.push_back(Game{620, L"Portal 2"});
            app.updateSteamInstallations(installation);
        }
        const auto found_portal = std::find_if(app.public_library_.begin(), app.public_library_.end(), [](const Game& game) { return game.app_id == 620; });
        if (found_portal == app.public_library_.end()) return 12;
        const Game portal = *found_portal;
        app.openDetails(portal);
        app.details_ = parseGameDetails(620, R"({"620":{"success":true,"data":{"short_description":"Wyrusz w podróż przez laboratoria Aperture Science. Rozwiązuj zagadki przestrzenne, wykorzystując portale i odkrywaj historię tajemniczego kompleksu.","developers":["Valve"],"publishers":["Valve"],"release_date":{"date":"19 kwietnia 2011"},"genres":[{"description":"Logiczne"},{"description":"Przygodowe"}]}}})");
        app.details_.playtime_minutes = 754; app.details_.achievements_unlocked = 7; app.details_.achievements_total = 51;
        app.details_loading_ = false;
        if (app.screen_ != ScreenMode::details || !dispatched.empty()) return 13;
        if (!capture(L"details-native-play.png")) return 97;
        app.openArtworkBrowser(portal);
        if (!app.artworkOpen() || !dispatched.empty()) return 30;
        auto images = std::make_unique<ArtworkBrowserMessage>();
        images->generation = app.artwork_jobs_->generation.load();
        images->page = parseGridPage(R"({"data":[{"id":77,"width":512,"height":512,"url":"https://example.invalid/a.png"},{"id":78,"width":600,"height":900,"url":"https://example.invalid/b.png"}]})");
        app.onArtworkBrowser(std::move(images));
        const auto image = pngFixture();
        for (uint32_t id : {77u, 78u}) {
            auto thumbnail = std::make_unique<ArtworkBrowserMessage>();
            thumbnail->generation = app.artwork_jobs_->generation.load();
            thumbnail->type = ArtworkBrowserMessage::Type::thumbnail;
            thumbnail->asset_id = id;
            thumbnail->bytes = image;
            app.onArtworkBrowser(std::move(thumbnail));
        }
        app.artworkAccept();
        app.artworkMove(1, 0);
        app.artworkAccept();
        for (int i = 0; i < 30; ++i) app.artworkAccept();
        if (std::count_if(app.artwork_jobs_->jobs.begin(), app.artwork_jobs_->jobs.end(), [](const auto& job) {
            return job.type == ArtworkBrowserMessage::Type::apply;
        }) != 1) return 31;
        const auto chosen_game = app.artwork_state_.game;
        if (!cacheArtworkVariant(chosen_game, ArtworkKind::cover, 78, image, coverCacheDirectory())) return 32;
        auto applied = std::make_unique<ArtworkBrowserMessage>();
        applied->generation = app.artwork_jobs_->generation.load();
        applied->type = ArtworkBrowserMessage::Type::apply;
        applied->asset_id = 78;
        applied->bytes = image;
        app.onArtworkBrowser(std::move(applied));
        if (app.artwork_state_.applied_id != 78 || selectedArtworkId(chosen_game, ArtworkKind::cover, coverCacheDirectory()) != 78) return 33;
        if (!capture(L"artwork-native-browser.png")) return 99;
        app.back(); // Gallery -> types.
        app.artworkMove(0, 1); // Background category.
        auto heroes = std::make_unique<ArtworkBrowserMessage>();
        heroes->generation = app.artwork_jobs_->generation.load();
        heroes->page = parseGridPage(R"({"data":[{"id":88,"width":1920,"height":1080,"url":"https://example.invalid/hero.png"}]})");
        app.onArtworkBrowser(std::move(heroes));
        app.artworkAccept(); app.artworkAccept();
        auto cancelled = std::make_unique<ArtworkBrowserMessage>();
        cancelled->generation = app.artwork_jobs_->generation.load();
        cancelled->type = ArtworkBrowserMessage::Type::apply;
        cancelled->asset_id = 88; cancelled->bytes = image;
        if (!cacheArtworkVariant(chosen_game, ArtworkKind::hero, 88, image, coverCacheDirectory())) return 34;
        app.back(); // Cancel the pending download and return to details.
        app.onArtworkBrowser(std::move(cancelled));
        if (app.screen_ != ScreenMode::details || selectedArtworkId(chosen_game, ArtworkKind::hero, coverCacheDirectory())) return 35;
        auto old_cover = std::make_unique<CoverMessage>();
        old_cover->generation = app.jobs_->generation.load();
        old_cover->key = gameKey(chosen_game);
        old_cover->bytes = pixel(); // An older automatic request finishing after the manual selection.
        app.onCover(std::move(old_cover));
        if (const auto* art = app.art_.peek(gameKey(chosen_game)); !art || !art->cover || *art->cover != image) return 36;
        app.detail_action_ = 0;
        app.launch();
        if (!app.pending_launch_) return 17;
        app.finishLaunch();
        if (dispatched.size() != 1 || dispatched.back() != L"steam://rungameid/620") return 18;
        if (app.screen_ != ScreenMode::shelves || app.categories_[app.category_].id != "recent" ||
            app.cards_[app.selected_].game.app_id != 620 || app.categories_[0].games[0].app_id != 620) return 19;
        app.focusRegained();
        if (app.audio_.music_target_ != 0) return 117; // Alt+Tab must not resume music.
        const auto session_time = GetTickCount64();
        const auto activity = [&](GameActivity state, uint64_t time) {
            auto message = std::make_unique<GameActivityMessage>();
            message->generation = app.game_activity_jobs_->generation.load(); message->observed_at = time;
            message->activities.emplace_back("steam:620", state);
            app.onGameActivity(std::move(message));
        };
        activity(GameActivity::running, session_time + 100);
        activity(GameActivity::stopped, session_time + 1000);
        if (app.audio_.music_target_ != 0) return 118;
        activity(GameActivity::stopped, session_time + 2700);
        if (!app.game_sessions_.empty() || app.audio_.music_target_ <= 0) return 119;
        app.beginGameSession(Game{900001, L"Session one"});
        const auto stale_session_generation = app.game_activity_jobs_->generation.load();
        app.beginGameSession(Game{900002, L"Session two"});
        auto stale_session = std::make_unique<GameActivityMessage>();
        stale_session->generation = stale_session_generation; stale_session->observed_at = session_time + 180000;
        stale_session->activities.emplace_back("steam:900001", GameActivity::stopped);
        app.onGameActivity(std::move(stale_session));
        if (app.game_sessions_.size() != 2) return 120;
        const auto batch = [&](GameActivity first, GameActivity second, uint64_t time) {
            auto message = std::make_unique<GameActivityMessage>();
            message->generation = app.game_activity_jobs_->generation.load(); message->observed_at = time;
            message->activities = {{"steam:900001", first}, {"steam:900002", second}};
            app.onGameActivity(std::move(message));
        };
        batch(GameActivity::running, GameActivity::running, session_time + 5000);
        batch(GameActivity::stopped, GameActivity::running, session_time + 6000);
        batch(GameActivity::stopped, GameActivity::running, session_time + 7600);
        if (app.game_sessions_.size() != 1 || app.audio_.music_target_ != 0) return 121;
        batch(GameActivity::stopped, GameActivity::stopped, session_time + 8000);
        batch(GameActivity::stopped, GameActivity::stopped, session_time + 9600);
        if (!app.game_sessions_.empty() || app.audio_.music_target_ <= 0) return 122;
        // Ended sessions leave Home and its focus as they were.
        if (app.screen_ != ScreenMode::shelves || app.cards_[app.selected_].game.app_id != 620) return 1005;
        auto pc = std::make_unique<PcLibraryMessage>();
        pc->generation = app.pc_token_->load();
        for (const auto source : pc_sources) {
            Game game;
            game.kind = GameKind::store; game.source = source;
            game.source_id = "123"; game.name = sourceName(source);
            game.play_action = {L"C:\\synthetic-game.exe"};
            pc->library.visible.push_back(game);
            const auto executable = userConfigDirectory() / (std::string(sourceSlug(source)) + "-client.exe");
            std::ofstream(executable) << "fixture";
            pc->library.clients.push_back({source, executable});
        }
        Game hidden = pc->library.visible[0];
        hidden.source_id = "456"; hidden.visibility = GameVisibility::private_game;
        pc->library.private_games.push_back(hidden);
        // A profile whose first scan already switched stores on (or off): the user's choice stands.
        app.settings_.pc_sources_detected = true;
        app.onPcLibrary(std::move(pc));
        if (std::any_of(app.public_library_.begin(), app.public_library_.end(), [](const auto& game) { return game.source != GameSource::steam; })) return 40;
        Game emulated{90000001, L"Emulation fixture"};
        emulated.kind = GameKind::nonsteam; emulated.platform = L"PS2";
        app.shortcut_library_.push_back(emulated);
        app.openSettings();
        app.settings_state_.selectCategory(static_cast<int>(settings_ui::Category::library));
        app.settings_state_.selected[app.settings_state_.category] = 0; // Biblioteka remembered its Kolekcje row.
        app.buildSettingsRows();
        // Platforms: Steam, DRM-free, then the stores by name (Launcher::platformOrder).
        const auto order = app.platformOrder();
        const auto at = [&](GameSource source) { return static_cast<int>(std::find(order.begin(), order.end(), source) - order.begin()); };
        app.settingsActivate(); app.settingsMove(at(GameSource::gog)); app.settingsActivate(); // Enable GOG.
        app.settingsMove(at(GameSource::epic) - at(GameSource::gog)); app.settingsActivate(); // Enable Epic.
        if (app.categories_[1].title != L"Platformy PC" || app.categories_[2].title != L"Emulacja" || app.categories_[1].games.size() != 3) return 41;
        if (std::count_if(app.public_library_.begin(), app.public_library_.end(), [](const auto& game) { return game.source != GameSource::steam; }) != 2 || app.private_category_.games.size() != 1) return 42;
        const auto launches = dispatched.size();
        app.settingsAdjust(1); app.settingsActivate();
        // The cog opens Epic's own options; it must not start the locally installed client.
        // The injected account/browser flow is exercised in epic_ui_cases.hpp.
        if (dispatched.size() != launches || app.settings_state_.platform() != GameSource::epic) return 43;
        if (!capture(L"settings-native-pc.png")) return 100;
        app.settings_state_.options = app.settings_state_.editing = false;
        app.back();
        app.showCategory(1);
        app.navigate(1, false); app.launch();
        if (app.screen_ != ScreenMode::library || app.pc_source_ != GameSource::gog || app.cards_.size() != 1 || app.cards_[0].game.source_id != "123") return 44;
        app.grid_scroll_ = 10000;
        app.rebuildCards(false);
        if (app.grid_scroll_ != 0) return 46;
        app.click(34 * app.renderer_.viewport().scale, 53 * app.renderer_.viewport().scale, false);
        if (app.screen_ != ScreenMode::shelves) return 45;
        for (size_t i = 0; i < app.cards_.size(); ++i) {
            auto& card = app.cards_[i];
            card.size = i == app.selected_ ? 280.0f : 140.0f;
            card.x = carousel::left(i, app.selected_, app.cards_.size(), 1600, false);
            card.y = 814 - card.size; card.opacity = 1;
        }
        if (!capture(L"pc-native-platforms.png")) return 101;
        auto fake = std::make_unique<FakeDevices>();
        auto* backend = fake.get();
        backend->data.audio[0] = {0.3f, true, false, L"out1"};
        backend->data.audio[1] = {0.65f, true, false, L"in1"};
        backend->data.endpoints = {{L"out1", L"Głośniki · Realtek Audio", false, true}, {L"out2", L"Słuchawki bezprzewodowe", false, false},
            {L"in1", L"Mikrofon · USB Audio", true, true}, {L"in2", L"Mikrofon zestawu słuchawkowego", true, false}};
        backend->data.controllers = {{L"usb1", L"DualSense", L"USB", devices::Picture::dualsense, 65, true, true},
            {L"usb2", L"8BitDo Ultimate 2", L"USB / odbiornik", devices::Picture::ultimate2},
            {L"usb3", L"Xbox Controller", L"Bezprzewodowo", devices::Picture::xbox}};
        backend->data.bluetooth = {{L"bt1", L"Kontroler testowy", false, false}};
        wchar_t online_art[4]{};
        if (GetEnvironmentVariableW(L"LAUNCHME_UI_CONTROLLER_ART", online_art, 4) == 1 && online_art[0] == L'1') {
            for (const auto kind : {devices::Picture::dualsense, devices::Picture::ultimate2, devices::Picture::xbox}) {
                auto bytes = controllerPicture(kind, userConfigDirectory() / L"controllers");
                if (bytes.empty()) return 73;
                backend->data.images[kind] = std::move(bytes);
            }
        }
        app.device_service_ = std::move(fake);
        app.openDevices(devices::Panel::audio); app.pollDevices();
        app.changeCategory(2); // The slider (default focus), then the device list.
        if (!backend->actions.empty() || app.device_state_.rows[app.device_state_.selected].id != L"out2") return 60;
        app.enterDown(false); app.enterUp();
        if (backend->actions.size() != 1 || backend->actions.back().action != devices::Action::endpoint || backend->actions.back().id != L"out2") return 61;
        app.deviceMove(-2);
        if (app.device_state_.rows[app.device_state_.selected].kind != device_ui::Kind::volume) return 184;
        app.deviceMove(-1); // Up from the slider: the mute button above it, at the card's top.
        if (app.device_state_.rows[app.device_state_.selected].kind != device_ui::Kind::mute || app.device_state_.rows[app.device_state_.selected].input) return 185;
        app.deviceAdjust(1); // Left/Right off the slider switch the column and never change the volume.
        if (backend->actions.size() != 1 || app.device_state_.audio_column != 1 || app.device_state_.audio_edit_column != -1 ||
            app.device_state_.rows[app.device_state_.selected].kind != device_ui::Kind::mute) return 190;
        app.deviceAdjust(-1); app.deviceMove(1);
        if (app.device_state_.rows[app.device_state_.selected].kind != device_ui::Kind::volume || app.device_state_.audio_column != 0) return 186;
        if (!capture(L"audio-slider-focused.png")) return 191;
        app.deviceAdjust(1); // On the focused slider Left/Right are the volume itself (PS5), 1% a step.
        if (backend->actions.size() != 2 || backend->actions.back().action != devices::Action::volume) return 62;
        if (backend->actions.back().input || backend->actions.back().id != L"out1" || std::abs(backend->actions.back().value - 0.31f) > 0.001f) return 170;
        if (app.device_state_.audio_edit_column != 0 || app.device_state_.audio_column != 0) return 192;
        if (!app.volumeSliderFocused()) return 187;
        if (!capture(L"audio-slider-editing.png")) return 193;
        app.enterDown(false); app.enterUp();
        if (app.device_state_.audio_edit_column != -1 || backend->actions.size() != 2) return 194;
        app.deviceAccept(); app.back();
        if (!app.devicesOpen() || app.device_state_.audio_edit_column != -1 || app.device_state_.audio_column != 0) return 195;
        app.deviceAccept(); app.tabDown(false);
        if (app.device_state_.audio_edit_column != -1 || app.device_state_.audio_column != 1 || backend->actions.size() != 2) return 196;
        app.launch(); // Gamepad A shares this activation path.
        if (app.device_state_.audio_edit_column != 1) return 197;
        app.deviceAdjust(-1);
        if (!backend->actions.back().input || backend->actions.back().id != L"in1" || std::abs(backend->actions.back().value - 0.64f) > 0.001f) return 171;
        app.next_panel_poll_ = 0; app.pollDevices();
        if (std::abs(app.device_state_.data.audio[1].volume - 0.64f) > 0.001f) return 172; // Old snapshot cannot snap back the drag/keyboard preview.
        const device_ui::AudioLayout audio_layout{app.renderer_.viewport().width, app.renderer_.viewport().height};
        const auto microphone_track = audio_layout.track(1);
        app.deviceClick(std::lerp(microphone_track.left, microphone_track.right, 0.5f), (microphone_track.top + microphone_track.bottom) / 2);
        app.deviceDrag(std::lerp(microphone_track.left, microphone_track.right, 0.83f), microphone_track.top);
        app.pointerUp();
        if (app.audio_drag_column_ != -1 || app.device_state_.audio_edit_column != -1 || !backend->actions.back().input || std::abs(backend->actions.back().value - 0.83f) > 0.001f) return 173;
        backend->data.audio[1].volume = 0.81f; backend->data.audio[1].acknowledged = backend->actions.back().sequence;
        app.next_panel_poll_ = 0; app.pollDevices();
        if (std::abs(app.device_state_.data.audio[1].volume - 0.81f) > 0.001f || app.device_state_.pending_audio[1]) return 174;
        app.deviceMove(-1); app.deviceAccept(); // The mute button sits above the slider.
        if (backend->actions.back().action != devices::Action::mute || !backend->actions.back().input || backend->actions.back().value != 1) return 175;
        backend->data.audio[1].muted = true; backend->data.audio[1].acknowledged = backend->actions.back().sequence;
        app.next_panel_poll_ = 0; app.pollDevices();
        app.volume_notice_.reset();
        if (!capture(L"devices-native-audio.png")) return 102;
        app.deviceMove(1);
        app.deviceClick(std::lerp(microphone_track.left, microphone_track.right, 0.5f), microphone_track.top + 14);
        if (app.audio_drag_column_ != 1) return 176;
        const auto input_snapshot = backend->data.audio[1];
        backend->data.audio[1].available = false; backend->data.audio[1].endpoint.clear();
        app.next_panel_poll_ = 0; app.pollDevices();
        if (app.audio_drag_column_ != -1 || app.device_state_.audio_edit_column != -1 || app.device_state_.pending_audio[1]) return 177;
        const auto before_unavailable = backend->actions.size(); app.deviceAdjust(1);
        if (backend->actions.size() != before_unavailable) return 178;
        app.deviceAccept();
        if (app.device_state_.audio_edit_column != -1) return 198;
        if (!capture(L"audio-native-missing-input.png")) return 179;
        backend->data.audio[1] = input_snapshot; app.next_panel_poll_ = 0; app.pollDevices();
        app.notifications_.clear();
        app.showNotification(NotificationKind::controller, L"Kontroler połączony", L"DualSense");
        app.notifications_.front().age = 0.5f;
        app.showVolume(0.64f, false); app.volume_notice_->age = 0.5f;
        if (app.notifications_.size() != 1 || !app.volume_notice_) return 180;
        app.showVolume(0.72f, false);
        if (app.notifications_.size() != 1 || app.volume_notice_->level != 0.72f || app.volume_notice_->age <= 0) return 181;
        if (!capture(L"audio-native-controller-toast-volume.png")) return 182;
        app.notifications_.front().title = L"Kontroler rozłączony"; app.notifications_.front().connected = false;
        app.showVolume(0.81f, true, true);
        if (!capture(L"audio-native-disconnected-mic-mute.png")) return 183;
        app.notifications_.clear(); app.volume_notice_.reset();
        app.device_state_.selected = static_cast<int>(app.device_state_.rows.size()) - 2; app.launch(); app.pollDevices();
        if (app.device_state_.panel != devices::Panel::bluetooth) return 63;
        app.deviceMove(1); app.launch();
        if (!capture(L"devices-native-bluetooth.png")) return 105;
        if (backend->actions.back().action != devices::Action::pair || backend->actions.back().id != L"bt1") return 64;
        backend->data.busy = true; app.next_panel_poll_ = 0; app.pollDevices();
        const auto action_count = backend->actions.size();
        app.launch();
        if (backend->actions.size() != action_count) return 65;
        app.deviceMove(-1); app.launch();
        if (backend->actions.back().action != devices::Action::cancel) return 66;
        app.back();
        if (app.device_state_.panel != devices::Panel::audio) return 67;
        // Kontrolery: a gallery of the pads (focus on the first one), "Sparuj kontroler" last.
        app.back(); app.openDevices(devices::Panel::controllers); app.pollDevices();
        if (!app.device_state_.gallery() || app.device_state_.selected != 1) return 103;
        if (!capture(L"devices-native-controllers.png")) return 103;
        app.deviceAdjust(1);
        if (app.device_state_.selected != 2 || !capture(L"devices-native-8bitdo.png")) return 106;
        app.deviceAdjust(1);
        if (app.device_state_.selected != 3 || !capture(L"devices-native-xbox.png")) return 107;
        app.deviceAdjust(1); // Past the pads: "Sparuj kontroler".
        if (app.device_state_.selected != 0) return 2041;
        app.deviceMove(-1);
        if (app.device_state_.selected != -1) return 2042;
        app.deviceMove(1);
        if (app.device_state_.selected != 1) return 2043;
        const auto connected_controllers = backend->data.controllers;
        backend->data.controllers.clear(); app.next_panel_poll_ = 0; app.pollDevices();
        if (app.device_state_.rows.size() != 1) return 199;
        if (!capture(L"controllers-native-empty.png")) return 200;
        const auto devices_view = app.renderer_.viewport();
        const device_ui::Layout controllers_layout{devices_view.width, devices_view.height, devices::Panel::controllers};
        const auto empty_button = controllers_layout.controllerEmptyButton();
        const auto before_empty_button = backend->actions.size();
        app.deviceClick((empty_button.left + empty_button.right) / 2, (empty_button.top + empty_button.bottom) / 2);
        if (app.device_state_.panel != devices::Panel::bluetooth || backend->actions.size() != before_empty_button) return 201;
        app.back(); backend->data.controllers = connected_controllers; app.next_panel_poll_ = 0; app.pollDevices();
        app.back();
        app.openSettings(); app.settings_state_.selectCategory(static_cast<int>(settings_ui::Category::audio));
        app.buildSettingsRows(); app.settingsActivate();
        {
            // Dźwięk opens with the source tiles; the music theme is in "Muzyka i dźwięki".
            if (app.settings_rows_.empty() || app.settings_rows_.front().control != settings_ui::Control::sources ||
                app.settings_rows_.front().tiles.size() != 2) return 104;
            const auto theme = std::find_if(app.settings_rows_.begin(), app.settings_rows_.end(),
                                            [](const auto& row) { return row.id == settings_ui::Id::music_theme; });
            if (theme == app.settings_rows_.end()) return 104;
            app.settings_state_.selected[app.settings_state_.category] = static_cast<int>(theme - app.settings_rows_.begin());
        }
        app.settingsActivate();
        if (!capture(L"settings-native-music.png")) return 104;
        // Only the shipped themes and "Brak", Horizons by default; no folder, so no file names.
        {
            const auto theme = std::find_if(app.settings_rows_.begin(), app.settings_rows_.end(),
                                            [](const auto& row) { return row.id == settings_ui::Id::music_theme; });
            if (theme == app.settings_rows_.end() || theme->choices.size() != music_themes.size() || theme->choice != 1) return 68;
        }
        app.settingsMove(1); app.settingsActivate(); app.saveSettings();
        if (loadAppSettings().music_theme != 2 || app.music_jobs_->pending != std::wstring(music_themes[2].file)) return 68;
        app.audio_.resumeMusic();
        app.audio_.setMusicLevel(0); app.audio_.setMusicLevel(0.15f);
        if (app.audio_.music_target_ != 0.15f) return 69;
        app.audio_.stopMusic(); app.audio_.setMusicLevel(0.25f);
        if (app.audio_.music_target_ != 0) return 70;
        app.audio_.resumeMusic();
        if (app.audio_.music_target_ != 0.25f) return 71;
        auto stale_music = std::make_unique<AudioMessage>();
        stale_music->cue = SoundCue::music; stale_music->generation = app.music_jobs_->generation.load() - 1;
        app.onAudio(std::move(stale_music));
        if (app.music_status_ != L"Wczytywanie…") return 72;
        // "Brak" is a real saved theme, cancels pending decodes, and cannot be undone by a late result.
        const auto old_generation = app.music_jobs_->generation.load();
        app.settingsActivate(); app.settingsMove(-2); app.settingsActivate(); app.saveSettings();
        if (app.settings_.music_theme != 0 || loadAppSettings().music_theme != 0 || app.music_jobs_->pending ||
            !app.music_status_.empty() || app.audio_.music_voice_ || !app.audio_.pending_music_.samples.empty()) return 156;
        auto cancelled_music = std::make_unique<AudioMessage>();
        cancelled_music->cue = SoundCue::music; cancelled_music->generation = old_generation;
        app.onAudio(std::move(cancelled_music));
        app.audio_.resumeMusic();
        if (!app.music_status_.empty() || app.audio_.music_voice_) return 157;
        // Whole-library warmup is disk-only and cannot take priority away from
        // navigation, or enqueue a hidden game's art through the public view.
        Game background_game{700007, L"Background fixture"};
        Game private_game{700008, L"Private fixture"}; private_game.visibility = GameVisibility::private_game;
        app.public_library_.push_back(background_game); app.private_category_.games.push_back(private_game);
        app.jobs_->jobs.clear(); app.jobs_->secondary.clear(); app.jobs_->prefetch.clear(); app.jobs_->requested.clear();
        app.prefetchLibraryArt();
        if (std::any_of(app.jobs_->prefetch.begin(), app.jobs_->prefetch.end(), [&](const auto& job) {
            return gameKey(job.game) == gameKey(private_game) || !job.prefetch;
        })) return 77;
        const auto background_count = app.jobs_->prefetch.size();
        app.requestArt(background_game, CoverMessage::Kind::cover);
        if (app.jobs_->prefetch.size() != background_count - 1 || app.jobs_->jobs.empty() ||
            app.jobs_->jobs.front().prefetch || gameKey(app.jobs_->jobs.front().game) != gameKey(background_game)) return 78;
        auto disk_ready = std::make_unique<CoverMessage>();
        disk_ready->key = gameKey(background_game); disk_ready->kind = CoverMessage::Kind::hero;
        disk_ready->generation = app.jobs_->generation.load(); disk_ready->prefetched = true;
        app.onCover(std::move(disk_ready));
        if (app.art_.contains(gameKey(background_game)) ||
            !app.jobs_->disk_cached.contains(gameKey(background_game) + ':' + std::to_string(static_cast<int>(CoverMessage::Kind::hero)))) return 79;
        app.openLibrary();
        const auto crop_game = app.cards_[0].game;
        const auto crop_file = userConfigDirectory() / L"manual-fixture.png";
        const auto portrait = pngFixture(40, 64, 96);
        { std::ofstream file(crop_file, std::ios::binary); file.write(reinterpret_cast<const char*>(portrait.data()), static_cast<std::streamsize>(portrait.size())); }
        const auto crop_reply = [&] {
            auto message = std::make_unique<CropMessage>();
            message->generation = app.crop_token_->load(); message->image = crop_file; message->bytes = portrait;
            message->width = 64; message->height = 96; message->automatic = {0.5f, 0.5f};
            message->focus = manualTextCrop(crop_file).value_or(message->automatic);
            app.onCrop(std::move(message));
        };
        // The game menu without plugin contributions: its own rows only.
        plugins::Job pause_hltb; pause_hltb.kind = plugins::Job::Kind::enable; pause_hltb.plugin = "launchme.howlongtobeat";
        pause_hltb.value = plugins::wire::boolean(false); app.submitPlugin(std::move(pause_hltb));
        if (!waitPlugins(app)) return 1005;
        if (crop_game.kind != GameKind::steam) return 125;
        dispatched.clear();
        app.openContextMenu();
        if (!app.pluginContext(crop_game).empty() || context_menu::item(1, app.menuRows()) != context_menu::Item::details) return 128;
        app.menuMove(10);
        // Its own rows, and Odinstaluj last for an installed Steam game.
        const auto own_rows = app.menuRows();
        if (own_rows.uninstall != app.canUninstall(app.cards_[app.selected_].game) ||
            context_menu::rows(own_rows) != context_menu::rows({}) + (own_rows.uninstall ? 1 : 0) || app.menu_selected_ != context_menu::rows(own_rows) - 1 ||
            own_rows.uninstall != (context_menu::item(app.menu_selected_, own_rows) == context_menu::Item::uninstall)) return 129;
        app.dismissMenu(); dispatched.clear();
        // The crop editor opens from "Zmień grafikę" ("Kadruj" beside "Szukaj", on Kwadrat), no longer from the game menu.
        app.openArtworkBrowser(crop_game);
        if (!app.artworkOpen()) return 159;
        app.artwork_state_.category = artwork_ui::category(ArtworkKind::cover);
        app.openArtworkCrop();
        if (!app.cropOpen() || app.pending_launch_ || app.artworkOpen()) return 123;
        crop_reply(); app.changeCategory(-10);
        if (!capture(L"crop-native-editor.png")) return 109;
        app.launch();
        const auto saved_crop = manualTextCrop(crop_file);
        if (app.cropOpen() || !saved_crop || saved_crop->y >= 0.5f) return 80;
        const auto crop_key = gameKey(crop_game) + ':' + std::to_string(static_cast<int>(CoverMessage::Kind::cover));
        const auto failures = userConfigDirectory() / L"artwork" / L"failures";
        const auto failed_at = artwork_failure::now() - 1000;
        artwork_failure::record(failures, crop_key, crop_game.name, failed_at, failed_at + 60000, false);
        app.retryGameArtwork(crop_game, false);
        if (artwork_failure::blockedUntil(failures, crop_key, crop_game.name, artwork_failure::now()) || !manualTextCrop(crop_file)) return 115;
        // Fixed coordinates below target the 4-row menu's last row, "Ponów grafiki".
        app.cards_[0].x = 68; app.cards_[0].y = 190; app.cards_[0].size = 186;
        app.mouse_transition_until_ = 0; app.openContextMenu();
        if (context_menu::rows(app.menuRows()) != 4 || context_menu::item(3, app.menuRows()) != context_menu::Item::retry) return 160;
        app.click(150 * app.renderer_.viewport().scale, 200 * app.renderer_.viewport().scale, false);
        app.click(150 * app.renderer_.viewport().scale, 200 * app.renderer_.viewport().scale, true);
        if (app.menu_open_ || app.pending_launch_) return 124;
        auto late_cover = std::make_unique<CoverMessage>();
        late_cover->key = gameKey(crop_game); late_cover->generation = app.jobs_->generation.load();
        late_cover->revision = app.art_revisions_[crop_key] - 1; late_cover->bytes = pixel();
        app.onCover(std::move(late_cover));
        if (const auto* art = app.art_.peek(gameKey(crop_game)); !art || !art->cover || *art->cover != portrait || art->focus.y != saved_crop->y) return 116;
        app.openCropEditor(crop_game); crop_reply(); app.changeCategory(20);
        auto late_crop = std::make_unique<CropMessage>(); late_crop->generation = app.crop_token_->load(); late_crop->focus = {0.5f, 0.65f};
        app.back(); app.onCrop(std::move(late_crop));
        if (std::abs(manualTextCrop(crop_file)->y - saved_crop->y) > 0.0001f) return 81;
        app.openCropEditor(crop_game); crop_reply(); app.cropReset();
        if (!manualTextCrop(crop_file)) return 82; // Reset is a preview until Save, not an immediate destructive action.
        app.crop_state_.control = 1; app.cropAccept();
        if (manualTextCrop(crop_file) || app.cropOpen()) return 83;
        plugins::Job restore_hltb; restore_hltb.kind = plugins::Job::Kind::enable; restore_hltb.plugin = "launchme.howlongtobeat";
        restore_hltb.value = plugins::wire::boolean(true); app.submitPlugin(std::move(restore_hltb));
        if (!waitPlugins(app)) return 1006;
        app.openLibrary(); app.navigate(50, false); app.rememberPlace();
        const auto bookmark = *app.library_state_.bookmark;
        app.saveLibraryPreferences(); app.openSettings();
        const auto saved_library = loadLibraryState(userConfigDirectory() / L"library-state.ini");
        if (!saved_library) return 84;
        app.library_state_ = *saved_library;
        app.restore_pending_ = true; app.restorePlace();
        if (app.screen_ != ScreenMode::library || library_view::selectionKey(app.cards_[app.selected_].game) != bookmark.selection ||
            std::abs(app.grid_scroll_ - bookmark.scroll) > 0.1f) return 84;
        // F6/Y opens the filter menu on the current order; moving only highlights.
        app.focusLibraryControls();
        if (!app.library_controls_.open() || app.library_controls_.focus != library_ui::filters_button) return 85;
        app.libraryVertical(1);
        if (app.libraryOptions().order != library_view::Order::last_played) return 85;
        for (int frame = 0; frame < 40; ++frame) { app.last_frame_ -= std::chrono::milliseconds(50); app.tick(); }
        app.notifications_.clear();
        if (!capture(L"library-native-sort.png")) return 110;
        app.libraryAccept(); // Applies and closes the menu over the re-sorted games.
        if (app.libraryOptions().order != library_view::Order::alphabetical || app.library_controls_.open() ||
            library_view::selectionKey(app.cards_[app.selected_].game) != bookmark.selection) return 86;
        app.libraryVertical(1);
        if (app.library_controls_.focus != -1 || app.library_controls_.open()) return 87;
        app.openPcLibrary(GameSource::gog);
        if (app.libraryOptions().order != library_view::Order::last_played) return 88;
        app.focusLibraryControls(); app.libraryVertical(library_view::order_count); app.libraryAccept(); app.saveLibraryPreferences();
        const auto preferences = loadLibraryState(userConfigDirectory() / L"library-state.ini");
        if (!preferences || !preferences->options.at("1:gog").installed_only || preferences->options.at("1:all").order != library_view::Order::alphabetical) return 89;
        app.closeLibraryPanel(); app.library_controls_.focus = -1;
        app.library_state_.bookmark = bookmark; app.library_state_.bookmark->selection = gameKey(private_game);
        app.restore_pending_ = true; app.restorePlace();
        if (app.private_shelf_ || app.screen_ != ScreenMode::shelves || app.category_ != 0) return 111;
        app.library_state_.bookmark = bookmark; app.library_state_.bookmark->source = "gog";
        app.pc_initial_ready_ = false; app.restore_pending_ = true; app.restorePlace();
        if (!app.restore_pending_) return 112;
        app.noteUserInput(); app.pc_initial_ready_ = true; app.restorePlace();
        if (app.screen_ != ScreenMode::shelves) return 113;
        app.library_state_.bookmark = bookmark; app.restore_pending_ = true;
        auto other_account = std::make_unique<LibraryMessage>(); other_account->account = 2;
        other_account->games.push_back(Game{9000, L"Another account public game"});
        app.onLibrary(std::move(other_account));
        if (app.account_ != 2 || app.restore_pending_ || app.private_shelf_ || app.screen_ != ScreenMode::shelves) return 114;
        // Optional read-only regression capture using a real poster outside the
        // repository. Both panels use the production squircle/bitmap renderer.
        wchar_t crop_image[4096]{}, crop_title[512]{};
        const auto crop_length = GetEnvironmentVariableW(L"LAUNCHME_UI_CROP_IMAGE", crop_image, 4096);
        if (crop_length && crop_length < 4096) {
            GetEnvironmentVariableW(L"LAUNCHME_UI_CROP_TITLE", crop_title, 512);
            std::ifstream input(crop_image, std::ios::binary);
            const std::vector<uint8_t> bytes(std::istreambuf_iterator<char>{input}, {});
            if (bytes.empty()) return 74;
            const auto metadata = userConfigDirectory() / L"crop-regression.focus";
            std::ofstream(metadata) << "3 0.5 0.5\n";
            const auto focus = cachedTextCrop(crop_image, metadata, crop_title);
            int version = 0; std::ifstream(metadata) >> version;
            if (version != crop_cache_version) return 75;
            const auto cached = cachedTextCrop(crop_image, metadata, crop_title);
            if (std::abs(cached.y - focus.y) > 0.00001f) return 76;
            app.screen_ = ScreenMode::shelves;
            app.category_ = 0; app.selected_ = 1;
            app.cards_.resize(2); app.renderer_.resetCovers(2, false);
            for (int i = 0; i < 2; ++i) {
                auto& card = app.cards_[i];
                card.game = Game{static_cast<uint32_t>(800 + i), crop_title};
                card.x = 184 + i * 650.0f; card.y = 176; card.size = 520;
                card.opacity = card.cover_opacity = 1;
                app.renderer_.setCover(i, bytes, i ? focus : CropFocus{});
            }
            app.menu_progress_ = app.friends_progress_ = app.category_menu_progress_ = app.quick_progress_ = 0;
            app.notifications_.clear(); app.launch_progress_ = 0;
            std::wstring crop_name = L"crop-";
            for (wchar_t ch : std::wstring(crop_title)) crop_name +=
                (ch >= L'A' && ch <= L'Z') || (ch >= L'a' && ch <= L'z') || (ch >= L'0' && ch <= L'9') ? ch : L'-';
            crop_name += L"-before-after.png";
            if (!capture(crop_name.c_str())) return 108;
            std::cout << "Real poster crop focus=" << focus.x << ',' << focus.y << "; cache v" << version << '\n';
        }
        // Live Steam updates share one state across dashboard, grids and details.
        app.pending_launch_ = false; app.showCategory(0);
        LibraryMessage progress; progress.installation_update = true; progress.account = app.account_;
        progress.games = app.installed_library_;
        Game downloading{70001, L"Instalowana gra"}; downloading.installed = false;
        downloading.installation = {InstallPhase::downloading, 375, 1000};
        progress.games.push_back(downloading);
        auto secret_download = downloading; secret_download.app_id = 70002; secret_download.visibility = GameVisibility::private_game;
        progress.private_games.push_back(secret_download); progress.excluded.insert(70002);
        app.updateSteamInstallations(progress);
        if (app.categories_[0].games[0].app_id != 70001 || std::any_of(app.categories_[0].games.begin(), app.categories_[0].games.end(), [](const Game& g) { return g.app_id == 70002; })) return 130;
        app.focusCard(0);
        for (int i = 0; i < 40; ++i) { app.last_frame_ -= std::chrono::milliseconds(50); app.tick(); }
        app.notifications_.clear();
        if (!capture(L"steam-install-dashboard.png")) return 131;
        app.openLibrary();
        const auto index = std::find_if(app.cards_.begin(), app.cards_.end(), [](const GameCard& c) { return c.game.app_id == 70001; });
        if (index == app.cards_.end()) return 132;
        app.focusCard(static_cast<size_t>(index - app.cards_.begin()));
        app.renderer_.setCover(app.selected_, image, {});
        const auto selection = app.selected_; const auto scroll = app.grid_scroll_;
        progress.games.back().installation.done = 490;
        app.updateSteamInstallations(progress);
        if (app.selected_ != selection || app.grid_scroll_ != scroll || !app.renderer_.hasCover(selection) || app.cards_[selection].game.installation.percent() != 49) return 133;
        if (!capture(L"steam-install-library.png")) return 134;
        app.openDetails(app.cards_[selection].game);
        if (app.actionLabel(app.cards_[0].game) != L"Pobieranie w Steam") return 135;
        if (!capture(L"steam-install-details.png")) return 136;
        const auto dispatch_count = dispatched.size();
        app.launch(); app.finishLaunch();
        if (dispatched.size() != dispatch_count + 1 || dispatched.back() != L"steam://nav/downloads") return 137;
        // Pobierania (the quick bar after the plugins' pages): the download and its share; the
        // private one stays hidden. Enter opens Steam's downloads; the share follows in place.
        {
            const auto installing = app.cards_[0].game;
            app.openDownloads();
            const auto& rows = app.downloads_state_.rows;
            if (!app.downloadsOpen() || app.downloads_games_.empty() || app.downloads_games_[0].app_id != 70001 || app.downloads_state_.back ||
                rows.empty() || !rows[0].fraction || std::abs(*rows[0].fraction - 0.49f) > 0.001f || !rows[0].moving ||
                !rows[0].amount.empty() || // A few bytes: no sizes below a megabyte.
                std::any_of(app.downloads_games_.begin(), app.downloads_games_.end(), [](const Game& g) { return g.app_id == 70002; })) return 161;
            if (!capture(L"downloads.png")) return 162;
            const auto opened = dispatched.size();
            app.enterDown(false);
            if (dispatched.size() != opened + 1 || dispatched.back() != L"steam://nav/downloads") return 163;
            progress.games.back().installation.done = 800;
            app.updateSteamInstallations(progress); app.refreshDownloads();
            if (!app.downloadsOpen() || app.downloads_state_.rows.empty() || std::abs(*app.downloads_state_.rows[0].fraction - 0.8f) > 0.001f) return 164;
            app.changeCategory(-1);
            if (!app.downloads_state_.back) return 165;
            app.enterDown(false);
            if (app.downloadsOpen()) return 166;
            app.openDetails(installing);
        }
        progress.games.back().installation = {}; progress.games.back().installed = true;
        app.updateSteamInstallations(progress);
        if (!app.cards_[0].game.installed || app.actionLabel(app.cards_[0].game) != L"Graj" || !app.completed_installs_.contains(70001)) return 138;
        if (!capture(L"steam-install-completed.png")) return 139;
        auto stale_install = progress; stale_install.account = 99; stale_install.games.back().installed = false;
        app.updateSteamInstallations(stale_install);
        if (!app.cards_[0].game.installed) return 140;

        Game external; external.kind = GameKind::nonsteam; external.app_id = 12345; external.shortcut_id = 987654321;
        external.name = L"Alan Wake II";
        app.openArtworkBrowser(external);
        // Right reaches the input while the initial gallery is still loading (no assets yet).
        const auto initial_generation = app.artwork_jobs_->generation.load();
        app.navigate(1);
        if (!app.editingArtworkSearch() || !app.artwork_state_.query_selected || app.artwork_state_.gallery) return 168;
        app.navigate(-1);
        if (app.editingArtworkSearch() || app.artwork_jobs_->generation.load() != initial_generation || app.artwork_state_.query != external.name) return 169;
        app.artwork_state_.local_focus = true;
        app.controllerNavigate(1); app.navigate(-1);
        if (app.editingArtworkSearch() || !app.artwork_state_.local_focus || app.artwork_state_.local_open) return 172;
        app.artwork_state_.local_focus = false;
        auto gallery = std::make_unique<ArtworkBrowserMessage>(); gallery->generation = initial_generation;
        gallery->page = parseGridPage(R"({"data":[{"id":77,"width":512,"height":512,"url":"https://example.invalid/a.png"}]})");
        app.onArtworkBrowser(std::move(gallery));
        app.navigate(1); app.changeCategory(1); // Input → Down → loaded gallery.
        if (!app.artwork_state_.gallery || app.editingArtworkSearch()) return 170;
        app.navigate(-1); app.controllerNavigate(1); // Gallery → rail → input via the pad's navigation path.
        if (!app.editingArtworkSearch() || !app.artwork_state_.query_selected) return 171;
        for (const auto c : std::wstring(L"Alan Wake 2")) app.onCharacter(c);
        app.artworkAccept();
        const auto search_generation = app.artwork_jobs_->generation.load();
        auto matches = std::make_unique<ArtworkBrowserMessage>(); matches->type = ArtworkBrowserMessage::Type::search;
        matches->generation = search_generation; matches->search.games = {{{123, false}, L"Alan Wake 2"}, {{456, false}, L"Alan Wake"}};
        app.onArtworkBrowser(std::move(matches));
        if (!capture(L"artwork-search-nonsteam.png")) return 141;
        app.artworkAccept();
        if (gameKey(app.artwork_state_.game) != gameKey(external) || app.artwork_state_.source.id != 123) return 142;
        app.artworkMove(0, 1); // The chosen SGDB game is retained for other artwork kinds.
        if (app.artwork_state_.override_source.id != 123) return 143;
        auto stale_matches = std::make_unique<ArtworkBrowserMessage>(); stale_matches->type = ArtworkBrowserMessage::Type::search;
        stale_matches->generation = search_generation; stale_matches->search.games = {{{999, false}, L"Wrong"}};
        app.onArtworkBrowser(std::move(stale_matches));
        if (app.artwork_state_.source.id != 123) return 144;
        app.closeArtworkBrowser();

        auto fake_gog = std::make_unique<FakeGog>(); auto* account_backend = fake_gog.get(); app.gog_service_ = std::move(fake_gog);
        app.openSettings(); app.settings_state_.selectCategory(static_cast<int>(settings_ui::Category::library));
        app.buildSettingsRows(); app.openPlatformSettings(GameSource::gog); app.gogCommand(gog::Command::connect);
        if (account_backend->commands.back() != gog::Command::connect) return 145;
        gog::Snapshot signing; signing.state = gog::State::signing_in; signing.browser_url = L"https://example.test/authorize";
        account_backend->result = signing; app.pollGog();
        if (dispatched.back() != signing.browser_url || app.gog_state_.state == gog::State::connected) return 146;
        const auto callback_row = std::find_if(app.settings_rows_.begin(), app.settings_rows_.end(), [](const auto& row) { return row.id == settings_ui::Id::gog_callback; });
        if (callback_row == app.settings_rows_.end() || callback_row != app.settings_rows_.begin() + 1 ||
            app.settings_state_.row() != 1 || !app.editingSecret()) return 147;
        app.buildSettingsRows(); // A scan/status repaint must keep the callback editor focused.
        if (!app.editingSecret() || app.settings_state_.row() != 1) return 147;
        app.settings_secret_input_ = L"synthetic-masked-callback";
        if (!capture(L"gog-masked-callback.png")) return 148;
        app.settingsActivate();
        if (account_backend->commands.back() != gog::Command::complete || !app.settings_secret_input_.empty()) return 149;
        gog::Snapshot connected; connected.state = gog::State::connected; connected.account = "11"; connected.synced_at = 1700000000;
        Game owned; owned.kind = GameKind::store; owned.source = GameSource::gog; owned.source_id = "77"; owned.name = L"GOG online fixture"; owned.installed = false;
        connected.games.push_back(owned); account_backend->result = connected; app.pollGog();
        if (!capture(L"gog-account-connected.png")) return 150;
        app.library_state_.bookmark = bookmark; app.library_state_.bookmark->account = app.account_; app.library_state_.bookmark->gog_account = "other";
        app.restore_pending_ = true; app.restorePlace();
        if (app.restore_pending_) return 151;
        auto online_pc = std::make_unique<PcLibraryMessage>(); online_pc->generation = app.pc_token_->load();
        owned.account_id = "11"; online_pc->library.visible.push_back(owned);
        auto local_gog = owned; local_gog.source_id = "88"; local_gog.installed = true; local_gog.account_id.clear(); online_pc->library.visible.push_back(local_gog);
        app.onPcLibrary(std::move(online_pc));
        app.openPcLibrary(GameSource::gog);
        app.openGogInstall(owned);
        gog::InstallSnapshot install; install.product = "77"; install.state = gog::InstallState::ready;
        install.choices = {{"windows_en", L"English", L"1.0", 1024 * 1024}, {"windows_pl", L"Polski", L"1.0", 1024 * 1024}};
        account_backend->installation = install; app.pollGogInstall();
        app.gogInstallKey(VK_RIGHT, false);
        if (!app.gogInstallOpen() || app.gog_install_ui_.variant != 1 || account_backend->installs.size() != 1) return 210;
        if (!capture(L"gog-installer-options.png")) return 211;
        app.gog_install_ui_.row = 3; app.gogInstallAccept();
        if (account_backend->installs.size() != 2 || account_backend->installs.back().variant != "windows_pl") return 212;
        install.state = gog::InstallState::downloading; install.done = 524288; install.total = 1048576;
        account_backend->installation = install; app.pollGogInstall();
        if (!capture(L"gog-installer-downloading.png") || app.gog_install_ui_.snapshot.progress().percent() != 50) return 213;
        app.gogInstallKey(VK_ESCAPE, false);
        const auto download = std::find_if(app.cards_.begin(), app.cards_.end(), [](const GameCard& card) { return card.game.source_id == "77"; });
        if (download == app.cards_.end() || download->game.installation.percent() != 50 || download->game.installed ||
            app.actionLabel(download->game).find(L"Steam") != std::wstring::npos) return 214;
        const auto selected_download = gameKey(app.cards_[app.selected_].game);
        app.openGogInstall(owned);
        install.state = gog::InstallState::installing;
        account_backend->installation = install; app.pollGogInstall();
        if (!capture(L"gog-installer-installing.png") || app.gog_install_ui_.snapshot.progress().percent() ||
            gameKey(app.cards_[app.selected_].game) != selected_download) return 215;
        app.gog_install_ui_.row = 3; app.gogInstallAccept();
        if (account_backend->cancellations) return 216; // Never force-kill the silent installer.
        install.state = gog::InstallState::completed;
        account_backend->installation = install; app.pollGogInstall();
        if (!capture(L"gog-installer-completed.png")) return 217;
        app.gogInstallKey(VK_ESCAPE, false);
        app.gogCommand(gog::Command::logout);
        if (app.store_library_.size() != 1 || app.store_library_[0].source_id != "88" || !app.gog_state_.account.empty()) return 152;
        app.completed_installs_.clear(); app.completed_pc_installs_.clear();
        app.game_sessions_.clear(); app.game_activity_jobs_->active.clear();
        app.library_state_.last_played.clear(); app.gog_state_.account = "history-A";
        Game recent_steam{71001, L"Steam recent fixture"}; recent_steam.last_played = 100;
        Game recent_external{71002, L"External recent fixture"}; recent_external.kind = GameKind::nonsteam;
        recent_external.shortcut_id = 71002; recent_external.last_played = 200;
        Game recent_gog = local_gog; recent_gog.source_id = "71003"; recent_gog.name = L"GOG recent fixture";
        auto recent_hidden = recent_gog; recent_hidden.source_id = "71004"; recent_hidden.last_played = gog::nowSeconds() + 100;
        recent_hidden.visibility = GameVisibility::private_game;
        app.installed_library_ = {recent_steam}; app.shortcut_library_ = {recent_external};
        app.store_library_ = {recent_gog}; app.store_private_library_ = {recent_hidden}; app.uninstalled_library_.clear();
        app.settings_.enabled_pc_sources.insert("gog"); app.buildShelves(); app.showCategory(0);
        const auto selected_before_play = gameKey(app.cards_[app.selected_].game);
        if (app.categories_[0].games.size() != 3 || gameKey(app.categories_[0].games[0]) != gameKey(recent_external)) return 218;
        app.beginGameSession(recent_gog);
        if (!app.library_state_.last_played.empty()) return 219; // Dispatch is not a confirmed start.
        auto running_gog = std::make_unique<GameActivityMessage>(); running_gog->generation = app.game_activity_jobs_->generation.load();
        running_gog->observed_at = GetTickCount64(); running_gog->activities = {{gameKey(recent_gog), GameActivity::running}};
        app.onGameActivity(std::move(running_gog));
        if (app.categories_[0].games.size() != 4 || gameKey(app.categories_[0].games[0]) != gameKey(recent_gog) ||
            gameKey(app.cards_[app.selected_].game) != selected_before_play) return 220;
        for (int i = 0; i < 40; ++i) { app.last_frame_ -= std::chrono::milliseconds(50); app.tick(); }
        app.notifications_.clear();
        if (!capture(L"mixed-recent-dashboard.png")) return 221;
        // Running, the game stays at the front under any account; ended, account B lacks A's history.
        app.gog_state_.account = "history-B"; app.buildShelves();
        if (app.categories_[0].games.size() != 4 || gameKey(app.categories_[0].games[0]) != gameKey(recent_gog)) return 1097;
        app.game_sessions_.clear(); app.buildShelves();
        if (app.categories_[0].games.size() != 3) return 222;
        return 0;
    }
};

#include "battlenet_ui_cases.hpp"
#include "ubisoft_ui_cases.hpp"
#include "web_import_ui_cases.hpp"
#include "epic_ui_cases.hpp"
#include "custom_games_ui_cases.hpp"
#include "file_browser_ui_cases.hpp"
#include "dashboard_ui_cases.hpp"
#include "screensaver_ui_cases.hpp"
#include "controller_ui_cases.hpp"
#include "controller_connections_ui_cases.hpp"
#include "headphone_connections_ui_cases.hpp"
#include "startup_audio_ui_cases.hpp"
#include "library_views_ui_cases.hpp"
#include "artwork_scheduling_ui_cases.hpp"
#include "boundary_shapes_ui_cases.hpp"
#include "focus_motion_ui_cases.hpp"
#include "rawg_ui_cases.hpp"
#include "metro_ui_cases.hpp"
#include "psfive_ui_cases.hpp"
#include "snitch_ui_cases.hpp"
#include "glance_ui_cases.hpp"
#include "tarot_ui_cases.hpp"
#include "low_memory_ui_cases.hpp"
#include "profile_ui_cases.hpp"
#include "aurelia_ui_cases.hpp"
#include "amazon_ui_cases.hpp"
#include "xbox_ui_cases.hpp"
#include "studios_ui_cases.hpp"
#include "update_ui_cases.hpp"
#include "companion_ui_cases.hpp"
#include "companion_semantic_ui_cases.hpp"
#include "companion_social_ui_cases.hpp"
#include "game_index_ui_cases.hpp"
#include "home_widgets_ui_cases.hpp"
#include "ui_refresh_ui_cases.hpp"
#include "desktop_ui_cases.hpp"
#include "desktop_perf_case.hpp"

int LauncherUiTest::xmb(HWND window) {
    Launcher app;
    app.settings_.launch_animation = false;
    app.hwnd_ = window;
    app.execute_ = fakeExecute;
    if (!app.renderer_.initialize(window)) return 200;
    app.settings_.home_layout = AppSettings::HomeLayout::xmb;
    app.settings_.glance = app.settings_.companion = false; // Their tiles: home_widgets_ui_cases.hpp.
    auto library = std::make_unique<LibraryMessage>();
    library->account = 1;
    for (uint32_t i = 0; i < 14; ++i) {
        Game game{2000 + i, L"XMB game " + std::to_wstring(i)};
        game.last_played = 100 + i;
        library->games.push_back(std::move(game));
    }
    for (int i = 0; i < 3; ++i) {
        SteamCollection collection{L"XMB collection " + std::to_wstring(i), {2000u + i, 2001u + i, 2002u + i, 2003u + i, 2004u + i}};
        collection.id = "xmb-" + std::to_string(i);
        library->collections.push_back(std::move(collection));
    }
    app.onLibrary(std::move(library));
    app.showCategory(0);
    const size_t top = app.xmb_top_.size();
    // 8 recent games, "Wszystkie gry", one tile per collection, then Settings last.
    if (!app.xmbActive() || top != 13 || app.xmb_top_[8].kind != GameKind::all ||
        app.xmb_top_[9].kind != GameKind::category || app.xmb_top_.back().kind != GameKind::settings) return 201;
    if (app.cards_.size() != top || app.selected_ != 0) return 202; // A recent game has no column.
    app.navigate(9);
    if (app.xmb_column_ != 9 || app.selected_ != 9 || app.cards_.size() != top + 5 || app.cards_[top].game.app_id != 2000) return 203;
    app.changeCategory(1); app.changeCategory(1);
    if (app.xmb_row_ != 1 || app.selected_ != top + 1) return 204;
    app.navigate(1); // Next collection, first game.
    if (app.xmb_column_ != 10 || app.xmb_row_ != 0 || app.cards_[top].game.app_id != 2001) return 205;
    app.navigate(-1); // Back to the remembered game of the first collection.
    if (app.xmb_column_ != 9 || app.xmb_row_ != 1) return 206;
    app.back();
    if (app.xmb_row_ != -1 || app.selected_ != 9) return 207;
    app.changeCategory(-1);
    if (!app.quick_open_) return 208;
    app.closeQuick();
    app.launch(); // Enter on a collection tile opens its column.
    if (app.xmb_row_ != 0 || app.pending_launch_) return 209;
    auto thumbnail = std::make_unique<CoverMessage>();
    thumbnail->key = gameKey(app.cards_[top].game); thumbnail->generation = app.jobs_->generation.load(); thumbnail->bytes = pixel();
    app.onCover(std::move(thumbnail));
    if (!app.renderer_.hasTileThumbnail(9, 0) || !app.renderer_.hasCover(top)) return 210;
    app.openContextMenu();
    if (!app.menu_open_) return 211;
    app.dismissMenu();
    dispatched.clear();
    app.launch(); app.finishLaunch();
    if (dispatched != std::vector<std::wstring>{L"steam://rungameid/2000"}) return 212;
    for (int i = 0; i < 40; ++i) { app.last_frame_ -= std::chrono::milliseconds(50); app.tick(); }
    // Started from a collection column: its tile leads the recent tiles of the top row, focused.
    if (app.xmb_row_ != -1 || app.selected_ != 0 || app.xmb_top_[0].app_id != 2000 ||
        std::abs(app.cards_[0].x - xmb_layout::anchor_x) > 1) return 213;
    // A library refresh over the same list (startup rescan, PC scan, names) keeps the settled tiles:
    // no second entry wave, no fade of the focused tile's logo.
    {
        const auto before_x = app.cards_[1].x;
        const auto before_count = app.cards_.size();
        app.refreshRecentGames();
        if (app.cards_.size() != before_count || app.cards_[1].opacity < 0.99f || app.cards_[1].x != before_x ||
            app.detail_ < 0.99f || app.arrived_ < 1) return 290;
        // A game new to the row: the kept tiles move aside with their state, only the new one enters.
        Game newest{2100, L"XMB newest"};
        newest.last_played = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count()) + 100000; // After the game just launched.
        app.installed_library_.push_back(newest);
        const auto focused = gameKey(app.cards_[app.selected_].game);
        const auto last = gameKey(app.cards_[7].game); // The 8th recent game: it moves one tile right.
        app.refreshRecentGames();
        const auto added = std::find_if(app.cards_.begin(), app.cards_.begin() + 8, [](const GameCard& card) { return card.game.app_id == 2100; });
        if (added == app.cards_.begin() + 8 || added->opacity != 0 || gameKey(app.cards_[app.selected_].game) != focused ||
            app.cards_[app.selected_].opacity < 0.99f || std::any_of(app.cards_.begin(), app.cards_.begin() + 8, [&](const GameCard& card) {
                return gameKey(card.game) == last; })) return 291;
        // Every other recent tile was kept as it was.
        if (std::any_of(app.cards_.begin(), app.cards_.begin() + 8, [](const GameCard& card) {
                return card.game.app_id != 2100 && card.opacity < 0.99f; })) return 292;
        app.installed_library_.pop_back();
        app.refreshRecentGames(); app.xmbFocus(0, -1);
        for (int i = 0; i < 40; ++i) { app.last_frame_ -= std::chrono::milliseconds(50); app.tick(); }
    }
    wchar_t folder[4096]{};
    const auto folder_length = GetEnvironmentVariableW(L"LAUNCHME_UI_CAPTURE_DIR", folder, 4096);
    if (folder_length && folder_length < 4096 &&
        !app.renderer_.captureForTest(std::filesystem::path(folder) / L"xmb-home.png", [&] { app.paint(); })) return 214;
    app.navigate(100);
    if (app.cards_[app.selected_].game.kind != GameKind::settings) return 215;
    app.launch();
    if (!app.settingsOpen()) return 216;
    app.back();
    if (app.screen_ != ScreenMode::shelves || app.xmb_column_ != top - 1) return 217;
    const auto settle = [&] { for (int i = 0; i < 40; ++i) { app.last_frame_ -= std::chrono::milliseconds(50); app.tick(); } };
    const auto capture = [&](const std::wstring& name) {
        app.notifications_.clear();
        return !folder_length || folder_length >= 4096 ||
            app.renderer_.captureForTest(std::filesystem::path(folder) / name, [&] { app.paint(); });
    };
    app.openSettings(); app.settings_state_.selectCategory(0); app.buildSettingsRows();
    const auto rowIndex = [&](settings_ui::Id id) {
        const auto found = std::find_if(app.settings_rows_.begin(), app.settings_rows_.end(), [&](const auto& row) { return row.id == id; });
        return found == app.settings_rows_.end() ? -1 : static_cast<int>(found - app.settings_rows_.begin());
    };
    // XMB's own options: the layout's tile opens their page.
    if (rowIndex(settings_ui::Id::xmb_posters) >= 0 || !app.revealSetting(settings_ui::Id::xmb_posters) ||
        !app.settings_state_.at(settings_ui::Page::layout_options)) return 219;
    const int posters_row = rowIndex(settings_ui::Id::xmb_posters), size_row = rowIndex(settings_ui::Id::xmb_size);
    if (posters_row != 0 || size_row != posters_row + 1 ||
        app.settings_.xmb_posters || app.settings_.xmb_size != AppSettings::XmbSize::normal) return 219;
    app.settings_state_.enter(static_cast<int>(app.settings_rows_.size()));
    app.settings_state_.selected[0] = posters_row; app.settingsActivate();
    app.settings_state_.selected[0] = size_row; app.settingsActivate();
    app.settingsAdjust(-1);
    if (app.settings_.xmb_size != AppSettings::XmbSize::compact) return 220;
    app.settingsAdjust(1); app.settingsAdjust(1); app.settingsAdjust(1);
    if (app.settings_.xmb_size != AppSettings::XmbSize::large || app.settings_rows_[size_row].level != 1) return 221;
    // Mouse dragging snaps to the same three stops as keyboard/pad and +/- buttons.
    const auto view = app.renderer_.viewport();
    const auto settings_layout = settings_ui::Layout::of(view.width, view.height, app.settings_state_, app.settings_rows_);
    const auto track = settings_layout.track(size_row, app.settings_state_.offset());
    for (int size = 0; size < 3; ++size) {
        app.settingsClick(std::lerp(track.left, track.right, 0.04f + size * 0.46f), (track.top + track.bottom) * 0.5f);
        app.pointerUp();
        if (static_cast<int>(app.settings_.xmb_size) != size || app.settings_rows_[size_row].level != size / 2.0f) return 222;
    }
    app.saveSettings();
    if (!loadAppSettings().xmb_posters || loadAppSettings().xmb_size != AppSettings::XmbSize::large ||
        !capture(L"settings-xmb-posters-size.png")) return 223;
    app.settings_state_.closeAll(); app.settings_state_.options = app.settings_state_.editing = false; app.back(); app.xmbFocus(0, -1); settle();
    const auto focused_key = gameKey(app.cards_[app.selected_].game);
    if (std::none_of(app.jobs_->secondary.begin(), app.jobs_->secondary.end(), [&](const auto& job) {
        return job.kind == CoverMessage::Kind::poster && gameKey(job.game) == focused_key;
    })) return 224;
    // Feed real portrait fixtures through the artwork-result path, including duplicates in a column.
    for (const auto& game : app.public_library_) {
        for (const auto kind : {CoverMessage::Kind::cover, CoverMessage::Kind::poster}) {
            auto artwork = std::make_unique<CoverMessage>();
            artwork->key = gameKey(game); artwork->generation = app.jobs_->generation.load(); artwork->kind = kind;
            artwork->bytes = pngFixture(static_cast<uint8_t>(40 + (game.app_id % 14) * 12), 80, kind == CoverMessage::Kind::poster ? 120 : 80);
            app.onCover(std::move(artwork));
        }
    }
    if (!app.renderer_.hasPoster(app.selected_)) return 225;
    for (const bool posters : {false, true}) for (int size = 0; size < 3; ++size) {
        app.settings_.xmb_posters = posters; app.settings_.xmb_size = static_cast<AppSettings::XmbSize>(size);
        app.rebuildXmb(false); app.xmbFocus(0, -1); settle();
        const auto& recent = app.cards_[0];
        const float expected_width = 150.0f * (0.8f + size * 0.2f);
        if (std::abs(recent.width() - expected_width) > 1 ||
            std::abs(recent.size - expected_width * (posters ? 1.5f : 1.0f)) > 1 || app.cards_[8].aspect != 1) return 226;
        if (!capture(std::wstring(posters ? L"xmb-posters-" : L"xmb-squares-") + std::to_wstring(size) + L".png")) return 227;
        app.xmbFocus(9, 0); settle();
        const auto& active = app.cards_[top];
        const auto next = app.cards_[top + 1];
        if (active.y + active.size >= next.y || (posters && !app.renderer_.hasPoster(top))) return 228;
        if (std::abs(active.y - (app.cards_[9].y + app.cards_[9].size + 58)) > 1) return 233;
        if (posters && !capture(L"xmb-posters-column-" + std::to_wstring(size) + L".png")) return 229;
        app.mouse_transition_until_ = 0;
        app.click((next.x + next.width() * 0.5f) * view.scale, (next.y + next.size - 5) * view.scale, false);
        if (app.selected_ != top + 1 || app.cardHit(top + 1, next.x + next.width() + 5, next.y + next.size * 0.5f)) return 230;
    }
    app.openArtworkBrowser(app.cards_[app.selected_].game);
    if (app.artwork_state_.kind() != ArtworkKind::poster) return 231;
    app.closeArtworkBrowser();
    app.openSettings(); app.settings_state_.selectCategory(0);
    for (const int layout : {0, 2}) {
        app.setHomeLayout(layout);
        if (rowIndex(settings_ui::Id::xmb_posters) != -1 || rowIndex(settings_ui::Id::xmb_size) != -1 ||
            !app.settings_.xmb_posters || app.settings_.xmb_size != AppSettings::XmbSize::large) return 232;
        // Their options (shelves' and Flix's) are behind the tile under the layout.
        if (rowIndex(settings_ui::Id::flix_size) != -1 || rowIndex(settings_ui::Id::page) != rowIndex(settings_ui::Id::home_layout) + 1) return 234;
    }
    // Flix: its own size row with two stops (default, large), for keys and the mouse alike.
    {
        if (!app.revealSetting(settings_ui::Id::flix_size)) return 235;
        const int flix_row = rowIndex(settings_ui::Id::flix_size);
        if (flix_row != 0 || app.settings_.flix_size != AppSettings::FlixSize::normal ||
            app.settings_rows_[flix_row].level != 0) return 235;
        app.settings_state_.enter(static_cast<int>(app.settings_rows_.size()));
        app.settings_state_.selected[0] = flix_row; app.settingsActivate();
        app.settingsAdjust(1);
        if (app.settings_.flix_size != AppSettings::FlixSize::large || app.settings_rows_[flix_row].level != 1) return 236;
        app.settingsAdjust(1);
        if (app.settings_.flix_size != AppSettings::FlixSize::large) return 237;
        app.settingsAdjust(-1);
        if (app.settings_.flix_size != AppSettings::FlixSize::normal || app.settings_rows_[flix_row].level != 0) return 238;
        const auto flix_track = settings_layout.track(flix_row, app.settings_state_.offset());
        app.settingsClick(std::lerp(flix_track.left, flix_track.right, 0.9f), (flix_track.top + flix_track.bottom) * 0.5f);
        app.pointerUp();
        if (app.settings_.flix_size != AppSettings::FlixSize::large || !capture(L"settings-flix-size.png")) return 239;
        app.settings_state_.options = app.settings_state_.editing = false;
        app.settings_.flix_size = AppSettings::FlixSize::normal;
    }
    app.setHomeLayout(0);
    app.showCategory(0);
    if (app.xmbActive() || app.cards_.size() != app.categories_[0].games.size()) return 218;
    return 0;
}

// Experimental quick resume with a fake process backend: no real program is
// ever suspended, resumed or focused. Real suspension is covered by game_session_tests.
int LauncherUiTest::quickResume(HWND window) {
    std::vector<std::string> log;
    QuickResumeBackend backend;
    const auto describe = [](const char* verb, const std::vector<GameProcess>& list) {
        return std::string(verb) + ':' + (list.empty() ? std::string("-") : std::to_string(list[0].pid));
    };
    backend.find = [](const Game& game) { return game.app_id ? std::vector<GameProcess>{{game.app_id, 7}} : std::vector<GameProcess>{}; };
    backend.suspend = [&](std::vector<GameProcess>& list) { log.push_back(describe("suspend", list)); return process_freeze::Result::ok; };
    backend.resume = [&](const std::vector<GameProcess>& list) { log.push_back(describe("resume", list)); return process_freeze::Result::ok; };
    backend.terminate = [&](const std::vector<GameProcess>& list) { log.push_back(describe("terminate", list)); return process_freeze::Result::ok; };
    backend.alive = [](const std::vector<GameProcess>&) { return true; };
    backend.focus = [&](const std::vector<GameProcess>& list) { log.push_back(describe("focus", list)); return true; };
    Launcher app;
    app.settings_.launch_animation = false;
    app.hwnd_ = window; app.execute_ = fakeExecute; app.quick_resume_ = backend;
    if (!app.renderer_.initialize(window)) return 300;
    auto library = std::make_unique<LibraryMessage>(); library->account = 1;
    for (uint32_t i = 0; i < 3; ++i) { Game game{3000 + i, L"Quick resume " + std::to_wstring(i)}; game.last_played = 300 - i; library->games.push_back(game); }
    app.onLibrary(std::move(library));
    app.showCategory(0);
    const auto focus = [&](uint32_t app_id) {
        for (size_t i = 0; i < app.cards_.size(); ++i) if (app.cards_[i].game.app_id == app_id) { app.focusCard(i); return true; }
        return false;
    };
    const auto running = [&](uint64_t time, std::vector<std::string> keys) {
        auto message = std::make_unique<GameActivityMessage>();
        message->generation = app.game_activity_jobs_->generation.load(); message->observed_at = time;
        for (auto& key : keys) message->activities.emplace_back(std::move(key), GameActivity::running);
        app.onGameActivity(std::move(message));
    };
    const auto capture = [&](const wchar_t* name) {
        wchar_t folder[4096]{};
        const auto count = GetEnvironmentVariableW(L"LAUNCHME_UI_CAPTURE_DIR", folder, 4096);
        if (!count || count >= 4096) return true;
        for (int i = 0; i < 40; ++i) { app.last_frame_ -= std::chrono::milliseconds(50); app.tick(); }
        app.notifications_.clear();
        return app.renderer_.captureForTest(std::filesystem::path(folder) / name, [&] { app.paint(); });
    };
    for (size_t i = 0; i < app.cards_.size(); ++i) if (isGame(app.cards_[i].game)) {
        const auto cover = shareImage(pngFixture(static_cast<uint8_t>(60 + i * 50)));
        const auto key = gameKey(app.cards_[i].game);
        app.art_.at(key).cover = cover; app.art_.resize(key);
        app.renderer_.setCover(i, cover, {});
    }
    const Game first = app.cards_[0].game, second = app.cards_[1].game;
    if (first.app_id != 3000 || second.app_id != 3001) return 301;

    // Details of an installed game with a known directory offer "Otwórz folder" (independent of quick resume).
    Game located = first; located.install_directory = userConfigDirectory() / L"folder-fixture";
    app.openDetails(located);
    if (app.lastDetailAction() != 3 || app.favoriteDetailAction() != 2) return 323;
    app.detail_action_ = 1; app.enterDown(false); app.enterUp();
    if (!app.menu_open_ || context_menu::row(context_menu::Item::folder, app.menuRows()) != 1) return 323;
    app.menuMove(1);
    if (!capture(L"details-open-folder.png")) return 324;
    dispatched.clear(); app.launch();
    if (app.menu_open_ || app.detail_action_ != 1 || dispatched != std::vector<std::wstring>{located.install_directory.wstring()}) return 325;
    app.back();
    app.openDetails(first); // No directory known: the slot stays hidden.
    if (app.lastDetailAction() != 3 || app.favoriteDetailAction() != 2 || context_menu::row(context_menu::Item::folder, app.menuRows()) != -1) return 326;
    app.back(); app.showCategory(0);
    if (!focus(3000)) return 327;

    // Off by default: a running game shows nothing new and the menu is unchanged.
    dispatched.clear();
    app.launch(); app.finishLaunch();
    running(GetTickCount64(), {"steam:3000"});
    if (app.sessionState(first) != SessionState::none || app.menuRows().suspend) return 302;
    app.settings_.quick_resume = true;
    if (app.sessionState(first) != SessionState::running || app.actionLabel(first) != L"Wróć") return 303;
    app.openContextMenu();
    const auto live = app.menuRows();
    // "Zamknij grę" takes the place of "Ponów grafiki" while the game runs.
    if (!live.suspend || context_menu::item(1, live) != context_menu::Item::suspend || context_menu::row(context_menu::Item::retry, live) != -1 ||
        context_menu::item(context_menu::rows(live) - 1, live) != context_menu::Item::close || context_menu::rows(live) != 5) return 304;
    if (!capture(L"quick-resume-running.png")) return 305;
    app.menu_selected_ = 1; app.menuAccept();
    if (app.sessionState(first) != SessionState::frozen || log != std::vector<std::string>{"suspend:3000"} ||
        app.actionLabel(first) != L"Wznów" || app.audio_.music_target_ <= 0) return 306; // A suspended game lets the menu music play.
    const auto persisted = process_freeze::load(app.quickResumeFile());
    if (!persisted || persisted->size() != 1 || (*persisted)[0].key != "steam:3000" || (*persisted)[0].processes != std::vector<GameProcess>{{3000, 7}}) return 307;
    app.openContextMenu();
    if (app.menuRows().suspend) return 308; // Resuming is the primary action.
    if (!capture(L"quick-resume-frozen.png")) return 309;
    app.dismissMenu();

    // Starting another game keeps the first one suspended.
    if (!focus(3001)) return 310;
    app.launch(); app.finishLaunch();
    if (dispatched.back() != L"steam://rungameid/3001" || app.audio_.music_target_ != 0) return 311;
    running(GetTickCount64() + 100, {"steam:3000", "steam:3001"});
    // Resuming the first one suspends the running second one first.
    log.clear();
    if (!focus(3000)) return 312;
    const auto launches = dispatched.size();
    app.launch();
    if (app.pending_launch_ || dispatched.size() != launches ||
        log != std::vector<std::string>{"suspend:3001", "resume:3000", "focus:3000"} ||
        app.sessionState(first) != SessionState::running || app.sessionState(second) != SessionState::frozen) return 313;
    // "Graj" on the running game switches to it instead of starting it again.
    log.clear(); app.launch();
    if (app.pending_launch_ || dispatched.size() != launches || log != std::vector<std::string>{"focus:3000"}) return 314;
    // "Zamknij grę" ends the running game at once; the suspended one stays.
    log.clear(); app.openContextMenu();
    app.menu_selected_ = context_menu::row(context_menu::Item::close, app.menuRows()); app.menuAccept();
    if (log != std::vector<std::string>{"terminate:3000"} || app.game_sessions_.contains("steam:3000") ||
        app.sessionState(second) != SessionState::frozen || app.actionLabel(first) != L"Graj" || app.audio_.music_target_ <= 0) return 322;

    // Closing asks first; Esc cancels, "keep" closes and leaves the list for the next start.
    if (!app.requestClose() || !app.exitPromptOpen()) return 315;
    if (!capture(L"quick-resume-exit.png")) return 316;
    const auto exit_view = app.renderer_.viewport();
    const exit_prompt::Layout exit_layout{exit_view.width, exit_view.height};
    const auto exit_panel = exit_layout.panel(), cancel_button = exit_layout.button(2);
    const float exit_x = (exit_panel.left + exit_panel.right) * 0.5f;
    if (app.hoverButton(exit_x * exit_view.scale, (exit_panel.top + 42) * exit_view.scale) ||
        !app.hoverButton(exit_x * exit_view.scale, (cancel_button.top + 22) * exit_view.scale)) return 1771;
    app.click(exit_x * exit_view.scale, (exit_panel.top + 42) * exit_view.scale, false);
    if (!app.exitPromptOpen()) return 1772; // The question is not an action.
    app.controllerNavigate(2); // D-pad down selects the next stacked button.
    if (*app.exit_prompt_ != static_cast<int>(exit_prompt::Choice::keep)) return 1773;
    app.controllerNavigate(-2);
    if (*app.exit_prompt_ != static_cast<int>(exit_prompt::Choice::resume)) return 1774;
    app.gog_install_ui_.open = true; // The exit modal has priority over an open installation panel.
    app.click(exit_x * exit_view.scale, (cancel_button.top + 22) * exit_view.scale, false);
    app.gog_install_ui_.open = false;
    if (app.exitPromptOpen() || app.closing_ || app.sessionState(second) != SessionState::frozen) return 1775;
    app.requestClose();
    app.exitPromptKey(VK_ESCAPE, false);
    if (app.exitPromptOpen() || app.closing_) return 317;
    app.requestClose(); app.exitPromptKey(VK_DOWN, false); app.exitPromptKey(VK_RETURN, false);
    MSG close{};
    if (app.exitPromptOpen() || !app.closing_ || !app.requestClose() || PeekMessageW(&close, window, WM_CLOSE, WM_CLOSE, PM_REMOVE) ||
        app.sessionState(second) != SessionState::frozen || process_freeze::load(app.quickResumeFile()) !=
            std::vector<FrozenGame>{{gameKey(second), {{second.app_id, 7}}}}) return 318;
    app.tickClose(app.close_started_ + std::chrono::milliseconds(1100));
    if (app.requestClose() || !PeekMessageW(&close, window, WM_CLOSE, WM_CLOSE, PM_REMOVE)) return 328;

    // A new launcher adopts the suspended game; turning the feature off resumes it.
    Launcher next;
    next.hwnd_ = window; next.execute_ = fakeExecute; next.quick_resume_ = backend;
    if (!next.renderer_.initialize(window)) return 319;
    next.settings_.quick_resume = true;
    next.restoreFrozenGames();
    if (::sessionState(next.game_sessions_, "steam:3001") != SessionState::frozen || next.game_sessions_.size() != 1) return 320;
    log.clear();
    next.openSettings();
    next.settings_state_.selectCategory(static_cast<int>(settings_ui::Category::system));
    next.buildSettingsRows();
    {
        const auto resume_row = std::find_if(next.settings_rows_.begin(), next.settings_rows_.end(),
                                             [](const auto& row) { return row.id == settings_ui::Id::quick_resume; });
        if (resume_row == next.settings_rows_.end()) return 321;
        next.settingsActivate();
        next.settings_state_.selected[next.settings_state_.category] = static_cast<int>(resume_row - next.settings_rows_.begin());
        next.settingsActivate();
    }
    if (next.settings_.quick_resume || next.game_sessions_.begin()->second.frozen() ||
        log != std::vector<std::string>{"resume:3001"} || std::filesystem::exists(next.quickResumeFile())) return 321;
    // Steam Controller row (Kontrolery): toggles the setting both ways. The controller input was never
    // initialized with a window here, so no HID reader starts and no hardware is touched.
    next.settings_state_.selectCategory(static_cast<int>(settings_ui::Category::controllers));
    next.buildSettingsRows();
    next.settings_state_.options = true;
    const auto steam_row = std::find_if(next.settings_rows_.begin(), next.settings_rows_.end(),
                                        [](const auto& row) { return row.id == settings_ui::Id::steam_controller; });
    if (steam_row == next.settings_rows_.end() || steam_row->checked || steam_row->control != settings_ui::Control::toggle) return 330;
    next.settings_state_.selected[next.settings_state_.category] = static_cast<int>(steam_row - next.settings_rows_.begin());
    next.settingsActivate();
    if (!next.settings_.steam_controller || steam_controller::active()) return 331;
    next.settingsActivate();
    if (next.settings_.steam_controller) return 332;

    // A game started elsewhere (from Steam, before the launcher) is taken up as running. The
    // scan itself (findRunningGames) is covered by game_session_tests; no worker runs here.
    Launcher third;
    third.hwnd_ = window; third.execute_ = fakeExecute; third.quick_resume_ = backend;
    if (!third.renderer_.initialize(window)) return 333;
    third.settings_.quick_resume = true;
    auto elsewhere = std::make_unique<LibraryMessage>(); elsewhere->account = 1;
    for (uint32_t i = 0; i < 2; ++i) elsewhere->games.push_back(Game{3100 + i, L"Started elsewhere " + std::to_wstring(i)});
    ShortcutGame emulated; emulated.game = Game{0x80001234u, L"Emulated"}; emulated.game.kind = GameKind::nonsteam;
    emulated.game.executable = L"C:\\Emulators\\emulator.exe"; // Shared by many shortcuts: never looked for.
    elsewhere->shortcuts.push_back(emulated);
    third.onLibrary(std::move(elsewhere));
    if (!third.running_version_ || third.running_scan_->candidates.size() != 2 ||
        std::any_of(third.running_scan_->candidates.begin(), third.running_scan_->candidates.end(), [](const Game& game) { return game.kind == GameKind::nonsteam; })) return 334;
    const Game external = third.running_scan_->candidates[0];
    const auto report = [&](uint32_t version) {
        auto message = std::make_unique<RunningGamesMessage>(); message->version = version; message->games = {external};
        third.onRunningGames(std::move(message));
    };
    report(third.running_version_ + 1); // Made from an older candidate list.
    if (!third.game_sessions_.empty()) return 335;
    report(third.running_version_);
    if (third.sessionState(external) != SessionState::running || third.actionLabel(external) != L"Wróć" ||
        third.audio_.music_target_ != 0 || !third.game_activity_jobs_->active.contains(gameKey(external))) return 336;
    report(third.running_version_); // Reported again while it runs: still one session.
    if (third.game_sessions_.size() != 1) return 337;
    // It ends like a game started here: after 1.5 s without its processes, the music comes back.
    const auto stopped = [&](uint64_t time) {
        auto message = std::make_unique<GameActivityMessage>();
        message->generation = third.game_activity_jobs_->generation.load(); message->observed_at = time;
        message->activities.emplace_back(gameKey(external), GameActivity::stopped);
        third.onGameActivity(std::move(message));
    };
    const auto now = GetTickCount64();
    stopped(now); stopped(now + 1600);
    if (!third.game_sessions_.empty() || third.audio_.music_target_ <= 0) return 338;
    return 0;
}

// Power menu, Guide and Start gestures. The power backend is a fake: this test
// can never sleep, restart or shut down the machine.
int LauncherUiTest::power(HWND window) {
    std::vector<PowerAction> requested;
    Launcher app;
    app.hwnd_ = window; app.execute_ = fakeExecute;
    app.power_actions_ = [] { return std::vector<PowerAction>{PowerAction::sleep, PowerAction::restart, PowerAction::shutdown}; };
    app.power_ = [&](PowerAction action) { requested.push_back(action); return action != PowerAction::shutdown; };
    if (!app.renderer_.initialize(window)) return 400;
    auto library = std::make_unique<LibraryMessage>(); library->account = 1;
    for (uint32_t i = 0; i < 3; ++i) library->games.push_back(Game{4000 + i, L"Power fixture " + std::to_wstring(i)});
    app.onLibrary(std::move(library));
    app.showCategory(0);
    // Quick bar "Zasilanie" opens the menu instead of closing the launcher at once.
    app.openQuick();
    app.quick_selected_ = quick_menu::count(static_cast<int>(app.plugin_menu_.size())) - 1;
    app.quickAccept();
    MSG close{};
    if (!app.powerMenuOpen() || app.quickOpen() || PeekMessageW(&close, window, WM_CLOSE, WM_CLOSE, PM_NOREMOVE) ||
        app.power_items_.size() != 6) return 401;
    // "Wyjdź z trybu TV" is first, then "Zamknij launcher"; keyboard Down ×3 + Enter = "Uruchom ponownie"
    // through the injected backend.
    if (app.power_items_.front() != power_menu::Item::tv_mode || app.power_items_[1] != power_menu::Item::exit || app.power_menu_ != 0) return 401;
    if (std::wstring(power_menu::label(power_menu::Item::tv_mode)) != L"Wyjdź z trybu TV" ||
        std::wstring(power_menu::label(power_menu::Item::tv_mode, true)) != L"Przejdź do trybu TV") return 401;
    app.powerMenuKey(VK_DOWN, false); app.powerMenuKey(VK_DOWN, false); app.powerMenuKey(VK_DOWN, false); app.powerMenuKey(VK_RETURN, false);
    if (app.powerMenuOpen() || requested != std::vector<PowerAction>{PowerAction::restart}) return 402;
    // A refused command becomes a notification, not a silent no-op.
    app.openPowerMenu(); app.power_menu_ = 4; app.powerMenuAccept();
    if (requested.back() != PowerAction::shutdown || app.notifications_.empty()) return 403;
    // Esc cancels; "Zamknij launcher" uses the normal WM_CLOSE path.
    app.openPowerMenu(); app.powerMenuKey(VK_ESCAPE, false);
    if (app.powerMenuOpen()) return 404;
    app.openPowerMenu(); app.power_menu_ = 1; app.powerMenuAccept();
    if (!PeekMessageW(&close, window, WM_CLOSE, WM_CLOSE, PM_REMOVE) || requested.size() != 2) return 405;
    // Home/Guide: a short press from Settings returns to the home row with the quick bar.
    app.openSettings();
    app.homeDown(false); app.homeUp();
    if (app.settingsOpen() || app.screen_ != ScreenMode::shelves || !app.quickOpen() || app.category_ != 0) return 406;
    app.homeDown(false); app.homeUp(); // Toggle.
    if (app.quickOpen()) return 407;
    // Holding Home opens the power menu while the key is still down; the release is silent.
    app.homeDown(false);
    app.home_key_.update(true, GetTickCount64() + HoldGesture::threshold_ms + 1);
    app.homeUp();
    if (app.powerMenuOpen() || app.quickOpen()) return 408; // Hold was consumed by update() above, not by pollHoldGestures.
    app.homeDown(false);
    Sleep(static_cast<DWORD>(HoldGesture::threshold_ms + 20));
    app.pollHoldGestures(); app.homeUp();
    if (!app.powerMenuOpen() || app.quickOpen()) return 409;
    app.closePowerMenu();
    // TAB/Start short toggles friends; hold opens the audio panel instead.
    app.tabDown(false); app.tabUp();
    if (!app.friendsOpen()) return 410;
    app.tabDown(false); app.tabUp();
    if (app.friendsOpen()) return 411;
    app.tabDown(false);
    Sleep(static_cast<DWORD>(HoldGesture::threshold_ms + 20));
    app.pollHoldGestures(); app.tabUp();
    if (!app.devicesOpen() || app.device_state_.panel != devices::Panel::audio || app.friendsOpen()) return 412;
    // Bright artwork behind the power menu: the panel must blur the already dimmed scene.
    app.back(); app.openDetails(app.installed_library_.front());
    auto backdrop = std::make_unique<CoverMessage>(); backdrop->generation = app.jobs_->generation.load();
    backdrop->key = gameKey(app.cards_[0].game); backdrop->kind = CoverMessage::Kind::hero;
    backdrop->bytes = pngFixture(255, 128, 72); app.onCover(std::move(backdrop));
    app.detail_ = 1; app.paint(); Sleep(360); app.openPowerMenu();
    wchar_t folder[4096]{};
    if (GetEnvironmentVariableW(L"LAUNCHME_UI_CAPTURE_DIR", folder, 4096) &&
        !app.renderer_.captureForTest(std::filesystem::path(folder) / L"power-menu.png", [&] { app.paint(); })) return 413;
    return 0;
}

// Settings -> Appearance -> Language switches the whole UI at once, without a restart:
// settings rows and rail, shelf titles, tiles, stand-in names, quick bar and menus.
int LauncherUiTest::language(HWND window) {
    Launcher app;
    app.hwnd_ = window; app.execute_ = fakeExecute;
    app.power_actions_ = [] { return std::vector<PowerAction>{PowerAction::sleep, PowerAction::hibernate, PowerAction::restart, PowerAction::shutdown}; };
    app.power_ = [](PowerAction) { return true; };
    if (!app.renderer_.initialize(window)) return 500;
    const auto capture = [&](const wchar_t* name) {
        wchar_t folder[4096]{};
        const auto count = GetEnvironmentVariableW(L"LAUNCHME_UI_CAPTURE_DIR", folder, 4096);
        if (!count || count >= 4096) return true;
        InvalidateRect(window, nullptr, FALSE);
        return app.renderer_.captureForTest(std::filesystem::path(folder) / name, [&] { app.paint(); });
    };
    auto library = std::make_unique<LibraryMessage>(); library->account = 1;
    for (uint32_t i = 0; i < 3; ++i) {
        Game game{5000 + i, L"Language fixture " + std::to_wstring(i)};
        game.last_played = 100 + i;
        library->games.push_back(std::move(game));
    }
    app.onLibrary(std::move(library));
    // An uninstalled collection title whose name is not known yet (no metadata worker here).
    Game pending{5100, tr(loading_game_name)};
    pending.installed = false;
    app.uninstalled_library_.push_back(pending);
    app.buildShelves(); app.showCategory(0);
    const auto stand_in = [&] {
        for (const auto& game : app.public_library_) if (game.app_id == 5100) return game.name;
        return std::wstring{};
    };
    if (i18n::language() != i18n::Language::pl || stand_in() != L"Ładowanie nazwy…" ||
        app.categories_[0].title != L"Ostatnio grane" || app.settings_.language != AppSettings::LanguageChoice::system) return 501;
    const auto choose = [&](int option) {
        app.openSettings();
        app.settings_state_.selectCategory(static_cast<int>(settings_ui::Category::system));
        app.buildSettingsRows();
        const auto row = std::find_if(app.settings_rows_.begin(), app.settings_rows_.end(),
            [](const settings_ui::Row& item) { return item.id == settings_ui::Id::language; });
        if (row == app.settings_rows_.end() || row->choices.size() != 5 || row->choices[1].label != L"Polski" ||
            row->choices[2].label != L"English" || row->choices[3].label != L"Portugu\u00eas (Brasil)" || row->choices[4].label != L"Espa\u00f1ol") return false;
        app.settings_state_.enter(static_cast<int>(app.settings_rows_.size()));
        app.settings_state_.selected[app.settings_state_.category] = static_cast<int>(row - app.settings_rows_.begin());
        app.settingsActivate(); // Opens the picker.
        if (!app.settings_state_.editing) return false;
        app.settings_state_.choice = option;
        app.settingsActivate();
        return !app.settings_state_.editing;
    };
    if (!choose(2)) return 502;
    if (i18n::language() != i18n::Language::en || app.settings_.language != AppSettings::LanguageChoice::en) return 503;
    // Rows and the rail change while Settings stays open.
    const auto english_row = [&](settings_ui::Id id) -> const settings_ui::Row* {
        const auto found = std::find_if(app.settings_rows_.begin(), app.settings_rows_.end(), [&](const auto& row) { return row.id == id; });
        return found == app.settings_rows_.end() ? nullptr : &*found;
    };
    const auto* language_row = english_row(settings_ui::Id::language);
    if (app.settings_rows_.size() < 2 || !language_row || language_row->title != L"Language" || language_row->value != L"English" ||
        app.settings_rows_[0].id != settings_ui::Id::update || app.settings_rows_[0].section != L"Updates" || app.settings_rows_[0].value != L"Check now" ||
        !english_row(settings_ui::Id::windowed) || english_row(settings_ui::Id::windowed)->title != L"TV mode in a window" ||
        english_row(settings_ui::Id::windowed)->section != L"Window and language" ||
        app.settings_rows_.back().title != L"Walkthrough" || app.settings_rows_.back().value != L"Show" ||
        !english_row(settings_ui::Id::about) || english_row(settings_ui::Id::about)->value != L"Open folder" ||
        std::wstring(tr(settings_ui::categories[0].title)) != L"Interface" ||
        std::wstring(tr(settings_ui::categories[static_cast<int>(settings_ui::Category::media)].title)) != L"Media & data" ||
        std::wstring(tr(settings_ui::categories[static_cast<int>(settings_ui::Category::companion)].title)) != L"Assistant" ||
        std::wstring(tr(settings_ui::categories[static_cast<int>(settings_ui::Category::system)].title)) != L"System") return 504;
    if (!capture(L"settings-english-system.png")) return 505;
    app.settingsActivate(); // Language picker: "System", "Polski", "English".
    if (!capture(L"settings-english-language.png")) return 506;
    app.back();
    app.settings_state_.selectCategory(static_cast<int>(settings_ui::Category::appearance));
    app.buildSettingsRows();
    {
        const auto* font_row = english_row(settings_ui::Id::font);
        if (!font_row || font_row->title != L"Font" || app.settings_rows_[0].section != L"TV mode") return 504;
        if (!capture(L"settings-english-appearance.png")) return 505;
    }
    for (const auto category : {settings_ui::Category::library, settings_ui::Category::media, settings_ui::Category::audio,
                                settings_ui::Category::controllers, settings_ui::Category::plugins, settings_ui::Category::companion, settings_ui::Category::system}) {
        app.settings_state_.selectCategory(static_cast<int>(category));
        app.buildSettingsRows();
        const std::wstring name = L"settings-english-" + std::to_wstring(static_cast<int>(category)) + L".png";
        if (!capture(name.c_str())) return 507;
    }
    // Shelves, tiles, stand-in names and fixed labels.
    if (app.categories_[0].title != L"Recently played" || app.category_titles_[0] != L"Recent" || stand_in() != L"Loading name…" ||
        std::wstring(quick_menu::caption(quick_menu::Action::settings)) != L"Settings" ||
        std::wstring(quick_menu::caption(quick_menu::Action::library)) != L"All Games" ||
        std::wstring(quick_menu::caption(quick_menu::Action::widgets)) != L"Widgets" ||
        std::wstring(power_menu::label(power_menu::Item::sleep)) != L"Sleep" ||
        std::wstring(power_menu::label(power_menu::Item::shutdown)) != L"Shut down PC" ||
        std::wstring(power_menu::label(power_menu::Item::exit)) != L"Close launcher") return 508;
    app.settings_state_.options = false;
    app.back();
    if (app.settingsOpen() || app.cards_.empty() || app.actionLabel(app.installed_library_[0]) != L"Play") return 509;
    const auto all = std::find_if(app.cards_.begin(), app.cards_.end(), [](const GameCard& card) { return card.game.kind == GameKind::all; });
    if (all == app.cards_.end() || all->game.name != L"All Games") return 510;
    const auto settle = [&] { // No frame clock here: place the shelf cards where tick() would.
        for (size_t i = 0; i < app.cards_.size(); ++i) {
            auto& card = app.cards_[i];
            card.size = i == app.selected_ ? 330.0f : 165.0f;
            card.x = 57.0f + (i > app.selected_ ? 185.0f : 0.0f) + i * 185.0f;
            card.y = 735.0f - card.size - (app.quick_open_ ? 100.0f : 0.0f);
            card.opacity = 1;
        }
    };
    app.openQuick(); app.quick_progress_ = 1; settle();
    if (!capture(L"home-english-quick.png")) return 511;
    app.closeQuick(); app.quick_progress_ = 0;
    app.focusCard(0); settle(); app.openContextMenu(); app.menu_progress_ = 1;
    if (!app.menuOpen() || !capture(L"home-english-menu.png")) return 512;
    app.dismissMenu(); app.menu_progress_ = 0;
    app.openDetails(app.cards_[0].game);
    if (app.screen_ != ScreenMode::details || !capture(L"details-english.png")) return 513;
    app.back();
    app.openPowerMenu();
    if (!capture(L"power-english.png")) return 514;
    app.closePowerMenu();
    app.openLibrary(); app.focusLibraryControls(); app.library_menu_progress_ = 1;
    if (app.screen_ != ScreenMode::library || !app.library_controls_.open() || !capture(L"library-english.png")) return 519;
    app.back(); app.back();
    if (app.library_controls_.open() || app.library_controls_.focus != -1) return 522;
    auto fake = std::make_unique<FakeDevices>();
    fake->data.audio[0] = {0.4f, true, false, L"out"}; fake->data.audio[1] = {0.7f, true, true, L"in"};
    fake->data.endpoints = {{L"out", L"Speakers", false, true}, {L"in", L"Microphone", true, true}};
    app.device_service_ = std::move(fake); app.openDevices(devices::Panel::audio); app.pollDevices();
    if (!app.devicesOpen() || !capture(L"audio-english.png")) return 520;
    app.openDevices(devices::Panel::controllers); app.pollDevices();
    if (!capture(L"controllers-english.png")) return 521;
    app.back();
    // Portuguese (Brazil) and Spanish: the same rows, the home's labels and Details in each.
    for (const auto& [option, language, tag] : {std::tuple{3, i18n::Language::pt_br, L"portuguese"}, std::tuple{4, i18n::Language::es, L"spanish"}}) {
        if (!choose(option) || i18n::language() != language) return 530;
        const auto* row = english_row(settings_ui::Id::language);
        if (!row || row->title != i18n::translate(L"Język", language) || row->title == L"Language" ||
            stand_in() != i18n::translate(L"Ładowanie nazwy…", language) ||
            app.actionLabel(app.installed_library_[0]) != i18n::translate(L"Graj", language)) return 531;
        if (!capture((L"settings-" + std::wstring(tag) + L"-system.png").c_str())) return 532;
        app.settings_state_.options = false;
        app.back(); app.focusCard(0); settle();
        if (!capture((L"home-" + std::wstring(tag) + L".png").c_str())) return 533;
        app.openDetails(app.cards_[0].game);
        if (!capture((L"details-" + std::wstring(tag) + L".png").c_str())) return 534;
        app.back();
    }
    // And back to Polish from the same row.
    if (!choose(1)) return 515;
    // Aurelia adds a section: assert the language/window rows by identity, not ordinal.
    const auto* polish_language = english_row(settings_ui::Id::language);
    const auto* polish_window = english_row(settings_ui::Id::windowed);
    if (i18n::language() != i18n::Language::pl || !polish_language || !polish_window ||
        app.settings_rows_[0].section != L"Aktualizacje" || app.settings_rows_[0].value != L"Sprawdź teraz" ||
        polish_window->section != L"Okno i język" || polish_window->title != L"Tryb TV w oknie" ||
        app.categories_[0].title != L"Ostatnio grane" || stand_in() != L"Ładowanie nazwy…" ||
        app.actionLabel(app.installed_library_[0]) != L"Graj") return 516;
    // "Systemowy" follows the Windows display language.
    if (!choose(0) || i18n::language() != i18n::systemLanguage()) return 517;
    app.saveSettings();
    if (loadAppSettings().language != AppSettings::LanguageChoice::system) return 518;
    i18n::setLanguage(i18n::Language::pl);
    return 0;
}

#include "plugins_ui_cases.hpp"
#if __has_include("local_ui_cases.hpp")
#include "local_ui_cases.hpp"
#endif
#include "settings_design_ui_cases.hpp"
#include "settings_devices_ui_cases.hpp"
#include "platform_settings_ui_cases.hpp"
#include "pc_refresh_ui_cases.hpp"
#include "drm_free_ui_cases.hpp"
#include "quick_resume_failure_ui_cases.hpp"
#include "game_discovery_ui_cases.hpp"
#include "game_overlay_ui_cases.hpp"
#include "game_focus_ui_cases.hpp"
#include "tray_ui_cases.hpp"
#include "recent_sessions_ui_cases.hpp"
#include "keyboard_ui_cases.hpp"
#include "private_games_ui_cases.hpp"
#include "steam_visibility_ui_cases.hpp"
#include "bulk_covers_ui_cases.hpp"
#include "exit_ui_cases.hpp"
#include "screen_transitions_ui_cases.hpp"
#include "tour_ui_cases.hpp"
#include "launch_animation_ui_cases.hpp"
#include "organization_ui_cases.hpp"
#include "excluded_games_ui_cases.hpp"
#include "uninstall_ui_cases.hpp"
#include "manual_games_ui_cases.hpp"
#include "hltb_ui_cases.hpp"
#include "plugin_achievements_ui_cases.hpp"
#include "render_layering_ui_cases.hpp"
#include "modal_layers_ui_cases.hpp"

int main(int argc, char** argv) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    // The checks below compare Polish labels; never depend on this machine's display language.
    i18n::setLanguage(i18n::Language::pl);
    const auto root = std::filesystem::temp_directory_path() / (L"launchme-ui-test-" + std::to_wstring(GetCurrentProcessId()));
    struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code ec; std::filesystem::remove_all(path, ec); } } cleanup{root};
    SetEnvironmentVariableW(L"LAUNCHME_CONFIG_DIR", root.c_str());
    SetEnvironmentVariableW(L"STEAM_DIR", (root / L"steam").c_str()); // Never scan the real client's supplementary library.
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    WNDCLASSW type{};
    type.lpfnWndProc = DefWindowProcW;
    type.hInstance = GetModuleHandleW(nullptr);
    type.lpszClassName = L"LaunchmeUiRegression";
    RegisterClassW(&type);
    wchar_t duration[16]{};
    const bool profiling = GetEnvironmentVariableW(L"LAUNCHME_PERF_SECONDS", duration, 16) > 0;
    const int width = profiling ? GetSystemMetrics(SM_CXSCREEN) : 1600, height = profiling ? GetSystemMetrics(SM_CYSCREEN) : 900;
    const auto window = CreateWindowW(type.lpszClassName, L"Test", WS_POPUP, 0, 0, width, height,
                                      nullptr, nullptr, type.hInstance, nullptr);
    if (argc == 2 && (std::string_view(argv[1]) == "--glance" || std::string_view(argv[1]) == "--glance-live")) {
        std::filesystem::create_directories(root);
        const auto code = window ? LauncherUiTest::glanceHub(window, std::string_view(argv[1]) == "--glance-live") : 99;
        if (window) DestroyWindow(window);
        CoUninitialize();
        if (code) std::cerr << "Glance regression failed at checkpoint " << code << '\n';
        else std::cout << "Glance: shortcut-only entry, pad, navigation, async sources, settings and hints passed\n";
        return code;
    }
    if (argc == 2 && (std::string_view(argv[1]) == "--companion-social" || std::string_view(argv[1]) == "--companion-semantic" || std::string_view(argv[1]) == "--companion" || std::string_view(argv[1]) == "--companion-appearance" || std::string_view(argv[1]) == "--companion-reply-length" || std::string_view(argv[1]) == "--companion-links" || std::string_view(argv[1]) == "--companion-paste" || std::string_view(argv[1]) == "--companion-remote")) {
        std::filesystem::create_directories(root);
        const auto code = window ? (std::string_view(argv[1]) == "--companion-social" ? LauncherUiTest::companionSocialDynamics(window) :
            std::string_view(argv[1]) == "--companion-semantic" ? LauncherUiTest::companionSemanticMemory(window) :
            std::string_view(argv[1]) == "--companion-paste" ? LauncherUiTest::companionPaste(window) :
            std::string_view(argv[1]) == "--companion-remote" ? LauncherUiTest::companionRemote(window) :
            std::string_view(argv[1]) == "--companion-links" ? LauncherUiTest::companionLinks(window) :
            std::string_view(argv[1]) == "--companion-reply-length" ? LauncherUiTest::companionReplyLength(window) :
            std::string_view(argv[1]) == "--companion-appearance" ? LauncherUiTest::companionAppearance(window) : LauncherUiTest::companionChat(window)) : 99;
        if (window) DestroyWindow(window);
        CoUninitialize();
        if (code) std::cerr << "Companion regression failed at checkpoint " << code << '\n';
        else std::cout << "Companion: home tile, greeting, L2 chat, chips, typing, launch, offline and settings passed\n";
        return code;
    }
    if (argc == 2 && std::string_view(argv[1]) == "--game-index") {
        std::filesystem::create_directories(root);
        const auto code = window ? LauncherUiTest::gameKnowledge(window) : 99;
        if (window) DestroyWindow(window);
        CoUninitialize();
        if (code) std::cerr << "Game index regression failed at checkpoint " << code << '\n';
        else std::cout << "Game index: library, cached pages, Szczegóły, release order, genre search, her tool and memory passed\n";
        return code;
    }
    if (argc == 2 && std::string_view(argv[1]) == "--tarot") {
        std::filesystem::create_directories(root);
        const auto code = window ? LauncherUiTest::tarotReading(window) : 99;
        if (window) DestroyWindow(window);
        CoUninitialize();
        if (code) std::cerr << "Tarot regression failed at checkpoint " << code << '\n';
        else std::cout << "Tarot: setting, T and View, deal, back, library and store readings, results, pad and mouse passed\n";
        return code;
    }
    if (argc == 2 && std::string_view(argv[1]) == "--low-memory") {
        std::filesystem::create_directories(root);
        const auto code = window ? LauncherUiTest::lowMemory(window) : 99;
        if (window) DestroyWindow(window);
        CoUninitialize();
        if (code) std::cerr << "Low memory regression failed at checkpoint " << code << '\n';
        else std::cout << "Low memory: setting, decode size, BC1, bytes let go, rebinding, larger draws, heroes, device loss, hidden and back passed\n";
        return code;
    }
    if (argc == 2 && std::string_view(argv[1]) == "--profile") {
        std::filesystem::create_directories(root);
        const auto code = window ? LauncherUiTest::profilePage(window) : 99;
        if (window) DestroyWindow(window);
        CoUninitialize();
        if (code) std::cerr << "Profile regression failed at checkpoint " << code << '\n';
        else std::cout << "Profile: drawer widget, week, achievements, history, platforms, keys, pad, mouse, desktop widgets, Czas gry and MultiMC passed\n";
        return code;
    }
    if (argc == 2 && std::string_view(argv[1]) == "--xbox") {
        std::filesystem::create_directories(root);
        const auto code = window ? LauncherUiTest::xboxAccount(window) : 99;
        if (window) DestroyWindow(window);
        CoUninitialize();
        if (code) std::cerr << "Xbox account UI failed at checkpoint " << code << '\n';
        else std::cout << "Xbox account: helper offer, sign-in, Xbox app hand-off, own install dialog, uninstall and launch passed\n";
        return code;
    }
    if (argc == 2 && std::string_view(argv[1]) == "--amazon") {
        std::filesystem::create_directories(root);
        const auto code = window ? LauncherUiTest::amazonGames(window) : 99;
        if (window) DestroyWindow(window);
        CoUninitialize();
        if (code) std::cerr << "Amazon Games UI failed at checkpoint " << code << '\n';
        else std::cout << "Amazon Games: Nile offer, sign-in address, library, install dialog, uninstall and launch passed\n";
        return code;
    }
    if (argc == 2 && std::string_view(argv[1]) == "--aurelia") {
        std::filesystem::create_directories(root);
        const auto code = window ? LauncherUiTest::aureliaIntegration(window) : 99;
        if (window) DestroyWindow(window);
        CoUninitialize();
        if (code) std::cerr << "Aurelia UI failed at checkpoint " << code << '\n';
        else std::cout << "Aurelia: onboarding, setting, cloud conflicts, DLC, versions, downloads, live chat and desktop/TV passed\n";
        return code;
    }
    if (argc == 2 && std::string_view(argv[1]) == "--studios") {
        std::filesystem::create_directories(root);
        const auto code = window ? LauncherUiTest::studioLibraries(window) : 99;
        if (window) DestroyWindow(window);
        CoUninitialize();
        if (code) std::cerr << "Studios regression failed at checkpoint " << code << '\n';
        else std::cout << "Studios: logo rows, board, studio libraries, hide/move/switch, desktop sections, pane link, search, Ctrl+K, gold edge and TV metadata passed\n";
        return code;
    }
    if (argc == 2 && std::string_view(argv[1]) == "--tray") {
        std::filesystem::create_directories(root);
        const auto code = window ? LauncherUiTest::trayAndBackgroundCrop(window) : 99;
        if (window) DestroyWindow(window);
        CoUninitialize();
        if (code) std::cerr << "Tray regression failed at checkpoint " << code << '\n';
        else std::cout << "Tray while playing, background crop, artwork page cache and the empty view passed\n";
        return code;
    }
    if (argc == 2 && std::string_view(argv[1]) == "--updates") {
        std::filesystem::create_directories(root);
        const auto code = window ? LauncherUiTest::updates(window) : 99;
        if (window) DestroyWindow(window);
        CoUninitialize();
        if (code) std::cerr << "Updates regression failed at checkpoint " << code << '\n';
        else std::cout << "Updates: check, notification, row, desktop icon, download, swap, restart, failures and Co nowego passed\n";
        return code;
    }
    if (argc == 2 && std::string_view(argv[1]) == "--home-widgets") {
        std::filesystem::create_directories(root);
        const auto code = window ? LauncherUiTest::homeWidgets(window) : 99;
        if (window) DestroyWindow(window);
        CoUninitialize();
        if (code) std::cerr << "Home widgets regression failed at checkpoint " << code << '\n';
        else std::cout << "Home widgets: Glance and Towarzyszka on Metro, XMB, PSfive and Flix passed\n";
        return code;
    }
    if (argc == 2 && std::string_view(argv[1]) == "--ui-refresh") {
        std::filesystem::create_directories(root);
        const auto code = window ? LauncherUiTest::uiRefresh(window) : 99;
        if (window) DestroyWindow(window);
        CoUninitialize();
        if (code) std::cerr << "Interface refresh regression failed at checkpoint " << code << '\n';
        else std::cout << "Interface refresh: home facts, search artwork, Kinowy column, power groups, entrance, buttons, focus gap and Flix icons passed\n";
        return code;
    }
    if (argc == 2 && std::string_view(argv[1]) == "--desktop") {
        std::filesystem::create_directories(root);
        const auto code = window ? LauncherUiTest::desktop(window) : 99;
        if (window) DestroyWindow(window);
        CoUninitialize();
        if (code) std::cerr << "Desktop mode regression failed at checkpoint " << code << '\n';
        else std::cout << "Desktop mode: sections, search, marking, collections, Ustawienia, Szczegóły and Tryb TV passed\n";
        return code;
    }
    if (argc == 2 && std::string_view(argv[1]) == "--launch-animation") {
        std::filesystem::create_directories(root);
        const auto code = window ? LauncherUiTest::launchAnimation(window) : 99;
        if (window) DestroyWindow(window);
        CoUninitialize();
        if (code) std::cerr << "CD loading regression failed at checkpoint " << code << '\n';
        else std::cout << "CD loading: launch timing, input, audio, rendering, failure and opt-out passed\n";
        return code;
    }
    if (argc == 2 && std::string_view(argv[1]) == "--file-browser") {
        std::filesystem::create_directories(root);
        const auto code = window ? LauncherUiTest::fileBrowserAndColors(window) : 99;
        if (window) DestroyWindow(window);
        CoUninitialize();
        if (code) std::cerr << "File picker / colour regression failed at checkpoint " << code << '\n';
        else std::cout << "File picker, colour editor and glow settings passed\n";
        return code;
    }
    if (argc == 2 && std::string_view(argv[1]) == "--recent-sessions") {
        std::filesystem::create_directories(root);
        auto code = window ? LauncherUiTest::recentSessions(window) : 99;
        if (!code) code = LauncherUiTest::onscreenKeyboard(window);
        if (!code) code = LauncherUiTest::launchAnimation(window);
        if (window) DestroyWindow(window);
        CoUninitialize();
        if (code) std::cerr << "Recent sessions regression failed at checkpoint " << code << '\n';
        else std::cout << "Recent: Steam updates, games in play first, focus on start, background continuity passed\n";
        return code;
    }
    if (argc == 2 && std::string_view(argv[1]) == "--menus") {
        std::filesystem::create_directories(root);
        auto code = window ? LauncherUiTest::privateGames(window) : 99;
        if (!code) code = LauncherUiTest::steamVisibility(window);
        if (!code) code = LauncherUiTest::power(window);
        if (window) DestroyWindow(window);
        CoUninitialize();
        if (code) std::cerr << "Menus regression: " << code << '\n';
        return code;
    }
    if (argc == 2 && std::string_view(argv[1]) == "--bulk-covers") {
        std::filesystem::create_directories(root);
        const auto code = window ? LauncherUiTest::bulkCovers(window) : 99;
        if (window) DestroyWindow(window);
        CoUninitialize();
        if (code) std::cerr << "Bulk covers regression: " << code << '\n';
        return code;
    }
    if (argc == 2 && std::string_view(argv[1]) == "--organization") {
        std::filesystem::create_directories(root);
        auto code = window ? LauncherUiTest::organization(window) : 99;
        if (!code) code = LauncherUiTest::excludedGames(window);
        if (window) DestroyWindow(window);
        CoUninitialize();
        if (code) std::cerr << "Organization regression: " << code << '\n';
        return code;
    }
    if (argc == 2 && std::string_view(argv[1]) == "--manual-games") {
        std::filesystem::create_directories(root);
        const auto code = window ? LauncherUiTest::manualGames(window) : 99;
        if (window) DestroyWindow(window);
        CoUninitialize();
        if (code) std::cerr << "Games added by hand regression: " << code << '\n';
        else std::cout << "Games added by hand: no file, poster, identify, remove from library passed\n";
        return code;
    }
    if (argc == 2 && std::string_view(argv[1]) == "--uninstall") {
        std::filesystem::create_directories(root);
        const auto code = window ? LauncherUiTest::uninstallGames(window) : 99;
        if (window) DestroyWindow(window);
        CoUninitialize();
        if (code) std::cerr << "Uninstall regression: " << code << '\n';
        return code;
    }
    if (argc == 2 && std::string_view(argv[1]) == "--plugin-library") {
        std::filesystem::create_directories(root);
        const auto code = window ? LauncherUiTest::pluginLibrarySupplements(window) : 99;
        if (window) DestroyWindow(window);
        CoUninitialize();
        if (code) std::cerr << "Plugin library supplement regression: " << code << '\n';
        return code;
    }
    if (argc == 2 && std::string_view(argv[1]) == "--snitch") {
        std::filesystem::create_directories(root);
        const auto code = window ? LauncherUiTest::snitch(window) : 99;
        if (window) DestroyWindow(window);
        CoUninitialize();
        if (code) std::cerr << "snitch home layout regression: " << code << '\n';
        else std::cout << "snitch: recent row, library, dock, pointer, settings and empty state passed\n";
        return code;
    }
    if (argc == 2 && std::string_view(argv[1]) == "--psfive") {
        std::filesystem::create_directories(root);
        const auto code = window ? LauncherUiTest::psfive(window) : 99;
        if (window) DestroyWindow(window);
        CoUninitialize();
        if (code) std::cerr << "PSfive home layout regression failed at checkpoint " << code << '\n';
        else std::cout << "PSfive: row, pinned focus, buttons, game menu, quick bar, mouse and settings passed\n";
        return code;
    }
    if (argc == 2 && std::string_view(argv[1]) == "--plugin-achievements") {
        std::filesystem::create_directories(root);
        const auto code = window ? LauncherUiTest::pluginAchievements(window) : 99;
        if (window) DestroyWindow(window);
        CoUninitialize();
        if (code) std::cerr << "Plugin achievements regression: " << code << '\n';
        else std::cout << "Plugin achievements: provider, counts, list, icons, refresh and none passed\n";
        return code;
    }
    if (argc == 2 && std::string_view(argv[1]) == "--hltb-details") {
        std::filesystem::create_directories(root);
        auto code = window ? LauncherUiTest::hltbDetails(window) : 99;
        if (!code) code = LauncherUiTest::gameLengths(window);
        if (window) DestroyWindow(window);
        CoUninitialize();
        if (code) std::cerr << "HLTB details/widget regression: " << code << '\n';
        return code;
    }
    if (argc == 2 && std::string_view(argv[1]) == "--library-views") {
        std::filesystem::create_directories(root);
        auto code = window ? renderLayering(window) : 99;
        if (!code) code = LauncherUiTest::libraryViews(window);
        if (!code) code = LauncherUiTest::artworkScheduling(window);
        if (window) DestroyWindow(window);
        CoUninitialize();
        if (code) std::cerr << "Library presentation regression failed at checkpoint " << code << '\n';
        else std::cout << "Library views including Coverflow: navigation, motion, artwork, actions and saved views passed\n";
        return code;
    }
    if (argc == 2 && std::string_view(argv[1]) == "--screen-transitions") {
        std::filesystem::create_directories(root);
        const auto code = window ? LauncherUiTest::screenTransitions(window) : 99;
        if (window) DestroyWindow(window);
        CoUninitialize();
        if (code) std::cerr << "Screen transition regression failed at checkpoint " << code << '\n';
        else std::cout << "Screen transition toggle, hero reuse and stationary wallpaper passed\n";
        return code;
    }
    if (argc == 2 && std::string_view(argv[1]) == "--exit-animation") {
        std::filesystem::create_directories(root);
        auto code = window ? LauncherUiTest::exitTransition(window) : 99;
        if (!code) code = LauncherUiTest::quickResume(window);
        if (window) DestroyWindow(window);
        CoUninitialize();
        if (code) std::cerr << "Exit animation regression failed at checkpoint " << code << '\n';
        else std::cout << "Exit timing, repeated close, suspended games, descending light and final black frame passed\n";
        return code;
    }
    if (argc == 2 && std::string_view(argv[1]) == "--guide-overlay") {
        std::filesystem::create_directories(root);
        auto code = window ? LauncherUiTest::gameOverlay(window) : 99;
        if (!code) code = LauncherUiTest::gameFocus(window);
        if (window) DestroyWindow(window);
        CoUninitialize();
        if (code) std::cerr << "Guide overlay regression failed at checkpoint " << code << '\n';
        else std::cout << "Guide overlay keyboard, controller, focus and animation regression passed\n";
        return code;
    }
    if (argc == 2 && std::string_view(argv[1]) == "--drm-free") {
        std::filesystem::create_directories(root);
        const auto code = window ? LauncherUiTest::drmFree(window) : 99;
        if (window) DestroyWindow(window);
        CoUninitialize();
        if (code) std::cerr << "DRM-free/artwork regression failed at checkpoint " << code << '\n';
        else std::cout << "DRM-free library, artwork search, square/background crop and Back routing passed\n";
        return code;
    }
    auto code = window ? (profiling ? LauncherUiTest::profile(window, std::clamp(_wtoi(duration), 3, 120)) : LauncherUiTest::run(window)) : 99;
    if (!code && !profiling) code = LauncherUiTest::xmb(window);
    if (!code && !profiling) code = renderLayering(window);
    if (!code && !profiling) code = LauncherUiTest::quickResume(window);
    if (!code && !profiling) code = LauncherUiTest::exitTransition(window);
    if (!code && !profiling) code = LauncherUiTest::screenTransitions(window);
    if (!code && !profiling) code = LauncherUiTest::quickResumeFailures(window);
    if (!code && !profiling) code = LauncherUiTest::runningCandidates(window);
    if (!code && !profiling) code = LauncherUiTest::gameOverlay(window);
    if (!code && !profiling) code = LauncherUiTest::gameFocus(window);
    if (!code && !profiling) code = LauncherUiTest::recentSessions(window);
    if (!code && !profiling) code = LauncherUiTest::onscreenKeyboard(window);
    if (!code && !profiling) code = LauncherUiTest::battlenet(window);
    if (!code && !profiling) code = LauncherUiTest::ubisoft(window);
    if (!code && !profiling) code = LauncherUiTest::webImports(window);
    if (!code && !profiling) code = LauncherUiTest::epicAccount(window);
    if (!code && !profiling) code = LauncherUiTest::power(window);
    if (!code && !profiling) code = LauncherUiTest::modalLayers(window);
    if (!code && !profiling) code = LauncherUiTest::language(window);
    if (!code && !profiling) code = LauncherUiTest::customGames(window);
    if (!code && !profiling) code = LauncherUiTest::manualGames(window);
    if (!code && !profiling) code = LauncherUiTest::dashboard(window);
    if (!code && !profiling) code = LauncherUiTest::screensaver(window);
    if (!code && !profiling) code = LauncherUiTest::controllerFamilies(window);
    if (!code && !profiling) code = LauncherUiTest::controllerConnections(window);
    if (!code && !profiling) code = LauncherUiTest::headphoneConnections(window);
    if (!code && !profiling) code = LauncherUiTest::startupAudio(window);
    if (!code && !profiling) code = LauncherUiTest::libraryViews(window);
    if (!code && !profiling) code = LauncherUiTest::artworkScheduling(window);
    if (!code && !profiling) code = LauncherUiTest::libraryStateRecovery(window);
    if (!code && !profiling) code = LauncherUiTest::flix(window);
    if (!code && !profiling) code = LauncherUiTest::metro(window);
    if (!code && !profiling) code = LauncherUiTest::psfive(window);
    if (!code && !profiling) code = LauncherUiTest::categoryRail(window);
    if (!code && !profiling) code = LauncherUiTest::boundaryAndShapes(window);
    if (!code && !profiling) code = LauncherUiTest::focusMotion(window);
    if (!code && !profiling) code = LauncherUiTest::rawgFlow(window);
#if __has_include("local_ui_cases.hpp")
    if (!code && !profiling) code = LauncherUiTest::localCases(window);
#endif
    if (!code && !profiling) code = LauncherUiTest::settingsDesign(window);
    if (!code && !profiling) code = LauncherUiTest::settingsDevices(window);
    if (!code && !profiling) code = LauncherUiTest::pcSettings(window);
    if (!code && !profiling) code = LauncherUiTest::pcRefresh(window);
    if (!code && !profiling) code = LauncherUiTest::privateGames(window);
    if (!code && !profiling) code = LauncherUiTest::steamVisibility(window);
    if (!code && !profiling) code = LauncherUiTest::bulkCovers(window);
    if (!code && !profiling) code = LauncherUiTest::drmFree(window);
    if (!code && !profiling) code = LauncherUiTest::tour(window);
    if (!code && !profiling) code = LauncherUiTest::organization(window);
    if (!code && !profiling) code = LauncherUiTest::excludedGames(window);
    if (!code && !profiling) code = LauncherUiTest::uninstallGames(window);
    if (window) DestroyWindow(window);
    CoUninitialize();
    if (code) std::cerr << "Launcher UI regression failed at checkpoint " << code << '\n';
    else if (!profiling) std::cout << "Settings, Store, details, devices and priority artwork prefetch passed (mock executors)\n";
    return code;
}
