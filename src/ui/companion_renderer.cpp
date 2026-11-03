// Towarzyszka: her home tile (a medium close-up whose head rises above the tile, with a speech
// bubble; on Flix a pill, on Metro and XMB inside the layout's own tile), her chat screen and the
// look previews in Settings. Her pictures are the embedded sprites of res/companion/, rigged by
// avatar::Rig; geometry comes from companion_view.hpp and home_widgets.hpp.
// Here: her text with game mentions, sprites and figure, the speech bubble, her face, the Flix pill
// and the home tile. The chat screen: companion_chat_renderer.cpp; cards, the picker and the
// Settings previews: companion_cards_renderer.cpp.
#include "renderer.hpp"
#include "renderer_internal.hpp"
#include "../platform/embedded_resources.hpp"

#include <algorithm>
#include <cmath>

using Microsoft::WRL::ComPtr;

namespace {
using palette::gray;
using render_detail::box;
using render_detail::smoothFade;
}

Renderer::CompanionText Renderer::companionText(const std::wstring& value, const std::vector<companion::GameMention>& mentions,
                                               float width, int max_lines) {
    CompanionText result;
    auto* font = settings_body_font_.Get();
    if (value.empty() || width <= 0 || !font) return result;
    const float max_height = typeMetrics(font).line * max_lines;
    if (FAILED(dwrite_->CreateTextLayout(value.c_str(), static_cast<UINT32>(value.size()), font, width, max_height,
                                         result.layout.GetAddressOf()))) return result;
    result.layout->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
    result.layout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    for (const auto& mention : mentions) {
        if (!mention.length || mention.at >= value.size()) continue;
        result.layout->SetFontWeight(DWRITE_FONT_WEIGHT_BOLD, {static_cast<UINT32>(mention.at),
            static_cast<UINT32>(std::min(mention.length, value.size() - mention.at))});
    }
    DWRITE_TEXT_METRICS metrics{};
    if (SUCCEEDED(result.layout->GetMetrics(&metrics))) {
        // Measure with the bold runs already applied: wrapping, bubbles and hit boxes agree.
        result.width = metrics.lineCount > 1 ? width : std::min(width, std::max(24.0f, metrics.widthIncludingTrailingWhitespace + 2));
        result.height = std::min(max_height, metrics.height);
        result.layout->SetMaxWidth(result.width);
    }
    return result;
}

