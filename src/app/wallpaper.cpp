// Interfejs → Tapeta: a picture or a looping video behind the screens that had a plain or black
// backdrop (snitch, Settings and the menu screens, "Zmień grafikę", home tiles without a picture
// such as XMB's categories). Chosen online in the wallpaper picker (artwork_browser.cpp in its
// wallpaper mode, sources in src/artwork/wallpaper_sources.hpp) or from disk; without one the
// launcher's own animated backdrop (orbiting lights) stays where it was. A video plays muted and
// only while the launcher is in front; the picker plays the focused animated one's preview.
#include "launcher.hpp"
#include "../platform/file_io.hpp"

#include <algorithm>

namespace {
// What a wallpaper video is decoded to (enough for a backdrop that is mostly dimmed or blurred).
constexpr uint32_t wallpaper_video_width = 1600;
}

std::wstring Launcher::wallpaperLabel() const {
    switch (settings_.wallpaper) {
    case AppSettings::WallpaperKind::picture: return tr(L"Zdjęcie");
    case AppSettings::WallpaperKind::video: return tr(L"Animowana");
    default: return tr(L"Brak");
    }
}

std::wstring Launcher::wallpaperDescription() const {
    return tr(L"Zdjęcia z Wallhaven i Danbooru (tylko bezpieczne treści) albo animowane tła z MotionBGs, profili Steam i subskrypcji Wallpaper Engine (wideo), z ruchomym podglądem przed wyborem. Także z dysku.");
}

// The wallpaper from the settings, shown again: a picture to the renderer, a video to the player.
void Launcher::applyWallpaper() {
    renderer_.setWallpaperPlaces(settings_.wallpaper_snitch, settings_.wallpaper_menus, settings_.wallpaper_artwork, settings_.wallpaper_tiles);
    if (!wallpaper_preview_.empty()) return; // The picker's preview plays meanwhile; closing it calls this again.
    if (settings_.wallpaper == AppSettings::WallpaperKind::picture && file_io::isFile(settings_.wallpaper_file)) {
        stopWallpaperVideo();
        auto bytes = file_io::readBytes(settings_.wallpaper_file, 40 * 1024 * 1024);
        renderer_.setWallpaperImage(bytes.empty() ? ImageBytes{} : shareImage(std::move(bytes)));
        return;
    }
    renderer_.setWallpaperImage({});
    if (settings_.wallpaper == AppSettings::WallpaperKind::video && file_io::isFile(settings_.wallpaper_file)) {
        if (wallpaper_playing_ != settings_.wallpaper_file) stopWallpaperVideo(); // Opened by tickWallpaper while watched.
        return;
    }
    stopWallpaperVideo();
}

void Launcher::stopWallpaperVideo() {
    if (wallpaper_player_.active()) wallpaper_player_.stop();
    wallpaper_playing_.clear();
    renderer_.clearWallpaperVideo();
}

void Launcher::showWallpaperPreview(const std::filesystem::path& file) {
    if (file.empty()) return;
    wallpaper_preview_ = file;
    stopWallpaperVideo(); // tickWallpaper opens the preview on its next frame.
    renderer_.setWallpaperImage({});
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::stopWallpaperPreview() {
    wallpaper_preview_id_ = 0;
    if (wallpaper_preview_.empty()) return;
    wallpaper_preview_.clear();
    stopWallpaperVideo();
    applyWallpaper();
}

// Every frame: the video's player opened while the launcher is seen (and let go behind a game),
// its new frames handed to the renderer.
void Launcher::tickWallpaper(bool& needs_paint) {
    const auto wanted = !wallpaper_preview_.empty() ? wallpaper_preview_ :
        settings_.wallpaper == AppSettings::WallpaperKind::video ? settings_.wallpaper_file : std::filesystem::path{};
    const bool seen = !wanted.empty() && windowWatched() && !IsIconic(hwnd_) && !background_mode_ && !screensaverActive();
    if (!seen) {
        if (wallpaper_player_.active()) stopWallpaperVideo();
        return;
    }
    if (wallpaper_playing_ != wanted) {
        stopWallpaperVideo();
        if (wallpaper_failed_ == wanted) return; // An unplayable file is not tried every frame.
        if (!wallpaper_player_.open(wanted, 0.0)) { wallpaper_failed_ = wanted; return; }
        wallpaper_playing_ = wanted;
    }
    if (wallpaper_player_.failed()) { wallpaper_failed_ = wanted; stopWallpaperVideo(); return; }
    // Behind Ustawienia the wallpaper is blurred: its frame stands still there (no decoding, no
    // repaints), and plays on from the same place afterwards. The picker's preview always plays.
    const bool still = settingsOpen() && wallpaper_preview_.empty() && renderer_.hasWallpaperVideo();
    wallpaper_player_.setPaused(still);
    if (still) return;
    // Frames only while the last frame drew it (or none has arrived yet): hidden under a game's
    // background it costs no copies and no repaints.
    if (!renderer_.wallpaperVisible() && renderer_.hasWallpaperVideo()) return;
    uint32_t width = 0, height = 0;
    if (wallpaper_player_.frame(wallpaper_frame_, width, height, wallpaper_video_width)) {
        renderer_.setWallpaperFrame(wallpaper_frame_.data(), width, height);
        needs_paint = true;
    }
}
