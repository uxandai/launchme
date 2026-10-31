// Metro home layout, after the 2011 Xbox 360 dashboard: a start page with the most recently
// played games and shortcuts, then one page per home category (stores, consoles and studios as
// logo tiles, other categories as posters after a "Wszystkie" tile). cards_ holds every page's
// tiles back to back (metro_pages_, metro_tiles_), so renderer slots, art binding and hit
// testing keep working per card index. category_ follows the focused page and each
// category's `selected` is the tile on it, so bookmarks, the category menu and returning from
// a library behave as on the other layouts. The tab strip above the pages takes the focus from
// a page's top row (metro_tabs_); the quick bar opens from its bottom row; there is no side
// category menu. Geometry: src/ui/metro_layout.hpp.
#include "launcher.hpp"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

using metro_layout::Role;

bool Launcher::metroActive() const {
    return settings_.home_layout == AppSettings::HomeLayout::metro && screen_ == ScreenMode::shelves && !desktop_mode_;
}

const Game* Launcher::metroInstallGame() const {
    const Game* newest = nullptr;
    uint64_t newest_seen = 0;
    for (const auto& game : public_library_) {
        if (!isGame(game) || game.visibility != GameVisibility::public_game) continue;
        if (game.installation.active()) return &game;
        if (!game.installed) continue;
        const auto added = library_view::addedTime(game, library_state_.first_seen);
        if (added > newest_seen) { newest_seen = added; newest = &game; }
    }
    return newest;
}