void Renderer::drawCompanionText(const CompanionText& text, const std::wstring& value, const std::vector<companion::GameMention>& mentions,
                                  float x, float y, D2D1_COLOR_F ink, D2D1_RECT_F clip, companion::Look speaker) {
    if (!text.layout) return;
    ComPtr<ID2D1SolidColorBrush> link_ink, hover_ink;
    const auto tint = companion_ui::tint(speaker);
    target_->CreateSolidColorBrush(D2D1::ColorF(tint.r, tint.g, tint.b, ink.a), link_ink.GetAddressOf());
    target_->CreateSolidColorBrush(D2D1::ColorF(0.3f + 0.7f * tint.r, 0.3f + 0.7f * tint.g, 0.3f + 0.7f * tint.b, ink.a), hover_ink.GetAddressOf());
    for (const auto& mention : mentions) {
        if (!mention.length || mention.at >= value.size()) continue;
        const DWRITE_TEXT_RANGE range{static_cast<UINT32>(mention.at), static_cast<UINT32>(std::min(mention.length, value.size() - mention.at))};
        if (link_ink) text.layout->SetDrawingEffect(link_ink.Get(), range);
        // The typewriter may show only the start of a title: colour it, but wait before linking it.
        if (mention.length > value.size() - mention.at) continue;
        UINT32 count = 0;
        text.layout->HitTestTextRange(range.startPosition, range.length, x, y, nullptr, 0, &count);
        if (!count) continue;
        std::vector<DWRITE_HIT_TEST_METRICS> hits(count);
        if (FAILED(text.layout->HitTestTextRange(range.startPosition, range.length, x, y, hits.data(), count, &count))) continue;
        bool hovered = false;
        for (const auto& hit : hits) {
            if (!hit.isText || hit.isTrimmed) continue;
            const settings_ui::Rect rect{std::max({hit.left, clip.left, x}), std::max({hit.top, clip.top, y}),
                std::min({hit.left + hit.width, clip.right, x + text.width}), std::min({hit.top + hit.height, clip.bottom, y + text.height})};
            if (rect.right > rect.left && rect.bottom > rect.top) {
                companion_links_.push_back({rect, value.substr(mention.at, mention.length)});
                if (context_->companion && context_->companion->link_pointer) {
                    const auto [px, py] = *context_->companion->link_pointer;
                    hovered = hovered || rect.contains(px, py);
                }
            }
        }
        if (hovered) {
            text.layout->SetUnderline(TRUE, range);
            if (hover_ink) text.layout->SetDrawingEffect(hover_ink.Get(), range);
        }
    }
    brush(ink);
    target_->DrawTextLayout(D2D1::Point2F(x, y), text.layout.Get(), solid_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

Renderer::Cover* Renderer::companionSprite(companion::Look look, companion_ui::Frame frame, companion::Emotion emotion, int costume) {
    return companionResource(companion_ui::spriteId(look, frame, emotion, std::max(costume, 0)));
}

Renderer::Cover* Renderer::companionResource(int id) {
    auto& sprite = companion_sprites_[id];
    if (sprite.missing) return nullptr;
    auto& cover = sprite.cover;
    if (!cover.loaded()) {
        // Embedded PNG (companion.rc on Windows, .incbin on Linux; generated by CMake from res/companion/): copied once.
        const auto bytes = embedded_resources::find(id);
        if (!bytes || bytes->empty()) { sprite.missing = true; return nullptr; }
        cover.pixels = shareImage(std::vector<uint8_t>(bytes->begin(), bytes->end()));
    }
    createBitmap(cover); // Decoded off the UI thread; uploaded when ready.
    return cover.bitmap ? &cover : nullptr;
}

void Renderer::drawCompanionFigure(companion::Look look, const avatar::Pose& pose, float x, float bottom, float height, float alpha, int costume) {
    if (alpha <= 0.003f) return;
    using companion_ui::Frame;
    // The frame of a feeling: closed eyes while blinking, an open mouth on syllables, else the
    // picture. A costume without that feeling shows its calm face (the outfit stays); one without
    // that either, her basic outfit's picture of it, else its calm face.
    const auto plain = [&](companion::Emotion emotion) {
        const auto attempts = costume ? std::array<std::pair<companion::Emotion, int>, 4>{{{emotion, costume}, {companion::calm, costume}, {emotion, 0}, {companion::calm, 0}}}
                                      : std::array<std::pair<companion::Emotion, int>, 4>{{{emotion, 0}, {companion::calm, 0}, {companion::calm, 0}, {companion::calm, 0}}};
        for (const auto& [feeling, slot] : attempts) {
            Cover* sprite = companionSprite(look, Frame::base, feeling, slot);
            if (sprite || !companion_sprites_[companion_ui::spriteId(look, Frame::base, feeling, slot)].missing) return sprite; // Still decoding: wait for it.
        }
        return static_cast<Cover*>(nullptr);
    };
    const auto pick = [&](companion::Emotion emotion) {
        Cover* sprite = nullptr;
        if (pose.blink) sprite = companionSprite(look, Frame::blink, emotion, costume);
        if (!sprite && pose.mouth) sprite = companionSprite(look, Frame::talk, emotion, costume);
        return sprite ? sprite : plain(emotion);
    };
    Cover* current = pick(pose.emotion);
    Cover* previous = pose.blend < 1 && pose.previous != pose.emotion ? plain(pose.previous) : nullptr;
    if (!current) { current = previous; previous = nullptr; } // A new picture still decoding: keep the last one.
    if (!current) return;
    const float w = height * companion_ui::sprite_aspect, unit = height / 300;
    D2D1_MATRIX_3X2_F base{};
    target_->GetTransform(&base);
    const auto screen = D2D1::Matrix3x2F::ReinterpretBaseType(&base)[0];
    // Her face on this picture (faces.txt); unknown: where the square bust usually has it.
    const auto known = companion_ui::faceOf(look, costume, pose.emotion);
    const float per_pixel = height / companion_ui::sprite_height;
    const auto face = known.size > 0 ? D2D1::Point2F(x - w * 0.5f + known.x * per_pixel, bottom - height + known.y * per_pixel)
                                     : D2D1::Point2F(x, bottom - height * 0.64f);
    const float face_size = known.size > 0 ? known.size * per_pixel : height * 0.34f;
    // Her pictures are close-ups, so moving the picture moves her head: it breathes and squashes on
    // its bottom edge, tilts and nods about the neck (just below her chin), glances aside.
    const auto anchor = D2D1::Point2F(x, bottom);
    const auto neck = D2D1::Point2F(x, std::min(bottom, face.y + face_size * 0.6f));
    const auto transform = D2D1::Matrix3x2F::Scale(pose.scale_x, pose.scale_y, anchor) *
                           D2D1::Matrix3x2F::Scale(1, 1 - pose.head_nod * 0.0035f, neck) *
                           D2D1::Matrix3x2F::Rotation(pose.tilt + pose.head_tilt, neck) *
                           D2D1::Matrix3x2F::Translation((pose.x + pose.head_x) * unit, (pose.y + pose.head_nod * 0.6f) * unit);
    ComPtr<ID2D1DeviceContext> context;
    target_.As(&context);
    const auto rect = D2D1::RectF(x - w * 0.5f, bottom - height, x + w * 0.5f, bottom);
    target_->SetTransform(transform * screen);
    const auto blit = [&](Cover& cover, float opacity) {
        // High-quality cubic where available: line art keeps its edges when scaled down.
        if (context) context->DrawBitmap(cover.bitmap.Get(), rect, opacity, D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC, nullptr, nullptr);
        else target_->DrawBitmap(cover.bitmap.Get(), rect, opacity, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    };
    // A quick cross-fade from the previous feeling.
    const float shown = previous ? std::clamp(pose.blend, 0.0f, 1.0f) : 1.0f;
    if (previous) blit(*previous, alpha * (1 - shown) / std::max(0.05f, 1 - alpha * shown));
    blit(*current, alpha * shown);
    drawCompanionMarks(pose, face, face_size, alpha); // Moving with her head.
    target_->SetTransform(base);
}

bool Renderer::drawCompanionPose(companion::Look look, int costume, settings_ui::Rect area, float alpha, float slide) {
    const int id = companion_ui::poseId(look, std::max(costume, 0));
    Cover* picture = companionResource(id);
    if (!picture) return !companion_sprites_[id].missing; // Decoding: her close-up waits for it.
    if (alpha <= 0.003f) return true;
    const auto size = picture->bitmap->GetSize();
    if (size.width <= 0 || size.height <= 0) return true;
    // As large as the column allows, standing on its bottom edge and centred across it; sitting or
    // lying poses are wider than tall and rest on the floor.
    const float room_w = area.right - area.left, room_h = area.bottom - area.top;
    const float scale = std::min(room_w / size.width, room_h / size.height);
    const float w = size.width * scale, h = size.height * scale;
    const float x = (area.left + area.right - w) * 0.5f - slide;
    const auto rect = D2D1::RectF(x, area.bottom - h, x + w, area.bottom);
    ComPtr<ID2D1DeviceContext> context;
    target_.As(&context);
    if (context) context->DrawBitmap(picture->bitmap.Get(), rect, alpha, D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC, nullptr, nullptr);
    else target_->DrawBitmap(picture->bitmap.Get(), rect, alpha, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    return true;
}

// Her speech bubble: near-black frosted glass with white text, a thin light rim and a tail curving
// towards (tip_x, tip_y); typing shows three dots.
D2D1_RECT_F Renderer::drawSpeechBubble(const std::wstring& value, bool typing, float anchor, float center_y, float max_width,
                                       float tip_x, float tip_y, float alpha, float min_top, bool left_of_anchor) {
    auto* font = settings_body_font_.Get();
    const float padding_x = 20, padding_y = 14;
    const auto& mentions = context_->companion->bubble_mentions;
    const auto text = companionText(value, mentions, max_width - 2 * padding_x, 8);
    const float text_width = typing ? 54 : std::max(text.width, 28.0f);
    const float text_height = typing ? typeMetrics(font).line : text.height;
    const float w = text_width + 2 * padding_x, h = text_height + 2 * padding_y;
    const float top = std::max(min_top, center_y - h * 0.5f);
    const auto rect = left_of_anchor ? D2D1::RectF(anchor - w, top, anchor, top + h) : D2D1::RectF(anchor, top, anchor + w, top + h);
    // The bubble and its tail as one outline, so glass, tint and rim follow both.
    ComPtr<ID2D1RoundedRectangleGeometry> body;
    ComPtr<ID2D1PathGeometry> tail, shape;
    ComPtr<ID2D1GeometrySink> sink;
    if (FAILED(factory_->CreateRoundedRectangleGeometry(D2D1::RoundedRect(rect, 20, 20), body.GetAddressOf())) ||
        FAILED(factory_->CreatePathGeometry(tail.GetAddressOf())) || FAILED(tail->Open(sink.GetAddressOf()))) return rect;
    const bool right_side = tip_x >= rect.right;
    const float edge = right_side ? rect.right - 12 : rect.left + 12;
    const float base_y = std::clamp(tip_y - 10, rect.top + 16, rect.bottom - 16);
    sink->BeginFigure(D2D1::Point2F(edge, base_y - 11), D2D1_FIGURE_BEGIN_FILLED);
    sink->AddBezier(D2D1::BezierSegment(D2D1::Point2F(edge + (tip_x - edge) * 0.45f, base_y - 10), D2D1::Point2F(tip_x - (tip_x - edge) * 0.15f, tip_y - 6),
                                        D2D1::Point2F(tip_x, tip_y)));
    sink->AddBezier(D2D1::BezierSegment(D2D1::Point2F(edge + (tip_x - edge) * 0.55f, base_y + 6), D2D1::Point2F(edge + (tip_x - edge) * 0.2f, base_y + 10),
                                        D2D1::Point2F(edge, base_y + 11)));
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    if (FAILED(sink->Close())) return rect;
    sink.Reset();
    if (FAILED(factory_->CreatePathGeometry(shape.GetAddressOf())) || FAILED(shape->Open(sink.GetAddressOf())) ||
        FAILED(body->CombineWithGeometry(tail.Get(), D2D1_COMBINE_MODE_UNION, nullptr, sink.Get())) || FAILED(sink->Close())) return rect;
    // A soft drop shadow, then frosted near-black glass.
    D2D1_MATRIX_3X2_F current{};
    target_->GetTransform(&current);
    const auto screen = D2D1::Matrix3x2F::ReinterpretBaseType(&current)[0];
    for (int i = 0; i < 3; ++i) {
        brush(gray(0, (0.16f - i * 0.04f) * alpha));
        target_->SetTransform(D2D1::Matrix3x2F::Translation(0, 3.0f + i * 2.5f) * screen);
        target_->FillGeometry(shape.Get(), solid_.Get());
    }
    target_->SetTransform(current);
    const Glass glass{shape, glass_level_ > 0.01f && frostCapture(shape.Get(), glassBlur(20))};
    glassFill(glass, D2D1::ColorF(0.02f, 0.022f, 0.03f, glass.frosted ? 0.86f : 0.94f), alpha);
    brush(gray(1, 0.16f * alpha)); target_->DrawGeometry(shape.Get(), solid_.Get(), 1.0f);
    if (typing) {
        for (int i = 0; i < 3; ++i) {
            const float lift = std::max(0.0f, std::sin(elapsed_ * 7 - i * 0.9f)) * 4;
            brush(gray(0.92f, alpha * (0.55f + 0.45f * lift / 4)));
            target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(rect.left + padding_x + 9 + i * 18.0f, (rect.top + rect.bottom) * 0.5f - lift), 4.2f, 4.2f), solid_.Get());
        }
    } else drawCompanionText(text, value, mentions, rect.left + padding_x, rect.top + padding_y, gray(0.97f, alpha),
                             D2D1::RectF(0, 0, viewport_.width, viewport_.height), context_->companion->look);
    return rect;
}

void Renderer::drawWidgetPill(D2D1_RECT_F rect, float alpha, float hover) {
    const float radius = (rect.bottom - rect.top) * 0.5f;
    const auto glass = glassPanel(D2D1::RoundedRect(rect, radius, radius), 16);
    if (!glass.shape) return;
    glassFill(glass, D2D1::ColorF(0.04f, 0.05f, 0.07f, glass_level_ > 0.01f ? 0.42f : 0.94f), alpha);
    brush(gray(1, (0.035f + hover * 0.075f) * alpha)); target_->FillGeometry(glass.shape.Get(), solid_.Get());
    brush(gray(1, (0.13f + hover * 0.42f) * alpha)); target_->DrawGeometry(glass.shape.Get(), solid_.Get(), 1.0f);
}

void Renderer::drawCompanionFace(companion::Look look, const avatar::Pose& pose, float cx, float cy, float radius, float alpha, int costume) {
    if (alpha <= 0.003f || radius <= 0) return;
    ComPtr<ID2D1EllipseGeometry> circle;
    if (FAILED(factory_->CreateEllipseGeometry(D2D1::Ellipse(D2D1::Point2F(cx, cy), radius, radius), circle.GetAddressOf()))) return;
    // The settings cards' glow (drawCompanionCardGlow) on a dark disc: her colour strongest behind her face.
    brush(gray(0, 0.32f * alpha));
    target_->FillGeometry(circle.Get(), solid_.Get());
    const auto tint = companion_ui::tint(look);
    const D2D1_GRADIENT_STOP stops[] = {
        {0, D2D1::ColorF(tint.r, tint.g, tint.b, 0.62f * alpha)},
        {0.5f, D2D1::ColorF(tint.r, tint.g, tint.b, 0.3f * alpha)},
        {1, D2D1::ColorF(tint.r, tint.g, tint.b, 0.06f * alpha)}};
    ComPtr<ID2D1GradientStopCollection> collection;
    ComPtr<ID2D1RadialGradientBrush> glow;
    if (SUCCEEDED(target_->CreateGradientStopCollection(stops, 3, collection.GetAddressOf())) &&
        SUCCEEDED(target_->CreateRadialGradientBrush(D2D1::RadialGradientBrushProperties(D2D1::Point2F(cx, cy - radius * 0.08f), {}, radius * 1.1f, radius * 1.1f),
                                                    collection.Get(), glow.GetAddressOf())))
        target_->FillGeometry(circle.Get(), glow.Get());
    // Her face in the middle: where it is on the picture shown (gliding with the cross-fade to the next feeling), at
    // the same size whatever the picture's framing. Without faces.txt: where the square bust usually has her face.
    auto face = companion_ui::faceOf(look, costume, pose.emotion);
    if (const auto before = companion_ui::faceOf(look, costume, pose.previous);
        pose.previous != pose.emotion && pose.blend < 1 && face.size > 0 && before.size > 0) {
        const float t = std::clamp(pose.blend, 0.0f, 1.0f), k = t * t * (3 - 2 * t);
        face = {std::lerp(before.x, face.x, k), std::lerp(before.y, face.y, k), std::lerp(before.size, face.size, k)};
    }
    float x = cx, height = radius * 4.0f, bottom = cy + height * 0.64f;
    if (face.size > 0) {
        const float scale = 2 * radius * companion_ui::face_fill / face.size; // DIPs per canvas pixel.
        height = companion_ui::sprite_height * scale;
        x = cx - (face.x - companion_ui::sprite_width * 0.5f) * scale;
        bottom = cy + (companion_ui::sprite_height - face.y) * scale;
    }
    target_->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), circle.Get()), nullptr);
    drawCompanionFigure(look, pose, x, bottom, height, alpha, costume);
    target_->PopLayer();
    brush(gray(1, 0.12f * alpha));
    target_->DrawGeometry(circle.Get(), solid_.Get(), 1);
}

