// Smoke test of the Direct2D port (d2d_*.cpp): draws scenes through the Direct2D API only and
// writes each finished frame as a PNG (argv[1], default ".") to be looked at, plus a few
// measurements that must hold. The window layer and DirectWrite are stubbed below: a window
// target's frame goes straight to its PNG, text draws nothing.
#include "../gfx_internal.hpp"

#include <d2d1_2.h>
#include <d2d1_2helper.h>
#include <wrl/client.h>

#include <cairo.h>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace {
std::string output_directory = ".";
constexpr UINT32 width = 640, height = 400;
int failures = 0;

void check(bool condition, const char* what) {
    std::printf("%s: %s\n", condition ? "ok  " : "FAIL", what);
    if (!condition) ++failures;
}
} // namespace

namespace lm_gfx {
// The scene's name stands in for the window.
void windowPixelSize(HWND, UINT32& w, UINT32& h) { w = width; h = height; }
bool presentWindow(HWND window, const uint8_t* pixels, UINT32 w, UINT32 h, UINT32 stride) {
    cairo_surface_t* frame = cairo_image_surface_create_for_data(const_cast<uint8_t*>(pixels), CAIRO_FORMAT_ARGB32,
                                                                 static_cast<int>(w), static_cast<int>(h), static_cast<int>(stride));
    const std::string file = output_directory + "/" + static_cast<const char*>(window) + ".png";
    const bool written = cairo_surface_write_to_png(frame, file.c_str()) == CAIRO_STATUS_SUCCESS;
    cairo_surface_destroy(frame);
    std::printf("wrote %s\n", file.c_str());
    return written;
}
void drawTextLayout(cairo_t*, IDWriteTextLayout*, float, float) {}
void drawText(cairo_t*, const wchar_t*, UINT32, IDWriteTextFormat*, float, float, float, float, bool) {}
} // namespace lm_gfx

namespace {
struct Scene {
    ComPtr<ID2D1Factory> factory;
    ComPtr<ID2D1HwndRenderTarget> window;
    ComPtr<ID2D1RenderTarget> target;
    ComPtr<ID2D1DeviceContext1> context;
    ComPtr<ID2D1SolidColorBrush> solid;

    Scene(ID2D1Factory* f, const char* name) : factory(f) {
        factory->CreateHwndRenderTarget(D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,
                                            D2D1::PixelFormat(DXGI_FORMAT_UNKNOWN, D2D1_ALPHA_MODE_UNKNOWN), 96, 96),
                                        D2D1::HwndRenderTargetProperties(const_cast<char*>(name), D2D1::SizeU(width, height)),
                                        window.GetAddressOf());
        window->QueryInterface(IID_PPV_ARGS(target.GetAddressOf()));
        target->QueryInterface(IID_PPV_ARGS(context.GetAddressOf()));
        target->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1), solid.GetAddressOf());
        target->BeginDraw();
        target->Clear(D2D1::ColorF(0.08f, 0.09f, 0.11f));
    }
    ~Scene() { check(SUCCEEDED(target->EndDraw()), "EndDraw"); }
    ID2D1SolidColorBrush* ink(D2D1_COLOR_F color) { solid->SetColor(color); return solid.Get(); }
    ComPtr<ID2D1GradientStopCollection> stops(std::vector<D2D1_GRADIENT_STOP> list, D2D1_EXTEND_MODE extend = D2D1_EXTEND_MODE_CLAMP) {
        ComPtr<ID2D1GradientStopCollection> collection;
        target->CreateGradientStopCollection(list.data(), static_cast<UINT32>(list.size()), D2D1_GAMMA_2_2, extend, collection.GetAddressOf());
        return collection;
    }
    ComPtr<ID2D1PathGeometry> path(const std::vector<D2D1_POINT_2F>& points, bool closed = true, D2D1_FILL_MODE mode = D2D1_FILL_MODE_ALTERNATE) {
        ComPtr<ID2D1PathGeometry> geometry;
        ComPtr<ID2D1GeometrySink> sink;
        factory->CreatePathGeometry(geometry.GetAddressOf());
        geometry->Open(sink.GetAddressOf());
        sink->SetFillMode(mode);
        sink->BeginFigure(points[0], closed ? D2D1_FIGURE_BEGIN_FILLED : D2D1_FIGURE_BEGIN_HOLLOW);
        sink->AddLines(points.data() + 1, static_cast<UINT32>(points.size() - 1));
        sink->EndFigure(closed ? D2D1_FIGURE_END_CLOSED : D2D1_FIGURE_END_OPEN);
        sink->Close();
        return geometry;
    }
    // A test picture: a colour wheel-ish gradient with a checker and a white frame.
    ComPtr<ID2D1Bitmap> picture(UINT32 w, UINT32 h, bool straight = false) {
        std::vector<uint8_t> pixels(static_cast<size_t>(w) * h * 4);
        for (UINT32 y = 0; y < h; ++y)
            for (UINT32 x = 0; x < w; ++x) {
                uint8_t* p = &pixels[(static_cast<size_t>(y) * w + x) * 4];
                const bool checker = ((x / 8) + (y / 8)) % 2;
                const bool frame = x < 2 || y < 2 || x >= w - 2 || y >= h - 2;
                p[0] = frame ? 255 : static_cast<uint8_t>(255 * y / h);
                p[1] = frame ? 255 : static_cast<uint8_t>(checker ? 200 : 60);
                p[2] = frame ? 255 : static_cast<uint8_t>(255 * x / w);
                p[3] = 255;
                if (straight && x > w / 2) p[3] = 128; // Straight colour: stays as bright at half alpha.
            }
        ComPtr<ID2D1Bitmap> bitmap;
        target->CreateBitmap(D2D1::SizeU(w, h), pixels.data(), w * 4,
                             D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                                                                      straight ? D2D1_ALPHA_MODE_STRAIGHT : D2D1_ALPHA_MODE_PREMULTIPLIED)),
                             bitmap.GetAddressOf());
        return bitmap;
    }
};

