// The case in the hand, held up closed (game_case_view.hpp): its front, spine and back are printed
// by Direct2D (the shelf's own package front, the spine's own scan or its name, the back of the box
// or one made up from the game's art, logo and description) and the case is drawn in real 3D (box3d.hpp), turned
// as the player turns it. Without Direct3D (Linux, the software capture) its faces are drawn flat
// in perspective instead. Also the right half shared with the open case: title, facts, the buttons
// (Graj, Instrukcja, Szczegóły), the description, the platform and the hints (R3: Pokaż środek).
#include "renderer.hpp"
#include "game_case_view.hpp"
#include "package_look.hpp"
#include "perspective.hpp"
#include "renderer_internal.hpp"
#include "../library/steam_stats.hpp"

#ifdef _WIN32
#include <d2d1_1helper.h>
#endif

#include <algorithm>
#include <cmath>

using Microsoft::WRL::ComPtr;
using palette::gray;

namespace {
D2D1_COLOR_F faded(D2D1_COLOR_F c, float alpha) { c.a *= alpha; return c; }
// The case's real thickness, of its height (the shelf draws plastic cases slimmer than life).
float thickness(const game_case::Kind& kind) {
    const float of_width = kind.package == game_case::Package::big_box ? game_case::depth(kind) : game_case::depth(kind) / 0.62f;
    return of_width * game_case::aspect(kind);
}
}

void Renderer::setCaseBack(const std::string& key, ImageBytes pixels) {
    if (key != case_back_key_) { case_back_ = {}; case_back_key_ = key; }
    if (!pixels || pixels->empty() || case_back_.pixels == pixels) return;
    case_back_ = {};
    case_back_.pixels = std::move(pixels);
    case_back_.short_edge = 1536;
    createBitmap(case_back_);
}

void Renderer::setCaseSide(const std::string& key, ImageBytes pixels) {
    if (key != case_side_key_) { case_side_ = {}; case_side_key_ = key; }
    if (!pixels || pixels->empty() || case_side_.pixels == pixels) return;
    case_side_ = {};
    case_side_.pixels = std::move(pixels);
    case_side_.short_edge = 512;
    createBitmap(case_side_);
}

void Renderer::setManualPage(int page, ImageBytes pixels) {
    if (page < 0 || !pixels || pixels->empty()) return;
    if (manual_pages_.size() != manual_slots) { manual_pages_.assign(manual_slots, {}); manual_page_numbers_.assign(manual_slots, -1); }
    // Its own slot, else the slot of the page farthest from it.
    size_t slot = 0;
    int farthest = -1;
    for (size_t i = 0; i < manual_slots; ++i) {
        if (manual_page_numbers_[i] == page) { slot = i; farthest = 1 << 30; break; }
        const int distance = manual_page_numbers_[i] < 0 ? (1 << 29) : std::abs(manual_page_numbers_[i] - page);
        if (distance > farthest) { farthest = distance; slot = i; }
    }
    manual_pages_[slot] = {};
    manual_pages_[slot].pixels = std::move(pixels);
    manual_page_numbers_[slot] = page;
    manual_page_retries_.resize(manual_slots, 0);
    manual_page_retries_[slot] = 0;
    createBitmap(manual_pages_[slot]);
}

void Renderer::clearManualPages() {
    manual_pages_.assign(manual_slots, {});
    manual_page_numbers_.assign(manual_slots, -1);
}

Renderer::Cover* Renderer::manualPage(int page) {
    for (size_t i = 0; i < manual_page_numbers_.size() && i < manual_pages_.size(); ++i) {
        if (manual_page_numbers_[i] != page) continue;
        auto& slot = manual_pages_[i];
        if (slot.bitmap) return &slot;
        // Its picture is here but no bitmap and none on its way (a decode that failed, a target that
        // was not ready): decoded again, a few times at most.
        manual_page_retries_.resize(manual_slots, 0);
        if (slot.loaded() && !slot.decoding && manual_page_retries_[i] < 3) { ++manual_page_retries_[i]; createBitmap(slot); }
        return slot.bitmap ? &slot : nullptr;
    }
    return nullptr;
}

bool Renderer::hasManualPage(int page) const {
    for (size_t i = 0; i < manual_page_numbers_.size() && i < manual_pages_.size(); ++i)
        if (manual_page_numbers_[i] == page && manual_pages_[i].loaded()) return true;
    return false;
}

