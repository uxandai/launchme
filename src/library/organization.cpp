#include "organization.hpp"
#include <algorithm>
#include <charconv>
#include <set>
#include <unordered_map>

namespace {
std::string scope(uint64_t account, const SteamCollection& c, const Game& game) {
    return favoriteCollection(c) && game.source != GameSource::steam ? "favorites:local" : collectionKey(account, c);
}
bool inherited(const SteamCollection& c, const Game& game) {
    return game.source == GameSource::steam && std::find(c.app_ids.begin(), c.app_ids.end(), game.app_id) != c.app_ids.end();
}
std::string memberKey(uint64_t account, const SteamCollection& c, const Game& game) {
    return localCollection(c) && game.source == GameSource::steam ? "steam-user:" + std::to_string(account) + '/' + gameKey(game) : gameKey(game);
}
}
std::vector<SteamCollection> managedCollections(uint64_t account, const std::vector<SteamCollection>& steam, const CollectionEdits& edits) {
    std::vector<SteamCollection> result{{tr(L"Ulubione"), {}, "user-collections.favorite"}};
    for (const auto& c : steam) {
        if (favoriteCollection(c)) result[0].app_ids = c.app_ids;
        else result.push_back(c);
    }
    for (const auto& [id, name] : edits.names)
        if (id.starts_with("local:")) result.push_back({name, {}, id});
    const auto prefix = std::to_string(account) + ':';
    for (const auto& scope : edits.creates) {
        if (!scope.starts_with(prefix)) continue;
        const auto id = scope.substr(prefix.size());
        const auto name = edits.names.find(scope);
        if (name != edits.names.end() && std::none_of(result.begin(), result.end(), [&](const auto& c) { return c.id == id; }))
            result.push_back({name->second, {}, id});
    }
    for (auto& c : result) {
        if (favoriteCollection(c)) continue;
        if (const auto it = edits.names.find(collectionKey(account, c)); it != edits.names.end()) c.name = it->second;
    }
    return result;
}
bool collectionContains(const CollectionEdits& edits, uint64_t account, const SteamCollection& c, const Game& game, const SmartMembers* smart) {
    if (!isGame(game) || game.kind == GameKind::catalog) return false;
    if (const auto group = edits.members.find(scope(account, c, game)); group != edits.members.end())
        if (const auto it = group->second.find(memberKey(account, c, game)); it != group->second.end()) return it->second;
    if (smart) if (const auto rules = smart->find(c.id); rules != smart->end()) return rules->second.contains(gameKey(game));
    return inherited(c, game);
}
void setCollectionMember(CollectionEdits& edits, uint64_t account, const SteamCollection& c, const Game& game, bool member) {
    const auto key = gameKey(game);
    if (key.empty() || !isGame(game) || game.kind == GameKind::catalog) return;
    // Explicit overrides are retained even when they happen to agree with Steam now.
    // A subsequent Steam refresh must not undo the user's local choice.
    edits.members[scope(account, c, game)][memberKey(account, c, game)] = member;
}
std::vector<uint32_t> localSteamMembers(const CollectionEdits& edits, uint64_t account) {
    std::set<uint32_t> ids;
    const auto prefix = std::to_string(account) + ':';
    const auto local_prefix = "steam-user:" + std::to_string(account) + "/steam:";
    for (const auto& [group, members] : edits.members) {
        const bool local = group.starts_with("local:");
        if (!local && !group.starts_with(prefix)) continue;
        for (const auto& [key, member] : members) {
            const std::string_view wanted = local ? std::string_view(local_prefix) : std::string_view("steam:");
            if (!member || !key.starts_with(wanted)) continue;
            const auto text = std::string_view(key).substr(wanted.size()); uint32_t id = 0;
            const auto parsed = std::from_chars(text.data(), text.data() + text.size(), id);
            if (parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size() && id && !steamShortcutId(id)) ids.insert(id);
        }
    }
    return {ids.begin(), ids.end()};
}
std::vector<SteamShelf> managedShelves(uint64_t account, const std::vector<SteamCollection>& collections,
    const CollectionEdits& edits, const std::vector<Game>& visible, bool include_private, const SmartMembers* smart) {
    std::vector<SteamShelf> result;
    std::unordered_map<std::string, size_t> positions;
    std::unordered_map<uint32_t, size_t> steam;
    for (size_t i = 0; i < visible.size(); ++i) {
        const auto& game = visible[i];
        if (!isGame(game) || game.kind == GameKind::catalog || (!include_private && game.visibility != GameVisibility::public_game)) continue;
        positions.emplace(gameKey(game), i);
        if (game.source == GameSource::steam) steam.emplace(game.app_id, i);
    }
    const auto steam_prefix = "steam-user:" + std::to_string(account) + '/';
    for (const auto& c : collections) {
        SteamShelf shelf{c.name, {}, c.id};
        std::set<size_t> members;
        for (const auto id : c.app_ids) if (const auto it = steam.find(id); it != steam.end()) members.insert(it->second);
        if (smart) if (const auto rules = smart->find(c.id); rules != smart->end())
            for (const auto& key : rules->second) if (const auto it = positions.find(key); it != positions.end()) members.insert(it->second);
        const auto apply = [&](const std::string& overlay_scope) {
            const auto overrides = edits.members.find(overlay_scope);
            if (overrides == edits.members.end()) return;
            for (const auto& [saved_key, included] : overrides->second) {
                std::string key = saved_key;
                if (localCollection(c) && key.starts_with("steam-user:")) {
                    if (!key.starts_with(steam_prefix)) continue;
                    key.erase(0, steam_prefix.size());
                }
                const auto game = positions.find(key);
                if (game == positions.end()) continue;
                if (scope(account, c, visible[game->second]) != overlay_scope || memberKey(account, c, visible[game->second]) != saved_key) continue;
                if (included) members.insert(game->second); else members.erase(game->second);
            }
        };
        apply(collectionKey(account, c));
        if (favoriteCollection(c)) apply("favorites:local");
        for (const auto index : members) shelf.games.push_back(visible[index]);
        if (!shelf.games.empty()) result.push_back(std::move(shelf));
    }
    return result;
}
