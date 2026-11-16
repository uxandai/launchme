#include "wine_runner.hpp"
#include "proc_fs.hpp"
#include "process_spawn.hpp"
#include "system.hpp"
#include "text_encoding.hpp"
#include "user_config.hpp"
#include "../library/launch_options_store.hpp"
#include "../library/xdg_paths.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <unistd.h>

extern char** environ;

namespace wine_runner {
namespace {
namespace fs = std::filesystem;
using Environment = std::vector<std::pair<std::string, std::string>>;
using Options = LaunchOptions;

std::string lowerAscii(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return text;
}
std::string extension(const fs::path& file) { return lowerAscii(file.extension().string()); }
bool isFolder(const fs::path& path) { std::error_code ec; return fs::is_directory(path, ec); }
bool isFile(const fs::path& path) { std::error_code ec; return fs::is_regular_file(path, ec); }
void add(Environment& environment, std::string name, std::string value) { environment.emplace_back(std::move(name), std::move(value)); }

// lsfg-vk's Vulkan layer: its manifest in a Vulkan layer folder (system, /usr/local, the user's), or its
// Flatpak extension (org.freedesktop.Platform.VulkanLayer.lsfg…, for Flatpak Steam).
bool lsfgLayerInstalled() {
    std::vector<fs::path> folders;
    const auto data = xdg_paths::dataHome();
    for (const fs::path& base : {fs::path("/usr/share/vulkan"), fs::path("/usr/local/share/vulkan"), fs::path("/etc/vulkan"),
                                data.empty() ? fs::path{} : data / "vulkan"})
        if (!base.empty()) for (const auto* kind : {"implicit_layer.d", "explicit_layer.d"}) folders.push_back(base / kind);
    for (const fs::path& runtime : {fs::path("/var/lib/flatpak/runtime"), data.empty() ? fs::path{} : data / "flatpak" / "runtime"})
        if (!runtime.empty()) folders.push_back(runtime);
    for (const auto& folder : folders) {
        std::error_code ec;
        for (fs::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec))
            if (lowerAscii(it->path().filename().string()).find("lsfg") != std::string::npos) return true;
    }
    return false;
}

// The Wine prefix inside a prefix folder: <prefix>/pfx when that is a real folder (a Proton prefix
// made by Steam or Heroic), else the folder itself (umu's pfx → . link, or plain Wine).
fs::path winePrefix(const fs::path& prefix) {
    std::error_code ec;
    const auto pfx = prefix / "pfx";
    return fs::is_directory(fs::symlink_status(pfx, ec)) ? pfx : prefix;
}

std::vector<std::string> gamescopeArguments(const Options& o, const fs::path& gamescope, bool mangoapp) {
    std::vector<std::string> result{gamescope.string()};
    const auto number = [&](const char* flag, uint32_t value) { if (value) result.insert(result.end(), {flag, std::to_string(value)}); };
    number("-w", o.gamescope_game_width);
    number("-h", o.gamescope_game_height);
    number("-W", o.gamescope_output_width);
    number("-H", o.gamescope_output_height);
    number("-r", o.gamescope_fps_limit);
    number("-o", o.gamescope_fps_limit_unfocused);
    if (o.gamescope_window == Options::GamescopeWindow::fullscreen) result.push_back("-f");
    else if (o.gamescope_window == Options::GamescopeWindow::borderless) result.push_back("-b");
    static constexpr const char* filters[] = {"", "linear", "nearest", "fsr", "nis", "pixel"};
    static constexpr const char* scalers[] = {"", "integer", "fit", "fill", "stretch"};
    if (o.gamescope_filter != Options::GamescopeFilter::automatic) result.insert(result.end(), {"-F", filters[static_cast<size_t>(o.gamescope_filter)]});
    if (o.gamescope_scaler != Options::GamescopeScaler::automatic) result.insert(result.end(), {"-S", scalers[static_cast<size_t>(o.gamescope_scaler)]});
    if (o.gamescope_filter == Options::GamescopeFilter::fsr || o.gamescope_filter == Options::GamescopeFilter::nis)
        result.insert(result.end(), {"--sharpness", std::to_string(std::min<uint32_t>(o.gamescope_sharpness, 20))});
    if (o.gamescope_hdr) result.push_back("--hdr-enabled");
    if (o.gamescope_grab_cursor) result.push_back("--force-grab-cursor");
    if (mangoapp) result.push_back("--mangoapp");
    const auto extra = shellWords(o.gamescope_options);
    result.insert(result.end(), extra.begin(), extra.end());
    result.push_back("--");
    return result;
}

bool resolution(std::string_view text) {
    const auto x = text.find('x');
    if (x == std::string_view::npos || x == 0 || x + 1 >= text.size() || text.size() > 11) return false;
    return std::all_of(text.begin(), text.end(), [](char ch) { return ch == 'x' || (ch >= '0' && ch <= '9'); }) && text.find('x', x + 1) == std::string_view::npos;
}

// Everything the options' switches set (before the action's own variables and the runner's).
Environment switches(const Options& o, Runner runner, const Context& context, const fs::path& log_folder, Plan& plan) {
    Environment env;
    const bool proton = runner == Runner::umu || runner == Runner::proton;
    if (proton) {
        switch (o.sync) {
        case Options::Sync::ntsync: add(env, "PROTON_USE_NTSYNC", "1"); break;
        case Options::Sync::fsync: add(env, "PROTON_NO_NTSYNC", "1"); break;
        case Options::Sync::esync: add(env, "PROTON_NO_NTSYNC", "1"); add(env, "PROTON_NO_FSYNC", "1"); break;
        case Options::Sync::wineserver:
            add(env, "PROTON_NO_NTSYNC", "1"); add(env, "PROTON_NO_FSYNC", "1"); add(env, "PROTON_NO_ESYNC", "1"); break;
        default: break;
        }
        if (o.wined3d) add(env, "PROTON_USE_WINED3D", "1");
        if (o.wayland) add(env, "PROTON_ENABLE_WAYLAND", "1");
    } else {
        if (o.sync == Options::Sync::fsync) { add(env, "WINEFSYNC", "1"); add(env, "WINEESYNC", "1"); }
        else if (o.sync == Options::Sync::esync) add(env, "WINEESYNC", "1");
        if (o.wayland) plan.unset.push_back("DISPLAY"); // Wine's default drivers "x11,wayland": X11 cannot connect.
    }
    switch (o.hdr) {
    case Options::Hdr::on: if (proton) add(env, "PROTON_ENABLE_HDR", "1"); add(env, "DXVK_HDR", "1"); break;
    case Options::Hdr::force:
        if (proton) { add(env, "PROTON_ENABLE_HDR", "1"); add(env, "PROTON_FORCE_HDR", "1"); }
        add(env, "DXVK_HDR", "1");
        break;
    case Options::Hdr::off: add(env, "DXVK_NO_HDR", "1"); break;
    default: break;
    }
    if (o.fsr) {
        add(env, "WINE_FULLSCREEN_FSR", "1");
        add(env, "WINE_FULLSCREEN_FSR_STRENGTH", std::to_string(std::min<uint32_t>(o.fsr_strength, 5)));
        if (resolution(o.fsr_resolution)) add(env, "WINE_FULLSCREEN_FSR_CUSTOM_MODE", o.fsr_resolution);
    }
    if (proton && o.fsr4_upgrade) add(env, "PROTON_FSR4_UPGRADE", "1");
    if (proton && o.xess_upgrade) add(env, "PROTON_XESS_UPGRADE", "1");
    if (o.large_address_aware) add(env, proton ? "PROTON_FORCE_LARGE_ADDRESS_AWARE" : "WINE_LARGE_ADDRESS_AWARE", "1");
    if (o.wow64) {
        if (proton) add(env, "PROTON_USE_WOW64", "1");
        else add(env, "WINEARCH", "wow64");
    }
    if (!o.dxvk_hud.empty()) add(env, "DXVK_HUD", o.dxvk_hud);
    if (o.dxvk_frame_rate) {
        add(env, "DXVK_FRAME_RATE", std::to_string(o.dxvk_frame_rate));
        add(env, "VKD3D_FRAME_RATE", std::to_string(o.dxvk_frame_rate));
    }
    if (proton && o.eac_runtime) {
        if (!context.eac_runtime.empty()) add(env, "PROTON_EAC_RUNTIME", context.eac_runtime.string());
        else plan.missing.push_back("eac-runtime");
    }
    if (proton && o.battleye_runtime) {
        if (!context.battleye_runtime.empty()) add(env, "PROTON_BATTLEYE_RUNTIME", context.battleye_runtime.string());
        else plan.missing.push_back("battleye-runtime");
    }
    if (!o.locale.empty()) add(env, proton ? "HOST_LC_ALL" : "LC_ALL", o.locale);
    if (proton && o.disable_hidraw) add(env, "PROTON_DISABLE_HIDRAW", "1");
    if (proton && o.prefer_sdl) add(env, "PROTON_PREFER_SDL", "1");
    if (o.pulse_latency) add(env, "PULSE_LATENCY_MSEC", "60");
    if (o.old_gl_string) {
        if (proton) add(env, "PROTON_OLD_GL_STRING", "1");
        else { add(env, "MESA_EXTENSION_MAX_YEAR", "2003"); add(env, "__GL_ExtensionStringVersion", "17700"); }
    }
    if (o.log) {
        if (proton) {
            add(env, "PROTON_LOG", "1");
            add(env, "PROTON_LOG_DIR", log_folder.string());
            if (runner == Runner::umu) add(env, "UMU_LOG", "1");
        } else add(env, "DXVK_LOG_PATH", log_folder.string());
    } else if (!proton) {
        add(env, "WINEDEBUG", "-all");
        add(env, "DXVK_LOG_LEVEL", "none");
    }
    // NVIDIA.
    if (o.nvapi == Options::Switch::on) {
        if (proton) add(env, "PROTON_ENABLE_NVAPI", "1");
        add(env, "DXVK_ENABLE_NVAPI", "1");
    } else if (o.nvapi == Options::Switch::off && proton) add(env, "PROTON_DISABLE_NVAPI", "1");
    if (proton && o.dlss_upgrade) add(env, "PROTON_DLSS_UPGRADE", "1");
    if (o.dlss_indicator) {
        if (proton) add(env, "PROTON_DLSS_INDICATOR", "1");
        add(env, "DXVK_NVAPI_SET_NGX_DEBUG_OPTIONS", "DLSSIndicator=1024,DLSSGIndicator=2");
    }
    if (proton && o.hide_nvidia_gpu) add(env, "PROTON_HIDE_NVIDIA_GPU", "1");
    if (o.prime_offload) {
        add(env, "__NV_PRIME_RENDER_OFFLOAD", "1");
        add(env, "__GLX_VENDOR_LIBRARY_NAME", "nvidia");
        add(env, "__VK_LAYER_NV_optimus", "NVIDIA_only");
    }
    if (o.gl_shader_cache_gb) add(env, "__GL_SHADER_DISK_CACHE_SIZE", std::to_string(uint64_t{o.gl_shader_cache_gb} * 1024 * 1024 * 1024));
    if (o.gl_shader_cache_keep) add(env, "__GL_SHADER_DISK_CACHE_SKIP_CLEANUP", "1");
    if (o.gl_threaded) add(env, "__GL_THREADED_OPTIMIZATIONS", "1");
    // AMD and Mesa.
    if (o.dri_prime) add(env, "DRI_PRIME", "1");
    std::string perftest;
    if (o.radv_sam) perftest += "sam";
    if (o.radv_nggc) perftest += std::string(perftest.empty() ? "" : ",") + "nggc";
    if (!perftest.empty()) add(env, "RADV_PERFTEST", perftest);
    if (o.radv_emulate_rt) add(env, "RADV_EXPERIMENTAL", "emulate_rt");
    if (o.mesa_glthread) add(env, "mesa_glthread", "true");
    static constexpr const char* present_modes[] = {"", "fifo", "mailbox", "immediate", "relaxed"};
    if (o.present_mode != Options::PresentMode::automatic) add(env, "MESA_VK_WSI_PRESENT_MODE", present_modes[static_cast<size_t>(o.present_mode)]);
    if (o.anisotropy) add(env, "RADV_TEX_ANISO", std::to_string(std::min<uint32_t>(o.anisotropy, 16)));
    if (!o.mesa_shader_cache_size.empty()) add(env, "MESA_SHADER_CACHE_MAX_SIZE", o.mesa_shader_cache_size);
    // DLL overrides: plain Wine keeps winemenubuilder away from file associations (as Heroic) and loads
    // the prefix's DXVK/VKD3D-Proton/DXVK-NVAPI; Proton appends WINEDLLOVERRIDES to its own.
    if (runner == Runner::wine) {
        std::string overrides = "winemenubuilder.exe=d";
        if (o.wined3d) overrides += ";d3d8,d3d9,d3d10core,d3d11,dxgi=b";
        else if (o.install_dxvk) {
            if (context.dxvk) overrides += ";d3d8,d3d9,d3d10core,d3d11,dxgi=n,b";
            else plan.missing.push_back("dxvk");
        }
        if (o.install_vkd3d) {
            if (context.vkd3d) overrides += ";d3d12,d3d12core=n,b";
            else plan.missing.push_back("vkd3d-proton");
        }
        if (o.install_dxvk_nvapi) {
            if (context.dxvk_nvapi) { overrides += ";nvapi,nvapi64,nvofapi64=n"; add(env, "DXVK_ENABLE_NVAPI", "1"); }
            else plan.missing.push_back("dxvk-nvapi");
        }
        if (!o.dll_overrides.empty()) overrides += ";" + o.dll_overrides;
        add(env, "WINEDLLOVERRIDES", overrides);
    } else if (!o.dll_overrides.empty()) add(env, "WINEDLLOVERRIDES", o.dll_overrides);
    return env;
}

// Plain Wine: the prefix made (wineboot) and DXVK, VKD3D-Proton and DXVK-NVAPI copied into it once
// per version, before the game; shell lines run by the launch script.
std::string componentSetup(const Options& o, const Context& context, const compat_tools::Tool& tool) {
    struct Copy { const compat_tools::Tool* component; const char* x64; const char* x86; };
    std::vector<Copy> copies;
    if (o.install_dxvk && !o.wined3d && context.dxvk) copies.push_back({&*context.dxvk, "x64", "x32"});
    if (o.install_vkd3d && context.vkd3d) copies.push_back({&*context.vkd3d, "x64", "x86"});
    if (o.install_dxvk_nvapi && context.dxvk_nvapi) copies.push_back({&*context.dxvk_nvapi, "x64", "x32"});
    if (copies.empty()) return {};
    std::string script;
    const auto wineserver = tool.wineserver.empty() ? std::string("wineserver") : tool.wineserver.string();
    script += "if [ ! -f \"$WINEPREFIX/system.reg\" ]; then " + shellQuote(tool.wine.string()) + " wineboot -u; " + shellQuote(wineserver) + " -w; fi\n";
    script += "d=\"$WINEPREFIX/drive_c/windows\"; m=\"$WINEPREFIX/.launchme-components\"; mkdir -p \"$d/system32\" \"$d/syswow64\"\n";
    for (const auto& copy : copies) {
        const auto folder = shellQuote(copy.component->path.string());
        const auto name = shellQuote(copy.component->path.filename().string());
        script += "grep -qxF " + name + " \"$m\" 2>/dev/null || { cp -f " + folder + "/" + copy.x64 + "/*.dll \"$d/system32/\" && { [ ! -d " + folder + "/" +
                  copy.x86 + " ] || cp -f " + folder + "/" + copy.x86 + "/*.dll \"$d/syswow64/\"; } && echo " + name + " >> \"$m\"; }\n";
    }
    return script;
}

// The Windows-side command: the program and its arguments as the runner passes them on.
std::vector<std::string> windowsCommand(const Request& request, const fs::path& prefix, Runner runner, const std::vector<std::string>& arguments) {
    std::vector<std::string> command;
    const auto& program = request.program;
    const auto kind = extension(program);
    if (!program.is_absolute()) {
        if (program == "createprefix" && runner != Runner::umu) command = {"wineboot", "-u"};
        else command = {program.string()};
    } else if (kind == ".msi") command = {"msiexec", "/i", windowsPath(program, prefix)};
    else if (kind == ".bat" || kind == ".cmd") command = {"cmd", "/c", windowsPath(program, prefix)};
    else if (kind == ".lnk") command = {"start", "/wait", "/unix", program.string()};
    else command = {program.string()};
    command.insert(command.end(), arguments.begin(), arguments.end());
    return command;
}
}

