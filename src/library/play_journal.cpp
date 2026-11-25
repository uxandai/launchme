#include "play_journal.hpp"
#include "../platform/atomic_file.hpp"
#include "../platform/text_encoding.hpp"

#include <algorithm>
#include <charconv>
#include <ctime>
#include <fstream>
#include <set>

namespace play_journal {
namespace {
// Howard Hinnant's civil-day algorithms: exact for any Gregorian date.
int64_t daysFromCivil(int y, unsigned m, unsigned d) {
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<int64_t>(doe) - 719468;
}
int civilFromDays(int64_t z) {
    z += 719468;
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = static_cast<unsigned>(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const int64_t y = static_cast<int64_t>(yoe) + era * 400;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    const unsigned d = doy - (153 * mp + 2) / 5 + 1;
    const unsigned m = mp + (mp < 10 ? 3 : -9);
    return static_cast<int>((y + (m <= 2)) * 10000 + m * 100 + d);
}
int64_t serial(int date) { return daysFromCivil(date / 10000, static_cast<unsigned>(date / 100 % 100), static_cast<unsigned>(date % 100)); }
bool validDate(int date) {
    const int month = date / 100 % 100, day = date % 100;
    return date >= 19700101 && date <= 99991231 && month >= 1 && month <= 12 && day >= 1 && day <= 31;
}
bool validKey(std::string_view key) {
    return !key.empty() && key.size() <= 200 && std::none_of(key.begin(), key.end(), [](char c) { return c == '\t' || c == '\n' || c == '\r'; });
}
std::wstring cleanName(std::wstring name) {
    for (auto& c : name) if (c == L'\t' || c == L'\n' || c == L'\r') c = L' ';
    if (name.size() > 200) name.resize(200);
    return name;
}
template<class T> bool number(std::string_view text, T& value) {
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    return error == std::errc{} && end == text.data() + text.size();
}
void trim(Journal& journal) {
    while (journal.days.size() > max_days) journal.days.erase(journal.days.begin());
    // Names only of games still in the journal.
    std::set<std::string> used;
    for (const auto& [date, games] : journal.days) for (const auto& [key, entry] : games) used.insert(key);
    std::erase_if(journal.names, [&](const auto& item) { return !used.contains(item.first); });
}
} // namespace

int dateOf(uint64_t unix_seconds) {
    const time_t time = static_cast<time_t>(unix_seconds);
    tm local{};
    if (localtime_s(&local, &time)) return 19700101;
    return (local.tm_year + 1900) * 10000 + (local.tm_mon + 1) * 100 + local.tm_mday;
}
int addDays(int date, int days) { return civilFromDays(serial(date) + days); }
int weekday(int date) { return static_cast<int>(((serial(date) % 7) + 7 + 3) % 7); } // 1970-01-01 was a Thursday.

void played(Journal& journal, int date, const std::string& key, const std::wstring& name, uint64_t ms) {
    if (!validDate(date) || !validKey(key) || !ms) return;
    auto& entry = journal.days[date][key];
    entry.ms += std::min<uint64_t>(ms, 24ull * 3600 * 1000);
    if (!name.empty()) journal.names[key] = cleanName(name);
    trim(journal);
}

void launched(Journal& journal, int date, const std::string& key, const std::wstring& name) {
    if (!validDate(date) || !validKey(key)) return;
    ++journal.days[date][key].launches;
    if (!name.empty()) journal.names[key] = cleanName(name);
    trim(journal);
}

std::string format(const Journal& journal) {
    std::string out = "launchme-journal\t1\n";
    for (const auto& [key, name] : journal.names) out += "n\t" + key + '\t' + text_encoding::utf8Lenient(name) + '\n';
    for (const auto& [date, games] : journal.days)
        for (const auto& [key, entry] : games)
            out += "d\t" + std::to_string(date) + '\t' + key + '\t' + std::to_string(entry.ms) + '\t' + std::to_string(entry.launches) + '\n';
    return out;
}

Journal parse(std::string_view text) {
    Journal journal;
    if (text.starts_with("\xef\xbb\xbf")) text.remove_prefix(3);
    bool header = false;
    while (!text.empty()) {
        const auto end = text.find('\n');
        auto line = text.substr(0, end);
        text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        std::vector<std::string_view> fields;
        for (size_t at = 0; fields.size() < 6;) {
            const auto tab = line.find('\t', at);
            fields.push_back(line.substr(at, tab == std::string_view::npos ? std::string_view::npos : tab - at));
            if (tab == std::string_view::npos) break;
            at = tab + 1;
        }
        if (!header) { header = fields.size() == 2 && fields[0] == "launchme-journal" && fields[1] == "1"; if (!header) return {}; continue; }
        if (fields.size() == 3 && fields[0] == "n" && validKey(fields[1])) {
            journal.names[std::string(fields[1])] = cleanName(text_encoding::wideLenient(fields[2]));
        } else if (fields.size() == 5 && fields[0] == "d" && validKey(fields[2])) {
            int date = 0; uint64_t ms = 0; uint32_t launches = 0;
            if (!number(fields[1], date) || !validDate(date) || !number(fields[3], ms) || !number(fields[4], launches)) continue;
            auto& entry = journal.days[date][std::string(fields[2])];
            entry.ms = std::min<uint64_t>(ms, 24ull * 3600 * 1000);
            entry.launches = std::min<uint32_t>(launches, 10000);
        }
    }
    trim(journal);
    return journal;
}

std::optional<Journal> read(const std::filesystem::path& file) {
    std::error_code ec;
    const bool exists = std::filesystem::exists(file, ec);
    if (ec) return std::nullopt;
    if (!exists) return Journal{}; // None yet.
    std::ifstream input(file, std::ios::binary | std::ios::ate);
    if (!input || input.tellg() < 0 || input.tellg() > 16 * 1024 * 1024) return std::nullopt;
    if (input.tellg() == 0) return Journal{};
    std::string text(static_cast<size_t>(input.tellg()), '\0');
    input.seekg(0);
    if (!input.read(text.data(), static_cast<std::streamsize>(text.size()))) return std::nullopt;
    // Without its header line the file is damaged or of a newer version: not an empty journal.
    std::string_view body(text);
    if (body.starts_with("\xef\xbb\xbf")) body.remove_prefix(3);
    if (!body.starts_with("launchme-journal\t1\n") && !body.starts_with("launchme-journal\t1\r\n") && body != "launchme-journal\t1") return std::nullopt;
    return parse(text);
}

Journal load(const std::filesystem::path& file) { return read(file).value_or(Journal{}); }

void merge(Journal& into, const Journal& from) {
    for (const auto& [date, games] : from.days)
        for (const auto& [key, entry] : games) {
            auto& sum = into.days[date][key];
            sum.ms = std::min<uint64_t>(sum.ms + entry.ms, 24ull * 3600 * 1000);
            sum.launches = std::min<uint32_t>(sum.launches + entry.launches, 10000);
        }
    for (const auto& [key, name] : from.names) into.names[key] = name;
    trim(into);
}

bool save(const Journal& journal, const std::filesystem::path& file) {
    std::error_code error;
    std::filesystem::create_directories(file.parent_path(), error);
    return atomic_file::writeDurable(file, format(journal)); // The player's history: flushed before the rename.
}

Week week(const Journal& journal, int today) {
    Week result;
    std::map<std::string, uint64_t> totals;
    for (int i = 0; i < 7; ++i) {
        const int date = addDays(today, i - 6);
        result.dates[static_cast<size_t>(i)] = date;
        const auto found = journal.days.find(date);
        if (found == journal.days.end()) continue;
        uint64_t day = 0;
        for (const auto& [key, entry] : found->second) {
            day += entry.ms;
            result.launches += entry.launches;
            if (entry.ms || entry.launches) totals[key] += entry.ms;
        }
        result.day_ms[static_cast<size_t>(i)] = day;
        result.total_ms += day;
        if (day) ++result.active_days;
    }
    for (int i = 7; i < 14; ++i)
        if (const auto found = journal.days.find(addDays(today, -i)); found != journal.days.end())
            for (const auto& [key, entry] : found->second) result.previous_ms += entry.ms;
    result.games = totals.size();
    result.top.assign(totals.begin(), totals.end());
    std::stable_sort(result.top.begin(), result.top.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
    return result;
}

int streak(const Journal& journal, int today) {
    const auto active = [&](int date) {
        const auto found = journal.days.find(date);
        return found != journal.days.end() && std::any_of(found->second.begin(), found->second.end(), [](const auto& item) { return item.second.ms >= 60000; });
    };
    int date = active(today) ? today : addDays(today, -1);
    int count = 0;
    while (active(date) && count < 10000) { ++count; date = addDays(date, -1); }
    return count;
}
} // namespace play_journal
