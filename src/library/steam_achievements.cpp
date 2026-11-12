#include "steam_achievements.hpp"
#include "../platform/ascii_text.hpp"
#include "../platform/file_io.hpp"
#include "../net/json.hpp"
#include "../platform/text_encoding.hpp"

#include <algorithm>
#include <charconv>
#include <cstring>
#include <fstream>
#include <unordered_map>
#include <unordered_set>

namespace {
constexpr uint64_t steam_id_offset = 76561197960265728ULL;

std::wstring text(const json::Value* value) {
    return value && value->type == json::Value::Type::string ? text_encoding::wideLenient(value->text) : std::wstring{};
}
double number(const json::Value* value) {
    if (!value) return -1;
    if (value->type == json::Value::Type::number) return value->numeric;
    if (value->type == json::Value::Type::string) { // Global percentages arrive as strings.
        double parsed = -1;
        const auto [end, error] = std::from_chars(value->text.data(), value->text.data() + value->text.size(), parsed);
        return error == std::errc{} ? parsed : -1;
    }
    return -1;
}
bool flag(const json::Value* value) {
    return value && ((value->type == json::Value::Type::boolean && value->flag) || (value->type == json::Value::Type::number && value->numeric != 0));
}
std::wstring httpsUrl(const json::Value* value) {
    const auto url = text(value);
    return url.starts_with(L"https://") && url.size() < 2048 && url.find_first_of(L" \r\n\t") == std::wstring::npos ? url : std::wstring{};
}
std::optional<json::Value> fetchJson(const SteamHttp& get, const std::wstring& url) {
    const auto reply = get(url, L"Accept: application/json\r\n", 4 * 1024 * 1024);
    if (reply.status != 200) return std::nullopt;
    return json::parse(std::string_view(reinterpret_cast<const char*>(reply.body.data()), reply.body.size()));
}
std::vector<uint8_t> readBytes(const std::filesystem::path& file, size_t limit) { return file_io::readBytes(file, limit); }
bool sameKey(std::string_view a, std::string_view b) { return ascii_text::equalIgnoreCase(a, b); }
int64_t integer(std::string_view text) {
    int64_t value = -1;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    return error == std::errc{} && end == text.data() + text.size() ? value : -1;
}
int64_t nodeNumber(const binary_kv::Node* node) {
    if (!node) return -1;
    return node->numeric ? node->number : integer(node->text);
}
// A stat's 32 unlock bits. Steam stores them as a signed 32-bit int: bit 31 makes it negative
// (all 32 unlocked is -1), so the value must not be tested for > 0.
uint32_t unlockBits(const binary_kv::Node* node) {
    if (!node) return 0;
    if (node->numeric) return static_cast<uint32_t>(node->number);
    int64_t value = 0;
    const auto* end = node->text.data() + node->text.size();
    const auto [stop, error] = std::from_chars(node->text.data(), end, value);
    return error == std::errc{} && stop == end ? static_cast<uint32_t>(value) : 0;
}
// A localized schema string: {"english": ..., "polish": ..., "token": ...} or a plain value.
std::wstring localized(const binary_kv::Node* node, bool english) {
    if (!node) return {};
    if (!node->object) return text_encoding::wideLenient(node->text);
    if (const auto* wanted = node->child(english ? "english" : "polish"); wanted && !wanted->text.empty()) return text_encoding::wideLenient(wanted->text);
    if (const auto* fallback = node->child("english"); fallback && !fallback->text.empty()) return text_encoding::wideLenient(fallback->text);
    for (const auto& entry : node->children)
        if (!entry.object && !entry.text.empty() && !sameKey(entry.name, "token")) return text_encoding::wideLenient(entry.text);
    return {};
}
std::wstring schemaIcon(uint32_t app_id, const binary_kv::Node* node) {
    if (!node || node->text.empty() || node->text.size() > 200) return {};
    const auto name = text_encoding::wideLenient(node->text);
    if (name.starts_with(L"https://")) return name;
    return L"https://cdn.cloudflare.steamstatic.com/steamcommunity/public/images/apps/" + std::to_wstring(app_id) + L"/" + name;
}
std::filesystem::path statsFolder(const std::filesystem::path& steam_root) { return steam_root / L"appcache" / L"stats"; }
}

