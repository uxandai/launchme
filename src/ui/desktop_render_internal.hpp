#pragma once
// Colours and small geometry shared by desktop mode's library renderers (desktop_renderer.cpp,
// desktop_chrome_renderer.cpp, desktop_grid_renderer.cpp, desktop_pane_renderer.cpp).
#include "renderer_internal.hpp"
#include "desktop_theme.hpp"

namespace desktop_draw {
using palette::gray;
using render_detail::box;
using render_detail::smoothFade;
using render_detail::upper;
inline D2D1_COLOR_F rgb(float r, float g, float b, float a = 1) { return D2D1::ColorF(r, g, b, a); }
inline desktop_ui::Rect inflate(desktop_ui::Rect r, float by) { return {r.left - by, r.top - by, r.right + by, r.bottom + by}; }
// Surfaces: near-black in the theme's tint, a step lighter for each layer.
inline const D2D1_COLOR_F& app_bg = desktop_theme::current().app;
inline const D2D1_COLOR_F& side_bg = desktop_theme::current().side;
inline const D2D1_COLOR_F& card_bg = desktop_theme::current().card;
} // namespace desktop_draw
