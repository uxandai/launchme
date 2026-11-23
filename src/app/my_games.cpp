// User-added games ("Moje gry"): custom-games.json, the worker that checks which files
// exist, merging them into shelves/libraries, and the Settings → Library editor panel.
#include "launcher.hpp"
#include "launcher_internal.hpp"
#include "../artwork/cover_cache.hpp"
#include "../library/recent_games.hpp"
#include "../library/view_options.hpp"
#include "../platform/text_encoding.hpp"
#include "../library/emulation_scan.hpp"

#include <algorithm>
#include <cwctype>
#include <optional>

using custom_game_ui::Item;
using custom_game_ui::Poster;

namespace {
std::filesystem::path customGamesFile() { return userConfigDirectory() / L"custom-games.json"; }
// Posters matched for names typed in the editor, before any game holds them; emptied as it closes.
std::filesystem::path posterPreviewCache() { return coverCacheDirectory().parent_path() / L"artwork" / L"editor-preview"; }
constexpr uint64_t poster_delay_ms = 500; // The typed name rests this long before its poster is looked up.

constexpr size_t max_script = 1000; // Opcje uruchamiania → Skrypty: library-state.ini's fields (UTF-8 bytes, checked on save).
constexpr size_t field_limits[custom_game_ui::field_count]{
    custom_games::max_name, custom_games::max_path, custom_games::max_arguments, custom_games::max_path, custom_games::max_arguments, custom_games::max_arguments,
    custom_games::max_name, max_script, max_script, max_script};

} // namespace

// --- Library ----------------------------------------------------------------

void Launcher::loadCustomGames() {
    custom_games_ = custom_games::load(customGamesFile()).games;
    custom_installed_.clear();
    rebuildCustomLibrary();
    refreshCustomGames();
}

void Launcher::rebuildCustomLibrary() {
    custom_library_.clear();
    for (const auto& entry : custom_games_) {
        const auto found = custom_installed_.find(entry.id);
        auto game = custom_games::makeGame(entry, found == custom_installed_.end() || found->second); // No file: never installed.
        if (game.kind == GameKind::nonsteam && game.installed) {
            // Its ROM as makeGame resolved it (a Switch 2 dump's exefs\main); its own emulator unless a command is typed.
            game.play_action = emulationAction(entry.platform, game.rom.empty() ? entry.path : game.rom, entry.arguments);
            if (entry.arguments.empty()) if (auto own = gameEmulatorAction(game); !own.target.empty()) game.play_action = std::move(own);
        }
        custom_library_.push_back(std::move(game));
    }
}

// Files may live on removable or network drives: never touch them on the UI thread.
void Launcher::refreshCustomGames() {
    if (!runtime_started_ || custom_games_.empty()) return;
    const auto generation = custom_token_->fetch_add(1) + 1;
    std::vector<std::pair<std::string, std::filesystem::path>> files;
    for (const auto& entry : custom_games_) files.emplace_back(entry.id, entry.path);
    work_.start([token = custom_token_, alive = alive_, window = hwnd_, generation, files = std::move(files)] {
        auto message = std::make_unique<CustomGamesMessage>();
        message->generation = generation;
        for (const auto& [id, path] : files) {
            if (!alive->load() || token->load() != generation) return;
            std::error_code error;
            message->installed[id] = !path.empty() && std::filesystem::exists(path, error) && !error; // No file chosen: nothing to look for.
        }
        if (alive->load() && token->load() == generation &&
            PostMessageW(window, custom_games_message, 0, reinterpret_cast<LPARAM>(message.get()))) message.release();
    });
}

void Launcher::onCustomGames(std::unique_ptr<CustomGamesMessage> message) {
    if (message->generation != custom_token_->load()) return;
    bool changed = false;
    for (const auto& [id, installed] : message->installed) {
        const auto found = custom_installed_.find(id);
        changed |= (found == custom_installed_.end() || found->second) != installed;
        custom_installed_[id] = installed;
    }
    if (changed) applyCustomGames();
    // Once a session, after the files are known: the PC games added by hand that nothing names yet.
    if (!std::exchange(custom_identified_, true)) {
        std::vector<custom_games::Entry> present;
        for (const auto& entry : custom_games_)
            if (const auto found = custom_installed_.find(entry.id); found != custom_installed_.end() && found->second) present.push_back(entry);
        identifyCustomGames(present);
    }
}

