#include "gog_galaxy_service.hpp"
#include "../net/http_download.hpp"
#include "../platform/atomic_file.hpp"
#include "../platform/file_io.hpp"
#include "../platform/hashing.hpp"
#include "../platform/text_encoding.hpp"
#include <miniz.h>
#ifdef _WIN32
#include <Windows.h>
#include <shellapi.h>
#include <shlobj.h>
#endif
#include <algorithm>
#include <chrono>
#include <cwctype>
#include <span>

namespace gog {
namespace {
constexpr auto archive_url = L"https://github.com/imLinguin/comet/releases/download/v0.4.1/dummy-service.zip";
constexpr auto archive_digest = "e89e837fb33d11e4757fe4ff46255ea35d95e1ed32750ef804a28bee8ef7de53";
constexpr uint64_t archive_bytes = 36793;
constexpr auto program_digest = "9c834cfbc106bbab314c78ce3ca8b66807fa3e2f59efee62c2a5b0728ea8d1e3";
constexpr auto program_name = "GalaxyCommunication.exe";

std::string digestOf(std::span<const uint8_t> bytes) {
    hashing::Digest digest(hashing::Algorithm::sha256);
    if (!digest.update(bytes.data(), bytes.size())) return {};
    const auto value = digest.finish();
    return value ? hashing::hex(*value) : std::string{};
}

std::wstring lower(std::wstring text) {
    std::transform(text.begin(), text.end(), text.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return text;
}

#ifdef _WIN32
std::filesystem::path serviceFolder() {
    PWSTR path = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_ProgramFiles, 0, nullptr, &path))) return {};
    std::filesystem::path folder(path); CoTaskMemFree(path);
    return folder / L"Launchme Galaxy Service";
}

std::wstring quoted(const std::wstring& text) { // A PowerShell single-quoted string.
    std::wstring result = L"'";
    for (const auto c : text) { result += c; if (c == L'\'') result += L'\''; }
    return result + L"'";
}

// -EncodedCommand: the script travels in the elevated process's own command line, never in a file
// another program could change between the prompt and the run.
std::wstring encoded(const std::wstring& script) {
    static constexpr char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::vector<uint8_t> bytes;
    for (const auto c : script) { bytes.push_back(static_cast<uint8_t>(c & 0xff)); bytes.push_back(static_cast<uint8_t>(c >> 8)); }
    std::wstring out;
    for (size_t i = 0; i < bytes.size(); i += 3) {
        const uint32_t value = bytes[i] << 16 | (i + 1 < bytes.size() ? bytes[i + 1] << 8 : 0) | (i + 2 < bytes.size() ? bytes[i + 2] : 0);
        out += table[value >> 18 & 63]; out += table[value >> 12 & 63];
        out += i + 1 < bytes.size() ? table[value >> 6 & 63] : '=';
        out += i + 2 < bytes.size() ? table[value & 63] : '=';
    }
    return out;
}

bool elevated(const std::wstring& script) {
    const auto parameters = L"-NoProfile -NonInteractive -WindowStyle Hidden -EncodedCommand " + encoded(script);
    SHELLEXECUTEINFOW info{sizeof(info)};
    info.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
    info.lpVerb = L"runas";
    info.lpFile = L"powershell.exe";
    info.lpParameters = parameters.c_str();
    info.nShow = SW_HIDE;
    if (!ShellExecuteExW(&info) || !info.hProcess) return false; // Also when the prompt was declined.
    DWORD code = 1;
    const bool finished = WaitForSingleObject(info.hProcess, 60000) == WAIT_OBJECT_0 && GetExitCodeProcess(info.hProcess, &code);
    CloseHandle(info.hProcess);
    return finished && code == 0;
}
#endif
}

