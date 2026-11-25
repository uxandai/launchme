#include "process_freeze.hpp"
#include "atomic_file.hpp"
#include "process_presence.hpp"

#ifdef _WIN32
#include <Windows.h>
#else
#include "proc_fs.hpp"
#include <cerrno>
#include <csignal>
#include <unistd.h>
#endif

#include <algorithm>
#include <fstream>
#include <utility>

#ifdef _WIN32
namespace {
using ProcessCall = LONG(NTAPI*)(HANDLE);
// Undocumented but stable since Windows XP; used by Resource Monitor and PsSuspend.
ProcessCall ntdll(const char* name) {
    const HMODULE module = GetModuleHandleW(L"ntdll.dll");
    return module ? reinterpret_cast<ProcessCall>(GetProcAddress(module, name)) : nullptr;
}
struct Handle {
    HANDLE value = nullptr;
    ~Handle() { if (value) CloseHandle(value); }
};
enum class Open { ok, gone, denied };
// Opens the exact process identity: a PID reused by another program counts as gone.
Open open(const GameProcess& process, DWORD access, Handle& handle) {
    handle.value = OpenProcess(access | SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, process.pid);
    if (!handle.value) return GetLastError() == ERROR_ACCESS_DENIED && !process_presence::exited(process.pid) ? Open::denied : Open::gone;
    const auto wait = WaitForSingleObject(handle.value, 0);
    if (wait == WAIT_OBJECT_0) return Open::gone;
    FILETIME created{}, exited{}, kernel{}, user{};
    if (!GetProcessTimes(handle.value, &created, &exited, &kernel, &user))
        return process_presence::exited(process.pid) ? Open::gone : Open::denied;
    const auto ticks = (static_cast<uint64_t>(created.dwHighDateTime) << 32) | created.dwLowDateTime;
    if (ticks != process.created) return Open::gone;
    return wait == WAIT_TIMEOUT ? Open::ok : Open::denied;
}
}
#else
namespace {
enum class Open { ok, gone, denied };
// The exact process identity: a PID reused by another program (another start time) counts as gone.
Open open(const GameProcess& process) {
    const auto stat = proc_fs::stat(process.pid);
    if (!stat || stat->ended() || stat->start != process.created) return Open::gone;
    if (::kill(static_cast<pid_t>(process.pid), 0) == 0) return Open::ok;
    return errno == ESRCH ? Open::gone : Open::denied;
}
// `signal` to each process that is still the same one; results as on Windows.
process_freeze::Result signalAll(const std::vector<GameProcess>& processes, int signal) {
    bool done = false, denied = false;
    for (const auto& process : processes) {
        const auto state = open(process);
        if (state == Open::gone) continue;
        if (state == Open::denied || ::kill(static_cast<pid_t>(process.pid), signal) != 0) {
            if (state == Open::denied || errno != ESRCH) denied = true;
        } else done = true;
    }
    return denied ? process_freeze::Result::denied : done ? process_freeze::Result::ok : process_freeze::Result::gone;
}
// The processes and every process below them (a launcher stub's game, a script's program):
// stopping only the parent would leave the game itself running.
std::vector<GameProcess> withDescendants(const std::vector<GameProcess>& processes) {
    std::vector<GameProcess> tree = processes;
    std::vector<proc_fs::Stat> all;
    for (const auto pid : proc_fs::processes())
        if (const auto stat = proc_fs::stat(pid); stat && !stat->ended() && stat->start) all.push_back(*stat);
    const auto self = static_cast<uint32_t>(::getpid());
    // Children only of the processes that still are the ones recorded (same start time): a game
    // that ended may have its PID taken by another program, whose children are not the game's.
    // Never Launchme itself, whatever the tree says.
    const auto same = [&](const GameProcess& process) {
        return process.pid != self && std::any_of(all.begin(), all.end(), [&](const proc_fs::Stat& stat) { return stat.pid == process.pid && stat.start == process.created; });
    };
    for (size_t at = 0; at < tree.size() && tree.size() < 64; ++at) {
        if (!same(tree[at])) continue;
        for (const auto& stat : all)
            if (stat.parent == tree[at].pid && stat.pid != self && tree.size() < 64 &&
                std::none_of(tree.begin(), tree.end(), [&](const GameProcess& known) { return known.pid == stat.pid; }))
                tree.push_back({stat.pid, stat.start});
    }
    return tree;
}
}
#endif

