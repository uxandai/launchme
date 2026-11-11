#pragma once
// Installs a launcher of the catalogue (launchers.hpp) by the first method that works:
//   winget     `winget install --id <id> --exact --silent …`, waited for; winget checks the hash and
//              raises its own elevation prompt;
//   installer  Windows: the vendor's installer downloaded to <config>/store-hub/installers, refused
//              unless Windows trusts its Authenticode signature, then opened with its own window
//              (the user finishes it there);
//   flatpak    Linux: the user's Flathub remote, then `flatpak install --user --noninteractive`;
//   wine       Linux: the vendor's installer run in the chosen prefix and waited for.
// Blocking: a worker's. Every outside effect goes through Backend, so tests run the whole flow
// without a network, a process or a file outside their folder.
#include "launchers.hpp"
#include "../net/http.hpp"

#include <functional>

namespace launcher_install {
enum class Phase { downloading, verifying, installing, waiting };
enum class Error { none, cancelled, unavailable, network, untrusted, signature, start, failed };

struct Request {
    const launchers::Entry* entry = nullptr;
    std::vector<launchers::Method> methods; // In order; Method::page is not an installation and is passed over.
    std::filesystem::path config;           // Launchme's data folder.
    std::filesystem::path prefix;           // Method::wine: the prefix to install into.
};
struct Result {
    Error error = Error::unavailable;
    launchers::Method method = launchers::Method::page; // The one that ran last.
    unsigned long exit_code = 0;                        // A tool's own, when it failed.
    bool handed_over = false;                           // Its own installer window is open: the user finishes there.
    bool ok() const { return error == Error::none; }
};

struct Backend {
    std::filesystem::path winget, flatpak; // Empty: not installed.
    // Runs a tool hidden and waits; nullopt when it could not start, else its exit code.
    std::function<std::optional<unsigned long>(const std::filesystem::path& program, const std::vector<std::wstring>& arguments, const std::function<bool()>& cancel)> run;
    // Downloads to `file` (resuming a partial one); true when it is complete.
    std::function<bool(const std::wstring& url, const std::filesystem::path& file, const std::function<bool()>& cancel)> download;
    std::function<bool(const std::filesystem::path& file)> trusted;   // Windows: a valid Authenticode signature.
    std::function<bool(const std::filesystem::path& file)> open;      // Windows: starts the installer with its window.
    // Linux: runs it in the prefix and waits; nullopt when it could not start, else its exit code.
    std::function<std::optional<unsigned long>(const launchers::Entry& entry, const std::filesystem::path& file, const std::filesystem::path& prefix, const std::function<bool()>& cancel)> wine;
};
Backend systemBackend();
// What this machine can install with (blocking: it looks for winget, flatpak and a Wine or Proton).
launchers::Machine machine();

Result install(const Request& request, const Backend& backend, const std::function<bool()>& cancel, const std::function<void(Phase)>& phase);
// Removes an installed launcher with winget or flatpak, waiting for it.
Result uninstall(const launchers::Entry& entry, launchers::Removal removal, const Backend& backend, const std::function<bool()>& cancel);
// <config>/store-hub/installers/<file>.
std::filesystem::path installerFile(const std::filesystem::path& config, const launchers::Entry& entry);
}