Context context(const CompatLaunch& launch) {
    Context result;
    const auto roots = compat_tools::systemRoots();
    result.config = roots.config;
    const auto tools = compat_tools::discover(roots);
    result.tool = compat_tools::resolve(tools, launch.options.compat_tool, &result.tool_fell_back);
    result.umu_run = roots.umu_run;
    result.steam_client = !roots.steam_roots.empty() ? roots.steam_roots.front() : compat_tools::toolsFolder(roots.config) / "steam-client";
    result.gamemoderun = xdg_paths::findProgram({L"gamemoderun"});
    result.mangohud = xdg_paths::findProgram({L"mangohud"});
    result.gamescope = xdg_paths::findProgram({L"gamescope"});
    result.game_performance = xdg_paths::findProgram({L"game-performance"});
    result.dlss_swapper = xdg_paths::findProgram({L"dlss-swapper"});
    result.lsfg_layer = lsfgLayerInstalled();
    result.dxvk = compat_tools::component(tools, "DXVK");
    result.vkd3d = compat_tools::component(tools, "VKD3D-Proton");
    result.dxvk_nvapi = compat_tools::component(tools, "DXVK-NVAPI");
    // Steam's runtime apps first; else the copies Launchme downloaded from Lutris' list (as Heroic).
    result.eac_runtime = compat_tools::steamApp(roots, L"Proton EasyAntiCheat Runtime");
    if (result.eac_runtime.empty()) result.eac_runtime = compat_tools::installedRuntime(roots.config, compat_tools::eac_runtime);
    result.battleye_runtime = compat_tools::steamApp(roots, L"Proton BattlEye Runtime");
    if (result.battleye_runtime.empty()) result.battleye_runtime = compat_tools::installedRuntime(roots.config, compat_tools::battleye_runtime);
    result.game_id = compat_tools::gameId(launch, roots.config);
    result.steam_game_id = text_encoding::utf8Lenient(system_info::environment(L"SteamGameId", 64).value_or(L""));
    const auto desktop = system_info::environment(L"XDG_CURRENT_DESKTOP", 256).value_or(L"");
    const auto nested = system_info::environment(L"GAMESCOPE_WAYLAND_DISPLAY", 256).value_or(L"");
    result.inside_gamescope = desktop == L"gamescope" || !nested.empty();
    return result;
}

