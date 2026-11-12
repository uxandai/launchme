#include "local_achievements.hpp"
#include "../net/json.hpp"
#include "../platform/ascii_text.hpp"
#include "../platform/atomic_file.hpp"
#include "../platform/file_io.hpp"
#include "../platform/text_encoding.hpp"

#include <algorithm>
#include <charconv>
#include <cwctype>
#include <deque>
#include <optional>
#include <system_error>
#include <unordered_map>
#include <unordered_set>

#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#endif

namespace local_achievements {
namespace {
namespace fs = std::filesystem;
constexpr size_t max_entries = 8000;  // Folder walk.
constexpr int max_depth = 4;
constexpr size_t max_state = 4 * 1024 * 1024, max_schema = 16 * 1024 * 1024;

bool same(std::wstring_view a, std::wstring_view b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](wchar_t x, wchar_t y) { return towlower(x) == towlower(y); });
}
uint32_t appId(std::string_view text) {
    text = ascii_text::trim(text);
    uint32_t value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    return error == std::errc{} && end == text.data() + text.size() ? value : 0;
}
std::string_view withoutBom(std::string_view text) { return text.starts_with("\xef\xbb\xbf") ? text.substr(3) : text; }
// "key=value" of a small ini file, case-insensitive key, the first one in any section.
std::string iniValue(std::string_view text, std::string_view key) {
    text = withoutBom(text);
    while (!text.empty()) {
        const auto end = text.find('\n');
        auto line = ascii_text::trim(text.substr(0, end));
        text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1);
        const auto equal = line.find('=');
        if (equal == std::string_view::npos || line.starts_with(';') || line.starts_with('#')) continue;
        if (ascii_text::equalIgnoreCase(ascii_text::trim(line.substr(0, equal)), key)) return std::string(ascii_text::trim(line.substr(equal + 1)));
    }
    return {};
}
fs::path relativeTo(const fs::path& base, std::string_view text) {
    auto value = std::string(ascii_text::trim(text, " \t\r\n\"'"));
    if (value.empty()) return base;
    fs::path path(text_encoding::wideLenient(value));
    return path.is_absolute() ? path : base / path;
}
bool isFile(const fs::path& path) { return file_io::isFile(path); }
uint64_t number(const json::Value* value) {
    if (!value) return 0;
    if (value->type == json::Value::Type::number) return value->numeric > 0 ? static_cast<uint64_t>(value->numeric) : 0;
    if (value->type == json::Value::Type::string) return appId(value->text);
    return 0;
}
bool flag(const json::Value* value) {
    if (!value) return false;
    if (value->type == json::Value::Type::boolean) return value->flag;
    if (value->type == json::Value::Type::number) return value->numeric != 0;
    if (value->type == json::Value::Type::string) return value->text == "1" || ascii_text::equalIgnoreCase(value->text, "true");
    return false;
}
// Schemas write names either as text or per language ({"english": ..., "polish": ...}).
std::wstring localized(const json::Value* value, bool english) {
    if (!value) return {};
    if (value->type == json::Value::Type::string) return text_encoding::wideLenient(value->text);
    if (value->type != json::Value::Type::object) return {};
    for (const char* language : {english ? "english" : "polish", "english"})
        if (const auto* found = value->get(language); found && found->type == json::Value::Type::string && !found->text.empty())
            return text_encoding::wideLenient(found->text);
    for (const auto& [name, entry] : value->properties)
        if (entry.type == json::Value::Type::string && !entry.text.empty() && name != "token") return text_encoding::wideLenient(entry.text);
    return {};
}
// A schema's icon: https as is, else a picture next to the schema (the path it names, or the
// generators' achievement_images\ or img\ folders).
std::wstring icon(const json::Value* value, const fs::path& steam_settings) {
    if (!value || value->type != json::Value::Type::string || value->text.empty() || value->text.size() > 400) return {};
    auto name = text_encoding::wideLenient(value->text);
    if (name.starts_with(L"https://")) return name.find_first_of(L" \r\n\t") == std::wstring::npos ? name : std::wstring{};
    if (name.find(L"://") != std::wstring::npos) return {};
    std::replace(name.begin(), name.end(), L'/', static_cast<wchar_t>(fs::path::preferred_separator));
    const fs::path relative(name);
    if (relative.is_absolute()) return {};
    const auto file = relative.filename();
    for (const auto& candidate : {steam_settings / relative, steam_settings / L"achievement_images" / file, steam_settings / L"img" / file,
                                  steam_settings / L"images" / file, steam_settings / L"achievement_images" / relative, steam_settings / L"img" / relative}) {
        if (!file.empty() && isFile(candidate)) return candidate.lexically_normal().wstring();
    }
    return {};
}

