#include "launcher.hpp"
#include "../platform/unix_time.hpp"
#include "../platform/text_encoding.hpp"
#include "../ui/quick_menu.hpp"
#include "../library/steam_stats.hpp"
#include <algorithm>
#include <shlobj.h>
#include <shellapi.h>
#ifndef _WIN32
#include <gio/gio.h>
#endif

using namespace plugins::wire;
namespace {
uint64_t pluginTime() { return unix_time::now(); }
#ifndef _WIN32
// The file manager shows the file selected (org.freedesktop.FileManager1.ShowItems, as Explorer's
// SHOpenFolderAndSelectItems); without one, the desktop opens its folder.
bool revealDownload(HWND window, const std::filesystem::path& path) {
    std::error_code error;
    if (!std::filesystem::exists(path, error)) return false;
    const bool folder = std::filesystem::is_directory(path, error);
    if (!folder)
        if (GDBusConnection* bus = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, nullptr)) {
            gchar* uri = g_filename_to_uri(path.c_str(), nullptr, nullptr);
            const gchar* uris[] = {uri, nullptr};
            GVariant* reply = uri ? g_dbus_connection_call_sync(bus, "org.freedesktop.FileManager1", "/org/freedesktop/FileManager1",
                                                                "org.freedesktop.FileManager1", "ShowItems", g_variant_new("(^ass)", uris, ""),
                                                                nullptr, G_DBUS_CALL_FLAGS_NONE, 3000, nullptr, nullptr) : nullptr;
            g_free(uri);
            g_object_unref(bus);
            if (reply) { g_variant_unref(reply); return true; }
        }
    const auto shown = folder ? path : path.parent_path();
    return reinterpret_cast<INT_PTR>(ShellExecuteW(window, L"open", shown.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL)) > 32;
}
#else
bool revealDownload(HWND window, const std::filesystem::path& path) {
    const auto attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) return false;
    if (attributes & FILE_ATTRIBUTE_DIRECTORY) return reinterpret_cast<INT_PTR>(ShellExecuteW(window, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) > 32;
    PIDLIST_ABSOLUTE item = nullptr;
    if (FAILED(SHParseDisplayName(path.c_str(), nullptr, &item, 0, nullptr))) return false;
    const auto result = SHOpenFolderAndSelectItems(item, 0, nullptr, 0);
    CoTaskMemFree(item); return SUCCEEDED(result);
}
#endif
std::filesystem::path bundledPlugins() {
    wchar_t file[32768]; const auto size = GetModuleFileNameW(nullptr, file, static_cast<DWORD>(std::size(file)));
    return std::filesystem::path(std::wstring(file, size)).parent_path() / L"plugin-packages";
}
}
// plugin_achievements.cpp: the achievements method with its store-request rounds.
json::Value pluginAchievementRounds(plugins::Manager& manager, const plugins::Job& job, launcher_queues::StoreGateway& gateway);
void Launcher::initializePlugins() {
    plugins_busy_ = true;
    const auto bundle = bundledPlugins();
    work_.start([queue = plugin_jobs_, alive = alive_, hwnd = hwnd_, bundle, crashed = crashed_plugin_, gateway = store_gateway_] {
        plugins::Manager manager(userConfigDirectory() / L"plugins");
        auto initial = std::make_unique<PluginsMessage>();
        try {
            // A plugin whose code crashed the previous run (diagnostics::takeUnseenCrash) stays off
            // until the user switches it on again: one broken plugin cannot crash every start.
            manager.load(crashed);
            // Every package shipped next to the EXE (plugin-packages/<id>.zip), in a stable order.
            std::vector<std::filesystem::path> packages;
            std::error_code missing;
            for (const auto& entry : std::filesystem::directory_iterator(bundle, missing))
                if (entry.is_regular_file() && _wcsicmp(entry.path().extension().wstring().c_str(), L".zip") == 0) packages.push_back(entry.path());
            std::sort(packages.begin(), packages.end());
            for (const auto& package : packages) manager.ensureBundled(text_encoding::utf8(package.stem().wstring()), package);
        } catch (const std::exception& error) { initial->error = error.what(); }
        initial->snapshot = manager.snapshot();
        if (alive->load() && PostMessageW(hwnd, plugins_message, 0, reinterpret_cast<LPARAM>(initial.get()))) initial.release();
        while (alive->load()) {
            plugins::Job job;
            {
                std::unique_lock lock(queue->mutex);
                queue->wake.wait(lock, [&] { return !alive->load() || !queue->jobs.empty(); });
                if (!alive->load()) return;
                job = std::move(queue->jobs.front()); queue->jobs.pop_front();
            }
            auto result = std::make_unique<PluginsMessage>(); result->job = job;
            try {
                using Kind = plugins::Job::Kind;
                switch (job.kind) {
                case Kind::install: result->job.plugin = manager.install(job.file); break;
                case Kind::enable: manager.enable(job.plugin, job.value.flag); break;
                case Kind::setting: manager.setting(job.plugin, job.key, job.value); break;
                case Kind::remove: manager.remove(job.plugin); break;
                case Kind::playtime: {
                    const auto stats = readSteamStats(findSteamRoot(), job.account, job.game.app_id);
                    if (stats.playtime_minutes) result->job.playtime_seconds = static_cast<uint64_t>(*stats.playtime_minutes) * 60;
                    break;
                }
                case Kind::search: case Kind::command: case Kind::widget: case Kind::details:
                    result->response = manager.invoke(job.plugin, job.kind == Kind::search ? "search" : job.kind == Kind::widget ? "widget" : job.kind == Kind::details ? "details" : "command", job.value, job.locale); break;
                case Kind::library:
                    result->response = manager.invoke(job.plugin, "library", job.value, job.locale); break;
                case Kind::downloads:
                    result->response = manager.invoke(job.plugin, "downloads", job.value, job.locale); break;
                case Kind::achievements:
                    result->response = pluginAchievementRounds(manager, job, *gateway); break;
                default: break;
                }
            } catch (const std::exception& error) { result->error = error.what(); }
            // Do not send edited secrets back with the completion payload.
            result->job.value = {};
            result->snapshot = manager.snapshot();
            if (alive->load() && PostMessageW(hwnd, plugins_message, 0, reinterpret_cast<LPARAM>(result.get()))) result.release();
        }
    });
}
void Launcher::submitPlugin(plugins::Job job) {
    job.locale = i18n::language() == i18n::Language::pl ? "pl" : "en";
    if (const auto* p = plugins_.find(job.plugin); p && p->manifest.library &&
        (job.kind == plugins::Job::Kind::command || job.kind == plugins::Job::Kind::widget))
        job.value.properties["library"] = pluginLibrary();
    if (job.management()) {
        if (plugins_busy_) return;
        plugins_busy_ = true; ++plugin_epoch_;
        closeQuick(); dismissMenu();
        // Invalidate pending catalog results/effects immediately, before the worker finishes.
        store_jobs_->generation.fetch_add(1); store_loading_ = false;
    }
    job.epoch = plugin_epoch_;
    {
        std::lock_guard lock(plugin_jobs_->mutex);
        if (job.kind == plugins::Job::Kind::search)
            std::erase_if(plugin_jobs_->jobs, [](const auto& j) { return j.kind == plugins::Job::Kind::search; });
        if (job.kind == plugins::Job::Kind::widget)
            std::erase_if(plugin_jobs_->jobs, [&](const auto& j) { return j.kind == job.kind && j.plugin == job.plugin && j.key == job.key; });
        if (job.kind == plugins::Job::Kind::details || job.kind == plugins::Job::Kind::achievements)
            std::erase_if(plugin_jobs_->jobs, [&](const auto& j) { return j.kind == job.kind && j.plugin == job.plugin && gameKey(j.game) == gameKey(job.game); });
        if (job.kind == plugins::Job::Kind::library)
            std::erase_if(plugin_jobs_->jobs, [&](const auto& j) { return j.kind == job.kind && j.plugin == job.plugin; });
        plugin_jobs_->jobs.push_back(std::move(job));
    }
    plugin_jobs_->wake.notify_one();
}
bool Launcher::storeEnabled() const {
    const auto* p = plugins_.find(catalog_plugin_);
    return p && p->enabled && p->error.empty() && !plugins_busy_;
}
std::vector<plugins::Action> Launcher::pluginContext(const Game& game) const {
    if (plugins_busy_) return {};
    auto result = plugins_.contributions("context", &game);
    if (result.size() > 3) result.resize(3);
    return result;
}
void Launcher::refreshPluginContributions() {
    const auto downloads = plugins_.contributions("downloads");
    std::erase_if(plugin_downloads_, [&](const auto& entry) {
        return std::none_of(downloads.begin(), downloads.end(), [&](const auto& p) { return p.plugin == entry.first; });
    });
    plugin_download_poll_at_ = 0;
    refreshDownloads();
    const auto providers = plugins_.contributions("library");
    const auto removed = std::erase_if(plugin_library_ids_, [&](const auto& entry) {
        return std::none_of(providers.begin(), providers.end(), [&](const auto& p) { return p.plugin == entry.first; });
    });
    if (removed) { rebuildUninstalledSteamGames(); refreshRecentGames(); }
    refreshPluginLibraries();
    plugin_comparisons_.clear(); plugin_achievements_.clear(); ++plugin_widget_generation_;
    for (auto& [key, sample] : plugin_playtimes_) sample.pending = false; // Stats belong to the session, not an enabled plugin.
    plugin_widget_scope_.clear(); next_plugin_widget_refresh_ = 0;
    plugin_menu_ = plugins_.contributions("menu");
    plugin_widgets_ = plugins_.contributions("widget");
    // Layout budgets; the settings page also exposes every menu command.
    if (plugin_menu_.size() > 4) plugin_menu_.resize(4);
    if (plugin_widgets_.size() > 64) plugin_widgets_.resize(64);
    dashboardEdit(false); // A changing contribution set cancels an in-flight drag.
    plugin_widget_text_.clear();
    for (const auto& w : plugin_widgets_) plugin_widget_text_.push_back(w.item.text.localized());
    quick_selected_ = std::clamp(quick_selected_, 0, quick_menu::count(quick_menu::store_slots) - 1);
    overlay_selected_ = std::clamp(overlay_selected_, 0, quick_menu::count(quick_menu::store_slots, true) - 1);
    dashboard_tile_ = std::min(dashboard_tile_, dashboardTileCount() - 1);
    if (screen_ == ScreenMode::details)
        detail_action_ = std::clamp(detail_action_, -1, std::max({lastDetailAction(), achievementsDetailAction(), ratingDetailAction(), pluginDetailAction()}));
    dashboardEnsureVisible();
}
void Launcher::refreshPluginLibraries() {
    if (plugins_busy_ || scanning_ || !account_) return;
    ++plugin_library_generation_;
    for (const auto& provider : plugins_.contributions("library")) {
        plugins::Job job; job.kind = plugins::Job::Kind::library; job.plugin = provider.plugin;
        job.account = account_; job.generation = plugin_library_generation_;
        job.value = object(); job.value.properties["command"] = string(provider.item.command);
        submitPlugin(std::move(job));
    }
}
void Launcher::refreshPluginWidgets(bool active_only) {
    if (plugins_busy_) return;
    const auto game = pluginActiveGame(); const auto scope = pluginWidgetScope();
    if (scope != plugin_widget_scope_) { plugin_widget_scope_ = scope; ++plugin_widget_generation_; }
    next_plugin_widget_refresh_ = GetTickCount64() + 5000;
    if (game && std::any_of(plugin_widgets_.begin(), plugin_widgets_.end(), [](const auto& w) { return w.item.active_game; })) requestGameComparisons(*game);
    for (const auto& w : plugin_widgets_) {
        if (active_only && !w.item.active_game) continue;
        plugins::Job job; job.kind = plugins::Job::Kind::widget; job.plugin = w.plugin; job.key = w.item.id;
        job.active_game = w.item.active_game; job.generation = plugin_widget_generation_;
        if (w.item.active_game && game) job.game = *game;
        job.value = w.item.active_game ? pluginGameParams(job.game) : object();
        job.value.properties["command"] = string(w.item.command); submitPlugin(std::move(job));
    }
}
void Launcher::pluginAction(const plugins::Action& action, const Game& game, bool refresh, const std::wstring* query, bool background) {
    if (plugins_busy_ || plugin_command_pending_) return;
    const auto* plugin = plugins_.find(action.plugin);
    if (!plugin || !plugin->enabled) return;
    plugins::Job job; job.kind = plugins::Job::Kind::command; job.plugin = action.plugin; job.key = action.item.id; job.game = game;
    if (action.item.active_game) {
        const auto active = pluginActiveGame();
        if (!active) return;
        job.game = *active;
    }
    job.value = pluginGameParams(job.game); job.value.properties["command"] = string(action.item.command);
    job.value.properties["refresh"] = plugins::wire::boolean(refresh);
    if (query) job.value.properties["query"] = string(text_encoding::utf8(*query));
    job.refresh = refresh; job.background = background;
    job.generation = ++plugin_command_generation_;
    if (background) { plugin_refresh_pending_ = true; plugin_refresh_generation_ = job.generation; submitPlugin(std::move(job)); return; }
    plugin_command_pending_ = true; submitPlugin(std::move(job)); dismissMenu();
}
void Launcher::onPlugins(std::unique_ptr<PluginsMessage> message) {
    const auto& job = message->job;
    if (job.kind == plugins::Job::Kind::downloads) {
        const auto pending = plugin_download_pending_.find(job.plugin);
        if (pending != plugin_download_pending_.end() && pending->second == job.epoch) plugin_download_pending_.erase(pending);
    }
    if (job.kind == plugins::Job::Kind::command && job.epoch == plugin_epoch_ && message->error.empty() && !plugins_busy_ && message->response.get("downloads")) {
        const auto providers = plugins_.contributions("downloads");
        if (std::any_of(providers.begin(), providers.end(), [&](const auto& p) { return p.plugin == job.plugin; }))
            receivePluginDownloads(job.plugin, message->response);
    }
    // A queue acknowledgement describes a completed mutation, even if its page was left.
    if (job.kind == plugins::Job::Kind::command && !job.background && !job.refresh && job.epoch == plugin_epoch_ && message->error.empty()) {
        const auto& queued = field(message->response, "queued");
        const auto* owner = plugins_.find(job.plugin);
        if (owner && owner->enabled && field(queued, "count").numeric > 0) {
            downloadQueued(text_encoding::wideLenient(text(queued, "title")));
            plugin_download_poll_at_ = 0;
        }
    }
    if (job.kind == plugins::Job::Kind::command && job.background && job.generation == plugin_refresh_generation_) plugin_refresh_pending_ = false;
    if (job.kind == plugins::Job::Kind::command && job.generation != plugin_command_generation_) {
        if (job.game.kind == GameKind::catalog) add_requests_.clear(job.game.app_id);
        return;
    }
    if (job.kind == plugins::Job::Kind::command) plugin_command_pending_ = false;
    if (job.epoch != plugin_epoch_) {
        if (job.game.kind == GameKind::catalog) add_requests_.clear(job.game.app_id);
        return;
    }
    if (job.kind == plugins::Job::Kind::playtime) {
        receivePluginGameView(*message); invalidateGameOverlay(); InvalidateRect(hwnd_, nullptr, FALSE); return;
    }
    if (job.management() || !message->error.empty()) {
        plugins_ = std::move(message->snapshot);
        if (job.management()) plugins_busy_ = false;
        refreshPluginContributions();
        if (plugin_drawer_active_) {
            const auto* owner = plugins_.find(plugin_page_.plugin);
            if (!owner || !owner->enabled) { plugin_drawer_active_ = false; renderer_.resetPluginThumbnails(0); }
        }
        if (screen_ == ScreenMode::plugin_page && !storeHubPage()) {
            const auto* owner = plugins_.find(plugin_page_.plugin);
            if (!owner || !owner->enabled) showCategory(category_);
        }
        if (!storeEnabled()) {
            store_results_.clear();
            if (screen_ == ScreenMode::store || (screen_ == ScreenMode::details && detail_catalog_)) showCategory(category_);
            if (detail_return_ == ScreenMode::store) detail_return_ = ScreenMode::shelves;
        }
        if (settingsOpen()) buildSettingsRows();
    }
    // Achievements answer quietly: a failure shows on their screen, not as a notification.
    if (job.kind == plugins::Job::Kind::achievements) { if (receivePluginAchievements(*message)) InvalidateRect(hwnd_, nullptr, FALSE); return; }
    if (message->error.empty() && receivePluginGameView(*message)) { invalidateGameOverlay(); InvalidateRect(hwnd_, nullptr, FALSE); return; }
    if (!message->error.empty()) {
        if (job.game.kind == GameKind::catalog) add_requests_.clear(job.game.app_id);
        if (job.kind == plugins::Job::Kind::details && job.background) return; // A quiet lookup for the game index.
        showNotification(NotificationKind::error, tr(L"Błąd pluginu"), text_encoding::wideLenient(message->error));
        if (job.kind == plugins::Job::Kind::search) { store_loading_ = false; store_status_ = text_encoding::wideLenient(message->error); }
    } else if (job.kind == plugins::Job::Kind::downloads) {
        const auto* owner = plugins_.find(job.plugin);
        if (!owner || !owner->enabled || plugins_busy_) return;
        receivePluginDownloads(job.plugin, message->response);
    } else if (job.kind == plugins::Job::Kind::library) {
        if (job.account != account_ || job.generation != plugin_library_generation_ || plugins_busy_) return;
        const auto* owner = plugins_.find(job.plugin);
        if (!owner || !owner->enabled || !owner->error.empty()) return;
        const auto ids = plugins::parseLibrarySupplement(message->response);
        if (ids && plugin_library_ids_[job.plugin] != *ids) {
            plugin_library_ids_[job.plugin] = *ids;
            rebuildUninstalledSteamGames();
            refreshRecentGames();
        }
    } else if (job.kind == plugins::Job::Kind::search) {
        if (job.generation != store_jobs_->generation.load() || screen_ != ScreenMode::store || job.plugin != catalog_plugin_ || !storeEnabled()) return;
        auto result = std::make_unique<StoreMessage>(); result->generation = job.generation; result->account = account_;
        try {
            result->search = plugins::parseCatalog(message->response, job.plugin);
            result->search.error = text_encoding::wideLenient(text(message->response, "message"));
        } catch (const std::exception& error) { result->search.error = text_encoding::wideLenient(error.what()); }
        onStore(std::move(result));
    } else if (job.kind == plugins::Job::Kind::widget) {
        for (size_t i = 0; i < plugin_widgets_.size(); ++i)
            if (plugin_widgets_[i].plugin == job.plugin && plugin_widgets_[i].item.id == job.key)
                plugin_widget_text_[i] = text_encoding::wideLenient(text(message->response, "text")).substr(0, 256);
    } else if (job.kind == plugins::Job::Kind::command) {
        const auto* plugin = plugins_.find(job.plugin);
        if (!plugin || !plugin->enabled || plugins_busy_) { if (job.game.kind == GameKind::catalog) add_requests_.clear(job.game.app_id); return; }
        if (message->response.get("text")) {
            for (size_t i = 0; i < plugin_widgets_.size(); ++i)
                if (plugin_widgets_[i].plugin == job.plugin && plugin_widgets_[i].item.id == job.key)
                    plugin_widget_text_[i] = text_encoding::wideLenient(text(message->response, "text")).substr(0, 256);
        }
        if (field(message->response, "openCatalog").flag) {
            const auto catalogs = plugins_.contributions("catalog");
            if (std::any_of(catalogs.begin(), catalogs.end(), [&](const auto& c) { return c.plugin == job.plugin; })) {
                catalog_plugin_ = job.plugin; openStore();
            }
        }
        // A periodic refresh only updates the page still shown; it never opens one again.
        const bool page_shown = plugin_drawer_active_ ? friends_open_ : screen_ == ScreenMode::plugin_page;
        if (job.background && (!page_shown || plugin_page_.plugin != job.plugin)) return;
        if (const auto* page = message->response.get("page")) {
            try { openPluginPage(job.plugin, *page, false, job.refresh || (plugin_page_.dialog && field(*page, "dialog").flag)); plugin_page_action_ = job.key; plugin_page_game_ = job.game; }
            catch (const std::exception& error) { showNotification(NotificationKind::error, tr(L"Błąd pluginu"), text_encoding::wideLenient(error.what())); }
        }
        if (const auto* drawer = message->response.get("drawer")) {
            try { openPluginPage(job.plugin, *drawer, true, job.refresh); plugin_page_action_ = job.key; plugin_page_game_ = job.game; }
            catch (const std::exception& error) { showNotification(NotificationKind::error, tr(L"Błąd pluginu"), text_encoding::wideLenient(error.what())); }
        }
        const auto uri = text_encoding::wide(text(message->response, "uri"));
        if (!job.background && !job.refresh) if (const auto* reveal = message->response.get("revealFile")) {
            const auto path = std::filesystem::path(text_encoding::wide(reveal->stringOr()));
            if (!path.empty() && path.is_absolute()) {
                const bool opened = reveal_download_ ? reveal_download_(path) : revealDownload(hwnd_, path);
                if (!opened) showNotification(NotificationKind::error, tr(L"Nie można otworzyć folderu"), path.wstring());
            }
        }
        const auto note = text_encoding::wideLenient(text(message->response, "message")).substr(0, 1024);
        bool sent = false;
        if (!uri.empty()) {
            // URI effects only; executable paths/arguments are outside SDK v1.
            const auto colon = uri.find(L':');
            const bool valid = uri.size() <= 2048 && colon > 1 && colon < 32 && uri.find_first_of(std::wstring(L"\0\r\n\" ", 5)) == uri.npos &&
                std::all_of(uri.begin(), uri.begin() + (colon < uri.size() ? colon : 0), [](wchar_t c) { return (c >= L'a' && c <= L'z') || (c >= L'0' && c <= L'9') || c == L'+' || c == L'-' || c == L'.'; }) &&
                !uri.starts_with(L"file:") && !uri.starts_with(L"javascript:") && !uri.starts_with(L"data:");
            sent = valid && execute_(hwnd_, GameAction{uri}) > 32;
            if (!sent) showNotification(NotificationKind::error, tr(L"Nie można wykonać akcji pluginu"), plugin->manifest.name.localized());
        }
        if (job.game.kind == GameKind::catalog) {
            if (sent) { add_requests_.complete(job.game.app_id, pluginTime()); add_requests_.save(userConfigDirectory() / L"store" / L"pending-adds.txt"); }
            else add_requests_.clear(job.game.app_id);
        }
        if (!note.empty() && !message->response.get("queued") && (uri.empty() || sent)) showNotification(NotificationKind::launch, plugin->manifest.name.localized(), note);
    }
    invalidateGameOverlay();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::pluginSettingsRows() {
    using namespace settings_ui;
    if (settings_state_.in(Page::plugin) && !plugins_.find(settings_state_.plugin())) settings_state_.close(Page::plugin);
    if (!settings_state_.in(Page::plugin)) {
        settings_rows_.push_back({Id::plugin_install, Control::action, tr(L"Zainstaluj plugin z ZIP"), {}, plugins_busy_ ? tr(L"Przetwarzanie…") : tr(L"Zainstaluj")});
        for (const auto& p : plugins_.plugins) {
            Row enabled{Id::plugin_enabled, Control::platform, p.manifest.name.localized(), p.error.empty() ?
                p.manifest.description.localized() : text_encoding::wideLenient(p.error), {}, p.enabled};
            enabled.key = p.manifest.id; settings_rows_.push_back(std::move(enabled));
        }
        return;
    }
    const auto& p = *plugins_.find(settings_state_.plugin());
    for (const auto& s : p.manifest.settings) {
        if (s.type == "folder") { // The path beneath its title; the button opens the folder browser.
            const auto path = text_encoding::wide(text(p.settings, s.id));
            Row row{Id::plugin_setting, Control::action, s.title.localized(), path.empty() ? std::wstring(tr(L"Nie wybrano folderu")) : path,
                path.empty() ? tr(L"Wybierz") : tr(L"Zmień")};
            row.key = p.manifest.id; row.plugin_setting = s.id;
            settings_rows_.push_back(std::move(row));
            continue;
        }
        Row row{Id::plugin_setting, s.type == "boolean" ? Control::toggle : s.type == "choice" ? Control::choice : s.type == "secret" ? Control::secret : Control::text,
            s.title.localized(), {}};
        row.key = p.manifest.id; row.plugin_setting = s.id;
        const auto& value = field(p.settings, s.id); row.checked = value.flag;
        row.value = s.type == "secret" ? (value.text.empty() ? tr(L"Wpisz klucz") : tr(L"Klucz zapisany")) : text_encoding::wide(value.text);
        for (const auto& [id, title] : s.choices) { if (id == value.text) row.choice = static_cast<int>(row.choices.size()); row.choices.push_back({title.localized()}); }
        if (!row.choices.empty()) row.value = row.choices[row.choice].label;
        settings_rows_.push_back(std::move(row));
    }
    Row remove{Id::plugin_remove, Control::action, tr(L"Odinstaluj plugin"), text_encoding::wide(p.manifest.version), tr(L"Odinstaluj")};
    remove.key = p.manifest.id; settings_rows_.push_back(std::move(remove));
}
bool Launcher::pluginSettingsActivate(const settings_ui::Row& row) {
    using namespace settings_ui;
    if (row.id != Id::plugin_install && row.id != Id::plugin_enabled && row.id != Id::plugin_setting && row.id != Id::plugin_remove) return false;
    if (plugins_busy_) return true;
    if (row.id == Id::plugin_enabled && settings_state_.editing) {
        openSettingsPage(Page::plugin, row.key);
        return true;
    }
    plugins::Job job; job.plugin = row.key;
    if (row.id == Id::plugin_install) {
        job.kind = plugins::Job::Kind::install;
        if (!pick_plugin_) {
            file_browser::Request request;
            request.title = tr(L"Zainstaluj plugin z ZIP");
            request.extensions = {L".zip"};
            request.native = [this] { return pickPluginPackage(hwnd_); };
            openFileBrowser(std::move(request), [this, job](const std::filesystem::path& picked) mutable {
                if (plugins_busy_) return;
                job.file = picked;
                submitPlugin(std::move(job)); buildSettingsRows();
            });
            return true;
        }
        job.file = pick_plugin_(hwnd_); if (job.file.empty()) return true; // Tests: an injected picker.
    } else if (row.id == Id::plugin_enabled) { job.kind = plugins::Job::Kind::enable; job.value = plugins::wire::boolean(!row.checked); }
    else if (row.id == Id::plugin_remove) job.kind = plugins::Job::Kind::remove;
    else {
        const auto* p = plugins_.find(row.key); if (!p) return true;
        const auto s = std::find_if(p->manifest.settings.begin(), p->manifest.settings.end(), [&](const auto& s) { return s.id == row.plugin_setting; });
        if (s == p->manifest.settings.end()) return true;
        if (s->type == "folder") {
            file_browser::Request request;
            request.mode = file_browser::Mode::folder;
            request.title = s->title.localized();
            request.initial = text_encoding::wide(text(p->settings, s->id));
            openFileBrowser(std::move(request), [this, plugin = row.key, id = s->id](const std::filesystem::path& picked) {
                if (plugins_busy_ || picked.empty() || !picked.is_absolute()) return;
                const auto value = text_encoding::utf8(picked.wstring());
                if (value.size() > 512) { showNotification(NotificationKind::error, tr(L"Ścieżka jest za długa"), picked.wstring()); return; }
                plugins::Job job; job.kind = plugins::Job::Kind::setting; job.plugin = plugin; job.key = id; job.value = string(value);
                submitPlugin(std::move(job)); buildSettingsRows();
            });
            return true;
        }
        if (row.control != Control::toggle && !settings_state_.editing) {
            settings_state_.editing = true; settings_state_.choice = row.choice;
            clearSecretInput(); settings_secret_selected_ = true;
            if (row.control == Control::text) settings_secret_input_ = row.value;
            InvalidateRect(hwnd_, nullptr, FALSE); return true;
        }
        job.kind = plugins::Job::Kind::setting; job.key = s->id;
        job.value = row.control == Control::toggle ? plugins::wire::boolean(!row.checked) : row.control == Control::choice ?
            string(s->choices.at(settings_state_.choice).first) : string(text_encoding::utf8(settings_secret_input_));
        clearSecretInput(); settings_state_.editing = false;
    }
    submitPlugin(std::move(job)); buildSettingsRows(); return true;
}
