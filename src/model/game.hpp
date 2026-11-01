#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <array>
#include <utility>
#include <vector>
#include "key_codec.hpp"
#include "installation.hpp"
#include "../i18n/i18n.hpp"

// custom: games the user added in Settings → Library ("Moje gry"), src/library/custom_games.hpp.
// xbox: Xbox app / Microsoft Store PC games (Gaming Services packages); appended to keep values stable.
// drmfree: games found in the user's DRM-free folders, src/library/drm_free.hpp.
// multimc: Minecraft instances of MultiMC / Prism Launcher / PolyMC, src/library/multimc.hpp.
// amazon: Amazon Games through the optional Nile helper, src/store/amazon.hpp.
enum class GameSource { steam, gog, battlenet, ea, ubisoft, epic, custom, xbox, drmfree, multimc, amazon };
inline constexpr std::array<GameSource, 8> pc_sources{GameSource::gog, GameSource::battlenet, GameSource::ea, GameSource::ubisoft, GameSource::epic,
                                                      GameSource::xbox, GameSource::multimc, GameSource::amazon};
// category/settings are XMB home tiles: a Steam collection (app_id = category index) and the Settings shortcut.
// glance/companion: the Glance and Towarzyszka tiles of the Metro and XMB homes (src/ui/home_widgets.hpp).
// tarot/add: the Tarot reading and "Dodaj grę" as home tiles (AppSettings::home_tiles, add_game_tile).
enum class GameKind { steam, all, platform, back, nonsteam, store, catalog, pc_platform, category, settings, glance, companion, tarot, add };
enum class GameVisibility { public_game, hidden, private_game };
enum class ActionKind { play, install, open_client };

struct CompatLaunch; // src/model/launch_options.hpp

struct GameAction {
    std::wstring target;
    std::wstring arguments;
    std::filesystem::path working_directory;
    ActionKind kind = ActionKind::play;
    // Launchme keeps the foreground: the started process gets no foreground permission, and
    // Launchme hands focus to the game's window itself (Steam → Ciche uruchamianie).
    bool keep_focus = false;
    // Variables the started process gets on top of Launchme's own (Amazon Games: its SDK and
    // entitlement, src/store/amazon.cpp); empty for nearly every action.
    std::vector<std::pair<std::wstring, std::wstring>> environment;
    // Linux: the game a Windows program (.exe, .msi, .bat, .lnk) belongs to and its Wine/Proton
    // options (launch_options_store::compatLaunch); null runs it with the defaults in the shared
    // "default" prefix. Ignored for every other target and on Windows.
    std::shared_ptr<const CompatLaunch> compat;
    // A helper's console starts hidden (EmuDeck's PowerShell launcher): only the program it opens shows.
    bool hidden = false;
    explicit operator bool() const { return !target.empty(); }
};

