// Linux only: the Wine/Proton/umu runner without a real Wine. Command composition for umu-run,
// Proton and Wine with every option, the environment's order, wrappers and gamescope, scripts,
// .msi/.bat/.lnk, the options catalog and its JSON, the per-game store, and executeGameAction
// starting fake umu-run/proton/wine scripts in a temporary home (nothing real is started).
#include "../src/library/compat_tools.hpp"
#include "../src/library/launch_options_store.hpp"
#include "../src/model/launch_options_catalog.hpp"
#include "../src/platform/game_activity.hpp"
#include "../src/platform/launch.hpp"
#include "../src/platform/process_spawn.hpp"
#include "../src/platform/text_encoding.hpp"
#include "../src/platform/wine_runner.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <unistd.h>
#include <vector>

namespace fs = std::filesystem;
using wine_runner::Plan;
using wine_runner::Request;

namespace {
int failures = 0;
void check(bool condition, const std::string& what) {
    if (!condition) { ++failures; std::cerr << "FAILED: " << what << '\n'; }
}
template<class Condition> bool waitFor(Condition condition) {
    for (int i = 0; i < 500; ++i) { if (condition()) return true; usleep(10000); }
    return condition();
}
std::string join(const std::vector<std::string>& parts) {
    std::string result;
    for (const auto& part : parts) result += (result.empty() ? "" : " | ") + part;
    return result;
}
// The final value of a variable in a plan (the last assignment), nullopt when never set.
std::optional<std::string> variable(const Plan& plan, const std::string& name) {
    std::optional<std::string> result;
    for (const auto& [key, value] : plan.environment) if (key == name) result = value;
    return result;
}
std::map<std::string, std::string> block(const Plan& plan, const std::vector<std::string>& current = {}) {
    std::map<std::string, std::string> result;
    for (const auto& entry : wine_runner::environmentBlock(plan, current)) {
        const auto equals = entry.find('=');
        result[entry.substr(0, equals)] = entry.substr(equals + 1);
    }
    return result;
}
std::string readText(const fs::path& file) {
    std::ifstream input(file, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
std::vector<std::string> lines(const fs::path& file) {
    std::vector<std::string> result;
    std::ifstream input(file);
    for (std::string line; std::getline(input, line);) result.push_back(line);
    return result;
}
void writeScript(const fs::path& file, const std::string& body) {
    fs::create_directories(file.parent_path());
    std::ofstream(file, std::ios::binary) << "#!/bin/sh\n" << body;
    fs::permissions(file, fs::perms::owner_all | fs::perms::group_read | fs::perms::group_exec | fs::perms::others_read | fs::perms::others_exec,
                    fs::perm_options::replace);
}

compat_tools::Tool geProton() {
    compat_tools::Tool tool;
    tool.id = "launchme:GE-Proton11-11";
    tool.kind = compat_tools::Kind::proton;
    tool.family = "GE-Proton";
    tool.version = "GE-Proton11-11";
    tool.path = "/tools/GE-Proton11-11";
    tool.proton = "/tools/GE-Proton11-11/proton";
    tool.wine = "/tools/GE-Proton11-11/files/bin/wine";
    tool.wineserver = "/tools/GE-Proton11-11/files/bin/wineserver";
    return tool;
}
compat_tools::Tool wineBuild() {
    compat_tools::Tool tool;
    tool.id = "launchme:wine-11.20-staging-tkg-amd64-wow64";
    tool.kind = compat_tools::Kind::wine;
    tool.family = "Wine-Staging-TkG";
    tool.path = "/tools/wine-11.20";
    tool.wine = "/tools/wine-11.20/bin/wine";
    tool.wineserver = "/tools/wine-11.20/bin/wineserver";
    return tool;
}
compat_tools::Tool component(const std::string& family, const std::string& folder) {
    compat_tools::Tool tool;
    tool.kind = compat_tools::Kind::component;
    tool.family = family;
    tool.path = "/cfg/compatibilitytools/components/" + folder;
    return tool;
}
wine_runner::Context baseContext() {
    wine_runner::Context context;
    context.config = "/cfg";
    context.tool = geProton();
    context.umu_run = "/usr/bin/umu-run";
    context.steam_client = "/home/u/.steam/root";
    context.gamemoderun = "/usr/bin/gamemoderun";
    context.mangohud = "/usr/bin/mangohud";
    context.gamescope = "/usr/bin/gamescope";
    context.game_id = "umu-397540";
    return context;
}
Request baseRequest() {
    Request request;
    request.program = "/games/bl3/game.exe";
    request.arguments = {"-dx12"};
    request.launch.game_key = "epic:Catnip";
    request.launch.title = L"Borderlands 3";
    request.launch.store = "egs";
    request.launch.store_id = "Catnip";
    request.launch.prefix = "/pfx/borderlands-3";
    request.launch.install_directory = "/games/bl3";
    return request;
}

void umuPlans() {
    auto request = baseRequest();
    auto context = baseContext();
    auto plan = wine_runner::plan(request, context);
    check(plan.error.empty() && plan.runner == wine_runner::Runner::umu, "umu-run for a Proton tool when umu is installed");
    check(plan.argv == std::vector<std::string>{"/usr/bin/umu-run", "/games/bl3/game.exe", "-dx12"}, "umu argv: " + join(plan.argv));
    check(variable(plan, "WINEPREFIX") == "/pfx/borderlands-3" && variable(plan, "GAMEID") == "umu-397540" && variable(plan, "STORE") == "egs" &&
          variable(plan, "PROTONPATH") == "/tools/GE-Proton11-11" && variable(plan, "PROTON_VERB") == "waitforexitandrun" &&
          variable(plan, "STEAM_COMPAT_INSTALL_PATH") == "/games/bl3", "umu variables");
    check(variable(plan, "LAUNCHME_GAME_KEY") == "epic:Catnip" && variable(plan, "LAUNCHME_GAME_NAME") == "Borderlands 3" &&
          variable(plan, "LAUNCHME_RUNNER") == "umu" && variable(plan, "LAUNCHME_PREFIX") == "/pfx/borderlands-3", "Launchme's variables for scripts");
    check(plan.prefix == "/pfx/borderlands-3" && !plan.proton_prefix, "umu makes its own prefix");
    check(plan.working_directory == "/games/bl3" && plan.log_file == "/cfg/logs/wine/epic-catnip.log", "folder and log");
    check(!variable(plan, "WINEDEBUG") && !variable(plan, "WINEDLLOVERRIDES") && !variable(plan, "PROTON_LOG"), "Proton's own defaults kept");

    // umu's automatic tools: GE-Proton by codename, UMU-Proton by leaving PROTONPATH out.
    compat_tools::Tool automatic;
    automatic.id = "umu:GE-Proton"; automatic.kind = compat_tools::Kind::proton; automatic.origin = compat_tools::Origin::umu;
    automatic.umu_protonpath = "GE-Proton";
    context.tool = automatic;
    plan = wine_runner::plan(request, context);
    check(plan.runner == wine_runner::Runner::umu && variable(plan, "PROTONPATH") == "GE-Proton", "umu GE-Proton codename");
    automatic.id = "umu:UMU-Proton"; automatic.umu_protonpath.clear();
    context.tool = automatic;
    plan = wine_runner::plan(request, context);
    check(!variable(plan, "PROTONPATH") && std::count(plan.unset.begin(), plan.unset.end(), "PROTONPATH") == 1, "umu's UMU-Proton: PROTONPATH unset");
    check(block(plan, {"PROTONPATH=/old", "HOME=/home/u"}).count("PROTONPATH") == 0, "inherited PROTONPATH removed");
    context.umu_run.clear();
    check(wine_runner::plan(request, context).error == "no-umu", "umu's automatic tool needs umu-run");

    // No store: no STORE; no tool at all: not runnable.
    context = baseContext();
    request.launch.store.clear();
    plan = wine_runner::plan(request, context);
    check(!variable(plan, "STORE"), "no STORE without a store");
    context.tool.reset();
    check(wine_runner::plan(request, context).error == "no-tool", "no tool installed");
    request.program.clear();
    context = baseContext();
    check(wine_runner::plan(request, context).error == "no-program", "no program");
}

void protonPlans() {
    auto request = baseRequest();
    request.launch.options.prefer_umu = false;
    request.launch.steam_app_id = 0;
    auto context = baseContext();
    auto plan = wine_runner::plan(request, context);
    check(plan.runner == wine_runner::Runner::proton, "Proton directly when umu is not wanted");
    check(plan.argv == std::vector<std::string>{"/tools/GE-Proton11-11/proton", "waitforexitandrun", "/games/bl3/game.exe", "-dx12"}, "proton argv: " + join(plan.argv));
    check(variable(plan, "STEAM_COMPAT_DATA_PATH") == "/pfx/borderlands-3" && variable(plan, "STEAM_COMPAT_CLIENT_INSTALL_PATH") == "/home/u/.steam/root" &&
          variable(plan, "WINEPREFIX") == "/pfx/borderlands-3" && variable(plan, "STEAM_COMPAT_APP_ID") == "0" &&
          variable(plan, "SteamGameId") == "launchme-epic-catnip" && !variable(plan, "GAMEID") && !variable(plan, "PROTONPATH"), "proton variables");
    check(plan.proton_prefix, "Proton's pfx link is made before it runs");
    check(variable(plan, "UMU_ID") == "umu-397540" && variable(plan, "STORE") == "egs", "protonfixes' UMU_ID and STORE without umu-run");
    request.launch.steam_app_id = 397540;
    check(!variable(wine_runner::plan(request, context), "UMU_ID"), "a Steam game's protonfixes come from SteamAppId");
    request.launch.steam_app_id = 0;
    context.steam_game_id = "12345";
    check(!variable(wine_runner::plan(request, context), "SteamGameId"), "Steam's own SteamGameId (Game Mode) kept");
    // umu wanted but not installed: Proton directly.
    request.launch.options.prefer_umu = true;
    context = baseContext();
    context.umu_run.clear();
    check(wine_runner::plan(request, context).runner == wine_runner::Runner::proton, "Proton directly without umu-run");
}

void winePlans() {
    auto request = baseRequest();
    auto& o = request.launch.options;
    o.sync = LaunchOptions::Sync::esync;
    o.install_dxvk = true;
    o.install_vkd3d = true;
    o.dll_overrides = "dinput8=n,b";
    o.wayland = true;
    o.wow64 = true;
    o.locale = "ja_JP.UTF-8";
    o.large_address_aware = true;
    auto context = baseContext();
    context.tool = wineBuild();
    context.dxvk = component("DXVK", "dxvk-3.1.1");
    auto plan = wine_runner::plan(request, context);
    check(plan.runner == wine_runner::Runner::wine && plan.error.empty(), "plain Wine for a Wine build");
    check(plan.argv.size() == 7 && plan.argv[0] == "/bin/sh" && plan.argv[1] == "-c" && plan.argv[3] == "launchme" &&
          std::vector<std::string>(plan.argv.begin() + 4, plan.argv.end()) == std::vector<std::string>{"/tools/wine-11.20/bin/wine", "/games/bl3/game.exe", "-dx12"},
          "wine argv with the component setup: " + join(plan.argv));
    const auto& script = plan.argv[2];
    check(script.find("'/tools/wine-11.20/bin/wine' wineboot -u") != std::string::npos && script.find("'/tools/wine-11.20/bin/wineserver' -w") != std::string::npos,
          "a new prefix is made before DXVK is copied");
    check(script.find("cp -f '/cfg/compatibilitytools/components/dxvk-3.1.1'/x64/*.dll \"$d/system32/\"") != std::string::npos &&
          script.find("'/cfg/compatibilitytools/components/dxvk-3.1.1'/x32/*.dll \"$d/syswow64/\"") != std::string::npos &&
          script.find("grep -qxF 'dxvk-3.1.1'") != std::string::npos && script.ends_with("exec \"$@\"\n"), "DXVK copied once per version");
    check(variable(plan, "WINEDLLOVERRIDES") == "winemenubuilder.exe=d;d3d8,d3d9,d3d10core,d3d11,dxgi=n,b;dinput8=n,b", "DLL overrides: " + variable(plan, "WINEDLLOVERRIDES").value_or(""));
    check(std::find(plan.missing.begin(), plan.missing.end(), "vkd3d-proton") != plan.missing.end(), "VKD3D-Proton missing is reported");
    check(variable(plan, "WINEESYNC") == "1" && !variable(plan, "WINEFSYNC") && variable(plan, "WINEDEBUG") == "-all" &&
          variable(plan, "DXVK_LOG_LEVEL") == "none" && variable(plan, "WINEARCH") == "wow64" && variable(plan, "LC_ALL") == "ja_JP.UTF-8" &&
          variable(plan, "WINE_LARGE_ADDRESS_AWARE") == "1" && variable(plan, "WINEPREFIX") == "/pfx/borderlands-3" &&
          variable(plan, "WINESERVER") == "/tools/wine-11.20/bin/wineserver", "Wine variables");
    check(std::find(plan.unset.begin(), plan.unset.end(), "DISPLAY") != plan.unset.end() && !variable(plan, "PROTON_ENABLE_WAYLAND"), "Wayland: DISPLAY unset");
    check(!variable(plan, "PROTON_USE_WOW64") && !variable(plan, "GAMEID") && !variable(plan, "STEAM_COMPAT_DATA_PATH"), "no Proton variables for Wine");
    // WineD3D: DXVK's DLLs (copied earlier) are bypassed and not copied again.
    o.wined3d = true; o.install_vkd3d = false;
    plan = wine_runner::plan(request, context);
    check(variable(plan, "WINEDLLOVERRIDES") == "winemenubuilder.exe=d;d3d8,d3d9,d3d10core,d3d11,dxgi=b;dinput8=n,b" && plan.argv.front() != "/bin/sh",
          "WineD3D with Wine");
    // Logging on: Wine's own debug channels, DXVK's logs in the game's log folder.
    o.log = true; o.install_dxvk = false;
    plan = wine_runner::plan(request, context);
    check(!variable(plan, "WINEDEBUG") && variable(plan, "DXVK_LOG_PATH") == "/cfg/logs/wine/epic-catnip", "Wine logging");
    // fsync and DXVK-NVAPI.
    o.sync = LaunchOptions::Sync::fsync; o.install_dxvk_nvapi = true;
    context.dxvk_nvapi = component("DXVK-NVAPI", "dxvk-nvapi-v0.9.2");
    plan = wine_runner::plan(request, context);
    check(variable(plan, "WINEFSYNC") == "1" && variable(plan, "WINEESYNC") == "1" && variable(plan, "DXVK_ENABLE_NVAPI") == "1" &&
          variable(plan, "WINEDLLOVERRIDES")->find("nvapi,nvapi64,nvofapi64=n") != std::string::npos &&
          plan.argv[2].find("'/cfg/compatibilitytools/components/dxvk-nvapi-v0.9.2'/x64/*.dll") != std::string::npos, "fsync and DXVK-NVAPI");
    context.tool->wine.clear();
    check(wine_runner::plan(request, context).error == "no-wine", "a Wine tool without its wine");
}

void protonSwitches() {
    auto request = baseRequest();
    auto& o = request.launch.options;
    o.sync = LaunchOptions::Sync::ntsync;
    o.wined3d = o.wayland = o.fsr = o.fsr4_upgrade = o.xess_upgrade = o.large_address_aware = o.wow64 = true;
    o.hdr = LaunchOptions::Hdr::on;
    o.fsr_strength = 4; o.fsr_resolution = "1280x720";
    o.dxvk_hud = "fps,gpuload"; o.dxvk_frame_rate = 60;
    o.eac_runtime = o.battleye_runtime = true;
    o.dll_overrides = "xinput1_3=n";
    o.locale = "ja_JP.UTF-8";
    o.log = true;
    o.disable_hidraw = o.prefer_sdl = o.pulse_latency = o.old_gl_string = true;
    o.nvapi = LaunchOptions::Switch::on;
    o.dlss_upgrade = o.dlss_indicator = o.hide_nvidia_gpu = o.prime_offload = o.gl_shader_cache_keep = o.gl_threaded = true;
    o.gl_shader_cache_gb = 10;
    o.dri_prime = o.radv_sam = o.radv_nggc = o.radv_emulate_rt = o.mesa_glthread = true;
    o.present_mode = LaunchOptions::PresentMode::mailbox;
    o.anisotropy = 16;
    o.mesa_shader_cache_size = "10G";
    auto context = baseContext();
    context.eac_runtime = "/steam/steamapps/common/Proton EasyAntiCheat Runtime";
    const auto plan = wine_runner::plan(request, context);
    const std::map<std::string, std::string> expected{
        {"PROTON_USE_NTSYNC", "1"}, {"PROTON_USE_WINED3D", "1"}, {"PROTON_ENABLE_WAYLAND", "1"}, {"PROTON_ENABLE_HDR", "1"}, {"DXVK_HDR", "1"},
        {"WINE_FULLSCREEN_FSR", "1"}, {"WINE_FULLSCREEN_FSR_STRENGTH", "4"}, {"WINE_FULLSCREEN_FSR_CUSTOM_MODE", "1280x720"},
        {"PROTON_FSR4_UPGRADE", "1"}, {"PROTON_XESS_UPGRADE", "1"}, {"PROTON_FORCE_LARGE_ADDRESS_AWARE", "1"}, {"PROTON_USE_WOW64", "1"},
        {"DXVK_HUD", "fps,gpuload"}, {"DXVK_FRAME_RATE", "60"}, {"VKD3D_FRAME_RATE", "60"},
        {"PROTON_EAC_RUNTIME", "/steam/steamapps/common/Proton EasyAntiCheat Runtime"}, {"WINEDLLOVERRIDES", "xinput1_3=n"},
        {"HOST_LC_ALL", "ja_JP.UTF-8"}, {"PROTON_LOG", "1"}, {"PROTON_LOG_DIR", "/cfg/logs/wine/epic-catnip"}, {"UMU_LOG", "1"},
        {"PROTON_ENABLE_NVAPI", "1"}, {"DXVK_ENABLE_NVAPI", "1"}, {"PROTON_DLSS_UPGRADE", "1"}, {"PROTON_DLSS_INDICATOR", "1"},
        {"DXVK_NVAPI_SET_NGX_DEBUG_OPTIONS", "DLSSIndicator=1024,DLSSGIndicator=2"}, {"PROTON_HIDE_NVIDIA_GPU", "1"},
        {"__NV_PRIME_RENDER_OFFLOAD", "1"}, {"__GLX_VENDOR_LIBRARY_NAME", "nvidia"}, {"__VK_LAYER_NV_optimus", "NVIDIA_only"},
        {"__GL_SHADER_DISK_CACHE_SIZE", "10737418240"}, {"__GL_SHADER_DISK_CACHE_SKIP_CLEANUP", "1"}, {"__GL_THREADED_OPTIMIZATIONS", "1"},
        {"DRI_PRIME", "1"}, {"RADV_PERFTEST", "sam,nggc"}, {"RADV_EXPERIMENTAL", "emulate_rt"}, {"mesa_glthread", "true"},
        {"MESA_VK_WSI_PRESENT_MODE", "mailbox"}, {"RADV_TEX_ANISO", "16"}, {"MESA_SHADER_CACHE_MAX_SIZE", "10G"},
        {"PROTON_DISABLE_HIDRAW", "1"}, {"PROTON_PREFER_SDL", "1"}, {"PULSE_LATENCY_MSEC", "60"}, {"PROTON_OLD_GL_STRING", "1"},
    };
    for (const auto& [name, value] : expected) check(variable(plan, name) == value, "switch " + name + "=" + value + " (got " + variable(plan, name).value_or("<unset>") + ")");
    check(std::find(plan.missing.begin(), plan.missing.end(), "battleye-runtime") != plan.missing.end() &&
          std::find(plan.missing.begin(), plan.missing.end(), "eac-runtime") == plan.missing.end(), "a runtime that is not installed is reported");
    // The other choices.
    o = {};
    o.sync = LaunchOptions::Sync::wineserver; o.hdr = LaunchOptions::Hdr::force; o.nvapi = LaunchOptions::Switch::off;
    auto other = wine_runner::plan(request, context);
    check(variable(other, "PROTON_NO_NTSYNC") == "1" && variable(other, "PROTON_NO_FSYNC") == "1" && variable(other, "PROTON_NO_ESYNC") == "1" &&
          variable(other, "PROTON_FORCE_HDR") == "1" && variable(other, "PROTON_DISABLE_NVAPI") == "1" && !variable(other, "DXVK_ENABLE_NVAPI"), "off switches");
    o.sync = LaunchOptions::Sync::esync; o.hdr = LaunchOptions::Hdr::off;
    other = wine_runner::plan(request, context);
    check(variable(other, "PROTON_NO_NTSYNC") == "1" && variable(other, "PROTON_NO_FSYNC") == "1" && !variable(other, "PROTON_NO_ESYNC") &&
          variable(other, "DXVK_NO_HDR") == "1" && !variable(other, "PROTON_ENABLE_HDR"), "esync only, HDR off");
    o.sync = LaunchOptions::Sync::fsync;
    other = wine_runner::plan(request, context);
    check(variable(other, "PROTON_NO_NTSYNC") == "1" && !variable(other, "PROTON_NO_FSYNC"), "fsync without ntsync");
    o.fsr_resolution = "1280x720; rm -rf"; o.fsr = true;
    check(!variable(wine_runner::plan(request, context), "WINE_FULLSCREEN_FSR_CUSTOM_MODE"), "a bad FSR resolution is left out");
}

void wrappers() {
    auto request = baseRequest();
    auto& o = request.launch.options;
    o.gamemode = o.mangohud = o.gamescope = true;
    o.wrapper = "obs-gamecapture --flag 'a b'";
    o.gamescope_game_width = 1280; o.gamescope_game_height = 720;
    o.gamescope_output_width = 2560; o.gamescope_output_height = 1440;
    o.gamescope_fps_limit = 60; o.gamescope_fps_limit_unfocused = 15;
    o.gamescope_window = LaunchOptions::GamescopeWindow::borderless;
    o.gamescope_filter = LaunchOptions::GamescopeFilter::fsr;
    o.gamescope_scaler = LaunchOptions::GamescopeScaler::fit;
    o.gamescope_sharpness = 5;
    o.gamescope_hdr = o.gamescope_grab_cursor = true;
    o.gamescope_options = "--adaptive-sync --expose-wayland";
    auto context = baseContext();
    auto plan = wine_runner::plan(request, context);
    const std::vector<std::string> expected{"obs-gamecapture", "--flag", "a b", "/usr/bin/gamemoderun", "/usr/bin/gamescope", "-w", "1280", "-h", "720",
        "-W", "2560", "-H", "1440", "-r", "60", "-o", "15", "-b", "-F", "fsr", "-S", "fit", "--sharpness", "5", "--hdr-enabled", "--force-grab-cursor",
        "--mangoapp", "--adaptive-sync", "--expose-wayland", "--", "/usr/bin/umu-run", "/games/bl3/game.exe", "-dx12"};
    check(plan.argv == expected, "wrapper order and gamescope options: " + join(plan.argv));
    // Without gamescope MangoHud wraps the runner itself.
    o.gamescope = false;
    plan = wine_runner::plan(request, context);
    check(plan.argv == std::vector<std::string>{"obs-gamecapture", "--flag", "a b", "/usr/bin/gamemoderun", "/usr/bin/mangohud", "/usr/bin/umu-run",
                                                "/games/bl3/game.exe", "-dx12"}, "gamemode and MangoHud: " + join(plan.argv));
    // Inside Steam's Game Mode gamescope is not nested.
    o.gamescope = true; o.wrapper.clear();
    context.inside_gamescope = true;
    plan = wine_runner::plan(request, context);
    check(plan.argv == std::vector<std::string>{"/usr/bin/gamemoderun", "/usr/bin/mangohud", "/usr/bin/umu-run", "/games/bl3/game.exe", "-dx12"}, "no nested gamescope");
    // Missing programs are reported and left out.
    context = baseContext();
    context.gamemoderun.clear(); context.gamescope.clear(); context.mangohud.clear();
    plan = wine_runner::plan(request, context);
    check(plan.argv == std::vector<std::string>{"/usr/bin/umu-run", "/games/bl3/game.exe", "-dx12"}, "missing wrappers skipped");
    check(plan.missing == std::vector<std::string>{"gamemoderun", "gamescope", "mangohud"}, "missing wrappers: " + join(plan.missing));
    for (const auto* code : {"no-tool", "no-umu", "no-wine", "no-program", "no-winetricks", "bad-verb", "gamemoderun", "mangohud", "gamescope", "dxvk",
                             "vkd3d-proton", "dxvk-nvapi", "eac-runtime", "battleye-runtime", "game-performance", "dlss-swapper", "lsfg-vk"})
        check(*wine_runner::problemText(code).polish && *wine_runner::problemText(code).english, std::string("text for ") + code);
    // CachyOS's wrappers go first, before GameMode; lsfg-vk is variables only.
    {
        auto cachy = baseRequest();
        auto& c = cachy.launch.options;
        c.game_performance = c.dlss_swapper = c.gamemode = true;
        c.lsfg = true; c.lsfg_multiplier = 3; c.lsfg_flow_scale = 75; c.lsfg_performance = true;
        auto with = baseContext();
        with.game_performance = "/usr/bin/game-performance"; with.dlss_swapper = "/usr/bin/dlss-swapper"; with.lsfg_layer = true;
        auto cachy_plan = wine_runner::plan(cachy, with);
        check(cachy_plan.argv == std::vector<std::string>{"/usr/bin/game-performance", "/usr/bin/dlss-swapper", "/usr/bin/gamemoderun", "/usr/bin/umu-run",
                                                          "/games/bl3/game.exe", "-dx12"} && cachy_plan.missing.empty(), "CachyOS wrappers: " + join(cachy_plan.argv));
        check(variable(cachy_plan, "LSFGVK_ENV") == "1" && variable(cachy_plan, "LSFGVK_MULTIPLIER") == "3" &&
              variable(cachy_plan, "LSFGVK_FLOW_SCALE") == "0.75" && variable(cachy_plan, "LSFGVK_PERFORMANCE_MODE") == "1", "lsfg-vk variables");
        cachy_plan = wine_runner::plan(cachy, baseContext());
        check(cachy_plan.missing == std::vector<std::string>{"game-performance", "dlss-swapper", "lsfg-vk"}, "missing CachyOS tools: " + join(cachy_plan.missing));
    }
    // Fullscreen, default filter: only -f.
    context = baseContext();
    o = {};
    o.gamescope = true;
    plan = wine_runner::plan(request, context);
    check(plan.argv == std::vector<std::string>{"/usr/bin/gamescope", "-f", "--", "/usr/bin/umu-run", "/games/bl3/game.exe", "-dx12"}, "gamescope defaults: " + join(plan.argv));
    // A tool (installer, winecfg) gets no wrappers, no options arguments, its own log.
    o.gamemode = true; o.arguments = "-windowed";
    request.purpose = wine_runner::Purpose::tool;
    plan = wine_runner::plan(request, context);
    check(plan.argv == std::vector<std::string>{"/usr/bin/umu-run", "/games/bl3/game.exe", "-dx12"} && plan.log_file == "/cfg/logs/wine/epic-catnip.tools.log",
          "tools run bare: " + join(plan.argv));
    request.purpose = wine_runner::Purpose::game;
    plan = wine_runner::plan(request, context);
    check(plan.argv.back() == "-windowed" && plan.argv[plan.argv.size() - 2] == "-dx12", "the options' arguments follow the action's");
}

void environmentOrder() {
    auto request = baseRequest();
    auto& o = request.launch.options;
    o.dxvk_hud = "fps";
    o.environment = {{"DXVK_HUD", "full"}, {"GAMEID", "umu-custom"}, {"PROTONPATH", "/mine"}};
    request.environment = {{"FUEL_DIR", "/amazon/fuel"}, {"DXVK_HUD", "action"}};
    const auto plan = wine_runner::plan(request, baseContext());
    const auto names = [&] { std::vector<std::string> result; for (const auto& [name, value] : plan.environment) result.push_back(name + "=" + value); return result; }();
    const auto position = [&](const std::string& entry) { return std::find(names.begin(), names.end(), entry) - names.begin(); };
    check(position("DXVK_HUD=fps") < position("FUEL_DIR=/amazon/fuel") && position("FUEL_DIR=/amazon/fuel") < position("GAMEID=umu-397540") &&
          position("GAMEID=umu-397540") < position("DXVK_HUD=full") && position("DXVK_HUD=full") < static_cast<long>(names.size()),
          "order: switches, the action's, the runner's, the user's: " + join(names));
    const auto final = block(plan, {"PATH=/usr/bin", "DXVK_HUD=launcher", "HOME=/home/u", "DISPLAY=:0"});
    check(final.at("DXVK_HUD") == "full" && final.at("GAMEID") == "umu-custom" && final.at("PROTONPATH") == "/mine" && final.at("FUEL_DIR") == "/amazon/fuel" &&
          final.at("PATH") == "/usr/bin" && final.at("HOME") == "/home/u" && final.at("DISPLAY") == ":0", "the user's variables win; the launcher's are kept");
    check(wine_runner::environmentBlock(plan, {"PATH=/usr/bin"}).size() == final.size() - 2, "each variable once");
}

void scriptsAndPrograms() {
    auto request = baseRequest();
    auto& o = request.launch.options;
    o.before_script = "/home/u/before it.sh";
    o.after_script = "/home/u/after's.sh";
    auto plan = wine_runner::plan(request, baseContext());
    check(plan.argv.size() == 7 && plan.argv[0] == "/bin/sh" && plan.argv[3] == "launchme" && plan.argv[4] == "/usr/bin/umu-run", "scripts wrap the command: " + join(plan.argv));
    const auto& script = plan.argv[2];
    check(script.find("if [ -x '/home/u/before it.sh' ]; then '/home/u/before it.sh'; else /bin/sh '/home/u/before it.sh'; fi\n") == 0, "before script first: " + script);
    check(script.find("\"$@\"\nstatus=$?\n") != std::string::npos && script.find("'/home/u/after'\\''s.sh'") != std::string::npos && script.ends_with("exit $status\n"),
          "after script keeps the game's status");
    o = {};
    // .msi, .bat, .lnk and Wine's own programs.
    request.program = "/downloads/setup.msi";
    request.arguments.clear();
    plan = wine_runner::plan(request, baseContext());
    check(plan.argv == std::vector<std::string>{"/usr/bin/umu-run", "msiexec", "/i", "Z:\\downloads\\setup.msi"}, "msi: " + join(plan.argv));
    request.program = "/pfx/borderlands-3/drive_c/Setup/x.msi";
    check(wine_runner::plan(request, baseContext()).argv[3] == "C:\\Setup\\x.msi", "msi inside drive_c");
    request.program = "/games/run game.bat";
    plan = wine_runner::plan(request, baseContext());
    check(plan.argv == std::vector<std::string>{"/usr/bin/umu-run", "cmd", "/c", "Z:\\games\\run game.bat"}, "bat: " + join(plan.argv));
    request.program = "/games/Game.lnk";
    plan = wine_runner::plan(request, baseContext());
    check(plan.argv == std::vector<std::string>{"/usr/bin/umu-run", "start", "/wait", "/unix", "/games/Game.lnk"}, "unresolved lnk through start: " + join(plan.argv));
    request.program = "winecfg";
    request.purpose = wine_runner::Purpose::tool;
    plan = wine_runner::plan(request, baseContext());
    check(plan.argv == std::vector<std::string>{"/usr/bin/umu-run", "winecfg"} && plan.working_directory.empty(), "winecfg");
    // createprefix: umu's own command, wineboot elsewhere.
    request.program = "createprefix";
    request.verb = "run";
    check(wine_runner::plan(request, baseContext()).argv == std::vector<std::string>{"/usr/bin/umu-run", "createprefix"}, "umu createprefix");
    auto context = baseContext();
    context.umu_run.clear();
    check(wine_runner::plan(request, context).argv == std::vector<std::string>{"/tools/GE-Proton11-11/proton", "run", "wineboot", "-u"}, "Proton's prefix");
    context.tool = wineBuild();
    check(wine_runner::plan(request, context).argv == std::vector<std::string>{"/tools/wine-11.20/bin/wine", "wineboot", "-u"}, "Wine's prefix");
}

void winetricks() {
    auto launch = baseRequest().launch;
    auto context = baseContext();
    auto plan = wine_runner::winetricksPlan(launch, {"vcrun2022", "d3dcompiler_47"}, context, "/usr/bin/winetricks");
    check(plan.argv == std::vector<std::string>{"/usr/bin/umu-run", "winetricks", "-q", "vcrun2022", "d3dcompiler_47"} &&
          variable(plan, "PROTONPATH") == "/tools/GE-Proton11-11" && variable(plan, "WINEPREFIX") == "/pfx/borderlands-3", "umu-run winetricks: " + join(plan.argv));
    check(wine_runner::winetricksPlan(launch, {}, context, {}).argv == std::vector<std::string>{"/usr/bin/umu-run", "winetricks", "--gui"}, "winetricks window");
    check(wine_runner::winetricksPlan(launch, {"--self-update"}, context, {}).error == "bad-verb" &&
          wine_runner::winetricksPlan(launch, {"a;b"}, context, {}).error == "bad-verb", "verbs are not options or shell");
    context.umu_run.clear();
    plan = wine_runner::winetricksPlan(launch, {"corefonts"}, context, "/usr/bin/winetricks");
    check(plan.argv == std::vector<std::string>{"/usr/bin/winetricks", "-q", "corefonts"} && variable(plan, "WINE") == "/tools/GE-Proton11-11/files/bin/wine" &&
          variable(plan, "WINESERVER") == "/tools/GE-Proton11-11/files/bin/wineserver" &&
          variable(plan, "LD_LIBRARY_PATH")->starts_with("/tools/GE-Proton11-11/files/lib64:/tools/GE-Proton11-11/files/lib"), "winetricks with Proton's Wine");
    check(wine_runner::winetricksPlan(launch, {"corefonts"}, context, {}).error == "no-winetricks", "no winetricks anywhere");
    context.tool = wineBuild();
    plan = wine_runner::winetricksPlan(launch, {"corefonts"}, context, "/usr/bin/winetricks");
    check(variable(plan, "WINE") == "/tools/wine-11.20/bin/wine" && !variable(plan, "LD_LIBRARY_PATH"), "winetricks with a Wine build");
}

void helpers() {
    check(wine_runner::windowsPath("/pfx/a/drive_c/Program Files/x.exe", "/pfx/a") == "C:\\Program Files\\x.exe", "C: path");
    check(wine_runner::windowsPath("/home/u/x.msi", "/pfx/a") == "Z:\\home\\u\\x.msi", "Z: path");
    check(wine_runner::unixPath("C:\\Program Files\\x.exe", "/pfx/a") == "/pfx/a/drive_c/Program Files/x.exe", "unix C:");
    check(wine_runner::unixPath("z:/home/u/x.exe", "/pfx/a") == "/home/u/x.exe" && wine_runner::unixPath("D:\\x", "/pfx/a") == "/pfx/a/dosdevices/d:/x", "unix Z: and D:");
    check(wine_runner::unixPath("C:\\..\\..\\etc", "/pfx/a").empty() && wine_runner::unixPath("relative\\x", "/pfx/a").empty(), "no escape, no relative path");
    check(wine_runner::shellWords("a 'b c' \"d \\\"e\\\"\" f\\ g ''") == std::vector<std::string>{"a", "b c", "d \"e\"", "f g", ""}, "shell words");
    check(wine_runner::shellQuote("it's") == "'it'\\''s'", "shell quote");
    CompatLaunch launch;
    launch.game_key = "epic:Fortnite";
    check(wine_runner::logName(launch) == "epic-fortnite", "log name");
    launch.game_key = "custom:0123456789abcdef";
    check(wine_runner::logName(launch) == "custom-0123456789abcdef", "custom log name");
    launch.game_key.clear();
    check(wine_runner::logName(launch) == "default", "no game: default");
    launch.game_key = "../..";
    check(wine_runner::logName(launch) == "default", "no escaping log names");
}

// What made a prefix, and the warning when a game's tool is of the other kind.
void prefixRunners(const fs::path& root) {
    const auto wine = root / "made-by-wine", proton = root / "made-by-proton", empty = root / "not-yet";
    fs::create_directories(wine / "drive_c" / "windows");
    fs::create_directories(proton / "drive_c" / "windows");
    fs::create_directories(empty);
    std::ofstream(proton / "version") << "GE-Proton10-25\n";
    std::ofstream(proton / "tracked_files") << "drive_c/windows/system32/d3d11.dll\n";
    check(wine_runner::prefixRunner(wine) == wine_runner::Runner::wine, "a Wine prefix");
    check(wine_runner::prefixRunner(proton) == wine_runner::Runner::proton, "a Proton prefix");
    check(wine_runner::prefixRunner(empty) == wine_runner::Runner::none, "no prefix yet");
    compat_tools::Tool ge; ge.kind = compat_tools::Kind::proton;
    compat_tools::Tool staging; staging.kind = compat_tools::Kind::wine;
    check(wine_runner::runnerMismatch(proton, staging) && wine_runner::runnerMismatch(wine, ge), "the other kind: warned");
    check(!wine_runner::runnerMismatch(proton, ge) && !wine_runner::runnerMismatch(wine, staging), "the same kind: quiet");
    check(!wine_runner::runnerMismatch(empty, staging) && !wine_runner::runnerMismatch(proton, std::nullopt), "unknown: quiet");
}

std::vector<uint8_t> shortcutBytes(const std::string& target, const std::u16string& folder, const std::u16string& arguments) {
    std::vector<uint8_t> bytes(76, 0);
    const auto put32 = [&](size_t at, uint32_t value) { for (int i = 0; i < 4; ++i) bytes[at + i] = static_cast<uint8_t>(value >> (8 * i)); };
    put32(0, 0x4C);
    const uint8_t clsid[16] = {0x01, 0x14, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46};
    std::copy(std::begin(clsid), std::end(clsid), bytes.begin() + 4);
    put32(20, 0x02 | 0x10 | 0x20 | 0x80); // LinkInfo, working folder, arguments, Unicode.
    // LinkInfo: header 0x1C, a 16-byte VolumeID, the ANSI local path, an empty suffix.
    const size_t info = bytes.size();
    const uint32_t volume = 0x1C, local = volume + 16, suffix = local + static_cast<uint32_t>(target.size()) + 1, size = suffix + 1;
    bytes.resize(info + size, 0);
    put32(info, size); put32(info + 4, 0x1C); put32(info + 8, 1); put32(info + 12, volume); put32(info + 16, local); put32(info + 24, suffix);
    put32(info + volume, 16);
    std::copy(target.begin(), target.end(), bytes.begin() + info + local);
    for (const auto* text : {&folder, &arguments}) {
        bytes.push_back(static_cast<uint8_t>(text->size())); bytes.push_back(static_cast<uint8_t>(text->size() >> 8));
        for (const char16_t unit : *text) { bytes.push_back(static_cast<uint8_t>(unit)); bytes.push_back(static_cast<uint8_t>(unit >> 8)); }
    }
    return bytes;
}

void shortcuts(const fs::path& root) {
    const auto bytes = shortcutBytes("C:\\Games\\Zażółć\\Game.exe", u"C:\\Games\\Zażółć", u"-skip \"two words\"");
    // The ANSI path is code-page text: its non-ASCII bytes are read as Latin-1.
    const auto parsed = wine_runner::parseShortcut(bytes);
    check(parsed && parsed->working_directory == "C:\\Games\\Zażółć" && parsed->arguments == "-skip \"two words\"", "shortcut strings");
    check(!wine_runner::parseShortcut(std::vector<uint8_t>(40, 0)) && !wine_runner::parseShortcut(std::vector<uint8_t>(bytes.begin(), bytes.begin() + 90)),
          "a damaged shortcut is refused");
    // Resolved inside the prefix, ignoring case.
    const auto prefix = root / "lnk-prefix";
    fs::create_directories(prefix / "drive_c" / "games" / "launcher");
    std::ofstream(prefix / "drive_c" / "games" / "launcher" / "GAME.EXE") << "MZ";
    const auto link = root / "Game.lnk";
    const auto good = shortcutBytes("C:\\Games\\Launcher\\game.exe", u"C:\\Games\\Launcher", u"-skip \"two words\"");
    std::ofstream(link, std::ios::binary).write(reinterpret_cast<const char*>(good.data()), static_cast<std::streamsize>(good.size()));
    Request request;
    request.program = link;
    request.arguments = {"-extra"};
    request.launch.prefix = prefix;
    const auto resolved = wine_runner::resolveShortcut(request);
    check(resolved.program == prefix / "drive_c" / "games" / "launcher" / "GAME.EXE", "shortcut target found: " + resolved.program.string());
    check(resolved.arguments == std::vector<std::string>{"-skip", "two words", "-extra"} && resolved.working_directory == prefix / "drive_c" / "games" / "launcher",
          "shortcut arguments and folder");
    const auto missing = shortcutBytes("C:\\Nowhere\\x.exe", u"", u"");
    std::ofstream(link, std::ios::binary | std::ios::trunc).write(reinterpret_cast<const char*>(missing.data()), static_cast<std::streamsize>(missing.size()));
    check(wine_runner::resolveShortcut(request).program == link, "unresolvable shortcut kept (start.exe opens it)");
}

LaunchOptions everything() {
    LaunchOptions o;
    o.compat_tool = "launchme:GE-Proton11-11"; o.prefix = "/pfx/x"; o.prefer_umu = false; o.umu_id = "umu-1";
    o.sync = LaunchOptions::Sync::wineserver; o.wined3d = true; o.wayland = true; o.hdr = LaunchOptions::Hdr::force; o.fsr = true; o.fsr_strength = 5;
    o.fsr_resolution = "1280x720"; o.fsr4_upgrade = true; o.xess_upgrade = true; o.large_address_aware = true; o.wow64 = true; o.dxvk_hud = "full";
    o.dxvk_frame_rate = 144; o.install_dxvk = true; o.install_vkd3d = true; o.eac_runtime = true; o.battleye_runtime = true; o.dll_overrides = "a=n";
    o.locale = "pl_PL.UTF-8"; o.log = true; o.disable_hidraw = true; o.prefer_sdl = true; o.pulse_latency = true; o.old_gl_string = true;
    o.nvapi = LaunchOptions::Switch::off; o.install_dxvk_nvapi = true; o.dlss_upgrade = true; o.dlss_indicator = true; o.hide_nvidia_gpu = true;
    o.prime_offload = true; o.gl_shader_cache_gb = 12; o.gl_shader_cache_keep = true; o.gl_threaded = true;
    o.dri_prime = true; o.radv_sam = true; o.radv_nggc = true; o.radv_emulate_rt = true; o.mesa_glthread = true;
    o.present_mode = LaunchOptions::PresentMode::relaxed; o.anisotropy = 8; o.mesa_shader_cache_size = "4G";
    o.gamemode = true; o.mangohud = true; o.wrapper = "strangle 60"; o.gamescope = true;
    o.game_performance = true; o.dlss_swapper = true; o.lsfg = true; o.lsfg_multiplier = 3; o.lsfg_flow_scale = 50; o.lsfg_performance = true;
    o.gamescope_game_width = 1; o.gamescope_game_height = 2; o.gamescope_output_width = 3; o.gamescope_output_height = 4;
    o.gamescope_fps_limit = 5; o.gamescope_fps_limit_unfocused = 6; o.gamescope_window = LaunchOptions::GamescopeWindow::windowed;
    o.gamescope_filter = LaunchOptions::GamescopeFilter::pixel; o.gamescope_scaler = LaunchOptions::GamescopeScaler::stretch;
    o.gamescope_sharpness = 20; o.gamescope_hdr = true; o.gamescope_grab_cursor = true; o.gamescope_options = "--rt";
    o.environment = {{"A", "1"}, {"B_2", "x=y"}}; o.arguments = "-a \"b c\""; o.working_directory = "/games";
    o.before_script = "/s/before.sh"; o.after_script = "/s/after.sh";
    return o;
}

void catalog() {
    namespace c = launch_options_catalog;
    std::set<std::string> ids;
    std::set<c::Section> sections;
    for (const auto& section : c::sections()) sections.insert(section.section);
    check(sections.size() == 8, "eight sections");
    const LaunchOptions defaults;
    for (const auto& option : c::options()) {
        check(ids.insert(option.id).second, std::string("unique option id ") + option.id);
        check(sections.contains(option.section), std::string("known section for ") + option.id);
        check(option.label.polish && *option.label.polish && option.label.english && *option.label.english && option.description.polish &&
              *option.description.polish && option.description.english && *option.description.english && option.sets && *option.sets &&
              option.get && option.set, std::string("complete texts for ") + option.id);
        check(option.type != c::Type::choice || !option.choices.empty(), std::string("choices for ") + option.id);
        check(!option.depends_on || c::find(option.depends_on), std::string("dependency exists for ") + option.id);
        // Every option has its own field: setting it changes it and nothing else.
        auto changed = defaults;
        const auto other = [&]() -> std::string {
            switch (option.type) {
            case c::Type::toggle: return c::value(defaults, option.id) == "1" ? "0" : "1";
            case c::Type::choice: return option.choices.back().value;
            case c::Type::number: return std::to_string(option.maximum);
            case c::Type::path: return "/some/where";
            case c::Type::environment: return "NAME=value";
            default: return "something";
            }
        }();
        check(c::setValue(changed, option.id, other) && changed != defaults && c::value(changed, option.id) == other, std::string("set ") + option.id);
        // A variable switch is one entry of the environment list, by design.
        for (const auto& rest : c::options())
            if (std::string_view(rest.id) != option.id && !(option.variable && std::string_view(rest.id) == "environment"))
                check(c::value(changed, rest.id) == c::value(defaults, rest.id), std::string(option.id) + " aliases " + rest.id);
    }
    // Variable switches and choices: entries of the environment list, never JSON keys of their own.
    {
        LaunchOptions v;
        check(c::setValue(v, "dxvk_low_latency", "1") && v.environment == std::vector<LaunchOptions::Variable>{{"PROTON_DXVK_LOWLATENCY", "1"}} &&
              c::value(v, "dxvk_low_latency") == "1", "variable switch on");
        check(c::setValue(v, "audio_driver", "pulse") && c::value(v, "audio_driver") == "pulse" && !c::setValue(v, "audio_driver", "oss"), "variable choice");
        const auto saved = launch_options_store::toJson(v);
        check(!saved.get("dxvk_low_latency") && !saved.get("audio_driver") && saved.get("environment"), "variables stored as the environment");
        const auto loaded = launch_options_store::fromJson(saved);
        check(c::value(loaded, "dxvk_low_latency") == "1" && c::value(loaded, "audio_driver") == "pulse", "variables read back");
        check(c::setValue(v, "dxvk_low_latency", "0") && c::setValue(v, "audio_driver", "") && v.environment.empty(), "variables removed");
        check(c::forVariable("PROTON_USE_OPTISCALER") == c::find("optiscaler") && !c::forVariable("NOT_A_SWITCH"), "variable lookup");
    }
    check(ids.size() >= 60, "every option listed: " + std::to_string(ids.size()));
    // Values each type refuses.
    LaunchOptions o;
    check(!c::setValue(o, "fsr_strength", "6") && !c::setValue(o, "fsr_strength", "-1") && !c::setValue(o, "fsr_strength", "2x") && c::setValue(o, "fsr_strength", "0"), "number limits");
    check(!c::setValue(o, "gamemode", "yes") && c::setValue(o, "gamemode", "true") && o.gamemode, "toggle text");
    check(!c::setValue(o, "sync", "turbo") && c::setValue(o, "sync", "ntsync") && o.sync == LaunchOptions::Sync::ntsync, "choice values");
    check(!c::setValue(o, "prefix", "relative/path") && c::setValue(o, "prefix", "") && o.prefix.empty(), "paths are absolute or empty");
    check(!c::setValue(o, "arguments", "a\nb") && !c::setValue(o, "environment", "1BAD=x") && !c::setValue(o, "environment", "NOEQUALS") &&
          c::setValue(o, "environment", "A=1\r\n\n  B=2=3\n") && o.environment == std::vector<LaunchOptions::Variable>{{"A", "1"}, {"B", "2=3"}}, "environment lines");
    check(!c::setValue(o, "no_such_option", "1") && c::value(o, "no_such_option").empty(), "unknown ids");
    check(!c::winetricksVerbs().empty() && c::text({L"pl", L"en"}) != nullptr, "verbs and texts");
    // WINEDLLOVERRIDES presets and their check list.
    check(c::dllOverride("dinput8=n,b;XInput1_3.dll=n", "xinput1_3") == std::optional<std::string>("n") && !c::dllOverride("a=n", "b"), "dll override lookup");
    check(c::withDllOverride("a,winhttp=n,b;c=b", "winhttp", std::nullopt) == "a=n,b;c=b", "dll removed from its entry");
    check(c::withDllOverride("a=n", "winhttp", "n,b") == "a=n;winhttp=n,b", "dll added");
    check(c::withDllOverride("winhttp=b", "winhttp", "n,b") == "winhttp=n,b", "dll mode replaced");
    check(c::withDllOverride("", "mscoree,mshtml", "") == "mscoree,mshtml=", "disabled dlls");
    check(c::presetSet("mscoree,mshtml=", c::dllPresets().back()) && !c::presetSet("mscoree=", c::dllPresets().back()), "preset state");
    for (const auto& preset : c::dllPresets()) {
        const auto set = c::withDllOverride("x=n", preset.dlls, std::string_view(preset.mode));
        check(c::presetSet(set, preset) && !c::presetSet(c::withDllOverride(set, preset.dlls, std::nullopt), preset), std::string("preset round trip ") + preset.dlls);
    }
    check(std::all_of(c::variablePresets().begin(), c::variablePresets().end(), [](const auto& preset) { return c::variableName(preset.name); }), "variable presets");
    // Round trip through JSON: the catalog covers every field.
    const auto all = everything();
    const auto back = launch_options_store::fromJson(launch_options_store::toJson(all));
    check(back == all, "every field survives JSON");
    check(json::serialize(launch_options_store::toJson(LaunchOptions{})) == "{}", "defaults are not written");
}

void store(const fs::path& root) {
    namespace s = launch_options_store;
    const auto config = root / "config";
    ::setenv("LAUNCHME_CONFIG_DIR", config.c_str(), 1);
    ::setenv("HOME", (root / "home").c_str(), 1);
    check(s::file() == config / "linux" / "launch_options.json", "store file");
    check(s::defaultPrefixRoot() == root / "home" / "Games" / "launchme" / "prefixes", "default prefix root");
    // Document round trip.
    s::Document document;
    document.defaults = everything();
    document.defaults.prefix.clear();
    document.games["epic:Catnip"] = LaunchOptions{};
    document.games["gog:1207658883"].gamemode = true;
    document.prefixes["epic:Catnip"] = "/pfx/catnip";
    document.prefix_root = "/games/prefixes";
    const auto parsed = s::parse(s::serialize(document));
    check(parsed && parsed->defaults == document.defaults && parsed->games == document.games && parsed->prefixes == document.prefixes &&
          parsed->prefix_root == document.prefix_root, "document round trip");
    check(parsed && parsed->overrides.at("gog:1207658883") == s::differences(document.games["gog:1207658883"], s::forGame(document.defaults)),
          "a game without a list overrides what differs from the default");
    check(parsed && s::parse(s::serialize(*parsed)) == parsed, "overrides round trip");
    check(!s::parse("{\"version\":3}") && !s::parse("[]") && !s::parse("not json"), "unknown documents refused");
    const auto lenient = s::parse(R"({"version":1,"default":{"gamemode":"yes","fsr_strength":9,"sync":"ntsync","environment":[{"name":"OK","value":"1"},{"name":"1X","value":"2"}]},
        "games":{"":{},"steam:10":{"mangohud":true}},"prefixes":{"steam:10":"relative"}})");
    check(lenient && !lenient->defaults.gamemode && lenient->defaults.fsr_strength == 2 && lenient->defaults.sync == LaunchOptions::Sync::ntsync &&
          lenient->defaults.environment == std::vector<LaunchOptions::Variable>{{"OK", "1"}} && lenient->games.size() == 1 && lenient->prefixes.empty(),
          "invalid values keep their defaults");
    // Version 1: each game a full copy; what differs from the default becomes its override.
    check(lenient && lenient->overrides.at("steam:10") == std::set<std::string>{"env:OK", "mangohud", "sync"} && lenient->games.at("steam:10").mangohud &&
          lenient->games.at("steam:10").sync == LaunchOptions::Sync::automatic && lenient->games.at("steam:10").environment.empty(), "version 1 games as overrides");
    const auto old = s::parse(R"({"version":1,"default":{},"games":{"custom:1":{"umu_id":"umu-1091500","arguments":"-dx11","environment":[{"name":"A","value":"1"}]}}})");
    const auto reread = old ? s::parse(s::serialize(*old)) : std::nullopt;
    check(reread && reread->games.at("custom:1").umu_id == "umu-1091500" && reread->games.at("custom:1").arguments == "-dx11" &&
          reread->games.at("custom:1").environment == std::vector<LaunchOptions::Variable>{{"A", "1"}}, "version 1 overrides kept in version 2");
    // The file.
    check(s::defaults() == LaunchOptions{} && !s::gameOptions("epic:Catnip"), "empty store");
    auto mine = LaunchOptions{};
    mine.mangohud = true;
    check(s::setDefaultTool("launchme:GE-Proton11-11") && s::defaultTool() == "launchme:GE-Proton11-11", "global tool");
    check(s::setGameOptions("epic:Catnip", mine) && s::gameOptions("epic:Catnip") == mine && s::effectiveOptions("epic:Catnip") == mine, "game options");
    check(s::effectiveOptions("gog:1").compat_tool == "launchme:GE-Proton11-11", "others follow the default");
    check(s::gamesUsingTool("launchme:GE-Proton11-11") == std::vector<std::string>{"default"}, "tool users");
    // Prefixes: assigned once per game, unique per title, kept after a rename.
    Game game;
    game.source = GameSource::epic; game.kind = GameKind::store; game.source_id = "Catnip"; game.name = L"Borderlands 3: Ultimate Edition";
    game.install_directory = "/games/bl3";
    const auto launch = s::compatLaunch(game);
    const auto prefixes = root / "home" / "Games" / "launchme" / "prefixes";
    check(launch->prefix == prefixes / "borderlands-3-ultimate-edition", "prefix by title: " + launch->prefix.string());
    check(launch->store == "egs" && launch->store_id == "Catnip" && launch->options.mangohud && launch->options.compat_tool == "launchme:GE-Proton11-11" &&
          launch->install_directory == "/games/bl3" && launch->game_key == "epic:Catnip", "compat launch of a store game");
    game.name = L"Borderlands 3 (renamed)";
    check(s::compatLaunch(game)->prefix == prefixes / "borderlands-3-ultimate-edition", "rename keeps the prefix");
    Game twin = game;
    twin.source = GameSource::gog; twin.source_id = "123"; twin.name = L"Borderlands 3: Ultimate Edition";
    check(s::compatLaunch(twin)->prefix == prefixes / "borderlands-3-ultimate-edition-2" && s::compatLaunch(twin)->store == "gog", "same title, own prefix");
    Game steam;
    steam.source = GameSource::steam; steam.kind = GameKind::steam; steam.app_id = 397540; steam.name = L"Default";
    const auto steam_launch = s::compatLaunch(steam);
    check(steam_launch->steam_app_id == 397540 && steam_launch->store == "steam" && steam_launch->prefix == prefixes / "default-2", "Steam game, reserved name");
    check(s::gamesUsingPrefix(prefixes / "borderlands-3-ultimate-edition") == std::vector<std::string>{"epic:Catnip"}, "prefix users");
    check(s::existingPrefix("epic:Catnip") == prefixes / "borderlands-3-ultimate-edition" && !s::existingPrefix("gog:never-launched") &&
          !s::existingPrefix({}), "an existing prefix, never a new one");
    check(!s::existingPrefix("gog:never-launched"), "asking assigns nothing");
    mine.prefix = "/shared/ea";
    s::setGameOptions("epic:Catnip", mine);
    check(s::compatLaunch(game)->prefix == "/shared/ea" && s::gamePrefix(game) == "/shared/ea", "an own prefix wins");
    check(s::resetGameOptions("epic:Catnip") && !s::gameOptions("epic:Catnip") && s::compatLaunch(game)->prefix == prefixes / "borderlands-3-ultimate-edition",
          "reset to the default");
    check(s::defaultCompatLaunch().prefix == prefixes / "default", "no game: the shared prefix");
    check(s::setPrefixRoot("/games/pfx") && s::prefixRoot() == "/games/pfx" && !s::setPrefixRoot("relative") && s::setPrefixRoot({}) && s::prefixRoot() == prefixes,
          "prefix root");
    check(s::slug(L"Wiedźmin 3: Dziki Gon", "x") == "wiedzmin-3-dziki-gon" && s::slug(L"Assassin's Creed® II", "x") == "assassins-creed-ii" &&
          s::slug(L"ゼルダ", "steam:10") == "steam-10" && s::slug(L"", "") == "game" && s::slug(std::wstring(200, L'a'), "").size() == 64, "slugs");
    const auto text = readText(s::file());
    check(text.find("\"prefixes\"") != std::string::npos && text.find("borderlands-3-ultimate-edition") != std::string::npos, "prefixes persisted");
    // The prefix manager: a named prefix with its own tool, chosen by a game; the tool order.
    const auto shared = s::newPrefix(L"Sklepy: EA i Ubisoft", "launchme:Wine-Staging-TkG-11.20");
    check(shared == prefixes / "sklepy-ea-i-ubisoft" && fs::is_directory(shared) && s::newPrefix(L"Sklepy: EA i Ubisoft", "default") == prefixes / "sklepy-ea-i-ubisoft-2",
          "named prefixes: " + shared.string());
    check(s::prefixInfo(shared).name == L"Sklepy: EA i Ubisoft" && s::prefixInfo(shared).compat_tool == "launchme:Wine-Staging-TkG-11.20", "prefix info");
    auto in_shared = LaunchOptions{};
    in_shared.prefix = shared;
    s::setGameOptions("epic:Catnip", in_shared);
    check(s::compatLaunch(game)->prefix == shared && s::compatLaunch(game)->options.compat_tool == "launchme:Wine-Staging-TkG-11.20", "the prefix's tool");
    in_shared.compat_tool = "launchme:GE-Proton11-12";
    s::setGameOptions("epic:Catnip", in_shared);
    check(s::compatLaunch(game)->options.compat_tool == "launchme:GE-Proton11-12" && s::effectiveTool("epic:Catnip", shared) == "launchme:GE-Proton11-12",
          "the game's own tool wins");
    check(s::effectiveTool("gog:123", prefixes / "borderlands-3-ultimate-edition-2") == "launchme:GE-Proton11-11", "else the global tool");
    fs::create_directories(prefixes / "borderlands-3-ultimate-edition"); // Made by the game's earlier launches.
    const auto list = s::prefixes();
    const auto entry = std::find_if(list.begin(), list.end(), [&](const auto& item) { return item.path == shared; });
    check(!list.empty() && list.front().named && entry != list.end() && entry->games == std::vector<std::string>{"epic:Catnip"}, "prefix list: named first, users");
    check(std::any_of(list.begin(), list.end(), [&](const auto& item) { return item.shared && item.path == prefixes / "default"; }),
          "the shared prefix listed");
    check(std::any_of(list.begin(), list.end(), [&](const auto& item) { return item.path == prefixes / "borderlands-3-ultimate-edition" && item.games.empty(); }),
          "a prefix no game uses any more listed");
    check(s::setPrefixInfo(shared, {L"EA", "default"}) && s::prefixInfo(shared).name == L"EA" && s::compatLaunch(game)->options.compat_tool == "launchme:GE-Proton11-12",
          "rename");
    const auto stored = s::load();
    check(s::parse(s::serialize(stored)) == stored, "prefix info round trip");
    check(s::forgetPrefix(shared) && s::gameOptions("epic:Catnip")->prefix.empty() && s::prefixInfo(shared).name.empty(), "forgotten prefix");
    s::resetGameOptions("epic:Catnip");
    // Overrides: a game keeps what it sets itself and follows the default in everything else.
    auto base = LaunchOptions{};
    base.environment = {{"KEEP", "1"}, {"DROP", "1"}};
    check(s::setDefaults(base), "a default with variables");
    auto own = s::forGame(s::defaults());
    own.mangohud = true;
    own.environment = {{"KEEP", "1"}, {"MINE", "2"}}; // DROP left out.
    check(s::setGameOptions("gog:7", own) && s::gameOverrides("gog:7") == std::set<std::string>{"env:DROP", "env:MINE", "mangohud"}, "overrides: what differs");
    base.gamemode = true;
    base.environment = {{"KEEP", "2"}, {"DROP", "1"}, {"NEW", "3"}};
    check(s::setDefaults(base), "a new default");
    const auto now = s::gameOptions("gog:7");
    check(now && now->gamemode && now->mangohud && now->environment == std::vector<LaunchOptions::Variable>{{"KEEP", "2"}, {"NEW", "3"}, {"MINE", "2"}},
          "the game follows the new default except its overrides");
    auto again = *now;
    again.mangohud = false; // Back to the default's value: still the game's own choice.
    check(s::setGameOptions("gog:7", again) && s::gameOverrides("gog:7").contains("mangohud"), "an override equal to the default stays");
    check(s::resetGameOption("gog:7", "mangohud") && s::gameOverrides("gog:7") == std::set<std::string>{"env:DROP", "env:MINE"}, "one override reset");
    const auto written = readText(s::file());
    check(written.find("\"environment_unset\"") != std::string::npos && written.find("\"MINE\"") != std::string::npos, "overrides written");
    check(s::resetGameOption("gog:7", "env:DROP") && s::resetGameOption("gog:7", "env:MINE") && !s::gameOptions("gog:7"), "the last override: back to the default");
    s::setDefaults(LaunchOptions{});
}

// executeGameAction with fake runners: Windows programs reach umu-run, Proton or Wine with the
// game's options and the action's variables, and stay the launch job's session.
void launches(const fs::path& root) {
    const auto home = root / "home";
    const auto config = root / "config";
    const auto bin = root / "bin";
    const auto out = root / "out";
    fs::create_directories(out);
    fs::remove_all(config);
    ::setenv("LAUNCHME_CONFIG_DIR", config.c_str(), 1);
    ::setenv("HOME", home.c_str(), 1);
    ::setenv("XDG_DATA_HOME", (home / ".local" / "share").c_str(), 1);
    ::setenv("LAUNCHME_TEST_OUT", out.c_str(), 1);
    ::unsetenv("STEAM_DIR");
    ::unsetenv("GAMESCOPE_WAYLAND_DISPLAY");
    const std::string previous_path = std::getenv("PATH") ? std::getenv("PATH") : "";
    ::setenv("PATH", (bin.string() + ":/usr/bin:/bin").c_str(), 1);
    const std::string record = "printf '%s\\n' \"$0\" \"$@\" > \"$LAUNCHME_TEST_OUT/args\"\nenv > \"$LAUNCHME_TEST_OUT/env\"\nsleep 1\n";
    writeScript(bin / "umu-run", record);
    // umu-database, cached: GAMEID for the store's game.
    fs::create_directories(compat_tools::umuDatabaseFile(config).parent_path());
    std::ofstream(compat_tools::umuDatabaseFile(config)) << "TITLE,STORE,CODENAME,UMU_ID\nBorderlands 3,egs,Catnip,umu-397540\n";
    const auto game_folder = root / "games" / "bl3";
    fs::create_directories(game_folder);
    std::ofstream(game_folder / "Game.exe") << "MZ";

    Game game;
    game.source = GameSource::epic; game.kind = GameKind::store; game.source_id = "Catnip"; game.name = L"Borderlands 3";
    game.install_directory = game_folder;
    GameAction action{(game_folder / "Game.exe").wstring(), L"-x \"y z\"", {}, ActionKind::play};
    action.environment = {{L"FUEL_DIR", L"/amazon/fuel"}};
    action.compat = launch_options_store::compatLaunch(game);
    const auto env = [&] {
        std::map<std::string, std::string> result;
        for (const auto& line : lines(out / "env")) if (const auto at = line.find('='); at != std::string::npos) result[line.substr(0, at)] = line.substr(at + 1);
        return result;
    };
    // umu-run (umu's own UMU-Proton: nothing else installed).
    check(executeGameAction(nullptr, action) == 33, "Windows program started through umu-run");
    const auto process = takeLaunchedProcess();
    check(process > 0 && launchJob(process) == process, "the runner leads the launch job's session");
    check(waitFor([&] { return lines(out / "args").size() == 4; }), "umu-run ran");
    check(lines(out / "args") == std::vector<std::string>{(bin / "umu-run").string(), (game_folder / "Game.exe").string(), "-x", "y z"}, "umu-run arguments: " + join(lines(out / "args")));
    check(waitFor([&] { return env().count("GAMEID"); }), "umu-run environment written");
    auto vars = env();
    const auto prefix = home / "Games" / "launchme" / "prefixes" / "borderlands-3";
    check(vars["GAMEID"] == "umu-397540" && vars["STORE"] == "egs" && vars["WINEPREFIX"] == prefix.string() && !vars.count("PROTONPATH") &&
          vars["PROTON_VERB"] == "waitforexitandrun" && vars["FUEL_DIR"] == "/amazon/fuel" && vars["STEAM_COMPAT_INSTALL_PATH"] == game_folder.string(),
          "umu-run variables, the umu-database GAMEID and Amazon's variables");
    check(readLaunchJob(launchJob(process)) == GameActivity::running, "the game runs in its session");
    check(waitFor([&] { return readLaunchJob(launchJob(process)) == GameActivity::stopped; }), "the session ends with the game");
    check(readText(config / "logs" / "wine" / "epic-catnip.log").starts_with("launchme: " + (bin / "umu-run").string()), "the runner's output goes to the game's log");

    // Proton directly, chosen per game.
    fs::remove(out / "args"); fs::remove(out / "env");
    writeScript(config / "compatibilitytools" / "GE-Proton11-11" / "proton", record);
    std::ofstream(config / "compatibilitytools" / "GE-Proton11-11" / "version") << "1760000000 GE-Proton11-11\n";
    auto options = LaunchOptions{};
    options.compat_tool = "launchme:GE-Proton11-11";
    options.prefer_umu = false;
    options.gamemode = true; // Not installed here: skipped.
    launch_options_store::setGameOptions(gameKey(game), options);
    action.compat = launch_options_store::compatLaunch(game);
    check(executeGameAction(nullptr, action) == 33, "Windows program started through Proton");
    takeLaunchedProcess();
    check(waitFor([&] { return lines(out / "args").size() == 5 && env().count("STEAM_COMPAT_DATA_PATH"); }), "proton ran");
    check(lines(out / "args") == std::vector<std::string>{(config / "compatibilitytools" / "GE-Proton11-11" / "proton").string(), "waitforexitandrun",
                                                           (game_folder / "Game.exe").string(), "-x", "y z"}, "proton arguments: " + join(lines(out / "args")));
    vars = env();
    check(vars["STEAM_COMPAT_DATA_PATH"] == prefix.string() && vars["SteamGameId"] == "launchme-epic-catnip" && fs::is_symlink(prefix / "pfx") &&
          fs::read_symlink(prefix / "pfx") == ".", "Proton's compat data path and the pfx link");

    // A Wine build: an installer (.msi) runs without the game's wrappers.
    fs::remove(out / "args"); fs::remove(out / "env");
    writeScript(config / "compatibilitytools" / "wine-test" / "bin" / "wine", record);
    options.compat_tool = "launchme:wine-test";
    launch_options_store::setGameOptions(gameKey(game), options);
    std::ofstream(game_folder / "setup.msi") << "MSI";
    GameAction installer{(game_folder / "setup.msi").wstring(), {}, {}, ActionKind::install};
    installer.compat = launch_options_store::compatLaunch(game);
    check(executeGameAction(nullptr, installer) == 33, "installer started through Wine");
    takeLaunchedProcess();
    check(waitFor([&] { return lines(out / "args").size() == 4 && env().count("WINEPREFIX"); }), "wine ran");
    std::string windows = "Z:" + (game_folder / "setup.msi").string();
    std::replace(windows.begin(), windows.end(), '/', '\\');
    check(lines(out / "args") == std::vector<std::string>{(config / "compatibilitytools" / "wine-test" / "bin" / "wine").string(), "msiexec", "/i", windows},
          "msiexec arguments: " + join(lines(out / "args")));
    vars = env();
    check(vars["WINEDEBUG"] == "-all" && vars["WINEDLLOVERRIDES"] == "winemenubuilder.exe=d" && vars["WINEPREFIX"] == prefix.string(), "Wine variables");
    check(fs::exists(config / "logs" / "wine" / "epic-catnip.tools.log"), "installers log apart from the game");

    // A native program gets the action's variables too.
    const auto script = game_folder / "native.sh";
    writeScript(script, "echo \"$FUEL_DIR\" > \"$LAUNCHME_TEST_OUT/native\"\n");
    GameAction native{script.wstring(), {}, {}, ActionKind::play};
    native.environment = {{L"FUEL_DIR", L"/native/fuel"}};
    check(executeGameAction(nullptr, native) == 33, "native program started");
    takeLaunchedProcess();
    check(waitFor([&] { return readText(out / "native") == "/native/fuel\n"; }), "native program got the action's variables");

    // Nothing installed: the old answer.
    fs::remove_all(config / "compatibilitytools");
    fs::remove(bin / "umu-run");
    ::setenv("PATH", bin.c_str(), 1);
    check(executeGameAction(nullptr, action) == launch_error::no_handler && takeLaunchedProcess() == 0, "no runner: no handler");
    ::setenv("PATH", previous_path.c_str(), 1);
}
}

int main() {
    const auto root = fs::temp_directory_path() / ("launchme-wine-tests-" + std::to_string(getpid()));
    fs::create_directories(root);
    umuPlans();
    protonPlans();
    winePlans();
    protonSwitches();
    wrappers();
    environmentOrder();
    scriptsAndPrograms();
    winetricks();
    helpers();
    prefixRunners(root / "runners");
    shortcuts(root);
    catalog();
    store(root / "store");
    launches(root / "launch");
    std::error_code ignored;
    fs::remove_all(root, ignored);
    if (failures) { std::cerr << failures << " failure(s)\n"; return 1; }
    std::cout << "Wine/Proton/umu runner, options catalog, store and launches passed\n";
    return 0;
}
