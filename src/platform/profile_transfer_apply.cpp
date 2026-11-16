// Kopia i przenoszenie: a staged import put in place at the start of Launchme, and the merges of
// settings, library state, own games, DRM-free folders and the play journal (profile_transfer.hpp).
#include "profile_transfer.hpp"
#include "profile_transfer_internal.hpp"
#include "atomic_file.hpp"
#include "text_encoding.hpp"
#include "unicode_text.hpp"
#include "../library/custom_games.hpp"
#include "../library/drm_free.hpp"
#include "../library/play_journal.hpp"
#include "../net/json.hpp"
#include <algorithm>
#include <ctime>

namespace profile_transfer {
using namespace transfer_detail;

namespace {
std::wstring slashes(std::wstring text) {
    for (auto& c : text) if (c == L'\\') c = L'/';
    while (text.size() > 1 && text.back() == L'/') text.pop_back();
    return text;
}
bool present(const std::filesystem::path& path) {
    std::error_code ec;
    return !path.empty() && std::filesystem::exists(path, ec);
}
// The imported path moved by the rules; kept only when it exists here, else the local one stays.
std::filesystem::path movedOr(const std::filesystem::path& imported, const std::filesystem::path& local, const PathMap& paths) {
    if (imported.empty()) return local.empty() ? imported : local;
    const auto moved = mapPath(imported, paths);
    return present(moved) ? moved : local;
}
}

std::filesystem::path mapPath(const std::filesystem::path& path, const PathMap& paths) {
    if (path.empty()) return path;
    const auto text = slashes(path.wstring());
    const std::pair<std::wstring, std::wstring>* best = nullptr;
    size_t best_length = 0;
    for (const auto& rule : paths) {
        const auto from = slashes(rule.first);
        if (from.empty() || from.size() < best_length) continue;
        if (text.size() >= from.size() && unicode_text::startsWithIgnoreCase(text, from) &&
            (text.size() == from.size() || text[from.size()] == L'/' || from.back() == L'/')) { best = &rule; best_length = from.size(); }
    }
    if (!best) return path;
    auto rest = text.substr(best_length);
    std::wstring target = best->second;
    if (!rest.empty() && rest.front() == L'/' && !target.empty() && (target.back() == L'/' || target.back() == L'\\')) rest.erase(0, 1);
    // The separators of the place it moves to: Windows' on Windows and for a Windows target ("F:\Gry").
#ifdef _WIN32
    const bool backslashes = true;
#else
    const bool backslashes = target.find(L'\\') != std::wstring::npos && target.find(L'/') == std::wstring::npos;
#endif
    if (backslashes) {
        for (auto& c : rest) if (c == L'/') c = L'\\';
        if (!rest.empty() && rest.front() == L'\\' && !target.empty() && target.back() == L'/') target.back() = L'\\';
#ifndef _WIN32
        return std::filesystem::path(target + rest); // Not this system's path: lexically_normal would not understand it.
#endif
    }
    return std::filesystem::path(target + rest).lexically_normal();
}

AppSettings mergeSettings(const AppSettings& local, AppSettings imported, const PathMap& paths) {
    // This computer's own: its window, monitor, start with the system, first-run state and the picker's last folder.
    imported.desktop_window = local.desktop_window;
    imported.desktop_maximized = local.desktop_maximized;
    imported.tv_monitor = local.tv_monitor;
    imported.autostart = local.autostart;
    imported.pc_sources_detected = local.pc_sources_detected;
    imported.file_browser_folder = local.file_browser_folder;
    imported.tour_done = local.tour_done;
    imported.handheld_detected = local.handheld_detected;
    imported.first_start = local.first_start;
    imported.load_failed = false;
    // Folders and programs: moved by the rules; one that does not exist here keeps the local choice.
    imported.multimc_path = movedOr(imported.multimc_path, local.multimc_path, paths);
    imported.sound_folder = movedOr(imported.sound_folder, local.sound_folder, paths);
    imported.video_folder = imported.video_folder.empty() ? imported.video_folder : movedOr(imported.video_folder, local.video_folder, paths);
    imported.wallpaper_file = imported.wallpaper_file.empty() ? imported.wallpaper_file : movedOr(imported.wallpaper_file, local.wallpaper_file, paths);
    imported.emulation_roms = imported.emulation_roms.empty() ? imported.emulation_roms : movedOr(imported.emulation_roms, local.emulation_roms, paths);
    {
        std::vector<std::filesystem::path> extra;
        for (const auto& folder : imported.emulation_roms_extra)
            if (const auto moved = mapPath(folder, paths); present(moved)) extra.push_back(moved);
        imported.emulation_roms_extra = extra.empty() ? local.emulation_roms_extra : extra;
    }
    for (auto& [id, override] : imported.emulator_overrides) {
        if (override.program.empty()) continue;
        const auto found = local.emulator_overrides.find(id);
        override.program = movedOr(override.program, found != local.emulator_overrides.end() ? found->second.program : std::filesystem::path{}, paths);
    }
    for (auto it = imported.system_roms.begin(); it != imported.system_roms.end();) {
        const auto moved = mapPath(it->second, paths);
        if (present(moved)) { it->second = moved; ++it; }
        else if (const auto found = local.system_roms.find(it->first); found != local.system_roms.end()) { it->second = found->second; ++it; }
        else it = imported.system_roms.erase(it);
    }
    for (auto* folder : {&imported.companion_photo_folder, &imported.companion_feed_folder}) {
        const auto& own = folder == &imported.companion_photo_folder ? local.companion_photo_folder : local.companion_feed_folder;
        if (!folder->empty()) *folder = movedOr(*folder, own, paths).wstring();
    }
    return imported;
}

void mergeLibraryState(LibraryState& local, const LibraryState& imported, Mode mode) {
    if (mode == Mode::replace) {
        auto bookmark = local.bookmark;
        auto pending = local.steam_hidden_pending;
        local = imported;
        local.bookmark = bookmark;
        local.steam_hidden_pending = pending;
        return;
    }
    const auto missing = [](auto& to, const auto& from) { for (const auto& [key, value] : from) to.try_emplace(key, value); };
    const auto larger = [](auto& to, const auto& from) { for (const auto& [key, value] : from) { auto& own = to[key]; own = std::max(own, value); } };
    missing(local.options, imported.options);
    for (const auto& [key, value] : imported.first_seen) {
        const auto [it, added] = local.first_seen.try_emplace(key, value);
        if (!added && value) it->second = it->second ? std::min(it->second, value) : value;
    }
    larger(local.last_played, imported.last_played);
    larger(local.playtime_ms, imported.playtime_ms);
    larger(local.recent_hidden, imported.recent_hidden);
    missing(local.collections.names, imported.collections.names);
    for (const auto& [collection, members] : imported.collections.members) missing(local.collections.members[collection], members);
    local.collections.creates.insert(imported.collections.creates.begin(), imported.collections.creates.end());
    missing(local.preferred_versions, imported.preferred_versions);
    local.hidden.insert(imported.hidden.begin(), imported.hidden.end());
    missing(local.steam_unsynced, imported.steam_unsynced);
    missing(local.excluded_games, imported.excluded_games);
    missing(local.game_emulators, imported.game_emulators);
    missing(local.launch_arguments, imported.launch_arguments);
    missing(local.launch_flags, imported.launch_flags);
}

namespace {
constexpr uint64_t text_limit = 64ull * 1024 * 1024;

struct Applier {
    std::filesystem::path config, stage, backup;
    Mode mode = Mode::merge;
    PathMap paths;
    std::string system;
    size_t files = 0;
    std::wstring error;

