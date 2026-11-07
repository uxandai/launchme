// PC settings: one store row on the overview; its account/library actions behind the cog.
#include "launcher.hpp"
#include <algorithm>

using namespace settings_ui;
namespace {
template<class State> bool signing(State state) {
    return state == State::preparing || state == State::signing_in || state == State::verifying;
}
template<class State> bool working(State state) { return signing(state) || state == State::syncing; }
// The Xbox helper signs in in its own window: no separate verification step.
bool xboxSigning(xbox_account::State state) { return state == xbox_account::State::preparing || state == xbox_account::State::signing_in; }
bool xboxWorking(xbox_account::State state) { return xboxSigning(state) || state == xbox_account::State::syncing; }
std::wstring syncedAt(uint64_t seconds) {
    if (!seconds) return {};
    const time_t time = static_cast<time_t>(seconds); tm local{}; localtime_s(&local, &time);
    wchar_t text[64]{}; wcsftime(text, std::size(text), L"%Y-%m-%d %H:%M", &local);
    return tr(L"Ostatnia synchronizacja: ") + std::wstring(text);
}
}

void Launcher::pcSettingsRows() {
    const auto installed = [&](GameSource source) {
        return std::count_if(store_library_.begin(), store_library_.end(), [&](const auto& game) { return game.source == source && game.installed; });
    };
    const auto library = [&](GameSource source) {
        if (pc_scanning_) return std::wstring(tr(L"Skanowanie…"));
        const auto local = tr(L"Zainstalowane: ") + std::to_wstring(installed(source));
        if ((source == GameSource::ubisoft && ubisoft_account_) || (source == GameSource::ea && ea_import_.snapshot)) {
            size_t owned = 0;
            for (const auto* list : {&store_library_, &store_private_library_})
                owned += std::count_if(list->begin(), list->end(), [&](const auto& game) { return game.source == source; });
            return tr(L"Biblioteka: ") + std::to_wstring(owned) + L" · " + local;
        }
        if (source == GameSource::battlenet) return battlenet_available_ ? tr(L"Lokalne instalacje: ") + std::to_wstring(installed(source)) : std::wstring(tr(L"Lokalne instalacje: stan niedostępny"));
        return local;
    };
    const auto account = [&](GameSource source) {
        if (source == GameSource::gog)
            return gog_state_.state == gog::State::disconnected && gog_state_.failure == gog::Failure::none ? std::wstring(tr(L"Konto niepołączone")) : gog::statusText(gog_state_);
        if (source == GameSource::epic)
            return epic_state_.state == epic::State::disconnected && epic_state_.failure == epic::Failure::none ? std::wstring(tr(L"Konto niepołączone")) : epic::statusText(epic_state_);
        if (source == GameSource::amazon) return amazon::statusText(amazon_state_);
        if (source == GameSource::xbox && (xbox_state_.state != xbox_account::State::disconnected || xbox_state_.failure != xbox_account::Failure::none))
            return xbox_account::statusText(xbox_state_);
        if (source == GameSource::ea) return ea_import_.busy ? std::wstring(tr(L"Importowanie…")) : ea_import_.awaiting ?
            std::wstring(tr(L"Oczekiwanie na dane logowania")) : ea_import_.snapshot ? library(source) : std::wstring(tr(L"Konto niepołączone"));
        return library(source);
    };
    const auto platformRow = [&](GameSource source, int indent) {
        auto summary = account(source);
        if (source == GameSource::gog || source == GameSource::epic || source == GameSource::amazon ||
            (source == GameSource::xbox && (xbox_state_.state != xbox_account::State::disconnected || xbox_state_.failure != xbox_account::Failure::none)) ||
            (source == GameSource::ea && (!ea_import_.snapshot || ea_import_.busy || ea_import_.awaiting))) summary += L" · " + library(source);
        if (source == GameSource::battlenet && battlenet_import_.snapshot)
            summary += L" · " + std::wstring(tr(L"Gry konta: ")) + std::to_wstring(battlenet_import_.snapshot->games.size());
        Row row{Id::pc_platform, Control::platform, sourceName(source), std::move(summary), tr(L"Opcje"), settings_.enabled_pc_sources.contains(sourceSlug(source))};
        row.source = source; row.indent = indent;
        settings_rows_.push_back(std::move(row));
    };
    if (!settings_state_.platform()) {
        // The DRM-free catalogue first (the user's own library: its folders and the games added by
        // hand), Emulacja under it, then Steam and the stores by name.
        {
            auto summary = drmSummary();
            if (!custom_games_.empty()) summary += L" · " + (tr(L"Dodane ręcznie: ") + std::to_wstring(custom_games_.size()));
            Row drm{Id::pc_platform, Control::platform, tr(L"Katalog DRM-free"), std::move(summary), tr(L"Opcje"),
                    settings_.enabled_pc_sources.contains(sourceSlug(GameSource::drmfree))};
            drm.source = GameSource::drmfree; settings_rows_.push_back(std::move(drm));
        }
        settings_rows_.push_back(emulationTileRow());
        {
            const auto steam_installed = std::count_if(installed_library_.begin(), installed_library_.end(), [](const auto& game) {
                return game.source == GameSource::steam && game.kind == GameKind::steam && game.installed;
            });
            std::wstring summary = tr(L"Zainstalowane: ") + std::to_wstring(steam_installed);
            if (aurelia_active_) summary += L" · Aurelia";
            Row steam{Id::pc_platform, Control::platform, L"Steam", std::move(summary), tr(L"Opcje"), settings_.steam_enabled};
            steam.source = GameSource::steam;
            steam.section = tr(L"Sklepy"); // A gap after the user's own library (DRM-free, Emulacja).
            settings_rows_.push_back(std::move(steam));
        }
        // The stores that are on, then the others in a fold: most players use two or three of them.
        if (!platforms_top_) {
            platforms_top_.emplace();
            for (const auto source : platformOrder())
                if (settings_.enabled_pc_sources.contains(sourceSlug(source))) platforms_top_->push_back(source);
        }
        std::vector<GameSource> off;
        for (const auto source : platformOrder()) {
            if (source == GameSource::steam || source == GameSource::drmfree) continue;
            if (std::find(platforms_top_->begin(), platforms_top_->end(), source) == platforms_top_->end()) { off.push_back(source); continue; }
            platformRow(source, 0);
        }
        if (off.empty()) return;
        Row fold{Id::platforms_more, Control::fold, tr(L"Więcej platform"), tr(L"Wyłączone sklepy i launchery. Włącz, żeby dodać ich gry."),
                 std::to_wstring(off.size()), platforms_all_};
        settings_rows_.push_back(std::move(fold));
        if (platforms_all_) for (const auto source : off) platformRow(source, 1);
        return;
    }

    const auto source = *settings_state_.platform();
    if (source == GameSource::drmfree) { drmSettingsRows(); return; }
    const auto add = [&](Row row) { row.source = source; settings_rows_.push_back(std::move(row)); };
    if (source == GameSource::steam) {
        // The Steam client's library and launch options, then Aurelia: optional, and only for the
        // games it is chosen for (or every game, with its default), the client keeps the rest.
        add({Id::steam_sync, Control::action, tr(L"Biblioteka"), steamSyncStatus(), tr(L"Odśwież")});
        add({Id::steam_silent_launch, Control::toggle, tr(L"Ciche uruchamianie Steam"),
            tr(L"Steam zostaje w zasobniku i nie przejmuje fokusu, launcher przekazuje go oknu gry. Okna Steam mogą zostać w tle."),
            {}, settings_.steam_silent_launch});
#ifdef _WIN32
        {
            Row hide{Id::steam_hide_launch_window, Control::toggle, tr(L"Ukryj okno „Uruchamianie…” Steam"),
                tr(L"Steam pokazuje na kilka sekund małe okno „Uruchamianie…” tuż przed grą. Launchme chowa je zaraz po otwarciu (tylko to jedno okienko, tylko po starcie gry z launchme)."),
                {}, settings_.steam_hide_launch_window};
            hide.lines = 2;
            add(std::move(hide));
        }
#endif
        add({Id::steam_account_switch, Control::toggle, tr(L"Przełączanie kont Steam"),
            tr(L"Gra zainstalowana z innego konta zapamiętanego w Steam: Steam uruchamia się ponownie na tym koncie, potem gra."),
            {}, settings_.steam_account_switch});
        add({Id::steam_install_dialog, Control::toggle, tr(L"Instalacja Steam w launcherze"),
            tr(L"Biblioteka gry wybierana w launcherze, z wolnym miejscem; Steam pobiera ją po ponownym uruchomieniu w tle."),
            {}, settings_.steam_install_dialog});
        Row integration{Id::aurelia, Control::toggle, tr(L"Rozszerzona integracja Aurelia"),
            tr(L"Osobny program do zarządzania grami Steam bez klienta Steam: instalacja, aktualizacje, zapisy w chmurze, DLC i Warsztat. Szczegóły na jego stronie."), {}, settings_.aurelia};
        integration.section = L"Aurelia"; add(std::move(integration));
        // Aurelia instead of the Steam client: the library comes from it too (after a restart, as the
        // library worker starts once), and every game installs, updates and starts through it.
        std::wstring replaces = tr(L"Konto, posiadane i zainstalowane gry, kolekcje, ukryte i czas gry z Aurelii; instalacja, aktualizacje i uruchamianie bez klienta Steam. Wyjątki ustawisz przy grze.");
        replaces += L"\n" + std::wstring(tr(L"Gry z Family Sharing na Windows wymagają klienta Steam. Biblioteka przełącza się po ponownym uruchomieniu Launchme."));
        if (settings_.aurelia && settings_.aurelia_default != aurelia_library_source_ && aurelia_steam_client_)
            replaces += L"\n" + std::wstring(tr(L"Wymaga ponownego uruchomienia Launchme"));
        Row managed{Id::aurelia_default, Control::toggle, tr(L"Aurelia jako zamiennik Steam"), std::move(replaces), {}, settings_.aurelia_default};
        managed.lines = 4;
        managed.locked = !settings_.aurelia; add(std::move(managed));
        Row cloud{Id::aurelia_cloud, Control::toggle, tr(L"Zapisy w chmurze"),
            tr(L"Stan i synchronizacja zapisów gier zarządzanych przez Aurelię."), {}, settings_.aurelia_cloud};
        cloud.locked = !settings_.aurelia; add(std::move(cloud));
        // Its version and who is signed in (or that nobody is).
        std::wstring state = aurelia_version_.empty() ? std::wstring(tr(L"Instalacja i konto.")) : aurelia_version_;
        if (!aurelia_version_.empty() && aurelia_authenticated_)
            state += L" · " + (aurelia_account_name_.empty() ? std::wstring(tr(L"Zalogowano")) : tr(L"Zalogowano: ") + aurelia_account_name_);
        else if (!aurelia_version_.empty() && aurelia_session_known_) state += L" · " + std::wstring(tr(L"Nie zalogowano"));
        add({Id::aurelia_open, Control::action, tr(L"Zarządzaj Aurelią"), std::move(state), tr(L"Otwórz")});
        return;
    }
    if (source == GameSource::gog || source == GameSource::epic) {
        const bool is_gog = source == GameSource::gog;
        const bool busy = is_gog ? working(gog_state_.state) : working(epic_state_.state);
        const bool connected = is_gog ? !gog_state_.account.empty() : !epic_state_.account.empty();
        const bool callback = is_gog ? gog_state_.state == gog::State::signing_in : epic_state_.state == epic::State::signing_in;
        add({Id::pc_connect, Control::action, tr(L"Konto"), account(source), busy ? tr(L"Anuluj") : connected ? tr(L"Zmień konto") : tr(L"Połącz")});
        if (callback) add({is_gog ? Id::gog_callback : Id::epic_callback, Control::secret,
            is_gog ? tr(L"Adres powrotu") : tr(L"Kod logowania"),
            is_gog ? tr(L"Wklej tutaj cały adres z przeglądarki po logowaniu.") : tr(L"Wklej authorizationCode lub całą odpowiedź JSON po logowaniu."),
            is_gog ? tr(L"Wklej adres") : tr(L"Wklej kod")});
        const auto sync = syncedAt(is_gog ? gog_state_.synced_at : epic_state_.synced_at);
        add({connected ? (is_gog ? Id::gog_refresh : Id::epic_refresh) : Id::pc_refresh, Control::action, tr(L"Biblioteka"),
            library(source) + (sync.empty() ? L"" : L" · " + sync), tr(L"Odśwież")});
        const auto prefix = std::string(sourceSlug(source)) + ':';
        const auto excluded = std::count_if(library_state_.excluded_games.begin(), library_state_.excluded_games.end(),
            [&](const auto& entry) { return entry.first.starts_with(prefix); });
        add({Id::pc_excluded, Control::action, tr(L"Wykluczone gry"),
            tr(L"Niewidoczne także w wyszukiwaniu i pod Ctrl+H.") + std::wstring(L" · ") + std::to_wstring(excluded), tr(L"Zarządzaj")});
        if (connected) add({is_gog ? Id::gog_logout : Id::epic_logout, Control::action, tr(L"Połączenie konta"),
            tr(L"Usuń lokalną sesję i bibliotekę kontową."), tr(L"Wyloguj")});
        return;
    }
    if (source == GameSource::amazon) {
        // Nile first: downloaded only when asked, needed for the account, downloads and updates.
        const auto& state = amazon_state_;
        const bool busy = working(state.state), connected = !state.account.empty();
        Row helper{Id::amazon_helper, Control::action, L"Nile",
            state.downloading ? amazon::statusText(state) : state.helper ?
                tr(L"Pomocnik Amazon Games, wersja ") + std::wstring(amazon::helper_version) :
                std::wstring(tr(L"Pomocnik do logowania i pobierania gier Amazon (open source, około 12 MB).")),
            state.helper ? std::wstring(tr(L"Usuń")) : std::wstring(tr(L"Pobierz"))};
        helper.locked = state.downloading || (state.helper && amazon_service_ && amazon_service_->busy()); add(std::move(helper));
        add({Id::pc_connect, Control::action, tr(L"Konto"), account(source), busy ? tr(L"Anuluj") : connected ? tr(L"Zmień konto") : tr(L"Połącz")});
        if (state.state == amazon::State::signing_in) add({Id::amazon_callback, Control::secret, tr(L"Adres po logowaniu"),
            tr(L"Zaloguj się na stronie Amazon, potem wklej tutaj cały adres z paska przeglądarki."), tr(L"Wklej adres")});
        const auto sync = syncedAt(state.synced_at);
        add({connected ? Id::amazon_refresh : Id::pc_refresh, Control::action, tr(L"Biblioteka"), library(source) + (sync.empty() ? L"" : L" · " + sync), tr(L"Odśwież")});
        const auto prefix = std::string(sourceSlug(source)) + ':';
        const auto excluded = std::count_if(library_state_.excluded_games.begin(), library_state_.excluded_games.end(),
            [&](const auto& entry) { return entry.first.starts_with(prefix); });
        add({Id::pc_excluded, Control::action, tr(L"Wykluczone gry"),
            tr(L"Niewidoczne także w wyszukiwaniu i pod Ctrl+H.") + std::wstring(L" · ") + std::to_wstring(excluded), tr(L"Zarządzaj")});
        if (connected) add({Id::amazon_logout, Control::action, tr(L"Połączenie konta"), tr(L"Usuń lokalną sesję i bibliotekę kontową."), tr(L"Wyloguj")});
        return;
    }
    if (source == GameSource::xbox) {
        // The Xbox app's installs need nothing; the account (and Launchme's own installs) need the
        // Xodus-based helper, downloaded only when asked.
        const auto& state = xbox_state_;
        const bool busy = xboxWorking(state.state), connected = !state.account.empty();
        Row helper{Id::xbox_helper, Control::action, tr(L"Pomocnik Xbox"),
            state.downloading ? xbox_account::statusText(state) : state.helper ?
                tr(L"Konto Xbox przez Xodus, wersja ") + std::wstring(xbox_account::helper_version) :
                std::wstring(tr(L"Logowanie, biblioteka konta i Game Pass (open source, około 12 MB).")),
            state.helper ? std::wstring(tr(L"Usuń")) : std::wstring(tr(L"Pobierz"))};
        helper.locked = state.downloading || (state.helper && xbox_service_ && xbox_service_->busy()); add(std::move(helper));
        add({Id::pc_connect, Control::action, tr(L"Konto"), xbox_account::statusText(state), busy ? tr(L"Anuluj") : connected ? tr(L"Zmień konto") : tr(L"Połącz")});
        const auto sync = syncedAt(state.synced_at);
        add({connected ? Id::xbox_refresh : Id::pc_refresh, Control::action, tr(L"Biblioteka"), library(source) + (sync.empty() ? L"" : L" · " + sync), tr(L"Odśwież")});
        Row own{Id::xbox_own_install, Control::toggle, tr(L"Instalacja przez Launchme (eksperymentalne)"),
            tr(L"Gry GDK pobiera pomocnik Xodus do wybranego folderu, bez aplikacji Xbox. Gra może nie wystartować bez usług Gaming Services."),
            {}, settings_.xbox_own_install};
        own.locked = !connected; add(std::move(own));
        const auto prefix = std::string(sourceSlug(source)) + ':';
        const auto excluded = std::count_if(library_state_.excluded_games.begin(), library_state_.excluded_games.end(),
            [&](const auto& entry) { return entry.first.starts_with(prefix); });
        add({Id::pc_excluded, Control::action, tr(L"Wykluczone gry"),
            tr(L"Niewidoczne także w wyszukiwaniu i pod Ctrl+H.") + std::wstring(L" · ") + std::to_wstring(excluded), tr(L"Zarządzaj")});
        if (connected) add({Id::xbox_logout, Control::action, tr(L"Połączenie konta"), tr(L"Usuń lokalną sesję i bibliotekę kontową."), tr(L"Wyloguj")});
    } else if (source == GameSource::ea) {
        add({Id::pc_connect, Control::action, tr(L"Konto"), account(source), ea_import_.busy ? tr(L"Importowanie…") :
            ea_import_.awaiting ? tr(L"Anuluj") : ea_import_.snapshot ? tr(L"Zmień konto") : tr(L"Połącz")});
        if (ea_import_.awaiting) add({Id::ea_callback, Control::secret, tr(L"Dane logowania"),
            tr(L"Zaloguj się na ea.com, odśwież kartę z access_token, skopiuj całą stronę i wklej tutaj."), tr(L"Wklej odpowiedź")});
        add({ea_import_.snapshot ? Id::ea_refresh : Id::pc_refresh, Control::action, tr(L"Biblioteka"),
            ea_import_.snapshot ? syncedAt(ea_import_.snapshot->synced_at) : library(source), tr(L"Odśwież")});
        if (ea_import_.snapshot) add({Id::ea_remove, Control::action, tr(L"Zapisana biblioteka"), tr(L"Usuń zapisaną bibliotekę konta."), tr(L"Usuń")});
    } else if (source == GameSource::battlenet) {
        const auto& state = battlenet_import_;
        const auto description = state.snapshot ? tr(L"Zaimportowano: ") + std::to_wstring(state.snapshot->games.size()) +
            (state.snapshot->synced_at ? L" · " + syncedAt(state.snapshot->synced_at) : std::wstring{}) : std::wstring(tr(L"Nie zaimportowano"));
        add({Id::battlenet_import, Control::action, tr(L"Gry konta"), description,
            state.busy ? tr(L"Importowanie…") : state.awaiting ? tr(L"Anuluj") : state.snapshot ? tr(L"Odśwież") : tr(L"Importuj")});
        if (state.awaiting) add({Id::battlenet_callback, Control::secret, tr(L"Lista gier"),
            tr(L"Po zalogowaniu odśwież kartę games-and-subs, skopiuj całą stronę i wklej tutaj."), tr(L"Wklej listę")});
        add({Id::pc_refresh, Control::action, tr(L"Zainstalowane gry"), library(source), tr(L"Odśwież")});
        if (state.snapshot) add({Id::battlenet_remove, Control::action, tr(L"Zapisana biblioteka"), tr(L"Usuń zaimportowaną listę gier."), tr(L"Usuń")});
    } else if (source == GameSource::multimc) {
        // Every instance is a game; MultiMC itself is portable, so it can be pointed at by hand.
        add({Id::pc_refresh, Control::action, tr(L"Instancje"), library(source), tr(L"Odśwież")});
        std::wstring found;
        for (const auto& client : pc_clients_) if (client.source == source && !client.executable.empty()) found = client.executable.wstring();
        auto description = settings_.multimc_path.empty() ? (found.empty() ? std::wstring(tr(L"Nie znaleziono Prism Launcher ani MultiMC")) :
            tr(L"Znaleziono: ") + found) : settings_.multimc_path.wstring();
        add({Id::multimc_path, Control::action, tr(L"Launcher"), std::move(description),
             settings_.multimc_path.empty() ? tr(L"Wskaż…") : tr(L"Wyczyść")});
    } else {
        add({Id::pc_refresh, Control::action, tr(L"Biblioteka"), library(source), tr(L"Odśwież")});
    }
    const bool client = std::any_of(pc_clients_.begin(), pc_clients_.end(), [&](const auto& entry) { return entry.source == source && !entry.executable.empty(); });
    add({Id::pc_client, Control::action, tr(L"Klient"), {}, client ? tr(L"Otwórz klienta") : tr(L"Pobierz klienta")});
}

