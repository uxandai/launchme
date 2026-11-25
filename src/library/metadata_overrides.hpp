#pragma once

// The player's own metadata for a game (desktop mode's "Edytuj metadane…"): wherever a field is set it
// wins over what the stores, RAWG, ScreenScraper or the game index found, e.g. a PC port matched as
// the console game it was made from. Kept by game key in metadata_overrides.json.
#include "../model/game_details.hpp"

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace metadata {

struct Override {
    std::wstring title, description, developer, publisher, release_date, genres; // Empty: automatic.
    std::wstring hours; // How long the main story takes (as HowLongToBeat), as typed: "22", "20-24", "7,5".
    // "Czas gry" the player set (minutes): time from another PC, a store that keeps it to itself, a
    // reset. Counted on from there: what is played later (measured here or by the store) is added.
    std::optional<uint32_t> playtime;
    uint32_t playtime_base = 0; // The automatic play time (minutes) when it was set.
    bool looked_up = false; // Its title's lookups were made afresh (HowLongToBeat, RAWG under the new name).
    bool empty() const {
        return title.empty() && description.empty() && developer.empty() && publisher.empty() && release_date.empty() && genres.empty() && hours.empty() &&
               !playtime;
    }
};

// The play time shown: the player's own value plus what was played since it was set.
uint32_t playtime(const Override& value, uint32_t automatic);
// "12", "12,5", "12:30", "12 h 30 min", "45 min" → minutes; none without a number or above 100000 h.
std::optional<uint32_t> parsePlaytime(std::wstring_view text);
std::wstring playtimeText(uint32_t minutes); // 750 → "12:30".

class Overrides {
public:
    // false: the file is there but could not be read (locked, damaged). Its entries are then not
    // here, and save() reads it again first (or keeps it as *.unreadable.json) before replacing it.
    bool load(const std::filesystem::path& file);
    bool save(const std::filesystem::path& file);
    const Override* find(const std::string& key) const;
    void set(const std::string& key, Override value); // An empty one is forgotten.
    bool erase(const std::string& key);
    const std::map<std::string, Override>& items() const { return items_; }

private:
    std::map<std::string, Override> items_;
    bool readable_ = true;
};

// The set fields over the found details.
void apply(const Override& value, GameDetails& details);

} // namespace metadata
