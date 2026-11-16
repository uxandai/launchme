// Linux only: compatibility tools without the network. Discovery from fixture folders (Launchme's,
// Steam's compatibilitytools.d and libraries, Heroic, Lutris, system Wine, umu), the automatic
// choice, GitHub release lists (fixture JSON, cache and stale answers), checksums, umu-database,
// and safe archive extraction: entries that would escape (../, absolute, links out, devices) are
// refused and nothing lands outside.
#include "../src/library/compat_tools.hpp"

#include <archive.h>
#include <archive_entry.h>

#include <algorithm>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>
#include <utime.h>
#include <vector>

namespace fs = std::filesystem;
using namespace compat_tools;

std::string readText(const std::filesystem::path& file) {
    std::ifstream input(file, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(input), {});
}

namespace {
int failures = 0;
void check(bool condition, const std::string& what) {
    if (!condition) { ++failures; std::cerr << "FAILED: " << what << '\n'; }
}
void write(const fs::path& file, const std::string& text, bool executable = false) {
    fs::create_directories(file.parent_path());
    std::ofstream(file, std::ios::binary) << text;
    if (executable) fs::permissions(file, fs::perms::owner_all, fs::perm_options::add);
}
void protonFolder(const fs::path& folder, const std::string& version, const std::string& display = {}) {
    write(folder / "proton", "#!/usr/bin/env python3\n", true);
    write(folder / "files" / "bin" / "wine", "", true);
    write(folder / "files" / "bin" / "wineserver", "", true);
    write(folder / "toolmanifest.vdf", "\"manifest\" { \"commandline\" \"/proton %verb%\" }\n");
    if (!version.empty()) write(folder / "version", "1760000000 " + version + "\n");
    if (!display.empty())
        write(folder / "compatibilitytool.vdf", "\"compatibilitytools\"\n{\n  \"compat_tools\"\n  {\n    \"" + folder.filename().string() +
              "\" // Internal name\n    {\n      \"install_path\" \".\"\n      \"display_name\" \"" + display +
              "\"\n      \"from_oslist\" \"windows\"\n      \"to_oslist\" \"linux\"\n    }\n  }\n}\n");
}
const Tool* byId(const std::vector<Tool>& tools, const std::string& id) {
    for (const auto& tool : tools) if (tool.id == id) return &tool;
    return nullptr;
}

void texts() {
    for (int origin = 0; origin <= static_cast<int>(Origin::umu); ++origin)
        check(*originName(static_cast<Origin>(origin)).polish && *originName(static_cast<Origin>(origin)).english, "origin text");
    for (int error = 0; error <= static_cast<int>(Error::unsupported); ++error)
        check(*errorText(static_cast<Error>(error)).polish && *errorText(static_cast<Error>(error)).english, "error text");
}

void versions() {
    check(compareVersions("GE-Proton11-11", "GE-Proton9-20") > 0 && compareVersions("GE-Proton10-3", "GE-Proton10-25") < 0, "GE versions");
    check(compareVersions("Proton 10.0", "Proton 9.0") > 0 && compareVersions("Proton - Experimental", "Proton 9.0") < 0, "Steam's Protons");
    check(compareVersions("dxvk-3.1.1", "dxvk-3.1") > 0 && compareVersions("same", "same") == 0, "components");
}

void discovery(const fs::path& root) {
    Roots roots;
    roots.config = root / "config";
    roots.home = root / "home";
    const auto steam = roots.home / ".local" / "share" / "Steam";
    roots.steam_roots = {steam};
    roots.steam_libraries = {steam, root / "library2"};
    roots.system_compat = {root / "usr-share-compat"};
    roots.system_wine = "/usr/bin/wine";
    roots.system_wineserver = "/usr/bin/wineserver";
    // Launchme's: a GE-Proton, a Wine build, components and umu-launcher; Steam's: GE, UMU, Proton.
    protonFolder(roots.config / "compatibilitytools" / "GE-Proton10-25", "GE-Proton10-25");
    write(roots.config / "compatibilitytools" / "GE-Proton10-25" / ".launchme-release", "ge-proton:GE-Proton10-25\n");
    write(roots.config / "compatibilitytools" / "wine-11.20-staging-tkg-amd64-wow64" / "bin" / "wine", "", true);
    write(roots.config / "compatibilitytools" / "wine-11.20-staging-tkg-amd64-wow64" / "bin" / "wineserver", "", true);
    fs::create_directories(roots.config / "compatibilitytools" / "components" / "dxvk-3.1" / "x64");
    fs::create_directories(roots.config / "compatibilitytools" / "components" / "dxvk-3.1.1" / "x64");
    fs::create_directories(roots.config / "compatibilitytools" / "components" / "dxvk-nvapi-v0.9.2" / "x64");
    fs::create_directories(roots.config / "compatibilitytools" / "components" / "vkd3d-proton-3.1" / "x64");
    fs::create_directories(roots.config / "compatibilitytools" / "components" / "unknown-1.0" / "x64");
    fs::create_directories(roots.config / "compatibilitytools" / "downloads");
    fs::create_directories(roots.config / "compatibilitytools" / "not-a-tool");
    write(roots.config / "compatibilitytools" / "umu-launcher-1.4.4" / "umu-run", "", true);
    protonFolder(steam / "compatibilitytools.d" / "GE-Proton11-11", "GE-Proton11-11", "GE-Proton11-11 (custom)");
    protonFolder(steam / "compatibilitytools.d" / "UMU-Proton-10.0-4", "UMU-Proton-10.0-4");
    fs::create_directories(steam / "compatibilitytools.d" / "Boxtron"); // Not Proton: skipped.
    protonFolder(steam / "steamapps" / "common" / "Proton 9.0", "proton-9.0-4");
    protonFolder(root / "library2" / "steamapps" / "common" / "Proton 10.0", "proton-10.0-1");
    protonFolder(root / "library2" / "steamapps" / "common" / "Proton - Experimental", "experimental-10.0");
    fs::create_directories(root / "library2" / "steamapps" / "common" / "Proton EasyAntiCheat Runtime");
    protonFolder(root / "usr-share-compat" / "proton-cachyos-11.0-20261015-slr-x86_64", "proton-cachyos-11.0");
    protonFolder(roots.home / ".config" / "heroic" / "tools" / "proton" / "GE-Proton9-20", "GE-Proton9-20");
    write(roots.home / ".local" / "share" / "lutris" / "runners" / "wine" / "wine-ge-8-26-x86_64" / "bin" / "wine", "", true);

    auto tools = discover(roots);
    const auto* ge = byId(tools, "steam:GE-Proton11-11");
    check(ge && ge->kind == Kind::proton && ge->family == "GE-Proton" && ge->name == L"GE-Proton11-11 (custom)" && ge->version == "GE-Proton11-11" &&
          ge->origin == Origin::steam_compat && ge->removable && ge->proton == ge->path / "proton" && ge->wine == ge->path / "files" / "bin" / "wine",
          "Steam's compatibilitytools.d GE-Proton with its display name");
    const auto* own = byId(tools, "launchme:GE-Proton10-25");
    check(own && own->release == "ge-proton:GE-Proton10-25" && own->removable && own->origin == Origin::launchme, "Launchme's GE-Proton and its marker");
    const auto* umu_proton = byId(tools, "steam:UMU-Proton-10.0-4");
    check(umu_proton && umu_proton->family == "UMU-Proton", "UMU-Proton");
    const auto* valve = byId(tools, "steam-common:Proton 10.0");
    check(valve && valve->origin == Origin::steam_official && !valve->removable && valve->family == "Proton" && byId(tools, "steam-common:Proton 9.0") &&
          byId(tools, "steam-common:Proton - Experimental") && !byId(tools, "steam-common:Proton EasyAntiCheat Runtime"), "Steam's own Protons in every library");
    const auto* cachy = byId(tools, "steam-system:proton-cachyos-11.0-20261015-slr-x86_64");
    check(cachy && cachy->family == "Proton-CachyOS" && !cachy->removable, "the system's compatibilitytools.d");
    check(byId(tools, "heroic:GE-Proton9-20") && !byId(tools, "heroic:GE-Proton9-20")->removable, "Heroic's Protons");
    const auto* wine = byId(tools, "launchme:wine-11.20-staging-tkg-amd64-wow64");
    check(wine && wine->kind == Kind::wine && wine->family == "Wine-Staging-TkG" && wine->wineserver == wine->path / "bin" / "wineserver", "Kron4ek's Wine");
    const auto* lutris = byId(tools, "lutris:wine-ge-8-26-x86_64");
    check(lutris && lutris->deprecated && lutris->family == "Wine-GE", "Lutris' Wine-GE (deprecated)");
    const auto* system = byId(tools, "system:wine");
    check(system && system->wine == "/usr/bin/wine" && system->wineserver == "/usr/bin/wineserver" && !system->removable, "system Wine");
    check(byId(tools, "launchme:umu-launcher-1.4.4") && byId(tools, "launchme:umu-launcher-1.4.4")->kind == Kind::umu, "Launchme's umu-launcher");
    check(!byId(tools, "launchme:not-a-tool") && !byId(tools, "launchme:downloads") && !byId(tools, "steam:Boxtron") && !byId(tools, "component:unknown-1.0"),
          "folders that are not tools");
    check(!byId(tools, "umu:GE-Proton"), "no umu tools without umu-run");
    // Components: the newest of each family.
    check(component(tools, "DXVK") && component(tools, "DXVK")->path.filename() == "dxvk-3.1.1" && component(tools, "DXVK")->version == "3.1.1" &&
          component(tools, "VKD3D-Proton") && component(tools, "DXVK-NVAPI") && component(tools, "DXVK-NVAPI")->version == "0.9.2" &&
          !component(tools, "Nothing"), "components");
    // Order: Protons first (GE newest first), then Wine, components, umu-launcher.
    const auto list = runners(tools);
    check(!list.empty() && list.front().id == "steam:GE-Proton11-11", "newest GE-Proton first: " + (list.empty() ? std::string() : list.front().id));
    check(std::all_of(list.begin(), list.end(), [](const Tool& t) { return t.kind == Kind::proton || t.kind == Kind::wine; }) &&
          std::is_partitioned(list.begin(), list.end(), [](const Tool& t) { return t.kind == Kind::proton; }), "runners: Protons, then Wine");
    check(steamApp(roots, L"Proton EasyAntiCheat Runtime") == root / "library2" / "steamapps" / "common" / "Proton EasyAntiCheat Runtime" &&
          steamApp(roots, L"Proton BattlEye Runtime").empty(), "Steam's runtime apps");

    // The automatic choice and its fallbacks.
    check(automatic(tools)->id == "steam:GE-Proton11-11", "automatic: the newest GE-Proton");
    bool fell = false;
    check(resolve(tools, "steam-common:Proton 10.0", &fell)->id == "steam-common:Proton 10.0" && !fell, "a chosen tool");
    check(resolve(tools, "launchme:gone", &fell)->id == "steam:GE-Proton11-11" && fell, "a removed tool falls back");
    check(resolve(tools, "default")->id == "steam:GE-Proton11-11", "default is automatic");
    std::vector<Tool> rest;
    for (const auto& tool : tools) if (tool.family != "GE-Proton") rest.push_back(tool);
    check(automatic(rest)->id == "steam:UMU-Proton-10.0-4", "then UMU-Proton");
    std::erase_if(rest, [](const Tool& t) { return t.family == "UMU-Proton"; });
    auto with_umu = rest;
    Tool umu;
    umu.id = "umu:UMU-Proton"; umu.kind = Kind::proton; umu.origin = Origin::umu; umu.family = "UMU-Proton";
    with_umu.push_back(umu);
    check(automatic(with_umu)->id == "umu:UMU-Proton", "then umu's own UMU-Proton");
    check(automatic(rest)->id == "steam-common:Proton 10.0", "then Steam's newest numbered Proton");
    std::erase_if(rest, [](const Tool& t) { return t.origin == Origin::steam_official; });
    check(automatic(rest)->id == "steam-system:proton-cachyos-11.0-20261015-slr-x86_64", "then another Proton");
    std::erase_if(rest, [](const Tool& t) { return t.kind == Kind::proton; });
    check(automatic(rest)->id == "launchme:wine-11.20-staging-tkg-amd64-wow64", "then a Wine build");
    std::erase_if(rest, [](const Tool& t) { return t.origin == Origin::launchme && t.kind == Kind::wine; });
    check(automatic(rest)->id == "lutris:wine-ge-8-26-x86_64", "then another manager's Wine");
    std::erase_if(rest, [](const Tool& t) { return t.origin == Origin::lutris; });
    check(automatic(rest)->id == "system:wine", "then system Wine");
    std::erase_if(rest, [](const Tool& t) { return t.kind == Kind::wine; });
    check(!automatic(rest), "nothing installed");

    // umu-run: its automatic tools appear (after the installed Protons).
    roots.umu_run = "/usr/bin/umu-run";
    tools = discover(roots);
    check(byId(tools, "umu:GE-Proton") && byId(tools, "umu:GE-Proton")->umu_protonpath == "GE-Proton" && byId(tools, "umu:UMU-Proton") &&
          byId(tools, "umu:UMU-Proton")->umu_protonpath.empty() && byId(tools, "umu:UMU-Proton")->path.empty(), "umu's automatic tools");
    const auto ordered = runners(tools);
    const auto first_umu = std::find_if(ordered.begin(), ordered.end(), [](const Tool& t) { return t.origin == Origin::umu; });
    check(first_umu != ordered.end() && std::none_of(first_umu, ordered.end(), [](const Tool& t) { return t.kind == Kind::proton && t.origin != Origin::umu; }),
          "umu's tools after the installed Protons");

    // Removal: only Launchme's and the user's Steam folder, atomically. (`tools` was rediscovered.)
    const auto valve_tool = *byId(tools, "steam-common:Proton 10.0");
    const auto own_tool = *byId(tools, "launchme:GE-Proton10-25");
    const auto ge_tool = *byId(tools, "steam:GE-Proton11-11");
    check(!remove(valve_tool, roots) && fs::exists(valve_tool.path), "Steam's own Proton is not removed");
    check(!remove(*byId(tools, "heroic:GE-Proton9-20"), roots), "Heroic's are not removed");
    Tool forged = own_tool;
    forged.path = root / "home" / "elsewhere";
    fs::create_directories(forged.path);
    check(!remove(forged, roots) && fs::exists(forged.path), "nothing outside the tool folders");
    check(remove(own_tool, roots) && !fs::exists(own_tool.path), "Launchme's GE-Proton removed");
    check(remove(ge_tool, roots) && !fs::exists(ge_tool.path), "a GE-Proton in compatibilitytools.d removed");
    bool leftovers = false;
    for (const auto& entry : fs::directory_iterator(roots.config / "compatibilitytools")) leftovers |= entry.path().filename().string().find("removing") != std::string::npos;
    check(!leftovers, "no half-removed folders");
    check(steamCompatFolder(roots) == steam / "compatibilitytools.d", "Install for Steam folder");
}

constexpr const char* ge_json = R"([
 {"tag_name":"GE-Proton11-12","prerelease":true,"draft":false,"published_at":"2026-10-25T00:00:00Z","assets":[
   {"name":"GE-Proton11-12-x86_64.tar.gz","size":10,"browser_download_url":"https://github.com/GloriousEggroll/proton-ge-custom/releases/download/GE-Proton11-12/GE-Proton11-12-x86_64.tar.gz"}]},
 {"tag_name":"GE-Proton11-11","prerelease":false,"draft":false,"published_at":"2026-10-19T18:20:53Z","html_url":"https://github.com/GloriousEggroll/proton-ge-custom/releases/tag/GE-Proton11-11","assets":[
   {"name":"GE-Proton11-11-aarch64.sha512sum","size":160,"browser_download_url":"https://github.com/GloriousEggroll/proton-ge-custom/releases/download/GE-Proton11-11/GE-Proton11-11-aarch64.sha512sum"},
   {"name":"GE-Proton11-11-aarch64.tar.gz","size":648071663,"browser_download_url":"https://github.com/GloriousEggroll/proton-ge-custom/releases/download/GE-Proton11-11/GE-Proton11-11-aarch64.tar.gz"},
   {"name":"GE-Proton11-11-x86_64.sha512sum","size":159,"browser_download_url":"https://github.com/GloriousEggroll/proton-ge-custom/releases/download/GE-Proton11-11/GE-Proton11-11-x86_64.sha512sum"},
   {"name":"GE-Proton11-11-x86_64.tar.gz","size":564789552,"browser_download_url":"https://github.com/GloriousEggroll/proton-ge-custom/releases/download/GE-Proton11-11/GE-Proton11-11-x86_64.tar.gz"}]},
 {"tag_name":"GE-Proton9-20","prerelease":false,"draft":false,"published_at":"2024-11-17T00:00:00Z","assets":[
   {"name":"GE-Proton9-20.sha512sum","size":150,"browser_download_url":"https://github.com/GloriousEggroll/proton-ge-custom/releases/download/GE-Proton9-20/GE-Proton9-20.sha512sum"},
   {"name":"GE-Proton9-20.tar.gz","size":450000000,"browser_download_url":"https://github.com/GloriousEggroll/proton-ge-custom/releases/download/GE-Proton9-20/GE-Proton9-20.tar.gz"}]},
 {"tag_name":"GE-Proton9-19","prerelease":false,"draft":false,"assets":[
   {"name":"GE-Proton9-19.tar.gz","size":1,"browser_download_url":"https://evil.example/GE-Proton9-19.tar.gz"}]},
 {"tag_name":"draft","draft":true,"assets":[]}
])";

