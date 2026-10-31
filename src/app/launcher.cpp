#include "launcher.hpp"
#include "launcher_internal.hpp"
#include "../platform/diagnostics.hpp"
#include "../platform/text_encoding.hpp"
#include "../ui/carousel.hpp"
#include "../ui/category_menu.hpp"
#include "../ui/category_rail.hpp"
#include "../ui/grid_layout.hpp"
#include "../ui/drawer_clock.hpp"

#include <algorithm>
#include <cmath>
#ifndef _WIN32
#include "../linux/window/window_system.hpp"
#include "../platform/atomic_file.hpp"
#include "../platform/system.hpp"
#include "../platform/updater.hpp"
#include <cstdlib>
#include <fstream>
#endif

namespace {
std::wstring monitorName(HMONITOR monitor) {
    MONITORINFOEXW info{}; info.cbSize = sizeof(info);
    return monitor && GetMonitorInfoW(monitor, &info) ? std::wstring(info.szDevice) : std::wstring{};
}
HMONITOR monitorNamed(const std::wstring& name) {
    if (name.empty()) return nullptr;
    struct Search { const std::wstring* name; HMONITOR found = nullptr; } search{&name};
    EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR monitor, HDC, LPRECT, LPARAM data) -> BOOL {
        auto* s = reinterpret_cast<Search*>(data);
        if (monitorName(monitor) == *s->name) { s->found = monitor; return FALSE; }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&search));
    return search.found;
}
}

// Tryb TV covers the monitor it covered last time (a TV beside the desk monitor), when present.
void Launcher::placeOnTvMonitor() {
    if (!coversMonitor()) return;
    const HMONITOR monitor = monitorNamed(settings_.tv_monitor);
    MONITORINFO info{sizeof(info)};
    if (!monitor || !GetMonitorInfoW(monitor, &info)) return;
    const RECT& r = info.rcMonitor;
    SetWindowPos(hwnd_, nullptr, r.left, r.top, r.right - r.left, r.bottom - r.top, SWP_NOZORDER | SWP_NOACTIVATE);
}

// Ustawienia → System → Uruchamiaj z systemem Windows: the user's own Run entry (no service, no
// task, no elevation), started minimized. Also rewritten at start, should the EXE have moved.
bool Launcher::applyAutostart(bool enabled) {
#ifdef LAUNCHME_UI_TEST
    (void)enabled;
    return true; // Tests never change what starts with Windows.
#elif !defined(_WIN32)
    // The desktop's autostart entry ($XDG_CONFIG_HOME/autostart, XDG Autostart), started minimized.
    const char* config = std::getenv("XDG_CONFIG_HOME");
    const char* home = std::getenv("HOME");
    std::filesystem::path folder = config && *config == '/' ? std::filesystem::path(config) :
                                   home && *home ? std::filesystem::path(home) / ".config" : std::filesystem::path{};
    if (folder.empty()) return false;
    folder /= "autostart";
    const auto file = folder / "launchme.desktop";
    std::error_code error;
    if (!enabled) { std::filesystem::remove(file, error); return !error; }
    // The AppImage itself when run as one (its mount point changes with every start).
    const auto program = updater::appImage().value_or(system_info::executablePath());
    if (program.empty()) return false;
    // Exec's quoting (Desktop Entry Specification): ", `, $ and \ escaped (\ twice, as the string
    // escape applies first), % doubled.
    std::string exec = "\"";
    for (const char c : program.string()) {
        if (c == '\\') exec += "\\\\\\\\";
        else if (c == '"' || c == '`' || c == '$') { exec += '\\'; exec += c; }
        else if (c == '%') exec += "%%";
        else exec += c;
    }
    exec += "\" --minimized";
    std::filesystem::create_directories(folder, error);
    const auto staged = folder / ".launchme.desktop.new";
    {
        std::ofstream out(staged, std::ios::binary | std::ios::trunc);
        out << "[Desktop Entry]\nType=Application\nName=launchme\nComment=Console-style game launcher\n"
            << "Exec=" << exec << "\nIcon=io.github.uxandai.launchme\nTerminal=false\nCategories=Game;\n"
            << "X-GNOME-Autostart-enabled=true\n";
        if (!out.flush()) { out.close(); std::filesystem::remove(staged, error); return false; }
    }
    if (atomic_file::replace(staged, file)) return true;
    std::filesystem::remove(staged, error);
    return false;
#else
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, nullptr, 0,
                        KEY_SET_VALUE | KEY_QUERY_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS) return false;
    bool ok = true;
    if (enabled) {
        wchar_t module[MAX_PATH * 2]{};
        const DWORD length = GetModuleFileNameW(nullptr, module, static_cast<DWORD>(std::size(module)));
        const std::wstring command = L"\"" + std::wstring(module, length) + L"\" --minimized";
        ok = length && RegSetValueExW(key, L"Launchme", 0, REG_SZ, reinterpret_cast<const BYTE*>(command.c_str()),
                                      static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;
    } else {
        const auto status = RegDeleteValueW(key, L"Launchme");
        ok = status == ERROR_SUCCESS || status == ERROR_FILE_NOT_FOUND;
    }
    RegCloseKey(key);
    return ok;
#endif
}

void Launcher::rememberTvMonitor() {
    if (!coversMonitor() || !hwnd_ || IsIconic(hwnd_)) return;
    const auto name = monitorName(MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST));
    if (name.empty() || name == settings_.tv_monitor) return;
    settings_.tv_monitor = name;
    settings_dirty_ = true; settings_save_failed_ = false; settings_edited_ = std::chrono::steady_clock::now();
}

