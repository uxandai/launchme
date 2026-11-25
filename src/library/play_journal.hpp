#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// What was played on which day, as Launchme saw it (every game in a session it followed, Steam's
// too): minutes per game per local day and how often each was started. The profile's "Ten tydzień"
// and its chart read it; nothing leaves the computer. play-journal.tsv in the config folder:
//   launchme-journal<TAB>1
//   n<TAB><game key><TAB><name>
//   d<TAB><yyyymmdd><TAB><game key><TAB><milliseconds><TAB><launches>
namespace play_journal {
inline constexpr size_t max_days = 400; // Older days fall off.

struct Entry {
    uint64_t ms = 0;
    uint32_t launches = 0;
    bool operator==(const Entry&) const = default;
};
struct Journal {
    std::map<int, std::map<std::string, Entry>> days; // yyyymmdd → game key → its day.
    std::map<std::string, std::wstring> names;        // The game's name when it was last played.
    bool operator==(const Journal&) const = default;
};

// Local calendar day of a Unix time, and day arithmetic over yyyymmdd.
int dateOf(uint64_t unix_seconds);
int addDays(int date, int days);
int weekday(int date); // 0 Monday … 6 Sunday.

void played(Journal& journal, int date, const std::string& key, const std::wstring& name, uint64_t ms);
void launched(Journal& journal, int date, const std::string& key, const std::wstring& name);

std::string format(const Journal& journal);
Journal parse(std::string_view text); // Never throws; bad lines are skipped.
// The file's journal; an empty one when there is no file yet, nullopt when one is there but cannot
// be read (locked, damaged, a newer version): then it must not be saved over unseen.
std::optional<Journal> read(const std::filesystem::path& file);
Journal load(const std::filesystem::path& file); // read(), an empty journal when it fails.
// Adds `from`'s days and names into `into` (what was played while the file could not be read).
void merge(Journal& into, const Journal& from);
bool save(const Journal& journal, const std::filesystem::path& file);

// The seven days ending `today` (oldest first) and the seven before them.
struct Week {
    std::array<int, 7> dates{};
    std::array<uint64_t, 7> day_ms{};
    uint64_t total_ms = 0, previous_ms = 0;
    uint32_t launches = 0;
    size_t games = 0;                                  // Different games played.
    std::vector<std::pair<std::string, uint64_t>> top; // Most played first.
    size_t active_days = 0;                            // Days with any play.
};
Week week(const Journal& journal, int today);
// Days in a row with play, ending today or yesterday (a day not over yet does not break it).
int streak(const Journal& journal, int today);
} // namespace play_journal