// Flix: a slim pill — her face in a circle, her name and her key.
void Renderer::drawCompanionPill(D2D1_RECT_F rect, float alpha, float hover) {
    const auto& view = *context_->companion;
    drawWidgetPill(rect, alpha, hover);
    const float cy = (rect.top + rect.bottom) * 0.5f, face = (rect.bottom - rect.top) * 0.5f - 4;
    const float cx = rect.left + 4 + face;
    drawCompanionFace(view.look, view.pose, cx, cy, face, alpha, view.costume);
    const float key_x = rect.right - 4 - 15;
    brush(gray(0.98f, alpha)); target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(key_x, cy), 15, 15), solid_.Get());
    textLine(view.key, key_x - 15, cy, 30, toast_sub_font_.Get(), gray(0.08f, alpha), DWRITE_TEXT_ALIGNMENT_CENTER);
    textLine(view.name, cx + face + 10, cy, key_x - 15 - 8 - (cx + face + 10), settings_body_font_.Get(), gray(0.98f, alpha));
}

float Renderer::drawCompanionInTile(ID2D1Geometry* shape, float x, float bottom, float figure_height, float alpha) {
    const auto& view = *context_->companion;
    target_->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), shape), nullptr);
    // On start she jumps up out of the tile's bottom edge, as on the default home.
    const float rise = std::max(companion_ui::entranceDrop(view.entrance) * figure_height * 1.1f, 0.0f);
    drawCompanionFigure(view.look, view.pose, x, bottom + rise, figure_height, alpha, view.costume);
    target_->PopLayer();
    return bottom - figure_height * 0.44f;
}

