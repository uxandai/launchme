#include "launch_scripts.hpp"
#include "text_encoding.hpp"

#include <algorithm>
#include <array>
#include <cwctype>
#include <thread>
#include <utility>
#include <vector>

namespace launch_scripts {
namespace {
constexpr size_t max_output = 64 * 1024;

std::wstring_view trimmed(std::wstring_view text) {
    while (!text.empty() && std::iswspace(text.front())) text.remove_prefix(1);
    while (!text.empty() && std::iswspace(text.back())) text.remove_suffix(1);
    return text;
}
wchar_t lower(wchar_t ch) { return ch >= L'A' && ch <= L'Z' ? static_cast<wchar_t>(ch - L'A' + L'a') : ch; }
bool absolutePath(std::wstring_view text) {
    return (text.size() > 2 && std::iswalpha(text[0]) && text[1] == L':' && (text[2] == L'\\' || text[2] == L'/')) ||
           text.starts_with(L"\\\\");
}
// An unquoted absolute path with spaces at the start of the command: its length up to the first
// program's or script's extension that ends a word, or 0.
size_t spacedPath(std::wstring_view command) {
    if (!absolutePath(command)) return 0;
    static constexpr std::array<std::wstring_view, 10> extensions{L".exe", L".bat", L".cmd", L".ps1", L".vbs", L".js", L".lnk", L".msi", L".py", L".com"};
    for (size_t at = 0; at < command.size(); ++at) {
        if (command[at] != L'.') continue;
        for (const auto extension : extensions) {
            if (at + extension.size() > command.size()) continue;
            bool same = true;
            for (size_t i = 0; i < extension.size() && same; ++i) same = lower(command[at + i]) == extension[i];
            const size_t end = at + extension.size();
            if (same && (end == command.size() || command[end] == L' ' || command[end] == L'\t'))
                return command.substr(0, end).find(L' ') == std::wstring_view::npos ? 0 : end;
        }
    }
    return 0;
}
std::wstring singleQuoted(std::wstring_view text) {
    std::wstring result = L"'";
    for (const wchar_t ch : text) { result += ch; if (ch == L'\'') result += L'\''; }
    return result + L'\'';
}
}

const char* phaseName(Phase phase) {
    switch (phase) {
    case Phase::before: return "before";
    case Phase::started: return "started";
    default: return "exited";
    }
}

std::wstring powershellScript(std::wstring_view typed) {
    const auto command = trimmed(typed);
    std::wstring body;
    if (command.starts_with(L'"') || command.starts_with(L'\'')) body = L"& " + std::wstring(command);
    else if (const size_t path = spacedPath(command)) body = L"& " + singleQuoted(command.substr(0, path)) + std::wstring(command.substr(path));
    else body = command;
    // UTF-8 output (read back by Launchme), no progress records; a failed last command keeps its code.
    return L"$ProgressPreference = 'SilentlyContinue'\n"
           L"try { [Console]::OutputEncoding = [System.Text.Encoding]::UTF8 } catch {}\n" +
           body +
           L"\n$launchmeOk = $?\n"
           L"if (-not $launchmeOk) { if ($LASTEXITCODE) { exit $LASTEXITCODE } else { exit 1 } }\n";
}

std::wstring encodedCommand(std::wstring_view script) {
    std::vector<uint8_t> bytes; // UTF-16LE (wchar_t is UTF-32 on Linux).
    const auto unit = [&](uint32_t value) { bytes.push_back(static_cast<uint8_t>(value & 0xFF)); bytes.push_back(static_cast<uint8_t>(value >> 8)); };
    for (const wchar_t ch : script) {
        const auto code = static_cast<uint32_t>(ch);
        if (code > 0xFFFF) { unit(0xD800 + ((code - 0x10000) >> 10)); unit(0xDC00 + ((code - 0x10000) & 0x3FF)); }
        else unit(code);
    }
    static constexpr wchar_t alphabet[] = L"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::wstring result;
    for (size_t at = 0; at < bytes.size(); at += 3) {
        const uint32_t chunk = (bytes[at] << 16) | ((at + 1 < bytes.size() ? bytes[at + 1] : 0) << 8) | (at + 2 < bytes.size() ? bytes[at + 2] : 0);
        result += alphabet[(chunk >> 18) & 63];
        result += alphabet[(chunk >> 12) & 63];
        result += at + 1 < bytes.size() ? alphabet[(chunk >> 6) & 63] : L'=';
        result += at + 2 < bytes.size() ? alphabet[chunk & 63] : L'=';
    }
    return result;
}

std::wstring lastLine(std::string_view output) {
    const auto text = text_encoding::wideLenient(output);
    std::wstring_view rest = text;
    while (!rest.empty()) {
        const auto end = rest.find_last_of(L"\r\n");
        const auto line = trimmed(end == std::wstring_view::npos ? rest : rest.substr(end + 1));
        if (!line.empty()) {
            std::wstring result(line.substr(0, 160));
            if (line.size() > 160) result += L"…";
            return result;
        }
        if (end == std::wstring_view::npos) break;
        rest = rest.substr(0, end);
    }
    return {};
}
}