struct Game {
    uint32_t app_id = 0; // Steam only; never put another store's numeric ID here.
    std::wstring name;
    uint64_t last_played = 0;
    uint64_t last_updated = 0;
    bool installed = true;
    GameKind kind = GameKind::steam;
    uint64_t shortcut_id = 0;
    std::wstring platform;
    GameSource source = GameSource::steam;
    std::string source_id;
    GameVisibility visibility = GameVisibility::public_game;
    GameAction play_action;
    GameAction install_action;
    bool catalog_available = false;
    std::filesystem::path install_directory;
    std::filesystem::path executable;
    // Emulated Steam shortcut: the ROM/ISO named in its exe/launch options. Identifies the
    // game in ES-DE's gamelist.xml the way ES-DE itself does (by file, not by title).
    std::filesystem::path rom;
    std::string account_id; // Online-only membership; local installations are independent.
    Installation installation;
    std::filesystem::path manifest_path;
    // Installed Steam game: the account it belongs to (its manifest's LastOwner, SteamID64); 0 when
    // unknown or when the signed-in account can play it (src/library/steam_accounts.hpp).
    uint64_t steam_owner = 0;
    // Steam: a preload, the content downloaded ahead of the release (its manifest waits on "not released");
    // neither installed nor installing, playable from the release day.
    bool preloaded = false;
    // Steam Family Sharing: borrowed from another member of the account's Steam Family (Aurelia's
    // list); it installs and starts through the Steam client, which authorises the licence.
    bool family_shared = false;
    // EGS account metadata and installations managed by Launchme/Legendary.
    std::string epic_namespace, epic_catalog, epic_partner;
    // The store's own art (https on its CDN): the tall box art, the wide one and the logo; empty when
    // the store has none (artworkForGame). Epic: its catalog's keyImages (src/store/epic_api.cpp);
    // Ubisoft: the client's configurations (library/ubisoft.hpp); GOG: its public product API
    // (store/gog_art.hpp); EA: the account import's offers (store/ea_account.cpp); Battle.net: the
    // client's catalog (library/battlenet_art.hpp).
    std::string store_tall_art, store_wide_art, store_logo_art;
    bool epic_managed = false;
    // Xbox account games (store/xbox_account.hpp): the Store product ID (BigId) and whether its PC
    // package is a GDK MSIXVC one the helper can install itself (Instalacja przez Launchme).
    std::string xbox_product;
    bool xbox_own_install = false;
    std::string catalog_plugin; // Catalog owner; retained when resolving to a local Steam game.
    std::string version_group; // Display-only aggregate; raw library entries retain their store identity.
    // Play time the store or launcher counted itself (MultiMC/Prism instances, a store account),
    // in minutes; 0 when unknown. Launchme's own count for the game is kept apart (library_state).
    uint32_t store_playtime_minutes = 0;
    // Unix seconds when acquired in the store library; 0 when the source cannot tell.
    // Not an installation/update timestamp. Display-only version groups keep the newest addition.
    uint64_t added_at = 0;
};

inline const char* sourceSlug(GameSource source) {
    switch (source) {
    case GameSource::gog: return "gog";
    case GameSource::battlenet: return "battlenet";
    case GameSource::ea: return "ea";
    case GameSource::ubisoft: return "ubisoft";
    case GameSource::epic: return "epic";
    case GameSource::xbox: return "xbox";
    case GameSource::custom: return "custom";
    case GameSource::drmfree: return "drmfree";
    case GameSource::multimc: return "multimc";
    case GameSource::amazon: return "amazon";
    default: return "steam";
    }
}

inline const wchar_t* sourceName(GameSource source) {
    switch (source) {
    case GameSource::gog: return L"GOG";
    case GameSource::battlenet: return L"Battle.net";
    case GameSource::ea: return L"EA Play";
    case GameSource::ubisoft: return L"Ubisoft Connect";
    case GameSource::epic: return L"Epic Games";
    case GameSource::xbox: return L"Xbox";
    case GameSource::custom: return tr(L"Gry dodane ręcznie");
    case GameSource::drmfree: return L"DRM-free";
    case GameSource::multimc: return L"MultiMC / Prism";
    case GameSource::amazon: return L"Amazon Games";
    default: return L"Steam";
    }
}

// The title artwork and metadata are looked up by: a Minecraft instance's own name ("1.20 Fabric")
// names no game, so its pictures are Minecraft's.
inline std::wstring artworkTitle(const Game& game) {
    return game.source == GameSource::multimc ? std::wstring(L"Minecraft: Java Edition") : game.name;
}

// Games launched straight from a file on disk (user-added or found in DRM-free folders):
// not installed means the file is missing, never "install".
inline bool localFileGame(GameSource source) { return source == GameSource::custom || source == GameSource::drmfree; }
// The PC platform a game is listed under: games added by hand are DRM-free games too (one tile, library,
// tab and desktop section; the scanned ones and those added by hand keep their own lists and editors).
inline GameSource platformSource(GameSource source) { return source == GameSource::custom ? GameSource::drmfree : source; }
// A game added by hand without its file yet (one to play next, or one played elsewhere): its action
// asks for the file ("Wskaż plik") instead of reporting it missing.
inline bool fileNotChosen(const Game& game) { return game.source == GameSource::custom && !game.source_id.empty() && game.play_action.target.empty(); }
inline const wchar_t* missingFileLabel(const Game& game) { return fileNotChosen(game) ? tr(L"Wskaż plik") : tr(L"Brak pliku"); }
// On the tile: nothing for a game without its file yet (its button and the hints say „Wskaż plik”).
inline const wchar_t* missingFileBadge(const Game& game) { return fileNotChosen(game) ? L"" : tr(L"BRAK PLIKU"); }

