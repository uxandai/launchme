#pragma once
// Linux: the Proton i Wine page's state in Launcher (src/app/compat_*.cpp) and ProtonDB's answers.
#ifndef _WIN32
#include "../library/compat_tools.hpp"
#include "../library/launch_options_store.hpp"
#include "../model/game.hpp"
#include "../net/protondb.hpp"

#include <atomic>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace launcher_state {
struct CompatSettings {
    std::vector<compat_tools::Tool> tools;   // compat_tools::discover(), best first within each kind.
    bool tools_loading = false, tools_loaded = false;
    std::filesystem::path umu_run;           // Empty: umu-launcher is not installed.
    std::map<std::string, compat_tools::ReleaseList> releases; // By Source::id.
    std::set<std::string> releases_loading;
    struct Install {
        std::string source, tag;
        compat_tools::Progress progress;
        std::shared_ptr<std::atomic<bool>> cancel;
    };
    std::optional<Install> install;          // One download at a time.
    std::wstring status;                     // The last download's or prefix tool's result.
    std::vector<launch_options_store::PrefixEntry> prefixes;
    std::map<std::filesystem::path, uint64_t> sizes; // Measured on a worker.
    std::set<std::filesystem::path> measuring;
    std::set<std::string> expanded{"runner"}; // The accordion's open sections.
    std::wstring query;                      // Szukaj: the options page shows the matching options instead.
    std::string confirm;                     // A destructive row asked to confirm (its key).
    bool steam_target = false;               // Proton downloads also into Steam's compatibilitytools.d.
    Game game;                               // The game whose options page is open.
    struct Installer { std::filesystem::path prefix, installer; std::wstring name; std::string store; };
    std::optional<Installer> installer;      // Dodaj grę z instalatora: the one running (store: a store launcher's id).
    std::string launch_after;                // The game that starts once the download (compatProvision) ends.
    std::set<std::string> vc_runtime_tried;  // Games whose Visual C++ install ran this session (no second try).
    std::string store_download;              // Sklepy i launchery: the launcher whose installer is downloading.
};
struct ProtonDbEntry { uint32_t app = 0; protondb::Summary summary; };
} // namespace launcher_state
#endif
