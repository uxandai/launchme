#include "renderer.hpp"
#include "coverflow_layout.hpp"
#include "media_layout.hpp"
#include "occlusion_mask.hpp"
#include "steelbook_material.hpp"
#include "../library/steam_stats.hpp"
#include <d2d1effects.h>
#include <cmath>
#include <initializer_list>
#include <vector>

namespace {
using palette::gray;
// Strong material preview: a broad, visibly rough reflection instead of the previous
// almost invisible polished highlight. Roughness controls the Blinn-Phong lobe width.
constexpr float metal_roughness = 0.40f;
constexpr float metal_reflection = 1.30f;
constexpr UINT streak_width = 256, streak_height = 64; // Shared smooth brushed-streak tile.
// Dyskietka's procedural tiles: a hash, and smooth value noise that wraps every `period` texels.
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
bool visible(const GameCard& card, float width) {
    return card.size > 1 && card.opacity > 0.005f && card.x < width + 20 && card.x + card.width() * 1.3f > -20;
}
D2D1_RECT_F posterRect(const GameCard& card, float launch) {
    const float grow = card.size * 0.025f * launch;
    return D2D1::RectF(card.x - grow * card.aspect, card.y - grow,
        card.x + card.width() + grow * card.aspect, card.y + card.size + grow);
}
}

void Renderer::drawLibraryCoverflow() {
    if (!cards_ || selected_ >= cards_->size()) return;
    if (context_->library_mode == library_view::View::bookshelf) { drawLibraryShelf(); return; } // Its own perspective pass.
    const coverflow_layout::Layout layout{viewport_.width, viewport_.height, context_->library_mode, context_->library_options.coverflow_cd};
    const bool cd = context_->library_mode == library_view::View::coverflow && context_->library_options.coverflow_cd;
    const bool media = library_view::media(context_->library_mode);
    target_->PushAxisAlignedClip(D2D1::RectF(0, 180, viewport_.width, viewport_.height), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    // Discs are their own background pass: even a retracting disc cannot cover a neighbour.
    // All posters follow, and the selected poster is always on top throughout a reversal.
    if (!media && std::any_of(cards_->begin(), cards_->end(), [&](const auto& card) {
            return card.disc >= 0.003f && visible(card, viewport_.width);
        })) {
        // Cut the cases out of the disc pass, including during the initial card fade and
        // artwork replacement. Use their animated silhouettes, not their resting positions.
        std::vector<Microsoft::WRL::ComPtr<ID2D1Geometry>> cases;
        const float previous_corner = squircle_corner_;
        squircle_corner_ = 0.2f;
        for (size_t i = 0; i < cards_->size(); ++i) if (visible((*cards_)[i], viewport_.width)) {
            const auto rect = posterRect((*cards_)[i], i == selected_ ? context_->launch_progress : 0);
            if (cd) {
                Microsoft::WRL::ComPtr<ID2D1RoundedRectangleGeometry> body;
                if (SUCCEEDED(factory_->CreateRoundedRectangleGeometry(D2D1::RoundedRect(rect, 3, 3), body.GetAddressOf()))) cases.push_back(body);
            } else cases.push_back(tileShape(rect, tile_shape_));
        }
        squircle_corner_ = previous_corner;
        const auto bounds = D2D1::RectF(0, 180, viewport_.width, viewport_.height);
        const auto mask = occlusionMask(factory_.Get(), bounds, cases);
        if (mask) {
            target_->PushLayer(D2D1::LayerParameters(bounds, mask.Get()), nullptr);
            for (size_t i = cards_->size(); i-- > 0;) drawCoverflowDisc((*cards_)[i], i);
            target_->PopLayer();
        }
    }
    const bool floppy = context_->library_mode == library_view::View::floppy;
    const auto draw = [&](size_t i) {
        if (floppy) drawFloppy((*cards_)[i], i);
        else if (media) drawMediaCard((*cards_)[i], i);
        else if (cd) drawCdCase((*cards_)[i], i);
        else drawCoverflowPoster((*cards_)[i], i);
    };
    for (size_t i = cards_->size(); i-- > 0;) if (i != selected_) draw(i);
    if (context_->launch_progress > 0.001f) {
        brush(D2D1::ColorF(0.008f, 0.012f, 0.016f, context_->launch_progress * 0.82f));
        target_->FillRectangle(D2D1::RectF(0, 180, viewport_.width, viewport_.height), solid_.Get());
    }
    draw(selected_);
    target_->PopAxisAlignedClip();
    const float fade = std::clamp(detail_ * 2, 0.0f, 1.0f);
    const float bottom = std::min(viewport_.height - 88, layout.top() + layout.coverHeight() + 50);
    const auto& game = (*cards_)[selected_].game;
    const float right = viewport_.width - 68;
    // Where the focus is in the row ("12 / 140"), right-aligned with the title.
    const auto position = std::to_wstring(selected_ + 1) + L" / " + std::to_wstring(cards_->size());
    const float position_width = textAdvance(position, settings_body_font_.Get());
    fadingLine(game.name, layout.left(), bottom + 12 * (1 - fade), right - layout.left() - position_width - 24,
               settings_heading_font_.Get(), gray(0.97f, fade), true);
    textLine(position, right - position_width, bottom + 12 * (1 - fade), position_width + 1, settings_body_font_.Get(), gray(0.6f, fade),
             DWRITE_TEXT_ALIGNMENT_TRAILING);
    // The platform's logo under the title (its name while there is none), then what Półka and
    // the lists say about the game: year · developer · playtime, and whether it needs installing.
    const float facts_y = bottom + 46 + 12 * (1 - fade);
    const float mark = drawPlatformMark(activePlatformName(game), layout.left(), facts_y, 26,
                                        std::min(260.0f, right - layout.left()), fade, settings_body_font_.Get());
    std::wstring facts;
    const auto add = [&](const std::wstring& value) { if (!value.empty()) facts += (facts.empty() ? L"" : L"   ·   ") + value; };
    if (const auto* info = context_->preview; info && !context_->preview_waiting) {
        add(gameReleaseYear(info->release_date));
        add(info->developer);
        if (info->playtime_minutes && *info->playtime_minutes) add(formatPlaytime(*info->playtime_minutes));
    }
    if (game.installation.active()) add(installationText(game.installation));
    else if (!game.installed && game.kind != GameKind::catalog && sessionOf(game) == SessionState::none)
        add(localFileGame(game.source) ? std::wstring(missingFileLabel(game)) : std::wstring(tr(L"Do instalacji")));
    const float facts_left = layout.left() + (mark > 0 ? mark + 20 : 0);
    if (!facts.empty() && right - facts_left > 80)
        textLine(facts, facts_left, facts_y, right - facts_left, settings_body_font_.Get(), gray(0.78f, fade));
}

void Renderer::createMediaMaterials() {
    const auto gradient = [&](Microsoft::WRL::ComPtr<ID2D1LinearGradientBrush>& brush, const D2D1_GRADIENT_STOP* stops, UINT count) {
        if (brush) return;
        Microsoft::WRL::ComPtr<ID2D1GradientStopCollection> colors;
        if (SUCCEEDED(target_->CreateGradientStopCollection(stops, count, colors.GetAddressOf())))
            target_->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(D2D1::Point2F(0, 0), D2D1::Point2F(1, 1)), colors.Get(), brush.GetAddressOf());
    };
    const D2D1_GRADIENT_STOP plastic[] = {{0, gray(0.12f)}, {0.24f, gray(0.075f)}, {0.72f, gray(0.038f)}, {1, gray(0.070f)}};
    const D2D1_GRADIENT_STOP edge[] = {{0, gray(1, 0.22f)}, {0.45f, gray(1, 0.055f)}, {0.55f, gray(0, 0.25f)}, {1, gray(0, 0.48f)}};
    gradient(media_plastic_, plastic, 4); gradient(media_edge_, edge, 4);
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
}