Plan plan(const Request& request, const Context& context) {
    Plan result;
    const auto& launch = request.launch;
    const auto& o = launch.options;
    const bool game = request.purpose == Purpose::game;
    if (request.program.empty()) { result.error = "no-program"; return result; }
    if (!context.tool) { result.error = "no-tool"; return result; }
    const auto& tool = *context.tool;
    // The runner.
    std::filesystem::path runner_program;
    if (tool.kind == compat_tools::Kind::proton) {
        if (tool.origin == compat_tools::Origin::umu || (o.prefer_umu && !context.umu_run.empty())) {
            if (context.umu_run.empty()) { result.error = "no-umu"; return result; }
            result.runner = Runner::umu;
            runner_program = context.umu_run;
        } else if (!tool.proton.empty()) {
            result.runner = Runner::proton;
            runner_program = tool.proton;
        } else { result.error = "no-tool"; return result; }
    } else if (tool.kind == compat_tools::Kind::wine) {
        if (tool.wine.empty()) { result.error = "no-wine"; return result; }
        result.runner = Runner::wine;
        runner_program = tool.wine;
    } else { result.error = "no-tool"; return result; }

    const auto folder = launch.prefix;
    // umu: WINEPREFIX is the prefix folder (umu links pfx to it); otherwise the real Wine prefix.
    result.prefix = result.runner == Runner::umu ? folder : winePrefix(folder);
    result.proton_prefix = result.runner == Runner::proton && result.prefix == folder;
    const auto name = logName(launch);
    const auto logs = logFolder(context.config);
    result.log_file = logs / (name + (game ? ".log" : ".tools.log"));
    const auto proton_logs = logs / name;

    // The command: [wrappers] <runner> [verb] <program> <arguments>.
    auto arguments = request.arguments;
    if (game && !o.arguments.empty()) {
        const auto extra = process_spawn::splitArguments(o.arguments);
        arguments.insert(arguments.end(), extra.begin(), extra.end());
    }
    std::vector<std::string> command{runner_program.string()};
    if (result.runner == Runner::proton) command.push_back(request.verb.empty() ? std::string("waitforexitandrun") : request.verb);
    const auto windows = windowsCommand(request, result.prefix, result.runner, arguments);
    command.insert(command.end(), windows.begin(), windows.end());

    std::vector<std::string> wrappers;
    if (game) {
        const auto custom = shellWords(o.wrapper);
        wrappers.insert(wrappers.end(), custom.begin(), custom.end());
        if (o.game_performance) {
            if (!context.game_performance.empty()) wrappers.push_back(context.game_performance.string());
            else result.missing.push_back("game-performance");
        }
        if (o.dlss_swapper) {
            if (!context.dlss_swapper.empty()) wrappers.push_back(context.dlss_swapper.string());
            else result.missing.push_back("dlss-swapper");
        }
        if (o.lsfg && !context.lsfg_layer) result.missing.push_back("lsfg-vk");
        if (o.gamemode) {
            if (!context.gamemoderun.empty()) wrappers.push_back(context.gamemoderun.string());
            else result.missing.push_back("gamemoderun");
        }
        const bool scoped = o.gamescope && !context.inside_gamescope;
        if (scoped && context.gamescope.empty()) result.missing.push_back("gamescope");
        const bool gamescope = scoped && !context.gamescope.empty();
        // Inside gamescope MangoHud draws through gamescope's --mangoapp.
        if (o.mangohud && !gamescope) {
            if (!context.mangohud.empty()) wrappers.push_back(context.mangohud.string());
            else result.missing.push_back("mangohud");
        }
        if (gamescope) {
            const auto scope = gamescopeArguments(o, context.gamescope, o.mangohud);
            wrappers.insert(wrappers.end(), scope.begin(), scope.end());
        }
    }

    // The environment: switches, the action's own variables, the runner's, Launchme's, the user's.
    result.environment = switches(o, result.runner, context, proton_logs, result);
    result.environment.insert(result.environment.end(), request.environment.begin(), request.environment.end());
    auto& env = result.environment;
    // lsfg-vk's own variables (its configuration file otherwise): the game only, not the prefix tools.
    if (game && o.lsfg) {
        add(env, "LSFGVK_ENV", "1");
        add(env, "LSFGVK_MULTIPLIER", std::to_string(std::clamp<uint32_t>(o.lsfg_multiplier, 2, 8)));
        if (const auto flow = std::clamp<uint32_t>(o.lsfg_flow_scale, 25, 100); o.lsfg_flow_scale && flow != 100) {
            char text[8]{};
            std::snprintf(text, sizeof text, "%u.%02u", flow / 100, flow % 100);
            add(env, "LSFGVK_FLOW_SCALE", text);
        }
        if (o.lsfg_performance) add(env, "LSFGVK_PERFORMANCE_MODE", "1");
    }
    const auto install_path = !launch.install_directory.empty() ? launch.install_directory
                              : request.program.is_absolute() ? request.program.parent_path() : fs::path{};
    switch (result.runner) {
    case Runner::umu:
        add(env, "WINEPREFIX", result.prefix.string());
        add(env, "GAMEID", context.game_id.empty() ? std::string("umu-default") : context.game_id);
        if (!launch.store.empty()) add(env, "STORE", launch.store);
        if (!tool.path.empty()) add(env, "PROTONPATH", tool.path.string());
        else if (!tool.umu_protonpath.empty()) add(env, "PROTONPATH", tool.umu_protonpath);
        else result.unset.push_back("PROTONPATH"); // umu's default: the newest UMU-Proton.
        add(env, "PROTON_VERB", request.verb.empty() ? std::string("waitforexitandrun") : request.verb);
        if (!install_path.empty()) add(env, "STEAM_COMPAT_INSTALL_PATH", install_path.string());
        break;
    case Runner::proton:
        add(env, "WINEPREFIX", result.prefix.string());
        add(env, "STEAM_COMPAT_DATA_PATH", folder.string());
        add(env, "STEAM_COMPAT_CLIENT_INSTALL_PATH", context.steam_client.string());
        add(env, "STEAM_COMPAT_APP_ID", launch.steam_app_id ? std::to_string(launch.steam_app_id) : std::string("0"));
        add(env, "SteamAppId", launch.steam_app_id ? std::to_string(launch.steam_app_id) : std::string("0"));
        // PROTON_LOG names its file after SteamGameId; Steam's own (Game Mode) is kept.
        if (context.steam_game_id.empty()) add(env, "SteamGameId", "launchme-" + name);
        // GE-Proton and UMU-Proton bring protonfixes: without umu-run they find a non-Steam game's
        // fixes by UMU_ID (umu-database's ID) and STORE, as umu-run passes them.
        if (!launch.steam_app_id) {
            add(env, "UMU_ID", context.game_id.empty() ? std::string("umu-default") : context.game_id);
            if (!launch.store.empty()) add(env, "STORE", launch.store);
        }
        if (!install_path.empty()) add(env, "STEAM_COMPAT_INSTALL_PATH", install_path.string());
        break;
    case Runner::wine:
        add(env, "WINEPREFIX", result.prefix.string());
        if (!tool.wineserver.empty()) add(env, "WINESERVER", tool.wineserver.string());
        break;
    default: break;
    }
    add(env, "LAUNCHME_GAME_KEY", launch.game_key);
    add(env, "LAUNCHME_GAME_NAME", text_encoding::utf8Lenient(launch.title));
    add(env, "LAUNCHME_PREFIX", result.prefix.string());
    add(env, "LAUNCHME_RUNNER", result.runner == Runner::umu ? "umu" : result.runner == Runner::proton ? "proton" : "wine");
    for (const auto& variable : o.environment) {
        add(env, variable.name, variable.value);
        // A variable the user sets is not removed again.
        result.unset.erase(std::remove(result.unset.begin(), result.unset.end(), variable.name), result.unset.end());
        if (variable.name == "WINEPREFIX" && fs::path(variable.value).is_absolute()) result.prefix = variable.value;
    }

    // The working folder.
    if (game && !o.working_directory.empty()) result.working_directory = o.working_directory;
    else if (!request.working_directory.empty()) result.working_directory = request.working_directory;
    else if (request.program.is_absolute()) result.working_directory = request.program.parent_path();

    // Scripts before and after, and the prefix's components: one sh -c around the command.
    std::string setup = game && result.runner == Runner::wine ? componentSetup(o, context, tool) : std::string{};
    const bool before = game && !o.before_script.empty(), after = game && !o.after_script.empty();
    std::vector<std::string> argv = wrappers;
    argv.insert(argv.end(), command.begin(), command.end());
    if (setup.empty() && !before && !after) {
        result.argv = std::move(argv);
        return result;
    }
    const auto run = [](const fs::path& script) {
        const auto quoted = shellQuote(script.string());
        return "if [ -x " + quoted + " ]; then " + quoted + "; else /bin/sh " + quoted + "; fi\n";
    };
    std::string script = setup;
    if (before) script += run(o.before_script);
    if (after) script += "\"$@\"\nstatus=$?\n" + run(o.after_script) + "exit $status\n";
    else script += "exec \"$@\"\n";
    result.argv = {context.shell.string(), "-c", script, "launchme"};
    result.argv.insert(result.argv.end(), argv.begin(), argv.end());
    return result;
}

