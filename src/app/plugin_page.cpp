#include "launcher.hpp"
#include "../ui/friends_layout.hpp"
#include "../ui/grid_layout.hpp"
#include "../ui/quick_menu.hpp"
#include <algorithm>

json::Value Launcher::pluginLibrary() const {
    json::Value out; out.type = json::Value::Type::array;
    size_t bytes = 0;
    for (const auto& game : public_library_) if (isGame(game) && game.visibility == GameVisibility::public_game) {
        auto encoded = plugins::gameJson(game);
        if ((bytes += encoded.size()) > 1500 * 1024) break; // Leave room for the request envelope.
        if (auto value = json::parse(encoded)) out.elements.push_back(std::move(*value));
    }
    return out;
}
// refreshed: the answer to F5 or the page's own periodic refresh. The same page (owner, place
// and title) then keeps its focus, scroll and the text being typed into its search field.
void Launcher::openPluginPage(const std::string& owner, const json::Value& input, bool drawer, bool refreshed) {
    auto page = plugin_ui::parse(input, owner);
    // A page opened from outside the plugin pages through the Stores entry belongs to the Stores tabs; one reached
    // from inside them (a tab, a row, back) stays theirs.
    if (!drawer) {
        if (screen_ != ScreenMode::plugin_page && screen_ != ScreenMode::store) stores_open_ = stores_opening_;
        stores_opening_ = false;
        if (stores_open_ && !owner.empty() && (store_tab_ < 0 || store_tab_ >= static_cast<int>(plugin_menu_.size()) || plugin_menu_[static_cast<size_t>(store_tab_)].plugin != owner)) {
            const auto found = std::find_if(plugin_menu_.begin(), plugin_menu_.end(), [&](const auto& entry) { return entry.plugin == owner; });
            store_tab_ = found == plugin_menu_.end() ? 0 : static_cast<int>(found - plugin_menu_.begin());
        }
    }
    if (drawer && !page.rows.empty() && page.rows.front().input) {
        // The drawer has no text entry: its pages list results only.
        page.rows.erase(page.rows.begin()); page.query.clear();
        page.focus = -1; page.focus = page.next(1);
    }
    const bool shown = drawer ? friends_open_ && plugin_drawer_active_ : screen_ == ScreenMode::plugin_page;
    const bool keep = refreshed && shown && plugin_page_.plugin == owner && plugin_page_.title == page.title;
    if (keep) {
        const int count = static_cast<int>(page.rows.size());
        int focus = std::min(plugin_page_.focus, count - 1);
        if (focus >= 0 && !page.rows[focus].actionable()) {
            // The row lost its action (a finished download, say): the nearest one above, else below.
            int up = focus; while (up >= 0 && !page.rows[up].actionable()) --up;
            int down = focus; while (down < count && !page.rows[down].actionable()) ++down;
            focus = up >= 0 ? up : down < count ? down : page.next(1);
        }
        page.focus = plugin_page_.focus < 0 ? -1 : focus;
        page.scroll = plugin_page_.scroll;
        if (plugin_page_.typing() && page.typing()) page.query = plugin_page_.query; // Typed since the request left.
    }
    std::vector<Game> games;
    for (auto& row : page.rows) {
        Game game; game.kind = GameKind::back; game.name = row.title;
        if (!row.download_id.empty()) if (const auto tasks = plugin_downloads_.find(owner); tasks != plugin_downloads_.end()) {
            const auto task = std::find_if(tasks->second.begin(), tasks->second.end(), [&](const auto& d) { return d.id == row.download_id; });
            if (task != tasks->second.end()) {
                game.kind = GameKind::store; game.source = GameSource::custom; game.installed = false;
                game.source_id = "plugin-download:" + owner + ':' + task->id; game.name = task->title; game.platform = task->platform;
            }
        }
        if (!row.game_key.empty()) {
            const auto found = std::find_if(public_library_.begin(), public_library_.end(), [&](const auto& g) {
                return g.visibility == GameVisibility::public_game && gameKey(g) == row.game_key;
            });
            if (found != public_library_.end()) game = *found;
            // Unknown/private keys never grant a plugin access to artwork or games.
        }
        games.push_back(std::move(game));
    }
    if (drawer) page.asDrawer();
    else if (!keep) { desktopLeaveLibrary(); rememberPlace(); closeFriends(); }
    if (!keep) { dashboardEdit(false); closeQuick(); dismissMenu(); }
    plugin_page_ = std::move(page); plugin_page_games_ = std::move(games);
    plugin_drawer_active_ = drawer;
    plugin_page_refresh_at_ = plugin_page_.refresh ? GetTickCount64() + plugin_page_.refresh * 1000ull : 0;
    {
        const auto view = drawerViewport();
        const plugin_ui::Layout layout{drawer ? friends_layout::drawer_width : view.width, view.height, drawer, plugin_page_.dialog};
        plugin_page_.scroll = std::clamp(plugin_page_.scroll, 0.0f, layout.maxScroll(plugin_page_));
    }
    if (drawer) {
        friends_open_ = true; friends_list_ = false; if (!keep) drawer_glide_.valid = false;
        if (game_overlay_) game_overlay_->renderer().resetPluginThumbnails(plugin_page_.rows.size());
        renderer_.resetPluginThumbnails(plugin_page_.rows.size()); requestPluginPageArt();
    } else {
        screen_ = ScreenMode::plugin_page; rebuildCards(false);
        selected_ = static_cast<size_t>(std::max(0, plugin_page_.focus));
        requestPluginPageArt();
    }
    if (!keep) menu_progress_ = 0;
    invalidateGameOverlay();
    InvalidateRect(hwnd_, nullptr, FALSE);
}
// The Stores entry of the quick bar and the desktop's toolbar: the first store plugin's page (the last tab
// used), under the tabs of all of them; without a store plugin, a page saying so.
void Launcher::openStores() {
    if (plugins_busy_ || plugin_command_pending_) return;
    if (plugin_menu_.empty()) {
        using namespace plugins::wire;
        auto section = object();
        section.properties["title"] = string(text_encoding::utf8(tr(L"Brak pluginów sklepu")));
        section.properties["empty"] = string(text_encoding::utf8(tr(L"Zainstaluj lub włącz plugin sklepu w Ustawieniach → Pluginy.")));
        section.properties["items"].type = json::Value::Type::array;
        auto page = object();
        page.properties["title"] = string(text_encoding::utf8(tr(L"Sklepy")));
        page.properties["status"] = string("");
        page.properties["sections"].type = json::Value::Type::array;
        page.properties["sections"].elements.push_back(std::move(section));
        stores_opening_ = true;
        openPluginPage({}, page, false, false);
        stores_open_ = true;
        return;
    }
    store_tab_ = std::clamp(store_tab_, 0, static_cast<int>(plugin_menu_.size()) - 1);
    // The screen opens at once, tabs and all, with a placeholder; the plugin's page replaces it when it answers.
    if (screen_ != ScreenMode::plugin_page || !stores_open_) {
        using namespace plugins::wire;
        auto section = object();
        section.properties["title"] = string(text_encoding::utf8(tr(L"Ładowanie…")));
        section.properties["items"].type = json::Value::Type::array;
        auto page = object();
        page.properties["title"] = string(text_encoding::utf8(tr(L"Sklepy")));
        page.properties["status"] = string("");
        page.properties["sections"].type = json::Value::Type::array;
        page.properties["sections"].elements.push_back(std::move(section));
        stores_opening_ = true;
        openPluginPage({}, page, false, false);
        stores_open_ = true;
    }
    pluginAction(plugin_menu_[static_cast<size_t>(store_tab_)]);
}
// On a store plugin's own page or on the catalog screen (a plugin that opens its catalog), as long as the
// Stores entry brought us there.
bool Launcher::storeTabsShown() const {
    if (!stores_open_ || friends_open_ || plugin_menu_.empty()) return false;
    if (screen_ == ScreenMode::store) return true;
    return screen_ == ScreenMode::plugin_page && !plugin_drawer_active_ && !plugin_page_.dialog;
}
int Launcher::storeTabIndex() const { return std::clamp(store_tab_, 0, std::max(0, static_cast<int>(plugin_menu_.size()) - 1)); }
void Launcher::switchStoreTab(int amount) {
    if (!storeTabsShown() || !amount || menu_open_ || pending_launch_) return;
    const int current = storeTabIndex(), next = std::clamp(current + amount, 0, static_cast<int>(plugin_menu_.size()) - 1);
    if (next == current) { navigationBoundary(); return; }
    if (plugins_busy_ || plugin_command_pending_) return;
    closeKeyboard();
    store_tab_ = next;
    audio_.navigate();
    pluginAction(plugin_menu_[static_cast<size_t>(next)]);
    InvalidateRect(hwnd_, nullptr, FALSE);
}
// A neighbouring tab of the strip in the top bar (the library's geometry).
bool Launcher::storeTabClick(float x, float y) {
    if (!storeTabsShown()) return false;
    const float width = drawerViewport().width;
    std::optional<int> offset;
    if (plugin_menu_.size() <= 3) { // A short strip takes only its own room, centred (Renderer::drawCategoryStrip, fit).
        const float count = static_cast<float>(plugin_menu_.size()), slot = library_tabs::home::slot(width);
        if (y >= library_tabs::top + plugin_ui::Layout::tabs_drop && y < library_tabs::top + plugin_ui::Layout::tabs_drop + library_tabs::height) {
            const float index = (x - library_tabs::home::center(width)) / slot + (count - 1) * 0.5f;
            if (index > -0.5f && index < count - 0.5f) offset = static_cast<int>(std::lround(index)) - storeTabIndex();
        }
    } else offset = library_tabs::home::hit(x, y, width, storeTabIndex(), plugin_ui::Layout::tabs_drop);
    if (!offset) return false;
    if (*offset) switchStoreTab(*offset);
    return true;
}
void Launcher::pluginPageMove(int direction) {
    if (!direction) return;
    const int next = plugin_page_.next(direction < 0 ? -1 : 1);
    if (next == plugin_page_.focus) { navigationBoundary(); return; }
    plugin_page_.focus = next;
    if (desktopStorePage()) desktop_sheet_keyboard_ = true; // Its focus ring shows from the first key.
    if (!plugin_drawer_active_) selected_ = static_cast<size_t>(std::max(0, next));
    const auto view = drawerViewport();
    const plugin_ui::Layout layout{plugin_drawer_active_ ? friends_layout::drawer_width : view.width, view.height, plugin_drawer_active_, plugin_page_.dialog};
    if (next >= 0) {
        const auto& row = plugin_page_.rows[next];
        if (row.top < plugin_page_.scroll) plugin_page_.scroll = row.top;
        if (row.top + row.height() > plugin_page_.scroll + layout.bottom() - layout.top)
            plugin_page_.scroll = row.top + row.height() - (layout.bottom() - layout.top);
    }
    plugin_page_.scroll = std::clamp(plugin_page_.scroll, 0.0f, layout.maxScroll(plugin_page_));
    requestPluginPageArt(); audio_.navigate(); InvalidateRect(hwnd_, nullptr, FALSE);
}
// A command of the open page for its owner. The opening contribution stays the page's: F5 and
// the periodic refresh repeat it, never a row's mutation. query: the search field's text.
void Launcher::pluginPageCommand(const std::string& command, const std::wstring* query) {
    const auto* owner = plugins_.find(plugin_page_.plugin);
    if (!owner || !owner->enabled || plugins_busy_ || plugin_command_pending_) return;
    plugins::Action action; action.plugin = plugin_page_.plugin;
    action.item.id = plugin_page_action_; action.item.command = command;
    pluginAction(action, plugin_page_game_, false, query);
}
void Launcher::pluginPageAccept() {
    if (plugin_page_.focus < 0) { if (plugin_drawer_active_) friendsBack(); else back(); return; }
    const auto* owner = plugins_.find(plugin_page_.plugin);
    if (!owner || !owner->enabled || plugins_busy_) return;
    const auto& row = plugin_page_.rows.at(plugin_page_.focus);
    if (!row.actionable() || plugin_command_pending_) return;
    if (row.input) { closeKeyboard(); const auto query = plugin_page_.query; pluginPageCommand(row.command, &query); return; }
    if (!row.command.empty()) { pluginPageCommand(row.command); return; }
    if (!row.uri.empty() && execute_(hwnd_, GameAction{row.uri}) <= 32)
        showNotification(NotificationKind::error, tr(L"Nie można wykonać akcji pluginu"), owner->manifest.name.localized());
}
// Esc/B, Back and Backspace in an empty search field: one level up when the page names a command.
bool Launcher::pluginPageBack() {
    const bool shown = plugin_drawer_active_ ? friends_open_ : screen_ == ScreenMode::plugin_page && !friends_open_;
    if (!shown || plugin_page_.back_command.empty()) return false;
    if (plugin_command_pending_) { ++plugin_command_generation_; plugin_command_pending_ = false; } // Leaving drops its answer.
    closeKeyboard();
    pluginPageCommand(plugin_page_.back_command);
    audio_.navigate();
    return true;
}
void Launcher::pluginPageType(wchar_t character) {
    if (!pluginPageTyping() || character < L' ' || character == 127 || plugin_page_.query.size() >= plugin_ui::query_limit) return;
    plugin_page_.query += character;
    InvalidateRect(hwnd_, nullptr, FALSE);
}
void Launcher::pluginPageErase() {
    if (!pluginPageTyping()) return;
    if (plugin_page_.query.empty()) { back(); return; }
    plugin_page_.query.pop_back();
    if (!plugin_page_.query.empty() && IS_HIGH_SURROGATE(plugin_page_.query.back())) plugin_page_.query.pop_back();
    InvalidateRect(hwnd_, nullptr, FALSE);
}
// The page's own refresh period (Page::refresh): the opening command again, in the background:
// a row picked meanwhile goes first and the stale answer is dropped.
void Launcher::pollPluginPageRefresh() {
    const bool shown = plugin_drawer_active_ ? friends_open_ : screen_ == ScreenMode::plugin_page;
    if (!shown || !plugin_page_.refresh || !plugin_page_refresh_at_ || GetTickCount64() < plugin_page_refresh_at_) return;
    if (plugin_command_pending_ || plugin_refresh_pending_ || plugins_busy_ || keyboard_.open || plugin_page_.typing()) return;
    plugin_page_refresh_at_ = GetTickCount64() + plugin_page_.refresh * 1000ull;
    refreshPluginPage(true);
}
bool Launcher::refreshPluginPage(bool background) {
    if (screen_ != ScreenMode::plugin_page && !(friends_open_ && plugin_drawer_active_)) return false;
    if (const auto* plugin = plugins_.find(plugin_page_.plugin))
        for (const auto& c : plugin->manifest.contributions) if (c.id == plugin_page_action_) {
            auto action = plugins::Action{plugin_page_.plugin, c}; action.item.active_game = false;
            pluginAction(action, plugin_page_game_, true, nullptr, background); break;
        }
    return true;
}
void Launcher::pluginPageClick(float x, float y) {
    const auto view = drawerViewport();
    const plugin_ui::Layout layout{plugin_drawer_active_ ? friends_layout::drawer_width : view.width, view.height, plugin_drawer_active_, plugin_page_.dialog};
    if (layout.back(x, y)) { if (plugin_drawer_active_) friendsBack(); else back(); return; }
    if (storeTabClick(x, y)) return;
    const auto hit = layout.hit(plugin_page_, x, y);
    if (hit >= 0) {
        const bool field = plugin_page_.rows[hit].input;
        plugin_page_.focus = hit;
        if (!plugin_drawer_active_) selected_ = static_cast<size_t>(hit);
        if (!field) pluginPageAccept(); // A click puts the caret in the search field; Enter searches.
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}
void Launcher::requestPluginPageArt() {
    const auto view = drawerViewport();
    const plugin_ui::Layout layout{plugin_drawer_active_ ? friends_layout::drawer_width : view.width, view.height, plugin_drawer_active_, plugin_page_.dialog};
    for (size_t i = 0; i < plugin_page_games_.size() && i < plugin_page_.rows.size(); ++i) {
        const auto& row = plugin_page_.rows[i];
        if ((row.game_key.empty() && row.download_id.empty()) || !isGame(plugin_page_games_[i])) continue;
        const bool visible = layout.rowTop(plugin_page_, i) + row.height() >= layout.top && layout.rowTop(plugin_page_, i) <= layout.bottom();
        if (!row.download_id.empty()) {
            const auto& game = plugin_page_games_[i];
            const auto key = downloadArtTarget(game).key();
            if (visible) requestDownloadArt(game);
            const auto* art = visible ? art_.find(key) : nullptr;
            if (plugin_drawer_active_) {
                renderer_.setPluginThumbnail(i, art ? art->cover : ImageBytes{}, art ? art->focus : CropFocus{});
                if (game_overlay_) game_overlay_->renderer().setPluginThumbnail(i, art ? art->cover : ImageBytes{}, art ? art->focus : CropFocus{});
            } else if (!visible) renderer_.releaseCover(i);
            else if (art && art->cover && i < cards_.size()) { renderer_.setCover(i, art->cover, art->focus); cards_[i].cover_opacity = 1; }
            continue;
        }
        if (plugin_drawer_active_) {
            if (game_overlay_) {
                if (!visible) game_overlay_->renderer().setPluginThumbnail(i, {}, {});
                else if (const auto* art = art_.find(row.game_key); art && art->cover)
                    game_overlay_->renderer().setPluginThumbnail(i, art->cover, art->focus);
            }
            if (!visible) renderer_.setPluginThumbnail(i, {}, {});
            else if (const auto* art = art_.find(row.game_key); art && art->cover) renderer_.setPluginThumbnail(i, art->cover, art->focus);
            else requestArt(plugin_page_games_[i], CoverMessage::Kind::cover);
            continue;
        }
        if (!visible) { renderer_.releaseCover(i); continue; }
        if (renderer_.hasCover(i)) continue;
        if (const auto* art = art_.find(row.game_key); art && art->cover) { renderer_.setCover(i, art->cover, art->focus); cards_[i].cover_opacity = 1; }
        else requestArt(plugin_page_games_[i], CoverMessage::Kind::cover);
    }
}
