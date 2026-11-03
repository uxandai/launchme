# Architecture

A map of the code for contributors: where things live, how threads hand work back to the
interface, and the patterns new code follows. User-facing documentation is in the
[README](../README.md); the Linux port's own rules are in [linux-port.md](linux-port.md).

## One process, two faces

Launchme is a single native process (C++20). The same `Launcher` object drives both faces:

- **Tryb TV** (TV mode): full screen, pad first, with the home layouts (shelves, XMB, Flix, Metro,
  PSfive, snitch), the library views and every TV screen and dialog.
- **The desktop window**: a library window with a sidebar, a poster grid and a details pane; dialogs
  open as sheets over it.

Everything runs on one UI thread fed by Win32 messages (on Linux the same messages come from the
emulated Win32 surface in `src/linux`). Work that blocks (disk scans, the network, processes) runs on
workers and comes back as posted messages.

## Directory map

| Path | What it holds |
| --- | --- |
| `src/main.cpp` | Window class, message loop, single instance, command line. |
| `src/app/` | `Launcher` (one class, declared in `launcher.hpp`) split by feature into many `.cpp` files, plus feature state headers (`companion_state.hpp`, `screensaver_state.hpp`, `update_state.hpp`, `tour_state.hpp`, `compat_state.hpp`, `launcher_queues.hpp`, `launcher_backends.hpp`) and private helper headers (`launcher_internal.hpp`, `companion_app_internal.hpp`, `compat_internal.hpp`). |
| `src/ui/` | `Renderer` (declared in `renderer.hpp`, one `*_renderer.cpp` per screen or part) and pure geometry and state: `*_view.hpp`, `*_layout.hpp`, `view_context.hpp`. |
| `src/library/` | Libraries and stores on disk (Steam, GOG, Epic via Heroic/Legendary, Battle.net, EA, Ubisoft, DRM-free folders, MultiMC/Prism, Lutris), collections, view options, the game index, Tarot's model, launch options, Ludusavi. |
| `src/store/` | Store backends that talk to services or helpers: GOG and Epic APIs and installers, EA/Battle.net imports, Amazon (Nile), Xbox, Aurelia. |
| `src/net/` | HTTP (`http.hpp` with a WinHTTP and a curl transport), JSON, Glance, ProtonDB, web search, and the AI companion (`companion*.cpp`: prompts, replies, transport, memory, mind, social model). |
| `src/artwork/` | The cover cache, SteamGridDB, store art, image codecs and compaction. |
| `src/platform/` | Operating-system services: settings (`user_config.*`), processes and launching, audio, devices, power, updates, files, Proton/Wine (`wine_runner.*`); `*_windows.cpp` / `*_linux.cpp` pairs where they differ. |
| `src/model/` | Plain data: `Game`, launch options and their catalog, controllers, notifications. |
| `src/plugins/` | The plugin host for the C ABI in `sdk/`. |
| `src/friends/`, `src/i18n/` | Steam friends; the translation tables. |
| `src/linux/` | The Linux port: the Win32/Direct2D/DirectWrite/WIC surface the shared code calls, implemented on SDL3 and Impeller, and the WebKitGTK browser helper. |
| `plugins/` | Public plugins built with the launcher: HowLongToBeat and Achievements. |
| `sdk/` | `launchme_plugin.h` and an example plugin. |
| `helpers/xbox/` | The Rust helper for the Xbox account (built separately). |
| `tests/` | Unit tests (`*_tests.cpp`) and the UI regression cases (`*_ui_cases.hpp`) of `launcher_ui_tests`. |
| `tools/` | `build.ps1`, probes for real services (manual), `tools/linux/` (Impeller SDK, AppImage, WSL scripts). |
| `cmake/` | Source lists and the Linux, companion and dependency modules. |
| `changelog/`, `docs/`, `_website/` | Release notes, guides, the landing page. |

## Threads and messages

- `Launcher::work_` (`BackgroundWork`) owns the workers. A job captures what it needs **by value**
  plus `alive_` (a shared `atomic_bool`) and the window handle, never `this`.
- A finished job posts its result with `PostMessageW`. Typed results use the messages listed in
  `src/app/worker_messages.def` (ID, payload type, `Launcher::on*` handler; the same table drives
  dispatch in `message_dispatch.hpp` and the cleanup of undelivered payloads at shutdown). Small
  one-off results use `RemoteMessage{run}`, a callback run on the UI thread.
- A result that can be overtaken by a newer request carries a generation number and is dropped when
  it no longer matches (see `src/app/tarot.cpp` and `steam_install.cpp`).
- Injectable transports (`glance_get_`, `tarot_get_`, `game_index_configure_`, the backends in
  `launcher_backends.hpp`) let tests run every path without real services, processes or windows.

## Rendering

