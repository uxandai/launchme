// An achievement just unlocked, over the game (achievement_toast_view.hpp): a dark card at the
// bottom right with the achievement's icon, "Osiągnięcie zdobyte", its name and description. The
// window is only as large as the card and its margin; the card slides in from the screen's edge.
#include "renderer.hpp"
#include "achievement_toast_view.hpp"
#include "image_decoder.hpp"
#include "palette.hpp"
#include <d2d1helper.h>
#include <algorithm>

using Microsoft::WRL::ComPtr;

bool Renderer::paintAchievementToast(const achievement_toast::View& toast) {
    using namespace achievement_toast;
    using palette::gray;
    PAINTSTRUCT ps{}; BeginPaint(hwnd_, &ps);
    bool painted = false;
    if (createTarget()) {
        target_->BeginDraw();
        target_->SetTransform(D2D1::Matrix3x2F::Scale(toast.scale, toast.scale));
        target_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
        target_->Clear(D2D1::ColorF(0, 0, 0, 0));
        // The icon: decoded once per toast (small, on this thread), again after a lost device.
        if (toast_icon_target_ != target_.Get()) { toast_backdrop_.Reset(); toast_backdrop_source_ = nullptr; } // A new target: new bitmaps.
        if (toast_icon_target_ != target_.Get() || toast_icon_id_ != toast.id) {
            toast_icon_.Reset();
            toast_icon_target_ = target_.Get();
            toast_icon_id_ = toast.id;
            if (toast.icon && !toast.icon->empty() && wic_) {
                DecodeKind kind;
                kind.short_edge = 128;
                const auto image = decodeImage(wic_.Get(), *toast.icon, kind);
                if (image.valid() && !image.pixels.empty())
                    target_->CreateBitmap(D2D1::SizeU(image.width, image.height), image.pixels.data(), image.width * 4,
                                          D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)),
                                          &toast_icon_);
            }
        }
        const float slide = std::clamp(toast.slide, 0.0f, 1.0f);
        const float left = (card_width + margin) * (1 - slide), top = 0;
        const auto card = D2D1::RectF(left, top, left + card_width, top + card_height);
        const float alpha = std::min(1.0f, slide * 1.6f);
        // Frosted glass, as Tryb TV's dialogs: the game under the card blurred (the launcher copies
        // and blurs it: a layered window has nothing behind it here), a dark tint over it, a faint
        // sheen along the top and a hairline rim. Without the copy: a nearly solid dark pane.
        const auto shape = D2D1::RoundedRect(card, 16, 16);
        bool frosted = false;
        if (toast.backdrop && toast.backdrop_width && toast.backdrop_height &&
            toast.backdrop->size() >= static_cast<size_t>(toast.backdrop_width) * toast.backdrop_height * 4) {
            const auto size = D2D1::SizeU(toast.backdrop_width, toast.backdrop_height);
            const bool fits = toast_backdrop_ &&
                              toast_backdrop_->GetPixelSize().width == size.width && toast_backdrop_->GetPixelSize().height == size.height;
            if (!fits) {
                toast_backdrop_.Reset();
                toast_backdrop_source_ = nullptr;
                target_->CreateBitmap(size, D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)),
                                      toast_backdrop_.GetAddressOf());
            }
            if (toast_backdrop_ && toast_backdrop_source_ != toast.backdrop.get() &&
                SUCCEEDED(toast_backdrop_->CopyFromMemory(nullptr, toast.backdrop->data(), toast.backdrop_width * 4)))
                toast_backdrop_source_ = toast.backdrop.get();
            ComPtr<ID2D1BitmapBrush> glass;
            if (toast_backdrop_ && toast_backdrop_source_ == toast.backdrop.get() &&
                SUCCEEDED(target_->CreateBitmapBrush(toast_backdrop_.Get(),
                    D2D1::BitmapBrushProperties(D2D1_EXTEND_MODE_CLAMP, D2D1_EXTEND_MODE_CLAMP, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR),
                    D2D1::BrushProperties(alpha), glass.GetAddressOf()))) {
                // Copy pixels → window pixels → DIPs (the target's transform scales DIPs to pixels).
                const float scale = std::max(0.01f, toast.scale);
                const float sx = (toast.backdrop_right - toast.backdrop_left) / toast.backdrop_width / scale;
                const float sy = (toast.backdrop_bottom - toast.backdrop_top) / toast.backdrop_height / scale;
                glass->SetTransform(D2D1::Matrix3x2F::Scale(sx, sy) *
                                    D2D1::Matrix3x2F::Translation(toast.backdrop_left / scale, toast.backdrop_top / scale));
                target_->FillRoundedRectangle(shape, glass.Get());
                frosted = true;
            }
        }
        brush(D2D1::ColorF(0.07f, 0.075f, 0.09f, (frosted ? 0.5f : 0.9f) * alpha));
        target_->FillRoundedRectangle(shape, solid_.Get());
        {
            const D2D1_GRADIENT_STOP stops[] = {{0.0f, D2D1::ColorF(1, 1, 1, 0.07f * alpha)}, {1.0f, D2D1::ColorF(1, 1, 1, 0.0f)}};
            ComPtr<ID2D1GradientStopCollection> collection;
            ComPtr<ID2D1LinearGradientBrush> sheen;
            if (SUCCEEDED(target_->CreateGradientStopCollection(stops, 2, collection.GetAddressOf())) &&
                SUCCEEDED(target_->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(D2D1::Point2F(0, card.top),
                                                            D2D1::Point2F(0, card.top + card_height * 0.5f)), collection.Get(), sheen.GetAddressOf())))
                target_->FillRoundedRectangle(shape, sheen.Get());
        }
        brush(D2D1::ColorF(1, 1, 1, 0.1f * alpha));
        target_->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(card.left + 0.5f, card.top + 0.5f, card.right - 0.5f, card.bottom - 0.5f), 15.5f, 15.5f),
                                      solid_.Get(), 1.0f / std::max(1.0f, toast.scale));
        constexpr float icon = 64, pad = 14;
        const auto picture = D2D1::RectF(card.left + pad + 4, card.top + (card_height - icon) / 2, card.left + pad + 4 + icon, card.top + (card_height + icon) / 2);
        if (toast_icon_) {
            target_->DrawBitmap(toast_icon_.Get(), picture, alpha, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
        } else {
            brush(uiAccent(0.85f * alpha));
            target_->FillRoundedRectangle(D2D1::RoundedRect(picture, 10, 10), solid_.Get());
            textLine(L"★", picture.left, (picture.top + picture.bottom) / 2, icon, friend_name_font_.Get(), gray(0.98f, alpha), DWRITE_TEXT_ALIGNMENT_CENTER);
        }
        const float text_left = picture.right + 14, text_width = card.right - pad - text_left;
        const float middle = card.top + card_height / 2;
        const bool described = !toast.description.empty();
        textLine(toast.heading, text_left, middle - (described ? 24 : 13), text_width, friend_section_font_.Get(), uiAccent(alpha));
        textLine(toast.name, text_left, middle + (described ? 0 : 11), text_width, friend_name_font_.Get(), gray(0.97f, alpha));
        if (described) textLine(toast.description, text_left, middle + 23, text_width, friend_status_font_.Get(), gray(0.68f, alpha));
        const auto result = target_->EndDraw();
        if (SUCCEEDED(result)) painted = layered_surface_.present(hwnd_);
        else { toast_icon_.Reset(); toast_backdrop_.Reset(); toast_backdrop_source_ = nullptr; toast_icon_target_ = nullptr; discardTarget(); }
    }
    EndPaint(hwnd_, &ps);
    return painted;
}
