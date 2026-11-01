#pragma once
#include "../platform/library_state.hpp"
#include "../platform/steam_library_sync.hpp"
#include "../platform/user_config.hpp"

namespace steam_sync {
// Existing Steam-only overlays are the durable outbox. Local/cross-store collections never enter it.
// Edits Steam already refused as unknown apps (LibraryState::steam_unsynced) are left out.
std::vector<Change> pending(const LibraryState& state, uint64_t account);
bool acknowledge(LibraryState& state, uint64_t account, const Change& change);
// Identity of a hide or membership edit in LibraryState::steam_unsynced (empty for create/name).
std::string unsyncedKey(uint64_t account, const Change& change);
// Steam has no record of the app: the edit stays in Launchme only. Showing it again needs nothing
// from Steam (it cannot hide an app it does not know), so that one is simply acknowledged.
bool keepLocal(LibraryState& state, uint64_t account, const Change& change);
// Forgets local-only marks whose edit is gone or changed (true when something was removed).
bool pruneUnsynced(LibraryState& state, uint64_t account);
// Edits of this account that only Launchme keeps.
size_t unsyncedCount(const LibraryState& state, uint64_t account);
// Explicit conversion, retaining non-Steam members locally in the Steam collection overlay.
// Refuses to lose members belonging to another Steam account.
bool moveCollection(LibraryState& state, AppSettings& settings, uint64_t account, const std::string& local_id, const std::string& steam_id);
}