ID3D11Device* Renderer::device3d() const {
#ifdef _WIN32
    if (swap_) return swap_.device3d();
    return capture_device_.Get();
#else
    return nullptr;
#endif
}

// The three printed faces of the focused game's case, drawn again when what they show changes.
bool Renderer::caseFaces(const GameCard& card, const game_case::Kind& kind, float aspect, float depth_of_height) {
    const auto& game = card.game;
    Cover* cover = selected_ < covers_.size() && covers_[selected_].bitmap ? &covers_[selected_] : nullptr;
    const bool box_scan = package_look::scanFirst(kind) && cover; // PS1: the square box scan first.
    Cover* poster = !box_scan && selected_ < posters_.size() && posters_[selected_].bitmap ? &posters_[selected_] : nullptr;
    Cover* art = poster ? poster : cover;
    Cover* logo = selected_ < logos_.size() && logos_[selected_].bitmap ? &logos_[selected_] : nullptr;
    Cover* back_art = case_back_key_ == context_->game_case->key && case_back_.bitmap ? &case_back_ : nullptr;
    Cover* side_art = case_side_key_ == context_->game_case->key && case_side_.bitmap ? &case_side_ : nullptr;
    const bool frozen = sessionOf(game) == SessionState::frozen;
    const auto* info = context_->preview_waiting ? nullptr : context_->preview;
    const std::wstring description = info ? info->description : std::wstring{};
    const auto platform = activePlatformName(game);
    auto& faces = case_faces_;
    const bool same = faces.front && faces.key == context_->game_case->key && faces.art == (art ? art->bitmap.Get() : nullptr) &&
                      faces.back_art == (back_art ? back_art->bitmap.Get() : nullptr) && faces.side_art == (side_art ? side_art->bitmap.Get() : nullptr) &&
                      faces.logo == (logo ? logo->bitmap.Get() : nullptr) &&
                      faces.installed == game.installed && faces.frozen == frozen && faces.description == description &&
                      faces.aspect == aspect && faces.depth == depth_of_height && faces.target == target_.Get();
    if (same) return true;
    faces.key = context_->game_case->key;
    faces.art = art ? art->bitmap.Get() : nullptr;
    faces.back_art = back_art ? back_art->bitmap.Get() : nullptr;
    faces.side_art = side_art ? side_art->bitmap.Get() : nullptr;
    faces.logo = logo ? logo->bitmap.Get() : nullptr;
    faces.installed = game.installed; faces.frozen = frozen; faces.description = description;
    faces.aspect = aspect; faces.depth = depth_of_height; faces.target = target_.Get();
    faces.front.Reset(); faces.back.Reset(); faces.spine.Reset();
    faces.front1.Reset(); faces.back1.Reset(); faces.spine1.Reset();
    ++faces.version;
    const auto look = package_look::of(kind);
    constexpr float tall = 1024;
    const float wide = std::round(tall * aspect);
    // Each face on a surface of its own; the renderer's helpers draw on target_, so it is the surface meanwhile.
    const auto paint = [&](float w, float h, const auto& draw) -> ComPtr<ID2D1Bitmap> {
        ComPtr<ID2D1BitmapRenderTarget> surface;
        const auto pixels = D2D1::SizeU(static_cast<UINT32>(w), static_cast<UINT32>(h));
        if (FAILED(target_->CreateCompatibleRenderTarget(D2D1::SizeF(w, h), pixels, surface.GetAddressOf()))) return {};
        ComPtr<ID2D1RenderTarget> main = target_;
        target_ = surface;
        surface->BeginDraw();
        surface->SetTransform(D2D1::Matrix3x2F::Identity());
        draw(w, h);
        const HRESULT done = surface->EndDraw();
        target_ = main;
        ComPtr<ID2D1Bitmap> bitmap;
        if (FAILED(done) || FAILED(surface->GetBitmap(bitmap.GetAddressOf()))) return {};
        return bitmap;
    };
    const auto artBitmap = [&](Cover& image) { return frozen && grayBrush(image) ? image.gray_bitmap.Get() : image.bitmap.Get(); };
    // The game's art over a part of a face, as the shelf prints it (a square cover centred over its own blur).
    const auto print = [&](D2D1_RECT_F r, float face_aspect, float opacity) {
        if (!art) return;
        auto* bitmap = artBitmap(*art);
        if (art == poster) {
            const auto source = perspective::cropTo(bitmap->GetSize(), face_aspect);
            target_->DrawBitmap(bitmap, r, opacity, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, &source);
            return;
        }
        if (auto* blur = blurredArt(*art)) {
            const auto source = perspective::cropTo(blur->GetSize(), face_aspect);
            target_->DrawBitmap(blur, r, opacity * 0.85f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, &source);
        }
        const float w = r.right - r.left, h = r.bottom - r.top;
        const auto inner = face_aspect < 1 ? D2D1::RectF(r.left, r.top + (h - w) * 0.5f, r.right, r.top + (h + w) * 0.5f)
                                           : D2D1::RectF(r.left + (w - h) * 0.5f, r.top, r.left + (w + h) * 0.5f, r.bottom);
        const auto source = perspective::cropTo(bitmap->GetSize(), 1);
        target_->DrawBitmap(bitmap, inner, opacity, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, &source);
    };

    // The front: the shelf's own package front, flat, with no lamp of its own (the 3D light is the lamp).
    faces.front = paint(wide, tall, [&](float w, float h) {
        target_->Clear(D2D1::ColorF(look.rim.r, look.rim.g, look.rim.b, 1));
        PackageFront f;
        f.kind = kind; f.platform = platform; f.aspect = aspect; f.alpha = 1; f.scale = 1;
        f.gloss_strength = 0; f.shade = 0; f.dim = !game.installed; f.scan = box_scan;
        f.at = [w, h](float s, float t) { return D2D1::Point2F(s * w, t * h); };
        f.picture = [&, w, h](float l, float t, float r, float b, ID2D1Bitmap* bitmap, D2D1_RECT_F source, float opacity) {
            if (bitmap) target_->DrawBitmap(bitmap, D2D1::RectF(l * w, t * h, r * w, b * h), opacity, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, &source);
        };
        f.print = [&, w, h](float l, float t, float r, float b, float art_aspect, float opacity) {
            print(D2D1::RectF(l * w, t * h, r * w, b * h), art_aspect, opacity);
        };
        drawPackageFront(f);
        if (!art) {
            // No art yet: the name, large, on the insert.
            target_->SetTransform(D2D1::Matrix3x2F::Scale(h / 600, h / 600));
            text(game.name, 600 * aspect * 0.1f, 600 * 0.38f, 600 * aspect * 0.8f, 600 * 0.3f, settings_heading_font_.Get(), gray(0.92f));
            target_->SetTransform(D2D1::Matrix3x2F::Identity());
        }
    });

    // The back: the box's own back when one was found, else one made up from the game: its art's blur,
    // darkened, its logo (or name), what it is about and its platform.
    faces.back = paint(wide, tall, [&](float w, float h) {
        target_->Clear(D2D1::ColorF(0.05f, 0.055f, 0.07f, 1));
        if (back_art) {
            auto* bitmap = artBitmap(*back_art);
            const auto source = perspective::cropTo(bitmap->GetSize(), aspect);
            target_->DrawBitmap(bitmap, D2D1::RectF(0, 0, w, h), 1, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, &source);
            return;
        }
        if (art)
            if (auto* blur = blurredArt(*art)) {
                const auto source = perspective::cropTo(blur->GetSize(), aspect);
                target_->DrawBitmap(blur, D2D1::RectF(0, 0, w, h), 1, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, &source);
            }
        brush(gray(0, 0.62f)); target_->FillRectangle(D2D1::RectF(0, 0, w, h), solid_.Get());
        // In a page 600 units tall.
        const float k = h / 600, pw = w / k, margin = pw * 0.09f;
        target_->SetTransform(D2D1::Matrix3x2F::Scale(k, k));
        float y = 600 * 0.07f;
        bool logo_drawn = false;
        if (logo) {
            const auto pixels = logo->bitmap->GetPixelSize();
            const auto source = logo->has_ink_bounds ? logo->ink_bounds : D2D1::RectF(0, 0, static_cast<float>(pixels.width), static_cast<float>(pixels.height));
            const float lw = source.right - source.left, lh = source.bottom - source.top;
            if (lw > 0 && lh > 0) {
                const float f = std::min((pw - 2 * margin) / lw, 72 / lh);
                const float x = (pw - lw * f) * 0.5f;
                target_->DrawBitmap(logo->bitmap.Get(), D2D1::RectF(x, y, x + lw * f, y + lh * f), 1, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, &source);
                y += lh * f + 26;
                logo_drawn = true;
            }
        }
        if (!logo_drawn) y += paragraph(game.name, margin, y, pw - 2 * margin, settings_heading_font_.Get(), gray(0.96f), 2) + 22;
        if (art) {
            // A picture of the game, framed, like the screenshot on a real back.
            auto* bitmap = artBitmap(*art);
            const float iw = pw - 2 * margin, ih = std::min(iw * 0.56f, 600 * 0.30f);
            const auto source = perspective::cropTo(bitmap->GetSize(), iw / ih);
            const auto frame = D2D1::RectF(margin, y, margin + iw, y + ih);
            target_->DrawBitmap(bitmap, frame, 1, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, &source);
            brush(gray(1, 0.35f)); target_->DrawRectangle(frame, solid_.Get(), 1.2f);
            y += ih + 20;
        }
        if (!description.empty()) {
            const int lines = std::max(2, static_cast<int>((600 - 86 - y) / 22));
            paragraph(description, margin, y, pw - 2 * margin, details_body_font_.Get(), gray(0.86f), lines);
        }
        // The foot: the platform's mark on the left, the developer on the right.
        const float foot = 600 - 40;
        brush(gray(1, 0.18f)); target_->FillRectangle(D2D1::RectF(margin, foot - 24, pw - margin, foot - 23), solid_.Get());
        if (!platform.empty()) {
            if (hasPlatformMark(platform)) drawPlatformMark(platform, margin, foot, 20, pw * 0.4f, 0.92f, settings_body_font_.Get());
            else textLine(platform, margin, foot, pw * 0.4f, settings_body_font_.Get(), gray(0.86f));
        }
        if (info && !(info->developer.empty() && info->publisher.empty()))
            textLine(info->developer.empty() ? info->publisher : info->developer, pw * 0.5f, foot, pw * 0.5f - margin, settings_body_font_.Get(), gray(0.70f),
                     DWRITE_TEXT_ALIGNMENT_TRAILING);
        target_->SetTransform(D2D1::Matrix3x2F::Identity());
    });

    // The spine: its own scan when there is one (a PS1 case's insert: the black strip with PlayStation
    // along it), 1:1 and full height, the rest of the edge the case's plastic (a jewel case's hinge);
    // else the case's colour, the game's name along it, a console's colour band at its head.
    const float thick = std::max(40.0f, std::round(tall * depth_of_height));
    faces.spine = paint(thick, tall, [&](float w, float h) {
        target_->Clear(D2D1::ColorF(look.spine.r, look.spine.g, look.spine.b, 1));
        if (side_art) {
            auto* bitmap = artBitmap(*side_art);
            const auto size = bitmap->GetSize();
            const bool lying = size.width > size.height; // Scanned lying down: turned upright first.
            const float long_side = lying ? size.width : size.height, short_side = lying ? size.height : size.width;
            if (long_side < 1 || short_side < 1) return;
            // Its true proportions: as tall as the case, as wide as that makes it (smaller if the edge is narrower).
            const float k = std::min(h / long_side, w / short_side);
            const float sw = short_side * k, sh = long_side * k;
            // The face runs from the case's front (x = 0, on screen on the right) to its back: the insert
            // lies in the tray, at the back; read from outside, so mirrored (and a lying scan turned a quarter).
            const float cx = w - sw * 0.5f, cy = h * 0.5f;
            target_->SetTransform(lying ? D2D1::Matrix3x2F::Rotation(90, {cx, cy}) * D2D1::Matrix3x2F::Scale(-1, 1, {cx, cy}) : D2D1::Matrix3x2F::Scale(-1, 1, {cx, cy}));
            const auto r = lying ? D2D1::RectF(cx - sh * 0.5f, cy - sw * 0.5f, cx + sh * 0.5f, cy + sw * 0.5f) : D2D1::RectF(cx - sw * 0.5f, cy - sh * 0.5f, cx + sw * 0.5f, cy + sh * 0.5f);
            target_->DrawBitmap(bitmap, r, 1, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
            target_->SetTransform(D2D1::Matrix3x2F::Identity());
            return;
        }
        const auto head = look.spine_head_always ? std::optional<D2D1_COLOR_F>(look.spine_head) :
                          !game.platform.empty() && game.platform != L"PC" ? std::optional<D2D1_COLOR_F>(render_detail::platformColor(game.platform)) :
                                                                              std::nullopt;
        if (auto* label = shelfSpine(game.name, platform, head, look.spine_ink, look.head_ink)) {
            const auto size = label->GetSize();
            target_->DrawBitmap(label, D2D1::RectF(0, 0, w, h), 1, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, D2D1::RectF(0, 0, size.width, size.height));
        }
    });
    if (!faces.front || !faces.back || !faces.spine) { faces.front.Reset(); return false; }

#ifdef _WIN32
    // Direct3D samples bitmaps of the device context with a surface of their own.
    ComPtr<ID2D1DeviceContext> context;
    if (device3d() && SUCCEEDED(target_.As(&context))) {
        const auto copy = [&](ID2D1Bitmap* from, ComPtr<ID2D1Bitmap1>& to) {
            const auto size = from->GetPixelSize();
            const auto properties = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_NONE, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96, 96);
            if (FAILED(context->CreateBitmap(size, nullptr, 0, &properties, to.GetAddressOf())) || FAILED(to->CopyFromBitmap(nullptr, from, nullptr))) to.Reset();
        };
        copy(faces.front.Get(), faces.front1); copy(faces.back.Get(), faces.back1); copy(faces.spine.Get(), faces.spine1);
    }
#endif
    return true;
}

