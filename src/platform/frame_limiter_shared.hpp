#pragma once
// The page of memory the launcher and the limiter library inside a game share (one per game process,
// "Local\launchme.limiter.<pid>"). The launcher writes the limit; the library only reads it.
#include <cstdint>
#include <string>

namespace frame_limiter {
inline constexpr uint32_t shared_magic = 0x4C4D464C; // "LMFL"
inline constexpr uint32_t hooked_none = 0, hooked_ok = 1, hooked_failed = 2;
struct Shared {
    uint32_t magic = shared_magic;
    volatile uint32_t limit_x100 = 0; // FPS * 100; 0: no limit.
    volatile uint32_t precise = 1;    // 1: the last part of each wait is a spin (smoother, a little more CPU).
    volatile uint32_t heartbeat = 0;  // GetTickCount() of the launcher: stale for 5 s means it is gone, no limit.
    volatile uint32_t hooked = hooked_none; // Set by the library once Present is wrapped.
    volatile uint32_t presents = 0;   // Presents the library has seen (proves the game is on DXGI).
    volatile uint32_t mode = 0;       // 0: smooth (a frame waits for its slot, in Present); 1: adaptive (see below).
    volatile uint32_t reserved = 0;
    volatile uint64_t held_us = 0;    // Time frames spent waiting inside Present: what mode 0 adds to the latency.
};
// Mode 1 waits after Present instead, so the game starts its next frame as late as it can still make the slot
// (its measured cost plus a margin): the same even delivery, the input read just before the frame is built.
inline constexpr uint32_t mode_smooth = 0, mode_adaptive = 1;
inline std::wstring mappingName(uint32_t pid) { return L"Local\\launchme.limiter." + std::to_wstring(pid); }
inline constexpr wchar_t library_name[] = L"launchme_limiter.dll";
}