// The overview's order: the DRM-free folders, Steam, then the stores by their (translated) names
// (Emulacja's tile sits between the first two rows, outside this list).
std::vector<GameSource> Launcher::platformOrder() const {
    std::vector<GameSource> stores(pc_sources.begin(), pc_sources.end());
    std::sort(stores.begin(), stores.end(), [](GameSource a, GameSource b) {
        return CompareStringEx(LOCALE_NAME_USER_DEFAULT, LINGUISTIC_IGNORECASE, sourceName(a), -1, sourceName(b), -1, nullptr, nullptr, 0) == CSTR_LESS_THAN;
    });
    stores.insert(stores.begin(), {GameSource::drmfree, GameSource::steam});
    return stores;
}

void Launcher::openPlatformSettings(GameSource source) {
    const auto order = platformOrder();
    const auto found = std::find(order.begin(), order.end(), source);
    if (found == order.end()) return;
    const int index = static_cast<int>(found - order.begin()) + (source == GameSource::drmfree ? 0 : 1); // Below Emulacja's tile.
    if (settings_state_.category != static_cast<int>(settings_ui::Category::library))
        settings_state_.selectCategory(static_cast<int>(settings_ui::Category::library));
    settings_state_.closeAll(); // Another platform's page replaces this one; Back leads to the overview.
    drm_confirm_.clear();
    settings_ui::Row opener{Id::pc_platform, Control::platform};
    opener.source = source;
    settings_state_.open(Page::platform, {}, static_cast<int>(source), RowRef::of(opener));
    // Overview rows in platformOrder (if the row is not found again).
    settings_state_.pages.back().parent_row = index;
    clearSecretInput(); settings_motion_.valid = false; settingsPreviewFocus(); buildSettingsRows(); audio_.context();
}

