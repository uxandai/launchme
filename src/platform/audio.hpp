#pragma once

#ifdef _WIN32 // Kept for the callers that rely on them; Audio itself needs no platform types.
#include <Windows.h>
#include <xaudio2.h>
#include <wrl/client.h>
#endif

#include <cstdint>
#include <deque>
#include <memory>
#include <vector>
#include "flac.hpp"

// The platform half (audio_output.hpp): XAudio2 on Windows, SDL3 audio streams elsewhere.
namespace audio_output { class Engine; class Voice; }

enum class SoundCue { navigation, toast, boot, music, achievement };

inline int soundCueSlot(SoundCue cue) {
    switch (cue) {
    case SoundCue::navigation: return 0;
    case SoundCue::toast: return 1;
    case SoundCue::boot: return 2;
    default: return -1;
    }
}

class Audio {
    friend class LauncherUiTest;
public:
    Audio();
    ~Audio();
    void navigate();
    void toast();
    void achievement(); // An achievement unlocked over a game (assets/achievement.mp3), at half volume.
    void prepareBoot() { boot_pending_ = true; }
    void boot();
    void skipBoot() { boot_pending_ = false; } // The desktop window starts silently: music is not left waiting.
    // The desktop window: no menu music and no navigation cues (toasts and the launch keep theirs).
    void setDesktop(bool desktop);
    void launch();
    // Opening a context menu, drawer or quick menu; all share the navigation cue.
    void context();
    void drawer();
    void quick();
    void setCue(SoundCue cue, AudioClip clip);
    void setMusic(AudioClip clip);
    void clearMusic();
    void cancelPendingMusic() { pending_music_ = {}; }
    void stopMusic();
    void resumeMusic();
    void setMusicLevel(float volume);
    // A game video plays: music fades out and back, apart from stopMusic/resumeMusic (games).
    void duckMusic(bool ducked);
    void update(float seconds);
    // Her voice (src/app/companion_voice.cpp): 16-bit PCM clips queued back to back on one source
    // voice; the menu music dips while she speaks. voiceLevel(): her loudness right now, 0–1;
    // speakingSpeaker(): the companion index of the clip currently playing (-1 when unspecified).
    void speak(AudioClip clip, int speaker = -1);
    void stopSpeaking();
    bool speaking() const;
    float voiceLevel() const;
    int speakingSpeaker() const;
    // After sleep or a change of audio devices: rebuild the engine if its output is gone.
    void recover();

private:
    struct Playing {
        // Declared before `voice`, so it is destroyed after it: the voice's destructor (DestroyVoice)
        // waits for the audio thread, which reads these samples until then.
        std::vector<uint8_t> samples;
        std::unique_ptr<audio_output::Voice> voice;
        bool startup_boot = false;
    };
    void play(bool confirm);
    Playing* playClip(const AudioClip& clip, float volume);
    bool musicBlocked() const;
    void startMusic(AudioClip clip);
    // The engine and its output. No output device at start (a TV that is off) or a lost device
    // (XAudio2's critical error) is not silence for the rest of the run: update() tries again.
    bool createEngine();
    void releaseEngine();
    uint64_t next_engine_attempt_ = 0;
    std::unique_ptr<audio_output::Engine> engine_;
    std::vector<std::unique_ptr<Playing>> playing_;
    AudioClip navigation_clip_;
    AudioClip toast_clip_;
    AudioClip boot_clip_;
    AudioClip achievement_clip_;
    AudioClip music_clip_;
    AudioClip pending_music_;
    std::unique_ptr<audio_output::Voice> music_voice_;
    bool music_running_ = false;
    bool music_paused_ = false;
    bool music_ducked_ = false;
    bool desktop_ = false;
    float musicTarget() const { return music_paused_ || music_ducked_ || desktop_ ? 0.0f : preferred_music_volume_ * (speech_ducked_ ? 0.3f : 1.0f); }
    void retargetMusic();
    // Her voice: the clips still queued, each with the index of its first frame in the voice's stream.
    struct Speech { std::vector<uint8_t> samples; uint64_t first = 0; int speaker = -1; };
    std::unique_ptr<audio_output::Voice> speech_voice_;
    std::vector<uint8_t> speech_format_;
    std::deque<Speech> speech_;
    uint64_t speech_frames_ = 0;
    bool speech_ducked_ = false;
    bool boot_pending_ = false;
    float music_volume_ = 0;
    float music_target_ = 0.075f;
    float preferred_music_volume_ = 0.075f;
};
