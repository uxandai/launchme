#pragma once
// GOG Galaxy's stand-in service ("GalaxyCommunication", comet's dummy-service, Apache-2.0).
// Some games' Galaxy SDK (Cuphead, among others) first wakes the Windows service Galaxy installs
// and talks to comet (gog_comet.hpp) only once it exists; without Galaxy it does not, and those
// games never connect. The stand-in does nothing but run.
//   Windows: installed once for the whole system (UAC) into Program Files, removable again.
//   Linux:   added to a GOG game's Wine/Proton prefix (its registry and drive_c) before it starts.
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>

namespace gog {
// GalaxyCommunication.exe from comet's pinned release (its dummy-service.zip), fetched once into
// <root>/tools and SHA-256 checked (the archive and the program). Empty: offline or cancelled.
std::filesystem::path prepareGalaxyService(const std::filesystem::path& root, const std::function<bool()>& cancel);

enum class GalaxyServiceState { missing, launchme, galaxy, unknown };
// Windows: whether the service exists and whose it is (Launchme's or GOG Galaxy's own).
GalaxyServiceState galaxyServiceState();
// Windows, asks for administrator rights (UAC) and waits: installs `exe` as the service, startable
// by any signed-in user. False when declined or failed; never touches GOG Galaxy's own service.
bool installGalaxyService(const std::filesystem::path& exe);
bool removeGalaxyService();

// Wine/Proton: the service in a prefix's registry (system.reg) and its program in drive_c.
bool prefixHasGalaxyService(const std::filesystem::path& prefix);
bool addGalaxyServiceToPrefix(const std::filesystem::path& prefix, const std::filesystem::path& exe);
std::string prefixServiceKey(uint64_t unix_seconds); // The registry block added (tests).
}