Launcher::~Launcher() {
    browser_.reset(); // Close WebView/its callbacks before any Launcher state is destroyed.
    { std::lock_guard lock(store_gateway_->mutex); store_gateway_->gog = nullptr; store_gateway_->epic = nullptr; } // The plugin worker may outlive us.
    glance_cancel_->store(true);
    companion_.cancel->store(true);
    if (companion_.semantic) companion_.semantic->stop();
    if (bulk_control_) bulk_control_->cancel = true;
    if (hwnd_) { KillTimer(hwnd_, guide_timer); if (guide_hotkey_active_) UnregisterHotKey(hwnd_, guide_hotkey); }
    removeTrayIcon(); // Left behind, it would stay in the notification area until hovered.
    overlay_monitor_.stop();
    overlay_target_.reset();
    game_overlay_.reset();
    controller_input_.stopRumble();
    ratings_alive_->store(false);
    ++*identify_token_;
    if (drm_stop_) drm_stop_->store(true);
    if (playtime_dirty_) library_state_dirty_ = true;
    profile_cancel_->store(true);
    saveJournal(); // The profile's week: the last minutes of a game still running.
    desktopSaveState(); rememberDesktopWindow(); rememberTvMonitor(); // The window's section, selection and placement.
    rememberPlace(); saveLibraryPreferences();
    if (settings_dirty_) saveSettings();
    alive_->store(false);
    if (remote_) remote_->stop(); // Phone access: its requests waiting on this thread give up.
    steam_rescan_->wake(); pc_watch_wake_->wake(); steam_apps_wake_->wake(); // Workers sleeping on change notifications.
    controller_rescan_->wake(); headphones_rescan_->wake();
    if (hwnd_) KillTimer(hwnd_, foreground_scan_timer);
    if (game_index_) game_index_->stop(); // Saves the index on its way out.
    volume_monitor_.stop();
    friends_token_->store(UINT32_MAX);
    metadata_token_->store(UINT32_MAX);
    pc_token_->store(UINT32_MAX);
    { std::lock_guard lock(jobs_->mutex); }
    jobs_->wake.notify_all();
    { std::lock_guard lock(steam_sync_queue_->mutex); }
    steam_sync_queue_->wake.notify_all();
    store_jobs_->wake.notify_all();
    { std::lock_guard lock(plugin_jobs_->mutex); }
    plugin_jobs_->wake.notify_all();
    artwork_jobs_->wake.notify_all();
    music_jobs_->wake.notify_all();
    game_activity_jobs_->wake.notify_all();
    { std::lock_guard lock(running_scan_->mutex); } // A scan between its check and its wait still gets the wake-up.
    running_scan_->wake.notify_all();
    preview_.stop();
    { std::lock_guard lock(achievement_jobs_->mutex); achievement_jobs_->generation.fetch_add(1); achievement_jobs_->jobs.clear(); }
    achievement_jobs_->wake.notify_all();
    stopDashboardPoll();
    {
        std::lock_guard lock(saver_jobs_->mutex);
        saver_jobs_->generation.fetch_add(1);
    }
    saver_jobs_->wake.notify_all();
    // Workers poll `alive` at most every ~100 ms outside blocking I/O. The window is
    // already gone here, so this wait is invisible; a worker stuck in a network
    // read longer than the budget is released as before.
    work_.shutdown(std::chrono::milliseconds(1500));
}

bool Launcher::initialize(HWND hwnd) {
    hwnd_ = hwnd;
    fullscreen_ = true;
    const int windowed_width = 1540, windowed_height = 870;
    const int x = (GetSystemMetrics(SM_CXSCREEN) - windowed_width) / 2;
    const int y = (GetSystemMetrics(SM_CYSCREEN) - windowed_height) / 2;
    previous_rect_ = RECT{x, y, x + windowed_width, y + windowed_height};
    settings_ = loadAppSettings();
    initializeAurelia();
    companionApplyHomes(); // Where they live, before anything reads their clocks.
    // The desktop window unless Interfejs → "Uruchamiaj w trybie TV" asks for Tryb TV.
    desktop_mode_ = !settings_.launch_tv_mode;
    audio_.setDesktop(desktop_mode_);
    if (desktop_mode_) {
        desktop_section_ = settings_.desktop_section.empty() ? std::string("all") : settings_.desktop_section;
        desktop_restore_key_ = settings_.desktop_game;
        constexpr const char* groups[] = {"library", "platforms", "emulation", "collections", "publishers", "developers"};
        for (int i = 0; i < desktop_ui::group_count; ++i) // Wydawcy and Developerzy: folded until opened.
            desktop_collapsed_[static_cast<size_t>(i)] = desktop_ui::studioGroup(static_cast<desktop_ui::Group>(i)) ?
                !settings_.desktop_expanded.contains(groups[i]) : settings_.desktop_collapsed.contains(groups[i]);
    } else if (settings_.windowed) { toggleFullscreen(); settings_dirty_ = false; }
    else placeOnTvMonitor();
    if (settings_.autostart) applyAutostart(true); // The EXE may have moved since it was switched on.
    audio_.setMusicLevel(settings_.music_volume);
    initializeMusic(); // Decode boot first, in parallel with renderer and library startup.
    if (!renderer_.initialize(hwnd)) {
        diagnostics::log("error", "Renderer initialization failed (Direct2D/DirectWrite/WIC): graphics driver or system components");
        return false;
    }
    profiler_.configure(); renderer_.profile(profiler_.enabled());
    applyLanguage(); // Before any row, shelf or tile text is built.
    {
        wchar_t module[MAX_PATH * 2]{};
        GetModuleFileNameW(nullptr, module, static_cast<DWORD>(std::size(module)));
        diagnostics::log("info", L"Start " + diagnostics::version() + L" " + module);
    }
    if (settings_.load_failed) {
        diagnostics::log("error", "settings.ini could not be read; defaults in use");
        showNotification(NotificationKind::error, tr(L"Nie można odczytać ustawień"),
            tr(L"Używane są domyślne. Dotychczasowy plik zostanie zachowany jako settings.unreadable.ini."));
    }
    // A crash in the previous run: where its report is, and a plugin that caused it is switched off.
    if (const auto crash = diagnostics::takeUnseenCrash()) {
        const auto packages = (userConfigDirectory() / L"plugins" / L"packages").lexically_normal().wstring();
        const auto module = std::filesystem::path(crash->module).lexically_normal().wstring();
        if (!packages.empty() && module.size() > packages.size() &&
            CompareStringOrdinal(module.c_str(), static_cast<int>(packages.size()), packages.c_str(), static_cast<int>(packages.size()), TRUE) == CSTR_EQUAL) {
            const auto rest = std::filesystem::path(module.substr(packages.size() + 1));
            if (!rest.empty()) crashed_plugin_ = text_encoding::utf8(rest.begin()->wstring());
        }
        if (!crashed_plugin_.empty()) {
            diagnostics::log("error", "Previous run crashed in plugin " + crashed_plugin_ + "; it is switched off");
            showNotification(NotificationKind::error, tr(L"Plugin spowodował awarię i został wyłączony"),
                text_encoding::wide(crashed_plugin_) + L" · " + tr(L"Raport: ") + L"crashes\\" + crash->dump);
        } else showNotification(NotificationKind::error, tr(L"Launcher zakończył się błędem"),
            tr(L"Raport: ") + (userConfigDirectory() / L"crashes" / crash->dump).wstring());
    }
    initializeLibraryPreferences();
    renderer_.setDrawerStyle(settings_.drawer);
    renderer_.setFontFamily(settings_.font_family);
    renderer_.setCoverAccent(settings_.cover_accent_rgb);
    renderer_.setUiAccent(settings_.ui_accent_rgb);
    renderer_.setButtonTheme(settings_.button_rgb, settings_.button_idle);
    renderer_.setFocusGap(settings_.focus_gap);
    renderer_.setGlow(settings_.glow_level, settings_.glow_pulse, settings_.glow_trace);
    renderer_.setBackgroundGrid(settings_.background_grid, settings_.background_grid_style);
    renderer_.setTileShape(settings_.tile_shape);
    renderer_.setPanelBorders(settings_.panel_borders);
    renderer_.setGlass(settings_.glass);
    renderer_.setConsoleColors(settings_.console_colors);
    applyLowMemory(); // System → Wydajność → Tryb niskiego zużycia RAM.
#ifndef LAUNCHME_UI_TEST
    // Kompresja grafik: compressed art kept on disk, so each picture is compressed once (tests
    // decode inline and keep nothing).
    renderer_.setTextureCache(userConfigDirectory() / L"textures");
#endif
    // Play a boot cue already decoded during renderer startup before initializing services.
    // Only consume our own audio payloads; do not reenter general window/input handling.
    MSG ready_audio{};
    while (PeekMessageW(&ready_audio, hwnd_, audio_message, audio_message, PM_REMOVE))
        onAudio(std::unique_ptr<AudioMessage>(reinterpret_cast<AudioMessage*>(ready_audio.lParam)));
    runtime_started_ = true;
    volume_monitor_.start(work_);
#ifdef _WIN32
    controller_input_.initialize(hwnd);
#else
    controller_input_.initialize(lm_window::sdlWindow(hwnd)); // SDL's window: the Steam Controller's focus check.
#endif
    controller_input_.steamController(settings_.steam_controller);
    configureGameOverlay();
    initializeControllerMonitor();
    initializeHeadphoneMonitor();
    initializeStore();
    initializePlugins();
    if (aurelia_active_) preview_.start(work_, hwnd_, [invoke = aurelia_transport_, alive = alive_](uint32_t app, uint64_t) {
        aurelia::Request request{aurelia::Command::info, app}; request.language = i18n::steamLanguage();
        const auto reply = invoke ? invoke(request, [alive] { return !alive->load(); }, {}) : aurelia::Reply{};
        auto details = reply.ok ? aurelia::details(reply.data) : GameDetails{}; details.error = reply.error;
        if (reply.ok && invoke && alive->load()) {
            request.command = aurelia::Command::achievements;
            const auto response = invoke(request, [alive] { return !alive->load(); }, {});
            if (response.ok) { const auto list = aurelia::achievements(response.data); details.achievements_total = list.total; details.achievements_unlocked = list.unlocked; }
        }
        return details;
    });
    else preview_.start(work_, hwnd_);
    initializeAchievements();
    initializeArtworkBrowser();
    initializeGog();
    initializeEpic();
    initializeAmazon();
    initializeXbox();
    loadCustomGames(); // Before the first shelves; file checks run on a worker.
    loadDrmFree();     // Likewise; folder scans start after the first library scans.
    startLibraryWorker();
    startPcWatcher();
    startSteamSync();
    initializeGameSessions();
    startGameIndex();
    companionRemoteSync(); // Phone access, when switched on.
    restoreFrozenGames();
    initializeUpdates(); // The previous update's old files away, its changes shown once; then a look for a newer one.
#ifndef _WIN32
    shortcutProgram(false); // The shortcuts' helper follows Launchme when it moved (a new AppImage).
    compatRefreshDatabase();
#endif
    if (desktop_mode_) { applyWindowMode(); buildDesktopSidebar(); } // The window's style and place; ShowWindow follows in main.cpp.
    if (!settings_.tour_done && !settings_.aurelia_intro_done) openAurelia(nullptr, true);
    else if (!desktop_mode_ && !settings_.tour_done) startTour(); // First start: the walkthrough over the loading home screen.
    profiler_.startup(FrameProfiler::Startup::initialized);
    return true;
}

