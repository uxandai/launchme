// Półka's open case (game_case_view.hpp): the shelf dims away, the focused case flies to the left and
// opens (a keep case or a Switch case like a book, the lid swinging out to the left in perspective;
// an Amiga big box by its top flap), its medium slides out, and the right half fills in: the title
// (logo), year · developer, Graj / Zainstaluj and Szczegóły, the description and the platform's logo
// on a small tile of its colour. The closed case is drawn by the shelf's own front (package_renderer.cpp)
// through the shelf's camera, so it leaves the shelf unchanged.
#include "renderer.hpp"
#include "game_case_view.hpp"
#include "media_layout.hpp"
#include "package_look.hpp"
#include "perspective.hpp"
#include "renderer_internal.hpp"
#include "../library/steam_stats.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

using Microsoft::WRL::ComPtr;
using palette::gray;

namespace {
using render_detail::smoothFade;
ComPtr<ID2D1PathGeometry> polygon(ID2D1Factory* factory, const D2D1_POINT_2F* points, size_t count) {
    ComPtr<ID2D1PathGeometry> path;
    ComPtr<ID2D1GeometrySink> sink;
    if (count < 3 || FAILED(factory->CreatePathGeometry(path.GetAddressOf())) || FAILED(path->Open(sink.GetAddressOf()))) return {};
    sink->BeginFigure(points[0], D2D1_FIGURE_BEGIN_FILLED);
    sink->AddLines(points + 1, static_cast<UINT32>(count - 1));
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    return SUCCEEDED(sink->Close()) ? path : ComPtr<ID2D1PathGeometry>{};
}
ComPtr<ID2D1PathGeometry> polygon(ID2D1Factory* factory, const std::vector<D2D1_POINT_2F>& points) { return polygon(factory, points.data(), points.size()); }
template <size_t N> D2D1_RECT_F boundsOf(const D2D1_POINT_2F (&q)[N]) {
    D2D1_RECT_F r{q[0].x, q[0].y, q[0].x, q[0].y};
    for (const auto& p : q) { r.left = std::min(r.left, p.x); r.top = std::min(r.top, p.y); r.right = std::max(r.right, p.x); r.bottom = std::max(r.bottom, p.y); }
    return r;
}
D2D1_RECT_F boundsOf(const std::vector<D2D1_POINT_2F>& q) {
    D2D1_RECT_F r{q[0].x, q[0].y, q[0].x, q[0].y};
    for (const auto& p : q) { r.left = std::min(r.left, p.x); r.top = std::min(r.top, p.y); r.right = std::max(r.right, p.x); r.bottom = std::max(r.bottom, p.y); }
    return r;
}
D2D1_COLOR_F mix(D2D1_COLOR_F a, D2D1_COLOR_F b, float t) {
    return D2D1::ColorF(std::lerp(a.r, b.r, t), std::lerp(a.g, b.g, t), std::lerp(a.b, b.b, t), std::lerp(a.a, b.a, t));
}
D2D1_COLOR_F faded(D2D1_COLOR_F c, float alpha) { c.a *= alpha; return c; }
}

