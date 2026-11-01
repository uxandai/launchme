// VideoPlayer on Linux (video_player.hpp): FFmpeg decodes on one worker thread, which paces the
// frames by their timestamps against the clock, scales each to BGRA (libswscale) and hands it
// to the UI thread; the sound goes to the default output through an SDL3 audio stream. At the
// end the file starts over, as the Media Engine's loop does.
#include "video_player.hpp"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/hwcontext.h>
#include <libavutil/pixdesc.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}
#include <SDL3/SDL.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <mutex>
#include <thread>

namespace {
constexpr int audio_rate = 48000;

struct Close {
    void operator()(AVFormatContext* p) const { avformat_close_input(&p); }
    void operator()(AVCodecContext* p) const { avcodec_free_context(&p); }
    void operator()(SwrContext* p) const { swr_free(&p); }
    void operator()(SwsContext* p) const { sws_freeContext(p); }
    void operator()(AVPacket* p) const { av_packet_free(&p); }
    void operator()(AVFrame* p) const { av_frame_free(&p); }
};
template <class T> using Owned = std::unique_ptr<T, Close>;

// The GPU's video decoder through VA-API (Intel, AMD, the Steam Deck) where the system has one for
// this codec: decoded frames come back to memory (av_hwframe_transfer_data) for the BGRA scaler, so
// the CPU only converts. A profile the GPU lacks falls back to software in get_format.
// LAUNCHME_VIDEO_HWACCEL=0 decodes on the CPU.
bool hardwareWanted() {
    const char* value = std::getenv("LAUNCHME_VIDEO_HWACCEL");
    return !(value && value[0] == '0');
}
bool vaapiDecodes(const AVCodec* codec) {
    for (int i = 0;; ++i) {
        const AVCodecHWConfig* config = avcodec_get_hw_config(codec, i);
        if (!config) return false;
        if (config->device_type == AV_HWDEVICE_TYPE_VAAPI && (config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX)) return true;
    }
}
AVPixelFormat pickFormat(AVCodecContext*, const AVPixelFormat* formats) {
    for (const AVPixelFormat* format = formats; *format != AV_PIX_FMT_NONE; ++format)
        if (*format == AV_PIX_FMT_VAAPI) return *format;
    for (const AVPixelFormat* format = formats; *format != AV_PIX_FMT_NONE; ++format) {
        const AVPixFmtDescriptor* description = av_pix_fmt_desc_get(*format);
        if (description && !(description->flags & AV_PIX_FMT_FLAG_HWACCEL)) return *format;
    }
    return AV_PIX_FMT_NONE;
}

Owned<AVCodecContext> openDecoder(const AVStream* stream, bool hardware = false) {
    const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!codec) return nullptr;
    Owned<AVCodecContext> decoder(avcodec_alloc_context3(codec));
    if (!decoder || avcodec_parameters_to_context(decoder.get(), stream->codecpar) < 0) return nullptr;
    decoder->thread_count = 0; // As many as FFmpeg finds useful.
    AVBufferRef* device = nullptr;
    if (hardware && vaapiDecodes(codec) && av_hwdevice_ctx_create(&device, AV_HWDEVICE_TYPE_VAAPI, nullptr, nullptr, 0) >= 0) {
        decoder->hw_device_ctx = device; // The decoder owns it now.
        decoder->get_format = pickFormat;
        decoder->thread_count = 1; // The GPU decodes; threads would only hold more surfaces.
    }
    if (avcodec_open2(decoder.get(), codec, nullptr) < 0) return device ? openDecoder(stream, false) : nullptr;
    return decoder;
}
}

struct VideoPlayer::Impl {
    // Opened on the UI thread (open() reports failure at once), then used by the worker only.
    Owned<AVFormatContext> input;
    Owned<AVCodecContext> video, audio;
    Owned<SwrContext> resampler;
    int video_index = -1, audio_index = -1;
    SDL_AudioStream* sound = nullptr; // Locks itself; the worker puts, the UI thread sets the gain.
    bool sdl_audio = false;