launch_options_catalog::Text problemText(std::string_view code) {
    static constexpr std::pair<std::string_view, launch_options_catalog::Text> texts[] = {
        {"no-tool", {L"Brak Protona i Wine: pobierz GE-Proton w Ustawieniach", L"No Proton or Wine: download GE-Proton in Settings"}},
        {"no-umu", {L"Brak umu-launcher", L"umu-launcher is missing"}},
        {"no-wine", {L"Wybrane narzędzie nie ma programu wine", L"The chosen tool has no wine program"}},
        {"no-program", {L"Brak programu do uruchomienia", L"No program to run"}},
        {"no-winetricks", {L"Brak winetricks", L"winetricks is missing"}},
        {"bad-verb", {L"Nieprawidłowa nazwa w winetricks", L"An invalid winetricks name"}},
        {"gamemoderun", {L"GameMode nie jest zainstalowany", L"GameMode is not installed"}},
        {"mangohud", {L"MangoHud nie jest zainstalowany", L"MangoHud is not installed"}},
        {"gamescope", {L"gamescope nie jest zainstalowany", L"gamescope is not installed"}},
        {"dxvk", {L"DXVK nie jest pobrany", L"DXVK is not downloaded"}},
        {"vkd3d-proton", {L"VKD3D-Proton nie jest pobrany", L"VKD3D-Proton is not downloaded"}},
        {"dxvk-nvapi", {L"DXVK-NVAPI nie jest pobrany", L"DXVK-NVAPI is not downloaded"}},
        {"eac-runtime", {L"Brak „Proton EasyAntiCheat Runtime” w Steam", L"\"Proton EasyAntiCheat Runtime\" is not installed in Steam"}},
        {"battleye-runtime", {L"Brak „Proton BattlEye Runtime” w Steam", L"\"Proton BattlEye Runtime\" is not installed in Steam"}},
        {"game-performance", {L"game-performance nie jest zainstalowany (CachyOS)", L"game-performance is not installed (CachyOS)"}},
        {"dlss-swapper", {L"dlss-swapper nie jest zainstalowany (CachyOS)", L"dlss-swapper is not installed (CachyOS)"}},
        {"lsfg-vk", {L"lsfg-vk nie jest zainstalowany", L"lsfg-vk is not installed"}},
    };
    for (const auto& [key, text] : texts) if (key == code) return text;
    return {L"", L""};
}