// Rounded rectangles with gradients, a radial gradient with an offset focus, strokes and styles.
void shapes(ID2D1Factory* factory) {
    Scene s(factory, "shapes");
    const auto vertical = s.stops({{0, D2D1::ColorF(0.95f, 0.45f, 0.20f)}, {1, D2D1::ColorF(0.30f, 0.10f, 0.60f)}});
    ComPtr<ID2D1LinearGradientBrush> linear;
    s.target->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties({20, 20}, {20, 180}), vertical.Get(), linear.GetAddressOf());
    s.target->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(20, 20, 200, 180), 24, 24), linear.Get());
    s.target->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(20, 20, 200, 180), 24, 24), s.ink(D2D1::ColorF(1, 1, 1, 0.6f)), 3);
    // Elliptical radial gradient, focus to the upper left, 70% opacity.
    const auto glow = s.stops({{0, D2D1::ColorF(1, 1, 0.8f)}, {0.5f, D2D1::ColorF(0.2f, 0.7f, 1)}, {1, D2D1::ColorF(0, 0.1f, 0.3f, 0)}});
    ComPtr<ID2D1RadialGradientBrush> radial;
    s.target->CreateRadialGradientBrush(D2D1::RadialGradientBrushProperties({320, 100}, {-30, -25}, 90, 60), glow.Get(), radial.GetAddressOf());
    radial->SetOpacity(0.7f);
    s.target->FillEllipse(D2D1::Ellipse({320, 100}, 95, 70), radial.Get());
    // A brush transform: the gradient turned 45 degrees around its centre, mirrored.
    const auto stripes = s.stops({{0, D2D1::ColorF(0.9f, 0.9f, 0.9f)}, {1, D2D1::ColorF(0.2f, 0.5f, 0.3f)}}, D2D1_EXTEND_MODE_MIRROR);
    ComPtr<ID2D1LinearGradientBrush> mirrored;
    s.target->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties({460, 100}, {480, 100}), stripes.Get(), mirrored.GetAddressOf());
    mirrored->SetTransform(D2D1::Matrix3x2F::Rotation(45, {500, 100}));
    s.target->FillRectangle(D2D1::RectF(430, 30, 610, 170), mirrored.Get());
    // Strokes: caps, joins and dashes; widths 1..8.
    ComPtr<ID2D1StrokeStyle> round, dashed;
    factory->CreateStrokeStyle(D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_LINE_JOIN_ROUND),
                               nullptr, 0, round.GetAddressOf());
    factory->CreateStrokeStyle(D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_FLAT, D2D1_CAP_STYLE_FLAT, D2D1_CAP_STYLE_ROUND, D2D1_LINE_JOIN_MITER,
                                                           10, D2D1_DASH_STYLE_DASH_DOT), nullptr, 0, dashed.GetAddressOf());
    for (int i = 0; i < 4; ++i) {
        const float x = 40.0f + i * 40, w = 1.0f + i * 2.3f;
        s.target->DrawLine({x, 220}, {x + 20, 370}, s.ink(D2D1::ColorF(0.9f, 0.9f, 0.3f)), w, i % 2 ? round.Get() : nullptr);
    }
    const auto zigzag = s.path({{220, 360}, {260, 230}, {300, 360}, {340, 230}}, false);
    s.target->DrawGeometry(zigzag.Get(), s.ink(D2D1::ColorF(0.4f, 0.9f, 0.6f)), 10, round.Get());
    s.target->DrawGeometry(zigzag.Get(), s.ink(D2D1::ColorF(0.1f, 0.1f, 0.1f)), 2);
    s.target->DrawRectangle(D2D1::RectF(380, 230, 610, 370), s.ink(D2D1::ColorF(1, 0.5f, 0.5f)), 4, dashed.Get());
    s.target->DrawEllipse(D2D1::Ellipse({495, 300}, 80, 45), s.ink(D2D1::ColorF(0.6f, 0.8f, 1, 0.8f)), 1.5f);
}

