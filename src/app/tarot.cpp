// Tarot (Settings → System → Funkcje, off by default): a card reading over the home that ends with
// up to three games. T on the home, View/Select on a pad or the desktop window's top bar opens
// it. The deck and the scoring live in src/library/tarot.hpp, the drawing in
// src/ui/tarot_renderer.cpp, the geometry in src/ui/tarot_view.hpp. A modal layer: it takes keys,
// pad buttons and clicks until it closes. A library reading is scored at once from the game index;
// a store reading asks Steam's search on a worker while the deck shuffles.
#include "launcher.hpp"
#include "../library/steam.hpp"

using tarot_ui::Phase;

namespace {
bool english() { return i18n::english(); }
}

bool Launcher::tarotAvailable() const { return settings_.tarot && homeOverlayAvailable(); }

void Launcher::openTarot() {
    if (!tarotAvailable()) return;
    cancelEnterHold(); cancelTabHold(); closeKeyboard();
    tarot_cancel_->store(true);
    tarot_cancel_ = std::make_shared<std::atomic_bool>(false);
    ++tarot_generation_;
    const float alpha = tarot_.alpha; // Reopened while still fading out: it fades back in from there.
    tarot_ = {};
    tarot_.alpha = alpha;
    tarot_.open = true;
    tarot_seed_ = tarot_seed_ * 1664525u + 1013904223u + static_cast<uint32_t>(GetTickCount64());
    tarot_.reading = tarot::begin(tarot_seed_);
    renderer_.resetTarotImages(0);
    audio_.quick();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::closeTarot() {
    if (!tarot_.open) return;
    tarot_.open = false;
    tarot_.waiting = false;
    tarot_cancel_->store(true);
    ++tarot_generation_;
    audio_.navigate();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::tarotAgain() {
    if (!tarot_.open) return;
    tarot_cancel_->store(true);
    tarot_cancel_ = std::make_shared<std::atomic_bool>(false);
    ++tarot_generation_;
    const float alpha = tarot_.alpha, time = tarot_.time;
    tarot_ = {};
    tarot_.open = true; tarot_.alpha = alpha; tarot_.time = time;
    tarot_seed_ = tarot_seed_ * 1664525u + 1013904223u + static_cast<uint32_t>(GetTickCount64());
    tarot_.reading = tarot::begin(tarot_seed_);
    renderer_.resetTarotImages(0);
    audio_.context();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

bool Launcher::tickTarot(float dt) {
    auto& t = tarot_;
    const bool animated = settings_.screen_transitions;
    const float target = t.open ? 1.0f : 0.0f;
    bool moving = false;
    if (t.alpha != target) {
        t.alpha = animated ? std::clamp(t.alpha + (t.open ? dt : -dt) / 0.28f, 0.0f, 1.0f) : target;
        moving = true;
        if (!t.open && t.alpha <= 0) { renderer_.resetTarotImages(0); t.results.clear(); }
    }
    if (!t.open) return moving;
    t.time += dt;
    t.phase_age += animated ? dt : 1000.0f; // Without transitions every phase is already played.
    if (t.phase == Phase::leaving && t.phase_age >= tarot_ui::leave_time) {
        tarot::choose(t.reading, static_cast<size_t>(std::max(0, t.chosen)));
        t.chosen = -1;
        if (t.reading.complete()) tarotDivine();
        else { t.phase = Phase::question; t.phase_age = 0; t.focus = 0; t.lift.fill(0); }
    }
    if (t.phase == Phase::divining && t.phase_age >= tarot_ui::divining_min && !t.waiting) tarotReveal();
    const float follow = animated ? std::min(1.0f, dt * 14.0f) : 1.0f;
    for (size_t i = 0; i < t.lift.size(); ++i) {
        const int index = static_cast<int>(i);
        const float lift = t.phase == Phase::question ? (t.focus == index ? 1.0f : t.hover == index ? 0.55f : 0.0f) : 0.0f;
        t.lift[i] += (lift - t.lift[i]) * follow;
    }
    return true; // Its stars twinkle and its sigil turns while it is open.
}

void Launcher::tarotKey(WPARAM key, bool repeated) {
    auto& t = tarot_;
    if (!t.open) return;
    if (key == VK_ESCAPE) { if (!repeated) closeTarot(); return; }
    if (key == VK_BACK) { if (!repeated) tarotBack(); return; }
    if (key == VK_RETURN || key == VK_SPACE) { if (!repeated) tarotAccept(); return; }
    const int dx = key == VK_LEFT || key == 'A' ? -1 : key == VK_RIGHT || key == 'D' ? 1 : 0;
    const int dy = key == VK_UP || key == 'W' ? -1 : key == VK_DOWN || key == 'S' ? 1 : 0;
    if (dx || dy) tarotMove(dx, dy);
}

void Launcher::tarotMove(int dx, int dy) {
    auto& t = tarot_;
    t.hover = tarot_ui::no_hit;
    const int before = t.focus;
    if (t.phase == Phase::question) {
        const auto* spread = t.spread();
        const int count = spread ? static_cast<int>(spread->cards.size()) : 0;
        if (t.focus == tarot_ui::focus_back) { if (dy > 0 || dx > 0) t.focus = 0; }
        else if (dy < 0) t.focus = tarot_ui::focus_back;
        else if (dx) t.focus = std::clamp(t.focus + dx, 0, std::max(0, count - 1));
    } else if (t.phase == Phase::reading) {
        const int count = static_cast<int>(t.results.size());
        if (t.focus == tarot_ui::focus_back) { if (dy > 0 || dx > 0) t.focus = count ? 0 : tarot_ui::focus_again; }
        else if (t.focus >= 0) {
            if (dy > 0) t.focus = tarot_ui::focus_again;
            else if (dy < 0) t.focus = tarot_ui::focus_back;
            else if (dx) t.focus = std::clamp(t.focus + dx, 0, std::max(0, count - 1));
        } else { // The buttons.
            if (dy < 0) t.focus = count ? std::min(1, count - 1) : tarot_ui::focus_back;
            else if (dx < 0) t.focus = tarot_ui::focus_again;
            else if (dx > 0) t.focus = tarot_ui::focus_close;
        }
    } else return;
    if (t.focus == before) navigationBoundary(); else audio_.navigate();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::tarotAccept() {
    auto& t = tarot_;
    if (t.focus == tarot_ui::focus_back) { closeTarot(); return; }
    if (t.phase == Phase::question) { tarotChoose(t.focus); return; }
    if (t.phase != Phase::reading) return;
    if (t.focus == tarot_ui::focus_again) { tarotAgain(); return; }
    if (t.focus == tarot_ui::focus_close) { closeTarot(); return; }
    tarotOpenResult(t.focus);
}

void Launcher::tarotChoose(int card) {
    auto& t = tarot_;
    const auto* spread = t.phase == Phase::question ? t.reading.current() : nullptr;
    if (!spread || card < 0 || card >= static_cast<int>(spread->cards.size())) return;
    if (settings_.screen_transitions && t.phase_age < 0.3f) return; // Still flying in.
    t.phase = Phase::leaving;
    t.phase_age = 0;
    t.chosen = t.focus = card;
    audio_.context();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::tarotBack() {
    auto& t = tarot_;
    if (t.phase == Phase::reading || t.phase == Phase::divining) { closeTarot(); return; }
    if (t.phase == Phase::leaving) return;
    if (!tarot::undo(t.reading)) { closeTarot(); return; } // The first question: Back closes it.
    t.phase = Phase::question; t.phase_age = 0; t.focus = 0; t.lift.fill(0);
    audio_.navigate();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

std::vector<tarot::Pick> Launcher::tarotLibraryPicks(const tarot::Wish& wish) const {
    // The library as the launcher shows it; the game index adds what it knows (genres, release,
    // reviews, length, play time). Games it does not know yet take part with their name only.
    std::vector<const Game*> games;
    for (const auto& game : public_library_) {
        if (!isGame(game) || game.kind == GameKind::catalog || game.visibility != GameVisibility::public_game || locallyHidden(game) ||
            game.name.empty() || placeholderGameName(game.name) || gameKey(game).empty() || (game.source == GameSource::steam && steamTool(game))) continue;
        games.push_back(&game);
    }
    const auto score = [&](const game_index::Index* index) {
        std::vector<game_index::Record> made;
        made.reserve(games.size());
        std::vector<const game_index::Record*> records;
        for (const auto* game : games) {
            const auto key = gameKey(*game);
            if (const auto* known = index ? index->get(key) : nullptr) { records.push_back(known); continue; }
            game_index::Record record;
            record.key = key; record.title = game->name; record.installed = game->installed; record.last_played = game->last_played;
            made.push_back(std::move(record));
        }
        for (const auto& record : made) records.push_back(&record);
        return tarot::pickLibrary(records, wish, tarot_seed_, english());
    };
    auto picks = game_index_ ? game_index_->read([&](const game_index::Index& index) { return score(&index); }) : score(nullptr);
    // The library's name wins over the index's (Edytuj metadane, versions).
    for (auto& pick : picks)
        for (const auto* game : games) if (gameKey(*game) == pick.key) { pick.title = game->name; break; }
    return picks;
}

void Launcher::tarotDivine() {
    auto& t = tarot_;
    t.phase = Phase::divining;
    t.phase_age = 0;
    t.results.clear();
    t.failed = false;
    t.pool = t.reading.pool();
    tarot_pending_.clear();
    const auto wish = tarot::wish(t.reading);
    audio_.drawer();
    if (t.pool == tarot::Pool::library) { tarot_pending_ = tarotLibraryPicks(wish); t.waiting = false; return; }
    // Steam: games the player has in any Steam account of the library are not for sale to them.
    std::unordered_set<uint32_t> owned;
    for (const auto* source : {&installed_library_, &uninstalled_library_, &public_library_})
        for (const auto& game : *source) if (game.source == GameSource::steam && game.app_id) owned.insert(game.app_id);
    t.waiting = true;
    const auto generation = ++tarot_generation_;
    const auto cancel = tarot_cancel_;
    const auto deliver = [post = uiPost(), generation](std::vector<tarot::Pick> picks, bool failed) {
        post([generation, picks = std::move(picks), failed](Launcher& app) mutable { app.tarotStoreFinished(generation, std::move(picks), failed); });
    };
    const bool started = work_.start([wish, owned, seed = tarot_seed_, en = english(), get = tarot_get_, cancel, deliver,
                                      cache = userConfigDirectory() / L"tarot"] {
        HttpCancellation cancellation(*cancel);
        const auto items = tarot::searchStore(wish, get, cache); // Pages of the last 30 minutes come from disk.
        if (cancel->load()) return;
        deliver(items ? tarot::pickStore(*items, wish, owned, seed, en) : std::vector<tarot::Pick>{}, !items);
    }, [deliver](std::exception_ptr) { deliver({}, true); });
    if (!started) { t.waiting = false; t.failed = true; }
}

void Launcher::tarotStoreFinished(uint32_t generation, std::vector<tarot::Pick> picks, bool failed) {
    if (generation != tarot_generation_ || !tarot_.open || tarot_.phase != Phase::divining) return;
    tarot_pending_ = std::move(picks);
    tarot_.failed = failed;
    tarot_.waiting = false;
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::tarotReveal() {
    auto& t = tarot_;
    t.phase = Phase::reading;
    t.phase_age = 0;
    t.results.clear();
    for (const auto& pick : tarot_pending_) {
        tarot_ui::Result result;
        result.key = pick.key; result.title = pick.title; result.detail = pick.detail;
        if (t.pool == tarot::Pool::store) {
            result.url = L"https://store.steampowered.com/app/" + std::to_wstring(pick.app_id) + L"/";
            result.game.app_id = pick.app_id; result.game.name = pick.title;
            result.game.kind = GameKind::catalog; result.game.source = GameSource::steam; result.game.installed = false;
        } else {
            const auto found = std::find_if(public_library_.begin(), public_library_.end(), [&](const Game& game) { return gameKey(game) == pick.key; });
            if (found == public_library_.end()) continue; // Left the library meanwhile.
            result.game = *found;
            if (result.detail.empty()) result.detail = activePlatformName(result.game); // Nothing known yet: its store.
        }
        t.results.push_back(std::move(result));
        if (t.results.size() == tarot_ui::max_results) break;
    }
    tarot_pending_.clear();
    t.focus = t.results.empty() ? tarot_ui::focus_again : 0;
    bindTarotArt();
    audio_.quick();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::bindTarotArt() {
    renderer_.resetTarotImages(tarot_.results.size());
    for (size_t i = 0; i < tarot_.results.size(); ++i) {
        const auto& game = tarot_.results[i].game;
        const auto* art = art_.find(gameKey(game));
        if (art && art->poster) renderer_.setTarotImage(i, art->poster, true);
        else if (art && art->cover) renderer_.setTarotImage(i, art->cover, false);
        // The poster, and the square cover to stand in until it comes (or when there is none).
        if (!art || !art->poster) requestArt(game, CoverMessage::Kind::poster);
        if (!art || (!art->poster && !art->cover)) requestArt(game, CoverMessage::Kind::cover);
    }
}

void Launcher::tarotArtArrived(const std::string& key, CoverMessage::Kind kind, const ImageBytes& image) {
    if (tarot_.alpha <= 0 || !image || (kind != CoverMessage::Kind::poster && kind != CoverMessage::Kind::cover)) return;
    for (size_t i = 0; i < tarot_.results.size(); ++i) {
        if (tarot_.results[i].key != key) continue;
        if (kind == CoverMessage::Kind::poster) renderer_.setTarotImage(i, image, true);
        else if (!renderer_.tarotPoster(i)) renderer_.setTarotImage(i, image, false);
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

void Launcher::tarotOpenResult(int index) {
    auto& t = tarot_;
    if (index < 0 || index >= static_cast<int>(t.results.size())) return;
    const auto result = t.results[static_cast<size_t>(index)];
    if (!result.url.empty()) { openGlanceBrowser(result.url); return; } // The reading stays under the store page.
    closeTarot();
    openDetails(result.game);
}

void Launcher::tarotClick(float x, float y) {
    auto& t = tarot_;
    const auto view = renderer_.viewport();
    const int hit = tarot_ui::Layout{view.width, view.height}.hit(t, x, y);
    if (hit == tarot_ui::no_hit) return;
    if (hit == tarot_ui::focus_back) { closeTarot(); return; }
    t.focus = hit;
    tarotAccept();
}

void Launcher::tarotPointer(float x, float y) {
    auto& t = tarot_;
    const auto view = renderer_.viewport();
    const int hit = tarot_ui::Layout{view.width, view.height}.hit(t, x, y);
    if (hit == t.hover) return;
    t.hover = hit;
    if (hit != tarot_ui::no_hit && (t.phase == Phase::question || t.phase == Phase::reading)) t.focus = hit;
    InvalidateRect(hwnd_, nullptr, FALSE);
}

bool Launcher::tarotPointerOver(float x, float y) const {
    const auto view = renderer_.viewport();
    return tarot_ui::Layout{view.width, view.height}.hit(tarot_, x, y) != tarot_ui::no_hit;
}
