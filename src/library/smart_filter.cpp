#include "smart_filter.hpp"
#include "game_index.hpp"
#include "view_options.hpp"
#include "../i18n/i18n.hpp"
#include "../model/game.hpp"
#include "../platform/text_encoding.hpp"

#include <algorithm>
#include <charconv>

namespace smart_filter {
namespace {
constexpr char field_letters[field_count] = {'t', 's', 'g', 'p', 'm', 'i', 'r', 'l', 'y'};
constexpr char rule_separator = '\x1e';

// The value's alternatives, folded ("Tag Tournament, 3" → "tagtournament", "3"); empty ones dropped.
std::vector<std::wstring> alternatives(std::wstring_view value) {
    std::vector<std::wstring> result;
    while (true) {
        const auto comma = value.find(L',');
        auto folded = game_index::fold(value.substr(0, comma));
        if (!folded.empty()) result.push_back(std::move(folded));
        if (comma == std::wstring_view::npos) break;
        value.remove_prefix(comma + 1);
    }
    return result;
}
std::optional<int> number(std::wstring_view value) {
    while (!value.empty() && value.front() == L' ') value.remove_prefix(1);
    while (!value.empty() && (value.back() == L' ' || value.back() == L'%' || value.back() == L'h')) value.remove_suffix(1);
    if (value.empty() || value.size() > 6) return std::nullopt;
    int result = 0;
    for (const wchar_t c : value) {
        if (c < L'0' || c > L'9') return std::nullopt;
        result = result * 10 + (c - L'0');
    }
    return result;
}
std::pair<int, int> range(Field field) {
    return field == Field::rating ? std::pair{1, 100} : field == Field::length ? std::pair{1, 1000} : std::pair{1950, 2100};
}
bool anyContains(const std::vector<std::wstring>& haystack, const std::wstring& needle) {
    return std::any_of(haystack.begin(), haystack.end(), [&](const std::wstring& item) { return item.find(needle) != std::wstring::npos; });
}
// Whether the game has what the rule names (before `exclude` turns it over).
bool has(const Rule& rule, const Facts& facts) {
    switch (rule.field) {
    case Field::title: {
        const auto title = game_index::fold(facts.title);
        for (const auto& word : alternatives(rule.value)) if (title.find(word) != std::wstring::npos) return true;
        return false;
    }
    case Field::studio: case Field::label: {
        std::vector<std::wstring> folded;
        for (const auto& item : rule.field == Field::studio ? facts.studios : facts.labels) folded.push_back(game_index::fold(item));
        for (const auto& word : alternatives(rule.value)) {
            if (anyContains(folded, word)) return true;
            // A Polish genre ("przygodowe", "strategia") as the index keeps it, in English.
            if (rule.field == Field::label)
                if (const auto english = game_index::fold(game_index::canonicalGenre(word)); english != word && anyContains(folded, english)) return true;
        }
        return false;
    }
    case Field::store: return text_encoding::wide(facts.store) == rule.value;
    case Field::mode: {
        for (int bit = 0; bit <= static_cast<int>(library_view::Filter::vr); ++bit)
            if (rule.value == choices(Field::mode)[static_cast<size_t>(bit)].key) return (facts.modes & (1u << bit)) != 0;
        return false;
    }
    case Field::status:
        return rule.value == L"installed" ? facts.installed : rule.value == L"played" ? facts.played :
               rule.value == L"game_pass" ? facts.game_pass : rule.value == L"favorite" && facts.favorite;
    default: return false;
    }
}
}

bool textField(Field field) { return field == Field::title || field == Field::studio || field == Field::label; }
bool choiceField(Field field) { return field == Field::store || field == Field::mode || field == Field::status; }
bool numberField(Field field) { return field == Field::rating || field == Field::length || field == Field::year; }

std::vector<Choice> choices(Field field) {
    std::vector<Choice> result;
    switch (field) {
    case Field::store:
        for (const auto source : {GameSource::steam, GameSource::gog, GameSource::epic, GameSource::xbox, GameSource::ea, GameSource::ubisoft,
                                  GameSource::battlenet, GameSource::amazon, GameSource::drmfree, GameSource::custom, GameSource::multimc})
            result.push_back({text_encoding::wide(sourceSlug(source)), sourceName(source)});
        break;
    case Field::mode: {
        static constexpr const wchar_t* keys[] = {L"single_player", L"multiplayer", L"coop", L"local", L"online", L"controller", L"vr"};
        for (int bit = 0; bit <= static_cast<int>(library_view::Filter::vr); ++bit)
            result.push_back({keys[bit], library_view::label(static_cast<library_view::Filter>(bit))});
        break;
    }
    case Field::status:
        result = {{L"installed", tr(L"Zainstalowana")}, {L"played", tr(L"Grana")}, {L"favorite", tr(L"Ulubiona")}, {L"game_pass", L"Game Pass"}};
        break;
    case Field::rating: for (const int n : {50, 60, 70, 75, 80, 85, 90, 95}) result.push_back({std::to_wstring(n), std::to_wstring(n) + L"%"}); break;
    case Field::length:
        for (const int n : {1, 2, 3, 5, 8, 10, 15, 20, 30, 50, 100}) result.push_back({std::to_wstring(n), std::to_wstring(n) + L' ' + tr(L"godz.")});
        break;
    case Field::year: for (const int n : {1990, 2000, 2005, 2010, 2015, 2018, 2020, 2022, 2024, 2025}) result.push_back({std::to_wstring(n), std::to_wstring(n)}); break;
    default: break;
    }
    return result;
}

const wchar_t* fieldLabel(Field field) {
    switch (field) {
    case Field::title: return tr(L"Tytuł");
    case Field::studio: return tr(L"Twórca lub wydawca");
    case Field::label: return tr(L"Gatunek lub tag");
    case Field::store: return tr(L"Sklep");
    case Field::mode: return tr(L"Tryb gry");
    case Field::status: return tr(L"Stan");
    case Field::rating: return tr(L"Ocena");
    case Field::length: return tr(L"Długość");
    default: return tr(L"Rok wydania");
    }
}

const wchar_t* operatorLabel(Field field, bool exclude) {
    if (textField(field)) return exclude ? tr(L"nie zawiera") : tr(L"zawiera");
    if (choiceField(field)) return exclude ? tr(L"nie jest") : tr(L"jest");
    if (field == Field::rating) return exclude ? tr(L"poniżej") : tr(L"co najmniej");
    if (field == Field::length) return exclude ? tr(L"ponad") : tr(L"do");
    return exclude ? tr(L"do") : tr(L"od");
}

std::wstring describe(const Rule& rule) {
    std::wstring text = std::wstring(fieldLabel(rule.field)) + L' ' + operatorLabel(rule.field, rule.exclude);
    if (textField(rule.field)) return text + L" „" + rule.value + L"”";
    if (choiceField(rule.field)) {
        for (const auto& choice : choices(rule.field)) if (choice.key == rule.value) return text + L": " + choice.label;
        return text + L": " + rule.value;
    }
    if (rule.field == Field::rating) return text + L' ' + rule.value + L'%';
    if (rule.field == Field::length) return text + L' ' + rule.value + L' ' + tr(L"godz.");
    return text + L' ' + rule.value;
}

bool valid(const Rule& rule) {
    if (static_cast<int>(rule.field) < 0 || static_cast<int>(rule.field) >= field_count || rule.value.size() > 200) return false;
    if (textField(rule.field)) return !alternatives(rule.value).empty();
    if (choiceField(rule.field)) {
        const auto options = choices(rule.field);
        return std::any_of(options.begin(), options.end(), [&](const Choice& choice) { return choice.key == rule.value; });
    }
    const auto value = number(rule.value);
    const auto [low, high] = range(rule.field);
    return value && *value >= low && *value <= high;
}

bool matches(const Rule& rule, const Facts& facts) {
    if (!valid(rule)) return true; // An unfinished rule does not hide games.
    if (numberField(rule.field)) {
        const int value = *number(rule.value);
        // A game the index knows no number for matches neither side.
        if (rule.field == Field::rating) return facts.rating && (rule.exclude ? *facts.rating < value : *facts.rating >= value);
        if (rule.field == Field::length) return facts.minutes && *facts.minutes && (rule.exclude ? *facts.minutes > static_cast<uint32_t>(value) * 60 : *facts.minutes <= static_cast<uint32_t>(value) * 60);
        return facts.year > 0 && (rule.exclude ? facts.year <= value : facts.year >= value);
    }
    return has(rule, facts) != rule.exclude;
}

bool matches(const Rules& rules, const Facts& facts) {
    return std::all_of(rules.begin(), rules.end(), [&](const Rule& rule) { return matches(rule, facts); });
}

std::string encode(const Rules& rules) {
    std::string out;
    for (size_t i = 0; i < std::min(rules.size(), max_rules); ++i) {
        const auto& rule = rules[i];
        if (i) out += rule_separator;
        out += field_letters[static_cast<int>(rule.field)];
        out += rule.exclude ? '!' : '=';
        std::wstring value = rule.value;
        std::erase_if(value, [](wchar_t c) { return c < L' '; });
        out += text_encoding::utf8(value);
    }
    return out;
}

std::optional<Rules> decode(std::string_view text) {
    Rules rules;
    if (text.empty()) return rules;
    while (true) {
        const auto end = text.find(rule_separator);
        const auto part = text.substr(0, end);
        if (part.size() < 2 || (part[1] != '=' && part[1] != '!') || rules.size() >= max_rules) return std::nullopt;
        const auto letter = std::find(std::begin(field_letters), std::end(field_letters), part[0]);
        if (letter == std::end(field_letters)) return std::nullopt;
        Rule rule{static_cast<Field>(letter - std::begin(field_letters)), part[1] == '!', text_encoding::wide(part.substr(2))};
        if (part.size() > 2 && rule.value.empty()) return std::nullopt; // Not UTF-8.
        rules.push_back(std::move(rule));
        if (end == std::string_view::npos) break;
        text.remove_prefix(end + 1);
    }
    return rules;
}
}
