// The physical media of Półka's open case (game_case_renderer.cpp) and Coverflow's printed disc:
// a 3.5" diskette, a game card (the real Switch cards: shouldered shell, top slot, the console's band,
// the art and the code strip on its label) and a pressed disc (metal rim, printed surface, a sweep of
// rainbow light, the clear hub). Outlines: media_layout.hpp.
#include "renderer.hpp"
#include "media_layout.hpp"
#include <cmath>
#include <functional>
#include <initializer_list>
#include <vector>

namespace {
using palette::gray;
// The diskette's procedural tiles: a hash, and smooth value noise that wraps every `period` texels.
float hash(uint32_t x, uint32_t y, uint32_t salt) {
    uint32_t seed = x * 374761393u + y * 668265263u + salt * 2246822519u;
    seed = (seed ^ (seed >> 13)) * 1274126177u;
    return static_cast<float>((seed ^ (seed >> 16)) & 0xffffu) / 65535.0f;
}
float valueNoise(float x, float y, uint32_t period_x, uint32_t period_y, uint32_t salt) {
    const auto x0 = static_cast<uint32_t>(std::floor(x)), y0 = static_cast<uint32_t>(std::floor(y));
    const float fx = x - std::floor(x), fy = y - std::floor(y);
    const float sx = fx * fx * (3 - 2 * fx), sy = fy * fy * (3 - 2 * fy);
    const auto at = [&](uint32_t i, uint32_t j) { return hash(i % period_x, j % period_y, salt); };
    const float top = std::lerp(at(x0, y0), at(x0 + 1, y0), sx), bottom = std::lerp(at(x0, y0 + 1), at(x0 + 1, y0 + 1), sx);
    return std::lerp(top, bottom, sy);
}
Microsoft::WRL::ComPtr<ID2D1PathGeometry> floppyOutline(ID2D1Factory* factory, settings_ui::Rect r) {
    const auto points = media_layout::floppyOutline(r);
    Microsoft::WRL::ComPtr<ID2D1PathGeometry> body;
    Microsoft::WRL::ComPtr<ID2D1GeometrySink> sink;
    if (FAILED(factory->CreatePathGeometry(body.GetAddressOf())) || FAILED(body->Open(sink.GetAddressOf()))) return {};
    sink->BeginFigure(D2D1::Point2F(points.points[0].x, points.points[0].y), D2D1_FIGURE_BEGIN_FILLED);
    for (size_t i = 1; i < points.count; ++i) sink->AddLine(D2D1::Point2F(points.points[i].x, points.points[i].y));
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    return SUCCEEDED(sink->Close()) ? body : Microsoft::WRL::ComPtr<ID2D1PathGeometry>{};
}
// A game card's shell (media_layout::card): rounded, its top a little narrower down to its shoulders.
Microsoft::WRL::ComPtr<ID2D1PathGeometry> cardOutline(ID2D1Factory* factory, D2D1_RECT_F r) {
    namespace card = media_layout::card;
    const float w = r.right - r.left, h = r.bottom - r.top;
    const float s = card::shoulder * w, top_r = card::top_corner * w, corner = card::corner * w, foot = card::shoulder_depth * h;
    Microsoft::WRL::ComPtr<ID2D1PathGeometry> body;
    Microsoft::WRL::ComPtr<ID2D1GeometrySink> sink;
    if (FAILED(factory->CreatePathGeometry(body.GetAddressOf())) || FAILED(body->Open(sink.GetAddressOf()))) return {};
    const auto arc = [&](float x, float y, float radius) {
        sink->AddArc(D2D1::ArcSegment({x, y}, {radius, radius}, 0, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
    };
    sink->BeginFigure({r.left + s + top_r, r.top}, D2D1_FIGURE_BEGIN_FILLED);
    sink->AddLine({r.right - s - top_r, r.top}); arc(r.right - s, r.top + top_r, top_r);
    sink->AddLine({r.right - s, r.top + foot - s}); sink->AddLine({r.right, r.top + foot}); // The right shoulder.
    sink->AddLine({r.right, r.bottom - corner}); arc(r.right - corner, r.bottom, corner);
    sink->AddLine({r.left + corner, r.bottom}); arc(r.left, r.bottom - corner, corner);
    sink->AddLine({r.left, r.top + foot}); sink->AddLine({r.left + s, r.top + foot - s}); // The left shoulder.
    sink->AddLine({r.left + s, r.top + top_r}); arc(r.left + s + top_r, r.top, top_r);
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    return SUCCEEDED(sink->Close()) ? body : Microsoft::WRL::ComPtr<ID2D1PathGeometry>{};
}
// A ring (outer minus inner circle): the disc's body and its printed surface.
Microsoft::WRL::ComPtr<ID2D1PathGeometry> ring(ID2D1Factory* factory, float cx, float cy, float outer, float inner) {
    Microsoft::WRL::ComPtr<ID2D1PathGeometry> path;
    Microsoft::WRL::ComPtr<ID2D1GeometrySink> sink;
    if (FAILED(factory->CreatePathGeometry(path.GetAddressOf())) || FAILED(path->Open(sink.GetAddressOf()))) return {};
    sink->SetFillMode(D2D1_FILL_MODE_ALTERNATE);
    for (const float r : {outer, inner}) {
        if (r <= 0) continue;
        sink->BeginFigure({cx + r, cy}, D2D1_FIGURE_BEGIN_FILLED);
        sink->AddArc(D2D1::ArcSegment({cx - r, cy}, {r, r}, 0, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
        sink->AddArc(D2D1::ArcSegment({cx + r, cy}, {r, r}, 0, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    }
    return SUCCEEDED(sink->Close()) ? path : Microsoft::WRL::ComPtr<ID2D1PathGeometry>{};
}
}

void Renderer::createMediaMaterials() {
    const auto gradient = [&](Microsoft::WRL::ComPtr<ID2D1LinearGradientBrush>& brush, std::initializer_list<D2D1_GRADIENT_STOP> stops) {
        if (brush) return;
        Microsoft::WRL::ComPtr<ID2D1GradientStopCollection> colors;
        if (SUCCEEDED(target_->CreateGradientStopCollection(stops.begin(), static_cast<UINT32>(stops.size()), colors.GetAddressOf())))
            target_->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(D2D1::Point2F(0, 0), D2D1::Point2F(1, 1)), colors.Get(), brush.GetAddressOf());
    };
    gradient(media_plastic_, {{0, gray(0.12f)}, {0.24f, gray(0.075f)}, {0.72f, gray(0.038f)}, {1, gray(0.070f)}});
    gradient(media_edge_, {{0, gray(1, 0.22f)}, {0.45f, gray(1, 0.055f)}, {0.55f, gray(0, 0.25f)}, {1, gray(0, 0.48f)}});
    // A moulded face lit from the top left (PyroCade's 120° front highlight), over any shell colour.
    gradient(media_gloss_, {{0, gray(1, 0.13f)}, {0.34f, gray(1, 0)}, {0.80f, gray(0, 0.07f)}, {1, gray(0, 0.16f)}});
    // The disc's pressed metal: a cool silver with darker bands, turned with the disc.
    gradient(disc_metal_, {{0, D2D1::ColorF(0.86f, 0.89f, 0.93f)}, {0.18f, D2D1::ColorF(0.46f, 0.49f, 0.58f)}, {0.40f, D2D1::ColorF(0.91f, 0.94f, 0.95f)},
                           {0.60f, D2D1::ColorF(0.69f, 0.76f, 0.82f)}, {0.80f, D2D1::ColorF(0.45f, 0.49f, 0.58f)}, {1, D2D1::ColorF(0.88f, 0.90f, 0.93f)}});
    if (!media_grain_) {
        // One tiny repeatable neutral grain tile for the target, not new noise every frame.
        constexpr UINT size = 128;
        std::array<uint8_t, size * size * 4> pixels{};
        for (uint32_t i = 0; i < size * size; ++i) {
            uint32_t seed = (i + 1) * 747796405u + 2891336453u;
            seed = ((seed >> ((seed >> 28) + 4)) ^ seed) * 277803737u;
            const auto alpha = static_cast<uint8_t>(3 + (seed & 7));
            pixels[i * 4] = pixels[i * 4 + 1] = pixels[i * 4 + 2] = seed & 256 ? alpha : 0;
            pixels[i * 4 + 3] = alpha;
        }
        Microsoft::WRL::ComPtr<ID2D1Bitmap> bitmap;
        if (SUCCEEDED(target_->CreateBitmap(D2D1::SizeU(size, size), pixels.data(), size * 4,
            D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)), bitmap.GetAddressOf())))
            target_->CreateBitmapBrush(bitmap.Get(), D2D1::BitmapBrushProperties(D2D1_EXTEND_MODE_WRAP, D2D1_EXTEND_MODE_WRAP), media_grain_.GetAddressOf());
    }
    if (!disc_wedge_) {
        // One slice of the unit circle (6°, from the centre): the rainbow sweep is drawn slice by slice.
        Microsoft::WRL::ComPtr<ID2D1GeometrySink> sink;
        if (SUCCEEDED(factory_->CreatePathGeometry(disc_wedge_.GetAddressOf())) && SUCCEEDED(disc_wedge_->Open(sink.GetAddressOf()))) {
            constexpr float slice = 6.2f * 3.14159265f / 180; // A hair wider than 6°: no seams between slices.
            sink->BeginFigure({0, 0}, D2D1_FIGURE_BEGIN_FILLED);
            sink->AddLine({1, 0});
            sink->AddArc(D2D1::ArcSegment({std::cos(slice), std::sin(slice)}, {1, 1}, 0, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
            sink->EndFigure(D2D1_FIGURE_END_CLOSED);
            if (FAILED(sink->Close())) disc_wedge_.Reset();
        } else disc_wedge_.Reset();
    }
}

void Renderer::createFloppyMaterials() {
    const auto tile = [&](Microsoft::WRL::ComPtr<ID2D1BitmapBrush>& brush, UINT width, UINT height, const std::vector<uint8_t>& pixels) {
        Microsoft::WRL::ComPtr<ID2D1Bitmap> bitmap;
        if (SUCCEEDED(target_->CreateBitmap(D2D1::SizeU(width, height), pixels.data(), width * 4,
                D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)), bitmap.GetAddressOf())))
            target_->CreateBitmapBrush(bitmap.Get(), D2D1::BitmapBrushProperties(D2D1_EXTEND_MODE_WRAP, D2D1_EXTEND_MODE_WRAP,
                D2D1_BITMAP_INTERPOLATION_MODE_LINEAR), brush.GetAddressOf());
    };
    // Light or dark specks over whatever is beneath: premultiplied white or black.
    const auto speck = [](uint8_t* p, float value) {
        const auto a = static_cast<uint8_t>(std::lround(std::clamp(std::abs(value), 0.0f, 1.0f) * 255));
        p[0] = p[1] = p[2] = value > 0 ? a : 0; p[3] = a;
    };
    if (!floppy_plastic_) {
        // Moulded, sand-blasted plastic: a fine stipple over a soft mottle, tileable (256 texels).
        constexpr UINT size = 256;
        std::vector<uint8_t> pixels(size * size * 4);
        for (UINT y = 0; y < size; ++y) for (UINT x = 0; x < size; ++x) {
            const float fine = hash(x, y, 11) - 0.5f;
            const float grain = valueNoise(x / 4.0f, y / 4.0f, size / 4, size / 4, 12) - 0.5f;
            const float mottle = valueNoise(x / 32.0f, y / 32.0f, size / 32, size / 32, 13) - 0.5f;
            speck(pixels.data() + (y * size + x) * 4, 0.075f * fine + 0.065f * grain + 0.05f * mottle);
        }
        tile(floppy_plastic_, size, size, pixels);
    }
    if (!floppy_metal_) {
        // Brushed steel: horizontal brush lines (each row its own brightness, drifting slowly
        // along it), long streaks and a little fine noise. Opaque; 512 x 128 texels.
        constexpr UINT width = 512, height = 128;
        std::vector<uint8_t> pixels(width * height * 4);
        for (UINT y = 0; y < height; ++y) {
            const float row = hash(0, y, 21) - 0.5f;
            for (UINT x = 0; x < width; ++x) {
                const float drift = valueNoise(x / 64.0f, static_cast<float>(y), width / 64, height, 22) - 0.5f;
                const float streak = valueNoise(x / 128.0f, y / 2.0f, width / 128, height / 2, 23) - 0.5f;
                const float tone = 0.64f + 0.13f * row + 0.08f * drift + 0.07f * streak + 0.03f * (hash(x, y, 24) - 0.5f);
                auto* p = pixels.data() + (y * width + x) * 4;
                p[0] = p[1] = p[2] = static_cast<uint8_t>(std::lround(std::clamp(tone, 0.0f, 1.0f) * 255)); p[3] = 255;
                p[0] = static_cast<uint8_t>(std::min(255, p[0] + 4)); // A faint cool tint.
            }
        }
        tile(floppy_metal_, width, height, pixels);
    }
    if (!floppy_paper_) {
        // The label's paper: faint fibres, darker specks only.
        constexpr UINT size = 128;
        std::vector<uint8_t> pixels(size * size * 4);
        for (UINT y = 0; y < size; ++y) for (UINT x = 0; x < size; ++x) {
            const float fibre = valueNoise(x / 8.0f, y / 2.0f, size / 8, size / 2, 31);
            speck(pixels.data() + (y * size + x) * 4, -(0.05f * fibre + 0.035f * hash(x, y, 32)));
        }
        tile(floppy_paper_, size, size, pixels);
    }
    const auto linear = [&](Microsoft::WRL::ComPtr<ID2D1LinearGradientBrush>& brush, std::initializer_list<D2D1_GRADIENT_STOP> stops) {
        if (brush) return;
        Microsoft::WRL::ComPtr<ID2D1GradientStopCollection> colors;
        if (SUCCEEDED(target_->CreateGradientStopCollection(stops.begin(), static_cast<UINT32>(stops.size()), colors.GetAddressOf())))
            target_->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties({0, 0}, {1, 1}), colors.Get(), brush.GetAddressOf());
    };
    // A specular band (narrow core, soft shoulders), and a moulded edge lit from the top left.
    linear(floppy_sheen_, {{0, gray(1, 0)}, {0.30f, gray(1, 0.10f)}, {0.47f, gray(1, 0.62f)}, {0.53f, gray(1, 0.70f)},
                           {0.68f, gray(1, 0.08f)}, {1, gray(1, 0)}});
    linear(floppy_bevel_, {{0, gray(1, 0.42f)}, {0.42f, gray(1, 0.10f)}, {0.58f, gray(0, 0.15f)}, {1, gray(0, 0.60f)}});
    linear(floppy_media_, {{0, D2D1::ColorF(0.11f, 0.12f, 0.14f)}, {0.5f, D2D1::ColorF(0.025f, 0.028f, 0.034f)}, {1, D2D1::ColorF(0.07f, 0.075f, 0.09f)}});
    if (!floppy_glint_) {
        const D2D1_GRADIENT_STOP stops[] = {{0, gray(1, 0.95f)}, {0.18f, gray(1, 0.45f)}, {0.55f, gray(1, 0.08f)}, {1, gray(1, 0)}};
        Microsoft::WRL::ComPtr<ID2D1GradientStopCollection> colors;
        if (SUCCEEDED(target_->CreateGradientStopCollection(stops, 4, colors.GetAddressOf())))
            target_->CreateRadialGradientBrush(D2D1::RadialGradientBrushProperties({0, 0}, {0, 0}, 1, 1), colors.Get(), floppy_glint_.GetAddressOf());
    }
}

// A 3.5" diskette: textured black plastic with moulded edges and recesses, a brushed steel shutter
// that catches the lamp (`light`, 0..1 across it), and a paper label: the cover printed on it, the
// game's name written under it in ballpoint.
void Renderer::drawFloppyDisk(D2D1_RECT_F r, float alpha, float light, Cover* art, bool frozen, const std::wstring& name) {
    const float w = r.right - r.left, h = r.bottom - r.top;
    const settings_ui::Rect area{r.left, r.top, r.right, r.bottom};
    const auto body = floppyOutline(factory_.Get(), area);
    if (!body || w < 4 || alpha < 0.003f) return;
    createMediaMaterials();
    createFloppyMaterials();
    const auto box = [&](float l, float t, float rr, float b) { return D2D1::RectF(r.left + w * l, r.top + h * t, r.left + w * rr, r.top + h * b); };
    const float texel = 1 / std::max(1.0f, viewport_.scale); // Tiles at about one texel per device pixel.
    const auto at = [&](ID2D1BitmapBrush* tile, float stretch_x, float stretch_y) {
        tile->SetTransform(D2D1::Matrix3x2F::Scale(texel * stretch_x, texel * stretch_y) * D2D1::Matrix3x2F::Translation(r.left, r.top));
    };
    const float k = w / 300; // Strokes scale with the disk (designed at 300 px wide).
    // A soft shadow under the disk.
    for (int layer = 3; layer >= 1; --layer) {
        const float spread = layer * 3.5f * k;
        brush(gray(0, 0.075f * alpha));
        target_->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(r.left - spread * 0.6f, r.top + 6 * k - spread * 0.4f,
            r.right + spread * 0.6f, r.bottom + 8 * k + spread), 8 * k + spread, 8 * k + spread), solid_.Get());
    }
    // Body: charcoal plastic, the stipple over it and a broad, dull sheen.
    if (media_plastic_) {
        media_plastic_->SetStartPoint({r.left, r.top}); media_plastic_->SetEndPoint({r.right, r.bottom}); media_plastic_->SetOpacity(alpha);
        target_->FillGeometry(body.Get(), media_plastic_.Get());
    } else { brush(gray(0.065f, alpha)); target_->FillGeometry(body.Get(), solid_.Get()); }
    if (floppy_plastic_) { at(floppy_plastic_.Get(), 1, 1); floppy_plastic_->SetOpacity(alpha); target_->FillGeometry(body.Get(), floppy_plastic_.Get()); }
    if (floppy_sheen_) {
        floppy_sheen_->SetStartPoint({r.left + w * (light - 0.75f), r.top - h * 0.1f});
        floppy_sheen_->SetEndPoint({r.left + w * (light + 0.35f), r.bottom});
        floppy_sheen_->SetOpacity(0.07f * alpha); target_->FillGeometry(body.Get(), floppy_sheen_.Get());
    }
    // Recesses: light catches their lower edge, their upper edge is in shade.
    const auto recess = [&](D2D1_ROUNDED_RECT shape, float depth) {
        brush(gray(0, depth * alpha)); target_->FillRoundedRectangle(shape, solid_.Get());
        const auto& b = shape.rect;
        line(b.left + shape.radiusX, b.top + 0.8f * k, b.right - shape.radiusX, b.top + 0.8f * k, gray(0, 0.55f * alpha), 1.6f * k);
        line(b.left + 0.8f * k, b.top + shape.radiusY, b.left + 0.8f * k, b.bottom - shape.radiusY, gray(0, 0.35f * alpha), 1.2f * k);
        line(b.left + shape.radiusX, b.bottom - 0.6f * k, b.right - shape.radiusX, b.bottom - 0.6f * k, gray(1, 0.13f * alpha), k);
        line(b.right - 0.6f * k, b.top + shape.radiusY, b.right - 0.6f * k, b.bottom - shape.radiusY, gray(1, 0.07f * alpha), k);
    };
    recess(D2D1::RoundedRect(box(0.205f, -0.01f, 0.815f, 0.335f), 3 * k, 3 * k), 0.20f); // The shutter's track.
    recess(D2D1::RoundedRect(box(0.145f, 0.355f, 0.855f, 1.01f), 4 * k, 4 * k), 0.16f);  // Under the label.
    // The insertion arrow, pressed into the top left; write-protect and density holes at the bottom.
    {
        const D2D1_POINT_2F arrow[] = {{r.left + w * 0.105f, r.top + h * 0.045f}, {r.left + w * 0.135f, r.top + h * 0.095f},
                                       {r.left + w * 0.075f, r.top + h * 0.095f}};
        for (int edge = 0; edge < 3; ++edge)
            line(arrow[edge].x, arrow[edge].y, arrow[(edge + 1) % 3].x, arrow[(edge + 1) % 3].y,
                 edge == 1 ? gray(1, 0.16f * alpha) : gray(0, 0.6f * alpha), 1.4f * k);
    }
    for (const float x : {0.040f, 0.928f}) {
        const auto hole = box(x, 0.885f, x + 0.034f, 0.950f);
        brush(gray(0.006f, alpha)); target_->FillRectangle(hole, solid_.Get());
        line(hole.left, hole.bottom, hole.right, hole.bottom, gray(1, 0.14f * alpha), k);
        line(hole.left, hole.top + 0.5f * k, hole.right, hole.top + 0.5f * k, gray(0, 0.7f * alpha), 1.2f * k);
    }
    // The moulded rim: lit at the top left, in shade at the bottom right; a second, inner lip.
    if (floppy_bevel_) {
        floppy_bevel_->SetStartPoint({r.left, r.top}); floppy_bevel_->SetEndPoint({r.right, r.bottom});
        floppy_bevel_->SetOpacity(alpha); target_->DrawGeometry(body.Get(), floppy_bevel_.Get(), 2.2f * k);
        D2D1_MATRIX_3X2_F base; target_->GetTransform(&base);
        target_->SetTransform(D2D1::Matrix3x2F::Scale(0.986f, 0.986f, D2D1::Point2F(r.left + w * 0.5f, r.top + h * 0.5f)) * base);
        floppy_bevel_->SetOpacity(alpha * 0.45f); target_->DrawGeometry(body.Get(), floppy_bevel_.Get(), k);
        target_->SetTransform(base);
    }
    // The shutter: brushed steel bent over the top edge, its window showing the disk beneath.
    const auto plate = box(0.235f, 0, 0.775f, 0.318f);
    const float pw = plate.right - plate.left, ph = plate.bottom - plate.top;
    Microsoft::WRL::ComPtr<ID2D1RoundedRectangleGeometry> shutter;
    if (SUCCEEDED(factory_->CreateRoundedRectangleGeometry(D2D1::RoundedRect(plate, 2.5f * k, 2.5f * k), shutter.GetAddressOf()))) {
        brush(gray(0, 0.45f * alpha)); // Its edge's shadow on the track.
        target_->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(plate.left + k, plate.top + 2 * k, plate.right + 2 * k, plate.bottom + 2.5f * k), 3 * k, 3 * k), solid_.Get());
        if (floppy_metal_) { at(floppy_metal_.Get(), 1.6f, 1); floppy_metal_->SetOpacity(alpha); target_->FillGeometry(shutter.Get(), floppy_metal_.Get()); }
        else { brush(gray(0.62f, alpha)); target_->FillGeometry(shutter.Get(), solid_.Get()); }
        target_->PushLayer(D2D1::LayerParameters(plate, shutter.Get()), nullptr);
        // Vertical falloff of the bent sheet, then the lamp: a broad band and the brushed metal's
        // long horizontal glint (brush lines stretch a reflection across them).
        if (media_edge_) {
            media_edge_->SetStartPoint({plate.left, plate.top}); media_edge_->SetEndPoint({plate.left, plate.bottom});
            media_edge_->SetOpacity(0.85f * alpha); target_->FillRectangle(plate, media_edge_.Get());
        }
        if (floppy_sheen_) {
            const float x = plate.left + pw * light;
            floppy_sheen_->SetStartPoint({x - pw * 0.42f, plate.top - ph * 0.6f});
            floppy_sheen_->SetEndPoint({x + pw * 0.22f, plate.bottom + ph * 0.6f});
            floppy_sheen_->SetOpacity(0.48f * alpha); target_->FillRectangle(plate, floppy_sheen_.Get());
        }
        if (floppy_glint_) {
            const auto centre = D2D1::Point2F(plate.left + pw * (light + 0.05f), plate.top + ph * 0.36f);
            floppy_glint_->SetCenter(centre); floppy_glint_->SetRadiusX(pw * 0.6f); floppy_glint_->SetRadiusY(ph * 0.11f);
            floppy_glint_->SetOpacity(0.5f * alpha); target_->FillRectangle(plate, floppy_glint_.Get());
            floppy_glint_->SetCenter({centre.x - pw * 0.08f, plate.top + ph * 0.80f});
            floppy_glint_->SetRadiusX(pw * 0.38f); floppy_glint_->SetRadiusY(ph * 0.05f);
            floppy_glint_->SetOpacity(0.14f * alpha); target_->FillRectangle(plate, floppy_glint_.Get());
        }
        target_->PopLayer();
        // Stamped edges: the fold along the top, a lit lower lip and the cut sides.
        line(plate.left + 2 * k, plate.top + k, plate.right - 2 * k, plate.top + k, gray(1, 0.55f * alpha), 1.2f * k);
        line(plate.left + 2 * k, plate.top + ph * 0.06f, plate.right - 2 * k, plate.top + ph * 0.06f, gray(0, 0.22f * alpha), k);
        line(plate.left + 3 * k, plate.bottom - 0.7f * k, plate.right - 3 * k, plate.bottom - 0.7f * k, gray(1, 0.40f * alpha), k);
        line(plate.left + 0.6f * k, plate.top + 2 * k, plate.left + 0.6f * k, plate.bottom - 3 * k, gray(1, 0.30f * alpha), k);
        line(plate.right - 0.6f * k, plate.top + 2 * k, plate.right - 0.6f * k, plate.bottom - 3 * k, gray(0, 0.35f * alpha), k);
        // The window: a pressed rim on the steel around the dark magnetic disk.
        const auto window = D2D1::RoundedRect(box(0.565f, 0.052f, 0.705f, 0.268f), 2.5f * k, 2.5f * k);
        brush(gray(1, 0.30f * alpha));
        target_->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(window.rect.left - 1.4f * k, window.rect.top - 1.4f * k, window.rect.right + 1.4f * k,
            window.rect.bottom + 1.4f * k), 3.5f * k, 3.5f * k), solid_.Get(), k);
        if (floppy_media_) {
            floppy_media_->SetStartPoint({window.rect.left, window.rect.top}); floppy_media_->SetEndPoint({window.rect.right, window.rect.bottom});
            floppy_media_->SetOpacity(alpha); target_->FillRoundedRectangle(window, floppy_media_.Get());
        }
        brush(D2D1::ColorF(0.24f, 0.17f, 0.10f, 0.35f * alpha)); target_->FillRoundedRectangle(window, solid_.Get()); // The brown oxide.
        line(window.rect.left + 2 * k, window.rect.top + 1.2f * k, window.rect.right - 2 * k, window.rect.top + 1.2f * k, gray(0, 0.85f * alpha), 2 * k);
        line(window.rect.left + k, window.rect.top + 2 * k, window.rect.left + k, window.rect.bottom - 2 * k, gray(0, 0.55f * alpha), 1.4f * k);
        line(window.rect.left + 2 * k, window.rect.bottom - 0.6f * k, window.rect.right - 2 * k, window.rect.bottom - 0.6f * k, gray(1, 0.18f * alpha), k);
    }
    // The label: off-white paper, the cover printed in its upper part with a narrow margin, the
    // game's name in blue ballpoint on the strip beneath, a faint gloss.
    const auto label = media_layout::label(media_layout::Kind::floppy, area);
    const auto paper = D2D1::RectF(label.left, label.top, label.right, label.bottom);
    Microsoft::WRL::ComPtr<ID2D1RoundedRectangleGeometry> sheet;
    if (FAILED(factory_->CreateRoundedRectangleGeometry(D2D1::RoundedRect(paper, 2.5f * k, 2.5f * k), sheet.GetAddressOf()))) return;
    brush(gray(0, 0.35f * alpha));
    target_->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(paper.left, paper.top + 1.5f * k, paper.right + k, paper.bottom + 1.5f * k), 3 * k, 3 * k), solid_.Get());
    brush(D2D1::ColorF(0.94f, 0.935f, 0.91f, alpha)); target_->FillGeometry(sheet.Get(), solid_.Get());
    const float pw2 = paper.right - paper.left, ph2 = paper.bottom - paper.top, margin = pw2 * 0.035f;
    const auto print = D2D1::RectF(paper.left + margin, paper.top + margin, paper.right - margin, paper.top + ph2 * 0.74f);
    if (art) fillImage(*art, print, 0, alpha * reveal(*art), frozen);
    else { brush(D2D1::ColorF(0.17f, 0.22f, 0.32f, alpha)); target_->FillRectangle(print, solid_.Get()); }
    // Ruled lines and the name.
    for (int rule = 1; rule <= 2; ++rule) {
        const float y = print.bottom + (paper.bottom - print.bottom) * rule / 3.0f;
        line(paper.left + margin, y, paper.right - margin, y, D2D1::ColorF(0.55f, 0.66f, 0.82f, 0.45f * alpha), 0.8f * k);
    }
    {
        D2D1_MATRIX_3X2_F base; target_->GetTransform(&base);
        const float strip = paper.bottom - print.bottom, size = std::max(1.0f, strip * 0.62f);
        const float factor = size / 23; // cd_pen_font_ is 23 px.
        const float x = paper.left + margin * 1.6f, y = print.bottom + strip * 0.52f;
        target_->SetTransform(D2D1::Matrix3x2F::Rotation(-1.5f) * D2D1::Matrix3x2F::Scale(factor, factor) * D2D1::Matrix3x2F::Translation(x, y) * base);
        textLine(name, 0, 0, (pw2 - margin * 3.2f) / factor, cd_pen_font_.Get(), D2D1::ColorF(0.08f, 0.16f, 0.40f, 0.92f * alpha));
        target_->SetTransform(base);
    }
    if (floppy_paper_) { at(floppy_paper_.Get(), 1, 1); floppy_paper_->SetOpacity(alpha); target_->FillGeometry(sheet.Get(), floppy_paper_.Get()); }
    if (floppy_sheen_) {
        floppy_sheen_->SetStartPoint({paper.left + pw2 * (light - 0.9f), paper.top});
        floppy_sheen_->SetEndPoint({paper.left + pw2 * (light + 0.5f), paper.bottom});
        floppy_sheen_->SetOpacity(0.05f * alpha); target_->FillGeometry(sheet.Get(), floppy_sheen_.Get());
    }
    brush(gray(1, 0.22f * alpha)); target_->DrawGeometry(sheet.Get(), solid_.Get(), 0.8f * k);
}

