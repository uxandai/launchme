#pragma once
// Steam's echo of a pad as keys. While the Steam client runs on Linux (a Steam Deck in desktop mode,
// or launchme started outside Steam), Steam's desktop layout turns the controller's buttons into key
// presses (the D-pad into arrows, A into Enter, B into Escape…) and launchme reads the same buttons as
// a pad as well: every press would act twice. A navigation key that comes while a pad button or
// direction is held, or within `window` of a press, is that echo and is dropped; the pad's own reading
// acts. Typing (WM_CHAR) and keys a pad never sends pass. Pure: Launcher feeds it pads and keys.
#include <cstdint>
#include <cstdlib>

class PadKeyEcho {
public:
    static constexpr uint64_t window = 250;

    // On while Steam runs (Linux); off: nothing is dropped.
    void enable(bool on) { enabled_ = on; if (!on) held_ = false; }
    bool enabled() const { return enabled_; }

    // Each pad poll: a button or a stick direction held now, and when one was last pressed.
    void pad(bool connected, uint16_t buttons, int16_t x, int16_t y, bool left_trigger, uint64_t now) {
        const bool held = connected && (buttons || left_trigger || std::abs(static_cast<int>(x)) > 16000 || std::abs(static_cast<int>(y)) > 16000);
        if (held && !held_) last_press_ = now;
        held_ = held;
    }

    // A WM_KEYDOWN (auto-repeats included): true when it is Steam's echo of the pad.
    bool swallow(uintptr_t key, uint64_t now) const {
        if (!enabled_ || !padKey(key)) return false;
        return held_ || (last_press_ && now - last_press_ <= window);
    }

    // The keys Steam's layouts give a controller's buttons and directions.
    static bool padKey(uintptr_t key) {
        // Win32 virtual-key codes (the Linux port uses the same): arrows, Enter, Esc, Space, Tab,
        // Backspace, Page Up/Down, Home, End.
        switch (key) {
        case 0x26: case 0x28: case 0x25: case 0x27:
        case 0x0D: case 0x1B: case 0x20: case 0x09: case 0x08:
        case 0x21: case 0x22: case 0x24: case 0x23:
            return true;
        default: return false;
        }
    }

private:
    bool enabled_ = false, held_ = false;
    uint64_t last_press_ = 0;
};
