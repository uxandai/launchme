// ScreenScraper and ES-DE in the launcher: the connection test and cache reset of Settings →
// ScreenScraper, ROM-matched names on emulated shortcuts, and reloading ES-DE's results when its
// gamelists change while the launcher runs.
#include "launcher.hpp"
#include "../library/esde_gamelist.hpp"
#include "../library/screenscraper.hpp"
#include "../net/http.hpp"

#include <algorithm>
#include <cwctype>

void Launcher::testScreenScraper() {
    if (screenscraper_testing_) return;
    screenscraper_testing_ = true;
    if (settingsOpen()) buildSettingsRows();
    audio_.context();
    InvalidateRect(hwnd_, nullptr, FALSE);
    work_.start([alive = alive_, hwnd = hwnd_] {
        namespace ss = screenscraper;
        auto message = std::make_unique<ScreenScraperTestMessage>();
        const auto config = ss::load();
        if (!config.ready()) message->text = tr(L"Wpisz devid i devpassword.");
        else {
            HttpBudget budget(15000);
            const auto account = ss::account(config);
            switch (account.status) {
            case ss::Status::found: {
                message->ok = true;
                message->text = tr(L"Działa");
                if (!account.user.empty()) message->text += L" · " + account.user;
                if (account.quota.max_per_day)
                    message->text += L" · " + std::wstring(tr(L"dziś")) + L" " + std::to_wstring(account.quota.requests_today) +
                                     L"/" + std::to_wstring(account.quota.max_per_day);
                if (account.threads) message->text += L" · " + std::wstring(tr(L"wątki")) + L": " + std::to_wstring(account.threads);
                // Working credentials end a pause a refusal started; a used-up quota keeps it.
                const auto metadata = userConfigDirectory() / L"metadata";
                if (account.quota.exhausted()) ss::pause(metadata, 12ull * 60 * 60 * 1000);
                else { std::error_code ec; std::filesystem::remove(metadata / L"screenscraper.cooldown", ec); }
                break;
            }
            case ss::Status::rejected: message->text = tr(L"Odrzucone: sprawdź devid, devpassword, softname, ssid i sspassword."); break;
            case ss::Status::quota: message->text = tr(L"Dzienny limit zapytań ScreenScraper wyczerpany. Spróbuję jutro."); break;
            case ss::Status::throttled:
            case ss::Status::closed: message->text = tr(L"ScreenScraper jest chwilowo niedostępny. Spróbuję później."); break;
            default:
                message->text = tr(L"Brak odpowiedzi ScreenScraper.");
                if (!account.error.empty()) message->text += L" " + account.error;
            }
        }
        if (alive->load() && PostMessageW(hwnd, screenscraper_test_message, 0, reinterpret_cast<LPARAM>(message.get()))) message.release();
    });
}

void Launcher::onScreenScraperTest(std::unique_ptr<ScreenScraperTestMessage> message) {
    screenscraper_testing_ = false;
    screenscraper_status_ = message->text;
    if (settingsOpen()) buildSettingsRows();
    showNotification(NotificationKind::launch, L"ScreenScraper", message->text);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::clearScreenScraper() {
    const auto metadata = userConfigDirectory() / L"metadata";
    size_t removed = 0;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(metadata, ec)) {
        const auto name = entry.path().filename().wstring();
        if (name.ends_with(L".screenscraper.json") && std::filesystem::remove(entry.path(), ec)) ++removed;
    }
    std::filesystem::remove(metadata / L"screenscraper.cooldown", ec);
    audio_.context();
    showNotification(NotificationKind::launch, L"ScreenScraper",
                     tr(L"Wyczyszczono zapamiętane identyfikacje: ") + std::to_wstring(removed));
}

bool Launcher::renameShortcuts() {
    bool changed = false;
    for (auto& game : shortcut_library_) {
        const auto key = gameKey(game);
        const auto original = shortcut_original_names_.find(key);
        if (original == shortcut_original_names_.end()) continue;
        const auto named = settings_.emulation_names || manual_names_.contains(key) ? emulation_names_.find(key) : emulation_names_.end();
        const auto& name = named != emulation_names_.end() && !named->second.empty() ? named->second : original->second;
        if (game.name != name) { game.name = name; changed = true; }
    }
    return changed;
}

