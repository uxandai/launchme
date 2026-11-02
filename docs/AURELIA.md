# Aurelia integration (Windows and Linux)

An optional, out-of-process integration with [Drackrath/Aurelia](https://github.com/Drackrath/Aurelia).
It is off by default and offered during first-run setup and in **Settings → Library → Steam** (its cog), in the
**Aurelia** section: *Extended Aurelia integration*, *Manage games through Aurelia by default*, *Cloud saves* and
**Manage Aurelia**.

**Primarily for single-player / offline games. Some games, especially multiplayer titles and those
using DRM or Steamworks, may require the Steam client to be running.** Availability of a standalone
launch depends on the game. The integration does not promise offline authentication or universal DRM compatibility.

## Setup

1. Open **Settings → Library → Steam → Manage Aurelia**.
2. Choose **Install Aurelia**. Launchme selects `aurelia_windows_x86_64.exe` from the latest stable
   GitHub release, verifies its advertised SHA-256 and Windows x64 PE header, and publishes it atomically.
3. **Sign in** (the first row says who is signed in, or *Not signed in*). One page, desktop and TV, as Steam's
   own sign-in: Steam's QR code (scan it in the Steam Mobile app, Steam Guard → Scan a QR code, and approve;
   Steam rotates the code and the page follows it) and, below it, the field for the account name and then its
   password (dots; Backspace in the empty field goes back to the name). A Steam Guard code from the e-mail or
   the app is asked for in the same field when Steam wants one. **Sign in using Aurelia's window** remains on
   the page, for Aurelia's own console; **Cancel sign-in** (or Back/Esc) leaves it.

   The QR code and the password run `aurelia login --json` (`--qr --timeout 300`, or `-u <account>`) with its
   standard input kept open: Steam Guard codes go there, and leaving sends `cancel` before the process is
   ended. They run in Launchme's child itself (`AURELIA_NO_DAEMON=1`), as Aurelia's console does: a forwarded
   `login --json` would reach the daemon with only its command line, without the password or this stdin. The
   password goes to the child only, as `AURELIA_PASSWORD` in its environment, never on a command line, and
   Launchme never saves it. The daemon takes the new session on the next command (`login --reconnect`).
   Launchme does not read Aurelia's tokens.
4. Enable **Extended Aurelia integration**, then close and reopen Launchme.

