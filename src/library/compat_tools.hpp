#pragma once
// Linux only: compatibility tools for Windows programs (Proton and Wine builds).
// - Installed tools: Launchme's own (<config>/compatibilitytools), Steam's compatibilitytools.d
//   (native, Flatpak, the system's /usr/share/steam/...), Steam's own Protons in every Steam library
//   (steamapps/common/Proton*), Heroic's and Lutris' downloads, Wine on PATH, and umu-launcher's
//   automatic GE-Proton / UMU-Proton when umu-run is installed.
// - Downloads: GitHub releases of GE-Proton, UMU-Proton, Proton-CachyOS and Kron4ek's Wine builds,
//   plus the prefix components DXVK, VKD3D-Proton, DXVK-NVAPI and umu-launcher itself; the lists are
//   cached for an hour, downloads resume, .sha512sum/.sha256sum files are verified when published,
//   archives (.tar.gz/.tar.xz/.tar.zst) are extracted with libarchive refusing anything that would
//   land outside (absolute or .. paths, escaping links, devices) and installed atomically.
// - The automatic default and the umu-database GAMEID lookup.
// Everything is blocking: call it from a worker. Nothing here starts a tool (wine_runner.hpp does).
#include "../model/launch_options.hpp"
#include "../model/launch_options_catalog.hpp"
#include "../net/http.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace compat_tools {
enum class Kind { proton, wine, component, umu };
enum class Origin { launchme, steam_compat, steam_system, steam_official, heroic, lutris, system, umu };

struct Tool {
    // "<origin>:<folder>": "launchme:GE-Proton11-11", "steam:GE-Proton11-11", "steam-system:...",
    // "steam-common:Proton 10.0", "heroic:...", "lutris:...", "system:wine", "umu:GE-Proton",
    // "umu:UMU-Proton". Stable across runs: LaunchOptions::compat_tool stores it.
    std::string id;
    std::wstring name;     // compatibilitytool.vdf's display_name, else the folder's name.
    // GE-Proton, UMU-Proton, Proton-CachyOS, Proton (Valve's), Proton (other builds), Wine,
    // Wine-Staging, Wine-Staging-TkG, Wine-GE; components DXVK, VKD3D-Proton, DXVK-NVAPI; umu-launcher.
    std::string family;
    std::string version;   // The `version` file's name ("GE-Proton11-11"), else the folder's.
    Kind kind = Kind::proton;
    Origin origin = Origin::launchme;
    std::filesystem::path path;               // The folder; empty for umu's automatic tools.
    std::filesystem::path proton;             // <path>/proton (Proton tools).
    std::filesystem::path wine, wineserver;   // bin/wine (Wine), files/bin/wine (Proton); may be empty.
    std::string umu_protonpath;               // umu's automatic tools: PROTONPATH ("GE-Proton"; "" = UMU-Proton).
    std::string release;                      // "<source>:<tag>" when Launchme installed it.
    bool removable = false;                   // Launchme's own, or in the user's Steam compatibilitytools.d.
    bool deprecated = false;                  // Wine-GE / Lutris-GE: replaced by GE-Proton through umu.
    uint64_t modified = 0;                    // The folder's change time (Unix seconds).
};

// Where tools are looked for; systemRoots() is this machine's, tests build their own.
struct Roots {
    std::filesystem::path config;                       // Launchme's data folder.
    std::filesystem::path home;
    std::vector<std::filesystem::path> steam_roots;     // Steam installations (native, Flatpak, Snap).
    std::vector<std::filesystem::path> steam_libraries; // Their library folders (with steamapps/).
    std::vector<std::filesystem::path> system_compat;   // /usr/share/steam/compatibilitytools.d, ...
    std::filesystem::path system_wine, system_wineserver;
    std::filesystem::path umu_run;                      // umu-run on PATH or Launchme's own; empty: none.
};
Roots systemRoots();

// <config>/compatibilitytools and its parts.
std::filesystem::path toolsFolder(const std::filesystem::path& config);
std::filesystem::path componentsFolder(const std::filesystem::path& config);
// The user's Steam compatibilitytools.d (Instaluj dla Steam), empty without Steam.
std::filesystem::path steamCompatFolder(const Roots& roots);

