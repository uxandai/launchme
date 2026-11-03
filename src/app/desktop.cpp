// Desktop mode, the library side (src/ui/desktop_view.hpp): the sidebar's sections, the section's
// games as the poster grid (sorted, filtered, searched), the selection and its artwork, the pane's
// data, the frame's motion and the ViewContext. Input: desktop_input.cpp (pointer), desktop_keys.cpp,
// desktop_actions.cpp, desktop_menus.cpp; the window: desktop_window.cpp.
#include "launcher.hpp"
#include "../platform/ascii_text.hpp"
#include "../platform/unix_time.hpp"
#include "launcher_internal.hpp"
#include "../library/organization.hpp"
#include "../library/emulation_platforms.hpp"
#include "../ui/brand_icons.hpp"
#include "../library/search.hpp"
#include "../library/steam_stats.hpp"
#include "../platform/text_encoding.hpp"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <numbers>

using namespace desktop_ui;

namespace {
// "Dzisiaj", "Wczoraj", "3 dni temu", else the date in the interface language; 0: never.
std::wstring playedWhen(uint64_t seconds) {
    if (!seconds) return tr(L"Nigdy");
    // Calendar days between the local midnights of both moments.
    const auto midnight = [](uint64_t at) {
        const auto value = static_cast<std::time_t>(at);
        std::tm local{};
        localtime_s(&local, &value);
        local.tm_hour = local.tm_min = local.tm_sec = 0; local.tm_isdst = -1;
        return std::mktime(&local);
    };
    const auto now = unix_time::now();
    const auto days = static_cast<int64_t>(std::llround(std::difftime(midnight(now), midnight(std::min(seconds, now))) / 86400.0));
    if (days <= 0) return tr(L"Dzisiaj");
    if (days == 1) return tr(L"Wczoraj");
    if (days < 7) return std::to_wstring(days) + tr(L" dni temu");
    FILETIME file{};
    const auto ticks = (seconds + 11644473600ull) * 10000000ull;
    file.dwLowDateTime = static_cast<DWORD>(ticks); file.dwHighDateTime = static_cast<DWORD>(ticks >> 32);
    FILETIME local_file{}; SYSTEMTIME system{};
    if (!FileTimeToLocalFileTime(&file, &local_file) || !FileTimeToSystemTime(&local_file, &system)) return {};
    wchar_t text[64]{};
    if (!GetDateFormatEx(i18n::localeName(), 0, &system, L"d MMM yyyy", text, 64, nullptr)) return {};
    return text;
}
std::wstring trimmed(const std::wstring& text) { return std::wstring(ascii_text::trim(text, L" ")); }
bool gameEntry(EntryKind kind) { return kind != EntryKind::hidden; }
}

std::wstring desktopPlayedWhen(uint64_t seconds) { return playedWhen(seconds); } // Szczegóły's sheet (desktop_settings.cpp).

// ---- Geometry --------------------------------------------------------------------------------

float Launcher::desktopScale() const { return renderer_.desktopViewport().scale; }

desktop_ui::Layout Launcher::desktopLayout() const {
    const auto view = renderer_.desktopViewport();
    Layout layout;
    layout.width = view.width; layout.height = view.height;
    layout.poster_width = static_cast<float>(settings_.desktop_poster);
    layout.tools = static_cast<int>(desktopTools().size());
    layout.companion_card = settings_.companion && !settings_.desktop_narrow; // Narrow: icons only, no card.
    layout.narrow = desktop_narrow_anim_;
    layout.pane_width = static_cast<float>(settings_.desktop_pane);
    layout.favorite = favoritesShown();
    return layout;
}

std::vector<desktop_ui::Tool> Launcher::desktopTools() const {
    std::vector<Tool> tools;
    if (updateAvailable()) tools.push_back(Tool::update); // A newer Launchme: first, in the accent colour.
    tools.push_back(Tool::friends); // Widżety (the drawer: the profile, friends, sensors, plugins), first.
    if (settings_.glance) tools.push_back(Tool::glance);
    if (settings_.tarot) tools.push_back(Tool::tarot);
    tools.push_back(Tool::store); // Always there: with no store plugin it says so.
    tools.push_back(Tool::downloads);
    tools.push_back(Tool::tv);
    tools.push_back(Tool::settings);
    return tools;
}

// ---- Sidebar ---------------------------------------------------------------------------------

void Launcher::buildDesktopSidebar() {
    std::vector<Entry> entries;
    const auto add = [&](EntryKind kind, Group group, std::string id, std::wstring title, size_t count, wchar_t icon) -> Entry& {
        Entry entry;
        entry.kind = kind; entry.group = group; entry.id = std::move(id); entry.title = std::move(title);
        entry.count = count; entry.icon = icon;
        entries.push_back(std::move(entry));
        return entries.back();
    };
    const auto shown = [&](const Game& game) { return isGame(game) && (private_shelf_ || game.visibility == GameVisibility::public_game); };
    size_t all = 0, recent = 0;
    for (const auto& game : grouped_library_) {
        if (!shown(game)) continue;
        ++all;
        if (settings_.recent_uninstalled ? recentTime(game) != 0 : game.last_played != 0) ++recent;
    }
    const size_t favorites = desktopFavoriteGames().size();
    add(EntryKind::all, Group::library, "all", tr(L"Wszystkie gry"), all, glyph::library);
    add(EntryKind::recent, Group::library, "recent", tr(L"Ostatnio grane"), recent, glyph::history);
    if (favoritesShown()) add(EntryKind::favorites, Group::library, "favorites", tr(L"Ulubione"), favorites, glyph::star).droppable = true;
    if (private_shelf_ && !private_category_.games.empty())
        add(EntryKind::hidden, Group::library, "hidden", tr(L"Ukryte"), private_category_.games.size(), glyph::hide);
    // Every store with games, in the TV mode's order; each copy under its own store.
    std::map<GameSource, size_t> per_source;
    for (const auto& game : public_library_) if (shown(game) && game.kind != GameKind::nonsteam) ++per_source[platformSource(game.source)];
    // DRM-free: the scanned games and those added by hand, one section.
    for (const auto source : {GameSource::steam, GameSource::gog, GameSource::epic, GameSource::battlenet, GameSource::ea, GameSource::ubisoft,
                              GameSource::amazon, GameSource::xbox, GameSource::multimc, GameSource::drmfree}) {
        const auto found = per_source.find(source);
        if (found == per_source.end() || !found->second) continue;
        auto& entry = add(EntryKind::source, Group::platforms, std::string("source:") + sourceSlug(source), sourceName(source), found->second,
                          source == GameSource::drmfree ? glyph::folder : glyph::shop);
        entry.source = source;
        entry.brand = brand_icons::sourceKey(source); // Its mark instead of the icon.
    }
    if (settings_.show_emulation)
        for (const auto& platform : emulation_platforms) {
            const std::wstring name(platform.name);
            const auto found = platform_games_.find(name);
            if (found == platform_games_.end() || found->second.empty()) continue;
            auto& entry = add(EntryKind::console, Group::emulation, "console:" + text_encoding::utf8(name), name, found->second.size(), glyph::game);
            entry.console = name;
            entry.brand = brand_icons::consoleKey(name);
        }
    // Every collection (also those hidden on the TV home); Ulubione stands above, Emulacja covers SRM's.
    std::vector<SteamCollection> collections;
    for (const auto& c : settings_collections_) {
        if (favoriteCollection(c) || (srmCollectionsHidden() && srmCollection(c))) continue;
        collections.push_back(c);
    }
    std::unordered_map<std::string, size_t> counts;
    for (const auto& shelf : managedShelves(account_, collections, library_state_.collections, public_library_, private_shelf_))
        counts[shelf.id] = versions_.collapse(shelf.games, library_state_.preferred_versions, library_state_.first_seen).size();
    for (const auto& c : collections) {
        auto& entry = add(EntryKind::collection, Group::collections, "collection:" + c.id, c.name, counts[c.id], glyph::folder);
        entry.collection = c.id;
        entry.droppable = entry.reorderable = true;
    }
    // Wydawcy and Developerzy (src/app/studio_libraries.cpp): folded by default.
    for (const auto role : {studios::Role::publisher, studios::Role::developer})
        for (const auto& studio : studio_groups_[static_cast<size_t>(role)]) {
            const auto key = studios::key(role, studio.studio->id);
            const auto* games = consoleGames(text_encoding::wide(key));
            if (!games || games->empty()) continue;
            add(EntryKind::studio, role == studios::Role::publisher ? Group::publishers : Group::developers, key, std::wstring(studio.studio->name),
                games->size(), glyph::tag);
        }
    desktop_entries_ = std::move(entries);
    // A section not (yet) in the list shows every game meanwhile: stores and consoles may still be
    // scanning. A collection missing once the library is read is gone.
    if (desktopEntryIndex(desktop_section_) < 0 && !scanning_ && desktop_section_.starts_with("collection:")) desktop_section_ = "all";
    if (desktopEntryIndex(desktop_section_) < 0 && studios::parse(desktop_section_) && (!studiosShown() || settings_.hidden_studios.contains(desktop_section_)))
        desktop_section_ = "all"; // Switched off or hidden; one the index has not described yet may still come.
}

