// The Stores' two built-in tabs, before the store plugins' own (Settings → System → Funkcje):
//   Launchery  the store clients and front ends of src/store/launchers.hpp as tiles: one click installs
//              one (winget or the vendor's signed installer; on Linux a question first: the native
//              Flatpak, or the Windows build in a Wine prefix), an installed one opens;
//   Promocje   games free to keep, the next free ones and discounts (src/net/promos.hpp); a tile
//              opens the game's store page.
// Both are grid pages of the plugin page screen (plugin_ui::Page::tiled) whose owner is one of
// store_hub's ids instead of a plugin, so the tabs, the frame, the keys and the desktop sheet are the
// stores'. Detection, downloads and installations run on workers and come back as RemoteMessages.
#include "launcher.hpp"
#include "../artwork/platform_logo.hpp"
#include "../platform/file_io.hpp"
#include "../platform/unix_time.hpp"
#include "../ui/desktop_sheet.hpp"
#include <ctime>
#ifndef _WIN32
#include "../library/launch_options_store.hpp" // Wine prefixes exist on Linux only.
#endif

namespace {
using launcher_install::Phase;

plugin_ui::Row heading(std::wstring title) { return {std::move(title), {}, {}, {}, true}; }

// "12.11": the day an offer ends or begins, in local time.
std::wstring dayOf(int64_t time) {
    const std::time_t value = static_cast<std::time_t>(time);
    std::tm local{};
#ifdef _WIN32
    if (localtime_s(&local, &value) != 0) return {};
#else
    if (!localtime_r(&value, &local)) return {};
#endif
    wchar_t text[16];
    swprintf(text, std::size(text), L"%d.%02d", local.tm_mday, local.tm_mon + 1);
    return text;
}

std::string fnv(std::string_view text) {
    uint64_t hash = 1469598103934665603ull;
    for (const unsigned char c : text) { hash ^= c; hash *= 1099511628211ull; }
    char out[17];
    snprintf(out, sizeof(out), "%016llx", static_cast<unsigned long long>(hash));
    return out;
}

bool picture(const std::vector<uint8_t>& bytes) {
    return bytes.size() > 128 && ((bytes[0] == 0x89 && bytes[1] == 'P') || (bytes[0] == 0xff && bytes[1] == 0xd8));
}

// A tile's picture (a worker's): a shipped or cached mark, or a store picture kept in <config>/store-hub/images.
}

std::vector<uint8_t> store_hub::tileImage(const std::string& image, const std::filesystem::path& config) {
    if (image.starts_with("logo:")) return platformLogo(text_encoding::wideLenient(image.substr(5)), config / L"platforms");
    if (image.starts_with("system:")) {
        wchar_t exe[32768]{};
        GetModuleFileNameW(nullptr, exe, static_cast<DWORD>(std::size(exe)));
        const auto folder = std::filesystem::path(exe).parent_path();
        const auto name = text_encoding::wideLenient(image.substr(7));
        for (const auto& place : {folder / L"assets" / L"systems", folder / L".." / L"share" / L"launchme" / L"assets" / L"systems"})
            if (auto bytes = file_io::readBytes(place / name, 2 * 1024 * 1024); picture(bytes)) return bytes;
        return {};
    }
    if (!image.starts_with("https://")) return {};
    const auto file = config / L"store-hub" / L"images" / (text_encoding::wideLenient(fnv(image)) + L".img");
    if (auto bytes = file_io::readBytes(file, 4 * 1024 * 1024); picture(bytes)) return bytes;
    const auto reply = httpGet(text_encoding::wideLenient(image), L"Accept: image/jpeg,image/png\r\n", 4 * 1024 * 1024);
    if (reply.status != 200 || reply.truncated || !picture(reply.body)) return {};
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    atomic_file::write(file, reply.body);
    return reply.body;
}

int Launcher::storeHubTabs() const { return settings_.store_hub ? store_hub::tab_count : 0; }
int Launcher::storeTabCount() const { return storeHubTabs() + static_cast<int>(plugin_menu_.size()); }
bool Launcher::storeHubPage() const { return screen_ == ScreenMode::plugin_page && !plugin_drawer_active_ && store_hub::builtin(plugin_page_.plugin); }

