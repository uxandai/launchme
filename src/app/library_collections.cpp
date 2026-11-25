// Siatka's "Kolekcje jako kafle" (Opcje widoku): the collections as tiles above the games, as a console's
// game library shows its lists, with the shown games' count and order under them (collection_band in
// src/ui/grid_layout.hpp; drawn by src/ui/collection_band_renderer.cpp). Up from the first row reaches
// them, Left/Right walk them, A opens one as the library's tab of that collection (Back returns here).
#include "launcher.hpp"
#include "launcher_internal.hpp"
#include "../library/collections.hpp"
#include "../library/organization.hpp"
#include "../ui/grid_layout.hpp"

#include <algorithm>

// The option is on, in the library's grid of every game or of a shelf (not a store's, a console's or the
// studios'), without the search field.
bool Launcher::libraryBandShown() const {
    const auto& options = library_controls_.options;
    return screen_ == ScreenMode::library && options.view == library_view::View::grid && options.grid_collections && !pc_source_ &&
        library_console_.empty() && !studioBoard() && !library_controls_.search && !desktopActive();
}

bool Launcher::libraryBandFocused() const {
    return band_focus_ >= 0 && libraryBandShown() && library_controls_.focus < 0 && !library_controls_.open();
}

// The user's collections in their order (those kept off the library's tabs and SRM's left out), each
// with its games and the first four of them for the tile's covers.
std::vector<Launcher::BandTile> Launcher::libraryBandTiles() const {
    std::vector<BandTile> tiles;
    for (const auto& collection : settings_collections_) {
        if ((srmCollectionsHidden() && srmCollection(collection)) || !homeRowTab(collection.id)) continue;
        const auto* category = findCategory(collection.id);
        if (!category) continue;
        BandTile tile{collection.id, collection.name, 0, {}};
        for (const auto& game : category->games) {
            if (!isGame(game)) continue;
            if (tile.count < tile.games.size()) tile.games[tile.count] = &game;
            ++tile.count;
        }
        if (tile.count) tiles.push_back(std::move(tile));
    }
    return tiles;
}

int Launcher::libraryBandHit(float x, float y) const {
    if (!libraryBandShown() || library_controls_.open() || menu_open_ || friends_open_ || quick_open_ || y < 150) return -1;
    const auto view = renderer_.viewport();
    const auto& options = library_controls_.options;
    return collection_band::hit(x, y, static_cast<int>(libraryBandTiles().size()), view.width, band_scroll_, band_slide_, options);
}

namespace {
// The row's slide that shows tile `focus` (the tick eases towards it).
float bandSlide(int focus, int count, float width, const library_view::Options& options) {
    return collection_band::slideFor(focus, count, width, options);
}
}

