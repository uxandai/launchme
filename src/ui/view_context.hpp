#pragma once

#include "../model/notification.hpp"
#include <string>
#include <unordered_set>
#include "../friends/steam_friends.hpp"
#include "settings_view.hpp"
#include "context_menu.hpp"
#include "flix_rail.hpp"
#include "../model/game_details.hpp"
#include "../plugins/time_comparison.hpp"
#include "artwork_browser.hpp"
#include "device_panel.hpp"
#include "crop_editor.hpp"
#include "library_controls.hpp"
#include "achievements_view.hpp"
#include "gog_install_view.hpp"
#include "custom_game_view.hpp"
#include "file_browser_view.hpp"
#include "library_manager_view.hpp"
#include "../model/game_session.hpp"
#include "../platform/sensors.hpp"
#include "screensaver.hpp"
#include "identify_view.hpp"
#include "bulk_covers_view.hpp"
#include "plugin_page.hpp"
#include "../model/game_fps.hpp"
#include "friends_layout.hpp"
#include "image_decoder.hpp"
#include "metro_layout.hpp"
#include "onscreen_keyboard.hpp"
#include "downloads_view.hpp"
#include "glance_view.hpp"
#include "tarot_view.hpp"
#include "profile_view.hpp"
#include "update_view.hpp"
#include "aurelia_view.hpp"
#include "friend_chat_view.hpp"
#include "companion_view.hpp"
#include "home_widgets.hpp"
#include "desktop_sheet.hpp"
#include "empty_library.hpp"

#include <array>
#include <map>
#include <optional>
#include <string>
#include <vector>

enum class ScreenMode { shelves, library, search, settings, platform_games, store, details, artwork, devices, crop, plugin_page, downloads, glance, companion };
inline bool gridScreen(ScreenMode screen) {
    return screen == ScreenMode::library || screen == ScreenMode::search || screen == ScreenMode::store;
}

// Right drawer home: clock header and tiles (friends_layout::dashboard). The Steam
// friends list replaces the tiles while `list` is set.
struct DrawerDashboard {
    bool list = false;
    int focus = 0; // Focused friends_layout::dashboard::Tile.
    std::wstring time, date;
    sensors::Reading sensors;
    bool discord_running = false;
    friends_layout::dashboard::Layout layout;
    float scroll = 0, edit_seconds = 0;
    bool editing = false, grabbed = false;
    // The profile tile: the player's name and initials, this week's play time and what it held.
    std::wstring profile_name, profile_initials, profile_value, profile_caption;
    std::array<float, 7> profile_week{}; // Each day's share of the week's longest one (a small chart).
};

// First-run walkthrough (src/app/tour.cpp, src/ui/tour_renderer.cpp, geometry in tour_view.hpp).
struct TourView {
    // The panel the step opened: the spotlight follows it as it slides in.
    enum class Target { none, category_menu, category_rail, xmb_row, drawer, quick_bar, menu };
    int step = 0, steps = 1;
    float alpha = 1;    // The whole overlay, fading in and out.
    float fade = 1;     // Step content and spotlight after a step change, 0..1.
    float progress = 1; // Fill of the current stepper segment (the quick bar: icons shown so far).
    std::wstring title, tag, body;          // Card: tag is a small label after the title.
    std::wstring bubble_title, bubble_body; // Next to the spotlight.
    Target target = Target::none;
    bool card_bottom = false; // tour::Layout::bottom.
    int button = -1, hover_button = -1; // tour::Button with keyboard/pad focus (-1: the tiles have it) and under the mouse.
    bool back = false, last = false;    // Back is shown; the last step says "Gotowe".
    // Last step: the layout tiles.
    bool layouts = false;
    int layout_choice = 0, focus_tile = -1, hover_tile = -1, preview_option = -1;
    float preview_seconds = 0;
    std::array<std::wstring, settings_ui::home_layout_count> layout_descriptions; // Indexed like AppSettings::HomeLayout.
};

// Bottom-right button hints (src/app/input_hints.cpp, src/ui/hints_renderer.cpp). The device decides
// the glyphs: keycaps, or the pad family's buttons by position (A = the bottom face button).
enum class HintButton { accept, back, menu, search, drawer, home, shoulders, type };
enum class HintDevice { keyboard, xbox, playstation, nintendo };
struct InputHint { HintButton button; std::wstring label; std::wstring key; }; // key: a keycap caption other than the default.

