#pragma once
// Półka's and Cover Flow's case in the hand (after PyroCade's case viewer and Boxshelf's inspect view):
// Enter on the focused case lifts it off the shelf and holds it up, closed, in real 3D (box3d.hpp):
// the right stick or a mouse drag turns it (it settles on its front, spine or back when let go).
// The right half fills in with the game's title, Graj / Zainstaluj, Instrukcja (a game with a manual)
// and Szczegóły, a short description and the platform's logo on a small tile of its colour.
// Pokaż środek (R3, O, a click on the case) moves it to the left, opens it and lets its medium slide out. What opens depends on
// the platform:
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

// The case held up (seconds since Enter, State::held): it flies off the shelf over travel_time and the
// right half rises in as below; Back plays it backwards at close_speed.
// Pokaż środek (State::time): seconds since the lid began to open; closing it plays backwards, faster. Each step starts while the
// one before settles: the lid as the case lands, the medium once the lid is two thirds open, the
// right half as the case arrives.
constexpr float travel_time = 0.48f;  // Off the shelf to its place on the left.
constexpr float lid_start = 0.30f, lid_time = 0.62f;    // The lid (big box: the top flap) opens.
constexpr float media_start = 0.64f, media_time = 0.56f; // The disc / cartridge / diskette slides out, the lid past upright.
constexpr float panel_start = 0.24f, panel_time = 0.42f; // The right half fades and rises in, row after row.
constexpr float panel_stagger = 0.055f;                  // Each row (title, facts, buttons, description, platform, hints) this much after the one above.
constexpr int panel_rows = 6;
constexpr float flap_media_start = 0.74f, flap_media_time = 0.58f; // The big box: the diskette rises once the flap is out of its way.
// A CD jewel case (PS1): its wide, thin lid swings slower and further, down flat beside the tray as
// a real one lies open, so it never tips towards the viewer; the disc comes out once it is down.
constexpr float jewel_lid_time = 1.05f, jewel_media_start = 1.02f;
constexpr float end_time = std::max({media_start + media_time, jewel_media_start + media_time, flap_media_start + flap_media_time,
                                     panel_start + panel_stagger * (panel_rows - 1) + panel_time});
constexpr float close_speed = 2.4f;   // Closing runs this much faster.
constexpr float lid_angle = 2.62f;    // How far the lid swings open (150°), radians.
constexpr float jewel_lid_angle = 2.97f; // A jewel case's (170°): lying open.
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
inline bool jewel(const Kind& kind) { return kind.style == Style::ps1; }
// The opening of this kind of case (a jewel case's lid takes its own time).
inline Phases phases(float time, const Kind& kind) {
    auto phase = phases(time);
    if (jewel(kind)) {
        phase.lid = easeInOut((time - lid_start) / jewel_lid_time);
        phase.media = easeBack((time - jewel_media_start) / media_time);
    }
    return phase;
}
inline float lidAngle(const Kind& kind) { return jewel(kind) ? jewel_lid_angle : lid_angle; }
// One row of the right half (0: the title … panel_rows - 1: the hints).
inline float panelRow(float time, int row) { return easeOut((time - panel_start - panel_stagger * row) / panel_time); }

constexpr float held_end = panel_start + panel_stagger * (panel_rows - 1) + panel_time;

// The right half's buttons: the primary action (Graj / Zainstaluj / …), Instrukcja (a game with a
// manual) and Szczegóły. Opening the case is no button: R3 / O / a click on it (Pokaż środek).
enum class Button { action, manual, details };
inline int buttonCount(bool manual) { return manual ? 3 : 2; }
inline Button buttonAt(int index, bool manual) {
    if (index <= 0) return Button::action;
    return manual && index == 1 ? Button::manual : Button::details;
}

// Turning the held case: radians a second at full stick, how far it tips, and where it settles
// once let go (the nearest quarter turn: its front, a spine, its back).
constexpr float turn_speed = 3.4f, tip_limit = 0.55f;
constexpr float half_pi = 1.57079633f, pi = 3.14159265f;
inline float settledYaw(float yaw) { return std::round(yaw / half_pi) * half_pi; }
// One step of a spring towards `goal` (frame-rate independent).
inline float settle(float value, float goal, float dt, float rate = 10.0f) { return goal + (value - goal) * std::exp(-rate * dt); }