std::vector<std::string> environmentBlock(const Plan& plan, const std::vector<std::string>& current) {
    // The final value of each variable, in the order of its first assignment.
    std::vector<std::string> order;
    std::map<std::string, std::string> values;
    for (const auto& [name, value] : plan.environment) {
        if (name.empty() || name.find('=') != std::string::npos || name.find('\0') != std::string::npos) continue;
        if (!values.contains(name)) order.push_back(name);
        values[name] = value;
    }
    const std::set<std::string> removed(plan.unset.begin(), plan.unset.end());
    std::vector<std::string> result;
    for (const auto& entry : current) {
        const auto name = entry.substr(0, entry.find('='));
        if (!values.contains(name) && !removed.contains(name)) result.push_back(entry);
    }
    for (const auto& name : order)
        if (!removed.contains(name)) result.push_back(name + "=" + values[name]);
    return result;
}

std::vector<std::string> currentEnvironment() {
    std::vector<std::string> result;
    for (char** entry = environ; entry && *entry; ++entry) result.emplace_back(*entry);
    return result;
}

int start(const Plan& plan, pid_t& pid) {
    pid = 0;
    if (!plan.error.empty() || plan.argv.empty()) return ENOENT;
    std::error_code ec;
    if (!plan.prefix.empty()) {
        // umu makes the prefix (and its pfx link) itself; Wine and Proton get the folder ready.
        if (plan.runner == Runner::umu) fs::create_directories(plan.prefix.parent_path(), ec);
        else fs::create_directories(plan.prefix, ec);
        if (plan.proton_prefix) {
            const auto pfx = plan.prefix / "pfx";
            if (!fs::exists(fs::symlink_status(pfx, ec))) fs::create_directory_symlink(".", pfx, ec);
        }
    }
    // Proton's script opens $PROTON_LOG_DIR/steam-<id>.log without creating the folder: a missing
    // folder would stop the game from starting. DXVK writes into its log path the same way.
    for (const auto& [name, value] : plan.environment)
        if ((name == "PROTON_LOG_DIR" || name == "DXVK_LOG_PATH") && !value.empty()) fs::create_directories(value, ec);
    int log = -1;
    if (!plan.log_file.empty()) {
        fs::create_directories(plan.log_file.parent_path(), ec);
        log = ::open(plan.log_file.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
        if (log >= 0) {
            std::string header = "launchme:";
            for (const auto& argument : plan.argv) header += " " + (argument.find_first_of(" \t\n'\"") == std::string::npos ? argument : shellQuote(argument));
            header += "\n";
            [[maybe_unused]] const auto written = ::write(log, header.data(), header.size());
        }
    }
    const auto block = environmentBlock(plan, currentEnvironment());
    process_spawn::Options options;
    options.search = plan.argv.front().find('/') == std::string::npos;
    options.session = true; // The launch job: game_activity follows the game's processes by their session.
    if (isFolder(plan.working_directory)) options.directory = plan.working_directory;
    options.output = log;
    options.error = log;
    options.environment = &block;
    const int error = process_spawn::spawn(plan.argv, options, pid);
    if (log >= 0) ::close(log);
    return error;
}

std::string windowsPath(const fs::path& file, const fs::path& prefix) {
    const auto normal = file.lexically_normal();
    const auto relative = normal.lexically_relative((prefix / "drive_c").lexically_normal());
    std::string result;
    if (!prefix.empty() && !relative.empty() && *relative.begin() != ".." && relative.is_relative()) result = "C:\\" + relative.string();
    else result = "Z:" + normal.string();
    std::replace(result.begin(), result.end(), '/', '\\');
    return result;
}

fs::path unixPath(std::string_view windows, const fs::path& prefix) {
    std::string text(windows);
    std::replace(text.begin(), text.end(), '\\', '/');
    if (text.starts_with("//?/")) text.erase(0, 4);
    if (text.size() < 2 || text[1] != ':' || !std::isalpha(static_cast<unsigned char>(text[0]))) return {};
    const char drive = static_cast<char>(std::tolower(static_cast<unsigned char>(text[0])));
    auto rest = text.substr(2);
    while (!rest.empty() && rest.front() == '/') rest.erase(0, 1);
    fs::path base;
    if (drive == 'z') base = "/";
    else if (drive == 'c') base = prefix / "drive_c";
    else base = prefix / "dosdevices" / (std::string(1, drive) + ":");
    fs::path result = base;
    for (const auto& part : fs::path(rest)) {
        if (part.empty() || part == ".") continue;
        if (part == "..") return {}; // Never above the drive.
        result /= part;
    }
    return result;
}

std::vector<std::string> shellWords(std::string_view text) {
    std::vector<std::string> result;
    std::string word;
    bool started = false;
    for (size_t i = 0; i < text.size(); ++i) {
        const char ch = text[i];
        if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r') {
            if (started) { result.push_back(std::move(word)); word.clear(); started = false; }
        } else if (ch == '\'') {
            started = true;
            const auto end = text.find('\'', i + 1);
            word += text.substr(i + 1, (end == std::string_view::npos ? text.size() : end) - i - 1);
            i = end == std::string_view::npos ? text.size() : end;
        } else if (ch == '"') {
            started = true;
            for (++i; i < text.size() && text[i] != '"'; ++i) {
                if (text[i] == '\\' && i + 1 < text.size() && std::string_view("\"\\$`").find(text[i + 1]) != std::string_view::npos) ++i;
                word += text[i];
            }
        } else if (ch == '\\' && i + 1 < text.size()) {
            started = true;
            word += text[++i];
        } else {
            started = true;
            word += ch;
        }
    }
    if (started) result.push_back(std::move(word));
    return result;
}

