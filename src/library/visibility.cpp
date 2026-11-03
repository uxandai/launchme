#include "visibility.hpp"

#include <charconv>
#include <fstream>
#include <string>
#include <string_view>

namespace {
constexpr uint64_t steam_id_offset = 76561197960265728ULL;

// No exceptions: "users" and every other non-numeric key of loginusers.vdf passes through here.
uint64_t number(const std::string& value) {
    uint64_t result = 0;
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), result);
    return error == std::errc() ? result : 0;
}

uint64_t activeAccount(const std::filesystem::path& root) {
    std::ifstream users(root / "config" / "loginusers.vdf", std::ios::binary);
    uint64_t current = 0, best = 0, timestamp = 0, best_timestamp = 0;
    bool auto_login = false, best_auto_login = false;
    auto finish = [&] {
        if (current && (auto_login > best_auto_login ||
                        (auto_login == best_auto_login && timestamp > best_timestamp))) {
            best = current;
            best_timestamp = timestamp;
            best_auto_login = auto_login;
        }
    };
    std::string line;
    while (std::getline(users, line)) {
        const size_t first = line.find('"');
        if (first == std::string::npos) continue;
        const size_t second = line.find('"', first + 1);
        if (second == std::string::npos) continue;
        const auto key = line.substr(first + 1, second - first - 1);
        const size_t third = line.find('"', second + 1);
        if (third == std::string::npos) {
            const uint64_t id = number(key);
            if (id > steam_id_offset) {
                finish();
                current = id - steam_id_offset;
                timestamp = 0;
                auto_login = false;
            }
        } else if (current) {
            const size_t fourth = line.find('"', third + 1);
            if (fourth == std::string::npos) continue;
            const auto value = line.substr(third + 1, fourth - third - 1);
            if (key == "AutoLogin") auto_login = value == "1";
            else if (key == "timestamp") timestamp = number(value);
        }
    }
    finish();
    return best;
}

std::unordered_set<uint32_t> ids(std::string_view list) {
    std::unordered_set<uint32_t> result;
    const auto open = list.find('['), close = list.find(']', open);
    if (open == std::string_view::npos || close == std::string_view::npos) return result;
    uint64_t id = 0;
    bool digits = false;
    for (size_t i = open + 1; i <= close; ++i) {
        const char ch = list[i];
        if (ch >= '0' && ch <= '9') {
            digits = true;
            id = id * 10 + static_cast<uint64_t>(ch - '0');
        } else if (digits) {
            if (id && id <= UINT32_MAX) result.insert(static_cast<uint32_t>(id));
            digits = false;
            id = 0;
        }
    }
    return result;
}

std::unordered_set<uint32_t> arrayFor(const std::string& json, size_t start, size_t limit,
                                      std::string_view key) {
    const auto position = json.find(key, start);
    if (position == std::string::npos || position >= limit) return {};
    const auto open = json.find('[', position + key.size());
    if (open == std::string::npos || open >= limit) return {};
    const auto close = json.find(']', open);
    if (close == std::string::npos || close >= limit) return {};
    return ids(std::string_view(json).substr(open, close - open + 1));
}
} // namespace

std::unordered_set<uint32_t> excludedSteamApps(const std::filesystem::path& steam_root) {
    const uint64_t account = activeAccount(steam_root);
    if (!account) return {};
    const auto userdata = steam_root / "userdata" / std::to_wstring(account) / "config";
    std::unordered_set<uint32_t> excluded;
    const auto private_key = "\"PrivateApps_" + std::to_string(account) + "\"";
    std::ifstream config(userdata / "localconfig.vdf", std::ios::binary);
    std::string line;
    while (std::getline(config, line)) {
        const auto start = line.find(private_key);
        if (start == std::string::npos) continue;
        const auto open = line.find('"', start + private_key.size());
        const auto close = line.find('"', open + 1);
        if (open != std::string::npos && close != std::string::npos) {
            auto private_games = ids(std::string_view(line).substr(open + 1, close - open - 1));
            excluded.insert(private_games.begin(), private_games.end());
        }
        break;
    }
    std::ifstream cloud(userdata / "cloudstorage" / "cloud-storage-namespace-1.json", std::ios::binary);
    if (!cloud) return excluded;
    const std::string json(std::istreambuf_iterator<char>{cloud}, {});
    const auto hidden = json.find("\"user-collections.hidden\"");
    if (hidden == std::string::npos) return excluded;
    const auto next_entry = json.find("\"version\"", hidden);
    const auto limit = next_entry == std::string::npos ? json.size() : next_entry;
    auto added = arrayFor(json, hidden, limit, "\\\"added\\\":");
    const auto removed = arrayFor(json, hidden, limit, "\\\"removed\\\":");
    for (uint32_t id : removed) added.erase(id);
    excluded.insert(added.begin(), added.end());
    return excluded;
}

uint64_t activeSteamId(const std::filesystem::path& steam_root) {
    const uint64_t account = activeAccount(steam_root);
    return account ? steam_id_offset + account : 0;
}
