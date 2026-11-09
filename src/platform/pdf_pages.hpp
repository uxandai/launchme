#pragma once
// A PDF's pages as pictures (the game's manual, src/app/game_manual.cpp): Windows renders them with
// its own PDF engine (Windows.Data.Pdf); Linux has none here (the manual opens in the desktop's
// viewer instead). Worker threads only: a page takes tens of milliseconds.
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace pdf_pages {
bool available();
struct Page {
    std::vector<uint8_t> png; // Empty: the page could not be drawn.
    int count = 0;            // The document's pages (0: not readable).
    std::wstring error;       // Why it could not be drawn (the engine's message), for the log.
};
// Page `index` (from 0), `width` pixels wide, with the document's page count.
Page render(const std::filesystem::path& file, int index, uint32_t width);
// Several pages of one document, opened once: each handed to `deliver` as it is drawn (false stops).
void render(const std::filesystem::path& file, const std::vector<int>& indices, uint32_t width,
            const std::function<bool(int index, Page page)>& deliver);
}
