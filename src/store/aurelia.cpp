#include "aurelia.hpp"
#include "../platform/text_encoding.hpp"
#include <algorithm>
#include <cctype>
#include <cwctype>
#include <charconv>
#include <set>

namespace aurelia {
namespace {
using Type = json::Value::Type;
using text_encoding::wideLenient;
std::optional<bool> boolean(const json::Value* value) {
    return value && value->type == Type::boolean ? std::optional(value->flag) : std::nullopt;
}
uint32_t appId(const json::Value& value) {
    const auto n = integer(value.get("app_id")); return n && *n <= UINT32_MAX ? static_cast<uint32_t>(*n) : 0;
}
std::wstring joined(const json::Value* value) {
    std::wstring out;
    if (value && value->type == Type::array) for (const auto& item : value->elements) {
        const auto part = item.type == Type::string ? item.text : text(item, "description");
        if (!part.empty()) { if (!out.empty()) out += L", "; out += wideLenient(part); }
    }
    return out;
}
}
std::optional<uint64_t> integer(const json::Value* value) {
    if (!value || (value->type != Type::number && value->type != Type::string) || value->text.empty()) return {};
    uint64_t n = 0;
    const auto [end, error] = std::from_chars(value->text.data(), value->text.data() + value->text.size(), n);
    return error == std::errc{} && end == value->text.data() + value->text.size() ? std::optional(n) : std::nullopt;
}
std::string text(const json::Value& value, std::string_view key) { const auto* p = value.get(key); return p ? p->stringOr() : std::string{}; }
bool flag(const json::Value& value, std::string_view key) { return boolean(value.get(key)).value_or(false); }
bool validGame(const Game& game) { return game.source == GameSource::steam && game.kind == GameKind::steam && game.app_id; }

bool validAccount(std::wstring_view name) {
    return !name.empty() && name.size() <= 64 && name.front() != L'-' &&
           std::none_of(name.begin(), name.end(), [](wchar_t c) { return c <= L' ' || c == 0x7f; });
}

std::vector<std::wstring> arguments(const Request& r) {
    using C = Command;
    switch (r.command) {
    case C::login_qr: case C::login_password:
    case C::health: case C::login: case C::logout: case C::reconnect: case C::library: case C::libraries:
    case C::friends: case C::presence: case C::chat_history: case C::chat_send: case C::chat_live:
    case C::downloads: case C::running: case C::workshop_install: case C::workshop_subscribe: case C::workshop_unsubscribe:
    case C::collections_pull: case C::collections_list: case C::collections_show: case C::duplicates: break;
    default: if (!r.app) return {};
    }
    const auto app = std::to_wstring(r.app), id = std::to_wstring(r.id);
    std::vector<std::wstring> args{L"--json"};
    const auto add = [&](std::initializer_list<std::wstring> values) { args.insert(args.end(), values); };
    switch (r.command) {
    case C::standalone: return {};
    case C::health: add({L"login", L"--health"}); break;
    case C::login: return {L"login"}; // An owned console; no credential ever enters Launchme's command line.
    // In-app sign-ins: Steam's QR challenge (scanned with the Steam Mobile app), or the account name
    // here and the password through the environment; Steam Guard codes go to stdin.
    case C::login_qr: add({L"login", L"--qr", L"--timeout", L"300"}); break;
    case C::login_password:
        if (!validAccount(r.text) || r.secret.empty() || r.secret.size() > 256) return {};
        add({L"login", L"-u", r.text}); break;
    case C::logout: add({L"logout"}); break;
    case C::reconnect: add({L"login", L"--reconnect"}); break;
    case C::library: add({L"list"}); break;
    case C::libraries: add({L"libraries"}); break;
    case C::info: add({L"info", app, L"--extended", L"--lang", r.language}); break;
    case C::achievements: add({L"achievements", app, L"--lang", r.language}); break;
    case C::friends: add({L"friends"}); break;
    case C::presence: if (r.text != L"online" && r.text != L"invisible" && r.text != L"away" && r.text != L"busy") return {}; add({L"config", L"presence", r.text}); break;
    case C::chat_history: if (!r.id) return {}; add({L"chat", L"history", id, L"--count", L"200"}); break;
    case C::chat_send: if (!r.id || r.text.empty() || r.text.size() > 4000) return {}; add({L"chat", L"send", id, L"--", r.text}); break;
    case C::chat_live: if (!r.id) return {}; add({L"chat", L"open", id}); break;
    case C::downloads: add({L"install", L"list"}); break;
    case C::cancel_install: add({L"install", L"stop", app}); break;
    case C::install_pause: add({L"install", L"pause", app}); break;
    case C::install_resume: add({L"install", L"resume", app}); break;
    case C::install_forget: add({L"install", L"forget", app}); if (r.delete_files) add({L"--delete-files"}); break;
    case C::duplicates: add({L"duplicates"}); break;
    // The library root must be absolute; Steam is never stopped or restarted for the user.
    case C::relink: if (!r.folder.is_absolute()) return {}; add({L"relink", app, r.folder.native()}); break;
    case C::import_files: if (!r.folder.is_absolute()) return {}; add({L"import", app, r.folder.native()}); break;
#ifdef _WIN32
    case C::install: add({L"install", app, L"--platform", L"windows"}); if (!r.folder.empty()) add({L"--library", r.folder.native()}); break;
#else
    case C::install:
        add({L"install", app});
        if (r.text == L"linux" || r.text == L"windows") add({L"--platform", r.text}); else if (!r.text.empty()) return {};
        if (!r.folder.empty()) add({L"--library", r.folder.wstring()});
        break;
#endif
    case C::update: add({L"update", app}); break;
    case C::verify: add({L"verify", app}); break;
    case C::uninstall: add({L"uninstall", app}); break;
#ifdef _WIN32
    case C::play: add({L"play", app, L"--windows", L"--no-update"}); if (r.steam) add({L"--steam"}); else add({L"--steam-mode", L"none", L"--platform", L"windows"}); break;
#else
    // Linux: no `--windows` (it would skip Proton); a Windows build runs through Aurelia's own
    // Proton/Wine, the native build directly.
    case C::play:
        add({L"play", app, L"--no-update"});
        if (r.text == L"linux") add({L"--platform", L"linux"}); else if (!r.text.empty() && r.text != L"windows") return {};
        if (r.steam) add({L"--steam"}); else if (r.text != L"linux") add({L"--steam-mode", L"none", L"--platform", L"windows"});
        break;
#endif
    case C::launch_targets: add({L"launch-targets", app}); break;
    case C::running: add({L"running"}); break;
    case C::stop: add({L"stop", app}); if (r.force) add({L"--force"}); break;
    case C::available: add({L"available", app}); break;
    case C::cloud_list: add({L"cloud", L"list", app}); break;
    case C::cloud_sync:
        add({L"cloud", L"sync", app});
        if (r.direction == Request::Direction::down) add({L"--down"});
        if (r.direction == Request::Direction::up) add({L"--up"});
        if (!r.text.empty()) { if (r.text != L"cloud" && r.text != L"local") return {}; add({L"--resolve", r.text}); }
        break;
    case C::dlc: add({L"dlc", app}); break;
    case C::enable_dlc: add({L"enable", app}); break;
    case C::disable_dlc: add({L"disable", app}); break;
    case C::manifests: add({L"manifests", app}); break;
    case C::pin: add({L"pin", app}); break;
    case C::unpin: add({L"unpin", app}); break;
    case C::downgrade: if (!r.depot || !r.id) return {}; add({L"downgrade", app, L"--depot", std::to_wstring(r.depot), L"--manifest", id}); break;
    case C::branches: add({L"branches", app}); break;
    case C::set_branch: if (r.text.empty() || r.text.size() > 128) return {}; add({L"set-branch", app, L"--", r.text}); break;
    case C::move: if (!r.folder.is_absolute()) return {}; add({L"move", app, r.folder.wstring()}); break;
    case C::workshop_browse: add({L"workshop", L"browse", app}); break;
    case C::workshop_status: add({L"workshop", L"status", app}); break;
    case C::workshop_install: if (!r.id) return {}; add({L"workshop", L"install", id}); break;
    case C::workshop_subscribe: if (!r.id) return {}; add({L"workshop", L"subscribe", id, L"--install"}); break;
    case C::workshop_unsubscribe: if (!r.id) return {}; add({L"workshop", L"unsubscribe", id}); break;
    case C::collections_pull: add({L"collections", L"pull"}); break;
    case C::collections_list: add({L"collections", L"list"}); break;
    case C::collections_show:
        if (r.text.empty() || r.text.size() > 64 || !std::iswalnum(r.text.front()) || !std::all_of(r.text.begin(), r.text.end(), [](wchar_t ch) { // Never an option.
                return (ch >= L'a' && ch <= L'z') || (ch >= L'A' && ch <= L'Z') || (ch >= L'0' && ch <= L'9') || ch == L'-' || ch == L'_'; })) return {};
        add({L"collections", L"show", r.text}); break;
    }
    if (std::any_of(args.begin(), args.end(), [](const auto& a) { return a.find(L'\0') != a.npos; })) return {};
    return args;
}

std::optional<std::vector<Game>> library(const json::Value& value, uint64_t account) {
    if (value.type != Type::array || value.elements.size() > 50000 || !account) return {};
    std::vector<Game> result; std::set<uint32_t> seen;
    for (const auto& item : value.elements) {
        const auto id = appId(item); const auto installed = boolean(item.get("is_installed"));
        const auto name = text(item, "name");
        if (!id || !seen.insert(id).second || !installed || name.empty()) return {};
        // Unlicensed on-disk games are not the signed-in account's library.
        if (!flag(item, "is_owned") && !flag(item, "is_family_shared")) continue;
        Game g; g.app_id = id; g.name = wideLenient(name); g.installed = *installed;
        g.family_shared = !flag(item, "is_owned") && flag(item, "is_family_shared");
        g.install_directory = wideLenient(text(item, "install_path"));
        g.account_id = std::to_string(account);
        g.store_playtime_minutes = static_cast<uint32_t>(std::min<uint64_t>(UINT32_MAX, integer(item.get("playtime_forever_minutes")).value_or(0)));
        result.push_back(std::move(g));
    }
    return result;
}
std::optional<std::vector<SteamCollection>> collectionList(const json::Value& value) {
    const auto* items = value.get("collections");
    if (!items || items->type != Type::array || items->elements.size() > 10000) return {};
    std::vector<SteamCollection> result;
    for (const auto& item : items->elements) {
        const auto id = text(item, "id"), name = text(item, "name");
        if (id.empty() || id.size() > 64 || !std::isalnum(static_cast<unsigned char>(id.front())) || !std::all_of(id.begin(), id.end(), [](char ch) { return std::isalnum(static_cast<unsigned char>(ch)) || ch == '-' || ch == '_'; })) return {};
        if (flag(item, "dynamic")) continue; // Steam computes its games; they cannot be listed.
        result.push_back({wideLenient(name), {}, "user-collections." + id});
    }
    return result;
}
std::optional<std::vector<uint32_t>> collectionMembers(const json::Value& value) {
    const auto* ids = value.get("app_ids");
    if (!ids || ids->type != Type::array || ids->elements.size() > 100000) return {};
    std::vector<uint32_t> result;
    for (const auto& item : ids->elements) {
        const auto id = integer(&item);
        if (!id || !*id || *id > UINT32_MAX) return {};
        result.push_back(static_cast<uint32_t>(*id));
    }
    return result;
}
FriendsResult friends(const json::Value& value) {
    FriendsResult result;
    if (value.type != Type::array) { result.error = tr(L"Nieprawidłowa odpowiedź Aurelii"); return result; }
    for (const auto& item : value.elements) {
        const auto id = integer(item.get("steam_id")); if (!id || !*id) continue;
        SteamFriend f; f.id = *id; f.name = wideLenient(text(item, "persona_name"));
        if (f.name.empty()) f.name = std::to_wstring(f.id);
        f.game = wideLenient(text(item, "game_name")); f.avatar_url = wideLenient(text(item, "avatar_url"));
        f.status = static_cast<int>(std::min<uint64_t>(6, integer(item.get("persona_state")).value_or(0)));
        result.friends.push_back(std::move(f));
    }
    std::stable_sort(result.friends.begin(), result.friends.end(), [](const auto& a, const auto& b) {
        const int x = !a.game.empty() ? 2 : a.status ? 1 : 0, y = !b.game.empty() ? 2 : b.status ? 1 : 0;
        return x != y ? x > y : a.name < b.name;
    });
    return result;
}
GameDetails details(const json::Value& value) {
    GameDetails d; d.description = wideLenient(text(value, "description")); d.release_date = wideLenient(text(value, "release_date"));
    d.developer = joined(value.get("developers")); d.publisher = joined(value.get("publishers"));
    if (const auto* ext = value.get("extended")) d.genres = joined(ext->get("genres"));
    return d;
}
SteamAchievements achievements(const json::Value& value) {
    SteamAchievements result;
    const auto* items = value.get("achievements"); if (!items || items->type != Type::array) return result;
    for (const auto& item : items->elements) {
        SteamAchievement a; a.id = text(item, "achievement_key"); a.name = wideLenient(text(item, "name"));
        a.description = wideLenient(text(item, "description")); a.achieved = flag(item, "unlocked");
        a.hidden = !flag(item, "visible"); a.unlocked_at = integer(item.get("unlock_time")).value_or(0);
        a.icon = wideLenient(text(item, a.achieved ? "image_url_unlocked" : "image_url_locked"));
        if (const auto* rarity = item.get("rarity"); rarity && rarity->type == Type::number && rarity->numeric >= 0 && rarity->numeric <= 100)
            a.global_percent = static_cast<float>(rarity->numeric);
        if (!a.id.empty()) { result.unlocked += a.achieved; result.items.push_back(std::move(a)); }
    }
    result.total = static_cast<uint32_t>(result.items.size()); result.complete = true; return result;
}
std::optional<std::vector<Download>> downloads(const json::Value& value) {
    if (value.type != Type::array) return {};
    std::vector<Download> result;
    for (const auto& item : value.elements) {
        Download d; d.app = appId(item); if (!d.app) return {};
        d.name = wideLenient(text(item, "name")); d.status = wideLenient(text(item, "status"));
        d.progress.done = integer(item.get("downloaded_bytes")).value_or(0);
        d.progress.total = integer(item.get("total_bytes")).value_or(0);
        if (d.progress.done > d.progress.total) d.progress.total = 0;
        // "state": running, or a backlog entry (queued, paused, aborted, interrupted) that nothing downloads.
        const auto state = text(item, "state");
        d.backlog = !state.empty() && state != "running";
        d.progress.live = !d.backlog;
        d.progress.phase = d.backlog ? (state == "queued" ? InstallPhase::queued : InstallPhase::paused)
                                     : flag(item, "is_downloading") ? InstallPhase::downloading : InstallPhase::allocating;
        result.push_back(std::move(d));
    }
    return result;
}
std::optional<std::vector<Duplicate>> duplicates(const json::Value& value) {
    const auto* list = value.get("duplicates"); if (!list || list->type != Type::array || list->elements.size() > 10000) return {};
    std::vector<Duplicate> result;
    for (const auto& item : list->elements) {
        Duplicate d; d.app = appId(item); if (!d.app) return {};
        d.name = wideLenient(text(item, "name"));
        if (const auto* stale = item.get("remove"); stale && stale->type == Type::array) for (const auto& copy : stale->elements) {
            const auto path = text(copy, "install_path"); if (path.empty()) continue;
            d.stale.push_back(std::filesystem::path(wideLenient(path)));
            d.reclaimable += integer(copy.get("size_bytes")).value_or(0);
        }
        if (!d.stale.empty()) result.push_back(std::move(d));
    }
    return result;
}
Cloud cloud(const Reply& reply, bool sync) {
    Cloud c;
    if (!reply.ok) { c.state = CloudState::failed; c.error = reply.error; return c; }
    if (!sync) {
        const auto* files = reply.data.get("files");
        if (!files || files->type != Type::array) { c.state = CloudState::failed; return c; }
        c.files = files->elements.size(); c.state = c.files ? CloudState::files : CloudState::empty; return c;
    }
    const auto status = text(reply.data, "status");
    c.state = status == "ok" ? CloudState::synced : status == "conflicts" ? CloudState::conflicts :
              status == "incomplete" ? CloudState::incomplete : CloudState::failed;
    for (const auto* key : {"conflicts", "failed", "skipped"}) {
        const auto* items = reply.data.get(key);
        if ((!items && status == "ok") || (items && items->type != Type::array)) { c.state = CloudState::failed; c.error = tr(L"Nieprawidłowa odpowiedź Aurelii"); return c; }
        if (!items || items->elements.empty()) continue;
        if (std::string_view(key) == "conflicts") c.conflicts = items->elements.size();
        else if (std::string_view(key) == "failed") c.state = CloudState::failed;
        else if (c.state != CloudState::failed) c.state = CloudState::incomplete;
        for (const auto& item : items->elements) {
            auto issue = wideLenient(text(item, "filename"));
            const auto detail = text(item, "error").empty() ? text(item, "reason") : text(item, "error");
            if (!detail.empty()) issue += L" · " + wideLenient(detail);
            if (!issue.empty() && c.issues.size() < 100) c.issues.push_back(std::move(issue));
        }
    }
    if (c.conflicts) c.state = CloudState::conflicts; // Resolution remains available even when other files also failed.
    return c;
}
const wchar_t* cloudLabel(CloudState state) {
    switch (state) {
    case CloudState::checking: return tr(L"Chmura: sprawdzanie…");
    case CloudState::files: return tr(L"Chmura: zapisy dostępne");
    case CloudState::empty: return tr(L"Chmura: brak zapisów");
    case CloudState::syncing: return tr(L"Chmura: synchronizacja…");
    case CloudState::synced: return tr(L"Chmura: zsynchronizowano");
    case CloudState::conflicts: return tr(L"Chmura: konflikt zapisów");
    case CloudState::incomplete: return tr(L"Chmura: niepełna synchronizacja");
    case CloudState::failed: return tr(L"Chmura: błąd połączenia lub synchronizacji");
    default: return tr(L"Chmura: nie sprawdzono");
    }
}
const wchar_t* cloudShort(CloudState state) {
    switch (state) {
    case CloudState::checking: return tr(L"Sprawdzanie…");
    case CloudState::files: return tr(L"Zapisy dostępne");
    case CloudState::empty: return tr(L"Brak zapisów");
    case CloudState::syncing: return tr(L"Synchronizacja…");
    case CloudState::synced: return tr(L"Zsynchronizowano");
    case CloudState::conflicts: return tr(L"Konflikt zapisów");
    case CloudState::incomplete: return tr(L"Niepełna synchronizacja");
    case CloudState::failed: return tr(L"Błąd synchronizacji");
    default: return tr(L"Nie sprawdzono");
    }
}
std::optional<std::vector<Dlc>> dlcs(const json::Value& value) {
    const auto* list = value.get("dlc"); if (!list || list->type != Type::array) return {};
    std::vector<Dlc> result;
    for (const auto& item : list->elements) {
        Dlc d; d.app = appId(item); if (!d.app) return {};
        d.name = wideLenient(text(item, "name")); if (d.name.empty()) d.name = std::to_wstring(d.app);
        d.owned = boolean(item.get("owned")); d.installed = boolean(item.get("installed")); d.disabled = boolean(item.get("disabled"));
        result.push_back(std::move(d));
    }
    return result;
}
std::optional<std::vector<ChatMessage>> chatHistory(const json::Value& value) {
    if (value.type != Type::array) return {};
    std::vector<ChatMessage> result;
    for (const auto& item : value.elements) {
        const auto stamp = integer(item.get("timestamp")); const auto self = boolean(item.get("from_self"));
        if (!stamp || !self || !item.get("message") || item.get("message")->type != Type::string) return {};
        result.push_back({*stamp, *self, wideLenient(text(item, "message"))});
    }
    std::stable_sort(result.begin(), result.end(), [](const auto& a, const auto& b) { return a.timestamp < b.timestamp; });
    return result;
}
}
