#pragma once
// The window's Direct2D target on Windows (Renderer::createTarget): a Direct3D 11 device the
// renderer owns, a flip-model swap chain (DXGI_SWAP_EFFECT_FLIP_DISCARD) and a device context
// drawing into its back buffer, instead of ID2D1HwndRenderTarget's blt-model presentation.
// - Full screen (Tryb TV), DWM can show the buffer as it is (independent flip): no composition
//   copy, one frame less of latency, less GPU and power.
// - Frame latency 1 on a waitable object: a frame is drawn only once the previous one is on its
//   way, so input reaches the screen with the next refresh.
// - The device is the renderer's own: its caches can be trimmed (behind a game), Direct2D's
//   texture cache bounded, and video frames decoded straight into textures on it.
// The layered Guide overlay and UI-test captures keep their own targets; Linux has none of this
// (src/linux implements ID2D1HwndRenderTarget on Impeller).
#ifdef _WIN32
#include <d2d1_1.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

class SwapChainTarget {
public:
    SwapChainTarget() = default;
    ~SwapChainTarget() { reset(); }
    SwapChainTarget(const SwapChainTarget&) = delete;
    SwapChainTarget& operator=(const SwapChainTarget&) = delete;

    // A device (hardware, else WARP), the swap chain for `window` and a device context drawing into
    // its back buffer at 96 DPI (one DIP a pixel, as the renderer's HWND target). `tearing`: allow
    // DXGI_PRESENT_ALLOW_TEARING where the system supports it (the uncapped benchmark only).
    bool create(ID2D1Factory* factory, HWND window, UINT width, UINT height, bool tearing);
    void reset();
    explicit operator bool() const { return context_ != nullptr; }
    // The new client size: the back buffer follows (its old bitmap is let go first).
    bool resize(UINT width, UINT height);
    // Before drawing a frame: until the swap chain takes another one (frame latency 1), at most
    // `timeout_ms` (a hung present never stops the window). Three timeouts in a row and this swap
    // chain is not waited for again (a driver that never signals must not cost every frame).
    void waitForFrame(DWORD timeout_ms);
    // After EndDraw. `immediate`: without waiting for the display's refresh (the frame clock paces
    // frames; DWM shows the newest at its next refresh). S_OK, or the device is gone (discard and
    // build everything again).
    HRESULT present(bool immediate);
    ID2D1DeviceContext* context() const { return context_.Get(); }
    ID2D1Device* device() const { return device2d_.Get(); }
    ID3D11Device* device3d() const { return device3d_.Get(); }
    // Video frames decoded on this device (Media Foundation's threads): Direct3D's own locking on.
    void protectForVideo();
    // Behind a game: Direct2D's cached textures and the driver's temporary allocations go back.
    void trim();
    // Direct2D's cache of intermediate textures (effects, layers, glyph atlases) is purged past this.
    void limitTextureMemory(UINT64 bytes);

private:
    bool bindBackBuffer();
    Microsoft::WRL::ComPtr<ID3D11Device> device3d_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> immediate_;
    Microsoft::WRL::ComPtr<ID2D1Device> device2d_;
    Microsoft::WRL::ComPtr<ID2D1DeviceContext> context_;
    Microsoft::WRL::ComPtr<IDXGISwapChain1> swap_chain_;
    Microsoft::WRL::ComPtr<ID2D1Bitmap1> back_buffer_;
    HANDLE frame_latency_ = nullptr;
    int missed_waits_ = 0;
    UINT flags_ = 0;
    bool tearing_ = false, protected_ = false;
};
#endif