void releaseParsing() {
    const auto& ge = *findSource("ge-proton");
    auto list = parseReleases(ge, ge_json, {});
    check(list.size() == 2, "drafts, prereleases and foreign hosts skipped: " + std::to_string(list.size()));
    if (list.size() == 2) {
        check(list[0].tag == "GE-Proton11-11" && list[0].asset == "GE-Proton11-11-x86_64.tar.gz" && list[0].size == 564789552 &&
              list[0].checksum_asset == "GE-Proton11-11-x86_64.sha512sum" && list[0].digest == Digest::sha512 && list[0].published == "2026-10-19" &&
              list[0].name == L"GE-Proton11-11" && list[0].page.starts_with(L"https://github.com/"), "GE-Proton x86-64 archive and checksum");
        check(list[1].asset == "GE-Proton9-20.tar.gz" && list[1].checksum_asset == "GE-Proton9-20.sha512sum", "older GE-Proton names");
    }
    const std::string cachy = R"([{"tag_name":"cachyos-11.0-20261015-slr","assets":[
        {"name":"proton-cachyos-11.0-20261015-slr-arm64.tar.xz","size":5,"browser_download_url":"https://github.com/CachyOS/proton-cachyos/releases/download/x/proton-cachyos-11.0-20261015-slr-arm64.tar.xz"},
        {"name":"proton-cachyos-11.0-20261015-slr-x86_64.sha512sum","size":177,"browser_download_url":"https://github.com/CachyOS/proton-cachyos/releases/download/x/proton-cachyos-11.0-20261015-slr-x86_64.sha512sum"},
        {"name":"proton-cachyos-11.0-20261015-slr-x86_64.tar.xz","size":6,"browser_download_url":"https://github.com/CachyOS/proton-cachyos/releases/download/x/proton-cachyos-11.0-20261015-slr-x86_64.tar.xz"},
        {"name":"proton-cachyos-11.0-20261015-slr-x86_64_v3.sha512sum","size":180,"browser_download_url":"https://github.com/CachyOS/proton-cachyos/releases/download/x/proton-cachyos-11.0-20261015-slr-x86_64_v3.sha512sum"},
        {"name":"proton-cachyos-11.0-20261015-slr-x86_64_v3.tar.xz","size":7,"browser_download_url":"https://github.com/CachyOS/proton-cachyos/releases/download/x/proton-cachyos-11.0-20261015-slr-x86_64_v3.tar.xz"}]}])";
    list = parseReleases(*findSource("proton-cachyos"), cachy, {false});
    check(list.size() == 1 && list[0].asset == "proton-cachyos-11.0-20261015-slr-x86_64.tar.xz" && list[0].name == L"Proton-CachyOS 11.0-20261015-slr" &&
          list[0].checksum_asset == "proton-cachyos-11.0-20261015-slr-x86_64.sha512sum", "Proton-CachyOS generic build");
    list = parseReleases(*findSource("proton-cachyos"), cachy, {true});
    check(list.size() == 1 && list[0].asset.ends_with("_v3.tar.xz") && list[0].checksum_asset.ends_with("_v3.sha512sum"), "Proton-CachyOS x86-64-v3 build");
    const std::string kron4ek = R"([{"tag_name":"11.20","assets":[
        {"name":"sha256sums.txt","size":882,"browser_download_url":"https://github.com/Kron4ek/Wine-Builds/releases/download/11.20/sha256sums.txt"},
        {"name":"wine-11.20-amd64-wow64.tar.xz","size":100025976,"browser_download_url":"https://github.com/Kron4ek/Wine-Builds/releases/download/11.20/wine-11.20-amd64-wow64.tar.xz"},
        {"name":"wine-11.20-amd64.tar.xz","size":102718596,"browser_download_url":"https://github.com/Kron4ek/Wine-Builds/releases/download/11.20/wine-11.20-amd64.tar.xz"},
        {"name":"wine-11.20-staging-tkg-amd64-wow64.tar.xz","size":101579508,"browser_download_url":"https://github.com/Kron4ek/Wine-Builds/releases/download/11.20/wine-11.20-staging-tkg-amd64-wow64.tar.xz"},
        {"name":"wine-11.20-x86.tar.xz","size":59456092,"browser_download_url":"https://github.com/Kron4ek/Wine-Builds/releases/download/11.20/wine-11.20-x86.tar.xz"}]}])";
    list = parseReleases(*findSource("wine-staging-tkg-kron4ek"), kron4ek, {});
    check(list.size() == 1 && list[0].asset == "wine-11.20-staging-tkg-amd64-wow64.tar.xz" && list[0].digest == Digest::sha256 &&
          list[0].checksum_asset == "sha256sums.txt" && list[0].name == L"Wine-Staging-TkG 11.20", "Kron4ek's staging-tkg WoW64 build");
    list = parseReleases(*findSource("wine-kron4ek"), kron4ek, {});
    check(list.size() == 1 && list[0].asset == "wine-11.20-amd64-wow64.tar.xz" && list[0].name == L"Wine 11.20", "Kron4ek's plain WoW64 build");
    const auto component = [](const char* source, const std::string& tag, const std::string& asset) {
        const std::string text = R"([{"tag_name":")" + tag + R"(","assets":[{"name":")" + asset + R"(","size":9,"browser_download_url":"https://github.com/x/y/releases/download/)" +
                                 tag + "/" + asset + R"("},{"name":"dxvk-native-3.1.1-steamrt-sniper.tar.gz","size":9,"browser_download_url":"https://github.com/x/y/releases/download/a/dxvk-native-3.1.1-steamrt-sniper.tar.gz"}]}])";
        const auto parsed = parseReleases(*findSource(source), text, {});
        return parsed.size() == 1 && parsed[0].asset == asset && parsed[0].digest == Digest::none;
    };
    check(component("dxvk", "v3.1.1", "dxvk-3.1.1.tar.gz"), "DXVK release");
    check(component("vkd3d-proton", "v3.1", "vkd3d-proton-3.1.tar.zst"), "VKD3D-Proton release");
    check(component("dxvk-nvapi", "v0.9.2", "dxvk-nvapi-v0.9.2.tar.gz"), "DXVK-NVAPI release");
    check(component("umu-launcher", "1.4.4", "umu-launcher-1.4.4-zipapp.tar"), "umu-launcher zipapp");
    check(parseReleases(ge, "{}", {}).empty() && parseReleases(ge, "not json", {}).empty(), "not a release list");

    // Installed: Launchme's marker, or a GE folder of the tag elsewhere.
    Release release;
    release.source = "ge-proton"; release.tag = "GE-Proton11-11"; release.asset = "GE-Proton11-11-x86_64.tar.gz";
    Tool tool;
    tool.path = "/x/GE-Proton11-11";
    check(installed(release, {tool}), "installed by folder name");
    tool.path = "/x/other"; tool.release = "ge-proton:GE-Proton11-11";
    check(installed(release, {tool}) && !installed(release, {}), "installed by marker");

    // Aktualizuj automatycznie: the newest GE-Proton when one is in use and the newest is missing.
    ReleaseList ge_list;
    ge_list.releases = parseReleases(ge, ge_json, {});
    Tool older;
    older.family = "GE-Proton"; older.path = "/x/GE-Proton9-20";
    const auto next = autoUpdate(ge_list, {older});
    check(next && next->tag == "GE-Proton11-11", "a newer GE-Proton is fetched");
    Tool newest = older; newest.path = "/x/GE-Proton11-11";
    check(!autoUpdate(ge_list, {older, newest}), "the newest already there");
    Tool umu; umu.family = "GE-Proton"; umu.origin = Origin::umu;
    Tool steam; steam.family = "Proton"; steam.path = "/x/Proton 10.0";
    check(!autoUpdate(ge_list, {umu, steam}) && !autoUpdate(ge_list, {}), "no GE-Proton in use: nothing downloaded");
    ReleaseList offline; offline.available = false;
    check(!autoUpdate(offline, {older}), "no list: nothing");
}

