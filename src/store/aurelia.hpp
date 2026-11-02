#pragma once
// Aurelia is an optional out-of-process Steam integration. Only its documented CLI is used;
// credentials and session files belong to Aurelia, in Launchme's isolated profile.
#include "../net/json.hpp"
#include "../model/game.hpp"
#include "../friends/steam_friends.hpp"
#include "../library/steam_achievements.hpp"
#include "../model/game_details.hpp"
#include "../library/collections.hpp"
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace aurelia {
inline constexpr wchar_t latest_url[] = L"https://api.github.com/repos/Drackrath/Aurelia/releases/latest";
// The release asset for this system and the name it is installed under (<root>/<release>/<name>).
#ifdef _WIN32
inline constexpr wchar_t asset_name[] = L"aurelia_windows_x86_64.exe";
inline constexpr wchar_t executable_name[] = L"aurelia_windows_x86_64.exe";
#elif defined(__aarch64__)
inline constexpr wchar_t asset_name[] = L"aurelia_linux_arm64";
inline constexpr wchar_t executable_name[] = L"aurelia";
#else
inline constexpr wchar_t asset_name[] = L"aurelia_linux_x86_64";
inline constexpr wchar_t executable_name[] = L"aurelia";
#endif
using Cancel = std::function<bool()>;
using Event = std::function<void(const json::Value&)>;
// Lines a running command reads on its stdin while it runs: a sign-in's Steam Guard code, or
// "cancel". Written by the UI, sent by the transport's loop.
struct Input {
    std::mutex mutex;
    std::vector<std::string> lines;
    void write(std::string line) { std::lock_guard lock(mutex); lines.push_back(std::move(line)); }
    std::vector<std::string> take() { std::lock_guard lock(mutex); return std::exchange(lines, {}); }
};
enum class Command {
    // login: Aurelia's own console (password, Steam Guard). login_qr and login_password are the
    // in-app sign-ins of `--json login` (events on stderr, stdin kept open for the code and cancel).
    login_qr, login_password,
    health, login, logout, reconnect, library, libraries, info, achievements, friends, presence,
    chat_history, chat_send, chat_live, install, downloads, cancel_install, update, verify, uninstall,
    play, launch_targets, running, stop, available, cloud_list, cloud_sync, dlc, enable_dlc, disable_dlc,
    manifests, pin, unpin, downgrade, branches, set_branch, move,
    workshop_browse, workshop_status, workshop_install, workshop_subscribe, workshop_unsubscribe,
    // Steam's collections, read only: pull fetches the cloud's into Aurelia's store (never push or sync,
    // which change the account), list names them, show (text: its id) gives one's games.
    collections_pull, collections_list, collections_show,
    standalone // Host-side workflow; never passed directly to the CLI.
};
struct Request {
    Command command = Command::health;
    uint32_t app = 0, depot = 0;
    uint64_t id = 0; // SteamID, manifest ID or Workshop ID; never a floating-point conversion.
    // Free text of the command (chat message, branch, presence, cloud resolution). Linux, install
    // and play: the build, "linux" (native) or "windows" (through Aurelia's Proton/Wine); empty:
    // install a native build when the game has one, play what launchStandalone selected.
    std::wstring text;
    std::filesystem::path folder;
    bool steam = true;
    std::wstring language = L"english";
    enum class Direction { both, down, up } direction = Direction::both;
    bool auto_cloud = false;
    bool force = false; // stop: end the recorded process tree at once (taskkill /F), as Zamknij grę does.
    // login_password: the password, given to the child as AURELIA_PASSWORD (never on a command line).
    std::wstring secret;
    std::shared_ptr<Input> input; // login_qr, login_password: lines for the child's stdin.
};
struct Reply {
    bool ok = false, cancelled = false;
    json::Value data;
    std::wstring error;
};
struct InstallationInfo { std::filesystem::path executable; std::wstring version; };
struct Release { std::wstring version, url; uint64_t size = 0; std::string sha256; };
struct Runtime { std::filesystem::path executable, profile; uint32_t query_timeout_ms = 90000; };
std::wstring daemonEndpoint(const std::filesystem::path& profile);
// The daemon's identity marker for an endpoint (upstream's version_marker_path): next to its socket on
// Linux, in the temp folder named after the pipe on Windows.
std::filesystem::path daemonMarker(const std::wstring& endpoint);
// A Steam account's name as `login -u` takes it: not an option, no spaces or control characters.
bool validAccount(std::wstring_view name);
using Transport = std::function<Reply(const Request&, const Cancel&, const Event&)>;
std::vector<std::wstring> arguments(const Request& request);
Reply invoke(const Runtime& runtime, const Request& request, const Cancel& cancelled, const Event& event = {});
Transport transport(Runtime runtime);
Reply checked(const Request& request, Reply reply);
Reply launchStandalone(const Transport& invoke, Request request, const Cancel& cancel, const Event& event = {});
bool saveLibraryCache(const std::filesystem::path& folder, uint64_t account, const json::Value& games);
std::optional<json::Value> loadLibraryCache(const std::filesystem::path& folder, uint64_t account);
InstallationInfo installed(const std::filesystem::path& root);
std::optional<Release> parseRelease(std::string_view body);
bool allowedDownload(std::wstring_view url);
Reply installRelease(const std::filesystem::path& root, const Release& release, const Cancel& cancel,
                     const std::function<void(uint64_t, uint64_t)>& progress = {});
// Versioned executables are published with an atomic pointer; an update never replaces an in-use EXE.
bool publish(const std::filesystem::path& root, const Release& release, const std::filesystem::path& downloaded);

std::optional<uint64_t> integer(const json::Value* value);
std::string text(const json::Value& value, std::string_view key);
bool flag(const json::Value& value, std::string_view key);
std::optional<std::vector<Game>> library(const json::Value& value, uint64_t account);
// `collections list`: Steam's collections as Aurelia keeps them, without their games; the dynamic ones
// (filter-based: Steam computes them) are left out. Ids as Launchme's ("user-collections.<id>").
std::optional<std::vector<SteamCollection>> collectionList(const json::Value& value);
// `collections show <id>`: that collection's games.
std::optional<std::vector<uint32_t>> collectionMembers(const json::Value& value);
// Aurelia is the library (account, owned and installed games, collections, play time) instead of the
// Steam client: it is on and either replaces Steam for every game ("Aurelia jako zamiennik Steam") or
// there is no Steam client signed in here (no steam.exe or no account in loginusers.vdf).
inline bool librarySource(bool active, bool replaces_steam, bool steam_signed_in) { return active && (replaces_steam || !steam_signed_in); }
FriendsResult friends(const json::Value& value);
SteamAchievements achievements(const json::Value& value);
GameDetails details(const json::Value& value);
struct Download {
    uint32_t app = 0; std::wstring name, status; Installation progress;
    uint64_t speed = 0;
    std::optional<uint64_t> eta;
};
std::optional<std::vector<Download>> downloads(const json::Value& value);
enum class CloudState { unknown, checking, files, empty, syncing, synced, conflicts, incomplete, failed };
struct Cloud {
    CloudState state = CloudState::unknown; size_t files = 0, conflicts = 0; uint64_t checked = 0; std::wstring error;
    std::vector<std::wstring> issues;
};
Cloud cloud(const Reply& reply, bool sync);
const wchar_t* cloudLabel(CloudState state);
const wchar_t* cloudShort(CloudState state); // The state alone (a row's value, the icon's tooltip).
struct Dlc { uint32_t app = 0; std::wstring name; std::optional<bool> owned, installed, disabled; };
std::optional<std::vector<Dlc>> dlcs(const json::Value& value);
struct ChatMessage { uint64_t timestamp = 0; bool self = false; std::wstring text; };
std::optional<std::vector<ChatMessage>> chatHistory(const json::Value& value);
bool validGame(const Game& game);
}
