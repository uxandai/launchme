// The same COM-shaped entry points the application uses, including compressed uploads,
// render-target snapshots and a target surviving destruction of its window.
#include "linux/gfx/d2d_internal.hpp"
#include "linux/window/window_system.hpp"
#include "artwork/block_compression.hpp"
#include <d2d1_2helper.h>
#include <cstdlib>
#include <iostream>

using Microsoft::WRL::ComPtr;
int main(int argc, char** argv) {
    const std::string transport = argc > 1 ? argv[1] : "gl";
    setenv("LAUNCHME_RENDERER", transport.c_str(), 1);
    setenv("LAUNCHME_IMPELLER", "1", 1);
    std::string error;
    if (!lm_window::start(error)) { std::cerr << error << '\n'; return 77; }
    struct Window {
        HWND handle = nullptr;
        ~Window() { if (handle) DestroyWindow(handle); lm_window::stop(); }
    } window;
    WNDCLASSEXW type{sizeof(type)};
    type.lpfnWndProc = DefWindowProcW;
    type.lpszClassName = L"LaunchmeImpellerTest";
    if (!RegisterClassExW(&type)) return 1;
    window.handle = CreateWindowExW(0, type.lpszClassName, L"Impeller D2D test", WS_POPUP, 0, 0, 64, 64, nullptr, nullptr, nullptr, nullptr);
    if (!window.handle) return 2;
    ShowWindow(window.handle, SW_SHOW);
    ComPtr<ID2D1Factory> factory;
    ComPtr<ID2D1HwndRenderTarget> target;
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, factory.GetAddressOf())) ||
        FAILED(factory->CreateHwndRenderTarget(D2D1::RenderTargetProperties(),
            D2D1::HwndRenderTargetProperties(window.handle, D2D1::SizeU(64, 64)), target.GetAddressOf()))) return 3;
    target->BeginDraw();
    if (!lm_impeller::active()) { std::cerr << "Impeller did not initialize\n"; return 77; }
    target->Clear(D2D1::ColorF(0, 0, 0));
    ComPtr<ID2D1DeviceContext1> context;
    if (FAILED(target.As(&context))) return 4;
    const bool bc = lm_impeller::supportsCompression(1);
    ComPtr<ID2D1Bitmap> cover, logo;
    if (bc) {
        const uint8_t red[]{0, 0xf8, 0, 0, 0, 0, 0, 0};
        if (!context->IsDxgiFormatSupported(DXGI_FORMAT_BC1_UNORM) || !context->IsDxgiFormatSupported(DXGI_FORMAT_BC3_UNORM)) return 5;
        if (FAILED(target->CreateBitmap({4, 4}, red, 8,
            D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_BC1_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)), cover.GetAddressOf()))) return 6;
        std::vector<uint8_t> alpha(64);
        for (size_t i = 0; i < alpha.size(); i += 4) { alpha[i + 2] = 128; alpha[i + 3] = 128; }
        const auto blocks = block_compression::encodeBc3(alpha.data(), 4, 4);
        if (FAILED(target->CreateBitmap({4, 4}, blocks.data(), 16,
            D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_BC3_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)), logo.GetAddressOf()))) return 7;
        target->DrawBitmap(cover.Get(), D2D1::RectF(0, 0, 16, 16));
        target->DrawBitmap(logo.Get(), D2D1::RectF(16, 0, 32, 16));
    }
    ComPtr<ID2D1SolidColorBrush> green;
    if (FAILED(target->CreateSolidColorBrush(D2D1::ColorF(0, 1, 0), green.GetAddressOf()))) return 8;
    target->PushAxisAlignedClip(D2D1::RectF(32, 0, 48, 16), D2D1_ANTIALIAS_MODE_ALIASED);
    target->FillRectangle(D2D1::RectF(32, 0, 64, 64), green.Get());
    target->PopAxisAlignedClip();
    if (FAILED(target->EndDraw())) return 9;
    ComPtr<ID2D1Bitmap> copy;
    if (FAILED(target->CreateBitmap({64, 64}, nullptr, 0,
        D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)), copy.GetAddressOf())) ||
        FAILED(copy->CopyFromRenderTarget(nullptr, target.Get(), nullptr))) return 10;
    auto image = lm_d2d::Bitmap::from(copy.Get())->image(nullptr);
    std::vector<uint8_t> pixels(64 * 64 * 4);
    if (!image || !image->readPixels(nullptr, lm_d2d::bgraInfo(64, 64), pixels.data(), 64 * 4, 0, 0)) return 11;
    const auto pixel = [&](int x, int y, int channel) { return pixels[(y * 64 + x) * 4 + channel]; };
    if (bc && (pixel(8, 8, 2) < 240 || pixel(24, 8, 2) < 110 || pixel(24, 8, 2) > 145)) return 12;
    if (pixel(40, 8, 1) < 240 || pixel(55, 8, 1) != 0 || pixel(40, 25, 1) != 0) return 13;
    DestroyWindow(window.handle); window.handle = nullptr;
    target->BeginDraw();
    if (target->EndDraw() != D2DERR_RECREATE_TARGET) return 14;
    std::cout << "D2D compressed artwork, clips, GPU copy/readback and destroyed-window lifetime passed (BC=" << bc << ")\n";
}
