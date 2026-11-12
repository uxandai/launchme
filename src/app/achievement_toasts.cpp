// Achievements unlocked while a DRM-free game runs: its local saves (local_achievements.hpp) are
// looked at every few seconds on a worker; the unlocks that are new since the game started slide in,
// one after another, as a card at the bottom right of the game's screen (achievement_toast_view.hpp,
// achievement_toast_renderer.cpp). The card's window is passive: no focus, clicks go through.
// `launchme.exe --jump=achievement-test[:name]` shows a test card over whatever is in front.
#include "launcher.hpp"
#include "../library/local_achievements.hpp"
#include "../platform/file_io.hpp"
#include "../platform/text_encoding.hpp"
#include "../ui/achievement_toast_view.hpp"
#include "../ui/overlay_window.hpp"
#include <algorithm>
#include <system_error>

namespace {
constexpr uint64_t watch_ms = 2000;          // How often a running game's saves are looked at.
constexpr uint64_t search_ms = 10000;        // The folder walked again while no saves were found…
constexpr uint64_t search_none_ms = 60000;   // …and while it shows no local achievements at all.
constexpr UINT_PTR toast_timer = 1;
constexpr size_t icon_limit = 2 * 1024 * 1024;

std::filesystem::file_time_type writtenAt(const std::filesystem::path& file) {
    std::error_code error;
    const auto time = std::filesystem::last_write_time(file, error);
    return error ? std::filesystem::file_time_type{} : time;
}

// The earned achievement's picture: a file on this computer, or the achievements screen's cached
// download of Steam's icon (achievements\<game cache key>\<file>).
std::shared_ptr<const std::vector<uint8_t>> toastIcon(const std::wstring& icon, const std::filesystem::path& cache) {
    if (icon.empty()) return {};
    std::vector<uint8_t> bytes;
    if (!icon.starts_with(L"https://")) bytes = file_io::readBytes(icon, icon_limit);
    else if (const auto name = achievementIconFile(icon); !name.empty() && !cache.empty()) bytes = file_io::readBytes(cache / name, icon_limit);
    if (bytes.empty()) return {};
    return std::make_shared<const std::vector<uint8_t>>(std::move(bytes));
}

// Frosted glass over a game: the layered card has nothing behind it to blur, so the screen under
// it is copied (GDI), shrunk by `shrink` and box-blurred three times (close to a Gaussian); drawn
// stretched back, the shrink adds its own softness. Small: a few hundred thousand pixels at most.
constexpr int shrink = 4, blur_radius = 3;
constexpr uint64_t capture_ms = 50; // At most this often while the card is shown.

struct Frost {
    std::vector<uint8_t> pixels; // BGRA, opaque.
    int width = 0, height = 0;
};

void boxBlur(std::vector<uint8_t>& image, int width, int height, int radius, bool horizontal) {
    std::vector<uint8_t> line(static_cast<size_t>(std::max(width, height)) * 4);
    const int count = horizontal ? height : width, length = horizontal ? width : height;
    for (int row = 0; row < count; ++row) {
        const auto at = [&](int i) -> uint8_t* {
            i = std::clamp(i, 0, length - 1);
            const int x = horizontal ? i : row, y = horizontal ? row : i;
            return image.data() + (static_cast<size_t>(y) * width + x) * 4;
        };
        int sum[3]{};
        for (int i = -radius; i <= radius; ++i) for (int c = 0; c < 3; ++c) sum[c] += at(i)[c];
        const int span = radius * 2 + 1;
        for (int i = 0; i < length; ++i) {
            for (int c = 0; c < 3; ++c) line[static_cast<size_t>(i) * 4 + c] = static_cast<uint8_t>(sum[c] / span);
            for (int c = 0; c < 3; ++c) sum[c] += at(i + radius + 1)[c] - at(i - radius)[c];
        }
        for (int i = 0; i < length; ++i) {
            auto* pixel = at(i);
            for (int c = 0; c < 3; ++c) pixel[c] = line[static_cast<size_t>(i) * 4 + c];
            pixel[3] = 255;
        }
    }
}

Frost frostScreen(const RECT& area) {
    Frost frost;
    const int width = area.right - area.left, height = area.bottom - area.top;
    if (width <= 0 || height <= 0) return frost;
    const HDC screen = GetDC(nullptr);
    if (!screen) return frost;
    const HDC memory = CreateCompatibleDC(screen);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = width; info.bmiHeader.biHeight = -height; // Top-down.
    info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32; info.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    const HBITMAP bitmap = memory ? CreateDIBSection(screen, &info, DIB_RGB_COLORS, &bits, nullptr, 0) : nullptr;
    bool copied = false;
    if (bitmap) {
        const HGDIOBJ previous = SelectObject(memory, bitmap);
        copied = BitBlt(memory, 0, 0, width, height, screen, area.left, area.top, SRCCOPY) != FALSE;
        GdiFlush();
        if (copied) {
            frost.width = std::max(1, width / shrink); frost.height = std::max(1, height / shrink);
            frost.pixels.assign(static_cast<size_t>(frost.width) * frost.height * 4, 255);
            const auto* source = static_cast<const uint8_t*>(bits);
            for (int y = 0; y < frost.height; ++y)
                for (int x = 0; x < frost.width; ++x) {
                    int sum[3]{}, samples = 0;
                    for (int dy = 0; dy < shrink; ++dy)
                        for (int dx = 0; dx < shrink; ++dx) {
                            const int sx = x * shrink + dx, sy = y * shrink + dy;
                            if (sx >= width || sy >= height) continue;
                            const auto* pixel = source + (static_cast<size_t>(sy) * width + sx) * 4;
                            for (int c = 0; c < 3; ++c) sum[c] += pixel[c];
                            ++samples;
                        }
                    auto* out = frost.pixels.data() + (static_cast<size_t>(y) * frost.width + x) * 4;
                    for (int c = 0; c < 3; ++c) out[c] = static_cast<uint8_t>(sum[c] / std::max(1, samples));
                }
            for (int pass = 0; pass < 3; ++pass) {
                boxBlur(frost.pixels, frost.width, frost.height, blur_radius, true);
                boxBlur(frost.pixels, frost.width, frost.height, blur_radius, false);
            }
        }
        SelectObject(memory, previous);
        DeleteObject(bitmap);
    }
    if (memory) DeleteDC(memory);
    ReleaseDC(nullptr, screen);
    if (!copied) frost = {};
    return frost;
}
} // namespace

