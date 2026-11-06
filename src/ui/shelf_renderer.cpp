// Półka: Cover Flow after the iPod's. The focused poster faces front in the middle, its title,
// year and developer under it; the others stand turned on both sides in real perspective
// (Direct2D 1.1 perspective DrawBitmap, perspective.hpp) above a dark glossy floor that mirrors
// them. Every cover's pose follows its offset from the gliding centre (ViewContext::library_flow),
// so the covers turn while they pass the middle. Geometry: shelf_layout (coverflow_layout.hpp).
#include "renderer.hpp"
#include "coverflow_layout.hpp"
#include "package_look.hpp"
#include "renderer_internal.hpp"
#include "../library/steam_stats.hpp"

#include <d2d1effects.h>
#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <optional>
#include <vector>

using Microsoft::WRL::ComPtr;
using palette::gray;

namespace {
using render_detail::smoothFade;
ComPtr<ID2D1PathGeometry> quadGeometry(ID2D1Factory* factory, const D2D1_POINT_2F (&corners)[4]) {
    ComPtr<ID2D1PathGeometry> path;
    ComPtr<ID2D1GeometrySink> sink;
    if (FAILED(factory->CreatePathGeometry(path.GetAddressOf())) || FAILED(path->Open(sink.GetAddressOf()))) return {};
    sink->BeginFigure(corners[0], D2D1_FIGURE_BEGIN_FILLED);
    sink->AddLines(corners + 1, 3);
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    return SUCCEEDED(sink->Close()) ? path : ComPtr<ID2D1PathGeometry>{};
}
}

// A copy of `bitmap` without colour, made once on the GPU (Saturation effect, Direct2D 1.1); without
// it the WIC grey copy a suspended game uses.
ID2D1Bitmap* Renderer::desaturatedArt(Cover& image) {
    if (!image.bitmap) return nullptr;
    if (image.desaturated && image.desaturated_source == image.bitmap) return image.desaturated.Get();
    image.desaturated.Reset(); image.desaturated_source.Reset();
    const auto pixels = image.bitmap->GetPixelSize();
    ComPtr<ID2D1BitmapRenderTarget> surface;
    ComPtr<ID2D1DeviceContext> context;
    ComPtr<ID2D1Effect> saturation;
    if (!pixels.width || !pixels.height ||
        FAILED(target_->CreateCompatibleRenderTarget(D2D1::SizeF(static_cast<float>(pixels.width), static_cast<float>(pixels.height)), pixels,
                                                     surface.GetAddressOf())) ||
        FAILED(surface.As(&context)) || FAILED(context->CreateEffect(CLSID_D2D1Saturation, saturation.GetAddressOf()))) {
        if (grayBrush(image)) return image.gray_bitmap.Get();
        return nullptr;
    }
    saturation->SetInput(0, image.bitmap.Get());
    saturation->SetValue(D2D1_SATURATION_PROP_SATURATION, 0.0f);
    surface->BeginDraw();
    surface->Clear(D2D1::ColorF(0, 0, 0, 0));
    surface->SetTransform(D2D1::Matrix3x2F::Identity());
    // The bitmap's own DPI may differ from 96: draw it by pixels.
    const auto size = image.bitmap->GetSize();
    context->SetTransform(D2D1::Matrix3x2F::Scale(static_cast<float>(pixels.width) / std::max(1.0f, size.width),
                                                  static_cast<float>(pixels.height) / std::max(1.0f, size.height)));
    context->DrawImage(saturation.Get());
    if (FAILED(surface->EndDraw()) || FAILED(surface->GetBitmap(image.desaturated.GetAddressOf()))) { image.desaturated.Reset(); return nullptr; }
    image.desaturated_source = image.bitmap;
    return image.desaturated.Get();
}

