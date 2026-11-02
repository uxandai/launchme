// launchme_limiter.dll: loaded into a game on the player's request (Ustawienia -> FPS i limiter).
// It wraps IDXGISwapChain::Present and Present1, which every Direct3D 10/11/12 game goes through,
// and holds each frame back until its slot in the limit has come. Nothing else of the game is touched.
// x64 only; Vulkan, OpenGL and Direct3D 9 games do not pass through DXGI and are left alone.
#include <Windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <atomic>
#include "../platform/frame_limiter_shared.hpp"

namespace {
using PresentFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
using Present1Fn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain1*, UINT, UINT, const DXGI_PRESENT_PARAMETERS*);
constexpr int present_slot = 8, present1_slot = 22;

PresentFn original_present = nullptr;
Present1Fn original_present1 = nullptr;
frame_limiter::Shared* shared = nullptr;
HANDLE mapping = nullptr, timer = nullptr;
int64_t frequency = 1;
std::atomic<int64_t> previous_release{0};
thread_local int64_t last_return = 0, current_release = 0; // Per rendering thread.
thread_local double cost_estimate = 0;                      // Ticks the game needs between two Presents: quick up, slow down.

int64_t now() { LARGE_INTEGER value; QueryPerformanceCounter(&value); return value.QuadPart; }

void waitUntil(int64_t target, bool precise) {
    // A high-resolution timer sleeps to within about half a millisecond; the spin covers the rest.
    const int64_t margin = frequency * (precise ? 1500 : 0) / 1000000;
    const int64_t sleep_to = target - margin;
    for (int64_t current = now(); current < sleep_to; current = now()) {
        const int64_t remaining = sleep_to - current;
        if (timer) {
            LARGE_INTEGER due;
            due.QuadPart = -(remaining * 10000000 / frequency); // 100 ns units, relative.
            if (due.QuadPart == 0) break;
            SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE);
            WaitForSingleObject(timer, INFINITE);
        } else {
            Sleep(1);
        }
    }
    if (precise) while (now() < target) YieldProcessor();
}

int64_t slotInterval() {
    const uint32_t limit = shared->limit_x100;
    return static_cast<int64_t>(static_cast<double>(frequency) * 100.0 / limit);
}

// Before the real Present: the frame waits for its slot. True when the limiter is on for this call.
bool beforePresent(UINT flags) {
    auto* const state = shared;
    if (!state || (flags & DXGI_PRESENT_TEST)) return false;
    state->presents = state->presents + 1;
    const uint32_t limit = state->limit_x100;
    if (!limit || GetTickCount() - state->heartbeat > 5000) { previous_release = 0; last_return = 0; return false; }
    const int64_t interval = slotInterval();
    const int64_t current = now();
    if (last_return) {
        const double cost = static_cast<double>(current - last_return);
        cost_estimate = cost_estimate == 0 || cost > cost_estimate ? cost : cost_estimate + (cost - cost_estimate) / 8;
    }
    const int64_t previous = previous_release.load();
    const int64_t target = previous ? previous + interval : current;
    int64_t release = current;
    if (current < target) {
        waitUntil(target, state->precise != 0);
        release = target;
        state->held_us = state->held_us + static_cast<uint64_t>((target - current) * 1000000 / frequency);
    }
    previous_release = release; // The ideal slot, not the late wake-up: the rate does not drift.
    current_release = release;
    return true;
}

// After it: the adaptive mode lets the game's next frame start late, so that it is ready just as its slot comes.
void afterPresent(bool limited) {
    if (limited && shared->mode == frame_limiter::mode_adaptive && cost_estimate > 0) {
        const int64_t next_slot = current_release + slotInterval();
        const int64_t margin = frequency / 2000; // 0.5 ms.
        const int64_t start = next_slot - static_cast<int64_t>(cost_estimate * 1.2) - margin;
        if (start > now()) waitUntil(start, shared->precise != 0);
    }
    last_return = limited ? now() : 0;
}

HRESULT STDMETHODCALLTYPE hookedPresent(IDXGISwapChain* chain, UINT interval, UINT flags) {
    const bool limited = beforePresent(flags);
    const HRESULT result = original_present(chain, interval, flags);
    afterPresent(limited);
    return result;
}
HRESULT STDMETHODCALLTYPE hookedPresent1(IDXGISwapChain1* chain, UINT interval, UINT flags, const DXGI_PRESENT_PARAMETERS* parameters) {
    const bool limited = beforePresent(flags);
    const HRESULT result = original_present1(chain, interval, flags, parameters);
    afterPresent(limited);
    return result;
}

template <class Fn> bool patch(void** table, int slot, Fn hook, Fn& original) {
    void** entry = table + slot;
    DWORD old = 0;
    if (!VirtualProtect(entry, sizeof(void*), PAGE_READWRITE, &old)) return false;
    original = reinterpret_cast<Fn>(*entry);
    *entry = reinterpret_cast<void*>(hook);
    VirtualProtect(entry, sizeof(void*), old, &old);
    return true;
}

bool install() {
    // A throwaway swap chain only to find where this process's DXGI keeps its Present: the table is
    // shared by every swap chain of that implementation.
    const HWND window = CreateWindowExW(0, L"STATIC", L"launchme limiter", WS_POPUP, 0, 0, 64, 64, nullptr, nullptr, nullptr, nullptr);
    if (!window) return false;
    DXGI_SWAP_CHAIN_DESC description{};
    description.BufferDesc.Width = 64;
    description.BufferDesc.Height = 64;
    description.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    description.SampleDesc.Count = 1;
    description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    description.BufferCount = 1;
    description.OutputWindow = window;
    description.Windowed = TRUE;
    IDXGISwapChain* chain = nullptr;
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    HRESULT result = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION,
        &description, &chain, &device, nullptr, &context);
    if (FAILED(result))
        result = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION,
            &description, &chain, &device, nullptr, &context);
    bool ok = false;
    if (SUCCEEDED(result) && chain) {
        void** table = *reinterpret_cast<void***>(chain);
        ok = patch(table, present_slot, hookedPresent, original_present);
        IDXGISwapChain1* chain1 = nullptr;
        if (ok && SUCCEEDED(chain->QueryInterface(__uuidof(IDXGISwapChain1), reinterpret_cast<void**>(&chain1))) && chain1) {
            void** table1 = *reinterpret_cast<void***>(chain1);
            if (table1 == table) patch(table1, present1_slot, hookedPresent1, original_present1);
            chain1->Release();
        }
    }
    if (chain) chain->Release();
    if (context) context->Release();
    if (device) device->Release();
    DestroyWindow(window);
    return ok;
}

DWORD WINAPI installThread(void*) {
    LARGE_INTEGER value;
    QueryPerformanceFrequency(&value);
    frequency = value.QuadPart;
    mapping = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, frame_limiter::mappingName(GetCurrentProcessId()).c_str());
    if (!mapping) return 0;
    shared = static_cast<frame_limiter::Shared*>(MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(frame_limiter::Shared)));
    if (!shared || shared->magic != frame_limiter::shared_magic) { shared = nullptr; return 0; }
    timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    if (!timer) timer = CreateWaitableTimerW(nullptr, FALSE, nullptr);
    shared->hooked = install() ? frame_limiter::hooked_ok : frame_limiter::hooked_failed;
    return 0;
}
}

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        // Not under the loader lock: the work needs Direct3D, which loads more libraries.
        if (const HANDLE thread = CreateThread(nullptr, 0, installThread, nullptr, 0, nullptr)) CloseHandle(thread);
    }
    return TRUE;
}