// Paths: BÄ‚Â©ziers, quadratics, arcs (rotated, both sweeps and sizes), fill modes, hollow figures.
void paths(ID2D1Factory* factory) {
    Scene s(factory, "paths");
    ComPtr<ID2D1PathGeometry> shape;
    ComPtr<ID2D1GeometrySink> sink;
    factory->CreatePathGeometry(shape.GetAddressOf());
    shape->Open(sink.GetAddressOf());
    sink->BeginFigure({40, 180}, D2D1_FIGURE_BEGIN_FILLED);
    sink->AddBezier(D2D1::BezierSegment({60, 40}, {160, 40}, {180, 120}));
    sink->AddQuadraticBezier(D2D1::QuadraticBezierSegment({200, 180}, {120, 190}));
    sink->AddArc(D2D1::ArcSegment({40, 180}, D2D1::SizeF(50, 30), 30, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    // Two circles from half arcs (controller_art's way) and an open arc.
    sink->BeginFigure({300, 100}, D2D1_FIGURE_BEGIN_FILLED);
    sink->AddArc(D2D1::ArcSegment({220, 100}, {40, 40}, 0, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
    sink->AddArc(D2D1::ArcSegment({300, 100}, {40, 40}, 0, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    sink->BeginFigure({250, 180}, D2D1_FIGURE_BEGIN_HOLLOW);
    sink->AddArc(D2D1::ArcSegment({330, 180}, {40, 40}, 0, D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE, D2D1_ARC_SIZE_LARGE));
    sink->EndFigure(D2D1_FIGURE_END_OPEN);
    sink->Close();
    s.target->FillGeometry(shape.Get(), s.ink(D2D1::ColorF(0.3f, 0.6f, 0.9f)));
    s.target->DrawGeometry(shape.Get(), s.ink(D2D1::ColorF(1, 1, 1)), 2);
    // A five-pointed star: hollow centre (alternate) beside a solid one (winding).
    std::vector<D2D1_POINT_2F> star;
    for (int i = 0; i < 5; ++i) {
        const float a = -1.5708f + i * 2 * 6.2832f / 5;
        star.push_back({70 * std::cos(a), 70 * std::sin(a)});
    }
    const auto even = s.path(star, true, D2D1_FILL_MODE_ALTERNATE), winding = s.path(star, true, D2D1_FILL_MODE_WINDING);
    s.target->SetTransform(D2D1::Matrix3x2F::Translation(110, 300));
    s.target->FillGeometry(even.Get(), s.ink(D2D1::ColorF(0.95f, 0.75f, 0.2f)));
    s.target->SetTransform(D2D1::Matrix3x2F::Translation(270, 300));
    s.target->FillGeometry(winding.Get(), s.ink(D2D1::ColorF(0.95f, 0.75f, 0.2f)));
    // A transformed geometry: its stroke keeps the world's width (2), not the 3x scale.
    s.target->SetTransform(D2D1::IdentityMatrix());
    ComPtr<ID2D1TransformedGeometry> big;
    factory->CreateTransformedGeometry(even.Get(), D2D1::Matrix3x2F::Scale(0.9f, 0.9f) * D2D1::Matrix3x2F::Translation(470, 300), big.GetAddressOf());
    s.target->DrawGeometry(big.Get(), s.ink(D2D1::ColorF(1, 0.4f, 0.4f)), 2);
    // A group: a frame with a hole (alternate), the tour's spotlight.
    ComPtr<ID2D1RectangleGeometry> outer;
    ComPtr<ID2D1EllipseGeometry> hole;
    factory->CreateRectangleGeometry(D2D1::RectF(400, 20, 620, 180), outer.GetAddressOf());
    factory->CreateEllipseGeometry(D2D1::Ellipse({510, 100}, 60, 45), hole.GetAddressOf());
    ID2D1Geometry* parts[] = {outer.Get(), hole.Get()};
    ComPtr<ID2D1GeometryGroup> group;
    factory->CreateGeometryGroup(D2D1_FILL_MODE_ALTERNATE, parts, 2, group.GetAddressOf());
    s.target->FillGeometry(group.Get(), s.ink(D2D1::ColorF(0.5f, 0.2f, 0.6f, 0.8f)));

    // Measurements.
    D2D1_RECT_F bounds{};
    outer->GetBounds(D2D1::Matrix3x2F::Scale(2, 2), &bounds);
    check(std::abs(bounds.left - 800) < 0.01f && std::abs(bounds.bottom - 360) < 0.01f, "GetBounds with a world transform");
    float length = 0;
    hole->ComputeLength(nullptr, &length);
    check(std::abs(length - 331.7f) < 1.0f, "ComputeLength of an ellipse (Ramanujan: 331.7)");
    D2D1_POINT_2F point{}, tangent{};
    outer->ComputePointAtLength(230, nullptr, &point, &tangent);
    check(std::abs(point.x - 620) < 0.01f && std::abs(point.y - 30) < 0.01f && std::abs(tangent.y - 1) < 0.01f, "ComputePointAtLength");
    BOOL inside = TRUE, outside = TRUE;
    group->FillContainsPoint({510, 100}, nullptr, D2D1_DEFAULT_FLATTENING_TOLERANCE, &inside);
    group->FillContainsPoint({410, 30}, nullptr, D2D1_DEFAULT_FLATTENING_TOLERANCE, &outside);
    check(!inside && outside, "FillContainsPoint (alternate group)");
    winding->FillContainsPoint({0, 0}, nullptr, D2D1_DEFAULT_FLATTENING_TOLERANCE, &inside);
    even->FillContainsPoint({0, 0}, nullptr, D2D1_DEFAULT_FLATTENING_TOLERANCE, &outside);
    check(inside && !outside, "FillContainsPoint (star centre, winding vs alternate)");
}

// Clips under rotation, layers with opacity, geometric masks and opacity brushes, nesting.
void layers(ID2D1Factory* factory) {
    Scene s(factory, "layers");
    // A rotated axis-aligned clip clips to the rotated rectangle's bounds.
    s.target->SetTransform(D2D1::Matrix3x2F::Rotation(30, {110, 110}));
    s.target->PushAxisAlignedClip(D2D1::RectF(60, 60, 160, 160), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    s.target->FillRectangle(D2D1::RectF(0, 0, 220, 220), s.ink(D2D1::ColorF(0.2f, 0.8f, 0.5f)));
    s.target->PopAxisAlignedClip();
    s.target->DrawRectangle(D2D1::RectF(60, 60, 160, 160), s.ink(D2D1::ColorF(1, 1, 1)), 1);
    s.target->SetTransform(D2D1::IdentityMatrix());
    // A layer at half opacity: the overlap of its two shapes does not show through.
    ComPtr<ID2D1Layer> layer;
    s.target->CreateLayer(layer.GetAddressOf());
    s.target->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), nullptr, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE, D2D1::IdentityMatrix(), 0.5f), layer.Get());
    s.target->FillEllipse(D2D1::Ellipse({280, 100}, 50, 50), s.ink(D2D1::ColorF(1, 0.3f, 0.3f)));
    s.target->FillEllipse(D2D1::Ellipse({330, 100}, 50, 50), s.ink(D2D1::ColorF(0.3f, 0.3f, 1)));
    s.target->PopLayer();
    // Geometric mask (a rotated ellipse through its mask transform) and a gradient opacity brush.
    ComPtr<ID2D1EllipseGeometry> oval;
    factory->CreateEllipseGeometry(D2D1::Ellipse({0, 0}, 90, 40), oval.GetAddressOf());
    const auto fade = s.stops({{0, D2D1::ColorF(0, 0, 0, 1)}, {1, D2D1::ColorF(0, 0, 0, 0)}});
    ComPtr<ID2D1LinearGradientBrush> mask;
    s.target->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties({420, 0}, {600, 0}), fade.Get(), mask.GetAddressOf());
    s.context->PushLayer(D2D1::LayerParameters1(D2D1::RectF(400, 0, 640, 200), oval.Get(), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
                                                D2D1::Matrix3x2F::Rotation(-20) * D2D1::Matrix3x2F::Translation(510, 100), 1, mask.Get()),
                         nullptr);
    s.target->Clear(D2D1::ColorF(0.9f, 0.85f, 0.3f));
    for (int i = 0; i < 6; ++i) s.target->FillRectangle(D2D1::RectF(400.0f + i * 40, 0, 420.0f + i * 40, 200), s.ink(D2D1::ColorF(0.2f, 0.2f, 0.7f)));
    s.target->PopLayer();
    // Nesting: a clip inside a masked layer inside a clip.
    s.target->PushAxisAlignedClip(D2D1::RectF(20, 230, 300, 380), D2D1_ANTIALIAS_MODE_ALIASED);
    ComPtr<ID2D1RoundedRectangleGeometry> card;
    factory->CreateRoundedRectangleGeometry(D2D1::RoundedRect(D2D1::RectF(40, 240, 280, 370), 30, 30), card.GetAddressOf());
    s.target->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), card.Get()), nullptr);
    s.target->FillRectangle(D2D1::RectF(0, 200, 640, 400), s.ink(D2D1::ColorF(0.3f, 0.3f, 0.35f)));
    s.target->PushAxisAlignedClip(D2D1::RectF(150, 200, 640, 400), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    s.target->FillEllipse(D2D1::Ellipse({160, 305}, 90, 90), s.ink(D2D1::ColorF(0.9f, 0.5f, 0.1f)));
    s.target->PopAxisAlignedClip();
    s.target->PopLayer();
    s.target->PopAxisAlignedClip();
    // Additive primitive blend: overlaps brighten.
    s.context->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_ADD);
    for (int i = 0; i < 3; ++i)
        s.target->FillEllipse(D2D1::Ellipse({430.0f + i * 50, 300}, 55, 55), s.ink(D2D1::ColorF(i == 0 ? 0.8f : 0, i == 1 ? 0.8f : 0, i == 2 ? 0.8f : 0)));
    s.context->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_SOURCE_OVER);
}