    std::thread worker;
    std::mutex mutex;
    std::condition_variable wake;
    bool stopping = false;
    bool paused = false; // Under the mutex: the worker waits before its next picture.
    // The latest frame, swapped out by frame().
    std::vector<uint8_t> ready;
    uint32_t ready_width = 0, ready_height = 0;
    bool fresh = false;
    std::atomic<uint32_t> max_width{1280};

    void run(const std::shared_ptr<std::atomic_bool>& error);
    bool waitUntil(std::chrono::steady_clock::time_point when) {
        std::unique_lock lock(mutex);
        return !wake.wait_until(lock, when, [&] { return stopping; });
    }
    bool stopped() { std::lock_guard lock(mutex); return stopping; }
    // While paused: waits, and moves the clock on by the time spent so the video goes on from where it stood.
    bool waitWhilePaused(std::chrono::steady_clock::duration& shift) {
        std::unique_lock lock(mutex);
        if (!paused) return !stopping;
        const auto since = std::chrono::steady_clock::now();
        wake.wait(lock, [&] { return stopping || !paused; });
        shift += std::chrono::steady_clock::now() - since;
        return !stopping;
    }
    void close() {
        if (sound) SDL_DestroyAudioStream(sound);
        sound = nullptr;
        if (sdl_audio) SDL_QuitSubSystem(SDL_INIT_AUDIO);
        sdl_audio = false;
        resampler.reset(); audio.reset(); video.reset(); input.reset();
        video_index = audio_index = -1;
        ready.clear(); ready_width = ready_height = 0; fresh = false;
        stopping = false; paused = false;
    }
};

