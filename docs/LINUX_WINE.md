# Windows programs on Linux: umu-launcher, Proton and Wine

On Linux, Launchme starts a Windows program (`.exe`, `.com`, `.msi`, `.bat`/`.cmd`, `.lnk`) through
**umu-launcher** (Proton in the Steam Linux Runtime, with protonfixes), **Proton** directly, or a
plain **Wine** build, with the game's own options: the same model as Heroic, Lutris and Faugus.
Everything below is Linux-only; the Windows build is unchanged.

| Part | File |
| --- | --- |
| Options of one game (the model) | `src/model/launch_options.hpp` (`LaunchOptions`, `CompatLaunch`) |
| Options as data for the settings UI and JSON | `src/model/launch_options_catalog.hpp/.cpp` |
| Per-game store, prefixes | `src/library/launch_options_store.hpp/.cpp` |
| Installed tools, downloads, archives, umu-database | `src/library/compat_tools.hpp/.cpp` |
| The command, its environment, prefix tools | `src/platform/wine_runner.hpp/.cpp` |
| Launching (`executeGameAction`) | `src/platform/launch.cpp` (Linux branch) |
| The settings page (tools, prefixes, store launchers, options) and Steam-style launch options | `src/app/compat_settings.cpp`, `compat_rows.cpp`, `compat_activate.cpp`, `compat_workers.cpp` (shared: `compat_internal.hpp`, state: `compat_state.hpp`) |
| ProtonDB and Steam Deck ratings | `src/net/protondb.hpp/.cpp`, `src/app/compat_ratings.cpp` |
| Save backups | `src/library/ludusavi.hpp/.cpp` |
| Shortcuts outside Launchme (desktop, applications menu, Steam) | `src/platform/game_shortcuts.hpp/.cpp`, `src/library/shortcuts.hpp/.cpp` |
| Build and tests | `cmake/linux/Wine.cmake`, `tests/wine_runner_tests.cpp`, `tests/compat_tools_tests.cpp`, `tests/protondb_tests.cpp`, `tests/ludusavi_tests.cpp`, `tests/wine_smoke_test.cpp` |

## How a launch works

1. The app attaches the game's context to its action: `action.compat = launch_options_store::compatLaunch(game)`.
   It holds the game key, title, umu `STORE` and store ID, Steam app ID, install folder, the effective
   options and the prefix (assigned on first use, see below). Since 0.4.1 a game keeps only its overrides:
   the effective options are the global defaults with the fields and environment variables the game sets
   itself (`launch_options_store::inherit`), so a later change of a default reaches every game that does not
   override it. The tool is the game's own choice, else its prefix's, else the global one.
2. `executeGameAction` sees a Windows extension and calls `wine_runner`: `.lnk` shortcuts are resolved
   inside the prefix first (MS-SHLLINK parser, path matched without case), `context()` reads the
   machine (installed tools, umu-run, gamemoderun/mangohud/gamescope, components, Steam's runtimes,
   the umu-database cache), `plan()` composes argv + environment + folder + log (pure, unit-tested)
   and `start()` spawns it with `posix_spawn` as a **session leader**.
3. `takeLaunchedProcess()` returns that PID; `launchJob(pid)` is its session, so `game_activity`
   follows the runner and everything it starts (umu's pressure-vessel, Proton, the game) until the
   session is empty. umu and Proton run with `waitforexitandrun`, so the runner itself lives as long
   as the game and the prefix's wineserver.
4. Without `action.compat` (a Windows target from a place that did not attach one) the program runs
   with the global default options in the shared prefix `<prefix root>/default`. With no tool at all
   the launch fails with `launch_error::no_handler`, as before.

The action's own variables (`GameAction::environment`, e.g. Amazon's SDK and entitlement) now reach
every program on Linux: native programs, `.desktop` entries, links handed to `xdg-open`/`steam`, and
Windows programs.

### The command

