#include "launcher.hpp"
#include "../library/search.hpp"
#include "../library/steam_sync_state.hpp"
#include "../platform/text_encoding.hpp"
#include <algorithm>

using library_manager_ui::Page;

bool Launcher::favoritesShown() const {
    return settings_.hidden_collections.empty() || !settings_.hidden_collections.contains(collectionKey(account_, desktopFavorites()));
}
bool Launcher::isFavorite(const Game& game) const {
    if (!favoritesShown()) return false;
    if (!game.version_group.empty()) if (const auto* copies = versions_.find(game))
        return std::any_of(copies->begin(), copies->end(), [&](const Game& copy) { return isFavorite(copy); });
    const auto found = std::find_if(settings_collections_.begin(), settings_collections_.end(), favoriteCollection);
    const SteamCollection fallback{tr(L"Ulubione"), {}, "user-collections.favorite"};
    return collectionContains(library_state_.collections, account_, found == settings_collections_.end() ? fallback : *found, game);
}
void Launcher::toggleFavorite(const Game& game) {
    if (!library_state_readable_) return;
    const Game copy = game;
    const auto found = std::find_if(settings_collections_.begin(), settings_collections_.end(), favoriteCollection);
    const SteamCollection fallback{tr(L"Ulubione"), {}, "user-collections.favorite"};
    const bool member = !isFavorite(copy);
    const auto& collection = found == settings_collections_.end() ? fallback : *found;
    if (!copy.version_group.empty() && versions_.find(copy)) {
        for (const auto& version : *versions_.find(copy)) setCollectionMember(library_state_.collections, account_, collection, version, member);
    } else setCollectionMember(library_state_.collections, account_, collection, copy, member);
    favorite_pulse_ = 1.0f;
    favorite_pulse_key_ = library_view::selectionKey(copy);
    collectionChanged();
}
int Launcher::favoriteDetailAction() const {
    if (screen_ != ScreenMode::details || cards_.empty() || cards_[0].game.kind == GameKind::catalog || !favoritesShown()) return -2;
    return std::max(1, identifyDetailAction()) + 1;
}
int Launcher::collectionsDetailAction() const {
    if (screen_ != ScreenMode::details || cards_.empty() || cards_[0].game.kind == GameKind::catalog) return -2;
    const int favorite = favoriteDetailAction(); // Without Ulubione: its place.
    return favorite >= 0 ? favorite + 1 : std::max(1, identifyDetailAction()) + 1;
}
std::string Launcher::hiddenKey(const Game& game) const {
    const auto key = gameKey(game);
    return !key.empty() && game.source == GameSource::steam ? "steam-user:" + std::to_string(account_) + '/' + key : key;
}
bool Launcher::locallyHidden(const Game& game) const {
    if (game.source == GameSource::steam) {
        if (const auto pending = library_state_.steam_hidden_pending.find(hiddenKey(game)); pending != library_state_.steam_hidden_pending.end()) return pending->second;
    }
    return (!library_state_.hidden.empty() && library_state_.hidden.contains(hiddenKey(game))) ||
        (!settings_.show_private && hiddenInSteam(game));
}
std::vector<Game> Launcher::hideableCopies(const Game& game) const {
    std::vector<Game> copies;
    if (!game.version_group.empty()) if (const auto* group = versions_.find(game)) copies = *group;
    if (copies.empty()) copies = {game};
    for (const auto& copy : copies) {
        if (!isGame(copy) || copy.kind == GameKind::catalog || copy.kind == GameKind::nonsteam || gameKey(copy).empty() ||
            (copy.source == GameSource::steam && excluded_ids_.contains(copy.app_id) && !hiddenInSteam(copy)) ||
            (copy.visibility != GameVisibility::public_game && !locallyHidden(copy) && !hiddenInSteam(copy))) return {};
    }
    return copies;
}
bool Launcher::hiddenInLauncher(const Game& game) const {
    const auto copies = hideableCopies(game);
    return !copies.empty() && std::all_of(copies.begin(), copies.end(), [&](const Game& copy) { return locallyHidden(copy) || hiddenInSteam(copy); });
}
void Launcher::toggleHidden(const Game& original) {
    const Game game = original; // Often a card: rebuilding the shelves below replaces it.
    if (!library_state_readable_) { showNotification(NotificationKind::error, tr(L"Nie można odczytać historii biblioteki"), L""); return; }
    const auto copies = hideableCopies(game);
    if (copies.empty()) return;
    const bool hide = !hiddenInLauncher(game);
    for (const auto& copy : copies) {
        if (copy.source == GameSource::steam) library_state_.steam_hidden_pending[hiddenKey(copy)] = hide;
        if (hide) library_state_.hidden.insert(hiddenKey(copy));
        else library_state_.hidden.erase(hiddenKey(copy));
    }
    library_state_dirty_ = true;
    requestSteamSync();
    audio_.context();
    // How to get it back, also without a keyboard.
    if (hide) showNotification(NotificationKind::launch, tr(L"Gra ukryta"), tr(L"Wróci: Ustawienia → Biblioteka → Pokaż ukryte/prywatne gry, albo Ctrl+H."));
    if (screen_ == ScreenMode::details && hide && !private_shelf_) {
        // Hidden games are not shown now: back to where the details were opened, without it.
        back();
        refreshRecentGames();
        if (screen_ == ScreenMode::library && cards_.empty()) showCategory(category_); // Its last game there.
        return;
    }
    if (screen_ == ScreenMode::details && !detail_games_.empty()) {
        // Ctrl+H on (or shown again): the details stay; Ctrl+H off later leaves a hidden game.
        auto& visibility = detail_games_[0].visibility;
        if (hide) visibility = GameVisibility::hidden;
        else if (visibility == GameVisibility::hidden || game.source == GameSource::steam) visibility = GameVisibility::public_game;
        if (!cards_.empty()) cards_[0].game.visibility = visibility;
    }
    refreshRecentGames(); // Also gives a grouped game its new version group.
    InvalidateRect(hwnd_, nullptr, FALSE);
}
bool Launcher::canExclude(const Game& game) const {
    if (!isGame(game) || game.kind == GameKind::catalog || gameKey(game).empty()) return false;
    return !(game.source == GameSource::custom && !game.source_id.empty()); // Added by hand: Usuń z biblioteki.
}
bool Launcher::excludedGame(const Game& game) const {
    return !library_state_.excluded_games.empty() && canExclude(game) && library_state_.excluded_games.contains(gameKey(game));
}
void Launcher::askExcludeGame(const Game& game) {
    if (!canExclude(game)) return;
    dismissMenu();
    exclude_game_ = game;
    prompt_kind_ = PromptKind::exclude;
    exit_prompt_ = 2; // Anuluj: the safe default.
    audio_.context();
    InvalidateRect(hwnd_, nullptr, FALSE);
}
void Launcher::excludeGame(const Game& original) {
    const Game game = original;
    if (!canExclude(game)) return;
    if (!library_state_readable_) { showNotification(NotificationKind::error, tr(L"Nie można odczytać historii biblioteki"), L""); return; }
    library_state_.excluded_games[gameKey(game)] = game.name.substr(0, 200);
    library_state_.hidden.erase(gameKey(game));
    library_state_dirty_ = true;
    saveLibraryPreferences();
    dismissMenu();
    if (screen_ == ScreenMode::details) back();
    refreshRecentGames();
    audio_.context();
    showNotification(NotificationKind::launch, tr(L"Gra wykluczona"), tr(L"Przywrócisz ją w Biblioteka → Widoczność → Wykluczone gry."));
}
void Launcher::restoreExcludedGame(const std::string& key) {
    if (!library_state_.excluded_games.erase(key)) return;
    library_state_dirty_ = true;
    saveLibraryPreferences();
    refreshRecentGames();
    audio_.context();
}
void Launcher::openExcludedGames(GameSource source) {
    if (source != GameSource::gog && source != GameSource::epic) return;
    if (!library_state_readable_) { showNotification(NotificationKind::error, tr(L"Nie można odczytać historii biblioteki"), L""); return; }
    library_dialog_ = {}; library_dialog_.open = true; library_dialog_.account = account_;
    library_dialog_.page = Page::excluded; library_dialog_.game.source = source;
    dismissMenu(); closeFriends(); closeQuick(); closeCategoryMenu();
    rebuildCollectionDialog(); audio_.drawer();
}
void Launcher::collectionChanged() {
    library_state_dirty_ = true;
    requestSteamSync();
    refreshRecentGames(); // Rebuilds shelves without disturbing the details/dialog underneath.
    if (libraryDialogOpen()) rebuildCollectionDialog();
    InvalidateRect(hwnd_, nullptr, FALSE);
}
// An own collection (local:…) goes: its games' membership and its name, its place in the order and its
// visibility. Steam's collections cannot be deleted here (the dialog restores their Steam assignments).
void Launcher::deleteCollection(const SteamCollection& collection) {
    if (!localCollection(collection) || !library_state_readable_) return;
    const auto scope = collectionKey(account_, collection);
    library_state_.collections.members.erase(scope);
    if (!library_state_.collections.creates.contains(scope)) library_state_.collections.names.erase(scope);
    settings_.hidden_collections.erase(scope); std::erase(settings_.collection_order, scope);
    // Its library's own view, order and filter ("<account>:category:<id>", libraryScope) go too.
    const auto library = "category:" + collection.id;
    std::erase_if(library_state_.options, [&](const auto& entry) {
        const auto colon = entry.first.find(':');
        return colon != std::string::npos && std::string_view(entry.first).substr(colon + 1) == library;
    });
    settingsChanged();
    collectionChanged();
}
// Its trash in Ustawienia → Biblioteka → Kolekcje (also Delete and the pad's X there): a question first,
// Anuluj with the focus; hiding it on the home screen is the gentler answer.
void Launcher::askDeleteCollection(const std::string& key) {
    const auto found = std::find_if(settings_collections_.begin(), settings_collections_.end(),
                                    [&](const SteamCollection& c) { return collectionKey(account_, c) == key; });
    if (found == settings_collections_.end() || !localCollection(*found)) return;
    delete_collection_key_ = key;
    prompt_kind_ = PromptKind::delete_collection;
    exit_prompt_ = 2;
    audio_.context(); InvalidateRect(hwnd_, nullptr, FALSE);
}
void Launcher::openCollectionManager(std::optional<Game> game) {
    if (!library_state_readable_) { showNotification(NotificationKind::error, tr(L"Nie można odczytać historii biblioteki"), L""); return; }
    library_dialog_ = {}; library_dialog_.open = true; library_dialog_.account = account_;
    if (game) { library_dialog_.game = *game; library_dialog_.page = Page::membership; }
    dismissMenu(); closeFriends(); closeQuick(); closeCategoryMenu();
    rebuildCollectionDialog(); audio_.drawer();
}
void Launcher::rebuildCollectionDialog() {
    auto& ui = library_dialog_;
    if (!ui.open) return;
    if (ui.account != account_) { ui = {}; return; }
    if (ui.page == Page::versions) return;
    ui.rows.clear();
    const auto add = [&](std::wstring title, std::string key, std::wstring subtitle = {}, bool toggle = false, bool checked = false) {
        ui.rows.push_back({std::move(title), std::move(subtitle), std::move(key), toggle, checked});
    };
    const auto found = std::find_if(settings_collections_.begin(), settings_collections_.end(), [&](const auto& c) { return c.id == ui.collection; });
    if ((ui.page == Page::collection || ui.page == Page::games) && found == settings_collections_.end()) ui.page = Page::collections;
    if (ui.page == Page::collections || ui.page == Page::membership) {
        ui.title = ui.page == Page::membership ? ui.game.name : tr(L"Zarządzaj kolekcjami");
        add(tr(L"Nowa kolekcja"), "@new");
        std::map<std::string, size_t> counts;
        for (const auto& shelf : managedShelves(account_, settings_collections_, library_state_.collections, public_library_, private_shelf_)) counts[shelf.id] = shelf.games.size();
        for (const auto& c : settings_collections_) {
            const auto count = counts[c.id];
            auto origin = localCollection(c) ? std::wstring(tr(L"Własna kolekcja")) : favoriteCollection(c) ? std::wstring{} : std::wstring(tr(L"Kolekcja Steam"));
            if (library_state_.collections.creates.contains(collectionKey(account_, c))) origin += L" · " + std::wstring(tr(L"Oczekuje na synchronizację"));
            bool member = ui.page == Page::membership && collectionContains(library_state_.collections, account_, c, ui.game);
            if (ui.page == Page::membership && !ui.game.version_group.empty()) if (const auto* copies = versions_.find(ui.game))
                member = std::any_of(copies->begin(), copies->end(), [&](const Game& game) { return collectionContains(library_state_.collections, account_, c, game); });
            add(c.name, c.id, (origin.empty() ? L"" : origin + L" · ") + std::to_wstring(count), ui.page == Page::membership,
                member);
        }
    } else if (ui.page == Page::collection) {
        const auto c = *found;
        ui.title = c.name;
        add(tr(L"Zarządzaj grami"), "@games");
        if (!favoriteCollection(c)) add(tr(L"Zmień nazwę"), "@rename");
        if (localCollection(c)) add(tr(L"Przenieś do Steam"), "@steam");
        add(tr(L"Pokaż na ekranie głównym"), "@visible", {}, true, !settings_.hidden_collections.contains(collectionKey(account_, c)));
        add(tr(L"Przenieś wyżej"), "@up");
        add(tr(L"Przenieś niżej"), "@down");
        if (localCollection(c)) add(ui.confirm ? tr(L"Potwierdź usunięcie") : tr(L"Usuń kolekcję"), "@delete");
        else add(ui.confirm ? tr(L"Potwierdź") : tr(L"Przywróć przypisania Steam"), "@reset");
    } else if (ui.page == Page::games) {
        ui.title = found->name;
        const auto query = foldGameTitle(ui.input);
        std::unordered_set<std::string> members;
        for (const auto& shelf : managedShelves(account_, {*found}, library_state_.collections, public_library_, private_shelf_))
            for (const auto& game : shelf.games) members.insert(gameKey(game));
        std::vector<const Game*> games;
        for (const auto& game : public_library_)
            if (isGame(game) && game.kind != GameKind::catalog && (query.empty() || titleMatch(foldGameTitle(game.name), query))) games.push_back(&game);
        std::stable_sort(games.begin(), games.end(), [](auto a, auto b) { return compareTitles(a->name, b->name) < 0; });
        for (const auto* game : games) add(game->name, gameKey(*game), sourceName(game->source), true, members.contains(gameKey(*game)));
    } else if (ui.page == Page::excluded) {
        ui.title = std::wstring(tr(L"Wykluczone gry")) + L" · " + sourceName(ui.game.source);
        const auto prefix = std::string(sourceSlug(ui.game.source)) + ':';
        const auto query = foldGameTitle(ui.input);
        for (const auto& [key, name] : library_state_.excluded_games) {
            if (!key.starts_with(prefix)) continue;
            const auto title = name.empty() ? text_encoding::wide(key) : name;
            if (query.empty() || titleMatch(foldGameTitle(title), query)) add(title, key, tr(L"Usuń z wykluczonych"));
        }
        std::stable_sort(ui.rows.begin(), ui.rows.end(), [](const auto& a, const auto& b) { return compareTitles(a.title, b.title) < 0; });
    } else if (ui.page == Page::name) {
        ui.title = ui.collection.empty() ? tr(L"Nowa kolekcja") : tr(L"Zmień nazwę");
        if (ui.collection.empty()) {
            add(tr(L"Kolekcja Steam"), "@type-steam", tr(L"Działa offline; synchronizuje po połączeniu."), true, ui.steam_collection);
            ui.rows.back().radio = true;
            add(tr(L"Własna kolekcja"), "@type-local", tr(L"Tylko w Launchme"), true, !ui.steam_collection);
            ui.rows.back().radio = true;
        }
        add(tr(L"Zapisz"), "@save");
    }
    ui.focus = std::clamp(ui.focus, ui.textField() ? -2 : -1, std::max(-1, static_cast<int>(ui.rows.size()) - 1));
    const auto view = renderer_.viewport(); const library_manager_ui::Layout layout{view.width, view.height};
    const int capacity = layout.capacity(ui);
    if (ui.focus >= 0) ui.scroll = std::clamp(ui.scroll, std::max(0, ui.focus - capacity + 1), ui.focus);
    ui.scroll = std::clamp(ui.scroll, 0, std::max(0, static_cast<int>(ui.rows.size()) - capacity));
    InvalidateRect(hwnd_, nullptr, FALSE);
}
void Launcher::collectionDialogAccept() {
    auto& ui = library_dialog_;
    if (!ui.open || ui.account != account_) { ui = {}; return; }
    if (ui.page == Page::versions) { chooseVersion(); return; }
    if (ui.focus == -1) { collectionDialogBack(); return; }
    if (ui.focus == -2) { ui.focus = 0; rebuildCollectionDialog(); return; }
    if (ui.focus < 0 || ui.focus >= static_cast<int>(ui.rows.size())) return;
    const auto row = ui.rows[ui.focus];
    if (ui.page == Page::excluded) {
        if (!library_state_readable_ || !row.key.starts_with(std::string(sourceSlug(ui.game.source)) + ':')) return;
        library_state_.excluded_games.erase(row.key);
        library_state_dirty_ = true;
        refreshRecentGames(); audio_.context();
        return;
    }
    const auto assign = [&](const SteamCollection& c, bool member) {
        if (!ui.game.version_group.empty() && versions_.find(ui.game)) {
            for (const auto& game : *versions_.find(ui.game)) setCollectionMember(library_state_.collections, account_, c, game, member);
        } else setCollectionMember(library_state_.collections, account_, c, ui.game, member);
    };
    if (row.key == "@new") {
        ui.steam_collection = account_ != 0 && (ui.page != Page::membership || ui.game.source == GameSource::steam);
        ui.name_return = ui.page; ui.page = Page::name; ui.collection.clear(); ui.input.clear(); ui.focus = -2;
    } else if (ui.page == Page::name && ui.collection.empty() && (row.key == "@type-steam" || row.key == "@type-local")) {
        ui.steam_collection = row.key == "@type-steam";
        ui.error.clear();
    } else if (ui.page == Page::collections) {
        ui.collection = row.key; ui.page = Page::collection; ui.focus = 0;
    } else if (ui.page == Page::name && row.key == "@save") {
        const auto name = custom_games::trim(ui.input);
        if (name.empty() || name.size() > 200) { ui.error = tr(L"Wpisz nazwę kolekcji"); rebuildCollectionDialog(); return; }
        std::string id = ui.collection;
        if (id.empty()) {
            if (ui.steam_collection && !account_) { ui.error = tr(L"Brak zapamiętanego konta Steam"); rebuildCollectionDialog(); return; }
            const auto random = custom_games::newId({});
            if (random.empty()) { ui.error = tr(L"Nie można utworzyć kolekcji"); rebuildCollectionDialog(); return; }
            id = (ui.steam_collection ? "user-collections.uc-launchme-" : "local:") + random;
            const auto scope = collectionKey(account_, SteamCollection{name, {}, id});
            if (library_state_.collections.names.contains(scope)) return;
            if (ui.steam_collection) library_state_.collections.creates.insert(scope);
        }
        SteamCollection c{name, {}, id};
        library_state_.collections.names[collectionKey(account_, c)] = name;
        if (ui.name_return == Page::membership) assign(c, true);
        for (const auto& game : ui.more_games) { // Desktop mode: the other games dropped or marked with it.
            if (!game.version_group.empty() && versions_.find(game)) {
                for (const auto& copy : *versions_.find(game)) setCollectionMember(library_state_.collections, account_, c, copy, true);
            } else setCollectionMember(library_state_.collections, account_, c, game, true);
        }
        if (ui.close_after_save) {
            const bool created = ui.collection.empty();
            ui = {};
            mouse_transition_until_ = GetTickCount64() + GetDoubleClickTime();
            collectionChanged();
            saveLibraryPreferences(); saveSettings();
            if (created) {
                showNotification(NotificationKind::launch, tr(L"Utworzono kolekcję"), name);
                if (desktop_mode_) desktopShowSection("collection:" + id);
            }
            return;
        }
        ui.page = ui.name_return == Page::membership ? Page::membership : Page::collection;
        ui.collection = id; ui.input.clear(); ui.error.clear(); ui.focus = 0;
        collectionChanged(); return;
    } else {
        const std::string id = ui.page == Page::membership ? row.key : ui.collection;
        const auto found = std::find_if(settings_collections_.begin(), settings_collections_.end(), [&](const auto& c) { return c.id == id; });
        if (found == settings_collections_.end()) return;
        const auto c = *found;
        const auto scope = collectionKey(account_, c);
        if (ui.page == Page::membership) {
            assign(c, !row.checked);
            collectionChanged(); return;
        }
        if (ui.page == Page::games) {
            const auto game = std::find_if(public_library_.begin(), public_library_.end(), [&](const Game& item) { return gameKey(item) == row.key; });
            if (game != public_library_.end()) setCollectionMember(library_state_.collections, account_, c, *game, !row.checked);
            collectionChanged(); return;
        }
        if (row.key == "@games") { ui.page = Page::games; ui.input.clear(); ui.focus = -2; }
        else if (row.key == "@steam" && localCollection(c)) {
            const auto random = custom_games::newId({});
            const auto steam_id = "user-collections.uc-launchme-" + random;
            if (random.empty() || !steam_sync::moveCollection(library_state_, settings_, account_, c.id, steam_id)) {
                ui.error = tr(L"Nie można przenieść kolekcji. Sprawdź konto Steam."); rebuildCollectionDialog(); return;
            }
            if (library_category_ == c.id) library_category_ = steam_id;
            ui.collection = steam_id; ui.focus = 0; ui.confirm = false;
            settingsChanged(); collectionChanged(); return;
        }
        else if (row.key == "@rename") { ui.name_return = Page::collection; ui.page = Page::name; ui.input = c.name; ui.selected = true; ui.focus = -2; }
        else if (row.key == "@visible") {
            if (!settings_.hidden_collections.erase(scope)) settings_.hidden_collections.insert(scope);
            settingsChanged(true);
        } else if (row.key == "@up" || row.key == "@down") {
            const auto index = static_cast<int>(found - settings_collections_.begin());
            const int next = index + (row.key == "@up" ? -1 : 1);
            if (next >= 0 && next < static_cast<int>(settings_collections_.size())) {
                std::swap(settings_collections_[index], settings_collections_[next]);
                for (const auto& item : settings_collections_) std::erase(settings_.collection_order, collectionKey(account_, item));
                for (const auto& item : settings_collections_) settings_.collection_order.push_back(collectionKey(account_, item));
                settingsChanged(true);
            }
        } else if (row.key == "@delete" || row.key == "@reset") {
            if (!ui.confirm) ui.confirm = true;
            else if (localCollection(c)) {
                ui.page = Page::collections; ui.focus = 0; ui.confirm = false;
                deleteCollection(c); return;
            } else {
                library_state_.collections.members.erase(scope);
                if (favoriteCollection(c)) library_state_.collections.members.erase("favorites:local");
                if (!library_state_.collections.creates.contains(scope)) library_state_.collections.names.erase(scope);
                ui.confirm = false; collectionChanged(); return;
            }
        }
    }
    rebuildCollectionDialog();
}
void Launcher::collectionDialogBack() {
    auto& ui = library_dialog_;
    if (ui.page == Page::name) ui.page = ui.name_return;
    else if (ui.page == Page::games) ui.page = Page::collection;
    else if (ui.page == Page::collection) ui.page = Page::collections;
    else { ui = {}; saveLibraryPreferences(); saveSettings(); mouse_transition_until_ = GetTickCount64() + GetDoubleClickTime(); InvalidateRect(hwnd_, nullptr, FALSE); return; }
    ui.input.clear(); ui.error.clear(); ui.confirm = false; ui.focus = 0; ui.scroll = 0; rebuildCollectionDialog();
}
void Launcher::collectionDialogEdit(wchar_t ch) {
    auto& ui = library_dialog_;
    if (!ui.textField() || ui.focus != -2 || ch < L' ' || ch == 127) return;
    if (ui.selected) { ui.input.clear(); ui.selected = false; }
    if (ui.input.size() < 200) ui.input += ch;
    ui.error.clear(); ui.scroll = 0; rebuildCollectionDialog();
}
void Launcher::libraryDialogKey(WPARAM key, bool repeated) {
    auto& ui = library_dialog_;
    if (!ui.open) return;
    if (key == VK_ESCAPE) { collectionDialogBack(); return; }
    if (key == VK_F11 && !repeated) { toggleFullscreen(); return; }
    if (ui.page == Page::versions) {
        if ((key == VK_RETURN || key == VK_SPACE) && !repeated) { chooseVersion(); return; }
        if (key == VK_BACK) { collectionDialogBack(); return; }
        const int count = static_cast<int>(ui.versions.size());
        const int delta = key == VK_LEFT ? -1 : key == VK_RIGHT || key == VK_TAB ? 1 : key == VK_UP ? -2 : key == VK_DOWN ? 2 : 0;
        if (delta) {
            ui.focus = std::clamp(ui.focus + delta, -1, count);
            const auto view = renderer_.viewport(); const int capacity = library_manager_ui::Layout::of(view.width, view.height, ui).versionCapacity();
            if (ui.focus >= 0 && ui.focus < count) {
                if (ui.focus < ui.scroll) ui.scroll = ui.focus / 2 * 2;
                if (ui.focus >= ui.scroll + capacity) ui.scroll = (ui.focus / 2 - capacity / 2 + 1) * 2;
            }
        }
        InvalidateRect(hwnd_, nullptr, FALSE); return;
    }
    if (key == VK_BACK && ui.focus != -2) { collectionDialogBack(); return; }
    if (ui.textField() && ui.focus == -2) {
        if (key == 'A' && (GetKeyState(VK_CONTROL) & 0x8000)) ui.selected = true;
        else if (key == VK_BACK || key == VK_DELETE) {
            if (ui.selected || key == VK_DELETE) ui.input.clear(); else if (!ui.input.empty()) ui.input.pop_back();
            ui.selected = false; rebuildCollectionDialog();
        } else if (key == 'V' && (GetKeyState(VK_CONTROL) & 0x8000) && OpenClipboard(hwnd_)) {
            if (const auto handle = GetClipboardData(CF_UNICODETEXT)) {
                if (const auto* text = static_cast<const wchar_t*>(GlobalLock(handle))) {
                    const size_t limit = std::min<size_t>(GlobalSize(handle) / sizeof(wchar_t), 200);
                    for (size_t i = 0; i < limit && text[i]; ++i) collectionDialogEdit(text[i]);
                    GlobalUnlock(handle);
                }
            }
            CloseClipboard();
        }
    }
    if ((key == VK_RETURN || (key == VK_SPACE && ui.focus != -2)) && !repeated) collectionDialogAccept();
    else if (key == VK_DOWN || key == VK_UP || key == VK_TAB) {
        const int direction = key == VK_UP || (key == VK_TAB && (GetKeyState(VK_SHIFT) & 0x8000)) ? -1 : 1;
        // Back precedes the input, then the list.
        if (ui.textField() && ui.focus == -1 && direction > 0) ui.focus = -2;
        else if (ui.textField() && ui.focus == -2) ui.focus = direction > 0 ? 0 : -1;
        else if (ui.textField() && ui.focus == 0 && direction < 0) ui.focus = -2;
        else ui.focus = std::clamp(ui.focus + direction, -1, std::max(-1, static_cast<int>(ui.rows.size()) - 1));
        ui.confirm = false; rebuildCollectionDialog();
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}
void Launcher::collectionDialogClick(float x, float y) {
    const auto view = renderer_.viewport();
    const int hit = library_manager_ui::Layout::of(view.width, view.height, library_dialog_).hit(library_dialog_, x, y);
    if (hit == -3) return;
    if (hit != library_dialog_.focus) library_dialog_.confirm = false;
    library_dialog_.focus = hit;
    if (hit != -2) collectionDialogAccept(); else InvalidateRect(hwnd_, nullptr, FALSE);
}