void Launcher::captureToastBackdrop() {
    auto frost = frostScreen(toast_backdrop_rect_);
    toast_captured_at_ = GetTickCount64();
    if (frost.pixels.empty()) { toast_backdrop_.reset(); return; }
    toast_backdrop_width_ = static_cast<uint32_t>(frost.width);
    toast_backdrop_height_ = static_cast<uint32_t>(frost.height);
    toast_backdrop_ = std::make_shared<const std::vector<uint8_t>>(std::move(frost.pixels));
}

void Launcher::watchLocalAchievements() {
    if (!runtime_started_) return;
    std::vector<std::pair<std::string, Game>> running;
    {
        std::lock_guard lock(game_activity_jobs_->mutex);
        for (const auto& [key, tracked] : game_activity_jobs_->active) {
            const auto session = game_sessions_.find(key);
            if (session != game_sessions_.end() && session->second.running()) running.emplace_back(key, tracked.game);
        }
    }
    std::erase_if(achievement_watch_, [&](const auto& entry) {
        return std::none_of(running.begin(), running.end(), [&](const auto& game) { return game.first == entry.first; });
    });
    const auto now = GetTickCount64();
    for (const auto& [key, game] : running) {
        auto [found, added] = achievement_watch_.try_emplace(key);
        auto& watch = found->second;
        if (added) {
            watch.folder = localAchievementsFolder(game);
            watch.generation = ++achievement_watch_generation_;
        }
        if (watch.folder.empty() || watch.pending || now < watch.next_at) continue;
        watch.pending = true;
        watch.next_at = now + watch_ms;
        // Walked again while the saves are unknown (the game writes its first unlock later).
        const bool search = watch.state.empty() &&
                            (!watch.searched_at || now - watch.searched_at >= (watch.app_id ? search_ms : search_none_ms));
        if (watch.state.empty() && !search) { watch.pending = false; continue; }
        const auto alive = alive_;
        const auto window = hwnd_;
        const bool english = i18n::english();
        const auto cache = userConfigDirectory() / L"achievements" / text_encoding::wideLenient(gameCacheKey(game));
        work_.start([=, folder = watch.folder, state = watch.state, written = watch.written, unlocked = watch.unlocked,
                     baseline = watch.baseline, generation = watch.generation] {
            namespace saves = local_achievements;
            AchievementWatchAnswer answer;
            answer.key = key; answer.generation = generation;
            answer.state = state; answer.written = written;
            // Unchanged saves: nothing read.
            if (!search && !state.empty() && writtenAt(state) == written) {
                // Answered below.
            } else {
                const auto found = saves::find(folder, saves::systemPlaces());
                answer.searched = true;
                answer.state = found.state; answer.app_id = found.app_id;
                if (found && !found.state.empty()) {
                    answer.changed = true;
                    answer.written = writtenAt(found.state);
                    const auto local = saves::localList(found, findSteamRoot(), english);
                    for (const auto& item : local.list.items) {
                        if (!item.achieved) continue;
                        answer.unlocked.insert(item.id);
                        if (baseline && !unlocked.contains(item.id))
                            answer.toasts.push_back({item.name, item.description, toastIcon(item.icon, cache)});
                    }
                }
            }
            if (!alive->load()) return;
            auto message = std::make_unique<RemoteMessage>();
            message->run = [answer = std::move(answer)](Launcher& app) mutable { app.achievementWatchAnswered(std::move(answer)); };
            if (PostMessageW(window, remote_message, 0, reinterpret_cast<LPARAM>(message.get()))) message.release();
        });
    }
}