// The spine's print: the game's name along its length, light on transparent, as large as the spine's
// thickness allows and smaller until it fits (an ellipsis below half size). With `band`, its top is
// a band in the platform's colour with the platform's logo (or name) along it, white. Spine-shaped
// (its thickness by its height); mapped onto the spine face in perspective. Cached by its content.
ID2D1Bitmap* Renderer::shelfSpine(const std::wstring& name, const std::wstring& platform, std::optional<D2D1_COLOR_F> band,
                                  std::optional<D2D1_COLOR_F> ink_colour, std::optional<D2D1_COLOR_F> band_ink) {
    if (name.empty() || !dwrite_) return nullptr;
    const auto found_logo = band && !platform.empty() ? platform_badges_.find(platform) : platform_badges_.end();
    Cover* logo = found_logo != platform_badges_.end() && found_logo->second.bitmap ? &found_logo->second : nullptr;
    const auto title_ink = ink_colour.value_or(gray(0.86f));
    const auto rgb = [](const D2D1_COLOR_F& c) {
        return std::to_wstring(std::lround(c.r * 255)) + L"." + std::to_wstring(std::lround(c.g * 255)) + L"." + std::to_wstring(std::lround(c.b * 255));
    };
    const auto head_ink = band_ink.value_or(gray(0.97f));
    const std::wstring key = name + L'\x1f' + (band ? platform + L'\x1f' + rgb(*band) + L'\x1f' + rgb(head_ink) : std::wstring{}) + (logo ? L"\x1f" L"logo" : L"") +
                             L'\x1f' + rgb(title_ink);
    if (const auto found = shelf_spines_.find(key); found != shelf_spines_.end()) return found->second.Get();
    if (shelf_spines_.size() >= 96) shelf_spines_.clear();
    constexpr UINT32 length = 768;
    const auto thickness = std::max<UINT32>(16, static_cast<UINT32>(std::lround(length * shelf_layout::depth * shelf_layout::aspect)));
    const float thick = static_cast<float>(thickness);
    const float band_length = band ? thick * 2.6f : 0.0f; // From the top: the platform's band.
    const float largest = thick * 0.58f, room = (length - band_length) * 0.9f;
    const auto layoutOf = [&](const std::wstring& value, float size, float space, ComPtr<IDWriteTextLayout>& layout) {
        ComPtr<IDWriteTextFormat> format;
        if (!font(format, size, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_CENTER)) return false;
        format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        if (FAILED(dwrite_->CreateTextLayout(value.c_str(), static_cast<UINT32>(value.size()), format.Get(), space, thick, layout.GetAddressOf()))) return false;
        DWRITE_TEXT_METRICS metrics{};
        if (SUCCEEDED(layout->GetMetrics(&metrics)) && metrics.widthIncludingTrailingWhitespace > space)
            layout->SetFontSize(std::max(size * 0.5f, size * space / metrics.widthIncludingTrailingWhitespace), {0, static_cast<UINT32>(value.size())});
        return true;
    };
    ComPtr<IDWriteTextLayout> title;
    if (!layoutOf(name, largest, room, title)) return nullptr;
    ComPtr<ID2D1BitmapRenderTarget> surface;
    ComPtr<ID2D1SolidColorBrush> ink;
    ComPtr<ID2D1Bitmap> bitmap;
    if (FAILED(target_->CreateCompatibleRenderTarget(D2D1::SizeF(thick, static_cast<float>(length)), D2D1::SizeU(thickness, length),
                                                     surface.GetAddressOf())) ||
        FAILED(surface->CreateSolidColorBrush(title_ink, ink.GetAddressOf()))) return nullptr;
    surface->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE); // On transparent: no ClearType fringes.
    surface->BeginDraw();
    surface->Clear(D2D1::ColorF(0, 0, 0, 0));
    // Turned a quarter: it reads from the top down. Mirrored too: the spine face runs from the case's
    // front towards its back, which on screen is right to left.
    const auto along = [&](float middle) {
        return D2D1::Matrix3x2F::Rotation(90, {thick * 0.5f, middle}) * D2D1::Matrix3x2F::Scale(-1, 1, {thick * 0.5f, middle});
    };
    if (band) {
        ink->SetColor(*band);
        surface->FillRectangle(D2D1::RectF(0, 0, thick, band_length), ink.Get());
        ink->SetColor(head_ink);
        const float middle = band_length * 0.5f;
        surface->SetTransform(along(middle));
        if (logo) {
            // Its monochrome logo's alpha as white ink, fitted in the band (aliased: the mask keeps its own edges).
            const auto pixels = logo->bitmap->GetPixelSize();
            const auto source = logo->has_ink_bounds ? logo->ink_bounds : D2D1::RectF(0, 0, static_cast<float>(pixels.width), static_cast<float>(pixels.height));
            const float ink_w = source.right - source.left, ink_h = source.bottom - source.top;
            if (ink_w >= 1 && ink_h >= 1) {
                const float factor = std::min(band_length * 0.72f / ink_w, thick * 0.56f / ink_h);
                const float w = ink_w * factor, h = ink_h * factor, cx = thick * 0.5f;
                const auto destination = D2D1::RectF(cx - w * 0.5f, middle - h * 0.5f, cx + w * 0.5f, middle + h * 0.5f);
                surface->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
                surface->FillOpacityMask(logo->bitmap.Get(), ink.Get(), D2D1_OPACITY_MASK_CONTENT_GRAPHICS, &destination, &source);
                surface->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
            }
        } else if (ComPtr<IDWriteTextLayout> label; !platform.empty() && layoutOf(platform, thick * 0.42f, band_length * 0.86f, label)) {
            surface->DrawTextLayout({thick * 0.5f - band_length * 0.43f, middle - thick * 0.5f}, label.Get(), ink.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
        }
        ink->SetColor(title_ink);
    }
    const float middle = band_length + (length - band_length) * 0.5f;
    surface->SetTransform(along(middle));
    surface->DrawTextLayout({thick * 0.5f - room * 0.5f, middle - thick * 0.5f}, title.Get(), ink.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
    if (FAILED(surface->EndDraw()) || FAILED(surface->GetBitmap(bitmap.GetAddressOf()))) return nullptr;
    return shelf_spines_.emplace(key, std::move(bitmap)).first->second.Get();
}
bool Renderer::createShelfResources() {
    const auto linear = [&](ComPtr<ID2D1LinearGradientBrush>& brush, std::initializer_list<D2D1_GRADIENT_STOP> stops) {
        if (brush) return true;
        ComPtr<ID2D1GradientStopCollection> collection;
        return SUCCEEDED(target_->CreateGradientStopCollection(stops.begin(), static_cast<UINT32>(stops.size()), collection.GetAddressOf())) &&
               SUCCEEDED(target_->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties({0, 0}, {0, 1}), collection.Get(), brush.GetAddressOf()));
    };
    // The reflections' opacity mask, the floor darkening towards the viewer, a face turned away
    // from the light (darkest at its far edge) and a lacquer gloss band.
    if (!linear(shelf_fade_, {{0, gray(0, 1)}, {0.28f, gray(0, 0.46f)}, {0.65f, gray(0, 0.10f)}, {1, gray(0, 0)}}) ||
        !linear(shelf_floor_, {{0, D2D1::ColorF(0.004f, 0.005f, 0.007f, 0)}, {0.42f, D2D1::ColorF(0.004f, 0.005f, 0.007f, 0.78f)},
                               {1, D2D1::ColorF(0.004f, 0.005f, 0.007f, 0.97f)}}) ||
        !linear(shelf_shade_, {{0, gray(0, 1)}, {1, gray(0, 0.2f)}}) ||
        !linear(shelf_gloss_, {{0, gray(1, 0)}, {0.40f, gray(1, 0.55f)}, {0.47f, gray(1, 1)}, {0.60f, gray(1, 0.18f)}, {1, gray(1, 0)}})) return false;
    if (!shelf_glow_) {
        // A spotlight from above onto the focused cover.
        // Eased towards its rim, so it has no visible edge.
        const D2D1_GRADIENT_STOP stops[] = {{0, gray(1, 0.13f)}, {0.3f, gray(1, 0.085f)}, {0.6f, gray(1, 0.03f)}, {0.82f, gray(1, 0.008f)}, {1, gray(1, 0)}};
        ComPtr<ID2D1GradientStopCollection> collection;
        if (FAILED(target_->CreateGradientStopCollection(stops, 5, collection.GetAddressOf())) ||
            FAILED(target_->CreateRadialGradientBrush(D2D1::RadialGradientBrushProperties({0, 0}, {0, 0}, 1, 1), collection.Get(),
                                                      shelf_glow_.GetAddressOf()))) return false;
    }
    return true;
}

