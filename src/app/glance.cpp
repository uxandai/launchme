#include "launcher.hpp"
#include "../library/steam.hpp"

#include <climits>

std::vector<glance::OwnedGame> Launcher::glanceGames() const {
    std::vector<glance::OwnedGame> games;
    for (const auto* source : {&installed_library_, &uninstalled_library_}) for (const auto& game : *source) {
        if (game.source != GameSource::steam || game.kind != GameKind::steam || !game.app_id || game.app_id == 480 ||
            game.visibility != GameVisibility::public_game || excluded_ids_.contains(game.app_id) || locallyHidden(game) || steamTool(game)) continue;
        uint64_t played = game.last_played;
        const auto key = recent_games::historyKey(game,account_,{},{});
        if (const auto found = library_state_.last_played.find(key); found != library_state_.last_played.end()) played = std::max(played,found->second);
        games.push_back({game.app_id,placeholderGameName(game.name) ? L"" : game.name,played,game.installed});
    }
    return glance::selectOwned(std::move(games));
}

void Launcher::syncGlanceLibrary() {
    if (!glanceOpen() || glance_.section != static_cast<int>(glance::Section::owned) || glanceGames() == glance_roster_) return;
    glance_.feed = {}; glance_.focus = -1; glance_.scroll = 0; glance_.revealContent();
    renderer_.resetGlanceThumbnails(0);
    requestGlance();
}

bool Launcher::tickGlance(float dt) {
    const float before = glance_shortcut_progress_;
    if (!glanceAvailable(true)) glance_shortcut_progress_ = 0;
    else if (!settings_.screen_transitions) glance_shortcut_progress_ = 1;
    else glance_shortcut_progress_ = std::min(1.0f,glance_shortcut_progress_+dt/0.30f);
    const float hover = glanceAvailable() && glance_hover_ ? 1.0f : 0.0f;
    if (settings_.screen_transitions) glance_hover_motion_.step(hover,dt,spring::glide.omega,spring::glide.zeta);
    else glance_hover_motion_.snap(hover);
    const bool hover_moving = !glance_hover_motion_.settled(hover,0.002f);
    if (!hover_moving) glance_hover_motion_.snap(hover);
    bool moving = hover_moving || glance_shortcut_progress_ != before;
    if (glanceOpen()) {
        const auto v = renderer_.viewport();
        if (glance_.measured_width != v.width) {
            renderer_.measureGlance(glance_);
            glance_.scroll = glance_ui::Layout{v.width,v.height}.reveal(glance_.focus,static_cast<int>(glance_.feed.items.size()),glance_.scroll,&glance_);
            glance_.focus_valid = false;
        }
        moving = glance_.step(dt,v.width,v.height,settings_.screen_transitions) || moving;
    }
    return moving;
}

bool Launcher::glanceAvailable(bool allow_closing) const { return settings_.glance && homeOverlayAvailable(allow_closing); }