namespace {
// steam_id 0: the schema alone, every achievement locked (a game run under a Steam emulator).
SteamAchievements readSchemaList(const std::filesystem::path& steam_root, uint64_t steam_id, uint32_t app_id, bool english) {
    SteamAchievements result;
    if ((steam_id && steam_id <= steam_id_offset) || !app_id) return result;
    const auto folder = statsFolder(steam_root);
    const auto schema_bytes = readBytes(folder / (L"UserGameStatsSchema_" + std::to_wstring(app_id) + L".bin"), 32 * 1024 * 1024);
    const auto schema = binary_kv::parse(schema_bytes);
    if (!schema) return result;
    const auto* game = schema->child(std::to_string(app_id));
    if (!game && !schema->children.empty() && schema->children.front().object) game = &schema->children.front();
    const auto* stats = game ? game->child("stats") : nullptr;
    if (!stats) return result;
    // The account's state: cache/<stat>/data (unlock bits) and cache/<stat>/AchievementTimes/<bit>.
    std::optional<binary_kv::Node> user;
    std::unordered_map<std::string, float> shares;
    if (steam_id) {
        const auto account = std::to_wstring(steam_id - steam_id_offset);
        user = binary_kv::parse(readBytes(folder / (L"UserGameStats_" + account + L"_" + std::to_wstring(app_id) + L".bin"), 4 * 1024 * 1024));
        // Global shares the game page cache already has.
        for (const auto& item : readCachedAchievements(steam_root, steam_id, app_id).items)
            if (item.global_percent >= 0) shares[item.id] = item.global_percent;
    }
    const auto* cache = user ? user->child("cache") : nullptr;
    for (const auto& stat : stats->children) {
        const auto* bits = stat.object ? stat.child("bits") : nullptr;
        if (!bits) continue;
        const auto* state = cache ? cache->child(stat.name) : nullptr;
        const uint32_t data = state ? unlockBits(state->child("data")) : 0;
        const auto* times = state ? state->child("AchievementTimes") : nullptr;
        for (const auto& bit : bits->children) {
            if (!bit.object) continue;
            int64_t index = nodeNumber(bit.child("bit"));
            if (index < 0) index = integer(bit.name);
            if (index < 0 || index > 31) continue;
            SteamAchievement item;
            item.id = bit.child("name") ? bit.child("name")->text : std::string{};
            if (item.id.empty()) continue;
            const auto* display = bit.child("display");
            item.name = localized(display ? display->child("name") : nullptr, english);
            item.description = localized(display ? display->child("desc") : nullptr, english);
            item.hidden = display && nodeNumber(display->child("hidden")) > 0;
            item.achieved = ((data >> index) & 1u) != 0;
            if (item.achieved && times) item.unlocked_at = static_cast<uint64_t>(std::max<int64_t>(0, nodeNumber(times->child(std::to_string(index)))));
            item.icon = schemaIcon(app_id, display ? display->child("icon") : nullptr);
            item.gray_icon = schemaIcon(app_id, display ? display->child("icon_gray") : nullptr);
            if (!item.achieved && !item.gray_icon.empty()) item.icon = item.gray_icon;
            if (const auto found = shares.find(item.id); found != shares.end()) item.global_percent = found->second;
            result.unlocked += item.achieved;
            result.items.push_back(std::move(item));
        }
    }
    result.total = static_cast<uint32_t>(result.items.size());
    result.complete = !result.items.empty();
    sortAchievements(result.items);
    return result;
}
}

SteamAchievements readLocalAchievements(const std::filesystem::path& steam_root, uint64_t steam_id, uint32_t app_id, bool english) {
    return steam_id ? readSchemaList(steam_root, steam_id, app_id, english) : SteamAchievements{};
}

SteamAchievements readSteamSchema(const std::filesystem::path& steam_root, uint32_t app_id, bool english) {
    return readSchemaList(steam_root, 0, app_id, english);
}

std::pair<uint32_t, uint32_t> localAchievementCounts(const std::filesystem::path& steam_root, uint64_t steam_id, uint32_t app_id) {
    const auto list = readLocalAchievements(steam_root, steam_id, app_id, true);
    return {list.unlocked, list.total};
}

std::vector<std::pair<std::string, float>> fetchGlobalPercentages(uint32_t app_id, const SteamHttp& get) {
    std::vector<std::pair<std::string, float>> result;
    if (!app_id) return result;
    const auto global = fetchJson(get, L"https://api.steampowered.com/ISteamUserStats/GetGlobalAchievementPercentagesForApp/v2/?gameid=" + std::to_wstring(app_id));
    const auto* holder = global ? global->get("achievementpercentages") : nullptr;
    const auto* list = holder ? holder->get("achievements") : nullptr;
    if (!list || list->type != json::Value::Type::array) return result;
    for (const auto& entry : list->elements) {
        const auto* name = entry.get("name");
        const auto percent = number(entry.get("percent"));
        if (name && percent >= 0 && percent <= 100) result.emplace_back(name->stringOr(), static_cast<float>(percent));
    }
    return result;
}

