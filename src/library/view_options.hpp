#pragma once
#include "../model/game.hpp"
#include <algorithm>
#include <bit>
#include <cstdint>
#include <map>
#include <unordered_map>
#include <vector>

namespace library_view {
// Indexed like the filter menu rows. Recently played first: it is the default. release: newest
// release first, by the game index's dates (undated games last).
// installed: Ostatnio zainstalowane (when the game's folder was made); playtime: Najdłużej grane.
// rating: Najwyżej oceniane (game_index::ratingScore); shortest / longest: HowLongToBeat's main story.
// Games the index has no value for come last, by title.
enum class Order { last_played, alphabetical, added, release, installed, playtime, rating, shortest, longest };
constexpr int order_count = 9;
// library-state.ini keeps the numbers of the original four orders (0 was "Domyślnie", the
// unsorted source order, now read as Recently played) so older files load unchanged; 4 is release,
// 5 installed, 6 playtime, 7 rating, 8 shortest, 9 longest.
inline int persistedOrder(Order order) {
    return order == Order::alphabetical ? 1 : order == Order::added ? 3 : order == Order::release ? 4 :
           order == Order::installed ? 5 : order == Order::playtime ? 6 : order == Order::rating ? 7 :
           order == Order::shortest ? 8 : order == Order::longest ? 9 : 2;
}
inline Order orderFromPersisted(int value) {
    return value == 1 ? Order::alphabetical : value == 3 ? Order::added : value == 4 ? Order::release :
           value == 5 ? Order::installed : value == 6 ? Order::playtime : value == 7 ? Order::rating :
           value == 8 ? Order::shortest : value == 9 ? Order::longest : Order::last_played;
}
constexpr int persisted_order_count = 10;
// Orders sorted by a value the launcher computes per game (arrange's `values`), largest first.
inline bool valueOrder(Order order) {
    return order == Order::installed || order == Order::playtime || order == Order::rating || order == Order::shortest || order == Order::longest;
}
// Filtry (beside the orders in the same menu): how a game is played, read from its tags in the game index
// (Steam's store tags and features, RAWG's tags), and whether it was ever played. A game must match
// every one checked; one the index knows nothing about matches none. game_pass: played through the
// Xbox Game Pass subscription only; owned (Kupione): every other game. Both are the game's own facts.
enum class Filter { single_player, multiplayer, coop, local, online, controller, vr, unplayed, game_pass, owned };
constexpr int filter_count = 10;
constexpr uint32_t filterBit(Filter filter) { return 1u << static_cast<int>(filter); }
// Whether these tags say the game is played that way (Filter::unplayed: never; the caller knows).
bool tagged(Filter filter, const std::vector<std::wstring>& tags);
// The numbers are what library-state.ini keeps: new views are appended, removed ones leave their
// number unused (4 was Plakat, 6 Dyskietka, 7 Kartridż Switch, 8 Kartridż NES; viewFromPersisted reads
// them as their nearest view: Plakat as Lista+, the media carousels as Półka).
// overview: the focused game in a panel on the left (poster, facts, description) beside a grid of
// landscape tiles; with cinematic it has the category tab strip (LB/RB) at the top.
// bookshelf (Półka): Coverflow's first variant (its "Wariant" row: Półka / Steelbook / Piracka płyta),
// a view of its own inside (its own perspective geometry, saved as 10) but no row of its own in the
// drawer: Cover Flow in perspective, the focused game case facing front in the middle above a floor
// reflection, the others turned on both sides. Enter opens the case (game_case_view.hpp).
// stage (Scena): the titles alone in a narrow column on the left; the focused game over the rest of the
// screen as on PSfive's home: its background, logo, Graj / Instaluj with "…", and badges (playtime,
// achievements, platform).
enum class View { grid = 0, list = 1, cinematic = 2, detailed = 3, coverflow = 5, overview = 9, bookshelf = 10, list_plus = 11, stage = 12 };
constexpr int view_count = 8;            // Views offered (drawer rows).
constexpr int persisted_view_count = 13; // Saved numbers 0..12, including the removed ones.
inline View viewFromPersisted(int value) {
    return value == 4 ? View::list_plus : value >= 6 && value <= 8 ? View::bookshelf : static_cast<View>(value);
}
// The view drawer lists them in this order (the numbers above are only what is saved). Coverflow's
// row starts on Półka; its Wariant row turns it into the flat Steelbook or Piracka płyta carousel.
inline constexpr View drawer_order[view_count] = {View::grid, View::list, View::list_plus, View::cinematic, View::overview, View::detailed,
                                                  View::stage, View::coverflow};
inline int drawerRow(View view) {
    if (view == View::bookshelf) view = View::coverflow;
    for (int i = 0; i < view_count; ++i) if (drawer_order[i] == view) return i;
    return 0;
}
inline View drawerView(int row) {
    const auto view = drawer_order[row < 0 ? 0 : row >= view_count ? view_count - 1 : row];
    return view == View::coverflow ? View::bookshelf : view;
}
// Coverflow's variants, in its Wariant row: 0 Półka, 1 Steelbook, 2 Piracka płyta.
constexpr int coverflow_variants = 3;
inline bool coverflowFamily(View view) { return view == View::coverflow || view == View::bookshelf; }
inline bool carousel(View view) { return coverflowFamily(view); }
inline bool posterArt(View view) { return view == View::list_plus || view == View::coverflow || view == View::bookshelf; }
// Półka and Coverflow show the focused game's year, developer and playtime under its cover.
inline bool metadata(View view) { return view == View::list || view == View::list_plus || view == View::detailed || view == View::stage || view == View::overview || carousel(view); }
inline bool rows(View view) { return view == View::list || view == View::list_plus || view == View::detailed || view == View::stage; }
inline bool landscape(View view) { return view == View::cinematic || view == View::overview; } // 920:430 capsules.
inline bool tabs(View view) { return view == View::cinematic || view == View::overview; }    // Category strip, LB/RB.
inline bool preview(View view) { return metadata(view) || view == View::cinematic || carousel(view); }
inline bool pinnedHeader(View view) { return rows(view) || view == View::cinematic || view == View::overview || carousel(view); }
inline bool actions(View view) { return view == View::detailed || view == View::stage; }
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
    bool stage_summary = true;        // Scena: the game's short summary under its logo (Krótki opis).
    uint32_t filters = 0;             // Filtry: Filter bits, all of them required.
    int grid_columns = 0;             // Siatka: its columns, 0 automatic (by the screen's width).
    bool grid_collections = false;    // Siatka: the collections as tiles above the games (Kolekcje jako kafle).
    bool operator==(const Options&) const = default;
};
inline bool metadata(const Options& options) {
    return metadata(options.view) || (options.view == View::cinematic && (options.cinematic_horizontal || options.cinematic_column));
}
inline bool posterArt(const Options& options) {
    return options.view == View::grid ? options.grid_posters && !options.grid_landscape :
           options.view == View::coverflow ? !options.coverflow_cd : posterArt(options.view);
}
inline int coverflowVariant(const Options& options) { return options.view == View::bookshelf ? 0 : options.coverflow_cd ? 2 : 1; }
inline void setCoverflowVariant(Options& options, int variant) {
    variant = ((variant % coverflow_variants) + coverflow_variants) % coverflow_variants;
    options.view = variant == 0 ? View::bookshelf : View::coverflow;
    if (variant) options.coverflow_cd = variant == 2;
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
// The filters in force, Tylko zainstalowane included (the number on the order's button).
inline int activeFilters(const Options& options) { return (options.installed_only ? 1 : 0) + std::popcount(options.filters); }
// Siatka's columns as its option steps through them: 0 automatic, then min..max for its artwork
// (landscape tiles are wider). A number saved for the other artwork is clamped where it is used.
inline int gridColumnsMin(const Options& options) { return options.grid_landscape ? 2 : 3; }
inline int gridColumnsMax(const Options& options) { return options.grid_landscape ? 6 : 10; }
inline int gridColumnChoices(const Options& options) { return gridColumnsMax(options) - gridColumnsMin(options) + 2; }
inline int gridColumnsChoice(const Options& options) {
    return options.grid_columns ? std::clamp(options.grid_columns, gridColumnsMin(options), gridColumnsMax(options)) - gridColumnsMin(options) + 1 : 0;
}
inline void setGridColumnsChoice(Options& options, int choice) { options.grid_columns = choice > 0 ? gridColumnsMin(options) + choice - 1 : 0; }
inline const wchar_t* label(Filter filter) {
    switch (filter) {
    case Filter::single_player: return tr(L"Dla jednego gracza");
    case Filter::multiplayer: return tr(L"Wieloosobowe");
    case Filter::coop: return tr(L"Kooperacja");
    case Filter::local: return tr(L"2+ graczy na jednym ekranie");
    case Filter::online: return tr(L"Gra przez sieć");
    case Filter::controller: return tr(L"Obsługa pada");
    case Filter::vr: return L"VR";
    case Filter::game_pass: return L"Game Pass";
    case Filter::owned: return tr(L"Kupione");
    default: return tr(L"Nigdy nie grane");
    }
}
inline const wchar_t* label(Order order) {
    switch (order) {
    case Order::alphabetical: return tr(L"Alfabetycznie");
    case Order::added: return tr(L"Ostatnio dodane");
    case Order::release: return tr(L"Data wydania");
    case Order::installed: return tr(L"Ostatnio zainstalowane");
    case Order::playtime: return tr(L"Najdłużej grane");
    case Order::rating: return tr(L"Najwyżej oceniane");
    case Order::shortest: return tr(L"Najkrótsze do przejścia");
    case Order::longest: return tr(L"Najdłuższe do przejścia");
    default: return tr(L"Ostatnio grane");
    }
}
inline const wchar_t* label(View view) {
    switch (view) {
    case View::list: return tr(L"Lista");
    case View::list_plus: return tr(L"Lista+");
    case View::cinematic: return tr(L"Kinowy");
    case View::detailed: return tr(L"Szczegółowy");
    case View::stage: return tr(L"Scena");
    case View::coverflow: return tr(L"Coverflow");
    case View::overview: return tr(L"Przegląd");
    case View::bookshelf: return tr(L"Coverflow"); // Its Półka variant: one drawer row.
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
// `values`: by gameKey for the valueOrder() orders (install time, minutes played, rating, length),
// largest first; missing = 0, last.
std::vector<Game> arrange(const std::vector<Game>& games, Options options, const std::map<std::string, uint64_t>& first_seen,
                          bool include_private = false, const std::unordered_map<std::string, int>* released = nullptr,
                          const std::unordered_map<std::string, uint64_t>* values = nullptr);
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
