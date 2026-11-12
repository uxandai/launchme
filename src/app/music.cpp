#include "launcher.hpp"
#include "../platform/flac.hpp"
#include "../platform/music_themes.hpp"
#include "../platform/sound_cues.hpp"
#include <mfapi.h>

#include <algorithm>

namespace {
const SoundOption* soundOption(SoundCue cue, int index) {
    const auto pick = [](const auto& options, int index) -> const SoundOption* {
        return index >= 0 && index < static_cast<int>(options.size()) ? &options[static_cast<size_t>(index)] : nullptr;
    };
    switch (cue) {
    case SoundCue::navigation: return pick(ui_sounds, index);
    case SoundCue::toast: return pick(toast_sounds, index);
    case SoundCue::boot: return pick(boot_sounds, index);
    default: return nullptr;
    }
}
}

void Launcher::requestMusic() {
    audio_.cancelPendingMusic();
    std::lock_guard lock(music_jobs_->mutex);
    music_jobs_->generation.fetch_add(1);
    // A file the sound folder still holds (its full path), else the chosen built-in theme (a name
    // in assets/).
    const wchar_t* theme = music_themes[std::clamp(settings_.music_theme, 0, static_cast<int>(music_themes.size()) - 1)].file;
    const auto chosen = soundFolderFile(settings_.sound_folder, settings_.music_file);
    const std::wstring file = !chosen.empty() ? chosen.wstring() : theme ? std::wstring(theme) : std::wstring{};
    if (file.empty()) {
        music_jobs_->pending.reset();
        music_status_ = L"";
        audio_.clearMusic();
        return;
    }
    music_status_ = trKey(L"Wczytywanie…"); // Polish key; the settings row translates it.
    music_jobs_->pending = file;
    music_jobs_->wake.notify_one();
}

void Launcher::requestSound(SoundCue cue, int index, bool preview) {
    const int slot = soundCueSlot(cue);
    if (slot < 0) return;
    const uint32_t generation = ++sound_generation_[slot];
    const auto* option = soundOption(cue, index);
    // The start sound can be any file the sound folder holds (its full path).
    const std::wstring file = cue == SoundCue::boot ? soundFolderFile(settings_.sound_folder, settings_.boot_file).wstring() : std::wstring{};
    if (file.empty() && (!option || !option->file)) {
        audio_.setCue(cue, {});
        if (cue == SoundCue::boot) { audio_.boot(); boot_played_ = true; }
        return;
    }
    MusicQueue::CueRequest request{cue, index, preview, generation, file};
    std::lock_guard lock(music_jobs_->mutex);
    std::erase_if(music_jobs_->cues, [&](const MusicQueue::CueRequest& item) { return item.cue == cue; });
    music_jobs_->cues.push_back(request);
    music_jobs_->wake.notify_one();
}

void Launcher::initializeMusic() {
    setUserSoundFolder(settings_.sound_folder);
    audio_.prepareBoot();
    requestSound(SoundCue::boot, settings_.boot_sound, false);
    requestSound(SoundCue::navigation, settings_.ui_sound, false);
    requestSound(SoundCue::toast, settings_.toast_sound, false);
    requestMusic();
    // The achievement card's sound (achievement_toasts.cpp), from assets/.
    if (const auto achievement = localSoundFile(L"achievement.mp3"); !achievement.empty()) {
        std::lock_guard lock(music_jobs_->mutex);
        music_jobs_->cues.push_back({SoundCue::achievement, 0, false, 0, achievement.wstring()});
        music_jobs_->wake.notify_one();
    }
    work_.start([hwnd = hwnd_, alive = alive_, jobs = music_jobs_] {
        const auto send = [&](AudioClip clip, SoundCue cue, uint32_t generation, bool preview) {
            if (!alive->load()) return;
            auto message = std::make_unique<AudioMessage>();
            message->clip = std::move(clip);
            message->cue = cue;
            message->generation = generation;
            message->preview = preview;
            if (PostMessageW(hwnd, audio_message, 0, reinterpret_cast<LPARAM>(message.get()))) message.release();
        };
        const bool com = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
        if (com && SUCCEEDED(MFStartup(MF_VERSION, MFSTARTUP_LITE))) {
            while (alive->load()) {
                std::optional<std::wstring> theme;
                uint32_t generation = 0;
                std::vector<MusicQueue::CueRequest> cues;
                {
                    std::unique_lock lock(jobs->mutex);
                    jobs->wake.wait(lock, [&] { return !alive->load() || jobs->pending.has_value() || !jobs->cues.empty(); });
                    if (!alive->load()) break;
                    if (jobs->pending) {
                        theme = std::move(jobs->pending);
                        jobs->pending.reset();
                        generation = jobs->generation.load();
                    }
                    cues.swap(jobs->cues);
                }
                for (const auto& cue : cues) {
                    if (!alive->load()) break;
                    const auto* option = soundOption(cue.cue, cue.index);
                    // A sound-folder file comes as its full path; a built-in one as a name in assets/.
                    const auto path = !cue.file.empty() ? std::filesystem::path(cue.file) :
                                      option && option->file ? localSoundFile(option->file) : std::filesystem::path{};
                    const size_t limit = cue.cue == SoundCue::boot ? 16 * 1024 * 1024 : 8 * 1024 * 1024;
                    send(decodeFlac(path, limit), cue.cue, cue.generation, cue.preview);
                }
                if (!theme || theme->empty()) continue;
                auto cancelled = [&] { return !alive->load() || jobs->generation.load() != generation; };
                const std::filesystem::path source(*theme);
                auto clip = decodeFlac(source.is_absolute() ? source : localSoundFile(theme->c_str()), music_pcm_limit, cancelled);
                if (!cancelled()) send(std::move(clip), SoundCue::music, generation, false);
            }
            MFShutdown();
        } else {
            // Release the startup gate even if the audio decoder cannot initialize.
            std::vector<MusicQueue::CueRequest> cues;
            { std::lock_guard lock(jobs->mutex); cues.swap(jobs->cues); }
            for (const auto& cue : cues) send({}, cue.cue, cue.generation, cue.preview);
        }
        if (com) CoUninitialize();
    });
}