namespace {
Microsoft::WRL::ComPtr<ID2D1PathGeometry> mediaOutline(ID2D1Factory* factory, library_view::View mode, settings_ui::Rect r) {
    const auto points = media_layout::outline(mode, r);
    Microsoft::WRL::ComPtr<ID2D1PathGeometry> body;
    Microsoft::WRL::ComPtr<ID2D1GeometrySink> sink;
    if (FAILED(factory->CreatePathGeometry(body.GetAddressOf())) || FAILED(body->Open(sink.GetAddressOf()))) return {};
    sink->BeginFigure(D2D1::Point2F(points.points[0].x, points.points[0].y), D2D1_FIGURE_BEGIN_FILLED);
    for (size_t i = 1; i < points.count; ++i) sink->AddLine(D2D1::Point2F(points.points[i].x, points.points[i].y));
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    return SUCCEEDED(sink->Close()) ? body : Microsoft::WRL::ComPtr<ID2D1PathGeometry>{};
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
// that catches the lamp as the disk glides (and slowly with the ambient clock), and a paper label
// with the square cover.
void Renderer::drawFloppy(const GameCard& card, size_t index) {
    if (!visible(card, viewport_.width)) return;
    const auto r = coverflow_layout::poster(card);
    const float w = card.width(), h = card.size, alpha = card.opacity;
    const auto body = mediaOutline(factory_.Get(), library_view::View::floppy, r);
    if (!body || w < 4) return;
    createMediaMaterials();
    createFloppyMaterials();
    const auto box = [&](float l, float t, float rr, float b) { return D2D1::RectF(r.left + w * l, r.top + h * t, r.left + w * rr, r.top + h * b); };
    const float texel = 1 / std::max(1.0f, viewport_.scale); // Tiles at about one texel per device pixel.
    const auto at = [&](ID2D1BitmapBrush* tile, float stretch_x, float stretch_y) {
        tile->SetTransform(D2D1::Matrix3x2F::Scale(texel * stretch_x, texel * stretch_y) * D2D1::Matrix3x2F::Translation(r.left, r.top));
    };
    // The lamp: its reflection slides over the disk the other way while it glides, and drifts slowly.
    const coverflow_layout::Layout layout{viewport_.width, viewport_.height, library_view::View::floppy};
    const float light = 0.5f + 0.16f * std::sin(elapsed_ * 0.30f + static_cast<float>(index) * 0.7f) + 0.10f * card.emphasis -
        0.55f * layout.lightSweep(card, index, selected_);
    // A soft shadow under the disk.
    for (int layer = 3; layer >= 1; --layer) {
        const float spread = layer * 3.5f;
        brush(gray(0, 0.075f * alpha));
        target_->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(r.left - spread * 0.6f, r.top + 6 - spread * 0.4f,
            r.right + spread * 0.6f, r.bottom + 8 + spread), 8 + spread, 8 + spread), solid_.Get());
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
        line(b.left + shape.radiusX, b.top + 0.8f, b.right - shape.radiusX, b.top + 0.8f, gray(0, 0.55f * alpha), 1.6f);
        line(b.left + 0.8f, b.top + shape.radiusY, b.left + 0.8f, b.bottom - shape.radiusY, gray(0, 0.35f * alpha), 1.2f);
        line(b.left + shape.radiusX, b.bottom - 0.6f, b.right - shape.radiusX, b.bottom - 0.6f, gray(1, 0.13f * alpha), 1);
        line(b.right - 0.6f, b.top + shape.radiusY, b.right - 0.6f, b.bottom - shape.radiusY, gray(1, 0.07f * alpha), 1);
    };
    recess(D2D1::RoundedRect(box(0.205f, -0.01f, 0.815f, 0.335f), 3, 3), 0.20f); // The shutter's track.
    recess(D2D1::RoundedRect(box(0.165f, 0.338f, 0.835f, 1.01f), 4, 4), 0.16f);   // Under the label.
    // The insertion arrow, pressed into the top left; write-protect and density holes at the bottom.
    {
        const D2D1_POINT_2F arrow[] = {{r.left + w * 0.105f, r.top + h * 0.045f}, {r.left + w * 0.135f, r.top + h * 0.095f},
                                       {r.left + w * 0.075f, r.top + h * 0.095f}};
        for (int edge = 0; edge < 3; ++edge)
            line(arrow[edge].x, arrow[edge].y, arrow[(edge + 1) % 3].x, arrow[(edge + 1) % 3].y,
                 edge == 1 ? gray(1, 0.16f * alpha) : gray(0, 0.6f * alpha), 1.4f);
    }
    for (const float x : {0.055f, 0.913f}) {
        const auto hole = box(x, 0.895f, x + 0.032f, 0.958f);
        brush(gray(0.006f, alpha)); target_->FillRectangle(hole, solid_.Get());
        line(hole.left, hole.bottom, hole.right, hole.bottom, gray(1, 0.14f * alpha), 1);
        line(hole.left, hole.top + 0.5f, hole.right, hole.top + 0.5f, gray(0, 0.7f * alpha), 1.2f);
    }
    // The moulded rim: lit at the top left, in shade at the bottom right; a second, inner lip.
    if (floppy_bevel_) {
        floppy_bevel_->SetStartPoint({r.left, r.top}); floppy_bevel_->SetEndPoint({r.right, r.bottom});
        floppy_bevel_->SetOpacity(alpha); target_->DrawGeometry(body.Get(), floppy_bevel_.Get(), 2.2f);
        D2D1_MATRIX_3X2_F base; target_->GetTransform(&base);
        target_->SetTransform(D2D1::Matrix3x2F::Scale(0.986f, 0.986f, D2D1::Point2F(r.left + w * 0.5f, r.top + h * 0.5f)) * base);
        floppy_bevel_->SetOpacity(alpha * 0.45f); target_->DrawGeometry(body.Get(), floppy_bevel_.Get(), 1);
        target_->SetTransform(base);
    }
    // The shutter: brushed steel bent over the top edge, its window showing the plastic beneath.
    const auto plate = box(0.235f, 0, 0.775f, 0.318f);
    const float pw = plate.right - plate.left, ph = plate.bottom - plate.top;
    Microsoft::WRL::ComPtr<ID2D1RoundedRectangleGeometry> shutter;
    if (SUCCEEDED(factory_->CreateRoundedRectangleGeometry(D2D1::RoundedRect(plate, 2.5f, 2.5f), shutter.GetAddressOf()))) {
        brush(gray(0, 0.45f * alpha)); // Its edge's shadow on the track.
        target_->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(plate.left + 1, plate.top + 2, plate.right + 2, plate.bottom + 2.5f), 3, 3), solid_.Get());
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
            floppy_glint_->SetOpacity((0.42f + 0.18f * card.emphasis) * alpha); target_->FillRectangle(plate, floppy_glint_.Get());
            floppy_glint_->SetCenter({centre.x - pw * 0.08f, plate.top + ph * 0.80f});
            floppy_glint_->SetRadiusX(pw * 0.38f); floppy_glint_->SetRadiusY(ph * 0.05f);
            floppy_glint_->SetOpacity(0.14f * alpha); target_->FillRectangle(plate, floppy_glint_.Get());
        }
        target_->PopLayer();
        // Stamped edges: the fold along the top, a lit lower lip and the cut sides.
        line(plate.left + 2, plate.top + 1, plate.right - 2, plate.top + 1, gray(1, 0.55f * alpha), 1.2f);
        line(plate.left + 2, plate.top + ph * 0.06f, plate.right - 2, plate.top + ph * 0.06f, gray(0, 0.22f * alpha), 1);
        line(plate.left + 3, plate.bottom - 0.7f, plate.right - 3, plate.bottom - 0.7f, gray(1, 0.40f * alpha), 1);
        line(plate.left + 0.6f, plate.top + 2, plate.left + 0.6f, plate.bottom - 3, gray(1, 0.30f * alpha), 1);
        line(plate.right - 0.6f, plate.top + 2, plate.right - 0.6f, plate.bottom - 3, gray(0, 0.35f * alpha), 1);
        // The window: a pressed rim on the steel around a dark opening.
        const auto window = D2D1::RoundedRect(box(0.565f, 0.052f, 0.705f, 0.268f), 2.5f, 2.5f);
        brush(gray(1, 0.30f * alpha));
        target_->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(window.rect.left - 1.4f, window.rect.top - 1.4f, window.rect.right + 1.4f,
            window.rect.bottom + 1.4f), 3.5f, 3.5f), solid_.Get(), 1);
        if (floppy_media_) {
            floppy_media_->SetStartPoint({window.rect.left, window.rect.top}); floppy_media_->SetEndPoint({window.rect.right, window.rect.bottom});
            floppy_media_->SetOpacity(alpha); target_->FillRoundedRectangle(window, floppy_media_.Get());
        }
        if (floppy_plastic_) target_->FillRoundedRectangle(window, floppy_plastic_.Get());
        line(window.rect.left + 2, window.rect.top + 1.2f, window.rect.right - 2, window.rect.top + 1.2f, gray(0, 0.85f * alpha), 2);
        line(window.rect.left + 1, window.rect.top + 2, window.rect.left + 1, window.rect.bottom - 2, gray(0, 0.55f * alpha), 1.4f);
        line(window.rect.left + 2, window.rect.bottom - 0.6f, window.rect.right - 2, window.rect.bottom - 0.6f, gray(1, 0.18f * alpha), 1);
    }
    // The label: paper with the square cover printed on it, a narrow white margin, faint gloss.
    const auto label = media_layout::label(library_view::View::floppy, r);
    const auto paper = D2D1::RectF(label.left, label.top, label.right, label.bottom);
    Microsoft::WRL::ComPtr<ID2D1RoundedRectangleGeometry> sheet;
    if (FAILED(factory_->CreateRoundedRectangleGeometry(D2D1::RoundedRect(paper, 3, 3), sheet.GetAddressOf()))) return;
    brush(gray(0, 0.35f * alpha));
    target_->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(paper.left, paper.top + 1.5f, paper.right + 1, paper.bottom + 1.5f), 3, 3), solid_.Get());
    brush(D2D1::ColorF(0.93f, 0.925f, 0.90f, alpha)); target_->FillGeometry(sheet.Get(), solid_.Get());
    const float margin = (paper.right - paper.left) * 0.035f;
    const auto print = D2D1::RectF(paper.left + margin, paper.top + margin, paper.right - margin, paper.bottom - margin);
    auto* image = index < covers_.size() && covers_[index].bitmap ? &covers_[index] : nullptr;
    if (image) fillImage(*image, print, 1.5f, alpha * reveal(*image), sessionOf(card.game) == SessionState::frozen);
    else text(card.game.name, print.left + 8, print.top + (print.bottom - print.top) * 0.40f, print.right - print.left - 16,
              (print.bottom - print.top) * 0.5f, menu_font_.Get(), gray(0.15f, alpha));
    if (floppy_paper_) { at(floppy_paper_.Get(), 1, 1); floppy_paper_->SetOpacity(alpha); target_->FillGeometry(sheet.Get(), floppy_paper_.Get()); }
    if (floppy_sheen_) {
        floppy_sheen_->SetStartPoint({paper.left + (paper.right - paper.left) * (light - 0.9f), paper.top});
        floppy_sheen_->SetEndPoint({paper.left + (paper.right - paper.left) * (light + 0.5f), paper.bottom});
        floppy_sheen_->SetOpacity(0.05f * alpha); target_->FillGeometry(sheet.Get(), floppy_sheen_.Get());
    }
    brush(gray(1, 0.22f * alpha)); target_->DrawGeometry(sheet.Get(), solid_.Get(), 0.8f);
    if (!card.game.installed) { brush(gray(0, 0.24f * alpha)); target_->FillGeometry(body.Get(), solid_.Get()); }
}

