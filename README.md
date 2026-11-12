![launchme](docs/branding/readme-banner.svg)

**[Website](https://uxandai.github.io/launchme-website/)** · **[Changelog](changelog/)** · **[Download](../../releases/latest)** ·
**[Discord](https://discord.gg/jhdrzy522)** · **[Architecture](docs/ARCHITECTURE.md)**

A console-style game launcher for Windows 10/11, made for a gamepad: Steam, GOG, Epic, EA, Battle.net, Ubisoft,
Amazon, Xbox, DRM-free folders, emulators and your own programs in one library. Native C++20 (Win32, Direct2D),
no HTML UI. English, Polish, Brazilian Portuguese and Spanish.

> **Linux: highly experimental (WIP)**, built from the same sources, with Proton/Wine for Windows games.
> See [Linux](#linux-highly-experimental-wip).

![PSfive home layout](docs/screenshots/psfive.jpg)

| | |
| --- | --- |
| ![Desktop window](docs/screenshots/desktop.jpg) **Desktop window** | ![Settings in the desktop window](docs/screenshots/desktop-settings.jpg) **Settings** |
| ![XMB home layout](docs/screenshots/xmb.jpg) **XMB** | ![Metro home layout](docs/screenshots/metro.jpg) **Metro** |
| ![Game details](docs/screenshots/game-details.jpg) **Game details** | ![TV mode layouts](docs/screenshots/layouts.jpg) **TV mode layouts** |
| ![List+ library view](docs/screenshots/list-plus.jpg) **List+** | ![Coverflow library view](docs/screenshots/coverflow.jpg) **Coverflow** |
| ![Search anywhere](docs/screenshots/search.jpg) **Search** | ![Widgets drawer](docs/screenshots/widgets.jpg) **Widgets** |
| ![Glance news hub](docs/screenshots/glance.jpg) **Glance** | ![AI companion](docs/screenshots/companion.jpg) **AI companion** |

## Features

- **Two faces**: TV mode (shelves, XMB, Flix, Metro, PSfive, snitch; a CRT filter and a pad cursor on L3 + R3) and a desktop window with a sidebar, a game pane and dark colour themes.
- **Every store**: local installs, GOG and Epic accounts, Amazon ([guide](docs/AMAZON_GAMES.md)), Xbox and Game Pass ([guide](docs/XBOX.md)), EA, Battle.net, Ubisoft, Prism Launcher; a Launchers tab that installs the launchers themselves and a Deals tab with free and discounted games.
- **Emulation**: an EmuDeck / ES-DE ROM folder, about 30 systems with their logos, emulators found or chosen by hand.
- **Your own games**: DRM-free folders and anything added by hand or dropped on the window.
- **Artwork**: SteamGridDB (animated too) and a dozen other sources, a variant browser and crop editor; animated wallpapers, also from Wallpaper Engine.
- **Library**: grid, list, coverflow, game-case shelf and more; collections synced with Steam; Recently played.
- **Playing**: Steam account switching, installs and downloads in one list, per-game logs and HDR switching, Proton per game on Linux, an in-game overlay (experimental).
- **Details**: metadata, playtime, achievements (also GOG, Epic, RetroAchievements and local saves of DRM-free games, with an unlock card over the game), ratings, system requirements.
- **Aurelia** (optional): Steam games without the Steam client ([guide](docs/AURELIA.md)).
- **Extras**: Glance news hub, Tarot game picker, an AI companion, widgets, profile with play stats, save backups (Ludusavi).
- **Plugins**: HowLongToBeat and Achievements built in; write your own with the [Plugin SDK](docs/plugins/README.md).
- **Light on the PC**: drops to low priority and almost no drawing while a game runs; updates itself.

## Download

[Releases](../../releases): `launchme-<version>-win64-setup.exe` installs for your account (no admin prompt; `/ALLUSERS`
for Program Files, `/VERYSILENT` for silent), or unpack `launchme-<version>-win64.zip` anywhere. Linux:
`launchme-<version>-x86_64.AppImage`. Settings, keys and caches live in `%LOCALAPPDATA%\Launchme`
(Linux: `~/.local/share/launchme`; any folder with `LAUNCHME_CONFIG_DIR`).

## API keys (optional, free)

Nothing is needed to start; paste keys in **Settings → Media & data → Service keys** (or set the variable).

| Service | Key | For |
| --- | --- | --- |
| [SteamGridDB](https://www.steamgriddb.com) (recommended) | Preferences → API | artwork |
| [Steam Web API](https://steamcommunity.com/dev/apikey) | any domain, e.g. `localhost` | friends, uninstalled games, achievements |
| [RAWG](https://rawg.io/apidocs) | *Get API key* | ratings and metadata outside Steam |
| [ScreenScraper](https://www.screenscraper.fr) | account + developer credentials | emulated games |
| [TheGamesDB](https://forums.thegamesdb.net) | public key from the forum | more artwork |

The AI companion needs a model: a free [Gemini key](https://aistudio.google.com/apikey) (**Settings → Assistant**),
or Claude, Grok or a local Ollama. All keys are stored encrypted for your Windows user (DPAPI; AES-GCM on Linux).
A key can also come from an environment variable (e.g. `STEAMGRIDDB_API_KEY`), or from a one-line text file
such as `%LOCALAPPDATA%\Launchme\steamgriddb_api_key.txt`, which launchme encrypts in place the first time it reads it.

## Building

Windows 10/11 x64, Visual Studio 2026 with the C++ workload, Git and network on the first configure.

```powershell
.\tools\build.ps1          # Release, runtime in build\vs
.\tools\build.ps1 -Test    # also runs every test
```

Installer: `tools\build-installer.ps1` (Inno Setup 6.6+). Code layout and conventions: [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

### Linux (highly experimental, WIP)

SDL3 and Impeller (Vulkan/OpenGL) in place of Win32 and Direct2D; Steam, Heroic, Lutris and Prism; Windows games
through umu-launcher, Proton or Wine ([docs/LINUX_WINE.md](docs/LINUX_WINE.md)). Expect bugs. Details: [docs/linux-port.md](docs/linux-port.md).

```sh
tools/linux/build-impeller.sh   # once
cmake -S . -B build/linux -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build/linux
./build/linux/launchme
```

## License

Copyright © 2026 uxandai. [GNU GPL v3 or later](LICENSE) (releases up to 0.3.2: MIT). Third-party notices ship in
`plugin-packages/licenses/` and `assets/companion-embeddings/`; studio logos from HVR88's
[Monochrome Gaming Logos](https://github.com/HVR88/Monochrome-Gaming-Logos), case fronts from Twicethedad's
[Game Cases](https://github.com/Twicethedad/Game-Cases) (MIT). Trademarks belong to their owners.