void Launcher::achievementWatchAnswered(AchievementWatchAnswer answer) {
    const auto found = achievement_watch_.find(answer.key);
    if (found == achievement_watch_.end() || found->second.generation != answer.generation) return;
    auto& watch = found->second;
    watch.pending = false;
    if (answer.searched) {
        watch.searched_at = GetTickCount64();
        watch.state = answer.state;
        watch.app_id = answer.app_id;
    }
    if (!answer.changed) {
        // No saves yet when the game started: every unlock from now on is new.
        if (!watch.baseline && answer.searched && answer.state.empty()) watch.baseline = true;
        return;
    }
    watch.written = answer.written;
    watch.unlocked = std::move(answer.unlocked);
    if (!watch.baseline) { watch.baseline = true; return; } // What was earned before this session.
    if (answer.toasts.empty()) return;
    // The achievements screen and the Details card read the saves again when next shown.
    if (const auto cached = local_achievements_.find(answer.key); cached != local_achievements_.end() && !cached->second.pending)
        cached->second.checked_at = 0;
    for (auto& toast : answer.toasts) queueAchievementToast(std::move(toast));
}

void Launcher::queueAchievementToast(AchievementToast toast) {
    achievement_toasts_.push_back(std::move(toast));
    if (!toast_visible_) showNextAchievementToast();
}

