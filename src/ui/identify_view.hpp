#pragma once
#include "settings_view.hpp"
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
    // Emulated games may be identified on ScreenScraper instead (jeuRecherche.php on their system).
    enum class Source { rawg, screenscraper };
    Source source = Source::rawg;
    bool emulated = false;
    std::vector<screenscraper::GameInfo> found; // Source::screenscraper: the game of each row.
};
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
    settings_ui::Rect source() const { const auto p = panel(); return {p.right - 270, p.top + 20, p.right - 88, p.top + 64}; }
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
