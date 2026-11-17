// PSfive → Szczegóły pod rzędem (Opcje układu, on by default): Down from "Graj" / "…" raises a
// sheet over the game's full-screen background, as the PS5's game hub: a row of activity cards
// (achievements, play time, the description, the facts), the Steam store's screenshots (of its
// own app or the Steam app of the same title) and similar games from the library (the game
// index's genres, tags, developers and series). Down / Up go a section at a time and the page
// scrolls to it; Up from the cards goes back to the buttons; the quick bar then opens from above
// the tabs (psfive.cpp). Enter on Opis shows the whole description.
#include "launcher.hpp"
#include "../artwork/cover_cache.hpp"
#include "../artwork/steamgriddb.hpp"
#include "../net/json.hpp"
#include "../platform/atomic_file.hpp"
#include "../platform/file_io.hpp"
#include "../platform/text_encoding.hpp"
#include "../store/steam_app_match.hpp"
#include "../ui/psfive_details_layout.hpp"
#include "../ui/psfive_layout.hpp"

#include <algorithm>
#include <cmath>
#include <optional>

namespace {
constexpr size_t most_shots = 10, most_similar = 10;
std::vector<uint8_t> cachedOrDownloaded(const std::wstring& url, const std::filesystem::path& file) {
    auto bytes = file_io::readBytes(file, 12 * 1024 * 1024);
    if (!bytes.empty() && decodableArtwork(bytes)) return bytes;
    auto response = httpGet(url, L"Accept: image/jpeg, image/png\r\n", 12 * 1024 * 1024);
    if (response.status != 200 || !decodableArtwork(response.body)) return {};
    atomic_file::write(file, response.body);
    return std::move(response.body);
}
}

bool Launcher::psfiveDetailsShown() const { return psfive_details_ >= 0 && psfiveActive(); }

bool Launcher::psfiveHubWanted() const { return psfiveActive() && (psfiveActions() || psfiveDetailsShown()); }

float Launcher::psfiveDetailsLeft() const { return psfive_layout::tabs_left; }

std::vector<psfive_details::Card> Launcher::psfiveCards() {
    using psfive_details::Card;
    std::vector<Card> cards;
    if (selected_ >= cards_.size() || !isGame(cards_[selected_].game)) return cards;
    const auto& game = cards_[selected_].game;
    if (psfiveAchievements().second) cards.push_back(Card::achievements);
    bool loading = false;
    const auto& info = desktopDetails(game, loading);
    if ((info.playtime_minutes && *info.playtime_minutes) || game.store_playtime_minutes || game.last_played ||
        !plugins_.contributions("details", &game).empty()) cards.push_back(Card::playtime);
    cards.push_back(Card::description);
    cards.push_back(Card::facts);
    return cards;
}

// Unlocked and total: Steam's or the game index's, else a DRM-free game's local list or an
// achievements plugin's (requested with the hub, as on Szczegóły).
std::pair<uint32_t, uint32_t> Launcher::psfiveAchievements() {
    if (selected_ >= cards_.size() || !isGame(cards_[selected_].game)) return {0, 0};
    const auto& game = cards_[selected_].game;
    bool loading = false;
    const auto& info = desktopDetails(game, loading);
    if (info.achievements_total) return {info.achievements_unlocked, info.achievements_total};
    if (const auto* local = localAchievements(game); local && local->list.total) return {local->list.unlocked, local->list.total};
    if (const auto* known = pluginAchievements(game); known && known->list.total) return {known->list.unlocked, known->list.total};
    return {0, 0};
}

float Launcher::psfiveRaise() const {
    const float height = renderer_.viewport().height;
    const float hub = psfive_layout::hubEase(psfive_hub_);
    const float peeked = height - psfive_details::peek * hub;
    const float t = std::clamp(psfive_details_progress_, 0.0f, 1.0f), rise = t * t * (3 - 2 * t); // As drawn (smoothFade).
    return psfive_layout::hub_lift * hub + (peeked - psfive_details::sheetTop(height, hub, rise)) + psfive_details_scroll_;
}

