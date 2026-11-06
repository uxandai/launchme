#include "game_index.hpp"
#include "../net/json.hpp"
#include "../platform/text_encoding.hpp"
#include "../platform/unicode_text.hpp"

#ifdef _WIN32
#include <Windows.h>
#endif

#include <algorithm>
#include <charconv>
#include <cmath>
#include <ctime>
#include <map>
#include <random>
#include <set>

namespace game_index {
namespace {
constexpr std::array<const char*, source_count> source_keys{
    "library", "steam_appinfo", "steam_store", "rawg", "reviews", "hltb", "emulation", "gog", "epic", "steam_stats", "achievements", "nintendo"};

// Per-field trust: the first source with the field wins.
using Order = std::initializer_list<Source>;
// Source::library first: only the player's own metadata (Edytuj metadane) puts these facts there.
constexpr Order release_order{Source::library, Source::steam_appinfo, Source::steam_store, Source::gog, Source::epic, Source::nintendo, Source::rawg, Source::emulation};
constexpr Order studio_order{Source::library, Source::steam_appinfo, Source::steam_store, Source::gog, Source::epic, Source::nintendo, Source::rawg, Source::emulation};
constexpr Order genre_order{Source::library, Source::steam_appinfo, Source::steam_store, Source::rawg, Source::gog, Source::epic, Source::nintendo, Source::emulation};
constexpr Order tag_order{Source::steam_appinfo, Source::rawg, Source::gog, Source::epic};
constexpr Order description_order{Source::library, Source::steam_store, Source::gog, Source::epic, Source::nintendo, Source::rawg, Source::emulation};
constexpr Order franchise_order{Source::steam_appinfo, Source::gog, Source::rawg};
constexpr Order title_order{Source::library, Source::steam_appinfo, Source::steam_store, Source::gog, Source::epic, Source::rawg, Source::emulation};
constexpr Order review_order{Source::reviews, Source::steam_appinfo, Source::steam_store};
constexpr Order metacritic_order{Source::steam_appinfo, Source::steam_store, Source::reviews, Source::rawg};
constexpr Order rawg_order{Source::reviews, Source::rawg};
constexpr Order play_order{Source::steam_stats, Source::library};
constexpr Order library_order{Source::library};
constexpr Order hltb_order{Source::library, Source::hltb};
constexpr Order achievement_order{Source::steam_stats, Source::achievements};
constexpr Order alias_order{Source::library, Source::emulation, Source::rawg, Source::steam_store};

std::string utf8(std::wstring_view text) { return text_encoding::utf8Lenient(text); }
std::wstring wide(std::string_view text) { return text_encoding::wideLenient(text); }
std::wstring lowered(std::wstring_view text) {
    return unicode_text::lower(text);
}
bool contains(const std::vector<std::wstring>& list, std::wstring_view needle) {
    return std::any_of(list.begin(), list.end(), [&](const std::wstring& item) { return item.find(needle) != std::wstring::npos; });
}
// Words of a query, folded: "Half Life" → {"half", "life"}.
std::vector<std::wstring> words(std::wstring_view text) {
    std::vector<std::wstring> result;
    std::wstring current;
    const auto flush = [&] { if (auto word = fold(current); !word.empty()) result.push_back(std::move(word)); current.clear(); };
    for (const wchar_t ch : text) {
        if (ch == L' ' || ch == L'\t' || ch == L',' || ch == L'-' || ch == L':' || ch == L'/') flush();
        else current += ch;
    }
    flush();
    return result;
}
int monthNumber(std::wstring_view word) {
    const auto key = fold(word);
    if (key.size() < 3) return 0;
    // English, Polish, Portuguese and Spanish names, full or shortened ("lis", "listopada", "Nov",
    // "novembro", "noviembre"): their first three letters.
    static constexpr std::array<std::array<const wchar_t*, 5>, 12> months{{
        {L"jan", L"sty", L"jan", L"ene", L"jan"}, {L"feb", L"lut", L"fev", L"feb", L"feb"}, {L"mar", L"mar", L"mar", L"mar", L"mar"},
        {L"apr", L"kwi", L"abr", L"abr", L"apr"}, {L"may", L"maj", L"mai", L"may", L"may"}, {L"jun", L"cze", L"jun", L"jun", L"jun"},
        {L"jul", L"lip", L"jul", L"jul", L"jul"}, {L"aug", L"sie", L"ago", L"ago", L"aug"}, {L"sep", L"wrz", L"set", L"sep", L"sep"},
        {L"oct", L"paz", L"out", L"oct", L"oct"}, {L"nov", L"lis", L"nov", L"nov", L"nov"}, {L"dec", L"gru", L"dez", L"dic", L"dec"}}};
    for (size_t i = 0; i < months.size(); ++i)
        for (const auto* name : months[i]) if (key.starts_with(name)) return static_cast<int>(i) + 1;
    return 0;
}
bool validDay(int year, int month, int day) {
    if (year < 1950 || year > 2100 || month < 1 || month > 12 || day < 1) return false;
    static constexpr int days[] = {31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    return day <= days[month - 1] && (month != 2 || day < 29 || (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0)));
}
std::optional<int> number(std::wstring_view text) {
    if (text.empty() || text.size() > 4) return std::nullopt;
    int value = 0;
    for (const wchar_t ch : text) { if (ch < L'0' || ch > L'9') return std::nullopt; value = value * 10 + (ch - L'0'); }
    return value;
}

// Patch ⇄ JSON.
json::Value strings(const std::vector<std::wstring>& list) {
    auto array = json::array();
    for (const auto& item : list) array.elements.push_back(json::string(utf8(item)));
    return array;
}
std::vector<std::wstring> strings(const json::Value* value) {
    std::vector<std::wstring> list;
    if (!value || value->type != json::Value::Type::array) return list;
    for (const auto& item : value->elements) if (item.type == json::Value::Type::string && !item.text.empty()) list.push_back(wide(item.text));
    return list;
}
std::optional<uint64_t> whole(const json::Value* value, double maximum) {
    if (!value || value->type != json::Value::Type::number || !std::isfinite(value->numeric) || value->numeric < 0 || value->numeric > maximum) return std::nullopt;
    return static_cast<uint64_t>(value->numeric);
}
json::Value encodePatch(const Patch& patch) {
    auto out = json::object();
    auto& p = out.properties;
    const auto text = [&](const char* name, const std::optional<std::wstring>& value) { if (value) p[name] = json::string(utf8(*value)); };
    const auto list = [&](const char* name, const std::optional<std::vector<std::wstring>>& value) { if (value) p[name] = strings(*value); };
    const auto count = [&](const char* name, const auto& value) { if (value) p[name] = json::number(static_cast<double>(*value)); };
    text("title", patch.title); text("store", patch.store); text("platform", patch.platform); text("franchise", patch.franchise); text("desc", patch.description);
    list("aliases", patch.aliases); list("dev", patch.developers); list("pub", patch.publishers); list("genres", patch.genres); list("tags", patch.tags);
    if (patch.release) p["release"] = json::string(utf8(patch.release->iso()));
    count("reviews", patch.review_percent); count("metacritic", patch.metacritic); count("review_count", patch.review_count);
    count("review_score", patch.review_score);
    if (patch.rawg_rating) p["rawg"] = json::number(std::round(*patch.rawg_rating * 100) / 100);
    count("hltb_main", patch.hltb_main); count("hltb_extra", patch.hltb_extra); count("hltb_complete", patch.hltb_complete);
    count("playtime", patch.playtime_minutes); count("ach_unlocked", patch.achievements_unlocked); count("ach_total", patch.achievements_total);
    if (patch.installed) p["installed"] = json::boolean(*patch.installed);
    count("first_seen", patch.first_seen); count("last_played", patch.last_played);
    if (patch.updated) p["u"] = json::number(static_cast<double>(patch.updated));
    return out;
}
Patch decodePatch(const json::Value& in) {
    Patch patch;
    const auto text = [&](const char* name, std::optional<std::wstring>& value) {
        if (const auto* item = in.get(name); item && item->type == json::Value::Type::string) value = wide(item->text);
    };
    const auto list = [&](const char* name, std::optional<std::vector<std::wstring>>& value) {
        if (const auto* item = in.get(name); item && item->type == json::Value::Type::array) value = strings(item);
    };
    const auto count = [&](const char* name, auto& value, double maximum) {
        if (const auto parsed = whole(in.get(name), maximum)) value = static_cast<std::remove_reference_t<decltype(*value)>>(*parsed);
    };
    text("title", patch.title); text("store", patch.store); text("platform", patch.platform); text("franchise", patch.franchise); text("desc", patch.description);
    list("aliases", patch.aliases); list("dev", patch.developers); list("pub", patch.publishers); list("genres", patch.genres); list("tags", patch.tags);
    if (const auto* release = in.get("release"); release && release->type == json::Value::Type::string)
        if (const auto date = parseDate(wide(release->text)); date.known()) patch.release = date;
    count("reviews", patch.review_percent, 100); count("metacritic", patch.metacritic, 100); count("review_count", patch.review_count, UINT32_MAX);
    count("review_score", patch.review_score, 9);
    if (const auto* rawg = in.get("rawg"); rawg && rawg->type == json::Value::Type::number && rawg->numeric >= 0 && rawg->numeric <= 5)
        patch.rawg_rating = static_cast<float>(rawg->numeric);
    count("hltb_main", patch.hltb_main, UINT32_MAX); count("hltb_extra", patch.hltb_extra, UINT32_MAX); count("hltb_complete", patch.hltb_complete, UINT32_MAX);
    count("playtime", patch.playtime_minutes, UINT32_MAX); count("ach_unlocked", patch.achievements_unlocked, 100000); count("ach_total", patch.achievements_total, 100000);
    if (const auto* installed = in.get("installed"); installed && installed->type == json::Value::Type::boolean) patch.installed = installed->flag;
    count("first_seen", patch.first_seen, 4102444800.0); count("last_played", patch.last_played, 4102444800.0);
    if (const auto updated = whole(in.get("u"), 4102444800.0)) patch.updated = *updated;
    return patch;
}

uint64_t seconds(uint64_t value) { return value > 100000000000ull ? value / 1000 : value; } // Milliseconds too.
// The Wilson lower bound (95%) of a share `p` of positive votes out of `n`, as 0–100.
float wilson(double p, double n) {
    if (n <= 0) return -1;
    constexpr double z = 1.96;
    const double z2 = z * z;
    const double bound = (p + z2 / (2 * n) - z * std::sqrt((p * (1 - p) + z2 / (4 * n)) / n)) / (1 + z2 / n);
    return static_cast<float>(std::clamp(bound, 0.0, 1.0) * 100);
}
bool completed(const Record& record) { return record.achievements_total && record.achievements_unlocked >= record.achievements_total; }
} // namespace

float ratingScore(const Record& record) {
    if (record.review_percent) {
        // Without a count, Steam's label tells roughly how many voted (9 and 8 need many).
        double votes = record.review_count ? *record.review_count : 0;
        if (!votes && record.review_score) votes = *record.review_score >= 9 ? 500 : *record.review_score == 8 ? 50 : *record.review_score >= 5 ? 20 : 10;
        if (!votes) votes = 20;
        return wilson(*record.review_percent / 100.0, votes);
    }
    if (record.rawg_rating) return wilson(*record.rawg_rating / 5.0, 40);
    if (record.metacritic) return wilson(*record.metacritic / 100.0, 30);
    return -1;
}

std::wstring Date::iso() const {
    if (!known()) return {};
    wchar_t buffer[16];
    if (month && day) swprintf_s(buffer, L"%04d-%02d-%02d", year, month, day);
    else if (month) swprintf_s(buffer, L"%04d-%02d", year, month);
    else swprintf_s(buffer, L"%04d", year);
    return buffer;
}

std::wstring dateLabel(const Date& date, bool english) {
    if (!date.known()) return {};
    static constexpr const wchar_t* en[]{L"Jan", L"Feb", L"Mar", L"Apr", L"May", L"Jun", L"Jul", L"Aug", L"Sep", L"Oct", L"Nov", L"Dec"};
    static constexpr const wchar_t* of[]{L"stycznia", L"lutego", L"marca", L"kwietnia", L"maja", L"czerwca", L"lipca", L"sierpnia", L"września",
                                          L"października", L"listopada", L"grudnia"};
    static constexpr const wchar_t* pl[]{L"styczeń", L"luty", L"marzec", L"kwiecień", L"maj", L"czerwiec", L"lipiec", L"sierpień", L"wrzesień",
                                          L"październik", L"listopad", L"grudzień"};
    const auto year = std::to_wstring(date.year);
    if (date.month < 1 || date.month > 12) return year;
    const size_t month = static_cast<size_t>(date.month - 1);
    if (!date.day) return std::wstring(english ? en[month] : pl[month]) + L" " + year;
    return english ? std::wstring(en[month]) + L" " + std::to_wstring(date.day) + L", " + year
                   : std::to_wstring(date.day) + L" " + of[month] + L" " + year;
}

std::wstring releaseLabel(std::wstring_view text, bool english) {
    const auto date = parseDate(text);
    if (!date.known() || !date.month || !date.day) return std::wstring(text);
    return dateLabel(date, english);
}

Date dateFromUnix(uint64_t value) {
    if (!value) return {};
    const auto time = static_cast<std::time_t>(seconds(value));
    std::tm utc{};
    if (gmtime_s(&utc, &time)) return {};
    Date date{utc.tm_year + 1900, utc.tm_mon + 1, utc.tm_mday};
    return date.year >= 1950 && date.year <= 2100 ? date : Date{};
}

Date parseDate(std::wstring_view text) {
    // ISO and ES-DE: digits first.
    std::wstring digits;
    size_t at = 0;
    while (at < text.size() && text[at] == L' ') ++at;
    if (at + 4 <= text.size() && number(text.substr(at, 4))) {
        const auto rest = text.substr(at);
        const auto year = *number(rest.substr(0, 4));
        if (rest.size() >= 8 && number(rest.substr(4, 2)) && number(rest.substr(6, 2)) && (rest.size() == 8 || rest[8] == L'T')) { // 20041116T000000
            const int month = *number(rest.substr(4, 2)), day = *number(rest.substr(6, 2));
            if (validDay(year, month, day)) return {year, month, day};
        }
        if (rest.size() >= 7 && rest[4] == L'-' && number(rest.substr(5, 2))) { // 2004-11[-16]
            const int month = *number(rest.substr(5, 2));
            if (rest.size() >= 10 && rest[7] == L'-' && number(rest.substr(8, 2)) && validDay(year, month, *number(rest.substr(8, 2))))
                return {year, month, *number(rest.substr(8, 2))};
            if (month >= 1 && month <= 12 && year >= 1950 && year <= 2100) return {year, month, 0};
        }
    }
    // Words: "16 Nov, 2004", "Nov 16, 2004", "16 lis 2004", "16 listopada 2004", "Nov 2004", "Q4 2004", "2004".
    std::vector<std::wstring> parts;
    std::wstring current;
    for (const wchar_t ch : text) {
        if (ch == L' ' || ch == L',' || ch == L'.' || ch == L'/' || ch == L'-') { if (!current.empty()) parts.push_back(std::move(current)); current.clear(); }
        else current += ch;
    }
    if (!current.empty()) parts.push_back(std::move(current));
    int year = 0, month = 0, day = 0;
    for (const auto& part : parts) {
        if (const auto value = number(part)) {
            if (part.size() == 4 && *value >= 1950 && *value <= 2100) year = *value;
            else if (part.size() <= 2 && *value >= 1 && *value <= 31 && !day) day = *value;
            continue;
        }
        if (!month) month = monthNumber(part);
    }
    if (!year) return {};
    if (month && day && validDay(year, month, day)) return {year, month, day};
    return {year, month, 0};
}

const char* sourceKey(Source source) { return source_keys[static_cast<size_t>(source)]; }
std::optional<Source> sourceFromKey(std::string_view key) {
    for (size_t i = 0; i < source_keys.size(); ++i) if (key == source_keys[i]) return static_cast<Source>(i);
    return std::nullopt;
}
std::optional<Sort> sortFromKey(std::string_view key) {
    static constexpr std::pair<const char*, Sort> keys[]{
        {"relevance", Sort::relevance}, {"title", Sort::title}, {"release_asc", Sort::release_asc}, {"oldest", Sort::release_asc},
        {"release_desc", Sort::release_desc}, {"newest", Sort::release_desc}, {"last_played", Sort::last_played}, {"playtime", Sort::playtime},
        {"rating", Sort::rating}, {"metacritic", Sort::metacritic}, {"shortest", Sort::shortest}, {"longest", Sort::longest},
        {"added", Sort::added}, {"random", Sort::random}};
    for (const auto& [name, sort] : keys) if (key == name) return sort;
    return std::nullopt;
}
std::optional<Status> statusFromKey(std::string_view key) {
    static constexpr std::pair<const char*, Status> keys[]{
        {"any", Status::any}, {"installed", Status::installed}, {"not_installed", Status::not_installed}, {"never_played", Status::never_played},
        {"played", Status::played}, {"recent", Status::recent}, {"completed", Status::completed}};
    for (const auto& [name, status] : keys) if (key == name) return status;
    return std::nullopt;
}
std::optional<Group> groupFromKey(std::string_view key) {
    static constexpr std::pair<const char*, Group> keys[]{
        {"genre", Group::genre}, {"tag", Group::tag}, {"developer", Group::developer}, {"publisher", Group::publisher},
        {"franchise", Group::franchise}, {"year", Group::year}, {"decade", Group::decade}, {"store", Group::store}, {"status", Group::status}};
    for (const auto& [name, group] : keys) if (key == name) return group;
    return std::nullopt;
}

std::wstring fold(std::wstring_view text) {
    if (text.empty()) return {};
    auto lower = lowered(text);
    for (auto& ch : lower) if (ch == L'ł') ch = L'l'; else if (ch == L'đ') ch = L'd'; else if (ch == L'ø') ch = L'o';
    // Accents go: "Wiedźmin" == "Wiedzmin".
    std::wstring result;
    for (const wchar_t ch : unicode_text::decompose(lower))
        if (unicode_text::isLetterOrDigit(ch) && !unicode_text::isMark(ch)) result += ch;
    return result;
}

std::wstring canonicalGenre(std::wstring_view name) {
    static const std::pair<std::wstring_view, std::wstring_view> polish[]{
        {L"akcja", L"Action"}, {L"akcji", L"Action"}, {L"przygodowa", L"Adventure"}, {L"przygodowe", L"Adventure"}, {L"przygodowka", L"Adventure"},
        {L"przygodowki", L"Adventure"}, {L"strategia", L"Strategy"}, {L"strategie", L"Strategy"}, {L"strategiczna", L"Strategy"}, {L"strategiczne", L"Strategy"},
        {L"symulacja", L"Simulation"}, {L"symulacje", L"Simulation"}, {L"symulator", L"Simulation"}, {L"symulatory", L"Simulation"},
        {L"wyscigi", L"Racing"}, {L"wyscigowa", L"Racing"}, {L"wyscigowe", L"Racing"}, {L"sportowa", L"Sports"}, {L"sportowe", L"Sports"},
        {L"niezalezne", L"Indie"}, {L"niezalezna", L"Indie"}, {L"swobodne", L"Casual"}, {L"casualowe", L"Casual"}, {L"groza", L"Horror"},
        {L"platformowka", L"Platformer"}, {L"platformowki", L"Platformer"}, {L"platformowe", L"Platformer"}, {L"logiczna", L"Puzzle"},
        {L"logiczne", L"Puzzle"}, {L"lamiglowka", L"Puzzle"}, {L"lamiglowki", L"Puzzle"}, {L"zagadki", L"Puzzle"}, {L"strzelanka", L"Shooter"},
        {L"strzelanki", L"Shooter"}, {L"bijatyka", L"Fighting"}, {L"bijatyki", L"Fighting"}, {L"otwartyswiat", L"Open World"},
        {L"wieloosobowa", L"Multiplayer"}, {L"wieloosobowe", L"Multiplayer"}, {L"kooperacja", L"Co-op"}, {L"kooperacyjne", L"Co-op"},
        {L"powiescwizualna", L"Visual Novel"}, {L"skradanka", L"Stealth"}, {L"skradanki", L"Stealth"}, {L"przetrwanie", L"Survival"},
        {L"karcianka", L"Card Game"}, {L"karcianki", L"Card Game"}, {L"taktyczna", L"Tactical"}, {L"taktyczne", L"Tactical"},
        {L"turowa", L"Turn-Based"}, {L"turowe", L"Turn-Based"}, {L"rytmiczna", L"Rhythm"}, {L"muzyczna", L"Music"}, {L"wczesnydostep", L"Early Access"},
        {L"darmowa", L"Free To Play"}, {L"darmowe", L"Free To Play"}, {L"mmo", L"Massively Multiplayer"}, {L"fantastykanaukowa", L"Sci-fi"},
        {L"budowanie", L"Building"}, {L"ekonomiczna", L"Economy"}, {L"edukacyjna", L"Education"}, {L"sandboks", L"Sandbox"}};
    const auto key = fold(name);
    for (const auto& [word, english] : polish) if (key == word) return std::wstring(english);
    return std::wstring(name);
}

std::wstring genreLabel(std::wstring_view name, bool english) {
    if (english) return std::wstring(name);
    static const std::pair<std::wstring_view, std::wstring_view> labels[]{
        {L"action", L"Akcja"}, {L"adventure", L"Przygodowe"}, {L"strategy", L"Strategie"}, {L"casual", L"Swobodne"}, {L"racing", L"Wyścigi"},
        {L"sports", L"Sportowe"}, {L"indie", L"Niezależne"}, {L"simulation", L"Symulacje"}, {L"massivelymultiplayer", L"MMO"},
        {L"earlyaccess", L"Wczesny dostęp"}, {L"puzzle", L"Logiczne"}, {L"platformer", L"Platformówki"}, {L"shooter", L"Strzelanki"},
        {L"fighting", L"Bijatyki"}, {L"arcade", L"Zręcznościowe"}, {L"family", L"Rodzinne"}, {L"board games", L"Planszowe"},
        {L"boardgames", L"Planszowe"}, {L"card", L"Karciane"}, {L"educational", L"Edukacyjne"}};
    const auto key = fold(name);
    for (const auto& [word, label] : labels) if (key == word) return std::wstring(label);
    return std::wstring(name);
}

bool Patch::empty() const {
    Patch blank;
    blank.updated = updated;
    return *this == blank;
}

bool Index::merge(const std::string& key, Source source, Patch patch) {
    if (key.empty()) return false;
    const bool empty = patch.empty();
    auto found = entries_.find(key);
    if (found == entries_.end()) {
        if (empty) return false;
        found = entries_.emplace(key, Entry{}).first;
    }
    auto& entry = found->second;
    auto& slot = entry.patches[static_cast<size_t>(source)];
    if (empty && !slot) return false;
    if (!empty && slot) {
        // The same facts again: only the time they were confirmed moves on.
        Patch same = patch;
        same.updated = slot->updated;
        if (same == *slot) { slot->updated = std::max(slot->updated, patch.updated); return false; }
    }
    if (empty) slot.reset(); else slot = std::move(patch);
    if (std::none_of(entry.patches.begin(), entry.patches.end(), [](const auto& item) { return item.has_value(); })) entries_.erase(found);
    else compose(key, entry);
    ++revision_;
    return true;
}

void Index::syncLibrary(const std::vector<std::pair<std::string, Patch>>& games, uint64_t now, uint64_t keep_days) {
    std::set<std::string_view> present;
    bool changed = false;
    for (const auto& [key, patch] : games) {
        if (key.empty()) continue;
        present.insert(key);
        auto& entry = entries_[key];
        auto& slot = entry.patches[static_cast<size_t>(Source::library)];
        // Only real changes count: `updated` of an otherwise equal patch is not news.
        Patch incoming = patch;
        if (slot) incoming.updated = slot->updated;
        if (!slot || !(*slot == incoming) || entry.gone_since) {
            slot = patch;
            entry.gone_since = 0;
            compose(key, entry);
            changed = true;
        }
    }
    for (auto it = entries_.begin(); it != entries_.end();) {
        auto& [key, entry] = *it;
        if (present.contains(key)) { ++it; continue; }
        if (!entry.gone_since) { entry.gone_since = now; entry.record.owned = false; changed = true; }
        if (now > entry.gone_since + keep_days * 86400) { it = entries_.erase(it); changed = true; continue; }
        ++it;
    }
    if (changed) ++revision_;
}

void Index::compose(const std::string& key, Entry& entry) {
    Record record;
    record.key = key;
    const auto& patches = entry.patches;
    const auto first = [&](Order order, auto member) -> const Patch* {
        for (const auto source : order) {
            const auto& patch = patches[static_cast<size_t>(source)];
            if (patch && (*patch).*member) return &*patch;
        }
        return nullptr;
    };
    const auto take = [&](Order order, auto member, auto& field) {
        if (const auto* patch = first(order, member)) field = *((*patch).*member);
    };
    const auto takeList = [&](Order order, auto member, std::vector<std::wstring>& field) {
        for (const auto source : order) {
            const auto& patch = patches[static_cast<size_t>(source)];
            if (patch && (*patch).*member && !((*patch).*member)->empty()) { field = *((*patch).*member); return; }
        }
    };
    take(title_order, &Patch::title, record.title);
    take(library_order, &Patch::store, record.store);
    take(library_order, &Patch::platform, record.platform);
    take(franchise_order, &Patch::franchise, record.franchise);
    take(description_order, &Patch::description, record.description);
    takeList(studio_order, &Patch::developers, record.developers);
    takeList(studio_order, &Patch::publishers, record.publishers);
    takeList(genre_order, &Patch::genres, record.genres);
    takeList(tag_order, &Patch::tags, record.tags);
    // Every source's other names for it.
    for (const auto source : alias_order)
        if (const auto& patch = patches[static_cast<size_t>(source)]; patch && patch->aliases)
            for (const auto& alias : *patch->aliases)
                if (!alias.empty() && alias != record.title && std::find(record.aliases.begin(), record.aliases.end(), alias) == record.aliases.end() && record.aliases.size() < 6)
                    record.aliases.push_back(alias);
    for (const auto source : release_order) // The first source with a usable date; a fuller date of the same year refines it.
        if (const auto& patch = patches[static_cast<size_t>(source)]; patch && patch->release && patch->release->known()) {
            if (!record.release.known()) record.release = *patch->release;
            else if (patch->release->year == record.release.year && patch->release->sortKey() > record.release.sortKey() && !record.release.day)
                record.release = *patch->release;
        }
    if (const auto* patch = first(review_order, &Patch::review_percent)) record.review_percent = patch->review_percent;
    if (const auto* patch = first(review_order, &Patch::review_count)) record.review_count = patch->review_count;
    if (const auto* patch = first(review_order, &Patch::review_score)) record.review_score = patch->review_score;
    if (const auto* patch = first(metacritic_order, &Patch::metacritic)) record.metacritic = patch->metacritic;
    if (const auto* patch = first(rawg_order, &Patch::rawg_rating)) record.rawg_rating = patch->rawg_rating;
    if (const auto* patch = first(hltb_order, &Patch::hltb_main)) record.hltb_main = patch->hltb_main;
    if (const auto* patch = first(hltb_order, &Patch::hltb_extra)) record.hltb_extra = patch->hltb_extra;
    if (const auto* patch = first(hltb_order, &Patch::hltb_complete)) record.hltb_complete = patch->hltb_complete;
    if (const auto* patch = first(play_order, &Patch::playtime_minutes)) record.playtime_minutes = patch->playtime_minutes;
    take(achievement_order, &Patch::achievements_unlocked, record.achievements_unlocked);
    take(achievement_order, &Patch::achievements_total, record.achievements_total);
    take(library_order, &Patch::installed, record.installed);
    take(library_order, &Patch::first_seen, record.first_seen);
    take(library_order, &Patch::last_played, record.last_played);
    record.first_seen = seconds(record.first_seen);
    record.last_played = seconds(record.last_played);
    for (size_t i = 0; i < source_count; ++i) if (patches[i]) record.sources |= 1u << i;
    record.owned = patches[static_cast<size_t>(Source::library)].has_value() && !entry.gone_since;
    entry.folded_title = fold(record.title);
    entry.folded_names.clear(); entry.folded_labels.clear();
    for (const auto* list : {&record.aliases, &record.developers, &record.publishers})
        for (const auto& item : *list) entry.folded_names.push_back(fold(item));
    if (!record.franchise.empty()) entry.folded_names.push_back(fold(record.franchise));
    for (const auto* list : {&record.genres, &record.tags}) for (const auto& item : *list) entry.folded_labels.push_back(fold(item));
    entry.record = std::move(record);
}

const Record* Index::get(std::string_view key) const {
    const auto found = entries_.find(std::string(key));
    return found == entries_.end() ? nullptr : &found->second.record;
}

const Patch* Index::patch(std::string_view key, Source source) const {
    const auto found = entries_.find(std::string(key));
    if (found == entries_.end()) return nullptr;
    const auto& slot = found->second.patches[static_cast<size_t>(source)];
    return slot ? &*slot : nullptr;
}

std::vector<std::string> Index::keys() const {
    std::vector<std::string> result;
    for (const auto& [key, entry] : entries_) if (entry.record.owned) result.push_back(key);
    std::sort(result.begin(), result.end());
    return result;
}

std::vector<std::string> Index::withLabel(std::wstring_view word) const {
    const auto typed = fold(word), english = fold(canonicalGenre(word));
    if (typed.size() < 3) return {};
    std::vector<std::string> result;
    for (const auto& [key, entry] : entries_) {
        if (!entry.record.owned) continue;
        if (std::any_of(entry.folded_labels.begin(), entry.folded_labels.end(), [&](const std::wstring& label) {
                return label.starts_with(typed) || label == english; }))
            result.push_back(key);
    }
    return result;
}

std::unordered_map<std::string, int> Index::releases() const {
    std::unordered_map<std::string, int> result;
    for (const auto& [key, entry] : entries_)
        if (entry.record.owned && entry.record.release.known()) result.emplace(key, entry.record.release.sortKey());
    return result;
}

std::vector<const Record*> Index::lookup(std::wstring_view title, size_t limit) const {
    const auto wanted = fold(title);
    if (wanted.empty()) return {};
    const auto lower = lowered(title);
    struct Hit { int rank; const Record* record; };
    std::vector<Hit> hits;
    for (const auto& [key, entry] : entries_) {
        if (!entry.record.owned) continue;
        int rank = -1;
        if (lowered(entry.record.title) == lower) rank = 0;
        else if (entry.folded_title == wanted) rank = 1;
        else if (std::any_of(entry.record.aliases.begin(), entry.record.aliases.end(), [&](const auto& alias) { return fold(alias) == wanted; })) rank = 2;
        else if (entry.folded_title.starts_with(wanted)) rank = 3;
        else if (entry.folded_title.find(wanted) != std::wstring::npos) rank = 4;
        else if (wanted.size() >= 4 && wanted.find(entry.folded_title) != std::wstring::npos && entry.folded_title.size() >= 4) rank = 5; // "Elden Ring GOTY" → Elden Ring.
        if (rank >= 0) hits.push_back({rank, &entry.record});
    }
    std::sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) {
        if (a.rank != b.rank) return a.rank < b.rank;
        if (a.record->title.size() != b.record->title.size()) return a.record->title.size() < b.record->title.size();
        return a.record->key < b.record->key;
    });
    std::vector<const Record*> result;
    for (const auto& hit : hits) { if (result.size() >= limit) break; result.push_back(hit.record); }
    return result;
}

