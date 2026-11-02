// System → Wydajność → Tryb niskiego zużycia RAM (AppSettings::low_memory): the renderer's artwork
// policy, the artwork cache's budget and the browser window follow the setting; behind a game the
// launcher gives its graphics and artwork back and builds them again when it is in front once more.
// Behind a game, whatever the setting, it also yields its CPU and memory priority (pollBackgroundMode).
#include "launcher.hpp"
#include "../platform/memory_usage.hpp"

#include <sstream>

void Launcher::applyLowMemory() {
    renderer_.setLowMemory(settings_.low_memory, settings_.texture_compression);
    art_.setBudget(settings_.low_memory ? low_memory_art_budget : art_budget);
    trimArt();
    if (browser_) browser_->setLowMemory(settings_.low_memory);
    if (!settings_.low_memory && memory_released_) restoreMemory();
}

// Minimized, or a game session runs and another window covering the launcher's monitor is in
// front (a game, or its client in full screen). The Guide drawer over a game keeps the launcher's
// graphics; a window beside the launcher (a windowed game, the desktop window side by side) too.
bool Launcher::launcherHidden() const {
    if (!hwnd_ || closing_) return false;
    if (IsIconic(hwnd_) || in_tray_) return true;
    if (!anyActiveSession() || gameOverlayOpen()) return false;
    const HWND front = foreground_window_ ? foreground_window_() : GetForegroundWindow();
    if (!front || front == hwnd_ || (browser_ && browser_->owns(front))) return false;
    const HMONITOR monitor = MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST);
    if (MonitorFromWindow(front, MONITOR_DEFAULTTONEAREST) != monitor) return false;
    MONITORINFO info{sizeof(info)};
    RECT covered{};
    if (!GetMonitorInfoW(monitor, &info) || !GetWindowRect(front, &covered)) return false;
    return covered.left <= info.rcMonitor.left && covered.top <= info.rcMonitor.top &&
           covered.right >= info.rcMonitor.right && covered.bottom >= info.rcMonitor.bottom;
}

void Launcher::pollMemory() {
    const bool behind = launcherHidden();
    // Guide over games: the launcher is a Guide target at any moment, so it keeps its priority and
    // frame rate (the video may stop, nothing else).
    pollBackgroundMode(behind && anyActiveSession() && !guideOverlayOn());
    const bool hidden = settings_.low_memory && behind;
    if (!hidden) {
        memory_hidden_since_ = 0;
        if (memory_released_) restoreMemory();
    } else if (!memory_released_) {
        const auto now = GetTickCount64();
        if (!memory_hidden_since_) memory_hidden_since_ = now;
        else if (now - memory_hidden_since_ >= memory_release_delay_ms) releaseMemory();
    }
    // Slots whose bytes were let go lost their bitmaps too (a new device): bound again from art_ or disk.
    if (!memory_released_ && renderer_.takeArtLost() && !cards_.empty()) { requestVisibleArt(); InvalidateRect(hwnd_, nullptr, FALSE); }
}

void Launcher::releaseMemory() {
    if (memory_released_) return;
    const auto before = currentMemoryUsage();
    memory_released_ = true;
    stopVideo();
    renderer_.releaseGraphics();
    // The artwork cache, every entry: read again from the disk when the launcher is back.
    art_.setBudget(0);
    forgetArtRequests(art_.trim([](const std::string&) { return false; }));
    art_.setBudget(settings_.low_memory ? low_memory_art_budget : art_budget);
    trimProcessMemory();
    const auto after = currentMemoryUsage();
    std::ostringstream line;
    line << "Behind a game: memory given back (private " << before.private_bytes / 1048576 << " -> " << after.private_bytes / 1048576
         << " MB, working set " << before.working_set / 1048576 << " -> " << after.working_set / 1048576 << " MB, GPU "
         << (before.gpu_local + before.gpu_shared) / 1048576 << " -> " << (after.gpu_local + after.gpu_shared) / 1048576 << " MB)";
    diagnostics::log("info", line.str());
}

void Launcher::restoreMemory() {
    if (!memory_released_) return;
    memory_released_ = false;
    memory_hidden_since_ = 0;
    renderer_.takeArtLost();
    // The next paint makes the target again; the artwork comes from the disk lane as at startup.
    if (!cards_.empty()) requestVisibleArt();
    detail_ = 0;
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
}

// A game in front of the launcher for background_delay_ms (with or without the low memory mode):
// the process yields to it (setBackgroundPriority: below-normal priority, EcoQoS, low memory
// priority), the frame clock drops to 10 Hz (frameLimit), nothing is painted, the video stops and
// the GPU's caches go back; without the low memory mode (which gives everything back) the working
// set is emptied too. The Guide overlay or the launcher in front again ends it at once.
void Launcher::pollBackgroundMode(bool behind_game) {
    if (!behind_game) {
        background_since_ = 0;
        if (background_mode_) {
            background_mode_ = false;
            setBackgroundPriority(false);
            if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
        }
        return;
    }
    if (background_mode_) return;
    const auto now = GetTickCount64();
    if (!background_since_) { background_since_ = now; return; }
    if (now - background_since_ < background_delay_ms) return;
    background_mode_ = true;
    setBackgroundPriority(true);
    stopVideo();
    renderer_.trimGraphics();
    if (!settings_.low_memory) trimProcessMemory();
    diagnostics::log("info", "Behind a game: background priority, 10 Hz frames");
}

void Launcher::releaseFarHeroes() {
    // Flix's first row shows its games' wide art (capsules, backgrounds standing in); a starting
    // game's picture covers the screen.
    const size_t capsules = flixActive() && !flix_rows_.empty() ? flix_rows_[0].count : 0;
    for (size_t i = capsules; i < cards_.size(); ++i) {
        if (i + 1 >= selected_ && i <= selected_ + 1) continue;
        if (launchAnimationActive() && gameKey(cards_[i].game) == launch_artwork_key_) continue;
        renderer_.releaseHero(i);
    }
}
