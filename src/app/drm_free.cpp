// DRM-free folders ("Katalog gier DRM-free"): drm-free.json, the scan worker (manual, after
// startup and while the launcher idles), merging its results into the library, and the
// Settings → Platformy PC → DRM-free page (folders, scan, per-game corrections).
#include "launcher.hpp"
#include "../platform/unix_time.hpp"
#include "../artwork/cover_cache.hpp"
#include "../library/view_options.hpp"
#include "../platform/text_encoding.hpp"

#include <algorithm>
#include <unordered_map>

using namespace settings_ui;

namespace {
std::filesystem::path drmFreeFile() { return userConfigDirectory() / L"drm-free.json"; }
// Background scans wait for this long without keyboard, mouse or pad input.
constexpr auto idle_after = std::chrono::minutes(5);
constexpr ULONGLONG idle_interval = 30ULL * 60 * 1000;  // Between finished background scans.
constexpr ULONGLONG idle_retry = 60ULL * 1000;          // After input interrupted one.

std::wstring when(uint64_t seconds) {
    const time_t time = static_cast<time_t>(seconds); tm local{};
    if (localtime_s(&local, &time)) return {};
    wchar_t text[64]{}; wcsftime(text, std::size(text), L"%Y-%m-%d %H:%M", &local);
    return text;
}

bool playable(const drm_free::Entry& entry) { return !entry.ignored && !entry.executable.empty(); }

enum class GameChoice { identify, pick, automatic, ignore, restore, folder };
std::vector<std::pair<GameChoice, const wchar_t*>> gameChoices(const drm_free::Entry& entry) {
    std::vector<std::pair<GameChoice, const wchar_t*>> result;
    if (entry.ignored) result.emplace_back(GameChoice::restore, tr(L"Przywróć do biblioteki"));
    else {
        result.emplace_back(GameChoice::identify, tr(L"Dopasuj w RAWG…"));
        result.emplace_back(GameChoice::pick, tr(L"Zmień plik gry…"));
        if (entry.manual_executable) result.emplace_back(GameChoice::automatic, tr(L"Wykrywaj plik automatycznie"));
        result.emplace_back(GameChoice::ignore, tr(L"Ignoruj"));
    }
    result.emplace_back(GameChoice::folder, tr(L"Otwórz folder"));
    return result;
}
} // namespace

// --- Catalog and library -------------------------------------------------------------

void Launcher::loadDrmFree() {
    drm_catalog_ = drm_free::load(drmFreeFile()).catalog;
    rebuildDrmLibrary();
}

void Launcher::rebuildDrmLibrary() {
    drm_library_.clear();
    for (const auto& entry : drm_catalog_.games) if (playable(entry)) drm_library_.push_back(drm_free::makeGame(entry));
}

bool Launcher::saveDrmFree() {
    if (drm_free::save(drm_catalog_, drmFreeFile())) return true;
    showNotification(NotificationKind::error, tr(L"Nie udało się zapisać katalogu DRM-free"), drmFreeFile().wstring());
    return false;
}

