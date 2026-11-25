# Xbox

Xbox is one of the PC platforms in **Settings → Library**. Games the **Xbox app** installed are found without
anything else (Gaming Services' own records) and start through Windows like from the Start menu.

The **Xbox account** (your bought games, Xbox Play Anywhere titles and the PC Game Pass catalog while you are
subscribed) works through **launchme-xbox**, Launchme's own open-source helper built on
[Xodus](https://github.com/xodus-gaming/xodus) (GPL-3.0). It is **not bundled**: Launchme downloads it only when
asked. Its source is in [`helpers/xbox`](../helpers/xbox).

> Xodus is an unofficial reverse-engineering project, not affiliated with or endorsed by Microsoft or Xbox.
> Use the account features at your own risk.

## Setup

1. Open **Settings → Library → Xbox (⚙)**. **Xbox helper → Download** fetches `launchme-xbox-0.2.0.exe` of the
   pinned release, checks its size and SHA-256 and stores it as `%LOCALAPPDATA%\Launchme\tools\launchme-xbox-0.2.0.exe`.
   **Remove** deletes it again.
2. **Account → Connect** opens Microsoft's sign-in in a small window of the helper (WebView2). After signing
   in, the library is downloaded. Closing the window cancels.

The helper runs hidden with its own profile in `%LOCALAPPDATA%\Launchme\xbox`, readable only by the current user:
the tokens (encrypted with Windows DPAPI), the sign-in window's WebView2 data and `library.json`. It never uses
the Xbox app's sign-in and never touches its installs. On first use it registers this PC as a Windows device with
Microsoft (as Windows itself does). **Account connection → Sign out** removes the session; the games stay.

## Games

- Account games appear under Xbox (shown when not installed with *Show uninstalled games*), with Microsoft
  Store art where SteamGridDB has none. A game the Xbox app installed keeps its entry and gains the account's art.
  **Library → Refresh** (or F5 on the page) syncs again.
- **Install** (default) opens the game's page in the **Xbox app** (`msxbox://`), or in the Microsoft Store
  when the Xbox app is missing. Once the Xbox app has installed it, the game shows up as installed by itself.
- **Install through Launchme (experimental)**, a switch on the Xbox page: GDK games (MSIXVC packages) are
  downloaded by the helper straight into a folder you choose (default `%USERPROFILE%\Games\Xbox-<title>`),
  with progress in **Downloads** and **Cancel download**. **Play** runs the game through the helper: it asks
  Microsoft for a license (so an ended Game Pass stops it, like in the Xbox app), decrypts the package's
  encrypted files for the session and seals them again when the game exits. **Uninstall** asks first, then
  deletes the folder.
- **Offline play on this PC** (Xbox page): *Test* asks Microsoft whether this PC is the account's offline PC;
  *Turn on* / *Turn off* changes it after a second press (Microsoft allows a few changes a year, the page shows
  how many are left). Each online start then stores the license lease, signed for this device, for up to 30
  days; without internet the game starts from it. The details show *Offline until* its date. Turning it off,
  a refused license or signing out deletes the stored leases.
- **Game Pass**: a *Game Pass* badge, *Game Pass* / *Purchased* filters and a *New in Game Pass* home row.
- **Xbox Live**: achievements, playtime and last played, also for games the Xbox app installed.
- **Updates**: *Update available* when the store has a newer package of a game installed through Launchme;
  *Update* downloads only the changed files into the same folder.
- **Bundles and add-ons**: a bought bundle's games appear one by one; its add-ons are counted on their game
  (*Add-ons: N*), not listed as games. Installing add-ons is not supported yet. Uninstalled demos, betas and
  "Not Supported" entries are hidden.

## Limits

- Install through Launchme needs Microsoft's **Gaming Services** on the PC (the Xbox app installs them); a game may
  still refuse to start outside its registered package. It is a test bed for the Linux version, not a
  replacement for the Xbox app on Windows.
- MSIXVC2 and older UWP packages are not supported by Xodus yet: those games always install through the Xbox app.
- Game updates of games installed through Launchme: Refresh/Install again downloads only changed files.
- The open `xgameruntime.dll` of the Xodus project (for Wine) is not used on Windows.

## Implementation

- `helpers/xbox` (Rust): `status`, `login`, `logout`, `library`, `install`, `launch-prepare`, `run`,
  `launch-cleanup`, `offline-pc`, `activity`, `achievements`, `updates`; one JSON answer on stdout, JSON progress on stderr. `tools/build-xbox-helper.ps1` builds it
  and prints the size and SHA-256 pinned in `src/store/xbox_account.*` (the published asset must be that file).
- `src/store/xbox_account.*`: the pinned download, the answers, the library and install records, the
  account/install worker, uninstall.
- `src/library/pc_platforms.cpp` (`mergeXboxLibrary`), `src/app/xbox.cpp`, `src/app/platform_settings.cpp`.
- `xbox_account_tests` (fixture files and a fake helper) and `launcher_ui_tests --xbox` (settings, sign-in, the
  Xbox app hand-off, the own install dialog, uninstall and launch; PNGs with `LAUNCHME_UI_CAPTURE_DIR`). No real
  account or helper.

## Linux

On the Linux build (highly experimental) the helper is not downloaded: `tools/build-xbox-helper.sh` builds
`launchme-xbox` (Rust; its sign-in window is GTK 4 with WebKitGTK 6.0), the AppImage ships it next to the launcher, and
`LAUNCHME_XBOX_HELPER` can name another build. Games install to `~/Games/Xbox/<title>` and run through Proton or
Wine like other Windows games ([LINUX_WINE.md](LINUX_WINE.md)); before the first start Launchme installs Visual C++
(`vcrun2022`, winetricks) into the game's prefix.
