#include "steam_friends.hpp"
#include "../i18n/i18n.hpp"
#include "../net/http.hpp"
#include "../net/json.hpp"
#include "../platform/user_config.hpp"
#include "../platform/atomic_file.hpp"
#include "../platform/text_encoding.hpp"

#ifdef _WIN32
#include <Windows.h>
#endif

#include <algorithm>
#include <optional>
#include <fstream>
#include <unordered_map>

namespace {
// Persona names are user-typed display text; never drop one for a bad byte.
std::wstring wide(std::string_view text) { return text_encoding::wideLenient(text); }

const json::Value* member(const json::Value* object, std::string_view field) {
    return object ? object->get(field) : nullptr;
}

std::string string(const json::Value* object, std::string_view field) {
    const auto* value = member(object, field);
    return value ? value->stringOr() : std::string{};
}

uint64_t steamId(const std::string& id) {
    try { return std::stoull(id); } catch (...) { return 0; }
}

std::optional<json::Value> steamJson(const std::wstring& uri) {
    const auto response = httpGet(uri, L"Accept: application/json\r\n", 2 * 1024 * 1024);
    if (response.status != 200) return std::nullopt;
    return json::parse(std::string_view(reinterpret_cast<const char*>(response.body.data()),
                                        response.body.size()));
}

bool image(const std::vector<uint8_t>& bytes) {
    return bytes.size() > 128 && ((bytes[0] == 0xff && bytes[1] == 0xd8) ||
           (bytes[0] == 0x89 && bytes[1] == 'P' && bytes[2] == 'N' && bytes[3] == 'G'));
}
} // namespace

FriendsResult fetchSteamFriends(uint64_t steam_id) {
    if (!steam_id) return {{}, tr(L"Nie znaleziono aktywnego konta Steam")};
    const auto key = userSecret(L"STEAM_WEB_API_KEY", L"steam_web_api_key.txt");
    if (key.empty()) return {{}, tr(L"Skonfiguruj klucz Steam Web API w ustawieniach użytkownika")};
    const auto friend_url = L"https://api.steampowered.com/ISteamUser/GetFriendList/v1/?key=" +
                            key + L"&steamid=" + std::to_wstring(steam_id) + L"&relationship=friend";
    const auto reply = httpGet(friend_url, L"Accept: application/json\r\n", 1024 * 1024);
    if (reply.status == 401 || reply.status == 403) return {{}, tr(L"Lista znajomych jest prywatna")};
    if (reply.status != 200) return {{}, tr(L"Nie można pobrać znajomych ze Steam")};
    const auto data = json::parse(std::string_view(reinterpret_cast<const char*>(reply.body.data()),
                                                   reply.body.size()));
    const auto* friends = member(member(data ? &*data : nullptr, "friendslist"), "friends");
    if (!friends || friends->type != json::Value::Type::array) return {{}, tr(L"Brak dostępu do listy znajomych")};
    FriendsResult result;
    std::unordered_map<uint64_t, size_t> positions;
    for (const auto& item : friends->elements) {
        const uint64_t id = steamId(string(&item, "steamid"));
        if (!id || positions.contains(id)) continue;
        positions[id] = result.friends.size();
        result.friends.push_back(SteamFriend{id, tr(L"Znajomy Steam")});
    }
    for (size_t offset = 0; offset < result.friends.size(); offset += 100) {
        std::wstring ids;
        for (size_t i = offset; i < std::min(result.friends.size(), offset + 100); ++i) {
            if (!ids.empty()) ids += L',';
            ids += std::to_wstring(result.friends[i].id);
        }
        const auto profiles = steamJson(L"https://api.steampowered.com/ISteamUser/GetPlayerSummaries/v2/?key=" +
                                        key + L"&steamids=" + ids);
        const auto* players = member(member(profiles ? &*profiles : nullptr, "response"), "players");
        if (!players || players->type != json::Value::Type::array) continue;
        for (const auto& profile : players->elements) {
            const auto at = positions.find(steamId(string(&profile, "steamid")));
            if (at == positions.end()) continue;
            auto& person = result.friends[at->second];
            const auto name = string(&profile, "personaname");
            if (!name.empty()) person.name = wide(name);
            person.game = wide(string(&profile, "gameextrainfo"));
            person.avatar_url = wide(string(&profile, "avatarmedium"));
            if (const auto* state = profile.get("personastate")) person.status = state->intOr();
        }
    }
    sortFriends(result.friends);
    return result;
}