// A game card as the real ones look (Switch: dark grey, Switch 2: red; the DS, 3DS and Vita cards in
// their shells' colours): the moulded shell with its shoulders and the slot at its top, the edge of its
// thickness, the label (the console's band with its logo, the game's art, the product code strip)
// recessed a hair, and the small arrow under it.
void Renderer::drawGameCartridge(D2D1_RECT_F r, float alpha, const game_case::Kind& kind, const std::wstring& platform, Cover* art, bool frozen,
                                 const std::wstring& name) {
    namespace card = media_layout::card;
    const float w = r.right - r.left, h = r.bottom - r.top;
    if (w < 4 || alpha < 0.003f) return;
    const auto body = cardOutline(factory_.Get(), r);
    if (!body) return;
    createMediaMaterials();
    const auto look = package_look::of(kind);
    const auto shell = look.card;
    const auto mixed = [&](float k) { return D2D1::ColorF(shell.r * k + 0.03f * (1 - k), shell.g * k + 0.035f * (1 - k), shell.b * k + 0.035f * (1 - k), alpha); };
    const float u = w / 100;
    D2D1_MATRIX_3X2_F base; target_->GetTransform(&base);
    // A soft shadow, then the shell's thickness: the same outline, darker, a little down and right.
    for (int layer = 3; layer >= 1; --layer) {
        target_->SetTransform(D2D1::Matrix3x2F::Translation(u * (1.2f + layer), u * (2.2f + layer * 1.6f)) * base);
        brush(gray(0, 0.11f * alpha)); target_->FillGeometry(body.Get(), solid_.Get());
    }
    for (float step = 2.2f; step > 0.2f; step -= 0.5f) {
        target_->SetTransform(D2D1::Matrix3x2F::Translation(u * step * 0.5f, u * step) * base);
        brush(mixed(0.60f)); target_->FillGeometry(body.Get(), solid_.Get());
    }
    target_->SetTransform(base);
    brush(D2D1::ColorF(shell.r, shell.g, shell.b, alpha)); target_->FillGeometry(body.Get(), solid_.Get());
    if (media_grain_) {
        media_grain_->SetTransform(D2D1::Matrix3x2F::Scale(h / 470, h / 470) * D2D1::Matrix3x2F::Translation(r.left, r.top));
        media_grain_->SetOpacity(0.35f * alpha); target_->FillGeometry(body.Get(), media_grain_.Get());
    }
    // The slot moulded into the top: dark inside, its lower lip lit.
    {
        const auto slot = D2D1::RectF(r.left + w * card::slot_left, r.top + h * card::slot_top, r.left + w * card::slot_right, r.top + h * card::slot_bottom);
        const float sr = (slot.bottom - slot.top) * 0.35f;
        brush(D2D1::ColorF(shell.r * 0.45f, shell.g * 0.45f, shell.b * 0.45f, alpha)); target_->FillRoundedRectangle(D2D1::RoundedRect(slot, sr, sr), solid_.Get());
        line(slot.left + sr, slot.bottom + 0.4f * u, slot.right - sr, slot.bottom + 0.4f * u, gray(1, 0.22f * alpha), 0.6f * u);
    }
    // The arrow under the label, pointing down to the contacts.
    {
        const float ax = r.left + w * 0.5f, ay = r.top + h * card::arrow_y, size = w * card::arrow_size * 1.6f;
        const D2D1_POINT_2F arrow[3]{{ax - size, ay - size * 0.55f}, {ax + size, ay - size * 0.55f}, {ax, ay + size * 0.55f}};
        Microsoft::WRL::ComPtr<ID2D1PathGeometry> path;
        Microsoft::WRL::ComPtr<ID2D1GeometrySink> sink;
        if (SUCCEEDED(factory_->CreatePathGeometry(path.GetAddressOf())) && SUCCEEDED(path->Open(sink.GetAddressOf()))) {
            sink->BeginFigure(arrow[0], D2D1_FIGURE_BEGIN_FILLED); sink->AddLines(arrow + 1, 2); sink->EndFigure(D2D1_FIGURE_END_CLOSED);
            if (SUCCEEDED(sink->Close())) {
                brush(D2D1::ColorF(shell.r * 0.62f, shell.g * 0.62f, shell.b * 0.62f, alpha)); target_->FillGeometry(path.Get(), solid_.Get());
                brush(gray(1, 0.14f * alpha)); target_->DrawGeometry(path.Get(), solid_.Get(), 0.5f * u);
            }
        }
    }
    // The label, recessed a hair: a dark ring under it, a lit lower edge.
    const settings_ui::Rect area{r.left, r.top, r.right, r.bottom};
    const auto label = media_layout::label(media_layout::Kind::cartridge, area);
    const auto rect = D2D1::RectF(label.left, label.top, label.right, label.bottom);
    const float radius = 2.2f * u, lw = rect.right - rect.left, lh = rect.bottom - rect.top;
    Microsoft::WRL::ComPtr<ID2D1RoundedRectangleGeometry> print;
    if (FAILED(factory_->CreateRoundedRectangleGeometry(D2D1::RoundedRect(rect, radius, radius), print.GetAddressOf()))) return;
    brush(gray(0, 0.33f * alpha));
    target_->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(rect.left - 0.6f * u, rect.top - 0.6f * u, rect.right + 0.6f * u, rect.bottom + 0.6f * u),
                                                    radius, radius), solid_.Get(), 1.2f * u);
    line(rect.left + radius, rect.bottom + 1.1f * u, rect.right - radius, rect.bottom + 1.1f * u, gray(1, 0.19f * alpha), 0.6f * u);
    target_->PushLayer(D2D1::LayerParameters(rect, print.Get()), nullptr);
    brush(gray(0.08f, alpha)); target_->FillRectangle(rect, solid_.Get());
    // The console's band at its top, the product code at its foot, the game's art between.
    const float band_bottom = rect.top + lh * card::band, code_top = rect.bottom - lh * card::code;
    const auto art_box = D2D1::RectF(rect.left, band_bottom, rect.right, code_top);
    if (art) fillImage(*art, art_box, 0, alpha * reveal(*art), frozen);
    else text(name, art_box.left + 4 * u, art_box.top + (art_box.bottom - art_box.top) * 0.30f, lw - 8 * u, (art_box.bottom - art_box.top) * 0.5f,
              menu_font_.Get(), gray(0.92f, alpha));
    if (auto* band = packageBand(kind, platform, lw / std::max(1.0f, band_bottom - rect.top), package_look::Band::card)) {
        target_->DrawBitmap(band, D2D1::RectF(rect.left, rect.top, rect.right, band_bottom), alpha, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    }
    brush(gray(0.035f, alpha)); target_->FillRectangle(D2D1::RectF(rect.left, code_top, rect.right, rect.bottom), solid_.Get());
    if (menu_font_) {
        // A product code in Nintendo's form, made from the game's name ("LA-H-CELES-EUR").
        std::wstring letters;
        for (const wchar_t c : name) {
            if (letters.size() >= 5) break;
            if (c >= L'a' && c <= L'z') letters += static_cast<wchar_t>(c - L'a' + L'A');
            else if (c >= L'A' && c <= L'Z') letters += c;
        }
        while (letters.size() < 5) letters += L'A';
        const std::wstring code = kind.style == game_case::Style::switch2 ? L"LB-" + letters + L"-EUR-0" : L"LA-H-" + letters + L"-EUR";
        const float strip = rect.bottom - code_top, size = menu_font_->GetFontSize();
        const float scale = std::min(strip * 0.62f / size, lw * 0.80f / std::max(1.0f, textAdvance(code, menu_font_.Get())));
        target_->SetTransform(D2D1::Matrix3x2F::Scale(scale, scale) * D2D1::Matrix3x2F::Translation(rect.left + lw * 0.5f, code_top + strip * 0.5f) * base);
        textLine(code, -lw * 0.5f / scale, 0, lw / scale, menu_font_.Get(), gray(0.93f, alpha), DWRITE_TEXT_ALIGNMENT_CENTER);
        target_->SetTransform(base);
    }
    // The sticker's lamination: a soft streak of the lamp across it.
    createFloppyMaterials();
    if (floppy_sheen_) {
        floppy_sheen_->SetStartPoint({rect.left - w * 0.25f, rect.top}); floppy_sheen_->SetEndPoint({rect.right, rect.bottom + h * 0.1f});
        floppy_sheen_->SetOpacity(0.10f * alpha); target_->FillRectangle(rect, floppy_sheen_.Get());
    }
    target_->PopLayer();
    brush(gray(1, 0.10f * alpha)); target_->DrawGeometry(print.Get(), solid_.Get(), 0.6f * u);
    // The moulded face: lit from the top left, its bevel bright along the top and left, dark below.
    if (media_gloss_) {
        media_gloss_->SetStartPoint({r.left, r.top}); media_gloss_->SetEndPoint({r.left + w * 0.9f, r.top + h * 0.75f});
        media_gloss_->SetOpacity(alpha); target_->FillGeometry(body.Get(), media_gloss_.Get());
    }
    if (floppy_bevel_) {
        floppy_bevel_->SetStartPoint({r.left, r.top}); floppy_bevel_->SetEndPoint({r.right, r.bottom});
        floppy_bevel_->SetOpacity(0.85f * alpha); target_->DrawGeometry(body.Get(), floppy_bevel_.Get(), 1.4f * u);
    } else { brush(gray(1, 0.15f * alpha)); target_->DrawGeometry(body.Get(), solid_.Get(), 0.9f * u); }
}

