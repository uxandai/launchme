#include "launcher.hpp"
#include <functional>
#include "../platform/etw_access.hpp"
#include "../platform/flac.hpp"
#include "../platform/music_themes.hpp"
#include "../platform/sound_cues.hpp"
#include "../library/screenscraper_config.hpp"
#include "../platform/text_encoding.hpp"

#include <algorithm>
#include <cmath>

using namespace settings_ui;

void Launcher::settingsPreviewFocus() {
    settings_hover_row_ = -1;
    const bool layout = settingsOpen() && settings_state_.options && !settings_state_.editing &&
        !settings_rows_.empty() && settings_rows_[settings_state_.row()].control == Control::layout;
    settings_preview_.focus(layout ? settings_rows_[settings_state_.row()].choice : -1, GetTickCount64());
}

bool Launcher::settingsPicking() const {
    return settingsOpen() && settings_state_.editing && !color_editor_.open && !settings_rows_.empty() &&
           settings_state_.row() < static_cast<int>(settings_rows_.size()) && settings_rows_[settings_state_.row()].picker();
}

void Launcher::ensureSettingVisible() {
    const auto view = renderer_.viewport();
    const auto layout = Layout::of(view.width, view.height, settings_state_, settings_rows_);
    settings_state_.scroll[settings_state_.category] = layout.ensureVisible(settings_state_.row(),
        static_cast<int>(settings_rows_.size()), settings_state_.offset());
}

void Launcher::openSettings() {
    desktopLeaveLibrary(); rememberPlace();
    stopVideo(); video_key_.clear(); // The focused game's video does not play on under Settings.
    screen_ = ScreenMode::settings;
    color_editor_ = {};
    settings_page_exits_ = false;
    settings_state_.closeAll();
    settings_state_.options = settings_state_.editing = settings_state_.back_focused = false;
    settings_motion_.valid = false;
    closeFriends();
    closeCategoryMenu();
    dismissMenu();
    closeQuick();
    quick_progress_ = 0;
    buildSettingsRows();
    settingsPreviewFocus();
    refreshPcLibraries();
    audio_.quick();
    rebuildCards(true);
}

