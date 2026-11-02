#pragma once
#include "../i18n/i18n.hpp"
#include <cstdint>
#include <optional>
#include <string>

enum class InstallPhase { none, queued, downloading, allocating, staging, verifying, paused, unknown };
struct Installation {
    InstallPhase phase = InstallPhase::none;
    uint64_t done = 0, total = 0;
    bool live = false;
    // An update (or validation) of a game that was already installed, not a new installation.
    bool update = false;
    bool aurelia = false; // Downloaded by Aurelia, not the Steam client ("Pobieranie w Aurelii").
    bool active() const { return phase != InstallPhase::none; }
    bool moving() const { return active() && phase != InstallPhase::paused && phase != InstallPhase::queued; }
    std::optional<int> percent() const {
        if (!total || done > total || (!done && !live)) return {};
        return static_cast<int>(100.0L * done / total);
    }
    bool operator==(const Installation&) const = default;
};
inline const wchar_t* installationLabel(InstallPhase phase) {
    switch (phase) {
    case InstallPhase::queued: return tr(L"W kolejce");
    case InstallPhase::downloading: return tr(L"Pobieranie");
    case InstallPhase::allocating: return tr(L"Przygotowanie");
    case InstallPhase::staging: return tr(L"Instalowanie");
    case InstallPhase::verifying: return tr(L"Weryfikacja");
    case InstallPhase::paused: return tr(L"Wstrzymano");
    case InstallPhase::unknown: return tr(L"Stan niedostępny");
    default: return L"";
    }
}
inline std::wstring installationText(const Installation& installation) {
    auto text = std::wstring(installationLabel(installation.phase));
    if (const auto percent = installation.percent()) text += L" · " + std::to_wstring(*percent) + L"%";
    return text;
}
