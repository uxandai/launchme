#pragma once
// The game's manual over the case in the hand (Instrukcja, src/app/game_manual.cpp): read like the
// booklet, its cover alone, then two pages side by side. LB / RB (Left / Right, a click on either
// half, the wheel) turn the pages, A (Enter, a double click) zooms in and the stick pans, B closes it.
// Pages are drawn by the system's PDF engine on a worker (src/platform/pdf_pages.hpp).
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <vector>
#include <string>
#include <utility>

namespace manual_ui {
// Spreads: the cover, then pages 1-2, 3-4, …
inline int spreadCount(int pages) { return pages <= 0 ? 0 : 1 + pages / 2; }
// The pages of a spread (-1: none on that side).
inline std::pair<int, int> pagesOf(int spread, int pages) {
    if (spread <= 0) return {0, -1};
    const int left = spread * 2 - 1;
    return {left < pages ? left : -1, left + 1 < pages ? left + 1 : -1};
}
constexpr float zoom = 2.0f;
constexpr uint32_t page_width = 1500; // Pixels a page is drawn at (sharp on a 4K screen's half).

struct State {
    bool open = false;
    float alpha = 0;           // Fades in and out.
    std::string key;           // The game.
    std::filesystem::path file;
    int pages = 0;             // 0 while unknown.
    int spread = 0;
    bool zoomed = false;
    float pan_x = 0, pan_y = 0; // Zoomed: -1..1 across the room the zoom makes.
    float stick_x = 0, stick_y = 0;
    uint32_t generation = 0;   // Pages of an earlier manual are dropped.
    bool failed = false;       // The PDF could not be read.
    std::vector<int> broken;   // Pages the engine could not draw (shown as such, not waited for).
    bool isBroken(int page) const { return std::find(broken.begin(), broken.end(), page) != broken.end(); }
};

struct Layout {
    float width, height;
    // The room the pages fill: below the title, above the hints.
    float top() const { return 112; }
    float bottom() const { return height - 78; }
    float left() const { return std::max(48.0f, width * 0.04f); }
    float right() const { return width - left(); }
};
}
