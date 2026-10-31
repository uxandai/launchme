// Settings added in 0.4.9, kept apart from settings.cpp's long chains (MSVC's nesting limit):
// the artwork sources of "Zmień grafikę", the wallpaper, the home's special tiles, PSfive's
// details, the large game menu and Metro's light theme.
#include "launcher.hpp"
#include "../artwork/artwork_sources.hpp"

using namespace settings_ui;

// A switch of these settings; false: not one of them.
bool Launcher::settingsExtraToggle(const Row& row) {
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
    if (row.id == Id::metro_light || row.id == Id::menu_dialog) {
        auto& flag = row.id == Id::metro_light ? settings_.metro_light : settings_.menu_dialog;
        flag = !flag;
        settingsChanged(); audio_.context(); // Visual only.
        return true;
    }
    return false;
}

// A choice picked from these rows' lists; false: not one of them.
bool Launcher::settingsExtraChoose(const Row& row) {
    if (emulationSettingsChoose(row)) return true; // Gry emulowane (emulation_library.cpp).
    if (row.id != Id::home_tiles) return false;
    settings_.home_tiles = static_cast<AppSettings::HomeTiles>(std::clamp(settings_state_.choice, 0, static_cast<int>(home_tile_places.size()) - 1));
    settings_state_.editing = false;
    settingsChanged(true); // Every layout's first row is built again.
    return true;
}

// The action rows: the wallpaper picker and "Bez tapety"; false: not one of them.
bool Launcher::settingsExtraActivate(const Row& row) {
    if (emulationSettingsActivate(row)) return true; // Folder ROM-ów (emulation_library.cpp).
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
