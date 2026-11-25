// Kolekcje dynamiczne and Zaawansowany filtr: rules clicked together (library/smart_filter.hpp) in the
// collection dialog's rule pages, matched against each game's facts from the library and the game index.
#include "launcher.hpp"
#include "../platform/text_encoding.hpp"

#include <algorithm>

using library_manager_ui::Page;
using smart_filter::Field;

namespace {
bool rulePage(Page page) { return page == Page::rules || page == Page::rule || page == Page::rule_field; }
std::wstring trimmedValue(const std::wstring& text) {
    const auto first = text.find_first_not_of(L' ');
    return first == std::wstring::npos ? std::wstring{} : text.substr(first, text.find_last_not_of(L' ') - first + 1);
}
}

smart_filter::Facts Launcher::smartFacts(const Game& game, const game_index::Record* record) const {
    smart_filter::Facts facts;
    facts.title = game.name;
    facts.store = sourceSlug(game.source);
    facts.installed = game.installed;
    facts.game_pass = game.xbox_game_pass;
    facts.favorite = isFavorite(game);
    facts.played = game.last_played || game.store_playtime_minutes || localPlaytime(game) || (record && record->played());
    if (record) {
        facts.studios = record->developers;
        facts.studios.insert(facts.studios.end(), record->publishers.begin(), record->publishers.end());
        facts.labels = record->genres;
        facts.labels.insert(facts.labels.end(), record->tags.begin(), record->tags.end());
        for (int bit = 0; bit <= static_cast<int>(library_view::Filter::vr); ++bit)
            if (library_view::tagged(static_cast<library_view::Filter>(bit), record->tags)) facts.modes |= 1u << bit;
        if (const float score = game_index::ratingScore(*record); score >= 0) facts.rating = score;
        facts.minutes = record->hltb_main;
        facts.year = record->release.year;
    }
    return facts;
}

std::unordered_set<std::string> Launcher::smartMatches(const smart_filter::Rules& rules, const std::vector<Game>& games) const {
    std::unordered_set<std::string> keys;
    const auto check = [&](const game_index::Index* index) {
        for (const auto& game : games) {
            if (!isGame(game) || game.kind == GameKind::catalog) continue;
            auto key = gameKey(game);
            if (smart_filter::matches(rules, smartFacts(game, index ? index->get(key) : nullptr))) keys.insert(std::move(key));
        }
    };
    if (game_index_) game_index_->read([&](const game_index::Index& index) { check(&index); });
    else check(nullptr);
    return keys;
}

void Launcher::refreshSmartMembers() {
    smart_members_.clear();
    for (const auto& [id, encoded] : library_state_.collections.rules)
        if (const auto rules = smart_filter::decode(encoded)) smart_members_[id] = smartMatches(*rules, public_library_);
}

void Launcher::applySmartRules(std::vector<Game>& games, const smart_filter::Rules& rules) const {
    if (rules.empty()) return;
    const auto keep = smartMatches(rules, games);
    std::erase_if(games, [&](const Game& game) { return isGame(game) && game.kind != GameKind::catalog && !keep.contains(gameKey(game)); });
}

void Launcher::openRuleEditor(RuleTarget target, std::string collection) {
    if (target == RuleTarget::collection && !library_state_readable_) {
        showNotification(NotificationKind::error, tr(L"Nie można odczytać historii biblioteki"), L""); return;
    }
    library_dialog_ = {}; library_dialog_.open = true; library_dialog_.account = account_;
    library_dialog_.page = Page::rules; library_dialog_.rule_target = target; library_dialog_.collection = std::move(collection);
    dismissMenu(); closeFriends(); closeQuick(); closeCategoryMenu();
    rebuildCollectionDialog(); audio_.drawer();
}

smart_filter::Rules Launcher::editedRules() const {
    const auto& ui = library_dialog_;
    if (ui.rule_target == RuleTarget::library) return library_rules_;
    if (ui.rule_target == RuleTarget::desktop) return desktop_rules_;
    const auto found = library_state_.collections.rules.find(ui.collection);
    if (found == library_state_.collections.rules.end()) return {};
    return smart_filter::decode(found->second).value_or(smart_filter::Rules{});
}

void Launcher::storeRules(const smart_filter::Rules& rules) {
    auto& ui = library_dialog_;
    if (ui.rule_target == RuleTarget::library) {
        library_rules_ = rules;
        library_controls_.advanced_rules = static_cast<int>(rules.size());
        if (libraryControls()) applyLibraryOptions(); else rebuildCards();
    } else if (ui.rule_target == RuleTarget::desktop) {
        desktop_rules_ = rules;
        if (desktop_mode_) rebuildDesktop(true);
    } else {
        if (!library_state_readable_ || !ui.collection.starts_with("local:")) return;
        library_state_.collections.rules[ui.collection] = smart_filter::encode(rules);
        collectionChanged(); // Saves, rebuilds the shelves (and so the members) and this dialog.
        return;
    }
    rebuildCollectionDialog();
}