    // The local file goes to backups/before-import-<time>/ before it is replaced.
    bool keep(const std::filesystem::path& relative) {
        const auto target = config / relative;
        if (!regularFile(target)) return true;
        std::error_code ec;
        const auto copy = backup / relative;
        std::filesystem::create_directories(copy.parent_path(), ec);
        if (!std::filesystem::copy_file(target, copy, std::filesystem::copy_options::overwrite_existing, ec)) {
            error = L"Cannot keep a copy of " + relative.wstring(); return false;
        }
        return true;
    }
    bool write(const std::filesystem::path& relative, std::string_view bytes) {
        if (!keep(relative)) return false;
        if (!atomic_file::write(config / relative, bytes)) { error = L"Cannot write " + relative.wstring(); return false; }
        ++files; return true;
    }
    bool copy(const std::filesystem::path& from, const std::filesystem::path& relative) {
        const auto target = config / relative;
        if (mode == Mode::merge && regularFile(target)) return true; // Merging keeps what this computer has.
        if (!keep(relative)) return false;
        std::error_code ec;
        std::filesystem::create_directories(target.parent_path(), ec);
        const std::filesystem::path temporary = target.wstring() + L".tmp.import";
        if (!std::filesystem::copy_file(from, temporary, std::filesystem::copy_options::overwrite_existing, ec) || !atomic_file::replace(temporary, target)) {
            std::filesystem::remove(temporary, ec);
            error = L"Cannot write " + relative.wstring(); return false;
        }
        ++files; return true;
    }