#ifdef _WIN32
#include <Windows.h>

#include <map>

namespace launch_scripts {
namespace {
struct Handle {
    HANDLE h = nullptr;
    explicit Handle(HANDLE value = nullptr) : h(value) {}
    ~Handle() { if (h && h != INVALID_HANDLE_VALUE) CloseHandle(h); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    explicit operator bool() const { return h && h != INVALID_HANDLE_VALUE; }
};
std::wstring upper(std::wstring text) {
    std::transform(text.begin(), text.end(), text.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towupper(c)); });
    return text;
}
std::wstring variable(const wchar_t* name) {
    const DWORD size = GetEnvironmentVariableW(name, nullptr, 0);
    if (!size) return {};
    std::wstring value(size, L'\0');
    value.resize(GetEnvironmentVariableW(name, value.data(), size));
    return value;
}
}

Result run(std::wstring_view command, const Context& context, std::chrono::milliseconds timeout, const std::function<bool()>& cancel) {
    Result result;
    if (trimmed(command).empty()) return result;
    // Output to a temporary file the script (and whatever it leaves running) may keep writing to: no
    // pipe to fill up or break. Deleted once the last handle closes.
    wchar_t folder[MAX_PATH + 1]{}, name[MAX_PATH + 1]{};
    if (!GetTempPathW(MAX_PATH, folder) || !GetTempFileNameW(folder, L"lms", 0, name)) return result;
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    Handle output(CreateFileW(name, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, &sa, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr));
    if (!output) { DeleteFileW(name); return result; }
    Handle input(CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr));
    if (!input) return result;
    std::map<std::wstring, std::pair<std::wstring, std::wstring>> variables; // UPPER name → (name, value)
    if (auto* original = GetEnvironmentStringsW()) {
        for (const wchar_t* p = original; *p; p += wcslen(p) + 1) {
            const std::wstring entry(p);
            const auto pos = entry.find(L'=', 1);
            if (pos == std::wstring::npos) continue;
            variables[upper(entry.substr(0, pos))] = {entry.substr(0, pos), entry.substr(pos + 1)};
        }
        FreeEnvironmentStringsW(original);
    }
    const auto set = [&](const std::wstring& key, std::wstring value) { variables[upper(key)] = {key, std::move(value)}; };
    set(L"LAUNCHME_GAME_NAME", context.game_name);
    set(L"LAUNCHME_GAME_KEY", text_encoding::wideLenient(context.game_key));
    set(L"LAUNCHME_SCRIPT_PHASE", text_encoding::wideLenient(phaseName(context.phase)));
    std::vector<wchar_t> block;
    for (const auto& [key, entry] : variables) {
        (void)key;
        const auto line = entry.first + L'=' + entry.second;
        block.insert(block.end(), line.begin(), line.end());
        block.push_back(0);
    }
    block.push_back(0);
    wchar_t system[MAX_PATH + 1]{};
    const UINT length = GetSystemDirectoryW(system, MAX_PATH);
    const std::wstring shell = std::wstring(system, length) + L"\\WindowsPowerShell\\v1.0\\powershell.exe";
    std::wstring line = L"\"" + shell + L"\" -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -EncodedCommand " +
                        encodedCommand(powershellScript(command));
    const auto home = variable(L"USERPROFILE");
    STARTUPINFOEXW start{};
    start.StartupInfo.cb = sizeof(start);
    start.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    start.StartupInfo.hStdInput = input.h;
    start.StartupInfo.hStdOutput = output.h;
    start.StartupInfo.hStdError = output.h;
    SIZE_T size = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &size);
    std::vector<uint8_t> attributes(size);
    start.lpAttributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributes.data());
    if (!InitializeProcThreadAttributeList(start.lpAttributeList, 1, 0, &size)) return result;
    HANDLE handles[]{input.h, output.h}; // Only these are inherited.
    PROCESS_INFORMATION pi{};
    const bool created = UpdateProcThreadAttribute(start.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, handles, sizeof(handles), nullptr, nullptr) &&
        CreateProcessW(shell.c_str(), line.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT | EXTENDED_STARTUPINFO_PRESENT,
                       block.data(), home.empty() ? nullptr : home.c_str(), &start.StartupInfo, &pi);
    DeleteProcThreadAttributeList(start.lpAttributeList);
    if (!created) return result;
    Handle process(pi.hProcess), thread(pi.hThread);
    result.started = true;
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    for (;;) {
        if (WaitForSingleObject(process.h, 50) == WAIT_OBJECT_0) { result.finished = true; break; }
        if ((cancel && cancel()) || std::chrono::steady_clock::now() >= deadline) break; // Left running.
    }
    if (result.finished) {
        DWORD code = 1;
        GetExitCodeProcess(process.h, &code);
        result.exit_code = static_cast<int>(code);
    }
    LARGE_INTEGER file_size{};
    if (GetFileSizeEx(output.h, &file_size) && file_size.QuadPart > 0) {
        const auto total = static_cast<uint64_t>(file_size.QuadPart);
        const uint64_t from = total > max_output ? total - max_output : 0;
        result.output.resize(static_cast<size_t>(total - from));
        OVERLAPPED at{};
        at.Offset = static_cast<DWORD>(from); at.OffsetHigh = static_cast<DWORD>(from >> 32);
        DWORD read = 0;
        if (!ReadFile(output.h, result.output.data(), static_cast<DWORD>(result.output.size()), &read, &at)) read = 0;
        result.output.resize(read);
    }
    return result;
}
}
#else
#include "process_spawn.hpp"