void releaseCache(const fs::path& root) {
    const auto& ge = *findSource("ge-proton");
    const auto cache = root / "cache";
    int calls = 0;
    HttpResponse answer;
    answer.status = 200;
    answer.body.assign(ge_json, ge_json + std::char_traits<char>::length(ge_json));
    const Fetch fetch = [&](const std::wstring& url) {
        ++calls;
        check(url == L"https://api.github.com/repos/GloriousEggroll/proton-ge-custom/releases?per_page=15", "GitHub API URL");
        return answer;
    };
    const auto now = static_cast<int64_t>(std::time(nullptr));
    auto list = releases(ge, cache, fetch, false, now, {});
    check(calls == 1 && list.releases.size() == 2 && !list.stale && list.available, "first list from GitHub");
    check(fs::exists(cache / "GloriousEggroll_proton-ge-custom.json"), "cached per repository");
    list = releases(ge, cache, fetch, false, now + 600, {});
    check(calls == 1 && list.releases.size() == 2, "within the hour: the cache");
    list = releases(ge, cache, fetch, true, now + 30, {});
    check(calls == 1, "a refresh within a minute: still the cache");
    list = releases(ge, cache, fetch, true, now + 120, {});
    check(calls == 2 && !list.stale, "a refresh asks GitHub again");
    answer.status = 403; // Rate limited.
    list = releases(ge, cache, fetch, false, now + 7200, {});
    check(calls == 3 && list.stale && list.releases.size() == 2, "GitHub refuses: the stale list");
    list = releases(ge, root / "empty-cache", fetch, false, now, {});
    check(!list.available && list.releases.empty(), "no answer, no cache");
}

