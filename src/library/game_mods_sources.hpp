#pragma once
// Where the mods come from, read from the sources' own pages and lists (pure parsers; the downloads
// are in game_mods_install.cpp):
//   ReShade     https://reshade.me/ links downloads/ReShade_Setup_<version>_Addon.exe (an .exe with a ZIP
//               appended: ReShade64.dll, ReShade32.dll); its shaders from crosire/reshade-shaders' slim branch.
//   RenoDX      clshortfuse/renodx's snapshot release: games-index.json (games by Steam app ID and title,
//               their add-ons renodx-<id>.addon64/.addon32 in the same release, Unreal's and Unity's generic ones).
//   OptiScaler  optiscaler/OptiScaler's latest release on GitHub (a .7z).
//   DLSS        RHI's dlss_manifest.json (RankFTW/RHI): NVIDIA's DLLs by version, each a ZIP of one DLL.
//   DLSS 5      RankFTW/rhi-repo's newest renodx-dlss5-<version> release (renodx-dlss5.addon64 in a ZIP)
//               with the manifest's newest nvngx_dlssnr.dll.
//   ANR FG      apex-compute/ANR's latest release: anr-fg-<version>-full.zip / -addon.zip and SHA256SUMS.txt.
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace game_mods::sources {
inline constexpr std::wstring_view reshade_page = L"https://reshade.me/";
inline constexpr std::wstring_view reshade_shaders = L"https://github.com/crosire/reshade-shaders/archive/refs/heads/slim.zip";
inline constexpr std::wstring_view renodx_index = L"https://github.com/clshortfuse/renodx/releases/download/snapshot/games-index.json";
inline constexpr std::wstring_view renodx_files = L"https://github.com/clshortfuse/renodx/releases/download/snapshot/";
inline constexpr std::wstring_view optiscaler_latest = L"https://api.github.com/repos/optiscaler/OptiScaler/releases/latest";
inline constexpr std::wstring_view dlss_manifest = L"https://raw.githubusercontent.com/RankFTW/RHI/main/dlss_manifest.json";
inline constexpr std::wstring_view rhi_releases = L"https://api.github.com/repos/RankFTW/rhi-repo/releases?per_page=60";
inline constexpr std::wstring_view anr_latest = L"https://api.github.com/repos/apex-compute/ANR/releases/latest";

struct Download {
    std::string version;
    std::wstring url;
    std::string file; // The asset's file name.
    explicit operator bool() const { return !url.empty(); }
};

// reshade.me's page: the add-on build's setup (absolute URL).
Download reshadeSetup(std::string_view html);

// A GitHub release (the API's JSON): its tag and the first asset whose name ends with `suffix`
// (case ignored); also the asset named exactly `named` when given.
struct Asset { std::string name; std::wstring url; uint64_t size = 0; };
struct Release { std::string tag; std::vector<Asset> assets; const Asset* find(std::string_view suffix) const; };
std::optional<Release> release(std::string_view json);
// The first release in a list whose tag starts with `prefix` and is not a release candidate.
std::optional<Release> newestTagged(std::string_view json, std::string_view prefix);

// RHI's DLSS manifest: the newest of a kind ("dlss", "dlssd", "dlssg", "dlssnr"); with several of the same
// version, `prefer` in the version ("nvidia": NVIDIA's own signed build) wins.
Download dlss(std::string_view json, std::string_view kind, std::string_view prefer = {});

// RenoDX's games-index.json.
struct RenoMod {
    std::string id, file;  // "1000xresist", "renodx-1000xresist.addon64"
    std::wstring title;
    std::vector<std::wstring> aliases; // The game's other names.
    uint32_t steam_appid = 0;
    bool x86 = false;
    std::string category;  // "game", "engine"
    std::string status;    // "stable", "beta", "experimental", or empty
};
std::vector<RenoMod> renodxIndex(std::string_view json);
// The game's add-on for the program's bitness: by Steam app ID, else by its title (or an alias) ignoring
// case, punctuation and trademark signs. An engine's generic add-on by its ID ("unrealengine").
std::optional<RenoMod> renodxMatch(const std::vector<RenoMod>& index, uint32_t steam_appid, std::wstring_view title, bool x86);
std::optional<RenoMod> renodxGeneric(const std::vector<RenoMod>& index, std::string_view id, bool x86);
// "Hollow Knight: Silksong™" -> "hollowknightsilksong"
std::wstring titleKey(std::wstring_view title);

// SHA256SUMS.txt: the digest written for a file (lowercase), empty when it is not listed.
std::string checksum(std::string_view sums, std::string_view file);
} // namespace game_mods::sources
