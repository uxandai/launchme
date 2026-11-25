#include "collections.hpp"
#include "visibility.hpp"
#include "../net/json.hpp"
#include "../platform/text_encoding.hpp"

#ifdef _WIN32
#include <Windows.h>
#endif

#include <algorithm>
#include <charconv>
#include <cmath>
#include <fstream>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace {
constexpr uint64_t steam_id_offset = 76561197960265728ULL;
constexpr std::string_view collection_prefix = "user-collections.";

using text_encoding::wide;

// Unsigned 32-bit IDs, as JSON numbers or strings: non-Steam shortcut IDs are above 2^31.
std::vector<uint32_t> appIds(const json::Value* value) {
    std::vector<uint32_t> ids;
    if (!value || value->type != json::Value::Type::array) return ids;
    for (const auto& element : value->elements) {
        uint64_t number = 0;
        if (element.type == json::Value::Type::number) {
            if (element.numeric >= 1 && element.numeric <= UINT32_MAX && element.numeric == std::floor(element.numeric))
                number = static_cast<uint64_t>(element.numeric);
        } else if (element.type == json::Value::Type::string) {
            const auto* end = element.text.data() + element.text.size();
            const auto parsed = std::from_chars(element.text.data(), end, number);
            if (parsed.ec != std::errc{} || parsed.ptr != end) number = 0;
        }
        if (number && number <= UINT32_MAX) ids.push_back(static_cast<uint32_t>(number));
    }
    return ids;
}

// Members kept on this machine only (Steam stores shortcuts and mods there instead of the
// synced namespace): localconfig.vdf -> WebStorage -> "user-collections", a JSON object of
// {"id","added","removed"} by collection ID without the "user-collections." prefix. Steam
// shows each collection with both lists merged.
struct Members { std::vector<uint32_t> added, removed; };
using LocalMembers = std::unordered_map<std::string, Members>;

std::string unescapeVdf(std::string_view text) {
    std::string result;
    result.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '\\' || i + 1 == text.size()) { result.push_back(text[i]); continue; }
        const char next = text[++i];
        result.push_back(next == 'n' ? '\n' : next == 't' ? '\t' : next);
    }
    return result;
}

LocalMembers localMembers(const std::filesystem::path& config) {
    LocalMembers result;
    std::ifstream input(config / "localconfig.vdf", std::ios::binary);
    constexpr std::string_view key = "\"user-collections\"";
    std::string line;
    while (std::getline(input, line)) {
        const auto start = line.find_first_not_of(" \t");
        if (start == std::string::npos || line.compare(start, key.size(), key) != 0) continue;
        const auto open = line.find('"', start + key.size());
        const auto close = line.find_last_of('"');
        if (open == std::string::npos || close == std::string::npos || close <= open) break;
        const auto root = json::parse(unescapeVdf(std::string_view(line).substr(open + 1, close - open - 1)));
        if (root && root->type == json::Value::Type::object)
            for (const auto& [id, entry] : root->properties)
                result[id] = Members{appIds(entry.get("added")), appIds(entry.get("removed"))};
        break;
    }
    return result;
}

void addCollection(std::vector<SteamCollection>& result, std::string key, const json::Value& record,
                   const LocalMembers& local) {
    if (!key.starts_with(collection_prefix) || key == "user-collections.hidden") return;
    const bool favorite = key == "user-collections.favorite" || key == "user-collections.favorites";
    const auto* value = record.get("value");
    if (!value) return;
    std::optional<json::Value> decoded;
    if (value->type == json::Value::Type::string) decoded = json::parse(value->text);
    const auto* data = decoded ? &*decoded : value;
    if (data->type != json::Value::Type::object) return;
    if (favorite) {
        const auto existing = std::find_if(result.begin(), result.end(), [](const auto& c) { return c.id == "user-collections.favorite"; });
        if (existing != result.end()) {
            if (key == "user-collections.favorites") return; // Modern record wins over the legacy alias.
            result.erase(existing);
        }
    }
    const auto* name = data->get("name");
    std::wstring title = favorite ? std::wstring(tr(L"Ulubione")) : name ? wide(name->stringOr()) : std::wstring{};
    // Custom collection IDs (uc-...) are opaque identifiers, never display names.
    if (title.empty()) return;
    auto ids = appIds(data->get("added"));
    auto removed = appIds(data->get("removed"));
    if (const auto found = local.find(key.substr(collection_prefix.size())); found != local.end()) {
        ids.insert(ids.end(), found->second.added.begin(), found->second.added.end());
        removed.insert(removed.end(), found->second.removed.begin(), found->second.removed.end());
    }
    const std::unordered_set<uint32_t> excluded(removed.begin(), removed.end());
    std::unordered_set<uint32_t> used;
    SteamCollection collection{std::move(title)};
    collection.id = favorite ? "user-collections.favorite" : key;
    for (const auto id : ids)
        if (!excluded.contains(id) && used.insert(id).second) collection.app_ids.push_back(id);
    result.push_back(std::move(collection)); // Empty named collections still exist in the manager.
}
} // namespace

std::vector<SteamCollection> readSteamCollections(const std::filesystem::path& steam_root) {
    const auto steam_id = activeSteamId(steam_root);
    if (steam_id <= steam_id_offset) return {};
    const auto config = steam_root / "userdata" / std::to_wstring(steam_id - steam_id_offset) / "config";
    const auto path = config / "cloudstorage" / "cloud-storage-namespace-1.json";
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input || input.tellg() <= 0 || input.tellg() > 32 * 1024 * 1024) return {};
    const auto length = static_cast<size_t>(input.tellg());
    input.seekg(0);
    std::string bytes(length, '\0');
    if (!input.read(bytes.data(), static_cast<std::streamsize>(length))) return {};
    const auto root = json::parse(bytes, 32 * 1024 * 1024); // The same bound as the read: a big account's file is over the default 2 MiB.
    if (!root) return {};
    const auto local = localMembers(config);
    std::vector<SteamCollection> result;
    if (root->type == json::Value::Type::array) {
        for (const auto& pair : root->elements) {
            if (pair.type != json::Value::Type::array || pair.elements.size() < 2) continue;
            addCollection(result, pair.elements[0].stringOr(), pair.elements[1], local);
        }
    } else if (root->type == json::Value::Type::object) {
        for (const auto& [key, value] : root->properties) addCollection(result, key, value, local);
        std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) { return a.name < b.name; });
    }
    return result;
}

std::vector<SteamShelf> publicSteamShelves(const std::vector<SteamCollection>& collections,
                                          const std::vector<Game>& visible,
                                          const std::unordered_set<uint32_t>& excluded,
                                          bool include_private) {
    // Pointers into `visible`: the index only picks games; each shelf copies its own.
    std::unordered_map<uint32_t, const Game*> public_games;
    public_games.reserve(visible.size());
    for (const auto& game : visible)
        if (game.source == GameSource::steam && isGame(game) && (include_private ||
            (game.visibility == GameVisibility::public_game && !excluded.contains(game.app_id))))
            public_games.emplace(game.app_id, &game);
    std::vector<SteamShelf> shelves;
    for (const auto& collection : collections) {
        SteamShelf shelf{collection.name, {}, collection.id};
        for (uint32_t id : collection.app_ids) {
            const auto found = public_games.find(id);
            if (found != public_games.end()) shelf.games.push_back(*found->second);
        }
        if (!shelf.games.empty()) shelves.push_back(std::move(shelf));
    }
    return shelves;
}
