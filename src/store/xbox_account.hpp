#pragma once
// The Xbox (Microsoft) account through launchme-xbox, Launchme's own helper built on Xodus
// (https://github.com/xodus-gaming/xodus, GPL-3.0; source in helpers/xbox). Like Nile for Amazon it is
// optional and downloaded only when asked (pinned version, size and SHA-256 verified), and it runs
// hidden with a private profile (<config>\xbox: DPAPI-sealed tokens, the WebView2 data of its
// sign-in window, the synced library). Launchme never touches the Xbox app's own sign-in.
//
// What it gives: the account's games (bought, Xbox Play Anywhere, the PC Game Pass catalog while
// subscribed) next to the games the Xbox app installed. Install goes through the Xbox app by
// default; with "Instalacja przez Launchme" (experimental) the helper streams a GDK title's MSIXVC
// package into a folder and Graj runs it through the helper, which asks for a fresh license,
// decrypts the package's encrypted files for the session and seals them again afterwards.
//
// Since helper 0.2.0 (offline play, activity and achievements after the Nexus launcher's backend,
// https://github.com/domsvoboda/nexus-backend): the account's designated offline PC (one per account,
// changed within Microsoft's own limit of a few times a year) keeps each game's offline lease when it
// starts online, so Graj also works without a connection while the lease lasts (about 30 days);
// the played titles' last play, minutes and achievement counts come with every sync (activity.json),
// a title's achievements on request; `updates` compares the installed packages with the current ones.
//
// Linux: the helper is not downloaded but shipped next to the launcher (tools/build-xbox-helper.sh);
// its profile (<config>/xbox, 0700) keeps the tokens in 0600 files and the sign-in window is
// WebKitGTK. There is no Xbox app: account games install only through the helper, and they run
// through the launcher's Wine/Proton runner with the helper wrapping its command (runAction).
#include "../library/steam_achievements.hpp"
#include "../model/game.hpp"
#include "../platform/helper_process.hpp"
#include "gog_install.hpp" // The shared install progress/modal DTOs.

#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace xbox_account {
using Cancel = std::function<bool()>;

inline constexpr std::wstring_view helper_version = L"0.2.0";
inline constexpr uint64_t helper_bytes = 15333376;

std::filesystem::path profileDirectory(const std::filesystem::path& config); // <config>\xbox
std::filesystem::path libraryPath(const std::filesystem::path& config);      // The helper's synced library.
std::filesystem::path activityPath(const std::filesystem::path& config);     // Played titles (helper `activity`).
std::filesystem::path offlinePath(const std::filesystem::path& config);      // The offline PC and leases (helper).
std::filesystem::path updatesPath(const std::filesystem::path& config);      // Products with a newer package.
// <config>\tools\launchme-xbox-<version>.exe; Linux: launchme-xbox next to the launcher, or $LAUNCHME_XBOX_HELPER.
std::filesystem::path helperPath(const std::filesystem::path& config);
bool helperInstalled(const std::filesystem::path& config); // Present with the pinned size (hashed when used); Linux: executable.

bool validProductId(std::string_view id);      // Store BigId, e.g. 9NBLGGH4R315.
bool validFamilyName(std::string_view family); // Package family name: Publisher.Name_publisherhash.
// A folder name for the game's installation: its title without the characters Windows forbids.
std::wstring folderName(std::wstring_view title, std::string_view product);
// The market and language the Store catalog is asked in, from the user's Windows region.
std::wstring market();
std::wstring language();

// The helper's answer: the last JSON object it printed on stdout.
struct Result {
    bool ok = false;
    std::string error;   // usage, signed_out, cancelled, network, not_entitled, unsupported_package, not_found, storage, internal.
    std::wstring message;
    std::string username, puid, executable;
};
std::optional<Result> parseResult(std::string_view output);
std::string_view answerLine(std::string_view output); // The last JSON object line.
std::string serializeUpdates(const std::set<std::string>& products); // updates.json
gog::InstallError installError(const std::string& helper_error); // The helper's error code as the install dialog's.
// {"progress":{"stage":"download","done":N,"total":M}} from its error stream.
struct Progress { std::string stage; uint64_t done = 0, total = 0; };
std::optional<Progress> parseProgress(std::string_view line);