struct Walk {
    uint32_t ini_app = 0, steam_app = 0; // RealAppId= of an .ini beside the game; steam_appid.txt.
    bool ini_saves = false, json_saves = false; // The folder's files point to saves in that format.
    fs::path steam_settings;                 // The first with a schema, else the first.
    std::vector<fs::path> local_roots;       // local_save.txt / local_save_path.
    std::vector<fs::path> json_states, ini_states; // achievements.json / Achievements.ini inside the game folder.
};
Walk walk(const fs::path& folder) {
    Walk found;
    std::deque<std::pair<fs::path, int>> queue{{folder, 0}};
    size_t seen = 0;
    while (!queue.empty() && seen < max_entries) {
        auto [current, depth] = std::move(queue.front());
        queue.pop_front();
        std::error_code error;
        const bool in_settings = same(current.filename().wstring(), L"steam_settings");
        for (fs::directory_iterator it(current, fs::directory_options::skip_permission_denied, error), end; !error && it != end && seen < max_entries; it.increment(error)) {
            ++seen;
            const auto& path = it->path();
            const auto name = path.filename().wstring();
            std::error_code type_error;
            if (it->is_directory(type_error)) {
                if (it->is_symlink(type_error)) continue;
                if (same(name, L"steam_settings")) {
                    found.json_saves = true;
                    if (found.steam_settings.empty() || (!isFile(found.steam_settings / L"achievements.json") && isFile(path / L"achievements.json")))
                        found.steam_settings = path;
                }
                if (depth + 1 <= max_depth) queue.emplace_back(path, depth + 1);
                continue;
            }
            if (same(name, L"OnlineFix.ini") || same(name, L"OnlineFix64.ini")) {
                found.ini_saves = true;
                if (!found.ini_app) found.ini_app = appId(iniValue(file_io::readText(path, 256 * 1024), "RealAppId"));
            } else if (same(name, L"steam_appid.txt")) {
                if (!found.steam_app) found.steam_app = appId(withoutBom(file_io::readText(path, 64)));
            } else if (same(name, L"local_save.txt")) {
                found.json_saves = true;
                found.local_roots.push_back(relativeTo(current, withoutBom(file_io::readText(path, 4096))));
            } else if (in_settings && same(name, L"configs.user.ini")) {
                // Relative to the folder holding steam_settings.
                if (const auto local = iniValue(file_io::readText(path, 256 * 1024), "local_save_path"); !local.empty())
                    found.local_roots.push_back(relativeTo(current.parent_path(), local));
            } else if (!in_settings && same(name, L"achievements.json")) {
                found.json_states.push_back(path);
            } else if (same(name, L"Achievements.ini")) {
                found.ini_states.push_back(path);
            }
        }
    }
    return found;
}
// Of several saves, the one in the AppID's own folder; else the first.
fs::path pick(const std::vector<fs::path>& files, uint32_t app_id) {
    if (files.empty()) return {};
    const auto app = std::to_wstring(app_id);
    for (const auto& file : files)
        for (const auto& part : file.parent_path()) if (app_id && part.wstring() == app) return file;
    return files.front();
}
bool jsonState(const fs::path& file) { return !parseJson(file_io::readText(file, max_state)).empty(); }
bool iniState(const fs::path& file) { return !parseIni(file_io::readText(file, max_state)).empty(); }
}