void checksums(const fs::path& root) {
    const std::string sum = "5e1e7c99e773b4d0757f90075c5ff7f5282bedd13a694fc0414486122e68c9452d3fb971f32ab32704d4463c9622475a44e58fbd014069aa1e84e27b4a4ed7e5";
    check(checksumFor(sum + "  UMU-Proton-10.0-4.tar.gz\n", "UMU-Proton-10.0-4.tar.gz") == sum, "a .sha512sum file");
    check(checksumFor("aaaa" + sum.substr(4) + " *other.tar.gz\n" + sum + "  ./dir/UMU-Proton-10.0-4.tar.gz\n", "UMU-Proton-10.0-4.tar.gz") == sum, "a list of sums");
    check(checksumFor(sum + "\n", "x.tar.gz") == sum && checksumFor("nothing here\n", "x.tar.gz").empty() && checksumFor(sum + "  y.tar.gz\n", "x.tar.gz").empty(),
          "a bare sum, or none");
    write(root / "abc.txt", "abc");
    check(fileDigest(root / "abc.txt", Digest::sha256) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "SHA-256");
    check(fileDigest(root / "abc.txt", Digest::sha512) ==
          "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f", "SHA-512");
    check(fileDigest(root / "missing", Digest::sha256).empty() && fileDigest(root / "abc.txt", Digest::sha256, [] { return true; }).empty(), "unreadable or cancelled");
}