// The tab an owner's page belongs to; -1 for an owner without one.
int Launcher::storeTabOf(const std::string& owner) const {
    if (storeHubTabs() && owner == store_hub::launchers_id) return 0;
    if (storeHubTabs() && owner == store_hub::promos_id) return 1;
    const auto found = std::find_if(plugin_menu_.begin(), plugin_menu_.end(), [&](const auto& entry) { return entry.plugin == owner; });
    return found == plugin_menu_.end() ? -1 : storeHubTabs() + static_cast<int>(found - plugin_menu_.begin());
}

void Launcher::openStoreTab(int tab) {
    tab = std::clamp(tab, 0, std::max(0, storeTabCount() - 1));
    store_tab_ = tab;
    if (tab < storeHubTabs()) { openStoreHub(tab); return; }
    pluginAction(plugin_menu_[static_cast<size_t>(tab - storeHubTabs())]);
}

plugin_ui::Page Launcher::launchersPage() const {
    const auto& hub = store_hub_;
    plugin_ui::Page page;
    page.plugin = store_hub::launchers_id;
    page.title = tr(L"Launchery");
    page.tiled = true;
    const auto machine = hub.machine_known ? hub.machine : launchers::Machine{launcher_windows_, false, false, false};
    if (!hub.machine_known) page.status = tr(L"Sprawdzam zainstalowane launchery…");
    for (const auto group : {launchers::Group::stores, launchers::Group::emulation}) {
        bool titled = false;
        for (const auto& entry : launchers::catalog()) {
            if (entry.group != group) continue;
            // Before the machine is known every program of this system shows; then only what can be had here.
            auto probe = machine;
            if (!hub.machine_known) { probe.winget = probe.flatpak = probe.wine = true; }
            const bool installed = hub.found.contains(std::string(entry.id));
            if (launchers::methods(entry, probe).empty() && !installed) continue;
            if (!titled) { page.rows.push_back(heading(group == launchers::Group::stores ? tr(L"Sklepy i klienty") : tr(L"Emulacja"))); titled = true; }
            plugin_ui::Row row;
            row.tile = true;
            row.id = std::string(entry.id);
            row.title = std::wstring(entry.name);
            row.subtitle = tr(entry.about);
            row.image = text_encoding::utf8Lenient(entry.image);
            row.tone = entry.tone;
            row.command = "launcher";
            if (const auto job = hub.jobs.find(row.id); job != hub.jobs.end()) {
                row.busy = true;
                row.badge = job->second.removing ? tr(L"Odinstalowywanie…") : job->second.phase == Phase::downloading ? tr(L"Pobieranie…") : job->second.phase == Phase::verifying ? tr(L"Sprawdzanie podpisu…") :
                            job->second.phase == Phase::waiting ? tr(L"Instalator otwarty") : tr(L"Instalowanie…");
            } else if (installed) row.badge = tr(L"Zainstalowany");
            else if (hub.handed.contains(row.id)) { row.busy = true; row.badge = tr(L"Instalator otwarty"); }
            if (const auto note = hub.notes.find(row.id); note != hub.notes.end() && !row.busy) row.subtitle = note->second;
            page.rows.push_back(std::move(row));
        }
    }
    page.focus = page.next(1);
    return page;
}

