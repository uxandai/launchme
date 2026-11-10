// Tryb TV's CRT filter (Dostosuj wygląd → Inne → Filtr CRT): the settings handed to the renderer
// (crt_renderer.cpp, crt_filter.cpp) and the bend applied to pointer positions, so a click lands on
// what the bent picture shows under it. The desktop window has no filter.

#include "launcher.hpp"

#include <algorithm>

crt_view::Settings Launcher::crtSettings() const {
    crt_view::Settings crt;
#ifdef _WIN32
    crt.on = settings_.crt && !desktop_mode_;
#else
    crt.on = false; // Windows only for now (crt_filter.cpp is a Direct2D effect).
#endif
    crt.mask = static_cast<crt_view::Mask>(std::clamp(settings_.crt_mask, 0, crt_view::mask_count - 1));
    crt.warp_x = settings_.crt_warp_x; crt.warp_y = settings_.crt_warp_y;
    crt.mask_level = settings_.crt_mask_level; crt.scanlines = settings_.crt_scanlines;
    crt.glow = settings_.crt_glow; crt.phosphor = settings_.crt_phosphor;
    return crt;
}

void Launcher::applyCrt() {
    renderer_.setCrt(crtSettings());
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::crtPoint(float& x, float& y) const {
    const auto crt = crtSettings();
    if (!crt.on || !hwnd_) return;
    RECT client{};
    GetClientRect(hwnd_, &client);
    crt_view::warp(crt, static_cast<float>(client.right - client.left), static_cast<float>(client.bottom - client.top), x, y);
}
