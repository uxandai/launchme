#include "audio.hpp"
#include "audio_output.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

Audio::Audio() { createEngine(); }

Audio::~Audio() { releaseEngine(); }

bool Audio::createEngine() {
    next_engine_attempt_ = GetTickCount64() + 10000;
    engine_ = audio_output::Engine::create();
    return engine_ != nullptr;
}

void Audio::releaseEngine() {
    speech_voice_.reset();
    speech_.clear(); speech_format_.clear(); speech_frames_ = 0; speech_ducked_ = false;
    music_voice_.reset(); music_running_ = false; music_volume_ = 0;
    playing_.clear();
    engine_.reset(); // Voices first, then the output.
}

void Audio::recover() {
    if (engine_ && !engine_->failed()) return;
    releaseEngine();
    if (!createEngine()) return;
    if (!music_clip_.samples.empty()) { auto clip = music_clip_; startMusic(std::move(clip)); } // The theme plays on.
}

void Audio::navigate() {
    if (desktop_) return; // The desktop window moves silently, like other desktop applications.
    if (!navigation_clip_.samples.empty()) playClip(navigation_clip_, 0.28f);
    else play(false);
}
void Audio::setDesktop(bool desktop) {
    if (desktop_ == desktop) return;
    desktop_ = desktop;
    retargetMusic(); // Fades out (or back in) as a game's start would.
}
void Audio::toast() {
    if (!toast_clip_.samples.empty()) playClip(toast_clip_, 0.28f);
}
void Audio::achievement() {
    if (!achievement_clip_.samples.empty()) playClip(achievement_clip_, 0.5f);
}
void Audio::boot() {
    if (auto* sound = playClip(boot_clip_, 0.5f)) sound->startup_boot = boot_pending_;
    boot_pending_ = false; // Missing/disabled/failed boot must not leave music waiting forever.
}
void Audio::launch() { play(true); }
void Audio::context() {
    navigate();
}
void Audio::drawer() {
    navigate();
}
void Audio::quick() {
    navigate();
}

void Audio::setCue(SoundCue cue, AudioClip clip) {
    if (cue == SoundCue::music) { setMusic(std::move(clip)); return; }
    if (cue == SoundCue::achievement) { // Played whole, as it is.
        achievement_clip_ = clip.samples.empty() || clip.format.size() < sizeof(WAVEFORMATEX) ? AudioClip{} : std::move(clip);
        return;
    }
    if (cue == SoundCue::boot) {
        boot_clip_ = clip.samples.empty() || clip.format.size() < sizeof(WAVEFORMATEX) ? AudioClip{} : std::move(clip);
        return;
    }
    if (clip.samples.empty() || clip.format.size() < sizeof(WAVEFORMATEX)) return;
    const auto* format = reinterpret_cast<const WAVEFORMATEX*>(clip.format.data());
    if (!format->nBlockAlign || !format->nAvgBytesPerSec) return;
    const size_t limit = static_cast<size_t>(format->nAvgBytesPerSec * 0.34f);
    const size_t size = std::min(clip.samples.size(), limit - limit % format->nBlockAlign);
    if (!size) return;
    clip.samples.resize(size);
    // A short portion of the chord becomes a soft UI cue with no audible edge.
    auto* pcm = reinterpret_cast<int16_t*>(clip.samples.data());
    for (size_t i = 0; i < clip.samples.size() / 2; ++i) {
        const float t = static_cast<float>(i * 2) / std::max<size_t>(1, clip.samples.size());
        const float envelope = std::min(1.0f, t * 35.0f) * std::min(1.0f, (1.0f - t) * 8.0f);
        pcm[i] = static_cast<int16_t>(pcm[i] * envelope);
    }
    if (cue == SoundCue::toast) toast_clip_ = std::move(clip);
    else navigation_clip_ = std::move(clip);
}

void Audio::setMusic(AudioClip clip) {
    if (clip.samples.empty() || clip.format.size() < sizeof(WAVEFORMATEX)) return;
    if (!engine_) { music_clip_ = std::move(clip); return; } // Kept for when an output appears (recover()).
    if (music_voice_ && music_volume_ > 0.0005f) pending_music_ = std::move(clip);
    else startMusic(std::move(clip));
}

void Audio::clearMusic() {
    music_voice_.reset();
    music_running_ = false;
    music_volume_ = 0;
    music_clip_ = {}; pending_music_ = {};
}