std::vector<desktop_ui::Row> Launcher::desktopRows() const { return layoutRows(desktop_entries_, desktop_collapsed_, settings_.desktop_narrow); }

int Launcher::desktopEntryIndex(const std::string& id) const {
    for (int i = 0; i < static_cast<int>(desktop_entries_.size()); ++i) if (desktop_entries_[i].id == id) return i;
    return -1;
}

const desktop_ui::Entry* Launcher::desktopEntry() const {
    const int index = desktopEntryIndex(desktop_section_);
    return index < 0 ? nullptr : &desktop_entries_[static_cast<size_t>(index)];
}

const SteamCollection* Launcher::desktopCollection(const std::string& id) const {
    const auto found = std::find_if(settings_collections_.begin(), settings_collections_.end(), [&](const SteamCollection& c) { return c.id == id; });
    return found == settings_collections_.end() ? nullptr : &*found;
}

// Ulubione as Tryb TV's shelf of it: the favourites collection's games (every store's copies, the
// private ones while shown), with any other game marked a favourite; one card per version group.
std::vector<Game> Launcher::desktopFavoriteGames() const {
    std::vector<Game> games;
    std::unordered_set<std::string> keys;
    for (auto& shelf : managedShelves(account_, {desktopFavorites()}, library_state_.collections, public_library_, private_shelf_))
        for (auto& game : versions_.collapse(shelf.games, library_state_.preferred_versions, library_state_.first_seen))
            if (isGame(game) && keys.insert(library_view::selectionKey(game)).second) games.push_back(std::move(game));
    for (const auto& game : grouped_library_)
        if (isGame(game) && (private_shelf_ || game.visibility == GameVisibility::public_game) && isFavorite(game) &&
            keys.insert(library_view::selectionKey(game)).second) games.push_back(game);
    return games;
}

SteamCollection Launcher::desktopFavorites() const {
    const auto found = std::find_if(settings_collections_.begin(), settings_collections_.end(), favoriteCollection);
    return found == settings_collections_.end() ? SteamCollection{tr(L"Ulubione"), {}, "user-collections.favorite"} : *found;
}

// ---- The section's games ---------------------------------------------------------------------

std::vector<Game> Launcher::desktopSource() const {
    const auto* entry = desktopEntry();
    const auto kind = entry ? entry->kind : EntryKind::all;
    std::vector<Game> games;
    switch (kind) {
    case EntryKind::all: return grouped_library_;
    case EntryKind::recent:
        for (const auto& game : grouped_library_)
            if (settings_.recent_uninstalled ? recentTime(game) != 0 : game.last_played != 0) games.push_back(game);
        return games;
    case EntryKind::favorites: return desktopFavoriteGames();
    case EntryKind::hidden:
        for (const auto& game : private_category_.games)
            if (settings_.show_uninstalled || game.installed || game.installation.active()) games.push_back(game);
        return games;
    case EntryKind::source:
        for (const auto& game : public_library_) if (platformSource(game.source) == entry->source && game.kind != GameKind::nonsteam) games.push_back(game);
        return games;
    case EntryKind::console:
        if (const auto found = platform_games_.find(entry->console); found != platform_games_.end()) return found->second;
        return games;
    case EntryKind::studio: return studioGames(entry->id);
    case EntryKind::collection:
        if (const auto* collection = desktopCollection(entry->collection))
            for (auto& shelf : managedShelves(account_, {*collection}, library_state_.collections, public_library_, private_shelf_))
                return versions_.collapse(shelf.games, library_state_.preferred_versions, library_state_.first_seen);
        return games;
    }
    return games;
}

