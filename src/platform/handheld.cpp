#include "handheld.hpp"
#ifdef _WIN32
#include <Windows.h>
#else
#include <fstream>
#endif
#include <algorithm>
#include <string>

namespace handheld {
namespace {
std::string lower(std::string_view text) {
    std::string result(text);
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) { return static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c); });
    return result;
}
bool contains(const std::string& text, std::string_view part) { return text.find(part) != std::string::npos; }

#ifdef _WIN32
std::string biosValue(const wchar_t* name) {
    wchar_t value[256]{};
    DWORD size = sizeof(value) - sizeof(wchar_t);
    if (RegGetValueW(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\BIOS", name, RRF_RT_REG_SZ, nullptr, value, &size) != ERROR_SUCCESS) return {};
    std::string result;
    for (const wchar_t* c = value; *c; ++c) result.push_back(*c < 128 ? static_cast<char>(*c) : '?');
    return result;
}
#else
std::string dmiValue(const char* name) {
    std::ifstream file(std::string("/sys/devices/virtual/dmi/id/") + name);
    std::string value;
    std::getline(file, value);
    return value;
}
#endif
}

bool knownModel(std::string_view vendor_text, std::string_view product_text, std::string_view family_text) {
    const auto vendor = lower(vendor_text), product = lower(product_text), family = lower(family_text);
    // Makers that build nothing but handhelds.
    for (const std::string_view maker : {"ayaneo", "ayadevice", "gpd", "one-netbook", "onexplayer", "ayn"})
        if (vendor == maker || vendor.starts_with(std::string(maker) + ' ')) return true;
    // Valve: Jupiter (LCD) and Galileo (OLED) are the Steam Deck.
    if (contains(vendor, "valve") && (contains(product, "jupiter") || contains(product, "galileo"))) return true;
    // ASUS ROG Ally (RC71L, RC72LA), Lenovo Legion Go (83E1, 83N0…), MSI Claw.
    const auto named = [&](std::string_view part) { return contains(product, part) || contains(family, part); };
    if (named("rog ally") || named("rc71l") || named("rc72l") || named("rc73")) return true;
    if (named("legion go")) return true;
    if (contains(vendor, "micro-star") && named("claw")) return true;
    return false;
}

bool detect() {
#ifdef _WIN32
    if (knownModel(biosValue(L"SystemManufacturer"), biosValue(L"SystemProductName"), biosValue(L"SystemFamily"))) return true;
    // A built-in screen this small (1280 x 800 and below) is a handheld's.
    DEVMODEW mode{}; mode.dmSize = sizeof(mode);
    if (!EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &mode)) return false;
    const auto longer = std::max(mode.dmPelsWidth, mode.dmPelsHeight), shorter = std::min(mode.dmPelsWidth, mode.dmPelsHeight);
    return longer <= 1280 && shorter <= 800 && GetSystemMetrics(SM_CMONITORS) == 1 && GetSystemMetrics(SM_CONVERTIBLESLATEMODE) == 0 &&
           GetSystemMetrics(SM_TABLETPC) != 0;
#else
    return knownModel(dmiValue("sys_vendor"), dmiValue("product_name"), dmiValue("product_family"));
#endif
}
}