bool Audio::musicBlocked() const {
    return boot_pending_ || std::any_of(playing_.begin(), playing_.end(), [](const auto& sound) { return sound->startup_boot; });
}

void Audio::startMusic(AudioClip clip) {
    music_voice_.reset();
    music_running_ = false;
    music_volume_ = 0;
    music_clip_ = std::move(clip);
    music_voice_ = engine_->voice(*reinterpret_cast<const WAVEFORMATEX*>(music_clip_.format.data()));
    if (!music_voice_) return;
    music_voice_->setVolume(0);
    if (!music_voice_->submit(music_clip_.samples.data(), music_clip_.samples.size(), true, false)) music_voice_.reset();
    else if (!musicBlocked() && music_target_ > 0 && music_voice_->start()) music_running_ = true;
}

void Audio::stopMusic() { music_paused_ = true; music_target_ = 0; }
void Audio::resumeMusic() {
    music_paused_ = false;
    music_target_ = musicTarget();
    if (!musicBlocked() && music_target_ > 0 && music_voice_ && !music_running_ && music_voice_->start()) music_running_ = true;
}
void Audio::duckMusic(bool ducked) {
    if (music_ducked_ == ducked) return;
    music_ducked_ = ducked;
    retargetMusic();
}
void Audio::retargetMusic() {
    if (music_paused_) return; // Games keep it off; resumeMusic applies the rest.
    music_target_ = musicTarget();
    if (!musicBlocked() && music_target_ > 0 && music_voice_ && !music_running_ && music_voice_->start()) music_running_ = true;
}

void Audio::speak(AudioClip clip, int speaker) {
    if (!engine_ || clip.samples.empty() || clip.format.size() < sizeof(WAVEFORMATEX)) return;
    const auto* format = reinterpret_cast<const WAVEFORMATEX*>(clip.format.data());
    if (!format->nBlockAlign) return;
    if (speech_voice_ && speech_format_ != clip.format) stopSpeaking(); // Another voice format: start over.
    if (!speech_voice_) {
        speech_voice_ = engine_->voice(*format);
        if (!speech_voice_) return;
        speech_format_ = clip.format;
        speech_frames_ = 0;
        if (!speech_voice_->start()) { stopSpeaking(); return; }
    }
    // A deque keeps every queued clip in place while the voice reads it.
    speech_.push_back({std::move(clip.samples), speech_frames_, speaker});
    const auto& samples = speech_.back().samples;
    if (!speech_voice_->submit(samples.data(), samples.size(), false, false)) { speech_.pop_back(); return; }
    speech_frames_ += samples.size() / format->nBlockAlign;
    if (!speech_ducked_) { speech_ducked_ = true; retargetMusic(); }
}

void Audio::stopSpeaking() {
    speech_voice_.reset();
    speech_.clear(); speech_format_.clear(); speech_frames_ = 0;
    if (speech_ducked_) { speech_ducked_ = false; retargetMusic(); }
}

bool Audio::speaking() const {
    return speech_voice_ && speech_voice_->queued() > 0;
}

float Audio::voiceLevel() const {
    if (!speech_voice_ || speech_.empty() || speech_format_.size() < sizeof(WAVEFORMATEX)) return 0;
    const auto* format = reinterpret_cast<const WAVEFORMATEX*>(speech_format_.data());
    const uint64_t played = speech_voice_->played();
    for (const auto& clip : speech_) {
        const uint64_t frames = clip.samples.size() / format->nBlockAlign;
        if (played < clip.first || played >= clip.first + frames) continue;
        // Loudness of the next 30 ms (first channel), as a mouth opening.
        const auto* pcm = reinterpret_cast<const int16_t*>(clip.samples.data());
        const uint64_t from = played - clip.first, to = std::min<uint64_t>(frames, from + format->nSamplesPerSec * 3 / 100);
        double sum = 0;
        for (uint64_t frame = from; frame < to; ++frame) {
            const double sample = pcm[frame * format->nChannels] / 32768.0;
            sum += sample * sample;
        }
        const double rms = to > from ? std::sqrt(sum / static_cast<double>(to - from)) : 0;
        return static_cast<float>(std::clamp(rms / 0.12, 0.0, 1.0));
    }
    return 0;
}