```
[before script]  <wrapper words> gamemoderun mangohud gamescope <options> --  <runner> <program> <arguments>  [after script]
```

| Runner | When | Command | Its variables |
| --- | --- | --- | --- |
| umu | a Proton tool and `prefer_umu` with umu-run installed, or umu's automatic GE-Proton/UMU-Proton | `umu-run <program> …` | `WINEPREFIX=<prefix>`, `GAMEID`, `STORE`, `PROTONPATH=<tool folder>` (`GE-Proton` for umu's automatic GE; unset for UMU-Proton), `PROTON_VERB=waitforexitandrun`, `STEAM_COMPAT_INSTALL_PATH=<install folder>` |
| Proton | a Proton tool without umu | `<tool>/proton waitforexitandrun <program> …` | `STEAM_COMPAT_DATA_PATH=<prefix>`, `WINEPREFIX`, `STEAM_COMPAT_CLIENT_INSTALL_PATH=<Steam root>` (else an empty `<config>/compatibilitytools/steam-client`), `STEAM_COMPAT_APP_ID`/`SteamAppId` (the Steam app ID or 0), `SteamGameId=launchme-<game>` (kept when Launchme itself runs under Steam), `STEAM_COMPAT_INSTALL_PATH` |
| Wine | a Wine build or system Wine | `<tool>/bin/wine <program> …` | `WINEPREFIX`, `WINESERVER`, `WINEDEBUG=-all` (unless logging), `WINEDLLOVERRIDES=winemenubuilder.exe=d;…` |

Programs: `.exe`/`.com` as they are (a Unix path), `.msi` → `msiexec /i <C:\… or Z:\…>`,
`.bat`/`.cmd` → `cmd /c <Windows path>`, an unresolvable `.lnk` → `start /wait /unix <file>`, a bare
name (`notepad.exe`, `winecfg`) → the prefix's own program.

Wrapper order (outermost first): the custom wrapper, `gamemoderun`, `mangohud` (inside gamescope
MangoHud draws through `gamescope --mangoapp` instead), `gamescope … --`, then the runner. Inside
Steam's Game Mode (`XDG_CURRENT_DESKTOP=gamescope` or `GAMESCOPE_WAYLAND_DISPLAY`) gamescope is not
nested. Enabled programs that are not installed are skipped and listed in `Plan::missing`.

Environment order (a later value wins): the option switches → the action's own variables → the
runner's variables above → `LAUNCHME_GAME_KEY`, `LAUNCHME_GAME_NAME`, `LAUNCHME_PREFIX`,
`LAUNCHME_RUNNER` (for scripts and wrappers) → the game's own environment list (so a `GAMEID=…`
from a wiki wins). Everything else is inherited from Launchme.

Scripts and setup: with a before/after script, or plain Wine with DXVK/VKD3D-Proton/DXVK-NVAPI to
copy, the command runs inside one `/bin/sh -c '<script>' launchme <command…>`: the setup (a new
prefix is made with `wine wineboot -u` + `wineserver -w`; each component's DLLs are copied into
`system32`/`syswow64` once per version, recorded in `<prefix>/.launchme-components`), the before script
(waited for; run directly when executable, else with `/bin/sh`), then `exec "$@"`, or `"$@"`, the
after script and the game's exit status.

Logs: the runner's stdout and stderr (umu, Proton, Wine, the scripts) go to
`<config>/logs/wine/<game>.log` (truncated at each launch, first line = the command); prefix tools
and installers to `<game>.tools.log`. With logging on, Proton also writes `steam-*.log` into
`<config>/logs/wine/<game>/` (`PROTON_LOG_DIR`). `<game>` is the game key in lowercase ASCII
(`epic:Fortnite` → `epic-fortnite`). `wine_runner::logFiles(launch)` lists them.

## Prefixes

