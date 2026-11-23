// Opcje uruchamiania → Mody (src/library/game_mods*.hpp): the sources' formats, the record, the INI edits,
// and whole installs and removals in a temporary game folder with every outside effect faked (pages and
// releases served from memory, downloads copied from local fixtures, ANR FG's PowerShell installer played
// by a stand-in): ReShade with its shaders, RenoDX on top, OptiScaler taking dxgi.dll (ReShade moving to
// ReShade64.dll and back), an original DLL backed up and restored, DLSS replaced, anti-cheat asked about,
// ANR FG's checksum and its scripts.
#include "../src/library/game_mods.hpp"
#include "../src/library/game_mods_install.hpp"
#include "../src/library/game_mods_sources.hpp"
#include "../src/platform/file_io.hpp"
#include "../src/platform/hashing.hpp"
#include "../src/platform/zip_archive.hpp"

#include <Windows.h>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace game_mods;

namespace {
void write(const fs::path& file, std::string_view bytes) {
    fs::create_directories(file.parent_path());
    std::ofstream(file, std::ios::binary).write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}
std::string read(const fs::path& file) { return file_io::readText(file, 64 * 1024 * 1024); }
bool zip(const fs::path& file, const std::vector<std::pair<std::string, std::string>>& entries) {
    fs::create_directories(file.parent_path());
    zip_archive::Writer writer;
    if (!writer.open(file)) return false;
    for (const auto& [name, bytes] : entries) if (!writer.add(name, bytes)) return false;
    return writer.finish();
}
HttpResponse ok(std::string_view text) { HttpResponse response; response.status = 200; response.body.assign(text.begin(), text.end()); return response; }
}

