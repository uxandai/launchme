# SDK v1 contract

## ABI and memory ownership

Library export: `const LaunchmePluginV1* LAUNCHME_CALL launchme_plugin_v1(uint32_t host_abi)` (`LAUNCHME_CALL` is
`__cdecl` on Windows and empty elsewhere; see `sdk/launchme_plugin.h`).
Return a stable pointer to the `{size, abi, invoke}` table, or `NULL` if the host version
is not supported. `size >= sizeof(LaunchmePluginV1)`, `abi == 1`.

```c
int32_t invoke(const char* json, uint32_t length, LaunchmeEmitV1 emit, void* context);
void emit(void* context, const char* json, uint32_t length);
```

- UTF-8; lengths in bytes, without NUL, request/response limit of 2 MiB.
- `invoke` returns 0; it calls `emit` **exactly once, synchronously**, before returning.
- The host copies the response during the callback. Input buffers and `context` are valid only
  during the call. Do not retain them or call `emit` from another thread.
- Each side manages its own memory. No STL, exceptions, HWNDs or CRT allocations across the ABI.
- All DLL calls are serialized on the worker. No reentrancy into the UI.
- A non-zero code, a missing/repeated callback, invalid JSON or `error` indicate a protocol
  error: the host deactivates the plugin and saves it as disabled.

## Request

```json
{
  "method": "command",
  "locale": "pl",
  "settings": {"api_key": "...", "available_only": false},
  "params": {
    "command": "browse",
    "refresh": false,
    "game": {"key": "steam:620", "name": "Portal 2", "source": "steam", "appId": 620,
             "installed": true, "available": false, "shortcut": false}
  }
}
```

Settings are a current snapshot on **every** call. Changing them does not require
restarting the DLL or a separate event. `locale` is `pl` or `en` (users of the other interface languages get
`en`); lifecycle calls use `en`.

| Method | `params` | Response |
| --- | --- | --- |
| `activate` | `dataDirectory`: absolute UTF-8 path to the plugin's existing directory | `{}` |
| `deactivate` | `{}`; settings may be empty | `{}`; release resources and stop your own threads |
| `command` | `command`, `game`, `refresh`; optionally `library` | effects below |
| `search` | `query`: text typed in the catalog | `games`, optional `message` |
| `widget` | `command`; optionally `library` | `text`: short widget text |
| `details` | `command`, `game`, `refresh`, `playtimeSeconds` | `comparison`: reference playtimes |
| `library` | `command` from the `library` extension | `steamAppIds`: supplementary game identifiers |
| `launch` | `command`, `game` (with `installDirectory`, `executable`) | `{"launched":true}`, or `{"launched":false, "message", "steamRequired"?}` |
| `downloads` | `command` from the `downloads` extension | `downloads`: full job snapshot, no UI effects |
| `achievements` | `command`, `game` (with paths, below), `accounts`, `refresh`; then `responses`, `state` | `achievements`, `storeRequests` or `message` |

`activate` is called when the plugin is enabled, at startup with the plugin enabled, and when validating
an active update. Even after a partially failed `activate`, the host attempts `deactivate`.
A newly installed, disabled plugin receives none of these calls.

`widget` is polled when the drawer opens, not on every frame. Clicking a widget
sends **`command` with the same command identifier**. To refresh an open list,
use F5: it repeats the list's command with `refresh: true`.
A `command` response may also contain `text`: it then refreshes the text of the widget
from which the view was opened.

`details` is read-only, with no URI/window-opening effects. The host polls the provider in the background
when Details is opened or for an active widget, with a cache of up to 60 s. A change of game,
language or plugin version has its own response identity.

### Library snapshot