bool Launcher::pcSettingsActivate(const Row& row) {
    if (drmSettingsActivate(row)) return true;
    if (row.id == Id::pc_platform) {
        if (settings_state_.editing) openPlatformSettings(row.source);
        else if (row.source == GameSource::steam) {
            // Steam can be switched off like any store: its games, collections and friends leave the launcher.
            settings_.steam_enabled = !settings_.steam_enabled;
            settingsChanged(true); rebuildCards(true); audio_.context();
        } else {
            const auto key = sourceSlug(row.source);
            const bool enabled = !settings_.enabled_pc_sources.erase(key);
            if (enabled) settings_.enabled_pc_sources.insert(key);
            settingsChanged(true); audio_.context();
            // Amazon Games needs Nile: switching it on offers the download (its page, the button first);
            // going back is a normal answer, the button stays there.
            if (enabled && row.source == GameSource::amazon && !amazon_state_.helper && amazon_state_.account.empty()) {
                openPlatformSettings(GameSource::amazon);
                settings_state_.selected[settings_state_.category] = 0; settings_state_.options = true; ensureSettingVisible();
            }
        }
    } else if (row.id == Id::pc_connect) {
        if (row.source == GameSource::gog) gogCommand(working(gog_state_.state) ? gog::Command::cancel : gog::Command::connect);
        else if (row.source == GameSource::epic) epicCommand(working(epic_state_.state) ? epic::Command::cancel : epic::Command::connect);
        else if (row.source == GameSource::amazon) amazonCommand(working(amazon_state_.state) ? amazon::Command::cancel : amazon::Command::connect);
        else if (row.source == GameSource::xbox) xboxCommand(xboxWorking(xbox_state_.state) ? xbox_account::Command::cancel : xbox_account::Command::connect);
        else if (row.source == GameSource::ea) { if (ea_import_.awaiting) cancelWebImport(row.source); else startWebImport(row.source); }
    } else if (row.id == Id::pc_excluded) openExcludedGames(row.source);
    else if (row.id == Id::pc_refresh) refreshPcLibraries();
    else if (row.id == Id::multimc_path) {
        if (!settings_.multimc_path.empty()) { setMultimcPath({}); return true; }
        file_browser::Request request;
        request.mode = file_browser::Mode::file;
        request.title = tr(L"Wskaż Prism Launcher lub MultiMC");
        request.extensions = {L".exe"};
        request.filter_label = tr(L"Programy (.exe)");
        openFileBrowser(std::move(request), [this](const std::filesystem::path& picked) { setMultimcPath(picked); });
    }
    else if (row.id == Id::pc_client) {
        const auto action = connectPcClient(row.source, pc_clients_);
        if (execute_(hwnd_, action) <= 32) showNotification(NotificationKind::error, tr(L"Nie można otworzyć klienta"), sourceName(row.source));
        else if (!action.target.starts_with(L"https://")) { settings_.enabled_pc_sources.insert(sourceSlug(row.source)); settingsChanged(true); }
    }
    // Store accounts: sync and sign-out, the website imports.
    else if (row.id == Id::gog_refresh) gogCommand(gog::Command::sync);
    else if (row.id == Id::gog_logout) gogCommand(gog::Command::logout);
    else if (row.id == Id::epic_refresh) epicCommand(epic::Command::sync);
    else if (row.id == Id::epic_logout) epicCommand(epic::Command::logout);
    else if (row.id == Id::amazon_helper) {
        // Nile on request only: "Pobierz" fetches it, "Usuń" removes the file again (the account stays signed in).
        if (amazon_state_.downloading) return true;
        if (!amazon_state_.helper) amazonCommand(amazon::Command::download);
        else if (!amazon_service_ || !amazon_service_->busy()) {
            std::error_code ec; std::filesystem::remove(amazon::helperPath(userConfigDirectory()), ec);
            amazon_state_.helper = amazon::helperInstalled(userConfigDirectory());
            buildSettingsRows(); audio_.context();
        }
    }
    else if (row.id == Id::amazon_refresh) amazonCommand(amazon::Command::sync);
    else if (row.id == Id::xbox_helper) {
        // The helper on request only: "Pobierz" fetches it, "Usuń" removes the file (the account stays signed in).
        if (xbox_state_.downloading) return true;
        if (!xbox_state_.helper) xboxCommand(xbox_account::Command::download);
        else if (!xbox_service_ || !xbox_service_->busy()) {
            std::error_code ec; std::filesystem::remove(xbox_account::helperPath(userConfigDirectory()), ec);
            xbox_state_.helper = xbox_account::helperInstalled(userConfigDirectory());
            buildSettingsRows(); audio_.context();
        }
    }
    else if (row.id == Id::xbox_refresh) xboxCommand(xbox_account::Command::sync);
    else if (row.id == Id::xbox_logout) xboxCommand(xbox_account::Command::logout);
    else if (row.id == Id::xbox_own_install) {
        if (row.locked) { navigationBoundary(); return true; }
        // Read when Instaluj is chosen: the shelves only need their actions recomputed.
        settings_.xbox_own_install = !settings_.xbox_own_install;
        settingsChanged(); buildSettingsRows(); audio_.context();
    }
    else if (row.id == Id::amazon_logout) amazonCommand(amazon::Command::logout);
    else if (row.id == Id::ea_refresh) startWebImport(GameSource::ea);
    else if (row.id == Id::ea_remove) clearWebImport(GameSource::ea);
    else if (row.id == Id::battlenet_import) {
        if (battlenet_import_.awaiting) cancelWebImport(GameSource::battlenet); else startWebImport(GameSource::battlenet);
    }
    else if (row.id == Id::battlenet_remove) clearWebImport(GameSource::battlenet);
    // Sign-in codes and pasted store pages: an empty field opens, the second Enter submits it.
    else if (row.id == Id::gog_callback || row.id == Id::epic_callback || row.id == Id::ea_callback || row.id == Id::battlenet_callback ||
             row.id == Id::amazon_callback) {
        if (settings_state_.editing) {
            if (row.id == Id::gog_callback) gogCommand(gog::Command::complete);
            else if (row.id == Id::epic_callback) epicCommand(epic::Command::complete);
            else if (row.id == Id::amazon_callback) amazonCommand(amazon::Command::complete);
            else submitWebImport(row.id == Id::ea_callback ? GameSource::ea : GameSource::battlenet);
            return true;
        }
        clearSecretInput();
        settings_secret_selected_ = true;
        settings_state_.editing = true;
        audio_.context();
        InvalidateRect(hwnd_, nullptr, FALSE);
    } else return false;
    return true;
}

