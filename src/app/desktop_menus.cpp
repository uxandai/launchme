// Desktop mode, the menus (game, submenus, sections, sort, collections, store) with their keys and
// actions, and collections: membership, drops, order, new ones and their dialog.
#include "launcher.hpp"
#include "launcher_internal.hpp"
#include "../library/organization.hpp"

#include <algorithm>

using namespace desktop_ui;
using library_manager_ui::Page;

// ---- Menus -----------------------------------------------------------------------------------

std::vector<desktop_ui::MenuItem> Launcher::desktopCollectionItems(const std::vector<Game>& games) const {
    std::vector<MenuItem> items;
    for (const auto& c : settings_collections_) {
        if (favoriteCollection(c) || (srmCollectionsHidden() && srmCollection(c))) continue;
        MenuItem item{Action::collection_toggle, c.name, {}, glyph::folder};
        item.key = c.id;
        item.checked = !games.empty() && std::all_of(games.begin(), games.end(), [&](const Game& game) { return desktopMember(game, c); });
        items.push_back(std::move(item));
    }
    if (!items.empty()) { MenuItem separator; separator.separator = true; items.push_back(separator); }
    items.push_back({Action::collection_new, tr(L"Nowa kolekcja…"), {}, glyph::add});
    items.push_back({Action::collection_manage, tr(L"Zarządzaj kolekcjami…"), {}, glyph::edit});
    return items;
}

void Launcher::desktopOpenGameMenu(float x, float y, bool keyboard) {
    const auto games = desktopTargets();
    if (games.empty()) return;
    desktop_downloads_open_ = false;
    Menu menu;
    menu.kind = Menu::Kind::game;
    menu.x = x; menu.y = y; menu.keyboard = keyboard;
    for (const auto& game : games) menu.games.push_back(library_view::selectionKey(game));
    auto& items = menu.items;
    const auto separator = [&] { if (!items.empty() && !items.back().separator) { MenuItem line; line.separator = true; items.push_back(line); } };
    if (games.size() == 1) {
        const auto& game = games.front();
        // "Enter" beside what Enter does on the card: launch, or its details (the default game action).
        const bool enter_details = settings_.accept_action == AppSettings::AcceptAction::details;
        MenuItem play{Action::play, actionLabel(game), enter_details ? std::wstring{} : std::wstring(tr(L"Enter")), glyph::play};
        play.enabled = actionEnabled(game) && !pending_launch_;
        items.push_back(std::move(play));
        const auto session = sessionState(game);
        if (settings_.quick_resume && session == SessionState::running) items.push_back({Action::suspend, tr(L"Wstrzymaj"), {}, glyph::pause});
        if (session != SessionState::none) items.push_back({Action::close_game, tr(L"Zamknij grę"), {}, glyph::stop});
        separator();
        const bool favorite = isFavorite(game);
        if (favoritesShown()) items.push_back({Action::favorite, favorite ? tr(L"Usuń z ulubionych") : tr(L"Dodaj do ulubionych"), {}, favorite ? glyph::star_fill : glyph::star});
        MenuItem collections{Action::collections, tr(L"Kolekcje"), {}, glyph::folder};
        collections.submenu = true;
        items.push_back(std::move(collections));
        if (const auto* group = versions_.find(game); group && group->size() > 1) items.push_back({Action::versions, tr(L"Wybierz wersję…"), {}, glyph::sync});
        separator();
        items.push_back({Action::details, tr(L"Szczegóły"), enter_details ? std::wstring(tr(L"Enter")) : std::wstring{}, glyph::info});
        // The rest in three groups, each a submenu: the game's data and art, how it starts, and the extras.
        const wchar_t* group_names[] = {trKey(L"Dane i grafika"), trKey(L"Opcje gry"), trKey(L"Więcej")};
        const wchar_t group_icons[] = {glyph::picture, glyph::settings, glyph::more};
        for (int group = 0; group < 3; ++group) {
            if (desktopGroupItems(game, group).empty()) continue;
            MenuItem item{Action::group, tr(group_names[group]), {}, group_icons[group]};
            item.submenu = true;
            item.value = group;
            items.push_back(std::move(item));
        }
        separator();
        if (!hideableCopies(game).empty()) {
            const bool hidden = hiddenInLauncher(game);
            items.push_back({Action::hide, hidden ? tr(L"Usuń z ukrytych") : tr(L"Ukryj"), {}, hidden ? glyph::view : glyph::hide});
        }
        if (canExclude(game)) { MenuItem exclude{Action::exclude, tr(L"Wyklucz z biblioteki"), {}, glyph::remove}; exclude.danger = true; items.push_back(std::move(exclude)); }
        if (canUninstall(game) && session == SessionState::none) { // Never while it runs or is suspended.
            if (hideableCopies(game).empty() && !canExclude(game)) separator();
            MenuItem uninstall{Action::uninstall, tr(L"Odinstaluj…"), {}, glyph::remove};
            uninstall.danger = true;
            items.push_back(std::move(uninstall));
        }
        if (canRemoveFromLibrary(game) && session == SessionState::none) { // A game added by hand: its entry, after a question.
            if (hideableCopies(game).empty() && !canExclude(game)) separator();
            MenuItem remove{Action::remove_game, tr(L"Usuń z biblioteki…"), {}, glyph::remove};
            remove.danger = true;
            items.push_back(std::move(remove));
        }
    } else {
        MenuItem title{Action::none, std::wstring(tr(L"Zaznaczone: ")) + gamesLabel(games.size()), {}, glyph::check};
        title.enabled = false;
        items.push_back(std::move(title));
        separator();
        if (favoritesShown()) items.push_back({Action::favorite, tr(L"Dodaj do ulubionych"), {}, glyph::star});
        MenuItem collections{Action::collections, tr(L"Kolekcje"), {}, glyph::folder};
        collections.submenu = true;
        items.push_back(std::move(collections));
        // Every selected game's missing or failed artwork asked for again (SteamGridDB, Epic's own art).
        items.push_back({Action::retry_art, tr(L"Pobierz grafiki ponownie"), {}, glyph::refresh});
        items.push_back({Action::missing_art, tr(L"Pobierz brakujące okładki…"), {}, glyph::picture});
        if (std::all_of(games.begin(), games.end(), [&](const Game& game) { return !hideableCopies(game).empty(); }))
            items.push_back({Action::hide, tr(L"Ukryj zaznaczone"), {}, glyph::hide});
        separator();
        items.push_back({Action::clear_marks, tr(L"Wyczyść zaznaczenie"), tr(L"Esc"), glyph::close});
    }
    while (!items.empty() && items.back().separator) items.pop_back();
    if (keyboard) menu.hover = stepItem(items, -1, 1);
    desktop_menu_ = std::move(menu);
    desktop_tooltip_ = 0;
    InvalidateRect(hwnd_, nullptr, FALSE);
}

