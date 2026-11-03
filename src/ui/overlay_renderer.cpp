#include "renderer.hpp"

bool Renderer::paintOverlay(const ViewContext& context) {
    PAINTSTRUCT ps{}; BeginPaint(hwnd_, &ps);
    bool painted = false;
    if (createTarget()) {
        updateViewport();
        context_ = &context;
        uploadDecoded();
        target_->BeginDraw();
        target_->SetTransform(D2D1::Matrix3x2F::Scale(viewport_.scale, viewport_.scale));
        target_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
        target_->Clear(D2D1::ColorF(0, 0, 0, 0));
        // A faint scrim also makes the interactive surface hit-testable.
        brush(D2D1::ColorF(0, 0, 0, 0.16f * context.guide_opacity));
        target_->FillRectangle(D2D1::RectF(0, 0, viewport_.width, viewport_.height), solid_.Get());
        drawQuickPanel();
        drawFriends();
        drawExitPrompt();
        drawNotification();
        const auto result = target_->EndDraw();
        context_ = nullptr;
        if (SUCCEEDED(result)) painted = layered_surface_.present(hwnd_);
        else discardTarget();
    }
    EndPaint(hwnd_, &ps);
    return painted;
}

#ifdef LAUNCHME_UI_TEST
int Renderer::overlayAlphaForTest(int x, int y) const {
    DIBSECTION bitmap{};
    if (!layered_surface_.dc() || GetObjectW(GetCurrentObject(layered_surface_.dc(), OBJ_BITMAP), sizeof(bitmap), &bitmap) != sizeof(bitmap) ||
        !bitmap.dsBm.bmBits || x < 0 || y < 0 || x >= bitmap.dsBm.bmWidth || y >= bitmap.dsBm.bmHeight) return -1;
    return static_cast<const uint8_t*>(bitmap.dsBm.bmBits)[y * bitmap.dsBm.bmWidthBytes + x * 4 + 3];
}
#endif