// Bitmaps: opacity, source rectangles, interpolation, straight alpha, brushes, A8 masks,
// compatible targets, CopyFromRenderTarget and the perspective DrawBitmap.
void bitmaps(ID2D1Factory* factory) {
    Scene s(factory, "bitmaps");
    const auto picture = s.picture(64, 48), straight = s.picture(64, 48, true);
    s.target->DrawBitmap(picture.Get(), D2D1::RectF(10, 10, 138, 106), 1, D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
    s.target->DrawBitmap(picture.Get(), D2D1::RectF(150, 10, 278, 106), 1, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    s.target->DrawBitmap(picture.Get(), D2D1::RectF(290, 10, 418, 106), 0.4f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, D2D1::RectF(16, 12, 48, 36));
    s.target->DrawBitmap(straight.Get(), D2D1::RectF(430, 10, 558, 106));
    // A wrapping bitmap brush, scaled and turned, at 80%.
    ComPtr<ID2D1BitmapBrush> tiles;
    s.target->CreateBitmapBrush(picture.Get(), D2D1::BitmapBrushProperties(D2D1_EXTEND_MODE_WRAP, D2D1_EXTEND_MODE_WRAP), tiles.GetAddressOf());
    tiles->SetTransform(D2D1::Matrix3x2F::Scale(0.5f, 0.5f) * D2D1::Matrix3x2F::Rotation(15) * D2D1::Matrix3x2F::Translation(10, 120));
    tiles->SetOpacity(0.8f);
    s.target->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(10, 120, 200, 250), 20, 20), tiles.Get());
    // FillOpacityMask: an A8 radial mask filled with a solid brush.
    std::vector<uint8_t> alpha(64 * 64);
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x)
            alpha[y * 64 + x] = static_cast<uint8_t>(std::clamp(255 - std::hypot(x - 31.5, y - 31.5) * 8, 0.0, 255.0));
    ComPtr<ID2D1Bitmap> spot;
    s.target->CreateBitmap(D2D1::SizeU(64, 64), alpha.data(), 64, D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)),
                           spot.GetAddressOf());
    s.target->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
    const auto area = D2D1::RectF(220, 120, 350, 250);
    s.target->FillOpacityMask(spot.Get(), s.ink(D2D1::ColorF(1, 0.8f, 0.2f)), D2D1_OPACITY_MASK_CONTENT_GRAPHICS, &area);
    s.target->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    // A compatible target drawn into, then shown twice through its bitmap.
    ComPtr<ID2D1BitmapRenderTarget> offscreen;
    s.target->CreateCompatibleRenderTarget(D2D1::SizeF(100, 100), offscreen.GetAddressOf());
    ComPtr<ID2D1SolidColorBrush> pen;
    offscreen->CreateSolidColorBrush(D2D1::ColorF(0.3f, 0.9f, 0.9f), pen.GetAddressOf());
    offscreen->BeginDraw();
    offscreen->Clear(D2D1::ColorF(0, 0, 0, 0));
    offscreen->DrawEllipse(D2D1::Ellipse({50, 50}, 40, 40), pen.Get(), 8);
    check(SUCCEEDED(offscreen->EndDraw()), "compatible target EndDraw");
    ComPtr<ID2D1Bitmap> drawn;
    offscreen->GetBitmap(drawn.GetAddressOf());
    s.target->DrawBitmap(drawn.Get(), D2D1::RectF(370, 130, 470, 230));
    s.target->DrawBitmap(drawn.Get(), D2D1::RectF(480, 120, 630, 270), 0.5f);
    // CopyFromRenderTarget: the top-left quarter copied, drawn small at the bottom.
    ComPtr<ID2D1Bitmap> copy;
    s.target->CreateBitmap(D2D1::SizeU(320, 200), D2D1::BitmapProperties(s.target->GetPixelFormat()), copy.GetAddressOf());
    const auto origin = D2D1::Point2U(0, 0);
    const auto source = D2D1::RectU(0, 0, 320, 200);
    check(SUCCEEDED(copy->CopyFromRenderTarget(&origin, s.target.Get(), &source)), "CopyFromRenderTarget");
    s.target->DrawBitmap(copy.Get(), D2D1::RectF(10, 280, 170, 380));
    // Perspective: the picture turned about its vertical axis (as the screensaver's boxes).
    D2D1_MATRIX_4X4_F turn{};
    const float angle = 0.7f, eye = 600, cx = 420, cy = 330;
    const float c = std::cos(angle), sn = std::sin(angle);
    // Rectangle (0,0)-(160,100) centred, turned, then projected (perspective.hpp's camera).
    const float u[3] = {c, 0, -sn}, v[3] = {0, 1, 0}, o[3] = {-80 * c, -50, 80 * sn};
    const float face[4][4] = {{u[0], u[1], u[2], 0}, {v[0], v[1], v[2], 0}, {0, 0, 1, 0}, {o[0], o[1], o[2], 1}};
    const float projection[4][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {cx / eye, cy / eye, 1 / eye, 1 / eye}, {cx, cy, 0.5f, 1}};
    for (int r = 0; r < 4; ++r)
        for (int k = 0; k < 4; ++k) {
            float sum = 0;
            for (int i = 0; i < 4; ++i) sum += face[r][i] * projection[i][k];
            turn.m[r][k] = sum;
        }
    s.context->DrawBitmap(picture.Get(), D2D1::RectF(0, 0, 160, 100), 1, D2D1_INTERPOLATION_MODE_ANISOTROPIC, nullptr, &turn);
    // Block compression is not offered on Linux (artwork stays BGRA), and creation fails cleanly.
    check(!s.context->IsDxgiFormatSupported(DXGI_FORMAT_BC1_UNORM) && !s.context->IsDxgiFormatSupported(DXGI_FORMAT_BC3_UNORM),
          "block compression is not offered");
    ComPtr<ID2D1Bitmap1> bc1;
    const uint8_t block[8]{};
    const HRESULT made = s.context->CreateBitmap(D2D1::SizeU(4, 4), block, 8,
                                                 D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_NONE, D2D1::PixelFormat(DXGI_FORMAT_BC1_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)),
                                                 bc1.GetAddressOf());
    check(FAILED(made), "BC1 bitmaps are refused");
}