// Kartridż Switch: black plastic, a recessed 2:3 poster label.
void Renderer::drawMediaCard(const GameCard& card, size_t index) {
    if (!visible(card, viewport_.width)) return;
    const auto mode = library_view::View::cartridge_switch;
    const auto r = coverflow_layout::poster(card);
    const float w = card.width(), h = card.size, alpha = card.opacity;
    const auto body = mediaOutline(factory_.Get(), mode, r);
    if (!body) return;
    createMediaMaterials();
    if (media_plastic_) {
        media_plastic_->SetStartPoint(D2D1::Point2F(r.left, r.top)); media_plastic_->SetEndPoint(D2D1::Point2F(r.right, r.bottom)); media_plastic_->SetOpacity(alpha);
        target_->FillGeometry(body.Get(), media_plastic_.Get());
    } else { brush(gray(0.065f, alpha)); target_->FillGeometry(body.Get(), solid_.Get()); }
    if (media_grain_) {
        media_grain_->SetTransform(D2D1::Matrix3x2F::Scale(h / 470, h / 470) * D2D1::Matrix3x2F::Translation(r.left, r.top));
        media_grain_->SetOpacity(0.45f * alpha); target_->FillGeometry(body.Get(), media_grain_.Get());
    }
    if (media_edge_) {
        media_edge_->SetStartPoint(D2D1::Point2F(r.left, r.top)); media_edge_->SetEndPoint(D2D1::Point2F(r.right, r.bottom));
        media_edge_->SetOpacity(alpha); target_->DrawGeometry(body.Get(), media_edge_.Get(), 1.5f);
        D2D1_MATRIX_3X2_F base; target_->GetTransform(&base);
        target_->SetTransform(D2D1::Matrix3x2F::Scale(0.982f, 0.987f, D2D1::Point2F(r.left + w * 0.5f, r.top + h * 0.5f)) * base);
        media_edge_->SetOpacity(alpha * 0.55f); target_->DrawGeometry(body.Get(), media_edge_.Get(), 1);
        target_->SetTransform(base);
    }
    const auto box = [&](float l, float t, float rr, float b) { return D2D1::RectF(r.left + w * l, r.top + h * t, r.left + w * rr, r.top + h * b); };
    brush(gray(0.013f, alpha));
    target_->FillRoundedRectangle(D2D1::RoundedRect(box(0.07f, 0.055f, 0.92f, 0.951f), 5, 5), solid_.Get());
    line(r.left + w * 0.29f, r.top + h * 0.028f, r.left + w * 0.69f, r.top + h * 0.028f, gray(0.15f, alpha), 2);
    const auto label = media_layout::label(mode, r);
    const auto rect = D2D1::RectF(label.left, label.top, label.right, label.bottom);
    Microsoft::WRL::ComPtr<ID2D1RoundedRectangleGeometry> print;
    if (FAILED(factory_->CreateRoundedRectangleGeometry(D2D1::RoundedRect(rect, 3, 3), print.GetAddressOf()))) return;
    brush(gray(0.045f, alpha)); target_->FillGeometry(print.Get(), solid_.Get());
    auto* image = index < posters_.size() && posters_[index].bitmap ? &posters_[index] :
                  index < covers_.size() && covers_[index].bitmap ? &covers_[index] : nullptr;
    if (image) fillImage(*image, rect, 3, alpha * reveal(*image), sessionOf(card.game) == SessionState::frozen, print.Get(), true);
    else text(card.game.name, label.left + 10, label.top + (label.bottom - label.top) * 0.42f, label.right - label.left - 20,
        (label.bottom - label.top) * 0.5f, menu_font_.Get(), gray(0.90f, alpha));
    brush(gray(0.65f, 0.20f * alpha)); target_->DrawGeometry(print.Get(), solid_.Get(), 1);
}

