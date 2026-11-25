#include "versions.hpp"
#include "search.hpp"
#include "view_options.hpp"
#include "../platform/text_encoding.hpp"
#include <algorithm>
#include <cwctype>
#include <set>

namespace versions {
bool emulatedCopy(const Game& game) {
    return game.kind == GameKind::nonsteam && !game.platform.empty() && game.platform != L"PC";
}
std::string identity(const Game& game, bool emulated) {
    if (!isGame(game) || game.kind == GameKind::catalog || placeholderGameName(game.name)) return {};
    if (game.kind == GameKind::nonsteam && !(emulated && emulatedCopy(game))) return {};
    std::wstring name;
    bool space = false;
    for (const auto ch : foldGameTitle(game.name)) {
        if (ch == L'™' || ch == L'®' || ch == L'©') continue;
        if (iswspace(ch)) { space = !name.empty(); continue; }
        if (space) name += L' ';
        space = false; name += ch;
    }
    if (name.empty()) return {};
    return std::to_string(static_cast<int>(game.visibility)) + ":" + text_encoding::utf8(name);
}
namespace {
// The file a game starts: copies under other titles (a user-added "Saya no Uta" pointing to
// GOG's "The Song of Saya" .exe) are one installation, so one game.
std::wstring executableIdentity(const Game& game) {
    if (!game.executable.is_absolute()) return {};
    auto path = game.executable.lexically_normal().wstring();
    std::replace(path.begin(), path.end(), L'/', L'\\');
    std::transform(path.begin(), path.end(), path.begin(), [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
    return std::to_wstring(static_cast<int>(game.visibility)) + L":" + path;
}
}

Index::Index(const std::vector<Game>& games, bool emulated) {
    std::vector<std::string> ids(games.size());
    std::map<std::string, std::string> parent; // Name groups joined by a shared executable.
    const auto root = [&](std::string id) {
        for (auto found = parent.find(id); found != parent.end(); found = parent.find(id)) id = found->second;
        return id;
    };
    std::map<std::wstring, std::string> executables;
    for (size_t i = 0; i < games.size(); ++i) {
        ids[i] = identity(games[i], emulated);
        if (ids[i].empty()) continue;
        const auto file = executableIdentity(games[i]);
        if (file.empty()) continue;
        const auto [found, inserted] = executables.emplace(file, ids[i]);
        if (inserted) continue;
        const auto first = root(found->second), second = root(ids[i]);
        if (first != second) parent[second] = first;
    }
    for (size_t i = 0; i < games.size(); ++i) if (!ids[i].empty()) groups[root(ids[i])].push_back(games[i]);
    std::erase_if(groups, [](const auto& entry) {
        std::set<std::pair<GameSource, std::wstring>> kinds; // A store, or a console's emulated copy.
        for (const auto& game : entry.second) kinds.insert({game.source, emulatedCopy(game) ? game.platform : std::wstring{}});
        return kinds.size() < 2;
    });
    for (const auto& [id, list] : groups) for (const auto& game : list) by_game[gameKey(game)] = id;
}
const std::vector<Game>* Index::find(const Game& game) const {
    const auto id = by_game.find(gameKey(game));
    if (id == by_game.end()) return nullptr;
    const auto group = groups.find(id->second);
    return group == groups.end() ? nullptr : &group->second;
}
std::vector<Game> Index::collapse(const std::vector<Game>& games, const std::map<std::string, std::string>& preferred,
                                 const std::map<std::string, uint64_t>& first_seen) const {
    std::vector<Game> result; result.reserve(games.size());
    std::map<std::string, size_t> slots;
    const auto rank = [&](const Game& game, const std::string& group) {
        const auto pref = preferred.find(group);
        return (pref != preferred.end() && pref->second == gameKey(game) ? 4 : 0) + (game.installed ? 2 : 0) + (game.source == GameSource::steam ? 1 : 0);
    };
    for (const auto& game : games) {
        const auto id = by_game.find(gameKey(game));
        if (id == by_game.end()) { result.push_back(game); continue; }
        const auto [slot, inserted] = slots.emplace(id->second, result.size());
        if (inserted) { result.push_back(game); result.back().version_group = id->second; }
        else {
            auto& existing = result[slot->second];
            const auto played = std::max(existing.last_played, game.last_played);
            const auto added = std::max(library_view::addedTime(existing, first_seen), library_view::addedTime(game, first_seen));
            if (rank(game, id->second) > rank(existing, id->second)) { existing = game; existing.version_group = id->second; }
            existing.last_played = played;
            existing.added_at = added;
        }
    }
    for (auto& game : result) // The player's pick shows its store or console on the tile.
        if (!game.version_group.empty())
            if (const auto pref = preferred.find(game.version_group); pref != preferred.end()) game.version_chosen = pref->second == gameKey(game);
    return result;
}
}