std::string shellQuote(std::string_view text) {
    std::string result = "'";
    for (const char ch : text) {
        if (ch == '\'') result += "'\\''";
        else result += ch;
    }
    return result + "'";
}

std::string logName(const CompatLaunch& launch) {
    std::string result;
    for (const unsigned char ch : launch.game_key) {
        const char lower = static_cast<char>(std::tolower(ch));
        if ((lower >= 'a' && lower <= 'z') || (lower >= '0' && lower <= '9') || lower == '_' || lower == '.') result += lower;
        else if (!result.empty() && result.back() != '-') result += '-';
        if (result.size() >= 96) break;
    }
    while (!result.empty() && (result.back() == '-' || result.back() == '.')) result.pop_back();
    while (!result.empty() && result.front() == '.') result.erase(0, 1);
    return result.empty() ? std::string("default") : result;
}

fs::path logFolder(const fs::path& config) { return config / "logs" / "wine"; }

std::vector<fs::path> logFiles(const CompatLaunch& launch) {
    const auto folder = logFolder(userConfigDirectory());
    const auto name = logName(launch);
    std::vector<fs::path> result;
    if (isFile(folder / (name + ".log"))) result.push_back(folder / (name + ".log"));
    std::error_code ec;
    for (fs::directory_iterator it(folder / name, ec), end; !ec && it != end; it.increment(ec))
        if (it->is_regular_file(ec) && it->path().extension() == ".log") result.push_back(it->path());
    if (isFile(folder / (name + ".tools.log"))) result.push_back(folder / (name + ".tools.log"));
    return result;
}

// MS-SHLLINK: a 76-byte header, then the ID list, LinkInfo and the string data its flags announce.
std::optional<Shortcut> parseShortcut(std::span<const uint8_t> bytes) {
    const auto u16 = [&](size_t at) -> std::optional<uint32_t> {
        if (at + 2 > bytes.size()) return std::nullopt;
        return static_cast<uint32_t>(bytes[at] | (bytes[at + 1] << 8));
    };
    const auto u32 = [&](size_t at) -> std::optional<uint32_t> {
        if (at + 4 > bytes.size()) return std::nullopt;
        return static_cast<uint32_t>(bytes[at] | (bytes[at + 1] << 8) | (bytes[at + 2] << 16) | (static_cast<uint32_t>(bytes[at + 3]) << 24));
    };
    static constexpr uint8_t clsid[16] = {0x01, 0x14, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46};
    if (bytes.size() < 76 || u32(0) != 0x4Cu || std::memcmp(bytes.data() + 4, clsid, 16) != 0) return std::nullopt;
    const uint32_t flags = *u32(20);
    const bool unicode = flags & 0x80;
    size_t at = 76;
    if (flags & 0x01) { // HasLinkTargetIDList
        const auto size = u16(at);
        if (!size) return std::nullopt;
        at += 2 + *size;
    }
    const auto ansi = [&](size_t from, size_t limit) {
        std::string result;
        for (size_t i = from; i < limit && bytes[i]; ++i) result += static_cast<char>(bytes[i]);
        // Code-page text: Latin-1 as a lenient stand-in for the system's ANSI code page.
        std::wstring wide;
        for (const unsigned char ch : result) wide += static_cast<wchar_t>(ch);
        return text_encoding::utf8Lenient(wide);
    };
    const auto utf16 = [&](size_t from, size_t limit, size_t count) {
        std::wstring wide;
        for (size_t i = 0; (count == SIZE_MAX || i < count) && from + 2 * i + 1 < limit; ++i) {
            const auto unit = static_cast<wchar_t>(bytes[from + 2 * i] | (bytes[from + 2 * i + 1] << 8));
            if (count == SIZE_MAX && !unit) break;
            wide += unit;
        }
        // UTF-16 to UTF-32: join surrogate pairs.
        std::wstring joined;
        for (size_t i = 0; i < wide.size(); ++i) {
            const auto unit = static_cast<uint32_t>(wide[i]);
            if (unit >= 0xD800 && unit < 0xDC00 && i + 1 < wide.size() && static_cast<uint32_t>(wide[i + 1]) >= 0xDC00 && static_cast<uint32_t>(wide[i + 1]) < 0xE000) {
                joined += static_cast<wchar_t>(0x10000 + ((unit - 0xD800) << 10) + (static_cast<uint32_t>(wide[i + 1]) - 0xDC00));
                ++i;
            } else joined += wide[i];
        }
        return text_encoding::utf8Lenient(joined);
    };
    Shortcut result;
    if (flags & 0x02) { // HasLinkInfo
        const auto size = u32(at), header = u32(at + 4), info_flags = u32(at + 8);
        if (!size || !header || !info_flags || *size < 28 || at + *size > bytes.size()) return std::nullopt;
        const size_t end = at + *size;
        if (*info_flags & 1) { // VolumeIDAndLocalBasePath
            const auto local = u32(at + 16), suffix = u32(at + 24);
            if (*header >= 0x24) {
                const auto local_unicode = u32(at + 28), suffix_unicode = u32(at + 32);
                if (local_unicode && *local_unicode && at + *local_unicode < end) {
                    result.target = utf16(at + *local_unicode, end, SIZE_MAX);
                    if (suffix_unicode && *suffix_unicode && at + *suffix_unicode < end) result.target += utf16(at + *suffix_unicode, end, SIZE_MAX);
                }
            }
            if (result.target.empty() && local && *local && at + *local < end) {
                result.target = ansi(at + *local, end);
                if (suffix && *suffix && at + *suffix < end) result.target += ansi(at + *suffix, end);
            }
        }
        at = end;
    }
    // StringData: NAME, RELATIVE_PATH, WORKING_DIR, COMMAND_LINE_ARGUMENTS, ICON_LOCATION.
    std::string strings[5];
    for (int index = 0; index < 5; ++index) {
        if (!(flags & (0x04u << index))) continue;
        const auto count = u16(at);
        if (!count) return std::nullopt;
        at += 2;
        const size_t length = unicode ? *count * 2u : *count;
        if (at + length > bytes.size()) return std::nullopt;
        strings[index] = unicode ? utf16(at, at + length, *count) : ansi(at, at + length);
        at += length;
    }
    result.working_directory = strings[2];
    result.arguments = strings[3];
    if (result.target.empty() && !strings[1].empty()) result.target = strings[1]; // Relative to the shortcut.
    if (result.target.empty()) return std::nullopt;
    return result;
}