// Every installed tool and component, best first within each kind (automatic choice order).
std::vector<Tool> discover(const Roots& roots);
inline std::vector<Tool> discover() { return discover(systemRoots()); }
// Proton and Wine tools only (no components, no umu-launcher), as the version list shows them.
std::vector<Tool> runners(const std::vector<Tool>& tools);
// The automatic default: the newest GE-Proton, else UMU-Proton (installed, else umu's automatic),
// else Steam's newest Proton, else another Proton, else a Wine build, else Wine on PATH.
std::optional<Tool> automatic(const std::vector<Tool>& tools);
// `id` ("default": automatic). A tool that is gone falls back to automatic; `fell_back` tells.
std::optional<Tool> resolve(const std::vector<Tool>& tools, std::string_view id, bool* fell_back = nullptr);
// The newest installed component of a family ("DXVK", "VKD3D-Proton", "DXVK-NVAPI").
std::optional<Tool> component(const std::vector<Tool>& tools, std::string_view family);
// A Steam runtime app ("Proton EasyAntiCheat Runtime", "Proton BattlEye Runtime") in a library.
std::filesystem::path steamApp(const Roots& roots, std::wstring_view folder);
// Natural version order: GE-Proton11-11 > GE-Proton9-20, "Proton 10.0" > "Proton 9.0". <0, 0, >0.
int compareVersions(std::string_view a, std::string_view b);

// umu-database (github.com/Open-Wine-Components/umu-database): its CSV cached for a day in
// <config>/compatibilitytools/cache/umu-database.csv.
std::filesystem::path umuDatabaseFile(const std::filesystem::path& config);
// The UMU_ID of a store's game ("egs", "Catnip" → "umu-397540"); empty when not listed. Amazon IDs
// match with or without "amzn1.adg.product.".
std::string umuId(std::string_view csv, std::string_view store, std::string_view codename);
// GAMEID for a launch: the options' umu_id, else Steam's umu-<app ID>, else the cached database's
// entry, else "umu-default". Reads the cached file only (no network).
std::string gameId(const CompatLaunch& launch, const std::filesystem::path& config);
// Downloads the CSV when the copy is older than a day (or `force`); false when it could not.
bool refreshUmuDatabase(const std::filesystem::path& config, bool force = false);

// Downloadable releases.
struct Source {
    const char* id;      // "ge-proton", "umu-proton", "proton-cachyos", "wine-kron4ek", "wine-staging-tkg-kron4ek",
                         // "dxvk", "vkd3d-proton", "dxvk-nvapi", "umu-launcher"
    const wchar_t* name; // "GE-Proton", ...
    const char* repository; // "GloriousEggroll/proton-ge-custom"
    Kind kind;
    const char* family;
};
std::span<const Source> sources();
const Source* findSource(std::string_view id);

enum class Digest { none, sha256, sha512 };
struct Release {
    std::string source;        // Source::id
    std::string tag;           // "GE-Proton11-11"
    std::wstring name;         // "GE-Proton11-11", "Wine-Staging-TkG 11.20"
    std::string published;     // "2026-10-19"
    std::string asset;         // The archive's file name.
    std::wstring url;          // Its download URL.
    uint64_t size = 0;
    std::string checksum_asset;
    std::wstring checksum_url; // Empty when the release publishes none.
    Digest digest = Digest::none;
    std::wstring page;         // The release's page (notes).
};
struct Platform { bool x86_64_v3 = false; }; // Proton-CachyOS: its x86-64-v3 build.
Platform thisPlatform();
// GitHub's /releases JSON: drafts and prereleases skipped, one archive per release for x86-64.
std::vector<Release> parseReleases(const Source& source, std::string_view json, const Platform& platform);
struct ReleaseList {
    std::vector<Release> releases;
    bool stale = false;        // GitHub did not answer (or refused: 60 requests an hour): the cached list.
    bool available = true;     // False: neither GitHub nor a cached list.
};
using Fetch = std::function<HttpResponse(const std::wstring& url)>;
// The list, from the hour-old cache (<config>/compatibilitytools/cache/<source>.json) unless `refresh`.
ReleaseList releases(const Source& source, bool refresh = false);
ReleaseList releases(const Source& source, const std::filesystem::path& cache_folder, const Fetch& fetch, bool refresh, int64_t now,
                     const Platform& platform);
