#pragma once
// Półka's open case (after PyroCade's case viewer): Enter on the focused case lifts it off the shelf
// to the left, opens it and lets its medium slide out, while the right half fills in with the
// game's title, Graj / Zainstaluj and Szczegóły, a short description and the platform's logo on a
// small tile of its colour. What opens depends on the platform:
// - Amiga: a cardboard big box; its top flap folds back and the 3.5" diskette rises out of it;
// - Switch, Switch 2, DS, 3DS, PS Vita: their case opens like a book, the game card slides out;
// - anything else: a keep case (the console's own: Xbox green, PS4 blue, Wii white, a PS3 Blu-ray case,
//   a PS1 jewel case...) opens like a book, the disc slides out of it.
// Each console's case has its real proportions and size (package_look.hpp has its colours and print).
// Geometry and the timeline are shared by drawing (src/ui/game_case_renderer.cpp), the pointer and
// the keys (src/app/game_case.cpp).
#include "settings_view.hpp"
#include <algorithm>
#include <cmath>
#include <string>
#include <string_view>

namespace game_case {
enum class Package { keep_case, switch_case, big_box }; // How it opens: a book with a tray (disc / card), a box with a flap.
enum class Media { disc, cartridge, floppy };
// Whose case it is: its size, plastic and print (package_look.hpp).
enum class Style : uint8_t { plain, big_box, switch1, switch2, xbox, xbox360, ps1, ps2, ps3, ps4, ps5, gamecube, wii, wii_u, psp, vita, ds, n3ds };
struct Kind {
    Package package = Package::keep_case;
    Media media = Media::disc;
    bool red = false; // Switch 2: a red case and cartridge.
    Style style = Style::plain;
};
inline Kind kindOf(std::wstring_view platform) {
    if (platform == L"Amiga") return {Package::big_box, Media::floppy, false, Style::big_box};
    if (platform == L"Switch") return {Package::switch_case, Media::cartridge, false, Style::switch1};
    if (platform == L"Switch 2") return {Package::switch_case, Media::cartridge, true, Style::switch2};
    if (platform == L"PS Vita") return {Package::switch_case, Media::cartridge, false, Style::vita};
    if (platform == L"DS") return {Package::switch_case, Media::cartridge, false, Style::ds};
    if (platform == L"3DS") return {Package::switch_case, Media::cartridge, false, Style::n3ds};
    const struct { std::wstring_view name; Style style; } discs[] = {
        {L"Xbox", Style::xbox}, {L"Xbox 360", Style::xbox360}, {L"PS1", Style::ps1}, {L"PS2", Style::ps2}, {L"PS3", Style::ps3}, {L"PS4", Style::ps4},
        {L"PS5", Style::ps5}, {L"GameCube", Style::gamecube}, {L"Wii", Style::wii}, {L"Wii / GameCube", Style::wii}, {L"Wii U", Style::wii_u}, {L"PSP", Style::psp}};
    for (const auto& disc : discs) if (platform == disc.name) return {Package::keep_case, Media::disc, false, disc.style};
    return {};
}
// Front (width : height) and height (of a DVD case's 190 mm) of each package, the same on Półka's shelf
// and in the open case. PC games keep the shelf's 2:3 poster.
//   DVD keep case 135 x 190 mm, Blu-ray case 135 x 171.5, CD jewel case 142 x 125, Switch 105 x 170,
//   PSP 99 x 168, PS Vita 105 x 135, DS / 3DS 122 x 135, Amiga big box about 22 x 29 cm (shown DVD-tall).
inline float aspect(const Kind& kind) {
    switch (kind.style) {
    case Style::big_box: return 0.76f;
    case Style::switch1: case Style::switch2: return 0.618f;
    case Style::xbox: case Style::xbox360: case Style::ps2: case Style::gamecube: case Style::wii: case Style::wii_u: return 0.711f;
    case Style::ps3: case Style::ps4: case Style::ps5: return 0.787f;
    case Style::ps1: return 1.136f;
    case Style::psp: return 0.589f;
    case Style::vita: return 0.778f;
    case Style::ds: case Style::n3ds: return 0.904f;
    default: return 2.0f / 3.0f;
    }
}
inline float height(const Kind& kind) {
    switch (kind.style) {
    case Style::switch1: case Style::switch2: return 0.895f;
    case Style::ps3: case Style::ps4: case Style::ps5: return 0.903f;
    case Style::ps1: return 0.658f;
    case Style::psp: return 0.884f;
    case Style::vita: case Style::ds: case Style::n3ds: return 0.711f;
    default: return 1.0f;
    }
}
// Thickness, of the front's width: 14 mm DVD, 12 mm Blu-ray, 10 mm jewel case, 11 mm Switch case, 5 cm big box.
// The plastic cases are drawn slimmer than life: in the shelf's perspective their true thickness reads heavy.
inline float depth(const Kind& kind) {
    constexpr float slim = 0.62f;
    switch (kind.style) {
    case Style::big_box: return 0.23f;
    case Style::ps3: case Style::ps4: case Style::ps5: return 0.09f * slim;
    case Style::ps1: return 0.07f * slim;
    case Style::psp: return 0.14f * slim;
    case Style::vita: case Style::ds: case Style::n3ds: return 0.11f * slim;
    case Style::plain: return 0.09f * slim;
    default: return 0.104f * slim;
    }
}
// The disc against a 12 cm one: GameCube's 8 cm mini disc, the PSP's 6 cm UMD.
inline float discSize(const Kind& kind) { return kind.style == Style::gamecube ? 0.667f : kind.style == Style::psp ? 0.55f : 1.0f; }

// Seconds since Enter. Closing plays the same timeline backwards, faster. Each step starts while the
// one before settles: the lid as the case lands, the medium once the lid is two thirds open, the
// right half as the case arrives.
constexpr float travel_time = 0.48f;  // Off the shelf to its place on the left.
constexpr float lid_start = 0.30f, lid_time = 0.62f;    // The lid (big box: the top flap) opens.
constexpr float media_start = 0.64f, media_time = 0.56f; // The disc / cartridge / diskette slides out, the lid past upright.
constexpr float panel_start = 0.24f, panel_time = 0.42f; // The right half fades and rises in, row after row.
constexpr float panel_stagger = 0.055f;                  // Each row (title, facts, buttons, description, platform, hints) this much after the one above.
constexpr int panel_rows = 6;
constexpr float flap_media_start = 0.74f, flap_media_time = 0.58f; // The big box: the diskette rises once the flap is out of its way.
constexpr float end_time = std::max({media_start + media_time, flap_media_start + flap_media_time, panel_start + panel_stagger * (panel_rows - 1) + panel_time});
constexpr float close_speed = 2.4f;   // Closing runs this much faster.
constexpr float lid_angle = 2.62f;    // How far the lid swings open (150°), radians.
constexpr float flap_angle = 2.30f;   // The big box's flap (about 130°).

inline float clamp01(float value) { return std::clamp(value, 0.0f, 1.0f); }
inline float easeOut(float t) { t = clamp01(t); return 1 - (1 - t) * (1 - t) * (1 - t); }
inline float easeInOut(float t) { t = clamp01(t); return t < 0.5f ? 4 * t * t * t : 1 - std::pow(-2 * t + 2, 3.0f) / 2; }
// A slight overshoot at the end (the medium settling where it was pulled to).
inline float easeBack(float t) {
    t = clamp01(t);
    constexpr float c1 = 0.90f, c3 = c1 + 1;
    return 1 + c3 * std::pow(t - 1, 3.0f) + c1 * std::pow(t - 1, 2.0f);
}
struct Phases { float travel = 0, lid = 0, media = 0, panel = 0; };
inline Phases phases(float time) {
    return {easeOut(time / travel_time), easeInOut((time - lid_start) / lid_time), easeBack((time - media_start) / media_time),
            easeOut((time - panel_start) / panel_time)};
}
// One row of the right half (0: the title … panel_rows - 1: the hints).
inline float panelRow(float time, int row) { return easeOut((time - panel_start - panel_stagger * row) / panel_time); }

// The right half's buttons: the primary action (Graj / Zainstaluj / …) and Szczegóły.
constexpr int button_count = 2;

struct Layout {
    float width, height;
    Kind kind;
    float gutter() const { return std::max(64.0f, width * 0.05f); }
    float panelLeft() const { return std::max(width * 0.54f, width - 860); }
    float panelRight() const { return width - gutter(); }
    // The case at rest: as tall as fits, leaving the open lid (to its left) and the slid-out medium
    // (to its right) room before the right half.
    float caseHeight() const {
        const float a = aspect(kind);
        const float room = panelLeft() - 48 - gutter();
        const float span = kind.package == Package::big_box ? 1.75f : 2.42f; // In front widths.
        // Smaller cases stay a little smaller in the hand too (a DS case beside a DVD one).
        const float size = 0.55f + 0.45f * game_case::height(kind);
        return std::max(120.0f, std::min({height * (kind.package == Package::big_box ? 0.50f : 0.58f) * size, room / (span * a), 640.0f}));
    }
    float caseWidth() const { return caseHeight() * aspect(kind); }
    // The case's front at rest. Book-like cases: their hinge (the spine, left edge) where the open
    // lid fits to its left. The big box: low enough for the diskette rising out of its top.
    settings_ui::Rect rest() const {
        const float h = caseHeight(), w = caseWidth();
        const float middle = 180 + (height - 180 - 64) * 0.5f;
        if (kind.package == Package::big_box) {
            const float left = gutter() + (panelLeft() - 48 - gutter() - w * 1.30f) * 0.5f;
            const float top = middle - h * 0.5f + h * 0.20f;
            return {left, top, left + w, top + h};
        }
        const float hinge = gutter() + w * 0.90f;
        return {hinge, middle - h * 0.5f, hinge + w, middle + h * 0.5f};
    }
    // The case between the shelf (its front there, `from`) and its rest.
    settings_ui::Rect at(const settings_ui::Rect& from, float travel) const {
        const auto to = rest();
        const auto mix = [&](float a, float b) { return a + (b - a) * travel; };
        // An arc: it rises a little on its way, as if lifted off the shelf.
        const float lift = std::sin(travel * 3.14159265f) * caseHeight() * 0.05f;
        return {mix(from.left, to.left), mix(from.top, to.top) - lift, mix(from.right, to.right), mix(from.bottom, to.bottom) - lift};
    }
    // The medium's size and its centre, out of the case by `out` (0: inside it, 1: out).
    float discRadius() const { return std::min(caseWidth(), caseHeight()) * 0.45f * discSize(kind); }
    float cartridgeWidth() const { return std::min(caseWidth(), caseHeight() * 0.62f) * 0.50f; } // Shown larger than life: its label reads.
    // In its holder: true to life (a 21 mm card in a 105 mm case), a little larger so its label shows.
    static constexpr float cartridge_inside = 0.46f; // Of cartridgeWidth().
    // The holder: low in the tray, a little left of its middle (as Nintendo moulds it); of the case's size.
    static constexpr float holder_x = -0.035f, holder_y = 0.315f;
    float cartridgeHeight() const { return cartridgeWidth() * 1.40f; }
    float floppySize() const { return caseWidth() * 0.66f; }       // Width; 3.5": 90 x 94 mm.
    struct Spot { float x, y, turn; };
    Spot media(const settings_ui::Rect& front, float out) const {
        const float w = front.right - front.left, h = front.bottom - front.top;
        const float cx = (front.left + front.right) * 0.5f, cy = (front.top + front.bottom) * 0.5f;
        switch (kind.media) {
        case Media::floppy: {
            // Up out of the box's top, a little to the right, tilting.
            const float fh = floppySize() / 0.957f;
            const float inside = front.top + fh * 0.5f + h * 0.06f, outside = front.top - fh * 0.20f;
            return {cx + w * 0.10f * out, inside + (outside - inside) * out, -0.07f * out};
        }
        case Media::cartridge: {
            // Out of its holder (low in the tray), up and to the right of the case.
            const float from_x = cx + w * holder_x, to_x = cx + w * 0.72f;
            const float from = cy + h * holder_y, to = cy - h * 0.08f;
            return {from_x + (to_x - from_x) * out, from + (to - from) * std::min(1.0f, out), 0.10f * out};
        }
        default:
            return {cx + w * 0.62f * out, cy - h * 0.02f * out, 0};
        }
    }
    // The right half, top down: the title, a line of facts, the buttons, the description, the platform tile.
    float titleTop() const { return std::max(200.0f, rest().top - (kind.package == Package::big_box ? caseHeight() * 0.20f : 0.0f)); }
    float buttonsTop() const { return titleTop() + 150; }
    settings_ui::Rect button(int index) const {
        const float left = panelLeft(), w = index == 0 ? 236.0f : 200.0f;
        const float x = index == 0 ? left : left + 236 + 16;
        return {x, buttonsTop(), x + w, buttonsTop() + 60};
    }
    settings_ui::Rect description() const {
        const float top = buttonsTop() + 60 + 36;
        return {panelLeft(), top, panelRight(), std::min(height - 176, top + 190)};
    }
    settings_ui::Rect platformTile(float tile_width) const {
        const float top = description().bottom + 24;
        return {panelLeft(), top, panelLeft() + tile_width, top + 48};
    }
    int hit(float x, float y) const {
        for (int i = 0; i < button_count; ++i) if (button(i).contains(x, y)) return i;
        return -1;
    }
};
}

namespace game_case_ui {
// The open case's state (Launcher::game_case_).
struct State {
    bool open = false;
    bool closing = false;   // Playing backwards; the layer ends at time 0.
    float time = 0;         // Along the timeline (game_case::end_time at rest).
    int focus = 0;          // 0: the primary action, 1: Szczegóły.
    int hover = -1;
    std::string key;        // gameKey of the game it shows; a change of library closes it.
    float from_left = 0, from_top = 0, from_right = 0, from_bottom = 0; // The front on the shelf.
};
}
