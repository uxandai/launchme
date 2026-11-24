#include "launcher.hpp"
#include "../artwork/background_focus.hpp"
#include "../artwork/cover_cache.hpp"
#include "../artwork/esde_art.hpp"
#include "../net/http.hpp"

namespace {
// The shape of the screen the background fills: the monitor's (Tryb TV fills it), 16:9 without one.
float screenAspect(HWND window) {
    MONITORINFO info{sizeof(info)};
    if (window && GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &info)) {
        const float w = static_cast<float>(info.rcMonitor.right - info.rcMonitor.left), h = static_cast<float>(info.rcMonitor.bottom - info.rcMonitor.top);
        if (w > 0 && h > 0) return std::clamp(w / h, 1.0f, 4.0f);
    }
    return 16.0f / 9.0f;
}
}

crop_ui::Layout Launcher::cropLayout() const {
    const auto view = renderer_.viewport();
    return {view.width, view.height, desktopEmbedded(), crop_state_.background ? crop_state_.aspect : 1.0f};
}

void Launcher::cropShowFocus() {
    if (crop_state_.background) renderer_.setHeroFocus(0, crop_state_.focus);
    else renderer_.setCoverFocus(0, crop_state_.focus);
}

void Launcher::openCropEditor(const Game& game, bool background) {
    if (!isGame(game) || pending_launch_) return;
    desktopLeaveLibrary(); rememberPlace();
    crop_state_ = {}; crop_state_.game = game;
    crop_state_.background = background;
    crop_state_.aspect = background ? screenAspect(hwnd_) : 1.0f;
    crop_background_.reset();
    crop_return_ = screen_; crop_return_selected_ = selected_; crop_return_scroll_ = grid_scroll_;
    crop_games_ = {game};
    suspendDetails();
    dismissMenu(); menu_progress_ = 0; closeFriends(); closeCategoryMenu(); closeQuick();
    screen_ = ScreenMode::crop; rebuildCards(false);
    const auto token = crop_token_; const auto generation = token->fetch_add(1) + 1;
    if (!runtime_started_) return; // UI tests inject a result; no network in the test executor.
    work_.start([window = hwnd_, alive = alive_, token, generation, game = crop_state_.game, rom_first = jobs_->rom_art.load(), background] {
        HttpBudget budget(30000);
        auto message = std::make_unique<CropMessage>(); message->generation = generation;
        const auto cache = coverCacheDirectory();
        if (background) {
            // The background as the screens show it (downloaded now when missing); centred by default.
            message->image = cachedArtworkFile(game, ArtworkKind::hero, cache, rom_first);
            if (!message->image.empty()) message->bytes = cachedArtwork(game, ArtworkKind::hero, cache, rom_first);
            else {
                message->bytes = artworkForGame(game, ArtworkKind::hero, cache, findSteamRoot());
                message->image = cachedArtworkFile(game, ArtworkKind::hero, cache, rom_first);
            }
            const auto [width, height] = message->image.empty() ? std::pair{0, 0} : cropImageSize(message->image);
            message->width = width; message->height = height;
            if (!width || !height || message->bytes.empty()) message->error = tr(L"Tło nie jest dostępne.");
            else message->focus = backgroundFocus(game, message->bytes, cache).value_or(message->automatic);
            if (alive->load() && token->load() == generation && PostMessageW(window, crop_message, 0, reinterpret_cast<LPARAM>(message.get()))) message.release();
            return;
        }
        message->image = cachedArtworkFile(game, ArtworkKind::cover, cache, rom_first);
        if (!message->image.empty()) message->bytes = cachedArtwork(game, ArtworkKind::cover, cache, rom_first);
        else {
            message->image = gameCoverPath(game, cache);
            if (game.kind == GameKind::nonsteam && rom_first) {
                message->bytes = esdeArtForShortcut(game.app_id, game.platform, game.name, EmuArtKind::cover, cache.parent_path() / L"emulation", game.rom);
                if (!message->bytes.empty()) message->image = esdeArtCachePath(game.app_id, EmuArtKind::cover, cache.parent_path() / L"emulation");
            }
            if (message->bytes.empty()) message->bytes = artworkForGame(game, ArtworkKind::cover, cache, findSteamRoot());
            if (message->bytes.empty() && game.kind == GameKind::nonsteam && !rom_first) {
                message->bytes = esdeArtForShortcut(game.app_id, game.platform, game.name, EmuArtKind::cover, cache.parent_path() / L"emulation", game.rom);
                if (!message->bytes.empty()) message->image = esdeArtCachePath(game.app_id, EmuArtKind::cover, cache.parent_path() / L"emulation");
            }
        }
        const auto [width, height] = cropImageSize(message->image);
        message->width = width; message->height = height;
        if (!width || !height || message->bytes.empty()) message->error = tr(L"Okładka nie jest dostępna.");
        else {
            auto metadata = message->image; metadata.replace_extension(L".focus");
            message->automatic = cachedAutomaticCrop(message->image, metadata, game.name);
            message->focus = manualTextCrop(message->image).value_or(message->automatic);
        }
        if (alive->load() && token->load() == generation && PostMessageW(window, crop_message, 0, reinterpret_cast<LPARAM>(message.get()))) message.release();
    });
}
void Launcher::onCrop(std::unique_ptr<CropMessage> message) {
    if (!cropOpen() || message->generation != crop_token_->load()) return;
    crop_state_.loading = false; crop_state_.image = message->image;
    crop_state_.width = message->width; crop_state_.height = message->height;
    crop_state_.focus = message->focus; crop_state_.automatic = message->automatic; crop_state_.error = message->error;
    crop_state_.clamp();
    if (message->error.empty()) {
        const auto key = gameKey(crop_state_.game);
        auto& art = art_.at(key);
        if (crop_state_.background) {
            art.hero = crop_background_ = shareImage(std::move(message->bytes)); art.hero_tone = HeroTone::full; art.hero_focus = crop_state_.focus;
            art_.resize(key);
            renderer_.setHero(0, art.hero, HeroTone::full, true, crop_state_.focus);
        } else {
            art.cover = shareImage(std::move(message->bytes)); art.focus = message->focus;
            art_.resize(key);
            renderer_.setCover(0, art.cover, message->focus);
            cards_[0].cover_opacity = 1;
        }
        trimArt();
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}
void Launcher::cropMove(int horizontal, int vertical) {
    auto& control = crop_state_.control;
    constexpr int back = crop_ui::State::back;
    // Back (top left): Down or Right returns to the picture; while loading or failed it is the only target.
    if (control == back) {
        if ((vertical > 0 || horizontal > 0) && crop_state_.error.empty() && !crop_state_.loading) { control = -1; audio_.navigate(); }
        else navigationBoundary();
        InvalidateRect(hwnd_, nullptr, FALSE); return;
    }
    if (crop_state_.loading || !crop_state_.error.empty()) {
        if (vertical < 0 || horizontal < 0) { control = back; audio_.navigate(); InvalidateRect(hwnd_, nullptr, FALSE); }
        return;
    }
    if (control >= 0) {
        if (horizontal < 0) control = -1;
        else if (vertical < 0 && control == 0) { if (!desktopEmbedded()) control = back; } // Up from "Automatyczny" (a desktop sheet has its ✕ instead).
        else control = std::clamp(control + vertical, 0, 1);
    } else {
        // Right goes to the buttons when the window cannot move sideways (a tall cover, a narrow background).
        if (horizontal > 0 && crop_state_.window().first >= crop_state_.width - 0.5f) crop_state_.control = 0;
        else { crop_state_.move(horizontal, vertical); cropShowFocus(); }
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}
void Launcher::cropTab(int amount) {
    // Back → picture → Automatyczny → Zapisz kadr, round (in a desktop sheet without Back: its ✕ closes).
    crop_state_.control = (crop_state_.control + 2 + amount + 4) % 4 - 2;
    if (desktopEmbedded() && crop_state_.control == crop_ui::State::back) crop_state_.control = (crop_state_.control + 2 + amount + 4) % 4 - 2;
    InvalidateRect(hwnd_, nullptr, FALSE);
}
void Launcher::cropReset() {
    if (crop_state_.loading) return;
    crop_state_.focus = crop_state_.automatic; crop_state_.automatic_selected = true;
    crop_state_.clamp();
    cropShowFocus(); InvalidateRect(hwnd_, nullptr, FALSE);
}
void Launcher::cropAccept() {
    if (crop_state_.control == crop_ui::State::back) { closeCropEditor(); return; }
    if (crop_state_.loading || crop_state_.image.empty() || !crop_state_.error.empty()) return;
    if (crop_state_.control == 0) { cropReset(); return; }
    const auto key = gameKey(crop_state_.game);
    if (crop_state_.background) {
        const auto cache = coverCacheDirectory();
        const bool saved = crop_state_.automatic_selected ? clearBackgroundFocus(crop_state_.game, cache) :
            crop_background_ && saveBackgroundFocus(crop_state_.game, *crop_background_, crop_state_.focus, cache);
        if (!saved) { showNotification(NotificationKind::error, tr(L"Nie udało się zapisać kadru"), L""); return; }
        art_.at(key).hero_focus = crop_state_.focus; // The screens bind it from art_ (no new decode).
        closeCropEditor();
        return;
    }
    const bool saved = crop_state_.automatic_selected ? clearManualTextCrop(crop_state_.image) : saveManualTextCrop(crop_state_.image, crop_state_.focus);
    if (!saved) { showNotification(NotificationKind::error, tr(L"Nie udało się zapisać kadru"), L""); return; }
    invalidateArtwork(crop_state_.game, CoverMessage::Kind::cover);
    auto& art = art_.at(key);
    art.focus = crop_state_.focus;
    desktopRecentArtArrived(key, CoverMessage::Kind::cover, art.cover, art.focus); // Ostatnio grane's thumbnail too.
    closeCropEditor();
}
void Launcher::closeCropEditor() {
    pointerUp(); crop_token_->fetch_add(1);
    crop_background_.reset();
    screen_ = crop_return_; grid_scroll_ = crop_return_scroll_;
    rebuildCards(false);
    if (screen_ == ScreenMode::artwork) { InvalidateRect(hwnd_, nullptr, FALSE); return; } // Back on "Popraw kadr" in "Zmień grafikę".
    if (!cards_.empty()) navigate(static_cast<int>(std::min(crop_return_selected_, cards_.size() - 1)) - static_cast<int>(selected_), false);
    if (screen_ == ScreenMode::details) requestDetails();
}
void Launcher::cropClick(float x, float y) {
    const auto layout = cropLayout();
    if (layout.back().contains(x, y)) { closeCropEditor(); return; }
    if (crop_state_.loading || !crop_state_.error.empty()) return;
    if (layout.preview().contains(x, y)) {
        crop_state_.control = -1; crop_state_.dragging = crop_state_.adjustable();
        crop_state_.drag_x = x; crop_state_.drag_y = y; crop_state_.drag_focus = crop_state_.focus;
        if (crop_state_.dragging) SetCapture(hwnd_);
    } else for (int i = 0; i < 2; ++i) if (layout.button(i).contains(x, y)) { crop_state_.control = i; cropAccept(); break; }
    InvalidateRect(hwnd_, nullptr, FALSE);
}
void Launcher::cropDrag(float x, float y) {
    if (!crop_state_.dragging) return;
    const auto layout = cropLayout();
    // The picture follows the pointer: one preview pixel is window / preview picture pixels.
    const auto [window_width, window_height] = crop_state_.window();
    crop_state_.focus.x = crop_state_.drag_focus.x - (x - crop_state_.drag_x) * window_width / (layout.previewWidth() * crop_state_.width);
    crop_state_.focus.y = crop_state_.drag_focus.y - (y - crop_state_.drag_y) * window_height / (layout.size() * crop_state_.height);
    crop_state_.clamp(); crop_state_.automatic_selected = false;
    cropShowFocus(); InvalidateRect(hwnd_, nullptr, FALSE);
}