Places systemPlaces() {
    Places places;
#ifdef _WIN32
    for (auto [id, target] : {std::pair{&FOLDERID_PublicDocuments, &places.public_documents}, std::pair{&FOLDERID_RoamingAppData, &places.roaming_app_data}}) {
        PWSTR path = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(*id, 0, nullptr, &path)) && path) *target = path;
        CoTaskMemFree(path);
    }
#endif
    return places;
}

Found find(const std::filesystem::path& game_folder, const Places& places) {
    Found result;
    if (game_folder.empty() || !file_io::isFolder(game_folder)) return result;
    auto scan = walk(game_folder);
    result.app_id = scan.ini_app ? scan.ini_app : scan.steam_app;
    result.steam_settings = scan.steam_settings;
    const auto app = std::to_wstring(result.app_id);
    // 1. Saves kept with the game (local_save.txt, configs.user.ini).
    for (const auto& root : scan.local_roots) {
        for (const auto& file : {root / app / L"achievements.json", root / L"achievements.json"})
            if ((result.app_id || file == root / L"achievements.json") && isFile(file)) { result.format = Format::json; result.state = file; return result; }
    }
    // 2. Any other save the game folder holds.
    std::erase_if(scan.json_states, [](const fs::path& file) { return !jsonState(file); });
    std::erase_if(scan.ini_states, [](const fs::path& file) { return !iniState(file); });
    if (scan.ini_saves && !scan.ini_states.empty()) { result.format = Format::ini; result.state = pick(scan.ini_states, result.app_id); return result; }
    if (!scan.json_states.empty()) { result.format = Format::json; result.state = pick(scan.json_states, result.app_id); return result; }
    if (!scan.ini_states.empty()) { result.format = Format::ini; result.state = pick(scan.ini_states, result.app_id); return result; }
    if (!result.app_id) return result;
    // 3. The shared save folders.
    if (scan.ini_saves && !places.public_documents.empty()) {
        const auto base = places.public_documents / L"OnlineFix" / app;
        for (const auto& file : {base / L"Stats" / L"Achievements.ini", base / L"Achievements.ini"})
            if (isFile(file)) { result.format = Format::ini; result.state = file; return result; }
    }
    if (!places.roaming_app_data.empty())
        if (const auto file = places.roaming_app_data / L"GSE Saves" / app / L"achievements.json"; isFile(file)) {
            result.format = Format::json; result.state = file; return result;
        }
    // Nothing saved yet: the achievements, all locked.
    if (scan.ini_saves) result.format = Format::ini;
    else if (scan.json_saves) result.format = Format::json;
    return result;
}

std::vector<Unlock> parseJson(std::string_view text) {
    std::vector<Unlock> result;
    const auto root = json::parse(withoutBom(text), max_state);
    if (!root || root->type != json::Value::Type::object) return result;
    for (const auto& [id, entry] : root->properties) {
        if (id.empty() || id.size() > 256 || entry.type != json::Value::Type::object || !entry.get("earned")) continue;
        Unlock unlock{id, flag(entry.get("earned")), 0};
        if (unlock.achieved) unlock.time = number(entry.get("earned_time"));
        result.push_back(std::move(unlock));
    }
    std::sort(result.begin(), result.end(), [](const Unlock& a, const Unlock& b) { return a.id < b.id; });
    return result;
}

