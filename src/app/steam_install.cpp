// Steam games in Launchme's own hands (src/library/steam_accounts.hpp), each behind its switch in
// Ustawienia → Biblioteka → Uruchamianie:
// - Instalacja Steam w launcherze: Install opens Launchme's dialog (gog_install_ui_ with `steam`)
//   with the library folders and their free space; the chosen one gets the game's install stub and
//   Steam, restarted in its tray, downloads it there. "Okno instalacji Steam" keeps Steam's own.
// - Przełączanie kont Steam: Play on a game another remembered account installed (its manifest's
//   LastOwner) closes Steam and makes it sign in to that account; the game starts once it has.
#include "launcher.hpp"
#include "../platform/text_encoding.hpp"

#include <algorithm>

namespace {
bool steamGame(const Game& game) { return game.source == GameSource::steam && game.kind == GameKind::steam && game.app_id; }
}

bool Launcher::steamGameRunning() const {
    // Steam closes its games when it quits: a switch or a restart waits for them (only those seen running).
    return blockingGame(true).has_value();
}

// ---- Instalacja Steam w launcherze --------------------------------------------------------------

bool Launcher::steamInstallDialogWanted(const Game& game) const {
    return settings_.steam_install_dialog && steamGame(game) && !game.installed && !game.installation.active();
}

bool Launcher::openSteamInstall(const Game& game) {
    // The dialog is GOG's and Epic's too: never over their running install.
    if (gog_install_ui_.snapshot.busy() && !gog_install_ui_.steam) return false;
    auto folders = steam_libraries_();
    if (folders.empty()) return false;
    dismissMenu(); closeQuick(); closeFriends(); closeCategoryMenu(); cancelEnterHold();
    gog_install_ui_ = {};
    gog_install_ui_.game = game;
    gog_install_ui_.steam = true;
    // The library with the most free space first; Install is focused.
    const auto roomiest = std::max_element(folders.begin(), folders.end(), [](const auto& a, const auto& b) { return a.free < b.free; });
    gog_install_ui_.library = static_cast<int>(roomiest - folders.begin());
    gog_install_ui_.libraries = std::move(folders);
    gog_install_ui_.row = 3;
    gog_install_ui_.open = true;
    audio_.context();
    InvalidateRect(hwnd_, nullptr, FALSE);
    return true;
}

void Launcher::steamInstallAccept() {
    auto& ui = gog_install_ui_;
    if (!ui.open || !ui.steam) return;
    switch (ui.row) {
    case 0: gogInstallKey(VK_RIGHT, false); return; // The next library.
    case 1: { // Steam's own install window, as without this dialog.
        if (ui.steam_busy) return;
        const auto id = std::to_wstring(ui.game.app_id);
        ui.open = false;
        if (execute_(hwnd_, GameAction{L"steam://install/" + id, {}, {}, ActionKind::install}) <= 32)
            showNotification(NotificationKind::error, tr(L"Nie udało się rozpocząć instalacji"), ui.game.name);
        return;
    }
    case 4: gogInstallKey(VK_ESCAPE, false); return;
    default: break;
    }
    if (ui.steam_busy || ui.library < 0 || ui.library >= static_cast<int>(ui.libraries.size())) return;
    if (steamGameRunning()) {
        showNotification(NotificationKind::error, tr(L"Instalacja Steam"), tr(L"Najpierw zamknij grę ze Steam: Steam uruchomi się ponownie."));
        return;
    }
    ui.steam_busy = true;
    const auto generation = ++steam_install_generation_;
    const auto game = ui.game;
    const auto library = ui.libraries[static_cast<size_t>(ui.library)].path;
    const auto owner = account_;
    // A throw or a failed start ends as a failure too: steam_busy never stays set.
    runOnWorker([game, library, owner, installer = steam_installer_] { return installer(game, library, owner); },
                [generation](Launcher& app, std::optional<bool> ok) { app.steamInstallFinished(generation, ok.value_or(false)); });
    audio_.context();
}