// Effects as the launcher chains them.
void effects(ID2D1Factory* factory) {
    Scene s(factory, "effects");
    const auto picture = s.picture(120, 90);
    // Gaussian blur, soft border (grows) and hard border (keeps its edges), at 2x.
    ComPtr<ID2D1Effect> blur;
    s.context->CreateEffect(CLSID_D2D1GaussianBlur, blur.GetAddressOf());
    blur->SetInput(0, picture.Get());
    blur->SetValue(D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION, 4.0f);
    blur->SetValue(D2D1_GAUSSIANBLUR_PROP_BORDER_MODE, D2D1_BORDER_MODE_SOFT);
    s.context->DrawImage(blur.Get(), D2D1::Point2F(20, 20));
    blur->SetValue(D2D1_GAUSSIANBLUR_PROP_BORDER_MODE, D2D1_BORDER_MODE_HARD);
    s.context->DrawImage(blur.Get(), D2D1::Point2F(170, 20));
    // A wide blur (reduced internally), as the frosted glass.
    blur->SetValue(D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION, 24.0f);
    s.context->DrawImage(blur.Get(), D2D1::Point2F(320, 20));
    // Saturation 0 into a compatible target (the shelf's desaturated art).
    ComPtr<ID2D1Effect> gray;
    s.context->CreateEffect(CLSID_D2D1Saturation, gray.GetAddressOf());
    gray->SetInput(0, picture.Get());
    gray->SetValue(D2D1_SATURATION_PROP_SATURATION, 0.0f);
    s.context->DrawImage(gray.Get(), D2D1::Point2F(470, 20));
    // Colour matrix: swap red and blue, premultiplied, clamped.
    ComPtr<ID2D1Effect> swap;
    s.context->CreateEffect(CLSID_D2D1ColorMatrix, swap.GetAddressOf());
    swap->SetInput(0, picture.Get());
    swap->SetValue(D2D1_COLORMATRIX_PROP_COLOR_MATRIX, D2D1_MATRIX_5X4_F{0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0});
    swap->SetValue(D2D1_COLORMATRIX_PROP_CLAMP_OUTPUT, TRUE);
    s.context->DrawImage(swap.Get(), D2D1::Point2F(20, 150));
    // Blend multiply of a scaled tile over the picture (the steelbook print).
    std::vector<uint8_t> metal(32 * 32 * 4);
    for (int i = 0; i < 32 * 32; ++i) {
        const uint8_t tone = static_cast<uint8_t>(150 + 100 * ((i % 32) / 31.0));
        metal[i * 4] = metal[i * 4 + 1] = metal[i * 4 + 2] = tone; metal[i * 4 + 3] = 255;
    }
    ComPtr<ID2D1Bitmap> sheet;
    s.target->CreateBitmap(D2D1::SizeU(32, 32), metal.data(), 128, D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)),
                           sheet.GetAddressOf());
    ComPtr<ID2D1Effect> stretch, printed;
    s.context->CreateEffect(CLSID_D2D12DAffineTransform, stretch.GetAddressOf());
    s.context->CreateEffect(CLSID_D2D1Blend, printed.GetAddressOf());
    stretch->SetInput(0, sheet.Get());
    stretch->SetValue(D2D1_2DAFFINETRANSFORM_PROP_TRANSFORM_MATRIX, D2D1::Matrix3x2F::Scale(120.0f / 32, 90.0f / 32));
    stretch->SetValue(D2D1_2DAFFINETRANSFORM_PROP_BORDER_MODE, D2D1_BORDER_MODE_HARD);
    printed->SetInputEffect(0, stretch.Get());
    printed->SetInput(1, picture.Get());
    printed->SetValue(D2D1_BLEND_PROP_MODE, D2D1_BLEND_MODE_MULTIPLY);
    s.context->DrawImage(printed.Get(), D2D1::Point2F(170, 150), D2D1_INTERPOLATION_MODE_LINEAR);
    // Point-specular lighting of a height map, its brightness as alpha, masked by the map.
    std::vector<uint8_t> bumps(120 * 90 * 4);
    for (int y = 0; y < 90; ++y)
        for (int x = 0; x < 120; ++x) {
            const double height = 0.5 + 0.5 * std::sin(x * 0.15) * std::cos(y * 0.15);
            const auto a = static_cast<uint8_t>(height * 255);
            uint8_t* p = &bumps[(y * 120 + x) * 4];
            p[0] = p[1] = p[2] = a; p[3] = a;
        }
    ComPtr<ID2D1Bitmap> map;
    s.target->CreateBitmap(D2D1::SizeU(120, 90), bumps.data(), 480, D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)),
                           map.GetAddressOf());
    ComPtr<ID2D1Effect> lighting, highlight, masked;
    s.context->CreateEffect(CLSID_D2D1PointSpecular, lighting.GetAddressOf());
    s.context->CreateEffect(CLSID_D2D1ColorMatrix, highlight.GetAddressOf());
    s.context->CreateEffect(CLSID_D2D1ArithmeticComposite, masked.GetAddressOf());
    lighting->SetInput(0, map.Get());
    lighting->SetValue(D2D1_POINTSPECULAR_PROP_SPECULAR_EXPONENT, 12.0f);
    lighting->SetValue(D2D1_POINTSPECULAR_PROP_SPECULAR_CONSTANT, 1.5f);
    lighting->SetValue(D2D1_POINTSPECULAR_PROP_SURFACE_SCALE, 4.0f);
    lighting->SetValue(D2D1_POINTSPECULAR_PROP_KERNEL_UNIT_LENGTH, D2D1_VECTOR_2F{1.25f, 1.25f});
    lighting->SetValue(D2D1_POINTSPECULAR_PROP_COLOR, D2D1_VECTOR_3F{1, 1, 1});
    lighting->SetValue(D2D1_POINTSPECULAR_PROP_LIGHT_POSITION, D2D1_VECTOR_3F{80, 30, 40});
    highlight->SetInputEffect(0, lighting.Get());
    highlight->SetValue(D2D1_COLORMATRIX_PROP_COLOR_MATRIX, D2D1_MATRIX_5X4_F{1, 0, 0, 1.0f / 3, 0, 1, 0, 1.0f / 3, 0, 0, 1, 1.0f / 3, 0, 0, 0, 0, 0, 0, 0, 0});
    highlight->SetValue(D2D1_COLORMATRIX_PROP_ALPHA_MODE, D2D1_COLORMATRIX_ALPHA_MODE_STRAIGHT);
    masked->SetInputEffect(0, highlight.Get());
    masked->SetInput(1, map.Get());
    masked->SetValue(D2D1_ARITHMETICCOMPOSITE_PROP_COEFFICIENTS, D2D1_VECTOR_4F{1, 0, 0, 0});
    masked->SetValue(D2D1_ARITHMETICCOMPOSITE_PROP_CLAMP_OUTPUT, TRUE);
    s.target->FillRectangle(D2D1::RectF(320, 150, 440, 240), s.ink(D2D1::ColorF(0.25f, 0.2f, 0.3f)));
    s.context->DrawImage(masked.Get(), D2D1::Point2F(320, 150), D2D1_INTERPOLATION_MODE_LINEAR);
    s.context->DrawImage(lighting.Get(), D2D1::Point2F(470, 150), D2D1_INTERPOLATION_MODE_LINEAR);
    // Composite (foreground over a copy) then blurred, drawn shrunk: evaluated at the smaller size.
    ComPtr<ID2D1Effect> composite, frost;
    s.context->CreateEffect(CLSID_D2D1Composite, composite.GetAddressOf());
    s.context->CreateEffect(CLSID_D2D1GaussianBlur, frost.GetAddressOf());
    composite->SetInput(0, picture.Get());
    composite->SetInput(1, map.Get());
    frost->SetInputEffect(0, composite.Get());
    frost->SetValue(D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION, 3.0f);
    frost->SetValue(D2D1_GAUSSIANBLUR_PROP_BORDER_MODE, D2D1_BORDER_MODE_HARD);
    s.target->SetTransform(D2D1::Matrix3x2F::Scale(0.5f, 0.5f) * D2D1::Matrix3x2F::Translation(40, 290));
    s.context->DrawImage(frost.Get());
    // The same at full size inside a layer at 60% (the orbit glow's way).
    s.target->SetTransform(D2D1::Matrix3x2F::Translation(150, 280));
    s.context->PushLayer(D2D1::LayerParameters1(D2D1::InfiniteRect(), nullptr, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE, D2D1::IdentityMatrix(), 0.6f), nullptr);
    s.context->DrawImage(frost.Get());
    s.context->PopLayer();
    s.target->SetTransform(D2D1::IdentityMatrix());
}