std::vector<Unlock> parseIni(std::string_view text) {
    std::vector<Unlock> result;
    text = withoutBom(text);
    std::optional<Unlock> current;
    const auto flush = [&] { if (current && !current->id.empty()) result.push_back(std::move(*current)); current.reset(); };
    while (!text.empty()) {
        const auto end = text.find('\n');
        const auto line = ascii_text::trim(text.substr(0, end));
        text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1);
        if (line.empty() || line.starts_with(';') || line.starts_with('#')) continue;
        if (line.starts_with('[') && line.ends_with(']')) {
            flush();
            const auto id = ascii_text::trim(line.substr(1, line.size() - 2));
            if (!id.empty() && id.size() <= 256) current = Unlock{std::string(id), false, 0};
            continue;
        }
        const auto equal = line.find('=');
        if (!current || equal == std::string_view::npos) continue;
        const auto key = ascii_text::trim(line.substr(0, equal));
        const auto value = ascii_text::trim(line.substr(equal + 1));
        if (ascii_text::equalIgnoreCase(key, "achieved")) current->achieved = value == "1" || ascii_text::equalIgnoreCase(value, "true");
        else if (ascii_text::equalIgnoreCase(key, "timestamp") || ascii_text::equalIgnoreCase(key, "unlocktime")) {
            uint64_t time = 0;
            std::from_chars(value.data(), value.data() + value.size(), time);
            current->time = time;
        }
    }
    flush();
    for (auto& unlock : result) if (!unlock.achieved) unlock.time = 0;
    return result;
}

std::vector<Unlock> readUnlocks(const Found& found) {
    if (found.state.empty()) return {};
    const auto text = file_io::readText(found.state, max_state);
    return same(found.state.extension().wstring(), L".ini") ? parseIni(text) : parseJson(text);
}

SteamAchievements parseSchema(std::string_view text, const std::filesystem::path& steam_settings, bool english) {
    SteamAchievements result;
    const auto root = json::parse(withoutBom(text), max_schema);
    if (!root || root->type != json::Value::Type::array) return result;
    std::unordered_set<std::string> seen;
    for (const auto& entry : root->elements) {
        if (entry.type != json::Value::Type::object) continue;
        SteamAchievement item;
        item.id = entry.get("name") ? entry.get("name")->stringOr() : std::string{};
        if (item.id.empty() || item.id.size() > 256 || !seen.insert(item.id).second) continue;
        item.name = localized(entry.get("displayName"), english);
        item.description = localized(entry.get("description"), english);
        item.hidden = flag(entry.get("hidden"));
        item.icon = icon(entry.get("icon"), steam_settings);
        item.gray_icon = icon(entry.get("icongray"), steam_settings);
        if (item.gray_icon.empty()) item.gray_icon = icon(entry.get("icon_gray"), steam_settings);
        result.items.push_back(std::move(item));
    }
    result.total = static_cast<uint32_t>(result.items.size());
    result.complete = !result.items.empty();
    return result;
}

SteamAchievements readSchema(const Found& found, bool english) {
    if (found.steam_settings.empty()) return {};
    return parseSchema(file_io::readText(found.steam_settings / L"achievements.json", max_schema), found.steam_settings, english);
}

SteamAchievements overlay(SteamAchievements schema, const std::vector<Unlock>& unlocks) {
    std::unordered_map<std::string, const Unlock*> exact, folded;
    for (const auto& unlock : unlocks) { exact.emplace(unlock.id, &unlock); folded.emplace(ascii_text::lower(unlock.id), &unlock); }
    const bool listed = !schema.items.empty();
    std::unordered_set<const Unlock*> used;
    for (auto& item : schema.items) {
        const Unlock* unlock = nullptr;
        if (const auto found = exact.find(item.id); found != exact.end()) unlock = found->second;
        else if (const auto loose = folded.find(ascii_text::lower(item.id)); loose != folded.end()) unlock = loose->second;
        item.achieved = unlock && unlock->achieved;
        item.unlocked_at = item.achieved ? unlock->time : 0;
        if (unlock) used.insert(unlock);
        if (!item.achieved && !item.gray_icon.empty()) item.icon = item.gray_icon;
        if (item.name.empty()) item.name = readableId(item.id);
    }
    // Unlocks the schema does not know (or no schema at all): named after their ids.
    bool locked_listed = false;
    for (const auto& unlock : unlocks) {
        if (used.contains(&unlock) || (listed && !unlock.achieved)) continue;
        locked_listed |= !unlock.achieved;
        SteamAchievement item;
        item.id = unlock.id;
        item.name = readableId(unlock.id);
        item.achieved = unlock.achieved;
        item.unlocked_at = unlock.achieved ? unlock.time : 0;
        schema.items.push_back(std::move(item));
    }
    schema.unlocked = static_cast<uint32_t>(std::count_if(schema.items.begin(), schema.items.end(), [](const SteamAchievement& item) { return item.achieved; }));
    schema.total = static_cast<uint32_t>(schema.items.size());
    schema.complete = listed || locked_listed; // JSON saves list every achievement; INI saves the earned ones.
    sortAchievements(schema.items);
    return schema;
}