// Whether a release is installed already (Launchme's marker, or a GE/UMU-Proton folder of its tag).
bool installed(const Release& release, const std::vector<Tool>& tools);
// Aktualizuj automatycznie (GE-Proton): the newest release of the list when it is not installed yet and
// GE-Proton is in use here (one installed, not umu's own); nullopt otherwise or without a list.
std::optional<Release> autoUpdate(const ReleaseList& list, const std::vector<Tool>& tools);
// At most once a day: true when the stamp in <config>/compatibilitytools/cache is older (and renews it).
bool autoUpdateDue(const std::filesystem::path& config, int64_t now);

enum class Error { none, cancelled, network, checksum, archive, unsafe_archive, layout, storage, exists, busy, unsupported };
enum class Target { launchme, steam }; // Steam: compatibilitytools.d (Proton tools only).
struct Progress {
    enum class Phase { download, verify, extract } phase = Phase::download;
    uint64_t done = 0, total = 0;
};
using Cancel = std::function<bool()>;
using OnProgress = std::function<void(const Progress&)>;
struct InstallResult {
    Error error = Error::none;
    std::filesystem::path folder; // The installed tool's folder.
};
// Downloads (resuming a partial file), verifies, extracts and installs a release; `cancel` keeps the
// partial download for the next attempt.
InstallResult install(const Release& release, Target target, const Cancel& cancel, const OnProgress& progress);
InstallResult install(const Release& release, Target target, const Cancel& cancel, const OnProgress& progress, const Roots& roots);
// Anti-cheat runtimes without Steam, as Heroic gets them: Lutris' list (lutris.net/api/runtimes) names
// eac_runtime and battleye_runtime, their archives on github.com/lutris. Installed into
// <config>/compatibilitytools/runtimes/<name> (its single top folder stripped: the folder
// PROTON_EAC_RUNTIME / PROTON_BATTLEYE_RUNTIME name) with the list's created_at in its `version` file.
// Lutris publishes no checksums: only HTTPS GitHub archives of the lutris organisation are taken.
inline constexpr std::string_view eac_runtime = "eac_runtime", battleye_runtime = "battleye_runtime";
struct Runtime { std::string name; std::wstring url; std::string created; };
inline const wchar_t* runtimes_url = L"https://lutris.net/api/runtimes";
std::vector<Runtime> parseRuntimes(std::string_view json); // The two anti-cheat entries only.
std::filesystem::path runtimesFolder(const std::filesystem::path& config);
// Launchme's installed runtime folder; empty when missing.
std::filesystem::path installedRuntime(const std::filesystem::path& config, std::string_view name);
// The list, then the archive (or `fetch`'s answers in tests); an installed copy of the list's version stays.
Error installRuntime(std::string_view name, const std::filesystem::path& config, const Fetch& fetch, const Cancel& cancel);
Error installRuntime(std::string_view name, const Cancel& cancel);

// Removes a removable tool (atomically renamed away first); false otherwise.
bool remove(const Tool& tool, const Roots& roots);
inline bool remove(const Tool& tool) { return remove(tool, systemRoots()); }

// Polish and English text for the settings UI (launch_options_catalog::text picks the language).
launch_options_catalog::Text originName(Origin origin); // "Steam", "Launchme", "umu-launcher (downloads it itself)"...
launch_options_catalog::Text errorText(Error error);    // "Checksum mismatch"...

// Lower level, for tests. A checksum file's digest for `asset` ("<hex>  <name>" lines, or one bare
// digest); empty when absent.
std::string checksumFor(std::string_view checksum_file, std::string_view asset);
std::string fileDigest(const std::filesystem::path& file, Digest digest, const Cancel& cancel = {});
// Extracts into `destination` (an existing empty folder); unsafe_archive for an entry that would
// escape it, archive for a damaged file.
Error extractArchive(const std::filesystem::path& archive, const std::filesystem::path& destination, const Cancel& cancel,
                     const std::function<void(uint64_t done, uint64_t total)>& progress);
// Extracts `archive` beside `folder`'s tools, checks the layout of `kind`, and renames its single
// top-level folder (or everything) to <folder>/<name> (`name` empty: the archive's own top folder).
Error installArchive(const std::filesystem::path& archive, const std::filesystem::path& folder, Kind kind, const std::string& name,
                     const Cancel& cancel, const std::function<void(uint64_t, uint64_t)>& progress, std::filesystem::path* installed);
}
