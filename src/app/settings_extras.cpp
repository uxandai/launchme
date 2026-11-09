// Settings added in 0.4.9, kept apart from settings.cpp's long chains (MSVC's nesting limit):
// the artwork sources of "Zmień grafikę", the wallpaper, the home's special tiles, PSfive's
// details, the large game menu and Metro's light theme.
#include "launcher.hpp"
#include "../artwork/artwork_sources.hpp"

using namespace settings_ui;

// A switch of these settings; false: not one of them.
bool Launcher::settingsExtraToggle(const Row& row) {
    if (studioSettingsToggle(row)) return true; // Wydawcy, Developerzy, the Steam rows, the tab icons (studio_libraries.cpp).
    if (row.id == Id::artwork_source) {
        if (row.key.empty()) return true;
        if (!settings_.artwork_sources_off.erase(row.key)) settings_.artwork_sources_off.insert(row.key);
        settingsChanged(); audio_.context();
        return true;
    }
    if (row.id == Id::wallpaper_snitch || row.id == Id::wallpaper_menus || row.id == Id::wallpaper_artwork || row.id == Id::wallpaper_tiles) {
        auto& flag = row.id == Id::wallpaper_snitch ? settings_.wallpaper_snitch : row.id == Id::wallpaper_menus ? settings_.wallpaper_menus :
                     row.id == Id::wallpaper_artwork ? settings_.wallpaper_artwork : settings_.wallpaper_tiles;
        flag = !flag;
        settingsChanged(); audio_.context(); // Visual only: the renderer reads the places.
        return true;
    }
    if (row.id == Id::hud_pin) { // Shown or hidden on the game's screen (src/app/game_overlay.cpp).
        toggleHudPin(row.key);
        settingsChanged(); audio_.context();
        return true;
    }
    if (row.id == Id::fps_graph || row.id == Id::fps_limiter || row.id == Id::fps_limit_precise || row.id == Id::fps_badges ||
        row.id == Id::hud_label_colors || row.id == Id::hud_enabled) {
        auto& flag = row.id == Id::fps_graph ? settings_.fps_graph : row.id == Id::fps_limiter ? settings_.fps_limiter :
                     row.id == Id::fps_badges ? settings_.fps_badges : row.id == Id::hud_label_colors ? settings_.hud_label_colors :
                     row.id == Id::hud_enabled ? settings_.hud_enabled : settings_.fps_limit_precise;
        flag = !flag;
        settingsChanged(); audio_.context(); // The limiter's rows appear and go; the monitor follows the switch.
        return true;
    }
    if (row.id == Id::add_game_tile) {
        settings_.add_game_tile = !settings_.add_game_tile;
        settingsChanged(true); audio_.context(); // The first row changes.
        return true;
    }
    if (row.id == Id::psfive_details) {
        settings_.psfive_details = !settings_.psfive_details;
        psfiveCloseDetails(false);
        settingsChanged(); audio_.context();
        return true;
    }
    if (row.id == Id::metro_light || row.id == Id::menu_dialog || row.id == Id::snitch_colour_icons) {
        auto& flag = row.id == Id::metro_light ? settings_.metro_light : row.id == Id::snitch_colour_icons ? settings_.snitch_colour_icons : settings_.menu_dialog;
        flag = !flag;
        settingsChanged(); audio_.context(); // Visual only.
        return true;
    }
    return false;
}

// A choice picked from these rows' lists; false: not one of them.
bool Launcher::settingsExtraChoose(const Row& row) {
    if (emulationSettingsChoose(row)) return true; // Gry emulowane (emulation_library.cpp).
    if (row.id == Id::studio_min_games) { // Najmniej gier of a studio (studio_libraries.cpp).
        settings_.studio_min_games = studio_min_counts[static_cast<size_t>(std::clamp(settings_state_.choice, 0, static_cast<int>(studio_min_counts.size()) - 1))];
        settings_state_.editing = false;
        settingsChanged(true); // The shelves again: their studios with it.
        return true;
    }
    if (row.id == Id::hud_layout || row.id == Id::cpu_temp_source) {
        auto& value = row.id == Id::hud_layout ? settings_.hud_layout : settings_.cpu_temp_source;
        value = std::clamp(settings_state_.choice, 0, 1);
        if (row.id == Id::cpu_temp_source) afterburner_cpu_->store(value == 0);
        settings_state_.editing = false;
        settingsChanged();
        return true;
    }
    if (row.id == Id::fps_limit_mode) {
        settings_.fps_limit_mode = std::clamp(settings_state_.choice, 0, 1);
        settings_state_.editing = false;
        settingsChanged();
        return true;
    }
    if (row.id == Id::fps_limit || row.id == Id::fps_game_limit) { // The limiter's limits (src/platform/frame_limiter.hpp).
        const int limit = _wtoi(row.choices[static_cast<size_t>(settings_state_.choice)].label.c_str()); // "60 FPS"; "Bez limitu" reads 0.
        if (row.id == Id::fps_limit) settings_.fps_limit = limit;
        else if (settings_state_.choice == 0) settings_.fps_game_limits.erase(row.key); // "Jak globalny".
        else settings_.fps_game_limits[row.key] = limit;
        settings_state_.editing = false;
        settingsChanged();
        return true;
    }
    if (row.id != Id::home_tiles) return false;
    settings_.home_tiles = static_cast<AppSettings::HomeTiles>(std::clamp(settings_state_.choice, 0, static_cast<int>(home_tile_places.size()) - 1));
    settings_state_.editing = false;
    settingsChanged(true); // Every layout's first row is built again.
    return true;
}

// The action rows: the wallpaper picker and "Bez tapety"; false: not one of them.
bool Launcher::settingsExtraActivate(const Row& row) {
    if (emulationSettingsActivate(row)) return true; // Folder ROM-ów (emulation_library.cpp).
    // Biblioteka → Co pokazywać → Wykluczone gry (collections.cpp): back to the library, one or all.
    if (row.id == Id::excluded_games) { openSettingsPage(Page::excluded_games); return true; }
    if (row.id == Id::excluded_game || row.id == Id::excluded_restore_all) {
        if (row.id == Id::excluded_game) restoreExcludedGame(row.key);
        else if (!library_state_.excluded_games.empty()) {
            library_state_.excluded_games.clear();
            library_state_dirty_ = true; saveLibraryPreferences();
            refreshRecentGames(); audio_.context();
        }
        if (library_state_.excluded_games.empty() && settings_state_.at(Page::excluded_games)) closeSettingsPage();
        else buildSettingsRows();
        return true;
    }
    if (row.id == Id::theme_studio) { // Interfejs → Dostosuj wygląd; in the desktop window: on to Tryb TV, where it has its preview.
        if (desktop_mode_) {
            setDesktopMode(false);
            showNotification(NotificationKind::launch, tr(L"Wygląd trybu TV"), tr(L"Ustawienia → Interfejs → Dostosuj wygląd."));
        } else themeStudio(true);
        return true;
    }
    if (row.id == Id::wallpaper_pick) { openWallpaperBrowser(); return true; }
    if (row.id == Id::wallpaper_clear) {
        settings_.wallpaper = AppSettings::WallpaperKind::none;
        settings_.wallpaper_file.clear();
        settings_.wallpaper_id = 0;
        applyWallpaper();
        settingsChanged(); audio_.context();
        settings_state_.selected[settings_state_.category] = std::max(0, settings_state_.row() - 1); // Its row is gone.
        return true;
    }
    return false;
}
