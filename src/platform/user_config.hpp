#pragma once
#include "../model/widget_layout.hpp"
#include "../model/companion_reply_style.hpp"

#include <array>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <cstdint>
#include <unordered_set>
#include <vector>

struct DrawerStyle {
    float opacity = 0.9f;
    float fade_fraction = 1.0f / 3.0f;
};

DrawerStyle parseDrawerStyle(std::string_view text);

struct AppSettings {
    DrawerStyle drawer;
    bool windowed = false; // Tryb TV in a window (F11) instead of full screen.
    // Start in Tryb TV (the console layouts below) instead of the desktop window (src/app/desktop*.cpp).
    bool launch_tv_mode = false;
    bool desktop_platform_badges = false;     // The desktop window's posters: the store's or console's mark top right.
    bool desktop_gray_uninstalled = false;    // ...and games not installed in black and white.
    // The desktop window: its restored position and size in physical pixels (width 0: a centred
    // default) and whether it was maximized.
    std::array<int, 4> desktop_window{};
    bool desktop_maximized = false;
    // The desktop library: poster width (DIPs), order (library_view::persistedOrder), installed games
    // only, the section shown, its selected game (library_view::selectionKey) and collapsed groups.
    int desktop_poster = 156;
    // The desktop window's panes: the right one's width (dragged by its edge; 0 a quarter of the
    // window) and the left one narrow, its icons only (the toggle by the logo).
    int desktop_pane = 0;
    bool desktop_narrow = false;
    int desktop_sort = 1;
    bool desktop_installed = false;
    std::string desktop_section = "all";
    std::string desktop_game;
    std::unordered_set<std::string> desktop_collapsed;
    // Sidebar groups folded by default (Wydawcy, Developerzy): the ones the player opened.
    std::unordered_set<std::string> desktop_expanded;
    bool show_uninstalled = true;
    bool recent_uninstalled = false; // Ostatnio grane also lists games not installed, by the date they were added.
    bool save_backup_after_game = false; // Ludusavi backs up a game's saves when it closes (Kopie zapisów).
    bool show_emulation = true;
    // Steam games the account marked hidden or private appear like the others (shelves.cpp).
    bool show_private = false;
    bool show_store = false;
    std::unordered_set<std::string> enabled_pc_sources;
    bool steam_enabled = true;
    // Szczegóły → "…" → Konto Steam: a game's chosen account (app id → SteamID64); absent: the default.
    std::map<std::string, uint64_t> steam_launch_accounts; // Ustawienia → Biblioteka → Steam: off hides its games, shortcuts and collections.
    // Platformy PC → MultiMC / Prism: a launcher the user pointed at (portable MultiMC is never
    // installed anywhere Launchme looks); empty: only the ones found on their own.
    std::filesystem::path multimc_path;
    float music_volume = 0.075f;
    int music_theme = 1; // An index of music_themes.hpp (saved by its id); 1: Launchme — Horizons.
    // Settings → Dźwięk → "Folder dźwięków": its audio files can be picked by name. music_file /
    // boot_file (a file name in that folder) win over music_theme / boot_sound while the folder
    // still holds them; otherwise the built-in choice plays.
    std::filesystem::path sound_folder;
    std::wstring music_file, boot_file;
    // The launcher's file picker opens here when a request names no folder ("Ostatnio używany").
    std::filesystem::path file_browser_folder;
    int ui_sound = 0;
    int toast_sound = 0;
    int boot_sound = 0;
    std::wstring font_family = L"SST";
    uint32_t cover_accent_rgb = 0;     // Selected cover border/glow; 0 = dominant poster color.
    uint32_t ui_accent_rgb = 0xEEEEEE; // Toggles, sliders, focus marks, toasts and progress (white).
    // Buttons: the focused one's fill (white by default; 0 = the interface accent) and the tint of the
    // others (grey frosted glass by default). Captions pick dark or light ink for contrast.
    uint32_t button_rgb = 0xF5F5F5;
    enum class ButtonIdle { gray, tinted, dark };
    ButtonIdle button_idle = ButtonIdle::gray;
    // The halo around the focused cover/tile outline: 0 = the outline alone, 1 = the original
    // strength, up to 2. Pulse and the travelling outline are mutually exclusive.
    float glow_level = 1.0f;
    bool glow_pulse = true;
    bool glow_trace = false;
    // Black dot grid over the background video (hides low-resolution trailers): 0 = off.
    float background_grid = 0.5f;
    // Its pattern: dots (LED), horizontal lines (CRT scanlines) or a mesh around every "pixel".
    enum class GridStyle { dots, lines, mesh };
    GridStyle background_grid_style = GridStyle::dots;
    // flix: landscape "recently played" row, then one row of posters per category.
    // metro: Xbox 360-style pages of tiles, a start page then one page per category.
    // psfive: PS5-style row of the recent games on top of a full-screen background, then Biblioteka.
    enum class HomeLayout { shelves, xmb, flix, metro, psfive, snitch };
    HomeLayout home_layout = HomeLayout::shelves;
    bool glance = true; // Default home's shortcut-only gaming hub (G / right trigger).
    // Tarot (src/app/tarot.cpp): a short card reading that recommends up to three games, from the
    // library or to buy on Steam (T / View, the desktop window's top bar). Off by default.
    bool tarot = false;
    // Companion (src/app/companion.cpp): an animated anime girl under Glance and her chat
    // (C / left trigger). Answers come from a Foundry-proxied Gemini model; offline she speaks
    // canned lines. The token is stored apart (DPAPI, companion_token.dat).
    bool companion = true;
    // The chosen companion: her key, the folder res/companion/<key>/ (companion_characters.hpp).
    // Empty (the default), unknown or a "soon" one: the first that can be chosen, by key
    // alphabetically. Her character (-dere) is her character.txt's, never a setting.
    std::string companion_look;
    // The other companions may visit her chat: once a day one drops in by herself, and the chat's
    // "…" menu calls one. Off: she is always alone.
    bool companion_guests = true;
    // Gamification (src/net/companion_affinity.hpp): her small sympathy bar and level (1–50) in the
    // chat header and on her home nameplate; reset from the chat's "…" menu. On by default.
    bool companion_gamification = true;
    // Who she talks with (src/net/companion_person.hpp): the name or nickname she may call you by
    // (empty: she doesn't know it yet) and your gender (auto: what she learns from the chat), so she
    // says "on"/"ona" — never "the player".
    std::wstring companion_user_name;
    enum class CompanionUserGender { automatic, male, female };
    CompanionUserGender companion_user_gender = CompanionUserGender::automatic;
    // Each companion's costume (src/net/companion_costumes.hpp): its folder, e.g. "costume_10_kimono"
    // (companion_costume_<key>); none: costume_00, her basic outfit.
    std::map<std::string, std::string, std::less<>> companion_costumes;
    bool companion_debug = false; // The chat's diagnostics panel ("…" → Pokaż diagnostykę): where each answer's time went.
    // Who answers for her (companion::Provider, same order): Foundry's Gemini or Grok proxy, Gemini,
    // Claude or Grok (xAI) with the user's own API key, or an Ollama server. Keys are stored apart
    // (DPAPI) per provider.
    enum class CompanionProvider { foundry, gemini, claude, ollama, xai };
    static constexpr std::array<const char*, 5> companion_provider_keys{"foundry", "gemini", "claude", "ollama", "xai"};
    CompanionProvider companion_provider = CompanionProvider::gemini; // A free Google AI Studio key is enough.
    std::string companion_model = "gemini-3-8-flash";   // Foundry and Gemini: our model ID (Foundry's without the RID prefix; grok-4-7 on Foundry).
    std::string companion_xai_model = "grok-4-7";       // xAI: our Grok ID (sent as grok-4.7).
    std::wstring companion_host;                        // Empty: FOUNDRY_HOST or opencode.
    std::string companion_claude_model = "auto-opus";   // auto-opus / auto-sonnet / auto-haiku: the newest of the family; or an exact ID.
    std::string companion_ollama_model;                 // Empty: the first model the server has.
    std::wstring companion_ollama_host;                 // Empty: OLLAMA_HOST or http://127.0.0.1:11434.
    bool companion_model_info = false;                  // The model's name in her chat header (hidden by default).
    companion::ReplyLength companion_reply_length = companion::ReplyLength::full; // Visible chat reply length; applied on the next request.
    // "Pisze…" as in a messenger: no streamed words or typewriter — the typing dots while she writes,
    // then each message appears whole (its typing time grows with its length).
    bool companion_typing = false;
    // The real weather where she is (Open-Meteo, no key; only her city goes out): on by default.
    bool companion_weather = true;
    // Where they live (companion::cities(), src/net/companion_characters.hpp): one city for all of
    // them (companion_city=<city key>; empty: each her own, character.txt's) and per companion over
    // it (companion_city_<key>=<city key>, or "home" for her own). The city sets her clock, so when
    // she sleeps; her neighbourhood the model makes up.
    std::string companion_city;
    std::map<std::string, std::string, std::less<>> companion_cities;
    // Phone access (src/app/companion_remote.cpp): her chat and Launchfeed in a browser, on this
    // port of every interface; the key goes in the page's address.
    bool companion_remote = false;
    int companion_remote_port = 6969;
    std::string companion_remote_key;
    // Her voice (src/app/companion_voice.cpp, src/net/elevenlabs.hpp): ElevenLabs in the interface's
    // language; in the chat only or also in her home remarks. The key is stored apart (DPAPI).
    enum class CompanionVoice { off, chat, everywhere };
    CompanionVoice companion_voice = CompanionVoice::off;
    std::string companion_voice_model; // An ElevenLabs model ID for all of them; empty: each one's character.txt (else v4 Turbo).
    // A voice picked in Settings for a companion (companion_voice_id_<key>=<voice ID>); none: her character.txt's.
    std::map<std::string, std::string, std::less<>> companion_voice_ids;
    // Her memory (src/net/companion_memory.hpp): recent remarks and suggestions (on), and notes
    // about the player with chat summaries (on by default). Both go to the model in her prompt.
    bool companion_memory = true;
    bool companion_notes = true;
    bool companion_semantic_memory = true; // Local multilingual embedding index; primary memories remain authoritative.
    // Her photos in the chat (src/app/companion_photos.cpp): once per chat, at most one a day, from
    // her own subfolder of a folder (<folder>/<her key>/). Available automatically when configured.
    enum class CompanionPhotos { off, folder };
    CompanionPhotos companion_photos = CompanionPhotos::folder; // Legacy preference; no on/off selector in the UI.
    std::wstring companion_photo_folder;
    std::wstring companion_feed_folder; // Launchfeed root: <character>/1.png + 1.txt; empty disables publication.
    std::wstring companion_import_folder; // Shared root: photos/<character>/ and feed/<character>/.
    void setCompanionImportFolder(const std::wstring& folder) {
        companion_import_folder = folder;
        companion_photo_folder = folder.empty() ? std::wstring{} : (std::filesystem::path(folder) / L"photos").wstring();
        companion_feed_folder = folder.empty() ? std::wstring{} : (std::filesystem::path(folder) / L"feed").wstring();
    }
    bool show_categories = false; // Default home: permanent category list instead of the left drawer.
    bool shelves_posters = false; // Default home: 2:3 game posters instead of square covers.
    bool shelves_category_all = false; // Default home: a "Wszystkie" tile first on game shelves (as flix_category_all).
    // Default home: the focused game's playtime, last played, release date and platform centred under
    // its logo (raised a little), each under a small capitalised heading; the corner badge moves there.
    bool shelves_hero_facts = false;
    bool xmb_posters = false; // XMB: portrait game artwork instead of square covers.
    enum class XmbSize { compact, normal, large };
    XmbSize xmb_size = XmbSize::normal;
    XmbSize psfive_size = XmbSize::normal; // PSfive tiles: the same three sizes (80 / 100 / 120%).
    bool psfive_clock = true;              // PSfive: the time in the top bar's right corner.
    enum class FlixSize { normal, large }; // Flix posters and capsules.
    FlixSize flix_size = FlixSize::normal;
    // Flix: a "Wszystkie" tile first in every game row (not Recent, PC platforms or Emulation),
    // with a random background of that category; it opens the library of the category.
    bool flix_category_all = false;
    // Flix: every quick-bar icon small in the top-right corner, beside the platform badge; the
    // companion shows only her avatar there and Glance only its icon.
    bool flix_top_icons = false;
    // Search results (Szukaj): square covers, 2:3 posters or 920:430 landscape tiles.
    enum class SearchArt { square, poster, landscape };
    SearchArt search_art = SearchArt::square;
    // Tryb TV: the view (library_view::View's saved number) of a console's, a collection's and a
    // publisher's or developer's library until it gets its own (Interfejs → Widoki biblioteki).
    int default_view_console = 0, default_view_collection = 0, default_view_studio = 0;
    // Metro: the pages beside the focused one stand back at 80% until they take the focus.
    bool metro_3d = false;
    // Metro / Flix: the library's category strip in the middle at the top (LB/RB, Page Up/Down change
    // the category; Metro: in place of its own lower-case tabs).
    bool metro_category_tabs = true, flix_category_tabs = true;
    bool psfive_category_tabs = true; // PSfive: the strip in place of its three tabs and "Więcej...".
    enum class TileShape { squircle, rounded, square };
    TileShape tile_shape = TileShape::squircle;
    // Thin frames around dialogs, context menus and the walkthrough bubble/card.
    bool panel_borders = false;
    // Smooth fade-in, zoom and subtle edge bow when opening or leaving screens.
    bool screen_transitions = true;
    // Frosted glass of dialogs, menus, drawers and the Settings backdrop: 0 = solid panels, 1 = the
    // strongest blur and the most see-through tint.
    float glass = 0.7f;
    // Button hints in the bottom-right corner: the pad's buttons after pad input, keys otherwise.
    bool input_hints = true;
    // Their symbols: from the active pad's family, or always one console's.
    enum class HintGlyphs { automatic, xbox, playstation, nintendo };
    HintGlyphs hint_glyphs = HintGlyphs::automatic;
    // Their key caps (and the small ones beside the category strips): light glass, or white with dark ink.
    enum class HintStyle { glass, white };
    HintStyle hint_style = HintStyle::glass;
    // The starting game's background and logo for a few seconds (src/ui/launch_animation.hpp). Always
    // on and no longer saved or shown in Ustawienia; tests switch it off to launch without it.
    bool launch_animation = true;
    // Steam games start through steam.exe -silent (a Steam started for them stays in the tray) and
    // Steam gets no foreground permission; Launchme hands focus to the game window itself.
    bool steam_silent_launch = false;
    // Xbox account games: Instaluj downloads a GDK title with Launchme's Xodus-based helper into a
    // chosen folder (experimental) instead of handing it to the Xbox app (store/xbox_account.hpp).
    bool xbox_own_install = false;
    // While a game started here runs, the window is hidden (no taskbar button, no Alt+Tab) and an
    // icon in the notification area stands in for it; it comes back when the game closes.
    bool hide_while_playing = false;
    // A Steam game installed by another account remembered on this PC: Steam restarts signed in to
    // that account, then the game starts (src/library/steam_accounts.hpp).
    bool steam_account_switch = true;
    // Installing a Steam game: Launchme's dialog picks the library (with its free space) instead of
    // Steam's install window.
    bool steam_install_dialog = true;
    // Optional out-of-process Steam features; changing the backend takes effect on restart.
    bool aurelia = false;
    bool aurelia_intro_done = false;
    // Games Aurelia manages (installed, launched and removed through it), each chosen; with
    // aurelia_default every Steam game but the ones in aurelia_steam (managed by the Steam client).
    std::unordered_set<std::string> aurelia_standalone;
    bool aurelia_default = false;
    std::unordered_set<std::string> aurelia_steam;
    bool aurelia_cloud = true; // Cloud state and sync of the games Aurelia manages.
    // Games whose saves are not synced before and after standalone play (pulled before, pushed after for the others).
    std::unordered_set<std::string> aurelia_cloud_skip;
    // Videos downloaded from ScreenScraper: <video_folder>\<ES-DE system>\videos\<ROM name>.mp4, the
    // layout of ES-DE's downloaded_media (pointing it there shares them). Empty: <config>\media.
    std::filesystem::path video_folder;
    bool game_videos = true;
    int video_delay = 3;   // Seconds on a game before its video: 1, 3, 5 or 10.
    int video_volume = 2;  // The video's own sound: 0 muted, 1 quiet, 2 normal, 3 loud.
    int trailer_space_mb = 250;  // Trailer clips kept at most (100, 250, 500, 1000); the oldest-played go first.
    bool steam_trailers = false; // Steam games: the store trailer (480p) the same way, downloaded once.
    // Emulated shortcuts: the title ES-DE/ScreenScraper give the ROM instead of the shortcut name.
    bool emulation_names = false;
    // Emulated shortcuts: covers and logos matched by ROM file (ES-DE, ScreenScraper) before SteamGridDB.
    bool emulation_rom_art = true; // 3 s on an emulated game: its ES-DE/ScreenScraper video as the background, music ducked.
    // Emulation console tiles on their platform colour (emulation_platforms.hpp) in every home
    // layout and library view; off: the dark fill of the store tiles.
    bool console_colors = true;
    // Picking a game (Enter, A, Space, double click) in every view, desktop and PSfive included: its
    // details (default), or launch/install. The game menu, the details screen and Play buttons still
    // launch directly.
    enum class AcceptAction { launch, details };
    AcceptAction accept_action = AcceptAction::details;
    // Interface language; system follows the Windows display language (Polish, Portuguese, Spanish or English).
    enum class LanguageChoice { system, pl, en, pt_br, es };
    LanguageChoice language = LanguageChoice::system;
    std::unordered_set<std::string> hidden_collections;
    std::vector<std::string> collection_order;
    // Wydawcy and Developerzy (src/app/studio_libraries.cpp): the libraries of the publishers and developers
    // with a logo; hidden ones ("publisher:capcom") and the player's order.
    bool studio_libraries = true;
    std::unordered_set<std::string> hidden_studios;
    std::vector<std::string> studio_order;
    bool quick_resume = false; // Experimental: suspend/resume running games from the game menu.
    // Guide/PS opens the launcher menu (hold: power). Off when Game Bar or Steam
    // already own that button on this PC.
    bool guide_menu = true;
    bool guide_overlay = false; // Experimental: Guide over a foreground game.
    bool game_fps = false, game_fps_low = false; // Read-only widget in the right drawer.
    std::vector<widgets::Preference> widget_layout; // Stable IDs, including temporarily disabled plugins.
    // Experimental: read the 2026 Steam Controller over HID while Steam is not running.
    bool steam_controller = false;
    // Experimental: the focus outline springs on its own, apart from the cover (PS5-like),
    // with an inner glow at list edges; menu highlights overshoot slightly too.
    bool focus_spring = false;
    // An empty band between the focus outline and the picture: none, the outline moved outward by its
    // own width (outside), or the outline on the tile's edge and the picture drawn smaller inside it.
    enum class FocusGap { none, outside, inside };
    FocusGap focus_gap = FocusGap::none;
    // Tiles appearing on a fresh Tryb TV screen (startup, from the desktop window, a new layout):
    // growing one after another from their centres like the PS5 (with a light stagger), the older
    // rise from below, or at once.
    enum class TileEntrance { grow, rise, none };
    TileEntrance tile_entrance = TileEntrance::grow;
    // Screensaver (src/app/screensaver.cpp): cached game artwork after an idle period.
    enum class ScreensaverStyle { backgrounds, boxes, discs };
    bool screensaver = true;
    ScreensaverStyle screensaver_style = ScreensaverStyle::backgrounds;
    int screensaver_minutes = 1; // 1, 3, 5 or 10.
    // DRM-free folders (src/app/drm_free.cpp): rescan after startup and while the launcher idles.
    bool drm_background = true;
    // First-run walkthrough (src/app/tour.cpp) finished or skipped. Only a missing settings.ini
    // (the first start) loads false; files written before the walkthrough existed count as done.
    bool tour_done = true;
    // Launchme's own updates (src/app/updates.cpp): look for a newer release on GitHub; a version
    // the user chose to skip gets no notification (Ustawienia → System still offers it).
    bool update_check = true;
    std::wstring update_skipped;
    // Not stored: settings.ini exists but could not be read (locked, damaged). These are the
    // defaults; saveAppSettings() keeps the unreadable file as settings.unreadable.ini first.
    bool load_failed = false;
    // Not stored: no settings.ini yet (the first start).
    bool first_start = false;
    // The first PC store scan of this profile switched on the stores with installed games
    // (only when none was on); afterwards the user's choice in Platformy stands.
    bool pc_sources_detected = false;
    // Tryb TV on several monitors: the display it covered last (\\.\DISPLAY2); empty = primary.
    std::wstring tv_monitor;
    // Start with Windows (HKCU Run entry, written by Launcher::applyAutostart), minimized.
    bool autostart = false;
    // System → Wydajność → Tryb niskiego zużycia RAM: artwork decoded at its size on screen, encoded
    // bytes not kept once uploaded, fewer bitmaps kept around the view, the browser window suspended
    // while hidden, and everything given back while a game runs in front of the launcher
    // (src/app/memory.cpp).
    bool low_memory = true;
    // System → Wydajność → Kompresja grafik (BC1): covers, posters and thumbnails kept on the GPU as BC1
    // blocks (an eighth of BGRA), logos as BC3 (a quarter); backgrounds too in the low memory mode.
    // The blocks are kept on disk (textures\), so a picture is compressed once. On by default
    // (stored as texture_compression; the older low_memory_bc1=1 turns it on as well).
    bool texture_compression = true;
    // Media i dane → Źródła grafik: the galleries "Zmień grafikę" may offer (artwork_sources slugs,
    // src/artwork/artwork_sources.hpp) that were switched off; every source is on by default.
    std::unordered_set<std::string> artwork_sources_off;
    // Interfejs → Tapeta (src/app/wallpaper.cpp): a picture or a looping video chosen online or from
    // disk, kept in <config>\wallpapers; none: the launcher's own animated backdrop stays.
    enum class WallpaperKind { none, picture, video };
    WallpaperKind wallpaper = WallpaperKind::none;
    std::filesystem::path wallpaper_file;
    uint32_t wallpaper_id = 0; // Its gallery entry (the check mark in the picker).
    // Where it shows: snitch's backdrop, Settings and the menu screens, Zmień grafikę, and home tiles
    // without a picture of their own (XMB categories, Wszystkie, Glance...).
    bool wallpaper_snitch = true, wallpaper_menus = true, wallpaper_artwork = true, wallpaper_tiles = true;
    // Tryb TV: Wszystkie, Glance, Tarot and Towarzyszka (with Dodaj grę) as tiles of the first row in
    // every layout, leading or closing it; layout: each layout's own way (floating widgets).
    enum class HomeTiles { layout, first, last };
    HomeTiles home_tiles = HomeTiles::layout;
    bool add_game_tile = false; // A "+" tile opening Dodaj grę.
    // PSfive: Down from Graj / … scrolls to the game's details, screenshots and similar games.
    bool psfive_details = true;
    // The game menu as a large scrollable dialog with an icon beside each option.
    bool menu_dialog = true;
    // Ustawienia > Biblioteka > Gry emulowane: the ROM folder read by launchme's own parsers
    // (src/library/emulation_scan.hpp; Steam ROM Manager's shortcuts of emulated platforms and its
    // collections are then left out; with no ROM folder found the shortcuts stay), or those shortcuts.
    enum class EmulationSource { roms, steam };
    EmulationSource emulation_source = EmulationSource::roms;
    std::filesystem::path emulation_roms; // Empty: the one ES-DE or EmuDeck names.
    // Ustawienia > Biblioteka > Emulacja (src/library/emulators.hpp): an emulator's program and its
    // arguments set by hand (emulator ID -> empty fields: found on its own, its default), and the
    // emulator a system folder starts with (system -> emulator ID; absent: the first one found).
    struct EmulatorOverride {
        std::filesystem::path program;
        std::wstring arguments;
        bool operator==(const EmulatorOverride&) const = default;
    };
    std::map<std::string, EmulatorOverride> emulator_overrides;
    std::map<std::string, std::string> system_emulator;
    // Metro: a white page with dark text (the light theme of Windows Phone).
    bool metro_light = false;
};

// The name or nickname she may call you by (AppSettings::companion_user_name): trimmed, one line,
// at most 40 characters, without control characters or the marks her texts use (‹ › |).
std::wstring companionUserName(std::wstring_view text);
AppSettings parseAppSettings(std::string_view text);
AppSettings loadAppSettings();
AppSettings loadAppSettings(const std::filesystem::path& file);
bool saveAppSettings(const AppSettings& settings);
bool saveAppSettings(const AppSettings& settings, const std::filesystem::path& file);

std::filesystem::path userConfigDirectory();
// Environment variables take precedence over files in %LOCALAPPDATA%\Launchme.
std::wstring userSecret(const wchar_t* environment, const wchar_t* filename);
// Settings → Library: writes the key file (one line); empty input removes it.
bool saveUserSecret(const wchar_t* filename, std::wstring_view value);
bool storedUserSecret(const wchar_t* filename);