void sortFriends(std::vector<SteamFriend>& friends) {
    std::stable_sort(friends.begin(), friends.end(), [](const SteamFriend& a, const SteamFriend& b) {
        const int left = !a.game.empty() ? 2 : a.status ? 1 : 0;
        const int right = !b.game.empty() ? 2 : b.status ? 1 : 0;
        if (left != right) return left > right;
        return a.name < b.name;
    });
}

namespace {
// The text of the first <name>…</name> in `xml` (CDATA unwrapped); nullopt without the element.
std::optional<std::string_view> element(std::string_view xml, std::string_view name) {
    const auto open = xml.find("<" + std::string(name) + ">");
    if (open == std::string_view::npos) return std::nullopt;
    auto value = xml.substr(open + name.size() + 2);
    const auto close = value.find("</" + std::string(name) + ">");
    if (close == std::string_view::npos) return std::nullopt;
    value = value.substr(0, close);
    if (value.starts_with("<![CDATA[") && value.ends_with("]]>")) value = value.substr(9, value.size() - 12);
    return value;
}
} // namespace

ProfilePresence profilePresence(std::string_view xml) {
    ProfilePresence presence;
    const auto state = element(xml, "onlineState");
    if (!state) return presence;
    presence.known = true;
    presence.status = *state == "online" || *state == "in-game" ? 1 : 0;
    if (*state == "in-game")
        if (const auto info = element(xml, "inGameInfo"))
            if (const auto game = element(*info, "gameName")) presence.game = wide(game->substr(0, 256));
    // Without <inGameInfo> the state message names the game ("In-Game<br/>Name", "In non-Steam game").
    if (*state == "in-game" && presence.game.empty())
        if (auto message = element(xml, "stateMessage")) {
            if (const auto line = message->find("<br/>"); line != std::string_view::npos) message = message->substr(line + 5);
            presence.game = wide(message->substr(0, 256));
        }
    presence.avatar_url = profileAvatarUrl(xml);
    return presence;
}

std::wstring profileAvatarUrl(std::string_view xml) {
    const auto value = element(xml, "avatarMedium");
    if (!value) return {};
    const auto url = wide(*value);
    const bool steam_cdn = url.starts_with(L"https://avatars.steamstatic.com/") || url.starts_with(L"https://avatars.akamai.steamstatic.com/") ||
                           url.starts_with(L"https://avatars.cloudflare.steamstatic.com/") || url.starts_with(L"https://avatars.fastly.steamstatic.com/");
    return steam_cdn && url.size() < 512 && url.find_first_of(L" \"<>\r\n") == std::wstring::npos ? url : std::wstring{};
}

std::vector<uint8_t> fetchFriendAvatar(const SteamFriend& info, const std::filesystem::path& cache, const std::filesystem::path& steam_root) {
    if (!info.id) return {};
    const auto path = cache / (std::to_wstring(info.id) + L".img");
    const auto read = [](const std::filesystem::path& file) {
        std::ifstream input(file, std::ios::binary);
        std::vector<uint8_t> bytes;
        if (input) bytes.assign(std::istreambuf_iterator<char>{input}, {});
        return image(bytes) && bytes.size() <= 2 * 1024 * 1024 ? bytes : std::vector<uint8_t>{};
    };
    if (auto bytes = read(path); !bytes.empty()) return bytes;
    auto url = info.avatar_url;
    if (!url.starts_with(L"https://")) {
        // The Steam client keeps its friends' avatars; no request at all.
        if (!steam_root.empty()) if (auto bytes = read(steam_root / L"config" / L"avatarcache" / (std::to_wstring(info.id) + L".png")); !bytes.empty()) {
            atomic_file::write(path, bytes);
            return bytes;
        }
        const auto profile = httpGet(L"https://steamcommunity.com/profiles/" + std::to_wstring(info.id) + L"/?xml=1",
                                     L"Accept: text/xml\r\n", 256 * 1024);
        if (profile.status != 200) return {};
        url = profileAvatarUrl(std::string_view(reinterpret_cast<const char*>(profile.body.data()), profile.body.size()));
        if (url.empty()) return {};
    }
    auto bytes = httpGet(url, L"Accept: image/jpeg, image/png\r\n", 1024 * 1024).body;
    if (!image(bytes)) return {};
    atomic_file::write(path, bytes);
    return bytes;
}