void Launcher::rebuildMetro(bool preserve_background) {
    const auto view = renderer_.viewport();
    // Rebuilt over the Metro pages still on screen (library refresh, installs): unchanged tiles
    // keep their decoded art, position and fade instead of entering again.
    const bool same_list = metro_slots_ == renderer_.slotGeneration() && metro_tiles_.size() == cards_.size();
    const auto tileKey = [](const std::string& page, Role role, const Game& game) {
        return page + '\n' + std::to_string(static_cast<int>(role)) + '\n' + library_view::selectionKey(game);
    };
    std::unordered_map<std::string, size_t> previous;
    std::vector<GameCard> old_cards;
    std::string old_focus;
    if (same_list) {
        for (size_t i = 0; i < cards_.size(); ++i)
            previous.emplace(tileKey(metro_pages_[metro_tiles_[i].page].id, metro_tiles_[i].role, cards_[i].game), i);
        if (selected_ < cards_.size())
            old_focus = tileKey(metro_pages_[metro_tiles_[selected_].page].id, metro_tiles_[selected_].role, cards_[selected_].game);
        old_cards = std::move(cards_);
    }
    std::vector<size_t> from;
    std::vector<MetroPage> pages;
    std::vector<MetroTile> tiles;
    cards_.clear();
    MetroPage* page = nullptr;
    const auto add = [&](const Game& game, Role role, metro_layout::Rect rect) {
        const auto old = same_list ? previous.find(tileKey(page->id, role, game)) : previous.end();
        if (old != previous.end()) {
            cards_.push_back(old_cards[old->second]); // Its animation state, with the fresh game data.
            cards_.back().game = game;
            from.push_back(old->second);
            previous.erase(old);
        } else { cards_.push_back(GameCard{game}); from.push_back(SIZE_MAX); }
        tiles.push_back({role, rect, static_cast<uint32_t>(pages.size() - 1)});
        ++page->count;
        page->right = std::max(page->right, rect.right);
    };
    const auto shortcut = [&](const char* id) -> std::optional<size_t> {
        for (size_t i = 1; i < categories_.size(); ++i) if (categories_[i].id == id && !categories_[i].games.empty()) return i;
        return std::nullopt;
    };
    // Start page: [2nd     ][   1st   ][Instalacja]
    //             [3rd     ][  game   ][ (poster, ]
    //             [Bib][Ust][PC ][Emu ][  tall)  ]
    // Towarzyszka and Glance (while on) take the PC and Emulacja cells; their pages stay in the tabs.
    if (!categories_.empty()) {
        pages.push_back({0, 0, 0, 0, 0, categories_[0].id});
        page = &pages.back();
        std::vector<const Game*> recent;
        for (const auto& game : categories_[0].games)
            if (isGame(game) && !game.installation.active() && recent.size() < 3) recent.push_back(&game);
        Game library;
        library.kind = GameKind::all;
        library.name = tr(L"Biblioteka");
        Game settings;
        settings.kind = GameKind::settings;
        settings.name = tr(L"Ustawienia");
        if (recent.empty()) {
            // Nothing played yet: Biblioteka takes the large tile.
            add(library, Role::library, metro_layout::cell(0, 1, 0, 2, 2));
            add(settings, Role::settings, metro_layout::cell(0, 0, 2));
        } else {
            add(*recent[0], Role::recent_large, metro_layout::cell(0, 1, 0, 2, 2));
            if (recent.size() > 1) add(*recent[1], Role::recent, metro_layout::cell(0, 0, 0));
            if (recent.size() > 2) add(*recent[2], Role::recent, metro_layout::cell(0, 0, 1));
            add(library, Role::library, metro_layout::half(0, 0, 2, 0));
            add(settings, Role::settings, metro_layout::half(0, 0, 2, 1));
        }
        // The bottom row's two middle cells: Towarzyszka, Glance, then the store and console
        // shortcuts, two at most; a single one spans both.
        std::vector<std::pair<Game, Role>> middle;
        if (settings_.companion) {
            Game tile;
            tile.kind = GameKind::companion;
            tile.name = companion::name(companionLook());
            middle.emplace_back(std::move(tile), Role::companion);
        }
        if (settings_.glance) {
            Game tile;
            tile.kind = GameKind::glance;
            tile.name = L"Glance";
            middle.emplace_back(std::move(tile), Role::glance);
        }
        for (const auto& [id, role] : {std::pair{"pc-platforms", Role::pc_platforms}, std::pair{"emulation", Role::emulation}})
            if (const auto category = shortcut(id); category && middle.size() < 2) {
                Game tile;
                tile.kind = GameKind::category; // app_id = category index, as the Flix "Wszystkie" tiles.
                tile.name = categories_[*category].title;
                tile.app_id = static_cast<uint32_t>(*category);
                tile.source_id = categories_[*category].id;
                middle.emplace_back(std::move(tile), role);
            }
        if (middle.size() > 2) middle.resize(2);
        for (size_t i = 0; i < middle.size(); ++i)
            add(middle[i].first, middle[i].second, metro_layout::cell(0, 1 + static_cast<int>(i), 2, middle.size() == 1 ? 2 : 1, 1));
        if (const auto* install = metroInstallGame();
            install && std::none_of(recent.begin(), recent.end(), [&](const Game* game) { return gameKey(*game) == gameKey(*install); }))
            add(*install, Role::install, metro_layout::cell(0, 3, 0, 1, 3));
    }
    // One page per further category, side by side.
    for (size_t index = 1; index < categories_.size(); ++index) {
        const auto& category = categories_[index];
        if (category.games.empty()) continue;
        const float origin = pages.back().right + metro_layout::page_gap;
        pages.push_back({index, cards_.size(), 0, origin, origin, category.id});
        page = &pages.back();
        // Logo pages: stores, consoles, publishers and developers, every one of them (columns of
        // three, the page as wide as it needs; its scroll follows the focus).
        if (category.id == "pc-platforms" || category.id == "emulation" || studioCategory(category.id)) {
            for (size_t i = 0; i < category.games.size(); ++i) add(category.games[i], Role::logo, metro_layout::logo(origin, i));
            continue;
        }
        size_t slot = 0;
        if (std::any_of(category.games.begin(), category.games.end(), [](const Game& game) { return isGame(game); })) {
            Game all;
            all.kind = GameKind::category;
            all.name = tr(L"Wszystkie");
            all.app_id = static_cast<uint32_t>(index);
            all.source_id = category.id;
            add(all, Role::all, metro_layout::poster(origin, slot++));
        }
        for (const auto& game : category.games) {
            if (slot >= metro_layout::category_tiles) break; // The rest: "Wszystkie".
            add(game, Role::game, metro_layout::poster(origin, slot++));
        }
    }
    metro_pages_ = std::move(pages);
    metro_tiles_ = std::move(tiles);
    if (same_list) renderer_.remapCovers(from, preserve_background);
    else { renderer_.resetCovers(cards_.size(), preserve_background); arrived_ = 0; }
    metro_slots_ = renderer_.slotGeneration();
    if (cards_.empty()) { selected_ = 0; metro_page_ = 0; detail_ = 0; InvalidateRect(hwnd_, nullptr, FALSE); return; }
    // Focus follows category_: a restored bookmark, the way back from a library, a new library.
    size_t page_index = 0;
    for (size_t i = 0; i < metro_pages_.size(); ++i) if (metro_pages_[i].category == category_) { page_index = i; break; }
    const auto& shown = metro_pages_[page_index];
    category_ = shown.category;
    categories_[category_].selected = std::min(categories_[category_].selected, shown.count - 1);
    selected_ = shown.begin + categories_[category_].selected;
    if (!same_list || metro_page_ != page_index) metro_scroll_ = shown.left;
    metro_scroll_ = metro_layout::reveal(metro_scroll_, shown.left, metro_tiles_[selected_].rect, view.width);
    if (!same_list) { metro_tab_ = static_cast<float>(page_index); metro_tabs_ = false; }
    metro_page_ = page_index;
    if (old_focus != tileKey(shown.id, metro_tiles_[selected_].role, cards_[selected_].game)) detail_ = 0;
    flixFocusArt(); // The focused game's background and logo; a "Wszystkie" tile: its category's.
    requestVisibleArt();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::metroFocus(size_t index) {
    if (index >= cards_.size() || index >= metro_tiles_.size()) return;
    metro_tabs_ = false; // A tile takes the focus back from the tab strip.
    const auto& tile = metro_tiles_[index];
    const auto& page = metro_pages_[tile.page];
    if (tile.page != metro_page_) metro_scroll_ = page.left; // A new page starts at its left edge.
    metro_page_ = tile.page;
    category_ = page.category;
    categories_[category_].selected = index - page.begin;
    selected_ = index;
    metro_scroll_ = metro_layout::reveal(metro_scroll_, page.left, tile.rect, renderer_.viewport().width);
    detail_ = 0;
    flixFocusArt();
    requestVisibleArt();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::metroMove(int dx, int dy, bool edge_menu) {
    if (cards_.empty() || selected_ >= metro_tiles_.size() || (!dx && !dy)) return;
    if (metro_tabs_) { // Tabs: Left/Right change the page, Down returns to its tile.
        if (dy > 0) { metroFocusTabs(false); audio_.navigate(); }
        else if (dy < 0) navigationBoundary();
        else homePage(dx);
        return;
    }
    const auto& here = metro_tiles_[selected_];
    const auto& page = metro_pages_[here.page];
    size_t best = SIZE_MAX;
    float best_score = 0;
    for (size_t i = page.begin; i < page.begin + page.count; ++i) {
        if (i == selected_) continue;
        const float score = metro_layout::score(here.rect, metro_tiles_[i].rect, dx, dy);
        if (score >= 0 && (best == SIZE_MAX || score < best_score)) { best = i; best_score = score; }
    }
    // Past the page edge: the nearest tile of the neighbouring page's first (or last) column.
    if (best == SIZE_MAX && dx && (dx > 0 ? here.page + 1 < metro_pages_.size() : here.page > 0)) {
        const auto& next = metro_pages_[dx > 0 ? here.page + 1 : here.page - 1];
        for (size_t i = next.begin; i < next.begin + next.count; ++i) {
            const auto& rect = metro_tiles_[i].rect;
            const bool edge = dx > 0 ? rect.left <= next.left + 1 : rect.right >= next.right - 1;
            if (!edge) continue;
            const float score = std::abs(rect.centerY() - here.rect.centerY());
            if (best == SIZE_MAX || score < best_score) { best = i; best_score = score; }
        }
    }
    if (best == SIZE_MAX) {
        // Up from the top row: the tabs; Down from the bottom row: the quick bar. Left at the start does nothing.
        if (edge_menu && dy < 0) { metroFocusTabs(true); audio_.navigate(); return; }
        if (edge_menu && dy > 0) { openQuick(); return; }
        navigationBoundary();
        return;
    }
    metroFocus(best);
    audio_.navigate();
}

void Launcher::homePage(int amount) {
    if (psfiveActive() && amount && !pending_launch_ && !friends_open_ && !category_menu_open_ && !quick_open_) {
        // PSfive: LB/RB, Page Up/Down change the tab (the top bar keeps its focus on it).
        const auto tabs = psfiveTabs();
        if (tabs.empty()) { navigationBoundary(); return; }
        const int current = psfiveActiveTab();
        const int next = std::clamp(current < 0 ? 0 : current + amount, 0, static_cast<int>(tabs.size()) - 1);
        if (next == current) { navigationBoundary(); return; }
        psfiveSelectTab(next, psfive_top_ >= 0 && psfive_top_ < static_cast<int>(tabs.size()));
        return;
    }
    if (!metroActive() || metro_pages_.empty() || !amount || pending_launch_ || friends_open_ || category_menu_open_ || quick_open_) return;
    const int next = std::clamp(static_cast<int>(metro_page_) + amount, 0, static_cast<int>(metro_pages_.size()) - 1);
    if (next == static_cast<int>(metro_page_)) { navigationBoundary(); return; }
    dismissMenu();
    const bool tabs = metro_tabs_; // Changing pages from the tab strip keeps the focus there.
    const auto& page = metro_pages_[static_cast<size_t>(next)];
    metroFocus(page.begin + std::min(categories_[page.category].selected, page.count - 1)); // Each page remembers its tile.
    metro_tabs_ = tabs;
    audio_.navigate();
}

void Launcher::metroFocusTabs(bool focus) {
    metro_tabs_ = focus && !cards_.empty();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

bool Launcher::metroActivate() {
    if (!metroActive() || selected_ >= metro_tiles_.size()) return false;
    const auto& game = cards_[selected_].game;
    switch (metro_tiles_[selected_].role) {
    case Role::pc_platforms:
    case Role::emulation:
        for (const auto& page : metro_pages_)
            if (page.category == game.app_id) {
                metroFocus(page.begin + std::min(categories_[page.category].selected, page.count - 1));
                audio_.navigate();
                break;
            }
        return true;
    case Role::all: openCategoryLibrary(game.source_id); return true;
    case Role::glance: openGlance(); return true;
    case Role::companion: openCompanion(); return true;
    default: return false;
    }
}

void Launcher::metroClick(size_t index, bool double_click) {
    if (index >= cards_.size() || index >= metro_tiles_.size()) return;
    const bool moved = index != selected_;
    if (moved) { metroFocus(index); audio_.navigate(); }
    const auto game = cards_[index].game;
    // Shortcuts open with one click, games (launch, install) with a double click.
    if (metroActivate()) return;
    if (game.kind == GameKind::all) { openLibrary(); return; }
    if (game.kind == GameKind::settings) { openSettings(); return; }
    if (game.kind == GameKind::platform) { openPlatform(game.platform); return; }
    if (game.kind == GameKind::pc_platform) { openPcLibrary(game.source); return; }
    if (!moved && double_click) accept();
}

void Launcher::metroTarget(size_t index, float& x, float& y, float& size, float& aspect, float& delay) const {
    if (index >= metro_tiles_.size()) return;
    const auto& tile = metro_tiles_[index];
    const auto& page = metro_pages_[tile.page];
    const float top = metro_layout::top(renderer_.viewport().height) - (quick_open_ ? metro_layout::quick_lift : 0.0f);
    const float shift = metro_layout::left - metro_scroll_;
    metro_layout::Rect screen{tile.rect.left + shift, tile.rect.top + top, tile.rect.right + shift, tile.rect.bottom + top};
    // "Efekt 3D": the other pages stand back at 80%, shrunk towards their edge facing the focused
    // page; moving onto a page brings it forward (the card easing animates both).
    if (settings_.metro_3d && tile.page != metro_page_)
        screen = metro_layout::shrink(screen, (tile.page > metro_page_ ? page.left : page.right) + shift,
                                      top + metro_layout::grid_height * 0.5f, metro_layout::depth_scale);
    x = screen.left;
    y = screen.top;
    size = screen.height();
    aspect = screen.width() / std::max(1.0f, screen.height());
    // Tiles enter column by column from the left of their page.
    delay = std::min(0.4f, 0.035f * (tile.rect.left - page.left) / metro_layout::cell_w + 0.02f * tile.rect.top / metro_layout::cell_h);
}

void Launcher::bindMetroArt(size_t index, bool replace) {
    if (index >= cards_.size() || index >= metro_tiles_.size()) return;
    const auto role = metro_tiles_[index].role;
    const auto& game = cards_[index].game;
    if (metro_layout::systemTile(role)) return; // Drawn: accent tiles with a glyph.
    if (role == Role::all) {
        // A category's "Wszystkie": the background of a random game of the category as its picture.
        if (!replace && renderer_.hasPoster(index)) return;
        const auto* backdrop = flixBackdrop(game.app_id);
        if (!backdrop) return;
        if (const auto* found = art_.find(gameKey(*backdrop)); found && found->hero) renderer_.setPoster(index, found->hero);
        else requestArt(*backdrop, CoverMessage::Kind::hero);
        return;
    }
    if (gameKey(game).empty()) return;
    const auto* found = art_.find(gameKey(game));
    const auto cover = [&] {
        if (found && found->cover) {
            if (renderer_.hasCover(index)) return;
            cards_[index].cover_opacity = 0;
            if (!isGame(game)) renderer_.setPlatformCover(index, found->cover);
            else renderer_.setCover(index, found->cover, found->focus);
        } else requestArt(game, CoverMessage::Kind::cover);
    };
    if (!isGame(game)) { cover(); return; } // Store and console logos.
    const bool wide = metro_layout::landscape(role);
    const ImageBytes shaped = found ? (wide ? found->landscape : found->poster) : ImageBytes{};
    if (shaped) {
        if (replace || !renderer_.hasPoster(index)) renderer_.setPoster(index, shaped);
        return;
    }
    requestArt(game, wide ? CoverMessage::Kind::landscape : CoverMessage::Kind::poster);
    // Until (or unless) it arrives: the wide background for a wide tile, the cover otherwise.
    if (wide && found && found->hero) { if (!renderer_.hasPoster(index)) renderer_.setPoster(index, found->hero); return; }
    if (wide) requestArt(game, CoverMessage::Kind::hero);
    cover();
}

void Launcher::metroRequestArt() {
    // The focused page and its neighbours; the others give their bitmaps back.
    std::vector<char> nearby(cards_.size(), 0);
    for (size_t p = 0; p < metro_pages_.size(); ++p) {
        if (p + 1 < metro_page_ || p > metro_page_ + 1) continue;
        const auto& page = metro_pages_[p];
        for (size_t i = page.begin; i < page.begin + page.count && i < cards_.size(); ++i) { nearby[i] = 1; bindMetroArt(i); }
    }
    for (size_t i = 0; i < cards_.size(); ++i)
        if (!nearby[i] && i != selected_) { renderer_.releaseCover(i); renderer_.releasePoster(i); }
}

void Launcher::metroArtArrived(const std::string& key, CoverMessage::Kind kind) {
    if (!metroActive()) return;
    for (size_t i = 0; i < cards_.size() && i < metro_tiles_.size(); ++i) {
        const auto role = metro_tiles_[i].role;
        if (role == Role::all) {
            // A "Wszystkie" tile whose category background just arrived (and the page behind it).
            if (kind != CoverMessage::Kind::hero) continue;
            const auto chosen = flix_backdrops_.find(cards_[i].game.source_id);
            if (chosen == flix_backdrops_.end() || chosen->second != key) continue;
            bindMetroArt(i, true);
            if (i == selected_) if (const auto* found = art_.find(key); found && found->hero) { renderer_.setHero(i, found->hero, found->hero_tone, false, found->hero_focus); detail_ = 0; }
            continue;
        }
        if (gameKey(cards_[i].game) != key) continue;
        if (kind == CoverMessage::Kind::poster || kind == CoverMessage::Kind::landscape) bindMetroArt(i, true);
        else if (kind == CoverMessage::Kind::hero && metro_layout::landscape(role)) bindMetroArt(i); // A wide tile's stand-in.
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}