void Launcher::showNextAchievementToast() {
    using namespace achievement_toast;
    if (achievement_toasts_.empty()) return;
    if (!toast_window_) {
        auto window = std::make_unique<OverlayWindow>();
        if (!window->create(true, [this](HWND w, UINT m, WPARAM p, LPARAM l) { return toastMessage(w, m, p, l); })) {
            achievement_toasts_.clear();
            return;
        }
        toast_window_ = std::move(window);
        toast_window_->appearance(settings_);
        // Kept out of screen copies (Windows 10 2004+): the glass may copy what is under it while shown.
        toast_live_backdrop_ = SetWindowDisplayAffinity(toast_window_->handle(), 0x11 /* WDA_EXCLUDEFROMCAPTURE */) != FALSE;
    }
    toast_current_ = std::move(achievement_toasts_.front());
    achievement_toasts_.pop_front();
    // The game's screen (the one in front when no game is known), at its bottom right.
    HWND over = overlay_snapshot_.target ? overlay_snapshot_.target->window : nullptr;
    if (!over || !IsWindow(over)) over = GetForegroundWindow();
    MONITORINFO monitor{sizeof(monitor)};
    GetMonitorInfoW(MonitorFromWindow(over, MONITOR_DEFAULTTOPRIMARY), &monitor);
    const RECT screen = monitor.rcMonitor;
    const float height = static_cast<float>(screen.bottom - screen.top);
    toast_scale_ = std::clamp(height / 1080.0f, 1.0f, 3.0f);
    const int width_px = static_cast<int>((card_width + margin) * toast_scale_ + 0.5f);
    const int height_px = static_cast<int>((card_height + margin) * toast_scale_ + 0.5f);
    const RECT rect{screen.right - width_px, screen.bottom - height_px, screen.right, screen.bottom};
    ++toast_id_;
    // The glass: the screen under the window and a blur's reach around it, before the card comes.
    const int reach = blur_radius * shrink * 3;
    toast_rect_ = rect;
    toast_backdrop_rect_ = {std::max(screen.left, rect.left - reach), std::max(screen.top, rect.top - reach),
                            std::min(screen.right, rect.right + reach), std::min(screen.bottom, rect.bottom + reach)};
    captureToastBackdrop();
    toast_shown_at_ = GetTickCount64();
    toast_visible_ = toast_window_->show(rect, false);
    if (!toast_visible_) { achievement_toasts_.clear(); return; }
    SetTimer(toast_window_->handle(), toast_timer, 15, nullptr);
    audio_.achievement();
}

LRESULT Launcher::toastMessage(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    using namespace achievement_toast;
    if (message == WM_PAINT && toast_window_) {
        View view;
        view.id = toast_id_;
        view.heading = tr(L"Osiągnięcie zdobyte");
        view.name = toast_current_.name;
        view.description = toast_current_.description;
        view.icon = toast_current_.icon;
        view.scale = toast_scale_;
        view.slide = std::max(0.0f, slideAt(GetTickCount64() - toast_shown_at_));
        if (toast_backdrop_) {
            view.backdrop = toast_backdrop_;
            view.backdrop_width = toast_backdrop_width_; view.backdrop_height = toast_backdrop_height_;
            view.backdrop_left = static_cast<float>(toast_backdrop_rect_.left - toast_rect_.left);
            view.backdrop_top = static_cast<float>(toast_backdrop_rect_.top - toast_rect_.top);
            view.backdrop_right = static_cast<float>(toast_backdrop_rect_.right - toast_rect_.left);
            view.backdrop_bottom = static_cast<float>(toast_backdrop_rect_.bottom - toast_rect_.top);
        }
        toast_window_->renderer().paintAchievementToast(view);
        return 0;
    }
    if (message == WM_TIMER && wparam == toast_timer) {
        const uint64_t elapsed = GetTickCount64() - toast_shown_at_;
        if (slideAt(elapsed) < 0) {
            KillTimer(window, toast_timer);
            toast_window_->hide();
            toast_visible_ = false;
            showNextAchievementToast();
        } else {
            // The game moves under the glass: copied again now and then (only when the card's own
            // window stays out of the copy).
            const bool fresh = toast_live_backdrop_ && GetTickCount64() - toast_captured_at_ >= capture_ms;
            if (fresh) captureToastBackdrop();
            if (fresh || elapsed < slide_in_ms + 40 || elapsed > slide_in_ms + shown_ms) toast_window_->invalidate();
        }
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

// --jump=achievement-test[:name]: a card as a game's unlock would show, with an icon from a local
// achievement already read when there is one, so it can be tried over a game.
void Launcher::testAchievementToast(const std::wstring& command) {
    AchievementToast toast;
    const auto colon = command.find(L':');
    toast.name = colon == std::wstring::npos || colon + 1 >= command.size() ? tr(L"Osiągnięcie testowe") : command.substr(colon + 1);
    toast.description = tr(L"Tak wygląda powiadomienie o osiągnięciu w grze.");
    for (const auto& [key, cached] : local_achievements_) {
        for (const auto& item : cached.list.items)
            if (!item.icon.empty() && !item.icon.starts_with(L"https://"))
                if ((toast.icon = toastIcon(item.icon, {}))) break;
        if (toast.icon) break;
    }
    queueAchievementToast(std::move(toast));
}