plugin_ui::Page Launcher::promosPage() {
    auto& hub = store_hub_;
    plugin_ui::Page page;
    page.plugin = store_hub::promos_id;
    page.title = tr(L"Promocje");
    page.tiled = true;
    hub.offers.clear();
    const int64_t now = static_cast<int64_t>(unix_time::now());
    const auto section = [&](const wchar_t* title, const std::vector<promos::Offer>& offers) {
        if (offers.empty()) return;
        page.rows.push_back(heading(title));
        for (const auto& offer : offers) {
            plugin_ui::Row row;
            row.tile = true;
            row.id = std::to_string(hub.offers.size());
            row.title = offer.title;
            row.image = text_encoding::utf8Lenient(promos::tilePicture(offer.image));
            row.command = "offer";
            const bool soon = offer.starts > now;
            row.badge = soon ? tr(L"Wkrótce") : offer.free ? tr(L"Za darmo") : L"-" + std::to_wstring(offer.discount) + L"%";
            row.badge_strong = !soon;
            // "Epic Games · zamiast 142,00 zł · do 12.11", "Steam · 76,49 zł · do 3.12".
            row.subtitle = offer.where;
            const auto add = [&](const std::wstring& part) { if (!part.empty()) row.subtitle += (row.subtitle.empty() ? L"" : L" · ") + part; };
            if (!offer.free) add(offer.price);
            else if (!offer.regular.empty()) add(std::wstring(tr(L"zamiast ")) + offer.regular);
            if (soon) add(std::wstring(tr(L"od ")) + dayOf(offer.starts));
            else if (offer.ends > now) add(std::wstring(tr(L"do ")) + dayOf(offer.ends));
            hub.offers.push_back(offer);
            page.rows.push_back(std::move(row));
        }
    };
    section(tr(L"Za darmo teraz"), hub.feed.free_now);
    section(tr(L"Wkrótce za darmo"), hub.feed.free_soon);
    section(tr(L"Przeceny"), hub.feed.deals);
    if (hub.feed_loading && hub.offers.empty()) page.status = tr(L"Sprawdzam sklepy…");
    else if (hub.feed_loaded && hub.feed.unavailable) page.status = tr(L"Nie udało się pobrać promocji. Sprawdź połączenie; F5 ponawia.");
    else if (hub.feed_loaded && hub.offers.empty()) page.status = tr(L"Dziś sklepy nic nie rozdają ani nie przeceniają.");
    if (page.rows.empty()) page.rows.push_back(heading(hub.feed_loading || !hub.feed_loaded ? tr(L"Ładowanie…") : tr(L"Brak promocji")));
    page.focus = page.next(1);
    return page;
}

// Shows a built-in tab (0 Launchery, 1 Promocje). refreshed: the same tab again with new facts; its
// focus and scroll stay.
void Launcher::openStoreHub(int tab, bool refreshed) {
    if (!storeHubTabs()) return;
    tab = std::clamp(tab, 0, store_hub::tab_count - 1);
    if (tab == 0) {
        if (!refreshed) storeHubDetect();
        showPluginPage(store_hub::launchers_id, launchersPage(), false, refreshed);
    } else {
        if (!refreshed) storeHubLoadPromos(false);
        showPluginPage(store_hub::promos_id, promosPage(), false, refreshed);
    }
    plugin_page_action_.clear();
    plugin_page_game_ = {};
}

// New facts for the tab on screen (not for the question over it: its rows are answered as they stand).
void Launcher::storeHubRefresh() {
    if (!storeHubPage() || plugin_page_.dialog) return;
    // A page that had nothing to pick yet (still loading) hands the focus to the first tile that arrives.
    const bool waiting = std::none_of(plugin_page_.rows.begin(), plugin_page_.rows.end(), [](const auto& row) { return row.actionable(); });
    openStoreHub(plugin_page_.plugin == store_hub::promos_id ? 1 : 0, true);
    if (waiting && plugin_page_.focus < 0) { plugin_page_.focus = plugin_page_.next(1); selected_ = static_cast<size_t>(std::max(0, plugin_page_.focus)); }
}

// F5: the launchers are looked for again, the promotions fetched again.
void Launcher::storeHubReload() {
    if (!storeHubPage() || plugin_page_.dialog) return;
    if (plugin_page_.plugin == store_hub::promos_id) storeHubLoadPromos(true); else storeHubDetect();
    storeHubRefresh();
}

