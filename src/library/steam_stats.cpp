#include "steam_stats.hpp"
#include "../platform/ascii_text.hpp"
#include "steam_achievements.hpp"
#include "../i18n/i18n.hpp"
#include "../net/json.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <fstream>
#include <mutex>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace {
constexpr uint64_t steam_id_offset = 76561197960265728ULL;

std::string lower(std::string_view text) { return ascii_text::lower(text); }
uint32_t number(std::string_view text) {
    uint32_t value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    return error == std::errc{} && end == text.data() + text.size() ? value : 0;
}
// Quoted tokens of one text-VDF line ("key" or "key" "value").
std::vector<std::string> tokens(std::string_view line) {
    std::vector<std::string> result;
    size_t position = 0;
    while (result.size() < 2) {
        while (position < line.size() && (line[position] == ' ' || line[position] == '\t' || line[position] == '\r')) ++position;
        if (position >= line.size() || line[position] != '"') break;
        ++position;
        std::string value;
        bool closed = false;
        while (position < line.size()) {
            const char ch = line[position++];
            if (ch == '"') { closed = true; break; }
            if (ch == '\\' && position < line.size()) value += line[position++];
            else value += ch;
        }
        if (!closed) break;
        result.push_back(std::move(value));
    }
    return result;
}
// UserLocalConfigStore/Software/Valve/Steam/apps/<AppID>/Playtime (minutes).
std::unordered_map<uint32_t, uint32_t> playtimes(std::istream& input) {
    std::unordered_map<uint32_t, uint32_t> result;
    std::vector<std::string> path;
    std::string pending, line;
    constexpr std::string_view prefix[] = {"userlocalconfigstore", "software", "valve", "steam", "apps"};
    while (std::getline(input, line)) {
        const auto first = line.find_first_not_of(" \t\r");
        if (first == std::string::npos) continue;
        if (line[first] == '{') { path.push_back(lower(pending)); pending.clear(); continue; }
        if (line[first] == '}') { if (!path.empty()) path.pop_back(); continue; }
        auto parts = tokens(line);
        if (parts.size() == 1) { pending = std::move(parts[0]); continue; }
        if (parts.size() != 2 || path.size() != 6 || lower(parts[0]) != "playtime") continue;
        if (!std::equal(std::begin(prefix), std::end(prefix), path.begin())) continue;
        if (const auto app = number(path[5])) result[app] = number(parts[1]);
    }
    return result;
}
std::optional<std::string> readText(const std::filesystem::path& file, size_t limit) {
    std::ifstream input(file, std::ios::binary | std::ios::ate);
    if (!input || input.tellg() <= 0 || static_cast<size_t>(input.tellg()) > limit) return std::nullopt;
    std::string text(static_cast<size_t>(input.tellg()), '\0');
    input.seekg(0);
    if (!input.read(text.data(), static_cast<std::streamsize>(text.size()))) return std::nullopt;
    return text;
}
uint32_t count(const json::Value* value) {
    return value && value->type == json::Value::Type::number && value->numeric >= 0 && value->numeric < 100000 ?
           static_cast<uint32_t>(value->numeric) : 0;
}
uint32_t appId(const json::Value& value) {
    return value.type == json::Value::Type::number && value.numeric > 0 && value.numeric <= UINT32_MAX ?
           static_cast<uint32_t>(value.numeric) : 0;
}
}

std::optional<uint32_t> readSteamPlaytime(const std::filesystem::path& steam_root, uint64_t steam_id, uint32_t app_id) {
    if (steam_id <= steam_id_offset || !app_id) return std::nullopt;
    static std::mutex mutex;
    static std::filesystem::path cached_file;
    static std::filesystem::file_time_type cached_time{};
    static std::chrono::steady_clock::time_point checked{};
    static std::unordered_map<uint32_t, uint32_t> cached;
    const auto file = steam_root / L"userdata" / std::to_wstring(steam_id - steam_id_offset) / L"config" / L"localconfig.vdf";
    std::lock_guard lock(mutex);
    // The file is looked at again at most every two seconds: a list asks for every visible row on each frame.
    const auto now = std::chrono::steady_clock::now();
    if (file != cached_file || now - checked >= std::chrono::seconds(2)) {
        checked = now;
        std::error_code ec;
        const auto time = std::filesystem::last_write_time(file, ec);
        if (ec) { cached.clear(); cached_file = file; cached_time = {}; }
        else if (file != cached_file || time != cached_time) {
            std::ifstream input(file, std::ios::binary);
            cached = input ? playtimes(input) : std::unordered_map<uint32_t, uint32_t>{};
            cached_file = file; cached_time = time;
        }
    }
    if (const auto found = cached.find(app_id); found != cached.end()) return found->second;
    return std::nullopt;
}

SteamStats readSteamStats(const std::filesystem::path& steam_root, uint64_t steam_id, uint32_t app_id) {
    SteamStats result;
    if (steam_id <= steam_id_offset || !app_id) return result;
    const auto config = steam_root / L"userdata" / std::to_wstring(steam_id - steam_id_offset) / L"config";
    result.playtime_minutes = readSteamPlaytime(steam_root, steam_id, app_id);
    // Exact: the client's achievement schema and the account's unlock bits (appcache/stats).
    if (const auto [unlocked, total] = localAchievementCounts(steam_root, steam_id, app_id); total) {
        result.achievements_unlocked = unlocked;
        result.achievements_total = total;
        return result;
    }
    const auto cache = config / L"librarycache";
    // The client's summary of recently seen games: {"mapCache":[[appid,{"unlocked":n,"total":m}],...]}.
    if (const auto text = readText(cache / L"achievement_progress.json", 8 * 1024 * 1024)) {
        const auto root = json::parse(*text);
        const auto* entries = root ? root->get("mapCache") : nullptr;
        if (entries && entries->type == json::Value::Type::array)
            for (const auto& entry : entries->elements) {
                if (entry.type != json::Value::Type::array || entry.elements.size() < 2 || appId(entry.elements[0]) != app_id) continue;
                const auto total = count(entry.elements[1].get("total"));
                if (!total) break;
                result.achievements_total = total;
                result.achievements_unlocked = std::min(total, count(entry.elements[1].get("unlocked")));
                return result;
            }
    }
    // The game page cache: [["achievements",{"data":{"nTotal":m,"nAchieved":n,...}}], ...].
    if (const auto text = readText(cache / (std::to_wstring(app_id) + L".json"), 4 * 1024 * 1024)) {
        const auto root = json::parse(*text);
        if (root && root->type == json::Value::Type::array)
            for (const auto& section : root->elements) {
                if (section.type != json::Value::Type::array || section.elements.size() < 2 ||
                    section.elements[0].stringOr() != "achievements") continue;
                const auto* data = section.elements[1].get("data");
                if (!data) break;
                result.achievements_total = count(data->get("nTotal"));
                result.achievements_unlocked = std::min(result.achievements_total, count(data->get("nAchieved")));
                break;
            }
    }
    return result;
}

std::wstring formatPlaytime(uint32_t minutes) {
    if (minutes < 60) return std::to_wstring(minutes) + L" min";
    const double hours = minutes / 60.0;
    std::wstring value;
    if (hours < 10) {
        const auto tenths = static_cast<int>(std::lround(hours * 10));
        value = std::to_wstring(tenths / 10);
        if (tenths % 10) value += (i18n::language() == i18n::Language::en ? L"." : L",") + std::to_wstring(tenths % 10);
    } else value = std::to_wstring(static_cast<long long>(std::lround(hours)));
    return value + tr(L" godz.");
}