struct Layout {
    float width, height;
    Kind kind;
    int buttons = 2; // buttonCount()
    float gutter() const { return std::max(64.0f, width * 0.05f); }
    float panelLeft() const { return std::max(width * 0.54f, width - 860); }
    float panelRight() const { return width - gutter(); }
    // The case at rest: as tall as fits, leaving the open lid (to its left) and the slid-out medium
    // (to its right) room before the right half.
    float caseHeight() const {
        const float a = aspect(kind);
        const float room = panelLeft() - 48 - gutter();
        const float span = kind.package == Package::big_box ? 1.75f : jewel(kind) ? 2.62f : 2.42f; // In front widths.
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
        const float hinge = gutter() + w * (jewel(kind) ? 1.06f : 0.90f); // A jewel case's lid lies flat: its whole width.
        return {hinge, middle - h * 0.5f, hinge + w, middle + h * 0.5f};
    }
    // The case held up, closed: as large as fits the left part, room left for it to turn.
    float heldHeight() const {
        const float a = aspect(kind);
        const float room = panelLeft() - 56 - gutter();
        const float size = 0.62f + 0.38f * game_case::height(kind);
        return std::max(140.0f, std::min({(height - 180 - 150) * size, room / (1.18f * a), 760.0f}));
    }
    settings_ui::Rect held() const {
        const float h = heldHeight(), w = h * aspect(kind);
        const float cx = gutter() + (panelLeft() - 56 - gutter()) * 0.5f, cy = 180 + (height - 180 - 96) * 0.5f;
        return {cx - w * 0.5f, cy - h * 0.5f, cx + w * 0.5f, cy + h * 0.5f};
    }
    // The case between the shelf (its front there, `from`) and where it is held.
    settings_ui::Rect heldAt(const settings_ui::Rect& from, float travel) const {
        const auto to = held();
        const auto mix = [&](float a, float b) { return a + (b - a) * travel; };
        const float lift = std::sin(travel * 3.14159265f) * heldHeight() * 0.04f;
        return {mix(from.left, to.left), mix(from.top, to.top) - lift, mix(from.right, to.right), mix(from.bottom, to.bottom) - lift};
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
    // A row of buttons (the primary one wider); one that would pass the right edge starts a second row.
    static constexpr float button_gap = 14, button_height = 60;
    static float buttonWidth(int index) { return index == 0 ? 224.0f : 176.0f; }
    settings_ui::Rect button(int index) const {
        float x = panelLeft(), y = buttonsTop();
        for (int i = 0; i < index; ++i) {
            x += buttonWidth(i) + button_gap;
            if (x + buttonWidth(i + 1) > panelRight()) { x = panelLeft(); y += button_height + button_gap; }
        }
        return {x, y, x + buttonWidth(index), y + button_height};
    }
    float buttonsBottom() const { return button(std::max(0, buttons - 1)).bottom; }
    settings_ui::Rect description() const {
        const float top = buttonsBottom() + 36;
        return {panelLeft(), top, panelRight(), std::min(height - 176, top + 190)};
    }
    settings_ui::Rect platformTile(float tile_width) const {
        const float top = description().bottom + 24;
        return {panelLeft(), top, panelLeft() + tile_width, top + 48};
    }
    int hit(float x, float y) const {
        for (int i = 0; i < buttons; ++i) if (button(i).contains(x, y)) return i;
        return -1;
    }
    // The held case's own area (a drag there turns it), or the open case's (at rest, its lid to its left).
    bool overHeld(float x, float y, bool opened = false) const {
        auto r = held();
        if (opened) { r = rest(); r.left -= caseWidth(); }
        const float pad = (r.bottom - r.top) * 0.08f;
        return x >= r.left - pad && x <= r.right + pad && y >= r.top - pad && y <= r.bottom + pad;
    }
};
}

namespace game_case_ui {
// The case in the hand (Launcher::game_case_).
struct State {
    bool open = false;      // The layer is up (Back plays it out).
    bool opened = false;    // Pokaż środek: the lid opens along `time`; else the closed case is held.
    bool closing = false;   // Playing backwards; the layer ends at time 0.
    float held = 0;         // Seconds along the held timeline (game_case::held_end at rest).
    float time = 0;         // Along the opening timeline (game_case::end_time at rest).
    int focus = 0;          // A button (game_case::buttonAt).
    int hover = -1;
    bool manual = false;    // The game has a manual: Instrukcja is a button.
    std::string key;        // gameKey of the game it shows; a change of library closes it.
    float from_left = 0, from_top = 0, from_right = 0, from_bottom = 0; // The front on the shelf.
    // The held case turned: yaw round its upright axis (positive: its spine towards the viewer),
    // pitch tipping its top away. The stick turns it; let go, it settles on the nearest quarter turn.
    float yaw = 0, pitch = 0;
    float stick_x = 0, stick_y = 0; // The right stick, -1..1, dead zone removed.
    bool dragging = false;
    float drag_x = 0, drag_y = 0, drag_yaw = 0, drag_pitch = 0;
    bool drag_moved = false; // A click on the case that never turned it opens it (or shuts it).
    float yaw_goal = 0;     // Where it settles.
    bool shown() const { return open || time > 0 || held > 0; }
};
}