void Launcher::openGlance() {
    if (!glanceAvailable()) return;
    desktopLeaveLibrary(); rememberPlace(); cancelEnterHold();
    glance_ = {}; glance_hover_ = false; glance_hover_motion_.snap(0);
    screen_ = ScreenMode::glance;
    renderer_.resetGlanceThumbnails(0);
    requestGlance();
    audio_.quick(); InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::closeGlance() {
    glance_cancel_->store(true); glance_pending_ = glance_pending_refresh_ = false;
    glance_.loading = false;
    renderer_.resetGlanceThumbnails(0);
    showCategory(category_);
}

void Launcher::requestGlance(bool refresh) {
    if (!glanceOpen() || !settings_.glance) return;
    if (refresh && GetTickCount64() - glance_requested_ < 10000) return;
    // Coalesce rapid tab changes into one pending request, keeping at most one worker alive.
    if (glance_busy_) {
        glance_pending_ = true; glance_pending_refresh_ = refresh; glance_cancel_->store(true); glance_.loading = true;
        return;
    }
    glance_requested_ = GetTickCount64();
    glance_pending_ = glance_pending_refresh_ = false;
    glance_busy_ = glance_.loading = true;
    const auto cancel = glance_cancel_ = std::make_shared<std::atomic_bool>(false);
    const auto image_focus = glance_image_focus_ = std::make_shared<std::atomic_int>(std::max(0,glance_.focus));
    const auto generation = ++glance_generation_;
    const int section = glance_.section;
    const bool owned = section == static_cast<int>(glance::Section::owned);
    glance_roster_ = owned ? glanceGames() : std::vector<glance::OwnedGame>{};
    const bool english = i18n::english();
    const auto cache = userConfigDirectory() / L"glance";
    const auto post = [hwnd = hwnd_, alive = alive_, generation, section](std::unique_ptr<GlanceMessage> message) {
        message->generation = generation; message->section = section;
        if (alive->load() && PostMessageW(hwnd, glance_message, 0, reinterpret_cast<LPARAM>(message.get()))) message.release();
    };
    work_.start([post, cancel, image_focus, section, owned, games = glance_roster_, english, cache, refresh, get = glance_get_] {
        HttpCancellation cancellation(*cancel);
        auto result = owned ? glance::loadOwned(games,english,cache,refresh,get) :
                              section==static_cast<int>(glance::Section::upcoming) ? glance::loadUpcoming(english,cache,refresh,get) :
                              glance::load(static_cast<glance::Section>(section), english, cache, refresh, get);
        auto message = std::make_unique<GlanceMessage>(); message->feed = result;
        if (!cancel->load()) post(std::move(message));
        glance::ImageSession images;
        const auto deliverImage = [&](size_t i) {
            auto image = std::make_unique<GlanceMessage>(); image->kind = GlanceMessage::Kind::image; image->index = i;
            image->bytes = glance::thumbnail(result.items[i], cache / ((english ? L"en-" : L"pl-") + std::to_wstring(section) + L"-" + std::to_wstring(i) + L".img"), get,english,&images);
            if (image->bytes.empty()) return false;
            if (!cancel->load()) post(std::move(image));
            return true;
        };
        std::vector<size_t> retry;
        std::vector<bool> attempted(result.items.size(),false);
        for (size_t count=0;count<result.items.size() && !cancel->load();++count) {
            size_t next=result.items.size(); int distance=INT_MAX;
            for (size_t i=0;i<attempted.size();++i) if (!attempted[i]) {
                const int away=std::abs(static_cast<int>(i)-image_focus->load());
                if (away<distance) { distance=away; next=i; }
            }
            if (next==result.items.size()) break;
            attempted[next]=true;
            if (!deliverImage(next)) retry.push_back(next);
        }
        // One bounded retry after all cards got a chance; failures no longer consume an
        // 18-second shared deadline and leave the end of the feed permanently blank.
        std::erase_if(images.metadata,[](const auto& entry) { return entry.second.empty(); });
        for (const auto i : retry) { if (cancel->load()) break; deliverImage(i); }
        auto done = std::make_unique<GlanceMessage>(); done->kind = GlanceMessage::Kind::done; post(std::move(done));
    }, [post](std::exception_ptr) {
        auto done = std::make_unique<GlanceMessage>(); done->kind = GlanceMessage::Kind::done; done->failed = true; post(std::move(done));
    });
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::onGlance(std::unique_ptr<GlanceMessage> message) {
    if (message->generation != glance_generation_) return;
    if (message->kind == GlanceMessage::Kind::done) {
        glance_busy_ = false;
        if (glanceOpen() && glance_pending_) { requestGlance(glance_pending_refresh_); return; }
        if (!glance_cancel_->load() && message->failed) { glance_.loading = false; glance_.feed.unavailable = true; }
    } else if (!settings_.glance || glance_cancel_->load() || message->section != glance_.section) return;
    else if (message->kind == GlanceMessage::Kind::feed) {
        const auto focused = glance_.focus >= 0 && glance_.focus < static_cast<int>(glance_.feed.items.size()) ? glance_.feed.items[glance_.focus].url : L"";
        glance_.feed = std::move(message->feed); glance_.loading = false;
        renderer_.measureGlance(glance_);
        glance_.revealContent();
        if (!settings_.screen_transitions) glance_.content_age = 1;
        if (glance_.focus >= 0) {
            const auto found = std::find_if(glance_.feed.items.begin(), glance_.feed.items.end(), [&](const auto& i) { return i.url == focused; });
            glance_.focus = glance_.feed.items.empty() ? -1 : found != glance_.feed.items.end() ? static_cast<int>(found - glance_.feed.items.begin()) : 0;
        }
        glance_image_focus_->store(std::max(0,glance_.focus));
        const auto view = renderer_.viewport();
        glance_.scroll = glance_ui::Layout{view.width,view.height}.reveal(glance_.focus, static_cast<int>(glance_.feed.items.size()), glance_.scroll,&glance_);
        renderer_.resetGlanceThumbnails(glance_.feed.items.size());
    } else if (message->index < glance_.feed.items.size()) renderer_.setGlanceThumbnail(message->index, shareImage(std::move(message->bytes)));
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::glanceSection(int section) {
    section = std::clamp(section, 0, glance::section_count - 1);
    if (!glanceOpen() || section == glance_.section) return;
    glance_.direction = glance_ui::tabIndex(section) > glance_ui::tabIndex(glance_.section) ? 1 : -1;
    glance_.section = section; glance_.focus = -1; glance_.hover = -100; glance_.scroll = 0; glance_.scroll_motion.snap(0); glance_.feed = {};
    glance_.revealContent();
    renderer_.resetGlanceThumbnails(0); requestGlance(); audio_.navigate();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::glanceStepSection(int amount) {
    const int next = std::clamp(glance_ui::tabIndex(glance_.section)+amount,0,glance::section_count-1);
    const int section = glance_ui::sectionAt(next);
    if (section == glance_.section) navigationBoundary(); else glanceSection(section);
}

void Launcher::glanceMove(int dx, int dy) {
    glance_.hover = -100;
    const auto view = renderer_.viewport(); const glance_ui::Layout layout{view.width,view.height};
    const int count = static_cast<int>(glance_.feed.items.size()), old = glance_.focus;
    if (old == -1) {
        if (dx) { glanceStepSection(dx); return; }
        if (dy < 0) glance_.focus = -2;
        else if (dy > 0 && count) glance_.focus = 0;
    } else if (old < -1) {
        if (dx) glance_.focus = dx > 0 ? -3 : -2;
        if (dy > 0) glance_.focus = -1;
    } else {
        if (dy < 0 && old < layout.columns(&glance_)) glance_.focus = -1;
        else if (dy) glance_.focus = std::clamp(old + dy * layout.columns(&glance_), 0, std::max(0, count - 1));
        else if (glance_.timeline() && dx<0) glance_.focus=-1;
        else if (dx && old / layout.columns(&glance_) == (old + dx) / layout.columns(&glance_) && old + dx >= 0 && old + dx < count) glance_.focus += dx;
    }
    if (old == glance_.focus) navigationBoundary(); else audio_.navigate();
    glance_image_focus_->store(std::max(0,glance_.focus));
    glance_.scroll = layout.reveal(glance_.focus, count, glance_.scroll,&glance_);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::glanceAccept() {
    if (glance_.focus == -2) { back(); return; }
    if (glance_.focus == -3) { requestGlance(true); return; }
    if (glance_.focus == -1) { glanceMove(0,1); return; }
    if (glance_.focus < 0 || glance_.focus >= static_cast<int>(glance_.feed.items.size())) return;
    const auto& item = glance_.feed.items[glance_.focus];
    if (glance::articleUrl(item.url)) openGlanceBrowser(item.url);
}

void Launcher::openGlanceBrowser(const std::wstring& url, bool embedded) {
    if (!BrowserWindow::supportedUrl(url)) return;
    if (browse_) { browse_(url); return; }
    cancelEnterHold(); cancelTabHold();
    if (desktop_mode_ && !embedded) {
        // The desktop window: the article opens in the user's own browser, beside the launcher.
        if (execute_(hwnd_, GameAction{url, {}, {}, ActionKind::open_client}) <= 32)
            showNotification(NotificationKind::error, tr(L"Nie można otworzyć strony"), url);
        return;
    }
    if (!browser_) browser_=std::make_unique<BrowserWindow>();
    browser_->setLowMemory(settings_.low_memory);
    BrowserWindow::Options options;
    options.closed=[this] { noteUserInput(); cancelEnterHold(); cancelTabHold(); InvalidateRect(hwnd_,nullptr,FALSE); };
    options.external=[this](const std::wstring& target) {
        if (execute_(hwnd_,GameAction{target,{}, {},ActionKind::open_client})<=32)
            showNotification(NotificationKind::error,tr(L"Nie można otworzyć strony"),L"WebView2");
    };
    if (!browser_->open(hwnd_,url,userConfigDirectory()/L"browser",std::move(options)))
        showNotification(NotificationKind::error,tr(L"Nie można otworzyć strony"),L"WebView2");
}

void Launcher::glanceClick(float x, float y) {
    const auto view = renderer_.viewport(); const int hit = glance_ui::Layout{view.width,view.height}.hit(x,y,glance_);
    if (hit == -100) return;
    if (hit <= -10) { glanceSection(-10-hit); glance_.focus = -1; }
    else { glance_.focus = hit; glance_image_focus_->store(std::max(0,hit)); glanceAccept(); }
    InvalidateRect(hwnd_, nullptr, FALSE);
}