bool Index::matches(const Entry& entry, const Query& query, const std::vector<std::wstring>& text) const {
    const auto& record = entry.record;
    if (!record.owned) return false;
    for (const auto& word : text)
        if (entry.folded_title.find(word) == std::wstring::npos && !contains(entry.folded_names, word)) return false;
    for (const auto* list : {&query.genres, &query.tags})
        for (const auto& label : *list) {
            const auto wanted = fold(canonicalGenre(label)), raw = fold(label);
            if (wanted.empty()) continue;
            if (!contains(entry.folded_labels, wanted) && (raw == wanted || !contains(entry.folded_labels, raw))) return false;
        }
    if (!query.developer.empty()) {
        const auto wanted = fold(query.developer);
        bool found = false;
        for (const auto* list : {&record.developers, &record.publishers})
            for (const auto& name : *list) found = found || fold(name).find(wanted) != std::wstring::npos;
        if (!found) return false;
    }
    if (!query.store.empty()) {
        const auto wanted = fold(query.store);
        if (fold(record.store).find(wanted) == std::wstring::npos && fold(record.platform).find(wanted) == std::wstring::npos) return false;
    }
    if (query.year_from || query.year_to) {
        if (!record.release.known()) return false;
        if (query.year_from && record.release.year < query.year_from) return false;
        if (query.year_to && record.release.year > query.year_to) return false;
    }
    switch (query.status) {
    case Status::installed: return record.installed;
    case Status::not_installed: return !record.installed;
    case Status::never_played: return !record.played();
    case Status::played: return record.played();
    case Status::recent: return record.last_played && query.now && record.last_played + 30 * 86400 >= query.now;
    case Status::completed: return completed(record);
    default: return true;
    }
}