void Launcher::storeHubDetect() {
    auto& hub = store_hub_;
    if (hub.detecting) return;
    hub.detecting = true;
    const auto alive = alive_; const auto hwnd = hwnd_;
    const bool known = hub.machine_known;
    if (!work_.start([alive, hwnd, known, machine_of = launcher_machine_, probe_of = launcher_probe_, prefixes_of = launcher_prefixes_, config = userConfigDirectory()] {
        std::optional<launchers::Machine> machine;
        if (!known) machine = machine_of ? machine_of() : launcher_install::machine();
        const auto probe = probe_of ? probe_of() : launchers::systemProbe(config);
        std::map<std::string, launchers::Found> found;
        for (const auto& entry : launchers::catalog())
            if (auto place = launchers::detect(entry, probe)) found.emplace(std::string(entry.id), std::move(*place));
        // Linux: the prefixes the question offers.
        std::vector<std::filesystem::path> prefixes;
        if (prefixes_of) prefixes = prefixes_of();
#ifndef _WIN32
        else for (const auto& prefix : launch_options_store::prefixes()) prefixes.push_back(prefix.path);
#endif
        if (!alive->load()) return;
        auto message = std::make_unique<RemoteMessage>();
        message->run = [machine, found = std::move(found), prefixes = std::move(prefixes)](Launcher& app) mutable {
            auto& hub = app.store_hub_;
            hub.detecting = false;
            if (machine) { hub.machine = *machine; hub.machine_known = true; }
            // The installer the user was finishing is done once its launcher is there.
            for (auto it = hub.handed.begin(); it != hub.handed.end();) {
                if (!found.contains(*it)) { ++it; continue; }
                if (const auto* entry = launchers::find(*it)) app.showNotification(NotificationKind::download, tr(L"Zainstalowano"), std::wstring(entry->name));
                hub.notes.erase(*it);
                it = hub.handed.erase(it);
            }
            const bool changed = hub.found.size() != found.size() ||
                !std::equal(hub.found.begin(), hub.found.end(), found.begin(), [](const auto& a, const auto& b) { return a.first == b.first; });
            hub.found = std::move(found);
            hub.choice_prefixes = std::move(prefixes);
            if (app.settings_.show_launchers && (changed || machine)) app.buildShelves(); // The home screen's Launchery row.
            app.storeHubRefresh();
        };
        if (PostMessageW(hwnd, remote_message, 0, reinterpret_cast<LPARAM>(message.get()))) message.release();
    })) hub.detecting = false;
}

void Launcher::storeHubLoadPromos(bool refresh) {
    auto& hub = store_hub_;
    if (hub.feed_loading) return;
    // A feed shown within the cache's time is not asked for again (F5 does).
    if (!refresh && hub.feed_loaded && GetTickCount64() - hub.feed_at < static_cast<uint64_t>(promos::cache_seconds) * 1000) return;
    hub.feed_loading = true;
    const auto generation = ++hub.feed_generation;
    const auto alive = alive_; const auto hwnd = hwnd_;
    const auto region = promos_region_ ? promos_region_() : promos::systemRegion(!i18n::english());
    if (!work_.start([alive, hwnd, generation, refresh, region, get = promos_get_, cache = userConfigDirectory() / L"store-hub" / L"promos"] {
        auto feed = promos::load(cache, region, refresh, static_cast<int64_t>(unix_time::now()), get);
        if (!alive->load()) return;
        auto message = std::make_unique<RemoteMessage>();
        message->run = [generation, feed = std::move(feed)](Launcher& app) mutable {
            auto& hub = app.store_hub_;
            if (generation != hub.feed_generation) return;
            hub.feed_loading = false; hub.feed_loaded = true; hub.feed_at = GetTickCount64();
            // A failed refresh keeps what is on screen.
            if (!feed.unavailable || hub.feed.empty()) hub.feed = std::move(feed);
            app.storeHubRefresh();
        };
        if (PostMessageW(hwnd, remote_message, 0, reinterpret_cast<LPARAM>(message.get()))) message.release();
    })) hub.feed_loading = false;
}

// A tile or a row of a launcher's card was chosen.
void Launcher::storeHubAccept() {
    if (plugin_page_.focus < 0 || plugin_page_.focus >= static_cast<int>(plugin_page_.rows.size())) return;
    const auto row = plugin_page_.rows[static_cast<size_t>(plugin_page_.focus)]; // A copy: the page may be replaced below.
    if (!row.actionable()) return;
    if (row.command == "offer") {
        size_t index = 0;
        try { index = static_cast<size_t>(std::stoul(row.id)); } catch (...) { return; }
        if (index < store_hub_.offers.size()) openGlanceBrowser(store_hub_.offers[index].url);
        return;
    }
    if (plugin_page_.dialog) storeHubChoice(row.command);
    else openLauncherCard(row.id); // Nothing is installed by the tile itself: its card asks first.
}

