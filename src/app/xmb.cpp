#include "launcher.hpp"
#include "../library/view_options.hpp"
#include "../ui/xmb_layout.hpp"
#include "../ui/category_rail.hpp"

#include <algorithm>
#include <unordered_map>

// XMB home layout. cards_ holds the top-row tiles first (xmb_top_), then the games of the
// focused tile's column, so renderer resources, art binding and hit testing keep working
// per card index. A column change only replaces the tail and keeps the row's bitmaps.
// Glance and Towarzyszka are tiles of the row, right after the recent games (launch() opens them).

bool Launcher::xmbActive() const {
    return settings_.home_layout == AppSettings::HomeLayout::xmb && screen_ == ScreenMode::shelves && !desktop_mode_;
}

const std::vector<Game>* Launcher::xmbColumn(const Game& tile) const {
    if (tile.kind == GameKind::category && tile.app_id < categories_.size() && !categories_[tile.app_id].games.empty())
        return &categories_[tile.app_id].games;
    if (tile.kind == GameKind::platform) {
        if (const auto* games = consoleGames(tile.platform); games && !games->empty()) return games;
    }
    return nullptr;
}

std::string Launcher::xmbKey(const Game& tile) const {
    if (tile.kind == GameKind::category && tile.app_id < categories_.size()) return "category:" + categories_[tile.app_id].id;
    return library_view::selectionKey(tile);
}

