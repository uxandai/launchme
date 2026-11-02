// The frame limiter end to end: this process presents through a real DXGI swap chain, the controller
// loads launchme_limiter.dll into it (itself, by the same path a game takes) and the measured rate
// follows the limit. Also the anti-cheat guard and the states before anything is loaded.
#include "../src/platform/frame_limiter.hpp"
#include "../src/platform/frame_limiter_shared.hpp"

#include <Windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <numeric>
#include <vector>

namespace {
using Clock = std::chrono::steady_clock;

struct Swap {
    HWND window = nullptr;
    IDXGISwapChain* chain = nullptr;
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    ~Swap() {
        if (chain) chain->Release();
        if (context) context->Release();
        if (device) device->Release();
        if (window) DestroyWindow(window);
    }
    bool create() {
        window = CreateWindowExW(0, L"STATIC", L"limiter test", WS_POPUP, 0, 0, 128, 128, nullptr, nullptr, nullptr, nullptr);
        DXGI_SWAP_CHAIN_DESC description{};
        description.BufferDesc.Width = 128; description.BufferDesc.Height = 128;
        description.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        description.SampleDesc.Count = 1;
        description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        description.BufferCount = 1;
        description.OutputWindow = window;
        description.Windowed = TRUE;
        for (const auto driver : {D3D_DRIVER_TYPE_HARDWARE, D3D_DRIVER_TYPE_WARP})
            if (SUCCEEDED(D3D11CreateDeviceAndSwapChain(nullptr, driver, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &description,
                    &chain, &device, nullptr, &context))) return true;
        return false;
    }
};

struct Rate { double fps = 0, worst_ms = 0, deviation_ms = 0; };

// Presents as fast as allowed for `seconds`, keeping the controller's heartbeat alive.
Rate measure(Swap& swap, frame_limiter::Controller& controller, double seconds, double game_ms = 0) {
    std::vector<double> intervals;
    const auto begin = Clock::now();
    auto last = begin;
    while (std::chrono::duration<double>(Clock::now() - begin).count() < seconds) {
        // A game's own work for the frame: a busy loop, as a game's CPU time is.
        if (game_ms > 0) for (const auto work = Clock::now(); std::chrono::duration<double, std::milli>(Clock::now() - work).count() < game_ms;) {}
        swap.chain->Present(0, 0);
        const auto now = Clock::now();
        intervals.push_back(std::chrono::duration<double, std::milli>(now - last).count());
        last = now;
        controller.heartbeat();
    }
    if (intervals.size() > 10) intervals.erase(intervals.begin(), intervals.begin() + 5); // Warm-up.
    Rate rate;
    const double total = std::accumulate(intervals.begin(), intervals.end(), 0.0);
    const double mean = total / intervals.size();
    rate.fps = 1000.0 * intervals.size() / total;
    rate.worst_ms = *std::max_element(intervals.begin(), intervals.end());
    double sum = 0;
    for (const double value : intervals) sum += (value - mean) * (value - mean);
    rate.deviation_ms = std::sqrt(sum / intervals.size());
    return rate;
}

int fail(int code, const char* what) { std::printf("frame_limiter_tests: %d %s\n", code, what); return code; }
}