// Stand-in names of Steam titles whose name is not known yet. They are stored in
// Game::name in the active language, so checks accept either language.
inline constexpr const wchar_t* loading_game_name = trKey(L"Ładowanie nazwy…");
inline constexpr const wchar_t* missing_game_name = trKey(L"Nazwa niedostępna");
inline bool placeholderGameName(std::wstring_view name) {
    return i18n::matches(name, loading_game_name) || i18n::matches(name, missing_game_name);
}

inline bool isGame(const Game& game) {
    return game.kind == GameKind::steam || game.kind == GameKind::nonsteam || game.kind == GameKind::store || game.kind == GameKind::catalog;
}

inline bool steamArtwork(const Game& game) {
    return game.source == GameSource::steam && (game.kind == GameKind::steam || game.kind == GameKind::catalog);
}

// Full, collision-free identity for UI messages, in-memory artwork and retries.
inline bool uiTile(const Game& game) {
    return game.kind == GameKind::all || game.kind == GameKind::back || game.kind == GameKind::category || game.kind == GameKind::settings ||
           game.kind == GameKind::glance || game.kind == GameKind::companion || game.kind == GameKind::tarot || game.kind == GameKind::add;
}
// The tiles that open a screen of the launcher (not a game, store or console): Wszystkie, Glance,
// Tarot, Towarzyszka and Dodaj grę; they may lead or close the first row (AppSettings::home_tiles).
inline bool specialTile(const Game& game) {
    return game.kind == GameKind::all || game.kind == GameKind::glance || game.kind == GameKind::companion ||
           game.kind == GameKind::tarot || game.kind == GameKind::add;
}

inline std::string gameKey(const Game& game) {
    if (uiTile(game)) return {};
    if (game.kind == GameKind::platform) return "platform:" + std::to_string(game.app_id);
    if (game.kind == GameKind::pc_platform) return std::string("pc-platform:") + sourceSlug(game.source);
    if (game.kind == GameKind::nonsteam)
        return "shortcut:" + std::to_string(game.shortcut_id ? game.shortcut_id : game.app_id);
    if (game.source == GameSource::steam)
        return game.app_id ? "steam:" + std::to_string(game.app_id) : std::string{};
    auto id = game.source_id;
    if (game.source == GameSource::battlenet)
        for (char& ch : id) if (ch >= 'A' && ch <= 'Z') ch += 'a' - 'A';
    return id.empty() ? std::string{} : std::string(sourceSlug(game.source)) + ':' + id;
}

// Encode rather than sanitize/hash: paths cannot escape the cache or alias another ID.
inline std::string gameCacheKey(const Game& game) {
    return key_codec::encode(gameKey(game));
}

inline const wchar_t* primaryActionLabel(const Game& game) {
    if (game.source == GameSource::battlenet && game.installation.phase == InstallPhase::unknown) return tr(L"Otwórz klienta");
    if (game.preloaded) return tr(L"Wstępnie pobrana");
    if (game.installation.active()) return game.source == GameSource::gog ? tr(L"Instalacja GOG") :
        game.source == GameSource::epic ? tr(L"Instalacja Epic") : game.source == GameSource::amazon ? tr(L"Instalacja Amazon") :
        game.source == GameSource::battlenet ? tr(L"Instalacja Battle.net") : tr(L"Pobieranie w Steam");
    if (game.installed) return tr(L"Uruchom");
    if (localFileGame(game.source)) return missingFileLabel(game);
    if (game.source == GameSource::gog && !game.account_id.empty()) return tr(L"Instaluj");
    if (game.source == GameSource::gog && game.install_action.target.starts_with(L"https://")) return tr(L"Otwórz GOG");
    return game.source != GameSource::steam && game.install_action.kind == ActionKind::open_client ?
           tr(L"Otwórz klienta") : tr(L"Instaluj");
}

inline std::wstring activePlatformName(const Game& game) {
    if (!game.version_group.empty()) return {};
    if (uiTile(game)) return {};
    if (game.kind == GameKind::platform || game.kind == GameKind::nonsteam) return game.platform;
    return sourceName(platformSource(game.source));
}