void Launcher::applyIdentifiedNames(std::vector<Game>& games) const {
    for (auto& game : games) {
        if (game.kind != GameKind::nonsteam || game.rom.empty()) continue;
        const auto key = gameKey(game);
        // The player's ScreenScraper pick, then their RAWG pick (an emulated game's RAWG identification is
        // always theirs), then an automatic ROM match when ROM titles are on.
        const auto named = emulation_names_.find(key);
        const bool has_name = named != emulation_names_.end() && !named->second.empty();
        if (has_name && manual_names_.contains(key)) { game.name = named->second; continue; }
        if (const auto rawg = rawg_metadata_.find(key); rawg != rawg_metadata_.end() && !rawg->second.name.empty()) { game.name = rawg->second.name; continue; }
        if (has_name && settings_.emulation_names) game.name = named->second;
    }
}

void Launcher::applyEmulationNames() {
    renameShortcuts(); // The ROM folder's own games take their titles in buildShelves (applyIdentifiedNames).
    buildShelves();
    refreshShownNames(); // Szczegóły, a system's games and the cards on screen in Tryb TV too.
    if (screen_ == ScreenMode::shelves || screen_ == ScreenMode::library) rebuildCards(true);
    else if (screen_ == ScreenMode::search) updateSearch();
}

void Launcher::watchEsde() {
    const auto now = std::chrono::steady_clock::now();
    if (!runtime_started_ || !ratings_started_ || now < esde_watch_next_) return;
    esde_watch_next_ = now + std::chrono::seconds(20);
    if (esde_watch_->running.exchange(true)) return;
    std::vector<Game> games;
    for (const auto& game : shortcut_library_) if (game.kind == GameKind::nonsteam && game.platform != L"PC") games.push_back(game);
    work_.start([watch = esde_watch_, games = std::move(games), alive = ratings_alive_, hwnd = hwnd_, generation = ratings_generation_] {
        struct Done { std::atomic_bool& flag; ~Done() { flag.store(false); } } done{watch->running};
        const auto root = esde::root();
        if (root.empty()) return;
        std::unordered_map<std::wstring, std::filesystem::file_time_type> times;
        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator(root / L"gamelists", ec)) {
            std::error_code item;
            if (!entry.is_directory(item)) continue;
            const auto modified = std::filesystem::last_write_time(entry.path() / L"gamelist.xml", item);
            if (item) continue;
            auto system = entry.path().filename().wstring();
            for (auto& ch : system) ch = static_cast<wchar_t>(std::towlower(ch));
            times[system] = modified;
        }
        std::vector<std::wstring> changed;
        {
            std::lock_guard lock(watch->mutex);
            // The first look only remembers: the startup pass has read everything already.
            if (watch->primed)
                for (const auto& [system, modified] : times)
                    if (const auto known = watch->times.find(system); known == watch->times.end() || known->second != modified) changed.push_back(system);
            watch->times = std::move(times);
            watch->primed = true;
        }
        if (changed.empty() || !alive->load()) return;
        esde::Library library(root / L"gamelists");
        const auto metadata = userConfigDirectory() / L"metadata";
        auto message = std::make_unique<RatingsMessage>();
        message->generation = generation;
        for (const auto& game : games) {
            const auto systems = esde::systems(game.platform, game.rom);
            if (std::none_of(systems.begin(), systems.end(), [&](const std::wstring& system) {
                    return std::find(changed.begin(), changed.end(), system) != changed.end(); })) continue;
            std::error_code missing;
            if (std::filesystem::exists(metadata / (gameCacheKey(game) + ".rawg.json"), missing)) continue;
            if (const auto picked = screenscraper::loadCached(game, metadata); picked && picked->manual) continue;
            const auto found = library.find(game.platform, game.rom, game.name);
            if (!found) continue;
            const auto key = gameKey(game);
            message->emulation[key] = found->entry.details;
            if (found->by_rom && !found->entry.name.empty()) message->names[key] = found->entry.name;
            // New ES-DE media: empty slots (and slots that failed before) load again.
            std::vector<ArtworkKind> empty;
            for (const auto kind : {ArtworkKind::cover, ArtworkKind::hero, ArtworkKind::logo})
                if (!cachedArtworkAvailable(game, kind, coverCacheDirectory())) empty.push_back(kind);
            if (!empty.empty()) message->artwork_refresh.emplace_back(game, std::move(empty));
        }
        if (message->emulation.empty() && message->artwork_refresh.empty()) return;
        if (alive->load() && PostMessageW(hwnd, ratings_message, 0, reinterpret_cast<LPARAM>(message.get()))) message.release();
    });
}
