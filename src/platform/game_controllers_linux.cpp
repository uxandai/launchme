#include "game_controllers.hpp"
#include "pad_rumble.hpp"
#include "steam_controller.hpp"
#include "controller_overrides.hpp"
#include "linux_hid.hpp"
#include "text_encoding.hpp"
#include "user_config.hpp"
#include "../i18n/i18n.hpp"
#include "../model/controller_models.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <map>
#include <utility>

// SDL3's gamepad API: evdev and SDL's own HIDAPI drivers (DualSense, DualShock 4, Switch Pro,
// Xbox over USB/Bluetooth, ...), which also report battery and rumble. The window's event loop
// (sdl_window) pumps SDL; read() updates the pads itself so the state is current regardless.
namespace {
constexpr uint16_t valve = 0x28de, steam_virtual = 0x11ff;
// The subsystem is reference counted; each user keeps its own reference.
struct Subsystem {
    bool ready = false;
    Subsystem() {
        // Keep held-button state current while a game owns focus (Raw Input's INPUTSINK on Windows).
        SDL_SetHintWithPriority(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1", SDL_HINT_DEFAULT);
        ready = SDL_InitSubSystem(SDL_INIT_GAMEPAD);
    }
    ~Subsystem() { if (ready) SDL_QuitSubSystem(SDL_INIT_GAMEPAD); }
    Subsystem(const Subsystem&) = delete;
    Subsystem& operator=(const Subsystem&) = delete;
};
// Stable for the connection: SDL's device path (/dev/input/eventN, /dev/hidrawN).
std::wstring padId(SDL_JoystickID id) {
    const char* path = SDL_GetGamepadPathForID(id);
    return path && *path ? text_encoding::wideLenient(path) : L"sdl:" + std::to_wstring(id);
}
bool steamVirtual(SDL_JoystickID id) { return SDL_GetGamepadVendorForID(id) == valve && SDL_GetGamepadProductForID(id) == steam_virtual; }
std::vector<SDL_JoystickID> gamepadIds() {
    int count = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&count);
    std::vector<SDL_JoystickID> result(ids, ids + (ids ? count : 0));
    SDL_free(ids);
    return result;
}
void power(SDL_Gamepad* pad, std::optional<int>& battery, bool& charging) {
    int percent = -1;
    const auto state = SDL_GetGamepadPowerInfo(pad, &percent);
    if (state == SDL_POWERSTATE_ON_BATTERY || state == SDL_POWERSTATE_CHARGING || state == SDL_POWERSTATE_CHARGED) {
        if (percent >= 0) battery = std::clamp(percent, 0, 100);
        else if (state == SDL_POWERSTATE_CHARGED) battery = 100;
        charging = state == SDL_POWERSTATE_CHARGING;
    }
}
// Buttons by position, as the XInput layout names them (bottom A, right B, left X, top Y).
constexpr std::pair<SDL_GamepadButton, uint16_t> button_map[]{
    {SDL_GAMEPAD_BUTTON_SOUTH, XINPUT_GAMEPAD_A}, {SDL_GAMEPAD_BUTTON_EAST, XINPUT_GAMEPAD_B},
    {SDL_GAMEPAD_BUTTON_WEST, XINPUT_GAMEPAD_X}, {SDL_GAMEPAD_BUTTON_NORTH, XINPUT_GAMEPAD_Y},
    {SDL_GAMEPAD_BUTTON_BACK, XINPUT_GAMEPAD_BACK}, {SDL_GAMEPAD_BUTTON_START, XINPUT_GAMEPAD_START},
    {SDL_GAMEPAD_BUTTON_GUIDE, pad_guide}, {SDL_GAMEPAD_BUTTON_LEFT_STICK, XINPUT_GAMEPAD_LEFT_THUMB},
    {SDL_GAMEPAD_BUTTON_RIGHT_STICK, XINPUT_GAMEPAD_RIGHT_THUMB}, {SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, XINPUT_GAMEPAD_LEFT_SHOULDER},
    {SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, XINPUT_GAMEPAD_RIGHT_SHOULDER}, {SDL_GAMEPAD_BUTTON_DPAD_UP, XINPUT_GAMEPAD_DPAD_UP},
    {SDL_GAMEPAD_BUTTON_DPAD_DOWN, XINPUT_GAMEPAD_DPAD_DOWN}, {SDL_GAMEPAD_BUTTON_DPAD_LEFT, XINPUT_GAMEPAD_DPAD_LEFT},
    {SDL_GAMEPAD_BUTTON_DPAD_RIGHT, XINPUT_GAMEPAD_DPAD_RIGHT}};
PadReading reading(SDL_Gamepad* pad, SDL_JoystickID id) {
    PadReading result;
    result.connected = true;
    result.id = padId(id);
    const char* name = SDL_GetGamepadName(pad);
    result.name = name && *name ? text_encoding::wideLenient(name) : std::wstring(tr(L"Kontroler HID"));
    for (const auto& [button, bit] : button_map) if (SDL_GetGamepadButton(pad, button)) result.buttons |= bit;
    if (SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > 16384) result.buttons |= pad_right_trigger;
    result.left_trigger = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) > 16384;
    result.x = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX);
    result.y = static_cast<int16_t>(-1 - SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTY)); // SDL: down is positive.
    result.rx = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHTX);
    result.ry = static_cast<int16_t>(-1 - SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHTY));
    power(pad, result.battery, result.charging);
    return result;
}
// Used only by the rumble worker; the SDL lock keeps the pad open while it is driven.
void rumble(const PadRumble::Target& target, bool running) {
    if (target.steam) { if (running) steam_controller::click(target.id); return; } // One-shot haptic click.
    SDL_LockJoysticks();
    for (const auto id : gamepadIds()) {
        if (padId(id) != target.id) continue;
        if (auto* pad = SDL_GetGamepadFromID(id)) SDL_RumbleGamepad(pad, running ? 5000 : 0, running ? 10000 : 0, running ? 200 : 0);
        break;
    }
    SDL_UnlockJoysticks();
}
}