std::filesystem::path prepareGalaxyService(const std::filesystem::path& root, const std::function<bool()>& cancel) {
    const auto folder = root / L"tools" / L"galaxy-service-0.4.1";
    const auto program = folder / program_name;
    if (const auto bytes = file_io::readBytes(program, 1024 * 1024); !bytes.empty() && digestOf(bytes) == program_digest) return program;
    if (cancel()) return {};
    std::error_code error;
    std::filesystem::create_directories(folder, error); if (error) return {};
    const auto archive = folder / L"dummy-service.zip.part";
    const auto result = httpDownload(archive_url, archive, archive_bytes, cancel, [](uint64_t) {}, [](std::wstring_view url) {
        return url.starts_with(archive_url) || url.starts_with(L"https://release-assets.githubusercontent.com/") ||
               url.starts_with(L"https://objects.githubusercontent.com/");
    });
    if (result != DownloadError::none || cancel()) return {};
    const auto zip = file_io::readBytes(archive, 1024 * 1024);
    std::filesystem::remove(archive, error);
    if (zip.empty() || digestOf(zip) != archive_digest) return {};
    mz_zip_archive reader{};
    if (!mz_zip_reader_init_mem(&reader, zip.data(), zip.size(), 0)) return {};
    size_t size = 0;
    void* data = mz_zip_reader_extract_file_to_heap(&reader, program_name, &size, 0);
    mz_zip_reader_end(&reader);
    if (!data) return {};
    const std::vector<uint8_t> bytes(static_cast<uint8_t*>(data), static_cast<uint8_t*>(data) + size);
    mz_free(data);
    if (digestOf(bytes) != program_digest || !atomic_file::write(program, bytes)) return {};
    return program;
}

GalaxyServiceState galaxyServiceState() {
#ifdef _WIN32
    const SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!manager) return GalaxyServiceState::unknown;
    const SC_HANDLE service = OpenServiceW(manager, L"GalaxyCommunication", SERVICE_QUERY_CONFIG);
    auto state = GalaxyServiceState::unknown;
    if (!service) state = GetLastError() == ERROR_SERVICE_DOES_NOT_EXIST ? GalaxyServiceState::missing : GalaxyServiceState::unknown;
    else {
        DWORD needed = 0;
        QueryServiceConfigW(service, nullptr, 0, &needed);
        std::vector<uint8_t> buffer(needed);
        auto* config = reinterpret_cast<QUERY_SERVICE_CONFIGW*>(buffer.data());
        if (needed && QueryServiceConfigW(service, config, needed, &needed) && config->lpBinaryPathName) {
            const auto ours = lower(serviceFolder().wstring());
            state = !ours.empty() && lower(config->lpBinaryPathName).find(ours) != std::wstring::npos ? GalaxyServiceState::launchme : GalaxyServiceState::galaxy;
        }
        CloseServiceHandle(service);
    }
    CloseServiceHandle(manager);
    return state;
#else
    return GalaxyServiceState::unknown;
#endif
}

bool installGalaxyService([[maybe_unused]] const std::filesystem::path& exe) {
#ifdef _WIN32
    const auto folder = serviceFolder();
    if (folder.empty() || galaxyServiceState() != GalaxyServiceState::missing) return false;
    std::wstring hash(program_digest, program_digest + 64);
    std::transform(hash.begin(), hash.end(), hash.begin(), towupper);
    // Copied into Program Files (only administrators write there), checked there, then registered:
    // started on demand, and any signed-in user may start and stop it (a game is no administrator).
    const std::wstring script =
        L"$ErrorActionPreference='Stop'\n"
        L"if (Get-Service -Name GalaxyCommunication -ErrorAction SilentlyContinue) { exit 3 }\n"
        L"$dir=" + quoted(folder.wstring()) + L"; $dst=Join-Path $dir 'GalaxyCommunication.exe'\n"
        L"New-Item -ItemType Directory -Force -Path $dir | Out-Null\n"
        L"Copy-Item -LiteralPath " + quoted(exe.wstring()) + L" -Destination $dst -Force\n"
        L"if ((Get-FileHash -Algorithm SHA256 -LiteralPath $dst).Hash -ne '" + hash + L"') { Remove-Item -LiteralPath $dir -Recurse -Force; exit 4 }\n"
        L"New-Service -Name GalaxyCommunication -BinaryPathName ('\"' + $dst + '\"') -DisplayName 'GalaxyCommunication (Launchme)' -StartupType Manual | Out-Null\n"
        L"& sc.exe sdset GalaxyCommunication 'D:(A;;CCLCSWRPWPDTLOCRRC;;;SY)(A;;CCDCLCSWRPWPDTLOCRSDRCWDWO;;;BA)(A;;CCLCSWLOCRRC;;;IU)(A;;CCLCSWLOCRRC;;;SU)(A;;RPWPLCRC;;;AU)' | Out-Null\n"
        L"if ($LASTEXITCODE -ne 0) { exit 5 }\n"
        L"exit 0\n";
    return elevated(script) && galaxyServiceState() == GalaxyServiceState::launchme;
#else
    return false;
#endif
}

