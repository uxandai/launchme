# Playnite import: analysis and plan

Status: analysis only, nothing is implemented. Facts about Playnite come from its sources
(`JosefNemec/Playnite` and `JosefNemec/PlayniteExtensions`, master, Playnite 10.64); items marked
**verify** were not confirmed from source and must be checked against a real library before coding.
No Playnite library exists on the development PC, so nothing here was tested on real data.

## Goal

One action that brings a Playnite user's own data into launchme: play time, last played, date added,
favourites, hidden games, categories, hand-added games and (optionally) their chosen covers.
The games themselves are not imported for stores launchme already scans: launchme finds them itself,
the import only attaches the user's history and organisation to them.

## What Playnite keeps on disk

| Item | Fact |
| --- | --- |
| Library folder | Installed: `%AppData%\Playnite\library`. Portable: `{PlayniteDir}\library`. Custom: `DatabasePath` in Playnite's `config.json` (**verify** the key name and file location), with `%AppData%` and `{PlayniteDir}` expanded. |
| Files | One LiteDB file per collection: `games.db`, `platforms.db`, `emulators.db`, `genres.db`, `companies.db`, `tags.db`, `categories.db`, `ageratings.db`, `series.db`, `regions.db`, `sources.db`, `features.db`, `scanners.db`, `filterpresets.db`, `importexclusions.db`, `completionstatuses.db`, plus `database.json` (format version, currently 4). |
| Engine | LiteDB **4.1.4** (not 5), opened with `Mode=Exclusive;Cache Size=0`, no password. Standard LiteDB 5 tools cannot open these files. |
| Media | `library\files\<game Guid>\<file>`. `CoverImage`, `BackgroundImage`, `Icon` hold `<Guid>\<file>` (relative to `files`), an http URL, or a local path. |
| Locking | Playnite keeps every file open exclusively while it runs: the import needs Playnite closed (**verify** that a shared read really fails). |
| Backups | `PlayniteBackup-<date>.zip`: `library/` holds the same `.db` files, `libraryfiles/` the media. A backup is a valid import source. |
| Export | No built-in JSON/CSV export was found (**verify**); only extensions offer one. |

Fields of a game we care about (`Playnite.SDK.Models.Game`):

| Field | Type | Use |
| --- | --- | --- |
| `Id` | Guid | Playnite's own id; names the media folder. |
| `Name` | string | Title (may be edited by the user). |
| `PluginId` | Guid | Which library plugin owns the game; `Guid.Empty` = added by hand. |
| `GameId` | string | The store's id (table below). |
| `Playtime` | ulong, seconds | Play time. |
| `LastActivity`, `Added` | DateTime? | Last played, date added. |
| `Favorite`, `Hidden` | bool | |
| `CategoryIds`, `TagIds`, `PlatformIds` | List of Guid | Names are in `categories.db`, `tags.db`, `platforms.db`. |
| `IsInstalled`, `InstallDirectory` | bool, string | Only for hand-added games. |
| `GameActions` | list | `Type` (File 0, URL 1, Emulator 2, Script 3), `Path`, `Arguments`, `WorkingDir`, `IsPlayAction`. |
| `Roms` | list of `{Name, Path}` | Emulated games. |
| `CoverImage`, `BackgroundImage` | string | See Media. |

`Path`, `Arguments` and `WorkingDir` may contain `{InstallDir}`, `{InstallDirName}`, `{ImagePath}`,
`{ImageNameNoExt}`, `{ImageName}`, `{ImageDir}`, `{PlayniteDir}`, `{Name}`, `{Platform}`, `{GameId}`,
`{DatabaseId}`, `{PluginId}`, `{Version}`, `{EmulatorDir}`.

Library plugins and what they store in `GameId`:

| Store | `PluginId` | `GameId` | launchme key |
| --- | --- | --- | --- |
| Steam | `CB91DFC9-B977-43BF-8E70-55F46E410FAB` | AppID (**verify**) | `steam:<AppID>` |
| GOG | `AEBE8B7C-6DC3-4A66-AF31-E7375C6B5E9E` | product id | `gog:<product>` |
| Epic | `00000002-DBD1-46C6-B5D0-B1BA559D10E4` | app name (not the catalog item id) | `epic:<id>` (**verify** our id is the app name) |
| Ubisoft | `C2F038E5-8B92-4877-91F1-DA9094155FC5` | uplay / install id | `ubisoft:<product id>` (**verify**) |
| Battle.net | `E3C26A3D-D695-4CB7-A769-5FF7612C7EDD` | product code (`W3`, `D2`…) | `battlenet:<family>` through the table in `battlenet.cpp` |
| Xbox | `7e4fbb5e-2ae3-48d4-8ba0-6b30e7a4e287` | package family name | `xbox:<family>` for installed games; account games use another id (**verify**) |
| Amazon | `402674cd-4af6-4886-b6ec-0e695bfa0688` | product id | `amazon:<id>` (**verify**) |
| EA | not confirmed | not confirmed | probably no id match: by title |
| itch.io | `00000001-EBB2-4EEC-ABCB-7C89937A42BB` | numeric id | no such source: hand-added game |
| Humble | `96e8c4bc-ec5c-4c8b-87e7-18ee5a690626` | machine name | no such source: hand-added game |

