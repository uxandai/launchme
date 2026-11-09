#pragma once
// What a user can send when something goes wrong, all in the data folder:
//   logs/launchme-app/launchme-<YYYY-MM-DD>.log
//                           starts, errors and notable events, one file a day; files of the last
//                           log_days days are kept, older ones go at the next start
//   logs/games/<game>/      a game's launches when "Zapisuj logi" is on for it (game_log.hpp)
//   crashes\*.dmp           a minidump per crash, from the unhandled-exception filter
//                           (Linux: crashes/*.txt, the signal and a backtrace)
//   crashes\unseen.txt      the last crash, reported once at the next start (takeUnseenCrash)
// Header-only: the filter and the log are usable before the Launcher exists.
#include "system.hpp"
#include "text_encoding.hpp"
#ifdef _WIN32
#include <Windows.h>
#include <DbgHelp.h>
#else
#include <csignal>
#include <cstring>
#include <execinfo.h>
#include <fcntl.h>
#include <unistd.h>
#endif

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

#ifdef _WIN32
#pragma comment(lib, "dbghelp.lib")
#pragma comment(lib, "version.lib")
#endif

namespace diagnostics {
// ProductVersion from the EXE's VERSIONINFO (CMake's project() version), e.g. "0.1.0";
// elsewhere LAUNCHME_VERSION, defined by the build from the same project() version.
inline std::wstring version() {
#ifndef _WIN32
#ifdef LAUNCHME_VERSION
    return text_encoding::wide(LAUNCHME_VERSION);
#else
    return {};
#endif
#else
    wchar_t path[MAX_PATH * 2]{};
    if (!GetModuleFileNameW(nullptr, path, static_cast<DWORD>(std::size(path)))) return {};
    DWORD ignored = 0;
    const DWORD size = GetFileVersionInfoSizeW(path, &ignored);
    if (!size) return {};
    std::wstring buffer(size / sizeof(wchar_t) + 1, L'\0');
    if (!GetFileVersionInfoW(path, 0, size, buffer.data())) return {};
    VS_FIXEDFILEINFO* info = nullptr; UINT length = 0;
    if (!VerQueryValueW(buffer.data(), L"\\", reinterpret_cast<void**>(&info), &length) || !info) return {};
    return std::to_wstring(HIWORD(info->dwProductVersionMS)) + L'.' + std::to_wstring(LOWORD(info->dwProductVersionMS)) + L'.' +
           std::to_wstring(HIWORD(info->dwProductVersionLS));
#endif
}
// Days of launchme-app logs kept (today included).
inline constexpr int log_days = 3;
namespace detail {
inline std::filesystem::path& folder() { static std::filesystem::path value; return value; }
inline std::mutex& lock() { static std::mutex value; return value; }
inline std::string utf8(std::wstring_view text) { return text_encoding::utf8Lenient(text); }
inline std::wstring stamp(bool file_name) {
    const auto now = system_info::localTime();
    wchar_t text[64]{};
    if (file_name) swprintf(text, std::size(text), L"%04d%02d%02d-%02d%02d%02d", now.year, now.month, now.day, now.hour, now.minute, now.second);
    else swprintf(text, std::size(text), L"%04d-%02d-%02d %02d:%02d:%02d.%03d", now.year, now.month, now.day, now.hour, now.minute, now.second, now.millisecond);
    return text;
}
// <data>/logs/launchme-app/launchme-<today>.log; empty before install().
inline std::filesystem::path appLog() {
    const auto& base = folder();
    if (base.empty()) return {};
    const auto now = system_info::localTime();
    wchar_t name[48]{};
    swprintf(name, std::size(name), L"launchme-%04d-%02d-%02d.log", now.year, now.month, now.day);
    return base / L"logs" / L"launchme-app" / name;
}
// No allocation-heavy work is safe in a crashing process; this only appends to a file.
inline void append(const std::filesystem::path& file, std::string_view bytes) {
#ifndef _WIN32
    const int handle = ::open(file.c_str(), O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
    if (handle < 0) return;
    [[maybe_unused]] const auto written = ::write(handle, bytes.data(), bytes.size());
    ::close(handle);
#else
    const HANDLE handle = CreateFileW(file.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                      nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    WriteFile(handle, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr);
    CloseHandle(handle);
#endif
}
#ifndef _WIN32
// Signals instead of SEH: the report is crashes/<stamp>.txt with the signal and a backtrace
// (no minidump), then the default action (a core dump where the system keeps them).
inline void crashed(int signal_number, siginfo_t* info, void*) {
    static volatile sig_atomic_t entered = 0;
    if (!entered) {
        entered = 1;
        const auto& base = folder();
        if (!base.empty()) {
            const auto crashes = base / L"crashes";
            std::error_code error;
            std::filesystem::create_directories(crashes, error);
            const auto name = L"launchme-" + stamp(true) + L".txt";
            const auto report = crashes / name;
            char code[160]{};
            snprintf(code, sizeof(code), "signal %d (%s) at %p", signal_number, strsignal(signal_number), info ? info->si_addr : nullptr);
            append(report, std::string(code) + "\n");
            if (const int handle = ::open(report.c_str(), O_WRONLY | O_APPEND | O_CLOEXEC); handle >= 0) {
                void* frames[64];
                backtrace_symbols_fd(frames, backtrace(frames, 64), handle);
                ::close(handle);
            }
            append(appLog(), utf8(stamp(false)) + " [crash] " + code + " -> crashes/" + utf8(name) + "\n");
            const auto unseen = crashes / L"unseen.txt";
            std::filesystem::remove(unseen, error);
            append(unseen, utf8(name) + "\n\n");
        }
    }
    ::signal(signal_number, SIG_DFL);
    ::raise(signal_number);
}
#else
inline std::wstring moduleOf(const void* address) {
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            static_cast<LPCWSTR>(address), &module) || !module) return {};
    wchar_t path[MAX_PATH * 2]{};
    return GetModuleFileNameW(module, path, static_cast<DWORD>(std::size(path))) ? path : L"";
}
inline LONG WINAPI unhandled(EXCEPTION_POINTERS* info) {
    static volatile LONG entered = 0;
    if (InterlockedExchange(&entered, 1)) return EXCEPTION_CONTINUE_SEARCH; // A second fault while dumping.
    const auto& base = folder();
    if (!base.empty()) {
        const auto crashes = base / L"crashes";
        CreateDirectoryW(base.c_str(), nullptr);
        CreateDirectoryW(crashes.c_str(), nullptr);
        const auto name = L"launchme-" + stamp(true) + L".dmp";
        const auto dump = crashes / name;
        const HANDLE file = CreateFileW(dump.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file != INVALID_HANDLE_VALUE) {
            MINIDUMP_EXCEPTION_INFORMATION exception{GetCurrentThreadId(), info, FALSE};
            MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file,
                static_cast<MINIDUMP_TYPE>(MiniDumpWithIndirectlyReferencedMemory | MiniDumpWithThreadInfo | MiniDumpWithUnloadedModules),
                info ? &exception : nullptr, nullptr, nullptr);
            CloseHandle(file);
        }
        const void* address = info && info->ExceptionRecord ? info->ExceptionRecord->ExceptionAddress : nullptr;
        const auto module = moduleOf(address);
        char code[160]{};
        sprintf_s(code, "code 0x%08lX at %p", info && info->ExceptionRecord ? info->ExceptionRecord->ExceptionCode : 0ul, address);
        append(appLog(), utf8(stamp(false)) + " [crash] " + code + " in " + utf8(module) + " -> crashes\\" + utf8(name) + "\r\n");
        // Read (and removed) by the next start: the dump's name, then the faulting module.
        const auto unseen = crashes / L"unseen.txt";
        DeleteFileW(unseen.c_str());
        append(unseen, utf8(name) + "\n" + utf8(module) + "\n");
    }
    return EXCEPTION_CONTINUE_SEARCH; // Windows Error Reporting still sees it.
}
#endif
}