void Launcher::rebuildRulePages() {
    auto& ui = library_dialog_;
    const auto add = [&](std::wstring title, std::string key, std::wstring subtitle = {}, bool toggle = false, bool checked = false, bool radio = false) {
        ui.rows.push_back({std::move(title), std::move(subtitle), std::move(key), toggle, checked, radio});
    };
    if (ui.page == Page::rules) {
        const auto rules = editedRules();
        ui.matching = static_cast<int>(smartMatches(rules, public_library_).size());
        std::wstring name = tr(L"Zaawansowany filtr");
        if (ui.rule_target == RuleTarget::collection)
            for (const auto& c : settings_collections_) if (c.id == ui.collection) name = c.name;
        ui.title = name + L"  ·  " + tr(L"pasuje") + L' ' + std::to_wstring(ui.matching);
        for (size_t i = 0; i < rules.size(); ++i) add(smart_filter::describe(rules[i]), "@rule:" + std::to_string(i), tr(L"Zmień albo usuń"));
        if (rules.size() < smart_filter::max_rules)
            add(tr(L"Dodaj regułę"), "@add", tr(L"Tytuł, twórca, gatunek, tryb gry, sklep, ocena, długość, rok"));
        if (ui.rule_target != RuleTarget::collection && !rules.empty()) {
            add(tr(L"Zapisz jako kolekcję dynamiczną"), "@save-smart", tr(L"Gry dobierane tymi regułami, także te dodane później"));
            add(tr(L"Wyczyść filtr"), "@clear");
        }
        add(tr(L"Gotowe"), "@close");
    } else if (ui.page == Page::rule_field) {
        ui.title = tr(L"Pole");
        for (int f = 0; f < smart_filter::field_count; ++f)
            add(smart_filter::fieldLabel(static_cast<Field>(f)), "@f:" + std::to_string(f), {}, true, static_cast<int>(ui.draft.field) == f, true);
    } else if (ui.page == Page::rule) {
        const auto field = ui.draft.field;
        ui.title = ui.rule_index < 0 ? tr(L"Nowa reguła") : tr(L"Reguła");
        add(std::wstring(tr(L"Pole")) + L": " + smart_filter::fieldLabel(field), "@field", tr(L"Zmień"));
        add(std::wstring(tr(L"Warunek")) + L": " + smart_filter::operatorLabel(field, ui.draft.exclude), "@op", tr(L"Zmień"));
        if (smart_filter::choiceField(field) || smart_filter::numberField(field)) {
            const auto current = smart_filter::choiceField(field) ? ui.draft.value : trimmedValue(ui.input);
            for (const auto& choice : smart_filter::choices(field))
                add(choice.label, "=" + text_encoding::utf8(choice.key), {}, true, choice.key == current, true);
        } else if ((field == Field::studio || field == Field::label) && game_index_) {
            // Names the library knows, the most games first; the typed text narrows them.
            const auto typed = game_index::fold(ui.input);
            const bool english = i18n::english();
            struct Suggestion { std::wstring value, label; size_t games; };
            const auto suggestions = game_index_->read([&](const game_index::Index& index) {
                std::vector<Suggestion> found;
                const auto groups = field == Field::studio ? std::vector{game_index::Group::developer, game_index::Group::publisher}
                                                           : std::vector{game_index::Group::genre, game_index::Group::tag};
                for (const auto group : groups)
                    for (const auto& bucket : index.stats(group, 3000).buckets) {
                        const auto label = group == game_index::Group::genre ? game_index::genreLabel(bucket.name, english) : bucket.name;
                        if (!typed.empty() && game_index::fold(label).find(typed) == std::wstring::npos &&
                            game_index::fold(bucket.name).find(typed) == std::wstring::npos) continue;
                        const auto same = std::find_if(found.begin(), found.end(), [&](const Suggestion& s) { return game_index::fold(s.label) == game_index::fold(label); });
                        if (same != found.end()) same->games = std::max(same->games, bucket.games);
                        else found.push_back({bucket.name, label, bucket.games});
                    }
                std::stable_sort(found.begin(), found.end(), [](const Suggestion& a, const Suggestion& b) { return a.games > b.games; });
                if (found.size() > 8) found.resize(8);
                return found;
            });
            for (const auto& s : suggestions)
                add(s.label, "=" + text_encoding::utf8(s.value), std::to_wstring(s.games) + L' ' + tr(L"gier w bibliotece"));
        }
        add(ui.rule_index < 0 ? tr(L"Dodaj regułę") : tr(L"Zapisz regułę"), "@done");
        if (ui.rule_index >= 0) add(tr(L"Usuń regułę"), "@remove");
    }
}