// The card of a launcher, a dialog over the tab: what it is and what can be done with it now. Installed:
// open it, remove it (after one more question), its page. Not installed: install it (Linux: the native
// build or the Windows one in a prefix), its page. While something runs for it, only that.
void Launcher::openLauncherCard(const std::string& id, bool confirm) {
    auto& hub = store_hub_;
    const auto* entry = launchers::find(id);
    if (!entry) return;
    hub.choosing = id;
    plugin_ui::Page card;
    card.plugin = store_hub::launchers_id;
    card.dialog = true;
    card.title = std::wstring(entry->name);
    card.status = tr(entry->about);
    const auto choice = [&](std::wstring title, std::wstring subtitle, std::string command) {
        plugin_ui::Row item{std::move(title), std::move(subtitle), {}, {}};
        item.command = std::move(command);
        card.rows.push_back(std::move(item));
    };
    const auto found = hub.found.find(id);
    const auto job = hub.jobs.find(id);
    if (job != hub.jobs.end()) {
        card.rows.push_back(heading(job->second.removing ? tr(L"Odinstalowywanie…") : tr(L"Instalacja trwa…")));
    } else if (!hub.machine_known) {
        card.rows.push_back(heading(tr(L"Sprawdzam zainstalowane launchery…")));
    } else if (found != hub.found.end() && confirm) {
        card.rows.push_back(heading(std::wstring(tr(L"Odinstalować ")) + std::wstring(entry->name) + L"?"));
        choice(tr(L"Odinstaluj"), tr(L"Gry zainstalowane przez ten launcher mogą przestać się uruchamiać."), "uninstall-yes");
    } else if (found != hub.found.end()) {
        card.rows.push_back(heading(tr(L"Zainstalowany")));
        choice(tr(L"Otwórz"), found->second.prefix.empty() ? found->second.program.wstring() + (found->second.arguments.empty() ? L"" : L" " + found->second.arguments) :
                                                             found->second.prefix.wstring(), "open");
        const auto removal = launchers::removal(*entry, hub.machine, found->second);
        if (removal == launchers::Removal::winget) choice(tr(L"Odinstaluj"), L"winget uninstall " + text_encoding::wide(entry->winget), "uninstall");
        else if (removal == launchers::Removal::flatpak) choice(tr(L"Odinstaluj"), L"flatpak uninstall --user " + text_encoding::wide(entry->flatpak), "uninstall");
        else if (hub.machine.windows) choice(tr(L"Odinstaluj w Ustawieniach Windows"), tr(L"Otwiera listę zainstalowanych aplikacji."), "apps");
    } else {
        const auto methods = launchers::methods(*entry, hub.machine);
        const auto has = [&](launchers::Method method) { return std::find(methods.begin(), methods.end(), method) != methods.end(); };
        if (hub.machine.windows && (has(launchers::Method::winget) || has(launchers::Method::installer))) {
            card.rows.push_back(heading(tr(L"Instalacja")));
            choice(tr(L"Zainstaluj"), has(launchers::Method::winget) ? L"winget install " + text_encoding::wide(entry->winget) :
                                                                     std::wstring(tr(L"Oficjalny instalator producenta; jego podpis jest sprawdzany przed uruchomieniem.")), "install");
        }
        if (has(launchers::Method::flatpak)) {
            card.rows.push_back(heading(tr(L"Natywny klient")));
            choice(tr(L"Zainstaluj z Flathuba"), L"flatpak install --user flathub " + text_encoding::wide(entry->flatpak), "native");
        }
        if (has(launchers::Method::wine)) {
            card.rows.push_back(heading(tr(L"Wersja dla Windows (Wine / Proton)")));
            choice(tr(L"Zainstaluj w nowym prefiksie"), tr(L"Osobny prefiks tylko dla tego launchera (zalecane)."), "prefix-new");
            for (size_t i = 0; i < hub.choice_prefixes.size() && i < 12; ++i)
                choice(std::wstring(tr(L"Zainstaluj w prefiksie ")) + hub.choice_prefixes[i].filename().wstring(), hub.choice_prefixes[i].wstring(), "prefix-" + std::to_string(i));
        }
    }
    if (const auto note = hub.notes.find(id); note != hub.notes.end() && job == hub.jobs.end() && !confirm) card.status = note->second;
    card.rows.push_back(heading({}));
    if (!confirm && !entry->page.empty()) choice(tr(L"Strona pobierania"), std::wstring(entry->page), "page");
    choice(confirm ? tr(L"Anuluj") : tr(L"Zamknij"), {}, confirm ? "back" : "cancel");
    for (auto& item : card.rows) { item.top = card.total; card.total += item.height(); }
    card.focus = card.next(1);
    // The question opens on "Anuluj": removing takes a deliberate step up.
    if (confirm) for (int i = 0; i < static_cast<int>(card.rows.size()); ++i) if (card.rows[static_cast<size_t>(i)].command == "back") card.focus = i;
    showPluginPage(store_hub::launchers_id, std::move(card), false, false);
}