void umuDatabase(const fs::path& root) {
    const std::string csv = "TITLE,STORE,CODENAME,UMU_ID,COMMON ACRONYM (Optional),NOTE (Optional),EXE_STRINGS (Optional)\n"
                            "Borderlands 3,egs,Catnip,umu-397540,bl3,,\n"
                            "\"Warhammer 40,000: Space Marine\",gog,1668484481,umu-55150,,,\n"
                            "Trackmania,egs,Pigeon,umu-2225070,tm,\"note, with \"\"quotes\"\"\nand a line\",\n"
                            "Star Wars,amazon,0127edd7-4a35-4991-ab79-ff7601a699da,umu-6020,,,\n"
                            "Case,egs,MixedCase,umu-1,,,\r\n"
                            "Last,gog,42,umu-42,,,";
    check(umuId(csv, "egs", "Catnip") == "umu-397540" && umuId(csv, "gog", "1668484481") == "umu-55150", "plain and quoted rows");
    check(umuId(csv, "egs", "Pigeon") == "umu-2225070", "a quoted field with a line break");
    check(umuId(csv, "amazon", "amzn1.adg.product.0127edd7-4a35-4991-ab79-ff7601a699da") == "umu-6020", "Amazon IDs with their prefix");
    check(umuId(csv, "egs", "mixedcase") == "umu-1" && umuId(csv, "gog", "42") == "umu-42", "case and the last row");
    check(umuId(csv, "gog", "Catnip").empty() && umuId(csv, "", "Catnip").empty() && umuId(csv, "egs", "").empty(), "other stores and empty IDs");
    const auto config = root / "umu-config";
    write(umuDatabaseFile(config), csv);
    CompatLaunch launch;
    launch.store = "egs"; launch.store_id = "Catnip";
    check(gameId(launch, config) == "umu-397540", "GAMEID from the cached database");
    launch.options.umu_id = "umu-mine";
    check(gameId(launch, config) == "umu-mine", "the options' GAMEID wins");
    launch.options.umu_id.clear();
    launch.store = "steam"; launch.steam_app_id = 570;
    check(gameId(launch, config) == "umu-570", "Steam games: umu-<app ID>");
    launch.store.clear(); launch.steam_app_id = 0;
    check(gameId(launch, config) == "umu-default", "otherwise umu-default");
}