std::vector<Game> Launcher::desktopGames() const {
    const auto* entry = desktopEntry();
    const bool recent = entry && entry->kind == EntryKind::recent;
    library_view::Options options;
    options.order = recent ? library_view::Order::last_played : library_view::orderFromPersisted(settings_.desktop_sort);
    options.installed_only = settings_.desktop_installed;
    std::unordered_map<std::string, int> released;
    if (options.order == library_view::Order::release && game_index_)
        released = game_index_->read([](const game_index::Index& index) { return index.releases(); });
    const bool include_private = private_shelf_ || (entry && !gameEntry(entry->kind));
    auto games = library_view::arrange(desktopSource(), options, library_state_.first_seen, include_private, &released);
    std::erase_if(games, [](const Game& game) { return !isGame(game); });
    // Ostatnio grane with games not installed: those by the date they were added, among the played.
    if (recent && settings_.recent_uninstalled)
        std::stable_sort(games.begin(), games.end(), [&](const Game& a, const Game& b) { return recentTime(a) > recentTime(b); });
    // Search: the title, a genre/tag the game index knows ("rpg", "roguelike") or its developer or publisher.
    const auto typed = trimmed(desktop_query_);
    const auto query = foldGameTitle(typed);
    if (!query.empty()) {
        std::unordered_set<std::string> labelled;
        if (game_index_) for (auto& key : game_index_->read([&](const game_index::Index& index) { return index.withLabel(typed); })) labelled.insert(std::move(key));
        labelled.merge(studioSearch(typed)); // Or who made it: "capcom", "devolver".
        std::erase_if(games, [&](const Game& game) { return !titleMatch(foldGameTitle(game.name), query) && !labelled.contains(gameKey(game)); });
    }
    return games;
}