// Starts an installed launcher (its card's Otwórz, its tile in the home screen's Launchery row).
void Launcher::openLauncher(const std::string& id) {
    const auto* entry = launchers::find(id);
    const auto found = store_hub_.found.find(id);
    if (!entry || found == store_hub_.found.end()) return;
    GameAction action{found->second.program.wstring(), found->second.arguments, {}, ActionKind::open_client};
#ifndef _WIN32
    if (!found->second.prefix.empty()) { // A Windows build: through its prefix, with the default options.
        auto launch = launch_options_store::defaultCompatLaunch();
        launch.game_key = "launcher:" + id; launch.title = std::wstring(entry->name); launch.prefix = found->second.prefix;
        action.compat = std::make_shared<const CompatLaunch>(std::move(launch));
    }
#endif
    if (execute_(hwnd_, action) <= 32) showNotification(NotificationKind::error, tr(L"Nie można uruchomić launchera"), std::wstring(entry->name));
    else audio_.launch();
}

// The Launchery row's tiles, in the catalogue's order: each a stand-in "game" that opens its launcher.
std::vector<Game> Launcher::launcherTiles() const {
    std::vector<Game> tiles;
    for (const auto& entry : launchers::catalog()) {
        const auto found = store_hub_.found.find(std::string(entry.id));
        if (found == store_hub_.found.end()) continue;
        Game tile;
        tile.kind = GameKind::store; tile.source = GameSource::custom;
        tile.source_id = "launcher:" + std::string(entry.id);
        tile.name = std::wstring(entry.name);
        tile.installed = true;
        tile.play_action = GameAction{found->second.program.wstring(), found->second.arguments, {}, ActionKind::open_client};
        tiles.push_back(std::move(tile));
    }
    return tiles;
}

// A row of the open card.
void Launcher::storeHubChoice(const std::string& choice) {
    auto& hub = store_hub_;
    const auto id = hub.choosing;
    const auto* entry = launchers::find(id);
    if (choice == "uninstall") { openLauncherCard(id, true); return; } // One more question.
    if (choice == "back") { openLauncherCard(id); return; }
    openStoreHub(0); // Every other answer closes the card onto the tab.
    if (!entry || choice == "cancel" || hub.jobs.contains(id)) return;
    if (choice == "page") { openGlanceBrowser(std::wstring(entry->page)); return; }
    const auto found = hub.found.find(id);
    if (choice == "open") { openLauncher(id); return; }
    if (choice == "apps") {
        if (execute_(hwnd_, GameAction{L"ms-settings:appsfeatures", {}, {}, ActionKind::open_client}) <= 32)
            showNotification(NotificationKind::error, tr(L"Nie można otworzyć Ustawień Windows"), std::wstring(entry->name));
        return;
    }
    if (choice == "uninstall-yes" && found != hub.found.end()) { storeHubUninstall(*entry, launchers::removal(*entry, hub.machine, found->second)); return; }
    if (found != hub.found.end() || !hub.machine_known) return; // Installing what is there already: nothing to do.
    if (choice == "install") { storeHubInstall(*entry, launchers::methods(*entry, hub.machine), {}, false); return; }
    if (choice == "native") { storeHubInstall(*entry, {launchers::Method::flatpak}, {}, false); return; }
    if (choice == "prefix-new") { storeHubInstall(*entry, {launchers::Method::wine}, {}, true); return; }
    if (choice.starts_with("prefix-")) {
        size_t index = 0;
        try { index = static_cast<size_t>(std::stoul(choice.substr(7))); } catch (...) { return; }
        if (index < hub.choice_prefixes.size()) storeHubInstall(*entry, {launchers::Method::wine}, hub.choice_prefixes[index], false);
    }
}

