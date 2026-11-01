#pragma once

#ifdef _WIN32
#include <Windows.h>
#include <wrl/client.h>
#endif

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <vector>

// A looping video file played by the Media Foundation Media Engine in frame-server mode (no
// window; video_player_windows.cpp) or by FFmpeg and SDL3 audio (video_player_ffmpeg.cpp, decoding
// on VA-API where the system has it): its sound goes to the default device, its frames are copied
// out on the UI thread, or on Windows drawn straight into a texture of the renderer's device.
// UI thread only; the player's own threads only raise the error flag.
class VideoPlayer {
public:
    VideoPlayer();
    ~VideoPlayer();
    VideoPlayer(const VideoPlayer&) = delete;
    VideoPlayer& operator=(const VideoPlayer&) = delete;

    // `device` (Windows: an ID3D11Device made with video support; null elsewhere): the video is
    // decoded on that GPU and its frames come through nextFrame/transfer instead of frame().
    bool open(const std::filesystem::path& file, double volume, void* device = nullptr);
    void stop();
    bool active() const;
    bool failed() const { return error_->load(); }
    void setVolume(double volume);
    // Holds the picture (and the sound) where it is, decoding nothing meanwhile; false plays on from there.
    void setPaused(bool paused);
    bool paused() const { return paused_; }
    // The frame shown now when it changed since the last call: BGRA, stride width * 4,
    // scaled down to at most max_width pixels wide.
    bool frame(std::vector<uint8_t>& pixels, uint32_t& width, uint32_t& height, uint32_t max_width = 1280);
    // Opened on a device: the device, and whether a new frame is due (its size, at most max_width
    // wide), drawn then into `texture` (an ID3D11Texture2D of that size on the device, BGRA).
    void* device() const;
    bool nextFrame(uint32_t& width, uint32_t& height, uint32_t max_width);
    bool transfer(void* texture, uint32_t width, uint32_t height);

private:
    struct Impl; // The platform's engine and frame buffers.
    std::unique_ptr<Impl> impl_;
    std::shared_ptr<std::atomic_bool> error_ = std::make_shared<std::atomic_bool>(false);
    bool paused_ = false; // open() and stop() reset it.
};
