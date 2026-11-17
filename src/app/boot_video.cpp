// Interfejs → Start → Wideo startowe: full-screen startup animation in Tryb TV, chosen from
// SteamDeckRepo (src/artwork/boot_video_sources.hpp) or from disk, with optional randomization
// across <config>\boot_videos (SDH-AnimationChanger style). Any pad button, key or click skips it.
#include "launcher.hpp"
#include "../artwork/boot_video_sources.hpp"
#include "../platform/file_io.hpp"

#include <algorithm>
#include <random>

namespace {
constexpr uint32_t boot_video_max_width = 1920;
constexpr float boot_video_fade_seconds = 0.26f;

double bootPlaybackVolume(const AppSettings& settings) {
    const int idx = std::clamp(settings.video_volume, 0, static_cast<int>(settings_ui::video_volume_levels.size()) - 1);
    return idx > 0 ? settings_ui::video_volume_levels[static_cast<size_t>(idx)] : 0.5;
}
} // namespace

double Launcher::bootVideoVolume() const { return bootPlaybackVolume(settings_); }

bool Launcher::hasStoredBootVideos() const {
    return file_io::isFile(settings_.boot_video_file) ||
           !boot_video_sources::storedVideos(userConfigDirectory() / L"boot_videos").empty();
}

std::wstring Launcher::bootVideoLabel(bool compact) const {
    if (settings_.boot_video_random && hasStoredBootVideos()) return tr(L"Losowe");
    if (!settings_.boot_video_file.empty() && file_io::isFile(settings_.boot_video_file)) {
        auto label = settings_.boot_video_title.empty() ? settings_.boot_video_file.stem().wstring() : settings_.boot_video_title;
        if (compact && label.size() > 18) label = label.substr(0, 17) + L"…";
        return label;
    }
    return tr(L"Brak");
}

std::wstring Launcher::bootVideoDescription() const {
    return tr(L"Animacja z dźwiękiem przy uruchomieniu w trybie TV, z bazy Steam Deck Repo albo z dysku (WebM, MP4). Dowolny przycisk pomija.");
}

void Launcher::openBootVideoBrowser() {
    if (pending_launch_ || desktop_mode_) return;
    artwork_return_ = screen_;
    artwork_return_selected_ = selected_;
    artwork_return_scroll_ = grid_scroll_;
    artwork_state_ = {};
    artwork_pages_.clear();
    artwork_page_key_.clear();
    artwork_state_.boot_video = true;
    artwork_state_.game.name = tr(L"Wideo startowe");
    artwork_state_.category = 0;
    artwork_state_.choices = artworkChoices(artwork_state_);
    artwork_state_.provider = artwork_state_.choices.front();
    artwork_state_.gallery = true;
    wallpaper_preview_id_ = 0;
    dismissMenu();
    screen_ = ScreenMode::artwork;
    loadArtworkPage();
    audio_.context();
}

void Launcher::syncBootVideoPreview() {
    auto& state = artwork_state_;
    if (!artworkOpen() || !state.boot_video) return;
    if (!state.gallery || state.assets.empty() || state.selected >= state.assets.size()) {
        stopWallpaperPreview();
        return;
    }
    const uint32_t id = state.assets[state.selected].id;
    if (wallpaper_preview_active_id_ != 0 && wallpaper_preview_active_id_ != id) stopWallpaperPreview();
    requestArtworkThumbnails();
}

void Launcher::pickLocalBootVideo() {
    auto& state = artwork_state_;
    state.local_focus = true;
    state.gallery = state.search_focus = false;
    const auto apply = [this](const std::filesystem::path& picked) {
        auto& browser = artwork_state_;
        if (!artworkOpen() || !browser.boot_video || browser.applying || picked.empty()) return;
        browser.applying = true;
        browser.status = tr(L"Wczytywanie wideo…");
        ArtworkBrowserJob job;
        job.type = ArtworkBrowserMessage::Type::local;
        job.boot_video = true;
        job.query = picked.wstring();
        {
            std::lock_guard lock(artwork_jobs_->mutex);
            job.generation = artwork_jobs_->generation.load();
            artwork_jobs_->jobs.push_front(std::move(job));
        }
        artwork_jobs_->wake.notify_one();
        audio_.context();
        InvalidateRect(hwnd_, nullptr, FALSE);
    };
    if (pick_image_) {
        const auto picked = pick_image_(hwnd_);
        mouse_transition_until_ = GetTickCount64() + GetDoubleClickTime();
        apply(picked);
        return;
    }
    file_browser::Request request;
    request.title = tr(L"Wybierz wideo startowe");
    request.extensions = {L".webm", L".mp4", L".m4v", L".mkv", L".mov", L".wmv"};
    openFileBrowser(std::move(request), apply);
}

void Launcher::clearBootVideo() {
    settings_.boot_video_file.clear();
    settings_.boot_video_id = 0;
    settings_.boot_video_title.clear();
    settings_.boot_video_random = false;
    settingsChanged();
    audio_.context();
}