// Up from the first row (or Down from the top bar): the tile last focused.
void Launcher::libraryBandFocus(int index) {
    const int count = static_cast<int>(libraryBandTiles().size());
    if (!count) { if (!cards_.empty()) focusCard(selected_); return; }
    library_controls_.focus = library_controls_.action = -1;
    band_focus_ = band_last_ = std::clamp(index, 0, count - 1);
    band_slide_target_ = bandSlide(band_focus_, count, renderer_.viewport().width, library_controls_.options);
    grid_scroll_ = 0;
    audio_.navigate();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::libraryBandMove(int amount) {
    const int count = static_cast<int>(libraryBandTiles().size());
    const int next = std::clamp(band_focus_ + amount, 0, std::max(0, count - 1));
    if (next == band_focus_) { navigationBoundary(); return; }
    band_focus_ = band_last_ = next;
    band_slide_target_ = bandSlide(next, count, renderer_.viewport().width, library_controls_.options);
    audio_.navigate();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

// A tile opens its collection as the library's tab, in the same view; Back comes back to the tile.
void Launcher::libraryBandOpen(int index) {
    const auto tiles = libraryBandTiles();
    if (index < 0 || index >= static_cast<int>(tiles.size())) return;
    const auto id = tiles[static_cast<size_t>(index)].id;
    band_last_ = index;
    band_focus_ = -1;
    if (!showLibraryCategory(id, library_controls_.options.view)) return;
    band_return_ = id;
}

bool Launcher::libraryBandReturn() {
    if (screen_ != ScreenMode::library || band_return_.empty() || library_category_ != band_return_ || library_controls_.open() ||
        library_controls_.search) return false;
    const auto id = std::exchange(band_return_, {});
    if (!showLibraryCategory({}, library_controls_.options.view)) return false;
    if (libraryBandShown()) {
        const auto tiles = libraryBandTiles();
        const auto found = std::find_if(tiles.begin(), tiles.end(), [&](const BandTile& tile) { return tile.id == id; });
        band_focus_ = band_last_ = found != tiles.end() ? static_cast<int>(found - tiles.begin()) : std::min(band_last_, static_cast<int>(tiles.size()) - 1);
        if (band_focus_ < 0) band_focus_ = -1;
        band_slide_target_ = band_slide_ = bandSlide(std::max(0, band_last_), static_cast<int>(tiles.size()), renderer_.viewport().width,
                                                     library_controls_.options); // Back where it was, without sliding.
        grid_scroll_ = 0;
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
    return true;
}

namespace {
// The grid's art (Opcje widoku: Okładki, Plakaty, Kapsuły): the tiles' covers take its shape.
CoverMessage::Kind bandKind(const library_view::Options& options) {
    return options.grid_landscape ? CoverMessage::Kind::landscape : options.grid_posters ? CoverMessage::Kind::poster : CoverMessage::Kind::cover;
}
}

// The tiles' covers: from memory, else asked for (the art of games off screen in the grid).
void Launcher::requestBandArt() {
    if (!libraryBandShown()) return;
    const auto kind = bandKind(library_controls_.options);
    for (const auto& tile : libraryBandTiles())
        for (const auto* game : tile.games) {
            if (!game) continue;
            const auto* found = art_.find(gameKey(*game));
            const bool have = found && (kind == CoverMessage::Kind::landscape ? !!found->landscape : kind == CoverMessage::Kind::poster ? !!found->poster : !!found->cover);
            if (!have) requestArt(*game, kind);
        }
}

// What the renderer draws: the tiles (name, count, covers), the focus, the line under them.
void Launcher::libraryBandContext(ViewContext& context) {
    if (!libraryBandShown()) { band_slide_target_ = 0; return; }
    context.band = true;
    const auto tiles = libraryBandTiles();
    const auto kind = bandKind(library_controls_.options);
    for (const auto& tile : tiles) {
        ViewContext::BandTile view{tile.name, gamesLabel(tile.count), {}, tile.count};
        for (size_t i = 0; i < tile.games.size(); ++i) {
            if (!tile.games[i]) continue;
            const auto* found = art_.find(gameKey(*tile.games[i]));
            if (!found) continue;
            // The grid's art; the square cover stands in until it comes.
            const auto& wanted = kind == CoverMessage::Kind::landscape ? found->landscape : kind == CoverMessage::Kind::poster ? found->poster : found->cover;
            view.covers[i] = wanted ? wanted : found->cover ? found->cover : found->poster;
        }
        context.band_tiles.push_back(std::move(view));
    }
    if (band_focus_ >= static_cast<int>(tiles.size())) band_focus_ = tiles.empty() ? -1 : static_cast<int>(tiles.size()) - 1;
    context.band_focus = libraryBandFocused() ? band_focus_ : -1;
    context.band_hover = band_hover_ < static_cast<int>(tiles.size()) ? band_hover_ : -1;
    const auto view = renderer_.viewport();
    const auto& options = library_controls_.options;
    band_slide_target_ = bandSlide(context.band_focus >= 0 ? context.band_focus : band_last_, static_cast<int>(tiles.size()), view.width, options);
    context.band_slide = band_slide_;
    const auto tabs = libraryTabs();
    const auto& tab = tabs.empty() ? std::wstring(tr(L"Wszystkie gry")) : tabs[static_cast<size_t>(libraryTabIndex(tabs))].title;
    context.band_summary = tab + L": " + std::to_wstring(cards_.size());
    context.band_order = std::wstring(tr(L"Sortuj")) + L": " + library_view::label(options.order);
    if (const int filters = library_view::activeFilters(options) + static_cast<int>(library_rules_.size())) context.band_order += L"  ·  " + std::wstring(tr(L"Filtry")) + L": " + std::to_wstring(filters);
}
