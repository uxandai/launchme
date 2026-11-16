#pragma once
// Is this PC a gaming handheld (Steam Deck, ROG Ally, Legion Go, MSI Claw, AYANEO, GPD,
// OneXPlayer, AYN)? Read once at the first start: Tryb handhelda is switched on there by itself
// (AppSettings::handheld_mode, Interfejs). The firmware's maker and model names (SMBIOS: Windows'
// registry, Linux's /sys/devices/virtual/dmi/id) decide, and a built-in screen of 1280 x 800 or
// less as a last hint.
#include <string_view>

namespace handheld {
// The maker, model and family as the firmware names them (any of them may be empty).
bool knownModel(std::string_view vendor, std::string_view product, std::string_view family);
// This computer (reads the firmware's names and the primary screen's size).
bool detect();
}
