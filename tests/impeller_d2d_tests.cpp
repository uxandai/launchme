// The same COM-shaped entry points the application uses, including compressed uploads,
// render-target snapshots and a target surviving destruction of its window.
#include "linux/gfx/d2d_internal.hpp"
#include "linux/window/window_system.hpp"
#include "artwork/block_compression.hpp"
#include <d2d1_2helper.h>
#include <cstdlib>
#include <cmath>
#include <iostream>

using Microsoft::WRL::ComPtr;
int main(int argc, char** argv) {
    const std::string transport = argc > 1 ? argv[1] : "gl";
    setenv("LAUNCHME_RENDERER", transport.c_str(), 1);
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
    // Plain premultiplied BGRA with alpha, as the companion's pictures are: drawn directly, through the device
    // context's cubic mode, and at partial alpha. Blue and red must keep their places.
    const uint8_t solid_pixels[16]{30, 100, 200, 255, 30, 100, 200, 255, 30, 100, 200, 255, 30, 100, 200, 255};            // B G R A
    const uint8_t soft_pixels[16]{15, 50, 100, 128, 15, 50, 100, 128, 15, 50, 100, 128, 15, 50, 100, 128};
    ComPtr<ID2D1Bitmap> solid_bitmap, soft_bitmap;
    const auto bgra = D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    if (FAILED(target->CreateBitmap({2, 2}, solid_pixels, 8, bgra, solid_bitmap.GetAddressOf())) ||
        FAILED(target->CreateBitmap({2, 2}, soft_pixels, 8, bgra, soft_bitmap.GetAddressOf()))) return 20;
    target->DrawBitmap(solid_bitmap.Get(), D2D1::RectF(0, 32, 16, 48));
    context->DrawBitmap(solid_bitmap.Get(), D2D1::RectF(16, 32, 32, 48), 1.0f, D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC, nullptr, nullptr);
    context->DrawBitmap(soft_bitmap.Get(), D2D1::RectF(32, 32, 48, 48), 1.0f, D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC, nullptr, nullptr);
    target->DrawBitmap(solid_bitmap.Get(), D2D1::RectF(48, 32, 64, 48), 0.5f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    if (FAILED(target->EndDraw())) return 9;
    ComPtr<ID2D1Bitmap> copy;
    if (FAILED(target->CreateBitmap({64, 64}, nullptr, 0,
        D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)), copy.GetAddressOf())) ||
        FAILED(copy->CopyFromRenderTarget(nullptr, target.Get(), nullptr))) return 10;
    auto image = lm_d2d::Bitmap::from(copy.Get())->image();
    const auto bytes = image ? image->pixels() : nullptr;
    if (!bytes || bytes->size() != 64 * 64 * 4) return 11;
    // BGRA: channel 0 is blue, 1 green, 2 red.
    const auto pixel = [&](int x, int y, int channel) { return (*bytes)[(y * 64 + x) * 4 + channel]; };
    if (bc && (pixel(8, 8, 2) < 240 || pixel(24, 8, 2) < 110 || pixel(24, 8, 2) > 145)) return 12;
    if (pixel(40, 8, 1) < 240 || pixel(55, 8, 1) != 0 || pixel(40, 25, 1) != 0) return 13;
    const auto near = [&](int x, int y, int channel, int expected) { return std::abs(int{pixel(x, y, channel)} - expected) <= 6; };
    // Opaque 200,100,30 (R,G,B): plain, cubic; the half-alpha one is 100,50,15 over black; 0.5 opacity halves the solid one.
    if (!near(8, 40, 2, 200) || !near(8, 40, 1, 100) || !near(8, 40, 0, 30)) return 21;
    if (!near(24, 40, 2, 200) || !near(24, 40, 1, 100) || !near(24, 40, 0, 30)) return 22;
    if (!near(40, 40, 2, 100) || !near(40, 40, 1, 50) || !near(40, 40, 0, 15)) return 23;
    if (!near(56, 40, 2, 100) || !near(56, 40, 1, 50) || !near(56, 40, 0, 15)) return 24;
    DestroyWindow(window.handle); window.handle = nullptr;
    target->BeginDraw();
    if (target->EndDraw() != D2DERR_RECREATE_TARGET) return 14;
    std::cout << "D2D compressed artwork, clips, GPU copy/readback and destroyed-window lifetime passed (BC=" << bc << ")\n";
}
