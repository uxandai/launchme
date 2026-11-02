// Wydawcy and Developerzy (src/library/studios.hpp): the games of the publishers and developers
// with a logo, from what the game index knows of each game. Tryb TV: a row of each under Platformy
// PC whose tiles are the logos (its "Wszystkie" opens the board of all of them, a logo the
// studio's library); the desktop window: two sidebar groups, folded until opened. The games with
// every achievement unlocked are collected here too (a gold border on their covers).
#include "launcher.hpp"
#include "../platform/text_encoding.hpp"
#include "../library/search.hpp"

#include <algorithm>
#include <map>
#include <set>

namespace {
std::wstring wideKey(studios::Role role, std::string_view id) { return text_encoding::wide(studios::key(role, id)); }
}

bool Launcher::refreshStudios() {
    std::array<std::vector<studios::Entry>, studios::role_count> groups;
    std::unordered_set<std::string> perfect;
    if (game_index_ && game_index_->loaded()) {
        std::unordered_set<std::string> keys;
        for (const auto& game : public_library_)
            if (isGame(game) && game.kind != GameKind::catalog && game.visibility == GameVisibility::public_game) keys.insert(gameKey(game));
        std::vector<studios::GameNames> publishers, developers;
        game_index_->read([&](const game_index::Index& index) {
            for (const auto& key : keys) {
                const auto* record = index.get(key);
                if (!record) continue;
                if (!record->publishers.empty()) publishers.push_back({key, record->publishers});
                if (!record->developers.empty()) developers.push_back({key, record->developers});
                if (record->achievements_total && record->achievements_unlocked >= record->achievements_total) perfect.insert(key);
            }
        });
        if (settings_.show_publishers) groups[static_cast<size_t>(studios::Role::publisher)] = studios::group(publishers);
        if (settings_.show_developers) groups[static_cast<size_t>(studios::Role::developer)] = studios::group(developers);
    }
    // What the profile saw complete (other stores' achievements through plugins).
    for (const auto& [key, progress] : history_.progress)
        if (progress.total && progress.unlocked >= progress.total) perfect.insert(key);
    // Hidden ones and those with fewer games than Najmniej gier out; the player's order first (Move
    // up / Move down), the rest by most games, then by name.
    std::unordered_map<std::string, size_t> positions;
    for (size_t i = 0; i < settings_.studio_order.size(); ++i) positions.emplace(settings_.studio_order[i], i);
    const auto least = static_cast<size_t>(std::max(1, settings_.studio_min_games));
    for (int role = 0; role < studios::role_count; ++role) {
        auto& list = groups[static_cast<size_t>(role)];
        const auto keyOf = [&](const studios::Entry& entry) { return studios::key(static_cast<studios::Role>(role), entry.studio->id); };
        std::erase_if(list, [&](const studios::Entry& entry) { return settings_.hidden_studios.contains(keyOf(entry)) || entry.games.size() < least; });
        std::stable_sort(list.begin(), list.end(), [&](const studios::Entry& a, const studios::Entry& b) {
            const auto left = positions.find(keyOf(a)), right = positions.find(keyOf(b));
            const size_t first = left == positions.end() ? SIZE_MAX : left->second, second = right == positions.end() ? SIZE_MAX : right->second;
            if (first != second) return first < second;
            if (a.games.size() != b.games.size()) return a.games.size() > b.games.size();
            return studios::fold(a.studio->name) < studios::fold(b.studio->name);
        });
    }
    const bool changed = groups != studio_groups_;
    const bool perfect_changed = perfect != perfect_games_;
    perfect_games_ = std::move(perfect);
    if (!changed) return perfect_changed;
    studio_groups_ = std::move(groups);
    studio_games_.clear();
    std::unordered_map<std::string, const Game*> by_key;
    for (const auto& game : public_library_) if (isGame(game)) by_key.emplace(gameKey(game), &game);
    for (int role = 0; role < studios::role_count; ++role)
        for (const auto& entry : studio_groups_[static_cast<size_t>(role)]) {
            auto& games = studio_games_[wideKey(static_cast<studios::Role>(role), entry.studio->id)];
            for (const auto& key : entry.games) if (const auto found = by_key.find(key); found != by_key.end()) games.push_back(*found->second);
            games = versions_.collapse(games, library_state_.preferred_versions, library_state_.first_seen);
        }
    requestStudioBadges();
    return true;
}