## How to read LiteDB 4

Three ways were considered.

1. **Our own read-only reader in C++ (recommended).** The format is small: 4096-byte pages, a header
   page, data pages whose blocks hold BSON documents, extend pages for documents longer than a page.
   Each Playnite file holds one collection, so a full scan of data pages is enough; indexes are not
   needed. About 400 to 600 lines with the BSON decoder, no new dependency, works on a backup zip and on
   Linux (a library copied from Windows), testable offline with fixture files. LiteDB is MIT, so
   reading its source for the layout is fine under GPL.
2. **PowerShell with Playnite's own `LiteDB.dll`.** Windows PowerShell 5.1 can load the DLL from the
   Playnite folder and dump the collections to JSON. No format work, but it needs Playnite still
   installed, starts a script host from the launcher and cannot read a bare backup. Good as the
   **oracle during development**: it produces the reference JSON the C++ reader is tested against.
3. **A Playnite extension that exports JSON.** The user must install it and run Playnite; too much
   friction for a migration feature. Rejected.

Reader details to confirm against LiteDB v4.1.4 source before writing code (**verify** all):

- Page header layout and page types (header 1, collection 2, index 3, data 4, extend 5); data block =
  index, extend page id, length, bytes; long documents continue through the extend chain.
- Guid is stored as BSON binary in .NET byte order (first three groups little-endian). The media
  folder is named by the Guid's text form, so the bytes must be reordered when formatted.
- `ulong` (`Playtime`, `PlayCount`) has no BSON type; accept int32, int64, double and decimal.
- DateTime is UTC milliseconds since 1970.
- `ReleaseDate` is a string `yyyy-M-d`, `yyyy-M` or `yyyy` (not needed in the first version).
- Null and empty strings are not stored at all; every field is optional.
- A leftover journal file next to a `.db` means an interrupted write: refuse that file with a message
  ("start and close Playnite once").

The reader treats its input as hostile: bounds on every offset, caps on file size, document size,
page count and chain length, no exceptions out of the parser, never a write into Playnite's folder.

## Mapping to launchme

Matching a Playnite game to a launchme game, in order: store id (table above), then exact normalised
title within the same store (the normalisation already used for version groups), otherwise unmatched.

