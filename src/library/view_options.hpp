#pragma once
#include "../model/game.hpp"
#include <map>
#include <unordered_map>
#include <vector>

namespace library_view {
// Indexed like the filter menu rows. Recently played first: it is the default. release: newest
// release first, by the game index's dates (undated games last).
enum class Order { last_played, alphabetical, added, release };
constexpr int order_count = 4;
// library-state.ini keeps the numbers of the original four orders (0 was "Domyślnie", the
// unsorted source order, now read as Recently played) so older files load unchanged; 4 is release.
inline int persistedOrder(Order order) { return order == Order::alphabetical ? 1 : order == Order::added ? 3 : order == Order::release ? 4 : 2; }
inline Order orderFromPersisted(int value) {
    return value == 1 ? Order::alphabetical : value == 3 ? Order::added : value == 4 ? Order::release : Order::last_played;
}
constexpr int persisted_order_count = 5;
// The numbers are what library-state.ini keeps: new views are appended, removed ones leave their
// number unused (4 was Plakat, 8 Kartridż NES; viewFromPersisted reads them as their nearest view).
// overview: the focused game in a panel on the left (poster, facts, description) beside a grid of
// landscape tiles; with cinematic it has the category tab strip (LB/RB) at the top.
// bookshelf (Półka): Cover Flow in perspective — the focused poster facing front in the middle above
// a floor reflection, its title, year and developer under it, the others turned on both sides.
enum class View { grid = 0, list = 1, cinematic = 2, detailed = 3, coverflow = 5, floppy = 6, cartridge_switch = 7, overview = 9, bookshelf = 10, list_plus = 11 };
constexpr int view_count = 10;           // Views offered (drawer rows).
constexpr int persisted_view_count = 12; // Saved numbers 0..11, including the removed ones.
inline View viewFromPersisted(int value) {
    return value == 4 ? View::list_plus : value == 8 ? View::cartridge_switch : static_cast<View>(value);
}
// The view drawer lists them in this order (the numbers above are only what is saved).
inline constexpr View drawer_order[view_count] = {View::grid, View::list, View::list_plus, View::cinematic, View::overview, View::detailed,
                                                  View::coverflow, View::bookshelf, View::floppy, View::cartridge_switch};
inline int drawerRow(View view) {
    for (int i = 0; i < view_count; ++i) if (drawer_order[i] == view) return i;
    return 0;
}
inline View drawerView(int row) { return drawer_order[row < 0 ? 0 : row >= view_count ? view_count - 1 : row]; }
inline bool media(View view) { return view == View::floppy || view == View::cartridge_switch; }
inline bool carousel(View view) { return view == View::coverflow || view == View::bookshelf || media(view); }
inline bool posterArt(View view) { return view == View::list_plus || view == View::coverflow || view == View::bookshelf || view == View::cartridge_switch; }
// Półka, Coverflow and the media carousels show the focused game's year, developer and playtime under its cover.
inline bool metadata(View view) { return view == View::list || view == View::list_plus || view == View::detailed || view == View::overview || carousel(view); }
inline bool rows(View view) { return view == View::list || view == View::list_plus || view == View::detailed; }
inline bool landscape(View view) { return view == View::cinematic || view == View::overview; } // 920:430 capsules.
inline bool tabs(View view) { return view == View::cinematic || view == View::overview; }    // Category strip, LB/RB.
inline bool preview(View view) { return metadata(view) || view == View::cinematic || carousel(view); }
inline bool pinnedHeader(View view) { return rows(view) || view == View::cinematic || view == View::overview || carousel(view); }
inline bool actions(View view) { return view == View::detailed; }
struct Options {
    Order order = Order::last_played;
    bool installed_only = false;
    View view = View::grid;
    bool grid_posters = false;
    bool grid_landscape = false; // Siatka: 920:430 landscape tiles (never together with grid_posters).
    bool list_year = true, list_playtime = true, list_rating = true;
    // Kinowy's scrolling: down the grid (both off), sideways in one row, or one column on the right
    // beside the game's logo, description and facts.
    bool cinematic_horizontal = false;
    bool cinematic_column = false;
    // Kinowy (sideways and column): the platform, last played and playtime beside year and genres.
    bool cinematic_platform = false, cinematic_played = false, cinematic_playtime = false;
    int overview_columns = 5;
    bool coverflow_cd = false;
    bool list_plus_steelbook = false; // Lista+: the poster turns into Coverflow's steelbook (no disc).
    bool bookshelf_gray = true;       // Półka: the cases beside the middle one lose their colour.
    bool bookshelf_platform = true;   // Półka: the platform's logo on a band of its colour atop the spine.
    bool operator==(const Options&) const = default;
};
inline bool metadata(const Options& options) {
    return metadata(options.view) || (options.view == View::cinematic && (options.cinematic_horizontal || options.cinematic_column));
}
inline bool posterArt(const Options& options) {
    return options.view == View::grid ? options.grid_posters && !options.grid_landscape :
           options.view == View::coverflow ? !options.coverflow_cd : posterArt(options.view);
}
// Kinowy's scrolling option: 0 down, 1 sideways, 2 one column.
inline int cinematicScroll(const Options& options) { return options.cinematic_column ? 2 : options.cinematic_horizontal ? 1 : 0; }
inline void setCinematicScroll(Options& options, int scroll) {
    scroll = ((scroll % 3) + 3) % 3;
    options.cinematic_horizontal = scroll == 1; options.cinematic_column = scroll == 2;
}
// Siatka's artwork: 0 square covers, 1 posters, 2 landscape tiles.
inline int gridArt(const Options& options) { return options.grid_landscape ? 2 : options.grid_posters ? 1 : 0; }
inline void setGridArt(Options& options, int art) {
    art = ((art % 3) + 3) % 3;
    options.grid_posters = art == 1; options.grid_landscape = art == 2;
}
inline const wchar_t* label(Order order) {
    switch (order) {
    case Order::alphabetical: return tr(L"Alfabetycznie");
    case Order::added: return tr(L"Ostatnio dodane");
    case Order::release: return tr(L"Data wydania");
    default: return tr(L"Ostatnio grane");
    }
}
inline const wchar_t* label(View view) {
    switch (view) {
    case View::list: return tr(L"Lista");
    case View::list_plus: return tr(L"Lista+");
    case View::cinematic: return tr(L"Kinowy");
    case View::detailed: return tr(L"Szczegółowy");
    case View::coverflow: return tr(L"Coverflow");
    case View::floppy: return tr(L"Dyskietka");
    case View::cartridge_switch: return tr(L"Kartridż Switch");
    case View::overview: return tr(L"Przegląd");
    case View::bookshelf: return tr(L"Półka");
    default: return tr(L"Siatka");
    }
}
// Prefer the store's acquisition date, otherwise the durable first detection in Launchme.
inline uint64_t addedTime(const Game& game, const std::map<std::string, uint64_t>& first_seen) {
    if (game.added_at) return game.added_at;
    const auto found = first_seen.find(gameKey(game));
    return found == first_seen.end() ? 0 : found->second;
}
// Hidden and private games are dropped unless `include_private` (Ctrl+H for this session).
// `released`: yyyymmdd by gameKey for Order::release (game_index::Date::sortKey; missing = undated).
std::vector<Game> arrange(const std::vector<Game>& games, Options options, const std::map<std::string, uint64_t>& first_seen,
                          bool include_private = false, const std::unordered_map<std::string, int>* released = nullptr);
inline std::string selectionKey(const Game& game) {
    if (!game.version_group.empty()) return "versions:" + game.version_group;
    if (game.kind == GameKind::all) return "tile:all";
    if (game.kind == GameKind::back) return "tile:back";
    if (game.kind == GameKind::settings) return "tile:settings";
    if (game.kind == GameKind::glance) return "tile:glance";
    if (game.kind == GameKind::companion) return "tile:companion";
    if (game.kind == GameKind::tarot) return "tile:tarot";
    if (game.kind == GameKind::add) return "tile:add";
    if (game.kind == GameKind::category) return "tile:category:" + std::to_string(game.app_id);
    return gameKey(game);
}
}
