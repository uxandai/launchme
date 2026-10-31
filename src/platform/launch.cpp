#include "launch.hpp"
#ifdef _WIN32
#include <shellapi.h>
#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>
#else
#include "process_spawn.hpp"
#include "text_encoding.hpp"
#include "wine_runner.hpp"
#include "../library/launch_options_store.hpp"
#include <algorithm>
#include <cerrno>
#include <cwctype>
#include <filesystem>
#include <optional>
#include <string>
#include <unistd.h>
#include <utility>
#include <vector>
#endif

namespace {
#ifdef _WIN32
thread_local HANDLE launched_process = nullptr;

std::wstring lowerExtension(const std::wstring& target) {
    std::error_code ec;
    auto extension = std::filesystem::path(target).extension().wstring();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return extension;
}

std::wstring systemPowerShell() {
    wchar_t folder[MAX_PATH]{};
    const UINT length = GetSystemDirectoryW(folder, MAX_PATH);
    if (!length || length >= MAX_PATH) return L"powershell.exe";
    return (std::filesystem::path(folder) / L"WindowsPowerShell" / L"v1.0" / L"powershell.exe").wstring();
}
#else
thread_local HANDLE launched_process = 0;
thread_local LaunchProblems launch_problems;

std::string utf8(const std::wstring& value) { return text_encoding::utf8Lenient(value); }

std::vector<std::string> splitArguments(const std::string& command_line) { return process_spawn::splitArguments(command_line); }

INT_PTR spawnError(int error) {
    switch (error) {
    case ENOENT: return launch_error::file_missing;
    case ENOTDIR: return launch_error::path_missing;
    case EACCES: case EPERM: return launch_error::denied;
    case ENOEXEC: return launch_error::no_handler;
    default: return launch_error::other;
    }
}

// A link ("steam://rungameid/1", "heroic://launch/...", "lutris:rungameid/2"): a scheme of two or
// more characters, so neither a path nor a Windows drive ("C:\") is one.
bool link(const std::wstring& target) {
    const auto colon = target.find(L':');
    if (colon == std::wstring::npos || colon < 2 || target[0] >= 128 || !std::iswalpha(target[0])) return false;
    return std::all_of(target.begin(), target.begin() + static_cast<std::ptrdiff_t>(colon), [](wchar_t ch) {
        return (ch < 128 && std::iswalnum(ch)) || ch == L'+' || ch == L'-' || ch == L'.';
    });
}

std::wstring lowerExtension(const std::filesystem::path& file) {
    auto extension = file.extension().wstring();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return extension;
}

// The action's own variables (Amazon's SDK, ...) on top of Launchme's environment; null (inherit)
// when it has none.
std::optional<std::vector<std::string>> actionEnvironment(const GameAction& action) {
    if (action.environment.empty()) return std::nullopt;
    wine_runner::Plan variables;
    for (const auto& [name, value] : action.environment)
        if (!name.empty() && name.find(L'=') == std::wstring::npos) variables.environment.emplace_back(utf8(name), utf8(value));
    return wine_runner::environmentBlock(variables, wine_runner::currentEnvironment());
}

// Hands a link, file or folder to the desktop (xdg-open), which picks the program. Waits briefly
// for its verdict, as ShellExecuteEx does with NOASYNC: a link nothing handles fails here.
INT_PTR openWithDesktop(const std::string& target, const std::vector<std::string>* environment = nullptr) {
    pid_t pid = 0;
    process_spawn::Options options;
    options.search = true; options.session = true; options.environment = environment;
    const int error = process_spawn::spawn({"xdg-open", target}, options, pid);
    if (error == ENOENT) return launch_error::no_handler;
    if (error) return spawnError(error);
    const auto code = process_spawn::waitExit(pid, std::chrono::milliseconds(1500));
    if (!code) { process_spawn::reapLater(pid); return 33; } // Still handing over: started.
    switch (*code) {
    case 0: return 33;
    case 2: return launch_error::file_missing;
    case 3: case 4: return launch_error::no_handler; // No tool, or no application for it.
    default: return launch_error::other;
    }
}

// A helper that passes the target on (steam, gio): started unless the helper is missing.
INT_PTR handOver(const std::vector<std::string>& arguments, const std::filesystem::path& directory,
                 const std::vector<std::string>* environment = nullptr) {
    pid_t pid = 0;
    process_spawn::Options options;
    options.search = true; options.session = true; options.directory = directory; options.environment = environment;
    const int error = process_spawn::spawn(arguments, options, pid);
    if (error) return error == ENOENT ? launch_error::no_handler : spawnError(error);
    process_spawn::reapLater(pid);
    return 33;
}

bool windowsProgram(const std::wstring& extension) {
    for (const auto* windows : {L".exe", L".bat", L".cmd", L".com", L".msi", L".lnk"})
        if (extension == windows) return true;
    return false;
}

// A Windows program through umu-launcher, Proton or Wine (wine_runner.hpp) with the game's options
// (GameAction::compat; without them the defaults in the shared "default" prefix). The runner (or
// the script around it) leads the session that the launch job follows.
INT_PTR startWindowsProgram(const GameAction& action, const std::filesystem::path& file, const std::vector<std::string>& arguments,
                            const std::filesystem::path& directory) {
    wine_runner::Request request;
    request.program = file;
    request.arguments = arguments;
    request.working_directory = directory;
    for (const auto& [name, value] : action.environment)
        if (!name.empty() && name.find(L'=') == std::wstring::npos) request.environment.emplace_back(utf8(name), utf8(value));
    request.launch = action.compat ? *action.compat : launch_options_store::defaultCompatLaunch();
    if (action.kind != ActionKind::play) request.purpose = wine_runner::Purpose::tool; // Installers and clients: no wrappers.
    request = wine_runner::resolveShortcut(std::move(request));
    const auto plan = wine_runner::plan(request, wine_runner::context(request.launch));
    launch_problems = {plan.error, plan.missing};
    if (!plan.error.empty()) return launch_error::no_handler; // No Proton, Wine or umu-launcher installed.
    pid_t process = 0;
    const int error = wine_runner::start(plan, process);
    if (error) return error == ENOENT ? launch_error::no_handler : spawnError(error);
    process_spawn::reapLater(process);
    launched_process = process;
    return 33;
}
#endif
}