bool gameBarUsesGuideButton() { return false; }

std::vector<devices::Controller> enumerateControllers() {
    std::vector<devices::Controller> result;
    const Subsystem sdl;
    const bool steam_reader = steam_controller::active();
    if (sdl.ready) {
        SDL_UpdateGamepads(); // Hotplug, also without a window pumping events.
        SDL_LockJoysticks();
        for (const auto id : gamepadIds()) {
            const auto vendor = SDL_GetGamepadVendorForID(id), product = SDL_GetGamepadProductForID(id);
            if ((steam_reader && steam_controller::supported(vendor, product)) || (vendor == valve && product == steam_virtual)) continue;
            devices::Controller pad;
            pad.id = padId(id);
            const char* name = SDL_GetGamepadNameForID(id);
            pad.name = name && *name ? text_encoding::wideLenient(name) : std::wstring(tr(L"Kontroler HID"));
            const auto node = linux_hid::node(pad.id);
            if (node && !node->name.empty() && (!name || !*name)) pad.name = text_encoding::wideLenient(node->name);
            pad.connection = node && node->bus == linux_hid::bus_bluetooth ? tr(L"Bezprzewodowo") : tr(L"USB / odbiornik");
            pad.picture = devices::classifyController(vendor, product, pad.name);
            pad.name = devices::controllerDisplayName(vendor, product, pad.name);
            // Open pads (ControllerInput's) answer through SDL; others through the kernel driver.
            if (auto* open = SDL_GetGamepadFromID(id)) {
                power(open, pad.battery, pad.charging);
                if (SDL_GetGamepadConnectionState(open) == SDL_JOYSTICK_CONNECTION_WIRELESS) pad.connection = tr(L"Bezprzewodowo");
            }
            if (!pad.battery && node)
                if (const auto level = linux_hid::battery(node->device)) { pad.battery = level->level; pad.charging = level->charging; }
            result.push_back(std::move(pad));
        }
        SDL_UnlockJoysticks();
    }
    for (const auto& steam : steam_controller::pads()) {
        devices::Controller pad;
        pad.id = steam.path; pad.name = steam.reading.name;
        pad.picture = devices::classifyController(steam_controller::valve_vendor, steam.product, pad.name);
        pad.connection = steam.bluetooth ? std::wstring(L"Bluetooth") : steam.receiver ? std::wstring(tr(L"Bezprzewodowo")) : std::wstring(L"USB");
        pad.battery = steam.reading.battery; pad.charging = steam.reading.charging;
        result.push_back(std::move(pad));
    }
    loadControllerOverrides(result, userConfigDirectory() / L"controller_overrides.json");
    return result;
}