namespace process_freeze {
#ifdef _WIN32
Result suspend(std::vector<GameProcess>& processes) {
    const auto call = ntdll("NtSuspendProcess"), undo = ntdll("NtResumeProcess");
    if (!call || !undo) return Result::denied;
    struct Held { std::vector<HANDLE> values; ~Held() { for (auto value : values) CloseHandle(value); } } held;
    std::vector<GameProcess> suspended;
    for (const auto& process : processes) {
        Handle handle;
        const auto state = open(process, PROCESS_SUSPEND_RESUME, handle);
        if (state == Open::gone) continue;
        // Anti-cheat drivers may strip PROCESS_SUSPEND_RESUME from an opened handle.
        if (state == Open::denied || call(handle.value) < 0) {
            for (auto value : held.values) undo(value);
            return Result::denied;
        }
        held.values.push_back(std::exchange(handle.value, nullptr));
        suspended.push_back(process);
    }
    if (suspended.empty()) return Result::gone;
    processes = std::move(suspended);
    return Result::ok;
}

Result resume(const std::vector<GameProcess>& processes) {
    const auto call = ntdll("NtResumeProcess");
    if (!call) return Result::denied;
    bool resumed = false, denied = false;
    for (const auto& process : processes) {
        Handle handle;
        const auto state = open(process, PROCESS_SUSPEND_RESUME, handle);
        if (state == Open::gone) continue;
        if (state == Open::denied || call(handle.value) < 0) denied = true;
        else resumed = true;
    }
    return denied ? Result::denied : resumed ? Result::ok : Result::gone;
}

Result terminate(const std::vector<GameProcess>& processes) {
    bool ended = false, denied = false;
    for (const auto& process : processes) {
        Handle handle;
        const auto state = open(process, PROCESS_TERMINATE, handle);
        if (state == Open::gone) continue;
        if (state == Open::denied || !TerminateProcess(handle.value, 0)) denied = true;
        else ended = true;
    }
    return denied ? Result::denied : ended ? Result::ok : Result::gone;
}

bool alive(const std::vector<GameProcess>& processes) {
    return std::any_of(processes.begin(), processes.end(), [](const GameProcess& process) {
        Handle handle;
        return open(process, 0, handle) != Open::gone;
    });
}

bool focus(const std::vector<GameProcess>& processes) {
    std::vector<GameProcess> live;
    for (const auto& process : processes) {
        Handle handle;
        if (open(process, 0, handle) == Open::ok && WaitForSingleObject(handle.value, 0) == WAIT_TIMEOUT) live.push_back(process);
    }
    if (live.empty()) return false;
    struct Search { const std::vector<GameProcess>* processes; HWND best = nullptr; long long area = 0; } search{&live};
    // Only non-blocking window queries: a window of a still-suspended process must not hang the UI.
    EnumWindows([](HWND window, LPARAM parameter) -> BOOL {
        auto& search = *reinterpret_cast<Search*>(parameter);
        DWORD pid = 0;
        GetWindowThreadProcessId(window, &pid);
        if (!IsWindowVisible(window) || GetWindow(window, GW_OWNER) || (GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) ||
            std::none_of(search.processes->begin(), search.processes->end(), [&](const GameProcess& process) { return process.pid == pid; }))
            return TRUE;
        RECT bounds{};
        WINDOWPLACEMENT placement{sizeof(placement)};
        if (IsIconic(window) && GetWindowPlacement(window, &placement)) bounds = placement.rcNormalPosition;
        else GetWindowRect(window, &bounds);
        const long long area = static_cast<long long>(bounds.right - bounds.left) * (bounds.bottom - bounds.top);
        if (area > search.area || !search.best) { search.area = area; search.best = window; }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&search));
    if (!search.best) return false;
    DWORD pid = 0;
    GetWindowThreadProcessId(search.best, &pid);
    AllowSetForegroundWindow(pid);
    if (IsIconic(search.best)) ShowWindowAsync(search.best, SW_RESTORE);
    SetForegroundWindow(search.best);
    const auto foreground = GetForegroundWindow();
    return foreground == search.best || (foreground && GetAncestor(foreground, GA_ROOTOWNER) == search.best);
}
#else
// SIGSTOP stops every thread of a process; it cannot be caught or ignored.
Result suspend(std::vector<GameProcess>& processes) {
    std::vector<GameProcess> suspended;
    for (const auto& process : withDescendants(processes)) {
        const auto state = open(process);
        if (state == Open::gone) continue;
        if (state == Open::denied || ::kill(static_cast<pid_t>(process.pid), SIGSTOP) != 0) {
            if (state == Open::ok && errno == ESRCH) continue; // Exited in between.
            for (const auto& held : suspended) ::kill(static_cast<pid_t>(held.pid), SIGCONT);
            return Result::denied;
        }
        suspended.push_back(process);
    }
    if (suspended.empty()) return Result::gone;
    processes = std::move(suspended);
    return Result::ok;
}

Result resume(const std::vector<GameProcess>& processes) { return signalAll(processes, SIGCONT); }

// SIGKILL, as TerminateProcess: it also ends a stopped process.
Result terminate(const std::vector<GameProcess>& processes) { return signalAll(processes, SIGKILL); }

bool alive(const std::vector<GameProcess>& processes) {
    return std::any_of(processes.begin(), processes.end(), [](const GameProcess& process) { return open(process) != Open::gone; });
}

// Raising another program's window needs the display server (X11, Wayland activation), which
// is the UI's: false lets the caller bring the game forward its own way.
bool focus(const std::vector<GameProcess>&) { return false; }
#endif

std::optional<std::vector<FrozenGame>> load(const std::filesystem::path& file) {
    std::error_code error;
    if (!std::filesystem::exists(file, error)) {
        if (error) return {};
        return std::vector<FrozenGame>{};
    }
    std::ifstream input(file, std::ios::binary | std::ios::ate);
    if (!input || input.tellg() < 0 || input.tellg() > 64 * 1024) return {};
    std::string text(static_cast<size_t>(input.tellg()), '\0');
    input.seekg(0);
    if (!input.read(text.data(), static_cast<std::streamsize>(text.size()))) return {};
    bool complete = false;
    auto games = parseFrozenGames(text, &complete);
    if (!complete) return {};
    return games;
}

bool save(const std::filesystem::path& file, const std::vector<FrozenGame>& games) {
    if (std::none_of(games.begin(), games.end(), [](const FrozenGame& game) { return !game.processes.empty(); })) {
        std::error_code error;
        std::filesystem::remove(file, error);
        return !error;
    }
    const auto text = formatFrozenGames(games);
    // Never acknowledge a journal that the bounded reader cannot restore fully.
    if (text.size() > 64 * 1024 || parseFrozenGames(text) != games) return false;
    return atomic_file::write(file, text);
}
}
