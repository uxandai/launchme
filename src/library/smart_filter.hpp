#pragma once
// Zaawansowany filtr and Kolekcje dynamiczne: rules clicked together, every one of which a game must
// match ("Tytuł zawiera tekken" + "Tytuł nie zawiera tag tournament"; "Twórca lub wydawca zawiera
// capcom" + "Tryb gry jest Kooperacja"). Pure: the launcher gathers each game's Facts (its title and
// store from the library, studios, genres, tags, rating, length and year from the game index).
// A text value may list alternatives with commas: "zawiera" any of them, "nie zawiera" none.
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace smart_filter {
// The order is the order of the rule editor's list (and the saved letters in encode()).
enum class Field : uint8_t { title, studio, label, store, mode, status, rating, length, year };
inline constexpr int field_count = 9;
// What `exclude` turns each kind of rule into: zawiera / nie zawiera (text), jest / nie jest (a choice),
// co najmniej / poniżej (rating), do / ponad (length), od / do (year).
struct Rule {
    Field field = Field::title;
    bool exclude = false;
    std::wstring value; // Text; a choice's key (store slug, mode, status); a number for rating, length, year.
    bool operator==(const Rule&) const = default;
};
using Rules = std::vector<Rule>;
inline constexpr size_t max_rules = 16;

// Everything a rule can ask about one game.
struct Facts {
    std::wstring title;
    std::vector<std::wstring> studios; // Developers and publishers.
    std::vector<std::wstring> labels;  // Genres and tags.
    std::string store;                 // sourceSlug().
    uint32_t modes = 0;                // library_view::filterBit() of the play modes its tags name.
    bool installed = false, played = false, game_pass = false, favorite = false;
    std::optional<float> rating;       // 0–100 (game_index::ratingScore).
    std::optional<uint32_t> minutes;   // HowLongToBeat's main story.
    int year = 0;                      // Release year; 0 unknown.
};

bool textField(Field field);   // title, studio, label: typed, with suggestions.
bool choiceField(Field field); // store, mode, status: one of choices().
bool numberField(Field field); // rating, length, year.
struct Choice { std::wstring key, label; };
// The fixed choices of a choice field, and the usual steps of a number field (any number can be typed).
std::vector<Choice> choices(Field field);
const wchar_t* fieldLabel(Field field);
const wchar_t* operatorLabel(Field field, bool exclude);
// "Tytuł zawiera „tekken”", "Tryb gry jest: Kooperacja", "Ocena co najmniej 80%".
std::wstring describe(const Rule& rule);
// A rule that can be matched: a non-empty value; a number field's value a number in its range.
bool valid(const Rule& rule);

bool matches(const Rule& rule, const Facts& facts);
bool matches(const Rules& rules, const Facts& facts); // Every rule; no rules match everything.

// One line for library-state.ini; decode() refuses anything it did not write.
std::string encode(const Rules& rules);
std::optional<Rules> decode(std::string_view text);
}