// Towarzyszka (src/app/companion.cpp, src/ui/companion_renderer.cpp): her pose and the state
// of the home tile, the bubble and the chat screen as one frame shows them.
struct CompanionView {
    const companion_ui::Feed* feed = nullptr;
    companion::Look look{};
    std::wstring name, persona, key, status;
    avatar::Pose pose;
    bool tile = false;     // Her free-standing home tile is shown (fading in with tile_alpha): default, PSfive.
    bool pill = false;     // Her Flix pill is shown instead (tile_alpha too).
    settings_ui::Rect spot{}; // That tile or pill (home_widgets::spots); Metro and XMB draw her in their own tile.
    float tile_alpha = 1, tile_hover = 0;
    float entrance = 1;    // Her jump out of the tile on start (companion_ui::entranceDrop).
    std::wstring bubble;   // The revealed part of her home line.
    std::vector<companion::GameMention> bubble_mentions;
    float bubble_alpha = 0;
    bool bubble_waiting = false;
    const companion_ui::Chat* chat = nullptr; // ScreenMode::companion.
    const companion_ui::Picker* picker = nullptr;
    std::optional<std::pair<float, float>> link_pointer;
    // An ended conversation: what the field says instead ("… odpoczywa · wróci za 25 min") and
    // whether "Nowa rozmowa" can start one yet. A history one: what the field says.
    std::wstring chat_note;
    bool can_restart = false;
    std::shared_ptr<const std::vector<uint8_t>> photo; // The picture she sent in this chat (Message::photo).
    // Another companion in the chat (CompanionGuest): her look, name and pose; duo 0 away … 1
    // beside the host; leaving: she is running off (else coming in or staying).
    std::optional<companion::Look> guest;
    std::wstring guest_name, guest_persona;
    avatar::Pose guest_pose;
    float duo = 0;
    bool guest_leaving = false;
    std::vector<std::wstring> diagnostics; // "…" → Pokaż diagnostykę: its heading, then one line each.
    // Gamification on: the host's level (1–50) and her sympathy bar towards the next one (0–1).
    bool affinity = false;
    int affinity_level = 1;
    float affinity_progress = 0;
    float affinity_pulse = 0; // 1 → 0 after a level up.
    int streak = 0;           // Days in a row (shown from 2).
    int affinity_card = 0;    // Her card for the player still live (affinity::Card: 0 none, 1 yellow, 2 red).
    float affinity_card_pulse = 0; // 1 → 0 right after it was given.
    // Costume slots (companion_costumes.hpp; 0: her basic outfit): hers and the guest's.
    int costume = 0, guest_costume = 0;
    // Settings → Asystent: every companion's card (companion::characters() order), animated, in her
    // costume; levels 0 unless gamification is on.
    std::vector<avatar::Pose> previews;
    std::vector<int> preview_costumes, preview_levels;
    // "…" → "Umysł i pamięć": nodes on the 3D holographic sphere and telemetry on the right panel.
    struct MindNode {
        enum class Kind { core, context, note, summary, pick, taste, thought, reflection, reading, feeling } kind = Kind::note;
        std::wstring label;    // Beside it on the sphere.
        std::wstring category; // What kind of memory it is (the inspector's badge).
        std::wstring detail;
        std::wstring when;     // How long ago, or where it comes from (the list's right column).
        float x = 0, y = 0, z = 0; // Normalized 3D coordinates (-1..1).
        float weight = 1.0f;
        int parent = 0;
        bool deletable = false;
        bool locked = false;   // Her private matter: closed until she tells the player or he opens it (Odkryj).
        bool masked = false;   // Her hidden thought: drawn masked until the player shows it (Pokaż); `detail` keeps its text.
        bool revealable = false; // A hidden thought: Pokaż/Ukryj (masked or shown in this visit).
        bool excited = false;  // Lit up by Spreading Activation from the player's last message.
        std::optional<companion::Emotion> emotion; // How she felt when this memory was formed.
        std::wstring feeling_label;                // Badge text for her emotion ("Zawstydzona", "Rozbawiona", …).
        std::wstring memory_tier;                  // Badge text for memory tier ("Pamięć krótkotrwała" / "Pamięć długotrwała").
        uint64_t entry_at = 0;
        std::string entry_key;
        std::optional<companion::Feeling> feeling;
        bool matches_filter = true;
    };
    struct MindTelemetry {
        std::wstring bio;
        std::wstring live_thought;      // What is actively on her mind right now (updates live without reopening).
        std::wstring live_thought_kind; // Badge for the live thought ("Teraz myśli", "Ukryta myśl", "Wniosek", …).
        bool live_busy = false;         // True while her model/worker is actively thinking in the background.
        bool live_hidden = false;       // `live_thought` is her hidden thought (<mysl>): Pokaż/Ukryj by a click on the bar.
        bool live_masked = false;       // …and it is masked now.
        std::wstring mood;              // Her mood now, in words (companion_mind.hpp describe).
        std::vector<std::wstring> likes, dislikes;
        int64_t points = 0, next_points = 100;
        int costumes_unlocked = 1, costumes_total = 1;
        int notes_count = 0, summaries_count = 0, picks_count = 0;
        uint32_t avg_latency_ms = 0;
        std::wstring model_name;
    };
    std::vector<MindNode> mind_nodes;
    MindTelemetry mind_telemetry;
};

