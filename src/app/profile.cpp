// Profil (src/ui/profile_view.hpp, drawn by src/ui/profile_renderer.cpp): the player's page, opened
// from its widget in the drawer's dashboard (the desktop window: the widgets icon, first in the top
// bar). A modal layer like Tarot. It shows this week from the play journal (play_journal.hpp: what
// every followed session added per day), the latest achievements and their monthly history from
// the achievement history (achievement_history.hpp), the games closest to 100 % from the game
// index's counts, and the library per platform. Opening it reads in the background what Steam's
// own files say was unlocked since the last look, the Steam account's picture and the latest
// achievements' icons; the page fills in as they come.
#include "launcher.hpp"
#include "../platform/unix_time.hpp"
#include "profile_scan.hpp"
#include "../library/steam.hpp"
#include "../library/steam_achievements.hpp"
#include "../library/steam_stats.hpp"
#include "../platform/atomic_file.hpp"
#include "../platform/text_encoding.hpp"

#include <algorithm>
#include <cmath>
#include <cwctype>
#include <fstream>
#include <set>
#include <span>

using profile_ui::Focus;
using profile_ui::Section;

namespace {
std::filesystem::path journalFile() { return userConfigDirectory() / L"play-journal.tsv"; }
std::filesystem::path historyFile() { return userConfigDirectory() / L"achievement-history.json"; }
std::vector<uint8_t> readPicture(const std::filesystem::path& file) {
    std::ifstream input(file, std::ios::binary | std::ios::ate);
    if (!input || input.tellg() < 64 || input.tellg() > 2 * 1024 * 1024) return {};
    std::vector<uint8_t> bytes(static_cast<size_t>(input.tellg()));
    input.seekg(0);
    if (!input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()))) return {};
    const bool picture = (bytes[0] == 0xff && bytes[1] == 0xd8) || (bytes[0] == 0x89 && bytes[1] == 'P' && bytes[2] == 'N' && bytes[3] == 'G');
    return picture ? bytes : std::vector<uint8_t>{};
}
// A cache name for an icon URL: Steam's own file name, else a hash of the URL (as achievements.cpp).
std::wstring iconFile(const std::wstring& url) {
    if (auto steam = achievementIconFile(url); !steam.empty()) return steam;
    uint64_t hash = 1469598103934665603ULL;
    for (const wchar_t c : url) { hash ^= static_cast<uint16_t>(c); hash *= 1099511628211ULL; }
    wchar_t name[32]{};
    swprintf_s(name, L"%016llx.img", static_cast<unsigned long long>(hash));
    return name;
}
std::vector<achievement_history::Unlock> unlocksOf(const SteamAchievements& list) {
    std::vector<achievement_history::Unlock> result;
    for (const auto& item : list.items)
        if (item.achieved && item.unlocked_at)
            result.push_back({{}, item.id, item.name, item.description, item.icon, item.unlocked_at, item.global_percent});
    return result;
}
// "12 godz. 30 min", "45 min".
std::wstring hoursMinutes(uint64_t minutes) {
    const auto hours = minutes / 60, rest = minutes % 60;
    if (!hours) return std::to_wstring(rest) + L" min";
    return std::to_wstring(hours) + tr(L" godz.") + (rest ? L" " + std::to_wstring(rest) + L" min" : std::wstring{});
}
std::wstring dateText(int date, const wchar_t* format) {
    SYSTEMTIME day{};
    day.wYear = static_cast<WORD>(date / 10000); day.wMonth = static_cast<WORD>(date / 100 % 100); day.wDay = static_cast<WORD>(date % 100);
    wchar_t text[64]{};
    if (!GetDateFormatEx(i18n::localeName(), 0, &day, format, text, static_cast<int>(std::size(text)), nullptr)) return {};
    return text;
}
// "dziś", "wczoraj", "3 dni temu", then the date.
std::wstring when(uint64_t unix_time, int today) {
    const int date = play_journal::dateOf(unix_time);
    if (date == today) return tr(L"dziś");
    if (date == play_journal::addDays(today, -1)) return tr(L"wczoraj");
    for (int days = 2; days <= 30; ++days)
        if (date == play_journal::addDays(today, -days)) return std::to_wstring(days) + tr(L" dni temu");
    return dateText(date, L"d MMM yyyy");
}
std::wstring percent(float value) {
    wchar_t text[16]{};
    swprintf_s(text, value < 10 ? L"%.1f" : L"%.0f", value);
    std::wstring result = text;
    if (i18n::language() != i18n::Language::en) std::replace(result.begin(), result.end(), L'.', L',');
    return result;
}
std::wstring initialsOf(const std::wstring& name) {
    std::wstring result;
    bool start = true;
    for (const wchar_t c : name) {
        if (std::iswalnum(c) && start && result.size() < 2) result += static_cast<wchar_t>(std::towupper(c));
        start = !std::iswalnum(c);
    }
    return result.empty() ? L"?" : result;
}
} // namespace

// ---- The background read ----------------------------------------------------------------------

