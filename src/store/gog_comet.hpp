#pragma once
// Comet (https://github.com/imLinguin/comet, Apache-2.0): an open reimplementation of GOG Galaxy's
// communication service. A GOG game built with the Galaxy SDK asks 127.0.0.1:9977 for its
// achievements, stats and leaderboards; without Galaxy running nobody answers there and they
// silently do nothing. Launchme starts comet beside such a game with the linked GOG account and
// ends it after the game. It is not a downloader (installs stay with gog_install.*).
//
// The account's tokens reach comet only on its command line, from the account worker
// (Service::comet); the UI never sees them. Comet keeps its own data (an offline achievements
// database, GOG's peer libraries) under <profile>/gog/comet.
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gog {
// Comet asks GOG for each game's own token with the Galaxy client's refresh token: only an account
// signed in through that client (the default profile) can use it.
inline constexpr std::string_view galaxy_client_id = "46899977096215655";
inline constexpr std::wstring_view comet_version = L"0.4.1";

struct CometCredentials { std::string access, refresh, user, username; };
struct CometRequest {
    std::filesystem::path exe, data; // The verified binary; comet's own folders.
    // An achievement the game unlocked (its name), called on comet's reader thread.
    std::function<void(std::wstring)> unlocked;
    std::function<void(std::string_view)> log; // Every log line (diagnostics only; may name the account).
    // A game opened its Galaxy session (its process ID): GOG Galaxy's overlay can attach to it.
    std::function<void(uint32_t)> session;
};
// A running comet: destroying it ends the process.
class Comet {
public:
    virtual ~Comet() = default;
    virtual bool running() const = 0;
};
using CometStart = std::function<std::shared_ptr<Comet>(const CometRequest&, const CometCredentials&)>;

// The pinned release for this computer, downloaded once into <root>/tools and SHA-256 checked
// (also when already there). Empty: no build for this system, offline or cancelled.
std::filesystem::path prepareComet(const std::filesystem::path& root, const std::function<bool()>& cancel);
// The arguments after the program: tokens, account, and --quit (it ends 15 s after the game leaves).
std::vector<std::string> cometArguments(const CometCredentials&);
// "… Unlocking achievement <key>, <name>" in comet's log: the name.
std::optional<std::wstring> cometUnlocked(std::string_view line);
// "... Requested session start for <pid>" in comet's log: the game's process ID.
std::optional<uint32_t> cometSession(std::string_view line);

// GOG Galaxy's own in-game overlay (Shift+Tab: friends, chat, game invitations), as Heroic uses it.
// Comet's "overlay" command fetches it from GOG (about 100 MB, GOG's own manifests) into
// <data>/local/comet/redist; on Windows comet answers it on the game's named pipe, on Linux
// galaxy-helper (inside Wine) starts it and bridges that pipe to comet's socket.
std::filesystem::path overlayFolder(const std::filesystem::path& data);
bool overlayInstalled(const std::filesystem::path& data);
// Runs comet's "overlay" command and waits for it (true: the overlay is there afterwards).
bool downloadOverlay(const std::filesystem::path& exe, const std::filesystem::path& data, const std::function<bool()>& cancel);
// GalaxyOverlay.exe's arguments, as GOG Galaxy starts it beside a game.
std::wstring overlayArguments(const std::filesystem::path& redist, uint32_t pid, std::string_view product,
                              const std::filesystem::path& cache, std::wstring_view language);
// Windows: starts the overlay for the game's process (null: not installed, a bad product ID, or it
// could not start). Destroying the result ends it. Linux: always null (galaxy-helper starts it).
std::shared_ptr<Comet> startOverlay(const std::filesystem::path& data, uint32_t pid, std::string_view product, std::wstring_view language);
// Linux: galaxy-helper (imLinguin/galaxy-helper 0.1.0, pinned) in <root>/tools/galaxy-helper-0.1.0:
// galaxy.exe and libgalaxyunixlib.dll.so. prepare: empty when offline or cancelled.
std::filesystem::path galaxyHelperFolder(const std::filesystem::path& root);
bool galaxyHelperInstalled(const std::filesystem::path& root);
std::filesystem::path prepareGalaxyHelper(const std::filesystem::path& root, const std::function<bool()>& cancel);

// 127.0.0.1:9977 is taken: GOG Galaxy (or another comet) already answers games.
bool galaxyPortBusy();
// Starts comet (nothing when the port is taken). Null when it could not start.
std::shared_ptr<Comet> startComet(const CometRequest&, const CometCredentials&);
}