// A tar archive written with libarchive. type: 'f' file, 'd' folder, 'l' symlink, 'h' hardlink, 'c' device.
struct Entry {
    std::string path;
    char type = 'f';
    std::string data;
    std::string link;
    int mode = 0644;
};
bool tar(const fs::path& file, const std::vector<Entry>& entries, int filter) {
    struct archive* writer = archive_write_new();
    archive_write_set_format_pax_restricted(writer);
    if (filter == ARCHIVE_FILTER_GZIP) archive_write_add_filter_gzip(writer);
    else if (filter == ARCHIVE_FILTER_XZ) archive_write_add_filter_xz(writer);
    else if (filter == ARCHIVE_FILTER_ZSTD) archive_write_add_filter_zstd(writer);
    if (archive_write_open_filename(writer, file.c_str()) != ARCHIVE_OK) { archive_write_free(writer); return false; }
    for (const auto& item : entries) {
        struct archive_entry* entry = archive_entry_new();
        archive_entry_set_pathname(entry, item.path.c_str());
        archive_entry_set_perm(entry, item.mode);
        switch (item.type) {
        case 'd': archive_entry_set_filetype(entry, AE_IFDIR); break;
        case 'l': archive_entry_set_filetype(entry, AE_IFLNK); archive_entry_set_symlink(entry, item.link.c_str()); break;
        case 'h': archive_entry_set_filetype(entry, AE_IFREG); archive_entry_set_hardlink(entry, item.link.c_str()); break;
        case 'c': archive_entry_set_filetype(entry, AE_IFCHR); archive_entry_set_rdev(entry, makedev(1, 3)); break;
        default: archive_entry_set_filetype(entry, AE_IFREG); archive_entry_set_size(entry, static_cast<la_int64_t>(item.data.size())); break;
        }
        archive_write_header(writer, entry);
        if (item.type == 'f' && !item.data.empty()) archive_write_data(writer, item.data.data(), item.data.size());
        archive_entry_free(entry);
    }
    archive_write_close(writer);
    archive_write_free(writer);
    return true;
}
std::vector<Entry> goodProton(const std::string& top) {
    return {{top + "/", 'd'}, {top + "/proton", 'f', "#!/usr/bin/env python3\n", {}, 0755}, {top + "/files/bin/wine", 'f', "ELF", {}, 04755},
            {top + "/files/lib/", 'd'}, {top + "/files/lib/libx.so.1", 'f', "lib"}, {top + "/files/lib/libx.so", 'l', {}, "libx.so.1"},
            {top + "/files/share/up", 'l', {}, "../lib"}, {top + "/version", 'f', "1 GE-Proton-Test\n"}, {top + "/hard", 'h', {}, top + "/version"}};
}