void Launcher::storeHubUninstall(const launchers::Entry& entry, launchers::Removal removal) {
    auto& hub = store_hub_;
    const std::string id(entry.id);
    if (removal == launchers::Removal::none || hub.jobs.contains(id)) return;
    auto cancel = std::make_shared<std::atomic_bool>(false);
    hub.jobs[id] = {Phase::installing, cancel, true};
    hub.notes.erase(id);
    const auto alive = alive_; const auto hwnd = hwnd_;
    const bool started = work_.start([alive, hwnd, cancel, id, entry = &entry, removal, backend_of = launcher_backend_] {
        const auto backend = backend_of ? backend_of() : launcher_install::systemBackend();
        const auto result = launcher_install::uninstall(*entry, removal, backend, [&] { return cancel->load() || !alive->load(); });
        auto message = std::make_unique<RemoteMessage>();
        message->run = [id, result](Launcher& app) {
            auto& hub = app.store_hub_;
            hub.jobs.erase(id);
            const auto* entry = launchers::find(id);
            const std::wstring name = entry ? std::wstring(entry->name) : std::wstring{};
            if (result.ok()) { hub.found.erase(id); app.showNotification(NotificationKind::download, tr(L"Odinstalowano"), name); }
            else if (result.error != launcher_install::Error::cancelled) {
                hub.notes[id] = tr(L"Nie udało się odinstalować.");
                app.showNotification(NotificationKind::error, name, tr(L"Nie udało się odinstalować."));
            }
            app.storeHubDetect();
            app.storeHubRefresh();
        };
        if (alive->load() && PostMessageW(hwnd, remote_message, 0, reinterpret_cast<LPARAM>(message.get()))) message.release();
    });
    if (!started) hub.jobs.erase(id);
    else audio_.context();
    storeHubRefresh();
}

void Launcher::storeHubInstall(const launchers::Entry& entry, std::vector<launchers::Method> methods, std::filesystem::path prefix, bool new_prefix) {
    auto& hub = store_hub_;
    const std::string id(entry.id);
    if (hub.jobs.contains(id)) return;
    auto cancel = std::make_shared<std::atomic_bool>(false);
    hub.jobs[id] = {Phase::installing, cancel};
    hub.notes.erase(id); hub.handed.erase(id);
    const auto alive = alive_; const auto hwnd = hwnd_;
    const auto post = [alive, hwnd](std::function<void(Launcher&)> run) {
        auto message = std::make_unique<RemoteMessage>();
        message->run = std::move(run);
        if (alive->load() && PostMessageW(hwnd, remote_message, 0, reinterpret_cast<LPARAM>(message.get()))) message.release();
    };
    const bool started = work_.start([post, alive, cancel, id, entry = &entry, methods = std::move(methods), prefix = std::move(prefix), new_prefix,
                                      backend_of = launcher_backend_, make_prefix = launcher_new_prefix_, config = userConfigDirectory()]() mutable {
        launcher_install::Request request{entry, std::move(methods), config, std::move(prefix)};
        if (new_prefix && make_prefix) request.prefix = make_prefix(std::wstring(entry->name));
#ifndef _WIN32
        else if (new_prefix) request.prefix = launch_options_store::newPrefix(std::wstring(entry->name), "default");
#endif
        // Remembered before it runs: the launcher is looked for in this prefix from now on.
        if (!request.prefix.empty()) launchers::rememberWinePrefix(config, id, request.prefix);
        const auto backend = backend_of ? backend_of() : launcher_install::systemBackend();
        const auto result = launcher_install::install(request, backend, [&] { return cancel->load() || !alive->load(); }, [&](Phase phase) {
            post([id, phase](Launcher& app) {
                if (const auto job = app.store_hub_.jobs.find(id); job != app.store_hub_.jobs.end()) { job->second.phase = phase; app.storeHubRefresh(); }
            });
        });
        post([id, result](Launcher& app) { app.storeHubInstalled(id, result); });
    });
    if (!started) hub.jobs.erase(id);
    else audio_.context();
    storeHubRefresh();
}

