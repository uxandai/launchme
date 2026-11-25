#pragma once
#include "../model/installation.hpp"
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace gog {
enum class InstallState { none, loading, ready, downloading, verifying, installing, completed, cancelled, error };
// device_limit: the Microsoft Store's limit of linked devices (Xbox).
enum class InstallError { none, account, unavailable, network, manifest, storage, checksum, installer, detection, cleanup, device_limit };
struct InstallerChoice {
    std::string id;
    std::wstring language, version;
    uint64_t bytes = 0;
};
struct InstallerVariant {
    InstallerChoice choice;
    std::vector<std::wstring> downlinks; // API endpoints, not signed download URLs.
};
struct InstallerPart {
    std::wstring name, url; // URL is short-lived, backend-only, never persisted.
    std::string md5;
    uint64_t bytes = 0;
};
struct InstallRequest {
    std::string product, variant; // Empty variant requests the language/version list.
    std::filesystem::path download_root, install_directory;
    uintptr_t owner_window = 0;
};
struct InstallSnapshot {
    std::string product;
    uint64_t generation = 0;
    InstallState state = InstallState::none;
    InstallError error = InstallError::none;
    std::vector<InstallerChoice> choices;
    uint64_t done = 0, total = 0;
    bool busy() const { return state == InstallState::loading || state == InstallState::downloading || state == InstallState::verifying || state == InstallState::installing; }
    Installation progress() const {
        return {state == InstallState::downloading ? InstallPhase::downloading :
            state == InstallState::verifying ? InstallPhase::verifying :
            state == InstallState::installing ? InstallPhase::staging : InstallPhase::none,
            state == InstallState::downloading ? done : 0, state == InstallState::downloading ? total : 0, true};
    }
};
using InstallCancel = std::function<bool()>;
using InstallProgress = std::function<void(InstallState, uint64_t, uint64_t)>;
struct InstallOperations {
    // Downloads into a task-owned .part file, resuming only a confirmed range.
    std::function<InstallError(const InstallerPart&, const std::filesystem::path&, const InstallCancel&, const std::function<void(uint64_t)>&)> download;
    std::function<bool(const std::filesystem::path&, const std::string&, const InstallCancel&)> checksum;
    std::function<InstallError(const std::filesystem::path&, const InstallRequest&, const InstallCancel&)> execute;
    std::function<bool(const InstallRequest&)> detect;
};
bool installerFilename(std::wstring_view name);
bool installerDownloadUrl(std::wstring_view url);
std::optional<InstallerPart> parseInstallerChecksum(std::string_view xml, std::wstring url);
std::optional<std::vector<InstallerVariant>> parseInstallers(std::string_view json, const std::string& product);
InstallOperations nativeInstallOperations();
InstallError runOfflineInstaller(const InstallRequest&, const std::vector<InstallerPart>&,
    const InstallOperations&, const InstallCancel&, const InstallProgress&);
std::wstring installStatusText(const InstallSnapshot&);
std::wstring silentInstallerArguments(const std::filesystem::path& directory);
}