void archives(const fs::path& root) {
    const auto folder = root / "tools";
    fs::create_directories(folder);
    // Each compression the sources use.
    for (const auto& [filter, suffix] : {std::pair{ARCHIVE_FILTER_GZIP, ".tar.gz"}, std::pair{ARCHIVE_FILTER_XZ, ".tar.xz"}, std::pair{ARCHIVE_FILTER_ZSTD, ".tar.zst"}}) {
        const auto top = std::string("GE-Proton-Test") + (suffix + 1);
        const auto file = root / (top + suffix);
        check(tar(file, goodProton(top), filter), std::string("archive written: ") + suffix);
        fs::path installed;
        uint64_t last = 0;
        const auto error = installArchive(file, folder, Kind::proton, {}, {}, [&](uint64_t done, uint64_t) { last = done; }, &installed);
        check(error == Error::none && installed == fs::canonical(folder) / top, std::string("installed from ") + suffix);
        check(fs::is_regular_file(installed / "proton") && (fs::status(installed / "proton").permissions() & fs::perms::owner_exec) != fs::perms::none &&
              fs::read_symlink(installed / "files" / "lib" / "libx.so") == "libx.so.1" && fs::is_symlink(installed / "files" / "share" / "up") &&
              fs::hard_link_count(installed / "version") == 2 && last > 0, std::string("contents, links and modes from ") + suffix);
        check((fs::status(installed / "files" / "bin" / "wine").permissions() & fs::perms::set_uid) == fs::perms::none, "no set-user-ID bit");
        check(installArchive(file, folder, Kind::proton, {}, {}, {}, nullptr) == Error::exists, std::string("an installed tool is not replaced: ") + suffix);
    }
    // A wrong layout and a forced name.
    const auto wine = root / "wine.tar.xz";
    tar(wine, {{"wine-x/bin/wine", 'f', "ELF", {}, 0755}}, ARCHIVE_FILTER_XZ);
    check(installArchive(wine, folder, Kind::proton, {}, {}, {}, nullptr) == Error::layout, "a Wine build is not a Proton");
    fs::path installed;
    check(installArchive(wine, folder, Kind::wine, "renamed", {}, {}, &installed) == Error::none && installed.filename() == "renamed", "installed under a given name");
    const auto loose = root / "dxvk-loose.tar.gz";
    tar(loose, {{"x64/d3d11.dll", 'f', "MZ"}, {"x32/d3d11.dll", 'f', "MZ"}}, ARCHIVE_FILTER_GZIP);
    check(installArchive(loose, folder, Kind::component, {}, {}, {}, &installed) == Error::none && installed.filename() == "dxvk-loose", "no top folder: the archive's name");
    // Hostile archives: refused, nothing outside, nothing half-installed.
    const auto outside = root / "outside";
    fs::create_directories(outside);
    const std::vector<std::pair<std::string, std::vector<Entry>>> hostile{
        {"dot-dot", {{"t/proton", 'f', "x", {}, 0755}, {"t/../../outside/evil", 'f', "pwned"}}},
        {"absolute", {{"t/proton", 'f', "x", {}, 0755}, {(outside / "evil").string(), 'f', "pwned"}}},
        {"link-out", {{"t/proton", 'f', "x", {}, 0755}, {"t/escape", 'l', {}, "../../outside"}}},
        {"link-absolute", {{"t/proton", 'f', "x", {}, 0755}, {"t/escape", 'l', {}, "/etc"}}},
        {"through-link", {{"t/proton", 'f', "x", {}, 0755}, {"t/self", 'l', {}, "."}, {"t/deep", 'l', {}, "self/self/self/../../.."}}},
        {"hardlink-out", {{"t/proton", 'f', "x", {}, 0755}, {"t/passwd", 'h', {}, "../../outside/target"}}},
        {"device", {{"t/proton", 'f', "x", {}, 0755}, {"t/null", 'c'}}},
    };
    std::ofstream(outside / "target") << "secret";
    for (const auto& [name, entries] : hostile) {
        const auto file = root / (name + ".tar.gz");
        tar(file, entries, ARCHIVE_FILTER_GZIP);
        const auto error = installArchive(file, folder, Kind::proton, name, {}, {}, nullptr);
        check(error == Error::unsafe_archive, "hostile archive refused: " + name + " (" + std::to_string(static_cast<int>(error)) + ")");
        check(!fs::exists(outside / "evil") && !fs::exists(folder / name), "nothing installed or outside: " + name);
    }
    check(fs::hard_link_count(outside / "target") == 1, "no hard link to an outside file");
    bool staging = false;
    for (const auto& entry : fs::directory_iterator(folder)) staging |= entry.path().filename().string().starts_with(".");
    check(!staging, "no staging folder left behind");
    // Damaged and cancelled.
    write(root / "broken.tar.gz", std::string("\x1f\x8b\x08\x00garbage", 12));
    check(installArchive(root / "broken.tar.gz", folder, Kind::proton, "broken", {}, {}, nullptr) == Error::archive, "a damaged archive");
    const auto file = root / "cancel.tar.gz";
    tar(file, goodProton("Cancelled"), ARCHIVE_FILTER_GZIP);
    check(installArchive(file, folder, Kind::proton, {}, [] { return true; }, {}, nullptr) == Error::cancelled && !fs::exists(folder / "Cancelled"), "cancelled");
    // Extraction alone needs an existing folder.
    check(extractArchive(file, root / "no-such-folder", {}, {}) == Error::storage, "no destination");
}