`Launcher::paint` fills a `ViewContext` (what the screen shows) and hands it to `Renderer::paint`,
which draws the layers in a fixed order (home, library, dialogs, toasts). On Windows the window's
target is a flip-model swap chain on the renderer's own Direct3D 11 device (`swap_chain.cpp`; an
HWND render target where it cannot be made); game videos are decoded into textures on that device.
The renderer keeps image slots (`std::vector<Cover>` per kind) uploaded from decoded bytes, BC1/BC3
blocks where the GPU samples them (`artwork/block_compression.cpp`, kept on disk by
`ui/texture_cache.cpp`); `renderer_resources.cpp` creates, discards and uploads them and
`renderer.cpp` counts their memory. Small drawing helpers shared by the
renderer files (`color`, `box`, `smoothFade`, `upper`) are in `renderer_internal.hpp`
(`render_detail`); desktop mode's in `desktop_render_internal.hpp`. On Linux the same calls reach Impeller
through `src/linux/gfx`.

On Linux Impeller is the only renderer (`cmake/Impeller.cmake`, kernel in `src/linux/impeller`): it
records GPU display lists behind the same Direct2D surface, text included. The pinned standalone SDK
is extended through a small C ABI for BC1/BC3 uploads and readback. See [IMPELLER.md](IMPELLER.md).

## Shared helpers

Before writing a small helper, use the shared one:

| Header | Provides |
| --- | --- |
| `platform/text_encoding.hpp` | UTF-8 ↔ UTF-16/32 (strict, lenient, legacy code page). |
| `platform/unicode_text.hpp` | Locale-aware lowercase, character classes, case-insensitive and natural comparison, `truncate` that keeps surrogate pairs. |
| `platform/ascii_text.hpp` | ASCII-only lowercase, case-insensitive equality and trimming for protocol and file-format text. |
| `platform/file_io.hpp` | Whole small files (`readText`, `readBytes`, `readTextWithoutBom`) with a size limit; quiet `isFile`/`isFolder`. |
| `platform/atomic_file.hpp`, `protected_file.hpp` | Durable writes; files only this user can read (DPAPI, AES-GCM on Linux). |
| `platform/unix_time.hpp` | `unix_time::now()`, seconds since the epoch as the launcher's files store them. |
| `platform/command_line.hpp`, `net/url_encoding.hpp` | Windows argument quoting; URL components. |
| `app/launcher_internal.hpp` | App-side helpers (`ease`, `gamesLabel`, `trimmedQuery`, `keyDown`, artwork request keys). |

## Patterns

- **A setting**: a field in `AppSettings` (`src/platform/user_config.hpp`), its load and save in
  `user_config.cpp`, a `settings_ui::Id` (`src/ui/settings_view.hpp`), the row in
  `src/app/settings_rows.cpp`, its action in `src/app/settings.cpp`, and both texts in
  `src/i18n/strings_en.cpp`.
- **A modal layer**: `Launcher::Modal` and the order of `topModal()`/`topDialog()`, `modalKey`,
  `modalPad`, the pointer (`pointer.cpp`), `back()`, `onCharacter`, the wheel in `main.cpp`, the draw
  call in `Renderer::paint` in the same order, and a case in `tests/modal_layers_ui_cases.hpp`.
- **A worker result**: a payload in `worker_messages.hpp`, a line in `worker_messages.def`, an
  `on*` handler; or a `RemoteMessage` for a one-off.
- **A large feature**: methods of `Launcher` in their own `src/app/<feature>*.cpp`, its state in a
  `<feature>_state.hpp` struct held by one member, its drawing in `src/ui/<feature>_renderer.cpp`,
  its geometry in a pure `<feature>_view.hpp`. Helpers shared by the feature's files go to a private
  `<feature>_internal.hpp` with `inline` functions in a named namespace (an anonymous namespace in a
  header gives every file its own copy and unused-function warnings).
- **Text**: the source language of UI strings is Polish; `tr(L"…")` returns the user's language from
  the tables in `src/i18n/` (`i18n_tests` checks that every key has English). Code, comments and
  documentation are in English. Files are UTF-8 without BOM with LF line endings.

## Build

- `cmake/Sources.cmake` lists the launcher's sources once. `CMakeLists.txt` builds `launchme_core`
  (everything outside `src/app` and `src/ui`), `launchme` and `launcher_ui_tests` (the same app
  sources compiled with `LAUNCHME_UI_TEST`) and every test executable.
- Linux: `cmake/Linux.cmake` lists the core sources ported so far (a new `src/net`, `src/library` or
  `src/platform` file goes there too), `cmake/LinuxApp.cmake` builds the app on `src/linux`, and
  `cmake/linux/*.cmake` add the Wine, store and browser parts.
- `local/CMakeLists.txt` (not in the repository) adds private plugins when present; `tools/build.ps1`
  installs the `Runtime` and `Local` components into `build\vs`.

## Tests

- Unit tests are one executable each (`launchme_executable(<name> TEST <ctest name> SOURCES …)`).
- UI tests are static methods of `LauncherUiTest` in `tests/*_ui_cases.hpp`, selected by a
  `launcher_ui_tests --flag` and registered with `add_test`. Each checkpoint returns a unique code.
- `LAUNCHME_UI_CAPTURE_DIR` makes UI cases save PNG captures for visual checks; some animated
  captures (the companion's figure, glides) differ slightly between runs.
- Tests never contact real services, launch games or store clients, or change system settings;
  `*_live_test` executables and `tools/` probes are run by hand.
