#include "hdr_display.hpp"

#ifdef _WIN32
#include <Windows.h>

#include <vector>

namespace hdr_display {
namespace {
// Windows 11 24H2 (SDK 10.0.26100): DISPLAYCONFIG_DEVICE_INFO_GET_ADVANCED_COLOR_INFO_2 (15) and
// DISPLAYCONFIG_DEVICE_INFO_SET_HDR_STATE (16), declared here so older SDKs build too.
constexpr auto get_color_info_2 = static_cast<DISPLAYCONFIG_DEVICE_INFO_TYPE>(15);
constexpr auto set_hdr_state = static_cast<DISPLAYCONFIG_DEVICE_INFO_TYPE>(16);
struct ColorInfo2 {
    DISPLAYCONFIG_DEVICE_INFO_HEADER header;
    union {
        struct {
            UINT32 advancedColorSupported : 1, advancedColorActive : 1, reserved1 : 1, advancedColorLimitedByPolicy : 1,
                highDynamicRangeSupported : 1, highDynamicRangeUserEnabled : 1, wideColorSupported : 1, wideColorUserEnabled : 1, reserved : 24;
        };
        UINT32 value;
    };
    DISPLAYCONFIG_COLOR_ENCODING colorEncoding;
    UINT32 bitsPerColorChannel;
    UINT32 activeColorMode;
};
struct HdrState {
    DISPLAYCONFIG_DEVICE_INFO_HEADER header;
    union {
        struct { UINT32 enableHdr : 1, reserved : 31; };
        UINT32 value;
    };
};

struct Target { LUID adapter{}; UINT32 id = 0; std::wstring device; };

std::vector<Target> targets() {
    std::vector<Target> result;
    UINT32 path_count = 0, mode_count = 0;
    std::vector<DISPLAYCONFIG_PATH_INFO> paths;
    std::vector<DISPLAYCONFIG_MODE_INFO> modes;
    for (int attempt = 0; attempt < 3; ++attempt) {
        if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &path_count, &mode_count) != ERROR_SUCCESS) return result;
        paths.resize(path_count); modes.resize(mode_count);
        const LONG status = QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &path_count, paths.data(), &mode_count, modes.data(), nullptr);
        if (status == ERROR_SUCCESS) break;
        if (status != ERROR_INSUFFICIENT_BUFFER) return result;
    }
    paths.resize(path_count);
    for (const auto& path : paths) {
        DISPLAYCONFIG_SOURCE_DEVICE_NAME source{};
        source.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
        source.header.size = sizeof(source);
        source.header.adapterId = path.sourceInfo.adapterId;
        source.header.id = path.sourceInfo.id;
        if (DisplayConfigGetDeviceInfo(&source.header) != ERROR_SUCCESS) continue;
        result.push_back({path.targetInfo.adapterId, path.targetInfo.id, source.viewGdiDeviceName});
    }
    return result;
}

// Supported and on: the 24H2 call first (HDR itself), else advanced colour (HDR before 24H2).
std::optional<std::pair<bool, bool>> read(const Target& target, bool& modern) {
    ColorInfo2 info2{};
    info2.header.type = get_color_info_2; info2.header.size = sizeof(info2);
    info2.header.adapterId = target.adapter; info2.header.id = target.id;
    if (DisplayConfigGetDeviceInfo(&info2.header) == ERROR_SUCCESS) {
        modern = true;
        return std::pair<bool, bool>{info2.highDynamicRangeSupported != 0, info2.highDynamicRangeUserEnabled != 0};
    }
    DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO info{};
    info.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_ADVANCED_COLOR_INFO; info.header.size = sizeof(info);
    info.header.adapterId = target.adapter; info.header.id = target.id;
    if (DisplayConfigGetDeviceInfo(&info.header) != ERROR_SUCCESS) return std::nullopt;
    modern = false;
    return std::pair<bool, bool>{info.advancedColorSupported && !info.wideColorEnforced && !info.advancedColorForceDisabled, info.advancedColorEnabled != 0};
}
}

std::optional<State> query(void* window) {
    const HMONITOR monitor = window ? MonitorFromWindow(static_cast<HWND>(window), MONITOR_DEFAULTTOPRIMARY)
                                    : MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFOEXW info{}; info.cbSize = sizeof(info);
    if (!monitor || !GetMonitorInfoW(monitor, &info)) return std::nullopt;
    for (const auto& target : targets()) {
        if (_wcsicmp(target.device.c_str(), info.szDevice) != 0) continue;
        bool modern = false;
        const auto value = read(target, modern);
        if (!value) return std::nullopt;
        return State{target.device, value->first, value->second};
    }
    return std::nullopt;
}

bool set(const std::wstring& device, bool enabled) {
    for (const auto& target : targets()) {
        if (_wcsicmp(target.device.c_str(), device.c_str()) != 0) continue;
        bool modern = false;
        const auto before = read(target, modern);
        if (!before || !before->first) return false;
        if (before->second == enabled) return true;
        LONG status = ERROR_NOT_SUPPORTED;
        if (modern) {
            HdrState state{};
            state.header.type = set_hdr_state; state.header.size = sizeof(state);
            state.header.adapterId = target.adapter; state.header.id = target.id;
            state.enableHdr = enabled ? 1 : 0;
            status = DisplayConfigSetDeviceInfo(&state.header);
        }
        if (status != ERROR_SUCCESS) {
            DISPLAYCONFIG_SET_ADVANCED_COLOR_STATE state{};
            state.header.type = DISPLAYCONFIG_DEVICE_INFO_SET_ADVANCED_COLOR_STATE; state.header.size = sizeof(state);
            state.header.adapterId = target.adapter; state.header.id = target.id;
            state.enableAdvancedColor = enabled ? 1 : 0;
            status = DisplayConfigSetDeviceInfo(&state.header);
        }
        return status == ERROR_SUCCESS;
    }
    return false;
}
}
#else
namespace hdr_display {
std::optional<State> query(void*) { return std::nullopt; }
bool set(const std::wstring&, bool) { return false; }
}
#endif
