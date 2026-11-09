#pragma once
// What each game package is made of, shared by Półka's shelf (shelf_renderer.cpp) and the open case
// (game_case_renderer.cpp, package_renderer.cpp), so a case looks the same on the shelf and in the hand.
// The consoles' own cases (game_case::Style):
//   keep case (PC):  black plastic, a clear sleeve over the poster;
//   Xbox:            opaque lime green, the cover's black head band with the green logo;
//   Xbox 360:        translucent green, the white head band with the grey logo;
//   PS1:             a CD jewel case (142 x 125 mm): clear front over the black tray, its hinge down the left,
//                    the square 120 mm insert (its black PlayStation stripe printed on it, else drawn);
//   PS2:             black, the black head band; PS3: a clear Blu-ray case, the black band;
//   PS4:             translucent blue, the blue band; PS5: translucent blue, the white band, black logo;
//   GameCube:        black, the black band curving down; Wii: white, the white band, grey logo; Wii U: cerulean, its blue band;
//   PSP:             clear, black band; PS Vita: blue, dark blue band, a black game card;
//   DS:              black, white band; 3DS: white, white band, red logo; their grey game cards;
//   Switch:          clear plastic (a thin grey rim), the art to the insert's edges with the red logo tile
//                    in its top left corner; inside, the insert's white back through the plastic;
//   Switch 2:        translucent red plastic (a thin red rim), the red head band with the logo in its
//                    middle; inside, glossy red;
//   big box:         printed cardboard to its edges, square corners.
// Sizes, colours and bands after en.wikipedia.org/wiki/Keep_case and the boxes themselves. PS2, PS3, PS4,
// Xbox 360, GameCube, Wii, Wii U and Switch draw their real front (frameOf) over the art; their band
// here is only the fallback while it loads (or when assets/cases is missing).
#include "game_case_view.hpp"
#include <d2d1.h>
#include <cmath>
#include <cstdint>
#include <vector>

