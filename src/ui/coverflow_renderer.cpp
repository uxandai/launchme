#include "renderer.hpp"
#include "coverflow_layout.hpp"
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
    target_->PushAxisAlignedClip(D2D1::RectF(0, 180, viewport_.width, viewport_.height), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    // Discs are their own background pass: even a retracting disc cannot cover a neighbour.
    // All posters follow, and the selected poster is always on top throughout a reversal.
    if (std::any_of(cards_->begin(), cards_->end(), [&](const auto& card) {
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
    const auto draw = [&](size_t i) {
        if (cd) drawCdCase((*cards_)[i], i);
        else drawCoverflowPoster((*cards_)[i], i);
    };
    for (size_t i = cards_->size(); i-- > 0;) if (i != selected_) draw(i);
    if (context_->launch_progress > 0.001f) {
        brush(D2D1::ColorF(0.008f, 0.012f, 0.016f, context_->launch_progress * 0.82f));
        target_->FillRectangle(D2D1::RectF(0, 180, viewport_.width, viewport_.height), solid_.Get());
    }
    // The case in the hand (game_case_view.hpp) has taken the focused poster away.
    if (!(context_->game_case && context_->game_case->shown())) draw(selected_);
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
    const auto disc = coverflow_layout::disc(card);
    auto* scan = index < discs_.size() && discs_[index].bitmap ? &discs_[index] : nullptr;
    const float radius = disc.radius, cx = disc.x, cy = disc.y;
    // The ambient clock pauses spinning when the launcher is idle or not being watched.
    const float spin = elapsed_ * 42;
    if (!context_->library_options.coverflow_cd) {
        drawGameDisc(cx, cy, radius, card.opacity, spin, image, scan, sessionOf(card.game) == SessionState::frozen, card.game.name);
        return;
    }
    // Printable white CD-R: a quiet label, transparent hub and blue ballpoint handwriting.
    const float scanned = scan ? reveal(*scan) : 0;
    const float alpha = card.opacity * (1 - scanned);
    const auto circle = D2D1::Ellipse(D2D1::Point2F(cx, cy), radius, radius);
    brush(gray(0.018f, 0.5f * card.opacity));
    target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx + 4, cy + 7), radius + 3, radius + 3), solid_.Get());
    Microsoft::WRL::ComPtr<ID2D1EllipseGeometry> shape;
    factory_->CreateEllipseGeometry(circle, shape.GetAddressOf());
    brush(gray(0.95f, alpha)); target_->FillEllipse(circle, solid_.Get());
    D2D1_MATRIX_3X2_F base; target_->GetTransform(&base);
    target_->SetTransform(D2D1::Matrix3x2F::Scale(radius / 160, radius / 160) *
        D2D1::Matrix3x2F::Translation(cx, cy) * D2D1::Matrix3x2F::Rotation(spin, {cx, cy}) * base);
    textLine(L"esperanza", -108, 66, 216, menu_font_.Get(), gray(0.43f, alpha), DWRITE_TEXT_ALIGNMENT_CENTER);
    textLine(L"CD-R  700 MB / 80 min", -112, 92, 224, label_font_.Get(), gray(0.56f, alpha), DWRITE_TEXT_ALIGNMENT_CENTER);
    line(-95, 106, 95, 106, gray(0.64f, alpha), 0.7f);
    D2D1_MATRIX_3X2_F disc_base; target_->GetTransform(&disc_base);
    target_->SetTransform(D2D1::Matrix3x2F::Rotation(-8, {0, -68}) * disc_base);
    paragraph(card.game.name, -114, -108, 228, cd_pen_font_.Get(), D2D1::ColorF(0.08f, 0.16f, 0.36f, alpha), 2);
    target_->SetTransform(base);
    brush(gray(0.94f, 0.8f * alpha)); target_->DrawEllipse(circle, solid_.Get(), 2.2f);
    brush(gray(0.7f, alpha));
    target_->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), radius - 5, radius - 5), solid_.Get(), 1.5f);
    brush(gray(0.72f, alpha)); target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), radius * 0.19f, radius * 0.19f), solid_.Get());
    brush(gray(0.11f, alpha)); target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), radius * 0.075f, radius * 0.075f), solid_.Get());
    brush(gray(1, 0.45f * alpha)); target_->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), radius * 0.14f, radius * 0.14f), solid_.Get(), 1);
    if (scan && shape) {
        target_->SetTransform(D2D1::Matrix3x2F::Rotation(spin, {cx, cy}) * base);
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