void VideoPlayer::Impl::run(const std::shared_ptr<std::atomic_bool>& error) {
    using clock = std::chrono::steady_clock;
    Owned<AVPacket> packet(av_packet_alloc());
    Owned<AVFrame> frame(av_frame_alloc()), software(av_frame_alloc()); // software: a VA-API frame copied back.
    if (!packet || !frame || !software) { error->store(true); return; }
    Owned<SwsContext> scaler;
    std::vector<uint8_t> scratch, pcm; // The next picture; converted sound.
    const AVStream* stream = input->streams[video_index];
    const double base = av_q2d(stream->time_base);
    const AVRational guessed = av_guess_frame_rate(input.get(), const_cast<AVStream*>(stream), nullptr);
    const double frame_time = guessed.num > 0 && guessed.den > 0 ? av_q2d(av_inv_q(guessed)) : 1.0 / 30;
    const int64_t first = stream->start_time != AV_NOPTS_VALUE ? stream->start_time : 0;
    const auto start = clock::now();
    clock::duration paused_for{}; // Time spent paused: every later picture is due that much later.
    double offset = 0, pass_end = 0, last = -frame_time; // Seconds: loops so far, this pass's length, the last frame.
    int pass_frames = 0;

    const auto present = [&](AVFrame* picture) {
        const int64_t stamp = picture->best_effort_timestamp;
        const double at = stamp != AV_NOPTS_VALUE ? (stamp - first) * base : last + frame_time;
        const double length = picture->duration > 0 ? picture->duration * base : frame_time;
        last = at; pass_end = std::max(pass_end, at + length); ++pass_frames;
        if (!waitWhilePaused(paused_for)) return false;
        const auto due = start + paused_for + std::chrono::duration_cast<clock::duration>(std::chrono::duration<double>(offset + at));
        if (!waitUntil(due)) return false;
        if (clock::now() - due > std::chrono::milliseconds(100)) return true; // Behind: drop it, catch up.
        if (picture->format == AV_PIX_FMT_VAAPI) { // Decoded on the GPU: its pixels back to memory (NV12 mostly).
            av_frame_unref(software.get());
            if (av_hwframe_transfer_data(software.get(), picture, 0) < 0) return true;
            picture = software.get();
        }
        const int width = picture->width, height = picture->height;
        if (width <= 0 || height <= 0) return true;
        uint32_t target_width = static_cast<uint32_t>(width), target_height = static_cast<uint32_t>(height);
        const uint32_t limit = max_width.load();
        if (limit && target_width > limit) {
            target_height = std::max<uint32_t>(1, static_cast<uint32_t>(uint64_t(height) * limit / width));
            target_width = limit;
        }
        scaler.reset(sws_getCachedContext(scaler.release(), width, height, static_cast<AVPixelFormat>(picture->format),
            static_cast<int>(target_width), static_cast<int>(target_height), AV_PIX_FMT_BGRA, SWS_BILINEAR, nullptr, nullptr, nullptr));
        if (!scaler) return true;
        scratch.resize(size_t(target_width) * target_height * 4);
        uint8_t* planes[4] = {scratch.data()};
        const int strides[4] = {static_cast<int>(target_width * 4)};
        if (sws_scale(scaler.get(), picture->data, picture->linesize, 0, height, planes, strides) <= 0) return true;
        std::lock_guard lock(mutex);
        ready.swap(scratch);
        ready_width = target_width; ready_height = target_height; fresh = true;
        return true;
    };
    const auto play = [&](AVFrame* samples) {
        if (!sound || !resampler) return;
        const int room = swr_get_out_samples(resampler.get(), samples->nb_samples);
        if (room <= 0) return;
        pcm.resize(size_t(room) * 2 * sizeof(float));
        uint8_t* out = pcm.data();
        const int done = swr_convert(resampler.get(), &out, room, const_cast<const uint8_t**>(samples->extended_data), samples->nb_samples);
        if (done > 0) SDL_PutAudioStreamData(sound, pcm.data(), done * 2 * static_cast<int>(sizeof(float)));
    };
    // Every frame the decoder has ready; false when stopping.
    const auto drain = [&](AVCodecContext* decoder, bool picture) {
        while (avcodec_receive_frame(decoder, frame.get()) >= 0) {
            const bool going = picture ? present(frame.get()) : (play(frame.get()), true);
            av_frame_unref(frame.get());
            if (!going) return false;
        }
        return true;
    };

    while (!stopped()) {
        // Sound runs ahead of the picture by the file's interleaving; never by more than two seconds.
        if (sound && SDL_GetAudioStreamQueued(sound) > audio_rate * 2 * static_cast<int>(sizeof(float)) * 2) {
            if (!waitUntil(clock::now() + std::chrono::milliseconds(20))) return;
            continue;
        }
        const int read = av_read_frame(input.get(), packet.get());
        if (read == AVERROR_EOF) {
            for (auto* decoder : {video.get(), audio.get()}) {
                if (!decoder || avcodec_send_packet(decoder, nullptr) < 0) continue;
                if (!drain(decoder, decoder == video.get())) return;
            }
            if (!pass_frames || av_seek_frame(input.get(), -1, input->start_time != AV_NOPTS_VALUE ? input->start_time : 0, AVSEEK_FLAG_BACKWARD) < 0) {
                error->store(true); return; // Nothing to show, or no way back to the start.
            }
            avcodec_flush_buffers(video.get());
            if (audio) avcodec_flush_buffers(audio.get());
            offset += pass_end;
            pass_end = 0; pass_frames = 0; last = -frame_time;
            continue;
        }
        if (read < 0) { error->store(true); return; }
        AVCodecContext* decoder = packet->stream_index == video_index ? video.get() : packet->stream_index == audio_index ? audio.get() : nullptr;
        if (decoder) {
            const int sent = avcodec_send_packet(decoder, packet.get());
            av_packet_unref(packet.get());
            if (sent < 0 && sent != AVERROR_INVALIDDATA && sent != AVERROR(EAGAIN)) { error->store(true); return; }
            if (!drain(decoder, decoder == video.get())) return;
        } else av_packet_unref(packet.get());
    }
}

VideoPlayer::VideoPlayer() : impl_(std::make_unique<Impl>()) {}

VideoPlayer::~VideoPlayer() { stop(); }

bool VideoPlayer::active() const { return impl_->worker.joinable(); }