void Launcher::storeHubInstalled(const std::string& id, const launcher_install::Result& result) {
    auto& hub = store_hub_;
    hub.jobs.erase(id);
    const auto* entry = launchers::find(id);
    const std::wstring name = entry ? std::wstring(entry->name) : std::wstring{};
    using launcher_install::Error;
    if (result.ok() && result.handed_over) {
        // The vendor's installer has its own window now: the launcher is looked for until it is there.
        hub.handed.insert(id);
        hub.notes[id] = tr(L"Dokończ instalację w oknie instalatora.");
        hub.detect_at = GetTickCount64() + 4000;
        showNotification(NotificationKind::download, name, tr(L"Instalator jest otwarty: dokończ instalację w jego oknie."));
    } else if (result.ok()) {
        // Found by the detection below when it is really there (Wine installers report little).
        if (result.method != launchers::Method::wine) showNotification(NotificationKind::download, tr(L"Zainstalowano"), name);
        else hub.handed.insert(id), hub.detect_at = GetTickCount64() + 1000;
    } else if (result.error != Error::cancelled) {
        const auto reason = result.error == Error::network ? tr(L"Nie udało się pobrać instalatora.") :
                            result.error == Error::signature ? tr(L"Instalator nie ma ważnego podpisu producenta: nie został uruchomiony.") :
                            result.error == Error::untrusted ? tr(L"Adres instalatora nie należy do producenta.") :
                            result.error == Error::unavailable ? tr(L"Na tym systemie nie ma czym go zainstalować.") :
                            result.error == Error::start ? tr(L"Nie udało się uruchomić instalatora.") : tr(L"Instalacja nie powiodła się.");
        hub.notes[id] = reason;
        showNotification(NotificationKind::error, name, reason);
    }
    storeHubDetect();
    storeHubRefresh();
}

// While an installer's own window is open the launcher is looked for every few seconds, on its tab.
void Launcher::pollStoreHub() {
    auto& hub = store_hub_;
    if (hub.handed.empty() || hub.detecting || !storeHubPage() || plugin_page_.plugin != store_hub::launchers_id) return;
    const auto now = GetTickCount64();
    if (now < hub.detect_at) return;
    hub.detect_at = now + 5000;
    storeHubDetect();
}

// A grid page's columns follow the room it is drawn in: the TV screen's column or the desktop sheet's.
void Launcher::arrangeStoreHub() {
    if (!plugin_page_.tiled || plugin_drawer_active_) return;
    const float before = plugin_page_.arranged_width;
    if (desktopStorePage()) {
        const auto view = renderer_.desktopViewport();
        const auto panel = desktop_ui::artworkSheet(view.width, view.height, desktopSheetDock()).panel();
        plugin_page_.arrange(panel.right - panel.left - 48, 190);
    } else {
        const auto view = drawerViewport();
        plugin_page_.arrange(plugin_ui::Layout{view.width, view.height}.contentWidth());
    }
    if (plugin_page_.arranged_width != before) requestStoreHubArt();
}

// Tile pictures: those in memory go to their cards, the rest are fetched by one worker at a time.
void Launcher::requestStoreHubArt() {
    if (!plugin_page_.tiled || screen_ != ScreenMode::plugin_page || plugin_drawer_active_) return;
    auto& hub = store_hub_;
    std::vector<std::string> wanted;
    for (size_t i = 0; i < plugin_page_.rows.size() && i < cards_.size(); ++i) {
        const auto& row = plugin_page_.rows[i];
        if (!row.tile || row.image.empty()) continue;
        if (const auto found = hub.images.find(row.image); found != hub.images.end()) {
            if (!renderer_.hasCover(i)) { renderer_.setCover(i, found->second, {}, 400); cards_[i].cover_opacity = 1; }
        } else if (!hub.image_failed.contains(row.image) && !hub.image_pending.contains(row.image) &&
                   std::find(wanted.begin(), wanted.end(), row.image) == wanted.end()) wanted.push_back(row.image);
    }
    if (wanted.empty() || !hub.image_pending.empty()) return;
    hub.image_pending.insert(wanted.begin(), wanted.end());
    const auto alive = alive_; const auto hwnd = hwnd_;
    const bool started = work_.start([alive, hwnd, wanted, fetch = store_hub_image_, config = userConfigDirectory()] {
        for (const auto& image : wanted) {
            if (!alive->load()) return;
            auto bytes = fetch ? fetch(image) : store_hub::tileImage(image, config);
            auto message = std::make_unique<RemoteMessage>();
            message->run = [image, bytes = std::move(bytes)](Launcher& app) mutable {
                auto& hub = app.store_hub_;
                hub.image_pending.erase(image);
                if (bytes.empty()) hub.image_failed.insert(image);
                else hub.images[image] = shareImage(std::move(bytes));
                app.requestStoreHubArt(); // Its card, and the next batch once this one is through.
                InvalidateRect(app.hwnd_, nullptr, FALSE);
            };
            if (PostMessageW(hwnd, remote_message, 0, reinterpret_cast<LPARAM>(message.get()))) message.release();
        }
    });
    if (!started) hub.image_pending.clear();
}