std::wstring readableId(std::string_view id) {
    auto text = std::string(id);
    for (const char* prefix : {"ACH_", "ACHIEVEMENT_", "ACHIEVEMENTS_"})
        if (text.size() > std::string_view(prefix).size() && ascii_text::equalIgnoreCase(std::string_view(text).substr(0, std::string_view(prefix).size()), prefix)) {
            text.erase(0, std::string_view(prefix).size());
            break;
        }
    std::replace(text.begin(), text.end(), '_', ' ');
    // SHOUTED ids read as a sentence; mixed case stays.
    if (std::none_of(text.begin(), text.end(), [](char c) { return c >= 'a' && c <= 'z'; })) text = ascii_text::lower(text);
    std::string spaced;
    for (size_t i = 0; i < text.size(); ++i) { // CamelCase → words.
        const char c = text[i];
        if (i && c >= 'A' && c <= 'Z' && text[i - 1] >= 'a' && text[i - 1] <= 'z') spaced += ' ';
        spaced += c;
    }
    auto trimmed = std::string(ascii_text::trim(spaced));
    if (!trimmed.empty() && trimmed[0] >= 'a' && trimmed[0] <= 'z') trimmed[0] = static_cast<char>(trimmed[0] - 'a' + 'A');
    return text_encoding::wideLenient(trimmed.empty() ? std::string(id) : trimmed);
}
} // namespace local_achievements

namespace local_achievements {
Local localList(const Found& found, const std::filesystem::path& steam_root, bool english) {
    Local result;
    if (!found) return result;
    auto schema = readSchema(found, english);
    if (schema.items.empty() && found.app_id && !steam_root.empty()) schema = readSteamSchema(steam_root, found.app_id, english);
    result.named = !schema.items.empty();
    result.list = overlay(std::move(schema), readUnlocks(found));
    return result;
}

void applyShares(SteamAchievements& list, const std::vector<std::pair<std::string, float>>& percentages) {
    if (percentages.empty()) return;
    if (!list.complete) {
        std::unordered_set<std::string> known;
        for (const auto& item : list.items) known.insert(ascii_text::lower(item.id));
        for (const auto& [id, share] : percentages) {
            if (id.empty() || !known.insert(ascii_text::lower(id)).second) continue;
            SteamAchievement item;
            item.id = id;
            item.name = readableId(id);
            list.items.push_back(std::move(item));
        }
        list.total = static_cast<uint32_t>(list.items.size());
        list.complete = true;
    }
    // Shares by id, whatever its case in the save file.
    std::vector<std::pair<std::string, float>> folded;
    std::unordered_map<std::string, std::string> ids;
    for (const auto& item : list.items) ids.emplace(ascii_text::lower(item.id), item.id);
    for (const auto& [id, share] : percentages)
        if (const auto found = ids.find(ascii_text::lower(id)); found != ids.end()) folded.emplace_back(found->second, share);
    applyGlobalPercentages(list, folded);
}
} // namespace local_achievements

