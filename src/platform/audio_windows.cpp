// Audio's output on Windows: XAudio2 (audio_output.hpp).
#include "audio_output.hpp"

#include <Windows.h>
#include <xaudio2.h>
#include <wrl/client.h>

#include <atomic>

namespace audio_output {
namespace {
// XAudio2 reports a lost output device on its own thread; Audio::update() rebuilds on the UI thread.
struct EngineCallback : IXAudio2EngineCallback {
    std::atomic_bool failed{false};
    void STDMETHODCALLTYPE OnProcessingPassStart() override {}
    void STDMETHODCALLTYPE OnProcessingPassEnd() override {}
    void STDMETHODCALLTYPE OnCriticalError(HRESULT) override { failed.store(true); }
};

class XAudio2Voice final : public Voice {
public:
    explicit XAudio2Voice(IXAudio2SourceVoice* voice) : voice_(voice) {}
    ~XAudio2Voice() override { voice_->DestroyVoice(); }
    bool submit(const uint8_t* data, size_t size, bool loop, bool last) override {
        XAUDIO2_BUFFER buffer{};
        buffer.AudioBytes = static_cast<UINT32>(size);
        buffer.pAudioData = data;
        if (loop) buffer.LoopCount = XAUDIO2_LOOP_INFINITE;
        if (last) buffer.Flags = XAUDIO2_END_OF_STREAM;
        return SUCCEEDED(voice_->SubmitSourceBuffer(&buffer));
    }
    bool start() override { return SUCCEEDED(voice_->Start()); }
    void stop() override { voice_->Stop(); }
    void setVolume(float volume) override { voice_->SetVolume(volume); }
    uint32_t queued() const override {
        XAUDIO2_VOICE_STATE state{};
        voice_->GetState(&state, XAUDIO2_VOICE_NOSAMPLESPLAYED);
        return state.BuffersQueued;
    }
    uint64_t played() const override {
        XAUDIO2_VOICE_STATE state{};
        voice_->GetState(&state);
        return state.SamplesPlayed;
    }
private:
    IXAudio2SourceVoice* voice_;
};

class XAudio2Engine final : public Engine {
public:
    bool open() {
        if (FAILED(XAudio2Create(engine_.GetAddressOf(), 0, XAUDIO2_DEFAULT_PROCESSOR))) { engine_.Reset(); return false; }
        // The default device through XAudio2's virtual client: a new default output is followed.
        if (FAILED(engine_->CreateMasteringVoice(&master_))) { master_ = nullptr; engine_.Reset(); return false; }
#ifdef LAUNCHME_UI_TEST
        master_->SetVolume(0); // UI tests play every sound as the app does, silently.
#endif
        engine_->RegisterForCallbacks(&callback_);
        return true;
    }
    ~XAudio2Engine() override {
        if (master_) master_->DestroyVoice();
        if (engine_) { engine_->UnregisterForCallbacks(&callback_); engine_->StopEngine(); }
    }
    bool failed() const override { return callback_.failed.load(); }
    std::unique_ptr<Voice> voice(const WAVEFORMATEX& format) override {
        IXAudio2SourceVoice* voice = nullptr;
        if (FAILED(engine_->CreateSourceVoice(&voice, &format))) return nullptr;
        return std::make_unique<XAudio2Voice>(voice);
    }
private:
    EngineCallback callback_;
    Microsoft::WRL::ComPtr<IXAudio2> engine_;
    IXAudio2MasteringVoice* master_ = nullptr;
};
}

std::unique_ptr<Engine> Engine::create() {
    auto engine = std::make_unique<XAudio2Engine>();
    return engine->open() ? std::move(engine) : nullptr;
}
}
