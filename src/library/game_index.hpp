#pragma once
// The launcher's local knowledge of the player's games: one record per library game, composed
// from what the launcher already learns about it (Steam's offline appinfo cache, the store pages
// it opens, RAWG, review scores, HowLongToBeat, ES-DE/ScreenScraper, GOG/Epic product data and
// the library itself). Every source keeps its own patch; a record takes each field from the
// most trusted source that has it. Queries filter and sort structured fields: no vector search.
// Pure and UI-free: src/app/game_index.cpp feeds it on a worker; the companion's tools
// (src/net/companion_tools.hpp), Szczegóły and the library read it.
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace game_index {
// A date with the precision its source gave: 0 month/day = unknown.
struct Date {
    int year = 0, month = 0, day = 0;
    bool known() const { return year > 0; }
    int sortKey() const { return year * 10000 + month * 100 + day; }
    std::wstring iso() const; // "1998-11-08", "1998-11" or "1998"; empty when unknown.
    bool operator==(const Date&) const = default;
};
// ISO "2004-11-16", ES-DE "20041116T000000", Steam "16 Nov, 2004" / "Nov 16, 2004" /
// "16 lis 2004" / "16 listopada 2004", "Nov 2004", "Q4 2004", "2004". Unknown otherwise
// ("Coming soon", "TBA").
Date parseDate(std::wstring_view text);
Date dateFromUnix(uint64_t seconds);
// For people: "1 listopada 1998" / "Nov 1, 1998", "listopad 1998" / "Nov 1998", "1998".
std::wstring dateLabel(const Date& date, bool english);
// A source's release date as shown: a full day ("2023-09-17", "12 Nov, 2020") in one format,
// dateLabel's; anything less precise or not a date ("Q4 2024", "Wkrótce") as written.
std::wstring releaseLabel(std::wstring_view text, bool english);

// Sources in no particular order; priorities are per field (game_index.cpp).
// achievements: counts from an achievements plugin (games outside Steam, src/app/plugin_achievements.cpp).
enum class Source : uint8_t { library, steam_appinfo, steam_store, rawg, reviews, hltb, emulation, gog, epic, steam_stats, achievements };
inline constexpr size_t source_count = 11;
const char* sourceKey(Source source);
std::optional<Source> sourceFromKey(std::string_view key);

// What one source says about one game. Only the fields it knows are set.
struct Patch {
    std::optional<std::wstring> title, store, platform, franchise, description;
    std::optional<std::vector<std::wstring>> aliases, developers, publishers, genres, tags;
    std::optional<Date> release;
    std::optional<int> review_percent, metacritic; // Steam's share of positive reviews; Metacritic 0–100.
    std::optional<int> review_score;               // Steam's label 1 (Overwhelmingly Negative) … 9 (Overwhelmingly Positive).
    std::optional<uint32_t> review_count;
    std::optional<float> rawg_rating;              // RAWG's 0–5 scale.
    std::optional<uint32_t> hltb_main, hltb_extra, hltb_complete; // Minutes.
    std::optional<uint32_t> playtime_minutes, achievements_unlocked, achievements_total;
    std::optional<bool> installed;
    std::optional<uint64_t> first_seen, last_played; // Unix seconds.
    uint64_t updated = 0; // When the source described it (Unix seconds); 0 unknown.
    bool empty() const;
    bool operator==(const Patch&) const = default;
};

// One game as composed from its patches.
struct Record {
    std::string key; // gameKey()
    std::wstring title, store, platform, franchise, description;
    std::vector<std::wstring> aliases, developers, publishers, genres, tags;
    Date release;
    std::optional<int> review_percent, metacritic, review_score;
    std::optional<uint32_t> review_count;
    std::optional<float> rawg_rating;
    std::optional<uint32_t> hltb_main, hltb_extra, hltb_complete;
    std::optional<uint32_t> playtime_minutes;
    uint32_t achievements_unlocked = 0, achievements_total = 0;
    bool installed = false, owned = true; // owned: still in the library.
    uint64_t first_seen = 0, last_played = 0;
    uint32_t sources = 0; // A bit per Source that described it.
    bool from(Source source) const { return (sources >> static_cast<int>(source)) & 1u; }
    bool played() const { return last_played || (playtime_minutes && *playtime_minutes); }
};

enum class Sort { relevance, title, release_asc, release_desc, last_played, playtime, rating, metacritic, shortest, longest, added, random };
// How well reviewed a game is, 0–100, with the number of votes counted in (a lower confidence
// bound: 100% of 5 reviews ranks below 97% of 50 000). -1 when no source rated it.
float ratingScore(const Record& record);
enum class Status { any, installed, not_installed, never_played, played, recent, completed };
std::optional<Sort> sortFromKey(std::string_view key);
std::optional<Status> statusFromKey(std::string_view key);

struct Query {
    std::wstring text;                       // Words of the title, an alias, studio or series.
    std::vector<std::wstring> genres, tags;  // Each must match a genre or tag (English or Polish names).
    std::wstring developer;                  // Developer or publisher.
    std::wstring store;                      // "Steam", "GOG", a console…
    int year_from = 0, year_to = 0;          // Release years, inclusive; 0 open.
    Status status = Status::any;
    Sort sort = Sort::relevance;
    size_t limit = 10, offset = 0;
    uint32_t seed = 0;                       // Sort::random.
    uint64_t now = 0;                        // Status::recent (last 30 days).
};
struct Found {
    std::vector<const Record*> games;
    size_t total = 0;   // Every match, before offset/limit.
    size_t undated = 0; // Matches without a release date (last in date sorts).
    size_t unrated = 0, untimed = 0; // Likewise for rating/metacritic and HowLongToBeat sorts.
};

enum class Group { genre, tag, developer, publisher, franchise, year, decade, store, status };
std::optional<Group> groupFromKey(std::string_view key);
struct Bucket { std::wstring name; size_t games = 0, played = 0; uint64_t minutes = 0; };
struct Stats {
    size_t total = 0, installed = 0, played = 0, never_played = 0, dated = 0, completed = 0;
    uint64_t minutes = 0;
    std::vector<Bucket> buckets; // Largest first.
};

// Folded for matching: lower case, letters and digits only ("Half-Life 2" → "halflife2").
std::wstring fold(std::wstring_view text);
// The English name of a Polish genre/tag word ("przygodowa" → "adventure"); the input when unknown.
std::wstring canonicalGenre(std::wstring_view name);
// A genre's name for the UI ("Adventure" → "Przygodowe" in Polish).
std::wstring genreLabel(std::wstring_view name, bool english);

class Index {
public:
    // Replaces what `source` says about the game (an empty patch removes it). False: no change.
    bool merge(const std::string& key, Source source, Patch patch);
    // The library: each game's library patch. Games missing from it are no longer owned (and
    // leave the index after `keep_days`); their other patches stay until then.
    void syncLibrary(const std::vector<std::pair<std::string, Patch>>& games, uint64_t now, uint64_t keep_days = 60);

    const Record* get(std::string_view key) const;
    const Patch* patch(std::string_view key, Source source) const;
    // Owned games by title: exact (any case), folded, then folded prefix/substring; best first.
    std::vector<const Record*> lookup(std::wstring_view title, size_t limit = 5) const;
    Found search(const Query& query) const;
    Stats stats(Group by, size_t limit, const Query* filter = nullptr) const;
    // Owned games most like `key`: weighted Jaccard over genres, tags, studios and series.
    std::vector<std::pair<const Record*, float>> similar(std::string_view key, size_t limit) const;
    std::vector<std::string> keys() const; // Owned games.
    // Owned games with a genre or tag that starts with the typed word ("rpg", "strateg",
    // "przygodowe"); at least three letters. The library's search field uses it.
    std::vector<std::string> withLabel(std::wstring_view word) const;
    // Every owned game's release date as yyyymmdd (Date::sortKey) by key; undated ones left out.
    std::unordered_map<std::string, int> releases() const;
    size_t size() const { return entries_.size(); }
    uint64_t revision() const { return revision_; } // Grows with every change.

    std::string save() const; // JSON, games-v1.
    static std::optional<Index> load(std::string_view json);

private:
    struct Entry {
        std::array<std::optional<Patch>, source_count> patches;
        Record record;
        std::wstring folded_title;
        std::vector<std::wstring> folded_names;  // Aliases, studios, series.
        std::vector<std::wstring> folded_labels; // Genres and tags.
        uint64_t gone_since = 0;                 // Not in the library since then.
    };
    static void compose(const std::string& key, Entry& entry);
    bool matches(const Entry& entry, const Query& query, const std::vector<std::wstring>& words) const;
    std::unordered_map<std::string, Entry> entries_;
    uint64_t revision_ = 0;
};
}