#include <cstdlib>
#include <fcntl.h>
#include <filesystem>
#include <unistd.h>

extern char** environ;

namespace launch_scripts {
Result run(std::wstring_view command, const Context& context, std::chrono::milliseconds timeout, const std::function<bool()>& cancel) {
    Result result;
    if (trimmed(command).empty()) return result;
    std::error_code ec;
    auto pattern = (std::filesystem::temp_directory_path(ec) / "launchme-script-XXXXXX").string();
    if (ec) pattern = "/tmp/launchme-script-XXXXXX";
    const int output = mkstemp(pattern.data());
    if (output < 0) return result;
    unlink(pattern.c_str()); // Gone once the script and Launchme have closed it.
    std::vector<std::string> environment;
    const auto ours = [](std::string_view entry) {
        return entry.starts_with("LAUNCHME_GAME_NAME=") || entry.starts_with("LAUNCHME_GAME_KEY=") || entry.starts_with("LAUNCHME_SCRIPT_PHASE=");
    };
    for (char** entry = environ; entry && *entry; ++entry) if (!ours(*entry)) environment.emplace_back(*entry);
    environment.push_back("LAUNCHME_GAME_NAME=" + text_encoding::utf8Lenient(context.game_name));
    environment.push_back("LAUNCHME_GAME_KEY=" + context.game_key);
    environment.push_back(std::string("LAUNCHME_SCRIPT_PHASE=") + phaseName(context.phase));
    process_spawn::Options options;
    if (const char* home = std::getenv("HOME"); home && *home) options.directory = home;
    options.session = true; // Not ended with Launchme's terminal.
    options.output = output;
    options.error = output;
    options.environment = &environment;
    pid_t pid = 0;
    if (process_spawn::spawn({"/bin/sh", "-c", text_encoding::utf8Lenient(trimmed(command))}, options, pid) != 0) { close(output); return result; }
    result.started = true;
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    for (;;) {
        if (const auto code = process_spawn::waitExit(pid, std::chrono::milliseconds(50))) { result.finished = true; result.exit_code = *code; break; }
        if ((cancel && cancel()) || std::chrono::steady_clock::now() >= deadline) { process_spawn::reapLater(pid); break; } // Left running.
    }
    const off_t total = lseek(output, 0, SEEK_END);
    if (total > 0) {
        const off_t from = total > static_cast<off_t>(max_output) ? total - static_cast<off_t>(max_output) : 0;
        result.output.resize(static_cast<size_t>(total - from));
        const ssize_t read = pread(output, result.output.data(), result.output.size(), from);
        result.output.resize(read > 0 ? static_cast<size_t>(read) : 0);
    }
    close(output);
    return result;
}
}
#endif
