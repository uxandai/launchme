// Biblioteka → Ekran główny i kolekcje: one list of every row the home screen can have (Ostatnio grane,
// Platformy PC, Emulacja, the studios, the Steam rows, then the collections), each with a page of its own:
// on the home screen or not, a tab of Wszystkie gry or not, and what belongs to it (a collection's games,
// name, place, its Steam assignments; the studios' options; Emulacja's page).
//
// Where a row shows is kept in the settings the rest of the launcher already reads: a row's own switch
// when it has one (show_emulation, show_publishers, show_developers, show_updated_row,
// show_discovery_row: on while the row is anywhere), hidden_collections (a collection off the home
// screen), hidden_home_sections (any other row off it) and hidden_library_tabs (a tab left out).
#include "launcher.hpp"
#include "launcher_internal.hpp"
#include "../library/custom_games.hpp"
#include "../library/steam_sync_state.hpp"

#include <algorithm>

using settings_ui::Control;
using settings_ui::Id;
using settings_ui::Page;
using settings_ui::Row;

namespace {
size_t gamesIn(const std::vector<Game>* games, bool board) {
    if (!games) return 0;
    if (board) return games->size(); // The logos.
    return static_cast<size_t>(std::count_if(games->begin(), games->end(), [](const Game& game) { return isGame(game); }));
}
bool studioRow(const std::string& id) { return id == "publishers" || id == "developers"; }
} // namespace

const SteamCollection* Launcher::homeRowCollection(const std::string& id) const {
    const auto found = std::find_if(settings_collections_.begin(), settings_collections_.end(), [&](const SteamCollection& c) { return c.id == id; });
    return found == settings_collections_.end() ? nullptr : &*found;
}

std::wstring Launcher::homeRowTitle(const std::string& id) const {
    if (id == "recent") return tr(L"Ostatnio grane");
    if (id == "pc-platforms") return tr(L"Platformy PC");
    if (id == "emulation") return tr(L"Emulacja");
    if (id == "publishers") return settings_.show_companies ? tr(L"Firmy") : tr(L"Wydawcy");
    if (id == "developers") return tr(L"Developerzy");
    if (id == "updated") return tr(L"Ostatnio zaktualizowane");
    if (id == "discovery") return tr(L"Jeszcze niegrane");
    if (const auto* c = homeRowCollection(id)) return c->name;
    return {};
}

std::vector<Launcher::HomeRowEntry> Launcher::homeRowEntries() const {
    std::vector<HomeRowEntry> entries;
    const auto add = [&](const std::string& id, const SteamCollection* collection = nullptr) {
        const auto* category = findCategory(id);
        entries.push_back({id, homeRowTitle(id), collection, gamesIn(category ? &category->games : nullptr, studioRow(id))});
    };
    // In the home screen's own order (buildShelves).
    add("recent");
    if (findCategory("pc-platforms")) add("pc-platforms");
    if (!platform_games_.empty()) add("emulation");
    add("publishers");
    if (!settings_.show_companies) add("developers");
    for (const auto& collection : settings_collections_)
        if (!(srmCollectionsHidden() && srmCollection(collection))) add(collection.id, &collection); // SRM's: in Emulacja.
    if (steam_rows_available_) { add("updated"); add("discovery"); }
    return entries;
}

const bool* Launcher::homeRowMaster(const std::string& id) const {
    if (id == "emulation") return &settings_.show_emulation;
    if (id == "publishers") return settings_.show_companies ? nullptr : &settings_.show_publishers;
    if (id == "developers") return &settings_.show_developers;
    if (id == "updated") return &settings_.show_updated_row;
    if (id == "discovery") return &settings_.show_discovery_row;
    return nullptr;
}

