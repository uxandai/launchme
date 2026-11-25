#pragma once
#include "../model/game.hpp"
#include <map>
#include <vector>

namespace versions {
// Conservative matching: case, whitespace and trademark marks only. Editions, remasters and
// sequels keep their identities; emulated copies (a ROM with its console) join the same title
// only with `emulated`. Besides the title, entries starting the very same .exe (same
// visibility) join one group whatever their names.
std::string identity(const Game& game, bool emulated = false);
// A ROM or a Steam shortcut of a console's game (not a PC program).
bool emulatedCopy(const Game& game);
struct Index {
    std::map<std::string, std::vector<Game>> groups;
    std::map<std::string, std::string> by_game;
    // A group needs two kinds of copy: two stores, or a store and a console (two consoles count too).
    explicit Index(const std::vector<Game>& games = {}, bool emulated = false);
    const std::vector<Game>* find(const Game& game) const;
    std::vector<Game> collapse(const std::vector<Game>& games, const std::map<std::string, std::string>& preferred,
                               const std::map<std::string, uint64_t>& first_seen = {}) const;
};
}