// A tiny copy of the art (Półka behind a square cover, desktop banners); drawn enlarged with linear filtering it is a soft blur (made once
// per bitmap, a high-quality downscale where the target allows it).
ID2D1Bitmap* Renderer::blurredArt(Cover& image) {
    if (!image.bitmap) return nullptr;
    if (image.blurred && image.blurred_source == image.bitmap) return image.blurred.Get();
    image.blurred.Reset(); image.blurred_source.Reset();
    const auto size = image.bitmap->GetSize();
    if (size.width < 1 || size.height < 1) return nullptr;
    const float scale = 20.0f / std::max(size.width, size.height);
    const auto tiny = D2D1::SizeF(std::max(2.0f, std::round(size.width * scale)), std::max(2.0f, std::round(size.height * scale)));
    Microsoft::WRL::ComPtr<ID2D1BitmapRenderTarget> surface;
    if (FAILED(target_->CreateCompatibleRenderTarget(tiny, D2D1::SizeU(static_cast<UINT32>(tiny.width), static_cast<UINT32>(tiny.height)),
                                                     surface.GetAddressOf()))) return nullptr;
    surface->BeginDraw();
    surface->Clear(D2D1::ColorF(0, 0, 0, 0));
    const auto destination = D2D1::RectF(0, 0, tiny.width, tiny.height);
    Microsoft::WRL::ComPtr<ID2D1DeviceContext> context;
    if (SUCCEEDED(surface.As(&context)))
        context->DrawBitmap(image.bitmap.Get(), &destination, 1, D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC, nullptr, nullptr);
    else surface->DrawBitmap(image.bitmap.Get(), destination, 1, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    if (FAILED(surface->EndDraw()) || FAILED(surface->GetBitmap(image.blurred.GetAddressOf()))) { image.blurred.Reset(); return nullptr; }
    image.blurred_source = image.bitmap;
    return image.blurred.Get();
}

void Renderer::drawCoverflowDisc(const GameCard& card, size_t index) {
    if (card.disc < 0.003f || !visible(card, viewport_.width)) return;
    auto* image = index < posters_.size() && posters_[index].bitmap ? &posters_[index] :
                  index < covers_.size() && covers_[index].bitmap ? &covers_[index] : nullptr;
    const auto accent = image ? image->accent : D2D1::ColorF(0.24f, 0.39f, 0.48f);
    const auto disc = coverflow_layout::disc(card);
    auto* scan = index < discs_.size() && discs_[index].bitmap ? &discs_[index] : nullptr;
    const float scanned = scan ? reveal(*scan) : 0;
    const float radius = disc.radius, cx = disc.x, cy = disc.y, alpha = card.opacity * (1 - scanned);
    const bool cd = context_->library_options.coverflow_cd;
    const auto circle = D2D1::Ellipse(D2D1::Point2F(cx, cy), radius, radius);
    brush(gray(0.018f, 0.5f * card.opacity));
    target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx + 4, cy + 7), radius + 3, radius + 3), solid_.Get());
    brush(gray(0.78f, alpha)); target_->FillEllipse(circle, solid_.Get());
    Microsoft::WRL::ComPtr<ID2D1EllipseGeometry> shape;
    factory_->CreateEllipseGeometry(circle, shape.GetAddressOf());
    if (cd) {
        // Printable white CD-R: a quiet label, transparent hub and blue ballpoint handwriting.
        brush(gray(0.95f, alpha)); target_->FillEllipse(circle, solid_.Get());
        D2D1_MATRIX_3X2_F base; target_->GetTransform(&base);
        target_->SetTransform(D2D1::Matrix3x2F::Scale(radius / 160, radius / 160) *
            D2D1::Matrix3x2F::Translation(cx, cy) * D2D1::Matrix3x2F::Rotation(elapsed_ * 42, {cx, cy}) * base);
        textLine(L"esperanza", -108, 66, 216, menu_font_.Get(), gray(0.43f, alpha), DWRITE_TEXT_ALIGNMENT_CENTER);
        textLine(L"CD-R  700 MB / 80 min", -112, 92, 224, label_font_.Get(), gray(0.56f, alpha), DWRITE_TEXT_ALIGNMENT_CENTER);
        line(-95, 106, 95, 106, gray(0.64f, alpha), 0.7f);
        D2D1_MATRIX_3X2_F disc_base; target_->GetTransform(&disc_base);
        target_->SetTransform(D2D1::Matrix3x2F::Rotation(-8, {0, -68}) * disc_base);
        paragraph(card.game.name, -114, -108, 228, cd_pen_font_.Get(), D2D1::ColorF(0.08f, 0.16f, 0.36f, alpha), 2);
        target_->SetTransform(base);
    } else if (shape && image) {
        D2D1_MATRIX_3X2_F base; target_->GetTransform(&base);
        // The ambient clock pauses spinning when the launcher is idle or not being watched.
        target_->SetTransform(D2D1::Matrix3x2F::Rotation(elapsed_ * 42, D2D1::Point2F(cx, cy)) * base);
        fillImage(*image, D2D1::RectF(cx - radius, cy - radius, cx + radius, cy + radius), 0,
            alpha * reveal(*image), false, shape.Get());
        line(cx - radius * 0.8f, cy, cx - radius * 0.25f, cy, gray(1, 0.42f * alpha), 2);
        target_->SetTransform(base);
    }
    brush(gray(0.94f, 0.8f * alpha)); target_->DrawEllipse(circle, solid_.Get(), 2.2f);
    brush(cd ? gray(0.7f, alpha) : D2D1::ColorF(accent.r, accent.g, accent.b, 0.65f * alpha));
    target_->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), radius - 5, radius - 5), solid_.Get(), 1.5f);
    brush(gray(0.72f, alpha)); target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), radius * 0.19f, radius * 0.19f), solid_.Get());
    brush(gray(0.11f, alpha)); target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), radius * 0.075f, radius * 0.075f), solid_.Get());
    brush(gray(1, 0.45f * alpha)); target_->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), radius * 0.14f, radius * 0.14f), solid_.Get(), 1);
    if (scan && shape) {
        D2D1_MATRIX_3X2_F base; target_->GetTransform(&base);
        target_->SetTransform(D2D1::Matrix3x2F::Rotation(elapsed_ * 42, {cx, cy}) * base);
        fillImage(*scan, D2D1::RectF(cx - radius, cy - radius, cx + radius, cy + radius), 0,
                  card.opacity * scanned, sessionOf(card.game) == SessionState::frozen, shape.Get(), true);
        target_->SetTransform(base);
    }
}