void Renderer::drawGameCase() {
    const auto* state = context_->game_case;
    if (!state || !cards_ || selected_ >= cards_->size() || state->time <= 0) return;
    const auto& card = (*cards_)[selected_];
    const auto& game = card.game;
    const auto kind = context_->game_case_kind;
    const auto look = package_look::of(kind);
    const game_case::Layout layout{viewport_.width, viewport_.height, kind};
    const float time = state->time;
    const auto phase = game_case::phases(time);
    // The big box opens its flap first; the diskette rises once the flap is out of its way.
    const float media = kind.package == game_case::Package::big_box ?
        game_case::easeBack((time - game_case::flap_media_start) / game_case::flap_media_time) : phase.media;
    const bool frozen = sessionOf(game) == SessionState::frozen;
    Cover* poster = selected_ < posters_.size() && posters_[selected_].bitmap ? &posters_[selected_] : nullptr;
    Cover* cover = selected_ < covers_.size() && covers_[selected_].bitmap ? &covers_[selected_] : nullptr;
    Cover* art = poster ? poster : cover;
    Cover* scan = selected_ < discs_.size() && discs_[selected_].bitmap ? &discs_[selected_] : nullptr;
    const float W = viewport_.width, H = viewport_.height, S = viewport_.scale;
    const auto platform = activePlatformName(game);
    if (!createShelfResources()) return;

    // The shelf dims away under a soft light where the case will stand.
    brush(D2D1::ColorF(0.006f, 0.008f, 0.011f, 0.90f * phase.travel)); target_->FillRectangle(D2D1::RectF(0, 0, W, H), solid_.Get());
    const auto rest = layout.rest();
    if (shelf_glow_) {
        const auto spot = D2D1::Point2F((rest.left + rest.right) * 0.5f, (rest.top + rest.bottom) * 0.5f);
        const float reach = layout.caseHeight() * 1.15f;
        shelf_glow_->SetCenter(spot); shelf_glow_->SetRadiusX(reach * 1.25f); shelf_glow_->SetRadiusY(reach);
        shelf_glow_->SetOpacity(phase.travel);
        target_->FillRectangle(D2D1::RectF(spot.x - reach * 1.25f, spot.y - reach, spot.x + reach * 1.25f, spot.y + reach), shelf_glow_.Get());
    }

    const settings_ui::Rect from{state->from_left, state->from_top, state->from_right, state->from_bottom};
    const auto front = layout.at(from, phase.travel);
    const float fw = front.right - front.left, fh = front.bottom - front.top;
    if (fw < 4 || fh < 4) return;
    const float k = fw / layout.caseWidth(); // The medium grows with the case on its flight.
    const float alpha = card.opacity;        // As on the shelf: the case is the shelf's own, lifted off it.
    // A soft contact shadow on the floor under it, wider as it opens.
    {
        const float cx = (front.left + front.right) * 0.5f, y = front.bottom + fh * 0.012f;
        for (int layer = 5; layer >= 1; --layer) {
            const float spread = layer * fw * 0.05f;
            brush(gray(0, 0.05f * phase.travel * alpha));
            target_->FillEllipse(D2D1::Ellipse({cx - fw * 0.35f * phase.lid, y}, fw * (0.55f + 0.45f * phase.lid) + spread, fh * 0.018f + spread * 0.16f), solid_.Get());
        }
    }

    // Perspective is drawn in device pixels around the case's centre, by the shelf's camera (its eye
    // 2.7 case heights away), so the case leaves the shelf exactly as it stood there.
    D2D1_MATRIX_3X2_F base; target_->GetTransform(&base);
    ComPtr<ID2D1DeviceContext> device_context;
    target_.As(&device_context);
    auto* context = device_context.Get();
    const float centre_x = (front.left + front.right) * 0.5f, centre_y = (front.top + front.bottom) * 0.5f;
    const float eye = fh * 2.7f * S;
    // A picture onto a face (device pixels; without Direct2D 1.1 an affine fill through its outline).
    const auto picture = [&](ID2D1Bitmap* bitmap, ID2D1BitmapBrush* fallback, const perspective::Face& face, const perspective::Camera& camera,
                             D2D1_RECT_F source, float opacity) {
        if (!bitmap || opacity < 0.002f) return;
        const float sw = source.right - source.left, sh = source.bottom - source.top;
        if (sw < 1 || sh < 1) return;
        if (context) {
            const auto matrix = perspective::matrix(face, sw, sh, camera);
            context->DrawBitmap(bitmap, D2D1::RectF(0, 0, sw, sh), opacity, D2D1_INTERPOLATION_MODE_ANISOTROPIC, &source, &matrix);
            return;
        }
        if (!fallback) return;
        D2D1_POINT_2F q[4];
        face.corners(camera, q);
        const float m11 = (q[1].x - q[0].x) / sw, m12 = (q[1].y - q[0].y) / sw, m21 = (q[3].x - q[0].x) / sh, m22 = (q[3].y - q[0].y) / sh;
        fallback->SetTransform(D2D1::Matrix3x2F(m11, m12, m21, m22, q[0].x - source.left * m11 - source.top * m21, q[0].y - source.left * m12 - source.top * m22));
        fallback->SetOpacity(opacity);
        if (const auto clip = polygon(factory_.Get(), q, 4)) target_->FillGeometry(clip.Get(), fallback);
    };
    // The game's art onto a face, as the shelf prints it (a square cover centred over its own blur).
    const auto print = [&](const perspective::Face& face, const perspective::Camera& camera, float face_aspect, float opacity) {
        if (!art) return;
        auto* bitmap = frozen && grayBrush(*art) ? art->gray_bitmap.Get() : art->bitmap.Get();
        auto* fallback = frozen && art->gray_brush ? art->gray_brush.Get() : art->brush.Get();
        if (art == poster || !context) { picture(bitmap, fallback, face, camera, perspective::cropTo(bitmap->GetSize(), face_aspect), opacity * reveal(*art)); return; }
        if (auto* blur = blurredArt(*art)) picture(blur, nullptr, face, camera, perspective::cropTo(blur->GetSize(), face_aspect), opacity * reveal(*art) * 0.85f);
        picture(bitmap, fallback, face_aspect < 1 ? face.inset(0, (1 - face_aspect) * 0.5f) : face.inset((1 - 1 / face_aspect) * 0.5f, 0), camera,
                perspective::cropTo(bitmap->GetSize(), 1), opacity * reveal(*art));
    };
    // The package's front onto a face (package_renderer.cpp): the shelf's very drawing.
    const float aspect = fw / fh;
    const auto packageFace = [&](const perspective::Face& face, const perspective::Camera& camera, float gloss, float strength, float shade, bool shade_right) {
        PackageFront f;
        f.kind = kind; f.platform = platform; f.aspect = aspect; f.alpha = alpha; f.scale = S;
        f.gloss = gloss; f.gloss_strength = strength; f.shade = shade; f.shade_right = shade_right;
        f.dim = !game.installed;
        f.at = [&face, &camera](float s, float t) { return camera.project(face.origin + face.u * s + face.v * t); };
        const auto part = [&face](float l, float t, float r, float b) { return perspective::Face{face.origin + face.u * l + face.v * t, face.u * (r - l), face.v * (b - t)}; };
        f.picture = [&, part](float l, float t, float r, float b, ID2D1Bitmap* bitmap, D2D1_RECT_F source, float opacity) {
            picture(bitmap, nullptr, part(l, t, r, b), camera, source, opacity);
        };
        f.print = [&, part](float l, float t, float r, float b, float art_aspect, float opacity) { print(part(l, t, r, b), camera, art_aspect, opacity); };
        drawPackageFront(f);
    };
    // A face seen from the camera at (0, 0, -eye) (world origin at the case's centre).
    const auto facing = [&](perspective::Vec3 centre, perspective::Vec3 normal) {
        return perspective::dot(normal, perspective::Vec3{0, 0, -eye} - centre) > 0;
    };

    if (kind.package == game_case::Package::big_box) {
        // An Amiga big box. It leaves the shelf face on, then the camera rises a little above it: its
        // top (printed, with the flap at the back edge folding open), the opening and the diskette
        // rising out of it, in front of the back wall and behind the front.
        const float above = phase.travel * fh * 1.05f; // From the box's middle to a little above its top.
        const perspective::Camera camera{centre_x * S, (centre_y - above) * S, eye};
        const float rise = above * S; // The camera's origin moved up: the box sits lower around it.
        const float ox = -fw * 0.5f * S, oy = -fh * 0.5f * S + rise, w = fw * S, h = fh * S, d = fw * game_case::depth(kind) * S;
        const perspective::Face face{{ox, oy, 0}, {w, 0, 0}, {0, h, 0}};
        const float flap = game_case::easeInOut((time - game_case::lid_start) / game_case::lid_time) * game_case::flap_angle;
        const perspective::Face top{{ox, oy, d}, {w, 0, 0}, {0, 0, -d}};
        D2D1_POINT_2F top_q[4]; top.corners(camera, top_q);
        const auto print_colour = faded(look.rim, alpha);
        const auto board = D2D1::ColorF(0.72f, 0.68f, 0.61f, alpha);
        target_->SetTransform(D2D1::Matrix3x2F::Identity());
        const float c = std::cos(flap), s = std::sin(flap);
        const perspective::Face lid{{ox, oy, d}, {w, 0, 0}, {0, -d * s, -d * c}};
        D2D1_POINT_2F lid_q[4]; lid.corners(camera, lid_q);
        const auto drawFlap = [&] {
            const auto shape = polygon(factory_.Get(), lid_q, 4);
            if (!shape) return;
            // Its outside (the top's print, the name along it) while it faces up, then its inside (plain board).
            const bool outside = flap < 1.5708f;
            brush(outside ? print_colour : board); target_->FillGeometry(shape.Get(), solid_.Get());
            if (outside && flap < 1.2f) {
                D2D1_MATRIX_3X2_F was; target_->GetTransform(&was);
                const float left = (lid_q[0].x + lid_q[3].x) * 0.5f, mid = (lid_q[0].y + lid_q[1].y + lid_q[2].y + lid_q[3].y) * 0.25f;
                const float span = (lid_q[1].x - lid_q[0].x), band = std::abs(lid_q[3].y - lid_q[0].y);
                if (band > 6) {
                    const float factor = std::min(1.0f, band * 0.6f / 19.0f);
                    target_->SetTransform(D2D1::Matrix3x2F::Scale(factor, factor, {left + span * 0.5f, mid}));
                    textLine(game.name, left + span * 0.08f, mid, span * 0.84f, menu_font_.Get(), D2D1::ColorF(0.20f, 0.23f, 0.40f, alpha * (1 - flap / 1.2f)),
                             DWRITE_TEXT_ALIGNMENT_CENTER);
                    target_->SetTransform(was);
                }
            }
            // Light: the outside lit from above, the inside in the box's shade until it turns to the lamp.
            brush(gray(0, (outside ? 0.06f + 0.10f * s : 0.30f - 0.12f * (flap - 1.5708f)) * alpha)); target_->FillGeometry(shape.Get(), solid_.Get());
            brush(gray(1, 0.35f * alpha)); target_->DrawGeometry(shape.Get(), solid_.Get(), 0.8f * S);
        };
        // The top (once the camera is above it), then the opening as the flap lifts: dark inside, the back wall lit.
        const bool top_shown = oy > 0 || flap > 0.01f;
        if (const auto opening = top_shown ? polygon(factory_.Get(), top_q, 4) : ComPtr<ID2D1PathGeometry>{}) {
            brush(print_colour); target_->FillGeometry(opening.Get(), solid_.Get());
            const float open = std::min(1.0f, flap * 2.5f);
            brush(D2D1::ColorF(0.07f, 0.06f, 0.05f, alpha * open)); target_->FillGeometry(opening.Get(), solid_.Get());
            const D2D1_POINT_2F wall[4]{top_q[0], top_q[1], {top_q[1].x, top_q[1].y + (top_q[2].y - top_q[1].y) * 0.35f}, {top_q[0].x, top_q[0].y + (top_q[3].y - top_q[0].y) * 0.35f}};
            if (const auto back = polygon(factory_.Get(), wall, 4)) { brush(D2D1::ColorF(0.35f, 0.32f, 0.28f, 0.6f * alpha * open)); target_->FillGeometry(back.Get(), solid_.Get()); }
            brush(gray(1, 0.25f * alpha)); target_->DrawGeometry(opening.Get(), solid_.Get(), 0.7f * S);
        }
        if (flap >= 1.25f) drawFlap(); // Folded back: behind the diskette.
        target_->SetTransform(base);
        // The diskette: up out of the opening, in front of the back wall, behind the front.
        if (media > 0.001f) {
            const float size = layout.floppySize() * k;
            const auto spot = layout.media(front, media);
            const float dh = size / media_layout::aspect(media_layout::Kind::floppy);
            target_->SetTransform(D2D1::Matrix3x2F::Rotation(spot.turn * 57.2958f, {spot.x, spot.y}) * base);
            drawFloppyDisk(D2D1::RectF(spot.x - size * 0.5f, spot.y - dh * 0.5f, spot.x + size * 0.5f, spot.y + dh * 0.5f), alpha,
                           0.5f + 0.16f * std::sin(elapsed_ * 0.30f) - 0.4f * (1 - media), cover ? cover : poster, frozen, game.name);
            target_->SetTransform(base);
        }
        target_->SetTransform(D2D1::Matrix3x2F::Identity());
        // The front: the shelf's own drawing; the print stays square-edged cardboard.
        packageFace(face, camera, 0.5f, 0.075f, 0, false);
        if (!art) {
            target_->SetTransform(base);
            text(game.name, front.left + fw * 0.1f, front.top + fh * 0.4f, fw * 0.8f, fh * 0.3f, settings_heading_font_.Get(), D2D1::ColorF(0.2f, 0.22f, 0.36f, alpha));
            target_->SetTransform(D2D1::Matrix3x2F::Identity());
        }
        if (flap < 1.25f && top_shown) drawFlap();
        target_->SetTransform(base);
    } else {
        // A book-like case: the tray (its back half, a moulded well), the medium in it, and the lid
        // (the front) hinged at the spine, swinging towards the viewer and over to the left.
        const bool switch_case = kind.package == game_case::Package::switch_case;
        const perspective::Camera camera{centre_x * S, centre_y * S, eye};
        const float w = fw * S, h = fh * S, d = fw * game_case::depth(kind) * S;
        const perspective::Face closed{{-w * 0.5f, -h * 0.5f, 0}, {w, 0, 0}, {0, h, 0}};
        const float angle = phase.lid * game_case::lid_angle;
        target_->SetTransform(D2D1::Matrix3x2F::Identity());
        if (angle < 0.002f) {
            // Closed: exactly the shelf's case.
            packageFace(closed, camera, 0.5f, 0.075f, 0, false);
            if (!art) {
                target_->SetTransform(base);
                text(game.name, front.left + fw * 0.1f, front.top + fh * 0.4f, fw * 0.8f, fh * 0.3f, menu_font_.Get(), gray(0.88f, alpha));
            }
            target_->SetTransform(base);
        } else {
            const auto outline = package_look::outline(look.corner, look.corner * aspect);
            // The outline inset by `inset` (of the width) at depth z.
            const auto ring = [&](float inset, float z) {
                std::vector<D2D1_POINT_2F> points;
                const float it = inset * aspect;
                for (const auto& p : outline) points.push_back(camera.project({-w * 0.5f + (inset + p.x * (1 - 2 * inset)) * w, -h * 0.5f + (it + p.y * (1 - 2 * it)) * h, z}));
                return points;
            };
            // The console's plastic: the tray in the rim's colour (a clear case: the insert's white behind it).
            const auto shell = look.rim;
            const bool bright = package_look::brightness(look.tray) > 0.6f;
            const bool bright_rim = package_look::brightness(shell) > 0.6f;
            const auto wall = bright ? mix(look.tray, gray(0.55f), 0.25f) : mix(look.tray, gray(0), 0.35f);
            const auto floor_colour = bright ? look.tray : mix(look.tray, gray(0), 0.10f);
            const float floor_z = d * 0.88f;
            const float lift = 1 / (1 + floor_z / eye); // Things on the floor look this much smaller.
            const auto floorTransform = [&](float z) {
                const float f = 1 / (1 + z / eye);
                return D2D1::Matrix3x2F::Scale(f, f, {centre_x, centre_y}) * base;
            };
            // The insert's inner print (Switch): one picture across the open case, its left half on the
            // lid's inside, its right half on the tray's floor, seen through the plastic's tint.
            ID2D1Bitmap* inner_bitmap = nullptr;
            ID2D1BitmapBrush* inner_fallback = nullptr;
            D2D1_RECT_F inner_source{};
            if (look.inside_art && art) {
                if ((look.inside_gray || frozen) && grayBrush(*art)) { inner_bitmap = art->gray_bitmap.Get(); inner_fallback = art->gray_brush.Get(); }
                else { inner_bitmap = art->bitmap.Get(); inner_fallback = art->brush.Get(); }
                inner_source = perspective::cropTo(inner_bitmap->GetSize(), aspect * 2);
            }
            const auto innerPrint = [&](const perspective::Face& face, bool right_half, ID2D1Geometry* clip, D2D1_RECT_F clip_bounds) {
                if (!look.inside_art) return;
                target_->PushLayer(D2D1::LayerParameters(clip_bounds, clip), nullptr);
                if (inner_bitmap) {
                    auto source = inner_source;
                    const float middle = (source.left + source.right) * 0.5f;
                    (right_half ? source.left : source.right) = middle;
                    picture(inner_bitmap, inner_fallback, face, camera, source, alpha * reveal(*art));
                }
                brush(faded(look.inside_tint, alpha)); target_->FillGeometry(clip, solid_.Get());
                target_->PopLayer();
            };
            // Glossy plastic: the lamp's band across it, from `from` to `to`.
            const auto shine = [&](ID2D1Geometry* shape, D2D1_POINT_2F from, D2D1_POINT_2F to, float band, float strength) {
                if (!shelf_gloss_) return;
                shelf_gloss_->SetStartPoint({std::lerp(from.x, to.x, band - 0.4f), std::lerp(from.y, to.y, band - 0.4f)});
                shelf_gloss_->SetEndPoint({std::lerp(from.x, to.x, band + 0.4f), std::lerp(from.y, to.y, band + 0.4f)});
                shelf_gloss_->SetOpacity(strength * alpha); target_->FillGeometry(shape, shelf_gloss_.Get());
            };
            const float glossy = look.inside_art ? 0.20f : 0.07f;
            // The tray: its rim, the walls of the well, its floor.
            {
                const auto rim = ring(0, 0);
                if (const auto shape = polygon(factory_.Get(), rim)) { brush(faded(shell, alpha)); target_->FillGeometry(shape.Get(), solid_.Get()); }
                const float thickness = switch_case ? 0.030f : 0.026f;
                if (const auto well = polygon(factory_.Get(), ring(thickness, 0))) { brush(faded(wall, alpha)); target_->FillGeometry(well.Get(), solid_.Get()); }
                const auto floor_points = ring(thickness, floor_z);
                if (const auto bottom = polygon(factory_.Get(), floor_points)) {
                    const auto b = boundsOf(floor_points);
                    brush(faded(floor_colour, alpha)); target_->FillGeometry(bottom.Get(), solid_.Get());
                    innerPrint(perspective::Face{{-w * 0.5f, -h * 0.5f, floor_z}, {w, 0, 0}, {0, h, 0}}, true, bottom.Get(), b);
                    // The lamp above: the floor's upper part in the wall's shade, its foot lit.
                    if (package_light_) {
                        package_light_->SetStartPoint(D2D1::Point2F(centre_x * S, b.top));
                        package_light_->SetEndPoint(D2D1::Point2F(centre_x * S, b.bottom));
                        package_light_->SetOpacity(alpha); target_->FillGeometry(bottom.Get(), package_light_.Get());
                    }
                    if (shelf_shade_) { // The upper wall's shadow on the floor.
                        shelf_shade_->SetStartPoint({centre_x * S, b.top}); shelf_shade_->SetEndPoint({centre_x * S, b.top + (b.bottom - b.top) * 0.10f});
                        shelf_shade_->SetOpacity((bright ? 0.10f : 0.35f) * alpha); target_->FillGeometry(bottom.Get(), shelf_shade_.Get());
                    }
                    shine(bottom.Get(), {b.left, b.top}, {b.right, b.bottom}, 0.62f, glossy);
                    brush(gray(0, 0.30f * alpha)); target_->DrawGeometry(bottom.Get(), solid_.Get(), 1.2f * S);
                }
                // The rim's lit top edge and the spine's hinge line.
                brush(gray(1, (bright_rim || look.clear || look.inside_art ? 0.35f : 0.16f) * alpha));
                if (const auto shape = polygon(factory_.Get(), rim)) target_->DrawGeometry(shape.Get(), solid_.Get(), 0.9f * S);
            }
            // What lies on the floor, drawn flat in DIPs and scaled to its depth.
            const float cx = centre_x, cy = centre_y;
            target_->SetTransform(floorTransform(floor_z));
            if (switch_case) {
                if (look.clear && bright && !look.inside_art) {
                    // The insert's back through the clear plastic: white card, a few lines of small print at its foot.
                    for (int row = 0; row < 5; ++row) {
                        const float y = front.bottom - fh * (0.115f - row * 0.016f);
                        line(front.left + fw * 0.14f, y, front.left + fw * (row == 4 ? 0.40f : 0.55f), y, gray(0.55f, 0.55f * alpha), std::max(1.0f, fh * 0.004f));
                    }
                }
                // Moulded in the tray's plastic: lighter where it rises towards the lamp.
                const auto moulded = mix(floor_colour, bright ? gray(0.86f) : mix(look.tray, gray(1), 0.16f), 0.55f);
                const auto through = mix(floor_colour, gray(1), look.inside_art && !look.inside_gray ? 0.28f : look.inside_art ? 0.70f : 0.45f); // A slot: the light behind the case.
                // Two leaflet clips by the hinge: a tab over a slot in the floor.
                for (const float tv : {0.155f, 0.515f}) {
                    const auto slot = D2D1::RectF(front.left + fw * 0.040f, front.top + fh * tv, front.left + fw * 0.215f, front.top + fh * (tv + 0.085f));
                    const float r = fw * 0.022f;
                    brush(faded(through, alpha)); target_->FillRoundedRectangle(D2D1::RoundedRect(slot, r, r), solid_.Get());
                    brush(gray(0, 0.22f * alpha)); target_->DrawRoundedRectangle(D2D1::RoundedRect(slot, r, r), solid_.Get(), 1);
                    const auto tab = D2D1::RectF(slot.left, slot.top + fh * 0.013f, slot.left + (slot.right - slot.left) * 0.72f, slot.bottom - fh * 0.013f);
                    brush(gray(0, 0.20f * alpha)); target_->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(tab.left + 1.5f, tab.top + 2, tab.right + 1.5f, tab.bottom + 2), r * 0.6f, r * 0.6f), solid_.Get());
                    brush(faded(moulded, alpha)); target_->FillRoundedRectangle(D2D1::RoundedRect(tab, r * 0.6f, r * 0.6f), solid_.Get());
                    line(tab.left + r * 0.5f, tab.top + 0.8f, tab.right - r * 0.5f, tab.top + 0.8f, gray(1, 0.55f * alpha), 1);
                    brush(gray(0, 0.18f * alpha)); target_->DrawRoundedRectangle(D2D1::RoundedRect(tab, r * 0.6f, r * 0.6f), solid_.Get(), 0.8f);
                }
                // The closing catches on the tray's free edge, top and foot.
                for (const float tv : {0.105f, 0.825f}) {
                    const auto catch_ = D2D1::RectF(front.right - fw * 0.050f, front.top + fh * tv, front.right - fw * 0.022f, front.top + fh * (tv + 0.07f));
                    brush(faded(moulded, alpha)); target_->FillRoundedRectangle(D2D1::RoundedRect(catch_, 2, 2), solid_.Get());
                    brush(gray(0, 0.25f * alpha)); target_->DrawRoundedRectangle(D2D1::RoundedRect(catch_, 2, 2), solid_.Get(), 0.8f);
                    line(catch_.left + 1, catch_.top + 1, catch_.left + 1, catch_.bottom - 1, gray(1, 0.45f * alpha), 0.8f);
                }
                // The card's holder, low in the tray: a raised frame round a recess, its side clips, a centre
                // stud, and the arrow beside it that shows where to press.
                const float cw = layout.cartridgeWidth() * k * game_case::Layout::cartridge_inside, ch = cw / media_layout::aspect(media_layout::Kind::cartridge);
                const float hx = cx + fw * game_case::Layout::holder_x, hy = cy + fh * game_case::Layout::holder_y;
                const auto frame = D2D1::RectF(hx - cw * 0.68f, hy - ch * 0.62f, hx + cw * 0.68f, hy + ch * 0.62f);
                const float fr = cw * 0.12f;
                brush(gray(0, 0.22f * alpha)); target_->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(frame.left + 2, frame.top + 3, frame.right + 2, frame.bottom + 3), fr, fr), solid_.Get());
                brush(faded(moulded, alpha)); target_->FillRoundedRectangle(D2D1::RoundedRect(frame, fr, fr), solid_.Get());
                brush(gray(1, 0.50f * alpha)); target_->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(frame.left + 0.5f, frame.top + 0.5f, frame.right - 0.5f, frame.bottom - 0.5f), fr, fr), solid_.Get(), 1);
                brush(gray(0, 0.20f * alpha)); target_->DrawRoundedRectangle(D2D1::RoundedRect(frame, fr, fr), solid_.Get(), 1);
                const auto bed = D2D1::RectF(hx - cw * 0.54f, hy - ch * 0.53f, hx + cw * 0.54f, hy + ch * 0.53f);
                brush(faded(mix(floor_colour, gray(0), 0.10f), alpha)); target_->FillRoundedRectangle(D2D1::RoundedRect(bed, cw * 0.06f, cw * 0.06f), solid_.Get());
                if (shelf_shade_) { // The frame's wall shades the recess's top.
                    shelf_shade_->SetStartPoint({hx, bed.top}); shelf_shade_->SetEndPoint({hx, bed.top + ch * 0.25f});
                    shelf_shade_->SetOpacity(0.30f * alpha); target_->FillRoundedRectangle(D2D1::RoundedRect(bed, cw * 0.06f, cw * 0.06f), shelf_shade_.Get());
                }
                brush(gray(0, 0.30f * alpha)); target_->DrawRoundedRectangle(D2D1::RoundedRect(bed, cw * 0.06f, cw * 0.06f), solid_.Get(), 1);
                for (const float side : {-1.0f, 1.0f}) for (const float t : {-0.34f, 0.34f}) {
                    // A gap cut in the frame's wall, the clip's nub inside it.
                    const float x0 = hx + side * cw * 0.61f, y0 = hy + t * ch;
                    const auto gap = D2D1::RectF(x0 - cw * 0.075f, y0 - ch * 0.075f, x0 + cw * 0.075f, y0 + ch * 0.075f);
                    brush(faded(through, alpha)); target_->FillRectangle(gap, solid_.Get());
                    const auto nub = D2D1::RectF(std::min(x0, x0 - side * cw * 0.10f), y0 - ch * 0.045f, std::max(x0, x0 - side * cw * 0.10f), y0 + ch * 0.045f);
                    brush(faded(moulded, alpha)); target_->FillRectangle(nub, solid_.Get());
                    brush(gray(0, 0.25f * alpha)); target_->DrawRectangle(nub, solid_.Get(), 0.7f);
                }
                const auto stud = D2D1::RectF(hx - cw * 0.13f, hy - cw * 0.13f, hx + cw * 0.13f, hy + cw * 0.13f);
                brush(gray(0, 0.18f * alpha)); target_->DrawRoundedRectangle(D2D1::RoundedRect(stud, 2, 2), solid_.Get(), 1);
                brush(gray(0, 0.20f * alpha)); target_->FillEllipse(D2D1::Ellipse({hx, frame.bottom - ch * 0.02f}, cw * 0.22f, ch * 0.06f), solid_.Get());
                {
                    // The arrow, pointing at the holder.
                    const float ax = frame.right + cw * 0.16f, size = cw * 0.10f;
                    const D2D1_POINT_2F arrow[3]{{ax - size, hy}, {ax + size * 0.4f, hy - size}, {ax + size * 0.4f, hy + size}};
                    if (const auto shape = polygon(factory_.Get(), arrow, 3)) {
                        brush(faded(moulded, alpha)); target_->FillGeometry(shape.Get(), solid_.Get());
                        brush(gray(0, 0.22f * alpha)); target_->DrawGeometry(shape.Get(), solid_.Get(), 0.8f);
                    }
                }
            } else {
                // The hub the disc sits on: a raised ring, the rosette's teeth that grip its hole, the centre button.
                // Moulded in the tray's own plastic (white in a Wii case, green in an Xbox 360 one).
                const float r = layout.discRadius() * k;
                const auto groove = bright ? gray(0, 0.10f * alpha) : gray(1, 0.045f * alpha);
                const auto hub = bright ? mix(look.tray, gray(0), 0.10f) : mix(look.tray, gray(0.20f), 0.45f);
                brush(groove); target_->DrawEllipse(D2D1::Ellipse({cx, cy}, r * 1.005f, r * 1.005f), solid_.Get(), 2);
                brush(bright ? gray(0, 0.06f * alpha) : gray(1, 0.03f * alpha)); target_->DrawEllipse(D2D1::Ellipse({cx, cy}, r * 0.55f, r * 0.55f), solid_.Get(), 1.5f);
                brush(faded(hub, alpha)); target_->FillEllipse(D2D1::Ellipse({cx, cy}, r * 0.27f, r * 0.27f), solid_.Get());
                brush(gray(1, (bright ? 0.5f : 0.10f) * alpha)); target_->DrawEllipse(D2D1::Ellipse({cx, cy - r * 0.004f}, r * 0.27f, r * 0.27f), solid_.Get(), std::max(1.0f, r * 0.008f));
                brush(gray(0, (bright ? 0.25f : 0.6f) * alpha)); target_->DrawEllipse(D2D1::Ellipse({cx, cy + r * 0.006f}, r * 0.27f, r * 0.27f), solid_.Get(), std::max(1.0f, r * 0.010f));
                const auto teeth = faded(mix(hub, gray(bright ? 0.0f : 1.0f), 0.22f), alpha);
                for (int tooth = 0; tooth < 12; ++tooth) {
                    const float a = tooth * 0.5236f;
                    const float ca = std::cos(a), sa = std::sin(a);
                    line(cx + ca * r * 0.06f, cy + sa * r * 0.06f, cx + ca * r * 0.115f, cy + sa * r * 0.115f, teeth, std::max(1.5f, r * 0.03f));
                    line(cx + ca * r * 0.06f, cy + sa * r * 0.06f - 0.6f, cx + ca * r * 0.115f, cy + sa * r * 0.115f - 0.6f, gray(1, 0.12f * alpha), 0.6f);
                }
                brush(gray(0.05f, alpha)); target_->FillEllipse(D2D1::Ellipse({cx, cy}, r * 0.05f, r * 0.05f), solid_.Get());
                brush(gray(1, 0.18f * alpha)); target_->DrawEllipse(D2D1::Ellipse({cx, cy}, r * 0.05f, r * 0.05f), solid_.Get(), 0.8f);
            }
            target_->SetTransform(D2D1::Matrix3x2F::Identity());

            // The medium: in its place on the floor, then out to the right (and nearer the viewer).
            const auto drawMedium = [&] {
                const auto spot = layout.media(front, media);
                const float out = std::clamp(media, 0.0f, 1.1f);
                const float f = std::lerp(lift, 1.0f, std::min(1.0f, out));
                if (kind.media == game_case::Media::cartridge) {
                    // It lifts out of the holder (larger, its shadow spreading), then slides to the hand.
                    const float grow = std::lerp(game_case::Layout::cartridge_inside, 1.0f, smoothFade(std::min(1.0f, out * 1.15f)));
                    const float cw = layout.cartridgeWidth() * k * grow * f, ch = cw / media_layout::aspect(media_layout::Kind::cartridge);
                    const float x = std::lerp(centre_x + (spot.x - centre_x) * lift, spot.x, std::min(1.0f, out));
                    const float y = std::lerp(centre_y + (spot.y - centre_y) * lift, spot.y, std::min(1.0f, out));
                    target_->SetTransform(D2D1::Matrix3x2F::Rotation(spot.turn * 57.2958f, {x, y}) * base);
                    drawGameCartridge(D2D1::RectF(x - cw * 0.5f, y - ch * 0.5f, x + cw * 0.5f, y + ch * 0.5f), alpha, kind, platform, poster ? poster : cover, frozen, game.name);
                    target_->SetTransform(base);
                    if (out < 0.25f) {
                        // The holder's four side clips over the card's edges: it snaps out past them.
                        target_->SetTransform(floorTransform(floor_z * 0.8f));
                        const float iw = layout.cartridgeWidth() * k * game_case::Layout::cartridge_inside, ih = iw / media_layout::aspect(media_layout::Kind::cartridge);
                        const float hx = centre_x + fw * game_case::Layout::holder_x, hy = centre_y + fh * game_case::Layout::holder_y;
                        const float clip = 1 - out / 0.25f;
                        const auto moulded = faded(mix(look.tray, gray(1), bright ? 0.35f : 0.16f), alpha * clip);
                        for (const float side : {-1.0f, 1.0f}) for (const float t : {-0.34f, 0.34f}) {
                            const float x0 = hx + side * iw * 0.61f, y0 = hy + t * ih;
                            const auto nub = D2D1::RectF(std::min(x0, x0 - side * iw * 0.15f), y0 - ih * 0.045f, std::max(x0, x0 - side * iw * 0.15f), y0 + ih * 0.045f);
                            brush(gray(0, 0.25f * alpha * clip)); target_->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(nub.left, nub.top + 1.5f, nub.right, nub.bottom + 1.5f), 1.5f, 1.5f), solid_.Get());
                            brush(moulded); target_->FillRoundedRectangle(D2D1::RoundedRect(nub, 1.5f, 1.5f), solid_.Get());
                            brush(gray(1, 0.40f * alpha * clip)); target_->DrawRoundedRectangle(D2D1::RoundedRect(nub, 1.5f, 1.5f), solid_.Get(), 0.7f);
                        }
                        target_->SetTransform(base);
                    }
                } else {
                    // The disc turns all the while; it rolls a little more as it slides out.
                    const float x = std::lerp(centre_x + (spot.x - centre_x) * lift, spot.x, std::min(1.0f, out));
                    drawGameDisc(x, centre_y + (spot.y - centre_y) * f, layout.discRadius() * k * f, alpha, elapsed_ * 40 + media * 150, poster ? poster : cover, scan, frozen,
                                 game.name);
                }
            };

            // The lid: a thin slab hinged at the front's left edge.
            const perspective::Vec3 u = perspective::turned({w, 0, 0}, angle), v{0, h, 0};
            const perspective::Vec3 n = perspective::turned({0, 0, 1}, angle); // Into the case, when closed.
            const float slab = d * 0.16f;
            const perspective::Face outer{closed.origin, u, v};
            const perspective::Face inner{closed.origin + n * slab + u, u * -1.0f, v}; // Read from inside, unmirrored.
            const bool outside = facing(outer.centre(), n * -1.0f);
            const auto drawLid = [&] {
                target_->SetTransform(D2D1::Matrix3x2F::Identity());
                // Its thickness: the free edge (and the top and foot when they face the eye).
                const auto o = closed.origin;
                const struct { perspective::Vec3 a, b, normal; } edges[] = {
                    {o + u, o + u + v, perspective::normalized(u)}, {o, o + u, {0, -1, 0}}, {o + v + u, o + v, {0, 1, 0}}};
                for (const auto& edge : edges) {
                    if (!facing((edge.a + edge.b) * 0.5f + n * (slab * 0.5f), edge.normal)) continue;
                    const D2D1_POINT_2F q[4]{camera.project(edge.a), camera.project(edge.b), camera.project(edge.b + n * slab), camera.project(edge.a + n * slab)};
                    if (const auto strip = polygon(factory_.Get(), q, 4)) {
                        brush(faded(look.edge, alpha)); target_->FillGeometry(strip.Get(), solid_.Get());
                        brush(gray(0, 0.25f * alpha)); target_->FillGeometry(strip.Get(), solid_.Get());
                    }
                }
                const float turned = std::sin(std::min(angle, 1.5708f));
                if (outside) {
                    // The front, as on the shelf: the lamp's band slides across it and it darkens as it turns.
                    packageFace(outer, camera, 0.5f - phase.lid * 0.7f, 0.075f + 0.05f * turned, 0.45f * turned, false);
                } else {
                    // Its inside, read from the left: plastic, and what it holds.
                    std::vector<D2D1_POINT_2F> points;
                    for (const auto& p : outline) points.push_back(camera.project(inner.origin + inner.u * p.x + inner.v * p.y));
                    const auto shape = polygon(factory_.Get(), points);
                    if (!shape) { target_->SetTransform(base); return; }
                    // The lid's inside: its plastic (clear: the insert's white back; Switch: as the tray).
                    const auto inside = look.inside_art ? look.tray : look.clear ? D2D1::ColorF(0.92f, 0.925f, 0.92f) : mix(shell, gray(0), 0.08f);
                    const bool bright_inside = package_look::brightness(inside) > 0.6f;
                    brush(faded(inside, alpha)); target_->FillGeometry(shape.Get(), solid_.Get());
                    const auto part = [&](float l, float t, float r, float b) { return perspective::Face{inner.origin + inner.u * l + inner.v * t, inner.u * (r - l), inner.v * (b - t)}; };
                    const auto quad = [&](const perspective::Face& face, D2D1_POINT_2F (&q)[4]) { face.corners(camera, q); return polygon(factory_.Get(), q, 4); };
                    if (!switch_case) {
                        // The manual in its clips: the game's cover on its front, a shadow under it.
                        const auto booklet = part(0.09f, 0.065f, 0.89f, 0.935f);
                        D2D1_POINT_2F bq[4];
                        const auto shadow = quad(part(0.10f, 0.075f, 0.905f, 0.95f), bq);
                        if (shadow) { brush(gray(0, 0.45f * alpha)); target_->FillGeometry(shadow.Get(), solid_.Get()); }
                        if (const auto paper = quad(booklet, bq)) {
                            brush(gray(0.95f, alpha)); target_->FillGeometry(paper.Get(), solid_.Get());
                            if (art) print(part(0.10f, 0.075f, 0.88f, 0.925f), camera, aspect * 0.78f / 0.85f, alpha);
                            else { brush(D2D1::ColorF(0.16f, 0.20f, 0.30f, alpha)); target_->FillGeometry(paper.Get(), solid_.Get()); }
                            brush(gray(0, 0.25f * alpha)); target_->DrawGeometry(paper.Get(), solid_.Get(), 0.8f * S);
                        }
                        for (const float tu : {0.07f, 0.81f}) for (const float tv : {0.045f, 0.925f}) {
                            D2D1_POINT_2F tq[4];
                            if (const auto clip = quad(part(tu, tv, tu + 0.12f, tv + 0.03f), tq)) {
                                brush(faded(mix(inside, gray(bright_inside ? 0.70f : 0.0f), 0.40f), alpha)); target_->FillGeometry(clip.Get(), solid_.Get());
                                brush(gray(bright_inside ? 0.0f : 1.0f, 0.18f * alpha)); target_->DrawGeometry(clip.Get(), solid_.Get(), 0.8f);
                            }
                        }
                    } else if (look.inside_art) {
                        innerPrint(inner, false, shape.Get(), boundsOf(points)); // The print's left half.
                    } else {
                        if (look.clear) {
                            // The insert's back through the clear plastic: white card, the band's edge at its top, small print.
                            D2D1_POINT_2F rq[4];
                            if (const auto band = quad(part(look.border, look.border * aspect, 1 - look.border, look.border * aspect + 0.012f), rq)) {
                                brush(faded(look.band_colour, 0.8f * alpha)); target_->FillGeometry(band.Get(), solid_.Get());
                            }
                            for (int row = 0; row < 6; ++row) {
                                const float t = 0.80f + row * 0.022f;
                                const auto a = camera.project(inner.origin + inner.u * 0.12f + inner.v * t);
                                const auto b = camera.project(inner.origin + inner.u * (row == 5 ? 0.55f : 0.88f) + inner.v * t);
                                line(a.x, a.y, b.x, b.y, gray(0.55f, 0.6f * alpha), std::max(1.0f, 1.2f * S));
                            }
                        }
                    }
                    // The plastic's moulded rim and its light: it faces the lamp again as it lies open.
                    const float lit = std::clamp((angle - 1.5708f) / 1.0f, 0.0f, 1.0f);
                    if (shelf_shade_) {
                        const auto hinge = camera.project(closed.origin + v * 0.5f), edge = camera.project(inner.origin + v * 0.5f);
                        shelf_shade_->SetStartPoint(edge); shelf_shade_->SetEndPoint(hinge);
                        shelf_shade_->SetOpacity((0.45f - 0.30f * lit) * alpha); target_->FillGeometry(shape.Get(), shelf_shade_.Get());
                    }
                    shine(shape.Get(), camera.project(inner.origin), camera.project(inner.origin + inner.u + inner.v), 0.2f + 0.3f * lit, glossy * (0.5f + 0.5f * lit));
                    brush(gray(bright_inside ? 0.0f : 1.0f, 0.22f * alpha)); target_->DrawGeometry(shape.Get(), solid_.Get(), 0.9f * S);
                }
                target_->SetTransform(base);
            };
            if (outside) { drawMedium(); drawLid(); } // Over the tray (and the medium inside it).
            else { drawLid(); drawMedium(); }        // Folded over to the left: the medium slides out above it.
            target_->SetTransform(base);
        }
    }

    // The right half: title, facts, buttons, description, the platform on its tile.
    // Row after row: each fades in as it rises the last few pixels to its place.
    if (phase.panel < 0.003f) return;
    const auto row = [&](int index) { return game_case::panelRow(time, index); };
    const auto riseOf = [](float shown) { return 22 * (1 - shown); };
    const float left = layout.panelLeft(), right = layout.panelRight(), width = right - left;
    float panel = row(0), rise = riseOf(panel);
    float y = layout.titleTop() + rise;
    bool logo_drawn = false;
    if (selected_ < logos_.size() && logos_[selected_].bitmap) {
        const auto& logo = logos_[selected_];
        const auto pixels = logo.bitmap->GetPixelSize();
        const auto source = logo.has_ink_bounds ? logo.ink_bounds : D2D1::RectF(0, 0, static_cast<float>(pixels.width), static_cast<float>(pixels.height));
        const float lw = source.right - source.left, lh = source.bottom - source.top;
        if (lw > 0 && lh > 0) {
            const float f = std::min(std::min(width, 520.0f) / lw, 110.0f / lh);
            target_->DrawBitmap(logo.bitmap.Get(), D2D1::RectF(left, y + 110 - lh * f, left + lw * f, y + 110), panel * reveal(logo),
                                D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, &source);
            logo_drawn = true;
        }
    }
    if (!logo_drawn) paragraph(game.name, left, y + 20, width, settings_heading_font_.Get(), gray(0.97f, panel), 2);
    // Year · developer · playtime, and the game's state.
    std::wstring facts;
    const auto add = [&](const std::wstring& value) { if (!value.empty()) facts += (facts.empty() ? L"" : L"   ·   ") + value; };
    const auto* info = context_->preview_waiting ? nullptr : context_->preview;
    if (info) {
        add(gameReleaseYear(info->release_date)); add(info->developer.empty() ? info->publisher : info->developer);
        if (info->playtime_minutes && *info->playtime_minutes) add(std::wstring(tr(L"Czas gry: ")) + formatPlaytime(*info->playtime_minutes));
    }
    if (game.installation.active()) add(installationText(game.installation));
    else if (!game.installed && game.kind != GameKind::catalog && sessionOf(game) == SessionState::none)
        add(localFileGame(game.source) ? std::wstring(missingFileLabel(game)) : std::wstring(tr(L"Do instalacji")));
    panel = row(1); rise = riseOf(panel);
    if (!facts.empty()) textLine(facts, left, layout.titleTop() + rise + 130, width, settings_body_font_.Get(), gray(0.74f, panel));
    // The buttons.
    panel = row(2); rise = riseOf(panel);
    for (int i = 0; i < game_case::button_count; ++i) {
        const auto r = layout.button(i);
        const auto box = D2D1::RectF(r.left, r.top + rise, r.right, r.bottom + rise);
        const bool enabled = i == 1 || context_->game_case_action_enabled;
        const auto ink = drawButton(box, state->focus == i && state->open ? 1.0f : 0.0f, panel, true, state->hover == i, enabled);
        if (i == 0 && drawInstallProgress(game, box, ink, gray(ink.r, 0.25f * ink.a), menu_font_.Get())) continue;
        textLine(i == 0 ? context_->game_case_action : std::wstring(tr(L"Szczegóły")), box.left, (box.top + box.bottom) * 0.5f, box.right - box.left, menu_font_.Get(), ink,
                 DWRITE_TEXT_ALIGNMENT_CENTER);
    }
    // A short description (what Szczegóły shows; its genres while none is known).
    const auto description = layout.description();
    panel = row(3); rise = riseOf(panel);
    // A hairline over it, drawn out from the left as it appears.
    if (panel > 0.01f) {
        const float line_y = description.top - 18 + rise;
        brush(gray(1, 0.10f * panel));
        target_->FillRectangle(D2D1::RectF(left, line_y, left + width * panel, line_y + 1), solid_.Get());
    }
    float bottom = description.top + rise;
    if (info && !info->description.empty()) {
        const int lines = std::max(2, static_cast<int>((description.bottom - description.top) / 27));
        bottom += paragraph(info->description, left, description.top + rise, width, details_body_font_.Get(), gray(0.80f, panel), lines);
    } else if (info && !info->genres.empty()) {
        bottom += paragraph(info->genres, left, description.top + rise, width, details_body_font_.Get(), gray(0.66f, panel), 2);
    }
    // The platform: its logo on a small horizontal tile of its colour.
    panel = row(4); rise = riseOf(panel);
    bottom += riseOf(row(4)) - riseOf(row(3));
    if (!platform.empty() && panel > 0.003f) {
        constexpr float mark_height = 24, mark_width = 190;
        const float mark = hasPlatformMark(platform) ? platformMarkWidth(platform, mark_height, mark_width) : std::min(mark_width, textAdvance(platform, settings_body_font_.Get()));
        const float tile_width = mark + 40;
        const float top = std::max(bottom + 26, description.top + rise + 60);
        const auto tile = D2D1::RectF(left, top, left + tile_width, top + 48);
        const auto colour = game.platform.empty() || game.platform == L"PC" ? D2D1::ColorF(0.13f, 0.20f, 0.30f) : render_detail::platformColor(game.platform);
        brush(faded(colour, 0.96f * panel)); target_->FillRoundedRectangle(D2D1::RoundedRect(tile, 10, 10), solid_.Get());
        brush(gray(1, 0.14f * panel)); target_->DrawRoundedRectangle(D2D1::RoundedRect(tile, 10, 10), solid_.Get(), 1);
        drawPlatformMark(platform, tile.left + 20, (tile.top + tile.bottom) * 0.5f, mark_height, mark_width, panel, settings_body_font_.Get());
    }
    // Hints.
    panel = row(5);
    if (panel < 0.003f) return;
    const auto device = context_->hint_device;
    const bool pad = device != HintDevice::keyboard;
    const std::wstring accept = pad ? (device == HintDevice::playstation ? L"✕" : device == HintDevice::nintendo ? L"B" : L"A") : L"Enter";
    const std::wstring close = pad ? (device == HintDevice::playstation ? L"○" : device == HintDevice::nintendo ? L"A" : L"B") : L"Esc";
    textLine(accept + L"  " + tr(L"Wybierz") + L"      " + close + L"  " + tr(L"Zamknij pudełko"), left, H - 52, width, settings_body_font_.Get(), gray(0.55f, panel));
}
