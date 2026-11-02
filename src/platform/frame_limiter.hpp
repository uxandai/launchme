#pragma once
// Frame limiter for a running game (Ustawienia -> FPS i limiter). On request it loads
// launchme_limiter.dll into the game (src/limiter/limiter_hook.cpp), which paces IDXGISwapChain::Present
// to the limit written in a shared page (frame_limiter_shared.hpp). Windows, x64 games on DXGI only.
// Nothing here runs unless the player turned the limiter on; a game with an anti-cheat is left alone.
#include <cstdint>
#include <filesystem>
#include <memory>

namespace frame_limiter {

enum class State {
    off,          // Nothing loaded into the game.
    starting,     // The library is going in, or has not seen a frame yet.
    active,       // Frames pass through the library.
    no_dxgi,      // Loaded, but the game never presented through DXGI (Vulkan, OpenGL, Direct3D 9).
    unsupported,  // A 32-bit game, or this build has no library.
    anticheat,    // The game folder holds an anti-cheat: nothing is loaded.
    denied,       // The game runs with more rights than the launcher.
    failed,
};

// Names in a game folder that mean an anti-cheat is guarding it.
bool antiCheatIn(const std::filesystem::path& folder);

class Controller {
public:
    Controller();
    ~Controller();
    // The limit for one game process; fps 0: none. The first non-zero limit loads the library in the background.
    // mode: frame_limiter::mode_smooth or mode_adaptive (frame_limiter_shared.hpp).
    void apply(uint32_t pid, double fps, bool precise, uint32_t mode = 0);
    // Microseconds the game's frames have waited inside Present so far (what the smooth mode adds to the latency).
    uint64_t heldMicroseconds(uint32_t pid) const;
    // Keeps every limited game's page fresh and forgets the ones that ended. Call a few times per second.
    void heartbeat();
    State state(uint32_t pid) const;
    // The folder holding launchme_limiter.dll; the executable's by default (tests point it elsewhere).
    void library(std::filesystem::path path);
private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
};

} // namespace frame_limiter