void Renderer::drawCdCase(const GameCard& card, size_t index) {
    if (!visible(card, viewport_.width)) return;
    const auto r = posterRect(card, index == selected_ ? context_->launch_progress : 0);
    const float w = r.right - r.left, h = r.bottom - r.top, alpha = card.opacity;
    // Standard jewel case: black ribbed tray spine, square paper insert, clear plastic edges.
    brush(gray(0, 0.5f * alpha));
    target_->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(r.left + 4, r.top + 9, r.right + 6, r.bottom + 10), 4, 4), solid_.Get());
    brush(gray(0.15f, alpha)); target_->FillRoundedRectangle(D2D1::RoundedRect(r, 3, 3), solid_.Get());
    const float spine = w * 0.095f, edge = h * 0.022f;
    const auto insert = D2D1::RectF(r.left + spine + edge, r.top + edge, r.right - edge, r.bottom - edge);
    brush(gray(0.025f, alpha)); target_->FillRectangle(D2D1::RectF(r.left + 3, r.top + 3, r.left + spine, r.bottom - 3), solid_.Get());
    for (float x = r.left + 5; x < r.left + spine - 3; x += std::max(2.5f, w * 0.005f))
        line(x, r.top + 6, x, r.bottom - 6, gray(0.38f, 0.45f * alpha), 1);
    auto* image = index < covers_.size() && covers_[index].bitmap ? &covers_[index] : nullptr;
    if (image) fillImage(*image, insert, 0, alpha * reveal(*image), sessionOf(card.game) == SessionState::frozen);
    else {
        brush(gray(0.89f, alpha)); target_->FillRectangle(insert, solid_.Get());
        paragraph(card.game.name, insert.left + 20, insert.top + h * 0.35f, insert.right - insert.left - 40, cd_pen_font_.Get(), gray(0.12f, alpha), 3);
    }
    // A few translucent moulded rims and tabs, rather than a metal finish over the insert.
    createMediaMaterials();
    if (media_edge_) {
        media_edge_->SetStartPoint({r.left, r.top}); media_edge_->SetEndPoint({r.right, r.bottom}); media_edge_->SetOpacity(alpha);
        target_->DrawRoundedRectangle(D2D1::RoundedRect(r, 3, 3), media_edge_.Get(), 4);
    }
    for (const float inset : {3.0f, 7.0f}) {
        brush(gray(0.92f, (inset == 3 ? 0.55f : 0.2f) * alpha));
        target_->DrawRectangle(D2D1::RectF(r.left + spine + inset, r.top + inset, r.right - inset, r.bottom - inset), solid_.Get(), 1);
    }
    for (const float y : {r.top + h * 0.1f, r.bottom - h * 0.1f}) {
        brush(gray(0.75f, 0.5f * alpha));
        target_->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(r.left + spine - 2, y - 9, r.left + spine + 7, y + 9), 2, 2), solid_.Get());
        line(r.right - edge, y - 12, r.right - 3, y - 12, gray(1, 0.5f * alpha), 1.4f);
    }
    line(insert.left + 8, insert.top + 4, insert.right - 8, insert.top + 4, gray(1, 0.24f * alpha), 2);
    if (!card.game.installed) { brush(gray(0, 0.25f * alpha)); target_->FillRectangle(insert, solid_.Get()); }
    if (card.game.installation.active() || sessionOf(card.game) != SessionState::none)
        drawCardStatus(r.left, r.top, w, h, alpha * (card.game.installation.active() ? 1.0f : statusFade(card.game)), card.game.installation.active() ? installationText(card.game.installation) :
            sessionOf(card.game) == SessionState::frozen ? tr(L"Wstrzymana") : tr(L"W grze"));
}

void Renderer::drawCoverflowPoster(const GameCard& card, size_t index) {
    if (!visible(card, viewport_.width)) return;
    const float launch = index == selected_ ? context_->launch_progress : 0;
    const auto rect = posterRect(card, launch);
    const float previous_corner = squircle_corner_;
    squircle_corner_ = 0.2f;
    const auto shape = tileShape(rect, tile_shape_);
    squircle_corner_ = previous_corner;
    if (!shape) return;
    const float alpha = card.opacity;
    for (int layer = 3; layer >= 1; --layer) {
        const float spread = layer * 3.0f;
        brush(gray(0, 0.065f * alpha));
        target_->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(rect.left - spread, rect.top + 7 - spread,
            rect.right + spread, rect.bottom + 7 + spread), 14, 14), solid_.Get());
    }
    brush(D2D1::ColorF(0.035f, 0.04f, 0.05f, alpha)); target_->FillGeometry(shape.Get(), solid_.Get());
    auto* image = index < posters_.size() && posters_[index].bitmap ? &posters_[index] :
                  index < covers_.size() && covers_[index].bitmap ? &covers_[index] : nullptr;
    const auto session = sessionOf(card.game);
    if (image) fillPoster(*image, rect, 14, alpha * reveal(*image), session == SessionState::frozen, shape.Get());
    else text(card.game.name, rect.left + 20, rect.top + card.size * 0.43f, card.width() - 40, card.size * 0.3f,
              menu_font_.Get(), gray(0.9f, alpha));
    if (image && session != SessionState::frozen) {
        // Plain print until selected; then the finish grows 0 -> 100% (card.steelbook) every time.
        const float finish = card.steelbook * card.steelbook * (3 - 2 * card.steelbook);
        const coverflow_layout::Layout layout{viewport_.width, viewport_.height, context_->library_mode};
        drawSteelbook(*image, rect, shape.Get(), alpha * reveal(*image) * finish, layout.lightSweep(card, index, selected_));
    }
    brush(gray(0, alpha * (!card.game.installed ? 0.28f : 0.16f * (1 - card.emphasis))));
    target_->FillGeometry(shape.Get(), solid_.Get());
    // Selection is the larger case and disc, never a neon outline around the print.
    brush(gray(1, 0.12f * alpha)); target_->DrawGeometry(shape.Get(), solid_.Get(), 0.8f);
    if (card.game.installation.active() || session != SessionState::none) {
        const float status_fade = card.game.installation.active() ? 1.0f : statusFade(card.game);
        drawCardStatusShade(rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top, alpha * status_fade, shape.Get());
        drawCardStatus(rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top, alpha * status_fade,
            card.game.installation.active() ? installationText(card.game.installation) :
            session == SessionState::frozen ? tr(L"Wstrzymana") : tr(L"W grze"));
    }
}

void Renderer::createSteelbookSurface() {
    if (steelbook_surface_) return;
    // A shared, neutral brushed sheet. The same substrate is used beneath every artwork.
    constexpr UINT size = 256;
    std::vector<uint8_t> pixels(size * size * 4);
    std::array<float, size> light_x{}, light_y{};
    for (UINT i = 0; i < size; ++i) {
        const float p = (i + 0.5f) / size;
        light_x[i] = std::exp(-std::pow((p - 0.72f) / 0.48f, 2.0f));
        light_y[i] = std::exp(-std::pow((p - 0.32f) / 0.42f, 2.0f));
    }
    for (UINT y = 0; y < size; ++y) for (UINT x = 0; x < size; ++x) {
        const float brushed = 0.026f * steelbook_material::noise(x * 101u) +
            0.010f * steelbook_material::noise(x * 374761393u + (y / 6) * 668265263u);
        const float tone = 0.91f + 0.055f * light_x[x] * light_y[y] + 0.015f * (1 - float(y) / size) + brushed;
        auto* p = pixels.data() + (y * size + x) * 4;
        p[0] = p[1] = p[2] = static_cast<uint8_t>(std::lround(std::clamp(tone, 0.0f, 1.0f) * 255)); p[3] = 255;
    }
    target_->CreateBitmap(D2D1::SizeU(size, size), pixels.data(), size * 4,
        D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96, 96), steelbook_surface_.GetAddressOf());
}