| Playnite | launchme | Rule |
| --- | --- | --- |
| `Playtime` | `LibraryState::playtime_ms[historyKey]` | `max(existing, imported)`, never a sum: both launchers may have counted the same sessions. Steam games are skipped (Steam's own count is used). |
| `LastActivity` | `last_played[historyKey]` | Newer wins. |
| `Added` | `first_seen[gameKey]` | Older wins. |
| `Favorite` | member of `favorites:local` | Non-Steam only by default (see Steam below). |
| `Hidden` | `hidden` | Same. |
| Categories | new `local:<id>` collections, members by game key | A category with no matched game is not created. An existing local collection of the same name is reused. |
| Tags | nothing | Optional later; libraries often carry dozens of scraped tags. |
| Hand-added game (`PluginId` empty) with a File play action | `custom_games::Entry` | `Path`, `Arguments`, `WorkingDir` with variables expanded; skipped if the same executable is already in the library (DRM-free scan, another custom game). |
| itch.io, Humble, unknown plugins, installed, with a File action | `custom_games::Entry` | Same; otherwise listed as skipped. |
| Emulated game (Emulator action, `Roms`) | nothing in the first version | launchme finds ROMs from its own ROM folders; history could be attached later by ROM path. |
| `CoverImage`, `BackgroundImage` (local files) | manual artwork choice | Optional, see open questions. Validated with WIC; the slot (cover, poster, landscape, hero) is chosen by aspect ratio. |
| Notes, description, scores, completion status, links | nothing | No home for them yet. |

Things that need care:

- **History keys carry the account** (`gog-user:<account>/gog:<id>`, `epic-user:…`, `steam-user:…`,
  otherwise `local/<key>`). Importing GOG or Epic history before the account is connected would
  write `local/…` rows that the connected account never reads. The plan step takes the current
  accounts; the summary tells the user to connect the store first when matches depend on it.
- **Steam favourites, hidden and collection edits are queued for the Steam client**
  (`steam_sync_state`): importing them changes the user's Steam library. Off by default, a separate
  switch with that sentence next to it.
- **`custom_games::max_games` is 500.** Count before applying; the summary reports what does not fit.
- **Idempotent.** A map `Playnite Guid → custom id / collection id` is kept in the data folder
  (`imports\playnite.json`) so a second run updates instead of duplicating.
- **Undo.** Copies of `library-state.ini` and `custom-games.json` are written before applying
  (`*.before-playnite`), and the summary names them.

## Flow

1. Entry: Settings → Library → *Import from Playnite*; also offered in the first-run tour when a
   Playnite library is found (the moment a migrating user decides whether to stay).
2. Locate: `config.json` → default folder → *Choose folder…* (own file browser; accepts a library
   folder, a Playnite folder or a backup zip). Valid = `games.db` and `database.json` present.
3. If `Playnite.DesktopApp.exe` or `Playnite.FullscreenApp.exe` runs: ask the user to close it and
   retry. Never close it for them.
4. Worker: read the files into memory, build the plan against the current library. No changes yet.
5. Summary: games read, matched, hand-added to create, skipped (with reasons), and the switches:
   play time and dates; favourites, hidden, categories; hand-added games; covers; also for Steam.
6. Apply on the UI thread (library state, custom games, artwork copies on the worker), rescan, report,
   and a log of every decision in `launchme.log`.

Unknown `database.json` version (Playnite 11 is a rewrite, an alpha ships SQLite): stop with a clear
message. SQLite is already linked, so that reader would be a small follow-up once the format is public.

## Code layout (following the existing patterns)

| File | Content |
| --- | --- |
| `src/library/litedb4.{hpp,cpp}` | Read-only LiteDB 4 pages → BSON documents → the existing JSON value type. Pure. |
| `src/library/playnite.{hpp,cpp}` | Locate the library, read it into `playnite::Library`, `playnite::plan(library, games, accounts, options)` → edits, custom entries, artwork copies, report. Pure, no Win32. |
| `src/app/playnite_import.cpp`, `playnite_import_state.hpp` | `Launcher` methods: worker (`work_.start`), `RemoteMessage` with a generation counter, applying the plan. |
| `src/ui/settings_view.hpp`, `src/app/settings_rows.cpp`, `settings.cpp` | The page and its rows; summary reuses the existing prompt and list rows. |
| `src/i18n/strings_en.cpp` (+ es, pt-BR) | Texts. |
| `cmake/Sources.cmake`, `cmake/Linux.cmake` | New sources; the two `src/library` files also in `LAUNCHME_CORE_SOURCES`. |
| `tests/playnite_tests.cpp` | Reader against fixture `.db` files made with real LiteDB 4.1.4 (multi-page documents, Guid order, missing fields), truncated and corrupt files, mapping, idempotent second run, the 500 limit. |
| `tests/playnite_import_ui_cases.hpp` | `--playnite-import`: page, summary, applied state, PNG captures; fixture folder instead of a real library. |

## Phases

0. **Spike (half a day).** A real library (a portable Playnite in a scratch folder with a few stores,
   or a `games.db` from a Discord user). Dump it with PowerShell and Playnite's `LiteDB.dll`; settle
   every **verify** above; keep small files as fixtures.
1. **Reader and plan, dry run only (3 days).** Settings page shows the summary; nothing is written.
2. **Apply (2 days).** Play time, dates, favourites, hidden, collections for non-Steam games,
   hand-added games, undo copies, idempotency map.
3. **Extras (2 days).** Covers, the Steam switch, the tour offer, backup zip as a source.

## Decisions for the owner

1. Steam favourites, hidden and categories: import them at all, given they sync to the Steam client?
   (Proposed: a separate switch, off.)
2. Covers: import the user's Playnite covers as manual choices for every matched game, only for
   hand-added games, or never? (Proposed: a switch, on for hand-added games only.)
3. Emulated games: leave to the ROM scan in the first version? (Proposed: yes.)
4. Tags as collections? (Proposed: no.)
5. Raise `custom_games::max_games` for imports? (Proposed: keep 500, report the rest.)

## Sources

- https://github.com/JosefNemec/Playnite (`Database/GameDatabase.cs`, `Database/Collections/*.cs`,
  `Playnite.csproj`, `Backup.cs`, `PlayniteSDK/Models/Game.cs`, `GameAction.cs`, `ReleaseDate.cs`,
  `PlayniteSDK/ExpandableVariables.cs`)
- https://github.com/JosefNemec/PlayniteExtensions (`source/Libraries/*Library/*.cs`)
- https://api.playnite.link/docs/manual/library/backup.html
- https://github.com/mbdavid/LiteDB (tag v4.1.4, `LiteDB/Engine/Pages`)
