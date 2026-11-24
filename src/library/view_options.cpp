#include "view_options.hpp"
#include "search.hpp"
#ifdef _WIN32
#include <Windows.h>
#endif
#include <algorithm>
#include <string_view>

namespace {
// "Split Screen Co-op" -> "splitscreencoop": letters and digits, lower case.
std::wstring foldedTag(const std::wstring& tag) {
    std::wstring out;
    for (const wchar_t c : tag) {
        if (c >= L'A' && c <= L'Z') out += static_cast<wchar_t>(c - L'A' + L'a');
        else if ((c >= L'a' && c <= L'z') || (c >= L'0' && c <= L'9')) out += c;
    }
    return out;
}
bool has(std::wstring_view text, std::wstring_view part) { return text.find(part) != std::wstring_view::npos; }
}

// Steam's store tags and features (Co-op, Online Co-op, Shared/Split Screen, Full controller support,
// 4 Player Local…) and RAWG's (Singleplayer, Multiplayer, Local Co-Op, Controller support…).
bool library_view::tagged(Filter filter, const std::vector<std::wstring>& tags) {
    for (const auto& tag : tags) {
        const auto t = foldedTag(tag);
        switch (filter) {
        case Filter::single_player: if (t == L"singleplayer") return true; break;
        case Filter::multiplayer:
            if (has(t, L"multiplayer") || has(t, L"coop") || has(t, L"pvp") || t == L"mmo" || has(t, L"splitscreen") || has(t, L"playerlocal")) return true;
            break;
        case Filter::coop: if (has(t, L"coop") || t == L"cooperative") return true; break;
        case Filter::local:
            if (has(t, L"splitscreen") || has(t, L"localcoop") || has(t, L"localmultiplayer") || has(t, L"playerlocal") || has(t, L"couchcoop")) return true;
            break;
        case Filter::online:
            if (has(t, L"online") || t == L"mmo" || has(t, L"massivelymultiplayer") || has(t, L"crossplatformmultiplayer")) return true;
            break;
        case Filter::controller: if (has(t, L"controller")) return true; break;
        case Filter::vr: if (t == L"vr" || t == L"vrsupported" || t == L"vronly" || t == L"vrsupport") return true; break;
        case Filter::unplayed: return false;
        }
    }
    return false;
}

std::vector<Game> library_view::arrange(const std::vector<Game>& games, Options options, const std::map<std::string, uint64_t>& first_seen,
                                        bool include_private, const std::unordered_map<std::string, int>* released) {
    // Sorted as pointers with the "added" time (or release date) looked up once per game: the
    // comparator used to rebuild gameKey and search first_seen on every comparison, and to move Games.
    struct Entry { const Game* game; uint64_t added; };
    std::vector<Entry> entries;
    entries.reserve(games.size());
    for (const auto& game : games) {
        if ((!include_private && game.visibility != GameVisibility::public_game) || (options.installed_only && isGame(game) && !game.installed)) continue;
        uint64_t added = 0;
        if (options.order == Order::added) added = addedTime(game, first_seen);
        if (options.order == Order::release && released)
            if (const auto found = released->find(gameKey(game)); found != released->end() && found->second > 0) added = static_cast<uint64_t>(found->second);
        entries.push_back({&game, added});
    }
    std::stable_sort(entries.begin(), entries.end(), [&](const Entry& left, const Entry& right) {
        const Game& a = *left.game;
        const Game& b = *right.game;
        if (a.kind == GameKind::back || b.kind == GameKind::back) return a.kind == GameKind::back && b.kind != GameKind::back;
        if (options.order == Order::last_played && a.last_played != b.last_played) return a.last_played > b.last_played;
        if ((options.order == Order::added || options.order == Order::release) && left.added != right.added) return left.added > right.added;
        // As a person reads titles: Ł after L, "Game 2" before "Game 10" (library/search.hpp).
        if (const int names = compareTitles(a.name, b.name)) return names < 0;
        return gameKey(a) < gameKey(b);
    });
    std::vector<Game> result;
    result.reserve(entries.size());
    for (const auto& entry : entries) result.push_back(*entry.game);
    return result;
}
