#include "../src/library/steam_sync_state.hpp"
#include "../src/net/json.hpp"
#include <algorithm>
#include <iostream>

int main() {
    using namespace steam_sync;
    LibraryState state;
    state.collections.names = {{"11:user-collections.uc-one", L"New name"}, {"local:one", L"Cross-store"}, {"12:user-collections.uc-one", L"Other account"}};
    state.collections.members["11:user-collections.uc-one"] = {{"steam:620", true}, {"gog:620", true}, {"shortcut:9999999", true}};
    state.collections.members["11:user-collections.favorite"]["steam:570"] = false;
    state.collections.members["local:one"]["steam-user:11/steam:620"] = true;
    state.collections.members["favorites:local"]["gog:620"] = true;
    state.hidden = {"steam-user:11/steam:10", "steam-user:12/steam:20", "epic:10"};
    state.steam_hidden_pending = {{"steam-user:11/steam:10", false}, {"steam-user:11/steam:30", true}};
    const auto changes = pending(state, 11);
    if (changes.size() != 5) return 1;
    const auto has = [&](Kind kind, uint32_t app, bool value) {
        return std::any_of(changes.begin(), changes.end(), [&](const Change& c) { return c.kind == kind && c.app == app && c.value == value; });
    };
    if (!has(Kind::member, 620, true) || !has(Kind::member, 570, false) || !has(Kind::hidden, 10, false) || !has(Kind::hidden, 30, true)) return 2;
    // An acknowledgement of an older edit must not discard a newer contradictory one.
    state.steam_hidden_pending["steam-user:11/steam:30"] = false;
    if (acknowledge(state, 11, {Kind::hidden, {}, 30, true}) || !state.steam_hidden_pending.contains("steam-user:11/steam:30")) return 3;
    state.collections.members["11:user-collections.uc-one"]["steam:620"] = false;
    if (acknowledge(state, 11, {Kind::member, "user-collections.uc-one", 620, true})) return 4;
    if (!acknowledge(state, 11, {Kind::member, "user-collections.uc-one", 620, false}) ||
        !state.collections.members.at("11:user-collections.uc-one").contains("gog:620")) return 5;
    if (!acknowledge(state, 11, {Kind::hidden, {}, 10, false}) || state.hidden.contains("steam-user:11/steam:10") ||
        !state.hidden.contains("steam-user:12/steam:20") || !state.hidden.contains("epic:10")) return 6;
    if (acknowledge(state, 12, changes.front())) return 7;
    const std::string valid = R"({"ok":true,"account":"11","collections":[{"id":"user-collections.favorite","name":"Favorites","members":[620,570,620]}],"hidden":[10],"privateApps":[20],"ack":[0],"error":"","cloudPending":true})";
    const auto result = parseResult(11, changes, valid);
    if (!result.snapshot || result.applied.size() != 1 || !result.cloud_pending || result.snapshot->hidden != std::set<uint32_t>{10} ||
        result.snapshot->private_apps != std::set<uint32_t>{20} || result.snapshot->collections[0].app_ids != std::vector<uint32_t>{570,620}) return 8;
    if (parseResult(12, changes, valid).snapshot || parseResult(11, {}, valid).snapshot || parseResult(11, changes, "{}").snapshot) return 9;
    const auto queued = pending(state, 11);
    for (const auto& c : queued) acknowledge(state, 11, c);
    if (!pending(state, 11).empty() || state.collections.names.at("local:one") != L"Cross-store" ||
        !state.collections.members.at("local:one").contains("steam-user:11/steam:620")) return 10;
    const auto expression = requestExpression(11, {{Kind::name, "user-collections.uc-one", 0, false, L"Name \"quoted\"\\line\nnext"}});
    const auto begin = expression.rfind(")({");
    const std::string suffix = ").then(JSON.stringify)";
    if (begin == std::string::npos || !expression.ends_with(suffix)) return 11;
    const auto request = json::parse(expression.substr(begin + 2, expression.size() - suffix.size() - begin - 2));
    const auto* list = request ? request->get("changes") : nullptr;
    if (!list || list->elements.size() != 1 || !list->elements[0].get("name") ||
        list->elements[0].get("name")->stringOr() != "Name \"quoted\"\\line\nnext") return 11;
    {
        LibraryState offline; AppSettings settings;
        const std::string local = "local:yakuza", id = "user-collections.uc-launchme-0123456789abcdef", scope = "11:" + id;
        offline.collections.names[local] = L"Yakuza";
        offline.collections.members[local] = {{"steam-user:11/steam:620", true}, {"gog:123", true}};
        offline.bookmark = LibraryBookmark{11, LibraryPlace::library, local, "steam:620", "category"};
        offline.options["11:category:" + local].view = library_view::View::overview;
        settings.hidden_collections.insert(local); settings.collection_order = {local};
        const auto original = offline;
        if (moveCollection(offline, settings, 12, local, id) || offline != original) return 12;
        if (!moveCollection(offline, settings, 11, local, id) || offline.collections.names.contains(local) ||
            offline.collections.names.at(scope) != L"Yakuza" || !offline.collections.members.at(scope).contains("steam:620") ||
            !offline.collections.members.at(scope).contains("gog:123") || settings.collection_order != std::vector<std::string>{scope} ||
            !settings.hidden_collections.contains(scope) || offline.bookmark->category != id ||
            offline.options.at("11:category:" + id).view != library_view::View::overview) return 13;
        const auto collections = managedCollections(11, {}, offline.collections);
        if (collections.size() != 2 || collections[1].id != id || collections[1].name != L"Yakuza" ||
            managedCollections(12, {}, offline.collections).size() != 1) return 14;
        const auto create = pending(offline, 11);
        if (create.size() != 1 || create[0].kind != Kind::create || !pending(offline, 12).empty()) return 15;
        // Rename/member edits remain local and usable while the create is in flight/offline.
        offline.collections.names[scope] = L"Yakuza renamed offline";
        if (!acknowledge(offline, 11, create[0]) || offline.collections.creates.contains(scope) ||
            offline.collections.names.at(scope) != L"Yakuza renamed offline") return 16;
        const auto rest = pending(offline, 11);
        if (rest.size() != 2 || rest[0].kind != Kind::name || rest[1].kind != Kind::member || rest[1].app != 620) return 17;
        const SteamCollection native{L"Yakuza", {}, id};
        if (managedCollections(11, {native}, offline.collections).size() != 2) return 18;
    }
    {
        // Apps Steam has no record of (removed demos): the edit stays local and leaves the queue.
        LibraryState local;
        local.hidden = {"steam-user:11/steam:3564860"};
        local.steam_hidden_pending = {{"steam-user:11/steam:3564860", true}};
        local.collections.members["11:user-collections.favorite"]["steam:3595150"] = false;
        auto outbox = pending(local, 11);
        if (outbox.size() != 2) return 20;
        const std::string reply = R"({"ok":true,"account":"11","collections":[],"hidden":[],"privateApps":[],"ack":[],"unavailable":[0,1],"error":""})";
        const auto parsed = parseResult(11, outbox, reply);
        if (!parsed.snapshot || !parsed.error.empty() || parsed.unavailable.size() != 2 || !parsed.applied.empty()) return 21;
        for (const auto& c : parsed.unavailable) if (!keepLocal(local, 11, c)) return 22;
        if (!pending(local, 11).empty() || unsyncedCount(local, 11) != 2 || !local.hidden.contains("steam-user:11/steam:3564860") ||
            !local.steam_hidden_pending.at("steam-user:11/steam:3564860")) return 23;
        if (keepLocal(local, 11, parsed.unavailable[0]) || pruneUnsynced(local, 11)) return 24; // Nothing changes twice.
        // Shown again: Steam cannot hide what it does not know, so that edit is simply done.
        local.steam_hidden_pending["steam-user:11/steam:3564860"] = false; local.hidden.clear();
        outbox = pending(local, 11);
        if (outbox.size() != 1 || outbox[0].kind != Kind::hidden || outbox[0].value) return 25;
        if (!keepLocal(local, 11, outbox[0]) || local.steam_hidden_pending.contains("steam-user:11/steam:3564860") || unsyncedCount(local, 11) != 1) return 26;
        // A membership edit undone in Launchme forgets its local mark.
        local.collections.members.erase("11:user-collections.favorite");
        if (!pruneUnsynced(local, 11) || unsyncedCount(local, 11) != 0) return 27;
        const std::string overlap = R"({"ok":true,"account":"11","collections":[],"hidden":[],"privateApps":[],"ack":[0],"unavailable":[0],"error":""})";
        if (parseResult(11, {{Kind::hidden, {}, 10, true}}, overlap).snapshot) return 28;
        const std::string create = R"({"ok":true,"account":"11","collections":[],"hidden":[],"privateApps":[],"ack":[],"unavailable":[0],"error":""})";
        if (parseResult(11, {{Kind::create, "user-collections.uc-launchme-0123456789abcdef", 0, false, L"X"}}, create).snapshot) return 29;
    }
    std::cout << "Steam outbox scoping, acknowledgements, snapshots and escaping passed\n";
}