namespace desktop_ui { struct MetaView; }

struct ViewContext {
    // Desktop mode's library (src/ui/desktop_renderer.cpp) instead of a Tryb TV home; toasts then
    // rise in the bottom-right corner, clear of its top bar.
    const desktop_ui::View* desktop = nullptr;
    // A TV screen shown as a desktop dialog (Ustawienia): its sheet's frame state (desktop_sheet.hpp).
    const desktop_ui::SheetView* desktop_sheet = nullptr;
    // Urządzenia, kadrowanie or a plugin page inside a desktop sheet: its frame state and title.
    const desktop_ui::SheetView* desktop_embed = nullptr;
    std::wstring desktop_embed_title;
    bool desktop_window = false; // Desktop mode: every screen and dialog in its desktop style.
    desktop_ui::SheetHit desktop_dialog_hover; // The control under the pointer in a desktop dialog.
    bool toast_bottom = false;
    // A TV screen inside the desktop window: its window buttons (desktop_ui::stripButton).
    bool window_strip = false, window_max_hover = false, window_maximized = false;
    int window_strip_hover = -1;
    float window_dpi = 1;
    std::vector<InputHint> hints;
    const CompanionView* companion = nullptr;
    HintDevice hint_device = HintDevice::keyboard;
    // Where Glance and Towarzyszka stand on this layout's home (src/ui/home_widgets.hpp).
    home_widgets::Placement home_widgets = home_widgets::Placement::column;
    bool glance_shortcut = false;  // Glance's free-standing tile or Flix pill is shown.
    settings_ui::Rect glance_spot{}; // That tile or pill, at rest.
    float glance_hover = 0;
    float glance_alpha = 1;
    std::wstring glance_key = L"G";
    const glance_ui::State* glance = nullptr;
    const TourView* tour = nullptr;
    bool guide_overlay = false;
    int guide_close_confirm = -1; // -1 closed, 0 close, 1 cancel.
    float guide_opacity = 1;
    bool fps_widget = false, fps_low = false, fps_graph = false;
    int hud_layout = 0;               // 0: a list, 1: a strip.
    bool hud_label_colors = false;
    float hud_scale = 1.0f;           // AppSettings::hud_size / 100.
    std::vector<std::wstring> hud_badges; // "LIMIT 60", "SMOOTH": chips on the widget over the game.
    std::vector<std::string> hud_pins; // Dashboard tiles kept on the game's screen ("fps", "cpu", "gpu").
    game_fps::Reading game_fps;
    const plugin_ui::Page* plugin_page = nullptr;
    const plugin_ui::Page* plugin_drawer = nullptr;
    std::wstring performance;
    ScreenMode screen = ScreenMode::shelves;
    float screen_transition = 1; // 1 = settled; 0..1 = screen fade-in, zoom and edge bow.
    float wallpaper_zoom = 1; // Shared crop: a slow, centred 1% approach in Details, eased back elsewhere.
    std::wstring search_query;
    std::wstring library_title;
    const std::vector<settings_ui::Row>* settings_rows = nullptr;
    std::vector<float> settings_toggles; // Per row, 0..1 animated switch position.
    settings_ui::State settings_state;
    std::wstring settings_title;
    std::wstring settings_parent; // A page opened from a page: that page's title (its way back); empty: the category.
    int settings_hover_row = -1, settings_preview_option = -1;
    int settings_hover_badge = -1; // The row whose badge button (Row::reset) the pointer is over.
    int settings_hover_trash = -1; // The collection row whose trash (Row::removable) the pointer is over.
    float settings_preview_seconds = 0;
    bool settings_save_failed = false;
    size_t secret_length = 0;
    std::wstring settings_text; // Only Control::text; secrets never reach the renderer.
    bool secret_selected = false;
    bool secret_cancel = false; // The key/text dialog: Cancel has the focus instead of Save.
    bool secret_browse = false; // The import-path dialog: Browse has the focus.
    std::vector<std::wstring> plugin_menu;
    std::vector<std::string> plugin_menu_glyphs;
    struct PluginWidget {
        std::wstring title, text, icon_text;
        std::string plugin;
        ImageBytes icon;
        std::wstring caption;
    };
    std::vector<PluginWidget> plugin_widgets;
    std::wstring store_status;
    const GameDetails* game_details = nullptr;
    std::optional<plugins::TimeComparison> detail_comparison;
    std::optional<uint64_t> detail_playtime_seconds;
    std::wstring detail_comparison_title;
    int detail_comparison_action = -2;
    const artwork_ui::State* artwork_browser = nullptr;
    const device_ui::State* device_panel = nullptr;
    const crop_ui::State* crop_editor = nullptr;
    const library_ui::State* library_controls = nullptr;
    float library_menu_progress = 0, library_views_progress = 0; // Filter menu, view drawer.
    float library_dropdown_progress = 0;
    library_ui::Panel library_panel = library_ui::Panel::none;    // Drawn while fading out too.
    bool library_list = false;                                    // Library screen in the list view.
    library_view::View library_mode = library_view::View::grid;
    // The TV home's focused game has its own background on the way (or the library is still loading):
    // the wallpaper behind tiles waits instead of showing for the moment it takes (background_renderer.cpp).
    bool home_background_pending = false;
    library_view::Options library_options;
    float library_search_progress = 0;                            // The library's search field unfolding.
    std::optional<float> library_choice_glide;
    std::optional<float> library_flow; // Półka: Cover Flow's gliding centre (fractional card index); none = the focused card.
    const GameDetails* preview = nullptr;                          // List view: focused game's description.
    bool preview_waiting = false;                                  // ...still on its way (a Steam game's store details).
    struct LibraryRow { std::wstring year, playtime, rating; };
    std::map<size_t, LibraryRow> library_rows; // Visible rows only, assembled from memory before rendering.
    // Library category tabs (title, games), the shown one and its gliding position.
    struct LibraryTab { std::wstring title; size_t count = 0; std::wstring mark; }; // mark: a studio's logo in place of the title.
    std::vector<LibraryTab> library_tabs;
    std::wstring library_note; // Under the tabs: what the screen is (the board of studio logos).
    int library_tab = 0;
    std::optional<float> library_tab_glide;
    // Metro and Flix home (their "Pasek kategorii" option): the same strip at the top in the middle.
    // home_tab: the shown category's (fractional while gliding) index; home_tabs_focus: Metro's Up from the tiles.
    std::vector<LibraryTab> home_tabs;
    float home_tab = 0, home_tabs_alpha = 1;
    bool home_tabs_focus = false;
    bool strip_hints = false; // The small LB/RB (PgUp/PgDn) caps beside a category strip (Podpowiedzi przycisków on).
    bool hint_white = false;  // Interfejs → Wygląd podpowiedzi: white key caps with dark ink instead of glass.
    bool grid_back = false; // Library, Search, Store: Back (top left) has the keyboard/pad focus.
    const downloads_ui::State* downloads = nullptr; // ScreenMode::downloads; rows match the cards.
    int downloads_moving = 0; // Games transferring now: a count on the quick bar's Pobierania icon.
    bool logo_pending = false; // Includes the download/cache lookup, before the renderer starts decoding.
    // Quick vertical scrolling: the focused game's initial over a dimmed screen.
    std::wstring scroll_letter;
    float scroll_letter_alpha = 0;
    const gog_install_ui::State* gog_install = nullptr;
    const tarot_ui::State* tarot = nullptr; // Tarot's reading, while open or fading out.
    const profile_ui::State* profile = nullptr; // Profil, while open or fading out.
    aurelia_ui::State* aurelia = nullptr; // The renderer records its measured note and visible rows.
    update_ui::WhatsNew* whats_new = nullptr; // Co nowego after an update; the renderer records the text's height.
    const desktop_ui::MetaView* tv_meta = nullptr; // Tryb TV: Edytuj metadane (the desktop dialog, on the TV canvas).
    const custom_game_ui::State* custom_editor = nullptr; // Settings → Library add/edit panel.
    ImageBytes custom_poster; // Its poster beside the fields (custom_game_ui::State::poster says why there is none).
    const file_browser::State* file_browser = nullptr;    // The launcher's file picker, over everything.
    const settings_ui::ColorEditor* color_editor = nullptr; // Settings: "Inny…" colour.
    const library_manager_ui::State* library_manager = nullptr;
    bool detail_favorite = false;
    float detail_favorite_pulse = 0; // 1 → 0 after toggling favourite.
    int detail_favorite_action = -2, detail_collections_action = -2;
    int detail_versions_action = -2;
    bool details_loading = false;
    const identify_ui::State* identify = nullptr;
    const bulk_covers_ui::State* bulk_covers = nullptr;
    std::wstring rating_label;
    int detail_rating = -2, detail_identify = -2;
    int detail_action = 0;
    int detail_achievements = -2;                            // Details action of the achievements row; -2 = none.
    const achievements_ui::State* achievements = nullptr;    // Szczegóły → Osiągnięcia, over the details.
    std::wstring primary_action;
    bool primary_enabled = true;
    float grid_scroll = 0;
    // Games with every achievement unlocked (their keys): a gold edge on their tiles and posters.
    const std::unordered_set<std::string>* perfect_games = nullptr;
    std::wstring category;
    std::wstring platform;
    bool main_row = true;
    float menu_progress = 0;
    float launch_progress = 0;
    float launch_animation_seconds = -1; // -1 inactive; a brief zoom of the launched game's cover.
    float launch_animation_ending = -1;  // Seconds into its fade-out; -1 while it stays.
    std::wstring launch_animation_title;
    size_t launch_artwork_index = static_cast<size_t>(-1);
    int menu_selected = 0;
    std::vector<std::wstring> menu_plugins;
    bool menu_suspend = false; // Context menu shows "Wstrzymaj" (experimental quick resume).
    bool menu_session = false; // Running or suspended: "Zamknij grę" replaces "Ponów grafiki".
    bool menu_detail_options = false, menu_folder = false, menu_exclude = false;
    bool menu_hide = false, menu_hidden = false; // Details options: "Ukryj" / "Pokaż".
    bool menu_remove = false, menu_edit_game = false; // "Usuń z biblioteki" (red, last) / "Opcje uruchamiania" read "Edytuj grę".
    bool menu_uninstall = false, menu_launch = false; // "Odinstaluj" (red, last) / Details' "Opcje uruchamiania".
    bool menu_compat = false; // Details' "Proton / Wine" (Linux).
    bool menu_shortcuts = false; // Details' "Skróty" (the game can have shortcuts).
    bool menu_metadata = false; // Details' "Edytuj metadane".
    bool menu_aurelia = false;
    std::wstring menu_steam_account; // Details' "…": the account a Steam game starts from; empty: no choice.
    // Flix → Pasek boczny (src/ui/flix_rail.hpp): its icons, the focused and hovered one (-1: none), the
    // row in view's own (-1: another row), and how much of it shows.
    std::vector<flix_rail::Item> flix_rail;
    int flix_rail_focus = -1, flix_rail_hover = -1, flix_rail_active = -1;
    float flix_rail_alpha = 1;
    std::wstring menu_emulator;      // Details' "…": "Emulator: Ryujinx" (Emulator per gra); empty: none.
    context_menu::Rows menu_rows{};  // The menu's rows as Launcher::menuRows builds them (every flag, the studio's too).
    std::wstring aurelia_cloud;
    // Szczegóły: the cloud icon after "…" (Aurelia-managed games), its state and its action's index.
    std::optional<aurelia::CloudState> detail_cloud;
    int detail_cloud_action = -2;
    int detail_close_action = -2; // "Zamknij grę" beside Wróć while the game runs; -2 = none.
    bool menu_beside = false;  // The game menu opens beside the card (XMB, Flix, list view).
    std::optional<settings_ui::Rect> menu_anchor;
    // With Quick Resume off, failed recoveries still show their frozen state.
    const std::map<std::string, GameSession>* sessions = nullptr;
    bool sessions_frozen_only = false;
    // On-screen keyboard (pad X): its state while shown or sliding out, and its field's text.
    const osk::State* keyboard = nullptr;
    float keyboard_progress = 0;
    std::wstring keyboard_text;
    size_t keyboard_caret = 0; // Insertion point within keyboard_text.
    size_t search_caret = 0;   // Insertion point within search_query.
    size_t desktop_text_begin = 0, desktop_text_end = 0; // The desktop window's focused field: its selected range.
    int exit_prompt = -1; // Selected exit_prompt::Choice; -1 = closed.
    // Another question in the same prompt (moving videos); empty: the exit question.
    std::wstring prompt_question;
    std::array<std::wstring, 3> prompt_labels;
    bool prompt_danger = false; // Its first answer destroys something (Usuń kolekcję): drawn red.
    std::wstring prompt_body;   // What the answer changes, under the question (the desktop window's dialog).
    float exit_progress = -1; // -1 inactive, 0..1 one-shot fade to black.
    size_t frozen_games = 0;
    int power_menu = -1; // Selected row of power_labels; -1 = closed.
    std::vector<std::wstring> power_labels;
    std::vector<bool> power_gaps, power_danger; // power_menu::gaps / danger of each row.
    std::vector<wchar_t> power_icons;           // power_menu::icon of each row.
    // The layer's header when it is not Zasilanie (the widget's options): title, subtitle, icon; empty / 0: Zasilanie's.
    std::wstring power_title, power_subtitle;
    // The widget options' rows: a switch on the right (-1: none, 0 off, 1 on) or a value with arrows; scrolled (rows hidden above / below).
    std::vector<int> power_switches;
    std::vector<std::wstring> power_values;
    bool power_wide = false, power_more_above = false, power_more_below = false;
    wchar_t power_header_icon = 0;
    // Default home (AppSettings::shelves_hero_facts): the focused game's facts centred under its logo
    // (capitalised heading, value); the platform's mark is drawn for hero_platform. Empty: off.
    struct HeroFact { std::wstring heading, value; };
    std::vector<HeroFact> hero_facts;
    std::wstring hero_platform;
    // Kinowy (side and column scrolling): the focused game's extra facts beside its year and genres,
    // each empty when its view option is off or unknown.
    std::wstring cinematic_platform, cinematic_played, cinematic_playtime;
    // The TV home without games (empty_library.hpp): why, and the focused / hovered button (-1 none).
    empty_library::Reason empty_reason = empty_library::Reason::empty;
    int empty_focus = 0, empty_hover = -1;
    bool empty_actions = false; // The buttons take input (no menu, bar or drawer over them).
    // Flix (AppSettings::flix_top_icons): the quick bar's icons small in the top-right corner.
    bool flix_icons = false;
    int flix_icon_hover = -1; // home_widgets::icons index under the pointer; -1 none.
    int flix_icon_focus = -1; // The pad's icon (Launcher::flix_icon_focus_); -1 none.
    bool flix_icon_glance = false; // Glance's icon follows the quick ones.
    int flix_icon_total = 0;  // Quick icons, then Glance's and her avatar when shown.
    float flix_icons_alpha = 0;
    // XMB home: cards = [xmb_top tiles..., focused column games...].
    bool xmb = false, xmb_posters = false;
    bool shelf_posters = false; // Default layout: the shown shelf's cards are 2:3 posters (GameCard::aspect).
    float xmb_scale = 1;
    size_t xmb_top = 0;
    size_t xmb_column = 0;
    int xmb_row = -1;
    std::wstring xmb_caption, xmb_subcaption;
    // Flix home: rows of cards (the first is landscape), the focused row.
    struct FlixRow {
        std::wstring title;
        size_t begin = 0, count = 0;
        bool landscape = false;
    };
    bool flix = false;
    std::vector<FlixRow> flix_rows;
    size_t flix_row = 0;
    float flix_scale = 1; // flix_layout::sizeScale of the tile size setting.
    // The unfolded tile (flix_layout::unfold_rest): its card, how far it has opened, its primary
    // action's label and whether it plays, its facts (rating, year, playtime) and genres.
    size_t flix_unfold_index = static_cast<size_t>(-1);
    float flix_unfold = 0;
    std::wstring flix_unfold_action;
    bool flix_unfold_play = true;
    bool flix_unfold_details = false; // Enter/A opens the details: their ring is the filled (primary) one.
    std::wstring flix_unfold_rating;
    std::vector<std::wstring> flix_unfold_facts;
    std::wstring flix_unfold_genres;
    // Metro home: one entry per card (its role and caption), the tab of every page and the eased
    // page position the tab strip follows.
    struct MetroTile {
        metro_layout::Role role = metro_layout::Role::game;
        std::wstring caption, sub;
        size_t page = 0; // Its page: tiles of the other pages are dimmed, the current one stands out.
    };
    bool metro = false;
    std::vector<MetroTile> metro_tiles;
    std::vector<std::wstring> metro_tabs;
    std::vector<wchar_t> metro_tab_glyphs; // Ikony zamiast nazw: a tab's icon (0: its name), as psfive_tab_glyphs.
    float metro_tab = 0;
    size_t metro_page = 0;
    bool metro_tabs_focus = false; // The tab strip has the focus (Up from the top row of tiles).
    // PSfive home (src/ui/psfive_renderer.cpp): one row of tiles over the focused game's
    // background; psfive_action is the focused button under it (-1: the tiles have the focus).
    bool psfive = false;
    bool snitch = false;
    std::wstring snitch_clock;
    int snitch_battery = -1; // Unknown/no battery: no fabricated percentage.
    bool snitch_charging = false;
    int psfive_action = -1;
    float psfive_scale = 1;      // psfive_layout::sizeScale of the tile size setting.
    int psfive_top = -1;         // The focused top bar icon (psfive_layout::TopItem); -1 none.
    std::vector<std::wstring> psfive_tabs; // Three categories, then optional "Więcej...".
    std::vector<wchar_t> psfive_tab_glyphs; // Ikony zamiast nazw: Platformy PC, Emulacja, Wydawcy, Developerzy, Ulubione (0: the name).
    int psfive_tab = -1;                   // The shown category's tab (overflow uses "Więcej...").
    int psfive_tab_focus = -1;
    float psfive_top_opacity = 0.66f; // Whole navigation row: active 100%, inactive 66%.
    float psfive_intro = 1; // The start-up intro (psfive_layout::intro): the top bar, logo and buttons fading in.
    std::optional<float> psfive_menu_glide; // Fractional category index; scroll uses category_scroll.
    bool psfive_clock_shown = true;
    std::wstring psfive_clock;   // The time in the top bar (drawer_clock::time).
    std::wstring psfive_caption; // Beside a focused "Wszystkie gry" / "Wszystkie" tile: games (and category).
    // PSfive → Szczegóły pod rzędem (psfive_details_renderer.cpp): the sheet under the row.
    struct PsfiveDetails {
        float progress = 0, scroll = 0; // The sheet's rise and the sections' offset (psfive_details_layout.hpp).
        int section = 0;                // 0 information, 1 screenshots, 2 similar games.
        size_t shot = 0, similar = 0;   // The focused screenshot and similar game.
        size_t shots = 0;               // Screenshots known (their pictures in the renderer's slots).
        bool loading = false, viewer = false; // Still asking; a screenshot shown full screen.
        std::wstring description;
        std::vector<std::pair<std::wstring, std::wstring>> facts; // Label, value.
        uint32_t achievements_unlocked = 0, achievements_total = 0;
        std::vector<std::wstring> similar_names;
        // The focus frame gliding between screenshots and similar games (absolute rectangle).
        bool focus_glide = false;
        float shots_scroll = 0, similar_scroll = 0; // The strips' slide (psfive_details_layout.hpp).
        float focus_left = 0, focus_top = 0, focus_right = 0, focus_bottom = 0;
    };
    std::optional<PsfiveDetails> psfive_details;
    // Metro's light theme (Opcje układu → Jasny motyw).
    bool metro_light = false;
    // The game menu as the large dialog with icons (Okna i podpowiedzi → Duże menu gry).
    bool menu_dialog = false;
    const Notification* notification = nullptr; // Borrowed for the duration of paint().
    const VolumeNotice* volume_notice = nullptr;
    float volume_level = -1; // Unknown until the first system audio reading.
    bool volume_muted = false;
    const FriendsResult* friends = nullptr;
    float friends_progress = 0;
    int friends_scroll = 0;
    size_t friends_cursor = 0;
    bool friends_loading = false;
    friend_chat::State* friend_chat = nullptr; // The drawer's conversation; the renderer measures its log.
    DrawerDashboard dashboard;
    const std::vector<std::wstring>* categories = nullptr;
    bool category_rail = false, category_rail_focus = false;
    size_t active_category = 0;
    float category_menu_progress = 0;
    int category_cursor = 0;
    float category_highlight_y = 0;
    float category_scroll = 0;
    float quick_progress = 0;
    int quick_selected = 0;
    // Focus motion (src/app/focus_motion.cpp). Unset = draw at the target (no tick yet).
    std::optional<float> quick_circle_x; // Quick bar focus circle centre, gliding between icons.
    float quick_ring_time = 0; // Seconds driving the light travelling around the focus ring.
    // Rail pill (a category or Back), row highlight in list pixels and list scroll. rows = false:
    // the category changed since the last tick, the row and scroll are drawn at their targets.
    struct SettingsMotion { float pill_top = 0, pill_bottom = 0, rail_focus = 1, row = 0, scroll = 0; bool rows = true; float height = settings_ui::Layout::row_height; };
    std::optional<SettingsMotion> settings_motion;
    // Settings choice picker: its row (also while it fades out after closing), the source tile, the
    // highlighted choice, how far it is shown (0..1, it rises while fading in) and the highlight's
    // gliding fractional position.
    int settings_picker_row = -1, settings_picker_tile = 0, settings_picker_choice = 0;
    float settings_picker_fade = 1;
    std::optional<float> settings_choice_glide;
    std::optional<float> menu_glide, picker_glide; // Context menu / power menu or exit prompt: fractional row.
    struct GlideBox { float left = 0, top = 0, right = 0, bottom = 0; };
    std::optional<GlideBox> device_glide; // Devices screen focus frame: Back, a row, the slider or mute button.
    std::optional<GlideBox> drawer_glide; // Drawer focus: a tile, a friend or Back; x relative to the drawer.
    std::optional<GlideBox> library_row_glide, library_action_glide;
    std::optional<GlideBox> psfive_action_glide; // PSfive: the focus frame between "Graj" and "…".
    std::optional<GlideBox> details_glide; // Szczegóły: the white pill (a lighter card on the cards) gliding to the focus.
    std::optional<GlideBox> library_bar_glide; // Library top bar: the white pill between Back, the filters and the magnifier.
    std::optional<GlideBox> downloads_glide; // Pobierania: Back or a row.
    std::optional<GlideBox> manager_glide; // Kolekcje / Wersje dialog: the highlight gliding between Back, the field and the rows.
    std::optional<GlideBox> artwork_glide; // Zmień grafikę: the white pill between the rail's entries and buttons.
    std::optional<float> friends_scroll_glide;
    bool friends_back = false;           // Friends list: Back has the keyboard/pad focus.
    // Experimental spring outline (focus_spring): its own rectangle, before the launch zoom, drawn
    // instead of the focused card's edge. glow: inner glow on the side of a hit list edge
    // (glow_direction ±1 left/right, ±2 up/down).
    struct FocusRing { float x = 0, y = 0, width = 0, height = 0, glow = 0; int glow_direction = 0; };
    std::optional<FocusRing> focus_ring;
    // Set while the screensaver runs: it covers everything (src/ui/screensaver_renderer.cpp).
    const screensaver::View* screensaver = nullptr;
};
