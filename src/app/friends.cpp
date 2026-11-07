// Right drawer: a small dashboard (clock, Steam friends / Discord / CPU / GPU tiles) with
// the Steam friends list inside it. Friends, avatars, temperatures and the Discord state
// load off-thread; the tiles repaint only when a value (or the displayed minute) changes.
#include "launcher.hpp"
#include "../artwork/cover_cache.hpp"
#include "../library/visibility.hpp"
#include "../ui/friends_layout.hpp"

#include "../net/http.hpp"

#include <algorithm>
#include <future>

namespace dashboard = friends_layout::dashboard;

void Launcher::onFriends(std::unique_ptr<FriendsMessage> message) {
    // Only for the open drawer's latest request.
    if (!friends_open_ || message->generation != friends_generation_) return;
    friends_ = std::move(message->result);
    friends_loading_ = false;
    // Steam may still be answering (a fresh session): ask again soon, the third time after a reconnect.
    if (friends_.no_presence && friends_retries_ < 3) {
        static constexpr int seconds[] = {6, 20, 20};
        friends_retry_at_ = std::chrono::steady_clock::now() + std::chrono::seconds(seconds[friends_retries_]);
    } else friends_retry_at_ = {};
    friends_scroll_ = 0;
    friends_cursor_ = 0;
    invalidateGameOverlay();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::onAvatar(std::unique_ptr<AvatarMessage> message) {
    if (!friends_open_ || message->generation != friends_generation_) return;
    if (game_overlay_) game_overlay_->renderer().setFriendAvatar(message->id, message->bytes);
    renderer_.setFriendAvatar(message->id, std::move(message->bytes));
    invalidateGameOverlay();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::loadFriends(bool retry) {
    if (!retry) friends_retries_ = 0;
    friends_retry_at_ = {};
    friends_ = {};
    friends_loading_ = true;
    friends_scroll_ = 0;
    friends_cursor_ = 0;
    renderer_.clearFriendAvatars();
    if (game_overlay_) game_overlay_->renderer().clearFriendAvatars();
    const auto generation = ++friends_generation_;
    friends_token_->store(generation);
    const auto alive = alive_;
    const auto token = friends_token_;
    const auto hwnd = hwnd_;
    if (aurelia_active_) {
        aureliaRequest({aurelia::Command::friends}, [generation](Launcher& app, aurelia::Reply reply) {
            auto message = std::make_unique<FriendsMessage>(); message->generation = generation;
            message->result = reply.ok ? aurelia::friends(reply.data) : FriendsResult{{}, reply.error};
            auto people = message->result.friends;
            // Everyone offline with names known: a session that announces no presence of its own gets none from
            // Steam (Aurelia's chat presence "offline"); the public profiles say who is online.
            const bool silent = message->result.error.empty() && std::none_of(people.begin(), people.end(),
                [](const SteamFriend& person) { return person.status > 0 || !person.game.empty(); });
            app.onFriends(std::move(message));
            // Aurelia's roster carries no avatars: the Steam client's cache or the public profiles have them.
            if (generation != app.friends_generation_ || people.empty()) return;
            if (silent) app.loadFriendPresence(std::move(people), generation);
            else app.loadFriendAvatars(std::move(people), generation);
        });
        return;
    }
    work_.start([alive, token, generation, hwnd] {
        const auto root = findSteamRoot();
        const auto steam_id = activeSteamId(root);
        auto result = fetchSteamFriends(steam_id);
        // No account because there is no Steam here at all: say that, not "no active account".
        if (!steam_id && steamExecutable(root).empty()) result.error = tr(L"Steam nie jest zainstalowany");
        if (!alive->load() || token->load() != generation) return;
        auto people = result.friends;
        auto message = std::make_unique<FriendsMessage>();
        message->generation = generation;
        message->result = std::move(result);
        if (!PostMessageW(hwnd, friends_message, 0, reinterpret_cast<LPARAM>(message.get()))) return;
        message.release();
        const auto cache = coverCacheDirectory().parent_path() / L"avatars";
        for (const auto& person : people) {
            if (!alive->load() || token->load() != generation) break;
            auto bytes = fetchFriendAvatar(person, cache, root);
            if (bytes.empty() || !alive->load() || token->load() != generation) continue;
            auto avatar = std::make_unique<AvatarMessage>();
            avatar->id = person.id;
            avatar->generation = generation;
            avatar->bytes = std::move(bytes);
            if (!PostMessageW(hwnd, avatar_message, 0, reinterpret_cast<LPARAM>(avatar.get()))) break;
            avatar.release();
        }
    });
}

void Launcher::loadFriendAvatars(std::vector<SteamFriend> people, uint32_t generation) {
    work_.start([alive = alive_, token = friends_token_, hwnd = hwnd_, generation, people = std::move(people), fetch = fetch_friend_avatar_] {
        const auto cache = coverCacheDirectory().parent_path() / L"avatars";
        const auto root = findSteamRoot();
        for (const auto& person : people) {
            if (!alive->load() || token->load() != generation) break;
            auto bytes = fetch ? fetch(person) : fetchFriendAvatar(person, cache, root);
            if (bytes.empty() || !alive->load() || token->load() != generation) continue;
            auto avatar = std::make_unique<AvatarMessage>();
            avatar->id = person.id; avatar->generation = generation; avatar->bytes = std::move(bytes);
            if (!PostMessageW(hwnd, avatar_message, 0, reinterpret_cast<LPARAM>(avatar.get()))) break;
            avatar.release();
        }
    });
}

void Launcher::loadFriendPresence(std::vector<SteamFriend> people, uint32_t generation) {
    work_.start([alive = alive_, token = friends_token_, hwnd = hwnd_, generation, people = std::move(people), fetch = fetch_friend_profile_]() mutable {
        const auto current = [&] { return alive->load() && token->load() == generation; };
        const auto page = [&fetch](uint64_t id) -> std::string {
            if (fetch) return fetch(id);
            const auto reply = httpGet(L"https://steamcommunity.com/profiles/" + std::to_wstring(id) + L"/?xml=1", L"Accept: text/xml\r\n", 256 * 1024);
            return reply.status == 200 ? std::string(reinterpret_cast<const char*>(reply.body.data()), reply.body.size()) : std::string{};
        };
        // A few profiles at a time: a roster of a hundred in seconds, without a burst of a hundred requests.
        constexpr size_t batch = 6;
        bool found = false;
        for (size_t first = 0; first < people.size() && current(); first += batch) {
            std::vector<std::future<std::string>> pages;
            for (size_t i = first; i < std::min(people.size(), first + batch); ++i) pages.push_back(std::async(std::launch::async, page, people[i].id));
            for (size_t i = 0; i < pages.size(); ++i) {
                const auto presence = profilePresence(pages[i].get());
                auto& person = people[first + i];
                if (presence.known) { person.status = presence.status; person.game = presence.game; found |= presence.status > 0; }
                if (person.avatar_url.empty()) person.avatar_url = presence.avatar_url; // The avatars need no second request.
            }
        }
        if (!current()) return;
        auto message = std::make_unique<RemoteMessage>();
        message->run = [generation, people = std::move(people), found](Launcher& app) mutable {
            if (found) app.friendPresenceFound(generation, people);
            if (generation == app.friends_generation_ && app.friends_open_) app.loadFriendAvatars(std::move(people), generation);
        };
        if (PostMessageW(hwnd, remote_message, 0, reinterpret_cast<LPARAM>(message.get()))) message.release();
    });
}

// The public profiles' presence over the shown roster: the same friend keeps the focus as the order changes.
void Launcher::friendPresenceFound(uint32_t generation, std::vector<SteamFriend> people) {
    if (!friends_open_ || generation != friends_generation_) return;
    const uint64_t focused = friends_cursor_ < friends_.friends.size() ? friends_.friends[friends_cursor_].id : 0;
    for (auto& person : friends_.friends) {
        const auto found = std::find_if(people.begin(), people.end(), [&](const SteamFriend& other) { return other.id == person.id; });
        if (found == people.end()) continue;
        person.status = found->status; person.game = found->game;
        if (person.avatar_url.empty()) person.avatar_url = found->avatar_url;
    }
    sortFriends(friends_.friends);
    for (size_t i = 0; i < friends_.friends.size(); ++i) if (friends_.friends[i].id == focused) friends_cursor_ = i;
    invalidateGameOverlay();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

// Temperatures and the Discord state, every ~2 s while the drawer is open. The worker
// owns the PDH query/gdi32 handles; closing the drawer bumps the generation and wakes it.
void Launcher::startDashboardPoll() {
    refreshPluginWidgets();
    const auto queue = dashboard_jobs_;
    uint32_t generation = 0;
    {
        std::lock_guard lock(queue->mutex);
        generation = queue->generation.fetch_add(1) + 1;
    }
    queue->wake.notify_all();
    const auto alive = alive_;
    const auto hwnd = hwnd_;
    afterburner_cpu_->store(settings_.cpu_temp_source == 0);
    work_.start([queue, generation, alive, hwnd, read = read_sensors_, running = discord_.running, afterburner = afterburner_cpu_] {
        const auto current = [&] { return alive->load() && queue->generation.load() == generation; };
        std::unique_ptr<sensors::Reader> reader;
        while (current()) {
            auto message = std::make_unique<DashboardMessage>();
            message->generation = generation;
            if (read) message->sensors = read();
            else {
                if (!reader) reader = std::make_unique<sensors::Reader>();
                message->sensors = reader->read();
                // Windows has no CPU temperature without a driver; MSI Afterburner, when it runs, has one.
                if (afterburner->load()) {
                    sensors::Reading other;
                    if (sensors::readAfterburner(other) && other.cpu) message->sensors.cpu = other.cpu;
                }
            }
            message->discord_running = running && running();
            if (!current()) return;
            if (PostMessageW(hwnd, dashboard_message, 0, reinterpret_cast<LPARAM>(message.get()))) message.release();
            std::unique_lock lock(queue->mutex);
            queue->wake.wait_for(lock, std::chrono::seconds(2), [&] { return !current(); });
        }
    });
}

void Launcher::stopDashboardPoll() {
    {
        std::lock_guard lock(dashboard_jobs_->mutex);
        dashboard_jobs_->generation.fetch_add(1);
    }
    dashboard_jobs_->wake.notify_all();
}

void Launcher::onDashboard(std::unique_ptr<DashboardMessage> message) {
    if ((!friends_open_ && !hud_polling_) || message->generation != dashboard_jobs_->generation.load()) return;
    if (message->sensors == sensors_ && message->discord_running == discord_running_) return;
    sensors_ = message->sensors;
    discord_running_ = message->discord_running;
    invalidateGameOverlay();
    if (!friends_list_) InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::closeFriends() {
    closeFriendChat();
    dashboardEdit(false);
    if (plugin_command_pending_) { ++plugin_command_generation_; plugin_command_pending_ = false; }
    friends_open_ = false;
    friends_retry_at_ = {};
    friends_token_->store(++friends_generation_);
    if (!hud_polling_) stopDashboardPoll(); // Pinned temperatures keep their reader.
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::friendsBack() {
    if (!friends_open_) return;
    if (dashboardEditing()) {
        if (dashboard_press_source_) dashboardCancelPress();
        else dashboardEdit(false);
        return;
    }
    if (plugin_drawer_active_) {
        if (pluginPageBack()) return; // The page's own level above.
        plugin_drawer_active_ = false; renderer_.resetPluginThumbnails(0);
        drawer_glide_.valid = false; audio_.navigate(); InvalidateRect(hwnd_, nullptr, FALSE); return;
    }
    if (!friends_list_) { closeFriends(); return; }
    if (friend_chat_.open) { closeFriendChat(); audio_.navigate(); return; } // The conversation: the list again.
    friends_list_ = false;
    friends_back_ = false;
    clock_check_at_ = 0; // The clock may have changed while the list covered it.
    audio_.navigate();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::scrollFriends(int amount) {
    if (plugin_drawer_active_) { pluginPageMove(amount); return; }
    if (friendChatOpen()) { friendChatScroll(amount * 70.0f); return; }
    if (!friends_list_) {
        dashboard_scroll_ = std::clamp(dashboard_scroll_ + amount * 70.0f, 0.0f, dashboardLayout().maxScroll(drawerViewport().height));
        drawer_glide_.valid = false;
        invalidateGameOverlay(); InvalidateRect(hwnd_, nullptr, FALSE); return;
    }
    const int total = static_cast<int>(friends_layout::bottom(friends_.friends));
    const int limit = std::max(0, total - static_cast<int>(drawerViewport().height) + 12);
    friends_scroll_ = std::clamp(friends_scroll_ + amount * 70, 0, limit);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::friendsMove(int amount) {
    if (!friends_open_) return;
    if (plugin_drawer_active_) { pluginPageMove(amount); return; }
    if (!friends_list_) { dashboardMove(0, amount); return; }
    if (friend_chat_.open) { friendChatKey(amount < 0 ? VK_UP : VK_DOWN, false); return; }
    // Back (top left) is the last stop above the first friend, also in an empty list.
    if (friends_back_) {
        if (amount < 0 || friends_.friends.empty()) { navigationBoundary(); return; }
        friends_back_ = false;
        audio_.navigate(); InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (amount < 0 && (friends_.friends.empty() || friends_cursor_ == 0)) {
        friends_back_ = true;
        friends_scroll_ = 0;
        audio_.navigate(); InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (friends_.friends.empty()) return;
    const auto before = friends_cursor_;
    friends_cursor_ = static_cast<size_t>(std::clamp(static_cast<int>(friends_cursor_) + amount,
                                    0, static_cast<int>(friends_.friends.size()) - 1));
    if (before == friends_cursor_) { navigationBoundary(); return; }
    const float top = friends_layout::rowTop(friends_.friends, friends_cursor_);
    const float height = drawerViewport().height;
    if (top < 104 + friends_scroll_) friends_scroll_ = std::max(0, static_cast<int>(top - 108));
    if (top + friends_layout::row_height - 5 > height - 12 + friends_scroll_)
        friends_scroll_ = static_cast<int>(top + friends_layout::row_height + 7 - height);
    friends_scroll_ = std::clamp(friends_scroll_, 0,
                        std::max(0, static_cast<int>(friends_layout::bottom(friends_.friends) - height + 12)));
    audio_.navigate();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::friendsAccept() {
    if (!friends_open_) return;
    if (plugin_drawer_active_) { pluginPageAccept(); return; }
    if (friendChatOpen()) {
        // A: Back when it has the focus; on the field the keyboard (Tryb TV), else it sends.
        if (friend_chat_.back_focus) { closeFriendChat(); audio_.navigate(); }
        else if (!desktop_mode_ && pad_input_ && !gameOverlayOpen() && !keyboard_.open) openKeyboard();
        else friendChatSend();
        return;
    }
    if (friends_list_ && (friends_back_ || friends_.friends.empty())) friendsBack(); // Enter/A on Back: the tiles again.
    else if (friends_list_) friendsChat();
    else dashboardActivate(dashboard_tile_);
}

void Launcher::friendsChat() {
    if (!friends_open_ || !friends_list_ || friends_cursor_ >= friends_.friends.size()) return;
    friends_back_ = false; // A click on a row moves the focus there.
    const auto& person = friends_.friends[friends_cursor_];
    const auto id = person.id;
    if (!id) return;
    // With Aurelia the conversation opens here, in the drawer; the Steam client is never started.
    if (aurelia_active_) {
        openFriendChat(id, person.name, person.game.empty() ? std::wstring(person.status ? tr(L"Dostępny") : tr(L"Niedostępny")) : person.game);
        return;
    }
    if (execute_(hwnd_, GameAction{L"steam://friends/message/" + std::to_wstring(id)}) <= 32)
        showNotification(NotificationKind::error, tr(L"Nie można otworzyć czatu"), tr(L"Uruchom klienta Steam"));
}

void Launcher::dashboardMove(int horizontal, int vertical) {
    if (friends_open_ && plugin_drawer_active_) {
        if (vertical) pluginPageMove(vertical);
        else if (horizontal < 0) { plugin_page_.focus = -1; InvalidateRect(hwnd_, nullptr, FALSE); }
        else if (plugin_page_.focus < 0) pluginPageMove(1);
        return;
    }
    if (!friends_open_ || friends_list_) return;
    dashboardPollHold(GetTickCount64());
    const int next = dashboardLayout().move(dashboard_tile_, horizontal, vertical);
    if (next == dashboard_tile_) { navigationBoundary(); return; }
    if (dashboard_press_source_) {
        if (!dashboard_grabbed_ || next < 0) return;
        dashboardReorder(next);
    } else dashboard_tile_ = next;
    dashboardEnsureVisible();
    audio_.navigate();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::dashboardActivate(int tile) {
    if (!friends_open_ || friends_list_ || plugin_drawer_active_ || tile < dashboard::reorganize || tile >= dashboardTileCount()) return;
    dashboard_tile_ = tile;
    if (tile == dashboard::reorganize) { dashboardEdit(!dashboard_editing_); return; }
    if (dashboard_editing_) {
        const auto layout = dashboardLayout();
        auto& saved = settings_.widget_layout;
        const auto& item = *std::find_if(layout.items.begin(), layout.items.end(), [&](const auto& i) { return i.source == tile; });
        const auto found = std::find_if(saved.begin(), saved.end(), [&](const auto& p) { return p.id == item.id; });
        if (found != saved.end()) found->size = widgets::next(found->size);
        settings_dirty_ = true;
        dashboardEnsureVisible(); drawer_glide_.valid = false;
        audio_.navigate(); invalidateGameOverlay(); InvalidateRect(hwnd_, nullptr, FALSE); return;
    }
    if (settings_.game_fps && tile == dashboardTileCount() - 1) { openHudMenu(); return; } // The widget's options: the master switch, each line, the look.
    if (tile >= dashboard::tile_count) { const auto action = plugin_widgets_[tile - dashboard::tile_count]; pluginAction(action); return; }
    switch (static_cast<dashboard::Tile>(tile)) {
    case dashboard::Tile::steam:
        friends_list_ = true;
        friends_back_ = false;
        friends_cursor_ = 0;
        friends_scroll_ = 0;
        audio_.drawer();
        InvalidateRect(hwnd_, nullptr, FALSE);
        break;
    case dashboard::Tile::discord: openDiscord(); break;
    case dashboard::Tile::cpu: openHudMenu(); break;
    case dashboard::Tile::gpu: openHudMenu(); break;
    case dashboard::Tile::profile:
        if (gameOverlayOpen()) break; // Over a game the page would open behind it.
        openProfile(); // Closes the drawer and opens over the screen beneath it.
        break;
    }
}

// Focus a visible Discord window, otherwise start the client (Update.exe or discord://).
// Launch/focus only: no Discord API or RPC.
void Launcher::openDiscord() {
    if (discord_.focus && discord_.focus()) { closeFriends(); return; }
    const auto action = discord_.locate ? discord_.locate() : std::nullopt;
    if (!action) {
        showNotification(NotificationKind::error, tr(L"Nie znaleziono Discorda"), L"");
        return;
    }
    AllowSetForegroundWindow(ASFW_ANY); // Discord's window may take the foreground from us.
    if (execute_(hwnd_, *action) <= 32) {
        showNotification(NotificationKind::error, tr(L"Nie można uruchomić Discorda"), L"");
        return;
    }
    audio_.launch();
    closeFriends();
}

bool Launcher::overFriends(float mouse_x) const {
    const auto view = renderer_.viewport();
    return mouse_x / view.scale >= view.width - friends_layout::drawer_width - 10;
}