// <data>/logs: launchme-app (Launchme's own) and games (each game's, when switched on for it).
inline std::filesystem::path logsFolder() { return detail::folder().empty() ? std::filesystem::path{} : detail::folder() / L"logs"; }
inline std::filesystem::path appLogsFolder() { const auto logs = logsFolder(); return logs.empty() ? logs : logs / L"launchme-app"; }

// Call once, early. The data folder must be known (LAUNCHME_CONFIG_DIR applied). Makes
// logs/launchme-app and removes its files older than log_days (and the single launchme.log of
// older versions).
inline void install(const std::filesystem::path& data_folder) {
    detail::folder() = data_folder;
    try {
        std::error_code error;
        const auto folder = appLogsFolder();
        std::filesystem::create_directories(folder, error);
        for (const auto* old : {L"launchme.log", L"launchme.old.log"}) std::filesystem::remove(data_folder / old, error);
        const auto cutoff = std::filesystem::file_time_type::clock::now() - std::chrono::hours(24 * log_days);
        for (std::filesystem::directory_iterator it(folder, error), end; !error && it != end; it.increment(error)) {
            const auto name = it->path().filename().wstring();
            if (!name.starts_with(L"launchme-") || it->path().extension() != L".log") continue;
            std::error_code time_error;
            if (const auto written = it->last_write_time(time_error); !time_error && written < cutoff) std::filesystem::remove(it->path(), time_error);
        }
    } catch (...) {}
#ifndef _WIN32
    struct sigaction action{};
    action.sa_sigaction = detail::crashed;
    action.sa_flags = SA_SIGINFO | SA_RESETHAND;
    for (const int signal_number : {SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT}) sigaction(signal_number, &action, nullptr);
    std::set_terminate([] { std::abort(); });
#else
    SetUnhandledExceptionFilter(detail::unhandled);
    // CRT failures (invalid parameter, pure call, std::terminate) become the same crash report.
    _set_invalid_parameter_handler([](const wchar_t*, const wchar_t*, const wchar_t*, unsigned, uintptr_t) {
        RaiseException(0xE0000001, EXCEPTION_NONCONTINUABLE, 0, nullptr);
    });
    _set_purecall_handler([] { RaiseException(0xE0000002, EXCEPTION_NONCONTINUABLE, 0, nullptr); });
    std::set_terminate([] { RaiseException(0xE0000003, EXCEPTION_NONCONTINUABLE, 0, nullptr); std::abort(); });
#endif
}

