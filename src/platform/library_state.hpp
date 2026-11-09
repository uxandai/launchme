#pragma once
#include "../library/view_options.hpp"
#include "../library/organization.hpp"
#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

enum class LibraryPlace { shelves, library, platform };
struct LibraryBookmark {
    uint64_t account = 0;
    LibraryPlace place = LibraryPlace::shelves;
    std::string category, selection, source;
    std::wstring platform;
    float scroll = 0;
    std::string gog_account;
    std::string epic_account;
    bool operator==(const LibraryBookmark&) const = default;
};
struct LibraryState {
    std::optional<LibraryBookmark> bookmark;
    std::map<std::string, library_view::Options> options;
    std::map<std::string, uint64_t> first_seen;
    std::map<std::string, uint64_t> last_played;
    CollectionEdits collections;
    std::map<std::string, uint64_t> playtime_ms;
    std::map<std::string, std::string> preferred_versions;
    // Games hidden in Launchme (Szczegóły → "…" → "Ukryj"), by gameKey; Steam uses
    // "steam-user:<account>/<gameKey>". They join "Ukryte". Row: hidden "<key>".
    std::set<std::string> hidden;
    // Durable pending native Steam hide/show operations; false must survive a client outage too.
    std::map<std::string, bool> steam_hidden_pending;
    // Steam edits that Steam itself cannot apply (an app it has no record of: a removed demo, an ended
    // playtest). The edit stays in Launchme only and is not sent again while its value is unchanged.
    // Key: steam_sync::unsyncedKey. Row: steam-unsynced "<key>" <0|1>.
    std::map<std::string, bool> steam_unsynced;
    // Wyklucz grę (any store, scanned DRM-free and emulated games, Steam shortcuts): never joins Ctrl+H.
    // Titles are kept for restoring even offline (Biblioteka → Co pokazywać → Wykluczone gry).
    std::map<std::string, std::wstring> excluded_games;
    // Emulator per gra: a game's own emulator by gameKey, a catalogue id ("ryujinx") or its own program.
    // Row: game-emulator "<key>" "<id>" "<program>" (one of them empty).
    struct GameEmulator { std::string id; std::wstring program; bool operator==(const GameEmulator&) const = default; };
    std::map<std::string, GameEmulator> game_emulators;
    // A Steam game's own launch options ("Opcje uruchamiania…"), by gameKey; passed through
    // steam.exe -applaunch. Row: launch-arguments "<key>" "<options>".
    std::map<std::string, std::wstring> launch_arguments;
    // Usuń z ostatnio granych, by gameKey: the game stays out of the recent rows until it is played
    // after this time (Steam's own play dates are never touched). Row: recent-hidden "<key>" <time>.
    std::map<std::string, uint64_t> recent_hidden;
    bool operator==(const LibraryState&) const = default;
};
inline constexpr size_t max_library_state_bytes = 32u * 1024 * 1024;
// Missing file = empty state; I/O, format or size failure = nullopt. A caller
// must not replace a state it could not read with freshly collected history.
std::optional<LibraryState> loadLibraryState(const std::filesystem::path& file);
// After loadLibraryState() refused a readable file (damaged rows, a newer version): every row
// this build understands. nullopt only when the file cannot be read at all (locked, I/O).
struct SalvagedLibraryState { LibraryState state; size_t skipped_rows = 0; };
std::optional<SalvagedLibraryState> salvageLibraryState(const std::filesystem::path& file);
bool saveLibraryState(const LibraryState& state, const std::filesystem::path& file);

// Latest-wins snapshots keep file flush/rename latency off the frame loop.
// Destruction drains the last snapshot so normal shutdown preserves the view.
class LibraryStateWriter {
public:
    explicit LibraryStateWriter(std::filesystem::path file);
    ~LibraryStateWriter();
    LibraryStateWriter(const LibraryStateWriter&) = delete;
    LibraryStateWriter& operator=(const LibraryStateWriter&) = delete;
    void save(const LibraryState& state);
    // Blocks until the latest snapshot is on disk (Windows is ending the session: the process
    // may be gone as soon as the message handler returns). False after `timeout`.
    bool flush(std::chrono::milliseconds timeout);
    std::optional<bool> takeResult();
private:
    std::filesystem::path file_;
    std::mutex mutex_;
    std::condition_variable wake_, idle_;
    std::optional<LibraryState> pending_;
    std::atomic_int result_{0};
    bool stopping_ = false, busy_ = false;
    std::thread worker_;
};