int Audio::speakingSpeaker() const {
    if (!speech_voice_ || speech_.empty() || speech_format_.size() < sizeof(WAVEFORMATEX)) return -1;
    const auto* format = reinterpret_cast<const WAVEFORMATEX*>(speech_format_.data());
    const uint64_t played = speech_voice_->played();
    for (const auto& clip : speech_) {
        const uint64_t frames = clip.samples.size() / format->nBlockAlign;
        if (played >= clip.first && played < clip.first + frames) return clip.speaker;
    }
    return speech_.front().speaker;
}

void Audio::setMusicLevel(float volume) {
    preferred_music_volume_ = std::clamp(volume, 0.0f, 0.4f);
    music_target_ = musicTarget();
    if (!musicBlocked() && music_target_ > 0 && music_voice_ && !music_running_ && music_voice_->start()) music_running_ = true;
}

Audio::Playing* Audio::playClip(const AudioClip& clip, float volume) {
    if (!engine_ || clip.samples.empty() || clip.format.size() < sizeof(WAVEFORMATEX)) return nullptr;
    auto sound = std::make_unique<Playing>();
    sound->samples = clip.samples;
    sound->voice = engine_->voice(*reinterpret_cast<const WAVEFORMATEX*>(clip.format.data()));
    if (!sound->voice) return nullptr;
    sound->voice->setVolume(volume);
    if (!sound->voice->submit(sound->samples.data(), sound->samples.size(), false, true) || !sound->voice->start()) return nullptr;
    playing_.push_back(std::move(sound));
    return playing_.back().get();
}

void Audio::play(bool confirm) {
    if (!engine_) return;
    constexpr int rate = 48000;
    const int count = confirm ? 14400 : 4704;
    auto sound = std::make_unique<Playing>();
    sound->samples.resize(count * sizeof(int16_t));
    auto* pcm = reinterpret_cast<int16_t*>(sound->samples.data());
    for (int i = 0; i < count; ++i) {
        const double t = static_cast<double>(i) / rate;
        const double phase = confirm ? (470.0 * t + 180.0 * t * t) : (670.0 * t + 150.0 * t * t);
        const double fade_in = std::min(1.0, t * 180.0);
        const double envelope = fade_in * std::pow(1.0 - static_cast<double>(i) / count, confirm ? 2.1 : 3.5);
        const double overtone = confirm ? 0.25 * std::sin(2.0 * std::numbers::pi * (phase * 1.5))
                                        : 0.18 * std::sin(2.0 * std::numbers::pi * (phase * 2.0));
        const double tone = std::sin(2.0 * std::numbers::pi * phase) + overtone;
        pcm[i] = static_cast<int16_t>(std::clamp(tone * envelope * 6100.0, -32767.0, 32767.0));
    }
    const WAVEFORMATEX format{WAVE_FORMAT_PCM, 1, rate, rate * 2, 2, 16, 0};
    sound->voice = engine_->voice(format);
    if (!sound->voice || !sound->voice->submit(sound->samples.data(), sound->samples.size(), false, true) ||
        !sound->voice->start()) return;
    playing_.push_back(std::move(sound));
}

void Audio::update(float seconds) {
    if ((engine_ && engine_->failed()) || (!engine_ && GetTickCount64() >= next_engine_attempt_)) recover();
    std::erase_if(playing_, [](const std::unique_ptr<Playing>& sound) { return sound->voice->queued() == 0; });
    if (speech_voice_) { // Her finished sentences go; the music comes back after the last.
        const uint32_t queued = speech_voice_->queued();
        while (speech_.size() > queued) speech_.pop_front();
        if (speech_.empty() && speech_ducked_) { speech_ducked_ = false; retargetMusic(); }
    }
    if (music_voice_) {
        if (musicBlocked()) return;
        if (!music_running_ && music_target_ > 0 && music_voice_->start()) music_running_ = true;
        if (!pending_music_.samples.empty()) {
            music_volume_ = std::max(0.0f, music_volume_ - seconds * std::max(0.04f, preferred_music_volume_) * 3.0f);
            if (music_volume_ < 0.0005f) {
                auto next = std::move(pending_music_); pending_music_ = {};
                startMusic(std::move(next));
            }
        } else music_volume_ += (music_target_ - music_volume_) * std::min(1.0f, seconds * 2.0f);
        if (!music_voice_) return;
        music_voice_->setVolume(music_volume_);
        if (music_running_ && music_target_ == 0 && music_volume_ < 0.0005f) {
            music_voice_->stop();
            music_running_ = false;
        }
    }
}