A manifest with `"library": true` receives `params.library` with `command` and `widget` — an array
of DTOs in the `game` format from the example. It contains public games, including uninstalled ones;
without hidden/private entries, account tokens, disk paths or personal data.
`shortcut: true` denotes a non-Steam shortcut/emulation. `appId` is a Steam identifier
only for real Steam games; never use it instead of `key` for other sources.
The library serialization budget is 1500 KiB; a very large snapshot may be truncated.

## Manifest extensions

```json
{"id":"notes","kind":"widget","title":{"pl":"Notatki","en":"Notes"},
 "text":{"pl":"Otwórz katalog","en":"Open catalog"},"command":"browse"}
```

`id` is local to the plugin; `command` is the identifier passed to the DLL. Texts may
be a single string or a `{pl, en}` object. A widget's `text` is its initial content.

Optional manifest `tags`: an array of up to 6 unique values `widget`, `screen`,
`integration`, `menu`, `context`, `drawer`, `details`. The host translates them and draws them as labels
on the plugin tile. They do not affect routing, library access or permissions.

- `menu`: bottom bar.
- `menu` may set `glyph: "server-download"` (a server with an arrow) or `"store"`
  (the default shopping bag). The symbol keeps its contrast under the moving focus.
- `context`: game menu. An optional `source` (`steam`, `gog`, `epic`, `ea`, `battlenet`,
  `ubisoft`, `xbox`, `custom`, `drmfree`) filters games. `steam` skips non-Steam shortcuts. Context
  actions do not appear on catalog entries.
- `widget`: an additional tile in the right drawer, text + click.
- `widget` with `"activeGame": true`: receives `params.game` and `params.playtimeSeconds`
  for the confirmed active game. Without a game, the DTO has an empty key. Refreshed when
  the game changes and every 5 s while the drawer is open; a click uses the same active
  game. In Guide it selects the game under the overlay. The snapshot contains no paths or accounts.
- `details`: one provider per plugin; the first matching one gets a native block
  below playtime and achievements. Clicking it invokes its `command` with the game.
- `catalog`: one per plugin; `command` and `title` define the catalog's main action.
  To open the catalog, another command returns `openCatalog`.
- `downloads`: one per plugin. The host polls the `downloads` method every second regardless
  of which page is open. Jobs are visible in the shared “Downloads” screen.
