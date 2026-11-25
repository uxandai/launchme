// Xbox account data beyond the library (helper 0.2.0): the played titles (activity.json), the offline
// PC and its leases (offline.json), packages with an update (updates.json), a title's achievements,
// and the texts Ustawienia and the install dialog show for them.
#include "xbox_account.hpp"
#include "xbox_account_internal.hpp"
#include "../model/installation.hpp"
#include "../platform/file_io.hpp"
#include "../platform/text_encoding.hpp"

#include <algorithm>
#include <cstdio>

namespace xbox_account {
using namespace internal;
using text_encoding::wide;
using text_encoding::wideLenient;

std::map<uint32_t, Activity> parseActivity(std::string_view activity_json) {
    std::map<uint32_t, Activity> result;
    const auto root = json::parse(activity_json, 16 * 1024 * 1024);
    const auto* titles = root ? root->get("titles") : nullptr;
    if (!titles || titles->type != json::Value::Type::array) return result;
    for (const auto& entry : titles->elements) {
        const auto id = titleId(field(entry, "title_id"));
        if (!id || result.contains(id)) continue;
        Activity activity;
        activity.last_played = unsignedNumber(entry.get("last_played"), 4e10).value_or(0);
        if (const auto minutes = unsignedNumber(entry.get("minutes"), 1e9)) activity.minutes = static_cast<uint32_t>(*minutes);
        if (const auto* counts = entry.get("achievements"); counts && counts->type == json::Value::Type::object) {
            const auto unlocked = unsignedNumber(counts->get("unlocked"), 100000), total = unsignedNumber(counts->get("total"), 100000);
            if (unlocked && total && *total && *unlocked <= *total) {
                activity.unlocked = static_cast<uint32_t>(*unlocked); activity.total = static_cast<uint32_t>(*total);
                activity.gamerscore = static_cast<uint32_t>(unsignedNumber(counts->get("gamerscore"), 1e7).value_or(0));
                activity.total_gamerscore = static_cast<uint32_t>(unsignedNumber(counts->get("total_gamerscore"), 1e7).value_or(0));
            }
        }
        result.emplace(id, activity);
        if (result.size() >= 5000) break;
    }
    return result;
}

void applyActivity(std::vector<Game>& games, const std::map<uint32_t, Activity>& activity) {
    for (auto& game : games) {
        if (game.source != GameSource::xbox || !game.xbox_title_id) continue;
        const auto found = activity.find(game.xbox_title_id);
        if (found == activity.end()) continue;
        if (found->second.minutes) game.store_playtime_minutes = std::max(game.store_playtime_minutes, *found->second.minutes);
        game.last_played = std::max(game.last_played, found->second.last_played);
    }
}

OfflineStatus parseOffline(std::string_view offline_json) {
    OfflineStatus status;
    const auto root = json::parse(offline_json, 1024 * 1024);
    if (!root || root->type != json::Value::Type::object) return status;
    if (const auto* enabled = root->get("enabled"); enabled && enabled->type == json::Value::Type::boolean) status.enabled = enabled->flag;
    status.checked_at = unsignedNumber(root->get("checked_at"), 4e10).value_or(0);
    const auto count = [&](const char* key) -> std::optional<uint32_t> {
        if (const auto value = unsignedNumber(root->get(key), 10000)) return static_cast<uint32_t>(*value);
        return {};
    };
    status.remaining = count("remaining"); status.max = count("max"); status.period_days = count("period_days");
    status.next_change_at = unsignedNumber(root->get("next_change_at"), 4e10).value_or(0);
    if (const auto* games = root->get("games"); games && games->type == json::Value::Type::object)
        for (const auto& [product, until] : games->properties) {
            const auto end = unsignedNumber(&until, 4e10);
            if (validProductId(product) && end && *end) status.games.emplace(product, *end);
            if (status.games.size() >= 4096) break;
        }
    return status;
}

uint64_t offlineUntil(const OfflineStatus& status, const std::string& product, uint64_t now) {
    const auto found = status.games.find(product);
    return found != status.games.end() && found->second > now ? found->second : 0;
}

Addons parseAddons(std::string_view library_json) {
    Addons result;
    const auto root = json::parse(library_json, 32 * 1024 * 1024);
    const auto* games = root ? root->get("games") : nullptr;
    if (!games || games->type != json::Value::Type::array) return result;
    size_t total = 0;
    for (const auto& game : games->elements) {
        const auto product = field(game, "product_id");
        const auto* list = game.get("addons");
        if (!validProductId(product) || !list || list->type != json::Value::Type::array || result.contains(product)) continue;
        std::vector<Addon> addons;
        for (const auto& entry : list->elements) {
            Addon addon;
            addon.product = field(entry, "product_id");
            addon.title = wideLenient(field(entry, "title"));
            if (!validProductId(addon.product) || addon.product == product || addon.title.empty() || addon.title.size() > 256 ||
                std::any_of(addons.begin(), addons.end(), [&](const Addon& a) { return a.product == addon.product; })) continue;
            const auto* content = entry.get("content");
            addon.content = content && content->type == json::Value::Type::boolean && content->flag;
            if (const auto image = field(entry, "image"); httpsArt(image)) addon.image = image;
            addons.push_back(std::move(addon));
            if (addons.size() >= 200) break;
        }
        total += addons.size();
        if (!addons.empty()) result.emplace(product, std::move(addons));
        if (total >= 4000) break;
    }
    return result;
}

std::set<std::string> parseUpdates(std::string_view updates_json) {
    std::set<std::string> result;
    const auto root = json::parse(updates_json, 4 * 1024 * 1024);
    const auto* list = root ? root->get("updates") : nullptr;
    if (!list || list->type != json::Value::Type::array) return result;
    for (const auto& entry : list->elements) {
        const auto* update = entry.get("update");
        const auto product = field(entry, "product_id");
        if (update && update->type == json::Value::Type::boolean && update->flag && validProductId(product)) result.insert(product);
        if (result.size() >= 4096) break;
    }
    return result;
}

std::optional<SteamAchievements> parseAchievements(std::string_view result_json, uint32_t title_id) {
    const auto root = json::parse(result_json, 4 * 1024 * 1024);
    if (!root || root->type != json::Value::Type::object || titleId(field(*root, "title_id")) != title_id || !title_id) return {};
    const auto* items = root->get("items");
    if (!items || items->type != json::Value::Type::array) return {};
    SteamAchievements list;
    std::set<std::string> seen;
    for (const auto& item : items->elements) {
        SteamAchievement a;
        a.id = field(item, "id");
        a.name = wideLenient(field(item, "name"));
        if (a.id.empty() || a.id.size() > 200 || a.name.empty() || a.name.size() > 256 || !seen.insert(a.id).second) continue;
        a.description = wideLenient(field(item, "description"));
        if (a.description.size() > 1500) a.description.resize(1500);
        const auto* unlocked = item.get("unlocked");
        a.achieved = unlocked && unlocked->type == json::Value::Type::boolean && unlocked->flag;
        const auto* secret = item.get("secret");
        a.hidden = secret && secret->type == json::Value::Type::boolean && secret->flag;
        if (a.achieved) a.unlocked_at = unsignedNumber(item.get("unlocked_at"), 4e10).value_or(0);
        if (const auto icon = field(item, "icon"); httpsArt(icon)) a.icon = wide(icon);
        if (const auto* rarity = item.get("rarity"); rarity && rarity->type == json::Value::Type::number && std::isfinite(rarity->numeric) &&
            rarity->numeric >= 0 && rarity->numeric <= 100) a.global_percent = static_cast<float>(rarity->numeric);
        list.unlocked += a.achieved ? 1 : 0;
        list.items.push_back(std::move(a));
        if (list.items.size() >= 500) break;
    }
    list.total = static_cast<uint32_t>(list.items.size());
    const auto* partial = root->get("partial");
    list.complete = !(partial && partial->type == json::Value::Type::boolean && partial->flag);
    return list;
}

namespace {
std::wstring day(uint64_t unix_seconds) {
    const auto days = static_cast<int64_t>(unix_seconds / 86400);
    // Civil date from days since 1970-01-01 (Howard Hinnant's algorithm).
    const int64_t z = days + 719468, era = (z >= 0 ? z : z - 146096) / 146097;
    const auto doe = static_cast<uint64_t>(z - era * 146097);
    const auto yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const auto doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const auto mp = (5 * doy + 2) / 153;
    const auto d = doy - (153 * mp + 2) / 5 + 1, m = mp < 10 ? mp + 3 : mp - 9;
    const auto y = static_cast<int64_t>(yoe) + era * 400 + (m <= 2 ? 1 : 0);
    wchar_t text[32]{};
    swprintf(text, std::size(text), L"%02u.%02u.%lld", static_cast<unsigned>(d), static_cast<unsigned>(m), static_cast<long long>(y));
    return text;
}
}

std::wstring offlineDate(uint64_t unix_seconds) { return day(unix_seconds); }

std::wstring offlineText(const Snapshot& s, uint64_t now) {
    if (s.offline_busy) return tr(L"Pytanie Microsoft o uprawnienia offline…");
    std::wstring text;
    if (!s.offline.enabled) text = tr(L"Nie sprawdzono. Ten komputer może grać bez internetu, jeśli jest komputerem offline konta.");
    else if (*s.offline.enabled) {
        size_t games = 0;
        for (const auto& [product, until] : s.offline.games) games += until > now ? 1 : 0;
        text = tr(L"Ten komputer jest komputerem offline konta. Gry offline: ") + std::to_wstring(games);
    } else text = tr(L"Ten komputer nie jest komputerem offline konta.");
    if (s.offline.remaining && s.offline.max) text += std::wstring(L" · ") + tr(L"zmiany: ") + std::to_wstring(*s.offline.remaining) + L"/" + std::to_wstring(*s.offline.max);
    if (s.offline.next_change_at > now) text += std::wstring(L" · ") + tr(L"następna zmiana od ") + day(s.offline.next_change_at);
    if (s.offline_failure == Failure::declined) text += std::wstring(L" · ") + tr(L"Microsoft nie przyjął zmiany.");
    else if (s.offline_failure == Failure::network) text += std::wstring(L" · ") + tr(L"Brak połączenia z Xbox. Spróbuj ponownie.");
    else if (s.offline_failure == Failure::helper) text += std::wstring(L" · ") + tr(L"brak pomocnika");
    else if (s.offline_failure == Failure::expired) text += std::wstring(L" · ") + tr(L"Sesja Xbox wygasła. Połącz konto ponownie.");
    return text;
}

std::wstring installStatusText(const gog::InstallSnapshot& s) {
    if (s.state == gog::InstallState::error) {
        switch (s.error) {
        case gog::InstallError::device_limit: return tr(L"Limit urządzeń Microsoft Store (10). Odłącz nieużywane urządzenie na account.microsoft.com/devices/content i ponów.");
        case gog::InstallError::account: return tr(L"Brak licencji: gra nie jest kupiona albo Game Pass wygasł. Połącz konto ponownie, jeśli sesja wygasła.");
        case gog::InstallError::unavailable: return tr(L"Tej gry nie da się pobrać bez aplikacji Xbox (pakiet inny niż GDK MSIXVC).");
        case gog::InstallError::network: return tr(L"Nie udało się pobrać plików. Ponów, aby wznowić.");
        case gog::InstallError::storage: return tr(L"Nie można zapisać plików. Sprawdź folder i wolne miejsce.");
        case gog::InstallError::detection: return tr(L"Nie potwierdzono instalacji w wybranym folderze. Pliki zachowano.");
        default: return tr(L"Instalacja Xbox nie powiodła się. Możesz wznowić pobieranie.");
        }
    }
    switch (s.state) {
    case gog::InstallState::loading: return tr(L"Przygotowanie pobierania Xbox…");
    case gog::InstallState::downloading: return installationText(s.progress());
    case gog::InstallState::verifying: return tr(L"Sprawdzanie plików gry…");
    case gog::InstallState::completed: return tr(L"Gra jest zainstalowana.");
    case gog::InstallState::cancelled: return tr(L"Pobieranie przerwane. Możesz je wznowić.");
    default: return tr(L"Instalacja bez aplikacji Xbox (pomocnik Xodus).");
    }
}
}