// One line in today's launchme-app log. Thread-safe; never throws.
inline void log(std::string_view level, std::wstring_view text) {
    try {
        const auto& base = detail::folder();
        if (base.empty()) return;
        std::lock_guard guard(detail::lock());
        const auto file = detail::appLog();
        std::error_code error;
        // A runaway day stops at 8 MiB (the start of the day is what tells most).
        if (const auto size = std::filesystem::file_size(file, error); !error && size > 8 * 1024 * 1024) return;
        std::filesystem::create_directories(file.parent_path(), error);
        std::string line = detail::utf8(detail::stamp(false)) + " [" + std::string(level) + "] " + detail::utf8(text);
        for (auto& ch : line) if (ch == '\r' || ch == '\n') ch = ' ';
        detail::append(file, line + "\r\n");
    } catch (...) {}
}
inline void log(std::string_view level, std::string_view text) {
    const auto wide = text_encoding::wideLenient(text);
    log(level, std::wstring_view(wide));
}
// The text of an exception caught as exception_ptr (worker failures), for the log.
inline std::string describe(std::exception_ptr error) {
    try { if (error) std::rethrow_exception(error); }
    catch (const std::exception& e) { return e.what(); }
    catch (...) { return "unknown exception"; }
    return {};
}

struct Crash { std::wstring dump, module; };
// The crash of a previous run, once: the next start tells the user where its report is.
inline std::optional<Crash> takeUnseenCrash() {
    const auto& base = detail::folder();
    if (base.empty()) return {};
    const auto file = base / L"crashes" / L"unseen.txt";
    std::FILE* input = nullptr;
#ifdef _WIN32
    if (_wfopen_s(&input, file.c_str(), L"rb") || !input) return {};
#else
    if (!(input = std::fopen(file.c_str(), "rb"))) return {};
#endif
    char buffer[4096]{};
    const size_t length = fread(buffer, 1, sizeof(buffer) - 1, input);
    fclose(input);
    std::error_code error;
    std::filesystem::remove(file, error);
    const std::string_view text(buffer, length);
    const auto line = text.find('\n');
    const auto wide = [](std::string_view part) { return text_encoding::wideLenient(part); };
    Crash crash;
    crash.dump = wide(text.substr(0, line));
    if (line != std::string_view::npos) {
        auto module = text.substr(line + 1);
        if (const auto end = module.find('\n'); end != std::string_view::npos) module = module.substr(0, end);
        crash.module = wide(module);
    }
    return crash.dump.empty() ? std::nullopt : std::optional<Crash>(crash);
}
}