- `achievements`: one per plugin, optional `source` filter; the achievements of games outside Steam
  (Steam games keep the client's own). The first matching provider fills the Details card and
  Szczegóły → Osiągnięcia; see [Achievements](#achievements).
- `library`: one per plugin, with `source: "steam"`; additional game identifiers,
  with no UI slot of its own. This is a separate feature from the `library: true` flag, which passes the snapshot to the DLL.
- `launch`: one per plugin, with a required `source` filter; an alternative way to start an
  installed game. It adds `title` to the game's launch options (Opcje uruchamiania → Uruchamianie);
  when chosen for a game, the host starts it through the plugin instead of the store's own path.
  See [Launch provider](#launch-provider).

## Responses and effects

### Launch provider

A `launch` contribution is invoked with the `launch` method when the user has chosen the plugin as a
game's launch route and starts it. `params.game` carries the game snapshot including
`installDirectory` and `executable` (the same fields as the achievements snapshot). The plugin starts
the game itself and returns promptly; it does not block until the game exits. The host shows the launch
and then tracks the real process through its own activity watch (the program running inside the game's
install folder), exactly as for a game started outside the launcher.

```json
{"launched": true}
```

- `launched: true`: the game was started. The host begins the session and lets the activity watch
  confirm and follow the process. No further fields are required.
- `launched: false`: the game was not started. `message` is shown to the user. Optional
  `steamRequired: true` tells the host the title needs the store client, so when the plugin was the
  default route (not an explicit per-game choice) the host falls back to the normal client path.

The call runs on the plugin worker; keep it short (spawn and return). Long work (preparation, downloads)
belongs to a `command`/`downloads` contribution invoked beforehand, not to `launch`.

### Download jobs

```json
{"downloads":[{"id":"17","title":"Game","phase":"converting","status":"Unpacking",
 "destination":"D:\\Downloads\\game.zip","fraction":0.6,"command":"job.17"}]}
```

A full snapshot (up to 1000 entries), with stable `id`s within the plugin. Phases: `queued`,
`downloading`, `converting`, `verifying`, `paused`, `done`, `failed`, `cancelled`. `fraction` 0–1 refers to the current
phase; missing/null = unknown progress. `destination` is the current output file. Clicking
a row sends `command` to the owner; polling does not change the page or focus.
Disabling/removing the plugin removes its snapshot; responses from an old epoch are ignored.

A command that adds jobs returns `queued: {"count":1,"title":"Game"}` only after the jobs have actually
been added, which shows the shared “Added to the download queue” toast. It may also return `downloads`
to register even very short jobs immediately. An observed transition to `done`
shows the “Download completed” toast, with the title and the cover, if available. The initial snapshot
of finished-job history alone, removing a job, `failed` or `cancelled` do not indicate success.
Conversion is part of the job: the plugin reports `done` only after it succeeds.

The response to an explicit command may contain `revealFile` with an absolute path. The host opens
the folder in File Explorer and selects the existing file (via PIDL, without building a shell command).
F5, background refreshes and late responses do not perform this effect.

The optional fields `artworkName`, `platform`, `titleId` describe the game for the list/toast thumbnail.
Updates and DLC pass the base game's name and Title ID. The host uses the cache, then
SteamGridDB for `PC`/`Windows` (only with a key) or ScreenScraper for a known system
(only when configured). A missing platform does not imply PC. The lookup does not scan ROM files
and does not add entries to the library. Networking, key reading, saving and decoding happen off the UI thread.

### Achievements

Asked when Details opens a game outside Steam (one question per game at a time; an answer is kept
for 10 minutes). `params.game` adds what finding the game needs: `sourceId` (the store's ID: GOG
product, Epic AppName…), `epicNamespace`, `platform`, `rom` (the file an emulator runs),
`installDirectory` and `executable`. `params.accounts` says which store accounts are signed in:
`{"gog":true,"epic":false}`.

```json
{"achievements":{"source":"GOG","items":[{"id":"KILL","name":"Monster slayer","description":"Kill 100 monsters",
 "icon":"https://images.gog.com/kill.png","achieved":true,"unlockedAt":1709634030,"percent":41.5,"hidden":false}]}}
```

`achievements: null` means none for this game (no card). `id` is unique (at most 128 bytes, 5000
items); `icon` is an https URL or an absolute path to a PNG/JPEG on this computer; `unlockedAt` is
Unix seconds; `percent` (0–100) the share of players; a `hidden` locked item shows as hidden.
The host sorts the list and counts it; the counts go to the game index (previews, desktop pane).
`message` is a passing failure: the last list stays.

**Store requests.** The plugin never sees account tokens. To call a store API with the signed-in
account it answers with requests instead:

```json
{"storeRequests":[{"id":"gog","account":"gog","method":"GET",
  "url":"https://gameplay.gog.com/clients/1207658924/users/{account}/achievements"}],"state":{"step":1}}
```

At most 4 per round and 6 rounds. `account` is `gog` (hosts `gameplay.gog.com`, `api.gog.com`,
`embed.gog.com`, `www.gog.com`, `menu.gog.com`) or `epic` (subdomains of `epicgames.com`); https only;
`POST` takes a JSON `body` up to 64 KiB. `{account}` in the URL or body becomes the account's ID.
The account worker adds the bearer token and refreshes it when needed. The host then calls
`achievements` again with `params.responses` (`[{"id","status","body","truncated"}]`: text bodies
up to 400 KiB; status 0 without the account or the network) and the same `state`.

### Library supplement

```json
{"steamAppIds":[400,620]}
```

Identification hints only, **not a source of ownership or installation information**.
The host merges them with normally detected games, deduplicates by AppID, and respects hiding/privacy
and the not-installed filter. Steam manifests take precedence. Names and metadata are fetched by
the host's existing mechanism; the plugin neither provides nor overrides this data.

The method is polled on the worker after the library is read (including after returning to the window),
after the plugin is enabled/updated, and after settings change. The host calls it only for an active
extension and a known Steam account. Results are bound to the plugin, account and generation.
Disabling or removing the plugin, or a plugin error, removes only its hints; games still present
in manifests or collections remain. An account change discards the previous snapshot.

Max. 20,000 integer AppIDs in the range 1–2147483647 (no shortcut identifiers).
An empty array replaces the previous hints with an empty set. A transient read error
returns only `message`, without `steamAppIds`: the host keeps the previous snapshot until the next
read. URI/window-opening effects are not supported by this method.

### Playtime comparison

```json
{"comparison":{"gameKey":"steam:620","mainSeconds":28800,
 "extraSeconds":43200,"completionSeconds":72000,"status":""}}
```

Seconds are integers 0–4294967295; a missing field or 0 = no reference.
`gameKey` must match the game in the request. `status` (up to 1024 characters) describes missing data
or the need to choose a match. The host draws the main story against its own playtime
and computes the percentage without capping it at 100%; the bar fits within 100% of the width.
If the counter/reference is unknown, the percentage remains unknown. The request's `playtimeSeconds`
is the host's current playtime or `null`; it is also available in `command`.

The `comparison` response is shared by `details`, the active `widget` and `command`
(e.g. after manual matching). Refreshing only the widget does not extend the provider
cache. Drawer item commands and F5 keep the game for which the drawer was opened,
even if the active session changed in the background. An automatic widget should not
change the private match state of the open page.

### Notification / URI

```json
{"message":"Done"}
```
```json
{"uri":"https://example.org/game/portal-2","message":"Page opened"}
```

Effects are executed on the UI after the generation and plugin state are checked. `uri` must have
a scheme; it must not be an EXE path or contain spaces, quotes or NUL/CR/LF.
`file:`, `javascript:` and `data:` are rejected. A `message` accompanying a URI is displayed
only after it has been successfully handed off to the system. Report network/user errors via
`message`, **not** `error` (which disables the plugin).

### Steam catalog

```json
{"openCatalog":true}
```

The host opens the owning plugin's search view; 400 ms debounce, stale results are discarded.
`search` response (max. 100 entries):

```json
{"games":[{"appId":620,"name":"Portal 2","available":true}],"message":""}
```

The host resolves local installation from its own library and displays artwork and Details.
`available` determines whether the catalog action is available. That action's `command` receives the game
and returns, e.g., a protocol URI. The host blocks duplicate clicks, and after a successful handoff
it remembers the confirmation and offers the Steam installation. Disabling the plugin removes its
UI entry points and closes the active catalog; metadata for regular games keeps working.

### Widget

```json
{"text":"12 do Twoich gier"}
```

Up to 256 characters are displayed. Do not perform external actions in `widget`; their effects
are not supported by this method.

### Native list: screen or drawer

A `drawer` response has the same format as `page` below, but replaces the contents of the right
drawer, like the friends list. The library stays in the background with its position
and artwork unchanged. Esc/B returns to the widgets; another Esc/B closes the drawer. Drawer thumbnails
have their own renderer slots (they do not overwrite library cards).

```json
{
    "page": {
    "title": "Notes",
    "status": "",
    "sections": [
      {"title":"For your games","empty":"No matches","items":[
        {"title":"Portal 2","subtitle":"Portal 2","gameKey":"steam:620",
         "uri":"https://example.org/gra/portal_2"}
      ]},
      {"title":"Wszystkie notatki","items":[
        {"title":"Portal 2","uri":"https://example.org/gra/portal_2"}
      ]}
    ]
  }
}
```

The plugin determines the order of sections and items. `gameKey` adds a small square image from
the launcher's public library. An unknown/private key does not allow the artwork to be read.
Alternatively, `downloadId` points to a job in the same plugin's `downloads` snapshot.
The row uses the shared “Downloads” thumbnail, even when it arrives asynchronously.
An unknown ID yields a placeholder; it is not resolved through another plugin's jobs.
`gameKey` and `downloadId` are mutually exclusive. Without either field, the row has no thumbnail.
Custom image files cannot be passed.

Instead of `uri`, an item may contain `command`, e.g.:

```json
{"title":"Load","command":"load","enabled":true}
```

`command`: 1–64 ASCII characters `a-zA-Z0-9_.-`, sent only to the owner of the
page as the `command` method. It does not require a separate slot in `contributions`.
`uri` and `command` are mutually exclusive. Without either field, the row is informational.
`enabled: false` disables the action and excludes it from navigation and hit-testing. A missing field means
`true`. A repeated click while the command is still running is ignored.
F5 still invokes the command that **opened** the view, not the row's last action.

Click/Enter/A runs the command or opens an HTTP(S) `uri` in the default browser.
Arrow keys/D-pad and the mouse wheel scroll; headers and informational rows are skipped
by focus; Esc/B returns to the main screen.
Limits: 8 sections, 4000 rows in total (including headers), page title 256 characters,
item title 512, description 1024, URI 2048. Artwork is fetched only for visible items.

Optional page fields (added in v1; older plugins do not need them):

```json
{"title":"Katalog","back":"back","refresh":1,
 "search":{"command":"find","placeholder":"Search by name","query":"portal"},
 "sections":[...]}
```

- `search`: a search field above the list (screen only; ignored in the drawer). Focus
  starts on the first row of the list, with the field above it (↑, or X on a gamepad, which
  also opens the on-screen keyboard). Typing is local; Enter / “Done” sends
  `command` with `params.query` (up to 100 characters). `query` is the text shown in the field.
  Backspace in an empty field acts like Esc/B.
- `back`: a command (same format as an item's `command`) sent instead of leaving the view via
  Esc/B, the Back button or Backspace in an empty field — one level up within the plugin.
  Without this field, Esc/B closes the view as before.
- `dialog: true`: a centered decision panel. Navigation, scrolling and hit-testing use
  the panel's geometry; `back` lets the user skip optional choices without cancelling the download.
- `refresh`: 1–60 s. While the page is visible, the host repeats the opening command
  with `refresh: true` in the background at that interval (e.g. for download progress). This does not block rows:
  a command selected in the meantime takes precedence, and a late refresh response
  is discarded. Skipped while typing in the field and while the keyboard is open.
  An F5 or refresh response with the same title preserves focus (or the nearest
  active row), scroll position and the text being typed. The plugin remembers which level it is showing.

## Settings

Types `boolean`, `choice`, `text`, `secret`, `folder`; each has an `id`, `title` and `default`.
`choice` has max. 12 `{value,title}` entries and a default equal to one of the values.
Text/secret/folder: up to 512 bytes of UTF-8. The host validates the types, handles the controls and saves
settings atomically. A secret is masked, and the field is empty when it is edited again;
confirming an empty field clears the value. The UI settings snapshot never contains the secret.
`folder` is a row with the path and a “Choose”/“Change” button that opens the launcher's folder
browser (gamepad, keyboard, mouse, Windows dialog); the value is an absolute path
or empty text. The plugin validates it itself before use.

## Compatibility

The plugin's semantic version (`version`) is package metadata; installing the same
ID may also be a downgrade. The API/ABI version (`apiVersion`, `abi`) is a separate contract.
A v1 host rejects other versions. Do not change the meaning of existing v1 fields; future
incompatible extensions require a new version. JSON Schema describes the messages,
while path and response validation and UI limits are also enforced in the host code.
