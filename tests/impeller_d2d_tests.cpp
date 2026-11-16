// The same COM-shaped entry points the application uses, including the effect shaders,
// render-target snapshots and a target surviving destruction of its window.
#include "linux/gfx/d2d_internal.hpp"
#include "linux/window/window_system.hpp"
#include <d2d1_2helper.h>
#include <cstdlib>
#include <cmath>
#include <iostream>

using Microsoft::WRL::ComPtr;

namespace {
// The effects the effect shaders run (the steelbook's lighting chain), checked against the maths:
// an 8x8 grid, inputs both uploaded and rendered (OpenGL ES keeps render results upside down).
int effectShaders(ID2D1RenderTarget* target) {
    const D2D1_SIZE_F size{8, 8};
    const D2D1_SIZE_U pixels{8, 8};
    // Rendered: red over blue.
    ComPtr<ID2D1BitmapRenderTarget> painter;
    if (FAILED(target->CreateCompatibleRenderTarget(&size, &pixels, nullptr, D2D1_COMPATIBLE_RENDER_TARGET_OPTIONS_NONE, painter.GetAddressOf())))
        return 30;
    ComPtr<ID2D1SolidColorBrush> red, blue;
    painter->BeginDraw();
    painter->CreateSolidColorBrush(D2D1::ColorF(1, 0, 0), red.GetAddressOf());
    painter->CreateSolidColorBrush(D2D1::ColorF(0, 0, 1), blue.GetAddressOf());
    painter->FillRectangle(D2D1::RectF(0, 0, 8, 4), red.Get());
    painter->FillRectangle(D2D1::RectF(0, 4, 8, 8), blue.Get());
    if (FAILED(painter->EndDraw())) return 31;
    ComPtr<ID2D1Bitmap> rendered;
    painter->GetBitmap(rendered.GetAddressOf());
    // Uploaded: opaque white, and grey at half alpha (premultiplied 128 of 128).
    std::vector<uint8_t> white(8 * 8 * 4, 255), grey(8 * 8 * 4, 128);
    const auto bgra = D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    ComPtr<ID2D1Bitmap> flat, half;
    if (FAILED(target->CreateBitmap(pixels, white.data(), 32, bgra, flat.GetAddressOf())) ||
        FAILED(target->CreateBitmap(pixels, grey.data(), 32, bgra, half.GetAddressOf()))) return 32;

    ComPtr<ID2D1DeviceContext> context;
    if (FAILED(painter.As(&context))) return 33;
    // An effect drawn into a fresh 8x8 target; its pixels (BGRA) read back.
    const auto evaluate = [&](ID2D1Effect* effect) -> std::vector<uint8_t> {
        ComPtr<ID2D1BitmapRenderTarget> into;
        ComPtr<ID2D1DeviceContext> drawing;
        if (FAILED(target->CreateCompatibleRenderTarget(&size, &pixels, nullptr, D2D1_COMPATIBLE_RENDER_TARGET_OPTIONS_NONE, into.GetAddressOf())) ||
            FAILED(into.As(&drawing))) return {};
        drawing->BeginDraw();
        drawing->Clear(D2D1::ColorF(0, 0, 0, 0));
        drawing->DrawImage(effect);
        if (FAILED(drawing->EndDraw())) return {};
        ComPtr<ID2D1Bitmap> result;
        into->GetBitmap(result.GetAddressOf());
        const auto image = lm_d2d::Bitmap::from(result.Get())->image();
        const auto bytes = image ? image->pixels() : nullptr;
        return bytes ? *bytes : std::vector<uint8_t>{};
    };
    const auto at = [](const std::vector<uint8_t>& p, int x, int y, int channel) { return int{p[(y * 8 + x) * 4 + channel]}; };
    const auto close = [](int value, int expected) { return std::abs(value - expected) <= 8; };

    // Colour matrix, straight mode (the premultiplied values as they are), identity: the picture stays upright.
    ComPtr<ID2D1Effect> matrix;
    if (FAILED(context->CreateEffect(CLSID_D2D1ColorMatrix, matrix.GetAddressOf()))) return 34;
    matrix->SetInput(0, rendered.Get());
    matrix->SetValue(D2D1_COLORMATRIX_PROP_ALPHA_MODE, D2D1_COLORMATRIX_ALPHA_MODE_STRAIGHT);
    auto out = evaluate(matrix.Get());
    if (out.size() != 256) return 35;
    if (!close(at(out, 3, 1, 2), 255) || !close(at(out, 3, 1, 0), 0) || !close(at(out, 3, 6, 0), 255) || !close(at(out, 3, 6, 2), 0)) {
        std::cerr << "matrix top " << at(out, 3, 1, 2) << "," << at(out, 3, 1, 0) << " bottom " << at(out, 3, 6, 2) << "," << at(out, 3, 6, 0) << '\n';
        return 36;
    }

    // The same for an uploaded picture (red over blue in memory).
    std::vector<uint8_t> halves(8 * 8 * 4, 0);
    for (int i = 0; i < 64; ++i) { halves[i * 4 + (i < 32 ? 2 : 0)] = 255; halves[i * 4 + 3] = 255; }
    ComPtr<ID2D1Bitmap> uploaded;
    if (FAILED(target->CreateBitmap(pixels, halves.data(), 32, bgra, uploaded.GetAddressOf()))) return 43;
    // An effect given another input computes again (its version once collided and kept the old result).
    matrix->SetInput(0, uploaded.Get());
    out = evaluate(matrix.Get());
    if (out.size() != 256 || !close(at(out, 3, 1, 2), 255) || !close(at(out, 3, 6, 0), 255)) {
        if (out.size() == 256) std::cerr << "new input top " << at(out, 3, 1, 2) << " bottom " << at(out, 3, 6, 0) << std::endl;
        return 47;
    }
    ComPtr<ID2D1Effect> upright;
    if (FAILED(context->CreateEffect(CLSID_D2D1ColorMatrix, upright.GetAddressOf()))) return 34;
    upright->SetInput(0, uploaded.Get());
    upright->SetValue(D2D1_COLORMATRIX_PROP_ALPHA_MODE, D2D1_COLORMATRIX_ALPHA_MODE_STRAIGHT);
    out = evaluate(upright.Get());
    if (out.size() != 256 || !close(at(out, 3, 1, 2), 255) || !close(at(out, 3, 6, 0), 255) || !close(at(out, 3, 6, 2), 0)) {
        if (out.size() == 256) std::cerr << "uploaded matrix top " << at(out, 3, 1, 2) << "," << at(out, 3, 1, 0) << " bottom " << at(out, 3, 6, 2) << "," << at(out, 3, 6, 0) << std::endl;
        return 44;
    }

    // And back and forth.
    upright->SetInput(0, rendered.Get());
    out = evaluate(upright.Get());
    if (out.size() != 256 || !close(at(out, 3, 1, 2), 255) || !close(at(out, 3, 6, 0), 255)) {
        if (out.size() == 256) std::cerr << "reused top " << at(out, 3, 1, 2) << " bottom " << at(out, 3, 6, 0) << std::endl;
        return 45;
    }

    upright->SetInput(0, uploaded.Get());
    out = evaluate(upright.Get());
    if (out.size() != 256 || !close(at(out, 3, 1, 2), 255) || !close(at(out, 3, 6, 0), 255)) {
        if (out.size() == 256) std::cerr << "input again top " << at(out, 3, 1, 2) << " bottom " << at(out, 3, 6, 0) << std::endl;
        return 46;
    }


    // Arithmetic composite k1*s*d: the rendered red/blue (d) times the uploaded half grey (s).
    ComPtr<ID2D1Effect> arithmetic;
    if (FAILED(context->CreateEffect(CLSID_D2D1ArithmeticComposite, arithmetic.GetAddressOf()))) return 37;
    arithmetic->SetInput(0, rendered.Get());
    arithmetic->SetInput(1, half.Get());
    arithmetic->SetValue(D2D1_ARITHMETICCOMPOSITE_PROP_COEFFICIENTS, D2D1_VECTOR_4F{1, 0, 0, 0});
    out = evaluate(arithmetic.Get());
    if (out.size() != 256) return 38;
    if (!close(at(out, 2, 1, 2), 128) || !close(at(out, 2, 1, 3), 128) || !close(at(out, 2, 1, 0), 0) || !close(at(out, 2, 6, 0), 128)) return 39;

    // Point specular over a flat surface: a light 1 px above it over the centre shines fully there
    // and at 0.766 (the half vector's z) in the corner.
    ComPtr<ID2D1Effect> specular;
    if (FAILED(context->CreateEffect(CLSID_D2D1PointSpecular, specular.GetAddressOf()))) return 40;
    specular->SetInput(0, flat.Get());
    specular->SetValue(D2D1_POINTSPECULAR_PROP_LIGHT_POSITION, D2D1_VECTOR_3F{4, 4, 2});
    specular->SetValue(D2D1_POINTSPECULAR_PROP_SPECULAR_EXPONENT, 1.0f);
    specular->SetValue(D2D1_POINTSPECULAR_PROP_SPECULAR_CONSTANT, 1.0f);
    specular->SetValue(D2D1_POINTSPECULAR_PROP_SURFACE_SCALE, 1.0f);
    out = evaluate(specular.Get());
    if (out.size() != 256) return 41;
    if (!close(at(out, 4, 4, 2), 255) || !close(at(out, 4, 4, 3), 255) || !close(at(out, 0, 0, 1), 195) || !close(at(out, 0, 0, 3), 195)) {
        std::cerr << "specular centre " << at(out, 4, 4, 2) << " corner " << at(out, 0, 0, 1) << '\n';
        return 42;
    }
    return 0;
}
} // namespace

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
    // Block compression stays off on Linux: the launcher then keeps its artwork as BGRA.
    if (context->IsDxgiFormatSupported(DXGI_FORMAT_BC1_UNORM) || context->IsDxgiFormatSupported(DXGI_FORMAT_BC3_UNORM)) return 5;
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
    if (pixel(40, 8, 1) < 240 || pixel(55, 8, 1) != 0 || pixel(40, 25, 1) != 0) return 13;
    const auto near = [&](int x, int y, int channel, int expected) { return std::abs(int{pixel(x, y, channel)} - expected) <= 6; };
    // Opaque 200,100,30 (R,G,B): plain, cubic; the half-alpha one is 100,50,15 over black; 0.5 opacity halves the solid one.
    if (!near(8, 40, 2, 200) || !near(8, 40, 1, 100) || !near(8, 40, 0, 30)) return 21;
    if (!near(24, 40, 2, 200) || !near(24, 40, 1, 100) || !near(24, 40, 0, 30)) return 22;
    if (!near(40, 40, 2, 100) || !near(40, 40, 1, 50) || !near(40, 40, 0, 15)) return 23;
    if (!near(56, 40, 2, 100) || !near(56, 40, 1, 50) || !near(56, 40, 0, 15)) return 24;
    if (const int failed = effectShaders(target.Get())) return failed;
    DestroyWindow(window.handle); window.handle = nullptr;
    target->BeginDraw();
    if (target->EndDraw() != D2DERR_RECREATE_TARGET) return 14;
    std::cout << "D2D clips, GPU copy/readback and destroyed-window lifetime passed\n";
}
