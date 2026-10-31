// PSfive → Szczegóły pod rzędem (Opcje układu, on by default): Down from "Graj" / "…" raises a
// sheet over the game's full-screen background, as the PS5's game hub: the description and the
// facts (developer, publisher, release, genres, playtime), the Steam store's screenshots (of its
// own app or the Steam app of the same title) and similar games from the library (the game
// index's genres, tags, developers and series). Only the two strips take the focus; Up from the first goes
// back to the buttons; the quick bar then opens from above the tabs (psfive.cpp).
#include "launcher.hpp"
#include "../artwork/cover_cache.hpp"
#include "../artwork/steamgriddb.hpp"
#include "../net/json.hpp"
#include "../platform/atomic_file.hpp"
#include "../platform/file_io.hpp"
#include "../platform/text_encoding.hpp"
#include "../store/steam_app_match.hpp"
#include "../ui/psfive_details_layout.hpp"

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

void Launcher::psfiveOpenDetails() {
    if (!psfiveButtons() || selected_ >= cards_.size()) { navigationBoundary(); return; }
    psfive_shot_view_ = false;
    psfive_focus_glide_ = false;
    psfiveLoadDetails(cards_[selected_].game);
    // The focus goes to the first strip (screenshots, else similar games); the cards above it are read, not focused.
    // While the screenshots load it waits on the cards (no frame) and moves when they arrive (psfiveShotsArrived).
    psfive_details_ = !psfive_shot_full_urls_.empty() ? 1 : psfive_shots_loading_ || psfive_similar_games_.empty() ? 0 : 2;
    audio_.navigate();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::psfiveCloseDetails(bool to_buttons) {
    if (psfive_details_ < 0) return;
    psfive_details_ = -1;
    psfive_shot_view_ = false;
    if (to_buttons && psfiveButtons()) psfive_action_ = std::max(0, psfive_action_);
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::psfiveDetailsBack() {
    if (psfive_shot_view_) { psfive_shot_view_ = false; audio_.navigate(); InvalidateRect(hwnd_, nullptr, FALSE); return; }
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
    // Arrived: the screenshots take the focus kept on the cards while they loaded; none: the similar games do.
    if (psfive_details_ == 0 || psfive_details_ == 1) psfive_details_ = !kept.empty() ? 1 : psfive_similar_games_.empty() ? 0 : 2;
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
    if (psfive_shot_view_) { navigationBoundary(); return; }
    // Only the strips take the focus: screenshots, then similar games; Up from the first goes back to the buttons.
    const bool shots = !psfive_shot_full_urls_.empty(), similar = !psfive_similar_games_.empty();
    int next = psfive_details_;
    if (amount > 0) {
        if (next <= 1 && similar && (next == 1 || !shots)) next = 2;
        else if (next == 0 && shots) next = 1;
    } else {
        if (next == 2 && shots) next = 1;
        else { psfiveCloseDetails(true); audio_.navigate(); return; }
    }
    if (next == psfive_details_) { navigationBoundary(); return; }
    psfive_details_ = next;
    audio_.navigate();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::psfiveDetailsHorizontal(int amount) {
    if (!amount) return;
    if (psfive_details_ == 1 || psfive_shot_view_) {
        const int count = static_cast<int>(psfive_shot_full_urls_.size());
        const int next = std::clamp(static_cast<int>(psfive_shot_) + (amount < 0 ? -1 : 1), 0, std::max(0, count - 1));
        if (next == static_cast<int>(psfive_shot_)) { navigationBoundary(); return; }
        psfive_shot_ = static_cast<size_t>(next);
        if (psfive_shot_view_) psfiveViewShot();
    } else if (psfive_details_ == 2) {
        const int count = static_cast<int>(psfive_similar_games_.size());
        const int next = std::clamp(static_cast<int>(psfive_similar_) + (amount < 0 ? -1 : 1), 0, std::max(0, count - 1));
        if (next == static_cast<int>(psfive_similar_)) { navigationBoundary(); return; }
        psfive_similar_ = static_cast<size_t>(next);
    } else { navigationBoundary(); return; }
    audio_.navigate();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::psfiveDetailsAccept() {
    if (psfive_shot_view_) { psfive_shot_view_ = false; audio_.context(); InvalidateRect(hwnd_, nullptr, FALSE); return; }
    if (psfive_details_ == 1) { psfiveViewShot(); return; }
    if (psfive_details_ == 2 && psfive_similar_ < psfive_similar_games_.size()) {
        const auto game = psfive_similar_games_[psfive_similar_];
        psfiveCloseDetails(false);
        openDetails(game);
        return;
    }
    if (selected_ < cards_.size()) { // The information: the game's full Szczegóły.
        const auto game = cards_[selected_].game;
        psfiveCloseDetails(false);
        openDetails(game);
    }
}

// A click on a screenshot or a similar game (the sheet's strips); true when it took the click.
bool Launcher::psfiveDetailsClick(float x, float y) {
    if (!psfiveDetailsShown()) return false;
    if (psfive_shot_view_) { psfive_shot_view_ = false; InvalidateRect(hwnd_, nullptr, FALSE); return true; }
    const auto view = renderer_.viewport();
    const float top = psfive_details::sheetTop(view.height, psfive_details_progress_) - psfive_details_scroll_;
    const auto shots_first = psfive_details::firstShown(psfive_shot_, psfive_shot_full_urls_.size(), view.width, psfive_details::shot_width + psfive_details::shot_gap);
    for (size_t i = shots_first; i < psfive_shot_full_urls_.size(); ++i)
        if (psfive_details::shot(i, shots_first, top).contains(x, y)) {
            const bool again = psfive_details_ == 1 && psfive_shot_ == i;
            psfive_details_ = 1; psfive_shot_ = i;
            if (again) psfiveViewShot(); else { audio_.navigate(); InvalidateRect(hwnd_, nullptr, FALSE); }
            return true;
        }
    const auto similar_first = psfive_details::firstShown(psfive_similar_, psfive_similar_games_.size(), view.width, psfive_details::similar_size + psfive_details::similar_gap);
    for (size_t i = similar_first; i < psfive_similar_games_.size(); ++i)
        if (psfive_details::similar(i, similar_first, top).contains(x, y)) {
            const bool again = psfive_details_ == 2 && psfive_similar_ == i;
            psfive_details_ = 2; psfive_similar_ = i;
            if (again) psfiveDetailsAccept(); else { audio_.navigate(); InvalidateRect(hwnd_, nullptr, FALSE); }
            return true;
        }
    return y >= psfive_details::sheetTop(view.height, psfive_details_progress_);
}

void Launcher::tickPsfiveDetails(float dt, bool& needs_paint) {
    const bool shown = psfiveDetailsShown();
    const float target = shown ? 1.0f : 0.0f;
    const float ease = 1.0f - std::exp(-dt * 11.0f);
    if (std::abs(psfive_details_progress_ - target) > 0.001f) {
        psfive_details_progress_ += (target - psfive_details_progress_) * ease;
        if (std::abs(psfive_details_progress_ - target) <= 0.001f) psfive_details_progress_ = target;
        needs_paint = true;
    }
    psfive_details_scroll_ = 0; // Everything fits on the screen (psfive_details_layout.hpp).
    if (!shown) { psfive_focus_glide_ = false; return; }
    // The focus frame glides between the screenshots and the similar games, as the tiles' outline does.
    const auto view = renderer_.viewport();
    const float top = psfive_details::sheetTop(view.height, psfive_details_progress_);
    std::optional<settings_ui::Rect> focus;
    if (psfive_details_ == 1 && psfive_shot_ < psfive_shot_full_urls_.size()) {
        const auto first = psfive_details::firstShown(psfive_shot_, psfive_shot_full_urls_.size(), view.width, psfive_details::shot_width + psfive_details::shot_gap);
        focus = psfive_details::shot(psfive_shot_, first, top);
    } else if (psfive_details_ == 2 && psfive_similar_ < psfive_similar_games_.size()) {
        const auto first = psfive_details::firstShown(psfive_similar_, psfive_similar_games_.size(), view.width, psfive_details::similar_size + psfive_details::similar_gap);
        focus = psfive_details::similar(psfive_similar_, first, top);
    }
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
    // The similar games' covers from the artwork cache, asked for once when missing.
    for (size_t i = 0; i < psfive_similar_games_.size() && i < psfive_similar_bound_.size(); ++i) {
        if (psfive_similar_bound_[i] && renderer_.hasPsfiveSimilar(i)) continue;
        const auto& game = psfive_similar_games_[i];
        const auto* art = art_.find(gameKey(game));
        if (art && art->cover) { renderer_.setPsfiveSimilar(i, art->cover); psfive_similar_bound_[i] = true; needs_paint = true; }
        else if (!psfive_similar_bound_[i]) { requestArt(game, CoverMessage::Kind::cover); psfive_similar_bound_[i] = true; }
    }
}