// The account's picture as the Steam client keeps it (no network); empty without one.
std::vector<uint8_t> readSteamAvatar(const std::filesystem::path& steam_root, uint64_t steam_id) {
    if (steam_root.empty() || steam_id <= steam_accounts::steam_id_offset) return {};
    return readPicture(steam_root / L"config" / L"avatarcache" / (std::to_wstring(steam_id) + L".png"));
}

ProfileScanResult readProfile(const ProfileScanRequest& request, const std::function<bool()>& cancelled) {
    ProfileScanResult result;
    result.scanned = request.scanned;
    if (!request.steam_root.empty() && request.steam_id > steam_accounts::steam_id_offset) {
        result.avatar = readSteamAvatar(request.steam_root, request.steam_id);
        // Unlocks: only games whose stats file changed since the last read.
        const auto account = std::to_wstring(request.steam_id - steam_accounts::steam_id_offset);
        const auto folder = request.steam_root / L"appcache" / L"stats";
        for (const auto& [key, app] : request.steam_games) {
            if (cancelled()) return result;
            std::error_code error;
            const auto file = folder / (L"UserGameStats_" + account + L"_" + std::to_wstring(app) + L".bin");
            const auto stamp = std::filesystem::last_write_time(file, error);
            if (error) continue;
            const auto ticks = static_cast<uint64_t>(stamp.time_since_epoch().count());
            const auto scan_key = "steam:" + std::to_string(app);
            if (const auto found = result.scanned.find(scan_key); found != result.scanned.end() && found->second == ticks) continue;
            const auto list = readLocalAchievements(request.steam_root, request.steam_id, app, request.english);
            result.scanned[scan_key] = ticks;
            if (!list.total) continue;
            const auto name = request.names.find(key);
            result.games.push_back({key, name == request.names.end() ? std::wstring{} : name->second, unlocksOf(list), {list.unlocked, list.total}});
        }
    }
    // The latest achievements' icons: the cache first, then the network once.
    for (const auto& [url, folder] : request.icons) {
        if (cancelled()) return result;
        if (!url.starts_with(L"https://")) {
            if (auto bytes = readPicture(url); !bytes.empty()) result.icons[url] = std::move(bytes);
            continue;
        }
        const auto file = request.icon_cache / folder / iconFile(url);
        auto bytes = readPicture(file);
        if (bytes.empty()) {
            HttpBudget budget(10000);
            auto reply = httpGet(url, L"Accept: image/jpeg, image/png\r\n", 1024 * 1024);
            const bool picture = reply.status == 200 && reply.body.size() >= 64 &&
                ((reply.body[0] == 0xff && reply.body[1] == 0xd8) || (reply.body[0] == 0x89 && reply.body[1] == 'P'));
            if (picture) { bytes = std::move(reply.body); atomic_file::write(file, std::span<const uint8_t>(bytes)); }
        }
        if (!bytes.empty()) result.icons[url] = std::move(bytes);
    }
    return result;
}

// ---- Journal and history -----------------------------------------------------------------------

void Launcher::loadProfileData() {
    if (!journal_loaded_) { journal_ = play_journal::load(journalFile()); journal_loaded_ = true; }
    if (!history_loaded_) { history_ = achievement_history::load(historyFile()); history_loaded_ = true; }
}

void Launcher::journalPlayed(const std::string& key, const std::wstring& name, uint64_t ms) {
    loadProfileData();
    play_journal::played(journal_, play_journal::dateOf(unix_time::now()), key, name, ms);
    journal_dirty_ = true;
}

void Launcher::journalLaunched(const std::string& key, const std::wstring& name) {
    loadProfileData();
    play_journal::launched(journal_, play_journal::dateOf(unix_time::now()), key, name);
    journal_dirty_ = true;
}

void Launcher::saveJournal() {
    if (!journal_dirty_) return;
    journal_dirty_ = false;
    play_journal::save(journal_, journalFile());
}

// An achievement list a game showed (Szczegóły → Osiągnięcia, a plugin's answer): its unlocks go
// into the history, and the page shows them if it is open.
void Launcher::recordAchievements(const std::string& key, const std::wstring& name, const SteamAchievements& list) {
    if (key.empty() || !list.total) return;
    loadProfileData();
    const auto before = history_;
    achievement_history::merge(history_, key, name, unlocksOf(list), {list.unlocked, list.total});
    if (history_ == before) return;
    achievement_history::save(history_, historyFile());
    if (profile_.open) rebuildProfile();
}

// ---- Opening and closing ------------------------------------------------------------------------

bool Launcher::profileAvailable() const {
    return !launchAnimationActive() && !pending_launch_ && !closing_ && (topModal() == Modal::none || topModal() == Modal::profile);
}