// The grid over the same screen keeps every card still listed: its decoded art, its fade and its
// place (the slots follow it); the focus follows its game.
void Launcher::rebuildDesktop(bool preserve_background) {
    auto games = desktopGames();
    const bool same_slots = desktop_slots_ == renderer_.slotGeneration() && desktop_keys_.size() == cards_.size();
    std::unordered_map<std::string, size_t> previous;
    if (same_slots) for (size_t i = 0; i < desktop_keys_.size(); ++i) previous.emplace(desktop_keys_[i], i);
    if (same_slots && selected_ < desktop_keys_.size()) desktop_focus_key_ = desktop_keys_[selected_];
    auto old_cards = std::move(cards_);
    auto old_hover = std::move(desktop_card_hover_);
    cards_.clear(); desktop_card_hover_.clear();
    std::vector<std::string> keys;
    std::vector<size_t> from;
    cards_.reserve(games.size()); keys.reserve(games.size()); from.reserve(games.size());
    for (auto& game : games) {
        auto key = library_view::selectionKey(game);
        const auto old = previous.find(key);
        if (old != previous.end() && old->second < old_cards.size()) {
            cards_.push_back(old_cards[old->second]);
            cards_.back().game = std::move(game);
            desktop_card_hover_.push_back(old->second < old_hover.size() ? old_hover[old->second] : 0.0f);
            from.push_back(old->second);
            previous.erase(old);
        } else {
            cards_.push_back(GameCard{std::move(game)});
            desktop_card_hover_.push_back(0);
            from.push_back(SIZE_MAX);
        }
        keys.push_back(std::move(key));
    }
    if (same_slots) renderer_.remapCovers(from, preserve_background);
    else { renderer_.resetCovers(cards_.size(), preserve_background); desktop_arrived_ = 0; }
    desktop_keys_ = std::move(keys);
    desktop_slots_ = renderer_.slotGeneration();
    std::erase_if(desktop_marked_, [&](const std::string& key) { return std::find(desktop_keys_.begin(), desktop_keys_.end(), key) == desktop_keys_.end(); });
    // The focus: the game restored from the last session, the same game, else the nearest place.
    size_t focus = std::min(selected_, cards_.empty() ? size_t{0} : cards_.size() - 1);
    bool restored = false;
    for (const auto* wanted : {&desktop_restore_key_, &desktop_focus_key_}) {
        if (wanted->empty()) continue;
        const auto found = std::find(desktop_keys_.begin(), desktop_keys_.end(), *wanted);
        if (found == desktop_keys_.end()) continue;
        focus = static_cast<size_t>(found - desktop_keys_.begin());
        restored = wanted == &desktop_restore_key_;
        break;
    }
    if (restored) desktop_restore_key_.clear();
    const bool changed = focus >= desktop_keys_.size() || desktop_focus_key_ != desktop_keys_[focus];
    selected_ = focus;
    if (!cards_.empty()) desktop_focus_key_ = desktop_keys_[selected_];
    const auto layout = desktopLayout();
    desktop_scroll_target_ = std::clamp(desktop_scroll_target_, 0.0f, layout.maxScroll(cards_.size()));
    if (restored) desktop_scroll_target_ = layout.reveal(selected_, cards_.size(), desktop_scroll_target_);
    desktop_scroll_ = std::clamp(desktop_scroll_, 0.0f, layout.maxScroll(cards_.size()));
    if (restored) desktop_scroll_ = desktop_scroll_target_;
    if (changed) { detail_ = 0; desktop_pane_scroll_ = 0; preview_.settle(); }
    if (!cards_.empty()) if (const auto* art = art_.find(gameKey(cards_[selected_].game))) {
        if (art->hero) renderer_.setHero(selected_, art->hero, art->hero_tone, false, art->hero_focus);
        if (art->logo) renderer_.setLogo(selected_, art->logo);
    }
    desktopRequestArt();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::desktopShowSection(const std::string& id) {
    if (desktopEntryIndex(id) < 0) return;
    const bool same = id == desktop_section_;
    desktop_section_ = id;
    desktop_marked_.clear();
    if (!same) {
        desktop_scroll_ = desktop_scroll_target_ = 0;
        desktop_focus_key_.clear();
        selected_ = 0;
    }
    settings_.desktop_section = id;
    if (screen_ == ScreenMode::shelves) rebuildCards(true);
}

void Launcher::desktopFocus(size_t index, bool reveal) {
    if (cards_.empty()) return;
    index = std::min(index, cards_.size() - 1);
    const bool changed = index != selected_;
    selected_ = index;
    desktop_focus_key_ = index < desktop_keys_.size() ? desktop_keys_[index] : std::string{};
    if (reveal) desktop_scroll_target_ = desktopLayout().reveal(index, cards_.size(), desktop_scroll_target_);
    if (changed) { detail_ = 0; desktop_pane_scroll_ = 0; preview_.settle(); library_focus_age_ = 0; }
    if (const auto* art = art_.find(gameKey(cards_[selected_].game))) {
        if (art->hero) renderer_.setHero(selected_, art->hero, art->hero_tone, false, art->hero_focus);
        if (art->logo) renderer_.setLogo(selected_, art->logo);
    }
    requestVisibleArt();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

uint32_t Launcher::desktopArtEdge() const {
    // A cell's width in device pixels (posters are 2:3, so their shorter side), rounded up to 64 px
    // so a window resized by a few pixels keeps its decodes; never above the default 1024.
    const float pixels = desktopLayout().cellWidth() * desktopScale();
    return std::clamp(static_cast<uint32_t>(std::ceil(pixels / 64.0f)) * 64u, 128u, 1024u);
}

// The rows in view, one behind and a screen ahead in the direction the grid last moved; `order`
// lists their cards by distance from the view's middle (visible rows first). `keep_*`: two more
// rows on each side, whose bitmaps are never given back.
Launcher::DesktopArtRange Launcher::desktopArtRange() const {
    DesktopArtRange range;
    const size_t count = cards_.size();
    if (!count) return range;
    const auto layout = desktopLayout();
    const size_t columns = static_cast<size_t>(layout.columns());
    const auto [first, end] = layout.visibleRange(count, desktop_scroll_target_, 0);
    const size_t total_rows = (count + columns - 1) / columns;
    const size_t first_row = std::min(first / columns, total_rows - 1), last_row = std::max(first_row, end ? (end - 1) / columns : 0);
    const size_t ahead = std::max<size_t>(2, last_row - first_row + 1), behind = 1;
    const size_t before = desktop_scroll_dir_ > 0 ? behind : ahead, after = desktop_scroll_dir_ > 0 ? ahead : behind;
    const size_t want_first_row = first_row > before ? first_row - before : 0;
    const size_t want_last_row = std::min(total_rows - 1, last_row + after);
    range.first = want_first_row * columns;
    range.end = std::min(count, (want_last_row + 1) * columns);
    range.keep_first = range.first > columns * 2 ? range.first - columns * 2 : 0;
    range.keep_end = std::min(count, range.end + columns * 2);
    const float middle = (static_cast<float>(first_row) + static_cast<float>(last_row)) * 0.5f;
    const auto distance = [&](size_t i) {
        const size_t row = i / columns;
        const bool visible = row >= first_row && row <= last_row;
        return (visible ? 0.0f : 10000.0f) + std::abs(static_cast<float>(row) - middle) * 64.0f + static_cast<float>(i % columns);
    };
    for (size_t i = range.first; i < range.end; ++i) range.order.push_back(i);
    std::stable_sort(range.order.begin(), range.order.end(), [&](size_t a, size_t b) { return distance(a) < distance(b); });
    return range;
}

// Posters of the rows in view, then of the rows the grid moves towards, nearest first: the disk
// lane, the decoders and the uploads all take them in that order. The square cover stands in only
// where the poster is not on disk (the disk lane says so) or cannot be fetched now, and for the
// pane. Queued requests for rows the grid has left are dropped; bitmaps of cards further away are
// given back from the farthest once they exceed a budget, so scrolling back finds them still there.
void Launcher::desktopRequestArt() {
    if (cards_.empty()) return;
    const size_t count = cards_.size();
    if (std::abs(desktop_scroll_target_ - desktop_request_scroll_) > 1.0f) desktop_scroll_dir_ = desktop_scroll_target_ > desktop_request_scroll_ ? 1 : -1;
    desktop_request_scroll_ = desktop_scroll_target_;
    const auto range = desktopArtRange();
    const uint32_t edge = desktopArtEdge();
    const auto& selected = cards_[selected_].game;
    {
        std::lock_guard lock(jobs_->mutex);
        jobs_->focus = gameKey(selected);
    }
    std::unordered_map<std::string, int> ranks; // artRequestKey → place in the queue.
    ranks.reserve(range.order.size() * 2);
    for (size_t place = 0; place < range.order.size(); ++place) {
        const size_t i = range.order[place];
        const auto& game = cards_[i].game;
        const auto key = gameKey(game);
        const auto* art = art_.find(key);
        const bool poster = art && art->poster;
        if (poster) renderer_.setPoster(i, art->poster, edge);
        else if (!renderer_.hasPoster(i)) requestArt(game, CoverMessage::Kind::poster); // Shown: its bytes may be let go (low memory).
        ranks.emplace(artRequestKey(game, CoverMessage::Kind::poster), static_cast<int>(place * 2));
        // The square cover in the poster's frame: only while the poster is missing.
        if (!poster && (!isGame(game) || desktop_poster_missing_.contains(key) || artBlocked(game, CoverMessage::Kind::poster))) {
            if (art && art->cover) {
                if (!renderer_.hasCover(i)) { cards_[i].cover_opacity = 0; renderer_.setCover(i, art->cover, art->focus, edge); }
                else renderer_.setCover(i, art->cover, art->focus, edge);
            } else if (!renderer_.hasCover(i)) requestArt(game, CoverMessage::Kind::cover);
            ranks.emplace(artRequestKey(game, CoverMessage::Kind::cover), static_cast<int>(place * 2 + 1));
        }
        renderer_.rankArt(i, static_cast<int>(place));
    }
    // The pane: the selected game's background, logo and (blurred, when it has no background) cover.
    if (const auto* art = art_.find(gameKey(selected)); art && art->cover && !renderer_.hasCover(selected_))
        renderer_.setCover(selected_, art->cover, art->focus, edge);
    requestArt(selected, CoverMessage::Kind::hero);
    requestArt(selected, CoverMessage::Kind::logo);
    requestArt(selected, CoverMessage::Kind::cover);
    {
        // Rows left behind: their queued requests go (a later view asks again; the disk lane is
        // quick), the rest is ordered as above. Retries keep their backoff; other kinds go first.
        std::lock_guard lock(jobs_->mutex);
        const auto rank = [&](const Job& job) { const auto found = ranks.find(job.key); return found == ranks.end() ? -1 : found->second; };
        for (auto* queue : {&jobs_->jobs, &jobs_->secondary}) {
            std::erase_if(*queue, [&](const Job& job) {
                if (job.prefetch || job.retry_count || (job.kind != CoverMessage::Kind::poster && job.kind != CoverMessage::Kind::cover) ||
                    gameKey(job.game) == jobs_->focus || ranks.contains(job.key)) return false;
                jobs_->requested.erase(job.key);
                return true;
            });
            std::stable_sort(queue->begin(), queue->end(), [&](const Job& a, const Job& b) { return rank(a) < rank(b); });
        }
    }
    // Bitmaps: kept near the view; further away, a card whose art is still decoding is dropped (no
    // longer wanted), shown ones stay until their bytes pass the budget, the farthest go first.
    // About 45 posters of 600×900 beyond the kept rows (low memory: a third); as BC1 an eighth of the
    // bytes each, so the same budget keeps far more of them.
    const size_t budget = !settings_.low_memory ? (settings_.texture_compression ? size_t{48} << 20 : size_t{96} << 20) :
                          settings_.texture_compression ? size_t{32} << 20 : size_t{48} << 20;
    const size_t columns = static_cast<size_t>(desktopLayout().columns());
    const size_t middle = (range.first + range.end) / 2 / columns;
    std::vector<std::pair<size_t, size_t>> shown; // Rows from the middle, card.
    size_t bytes = 0;
    for (size_t i = 0; i < count; ++i) {
        if (i == selected_) continue;
        const bool inside = i >= range.keep_first && i < range.keep_end;
        if (!inside) renderer_.releasePreview(i);
        if (!renderer_.hasPoster(i) && !renderer_.hasCover(i)) continue;
        const size_t size = renderer_.artBytes(i);
        if (inside) { bytes += size; continue; }
        if (!renderer_.artShown(i)) { renderer_.releaseCover(i); renderer_.releasePoster(i); continue; }
        const size_t row = i / columns;
        shown.emplace_back(row > middle ? row - middle : middle - row, i);
        bytes += size;
    }
    if (bytes > budget) {
        std::sort(shown.begin(), shown.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
        for (const auto& [distance, i] : shown) {
            if (bytes <= budget) break;
            bytes -= std::min(bytes, renderer_.artBytes(i));
            renderer_.releaseCover(i); renderer_.releasePoster(i);
        }
    }
}

// ---- Motion ----------------------------------------------------------------------------------

bool Launcher::tickDesktop(float dt) {
    bool paint = false;
    const auto now = std::chrono::steady_clock::now();
    const auto ms = GetTickCount64();
    if (ms - desktop_install_sync_ >= 400) { desktop_install_sync_ = ms; desktopSyncInstallations(); }
    if ((desktop_quick_.open || desktop_meta_.open) && ms / 530 != desktop_quick_blink_) { desktop_quick_blink_ = ms / 530; paint = true; } // Their caret.
    // An installation without a known share sweeps (its poster and the pane's button).
    if (std::any_of(cards_.begin(), cards_.end(), [](const auto& card) { return card.game.installation.active() && !card.game.installation.percent(); })) paint = true;
    if (desktop_query_dirty_ && now - desktop_query_edited_ >= std::chrono::milliseconds(120)) desktopApplyQuery();
    const auto layout = desktopLayout();
    const size_t count = cards_.size();
    // Sidebar: the active section's bar glides on a spring; group chevrons turn as sections fold;
    // a drop flashes its target row; Biblioteka's refresh arrows spin while rescans run.
    {
        const auto rows = desktopRows();
        int active_entry = desktopEntryIndex(desktop_section_);
        if (active_entry < 0) active_entry = desktopEntryIndex("all");
        float active_target = -1;
        for (const auto& row : rows)
            if (!row.heading && row.entry == active_entry) { active_target = row.top; break; }
        if (active_target >= 0) {
            if (!desktop_active_y_valid_) { desktop_active_y_.snap(active_target); desktop_active_y_valid_ = true; }
            else desktop_active_y_.step(active_target, dt, spring::glide.omega, spring::glide.zeta);
            if (!desktop_active_y_.settled(active_target)) paint = true;
        } else desktop_active_y_valid_ = false;
        for (int g = 0; g < group_count; ++g) {
            const float target = desktop_collapsed_[static_cast<size_t>(g)] ? 1.0f : 0.0f;
            auto& anim = desktop_collapse_anim_[static_cast<size_t>(g)];
            if (!desktop_collapse_valid_) anim = target;
            else if (std::abs(anim - target) > 0.004f) { anim += (target - anim) * ease(dt, 18.0f); paint = true; }
            else anim = target;
        }
        desktop_collapse_valid_ = true;
        // Wide ↔ narrow: the sidebar eases, the grid widening or narrowing with it.
        const float narrow = settings_.desktop_narrow ? 1.0f : 0.0f;
        if (!desktop_narrow_valid_) { desktop_narrow_anim_ = narrow; desktop_narrow_valid_ = true; }
        else if (std::abs(desktop_narrow_anim_ - narrow) > 0.003f) { desktop_narrow_anim_ += (narrow - desktop_narrow_anim_) * ease(dt, 14.0f); paint = true; }
        else if (desktop_narrow_anim_ != narrow) { desktop_narrow_anim_ = narrow; paint = true; }
        if (const auto columns = desktopLayout().columns(); columns != desktop_columns_) {
            if (desktop_columns_) desktopReflow(); else desktop_columns_ = columns; // The first frame only notes them.
        }
        if (desktop_drop_flash_ > 0.0f) {
            desktop_drop_flash_ = std::max(0.0f, desktop_drop_flash_ - dt / 0.45f);
            paint = true;
        }
        const bool refreshing = scanning_ || pc_scanning_ || drm_scanning_ || steam_rescan_->load();
        if (refreshing && desktop_refresh_spin_ < 0.25f) desktop_refresh_spin_ = 0.25f;
        if (desktop_refresh_spin_ > 0.0f) {
            desktop_refresh_spin_ = refreshing ? std::max(desktop_refresh_spin_, 0.25f) : std::max(0.0f, desktop_refresh_spin_ - dt);
            desktop_refresh_angle_ = std::fmod(desktop_refresh_angle_ + dt * 6.5f, 2.0f * std::numbers::pi_v<float>);
            paint = true;
        }
        for (auto it = desktop_mark_pulse_.begin(); it != desktop_mark_pulse_.end();) {
            it->second = std::max(0.0f, it->second - dt / 0.18f);
            paint = true;
            if (it->second <= 0.0f) it = desktop_mark_pulse_.erase(it);
            else ++it;
        }
    }
    // The wheel and keys set the target; the grid glides there.
    desktop_scroll_target_ = std::clamp(desktop_scroll_target_, 0.0f, layout.maxScroll(count));
    if (std::abs(desktop_scroll_ - desktop_scroll_target_) > 0.3f) {
        const float before = desktop_scroll_;
        desktop_scroll_ += (desktop_scroll_target_ - desktop_scroll_) * ease(dt, 16.0f);
        if (std::floor(before / layout.rowStep()) != std::floor(desktop_scroll_ / layout.rowStep())) desktopRequestArt();
        desktop_scrolled_at_ = ms;
        paint = true;
    } else if (desktop_scroll_ != desktop_scroll_target_) { desktop_scroll_ = desktop_scroll_target_; paint = true; }
    // The scrollbar shows while the grid moves, the pointer is over it or it is dragged.
    const bool over_bar = desktop_hover_.target == Target::scrollbar || desktop_drag_.kind == DesktopDrag::Kind::scrollbar;
    const bool over_grid = desktop_hover_.target == Target::card || desktop_hover_.target == Target::card_play ||
                           desktop_hover_.target == Target::card_check || desktop_hover_.target == Target::grid;
    const float bar = over_bar ? 1.0f : (ms - desktop_scrolled_at_ < 1100 || over_grid) ? 0.55f : 0.0f;
    if (std::abs(desktop_scrollbar_ - bar) > 0.01f) { desktop_scrollbar_ += (bar - desktop_scrollbar_) * ease(dt, 10.0f); paint = true; }
    else desktop_scrollbar_ = bar;
    // Cards fade in as a quick wave (rebuilt lists), covers as they arrive, hover lifts them.
    desktop_arrived_ += dt;
    if (desktop_card_hover_.size() != count) desktop_card_hover_.assign(count, 0);
    const auto [first, end] = layout.visibleRange(count, desktop_scroll_, 1);
    const int hovered = desktop_drag_.active ? -1 : desktop_hover_.target == Target::card || desktop_hover_.target == Target::card_play ||
                        desktop_hover_.target == Target::card_check ? desktop_hover_.index : -1;
    for (size_t i = 0; i < count; ++i) {
        auto& card = cards_[i];
        if (i < first || i >= end) { card.opacity = 1; card.cover_opacity = 1; desktop_card_hover_[i] = 0; continue; }
        const float delay = static_cast<float>(std::min<size_t>(i - first, 16)) * 0.016f;
        if (desktop_arrived_ < delay) { card.opacity = 0; paint = true; continue; }
        if (card.opacity < 0.995f) { card.opacity += (1 - card.opacity) * ease(dt, 14.0f); paint = true; } else card.opacity = 1;
        if (renderer_.hasCover(i) || renderer_.hasPoster(i)) {
            if (card.cover_opacity < 0.995f) { card.cover_opacity += (1 - card.cover_opacity) * ease(dt, 9.0f); paint = true; } else card.cover_opacity = 1;
        }
        const float target = static_cast<int>(i) == hovered ? 1.0f : 0.0f;
        auto& hover = desktop_card_hover_[i];
        if (std::abs(hover - target) > 0.004f) { hover += (target - hover) * ease(dt, 18.0f); paint = true; } else hover = target;
        const float emphasis = i == selected_ ? 1.0f : 0.0f;
        if (std::abs(card.emphasis - emphasis) > 0.004f) { card.emphasis += (emphasis - card.emphasis) * ease(dt, 14.0f); paint = true; } else card.emphasis = emphasis;
    }
    // Tooltips after a short rest of the pointer.
    const bool tip = !desktop_drag_.active && !desktop_menu_ && !desktopTooltip(desktop_hover_).empty() && ms - desktop_hover_at_ >= 520;
    const float tip_target = tip ? 1.0f : 0.0f;
    if (std::abs(desktop_tooltip_ - tip_target) > 0.01f) { desktop_tooltip_ += (tip_target - desktop_tooltip_) * ease(dt, tip ? 16.0f : 30.0f); paint = true; }
    else desktop_tooltip_ = tip_target;
    // The search caret blinks while the field is focused.
    if (desktop_search_focus_) { static ULONGLONG phase = 0; const auto now_phase = ms / 530; if (now_phase != phase) { phase = now_phase; paint = true; } }
    // The selected game's description for the pane (debounced by the preview worker).
    if (runtime_started_ && !cards_.empty()) preview_.request(cards_[selected_].game, account_);
    // Its achievements outside Steam (GOG, Epic, RetroAchievements…) once the selection rests.
    if (runtime_started_ && !cards_.empty() && preview_.settled()) requestFocusedAchievements(cards_[selected_].game);
    // Pobierania: the shortcut's ring and the open list follow the sources.
    if (ms - desktop_downloads_at_ >= (desktop_downloads_open_ ? 400u : 1500u)) {
        desktop_downloads_at_ = ms;
        const auto previous = desktop_download_rows_.size();
        const bool moving_before = std::any_of(desktop_download_rows_.begin(), desktop_download_rows_.end(), [](const auto& row) { return row.moving; });
        desktopRefreshDownloads();
        if (desktop_download_rows_.size() != previous || moving_before || desktop_downloads_open_) paint = true;
    }
    if (std::any_of(desktop_download_rows_.begin(), desktop_download_rows_.end(), [](const auto& row) { return row.moving && !row.fraction; })) paint = true;
    return paint;
}

void Launcher::desktopRefreshDownloads() {
    desktop_download_games_ = collectDownloads(desktop_download_rows_);
    // Wyczyść listę put these away until their state changes.
    for (size_t i = desktop_download_rows_.size(); i-- > 0;) {
        if (i >= desktop_download_games_.size() || desktop_download_rows_[i].moving) continue;
        const auto found = desktop_cleared_downloads_.find(gameKey(desktop_download_games_[i]));
        if (found == desktop_cleared_downloads_.end() || found->second != desktop_download_rows_[i].status) continue;
        desktop_download_rows_.erase(desktop_download_rows_.begin() + static_cast<std::ptrdiff_t>(i));
        desktop_download_games_.erase(desktop_download_games_.begin() + static_cast<std::ptrdiff_t>(i));
    }
}

bool Launcher::desktopDownloadsClearable() const {
    return std::any_of(desktop_download_rows_.begin(), desktop_download_rows_.end(), [](const auto& row) { return !row.moving; });
}

// ---- The pane --------------------------------------------------------------------------------

const GameDetails& Launcher::desktopDetails(const Game& game, bool& loading) {
    const auto key = gameKey(game);
    const auto* preview = preview_.find(game);
    if (key != desktop_details_key_ || (preview != nullptr) != desktop_details_preview_ || game_index_revision_ != desktop_details_revision_) {
        const auto* local = localDetails(game);
        desktop_details_ = preview ? *preview : local ? *local : GameDetails{};
        desktop_details_.playtime_minutes = gamePlaytime(game, desktop_details_.playtime_minutes);
        completeDetails(desktop_details_, game);
    if (const auto* own = metadataOverride(game)) metadata::apply(*own, desktop_details_); // Edytuj metadane wins.
        desktop_details_key_ = key;
        desktop_details_preview_ = preview != nullptr;
        desktop_details_revision_ = game_index_revision_;
    }
    loading = steamArtwork(game) && !preview && desktop_details_.description.empty();
    return desktop_details_;
}

void Launcher::desktopView(desktop_ui::View& view) {
    view.entries = &desktop_entries_;
    view.companies = settings_.show_companies;
    view.rows = desktopRows();
    view.collapsed = desktop_collapsed_;
    for (int g = 0; g < group_count; ++g)
        view.collapse_progress[static_cast<size_t>(g)] = desktop_collapse_valid_ ? desktop_collapse_anim_[static_cast<size_t>(g)] :
                                                         (desktop_collapsed_[static_cast<size_t>(g)] ? 1.0f : 0.0f);
    view.active = desktopEntryIndex(desktop_section_);
    if (view.active < 0) view.active = desktopEntryIndex("all"); // Its games are shown meanwhile.
    if (desktop_active_y_valid_) view.active_y = desktop_active_y_.value;
    if (desktop_drop_flash_ > 0.0f) {
        view.drop_flash_entry = desktopEntryIndex(desktop_drop_flash_id_);
        view.drop_flash = desktop_drop_flash_;
    }
    view.sidebar_scroll = desktop_sidebar_scroll_;
    view.refreshing = desktop_refresh_spin_ > 0.0f || scanning_ || pc_scanning_ || drm_scanning_ || steam_rescan_->load();
    view.refresh_angle = desktop_refresh_angle_;
    view.tools = desktopTools();
    const auto* entry = desktopEntry();
    view.title = entry ? entry->title : std::wstring(tr(L"Wszystkie gry"));
    view.title_mark = entry && entry->kind == EntryKind::studio ? text_encoding::wide(entry->id) : std::wstring{}; // Its logo instead.
    view.count = cards_.size();
    view.query = desktop_query_;
    view.caret = std::min(desktop_caret_, desktop_query_.size());
    view.search_focus = desktop_search_focus_;
    view.caret_on = (GetTickCount64() / 530) % 2 == 0;
    const bool recent = entry && entry->kind == EntryKind::recent;
    view.sort_label = recent ? std::wstring(tr(L"Ostatnio grane")) : std::wstring(library_view::label(library_view::orderFromPersisted(settings_.desktop_sort)));
    view.installed_only = settings_.desktop_installed;
    view.poster = static_cast<float>(settings_.desktop_poster);
    view.scroll = desktop_scroll_;
    view.scrollbar = desktop_scrollbar_;
    view.hover = desktop_hover_;
    view.pressed = desktop_pressed_;
    view.max_hover = desktop_max_hover_;
    view.card_hover = desktop_card_hover_;
    view.quick = desktop_quick_;
    view.meta = desktop_meta_;
    view.platform_badges = settings_.desktop_platform_badges;
    view.gray_uninstalled = settings_.desktop_gray_uninstalled;
    view.marked.assign(cards_.size(), 0);
    view.card_mark.assign(cards_.size(), 0.0f);
    for (size_t i = 0; i < cards_.size() && i < desktop_keys_.size(); ++i) {
        if (desktop_marked_.contains(desktop_keys_[i])) { view.marked[i] = 1; ++view.marked_count; }
        if (const auto found = desktop_mark_pulse_.find(desktop_keys_[i]); found != desktop_mark_pulse_.end())
            view.card_mark[i] = found->second;
    }
    view.keyboard = desktop_keyboard_;
    view.zone = desktop_zone_;
    view.pane_focus = desktop_pane_focus_;
    view.focus_row = desktop_zone_ == 0 ? desktop_sidebar_focus_ : -1;
    if (desktop_hover_.target == Target::sidebar_row) view.hover_row = desktop_hover_.index;
    view.drop_row = desktop_drag_.active ? desktop_drag_.drop_row : -1;
    if (desktop_drag_.active && desktop_drag_.kind == DesktopDrag::Kind::entry) { view.reorder_row = desktop_drag_.row; view.reorder_y = desktop_drag_.reorder_y; }
    view.pane_scroll = desktop_pane_scroll_;
    view.maximized = IsZoomed(hwnd_) != FALSE;
    view.active_window = desktop_window_active_;
    view.scanning = scanning_ || (cards_.empty() && libraryLoading());
    view.launch = launch_progress_;
    view.menu = desktop_menu_ ? &*desktop_menu_ : nullptr;
    view.downloads_open = desktop_downloads_open_;
    view.download_rows = desktop_download_rows_;
    for (const auto& game : desktop_download_games_) {
        view.download_names.push_back(game.name);
        view.download_actions.push_back(desktopOwnInstall(game) ? std::wstring(tr(L"Anuluj")) : std::wstring(tr(L"Otwórz")));
    }
    view.downloads_clearable = desktopDownloadsClearable();
    view.downloads_total = static_cast<int>(desktop_download_rows_.size());
    double share = 0; int shares = 0;
    for (const auto& row : desktop_download_rows_) {
        if (row.moving) ++view.downloads_moving;
        if (row.fraction) { share += *row.fraction; ++shares; }
    }
    view.downloads_fraction = shares ? static_cast<float>(share / shares) : -1.0f;
    view.update_progress = updateFraction();
    if (desktop_tooltip_ > 0.01f) {
        view.tooltip = desktopTooltip(desktop_hover_);
        view.tooltip_anchor = desktopTooltipAnchor(desktop_hover_);
        view.tooltip_alpha = desktop_tooltip_;
    }
    if (desktop_drag_.active && desktop_drag_.kind == DesktopDrag::Kind::card) {
        view.dragging = true;
        view.drag_x = desktop_drag_.x; view.drag_y = desktop_drag_.y;
        view.drag_card = desktop_drag_.card;
        view.drag_count = desktop_drag_.games.size();
        const auto rows = view.rows;
        if (desktop_drag_.drop_row >= 0 && desktop_drag_.drop_row < static_cast<int>(rows.size()) && rows[desktop_drag_.drop_row].entry >= 0) {
            view.drag_valid = true;
            view.drag_label = std::wstring(tr(L"Dodaj do „")) + desktop_entries_[static_cast<size_t>(rows[desktop_drag_.drop_row].entry)].title + L"”";
        } else view.drag_label = tr(L"Upuść na kolekcję lub Ulubione");
    }
    view.companion_card = settings_.companion && !settings_.desktop_narrow;
    view.sidebar_narrow = desktop_narrow_anim_;
    view.pane_width = static_cast<float>(settings_.desktop_pane);
    view.companion_bubble = settings_.companion && companion_.bubble.alpha > 0.003f && (companion_.bubble.waiting || !companion_.bubble.text.empty());
    // Empty grid: why, and what helps.
    if (cards_.empty() && !view.scanning) {
        auto& empty = view.empty;
        if (!trimmed(desktop_query_).empty()) {
            empty.icon = glyph::search;
            empty.title = std::wstring(tr(L"Brak wyników dla „")) + trimmed(desktop_query_) + L"”";
            empty.body = tr(L"Sprawdź pisownię albo wpisz gatunek lub tag.");
            empty.primary = tr(L"Wyczyść wyszukiwanie");
            if (desktop_section_ != "all") empty.secondary = tr(L"Szukaj we wszystkich grach");
        } else if (settings_.desktop_installed && !desktopSource().empty()) {
            empty.icon = glyph::download;
            empty.title = tr(L"Brak zainstalowanych gier");
            empty.body = tr(L"W tym widoku są tylko gry do zainstalowania.");
            empty.primary = tr(L"Pokaż wszystkie");
        } else if (entry && entry->kind == EntryKind::collection) {
            empty.icon = glyph::folder;
            empty.title = tr(L"Kolekcja jest pusta");
            empty.body = tr(L"Przeciągnij tu gry z biblioteki albo dodaj je w menu gry (prawy przycisk myszy).");
            empty.primary = tr(L"Wybierz gry");
        } else if (entry && entry->kind == EntryKind::favorites) {
            empty.icon = glyph::star;
            empty.title = tr(L"Brak ulubionych gier");
            empty.body = tr(L"Kliknij gwiazdkę na karcie gry albo przeciągnij tu jej plakat.");
        } else if (public_library_.empty()) {
            empty.icon = glyph::game;
            // Why it is empty: no Steam on this PC, Steam without a signed-in account, or nothing found.
            const auto reason = emptyLibraryReason();
            empty.title = empty_library::title(reason);
            empty.body = reason == empty_library::Reason::no_steam || reason == empty_library::Reason::no_account ? std::wstring(empty_library::body(reason)) :
                std::wstring(tr(L"Połącz sklepy w Ustawieniach albo dodaj grę z dysku — możesz też upuścić tu plik .exe."));
            empty.primary = tr(L"Dodaj grę");
            empty.secondary = tr(L"Ustawienia biblioteki");
            view.library_empty = true;
        } else {
            empty.icon = glyph::library;
            empty.title = tr(L"Brak gier w tym widoku");
        }
    }
    // The pane: the selected game, or the marked ones.
    auto& pane = view.pane;
    pane.marked = view.marked_count;
    if (cards_.empty() || view.marked_count > 1) return;
    const auto& game = cards_[selected_].game;
    pane.game = true;
    pane.name = game.name;
    pane.platform = activePlatformName(game);
    if (pane.platform.empty() && !game.version_group.empty()) pane.platform = tr(L"Kilka wersji");
    const auto session = sessionState(game);
    pane.running = session == SessionState::running;
    pane.frozen = session == SessionState::frozen;
    pane.installed = game.installed;
    pane.primary = actionLabel(game);
    pane.primary_enabled = actionEnabled(game);
    pane.launching = pending_launch_ && gameKey(pending_game_) == gameKey(game);
    if (game.installation.active()) {
        // The button becomes the installation: its share fills it, its state is its label.
        const auto percent = game.installation.percent();
        pane.progress = percent ? *percent / 100.0f : -1.0f; // -1: still unknown, the bar sweeps.
        pane.primary = installationText(game.installation);
        pane.status = tr(L"Instalacja w toku");
    } else pane.status = pane.frozen ? tr(L"Wstrzymana") : pane.running ? tr(L"W grze") :
                         game.installed ? tr(L"Zainstalowana") : localFileGame(game.source) ? missingFileLabel(game) : tr(L"Niezainstalowana");
    if (game.family_shared) pane.status += std::wstring(L" · ") + tr(L"Udostępniona rodzinnie"); // Borrowed from the account's Steam Family.
    pane.favorite = isFavorite(game);
    if (favorite_pulse_ > 0.0f && library_view::selectionKey(game) == favorite_pulse_key_) pane.favorite_pulse = favorite_pulse_;
    bool loading = false;
    pane.details = desktopDetails(game, loading);
    pane.details_loading = loading;
    if (pane.details.playtime_minutes && *pane.details.playtime_minutes) pane.playtime = formatPlaytime(*pane.details.playtime_minutes);
    else pane.playtime = steamArtwork(game) && loading ? L"…" : tr(L"Brak");
    pane.last_played = playedWhen(game.last_played);
    if (pane.details.achievements_total) {
        pane.achievements = std::to_wstring(pane.details.achievements_unlocked) + L" / " + std::to_wstring(pane.details.achievements_total);
        pane.achievements_share = std::clamp(static_cast<float>(pane.details.achievements_unlocked) / pane.details.achievements_total, 0.0f, 1.0f);
        pane.achievements_link = steamArtwork(game) || pluginAchievementsProvider(game);
    }
    pane.rating = ratingLabel();
    pane.protondb = protonDbLabel(game); // Linux only.
    for (const auto& c : settings_collections_) {
        if (favoriteCollection(c) || (srmCollectionsHidden() && srmCollection(c))) continue;
        if (desktopMember(game, c)) pane.collections.emplace_back(c.name, "collection:" + c.id);
    }
    pane.folder = !gameFolder(game).empty();
    pane.hideable = !hideableCopies(game).empty();
    pane.hidden = pane.hideable && hiddenInLauncher(game);
}

bool Launcher::desktopMember(const Game& game, const SteamCollection& collection) const {
    if (!game.version_group.empty()) if (const auto* copies = versions_.find(game))
        return std::any_of(copies->begin(), copies->end(), [&](const Game& copy) { return collectionContains(library_state_.collections, account_, collection, copy); });
    return collectionContains(library_state_.collections, account_, collection, game);
}

// ---- Persistence -----------------------------------------------------------------------------

void Launcher::desktopSaveState() {
    if (!desktop_mode_) return;
    const auto section = desktop_section_;
    const auto game = !cards_.empty() && selected_ < desktop_keys_.size() && desktopActive() ? desktop_keys_[selected_] : settings_.desktop_game;
    std::unordered_set<std::string> collapsed, expanded;
    constexpr const char* names[] = {"library", "platforms", "emulation", "collections", "publishers", "developers"};
    for (int i = 0; i < group_count; ++i) {
        const bool folded = desktop_collapsed_[static_cast<size_t>(i)];
        if (studioGroup(static_cast<Group>(i))) { if (!folded) expanded.insert(names[i]); } // Folded unless opened.
        else if (folded) collapsed.insert(names[i]);
    }
    if (settings_.desktop_section != section || settings_.desktop_game != game || settings_.desktop_collapsed != collapsed || settings_.desktop_expanded != expanded) {
        settings_.desktop_section = section;
        settings_.desktop_game = game;
        settings_.desktop_collapsed = std::move(collapsed);
        settings_.desktop_expanded = std::move(expanded);
        settings_dirty_ = true; settings_save_failed_ = false; settings_edited_ = std::chrono::steady_clock::now();
    }
}

std::unordered_map<std::string, Installation> Launcher::desktopInstallations() const {
    std::unordered_map<std::string, Installation> live;
    const auto add = [&](const Game& game, const Installation& install) {
        if (install.active() && install.phase != InstallPhase::unknown) live.emplace(gameKey(game), install);
    };
    for (const auto& game : installed_library_) if (game.source == GameSource::steam) add(game, game.installation);
    if (private_shelf_) for (const auto& game : private_category_.games) if (game.source == GameSource::steam) add(game, game.installation);
    for (const auto& game : store_library_) if (game.source == GameSource::battlenet) add(game, game.installation);
    if (!gog_install_ui_.game.name.empty() && gog_install_ui_.snapshot.busy()) {
        auto install = gog_install_ui_.snapshot.progress();
        if (gog_install_ui_.snapshot.state == gog::InstallState::loading) install.phase = InstallPhase::allocating; // Asking for the files.
        live[gameKey(gog_install_ui_.game)] = install;
    }
    return live;
}

void Launcher::desktopSyncInstallations() {
    if (!desktopActive() || cards_.empty()) return;
    const auto live = desktopInstallations();
    bool changed = false;
    for (auto& card : cards_) {
        auto& install = card.game.installation;
        const auto found = live.find(gameKey(card.game));
        if (found != live.end()) { if (install != found->second) { install = found->second; changed = true; } }
        else if (install.active() && install.phase != InstallPhase::unknown) { install = {}; changed = true; } // Over: the library brings its state.
    }
    if (changed) InvalidateRect(hwnd_, nullptr, FALSE);
}