std::vector<Game> Launcher::studioGames(const std::string& key) const {
    const auto found = studio_games_.find(text_encoding::wide(key));
    return found == studio_games_.end() ? std::vector<Game>{} : found->second;
}

const std::vector<Game>* Launcher::consoleGames(const std::wstring& name) const {
    if (const auto found = platform_games_.find(name); found != platform_games_.end()) return &found->second;
    if (const auto found = studio_games_.find(name); found != studio_games_.end()) return &found->second;
    return nullptr;
}

Game Launcher::studioTile(studios::Role role, const studios::Entry& entry) const {
    Game tile;
    tile.kind = GameKind::platform;
    tile.name = std::wstring(entry.studio->name);
    tile.platform = wideKey(role, entry.studio->id);
    const auto& catalog = studios::catalog();
    tile.app_id = studios::tile_base + static_cast<uint32_t>(role) * 0x10000u + static_cast<uint32_t>(entry.studio - catalog.data());
    return tile;
}

// The logos as marks: the desktop header, the library's tabs.
void Launcher::requestStudioBadges() {
    if (!runtime_started_) return;
    for (int role = 0; role < studios::role_count; ++role)
        for (const auto& entry : studio_groups_[static_cast<size_t>(role)])
            requestArt(studioTile(static_cast<studios::Role>(role), entry), CoverMessage::Kind::badge, true);
}

wchar_t Launcher::homeTabGlyph(size_t category) const { return settings_.home_tab_icons ? categoryGlyph(category) : 0; }

wchar_t Launcher::categoryGlyph(size_t category) const {
    if (category >= categories_.size()) return 0;
    const auto& id = categories_[category].id;
    if (id == "pc-platforms") return L'\uE770';                // A monitor.
    if (id == "emulation") return L'\uE7FC';                   // A gamepad.
    if (id == "publishers") return L'\uE8EC';                  // A label: the brand on the box.
    if (id == "developers") return L'\uE716';                  // People: the team that made it.
    if (id == "user-collections.favorite") return L'\uE735';   // The filled star.
    return 0;
}

bool Launcher::studioMenuTile(const Game& tile) const {
    return tile.kind == GameKind::platform && tile.platform.size() > 3 && studios::parse(text_encoding::utf8(tile.platform)).has_value();
}

bool Launcher::studioSettingsToggle(const settings_ui::Row& row) {
    using settings_ui::Id;
    if (row.id == Id::home_tab_icons) { // Visual only: the tabs read it when they draw.
        settings_.home_tab_icons = !settings_.home_tab_icons;
        settingsChanged(); audio_.context();
        return true;
    }
    if (row.id == Id::flix_rail) { // The rows move right of it: Flix again.
        settings_.flix_rail = !settings_.flix_rail;
        flix_rail_focus_ = -1;
        settingsChanged(true); audio_.context();
        return true;
    }
    if (row.id == Id::studio_hidden_entry) {
        // Shown again; the page lists the others (back to Biblioteka once none is hidden).
        if (!settings_.hidden_studios.erase(row.key)) return true;
        settingsChanged(true); audio_.context();
        if (settings_.hidden_studios.empty() && settings_state_.at(settings_ui::Page::hidden_studios)) closeSettingsPage();
        return true;
    }
    bool* flag = row.id == Id::studio_publishers ? &settings_.show_publishers : row.id == Id::studio_developers ? &settings_.show_developers :
                 row.id == Id::updated_row ? &settings_.show_updated_row : row.id == Id::discovery_row ? &settings_.show_discovery_row : nullptr;
    if (!flag) return false;
    *flag = !*flag;
    settingsChanged(true); // Rebuild: the rows and the desktop sections.
    audio_.context();
    return true;
}

void Launcher::showStudios() {
    settings_.hidden_studios.clear();
    settingsChanged(true);
    if (screen_ != ScreenMode::settings) rebuildCards(true);
    audio_.context();
}