// The definitions changed: rebuild shelves and the visible screen, keeping focus by identity.
void Launcher::applyCustomGames() {
    rebuildCustomLibrary();
    const auto selected_key = cards_.empty() ? std::string{} : library_view::selectionKey(cards_[selected_].game);
    const auto category_id = categories_.empty() ? std::string{} : categories_[category_].id;
    const auto custom = [](const Game& game) { return game.source == GameSource::custom && !game.source_id.empty() && isGame(game); };
    const auto current = [&](const Game& game) -> const Game* {
        const auto key = gameKey(game);
        const auto found = std::find_if(custom_library_.begin(), custom_library_.end(), [&](const Game& item) { return gameKey(item) == key; });
        return found == custom_library_.end() ? nullptr : &*found;
    };
    if (pending_launch_ && custom(pending_game_)) {
        if (const auto* next = current(pending_game_)) pending_game_ = *next;
        else { pending_launch_ = false; launch_progress_ = 0; }
    }
    closeEditorsFor([&](const Game& game) { return custom(game) && !current(game); });
    if (screen_ == ScreenMode::details && !detail_games_.empty() && custom(detail_games_[0])) {
        if (const auto* next = current(detail_games_[0])) {
            const auto played = detail_games_[0].last_played;
            const auto visibility = detail_games_[0].visibility; // Hidden in Launchme ("Ukryj") stays hidden.
            detail_games_[0] = *next; detail_games_[0].last_played = played; detail_games_[0].visibility = visibility;
            if (!cards_.empty()) cards_[0].game = detail_games_[0];
        } else { suspendDetails(); showCategory(0); }
    }
    buildShelves();
    const auto category = std::find_if(categories_.begin(), categories_.end(), [&](const Category& entry) { return entry.id == category_id; });
    if (category != categories_.end()) category_ = static_cast<size_t>(category - categories_.begin());
    if (screen_ == ScreenMode::library && pc_source_ == GameSource::drmfree && pc_library_.empty()) { showCategory(category_); return; }
    if (screen_ == ScreenMode::search) updateSearch();
    else if (screen_ == ScreenMode::shelves || screen_ == ScreenMode::library) rebuildCards();
    else { InvalidateRect(hwnd_, nullptr, FALSE); return; }
    if (homeUntouched()) focusCard(0, false);
    else if (const auto found = findCard(selected_key, true)) focusCard(*found, library_controls_.focus < 0);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::forgetCustomArtwork(const Game& game, bool selections) {
    removeGameArtwork(game, coverCacheDirectory(), selections);
    art_.erase(gameKey(game));
    // Every kind the folder held, the posters of the desktop grid and the poster layouts included.
    for (const auto kind : {CoverMessage::Kind::cover, CoverMessage::Kind::logo, CoverMessage::Kind::hero,
                            CoverMessage::Kind::poster, CoverMessage::Kind::landscape}) invalidateArtwork(game, kind);
}

// --- Editor -----------------------------------------------------------------

void Launcher::openCustomEditor(const std::string& id, std::optional<Item> focus) {
    if (custom_editor_.open) closeCustomEditor(); // Another game's panel (its preview and a new game's identification go).
    custom_game_ui::State ui;
    if (!id.empty()) {
        const auto found = std::find_if(custom_games_.begin(), custom_games_.end(), [&](const auto& entry) { return entry.id == id; });
        if (found == custom_games_.end()) return;
        ui.id = id;
        ui.fields = {found->name, found->path.wstring(), found->arguments, found->directory.wstring()};
        ui.fields[custom_game_ui::platform_field] = found->platform;
        ui.art_name = found->name; // Its artwork was matched by this name.
        compatEditorFill(ui, custom_games::makeGame(*found, true)); // Linux, a Windows game: its Wine/Proton fields.
        customEditorFlags(ui, custom_games::makeGame(*found, true)); // Its options (emulator, logs, HDR) and scripts.
    } else ui.pending_id = custom_games::newId(custom_games_); // Zidentyfikuj grę keys a new game by it before it is saved.
    ui.open = true;
    ui.desktop = desktop_mode_;
    // A new game starts at "Wskaż plik" (the file row's "Wybierz" in the desktop window): one press picks the file, the next one saves it.
    ui.focus = focus ? *focus : id.empty() ? (ui.desktop ? Item::file_browse : Item::pick_file) : Item::name;
    if (ui.focus == Item::file_browse && !ui.browses(custom_game_ui::file_field)) ui.focus = Item::pick_file;
    const int index = custom_game_ui::field(ui.focus);
    ui.caret = ui.fields[index >= 0 ? index : custom_game_ui::name_field].size();
    // The poster: at once for the name the panel opens with, then a moment after typing stops.
    custom_poster_ = {};
    custom_poster_name_ = custom_games::trim(ui.fields[custom_game_ui::name_field]);
    ++custom_poster_generation_;
    custom_poster_at_ = custom_poster_name_.empty() ? 0 : GetTickCount64();
    ui.poster = custom_poster_name_.empty() ? Poster::none : Poster::loading;
    const bool editing = ui.editing();
    custom_editor_ = std::move(ui);
    if (editing) if (const auto found = std::find_if(custom_games_.begin(), custom_games_.end(), [&](const auto& entry) { return entry.id == id; });
                     found != custom_games_.end()) modsPanelOpen(custom_games::makeGame(*found, true));
    // The Space/Enter that opened the panel already queued its WM_CHAR: it must not type.
    for (MSG typed{}; PeekMessageW(&typed, hwnd_, WM_CHAR, WM_CHAR, PM_REMOVE);) {}
    pollCustomEditorPoster();
    audio_.context();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

Game Launcher::customEditorGame() const {
    using namespace custom_game_ui;
    const auto& ui = custom_editor_;
    custom_games::Entry entry;
    entry.id = ui.gameId();
    entry.name = custom_games::trim(ui.fields[name_field]).substr(0, custom_games::max_name);
    entry.path = custom_games::cleanPath(ui.fields[file_field]);
    entry.arguments = custom_games::trim(ui.fields[arguments_field]);
    entry.directory = custom_games::cleanPath(ui.fields[directory_field]);
    entry.platform = ui.fields[platform_field];
    return custom_games::makeGame(entry, false); // Never resolves a shortcut here (makeGame does that only for found files).
}

void Launcher::pollCustomEditorPoster() {
    auto& ui = custom_editor_;
    if (!ui.open || !ui.posterShown()) return;
    const auto name = custom_games::trim(ui.fields[custom_game_ui::name_field]);
    const auto now = GetTickCount64();
    if (name != custom_poster_name_) {
        // Typed: the lookup waits until the name rests; an answer for an older name is dropped.
        custom_poster_name_ = name;
        ++custom_poster_generation_;
        custom_poster_at_ = name.empty() ? 0 : now + poster_delay_ms;
        if (name.empty()) { custom_poster_ = {}; ui.poster = Poster::none; }
        else ui.poster = Poster::loading; // The last poster stays, dimmed, until the next one comes.
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
    if (!custom_poster_at_ || now < custom_poster_at_) return;
    custom_poster_at_ = 0;
    // The game's own artwork while the name is the one it belongs to (the saved name, or the one it
    // was identified under); any other name is matched apart, so the game keeps its artwork until saved.
    auto game = customEditorGame();
    const bool own = !ui.gameId().empty() && !ui.art_name.empty() && name == ui.art_name;
    if (!own) {
        auto folded = name;
        std::transform(folded.begin(), folded.end(), folded.begin(), [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
        constexpr char hex[] = "0123456789abcdef";
        std::string id = "preview-";
        auto value = static_cast<uint64_t>(std::hash<std::wstring>{}(folded));
        for (int i = 0; i < 16; ++i, value >>= 4) id += hex[value & 15];
        game.source_id = std::move(id);
    }
    const auto generation = ++custom_poster_generation_;
    if (custom_poster_fetch_) { customEditorPosterFound(generation, name, custom_poster_fetch_(game, own)); return; } // Tests.
    if (!runtime_started_) { ui.poster = Poster::missing; return; }
    const auto deliver = [alive = alive_, window = hwnd_, generation, name](std::vector<uint8_t> bytes) {
        if (!alive->load()) return;
        auto message = std::make_unique<RemoteMessage>();
        message->run = [generation, name, bytes = std::move(bytes)](Launcher& app) mutable { app.customEditorPosterFound(generation, name, std::move(bytes)); };
        if (PostMessageW(window, remote_message, 0, reinterpret_cast<LPARAM>(message.get()))) message.release();
    };
    const bool started = work_.start([game, deliver, cache = own ? coverCacheDirectory() : posterPreviewCache()] {
        HttpBudget budget(20000);
        std::error_code ec;
        std::filesystem::create_directories(cache, ec);
        // SteamGridDB's 600×900 grid (or the Steam store's capsule) by title; a square cover stands in.
        auto bytes = artworkForGame(game, ArtworkKind::poster, cache, {});
        if (bytes.empty()) bytes = artworkForGame(game, ArtworkKind::cover, cache, {});
        deliver(std::move(bytes));
    }, [deliver](std::exception_ptr) { deliver({}); });
    if (!started) ui.poster = Poster::missing;
}

void Launcher::customEditorPosterFound(uint64_t generation, std::wstring name, std::vector<uint8_t> bytes) {
    auto& ui = custom_editor_;
    if (!ui.open || generation != custom_poster_generation_ || name != custom_poster_name_) return;
    custom_poster_ = bytes.empty() ? ImageBytes{} : shareImage(std::move(bytes));
    ui.poster = custom_poster_ ? Poster::shown : Poster::missing;
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::customEditorIdentify() {
    auto& ui = custom_editor_;
    if (!ui.open || !ui.posterShown()) return;
    const auto game = customEditorGame();
    if (game.name.empty()) {
        ui.error = tr(L"Wpisz nazwę gry (do 200 znaków).");
        customEditorFocus(Item::name);
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (game.source_id.empty()) { ui.error = tr(L"Nie udało się utworzyć identyfikatora gry."); InvalidateRect(hwnd_, nullptr, FALSE); return; }
    openIdentifyFor(game); // Over the panel; the panel waits beneath it.
}

void Launcher::customEditorIdentified(const std::string& key) {
    auto& ui = custom_editor_;
    if (!ui.open || !ui.posterShown() || ui.gameId().empty() || key != "custom:" + ui.gameId()) return;
    // The artwork the identification stored is the game's for the name it was identified under.
    ui.art_name = custom_games::trim(ui.fields[custom_game_ui::name_field]);
    if (!ui.editing()) ui.identified_new = true;
    custom_poster_name_ = ui.art_name;
    ++custom_poster_generation_;
    custom_poster_at_ = GetTickCount64();
    ui.poster = Poster::loading;
    pollCustomEditorPoster();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

// "Opcje uruchamiania…" in a game's menu: a user-added game opens its own editor; a DRM-free
// game edits its file, options and working directory; a Steam game its launch options (passed
// through steam.exe -applaunch, see finishLaunch). Other stores start through their clients'
// links, which take no options; they, and the games found in the ROM folders, get the switches,
// the scripts and the mods.
bool Launcher::launchEditable(const Game& game) const {
    if (launcherTile(game)) return false;
    if (game.source == GameSource::custom && !game.source_id.empty()) return true; // A user-added game, PC or emulated.
    if (game.source == GameSource::drmfree)
        return std::any_of(drm_catalog_.games.begin(), drm_catalog_.games.end(), [&](const auto& entry) { return entry.id == game.source_id; });
    if (game.source == GameSource::steam && game.kind == GameKind::steam && game.app_id) return true;
    // Every other game: its switches (logs, HDR) alone.
    return isGame(game) && game.kind != GameKind::catalog && !gameKey(game).empty();
}

void Launcher::openLaunchEditor(const Game& game) {
    if (!launchEditable(game)) return;
    if (game.source == GameSource::custom && !game.source_id.empty()) { openCustomEditor(game.source_id); return; }
    custom_game_ui::State ui;
    ui.title = game.name;
    if (game.source == GameSource::drmfree) {
        const auto entry = std::find_if(drm_catalog_.games.begin(), drm_catalog_.games.end(), [&](const auto& item) { return item.id == game.source_id; });
        ui.target = custom_game_ui::Target::drm_free;
        ui.key = entry->id;
        ui.fields = {entry->name, entry->executable.wstring(), entry->arguments, entry->working_directory.wstring()};
        ui.focus = Item::file;
        ui.caret = ui.fields[custom_game_ui::file_field].size();
        compatEditorFill(ui, game);
    } else if (!(game.source == GameSource::steam && game.kind == GameKind::steam && game.app_id)) {
        // A game of another store (or from a ROM folder): its client takes no options; the options (its
        // emulator, the switches), and on Linux its Windows build's Proton / Wine (WINEDLLOVERRIDES, the
        // options around %command%, the page).
        ui.target = custom_game_ui::Target::other;
        ui.key = gameKey(game);
        ui.focus = Item::option;
        compatEditorFill(ui, game);
        if (ui.compat) { ui.focus = Item::wine; ui.caret = ui.fields[custom_game_ui::wine_field].size(); }
    } else {
        ui.target = custom_game_ui::Target::steam;
        ui.key = gameKey(game);
        ui.fields[custom_game_ui::name_field] = game.name;
        if (const auto found = library_state_.launch_arguments.find(ui.key); found != library_state_.launch_arguments.end())
            ui.fields[custom_game_ui::arguments_field] = found->second;
        ui.focus = Item::arguments;
        ui.caret = ui.fields[custom_game_ui::arguments_field].size();
    }
    customEditorFlags(ui, game);
    ui.open = true;
    custom_editor_ = std::move(ui);
    modsPanelOpen(game);
    for (MSG typed{}; PeekMessageW(&typed, hwnd_, WM_CHAR, WM_CHAR, PM_REMOVE);) {} // The key that opened it must not type.
    audio_.context();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

std::wstring Launcher::launchArguments(const Game& game) const {
    if (game.source != GameSource::steam || game.kind != GameKind::steam) return {};
    const auto found = library_state_.launch_arguments.find(gameKey(game));
    return found == library_state_.launch_arguments.end() ? std::wstring{} : found->second;
}

void Launcher::closeCustomEditor() {
    const auto& ui = custom_editor_;
    if (ui.identified_new && !ui.pending_id.empty()) {
        // A new game identified, then not saved: what the identification stored under its key goes.
        auto game = customEditorGame();
        forgetCustomArtwork(game, true);
        rawg_metadata_.erase(gameKey(game));
        std::error_code ec;
        std::filesystem::remove(userConfigDirectory() / L"metadata" / (gameCacheKey(game) + ".rawg.json"), ec);
    }
    if (ui.posterShown() && ui.open && runtime_started_) // Posters matched for typed names are not kept.
        work_.start([cache = posterPreviewCache()] { std::error_code ec; std::filesystem::remove_all(cache, ec); });
    custom_poster_ = {}; custom_poster_name_.clear(); custom_poster_at_ = 0; ++custom_poster_generation_;
    ++script_test_generation_; // A test still running reports to no one.
    mods_.key.clear();         // An install still running finishes; its toast says how.
    custom_editor_ = {};
    mouse_transition_until_ = GetTickCount64() + GetDoubleClickTime();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::customEditorInsert(std::wstring_view text) {
    auto& ui = custom_editor_;
    const int index = custom_game_ui::field(ui.focus);
    if (!ui.open || index < 0 || index == custom_game_ui::platform_field) return;
    auto& value = ui.fields[index];
    if (ui.selected) { value.clear(); ui.caret = 0; ui.selected = false; }
    std::wstring clean; // One line: tabs become spaces, other control characters are dropped.
    for (const wchar_t ch : text) {
        if (ch == L'\t') clean += L' ';
        else if (ch >= L' ' && ch != 127) clean += ch;
    }
    const auto room = field_limits[index] > value.size() ? field_limits[index] - value.size() : 0;
    if (clean.size() > room) {
        clean.resize(room);
        if (!clean.empty() && IS_HIGH_SURROGATE(clean.back())) clean.pop_back();
    }
    ui.caret = std::min(ui.caret, value.size());
    value.insert(ui.caret, clean);
    ui.caret += clean.size();
    if (!clean.empty()) ui.error.clear();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::customEditorPaste() {
    if (!customEditorOpen() || custom_game_ui::field(custom_editor_.focus) < 0 || !OpenClipboard(hwnd_)) return;
    std::wstring text;
    if (const auto handle = GetClipboardData(CF_UNICODETEXT)) {
        if (const auto* data = static_cast<const wchar_t*>(GlobalLock(handle))) {
            const size_t capacity = std::min<size_t>(GlobalSize(handle) / sizeof(wchar_t), 8192);
            for (size_t i = 0; i < capacity && data[i]; ++i) text += data[i];
            GlobalUnlock(handle);
        }
    }
    CloseClipboard();
    customEditorInsert(text);
}

void Launcher::customEditorBrowse(bool folder) {
    using namespace custom_game_ui;
    auto& ui = custom_editor_;
    const std::filesystem::path file = custom_games::cleanPath(ui.fields[file_field]);
    const std::filesystem::path directory = custom_games::cleanPath(ui.fields[directory_field]);
    auto initial = folder && !directory.empty() ? directory : file.parent_path();
    if (!initial.is_absolute()) initial.clear();
    const auto apply = [this, folder](const std::filesystem::path& picked) {
        auto& ui = custom_editor_;
        if (!ui.open || picked.empty()) { InvalidateRect(hwnd_, nullptr, FALSE); return; }
        ui.error.clear();
        if (folder) { ui.fields[directory_field] = picked.wstring(); InvalidateRect(hwnd_, nullptr, FALSE); }
        else customEditorApplyFile(picked); // A program, a ROM or a game's folder: it decides what the game is (add_game.cpp).
    };
    if (pick_path_) { // Tests: an injected picker answers at once.
        const auto picked = pick_path_(hwnd_, folder, initial);
        mouse_transition_until_ = GetTickCount64() + GetDoubleClickTime();
        apply(picked);
        return;
    }
    // Shortcuts stay shortcuts: the list shows .lnk files themselves, never their targets.
    file_browser::Request request;
    request.mode = folder ? file_browser::Mode::folder : file_browser::Mode::file;
    request.title = folder ? tr(L"Wybierz folder roboczy") : tr(L"Wybierz plik gry");
    if (!folder) {
        // Programs and shortcuts (PC games), disc images and ROMs (emulated ones), or a game's folder.
        request.extensions = {L".exe", L".lnk", L".bat", L".cmd", L".url"};
        for (auto& extension : emulation_scan::romExtensions()) request.extensions.push_back(std::move(extension));
        request.folder_option = ui.editing() || ui.launchOptions(); // A new game has "Wskaż folder" / "Dodaj folder jako bibliotekę" of its own.
        request.filter_label = tr(L"Programy, skróty, obrazy płyt i ROM-y");
        request.all_files_option = true;
    }
    request.initial = folder || file.empty() || !file.is_absolute() ? initial : file;
    request.native = folder ? std::function<std::filesystem::path()>([this, initial] { return pickGamePath(hwnd_, true, initial); }) :
        std::function<std::filesystem::path()>([this, initial, extensions = request.extensions, label = request.filter_label] {
            return pickPath(hwnd_, false, tr(L"Wybierz plik gry"), extensions, label, true, initial); });
    openFileBrowser(std::move(request), apply);
}

void Launcher::customEditorSave() {
    using namespace custom_game_ui;
    auto& ui = custom_editor_;
    if (!ui.open) return;
    const auto fail = [&](std::wstring message, Item focus) {
        ui.error = std::move(message);
        customEditorFocus(focus);
        InvalidateRect(hwnd_, nullptr, FALSE);
    };
    if (!customEditorScriptsValid()) return;
    if (ui.launchOptions()) { launchEditorSave(); return; }
    // Linux, a Windows game: its Wine/Proton fields are checked before anything is saved.
    std::wstring compat_error;
    Item compat_field = Item::wine;
    if (!compatEditorApply(ui, false, compat_error, compat_field)) { fail(compat_error, compat_field); return; }
    const auto existing = std::find_if(custom_games_.begin(), custom_games_.end(), [&](const auto& entry) { return entry.id == ui.id; });
    if (ui.editing() && existing == custom_games_.end()) { closeCustomEditor(); return; } // Removed meanwhile.
    if (!ui.editing() && custom_games_.size() >= custom_games::max_games) { fail(tr(L"Osiągnięto limit gier: ") + std::to_wstring(custom_games::max_games), Item::save); return; }
    custom_games::Entry entry;
    // A new game keeps the ID it was opened with (its identification is stored under it).
    const bool pending_free = custom_games::validId(ui.pending_id) &&
        std::none_of(custom_games_.begin(), custom_games_.end(), [&](const auto& item) { return item.id == ui.pending_id; });
    entry.id = ui.editing() ? ui.id : pending_free ? ui.pending_id : custom_games::newId(custom_games_);
    entry.name = custom_games::trim(ui.fields[name_field]);
    entry.path = custom_games::cleanPath(ui.fields[file_field]);
    entry.arguments = custom_games::trim(ui.fields[arguments_field]);
    entry.directory = custom_games::cleanPath(ui.fields[directory_field]);
    entry.platform = ui.fields[platform_field];
    if (!entry.platform.empty()) entry.directory.clear(); // An emulated game: its emulator's own folder.
    if (entry.id.empty()) { fail(tr(L"Nie udało się utworzyć identyfikatora gry."), Item::save); return; }
    switch (custom_games::validate(entry)) {
    case custom_games::Problem::name: fail(tr(L"Wpisz nazwę gry (do 200 znaków)."), Item::name); return;
    case custom_games::Problem::path: fail(tr(L"Wybierz plik gry lub wpisz jego pełną ścieżkę."), Item::file); return;
    case custom_games::Problem::arguments: fail(tr(L"Opcje uruchamiania: jedna linia, do 2000 znaków."), Item::arguments); return;
    case custom_games::Problem::directory: fail(tr(L"Folder roboczy musi być pełną ścieżką."), Item::directory); return;
    default: break;
    }
    auto next = custom_games_;
    std::optional<Game> previous;
    if (existing != custom_games_.end()) {
        previous = custom_games::makeGame(*existing, true);
        next[static_cast<size_t>(existing - custom_games_.begin())] = entry;
    } else next.push_back(entry);
    if (!custom_games::save(next, customGamesFile())) { fail(tr(L"Nie udało się zapisać listy gier."), Item::save); return; }
    custom_games_ = std::move(next);
    // A new file is assumed present until the worker has checked it.
    if (!previous || previous->play_action.target != entry.path.wstring()) custom_installed_.erase(entry.id);
    // Artwork was matched by title (or identified as one): a new name is matched again (the user's picks stay).
    if (!ui.art_name.empty() && ui.art_name != entry.name) forgetCustomArtwork(previous ? *previous : custom_games::makeGame(entry, false), false);
    ui.identified_new = false; // Saved: its identification stays with it.
    compatEditorApply(ui, true, compat_error, compat_field);
    if (ui.flags) launchChoicesSave(ui.flags_key); // Its options and scripts.
    else if (ui.tabs()) setLaunchScripts(gameKey(custom_games::makeGame(entry, true)), customEditorScripts()); // A new game: the key it now has.
    closeCustomEditor();
    applyCustomGames();
    if (settingsOpen() && settings_state_.category == static_cast<int>(settings_ui::Category::library)) {
        // Added from the DRM-free page: its list of games added by hand opens, from its tile.
        if (settings_state_.platform() == GameSource::drmfree && !settings_state_.in(settings_ui::Page::custom_games) &&
            !settings_state_.in(settings_ui::Page::drm_games) && !settings_state_.in(settings_ui::Page::drm_roots)) {
            buildSettingsRows();
            const auto tile = std::find_if(settings_rows_.begin(), settings_rows_.end(), [](const settings_ui::Row& item) { return item.id == settings_ui::Id::custom_games; });
            if (tile != settings_rows_.end()) {
                settings_state_.selected[settings_state_.category] = static_cast<int>(tile - settings_rows_.begin());
                openSettingsPage(settings_ui::Page::custom_games);
            }
        }
        const auto row = std::find_if(settings_rows_.begin(), settings_rows_.end(), [&](const settings_ui::Row& item) {
            return item.id == settings_ui::Id::custom_game && item.key == entry.id;
        });
        if (row != settings_rows_.end()) {
            settings_state_.options = true;
            settings_state_.selected[settings_state_.category] = static_cast<int>(row - settings_rows_.begin());
            ensureSettingVisible();
        }
    }
    refreshCustomGames();
    // A new game, or another file: identified now (a game identified in the editor keeps that).
    if (!previous || previous->play_action.target != entry.path.wstring()) identifyCustomGames({entry});
    audio_.context();
}

void Launcher::launchEditorSave() {
    using namespace custom_game_ui;
    auto& ui = custom_editor_;
    const auto fail = [&](std::wstring message, Item focus) {
        ui.error = std::move(message);
        customEditorFocus(focus);
        InvalidateRect(hwnd_, nullptr, FALSE);
    };
    // A game of another store: its switches (and on Linux its Proton / Wine fields).
    if (ui.target == Target::other) {
        std::wstring compat_error;
        Item compat_field = Item::wine;
        if (!compatEditorApply(ui, false, compat_error, compat_field)) { fail(compat_error, compat_field); return; }
        compatEditorApply(ui, true, compat_error, compat_field);
        launchChoicesSave(ui.flags_key);
        closeCustomEditor();
        audio_.context();
        return;
    }
    // The fields are checked as a user-added game's are (one line of options, full paths).
    custom_games::Entry check;
    check.name = ui.title.empty() ? std::wstring(L"-") : ui.title.substr(0, custom_games::max_name);
    check.path = ui.target == Target::drm_free ? custom_games::cleanPath(ui.fields[file_field]) : std::wstring(L"C:\\steam");
    check.arguments = custom_games::trim(ui.fields[arguments_field]);
    if (ui.target == Target::drm_free) check.directory = custom_games::cleanPath(ui.fields[directory_field]);
    // A DRM-free game is its folder's file: unlike a game added by hand, it always has one.
    if (check.path.empty()) { fail(tr(L"Wybierz plik gry lub wpisz jego pełną ścieżkę."), Item::file); return; }
    std::wstring compat_error;
    Item compat_field = Item::wine;
    if (!compatEditorApply(ui, false, compat_error, compat_field)) { fail(compat_error, compat_field); return; }
    switch (custom_games::validate(check)) {
    case custom_games::Problem::path: fail(tr(L"Wybierz plik gry lub wpisz jego pełną ścieżkę."), Item::file); return;
    case custom_games::Problem::arguments: fail(tr(L"Opcje uruchamiania: jedna linia, do 2000 znaków."), Item::arguments); return;
    case custom_games::Problem::directory: fail(tr(L"Folder roboczy musi być pełną ścieżką."), Item::directory); return;
    default: break;
    }
    if (ui.target == Target::steam) {
        // Kept with the library's own state (library-state.ini), whose fields hold 1024 bytes.
        if (text_encoding::utf8(check.arguments).size() > 1000) { fail(tr(L"Opcje uruchamiania: jedna linia, do 1000 znaków."), Item::arguments); return; }
        if (check.arguments.empty()) library_state_.launch_arguments.erase(ui.key);
        else library_state_.launch_arguments[ui.key] = check.arguments;
        library_state_dirty_ = true;
        launchChoicesSave(ui.flags_key);
        saveLibraryPreferences();
        closeCustomEditor();
        audio_.context();
        return;
    }
    const auto entry = std::find_if(drm_catalog_.games.begin(), drm_catalog_.games.end(), [&](const auto& item) { return item.id == ui.key; });
    if (entry == drm_catalog_.games.end()) { closeCustomEditor(); return; } // Gone with a scan meanwhile.
    const auto previous = *entry;
    // The user's choice: scans keep it (with its options) until "Wykrywaj plik automatycznie".
    entry->executable = check.path;
    entry->arguments = check.arguments;
    entry->working_directory = check.directory;
    if (entry->executable != previous.executable || entry->arguments != previous.arguments || entry->working_directory != previous.working_directory) {
        entry->manual_executable = true;
        entry->missing = false;
    }
    if (!saveDrmFree()) { *entry = previous; fail(tr(L"Nie udało się zapisać katalogu DRM-free"), Item::save); return; }
    compatEditorApply(ui, true, compat_error, compat_field);
    launchChoicesSave(ui.flags_key);
    closeCustomEditor();
    applyDrmFree();
    if (settingsOpen()) buildSettingsRows();
    audio_.context();
}

// The scripts' tab as typed (trimmed; an empty field runs nothing).
LibraryState::LaunchScripts Launcher::customEditorScripts() const {
    const auto& ui = custom_editor_;
    return {custom_games::trim(ui.fields[custom_game_ui::before_field]), custom_games::trim(ui.fields[custom_game_ui::started_field]),
            custom_games::trim(ui.fields[custom_game_ui::exited_field])};
}

// Each script fits a library-state.ini field; one that does not shows its tab and field.
bool Launcher::customEditorScriptsValid() {
    auto& ui = custom_editor_;
    if (!ui.tabs()) return true;
    for (int index = custom_game_ui::before_field; index <= custom_game_ui::exited_field; ++index) {
        if (text_encoding::utf8(custom_games::trim(ui.fields[static_cast<size_t>(index)])).size() <= max_script) continue;
        ui.tab = custom_game_ui::Tab::scripts;
        ui.error = tr(L"Skrypt: jedna linia, do 1000 znaków.");
        customEditorFocus(custom_game_ui::fieldItem(index));
        InvalidateRect(hwnd_, nullptr, FALSE);
        return false;
    }
    return true;
}

void Launcher::customEditorRemove() {
    auto& ui = custom_editor_;
    const auto existing = std::find_if(custom_games_.begin(), custom_games_.end(), [&](const auto& entry) { return entry.id == ui.id; });
    if (!ui.editing() || existing == custom_games_.end()) { closeCustomEditor(); return; }
    if (!removeCustomGame(ui.id)) {
        ui.error = tr(L"Nie udało się zapisać listy gier.");
        ui.confirm_remove = false;
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

bool Launcher::removeCustomGame(const std::string& id) {
    const auto existing = std::find_if(custom_games_.begin(), custom_games_.end(), [&](const auto& entry) { return entry.id == id; });
    if (existing == custom_games_.end()) return true; // Gone already.
    auto next = custom_games_;
    next.erase(next.begin() + (existing - custom_games_.begin()));
    if (!custom_games::save(next, customGamesFile())) return false;
    const auto game = custom_games::makeGame(*existing, true);
    custom_games_ = std::move(next);
    custom_installed_.erase(game.source_id);
    removeShortcuts(game); // Its shortcuts outside Launchme would start nothing (game_links.cpp).
    forgetCustomArtwork(game, true);
    // Its ID is never reused, so its history would only linger.
    if (library_state_.first_seen.erase(gameKey(game)) + library_state_.last_played.erase(recent_games::historyKey(game, account_, gog_state_.account)) +
        library_state_.hidden.erase(gameKey(game)))
        library_state_dirty_ = true;
    if (custom_editor_.open && custom_editor_.id == id) closeCustomEditor();
    applyCustomGames();
    audio_.context();
    return true;
}

std::string Launcher::addCustomGame(custom_games::Entry entry) {
    entry.id = custom_games::newId(custom_games_);
    if (entry.id.empty() || custom_games::validate(entry) != custom_games::Problem::none) return {};
    auto next = custom_games_;
    next.push_back(entry);
    if (!custom_games::save(next, customGamesFile())) return {};
    custom_games_ = std::move(next);
    applyCustomGames();
    refreshCustomGames();
    return entry.id;
}

// "Usuń z biblioteki" in the game's menu (Tryb TV, its Details' "…" and the desktop window): only the
// entry goes, never the game's file; while it runs or is suspended it stays.
bool Launcher::canRemoveFromLibrary(const Game& game) const {
    return game.source == GameSource::custom && !game.source_id.empty() && !launcherTile(game) &&
           std::any_of(custom_games_.begin(), custom_games_.end(), [&](const auto& entry) { return entry.id == game.source_id; });
}

void Launcher::askRemoveFromLibrary(const Game& game) {
    if (!canRemoveFromLibrary(game) || sessionState(game) != SessionState::none) return;
    dismissMenu();
    remove_game_ = game;
    prompt_kind_ = PromptKind::remove;
    exit_prompt_ = 1; // "Opcje uruchamiania…" is the safe default; Usuń z biblioteki is one step up.
    audio_.context();
    InvalidateRect(hwnd_, nullptr, FALSE);
}