void Launcher::psfiveOpenDetails() {
    if (!psfiveButtons() || selected_ >= cards_.size()) { navigationBoundary(); return; }
    psfive_shot_view_ = psfive_text_view_ = false;
    psfive_focus_glide_ = false;
    psfiveLoadDetails(cards_[selected_].game);
    // The focus goes to the activity cards right under the buttons, on their first.
    psfive_details_ = 0;
    psfive_card_ = 0;
    audio_.navigate();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::psfiveCloseDetails(bool to_buttons) {
    if (psfive_details_ < 0) return;
    psfive_details_ = -1;
    psfive_shot_view_ = psfive_text_view_ = false;
    if (to_buttons && psfiveButtons()) psfive_action_ = std::max(0, psfive_action_);
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::psfiveDetailsBack() {
    if (psfive_shot_view_ || psfive_text_view_) {
        psfive_shot_view_ = psfive_text_view_ = false;
        audio_.navigate(); InvalidateRect(hwnd_, nullptr, FALSE); return;
    }
    psfiveCloseDetails(true);
    audio_.navigate();
}

// The game's similar games now; its screenshots on a worker (cached in artwork\screenshots).
void Launcher::psfiveLoadDetails(const Game& game) {
    const auto key = gameKey(game);
    if (key == psfive_details_key_) return;
    psfive_details_key_ = key;
    const auto generation = ++psfive_details_generation_;
    psfive_shot_ = psfive_similar_ = 0;
    psfive_shots_scroll_ = psfive_similar_scroll_ = 0;
    psfive_shot_full_urls_.clear();
    psfive_shots_loading_ = true;
    psfive_similar_games_.clear();
    psfive_similar_bound_.fill(false);
    if (game_index_ && !key.empty()) {
        const auto similar = game_index_->read([&](const game_index::Index& index) {
            std::vector<std::string> keys;
            for (const auto& [record, score] : index.similar(key, most_similar * 2)) if (record) keys.push_back(record->key);
            return keys;
        });
        for (const auto& wanted : similar) {
            if (psfive_similar_games_.size() >= most_similar) break;
            const auto found = std::find_if(grouped_library_.begin(), grouped_library_.end(), [&](const Game& item) { return gameKey(item) == wanted; });
            if (found != grouped_library_.end() && isGame(*found)) psfive_similar_games_.push_back(*found);
        }
    }
    renderer_.resetPsfiveDetails(0, psfive_similar_games_.size());
    preview_.request(game, account_);
    uint32_t linked = steamArtwork(game) ? game.app_id : 0;
    if (!linked) if (const auto found = rawg_metadata_.find(key); found != rawg_metadata_.end()) linked = found->second.steam_app_id;
    const bool by_title = trailerByTitle(game);
#ifdef LAUNCHME_UI_TEST
    psfive_shots_loading_ = false; // UI tests never ask the Steam store; they set the screenshots themselves.
    (void)linked; (void)by_title; (void)generation;
#else
    work_.start([alive = alive_, hwnd = hwnd_, generation, game, linked, by_title] {
        uint32_t app = linked;
        const auto root = userConfigDirectory();
        if (!app) if (const auto source = selectedArtworkSource(game, coverCacheDirectory()); source.steam) app = source.id;
        if (!app && by_title) app = steamAppForTitle(artworkTitle(game), root / L"metadata", gameCacheKey(game));
        std::vector<std::wstring> thumbs, fulls;
        if (app) {
            HttpBudget budget(30000);
            const auto folder = root / L"artwork" / L"screenshots";
            const auto list = folder / (std::to_wstring(app) + L".json");
            auto body = file_io::readText(list, 2 * 1024 * 1024);
            if (body.empty()) {
                const auto response = httpGet(L"https://store.steampowered.com/api/appdetails?appids=" + std::to_wstring(app) + L"&filters=screenshots",
                                              L"Accept: application/json\r\n", 2 * 1024 * 1024);
                if (response.status == 200) { body.assign(response.body.begin(), response.body.end()); atomic_file::write(list, body); }
            }
            if (const auto parsed = json::parse(body)) {
                const auto* entry = parsed->get(std::to_string(app));
                const auto* data = entry ? entry->get("data") : nullptr;
                if (const auto* shots = data ? data->get("screenshots") : nullptr; shots && shots->type == json::Value::Type::array)
                    for (const auto& shot : shots->elements) {
                        if (thumbs.size() >= most_shots) break;
                        const auto thumb = shot.get("path_thumbnail"), full = shot.get("path_full");
                        if (!thumb || !full) continue;
                        thumbs.push_back(text_encoding::wideLenient(thumb->stringOr()));
                        fulls.push_back(text_encoding::wideLenient(full->stringOr()));
                    }
            }
        }
        std::vector<std::vector<uint8_t>> pictures;
        for (size_t i = 0; i < thumbs.size(); ++i) {
            if (!alive->load()) return;
            pictures.push_back(cachedOrDownloaded(thumbs[i], root / L"artwork" / L"screenshots" / (std::to_wstring(app) + L"_" + std::to_wstring(i) + L".img")));
        }
        for (size_t i = 0; i < fulls.size(); ++i) // The full picture's cache file, by the same number.
            fulls[i] = std::to_wstring(app) + L"_" + std::to_wstring(i) + L"_full.img|" + fulls[i];
        if (!alive->load()) return;
        auto message = std::make_unique<RemoteMessage>();
        message->run = [generation, fulls = std::move(fulls), pictures = std::move(pictures)](Launcher& launcher) mutable {
            launcher.psfiveShotsArrived(generation, std::move(fulls), std::move(pictures));
        };
        if (PostMessageW(hwnd, remote_message, 0, reinterpret_cast<LPARAM>(message.get()))) message.release();
    });
#endif
}

void Launcher::psfiveShotsArrived(uint32_t generation, std::vector<std::wstring> fulls, std::vector<std::vector<uint8_t>> pictures) {
    if (generation != psfive_details_generation_) return;
    psfive_shots_loading_ = false;
    // Only those that arrived: a broken link leaves no empty frame.
    psfive_shot_full_urls_.clear();
    std::vector<std::vector<uint8_t>> kept;
    for (size_t i = 0; i < pictures.size() && i < fulls.size(); ++i)
        if (!pictures[i].empty()) { kept.push_back(std::move(pictures[i])); psfive_shot_full_urls_.push_back(std::move(fulls[i])); }
    renderer_.resetPsfiveDetails(kept.size(), psfive_similar_games_.size());
    for (size_t i = 0; i < kept.size(); ++i) renderer_.setPsfiveShot(i, shareImage(std::move(kept[i])));
    psfive_shot_ = std::min(psfive_shot_, kept.empty() ? size_t{0} : kept.size() - 1);
    psfive_similar_bound_.fill(false); // Their slots were made again.
    // None after all while their strip had the focus: the similar games (else the cards) take it.
    if (psfive_details_ == 1 && kept.empty()) psfive_details_ = psfive_similar_games_.empty() ? 0 : 2;
    InvalidateRect(hwnd_, nullptr, FALSE);
}

// Enter on a screenshot: shown full screen, its full-size picture fetched (and cached) meanwhile.
void Launcher::psfiveViewShot() {
    if (psfive_shot_ >= psfive_shot_full_urls_.size()) return;
    psfive_shot_view_ = true;
    renderer_.setPsfiveShotFull({});
    const auto entry = psfive_shot_full_urls_[psfive_shot_];
    const auto bar = entry.find(L'|');
    if (bar == std::wstring::npos) return;
    const auto file = userConfigDirectory() / L"artwork" / L"screenshots" / entry.substr(0, bar);
    const auto url = entry.substr(bar + 1);
    const auto generation = psfive_details_generation_;
    const auto shot = psfive_shot_;
    work_.start([alive = alive_, hwnd = hwnd_, generation, shot, file, url] {
        auto bytes = cachedOrDownloaded(url, file);
        if (!alive->load() || bytes.empty()) return;
        auto message = std::make_unique<RemoteMessage>();
        message->run = [generation, shot, bytes = std::move(bytes)](Launcher& launcher) mutable {
            if (generation != launcher.psfive_details_generation_ || shot != launcher.psfive_shot_ || !launcher.psfive_shot_view_) return;
            launcher.renderer_.setPsfiveShotFull(shareImage(std::move(bytes)));
            InvalidateRect(launcher.hwnd_, nullptr, FALSE);
        };
        if (PostMessageW(hwnd, remote_message, 0, reinterpret_cast<LPARAM>(message.get()))) message.release();
    });
    audio_.context();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::psfiveDetailsVertical(int amount) {
    if (psfive_shot_view_ || psfive_text_view_) { navigationBoundary(); return; }
    // A section at a time: the cards, the screenshots (while there are or may be some), the similar games.
    const bool shots = !psfive_shot_full_urls_.empty(), similar = !psfive_similar_games_.empty();
    int next = psfive_details_;
    if (amount > 0) {
        if (next == 0 && shots) next = 1;
        else if (next <= 1 && similar) next = 2;
    } else {
        if (next == 2 && shots) next = 1;
        else if (next >= 1) next = 0;
        else { psfiveCloseDetails(true); audio_.navigate(); return; }
    }
    if (next == psfive_details_) { navigationBoundary(); return; }
    psfive_details_ = next;
    audio_.navigate();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::psfiveDetailsHorizontal(int amount) {
    if (!amount || psfive_text_view_) { if (psfive_text_view_) navigationBoundary(); return; }
    const auto step = [&](size_t& index, size_t count) {
        const int next = std::clamp(static_cast<int>(index) + (amount < 0 ? -1 : 1), 0, std::max(0, static_cast<int>(count) - 1));
        if (next == static_cast<int>(index)) return false;
        index = static_cast<size_t>(next);
        return true;
    };
    bool moved = false;
    if (psfive_details_ == 1 || psfive_shot_view_) {
        moved = step(psfive_shot_, psfive_shot_full_urls_.size());
        if (moved && psfive_shot_view_) psfiveViewShot();
    } else if (psfive_details_ == 2) moved = step(psfive_similar_, psfive_similar_games_.size());
    else if (psfive_details_ == 0) moved = step(psfive_card_, psfiveCards().size());
    if (!moved) { navigationBoundary(); return; }
    audio_.navigate();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::psfiveDetailsAccept() {
    if (psfive_shot_view_ || psfive_text_view_) {
        psfive_shot_view_ = psfive_text_view_ = false;
        audio_.context(); InvalidateRect(hwnd_, nullptr, FALSE); return;
    }
    if (psfive_details_ == 1) { psfiveViewShot(); return; }
    if (psfive_details_ == 2 && psfive_similar_ < psfive_similar_games_.size()) {
        const auto game = psfive_similar_games_[psfive_similar_];
        psfiveCloseDetails(false);
        openDetails(game);
        return;
    }
    if (selected_ >= cards_.size()) return;
    const auto cards = psfiveCards();
    const auto card = psfive_card_ < cards.size() ? cards[psfive_card_] : psfive_details::Card::facts;
    if (card == psfive_details::Card::description) { // The whole description over the screen.
        psfive_text_view_ = true;
        audio_.context();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    // The others: the game's full Szczegóły (achievements: their list over it).
    const auto game = cards_[selected_].game;
    psfiveCloseDetails(false);
    openDetails(game);
    if (card == psfive_details::Card::achievements) openAchievements();
}

// A click on a screenshot or a similar game (the sheet's strips); true when it took the click.
bool Launcher::psfiveDetailsClick(float x, float y) {
    if (!psfiveDetailsShown()) return false;
    if (psfive_shot_view_ || psfive_text_view_) { psfive_shot_view_ = psfive_text_view_ = false; InvalidateRect(hwnd_, nullptr, FALSE); return true; }
    const auto view = renderer_.viewport();
    const float hub = psfive_layout::hubEase(psfive_hub_), left = psfiveDetailsLeft();
    const float top = psfive_details::sheetTop(view.height, hub, psfive_details_progress_) - psfive_details_scroll_;
    const auto cards = psfiveCards();
    for (size_t i = 0; i < cards.size(); ++i)
        if (const auto r = psfive_details::card(i, view.width, top, left); r.contains(x, y) && r.top >= psfive_details::open_top - 20) {
            const bool again = psfive_details_ == 0 && psfive_card_ == i;
            psfive_details_ = 0; psfive_card_ = i;
            if (again) psfiveDetailsAccept(); else { audio_.navigate(); InvalidateRect(hwnd_, nullptr, FALSE); }
            return true;
        }
    for (size_t i = 0; i < psfive_shot_full_urls_.size(); ++i)
        if (psfive_details::shot(i, psfive_shots_scroll_, top, left).contains(x, y) && x >= left - 1) {
            const bool again = psfive_details_ == 1 && psfive_shot_ == i;
            psfive_details_ = 1; psfive_shot_ = i;
            if (again) psfiveViewShot(); else { audio_.navigate(); InvalidateRect(hwnd_, nullptr, FALSE); }
            return true;
        }
    for (size_t i = 0; i < psfive_similar_games_.size(); ++i)
        if (psfive_details::similar(i, psfive_similar_scroll_, top, left, psfive_similar_up_).contains(x, y) && x >= left - 1) {
            const bool again = psfive_details_ == 2 && psfive_similar_ == i;
            psfive_details_ = 2; psfive_similar_ = i;
            if (again) psfiveDetailsAccept(); else { audio_.navigate(); InvalidateRect(hwnd_, nullptr, FALSE); }
            return true;
        }
    return y >= psfive_details::sheetTop(view.height, hub, psfive_details_progress_);
}

void Launcher::tickPsfiveDetails(float dt, bool& needs_paint) {
    const bool shown = psfiveDetailsShown();
    const float target = shown ? 1.0f : 0.0f;
    const float ease = 1.0f - std::exp(-dt * 8.0f);
    // The hub: two critically damped springs (psfive_layout::hub_omega, corner_omega).
    const float hub_target = psfiveHubWanted() ? 1.0f : 0.0f;
    const auto move = [&](Spring& motion, float& value, float omega) {
        if (!psfiveActive()) { motion.snap(0); value = 0; return; }
        if (motion.value == hub_target && motion.velocity == 0) return;
        motion.step(hub_target, dt, omega, 1.0f);
        if (motion.settled(hub_target, 0.001f)) motion.snap(hub_target);
        value = std::clamp(motion.value, 0.0f, 1.0f);
        needs_paint = true;
    };
    move(psfive_hub_motion_, psfive_hub_, psfive_layout::hub_omega);
    move(psfive_corner_motion_, psfive_corner_, psfive_layout::corner_omega);
    // No screenshots (none found, not still loading): the similar games close the gap, gliding up.
    const float up = psfive_shot_full_urls_.empty() && !psfive_shots_loading_ ? 1.0f : 0.0f;
    if (psfive_similar_up_ != up) {
        if (psfive_details_progress_ < 0.01f) psfive_similar_up_ = up; // Unseen: no glide.
        else {
            psfive_similar_up_ += (up - psfive_similar_up_) * (1.0f - std::exp(-dt * 12.0f));
            if (std::abs(psfive_similar_up_ - up) < 0.002f) psfive_similar_up_ = up;
        }
        needs_paint = true;
    }
    // The sheet peeking under the buttons: its game's description and facts load with the hub.
    if (psfive_hub_ > 0 && settings_.psfive_details && selected_ < cards_.size() && psfiveButtons()) {
        if (gameKey(cards_[selected_].game) != psfive_details_key_) psfiveLoadDetails(cards_[selected_].game);
        if (preview_.request(cards_[selected_].game, account_)) needs_paint = true;
        requestFocusedAchievements(cards_[selected_].game); // Local and plugin achievements (once per game).
        requestGameComparisons(cards_[selected_].game);     // The details plugin's times (HowLongToBeat).
    }
    if (std::abs(psfive_details_progress_ - target) > 0.001f) {
        psfive_details_progress_ += (target - psfive_details_progress_) * ease;
        if (std::abs(psfive_details_progress_ - target) <= 0.001f) psfive_details_progress_ = target;
        needs_paint = true;
    }
    // The page scrolls a section at a time (a critically damped spring, as the hub).
    {
        const float wanted = shown ? psfive_details::sectionScroll(psfive_details_, psfive_similar_up_) : 0.0f;
        if (psfive_page_motion_.value != wanted || psfive_page_motion_.velocity != 0) {
            psfive_page_motion_.step(wanted, dt, 15.0f, 1.0f);
            if (psfive_page_motion_.settled(wanted, 0.3f)) psfive_page_motion_.snap(wanted);
            needs_paint = true;
        }
        // The frame rides with the page (its own glide only between items).
        if (psfive_focus_glide_) { const float moved = psfive_page_motion_.value - psfive_details_scroll_; psfive_focus_box_[1] -= moved; psfive_focus_box_[3] -= moved; }
        psfive_details_scroll_ = psfive_page_motion_.value;
    }
    if (!shown) { psfive_focus_glide_ = false; return; }
    // The focus frame glides between the screenshots and the similar games, as the tiles' outline does.
    const auto view = renderer_.viewport();
    const float left = psfiveDetailsLeft();
    const float progress = std::clamp(psfive_details_progress_, 0.0f, 1.0f), rise = progress * progress * (3 - 2 * progress); // As drawn.
    const float top = psfive_details::sheetTop(view.height, psfive_layout::hubEase(psfive_hub_), rise) - psfive_details_scroll_;
    // The strips slide to keep their focused item in view, and the frame slides with its strip.
    const float shot_step = psfive_details::shot_width + psfive_details::shot_gap;
    const float similar_step = psfive_details::similar_size + psfive_details::similar_gap;
    const auto slide = [&](float& scroll, float wanted) {
        const float before = scroll;
        if (std::abs(wanted - scroll) > 0.3f) { scroll += (wanted - scroll) * (1.0f - std::exp(-dt * 14.0f)); needs_paint = true; }
        else scroll = wanted;
        return scroll - before;
    };
    const float shots_moved = slide(psfive_shots_scroll_, psfive_details::scrollFor(psfive_shot_, psfive_shot_full_urls_.size(), view.width, shot_step, left));
    const float similar_moved = slide(psfive_similar_scroll_, psfive_details::scrollFor(psfive_similar_, psfive_similar_games_.size(), view.width, similar_step, left));
    if (psfive_focus_glide_) {
        const float moved = psfive_details_ == 1 ? shots_moved : psfive_details_ == 2 ? similar_moved : 0.0f;
        psfive_focus_box_[0] -= moved; psfive_focus_box_[2] -= moved;
    }
    std::optional<settings_ui::Rect> focus;
    if (const auto cards = psfiveCards(); psfive_details_ == 0 && !cards.empty()) {
        psfive_card_ = std::min(psfive_card_, cards.size() - 1);
        focus = psfive_details::card(psfive_card_, view.width, top, left);
    } else if (psfive_details_ == 1 && psfive_shot_ < psfive_shot_full_urls_.size()) focus = psfive_details::shot(psfive_shot_, psfive_shots_scroll_, top, left);
    else if (psfive_details_ == 2 && psfive_similar_ < psfive_similar_games_.size()) focus = psfive_details::similar(psfive_similar_, psfive_similar_scroll_, top, left, psfive_similar_up_);
    if (!focus) psfive_focus_glide_ = false;
    else if (!psfive_focus_glide_) { psfive_focus_box_ = {focus->left, focus->top, focus->right, focus->bottom}; psfive_focus_glide_ = true; needs_paint = true; }
    else {
        const float glide = 1.0f - std::exp(-dt * 16.0f);
        const float wanted[4] = {focus->left, focus->top, focus->right, focus->bottom};
        for (size_t i = 0; i < 4; ++i) {
            if (std::abs(psfive_focus_box_[i] - wanted[i]) > 0.2f) { psfive_focus_box_[i] += (wanted[i] - psfive_focus_box_[i]) * glide; needs_paint = true; }
            else psfive_focus_box_[i] = wanted[i];
        }
    }
    if (selected_ < cards_.size()) {
        if (gameKey(cards_[selected_].game) != psfive_details_key_) psfiveLoadDetails(cards_[selected_].game);
        if (preview_.request(cards_[selected_].game, account_)) needs_paint = true;
    }
    bindSimilarCovers(needs_paint); // The similar games' covers (src/app/details_media.cpp).
}