void Launcher::hideStudio(const std::string& key) {
    if (!studios::parse(key) || !settings_.hidden_studios.insert(key).second) return;
    if (desktop_section_ == key) desktop_section_ = "all";
    settingsChanged(true);
    rebuildCards(true);
    audio_.context();
}

// Up or down among the studios of its role; the whole order is kept from then on.
void Launcher::moveStudio(const std::string& key, int direction) {
    const auto parsed = studios::parse(key);
    if (!parsed || !direction) return;
    const auto role = parsed->first;
    const auto& list = studio_groups_[static_cast<size_t>(role)];
    const auto at = std::find_if(list.begin(), list.end(), [&](const studios::Entry& entry) { return entry.studio == parsed->second; });
    if (at == list.end()) return;
    const auto from = static_cast<size_t>(at - list.begin());
    const auto to = static_cast<size_t>(std::clamp(static_cast<int>(from) + direction, 0, static_cast<int>(list.size()) - 1));
    if (to == from) { navigationBoundary(); return; }
    std::vector<std::string> order;
    for (const auto& entry : list) order.push_back(studios::key(role, entry.studio->id));
    std::swap(order[from], order[to]);
    // The other role's place stays as it was.
    std::erase_if(settings_.studio_order, [&](const std::string& item) { const auto other = studios::parse(item); return !other || other->first == role; });
    settings_.studio_order.insert(settings_.studio_order.end(), order.begin(), order.end());
    settingsChanged(true);
    rebuildCards(true);
    audio_.navigate();
}

// Search by who made it ("capcom", "ubisoft montreal"), with a logo or not: the games whose
// developer or publisher has every typed word at a word's start; `names` gets each such name
// (as the index spells it) with its number of games, most first.
std::unordered_set<std::string> Launcher::studioSearch(const std::wstring& typed, std::vector<std::pair<std::wstring, size_t>>* names) const {
    std::unordered_set<std::string> keys;
    const auto query = foldGameTitle(typed);
    if (query.size() < 2 || !game_index_) return keys;
    std::map<std::wstring, std::pair<std::wstring, size_t>> found; // Folded → spelling, games.
    game_index_->read([&](const game_index::Index& index) {
        for (const auto& key : index.keys()) {
            const auto* record = index.get(key);
            if (!record) continue;
            bool hit = false;
            std::set<std::wstring> counted; // A studio both made and published it: one game.
            for (const auto* list : {&record->developers, &record->publishers})
                for (const auto& name : *list) {
                    if (titleMatch(foldGameTitle(name), query) < 2) continue;
                    hit = true;
                    auto& entry = found[studios::fold(name)];
                    if (entry.first.empty()) entry.first = name;
                    if (counted.insert(studios::fold(name)).second) ++entry.second;
                }
            if (hit) keys.insert(key);
        }
    });
    if (names) {
        for (auto& [folded, entry] : found) names->push_back(std::move(entry));
        std::sort(names->begin(), names->end(), [](const auto& a, const auto& b) { return a.second != b.second ? a.second > b.second : a.first < b.first; });
    }
    return keys;
}
// The desktop window: a studio's games. With a logo, its own section (unfolded); otherwise every
// game with that developer or publisher, through the search field.
void Launcher::desktopFilterStudio(const std::wstring& name, std::optional<studios::Role> role) {
    if (name.empty()) return;
    if (const auto* studio = studios::match(name); studio && studiosShown())
        for (const auto each : {studios::Role::publisher, studios::Role::developer}) {
            if (role && *role != each) continue;
            const auto key = studios::key(each, studio->id);
            if (desktopEntryIndex(key) < 0) continue;
            if (!desktop_query_.empty()) { desktop_query_.clear(); desktop_caret_ = 0; desktopApplyQuery(); }
            desktop_collapsed_[static_cast<size_t>(each == studios::Role::publisher ? desktop_ui::Group::publishers : desktop_ui::Group::developers)] = false;
            desktopShowSection(key);
            return;
        }
    desktopShowSection("all");
    desktop_query_ = name;
    desktop_caret_ = desktop_query_.size();
    desktopApplyQuery();
}
