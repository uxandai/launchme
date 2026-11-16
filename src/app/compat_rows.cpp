// Linux: the rows of Settings -> Biblioteka -> Proton i Wine: tools, sources, store launchers,
// prefixes, one prefix and the options of every game or one game (an accordion of sections).
#include "launcher.hpp"

#ifndef _WIN32
#include "compat_internal.hpp"
#include "../artwork/cover_cache.hpp"
#include "../library/shortcuts.hpp"
#include "../library/steam_accounts.hpp"
#include "../library/xdg_paths.hpp"
#include "../model/launch_options_catalog.hpp"
#include "../platform/command_line.hpp"
#include "../platform/process_spawn.hpp"
#include "../platform/system.hpp"
#include "../platform/text_encoding.hpp"
#include "../platform/unicode_text.hpp"
#include "../platform/updater.hpp"
#include "../platform/wine_runner.hpp"

#include <functional>

#include <sys/wait.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <ctime>
#include <cwchar>
#include <cwctype>
#include <fstream>

using namespace settings_ui;
using namespace compat_detail;

namespace {
std::wstring sizeText(uint64_t bytes) {
    const wchar_t* units[] = {L"B", L"KB", L"MB", L"GB", L"TB"};
    double value = static_cast<double>(bytes);
    int unit = 0;
    while (value >= 1024 && unit < 4) { value /= 1024; ++unit; }
    wchar_t text[32]{};
    if (unit == 0) swprintf_s(text, L"%llu %ls", static_cast<unsigned long long>(bytes), units[0]);
    else swprintf_s(text, value < 10 ? L"%.1f %ls" : L"%.0f %ls", value, units[unit]);
    return text;
}

constexpr std::pair<const char*, const wchar_t*> wine_programs[] = {
    {"winecfg", trKey(L"Konfiguracja Wine (winecfg)")},
    {"regedit", trKey(L"Edytor rejestru (regedit)")},
    {"control", trKey(L"Panel sterowania")},
    {"taskmgr", trKey(L"Menedżer zadań")},
    {"explorer", trKey(L"Eksplorator plików")},
    {"uninstaller", trKey(L"Odinstaluj programy")},
};
// A WINEDLLOVERRIDES entry's modes ("": disabled).
constexpr std::pair<const char*, const wchar_t*> dll_modes[] = {
    {"n,b", trKey(L"Najpierw z gry (n,b)")},
    {"b,n", trKey(L"Najpierw z Wine (b,n)")},
    {"n", trKey(L"Tylko z gry (n)")},
    {"b", trKey(L"Tylko z Wine (b)")},
    {"", trKey(L"Wyłączona")},
};
} // namespace

// ---- Rows ------------------------------------------------------------------------------------------

