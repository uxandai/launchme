#pragma once
#include "../model/devices.hpp"
#include "pad_buttons.hpp"
#ifdef _WIN32
#include <Windows.h>
#endif
#include <cstdint>
#include <memory>
#include <span>

// Guide (Xbox logo) / PS button. XInput has no public flag for it; the value is
// what XInputGetStateEx reports, and the DualSense parser maps PS to the same bit.
constexpr uint16_t pad_guide = 0x0400;
// Reserved XInput bit, normalized from analog R2/RT (also participates in wake/refocus gates).
constexpr uint16_t pad_right_trigger = 0x0800;

struct PadReading {
    bool connected = false;
    std::wstring id;
    uint16_t buttons = 0; // XINPUT_GAMEPAD_* plus pad_guide and pad_right_trigger.
    int16_t x = 0, y = 0; // Y up, as XInput.
    bool left_trigger = false; // Analog L2/LT past half way (no free bit is left in `buttons`).
    std::optional<int> battery;
    bool charging = false;
    std::wstring name;
    int16_t rx = 0, ry = 0; // The right stick, Y up (zero where a reader has none).
};
// Pure parser: Bluetooth 0x01 (10 bytes, padded to 78 by Raw Input) is distinct
// from USB 0x01 (64 bytes); simple reports never fabricate battery information.
PadReading parseDualSense(std::span<const uint8_t> report);
// DualShock 4: USB 0x01, Bluetooth 0x11 (and its simple 0x01). No battery or rumble.
PadReading parseDualShock4(std::span<const uint8_t> report);
// Rumble-only output: USB 0x02 / Bluetooth 0x31 with HIDP CRC; no LEDs or triggers.
std::vector<uint8_t> dualSenseRumbleReport(bool bluetooth, bool running, uint8_t sequence = 0);
std::vector<devices::Controller> enumerateControllers();
// Windows setting "Open Xbox Game Bar using this button on a controller" (on by default).
// Always false elsewhere.
bool gameBarUsesGuideButton();
// Windows: XInput, Windows.Gaming.Input and Raw Input (game_controllers_windows.cpp).
// Linux: SDL3's gamepad API, polled by read(); hotplug is SDL's (game_controllers_linux.cpp).
class ControllerInput {
public:
    ControllerInput();
    ~ControllerInput();
#ifdef _WIN32
    void initialize(HWND window);
#else
    void initialize(void* window); // SDL_Window*, for the Steam Controller's focus check; may be null.
#endif
    // Experimental: 2026 Steam Controller straight from HID (steam_controller.hpp). After initialize().
    void steamController(bool enabled);
#ifdef _WIN32
    void rawInput(HRAWINPUT input);
    void deviceChanged(HANDLE device);
#else
    void deviceChanged(void* device); // SDL tracks hotplug; stops a rumble on a pad that left.
#endif
    PadReading read();
    void pulseBoundary();
    void stopRumble();
    void enrich(std::vector<devices::Controller>& controllers) const;
private:
    struct State;
    std::unique_ptr<State> state_;
};