// A page opened from the focused row of the current category: that row and the list's scroll are
// kept for Back, the page starts at its first row.
void Launcher::openSettingsPage(Page page, std::string key, int value) {
    std::optional<RowRef> opener;
    if (settings_state_.row() >= 0 && settings_state_.row() < static_cast<int>(settings_rows_.size()))
        opener = RowRef::of(settings_rows_[settings_state_.row()]);
    settings_state_.open(page, std::move(key), value, std::move(opener));
    clearSecretInput();
    settings_motion_.valid = false;
    buildSettingsRows(); settingsPreviewFocus(); audio_.context();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::closeSettingsPage() {
    settings_state_.close();
    settings_motion_.valid = false;
    buildSettingsRows(); settingsPreviewFocus();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

// The row wherever it is in the current category: its own list, or one of the pages its tiles open.
bool Launcher::revealSetting(Id id, const std::string& key) {
    if (!settingsOpen()) return false;
    const auto focus = [&] {
        const auto found = std::find_if(settings_rows_.begin(), settings_rows_.end(), [&](const Row& row) {
            return row.id == id && (key.empty() || row.key == key);
        });
        if (found == settings_rows_.end()) return false;
        settings_state_.options = true; settings_state_.editing = false; settings_state_.back_focused = false;
        settings_state_.selected[settings_state_.category] = static_cast<int>(found - settings_rows_.begin());
        ensureSettingVisible(); settingsPreviewFocus();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return true;
    };
    if (focus()) return true;
    settings_state_.closeAll(); settings_motion_.valid = false;
    buildSettingsRows();
    if (focus()) return true;
    // The pages behind the list's tiles, and the pages behind those (Interfejs → Wszystkie opcje wyglądu → Zaznaczona okładka).
    const std::function<bool(int)> search = [&](int depth) {
        std::vector<std::pair<int, Page>> tiles;
        for (int i = 0; i < static_cast<int>(settings_rows_.size()); ++i)
            if (settings_rows_[i].control == Control::page) tiles.push_back({i, settings_rows_[i].page});
        for (const auto& [index, page] : tiles) {
            settings_state_.selected[settings_state_.category] = index;
            settings_state_.open(page, {}, 0, RowRef::of(settings_rows_[index]));
            buildSettingsRows();
            if (focus() || (depth > 0 && search(depth - 1))) return true;
            settings_state_.close();
            buildSettingsRows();
        }
        return false;
    };
    return search(1);
}

void Launcher::settingsMove(int amount) {
    if (bulkCoversOpen()) { bulkCoversKey(amount < 0 ? VK_LEFT : VK_RIGHT, false); return; }
    if (!settingsOpen() || !amount) return;
    desktop_sheet_keyboard_ = true; // Keys or the pad: the desktop dialog shows its focus.
    if (color_editor_.open) { colorEditorMove(amount); return; }
    if (editingSecret()) return;
    if (!drm_confirm_.empty()) { drm_confirm_.clear(); buildSettingsRows(); } // Moving away cancels "Potwierdź".
#ifndef _WIN32
    if (!compat_.confirm.empty() && !settings_state_.editing) { compat_.confirm.clear(); buildSettingsRows(); }
#endif
    if (settings_state_.editing && !settings_rows_.empty() && settings_rows_[settings_state_.row()].picker()) {
        const auto count = static_cast<int>(settings_rows_[settings_state_.row()].pickList(settings_state_.tile).size());
        const int before = settings_state_.choice;
        settings_state_.choice = std::clamp(settings_state_.choice + amount, 0, count - 1);
        if (before == settings_state_.choice) { navigationBoundary(); return; }
    } else {
        settings_state_.editing = false;
        const int previous = settings_state_.category;
        const int row = settings_state_.row();
        const bool back = settings_state_.backFocused();
        settings_state_.move(amount, static_cast<int>(settings_rows_.size()));
        if (previous == settings_state_.category && row == settings_state_.row() && back == settings_state_.backFocused()) {
            navigationBoundary(); return;
        }
        if (settings_state_.category != previous) buildSettingsRows();
        // The trash's column keeps to a collection that has one (a Steam collection's ends at ↓).
        if (settings_state_.subcontrol == 3 && !settings_rows_.empty() && !settings_rows_[settings_state_.row()].removable) settings_state_.subcontrol = 2;
        ensureSettingVisible();
    }
    audio_.navigate();
    settingsPreviewFocus();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::settingsChanged(bool shelves) {
    settings_dirty_ = true;
    settings_save_failed_ = false;
    settings_edited_ = std::chrono::steady_clock::now();
    renderer_.setDrawerStyle(settings_.drawer);
    renderer_.setFontFamily(settings_.font_family);
    renderer_.setCoverAccent(settings_.cover_accent_rgb);
    renderer_.setUiAccent(settings_.ui_accent_rgb);
    renderer_.setButtonTheme(settings_.button_rgb, settings_.button_idle);
    renderer_.setFocusGap(settings_.focus_gap);
    renderer_.setGlow(settings_.glow_level, settings_.glow_pulse, settings_.glow_trace);
    renderer_.setBackgroundGrid(settings_.background_grid, settings_.background_grid_style);
    renderer_.setTileShape(settings_.tile_shape);
    renderer_.setPanelBorders(settings_.panel_borders);
    renderer_.setGlass(settings_.glass);
    renderer_.setConsoleColors(settings_.console_colors);
    renderer_.setMetroLight(settings_.metro_light);
    renderer_.setWallpaperPlaces(settings_.wallpaper_snitch, settings_.wallpaper_menus, settings_.wallpaper_artwork, settings_.wallpaper_tiles);
    configureGameOverlay();
    audio_.setMusicLevel(settings_.music_volume);
    if (shelves) buildShelves(); else buildSettingsRows();
}

void Launcher::saveSettings() {
    if (!settings_dirty_) return;
    settings_save_failed_ = !saveAppSettings(settings_);
    if (!settings_save_failed_) { settings_dirty_ = false; settings_.load_failed = false; } // The unreadable file was kept once.
    else showNotification(NotificationKind::error, tr(L"Nie udało się zapisać ustawień"),
                          tr(L"Zmiany są aktywne. Ponów zapis w Ustawieniach."));
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::settingsReorder(int amount) {
    if (!settingsOpen() || !settings_state_.options || settings_state_.editing ||
        !settings_state_.at(Page::collections)) return;
    // The page starts with "Zarządzaj kolekcjami": a collection is found by its row's key.
    const int row = settings_state_.row(), next_row = row + amount;
    if (row < 0 || row >= static_cast<int>(settings_rows_.size()) || settings_rows_[row].id != Id::collection ||
        next_row < 0 || next_row >= static_cast<int>(settings_rows_.size()) || settings_rows_[next_row].id != Id::collection) return;
    const auto found = std::find_if(settings_collections_.begin(), settings_collections_.end(), [&](const auto& collection) {
        return collectionKey(account_, collection) == settings_rows_[row].key;
    });
    if (found == settings_collections_.end()) return;
    const int current = static_cast<int>(found - settings_collections_.begin()), next = current + amount;
    if (next < 0 || next >= static_cast<int>(settings_collections_.size())) return;
    std::swap(settings_collections_[current], settings_collections_[next]);
    // The rows follow at once: the rebuild below keeps the focus on the moved collection's row.
    std::swap(settings_rows_[row], settings_rows_[next_row]);
    for (const auto& collection : settings_collections_) std::erase(settings_.collection_order, collectionKey(account_, collection));
    for (const auto& collection : settings_collections_)
        settings_.collection_order.push_back(collectionKey(account_, collection));
    settings_state_.selected[settings_state_.category] = next_row;
    settingsChanged(true);
    audio_.navigate();
}

void Launcher::settingsSetLevel(float level) {
    if (settings_rows_.empty()) return;
    level = std::clamp(level, 0.0f, 1.0f);
    switch (settings_rows_[settings_state_.row()].id) {
    case Id::system_volume: {
        // The default output's volume, through the device service like the Dźwięk panel's slider.
        auto& channel = settings_devices_.audio[0];
        const float value = std::round(level * 50) / 50;
        if (!device_service_ || !channel.available || std::abs(channel.volume - value) < 0.005f) return;
        const auto sequence = ++audio_command_sequence_;
        settings_volume_pending_[0] = sequence;
        channel.volume = value;
        device_service_->submit({devices::Action::volume, channel.endpoint, value, false, sequence});
        buildSettingsRows();
        return;
    }
    case Id::glass: settings_.glass = std::round(level * 20) / 20.0f; settingsChanged(); return; // Visual only.
    case Id::music: settings_.music_volume = std::round(level * 40) / 100.0f; break;
    case Id::opacity: settings_.drawer.opacity = std::round(level * 98) / 100.0f; break;
    case Id::fade: settings_.drawer.fade_fraction = (6 + std::round(level * 69)) / 100.0f; break;
    case Id::xmb_size: settings_.xmb_size = static_cast<AppSettings::XmbSize>(static_cast<int>(std::round(level * 2))); break;
    case Id::flix_size: settings_.flix_size = level >= 0.5f ? AppSettings::FlixSize::large : AppSettings::FlixSize::normal; break;
    case Id::psfive_size: settings_.psfive_size = static_cast<AppSettings::XmbSize>(static_cast<int>(std::round(level * 2))); break;
    case Id::companion_reply_length: settings_.companion_reply_length = static_cast<companion::ReplyLength>(static_cast<int>(std::round(level * 2))); break;
    case Id::hud_size: settings_.hud_size = 50 + 10 * static_cast<int>(std::round(level * 5)); break; // 50..100 %, in tens.
    case Id::glow_level: settings_.glow_level = std::round(level * 20) / 10.0f; settingsChanged(); return; // Visual only.
    case Id::background_grid: settings_.background_grid = std::round(level * 20) / 20.0f; settingsChanged(); return;
    default: return;
    }
    settingsChanged();
}

void Launcher::settingsAdjust(int amount) {
    if (bulkCoversOpen()) { bulkCoversKey(amount < 0 ? VK_LEFT : VK_RIGHT, false); return; }
    if (!settingsOpen() || !amount) return;
    desktop_sheet_keyboard_ = true;
    if (color_editor_.open) { colorEditorAdjust(amount); return; }
    if (editingSecret()) {
        if (settings_rows_[settings_state_.row()].id == Id::companion_import_folder) {
            const int current = settings_secret_browse_ ? 0 : settings_secret_cancel_ ? 1 : 2;
            const int next = std::clamp(current + (amount < 0 ? -1 : 1), 0, 2);
            if (current == next) { navigationBoundary(); return; }
            settings_secret_browse_ = next == 0;
            settings_secret_cancel_ = next == 1;
            audio_.navigate(); InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        }
        // Left: Cancel, Right: Save (the dialog opens on Save).
        const bool cancel = amount < 0;
        if (cancel == settings_secret_cancel_) { navigationBoundary(); return; }
        settings_secret_cancel_ = cancel; audio_.navigate(); InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (!settings_state_.options) {
        if (settings_state_.backFocused()) return; // Back has no row to the right of it.
        if (amount > 0) settings_state_.enter(static_cast<int>(settings_rows_.size()));
    } else if (!settings_rows_.empty() && settings_rows_[settings_state_.row()].control == Control::collection) {
        if (amount < 0 && settings_state_.subcontrol == 0) settings_state_.options = false;
        else settings_state_.subcontrol = std::clamp(settings_state_.subcontrol + amount, 0, settings_rows_[settings_state_.row()].removable ? 3 : 2);
    } else if (!settings_rows_.empty() && settings_rows_[settings_state_.row()].control == Control::layout) {
        // Left/Right pick the preview directly; Left on the first one returns to the category rail.
        const int current = settings_rows_[settings_state_.row()].choice;
        if (amount < 0 && current == 0) settings_state_.options = false;
        else setHomeLayout(std::clamp(current + amount, 0, static_cast<int>(home_layouts.size()) - 1));
    } else if (!settings_rows_.empty() && settings_rows_[settings_state_.row()].control == Control::avatar) {
        // Left/Right pick a companion (past the "Wkrótce" cards); Left on the first one returns to the category rail.
        const int current = settings_rows_[settings_state_.row()].choice;
        int next = current + amount;
        while (next >= 0 && next < companion::lookCount() && companion::character(static_cast<companion::Look>(next))->soon) next += amount;
        if (amount < 0 && next < 0) settings_state_.options = false; // Nobody to the left.
        else if (next >= companion::lookCount()) { navigationBoundary(); return; }
        else { settings_.companion_look = std::string(companion::lookKey(static_cast<companion::Look>(next))); settingsChanged(); audio_.navigate(); }
    } else if (!settings_state_.editing && !settings_rows_.empty() && tiled(settings_rows_[settings_state_.row()].control)) {
        // Tiles: Left/Right move between them; Left on the first one returns to the category rail.
        const int count = static_cast<int>(settings_rows_[settings_state_.row()].tiles.size());
        const int current = std::clamp(settings_state_.tile, 0, std::max(0, count - 1));
        if (amount < 0 && current == 0) settings_state_.options = false;
        else if (current + amount < 0 || current + amount >= count) { navigationBoundary(); return; }
        else { settings_state_.tile = current + amount; audio_.navigate(); }
    } else if (settings_state_.editing && !settings_rows_.empty()) {
        const auto& row = settings_rows_[settings_state_.row()];
        if (row.control == Control::platform) {
            if (amount < 0) settings_state_.editing = false;
            InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        }
        if (row.picker()) { settingsMove(amount); return; }
        const int steps = sizeSteps(row.id);
        const float range = steps ? steps - 1.0f : row.id == Id::music ? 40.0f : row.id == Id::opacity ? 98.0f :
                            row.id == Id::system_volume ? 50.0f :
                            row.id == Id::glow_level || row.id == Id::background_grid || row.id == Id::glass ? 20.0f : 69.0f;
        settingsSetLevel(row.level + amount / range);
    } else if (amount < 0) settings_state_.options = false;
    else if (!settings_rows_.empty() && settings_rows_[settings_state_.row()].control == Control::platform) settings_state_.editing = true;
    else { settingsActivate(); return; }
    settingsPreviewFocus();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::settingsActivate() {
    if (bulkCoversOpen()) { bulkCoversKey(VK_RETURN, false); return; }
    if (!settingsOpen()) return;
    if (color_editor_.open) {
        // A on a slider saves as well: the colour is already shown everywhere. Cancel restores it.
        closeColorEditor(color_editor_.focus != settings_ui::ColorEditor::cancel);
        return;
    }
    // The key/text dialog: Save, or Cancel (Left) as Esc does.
    if (editingSecret() && settings_secret_browse_) { browseCompanionImport(); return; }
    if (editingSecret() && settings_secret_cancel_) { back(); return; }
    if (settings_state_.backFocused()) { back(); return; } // Same as the mouse on Back: leave Settings.
    if (!settings_state_.options) {
        settings_state_.enter(static_cast<int>(settings_rows_.size()));
        settingsPreviewFocus();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (settings_rows_.empty()) return;
    const auto row = settings_rows_[settings_state_.row()];
#ifndef _WIN32
    if (compatSettingsActivate(row)) return; // Proton i Wine and its pages (compat_activate.cpp).
    if (row.id == Id::compat_install_game) { compatInstallGame(); return; } // Also on Gry dodane ręcznie.
#endif
    if (row.id == Id::ludusavi) { if (row.locked) navigationBoundary(); else openLudusavi(); return; }
    // A game's shortcuts (Page::game_shortcuts) and every game's at once (Biblioteka → Skróty gier).
    if (settings_state_.at(Page::game_shortcuts) && (row.id == Id::launch_shortcut_desktop || row.id == Id::launch_shortcut_menu || row.id == Id::launch_steam)) {
        const auto place = row.id == Id::launch_shortcut_desktop ? ShortcutPlace::desktop : row.id == Id::launch_shortcut_menu ? ShortcutPlace::menu : ShortcutPlace::steam;
        setShortcut(shortcuts_game_, place, !hasShortcut(shortcuts_game_, place));
        buildSettingsRows(); audio_.context(); InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (row.id == Id::shortcuts_all_menu || row.id == Id::shortcuts_all_steam) {
        addShortcutsForAll(row.id == Id::shortcuts_all_steam ? ShortcutPlace::steam : ShortcutPlace::menu);
        buildSettingsRows(); audio_.context(); InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (row.id == Id::aurelia) { toggleAurelia(); return; }
    if (row.id == Id::aurelia_open) { openAurelia(); return; }
    if (row.id == Id::aurelia_cloud || row.id == Id::aurelia_default) { // Steam's page: what Aurelia does.
        if (row.locked) { navigationBoundary(); return; }
        auto& value = row.id == Id::aurelia_cloud ? settings_.aurelia_cloud : settings_.aurelia_default;
        value = !value;
        if (row.id == Id::aurelia_cloud && !value) aurelia_cloud_.clear();
        settingsChanged(true); buildSettingsRows();
        if (aurelia_ui_.open) aureliaRows();
        audio_.context(); return;
    }
    // Its settings on a page of their own; a row of one kind (Emulacja's systems) passes which one.
    if (row.control == Control::page) { openSettingsPage(row.page, row.id == Id::page ? std::string{} : row.key); return; }
    if (row.id == Id::bulk_covers) { openBulkCovers(); return; }
    if (settingsExtraActivate(row)) return; // The wallpaper picker, Bez tapety (settings_extras.cpp).
    if (row.id == Id::steam_sync) { requestSteamSync(true); audio_.context(); return; }
    if (row.id == Id::collection_manage) { openCollectionManager(); return; }
    if (row.id == Id::studio_hidden) { openSettingsPage(Page::hidden_studios); return; }
    if (row.id == Id::studio_show_all) { showStudios(); closeSettingsPage(); return; }
    if (row.id == Id::collections_open) { openSettingsCollections(); return; }
    if (row.control == Control::sources && settings_state_.editing) { settingsPickSource(row, settings_state_.choice); return; }
    if (settingsTileActivate(row)) return;
    if (row.id == Id::bt_devices) { openDevices(devices::Panel::bluetooth); return; }
    if (row.id == Id::windows_sound || (row.id == Id::bt_audio && row.checked)) {
        // Windows owns connecting paired audio devices and their formats.
        if (execute_(hwnd_, GameAction{row.id == Id::windows_sound ? L"ms-settings:sound" : L"ms-settings:bluetooth"}) <= 32)
            showNotification(NotificationKind::error, tr(L"Nie udało się otworzyć ustawień Windows."), row.title);
        return;
    }
    if (row.id == Id::bt_audio) {
        if (row.locked || !device_service_) { navigationBoundary(); return; }
        device_service_->submit({devices::Action::pair, text_encoding::wideLenient(row.key)});
        settings_device_poll_ = 0; audio_.context();
        return;
    }
    if (row.id == Id::custom_games) { openSettingsPage(Page::custom_games); return; } // "Dodaj grę", the games below it.
    if (row.id == Id::windowed) {
        // Tryb TV's window; the desktop window stays as it is meanwhile.
        if (desktop_mode_) { settings_.windowed = !settings_.windowed; settingsChanged(); }
        else toggleFullscreen();
        audio_.context(); return;
    }
    settingsPreviewFocus();
    if (pluginSettingsActivate(row)) return;
    if (pcSettingsActivate(row)) return; // Also the store accounts and their sign-in codes.
    if (row.id == Id::custom_add || row.id == Id::custom_game) { openCustomEditor(row.key); return; }
    if (row.id == Id::custom_covers) {
        std::vector<Game> games;
        for (const auto& game : custom_library_) if (isGame(game)) games.push_back(game);
        openBulkCovers(std::move(games), true); // Starts at once: the row asked for exactly this.
        return;
    }
    if (row.id == Id::screensaver_start) { startScreensaver(); return; } // Also while it is switched off: a preview.
    if (companionSetting(row)) return; // Towarzyszka's rows (companion_settings.cpp).
    if (row.id == Id::screenscraper_test) { testScreenScraper(); return; }
    if (row.id == Id::screenscraper_clear) { clearScreenScraper(); return; }
    if (row.id == Id::video_folder_reset) {
        settings_.video_folder.clear();
        settingsChanged(); audio_.context();
        settings_state_.selected[settings_state_.category] = std::max(0, settings_state_.row() - 1); // Its row is gone.
        return;
    }
    if (row.id == Id::video_folder) {
        const auto apply = [this](const std::filesystem::path& picked) {
            if (!settingsOpen() || picked.empty()) return;
            settings_.video_folder = picked;
            settingsChanged(); audio_.context();
        };
        file_browser::Request request;
        request.mode = file_browser::Mode::folder;
        request.title = tr(L"Wybierz folder filmów");
        request.initial = settings_.video_folder.empty() ? userConfigDirectory() / L"media" : settings_.video_folder;
        request.native = [this, initial = request.initial] { return pickVideoFolder(hwnd_, initial); };
        openFileBrowser(std::move(request), apply);
        return;
    }
    if (row.id == Id::sound_folder) {
        const auto apply = [this](const std::filesystem::path& picked) {
            if (!settingsOpen()) return;
            settings_.sound_folder = picked;
            setUserSoundFolder(picked);
            settingsChanged(); // Lists the folder's files in the rows below.
            requestMusic();    // A theme missing before may be there now.
            requestSound(SoundCue::boot, settings_.boot_sound, false);
            audio_.context();
        };
        if (pick_sound_folder_) { // Tests: an injected picker answers at once.
            const auto picked = pick_sound_folder_(hwnd_, settings_.sound_folder);
            mouse_transition_until_ = GetTickCount64() + GetDoubleClickTime();
            if (picked.empty()) InvalidateRect(hwnd_, nullptr, FALSE); else apply(picked);
            return;
        }
        file_browser::Request request;
        request.mode = file_browser::Mode::folder;
        request.title = tr(L"Wybierz folder dźwięków");
        request.initial = settings_.sound_folder;
        request.native = [this, initial = settings_.sound_folder] { return pickSoundFolder(hwnd_, initial); };
        openFileBrowser(std::move(request), apply);
        return;
    }
    if (row.id == Id::tour) { // Back to the first-run state: the walkthrough runs now, and again at the next start until finished.
        settings_.tour_done = false;
        settings_dirty_ = true; settings_edited_ = std::chrono::steady_clock::now();
        startTour();
        return;
    }
    if (row.id == Id::fps_access) {
        if (etw_access::state() != etw_access::State::missing) return;
        if (etw_access::grant()) {
            showNotification(NotificationKind::launch, tr(L"Uprawnienia nadane"), tr(L"Wyloguj się z Windows i zaloguj ponownie, aby pomiar FPS zadziałał."));
            settingsChanged(); // Rebuilds the page: the row now says to sign in again.
        } else showNotification(NotificationKind::error, tr(L"Nie nadano uprawnień"), tr(L"Potrzebna jest zgoda administratora."));
        return;
    }
    if (row.id == Id::update) { updateAction(); return; } // Aktualizuj (one click), or look for one now.
    if (row.id == Id::about) { // The data folder: launchme.log and crashes\ to send with a report.
        if (execute_(hwnd_, GameAction{userConfigDirectory().wstring(), {}, {}, ActionKind::open_client}) <= 32)
            showNotification(NotificationKind::error, tr(L"Nie można otworzyć folderu"), userConfigDirectory().wstring());
        return;
    }
    if (row.id == Id::autostart) {
        settings_.autostart = !settings_.autostart;
        if (!applyAutostart(settings_.autostart)) {
            settings_.autostart = !settings_.autostart;
            showNotification(NotificationKind::error, tr(L"Nie można zmienić uruchamiania z systemem"), L"");
        }
        settingsChanged(); audio_.context(); return;
    }
    if (row.control == Control::collection && settings_state_.subcontrol == 3) {
        if (row.removable) askDeleteCollection(row.key);
        return;
    }
    if (row.control == Control::collection && settings_state_.subcontrol != 0) {
        settingsReorder(settings_state_.subcontrol == 1 ? -1 : 1);
        return;
    }
    if (row.id == Id::screenscraper) {
        const auto* field = screenscraper::findField(row.key);
        if (!field) return;
        if (settings_state_.editing) {
            if (!screenscraper::save(field->field, settings_secret_input_)) {
                showNotification(NotificationKind::error, tr(L"Nie udało się zapisać ustawienia ScreenScraper"),
                    field->field == screenscraper::Field::base_url ? tr(L"Adres musi zaczynać się od https://.") :
                    field->field == screenscraper::Field::output ? tr(L"Wpisz json albo xml.") :
                    tr(L"Sprawdź wartość i dostęp do katalogu konfiguracji."));
                return;
            }
            clearSecretInput();
            settings_state_.editing = false;
            buildSettingsRows();
        } else {
            clearSecretInput();
            // Text fields edit the current value; an emptied field returns to its default.
            settings_secret_input_ = screenscraper::value(field->field);
            settings_secret_selected_ = true;
            settings_state_.editing = true;
        }
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (row.control == Control::secret) {
        if (settings_state_.editing) {
            const bool saved = row.id == Id::steam_key ? saveUserSecret(L"steam_web_api_key.txt", settings_secret_input_) :
                               row.id == Id::rawg_key ? saveUserSecret(L"rawg_api_key.txt", settings_secret_input_) :
                               row.id == Id::sgdb_key ? saveUserSecret(L"steamgriddb_api_key.txt", settings_secret_input_) :
                               row.id == Id::thegamesdb_key ? saveUserSecret(L"thegamesdb_api_key.txt", settings_secret_input_) :
                               false;
            if (!saved) {
                showNotification(NotificationKind::error, tr(L"Nie udało się zapisać klucza"), tr(L"Sprawdź klucz i dostęp do katalogu konfiguracji."));
                return;
            }
            clearSecretInput();
            settings_state_.editing = false;
            buildSettingsRows();
        } else {
            clearSecretInput();
            settings_secret_input_ = row.id == Id::steam_key ? userSecret(L"STEAM_WEB_API_KEY", L"steam_web_api_key.txt") :
                                     row.id == Id::rawg_key ? userSecret(L"RAWG_API_KEY", L"rawg_api_key.txt") :
                                     row.id == Id::sgdb_key ? userSecret(L"STEAMGRIDDB_API_KEY", L"steamgriddb_api_key.txt") :
                                     row.id == Id::thegamesdb_key ? userSecret(L"THEGAMESDB_API_KEY", L"thegamesdb_api_key.txt") : std::wstring{};
            settings_secret_selected_ = true;
            settings_state_.editing = true;
        }
    } else if (row.control == Control::choice) {
        if (!settings_state_.editing) { settings_state_.choice = row.choice; settings_state_.editing = true; }
        else if (row.choices[settings_state_.choice].custom) { openColorEditor(row); return; } // "Inny…".
        else settingsChoose(row);
    } else if (row.control == Control::slider) settings_state_.editing = !settings_state_.editing;
    else if (row.control == Control::layout) setHomeLayout((row.choice + 1) % static_cast<int>(home_layouts.size()));
    else { settingsToggle(row); return; }
    audio_.context();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

// The choice picked from a row's list, applied at once.
void Launcher::settingsChoose(const Row& row) {
    // Interfejs → Widoki biblioteki, its choices in the view drawer's order (outside the chain below: C1061).
    if (row.id == Id::default_view_console || row.id == Id::default_view_collection || row.id == Id::default_view_studio) {
        auto& view = row.id == Id::default_view_console ? settings_.default_view_console :
                     row.id == Id::default_view_collection ? settings_.default_view_collection : settings_.default_view_studio;
        view = static_cast<int>(library_view::drawerView(settings_state_.choice));
        settings_state_.editing = false;
        settingsChanged();
        return;
    }
    if (settingsExtraChoose(row)) return; // 0.4.9's choices (settings_extras.cpp).
    const auto& choice = row.choices[settings_state_.choice];
    bool relabel = false;
    if (row.id == Id::font) settings_.font_family = choice.label;
    else if (row.id == Id::tile_shape)
        settings_.tile_shape = static_cast<AppSettings::TileShape>(std::clamp(settings_state_.choice, 0, 2));
    else if (row.id == Id::background_grid_style)
        settings_.background_grid_style = static_cast<AppSettings::GridStyle>(std::clamp(settings_state_.choice, 0, 2));
    else if (row.id == Id::accept_action)
        settings_.accept_action = static_cast<AppSettings::AcceptAction>(std::clamp(settings_state_.choice, 0, 1));
    else if (row.id == Id::hint_glyphs)
        settings_.hint_glyphs = static_cast<AppSettings::HintGlyphs>(std::clamp(settings_state_.choice, 0, static_cast<int>(hint_glyph_styles.size()) - 1));
    else if (row.id == Id::input_hints) { // Off, or on with these symbols.
        settings_.input_hints = settings_state_.choice > 0;
        if (settings_state_.choice > 0)
            settings_.hint_glyphs = static_cast<AppSettings::HintGlyphs>(std::clamp(settings_state_.choice - 1, 0, static_cast<int>(hint_glyph_styles.size()) - 1));
    }
    else if (row.id == Id::glow_motion) { // Still, pulsing, or the travelling outline: never both.
        settings_.glow_pulse = settings_state_.choice == 1;
        settings_.glow_trace = settings_state_.choice == 2;
    }
    else if (row.id == Id::game_fps) { // Off, the average, or with the 1% low.
        settings_.game_fps = settings_state_.choice > 0;
        if (settings_state_.choice > 0) settings_.game_fps_low = settings_state_.choice == 2;
        auto& pins = settings_.hud_pins; // Turning the widget on shows it over the game at once.
        const auto found = std::find(pins.begin(), pins.end(), "fps");
        if (settings_state_.choice > 0 && found == pins.end()) pins.push_back("fps");
        else if (settings_state_.choice == 0 && found != pins.end()) pins.erase(found);
    }
    else if (row.id == Id::trailer_space)
        settings_.trailer_space_mb = trailer_spaces[std::clamp<size_t>(settings_state_.choice, 0, trailer_spaces.size() - 1)];
    else if (row.id == Id::video_delay)
        settings_.video_delay = video_delays[std::clamp<size_t>(settings_state_.choice, 0, video_delays.size() - 1)];
    else if (row.id == Id::video_volume) {
        settings_.video_volume = std::clamp(settings_state_.choice, 0, static_cast<int>(video_volumes.size()) - 1);
        if (!video_silent_) {
            const double level = video_volume_levels[settings_.video_volume];
            video_player_.setVolume(level);
            if (video_player_.active()) audio_.duckMusic(level > 0); // Muted: the menu music comes back.
        }
    }
    else if (row.id == Id::language) {
        settings_.language = static_cast<AppSettings::LanguageChoice>(std::clamp(settings_state_.choice, 0, 4));
        relabel = applyLanguage();
    }
    else if (row.id == Id::music_theme) {
        // Built-in themes first, then the sound folder's files.
        const int builtin = static_cast<int>(music_themes.size());
        if (settings_state_.choice < builtin) { settings_.music_theme = settings_state_.choice; settings_.music_file.clear(); }
        else if (static_cast<size_t>(settings_state_.choice - builtin) < sound_files_.size())
            settings_.music_file = sound_files_[static_cast<size_t>(settings_state_.choice - builtin)];
        requestMusic();
    }
    else if (row.id == Id::ui_sound) {
        settings_.ui_sound = std::clamp(settings_state_.choice, 0, static_cast<int>(ui_sounds.size()) - 1);
        requestSound(SoundCue::navigation, settings_.ui_sound, true);
    }
    else if (row.id == Id::toast_sound) {
        settings_.toast_sound = std::clamp(settings_state_.choice, 0, static_cast<int>(toast_sounds.size()) - 1);
        requestSound(SoundCue::toast, settings_.toast_sound, true);
    }
    else if (row.id == Id::boot_sound) {
        const int builtin = static_cast<int>(boot_sounds.size());
        if (settings_state_.choice < builtin) { settings_.boot_sound = settings_state_.choice; settings_.boot_file.clear(); }
        else if (static_cast<size_t>(settings_state_.choice - builtin) < sound_files_.size())
            settings_.boot_file = sound_files_[static_cast<size_t>(settings_state_.choice - builtin)];
        requestSound(SoundCue::boot, settings_.boot_sound, true);
    }
    else if (row.id == Id::screensaver_style) {
        settings_.screensaver_style = static_cast<AppSettings::ScreensaverStyle>(std::clamp(settings_state_.choice, 0, 2));
        prefetchPosters(); // Boxes: vertical posters download in the background.
    }
    else if (row.id == Id::screensaver_minutes)
        settings_.screensaver_minutes = screensaver::minute_choices[std::clamp<size_t>(settings_state_.choice, 0, screensaver::minute_choices.size() - 1)];
    else if (row.id == Id::ui_accent) settings_.ui_accent_rgb = choice.color;
    else if (row.id == Id::button_color) settings_.button_rgb = choice.color;
    else if (row.id == Id::button_idle)
        settings_.button_idle = static_cast<AppSettings::ButtonIdle>(std::clamp(settings_state_.choice, 0, static_cast<int>(button_idles.size()) - 1));
    else if (row.id == Id::hint_style)
        settings_.hint_style = static_cast<AppSettings::HintStyle>(std::clamp(settings_state_.choice, 0, static_cast<int>(hint_styles.size()) - 1));
    else if (row.id == Id::tile_entrance)
        settings_.tile_entrance = static_cast<AppSettings::TileEntrance>(std::clamp(settings_state_.choice, 0, static_cast<int>(tile_entrances.size()) - 1));
    else if (row.id == Id::focus_gap)
        settings_.focus_gap = static_cast<AppSettings::FocusGap>(std::clamp(settings_state_.choice, 0, static_cast<int>(focus_gaps.size()) - 1));
    else if (row.id == Id::search_art)
        settings_.search_art = static_cast<AppSettings::SearchArt>(std::clamp(settings_state_.choice, 0, static_cast<int>(search_arts.size()) - 1));
    else settings_.cover_accent_rgb = choice.color;
    settings_state_.editing = false;
    // A new language rebuilds shelves (titles, tiles) and with them these rows.
    settingsChanged(relabel);
}

// A row's badge button (Row::reset): its value back to the default (a game's option, compat_activate.cpp).
bool Launcher::settingsResetRow(const Row& row) {
    if (row.reset.empty()) return false;
    // Interfejs → Widoki biblioteki: every library of the kind follows the default view again.
    if (row.reset.starts_with("views:")) { resetLibraryViews(std::string_view(row.reset).substr(6)); return true; }
#ifndef _WIN32
    return compatResetOption(row);
#else
    return false;
#endif
}
// Delete and the pad's X on the focused row.
bool Launcher::settingsResetFocused() {
    if (!settingsOpen() || !settings_state_.options || settings_state_.editing || editingSecret() || settings_rows_.empty()) return false;
    const auto row = settings_rows_[static_cast<size_t>(settings_state_.row())];
    // An own collection: Delete and X ask whether to delete it (its trash).
    if (row.id == Id::collection && row.removable) { askDeleteCollection(row.key); return true; }
    return settingsResetRow(row);
}

// A switch (or a collection's visibility). Most rebuild the shelves; visual-only switches and
// those read later just save and repaint. Unknown rows do nothing.
void Launcher::settingsToggle(const Row& row) {
    // Outside the else-if chain below (MSVC's nesting limit): read after the next game.
    if (row.id == Id::save_backups) { settings_.save_backup_after_game = !settings_.save_backup_after_game; settingsChanged(); audio_.context(); return; }
    if (settingsExtraToggle(row)) return; // 0.4.9's switches (settings_extras.cpp).
    if (row.id == Id::uninstalled) settings_.show_uninstalled = !settings_.show_uninstalled;
    else if (row.id == Id::recent_uninstalled) settings_.recent_uninstalled = !settings_.recent_uninstalled;
    else if (row.id == Id::emulation) settings_.show_emulation = !settings_.show_emulation;
    else if (row.id == Id::show_private) {
        // The split into public and private games comes from the scan: onLibrary applies the setting
        // to the fresh snapshot of the same account and keeps this screen.
        settings_.show_private = !settings_.show_private;
        steam_rescan_->request();
    }
    else if (row.id == Id::show_categories) {
        settings_.show_categories = !settings_.show_categories;
        category_rail_focus_ = false;
        closeCategoryMenu(); category_menu_progress_ = 0;
        settingsChanged(); audio_.context(); return;
    }
    else if (row.id == Id::shelves_posters) {
        settings_.shelves_posters = !settings_.shelves_posters;
        prefetchPosters();
        settingsChanged(); audio_.context(); return;
    }
    else if (row.id == Id::xmb_posters) {
        settings_.xmb_posters = !settings_.xmb_posters;
        prefetchPosters();
        settingsChanged(); audio_.context(); return;
    }
    else if (row.id == Id::quick_resume) {
        settings_.quick_resume = !settings_.quick_resume;
        // Without the feature there is no menu to resume from: never leave a game suspended.
        if (!settings_.quick_resume) resumeFrozenGames();
    } else if (row.id == Id::guide_menu) settings_.guide_menu = !settings_.guide_menu;
    else if (row.id == Id::guide_overlay) {
        settings_.guide_overlay = !settings_.guide_overlay;
        if (settings_.guide_overlay) settings_.guide_menu = true;
    }
    else if (row.id == Id::guide_open_widgets) settings_.guide_open_widgets = !settings_.guide_open_widgets;
    else if (row.id == Id::game_fps) settings_.game_fps = !settings_.game_fps;
    else if (row.id == Id::game_fps_low) settings_.game_fps_low = !settings_.game_fps_low;
    else if (row.id == Id::steam_controller) {
        settings_.steam_controller = !settings_.steam_controller;
        controller_input_.steamController(settings_.steam_controller);
    }
    else if (row.id == Id::focus_spring) {
        settings_.focus_spring = !settings_.focus_spring;
        settingsChanged(); audio_.context(); return; // Visual only: no shelf rebuild.
    }
    else if (row.id == Id::shelves_hero_facts) {
        settings_.shelves_hero_facts = !settings_.shelves_hero_facts;
        settingsChanged(); audio_.context(); return; // Visual only: no shelf rebuild.
    }
    else if (row.id == Id::steam_silent_launch || row.id == Id::steam_account_switch || row.id == Id::steam_install_dialog ||
             row.id == Id::hide_while_playing || row.id == Id::steam_hide_launch_window) {
        auto& flag = row.id == Id::steam_silent_launch ? settings_.steam_silent_launch :
                     row.id == Id::steam_hide_launch_window ? settings_.steam_hide_launch_window :
                     row.id == Id::steam_account_switch ? settings_.steam_account_switch :
                     row.id == Id::hide_while_playing ? settings_.hide_while_playing : settings_.steam_install_dialog;
        flag = !flag;
        settingsChanged(); audio_.context(); return; // Read at the next launch or install: no shelf rebuild.
    }
    else if (row.id == Id::flix_category_all) settings_.flix_category_all = !settings_.flix_category_all; // Rebuild: the rows change.
    else if (row.id == Id::shelves_category_all) settings_.shelves_category_all = !settings_.shelves_category_all; // Rebuild: the shelves change.
    else if (row.id == Id::panel_borders) {
        settings_.panel_borders = !settings_.panel_borders;
        settingsChanged(); audio_.context(); return; // Visual only: no shelf rebuild.
    }
    else if (row.id == Id::desktop_platform_badges || row.id == Id::desktop_gray_uninstalled) {
        auto& value = row.id == Id::desktop_platform_badges ? settings_.desktop_platform_badges : settings_.desktop_gray_uninstalled;
        value = !value;
        settingsChanged(); audio_.context(); return; // Visual only: the posters redraw.
    }
    else if (row.id == Id::launch_tv_mode) {
        settings_.launch_tv_mode = !settings_.launch_tv_mode;
        settingsChanged(); audio_.context(); return; // Read at the next start; the mode now stays.
    }
    else if (row.id == Id::screen_transitions) {
        settings_.screen_transitions = !settings_.screen_transitions;
        if (!settings_.screen_transitions) screen_transition_ = 1.0f;
        settingsChanged(); audio_.context(); return; // Visual only: no shelf rebuild.
    }
    else if (row.id == Id::glow_pulse) {
        settings_.glow_pulse = !settings_.glow_pulse;
        if (settings_.glow_pulse) settings_.glow_trace = false;
        settingsChanged(); audio_.context(); return; // Visual only: no shelf rebuild.
    }
    else if (row.id == Id::glow_trace) {
        settings_.glow_trace = !settings_.glow_trace;
        if (settings_.glow_trace) settings_.glow_pulse = false;
        settingsChanged(); audio_.context(); return; // Visual only: no shelf rebuild.
    }
    else if (row.id == Id::metro_3d) {
        settings_.metro_3d = !settings_.metro_3d;
        settingsChanged(); audio_.context(); return; // Visual only: the tiles ease to their new targets.
    }
    else if (row.id == Id::metro_category_tabs || row.id == Id::flix_category_tabs || row.id == Id::psfive_category_tabs) {
        auto& value = row.id == Id::metro_category_tabs ? settings_.metro_category_tabs :
                      row.id == Id::flix_category_tabs ? settings_.flix_category_tabs : settings_.psfive_category_tabs;
        value = !value;
        settingsChanged(); audio_.context(); return; // Visual only: the strip or Metro's tabs at the top.
    }
    else if (row.id == Id::psfive_clock) {
        settings_.psfive_clock = !settings_.psfive_clock;
        settingsChanged(); audio_.context(); return; // Visual only: the icons take the clock's place.
    }
    else if (row.id == Id::steam_trailers || row.id == Id::emulation_rom_art) {
        auto& flag = row.id == Id::steam_trailers ? settings_.steam_trailers : settings_.emulation_rom_art;
        flag = !flag;
        jobs_->rom_art = settings_.emulation_rom_art; // Artwork loaded from now on; F5 reloads a game.
        settingsChanged(); audio_.context(); return;
    }
    else if (row.id == Id::emulation_names) {
        settings_.emulation_names = !settings_.emulation_names;
        applyEmulationNames(); // Shelves rebuilt with the chosen names.
        settingsChanged(); audio_.context(); return;
    }
    else if (row.id == Id::game_videos) {
        settings_.game_videos = !settings_.game_videos;
        settingsChanged(); audio_.context(); return; // tickVideo stops or starts on its next frame.
    }
    else if (row.id == Id::console_colors) {
        settings_.console_colors = !settings_.console_colors;
        settingsChanged(); audio_.context(); return; // Visual only: no shelf rebuild.
    }
    else if (row.id == Id::screensaver) settings_.screensaver = !settings_.screensaver; // Shelves rebuild: poster prefetch.
    else if (row.id == Id::input_hints) {
        settings_.input_hints = !settings_.input_hints;
        settingsChanged(); audio_.context(); return; // Visual only.
    }
    else if (row.id == Id::glance) {
        settings_.glance = !settings_.glance;
        if (!settings_.glance) { glance_cancel_->store(true); glance_pending_ = false; }
        settingsChanged(homeWidgets() == home_widgets::Placement::tiles); audio_.context(); return;
    }
    else if (row.id == Id::tarot) {
        settings_.tarot = !settings_.tarot;
        if (!settings_.tarot) closeTarot();
        settingsChanged(); audio_.context(); return; // The desktop top bar reads it on its next frame.
    }
    else if (row.id == Id::update_check) {
        settings_.update_check = !settings_.update_check;
        if (settings_.update_check) checkForUpdate(true);
        settingsChanged(); audio_.context(); return;
    }
    else if (row.id == Id::low_memory) {
        settings_.low_memory = !settings_.low_memory;
        applyLowMemory(); // Takes effect for artwork decoded from now on (src/app/memory.cpp).
        settingsChanged(); audio_.context(); return;
    }
    else if (row.id == Id::texture_compression) {
        settings_.texture_compression = !settings_.texture_compression;
        applyLowMemory();
        settingsChanged(); audio_.context(); return;
    }
    else if (row.id == Id::collection && !row.key.empty()) {
        if (row.locked) { navigationBoundary(); return; } // SRM collection covered by Emulacja.
        // The row's key is the collection's (the page starts with "Zarządzaj kolekcjami").
        if (!settings_.hidden_collections.erase(row.key)) settings_.hidden_collections.insert(row.key);
    }
    else return;
    settingsChanged(true);
    audio_.context();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::settingsClick(float x, float y) {
    const auto view = renderer_.viewport();
    const auto layout = Layout::of(view.width, view.height, settings_state_, settings_rows_);
    if (color_editor_.open) { colorEditorClick(x, y); return; }
    if (editingSecret()) {
        if (layout.import_help && layout.secretBrowse().contains(x, y)) browseCompanionImport();
        else if (layout.secretSave().contains(x, y)) { settings_secret_cancel_ = settings_secret_browse_ = false; settingsActivate(); }
        else if (layout.secretCancel().contains(x, y) || !layout.secretPanel().contains(x, y)) back();
        return;
    }
    if (settings_state_.editing && !settings_rows_.empty() && settings_rows_[settings_state_.row()].picker()) {
        const int count = static_cast<int>(settings_rows_[settings_state_.row()].pickList(settings_state_.tile).size());
        const int choice = layout.hitChoice(x, y, count, settings_state_.choice);
        if (choice >= 0) { settings_state_.choice = choice; settingsActivate(); }
        else if (!layout.picker(count).contains(x, y)) back();
        return;
    }
    if (layout.backButton().contains(x, y)) {
        settings_state_.options = settings_state_.editing = false;
        back();
        return;
    }
    if (settings_save_failed_ && layout.saveStatus().contains(x, y)) { saveSettings(); return; }
    const int category = layout.hitRail(x, y);
    if (category >= 0) {
        settings_state_.selectCategory(category);
        buildSettingsRows();
        settingsPreviewFocus();
        audio_.navigate();
        return;
    }
    const int index = layout.hitRow(x, y, static_cast<int>(settings_rows_.size()), settings_state_.offset());
    if (index < 0) return;
    const bool revealing = settings_rows_[index].dense && !(settings_state_.options && settings_state_.row() == index);
    settings_state_.options = true;
    settings_state_.editing = false;
    settings_state_.selected[settings_state_.category] = index;
    const auto& row = settings_rows_[index];
    // A badge button: back to the default. A dense row: its description first, then its control.
    if (!row.reset.empty() && !row.badge.empty() && layout.badge(index, settings_state_.offset(), row.control).contains(x, y)) {
        settingsResetRow(row);
        return;
    }
    if (revealing) {
        settings_state_.scroll[settings_state_.category] = Layout::of(view.width, view.height, settings_state_, settings_rows_)
            .ensureVisible(index, static_cast<int>(settings_rows_.size()), settings_state_.offset());
        audio_.navigate(); InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (row.control == Control::platform) {
        settings_state_.editing = (hasOptionsButton(row.id) ? layout.pluginCog(index, settings_state_.offset()) : layout.connect(index, settings_state_.offset())).contains(x, y);
        settingsActivate();
    } else if (row.control == Control::slider) {
        settings_state_.editing = true;
        if (layout.minus(index, settings_state_.offset()).contains(x, y)) settingsAdjust(-1);
        else if (layout.plus(index, settings_state_.offset()).contains(x, y)) settingsAdjust(1);
        else if (layout.track(index, settings_state_.offset()).contains(x, y)) {
            settings_dragging_ = true;
            SetCapture(hwnd_);
            mouseMove(x * view.scale, y * view.scale);
        }
    } else if (row.control == Control::collection) {
        settings_state_.subcontrol = row.removable && layout.trash(index, settings_state_.offset()).contains(x, y) ? 3 :
                                     layout.reorder(index, settings_state_.offset(), false).contains(x, y) ? 1 :
                                     layout.reorder(index, settings_state_.offset(), true).contains(x, y) ? 2 : 0;
        settingsActivate();
    } else if (row.control == Control::avatar) {
        const int count = companion::lookCount();
        const int first = settings_ui::Layout::avatarFirst(row.choice, count);
        for (int option = 0; option < count; ++option)
            if (layout.avatarOption(index, settings_state_.offset(), option, count, first).contains(x, y)) {
                if (companion::character(static_cast<companion::Look>(option))->soon) { navigationBoundary(); break; } // "Wkrótce".
                settings_.companion_look = std::string(companion::lookKey(static_cast<companion::Look>(option)));
                settingsChanged(); audio_.context();
            }
    } else if (row.control == Control::layout) {
        // The slider: a tile half out of view comes into it once chosen.
        const float first = layout.layoutFirstOf(index, row.choice);
        if (layout.layoutBand(index, settings_state_.offset()).contains(x, y))
            for (int option = 0; option < static_cast<int>(home_layouts.size()); ++option)
                if (layout.layoutOption(index, settings_state_.offset(), option, first).contains(x, y)) { setHomeLayout(option); audio_.context(); break; }
    } else if (tiled(row.control)) {
        const int count = static_cast<int>(row.tiles.size());
        const int tile = layout.hitTile(index, settings_state_.offset(), row.control, count, settings_state_.tile, x, y);
        if (tile >= 0) { settings_state_.tile = tile; settingsActivate(); }
    }
    else settingsActivate();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

bool Launcher::applyLanguage() {
    using LanguageChoice = AppSettings::LanguageChoice;
    const auto next = settings_.language == LanguageChoice::system ? i18n::systemLanguage() :
                      settings_.language == LanguageChoice::en ? i18n::Language::en :
                      settings_.language == LanguageChoice::pt_br ? i18n::Language::pt_br :
                      settings_.language == LanguageChoice::es ? i18n::Language::es : i18n::Language::pl;
    if (next == i18n::language()) return false;
    i18n::setLanguage(next);
    // Store descriptions are localized: the list view asks again in the new language.
    preview_.clear();
    // Stand-in names are stored in the game list that buildShelves() copies from.
    for (auto& game : uninstalled_library_) {
        if (i18n::matches(game.name, loading_game_name)) game.name = tr(loading_game_name);
        else if (i18n::matches(game.name, missing_game_name)) game.name = tr(missing_game_name);
    }
    return true;
}

void Launcher::setHomeLayout(int option) {
    settings_preview_.focus(std::clamp(option, 0, home_layout_count - 1), GetTickCount64());
    const auto layout = static_cast<AppSettings::HomeLayout>(std::clamp(option, 0, static_cast<int>(home_layouts.size()) - 1));
    if (settings_.home_layout == layout) return;
    // The console rows keep twelve games: rebuild Recent when entering or leaving them.
    const bool recent_count = layout == AppSettings::HomeLayout::psfive || settings_.home_layout == AppSettings::HomeLayout::psfive ||
                              layout == AppSettings::HomeLayout::snitch || settings_.home_layout == AppSettings::HomeLayout::snitch;
    settings_.home_layout = layout;
    category_rail_focus_ = false;
    closeCategoryMenu(); category_menu_progress_ = 0;
    xmb_column_ = 0; xmb_row_ = -1; xmb_rows_.clear();
    flix_row_ = 0;
    metro_page_ = 0; metro_scroll_ = metro_tab_ = 0; metro_tabs_ = false;
    psfive_action_ = psfive_top_ = psfive_details_ = -1;
    closeQuick(); quick_progress_ = 0; quick_circle_valid_ = false;
    // Metro pages keep a tile per category; positions from a row layout mean nothing there.
    if (layout == AppSettings::HomeLayout::metro) for (auto& category : categories_) category.selected = 0;
    category_ = 0;
    settingsChanged(recent_count);
    prefetchPosters(); // Queue artwork needed by the chosen layout; drop unused poster/capsule jobs.
}

void Launcher::settingsWheel(float mouse_x, int amount) {
    if (!settingsOpen() || customEditorOpen()) return;
    const auto view = renderer_.viewport();
    if (!settings_state_.editing)
        settings_state_.options = mouse_x / view.scale > Layout{view.width, view.height}.railRight() && !settings_rows_.empty();
    settingsMove(amount);
}