void Launcher::ruleDialogAccept(const library_manager_ui::Row& row) {
    auto& ui = library_dialog_;
    ui.error.clear();
    const auto openRule = [&](int index, smart_filter::Rule rule) {
        ui.rule_index = index; ui.draft = std::move(rule); ui.page = Page::rule; ui.scroll = 0;
        ui.input = smart_filter::choiceField(ui.draft.field) ? std::wstring{} : ui.draft.value;
        ui.focus = ui.ruleText() && index < 0 ? -2 : 0;
    };
    if (ui.page == Page::rules) {
        auto rules = editedRules();
        if (row.key.starts_with("@rule:")) {
            const size_t index = std::stoul(row.key.substr(6));
            if (index < rules.size()) openRule(static_cast<int>(index), rules[index]);
        } else if (row.key == "@add") openRule(-1, {});
        else if (row.key == "@clear") { storeRules({}); return; }
        else if (row.key == "@close") { ui = {}; saveLibraryPreferences(); mouse_transition_until_ = GetTickCount64() + GetDoubleClickTime(); InvalidateRect(hwnd_, nullptr, FALSE); return; }
        else if (row.key == "@save-smart") {
            // A new dynamic collection with these rules: its name first (Nowa kolekcja → Kolekcja dynamiczna).
            if (!library_state_readable_) { ui.error = tr(L"Nie można odczytać historii biblioteki"); rebuildCollectionDialog(); return; }
            ui.rules = rules; ui.smart_collection = true; ui.steam_collection = false;
            ui.name_return = Page::rules; ui.page = Page::name; ui.collection.clear(); ui.input.clear(); ui.focus = -2;
        }
        rebuildCollectionDialog(); return;
    }
    if (ui.page == Page::rule_field) {
        if (!row.key.starts_with("@f:")) return;
        const auto field = static_cast<Field>(std::stoi(row.key.substr(3)));
        const bool same_kind = smart_filter::textField(field) == smart_filter::textField(ui.draft.field) && smart_filter::numberField(field) == smart_filter::numberField(ui.draft.field);
        if (field != ui.draft.field && (!same_kind || smart_filter::choiceField(field))) { ui.draft.value.clear(); ui.input.clear(); }
        ui.draft.field = field; ui.page = Page::rule; ui.scroll = 0;
        ui.focus = ui.ruleText() ? -2 : 2; // Typing, or the first choice.
        rebuildCollectionDialog(); return;
    }
    // Page::rule.
    if (row.key == "@field") { ui.page = Page::rule_field; ui.focus = static_cast<int>(ui.draft.field); ui.scroll = 0; }
    else if (row.key == "@op") ui.draft.exclude = !ui.draft.exclude;
    else if (row.key.starts_with("=")) {
        const auto value = text_encoding::wide(row.key.substr(1));
        if (smart_filter::choiceField(ui.draft.field)) ui.draft.value = value;
        else if (smart_filter::numberField(ui.draft.field)) ui.input = value;
        else {
            // A suggestion joins what was typed before the last comma ("capcom, " + Bandai Namco).
            const auto comma = ui.input.rfind(L',');
            ui.input = comma == std::wstring::npos ? value : ui.input.substr(0, comma + 1) + L' ' + value;
        }
        ui.selected = false;
    } else if (row.key == "@done" || row.key == "@remove") {
        auto rules = editedRules();
        if (row.key == "@remove") {
            if (ui.rule_index >= 0 && ui.rule_index < static_cast<int>(rules.size())) rules.erase(rules.begin() + ui.rule_index);
        } else {
            if (!smart_filter::choiceField(ui.draft.field)) ui.draft.value = trimmedValue(ui.input);
            if (!smart_filter::valid(ui.draft)) {
                ui.error = smart_filter::numberField(ui.draft.field) ? tr(L"Wpisz liczbę albo wybierz z listy") :
                           smart_filter::choiceField(ui.draft.field) ? tr(L"Wybierz z listy") : tr(L"Wpisz, czego szukać");
                rebuildCollectionDialog(); return;
            }
            if (ui.rule_index >= 0 && ui.rule_index < static_cast<int>(rules.size())) rules[static_cast<size_t>(ui.rule_index)] = ui.draft;
            else if (rules.size() < smart_filter::max_rules) rules.push_back(ui.draft);
        }
        const int focus = row.key == "@remove" ? 0 : ui.rule_index >= 0 ? ui.rule_index : static_cast<int>(rules.size()) - 1;
        ui.page = Page::rules; ui.input.clear(); ui.scroll = 0; ui.focus = std::max(0, focus);
        storeRules(rules);
        audio_.context();
        return;
    }
    rebuildCollectionDialog();
}

bool Launcher::ruleDialogBack() {
    auto& ui = library_dialog_;
    if (!rulePage(ui.page)) return false;
    ui.error.clear();
    if (ui.page == Page::rule_field) { ui.page = Page::rule; ui.focus = 0; }
    else if (ui.page == Page::rule) { ui.page = Page::rules; ui.input.clear(); ui.focus = std::max(0, ui.rule_index); }
    else if (ui.rule_target == RuleTarget::collection && !ui.settings_return) { ui.page = Page::collection; ui.focus = 0; }
    else { ui = {}; saveLibraryPreferences(); saveSettings(); mouse_transition_until_ = GetTickCount64() + GetDoubleClickTime(); InvalidateRect(hwnd_, nullptr, FALSE); return true; }
    ui.scroll = 0;
    rebuildCollectionDialog();
    return true;
}
