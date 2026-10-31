#include "../src/platform/user_config.hpp"

#include <cmath>
#include <iostream>
#include <Windows.h>

int main() {
    // Themes are saved by id; the old numbered list maps "Brak" (10) and Horizons (11), and its
    // never-shipped themes fall back to the default (Horizons).
    if (AppSettings{}.music_theme != 1 ||
        parseAppSettings("music_track=none\n").music_theme != 0 ||
        parseAppSettings("music_track=exploratory-silence\n").music_theme != 2 ||
        parseAppSettings("music_track=gone\n").music_theme != 1 ||
        parseAppSettings("music_theme=9\n").music_theme != 1 ||
        parseAppSettings("music_theme=10\n").music_theme != 0 ||
        parseAppSettings("music_theme=11\n").music_theme != 1 ||
        parseAppSettings("music_theme=12\n").music_theme != 1 ||
        parseAppSettings("music_theme=-1\n").music_theme != 1 ||
        parseAppSettings("music_theme=4junk\n").music_theme != 1 ||
        parseAppSettings("music_track=none\nmusic_theme=11\n").music_theme != 0 ||
        parseAppSettings("music_theme=11\nmusic_track=none\n").music_theme != 0) return 7;
    {
        AppSettings saved; saved.music_theme = 2;
        const auto file = std::filesystem::temp_directory_path() / L"launchme-music-track-test.ini";
        if (!saveAppSettings(saved, file) || loadAppSettings(file).music_theme != 2) return 7;
        std::error_code error; std::filesystem::remove(file, error);
    }
    if (AppSettings{}.ui_sound || AppSettings{}.toast_sound || AppSettings{}.boot_sound ||
        parseAppSettings("ui_sound=1\n").ui_sound || parseAppSettings("toast_sound=-1\n").toast_sound ||
        parseAppSettings("boot_sound=2\n").boot_sound || parseAppSettings("boot_sound=1junk\n").boot_sound ||
        parseAppSettings(" boot_sound = 1 \r\n").boot_sound) return 29; // One built-in start sound ("Brak") is left.
    const auto style = parseDrawerStyle("# appearance\n drawer_opacity = 0.74\r\n"
                                        "drawer_fade_fraction=0.24\n");
    if (std::abs(style.opacity - 0.74f) > 0.001f ||
        std::abs(style.fade_fraction - 0.24f) > 0.001f) {
        std::cerr << "Drawer appearance was not loaded from settings\n";
        return 1;
    }
    const auto invalid = parseDrawerStyle("drawer_opacity=oops\ndrawer_fade_fraction=-10\n");
    if (std::abs(invalid.opacity - 0.9f) > 0.001f ||
        std::abs(invalid.fade_fraction - 0.06f) > 0.001f) {
        std::cerr << "Invalid or out-of-range drawer settings were not handled\n";
        return 1;
    }
    const auto config = parseAppSettings("show_uninstalled=0\nshow_emulation=0\nfont_family=Arial\n"
                                         "accent_color=#AABBCC\nui_accent_color=#4B8ED9\nhome_layout=xmb\nhidden_collections=user-collections.foo\n"
                                         "collection_order=user-collections.bar,user-collections.foo\n");
    if (config.show_uninstalled || config.show_emulation || config.font_family != L"Arial" ||
        config.cover_accent_rgb != 0xAABBCC || config.ui_accent_rgb != 0x4B8ED9 ||
        config.home_layout != AppSettings::HomeLayout::xmb || parseAppSettings("home_layout=psp\n").home_layout != AppSettings::HomeLayout::shelves ||
        !config.hidden_collections.contains("user-collections.foo") ||
        config.collection_order.size() != 2) {
        std::cerr << "Launcher appearance/library settings were not parsed\n";
        return 1;
    }
    const auto trimmed = parseAppSettings("\xef\xbb\xbf show_store = 1\r\n music_volume = 0.23 \n show_emulation=garbage\npc_platforms=gog,epic,unknown\n");
    if (!trimmed.show_store || std::abs(trimmed.music_volume - 0.23f) > 0.001f || !trimmed.show_emulation ||
        trimmed.enabled_pc_sources != std::unordered_set<std::string>{"gog", "epic"}) return 2;
    // Experimental quick resume is opt-in and survives a save/load round trip.
    if (AppSettings{}.quick_resume || !parseAppSettings("quick_resume=1\n").quick_resume || parseAppSettings("quick_resume=yes\n").quick_resume) return 9;
    const auto root = std::filesystem::temp_directory_path() / (L"launchme-settings-" + std::to_wstring(GetCurrentProcessId()));
    struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code ec; std::filesystem::remove_all(path, ec); } } cleanup{root};
    const auto file = root / "settings.ini";
    {
        auto feed = config;
        feed.companion_feed_folder = L"C:\\Zdjęcia\\companion_feed";
        if (!AppSettings{}.companion_feed_folder.empty() || !saveAppSettings(feed, file) || loadAppSettings(file).companion_feed_folder != feed.companion_feed_folder ||
            !parseAppSettings("companion_feed_folder=\n").companion_feed_folder.empty()) return 150;
        feed.setCompanionImportFolder(L"C:\\Zdjęcia\\companion_import");
        if (!saveAppSettings(feed, file)) return 151;
        const auto imported = loadAppSettings(file);
        if (imported.companion_import_folder != feed.companion_import_folder ||
            imported.companion_photo_folder != L"C:\\Zdjęcia\\companion_import\\photos" ||
            imported.companion_feed_folder != L"C:\\Zdjęcia\\companion_import\\feed") return 152;
        const auto ordered = parseAppSettings("companion_import_folder=C:\\import\ncompanion_photo_folder=C:\\old-photos\ncompanion_feed_folder=C:\\old-feed\n");
        if (ordered.companion_photo_folder != L"C:\\import\\photos" || ordered.companion_feed_folder != L"C:\\import\\feed") return 153;
        feed.setCompanionImportFolder({});
        if (!saveAppSettings(feed, file) || !loadAppSettings(file).companion_photo_folder.empty() || !loadAppSettings(file).companion_feed_folder.empty()) return 154;
    }
    if (!parseAppSettings("").glance || !parseAppSettings("glance=bad\n").glance || parseAppSettings("glance=0\n").glance) return 90;
    for (bool enabled : {false,true}) {
        auto chosen = config; chosen.glance = enabled;
        if (!saveAppSettings(chosen,file) || loadAppSettings(file).glance != enabled) return 91;
    }
    // Companions by key (any number of them): the chosen one (none by default: the app takes the
    // first by key) and each one's costume; the first version's white/red are Yuki/Akane.
    {
        const AppSettings fresh;
        if (!fresh.companion_look.empty() || !fresh.companion_guests || fresh.companion_model_info || !fresh.companion_memory || !fresh.companion_notes) return 92;
        const auto own = parseAppSettings("companion_look=hana\ncompanion_persona_mei=kuudere\ncompanion_guests=0\n"); // Characters are not settings.
        if (own.companion_look != "hana" || own.companion_guests) return 94;
        if (!parseAppSettings("companion_look=../x\n").companion_look.empty() || parseAppSettings("companion_look=white\n").companion_look != "yuki" ||
            parseAppSettings("companion_look=red\n").companion_look != "akane") return 93;
        auto chosen = config;
        chosen.companion_look = "rin";
        chosen.companion_guests = false; chosen.companion_debug = true;
        chosen.companion_gamification = true;
        chosen.companion_costumes = {{"hana", "costume_10_kimono"}};
        const auto loaded = saveAppSettings(chosen, file) ? loadAppSettings(file) : AppSettings{};
        if (loaded.companion_look != "rin" || loaded.companion_guests || !loaded.companion_debug || fresh.companion_debug) return 95;
        if (!loaded.companion_gamification || !fresh.companion_gamification || !parseAppSettings("companion_gamification=2\n").companion_gamification ||
            parseAppSettings("companion_gamification=0\n").companion_gamification) return 96;
        // Who she talks with: a name or nickname (one clean line, ≤ 40 characters) and a gender (auto by default).
        {
            auto named = config;
            named.companion_user_name = L"Marek \u2039x|y\u203A";
            named.companion_user_name = companionUserName(named.companion_user_name);
            named.companion_user_gender = AppSettings::CompanionUserGender::female;
            const auto back = saveAppSettings(named, file) ? loadAppSettings(file) : AppSettings{};
            if (named.companion_user_name != L"Marek xy" || back.companion_user_name != L"Marek xy" ||
                back.companion_user_gender != AppSettings::CompanionUserGender::female || fresh.companion_user_gender != AppSettings::CompanionUserGender::automatic ||
                !fresh.companion_user_name.empty() || companionUserName(L"  Ola\n  Nowak  ") != L"Ola Nowak" ||
                companionUserName(std::wstring(60, L'a')).size() != 40 ||
                parseAppSettings("companion_user_gender=male\n").companion_user_gender != AppSettings::CompanionUserGender::male ||
                parseAppSettings("companion_user_gender=bogus\n").companion_user_gender != AppSettings::CompanionUserGender::automatic) return 101;
        }
        // Her costume: a folder name only (costume_…); anything else stays the basic outfit.
        const auto dressed = parseAppSettings("companion_costume_rin=costume_05_maid\ncompanion_costume_hana=../evil\ncompanion_costume_Bad=costume_01\n");
        if (loaded.companion_costumes != chosen.companion_costumes || dressed.companion_costumes.size() != 1 || dressed.companion_costumes.at("rin") != "costume_05_maid") return 97;
        // Her photos: from a folder or off (ComfyUI's are gone).
        if (parseAppSettings("companion_photos=comfyui\n").companion_photos != AppSettings::CompanionPhotos::off ||
            parseAppSettings("companion_photos=folder\n").companion_photos != AppSettings::CompanionPhotos::folder) return 98;
        // Her ElevenLabs voice: a model for all (empty: each one's own) and voices picked per companion;
        // VOICEVOX's old lines are skipped.
        auto voiced = config;
        voiced.companion_voice = AppSettings::CompanionVoice::chat;
        voiced.companion_voice_model = "eleven_v4";
        voiced.companion_voice_ids = {{"hana", "cgSgspJ2msm6clMCkdW9"}};
        const auto heard = saveAppSettings(voiced, file) ? loadAppSettings(file) : AppSettings{};
        if (heard.companion_voice != AppSettings::CompanionVoice::chat || heard.companion_voice_model != "eleven_v4" ||
            heard.companion_voice_ids != voiced.companion_voice_ids || !fresh.companion_voice_model.empty() || !fresh.companion_voice_ids.empty()) return 99;
        const auto odd = parseAppSettings("companion_voice_engine=hybrid_pl\ncompanion_voice_style=6\ncompanion_voice_model=Bad Model\n"
                                          "companion_voice_id_rin=abc/../x\ncompanion_voice_id_Bad=abc\ncompanion_voice_id_mei=Xb7hH8MSUJpSbSDYk0k2\n");
        if (!odd.companion_voice_model.empty() || odd.companion_voice_ids.size() != 1 || odd.companion_voice_ids.at("mei") != "Xb7hH8MSUJpSbSDYk0k2") return 100;
        // Where they live: one city for all (empty: each her own) and per companion ("home": her own).
        auto moved = config;
        moved.companion_city = "los-angeles";
        moved.companion_cities = {{"hana", "tokyo"}, {"rin", "home"}};
        const auto housed = saveAppSettings(moved, file) ? loadAppSettings(file) : AppSettings{};
        if (housed.companion_city != "los-angeles" || housed.companion_cities != moved.companion_cities || !fresh.companion_city.empty() ||
            !fresh.companion_cities.empty()) return 102;
        const auto bad = parseAppSettings("companion_city=Los Angeles\ncompanion_city_Bad=tokyo\ncompanion_city_mei=osaka\ncompanion_city_rin=../x\n");
        if (!bad.companion_city.empty() || bad.companion_cities.size() != 1 || bad.companion_cities.at("mei") != "osaka") return 103;
    }
    if (parseAppSettings("windowed=no\n").windowed || !parseAppSettings("windowed=1\n").windowed) return 60;
    for (const bool windowed : {false, true}) {
        auto chosen = config; chosen.windowed = windowed;
        if (!saveAppSettings(chosen, file) || loadAppSettings(file).windowed != windowed) return 61;
    }
    // The launch picture is always on: its old switch (and the older cd_loading key) is ignored and
    // never written again.
    if (!AppSettings{}.launch_animation || !parseAppSettings("").launch_animation ||
        !parseAppSettings("launch_animation=0\n").launch_animation || !parseAppSettings(" cd_loading = 0 \r\n").launch_animation) return 44;
    {
        auto chosen = config; chosen.launch_animation = false;
        if (!saveAppSettings(chosen, file) || !loadAppSettings(file).launch_animation) return 45;
    }
    // The default game action is Szczegóły; the earlier key (it saved "launch" for everyone) is
    // ignored, a choice made since is kept.
    if (AppSettings{}.accept_action != AppSettings::AcceptAction::details ||
        parseAppSettings("accept_action=launch\n").accept_action != AppSettings::AcceptAction::details ||
        parseAppSettings("game_action=launch\n").accept_action != AppSettings::AcceptAction::launch) return 146;
    for (const auto action : {AppSettings::AcceptAction::launch, AppSettings::AcceptAction::details}) {
        auto chosen = config; chosen.accept_action = action;
        if (!saveAppSettings(chosen, file) || loadAppSettings(file).accept_action != action) return 147;
    }
    // Silent Steam launch is opt-in; only strict 0/1 values are read.
    if (AppSettings{}.steam_silent_launch || parseAppSettings("").steam_silent_launch ||
        parseAppSettings("steam_silent_launch=on\n").steam_silent_launch ||
        !parseAppSettings(" steam_silent_launch = 1 \r\n").steam_silent_launch) return 64;
    for (const bool enabled : {false, true}) {
        auto chosen = config; chosen.steam_silent_launch = enabled;
        if (!saveAppSettings(chosen, file) || loadAppSettings(file).steam_silent_launch != enabled) return 65;
    }
    // Hiding to the tray while playing is opt-in, strict 0/1 too.
    if (AppSettings{}.hide_while_playing || parseAppSettings("hide_while_playing=yes\n").hide_while_playing ||
        !parseAppSettings("hide_while_playing=1\n").hide_while_playing) return 155;
    for (const bool enabled : {false, true}) {
        auto chosen = config; chosen.hide_while_playing = enabled;
        if (!saveAppSettings(chosen, file) || loadAppSettings(file).hide_while_playing != enabled) return 156;
    }
    { std::error_code removed; std::filesystem::remove(file, removed); }
    // First-run walkthrough: only a missing file is a first start; older files count as done.
    if (std::filesystem::exists(file) || loadAppSettings(file).tour_done || !AppSettings{}.tour_done ||
        !parseAppSettings("show_store=1\n").tour_done || parseAppSettings("tour_done=0\n").tour_done ||
        !parseAppSettings("tour_done=no\n").tour_done) return 39;
    {
        AppSettings pending; pending.tour_done = false;
        if (!saveAppSettings(pending, file) || loadAppSettings(file).tour_done) return 40;
        pending.tour_done = true;
        if (!saveAppSettings(pending, file) || !loadAppSettings(file).tour_done) return 41;
        std::error_code removed; std::filesystem::remove(file, removed);
    }
    const auto widgets = parseAppSettings("widget_layout=cpu:icon,example.plugin/widget:wide,steam:square,cpu:wide,broken:huge,=bad:icon\n");
    if (widgets.widget_layout != std::vector<widgets::Preference>{{"cpu", widgets::Size::icon}, {"example.plugin/widget", widgets::Size::wide}, {"steam", widgets::Size::square}} ||
        !saveAppSettings(widgets, file) || loadAppSettings(file).widget_layout != widgets.widget_layout) return 36;
    const auto overlay_defaults = parseAppSettings("guide_overlay=yes\ngame_fps=on\ngame_fps_low=true\ngame_fps_corner=4\n");
    if (overlay_defaults.guide_overlay || overlay_defaults.game_fps || overlay_defaults.game_fps_low) return 33;
    // Legacy pin/corner keys are ignored. They cannot enable a separate HUD.
    for (const bool low : {false, true}) {
        auto overlay = parseAppSettings("guide_overlay=1\ngame_fps=1\ngame_fps_low=1\ngame_fps_pinned=1\ngame_fps_corner=3\n");
        overlay.game_fps_low = low;
        if (!saveAppSettings(overlay, file)) return 34;
        const auto restored = loadAppSettings(file);
        if (!restored.guide_overlay || !restored.game_fps || restored.game_fps_low != low || restored.quick_resume) return 35;
    }
    // Permanent categories are opt-in and remembered even while another home layout is selected.
    if (parseAppSettings("").show_categories || parseAppSettings("show_categories=yes\n").show_categories ||
        !parseAppSettings(" show_categories = 1 \r\n").show_categories) return 22;
    for (const bool enabled : {false, true}) {
        auto chosen = config; chosen.show_categories = enabled;
        if (!saveAppSettings(chosen, file) || loadAppSettings(file).show_categories != enabled ||
            loadAppSettings(file).home_layout != chosen.home_layout) return 23;
    }
    // XMB keeps its artwork and size preferences even when another layout is selected.
    using XmbSize = AppSettings::XmbSize;
    if (parseAppSettings("").xmb_posters || parseAppSettings("xmb_posters=yes\n").xmb_posters ||
        !parseAppSettings(" xmb_posters = 1 \r\n").xmb_posters ||
        parseAppSettings("xmb_size=unknown\n").xmb_size != XmbSize::normal ||
        parseAppSettings("xmb_size=small\n").xmb_size != XmbSize::compact ||
        parseAppSettings(" xmb_size = large \r\n").xmb_size != XmbSize::large) return 24;
    for (const auto size : {XmbSize::compact, XmbSize::normal, XmbSize::large})
        for (const bool posters : {false, true}) {
            auto chosen = config; chosen.home_layout = AppSettings::HomeLayout::shelves;
            chosen.xmb_posters = posters; chosen.xmb_size = size;
            if (!saveAppSettings(chosen, file)) return 25;
            const auto loaded = loadAppSettings(file);
            if (loaded.xmb_size != size || loaded.xmb_posters != posters || loaded.home_layout != chosen.home_layout) return 26;
        }
    // Flix: two tile sizes, also kept while another layout is selected.
    using FlixSize = AppSettings::FlixSize;
    if (parseAppSettings("").flix_size != FlixSize::normal || parseAppSettings("flix_size=small\n").flix_size != FlixSize::normal ||
        parseAppSettings(" flix_size = large \r\n").flix_size != FlixSize::large ||
        parseAppSettings("flix_size=large\nflix_size=default\n").flix_size != FlixSize::normal) return 31;
    for (const auto size : {FlixSize::normal, FlixSize::large}) {
        auto chosen = config; chosen.home_layout = AppSettings::HomeLayout::xmb; chosen.flix_size = size;
        if (!saveAppSettings(chosen, file) || loadAppSettings(file).flix_size != size) return 32;
    }
    using Shape = AppSettings::TileShape;
    if (parseAppSettings("").tile_shape != Shape::squircle ||
        parseAppSettings("tile_shape=unknown\n").tile_shape != Shape::squircle ||
        parseAppSettings(" tile_shape = rounded \r\n").tile_shape != Shape::rounded ||
        parseAppSettings("tile_shape=square\n").tile_shape != Shape::square) return 20;
    for (const auto shape : {Shape::squircle, Shape::rounded, Shape::square}) {
        auto chosen = config; chosen.tile_shape = shape;
        if (!saveAppSettings(chosen, file) || loadAppSettings(file).tile_shape != shape) return 21;
    }
    if (!saveAppSettings(config, file)) return 3;
    const auto loaded = loadAppSettings(file);
    if (loaded.font_family != config.font_family || loaded.hidden_collections != config.hidden_collections ||
        loaded.collection_order != config.collection_order || loaded.cover_accent_rgb != config.cover_accent_rgb ||
        loaded.ui_accent_rgb != config.ui_accent_rgb || loaded.home_layout != config.home_layout || loaded.quick_resume) return 4;
    auto experimental = config; experimental.quick_resume = true;
    if (!saveAppSettings(experimental, file) || !loadAppSettings(file).quick_resume || !saveAppSettings(config, file)) return 10;
    // Steam Controller without Steam: opt-in, strict 0/1, survives a round trip.
    if (AppSettings{}.steam_controller || !parseAppSettings(" steam_controller = 1 \r\n").steam_controller ||
        parseAppSettings("steam_controller=on\n").steam_controller || loadAppSettings(file).steam_controller) return 27;
    experimental = config; experimental.steam_controller = true;
    if (!saveAppSettings(experimental, file) || !loadAppSettings(file).steam_controller || !saveAppSettings(config, file)) return 28;
    // Springy focus outline: opt-in, strict 0/1, survives a round trip.
    if (AppSettings{}.focus_spring || !parseAppSettings("focus_spring=1\n").focus_spring ||
        parseAppSettings("focus_spring=true\n").focus_spring || loadAppSettings(file).focus_spring) return 29;
    experimental = config; experimental.focus_spring = true;
    if (!saveAppSettings(experimental, file) || !loadAppSettings(file).focus_spring || !saveAppSettings(config, file)) return 30;
    // Frames around dialogs, context menus and the walkthrough: off by default, strict 0/1, survives a round trip.
    if (AppSettings{}.panel_borders || !parseAppSettings(" panel_borders = 1 \r\n").panel_borders ||
        parseAppSettings("panel_borders=on\n").panel_borders || loadAppSettings(file).panel_borders) return 42;
    auto framed = config; framed.panel_borders = true;
    if (!saveAppSettings(framed, file) || !loadAppSettings(file).panel_borders || !saveAppSettings(config, file)) return 43;
    // Screen entrances are on for both new and legacy settings; malformed values
    // retain that default, and an explicit opt-out survives restarting the app.
    if (!AppSettings{}.screen_transitions || !parseAppSettings("").screen_transitions ||
        !parseAppSettings("screen_transitions=off\n").screen_transitions ||
        parseAppSettings(" screen_transitions = 0 \r\n").screen_transitions) return 69;
    for (const bool enabled : {false, true}) {
        auto transitions = config; transitions.screen_transitions = enabled;
        if (!saveAppSettings(transitions, file) || loadAppSettings(file).screen_transitions != enabled) return 70;
    }
    if (!saveAppSettings(config, file)) return 71;
    // Console tiles on their platform colour: on by default (also without the key), strict 0/1, survives a round trip.
    if (!AppSettings{}.console_colors || parseAppSettings(" console_colors = 0 \r\n").console_colors ||
        !parseAppSettings("console_colors=off\n").console_colors || !loadAppSettings(file).console_colors) return 46;
    auto dark = config; dark.console_colors = false;
    if (!saveAppSettings(dark, file) || loadAppSettings(file).console_colors || !saveAppSettings(config, file)) return 47;
    // Steam's hidden and private games: off by default, strict 0/1, survives a round trip.
    if (AppSettings{}.show_private || !parseAppSettings(" show_private = 1 \r\n").show_private ||
        parseAppSettings("show_private=yes\n").show_private || loadAppSettings(file).show_private) return 37;
    auto revealed = config; revealed.show_private = true;
    if (!saveAppSettings(revealed, file) || !loadAppSettings(file).show_private || !saveAppSettings(config, file)) return 38;
    // Flix home layout survives a round trip.
    auto flix = config; flix.home_layout = AppSettings::HomeLayout::flix;
    if (parseAppSettings("home_layout=flix\n").home_layout != AppSettings::HomeLayout::flix ||
        !saveAppSettings(flix, file) || loadAppSettings(file).home_layout != AppSettings::HomeLayout::flix || !saveAppSettings(config, file)) return 11;
    // So does Metro.
    auto snitch = config; snitch.home_layout = AppSettings::HomeLayout::snitch;
    if (parseAppSettings("home_layout=snitch\n").home_layout != AppSettings::HomeLayout::snitch ||
        !saveAppSettings(snitch, file) || loadAppSettings(file).home_layout != AppSettings::HomeLayout::snitch || !saveAppSettings(config, file)) return 80;
    auto metro = config; metro.home_layout = AppSettings::HomeLayout::metro;
    if (parseAppSettings("home_layout=metro\n").home_layout != AppSettings::HomeLayout::metro ||
        !saveAppSettings(metro, file) || loadAppSettings(file).home_layout != AppSettings::HomeLayout::metro || !saveAppSettings(config, file)) return 48;
    // PSfive and its tile size (small / default / large, separate from XMB's; unknown values ignored).
    auto psfive = config; psfive.home_layout = AppSettings::HomeLayout::psfive; psfive.psfive_size = AppSettings::XmbSize::large; psfive.psfive_clock = false;
    if (AppSettings{}.psfive_size != AppSettings::XmbSize::normal || parseAppSettings("home_layout=psfive\n").home_layout != AppSettings::HomeLayout::psfive ||
        parseAppSettings("psfive_size=small\n").psfive_size != AppSettings::XmbSize::compact ||
        parseAppSettings("psfive_size=small\n").xmb_size != AppSettings::XmbSize::normal ||
        parseAppSettings("psfive_size=huge\n").psfive_size != AppSettings::XmbSize::normal ||
        !AppSettings{}.psfive_clock || parseAppSettings("psfive_clock=0\n").psfive_clock || !parseAppSettings("psfive_clock=off\n").psfive_clock) return 62;
    if (!saveAppSettings(psfive, file) || loadAppSettings(file).home_layout != AppSettings::HomeLayout::psfive ||
        loadAppSettings(file).psfive_size != AppSettings::XmbSize::large || loadAppSettings(file).xmb_size != config.xmb_size ||
        loadAppSettings(file).psfive_clock ||
        !saveAppSettings(config, file)) return 63;
    auto posters = config; posters.shelves_posters = true; // Default layout posters: off by default, strict 0/1.
    if (AppSettings{}.shelves_posters || !parseAppSettings("shelves_posters=1\n").shelves_posters || parseAppSettings("shelves_posters=on\n").shelves_posters ||
        !saveAppSettings(posters, file) || !loadAppSettings(file).shelves_posters || !saveAppSettings(config, file)) return 50;
    // Sound folder and files picked from it (UTF-8): an absolute folder, plain file names only.
    auto picked = config; picked.sound_folder = L"C:\\Muzyka ąę"; picked.music_file = L"Temat ł.mp3"; picked.boot_file = L"start.wav";
    if (!saveAppSettings(picked, file)) return 51;
    const auto picked_loaded = loadAppSettings(file);
    if (picked_loaded.sound_folder != picked.sound_folder || picked_loaded.music_file != picked.music_file || picked_loaded.boot_file != picked.boot_file ||
        !parseAppSettings("sound_folder=relative\nmusic_file=..\\x.mp3\n").sound_folder.empty() ||
        !parseAppSettings("music_file=..\\x.mp3\n").music_file.empty() || !saveAppSettings(config, file) || !loadAppSettings(file).music_file.empty()) return 52;
    {
        // Interface looks added later: white buttons on grey glass, the PS5-like entrance, no focus gap,
        // square search results, no facts under the logo and no Flix icons by default; all survive a
        // round trip, junk keeps the defaults, "accent" means the interface accent.
        const AppSettings defaults;
        if (defaults.button_rgb != 0xF5F5F5 || defaults.button_idle != AppSettings::ButtonIdle::gray ||
            defaults.tile_entrance != AppSettings::TileEntrance::grow || defaults.focus_gap != AppSettings::FocusGap::none || defaults.search_art != AppSettings::SearchArt::square ||
            defaults.shelves_hero_facts || defaults.flix_top_icons) return 110;
        auto looks = config;
        looks.button_rgb = 0x0070D1; looks.button_idle = AppSettings::ButtonIdle::tinted; looks.tile_entrance = AppSettings::TileEntrance::none;
        looks.focus_gap = AppSettings::FocusGap::inside; looks.search_art = AppSettings::SearchArt::landscape; looks.shelves_hero_facts = true; looks.flix_top_icons = true;
        if (!saveAppSettings(looks, file)) return 111;
        const auto read = loadAppSettings(file);
        if (read.button_rgb != 0x0070D1 || read.button_idle != AppSettings::ButtonIdle::tinted || read.tile_entrance != AppSettings::TileEntrance::none ||
            read.focus_gap != AppSettings::FocusGap::inside || read.search_art != AppSettings::SearchArt::landscape || !read.shelves_hero_facts || !read.flix_top_icons) return 112;
        looks.button_rgb = 0;
        if (!saveAppSettings(looks, file) || loadAppSettings(file).button_rgb != 0) return 113;
        // The first version's focus_gap=1 was the outline moved outward.
        if (parseAppSettings("focus_gap=1\n").focus_gap != AppSettings::FocusGap::outside ||
            parseAppSettings("focus_gap=outside\n").focus_gap != AppSettings::FocusGap::outside) return 115;
        const auto junk = parseAppSettings("button_color=#XYZ123\nbutton_idle=neon\ntile_entrance=spin\nsearch_art=round\nfocus_gap=yes\n");
        if (junk.button_rgb != defaults.button_rgb || junk.button_idle != defaults.button_idle || junk.tile_entrance != defaults.tile_entrance ||
            junk.search_art != defaults.search_art || junk.focus_gap != AppSettings::FocusGap::none || !saveAppSettings(config, file)) return 114;
    }
    metro.metro_3d = true; // Its 3D effect: off by default, strict 0/1.
    if (AppSettings{}.metro_3d || !parseAppSettings("metro_3d=1\n").metro_3d || parseAppSettings("metro_3d=yes\n").metro_3d ||
        !saveAppSettings(metro, file) || !loadAppSettings(file).metro_3d || !saveAppSettings(config, file)) return 49;
    // Interface language: system by default, explicit choices survive a round trip, junk is ignored.
    using Language = AppSettings::LanguageChoice;
    if (AppSettings{}.language != Language::system || parseAppSettings("language=pl\n").language != Language::pl ||
        parseAppSettings(" language = en \r\n").language != Language::en || parseAppSettings("language=de\n").language != Language::system ||
        parseAppSettings("language=en\nlanguage=system\n").language != Language::system) return 11;
    for (const auto language : {Language::system, Language::pl, Language::en, Language::pt_br, Language::es}) {
        auto chosen = config; chosen.language = language;
        if (!saveAppSettings(chosen, file) || loadAppSettings(file).language != language) return 12;
    }
    // Screensaver: on, game backgrounds, one minute by default; only known values are accepted.
    using Style = AppSettings::ScreensaverStyle;
    const AppSettings defaults;
    if (!defaults.screensaver || defaults.screensaver_style != Style::backgrounds || defaults.screensaver_minutes != 1) return 14;
    const auto saver = parseAppSettings("screensaver=0\nscreensaver_style=discs\nscreensaver_minutes=10\n");
    if (saver.screensaver || saver.screensaver_style != Style::discs || saver.screensaver_minutes != 10) return 15;
    const auto junk = parseAppSettings("screensaver=off\nscreensaver_style=cubes\nscreensaver_minutes=7\n");
    if (!junk.screensaver || junk.screensaver_style != Style::backgrounds || junk.screensaver_minutes != 1 ||
        parseAppSettings(" screensaver_style = boxes \r\nscreensaver_minutes=3\n").screensaver_style != Style::boxes ||
        parseAppSettings("screensaver_minutes=3\n").screensaver_minutes != 3 || parseAppSettings("screensaver_minutes=05\n").screensaver_minutes != 1) return 16;
    for (const auto saver_style : {Style::backgrounds, Style::boxes, Style::discs})
        for (const int minutes : {1, 3, 5, 10})
            for (const bool enabled : {false, true}) {
                auto chosen = config; chosen.screensaver = enabled; chosen.screensaver_style = saver_style; chosen.screensaver_minutes = minutes;
                if (!saveAppSettings(chosen, file)) return 17;
                const auto loaded_saver = loadAppSettings(file);
                if (loaded_saver.screensaver != enabled || loaded_saver.screensaver_style != saver_style ||
                    loaded_saver.screensaver_minutes != minutes || loaded_saver.font_family != config.font_family) return 18;
            }
    // The drawer sliders (now under Appearance) keep their keys.
    auto drawer = config; drawer.drawer.opacity = 0.5f; drawer.drawer.fade_fraction = 0.2f;
    if (!saveAppSettings(drawer, file) || std::abs(loadAppSettings(file).drawer.opacity - 0.5f) > 0.001f ||
        std::abs(loadAppSettings(file).drawer.fade_fraction - 0.2f) > 0.001f) return 19;
    auto sounds = config;
    sounds.ui_sound = 0;
    sounds.toast_sound = 0;
    sounds.boot_sound = 0;
    sounds.music_theme = 0; // "Brak"
    if (!saveAppSettings(sounds, file) || loadAppSettings(file).boot_sound != 0 ||
        loadAppSettings(file).ui_sound != 0 || loadAppSettings(file).toast_sound != 0 ||
        loadAppSettings(file).music_theme != 0) return 30;
    // Aurelia and Steam: what Aurelia manages, its cloud, Steam switched off and a game's account.
    auto steam = config; steam.steam_enabled = false; steam.aurelia_default = true; steam.aurelia_cloud = false;
    steam.aurelia_steam = {"620"}; steam.steam_launch_accounts["400"] = 76561198000000002ULL;
    if (!saveAppSettings(steam, file)) return 31;
    {
        const auto loaded = loadAppSettings(file);
        if (loaded.steam_enabled || !loaded.aurelia_default || loaded.aurelia_cloud || loaded.aurelia_steam != std::unordered_set<std::string>{"620"} ||
            loaded.steam_launch_accounts.size() != 1 || loaded.steam_launch_accounts.at("400") != 76561198000000002ULL) return 31;
        if (!AppSettings{}.steam_enabled || AppSettings{}.aurelia_default || !AppSettings{}.aurelia_cloud ||
            !parseAppSettings("steam_account=../x:1\nsteam_account=400:junk\n").steam_launch_accounts.empty()) return 31;
    }
    if (!saveAppSettings(config, file)) return 13;
    // Older files only had accent_color: it must stay a cover colour and leave the UI on its default.
    const auto legacy = parseAppSettings("accent_color=#D64649\n");
    if (legacy.cover_accent_rgb != 0xD64649 || legacy.ui_accent_rgb != AppSettings{}.ui_accent_rgb) return 8;
    // White is the interface default; the former default (sage, always written) becomes white too.
    if (AppSettings{}.ui_accent_rgb != 0xEEEEEE || parseAppSettings("ui_accent_color=#A3D1C7\n").ui_accent_rgb != 0xEEEEEE ||
        parseAppSettings("ui_accent_color=#107C10\n").ui_accent_rgb != 0x107C10) return 53;
    // Outline tracing: opt-in, strict 0/1, excludes pulse even in conflicting hand-edited files.
    if (AppSettings{}.glow_trace || parseAppSettings("glow_trace=on\n").glow_trace ||
        !parseAppSettings("glow_trace=on\n").glow_pulse) return 66;
    for (const auto text : {"glow_trace=1\n", "glow_pulse=1\nglow_trace=1\n", "glow_trace=1\nglow_pulse=1\n"}) {
        const auto tracing = parseAppSettings(text);
        if (!tracing.glow_trace || tracing.glow_pulse || !saveAppSettings(tracing, file) ||
            !loadAppSettings(file).glow_trace || loadAppSettings(file).glow_pulse) return 67;
    }
    for (const bool tracing : {false, true}) {
        auto chosen = config; chosen.glow_trace = tracing; chosen.glow_pulse = false;
        if (!saveAppSettings(chosen, file) || loadAppSettings(file).glow_trace != tracing || loadAppSettings(file).glow_pulse) return 68;
    }
    // Focus glow: 100% and pulsing by default, clamped to 0-200%, strict 0/1, survives a round trip.
    if (std::abs(AppSettings{}.glow_level - 1.0f) > 0.001f || !AppSettings{}.glow_pulse ||
        std::abs(parseAppSettings("glow_level=0.4\n").glow_level - 0.4f) > 0.001f || parseAppSettings("glow_level=7\n").glow_level != 2.0f ||
        parseAppSettings("glow_level=-1\n").glow_level != 0.0f || parseAppSettings("glow_level=x\n").glow_level != 1.0f ||
        parseAppSettings("glow_pulse=0\n").glow_pulse || !parseAppSettings("glow_pulse=off\n").glow_pulse) return 54;
    // Background dot grid: 50% by default, clamped to 0-100%.
    if (std::abs(AppSettings{}.background_grid - 0.5f) > 0.001f ||
        std::abs(parseAppSettings("background_grid=0.25\n").background_grid - 0.25f) > 0.001f ||
        parseAppSettings("background_grid=3\n").background_grid != 1.0f || parseAppSettings("background_grid=-1\n").background_grid != 0.0f ||
        std::abs(parseAppSettings("background_grid=x\n").background_grid - 0.5f) > 0.001f ||
        AppSettings{}.background_grid_style != AppSettings::GridStyle::dots ||
        parseAppSettings("background_grid_style=lines\n").background_grid_style != AppSettings::GridStyle::lines ||
        parseAppSettings("background_grid_style=mesh\n").background_grid_style != AppSettings::GridStyle::mesh ||
        parseAppSettings("background_grid_style=x\n").background_grid_style != AppSettings::GridStyle::dots) return 57;
    {
        auto glow = config; glow.glow_level = 1.5f; glow.glow_pulse = false; glow.background_grid = 0.35f;
        glow.background_grid_style = AppSettings::GridStyle::mesh;
        glow.file_browser_folder = L"C:\\Gry ąę";
        if (!saveAppSettings(glow, file)) return 55;
        const auto restored = loadAppSettings(file);
        if (std::abs(restored.glow_level - 1.5f) > 0.001f || restored.glow_pulse || restored.file_browser_folder != glow.file_browser_folder ||
            std::abs(restored.background_grid - 0.35f) > 0.001f || restored.background_grid_style != AppSettings::GridStyle::mesh ||
            !parseAppSettings("file_browser_folder=relative\n").file_browser_folder.empty() || !saveAppSettings(config, file)) return 56;
    }
    {
        // Emulacja: an emulator's program and arguments by hand, a system's emulator; odd IDs and relative paths are dropped.
        AppSettings emulation;
        emulation.emulator_overrides["pcsx2"] = {L"C:\\Emu\\PCSX2 ą\\pcsx2-qt.exe", L"-batch %ROM%"};
        emulation.emulator_overrides["retroarch"] = {{}, L"-L %CORE%"};
        emulation.system_emulator["switch"] = "ryujinx";
        if (!saveAppSettings(emulation, file)) return 57;
        const auto restored = loadAppSettings(file);
        if (restored.emulator_overrides != emulation.emulator_overrides || restored.system_emulator != emulation.system_emulator) return 58;
        const auto odd = parseAppSettings("emulator.Bad Id.program=C:\\x.exe\nemulator.cemu.program=relative.exe\nsystem_emulator.ps2=PCSX2!\n");
        if (!odd.emulator_overrides.empty() || !odd.system_emulator.empty() || !saveAppSettings(config, file)) return 59;
    }
    // Deny replacement while the old file is open: the failed save must preserve it.
    const auto locked = CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (locked == INVALID_HANDLE_VALUE) return 5;
    const bool replaced = saveAppSettings(AppSettings{}, file);
    CloseHandle(locked);
    if (replaced || loadAppSettings(file).font_family != config.font_family) return 6;
}