// The held case at `front` (its front at rest there), turned by the state's yaw and pitch.
void Renderer::drawHeldCaseBox(const game_case_ui::State& state, const game_case::Kind& kind, D2D1_RECT_F front, float alpha) {
    if (!cards_ || selected_ >= cards_->size()) return;
    const auto& card = (*cards_)[selected_];
    const float S = viewport_.scale;
    const float fw = front.right - front.left, fh = front.bottom - front.top;
    if (fw < 4 || fh < 4) return;
    const float aspect = game_case::aspect(kind);
    const float depth_of_height = thickness(kind);
    if (!caseFaces(card, kind, aspect, depth_of_height)) return;
    const auto look = package_look::of(kind);
    const auto& faces = case_faces_;
    const float cx = (front.left + front.right) * 0.5f, cy = (front.top + front.bottom) * 0.5f;

    // A soft shadow on the floor under it.
    for (int layer = 5; layer >= 1; --layer) {
        const float spread = layer * fw * 0.05f;
        brush(gray(0, 0.05f * alpha));
        target_->FillEllipse(D2D1::Ellipse({cx, front.bottom + fh * 0.03f}, fw * 0.55f + spread, fh * 0.02f + spread * 0.16f), solid_.Get());
    }

#ifdef _WIN32
    ComPtr<ID2D1DeviceContext> context;
    if (auto* device = device3d(); device && faces.front1 && SUCCEEDED(target_.As(&context))) {
        // Drawn at the held size's pixels (sharp at rest), scaled on its flight.
        const game_case::Layout layout{viewport_.width, viewport_.height, kind};
        const float rest_h = layout.heldHeight() * S, rest_w = rest_h * aspect;
        const float depth = rest_h * depth_of_height;
        const float frame_w = std::ceil(std::sqrt(rest_w * rest_w + depth * depth) * 1.30f), frame_h = std::ceil(std::sqrt(rest_h * rest_h + depth * depth) * 1.22f);
        Box3D::Pose pose;
        pose.width = rest_w; pose.height = rest_h; pose.depth = depth;
        // The front's own corners (its print is cut to them); the edges only just eased, as moulded plastic is.
        pose.corner = rest_w * std::max(0.006f, look.corner);
        pose.bevel = std::min(depth * 0.10f, pose.corner * 0.45f);
        if (kind.package == game_case::Package::big_box) { pose.corner = rest_w * 0.006f; pose.bevel = depth * 0.02f; }
        pose.yaw = state.yaw; pose.pitch = state.pitch;
        pose.eye = rest_h * 3.4f;
        pose.gloss = std::clamp(look.sleeve_gloss, 0.15f, 1.0f);
        pose.plastic[0] = look.rim.r; pose.plastic[1] = look.rim.g; pose.plastic[2] = look.rim.b;
        pose.light = 0.12f * std::sin(elapsed_ * 0.35f);
        const Box3D::Faces printed{faces.front1.Get(), faces.back1.Get(), faces.spine1.Get()};
        if (auto* bitmap = box3d_.render(device, context.Get(), printed, pose, static_cast<uint32_t>(frame_w), static_cast<uint32_t>(frame_h), faces.version)) {
            const float k = fh * S / rest_h; // Its flight's size against the held one.
            const float dw = frame_w * k / S, dh = frame_h * k / S;
            target_->DrawBitmap(bitmap, D2D1::RectF(cx - dw * 0.5f, cy - dh * 0.5f, cx + dw * 0.5f, cy + dh * 0.5f), alpha, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
            return;
        }
    }
#endif
    // Flat faces in perspective (device pixels round the case's middle): the ones turned to the viewer.
    D2D1_MATRIX_3X2_F base; target_->GetTransform(&base);
    ComPtr<ID2D1DeviceContext> context2d;
    target_.As(&context2d);
    const float w = fw * S, h = fh * S, d = h * depth_of_height;
    const perspective::Camera camera{cx * S, cy * S, h * 3.4f};
    const float cyaw = std::cos(state.yaw), syaw = std::sin(state.yaw), cp = std::cos(state.pitch), sp = std::sin(state.pitch);
    const auto turn = [&](perspective::Vec3 p) {
        const perspective::Vec3 r{p.x * cyaw - p.z * syaw, p.y, p.x * syaw + p.z * cyaw};
        return perspective::Vec3{r.x, r.y * cp + r.z * sp, -r.y * sp + r.z * cp};
    };
    // inward: v x u points into the case (the spine's print runs from the front's edge, as on the shelf).
    struct Side { perspective::Vec3 origin, u, v; ID2D1Bitmap* bitmap; float lit; bool inward; };
    const float hw = w * 0.5f, hh = h * 0.5f, hd = d * 0.5f;
    const Side sides[] = {
        {{-hw, -hh, -hd}, {w, 0, 0}, {0, h, 0}, faces.front.Get(), 1.0f, false},
        {{hw, -hh, hd}, {-w, 0, 0}, {0, h, 0}, faces.back.Get(), 0.92f, false},
        {{-hw, -hh, -hd}, {0, 0, d}, {0, h, 0}, faces.spine.Get(), 0.78f, true},
        {{hw, -hh, -hd}, {0, 0, d}, {0, h, 0}, nullptr, 0.70f, false},
    };
    target_->SetTransform(D2D1::Matrix3x2F::Identity());
    for (const auto& side : sides) {
        const auto o = turn(side.origin), u = turn(side.origin + side.u) - o, v = turn(side.origin + side.v) - o;
        // Facing the camera at (0, 0, -eye): its normal (v x u, outward) towards it.
        const perspective::Vec3 n = perspective::cross(v, u) * (side.inward ? -1.0f : 1.0f);
        const perspective::Vec3 centre = o + u * 0.5f + v * 0.5f;
        if (perspective::dot(n, perspective::Vec3{0, 0, -camera.eye} - centre) <= 0) continue;
        const perspective::Face face{o, u, v};
        D2D1_POINT_2F q[4]; face.corners(camera, q);
        if (side.bitmap && context2d) {
            const auto size = side.bitmap->GetSize();
            const auto matrix = perspective::matrix(face, size.width, size.height, camera);
            context2d->DrawBitmap(side.bitmap, D2D1::RectF(0, 0, size.width, size.height), alpha, D2D1_INTERPOLATION_MODE_ANISOTROPIC, nullptr, &matrix);
        } else {
            ComPtr<ID2D1PathGeometry> path;
            ComPtr<ID2D1GeometrySink> sink;
            if (SUCCEEDED(factory_->CreatePathGeometry(path.GetAddressOf())) && SUCCEEDED(path->Open(sink.GetAddressOf()))) {
                sink->BeginFigure(q[0], D2D1_FIGURE_BEGIN_FILLED); sink->AddLines(q + 1, 3); sink->EndFigure(D2D1_FIGURE_END_CLOSED);
                if (SUCCEEDED(sink->Close())) { brush(faded(look.edge, alpha)); target_->FillGeometry(path.Get(), solid_.Get()); }
            }
        }
        // Turned from the lamp: darker.
        if (side.lit < 1 || std::abs(syaw) > 0.05f) {
            ComPtr<ID2D1PathGeometry> path;
            ComPtr<ID2D1GeometrySink> sink;
            if (SUCCEEDED(factory_->CreatePathGeometry(path.GetAddressOf())) && SUCCEEDED(path->Open(sink.GetAddressOf()))) {
                sink->BeginFigure(q[0], D2D1_FIGURE_BEGIN_FILLED); sink->AddLines(q + 1, 3); sink->EndFigure(D2D1_FIGURE_END_CLOSED);
                const float facing = std::abs(perspective::dot(n, perspective::Vec3{0, 0, -1})) / std::max(1.0f, std::sqrt(perspective::dot(n, n)));
                if (SUCCEEDED(sink->Close())) { brush(gray(0, (0.30f * (1 - facing) + (1 - side.lit) * 0.5f) * alpha)); target_->FillGeometry(path.Get(), solid_.Get()); }
            }
        }
    }
    target_->SetTransform(base);
}

void Renderer::drawHeldCase() {
    const auto* state = context_->game_case;
    if (!state || !cards_ || selected_ >= cards_->size() || state->held <= 0) return;
    const auto kind = context_->game_case_kind;
    const game_case::Layout layout{viewport_.width, viewport_.height, kind, game_case::buttonCount(state->manual)};
    const auto phase = game_case::phases(state->held);
    const float W = viewport_.width, H = viewport_.height;
    if (!createShelfResources()) return;
    // The library dims away under a soft light where the case is held.
    brush(D2D1::ColorF(0.006f, 0.008f, 0.011f, 0.90f * phase.travel)); target_->FillRectangle(D2D1::RectF(0, 0, W, H), solid_.Get());
    const auto held = layout.held();
    if (shelf_glow_) {
        const auto spot = D2D1::Point2F((held.left + held.right) * 0.5f, (held.top + held.bottom) * 0.5f);
        const float reach = layout.heldHeight() * 0.95f;
        shelf_glow_->SetCenter(spot); shelf_glow_->SetRadiusX(reach * 1.25f); shelf_glow_->SetRadiusY(reach);
        shelf_glow_->SetOpacity(phase.travel);
        target_->FillRectangle(D2D1::RectF(spot.x - reach * 1.25f, spot.y - reach, spot.x + reach * 1.25f, spot.y + reach), shelf_glow_.Get());
    }
    const settings_ui::Rect from{state->from_left, state->from_top, state->from_right, state->from_bottom};
    const auto front = layout.heldAt(from, phase.travel);
    drawHeldCaseBox(*state, kind, D2D1::RectF(front.left, front.top, front.right, front.bottom), (*cards_)[selected_].opacity);
    drawGameCasePanel(layout, state->held);
}

// The right half, row after row: each fades in as it rises the last few pixels to its place.
void Renderer::drawGameCasePanel(const game_case::Layout& layout, float time) {
    const auto* state = context_->game_case;
    const auto& game = (*cards_)[selected_].game;
    const auto row = [&](int index) { return game_case::panelRow(time, index); };
    if (row(0) < 0.003f) return;
    const auto riseOf = [](float shown) { return 22 * (1 - shown); };
    const float left = layout.panelLeft(), right = layout.panelRight(), width = right - left, H = viewport_.height;
    const auto platform = activePlatformName(game);
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
    for (int i = 0; i < layout.buttons; ++i) {
        const auto r = layout.button(i);
        const auto box = D2D1::RectF(r.left, r.top + rise, r.right, r.bottom + rise);
        const auto which = game_case::buttonAt(i, state->manual);
        const bool enabled = which != game_case::Button::action || context_->game_case_action_enabled;
        const bool focused = state->focus == i && state->open && !(context_->manual && context_->manual->open);
        const auto ink = drawButton(box, focused ? 1.0f : 0.0f, panel, true, state->hover == i, enabled);
        if (which == game_case::Button::action && drawInstallProgress(game, box, ink, gray(ink.r, 0.25f * ink.a), menu_font_.Get())) continue;
        const std::wstring label = which == game_case::Button::action ? context_->game_case_action :
                                   which == game_case::Button::manual ? std::wstring(tr(L"Instrukcja")) : std::wstring(tr(L"Szczegóły"));
        textLine(label, box.left, (box.top + box.bottom) * 0.5f, box.right - box.left, menu_font_.Get(), ink, DWRITE_TEXT_ALIGNMENT_CENTER);
    }
    // A short description (what Szczegóły shows; its genres while none is known).
    const auto description = layout.description();
    panel = row(3); rise = riseOf(panel);
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
    // The hints: the system's own, in the bottom right corner (input_hints.cpp).
}