// A pressed disc, label side up, as a DVD is built: the clear centre with its hole (15 of 120 mm) and
// stacking ring, the mirror band of the data layer around it, the printed label from 0.34 to 0.975 of
// its radius (the poster, or a scan of the real disc instead), the clear outer edge. The print turns
// (`spin`, degrees); the light does not: a bow-tie sheen on the lacquer, the lamp's falloff towards the
// foot, and a narrow rainbow only where the mirror shows.
void Renderer::drawGameDisc(float cx, float cy, float radius, float alpha, float spin, Cover* art, Cover* scan, bool frozen, const std::wstring& name) {
    if (radius < 2 || alpha < 0.003f) return;
    createMediaMaterials();
    const float scanned = scan ? reveal(*scan) : 0;
    const float hole = radius * 0.125f, clamp = radius * 0.275f, mirror = radius * 0.335f, edge = radius * 0.975f;
    D2D1_MATRIX_3X2_F base; target_->GetTransform(&base);
    const auto bounds = D2D1::RectF(cx - radius, cy - radius, cx + radius, cy + radius);
    // Its shadow: soft, a little below (the lamp is above).
    for (int layer = 3; layer >= 1; --layer) {
        const float spread = radius * 0.025f * layer;
        brush(gray(0, 0.11f * alpha));
        target_->FillEllipse(D2D1::Ellipse({cx + radius * 0.015f, cy + radius * 0.045f}, radius + spread, radius + spread), solid_.Get());
    }
    const auto body = ring(factory_.Get(), cx, cy, radius, hole);
    const auto label = ring(factory_.Get(), cx, cy, edge, mirror);
    const auto band = ring(factory_.Get(), cx, cy, mirror, clamp);
    const auto centre = ring(factory_.Get(), cx, cy, clamp, hole);
    if (!body || !label || !band || !centre) return;
    // The clear polycarbonate: the outer edge and the centre, the floor faintly through them.
    brush(D2D1::ColorF(0.70f, 0.73f, 0.77f, 0.42f * alpha)); target_->FillGeometry(body.Get(), solid_.Get());
    brush(D2D1::ColorF(0.86f, 0.88f, 0.91f, 0.30f * alpha)); target_->FillGeometry(centre.Get(), solid_.Get());
    // The mirror band: the data layer's silver, lit from above.
    if (disc_metal_) {
        disc_metal_->SetStartPoint({cx - radius * 0.4f, cy - radius * 0.4f}); disc_metal_->SetEndPoint({cx + radius * 0.4f, cy + radius * 0.4f});
        disc_metal_->SetOpacity(alpha); target_->FillGeometry(band.Get(), disc_metal_.Get());
    } else { brush(gray(0.78f, alpha)); target_->FillGeometry(band.Get(), solid_.Get()); }
    // The print, turning with the disc.
    target_->SetTransform(D2D1::Matrix3x2F::Rotation(spin, {cx, cy}) * base);
    if (scan && scanned > 0.001f) fillImage(*scan, bounds, 0, alpha * scanned, frozen, body.Get(), true);
    if (scanned < 0.999f) {
        target_->PushLayer(D2D1::LayerParameters(bounds, label.Get()), nullptr);
        brush(gray(0.94f, alpha * (1 - scanned))); target_->FillRectangle(bounds, solid_.Get()); // The white base coat.
        if (art) fillImage(*art, bounds, 0, alpha * (1 - scanned) * reveal(*art), frozen);
        else {
            brush(D2D1::ColorF(0.16f, 0.22f, 0.34f, alpha * (1 - scanned))); target_->FillRectangle(bounds, solid_.Get());
            textLine(name, cx - radius * 0.7f, cy - radius * 0.62f, radius * 1.4f, menu_font_.Get(), gray(0.92f, alpha * (1 - scanned)), DWRITE_TEXT_ALIGNMENT_CENTER);
        }
        target_->PopLayer();
    }
    target_->SetTransform(base);
    // The light, still while the disc turns under it.
    if (disc_wedge_) {
        constexpr int slices = 60;
        constexpr float pi = 3.14159265f;
        const float lamp = -0.62f; // The sheen's axis: from the upper left to the lower right.
        const auto wedge = [&](ID2D1Geometry* clip, float reach, const std::function<D2D1_COLOR_F(float)>& colour) {
            target_->PushLayer(D2D1::LayerParameters(bounds, clip), nullptr);
            for (int i = 0; i < slices; ++i) {
                const float a = (i + 0.5f) * 2 * pi / slices;
                auto ink = colour(a);
                if (ink.a < 0.004f) continue;
                ink.a *= alpha;
                target_->SetTransform(D2D1::Matrix3x2F::Scale(reach, reach) * D2D1::Matrix3x2F::Rotation(i * 6.0f) * D2D1::Matrix3x2F::Translation(cx, cy) * base);
                brush(ink); target_->FillGeometry(disc_wedge_.Get(), solid_.Get());
            }
            target_->SetTransform(base);
            target_->PopLayer();
        };
        // The lacquer's sheen: two opposite lobes along the lamp's axis.
        wedge(label.Get(), edge, [&](float a) {
            const float c = std::abs(std::cos(a - lamp));
            return gray(1, 0.20f * std::pow(c, 14.0f) + 0.035f * c * c);
        });
        // The mirror band and the clear edge split the light into colours across the same axis.
        const auto rainbow = [&](float a, float strength) {
            const float across = a - lamp + pi * 0.5f;
            const float c = std::pow(std::abs(std::cos(a - lamp)), 3.0f);
            const float hue = std::fmod(std::abs(across) * 3 / pi * 2 + 6, 6.0f);
            const auto channel = [&](float offset) { return std::clamp(std::abs(std::fmod(hue + offset, 6.0f) - 3) - 1, 0.0f, 1.0f); };
            return D2D1::ColorF(channel(0), channel(4), channel(2), strength * c);
        };
        wedge(band.Get(), mirror, [&](float a) { return rainbow(a, 0.38f); });
        if (const auto rim = ring(factory_.Get(), cx, cy, radius, edge)) wedge(rim.Get(), radius, [&](float a) { return rainbow(a, 0.30f); });
    }
    // The lamp above: the disc's lower half a little in shade.
    if (media_edge_) {
        media_edge_->SetStartPoint({cx, cy - radius}); media_edge_->SetEndPoint({cx, cy + radius});
        media_edge_->SetOpacity(0.35f * alpha); target_->FillGeometry(body.Get(), media_edge_.Get());
    }
    // The moulded centre: the stacking ring, the label's inner edge, the hole's lip.
    brush(gray(1, 0.35f * alpha)); target_->DrawEllipse(D2D1::Ellipse({cx, cy - 0.5f}, radius * 0.215f, radius * 0.215f), solid_.Get(), std::max(0.8f, radius * 0.012f));
    brush(gray(0, 0.20f * alpha)); target_->DrawEllipse(D2D1::Ellipse({cx, cy + 0.5f}, radius * 0.215f, radius * 0.215f), solid_.Get(), std::max(0.8f, radius * 0.008f));
    brush(gray(0, 0.25f * alpha)); target_->DrawEllipse(D2D1::Ellipse({cx, cy}, mirror, mirror), solid_.Get(), 0.8f);
    brush(gray(1, 0.18f * alpha)); target_->DrawEllipse(D2D1::Ellipse({cx, cy}, clamp, clamp), solid_.Get(), 0.8f);
    brush(gray(1, 0.55f * alpha)); target_->DrawEllipse(D2D1::Ellipse({cx, cy - 0.4f}, hole + 0.5f, hole + 0.5f), solid_.Get(), 1);
    brush(gray(0, 0.35f * alpha)); target_->DrawEllipse(D2D1::Ellipse({cx, cy + 0.6f}, hole + 1.2f, hole + 1.2f), solid_.Get(), 0.8f);
    // The rim: a lit edge, darker below.
    brush(gray(1, 0.55f * alpha)); target_->DrawEllipse(D2D1::Ellipse({cx, cy - 0.4f}, radius - 0.6f, radius - 0.6f), solid_.Get(), 1.0f);
    brush(gray(0, 0.30f * alpha)); target_->DrawEllipse(D2D1::Ellipse({cx, cy}, edge, edge), solid_.Get(), 0.8f);
}