void Launcher::openProfile() {
    if (friends_open_) closeFriends();
    if (!profileAvailable() || profile_.open) return;
    cancelEnterHold(); cancelTabHold(); closeKeyboard(); dismissMenu(); closeQuick();
    loadProfileData();
    const float alpha = profile_.alpha; // Reopened while fading out: it fades back in from there.
    profile_ = {};
    profile_.alpha = alpha;
    profile_.open = true;
    rebuildProfile();
    startProfileScan(true);
    audio_.drawer();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::closeProfile() {
    if (!profile_.open) return;
    profile_.open = false;
    profile_cancel_->store(true);
    ++profile_generation_;
    audio_.navigate();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

bool Launcher::tickProfile(float dt) {
    auto& p = profile_;
    const bool animated = settings_.screen_transitions;
    bool moving = false;
    const float target = p.open ? 1.0f : 0.0f;
    if (p.alpha != target) {
        p.alpha = animated ? std::clamp(p.alpha + (p.open ? dt : -dt) / 0.24f, 0.0f, 1.0f) : target;
        moving = true;
        if (!p.open && p.alpha <= 0) { renderer_.resetProfileImages(0); profile_slots_.clear(); }
    }
    if (!p.open) return moving;
    if (p.time < 2) { p.time += animated ? dt : 2.0f; moving = true; } // The charts grow in, then rest.
    const float follow = animated ? std::min(1.0f, dt * 12.0f) : 1.0f;
    const auto glide = [&](float& value, float goal) {
        if (std::abs(value - goal) < 0.25f) { if (value != goal) { value = goal; moving = true; } return; }
        value += (goal - value) * follow; moving = true;
    };
    glide(p.scroll, p.scroll_target);
    if (std::abs(p.recent_offset - static_cast<float>(p.recent_first)) < 0.002f) p.recent_offset = static_cast<float>(p.recent_first);
    else { p.recent_offset += (static_cast<float>(p.recent_first) - p.recent_offset) * follow; moving = true; }
    if (p.loading) moving = true; // Its "Wczytywanie osiągnięć…" breathes.
    return moving;
}

// ---- Building the page --------------------------------------------------------------------------

// Who plays: the Steam account's name, else the one the companion uses, else Windows'.
std::wstring Launcher::profileName() {
    if (profile_name_account_ != account_ || profile_name_.empty()) {
        profile_name_account_ = account_;
        profile_name_.clear();
        if (account_ && steam_accounts_)
            for (const auto& item : steam_accounts_()) if (item.steam_id == account_ && !item.persona.empty()) profile_name_ = item.persona;
    }
    if (!profile_name_.empty()) return profile_name_;
    if (!settings_.companion_user_name.empty()) return settings_.companion_user_name;
    wchar_t user[256]{}; DWORD size = static_cast<DWORD>(std::size(user));
    if (GetUserNameW(user, &size) && user[0]) return user;
    return tr(L"Gracz");
}

// The drawer's profile tile: this week at a glance, refreshed every few seconds while it shows.
void Launcher::profileTile(DrawerDashboard& tile) {
    const auto tick = GetTickCount64();
    if (profile_tile_.profile_name.empty() || tick >= profile_tile_at_) {
        loadProfileData();
        const uint64_t now = unix_time::now();
        const auto week = play_journal::week(journal_, play_journal::dateOf(now));
        const auto minutes = week.total_ms / 60000;
        auto& c = profile_tile_;
        c.profile_name = profileName();
        c.profile_initials = initialsOf(c.profile_name);
        c.profile_value = minutes ? hoursMinutes(minutes) + tr(L" w tym tygodniu") : std::wstring(tr(L"W tym tygodniu bez gry"));
        const auto unlocks = achievement_history::since(history_, now >= 7 * 86400 ? now - 7 * 86400 : 0);
        c.profile_caption = std::to_wstring(week.games) + tr(L" gier") + L"  \u00b7  " + std::to_wstring(unlocks) + tr(L" osiągnięć");
        uint64_t longest = 0;
        for (const auto ms : week.day_ms) longest = std::max(longest, ms);
        for (size_t i = 0; i < 7; ++i) c.profile_week[i] = longest ? static_cast<float>(week.day_ms[i]) / static_cast<float>(longest) : 0;
        profile_tile_at_ = tick + 5000;
    }
    tile.profile_name = profile_tile_.profile_name;
    tile.profile_initials = profile_tile_.profile_initials;
    tile.profile_value = profile_tile_.profile_value;
    tile.profile_caption = profile_tile_.profile_caption;
    tile.profile_week = profile_tile_.profile_week;
}

void Launcher::rebuildProfile() {
    auto& p = profile_;
    const uint64_t now = unix_time::now();
    const int today = play_journal::dateOf(now);
    // The library by key: names, covers and platforms.
    std::unordered_map<std::string, const Game*> games;
    for (const auto& game : public_library_)
        if (isGame(game) && game.visibility == GameVisibility::public_game) games.emplace(gameKey(game), &game);
    const auto nameOf = [&](const std::string& key) {
        if (const auto found = games.find(key); found != games.end()) return found->second->name;
        if (const auto found = journal_.names.find(key); found != journal_.names.end()) return found->second;
        if (const auto found = history_.names.find(key); found != history_.names.end()) return found->second;
        return std::wstring{};
    };
    // The index's view: play time (Steam's), achievement counts.
    struct Known { uint32_t minutes = 0, unlocked = 0, total = 0; };
    std::unordered_map<std::string, Known> index;
    if (game_index_)
        index = game_index_->read([&](const game_index::Index& value) {
            std::unordered_map<std::string, Known> out;
            for (const auto& [key, game] : games) {
                (void)game;
                if (const auto* record = value.get(key))
                    out[key] = {record->playtime_minutes.value_or(0), record->achievements_unlocked, record->achievements_total};
            }
            return out;
        });
    const auto minutesOf = [&](const Game& game) -> uint64_t {
        const auto found = index.find(gameKey(game));
        const std::optional<uint32_t> steam = found != index.end() && found->second.minutes ? std::optional<uint32_t>(found->second.minutes) : std::nullopt;
        return gamePlaytime(game, steam).value_or(0);
    };

    p.name = profileName();
    p.initials = initialsOf(p.name);
    uint64_t library_minutes = 0;
    for (const auto& [key, game] : games) { (void)key; library_minutes += minutesOf(*game); }

    // Achievement counts: the index (Steam's progress file, plugins), the history for the rest.
    std::map<std::string, achievement_history::Progress> progress = history_.progress;
    for (const auto& [key, known] : index)
        if (known.total && (!progress.contains(key) || progress[key].total != known.total || known.unlocked > progress[key].unlocked))
            progress[key] = {known.unlocked, known.total};
    std::erase_if(progress, [&](const auto& item) { return !games.contains(item.first); });
    uint64_t unlocked_total = 0;
    size_t perfect = 0;
    for (const auto& [key, value] : progress) { unlocked_total += value.unlocked; perfect += value.total && value.unlocked >= value.total; }
    unlocked_total = std::max<uint64_t>(unlocked_total, history_.unlocks.size());
    p.subtitle = std::to_wstring(games.size()) + tr(L" gier");
    if (library_minutes >= 60) p.subtitle += L"  \u00b7  " + std::to_wstring(library_minutes / 60) + tr(L" godz. łącznie");
    if (unlocked_total) p.subtitle += L"  \u00b7  " + std::to_wstring(unlocked_total) + tr(L" osiągnięć");

    // Images: the picture, then covers and icons as they are needed.
    renderer_.resetProfileImages(0);
    profile_slots_.clear();
    p.avatar = false;
    if (!profile_avatar_.empty()) {
        profile_slots_.push_back({ProfileSlot::Kind::avatar, {}, {}});
        p.avatar = true;
    }
    const auto coverSlot = [&](const std::string& key) -> size_t {
        for (size_t i = 0; i < profile_slots_.size(); ++i)
            if (profile_slots_[i].kind == ProfileSlot::Kind::cover && profile_slots_[i].key == key) return i;
        if (!games.contains(key)) return profile_ui::no_image;
        profile_slots_.push_back({ProfileSlot::Kind::cover, key, {}});
        return profile_slots_.size() - 1;
    };
    const auto iconSlot = [&](const std::wstring& url) -> size_t {
        if (!profile_icons_.contains(url)) return profile_ui::no_image;
        for (size_t i = 0; i < profile_slots_.size(); ++i)
            if (profile_slots_[i].kind == ProfileSlot::Kind::icon && profile_slots_[i].url == url) return i;
        profile_slots_.push_back({ProfileSlot::Kind::icon, {}, url});
        return profile_slots_.size() - 1;
    };

    // This week.
    const auto week = play_journal::week(journal_, today);
    const uint64_t week_minutes = week.total_ms / 60000, previous = week.previous_ms / 60000;
    auto& time = p.stats[0];
    time = {tr(L"Czas gry"), hoursMinutes(week_minutes), {}};
    if (previous && week_minutes) {
        const int change = static_cast<int>(std::lround((static_cast<double>(week_minutes) - previous) * 100.0 / previous));
        time.note = (change > 0 ? L"+" : L"") + std::to_wstring(change) + tr(L"% wobec poprzedniego tygodnia");
        time.good = change > 0; time.bad = change < 0;
    } else time.note = week_minutes ? tr(L"Pierwszy taki tydzień") : tr(L"Jeszcze nic w tym tygodniu");
    p.stats[1] = {tr(L"Gry"), std::to_wstring(week.games), week.top.empty() ? std::wstring(tr(L"Żadnej gry w tym tygodniu")) :
                  tr(L"Najwięcej: ") + nameOf(week.top.front().first)};
    p.stats[2] = {tr(L"Uruchomienia"), std::to_wstring(week.launches), tr(L"Dni z grą: ") + std::to_wstring(week.active_days) + L" / 7"};
    const auto streak = play_journal::streak(journal_, today);
    const auto week_unlocks = achievement_history::since(history_, now >= 7 * 86400 ? now - 7 * 86400 : 0);
    p.stats[3] = {tr(L"Osiągnięcia"), std::to_wstring(week_unlocks),
                  streak >= 2 ? tr(L"Seria: ") + std::to_wstring(streak) + tr(L" dni z rzędu") : std::wstring(tr(L"Zdobyte w ostatnich 7 dniach"))};
    p.stats[3].good = streak >= 2;
    uint64_t longest = 0;
    for (const auto ms : week.day_ms) longest = std::max(longest, ms);
    for (size_t i = 0; i < 7; ++i) {
        p.week[i] = longest ? static_cast<float>(week.day_ms[i]) / static_cast<float>(longest) : 0;
        p.week_days[i] = dateText(week.dates[i], L"ddd");
        const auto minutes = week.day_ms[i] / 60000;
        p.week_times[i] = minutes ? (minutes >= 60 ? std::to_wstring(minutes / 60) + L":" + (minutes % 60 < 10 ? L"0" : L"") + std::to_wstring(minutes % 60) :
                                     std::to_wstring(minutes) + L"m") : std::wstring{};
    }
    p.week_total = week_minutes ? tr(L"Łącznie ") + hoursMinutes(week_minutes) : std::wstring{};
    p.top.clear();
    for (const auto& [key, ms] : week.top) {
        if (p.top.size() >= profile_ui::max_top) break;
        const auto name = nameOf(key);
        if (name.empty()) continue;
        p.top.push_back({key, name, hoursMinutes(ms / 60000), week.top.front().second ? static_cast<float>(ms) / week.top.front().second : 0, coverSlot(key)});
    }

    // The latest achievements.
    p.recent.clear();
    for (const auto& item : history_.unlocks) {
        if (p.recent.size() >= profile_ui::max_recent) break;
        if (!games.contains(item.game)) continue; // A game no longer in the library (or hidden).
        profile_ui::Unlock unlock{item.game, item.name.empty() ? std::wstring(tr(L"Osiągnięcie")) : item.name, nameOf(item.game), when(item.at, today)};
        unlock.image = iconSlot(item.icon);
        unlock.icon = unlock.image != profile_ui::no_image;
        if (!unlock.icon) unlock.image = coverSlot(item.game);
        if (item.rarity >= 0) { unlock.rare = item.rarity < 10; unlock.rarity = percent(item.rarity) + tr(L"% graczy"); }
        p.recent.push_back(std::move(unlock));
    }
    p.recent_first = std::clamp(p.recent_first, 0, std::max(0, static_cast<int>(p.recent.size()) - 1));

    // Twelve months.
    {
        const time_t stamp = static_cast<time_t>(now); tm local{}; localtime_s(&local, &stamp);
        const auto months = achievement_history::monthly(history_, local.tm_year + 1900, local.tm_mon + 1);
        uint32_t busiest = 0;
        for (const auto& month : months) busiest = std::max(busiest, month.count);
        for (size_t i = 0; i < months.size(); ++i) {
            p.months[i] = busiest ? static_cast<float>(months[i].count) / busiest : 0;
            p.month_labels[i] = dateText(months[i].year * 10000 + months[i].month * 100 + 1, L"MMM");
            p.month_counts[i] = months[i].count ? std::to_wstring(months[i].count) : std::wstring{};
        }
        tm january{}; january.tm_year = local.tm_year; january.tm_mon = 0; january.tm_mday = 1; january.tm_isdst = -1;
        const auto year_start = std::mktime(&january);
        p.history[0] = {tr(L"Łącznie"), std::to_wstring(unlocked_total), tr(L"zdobytych osiągnięć")};
        p.history[1] = {tr(L"W tym roku"), std::to_wstring(achievement_history::since(history_, year_start > 0 ? static_cast<uint64_t>(year_start) : 0)),
                        tr(L"z datą zdobycia")};
        p.history[2] = {tr(L"Gry na 100%"), std::to_wstring(perfect), perfect ? tr(L"wszystkie osiągnięcia") : std::wstring(tr(L"jeszcze żadnej"))};
        p.history[2].good = perfect > 0;
    }

    // Closest to 100 %.
    p.completion.clear();
    for (const auto& item : achievement_history::completion(progress)) {
        if (p.completion.size() >= profile_ui::max_completion) break;
        p.completion.push_back({item.game, nameOf(item.game), std::to_wstring(item.unlocked) + L" / " + std::to_wstring(item.total) + L"  \u00b7  " +
                                std::to_wstring(static_cast<int>(std::floor(item.share() * 100))) + L"%", item.share(), coverSlot(item.game)});
    }

    // Platforms: every store with games here, its account and its hours.
    struct Totals { size_t games = 0; uint64_t minutes = 0; uint32_t unlocked = 0; };
    std::map<int, Totals> per;
    const int emulation = 1000;
    for (const auto& [key, game] : games) {
        const int slot = game->kind == GameKind::nonsteam ? emulation : static_cast<int>(game->source);
        auto& totals = per[slot];
        ++totals.games; totals.minutes += minutesOf(*game);
        if (const auto found = progress.find(key); found != progress.end()) totals.unlocked += found->second.unlocked;
    }
    p.platforms.clear();
    const auto add = [&](int slot, GameSource source, std::wstring name, std::string brand, std::wstring account, bool connected) {
        const auto found = per.find(slot);
        if (found == per.end() || !found->second.games) return;
        const auto minutes = found->second.minutes;
        profile_ui::Platform item{source, std::move(brand), std::move(name), std::move(account), std::to_wstring(found->second.games) + tr(L" gier"),
                                  !minutes ? std::wstring{} : minutes >= 60 ? std::to_wstring(minutes / 60) + tr(L" godz.") : hoursMinutes(minutes), connected};
        if (found->second.unlocked) item.hours += (item.hours.empty() ? L"" : L"  \u00b7  ") + std::to_wstring(found->second.unlocked) + tr(L" osiągnięć");
        p.platforms.push_back(std::move(item));
    };
    const auto local_only = std::wstring(tr(L"Lokalne instalacje"));
    add(static_cast<int>(GameSource::steam), GameSource::steam, L"Steam", "steam", account_ && p.name != tr(L"Gracz") ? p.name : std::wstring(tr(L"Brak konta")), account_ != 0);
    add(static_cast<int>(GameSource::gog), GameSource::gog, L"GOG", "gog", gog_state_.account.empty() ? local_only : std::wstring(tr(L"Konto połączone")), !gog_state_.account.empty());
    add(static_cast<int>(GameSource::epic), GameSource::epic, L"Epic Games", "epic", epic_state_.account.empty() ? local_only : std::wstring(tr(L"Konto połączone")), !epic_state_.account.empty());
    add(static_cast<int>(GameSource::amazon), GameSource::amazon, L"Amazon Games", "amazon", amazon_state_.account.empty() ? local_only : std::wstring(tr(L"Konto połączone")), amazonConnected());
    add(static_cast<int>(GameSource::xbox), GameSource::xbox, L"Xbox", "xbox", tr(L"Aplikacja Xbox"), false);
    add(static_cast<int>(GameSource::ea), GameSource::ea, L"EA", "ea", ea_import_.snapshot ? std::wstring(tr(L"Biblioteka zaimportowana")) : local_only, ea_import_.snapshot.has_value());
    add(static_cast<int>(GameSource::ubisoft), GameSource::ubisoft, L"Ubisoft Connect", "ubisoft", ubisoft_account_ ? std::wstring(tr(L"Konto Ubisoft Connect")) : local_only, ubisoft_account_);
    add(static_cast<int>(GameSource::battlenet), GameSource::battlenet, L"Battle.net", "battlenet",
        battlenet_import_.snapshot ? std::wstring(tr(L"Biblioteka zaimportowana")) : local_only, battlenet_import_.snapshot.has_value());
    {
        std::wstring launcher = tr(L"Instancje Minecraft");
        for (const auto& client : pc_clients_)
            if (client.source == GameSource::multimc && !client.executable.empty())
                if (const auto flavor = multimc::flavorOf(client.executable)) launcher = multimc::flavorName(*flavor);
        add(static_cast<int>(GameSource::multimc), GameSource::multimc, sourceName(GameSource::multimc), "minecraft", launcher, false);
    }
    add(emulation, GameSource::steam, tr(L"Emulacja"), "retroarch", tr(L"Skróty Steam i emulatory"), false);
    add(static_cast<int>(GameSource::drmfree), GameSource::drmfree, L"DRM-free", "windows", tr(L"Katalogi na dysku"), false);
    add(static_cast<int>(GameSource::custom), GameSource::custom, tr(L"Moje gry"), {}, tr(L"Dodane ręcznie"), false);

    // Keep the focus on something that still exists.
    if (p.focus.section != Section::close && !p.count(p.focus.section)) p.focus = {Section::close, 0};
    p.focus.item = std::clamp(p.focus.item, 0, std::max(0, static_cast<int>(p.count(p.focus.section)) - 1));
    const auto view = renderer_.viewport();
    p.scroll_target = std::clamp(p.scroll_target, 0.0f, profile_ui::Layout{view.width, view.height}.maxScroll(p));
    bindProfileArt();
}

void Launcher::bindProfileArt() {
    renderer_.resetProfileImages(profile_slots_.size());
    for (size_t i = 0; i < profile_slots_.size(); ++i) {
        const auto& slot = profile_slots_[i];
        if (slot.kind == ProfileSlot::Kind::avatar) { renderer_.setProfileImage(i, shareImage(profile_avatar_)); continue; }
        if (slot.kind == ProfileSlot::Kind::icon) {
            if (const auto found = profile_icons_.find(slot.url); found != profile_icons_.end()) renderer_.setProfileImage(i, shareImage(found->second));
            continue;
        }
        const auto* art = art_.find(slot.key);
        if (art && art->cover) { renderer_.setProfileImage(i, art->cover); continue; }
        const auto game = std::find_if(public_library_.begin(), public_library_.end(), [&](const Game& item) { return gameKey(item) == slot.key; });
        if (game != public_library_.end()) requestArt(*game, CoverMessage::Kind::cover);
    }
}

void Launcher::profileArtArrived(const std::string& key, CoverMessage::Kind kind, const ImageBytes& image) {
    if (profile_.alpha <= 0 || !image || kind != CoverMessage::Kind::cover) return;
    for (size_t i = 0; i < profile_slots_.size(); ++i)
        if (profile_slots_[i].kind == ProfileSlot::Kind::cover && profile_slots_[i].key == key) {
            renderer_.setProfileImage(i, image);
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
}

// Steam's own files for unlocks since the last look (`steam`), and the latest achievements' icons.
void Launcher::startProfileScan(bool steam) {
    ProfileScanRequest request;
    request.icon_cache = userConfigDirectory() / L"achievements";
    request.english = i18n::english();
    if (steam) {
        request.steam_root = findSteamRoot();
        request.steam_id = account_;
        request.scanned = history_.scanned;
        for (const auto& game : public_library_)
            if (steamArtwork(game) && game.app_id && game.kind == GameKind::steam) {
                request.steam_games.emplace_back(gameKey(game), game.app_id);
                request.names[gameKey(game)] = game.name;
            }
    }
    // Icons of the achievements on the page that are not here yet.
    std::set<std::wstring> wanted;
    for (const auto& item : history_.unlocks) {
        if (wanted.size() >= profile_ui::max_recent) break;
        if (item.icon.empty() || profile_icons_.contains(item.icon)) continue;
        const auto game = std::find_if(public_library_.begin(), public_library_.end(), [&](const Game& g) { return gameKey(g) == item.game; });
        if (game == public_library_.end()) continue;
        if (!wanted.insert(item.icon).second) continue;
        request.icons.emplace_back(item.icon, steamArtwork(*game) ? std::to_wstring(game->app_id) : text_encoding::wideLenient(gameCacheKey(*game)));
    }
    if (request.steam_games.empty() && request.icons.empty() && request.steam_root.empty()) { profile_.loading = false; return; }
    profile_cancel_->store(true);
    profile_cancel_ = std::make_shared<std::atomic_bool>(false);
    const auto generation = ++profile_generation_;
    profile_.loading = steam;
    const auto deliver = [alive = alive_, window = hwnd_, generation, steam](ProfileScanResult result) {
        if (!alive->load()) return;
        auto message = std::make_unique<RemoteMessage>();
        message->run = [generation, steam, result = std::move(result)](Launcher& app) mutable { app.profileScanned(generation, steam, std::move(result)); };
        if (PostMessageW(window, remote_message, 0, reinterpret_cast<LPARAM>(message.get()))) message.release();
    };
    if (!runtime_started_ && profile_reader_ == nullptr) { profile_.loading = false; return; }
    const auto reader = profile_reader_ ? profile_reader_ : ProfileReader(readProfile);
    if (!work_.start([request = std::move(request), reader, deliver, cancel = profile_cancel_] {
            SetThreadPriority(GetCurrentThread(), THREAD_MODE_BACKGROUND_BEGIN);
            auto result = reader(request, [&] { return cancel->load(); });
            SetThreadPriority(GetCurrentThread(), THREAD_MODE_BACKGROUND_END);
            if (!cancel->load()) deliver(std::move(result));
        }, [deliver](std::exception_ptr) { deliver({}); }))
        profile_.loading = false;
}

void Launcher::profileScanned(uint32_t generation, bool steam, ProfileScanResult result) {
    if (generation != profile_generation_) return;
    profile_.loading = false;
    bool changed = false;
    if (!result.avatar.empty() && result.avatar != profile_avatar_) {
        profile_avatar_ = std::move(result.avatar);
        snitch_avatar_ = shareImage(profile_avatar_); // Snitch's home shows the same picture.
        changed = true;
    }
    if (steam) {
        const auto before = history_;
        for (auto& game : result.games) {
            auto name = game.name;
            if (const auto found = std::find_if(public_library_.begin(), public_library_.end(), [&](const Game& g) { return gameKey(g) == game.key; });
                found != public_library_.end()) name = found->name;
            achievement_history::merge(history_, game.key, name, game.unlocks, game.progress);
        }
        for (const auto& [key, stamp] : result.scanned) history_.scanned[key] = stamp;
        if (history_ != before) { achievement_history::save(history_, historyFile()); changed = true; }
    }
    for (auto& [url, bytes] : result.icons) if (!profile_icons_.contains(url)) { profile_icons_.emplace(url, std::move(bytes)); changed = true; }
    if (profile_icons_.size() > 64) profile_icons_.clear(); // Only the page's own few.
    if (!profile_.open) return;
    if (changed) rebuildProfile();
    // The unlocks just read may have icons not fetched yet.
    if (steam) startProfileScan(false);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

// ---- Keys, pad and pointer ----------------------------------------------------------------------

void Launcher::profileKey(WPARAM key, bool repeated) {
    auto& p = profile_;
    if (!p.open) return;
    p.pointer = false;
    if (key == VK_ESCAPE || key == VK_BACK) { if (!repeated) closeProfile(); return; }
    if (key == VK_RETURN || key == VK_SPACE) { if (!repeated) profileAccept(); return; }
    const auto view = renderer_.viewport();
    const profile_ui::Layout layout{view.width, view.height};
    if (key == VK_PRIOR || key == VK_NEXT) { profileScroll((key == VK_NEXT ? 1 : -1) * (view.height - 160)); return; }
    if (key == VK_HOME) { p.focus = {Section::close, 0}; p.scroll_target = 0; InvalidateRect(hwnd_, nullptr, FALSE); return; }
    if (key == VK_END) { p.scroll_target = layout.maxScroll(p); InvalidateRect(hwnd_, nullptr, FALSE); return; }
    const int dx = key == VK_LEFT || key == 'A' ? -1 : key == VK_RIGHT || key == 'D' ? 1 : 0;
    const int dy = key == VK_UP || key == 'W' ? -1 : key == VK_DOWN || key == 'S' ? 1 : 0;
    if (dx || dy) profileMove(dx, dy);
}

void Launcher::profileMove(int dx, int dy) {
    auto& p = profile_;
    const auto view = renderer_.viewport();
    const profile_ui::Layout layout{view.width, view.height};
    const auto before = p.focus;
    const auto columns = [&](Section section) {
        return section == Section::completion ? layout.completionColumns() : section == Section::platforms ? layout.platformColumns() : 1;
    };
    const auto enter = [&](int direction) {
        // The next section with something in it, up or down.
        int section = static_cast<int>(p.focus.section);
        for (section += direction; section >= 0 && section < profile_ui::section_count; section += direction) {
            const auto next = static_cast<Section>(section);
            if (!p.count(next)) continue;
            int item = 0;
            if (next == Section::recent) item = p.recent_first;
            else if (direction < 0 && columns(next) > 1) {
                const int count = static_cast<int>(p.count(next)), per = columns(next);
                item = std::min(count - 1, ((count - 1) / per) * per + std::min(p.focus.item % std::max(1, columns(p.focus.section)), per - 1));
            }
            p.focus = {next, item};
            return true;
        }
        return false;
    };
    const int count = static_cast<int>(p.count(p.focus.section));
    if (dx) {
        if (p.focus.section == Section::close) { if (dx > 0) enter(1); }
        else if (p.focus.section == Section::top) { /* One column. */ }
        else p.focus.item = std::clamp(p.focus.item + dx, 0, count - 1);
    } else if (dy) {
        const int per = columns(p.focus.section);
        const int next = p.focus.item + dy * per;
        if (p.focus.section == Section::top && next >= 0 && next < count) p.focus.item = p.focus.item + dy;
        else if (per > 1 && next >= 0 && next < count) p.focus.item = next;
        else if (per > 1 && dy > 0 && p.focus.item / per < (count - 1) / per) p.focus.item = count - 1; // A shorter last row.
        else if (!enter(dy)) {
            if (dy > 0) p.scroll_target = layout.maxScroll(p); // Past the last: the page's end shows.
            else { p.focus = {Section::close, 0}; }
        }
    }
    if (p.focus == before && !(dy > 0)) { navigationBoundary(); return; }
    // The achievement row slides to keep the focused card in view.
    if (p.focus.section == Section::recent) {
        const int shown = layout.unlockColumns();
        if (p.focus.item < p.recent_first) p.recent_first = p.focus.item;
        if (p.focus.item >= p.recent_first + shown) p.recent_first = p.focus.item - shown + 1;
    }
    p.scroll_target = layout.reveal(p, p.focus, p.scroll_target);
    audio_.navigate();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::profileAccept() {
    auto& p = profile_;
    const auto focus = p.focus;
    std::string key;
    switch (focus.section) {
    case Section::close: closeProfile(); return;
    case Section::top: if (focus.item < static_cast<int>(p.top.size())) key = p.top[static_cast<size_t>(focus.item)].key; break;
    case Section::recent: if (focus.item < static_cast<int>(p.recent.size())) key = p.recent[static_cast<size_t>(focus.item)].key; break;
    case Section::completion: if (focus.item < static_cast<int>(p.completion.size())) key = p.completion[static_cast<size_t>(focus.item)].key; break;
    case Section::platforms: {
        if (focus.item >= static_cast<int>(p.platforms.size())) return;
        const auto& platform = p.platforms[static_cast<size_t>(focus.item)];
        const auto source = platform.source;
        const bool emulation = platform.brand == "retroarch";
        closeProfile();
        if (desktop_mode_) { desktopShowSection(emulation ? "all" : std::string("source:") + sourceSlug(source)); return; }
        if (emulation) openLibrary(); else openPcLibrary(source);
        return;
    }
    }
    const auto game = std::find_if(public_library_.begin(), public_library_.end(), [&](const Game& item) { return gameKey(item) == key; });
    if (game == public_library_.end()) return;
    const auto chosen = *game;
    closeProfile();
    openDetails(chosen);
}

void Launcher::profileScroll(float amount) {
    auto& p = profile_;
    const auto view = renderer_.viewport();
    p.scroll_target = std::clamp(p.scroll_target + amount, 0.0f, profile_ui::Layout{view.width, view.height}.maxScroll(p));
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::profileClick(float x, float y) {
    auto& p = profile_;
    const auto view = renderer_.viewport();
    const auto hit = profile_ui::Layout{view.width, view.height}.hit(p, x, y);
    if (!hit) return;
    p.focus = *hit;
    profileAccept();
}

void Launcher::profilePointer(float x, float y) {
    auto& p = profile_;
    const auto view = renderer_.viewport();
    const auto hit = profile_ui::Layout{view.width, view.height}.hit(p, x, y);
    if (hit == p.hover) return;
    p.hover = hit;
    if (hit) { p.focus = *hit; p.pointer = true; }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

bool Launcher::profilePointerOver(float x, float y) const {
    const auto view = renderer_.viewport();
    return profile_ui::Layout{view.width, view.height}.hit(profile_, x, y).has_value();
}