void Renderer::createSteelbookStreak() {
    if (steelbook_streak_) return;
    // Brushed metal reflects a point light as a band across its vertical brush lines: long,
    // narrow and fading along its length. Smooth on purpose: the tile travels with the light,
    // so any texture in it would slide over the print. One shared A8 tile per target.
    constexpr UINT width = streak_width, height = streak_height;
    std::vector<uint8_t> alpha(width * height);
    const auto window = [](float t, float spread) { // A bell that reaches exactly 0 at the tile edge.
        const float edge = std::exp(-0.25f / (spread * spread));
        return std::max(0.0f, (std::exp(-(t - 0.5f) * (t - 0.5f) / (spread * spread)) - edge) / (1 - edge));
    };
    for (UINT x = 0; x < width; ++x) {
        const float along = window((x + 0.5f) / width, 0.30f);
        for (UINT y = 0; y < height; ++y)
            alpha[y * width + x] = static_cast<uint8_t>(std::lround(std::clamp(along * window((y + 0.5f) / height, 0.22f), 0.0f, 1.0f) * 255));
    }
    Microsoft::WRL::ComPtr<ID2D1Bitmap> tile;
    if (SUCCEEDED(target_->CreateBitmap(D2D1::SizeU(width, height), alpha.data(), width,
            D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96, 96), tile.GetAddressOf())))
        target_->CreateBitmapBrush(tile.Get(), D2D1::BitmapBrushProperties(D2D1_EXTEND_MODE_CLAMP, D2D1_EXTEND_MODE_CLAMP,
            D2D1_BITMAP_INTERPOLATION_MODE_LINEAR), steelbook_streak_.GetAddressOf());
}

void Renderer::uploadSteelbook(Cover& image) {
    if (!image.steelbook) return;
    if (image.steelbook->source != image.encoded()) { image.steelbook.reset(); return; }
    auto& material = *image.steelbook;
    if (!material.decoding || !material.decoding->done.load(std::memory_order_acquire) || !target_) return;
    const auto ticket = std::move(material.decoding);
    const auto& pixels = ticket->image;
    material.failed = true; // A broken source/target is tried again only after replacement/device loss.
    if (!pixels.valid()) return;
    const auto properties = D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96, 96);
    if (FAILED(target_->CreateBitmap(D2D1::SizeU(pixels.width, pixels.height), pixels.pixels.data(), pixels.width * 4,
        properties, material.bitmap.ReleaseAndGetAddressOf()))) return;
    if (FAILED(target_->CreateBitmapBrush(material.bitmap.Get(), material.brush.ReleaseAndGetAddressOf()))) return;
    material.brush->SetInterpolationMode(D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    // Optional masks: a failed upload only loses the lacquer sheen or the matte haze.
    const auto mask = [&](const std::vector<uint8_t>& values, Microsoft::WRL::ComPtr<ID2D1Bitmap>& bitmap) {
        bitmap.Reset();
        if (values.size() == static_cast<size_t>(pixels.width) * pixels.height)
            target_->CreateBitmap(D2D1::SizeU(pixels.width, pixels.height), values.data(), pixels.width,
                D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96, 96), bitmap.GetAddressOf());
    };
    mask(pixels.gloss, material.gloss);
    mask(pixels.matte, material.matte);
    // Embossed edges: two colour matrices turn relief . light direction into rims each frame.
    material.relief.Reset(); material.rim_light.Reset(); material.rim_shade.Reset();
    if (realizer_ && pixels.relief.size() == pixels.pixels.size() &&
        SUCCEEDED(target_->CreateBitmap(D2D1::SizeU(pixels.width, pixels.height), pixels.relief.data(), pixels.width * 4,
            properties, material.relief.GetAddressOf())) &&
        SUCCEEDED(realizer_->CreateEffect(CLSID_D2D1ColorMatrix, material.rim_light.GetAddressOf())) &&
        SUCCEEDED(realizer_->CreateEffect(CLSID_D2D1ColorMatrix, material.rim_shade.GetAddressOf()))) {
        for (auto* rim : {material.rim_light.Get(), material.rim_shade.Get()}) {
            rim->SetInput(0, material.relief.Get());
            rim->SetValue(D2D1_COLORMATRIX_PROP_ALPHA_MODE, D2D1_COLORMATRIX_ALPHA_MODE_PREMULTIPLIED);
            rim->SetValue(D2D1_COLORMATRIX_PROP_CLAMP_OUTPUT, TRUE);
        }
    } else { material.relief.Reset(); material.rim_light.Reset(); material.rim_shade.Reset(); }
    createSteelbookSurface();
    if (realizer_ && steelbook_surface_ && image.bitmap &&
        SUCCEEDED(realizer_->CreateEffect(CLSID_D2D12DAffineTransform, material.surface_transform.ReleaseAndGetAddressOf())) &&
        SUCCEEDED(realizer_->CreateEffect(CLSID_D2D1Blend, material.printed.ReleaseAndGetAddressOf()))) {
        const auto art_size = image.bitmap->GetSize(), metal_size = steelbook_surface_->GetSize();
        material.surface_transform->SetInput(0, steelbook_surface_.Get());
        material.surface_transform->SetValue(D2D1_2DAFFINETRANSFORM_PROP_TRANSFORM_MATRIX,
            D2D1::Matrix3x2F::Scale(art_size.width / metal_size.width, art_size.height / metal_size.height));
        material.surface_transform->SetValue(D2D1_2DAFFINETRANSFORM_PROP_BORDER_MODE, D2D1_BORDER_MODE_HARD);
        material.printed->SetInputEffect(0, material.surface_transform.Get());
        material.printed->SetInput(1, image.bitmap.Get());
        material.printed->SetValue(D2D1_BLEND_PROP_MODE, D2D1_BLEND_MODE_MULTIPLY);
    } else { material.surface_transform.Reset(); material.printed.Reset(); }
    if (realizer_ && SUCCEEDED(realizer_->CreateEffect(CLSID_D2D1PointSpecular, material.lighting.ReleaseAndGetAddressOf())) &&
        SUCCEEDED(realizer_->CreateEffect(CLSID_D2D1ColorMatrix, material.highlight.ReleaseAndGetAddressOf())) &&
        SUCCEEDED(realizer_->CreateEffect(CLSID_D2D1ArithmeticComposite, material.masked.ReleaseAndGetAddressOf()))) {
        material.lighting->SetInput(0, material.bitmap.Get());
        material.lighting->SetValue(D2D1_POINTSPECULAR_PROP_SPECULAR_EXPONENT, 2.0f / (metal_roughness * metal_roughness) - 2.0f);
        material.lighting->SetValue(D2D1_POINTSPECULAR_PROP_SPECULAR_CONSTANT, metal_reflection);
        material.lighting->SetValue(D2D1_POINTSPECULAR_PROP_SURFACE_SCALE, 4.0f);
        material.lighting->SetValue(D2D1_POINTSPECULAR_PROP_KERNEL_UNIT_LENGTH, D2D1_VECTOR_2F{1.25f, 1.25f});
        material.lighting->SetValue(D2D1_POINTSPECULAR_PROP_COLOR, D2D1_VECTOR_3F{1, 1, 1});
        // PointSpecular has opaque output. Use its intensity as alpha so unlit pixels
        // cannot darken the entire poster. STRAIGHT avoids premultiplying it twice.
        const D2D1_MATRIX_5X4_F alpha_from_light{1, 0, 0, 1.0f / 3, 0, 1, 0, 1.0f / 3,
            0, 0, 1, 1.0f / 3, 0, 0, 0, 0, 0, 0, 0, 0};
        material.highlight->SetInputEffect(0, material.lighting.Get());
        material.highlight->SetValue(D2D1_COLORMATRIX_PROP_COLOR_MATRIX, alpha_from_light);
        material.highlight->SetValue(D2D1_COLORMATRIX_PROP_ALPHA_MODE, D2D1_COLORMATRIX_ALPHA_MODE_STRAIGHT);
        material.masked->SetInputEffect(0, material.highlight.Get());
        material.masked->SetInput(1, material.bitmap.Get());
        material.masked->SetValue(D2D1_ARITHMETICCOMPOSITE_PROP_COEFFICIENTS, D2D1_VECTOR_4F{1, 0, 0, 0});
        material.masked->SetValue(D2D1_ARITHMETICCOMPOSITE_PROP_CLAMP_OUTPUT, TRUE);
    } else { material.lighting.Reset(); material.highlight.Reset(); material.masked.Reset(); }
    material.revealed = last_reveal_ = std::chrono::steady_clock::now();
    material.failed = false;
}