struct Account { std::string username, puid; };
std::optional<Account> parseAccount(std::string_view account_json);

// The account's played titles (activity.json), by Xbox title id.
struct Activity {
    uint64_t last_played = 0;        // Unix seconds; 0 unknown.
    std::optional<uint32_t> minutes; // Missing is not zero.
    uint32_t unlocked = 0, total = 0, gamerscore = 0, total_gamerscore = 0;
    bool operator==(const Activity&) const = default;
};
std::map<uint32_t, Activity> parseActivity(std::string_view activity_json);

// offline.json: the designation as Microsoft last said (enabled empty: never asked) and, per Store
// product, until when its stored offline lease lets it start without a connection.
struct OfflineStatus {
    std::optional<bool> enabled;
    uint64_t checked_at = 0;
    std::optional<uint32_t> remaining, max, period_days; // Changes left in Microsoft's period.
    uint64_t next_change_at = 0;
    std::map<std::string, uint64_t> games;
    bool operator==(const OfflineStatus&) const = default;
};
OfflineStatus parseOffline(std::string_view offline_json);
// The game's offline lease end when it is still ahead of `now`; 0 otherwise.
uint64_t offlineUntil(const OfflineStatus& status, const std::string& product, uint64_t now);

// A library entry that is no game to keep: a demo, a beta ("Closed Beta") or a title the Store marks
// "Not Supported" (Roblox's old Windows app). Whole words, any case. Listed only once installed.
bool unlistedTitle(std::wstring_view title);

// The account's add-ons by their game's Store product (library.json's "addons"): content has a PC
// package to download, the rest is a licence the game checks itself. Listed only (no install yet).
struct Addon {
    std::string product;
    std::wstring title;
    bool content = false;
    std::string image; // https only; empty without one.
    bool operator==(const Addon&) const = default;
};
using Addons = std::map<std::string, std::vector<Addon>>;
Addons parseAddons(std::string_view library_json);

// The helper's `updates` answer: the Store products whose installed package has a newer one.
std::set<std::string> parseUpdates(std::string_view updates_json);

// One title's achievements (helper `achievements`), as Launchme's achievement lists.
std::optional<SteamAchievements> parseAchievements(std::string_view result_json, uint32_t title_id);

// Games installed by the helper: <config>\xbox\installs.json, each checked against its marker
// (<dir>\.launchme-xbox.json, written by `install`) and its package header.
struct OwnInstall { std::string product, source_id; std::filesystem::path directory; };
std::vector<OwnInstall> parseInstalls(std::string_view installs_json);
std::string serializeInstalls(const std::vector<OwnInstall>& installs);
// The executable the install marker names, inside `directory`; empty if missing or escaping it.
std::filesystem::path markerExecutable(std::string_view marker_json, const std::filesystem::path& directory, const std::string& product);
bool ownInstall(const Game& game); // Installed by the helper (its marker is in the game's folder).

// The synced library (helper `library`) as Launchme games. `account` empty: signed out, only
// installs made here. Account games use the package family name as their ID (the Xbox app's
// installs use it too, so both meet in one entry); the Store product ID stays in xbox_product.
std::vector<Game> parseLibrary(std::string_view library_json, const std::vector<OwnInstall>& installs, const std::string& account,
                               const std::filesystem::path& config);
// The helper started for Graj of a game it installed.
GameAction runAction(const std::filesystem::path& config, const std::filesystem::path& directory);
#ifndef _WIN32
// Linux, Graj of a game the helper installed: the game's (encrypted) executable is the target, so
// the launcher's Wine/Proton runner chooses the compatibility tool, prefix and options as for any
// Windows game, and `launchme-xbox --profile <dir> run --dir <game> --market <m> --` wraps that
// command (GameAction::wrapper): a fresh license, the encrypted files decrypted for the session,
// xgameruntime.dll, then the files sealed again. A model without `wrapper` gets runAction(config,
// directory) instead: the helper itself, which then runs the game with $WINE or wine from PATH.
GameAction runAction(const std::filesystem::path& config, const std::filesystem::path& directory, const std::filesystem::path& executable);
#endif
// Install through the Xbox app (its product page; the Microsoft Store when the app is missing).
// Linux has neither: empty.
GameAction xboxAppInstall(const std::string& product, bool xbox_app);

