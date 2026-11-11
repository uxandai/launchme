// Frame output and window state: ViewContext assembly for the renderer, notifications,
// system volume polling, resize/fullscreen/focus handling and the performance overlay.
#include "launcher.hpp"
#include "launcher_internal.hpp"
#include "../ui/drawer_clock.hpp"
#include "../ui/flix_layout.hpp"
#include "../ui/xmb_layout.hpp"
#include "../ui/grid_layout.hpp"
#include "../ui/psfive_layout.hpp"
#include "../library/steam_stats.hpp"

#include <algorithm>
#include <cmath>

std::wstring desktopPlayedWhen(uint64_t seconds); // desktop.cpp: "Dzisiaj", "3 dni temu", a date; 0: "Nigdy".

namespace {
// Metro tabs are lower case, like the dashboard they follow.
std::wstring lowered(std::wstring text) {
    if (!text.empty())
        LCMapStringEx(LOCALE_NAME_USER_DEFAULT, LCMAP_LOWERCASE, text.c_str(), static_cast<int>(text.size()),
                      text.data(), static_cast<int>(text.size()), nullptr, nullptr, 0);
    return text;
}
}

void Launcher::paint(bool overlay) {
    // Behind a game with its graphics given back (src/app/memory.cpp): nothing is drawn until the
    // launcher is in front again, or a paint would build the whole device back.
    if (memory_released_ && !overlay) {
        if (settings_.low_memory && launcherHidden()) { ValidateRect(hwnd_, nullptr); return; }
        restoreMemory();
    }
    // Yielding to a game (pollBackgroundMode): no GPU work for a window nobody sees.
    if (background_mode_ && !overlay && launcherHidden()) { ValidateRect(hwnd_, nullptr); return; }
    // Desktop mode's library is laid out in DIPs at the window's DPI; every other screen (and Tryb
    // TV) on the TV canvas. Switched first: the context below reads the viewport.
    const bool desktop = !overlay && desktopActive() && !launchAnimationActive();
    const bool sheet = !overlay && desktopSheet() && !launchAnimationActive(); // Ustawienia as a desktop dialog.
    const bool embed = !overlay && desktopEmbedded() && !launchAnimationActive(); // Urządzenia, kadrowanie, plugin pages in a sheet.
    renderer_.setDesktopViewport(desktop || sheet || embed);
    ViewContext context;
    desktopText(); // Pairs the selection with the field that has the focus now.
    std::tie(context.desktop_text_begin, context.desktop_text_end) = desktopTextSelection();
    GameDetails local_preview, local_details;
    if (closing_) context.exit_progress = exit_progress_;
    context.fps_widget = settings_.game_fps;
    context.fps_graph = settings_.fps_graph;
    context.hud_layout = settings_.hud_layout;
    context.hud_label_colors = settings_.hud_label_colors;
    context.hud_scale = settings_.hud_size / 100.0f;
    context.hud_pins = settings_.hud_pins;
    context.fps_low = settings_.game_fps_low;
    context.game_fps = overlay_snapshot_.fps;
    if (profiler_.overlay()) {
        context.performance = profiler_.label(refresh_hz_, target_hz_);
        std::lock_guard lock(jobs_->mutex);
        context.performance += tr(L"\nGrafiki: ") + std::to_wstring(jobs_->jobs.size() + jobs_->secondary.size()) +
            tr(L" priorytet / ") + std::to_wstring(jobs_->prefetch.size()) + tr(L" tło / ") + std::to_wstring(jobs_->disk_cached.size()) + tr(L" gotowe");
    }
    syncScreenTransition();
    context.screen = screen_;
    const auto widget_spots = homeWidgetSpots();
    context.home_widgets = widget_spots.placement;
    context.glance_shortcut = glanceAvailable(true) && widget_spots.glance;
    if (!glanceAvailable(true)) glance_shortcut_progress_ = 0;
    context.glance_alpha = settings_.screen_transitions ? glance_ui::ease(glance_shortcut_progress_) : 1;
    context.glance_hover = glance_hover_motion_.value;
    if (widget_spots.glance) context.glance_spot = *widget_spots.glance;
    context.glance = glanceOpen() ? &glance_ : nullptr;
    context.theme_studio = theme_.open ? &theme_ : nullptr; if (theme_.open) context.hint_device = hintDevice();
    CompanionView companion_view; // Borrowed by the context until the frame is painted.
    if (settings_.companion) { companionView(companion_view); context.companion = &companion_view; }
    const auto glance_device = hintDevice();
    context.glance_key = glance_device == HintDevice::keyboard ? L"G" : glance_device == HintDevice::playstation ? L"R2" :
                        glance_device == HintDevice::nintendo ? L"ZR" : L"RT";
    context.screen_transition = screen_transition_;
    if (theme_.open && !desktop_mode_) { context.theme_scene = true; context.screen_transition = 1; } // The home behind the studio is drawn into its preview.
    context.wallpaper_zoom = wallpaper_zoom_;
    context.library_manager = libraryDialogOpen() ? &library_dialog_ : nullptr;
    context.detail_favorite_action = favoriteDetailAction();
    context.detail_collections_action = collectionsDetailAction();
    context.detail_versions_action = versionsDetailAction();
    if (screen_ == ScreenMode::details && !cards_.empty()) {
        context.detail_favorite = isFavorite(cards_[0].game);
        if (favorite_pulse_ > 0.0f && library_view::selectionKey(cards_[0].game) == favorite_pulse_key_)
            context.detail_favorite_pulse = favorite_pulse_;
        context.menu_hidden = hiddenInLauncher(cards_[0].game);
    }
    context.identify = identifyOpen() ? &identify_state_ : nullptr;
    context.bulk_covers = bulkCoversOpen() ? &bulk_ui_ : nullptr;
    context.rating_label = ratingLabel();
    context.detail_rating = ratingDetailAction();
    context.detail_identify = identifyDetailAction();
    context.detail_close_action = closeDetailAction();
    context.detail_cloud_action = cloudDetailAction();
    if (context.detail_cloud_action >= 0 && !cards_.empty()) {
        const auto found = aurelia_cloud_.find(cards_[0].game.app_id);
        context.detail_cloud = found == aurelia_cloud_.end() ? aurelia::CloudState::unknown : found->second.state;
    }
    // The board of logos has no order, filter or view of its own: what it is says a line under its tabs.
    context.library_controls = libraryControls() && !studioBoard() ? &library_controls_ : nullptr;
    if (studioBoard()) context.library_note = settings_.show_companies ?
        tr(L"Wydawcy i developerzy gier z Twojej biblioteki, którzy mają logo. Wybierz logo, aby zobaczyć ich gry.") :
        library_category_ == "publishers" ?
        tr(L"Wydawcy gier z Twojej biblioteki, którzy mają logo. Wybierz logo, aby zobaczyć ich gry.") :
        tr(L"Twórcy gier z Twojej biblioteki, którzy mają logo. Wybierz logo, aby zobaczyć ich gry.");
    context.library_menu_progress = library_menu_progress_;
    context.library_views_progress = library_views_progress_;
    context.library_dropdown_progress = library_dropdown_progress_;
    context.library_panel = library_controls_.open() ? library_controls_.panel : library_panel_shown_;
    context.library_list = libraryList();
    context.library_mode = libraryView();
    context.home_background_pending = (screen_ == ScreenMode::shelves || screen_ == ScreenMode::platform_games) && selectedHeroPending();
    context.library_options = libraryPresentation();
    context.library_search_progress = library_search_progress_;
    if (library_view::metadata(context.library_options) && !cards_.empty()) {
        const auto& game = cards_[selected_].game;
        context.preview = preview_.find(game);
        if (!context.preview) context.preview = localDetails(game);
        const auto* own = metadataOverride(game);
        if (!steamArtwork(game) || (own && own->playtime)) {
            if (context.preview) local_preview = *context.preview;
            local_preview.playtime_minutes = gamePlaytime(game, context.preview ? context.preview->playtime_minutes : std::nullopt);
            // What Szczegóły shows: the game index fills the gaps (store descriptions, RAWG, Edytuj metadane).
            if (!steamArtwork(game)) completeDetails(local_preview, game);
            context.preview = &local_preview;
        }
        // A Steam game's store details are on their way (the worker always answers, empty on failure).
        context.preview_waiting = steamArtwork(game) && game.app_id && !preview_.find(game);
    }
    context.hint_white = settings_.hint_style == AppSettings::HintStyle::white;
    if (libraryTabsShown() || homeTabsShown()) {
        // The small LB/RB (PgUp/PgDn) caps beside the strip, as the bottom-right hints.
        context.strip_hints = settings_.input_hints;
        context.hint_device = hintDevice();
    }
    if (libraryTabsShown()) {
        const auto tabs = libraryTabs();
        for (const auto& tab : tabs) context.library_tabs.push_back({tab.title, tab.count, tab.mark});
        context.library_tab = libraryTabIndex(tabs);
        if (library_tab_glide_.valid) context.library_tab_glide = library_tab_glide_.value.value;
    }
    if (storeTabsShown()) {
        if (storeHubTabs()) { context.store_tabs.push_back({tr(L"Launchery"), 0, {}, true}); context.store_tabs.push_back({tr(L"Promocje"), 0, {}, true}); }
        for (const auto& entry : plugin_menu_) context.store_tabs.push_back({entry.item.title.localized(), 0, {}, true});
        context.store_tab = storeTabIndex();
        context.strip_hints = settings_.input_hints; context.hint_device = hintDevice(); // The LB/RB caps beside the strip, as in the library.
        if (screen_ == ScreenMode::store) {
            // The catalog screen draws them in the library's place.
            context.library_tabs = context.store_tabs; context.library_tab = context.store_tab;
        }
    }
    if (homeTabsShown()) {
        for (const auto& tab : homeTabs()) context.home_tabs.push_back({tab.title, tab.count, tab.mark});
        // Metro's glides with its pages; Flix's and PSfive's with their own spring.
        context.home_tab = metroActive() ? metro_tab_ : home_tab_glide_.valid ? home_tab_glide_.value.value : static_cast<float>(homeTabIndex());
        context.home_tabs_focus = metroTabsFocused() || (psfiveTopFocused() && psfive_top_ < psfiveTabCount());
        // Out of the way of the quick bar, the drawers and the menus, as Flix's top icons.
        const float away = std::max({quick_progress_, friends_progress_, category_menu_progress_, menu_progress_});
        context.home_tabs_alpha = homeOverlayAvailable(true) ? std::clamp(1.0f - away, 0.0f, 1.0f) : 0.0f;
        if (psfiveActive()) {
            // PSfive's top bar: the strip in place of its tabs, dimmed with the bar and faded in after the start-up row.
            context.home_tabs_alpha = std::clamp(psfive_top_opacity_.value, 0.66f, 1.0f) * (psfiveIntro() ? psfive_layout::intro::chrome(arrived_) : 1.0f);
            if (!psfiveTopAvailable()) context.home_tabs_alpha *= std::clamp(1.0f - away, 0.0f, 1.0f);
        }
    }
    if (libraryView() == library_view::View::list || libraryListPlus()) {
        const auto view = renderer_.viewport();
        const auto [first, end] = list_layout::visibleRange(cards_.size(), view.height, grid_scroll_);
        for (size_t i = first; i < end; ++i) {
            const auto& game = cards_[i].game;
            const auto* info = preview_.find(game);
            if (!info) info = localDetails(game);
            auto& row = context.library_rows[i];
            if (info) {
                row.year = gameReleaseYear(info->release_date);
                row.rating = info->score;
            }
            if (const auto minutes = gamePlaytime(game, info ? info->playtime_minutes : std::nullopt)) row.playtime = formatPlaytime(*minutes);
            const auto key = gameKey(game);
            const ratings::Rating* rating = nullptr;
            if (const auto found = rawg_metadata_.find(key); found != rawg_metadata_.end()) rating = &found->second.rating;
            else if (const auto cached = ratings_.find(key); cached != ratings_.end()) rating = &cached->second;
            if (rating && rating->available()) row.rating = ratings::compactLabel(*rating);
        }
    }
    if (fast_scroll_alpha_ > 0.003f) { context.scroll_letter = scrollLetter(); context.scroll_letter_alpha = fast_scroll_alpha_; }
    context.grid_back = (libraryControls() && library_controls_.focus == library_ui::back_button && !library_controls_.open()) || searchBackFocused();
    context.crop_editor = cropOpen() ? &crop_state_ : nullptr;
    context.device_panel = devicesOpen() ? &device_state_ : nullptr;
    context.search_query = search_query_;
    context.search_caret = searchCaret();
    context.library_title = pc_source_ ? sourceName(*pc_source_) : !library_console_.empty() ? library_console_ :
                            !library_category_.empty() ? category_library_title_ : tr(L"Wszystkie gry");
    context.logo_pending = selectedLogoPending();
    context.settings_rows = &settings_rows_;
    if (settingsOpen()) {
        // Before drawing, and so before the next click or scroll: rows with tags grow to hold their description.
        renderer_.fitSettingsDescriptions(settings_rows_, settings_state_, desktop_mode_);
        context.settings_toggles.reserve(settings_rows_.size());
        for (const auto& item : settings_rows_) {
            const auto key = std::to_string(static_cast<int>(item.id)) + '|' + item.key + '|' + item.plugin_setting + '|' +
                             std::to_string(static_cast<int>(item.source)) + '|' + std::to_string(item.choice);
            const auto found = settings_toggles_.find(key);
            context.settings_toggles.push_back(found != settings_toggles_.end() ? found->second : (item.checked ? 1.0f : 0.0f));
        }
    }
    context.settings_state = settings_state_;
    {
        // The picker rises and fades in over 170 ms and fades out after closing with its last list.
        const bool picking = settingsPicking();
        const auto now = GetTickCount64();
        if (picking) {
            settings_picker_row_ = settings_state_.row(); settings_picker_tile_ = settings_state_.tile;
            settings_picker_choice_ = settings_state_.choice;
        }
        if (picking != settings_picker_open_) { settings_picker_open_ = picking; settings_picker_changed_ = now; }
        bool motion = true;
#ifdef LAUNCHME_UI_TEST
        motion = picker_motion_for_test_;
#endif
        const float progress = motion ? std::clamp(static_cast<float>(now - settings_picker_changed_) / 170.0f, 0.0f, 1.0f) : 1.0f;
        context.settings_picker_fade = !settingsOpen() ? 0.0f : picking ? progress : 1.0f - progress;
        context.settings_picker_row = context.settings_picker_fade > 0.004f ? settings_picker_row_ : -1;
        context.settings_picker_tile = settings_picker_tile_;
        context.settings_picker_choice = settings_picker_choice_;
    }
    context.settings_hover_row = settings_hover_row_;
    if (settings_hold_)
        context.settings_hold = std::clamp(std::chrono::duration<float>(std::chrono::steady_clock::now() - settings_hold_started_).count() / enter_hold_seconds, 0.0f, 1.0f);
    context.settings_hover_badge = settings_hover_badge_;
    context.settings_preview_option = settings_preview_.option;
    context.settings_preview_seconds = settings_preview_.seconds(GetTickCount64());
    context.settings_title = settingsPageTitle();
    context.settings_parent = settingsParentTitle();
    if (screen_ == ScreenMode::plugin_page) { arrangeStoreHub(); context.plugin_page = &plugin_page_; }
    if (plugin_drawer_active_) context.plugin_drawer = &plugin_page_;
    if (downloadsOpen()) context.downloads = &downloads_state_;
    if (quick_open_ || quick_progress_ > 0.003f || snitchActive()) context.downloads_moving = downloadsMoving(); // The bar's Pobierania count.
    context.settings_save_failed = settings_save_failed_;
    context.secret_length = settings_secret_input_.size();
    if (editingSecret() && settings_rows_[settings_state_.row()].control == settings_ui::Control::text) context.settings_text = settings_secret_input_;
    context.secret_selected = settings_secret_selected_;
    context.secret_cancel = settings_secret_cancel_;
    context.secret_browse = settings_secret_browse_;
    // One dock entry for every store plugin (its tabs switch between them); shown even with none.
    context.plugin_menu.push_back(tr(L"Sklepy"));
    context.plugin_menu_glyphs.push_back("store");
    for (size_t i = 0; i < plugin_widgets_.size(); ++i) {
        const auto& action = plugin_widgets_[i];
        const auto* owner = plugins_.find(action.plugin);
        context.plugin_widgets.push_back({action.item.title.localized(), pluginWidgetText(i),
            owner ? owner->manifest.icon_text : L"", action.plugin, owner ? owner->icon : ImageBytes{}});
        if (action.item.active_game) if (const auto game = pluginActiveGame()) {
            auto& widget = context.plugin_widgets.back(); widget.title = game->name;
            if (const auto* data = pluginComparison(action.plugin, *game); data && data->value.main_seconds) {
                const auto played = pluginPlaytimeSeconds(*game);
                widget.text = plugins::percentLabel(data->value, played);
                widget.caption = plugins::compactTime(played) + L" / " + plugins::compactTime(data->value.main_seconds);
            }
        }
    }
    context.store_status = store_status_;
    context.game_details = &details_;
    if (const auto action = pluginDetailProvider()) {
        context.detail_comparison_title = action->item.title.localized();
        context.detail_comparison_action = pluginDetailAction();
        const auto& game = detail_games_[0];
        const auto* data = pluginComparison(action->plugin, game);
        context.detail_comparison = data ? data->value : plugins::TimeComparison{gameKey(game), 0, 0, 0, tr(L"Wczytywanie…")};
        context.detail_playtime_seconds = pluginPlaytimeSeconds(game);
        if (context.detail_playtime_seconds) {
            local_details = details_;
            local_details.playtime_minutes = static_cast<uint32_t>(std::min<uint64_t>(UINT32_MAX, *context.detail_playtime_seconds / 60));
            context.game_details = &local_details;
        }
    }
    if (screen_ == ScreenMode::details && !cards_.empty() && !context.detail_playtime_seconds) {
        const auto& game = cards_[0].game;
        const auto* own = metadataOverride(game);
        if (!steamArtwork(game) || (own && own->playtime)) {
            local_details = details_; local_details.playtime_minutes = gamePlaytime(game, details_.playtime_minutes);
            context.game_details = &local_details;
        }
    }
    if (screen_ == ScreenMode::details && !cards_.empty())
        if (const auto* own = metadataOverride(cards_[0].game)) { // Edytuj metadane: over whatever was found.
            if (context.game_details != &local_details) { local_details = *context.game_details; context.game_details = &local_details; }
            metadata::apply(*own, local_details);
        }
    context.artwork_browser = artworkOpen() ? &artwork_state_ : nullptr;
    context.details_loading = details_loading_ && details_.description.empty() && details_.error.empty();
    context.detail_action = detail_action_;
    context.detail_achievements = achievementsDetailAction();
    if (achievementsOpen()) context.achievements = &achievements_;
    if (!cards_.empty()) {
        context.primary_action = actionLabel(cards_[selected_].game);
        context.primary_enabled = actionEnabled(cards_[selected_].game);
    }
    context.grid_scroll = grid_scroll_;
    context.perfect_games = perfect_games_.empty() ? nullptr : &perfect_games_;
    context.main_row = category_ == 0;
    context.category = categories_.empty() ? tr(L"Ostatnio grane") : categories_[category_].title;
    if (screen_ == ScreenMode::platform_games) context.category = nested_platform_;
    context.platform = screen_ == ScreenMode::platform_games ? nested_platform_ :
                       screen_ == ScreenMode::library && !library_console_.empty() ? library_console_ :
                       screen_ == ScreenMode::shelves && !cards_.empty() &&
                       cards_[selected_].game.kind == GameKind::platform ? cards_[selected_].game.platform : L"";
    context.menu_progress = menu_progress_;
    context.menu_selected = menu_selected_;
    const auto menu_rows = menuRows();
    context.menu_rows = menu_rows;
    if (menu_rows.emulator && selected_ < cards_.size()) context.menu_emulator = gameEmulatorLabel(cards_[selected_].game);
    context.menu_detail_options = menu_rows.detail_options;
    context.menu_folder = menu_rows.folder;
    context.menu_exclude = menu_rows.exclude;
    context.menu_hide = menu_rows.hide;
    context.menu_uninstall = menu_rows.uninstall;
    context.menu_remove = menu_rows.remove;
    // A game added by hand: its launch options are the game's own editor.
    context.menu_edit_game = selected_ < cards_.size() && cards_[selected_].game.source == GameSource::custom;
    context.menu_launch = menu_rows.launch;
    context.menu_compat = menu_rows.compat;
    context.menu_shortcuts = menu_rows.shortcuts;
    context.menu_metadata = menu_rows.metadata;
    context.menu_aurelia = menu_rows.aurelia;
    if (menu_rows.steam_account && selected_ < cards_.size()) context.menu_steam_account = steamAccountLabel(cards_[selected_].game);
    if (selected_ < cards_.size()) context.aurelia_cloud = aureliaCloudText(cards_[selected_].game);
    // Kept while the menu fades out so its height does not jump mid-animation.
    if (!cards_.empty()) for (const auto& action : pluginContext(cards_[selected_].game)) context.menu_plugins.push_back(action.item.title.localized());
    context.menu_beside = menuBeside();
    context.menu_anchor = libraryMenuAnchor();
    const auto state = cards_.empty() ? SessionState::none : sessionState(cards_[selected_].game);
    context.menu_suspend = settings_.quick_resume && state == SessionState::running;
    context.menu_session = state != SessionState::none;
    context.frozen_games = static_cast<size_t>(std::count_if(game_sessions_.begin(), game_sessions_.end(),
        [](const auto& entry) { return entry.second.frozen(); }));
    if (settings_.quick_resume || settings_.guide_overlay || context.frozen_games) context.sessions = &game_sessions_;
    context.sessions_frozen_only = !settings_.quick_resume && !settings_.guide_overlay;
    if (keyboard_progress_ > 0) {
        context.keyboard = &keyboard_;
        context.keyboard_progress = keyboard_progress_;
        context.keyboard_text = keyboardPreview();
        context.keyboard_caret = std::min(keyboardPreviewCaret(), context.keyboard_text.size());
    }
    context.exit_prompt = exit_prompt_.value_or(-1);
    if (exit_prompt_ && prompt_kind_ == PromptKind::uninstall) {
        context.prompt_question = std::wstring(tr(L"Odinstalować grę?")) + L" " + uninstall_game_.name;
        context.prompt_labels = {std::wstring(tr(L"Odinstaluj")), std::wstring(tr(L"Otwórz folder gry")), std::wstring(tr(L"Anuluj"))};
        context.prompt_body = tr(L"Gra zostanie odinstalowana z tego komputera; jej wpis w bibliotece zostaje.");
        context.prompt_danger = true; // Never the primary-looking answer.
    }
    if (exit_prompt_ && prompt_kind_ == PromptKind::remove) {
        context.prompt_question = std::wstring(tr(L"Usunąć z biblioteki?")) + L" " + remove_game_.name;
        context.prompt_labels = {std::wstring(tr(L"Usuń z biblioteki")), std::wstring(tr(L"Edytuj grę…")), std::wstring(tr(L"Anuluj"))};
        context.prompt_body = tr(L"Zniknie tylko wpis w launcherze; plik gry na dysku zostaje.");
        context.prompt_danger = true;
    }
    if (exit_prompt_ && prompt_kind_ == PromptKind::exclude) {
        context.prompt_question = std::wstring(tr(L"Wykluczyć grę?")) + L" " + exclude_game_.name;
        context.prompt_labels = {std::wstring(tr(L"Wyklucz")), std::wstring(tr(L"Ukryj")), std::wstring(tr(L"Anuluj"))};
        context.prompt_body = std::wstring(tr(L"Zniknie z biblioteki, kolekcji i wyszukiwania. Trafi do Biblioteka → Co pokazywać → Wykluczone gry")) +
                              L" (" + std::to_wstring(library_state_.excluded_games.size() + 1) + L"), " + tr(L"skąd ją przywrócisz.");
        context.prompt_danger = true;
    }
    if (exit_prompt_ && prompt_kind_ == PromptKind::delete_collection) {
        // An own collection's trash: delete it, or only hide (show) it on the home screen; Anuluj has the focus.
        const auto found = std::find_if(settings_collections_.begin(), settings_collections_.end(),
                                        [&](const SteamCollection& c) { return collectionKey(account_, c) == delete_collection_key_; });
        context.prompt_question = std::wstring(tr(L"Usunąć kolekcję?")) + (found == settings_collections_.end() ? std::wstring{} : L" " + found->name);
        context.prompt_labels = {std::wstring(tr(L"Usuń kolekcję")),
                                 std::wstring(settings_.hidden_collections.contains(delete_collection_key_) ? tr(L"Pokaż na ekranie głównym") : tr(L"Ukryj na ekranie głównym")),
                                 std::wstring(tr(L"Anuluj"))};
        context.prompt_body = tr(L"Gry zostają w bibliotece; znika tylko kolekcja.");
        context.prompt_danger = true;
    }
    if (exit_prompt_ && prompt_kind_ == PromptKind::restore_saves) {
        context.prompt_question = std::wstring(tr(L"Przywrócić zapisy z ostatniej kopii?")) + L" " + restore_game_.name;
        context.prompt_labels = {std::wstring(tr(L"Przywróć")), std::wstring(tr(L"Najpierw zrób kopię obecnych")), std::wstring(tr(L"Anuluj"))};
    }
    if (exit_prompt_ && prompt_kind_ == PromptKind::move_videos) {
        const auto megabytes = std::to_wstring((video_move_bytes_ + 512 * 1024) / (1024 * 1024));
        context.prompt_question = tr(L"Przenieść pobrane filmy?");
        context.prompt_labels = {tr(L"Przenieś") + (L" (" + megabytes + L" MB)"), std::wstring(tr(L"Zostaw w starym folderze")), std::wstring(tr(L"Anuluj"))};
    }
    if (power_menu_) {
        context.power_menu = *power_menu_ - (hud_menu_ ? hud_menu_first_ : 0);
        if (hud_menu_) {
            for (int i = 0; i < hudMenuVisible(); ++i) {
                const auto& row = hud_menu_rows_[static_cast<size_t>(hud_menu_first_ + i)];
                context.power_labels.push_back(row.label); context.power_icons.push_back(row.icon);
                context.power_switches.push_back(row.toggle); context.power_values.push_back(row.value);
            }
            context.power_wide = hud_menu_kind_ == MenuKind::hud;
            context.power_more_above = hud_menu_first_ > 0;
            context.power_more_below = hud_menu_first_ + hud_menu_window < static_cast<int>(hud_menu_rows_.size());
            for (int i = 0; i < hudMenuVisible(); ++i) context.power_danger.push_back(hud_menu_rows_[static_cast<size_t>(hud_menu_first_ + i)].danger);
            if (hud_menu_kind_ == MenuKind::game_close) {
                context.power_title = tr(L"Zamknąć grę?");
                context.power_subtitle = overlay_target_ ? libraryGameName(overlay_target_->key) : std::wstring{};
                context.power_header_icon = 0xE7E8;
            } else {
                context.power_title = tr(L"Widget na ekranie gry");
                context.power_subtitle = tr(L"Co i jak pokazuje się w grze");
                context.power_header_icon = 0xE7B3;
            }
        }
        for (const auto item : hud_menu_ ? std::vector<power_menu::Item>{} : power_items_) {
            context.power_labels.emplace_back(power_menu::label(item, desktop_mode_));
            context.power_danger.push_back(power_menu::danger(item));
            context.power_icons.push_back(power_menu::icon(item));
        }
        context.power_gaps = powerMenuGaps();
    }
    if (screen_ == ScreenMode::shelves && cards_.empty()) {
        context.empty_reason = emptyLibraryReason();
        context.empty_actions = emptyHomeActive();
        context.empty_focus = empty_focus_;
        context.empty_hover = context.empty_actions ? empty_hover_ : -1;
    }
    if (heroFactsShown()) {
        // Under the default home's logo: what the details page would say first, never blank.
        const auto& game = cards_[selected_].game;
        const auto* info = preview_.find(game);
        if (!info) info = localDetails(game);
        // A fact the game does not have is left out rather than shown as a dash. Steam's count from the
        // preview, else (not read yet, or the preview had none) the game index's, as the profile shows it.
        std::optional<uint32_t> steam = info ? info->playtime_minutes : std::nullopt;
        if ((!steam || !*steam) && game_index_) {
            const auto key = gameKey(game);
            const auto indexed = game_index_->read([&](const game_index::Index& value) -> std::optional<uint32_t> {
                const auto* record = value.get(key);
                return record ? record->playtime_minutes : std::nullopt;
            });
            if (indexed && *indexed) steam = indexed;
        }
        const auto minutes = gamePlaytime(game, steam);
        if (minutes && *minutes) context.hero_facts.push_back({tr(L"CZAS GRY"), formatPlaytime(*minutes)});
        context.hero_facts.push_back({tr(L"OSTATNIO GRANE"), desktopPlayedWhen(game.last_played)});
        std::wstring released = info ? info->release_date : std::wstring{};
        if (const auto* own = metadataOverride(game); own && !own->release_date.empty()) released = own->release_date;
        if (!released.empty()) context.hero_facts.push_back({tr(L"DATA WYDANIA"), game_index::releaseLabel(released, i18n::english())});
        context.hero_platform = activePlatformName(game);
        if (!context.hero_platform.empty()) context.hero_facts.push_back({tr(L"PLATFORMA"), context.hero_platform});
    }
    if (libraryCinematic() && (context.library_options.cinematic_horizontal || context.library_options.cinematic_column) && !cards_.empty()) {
        // Kinowy's synopsis: the optional platform, last played and playtime beside year and genres.
        const auto& options = context.library_options;
        const auto& game = cards_[selected_].game;
        if (options.cinematic_platform && isGame(game)) context.cinematic_platform = activePlatformName(game);
        if (options.cinematic_played && isGame(game))
            context.cinematic_played = std::wstring(tr(L"Ostatnio grane: ")) + desktopPlayedWhen(game.last_played);
        if (options.cinematic_playtime && isGame(game) && context.preview && context.preview->playtime_minutes && *context.preview->playtime_minutes)
            context.cinematic_playtime = std::wstring(tr(L"Czas gry: ")) + formatPlaytime(*context.preview->playtime_minutes);
    }
    if (xmbActive() && xmb_column_ < xmb_top_.size() && cards_.size() >= xmb_top_.size()) {
        context.xmb = true;
        context.xmb_posters = settings_.xmb_posters;
        context.xmb_scale = xmb_layout::sizeScale(static_cast<int>(settings_.xmb_size));
        context.xmb_top = xmb_top_.size();
        context.xmb_column = xmb_column_;
        context.xmb_row = xmb_row_;
        const auto& tile = xmb_top_[xmb_column_];
        context.xmb_caption = tile.name;
        if (const auto* column = xmbColumn(tile)) context.xmb_subcaption = gamesLabel(column->size());
        else if (tile.kind == GameKind::all) context.xmb_subcaption = gamesLabel(public_library_.size());
        else if (isGame(tile)) {
            // What the column's focused game says: its state, else its platform (the row is Ostatnio grane already).
            const auto session = sessionState(tile);
            context.xmb_subcaption = session == SessionState::frozen ? std::wstring(tr(L"Wstrzymana")) :
                session == SessionState::running ? std::wstring(tr(L"W grze")) :
                tile.installation.active() ? installationText(tile.installation) :
                !tile.installed ? std::wstring(localFileGame(tile.source) ? missingFileLabel(tile) : tr(L"Do instalacji")) : activePlatformName(tile);
        }
        else if (tile.kind == GameKind::glance) context.xmb_subcaption = tr(L"Nowości i premiery");
        else if (tile.kind == GameKind::companion && context.companion) {
            context.xmb_caption = context.companion->name;
            context.xmb_subcaption = context.companion->persona;
        }
    }
    if (flixActive() && !flix_rows_.empty()) {
        context.flix = true;
        context.flix_row = flix_row_;
        context.flix_scale = flix_layout::sizeScale(static_cast<int>(settings_.flix_size));
        for (size_t i = 0; i < flix_rows_.size(); ++i) {
            const auto& row = flix_rows_[i];
            context.flix_rows.push_back({categories_[row.category].title, row.begin, row.count, i == 0});
        }
        if (flix_unfold_ > 0.0005f && flix_unfold_index_ < cards_.size()) {
            // The unfolded tile's card: what the details page says first, never blank.
            const auto& game = cards_[flix_unfold_index_].game;
            context.flix_unfold_index = flix_unfold_index_;
            context.flix_unfold = flix_unfold_;
            context.flix_unfold_action = actionLabel(game);
            context.flix_unfold_play = game.installed || sessionState(game) != SessionState::none;
            context.flix_unfold_details = settings_.accept_action == AppSettings::AcceptAction::details;
            if (flix_unfold_index_ == selected_) context.flix_unfold_rating = ratingLabel();
            const auto* info = preview_.find(game);
            if (!info) info = localDetails(game);
            std::wstring released = info ? info->release_date : std::wstring{};
            if (const auto* own = metadataOverride(game); own && !own->release_date.empty()) released = own->release_date;
            if (const auto year = gameReleaseYear(released); !year.empty()) context.flix_unfold_facts.push_back(year);
            const auto minutes = gamePlaytime(game, info ? info->playtime_minutes : std::nullopt);
            if (minutes && *minutes) context.flix_unfold_facts.push_back(formatPlaytime(*minutes));
            if (const auto platform = activePlatformName(game); !platform.empty()) context.flix_unfold_facts.push_back(platform);
            if (info) {
                // "Akcja, RPG" as "Akcja • RPG"; at most three.
                std::wstring genres;
                size_t count = 0, at = 0;
                const auto& all = info->genres;
                while (at < all.size() && count < 3) {
                    auto end = all.find(L',', at);
                    if (end == std::wstring::npos) end = all.size();
                    auto genre = all.substr(at, end - at);
                    while (!genre.empty() && genre.front() == L' ') genre.erase(genre.begin());
                    while (!genre.empty() && genre.back() == L' ') genre.pop_back();
                    if (!genre.empty()) { genres += (count ? L"  •  " : L"") + genre; ++count; }
                    at = end + 1;
                }
                context.flix_unfold_genres = std::move(genres);
            }
        }
        if (flixRailShown()) {
            // Pasek boczny: always there; it gives way only to the drawers and menus over the page.
            context.flix_rail = flixRailItems();
            context.flix_rail_focus = flixRailFocused() ? flix_rail_focus_ : -1;
            context.flix_rail_hover = flix_rail_hover_;
            context.flix_rail_active = flixRailActive(context.flix_rail);
            const float away = std::max({friends_progress_, category_menu_progress_, menu_progress_});
            context.flix_rail_alpha = std::clamp(1.0f - away, 0.0f, 1.0f);
        }
    }
    context.shelf_posters = shelfPosters();
    if (metroActive() && !metro_pages_.empty() && metro_tiles_.size() == cards_.size()) {
        using metro_layout::Role;
        context.metro = true;
        context.metro_tab = metro_tab_;
        context.metro_page = metro_page_;
        context.metro_tabs_focus = metro_tabs_;
        for (const auto& page : metro_pages_) {
            context.metro_tabs.push_back(lowered(page.category ? categories_[page.category].title : std::wstring(tr(L"Start"))));
            context.metro_tab_glyphs.push_back(page.category ? homeTabGlyph(page.category) : 0);
        }
        context.metro_tiles.reserve(cards_.size());
        for (size_t i = 0; i < cards_.size(); ++i) {
            const auto role = metro_tiles_[i].role;
            const auto& game = cards_[i].game;
            ViewContext::MetroTile tile{role, game.name, {}, metro_tiles_[i].page};
            const auto games = [&](size_t category) {
                return category < categories_.size() ? static_cast<size_t>(std::count_if(categories_[category].games.begin(),
                    categories_[category].games.end(), [](const Game& item) { return isGame(item); })) : size_t{0};
            };
            if (role == Role::recent_large) tile.sub = tr(L"Ostatnio grane");
            else if (role == Role::library) tile.sub = gamesLabel(public_library_.size());
            else if (role == Role::install) tile.sub = game.installation.active() ? tr(L"Instalacja") : tr(L"Ostatnio dodana");
            else if (role == Role::all) tile.sub = gamesLabel(games(game.app_id));
            else if (role == Role::glance) tile.sub = tr(L"Nowości i premiery");
            else if (role == Role::companion && context.companion) { tile.caption = context.companion->name; tile.sub = context.companion->persona; }
            context.metro_tiles.push_back(std::move(tile));
        }
    }
    if (snitchActive()) {
        context.snitch = true;
        snitchLoadAvatar();
        context.snitch_avatar = snitch_avatar_;
        context.snitch_profile_progress = snitch_profile_progress_;
        const auto local = local_time_();
        context.snitch_clock = drawer_clock::time(local, i18n::localeName());
        clock_minute_ = drawer_clock::minute(local);
        SYSTEM_POWER_STATUS power{};
        if (GetSystemPowerStatus(&power) && power.BatteryFlag != 255 && !(power.BatteryFlag & 128) && power.BatteryLifePercent <= 100) {
            context.snitch_battery = power.BatteryLifePercent;
            context.snitch_charging = (power.BatteryFlag & 8) != 0;
        }
    }
    if (psfiveActive()) {
        context.psfive = true;
        context.psfive_action = psfiveActions() ? psfive_action_ : -1;
        context.psfive_scale = psfiveScale();
        if (psfiveIntro()) {
            context.psfive_intro = psfive_layout::intro::chrome(arrived_);
            // The home widgets (Glance, her tile) come in with the top bar, after the row.
            context.glance_alpha *= context.psfive_intro;
            if (context.companion) companion_view.tile_alpha *= context.psfive_intro;
        }
        const auto tabs = psfiveTabs();
        const int tab_count = psfiveTabCount();
        for (const auto category : tabs) {
            context.psfive_tabs.push_back(category_titles_[category]);
            context.psfive_tab_glyphs.push_back(homeTabGlyph(category));
        }
        if (tab_count > static_cast<int>(tabs.size())) {
            context.psfive_tabs.push_back(category_ >= tabs.size() ? category_titles_[category_] + L"..." : tr(L"Więcej..."));
            // With the icons on, "Więcej..." is three dots (the name of an overflow category shown stays text).
            if (settings_.home_tab_icons && category_ < tabs.size()) context.psfive_tab_glyphs.push_back(L'');
        }
        context.psfive_tab = psfiveActiveTab();
        context.psfive_tab_focus = psfiveTopFocused() && psfive_top_ < tab_count ? psfive_top_ : -1;
        if (psfiveTopFocused() && psfive_top_ >= tab_count) context.psfive_top = psfive_top_ - tab_count; // An icon (a tab: it is the bold one).
        context.psfive_clock_shown = settings_.psfive_clock;
        context.psfive_dim = settings_.psfive_dim;
        context.psfive_visible_tabs = psfiveVisibleTabs();
        if (settings_.psfive_clock) {
            const auto local = local_time_();
            context.psfive_clock = drawer_clock::time(local, i18n::localeName());
            clock_minute_ = drawer_clock::minute(local); // tick() repaints when this goes stale.
        }
        // Beside the library tile: how many games; beside a category's "Wszystkie": which category.
        if (selected_ < cards_.size()) {
            const auto& game = cards_[selected_].game;
            if (game.kind == GameKind::all) context.psfive_caption = gamesLabel(public_library_.size());
            else if (game.kind == GameKind::category && game.app_id < categories_.size()) {
                // A row's "Wszystkie": which category; a tile named after its category: only how many games.
                const auto& category = categories_[game.app_id];
                const auto count = gamesLabel(static_cast<size_t>(
                    std::count_if(category.games.begin(), category.games.end(), [](const Game& item) { return isGame(item); })));
                context.psfive_caption = game.name == category.title ? count : category.title + L" · " + count;
            } else if (game.kind == GameKind::platform) { // A console tile (Emulacja from the category menu): its games.
                if (const auto* games = consoleGames(game.platform)) context.psfive_caption = gamesLabel(games->size());
            }
        }
        // Szczegóły pod rzędem (psfive_details.cpp): the sheet while it shows or slides away.
        if ((psfiveDetailsShown() || psfive_details_progress_ > 0.001f) && selected_ < cards_.size() && isGame(cards_[selected_].game)) {
            const auto& game = cards_[selected_].game;
            ViewContext::PsfiveDetails details_sheet;
            details_sheet.progress = psfive_details_progress_;
            details_sheet.scroll = psfive_details_scroll_;
            details_sheet.section = std::max(0, psfive_details_);
            details_sheet.shot = psfive_shot_; details_sheet.similar = psfive_similar_;
            details_sheet.shots = psfive_shot_full_urls_.size();
            details_sheet.viewer = psfive_shot_view_ && psfiveDetailsShown();
            bool loading = false;
            const auto& info = desktopDetails(game, loading);
            details_sheet.loading = loading || psfive_shots_loading_;
            details_sheet.description = info.description;
            details_sheet.achievements_unlocked = info.achievements_unlocked;
            details_sheet.achievements_total = info.achievements_total;
            details_sheet.focus_glide = psfive_focus_glide_;
            details_sheet.shots_scroll = psfive_shots_scroll_; details_sheet.similar_scroll = psfive_similar_scroll_;
            details_sheet.focus_left = psfive_focus_box_[0]; details_sheet.focus_top = psfive_focus_box_[1];
            details_sheet.focus_right = psfive_focus_box_[2]; details_sheet.focus_bottom = psfive_focus_box_[3];
            // As on Szczegóły: the four main facts always (a dash when unknown), the others when known.
            const auto fact = [&](const wchar_t* label, const std::wstring& value) { if (!value.empty()) details_sheet.facts.push_back({tr(label), value}); };
            const auto main_fact = [&](const wchar_t* label, const std::wstring& value) { details_sheet.facts.push_back({tr(label), value.empty() ? L"\u2014" : value}); };
            main_fact(L"Developer", info.developer);
            main_fact(L"Wydawca", info.publisher);
            main_fact(L"Premiera", info.release_date.empty() ? std::wstring{} : game_index::releaseLabel(info.release_date, i18n::english()));
            main_fact(L"Gatunek", info.genres);
            fact(L"Gracze", info.players);
            if (info.playtime_minutes && *info.playtime_minutes) fact(L"Czas gry", formatPlaytime(*info.playtime_minutes));
            fact(L"Ocena", info.score);
            for (const auto& similar : psfive_similar_games_) details_sheet.similar_names.push_back(similar.name);
            context.psfive_details = std::move(details_sheet);
            if (psfiveDetailsShown()) context.psfive_action = -1; // The buttons give their focus to the sheet.
        }
    }
    context.metro_light = settings_.metro_light;
    context.snitch_colour_icons = settings_.snitch_colour_icons;
    context.menu_dialog = menuDialog();
    context.friends_progress = friends_progress_;
    context.friends = &friends_;
    context.friends_loading = friends_loading_;
    context.friends_scroll = friends_scroll_;
    context.friends_cursor = friends_cursor_;
    context.dashboard.list = friends_list_;
    if (friends_list_ && friend_chat_.open) context.friend_chat = &friend_chat_;
    context.dashboard.focus = dashboard_tile_;
    context.dashboard.layout = dashboardLayout();
    context.dashboard.scroll = std::min(dashboard_scroll_, context.dashboard.layout.maxScroll(drawerViewport().height));
    context.dashboard.editing = dashboardEditing();
    context.dashboard.grabbed = dashboard_grabbed_;
    context.dashboard.edit_seconds = static_cast<float>(GetTickCount64() - dashboard_edit_at_) / 1000;
    context.dashboard.sensors = sensors_;
    context.dashboard.discord_running = discord_running_;
    if (friends_progress_ > 0.003f || friends_open_) {
        const auto local = local_time_();
        context.dashboard.time = drawer_clock::time(local, i18n::localeName());
        context.dashboard.date = drawer_clock::date(local, i18n::localeName());
        clock_minute_ = drawer_clock::minute(local); // tick() repaints when this goes stale.
        profileTile(context.dashboard);
    }
    context.categories = &category_titles_;
    context.category_rail = categoryRailActive();
    context.category_rail_focus = context.category_rail && category_rail_focus_;
    context.active_category = category_;
    context.category_menu_progress = context.category_rail ? 0 : category_menu_progress_;
    context.category_cursor = category_cursor_;
    context.category_highlight_y = category_highlight_y_;
    context.category_scroll = category_menu_scroll_;
    context.quick_progress = quick_progress_;
    context.quick_selected = quick_selected_;
    focusContext(context);
    if (!notifications_.empty()) {
        auto& notice = notifications_.front();
        if (notice.kind == NotificationKind::controller && notice.connected && !notice.device_id.empty()) {
            // HID battery reports may arrive just after the connection census. This only reads
            // cached Raw Input samples, so a toast never waits on device I/O in the paint loop.
            controller_input_.enrich(connected_pads_);
            const auto pad = std::find_if(connected_pads_.begin(), connected_pads_.end(), [&](const auto& item) { return item.id == notice.device_id; });
            if (pad != connected_pads_.end()) notice.battery = pad->battery;
        }
        context.notification = &notice;
    }
    if (volume_notice_) context.volume_notice = &*volume_notice_;
    context.volume_level = volume_initialized_ ? last_volume_.level : -1;
    context.volume_muted = volume_initialized_ && last_volume_.muted;
    context.launch_progress = launch_progress_;
    if (launchAnimationActive()) {
        const auto now = std::chrono::steady_clock::now();
        context.launch_animation_seconds = std::chrono::duration<float>(now - *launch_animation_started_).count();
        if (launch_animation_ending_) context.launch_animation_ending = std::chrono::duration<float>(now - *launch_animation_ending_).count();
        context.launch_animation_title = launch_animation_title_;
        // The focused card first: its background and logo are the ones bound in the renderer.
        if (selected_ < cards_.size() && gameKey(cards_[selected_].game) == launch_artwork_key_) context.launch_artwork_index = selected_;
        else for (size_t i = 0; i < cards_.size(); ++i) if (gameKey(cards_[i].game) == launch_artwork_key_) { context.launch_artwork_index = i; break; }
    }
    if (gogInstallOpen()) context.gog_install = &gog_install_ui_;
    if (tarot_.open || tarot_.alpha > 0.003f) { context.tarot = &tarot_; context.hint_device = hintDevice(); } // Its own line of hints.
    if (gameCaseShown() && selected_ < cards_.size()) {
        context.game_case = &game_case_;
        context.game_case_kind = gameCaseKind();
        context.game_case_action = actionLabel(cards_[selected_].game);
        context.game_case_action_enabled = actionEnabled(cards_[selected_].game);
        context.hint_device = hintDevice();
        if (manual_.open || manual_.alpha > 0.003f) context.manual = &manual_;
    }
    if (profile_.open || profile_.alpha > 0.003f) { context.profile = &profile_; context.hint_device = hintDevice(); }
    if (whats_new_.open) context.whats_new = &whats_new_;
    if (aurelia_ui_.open) context.aurelia = &aurelia_ui_;
    if (customEditorOpen()) { context.custom_editor = &custom_editor_; context.custom_poster = custom_poster_; }
    if (metadataOpen()) context.tv_meta = &desktop_meta_;
    if (fileBrowserOpen()) context.file_browser = &file_browser_;
    if (color_editor_.open && settingsOpen()) context.color_editor = &color_editor_;
    screensaver::View screensaver;
    if (screensaverActive()) { screensaver = screensaverView(); context.screensaver = &screensaver; }
    TourView tour;
    if (tour_.alpha > 0.003f) { tourView(tour); context.tour = &tour; }
    context.quick_ring_time = static_cast<float>(ambient_time_);
    if (overlay && game_overlay_) {
        context.guide_overlay = true;
        context.quick_ring_time = overlay_ring_time_;
        context.quick_selected = overlay_selected_;
        context.quick_progress = overlay_motion_.bar;
        context.friends_progress = overlay_motion_.drawer;
        context.guide_opacity = overlay_motion_.opacity;
        context.quick_circle_x = overlay_motion_.circle.value;
        game_overlay_->renderer().paintOverlay(context);
        return;
    }
    inputHints(context);
    desktop_ui::View desktop_view; // Borrowed by the context until the frame is painted.
    desktop_ui::SheetView sheet_view;
    desktop_ui::View chrome_view;
    if (desktop) {
        desktopView(desktop_view);
        context.desktop = &desktop_view;
        context.hints.clear(); // Its own buttons say what they do; tooltips explain the rest.
        if (profileDocked()) {
            const auto dock = desktopDock();
            context.profile_dock_left = dock.left; context.profile_dock_top = dock.top;
            context.profile_dock_right = dock.right; context.profile_dock_bottom = dock.bottom;
        }
        if (friendsDocked()) {
            const auto dock = desktopDock();
            context.friends_docked = true;
            context.friends_chat_here = aurelia_active_;
            context.friends_dock_left = dock.left; context.friends_dock_top = dock.top;
            context.friends_dock_right = dock.right; context.friends_dock_bottom = dock.bottom;
        }
    }
    if (sheet) {
        desktopSheetView(sheet_view);
        context.desktop_sheet = &sheet_view;
        if (desktop_menu_ && screen_ == ScreenMode::details) context.desktop_sheet_menu = &*desktop_menu_;
        context.hints.clear();
        if (desktopPageDocked()) { desktopChromeView(chrome_view); context.desktop_chrome = &chrome_view; }
    }
    if (embed) {
        desktopSheetView(sheet_view);
        context.desktop_embed = &sheet_view;
        context.desktop_embed_title = screen_ == ScreenMode::crop ? std::wstring(tr(L"Popraw kadr")) :
                                      screen_ == ScreenMode::plugin_page ? plugin_page_.title : std::wstring(tr(L"Urządzenia"));
        context.hints.clear();
    }
    context.pad_cursor = pad_cursor_.on;
    context.pad_cursor_x = pad_cursor_.x; context.pad_cursor_y = pad_cursor_.y;
    crtPoint(context.pad_cursor_x, context.pad_cursor_y); // Drawn where the CRT's bend shows it under the pointer.
    if (desktop_mode_) {
        context.desktop_window = true;
        context.desktop_dialog_hover = desktop_dialog_hover_;
        context.toast_bottom = desktop || sheet;
        // A TV screen inside the desktop window keeps the window's buttons in its corner.
        context.window_strip = !desktop && !sheet && !embed && !closing_;
        context.window_max_hover = desktop_max_hover_;
        context.window_maximized = IsZoomed(hwnd_) != FALSE;
        context.window_strip_hover = desktop_strip_hover_;
        context.window_dpi = windowDpi();
    }
    // Store games still arriving: "loading" rather than "no games" while nothing is shown yet.
    // Start-up (startupHold): the cards wait behind the loading state until the libraries are in.
    renderer_.paint(cards_, selected_, scanning_ || (cards_.empty() && libraryLoading()) || startup_hold_, ambient_time_, detail_, context);
    if (closing_ && exit_progress_ >= 1) exit_black_painted_ = true;
    profiler_.frame(renderer_.timing());
    if (!scanning_ && !cards_.empty() && screen_ == ScreenMode::shelves) profiler_.startup(FrameProfiler::Startup::first_shelf);
}

