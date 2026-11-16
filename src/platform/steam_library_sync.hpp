#pragma once
#include "../library/collections.hpp"
#include <optional>
#include <set>

namespace steam_sync {
enum class Kind { member, name, hidden, create };
struct Change {
    Kind kind = Kind::member;
    std::string collection;
    uint32_t app = 0;
    bool value = false;
    std::wstring name;
    bool operator==(const Change&) const = default;
};
struct Snapshot {
    uint64_t account = 0;
    std::vector<SteamCollection> collections;
    std::set<uint32_t> hidden, private_apps;
};
struct Result {
    uint64_t account = 0;
    std::optional<Snapshot> snapshot;
    std::vector<Change> applied;
    // Steam has no record of the app (removed demo, ended playtest, shortcut): it cannot apply these.
    std::vector<Change> unavailable;
    // Stable diagnostics; never includes client output, account names or tokens.
    std::string error;
    bool cloud_pending = false;
};
// Existing loopback Steam CEF endpoint, owned by steamwebhelper.exe from this Steam installation.
// Writes are performed by Steam's live collectionStore and confirmed against its native cache.
Result exchange(const std::filesystem::path& steam_root, uint64_t account, const std::vector<Change>& changes);
// Steam's own CEF port (127.0.0.1:8080, held by this installation's steamwebhelper) is open.
bool endpoint(const std::filesystem::path& steam_root);
// One expression in Steam's SharedJSContext, resolving to JSON text (Linux; Windows: unavailable).
// error: "unavailable" (no port or page), "unsupported" (it threw), "invalid-reply" (not a string); empty when `value` holds it.
struct Evaluation { std::string value, error; };
Evaluation evaluate(const std::filesystem::path& steam_root, const std::string& expression);
std::string requestExpression(uint64_t account, const std::vector<Change>& changes);
Result parseResult(uint64_t account, const std::vector<Change>& changes, std::string_view json);
}