- Default: one per game, `~/Games/launchme/prefixes/<slug>` (the root is settable:
  `launch_options_store::setPrefixRoot`). The slug is the title in lowercase ASCII words
  (`Wiedźmin 3: Dziki Gon` → `wiedzmin-3-dziki-gon`), else the key's; a second game with the same
  title gets `-2`. The assignment is stored (`"prefixes"` in the store), so a rename keeps the prefix.
  `<root>/default` is the shared prefix of programs without a game.
- A game may name any prefix (`LaunchOptions::prefix`), e.g. one store's shared prefix (EA app,
  Ubisoft Connect, Battle.net) or a Heroic/Steam Proton prefix (a real `pfx/` folder inside is used).
- One layout serves every runner: `<prefix>/pfx` is a link to `.` (umu-launcher's convention, made
  by umu itself and by `start()` before Proton runs), so `WINEPREFIX = STEAM_COMPAT_DATA_PATH = <prefix>`
  and a prefix can move between umu, Proton and Wine.
- Creation: umu and Proton make the prefix on first launch; plain Wine too (Wine initialises an empty
  folder), or explicitly with `wineboot -u` when components must be copied. `createPrefix()` makes
  one ahead (umu: `umu-run createprefix`).

## Compatibility tools

Discovered (`compat_tools::discover()`), each with a stable ID stored in the options:

| Origin | Where | ID | Removable |
| --- | --- | --- | --- |
| Launchme | `<config>/compatibilitytools/<tool>` (Proton: `proton`; Wine: `bin/wine`) | `launchme:<folder>` | yes |
| Steam (user) | `~/.steam/root/compatibilitytools.d`, every Steam root's `compatibilitytools.d` (native, Flatpak, Snap) | `steam:<folder>` | yes |
| Steam (system) | `/usr/share/steam/compatibilitytools.d`, `/usr/local/share/...` (distribution packages) | `steam-system:<folder>` | no |
| Steam's own | `steamapps/common/Proton*` of every Steam library (Proton 10.0, Experimental, Hotfix) | `steam-common:<folder>` | no |
| Heroic, Lutris | `~/.config/heroic/tools/{proton,wine}`, `~/.local/share/lutris/runners/{proton,wine}` (+ Flatpak) | `heroic:…`, `lutris:…` | no |
| System | `wine` (or `wine64`) on PATH | `system:wine` | no |
| umu | when umu-run is installed: umu downloads and updates them itself | `umu:GE-Proton`, `umu:UMU-Proton` | no |
| Components | `<config>/compatibilitytools/components/{dxvk-*,vkd3d-proton-*,dxvk-nvapi-*}` | `component:<folder>` | yes |
| umu-launcher | `umu-run` on PATH, else `<config>/compatibilitytools/umu-launcher-<version>/umu-run` (zipapp, needs python3) | `launchme:umu-launcher-…` | yes |

