#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>
#include "../model/game.hpp"
#include "../model/devices.hpp"
#include "../i18n/i18n.hpp"

namespace settings_ui {
// Settings 2.0: eight categories, each split into titled sections (Row::section).
// Biblioteka holds the PC platforms (their pages behind the cog), "Moje gry" and the
// collections page; Media i dane the background videos, emulation data and API keys.
#ifdef _WIN32
enum class Category { appearance, library, media, audio, controllers, plugins, companion, system };
#else
// Linux: Zgodność (compat) after Biblioteka: Proton and Wine, their prefixes and Windows games' options.
enum class Category { appearance, library, compat, media, audio, controllers, plugins, companion, system };
#endif
enum class Id { font, home_layout, ui_accent, accent, uninstalled, emulation, collection, music, music_theme, ui_sound, toast_sound, boot_sound, opacity, fade, pc_platform, gog_callback, gog_refresh, gog_logout, quick_resume, guide_menu, guide_overlay, game_fps, game_fps_low, language, custom_add, custom_game,
                screensaver, screensaver_style, screensaver_minutes, screensaver_start, steam_key, sgdb_key, epic_callback, epic_refresh, epic_logout, tile_shape, rawg_key, show_categories, xmb_posters, xmb_size,
                ea_callback, ea_refresh, ea_remove, battlenet_import, battlenet_callback, battlenet_remove, steam_controller, focus_spring, flix_size,
                plugin_install, plugin_enabled, plugin_setting, plugin_remove, pc_connect, pc_client, pc_refresh, pc_excluded, steam_sync,
                 drm_scan, drm_background, drm_add_root, drm_root, drm_game, show_private, drm_games, tour, start_reset, panel_borders,
                 flix_category_all, accept_action, console_colors, metro_3d, shelves_posters, shelves_steam, shelves_category_all, sound_folder, collection_manage, windowed,
                 glow_level, glow_pulse, glow_trace, screenscraper, screenscraper_open, game_videos, video_folder, video_folder_reset, screenscraper_test, screenscraper_clear, video_delay, video_volume, steam_trailers, emulation_names, emulation_rom_art, trailer_space, background_grid, background_grid_style, psfive_size, psfive_clock, custom_games,
                 // Settings 2.0.
                  glass, input_hints, hint_glyphs, audio_sources, system_volume, bt_audio, bt_devices, windows_sound, controller_pads, collections_open,
                  steam_silent_launch, steam_hide_launch_window, steam_account_switch, steam_install_dialog, bulk_covers, screen_transitions, glance,
                  // Towarzyszka (Asystent).
                  companion, companion_look, companion_model, companion_token, companion_host, companion_test,
                  companion_voice, companion_voice_key, companion_voice_model, companion_voice_style, companion_voice_test,
                  companion_memory, companion_notes, companion_semantic_memory, companion_forget, companion_provider, companion_ollama_model, companion_model_info,
                  companion_photos, companion_photo_folder, companion_guests, companion_gamification,
                  // Interfejs → Tryb TV: start in it instead of the desktop window.
                  launch_tv_mode,
                  // Interfejs  Okno desktopowe: posters' marks of their store; games not installed in black and white.
                  desktop_platform_badges, desktop_gray_uninstalled,
                  // Towarzyszka: the Tavily key for her web search (szukaj_w_sieci).
                   companion_web_key, companion_feed_folder, companion_backup, companion_reply_length, companion_import_folder, companion_typing, companion_weather,
                   // Towarzyszka: who she talks with — your name or nickname and your gender (she says "on"/"ona").
                   companion_user_name, companion_user_gender,
                   // Towarzyszka: her chat and Launchfeed in a phone's browser (companion_remote.cpp).
                   companion_remote, companion_remote_address,
                   // System: start with Windows; the version and the data folder (logs, crash reports).
                   autostart, about, discord,
                   // A tile grouping related settings on a page of their own (Control::page, Row::page).
                   page,
                   // Interfejs → Zaznaczona okładka: the glow's animation (still, pulsing or a travelling outline).
                   glow_motion,
                   // Interfejs → Wygląd: the focused button's colour and the other buttons' tint.
                   button_color, button_idle,
                   // Interfejs → Zaznaczona okładka: the outline an empty ring away from its tile.
                   focus_gap,
                   // Interfejs → Tryb TV: how tiles appear; the artwork of search results.
                   tile_entrance, search_art,
                   // Interfejs: opens the theme studio (Dostosuj wygląd).
                   theme_studio,
                   // Opcje układu: the default home's facts under the logo.
                   shelves_hero_facts,
                   // System → Funkcje: Tarot, the card reading (src/app/tarot.cpp).
                   tarot,
                   // Towarzyszka → Gdzie mieszkają: one city for all of them, and the chosen one's own.
                   companion_city, companion_city_own,
                   // System → Aktualizacje: Launchme's own update (src/app/updates.cpp) and whether to look for one.
                   update, update_check,
                   // Platformy PC → MultiMC / Prism: the launcher picked by hand (portable MultiMC).
                   multimc_path, studio_publishers, studio_developers, studio_companies, studio_min_games, studio_hidden, studio_hidden_entry, studio_show_all,
                   updated_row, discovery_row, home_tab_icons, flix_rail, excluded_games, excluded_game, excluded_restore_all,
                   // System → Wydajność: the low memory mode (AppSettings::low_memory) and Kompresja grafik
                   // (AppSettings::texture_compression).
                   low_memory, texture_compression,
                   // Biblioteka → Uruchamianie: hidden in the notification area while a game runs (src/app/tray.cpp).
                   hide_while_playing, aurelia, aurelia_open, aurelia_cloud, aurelia_default,
                   // Biblioteka → Amazon Games: Nile (the helper), the sign-in address, sync and sign-out.
                   amazon_helper, amazon_callback, amazon_refresh, amazon_logout,
                   // Biblioteka → Xbox: the Xodus-based helper, sync, sign-out and Instalacja przez Launchme.
                   xbox_helper, xbox_refresh, xbox_logout, xbox_own_install,
                   // Biblioteka → Gry dodane ręcznie: find and download their missing covers.
                   custom_covers,
                   // Linux: Biblioteka → Proton i Wine (src/app/compat_rows.cpp): the default tool, the
                   // installed tools and components, umu-launcher, downloads, the prefixes folder.
                   compat_default_tool, compat_defaults, compat_prefixes, compat_prefix_root, compat_umu, compat_tool,
                   compat_source, compat_release, compat_steam_target, compat_source_refresh, compat_status,
                   // The prefix manager and one prefix's page (Row::key: its path, UTF-8).
                   prefix_new, prefix_entry, prefix_name, prefix_tool, prefix_folder, prefix_size, prefix_games, prefix_create,
                   prefix_program, prefix_winetricks, prefix_run, prefix_kill, prefix_delete,
                   // A game's (or the default) Wine/Proton options: the accordion's sections (Control::fold,
                   // Row::key the section's id), an option (Row::key its catalog id), WINEDLLOVERRIDES
                   // presets (Row::key their DLLs), environment variables (Row::key the index).
                   launch_scope, launch_fold, launch_option, launch_dll, launch_env, launch_env_add, launch_env_preset, launch_edit_game,
                   // Zgodność: a game from a Windows installer; a game's shortcuts in Steam, on the desktop, in the menu.
                   compat_install_game, launch_steam, launch_shortcut_desktop, launch_shortcut_menu, launch_log,
                   // Biblioteka → Widoczność: not installed games in Ostatnio grane.
                   recent_uninstalled,
                   // A Windows game's page: its ProtonDB and Steam Deck rating.
                   launch_protondb,
                   // Zgodność → Sklepy i launchery: the page, and one launcher to install (Row::key: its id).
                   compat_stores_open, compat_store_install,
                   // Biblioteka → Kopie zapisów: Ludusavi after each game, and Ludusavi itself.
                   save_backups, ludusavi,
                   // Biblioteka → Skróty gier: every installed game into the Start/applications menu or Steam.
                   shortcuts_all_menu, shortcuts_all_steam,
                   // A game's (or the default) options: their search and its clearing; WINEDLLOVERRIDES
                   // from the presets list (Row::key their DLLs) or typed in.
                   launch_search, launch_search_clear, launch_dll_preset, launch_dll_add,
                   // A game's page: winetricks and Wine programs with its own Proton or Wine and prefix.
                   launch_tricks,
                   // Interfejs → Widoki biblioteki: Tryb TV's view of a console's, a collection's and a
                   // publisher's library without its own; Row::reset ("views:<kind>") makes all follow it.
                   default_view_console, default_view_collection, default_view_studio,
                   // Media i dane → Źródła grafik: one source on or off (Row::key its slug); TheGamesDB's key.
                   artwork_source, thegamesdb_key,
                   // Interfejs → Tapeta: the picker, back to none, and where it shows.
                   wallpaper_pick, wallpaper_clear, wallpaper_snitch, wallpaper_menus, wallpaper_artwork, wallpaper_tiles,
                   // Interfejs → Tryb TV: the special tiles in the first row; the "+" tile (Dodaj grę).
                   home_tiles, add_game_tile,
                   // Opcje układu: PSfive's details under the row; Metro's light theme. Okna: the game menu as a dialog.
                   psfive_details, metro_light, snitch_colour_icons, menu_dialog, emulation_source, emulation_folder, emulation_folder_reset,
                   // Biblioteka → Emulacja (src/app/emulation_settings.cpp): a system's row (Row::key its folder),
                   // the other systems' fold; a system's page: its emulator, how it was found, the program
                   // chosen by hand and its reset, the launch arguments and the command they make.
                   emulation_system, emulation_more, emulation_emulator, emulation_detected, emulation_program,
                    emulation_program_reset, emulation_arguments, emulation_preview,
                    emulation_system_folder, emulation_system_folder_reset, emulation_system_rescan,
                   // DRM-free → Katalogi: the page of the scanned folders.
                   drm_roots,
                   // Metro / Flix → Pasek kategorii (AppSettings::metro_category_tabs / flix_category_tabs).
                   metro_category_tabs, flix_category_tabs, psfive_category_tabs,
                   // Okna i podpowiedzi → Wygląd podpowiedzi (AppSettings::hint_style).
                   hint_style,
                   // Biblioteka → Uruchamianie: FPS widget's one-time ETW permission (src/platform/etw_access.cpp).
                   fps_access, fps_graph, fps_limiter, fps_limit, fps_limit_precise, fps_limit_mode, fps_badges, hud_enabled, hud_layout, hud_label_colors, hud_size, cpu_temp_source, hud_pin, fps_game_limit, fps_limiter_status,
                   // Sterowanie → Przycisk Guide: Guide in a game opens on the widgets.
                   guide_open_widgets,
                   // Search in Settings (src/app/settings_search.cpp): the field, and a found setting (Row::key its place in the index).
                   settings_search, settings_hit,
                   // Opcje układu → PSfive: how much darker the background picture is.
                   psfive_dim };
inline bool hasOptionsButton(Id id) { return id == Id::plugin_enabled || id == Id::pc_platform; }
// layout: drawn preview tiles in the row (one per home layout); Row::choice is the active one.
// count: Row::value as a large number with a chevron; activating opens a sub-list.
// sources: two tiles side by side (Row::tiles: output and input); A opens a picker of the tile's choices.
// pads: a gallery of controller tiles with their pictures, the last one pairs a new pad.
// avatar: two animated previews of the companion's looks; Row::choice is the active one.
// page: a tile opening its page of settings (Row::page): Row::value as a short summary and a chevron.
// fold: an accordion section's heading: Row::checked open (the rows after it, Row::indent 1, are its
// contents), Row::value a short summary; activating opens or closes it.
enum class Control { choice, toggle, collection, slider, secret, platform, action, layout, text, count, sources, pads, avatar, page, fold };
// Drill-down pages: a row opens a page whose rows replace its category's list; Back returns to the
// row it was opened from. Pages nest (a DRM-free catalogue → its found games).
enum class Page {
    plugin, platform, drm_games, screenscraper, custom_games, collections,
    // Settings grouped behind one tile (Control::page).
    layout_options, cover_focus, backdrop, videos, service_keys, sounds, screensaver,
    companion_model, companion_voice, companion_remote,
    // Linux: Proton i Wine; one download source's releases (key: its id); the prefix manager; one
    // prefix (key: its path); Wine/Proton options of a game (key: its game key) or the default (key empty).
    compat, compat_source, prefixes, prefix, launch_options,
    compat_stores, // Linux: store launchers installed in a prefix of their own (Battle.net, EA app...).
    game_shortcuts, // One game's shortcuts outside Launchme (key: its game key); Tryb TV's way to them.
    artwork_sources, // Media i dane → Źródła grafik: the galleries of "Zmień grafikę", each on or off.
    // Biblioteka → Emulacja: the ROM folder, the emulators and the console row; one system (key: its folder).
    emulation, emulation_system,
    fps_limiter, // Funkcje -> FPS i limiter: the widget, its graph, the frame limiter and the limits of single games.
    hidden_studios, // Biblioteka → Ukryci wydawcy i developerzy: each one with a switch to show it again.
    excluded_games, // Biblioteka → Widoczność → Wykluczone gry: each title with Przywróć.
    drm_roots, // DRM-free → Katalogi: adding a folder and every folder with its removal.
    settings_search, // Search in every category's settings: the field and the settings found (src/app/settings_search.cpp).
    appearance_all // Interfejs → Wszystkie opcje wyglądu: the classic list of every appearance setting (the studio has them as pictures).
};
// A row drawn as tiles, with State::tile picking one of them.
inline bool tiled(Control control) { return control == Control::sources || control == Control::pads; }
inline std::wstring companionImportHelp() {
    return std::wstring(tr(L"Wskaż wspólny folder, np. companion_import. Przykładowa struktura:")) +
        L"\n\ncompanion_import/photos/saki/1.png\ncompanion_import/photos/saki/10-19/2.png\ncompanion_import/feed/saki/1.png + 1.txt\n\n" +
        tr(L"Zamiast saki użyj klucza postaci. Zdjęcia: PNG, JPEG, WebP, GIF lub BMP; opis o tej samej nazwie w TXT jest opcjonalny. Zakresy 00-09 … 50-59 odblokowują się z poziomem postaci (od pierwszej liczby; najwyższy poziom to 50). Feed wymaga pary zdjęcie + TXT. Brak plików oznacza brak zdjęć lub postów.");
}
// Polish keys: draw them through tr(). Indexed like AppSettings::HomeLayout.
inline constexpr std::array<const wchar_t*, 6> home_layouts{trKey(L"Domyślny"), L"XMB", L"Flix", L"Metro", L"PSfive", L"snitch"};
inline constexpr int home_layout_count = static_cast<int>(home_layouts.size());
// The layout tiles of Settings, the desktop window's settings and the walkthrough are a slider:
// `visible` tiles at a time, the one cut at an edge shows there are more. Its leftmost slot
// (fractional while it glides) keeps `focus` in the middle.
inline float layoutFirst(int focus, float visible) {
    const float last = std::max(0.0f, static_cast<float>(home_layout_count) - visible);
    return std::clamp(static_cast<float>(focus) - (visible - 1.0f) * 0.5f, 0.0f, last);
}
inline constexpr std::array<const wchar_t*, 3> xmb_sizes{trKey(L"Mały"), trKey(L"Domyślny"), trKey(L"Duży")};
inline constexpr std::array<const wchar_t*, 2> flix_sizes{trKey(L"Domyślny"), trKey(L"Duży")}; // AppSettings::FlixSize.
// Stepped sliders: the number of positions (0 = a continuous slider).
inline int sizeSteps(Id id) { return id == Id::xmb_size || id == Id::psfive_size || id == Id::companion_reply_length ? 3 : id == Id::flix_size ? 2 : id == Id::hud_size ? 6 : 0; }
// Indexed like AppSettings::AcceptAction.
inline constexpr std::array<const wchar_t*, 2> accept_actions{trKey(L"Uruchom / instaluj"), trKey(L"Szczegóły")};
inline constexpr std::array<int, 4> video_delays{1, 3, 5, 10};
inline constexpr std::array<int, 4> trailer_spaces{100, 250, 500, 1000}; // MB
inline constexpr std::array<int, 4> studio_min_counts{1, 2, 3, 5};      // Najmniej gier of a studio.
inline constexpr std::array<const wchar_t*, 4> video_volumes{trKey(L"Wyciszony"), trKey(L"Cicho"), trKey(L"Normalnie"), trKey(L"Głośno")};
inline constexpr std::array<double, 4> video_volume_levels{0.0, 0.2, 0.45, 0.8};
// Indexed like AppSettings::GridStyle.
inline constexpr std::array<const wchar_t*, 3> grid_styles{trKey(L"Kropki"), trKey(L"Linie poziome"), trKey(L"Kratka")};
// Indexed like AppSettings::TileShape.
inline constexpr std::array<const wchar_t*, 3> tile_shapes{L"Squircle", trKey(L"Zaokrąglone"), trKey(L"Kwadratowe")};
struct CategoryInfo { const wchar_t* title; const wchar_t* description; };
// Indexed like Category.
inline constexpr auto categories = std::to_array<CategoryInfo>({
    {trKey(L"Interfejs"), trKey(L"Układ ekranu, wygląd, kolory i animacje.")},
    {trKey(L"Biblioteka"), trKey(L"Platformy, gry spoza sklepów i kolekcje.")},
#ifndef _WIN32
    {trKey(L"Zgodność"), trKey(L"Proton, Wine, prefiksy i domyślne opcje gier Windows.")},
#endif
    {trKey(L"Media i dane"), trKey(L"Filmy w tle, dane emulacji i klucze usług.")},
    {trKey(L"Dźwięk"), trKey(L"Urządzenia audio, głośność, muzyka i dźwięki interfejsu.")},
    {trKey(L"Kontrolery"), trKey(L"Połączone pady, parowanie i przycisk Guide.")},
    {trKey(L"Pluginy"), trKey(L"Rozszerzenia launchera i ich ustawienia.")},
    {trKey(L"Asystent"), trKey(L"Postać, model AI, głos, zdjęcia i Launchfeed.")},
    {trKey(L"System"), trKey(L"Okno, język, wygaszacz i funkcje systemowe.")}
});
// Indexed like AppSettings::HintGlyphs.
inline constexpr std::array<const wchar_t*, 4> hint_glyph_styles{trKey(L"Automatycznie"), L"Xbox", L"PlayStation", L"Nintendo"};
// Lexend (OFL, Google Fonts) travels with the program: assets/fonts/Lexend-*.ttf.
inline constexpr std::array<const wchar_t*, 5> fonts{L"SST", L"Segoe UI", L"Consolas", L"Arial", L"Lexend"};
// Indexed like AppSettings::ScreensaverStyle.
inline constexpr std::array<const wchar_t*, 3> screensaver_styles{trKey(L"Tła gier"), trKey(L"Pudełka 3D"), trKey(L"Płyty")};
// Console colours keep their brand names in every language. After the presets each accent
// picker lists "Inny…" (Choice::custom), which opens the HSV editor (ColorEditor).
inline constexpr std::array<uint32_t, 7> accents{0, 0xEEEEEE, 0x9A9A9A, 0x52B043, 0x0070D1, 0xE60012, 0x34BEED};
inline constexpr std::array<const wchar_t*, 7> accent_names{
    trKey(L"Z okładki"), trKey(L"Biały"), trKey(L"Szary"), L"Xbox Green", L"PlayStation Blue", L"Switch Red", L"Wii Blue"};
// Interface accent always needs a concrete colour; the first entry is the default.
inline constexpr std::array<uint32_t, 6> ui_accents{0xEEEEEE, 0x9A9A9A, 0x52B043, 0x0070D1, 0xE60012, 0x34BEED};
inline constexpr std::array<const wchar_t*, 6> ui_accent_names{
    trKey(L"Biały (domyślny)"), trKey(L"Szary"), L"Xbox Green", L"PlayStation Blue", L"Switch Red", L"Wii Blue"};
// The focused button's fill: white by default, the interface accent (0) or a console colour.
inline constexpr std::array<uint32_t, 8> button_colors{0xF5F5F5, 0, 0x0070D1, 0x52B043, 0xE60012, 0x34BEED, 0x8E6CFF, 0xFF8A2A};
inline constexpr std::array<const wchar_t*, 8> button_color_names{
    trKey(L"Biały (domyślny)"), trKey(L"Jak akcent interfejsu"), L"PlayStation Blue", L"Xbox Green", L"Switch Red", L"Wii Blue",
    trKey(L"Fioletowy"), trKey(L"Pomarańczowy")};
// Indexed like AppSettings::ButtonIdle.
inline constexpr std::array<const wchar_t*, 3> button_idles{trKey(L"Szare szkło (domyślne)"), trKey(L"W kolorze przycisku"), trKey(L"Ciemne")};
// Indexed like AppSettings::TileEntrance.
inline constexpr std::array<const wchar_t*, 2> hint_styles{trKey(L"Szklane"), trKey(L"Białe")}; // AppSettings::HintStyle.
inline constexpr std::array<const wchar_t*, 3> tile_entrances{trKey(L"Powiększanie po kolei"), trKey(L"Wysuwanie z dołu"), trKey(L"Bez animacji")};
// Indexed like AppSettings::FocusGap.
inline constexpr std::array<const wchar_t*, 3> focus_gaps{trKey(L"Brak"), trKey(L"Obrys odsunięty na zewnątrz"), trKey(L"Grafika odsunięta do środka")};
// Indexed like AppSettings::SearchArt.
inline constexpr std::array<const wchar_t*, 3> search_arts{trKey(L"Kwadratowe okładki"), trKey(L"Pionowe plakaty"), trKey(L"Poziome kafle")};
// Indexed like AppSettings::HomeTiles.
inline constexpr std::array<const wchar_t*, 3> home_tile_places{trKey(L"Jak w układzie"), trKey(L"Na początku rzędu"), trKey(L"Na końcu rzędu")};
struct Choice { std::wstring label; uint32_t color = 0; bool custom = false; std::wstring key; };

// "Inny…" colour: hue (0..360), saturation and value (0..1) edited with three sliders.
// focus: 0..2 the sliders, 3 the Save button.
struct ColorEditor {
    bool open = false;
    Id target = Id::ui_accent;
    std::wstring title;
    float hue = 0, saturation = 0, value = 1;
    int focus = 0;
    bool dragging = false;
    // focus: the three sliders, then Save (right) and Cancel (left) on the bottom line.
    static constexpr int sliders = 3, save = 3, cancel = 4;
    static uint32_t rgb(float hue, float saturation, float value) {
        hue = std::fmod(std::fmod(hue, 360.0f) + 360.0f, 360.0f);
        saturation = std::clamp(saturation, 0.0f, 1.0f); value = std::clamp(value, 0.0f, 1.0f);
        const float c = value * saturation, h = hue / 60.0f, x = c * (1 - std::fabs(std::fmod(h, 2.0f) - 1)), m = value - c;
        float r = 0, g = 0, b = 0;
        switch (static_cast<int>(h)) {
        case 0: r = c; g = x; break;
        case 1: r = x; g = c; break;
        case 2: g = c; b = x; break;
        case 3: g = x; b = c; break;
        case 4: r = x; b = c; break;
        default: r = c; b = x; break;
        }
        const auto byte = [](float channel) { return static_cast<uint32_t>(std::lround(std::clamp(channel, 0.0f, 1.0f) * 255)); };
        return byte(r + m) << 16 | byte(g + m) << 8 | byte(b + m);
    }
    uint32_t rgb() const { return rgb(hue, saturation, value); }
    void set(uint32_t color) {
        const float r = ((color >> 16) & 255) / 255.0f, g = ((color >> 8) & 255) / 255.0f, b = (color & 255) / 255.0f;
        const float high = std::max({r, g, b}), low = std::min({r, g, b}), delta = high - low;
        value = high;
        saturation = high > 0 ? delta / high : 0;
        if (delta <= 0) return; // Grey: keep the previous hue, so the slider does not jump.
        hue = high == r ? 60 * std::fmod((g - b) / delta, 6.0f) : high == g ? 60 * ((b - r) / delta + 2) : 60 * ((r - g) / delta + 4);
        if (hue < 0) hue += 360;
    }
    // Normalised slider position and its inverse.
    float level(int slider) const { return slider == 0 ? hue / 360.0f : slider == 1 ? saturation : value; }
    void setLevel(int slider, float level) {
        level = std::clamp(level, 0.0f, 1.0f);
        if (slider == 0) hue = std::min(level * 360.0f, 359.0f); else if (slider == 1) saturation = level; else value = level;
    }
    // One press of Left/Right: 6° of hue, 5% of saturation or value.
    void step(int slider, int amount) {
        if (slider == 0) hue = std::clamp(std::round(hue / 6.0f) * 6.0f + amount * 6.0f, 0.0f, 359.0f);
        else setLevel(slider, std::round(level(slider) * 20.0f + amount) / 20.0f);
    }
};
// One tile of a Control::sources or Control::pads row.
// sources: caption "Odtwarzanie"/"Nagrywanie", title the default device, level its volume (-1 unknown),
//          choices every device of that direction (Choice::key = endpoint ID), choice the default one.
// pads:    title the pad's name, value its connection and battery, picture its family; key the
//          Bluetooth address for Połącz/Rozłącz (action), empty = the "Sparuj kontroler" tile.
struct Tile {
    std::wstring caption, title, value, key;
    devices::Picture picture = devices::Picture::none;
    float level = -1;
    std::optional<int> battery;
    bool muted = false, input = false, connected = true, pair = false, charging = false;
    std::wstring action; // Button caption; empty: the tile has no action.
    std::vector<Choice> choices;
    int choice = 0;
};
struct Row {
    Id id;
    Control control;
    std::wstring title, description, value;
    bool checked = false;
    float level = 0; // Normalized slider position.
    std::vector<Choice> choices;
    int choice = 0;
    GameSource source = GameSource::steam;
    std::string key; // Id::custom_game / drm_game: the game's ID; Id::drm_root: the folder (UTF-8).
    bool locked = false; // Shown in its current state; the switch does not change (Id::collection from SRM).
    std::string plugin_setting; // Plugin setting/contribution ID; key identifies its owner.
    std::vector<std::wstring> tags;
    // Nonempty: a section heading drawn above this row (Layout::section_gap). Headings are never
    // focused; navigation moves between rows as before.
    std::wstring section;
    std::vector<Tile> tiles; // Control::sources and Control::pads.
    Page page = Page::plugin; // Control::page: the page it opens.
    // More than two: the description wraps to this many lines and the row grows to fit them.
    int lines = 0;
    // Nested under an open Control::fold heading: drawn inset by this many steps.
    int indent = 0;
    // One line (the title, `detail` under it) while another row has the focus; the focused row grows to
    // show its description (Layout::of, settings_sheet::Layout::of).
    bool dense = false;
    std::wstring detail; // Dense rows: what it sets ("PROTON_DXVK_LOWLATENCY=1").
    // A pill before the controls ("Ta gra": the game's own value, not the default's). With `reset`
    // (what goes back to the default) it is a button: a click, Delete or the pad's X.
    std::wstring badge;
    std::string reset;
    // Id::collection: an own collection, its trash (after ↑ ↓) deletes it after a question.
    bool removable = false;
    // The list a picker shows for this row: its choices, or those of the focused source tile.
    const std::vector<Choice>& pickList(int tile) const {
        if (control == Control::sources && !tiles.empty()) return tiles[std::clamp(tile, 0, static_cast<int>(tiles.size()) - 1)].choices;
        return choices;
    }
    int picked(int tile) const {
        if (control == Control::sources && !tiles.empty()) return tiles[std::clamp(tile, 0, static_cast<int>(tiles.size()) - 1)].choice;
        return choice;
    }
    const std::wstring& pickTitle(int tile) const {
        if (control == Control::sources && !tiles.empty()) return tiles[std::clamp(tile, 0, static_cast<int>(tiles.size()) - 1)].caption;
        return title;
    }
    // Rows whose editing state is a picker of choices.
    bool picker() const { return control == Control::choice || control == Control::sources; }
};

// Biblioteka → Emulacja's tile among the platforms: their icon column and a cog (Opcje) in place of a chevron.
inline bool optionsTile(const Row& row) { return row.control == Control::page && row.id == Id::page && row.page == Page::emulation; }

// A row's identity: rows of one kind (DRM-free folders, collections, plugin settings, page tiles)
// are told apart by their key, source and plugin setting.
struct RowRef {
    Id id = Id::font;
    GameSource source = GameSource::steam;
    std::string key, setting;
    static RowRef of(const Row& row) { return {row.id, row.source, row.key, row.plugin_setting}; }
    bool matches(const Row& row) const { return row.id == id && row.source == source && row.key == key && row.plugin_setting == setting; }
};
// One open page: what it shows (plugin: its ID in key; platform: the GameSource in value) and the
// place in its parent's list it was opened from, restored by Back.
struct PageFrame {
    Page page = Page::plugin;
    std::string key;
    int value = 0;
    int parent_row = 0;
    float parent_scroll = 0;
    std::optional<RowRef> opener; // Found again by identity when the page closes.
};

struct State {
    int category = 0;
    bool options = false;
    bool editing = false;
    int choice = 0;
    int subcontrol = 0;
    // Rail focus on the Back button under the last category (Down from it); keyboard and pad.
    bool back_focused = false;
    // Pages opened from the category's rows, innermost last; empty: the category's own list.
    std::vector<PageFrame> pages;
    // Set when a page closes: the row it was opened from, focused by the next rebuild of the rows.
    std::optional<RowRef> reopen;
    // The focused tile of a Control::sources / Control::pads row.
    int tile = 0;
    std::array<int, categories.size()> selected{};
    std::array<float, categories.size()> scroll{};
    int row() const { return selected[category]; }
    float offset() const { return scroll[category]; }
    bool backFocused() const { return back_focused && !options; }
    bool pluginList() const { return category == static_cast<int>(Category::plugins) && pages.empty(); }
    // A page opened from a row of its category (its rows replace the category's list).
    bool subpage() const { return !pages.empty(); }
    const PageFrame* top() const { return pages.empty() ? nullptr : &pages.back(); }
    bool in(Page page) const {
        return std::any_of(pages.begin(), pages.end(), [&](const PageFrame& frame) { return frame.page == page; });
    }
    bool at(Page page) const { return !pages.empty() && pages.back().page == page; }
    // The plugin whose options page is open; empty: the plugin list.
    std::string plugin() const {
        for (const auto& frame : pages) if (frame.page == Page::plugin) return frame.key;
        return {};
    }
    // The PC platform whose page is open (also under its DRM-free game list); empty: the overview.
    std::optional<GameSource> platform() const {
        for (const auto& frame : pages) if (frame.page == Page::platform) return static_cast<GameSource>(frame.value);
        return std::nullopt;
    }
    // Opens a page from the focused row: its list starts at the first row.
    void open(Page page, std::string key = {}, int value = 0, std::optional<RowRef> opener = std::nullopt) {
        pages.push_back({page, std::move(key), value, row(), offset(), std::move(opener)});
        selected[category] = 0; scroll[category] = 0;
        options = true; editing = false; subcontrol = 0;
        reopen.reset();
    }
    // Back from the innermost page to the row it was opened from.
    void close() {
        if (pages.empty()) return;
        auto frame = std::move(pages.back());
        pages.pop_back();
        selected[category] = frame.parent_row; scroll[category] = frame.parent_scroll;
        reopen = std::move(frame.opener);
        editing = false; options = true; subcontrol = 0;
    }
    // Closes `page` and every page opened from it; false when it is not open.
    bool close(Page page) {
        if (!in(page)) return false;
        while (!pages.empty()) { const bool last = pages.back().page == page; close(); if (last) break; }
        return true;
    }
    // Back to the category's own list at the place the outermost page was opened from.
    void closeAll() {
        while (!pages.empty()) close();
        reopen.reset();
    }
    void clamp(int count) { selected[category] = std::clamp(row(), 0, std::max(0, count - 1)); }
    void selectCategory(int index) {
        closeAll();
        category = std::clamp(index, 0, static_cast<int>(categories.size()) - 1);
        options = editing = back_focused = false;
        subcontrol = 0;
        tile = 0;
    }
    void move(int amount, int rows) {
        if (options) { selected[category] += amount; clamp(rows); return; }
        if (back_focused) { if (amount > 0) back_focused = false; return; } // Down returns to the first category.
        if (amount < 0 && category == 0) { back_focused = true; return; }  // Up from the first one: Back at the top.
        selectCategory(category + amount);
    }
    void enter(int count) { if (count > 0) { options = true; back_focused = false; subcontrol = 0; clamp(count); } }
    // False means the caller should leave Settings. Back never changes a value.
    bool back() {
        if (editing) { editing = false; return true; }
        if (options) { options = false; subcontrol = 0; return true; }
        return false;
    }
};

struct Rect {
    float left, top, right, bottom;
    bool contains(float x, float y) const { return x >= left && x < right && y >= top && y < bottom; }
};

// One-shot preview. Repeated mouse moves inside the same tile never restart it;
// leaving and entering again (or changing keyboard/pad focus) does.
struct LayoutPreview {
    int option = -1;
    uint64_t started = 0;
    bool final_frame = false;
    void focus(int next, uint64_t now) {
        if (next == option) return;
        option = next; started = now; final_frame = next >= 0;
    }
    float seconds(uint64_t now) const { return option < 0 ? 0 : std::min(2.0f, static_cast<float>(now >= started ? now - started : 0) / 1000.0f); }
    bool tick(uint64_t now) {
        if (option < 0) return false;
        if (seconds(now) < 2) return true;
        const bool draw = final_frame; final_frame = false; return draw;
    }
};

inline bool described(int category) {
    return *categories[std::clamp(category, 0, static_cast<int>(categories.size()) - 1)].description != L'\0';
}

// Geometry is shared by rendering, pointer hit testing and focus scrolling.
// Header: the heading's capitals start at heading_cap_top; a category description sits
// directly beneath it and pushes the list down by one line, so the gap to the first row
// stays the same with and without it. Every row control is centred on the row's centre line.
struct Layout {
    float width, height;
    bool described = true; // Use of(): the category shows a description under its heading.
    bool plugin_list = false;
    // The page's first row of previews or tiles (home layouts, the companions, sound sources or the
    // controller gallery), -1 none.
    int layout_index = -1;
    bool compact = false; // Thin rows of a long list (DRM-free games).
    std::vector<int> sections; // Rows with a section heading above them, ascending (Row::section).
    // Every row taller or shorter than row_height (previews, tiles, a long description, a dense row
    // without the focus) with its height, ascending.
    std::vector<std::pair<int, float>> talls;
    static constexpr float row_height = 102, row_step = 114, row_center = row_height * 0.5f;
    static constexpr float compact_height = 70, compact_step = 78;
    static constexpr float button_height = 44, button_radius = 8, layout_height = 272;
    static constexpr float sources_height = 178, pads_height = 252, tile_gap = 16, pad_tile_width = 216, avatar_height = 330;
    float tall_height = layout_height;
    bool import_help = false;
    static float tallHeight(Control control) {
        return control == Control::sources ? sources_height : control == Control::pads ? pads_height : control == Control::avatar ? avatar_height : layout_height;
    }
    static bool tall(Control control) { return control == Control::layout || control == Control::avatar || tiled(control); }
    // One more line of a description (Row::lines) beyond the two every row has room for.
    static constexpr float description_line = 22;
    // focused: a dense row (Row::dense) shows its description only then.
    static float rowHeightOf(const Row& row, bool focused = true) {
        if (tall(row.control)) return tallHeight(row.control);
        if (row.dense && !focused) return compact_height;
        return row.lines > 2 ? row_height + (row.lines - 2) * description_line : row_height;
    }
    // A row's badge (Row::badge), before its controls; a button when the row has Row::reset.
    Rect badge(int index, float scroll, Control control) const {
        const auto r = row(index, scroll);
        const float y = middle(r), right = controlsLeft(control) - (control == Control::toggle ? 12.0f : 16.0f);
        return {right - 132, y - 17, right, y + 17};
    }
    static constexpr float badge_space = 148; // What a badge takes from the row's text.
    static constexpr float heading_cap_top = 90, described_top = 186, plain_top = 156, rail_top = plain_top;
    static constexpr float footer_logo = 36; // System's logo at the bottom, level with Back (Renderer::drawLaunchmeLogo).
    // Extra space above a row that starts a section; its heading is centred section_label above the row.
    static constexpr float section_gap = 66, section_label = 30;
    // Without the rows: the Interfejs layout preview is its first row (settings_rows.cpp).
    static Layout of(float width, float height, int category, bool plugin_list = false) {
        Layout result{width, height, settings_ui::described(category), plugin_list};
        if (category == static_cast<int>(Category::appearance)) { result.layout_index = 0; result.talls = {{0, layout_height}}; }
        return result;
    }
    // The page currently shown by `state` (plugin list, compact DRM-free game list).
    static Layout of(float width, float height, const State& state) {
        auto result = of(width, height, state.category, state.pluginList());
        result.compact = state.at(Page::drm_games);
        if (state.subpage()) { result.layout_index = -1; result.talls.clear(); }
        return result;
    }
    // The same page with its section headings and tall rows taken from `rows`.
    static Layout of(float width, float height, const State& state, const std::vector<Row>& rows) {
        auto result = of(width, height, state);
        result.import_help = state.row() >= 0 && state.row() < static_cast<int>(rows.size()) && rows[state.row()].id == Id::companion_import_folder;
        result.layout_index = -1;
        result.talls.clear();
        for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
            if (!result.compact) {
                const float h = rowHeightOf(rows[i], state.options && state.row() == i);
                if (h != row_height) result.talls.push_back({i, h});
                if (tall(rows[i].control) && result.layout_index < 0) { result.layout_index = i; result.tall_height = h; }
            }
            if (!rows[i].section.empty()) result.sections.push_back(i);
        }
        return result;
    }
    bool sectionStart(int index) const { return std::binary_search(sections.begin(), sections.end(), index); }
    float sectionOffset(int index) const {
        return static_cast<float>(std::upper_bound(sections.begin(), sections.end(), index) - sections.begin()) * section_gap;
    }
    float rowStep() const { return compact ? compact_step : row_step; }
    static float middle(const Rect& r) { return (r.top + r.bottom) * 0.5f; }
    float listTop() const { return described ? described_top : plain_top; }
    float railRight() const { return std::clamp(width * 0.235f, 300.0f, 374.0f); }
    float contentLeft() const { return railRight() + 56; }
    float contentRight() const { return width - 64; }
    float listBottom() const { return height - 138; }
    Rect rail(int index) const { return {56, rail_top + index * 74, railRight() - 22, rail_top + 64 + index * 74}; }
    // Settings' categories down the rail: 74 apart, closer (and their pills lower) when they would
    // reach Back (Linux has a ninth, Zgodność, on the 900-high canvas).
    float categoryStep() const {
        const int count = static_cast<int>(categories.size());
        return count > 1 ? std::min(74.0f, (height - 110 - 12 - rail_top - 64) / static_cast<float>(count - 1)) : 74.0f;
    }
    Rect categoryRail(int index) const {
        const float step = categoryStep(), pill = std::min(64.0f, step - 8);
        return {56, rail_top + index * step, railRight() - 22, rail_top + pill + index * step};
    }
    // Back: a glass dot level with the heading (the rail's pill glides up onto it); backBar is the line at the bottom the file browser's buttons use.
    Rect backButton() const { return {56, 86, 92, 122}; }
    Rect backBar() const { return {56, height - 110, railRight() - 22, height - 62}; }
    Rect saveStatus() const { return {contentRight() - 340, height - 100, contentRight(), height - 58}; }
    Rect row(int index, float scroll) const {
        const float y = listTop() + rowOffset(index) - scroll;
        return {contentLeft(), y, contentRight(), y + rowHeight(index)};
    }
    float rowHeight(int index) const {
        if (compact) return compact_height;
        for (const auto& [row, tall] : talls) if (row == index) return tall;
        return row_height;
    }
    float rowOffset(int index) const {
        float extra = 0;
        for (const auto& [row, tall] : talls) if (row < index) extra += tall - row_height;
        return index * rowStep() + (plugin_list && index > 0 ? 80.0f : 0.0f) + extra + sectionOffset(index);
    }
    // Tiles of a Control::sources row (two halves) or Control::pads row (a sliding gallery whose
    // `focus` tile stays in view). Shared by drawing, clicks and the focus frame.
    int visibleTiles(Control control, int count) const {
        if (control == Control::sources) return count;
        return std::max(1, static_cast<int>((contentRight() - contentLeft() + tile_gap) / (pad_tile_width + tile_gap)));
    }
    int firstTile(Control control, int count, int focus) const {
        const int visible = visibleTiles(control, count);
        return count <= visible ? 0 : std::clamp(focus - visible / 2, 0, count - visible);
    }
    Rect tile(int index, float scroll, Control control, int which, int count, int focus) const {
        const auto r = row(index, scroll);
        if (control == Control::sources) {
            const int n = std::max(1, count);
            const float w = (r.right - r.left - tile_gap * (n - 1)) / n, left = r.left + which * (w + tile_gap);
            return {left, r.top, left + w, r.bottom};
        }
        const float left = r.left + (which - firstTile(control, count, focus)) * (pad_tile_width + tile_gap);
        return {left, r.top, left + pad_tile_width, r.bottom};
    }
    int hitTile(int index, float scroll, Control control, int count, int focus, float x, float y) const {
        const int first = firstTile(control, count, focus), end = std::min(count, first + visibleTiles(control, count));
        for (int i = first; i < end; ++i) if (tile(index, scroll, control, i, count, focus).contains(x, y)) return i;
        return -1;
    }
    Rect field(int index, float scroll) const { const auto r = row(index, scroll); const float y = middle(r); return {r.right - 324, y - 22, r.right - 24, y + 22}; }
    Rect actionButton(int index, float scroll) const { auto r = field(index, scroll); r.left = r.right - 176; return r; }
    Rect pluginCog(int index, float scroll) const { const auto r = row(index, scroll); const float y = middle(r); return {r.right - 72, y - 22, r.right - 24, y + 22}; }
    // Left edge of a row's right-hand controls; the row's title and description end before it.
    float controlsLeft(Control control) const {
        const float right = contentRight();
        switch (control) {
        case Control::slider: return right - 400;     // Value readout, then − track +.
        case Control::toggle: return right - 206;     // State label, then the switch.
        case Control::collection: return right - 392; // State label, switch, ↑ ↓, the trash.
        case Control::platform: return right - 440;   // State label, switch, then the connect button.
        case Control::layout: case Control::avatar: return contentLeft() + 24;
        case Control::count: return right - 220;      // The number, then a chevron.
        case Control::page: case Control::fold: return right - 300; // A short summary, then a chevron.
        default: return right - 324;
        }
    }
    Rect minus(int index, float scroll) const {
        const auto r = row(index, scroll); const float y = middle(r);
        return {r.right - 306, y - 22, r.right - 262, y + 22};
    }
    Rect plus(int index, float scroll) const {
        const auto r = row(index, scroll); const float y = middle(r);
        return {r.right - 68, y - 22, r.right - 24, y + 22};
    }
    Rect track(int index, float scroll) const {
        const auto r = row(index, scroll); const float y = middle(r);
        return {r.right - 247, y - 16, r.right - 77, y + 16};
    }
    // Slider value readout, right-aligned just before the minus button (drawn only).
    Rect sliderValue(int index, float scroll) const {
        const auto m = minus(index, scroll);
        return {m.left - 84, m.top, m.left - 12, m.bottom};
    }
    Rect reorder(int index, float scroll, bool down) const {
        const auto r = row(index, scroll); const float y = middle(r);
        const float x = r.right - (down ? 120 : 172);
        return {x, y - 22, x + 44, y + 22};
    }
    // A collection row's trash, last (Row::removable; the place stays empty for a Steam collection).
    Rect trash(int index, float scroll) const {
        const auto r = row(index, scroll); const float y = middle(r);
        return {r.right - 68, y - 22, r.right - 24, y + 22};
    }
    // Preview tile of a Control::layout row; option 0 is left. The large row (Interfejs) is a slider of
    // layout_visible tiles; `first` is its leftmost slot (layoutFirstOf, gliding in the renderer).
    static constexpr float layout_tile = 136, layout_step = 148, layout_visible = 4.5f;
    Rect layoutOption(int index, float scroll, int option, float first = 0) const {
        const auto r = row(index, scroll);
        if (index == layout_index) {
            const float pitch = (r.right - r.left - 48 + 16) / layout_visible;
            const float left = r.left + 24 + (static_cast<float>(option) - first) * pitch;
            return {left, r.top + 84, left + pitch - 16, r.bottom - 20};
        }
        const float step = std::min(layout_step, (r.right - r.left - 48 + layout_step - layout_tile) / home_layout_count);
        const float right = r.right - 24 - (home_layout_count - 1 - option) * step;
        return {right - (step - (layout_step - layout_tile)), r.top + 10, right, r.bottom - 10};
    }
    // Where the layout slider shows its tiles: the renderer clips to it, a pointer outside it hits none.
    Rect layoutBand(int index, float scroll) const {
        const auto r = row(index, scroll);
        return index == layout_index ? Rect{r.left + 18, r.top + 70, r.right - 18, r.bottom - 8} : r;
    }
    float layoutFirstOf(int index, int choice) const { return index == layout_index ? layoutFirst(choice, layout_visible) : 0.0f; }
    // A Control::avatar row: a slider of the companions' cards under its title, up to three at a
    // time; `first` is the leftmost one shown (avatarFirst). A card out of view is empty.
    static constexpr int avatar_visible = 3;
    static int avatarFirst(int choice, int count) { return std::clamp(choice - 1, 0, std::max(0, count - avatar_visible)); }
    Rect avatarOption(int index, float scroll, int option, int count, int first) const {
        const auto r = row(index, scroll);
        const int visible = std::clamp(count, 1, avatar_visible), slot = option - first;
        if (slot < 0 || slot >= visible) return {};
        const float w = (r.right - r.left - 48 - 16.0f * static_cast<float>(visible - 1)) / static_cast<float>(visible);
        const float left = r.left + 24 + static_cast<float>(slot) * (w + 16);
        return {left, r.top + 84, left + w, r.bottom - 20};
    }
    // The same card while the slider glides: `first` fractional, a card partly in view kept (the
    // renderer clips the band). Empty: wholly out of view.
    Rect avatarSlide(int index, float scroll, int option, int count, float first) const {
        const auto r = row(index, scroll);
        const int visible = std::clamp(count, 1, avatar_visible);
        const float slot = static_cast<float>(option) - first;
        if (slot <= -1 || slot >= static_cast<float>(visible)) return {};
        const float w = (r.right - r.left - 48 - 16.0f * static_cast<float>(visible - 1)) / static_cast<float>(visible);
        const float left = r.left + 24 + slot * (w + 16);
        return {left, r.top + 84, left + w, r.bottom - 20};
    }
    Rect connect(int index, float scroll) const {
        return actionButton(index, scroll);
    }
    // 0 the switch, 1 ↑, 2 ↓, 3 the trash.
    Rect collectionControl(int index, float scroll, int control) const {
        if (control == 3) return trash(index, scroll);
        if (control) return reorder(index, scroll, control == 2);
        const auto r = row(index, scroll); const float y = middle(r);
        return {r.right - 244, y - 22, r.right - 192, y + 22};
    }
    int hitRail(float x, float y) const {
        for (int i = 0; i < static_cast<int>(categories.size()); ++i) if (categoryRail(i).contains(x, y)) return i;
        return -1;
    }
    int hitRow(float x, float y, int count, float scroll) const {
        if (y < listTop() || y >= listBottom()) return -1;
        if (!sections.empty() || talls.size() > 1 || (talls.size() == 1 && talls.front().first != layout_index)) {
            // Headings or several tall rows between rows: a short list, found by its rectangles.
            for (int i = 0; i < count; ++i) if (row(i, scroll).contains(x, y)) return i;
            return -1;
        }
        float local = y - listTop() + scroll;
        if (plugin_list && local >= row_step) { if (local < row_step + 80) return -1; local -= 80; }
        if (layout_index >= 0 && local >= layout_index * row_step) {
            if (local < layout_index * row_step + tall_height)
                return layout_index < count && row(layout_index, scroll).contains(x, y) ? layout_index : -1;
            local -= tall_height - row_height;
        }
        const int index = static_cast<int>(std::floor(local / rowStep()));
        return index >= 0 && index < count && row(index, scroll).contains(x, y) ? index : -1;
    }
    float maxScroll(int count) const {
        return count > 0 ? std::max(0.0f, rowOffset(count - 1) + rowHeight(count - 1) - (listBottom() - listTop())) : 0;
    }
    float ensureVisible(int index, int count, float scroll) const {
        auto r = row(index, scroll);
        if (sectionStart(index)) r.top -= section_gap; // Bring the row's heading into view with it.
        if (r.top < listTop()) scroll -= listTop() - r.top;
        else if (r.bottom > listBottom()) scroll += r.bottom - listBottom();
        return std::clamp(scroll, 0.0f, maxScroll(count));
    }
    // A long list (the sound folder's files) shows as many choices as fit and scrolls with the
    // highlighted one (`selected`, -1 = the top).
    int visibleChoices() const { return std::max(3, static_cast<int>((height - 200) / 54)); }
    int firstChoice(int count, int selected) const {
        const int visible = visibleChoices();
        return count <= visible || selected < 0 ? 0 : std::clamp(selected - visible / 2, 0, count - visible);
    }
    Rect picker(int count) const {
        const float h = 100.0f + std::min(count, visibleChoices()) * 54;
        const float w = std::min(620.0f, width - 96);
        return {(width - w) * 0.5f, (height - h) * 0.5f, (width + w) * 0.5f, (height + h) * 0.5f};
    }
    Rect choice(int index, int count, int selected = -1) const {
        const auto p = picker(count);
        const int row = index - firstChoice(count, selected);
        return {p.left + 18, p.top + 83 + row * 54, p.right - 18, p.top + 133 + row * 54};
    }
    int hitChoice(float x, float y, int count, int selected = -1) const {
        const int first = firstChoice(count, selected), end = std::min(count, first + visibleChoices());
        for (int i = first; i < end; ++i) if (choice(i, count, selected).contains(x, y)) return i;
        return -1;
    }
    Rect secretPanel() const {
        const float w = import_help ? 800.0f : 680.0f, h = import_help ? 560.0f : 252.0f;
        return {(width - w) * 0.5f, (height - h) * 0.5f, (width + w) * 0.5f, (height + h) * 0.5f};
    }
    Rect secretSave() const { const auto p = secretPanel(); return {p.right - 204, p.bottom - 72, p.right - 28, p.bottom - 28}; }
    Rect secretCancel() const {
        const auto p = secretPanel(), s = secretSave();
        return import_help ? Rect{s.left - 192, s.top, s.left - 16, s.bottom} : Rect{p.left + 28, p.bottom - 72, p.left + 72, p.bottom - 28};
    }
    Rect secretBrowse() const { const auto p = secretPanel(); return {p.left + 28, p.bottom - 72, p.left + 248, p.bottom - 28}; }
    // "Inny…" colour editor (ColorEditor): the swatch on the left with its hex below, three labelled
    // sliders (label, track, readout), then Cancel (a frame with a dot) and Save at the bottom.
    Rect colorPanel() const { return {width * 0.5f - 390, height * 0.5f - 200, width * 0.5f + 390, height * 0.5f + 200}; }
    Rect colorSwatch() const { const auto p = colorPanel(); return {p.left + 28, p.top + 84, p.left + 188, p.top + 244}; }
    Rect colorLabel(int slider) const {
        const auto p = colorPanel(); const float y = p.top + 104 + slider * 70.0f;
        return {p.left + 220, y - 20, p.left + 360, y + 20};
    }
    Rect colorTrack(int slider) const {
        const auto p = colorPanel(); const float y = p.top + 104 + slider * 70.0f;
        return {p.left + 370, y - 16, p.right - 116, y + 16};
    }
    Rect colorValue(int slider) const { const auto t = colorTrack(slider); return {t.right + 14, t.top, colorPanel().right - 28, t.bottom}; }
    Rect colorSave() const { const auto p = colorPanel(); return {p.right - 204, p.bottom - 72, p.right - 28, p.bottom - 28}; }
    Rect colorCancel() const { const auto p = colorPanel(); return {p.left + 28, p.bottom - 72, p.left + 72, p.bottom - 28}; }
};
} // namespace settings_ui