// The catalog changed: rebuild shelves and the visible screen, keeping focus by identity.
void Launcher::applyDrmFree(const std::vector<std::string>& artwork_reset) {
    rebuildDrmLibrary();
    const auto current = [&](const Game& game) -> const Game* {
        const auto key = gameKey(game);
        const auto found = std::find_if(drm_library_.begin(), drm_library_.end(), [&](const Game& item) { return gameKey(item) == key; });
        return found == drm_library_.end() ? nullptr : &*found;
    };
    // The worker removed automatic artwork matched by the folder title (every kind, the desktop
    // grid's posters too): request it again.
    for (const auto& key : artwork_reset) {
        art_.erase(key);
        const auto found = std::find_if(drm_library_.begin(), drm_library_.end(), [&](const Game& item) { return gameKey(item) == key; });
        if (found != drm_library_.end())
            for (const auto kind : {CoverMessage::Kind::cover, CoverMessage::Kind::logo, CoverMessage::Kind::hero,
                                    CoverMessage::Kind::poster, CoverMessage::Kind::landscape}) invalidateArtwork(*found, kind);
    }
    const auto selected_key = cards_.empty() ? std::string{} : library_view::selectionKey(cards_[selected_].game);
    const auto category_id = categories_.empty() ? std::string{} : categories_[category_].id;
    const auto drm = [](const Game& game) { return game.source == GameSource::drmfree && isGame(game); };
    if (pending_launch_ && drm(pending_game_)) {
        if (const auto* next = current(pending_game_)) pending_game_ = *next;
        else { pending_launch_ = false; launch_progress_ = 0; }
    }
    closeEditorsFor([&](const Game& game) { return drm(game) && !current(game); });
    if (screen_ == ScreenMode::details && !detail_games_.empty() && drm(detail_games_[0])) {
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
    if (const auto found = findCard(selected_key, true)) focusCard(*found, library_controls_.focus < 0);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

// "Zidentyfikuj grę" on a DRM-free game: its name follows the user's choice from now on.
void Launcher::drmIdentified(const Game& game, const rawg::Metadata& metadata) {
    const auto entry = std::find_if(drm_catalog_.games.begin(), drm_catalog_.games.end(), [&](const auto& item) { return item.id == game.source_id; });
    if (entry == drm_catalog_.games.end()) return;
    entry->manual_identity = true;
    entry->rawg_id = metadata.id;
    if (!metadata.name.empty()) entry->name = metadata.name.substr(0, 200);
    if (metadata.steam_app_id) entry->steam_app_id = metadata.steam_app_id;
    entry->checked = unix_time::now();
    saveDrmFree();
    applyDrmFree();
}

// --- Scanning --------------------------------------------------------------------------

void Launcher::startDrmScan(bool manual, bool idle) {
    if (drm_catalog_.roots.empty()) return;
    if (drm_scanning_) {
        // One scan at a time: the request runs after it; a manual one also cuts an idle scan short.
        drm_pending_ = true; drm_pending_full_ |= manual;
        if (manual && drm_idle_scan_ && drm_stop_) drm_stop_->store(true);
        return;
    }
    const auto generation = ++drm_generation_;
    drm_scanning_ = true; drm_identifying_ = false; drm_idle_scan_ = idle; drm_progress_ = {};
    drm_stop_ = std::make_shared<std::atomic_bool>(false);
    drm_free::ScanOptions options;
    options.full = manual; options.gentle = !manual; options.now = unix_time::now();
    // Folders other sources already own are skipped (Steam libraries, store installs, "Moje gry").
    for (const auto* list : {&installed_library_, &private_category_.games, &store_library_, &store_private_library_, &custom_library_, &shortcut_library_})
        for (const auto& game : *list) {
            auto folder = game.install_directory.is_absolute() ? game.install_directory :
                          game.executable.is_absolute() ? game.executable.parent_path() : std::filesystem::path{};
            if (folder.empty() || folder == folder.root_path()) continue;
            const auto key = drm_free::folderKey(folder);
            const bool root = std::any_of(drm_catalog_.roots.begin(), drm_catalog_.roots.end(), [&](const auto& item) {
                return drm_free::folderKey(item) == key || drm_free::inside(item, folder);
            });
            if (!root) options.known.insert(key);
        }
    if (settingsOpen()) buildSettingsRows();
    if (!runtime_started_) return; // UI tests deliver DrmFreeMessage themselves.
    const auto post = [generation, alive = alive_, window = hwnd_](std::unique_ptr<DrmFreeMessage> message) {
        message->generation = generation;
        if (alive->load() && PostMessageW(window, drm_free_message, 0, reinterpret_cast<LPARAM>(message.get()))) message.release();
    };
    const auto failure = [post](std::exception_ptr) {
        auto message = std::make_unique<DrmFreeMessage>(); message->kind = DrmFreeMessage::Kind::finished; post(std::move(message));
    };
    if (!work_.start([post, manual, options = std::move(options), snapshot = drm_catalog_, stop = drm_stop_, alive = alive_] {
        // Background scans yield the disk and CPU to everything else (THREAD_MODE_BACKGROUND).
        if (!manual) SetThreadPriority(GetCurrentThread(), THREAD_MODE_BACKGROUND_BEGIN);
        struct Restore { bool active; ~Restore() { if (active) SetThreadPriority(GetCurrentThread(), THREAD_MODE_BACKGROUND_END); } } restore{!manual};
        const drm_free::Cancelled cancelled = [&] { return !alive->load() || stop->load(); };
        auto last = std::chrono::steady_clock::time_point{};
        auto catalog = drm_free::scan(snapshot, options, cancelled, [&](drm_free::Progress progress) {
            const auto now = std::chrono::steady_clock::now();
            if (now - last < std::chrono::milliseconds(250) && progress.done < progress.total) return;
            last = now;
            auto message = std::make_unique<DrmFreeMessage>(); message->progress = progress; post(std::move(message));
        });
        { auto message = std::make_unique<DrmFreeMessage>(); message->kind = DrmFreeMessage::Kind::scanned; message->catalog = catalog; post(std::move(message)); }
        if (!cancelled()) {
            std::unordered_map<std::string, uint32_t> identity;
            for (const auto& entry : catalog.games) identity.emplace(entry.id, entry.rawg_id);
            auto published = std::chrono::steady_clock::now();
            std::vector<Game> fresh; // Identified since the last message.
            const auto root = userConfigDirectory();
            const auto publish = [&](const drm_free::Catalog& current) {
                auto message = std::make_unique<DrmFreeMessage>();
                message->kind = DrmFreeMessage::Kind::identified; message->catalog = current;
                for (const auto& game : fresh) {
                    message->artwork_reset.push_back(gameKey(game));
                    if (auto metadata = rawg::load(game, root / L"metadata")) message->metadata.emplace(gameKey(game), std::move(*metadata));
                }
                fresh.clear(); published = std::chrono::steady_clock::now();
                post(std::move(message));
            };
            const auto changed = [&](const drm_free::Catalog& current) {
                for (const auto& entry : current.games) {
                    auto& known = identity[entry.id];
                    if (entry.rawg_id && entry.rawg_id != known) { known = entry.rawg_id; fresh.push_back(drm_free::makeGame(entry)); }
                }
                if (std::chrono::steady_clock::now() - published >= std::chrono::seconds(2)) publish(current);
            };
            drm_free::IdentifyBackend backend;
            backend.sgdb_key = userSecret(L"STEAMGRIDDB_API_KEY", L"steamgriddb_api_key.txt");
            drm_free::identify(catalog, userSecret(L"RAWG_API_KEY", L"rawg_api_key.txt"),
                               {root / L"metadata", root / L"ratings", coverCacheDirectory()}, cancelled, changed, backend);
            if (!cancelled()) publish(catalog);
        }
        auto message = std::make_unique<DrmFreeMessage>(); message->kind = DrmFreeMessage::Kind::finished; post(std::move(message));
    }, failure)) {
        drm_scanning_ = drm_idle_scan_ = false; drm_stop_.reset();
    }
}

void Launcher::pollDrmFree() {
    if (!runtime_started_ || drm_scanning_ || drm_catalog_.roots.empty()) return;
    const bool enabled = settings_.enabled_pc_sources.contains(sourceSlug(GameSource::drmfree));
    if (!drm_started_) {
        // After the first Steam and store scans, so their install folders are known and skipped.
        if (scanning_ || !pc_initial_ready_) return;
        drm_started_ = true;
        drm_next_idle_ = GetTickCount64() + idle_interval;
        if (enabled && settings_.drm_background) startDrmScan(false);
        return;
    }
    if (!enabled || !settings_.drm_background || GetTickCount64() < drm_next_idle_ || !game_sessions_.empty()) return;
    if (std::chrono::steady_clock::now() - last_input_ < idle_after) return;
    startDrmScan(false, true);
}

void Launcher::onDrmFree(std::unique_ptr<DrmFreeMessage> message) {
    if (message->generation != drm_generation_ || !drm_scanning_) return;
    const bool page = settingsOpen() && settings_state_.category == static_cast<int>(settings_ui::Category::library);
    switch (message->kind) {
    case DrmFreeMessage::Kind::progress:
        drm_progress_ = message->progress;
        if (page) buildSettingsRows();
        return;
    case DrmFreeMessage::Kind::scanned:
    case DrmFreeMessage::Kind::identified: {
        if (message->kind == DrmFreeMessage::Kind::scanned) drm_identifying_ = !drm_stop_ || !drm_stop_->load();
        // RAWG details of games identified in the background, as if loaded at startup.
        for (auto& [key, metadata] : message->metadata) rawg_metadata_[key] = std::move(metadata);
        if (!message->metadata.empty() && screen_ == ScreenMode::details && !detail_games_.empty())
            if (const auto found = rawg_metadata_.find(gameKey(detail_games_[0])); found != rawg_metadata_.end() && message->metadata.contains(found->first))
                details_ = found->second.details;
        auto merged = drm_free::merge(drm_catalog_, std::move(message->catalog));
        size_t added = 0;
        for (const auto& entry : merged.games)
            if (playable(entry) && std::none_of(drm_catalog_.games.begin(), drm_catalog_.games.end(), [&](const auto& old) { return old.id == entry.id; })) ++added;
        if (merged != drm_catalog_ || !message->artwork_reset.empty()) {
            const bool changed = merged != drm_catalog_;
            drm_catalog_ = std::move(merged);
            if (changed) saveDrmFree();
            applyDrmFree(message->artwork_reset);
        }
        if (added && settings_.enabled_pc_sources.contains(sourceSlug(GameSource::drmfree)))
            showNotification(NotificationKind::launch, tr(L"Nowe gry DRM-free"), tr(L"Dodano do biblioteki: ") + std::to_wstring(added));
        if (page) buildSettingsRows();
        return;
    }
    case DrmFreeMessage::Kind::finished: {
        const bool interrupted = drm_stop_ && drm_stop_->load();
        drm_scanning_ = drm_identifying_ = drm_idle_scan_ = false; drm_stop_.reset(); drm_progress_ = {};
        drm_next_idle_ = GetTickCount64() + (interrupted ? idle_retry : idle_interval);
        if (drm_pending_) {
            const bool full = drm_pending_full_;
            drm_pending_ = drm_pending_full_ = false;
            startDrmScan(full);
        }
        if (page) buildSettingsRows();
        return;
    }
    }
}

// --- Settings → Platformy PC -------------------------------------------------------------

std::wstring Launcher::drmSummary() const {
    if (drm_scanning_) return tr(L"Skanowanie…");
    if (drm_catalog_.roots.empty()) return tr(L"Dodaj katalogi z grami");
    const auto games = std::count_if(drm_catalog_.games.begin(), drm_catalog_.games.end(), playable);
    return tr(L"Katalogi: ") + std::to_wstring(drm_catalog_.roots.size()) + L" · " + tr(L"Gry: ") + std::to_wstring(games);
}

void Launcher::drmSettingsRows() {
    const auto add = [&](Row row) { row.source = GameSource::drmfree; settings_rows_.push_back(std::move(row)); };
    const auto games = std::count_if(drm_catalog_.games.begin(), drm_catalog_.games.end(), playable);
    std::vector<const drm_free::Entry*> list; // Found games, ignored ones last.
    for (const auto& entry : drm_catalog_.games) if (!entry.executable.empty() || entry.ignored) list.push_back(&entry);
    std::stable_sort(list.begin(), list.end(), [](const drm_free::Entry* a, const drm_free::Entry* b) {
        if (a->ignored != b->ignored) return b->ignored;
        return _wcsicmp(a->name.c_str(), b->name.c_str()) < 0;
    });
    if (settings_state_.at(Page::drm_games) && list.empty()) settings_state_.close(); // The last game went away.
    if (settings_state_.at(Page::drm_roots)) {
        // "Katalogi": adding one first, then each folder with its games and its removal (a second press confirms).
        add({Id::drm_add_root, Control::action, tr(L"Dodaj katalog"), tr(L"Każdy podfolder to jedna gra."), tr(L"Dodaj")});
        for (const auto& root : drm_catalog_.roots) {
            size_t count = 0, missing = 0;
            for (const auto& entry : drm_catalog_.games)
                if (playable(entry) && (drm_free::inside(entry.folder, root) || drm_free::folderKey(entry.folder) == drm_free::folderKey(root))) { ++count; missing += entry.missing; }
            auto description = tr(L"Gry: ") + std::to_wstring(count);
            if (count && missing == count) description += L" · " + std::wstring(tr(L"Katalog niedostępny"));
            Row row{Id::drm_root, Control::action, root.wstring(), std::move(description)};
            row.key = text_encoding::utf8(root.wstring());
            row.value = drm_confirm_ == row.key ? tr(L"Potwierdź") : tr(L"Usuń");
            add(std::move(row));
        }
        return;
    }
    if (settings_state_.at(Page::drm_games)) {
        // "Znalezione gry": thin rows, one per game, each with its options.
        for (const auto* entry : list) {
            std::wstring description;
            if (entry->ignored) description = tr(L"Ignorowana");
            else {
                description = entry->executable.lexically_relative(entry->folder).wstring();
                if (description.empty() || description.starts_with(L"..")) description = entry->executable.wstring();
                if (entry->missing) description = tr(L"Brak pliku") + (L" · " + description);
                if (entry->manual_executable) description += L" · " + std::wstring(tr(L"wybrany plik"));
                if (!entry->rawg_id && !entry->manual_identity && entry->checked) description += L" · " + std::wstring(tr(L"nie rozpoznano w RAWG"));
            }
            Row row{Id::drm_game, Control::choice, entry->name, std::move(description), tr(L"Opcje")};
            row.key = entry->id;
            row.choice = -1; // Actions, not a value: no check mark in the picker.
            for (const auto& [choice, label] : gameChoices(*entry)) row.choices.push_back({label});
            add(std::move(row));
        }
        return;
    }
    // The games added by hand live here too: adding one and their covers first.
    add({Id::custom_add, Control::action, tr(L"Dodaj grę"),
        tr(L"Plik programu, skrótu, ROM-u lub folder: gra, folder ROM-ów albo katalog z wieloma grami."), tr(L"Dodaj")});
#ifndef _WIN32
    // A Windows game from its installer: installed into a prefix of its own (compat_workers.cpp).
    add({Id::compat_install_game, Control::action, tr(L"Dodaj grę z instalatora"),
        tr(L"Instalator Windows (.exe, .msi) w nowym prefiksie; potem wskazujesz plik gry."), tr(L"Wybierz…")});
#endif
    // Their covers in one go: each title matched (SteamGridDB, else the Steam store) and only missing art fetched.
    if (!custom_games_.empty())
        add({Id::custom_covers, Control::action, tr(L"Okładki"),
            tr(L"Wyszukaj i pobierz brakujące okładki gier dodanych ręcznie (SteamGridDB, sklep Steam)."), tr(L"Szukaj")});
    std::wstring status;
    if (drm_scanning_ && drm_identifying_) status = tr(L"Rozpoznawanie gier w RAWG…");
    else if (drm_scanning_) status = drm_progress_.total ? tr(L"Skanowanie… ") + std::to_wstring(drm_progress_.done) + L" / " + std::to_wstring(drm_progress_.total) :
                                                         std::wstring(tr(L"Skanowanie…"));
    else if (drm_catalog_.roots.empty()) status = tr(L"Dodaj katalog, aby skanować.");
    else status = (drm_catalog_.scanned ? tr(L"Ostatni skan: ") + when(drm_catalog_.scanned) : std::wstring(tr(L"Jeszcze nie skanowano"))) +
                  L" · " + tr(L"Gry: ") + std::to_wstring(games);
    if (!drm_scanning_ && !drm_catalog_.roots.empty() && !storedUserSecret(L"rawg_api_key.txt"))
        status += L" · " + std::wstring(tr(L"bez klucza RAWG nazwy pochodzą z folderów"));
    add({Id::drm_scan, Control::action, tr(L"Skanowanie"), std::move(status), drm_scanning_ ? tr(L"Zatrzymaj") : tr(L"Skanuj teraz")});
    add({Id::drm_background, Control::toggle, tr(L"Skanowanie w tle"),
         tr(L"Po starcie i w bezczynności, z niskim priorytetem dysku. Nigdy podczas gry."), {}, settings_.drm_background});
    {
        // The scanned folders: one tile, the list (adding and removing them) is its own page.
        std::wstring folders;
        for (const auto& root : drm_catalog_.roots) {
            if (folders.size() > 160) { folders += L" …"; break; }
            const auto name = root.filename().wstring();
            folders += (folders.empty() ? L"" : L" · ") + (name.empty() ? root.wstring() : name);
        }
        if (folders.empty()) folders = tr(L"Dodaj katalog, w którym każdy podfolder to jedna gra.");
        add({Id::drm_roots, Control::count, tr(L"Katalogi"), std::move(folders), std::to_wstring(drm_catalog_.roots.size())});
    }
    {
        // The games added by hand as one total; the list is its own page (whether the folders are scanned or not).
        const auto missing = std::count_if(custom_games_.begin(), custom_games_.end(), [&](const auto& entry) {
            const auto found = custom_installed_.find(entry.id);
            return !entry.path.empty() && found != custom_installed_.end() && !found->second; // No file chosen is not missing.
        });
        std::wstring description = tr(L"Programy, skróty, skrypty i ROM-y spoza sklepów.");
        if (missing) description += L" · " + (tr(L"Brak pliku: ") + std::to_wstring(missing));
        Row custom{Id::custom_games, Control::count, tr(L"Gry dodane ręcznie"), std::move(description), std::to_wstring(custom_games_.size())};
        custom.section = tr(L"Gry");
        add(std::move(custom));
    }
    if (list.empty()) return;
    // One row for all found games: the total, then facts worth a look.
    size_t ignored = 0, unmatched = 0, missing = 0;
    for (const auto* entry : list) {
        if (entry->ignored) { ++ignored; continue; }
        missing += entry->missing;
        unmatched += !entry->rawg_id && !entry->manual_identity && entry->checked;
    }
    std::wstring facts;
    const auto fact = [&](const wchar_t* label, size_t count) {
        if (!count) return;
        if (!facts.empty()) facts += L" · ";
        facts += label + std::to_wstring(count);
    };
    fact(tr(L"Nierozpoznane w RAWG: "), unmatched); fact(tr(L"Brak pliku: "), missing); fact(tr(L"Ignorowane: "), ignored);
    add({Id::drm_games, Control::count, tr(L"Znalezione gry"), std::move(facts), std::to_wstring(games)});
}

// DRM-free → "Gry dodane ręcznie": each game with its editor (which also removes it); adding one
// when there is none yet.
void Launcher::customGamesSettingsRows() {
    if (custom_games_.empty())
        settings_rows_.push_back({Id::custom_add, Control::action, tr(L"Dodaj grę"),
            tr(L"Plik programu, skrótu, ROM-u lub folder: gra, folder ROM-ów albo katalog z wieloma grami."), tr(L"Dodaj")});
    for (const auto& entry : custom_games_) {
        std::wstring description = entry.path.empty() ? std::wstring(tr(L"Bez pliku gry")) : entry.path.filename().wstring();
        if (!entry.platform.empty()) description = entry.platform + L" · " + description;
        if (!entry.arguments.empty()) description += L"  " + entry.arguments;
        if (const auto found = custom_installed_.find(entry.id); !entry.path.empty() && found != custom_installed_.end() && !found->second)
            description = tr(L"Brak pliku") + (L" · " + description);
        Row row{Id::custom_game, Control::action, entry.name, std::move(description), tr(L"Edytuj")};
        row.key = entry.id;
        settings_rows_.push_back(std::move(row));
    }
}

void Launcher::drmAddRoot() {
    if (!pick_games_folder_) {
        file_browser::Request request;
        request.mode = file_browser::Mode::folder;
        request.title = tr(L"Wybierz katalog z grami DRM-free");
        request.native = [this] { return pickGamesFolder(hwnd_); };
        openFileBrowser(std::move(request), [this](const std::filesystem::path& picked) { drmAddRoot(picked); });
        return;
    }
    // Tests: an injected picker answers at once.
    const auto picked = pick_games_folder_(hwnd_);
    mouse_transition_until_ = GetTickCount64() + GetDoubleClickTime();
    if (picked.empty()) { InvalidateRect(hwnd_, nullptr, FALSE); return; }
    drmAddRoot(picked);
}

void Launcher::drmAddRoot(const std::filesystem::path& picked) {
    const auto refuse = [&](std::wstring reason) { showNotification(NotificationKind::error, std::move(reason), picked.wstring()); };
    if (!drm_free::validRoot(picked)) { refuse(tr(L"Nie można dodać tego katalogu")); return; }
    if (drm_catalog_.roots.size() >= drm_free::max_roots) {
        refuse(tr(L"Osiągnięto limit katalogów: ") + std::to_wstring(drm_free::max_roots)); return;
    }
    for (const auto& root : drm_catalog_.roots) {
        if (drm_free::folderKey(root) == drm_free::folderKey(picked)) { refuse(tr(L"Ten katalog jest już dodany")); return; }
        if (drm_free::inside(picked, root) || drm_free::inside(root, picked)) { refuse(tr(L"Katalogi nie mogą zawierać się nawzajem")); return; }
    }
    drm_catalog_.roots.push_back(picked);
    if (!saveDrmFree()) { drm_catalog_.roots.pop_back(); return; }
    if (settings_.enabled_pc_sources.insert(sourceSlug(GameSource::drmfree)).second) settingsChanged(true);
    startDrmScan(true);
    buildSettingsRows();
    audio_.context();
}

bool Launcher::drmSettingsActivate(const Row& row) {
    const auto refresh = [&] { saveDrmFree(); applyDrmFree(); buildSettingsRows(); audio_.context(); };
    switch (row.id) {
    case Id::drm_scan:
        if (drm_scanning_) { drm_pending_ = drm_pending_full_ = false; if (drm_stop_) drm_stop_->store(true); }
        else startDrmScan(true);
        buildSettingsRows(); audio_.context();
        return true;
    case Id::drm_background:
        settings_.drm_background = !settings_.drm_background;
        settingsChanged(); audio_.context();
        return true;
    case Id::drm_add_root:
        drmAddRoot();
        return true;
    case Id::drm_games:
        openSettingsPage(Page::drm_games);
        return true;
    case Id::drm_roots:
        drm_confirm_.clear();
        openSettingsPage(Page::drm_roots);
        return true;
    case Id::drm_root: {
        if (drm_confirm_ != row.key) { drm_confirm_ = row.key; buildSettingsRows(); audio_.context(); return true; }
        drm_confirm_.clear();
        const std::filesystem::path root = text_encoding::wide(row.key);
        const auto key = drm_free::folderKey(root);
        std::erase_if(drm_catalog_.roots, [&](const auto& item) { return drm_free::folderKey(item) == key; });
        std::erase_if(drm_catalog_.games, [&](const auto& entry) { return drm_free::inside(entry.folder, root) || drm_free::folderKey(entry.folder) == key; });
        refresh();
        return true;
    }
    case Id::drm_game: {
        const auto entry = std::find_if(drm_catalog_.games.begin(), drm_catalog_.games.end(), [&](const auto& item) { return item.id == row.key; });
        if (entry == drm_catalog_.games.end()) { settings_state_.editing = false; buildSettingsRows(); return true; }
        if (!settings_state_.editing) {
            settings_state_.choice = 0; settings_state_.editing = true;
            audio_.context(); InvalidateRect(hwnd_, nullptr, FALSE);
            return true;
        }
        const auto choices = gameChoices(*entry);
        const auto choice = choices[static_cast<size_t>(std::clamp(settings_state_.choice, 0, static_cast<int>(choices.size()) - 1))].first;
        settings_state_.editing = false;
        switch (choice) {
        case GameChoice::identify:
            // RAWG search over Settings; the chosen game's name, metadata and artwork follow.
            openIdentifyFor(drm_free::makeGame(*entry));
            return true;
        case GameChoice::pick: {
            // The catalog may change while the picker is open (a scan): find the game again by ID.
            const auto apply = [this, id = entry->id](const std::filesystem::path& picked) {
                const auto game = std::find_if(drm_catalog_.games.begin(), drm_catalog_.games.end(), [&](const auto& item) { return item.id == id; });
                if (picked.empty() || !picked.is_absolute() || game == drm_catalog_.games.end()) { InvalidateRect(hwnd_, nullptr, FALSE); return; }
                game->executable = picked; game->manual_executable = true;
                game->arguments.clear(); game->working_directory.clear(); game->missing = false;
                saveDrmFree(); applyDrmFree(); buildSettingsRows(); audio_.context();
            };
            if (pick_path_) { // Tests: an injected picker answers at once.
                const auto picked = pick_path_(hwnd_, false, entry->folder);
                mouse_transition_until_ = GetTickCount64() + GetDoubleClickTime();
                apply(picked);
                return true;
            }
            file_browser::Request request;
            request.title = tr(L"Wybierz plik gry");
            request.extensions = {L".exe", L".lnk", L".bat", L".cmd", L".url"};
            request.filter_label = tr(L"Programy i skróty");
            request.all_files_option = true;
            request.initial = entry->executable.empty() ? entry->folder : entry->executable;
            request.native = [this, folder = entry->folder] { return pickGamePath(hwnd_, false, folder); };
            openFileBrowser(std::move(request), apply);
            return true;
        }
        case GameChoice::automatic:
            entry->manual_executable = false; entry->folder_time = 0; // The next scan detects it again.
            refresh(); startDrmScan(false);
            return true;
        case GameChoice::ignore: entry->ignored = true; break;
        case GameChoice::restore:
            entry->ignored = false;
            if (entry->executable.empty()) { entry->folder_time = 0; refresh(); startDrmScan(false); return true; }
            break;
        case GameChoice::folder:
            if (execute_(hwnd_, GameAction{entry->folder.wstring(), {}, {}, ActionKind::open_client}) <= 32)
                showNotification(NotificationKind::error, tr(L"Nie można otworzyć folderu"), entry->folder.wstring());
            buildSettingsRows();
            return true;
        }
        refresh();
        return true;
    }
    default:
        return false;
    }
}