namespace package_look {
// The shapes of a cover's printed console mark: a band across its head, a stripe down its left, a corner
// tile; and the band atop a game card's label (its logo in the middle).
enum class Band : uint8_t { across, down, square, card };
struct Look {
    float corner = 0.012f;   // Corner radius, of the front's width (the art itself stays square).
    float border = 0.018f;   // The plastic rim around the insert, of the width (0: printed to the edges).
    float band = 0;          // The insert's head band, of the insert's height.
    float stripe = 0;        // Or a stripe down its left side, of the insert's width (PS1's inlay).
    float hinge = 0;         // A jewel case's hinge: the plastic left of the insert, beyond the border, of the width.
    float mark = 0;          // Or a square in its top left corner, of the insert's width (Switch: the red logo tile).
    bool band_centred = false; // The logo in the band's middle (Switch 2), else at its left.
    D2D1_COLOR_F band_colour{}, band_ink{1, 1, 1, 1};
    bool band_gloss = false;     // A white band printed glossy (a soft grey fade at its foot).
    const wchar_t* band_text = L""; // The console's name when its logo is missing.
    D2D1_COLOR_F rim{}, spine{}, spine_ink{}, spine_head{}, head_ink{1, 1, 1, 1}, edge{};
    bool spine_head_always = false; // The head band is part of the package (else Półka's "Platforma na grzbiecie").
    bool clear = false;      // Clear plastic: the insert's white back shows inside.
    float haze = 0;          // The clear sleeve's milky veil over the print.
    float sleeve_gloss = 1;  // How much the front shines (cardboard less).
    D2D1_COLOR_F card{0.20f, 0.20f, 0.21f, 1}; // A game card's shell.
    D2D1_COLOR_F tray{0, 0, 0, 0}; // Inside the open case (a = 0: the rim's plastic, or the insert's white behind clear plastic).
    // The insert's inner print seen inside the open case, across both halves (Switch): through clear
    // plastic in its colours, through Switch 2's red plastic in grey under the red.
    bool inside_art = false, inside_gray = false;
    D2D1_COLOR_F inside_tint{0, 0, 0, 0};
};
// A case whose front is the box's own square scan (PS1: ScreenScraper's / ES-DE's box-2D, the cover
// slot) before any poster: the scan is the whole insert, its PlayStation stripe printed on it.
inline bool scanFirst(const game_case::Kind& kind) { return kind.style == game_case::Style::ps1; }
// How light a colour reads (0 black, 1 white).
inline float brightness(D2D1_COLOR_F c) { return 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b; }
inline D2D1_COLOR_F rgb(float r, float g, float b) { return D2D1::ColorF(r, g, b); }
inline Look of(const game_case::Kind& kind) {
    using game_case::Style;
    Look look;
    const auto head = [&](float band, D2D1_COLOR_F colour, D2D1_COLOR_F ink, const wchar_t* text) {
        look.band = band; look.band_colour = colour; look.band_ink = ink; look.band_text = text;
        look.spine_head = colour; look.head_ink = ink; look.spine_head_always = true;
    };
    // Defaults: the black keep case.
    look.rim = rgb(0.05f, 0.05f, 0.05f);
    look.spine = rgb(0.012f, 0.012f, 0.012f);
    look.spine_ink = rgb(0.86f, 0.86f, 0.86f);
    look.edge = rgb(0.10f, 0.105f, 0.115f);
    switch (kind.style) {
    case Style::switch1:
        // The art to the insert's edges, the red logo tile in its top left corner; a thin rim of
        // clear grey plastic.
        look.corner = 0.035f; look.border = 0.016f;
        head(0, rgb(0.902f, 0.0f, 0.071f), rgb(1, 1, 1), L"NINTENDO\nSWITCH");
        look.mark = 0.205f;
        look.rim = rgb(0.75f, 0.757f, 0.753f);
        look.spine = rgb(0.94f, 0.94f, 0.93f); look.spine_ink = rgb(0.12f, 0.12f, 0.13f);
        look.edge = rgb(0.72f, 0.735f, 0.75f);
        look.clear = true; look.haze = 0.03f;
        break;
    case Style::switch2:
        // Red plastic, a thin rim of it round the insert; the red head band, the logo in its middle.
        look.corner = 0.035f; look.border = 0.020f;
        head(0.135f, rgb(0.85f, 0.04f, 0.09f), rgb(1, 1, 1), L"NINTENDO SWITCH 2");
        look.band_centred = true;
        look.spine_head = rgb(0.40f, 0.02f, 0.05f);
        look.rim = rgb(0.80f, 0.06f, 0.09f);
        look.spine = rgb(0.72f, 0.05f, 0.08f); look.spine_ink = rgb(0.98f, 0.95f, 0.95f);
        look.edge = rgb(0.66f, 0.05f, 0.08f);
        look.card = rgb(0.84f, 0.11f, 0.12f);
        look.tray = rgb(0.86f, 0.13f, 0.11f);
        break;
    case Style::big_box:
        look.corner = 0; look.border = 0;
        look.rim = rgb(0.86f, 0.84f, 0.79f);
        look.spine = rgb(0.11f, 0.11f, 0.15f); look.spine_ink = rgb(0.93f, 0.91f, 0.86f);
        look.edge = rgb(0.84f, 0.82f, 0.76f);
        look.sleeve_gloss = 0.45f;
        break;
    case Style::xbox:
        look.border = 0.022f;
        head(0.072f, rgb(0.03f, 0.035f, 0.03f), rgb(0.47f, 0.76f, 0.10f), L"XBOX");
        look.rim = rgb(0.33f, 0.60f, 0.06f);
        look.spine = rgb(0.06f, 0.07f, 0.05f); look.spine_ink = rgb(0.92f, 0.94f, 0.90f);
        look.edge = rgb(0.30f, 0.54f, 0.05f);
        break;
    case Style::xbox360:
        look.corner = 0.03f; look.border = 0.022f;
        head(0.072f, rgb(0.965f, 0.965f, 0.96f), rgb(0.36f, 0.38f, 0.38f), L"XBOX 360");
        look.band_gloss = true;
        look.spine_head = rgb(0.40f, 0.70f, 0.13f); look.head_ink = rgb(1, 1, 1);
        look.rim = rgb(0.48f, 0.65f, 0.20f);
        look.spine = rgb(0.93f, 0.93f, 0.92f); look.spine_ink = rgb(0.16f, 0.17f, 0.17f);
        look.edge = rgb(0.30f, 0.58f, 0.17f);
        look.haze = 0.02f;
        break;
    case Style::ps1:
        // The insert 120 x 120 mm in the 142 x 125 mm case: 2.5 mm of plastic round it, the hinge's
        // 17 mm more on its left; the clear lid over the black tray reads black.
        look.corner = 0.006f; look.border = 0.0176f; look.hinge = 0.12f;
        look.stripe = 0.105f; look.band_colour = rgb(0.025f, 0.025f, 0.03f); look.band_ink = rgb(1, 1, 1); look.band_text = L"PlayStation";
        look.spine_head = rgb(0.025f, 0.025f, 0.03f); look.spine_head_always = true;
        look.rim = rgb(0.045f, 0.045f, 0.05f);
        look.spine = rgb(0.03f, 0.03f, 0.035f); look.spine_ink = rgb(0.92f, 0.92f, 0.92f);
        look.edge = rgb(0.13f, 0.135f, 0.145f);
        look.haze = 0.025f;
        look.tray = rgb(0.035f, 0.035f, 0.04f); // The jewel case's black tray.
        break;
    case Style::ps2:
        look.corner = 0.03f; look.border = 0.02f;
        head(0.072f, rgb(0.02f, 0.02f, 0.025f), rgb(1, 1, 1), L"PlayStation 2");
        look.rim = rgb(0.106f, 0.106f, 0.106f);
        break;
    case Style::ps3:
        look.corner = 0.03f; look.border = 0.03f;
        head(0.085f, rgb(0.03f, 0.03f, 0.035f), rgb(1, 1, 1), L"PS3");
        look.rim = rgb(0.455f, 0.455f, 0.455f);
        look.spine = rgb(0.93f, 0.93f, 0.93f); look.spine_ink = rgb(0.12f, 0.12f, 0.13f);
        look.edge = rgb(0.50f, 0.50f, 0.51f);
        look.clear = true; look.haze = 0.03f;
        break;
    case Style::ps4:
        look.corner = 0.03f; look.border = 0.03f;
        head(0.085f, rgb(0.0f, 0.36f, 0.78f), rgb(1, 1, 1), L"PS4");
        look.rim = rgb(0.02f, 0.09f, 0.22f);
        look.spine = rgb(0.06f, 0.20f, 0.48f); look.spine_ink = rgb(0.96f, 0.97f, 1.0f);
        look.edge = rgb(0.04f, 0.14f, 0.34f);
        break;
    case Style::ps5:
        look.corner = 0.03f; look.border = 0.03f;
        head(0.085f, rgb(0.975f, 0.975f, 0.975f), rgb(0.03f, 0.03f, 0.035f), L"PS5");
        look.band_gloss = true;
        look.rim = rgb(0.10f, 0.30f, 0.66f);
        look.spine = rgb(0.95f, 0.95f, 0.95f); look.spine_ink = rgb(0.08f, 0.08f, 0.09f);
        look.edge = rgb(0.10f, 0.28f, 0.60f);
        break;
    case Style::gamecube:
        look.corner = 0.03f; look.border = 0.02f;
        head(0.072f, rgb(0.02f, 0.02f, 0.025f), rgb(1, 1, 1), L"NINTENDO GAMECUBE");
        look.rim = rgb(0.106f, 0.106f, 0.106f);
        break;
    case Style::wii:
        look.corner = 0.03f; look.border = 0.022f;
        head(0.072f, rgb(0.975f, 0.975f, 0.975f), rgb(0.55f, 0.57f, 0.58f), L"Wii");
        look.band_gloss = true;
        look.rim = rgb(0.88f, 0.88f, 0.88f);
        look.spine = rgb(0.96f, 0.96f, 0.96f); look.spine_ink = rgb(0.30f, 0.31f, 0.32f);
        look.edge = rgb(0.90f, 0.90f, 0.90f);
        look.clear = true;
        break;
    case Style::wii_u:
        look.corner = 0.03f; look.border = 0.022f;
        head(0.072f, rgb(0.0f, 0.62f, 0.86f), rgb(1, 1, 1), L"Wii U");
        look.rim = rgb(0.07f, 0.44f, 0.56f);
        look.spine = rgb(0.0f, 0.52f, 0.76f); look.spine_ink = rgb(1, 1, 1);
        look.edge = rgb(0.0f, 0.50f, 0.74f);
        break;
    case Style::psp:
        look.corner = 0.04f; look.border = 0.03f;
        head(0.08f, rgb(0.03f, 0.03f, 0.035f), rgb(1, 1, 1), L"PSP");
        look.rim = rgb(0.82f, 0.84f, 0.86f);
        look.spine = rgb(0.05f, 0.05f, 0.06f);
        look.edge = rgb(0.78f, 0.80f, 0.82f);
        look.clear = true; look.haze = 0.03f;
        break;
    case Style::vita:
        look.corner = 0.04f; look.border = 0.03f;
        head(0.09f, rgb(0.02f, 0.11f, 0.36f), rgb(1, 1, 1), L"PS Vita");
        look.rim = rgb(0.08f, 0.30f, 0.70f);
        look.spine = rgb(0.07f, 0.26f, 0.62f); look.spine_ink = rgb(1, 1, 1);
        look.edge = rgb(0.07f, 0.27f, 0.64f);
        look.card = rgb(0.07f, 0.07f, 0.08f);
        break;
    case Style::ds:
        look.corner = 0.04f; look.border = 0.03f;
        head(0.085f, rgb(0.97f, 0.97f, 0.97f), rgb(0.30f, 0.31f, 0.33f), L"NINTENDO DS");
        look.band_gloss = true;
        look.rim = rgb(0.05f, 0.05f, 0.055f);
        look.card = rgb(0.58f, 0.59f, 0.61f);
        break;
    case Style::n3ds:
        look.corner = 0.04f; look.border = 0.03f;
        head(0.085f, rgb(0.975f, 0.975f, 0.975f), rgb(0.82f, 0.04f, 0.10f), L"NINTENDO 3DS");
        look.band_gloss = true;
        look.rim = rgb(0.95f, 0.95f, 0.95f);
        look.spine = rgb(0.96f, 0.96f, 0.96f); look.spine_ink = rgb(0.22f, 0.22f, 0.24f);
        look.edge = rgb(0.90f, 0.90f, 0.90f);
        look.clear = true;
        look.card = rgb(0.80f, 0.80f, 0.82f);
        break;
    default:
        break;
    }
    if (look.tray.a == 0) look.tray = look.clear ? rgb(0.93f, 0.93f, 0.92f) : look.rim;
    return look;
}
// A console's real case front (assets/cases, Twicethedad's Game Cases, MIT): a 600 x 900 picture of the
// plastic and the cover's banner, clear where the art shows. Drawn over the art in two slices, so a case
// of another shape keeps its banner's proportions: the banner (to `banner` px) scaled with the width,
// the rest stretched to the case's height. The art starts at `art_top` px (under the banner's edge).
struct Frame { const wchar_t* file; float banner, art_top; };
constexpr float frame_width = 600, frame_height = 900, frame_rim = 10;
inline const Frame* frameOf(game_case::Style style) {
    using game_case::Style;
    static constexpr Frame ps2{L"ps2.png", 112, 104}, ps3{L"ps3.png", 93, 83}, ps4{L"ps4.png", 99, 93}, xbox360{L"xbox360.png", 157, 150},
        gamecube{L"gamecube.png", 113, 76}, wii{L"wii.png", 126, 33}, wii_u{L"wiiu.png", 73, 26}, switch1{L"switch.png", 126, 10};
    switch (style) {
    case Style::ps2: return &ps2;
    case Style::ps3: return &ps3;
    case Style::ps4: return &ps4;
    case Style::xbox360: return &xbox360;
    case Style::gamecube: return &gamecube;
    case Style::wii: return &wii;
    case Style::wii_u: return &wii_u;
    case Style::switch1: return &switch1;
    default: return nullptr;
    }
}
// A frame's row `px` on a front of `aspect` (width : height), in fractions of its height.
inline float frameRow(const Frame& frame, float px, float aspect) {
    const float banner = frame.banner * aspect / frame_width; // The banner at the front's width.
    if (px <= frame.banner) return px * aspect / frame_width;
    return banner + (px - frame.banner) * (1 - banner) / (frame_height - frame.banner);
}

// The front's outline in face fractions (corner radius rs of the width, rt of the height), clockwise in
// four arcs of equal length: top right, bottom right, bottom left, top left. Square: the four corners
// repeated, so a side strip (half the outline) still runs from one arc to the next.
inline std::vector<D2D1_POINT_2F> outline(float rs, float rt) {
    constexpr int steps = 6;
    constexpr float pi = 3.14159265f;
    const struct { float cx, cy, from; } arcs[] = {{1 - rs, rt, -90}, {1 - rs, 1 - rt, 0}, {rs, 1 - rt, 90}, {rs, rt, 180}};
    std::vector<D2D1_POINT_2F> points;
    for (const auto& arc : arcs)
        for (int i = 0; i <= steps; ++i) {
            const float angle = (arc.from + 90.0f * static_cast<float>(i) / steps) * pi / 180;
            points.push_back({arc.cx + rs * std::cos(angle), arc.cy + rt * std::sin(angle)});
        }
    return points;
}
}