namespace {
// `path` with each missing component matched without case (Windows paths in a Linux prefix).
fs::path findIgnoringCase(const fs::path& path) {
    std::error_code ec;
    if (path.empty() || fs::exists(path, ec)) return path;
    fs::path result = path.root_path();
    for (const auto& part : path.relative_path()) {
        const auto direct = result / part;
        if (fs::exists(direct, ec)) { result = direct; continue; }
        const auto wanted = lowerAscii(part.string());
        bool found = false;
        for (fs::directory_iterator it(result, ec), end; !ec && it != end; it.increment(ec))
            if (lowerAscii(it->path().filename().string()) == wanted) { result = it->path(); found = true; break; }
        if (!found) return {};
    }
    return result;
}
}

Request resolveShortcut(Request request) {
    if (extension(request.program) != ".lnk") return request;
    std::ifstream input(request.program, std::ios::binary);
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    if (bytes.size() > 1024 * 1024) return request;
    const auto shortcut = parseShortcut(bytes);
    if (!shortcut) return request;
    const auto prefix = winePrefix(request.launch.prefix);
    fs::path target;
    if (shortcut->target.size() > 1 && shortcut->target[1] == ':') target = unixPath(shortcut->target, prefix);
    else { // A path relative to the shortcut's folder.
        auto relative = shortcut->target;
        std::replace(relative.begin(), relative.end(), '\\', '/');
        target = (request.program.parent_path() / relative).lexically_normal();
    }
    if (target.empty()) return request;
    target = findIgnoringCase(target);
    if (target.empty() || !isFile(target)) return request;
    auto arguments = process_spawn::splitArguments(shortcut->arguments);
    arguments.insert(arguments.end(), request.arguments.begin(), request.arguments.end());
    request.arguments = std::move(arguments);
    request.program = target;
    if (!shortcut->working_directory.empty())
        if (const auto folder = findIgnoringCase(unixPath(shortcut->working_directory, prefix)); !folder.empty() && isFolder(folder))
            request.working_directory = folder;
    if (request.working_directory.empty()) request.working_directory = target.parent_path();
    return request;
}

namespace {
int startTool(const CompatLaunch& launch, Request request, pid_t& pid) {
    request.launch = launch;
    request.purpose = Purpose::tool;
    const auto steps = plan(request, context(launch));
    return start(steps, pid);
}
}

int runWineProgram(const CompatLaunch& launch, std::string_view program, pid_t& pid) {
    static constexpr std::string_view allowed[] = {"winecfg", "regedit", "control", "taskmgr", "explorer", "uninstaller", "notepad", "cmd", "wineboot"};
    if (std::find(std::begin(allowed), std::end(allowed), program) == std::end(allowed)) { pid = 0; return EINVAL; }
    Request request;
    request.program = std::string(program);
    return startTool(launch, std::move(request), pid);
}

int runInPrefix(const CompatLaunch& launch, const fs::path& program, const std::vector<std::string>& arguments, pid_t& pid) {
    if (!program.is_absolute() || !isFile(program)) { pid = 0; return ENOENT; }
    Request request;
    request.program = program;
    request.arguments = arguments;
    request.working_directory = program.parent_path();
    request = resolveShortcut(std::move(request));
    return startTool(launch, std::move(request), pid);
}

int createPrefix(const CompatLaunch& launch, pid_t& pid) {
    Request request;
    request.program = "createprefix";
    request.verb = "run";
    return startTool(launch, std::move(request), pid);
}

Plan winetricksPlan(const CompatLaunch& launch, const std::vector<std::string>& verbs, const Context& context, const fs::path& winetricks) {
    for (const auto& verb : verbs)
        if (verb.empty() || verb.front() == '-' || !std::all_of(verb.begin(), verb.end(), [](unsigned char ch) {
                return std::isalnum(ch) || ch == '_' || ch == '-' || ch == '.' || ch == '='; })) {
            Plan refused;
            refused.error = "bad-verb";
            return refused;
        }
    Request request;
    request.launch = launch;
    request.purpose = Purpose::tool;
    request.program = "winetricks";
    request.verb = "waitforexitandrun";
    std::vector<std::string> arguments;
    if (verbs.empty()) arguments.push_back("--gui");
    else { arguments.push_back("-q"); arguments.insert(arguments.end(), verbs.begin(), verbs.end()); }
    request.arguments = arguments;
    auto result = plan(request, context);
    if (!result.error.empty() || result.runner == Runner::umu) return result; // umu-run winetricks <verbs>
    // Proton directly or plain Wine: winetricks itself with that Wine.
    const auto& tool = *context.tool;
    auto program = winetricks;
    if (program.empty() && !tool.path.empty() && isFile(tool.path / "protonfixes" / "winetricks")) program = tool.path / "protonfixes" / "winetricks";
    if (program.empty() || tool.wine.empty()) {
        Plan refused;
        refused.error = program.empty() ? "no-winetricks" : "no-wine";
        return refused;
    }
    result.argv = {program.string()};
    result.argv.insert(result.argv.end(), arguments.begin(), arguments.end());
    auto& env = result.environment;
    add(env, "WINE", tool.wine.string());
    add(env, "WINELOADER", tool.wine.string());
    if (!tool.wineserver.empty()) add(env, "WINESERVER", tool.wineserver.string());
    add(env, "WINEPREFIX", result.prefix.string());
    if (result.runner == Runner::proton) {
        const auto files = tool.wine.parent_path().parent_path(); // <tool>/files
        add(env, "WINEDLLPATH", (files / "lib64" / "wine").string() + ":" + (files / "lib" / "wine").string());
        add(env, "LD_LIBRARY_PATH", (files / "lib64").string() + ":" + (files / "lib").string() + ":" + (files / "lib" / "x86_64-linux-gnu").string() +
                                        ":" + (files / "lib" / "i386-linux-gnu").string());
        result.runner = Runner::wine; // Proton's Wine, run directly.
    }
    add(env, "WINETRICKS_LATEST_VERSION_CHECK", "disabled");
    result.working_directory.clear();
    return result;
}