// The single-game menu's groups (submenus): 0 Dane i grafika, 1 Opcje gry, 2 Więcej.
std::vector<MenuItem> Launcher::desktopGroupItems(const Game& game, int group) {
    std::vector<MenuItem> items;
    if (group == 0) {
        items.push_back({Action::artwork, tr(L"Zmień grafikę…"), {}, glyph::picture});
        items.push_back({Action::retry_art, tr(L"Pobierz grafiki ponownie"), {}, glyph::refresh});
        if (!gameKey(game).empty()) items.push_back({Action::metadata, tr(L"Edytuj metadane…"), {}, glyph::edit}); // Own title and details.
    } else if (group == 1) {
        if (aurelia_active_ && aurelia::validGame(game)) items.push_back({Action::aurelia, tr(L"Zarządzaj · Aurelia"), {}, glyph::edit});
        // The file, options and folder it starts with (Steam: its launch options).
        if (steamAccountChoices(game).size() > 1) items.push_back({Action::steam_account, tr(L"Konto Steam: ") + steamAccountLabel(game), {}, glyph::people});
        if (gameEmulatorEditable(game)) items.push_back({Action::emulator, gameEmulatorLabel(game), {}, glyph::game}); // Emulator per gra: the next choice.
        if (launchEditable(game)) // A game added by hand: the file, name and options are its own editor.
            items.push_back({Action::launch_options, game.source == GameSource::custom ? tr(L"Edytuj grę…") : tr(L"Opcje uruchamiania…"), {}, glyph::settings});
        if (compatGame(game) && !gameKey(game).empty()) items.push_back({Action::compat_options, L"Proton / Wine…", {}, glyph::settings});
    } else {
        const auto line = [&] { if (!items.empty() && !items.back().separator) { MenuItem separator; separator.separator = true; items.push_back(separator); } };
        // Not for a Steam game: Steam knows what it is.
        if (!steamArtwork(game) && !gameKey(game).empty()) items.push_back({Action::identify, tr(L"Zidentyfikuj grę…"), {}, glyph::search});
        if (!gameFolder(game).empty()) items.push_back({Action::folder, tr(L"Otwórz folder gry"), {}, glyph::folder_open});
        // Skróty: the game started through Launchme from outside it (Pulpit, Menu Start, Steam), each a toggle.
        for (const auto place : {ShortcutPlace::desktop, ShortcutPlace::menu, ShortcutPlace::steam}) {
            if (!shortcutAvailable(game, place)) continue;
            MenuItem item{Action::shortcut_toggle, std::wstring(tr(L"Skrót: ")) + shortcutPlaceName(place), {},
                          place == ShortcutPlace::desktop ? glyph::tv : place == ShortcutPlace::menu ? glyph::library : glyph::game};
            item.value = static_cast<int>(place);
            item.checked = hasShortcut(game, place);
            if (items.empty() || items.back().action != Action::shortcut_toggle) line();
            items.push_back(std::move(item));
        }
        // Zapisy: Ludusavi's backup and restore.
        if (!gameKey(game).empty()) {
            const bool ready = !ludusaviProgram().empty();
            line();
            items.push_back({Action::save_backup, tr(L"Zrób kopię zapisów"), {}, glyph::copy});
            items.push_back({Action::save_restore, tr(L"Przywróć ostatnią kopię…"), {}, glyph::refresh});
            items.push_back({Action::save_ludusavi, ready ? tr(L"Otwórz Ludusavi") : tr(L"Pobierz Ludusavi"), {}, ready ? glyph::open : glyph::download});
        }
        const auto plugins = pluginContext(game);
        if (!plugins.empty()) line();
        for (size_t i = 0; i < plugins.size(); ++i) {
            MenuItem item{Action::plugin, plugins[i].item.title.localized(), {}, glyph::open};
            item.value = static_cast<int>(i);
            items.push_back(std::move(item));
        }
        while (!items.empty() && items.front().separator) items.erase(items.begin());
    }
    return items;
}