LaunchProblems takeLaunchProblems() {
#ifdef _WIN32
    return {};
#else
    return std::exchange(launch_problems, LaunchProblems{});
#endif
}

HANDLE takeLaunchedProcess() {
#ifdef _WIN32
    const HANDLE process = launched_process;
    launched_process = nullptr;
#else
    const HANDLE process = launched_process;
    launched_process = 0;
#endif
    return process;
}

INT_PTR executeGameAction(HWND window, const GameAction& action) {
#ifdef _WIN32
    if (launched_process) { CloseHandle(launched_process); launched_process = nullptr; }
    if (!action) return launch_error::other;
    // URI launches may go through an already running Steam/store process rather than a new
    // child. Let that broker hand foreground permission to its game (or its install dialog),
    // unless Launchme keeps the foreground and focuses the game window itself.
    if (!action.keep_focus) AllowSetForegroundWindow(ASFW_ANY);
    std::wstring target = action.target, arguments = action.arguments;
    // A PowerShell script runs; its "open" verb would only show it in Notepad.
    if (lowerExtension(target) == L".ps1") {
        arguments = L"-NoProfile -ExecutionPolicy Bypass -File \"" + target + L"\"" + (arguments.empty() ? L"" : L" " + arguments);
        target = systemPowerShell();
    }
    SHELLEXECUTEINFOW info{sizeof(info)};
    // NO_UI: a link no program handles fails here (and the launcher says which client is
    // missing) instead of Windows' "find an app" prompt, which a pad cannot answer.
    // NOZONECHECKS: a game the user added (often downloaded) starts without the modal
    // "Open File - Security Warning" that would block the launcher's frames and pad input.
    info.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI | SEE_MASK_NOZONECHECKS | SEE_MASK_NOASYNC;
    info.hwnd = window;
    info.lpVerb = L"open";
    info.lpFile = target.c_str();
    info.lpParameters = arguments.empty() ? nullptr : arguments.c_str();
    const auto directory = action.working_directory.wstring();
    info.lpDirectory = directory.empty() ? nullptr : directory.c_str();
    info.nShow = action.hidden ? SW_HIDE : SW_SHOWNORMAL;
    // The action's own variables: set for the synchronous ShellExecuteEx (SEE_MASK_NOASYNC), the
    // started process inherits them, then Launchme's environment is restored.
    std::vector<std::pair<std::wstring, std::optional<std::wstring>>> previous;
    for (const auto& [name, value] : action.environment) {
        if (name.empty() || name.find(L'=') != std::wstring::npos) continue;
        std::wstring old(32767, L'\0');
        const DWORD length = GetEnvironmentVariableW(name.c_str(), old.data(), static_cast<DWORD>(old.size()));
        previous.emplace_back(name, length && length < old.size() ? std::optional<std::wstring>(old.substr(0, length)) : std::nullopt);
        SetEnvironmentVariableW(name.c_str(), value.c_str());
    }
    const BOOL started = ShellExecuteExW(&info);
    const DWORD error = GetLastError();
    for (auto it = previous.rbegin(); it != previous.rend(); ++it)
        SetEnvironmentVariableW(it->first.c_str(), it->second ? it->second->c_str() : nullptr);
    if (started) {
        launched_process = info.hProcess; // May be null: a running client took the link.
        return 33;
    }
    switch (error) {
    case ERROR_CANCELLED: return launch_error::cancelled;
    case ERROR_FILE_NOT_FOUND: return launch_error::file_missing;
    case ERROR_PATH_NOT_FOUND: case ERROR_BAD_NETPATH: case ERROR_INVALID_DRIVE: return launch_error::path_missing;
    case ERROR_ACCESS_DENIED: return launch_error::denied;
    case ERROR_NO_ASSOCIATION: case ERROR_DDE_FAIL: return launch_error::no_handler;
    default: return launch_error::other;
    }
#else
    (void)window;
    launched_process = 0; // An earlier process is the reaper's; nothing to close.
    if (!action) return launch_error::other;
    const auto& target = action.target;
    const auto& directory = action.working_directory;
    std::error_code ec;
    if (!directory.empty() && !std::filesystem::is_directory(directory, ec)) return launch_error::path_missing;
    const auto environment = actionEnvironment(action);
    const auto* variables = environment ? &*environment : nullptr;
    if (link(target)) {
        // The steam command hands the link to a running client (or starts it with the link);
        // without it (Flatpak) the desktop's steam:// handler does the same.
        if (target.starts_with(L"steam:")) {
            const auto result = handOver({"steam", utf8(target)}, {}, variables);
            if (result != launch_error::no_handler) return result;
        }
        return openWithDesktop(utf8(target), variables);
    }
    const auto arguments = splitArguments(utf8(action.arguments));
    std::filesystem::path file(target);
    const bool bare = !file.has_parent_path() && !file.is_absolute(); // A program in PATH.
    if (!bare && file.is_relative() && !directory.empty()) file = directory / file;
    if (!bare) {
        const auto status = std::filesystem::status(file, ec);
        if (!std::filesystem::exists(status)) {
            std::error_code parent;
            return std::filesystem::is_directory(file.parent_path(), parent) ? launch_error::file_missing : launch_error::path_missing;
        }
        if (std::filesystem::is_directory(status)) return openWithDesktop(file.string(), variables);
    }
    const auto extension = lowerExtension(file);
    // Windows programs: umu-launcher, Proton or Wine with the game's options. A bare name
    // ("notepad.exe") is one of the prefix's own programs.
    if (windowsProgram(extension)) return startWindowsProgram(action, bare ? std::filesystem::path(utf8(target)) : file, arguments, directory);
    // An application's .desktop entry: gio runs its Exec line.
    if (extension == L".desktop") {
        std::vector<std::string> line{"gio", "launch", file.string()};
        line.insert(line.end(), arguments.begin(), arguments.end());
        return handOver(line, directory, variables);
    }
    std::vector<std::string> line;
    bool interpreter = false;
    if (extension == L".ps1") { line = {"pwsh", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", file.string()}; interpreter = true; }
    else if (bare || ::access(file.c_str(), X_OK) == 0) line = {bare ? utf8(target) : file.string()};
    // A shell script without its executable bit still runs, as Windows runs a .bat.
    else if (extension == L".sh") line = {"/bin/sh", file.string()};
    else return openWithDesktop(file.string(), variables); // A document, picture or ROM: its default program.
    line.insert(line.end(), arguments.begin(), arguments.end());
    pid_t process = 0;
    process_spawn::Options options;
    // Its own session: the launch job (game_activity.hpp) follows the game's processes by it.
    options.search = bare || interpreter; options.session = true; options.directory = directory; options.environment = variables;
    const int error = process_spawn::spawn(line, options, process);
    // The file is there, so a missing file is its interpreter (pwsh, a #! line).
    if (error == ENOENT) return bare ? launch_error::file_missing : launch_error::no_handler;
    if (error) return spawnError(error);
    process_spawn::reapLater(process);
    launched_process = process;
    return 33;
#endif
}