// MultiMC / Prism picked by hand: only one of their EXEs is accepted; the scan starts at once and
// the source is switched on (the user asked for its instances).
void Launcher::setMultimcPath(const std::filesystem::path& picked) {
    if (!picked.empty() && !multimc::flavorOf(picked)) {
        showNotification(NotificationKind::error, tr(L"To nie jest Prism Launcher ani MultiMC"), picked.wstring());
        return;
    }
    settings_.multimc_path = picked;
    if (!picked.empty()) settings_.enabled_pc_sources.insert(sourceSlug(GameSource::multimc));
    settingsChanged(true);
    refreshPcLibraries();
    if (settingsOpen()) buildSettingsRows();
    audio_.context();
}

void Launcher::cancelPlatformSignIn(GameSource source) {
    if (source == GameSource::gog && signing(gog_state_.state)) gogCommand(gog::Command::cancel);
    else if (source == GameSource::epic && signing(epic_state_.state)) epicCommand(epic::Command::cancel);
    else if (source == GameSource::amazon && signing(amazon_state_.state)) amazonCommand(amazon::Command::cancel);
    else if (source == GameSource::xbox && xboxSigning(xbox_state_.state)) xboxCommand(xbox_account::Command::cancel);
    else if ((source == GameSource::ea || source == GameSource::battlenet) && webImport(source).awaiting) cancelWebImport(source);
}