bool Launcher::deferPaint() {
    if (frame_painting_ || size_move_ || closing_ || !hwnd_) return false;
    // Frames still run when the last tick is recent (2.5 frame periods, at least 50 ms); a modal
    // loop (menu, dialog, the system's own move/size loop) stops them and paints on its own.
    const auto period = static_cast<ULONGLONG>(2500 / std::max(1, target_hz_));
    if (GetTickCount64() - last_tick_ms_ > std::max<ULONGLONG>(50, period)) return false;
    paint_deferred_ = true;
    return true;
}

void Launcher::updateFrame() {
    const HWND window = frameWindow();
    if (paint_deferred_) { paint_deferred_ = false; InvalidateRect(hwnd_, nullptr, FALSE); }
    if (IsIconic(window)) return;
    frame_painting_ = true;
    UpdateWindow(window);
    frame_painting_ = false;
}

void Launcher::togglePerformance() {
    profiler_.toggle(); renderer_.profile(profiler_.enabled());
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::showNotification(NotificationKind kind, std::wstring title,
                                std::wstring subtitle, float level) {
    if (kind == NotificationKind::volume) { showVolume(level, false); return; }
    if (kind == NotificationKind::error) diagnostics::log("error", title + (subtitle.empty() ? std::wstring{} : L" — " + subtitle));
    // The same message already waiting (e.g. a save retried every frame) is not queued twice.
    const bool queued = std::any_of(notifications_.begin(), notifications_.end(), [&](const Notification& notice) {
        return notice.kind == kind && notice.title == title && notice.subtitle == subtitle;
    });
    if (queued && kind != NotificationKind::controller && kind != NotificationKind::headphones && kind != NotificationKind::download) return;
    audio_.toast();
    if (notifications_.size() >= 64 || (notifications_.size() >= 3 && kind != NotificationKind::download &&
        std::none_of(notifications_.begin(), notifications_.end(), [](const auto& n) { return n.kind == NotificationKind::download; }))) {
        // A full queue drops the oldest plain toast first, so an error is not lost to later news.
        const auto plain = std::find_if(notifications_.begin(), notifications_.end(),
            [](const Notification& notice) { return notice.kind != NotificationKind::error; });
        notifications_.erase(plain != notifications_.end() ? plain : notifications_.begin());
    }
    notifications_.push_back(Notification{kind, std::move(title), std::move(subtitle), level, 0});
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::pollVolume() {
    // The worker owns the 120 ms cadence; consume on the next frame rather than
    // adding a second polling interval to the hardware volume-key response.
    const auto snapshot = volume_monitor_.take();
    if (!snapshot) return;
    const auto& volume = *snapshot;
    if (volume_initialized_ &&
        (std::abs(volume.level - last_volume_.level) >= 0.007f || volume.muted != last_volume_.muted)) {
        showVolume(volume.level, volume.muted);
    }
    last_volume_ = volume;
    volume_initialized_ = true;
}

void Launcher::resize() {
    renderer_.resize();
    if (friends_open_) { dashboardEnsureVisible(); drawer_glide_.valid = false; }
    if (artworkOpen()) requestArtworkThumbnails();
    else if (settingsOpen()) ensureSettingVisible();
    else requestVisibleArt();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::toggleFullscreen() {
    if (desktop_mode_) { toggleMaximized(); return; } // F11 in the desktop window (Tryb TV keeps its setting).
    fullscreen_ = !fullscreen_;
    if (fullscreen_) {
        GetWindowRect(hwnd_, &previous_rect_);
        SetWindowLongPtrW(hwnd_, GWL_STYLE, WS_POPUP | WS_SYSMENU | WS_MINIMIZEBOX);
        MONITORINFO monitor{sizeof(monitor)};
        GetMonitorInfoW(MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST), &monitor);
        const RECT& r = monitor.rcMonitor;
        SetWindowPos(hwnd_, HWND_TOP, r.left, r.top, r.right - r.left, r.bottom - r.top,
                     SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    } else {
        SetWindowLongPtrW(hwnd_, GWL_STYLE, WS_OVERLAPPEDWINDOW);
        MONITORINFO monitor{sizeof(monitor)};
        GetMonitorInfoW(MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST), &monitor);
        const auto& work = monitor.rcWork;
        const LONG width = std::min(std::max<LONG>(640, previous_rect_.right - previous_rect_.left), work.right - work.left);
        const LONG height = std::min(std::max<LONG>(480, previous_rect_.bottom - previous_rect_.top), work.bottom - work.top);
        const LONG x = std::clamp(previous_rect_.left, work.left, work.right - width);
        const LONG y = std::clamp(previous_rect_.top, work.top, work.bottom - height);
        previous_rect_ = {x, y, x + width, y + height};
        const RECT& r = previous_rect_;
        SetWindowPos(hwnd_, HWND_NOTOPMOST, r.left, r.top, r.right - r.left, r.bottom - r.top,
                     SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    }
    settings_.windowed = !fullscreen_;
    settings_dirty_ = true; settings_save_failed_ = false; settings_edited_ = std::chrono::steady_clock::now();
    if (settingsOpen()) buildSettingsRows();
}

void Launcher::focusRegained() {
    if (closing_) return;
    // Steam and the PC stores are not rescanned here: their workers follow the files and
    // registry keys they read (ChangeWatch) and rescan exactly when those change.
    pokeRunningScan(); // A game may have been started (or closed) while the launcher was in the background.
    refreshCustomGames(); // A drive with user-added games may have been (dis)connected.
    if (settings_.emulation_source == AppSettings::EmulationSource::roms && !rom_scanning_) startRomScan(); // ROMs added meanwhile.
}