namespace local_achievements {
std::vector<std::wstring> iconUrls(const std::wstring& url, uint32_t app_id) {
    std::vector<std::wstring> result;
    const auto name = achievementIconFile(url);
    if (name.empty()) return result;
    result.push_back(url);
    const auto slash = url.find(L'/', 8);
    const auto path = slash == std::wstring::npos ? std::wstring{} : url.substr(slash);
    const auto add = [&](std::wstring candidate) {
        if (std::find(result.begin(), result.end(), candidate) == result.end()) result.push_back(std::move(candidate));
    };
    if (!path.empty())
        for (const wchar_t* host : {L"cdn.fastly.steamstatic.com", L"cdn.akamai.steamstatic.com", L"steamcdn-a.akamaihd.net"})
            add(std::wstring(L"https://") + host + path);
    if (app_id) {
        const auto app = std::to_wstring(app_id);
        add(L"https://shared.fastly.steamstatic.com/community_assets/images/apps/" + app + L"/" + name);
        add(L"https://shared.akamai.steamstatic.com/community_assets/images/apps/" + app + L"/" + name);
        add(L"https://cdn.cloudflare.steamstatic.com/steamcommunity/public/images/apps/" + app + L"/" + name);
    }
    return result;
}

bool saveSchema(const Found& found, const SteamAchievements& english, const SteamAchievements& polish,
                const SteamHttp& get, const std::function<bool()>& stop) {
    const auto& folder = found.steam_settings;
    const auto target = folder / L"achievements.json";
    if (folder.empty() || english.items.empty() || isFile(target)) return false;
    const auto images = folder / L"img";
    std::error_code error;
    fs::create_directories(images, error);
    if (error) return false;
    std::unordered_map<std::string, const SteamAchievement*> translated;
    for (const auto& item : polish.items) translated.emplace(item.id, &item);
    // An icon on disk (downloaded now or before): its path relative to steam_settings.
    const auto icon = [&](const std::wstring& url) -> std::string {
        const auto name = achievementIconFile(url);
        if (name.empty()) return {};
        const auto file = images / name;
        if (!isFile(file))
            for (const auto& candidate : iconUrls(url, found.app_id)) {
                if (stop && stop()) return {};
                const auto reply = get(candidate, L"Accept: image/jpeg, image/png\r\n", 1024 * 1024);
                if (reply.status == 200 && reply.body.size() >= 64 && atomic_file::write(file, std::span<const uint8_t>(reply.body))) break;
            }
        return isFile(file) ? "img/" + text_encoding::utf8Lenient(name) : std::string{};
    };
    auto list = json::array();
    for (const auto& item : english.items) {
        if (stop && stop()) return false;
        const auto local = translated.find(item.id);
        const auto* pl = local == translated.end() ? nullptr : local->second;
        auto name = json::object(), description = json::object();
        name.properties["english"] = json::string(text_encoding::utf8Lenient(item.name));
        description.properties["english"] = json::string(text_encoding::utf8Lenient(item.description));
        if (pl && !pl->name.empty()) name.properties["polish"] = json::string(text_encoding::utf8Lenient(pl->name));
        if (pl && !pl->description.empty()) description.properties["polish"] = json::string(text_encoding::utf8Lenient(pl->description));
        auto entry = json::object();
        entry.properties["name"] = json::string(item.id);
        entry.properties["displayName"] = std::move(name);
        entry.properties["description"] = std::move(description);
        entry.properties["hidden"] = json::number(item.hidden ? 1 : 0);
        entry.properties["icon"] = json::string(icon(item.icon));
        entry.properties["icongray"] = json::string(icon(item.gray_icon.empty() ? item.icon : item.gray_icon));
        list.elements.push_back(std::move(entry));
    }
    if (stop && stop()) return false;
    return atomic_file::write(target, json::serialize(list));
}
} // namespace local_achievements