// CombineWithGeometry as the launcher uses it: a speech bubble (union, filled and stroked),
// an occlusion mask (rectangle minus overlapping shapes), an inlay clipped to a body
// (intersection) and the DRM-free padlock (union, then exclusion).
void combine(ID2D1Factory* factory) {
    Scene s(factory, "combine");
    const auto result = [&](ID2D1Geometry* a, ID2D1Geometry* b, D2D1_COMBINE_MODE mode) {
        ComPtr<ID2D1PathGeometry> out;
        ComPtr<ID2D1GeometrySink> sink;
        factory->CreatePathGeometry(out.GetAddressOf());
        out->Open(sink.GetAddressOf());
        const HRESULT hr = a->CombineWithGeometry(b, mode, nullptr, sink.Get());
        check(SUCCEEDED(hr) && SUCCEEDED(sink->Close()), "CombineWithGeometry");
        return out;
    };
    ComPtr<ID2D1RoundedRectangleGeometry> body;
    factory->CreateRoundedRectangleGeometry(D2D1::RoundedRect(D2D1::RectF(30, 30, 230, 120), 20, 20), body.GetAddressOf());
    ComPtr<ID2D1PathGeometry> tail;
    ComPtr<ID2D1GeometrySink> sink;
    factory->CreatePathGeometry(tail.GetAddressOf());
    tail->Open(sink.GetAddressOf());
    sink->BeginFigure({200, 70}, D2D1_FIGURE_BEGIN_FILLED);
    sink->AddBezier(D2D1::BezierSegment({230, 70}, {255, 85}, {280, 110}));
    sink->AddBezier(D2D1::BezierSegment({250, 105}, {225, 95}, {200, 92}));
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    sink->Close();
    const auto bubble = result(body.Get(), tail.Get(), D2D1_COMBINE_MODE_UNION);
    s.target->FillGeometry(bubble.Get(), s.ink(D2D1::ColorF(0.05f, 0.06f, 0.08f, 0.9f)));
    s.target->DrawGeometry(bubble.Get(), s.ink(D2D1::ColorF(1, 1, 1, 0.8f)), 1.5f);

    // Occlusion: everything but three overlapping squircle-ish cases is painted.
    std::vector<ComPtr<ID2D1Geometry>> cases;
    for (int i = 0; i < 3; ++i) {
        ComPtr<ID2D1RoundedRectangleGeometry> c;
        factory->CreateRoundedRectangleGeometry(D2D1::RoundedRect(D2D1::RectF(330.0f + i * 70, 40.0f + i * 20, 430.0f + i * 70, 150.0f + i * 20), 18, 18),
                                                c.GetAddressOf());
        cases.push_back(c);
    }
    std::vector<ID2D1Geometry*> shapes{cases[0].Get(), cases[1].Get(), cases[2].Get()};
    ComPtr<ID2D1RectangleGeometry> area;
    ComPtr<ID2D1GeometryGroup> group;
    factory->CreateRectangleGeometry(D2D1::RectF(310, 20, 620, 210), area.GetAddressOf());
    factory->CreateGeometryGroup(D2D1_FILL_MODE_WINDING, shapes.data(), 3, group.GetAddressOf());
    ComPtr<ID2D1PathGeometry> occlusion;
    factory->CreatePathGeometry(occlusion.GetAddressOf());
    occlusion->Open(sink.ReleaseAndGetAddressOf());
    area->CombineWithGeometry(group.Get(), D2D1_COMBINE_MODE_EXCLUDE, D2D1::IdentityMatrix(), sink.Get());
    sink->Close();
    s.target->PushLayer(D2D1::LayerParameters(D2D1::RectF(310, 20, 620, 210), occlusion.Get()), nullptr);
    for (int i = 0; i < 16; ++i) s.target->FillRectangle(D2D1::RectF(310.0f + i * 20, 20, 320.0f + i * 20, 210), s.ink(D2D1::ColorF(0.9f, 0.6f, 0.2f)));
    s.target->PopLayer();
    s.target->DrawGeometry(group.Get(), s.ink(D2D1::ColorF(0.5f, 0.8f, 1, 0.5f)), 1);

    // Intersection: a striped inlay clipped by a round body, filled and outlined.
    ComPtr<ID2D1EllipseGeometry> round_body;
    factory->CreateEllipseGeometry(D2D1::Ellipse({130, 300}, 100, 70), round_body.GetAddressOf());
    ComPtr<ID2D1PathGeometry> inlay;
    factory->CreatePathGeometry(inlay.GetAddressOf());
    inlay->Open(sink.ReleaseAndGetAddressOf());
    sink->SetFillMode(D2D1_FILL_MODE_WINDING);
    for (int i = 0; i < 5; ++i) {
        const float x = 20.0f + i * 45;
        const D2D1_POINT_2F band[] = {{x + 30, 230}, {x + 60, 230}, {x + 30, 380}, {x, 380}};
        sink->BeginFigure({x, 230}, D2D1_FIGURE_BEGIN_FILLED);
        sink->AddLines(band, 4);
        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    }
    sink->Close();
    const auto clipped = result(inlay.Get(), round_body.Get(), D2D1_COMBINE_MODE_INTERSECT);
    s.target->FillGeometry(round_body.Get(), s.ink(D2D1::ColorF(0.25f, 0.25f, 0.3f)));
    s.target->FillGeometry(clipped.Get(), s.ink(D2D1::ColorF(0.3f, 0.8f, 0.5f)));
    s.target->DrawGeometry(clipped.Get(), s.ink(D2D1::ColorF(1, 1, 1)), 1);

    // The padlock: keyhole = circle + slot, body minus keyhole, plus an open shackle.
    ComPtr<ID2D1RoundedRectangleGeometry> lock_body;
    ComPtr<ID2D1EllipseGeometry> hole;
    ComPtr<ID2D1RectangleGeometry> slot;
    factory->CreateRoundedRectangleGeometry(D2D1::RoundedRect(D2D1::RectF(330, 280, 500, 380), 24, 24), lock_body.GetAddressOf());
    factory->CreateEllipseGeometry(D2D1::Ellipse({415, 315}, 17, 17), hole.GetAddressOf());
    factory->CreateRectangleGeometry(D2D1::RectF(406, 321, 424, 365), slot.GetAddressOf());
    const auto keyhole = result(hole.Get(), slot.Get(), D2D1_COMBINE_MODE_UNION);
    const auto lock = result(lock_body.Get(), keyhole.Get(), D2D1_COMBINE_MODE_EXCLUDE);
    s.target->FillGeometry(lock.Get(), s.ink(D2D1::ColorF(1, 1, 1)));
    ComPtr<ID2D1PathGeometry> shackle;
    factory->CreatePathGeometry(shackle.GetAddressOf());
    shackle->Open(sink.ReleaseAndGetAddressOf());
    sink->BeginFigure({375, 246}, D2D1_FIGURE_BEGIN_HOLLOW);
    sink->AddLine({375, 232});
    sink->AddArc(D2D1::ArcSegment({455, 232}, D2D1::SizeF(40, 40), 0, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
    sink->AddLine({455, 286});
    sink->EndFigure(D2D1_FIGURE_END_OPEN);
    sink->Close();
    s.target->DrawGeometry(shackle.Get(), s.ink(D2D1::ColorF(1, 1, 1)), 18);
    BOOL in_hole = TRUE, in_body = FALSE;
    lock->FillContainsPoint({415, 315}, nullptr, D2D1_DEFAULT_FLATTENING_TOLERANCE, &in_hole);
    lock->FillContainsPoint({350, 360}, nullptr, D2D1_DEFAULT_FLATTENING_TOLERANCE, &in_body);
    check(!in_hole && in_body, "exclusion leaves the keyhole open");
    BOOL in_overlap = TRUE;
    occlusion->FillContainsPoint({460, 110}, nullptr, D2D1_DEFAULT_FLATTENING_TOLERANCE, &in_overlap);
    check(!in_overlap, "occlusion mask excludes overlapping shapes");
}

// A launcher-like 1080p frame, timed: a wallpaper, 24 masked covers with shadows and outlines,
// gradients, and a frosted full-screen backdrop (copy, wide blur, masked layer).
void timing(ID2D1Factory* factory) {
    ComPtr<ID2D1HwndRenderTarget> window;
    factory->CreateHwndRenderTarget(D2D1::RenderTargetProperties(), D2D1::HwndRenderTargetProperties(const_cast<char*>("timing"), D2D1::SizeU(1920, 1080)),
                                    window.GetAddressOf());
    ComPtr<ID2D1DeviceContext1> context;
    window->QueryInterface(IID_PPV_ARGS(context.GetAddressOf()));
    ComPtr<ID2D1SolidColorBrush> solid;
    context->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, 0.1f), solid.GetAddressOf());
    std::vector<uint8_t> pixels(320 * 320 * 4);
    for (size_t i = 0; i < pixels.size(); i += 4) {
        const size_t p = i / 4, x = p % 320, y = p / 320;
        pixels[i] = static_cast<uint8_t>(y * 255 / 320); pixels[i + 1] = static_cast<uint8_t>((x ^ y) & 255);
        pixels[i + 2] = static_cast<uint8_t>(x * 255 / 320); pixels[i + 3] = 255;
    }
    ComPtr<ID2D1Bitmap> cover, copy;
    context->CreateBitmap(D2D1::SizeU(320, 320), pixels.data(), 320 * 4, D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)),
                          cover.GetAddressOf());
    ComPtr<ID2D1BitmapBrush> art;
    context->CreateBitmapBrush(cover.Get(), art.GetAddressOf());
    context->CreateBitmap(D2D1::SizeU(1920, 1080), D2D1::BitmapProperties(context->GetPixelFormat()), copy.GetAddressOf());
    ComPtr<ID2D1Effect> frost;
    context->CreateEffect(CLSID_D2D1GaussianBlur, frost.GetAddressOf());
    frost->SetInput(0, copy.Get());
    frost->SetValue(D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION, 40.0f);
    frost->SetValue(D2D1_GAUSSIANBLUR_PROP_BORDER_MODE, D2D1_BORDER_MODE_HARD);
    ComPtr<ID2D1RoundedRectangleGeometry> pane;
    factory->CreateRoundedRectangleGeometry(D2D1::RoundedRect(D2D1::RectF(360, 240, 1560, 840), 28, 28), pane.GetAddressOf());
    std::vector<ComPtr<ID2D1RoundedRectangleGeometry>> shapes;
    for (int i = 0; i < 24; ++i) {
        ComPtr<ID2D1RoundedRectangleGeometry> shape;
        const float x = 60.0f + (i % 8) * 230, y = 80.0f + (i / 8) * 320;
        factory->CreateRoundedRectangleGeometry(D2D1::RoundedRect(D2D1::RectF(x, y, x + 200, y + 280), 18, 18), shape.GetAddressOf());
        shapes.push_back(shape);
    }
    using clock = std::chrono::steady_clock;
    double scene_ms = 0, frost_ms = 0;
    constexpr int frames = 10;
    for (int frame = 0; frame < frames; ++frame) {
        const auto start = clock::now();
        context->BeginDraw();
        context->SetTransform(D2D1::IdentityMatrix());
        context->Clear(D2D1::ColorF(0.02f, 0.03f, 0.04f));
        context->DrawBitmap(cover.Get(), D2D1::RectF(0, 0, 1920, 1080), 0.6f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
        for (int i = 0; i < 24; ++i) {
            const float x = 60.0f + (i % 8) * 230, y = 80.0f + (i / 8) * 320;
            for (int layer = 3; layer >= 1; --layer) {
                solid->SetColor(D2D1::ColorF(0, 0, 0, 0.07f));
                context->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(x - layer * 3, y + 7 - layer * 3, x + 200 + layer * 3, y + 287 + layer * 3), 20, 20),
                                              solid.Get());
            }
            art->SetTransform(D2D1::Matrix3x2F::Scale(280.0f / 320, 280.0f / 320) * D2D1::Matrix3x2F::Translation(x - 22, y));
            context->FillGeometry(shapes[i].Get(), art.Get());
            solid->SetColor(D2D1::ColorF(1, 1, 1, 0.12f));
            context->DrawGeometry(shapes[i].Get(), solid.Get(), 1);
        }
        const auto backdrop = clock::now();
        copy->CopyFromRenderTarget(nullptr, context.Get(), nullptr);
        context->PushLayer(D2D1::LayerParameters1(D2D1::InfiniteRect(), pane.Get()), nullptr);
        context->DrawImage(frost.Get());
        context->PopLayer();
        solid->SetColor(D2D1::ColorF(0.04f, 0.05f, 0.07f, 0.4f));
        context->FillGeometry(pane.Get(), solid.Get());
        const auto end = clock::now();
        if (frame == frames - 1) check(SUCCEEDED(context->EndDraw()), "1080p frame EndDraw");
        else context->EndDraw();
        scene_ms += std::chrono::duration<double, std::milli>(backdrop - start).count();
        frost_ms += std::chrono::duration<double, std::milli>(end - backdrop).count();
    }
    std::printf("1080p frame: covers %.1f ms, frosted backdrop %.1f ms (mean of %d)\n", scene_ms / frames, frost_ms / frames, frames);
}
} // namespace

int main(int argc, char** argv) {
    if (argc > 1) output_directory = argv[1];
    ComPtr<ID2D1Factory> factory;
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, factory.GetAddressOf()))) return 1;
    shapes(factory.Get());
    paths(factory.Get());
    layers(factory.Get());
    bitmaps(factory.Get());
    effects(factory.Get());
    combine(factory.Get());
    timing(factory.Get());
    std::printf("%d failure(s)\n", failures);
    return failures ? 1 : 0;
}