Found Index::search(const Query& query) const {
    const auto text = words(query.text);
    std::vector<const Entry*> hits;
    for (const auto& [key, entry] : entries_) if (matches(entry, query, text)) hits.push_back(&entry);
    Found found;
    found.total = hits.size();
    for (const auto* entry : hits) {
        const auto& record = entry->record;
        if (!record.release.known()) ++found.undated;
        if (ratingScore(record) < 0) ++found.unrated;
        if (!record.hltb_main) ++found.untimed;
    }
    const auto byTitle = [](const Entry* a, const Entry* b) {
        return a->folded_title != b->folded_title ? a->folded_title < b->folded_title : a->record.key < b->record.key;
    };
    // Unknown values go last whatever the direction.
    const auto known = [&](auto value, auto unknown, bool ascending) {
        return [=](const Entry* a, const Entry* b) {
            const auto x = value(a->record), y = value(b->record);
            const bool ux = x == unknown, uy = y == unknown;
            if (ux != uy) return uy;
            if (x != y) return ascending ? x < y : x > y;
            return a->folded_title != b->folded_title ? a->folded_title < b->folded_title : a->record.key < b->record.key;
        };
    };
    const std::wstring whole = fold(query.text);
    switch (query.sort) {
    case Sort::title: std::sort(hits.begin(), hits.end(), byTitle); break;
    case Sort::release_asc: std::sort(hits.begin(), hits.end(), known([](const Record& r) { return r.release.sortKey(); }, 0, true)); break;
    case Sort::release_desc: std::sort(hits.begin(), hits.end(), known([](const Record& r) { return r.release.sortKey(); }, 0, false)); break;
    case Sort::last_played: std::sort(hits.begin(), hits.end(), known([](const Record& r) { return r.last_played; }, uint64_t{0}, false)); break;
    case Sort::playtime: std::sort(hits.begin(), hits.end(), known([](const Record& r) { return r.playtime_minutes.value_or(0); }, uint32_t{0}, false)); break;
    case Sort::rating: std::sort(hits.begin(), hits.end(), known([](const Record& r) { return ratingScore(r); }, -1.0f, false)); break;
    case Sort::metacritic: std::sort(hits.begin(), hits.end(), known([](const Record& r) { return r.metacritic.value_or(-1); }, -1, false)); break;
    case Sort::shortest: std::sort(hits.begin(), hits.end(), known([](const Record& r) { return r.hltb_main.value_or(0); }, uint32_t{0}, true)); break;
    case Sort::longest: std::sort(hits.begin(), hits.end(), known([](const Record& r) { return r.hltb_main.value_or(0); }, uint32_t{0}, false)); break;
    case Sort::added: std::sort(hits.begin(), hits.end(), known([](const Record& r) { return r.first_seen; }, uint64_t{0}, false)); break;
    case Sort::random: {
        std::sort(hits.begin(), hits.end(), byTitle);
        std::mt19937 random(query.seed);
        std::shuffle(hits.begin(), hits.end(), random);
        break;
    }
    default: // Relevance: the title itself, then its start, then recently played.
        std::sort(hits.begin(), hits.end(), [&](const Entry* a, const Entry* b) {
            const auto rank = [&](const Entry* e) { return whole.empty() ? 2 : e->folded_title == whole ? 0 : e->folded_title.starts_with(whole) ? 1 : 2; };
            const int ra = rank(a), rb = rank(b);
            if (ra != rb) return ra < rb;
            if (a->record.last_played != b->record.last_played) return a->record.last_played > b->record.last_played;
            return byTitle(a, b);
        });
    }
    for (size_t i = query.offset; i < hits.size() && found.games.size() < query.limit; ++i) found.games.push_back(&hits[i]->record);
    return found;
}