The executable lives in `%LOCALAPPDATA%\Launchme\helpers\aurelia\<release>\`. `current.txt` selects
the installed release. **Check and install update** follows the same validation path. An update uses a
new directory; the current session keeps its original executable until restart. A failed update retains
the previous selection. Binaries are downloaded on demand, not bundled with Launchme.

`AURELIA_CONFIG_DIR` is supplied only to the child process and points to `%LOCALAPPDATA%\Launchme\aurelia`.
This session is separate from standalone Aurelia and from the Steam client's remembered accounts.
The launcher never changes its own process environment to select that session.
The child also receives a dedicated `AURELIA_DAEMON_SOCKET`: a Windows named pipe derived from the
canonical profile path and current user's SID (on Linux a Unix socket in the runtime folder). Before a
command reaches the daemon, Launchme checks who serves the endpoint (the pipe's server process, the socket's
peer): Launchme's own helper with its identity marker naming it is used as it is. Its own helper without a
usable marker (cleaned from the temp folder, or removed by a newer CLI whose restart could not stop it), or
running an older installed version's executable after an update, is ended, so the command starts a fresh
daemon. Anything else at the endpoint is refused, avoiding the upstream fallback that stops all Aurelia
daemons.

## Games and downloads

When enabled, Aurelia supplies metadata, achievements and friends. When a Steam client with a signed-in
account is installed on this PC, its own library scan keeps running as before (installations from every
remembered account, preloads, collections, shortcuts) and Aurelia only adds what it knows.

**Aurelia as a Steam replacement** (Settings → Library → Steam), or no Steam client signed in here: Aurelia is
the library. The account is the one signed in to Aurelia (no `loginusers.vdf` is needed); owned and installed
games and their play time come from `aurelia list`; Steam's collections, Favorites and hidden games from
`aurelia collections pull/list/show` (read only: Launchme never pushes them back; dynamic collections have no
list offline). Recently played comes from Launchme's own history. Every game installs, updates and starts
through Aurelia (exceptions per game while a Steam client is here); the library switches after Launchme
restarts. Other stores continue using their own adapters. Artwork, news, ratings and other independent
services keep their existing providers.

**Steam Family Sharing.** Games borrowed from the account's Steam Family (`is_family_shared` and not owned in
`aurelia list --json`) appear in the library, marked *Family shared*. With a local Steam client, Aurelia's family
list is asked again every 10 minutes and merged into the client's scan: an installed borrowed game is marked,
a missing one is added as not installed. Upstream requires the Steam client for them, so they always launch
through it and are never managed by Aurelia.

**Managed games.** Aurelia installs, updates, uninstalls and launches only the games it manages: those with
**Manage through Aurelia** switched on in **Manage · Aurelia**, or every game when **Manage games through
Aurelia by default** is on (a game can then be excluded the same way). Every other Steam game keeps the Steam
client for all of that. When Aurelia reports that a game from the default set needs the Steam client, it is
launched through the client instead.

**Close game** (✕ next to Play in the details while a game runs, desktop and TV) ends the game's processes like
for any other game; a game Aurelia started is stopped through `aurelia stop --force`, run locally (`AURELIA_NO_DAEMON=1`)
because v0.1.40's daemon does not answer while a game is playing.

Open a game's context menu → **Manage · Aurelia** in desktop or TV mode:

- Install into a selected library, including a manually entered absolute library path.
- Update, verify, uninstall after confirmation, or move the installation to another library.
- Follow transfers in Launchme's existing **Downloads** page, with bytes/progress and, when emitted by
  Aurelia, speed and ETA. Open a transfer to cancel it. Closing its panel leaves the transfer running.
- **Manage through Aurelia** is off by default per game (unless the default above is on). Off uses the
  normal Steam client paths. On launches through Aurelia without `--steam`, for a compatible standalone game. The Aurelia path
  uses `--no-update` so launching cannot silently replace a deliberately selected build; updates are
  explicit actions. The global integration alone never reroutes an installed game's launch.
  Before standalone play, Launchme checks ownership/Family Sharing and `launch-targets`, and explicitly
  selects `--platform windows --steam-mode none`. An unavailable standalone build is an error, not a
  silent fallback to Steam. Borrowed/Family-Shared games must use the normal Steam route.

The library cache is scoped by the account reported by Aurelia. A known account can retain its last
valid library when offline; cached installations are checked for their folders on disk. A cache is
never used to guess an unknown account or import another account's games. Metadata and achievement
requests follow the interface's Steam language, including Brazilian Portuguese and Spanish.

Aurelia's daemon owns background transfers. Closing Launchme detaches its CLI processes; it does not
terminate Steam or running games. Reopening the launcher discovers active transfers through `install list`.

**Pause, resume, cancel.** A running transfer in **Manage · Aurelia** has *Pause download* (`install pause`, partial
files are kept) and *Cancel download* (`install stop`). Since upstream keeps an install backlog, `install list` also
returns entries nobody is downloading (state `queued`, `paused`, `aborted`, `interrupted`): Launchme shows them as
*Paused* (not as running) with *Resume download* (`install resume`, continues from the files on disk, reported like an
install), *Remove from queue* (`install forget`) and *Remove from queue and delete downloaded files*
(`install forget --delete-files`, after confirmation; upstream only deletes folders it knows are safe).

## Library maintenance

In **Manage · Aurelia** for an installed game, **Point to moved files** runs `relink <app> <library>` (Steam is pointed
at files already moved to another library; nothing is copied). For a game that is not installed, **Register existing
files** runs `import <app> <library>` (the files are already under `steamapps/common` of that library). Both use the
same library picker as *Move*. Launchme never asks Aurelia to stop or restart the Steam client for them, so they refuse
while Steam runs, as upstream does. **Settings → Library → Steam → Manage Aurelia → Find duplicate installs** runs
`duplicates` and only reports games installed in several libraries and the space the stale copies take; nothing is
deleted.

## Cloud saves

Shown for managed games while **Cloud saves** (Settings → Library → Steam, on by default) is on: a cloud
button after **…** in the details (desktop and TV) and a chip in the desktop details pane show the status. **Saves available** means files
were enumerated, not that local and remote saves match. **Synchronized** is shown only after a complete
successful sync. Conflicts, skipped paths, transfer failures and unknown state stay distinct.

**Cloud saves → Sync saves** uses Aurelia's conflict-detecting default. No conflict winner is selected
automatically. **Use cloud saves** or **Use local saves** requires confirmation. File/save operations are
blocked while the launcher knows that the game is running. **Sync before and after playing** is on
for every managed game while **Cloud saves** is on (switch it off per game in **Manage · Aurelia**): it
downloads before standalone play and uploads after the game has finished. A failed, partial or conflicting pre-launch sync prevents launch; resolve it in
the Cloud page and retry. A failed upload is reported separately from a launch failure. The ordinary
Steam launch path leaves save synchronization to Steam. Listing status never uploads or overwrites saves,
and a delayed file-list reply cannot erase a newer sync outcome.

## DLC, versions and Workshop

- **Manage DLC** lists ownership, installation and enabled state. Only owned DLC with known state can
  be installed/enabled/disabled. A running Steam client may need restarting to notice a DLC change.
- **Versions and pinning** pins the current manifests or releases the pin. Pinning controls Aurelia's
  updates; it cannot override the official Steam client's update policy.
- Enter `depot_id manifest_id` to downgrade and pin that depot. IDs are kept as exact 64-bit integers.
  The operation requires confirmation. Steam only exposes current manifest IDs; historical IDs must
  be obtained separately, for example from SteamDB. Multi-depot games may require more than one operation.
- **Beta branches** changes the selected branch; explicitly update afterward to fetch its files.
- **Steam Workshop** lists browse results and lets you subscribe and install. The ID field also accepts
  an item/collection ID to install, `+ID` to subscribe/install, or `-ID` to unsubscribe. Unsubscribing is
  not an uninstall. Workshop pagination and the upstream inventory/market/community-writing features
  are not presented in this UI.

## Friends and chat

The friends drawer (and the desktop widgets' friends) use Aurelia's roster. Selecting a friend opens the
conversation inside the drawer, which keeps its width; Back returns to the list. The Steam client is never
started for it. Aurelia's roster has no pictures: avatars come from the Steam client's own avatar cache when it is
installed, else from the friend's public Steam profile, and are cached. The conversation has recent history, incoming messages, echoes from other clients and typing notifications. Messages are sent
as a separate bounded command; a failed send retains the draft. **F5** reconnects a dropped conversation.
Closing a conversation cancels its listener; late responses cannot change another conversation.
Drafts survive closing/reconnecting within the current account's session and are cleared on account
change/logout. History is reconciled again after the listener is armed to cover the connection gap.

Keyboard: arrows (Up from the top of the log reaches Back), Enter sends, Esc returns to the list, Ctrl+V,
PgUp/PgDn, F5 reconnects. Gamepad: D-pad, A types (or takes Back), B returns, X the on-screen keyboard, Y
reconnects, LB/RB scroll. In the in-game Guide overlay the conversation takes the keyboard; the on-screen
keyboard is not available there.

## Implementation and verification

Contract reviewed against Aurelia **v0.1.40** (`a47e90bf42aa9f99818bd8d0de286a5c33da9b44`),
including the JSON command handlers and NDJSON progress/chat events; the in-app sign-in (`login --json`
events `qr_challenge`, `qr_scanned`, `guard_required`, its stdin control channel) and `is_family_shared`
against **v0.1.41**. The installer deliberately selects the latest stable release; incompatible responses
are reported rather than guessed. QR codes are drawn with Nayuki's
[QR Code generator](https://github.com/nayuki/QR-Code-generator) (MIT, fetched at build time).

- `src/store/aurelia*`: typed requests/parsers, Win32 child-process transport, release installer.
- `src/store/aurelia_lifecycle.cpp`: semantic operation results, standalone preflight, ordered cloud/play
  workflow and account-scoped library cache.
- `src/app/aurelia*`: workers, account epochs, operation ownership, native pages and input.
- `src/ui/aurelia*`: shared desktop/TV presentation and geometry.
- `aurelia_tests`: parser/64-bit identity/installer tests and a real child-process fixture for output,
  errors, streaming and cancellation. No account or external service.
- `launcher_ui_tests --aurelia`: injected end-to-end UI flows and optional PNG captures through
  `LAUNCHME_UI_CAPTURE_DIR`.

Aurelia remains a separately downloaded GPL-3.0 program. Launchme's adapter communicates through its
public CLI and contains no copied Aurelia implementation. Steam and Valve trademarks belong to Valve;
Aurelia is an independent project.

## Linux

The same integration runs on the Linux build (highly experimental). Launchme installs
`aurelia_linux_x86_64` (or `aurelia_linux_arm64`) from the same release, checks it as a 64-bit ELF executable and keeps it as
`~/.local/share/launchme/helpers/aurelia/<release>/aurelia`; its isolated profile is `~/.local/share/launchme/aurelia`.
The daemon's socket goes to the user's runtime folder (`$XDG_RUNTIME_DIR`), else to a private folder under `/tmp`.
A game installed through Aurelia starts natively when it has a Linux build, otherwise through Aurelia's own
Proton/Wine.
