#pragma once
// What the case in the hand shows beyond its front (src/app/game_case.cpp): the back of the box,
// its spine and the game's manual, for emulated games. ES-DE's own downloads first (downloaded_media's
// backcovers and manuals; it keeps no spines), then a copy kept before, else ScreenScraper's
// box-2D-back, box-2D-side and "manuel" of a ROM it identified (the metadata pass caches that,
// src/app/ratings.cpp): no identification lookups here. Kept as <root>\emulation\backs\<app id>.img,
// <root>\emulation\sides\<app id>.img and <root>\emulation\manuals\<app id>.pdf.
// Worker only: a download takes seconds.
#include "../library/screenscraper.hpp"
#include "../model/game.hpp"

#include <filesystem>
#include <vector>

namespace case_extras {
std::filesystem::path backCopy(const Game& game, const std::filesystem::path& config_root);
std::filesystem::path sideCopy(const Game& game, const std::filesystem::path& config_root);
std::filesystem::path manualCopy(const Game& game, const std::filesystem::path& config_root);
// The back of the box (PNG or JPEG bytes); empty when there is none.
std::vector<uint8_t> back(const Game& game, const std::filesystem::path& config_root, const screenscraper::Get& get = httpGetPrivate);
// The spine's own print, as scanned (a PS1 case's black strip with PlayStation along it); empty when there is none.
std::vector<uint8_t> side(const Game& game, const std::filesystem::path& config_root, const screenscraper::Get& get = httpGetPrivate);
// The manual, a PDF on disk; empty when there is none.
std::filesystem::path manual(const Game& game, const std::filesystem::path& config_root, const screenscraper::Get& get = httpGetPrivate);
// Whether bytes are a PDF (ScreenScraper answers "NOMEDIA" and the like with status 200).
bool pdf(const std::vector<uint8_t>& bytes);
}
