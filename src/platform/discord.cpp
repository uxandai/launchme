#include "discord.hpp"

#ifdef _WIN32
#include <Windows.h>
#include <TlHelp32.h>
#endif

#include <algorithm>
#include <array>
#include <filesystem>
#include <unordered_set>

namespace discord {
namespace {
struct Flavor { const wchar_t* folder; const wchar_t* executable; };
constexpr std::array<Flavor, 3> flavors{{
    {L"Discord", L"Discord.exe"}, {L"DiscordPTB", L"DiscordPTB.exe"}, {L"DiscordCanary", L"DiscordCanary.exe"},
}};

bool discordExecutable(const wchar_t* name) {
    return std::any_of(flavors.begin(), flavors.end(),
                       [name](const Flavor& flavor) { return _wcsicmp(name, flavor.executable) == 0; });
}

std::unordered_set<DWORD> processes(const std::filesystem::path& program) {
    std::unordered_set<DWORD> result;
    const auto name = program.filename().wstring();
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return result;
    PROCESSENTRY32W entry{sizeof(entry)};
    for (BOOL more = Process32FirstW(snapshot, &entry); more; more = Process32NextW(snapshot, &entry))
        if (name.empty() ? discordExecutable(entry.szExeFile) : _wcsicmp(entry.szExeFile, name.c_str()) == 0) result.insert(entry.th32ProcessID);
    CloseHandle(snapshot);
    return result;
}

struct WindowSearch {
    const std::unordered_set<DWORD>* owners = nullptr;
    HWND found = nullptr;
};

BOOL CALLBACK findWindow(HWND window, LPARAM parameter) {
    auto& search = *reinterpret_cast<WindowSearch*>(parameter);
    if (!IsWindowVisible(window) || GetWindow(window, GW_OWNER) || GetWindowTextLengthW(window) == 0 ||
        (GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_TOOLWINDOW)) return TRUE;
    DWORD process = 0;
    GetWindowThreadProcessId(window, &process);
    if (!search.owners->contains(process)) return TRUE;
    search.found = window;
    return FALSE;
}
} // namespace

bool running(const std::filesystem::path& program) { return !processes(program).empty(); }

bool focus(const std::filesystem::path& program) {
    const auto owners = processes(program);
    if (owners.empty()) return false;
    WindowSearch search{&owners};
    EnumWindows(findWindow, reinterpret_cast<LPARAM>(&search));
    if (!search.found) return false;
    if (IsIconic(search.found)) ShowWindow(search.found, SW_RESTORE);
    return SetForegroundWindow(search.found) != FALSE;
}

std::optional<GameAction> launchAction() {
    wchar_t local[MAX_PATH]{};
    const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", local, MAX_PATH);
    if (length && length < MAX_PATH) {
        for (const auto& flavor : flavors) {
            const auto folder = std::filesystem::path(local) / flavor.folder;
            const auto updater = folder / L"Update.exe";
            std::error_code error;
            if (std::filesystem::is_regular_file(updater, error))
                return GameAction{updater.wstring(), std::wstring(L"--processStart ") + flavor.executable, folder, ActionKind::open_client};
        }
    }
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CLASSES_ROOT, L"discord", 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) return std::nullopt;
    const bool protocol = RegQueryValueExW(key, L"URL Protocol", nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS;
    RegCloseKey(key);
    if (!protocol) return std::nullopt;
    return GameAction{L"discord://", {}, {}, ActionKind::open_client};
}
} // namespace discord
