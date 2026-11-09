#include "game_controllers.hpp"
#include "pad_rumble.hpp"
#include "steam_controller.hpp"
#include "controller_overrides.hpp"
#include "user_config.hpp"
#include "../i18n/i18n.hpp"
#include "../model/controller_models.hpp"
#include <xinput.h>
#include <hidsdi.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Gaming.Input.h>
#include <winrt/Windows.Devices.Power.h>
#include <winrt/Windows.System.Power.h>
#include <map>
#include <cwctype>
#include <array>
#include <utility>

// XInput, Windows.Gaming.Input and Raw Input; the shared parsers are in game_controllers.cpp.
namespace {
struct HidInfo { HANDLE handle; uint16_t vendor, product; std::wstring path, name; };
std::vector<HidInfo> hidDevices() {
    UINT count = 0;
    GetRawInputDeviceList(nullptr, &count, sizeof(RAWINPUTDEVICELIST));
    std::vector<RAWINPUTDEVICELIST> list(count);
    if (!count || GetRawInputDeviceList(list.data(), &count, sizeof(RAWINPUTDEVICELIST)) == UINT(-1)) return {};
    std::vector<HidInfo> result;
    for (UINT i = 0; i < count; ++i) {
        if (list[i].dwType != RIM_TYPEHID) continue;
        RID_DEVICE_INFO info{sizeof(info)}; UINT size = sizeof(info);
        if (GetRawInputDeviceInfoW(list[i].hDevice, RIDI_DEVICEINFO, &info, &size) == UINT(-1) ||
            info.hid.usUsagePage != 1 || (info.hid.usUsage != 4 && info.hid.usUsage != 5)) continue;
        UINT length = 0; GetRawInputDeviceInfoW(list[i].hDevice, RIDI_DEVICENAME, nullptr, &length);
        std::wstring path(length, 0);
        if (GetRawInputDeviceInfoW(list[i].hDevice, RIDI_DEVICENAME, path.data(), &length) == UINT(-1)) continue;
        path.resize(wcslen(path.c_str()));
        std::wstring name;
        const auto device = CreateFileW(path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
        if (device != INVALID_HANDLE_VALUE) {
            wchar_t product[256]{};
            if (HidD_GetProductString(device, product, sizeof(product))) name = product;
            CloseHandle(device);
        }
        result.push_back({list[i].hDevice, static_cast<uint16_t>(info.hid.dwVendorId),
                          static_cast<uint16_t>(info.hid.dwProductId), path, name});
    }
    return result;
}
devices::Picture picture(uint16_t vendor, uint16_t product, const std::wstring& name) {
    return devices::classifyController(vendor, product, name);
}
bool dualsense(const HidInfo& hid) { return hid.vendor == 0x054c && (hid.product == 0x0ce6 || hid.product == 0x0df2); }
// DualShock 4 (both revisions) and Sony's wireless adapter.
bool dualshock4(const HidInfo& hid) { return hid.vendor == 0x054c && (hid.product == 0x05c4 || hid.product == 0x09cc || hid.product == 0x0ba0); }
// Read elsewhere: XInput pads (their HID shadow has IG_ in its path, Microsoft's own pads) and
// Valve's devices (Steam's virtual pads, Steam Deck, Steam Controller: Steam Input / its reader).
bool otherReader(const HidInfo& hid) {
    return hid.path.find(L"IG_") != std::wstring::npos || hid.vendor == 0x045e || hid.vendor == 0x28de;
}

// Any other gamepad or joystick (Switch Pro in its HID mode, 8BitDo in D-input, the many
// generic USB pads): its own report descriptor says where buttons, the stick and the hat are.
struct GenericPad {
    std::vector<uint8_t> preparsed;
    struct Axis { USAGE usage = 0; LONG min = 0, max = 0; bool present = false; };
    Axis x, y, hat;
    bool nintendo = false, valid = false;
    std::wstring name;
};
GenericPad describeGeneric(HANDLE device, const HidInfo& hid) {
    GenericPad pad;
    UINT size = 0;
    if (GetRawInputDeviceInfoW(device, RIDI_PREPARSEDDATA, nullptr, &size) == UINT(-1) || !size || size > 1 << 20) return pad;
    pad.preparsed.resize(size);
    if (GetRawInputDeviceInfoW(device, RIDI_PREPARSEDDATA, pad.preparsed.data(), &size) == UINT(-1)) return pad;
    const auto data = reinterpret_cast<PHIDP_PREPARSED_DATA>(pad.preparsed.data());
    HIDP_CAPS caps{};
    if (HidP_GetCaps(data, &caps) != HIDP_STATUS_SUCCESS || caps.UsagePage != 1 || (caps.Usage != 4 && caps.Usage != 5)) return pad;
    USHORT count = caps.NumberInputValueCaps;
    std::vector<HIDP_VALUE_CAPS> values(count);
    if (count && HidP_GetValueCaps(HidP_Input, values.data(), &count, data) == HIDP_STATUS_SUCCESS) {
        for (USHORT i = 0; i < count; ++i) {
            const auto& value = values[i];
            if (value.UsagePage != 1) continue;
            const USAGE first = value.IsRange ? value.Range.UsageMin : value.NotRange.Usage;
            const USAGE last = value.IsRange ? value.Range.UsageMax : value.NotRange.Usage;
            for (USAGE usage = first; usage <= last && usage >= first; ++usage) {
                GenericPad::Axis axis{usage, value.LogicalMin, value.LogicalMax, true};
                // A descriptor without a signed range: the field's own size.
                if (axis.max <= axis.min && value.BitSize > 0 && value.BitSize < 32) { axis.min = 0; axis.max = (1L << value.BitSize) - 1; }
                if (usage == 0x30 && !pad.x.present) pad.x = axis;
                else if (usage == 0x31 && !pad.y.present) pad.y = axis;
                else if (usage == 0x39 && !pad.hat.present) pad.hat = axis;
            }
        }
    }
    pad.nintendo = hid.vendor == 0x057e;
    pad.name = pad.nintendo ? L"Pro Controller" : L"";
    pad.valid = true;
    return pad;
}
// Buttons by position, as the XInput layout names them (bottom A, right B, left X, top Y).
// Nintendo numbers B, A, Y, X; most other HID pads X, A, B, Y (the classic DirectInput order).
WORD genericButton(USAGE number, bool nintendo, bool& left_trigger) {
    static constexpr WORD common[]{0, XINPUT_GAMEPAD_X, XINPUT_GAMEPAD_A, XINPUT_GAMEPAD_B, XINPUT_GAMEPAD_Y};
    static constexpr WORD switch_pro[]{0, XINPUT_GAMEPAD_A, XINPUT_GAMEPAD_B, XINPUT_GAMEPAD_X, XINPUT_GAMEPAD_Y};
    if (number >= 1 && number <= 4) return nintendo ? switch_pro[number] : common[number];
    switch (number) {
    case 5: return XINPUT_GAMEPAD_LEFT_SHOULDER;
    case 6: return XINPUT_GAMEPAD_RIGHT_SHOULDER;
    case 7: left_trigger = true; return 0;
    case 8: return pad_right_trigger;
    case 9: return XINPUT_GAMEPAD_BACK;
    case 10: return XINPUT_GAMEPAD_START;
    case 11: return XINPUT_GAMEPAD_LEFT_THUMB;
    case 12: return XINPUT_GAMEPAD_RIGHT_THUMB;
    case 13: return pad_guide;
    default: return 0;
    }
}
PadReading parseGeneric(const GenericPad& pad, std::span<const uint8_t> report) {
    PadReading result;
    if (!pad.valid || report.empty()) return result;
    auto* data = reinterpret_cast<PHIDP_PREPARSED_DATA>(const_cast<uint8_t*>(pad.preparsed.data()));
    auto* bytes = reinterpret_cast<PCHAR>(const_cast<uint8_t*>(report.data()));
    const auto length = static_cast<ULONG>(report.size());
    ULONG count = HidP_MaxUsageListLength(HidP_Input, 9, data);
    std::array<USAGE, 64> usages{};
    count = std::min<ULONG>(count, static_cast<ULONG>(usages.size()));
    if (count && HidP_GetUsages(HidP_Input, 9, 0, usages.data(), &count, data, bytes, length) == HIDP_STATUS_SUCCESS)
        for (ULONG i = 0; i < count; ++i) result.buttons |= genericButton(usages[i], pad.nintendo, result.left_trigger);
    const auto axis = [&](const GenericPad::Axis& a) -> std::optional<int> {
        ULONG value = 0;
        if (!a.present || a.max <= a.min || HidP_GetUsageValue(HidP_Input, 1, 0, a.usage, &value, data, bytes, length) != HIDP_STATUS_SUCCESS) return {};
        const double t = (static_cast<double>(static_cast<LONG>(value)) - a.min) / (static_cast<double>(a.max) - a.min);
        return static_cast<int>(std::clamp(t * 65535.0 - 32768.0, -32768.0, 32767.0));
    };
    if (const auto x = axis(pad.x)) result.x = static_cast<SHORT>(*x);
    if (const auto y = axis(pad.y)) result.y = static_cast<SHORT>(std::clamp(-*y - 1, -32768, 32767)); // HID: down is positive.
    if (pad.hat.present) {
        ULONG value = 0;
        if (HidP_GetUsageValue(HidP_Input, 1, 0, pad.hat.usage, &value, data, bytes, length) == HIDP_STATUS_SUCCESS) {
            const long hat = static_cast<long>(value) - pad.hat.min; // 0 up, clockwise; out of range = centred.
            if (hat >= 0 && hat <= 7 && static_cast<long>(value) <= pad.hat.max) {
                if (hat == 7 || hat == 0 || hat == 1) result.buttons |= XINPUT_GAMEPAD_DPAD_UP;
                if (hat >= 1 && hat <= 3) result.buttons |= XINPUT_GAMEPAD_DPAD_RIGHT;
                if (hat >= 3 && hat <= 5) result.buttons |= XINPUT_GAMEPAD_DPAD_DOWN;
                if (hat >= 5 && hat <= 7) result.buttons |= XINPUT_GAMEPAD_DPAD_LEFT;
            }
        }
    }
    result.connected = true;
    return result;
}

// Ordinal 100 of xinput1_4.dll (XInputGetStateEx) also reports the Guide button.
// Undocumented but stable since Windows 8; public XInputGetState is the fallback.
using GetState = DWORD(WINAPI*)(DWORD, XINPUT_STATE*);
GetState stateReader() {
    static const GetState reader = [] {
        HMODULE module = GetModuleHandleW(L"xinput1_4.dll");
        if (!module) module = LoadLibraryExW(L"xinput1_4.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        const auto extended = module ? reinterpret_cast<GetState>(GetProcAddress(module, reinterpret_cast<LPCSTR>(100))) : nullptr;
        return extended ? extended : static_cast<GetState>(XInputGetState);
    }();
    return reader;
}
}

namespace {
// Owned and used only by the rumble worker. Opening/writing HID never blocks UI.
struct RumbleOutput {
    HANDLE hid = INVALID_HANDLE_VALUE;
    uint8_t sequence = 0;
    ~RumbleOutput() { if (hid != INVALID_HANDLE_VALUE) CloseHandle(hid); }
    void operator()(const PadRumble::Target& target, bool running) {
        if (target.id.size() == 8 && target.id.starts_with(L"xinput:") && target.id[7] >= L'0' && target.id[7] <= L'3') {
            XINPUT_VIBRATION vibration{};
            if (running) { vibration.wLeftMotorSpeed = 5000; vibration.wRightMotorSpeed = 10000; }
            XInputSetState(static_cast<DWORD>(target.id[7] - L'0'), &vibration);
            return;
        }
        if (target.steam) { if (running) steam_controller::click(target.id); return; } // One-shot haptic click.
        if (running) hid = CreateFileW(target.id.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                      nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
        if (hid == INVALID_HANDLE_VALUE) return;
        auto bytes = dualSenseRumbleReport(target.bluetooth, running, sequence++);
        OVERLAPPED io{}; io.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (io.hEvent) {
            DWORD sent = 0;
            if (!WriteFile(hid, bytes.data(), static_cast<DWORD>(bytes.size()), &sent, &io) && GetLastError() == ERROR_IO_PENDING) {
                if (WaitForSingleObject(io.hEvent, 40) != WAIT_OBJECT_0) CancelIoEx(hid, &io);
                GetOverlappedResult(hid, &io, &sent, TRUE); // Drain before releasing the report/event.
            }
            CloseHandle(io.hEvent);
        }
        if (!running) { CloseHandle(hid); hid = INVALID_HANDLE_VALUE; }
    }
};
}

bool gameBarUsesGuideButton() {
    DWORD value = 1, size = sizeof(value);
    const auto status = RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\GameBar", L"UseNexusForGameBarEnabled",
                                     RRF_RT_REG_DWORD, nullptr, &value, &size);
    return status != ERROR_SUCCESS || value != 0; // Missing value = Windows default (enabled).
}

std::vector<devices::Controller> enumerateControllers() {
    std::vector<devices::Controller> result;
    auto hid = hidDevices();
    std::vector<HANDLE> used;
    // With the experimental reader on, it alone lists Steam Controllers (per live Puck slot).
    const bool steam_reader = steam_controller::active();
    try {
        for (const auto& raw : winrt::Windows::Gaming::Input::RawGameController::RawGameControllers()) {
            devices::Controller pad;
            pad.id = raw.NonRoamableId().c_str(); pad.name = raw.DisplayName().c_str();
            const auto vendor = raw.HardwareVendorId(), product = raw.HardwareProductId();
            if (steam_reader && steam_controller::supported(vendor, product)) continue;
            pad.connection = raw.IsWireless() ? tr(L"Bezprzewodowo") : tr(L"USB / odbiornik");
            for (const auto& item : hid) {
                if (item.vendor != vendor || item.product != product || std::find(used.begin(), used.end(), item.handle) != used.end()) continue;
                used.push_back(item.handle);
                pad.id = item.path;
                if (!item.name.empty()) pad.name = item.name;
                break;
            }
            pad.picture = picture(vendor, product, pad.name);
            pad.name = devices::controllerDisplayName(vendor, product, pad.name);
            if (auto info = raw.try_as<winrt::Windows::Gaming::Input::IGameControllerBatteryInfo>()) {
                const auto report = info.TryGetBatteryReport();
                if (report) {
                    const auto remaining = report.RemainingCapacityInMilliwattHours(), full = report.FullChargeCapacityInMilliwattHours();
                    if (remaining && full && full.Value() > 0 && remaining.Value() >= 0)
                        pad.battery = std::clamp(static_cast<int>(100.0 * remaining.Value() / full.Value()), 0, 100);
                    pad.charging = report.Status() == winrt::Windows::System::Power::BatteryStatus::Charging;
                }
            }
            result.push_back(std::move(pad));
        }
    } catch (const winrt::hresult_error&) { /* Raw Input remains available on older systems. */ }
    for (const auto& item : hid) {
        if (std::find(used.begin(), used.end(), item.handle) != used.end()) continue;
        if (steam_reader && steam_controller::supported(item.vendor, item.product)) continue;
        // IG_ is the HID shadow of an XInput controller already exposed by WGI.
        if (item.path.find(L"IG_") != item.path.npos && std::any_of(result.begin(), result.end(), [](const auto& pad) { return devices::xboxFamily(pad.picture); })) continue;
        devices::Controller pad;
        pad.id = item.path; pad.name = item.name.empty() ? std::wstring(tr(L"Kontroler HID")) : item.name;
        pad.picture = picture(item.vendor, item.product, pad.name);
        pad.name = devices::controllerDisplayName(item.vendor, item.product, pad.name);
        pad.connection = L"HID";
        result.push_back(std::move(pad));
    }
    for (const auto& steam : steam_controller::pads()) {
        devices::Controller pad;
        pad.id = steam.path; pad.name = steam.reading.name;
        pad.picture = picture(steam_controller::valve_vendor, steam.product, pad.name);
        pad.connection = steam.bluetooth ? std::wstring(L"Bluetooth") : steam.receiver ? std::wstring(tr(L"Bezprzewodowo")) : std::wstring(L"USB");
        pad.battery = steam.reading.battery; pad.charging = steam.reading.charging;
        result.push_back(std::move(pad));
    }
    loadControllerOverrides(result, userConfigDirectory() / L"controller_overrides.json");
    return result;
}

struct ControllerInput::State {
    // continuous: the pad reports all the time (Sony's), so a silent one has gone; other HID pads
    // may report only changes, and a held button stays held until the next report.
    struct Sample { PadReading reading; ULONGLONG time = 0; bool bluetooth = false; bool continuous = true; bool dualsense = true; };
    std::map<HANDLE, HidInfo> devices;
    std::map<HANDLE, Sample> samples;
    std::map<HANDLE, GenericPad> generic;
    // Steam Input mirrors a PlayStation/Nintendo/generic pad as a virtual XInput pad: while Steam
    // runs and an XInput pad is there, the raw HID reading of that same pad is not read twice
    // (alternating between the two lost presses). Checked every two seconds.
    ULONGLONG steam_checked = 0;
    bool steam_running = false;
    std::array<ULONGLONG, XUSER_MAX_COUNT> next_probe{};
    std::wstring active;
    std::unique_ptr<PadRumble> rumble;
    HWND window = nullptr;
    std::unique_ptr<steam_controller::Reader> steam; // Experimental: Steam Controller without Steam.
};
ControllerInput::ControllerInput() : state_(std::make_unique<State>()) {}
ControllerInput::~ControllerInput() = default;
void ControllerInput::steamController(bool enabled) {
    if (enabled == static_cast<bool>(state_->steam) || (enabled && !state_->window)) return;
    if (enabled) state_->steam = std::make_unique<steam_controller::Reader>(state_->window);
    else {
        if (steam_controller::known(state_->active)) stopRumble();
        state_->steam.reset(); // Joins the worker; it restores lizard mode first.
    }
}
void ControllerInput::initialize(HWND window) {
    state_->window = window;
    // Keep held-button state current while a game owns focus. Actions are still
    // dispatched only in the launcher, so returning with A held cannot relaunch.
    RAWINPUTDEVICE devices[]{{1, 4, RIDEV_DEVNOTIFY | RIDEV_INPUTSINK, window}, {1, 5, RIDEV_DEVNOTIFY | RIDEV_INPUTSINK, window}};
    RegisterRawInputDevices(devices, 2, sizeof(RAWINPUTDEVICE));
}
void ControllerInput::rawInput(HRAWINPUT input) {
    UINT size = 0;
    if (GetRawInputData(input, RID_INPUT, nullptr, &size, sizeof(RAWINPUTHEADER)) == UINT(-1) ||
        size > 65536 || size < sizeof(RAWINPUTHEADER) + 2 * sizeof(DWORD)) return;
    std::vector<uint8_t> bytes(size);
    if (GetRawInputData(input, RID_INPUT, bytes.data(), &size, sizeof(RAWINPUTHEADER)) != size) return;
    const auto* raw = reinterpret_cast<const RAWINPUT*>(bytes.data());
    if (raw->header.dwType != RIM_TYPEHID) return;
    auto it = state_->devices.find(raw->header.hDevice);
    if (it == state_->devices.end()) {
        // Only cached Raw Input metadata is needed for input dispatch. USB
        // product strings belong to the background device panel, not each frame.
        RID_DEVICE_INFO info{sizeof(info)}; UINT length = sizeof(info);
        if (GetRawInputDeviceInfoW(raw->header.hDevice, RIDI_DEVICEINFO, &info, &length) == UINT(-1)) return;
        HidInfo hid{raw->header.hDevice, static_cast<uint16_t>(info.hid.dwVendorId), static_cast<uint16_t>(info.hid.dwProductId)};
        GetRawInputDeviceInfoW(raw->header.hDevice, RIDI_DEVICENAME, nullptr, &length);
        hid.path.resize(length);
        if (!length || GetRawInputDeviceInfoW(raw->header.hDevice, RIDI_DEVICENAME, hid.path.data(), &length) == UINT(-1)) return;
        hid.path.resize(wcslen(hid.path.c_str()));
        it = state_->devices.emplace(raw->header.hDevice, std::move(hid)).first;
    }
    if (it == state_->devices.end() || otherReader(it->second)) return;
    if (steam_controller::supported(it->second.vendor, it->second.product)) return;
    const auto& hid = raw->data.hid;
    const size_t offset = reinterpret_cast<const uint8_t*>(hid.bRawData) - bytes.data();
    if (!hid.dwSizeHid || static_cast<uint64_t>(hid.dwCount) * hid.dwSizeHid > bytes.size() - offset) return;
    const bool sense = dualsense(it->second), shock = dualshock4(it->second);
    const GenericPad* generic = nullptr;
    if (!sense && !shock) {
        // Described once: its report layout comes from its own HID descriptor.
        auto found = state_->generic.find(raw->header.hDevice);
        if (found == state_->generic.end()) found = state_->generic.emplace(raw->header.hDevice, describeGeneric(raw->header.hDevice, it->second)).first;
        if (!found->second.valid) return;
        generic = &found->second;
    }
    for (DWORD i = 0; i < hid.dwCount; ++i) {
        const std::span<const uint8_t> report{hid.bRawData + i * hid.dwSizeHid, hid.dwSizeHid};
        auto reading = sense ? parseDualSense(report) : shock ? parseDualShock4(report) : parseGeneric(*generic, report);
        reading.id = it->second.path;
        reading.name = sense ? (it->second.product == 0x0df2 ? L"DualSense Edge" : L"DualSense") : shock ? L"DualShock 4" :
                       !generic->name.empty() ? generic->name : std::wstring(tr(L"Kontroler HID"));
        if (reading.connected)
            state_->samples[raw->header.hDevice] = {reading, GetTickCount64(), report[0] == 0x31 || report[0] == 0x11 || hid.dwSizeHid != 64,
                                                    sense || shock, sense};
    }
}
PadReading ControllerInput::read() {
    const auto now = GetTickCount64();
    std::vector<PadReading> available;
    for (DWORD slot = 0; slot < XUSER_MAX_COUNT; ++slot) {
        if (now < state_->next_probe[slot]) continue;
        XINPUT_STATE input{};
        if (stateReader()(slot, &input) == ERROR_SUCCESS) {
            available.push_back({true, L"xinput:" + std::to_wstring(slot), static_cast<WORD>(input.Gamepad.wButtons |
                (input.Gamepad.bRightTrigger > 128 ? pad_right_trigger : 0)), input.Gamepad.sThumbLX, input.Gamepad.sThumbLY,
                input.Gamepad.bLeftTrigger > 128});
            available.back().rx = input.Gamepad.sThumbRX; available.back().ry = input.Gamepad.sThumbRY;
        } else state_->next_probe[slot] = now + 500;
    }
    if (now >= state_->steam_checked) {
        state_->steam_checked = now + 2000;
        DWORD pid = 0, size = sizeof(pid);
        RegGetValueW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam\\ActiveProcess", L"pid", RRF_RT_REG_DWORD, nullptr, &pid, &size);
        state_->steam_running = false;
        if (const HANDLE process = pid ? OpenProcess(SYNCHRONIZE, FALSE, pid) : nullptr) {
            state_->steam_running = WaitForSingleObject(process, 0) == WAIT_TIMEOUT;
            CloseHandle(process);
        }
    }
    const bool mirrored = state_->steam_running && !available.empty(); // `available` holds the XInput pads so far.
    for (const auto& [handle, sample] : state_->samples)
        if ((!sample.continuous || now - sample.time < 3000) && sample.reading.connected && !mirrored) available.push_back(sample.reading);
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
void ControllerInput::deviceChanged(HANDLE device) {
    const auto found = state_->samples.find(device);
    if (found != state_->samples.end() && found->second.reading.id == state_->active) stopRumble();
    state_->devices.erase(device); state_->samples.erase(device); state_->generic.erase(device);
    state_->next_probe.fill(0);
}
void ControllerInput::pulseBoundary() {
    if (state_->active.empty()) return;
    PadRumble::Target target{state_->active};
    if (steam_controller::known(target.id)) target.steam = true;
    else if (!target.id.starts_with(L"xinput:")) {
        const auto found = std::find_if(state_->samples.begin(), state_->samples.end(), [&](const auto& entry) {
            return entry.second.reading.id == target.id;
        });
        if (found == state_->samples.end() || !found->second.dualsense) return; // Rumble reports are the DualSense's only.
        target.bluetooth = found->second.bluetooth;
    }
    if (!state_->rumble) {
        auto output = std::make_shared<RumbleOutput>();
        state_->rumble = std::make_unique<PadRumble>([output](const auto& pad, bool running) { (*output)(pad, running); });
    }
    state_->rumble->pulse(std::move(target));
}
void ControllerInput::stopRumble() { if (state_->rumble) state_->rumble->stop(); }
void ControllerInput::enrich(std::vector<devices::Controller>& controllers) const {
    for (auto& pad : controllers) for (const auto& [handle, sample] : state_->samples) {
        if (pad.id != sample.reading.id || !sample.reading.battery || GetTickCount64() - sample.time > 10000) continue;
        pad.battery = sample.reading.battery; pad.approximate = true; pad.charging = sample.reading.charging;
    }
}
