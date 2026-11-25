#pragma once
#include "settings_view.hpp"
#include "../library/smart_filter.hpp"

namespace library_manager_ui {
// rules: a dynamic collection's or Zaawansowany filtr's rules (each a row, Dodaj regułę, …); rule: one
// rule being made or changed (its value in the text field, Pole, Warunek, suggestions, Gotowe);
// rule_field: the list of fields to choose from.
enum class Page { collections, collection, games, membership, name, versions, excluded, rules, rule, rule_field };
// Whose rules the rules page edits: a dynamic collection's (State::collection), Tryb TV's library or the
// desktop window's games (both for this session).
enum class RuleTarget { collection, library, desktop };
struct Row {
    std::wstring title, subtitle;
    std::string key;
    bool toggle = false, checked = false;
    bool radio = false;
};
struct State {
    bool open = false;
    Page page = Page::collections, name_return = Page::collections;
    std::wstring title, input, error;
    std::string collection;
    Game game;
    uint64_t account = 0;
    std::vector<Row> rows;
    int focus = 0, scroll = 0; // -2: text field, -1: back.
    bool selected = false, confirm = false;
    bool steam_collection = true;
    std::vector<Game> versions;
    bool remember = false, launch_version = false;
    // Desktop mode: a new collection made from several games (`game` and these) or a rename; the
    // dialog closes once it is saved.
    std::vector<Game> more_games;
    bool close_after_save = false;
    bool settings_return = false; // Opened from a collection's page in Settings: Back closes the dialog.
    // Kolekcje dynamiczne and Zaawansowany filtr.
    bool smart_collection = false;     // Nowa kolekcja: the third kind (dynamic), beside steam_collection.
    RuleTarget rule_target = RuleTarget::collection;
    smart_filter::Rules rules;         // A new dynamic collection's first rules (Zapisz jako kolekcję dynamiczną).
    smart_filter::Rule draft;          // Page::rule: the rule being made or changed.
    int rule_index = -1;               // Page::rule: which rule (-1: a new one).
    int matching = -1;                 // Page::rules: games the rules match now (-1: not counted).
    bool ruleText() const { return page == Page::rule && !smart_filter::choiceField(draft.field); }
    bool textField() const { return page == Page::name || page == Page::games || page == Page::excluded || ruleText(); }
    const wchar_t* placeholder() const {
        using smart_filter::Field;
        if (page == Page::name) return tr(L"Nazwa");
        if (page != Page::rule) return tr(L"Szukaj");
        switch (draft.field) {
        case Field::title: return tr(L"Słowa z tytułu, np. tekken (przecinek: kilka)");
        case Field::studio: return tr(L"Np. capcom, bandai namco");
        case Field::label: return tr(L"Np. rpg, roguelike, przygodowe");
        case Field::rating: return tr(L"Procent, np. 80");
        case Field::length: return tr(L"Godziny, np. 10");
        default: return tr(L"Rok, np. 2015");
        }
    }
};
struct Layout {
    float width, height;
    int versions_count = -1;
    static Layout of(float width, float height, const State& state) {
        return {width, height, state.page == Page::versions ? static_cast<int>(state.versions.size()) : -1};
    }
    settings_ui::Rect panel() const {
        const float w = std::min(980.0f, width - 80);
        const float wanted = versions_count < 0 ? 820.0f : std::min(820.0f, 216.0f + ((versions_count + 1) / 2) * 146.0f);
        const float h = std::min(wanted, height - 64);
        return {(width - w) / 2, (height - h) / 2, (width + w) / 2, (height + h) / 2};
    }
    settings_ui::Rect back() const { const auto p = panel(); return {p.left + 24, p.top + 24, p.left + 80, p.top + 68}; }
    settings_ui::Rect input() const { const auto p = panel(); return {p.left + 28, p.top + 92, p.right - 28, p.top + 146}; }
    settings_ui::Rect version(int index) const {
        const auto p = panel(); const float w = (p.right - p.left - 76) / 2;
        const float left = p.left + 28 + (index % 2) * (w + 20), top = p.top + 106 + (index / 2) * 146;
        return {left, top, left + w, top + 126};
    }
    settings_ui::Rect remember() const { const auto p = panel(); return {p.left + 28, p.bottom - 84, p.right - 28, p.bottom - 30}; }
    float listTop(const State& s) const { return panel().top + (s.textField() ? 164 : 96); }
    int capacity(const State& s) const { return std::max(1, static_cast<int>((panel().bottom - 64 - listTop(s)) / 66)); }
    settings_ui::Rect row(const State& s, int i) const {
        const auto p = panel(); const float y = listTop(s) + (i - s.scroll) * 66;
        return {p.left + 28, y, p.right - 28, y + 60};
    }
    int hit(const State& s, float x, float y) const {
        if (back().contains(x, y)) return -1;
        if (s.page == Page::versions) {
            if (remember().contains(x, y)) return static_cast<int>(s.versions.size());
            for (int i = s.scroll; i < std::min(static_cast<int>(s.versions.size()), s.scroll + versionCapacity()); ++i)
                if (version(i - s.scroll).contains(x, y)) return i;
            return -3;
        }
        if (s.textField() && input().contains(x, y)) return -2;
        for (int i = s.scroll; i < std::min(static_cast<int>(s.rows.size()), s.scroll + capacity(s)); ++i)
            if (row(s, i).contains(x, y)) return i;
        return -3;
    }
    int versionCapacity() const { return std::max(2, static_cast<int>((panel().bottom - panel().top - 216) / 146) * 2); }
};
}