void Launcher::steamInstallFinished(uint32_t generation, bool ok) {
    if (generation != steam_install_generation_) return;
    auto& ui = gog_install_ui_;
    if (!ui.steam) return;
    ui.steam_busy = false;
    if (!ok) {
        // The dialog stays: another library, or Steam's own window.
        showNotification(NotificationKind::error, tr(L"Nie udało się przygotować instalacji Steam"), ui.game.name);
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    const auto name = ui.game.name;
    ui = {};
    downloadQueued(name); // Steam's manifest now says it downloads: the library scan follows its progress.
    InvalidateRect(hwnd_, nullptr, FALSE);
}

// ---- Przełączanie kont Steam --------------------------------------------------------------------

// Szczegóły → "…" → Konto Steam: the default (the account that installed it, when switching is on),
// then every account Steam remembers. The list is read at most every few seconds (the menu repaints).
const std::vector<steam_accounts::Account>& Launcher::rememberedSteamAccounts() const {
    const auto now = GetTickCount64();
    if (!steam_accounts_cache_at_ || now - steam_accounts_cache_at_ > 5000) {
        steam_accounts_cache_.clear();
        for (auto& account : steam_accounts_()) if (account.remember && !account.account_name.empty() && account.steam_id) steam_accounts_cache_.push_back(std::move(account));
        steam_accounts_cache_at_ = now;
    }
    return steam_accounts_cache_;
}
std::vector<uint64_t> Launcher::steamAccountChoices(const Game& game) const {
    if (!steamGame(game) || !game.installed) return {};
    const auto& accounts = rememberedSteamAccounts();
    if (accounts.size() < 2) return {};
    std::vector<uint64_t> choices{0};
    for (const auto& account : accounts) choices.push_back(account.steam_id);
    return choices;
}
std::wstring Launcher::steamAccountLabel(const Game& game) const {
    const auto found = settings_.steam_launch_accounts.find(std::to_string(game.app_id));
    if (found == settings_.steam_launch_accounts.end()) return tr(L"domyślne");
    for (const auto& account : rememberedSteamAccounts())
        if (account.steam_id == found->second) return account.persona.empty() ? text_encoding::wideLenient(account.account_name) : account.persona;
    return tr(L"domyślne");
}
void Launcher::cycleSteamAccount(const Game& game) {
    const auto choices = steamAccountChoices(game);
    if (choices.size() < 2) return;
    const auto id = std::to_string(game.app_id);
    const auto found = settings_.steam_launch_accounts.find(id);
    const uint64_t current = found == settings_.steam_launch_accounts.end() ? 0 : found->second;
    auto at = std::find(choices.begin(), choices.end(), current);
    const uint64_t next = at == choices.end() || at + 1 == choices.end() ? choices.front() : *(at + 1);
    if (next) settings_.steam_launch_accounts[id] = next; else settings_.steam_launch_accounts.erase(id);
    settings_dirty_ = true; saveSettings();
    audio_.navigate(); InvalidateRect(hwnd_, nullptr, FALSE);
}

std::optional<steam_accounts::Account> Launcher::steamSwitchTarget(const Game& game, bool& unremembered) const {
    unremembered = false;
    if (!steamGame(game) || !game.installed || !account_) return {};
    // A chosen account (Szczegóły → "…" → Konto Steam) always; otherwise the installing one, when switching is on.
    const auto chosen = settings_.steam_launch_accounts.find(std::to_string(game.app_id));
    const uint64_t wanted = chosen != settings_.steam_launch_accounts.end() ? chosen->second : settings_.steam_account_switch ? game.steam_owner : 0;
    if (!wanted || wanted == account_) return {};
    for (auto& account : steam_accounts_()) {
        if (account.steam_id != wanted) continue;
        if (account.remember && !account.account_name.empty()) return account;
        unremembered = true; // Steam would ask for its password: Launchme leaves it to Steam.
        return {};
    }
    return {}; // Not an account of this PC (a borrowed game): Steam decides.
}

bool Launcher::beginSteamAccountSwitch(const Game& game) {
    bool unremembered = false;
    const auto target = steamSwitchTarget(game, unremembered);
    if (unremembered) showNotification(NotificationKind::error, tr(L"Konto Steam nie jest zapamiętane"),
                                       tr(L"Zaloguj się na nie raz w Steam z opcją „Zapamiętaj mnie”."));
    if (!target) return true;
    if (steamGameRunning()) {
        showNotification(NotificationKind::error, tr(L"Przełączanie kont Steam"), tr(L"Najpierw zamknij grę ze Steam: Steam uruchomi się ponownie."));
        return false;
    }
    steam_switch_pending_ = true;
    const auto generation = ++steam_switch_generation_;
    const auto account = *target;
    showNotification(NotificationKind::launch, tr(L"Przełączanie kont Steam"), account.persona.empty() ? text_encoding::wideLenient(account.account_name) : account.persona);
    // A throw or a failed start ends as a failed switch: steam_switch_pending_ (which holds every
    // launch back) never stays set.
    runOnWorker([account, switcher = steam_switcher_] { return switcher(account); },
                [generation, persona = account.persona](Launcher& app, std::optional<bool> ok) { app.steamSwitchFinished(generation, ok.value_or(false), persona); });
    return true;
}

void Launcher::steamSwitchFinished(uint32_t generation, bool ok, const std::wstring& persona) {
    if (generation != steam_switch_generation_ || !steam_switch_pending_) return;
    steam_switch_pending_ = false;
    if (ok) { InvalidateRect(hwnd_, nullptr, FALSE); return; } // tick() starts the game: Steam signs in to the account on the way.
    pending_launch_ = false; launch_progress_ = 0;
    stopLaunchAnimation();
    if (!anyActiveSession()) audio_.resumeMusic();
    showNotification(NotificationKind::error, tr(L"Nie udało się przełączyć konta Steam"), persona);
    InvalidateRect(hwnd_, nullptr, FALSE);
}