// Frames always come through memory here (frame()); a GPU device is Windows' path.
void* VideoPlayer::device() const { return nullptr; }
bool VideoPlayer::nextFrame(uint32_t&, uint32_t&, uint32_t) { return false; }
bool VideoPlayer::transfer(void*, uint32_t, uint32_t) { return false; }

bool VideoPlayer::open(const std::filesystem::path& file, double volume, void*) {
    stop();
    error_ = std::make_shared<std::atomic_bool>(false);
    av_log_set_level(AV_LOG_ERROR); // Not the decoders' notes on recoverable quirks (MP3 padding, ...).
    auto& impl = *impl_;
    AVFormatContext* opened = nullptr;
    if (avformat_open_input(&opened, file.c_str(), nullptr, nullptr) < 0) return false;
    impl.input.reset(opened);
    if (avformat_find_stream_info(impl.input.get(), nullptr) < 0) { impl.close(); return false; }
    impl.video_index = av_find_best_stream(impl.input.get(), AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (impl.video_index < 0 || !(impl.video = openDecoder(impl.input->streams[impl.video_index], hardwareWanted()))) { impl.close(); return false; }
    // Sound is optional: a silent clip, or no output, still plays its picture.
    impl.audio_index = av_find_best_stream(impl.input.get(), AVMEDIA_TYPE_AUDIO, -1, impl.video_index, nullptr, 0);
    if (impl.audio_index >= 0) impl.audio = openDecoder(impl.input->streams[impl.audio_index]);
    if (impl.audio) {
        AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
        SwrContext* resampler = nullptr;
        if (swr_alloc_set_opts2(&resampler, &stereo, AV_SAMPLE_FMT_FLT, audio_rate, &impl.audio->ch_layout,
                                impl.audio->sample_fmt, impl.audio->sample_rate, 0, nullptr) >= 0) impl.resampler.reset(resampler);
        if (impl.resampler && swr_init(impl.resampler.get()) < 0) impl.resampler.reset();
        SDL_SetHintWithPriority(SDL_HINT_APP_NAME, "Launchme", SDL_HINT_DEFAULT);
        if (impl.resampler && (impl.sdl_audio = SDL_InitSubSystem(SDL_INIT_AUDIO))) {
            const SDL_AudioSpec spec{SDL_AUDIO_F32, 2, audio_rate};
            impl.sound = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
        }
    }
    if (impl.sound) {
        setVolume(volume);
        SDL_ResumeAudioStreamDevice(impl.sound);
    }
    impl.worker = std::thread([&impl, error = error_] { impl.run(error); });
    return true;
}

void VideoPlayer::setPaused(bool paused) {
    auto& impl = *impl_;
    if (!impl.worker.joinable() || paused == paused_) return;
    { std::lock_guard lock(impl.mutex); impl.paused = paused; }
    impl.wake.notify_all();
    if (impl.sound) paused ? SDL_PauseAudioStreamDevice(impl.sound) : SDL_ResumeAudioStreamDevice(impl.sound);
    paused_ = paused;
}

void VideoPlayer::stop() {
    auto& impl = *impl_;
    paused_ = false;
    if (!impl.worker.joinable()) return;
    { std::lock_guard lock(impl.mutex); impl.stopping = true; }
    impl.wake.notify_all();
    impl.worker.join();
    impl.close();
}

void VideoPlayer::setVolume(double volume) {
    if (impl_->sound) SDL_SetAudioStreamGain(impl_->sound, static_cast<float>(std::clamp(volume, 0.0, 1.0)));
}

bool VideoPlayer::frame(std::vector<uint8_t>& pixels, uint32_t& width, uint32_t& height, uint32_t max_width) {
    auto& impl = *impl_;
    if (!impl.worker.joinable() || error_->load()) return false;
    impl.max_width.store(max_width);
    std::lock_guard lock(impl.mutex);
    if (!impl.fresh) return false;
    pixels.swap(impl.ready);
    width = impl.ready_width; height = impl.ready_height;
    impl.fresh = false;
    return true;
}