Names come from `compatibilitytool.vdf` (`display_name`), versions from the `version` file. Families:
GE-Proton, UMU-Proton, Proton-CachyOS, Proton (Valve's and others), Wine, Wine-Staging,
Wine-Staging-TkG, Wine-GE (deprecated, marked).

Automatic default (`compat_tools::automatic`): the newest GE-Proton → the newest installed UMU-Proton
→ umu's own UMU-Proton (when umu-run is there) → Steam's newest numbered Proton (then Experimental)
→ any other Proton → a Launchme Wine build → Heroic's/Lutris' Wine → system Wine. A chosen tool that
is gone falls back to the automatic one (`Context::tool_fell_back`).

### Downloads

`compat_tools::sources()`: GitHub releases, `GET /repos/<repo>/releases?per_page=15`, cached for an
hour per repository in `<config>/compatibilitytools/cache/<owner>_<repo>.json` (a refresh asks again at
most once a minute; a failed or rate-limited answer returns the cached list as `stale`).

| Source | Repository | Archive picked | Verified with |
| --- | --- | --- | --- |
| `ge-proton` | GloriousEggroll/proton-ge-custom | `<tag>-x86_64.tar.gz` (older: `<tag>.tar.gz`) | `.sha512sum` |
| `umu-proton` | Open-Wine-Components/umu-proton | `<tag>.tar.gz` | `.sha512sum` |
| `proton-cachyos` | CachyOS/proton-cachyos | `-x86_64_v3.tar.xz` on x86-64-v3 CPUs, else `-x86_64.tar.xz` | `.sha512sum` |
| `wine-staging-tkg-kron4ek` | Kron4ek/Wine-Builds | `wine-<v>-staging-tkg-amd64-wow64.tar.xz` | `sha256sums.txt` |
| `wine-kron4ek` | Kron4ek/Wine-Builds | `wine-<v>-amd64-wow64.tar.xz` | `sha256sums.txt` |
| `dxvk` | doitsujin/dxvk | `dxvk-<v>.tar.gz` | — |
| `vkd3d-proton` | HansKristian-Work/vkd3d-proton | `vkd3d-proton-<v>.tar.zst` | — |
| `dxvk-nvapi` | jp7677/dxvk-nvapi | `dxvk-nvapi-v<v>.tar.gz` | — |
| `umu-launcher` | Open-Wine-Components/umu-launcher | `umu-launcher-<v>-zipapp.tar` | — |

Wine-GE is deprecated (replaced by GE-Proton through umu) and not offered. Drafts and prereleases
are skipped; download URLs must be `https://github.com/` (redirects only to `*.githubusercontent.com`).

`compat_tools::install(release, Target::launchme | Target::steam, cancel, progress)`: downloads to
`<config>/compatibilitytools/downloads/<asset>.part` (resumed after a cancel or failure; one download
of an asset at a time), checks the size and the published SHA-512/SHA-256 (a mismatch deletes the
file), extracts with libarchive into a hidden staging folder beside the tools, checks the layout
(`proton`, `bin/wine`, `x64/`, `umu-run`) and renames the archive's single top folder into place (an
existing tool is never replaced). Extraction refuses absolute paths and `..`, links that point out of
the tool (checked again on disk after extraction), hard links out, devices, FIFOs and sockets, never
writes through a link, and drops set-ID bits. `Target::steam` installs a Proton into the user's
`compatibilitytools.d` so Steam games can use it too. Each install leaves
`<tool>/.launchme-release` (`<source>:<tag>`), which `installed(release, tools)` reads.
`remove(tool)` renames a removable tool away and deletes it.

### umu-database

`refreshUmuDatabase(config)` downloads `umu-database.csv` (at most daily) to
`<config>/compatibilitytools/cache/`. `gameId(launch, config)` reads only that file:
the options' `umu_id`, else `umu-<app ID>` for Steam games, else the database's `UMU_ID` for the
store (`egs`, `gog`, `amazon` with or without `amzn1.adg.product.`, `ubisoft`, `ea`) and store ID, else
`umu-default`. `STORE` is sent for every store game, so protonfixes can also look it up itself.

## Options

The main options, by their catalog ID (the JSON key); the complete list, with the extras (CachyOS and GE-Proton
switches, lsfg-vk, NVIDIA and wrapper options), is `launch_options_catalog::options()` in
`src/model/launch_options_catalog.cpp`. "Proton" means umu-run and Proton run directly;
"Wine" a plain Wine build. Toggles set nothing while off.