void Launcher::compatSettingsRows() {
    // The category's own list is the overview (Page::compat); its pages open from it.
    const PageFrame overview{Page::compat};
    const auto* frame = settings_state_.top() ? settings_state_.top() : &overview;
    if (!compat_.tools_loaded) compatLoadTools();
    if (frame->page == Page::steam_game) { steamGameRows(); return; } // A Steam game's own (steam_game.cpp).
    std::wstring next_section;
    const auto section = [&](std::wstring title) { next_section = std::move(title); };
    const auto add = [&](Row row) -> Row& {
        if (!next_section.empty()) { row.section = std::move(next_section); next_section.clear(); }
        settings_rows_.push_back(std::move(row));
        return settings_rows_.back();
    };
    const auto runners = compat_tools::runners(compat_.tools);
    // A store launcher already in the library (added by Sklepy i launchery, by its name).
    const auto installedStore = [&](const StoreInstaller& store) {
        return std::any_of(custom_library_.begin(), custom_library_.end(), [&](const Game& game) { return game.name == store.name; });
    };
    // A choice of tool: "default" first (what it falls back to), then every Proton and Wine build.
    const auto toolChoices = [&](Row& row, const std::string& current, std::wstring fallback) {
        row.choices.push_back({std::move(fallback), 0, false, L"default"});
        row.choice = 0;
        for (const auto& tool : runners) {
            row.choices.push_back({tool.name + L" · " + label(compat_tools::originName(tool.origin)), 0, false, wide(tool.id)});
            if (tool.id == current) row.choice = static_cast<int>(row.choices.size()) - 1;
        }
        if (chosen(current) && row.choice == 0) { // Chosen, but gone: still shown as chosen.
            row.choices.push_back({compatToolName(current), 0, false, wide(current)});
            row.choice = static_cast<int>(row.choices.size()) - 1;
        }
        row.value = row.choices[static_cast<size_t>(row.choice)].label;
    };
    switch (frame->page) {
    case Page::compat: {
        section(tr(L"Gry"));
        {
            Row row{Id::compat_install_game, Control::action, tr(L"Dodaj grę z instalatora"),
                compat_.installer ? tr(L"Instalowanie: ") + compat_.installer->name + L" · " + compat_.installer->prefix.wstring() :
                                    std::wstring(tr(L"Instalator Windows (.exe, .msi) w nowym prefiksie; potem wskazujesz plik gry.")),
                compat_.installer ? tr(L"Instalowanie…") : tr(L"Wybierz…")};
            row.locked = compat_.installer.has_value();
            add(std::move(row));
        }
        add({Id::compat_stores_open, Control::count, tr(L"Sklepy i launchery"),
             tr(L"Battle.net, EA app, Epic, Ubisoft Connect, Rockstar i GOG Galaxy: każdy w swoim prefiksie, jednym kliknięciem."),
             std::to_wstring(std::count_if(std::begin(store_installers), std::end(store_installers),
                                           [&](const StoreInstaller& store) { return installedStore(store); }))});
        section(tr(L"Domyślnie"));
        {
            Row row{Id::compat_default_tool, Control::choice, tr(L"Wersja Protona / Wine"),
                tr(L"Dla gier, które nie wybrały własnej, i prefiksów bez własnej wersji.")};
            toolChoices(row, store::defaultTool(), compatToolName("default"));
            add(std::move(row));
        }
        {
            const auto defaults = store::defaults();
            int changed = 0;
            for (const auto& option : catalog::options())
                if (std::string_view(option.id) != "compat_tool" && catalog::value(defaults, option.id) != catalog::value(LaunchOptions{}, option.id)) ++changed;
            Row row{Id::compat_defaults, Control::page, tr(L"Domyślne opcje uruchamiania"),
                tr(L"Synchronizacja, nakładki, gamescope, zmienne… dla gier bez własnych ustawień."),
                changed ? tr(L"Zmienione: ") + std::to_wstring(changed) : std::wstring(tr(L"Bez zmian"))};
            row.page = Page::launch_options;
            add(std::move(row));
        }
        add({Id::compat_prefixes, Control::count, tr(L"Prefiksy"), tr(L"Nazwane prefiksy i prefiksy gier: wersja, rozmiar, winecfg, winetricks."),
             compat_.tools_loaded ? std::to_wstring(compat_.prefixes.size()) : std::wstring(L"…")});
        add({Id::compat_prefix_root, Control::action, tr(L"Folder prefiksów"), store::prefixRoot().wstring(), tr(L"Zmień")});
        {
            Row umu{Id::compat_umu, Control::action, L"umu-launcher",
                compat_.umu_run.empty() ? std::wstring(tr(L"Proton w Steam Linux Runtime z poprawkami protonfixes, jak w Steamie. Zalecany.")) :
                                          tr(L"Zainstalowany: ") + compat_.umu_run.wstring(),
                compat_.umu_run.empty() ? tr(L"Pobierz") : tr(L"Wersje")};
            add(std::move(umu));
        }
        section(tr(L"Zainstalowane"));
        if (!compat_.tools_loaded) add({Id::compat_status, Control::action, tr(L"Szukanie Protona i Wine…"), {}, {}, false}).locked = true;
        else if (runners.empty())
            add({Id::compat_status, Control::action, tr(L"Brak Protona i Wine"), tr(L"Pobierz GE-Proton poniżej: najlepszy wybór dla większości gier."), {}}).locked = true;
        const auto global = store::defaultTool();
        for (const auto& tool : runners) {
            std::wstring description = wide(tool.family);
            if (!tool.version.empty() && wide(tool.version) != tool.name && tool.origin != compat_tools::Origin::system) description += L" · " + wide(tool.version);
            description += L" · " + label(compat_tools::originName(tool.origin));
            if (tool.id == global) description += L" · " + std::wstring(tr(L"domyślna"));
            if (tool.deprecated) description += L" · " + std::wstring(tr(L"przestarzała"));
            Row row{Id::compat_tool, Control::choice, tool.name, std::move(description), tr(L"Opcje")};
            row.key = tool.id;
            row.choice = -1;
            row.choices.push_back({tr(L"Ustaw jako domyślną"), 0, false, L"default"});
            if (!tool.path.empty()) row.choices.push_back({tr(L"Otwórz folder"), 0, false, L"folder"});
            if (tool.removable) row.choices.push_back({compat_.confirm == "tool:" + tool.id ? tr(L"Potwierdź usunięcie") : tr(L"Usuń"), 0, false, L"remove"});
            add(std::move(row));
        }
        bool components = false;
        for (const auto& tool : compat_.tools) {
            if (tool.kind != compat_tools::Kind::component) continue;
            if (!components) { section(tr(L"Komponenty dla Wine")); components = true; }
            Row row{Id::compat_tool, Control::choice, tool.name, wide(tool.family) + L" · " + label(compat_tools::originName(tool.origin)), tr(L"Opcje")};
            row.key = tool.id;
            row.choice = -1;
            if (!tool.path.empty()) row.choices.push_back({tr(L"Otwórz folder"), 0, false, L"folder"});
            if (tool.removable) row.choices.push_back({compat_.confirm == "tool:" + tool.id ? tr(L"Potwierdź usunięcie") : tr(L"Usuń"), 0, false, L"remove"});
            if (!row.choices.empty()) add(std::move(row));
        }
        section(tr(L"Pobierz"));
        for (const auto& source : compat_tools::sources()) {
            const bool runner = source.kind == compat_tools::Kind::proton || source.kind == compat_tools::Kind::wine;
            // Its builds wherever they came from (Heroic, Steam, Launchme), not the system's Wine or umu's own.
            const auto count = std::count_if(compat_.tools.begin(), compat_.tools.end(), [&](const auto& tool) {
                return tool.family == source.family && tool.origin != compat_tools::Origin::system && tool.origin != compat_tools::Origin::umu;
            });
            std::wstring description = source.kind == compat_tools::Kind::proton ? tr(L"Proton do gier spoza Steama") :
                                       source.kind == compat_tools::Kind::wine ? tr(L"Wine bez Steam Linux Runtime") :
                                       source.kind == compat_tools::Kind::umu ? tr(L"Uruchamianie Protona jak w Steamie") :
                                       tr(L"Komponent kopiowany do prefiksów Wine");
            description += L" · github.com/" + wide(source.repository);
            Row row{Id::compat_source, Control::count, source.name, std::move(description), runner || count ? std::to_wstring(count) : std::wstring{}};
            row.key = source.id;
            add(std::move(row));
        }
        break;
    }
    case Page::compat_source: {
        const auto* source = compat_tools::findSource(frame->key);
        if (!source) break;
        const auto found = compat_.releases.find(source->id);
        if (found == compat_.releases.end() && !compat_.releases_loading.contains(source->id)) compatLoadReleases(source->id, false);
        if (std::string_view(source->id) == "ge-proton")
            add({Id::compat_auto_update, Control::toggle, tr(L"Aktualizuj automatycznie"),
                 tr(L"Raz dziennie pobiera nowe wydanie, gdy używasz GE-Protona. Gry z wersją automatyczną od razu go używają, wybrane wersje zostają."),
                 {}, settings_.compat_auto_update});
        if (source->kind == compat_tools::Kind::proton)
            add({Id::compat_steam_target, Control::toggle, tr(L"Instaluj także dla Steama"),
                 tr(L"Do compatibilitytools.d Steama: ta wersja będzie też do wyboru we właściwościach gier Steam."), {}, compat_.steam_target});
        if (found == compat_.releases.end()) {
            add({Id::compat_status, Control::action, tr(L"Ładowanie listy wydań…"), L"github.com/" + wide(source->repository), {}}).locked = true;
            break;
        }
        const auto& list = found->second;
        if (!list.available) {
            add({Id::compat_source_refresh, Control::action, tr(L"Brak połączenia z GitHubem"), tr(L"Lista wydań jest niedostępna. Sprawdź połączenie."), tr(L"Ponów")});
            break;
        }
        section(list.stale ? tr(L"Wydania (zapisana lista: GitHub nie odpowiedział)") : tr(L"Wydania"));
        for (const auto& release : list.releases) {
            const bool installed = compat_tools::installed(release, compat_.tools);
            const bool active = compat_.install && compat_.install->source == release.source && compat_.install->tag == release.tag;
            std::wstring description = wide(release.published);
            if (release.size) description += L" · " + sizeText(release.size);
            if (release.digest == compat_tools::Digest::none) description += L" · " + std::wstring(tr(L"bez sumy kontrolnej"));
            if (active) {
                const auto& progress = compat_.install->progress;
                const auto phase = progress.phase == compat_tools::Progress::Phase::download ? tr(L"Pobieranie") :
                                   progress.phase == compat_tools::Progress::Phase::verify ? tr(L"Sprawdzanie") : tr(L"Rozpakowywanie");
                description = phase + (progress.total ? L" " + std::to_wstring(progress.done * 100 / progress.total) + L"% · " +
                                                        sizeText(progress.done) + L" / " + sizeText(progress.total) : std::wstring(L"…"));
            }
            Row row{Id::compat_release, Control::action, release.name, std::move(description),
                active ? tr(L"Anuluj") : installed ? tr(L"Zainstalowana") : tr(L"Pobierz")};
            row.key = release.tag;
            row.locked = installed || (compat_.install && !active);
            add(std::move(row));
        }
        if (!compat_.status.empty()) add({Id::compat_status, Control::action, tr(L"Ostatnia instalacja"), compat_.status, {}}).locked = true;
        add({Id::compat_source_refresh, Control::action, tr(L"Lista wydań"), tr(L"Zapisana na godzinę; odśwież, aby zobaczyć nowsze."), tr(L"Odśwież")});
        break;
    }
    case Page::compat_stores: {
        section(tr(L"Sklepy i launchery"));
        for (const auto& store : store_installers) {
            const bool running = compat_.installer && compat_.installer->store == store.id;
            const bool downloading = compat_.store_download == store.id;
            const bool there = installedStore(store);
            std::wstring description = tr(store.description);
            if (downloading) description = tr(L"Pobieranie instalatora…");
            else if (running) description = tr(L"Instalator działa w prefiksie: ") + compat_.installer->prefix.wstring();
            else if (there) description += L"\n" + std::wstring(tr(L"Jest w bibliotece (Gry dodane ręcznie)."));
            Row row{Id::compat_store_install, Control::action, store.name, std::move(description),
                    downloading || running ? tr(L"Instalowanie") : there ? tr(L"Zainstaluj ponownie") : tr(L"Zainstaluj")};
            row.key = store.id;
            row.locked = compat_.installer.has_value() || !compat_.store_download.empty();
            add(std::move(row));
        }
        add({Id::compat_status, Control::action, tr(L"Jak to działa"),
             tr(L"Oficjalny instalator działa w nowym prefiksie (najlepiej z GE-Proton lub Proton-CachyOS); po instalacji sklep trafia do biblioteki. Gry instalujesz już w nim."),
             {}}).locked = true;
        break;
    }
    case Page::prefixes: {
        add({Id::prefix_new, Control::text, tr(L"Nowy prefiks"),
             tr(L"Wspólny dysk C: dla kilku gier, np. Sklepy albo Ubisoft. Wybierasz go potem w opcjach gry."), tr(L"Utwórz…")});
        section(tr(L"Prefiksy"));
        if (!compat_.tools_loaded) add({Id::compat_status, Control::action, tr(L"Szukanie prefiksów…"), {}, {}}).locked = true;
        for (const auto& entry : compat_.prefixes) {
            std::wstring description = entry.path.wstring();
            description += L" · " + compatToolName(chosen(entry.info.compat_tool) ? entry.info.compat_tool : store::defaultTool());
            if (entry.shared) description += L" · " + std::wstring(tr(L"programy bez gry"));
            else if (entry.games.empty()) description += L" · " + std::wstring(tr(L"żadna gra go nie używa"));
            else description += L" · " + (tr(L"Gry: ") + std::to_wstring(entry.games.size()));
            const auto size = compat_.sizes.find(entry.path);
            std::error_code ec;
            const bool exists = std::filesystem::exists(entry.path, ec);
            if (exists && size == compat_.sizes.end()) compatMeasure(entry.path);
            Row row{Id::prefix_entry, Control::count, compatPrefixName(entry), std::move(description),
                !exists ? std::wstring(tr(L"Pusty")) : size == compat_.sizes.end() ? std::wstring(L"…") : sizeText(size->second)};
            row.key = keyOf(entry.path);
            add(std::move(row));
        }
        break;
    }
    case Page::prefix: {
        const auto path = pathOf(frame->key);
        const auto found = std::find_if(compat_.prefixes.begin(), compat_.prefixes.end(), [&](const auto& entry) { return entry.path == path.lexically_normal(); });
        store::PrefixEntry entry;
        if (found != compat_.prefixes.end()) entry = *found; else entry.path = path;
        Row name{Id::prefix_name, Control::text, tr(L"Nazwa"), tr(L"Jak prefiks nazywa się na liście i w opcjach gier."),
            entry.info.name.empty() ? compatPrefixName(entry) : entry.info.name};
        name.key = frame->key;
        add(std::move(name));
        {
            Row row{Id::prefix_tool, Control::choice, tr(L"Wersja Protona / Wine"),
                tr(L"Gry w tym prefiksie używają jej, gdy nie wybrały własnej. Zmiana głównej wersji (np. Wine 9 → 10) aktualizuje prefiks.")};
            toolChoices(row, entry.info.compat_tool, tr(L"Domyślna: ") + compatToolName(store::defaultTool()));
            if (const auto warning = runnerWarning(compat_.tools, store::effectiveTool({}, path), path); !warning.empty()) row.description = warning;
            row.key = frame->key;
            add(std::move(row));
        }
        std::error_code ec;
        const bool exists = wine_runner::prefixExists(path);
        add({Id::prefix_folder, Control::action, tr(L"Folder"), path.wstring(), tr(L"Otwórz")}).key = frame->key;
        {
            const auto size = compat_.sizes.find(path.lexically_normal());
            if (std::filesystem::exists(path, ec) && size == compat_.sizes.end()) compatMeasure(path);
            Row row{Id::prefix_size, Control::action, tr(L"Rozmiar"),
                compat_.measuring.contains(path.lexically_normal()) ? std::wstring(tr(L"Liczenie…")) :
                size == compat_.sizes.end() ? std::wstring(tr(L"Pusty")) : sizeText(size->second),
                tr(L"Przelicz")};
            row.key = frame->key;
            add(std::move(row));
        }
        {
            std::wstring games;
            for (const auto& key : entry.games) {
                const auto game = std::find_if(public_library_.begin(), public_library_.end(), [&](const Game& item) { return gameKey(item) == key; });
                games += (games.empty() ? L"" : L", ") + (game != public_library_.end() ? game->name : wide(key));
            }
            Row row{Id::prefix_games, Control::action, tr(L"Gry"),
                entry.shared ? std::wstring(tr(L"Programy uruchamiane bez gry.")) : games.empty() ? std::wstring(tr(L"Żadna gra go nie używa.")) : games, {}};
            row.locked = true;
            row.lines = games.size() > 120 ? 3 : 0;
            add(std::move(row));
        }
        section(tr(L"Narzędzia"));
        if (!exists) add({Id::prefix_create, Control::action, tr(L"Utwórz prefiks teraz"),
                         tr(L"Inaczej powstanie przy pierwszym uruchomieniu gry (umu, Proton i Wine robią to same)."), tr(L"Utwórz")}).key = frame->key;
        {
            Row row{Id::prefix_program, Control::choice, tr(L"Programy Wine"), tr(L"winecfg, regedit, panel sterowania i inne, w tym prefiksie."), tr(L"Uruchom…")};
            row.key = frame->key;
            row.choice = -1;
            for (const auto& [program, text] : wine_programs) row.choices.push_back({tr(text), 0, false, wide(program)});
            add(std::move(row));
        }
        {
            const auto launch = compatPrefixLaunch(path);
            Row row{Id::prefix_winetricks, Control::choice, L"Winetricks",
                wine_runner::winetricksAvailable(launch) ? std::wstring(tr(L"Biblioteki Microsoft i czcionki, których gra potrzebuje.")) :
                                                           std::wstring(tr(L"Zainstaluj winetricks (albo umu-launcher), aby tego użyć.")),
                tr(L"Zainstaluj…")};
            row.key = frame->key;
            row.choice = -1;
            row.locked = !wine_runner::winetricksAvailable(launch);
            row.choices.push_back({tr(L"Okno winetricks"), 0, false, {}});
            for (const auto& verb : catalog::winetricksVerbs()) row.choices.push_back({label(verb.label) + L" (" + wide(verb.verb) + L")", 0, false, wide(verb.verb)});
            add(std::move(row));
        }
        add({Id::prefix_run, Control::action, tr(L"Uruchom program w prefiksie"), tr(L"Instalator, łatka albo menedżer modów (.exe, .msi, .bat)."), tr(L"Wybierz…")}).key = frame->key;
        add({Id::prefix_kill, Control::action, tr(L"Zakończ procesy prefiksu"), tr(L"wineserver -k: gdy gra albo instalator się zawiesi."), tr(L"Zakończ")}).key = frame->key;
        {
            Row row{Id::prefix_delete, Control::action, tr(L"Usuń prefiks"),
                tr(L"Usuwa dysk C: prefiksu z zapisami gier, które trzymają je w nim. Gry dostaną nowy przy następnym uruchomieniu."),
                compat_.confirm == "prefix:" + frame->key ? tr(L"Potwierdź") : tr(L"Usuń")};
            row.key = frame->key;
            row.lines = 3;
            add(std::move(row));
        }
        break;
    }
    case Page::launch_options: {
        const std::string key = frame->key;
        const bool game = !key.empty();
        const auto stored = game ? store::gameOptions(key) : std::optional<LaunchOptions>{};
        const LaunchOptions options = game ? stored.value_or(gameDefaults()) : store::defaults();
        const LaunchOptions global = game ? store::defaults() : options;
        // A game's overrides: the badge "Ta gra" on each, a button back to the default.
        const auto overrides = game ? store::gameOverrides(key) : std::set<std::string>{};
        const auto own = [&](Row& row, const std::string& id) {
            if (!game || !overrides.contains(id)) return;
            row.badge = tr(L"Ta gra");
            row.reset = id;
        };
        if (game) {
            Row scope{Id::launch_scope, Control::action, stored ? tr(L"Własne ustawienia tej gry") : tr(L"Ustawienia domyślne"),
                stored ? std::wstring(tr(L"Opcje z plakietką „Ta gra” są jej własne (↺ wraca do domyślnej); reszta podąża za domyślnymi.")) :
                         std::wstring(tr(L"Gra używa ustawień domyślnych. Zmiana dowolnej opcji zapisze ją tylko dla tej gry.")),
                stored ? (compat_.confirm == "reset:" + key ? tr(L"Potwierdź") : tr(L"Przywróć wszystkie")) : tr(L"Edytuj domyślne")};
            add(std::move(scope));
            if (const auto rating = protonDbLabel(compat_.game); !rating.empty())
                add({Id::launch_protondb, Control::action, L"ProtonDB", rating + L"\n" + tr(L"Ocena graczy i Valve: jak gra działa przez Protona i na Steam Decku."),
                     tr(L"Otwórz")});
            if (launchEditable(compat_.game))
                add({Id::launch_edit_game, Control::action, tr(L"Plik gry i argumenty"), tr(L"Program, który się uruchamia, jego argumenty i folder."), tr(L"Edytuj")});
            // Protontricks for any game: winetricks with this game's Proton (or Wine), prefix and umu ID.
            if (const auto launch = store::compatLaunch(compat_.game)) {
                const bool available = wine_runner::winetricksAvailable(*launch);
                Row tricks{Id::launch_tricks, Control::choice, L"Protontricks",
                    available ? std::wstring(tr(L"Winetricks z Protonem i prefiksem tej gry: biblioteki Microsoft, czcionki, poprawki. Także dla gier spoza Steam.")) :
                                std::wstring(tr(L"Zainstaluj winetricks (albo umu-launcher), aby tego użyć.")),
                    tr(L"Wybierz…")};
                tricks.choice = -1;
                tricks.locked = !available;
                tricks.choices.push_back({tr(L"Okno winetricks"), 0, false, L"winetricks"});
                for (const auto& [program, text] : wine_programs) tricks.choices.push_back({tr(text), 0, false, wide(program)});
                for (const auto& verb : catalog::winetricksVerbs())
                    tricks.choices.push_back({L"winetricks " + wide(verb.verb) + L" · " + label(verb.label), 0, false, L"verb:" + wide(verb.verb)});
                add(std::move(tricks));
            }
            // The runner's output of the last start: what to look at (or send) when it does not start.
            std::filesystem::path log;
            if (const auto launch = store::compatLaunch(compat_.game))
                for (const auto& file : wine_runner::logFiles(*launch))
                    if (std::error_code ec; std::filesystem::is_regular_file(file, ec)) { log = file; break; }
            Row log_row{Id::launch_log, Control::action, tr(L"Dziennik ostatniego uruchomienia"),
                        tr(L"Wyjście Wine/Protona tej gry: przydaje się, gdy gra nie startuje."), log.empty() ? tr(L"Jeszcze nie uruchomiono") : tr(L"Otwórz")};
            log_row.locked = log.empty();
            log_row.key = text_encoding::utf8Lenient(log.wstring());
            add(std::move(log_row));
        }
        // One option's row (nullopt: shown elsewhere, or not for the default).
        const auto overrideId = [](const catalog::Option& option) {
            return option.variable ? "env:" + std::string(option.variable) : std::string(option.id);
        };
        const auto optionRow = [&](const catalog::Option& option) -> std::optional<Row> {
            const std::string_view id = option.id;
            if (id == "dll_overrides" || id == "environment") return std::nullopt; // Sections of their own.
            if (id == "prefix" && !game) return std::nullopt; // The default has no prefix: each game gets its own.
            Row row{Id::launch_option, Control::toggle, label(option.label), {}};
            row.key = option.id;
            row.dense = true;
            row.detail = wide(option.sets);
            if (id == "compat_tool") {
                row.control = Control::choice;
                row.description = label(option.description);
                std::wstring fallback;
                if (game) {
                    const auto prefix = options.prefix.empty() ? store::gamePrefix(compat_.game) : options.prefix;
                    const auto prefix_tool = store::prefixInfo(prefix).compat_tool;
                    fallback = chosen(prefix_tool) ? tr(L"Jak prefiks: ") + compatToolName(prefix_tool) :
                                                     tr(L"Domyślna: ") + compatToolName(store::defaultTool());
                    if (const auto warning = runnerWarning(compat_.tools, store::effectiveTool(gameKey(compat_.game), prefix), prefix); !warning.empty())
                        row.description = warning;
                } else fallback = compatToolName("default");
                toolChoices(row, options.compat_tool, std::move(fallback));
                row.detail = row.value;
                row.lines = 3;
            } else if (id == "prefix") {
                const auto mine = store::gamePrefix(compat_.game);
                row.control = Control::choice;
                row.description = options.prefix.empty() ? mine.wstring() : options.prefix.wstring();
                row.detail = row.description;
                row.choices.push_back({tr(L"Własny prefiks gry"), 0, false, {}});
                for (const auto& entry : compat_.prefixes) {
                    if (!entry.named && !entry.shared) continue;
                    row.choices.push_back({compatPrefixName(entry), 0, false, entry.path.wstring()});
                    if (!options.prefix.empty() && entry.path == options.prefix.lexically_normal()) row.choice = static_cast<int>(row.choices.size()) - 1;
                }
                if (!options.prefix.empty() && row.choice == 0) {
                    row.choices.push_back({options.prefix.filename().wstring(), 0, false, options.prefix.wstring()});
                    row.choice = static_cast<int>(row.choices.size()) - 1;
                }
                row.choices.push_back({tr(L"Inny folder…"), 0, false, L"browse"});
                row.value = row.choices[static_cast<size_t>(row.choice)].label;
            } else {
                const auto value = catalog::value(options, option.id);
                std::wstring description = label(option.description);
                if (option.runners == catalog::proton) description += L" " + std::wstring(tr(L"Tylko Proton."));
                else if (option.runners == catalog::wine) description += L" " + std::wstring(tr(L"Tylko zwykłe Wine."));
                row.description = description + L"\n" + wide(option.sets);
                row.lines = 3;
                switch (option.type) {
                case catalog::Type::toggle: row.checked = value == "1"; break;
                case catalog::Type::choice:
                    row.control = Control::choice;
                    for (size_t i = 0; i < option.choices.size(); ++i) {
                        row.choices.push_back({label(option.choices[i].label), 0, false, wide(option.choices[i].value)});
                        if (value == option.choices[i].value) row.choice = static_cast<int>(i);
                    }
                    row.value = row.choices.empty() ? std::wstring{} : row.choices[static_cast<size_t>(row.choice)].label;
                    break;
                case catalog::Type::number:
                    row.control = Control::text;
                    row.value = wide(value);
                    row.description = description + L" (" + std::to_wstring(option.minimum) + L"–" + std::to_wstring(option.maximum) + L")\n" + wide(option.sets);
                    break;
                default:
                    row.control = Control::text;
                    row.value = value.empty() ? std::wstring(tr(L"Brak")) : wide(value);
                    break;
                }
            }
            own(row, overrideId(option));
            return row;
        };
        // Environment variables: the switches above show some; each other one as a row of its own.
        const auto shownAsSwitch = [](const LaunchOptions::Variable& variable) {
            const auto* shown = catalog::forVariable(variable.name);
            return shown && (shown->type == catalog::Type::toggle ? variable.value == shown->variable_on :
                             std::any_of(shown->choices.begin(), shown->choices.end(), [&](const catalog::Choice& c) { return variable.value == c.value; }));
        };
        const auto variableRow = [&](const std::string& name, const std::string* value) {
            Row row{Id::launch_env, Control::text, wide(name), {}, value ? wide(*value) : std::wstring(tr(L"Wyłączona"))};
            row.key = name;
            row.dense = true;
            row.detail = value ? wide(name + "=" + *value) : std::wstring(tr(L"Z ustawień domyślnych, wyłączona w tej grze"));
            const bool mine = game && overrides.contains("env:" + name);
            row.description = !game ? std::wstring(tr(L"Każda gra, która nie ustawia jej sama. Puste pole ją usuwa.")) :
                              mine ? std::wstring(tr(L"Ustawiona dla tej gry. Puste pole ją usuwa.")) :
                                     std::wstring(tr(L"Z ustawień domyślnych. Zmiana albo puste pole (wyłącza) dotyczy tylko tej gry."));
            row.description += L"\n" + row.detail;
            row.lines = 3;
            own(row, "env:" + name);
            return row;
        };
        // WINEDLLOVERRIDES: one row per entry with its mode, the presets and a typed entry, then the whole text.
        const auto dllRows = [&](const std::function<void(Row)>& put) {
            for (const auto& entry : catalog::dllEntries(options.dll_overrides)) {
                Row row{Id::launch_dll, Control::choice, wide(entry.dlls), {}, {}};
                row.key = entry.dlls;
                row.dense = true;
                row.detail = wide(entry.dlls + "=" + entry.mode);
                const auto presets = catalog::dllPresets();
                const auto preset = std::find_if(presets.begin(), presets.end(), [&](const auto& item) { return entry.dlls == item.dlls; });
                row.description = (preset != presets.end() ? std::wstring(label(preset->label)) : std::wstring(tr(L"Biblioteka gry albo moda."))) + L"\n" + row.detail;
                row.choice = -1;
                for (const auto& [mode, text] : dll_modes) {
                    row.choices.push_back({tr(text), 0, false, wide(mode)});
                    if (entry.mode == mode) row.choice = static_cast<int>(row.choices.size()) - 1;
                }
                if (row.choice < 0) { row.choices.push_back({wide(entry.mode), 0, false, wide(entry.mode)}); row.choice = static_cast<int>(row.choices.size()) - 1; }
                row.value = row.choices[static_cast<size_t>(row.choice)].label;
                row.choices.push_back({tr(L"Usuń z listy"), 0, false, L"remove"});
                put(std::move(row));
            }
            Row presets{Id::launch_dll_preset, Control::choice, tr(L"Dodaj z listy"), tr(L"ReShade, ładowarki ASI, mody i poprawki: gotowe ustawienia."), tr(L"Wybierz…")};
            presets.choice = -1;
            for (const auto& preset : catalog::dllPresets())
                if (!catalog::presetSet(options.dll_overrides, preset))
                    presets.choices.push_back({label(preset.label) + L" (" + wide(preset.dlls) + L"=" + wide(preset.mode) + L")", 0, false, wide(preset.dlls)});
            if (!presets.choices.empty()) put(std::move(presets));
            put({Id::launch_dll_add, Control::text, tr(L"Dodaj ręcznie"), tr(L"nazwa=tryb, np. dinput8=n,b (n: z gry, b: z Wine, pusty tryb: wyłączona)."), tr(L"Dodaj…")});
            const auto* manual = catalog::find("dll_overrides");
            Row row{Id::launch_option, Control::text, tr(L"Całość (WINEDLLOVERRIDES)"), label(manual->description),
                options.dll_overrides.empty() ? std::wstring(tr(L"Brak")) : wide(options.dll_overrides)};
            row.key = "dll_overrides";
            row.dense = true;
            row.detail = L"WINEDLLOVERRIDES";
            own(row, "dll_overrides");
            put(std::move(row));
        };
        // Szukaj: the options whose name, description or variable has the text, from every section.
        {
            Row search{Id::launch_search, Control::text, tr(L"Szukaj opcji"),
                tr(L"Nazwa, opis albo zmienna (np. DXVK, HDR, MangoHud)."), compat_.query.empty() ? std::wstring(tr(L"Wpisz…")) : compat_.query};
            search.section = tr(L"Opcje");
            add(std::move(search));
        }
        if (!compat_.query.empty()) {
            const auto wanted = unicode_text::lower(compat_.query);
            const auto has = [&](std::wstring_view text) { return unicode_text::lower(text).find(wanted) != std::wstring::npos; };
            int found = 0;
            for (const auto& option : catalog::options()) {
                if (option.depends_on && catalog::value(options, option.depends_on) != "1") continue;
                if (!has(label(option.label)) && !has(label(option.description)) && !has(wide(option.sets)) && !has(wide(option.id))) continue;
                if (std::string_view(option.id) == "dll_overrides") { dllRows([&](Row row) { add(std::move(row)); }); ++found; continue; }
                if (auto row = optionRow(option)) { add(std::move(*row)); ++found; }
            }
            for (const auto& variable : options.environment)
                if (!shownAsSwitch(variable) && (has(wide(variable.name)) || has(wide(variable.value)))) { add(variableRow(variable.name, &variable.value)); ++found; }
            add({Id::launch_search_clear, Control::action, found ? tr(L"Wyniki: ") + std::to_wstring(found) : std::wstring(tr(L"Nic nie pasuje")),
                 tr(L"Wróć do wszystkich sekcji."), tr(L"Wyczyść")});
            break;
        }
        // Ustawione dla tej gry: every override in one place, each with its way back to the default.
        if (game && !overrides.empty()) {
            const bool open = compat_.expanded.contains("own");
            Row heading{Id::launch_fold, Control::fold, tr(L"Ustawione dla tej gry"), tr(L"Tu gra nie podąża za ustawieniami domyślnymi."),
                tr(L"Ta gra: ") + std::to_wstring(overrides.size()), open};
            heading.key = "own";
            add(std::move(heading));
            if (open) {
                const auto nested = [&](Row row) { row.indent = 1; add(std::move(row)); };
                for (const auto& option : catalog::options()) {
                    if (!overrides.contains(overrideId(option))) continue;
                    if (std::string_view(option.id) == "dll_overrides") {
                        Row row{Id::launch_option, Control::text, tr(L"Biblioteki DLL (WINEDLLOVERRIDES)"), label(option.description),
                            options.dll_overrides.empty() ? std::wstring(tr(L"Brak")) : wide(options.dll_overrides)};
                        row.key = "dll_overrides"; row.dense = true; row.detail = L"WINEDLLOVERRIDES";
                        own(row, "dll_overrides");
                        nested(std::move(row));
                    } else if (auto row = optionRow(option)) nested(std::move(*row));
                }
                for (const auto& id : overrides) {
                    if (!id.starts_with("env:") || catalog::forVariable(std::string_view(id).substr(4))) continue;
                    const auto name = id.substr(4);
                    const auto found = std::find_if(options.environment.begin(), options.environment.end(), [&](const auto& v) { return v.name == name; });
                    nested(variableRow(name, found != options.environment.end() ? &found->value : nullptr));
                }
            }
        }
        const LaunchOptions plain;
        for (const auto& fold : folds) {
            if (fold.section == catalog::Section::gamescope && !options.gamescope) continue; // Its options follow its switch.
            const auto in = [&](const catalog::Option& option) {
                return fold.section ? option.section == *fold.section && std::string_view(option.id) != "dll_overrides" :
                                      std::string_view(option.id) == "dll_overrides";
            };
            // The default page counts what differs from Launchme's own; a game's page its overrides.
            int changed = 0;
            std::vector<std::string> mine;
            for (const auto& option : catalog::options()) {
                if (!in(option)) continue;
                if (game && overrides.contains(overrideId(option))) mine.push_back(overrideId(option));
                else if (!game && std::string_view(option.id) != "prefix" && catalog::value(options, option.id) != catalog::value(plain, option.id)) ++changed;
            }
            if (game && fold.section == catalog::Section::game)
                for (const auto& id : overrides)
                    if (id.starts_with("env:") && !catalog::forVariable(std::string_view(id).substr(4))) mine.push_back(id);
            if (game) changed = static_cast<int>(mine.size());
            const bool open = compat_.expanded.contains(fold.id);
            Row heading{Id::launch_fold, Control::fold, tr(fold.title), tr(fold.description),
                changed ? (game ? tr(L"Ta gra: ") : tr(L"Zmienione: ")) + std::to_wstring(changed) : std::wstring{}, open};
            heading.key = fold.id;
            // A section's overrides back to the default at once.
            if (!mine.empty()) {
                heading.badge = tr(L"Domyślne");
                heading.reset = "section:" + std::string(fold.id);
            }
            add(std::move(heading));
            if (!open) continue;
            const auto nested = [&](Row row) { row.indent = 1; add(std::move(row)); };
            if (!fold.section) { dllRows(nested); continue; }
            for (const auto& option : catalog::options()) {
                if (option.section != *fold.section) continue;
                const std::string_view id = option.id;
                if (option.depends_on && catalog::value(options, option.depends_on) != "1") continue;
                if (id == "environment") {
                    for (const auto& variable : options.environment)
                        if (!shownAsSwitch(variable)) nested(variableRow(variable.name, &variable.value));
                    // The default's variables this game goes without.
                    if (game)
                        for (const auto& variable : global.environment)
                            if (overrides.contains("env:" + variable.name) && !shownAsSwitch(variable) &&
                                std::none_of(options.environment.begin(), options.environment.end(), [&](const auto& v) { return v.name == variable.name; }))
                                nested(variableRow(variable.name, nullptr));
                    nested({Id::launch_env_add, Control::text, tr(L"Dodaj zmienną"),
                            tr(L"NAZWA=wartość. Ma pierwszeństwo przed przełącznikami powyżej."), tr(L"Dodaj…")});
                    Row presets{Id::launch_env_preset, Control::choice, tr(L"Popularne zmienne"), tr(L"Często polecane na ProtonDB i PCGamingWiki."), tr(L"Wybierz…")};
                    presets.choice = -1;
                    for (const auto& preset : catalog::variablePresets())
                        presets.choices.push_back({label(preset.label) + L" (" + wide(preset.name) + L"=" + wide(preset.value) + L")", 0, false, wide(preset.name)});
                    nested(std::move(presets));
                    continue;
                }
                auto row = optionRow(option);
                if (!row) continue;
                nested(std::move(*row));
                if (id == "prefix") {
                    const auto mine_prefix = store::gamePrefix(compat_.game);
                    Row tools{Id::prefix_entry, Control::page, tr(L"Narzędzia prefiksu"), tr(L"Rozmiar, winecfg, winetricks, instalatory i usuwanie."), {}};
                    const auto effective = options.prefix.empty() ? mine_prefix : options.prefix;
                    tools.key = keyOf(effective.lexically_normal());
                    if (const auto size = compat_.sizes.find(effective.lexically_normal()); size != compat_.sizes.end()) tools.value = sizeText(size->second);
                    else if (std::error_code ec; std::filesystem::exists(effective, ec)) compatMeasure(effective);
                    nested(std::move(tools));
                }
            }
        }
        // Skróty: the game started through Launchme, with these options, from outside it.
        if (game) {
            section(tr(L"Skróty"));
            if (shortcutAvailable(compat_.game, ShortcutPlace::steam)) {
                const bool in_steam = hasShortcut(compat_.game, ShortcutPlace::steam);
                add({Id::launch_steam, Control::action, L"Steam",
                     in_steam ? std::wstring(tr(L"Gra jest w bibliotece Steam: Steam uruchamia ją przez Launchme, z tymi opcjami.")) :
                                std::wstring(tr(L"Skrót w bibliotece Steam (także w trybie gry na Steam Decku), z grafikami; gra startuje przez Launchme.")),
                     in_steam ? tr(L"Usuń ze Steam") : tr(L"Dodaj do Steam")});
            }
            if (shortcutAvailable(compat_.game, ShortcutPlace::desktop)) {
                const bool there = hasShortcut(compat_.game, ShortcutPlace::desktop);
                add({Id::launch_shortcut_desktop, Control::action, tr(L"Skrót na pulpicie"),
                     tr(L"Ikona gry na pulpicie; uruchamia ją przez Launchme, z tymi opcjami."), there ? tr(L"Usuń") : tr(L"Utwórz")});
            }
            if (shortcutAvailable(compat_.game, ShortcutPlace::menu)) {
                const bool there = hasShortcut(compat_.game, ShortcutPlace::menu);
                add({Id::launch_shortcut_menu, Control::action, tr(L"Menu aplikacji"),
                     tr(L"Gra na liście programów systemu, jak gry spoza Launchme."), there ? tr(L"Usuń") : tr(L"Dodaj")});
            }
        }
        break;
    }
    default: break;
    }
}
#endif