void applyGlobalPercentages(SteamAchievements& list, const std::vector<std::pair<std::string, float>>& percentages) {
    std::unordered_map<std::string, float> shares(percentages.begin(), percentages.end());
    for (auto& item : list.items) if (const auto found = shares.find(item.id); found != shares.end()) item.global_percent = found->second;
    sortAchievements(list.items);
}

SteamAchievements readCachedAchievements(const std::filesystem::path& steam_root, uint64_t steam_id, uint32_t app_id) {
    SteamAchievements result;
    if (steam_id <= steam_id_offset || !app_id) return result;
    const auto file = steam_root / L"userdata" / std::to_wstring(steam_id - steam_id_offset) / L"config" / L"librarycache" /
                      (std::to_wstring(app_id) + L".json");
    std::ifstream input(file, std::ios::binary | std::ios::ate);
    if (!input || input.tellg() <= 0 || input.tellg() > 4 * 1024 * 1024) return result;
    std::string bytes(static_cast<size_t>(input.tellg()), '\0');
    input.seekg(0);
    if (!input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()))) return result;
    const auto root = json::parse(bytes);
    if (!root || root->type != json::Value::Type::array) return result;
    for (const auto& section : root->elements) {
        if (section.type != json::Value::Type::array || section.elements.size() < 2 || section.elements[0].stringOr() != "achievements") continue;
        const auto* data = section.elements[1].get("data");
        if (!data || data->type != json::Value::Type::object) break;
        const auto total = number(data->get("nTotal")), unlocked = number(data->get("nAchieved"));
        result.total = total > 0 && total < 100000 ? static_cast<uint32_t>(total) : 0;
        result.unlocked = unlocked > 0 ? std::min(result.total, static_cast<uint32_t>(unlocked)) : 0;
        // vecHighlight, vecUnachieved, vecAchievedHidden, ...: whatever lists the client kept.
        // Unlocked entries are taken first, so an achievement listed twice counts as unlocked.
        std::unordered_set<std::string> seen;
        for (const bool unlocked_pass : {true, false})
        for (const auto& [name, list] : data->properties) {
            if (!name.starts_with("vec") || list.type != json::Value::Type::array) continue;
            for (const auto& entry : list.elements) {
                if (flag(entry.get("bAchieved")) != unlocked_pass) continue;
                SteamAchievement item;
                item.id = entry.get("strID") ? entry.get("strID")->stringOr() : std::string{};
                if (item.id.empty() || !seen.insert(item.id).second) continue;
                item.name = text(entry.get("strName"));
                item.description = text(entry.get("strDescription"));
                item.icon = httpsUrl(entry.get("strImage"));
                item.achieved = flag(entry.get("bAchieved"));
                item.hidden = flag(entry.get("bHidden"));
                const auto unlocked_at = number(entry.get("rtUnlocked"));
                item.unlocked_at = item.achieved && unlocked_at > 0 ? static_cast<uint64_t>(unlocked_at) : 0;
                const auto percent = number(entry.get("flAchieved"));
                item.global_percent = percent >= 0 && percent <= 100 ? static_cast<float>(percent) : -1.0f;
                result.items.push_back(std::move(item));
            }
        }
        break;
    }
    result.complete = result.total && result.items.size() >= result.total;
    sortAchievements(result.items);
    return result;
}

namespace {
bool usableKey(const std::wstring& key) { return !key.empty() && key.size() <= 1024 && key.find_first_of(L"\r\n&?#") == std::wstring::npos; }
std::optional<json::Value> schemaJson(const std::wstring& key, uint32_t app_id, bool english, const SteamHttp& get) {
    return fetchJson(get, L"https://api.steampowered.com/ISteamUserStats/GetSchemaForGame/v2/?key=" + key +
                          L"&appid=" + std::to_wstring(app_id) + (english ? L"&l=english" : L"&l=polish"));
}
const json::Value* schemaAchievements(const std::optional<json::Value>& schema) {
    const auto* game = schema ? schema->get("game") : nullptr;
    const auto* stats = game ? game->get("availableGameStats") : nullptr;
    const auto* achievements = stats ? stats->get("achievements") : nullptr;
    return achievements && achievements->type == json::Value::Type::array && !achievements->elements.empty() ? achievements : nullptr;
}
SteamAchievement schemaItem(const json::Value& entry) {
    SteamAchievement item;
    item.id = entry.get("name") ? entry.get("name")->stringOr() : std::string{};
    item.name = text(entry.get("displayName"));
    item.description = text(entry.get("description"));
    item.icon = httpsUrl(entry.get("icon"));
    item.hidden = flag(entry.get("hidden"));
    return item;
}
}