Stats Index::stats(Group by, size_t limit, const Query* filter) const {
    Stats result;
    std::map<std::wstring, Bucket> buckets;
    const auto text = filter ? words(filter->text) : std::vector<std::wstring>{};
    for (const auto& [key, entry] : entries_) {
        const auto& record = entry.record;
        if (filter ? !matches(entry, *filter, text) : !record.owned) continue;
        ++result.total;
        if (record.installed) ++result.installed;
        if (record.played()) ++result.played; else ++result.never_played;
        if (record.release.known()) ++result.dated;
        if (completed(record)) ++result.completed;
        const uint64_t minutes = record.playtime_minutes.value_or(0);
        result.minutes += minutes;
        std::vector<std::wstring> names;
        switch (by) {
        case Group::genre: names = record.genres; break;
        case Group::tag: names = record.tags; break;
        case Group::developer: names = record.developers; break;
        case Group::publisher: names = record.publishers; break;
        case Group::franchise: if (!record.franchise.empty()) names.push_back(record.franchise); break;
        case Group::year: names.push_back(record.release.known() ? std::to_wstring(record.release.year) : L"?"); break;
        case Group::decade: names.push_back(record.release.known() ? std::to_wstring(record.release.year / 10 * 10) + L"s" : L"?"); break;
        case Group::store: names.push_back(record.platform.empty() ? record.store : record.platform); break;
        case Group::status:
            names.push_back(completed(record) ? L"completed" : record.played() ? L"played" : record.installed ? L"installed, never played" : L"never played");
            break;
        }
        for (const auto& name : names) {
            if (name.empty()) continue;
            auto& bucket = buckets[name];
            bucket.name = name;
            ++bucket.games;
            if (record.played()) ++bucket.played;
            bucket.minutes += minutes;
        }
    }
    for (auto& [name, bucket] : buckets) result.buckets.push_back(std::move(bucket));
    std::sort(result.buckets.begin(), result.buckets.end(), [&](const Bucket& a, const Bucket& b) {
        if (by == Group::year || by == Group::decade) return a.name < b.name; // Chronological.
        return a.games != b.games ? a.games > b.games : a.name < b.name;
    });
    if (by != Group::year && by != Group::decade && result.buckets.size() > limit) result.buckets.resize(limit);
    if ((by == Group::year || by == Group::decade) && result.buckets.size() > limit) // The most recent ones.
        result.buckets.erase(result.buckets.begin(), result.buckets.end() - static_cast<std::ptrdiff_t>(limit));
    return result;
}

