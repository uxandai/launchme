#pragma once
// The Stores' two built-in tabs (src/app/store_hub.cpp): Launchery and Promocje.
#include "../net/promos.hpp"
#include "../store/launcher_install.hpp"
#include "../ui/image_decoder.hpp"

#include <atomic>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace store_hub {
// The tabs' owners, where a plugin page has its plugin's id.
inline constexpr const char* launchers_id = "launchme.launchers";
inline constexpr const char* promos_id = "launchme.promos";
inline constexpr int tab_count = 2;
inline bool builtin(const std::string& owner) { return owner == launchers_id || owner == promos_id; }
// A tile's picture, for a worker: "logo:<platform mark>", "system:<assets/systems file>" or an https picture
// kept in <config>/store-hub/images. Empty when there is none.
std::vector<uint8_t> tileImage(const std::string& image, const std::filesystem::path& config);
}

namespace launcher_state {
struct StoreHub {
    // Launchery: what this machine can install with, what is installed, what is being installed.
    launchers::Machine machine;
    bool machine_known = false, detecting = false;
    std::map<std::string, launchers::Found> found;
    struct Job {
        launcher_install::Phase phase = launcher_install::Phase::installing;
        std::shared_ptr<std::atomic_bool> cancel;
        bool removing = false; // An uninstallation.
    };
    std::map<std::string, Job> jobs;
    std::set<std::string> handed;               // Their own installer window is open: looked for again while the tab shows.
    std::map<std::string, std::wstring> notes;  // The last result under a tile ("Nie udało się zainstalować").
    uint64_t detect_at = 0;                     // GetTickCount64 of the next look while `handed` waits.
    // Linux: the question before an installation (native or Wine, and the prefix).
    std::string choosing;                       // The launcher whose card (the dialog over the tab) is open.
    std::vector<std::filesystem::path> choice_prefixes; // The prefixes its rows offer, by row command "prefix-<n>".
    // Promocje.
    promos::Feed feed;
    std::vector<promos::Offer> offers;          // As the page's tiles index them (Row::id).
    bool feed_loaded = false, feed_loading = false;
    uint64_t feed_at = 0;                       // GetTickCount64 when it arrived.
    uint32_t feed_generation = 0;
    // Tile pictures by Row::image, kept while the launcher runs (a few dozen small files).
    std::map<std::string, ImageBytes> images;
    std::set<std::string> image_pending, image_failed;
};
}