bool Launcher::homeRowOnHome(const std::string& id) const {
    if (id == "recent") return true; // The home screen's first row in every layout.
    if (const auto* master = homeRowMaster(id); master && !*master) return false;
    if (const auto* c = homeRowCollection(id)) return !settings_.hidden_collections.contains(collectionKey(account_, *c));
    return !settings_.hidden_home_sections.contains(id);
}

bool Launcher::homeRowTab(const std::string& id) const {
    if (!homeRowHasTab(id)) return false;
    if (const auto* master = homeRowMaster(id); master && !*master) return false;
    return !settings_.hidden_library_tabs.contains(id);
}

std::wstring Launcher::homeRowPlace(const std::string& id) const {
    const bool home = homeRowOnHome(id), tab = homeRowTab(id);
    if (home && tab) return tr(L"Wszędzie");
    if (home) return tr(L"Ekran główny");
    if (tab) return tr(L"Zakładka");
    return tr(L"Ukryty");
}

void Launcher::setHomeRow(const std::string& id, bool home, bool tab) {
    if (id == "recent") home = true;
    if (!homeRowHasTab(id)) tab = false;
    if (const auto* master = homeRowMaster(id)) {
        // The row's own switch: on while it is anywhere. Off, its own marks go (it comes back everywhere).
        *const_cast<bool*>(master) = home || tab;
        if (!home && !tab) home = tab = true;
    }
    if (const auto* c = homeRowCollection(id)) {
        const auto key = collectionKey(account_, *c);
        if (home) settings_.hidden_collections.erase(key); else settings_.hidden_collections.insert(key);
    } else if (id != "recent") {
        if (home) settings_.hidden_home_sections.erase(id); else settings_.hidden_home_sections.insert(id);
    }
    if (homeRowHasTab(id)) {
        if (tab) settings_.hidden_library_tabs.erase(id); else settings_.hidden_library_tabs.insert(id);
    }
    settingsChanged(true); // The rows, the tabs and the desktop window's sections again.
    audio_.context();
}

void Launcher::toggleHomeRow(const std::string& id, bool tab) {
    const bool home = homeRowOnHome(id), shown = homeRowTab(id);
    if (tab) setHomeRow(id, home, !shown);
    else if (id == "recent") navigationBoundary(); // Always on the home screen.
    else setHomeRow(id, !home, shown);
}

// A collection one place up or down among the collections (the home screen's order and the tabs').
bool Launcher::moveHomeCollection(const std::string& id, int amount) {
    std::vector<size_t> listed; // The collections the list shows (not SRM's while Emulacja has their games).
    for (size_t i = 0; i < settings_collections_.size(); ++i)
        if (!(srmCollectionsHidden() && srmCollection(settings_collections_[i]))) listed.push_back(i);
    const auto at = std::find_if(listed.begin(), listed.end(), [&](size_t i) { return settings_collections_[i].id == id; });
    if (at == listed.end()) return false;
    const auto position = static_cast<int>(at - listed.begin()), next = position + amount;
    if (next < 0 || next >= static_cast<int>(listed.size())) { navigationBoundary(); return false; }
    std::swap(settings_collections_[listed[position]], settings_collections_[listed[next]]);
    for (const auto& c : settings_collections_) std::erase(settings_.collection_order, collectionKey(account_, c));
    for (const auto& c : settings_collections_) settings_.collection_order.push_back(collectionKey(account_, c));
    settingsChanged(true);
    audio_.navigate();
    return true;
}

