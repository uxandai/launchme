#include "theme_preset.hpp"

#include <algorithm>
#include <cmath>
#include <cwctype>
#include <system_error>
#include <type_traits>

namespace theme_preset {
namespace {
// One list for copying and comparing: every appearance field, by member.
#define LAUNCHME_LOOK_FIELDS(X) \
    X(home_layout) X(font_family) X(cover_accent_rgb) X(ui_accent_rgb) X(button_rgb) X(button_idle) \
    X(glow_level) X(glow_pulse) X(glow_trace) X(focus_spring) X(focus_gap) X(background_grid) \
    X(background_grid_style) X(tile_shape) X(tile_entrance) X(console_colors) X(panel_borders) \
    X(screen_transitions) X(glass) X(hint_style) X(menu_dialog) X(search_art) X(home_tiles) X(add_game_tile) \
    X(show_categories) X(shelves_posters) X(shelves_steam) X(shelves_category_all) X(shelves_hero_facts) X(xmb_posters) X(xmb_size) \
    X(flix_size) X(flix_category_all) X(flix_category_tabs) X(flix_rail) \
    X(metro_category_tabs) X(metro_3d) X(metro_light) X(psfive_size) X(psfive_clock) X(psfive_dim) X(psfive_details) \
    X(psfive_category_tabs) X(home_tab_icons) X(wallpaper) X(wallpaper_file) X(wallpaper_id) X(wallpaper_snitch) \
    X(wallpaper_menus) X(wallpaper_artwork) X(wallpaper_tiles)
} // namespace

void copyLook(const AppSettings& from, AppSettings& to) {
#define LAUNCHME_COPY(field) to.field = from.field;
    LAUNCHME_LOOK_FIELDS(LAUNCHME_COPY)
#undef LAUNCHME_COPY
    to.drawer = from.drawer;
}

bool sameLook(const AppSettings& a, const AppSettings& b) {
#define LAUNCHME_COMPARE(field) if (!(a.field == b.field)) return false;
    LAUNCHME_LOOK_FIELDS(LAUNCHME_COMPARE)
#undef LAUNCHME_COMPARE
    // Saved with six digits: close is the same.
    return std::abs(a.drawer.opacity - b.drawer.opacity) < 0.0005f && std::abs(a.drawer.fade_fraction - b.drawer.fade_fraction) < 0.0005f;
}

std::wstring cleanName(std::wstring_view name) {
    std::wstring clean;
    for (const wchar_t ch : name) {
        if (ch < 32 || std::wstring_view(L"<>:\"/\\|?*").find(ch) != std::wstring_view::npos) continue;
        clean.push_back(ch);
    }
    const auto first = clean.find_first_not_of(L" .");
    if (first == std::wstring::npos) return {};
    clean = clean.substr(first, clean.find_last_not_of(L" .") - first + 1);
    if (clean.size() > 40) clean.resize(40);
    // Windows device names are not file names.
    std::wstring upper = clean;
    for (auto& ch : upper) ch = static_cast<wchar_t>(std::towupper(ch));
    for (const auto* reserved : {L"CON", L"PRN", L"AUX", L"NUL", L"COM1", L"COM2", L"COM3", L"LPT1", L"LPT2", L"LPT3"})
        if (upper == reserved) return {};
    return clean;
}

std::filesystem::path folder() { return userConfigDirectory() / L"themes"; }

std::filesystem::path fileFor(std::wstring_view name, const std::filesystem::path& base) {
    const auto clean = cleanName(name);
    return clean.empty() ? std::filesystem::path{} : base / (clean + L".ini");
}

std::vector<std::wstring> list(const std::filesystem::path& base) {
    std::vector<std::wstring> names;
    std::error_code error;
    for (const auto& entry : std::filesystem::directory_iterator(base, error)) {
        if (!entry.is_regular_file(error) || entry.path().extension() != L".ini") continue;
        const auto name = entry.path().stem().wstring();
        if (cleanName(name) == name) names.push_back(name);
    }
    std::sort(names.begin(), names.end(), [](const std::wstring& a, const std::wstring& b) {
        return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end(),
                                            [](wchar_t x, wchar_t y) { return std::towlower(x) < std::towlower(y); });
    });
    return names;
}

bool save(std::wstring_view name, const AppSettings& settings, const std::filesystem::path& base) {
    const auto file = fileFor(name, base);
    if (file.empty()) return false;
    std::error_code error;
    std::filesystem::create_directories(base, error);
    AppSettings look;
    copyLook(settings, look);
    return saveAppSettings(look, file);
}

bool load(std::wstring_view name, AppSettings& settings, const std::filesystem::path& base) {
    const auto file = fileFor(name, base);
    std::error_code error;
    if (file.empty() || !std::filesystem::is_regular_file(file, error)) return false;
    copyLook(loadAppSettings(file), settings);
    return true;
}

bool remove(std::wstring_view name, const std::filesystem::path& base) {
    const auto file = fileFor(name, base);
    std::error_code error;
    return !file.empty() && std::filesystem::remove(file, error);
}

} // namespace theme_preset