int main() {
    // The catalogue and its methods.
    if (std::string(slug(Mod::optiscaler)) != "optiscaler" || fromSlug("dlss5") != Mod::dlss5 || fromSlug("x")) return 1;
    if (methodOr(Mod::optiscaler, "winmm.dll") != "winmm.dll" || methodOr(Mod::optiscaler, "opengl32.dll") != "dxgi.dll" ||
        methodOr(Mod::reshade, "") != "dxgi.dll" || methodOr(Mod::anr, "addon") != "addon") return 2;
    if (!forEmulators(Mod::reshade) || forEmulators(Mod::optiscaler)) return 3;
    // Versions.
    if (compareVersions("310.10", "310.9.16") != 1 || compareVersions("v0.9.5", "0.9.5") != 0 || compareVersions("6.7.3", "6.8.0") != -1 ||
        compareVersions("", "1") != -1 || compareVersions("FG-V.B20", "FG-V.B19") != 1) return 4;
    if (shortVersion(L"310.9.16.0") != L"310.9.16" || shortVersion(L"6.8.0.2104") != L"6.8.0" || shortVersion(L"0.9.5") != L"0.9.5" ||
        shortVersion(L"beta 2") != L"beta 2") return 5;
    // INI: a value replaced in its section, added at a section's end, a new section; line endings kept.
    {
        const std::string ini = "[Menu]\r\nScale=1\r\n\r\n[Plugins]\r\n; Loads ReShade64.dll\r\nLoadReshade=auto\r\nPath=auto\r\n\r\n[Log]\r\nLevel=2\r\n";
        const auto set = setIniValue(ini, "Plugins", "LoadReshade", "true");
        if (set.find("LoadReshade=true\r\nPath=auto") == std::string::npos || set.find("LoadReshade=auto") != std::string::npos) return 6;
        if (iniValue(set, "plugins", "loadreshade") != "true" || iniValue(set, "Menu", "LoadReshade")) return 7;
        const auto added = setIniValue(ini, "Log", "File", "x.log");
        if (added.find("Level=2\r\nFile=x.log") == std::string::npos) return 8;
        const auto inserted = setIniValue(ini, "Menu", "Key", "45");
        if (inserted.find("Scale=1\r\nKey=45\r\n\r\n[Plugins]") == std::string::npos) return 9;
        const auto fresh = setIniValue("[A]\nx=1\n", "Plugins", "LoadReshade", "true");
        if (fresh != "[A]\nx=1\n\n[Plugins]\nLoadReshade=true\n") return 10;
    }
    // reshade.me: the newest add-on build.
    {
        const auto setup = sources::reshadeSetup(R"(<a href="/downloads/ReShade_Setup_6.7.3_Addon.exe">x</a> <a href="downloads/ReShade_Setup_6.8.0.exe">)"
                                                 R"(<a href="downloads/ReShade_Setup_6.8.0_Addon.exe">)");
        if (setup.version != "6.8.0" || setup.url != L"https://reshade.me/downloads/ReShade_Setup_6.8.0_Addon.exe" || setup.file != "ReShade_Setup_6.8.0_Addon.exe") return 11;
        if (sources::reshadeSetup("<html>nothing</html>")) return 12;
    }
    // GitHub releases: an asset by its end, GitHub's own URLs only; the newest tag of a prefix, no release candidates.
    {
        const auto release = sources::release(R"({"tag_name":"v0.9.5","assets":[{"name":"Optiscaler_0.9.5.7z","size":70,"browser_download_url":"https://github.com/o/O/releases/download/v0.9.5/Optiscaler_0.9.5.7z"},)"
                                              R"({"name":"evil.7z","browser_download_url":"https://evil.example/x.7z"}]})");
        if (!release || release->tag != "v0.9.5" || release->assets.size() != 1 || !release->find(".7Z") || release->find(".zip")) return 13;
        const auto newest = sources::newestTagged(R"([{"tag_name":"renodx-dlss5-11.0.0-rc2","assets":[{"name":"a.zip","browser_download_url":"https://github.com/a"}]},)"
                                                  R"({"tag_name":"dlssnr-310.9.16","assets":[{"name":"b.zip","browser_download_url":"https://github.com/b"}]},)"
                                                  R"({"tag_name":"renodx-dlss5-10.8.0","assets":[{"name":"renodx-dlss5_10.8.0.zip","browser_download_url":"https://github.com/c"}]},)"
                                                  R"({"tag_name":"renodx-dlss5-8.5.0","assets":[{"name":"d.zip","browser_download_url":"https://github.com/d"}]}])", "renodx-dlss5-");
        if (!newest || newest->tag != "renodx-dlss5-10.8.0") return 14;
    }
    // RHI's DLSS manifest: the newest, NVIDIA's own build among equals.
    {
        const std::string manifest = R"({"dlss":[{"version":"310.9.1","url":"https://github.com/r/x/releases/download/a/nvngx_dlss_310.9.1.zip"},)"
                                     R"({"version":"310.9.16","url":"https://github.com/r/x/releases/download/b/nvngx_dlss_310.9.16.zip"}],)"
                                     R"("dlssnr":[{"version":"310.9.16","url":"https://github.com/r/x/releases/download/c/nr.zip"},)"
                                     R"({"version":"310.9.16-nvidia","url":"https://github.com/r/x/releases/download/d/nr-nvidia.zip"},)"
                                     R"({"version":"310.8.SF","url":"https://github.com/r/x/releases/download/e/old.zip"}]})";
        const auto sr = sources::dlss(manifest, "dlss");
        if (sr.version != "310.9.16" || sr.file != "nvngx_dlss_310.9.16.zip") return 15;
        if (sources::dlss(manifest, "dlssnr", "nvidia").file != "nr-nvidia.zip" || sources::dlss(manifest, "dlssnr").file != "nr.zip") return 16;
        if (sources::dlss(manifest, "dlssg")) return 17;
    }
    // RenoDX's index: by Steam app ID, by title or alias, the program's bitness, the engines' generic add-ons.
    const std::string renodx_index = R"({"games":[)"
        R"({"id":"1000xresist","title":"1000xResist","aliases":[],"steam_appid":1675830,"mods":[{"id":"1000xresist","category":"game","status":"stable","deploy":{"steam_appid":1675830},)"
        R"("artifacts":[{"name":"renodx-1000xresist.addon64","arch":"x64","url":"./renodx-1000xresist.addon64"}]}]},)"
        R"({"id":"ac","title":"Assassin’s Creed® Unity","aliases":["ACU"],"mods":[{"id":"acunity","category":"game","deploy":{},)"
        R"("artifacts":[{"name":"renodx-acunity.addon64","arch":"x64"}]}]},)"
        R"({"id":"alien","title":"Alien: Isolation","aliases":[],"steam_appid":214490,"mods":[{"id":"alienisolation","category":"game","deploy":{"steam_appid":214490},)"
        R"("artifacts":[{"name":"renodx-alienisolation.addon32","arch":"x86"}]}]},)"
        R"({"id":"ue","title":"Unreal Engine","aliases":[],"mods":[{"id":"unrealengine","category":"engine","deploy":{"steam_appid":502500},)"
        R"("artifacts":[{"name":"renodx-unrealengine.addon64","arch":"x64"},{"name":"../evil.addon64","arch":"x64"}]}]},)"
        R"({"id":"ds2","title":"DS2","aliases":[],"mods":[{"id":"ds2le","category":"related","artifacts":[]}]}]})";
    {
        const auto index = sources::renodxIndex(renodx_index);
        if (index.size() != 4) return 18;
        if (sources::renodxMatch(index, 1675830, L"", false)->file != "renodx-1000xresist.addon64") return 19;
        if (sources::renodxMatch(index, 0, L"Assassin's Creed Unity", false)->id != "acunity" || sources::renodxMatch(index, 0, L"acu", false)->id != "acunity") return 20;
        const auto alien = sources::renodxMatch(index, 214490, L"Alien", false);
        if (!alien || !alien->x86) return 21; // The only build: the installer says it does not fit.
        if (sources::renodxMatch(index, 502500, L"Some Game", false)) return 22; // An engine's sample game is not a match.
        if (sources::renodxGeneric(index, "unrealengine", false)->file != "renodx-unrealengine.addon64" || sources::renodxGeneric(index, "unityengine", false)) return 23;
        if (sources::titleKey(L"Hollow Knight: Silksong™") != L"hollowknightsilksong") return 24;
    }
    if (sources::checksum("750885d982b0e58e815004da698999f3b85f20c84a28c33cc15ae54e555f70ae  anr-fg-V.B20-addon.zip\n"
                          "999E8B08AC177749880D75018BBE02F6D184730BBD3BB13B68633860BF491598 *anr-fg-V.B20-full.zip\n", "anr-fg-V.B20-full.zip") !=
        "999e8b08ac177749880d75018bbe02f6d184730bbd3bb13b68633860bf491598" || !sources::checksum("x  y\n", "y").empty()) return 25;
    if (quotePowerShell(L"D:\\Gry\\O'Brien") != L"'D:\\Gry\\O''Brien'") return 26;
    // The record: one file per folder however it is spelled; written and read back the same.
    {
        Record record;
        record.folder = L"C:\\Gry\\Żółw";
        record.mods["reshade"] = {"6.8.0", "dxgi.dll", {}, {{L"C:\\Gry\\Żółw\\dxgi.dll", L"C:\\Gry\\Żółw\\dxgi.dll.launchme-backup"}}, {L"C:\\Gry\\Żółw\\reshade-shaders"}, {}};
        record.mods["anr"] = {"V.B20", "full", L"C:\\cache\\anr", {}, {}, "dxgi.dll"};
        const auto back = parseRecord(serialize(record));
        if (!back || back->folder != record.folder || back->mods != record.mods) return 27;
        if (recordFile(L"C:\\d", L"C:\\Gry\\ŻÓŁW\\") != recordFile(L"C:\\d", L"c:\\gry\\żółw\\")) return 28;
    }

    // Installs in a temporary game folder.
    const auto root = fs::temp_directory_path() / (L"launchme mods " + std::to_wstring(GetCurrentProcessId()));
    struct Cleanup { fs::path path; ~Cleanup() { std::error_code ec; fs::remove_all(path, ec); } } cleanup{root};
    fs::remove_all(root);
    const auto server = root / L"server", data = root / L"data", game = root / L"Gra Testowa";
    write(game / L"Game.exe", "MZ fake game");
    write(game / L"Engine" / L"Plugins" / L"nvngx_dlss.dll", "old dlss");
    write(game / L"nvngx_dlssg.dll", "old frame generation");
    write(game / L"winmm.dll", "the game's own winmm");
    // The servers: pages in memory, files on disk.
    std::map<std::wstring, std::string> pages;
    std::map<std::wstring, fs::path> files;
    // ReShade's setup as reshade.me serves it: a program with the archive appended, its offsets counted from
    // the archive's own start (miniz alone calls that damaged).
    if (!zip(server / L"setup.zip", {{"ReShade64.dll", "reshade 64"}, {"ReShade32.dll", "reshade 32"}, {"ReShade64.json", "{}"}})) return 29;
    write(server / L"ReShade_Setup_6.8.0_Addon.exe", "MZ" + std::string(150000, '\x90') + "PK\x03\x04 a stray signature" + read(server / L"setup.zip"));
    if (!zip(server / L"slim.zip", {{"reshade-shaders-slim/Shaders/ReShade.fxh", "fxh"}, {"reshade-shaders-slim/Textures/a.png", "png"},
                                    {"reshade-shaders-slim/README.md", "x"}})) return 30;
    write(server / L"renodx-1000xresist.addon64", "renodx addon");
    write(server / L"opti" / L"OptiScaler.dll", "optiscaler");
    write(server / L"opti" / L"OptiScaler.ini", "[Plugins]\r\nLoadReshade=auto\r\n");
    write(server / L"opti" / L"libxess.dll", "xess");
    write(server / L"opti" / L"D3D12_Optiscaler" / L"D3D12Core.dll", "core");
    write(server / L"opti" / L"setup_windows.bat", "@echo off");
    write(server / L"opti" / L"Licenses" / L"XeSS_LICENSE.txt", "license");
    write(server / L"Optiscaler_0.9.5.7z", "7z bytes");
    if (!zip(server / L"nvngx_dlss_310.9.16.zip", {{"nvngx_dlss.dll", "new dlss"}})) return 31;
    if (!zip(server / L"nvngx_dlssg_310.9.16.zip", {{"nvngx_dlssg.dll", "new frame generation"}})) return 32;
    if (!zip(server / L"anr-fg-V.B20-full.zip", {{"install.ps1", "# install"}, {"uninstall.ps1", "# uninstall"}, {"game/dxgi.dll", "anr opti"}})) return 33;
    const auto anr_sum = hashing::fileHex(hashing::Algorithm::sha256, server / L"anr-fg-V.B20-full.zip");
    pages[std::wstring(sources::reshade_page)] = R"(<a href="downloads/ReShade_Setup_6.8.0_Addon.exe">)";
    files[L"https://reshade.me/downloads/ReShade_Setup_6.8.0_Addon.exe"] = server / L"ReShade_Setup_6.8.0_Addon.exe";
    files[std::wstring(sources::reshade_shaders)] = server / L"slim.zip";
    pages[std::wstring(sources::renodx_index)] = renodx_index;
    files[std::wstring(sources::renodx_files) + L"renodx-1000xresist.addon64"] = server / L"renodx-1000xresist.addon64";
    pages[std::wstring(sources::optiscaler_latest)] =
        R"({"tag_name":"v0.9.5","assets":[{"name":"Optiscaler_0.9.5.7z","browser_download_url":"https://github.com/o/O/releases/download/v0.9.5/Optiscaler_0.9.5.7z"}]})";
    files[L"https://github.com/o/O/releases/download/v0.9.5/Optiscaler_0.9.5.7z"] = server / L"Optiscaler_0.9.5.7z";
    pages[std::wstring(sources::dlss_manifest)] = R"({"dlss":[{"version":"310.9.16","url":"https://github.com/r/x/releases/download/a/nvngx_dlss_310.9.16.zip"}],)"
                                                  R"("dlssg":[{"version":"310.9.16","url":"https://github.com/r/x/releases/download/b/nvngx_dlssg_310.9.16.zip"}]})";
    files[L"https://github.com/r/x/releases/download/a/nvngx_dlss_310.9.16.zip"] = server / L"nvngx_dlss_310.9.16.zip";
    files[L"https://github.com/r/x/releases/download/b/nvngx_dlssg_310.9.16.zip"] = server / L"nvngx_dlssg_310.9.16.zip";
    pages[std::wstring(sources::anr_latest)] = R"({"tag_name":"FG-V.B20","assets":[{"name":"anr-fg-V.B20-full.zip","browser_download_url":"https://github.com/a/ANR/full.zip"},)"
                                               R"({"name":"SHA256SUMS.txt","browser_download_url":"https://github.com/a/ANR/SHA256SUMS.txt"}]})";
    pages[L"https://github.com/a/ANR/SHA256SUMS.txt"] = anr_sum + "  anr-fg-V.B20-full.zip\n";
    files[L"https://github.com/a/ANR/full.zip"] = server / L"anr-fg-V.B20-full.zip";
    int downloads = 0;
    std::vector<std::wstring> commands;
    Services services;
    services.fetch = [&](const std::wstring& url) { const auto found = pages.find(url); return found == pages.end() ? HttpResponse{404} : ok(found->second); };
    services.download = [&](const std::wstring& url, const fs::path& file) {
        const auto found = files.find(url);
        if (found == files.end()) return false;
        ++downloads;
        fs::create_directories(file.parent_path());
        std::error_code ec;
        fs::copy_file(found->second, file, fs::copy_options::overwrite_existing, ec);
        return !ec;
    };
    services.extract7z = [&](const fs::path& archive, const fs::path& folder) {
        if (read(archive) != "7z bytes") return false;
        std::error_code ec;
        fs::copy(server / L"opti", folder, fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
        return !ec;
    };
    services.shell = [&](std::wstring_view command) { // ANR FG's scripts, played.
        commands.emplace_back(command);
        launch_scripts::Result result{true, true, 0, "done"};
        if (command.find(L"install.ps1' -Game") != std::wstring_view::npos && command.find(L"uninstall.ps1") == std::wstring_view::npos) {
            write(game / L"OptiScaler" / L"nr_fg_install.json", "{}");
            write(game / L"dxgi.dll", "anr opti");
            write(game / L"OptiScaler.ini", "[Plugins]\r\nLoadReshade=auto\r\n");
        } else if (command.find(L"uninstall.ps1") != std::wstring_view::npos) {
            fs::remove_all(game / L"OptiScaler");
            fs::remove(game / L"dxgi.dll");
            fs::remove(game / L"OptiScaler.ini");
        }
        return result;
    };
    Target target;
    target.folder = game;
    target.root = game;
    target.exe = game / L"Game.exe";
    target.steam_appid = 1675830;
    target.title = L"1000xResist";
    const auto run = [&](Mod mod, std::string method, bool remove = false) {
        Request request;
        request.mod = mod; request.method = std::move(method); request.remove = remove; request.target = target;
        request.reshade_method = "dxgi.dll";
        return apply(request, data, services);
    };

    // ReShade as dxgi.dll with its ini and the slim shaders.
    auto outcome = run(Mod::reshade, "dxgi.dll");
    if (!outcome.ok() || read(game / L"dxgi.dll") != "reshade 64" || !file_io::isFile(game / L"ReShade.ini") ||
        read(game / L"reshade-shaders" / L"Shaders" / L"ReShade.fxh") != "fxh" || file_io::isFile(game / L"reshade-shaders" / L"README.md")) return 40;
    auto record = loadRecord(data, game);
    if (!record || !record->find(Mod::reshade) || record->find(Mod::reshade)->version != "6.8.0") return 41;
    auto state = scan(target, &*record);
    if (!state[Mod::reshade].present || !state[Mod::reshade].ours || state[Mod::reshade].method != "dxgi.dll" || !state.has_dlss || state.dlss_files.size() != 2) return 42;
    // RenoDX on top: the game's add-on by its Steam app ID.
    outcome = run(Mod::renodx, "game");
    if (!outcome.ok() || read(game / L"renodx-1000xresist.addon64") != "renodx addon") return 43;
    // OptiScaler as dxgi.dll: ReShade moves to ReShade64.dll, which OptiScaler loads.
    outcome = run(Mod::optiscaler, "dxgi.dll");
    if (!outcome.ok() || read(game / L"dxgi.dll") != "optiscaler" || read(game / L"ReShade64.dll") != "reshade 64" ||
        read(game / L"D3D12_Optiscaler" / L"D3D12Core.dll") != "core" || file_io::isFile(game / L"setup_windows.bat") ||
        fs::exists(game / L"Licenses") || iniValue(read(game / L"OptiScaler.ini"), "Plugins", "LoadReshade") != "true") return 44;
    record = loadRecord(data, game);
    state = scan(target, &*record);
    if (!state[Mod::optiscaler].ours || state[Mod::optiscaler].version != L"0.9.5" || !state[Mod::reshade].ours) return 45;
    const auto shown = badges(state);
    if (shown.size() != 4 || shown[0] != L"ReShade" || shown[1] != L"RenoDX" || shown[2] != L"OptiScaler" || !shown[3].starts_with(L"DLSS")) return 46;
    // An update keeps the user's OptiScaler.ini.
    write(game / L"OptiScaler.ini", "[Plugins]\r\nLoadReshade=true\r\n[Menu]\r\nScale=2\r\n");
    const int before_update = downloads;
    outcome = run(Mod::optiscaler, "dxgi.dll");
    if (!outcome.ok() || downloads != before_update || iniValue(read(game / L"OptiScaler.ini"), "Menu", "Scale") != "2") return 47;
    // OptiScaler out: ReShade back as dxgi.dll, OptiScaler's folder gone.
    outcome = run(Mod::optiscaler, "", true);
    if (!outcome.ok() || read(game / L"dxgi.dll") != "reshade 64" || fs::exists(game / L"ReShade64.dll") || fs::exists(game / L"libxess.dll") ||
        fs::exists(game / L"D3D12_Optiscaler") || fs::exists(game / L"OptiScaler.ini")) return 48;
    // OptiScaler as winmm.dll: the game's own winmm.dll is kept aside and comes back.
    outcome = run(Mod::optiscaler, "winmm.dll");
    if (!outcome.ok() || read(game / L"winmm.dll") != "optiscaler" || read(game / L"winmm.dll.launchme-backup") != "the game's own winmm" ||
        read(game / L"dxgi.dll") != "reshade 64") return 49;
    outcome = run(Mod::optiscaler, "", true);
    if (!outcome.ok() || read(game / L"winmm.dll") != "the game's own winmm" || fs::exists(game / L"winmm.dll.launchme-backup")) return 50;
    // DLSS: every DLL of the game replaced (anywhere under it), the originals back on removal.
    outcome = run(Mod::dlss, "all");
    if (!outcome.ok() || read(game / L"Engine" / L"Plugins" / L"nvngx_dlss.dll") != "new dlss" || read(game / L"nvngx_dlssg.dll") != "new frame generation" ||
        read(game / L"Engine" / L"Plugins" / L"nvngx_dlss.dll.launchme-backup") != "old dlss") return 51;
    outcome = run(Mod::dlss, "", true);
    if (!outcome.ok() || read(game / L"Engine" / L"Plugins" / L"nvngx_dlss.dll") != "old dlss" || read(game / L"nvngx_dlssg.dll") != "old frame generation") return 52;
    // ReShade out takes its add-ons with it; the folder is as it was.
    outcome = run(Mod::reshade, "", true);
    if (!outcome.ok() || fs::exists(game / L"dxgi.dll") || fs::exists(game / L"renodx-1000xresist.addon64") || fs::exists(game / L"reshade-shaders") ||
        fs::exists(game / L"ReShade.ini") || loadRecord(data, game)) return 53;
    // Another tool's mod is shown but never removed here.
    write(game / L"OptiScaler.ini", "[Plugins]\r\n");
    outcome = run(Mod::optiscaler, "", true);
    if (outcome.ok() || !scan(target, nullptr)[Mod::optiscaler].present) return 54;
    fs::remove(game / L"OptiScaler.ini");
    // ANR FG's full package: its checksum, its own installer with the proxy, ReShade moved aside for it.
    if (!run(Mod::reshade, "dxgi.dll").ok()) return 55;
    outcome = run(Mod::anr, "auto");
    if (!outcome.ok() || commands.size() != 1 || commands[0].find(L"install.ps1' -Game '" + game.wstring() + L"' -Proxy 'dxgi.dll'") == std::wstring::npos ||
        read(game / L"ReShade64.dll") != "reshade 64" || iniValue(read(game / L"OptiScaler.ini"), "Plugins", "LoadReshade") != "true") return 56;
    record = loadRecord(data, game);
    state = scan(target, record ? &*record : nullptr);
    if (!state[Mod::anr].present || !state[Mod::anr].ours || state[Mod::anr].method != "full" || state[Mod::anr].version != L"V.B20" ||
        !state[Mod::optiscaler].present || state[Mod::optiscaler].ours) return 57;
    if (run(Mod::optiscaler, "dxgi.dll").ok()) return 58; // Its OptiScaler is the package's.
    outcome = run(Mod::anr, "", true);
    if (!outcome.ok() || commands.size() != 2 || commands[1].find(L"uninstall.ps1' -Game") == std::wstring::npos ||
        read(game / L"dxgi.dll") != "reshade 64" || fs::exists(game / L"ReShade64.dll")) return 59;
    if (!run(Mod::reshade, "", true).ok()) return 60;
    // A package whose checksum does not match is refused and dropped.
    pages[L"https://github.com/a/ANR/SHA256SUMS.txt"] = std::string(64, 'a') + "  anr-fg-V.B20-full.zip\n";
    fs::remove_all(data / L"cache" / L"anr");
    outcome = run(Mod::anr, "full");
    if (outcome.ok() || commands.size() != 2 || fs::exists(data / L"cache" / L"anr" / L"V.B20" / L"anr-fg-V.B20-full.zip")) return 61;
    // Anti-cheat: asked once, then installed when allowed.
    write(game / L"EasyAntiCheat" / L"Settings.json", "{}");
    outcome = run(Mod::reshade, "dxgi.dll");
    if (outcome.problem != Problem::anticheat || outcome.message.find(L"Easy Anti-Cheat") == std::wstring::npos || fs::exists(game / L"dxgi.dll")) return 62;
    Request allowed;
    allowed.mod = Mod::reshade; allowed.method = "dxgi.dll"; allowed.target = target; allowed.allow_anticheat = true;
    if (!apply(allowed, data, services).ok() || read(game / L"dxgi.dll") != "reshade 64") return 63;
    // No DLSS in a game: nothing to update.
    const auto bare = root / L"Bare";
    write(bare / L"Bare.exe", "MZ");
    Request no_dlss;
    no_dlss.mod = Mod::dlss; no_dlss.target.folder = bare; no_dlss.target.root = bare;
    if (apply(no_dlss, data, services).ok()) return 64;
    // The game's program: an Unreal game's shipping build over larger tools.
    write(root / L"Unreal" / L"Game" / L"Binaries" / L"Win64" / L"Game-Win64-Shipping.exe", "MZ");
    write(root / L"Unreal" / L"Game.exe", std::string(4096, 'x'));
    write(root / L"Unreal" / L"unins000.exe", std::string(8192, 'x'));
    if (mainExecutable(root / L"Unreal").filename() != L"Game-Win64-Shipping.exe") return 65;
    fs::remove(root / L"Unreal" / L"Game" / L"Binaries" / L"Win64" / L"Game-Win64-Shipping.exe");
    if (mainExecutable(root / L"Unreal").filename() != L"Game.exe") return 66;
    // A 32-bit program by its PE header.
    std::string pe(0x100, '\0');
    pe[0] = 'M'; pe[1] = 'Z'; pe[0x3c] = static_cast<char>(0x80);
    pe.replace(0x80, 6, std::string("PE\0\0\x4c\x01", 6));
    write(root / L"x86.exe", pe);
    pe[0x84] = static_cast<char>(0x64); pe[0x85] = static_cast<char>(0x86);
    write(root / L"x64.exe", pe);
    if (!x86Image(root / L"x86.exe") || x86Image(root / L"x64.exe") || x86Image(root / L"Bare" / L"Bare.exe")) return 67;
    return 0;
}