void Launcher::homeRowsSettingsRows(const std::function<void(Row)>& add) {
    const auto description = [&](const HomeRowEntry& entry) -> std::wstring {
        std::wstring text;
        if (entry.id == "recent") text = tr(L"Pierwszy rząd ekranu głównego: gry w toku i ostatnio grane.");
        else if (entry.id == "pc-platforms") text = tr(L"Kafle sklepów i katalogu DRM-free.");
        else if (entry.id == "emulation") text = tr(L"Kafle konsol z grami z emulatorów.");
        else if (studioRow(entry.id)) text = entry.id == "developers" ? tr(L"Logo twórców gier z biblioteki.") : tr(L"Logo wydawców gier z biblioteki.");
        else if (entry.id == "updated") text = tr(L"Gry Steam zaktualizowane ostatnio.");
        else if (entry.id == "discovery") text = tr(L"Zainstalowane gry Steam, w które jeszcze nie grano.");
        else if (entry.collection) {
            text = localCollection(*entry.collection) ? tr(L"Własna kolekcja") : favoriteCollection(*entry.collection) ? tr(L"Gry z gwiazdką") : tr(L"Kolekcja Steam");
            if (library_state_.collections.creates.contains(collectionKey(account_, *entry.collection))) text += L" · " + std::wstring(tr(L"Oczekuje na synchronizację"));
        }
        if (!text.empty() && text.back() == L'.') text.pop_back(); // List lines read as labels: "… ostatnio grane · 8 gier".
        if (entry.games && entry.id != "pc-platforms" && entry.id != "emulation") {
            text += L" · " + (studioRow(entry.id) ? std::to_wstring(entry.games) + L" logo" : gamesLabel(entry.games));
        }
        return text;
    };
    bool collections = false, first = true;
    for (const auto& entry : homeRowEntries()) {
        if (entry.collection && !collections) {
            // Kolekcje: a new one first, then each in the home screen's order (Shift+↑↓, LB/RB move it).
            collections = true;
            Row create{Id::home_row_new, Control::action, tr(L"Nowa kolekcja"), tr(L"Kolekcja Steam albo własna; jej gry wybierzesz od razu."), tr(L"Utwórz")};
            create.section = tr(L"Kolekcje");
            add(std::move(create));
        }
        Row row{Id::home_row, Control::page, entry.title, description(entry), homeRowPlace(entry.id)};
        row.page = Page::home_row;
        row.key = entry.id;
        if (first) { row.section = tr(L"Rzędy"); first = false; }
        if (!entry.collection && collections) row.section = tr(L"Rzędy Steam"); // Konto bez kolekcji: after them.
        add(std::move(row));
    }
    if (!collections) {
        Row create{Id::home_row_new, Control::action, tr(L"Nowa kolekcja"), tr(L"Kolekcja Steam albo własna; jej gry wybierzesz od razu."), tr(L"Utwórz")};
        create.section = tr(L"Kolekcje");
        add(std::move(create));
    }
}