SteamAchievements fetchSteamSchema(const std::wstring& key, uint32_t app_id, bool english, const SteamHttp& get) {
    SteamAchievements result;
    if (!usableKey(key) || !app_id) return result;
    const auto schema = schemaJson(key, app_id, english, get);
    const auto* achievements = schemaAchievements(schema);
    if (!achievements) return result;
    for (const auto& entry : achievements->elements) {
        auto item = schemaItem(entry);
        if (item.id.empty()) continue;
        if (const auto gray = httpsUrl(entry.get("icongray")); !gray.empty()) item.gray_icon = gray;
        result.items.push_back(std::move(item));
    }
    result.total = static_cast<uint32_t>(result.items.size());
    result.complete = !result.items.empty();
    return result;
}

SteamAchievements fetchSteamAchievements(const std::wstring& key, uint64_t steam_id, uint32_t app_id, bool english, const SteamHttp& get) {
    SteamAchievements result;
    if (!usableKey(key) || !steam_id || !app_id) return result;
    const auto app = std::to_wstring(app_id);
    const auto schema = schemaJson(key, app_id, english, get);
    const auto* achievements = schemaAchievements(schema);
    if (!achievements) return result;
    const auto player = fetchJson(get, L"https://api.steampowered.com/ISteamUserStats/GetPlayerAchievements/v1/?key=" + key +
                                       L"&steamid=" + std::to_wstring(steam_id) + L"&appid=" + app);
    const auto* player_stats = player ? player->get("playerstats") : nullptr;
    const auto* unlocks = player_stats ? player_stats->get("achievements") : nullptr;
    if (!player_stats || !flag(player_stats->get("success")) || !unlocks || unlocks->type != json::Value::Type::array) return result;
    std::unordered_map<std::string, std::pair<bool, uint64_t>> state;
    for (const auto& entry : unlocks->elements) {
        const auto* name = entry.get("apiname");
        if (!name) continue;
        const auto time = number(entry.get("unlocktime"));
        state[name->stringOr()] = {flag(entry.get("achieved")), time > 0 ? static_cast<uint64_t>(time) : 0};
    }
    const auto global = fetchGlobalPercentages(app_id, get); // Optional: public numbers.
    const std::unordered_map<std::string, float> percentages(global.begin(), global.end());
    for (const auto& entry : achievements->elements) {
        auto item = schemaItem(entry);
        if (item.id.empty()) continue;
        if (const auto found = state.find(item.id); found != state.end()) {
            item.achieved = found->second.first;
            item.unlocked_at = item.achieved ? found->second.second : 0;
        }
        if (const auto found = percentages.find(item.id); found != percentages.end()) item.global_percent = found->second;
        result.unlocked += item.achieved;
        result.items.push_back(std::move(item));
    }
    result.total = static_cast<uint32_t>(result.items.size());
    result.complete = true;
    sortAchievements(result.items);
    return result;
}

void sortAchievements(std::vector<SteamAchievement>& items) {
    std::stable_sort(items.begin(), items.end(), [](const SteamAchievement& a, const SteamAchievement& b) {
        if (a.achieved != b.achieved) return a.achieved;
        if (a.achieved) return a.unlocked_at > b.unlocked_at;
        if (a.hidden != b.hidden) return b.hidden;
        return a.global_percent > b.global_percent;
    });
}

std::wstring achievementIconFile(const std::wstring& url) {
    const auto slash = url.find_last_of(L'/');
    if (!url.starts_with(L"https://") || slash == std::wstring::npos) return {};
    auto name = url.substr(slash + 1);
    if (const auto query = name.find_first_of(L"?#"); query != std::wstring::npos) name.resize(query);
    if (name.empty() || name.size() > 96 || !(name.ends_with(L".jpg") || name.ends_with(L".png"))) return {};
    for (const wchar_t ch : name)
        if (!((ch >= L'0' && ch <= L'9') || (ch >= L'a' && ch <= L'z') || (ch >= L'A' && ch <= L'Z') || ch == L'.' || ch == L'_' || ch == L'-')) return {};
    if (name.find(L"..") != std::wstring::npos) return {};
    return name;
}