    bool settings(const std::filesystem::path& from) {
        const auto text = readFile(from, text_limit);
        if (!text) { error = L"Cannot read the imported settings."; return false; }
        const auto local = loadAppSettings(config / L"settings.ini");
        const auto merged = mergeSettings(local, parseAppSettings(*text), paths);
        if (!keep(L"settings.ini")) return false;
        if (!saveAppSettings(merged, config / L"settings.ini")) { error = L"Cannot write settings.ini."; return false; }
        ++files; return true;
    }
    bool library(const std::filesystem::path& from) {
        auto imported = loadLibraryState(from);
        if (!imported) if (auto salvaged = salvageLibraryState(from)) imported = std::move(salvaged->state);
        if (!imported) { error = L"Cannot read the imported library state."; return false; }
        LibraryState local;
        if (auto own = loadLibraryState(config / L"library-state.ini")) local = std::move(*own);
        else if (auto salvaged = salvageLibraryState(config / L"library-state.ini")) local = std::move(salvaged->state);
        mergeLibraryState(local, *imported, mode);
        if (!keep(L"library-state.ini")) return false;
        if (!saveLibraryState(local, config / L"library-state.ini")) { error = L"Cannot write library-state.ini."; return false; }
        ++files; return true;
    }
    bool customGames(const std::filesystem::path& from) {
        auto imported = custom_games::load(from).games;
        for (auto& game : imported) {
            game.path = mapPath(game.path, paths);
            game.directory = mapPath(game.directory, paths);
        }
        std::vector<custom_games::Entry> games;
        if (mode == Mode::merge) games = custom_games::load(config / L"custom-games.json").games;
        const auto same = [](const custom_games::Entry& a, const custom_games::Entry& b) {
            return a.id == b.id || (!a.path.empty() && unicode_text::equalIgnoreCase(slashes(a.path.wstring()), slashes(b.path.wstring())));
        };
        for (auto& game : imported) {
            if (games.size() >= custom_games::max_games) break;
            if (std::none_of(games.begin(), games.end(), [&](const auto& own) { return same(own, game); })) games.push_back(std::move(game));
        }
        if (!keep(L"custom-games.json")) return false;
        if (!custom_games::save(games, config / L"custom-games.json")) { error = L"Cannot write custom-games.json."; return false; }
        ++files; return true;
    }
    bool drmFree(const std::filesystem::path& from) {
        auto imported = drm_free::load(from).catalog;
        for (auto& root : imported.roots) root = mapPath(root, paths);
        for (auto& game : imported.games) {
            game.folder = mapPath(game.folder, paths);
            game.executable = mapPath(game.executable, paths);
            game.working_directory = mapPath(game.working_directory, paths);
        }
        drm_free::Catalog catalog = mode == Mode::merge ? drm_free::load(config / L"drm-free.json").catalog : drm_free::Catalog{};
        for (const auto& root : imported.roots)
            if (catalog.roots.size() < drm_free::max_roots &&
                std::none_of(catalog.roots.begin(), catalog.roots.end(), [&](const auto& own) { return unicode_text::equalIgnoreCase(slashes(own.wstring()), slashes(root.wstring())); }))
                catalog.roots.push_back(root);
        for (const auto& game : imported.games)
            if (catalog.games.size() < drm_free::max_games &&
                std::none_of(catalog.games.begin(), catalog.games.end(), [&](const auto& own) { return own.id == game.id; }))
                catalog.games.push_back(game);
        if (!keep(L"drm-free.json")) return false;
        if (!drm_free::save(catalog, config / L"drm-free.json")) { error = L"Cannot write drm-free.json."; return false; }
        ++files; return true;
    }
    bool journal(const std::filesystem::path& from) {
        const auto imported = play_journal::load(from);
        auto journal = mode == Mode::merge ? play_journal::load(config / L"play-journal.tsv") : play_journal::Journal{};
        for (const auto& [day, games] : imported.days)
            for (const auto& [key, entry] : games) {
                auto& own = journal.days[day][key];
                own.ms = std::max(own.ms, entry.ms);
                own.launches = std::max(own.launches, entry.launches);
            }
        for (const auto& [key, name] : imported.names) journal.names.try_emplace(key, name);
        if (!keep(L"play-journal.tsv")) return false;
        if (!play_journal::save(journal, config / L"play-journal.tsv")) { error = L"Cannot write play-journal.tsv."; return false; }
        ++files; return true;
    }