std::vector<std::pair<const Record*, float>> Index::similar(std::string_view key, size_t limit) const {
    const auto found = entries_.find(std::string(key));
    if (found == entries_.end()) return {};
    using Features = std::map<std::wstring, float>;
    const auto features = [](const Record& record) {
        Features result;
        for (const auto& genre : record.genres) result[L"g:" + fold(genre)] = 1.0f;
        for (size_t i = 0; i < record.tags.size() && i < 15; ++i) result[L"t:" + fold(record.tags[i])] = 1.0f;
        for (const auto& developer : record.developers) result[L"d:" + fold(developer)] = 2.0f;
        if (!record.franchise.empty()) result[L"f:" + fold(record.franchise)] = 3.0f;
        return result;
    };
    const auto mine = features(found->second.record);
    if (mine.empty()) return {};
    std::vector<std::pair<const Record*, float>> result;
    for (const auto& [other_key, entry] : entries_) {
        if (other_key == found->first || !entry.record.owned) continue;
        const auto theirs = features(entry.record);
        if (theirs.empty()) continue;
        float shared = 0, all = 0;
        for (const auto& [feature, weight] : mine) {
            const auto other = theirs.find(feature);
            if (other != theirs.end()) shared += std::min(weight, other->second);
            all += weight;
        }
        for (const auto& [feature, weight] : theirs) if (!mine.contains(feature)) all += weight;
        const float score = all > 0 ? shared / all : 0;
        if (score > 0.05f) result.emplace_back(&entry.record, score);
    }
    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) {
        return a.second != b.second ? a.second > b.second : a.first->key < b.first->key;
    });
    if (result.size() > limit) result.resize(limit);
    return result;
}

