![launchme](docs/branding/readme-banner.svg)

**[Website](https://uxandai.github.io/launchme/)** · **[Changelog](changelog/)** · **[Download](../../releases/latest)** ·
**[Discord](https://discord.gg/y2wk3kr9wW)** · **[Architecture](docs/ARCHITECTURE.md)**

A full-screen, console-style game launcher for Windows 10/11, built for a gamepad.
It merges Steam (collections and non-Steam shortcuts), GOG, Battle.net, EA app, Ubisoft Connect,
Epic Games, Amazon Games, Xbox (PC) and your own programs into one library, with artwork from SteamGridDB (and Epic's
own store art for an Epic account's games).

> **Linux: highly experimental (WIP).** Since 0.4.0 the same launcher also builds and runs natively on Linux,
> with Proton/Wine for Windows games (umu-launcher, GE-Proton, prefixes, per-game options). It is a work in
> progress: expect bugs and missing pieces, and keep Windows as the supported platform for now.
> See [Linux](#linux-highly-experimental-wip).

Native C++20: Win32, Direct2D/DirectWrite/WIC, WinHTTP, XAudio2 and Media Foundation — no HTML UI.
The interface is available in English, Polish, Brazilian Portuguese and Spanish (Settings → System → Language).
The AI companion and Tarot speak English or Polish.

![PSfive home layout](docs/screenshots/psfive.jpg)

## Screenshots

| | |
| --- | --- |
| ![Desktop window](docs/screenshots/desktop.jpg) **Desktop window** | ![Settings in the desktop window](docs/screenshots/desktop-settings.jpg) **Settings (desktop window)** |
| ![XMB home layout](docs/screenshots/xmb.jpg) **XMB** | ![Metro home layout](docs/screenshots/metro.jpg) **Metro** |
| ![Game details](docs/screenshots/game-details.jpg) **Game details** | ![TV mode layouts](docs/screenshots/layouts.jpg) **TV mode layouts** |
| ![List+ library view](docs/screenshots/list-plus.jpg) **List+ library view** | ![Coverflow library view](docs/screenshots/coverflow.jpg) **Coverflow** |
| ![Search anywhere](docs/screenshots/search.jpg) **Search** | ![Widgets drawer](docs/screenshots/widgets.jpg) **Widgets drawer** |
| ![Glance news hub](docs/screenshots/glance.jpg) **Glance** | ![AI companion](docs/screenshots/companion.jpg) **AI companion** |

## Download

Grab `launchme-<version>-win64-setup.exe` from [Releases](../../releases) and run it: by default it installs for
your account only (no administrator prompt) into `%LOCALAPPDATA%\Programs\Launchme`, adds a Start menu entry and an
uninstaller in Settings → Apps, and keeps updating itself from there. Choose **Install for all users** at the start
(or pass `/ALLUSERS`) to install into `Program Files` instead; that copy updates by downloading the next installer
and asking for administrator approval. Settings stay per user either way. Silent install: `/VERYSILENT /SUPPRESSMSGBOXES`.
Prefer no installer? Unpack `launchme-<version>-win64.zip` anywhere and run `launchme.exe`; both update the same way.
No Visual C++ Redistributable is needed (the WebView2 Evergreen Runtime that Windows 10/11 ship is used for
Glance's pages). The bundled plugins (`plugin-packages/`) install themselves on first start; manage plugins in
Settings → Plugins. Uninstalling keeps your settings unless you choose to remove them too.

Building the installer: `tools\build-installer.ps1` (Inno Setup 6.6 or newer; `-SkipBuild` packs an existing
`build\vs`, `-Sign` signs Setup and its uninstaller).

Linux (highly experimental): `launchme-<version>-x86_64.AppImage` from the same release; make it executable and
run it.

Questions, ideas and news: the [Discord server](https://discord.gg/y2wk3kr9wW).

## Where settings and cache are stored

Everything launchme writes goes to **`%LOCALAPPDATA%\Launchme`** (e.g. `C:\Users\<you>\AppData\Local\Launchme`),
never next to the EXE. On Linux the folder is `$XDG_DATA_HOME/launchme` (usually `~/.local/share/launchme`).

| What | Where |
| --- | --- |
| Settings | `settings.ini` |
| API keys | `*_api_key.txt` (plain text) or `*.dat` (encrypted: Windows DPAPI for your account; on Linux AES-256-GCM with the key in the desktop keyring, else in a `0600` file) |
| Store accounts (GOG, Epic, EA, Battle.net imports) | `accounts\` (encrypted as above) |
| Optional Aurelia binary / isolated session | `helpers\aurelia\` / `aurelia\` |
| Optional Nile binary / Amazon Games session | `tools\nile-1.2.0.exe` (Linux: `tools/nile-1.2.0`) / `amazon\` |
| Optional Xbox helper / Xbox account session | `tools\launchme-xbox-0.1.0.exe` (Linux: next to the launcher or `$LAUNCHME_XBOX_HELPER`) / `xbox\` |
| Optional Ludusavi (save backups) | `tools\ludusavi\` |
| Installed plugins | `plugins\` |
| Artwork cache | `covers`, `heroes`, `logos`, `posters`, `landscapes`, `stores`, `artwork` |
| Metadata and ratings cache | `metadata`, `ratings`, `knowledge`, `glance` |
| Downloaded game videos | `media\` (folder can be changed in Settings → Media & data) |
| Glance browser profile | `browser\` |
| Play journal and achievement history (Profile) | `play-journal.tsv`, `achievement-history.json` |
| Logs and crash dumps | `launchme.log`, `crashes\` |
| Linux: Proton/Wine tools, options and logs | `compatibilitytools/`, `linux/`, `logs/wine/` |

Cache folders can be deleted — they are rebuilt from disk or the network — except your manual choices
(`artwork\selections`, `*.manual-focus` crops, `metadata\*.rawg.json`). Set `LAUNCHME_CONFIG_DIR`
to an absolute path to use a different data folder (e.g. a portable or test profile).
Settings → System → Help → **Open folder** (the version row) opens it.

## Features

- **Home layouts**: shelves (default), XMB, Flix, Metro, PSfive and snitch, with a PS5-like tile
  entrance, frosted glass, button colour themes and several focus-outline styles. In Flix, resting
  on a game for a few seconds unfolds its poster into a wide card with its background, logo and facts,
  pushing the neighbouring tiles aside. In PSfive, Down from Play scrolls to the game's description, facts,
  Steam screenshots and similar games from your library. All games, Glance, Tarot, the companion and an
  *Add game* tile can lead or close the first row of every layout; Metro has a light theme, and the game menu
  can be a large dialog with an icon for each option.
- **Wallpaper** (Settings → Interface → *Wallpaper*): a picture from Wallhaven or Danbooru (safe content only) or
  an animated background from MotionBGs or Steam's animated profile backgrounds, with a moving preview before it
  is downloaded, or your own picture or video; it shows on snitch, behind Settings and the menu screens, in
  *Change artwork* and behind home tiles without a picture (XMB categories, All games). Without one, the
  launcher's own backdrop of lights orbiting along lines fills those places.
- **Made for a pad**: XInput, DualSense, DualShock 4, Nintendo (Switch Pro, Joy-Con), Steam Controller and generic
  HID pads, an on-screen keyboard, and a bottom
  bar with Search, All games, plugins, Downloads, Controllers, Sound, Widgets, Settings and Power. Keyboard and
  mouse work everywhere too. Paired Bluetooth pads stay listed, greyed, while Bluetooth is off.
- **Launch screen**: starting a game shows its background, slowly zooming in, with its logo until the game's
  window shows, then fades away behind it.
- **Desktop window mode** with a sidebar, search and a details pane; switch between it and full screen from
  the power menu. The sidebar folds to its icons (the button by the logo) and the details pane is as wide as
  you drag its edge (double-click it for the default).
- **Library views**: grid (squares, posters or landscape tiles), list, cinematic, detailed, poster,
  coverflow, a shelf of game cases (their spines carry the name and the platform) and novelty
  cartridge/floppy views; sorting, filters, search, *Recently added* by the store's acquisition date and
  remembered positions. Consoles, collections and publishers each open in a default view of their own until one
  gets its own (Settings → Interface → *Library views in TV mode*, with a reset for each). With game videos on, the video plays in List+'s picture, in Cinematic's focused tile
  and in the desktop window's details pane; PC games from other stores play the Steam trailer of the same game.
  Games you hide, and Steam's hidden and private games, stay out of sight until you show them.
- **Collections**: Steam's collections (synced with the running client) and your own ones (create them from a
  game's menu or the desktop window's sidebar, drag games onto them); each can be hidden from the home screen or
  reordered, and your own ones deleted (Settings → Library → *Collections*).
- **Playing**: Play on a game installed by another remembered Steam account switches Steam to that account
  first, and a Steam game can be tied to one account (game menu → *Steam account*); Install picks the Steam
  library (with its free space) in Launchme's own dialog. While a game runs, *Close game* sits next to Play.
  Account switching and the install dialog can be turned off in Settings → Library → Launching.
- **Downloads**: Steam, GOG, Epic, Battle.net, Amazon Games, Xbox (through Launchme) and Aurelia installs show their phase and percent on
  the game's Play button and in one *Downloads* list; a game preloaded ahead of its release waits there as
  *Preloaded* until the release day.
- **Stores**: local installs of every store above, Steam collections synced with the running client,
  GOG and Epic account libraries (Epic installs through Legendary, no Epic launcher needed), Amazon Games
  through the optional, separately downloaded Nile helper ([guide](docs/AMAZON_GAMES.md)), the Xbox account (bought,
  Play Anywhere and PC Game Pass games) through Launchme's optional Xodus-based helper ([guide](docs/XBOX.md)),
  EA and Battle.net libraries imported from their store pages (no EA app needed: sign in on ea.com,
  paste the token page), Ubisoft from the signed-in client, DRM-free folders, and MultiMC / Prism Launcher
  instances as games. Every platform, Steam included, can be switched off.
- **Emulated games from the ROM folder** (Settings → Library → *Emulation*): an EmuDeck / ES-DE ROM folder
  read with launchme's own parsers (the systems, file types and emulators of EmuDeck's Steam ROM Manager parsers,
  built in: no Steam ROM Manager or Steam shortcuts needed). The folder is found by itself or chosen; playlists and
  cue sheets hide their discs, Steam ROM Manager's exclusions and ES-DE's hidden games apply when they exist. The
  Emulation row has tiles for about 30 consoles, from NES and Game Boy to PS Vita, Dreamcast, Xbox 360, Arcade and
  DOS. *Steam shortcuts (Steam ROM Manager)* keeps the shortcut-based library instead.
- **Emulators visible and set by hand** (Settings → Library → *Emulation* → a system): each system shows the
  emulator that starts it and how it was found: EmuDeck's launcher script (with the program it runs), EmuDeck's
  Emulators folder, ES-DE's find rules, the Windows registry, the usual install folders, `PATH`, or on Linux a
  flatpak or an AppImage, so EmuDeck is optional. Pick another emulator (PS1, PSP, DS, GameCube and Wii can also
  run on RetroArch), point at its program by hand and replace its launch arguments (`%ROM%`, `%CORE%`); the
  command a game starts with is shown below.
- **Games added by hand** (Settings → Library → *DRM-free folders* → **Add game** → **Choose file** or **Choose
  folder**, the "+" tile, or a file or folder dropped on the desktop window): a program, shortcut or script is a PC
  game; a disc image or ROM is an emulated game with its console guessed and changeable (*Platform*), started by
  the system's emulator or by an *Emulator command* of its own (`%ROM%` for the file); a folder is a PS3 or Wii U
  game, one game's ROM folder or a PC game's folder with its program found; a whole ROM folder becomes the emulated
  games' ROM folder, and a folder of many games joins the DRM-free folders. The list of these games is under
  *DRM-free folders* → *Manually added games*. A game can also have no file at all, for one to play next or played
  elsewhere; such a game reads **Choose file** and opens its editor at the file. The editor shows the poster its name
  finds as you type, with **Identify game** under it; **Remove from library** (the game's menu, in red, after a
  question) removes the entry, never the game's files.
- **Shortcuts** (the desktop window's game menu → *Shortcuts*; TV mode: the game's details → … → *Shortcuts*): a
  desktop shortcut, a Start menu entry (on Linux the applications menu) and a Steam shortcut with the game's artwork
  for any game, starting it through Launchme with its launch options; Settings → Library → *Game shortcuts* adds
  every installed game at once.
- **Save backups through Ludusavi** (Settings → Library → *Save backups*): a game's saves backed up after it ends
  (off by default) or by hand from its menu → *Saves*, and restored from there.
- **Recently played** can also list games not installed, by the date they were added (Settings → Library →
  Visibility → *Not installed in Recently played*).
- **Extended Aurelia integration** (Windows and Linux, optional, off by default): install/update the latest helper
  from Settings → Library → Steam → *Manage Aurelia*, and sign in there with a QR code from the Steam Mobile app or
  the account name and password. It manages only the games you choose (or all, by default)
  and adds managed Steam downloads, cloud saves synced before and after playing, cloud-save status and conflict
  resolution, Steam Family Sharing games, DLC, manifest pinning/downgrades, beta branches, Workshop and native
  live friends chat in both desktop and TV modes. Primarily for single-player/offline games; some games, especially multiplayer,
  DRM and Steamworks titles, may require the Steam client. See the [Aurelia guide](docs/AURELIA.md).
- **Artwork**: SteamGridDB covers, heroes (the sharp 3840 px ones first), logos and posters with OCR-assisted cropping,
  a variant browser (*Change artwork*: its *Source* list adds the Steam store, GOG, Xbox / Microsoft Store,
  Nintendo eShop, GameTDB, TitleDB, TheGamesDB and Wikimedia Commons, each switched on or off in Settings → Media &
  data → *Artwork sources*) and *Adjust crop* for covers and backgrounds; where SteamGridDB has nothing, the store's own
  art (Epic, Ubisoft Connect, GOG, EA, Battle.net, Amazon Games). Epic account games use Epic's box art as their poster.
  Games added by hand (and DRM-free folders) fall back to the same game's Steam store art; Settings → Library →
  *DRM-free folders* → **Covers** fetches all their missing covers at once. Missing art downloads in the background, covers and
  posters first. To fetch it now: Settings → Media & data → *Download missing covers*, or in the desktop
  window select several games and pick *Download artwork again* or *Download missing covers…*.
- **Details**: Steam metadata, playtime (also for non-Steam games, Epic's and Prism's counts, or your own value from
  *Edit metadata*, in both modes), achievements, Steam and RAWG ratings,
  versions of the same game across stores, uninstall from the game menu (Steam's own dialog, GOG games
  silently after Launchme asks, other stores through the uninstaller they registered). Achievements of games outside
  Steam come from the Achievements plugin: GOG and Epic (with their accounts connected),
  RetroAchievements for emulated games (user name and Web API key in the plugin's settings), RPCS3 and
  shadPS4 trophies. An achievement opens its card: a larger icon, the description, when it was earned and
  how rare it is.
- **Glance**: a news hub (your games' Steam announcements, new and upcoming releases, articles) with a
  built-in WebView2 browser window driven by the pad.
- **Tarot** (optional, off by default): a short card reading — 5 to 8 questions drawn as tarot cards (buy something
  new or play what you own, genre, era, mood, company, length…) — that ends with up to three games, from your
  library or to buy on Steam.
- **AI companion** (optional): a chat character that knows your library, with an optional voice and photo feed,
  a local semantic memory (a small multilingual model on the CPU, no service) and a relationship that grows with
  your conversations — see [AI companion](#ai-companion).
- **Publishers and developers**: rows of their logos on the TV home (a board of all of them, then a studio's games),
  folded sections in the desktop window, links in the details pane, and search by studio.
  Games with every achievement unlocked have a gold border.
- **Widgets drawer**: clock, Steam friends, Discord, CPU/GPU temperatures, controllers, audio and Bluetooth.
- **Profile** (the drawer's *Profile* widget): this week's play time, games and launches with a chart, the latest
  achievements, a year of achievement history, the games closest to 100 % and every platform with its hours.
- **In-game Guide overlay** (experimental): back to the dashboard, close the game, optional FPS from PresentMon.
- **Plugins** (ZIP + native DLL): HowLongToBeat (completion times in game details) and Achievements
  (after [SuccessStory](https://github.com/Lacro59/playnite-successstory-plugin), MIT) ship with the Windows build;
  write your own with the [Plugin SDK](docs/plugins/README.md) (Windows DLLs; the host also loads Linux `.so` plugins).
- **Updates**: Launchme looks for a new release on GitHub, says so, and updates itself with one click
  (Settings → System → Updates, or the icon on the desktop window's top bar), then shows what is new.
- **Low memory mode** (Settings → System → Performance, on by default): artwork decoded at its size on
  screen, fewer images kept in memory, and while a game runs in front of the launcher it gives its graphics
  and artwork back (GPU memory to zero).
- **Artwork compression (BC1)** (Settings → System → Performance, on by default): covers and posters take 8×
  less graphics memory, logos 4× (BC3); compressed once and kept on disk.
- **Out of the game's way**: with a game in front the launcher drops to below-normal priority with EcoQoS,
  10 frames a second and nothing drawn; a still screen ticks at 60, then 30 Hz. Flip-model presentation
  (Windows) and game videos decoded on the GPU.
- **Hide to tray while playing** (Settings → Library → Launching, off by default): while a game started here runs,
  the launcher leaves the taskbar and Alt+Tab and comes back when the game closes.
- **Music and sound**: background music with two themes (Horizons and Exploratory Silence) or none, and
  interface sounds that can come from your own sound folder.
- Screensaver, start with Windows, single instance, crash dumps; the computer stays awake while a game runs.

## API keys (all optional, all free)

Nothing is needed to start; without a key only the related feature is off. Paste keys in
**Settings → Media & data → Service keys** (or set the environment variable, which wins over the file).

| Service | How to get the key | Used for |
| --- | --- | --- |
| **SteamGridDB** (recommended) | Sign in at [steamgriddb.com](https://www.steamgriddb.com) → Preferences → API → *Generate API Key* | covers, heroes, logos, posters, the artwork browser |
| **Steam Web API** | Sign in at [steamcommunity.com/dev/apikey](https://steamcommunity.com/dev/apikey), enter any domain (e.g. `localhost`) | Steam friends, names of uninstalled games, full achievement lists |
| **RAWG** | Create an account at [rawg.io/apidocs](https://rawg.io/apidocs) → *Get API key* | ratings and metadata for non-Steam games |
| **ScreenScraper** | Create an account at [screenscraper.fr](https://www.screenscraper.fr); API developer credentials (*devid* / *devpassword*) are requested from ScreenScraper on their forum | emulated games: ROM identification, descriptions, artwork, videos |
| **TheGamesDB** | Request a free public API key on the [TheGamesDB forum](https://forums.thegamesdb.net) | box art, fanart, banners, clear logos in *Change artwork* |

The other artwork sources (Steam store, GOG, Xbox / Microsoft Store, Nintendo eShop, GameTDB, TitleDB, Wikimedia
Commons) and the wallpaper sources (Wallhaven, Danbooru, MotionBGs, Steam) need no key.

Files and variables: `steamgriddb_api_key.txt` / `STEAMGRIDDB_API_KEY`, `steam_web_api_key.txt` /
`STEAM_WEB_API_KEY`, `rawg_api_key.txt` / `RAWG_API_KEY`, `thegamesdb_api_key.txt` / `THEGAMESDB_API_KEY`. ScreenScraper has its own page with the
developer and user login (**Settings → Media & data → ScreenScraper**, variables `SCREENSCRAPER_DEV_ID`,
`SCREENSCRAPER_DEV_PASSWORD`, `SCREENSCRAPER_SSID`, `SCREENSCRAPER_SSPASSWORD`); passwords are stored with DPAPI.

Store accounts (GOG, Epic, EA) connect in **Settings → Library → Platforms → ⚙** and are stored with DPAPI.
Steam collection sync needs the Steam client started with `-cef-enable-debugging`.

## AI companion

The companion is an optional chat character on the home screen (**Settings → Assistant**) that knows
your library and playtime. It needs a language model — the easiest is a **free Gemini API key**:

1. Open [aistudio.google.com/apikey](https://aistudio.google.com/apikey), sign in with a Google account
   and click *Create API key* (the free tier is enough for chatting).
2. In launchme: **Settings → Assistant** — Gemini is the default model provider — paste the key into
   **Gemini API key** (or set `GEMINI_API_KEY`).

Other providers: Claude (`ANTHROPIC_API_KEY`), Grok (`XAI_API_KEY`) or a local [Ollama](https://ollama.com)
server with no key. Optional voice uses an [ElevenLabs](https://elevenlabs.io) key (`ELEVENLABS_API_KEY`).
Keys typed in Settings are encrypted with DPAPI for your Windows account.

The companions live in real cities (Europe and Tokyo) and keep their own clocks, so they sleep at night there.
Far from those time zones, **Settings → Assistant → Where they live** moves them all to one of 27 cities
from Honolulu to Auckland (*Shared city*), or one of them on her own (*City: …*); the city's time zone
sets when she sleeps, and she makes up her neighbourhood there herself.

## Building

Requirements: Windows 10/11 x64, Visual Studio 2026 (18) Community with the C++ workload and Windows SDK
(`tools\build.ps1` looks for the Community edition's bundled CMake), Git and network access on the first
configure: miniz, PresentMon, the WebView2 SDK, QR Code generator, ONNX Runtime, SentencePiece, SQLite and the
companion's MiniLM embedding model (about 118 MB) are fetched. Node.js is optional (one Steam bridge test).

```powershell
.\tools\build.ps1          # Release build, runtime published to build\vs
.\tools\build.ps1 -Test    # also builds and runs every CTest test
.\build\vs\launchme.exe
```

Close a running launcher before rebuilding. The C++ runtime is linked statically. Tests never launch
games, store clients, installers or change system settings; `*_live_test.exe` and `tools/` probes are
manual (network, real accounts). How the code is organised, the threading rules and how to add a setting,
a screen or a test: [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

### Linux (highly experimental, WIP)

**Highly experimental, work in progress.** The full launcher (TV mode and the desktop window) builds and runs
natively on Linux from the same sources: SDL3 for the window, input and sound, Skia on the GPU (Vulkan, else
OpenGL) in place of Direct2D/DirectWrite. It reads Steam (native, Flatpak, Snap), Heroic (Epic and GOG), Lutris,
Prism Launcher and your own games, and runs Windows games through umu-launcher, Proton or Wine with a prefix
manager and per-game options (Settings → *Compatibility*, [docs/LINUX_WINE.md](docs/LINUX_WINE.md)): a game keeps
only its own values on top of the defaults, CachyOS and GE-Proton extras and lsfg-vk are one click, launch options
can be pasted as Steam writes them, Protontricks works for every game, ProtonDB and Steam Deck ratings are shown,
and store launchers (Battle.net, EA app, Epic, Ubisoft, Rockstar, GOG Galaxy) install in a prefix of their own.
It has been tested far less than the Windows build: expect crashes, missing features (no FPS reading; the in-game
Guide opens in the launcher's window; web pages need WebKitGTK) and changes between versions. Bug reports are welcome.

Status, building (Skia is built from source by `tools/linux/build-skia.sh`) and conventions:
[docs/linux-port.md](docs/linux-port.md).

```sh
tools/linux/build-skia.sh                  # once: Skia into ~/skia (or -DLAUNCHME_SKIA_DIR=<checkout>)
cmake -S . -B build/linux -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/linux
ctest --test-dir build/linux               # optional
./build/linux/launchme
```

`tools/linux/make-appimage.sh` packs a build into the release's AppImage.

## License

Copyright © 2026 uxandai.

launchme is free software: you can redistribute it and/or modify it under the terms of the
[GNU General Public License](LICENSE) as published by the Free Software Foundation, either version 3 of
the License, or (at your option) any later version. It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
PURPOSE. The license text ships next to `launchme.exe` as `LICENSE.txt`. Releases up to 0.3.2 were
published under the MIT License.

Third-party licenses (miniz, PresentMon, WebView2 SDK, QR Code generator, SuccessStory for the Achievements
plugin, see [its notice](plugins/achievements/NOTICE.md)) ship in `plugin-packages/licenses/`; those of ONNX
Runtime and SentencePiece, used with the MiniLM model by the companion's memory, in `assets/companion-embeddings/`
(SQLite is public domain). The publisher and developer logos
are from HVR88's [Monochrome Gaming Logos](https://github.com/HVR88/Monochrome-Gaming-Logos) (their note:
`res/studios/LICENSE.txt`, installed as `assets/studios/LICENSE.txt`); the trademarks belong to their owners.