    bool part(Part part) {
        const auto root = stage / text_encoding::wideLenient(slug(part)) / L"files";
        std::vector<std::filesystem::path> found;
        std::error_code ec;
        if (std::filesystem::is_directory(root, ec))
            for (std::filesystem::recursive_directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec))
                if (it->is_regular_file(ec)) found.push_back(std::filesystem::relative(it->path(), root, ec));
        std::sort(found.begin(), found.end());
        for (const auto& relative : found) {
            const auto from = root / relative;
            const auto name = relative.generic_wstring();
            bool ok = true;
            if (name == L"custom-games.json") ok = customGames(from);
            else if (name == L"drm-free.json") ok = drmFree(from);
            else if (name == L"play-journal.tsv") ok = journal(from);
            else if (name == L"linux/launch_options.json" && system != systemName()) continue; // Wine options belong to their system.
            else ok = copy(from, relative);
            if (!ok) return false;
        }
        return true;
    }
};

std::string stamp() {
    const auto time = std::time(nullptr);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &time);
#else
    localtime_r(&time, &local);
#endif
    char text[32]{};
    std::strftime(text, sizeof(text), "%Y-%m-%d--%H-%M-%S", &local);
    return text;
}
void record(const std::filesystem::path& config, const Applied& applied) {
    auto done = json::object();
    done.properties["ok"] = json::boolean(applied.ok);
    done.properties["error"] = json::string(text_encoding::utf8Lenient(applied.error));
    done.properties["files"] = json::number(static_cast<double>(applied.files));
    done.properties["backup"] = json::string(text_encoding::utf8Lenient(applied.backup.wstring()));
    auto list = json::array();
    for (const auto& part : applied.parts) list.elements.push_back(json::string(part));
    done.properties["parts"] = std::move(list);
    atomic_file::write(config / done_file, json::serialize(done));
}
}

Applied applyPending(const std::filesystem::path& config) {
    Applied applied;
    if (!pending(config)) { applied.ok = true; return applied; }
    Applier applier;
    applier.config = config;
    applier.stage = config / pending_folder;
    const auto plan_text = readFile(applier.stage / L"plan.json", 1024 * 1024);
    const auto plan = plan_text ? json::parse(*plan_text) : std::nullopt;
    if (!plan || plan->type != json::Value::Type::object) {
        applied.error = L"The staged import is damaged and was dropped.";
        discardPending(config); record(config, applied);
        return applied;
    }
    applier.mode = plan->get("mode") && plan->get("mode")->stringOr() == "replace" ? Mode::replace : Mode::merge;
    if (const auto* value = plan->get("system")) applier.system = value->stringOr();
    if (const auto* rules = plan->get("paths"); rules && rules->type == json::Value::Type::array)
        for (const auto& rule : rules->elements)
            if (rule.elements.size() == 2)
                applier.paths.emplace_back(text_encoding::wideLenient(rule.elements[0].stringOr()), text_encoding::wideLenient(rule.elements[1].stringOr()));
    applier.backup = config / L"backups" / (L"before-import-" + text_encoding::wideLenient(stamp()));
    std::set<Part> chosen;
    if (const auto* list = plan->get("parts"); list && list->type == json::Value::Type::array)
        for (const auto& item : list->elements)
            if (const auto part = partFromSlug(item.stringOr())) { chosen.insert(*part); applied.parts.push_back(item.stringOr()); }

    // Files first: the settings point at some of them (a wallpaper, sounds) and keep only folders that exist.
    bool ok = true;
    for (const auto part : parts) if (ok && chosen.contains(part)) ok = applier.part(part);
    if (ok && chosen.contains(Part::settings) && regularFile(applier.stage / L"settings" / L"settings.ini"))
        ok = applier.settings(applier.stage / L"settings" / L"settings.ini");
    if (ok && chosen.contains(Part::library) && regularFile(applier.stage / L"library" / L"library-state.ini"))
        ok = applier.library(applier.stage / L"library" / L"library-state.ini");

    applied.ok = ok;
    applied.error = applier.error;
    applied.files = applier.files;
    std::error_code ec;
    if (std::filesystem::is_directory(applier.backup, ec)) applied.backup = applier.backup;
    discardPending(config);
    record(config, applied);
    return applied;
}

std::optional<Applied> takeApplied(const std::filesystem::path& config) {
    const auto file = config / done_file;
    const auto text = readFile(file, 1024 * 1024);
    std::error_code ec;
    std::filesystem::remove(file, ec);
    const auto parsed = text ? json::parse(*text) : std::nullopt;
    if (!parsed || parsed->type != json::Value::Type::object) return std::nullopt;
    Applied applied;
    if (const auto* value = parsed->get("ok")) applied.ok = value->flag;
    if (const auto* value = parsed->get("error")) applied.error = text_encoding::wideLenient(value->stringOr());
    if (const auto* value = parsed->get("files")) applied.files = static_cast<size_t>(std::max(0.0, value->numeric));
    if (const auto* value = parsed->get("backup")) applied.backup = text_encoding::wideLenient(value->stringOr());
    if (const auto* list = parsed->get("parts"); list && list->type == json::Value::Type::array)
        for (const auto& item : list->elements) applied.parts.push_back(item.stringOr());
    return applied;
}
}