void Launcher::homeRowSettingsRows(const std::string& id, const std::function<void(Row)>& add) {
    const auto entries = homeRowEntries();
    const auto entry = std::find_if(entries.begin(), entries.end(), [&](const HomeRowEntry& item) { return item.id == id; });
    if (entry == entries.end()) {
        Row gone{Id::home_row_options, Control::action, tr(L"Tego rzędu już nie ma"), tr(L"Wróć do listy rzędów."), {}};
        gone.locked = true;
        add(std::move(gone));
        return;
    }
    // Where it shows.
    {
        Row home{Id::home_section, Control::toggle, tr(L"Na ekranie głównym"),
                 id == "recent" ? std::wstring(tr(L"Ostatnio grane są zawsze pierwszym rzędem ekranu głównego.")) :
                 id == "emulation" ? std::wstring(tr(L"Wyłączona: kolekcje Steam ROM Manager wracają jako zwykłe kolekcje.")) :
                 homeRowHasTab(id) ? std::wstring(tr(L"Rząd z kaflami. Wyłączony może zostać zakładką Wszystkich gier.")) :
                                     std::wstring(tr(L"Rząd z kaflami.")),
                 {}, homeRowOnHome(id)};
        home.key = id;
        home.locked = id == "recent";
        home.section = tr(L"Gdzie");
        add(std::move(home));
    }
    if (homeRowHasTab(id)) {
        Row tab{Id::library_tab, Control::toggle, tr(L"Zakładka we Wszystkich grach"),
                tr(L"Na górnym pasku Wszystkich gier (LB / RB). Przycisk View na tym rzędzie otwiera ją od razu."), {}, homeRowTab(id)};
        tab.key = id;
        add(std::move(tab));
    }
    if (id == "recent") {
        Row row{Id::recent_uninstalled, Control::toggle, tr(L"Niezainstalowane w Ostatnio granych"),
                tr(L"Także gry bez instalacji i dodane bez pliku, według daty dodania do biblioteki."), {}, settings_.recent_uninstalled};
        row.section = tr(L"Rząd");
        add(std::move(row));
    } else if (id == "emulation") {
        Row row{Id::home_row_options, Control::page, tr(L"Opcje emulacji"), tr(L"Folder ROM-ów, emulatory i systemy."), {}};
        row.page = Page::emulation;
        row.section = tr(L"Rząd");
        add(std::move(row));
    } else if (studioRow(id)) {
        Row companies{Id::studio_companies, Control::toggle, tr(L"Firmy"),
                      tr(L"Wydawcy i developerzy w jednej liście, bez powtórzeń logo i kategorii."), {}, settings_.show_companies};
        companies.section = tr(L"Rząd");
        add(std::move(companies));
        Row least{Id::studio_min_games, Control::choice, tr(L"Najmniej gier"),
                  tr(L"Studio pokazuje się, gdy ma tyle gier w bibliotece. Kolejność: najwięcej gier najpierw.")};
        for (const int count : settings_ui::studio_min_counts) least.choices.push_back({std::to_wstring(count)});
        for (size_t i = 0; i < settings_ui::studio_min_counts.size(); ++i)
            if (settings_ui::studio_min_counts[i] == settings_.studio_min_games) least.choice = static_cast<int>(i);
        least.value = std::to_wstring(settings_.studio_min_games);
        add(std::move(least));
        if (!settings_.hidden_studios.empty())
            add({Id::studio_hidden, Control::count, tr(L"Ukryci wydawcy i developerzy"),
                 tr(L"Ukryte w menu ich kafla albo w oknie desktopowym; tu można je przywrócić."), std::to_wstring(settings_.hidden_studios.size())});
    } else if (const auto* c = entry->collection) {
        const auto key = [&](Row row) { row.key = id; add(std::move(row)); };
        Row games{Id::home_row_games, Control::action, tr(L"Gry w kolekcji"), tr(L"Dodaj albo usuń gry; szukaj po tytule."),
                  entry->games ? gamesLabel(entry->games) : std::wstring(tr(L"Wybierz"))};
        games.section = tr(L"Kolekcja");
        key(std::move(games));
        if (!favoriteCollection(*c)) key({Id::home_row_rename, Control::action, tr(L"Zmień nazwę"), c->name, tr(L"Zmień")});
        // Its place among the collections (also Shift+↑↓ or LB/RB on the list).
        size_t position = 0, count = 0;
        for (const auto& item : entries) if (item.collection) { ++count; if (item.id == id) position = count; }
        const auto place = tr(L"Pozycja ") + std::to_wstring(position) + L" / " + std::to_wstring(count);
        Row up{Id::home_row_up, Control::action, tr(L"Przenieś wyżej"), place, tr(L"Wyżej")};
        up.locked = position <= 1;
        key(std::move(up));
        Row down{Id::home_row_down, Control::action, tr(L"Przenieś niżej"), place, tr(L"Niżej")};
        down.locked = position >= count;
        key(std::move(down));
        if (localCollection(*c)) {
            key({Id::home_row_steam, Control::action, tr(L"Przenieś do Steam"), tr(L"Kolekcja Steam synchronizuje się z klientem Steam na tym koncie."), tr(L"Przenieś")});
            key({Id::home_row_delete, Control::action, tr(L"Usuń kolekcję"), tr(L"Gry zostają w bibliotece; znika tylko kolekcja."), tr(L"Usuń")});
        } else {
            key({Id::home_row_reset, Control::action, tr(L"Przywróć przypisania Steam"),
                 tr(L"Cofa zmiany zrobione w Launchme: dodane i usunięte gry, nazwę."), home_row_confirm_ == id ? tr(L"Potwierdź") : tr(L"Przywróć")});
        }
    }
}