struct Profile {
    std::optional<Account> account;
    std::vector<Game> games;   // With their play time and last play from activity.json.
    uint64_t synced_at = 0;
    std::string game_pass; // The active subscription that lists the PC Game Pass catalog.
    std::vector<std::string> gamepass_recent; // Store products newest in Game Pass first (account's games only).
    std::map<uint32_t, Activity> activity;
    Addons addons;
};
// Play time and last play of the account's played titles on the games (by title id).
void applyActivity(std::vector<Game>& games, const std::map<uint32_t, Activity>& activity);
Profile readProfile(const std::filesystem::path& config); // Files only; no helper, no network.

enum class State { disconnected, preparing, signing_in, syncing, connected, error };
enum class Failure { none, cancelled, helper, network, expired, storage, declined };
// offline_status asks Microsoft whether this PC is the offline PC; offline_on/off change it (the
// player confirmed: a change counts against Microsoft's yearly limit).
enum class Command { restore, download, connect, sync, cancel, logout, offline_status, offline_on, offline_off };
struct Snapshot {
    State state = State::disconnected;
    Failure failure = Failure::none;
    std::string account;   // PUID.
    std::wstring name;     // The account's e-mail address.
    uint64_t synced_at = 0;
    size_t games = 0;
    bool game_pass = false;
    bool helper = false;   // Downloaded.
    bool downloading = false; uint64_t download_done = 0, download_total = 0;
    std::vector<std::string> gamepass_recent;
    OfflineStatus offline;
    bool offline_busy = false;           // Asking or changing the offline PC.
    Failure offline_failure = Failure::none;
    std::set<std::string> updates;       // Store products with a newer package (own installs).
    std::map<uint32_t, Activity> activity;
    Addons addons;
};

// A title's achievements as the helper read them (`failed`: not available now).
struct AchievementsAnswer {
    uint32_t title_id = 0;
    SteamAchievements list;
    bool failed = false;
};

// The helper's side, replaceable in tests.
struct Operations {
    std::function<std::filesystem::path(const std::filesystem::path& config, bool download, const Cancel&,
                                        const std::function<void(uint64_t)>&)> prepare;
    std::function<helper_process::Result(const std::filesystem::path& exe, const std::filesystem::path& config,
                                         const std::vector<std::wstring>& arguments, const Cancel&,
                                         const std::function<void(std::string_view)>&)> invoke;
};
Operations nativeOperations();

class Service {
public:
    virtual ~Service() = default;
    virtual void submit(Command) = 0;
    virtual std::optional<Snapshot> take() = 0;
    virtual bool install(const Game&, gog::InstallRequest) = 0;
    virtual std::optional<gog::InstallSnapshot> takeInstall() = 0;
    virtual void cancelOperation() = 0;
    virtual bool busy() const = 0;
    virtual void achievements(uint32_t title_id) = 0; // Answered through takeAchievements.
    virtual std::optional<AchievementsAnswer> takeAchievements() = 0;
};
std::unique_ptr<Service> makeService(std::filesystem::path config, Operations = nativeOperations());

// "Odinstaluj" for a game the helper installed: its folder goes and it leaves installs.json. Worker only.
bool uninstall(const std::filesystem::path& config, const Game& game);
std::wstring statusText(const Snapshot&);
std::wstring offlineText(const Snapshot&, uint64_t now); // The offline PC row's description.
std::wstring offlineDate(uint64_t unix_seconds);         // "dd.mm.yyyy" (UTC), for "Offline do".
std::wstring installStatusText(const gog::InstallSnapshot&);
}
