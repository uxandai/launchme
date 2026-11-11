#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <vector>

// Animated artwork: SteamGridDB's animated grids and heroes are animated WebP files (their "thumb"
// is a WebM video, not used). The container is read here (animated_image.cpp, every platform);
// frames are decoded by WIC on Windows (animated_image_wic.cpp: its WebP decoder hands out each
// frame already composed on the canvas). Elsewhere nothing decodes yet (animated_image_linux.cpp):
// the still first frame stands in.
namespace animated_image {
// Frames decode on this platform (Windows): animated artwork is offered at all.
bool supported();
// A RIFF WEBP file whose VP8X header has the animation flag.
bool animatedWebp(std::span<const uint8_t> bytes);
// How long each frame shows (its ANMF chunk), in file order, in milliseconds. A duration of 10 ms or
// less counts as 100 ms, as browsers show it.
std::vector<uint32_t> frameDurations(std::span<const uint8_t> bytes);
std::vector<uint32_t> frameDurations(const std::filesystem::path& file);
// The start of a file (a ranged download): the whole frames it holds as a complete file, its RIFF size
// written anew. Empty when not even the first frame arrived whole.
std::vector<uint8_t> wholeFrames(std::span<const uint8_t> bytes);

// A short loop decoded up front (Zmień grafikę's tiles): premultiplied BGRA frames of one size.
struct Frames {
    uint32_t width = 0, height = 0;
    std::vector<std::vector<uint8_t>> pixels;
    std::vector<uint32_t> durations; // Milliseconds per frame, as pixels.
    uint32_t total = 0;              // Their sum: one pass of the loop.
    size_t bytes() const;
    size_t at(uint64_t milliseconds) const; // The frame shown that long after the loop started.
};
// Frames fit inside width x height (the aspect kept, never enlarged), each shown at least min_ms (the
// frames between are skipped, their time added), until max_frames or `budget` bytes of pixels.
// Nullopt for anything that is not an animated WebP or does not decode (and on Linux).
std::optional<Frames> decodeLoop(std::span<const uint8_t> bytes, uint32_t width, uint32_t height,
                                 size_t budget, size_t max_frames, uint32_t min_ms);

// An animated WebP file played in a loop on its own thread, its frames fit inside width x height
// (aspect kept, never enlarged). Like VideoPlayer: frame() on the UI thread hands over the frame
// due now when it changed. stop() (and the destructor) never wait for the thread: it ends after the
// frame it is decoding.
class Player {
public:
    Player() = default;
    ~Player();
    Player(const Player&) = delete;
    Player& operator=(const Player&) = delete;
    bool open(const std::filesystem::path& file, uint32_t width, uint32_t height);
    void stop();
    bool active() const { return shared_ != nullptr; }
    bool failed() const;
    // Premultiplied BGRA, stride width * 4.
    bool frame(std::vector<uint8_t>& pixels, uint32_t& width, uint32_t& height);
    struct Shared; // The thread's state (animated_image_wic.cpp).
private:
    std::shared_ptr<Shared> shared_;
};
} // namespace animated_image