void Launcher::saveSessionState() {
    if (session_saved_) return; // WM_QUERYENDSESSION then WM_ENDSESSION: once is enough.
    session_saved_ = true;
    diagnostics::log("info", "Windows session ending: saving state");
    if (playtime_dirty_) { library_state_dirty_ = true; playtime_dirty_ = false; }
    desktopSaveState(); rememberDesktopWindow(); rememberTvMonitor(); rememberPlace();
    saveLibraryPreferences();
    if (library_writer_) library_writer_->flush(std::chrono::milliseconds(3000));
    if (settings_dirty_) saveSettings();
}

void Launcher::displayChanged() {
    if (closing_ || !hwnd_) return;
    if (coversMonitor()) {
        // Cover the monitor the window is on now (it may have a new size, or be another one); the
        // remembered TV when it has just come back.
        const HMONITOR remembered = monitorNamed(settings_.tv_monitor);
        MONITORINFO monitor{sizeof(monitor)};
        if (!GetMonitorInfoW(remembered ? remembered : MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST), &monitor)) return;
        const RECT& r = monitor.rcMonitor;
        RECT current{};
        GetWindowRect(hwnd_, &current);
        if (!EqualRect(&current, &r))
            SetWindowPos(hwnd_, nullptr, r.left, r.top, r.right - r.left, r.bottom - r.top, SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    } else if (desktop_mode_ && !IsZoomed(hwnd_) && !IsIconic(hwnd_)) {
        // A window left (partly) outside every monitor comes back onto the nearest one.
        RECT current{};
        GetWindowRect(hwnd_, &current);
        MONITORINFO monitor{sizeof(monitor)};
        if (!GetMonitorInfoW(MonitorFromRect(&current, MONITOR_DEFAULTTONEAREST), &monitor)) return;
        const RECT& work = monitor.rcWork;
        const LONG width = std::min(current.right - current.left, work.right - work.left);
        const LONG height = std::min(current.bottom - current.top, work.bottom - work.top);
        const LONG x = std::clamp(current.left, work.left, work.right - width);
        const LONG y = std::clamp(current.top, work.top, work.bottom - height);
        if (x != current.left || y != current.top || width != current.right - current.left || height != current.bottom - current.top)
            SetWindowPos(hwnd_, nullptr, x, y, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
    }
    resize();
}

void Launcher::resumedFromSleep() {
    if (closing_) return;
    diagnostics::log("info", "Resumed from sleep");
    audio_.recover();                // An output device that went away meanwhile.
    controller_input_.deviceChanged(nullptr); // Pads are probed again.
    displayChanged();                // A TV may have come back in another mode.
    pokeRunningScan();
}

void Launcher::onAudio(std::unique_ptr<AudioMessage> message) {
    if (message->cue == SoundCue::music) {
        if (message->generation != music_jobs_->generation.load()) return;
        // Kept as the Polish key; the settings row translates it.
        music_status_ = message->clip.samples.empty() ? trKey(L"Nie znaleziono lub nie można odczytać pliku motywu. Wskaż folder dźwięków powyżej.") : L"";
        if (settingsOpen()) buildSettingsRows();
        audio_.setCue(message->cue, std::move(message->clip));
        return;
    }
    const int slot = soundCueSlot(message->cue);
    if (slot < 0 || message->generation != sound_generation_[slot]) return;
    const bool preview = message->preview;
    const auto cue = message->cue;
    if (cue != SoundCue::boot && message->clip.samples.empty()) return;
    audio_.setCue(cue, std::move(message->clip));
    if (cue == SoundCue::boot) {
        if (preview) audio_.boot();
        else if (!boot_played_) { if (desktop_mode_) audio_.skipBoot(); else audio_.boot(); } // The desktop window starts silently.
        boot_played_ = true;
    } else if (preview) {
        if (cue == SoundCue::toast) audio_.toast();
        else audio_.navigate();
    }
}

void Launcher::beginScreenTransition() {
    transition_screen_ = screen_;
    bool motion = runtime_started_ && !library_transition_ && settings_.screen_transitions;
#ifdef LAUNCHME_UI_TEST
    motion = screen_motion_for_test_ && settings_.screen_transitions;
#endif
    screen_transition_ = motion ? 0.04f : 1.0f;
    if (motion) {
        arrived_ = 10.0f;
        snapCardsForTransition();
    }
}

void Launcher::syncScreenTransition() {
    if (screen_ != transition_screen_) beginScreenTransition();
}

void Launcher::snapCardsForTransition() {
    if (cards_.empty() || screen_ == ScreenMode::plugin_page || screen_ == ScreenMode::downloads) return;
    const auto view = renderer_.viewport();
    const bool grid = gridScreen(screen_);
    const bool rail = categoryRailActive();
    const float shelf_inset = rail ? category_rail::Layout{view.width, view.height}.right() : 0;
    const bool fixed_row = !rail && category_ == 0 && !grid;
    const bool xmb = xmbActive(), flix = flixActive(), metro = metroActive(), psfive = psfiveActive(), list = libraryList();
    const bool psfive_buttons = psfive && (psfiveActions() || psfiveTopFocused());
    const float shelf_aspect = shelfPosters() ? 2.0f / 3.0f : 1.0f;
    const float compact_size = grid ? grid_layout::cell(view.width) :
                               carousel::smallSize(cards_.size(), view.width - shelf_inset, fixed_row);
    for (size_t i = 0; i < cards_.size(); ++i) {
        auto& card = cards_[i];
        float target_size = 0, target_x = 0, target_y = 0, delay = 0, aspect = 1;
        if (xmb) xmbTarget(i, target_x, target_y, target_size, aspect, delay);
        else if (flix) flixTarget(i, target_x, target_y, target_size, aspect, delay);
        else if (metro) metroTarget(i, target_x, target_y, target_size, aspect, delay);
        else if (psfive) psfiveTarget(i, target_x, target_y, target_size, aspect, delay);
        else if (snitchActive()) snitchTarget(i, target_x, target_y, target_size, aspect, delay);
        else if (libraryCarousel()) {
            const coverflow_layout::Layout flow{view.width, view.height, libraryView(), libraryPresentation().coverflow_cd};
            flow.target(i, selected_, target_x, target_y, target_size, aspect);
        } else if (list) {
            target_size = list_layout::thumb;
            target_x = library_layout::rowThumbLeft(libraryView(), view.width, view.height);
            target_y = list_layout::thumbTop(list_layout::top(i, grid_scroll_));
        } else if (libraryCinematic()) {
            library_layout::cinematicTarget(i, view.width, view.height, grid_scroll_, libraryPresentation(), target_x, target_y, target_size);
            aspect = cinematic_layout::aspect;
        } else if (libraryOverview()) {
            const int columns = libraryPresentation().overview_columns;
            target_size = overview_layout::cell(view.width, columns);
            target_x = overview_layout::x(i, view.width, columns);
            target_y = overview_layout::top(i, view.width, grid_scroll_, columns);
            aspect = overview_layout::aspect;
        } else if (libraryGridLandscape()) {
            target_size = landscape_grid::cell(view.width);
            target_x = landscape_grid::left(view.width, i);
            target_y = landscape_grid::top(i, view.width, grid_scroll_);
            aspect = landscape_grid::aspect;
        } else if (libraryGridPosters()) {
            target_size = grid_layout::cell(view.width) * 1.5f;
            target_x = grid_layout::left(view.width, i);
            target_y = grid_layout::top(i, view.width, grid_scroll_, true);
            aspect = 2.0f / 3.0f;
        } else {
            target_size = grid ? compact_size : (i == selected_ ? 2 * compact_size : compact_size) / shelf_aspect;
            target_x = grid ? grid_layout::left(view.width, i) :
                       shelf_inset + carousel::left(i, selected_, cards_.size(), view.width - shelf_inset, fixed_row);
            if (!grid) aspect = shelf_aspect;
            target_y = grid ? grid_layout::top(i, view.width, grid_scroll_) :
                       carousel::bottom(view.height) - target_size - (quick_open_ ? 100.0f : 0.0f);
            if (rail) {
                if (i < selected_) target_x -= category_rail::Layout::hidden_card_offset;
                if (i != selected_) target_y -= category_rail::Layout::compact_lift;
            }
        }
        card.x = target_x;
        card.y = target_y;
        card.size = target_size;
        card.aspect = aspect;
        card.opacity = 1.0f;
        card.emphasis = i == selected_ && !quick_open_ && !(metro && metro_tabs_) && !psfive_buttons && !searchBackFocused() &&
            !(libraryControls() && (library_controls_.focus >= 0 || library_controls_.open() ||
                                    (libraryOverview() && library_controls_.action >= 0))) ? 1.0f : 0.0f;
        if (renderer_.hasCover(i) || renderer_.hasPoster(i)) card.cover_opacity = 1.0f;
    }
}

void Launcher::tick() {
    const auto now = std::chrono::steady_clock::now();
    last_tick_ms_ = GetTickCount64();
    if (closing_) { frame_limit_ = 240; tickClose(now); return; } // The exit animation at the full rate.
    pollMemory();
    pollGameFocus();
    pollTray();
    pollReturnFocus();
    startRatings();
    pollLibrarySave();
    pollPluginGameViews();
    focusCouchCategory();
    pollJumpList();
    rememberPlace();
    if (library_state_dirty_ && !restore_pending_ && GetTickCount64() >= next_library_save_) {
        saveLibraryPreferences(); next_library_save_ = GetTickCount64() + 1000;
    }
    if (settings_dirty_ && !settings_save_failed_ && !settings_dragging_ &&
        std::chrono::duration<float>(now - settings_edited_).count() >= 0.35f) saveSettings();
    if (searching() && !pluginPageTyping() && !companionTyping() && search_dirty_ && // A plugin's field searches on Enter.
        std::chrono::duration<float>(now - search_edited_).count() >= (screen_ == ScreenMode::store ? 0.40f : 0.16f)) {
        if (screen_ == ScreenMode::store) searchStore(); else updateSearch();
    }
    if (screen_ == ScreenMode::details && !details_loading_ && GetTickCount64() >= next_install_poll_) requestDetails();
    pollPluginPageRefresh();
    pollPluginDownloads();
    aureliaPoll(GetTickCount64());
    pollDownloadArt();
    pollCompanionLife(); // Her life goes on in the background (her web reading), a look every ten minutes.
    const float dt = std::clamp(std::chrono::duration<float>(now - last_frame_).count(), 0.001f, 0.05f);
    last_frame_ = now;
    syncScreenTransition();
    const bool screen_transitioning = screen_transition_ < 1.0f;
    if (screen_transitioning) screen_transition_ = std::min(1.0f, screen_transition_ + dt / screen_transition_duration);
    // The picture keeps its crop across navigation. Details gently approaches 101%
    // over several seconds; leaving eases back from the current scale, never snaps.
    const bool details_wallpaper = screen_ == ScreenMode::details && settings_.screen_transitions;
    const float wallpaper_target = details_wallpaper ? 1.01f : 1.0f;
    const bool wallpaper_moving = wallpaper_zoom_ != wallpaper_target;
    wallpaper_zoom_ += (wallpaper_target - wallpaper_zoom_) * ease(dt, details_wallpaper ? 0.45f : 2.4f);
    if (std::abs(wallpaper_zoom_ - wallpaper_target) < 0.0002f) wallpaper_zoom_ = wallpaper_target;
    arrived_ += dt;
    detail_ = std::min(1.0f, detail_ + dt * 1.7f);
    library_focus_age_ += dt;
    if (pending_launch_) {
        launch_age_ += dt;
        launch_progress_ = launchAnimationActive() ? 0 : std::min(1.0f, launch_age_ / 0.52f);
        // A Steam account switch first restarts Steam (src/app/steam_install.cpp): the game waits for it.
        if (!steam_switch_pending_ && (launchAnimationActive() || launch_age_ >= 0.52f)) finishLaunch();
    } else launch_progress_ = std::max(0.0f, launch_progress_ - dt / 0.30f);
    tickLaunchAnimation(now);
    if (enter_held_ && !menu_open_ && !cards_.empty() &&
        std::chrono::duration<float>(now - enter_started_).count() >= enter_hold_seconds) {
        openContextMenu();
    }
    pollHoldGestures();
    pollFileBrowserPreview();
    pollCustomEditorPoster(); // Dodaj / Edytuj grę: the typed name's poster once typing rests.
    menu_progress_ += ((menu_open_ ? 1.0f : 0.0f) - menu_progress_) * ease(dt, 13.0f);
    if (keyboard_.open && !textFieldActive()) closeKeyboard(); // Its field closed (Esc, a click, a screen change).
    keyboard_progress_ += ((keyboard_.open ? 1.0f : 0.0f) - keyboard_progress_) * ease(dt, 14.0f);
    if (keyboard_progress_ < 0.002f) keyboard_progress_ = 0;
    quick_progress_ += ((quick_open_ ? 1.0f : 0.0f) - quick_progress_) * ease(dt, 7.0f);
    friends_progress_ += ((friends_open_ ? 1.0f : 0.0f) - friends_progress_) * ease(dt, 7.0f);
    category_menu_progress_ += ((category_menu_open_ ? 1.0f : 0.0f) - category_menu_progress_) * ease(dt, psfiveActive() ? 14.0f : 7.0f);
    const bool library_panels = libraryControls();
    const bool filters_open = library_panels && library_controls_.panel == library_ui::Panel::filters;
    const bool views_open = library_panels && library_controls_.panel == library_ui::Panel::views;
    library_menu_progress_ += ((filters_open ? 1.0f : 0.0f) - library_menu_progress_) * ease(dt, 13.0f);
    library_views_progress_ += ((views_open ? 1.0f : 0.0f) - library_views_progress_) * ease(dt, 7.0f);
    const float dropdown_target = views_open && library_controls_.view_dropdown ? 1.0f : 0.0f;
    library_dropdown_progress_ += (dropdown_target - library_dropdown_progress_) * ease(dt, 14.0f);
    if (std::abs(library_dropdown_progress_ - dropdown_target) < 0.002f) library_dropdown_progress_ = dropdown_target;
    if (!library_panels) library_menu_progress_ = library_views_progress_ = library_dropdown_progress_ = 0;
    // Quick scrolling in the library: vertical moves held for about a second, each soon after the last.
    const bool alphabetic_scroll = screen_ == ScreenMode::library && library_controls_.options.order == library_view::Order::alphabetical;
    const bool fast_scroll = alphabetic_scroll && !library_controls_.open() && library_controls_.focus < 0 &&
                             now - fast_scroll_at_ < std::chrono::milliseconds(380) &&
                             now - fast_scroll_started_ >= std::chrono::milliseconds(1000);
    fast_scroll_alpha_ += ((fast_scroll ? 1.0f : 0.0f) - fast_scroll_alpha_) * ease(dt, fast_scroll ? 16.0f : 6.0f);
    if (!alphabetic_scroll) fast_scroll_alpha_ = 0;
    if (library_view::metadata(libraryPresentation()) || heroFactsShown()) requestPreview(); // The preview component debounces focus changes.
    // Library search field: unfolds under the title; typed text filters the games a moment later.
    const bool library_search = screen_ == ScreenMode::library && library_controls_.search;
    library_search_progress_ += ((library_search ? 1.0f : 0.0f) - library_search_progress_) * ease(dt, 12.0f);
    if (screen_ != ScreenMode::library) library_search_progress_ = 0;
    if (library_query_dirty_ && screen_ == ScreenMode::library && now - library_query_edited_ >= std::chrono::milliseconds(160)) applyLibraryQuery();
    const auto view = renderer_.viewport();
    if (category_menu_open_ && !psfiveActive()) {
        const float target = category_menu::top(view.height, category_titles_.size()) +
                             category_cursor_ * category_menu::row_height - category_menu_scroll_;
        category_highlight_y_ += (target - category_highlight_y_) * ease(dt, 13.0f);
    }
    const bool grid = gridScreen(screen_);
    const bool rail = categoryRailActive();
    const float shelf_inset = rail ? category_rail::Layout{view.width, view.height}.right() : 0;
    const bool fixed_row = !rail && category_ == 0 && !grid;
    const bool glance_motion = tickGlance(dt);
    const bool companion_motion = tickCompanion(dt);
    const bool tarot_motion = tickTarot(dt);
    const bool profile_motion = tickProfile(dt);
    tickGameIndex();
    tickUpdates();
    bool needs_paint = scanning_ || detail_ < 1.0f || screen_transitioning || wallpaper_moving || glance_motion || companion_motion || tarot_motion || profile_motion ||
                       dashboardEditing() ||
                       launch_progress_ > 0.003f || pending_launch_ || launchAnimationMoving(now) ||
                        (friends_progress_ > 0.003f && friends_progress_ < 0.997f) ||
                       (category_menu_progress_ > 0.003f && category_menu_progress_ < 0.997f) ||
                       (category_menu_open_ && !psfiveActive() && std::abs(category_highlight_y_ -
                            (category_menu::top(view.height, category_titles_.size()) +
                             category_cursor_ * category_menu::row_height - category_menu_scroll_)) > 0.18f) ||
                       (menu_progress_ > 0.003f && menu_progress_ < 0.997f) ||
                       (keyboard_progress_ > 0.003f && keyboard_progress_ < 0.997f) ||
                       (quick_progress_ > 0.003f && quick_progress_ < 0.997f) ||
                       (library_menu_progress_ > 0.003f && library_menu_progress_ < 0.997f) ||
                       (library_views_progress_ > 0.003f && library_views_progress_ < 0.997f) ||
                       (library_dropdown_progress_ > 0 && library_dropdown_progress_ < 1) ||
                       (library_search_progress_ > 0.003f && library_search_progress_ < 0.997f) ||
                       (fast_scroll_alpha_ > 0.003f && fast_scroll_alpha_ < 0.997f);
    const bool xmb = xmbActive(), flix = flixActive(), metro = metroActive(), psfive = psfiveActive(), list = libraryList();
    // Flix: a rest on a game of a poster row unfolds its tile (flixTarget follows flix_unfold_).
    if (flix) needs_paint |= tickFlixUnfold(now, dt);
    else { flix_unfold_ = 0; flix_unfold_index_ = SIZE_MAX; }
    // The buttons or the top bar have the focus: the focused tile keeps its size, not its outline.
    const bool psfive_buttons = psfive && (psfiveActions() || psfiveTopFocused());
    // Shelves of posters: 2:3 cards as wide as the square covers (so 1.5x as tall).
    const float shelf_aspect = shelfPosters() ? 2.0f / 3.0f : 1.0f;
    // Per frame, not per card: a library grid can hold thousands of cards.
    const float compact_size = grid ? grid_layout::cell(view.width) :
                               carousel::smallSize(cards_.size(), view.width - shelf_inset, fixed_row);
    const float ease_x = ease(dt, 12.0f), ease_size = ease(dt, 10.5f), ease_opacity = ease(dt, 8.0f), ease_cover = ease(dt, 7.0f);
    const auto entrance = settings_.tile_entrance;
    // A move while the tiles still grow in (the grow entrance places them at their targets, which a
    // new focus just moved): the entrance ends there and every tile glides on from where it is,
    // instead of the row jumping at once. A focus restored before the tiles appear keeps the wave.
    constexpr float grow_entrance_end = 0.28f + 0.85f + 0.72f; // lead + longest delay + grow (below).
    if (entrance == AppSettings::TileEntrance::grow && selected_ != entrance_selected_ && arrived_ > 0.3f && arrived_ < grow_entrance_end)
        arrived_ = grow_entrance_end;
    entrance_selected_ = selected_;
    // One spring for the lift, rather than stacking the slow drawer fade and
    // card easing. About 95% of the 100px movement completes in 170ms.
    const float ease_y = ease(dt, quick_open_ || quick_progress_ > 0.003f ? 18.0f : 9.0f);
    float focus_x = 0, focus_y = 0, focus_w = 0, focus_h = 0; // The focused card's resting rectangle.
    if (metro) { // The Metro tab strip glides with the pages.
        const float page = static_cast<float>(metro_page_);
        metro_tab_ += (page - metro_tab_) * ease_x;
        if (std::abs(page - metro_tab_) > 0.001f) needs_paint = true; else metro_tab_ = page;
    }
    const bool desktop = desktopActive(); // Its grid moves on its own (tickDesktop).
    if (desktop) needs_paint |= tickDesktop(dt);
    needs_paint |= tickDesktopSheet(dt); // Ustawienia as a desktop dialog: its scroll and caret.
    needs_paint |= tickDesktopDialog();  // Kolekcje, Dodaj grę, Zidentyfikuj grę: the caret.
    needs_paint |= customEditorOpen() && renderer_.customPosterPending(); // Its poster shows once decoded.
    if (favorite_pulse_ > 0.0f) {
        favorite_pulse_ = std::max(0.0f, favorite_pulse_ - dt / 0.22f);
        needs_paint = true;
    }
    if (settingsOpen()) {
        const auto page = settingsPage();
        if (page != settings_toggles_page_) { settings_toggles_page_ = page; settings_toggles_.clear(); }
        for (const auto& item : settings_rows_) {
            if (item.control != settings_ui::Control::toggle && item.control != settings_ui::Control::collection &&
                item.control != settings_ui::Control::platform) continue;
            const auto key = std::to_string(static_cast<int>(item.id)) + '|' + item.key + '|' + item.plugin_setting + '|' +
                             std::to_string(static_cast<int>(item.source)) + '|' + std::to_string(item.choice);
            const float target = item.checked ? 1.0f : 0.0f;
            const auto [it, inserted] = settings_toggles_.try_emplace(key, target);
            if (!inserted) {
                if (std::abs(it->second - target) > 0.004f) { it->second += (target - it->second) * ease(dt, 18.0f); needs_paint = true; }
                else it->second = target;
            }
        }
    } else if (!settings_toggles_.empty()) {
        settings_toggles_.clear();
        settings_toggles_page_.clear();
    }
    for (size_t i = 0; i < cards_.size() && !desktop && screen_ != ScreenMode::plugin_page && screen_ != ScreenMode::downloads && !glanceOpen() && !companionOpen(); ++i) {
        auto& card = cards_[i];
        float target_size = 0, target_x = 0, target_y = 0, delay = 0, aspect = 1;
        if (xmb) xmbTarget(i, target_x, target_y, target_size, aspect, delay);
        else if (flix) flixTarget(i, target_x, target_y, target_size, aspect, delay);
        else if (metro) metroTarget(i, target_x, target_y, target_size, aspect, delay);
        else if (psfive) psfiveTarget(i, target_x, target_y, target_size, aspect, delay);
        else if (snitchActive()) snitchTarget(i, target_x, target_y, target_size, aspect, delay);
        else if (libraryCarousel()) {
            const coverflow_layout::Layout flow{view.width, view.height, libraryView(), libraryPresentation().coverflow_cd};
            flow.target(i, selected_, target_x, target_y, target_size, aspect);
            delay = std::min<size_t>(i > selected_ ? i - selected_ : 0, 8) * 0.035f;
        } else if (list) {
            target_size = list_layout::thumb;
            target_x = library_layout::rowThumbLeft(libraryView(), view.width, view.height);
            target_y = list_layout::thumbTop(list_layout::top(i, grid_scroll_));
            delay = std::min<size_t>(i, 10) * 0.03f;
        } else if (libraryCinematic()) {
            library_layout::cinematicTarget(i, view.width, view.height, grid_scroll_, libraryPresentation(), target_x, target_y, target_size);
            aspect = cinematic_layout::aspect;
            delay = std::min<size_t>(i, 10) * 0.03f;
        } else if (libraryOverview()) {
            const int columns = libraryPresentation().overview_columns;
            target_size = overview_layout::cell(view.width, columns);
            target_x = overview_layout::x(i, view.width, columns);
            target_y = overview_layout::top(i, view.width, grid_scroll_, columns);
            aspect = overview_layout::aspect;
            delay = std::min<size_t>(i, 12) * 0.025f;
        } else if (libraryGridLandscape()) {
            target_size = landscape_grid::cell(view.width);
            target_x = landscape_grid::left(view.width, i);
            target_y = landscape_grid::top(i, view.width, grid_scroll_);
            aspect = landscape_grid::aspect;
            delay = std::min<size_t>(i, 10) * 0.035f;
        } else if (libraryGridPosters()) {
            target_size = grid_layout::cell(view.width) * 1.5f;
            target_x = grid_layout::left(view.width, i);
            target_y = grid_layout::top(i, view.width, grid_scroll_, true);
            aspect = 2.0f / 3.0f;
            delay = std::min<size_t>(i, 10) * 0.035f;
        } else {
            target_size = grid ? compact_size : (i == selected_ ? 2 * compact_size : compact_size) / shelf_aspect;
            target_x = grid ? grid_layout::left(view.width, i) :
                       shelf_inset + carousel::left(i, selected_, cards_.size(), view.width - shelf_inset, fixed_row);
            if (!grid) aspect = shelf_aspect;
            target_y = grid ? grid_layout::top(i, view.width, grid_scroll_) :
                       carousel::bottom(view.height) - target_size - (quick_open_ ? 100.0f : 0.0f);
            if (rail) {
                // Keep earlier covers (including their shadows) behind the rail's clip.
                if (i < selected_) target_x -= category_rail::Layout::hidden_card_offset;
                // The selected title sits beside the large cover, under the compact row.
                if (i != selected_) target_y -= category_rail::Layout::compact_lift;
            }
            // The entry wave starts at the card before the focus: a shelf remembered far to the
            // right shows its focused card at once, not after every card off screen to the left.
            const size_t wave_first = fixed_row || !selected_ ? 0 : selected_ - 1;
            delay = grid ? std::min<size_t>(i, 10) * 0.035f : static_cast<float>(i > wave_first ? i - wave_first : 0) * 0.09f;
        }
        card.aspect = aspect;
        if (i == selected_) { focus_x = target_x; focus_y = target_y; focus_w = target_size * aspect; focus_h = target_size; }
        const float emphasis = i == selected_ && !quick_open_ && !(metro && metro_tabs_) && !psfive_buttons && !searchBackFocused() &&
            !(libraryControls() && (library_controls_.focus >= 0 || library_controls_.open() ||
                                    (libraryOverview() && library_controls_.action >= 0))) ? 1.0f : 0.0f; // Przegląd: its panel's buttons.
        // The entrance of a fresh screen (Interfejs → Pojawianie się kafli; arrived_ restarts with a new list).
        if (entrance == AppSettings::TileEntrance::grow) {
            // PS5-like: the screen first stands empty for a moment (only its background), then every
            // tile grows from its own centre after the one before it, slowly, with a soft settle; the
            // focused one already has its focus size and outline, so nothing grows after. The wave is
            // capped so a long row is in place within about 1.8 s.
            constexpr float lead = 0.28f, grow_seconds = 0.72f, grow_from = 0.62f, stagger = 1.35f, longest = 0.85f;
            delay = (entrance_lead_ ? lead : 0.0f) + std::min(delay * stagger, longest);
            const float t = (arrived_ - delay) / grow_seconds;
            if (t < 1) {
                const float u = std::clamp(t, 0.0f, 1.0f) - 1, overshoot = 0.9f;
                const float curve = t <= 0 ? 0.0f : 1 + (overshoot + 1) * u * u * u + overshoot * u * u; // Back-out.
                const float scale = grow_from + (1 - grow_from) * curve, w = target_size * aspect;
                card.size = target_size * scale;
                card.x = target_x + w * (1 - scale) * 0.5f;
                card.y = target_y + target_size * (1 - scale) * 0.5f + 10 * (1 - curve); // A slight rise as it grows.
                const float shown = std::clamp(t * 1.8f, 0.0f, 1.0f);
                card.opacity = shown * shown * (3 - 2 * shown);
                card.emphasis = emphasis;
                if (renderer_.hasCover(i) || renderer_.hasPoster(i)) card.cover_opacity = 1.0f;
                needs_paint = true;
                continue;
            }
            if (card.size == 0) { card.x = target_x; card.y = target_y; card.size = target_size; } // Added later: in place.
        } else if (entrance == AppSettings::TileEntrance::none) {
            if (card.size == 0 || arrived_ < 0.05f) { card.x = target_x; card.y = target_y; card.size = target_size; card.opacity = 1; card.emphasis = emphasis; }
        } else {
            // Rising from below, a little smaller, one after another.
            if (card.size == 0) {
                card.x = target_x;
                card.y = target_y + 28.0f;
                card.size = target_size * 0.94f;
            }
            if (arrived_ < delay) {
                card.x = target_x;
                card.y = target_y + 28.0f;
                card.size = target_size * 0.94f;
                card.opacity = 0;
                needs_paint = true;
                continue;
            }
        }
        card.x += (target_x - card.x) * ease_x;
        card.y += (target_y - card.y) * ease_y;
        card.size += (target_size - card.size) * ease_size;
        card.emphasis += (emphasis - card.emphasis) * ease_size;
        if (std::abs(card.emphasis - emphasis) > 0.002f) needs_paint = true;
        const bool steelbook = libraryCoverflow() || (libraryListPlus() && libraryPresentation().list_plus_steelbook);
        if (steelbook) {
            if (libraryCoverflow()) { // Lista+ has the finish without the disc.
                const float disc = emphasis > 0 && !menu_open_ && !pending_launch_ && library_focus_age_ >= 0.85f ? 1.0f : 0.0f;
                card.disc += (disc - card.disc) * ease(dt, disc > 0 ? 4.5f : 14.0f);
                if (i != selected_ && i != selected_ + 1 && card.disc < 0.003f) renderer_.releaseDisc(i);
                if (std::abs(card.disc - disc) > 0.002f || library_focus_age_ < 0.85f) needs_paint = true;
            }
            // Every selection turns the plain print into a steelbook (0 -> 100% in 0.9 s, after a
            // short hold so fast scrolling does not flash); leaving the poster fades it quicker.
            // Lista+'s poster stays the selected game's while the top bar or the view drawer has
            // the focus, so its finish stays too (switching the option shows it at once).
            const bool finish = libraryListPlus() ? i == selected_ && !quick_open_ && !menu_open_ : emphasis > 0;
            const float previous = card.steelbook;
            if (!finish) card.steelbook = std::max(0.0f, card.steelbook - dt / 0.35f);
            else if (library_focus_age_ >= 0.15f) card.steelbook = std::min(1.0f, card.steelbook + dt / 0.9f);
            if (card.steelbook != previous || (finish && library_focus_age_ < 0.15f)) needs_paint = true;
        } else if (card.steelbook > 0) {
            card.steelbook = 0;
            needs_paint = true;
        }
        card.opacity += (1.0f - card.opacity) * ease_opacity;
        // Bound cover = its bytes arrived (the renderer shares art_'s buffer); no key lookup per frame.
        if (renderer_.hasCover(i) || renderer_.hasPoster(i)) {
            card.cover_opacity += (1.0f - card.cover_opacity) * ease_cover;
            if (card.cover_opacity < 0.995f) needs_paint = true;
        }
        if (std::abs(card.x - target_x) > 0.12f || std::abs(card.y - target_y) > 0.12f ||
            std::abs(card.size - target_size) > 0.12f || card.opacity < 0.995f)
            needs_paint = true;
    }
    // The fresh screen's wave has played: later rebuilds (another shelf, a filter) start at once.
    if (entrance_lead_ && !desktop && !cards_.empty() && arrived_ > 2.2f) entrance_lead_ = false;
    if (!desktop) animateFocus(dt, focus_x, focus_y, focus_w, focus_h, needs_paint);
    tickTour(dt, needs_paint);
    if (settingsOpen()) needs_paint |= settings_preview_.tick(GetTickCount64());
    else { settings_preview_.focus(-1, GetTickCount64()); settings_hover_row_ = -1; }
    // The Settings picker fading in or out (paint.cpp).
    if (settingsOpen() && (settingsPicking() != settings_picker_open_ || GetTickCount64() - settings_picker_changed_ < 220)) needs_paint = true;
    // Continuous parallax/glow follows the frame clock, not a second 30Hz gate, but
    // only while someone is watching: launcher in the foreground and either recent
    // input or a visible activity (spinning install disc, search caret, scan).
    // Otherwise frames are painted only for real state changes - no GPU work while
    // a game runs or the screen idles. Static panels always repaint on change only.
    const bool visible_activity = searching() || scanning_ || (gogInstallOpen() && gog_install_ui_.snapshot.busy()) ||
        std::any_of(cards_.begin(), cards_.end(), [](const GameCard& card) { return card.game.installation.moving(); });
    const bool watched = windowWatched();
    const bool ambient = watched && (now - last_input_ < std::chrono::seconds(60) || visible_activity);
    if (ambient) ambient_time_ += dt;
    // The desktop library repaints only for its own motion and visible activity (an install's bar).
    if ((ambient && !desktop && !cards_.empty() && (screen_ == ScreenMode::shelves || screen_ == ScreenMode::platform_games ||
        screen_ == ScreenMode::details || grid)) || (ambient && desktop && visible_activity) || (ambient && searching()) ||
        (ambient && !desktop && (quick_open_ || quick_progress_ > 0.003f)) || // The quick bar's focus ring light travels.
        profiler_.overlay() || renderer_.animating())
        needs_paint = true;
    const auto input_started = std::chrono::steady_clock::now();
#ifdef LAUNCHME_UI_TEST
    const bool read_pads = real_pads_;
#else
    constexpr bool read_pads = true;
#endif
    if (!guide_timer_active_) { if (read_pads) controller(); tickGameOverlay(GetTickCount64()); }
    autoKeyboard();
    pollGameOverlay();
    const auto input_finished = std::chrono::steady_clock::now();
    pollDevices();
    syncSettingsDevices();
    pollSettingsDevices();
    pollGog();
    pollEpic();
    pollAmazon();
    pollXbox();
    pollGogInstall();
    pollPcInstallations();
    pollDrmFree();
    if (gogInstallOpen() && gog_install_ui_.snapshot.busy()) needs_paint = true;
    // Pobierania follows the sources four times a second while it is open.
    if (downloadsOpen()) {
        const auto stamp = std::chrono::steady_clock::now();
        if (stamp - downloads_refreshed_ >= std::chrono::milliseconds(250)) { downloads_refreshed_ = stamp; refreshDownloads(); }
        // A transfer without a share yet shows a gliding segment: keep painting.
        if (std::any_of(downloads_state_.rows.begin(), downloads_state_.rows.end(), [](const auto& row) { return row.moving && !row.fraction; }))
            needs_paint = true;
    }
    // Drawer clock (and PSfive's in its top bar): one repaint when the displayed minute changes,
    // also while idle. The local time is read at most once a second (and right at the minute boundary).
    if ((friends_open_ && !friends_list_) || (psfive && settings_.psfive_clock) || snitchActive()) {
        const auto ms = GetTickCount64();
        if (ms >= clock_check_at_) {
            const auto local = local_time_();
            if (drawer_clock::minute(local) != clock_minute_) needs_paint = true;
            clock_check_at_ = ms + std::min(1000u, drawer_clock::millisecondsToNextMinute(local) + 2);
        }
    }
    volume_monitor_.watched(watched); // Its indicator is seen only while the launcher is in front.
    pollVolume();
    const auto system_finished = std::chrono::steady_clock::now();
    if (!notifications_.empty()) {
        // An error raised while a game (or another app) is in front waits until the launcher is
        // seen again instead of expiring unseen behind it; other toasts keep their time.
        if (notifications_.front().kind != NotificationKind::error || watched) notifications_.front().age += dt;
        if (notifications_.front().age >= notificationLifetime(notifications_.front().kind)) notifications_.pop_front();
        if (watched) needs_paint = true;
    }
    if (volume_notice_) {
        volume_notice_->age += dt;
        if (volume_notice_->age >= 1.8f) volume_notice_.reset();
        needs_paint = true;
    }
    audio_.update(dt);
    // Screensaver (src/app/screensaver.cpp): starts after the idle period, then paints
    // every frame while the window is watched; losing the foreground ends it.
    updateScreensaver(now, dt);
    if (screensaverActive()) needs_paint = true;
    tickVideo(needs_paint); // After the screensaver: it stops the video the moment it starts.
    if (renderer_.marqueeActive() && GetForegroundWindow() == hwnd_) needs_paint = true; // A long focused name glides (fadingLine).
    if (GetForegroundWindow() == hwnd_) watchEsde(); // Every 20 s: did ES-DE scrape meanwhile?
    if (profiler_.enabled()) profiler_.tick(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - now).count(),
        std::chrono::duration<double, std::milli>(input_finished - input_started).count(),
        std::chrono::duration<double, std::milli>(system_finished - input_finished).count());
    if (needs_paint) {
        if (gameOverlayOpen()) invalidateGameOverlay();
        else if (!IsIconic(hwnd_)) InvalidateRect(hwnd_, nullptr, FALSE);
    }
    // Fewer wake-ups while nothing moves: after 3 s of a still screen without input 60 Hz, after 20 s
    // 30 Hz (a pad press is still seen within a frame of that), 10 Hz behind a game. Any motion, paint
    // or input brings the full rate back with the next tick (main.cpp: FrameClock::limit).
    if (needs_paint || paint_deferred_ || gameOverlayOpen() || profiler_.overlay()) still_since_ = now;
    const auto still = std::min(now - still_since_, now - last_input_);
    frame_limit_ = background_mode_ ? 10 : still >= std::chrono::seconds(20) ? 30 : still >= std::chrono::seconds(3) ? 60 : 240;
}

// Library scan/poller and artwork downloaders: src/app/library_worker.cpp.
// Library -> categories/cards: src/app/shelves.cpp; artwork requests: src/app/artwork_requests.cpp.
// Keyboard/pad navigation: src/app/navigation.cpp, menus: navigation_menus.cpp, launching:
// game_launch.cpp; mouse: src/app/pointer.cpp.
// Friends drawer: src/app/friends.cpp; ViewContext, notifications and window state: src/app/paint.cpp.
// Gamepad/keyboard dispatch, Guide/Start gestures and the power menu: src/app/input.cpp.