| ID | Section | Sets |
| --- | --- | --- |
| `compat_tool` | runner | the tool: `PROTONPATH` (umu), `<tool>/proton`, `<tool>/bin/wine`; "default": the global choice |
| `prefix` | runner | `WINEPREFIX`, `STEAM_COMPAT_DATA_PATH`; empty: the game's own prefix |
| `prefer_umu` | runner | Proton tools through `umu-run` when it is installed (default on) |
| `umu_id` | runner | `GAMEID` (empty: umu-database, else `umu-default`) |
| `sync` | proton | automatic; ntsync `PROTON_USE_NTSYNC=1`; fsync `PROTON_NO_NTSYNC=1`; esync also `PROTON_NO_FSYNC=1`; wineserver also `PROTON_NO_ESYNC=1`. Wine: fsync `WINEFSYNC=1 WINEESYNC=1`, esync `WINEESYNC=1` (Wine 10.16+ uses `/dev/ntsync` by itself) |
| `wined3d` | proton | `PROTON_USE_WINED3D=1`; Wine: `WINEDLLOVERRIDES=…;d3d8,d3d9,d3d10core,d3d11,dxgi=b` |
| `wayland` | proton | `PROTON_ENABLE_WAYLAND=1`; Wine: `DISPLAY` unset |
| `hdr` | proton | on `PROTON_ENABLE_HDR=1 DXVK_HDR=1`; force also `PROTON_FORCE_HDR=1` (GE 11, Wayland); off `DXVK_NO_HDR=1`; automatic: GE-Proton detects HDR itself |
| `fsr` | proton | `WINE_FULLSCREEN_FSR=1` |
| `fsr_strength` | proton | `WINE_FULLSCREEN_FSR_STRENGTH=0…5` (with `fsr`) |
| `fsr_resolution` | proton | `WINE_FULLSCREEN_FSR_CUSTOM_MODE=<w>x<h>` (with `fsr`) |
| `fsr4_upgrade` | proton | `PROTON_FSR4_UPGRADE=1` (GE) |
| `xess_upgrade` | proton | `PROTON_XESS_UPGRADE=1` (GE) |
| `large_address_aware` | proton | `PROTON_FORCE_LARGE_ADDRESS_AWARE=1`; Wine: `WINE_LARGE_ADDRESS_AWARE=1` |
| `wow64` | proton | `PROTON_USE_WOW64=1`; Wine: `WINEARCH=wow64` |
| `dxvk_hud` | proton | `DXVK_HUD=<value>` |
| `dxvk_frame_rate` | proton | `DXVK_FRAME_RATE=<fps>` and `VKD3D_FRAME_RATE=<fps>` |
| `install_dxvk` | proton (Wine) | DXVK's DLLs in the prefix, `WINEDLLOVERRIDES=…;d3d8,d3d9,d3d10core,d3d11,dxgi=n,b` |
| `install_vkd3d` | proton (Wine) | VKD3D-Proton's DLLs, `WINEDLLOVERRIDES=…;d3d12,d3d12core=n,b` |
| `eac_runtime` | proton | `PROTON_EAC_RUNTIME=<library>/steamapps/common/Proton EasyAntiCheat Runtime`, else `<config>/compatibilitytools/runtimes/eac_runtime` |
| `battleye_runtime` | proton | `PROTON_BATTLEYE_RUNTIME=<library>/steamapps/common/Proton BattlEye Runtime`, else `<config>/compatibilitytools/runtimes/battleye_runtime` |
| `dll_overrides` | proton | `WINEDLLOVERRIDES=<value>` (Wine: after Launchme's own; Proton appends it to its own) |
| `locale` | proton | `HOST_LC_ALL=<locale>`; Wine: `LC_ALL=<locale>` |
| `log` | proton | `PROTON_LOG=1 PROTON_LOG_DIR=<config>/logs/wine/<game>` (+ `UMU_LOG=1`); Wine: Wine's own `WINEDEBUG`, `DXVK_LOG_PATH`; off (Wine): `WINEDEBUG=-all DXVK_LOG_LEVEL=none` |
| `nvapi` | nvidia | on `PROTON_ENABLE_NVAPI=1 DXVK_ENABLE_NVAPI=1`; off `PROTON_DISABLE_NVAPI=1`; automatic: Proton's choice |
| `install_dxvk_nvapi` | nvidia (Wine) | DXVK-NVAPI's DLLs, `WINEDLLOVERRIDES=…;nvapi,nvapi64,nvofapi64=n`, `DXVK_ENABLE_NVAPI=1` |
| `dlss_upgrade` | nvidia | `PROTON_DLSS_UPGRADE=1` (GE) |
| `dlss_indicator` | nvidia | `PROTON_DLSS_INDICATOR=1`, `DXVK_NVAPI_SET_NGX_DEBUG_OPTIONS=DLSSIndicator=1024,DLSSGIndicator=2` |
| `hide_nvidia_gpu` | nvidia | `PROTON_HIDE_NVIDIA_GPU=1` |
| `prime_offload` | nvidia | `__NV_PRIME_RENDER_OFFLOAD=1 __GLX_VENDOR_LIBRARY_NAME=nvidia __VK_LAYER_NV_optimus=NVIDIA_only` |
| `gl_shader_cache_gb` | nvidia | `__GL_SHADER_DISK_CACHE_SIZE=<GiB × 2^30>` |
| `gl_shader_cache_keep` | nvidia | `__GL_SHADER_DISK_CACHE_SKIP_CLEANUP=1` |
| `gl_threaded` | nvidia | `__GL_THREADED_OPTIMIZATIONS=1` |
| `dri_prime` | mesa | `DRI_PRIME=1` |
| `radv_sam` | mesa | `RADV_PERFTEST=sam` |
| `radv_nggc` | mesa | `RADV_PERFTEST=nggc` (with `sam`: `sam,nggc`) |
| `radv_emulate_rt` | mesa | `RADV_EXPERIMENTAL=emulate_rt` |
| `mesa_glthread` | mesa | `mesa_glthread=true` |
| `present_mode` | mesa | `MESA_VK_WSI_PRESENT_MODE=` `fifo`, `mailbox`, `immediate` or `relaxed` |
| `anisotropy` | mesa | `RADV_TEX_ANISO=<n>` |
| `mesa_shader_cache_size` | mesa | `MESA_SHADER_CACHE_MAX_SIZE=<size>` |
| `gamemode` | wrappers | `gamemoderun` |
| `mangohud` | wrappers | `mangohud`; with gamescope `--mangoapp` |
| `gamescope` | wrappers | `gamescope <options> --` |
| `wrapper` | wrappers | a command before everything (shell words) |
| `gamescope_game_width`/`_height` | gamescope | `-w`/`-h` |
| `gamescope_output_width`/`_height` | gamescope | `-W`/`-H` |
| `gamescope_fps_limit` | gamescope | `-r` |
| `gamescope_fps_limit_unfocused` | gamescope | `-o` |
| `gamescope_window` | gamescope | fullscreen `-f`, borderless `-b`, windowed nothing |
| `gamescope_filter` | gamescope | `-F` `linear`, `nearest`, `fsr`, `nis` or `pixel` |
| `gamescope_scaler` | gamescope | `-S` `integer`, `fit`, `fill` or `stretch` |
| `gamescope_sharpness` | gamescope | `--sharpness <0-20>` with the FSR or NIS filter |
| `gamescope_hdr` | gamescope | `--hdr-enabled` |
| `gamescope_grab_cursor` | gamescope | `--force-grab-cursor` |
| `gamescope_options` | gamescope | more options (shell words) |
| `environment` | game | `NAME=value` lines, set last |
| `arguments` | game | after the action's own arguments (Windows command-line rules) |
| `working_directory` | game | the program's folder otherwise |
| `before_script`, `after_script` | game | run before (waited for) and after the game |

Left out on purpose (2026): `AMD_VULKAN_ICD=AMDVLK` (AMDVLK was discontinued), `RADV_PERFTEST=gpl`/`rt`
(the default in Mesa now), `DXVK_ASYNC` (only the unofficial gplasync builds), `RADV_DEBUG=nocompute`
(gone from Mesa), Wine-GE builds. Anything else can go in the environment list.

The catalog (`launch_options_catalog::options()`) carries for each option: section, type (toggle,
choice, number with limits, text, path, tool, environment), Polish and English label and description
(`text()` picks by `i18n::english()`; not `tr()` keys), what it sets, which runners it affects
(`proton`, `wine`), the toggle it depends on (`fsr`, `gamescope`, `prefer_umu`), and `value()` /
`setValue()` through canonical text. `winetricksVerbs()` lists popular verbs with labels.

The store file `<config>/linux/launch_options.json` (version 2) lists only what differs: the default's fields
that differ from `LaunchOptions{}`, and for each game only its overrides (`environment` for the variables it sets,
`environment_unset` for the default's variables it goes without). Version 1 files, with a full copy per game, are
read as overrides of whatever differs from the default:

```json
{"default":{"compat_tool":"launchme:GE-Proton11-11-x86_64","gamemode":true},
 "games":{"epic:Catnip":{"mangohud":true,"environment":[{"name":"DXVK_HUD","value":"fps"}]}},
 "prefix_root":"/home/u/Games/launchme/prefixes",
 "prefixes":{"epic:Catnip":"/home/u/Games/launchme/prefixes/borderlands-3"},"version":2}
```

## Steam games

A Steam game's menu also has "Proton / Wine…": its own Proton version and launch options, edited where Steam keeps
them (`src/library/steam_game_config.hpp`, `src/app/steam_game.cpp`). The prefix stays Steam's (`compatdata/<appid>`).

- Steam running with its CEF port (127.0.0.1:8080, held by this installation's steamwebhelper): through
  SharedJSContext, `SteamClient.Apps.SetAppLaunchOptions` / `SpecifyCompatTool` (as Decky's plugins call them), read
  back with `RegisterForAppDetails`; Steam's own list of tools comes from `GetAvailableCompatTools`.
- Steam running without it: the first change asks. "Turn on live saving" creates `<steam>/.cef-enable-remote-debugging`
  (as Decky's installer does; Steam reads it when it starts); either answer queues the change in
  `<config>/steam_game_pending.txt`, written once Steam closes (a worker looks every few seconds).
- Steam closed: `userdata/<account>/config/localconfig.vdf` (`apps/<appid>/LaunchOptions`) and `config/config.vdf`
  (`CompatToolMapping/<appid>`, priority 250), each copied to `<file>.launchme-backup` first.
- Switches compose the launch options: variables before `%command%` (`PROTON_ENABLE_NVAPI=1`, `PROTON_ENABLE_WAYLAND=1`,
  `PROTON_ENABLE_HDR=1`, `VKD3D_CONFIG=dxr`, `PROTON_USE_WINED3D=1`, `PROTON_NO_ESYNC=1`, `PROTON_NO_FSYNC=1`,
  `PROTON_LOG=1`), wrappers just before it (`gamemoderun`, `mangohud`); the whole line stays editable.
- Offline tool names: compatibilitytool.vdf's key, Valve's Proton by folder (`Proton 9.0` → `proton_9`).

## Prefix safety

- Uninstalling a managed game asks whether to delete its prefix too (not the shared `default` one), and names the
  other games still using it; they get a new, empty prefix.
- A prefix Proton made (`version` plus `tracked_files` or `config_info`) chosen with Wine, or the reverse, shows a
  warning on the game's version row and the prefix page (`wine_runner::runnerMismatch`).
- GE-Proton → Update automatically (on by default): once a day, when a GE-Proton is installed (not umu's own), the
  newest release is downloaded; games on the automatic version use it, pinned ones keep theirs.
- Steam running on Linux (Steam Deck desktop mode): Steam's desktop layout echoes pad presses as keys; those keys,
  while a pad button is held or within 250 ms of a press, are dropped (`PadKeyEcho`).

## Prefix tools

| Function | What it runs |
| --- | --- |
| `runWineProgram(launch, "winecfg" / "regedit" / "control" / "taskmgr" / "explorer" / "uninstaller", pid)` | the prefix's program through its runner |
| `runInPrefix(launch, exe, args, pid)` | any `.exe`/`.msi`/`.bat`/`.lnk` in the game's prefix (installers, patches, mods) |
| `createPrefix(launch, pid)` | `umu-run createprefix`, `proton run wineboot -u`, `wine wineboot -u` |
| `winetricks(launch, verbs, pid)` | `umu-run winetricks -q <verbs>` (umu), else `winetricks -q <verbs>` with `WINE`/`WINESERVER`/`WINEPREFIX` of the tool (Proton's `files/bin/wine` with its libraries; GE's bundled `protonfixes/winetricks` when none is on PATH); no verbs: `--gui` |
| `protontricks(app_id, verbs, pid)` | `protontricks <app ID> -q <verbs>` or the Flatpak `com.github.Matoking.protontricks` (Steam games' own prefixes) |
| `killPrefix(launch)` | the tool's `wineserver -k`, then SIGKILL to this user's processes whose environment names the prefix |
| `deletePrefix(prefix)` | removes a prefix (refuses `/`, the home, the prefix root, Launchme's folder and folders that are not prefixes) |
| `logFiles(launch)` | the last launch's logs |

## Tests

- `wine_runner_commands_and_options` (`tests/wine_runner_tests.cpp`): umu/Proton/Wine commands with
  every option, environment order and the user's overrides, wrappers and gamescope, scripts, `.msi`,
  `.bat`, `.lnk` (parser and resolution), winetricks, the catalog (every option has its own field,
  limits, JSON round trip of every field), the store (prefix assignment, renames, shared prefixes),
  and `executeGameAction` starting fake `umu-run`, `proton` and `wine` scripts in a temporary home
  (arguments, variables, the umu-database GAMEID, Amazon's variables, the pfx link, the launch job).
- `compat_tools_discovery_releases_and_archives` (`tests/compat_tools_tests.cpp`): discovery from
  fixture folders, the automatic choice, removal rules, release JSON of every source, the cache,
  checksums, umu-database CSV, and extraction of `.tar.gz`/`.tar.xz`/`.tar.zst` including hostile
  archives (`../`, absolute paths, links out, links escaping through other links, hard links out,
  devices): refused, nothing outside, no staging left.
- `wine_smoke_test` (not in CTest) runs real programs:
  `wine_smoke_test [tool ID] [--gui] [--dxvk] [--no-umu] [--log]`, `--releases`, `--install <source>`,
  `--umu-database`. Use `LAUNCHME_CONFIG_DIR` to keep it away from a real data folder.

## Limitations

- `GameAction::compat` must be attached by the caller; without it a Windows program uses the default
  options and the shared default prefix.
- `context()` scans the tool folders and Steam libraries at each launch (milliseconds, on the calling
  thread).
- umu's first run downloads the Steam Linux Runtime and, for its automatic tools, the Proton itself
  (several hundred MB), so the first launch can take minutes; the log shows the progress.
- Paths outside the home folder must be visible in pressure-vessel's container: the install folder is
  passed as `STEAM_COMPAT_INSTALL_PATH`; the prefix and the tool are mounted by umu.
- After a plain Wine game exits, Wine's own services stay in the session for a few seconds until the
  wineserver ends; umu/Proton wait for that themselves.
- `.lnk` targets on other drives are resolved through `dosdevices`; network targets are not.
- DXVK for plain Wine is copied, never removed: switching it off only drops its DLL overrides.
- EAC/BattlEye: Steam's runtime apps first. Without them, the first launch of a game that asks for one downloads it as
  Heroic does: Lutris' list (`lutris.net/api/runtimes`), archives only from `https://github.com/lutris/`, no checksum
  (Lutris publishes none), then the game starts (`compat_tools::installRuntime`).