void Launcher::desktopOpenSubmenu(int index) {
    if (!desktop_menu_ || index < 0 || index >= static_cast<int>(desktop_menu_->items.size())) return;
    auto& menu = *desktop_menu_;
    menu.sub_parent = index;
    if (menu.items[static_cast<size_t>(index)].action == Action::group) {
        menu.sub.clear();
        const auto games = desktopGamesOf(menu.games);
        if (!games.empty()) menu.sub = desktopGroupItems(games.front(), menu.items[static_cast<size_t>(index)].value);
    } else if (menu.items[static_cast<size_t>(index)].action == Action::shortcuts) {
        menu.sub.clear();
        const auto games = desktopGamesOf(menu.games);
        if (!games.empty())
            for (const auto place : {ShortcutPlace::desktop, ShortcutPlace::menu, ShortcutPlace::steam}) {
                if (!shortcutAvailable(games.front(), place)) continue;
                MenuItem item{Action::shortcut_toggle, shortcutPlaceName(place), {},
                              place == ShortcutPlace::desktop ? glyph::tv : place == ShortcutPlace::menu ? glyph::library : glyph::game};
                item.value = static_cast<int>(place);
                item.checked = hasShortcut(games.front(), place);
                menu.sub.push_back(std::move(item));
            }
    } else if (menu.items[static_cast<size_t>(index)].action == Action::saves) {
        const bool ready = !ludusaviProgram().empty();
        menu.sub = {{Action::save_backup, tr(L"Zrób kopię zapisów"), {}, glyph::copy},
                    {Action::save_restore, tr(L"Przywróć ostatnią kopię…"), {}, glyph::refresh},
                    {Action::save_ludusavi, ready ? tr(L"Otwórz Ludusavi") : tr(L"Pobierz Ludusavi"), {}, ready ? glyph::open : glyph::download}};
    } else menu.sub = desktopCollectionItems(desktopGamesOf(menu.games));
    menu.sub_hover = menu.keyboard ? stepItem(menu.sub, -1, 1) : -1;
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::desktopOpenSectionMenu(int entry_index, float x, float y) {
    if (entry_index < 0 || entry_index >= static_cast<int>(desktop_entries_.size())) return;
    const auto& entry = desktop_entries_[static_cast<size_t>(entry_index)];
    Menu menu;
    menu.kind = Menu::Kind::section;
    menu.x = x; menu.y = y; menu.entry = entry.id;
    if (entry.kind == EntryKind::favorites) {
        MenuItem games{Action::section_games, tr(L"Wybierz gry…"), {}, glyph::check};
        games.key = desktopFavorites().id;
        menu.items.push_back(std::move(games));
    } else if (entry.kind == EntryKind::collection) {
        const auto* collection = desktopCollection(entry.collection);
        if (!collection) return;
        const auto scope = collectionKey(account_, *collection);
        const auto add = [&](Action action, std::wstring label, wchar_t icon, std::wstring shortcut = {}) -> MenuItem& {
            MenuItem item{action, std::move(label), std::move(shortcut), icon};
            item.key = collection->id;
            menu.items.push_back(std::move(item));
            return menu.items.back();
        };
        add(Action::section_rename, tr(L"Zmień nazwę…"), glyph::edit, L"F2");
        add(Action::section_games, tr(L"Wybierz gry…"), glyph::check);
        add(Action::section_tv, tr(L"Na ekranie głównym (TV)"), glyph::tv).checked = !settings_.hidden_collections.contains(scope);
        MenuItem line; line.separator = true; menu.items.push_back(line);
        std::vector<std::string> shown;
        for (const auto& item : desktop_entries_) if (item.kind == EntryKind::collection) shown.push_back(item.collection);
        const auto at = std::find(shown.begin(), shown.end(), collection->id);
        add(Action::section_up, tr(L"Przenieś wyżej"), glyph::chevron_down).enabled = at != shown.begin();
        add(Action::section_down, tr(L"Przenieś niżej"), glyph::chevron_down).enabled = at != shown.end() && at + 1 != shown.end();
        menu.items.push_back(line);
        add(Action::section_delete, localCollection(*collection) ? tr(L"Usuń kolekcję…") : tr(L"Przywróć przypisania Steam…"), glyph::remove).danger = true;
    } else if (entry.kind == EntryKind::studio) {
        // A publisher or developer: its place among the others, or hidden (Ustawienia brings it back).
        std::vector<std::string> shown;
        for (const auto& item : desktop_entries_) if (item.kind == EntryKind::studio && item.group == entry.group) shown.push_back(item.id);
        const auto at = std::find(shown.begin(), shown.end(), entry.id);
        const auto add = [&](Action action, std::wstring label, wchar_t icon) -> MenuItem& {
            MenuItem item{action, std::move(label), {}, icon};
            item.key = entry.id;
            menu.items.push_back(std::move(item));
            return menu.items.back();
        };
        add(Action::section_up, tr(L"Przenieś wyżej"), glyph::chevron_down).enabled = at != shown.begin();
        add(Action::section_down, tr(L"Przenieś niżej"), glyph::chevron_down).enabled = at != shown.end() && at + 1 != shown.end();
        MenuItem line; line.separator = true; menu.items.push_back(line);
        add(Action::section_hide, tr(L"Ukryj"), glyph::hide);
    } else return;
    desktop_menu_ = std::move(menu);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::desktopOpenSortMenu() {
    const auto r = desktopLayout().sortButton();
    Menu menu;
    menu.kind = Menu::Kind::sort;
    menu.x = r.left; menu.y = r.bottom + 6;
    using library_view::Order;
    for (const auto order : {Order::alphabetical, Order::last_played, Order::added, Order::release}) {
        MenuItem item{Action::sort, library_view::label(order), {}, 0};
        item.value = library_view::persistedOrder(order);
        item.checked = settings_.desktop_sort == item.value;
        menu.items.push_back(std::move(item));
    }
    desktop_menu_ = std::move(menu);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

// The title bar's "…": the tools that do not have an icon of their own.
void Launcher::desktopOpenToolsMenu() {
    const auto tools = desktopTools();
    const auto at = std::find(tools.begin(), tools.end(), Tool::more);
    if (at == tools.end()) return;
    const auto r = desktopLayout().tool(static_cast<int>(at - tools.begin()));
    Menu menu;
    menu.kind = Menu::Kind::tools;
    menu.align_right = true;
    menu.x = r.right; menu.y = r.bottom + 6;
    for (const auto tool : desktopMoreTools()) {
        MenuItem item{Action::tool, {}, {}, 0};
        item.value = static_cast<int>(tool);
        switch (tool) {
        case Tool::friends: item.label = tr(L"Widżety"); item.icon = glyph::people; break;
        case Tool::glance: item.label = L"Glance"; item.icon = glyph::info; break;
        case Tool::tarot: item.label = L"Tarot"; item.icon = glyph::star; break;
        default: item.label = tr(L"Sklep"); item.icon = glyph::shop; break;
        }
        menu.items.push_back(std::move(item));
    }
    desktop_menu_ = std::move(menu);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

// The grid's cells: posters (2:3), squares (covers) or landscape (capsules).
void Launcher::desktopOpenViewMenu() {
    const auto r = desktopLayout().viewButton();
    Menu menu;
    menu.kind = Menu::Kind::view;
    menu.x = r.left; menu.y = r.bottom + 6;
    const wchar_t* names[] = {trKey(L"Plakaty"), trKey(L"Kwadraty"), trKey(L"Poziome")};
    for (int i = 0; i < 3; ++i) {
        MenuItem item{Action::view_mode, tr(names[i]), {}, 0};
        item.value = i;
        item.checked = settings_.desktop_view == i;
        menu.items.push_back(std::move(item));
    }
    desktop_menu_ = std::move(menu);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::desktopSetView(int view) {
    view = std::clamp(view, 0, 2);
    if (view == settings_.desktop_view) return;
    settings_.desktop_view = view;
    settings_dirty_ = true; settings_edited_ = std::chrono::steady_clock::now();
    desktop_poster_missing_.clear();
    rebuildCards(true); // The cells take their new art from scratch.
    desktopReflow();
    prefetchPosters();
}

void Launcher::desktopOpenCollectionsMenu(float x, float y) {
    const auto games = desktopTargets();
    if (games.empty()) return;
    Menu menu;
    menu.kind = Menu::Kind::collections;
    menu.x = x; menu.y = y;
    for (const auto& game : games) menu.games.push_back(library_view::selectionKey(game));
    menu.items = desktopCollectionItems(games);
    desktop_menu_ = std::move(menu);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::desktopMenuKey(WPARAM key) {
    if (!desktop_menu_) return;
    auto& menu = *desktop_menu_;
    menu.keyboard = true;
    const bool sub = menu.in_sub && menu.sub_parent >= 0;
    auto& items = sub ? menu.sub : menu.items;
    int& hover = sub ? menu.sub_hover : menu.hover;
    switch (key) {
    case VK_UP: hover = stepItem(items, hover, -1); break;
    case VK_DOWN: hover = stepItem(items, hover, 1); break;
    case VK_RIGHT:
        if (!sub && hover >= 0 && menu.items[static_cast<size_t>(hover)].submenu) { desktopOpenSubmenu(hover); menu.in_sub = true; menu.sub_hover = stepItem(menu.sub, -1, 1); }
        break;
    case VK_LEFT: case VK_BACK:
        if (sub) { menu.in_sub = false; menu.sub_parent = -1; menu.sub.clear(); }
        else if (key == VK_BACK) { desktopCloseMenu(); return; }
        break;
    case VK_ESCAPE:
        if (sub) { menu.in_sub = false; menu.sub_parent = -1; menu.sub.clear(); }
        else { desktopCloseMenu(); return; }
        break;
    case VK_RETURN: case VK_SPACE:
        if (hover >= 0 && hover < static_cast<int>(items.size())) {
            const auto item = items[static_cast<size_t>(hover)];
            if (!item.enabled || item.separator) break;
            if (item.submenu && !sub) { desktopOpenSubmenu(hover); menu.in_sub = true; menu.sub_hover = stepItem(menu.sub, -1, 1); break; }
            desktopMenuPick(item, sub);
            return;
        }
        break;
    default: break;
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::desktopCloseMenu() {
    desktop_menu_.reset();
    desktop_pressed_ = {};
    mouse_transition_until_ = GetTickCount64() + GetDoubleClickTime();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::desktopMenuPick(const desktop_ui::MenuItem& item, bool /*from_sub*/) {
    if (!desktop_menu_) return;
    const auto games = desktopGamesOf(desktop_menu_->games);
    const auto entry = desktop_menu_->entry;
    desktopCloseMenu();
    const Game* game = games.empty() ? nullptr : &games.front();
    switch (item.action) {
    case Action::tool: desktopToolPick(static_cast<Tool>(item.value)); return;
    case Action::start_step: desktopStartPick(item.value); return;
    case Action::start_hide:
        settings_.start_hidden = true;
        settings_dirty_ = true; settings_edited_ = std::chrono::steady_clock::now();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    case Action::view_mode: desktopSetView(item.value); return;
    case Action::companion_change: companionPicker(true); return;
    case Action::companion_mind:
        openCompanion();
        if (companionOpen()) {
            companionMind(true);
            companion_.chat.mind_from_sidebar = true;
        }
        return;
    case Action::companion_settings: openCompanionSettings(); return;
    case Action::play: desktopLaunchSelected(); return;
    case Action::suspend: if (game) freezeGame(gameKey(*game), true); return;
    case Action::close_game: if (game) closeGame(*game); return;
    case Action::favorite:
        if (games.size() == 1) toggleFavorite(*game);
        else if (!games.empty()) {
            desktopSetMember(games, desktopFavorites(), true);
            showNotification(NotificationKind::launch, tr(L"Dodano do ulubionych"), gamesLabel(games.size()));
        }
        return;
    case Action::collection_toggle: {
        const auto* collection = desktopCollection(item.key);
        if (!collection || games.empty()) return;
        const auto c = *collection;
        desktopSetMember(games, c, !item.checked);
        showNotification(NotificationKind::launch, item.checked ? tr(L"Usunięto z kolekcji") : tr(L"Dodano do kolekcji"),
                         c.name + L" · " + (games.size() == 1 ? games.front().name : gamesLabel(games.size())));
        return;
    }
    case Action::collection_new: desktopNewCollection(games); return;
    case Action::collection_manage:
        if (games.size() == 1) openCollectionManager(games.front()); else openCollectionManager();
        return;
    case Action::versions: if (game) openVersions(*game, false); return;
    case Action::details: if (game) openDetails(*game); return;
    case Action::artwork: if (game) openArtworkBrowser(*game); return;
    case Action::retry_art: for (const auto& each : games) retryGameArtwork(each, &each == &games.front()); return;
    case Action::missing_art: openBulkCovers(games); return; // Pobierz brakujące okładki, for these games only.
    case Action::identify: if (game) openIdentifyFor(*game); return;
    case Action::metadata: if (game) desktopMetaOpen(*game); return;
    case Action::aurelia: if (game) openAurelia(game); return;
    case Action::steam_account: if (game) cycleSteamAccount(*game); return;
    case Action::emulator: if (game) cycleGameEmulator(*game); return;
    case Action::launch_options: if (game) openLaunchEditor(*game); return;
    case Action::compat_options: if (game) openGameCompatSettings(*game); return;
    case Action::shortcuts: case Action::saves: case Action::group: return; // Their submenus.
    case Action::save_backup: if (game) gameSaves(*game, false, false); return;
    case Action::save_restore: if (game) askRestoreSaves(*game); return;
    case Action::save_ludusavi: openLudusavi(); return;
    case Action::shortcut_toggle: if (game) setShortcut(*game, static_cast<ShortcutPlace>(item.value), !item.checked); return;
    case Action::folder:
        if (game) if (const auto folder = gameFolder(*game); !folder.empty())
            if (execute_(hwnd_, GameAction{folder.wstring(), {}, {}, ActionKind::open_client}) <= 32)
                showNotification(NotificationKind::error, tr(L"Nie można otworzyć folderu"), folder.wstring());
        return;
    case Action::plugin:
        if (game) {
            const auto actions = pluginContext(*game);
            if (item.value >= 0 && item.value < static_cast<int>(actions.size())) pluginAction(actions[static_cast<size_t>(item.value)], *game);
        }
        return;
    case Action::hide:
        if (games.size() == 1) toggleHidden(*game);
        else for (const auto& each : games) if (!hiddenInLauncher(each)) toggleHidden(each);
        desktop_marked_.clear();
        return;
    case Action::exclude: if (game) askExcludeGame(*game); return;
    case Action::uninstall: if (game) uninstallGame(*game); return;
    case Action::remove_game: if (game) askRemoveFromLibrary(*game); return;
    case Action::sort:
        settings_.desktop_sort = std::clamp(item.value, 1, 4);
        settings_dirty_ = true; settings_edited_ = std::chrono::steady_clock::now();
        desktop_scroll_ = desktop_scroll_target_ = 0;
        rebuildCards(true);
        if (!cards_.empty()) desktopFocus(selected_);
        return;
    case Action::clear_marks: desktop_marked_.clear(); InvalidateRect(hwnd_, nullptr, FALSE); return;
    case Action::downloads: openDownloads(); return;
    case Action::section_rename: desktopCollectionDialog(item.key, Page::name); return;
    case Action::section_games: desktopCollectionDialog(item.key, Page::games); return;
    case Action::section_options: desktopCollectionDialog(item.key, Page::collection); return;
    case Action::section_delete: desktopCollectionDialog(item.key, Page::collection, true); return;
    case Action::section_up: if (studios::parse(item.key)) moveStudio(item.key, -1); else desktopMoveCollection(item.key, -1); return;
    case Action::section_down: if (studios::parse(item.key)) moveStudio(item.key, 1); else desktopMoveCollection(item.key, 1); return;
    case Action::section_hide: hideStudio(item.key); return;
    case Action::section_tv: {
        const auto* collection = desktopCollection(item.key);
        if (!collection) return;
        const auto scope = collectionKey(account_, *collection);
        if (!settings_.hidden_collections.erase(scope)) settings_.hidden_collections.insert(scope);
        settingsChanged(true);
        return;
    }
    default: (void)entry; return;
    }
}

// ---- Collections -----------------------------------------------------------------------------

void Launcher::desktopSetMember(const std::vector<Game>& games, const SteamCollection& collection, bool member) {
    if (!library_state_readable_) { showNotification(NotificationKind::error, tr(L"Nie można odczytać historii biblioteki"), L""); return; }
    for (const auto& game : games) {
        if (!game.version_group.empty()) if (const auto* copies = versions_.find(game)) {
            for (const auto& copy : *copies) setCollectionMember(library_state_.collections, account_, collection, copy, member);
            continue;
        }
        setCollectionMember(library_state_.collections, account_, collection, game, member);
    }
    collectionChanged();
}

void Launcher::desktopDrop(const desktop_ui::Entry& entry, const std::vector<std::string>& keys) {
    const auto games = desktopGamesOf(keys);
    if (games.empty() || !entry.droppable) return;
    const std::string target_id = entry.id;
    const auto subtitle = games.size() == 1 ? games.front().name : gamesLabel(games.size());
    if (entry.kind == EntryKind::favorites) {
        desktopSetMember(games, desktopFavorites(), true);
        desktop_drop_flash_id_ = target_id;
        desktop_drop_flash_ = 1.0f;
        showNotification(NotificationKind::launch, tr(L"Dodano do ulubionych"), subtitle);
    } else if (entry.kind == EntryKind::collection) {
        const auto* collection = desktopCollection(entry.collection);
        if (!collection) return;
        const auto c = *collection;
        desktopSetMember(games, c, true);
        desktop_drop_flash_id_ = target_id;
        desktop_drop_flash_ = 1.0f;
        showNotification(NotificationKind::launch, std::wstring(tr(L"Dodano do kolekcji „")) + c.name + L"”", subtitle);
    }
}

// The collection order of the sidebar (and the TV home): `id` moves before the `before`-th shown
// collection (-1: after the last).
void Launcher::desktopReorder(const std::string& id, int before) {
    std::vector<std::string> shown;
    for (const auto& entry : desktop_entries_) if (entry.kind == EntryKind::collection) shown.push_back(entry.collection);
    const auto from = std::find(shown.begin(), shown.end(), id);
    if (from == shown.end()) return;
    const std::string before_id = before >= 0 && before < static_cast<int>(shown.size()) ? shown[static_cast<size_t>(before)] : std::string{};
    if (before_id == id) return;
    if (before_id.empty() && shown.back() == id) return;
    if (!before_id.empty() && from + 1 != shown.end() && *(from + 1) == before_id) return; // Already there.
    auto order = settings_collections_;
    const auto moved = std::find_if(order.begin(), order.end(), [&](const SteamCollection& c) { return c.id == id; });
    if (moved == order.end()) return;
    const auto item = *moved;
    order.erase(moved);
    auto at = order.end();
    if (!before_id.empty()) at = std::find_if(order.begin(), order.end(), [&](const SteamCollection& c) { return c.id == before_id; });
    else if (const auto last = std::find_if(order.begin(), order.end(), [&](const SteamCollection& c) { return c.id == shown.back(); }); last != order.end()) at = last + 1;
    order.insert(at, item);
    for (const auto& c : order) std::erase(settings_.collection_order, collectionKey(account_, c));
    for (const auto& c : order) settings_.collection_order.push_back(collectionKey(account_, c));
    settingsChanged(true);
}

void Launcher::desktopMoveCollection(const std::string& id, int amount) {
    std::vector<std::string> shown;
    for (const auto& entry : desktop_entries_) if (entry.kind == EntryKind::collection) shown.push_back(entry.collection);
    const auto at = std::find(shown.begin(), shown.end(), id);
    if (at == shown.end()) return;
    const int index = static_cast<int>(at - shown.begin());
    const int before = amount < 0 ? index - 1 : index + 2;
    if (before < 0) return;
    desktopReorder(id, before >= static_cast<int>(shown.size()) ? -1 : before);
}

void Launcher::desktopNewCollection(std::vector<Game> games) {
    if (!library_state_readable_) { showNotification(NotificationKind::error, tr(L"Nie można odczytać historii biblioteki"), L""); return; }
    auto& ui = library_dialog_;
    ui = {};
    ui.open = true; ui.account = account_;
    ui.page = Page::name;
    ui.name_return = games.empty() ? Page::collections : Page::membership;
    if (!games.empty()) {
        ui.game = games.front();
        ui.more_games.assign(games.begin() + 1, games.end());
    }
    ui.steam_collection = account_ != 0 && (games.empty() || std::all_of(games.begin(), games.end(), [](const Game& g) { return g.source == GameSource::steam; }));
    ui.focus = -2;
    ui.close_after_save = true;
    closeFriends();
    rebuildCollectionDialog();
    audio_.drawer();
}

void Launcher::desktopCollectionDialog(const std::string& id, library_manager_ui::Page page, bool confirm) {
    if (!library_state_readable_) { showNotification(NotificationKind::error, tr(L"Nie można odczytać historii biblioteki"), L""); return; }
    const auto* collection = desktopCollection(id);
    if (!collection) return;
    auto& ui = library_dialog_;
    ui = {};
    ui.open = true; ui.account = account_;
    ui.collection = id;
    ui.page = page;
    if (page == Page::name) {
        if (favoriteCollection(*collection)) return; // Ulubione keeps its name.
        ui.input = collection->name; ui.selected = true; ui.focus = -2;
        ui.name_return = Page::collection; ui.close_after_save = true;
    } else if (page == Page::games) { ui.focus = -2; }
    closeFriends();
    rebuildCollectionDialog();
    if (confirm) {
        for (size_t i = 0; i < ui.rows.size(); ++i)
            if (ui.rows[i].key == "@delete" || ui.rows[i].key == "@reset") { ui.focus = static_cast<int>(i); ui.confirm = true; }
        rebuildCollectionDialog();
    }
    audio_.drawer();
}