int winetricks(const CompatLaunch& launch, const std::vector<std::string>& verbs, pid_t& pid) {
    const auto steps = winetricksPlan(launch, verbs, context(launch), xdg_paths::findProgram({L"winetricks"}));
    return start(steps, pid);
}

bool winetricksAvailable(const CompatLaunch& launch) {
    const auto steps = winetricksPlan(launch, {}, context(launch), xdg_paths::findProgram({L"winetricks"}));
    return steps.error.empty();
}

bool protontricksAvailable() {
    return !xdg_paths::findProgram({L"protontricks"}).empty() || !xdg_paths::flatpakWrapper(L"com.github.Matoking.protontricks").empty();
}

std::vector<std::string> protontricksCommand(uint32_t app_id, const std::vector<std::string>& verbs) {
    std::vector<std::string> command;
    if (const auto native = xdg_paths::findProgram({L"protontricks"}); !native.empty()) command.push_back(native.string());
    else if (const auto flatpak = xdg_paths::flatpakWrapper(L"com.github.Matoking.protontricks"); !flatpak.empty()) command.push_back(flatpak.string());
    else return {};
    command.push_back(std::to_string(app_id));
    if (verbs.empty()) command.push_back("--gui");
    else {
        command.push_back("-q");
        command.insert(command.end(), verbs.begin(), verbs.end());
    }
    return command;
}

int protontricks(uint32_t app_id, const std::vector<std::string>& verbs, pid_t& pid) {
    pid = 0;
    for (const auto& verb : verbs) if (verb.empty() || verb.front() == '-') return EINVAL;
    const auto command = protontricksCommand(app_id, verbs);
    if (!app_id || command.empty()) return ENOENT;
    process_spawn::Options options;
    options.session = true;
    return process_spawn::spawn(command, options, pid);
}

bool killPrefix(const CompatLaunch& launch) {
    const auto prefix = winePrefix(launch.prefix);
    bool done = false;
    // The tool's own wineserver ends the prefix's processes cleanly.
    const auto tool_context = context(launch);
    if (tool_context.tool && !tool_context.tool->wineserver.empty() && isFile(tool_context.tool->wineserver)) {
        Plan kill;
        kill.argv = {tool_context.tool->wineserver.string(), "-k"};
        kill.environment = {{"WINEPREFIX", prefix.string()}};
        if (tool_context.tool->kind == compat_tools::Kind::proton) {
            const auto files = tool_context.tool->wine.parent_path().parent_path();
            kill.environment.emplace_back("LD_LIBRARY_PATH", (files / "lib64").string() + ":" + (files / "lib").string());
        }
        pid_t pid = 0;
        if (start(kill, pid) == 0) {
            const auto code = process_spawn::waitExit(pid, std::chrono::seconds(5));
            if (!code) process_spawn::reapLater(pid);
            done = code == 0;
        }
    }
    // Whatever is left (another Wine version's wineserver, a runtime container): every process of this
    // user whose environment names the prefix.
    const auto self = system_info::processId();
    const auto user = static_cast<uint32_t>(::getuid());
    const auto wanted = {"WINEPREFIX=" + prefix.string(), "WINEPREFIX=" + launch.prefix.string(), "STEAM_COMPAT_DATA_PATH=" + launch.prefix.string()};
    for (const auto pid : proc_fs::processes()) {
        if (pid == self || proc_fs::owner(pid) != user) continue;
        std::ifstream input("/proc/" + std::to_string(pid) + "/environ", std::ios::binary);
        std::string entry;
        bool match = false;
        while (!match && std::getline(input, entry, '\0'))
            match = std::find(wanted.begin(), wanted.end(), entry) != wanted.end();
        if (match) { ::kill(static_cast<pid_t>(pid), SIGKILL); done = true; }
    }
    return done;
}

bool prefixExists(const fs::path& prefix) {
    // Wine writes system.reg when its wineserver exits; drive_c/windows is there from the start.
    const auto real = winePrefix(prefix);
    return isFile(real / "system.reg") || isFolder(real / "drive_c" / "windows");
}

Runner prefixRunner(const fs::path& prefix) {
    if (!prefixExists(prefix)) return Runner::none;
    // Proton writes these into the compat data folder (the prefix's parent of pfx/, here the same folder).
    if (isFile(prefix / "version") && (isFile(prefix / "tracked_files") || isFile(prefix / "config_info"))) return Runner::proton;
    return Runner::wine;
}

bool runnerMismatch(const fs::path& prefix, const std::optional<compat_tools::Tool>& tool) {
    if (!tool || (tool->kind != compat_tools::Kind::proton && tool->kind != compat_tools::Kind::wine)) return false;
    const auto made = prefixRunner(prefix);
    if (made == Runner::none) return false;
    return (made == Runner::proton) != (tool->kind == compat_tools::Kind::proton);
}

bool deletePrefix(const fs::path& prefix) {
    std::error_code ec;
    if (prefix.empty() || !prefix.is_absolute()) return false;
    const auto path = prefix.lexically_normal();
    if (fs::is_symlink(path, ec)) return fs::remove(path, ec) && !ec; // A linked prefix: only the link.
    if (!isFolder(path)) return !fs::exists(path, ec);
    const auto real = fs::canonical(path, ec);
    if (ec || real == real.root_path()) return false;
    const auto inside = [](const fs::path& outer, const fs::path& inner) {
        const auto relative = inner.lexically_relative(outer);
        return !relative.empty() && *relative.begin() != "..";
    };
    // Never the home folder, the prefixes root or anything holding them.
    for (const auto& kept : {xdg_paths::home(), launch_options_store::prefixRoot(), launch_options_store::defaultPrefixRoot(), userConfigDirectory()}) {
        if (kept.empty()) continue;
        const auto kept_real = fs::weakly_canonical(kept, ec);
        if (real == kept_real || inside(real, kept_real)) return false;
    }
    const bool looks_like_prefix = prefixExists(real) || isFolder(real / "drive_c") || isFolder(real / "pfx") || isFolder(real / "dosdevices");
    const auto root = fs::weakly_canonical(launch_options_store::prefixRoot(), ec);
    if (!looks_like_prefix && !inside(root, real)) return false;
    const auto doomed = real.parent_path() / ("." + real.filename().string() + ".deleting-" + std::to_string(system_info::processId()));
    fs::rename(real, doomed, ec);
    if (ec) return false;
    fs::remove_all(doomed, ec);
    return !fs::exists(real, ec);
}
}
