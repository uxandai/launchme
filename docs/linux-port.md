# Linux port

Launchme runs natively on Linux. It is not a Wine build: the same sources build for both systems, with
Windows code kept behind `#ifdef _WIN32` or in per-platform files. The Windows build is unchanged in
behaviour and is built from the same tree.

**Highly experimental (WIP).** Since 0.4.0 the real launcher (TV mode and the desktop window) runs on Linux, but
it has been tested far less than on Windows: expect bugs and missing pieces.

## Status

| Part | State |
| --- | --- |
| Build (`cmake/Linux.cmake`) | CMake + Ninja, GCC or Clang, C++20 without GNU extensions |
| Shared core (`launchme_core`) | Every shared source builds on Linux or has a Linux counterpart |
| Platform services | Audio (SDL3), decoding and trailers (FFmpeg), system volume (PulseAudio/PipeWire), controllers (SDL3, hidraw, BlueZ), processes (`/proc`, signals), power (logind), sensors (hwmon), memory, Discord, Steam downloads |
| Stores | Steam (native, Flatpak, Snap), Heroic (Epic and GOG, launched through its legendary/gogdl backends with the game's Wine/Proton settings), Lutris, Prism Launcher/PolyMC/MultiMC, Amazon Games (Nile), Xbox (the `launchme-xbox` helper), Aurelia; store launchers (Battle.net, EA app, Epic, Ubisoft, Rockstar, GOG Galaxy) installed into a prefix of their own |
| Launcher window | The full launcher (`cmake/LinuxApp.cmake`): the Win32 surface the shared code calls is implemented on SDL3 (`src/linux/window`, `src/linux/win32`) |
| Renderer | Direct2D, DirectWrite and WIC on Impeller (`src/linux/gfx`, kernel in `src/linux/impeller`): GPU through Vulkan, else OpenGL; fonts through Fontconfig and Impeller's typography; no CPU renderer |
| Windows games | umu-launcher, Proton or Wine with a prefix manager and per-game options (Settings → Library → Proton i Wine, [LINUX_WINE.md](LINUX_WINE.md)) |
| Web view | `launchme-browser` helper on GTK 4 + WebKitGTK 6.0 when present, else the user's browser |
| Plugins | The host loads `linux-x64` packages (`.so`); the Linux build does not package plugins yet |
| Packaging | An AppImage (`tools/linux/make-appimage.sh`), published with each release and used by the updater |
| Tests | About 64 CTest suites (`cmake/Linux.cmake`, `cmake/linux/*.cmake`); `webkit_browser_lifecycle` needs a display with EGL |

The renderer, its SDK build and verification: [IMPELLER.md](IMPELLER.md).

## Building on Linux

Packages (Arch names): `cmake ninja gcc pkgconf icu openssl libsecret curl sdl3 ffmpeg libpulse libpng
libjpeg-turbo libwebp expat sqlite systemd-libs glib2 libarchive fontconfig freetype2 harfbuzz`, and `git python
clang` to build the Impeller SDK. Optional: `gtk4 webkitgtk-6.0 libsoup3 libx11` (the browser helper), `cargo protobuf` (the
Xbox helper, `tools/build-xbox-helper.sh`), at run time `tesseract` (text-aware cover crops) and `steam-devices`
(udev rules for the Steam Controller's hidraw access).

```sh
tools/linux/build-impeller.sh                  # once: the Impeller SDK into ~/launchme-impeller; CMake: LAUNCHME_IMPELLER_DIR
cmake -S . -B build/linux -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/linux
ctest --test-dir build/linux --output-on-failure
./build/linux/launchme
tools/linux/make-appimage.sh build/linux out    # optional: the release's AppImage
```

The data folder is `$XDG_DATA_HOME/launchme` (normally `~/.local/share/launchme`); `LAUNCHME_CONFIG_DIR`
still overrides it.

### From Windows through WSL

`tools/linux/wsl-sync.sh` mirrors the Windows checkout into the WSL file system (`~/lm/src`, fast builds),
`wsl-build.sh` configures and builds there (`-k 0`, then prints the distinct errors), and `wsl-run.sh` starts the
built launcher with a throwaway data folder on a private Xvfb display. The scripts name the checkout's Windows
path in their first lines; adjust it for another machine.

## Conventions for portable code

- **Wide strings.** `wchar_t` is UTF-16 on Windows and UTF-32 on Linux. Convert only through
  `platform/text_encoding.hpp`; never assume surrogate pairs or a 2-byte `wchar_t`.
- **Paths.** Use `path.wstring()`, not `path.native()` (on Linux `native()` is a narrow `std::string`).
  Pass `std::filesystem::path` to `std::ifstream`/`std::ofstream`, never a `std::wstring`.
- **Text comparison, case and normalization.** `platform/unicode_text.hpp`: NLS on Windows, ICU elsewhere
  (`compareNatural`, `equalIgnoreCase`, `lower`, `decompose`, `isLetterOrDigit`, `isMark`, …). Replaces
  `CompareStringEx`, `CompareStringOrdinal`, `LCMapStringEx`, `NormalizeString`, `FoldStringW`,
  `GetStringTypeW`, `IsCharAlphaNumericW` and `_wcsicmp`.
- **Process and environment.** `platform/system.hpp`: `environment()`, `processId()`, `executablePath()`,
  `randomBytes()`, `localTime()`.
- **Files.** `platform/atomic_file.hpp`: atomic writes, `replace()` (instead of `MoveFileExW`) and
  `ExclusiveLock` (instead of an unshared `CreateFileW`).
- **Encrypted files.** `platform/protected_file.hpp`: DPAPI on Windows. On Linux, AES-256-GCM with a random
  key kept in the desktop keyring (Secret Service, via libsecret), or in a `0600` `protected.key` in the data
  folder when no keyring answers. Files are not portable between systems (the same as DPAPI files between
  accounts).
- **Crash reports.** `platform/diagnostics.hpp`: minidumps on Windows; on Linux a signal handler writes
  `crashes/launchme-<time>.txt` with the signal and a backtrace.
- **MSVC CRT extensions** (`localtime_s`, `swprintf_s`, `_wtoi`, …) and a few Win32 helpers that have an
  exact POSIX equivalent (`SecureZeroMemory`, `GetTickCount64`, `Sleep`) come from
  `platform/crt_compat.hpp`, which the Linux build force-includes. Anything with platform behaviour gets a
  real port, not a shim. Note that in `swprintf` on Linux `%s` takes a narrow string; use `%ls` for
  `wchar_t*`. MSVC accepts `%ls` too.
- **Windows headers** in shared files are wrapped in `#ifdef _WIN32`.
- **Tests** may use `GetCurrentProcessId`, `GetEnvironmentVariableW` and similar through
  `tests/linux_compat/Windows.h`, a stand-in that is on the include path of test targets only.

## Known gaps

- GOG offline installers report that they are unavailable (run them with *Add a game from its installer*).
- No FPS reading (PresentMon has no Linux equivalent); no window focus or game-window lookup (Wayland).
- Heroic: Amazon and sideloaded games; Lutris: games without a store; Heroic's AppImage hides its
  backends (games then launch through `heroic://`).
- Images: GIF, TIFF, ICO, RLE BMP and animated WebP are not decoded.
- Flatpak Steam: the account switcher and Steam library sync expect the native layout.
- Updates: only as an AppImage (`launchme-<version>-x86_64.AppImage` release asset with a SHA-256).
- Some messages still name Windows (no new translation keys yet).
- No in-game overlay over other applications (Wayland): the Guide opens in the launcher's window.
- Flatpak and AppStream packaging are still open.