void Renderer::drawSteelbook(Cover& image, D2D1_RECT_F rect, ID2D1Geometry* clip, float opacity, float sweep) {
#ifdef LAUNCHME_UI_TEST
    if (!steelbook_for_test) return;
#endif
    if (!image.bitmap || !clip) return;
    if (!image.steelbook || image.steelbook->source != image.encoded()) {
        image.steelbook = std::make_shared<SteelbookMaterial>();
        image.steelbook->source = image.encoded();
    }
    auto& material = *image.steelbook;
    // Visible neighbours prepare their maps while still plain, so selecting one can start
    // its finish at once instead of waiting for the worker.
    if (!material.bitmap && !material.decoding && !material.failed)
        material.decoding = decoder_.submit(material.source, {false, false, false, 0, true});
    uploadSteelbook(image);
    if (opacity < 0.002f) return;
    const float w = rect.right - rect.left, h = rect.bottom - rect.top;
    if (material.bitmap && material.brush) {
        const auto size = material.bitmap->GetSize();
        // As fillPoster: a poster of nearly the frame's shape fills it, another shape shows whole.
        const auto fits = [&](D2D1_SIZE_F s) { return std::abs((s.width / std::max(1.0f, s.height)) / (w / std::max(1.0f, h)) - 1) < 0.25f; };
        const float scale = fits(size) ? std::max(w / size.width, h / size.height) : std::min(w / size.width, h / size.height);
        const float left = rect.left + (w - size.width * scale) * 0.5f, top = rect.top + (h - size.height * scale) * 0.5f;
        const float t = std::clamp(std::chrono::duration<float>(std::chrono::steady_clock::now() - material.revealed).count() / 0.25f, 0.0f, 1.0f);
        const auto fit = D2D1::Matrix3x2F::Scale(scale, scale) * D2D1::Matrix3x2F::Translation(left, top);
        const auto area = D2D1::RectF(left, top, left + size.width * scale, top + size.height * scale);
        const float finish = opacity * t * t * (3 - 2 * t);
        const float phase = elapsed_ * 0.45f + rect.left / viewport_.width * 0.3f;
        // Everything below follows one light; a gliding case slides it across (sweep).
        const float light_x = 0.78f + 0.38f * std::sin(phase) + sweep, light_y = 0.40f + 0.08f * std::sin(phase * 0.83f);
        material.brush->SetTransform(fit); material.brush->SetOpacity(1);
        target_->PushAxisAlignedClip(area, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        target_->PushLayer(D2D1::LayerParameters(rect, clip, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
            D2D1::IdentityMatrix(), finish), nullptr);
        if (realizer_ && material.printed) {
            const auto original = image.bitmap->GetSize();
            const float print_scale = fits(original) ? std::max(w / original.width, h / original.height) : std::min(w / original.width, h / original.height);
            const float print_left = rect.left + (w - original.width * print_scale) * 0.5f;
            const float print_top = rect.top + (h - original.height * print_scale) * 0.5f;
            D2D1_MATRIX_3X2_F base; target_->GetTransform(&base);
            target_->SetTransform(D2D1::Matrix3x2F::Scale(print_scale, print_scale) *
                D2D1::Matrix3x2F::Translation(print_left, print_top) * base);
            realizer_->DrawImage(material.printed.Get(), D2D1::Point2F(0, 0), D2D1_INTERPOLATION_MODE_LINEAR);
            target_->SetTransform(base);
        }
        // Slightly darker metal away from the reflection; only the selected pigment is affected.
        const auto aa = target_->GetAntialiasMode(); target_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
        brush(gray(0, 0.04f)); target_->FillOpacityMask(material.bitmap.Get(), solid_.Get(), D2D1_OPACITY_MASK_CONTENT_GRAPHICS, &area);
        // Matte print scatters the same light: a broad, dull haze through the grainy matte mask,
        // never a crisp reflection, so it reads differently from lacquer and foil.
        if (material.matte && !steelbook_haze_) {
            const D2D1_GRADIENT_STOP stops[] = {{0, gray(1, 0.085f)}, {0.5f, gray(1, 0.04f)}, {1, gray(1, 0)}};
            Microsoft::WRL::ComPtr<ID2D1GradientStopCollection> colors;
            if (SUCCEEDED(target_->CreateGradientStopCollection(stops, 3, colors.GetAddressOf())))
                target_->CreateRadialGradientBrush(D2D1::RadialGradientBrushProperties(D2D1::Point2F(0, 0), D2D1::Point2F(0, 0), 1, 1),
                    colors.Get(), steelbook_haze_.GetAddressOf());
        }
        if (material.matte && steelbook_haze_) {
            steelbook_haze_->SetCenter(D2D1::Point2F(left + size.width * scale * light_x, top + size.height * scale * light_y));
            steelbook_haze_->SetRadiusX(w * 1.05f); steelbook_haze_->SetRadiusY(h * 0.75f);
            target_->FillOpacityMask(material.matte.Get(), steelbook_haze_.Get(), D2D1_OPACITY_MASK_CONTENT_GRAPHICS, &area);
        }
        // Flat polished black lacquer mirrors a soft, tilted oval of the same moving light.
        // Brushed foil scatters it instead, so the gloss mask holds only the darkest ink.
        if (material.gloss && !steelbook_sheen_) {
            // Softbox-like: a brighter core inside a broad, slowly fading halo.
            const D2D1_GRADIENT_STOP stops[] = {{0, gray(1, 0.15f)}, {0.30f, gray(1, 0.10f)}, {0.65f, gray(1, 0.04f)}, {1, gray(1, 0)}};
            Microsoft::WRL::ComPtr<ID2D1GradientStopCollection> colors;
            if (SUCCEEDED(target_->CreateGradientStopCollection(stops, 4, colors.GetAddressOf())))
                target_->CreateRadialGradientBrush(D2D1::RadialGradientBrushProperties(D2D1::Point2F(0, 0), D2D1::Point2F(0, 0), 1, 1),
                    colors.Get(), steelbook_sheen_.GetAddressOf());
        }
        if (material.gloss && steelbook_sheen_) {
            const auto centre = D2D1::Point2F(left + size.width * scale * light_x, top + size.height * scale * light_y);
            steelbook_sheen_->SetCenter(centre);
            steelbook_sheen_->SetRadiusX(w * 0.85f); steelbook_sheen_->SetRadiusY(h * 0.38f);
            steelbook_sheen_->SetTransform(D2D1::Matrix3x2F::Rotation(-28, centre));
            target_->FillOpacityMask(material.gloss.Get(), steelbook_sheen_.Get(), D2D1_OPACITY_MASK_CONTENT_GRAPHICS, &area);
        }
        target_->SetAntialiasMode(aa);
        if (realizer_ && material.rim_light && material.rim_shade) {
            // Embossed high-contrast edges: the side facing the light catches a thin bright lip,
            // the far side a soft shadow. The light comes from the case centre towards the moving
            // light and always a little from above, like tilting a real case under a lamp.
            float dx = light_x - 0.5f, dy = light_y - 0.5f - 0.35f;
            const float length = std::max(0.001f, std::hypot(dx, dy)); dx /= length; dy /= length;
            // relief = 128/255 + 127/255 * v; alpha = k * dot(v, d), colour = alpha (white) or 0.
            constexpr float centre = 128.0f / 255, unit = 255.0f / 127;
            const auto rims = [&](float k, float white) {
                const float ax = k * unit * dx, ay = k * unit * dy, a0 = -k * unit * centre * (dx + dy);
                return D2D1_MATRIX_5X4_F{white * ax, white * ax, white * ax, ax, white * ay, white * ay, white * ay, ay,
                    0, 0, 0, 0, 0, 0, 0, 0, white * a0, white * a0, white * a0, a0};
            };
            // Lighter ink is raised: its lit edge faces against the gradient.
            material.rim_light->SetValue(D2D1_COLORMATRIX_PROP_COLOR_MATRIX, rims(-0.38f, 1));
            material.rim_shade->SetValue(D2D1_COLORMATRIX_PROP_COLOR_MATRIX, rims(0.30f, 0));
            D2D1_MATRIX_3X2_F base; target_->GetTransform(&base);
            target_->SetTransform(fit * base);
            realizer_->DrawImage(material.rim_shade.Get(), D2D1::Point2F(0, 0), D2D1_INTERPOLATION_MODE_LINEAR);
            realizer_->DrawImage(material.rim_light.Get(), D2D1::Point2F(0, 0), D2D1_INTERPOLATION_MODE_LINEAR);
            target_->SetTransform(base);
        }
        if (realizer_ && material.masked) {
            material.lighting->SetValue(D2D1_POINTSPECULAR_PROP_LIGHT_POSITION,
                D2D1_VECTOR_3F{size.width * light_x, size.height * light_y, size.width * 0.35f});
            D2D1_MATRIX_3X2_F base; target_->GetTransform(&base);
            target_->SetTransform(fit * base);
            realizer_->DrawImage(material.masked.Get(), D2D1::Point2F(0, 0), D2D1_INTERPOLATION_MODE_LINEAR);
            target_->SetTransform(base);
        } else {
            // Older render targets retain hue-selective, moving reflections instead of a flat wash.
            if (!steelbook_light_) {
                const D2D1_GRADIENT_STOP stops[] = {{0, gray(1, 0.78f)}, {0.4f, gray(1, 0.25f)}, {1, gray(1, 0)}};
                Microsoft::WRL::ComPtr<ID2D1GradientStopCollection> colors;
                if (SUCCEEDED(target_->CreateGradientStopCollection(stops, 3, colors.GetAddressOf())))
                    target_->CreateRadialGradientBrush(D2D1::RadialGradientBrushProperties(D2D1::Point2F(0, 0), D2D1::Point2F(0, 0), 1, 1),
                        colors.Get(), steelbook_light_.GetAddressOf());
            }
            if (steelbook_light_) {
                steelbook_light_->SetCenter(D2D1::Point2F(left + size.width * scale * light_x, top + size.height * scale * light_y));
                steelbook_light_->SetRadiusX(w * 0.60f); steelbook_light_->SetRadiusY(h * 0.45f);
                target_->PushLayer(D2D1::LayerParameters(rect, nullptr, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE, D2D1::IdentityMatrix(), 1,
                    material.brush.Get()), nullptr);
                target_->FillRectangle(area, steelbook_light_.Get()); target_->PopLayer();
            }
        }
        // Brushed foil: an anisotropic band across the vertical brush lines, in the foil's own
        // tint (the map), so metal shows a streak where lacquer shows an oval.
        createSteelbookStreak();
        if (steelbook_streak_) {
            const float band_w = w * 2.2f, band_h = h * 0.16f;
            const float cx = left + size.width * scale * light_x, cy = top + size.height * scale * light_y;
            steelbook_streak_->SetTransform(D2D1::Matrix3x2F::Scale(band_w / streak_width, band_h / streak_height) *
                D2D1::Matrix3x2F::Translation(cx - band_w * 0.5f, cy - band_h * 0.5f));
            target_->PushLayer(D2D1::LayerParameters(area, nullptr, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE, D2D1::IdentityMatrix(), 0.55f,
                steelbook_streak_.Get()), nullptr);
            target_->FillRectangle(area, material.brush.Get());
            target_->PopLayer();
        }
        target_->PopLayer();
        target_->PopAxisAlignedClip();
    }
    // A pressed lip inside the existing frame. Two hairlines, never a second glow or a
    // heavy black outline around the print. Right/upper side catches the soft room light.
    if (!steelbook_edge_) {
        const D2D1_GRADIENT_STOP stops[] = {{0, gray(1, 0.38f)}, {0.30f, gray(1, 0.16f)},
            {0.55f, gray(0, 0.06f)}, {1, gray(0, 0.42f)}};
        Microsoft::WRL::ComPtr<ID2D1GradientStopCollection> gradient;
        if (SUCCEEDED(target_->CreateGradientStopCollection(stops, 4, gradient.GetAddressOf())))
            target_->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(D2D1::Point2F(0, 1), D2D1::Point2F(1, 0)),
                gradient.Get(), steelbook_edge_.GetAddressOf());
    }
    const float previous_corner = squircle_corner_; squircle_corner_ = 0.2f;
    const auto inner = tileShape(D2D1::RectF(rect.left + 1.8f, rect.top + 1.8f, rect.right - 1.8f, rect.bottom - 1.8f), tile_shape_);
    squircle_corner_ = previous_corner;
    if (inner && steelbook_edge_) {
        steelbook_edge_->SetStartPoint(D2D1::Point2F(rect.left, rect.top));
        steelbook_edge_->SetEndPoint(D2D1::Point2F(rect.right, rect.bottom));
        steelbook_edge_->SetOpacity(opacity);
        target_->DrawGeometry(clip, steelbook_edge_.Get(), 1.25f);
        brush(gray(0, 0.17f * opacity)); target_->DrawGeometry(inner.Get(), solid_.Get(), 1);
        steelbook_edge_->SetOpacity(opacity * 0.45f); target_->DrawGeometry(inner.Get(), steelbook_edge_.Get(), 0.65f);
    }
}
