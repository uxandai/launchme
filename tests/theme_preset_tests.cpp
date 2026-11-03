#include "../src/platform/theme_preset.hpp"

#include <iostream>
#include <Windows.h>

int main() {
    const auto base = std::filesystem::temp_directory_path() / L"launchme-theme-test";
    std::error_code error;
    std::filesystem::remove_all(base, error);

    if (theme_preset::cleanName(L"  My: look?/\\ ") != L"My look" || !theme_preset::cleanName(L"...").empty() ||
        !theme_preset::cleanName(L"con").empty() || theme_preset::cleanName(std::wstring(80, L'a')).size() != 40) return 1;
    if (!theme_preset::list(base).empty()) return 2; // No folder yet.

    AppSettings neon;
    neon.home_layout = AppSettings::HomeLayout::flix;
    neon.font_family = L"Consolas";
    neon.ui_accent_rgb = 0xE60012;
    neon.tile_shape = AppSettings::TileShape::square;
    neon.glass = 0.2f;
    neon.metro_light = true;
    neon.drawer.opacity = 0.5f;
    neon.windowed = true; // Not a look.
    if (!theme_preset::save(L"Neon", neon, base) || !theme_preset::save(L"alpha", AppSettings{}, base)) return 3;
    if (theme_preset::save(L"???", neon, base) || theme_preset::list(base) != std::vector<std::wstring>{L"alpha", L"Neon"}) return 4;

    AppSettings mine;
    mine.windowed = false; mine.autostart = true;
    if (!theme_preset::load(L"Neon", mine, base)) return 5;
    if (!theme_preset::sameLook(mine, neon) || mine.windowed || !mine.autostart) return 6; // The look came, the rest stayed.
    if (theme_preset::sameLook(mine, AppSettings{}) || theme_preset::load(L"missing", mine, base)) return 7;
    if (!theme_preset::remove(L"Neon", base) || theme_preset::remove(L"Neon", base) || theme_preset::list(base).size() != 1) return 8;

    std::filesystem::remove_all(base, error);
    std::cout << "Theme presets passed\n";
    return 0;
}
