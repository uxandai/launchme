#pragma once
// Wine/Proton launch options (src/model/launch_options.hpp): the global default and each game's own,
// and the prefix assigned to each game. One file, <config>/linux/launch_options.json:
//   {"version":2,"prefix_root":"/home/u/Games/launchme/prefixes","default":{...},
//    "games":{"<game key>":{...}},"prefixes":{"<game key>":"/home/u/Games/launchme/prefixes/<slug>"}}
// Keys are gameKey(). "default" lists only the fields that differ from LaunchOptions{}, by their
// catalog ids (launch_options_catalog.hpp); its compat_tool is the global tool choice ("default"
// there: automatic). A game's object lists only its overrides: every other field follows the default,
// also when the default changes later. Its "environment" holds the variables it sets itself and
// "environment_unset" the default's variables it goes without. Version 1 files (each game a full copy)
// are read as overrides of whatever differs from the default.
// Thread-safe (one process-wide lock); every write is atomic and durable. Portable.
#include "../model/game.hpp"
#include "../model/launch_options.hpp"
#include "../net/json.hpp"

#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace launch_options_store {
// A prefix the manager (Ustawienia → Proton i Wine → Prefiksy) knows by name, with the tool its
// games run with when their own choice is "default" (a game's own tool > its prefix's > the global).
struct PrefixInfo {
    std::wstring name;                   // Empty: the game's title or the folder's name.
    std::string compat_tool = "default"; // compat_tools::Tool::id, or "default".
    bool operator==(const PrefixInfo&) const = default;
};
struct Document {
    LaunchOptions defaults;
    // Each game's options as its launches use them: the default with the game's overrides.
    std::map<std::string, LaunchOptions> games;
    // The overrides: the catalog ids a game sets itself, and "env:NAME" for each environment variable
    // it sets (in its list) or leaves out (absent from it). A game in `games` without an entry here
    // overrides whatever differs from the default.
    std::map<std::string, std::set<std::string>> overrides;
    std::map<std::string, std::filesystem::path> prefixes;
    std::filesystem::path prefix_root; // Empty: defaultPrefixRoot().
    std::map<std::filesystem::path, PrefixInfo> prefix_info; // Absolute, normal paths.
    bool operator==(const Document&) const = default;
};

// Pure: the document's JSON and back. parse() skips invalid entries and values (they keep their
// defaults); nullopt only when the text is not a version 1 document.
std::string serialize(const Document& document);
std::optional<Document> parse(std::string_view text);
json::Value toJson(const LaunchOptions& options);
LaunchOptions fromJson(const json::Value& value);
// A game's starting point: the default without its tool and prefix (a game's "default" tool follows
// its prefix's and then the global choice; its prefix is its own).
LaunchOptions forGame(const LaunchOptions& defaults);
// The ids where two option sets differ: catalog ids, and "env:NAME" per environment variable.
std::set<std::string> differences(const LaunchOptions& a, const LaunchOptions& b);
// A game's options: forGame(defaults) with the fields and variables in `overrides` taken from `game`
// (an overridden variable `game` does not have is left out). Variables: the default's order, then
// the game's own.
LaunchOptions inherit(const LaunchOptions& defaults, const LaunchOptions& game, const std::set<std::string>& overrides);
// A prefix folder name for a game: its title in lowercase ASCII words joined by '-' (diacritics
// dropped), else the key's, else "game"; at most 64 characters.
std::string slug(std::wstring_view title, std::string_view key);
// umu's STORE for a source: egs, gog, amazon, ubisoft, ea, battlenet, steam; empty for the others.
std::string umuStore(GameSource source);

// <config>/linux/launch_options.json (LAUNCHME_CONFIG_DIR moves <config>, as for every file).
std::filesystem::path file();
Document load(); // An empty document when the file is missing or unreadable.
bool save(const Document& document);

// For the settings UI.
LaunchOptions defaults();
bool setDefaults(const LaunchOptions& options);
// The global tool (compat_tools::Tool::id, or "default" for the automatic choice).
std::string defaultTool();
bool setDefaultTool(const std::string& id);
// The game's own options (the default with its overrides); nullopt while it follows the default.
std::optional<LaunchOptions> gameOptions(const std::string& key);
// The options a launch uses: the game's own, else the default (compat_tool not resolved).
LaunchOptions effectiveOptions(const std::string& key);
// Every field that differs from the game's current options becomes (or stays) its override.
bool setGameOptions(const std::string& key, const LaunchOptions& options);
bool resetGameOptions(const std::string& key); // Back to the default.
// What the game sets itself (ids as in Document::overrides); empty while it follows the default.
std::set<std::string> gameOverrides(const std::string& key);
// One override back to the default; without any left the game follows the default again.
bool resetGameOption(const std::string& key, const std::string& id);
// ~/Games/launchme/prefixes; the prefixes root in use (settable) and the folder of one game.
std::filesystem::path defaultPrefixRoot();
std::filesystem::path prefixRoot();
bool setPrefixRoot(const std::filesystem::path& root); // Empty: back to the default; else absolute.
// The game's prefix: its options' prefix, else the one assigned to it, else a new <root>/<slug>
// (another game's folder gets -2, -3...), remembered so a rename keeps the prefix.
std::filesystem::path gamePrefix(const Game& game);
// The prefix the game already has (its options' own, else the one assigned to it), without assigning
// one; nullopt when it never had any. Uninstalling asks about this folder.
std::optional<std::filesystem::path> existingPrefix(const std::string& key);
// Keys of games whose own options name the tool ("default" listed when the global choice names it):
// the warning before removing a tool.
std::vector<std::string> gamesUsingTool(const std::string& tool_id);
// Keys of games whose prefix is `prefix` (shared prefixes; the warning before deleting one).
std::vector<std::string> gamesUsingPrefix(const std::filesystem::path& prefix);

// The prefix manager. Every prefix Launchme knows: named ones, each game's (assigned or chosen), the
// shared <root>/default, and folders left in the root (a game that went away); named first, then by
// path. `games`: the keys of games that use it.
struct PrefixEntry {
    std::filesystem::path path;
    PrefixInfo info;
    std::vector<std::string> games;
    bool named = false;   // In the manager's list (made or renamed there).
    bool shared = false;  // <root>/default.
};
std::vector<PrefixEntry> prefixes();
PrefixInfo prefixInfo(const std::filesystem::path& prefix); // Defaults when not named.
// Names (or renames) a prefix and sets its tool; `prefix` absolute.
bool setPrefixInfo(const std::filesystem::path& prefix, const PrefixInfo& info);
// A new named prefix folder <root>/<slug(name)> (-2, -3… when taken), empty on failure. The folder
// is made; Wine fills it at the first launch or "Utwórz prefiks".
std::filesystem::path newPrefix(const std::wstring& name, const std::string& compat_tool);
// Forgets a prefix (after deleting it): its name, and games that named it or had it assigned get a
// new one at their next launch.
bool forgetPrefix(const std::filesystem::path& prefix);
// The tool a game's launch uses: its own choice, else its prefix's, else the global one ("default":
// automatic). `key` empty: programs without a game.
std::string effectiveTool(const std::string& key, const std::filesystem::path& prefix);

// What GameAction::compat carries for a game's Windows program (src/platform/wine_runner.hpp):
// its identity for umu (STORE, store ID, Steam app ID), its install folder, its effective options
// with compat_tool "default" replaced by the global choice, and its prefix (assigned on first use).
std::shared_ptr<const CompatLaunch> compatLaunch(const Game& game);
// No game known: the default options in <root>/default.
CompatLaunch defaultCompatLaunch();
}