std::string Index::save() const {
    auto root = json::object();
    root.properties["format"] = json::string("launchme-games");
    root.properties["version"] = json::number(1);
    auto games = json::object();
    for (const auto& [key, entry] : entries_) {
        auto game = json::object();
        for (size_t i = 0; i < source_count; ++i)
            if (entry.patches[i]) game.properties[source_keys[i]] = encodePatch(*entry.patches[i]);
        if (entry.gone_since) game.properties["gone"] = json::number(static_cast<double>(entry.gone_since));
        games.properties[key] = std::move(game);
    }
    root.properties["games"] = std::move(games);
    return json::serialize(root);
}

std::optional<Index> Index::load(std::string_view text) {
    const auto root = json::parse(text, 256ull * 1024 * 1024);
    if (!root || root->type != json::Value::Type::object) return std::nullopt;
    const auto* format = root->get("format");
    const auto* version = root->get("version");
    const auto* games = root->get("games");
    if (!format || format->stringOr() != "launchme-games" || !version || version->intOr() != 1 || !games || games->type != json::Value::Type::object)
        return std::nullopt;
    Index index;
    for (const auto& [key, game] : games->properties) {
        if (key.empty() || game.type != json::Value::Type::object) continue;
        Entry entry;
        for (size_t i = 0; i < source_count; ++i)
            if (const auto* patch = game.get(source_keys[i]); patch && patch->type == json::Value::Type::object) {
                auto decoded = decodePatch(*patch);
                if (!decoded.empty()) entry.patches[i] = std::move(decoded);
            }
        if (std::none_of(entry.patches.begin(), entry.patches.end(), [](const auto& item) { return item.has_value(); })) continue;
        if (const auto gone = whole(game.get("gone"), 4102444800.0)) entry.gone_since = *gone;
        compose(key, entry);
        index.entries_.emplace(key, std::move(entry));
    }
    index.revision_ = 1;
    return index;
}
}