void Renderer::drawCompanionTileBubble(D2D1_RECT_F tile, float mouth_y, float alpha, bool prefer_left) {
    const auto& view = *context_->companion;
    if (view.bubble_alpha <= 0.003f || (!view.bubble_waiting && view.bubble.empty())) return;
    constexpr float width = 380, gap = 10;
    const bool room_left = tile.left - gap - width >= 16, room_right = tile.right + gap + width <= viewport_.width - 16;
    const bool left = prefer_left ? room_left || !room_right : !room_right && room_left;
    const auto bubble = drawSpeechBubble(view.bubble, view.bubble_waiting && view.bubble.empty(), left ? tile.left - gap : tile.right + gap,
                                         mouth_y - 34, width, left ? tile.left + 18 : tile.right - 18, mouth_y,
                                         alpha * view.bubble_alpha, glance_ui::shortcut_top, left);
    companion_bubble_ = {bubble.left, bubble.top, bubble.right, bubble.bottom};
}

void Renderer::drawCompanionTile() {
    const auto& view = *context_->companion;
    const float alpha = view.tile_alpha;
    if (alpha <= 0.003f) return;
    const float hover = view.tile_hover;
    if (view.pill) { drawCompanionPill(box(view.spot), alpha, hover); return; }
    auto r = view.spot;
    r.left -= hover * 3; r.right += hover * 3; r.bottom += hover * 3;
    const auto rect = box(r);
    const float previous_corner = squircle_corner_, previous_reference = corner_reference_;
    squircle_corner_ = 0.4f; corner_reference_ = 80;
    const auto shape = tileShape(rect, tile_shape_);
    squircle_corner_ = previous_corner; corner_reference_ = previous_reference;
    if (!shape) return;
    // The same smoked glass as the Glance tile above it.
    const Glass glass{shape, glass_level_ > 0.01f && frostCapture(shape.Get(), glassBlur(24))};
    glassFill(glass, D2D1::ColorF(0.04f, 0.05f, 0.07f, glass_level_ > 0.01f ? 0.30f : 0.94f), alpha);
    brush(gray(1, (0.035f + hover * 0.075f) * alpha)); target_->FillGeometry(shape.Get(), solid_.Get());
    brush(gray(1, (0.13f + hover * 0.42f) * alpha)); target_->DrawGeometry(shape.Get(), solid_.Get(), 1.0f);
    // Her picture fills the tile and her head rises above it: clipped by the tile's bottom only.
    const auto figure = companion_ui::tileFigure(view.spot);
    ComPtr<ID2D1RectangleGeometry> above;
    ComPtr<ID2D1PathGeometry> mask;
    ComPtr<ID2D1GeometrySink> sink;
    if (SUCCEEDED(factory_->CreateRectangleGeometry(D2D1::RectF(r.left + 1, r.top - 320, r.right - 1, r.bottom - 40), above.GetAddressOf())) &&
        SUCCEEDED(factory_->CreatePathGeometry(mask.GetAddressOf())) && SUCCEEDED(mask->Open(sink.GetAddressOf())) &&
        SUCCEEDED(shape->CombineWithGeometry(above.Get(), D2D1_COMBINE_MODE_UNION, nullptr, sink.Get())) && SUCCEEDED(sink->Close())) {
        target_->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), mask.Get()), nullptr);
        // Below the tile's clipped edge at first; never lifted so far that her cut bottom edge shows.
        const float rise = std::max(companion_ui::entranceDrop(view.entrance) * figure.height * 1.1f, 2 - companion_ui::figure_sink);
        drawCompanionFigure(view.look, view.pose, figure.x + hover * 2, figure.bottom + rise, figure.height, alpha, view.costume);
        target_->PopLayer();
    }
    // Her nameplate over the picture: the shortcut, her name and personality.
    const float plate_h = 40, plate_top = r.bottom - 14 - plate_h;
    const float plate_w = std::min(r.right - r.left - 28, 64 + textAdvance(view.name + L"  " + view.persona, settings_body_font_.Get()));
    const auto plate = D2D1::RectF(r.left + 14, plate_top, r.left + 14 + plate_w, plate_top + plate_h);
    brush(D2D1::ColorF(0.02f, 0.022f, 0.03f, 0.72f * alpha)); target_->FillRoundedRectangle(pill(plate), solid_.Get());
    const float cx = plate.left + plate_h * 0.5f, cy = (plate.top + plate.bottom) * 0.5f;
    brush(gray(0.98f, alpha)); target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), 15, 15), solid_.Get());
    textLine(view.key, cx - 15, cy, 30, toast_sub_font_.Get(), gray(0.08f, alpha), DWRITE_TEXT_ALIGNMENT_CENTER);
    textLine(view.name, cx + 24, cy, plate.right - cx - 30, settings_body_font_.Get(), gray(0.98f, alpha));
    const float name_w = textAdvance(view.name + L"  ", settings_body_font_.Get());
    textLine(view.persona, cx + 24 + name_w, cy, plate.right - cx - 30 - name_w, settings_body_font_.Get(), gray(0.68f, alpha));
    // Gamification: her level beside the nameplate, or above it when the tile is too narrow.
    if (view.affinity) {
        const float badge_w = drawCompanionAffinity(0, 0, alpha, false);
        if (plate.right + 8 + badge_w <= r.right - 14) drawCompanionAffinity(plate.right + 8, cy, alpha);
        else drawCompanionAffinity(plate.left, plate.top - 22, alpha);
    }
    companion_bubble_ = {};
    if (view.bubble_alpha > 0.003f && (view.bubble_waiting || !view.bubble.empty())) {
        // The tail points at her mouth from the bubble on her right (screen left).
        const float mouth_y = figure.bottom - figure.height * 0.44f;
        const float tip_x = r.left + 18, tip_y = mouth_y;
        const auto bubble = drawSpeechBubble(view.bubble, view.bubble_waiting && view.bubble.empty(), r.left - 10, mouth_y - 34,
                                             380, tip_x, tip_y, alpha * view.bubble_alpha, glance_ui::shortcut_top);
        companion_bubble_ = {bubble.left, bubble.top, bubble.right, bubble.bottom};
    }
}