std::filesystem::path Launcher::chooseBootVideoFile() const {
    if (settings_.boot_video_random) {
        const auto all = boot_video_sources::storedVideos(userConfigDirectory() / L"boot_videos");
        if (!all.empty()) {
            std::random_device rd;
            std::uniform_int_distribution<size_t> pick(0, all.size() - 1);
            return all[pick(rd)].file;
        }
    }
    if (!settings_.boot_video_file.empty() && file_io::isFile(settings_.boot_video_file))
        return settings_.boot_video_file;
    return {};
}

void Launcher::prepareBootVideo() {
#ifdef LAUNCHME_UI_TEST
    return;
#else
    if (desktop_mode_) return;
    const auto chosen = chooseBootVideoFile();
    if (chosen.empty()) return;
    boot_video_pending_ = chosen;
    boot_video_from_preview_ = false;
    boot_video_alpha_ = 1.0f;
#endif
}

void Launcher::previewBootVideo() {
    const auto chosen = chooseBootVideoFile();
    if (chosen.empty()) return;
    if (boot_video_player_.active()) boot_video_player_.stop();
    renderer_.clearBootVideo();
    audio_.duckMusic(true);
    if (!boot_video_player_.open(chosen, bootVideoVolume(), nullptr, false)) {
        audio_.duckMusic(video_player_.active() && settings_.video_volume > 0);
        return;
    }
    boot_video_pending_.clear();
    boot_video_playing_ = true;
    boot_video_from_preview_ = true;
    boot_video_alpha_ = 1.0f;
    boot_video_started_ = std::chrono::steady_clock::now();
    last_input_ = boot_video_started_;
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::cancelBootVideo() {
    if (!bootVideoActive() && boot_video_alpha_ <= 0.0f) return;
    const bool preview = boot_video_from_preview_;
    boot_video_pending_.clear();
    boot_video_playing_ = false;
    boot_video_from_preview_ = false;
    boot_video_alpha_ = 0.0f;
    if (boot_video_player_.active()) boot_video_player_.stop();
    renderer_.clearBootVideo();
    if (preview) audio_.duckMusic(video_player_.active() && settings_.video_volume > 0);
    else audio_.skipBoot();
}

void Launcher::dismissBootVideo() {
    if (!bootVideoActive()) return;
    if (boot_video_playing_ && std::chrono::steady_clock::now() - boot_video_started_ < std::chrono::milliseconds(120)) return;
    finishBootVideo();
}

void Launcher::finishBootVideo() {
    const bool was_active = bootVideoActive();
    const bool preview = boot_video_from_preview_;
    boot_video_pending_.clear();
    boot_video_playing_ = false;
    boot_video_from_preview_ = false;
    if (boot_video_player_.active()) boot_video_player_.stop();
    if (!was_active) return;
    last_input_ = std::chrono::steady_clock::now();
    if (preview) {
        audio_.duckMusic(video_player_.active() && settings_.video_volume > 0);
    } else {
        audio_.skipBoot();
        arrived_ = 0.0f;
        entrance_lead_ = true;
        if (settings_.home_layout == AppSettings::HomeLayout::psfive) psfive_intro_ = true;
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::tickBootVideo(float dt, bool& needs_paint) {
    if (!boot_video_pending_.empty()) {
        if (closing_ || desktop_mode_ || IsIconic(hwnd_)) {
            cancelBootVideo();
            return;
        }
        if (!IsWindowVisible(hwnd_)) return;
        const auto file = std::move(boot_video_pending_);
        boot_video_pending_.clear();
        if (!boot_video_player_.open(file, bootVideoVolume(), nullptr, false)) {
            boot_video_alpha_ = 0.0f;
            audio_.skipBoot();
            needs_paint = true;
            return;
        }
        boot_video_playing_ = true;
        boot_video_started_ = std::chrono::steady_clock::now();
        last_input_ = boot_video_started_;
    }

    if (boot_video_playing_) {
        if (closing_ || IsIconic(hwnd_) || boot_video_player_.failed() || boot_video_player_.ended() ||
            std::chrono::steady_clock::now() - boot_video_started_ > std::chrono::seconds(45)) {
            finishBootVideo();
            needs_paint = true;
        } else {
            uint32_t width = 0, height = 0;
            if (boot_video_player_.frame(boot_video_frame_, width, height, boot_video_max_width))
                renderer_.setBootVideoFrame(boot_video_frame_.data(), width, height);
            needs_paint = true;
            return;
        }
    }

    if (boot_video_alpha_ > 0.0f) {
        boot_video_alpha_ = std::max(0.0f, boot_video_alpha_ - dt / boot_video_fade_seconds);
        if (boot_video_alpha_ <= 0.001f) {
            boot_video_alpha_ = 0.0f;
            renderer_.clearBootVideo();
        }
        needs_paint = true;
    }
}
