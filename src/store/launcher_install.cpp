#include "launcher_install.hpp"
#include "../platform/helper_process.hpp"
#include "../platform/text_encoding.hpp"

#include <chrono>
#include <cstdlib>
#include <thread>
#ifdef _WIN32
#include <Windows.h>
#include <shellapi.h>
#include <SoftPub.h>
#include <WinTrust.h>
#pragma comment(lib, "wintrust.lib")
#else
#include "../library/compat_tools.hpp"
#include "../library/launch_options_store.hpp"
#include "../platform/process_spawn.hpp"
#include "../platform/wine_runner.hpp"
#endif

namespace launcher_install {
namespace {
constexpr size_t max_installer_bytes = 1024ull * 1024 * 1024;

std::optional<unsigned long> runTool(const std::filesystem::path& program, const std::vector<std::wstring>& arguments, const std::function<bool()>& cancel) {
    const auto result = helper_process::run(program, {}, arguments, {}, cancel);
    if (!result.started) return std::nullopt;
    return result.exit_code;
}

bool downloadFile(const std::wstring& url, const std::filesystem::path& file, const std::function<bool()>& cancel) {
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    std::filesystem::remove(file, ec); // Always the vendor's current build, never an old download.
    // Each call streams for up to five minutes and resumes the partial file.
    for (int attempt = 0; attempt < 6 && !(cancel && cancel()); ++attempt) {
        const auto reply = httpDownloadFile(url, L"", file, max_installer_bytes, 5 * 60 * 1000, false);
        if (reply.status == 200) return true;
        if (reply.status >= 400 && reply.status < 500) return false;
    }
    return false;
}

#ifdef _WIN32
std::filesystem::path wingetProgram() {
    wchar_t local[32768];
    const auto size = GetEnvironmentVariableW(L"LOCALAPPDATA", local, static_cast<DWORD>(std::size(local)));
    if (!size || size >= std::size(local)) return {};
    const auto file = std::filesystem::path(std::wstring(local, size)) / L"Microsoft" / L"WindowsApps" / L"winget.exe";
    return GetFileAttributesW(file.c_str()) != INVALID_FILE_ATTRIBUTES ? file : std::filesystem::path{};
}

bool signedByTrustedPublisher(const std::filesystem::path& file) {
    WINTRUST_FILE_INFO info{};
    info.cbStruct = sizeof(info);
    info.pcwszFilePath = file.c_str();
    WINTRUST_DATA data{};
    data.cbStruct = sizeof(data);
    data.dwUIChoice = WTD_UI_NONE;
    data.fdwRevocationChecks = WTD_REVOKE_WHOLECHAIN;
    data.dwUnionChoice = WTD_CHOICE_FILE;
    data.pFile = &info;
    data.dwStateAction = WTD_STATEACTION_VERIFY;
    data.dwProvFlags = WTD_CACHE_ONLY_URL_RETRIEVAL; // Revocation from what Windows has cached: no hang offline.
    GUID policy = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    const auto status = WinVerifyTrust(static_cast<HWND>(INVALID_HANDLE_VALUE), &policy, &data);
    data.dwStateAction = WTD_STATEACTION_CLOSE;
    WinVerifyTrust(static_cast<HWND>(INVALID_HANDLE_VALUE), &policy, &data);
    return status == ERROR_SUCCESS;
}

bool openInstaller(const std::filesystem::path& file) {
    return reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", file.c_str(), nullptr, file.parent_path().c_str(), SW_SHOWNORMAL)) > 32;
}
#else
std::filesystem::path onPath(const char* name) {
    const char* value = std::getenv("PATH");
    const std::string path(value ? value : "");
    for (size_t at = 0; at <= path.size();) {
        const auto end = std::min(path.find(':', at), path.size());
        const auto file = std::filesystem::path(path.substr(at, end - at)) / name;
        std::error_code ec;
        if (end > at && std::filesystem::is_regular_file(file, ec)) return file;
        at = end + 1;
    }
    return {};
}

CompatLaunch prefixLaunch(const launchers::Entry& entry, const std::filesystem::path& prefix) {
    auto launch = launch_options_store::defaultCompatLaunch();
    launch.game_key = "launcher:" + std::string(entry.id);
    launch.title = std::wstring(entry.name);
    launch.prefix = prefix;
    launch.options.compat_tool = launch_options_store::effectiveTool({}, prefix);
    return launch;
}

std::optional<unsigned long> runInWine(const launchers::Entry& entry, const std::filesystem::path& file, const std::filesystem::path& prefix, const std::function<bool()>& cancel) {
    pid_t pid = 0;
    if (wine_runner::runInPrefix(prefixLaunch(entry, prefix), file, {}, pid) != 0 || pid <= 0) return std::nullopt;
    while (true) {
        if (const auto code = process_spawn::waitExit(pid, std::chrono::milliseconds(500))) return static_cast<unsigned long>(*code);
        if (cancel && cancel()) { process_spawn::killGroup(pid); return 1ul; }
    }
}
#endif
}

Backend systemBackend() {
    Backend backend;
    backend.run = runTool;
    backend.download = downloadFile;
#ifdef _WIN32
    backend.winget = wingetProgram();
    backend.trusted = signedByTrustedPublisher;
    backend.open = openInstaller;
#else
    backend.flatpak = onPath("flatpak");
    backend.wine = runInWine;
#endif
    return backend;
}

launchers::Machine machine() {
    launchers::Machine machine;
#ifdef _WIN32
    machine.windows = true;
    machine.winget = !wingetProgram().empty();
#else
    machine.windows = false;
    machine.flatpak = !onPath("flatpak").empty();
    machine.wine = compat_tools::automatic(compat_tools::discover()).has_value();
#endif
    return machine;
}

std::filesystem::path installerFile(const std::filesystem::path& config, const launchers::Entry& entry) {
    return config / L"store-hub" / L"installers" / std::wstring(entry.file);
}

Result uninstall(const launchers::Entry& entry, launchers::Removal removal, const Backend& backend, const std::function<bool()>& cancel) {
    Result result;
    const bool winget = removal == launchers::Removal::winget;
    const auto& program = winget ? backend.winget : backend.flatpak;
    if (removal == launchers::Removal::none || program.empty() || !backend.run) return result;
    result.method = winget ? launchers::Method::winget : launchers::Method::flatpak;
    const auto code = backend.run(program, winget ? launchers::wingetUninstallArguments(entry) : launchers::flatpakUninstallArguments(entry), cancel);
    result.exit_code = code.value_or(0);
    result.error = cancel && cancel() ? Error::cancelled : !code ? Error::start : *code == 0 ? Error::none : Error::failed;
    return result;
}

Result install(const Request& request, const Backend& backend, const std::function<bool()>& cancel, const std::function<void(Phase)>& phase) {
    Result result;
    if (!request.entry) return result;
    const auto& entry = *request.entry;
    const auto cancelled = [&] { return cancel && cancel(); };
    const auto report = [&](Phase next) { if (phase) phase(next); };
    // The vendor's installer, downloaded fresh: its address must be one of the catalogue's hosts.
    const auto fetch = [&](std::filesystem::path& file) {
        if (entry.installer.empty() || entry.file.empty() || !launchers::trustedInstallerUrl(entry.installer)) return Error::untrusted;
        file = installerFile(request.config, entry);
        report(Phase::downloading);
        if (!backend.download || !backend.download(std::wstring(entry.installer), file, cancel)) return cancelled() ? Error::cancelled : Error::network;
        return Error::none;
    };
    for (const auto method : request.methods) {
        if (method == launchers::Method::page) continue;
        if (cancelled()) { result.error = Error::cancelled; return result; }
        result = {};
        result.method = method;
        if (method == launchers::Method::winget) {
            if (backend.winget.empty() || entry.winget.empty() || !backend.run) continue;
            report(Phase::installing);
            const auto code = backend.run(backend.winget, launchers::wingetArguments(entry), cancel);
            if (code && (*code == 0 || launchers::wingetAlreadyInstalled(*code))) { result.error = Error::none; return result; }
            result.error = cancelled() ? Error::cancelled : code ? Error::failed : Error::start;
            result.exit_code = code.value_or(0);
        } else if (method == launchers::Method::installer) {
            std::filesystem::path file;
            result.error = fetch(file);
            if (result.error == Error::none) {
                report(Phase::verifying);
                std::error_code ec;
                if (!backend.trusted || !backend.trusted(file)) { std::filesystem::remove(file, ec); result.error = Error::signature; }
                else if (!backend.open || !backend.open(file)) result.error = Error::start;
                else { report(Phase::waiting); result.handed_over = true; return result; }
            }
        } else if (method == launchers::Method::flatpak) {
            if (backend.flatpak.empty() || entry.flatpak.empty() || !backend.run) continue;
            report(Phase::installing);
            result.error = Error::none;
            for (const auto& command : launchers::flatpakCommands(entry)) {
                const auto code = backend.run(backend.flatpak, command, cancel);
                if (code && *code == 0) continue;
                result.error = cancelled() ? Error::cancelled : code ? Error::failed : Error::start;
                result.exit_code = code.value_or(0);
                break;
            }
            if (result.ok()) return result;
        } else if (method == launchers::Method::wine) {
            if (!backend.wine || request.prefix.empty() || !request.prefix.is_absolute()) continue;
            std::filesystem::path file;
            result.error = fetch(file);
            if (result.error == Error::none) {
                report(Phase::installing);
                // The installer's own exit code says little under Wine (many end with 1 after a
                // good install): the launcher is looked for in the prefix afterwards.
                const auto code = backend.wine(entry, file, request.prefix, cancel);
                result.exit_code = code.value_or(0);
                result.error = cancelled() ? Error::cancelled : code ? Error::none : Error::start;
                if (result.ok()) return result;
            }
        }
        if (result.error == Error::cancelled) return result;
    }
    return result;
}
}