int main() {
    namespace fl = frame_limiter;
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST); // Timing under a busy machine (tests run in parallel).
    // The anti-cheat guard looks at names in the game's folder.
    const auto folder = std::filesystem::temp_directory_path() / L"launchme_limiter_test";
    std::filesystem::remove_all(folder);
    std::filesystem::create_directories(folder);
    if (fl::antiCheatIn(folder)) return fail(1, "empty folder flagged");
    std::ofstream(folder / L"game.exe").put('x');
    if (fl::antiCheatIn(folder)) return fail(2, "plain game flagged");
    std::filesystem::create_directories(folder / L"EasyAntiCheat");
    if (!fl::antiCheatIn(folder)) return fail(3, "Easy Anti-Cheat missed");
    std::filesystem::remove_all(folder);
    std::filesystem::create_directories(folder);
    std::ofstream(folder / L"BEService_x64.exe").put('x');
    if (!fl::antiCheatIn(folder)) return fail(4, "BattlEye missed");
    std::filesystem::remove_all(folder);

    Swap swap;
    if (!swap.create()) { std::printf("frame_limiter_tests: no Direct3D 11 device, skipped\n"); return 0; }
    fl::Controller controller;
    const uint32_t pid = GetCurrentProcessId();
    if (controller.state(pid) != fl::State::off) return fail(10, "state before any limit");

    const Rate free_run = measure(swap, controller, 1.0);
    if (free_run.fps < 200) return fail(11, "baseline too slow to tell a limit from the machine");

    controller.apply(pid, 60, true);
    const auto wait_until = Clock::now() + std::chrono::seconds(15);
    while (controller.state(pid) != fl::State::active && Clock::now() < wait_until) {
        swap.chain->Present(0, 0);
        controller.heartbeat();
        if (const auto state = controller.state(pid); state != fl::State::starting && state != fl::State::active)
            return fail(20 + static_cast<int>(state), "the library did not load");
    }
    if (controller.state(pid) != fl::State::active) return fail(30, "limiter never became active");

    const Rate sixty = measure(swap, controller, 2.0);
    std::printf("60: %.1f fps, deviation %.2f ms, worst %.1f ms (free: %.0f fps)\n", sixty.fps, sixty.deviation_ms, sixty.worst_ms, free_run.fps);
    if (std::abs(sixty.fps - 60) > 1.5) return fail(31, "60 fps limit missed");
    if (sixty.deviation_ms > 3) return fail(32, "60 fps pacing uneven");

    controller.apply(pid, 30, false);
    const Rate thirty = measure(swap, controller, 2.0);
    std::printf("30 (relaxed): %.1f fps, deviation %.2f ms\n", thirty.fps, thirty.deviation_ms);
    if (std::abs(thirty.fps - 30) > 1.5) return fail(33, "30 fps limit missed");

    // A game that needs 4 ms per frame at 60 FPS: the smooth mode holds each frame for the rest of its slot (about
    // 12.7 ms of added latency); the adaptive mode starts the frame late instead and holds it for next to nothing.
    controller.apply(pid, 60, true, frame_limiter::mode_smooth);
    measure(swap, controller, 0.5, 4.0);
    const auto held_before = controller.heldMicroseconds(pid);
    const Rate smooth = measure(swap, controller, 2.0, 4.0);
    const double smooth_hold = (controller.heldMicroseconds(pid) - held_before) / 1000.0 / (smooth.fps * 2.0);
    controller.apply(pid, 60, true, frame_limiter::mode_adaptive);
    measure(swap, controller, 1.0, 4.0);
    const auto adaptive_before = controller.heldMicroseconds(pid);
    const Rate adaptive = measure(swap, controller, 2.0, 4.0);
    const double adaptive_hold = (controller.heldMicroseconds(pid) - adaptive_before) / 1000.0 / (adaptive.fps * 2.0);
    std::printf("smooth: %.1f fps, deviation %.2f ms, held %.2f ms per frame\n", smooth.fps, smooth.deviation_ms, smooth_hold);
    std::printf("adaptive: %.1f fps, deviation %.2f ms, held %.2f ms per frame\n", adaptive.fps, adaptive.deviation_ms, adaptive_hold);
    if (std::abs(smooth.fps - 60) > 1.5 || std::abs(adaptive.fps - 60) > 1.5) return fail(35, "rate with a game's own work");
    if (smooth_hold < 9) return fail(36, "smooth mode should hold a frame for most of its slot");
    if (adaptive_hold > 3 || adaptive_hold > smooth_hold / 3) return fail(37, "adaptive mode still holds frames");
    if (adaptive.deviation_ms > 3) return fail(38, "adaptive pacing uneven");

    controller.apply(pid, 0, true);
    const Rate released = measure(swap, controller, 1.0);
    std::printf("off: %.0f fps\n", released.fps);
    if (released.fps < 200) return fail(34, "limit not released");
    std::printf("frame_limiter_tests: ok\n");
    return 0;
}