struct ControllerInput::State {
    Subsystem sdl;
    std::map<SDL_JoystickID, SDL_Gamepad*> pads;
    std::map<std::wstring, PadReading> last; // By id, for enrich().
    std::wstring active;
    std::unique_ptr<PadRumble> rumble;
    void* window = nullptr;
    std::unique_ptr<steam_controller::Reader> steam; // Experimental: Steam Controller without Steam.
    ~State() {
        rumble.reset(); // Joins the worker before its pads close.
        steam.reset();
        for (const auto& [id, pad] : pads) SDL_CloseGamepad(pad);
    }
};
ControllerInput::ControllerInput() : state_(std::make_unique<State>()) {}
ControllerInput::~ControllerInput() = default;
void ControllerInput::initialize(void* window) { state_->window = window; }
void ControllerInput::steamController(bool enabled) {
    if (enabled == static_cast<bool>(state_->steam)) return;
    if (enabled) state_->steam = std::make_unique<steam_controller::Reader>(state_->window);
    else {
        if (steam_controller::known(state_->active)) stopRumble();
        state_->steam.reset(); // Joins the worker; it restores lizard mode first.
    }
}
void ControllerInput::deviceChanged(void*) {
    if (!state_->active.empty() && !state_->last.contains(state_->active)) stopRumble();
}
PadReading ControllerInput::read() {
    std::vector<PadReading> available;
    if (state_->sdl.ready) {
        SDL_UpdateGamepads();
        const auto ids = gamepadIds();
        for (auto it = state_->pads.begin(); it != state_->pads.end();) {
            if (std::find(ids.begin(), ids.end(), it->first) != ids.end() && SDL_GamepadConnected(it->second)) { ++it; continue; }
            SDL_CloseGamepad(it->second);
            it = state_->pads.erase(it);
        }
        for (const auto id : ids) {
            if (state_->pads.contains(id) || steam_controller::supported(SDL_GetGamepadVendorForID(id), SDL_GetGamepadProductForID(id))) continue;
            if (auto* pad = SDL_OpenGamepad(id)) state_->pads.emplace(id, pad);
        }
        // SDL shows Steam's virtual pads only to programs Steam starts (with Steam Input on). Then Steam
        // mirrors the physical pads it handles: read only the virtual ones, never one pad twice.
        const bool mirrored = std::any_of(state_->pads.begin(), state_->pads.end(), [](const auto& pad) { return steamVirtual(pad.first); });
        state_->last.clear();
        for (const auto& [id, pad] : state_->pads) {
            auto value = reading(pad, id);
            state_->last[value.id] = value;
            if (!mirrored || steamVirtual(id)) available.push_back(std::move(value));
        }
    }
    // While Steam runs it owns the Steam Controller (desktop layout / Steam Input): no double input.
    if (state_->steam)
        for (auto& pad : steam_controller::pads()) if (pad.input) available.push_back(std::move(pad.reading));
    if (available.empty()) { if (!state_->active.empty()) stopRumble(); state_->active.clear(); return {}; }
    const auto active = std::find_if(available.begin(), available.end(), [&](const auto& pad) { return pad.id == state_->active; });
    auto chosen = active != available.end() ? active : available.begin();
    for (auto it = available.begin(); it != available.end(); ++it)
        if (it->buttons || std::abs(static_cast<int>(it->x)) > 16000 || std::abs(static_cast<int>(it->y)) > 16000) { chosen = it; break; }
    if (state_->active != chosen->id) stopRumble();
    state_->active = chosen->id;
    return *chosen;
}
void ControllerInput::pulseBoundary() {
    if (state_->active.empty()) return;
    PadRumble::Target target{state_->active};
    target.steam = steam_controller::known(target.id);
    if (!state_->rumble) state_->rumble = std::make_unique<PadRumble>(rumble);
    state_->rumble->pulse(std::move(target));
}
void ControllerInput::stopRumble() { if (state_->rumble) state_->rumble->stop(); }
void ControllerInput::enrich(std::vector<devices::Controller>& controllers) const {
    for (auto& pad : controllers) {
        const auto found = state_->last.find(pad.id);
        if (pad.battery || found == state_->last.end() || !found->second.battery) continue;
        pad.battery = found->second.battery; pad.charging = found->second.charging;
    }
}