bool removeGalaxyService() {
#ifdef _WIN32
    const auto folder = serviceFolder();
    if (folder.empty() || galaxyServiceState() != GalaxyServiceState::launchme) return false;
    // Only Launchme's: GOG Galaxy's own service (another path) is never removed.
    const std::wstring script =
        L"$ErrorActionPreference='Stop'\n"
        L"$s = Get-CimInstance Win32_Service -Filter \"Name='GalaxyCommunication'\"\n"
        L"if (-not $s) { exit 0 }\n"
        L"if ($s.PathName -notlike ('*' + " + quoted(folder.wstring()) + L" + '*')) { exit 3 }\n"
        L"Stop-Service -Name GalaxyCommunication -Force -ErrorAction SilentlyContinue\n"
        L"& sc.exe delete GalaxyCommunication | Out-Null\n"
        L"Remove-Item -LiteralPath " + quoted(folder.wstring()) + L" -Recurse -Force -ErrorAction SilentlyContinue\n"
        L"exit 0\n";
    return elevated(script) && galaxyServiceState() == GalaxyServiceState::missing;
#else
    return false;
#endif
}

std::string prefixServiceKey(uint64_t now) {
    return "\n[System\\\\CurrentControlSet\\\\Services\\\\GalaxyCommunication] " + std::to_string(now) + "\n"
           "\"DisplayName\"=\"GalaxyCommunication\"\n"
           "\"ErrorControl\"=dword:00000001\n"
           "\"ImagePath\"=\"C:\\\\ProgramData\\\\GOG.com\\\\Galaxy\\\\redists\\\\GalaxyCommunication.exe\"\n"
           "\"ObjectName\"=\"LocalSystem\"\n"
           "\"Start\"=dword:00000003\n"
           "\"Type\"=dword:00000010\n";
}

bool prefixHasGalaxyService(const std::filesystem::path& prefix) {
    auto registry = file_io::readText(prefix / "system.reg", 256 * 1024 * 1024);
    std::transform(registry.begin(), registry.end(), registry.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return registry.find("[system\\\\currentcontrolset\\\\services\\\\galaxycommunication]") != std::string::npos &&
           std::filesystem::exists(prefix / "drive_c" / "ProgramData" / "GOG.com" / "Galaxy" / "redists" / program_name);
}

bool addGalaxyServiceToPrefix(const std::filesystem::path& prefix, const std::filesystem::path& exe) {
    const auto registry_file = prefix / "system.reg";
    auto registry = file_io::readText(registry_file, 256 * 1024 * 1024);
    if (registry.empty() || !registry.starts_with("WINE REGISTRY")) return false; // No prefix yet: its first start makes one.
    const auto program = file_io::readBytes(exe, 1024 * 1024);
    if (program.empty() || digestOf(program) != program_digest) return false;
    const auto target = prefix / "drive_c" / "ProgramData" / "GOG.com" / "Galaxy" / "redists" / program_name;
    std::error_code error;
    std::filesystem::create_directories(target.parent_path(), error);
    if (error || !atomic_file::write(target, program)) return false;
    auto lowered = registry;
    std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (lowered.find("[system\\\\currentcontrolset\\\\services\\\\galaxycommunication]") != std::string::npos) return true;
    if (!registry.ends_with('\n')) registry += '\n';
    registry += prefixServiceKey(static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count()));
    return atomic_file::write(registry_file, registry);
}
}