bool Launcher::homeRowActivate(const Row& row) {
    if (row.id != Id::home_row_reset) home_row_confirm_.clear();
    switch (row.id) {
    case Id::home_row_new:
        openCollectionManager();
        if (library_dialog_.open) {
            // Straight to its name; once saved the dialog closes on the new collection's games.
            auto& ui = library_dialog_;
            ui.steam_collection = account_ != 0;
            ui.name_return = library_manager_ui::Page::collections; ui.page = library_manager_ui::Page::name;
            ui.collection.clear(); ui.input.clear(); ui.focus = -2; ui.settings_return = true;
            rebuildCollectionDialog();
        }
        return true;
    case Id::home_row_games: openCollectionPage(row.key, false); return true;
    case Id::home_row_rename: openCollectionPage(row.key, true); return true;
    case Id::home_row_up: case Id::home_row_down:
        if (row.locked) { navigationBoundary(); return true; }
        moveHomeCollection(row.key, row.id == Id::home_row_up ? -1 : 1);
        return true;
    case Id::home_row_steam: {
        const auto* c = homeRowCollection(row.key);
        if (!c || !localCollection(*c)) return true;
        const auto local = c->id;
        const auto random = custom_games::newId({});
        const auto steam_id = "user-collections.uc-launchme-" + random;
        if (random.empty() || !steam_sync::moveCollection(library_state_, settings_, account_, local, steam_id)) {
            showNotification(NotificationKind::error, tr(L"Nie można przenieść kolekcji. Sprawdź konto Steam."), L"");
            return true;
        }
        if (library_category_ == local) library_category_ = steam_id;
        if (settings_.hidden_library_tabs.erase(local)) settings_.hidden_library_tabs.insert(steam_id);
        if (settings_state_.at(Page::home_row)) settings_state_.pages.back().key = steam_id; // The page follows it.
        settingsChanged(); collectionChanged(); audio_.context();
        return true;
    }
    case Id::home_row_delete:
        if (const auto* c = homeRowCollection(row.key)) askDeleteCollection(collectionKey(account_, *c));
        return true;
    case Id::home_row_reset: {
        const auto* c = homeRowCollection(row.key);
        if (!c || localCollection(*c)) return true;
        if (home_row_confirm_ != row.key) { home_row_confirm_ = row.key; audio_.context(); buildSettingsRows(); return true; }
        home_row_confirm_.clear();
        const auto scope = collectionKey(account_, *c);
        library_state_.collections.members.erase(scope);
        if (favoriteCollection(*c)) library_state_.collections.members.erase("favorites:local");
        if (!library_state_.collections.creates.contains(scope)) library_state_.collections.names.erase(scope);
        collectionChanged(); audio_.context();
        return true;
    }
    case Id::home_row_options:
        if (row.control == Control::page) openSettingsPage(row.page);
        else navigationBoundary();
        return true;
    default: return false;
    }
}

void Launcher::openCollectionPage(const std::string& id, bool rename) {
    const auto* c = homeRowCollection(id);
    if (!c) return;
    const auto name = c->name;
    openCollectionManager();
    auto& ui = library_dialog_;
    if (!ui.open) return;
    ui.collection = id; ui.settings_return = true; ui.input.clear(); ui.focus = -2;
    if (rename) {
        ui.name_return = library_manager_ui::Page::collection; ui.page = library_manager_ui::Page::name;
        ui.input = name; ui.selected = true; ui.close_after_save = true;
    } else ui.page = library_manager_ui::Page::games;
    rebuildCollectionDialog();
}
