// Flix home layout: the recently played games as landscape capsules, then one row of
// vertical posters per category. cards_ holds every row's games back to back (flix_rows_),
// so renderer slots, art binding and hit testing keep working per card index. category_
// follows the focused row and each category's `selected` is its position in that row, so
// bookmarks, the category menu and returning from a platform behave as on the shelves.
#include "launcher.hpp"
#include "../ui/flix_layout.hpp"

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace {
float tileScale(const AppSettings& settings) { return flix_layout::sizeScale(static_cast<int>(settings.flix_size)); }
}

bool Launcher::flixActive() const {
    return settings_.home_layout == AppSettings::HomeLayout::flix && screen_ == ScreenMode::shelves && !desktop_mode_;
}

void Launcher::rebuildFlix(bool preserve_background) {
    flixRailInset(); // Pasek boczny: the rows start right of it.
    const auto view = renderer_.viewport();
    const float scale = tileScale(settings_);
    std::unordered_map<std::string, size_t> scrolled; // A library refresh keeps each row's scroll.
    for (const auto& row : flix_rows_) scrolled.emplace(row.id, row.first);
    // Rebuilt over the Flix list still on screen (library refresh, installs, a PC rescan): tiles
    // that did not change keep their decoded art, position and fade instead of entering again.
    const bool same_list = flix_slots_ == renderer_.slotGeneration() && flix_card_rows_.size() == cards_.size();
    const auto tileKey = [&](size_t row, const Game& game) { return flix_rows_[row].id + '\n' + library_view::selectionKey(game); };
    std::unordered_map<std::string, size_t> previous;
    std::vector<GameCard> old_cards;
    std::string old_focus;
    if (same_list) {
        for (size_t i = 0; i < cards_.size(); ++i) previous.emplace(tileKey(flix_card_rows_[i], cards_[i].game), i);
        if (selected_ < cards_.size()) old_focus = tileKey(flix_card_rows_[selected_], cards_[selected_].game);
        old_cards = std::move(cards_);
    }
    std::vector<size_t> from;
    std::vector<FlixRow> rows;
    std::vector<uint32_t> card_rows;
    cards_.clear();
    const auto key = [&](const FlixRow& row, const Game& game) { return row.id + '\n' + library_view::selectionKey(game); };
    for (size_t index = 0; index < categories_.size(); ++index) {
        const auto& category = categories_[index];
        if (category.games.empty()) continue;
        const bool landscape = rows.empty();
        // Optional first tile of a game row: the whole category (not Recent, stores or consoles).
        // The Ctrl+H shelf too: while it is shown, the library lists its hidden and private games.
        const bool all_tile = settings_.flix_category_all && !landscape && category.id != "pc-platforms" && category.id != "emulation" &&
            std::any_of(category.games.begin(), category.games.end(), [](const Game& game) { return isGame(game); });
        FlixRow row{index, cards_.size(), category.games.size() + (all_tile ? 1 : 0), 0, category.id};
        const auto scroll = scrolled.find(row.id);
        row.first = flix_layout::firstVisible(scroll == scrolled.end() ? 0 : scroll->second,
                                              std::min(category.selected, row.count - 1), row.count, landscape, view.width, scale);
        const auto add = [&](const Game& game) {
            const auto old = same_list ? previous.find(key(row, game)) : previous.end();
            if (old != previous.end()) {
                cards_.push_back(old_cards[old->second]); // Its animation state, with the fresh game data.
                cards_.back().game = game;
                from.push_back(old->second);
                previous.erase(old);
            } else { cards_.push_back(GameCard{game}); from.push_back(SIZE_MAX); }
            card_rows.push_back(static_cast<uint32_t>(rows.size()));
        };
        if (all_tile) {
            Game all;
            all.kind = GameKind::category; // app_id = category index, as XMB collection tiles.
            all.name = tr(L"Wszystkie");
            all.app_id = static_cast<uint32_t>(index);
            all.source_id = category.id;
            add(all);
        }
        for (const auto& game : category.games) add(game);
        rows.push_back(std::move(row));
    }
    flix_rows_ = std::move(rows);
    flix_card_rows_ = std::move(card_rows);
    flix_unfold_index_ = SIZE_MAX; flix_unfold_ = 0; // Card indexes change: a new rest unfolds again.
    if (same_list) renderer_.remapCovers(from, preserve_background);
    else { renderer_.resetCovers(cards_.size(), preserve_background); arrived_ = 0; }
    flix_slots_ = renderer_.slotGeneration();
    if (cards_.empty()) { selected_ = 0; flix_row_ = 0; detail_ = 0; InvalidateRect(hwnd_, nullptr, FALSE); return; }
    // Focus follows category_: a restored bookmark, the way back from a platform, a new library.
    flix_row_ = 0;
    for (size_t i = 0; i < flix_rows_.size(); ++i) if (flix_rows_[i].category == category_) { flix_row_ = i; break; }
    const auto& row = flix_rows_[flix_row_];
    category_ = row.category;
    categories_[category_].selected = std::min(categories_[category_].selected, row.count - 1);
    selected_ = row.begin + categories_[category_].selected;
    if (old_focus != key(row, cards_[selected_].game)) detail_ = 0; // The same focus keeps its settled background.
    flixFocusArt();
    requestVisibleArt();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::flixFocusArt() {
    if (selected_ >= cards_.size()) return;
    const auto& game = cards_[selected_].game;
    // A row's "Wszystkie" tile shows its category's background behind the page.
    const Game* source = game.kind == GameKind::category ? flixBackdrop(game.app_id) : &game;
    if (!source) return;
    if (const auto* found = art_.find(gameKey(*source))) {
        if (found->logo && source == &game) renderer_.setLogo(selected_, found->logo);
        if (found->hero) renderer_.setHero(selected_, found->hero, found->hero_tone, false, found->hero_focus);
    } else if (source != &game) requestArt(*source, CoverMessage::Kind::hero);
}

const Game* Launcher::flixBackdrop(size_t category) {
    if (category >= categories_.size()) return nullptr;
    const auto& games = categories_[category].games;
    auto& chosen = flix_backdrops_[categories_[category].id];
    const auto usable = [&](const Game& game) { return isGame(game) && !artBlocked(game, CoverMessage::Kind::hero); };
    // Kept for the session while the game stays in the category and its background loads.
    for (const auto& game : games) if (!chosen.empty() && gameKey(game) == chosen && usable(game)) return &game;
    std::vector<const Game*> candidates, ready;
    for (const auto& game : games) if (usable(game)) candidates.push_back(&game);
    if (candidates.empty()) { chosen.clear(); return nullptr; }
    // Preferably one whose background is already in memory: the tile shows it at once.
    for (const auto* game : candidates) if (const auto* art = art_.peek(gameKey(*game)); art && art->hero) ready.push_back(game);
    const auto& pool = ready.empty() ? candidates : ready;
    const auto* pick = pool[std::uniform_int_distribution<size_t>(0, pool.size() - 1)(flix_random_)];
    chosen = gameKey(*pick);
    return pick;
}


void Launcher::flixFocus(size_t row, size_t column) {
    if (flix_rows_.empty()) return;
    flix_rail_focus_ = -1; // A tile takes the focus back from the side icons.
    row = std::min(row, flix_rows_.size() - 1);
    auto& info = flix_rows_[row];
    column = std::min(column, info.count - 1);
    info.first = flix_layout::firstVisible(info.first, column, info.count, row == 0, renderer_.viewport().width, tileScale(settings_));
    flix_row_ = row;
    category_ = info.category;
    categories_[category_].selected = column;
    selected_ = info.begin + column;
    detail_ = 0;
    flix_focus_at_ = std::chrono::steady_clock::now(); // The rest before the tile unfolds starts again.
    flixFocusArt();
    requestVisibleArt();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::flixFocusCard(size_t index) {
    if (index >= cards_.size() || index >= flix_card_rows_.size()) return;
    const size_t row = flix_card_rows_[index];
    flixFocus(row, index - flix_rows_[row].begin);
}

void Launcher::flixHorizontal(int amount) {
    if (flix_rows_.empty() || !amount) return;
    const auto& row = flix_rows_[flix_row_];
    const int column = static_cast<int>(selected_ - row.begin);
    // Like the shelves: Left at the start of a row lists the categories to jump to; with Pasek boczny,
    // its icons take the focus (the row in view's own first).
    if (amount < 0 && column == 0) {
        if (flixRailShown()) { const auto items = flixRailItems(); flixRailFocus(std::max(0, flixRailActive(items))); return; }
        openCategoryMenu(); return;
    }
    const int next = std::clamp(column + amount, 0, static_cast<int>(row.count) - 1);
    if (next == column) { navigationBoundary(); return; }
    flixFocus(flix_row_, static_cast<size_t>(next));
    audio_.navigate();
}

void Launcher::flixVertical(int amount, bool edge_menu) {
    if (flix_rows_.empty() || !amount) return;
    if (amount < 0 && flix_row_ == 0) { if (edge_menu) openQuick(); return; }
    const auto next = static_cast<size_t>(std::clamp(static_cast<int>(flix_row_) + amount, 0, static_cast<int>(flix_rows_.size()) - 1));
    if (next == flix_row_) { navigationBoundary(); return; }
    flixFocus(next, categories_[flix_rows_[next].category].selected); // Each row remembers its position.
    audio_.navigate();
}

void Launcher::flixClick(size_t index, bool double_click) {
    if (index >= cards_.size()) return;
    const bool moved = index != selected_;
    if (moved) { flixFocusCard(index); audio_.navigate(); }
    const auto game = cards_[index].game;
    if (game.kind == GameKind::all) { openLibrary(); return; }
    if (specialTile(game)) { launch(); return; } // Glance, Tarot, Towarzyszka, Dodaj grę as tiles: one click.
    if (game.kind == GameKind::category) { openCategoryLibrary(game.source_id); return; }
    if (game.kind == GameKind::platform) { openPlatform(game.platform); return; }
    if (game.kind == GameKind::pc_platform) { openPcLibrary(game.source); return; }
    if (!moved && double_click) accept();
}

void Launcher::flixTarget(size_t index, float& x, float& y, float& size, float& aspect, float& delay) const {
    if (index >= flix_card_rows_.size()) return;
    const size_t row_index = flix_card_rows_[index];
    const auto& row = flix_rows_[row_index];
    const bool landscape = row_index == 0;
    const size_t column = index - row.begin;
    const float scale = tileScale(settings_);
    size = flix_layout::tileHeight(landscape, scale);
    aspect = flix_layout::aspect(landscape);
    x = flix_layout::tileLeft(landscape, column, row.first, scale);
    y = flix_layout::rowTop(row_index, flix_row_, renderer_.viewport().height, scale) + flix_layout::header -
        (quick_open_ ? flix_layout::quick_lift : 0.0f);
    const size_t shown = column > row.first ? column - row.first : 0;
    delay = 0.06f * static_cast<float>(std::min<size_t>(row_index, 4)) + 0.03f * static_cast<float>(std::min<size_t>(shown, 8));
    // The unfolded tile widens to both sides about its centre, pushing the tiles before it left and
    // those after it right, as Netflix does; the first tile in view grows only to the right. The row
    // moves left when the wide card would pass the right margin.
    if (landscape || flix_unfold_ <= 0.0005f || flix_unfold_index_ >= flix_card_rows_.size() || flix_card_rows_[flix_unfold_index_] != row_index) return;
    const float t = std::clamp(flix_unfold_, 0.0f, 1.0f), opened = t * t * (3 - 2 * t);
    const float extra = flix_layout::unfoldExtra(scale) * opened;
    const size_t unfolded = flix_unfold_index_ - row.begin;
    const float before = unfolded > row.first ? extra * 0.5f : 0.0f, after = extra - before;
    const float right = renderer_.viewport().width - flix_layout::right_margin;
    const float card_right = flix_layout::tileLeft(false, unfolded, row.first, scale) + flix_layout::tileWidth(false, scale) + after;
    x -= std::max(0.0f, card_right - right);
    if (column < unfolded) x -= before;
    else if (column > unfolded) x += after;
    else { x -= before; aspect = (flix_layout::tileWidth(false, scale) + extra) / size; }
}

bool Launcher::flixUnfoldReady(std::chrono::steady_clock::time_point now) const {
    if (!flixActive() || flix_row_ == 0 || flix_row_ >= flix_rows_.size() || selected_ >= cards_.size() ||
        selected_ >= flix_card_rows_.size() || flix_card_rows_[selected_] != flix_row_) return false;
    const auto& game = cards_[selected_].game;
    if (!isGame(game) || game.kind == GameKind::catalog) return false; // Store and console tiles, "Wszystkie".
    if (menu_open_ || quick_open_ || category_menu_open_ || friends_open_ || pending_launch_ || launchAnimationActive() || flixRailFocused() ||
        topModal() != Modal::none || screensaverActive() || !windowWatched()) return false;
    const auto rest = std::chrono::duration<float>(flix_layout::unfold_rest);
    return now - flix_focus_at_ >= rest && now - last_input_ >= rest;
}

bool Launcher::tickFlixUnfold(std::chrono::steady_clock::time_point now, float dt) {
    const bool ready = flixUnfoldReady(now);
    // A new focus: the previous card folds back before this one opens.
    if (ready && flix_unfold_index_ != selected_ && flix_unfold_ < 0.002f) {
        flix_unfold_index_ = selected_;
        if (runtime_started_) preview_.request(cards_[selected_].game, account_); // Its year, playtime and genres for the card.
    }
    const float target = ready && flix_unfold_index_ == selected_ ? 1.0f : 0.0f;
    if (flix_unfold_ == target) {
        if (!target) flix_unfold_index_ = SIZE_MAX;
        return false;
    }
    const float speed = target > flix_unfold_ ? 4.5f : 14.0f; // Opens slowly, folds back at once.
    flix_unfold_ += (target - flix_unfold_) * (1.0f - std::exp(-dt * speed));
    if (std::abs(target - flix_unfold_) < 0.002f) flix_unfold_ = target;
    return true;
}

void Launcher::bindFlixArt(size_t index, bool replace) {
    if (index >= cards_.size() || index >= flix_card_rows_.size()) return;
    const auto& game = cards_[index].game;
    if (game.kind == GameKind::category) {
        // A row's "Wszystkie": the background of a random game of the category as its picture.
        if (!replace && renderer_.hasPoster(index)) return;
        const auto* backdrop = flixBackdrop(game.app_id);
        if (!backdrop) return;
        if (const auto* found = art_.find(gameKey(*backdrop)); found && found->hero) renderer_.setPoster(index, found->hero);
        else requestArt(*backdrop, CoverMessage::Kind::hero);
        return;
    }
    if (gameKey(game).empty()) return; // "Wszystkie gry" is drawn, not loaded.
    const auto* found = art_.find(gameKey(game));
    const auto cover = [&] {
        if (found && found->cover) {
            if (renderer_.hasCover(index)) return;
            cards_[index].cover_opacity = 0;
            if (!isGame(game)) renderer_.setPlatformCover(index, found->cover);
            else renderer_.setCover(index, found->cover, found->focus);
        } else requestArt(game, CoverMessage::Kind::cover);
    };
    if (!isGame(game)) { cover(); return; } // Store and console tiles show their logo.
    const bool landscape = flix_card_rows_[index] == 0;
    const ImageBytes shaped = found ? (landscape ? found->landscape : found->poster) : ImageBytes{};
    if (shaped) {
        if (replace || !renderer_.hasPoster(index)) renderer_.setPoster(index, shaped);
        return;
    }
    requestArt(game, landscape ? CoverMessage::Kind::landscape : CoverMessage::Kind::poster);
    // Until (or unless) it arrives: the wide background for a capsule, the square cover for a poster.
    if (landscape && found && found->hero) { if (!renderer_.hasPoster(index)) renderer_.setPoster(index, found->hero); return; }
    if (landscape) requestArt(game, CoverMessage::Kind::hero);
    cover();
}

void Launcher::flixRequestArt() {
    const auto view = renderer_.viewport();
    const float scale = tileScale(settings_);
    std::vector<char> nearby(cards_.size(), 0);
    for (size_t r = 0; r < flix_rows_.size(); ++r) {
        const auto& row = flix_rows_[r];
        const bool landscape = r == 0;
        const float top = flix_layout::rowTop(r, flix_row_, view.height, scale);
        // The rows on screen and one more above and below; their visible tiles and two past each edge.
        if (top > view.height + flix_layout::rowHeight(false, scale) || top + 2 * flix_layout::rowHeight(landscape, scale) < 0) continue;
        const size_t first = row.first > 2 ? row.first - 2 : 0;
        const size_t end = std::min(row.count, row.first + flix_layout::visibleCount(landscape, view.width, scale) + 3);
        for (size_t column = first; column < end; ++column) {
            nearby[row.begin + column] = 1;
            bindFlixArt(row.begin + column);
        }
    }
    // Far away cards give their bitmaps back; they are decoded again from RAM/disk on return.
    for (size_t i = 0; i < cards_.size(); ++i)
        if (!nearby[i] && i != selected_) { renderer_.releaseCover(i); renderer_.releasePoster(i); }
}