// EAC and BattlEye without Steam: Lutris' list, its GitHub archives only, installed and replaced by version.
void runtimes(const fs::path& root) {
    fs::create_directories(root);
    const std::string list = R"([
        {"id":1,"name":"wine-ge","created_at":"2020-01-01","architecture":"x86_64","url":"https://github.com/lutris/buildbot/releases/download/a/wine.tar.xz"},
        {"id":22,"name":"eac_runtime","created_at":"2024-01-14T19:38:26Z","architecture":"all","url":"https://github.com/lutris/buildbot/releases/download/2022.06.23/eac_runtime-20240114.tar.xz"},
        {"id":16,"name":"battleye_runtime","created_at":"2024-05-21T19:35:07Z","architecture":"all","url":"https://evil.example/battleeye_runtime.tar.xz"},
        {"id":17,"name":"battleye_runtime","created_at":"2024-05-21T19:35:07Z","architecture":"all","url":"https://github.com/someone/x/releases/download/a/battleeye_runtime.tar.xz"}])";
    const auto parsed = parseRuntimes(list);
    check(parsed.size() == 1 && parsed[0].name == "eac_runtime" && parsed[0].created == "2024-01-14T19:38:26Z" &&
          parsed[0].url.ends_with(L"eac_runtime-20240114.tar.xz"), "only Lutris' GitHub anti-cheat archives");
    check(parseRuntimes("not json").empty() && parseRuntimes("{}").empty(), "not a list");

    const auto archive = root / "eac.tar.xz";
    check(tar(archive, {{"eac_runtime/", 'd'}, {"eac_runtime/v2/", 'd'}, {"eac_runtime/v2/lib64/", 'd'},
                        {"eac_runtime/v2/lib64/easyanticheat_x64.so", 'f', "ELF"}}, ARCHIVE_FILTER_XZ), "runtime archive written");
    std::vector<uint8_t> bytes;
    { std::ifstream input(archive, std::ios::binary); bytes.assign(std::istreambuf_iterator<char>(input), {}); }
    std::string answer = list;
    int downloads = 0;
    const Fetch fetch = [&](const std::wstring& url) {
        HttpResponse response;
        response.status = 200;
        if (url == runtimes_url) response.body.assign(answer.begin(), answer.end());
        else if (url.starts_with(L"https://github.com/lutris/")) { response.body = bytes; ++downloads; }
        else response.status = 404;
        return response;
    };
    const auto config = root / "runtime-config";
    check(installedRuntime(config, eac_runtime).empty(), "nothing before");
    check(installRuntime(eac_runtime, config, fetch, {}) == Error::none, "EAC runtime installed");
    const auto folder = installedRuntime(config, eac_runtime);
    check(folder == runtimesFolder(config) / "eac_runtime" && fs::is_regular_file(folder / "v2" / "lib64" / "easyanticheat_x64.so") &&
          readText(folder / "version") == "2024-01-14T19:38:26Z", "its top folder stripped, its version kept");
    check(installRuntime(eac_runtime, config, fetch, {}) == Error::none && downloads == 1, "the same version: no download");
    answer.replace(answer.find("2024-01-14T19:38:26Z"), 20, "2025-02-02T10:00:00Z");
    check(installRuntime(eac_runtime, config, fetch, {}) == Error::none && downloads == 2 && readText(folder / "version") == "2025-02-02T10:00:00Z" &&
          fs::is_regular_file(folder / "v2" / "lib64" / "easyanticheat_x64.so"), "a newer version replaces it");
    check(installRuntime(battleye_runtime, config, fetch, {}) == Error::network && installedRuntime(config, battleye_runtime).empty(),
          "a runtime only on a foreign host: not installed");
    check(installRuntime("wine-ge", config, fetch, {}) == Error::unsupported, "only the anti-cheat runtimes");
    std::vector<std::string> left;
    for (const auto& item : fs::directory_iterator(runtimesFolder(config))) left.push_back(item.path().filename().string());
    check(left == std::vector<std::string>{"eac_runtime"}, "no staging, archive or old copy left");
    Roots only;
    only.config = config;
    const auto tools = discover(only);
    check(std::none_of(tools.begin(), tools.end(), [](const Tool& tool) { return tool.path.filename() == "runtimes"; }), "runtimes are not tools");
}

void installs(const fs::path& root) {
    Roots roots;
    roots.config = root / "install-config";
    Release release;
    release.source = "nothing"; release.asset = "x.tar.gz"; release.url = L"https://github.com/x"; release.size = 1;
    check(install(release, Target::launchme, {}, {}, roots).error == Error::unsupported, "unknown source");
    release.source = "wine-kron4ek";
    check(install(release, Target::steam, {}, {}, roots).error == Error::unsupported, "Wine builds are not Steam tools");
    release.source = "ge-proton"; release.asset = "../x.tar.gz";
    check(install(release, Target::launchme, {}, {}, roots).error == Error::unsupported, "an asset name cannot leave the downloads folder");
    release.asset = "GE-Proton11-11-x86_64.tar.gz";
    check(install(release, Target::steam, {}, {}, roots).error == Error::unsupported, "no Steam, no Install for Steam");
    // Cancelled before any byte: nothing downloaded, the partial file kept for a later try.
    const auto result = install(release, Target::launchme, [] { return true; }, {}, roots);
    check(result.error == Error::cancelled && !fs::exists(toolsFolder(roots.config) / "GE-Proton11-11"), "cancelled download");
}
}

int main() {
    const auto root = fs::temp_directory_path() / ("launchme-compat-tests-" + std::to_string(getpid()));
    fs::create_directories(root);
    texts();
    versions();
    discovery(root / "discovery");
    releaseParsing();
    releaseCache(root / "releases");
    checksums(root);
    umuDatabase(root);
    archives(root / "archives");
    installs(root);
    runtimes(root / "runtimes");
    // Aktualizuj automatycznie looks at most once a day.
    check(autoUpdateDue(root / "auto", 1000000) && !autoUpdateDue(root / "auto", 1000000 + 3600) && autoUpdateDue(root / "auto", 1000000 + 25 * 3600),
          "auto-update once a day");
    std::error_code ignored;
    fs::remove_all(root, ignored);
    if (failures) { std::cerr << failures << " failure(s)\n"; return 1; }
    std::cout << "Compatibility tools: discovery, releases, checksums, umu-database and safe archives passed\n";
    return 0;
}
