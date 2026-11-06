#pragma once
#include "settings_view.hpp"
#include "../library/metadata_sources.hpp"
#include "../library/rawg.hpp"
#include "../library/screenscraper.hpp"

namespace identify_ui {
struct State {
    bool open = false, busy = false, applying = false, query_focus = true, select_all = true;
    bool back_focus = false; // Back (top right): Up from the search field.
    Game game;
    std::wstring query, status;
    std::vector<rawg::Candidate> games;
    int selected = 0;
    // Where to look: every provider in turn (ScreenScraper for emulated games, RAWG, then the stores' text
    // details), or one of them. Emulated games may be identified on ScreenScraper (jeuRecherche.php on their system).
    // The stores follow in metadata_sources::Source order: only their text details (no artwork).
    enum class Source { all, rawg, screenscraper, nintendo, steam, gog, xbox, titledb };
    Source source = Source::all;
    Source resolved = Source::rawg; // The provider whose answer `games` / `found` hold.
    bool emulated = false;
    // "Zidentyfikuj grę" starts at once: the best match is taken as soon as a provider has one. "Dopasuj ręcznie"
    // (manual) shows the title field, the provider choice and the list instead.
    bool manual = false;
    std::vector<screenscraper::GameInfo> found; // Source::screenscraper: the game of each row.
    size_t found_offset = 0;                    // The row of found[0] (a Nintendo game's eShop rows lead).
    std::wstring provider;                      // The provider that answered, as the status names it.
    bool menu = false;                          // The provider dropdown is open.
    int menu_hover = 0;                         // Its highlighted row (keys).
    // Beside the games of the databases, the stores' text details (Nintendo eShop, Steam, GOG, Microsoft Store, TitleDB):
    // one per row of `games`, empty for a database's row. A chosen one becomes the game's own details; no artwork.
    std::vector<std::optional<metadata_sources::Offer>> offers;
};
// The providers of the dropdown, in its order: RAWG, ScreenScraper and TitleDB only for emulated games.
inline std::vector<State::Source> providers(bool emulated) {
    using S = State::Source;
    std::vector<S> list{S::all, S::rawg};
    if (emulated) list.push_back(S::screenscraper);
    list.insert(list.end(), {S::nintendo, S::steam, S::gog, S::xbox});
    if (emulated) list.push_back(S::titledb);
    return list;
}
inline bool storeSource(State::Source source) { return source >= State::Source::nintendo; }
inline metadata_sources::Source storeOf(State::Source source) {
    return static_cast<metadata_sources::Source>(static_cast<int>(source) - static_cast<int>(State::Source::nintendo));
}
// The provider as the switch names it; null for "Wszyscy dostawcy" (translated by the caller).
inline const wchar_t* sourceName(State::Source source) {
    return source == State::Source::all ? nullptr : source == State::Source::rawg ? L"RAWG" :
           source == State::Source::screenscraper ? L"ScreenScraper" : metadata_sources::name(storeOf(source));
}
// What the game is on disk, shown at the top of the dialog: its ROM, else its program, else its
// folder (a DRM-free folder game), else what its play action starts.
inline std::wstring gamePath(const Game& game) {
    if (!game.rom.empty()) return game.rom.wstring();
    if (!game.executable.empty()) return game.executable.wstring();
    if (!game.install_directory.empty()) return game.install_directory.wstring();
    return game.play_action.target;
}
struct Layout {
    float width, height;
    static constexpr int visible = 6;
    settings_ui::Rect panel() const { return {width * 0.5f - 420, 70, width * 0.5f + 420, height - 70}; }
    settings_ui::Rect search() const { const auto p = panel(); return {p.left + 28, p.top + 86, p.right - 28, p.top + 140}; }
    settings_ui::Rect submit() const { const auto s = search(); return {s.right - 54, s.top, s.right, s.bottom}; }
    settings_ui::Rect back() const { const auto p = panel(); return {p.right - 72, p.top + 20, p.right - 28, p.top + 64}; }
    settings_ui::Rect source() const { const auto p = panel(); return {p.right - 330, p.top + 20, p.right - 88, p.top + 64}; }
    // "Dopasuj ręcznie": under the spinner while the game is looked up on its own.
    settings_ui::Rect manualButton() const { const auto p = panel(); const float cx = (p.left + p.right) * 0.5f; return {cx - 130, p.bottom - 110, cx + 130, p.bottom - 56}; }
    // The provider dropdown under the provider button (Tryb TV).
    settings_ui::Rect menuRow(int index) const { const auto s = source(); const float y = s.bottom + 8 + index * 40.0f; return {s.left, y, s.right, y + 40}; }
    settings_ui::Rect menuPanel(int count) const { const auto s = source(); return {s.left - 6, s.bottom + 4, s.right + 6, s.bottom + 12 + count * 40.0f}; }
    int menuHit(float x, float y, int count) const { for (int i = 0; i < count; ++i) if (menuRow(i).contains(x, y)) return i; return -1; }
    int first(const State& state) const { return std::max(0, state.selected - visible + 1); }
    settings_ui::Rect row(int index, const State& state) const {
        const auto p = panel(); const float y = p.top + 160 + (index - first(state)) * 76.0f;
        return {p.left + 28, y, p.right - 28, y + 68};
    }
    int hit(float x, float y, const State& state) const {
        for (int i = first(state); i < static_cast<int>(state.games.size()) && i < first(state) + visible; ++i)
            if (row(i, state).contains(x, y)) return i;
        return -1;
    }
};
}