void Renderer::drawLibraryShelf() {
    if (!cards_ || cards_->empty() || selected_ >= cards_->size() || !createShelfResources()) return;
    const auto& cards = *cards_;
    const shelf_layout::Layout layout{viewport_.width, viewport_.height};
    const size_t count = cards.size();
    const float flow = std::clamp(context_->library_flow.value_or(static_cast<float>(selected_)), -4.0f, static_cast<float>(count) + 3);
    const float scale = viewport_.scale, h = layout.coverHeight(), w = layout.coverWidth(), floor = layout.floor();
    const auto camera = layout.camera(scale);
    const float launch = smoothFade(context_->launch_progress);

    // Far covers first: the nearer the middle, the later (on top).
    std::vector<size_t> order;
    const auto side = static_cast<ptrdiff_t>(layout.sideCount());
    const auto centre = static_cast<ptrdiff_t>(std::lround(std::clamp(flow, 0.0f, static_cast<float>(count - 1))));
    // The open case (game_case_renderer.cpp) has taken the focused one off the shelf.
    const bool case_open = context_->game_case && context_->game_case->time > 0;
    for (ptrdiff_t i = std::max<ptrdiff_t>(0, centre - side); i < std::min<ptrdiff_t>(static_cast<ptrdiff_t>(count), centre + side + 1); ++i)
        if (cards[static_cast<size_t>(i)].opacity > 0.005f) order.push_back(static_cast<size_t>(i));
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        return std::abs(static_cast<float>(a) - flow) > std::abs(static_cast<float>(b) - flow);
    });

    // The stage: a soft light around the middle (faded out above the top bar's edge), the floor
    // fading to black towards the viewer.
    const auto spot = D2D1::Point2F(layout.centreX(), layout.top() + h * 0.55f);
    const float reach_x = h * 1.3f, reach_y = std::max(40.0f, spot.y - 186);
    shelf_glow_->SetCenter(spot); shelf_glow_->SetRadiusX(reach_x); shelf_glow_->SetRadiusY(reach_y);
    shelf_glow_->SetOpacity(0.8f + 0.2f * smoothFade(detail_));
    target_->FillRectangle(D2D1::RectF(spot.x - reach_x, spot.y - reach_y, spot.x + reach_x, spot.y + reach_y), shelf_glow_.Get());
    target_->PushAxisAlignedClip(D2D1::RectF(0, 180, viewport_.width, viewport_.height), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    shelf_floor_->SetStartPoint({0, floor - h * 0.34f}); shelf_floor_->SetEndPoint({0, floor + h * 0.62f});
    shelf_floor_->SetOpacity(1);
    target_->FillRectangle(D2D1::RectF(0, floor - h * 0.34f, viewport_.width, viewport_.height), shelf_floor_.Get());

    D2D1_MATRIX_3X2_F base; target_->GetTransform(&base);
    target_->SetTransform(D2D1::Matrix3x2F::Identity()); // Perspective is drawn in device pixels.
    ComPtr<ID2D1DeviceContext> device_context; // Perspective DrawBitmap: Direct2D 1.1 (Windows 8+).
    target_.As(&device_context);
    auto* context = device_context.Get();
    // One game case (or its mirror image below the floor): a box with rounded corners, the poster
    // under its clear front, the spine (left; the art wraps onto it) or the opening edge (right)
    // showing on the side that faces the viewer. Only the focused cover keeps its colour; the
    // others turn grey, and colour returns as one glides to the middle.
    const float floor_y = h * 0.5f * scale; // The floor plane, from the camera's origin.
    const auto unit = perspective::Vec3{0, 0, 1};
    // Półka's option "Platforma na grzbiecie": the spine's top band, in the console's colour or the store's.
    const auto spineBand = [&](const Game& game) -> std::optional<D2D1_COLOR_F> {
        if (!context_->library_options.bookshelf_platform) return std::nullopt;
        if (!game.platform.empty() && game.platform != L"PC") return render_detail::platformColor(game.platform);
        switch (game.source) {
        case GameSource::steam: return D2D1::ColorF(0.09f, 0.16f, 0.27f);
        case GameSource::gog: return D2D1::ColorF(0.42f, 0.16f, 0.52f);
        case GameSource::battlenet: return D2D1::ColorF(0.0f, 0.45f, 0.82f);
        case GameSource::ea: return D2D1::ColorF(0.86f, 0.22f, 0.27f);
        case GameSource::ubisoft: return D2D1::ColorF(0.05f, 0.33f, 0.78f);
        case GameSource::epic: return D2D1::ColorF(0.17f, 0.17f, 0.18f);
        case GameSource::xbox: return D2D1::ColorF(0.06f, 0.48f, 0.06f);
        default: return D2D1::ColorF(0.18f, 0.25f, 0.33f);
        }
    };
    const auto cover = [&](size_t index, bool mirrored) {
        const auto& card = cards[index];
        // Each game in its own package (package_look.hpp): a keep case, a Switch case, an Amiga big box.
        const auto platform = activePlatformName(card.game);
        const auto kind = game_case::kindOf(platform);
        const auto look = package_look::of(kind);
        const float package_aspect = game_case::aspect(kind), package_height = game_case::height(kind);
        const float offset = static_cast<float>(index) - flow, along = std::abs(offset);
        const float lift = index == selected_ ? launch * w * 0.16f : 0;
        auto front = layout.face(offset, lift, scale);
        {
            // Its own size around the slot's middle, standing on the floor.
            const float narrow = package_aspect * package_height / shelf_layout::aspect;
            front.origin = front.origin + front.u * ((1 - narrow) * 0.5f) + front.v * (1 - package_height);
            front.u = front.u * narrow;
            front.v = front.v * package_height;
        }
        const float depth = w * package_aspect * package_height / shelf_layout::aspect * game_case::depth(kind) * scale;
        const float angle = std::clamp(offset, -1.0f, 1.0f) * shelf_layout::turn;
        const auto back = perspective::turned(unit, angle) * depth; // Into the screen, as the case is turned.
        const auto world = [&](float s, float t, bool rear) {
            auto p = front.origin + front.u * s + front.v * t;
            if (rear) p = p + back;
            if (mirrored) p.y = 2 * floor_y - p.y;
            return camera.project(p);
        };
        const auto flat = [&](const perspective::Face& face) { return mirrored ? face.mirrored(floor_y) : face; };
        const auto partOf = [&](float l, float t, float r, float b) {
            return perspective::Face{front.origin + front.u * l + front.v * t, front.u * (r - l), front.v * (b - t)};
        };
        const auto outline = package_look::outline(look.corner, look.corner * package_aspect);
        const size_t arc = outline.size() / 4;
        // A closed outline through `points`; its bounds for the clipping layers.
        D2D1_RECT_F bounds{};
        const auto shape = [&](const std::vector<D2D1_POINT_2F>& points) {
            ComPtr<ID2D1PathGeometry> path;
            ComPtr<ID2D1GeometrySink> sink;
            if (points.size() < 3 || FAILED(factory_->CreatePathGeometry(path.GetAddressOf())) || FAILED(path->Open(sink.GetAddressOf()))) return ComPtr<ID2D1PathGeometry>{};
            bounds = {points[0].x, points[0].y, points[0].x, points[0].y};
            for (const auto& p : points) { bounds.left = std::min(bounds.left, p.x); bounds.top = std::min(bounds.top, p.y); bounds.right = std::max(bounds.right, p.x); bounds.bottom = std::max(bounds.bottom, p.y); }
            sink->BeginFigure(points[0], D2D1_FIGURE_BEGIN_FILLED);
            sink->AddLines(points.data() + 1, static_cast<UINT32>(points.size() - 1));
            sink->EndFigure(D2D1_FIGURE_END_CLOSED);
            return SUCCEEDED(sink->Close()) ? path : ComPtr<ID2D1PathGeometry>{};
        };
        const float alpha = card.opacity;
        auto* poster = index < posters_.size() && posters_[index].bitmap ? &posters_[index] : nullptr;
        auto* square = !poster && index < covers_.size() && covers_[index].bitmap ? &covers_[index] : nullptr;
        auto* image = poster ? poster : square;
        const bool frozen = sessionOf(card.game) == SessionState::frozen;
        struct Art { ID2D1Bitmap* bitmap = nullptr; ID2D1BitmapBrush* brush = nullptr; };
        Art colour, grey;
        if (image) {
            colour = {image->bitmap.Get(), image->brush.Get()};
            if (context) grey.bitmap = desaturatedArt(*image);
            if (!grey.bitmap && grayBrush(*image)) grey = {image->gray_bitmap.Get(), image->gray_brush.Get()};
        }
        // Full colour only in the middle unless the view's option keeps every case in colour (a suspended game stays grey, as everywhere).
        const float colourful = frozen ? 0.0f : context_->library_options.bookshelf_gray ? smoothFade(1 - along * 1.6f) : 1.0f;
        const float shown = image ? alpha * reveal(*image) : 0;
        // A picture onto (a part of) a face: in perspective, or without Direct2D 1.1 an affine fill
        // through the part's outline (its first three corners; close for the moderate turns).
        const auto picture = [&](const Art& art, const perspective::Face& part, D2D1_RECT_F source, float opacity) {
            if (!art.bitmap || opacity < 0.002f) return;
            const float sw = source.right - source.left, sh = source.bottom - source.top;
            if (sw < 1 || sh < 1) return;
            if (low_memory_ && image && art.bitmap == image->bitmap.Get()) { // The face's height on screen against the picture's.
                D2D1_POINT_2F corners[4];
                part.corners(camera, corners);
                wantEdge(*image, std::hypot(corners[3].x - corners[0].x, corners[3].y - corners[0].y) * viewport_.scale / sh);
            }
            if (context) {
                const auto matrix = perspective::matrix(part, sw, sh, camera);
                context->DrawBitmap(art.bitmap, D2D1::RectF(0, 0, sw, sh), opacity, D2D1_INTERPOLATION_MODE_ANISOTROPIC, &source, &matrix);
                return;
            }
            if (!art.brush) return;
            D2D1_POINT_2F q[4];
            part.corners(camera, q);
            const float m11 = (q[1].x - q[0].x) / sw, m12 = (q[1].y - q[0].y) / sw, m21 = (q[3].x - q[0].x) / sh, m22 = (q[3].y - q[0].y) / sh;
            art.brush->SetTransform(D2D1::Matrix3x2F(m11, m12, m21, m22, q[0].x - source.left * m11 - source.top * m21,
                                                     q[0].y - source.left * m12 - source.top * m22));
            art.brush->SetOpacity(opacity);
            if (const auto clip = quadGeometry(factory_.Get(), q)) target_->FillGeometry(clip.Get(), art.brush);
        };
        // The grey print, the coloured one over it as the cover nears the middle.
        const auto print = [&](const perspective::Face& part, float crop_aspect, float opacity) {
            if (colourful < 0.999f && grey.bitmap) picture(grey, flat(part), perspective::cropTo(grey.bitmap->GetSize(), crop_aspect), opacity);
            if (colourful > 0.001f && colour.bitmap)
                picture(colour, flat(part), perspective::cropTo(colour.bitmap->GetSize(), crop_aspect), opacity * (grey.bitmap ? colourful : 1));
        };
        // The side that faces the viewer: a cover on the right shows its opening edge, one on the
        // left its spine (the centre one shows neither).
        const bool right_side = offset > 0.02f, left_side = offset < -0.02f;
        if (right_side || left_side) {
            std::vector<D2D1_POINT_2F> strip;
            const size_t first = right_side ? 0 : 2 * arc;
            for (size_t k = 0; k < 2 * arc; ++k) { const auto& p = outline[first + k]; strip.push_back(world(p.x, p.y, false)); }
            for (size_t k = 2 * arc; k-- > 0;) { const auto& p = outline[first + k]; strip.push_back(world(p.x, p.y, true)); }
            if (const auto edge = shape(strip)) {
                const float lit = 1 - std::min(1.0f, along) * 0.35f;
                if (left_side) {
                    // The spine with the game's name printed along it (a Switch case's head band always),
                    // darker: it faces away from the lamp.
                    brush(D2D1::ColorF(look.spine.r, look.spine.g, look.spine.b, alpha)); target_->FillGeometry(edge.Get(), solid_.Get());
                    const auto head = look.spine_head_always ? std::optional<D2D1_COLOR_F>(look.spine_head) : spineBand(card.game);
                    if (auto* label = shelfSpine(card.game.name, platform, head, look.spine_ink, look.head_ink)) {
                        target_->PushLayer(D2D1::LayerParameters(bounds, edge.Get()), nullptr);
                        const perspective::Face spine{front.origin, back, front.v};
                        const auto size = label->GetSize();
                        picture({label, nullptr}, flat(spine), D2D1::RectF(0, 0, size.width, size.height), alpha);
                        target_->PopLayer();
                    }
                    brush(gray(0, (0.26f - 0.12f * lit + (mirrored ? 0.15f : 0)) * alpha)); target_->FillGeometry(edge.Get(), solid_.Get());
                } else {
                    // The opening edge: the plastic (clear over the insert's white on a Switch case), or cardboard.
                    brush(D2D1::ColorF(look.edge.r * lit, look.edge.g * lit, look.edge.b * lit, alpha)); target_->FillGeometry(edge.Get(), solid_.Get());
                    if (!mirrored) {
                        const auto a = world(1.0f, 0.12f, false), b = world(1.0f, 0.88f, false);
                        const auto c = world(1.0f, 0.12f, true), d = world(1.0f, 0.88f, true);
                        const bool light_edge = look.edge.r > 0.5f;
                        line((a.x * 0.62f + c.x * 0.38f), (a.y * 0.62f + c.y * 0.38f), (b.x * 0.62f + d.x * 0.38f), (b.y * 0.62f + d.y * 0.38f),
                             light_edge ? gray(0, 0.16f * alpha) : gray(0.85f, 0.20f * alpha), 1.2f * scale);
                    }
                }
                brush(gray(1, (mirrored ? 0.03f : 0.09f) * alpha)); target_->DrawGeometry(edge.Get(), solid_.Get(), 0.7f * scale);
            }
        }
        // The front (package_renderer.cpp): the same drawing as the open case's.
        const float turned = std::min(1.0f, along);
        PackageFront face;
        face.kind = kind;
        face.platform = platform;
        face.aspect = package_aspect;
        face.alpha = alpha;
        face.scale = scale;
        face.at = [&](float s, float t) { return world(s, t, false); };
        face.picture = [&](float l, float t, float r, float b, ID2D1Bitmap* bitmap, D2D1_RECT_F source, float opacity) {
            picture({bitmap, nullptr}, flat(partOf(l, t, r, b)), source, opacity);
        };
        face.print = [&](float l, float t, float r, float b, float art_aspect, float) {
            if (!image) return;
            const auto paper = partOf(l, t, r, b);
            if (poster) { print(paper, art_aspect, shown); return; }
            // A square cover stands in for the poster: its own blur behind it fills the face.
            if (context && colourful > 0.001f) if (auto* blur = blurredArt(*image))
                picture({blur, nullptr}, flat(paper), perspective::cropTo(blur->GetSize(), art_aspect), shown * 0.85f * colourful);
            print(art_aspect < 1 ? paper.inset(0, (1 - art_aspect) * 0.5f) : paper.inset((1 - 1 / art_aspect) * 0.5f, 0), 1, shown);
        };
        face.gloss = 0.5f - std::clamp(offset, -1.6f, 1.6f) * 0.32f;
        face.gloss_strength = 0.075f + 0.035f * turned;
        // Turned away from the light: darkest at the far edge, more so towards the screen's edges.
        face.shade = std::clamp(0.30f * turned + 0.07f * std::max(0.0f, along - 1), 0.0f, 0.62f) + (mirrored ? 0.18f : 0);
        face.shade_right = offset <= 0;
        face.dim = !card.game.installed;
        face.reflection = mirrored;
        drawPackageFront(face);
    };
    // The mirror images first, all through one fading mask anchored at the floor.
    shelf_fade_->SetStartPoint({0, floor * scale});
    shelf_fade_->SetEndPoint({0, (floor + h * shelf_layout::reflection) * scale});
    shelf_fade_->SetOpacity(1);
    target_->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), nullptr, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE, D2D1::IdentityMatrix(),
                                             0.34f, shelf_fade_.Get()), nullptr);
    for (const auto index : order) cover(index, true);
    target_->PopLayer();
    for (const auto index : order) {
        if (index == selected_ && launch > 0.001f) {
            // Launching: everything else dims beneath the focused cover, which comes forward.
            target_->SetTransform(base);
            brush(D2D1::ColorF(0.008f, 0.012f, 0.016f, launch * 0.82f));
            target_->FillRectangle(D2D1::RectF(0, 180, viewport_.width, viewport_.height), solid_.Get());
            target_->SetTransform(D2D1::Matrix3x2F::Identity());
        }
        if (!(case_open && index == selected_)) cover(index, false); // Its mirror image stays under the dimming.
    }
    target_->SetTransform(base);

    // The focused game's state over its cover once it faces front; its title when it has no art.
    const auto& card = cards[selected_];
    const float facing = case_open ? 0.0f : std::clamp(1 - std::abs(static_cast<float>(selected_) - flow) * 3, 0.0f, 1.0f) * card.opacity;
    const auto r = layout.bounds(static_cast<float>(selected_) - flow);
    const bool art = (selected_ < posters_.size() && posters_[selected_].bitmap) || (selected_ < covers_.size() && covers_[selected_].bitmap);
    if (!art && facing > 0.01f)
        text(card.game.name, r.left + 18, r.top + (r.bottom - r.top) * 0.40f, r.right - r.left - 36, (r.bottom - r.top) * 0.3f,
             menu_font_.Get(), gray(0.88f, facing));
    const auto session = sessionOf(card.game);
    if (facing > 0.01f && (card.game.installation.active() || session != SessionState::none)) {
        ComPtr<ID2D1RoundedRectangleGeometry> box;
        const float radius = (r.right - r.left) * shelf_layout::corner;
        if (SUCCEEDED(factory_->CreateRoundedRectangleGeometry(D2D1::RoundedRect(D2D1::RectF(r.left, r.top, r.right, r.bottom), radius, radius),
                                                               box.GetAddressOf()))) {
            const float status_fade = card.game.installation.active() ? 1.0f : statusFade(card.game);
            drawCardStatusShade(r.left, r.top, r.right - r.left, r.bottom - r.top, facing * status_fade, box.Get());
            drawCardStatus(r.left, r.top, r.right - r.left, r.bottom - r.top, facing * status_fade, card.game.installation.active() ?
                installationText(card.game.installation) : session == SessionState::frozen ? tr(L"Wstrzymana") : tr(L"W grze"));
        }
    }
    target_->PopAxisAlignedClip();

    // Title, then year · developer, centred under the cover; they rise in with every new focus.
    const float fade = smoothFade(detail_ * 1.6f) * (1 - launch * 0.6f) * (case_open ? 0.0f : 1.0f), rise = 10 * (1 - fade);
    if (fade < 0.003f) return;
    const float text_left = 68, text_width = viewport_.width - 136;
    fadingLine(card.game.name, text_left, layout.titleY() + rise, text_width, settings_heading_font_.Get(), gray(0.97f, fade), true, DWRITE_TEXT_ALIGNMENT_CENTER);
    // Under it, as in PSfive: the platform's badge, then year · developer · playtime, all at once when
    // the game's details are in (never the platform first and the rest replacing it later).
    if (context_->preview_waiting || !isGame(card.game)) return;
    std::wstring facts;
    const auto add = [&](const std::wstring& value) { if (!value.empty()) facts += (facts.empty() ? L"" : L"   ·   ") + value; };
    if (const auto* info = context_->preview) {
        add(gameReleaseYear(info->release_date)); add(info->developer);
        if (info->playtime_minutes && *info->playtime_minutes) add(std::wstring(tr(L"Czas gry: ")) + formatPlaytime(*info->playtime_minutes));
    }
    const auto platform = activePlatformName(card.game);
    const auto body = settings_body_font_.Get();
    const float y = layout.factsY() + rise * 1.4f, gap = facts.empty() ? 0.0f : 16.0f;
    float facts_width = facts.empty() ? 0.0f : std::min(textAdvance(facts, body), text_width);
    // The platform as its plain mark (logo, or its name until one arrives), as in Coverflow: a white
    // pill here read as a focused button.
    constexpr float mark_height = 24, mark_width = 200;
    const float badge = platform.empty() ? 0.0f : hasPlatformMark(platform) ? platformMarkWidth(platform, mark_height, mark_width) :
        std::min(mark_width, textAdvance(platform, body));
    facts_width = std::min(facts_width, std::max(0.0f, text_width - badge - gap));
    const float left = (viewport_.width - (badge + gap + facts_width)) * 0.5f;
    if (badge > 0) drawPlatformMark(platform, left, y, mark_height, mark_width, fade, body);
    if (!facts.empty()) textLine(facts, left + badge + gap, y, facts_width + 2, body, gray(0.72f, fade));
}