void Launcher::rebuildXmb(bool keep_top, bool preserve_background) {
    keep_top = keep_top && !xmb_top_.empty() && cards_.size() >= xmb_top_.size();
    std::string row_key;
    if (!keep_top) {
        // A library refresh may reorder rows: follow the focused tile and game by identity.
        const auto column_key = xmb_column_ < xmb_top_.size() ? xmbKey(xmb_top_[xmb_column_]) : std::string{};
        if (xmb_row_ >= 0 && selected_ < cards_.size() && selected_ >= xmb_top_.size()) row_key = gameKey(cards_[selected_].game);
        category_ = 0;
        xmb_top_.clear();
        std::vector<Game> collections, stores, consoles, hidden;
        // Glance and Towarzyszka (while on) right after the recent games, before "Wszystkie gry".
        std::vector<Game> widgets;
        if (settings_.glance) { Game tile; tile.kind = GameKind::glance; tile.name = L"Glance"; widgets.push_back(std::move(tile)); }
        if (settings_.companion) {
            Game tile;
            tile.kind = GameKind::companion;
            tile.name = companion::name(companionLook());
            widgets.push_back(std::move(tile));
        }
        // Kafle Wszystkie, Glance…: they are in the recent row already, first or last (homeSpecialTiles).
        const bool special_row = settings_.home_tiles != AppSettings::HomeTiles::layout;
        if (special_row) widgets.clear();
        for (size_t i = 0; i < categories_.size(); ++i) {
            const auto& category = categories_[i];
            if (category.id == "recent" && special_row) xmb_top_.insert(xmb_top_.end(), category.games.begin(), category.games.end());
            else if (category.id == "recent") {
                const auto games = std::find_if(category.games.begin(), category.games.end(), [](const Game& game) { return !isGame(game); });
                xmb_top_.insert(xmb_top_.end(), category.games.begin(), games);
                xmb_top_.insert(xmb_top_.end(), widgets.begin(), widgets.end());
                xmb_top_.insert(xmb_top_.end(), games, category.games.end());
                widgets.clear();
            } else if (category.id == "pc-platforms") stores = category.games;
            else if (category.id == "emulation") consoles = category.games;
            else if (!category.games.empty()) {
                Game tile;
                tile.name = category.title;
                tile.kind = GameKind::category;
                tile.app_id = static_cast<uint32_t>(i);
                // The Ctrl+H shelf comes last, after the consoles (just before Settings).
                (category.id == "private" ? hidden : collections).push_back(std::move(tile));
            }
        }
        xmb_top_.insert(xmb_top_.begin(), widgets.begin(), widgets.end()); // No recent row yet: they lead.
        for (auto* group : {&collections, &stores, &consoles, &hidden}) xmb_top_.insert(xmb_top_.end(), group->begin(), group->end());
        Game settings;
        settings.name = tr(L"Ustawienia");
        settings.kind = GameKind::settings;
        xmb_top_.push_back(std::move(settings));
        xmb_column_ = std::min(xmb_column_, xmb_top_.size() - 1);
        if (!column_key.empty())
            for (size_t i = 0; i < xmb_top_.size(); ++i) if (xmbKey(xmb_top_[i]) == column_key) { xmb_column_ = i; break; }
    }
    const size_t top = xmb_top_.size();
    const auto* column = xmbColumn(xmb_top_[xmb_column_]);
    const int count = column ? static_cast<int>(column->size()) : 0;
    if (!row_key.empty() && column) {
        const auto found = std::find_if(column->begin(), column->end(), [&](const Game& game) { return gameKey(game) == row_key; });
        if (found != column->end()) xmb_row_ = static_cast<int>(found - column->begin());
    }
    xmb_row_ = count ? std::clamp(xmb_row_, -1, count - 1) : -1;
    // Rebuilt over the XMB list still on screen (the startup Steam rescan, the PC scan, resolved
    // names, the restored bookmark, installs): tiles that did not change keep their decoded art,
    // position and fade instead of the whole row and column entering again.
    const bool same_list = !keep_top && xmb_slots_ == renderer_.slotGeneration() && xmb_card_keys_.size() == cards_.size();
    std::unordered_map<std::string, size_t> previous;
    std::vector<GameCard> old_cards;
    std::string old_focus;
    if (same_list) {
        for (size_t i = 0; i < cards_.size(); ++i) previous.emplace(xmb_card_keys_[i], i);
        if (selected_ < cards_.size()) old_focus = xmb_card_keys_[selected_];
        old_cards = std::move(cards_);
    }
    const auto identity = [&](const Game& game) {
        // Version groups: the shown copy can change under the same group key.
        return isGame(game) ? library_view::selectionKey(game) + '\n' + gameKey(game) : xmbKey(game);
    };
    const auto column_prefix = "column\n" + xmbKey(xmb_top_[xmb_column_]) + '\n';
    std::vector<std::string> keys;
    std::vector<size_t> from;
    const auto add = [&](const Game& game, std::string key) {
        const auto old = same_list ? previous.find(key) : previous.end();
        if (old != previous.end()) {
            cards_.push_back(old_cards[old->second]); // Its animation state, with the fresh game data.
            cards_.back().game = game;
            from.push_back(old->second);
            previous.erase(old);
        } else { cards_.push_back(GameCard{game}); from.push_back(SIZE_MAX); }
        keys.push_back(std::move(key));
    };
    if (keep_top) {
        cards_.resize(top);
        for (const auto& tile : xmb_top_) keys.push_back("top\n" + identity(tile));
    } else {
        cards_.clear();
        for (const auto& tile : xmb_top_) add(tile, "top\n" + identity(tile));
    }
    if (column) for (const auto& game : *column) add(game, column_prefix + identity(game));
    selected_ = xmb_row_ < 0 ? xmb_column_ : top + static_cast<size_t>(xmb_row_);
    if (keep_top) renderer_.keepCovers(top, cards_.size());
    else {
        if (same_list) renderer_.remapCovers(from, preserve_background);
        else { renderer_.resetCovers(cards_.size(), preserve_background); arrived_ = 0; }
        for (size_t i = 0; i < top; ++i) {
            if (from[i] != SIZE_MAX) continue; // Kept tile: its slot came along with its bitmap.
            const auto* found = art_.find(gameKey(cards_[i].game));
            if (!found || !found->cover) continue;
            cards_[i].cover_opacity = 1;
            if (cards_[i].game.kind == GameKind::platform || cards_[i].game.kind == GameKind::pc_platform) renderer_.setPlatformCover(i, found->cover);
            else renderer_.setCover(i, found->cover, found->focus);
        }
    }
    xmb_card_keys_ = std::move(keys);
    xmb_slots_ = renderer_.slotGeneration();
    // The same focus keeps its settled logo and labels.
    if (!same_list || old_focus != xmb_card_keys_[selected_]) detail_ = 0;
    if (const auto* found = art_.find(gameKey(cards_[selected_].game))) {
        if (found->logo) renderer_.setLogo(selected_, found->logo);
        if (found->hero) renderer_.setHero(selected_, found->hero, found->hero_tone, false, found->hero_focus);
    }
    requestVisibleArt();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::xmbFocus(size_t column, int row) {
    if (xmb_top_.empty()) return;
    column = std::min(column, xmb_top_.size() - 1);
    if (column != xmb_column_) {
        xmb_column_ = column;
        xmb_row_ = row;
        rebuildXmb(true);
        return;
    }
    const auto* games = xmbColumn(xmb_top_[column]);
    xmb_row_ = games ? std::clamp(row, -1, static_cast<int>(games->size()) - 1) : -1;
    if (xmb_row_ >= 0) xmb_rows_[xmbKey(xmb_top_[column])] = xmb_row_;
    const size_t next = xmb_row_ < 0 ? column : xmb_top_.size() + static_cast<size_t>(xmb_row_);
    // A library refresh focusing the same tile again keeps its settled logo and labels.
    if (next != selected_) detail_ = 0;
    selected_ = next;
    if (const auto* found = art_.find(gameKey(cards_[selected_].game))) {
        if (found->logo) renderer_.setLogo(selected_, found->logo);
        if (found->hero) renderer_.setHero(selected_, found->hero, found->hero_tone, false, found->hero_focus);
    }
    requestVisibleArt();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::xmbFocusCard(size_t index) {
    if (index < xmb_top_.size()) xmbFocus(index, -1);
    else xmbFocus(xmb_column_, static_cast<int>(index - xmb_top_.size()));
}

void Launcher::xmbHorizontal(int amount) {
    if (xmb_top_.empty()) return;
    const auto next = static_cast<size_t>(std::clamp(static_cast<int>(xmb_column_) + amount, 0, static_cast<int>(xmb_top_.size()) - 1));
    if (next == xmb_column_) { navigationBoundary(); return; }
    // Along the tile row focus stays on tiles; inside lists it lands on the neighbour's remembered game.
    int row = -1;
    if (xmb_row_ >= 0 && xmbColumn(xmb_top_[next])) {
        const auto found = xmb_rows_.find(xmbKey(xmb_top_[next]));
        row = found == xmb_rows_.end() ? 0 : found->second;
    }
    xmbFocus(next, row);
    audio_.navigate();
}

void Launcher::xmbVertical(int amount, bool edge_menu) {
    if (xmb_top_.empty() || !amount) return;
    if (amount < 0 && xmb_row_ < 0) { if (edge_menu) openQuick(); return; }
    const auto* games = xmbColumn(xmb_top_[xmb_column_]);
    if (!games) return;
    const int next = std::clamp(xmb_row_ + amount, -1, static_cast<int>(games->size()) - 1);
    if (next == xmb_row_) { navigationBoundary(); return; }
    xmbFocus(xmb_column_, next);
    audio_.navigate();
}

void Launcher::wheel(int amount, float mouse_x) {
    if (glanceOpen() && !friends_open_) { glanceMove(0, amount); return; }
    if (screen_ == ScreenMode::plugin_page && !friends_open_) { pluginPageMove(amount); return; }
    if (downloadsOpen() && !friends_open_ && !gogInstallOpen()) { if (amount > 0 || selected_ > 0) downloadsMove(amount); return; } // The wheel stays on the rows.
    if (identifyOpen()) { identifyMove(amount); return; }
    if (psfiveDetailsShown() && !menu_open_) { psfiveDetailsVertical(amount); return; } // PSfive's details: between their sections.
    // An open filter menu or view drawer scrolls its rows, never picks one.
    if (libraryControls() && library_controls_.open()) { libraryVertical(amount); return; }
    if (achievementsOpen()) { if (amount > 0 || achievements_.selected > 0) achievementsMove(amount); return; } // The wheel stays on the list.
    if (categoryRailActive() && !pending_launch_ && !friends_open_ && !quick_open_) {
        const auto view = renderer_.viewport();
        const bool over_rail = mouse_x >= 0 ? mouse_x / view.scale < category_rail::Layout{view.width, view.height}.contentClip() : category_rail_focus_;
        if (over_rail) { focusCategoryRail(true); changeCategory(amount); return; }
        focusCategoryRail(false);
    }
    if (flixActive() && !cards_.empty() && !pending_launch_ && !friends_open_ && !category_menu_open_ && !quick_open_) {
        dismissMenu(); flixVertical(amount, false); return; // Rows, like the page scroll.
    }
    if (metroActive()) { homePage(amount); return; } // Metro: a page per notch.
    if (!xmbActive() || cards_.empty() || pending_launch_ || friends_open_ || category_menu_open_ || quick_open_) {
        navigate(amount, false);
        return;
    }
    dismissMenu();
    // Inside a column the wheel scrolls its games; on the tile row it scrolls tiles.
    if (xmb_row_ >= 0) xmbVertical(amount, false); else xmbHorizontal(amount);
}

void Launcher::xmbClick(size_t index, bool double_click) {
    const size_t top = xmb_top_.size();
    if (index < top) {
        if (index != xmb_column_ || xmb_row_ >= 0) { xmbFocus(index, -1); audio_.navigate(); return; }
        // Focused tile: games need a double click like elsewhere, other tiles act at once.
        if (!isGame(xmb_top_[index])) launch(); else if (double_click) accept();
        return;
    }
    const int row = static_cast<int>(index - top);
    if (row == xmb_row_) { if (double_click) accept(); return; }
    xmbFocus(xmb_column_, row);
    audio_.navigate();
}

void Launcher::xmbTarget(size_t index, float& x, float& y, float& size, float& aspect, float& delay) const {
    using namespace xmb_layout;
    const size_t top = xmb_top_.size();
    const bool poster = settings_.xmb_posters && isGame(cards_[index].game);
    const float scale = sizeScale(static_cast<int>(settings_.xmb_size));
    aspect = poster ? poster_aspect : 1.0f;
    if (index < top) {
        const int offset = static_cast<int>(index) - static_cast<int>(xmb_column_);
        size = artHeight(tileSize(offset == 0, scale), poster);
        x = tileLeft(offset, scale);
        y = tileTop(size);
        delay = std::min(std::abs(offset), 8) * 0.045f;
        return;
    }
    const int row = static_cast<int>(index - top);
    const float width = itemSize(row == xmb_row_, scale);
    size = artHeight(width, poster);
    x = itemLeft(width, scale);
    y = itemTop(row, xmb_row_, settings_.xmb_posters, scale);
    delay = 0.12f + std::min(row, 8) * 0.04f;
}

void Launcher::xmbBindThumbnails(size_t tile) {
    // Also the default layout's "Wszystkie" tile (a category's games as its 2x2 preview).
    if (tile >= cards_.size() || cards_[tile].game.kind != GameKind::category) return;
    const auto* games = xmbColumn(cards_[tile].game);
    if (!games) return;
    for (int slot = 0; slot < 4 && slot < static_cast<int>(games->size()); ++slot) {
        const auto& game = (*games)[slot];
        const auto* found = art_.find(gameKey(game));
        if (!found || !found->cover) requestArt(game, CoverMessage::Kind::cover);
        // Same bytes are kept as they are; a kept tile whose category changed shows its new games.
        else renderer_.setTileThumbnail(tile, slot, found->cover, found->focus);
    }
}

void Launcher::bindXmbArt(size_t index, bool replace) {
    if (index >= cards_.size()) return;
    const auto& game = cards_[index].game;
    const auto* found = art_.find(gameKey(game));
    if (settings_.xmb_posters && isGame(game)) {
        if (found && found->poster) {
            if (replace || !renderer_.hasPoster(index)) renderer_.setPoster(index, found->poster);
            return;
        }
        requestArt(game, CoverMessage::Kind::poster);
    }
    // Square cover while the poster is unavailable; icons keep their usual artwork.
    if (found && found->cover) {
        if (renderer_.hasCover(index)) return;
        cards_[index].cover_opacity = 0;
        if (game.kind == GameKind::platform || game.kind == GameKind::pc_platform) renderer_.setPlatformCover(index, found->cover);
        else renderer_.setCover(index, found->cover, found->focus);
    } else requestArt(game, CoverMessage::Kind::cover);
}

void Launcher::xmbRequestArt() {
    const size_t top = std::min(xmb_top_.size(), cards_.size());
    const float scale = xmb_layout::sizeScale(static_cast<int>(settings_.xmb_size));
    const auto view = renderer_.viewport();
    // Tiles left of the anchor scroll off quickly; the right side shows roughly width / tile pitch.
    const size_t first = xmb_column_ > 2 ? xmb_column_ - 2 : 0;
    const size_t last = std::min(top, xmb_column_ + static_cast<size_t>(view.width / ((xmb_layout::tile + xmb_layout::gap) * scale)) + 2);
    for (size_t i = first; i < last; ++i) {
        if (cards_[i].game.kind == GameKind::category) xmbBindThumbnails(i);
        else if (!gameKey(cards_[i].game).empty()) bindXmbArt(i);
    }
    const int rows = static_cast<int>(cards_.size() - top);
    const int begin = xmb_layout::firstVisibleRow(xmb_row_);
    const int end = std::min(rows, xmb_layout::lastVisibleRow(xmb_row_, view.height, settings_.xmb_posters, scale));
    for (int row = begin; row < end; ++row) bindXmbArt(top + static_cast<size_t>(row));
    for (size_t i = 0; i < cards_.size(); ++i) {
        const bool nearby = i < top ? i >= first && i < last : i >= top + begin && i < top + end;
        if (!nearby && i != selected_) renderer_.releasePoster(i);
    }
}
