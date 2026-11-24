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
    // Tryb TV on a handheld's small screen (Steam Deck, ROG Ally, Legion Go): everything a quarter
    // larger (Renderer::setHandheld: a 1280x720 canvas instead of 1600x900).
    bool handheld_mode = false;
    bool handheld_detected = false; // The first start looked at the hardware once (handheld::detect).
    bool pad_cursor = true; // Tryb TV: L3 + R3 turns the pad into a mouse (left stick moves, R2 clicks, L2 goes back).
    bool desktop_platform_badges = false;     // The desktop window's posters: the store's or console's mark top right.
    bool desktop_gray_uninstalled = false;    // ...and games not installed in black and white.
    // The desktop window's sidebar (Interfejs → Panel boczny): Sklep, Platformy, Wydawcy and Developerzy shown.
    bool desktop_sidebar_store = true, desktop_sidebar_platforms = true, desktop_sidebar_studios = true;
    // Widżety → Discord: the client its tile opens (Vesktop, Legcord…); empty: Discord's own (stable, PTB, Canary).
    std::filesystem::path discord_program;
    // The selected poster: an outline in the cover's colour (Kolor akcentu okładek) and its art's glow behind it.
    bool desktop_poster_glow = true;
    int desktop_theme = 0; // The window's colours: desktop_themes in settings_view.hpp (desktop_theme.hpp).
    // The desktop window's own accent (switches, focus, progress, links) and its primary buttons
    // (Graj, Zainstaluj); 0: the button takes the accent. Tryb TV keeps ui_accent_rgb and button_rgb.
    uint32_t desktop_accent_rgb = 0xEEEEEE;
    uint32_t desktop_button_rgb = 0;
    // The desktop window: its restored position and size in physical pixels (width 0: a centred
    // default) and whether it was maximized.
    std::array<int, 4> desktop_window{};
    bool desktop_maximized = false;
    // The desktop library: poster width (DIPs), order (library_view::persistedOrder), installed games
    // only, the section shown, its selected game (library_view::selectionKey) and collapsed groups.
    int desktop_poster = 156;
    int stage_dim = 0; // Scena (every library in that view): the game's background darkened by this percent (0-80, steps of 10).
    int desktop_view = 0; // The grid's cells: 0 posters (2:3), 1 squares (covers), 2 landscape (capsules), 3 a list.
    // The desktop window's panes: the right one's width (dragged by its edge; 0 a quarter of the
    // window) and the left one narrow, its icons only (the toggle by the logo).
    int desktop_pane = 0;
    bool desktop_narrow = false;
    int desktop_sort = 1;
    bool desktop_installed = false;
    uint32_t desktop_filters = 0; // Filtry: library_view::Filter bits, every one required.
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
    // Opcje uruchamiania → Uruchamianie: a game started through a plugin launch provider instead of the
    // store's own path (Steam app id → plugin id); absent: the normal path.
    std::map<std::string, std::string> plugin_launch;
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
    // Motyw muzyczny → Własny plik…: any audio file (.mp3, .wav, .flac) by its full path; while it exists it
    // wins over music_file and music_theme.
    std::filesystem::path music_path;
    // The launcher's file picker opens here when a request names no folder ("Ostatnio używany").
    std::filesystem::path file_browser_folder;
    int ui_sound = 0;    // Zestaw dźwięków: ui_sounds (sound_cues.hpp), ui_sound_none = Brak.
    int toast_sound = 0; // Powiadomienia: 0 off, 1 on (toast_sounds).
    int boot_sound = 0;
    // Dźwięki interfejsu: which moments of the set play (moves, menus and Back, a game's start), its
    // volume in percent and a mute that keeps that volume.
    bool ui_sound_moves = true, ui_sound_menus = true, ui_sound_launch = true, ui_muted = false;
    int ui_volume = 100;
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
    // The Stores' built-in tabs (src/app/store_hub.cpp): Launchery (install a store client or a front
    // end with one click) and Promocje (free games and discounts), before the store plugins' own.
    bool store_hub = true;
    // Linux, Proton i Wine → GE-Proton: once a day the newest GE-Proton is downloaded when one is in use
    // (games on the automatic version take it at once; pinned ones keep theirs).
    bool compat_auto_update = true;
    // The home screen's Launchery row: the installed launchers of that tab as tiles. Off until the player
    // turns it on (Biblioteka → Ekran główny i kolekcje), like every other row there.
    bool show_launchers = false;
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
    bool shelves_steam = false;   // Default home, "Wygląd Steam": posters, and Ostatnie as one row of equal height with a wide first tile. Implies shelves_posters.
    bool shelves_category_all = false; // Default home: a "Wszystkie" tile first on game shelves (as flix_category_all).
    // Default home: the focused game's playtime, last played, release date and platform centred under
    // its logo (raised a little), each under a small capitalised heading; the corner badge moves there.
    bool shelves_hero_facts = false;
    bool xmb_posters = false;        // XMB: portrait game artwork instead of square covers.
    bool xmb_landscape = false;      // XMB: landscape game tiles instead of square covers or posters.
    bool xmb_recent_icon = false;    // XMB: Ostatnio grane as one clock-icon tile with its games in a column.
    bool xmb_favorites_icon = false; // XMB: Ulubione as a star-icon tile instead of a 2x2 cover preview.
    bool xmb_title_glow = false;     // XMB: subtle PS3-style pulsing glow behind the focused game's title.
    bool xmb_icons_only = false;     // XMB: unfocused top-row tiles translucent, so the focused one stands out (PS3).
    enum class XmbSize { compact, normal, large };
    XmbSize xmb_size = XmbSize::normal;
    XmbSize psfive_size = XmbSize::normal; // PSfive tiles: the same three sizes (80 / 100 / 120%).
    bool psfive_clock = true;              // PSfive: the time in the top bar's right corner.
    float psfive_dim = 0;                  // PSfive: extra darkening of the background picture, 0..1 (0 = as it was).
    enum class FlixSize { normal, large }; // Flix posters and capsules.
    FlixSize flix_size = FlixSize::normal;
    // Flix: a "Wszystkie" tile first in every game row (not Recent, PC platforms or Emulation),
    // with a random background of that category; it opens the library of the category.
    bool flix_category_all = false;
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
    bool xmb_platform_badge = true;  // XMB: the platform as PSfive's white logo badge instead of its name.
    bool metro_title_corner = false; // Metro: the focused game's name and platform logo bottom left, not on its tile.
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
    // Tryb TV's CRT filter (Dostosuj wygląd → Inne; src/ui/crt_view.hpp): on, its mask (0 lines only, 1 TV,
    // 2 Trinitron, 3 arcade, 4 VGA) and its sliders, 0..1 each.
    bool crt = false;
    int crt_mask = 3;
    float crt_warp_x = 0.25f, crt_warp_y = 0.33f, crt_mask_level = 0.7f, crt_scanlines = 0.6f, crt_glow = 0.3f, crt_phosphor = 0.0f;
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
    // Steam's small "Uruchamianie…" window (about three seconds, a moment before the game's own) is hidden as it opens
    // (src/platform/steam_launch_window.hpp).
    bool steam_hide_launch_window = true;
    // Xbox account games: Instaluj downloads a GDK title with Launchme's Xodus-based helper into a
    // chosen folder (experimental) instead of handing it to the Xbox app (store/xbox_account.hpp).
    bool xbox_own_install = false;
    // While a game started here runs, the window is hidden (no taskbar button, no Alt+Tab) and an
    // icon in the notification area stands in for it; it comes back when the game closes.
    bool hide_while_playing = false;
    // The window's ✕, Alt+F4 and the taskbar hide the launcher in the notification area, whose icon
    // stays while it runs (src/app/tray.cpp); close_to_tray_told: the balloon saying so was shown.
    bool close_to_tray = false;
    bool close_to_tray_told = false;
    // Opcje uruchamiania → Mody (src/library/game_mods.hpp): each mod's preferred method by its slug
    // ("reshade" -> "dxgi.dll", "anr" -> "full"); missing: the mod's first. mod_badges: the mods a game
    // has as badges on its page (Tryb TV and the desktop window).
    std::map<std::string, std::string> mod_methods;
    bool mod_badges = true;
    // A Steam game installed by another account remembered on this PC: Steam restarts signed in to
    // that account, then the game starts (src/library/steam_accounts.hpp).
    bool steam_account_switch = true;
    // Installing a Steam game: Launchme's dialog picks the library (with its free space) instead of
    // Steam's install window.
    bool steam_install_dialog = true;
    // A GOG game started here gets comet (an open GOG Galaxy service) with the linked account:
    // achievements, stats and leaderboards without Galaxy (src/store/gog_comet.hpp).
    bool gog_comet = true;
    // Linux: GOG Galaxy's stand-in service added to a GOG game's Wine/Proton prefix before it starts
    // (src/store/gog_galaxy_service.hpp). On Windows the service is installed once, by hand.
    bool gog_galaxy_service = true;
    // GOG Galaxy's own in-game overlay (friends, chat, invitations) in GOG games, through comet
    // (src/store/gog_comet.hpp); fetched once from GOG when switched on.
    bool gog_overlay = false;
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
    // Standalone sessions Launchme did not see end (closed or crashed while the game ran): their saves
    // go up at its next start, once Aurelia no longer runs the game.
    std::unordered_set<std::string> aurelia_cloud_pending;
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
    // Biblioteka → Ekran główny i kolekcje: the home rows' order after Ostatnio grane (row ids; a collection's
    // id marks a place for a collection, collection_order says which one stands there). Empty: the built-in order.
    std::vector<std::string> home_row_order;
    // Wydawcy and Developerzy (src/app/studio_libraries.cpp): the libraries of the publishers and developers
    // with a logo, each row on its own; a studio needs studio_min_games games (1, 2, 3 or 5) to show; hidden
    // ones ("publisher:capcom") and the player's order (the rest: most games first).
    bool show_publishers = true;
    bool show_developers = true;
    // Firmy: one list of both, a studio once however it took part (it replaces the two above, which are then off).
    bool show_companies = false;
    int studio_min_games = 3;
    std::unordered_set<std::string> hidden_studios;
    // Biblioteka → Ekran główny i kolekcje: home rows (category ids: "pc-platforms", "emulation", a collection's id...) kept off
    // the home screen (still tabs of Wszystkie gry), and the tabs left out of Wszystkie gry ("recent"...).
    std::unordered_set<std::string> hidden_home_sections, hidden_library_tabs;
    bool home_rows_migrated = false; // hidden_collections (once: home and tabs) carried into hidden_library_tabs.
    std::vector<std::string> studio_order;
    // The Steam rows of an account with no collections: Ostatnio zaktualizowane, Jeszcze niegrane.
    bool show_updated_row = true;
    bool show_discovery_row = true;
    // Native PSfive and Metro tabs: icons for Platformy PC, Emulacja, Wydawcy, Developerzy and Ulubione.
    bool home_tab_icons = false;
    // Flix → Pasek boczny: the icons at the left edge (src/ui/flix_rail.hpp).
    bool flix_rail = false;
    bool quick_resume = false; // Experimental: suspend/resume running games from the game menu.
    // Guide/PS opens the launcher menu (hold: power). Off when Game Bar or Steam
    // already own that button on this PC.
    bool guide_menu = true;
    bool guide_overlay = false; // Experimental: Guide over a foreground game.
    bool guide_open_widgets = true; // The overlay's shortcut over a game (Guide, the pad pair or the keyboard chord) opens on the widgets.
    // The in-game overlay's shortcuts (Windows): the keyboard chord and the pad button or pair.
    enum class OverlayKey { shift_tab, ctrl_tab, shift_f1, ctrl_shift_o, none };
    enum class OverlayPad { guide, view_menu, sticks };
    OverlayKey overlay_key = OverlayKey::shift_tab;
    OverlayPad overlay_pad = OverlayPad::guide;
    bool game_fps = false, game_fps_low = false; // Read-only widget in the right drawer.
    std::vector<std::string> hud_pins;
    // Ustawienia -> FPS i limiter. The limiter loads a library into the game, so it is opt-in.
    bool fps_graph = false;           // Frame-time graph in the FPS widget and on the game's screen.
    bool fps_frametime = false;       // Mean frame time in ms (FT) in the FPS widget and on the game's screen.
    bool fps_frametime_spike = false; // Also show the slowest frame (max ms) from the last second.
    bool fps_limiter = false;
    int fps_limit = 0;                // 0: none; else frames per second.
    bool fps_limit_precise = true;    // Smoother pacing for a little more CPU.
    int fps_limit_mode = 0;           // 0: smooth (frames wait for their slot); 1: adaptive (low latency, as even).
    bool fps_badges = true;           // LIMIT / SMOOTH chips on the pinned widgets while the limiter works.
    bool hud_enabled = true;          // Master switch of the widget on the game's screen: off, nothing is shown there.
    int hud_layout = 0;               // The widget on the game's screen: 0 a list, 1 a strip (one line).
    bool hud_label_colors = false;    // Coloured FPS / CPU / GPU / RAM / VRAM labels instead of grey.
    int hud_size = 100;               // Percent of the largest size: 50..100.
    int cpu_temp_source = 0;          // 0: MSI Afterburner when it runs, else Windows; 1: Windows only.
    std::map<std::string, int> fps_game_limits; // gameKey -> limit; 0: no limit for that game. Absent: the global one. // Widgets kept on the game's screen: "fps", "cpu", "gpu" (Guide over games).
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
    // "Jak zacząć" (the desktop sidebar's card above the companion, src/app/getting_started.cpp): the steps the
    // player has taken (bit per step) and whether the card is hidden. Only a first start shows it; a settings
    // file from before the card hides it, Ustawienia → System brings it back.
    uint32_t start_done = 0;
    bool start_hidden = true;
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
    // System → Wydajność → Oszczędzaj zasoby za grą: with a game in front of the launcher (full screen
    // on its monitor) it yields: below-normal priority, EcoQoS, 10 Hz, no video (src/app/memory.cpp).
    // Opt-in: without it the launcher only slows down when minimized or in the tray.
    bool background_yield = false;
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
    // Interfejs → Tło i szkło: spatial upscaler for non-4K backgrounds and wallpapers in Tryb TV
    // (AMD FSR 1.0 EASU + RCAS on Windows, 5-tap Catmull-Rom + CAS on Linux).
    bool wallpaper_upscale = true;
    // Media i dane → Okładki → Tła gier w 4K: keeps up to 3840×2160 in the disk cache and prefers 4K
    // Steam / SteamGridDB heroes over local 1920×620 copies.
    bool backgrounds_4k = false;
    // Interfejs → Start (src/app/boot_video.cpp): a startup video from SteamDeckRepo or disk, kept in
    // <config>\boot_videos; empty: none. boot_video_random picks any stored video on startup.
    std::filesystem::path boot_video_file;
    uint32_t boot_video_id = 0;
    std::wstring boot_video_title;
    bool boot_video_random = false;
    bool boot_video_fill = false; // false: fit whole frame (16:10 intact); true: cover-fill 16:9 screen.
    // Interfejs → Start: the CRT bow + vertical light trail when closing (src/ui/exit_renderer.cpp).
    bool exit_animation = true;
    uint32_t exit_color_rgb = 0x57D1F0; // 0 = interface accent (ui_accent_rgb / desktop_accent_rgb).
    // Tryb TV: Wszystkie, Glance, Tarot and Towarzyszka (with Dodaj grę) as tiles of the first row in
    // every layout, leading or closing it; layout: each layout's own way (floating widgets).
    enum class HomeTiles { layout, first, last };
    HomeTiles home_tiles = HomeTiles::layout;
    bool add_game_tile = false; // A "+" tile opening Dodaj grę.
    // PSfive: Down from Graj / … scrolls to the game's details, screenshots and similar games.
    bool psfive_details = true;
    // PSfive: a sentence or two about the focused game between its logo and Graj / … (as on the PS5).
    bool psfive_summary = true;
    // Szczegóły (Tryb TV and the desktop window): the game's screenshots and similar games below its cards.
    bool details_media = true;
    // The game menu as a large scrollable dialog with an icon beside each option.
    bool menu_dialog = true;
    // Ustawienia > Biblioteka > Gry emulowane: the ROM folder read by launchme's own parsers
    // (src/library/emulation_scan.hpp; Steam ROM Manager's shortcuts of emulated platforms and its
    // collections are then left out; with no ROM folder found the shortcuts stay), or those shortcuts.
    enum class EmulationSource { roms, steam };
    EmulationSource emulation_source = EmulationSource::roms;
    std::filesystem::path emulation_roms; // Empty: the one ES-DE or EmuDeck names.
    std::vector<std::filesystem::path> emulation_roms_extra; // More ROM folders (other drives), scanned the same way.
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
    std::map<std::string, std::filesystem::path> system_roms; // System ID -> ROM folder override.
    // Metro: a white page with dark text (the light theme of Windows Phone).
    bool metro_light = false;
    // Snitch: the dock's News/eShop/Album/Controllers discs in colour, as on the real Switch.
    bool snitch_colour_icons = false;
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
// The key files are encrypted for this user (protected_file.hpp); a plain one-line file (written by hand,
// or by 0.7.12 and older) is read and then encrypted in place.
std::wstring userSecret(const wchar_t* environment, const wchar_t* filename);
std::wstring userSecretFile(const std::filesystem::path& file);
// Settings → Library: writes the key file, encrypted; empty input removes it.
bool saveUserSecret(const wchar_t* filename, std::wstring_view value);
bool storedUserSecret(const wchar_t* filename);
