#include "aurelia.hpp"
#include "../platform/file_io.hpp"
#include "aurelia_stream.hpp"
#include "../library/game_log.hpp"
#include "../platform/command_line.hpp"
#include "../platform/text_encoding.hpp"
#ifdef _WIN32
#include <Windows.h>
#include <bcrypt.h>
#include <sddl.h>
#include <algorithm>
#include <map>
#include <fstream>

namespace aurelia {
namespace {
struct Handle {
    HANDLE value = nullptr;
    explicit Handle(HANDLE handle = nullptr) : value(handle) {}
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    ~Handle() { reset(); }
    void reset() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); value = nullptr; }
};
bool longCommand(Command c) {
    return c == Command::install || c == Command::install_resume || c == Command::relink || c == Command::import_files || c == Command::update || c == Command::verify || c == Command::downgrade ||
           c == Command::move || c == Command::workshop_install || c == Command::workshop_subscribe || c == Command::play || c == Command::chat_live;
}
bool pipeAbsent(const std::wstring& endpoint) {
    if (WaitNamedPipeW(endpoint.c_str(), 1)) return false;
    const auto error = GetLastError();
    return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND;
}
std::optional<DWORD> markerPid(const std::filesystem::path& marker) {
    if (marker.empty()) return {};
    std::ifstream file(marker, std::ios::binary | std::ios::ate);
    if (!file || file.tellg() <= 0 || file.tellg() > 65536) return {};
    std::string content(static_cast<size_t>(file.tellg()), '\0'); file.seekg(0);
    if (!file.read(content.data(), content.size())) return {};
    const auto value = json::parse(content); const auto pid = value ? integer(value->get("pid")) : std::nullopt;
    if (!pid || !*pid || *pid > UINT32_MAX || text(*value, "version").empty()) return {};
    return static_cast<DWORD>(*pid);
}
std::wstring lower(std::wstring text) { std::transform(text.begin(), text.end(), text.begin(), towlower); return text; }
// Launchme's own Aurelia: the helper's executable in an installed version's folder
// (helpers\aurelia\<version>\aurelia_windows_x86_64.exe), never this process. Its image when it is one.
std::optional<std::wstring> ownAurelia(DWORD pid, const std::filesystem::path& executable) {
    if (!pid || pid == GetCurrentProcessId()) return {};
    Handle process{OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)};
    if (!process.value) return {};
    std::wstring path(32768, L'\0'); DWORD size = static_cast<DWORD>(path.size());
    if (!QueryFullProcessImageNameW(process.value, 0, path.data(), &size)) return {};
    path.resize(size);
    const std::filesystem::path image(path);
    if (lower(image.filename().wstring()) != lower(executable.filename().wstring()) ||
        lower(image.parent_path().parent_path().lexically_normal().wstring()) != lower(executable.parent_path().parent_path().lexically_normal().wstring())) return {};
    return lower(image.lexically_normal().wstring());
}
bool current(const std::wstring& image, const std::filesystem::path& executable) { return image == lower(executable.lexically_normal().wstring()); }
// The process serving a named pipe. Its instances may be busy, or one may drop this connection before
// it is asked (a server that ends each connection at once): a few attempts.
std::optional<DWORD> pipeServer(const std::wstring& endpoint) {
    for (int attempt = 0; attempt < 10; ++attempt) {
        Handle pipe{CreateFileW(endpoint.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr)};
        if (pipe.value == INVALID_HANDLE_VALUE) {
            pipe.value = nullptr;
            const auto error = GetLastError();
            if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) return {};
            if (error == ERROR_PIPE_BUSY) WaitNamedPipeW(endpoint.c_str(), 500); else Sleep(50);
            continue;
        }
        ULONG pid = 0;
        if (GetNamedPipeServerProcessId(pipe.value, &pid) && pid) return static_cast<DWORD>(pid);
        Sleep(20);
    }
    return {};
}
// Before a command reaches the daemon. Upstream's stale-daemon fallback can stop every Aurelia
// daemon when its marker is missing: never enter it for an endpoint without a usable marker. A
// daemon of Launchme's own helper is ended here instead when it has no marker (cleaned from the temp
// folder, or removed by a newer CLI that could not stop it) or runs an older version's executable
// (after an update; upstream's own restart may fail and still remove the marker), so the command
// starts a fresh one; anything else at the endpoint is refused.
bool daemonReady(const std::wstring& endpoint, const std::filesystem::path& executable) {
    if (pipeAbsent(endpoint)) return true;
    const auto marker = daemonMarker(endpoint);
    std::optional<DWORD> stale;
    // A daemon that has just bound its pipe writes its marker right after: a moment for it.
    for (int wait = 0; wait < 20 && !stale; ++wait) {
        if (const auto pid = markerPid(marker))
            if (const auto image = ownAurelia(*pid, executable)) { if (current(*image, executable)) return true; stale = *pid; break; }
        Sleep(100);
        if (pipeAbsent(endpoint)) return true;
    }
    const auto server = stale ? stale : pipeServer(endpoint);
    if (!server || !ownAurelia(*server, executable)) return pipeAbsent(endpoint);
    Handle process{OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, *server)};
    if (!process.value || !TerminateProcess(process.value, 1)) return false;
    WaitForSingleObject(process.value, 5000);
    std::error_code error; if (!marker.empty()) std::filesystem::remove(marker, error);
    for (int wait = 0; wait < 50; ++wait) { if (pipeAbsent(endpoint)) return true; Sleep(100); }
    return false;
}
}
// The endpoint's name made a file name, in the temp folder.
std::filesystem::path daemonMarker(const std::wstring& endpoint) {
    auto name = endpoint;
    for (auto& c : name) if (!((c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') || (c >= L'0' && c <= L'9') || c == L'-' || c == L'.')) c = L'-';
    std::error_code error; const auto temp = std::filesystem::temp_directory_path(error);
    return error || endpoint.empty() ? std::filesystem::path{} : temp / (name + L".info");
}
std::wstring daemonEndpoint(const std::filesystem::path& profile) {
    if (!profile.is_absolute()) return {};
    std::error_code error; auto canonical = std::filesystem::weakly_canonical(profile, error);
    if (error) return {};
    auto path = canonical.make_preferred().native(); std::transform(path.begin(), path.end(), path.begin(), towlower);
    Handle token; if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token.value)) return {};
    DWORD count = 0; GetTokenInformation(token.value, TokenUser, nullptr, 0, &count);
    std::vector<uint8_t> bytes(count);
    if (!count || !GetTokenInformation(token.value, TokenUser, bytes.data(), count, &count)) return {};
    LPWSTR sid = nullptr;
    if (!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(bytes.data())->User.Sid, &sid)) return {};
    path += L'\n'; path += sid; LocalFree(sid);
    const auto identity = text_encoding::utf8(path);
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) return {};
    uint8_t hash[32]{};
    const auto result = BCryptHash(algorithm, nullptr, 0, reinterpret_cast<PUCHAR>(const_cast<char*>(identity.data())),
                                  static_cast<ULONG>(identity.size()), hash, sizeof(hash));
    BCryptCloseAlgorithmProvider(algorithm, 0);
    return result < 0 ? std::wstring{} : L"\\\\.\\pipe\\launchme-aurelia-" + text_encoding::wideLenient(key_codec::encode({reinterpret_cast<char*>(hash), sizeof(hash)}));
}
Reply invoke(const Runtime& runtime, const Request& request, const Cancel& cancel, const Event& event) {
    Reply result;
    const auto args = arguments(request);
    if (args.empty() || !runtime.executable.is_absolute() || !runtime.profile.is_absolute()) {
        result.error = tr(L"Nieprawidłowe polecenie Aurelii"); return result;
    }
    if (cancel && cancel()) { result.cancelled = true; return result; }
    std::error_code ec; std::filesystem::create_directories(runtime.profile, ec);
    if (ec) { result.error = tr(L"Nie można utworzyć profilu Aurelii"); return result; }
    // A private environment block for this child. Nothing changes the parent's environment,
    // and an existing standalone Aurelia session is never imported.
    std::map<std::wstring, std::wstring> env;
    auto original = GetEnvironmentStringsW(); if (!original) return result;
    for (const wchar_t* p = original; *p; p += wcslen(p) + 1) {
        const std::wstring entry(p); const auto at = entry.find(L'=', 1); if (at == entry.npos) continue;
        auto key = entry.substr(0, at); std::transform(key.begin(), key.end(), key.begin(), towupper);
        if (!key.starts_with(L"AURELIA_")) env[key] = entry.substr(at + 1);
    }
    FreeEnvironmentStringsW(original);
    env[L"AURELIA_CONFIG_DIR"] = runtime.profile.native();
    const auto endpoint = daemonEndpoint(runtime.profile);
    if (endpoint.empty()) { result.error = tr(L"Nie można utworzyć profilu Aurelii"); return result; }
    env[L"AURELIA_DAEMON_SOCKET"] = endpoint;
    // `stop` and `running` only read Aurelia's launch records and end the recorded process: run in
    // this child, not forwarded to the daemon, which (v0.1.40) answers nothing while it waits for a game.
    const bool local = request.command == Command::stop || request.command == Command::running;
    // The in-app sign-ins run in this child too, as the console's does: `login --json` would be
    // forwarded to the daemon, which sees only the command line (not AURELIA_PASSWORD) and its own
    // stdin. The session file they write is what the daemon reads on the next command.
    const bool interactive = request.command == Command::login_qr || request.command == Command::login_password;
    // A logged play runs here too: its -vv output is the game's log.
    const bool logged = request.command == Command::play && !request.log.empty();
    if (local || interactive || logged) env[L"AURELIA_NO_DAEMON"] = L"1";
    else if (request.command != Command::login && !daemonReady(endpoint, runtime.executable)) {
        result.error = tr(L"Nie można potwierdzić tożsamości daemona Aurelii. Spróbuj ponownie po zakończeniu jego uruchamiania."); return result;
    }
    env[L"NO_COLOR"] = L"1";
    if (request.command == Command::login_password) env[L"AURELIA_PASSWORD"] = request.secret; // Never on the command line.
    std::vector<wchar_t> environment;
    for (const auto& [key, value] : env) {
        const auto entry = key + L'=' + value; environment.insert(environment.end(), entry.begin(), entry.end()); environment.push_back(0);
    }
    environment.push_back(0);
    std::wstring command = command_line::quoteArgument(runtime.executable.native());
    for (const auto& arg : args) command += L' ' + command_line::quoteArgument(arg);
    if (command.size() >= 32767) { result.error = tr(L"Polecenie jest zbyt długie"); return result; }

    const bool console = request.command == Command::login, live = request.command == Command::chat_live;
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    Handle out_read, out_write, err_read, err_write, in_read, in_write;
    STARTUPINFOEXW startup{}; startup.StartupInfo.cb = console ? sizeof(STARTUPINFOW) : sizeof(startup);
    std::vector<uint8_t> attributes;
    if (!console) {
        if (!CreatePipe(&out_read.value, &out_write.value, &sa, 65536) || !CreatePipe(&err_read.value, &err_write.value, &sa, 65536) ||
            !CreatePipe(&in_read.value, &in_write.value, &sa, 0)) return result;
        SetHandleInformation(out_read.value, HANDLE_FLAG_INHERIT, 0); SetHandleInformation(err_read.value, HANDLE_FLAG_INHERIT, 0);
        SetHandleInformation(in_write.value, HANDLE_FLAG_INHERIT, 0);
        // Queries cannot wait for a prompt. Live chat and the in-app sign-ins keep stdin open (EOF ends a sign-in).
        if (!live && !interactive) in_write.reset();
        startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
        startup.StartupInfo.hStdInput = in_read.value; startup.StartupInfo.hStdOutput = out_write.value; startup.StartupInfo.hStdError = err_write.value;
        SIZE_T size = 0; InitializeProcThreadAttributeList(nullptr, 1, 0, &size); attributes.resize(size);
        startup.lpAttributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributes.data());
        if (!InitializeProcThreadAttributeList(startup.lpAttributeList, 1, 0, &size)) return result;
        HANDLE handles[]{in_read.value, out_write.value, err_write.value};
        if (!UpdateProcThreadAttribute(startup.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, handles, sizeof(handles), nullptr, nullptr)) {
            DeleteProcThreadAttributeList(startup.lpAttributeList); return result;
        }
    }
    PROCESS_INFORMATION pi{};
    const DWORD flags = CREATE_UNICODE_ENVIRONMENT | (console ? CREATE_NEW_CONSOLE : CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT);
    const bool created = CreateProcessW(runtime.executable.c_str(), command.data(), nullptr, nullptr, !console, flags,
                                       environment.data(), runtime.profile.c_str(), &startup.StartupInfo, &pi) != FALSE;
    if (!console) DeleteProcThreadAttributeList(startup.lpAttributeList);
    if (!created) { result.error = tr(L"Nie można uruchomić Aurelii"); return result; }
    Handle process{pi.hProcess}, thread{pi.hThread};
    struct ChildScope {
        HANDLE process; bool exited = false;
        ~ChildScope() { if (!exited && WaitForSingleObject(process, 0) != WAIT_OBJECT_0) { TerminateProcess(process, 1); WaitForSingleObject(process, 3000); } }
    } child{process.value}; // Also cleans up if an output consumer throws.
    out_write.reset(); err_write.reset(); in_read.reset();
    // Do not place the thin client in a kill-on-close job: its daemon and launched games
    // are independent. Cancellation closes only the owned CLI process, never the game/Steam.
    JsonStream stdout_json, stderr_json;
    std::optional<json::Value> stdout_result, stderr_result;
    size_t output_bytes = 0;
    bool overflow = false;
    constexpr size_t limit = 16 * 1024 * 1024;
    const auto drain = [&](HANDLE pipe, bool stdout_pipe) {
        if (!pipe) return;
        for (int reads = 0; reads < 64; ++reads) {
            DWORD available = 0, received = 0;
            if (!PeekNamedPipe(pipe, nullptr, 0, nullptr, &available, nullptr) || !available) break;
            char buffer[16384];
            if (!ReadFile(pipe, buffer, std::min<DWORD>(available, sizeof(buffer)), &received, nullptr) || !received) break;
            if (logged) game_log::append(request.log, {buffer, received});
            if (stdout_pipe && !live && (output_bytes += received) > limit) { overflow = true; return; }
            auto& parser = stdout_pipe ? stdout_json : stderr_json;
            if (!parser.feed({buffer, received}, [&](const json::Value& item) {
                if (event) event(item);
                if (item.get("error")) result.error = text_encoding::wideLenient(text(item, "error"));
                const auto kind = text(item, "event");
                if (kind.empty() || kind == "result" || item.get("error")) (stdout_pipe ? stdout_result : stderr_result) = item;
            })) { overflow = true; return; }
        }
    };
    const auto started = GetTickCount64();
    // A sign-in waits for the user; a cloud sync may move large saves: ten minutes each.
    const bool slow = console || interactive || request.command == Command::cloud_sync;
    const uint64_t timeout = slow ? 10 * 60 * 1000ULL : longCommand(request.command) ? 0 : runtime.query_timeout_ms;
    // Lines the UI wrote for the child's stdin (a Steam Guard code, cancel).
    const auto feed = [&] {
        if (!request.input || !in_write.value) return;
        for (auto line : request.input->take()) {
            line += '\n'; DWORD written = 0;
            if (!WriteFile(in_write.value, line.data(), static_cast<DWORD>(line.size()), &written, nullptr)) { in_write.reset(); return; }
        }
    };
    while (true) {
        feed();
        drain(out_read.value, true); drain(err_read.value, false);
        if ((cancel && cancel()) || overflow || (timeout && GetTickCount64() - started > timeout)) {
            result.cancelled = cancel && cancel();
            result.error = result.cancelled ? tr(L"Anulowano") : overflow ? tr(L"Odpowiedź Aurelii jest zbyt duża") : tr(L"Aurelia nie odpowiedziała na czas");
            // A sign-in is asked to stop (it answers "cancelled"), live chat sees its stdin end.
            if (interactive && in_write.value) { const std::string stop = "cancel\n"; DWORD written = 0; WriteFile(in_write.value, stop.data(), static_cast<DWORD>(stop.size()), &written, nullptr); }
            in_write.reset();
            if (WaitForSingleObject(process.value, live || interactive ? 2000 : 0) != WAIT_OBJECT_0) TerminateProcess(process.value, 1);
            WaitForSingleObject(process.value, 3000); return result;
        }
        if (WaitForSingleObject(process.value, 40) == WAIT_OBJECT_0) break;
    }
    drain(out_read.value, true); drain(err_read.value, false);
    child.exited = true;
    DWORD code = 1; GetExitCodeProcess(process.value, &code);
    if (console) { result.ok = code == 0; if (!result.ok) result.error = tr(L"Logowanie Aurelii nie powiodło się"); return result; }
    auto parsed = stdout_result ? std::move(stdout_result) : std::move(stderr_result);
    if (parsed) result.data = std::move(*parsed);
    if (result.data.get("error")) result.error = text_encoding::wideLenient(text(result.data, "error"));
    result.ok = code == 0 && !overflow && !stdout_json.incomplete() && (parsed.has_value() || live) && result.error.empty();
    if (!result.ok && result.error.empty()) result.error = tr(L"Polecenie Aurelii nie powiodło się");
    return result;
}
Transport transport(Runtime runtime) {
    return [runtime = std::move(runtime)](const Request& r, const Cancel& c, const Event& e) { return invoke(runtime, r, c, e); };
}
}
#else
// Linux: the same contract over posix_spawn and pipes; the daemon endpoint is a Unix socket and the
// interactive sign-in runs in a terminal window.
#include "../library/xdg_paths.hpp"
#include "../platform/hashing.hpp"
#include "../platform/process_spawn.hpp"
#include "../platform/system.hpp"
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <poll.h>
#include <spawn.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

extern char** environ;

namespace aurelia {
namespace {
using namespace std::chrono_literals;
struct Descriptor {
    int fd = -1;
    Descriptor() = default;
    Descriptor(const Descriptor&) = delete;
    Descriptor& operator=(const Descriptor&) = delete;
    ~Descriptor() { reset(); }
    void reset() { if (fd >= 0) ::close(fd); fd = -1; }
};
bool pipeOf(Descriptor& read_end, Descriptor& write_end) {
    int fds[2];
    if (::pipe2(fds, O_CLOEXEC) != 0) return false;
    read_end.fd = fds[0]; write_end.fd = fds[1];
    return ::fcntl(read_end.fd, F_SETFL, ::fcntl(read_end.fd, F_GETFL) | O_NONBLOCK) == 0;
}
bool longCommand(Command c) {
    return c == Command::install || c == Command::install_resume || c == Command::relink || c == Command::import_files || c == Command::update || c == Command::verify || c == Command::downgrade ||
           c == Command::move || c == Command::workshop_install || c == Command::workshop_subscribe || c == Command::play || c == Command::chat_live;
}
std::string readSmall(const std::filesystem::path& file, std::streamoff limit) { return file_io::readText(file, static_cast<size_t>(limit)); }
// The daemon sockets' folder: the user's runtime folder, else a private one under /tmp.
std::filesystem::path socketFolder() {
    struct stat status{};
    if (const auto runtime = xdg_paths::variable(L"XDG_RUNTIME_DIR");
        !runtime.empty() && ::stat(runtime.c_str(), &status) == 0 && S_ISDIR(status.st_mode) && status.st_uid == ::getuid()) return runtime;
    std::error_code error; const auto temp = std::filesystem::temp_directory_path(error); if (error) return {};
    const auto folder = temp / ("launchme-" + std::to_string(::getuid()));
    if (::mkdir(folder.c_str(), 0700) != 0 && errno != EEXIST) return {};
    if (::lstat(folder.c_str(), &status) != 0 || !S_ISDIR(status.st_mode) || status.st_uid != ::getuid() || (status.st_mode & 077)) return {};
    return folder;
}
// The daemon's socket: 1 something listens on it (its process in `server`), 0 nothing does any more
// (left by a daemon that ended: Aurelia binds a new one), -1 unusable.
int socketState(const std::filesystem::path& socket_path, pid_t* server = nullptr) {
    sockaddr_un address{}; address.sun_family = AF_UNIX;
    const auto name = socket_path.string();
    if (name.size() >= sizeof(address.sun_path)) return -1;
    std::memcpy(address.sun_path, name.c_str(), name.size() + 1);
    // Never waiting: a listener that does not accept (its queue full) answers EAGAIN instead of blocking.
    const int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (fd < 0) return -1;
    const bool connected = ::connect(fd, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0;
    const int error = errno;
    if (connected && server) {
        ucred peer{}; socklen_t size = sizeof(peer);
        *server = ::getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &peer, &size) == 0 ? peer.pid : 0;
    }
    ::close(fd);
    if (connected || error == EAGAIN || error == EINPROGRESS) return 1;
    return error == ECONNREFUSED || error == ENOENT ? 0 : -1;
}
std::optional<pid_t> markerPid(const std::filesystem::path& marker) {
    const auto content = readSmall(marker, 65536);
    const auto value = content.empty() ? std::nullopt : json::parse(content);
    const auto pid = value ? integer(value->get("pid")) : std::nullopt;
    if (!pid || !*pid || *pid > INT32_MAX || text(*value, "version").empty()) return {};
    return static_cast<pid_t>(*pid);
}
// Launchme's own Aurelia: the helper's executable in an installed version's folder
// (helpers/aurelia/<version>/aurelia…), never this process. Its image when it is one.
std::optional<std::filesystem::path> ownAurelia(pid_t pid, const std::filesystem::path& executable) {
    if (pid <= 0 || pid == ::getpid()) return {};
    std::error_code error;
    auto image = std::filesystem::read_symlink("/proc/" + std::to_string(pid) + "/exe", error).string();
    if (error || image.empty()) return {};
    if (image.ends_with(" (deleted)")) image.resize(image.size() - 10); // Its binary was replaced.
    const std::filesystem::path path = std::filesystem::path(image).lexically_normal();
    const auto folder = std::filesystem::weakly_canonical(executable.parent_path().parent_path(), error);
    if (error || path.filename() != executable.filename() || path.parent_path().parent_path() != folder.lexically_normal()) return {};
    return path;
}
bool current(const std::filesystem::path& image, const std::filesystem::path& executable) {
    std::error_code error; const auto canonical = std::filesystem::weakly_canonical(executable, error);
    return !error && image == canonical.lexically_normal();
}
// Still running: neither gone nor a zombie waiting for its parent.
bool running(pid_t pid) {
    if (::kill(pid, 0) != 0 && errno == ESRCH) return false;
    std::ifstream file("/proc/" + std::to_string(pid) + "/stat");
    std::string stat; std::getline(file, stat);
    const auto close = stat.rfind(')');
    return close == std::string::npos || close + 2 >= stat.size() || stat[close + 2] != 'Z';
}
// Before a command reaches the daemon. Upstream's stale-daemon fallback can stop every Aurelia
// daemon when its marker is missing: never enter it for a listening socket without a usable marker
// (<socket>.info). A daemon of Launchme's own helper is ended here instead when it has no marker
// (removed by a newer CLI that could not stop it) or runs an older version's executable (after an
// update; upstream's own restart may fail and still remove the marker), so the command starts a
// fresh one; anything else at the socket is refused.
bool daemonReady(const std::wstring& endpoint, const std::filesystem::path& executable) {
    const std::filesystem::path socket_path(endpoint);
    struct stat status{};
    if (::lstat(socket_path.c_str(), &status) != 0) return errno == ENOENT; // No daemon yet: Aurelia starts one.
    if (!S_ISSOCK(status.st_mode) || status.st_uid != ::getuid()) return false; // Aurelia would replace it.
    const auto marker = daemonMarker(endpoint);
    pid_t server = 0;
    // A daemon that has just bound its socket writes its marker right after: a moment for it.
    for (int wait = 0; wait < 20 && !server; ++wait) {
        const auto state = socketState(socket_path);
        if (state != 1) return state == 0;
        if (const auto pid = markerPid(marker))
            if (const auto image = ownAurelia(*pid, executable)) { if (current(*image, executable)) return true; server = *pid; break; }
        std::this_thread::sleep_for(100ms);
    }
    if (!server) {
        if (const auto state = socketState(socket_path, &server); state != 1) return state == 0;
    }
    if (!ownAurelia(server, executable)) return false;
    ::kill(server, SIGTERM);
    for (int wait = 0; wait < 30 && running(server); ++wait) std::this_thread::sleep_for(100ms);
    if (running(server)) { ::kill(server, SIGKILL); for (int wait = 0; wait < 20 && running(server); ++wait) std::this_thread::sleep_for(100ms); }
    std::error_code error; std::filesystem::remove(marker, error);
    for (int wait = 0; wait < 50; ++wait) { if (socketState(socket_path) == 0) return true; std::this_thread::sleep_for(100ms); }
    return false;
}
// posix_spawn with the three standard streams given; its own session, nothing else inherited.
int spawnChild(const std::vector<std::string>& arguments, const std::vector<std::string>& environment, const std::filesystem::path& directory,
               int input, int output, int error_output, pid_t& pid) {
    std::vector<char*> argv, envp;
    for (const auto& argument : arguments) argv.push_back(const_cast<char*>(argument.c_str()));
    argv.push_back(nullptr);
    for (const auto& entry : environment) envp.push_back(const_cast<char*>(entry.c_str()));
    envp.push_back(nullptr);
    posix_spawn_file_actions_t actions;
    posix_spawnattr_t attributes;
    if (posix_spawn_file_actions_init(&actions) != 0) return ENOMEM;
    if (posix_spawnattr_init(&attributes) != 0) { posix_spawn_file_actions_destroy(&actions); return ENOMEM; }
    int error = posix_spawn_file_actions_adddup2(&actions, input, 0);
    if (!error) error = posix_spawn_file_actions_adddup2(&actions, output, 1);
    if (!error) error = posix_spawn_file_actions_adddup2(&actions, error_output, 2);
    if (!error) error = posix_spawn_file_actions_addclosefrom_np(&actions, 3);
    if (!error) error = posix_spawn_file_actions_addchdir_np(&actions, directory.c_str());
    sigset_t none, all;
    sigemptyset(&none); sigfillset(&all);
    if (!error) error = posix_spawnattr_setsigmask(&attributes, &none);
    if (!error) error = posix_spawnattr_setsigdefault(&attributes, &all);
    if (!error) error = posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF | POSIX_SPAWN_SETSID);
    if (!error) error = posix_spawn(&pid, argv[0], &actions, &attributes, argv.data(), envp.data());
    posix_spawnattr_destroy(&attributes);
    posix_spawn_file_actions_destroy(&actions);
    return error;
}
// Ends the child when it is still running; a child that does not go within 3 s is reaped later.
void stopChild(pid_t pid, std::chrono::milliseconds grace) {
    if (process_spawn::waitExit(pid, grace)) return;
    ::kill(pid, SIGKILL);
    if (!process_spawn::waitExit(pid, 3000ms)) process_spawn::reapLater(pid);
}
std::string hexRandom() {
    uint8_t bytes[8]{};
    if (!system_info::randomBytes(bytes, sizeof(bytes))) return std::to_string(::getpid());
    return hashing::hex(bytes);
}
// A terminal window running `command`, from $TERMINAL or the usual emulators; empty when none is.
std::vector<std::string> terminalCommand(const std::vector<std::string>& command) {
    std::vector<std::string> line;
    if (const auto chosen = system_info::environment(L"TERMINAL", 4095); chosen && !chosen->empty()) {
        std::filesystem::path program(*chosen);
        if (!program.is_absolute()) program = xdg_paths::findProgram({*chosen});
        if (xdg_paths::executableFile(program)) line = {program.string(), "-e"};
    }
    // Each with the options that keep it in the foreground until the command ends.
    struct Known { const wchar_t* name; std::initializer_list<const char*> options; };
    static const Known known[] = {
        {L"x-terminal-emulator", {"-e"}}, {L"konsole", {"--nofork", "-e"}}, {L"gnome-terminal", {"--wait", "--"}},
        {L"ptyxis", {"--new-window", "--"}}, {L"kgx", {"--"}}, {L"xfce4-terminal", {"--disable-server", "-x"}},
        {L"mate-terminal", {"--disable-factory", "-x"}}, {L"lxterminal", {"-e"}}, {L"alacritty", {"-e"}},
        {L"kitty", {"--"}}, {L"foot", {}}, {L"wezterm", {"start", "--always-new-process", "--"}}, {L"xterm", {"-e"}},
    };
    for (size_t i = 0; line.empty() && i < std::size(known); ++i)
        if (const auto program = xdg_paths::findProgram({known[i].name}); !program.empty()) {
            line = {program.string()};
            line.insert(line.end(), known[i].options.begin(), known[i].options.end());
        }
    if (!line.empty()) line.insert(line.end(), command.begin(), command.end());
    return line;
}
// `aurelia login` is interactive (password, Steam Guard): it gets a terminal window of its own, as
// the Windows build gives it a console. The result comes back through files in the private profile,
// so terminals that hand the window to a server process (and return at once) work too.
Reply terminalLogin(const Runtime& runtime, const std::vector<std::string>& variables, const Cancel& cancel) {
    Reply result;
    const auto base = runtime.profile.string() + "/.launchme-login-" + hexRandom();
    const std::filesystem::path pid_file = base + ".pid", status_file = base + ".status";
    struct Files { std::filesystem::path a, b; ~Files() { std::error_code e; std::filesystem::remove(a, e); std::filesystem::remove(b, e); } } files{pid_file, status_file};
    std::vector<std::string> inner{"/usr/bin/env"};
    inner.insert(inner.end(), variables.begin(), variables.end());
    inner.insert(inner.end(), {"/bin/sh", "-c",
        "echo $$ > \"$1\"; \"$3\" login; code=$?; echo $code > \"$2\"; "
        "if [ $code -ne 0 ]; then printf '\\nPress Enter to close this window.'; read _; fi",
        "aurelia-login", pid_file.string(), status_file.string(), runtime.executable.string()});
    const auto line = terminalCommand(inner);
    pid_t terminal = 0;
    process_spawn::Options options; options.session = true; options.directory = runtime.profile;
    if (line.empty() || process_spawn::spawn(line, options, terminal) != 0) { result.error = tr(L"Logowanie Aurelii nie powiodło się"); return result; }
    const auto started = std::chrono::steady_clock::now();
    std::optional<std::chrono::steady_clock::time_point> terminal_ended;
    const auto shell = [&]() -> pid_t {
        const auto text = readSmall(pid_file, 64);
        try { return text.empty() ? 0 : static_cast<pid_t>(std::stol(text)); } catch (...) { return 0; }
    };
    const auto status = [&]() -> std::optional<int> {
        const auto text = readSmall(status_file, 64);
        try { return text.empty() ? std::nullopt : std::optional(std::stoi(text)); } catch (...) { return std::nullopt; }
    };
    std::optional<int> code;
    for (;;) {
        if ((code = status())) break;
        const bool stopped = cancel && cancel();
        if (stopped || std::chrono::steady_clock::now() - started > 10min) {
            if (const auto sh = shell(); sh > 0) ::kill(sh, SIGHUP);
            if (!terminal_ended) process_spawn::killGroup(terminal);
            result.cancelled = stopped;
            result.error = stopped ? tr(L"Anulowano") : tr(L"Logowanie Aurelii nie powiodło się");
            return result;
        }
        if (!terminal_ended && process_spawn::waitExit(terminal, 0ms)) terminal_ended = std::chrono::steady_clock::now();
        const auto sh = shell();
        // The window was closed before the sign-in ended, or the terminal never ran the command.
        if (sh > 0 && ::kill(sh, 0) != 0 && errno == ESRCH && !(code = status())) break;
        if (code) break;
        if (sh <= 0 && terminal_ended && std::chrono::steady_clock::now() - *terminal_ended > 10s) break;
        std::this_thread::sleep_for(200ms);
    }
    if (!terminal_ended) process_spawn::reapLater(terminal);
    result.ok = code && *code == 0;
    if (!result.ok) result.error = tr(L"Logowanie Aurelii nie powiodło się");
    return result;
}
// Linux, a native build is preferred: `install` without a chosen platform asks the store first.
bool nativeBuild(const Runtime& runtime, const Request& request, const Cancel& cancel) {
    Request details{Command::info, request.app};
    const auto reply = invoke(runtime, details, cancel);
    const auto* platforms = reply.ok ? reply.data.get("platforms") : nullptr;
    if (!platforms || platforms->type != json::Value::Type::array) return false;
    return std::any_of(platforms->elements.begin(), platforms->elements.end(), [](const json::Value& p) { return p.stringOr() == "linux"; });
}
}
std::wstring daemonEndpoint(const std::filesystem::path& profile) {
    if (!profile.is_absolute()) return {};
    std::error_code error; const auto canonical = std::filesystem::weakly_canonical(profile, error);
    if (error) return {};
    const auto identity = canonical.lexically_normal().string() + '\n' + std::to_string(::getuid());
    const auto digest = hashing::bytesHex(hashing::Algorithm::sha256, {reinterpret_cast<const uint8_t*>(identity.data()), identity.size()});
    const auto folder = socketFolder();
    if (digest.size() != 64 || folder.empty()) return {};
    // Half the digest keeps the path inside sockaddr_un's 108 bytes.
    const auto endpoint = folder / ("launchme-aurelia-" + digest.substr(0, 32) + ".sock");
    if (endpoint.string().size() >= sizeof(sockaddr_un::sun_path)) return {};
    return endpoint.wstring();
}
std::filesystem::path daemonMarker(const std::wstring& endpoint) {
    if (endpoint.empty()) return {};
    std::filesystem::path marker(endpoint); marker.replace_extension(".info");
    return marker;
}
Reply invoke(const Runtime& runtime, const Request& request, const Cancel& cancel, const Event& event) {
    Reply result;
    if (arguments(request).empty() || !runtime.executable.is_absolute() || !runtime.profile.is_absolute()) {
        result.error = tr(L"Nieprawidłowe polecenie Aurelii"); return result;
    }
    if (cancel && cancel()) { result.cancelled = true; return result; }
    std::error_code ec; std::filesystem::create_directories(runtime.profile, ec);
    if (ec) { result.error = tr(L"Nie można utworzyć profilu Aurelii"); return result; }
    Request effective = request;
    if (request.command == Command::install && request.text.empty() && nativeBuild(runtime, request, cancel)) effective.text = L"linux";
    if (cancel && cancel()) { result.cancelled = true; result.error = tr(L"Anulowano"); return result; }
    const auto args = arguments(effective);
    if (args.empty()) { result.error = tr(L"Nieprawidłowe polecenie Aurelii"); return result; }
    // A private environment for this child. Nothing changes the parent's environment, and an
    // existing standalone Aurelia session is never imported.
    const auto endpoint = daemonEndpoint(runtime.profile);
    if (endpoint.empty()) { result.error = tr(L"Nie można utworzyć profilu Aurelii"); return result; }
    std::vector<std::string> own{"AURELIA_CONFIG_DIR=" + runtime.profile.string(), "AURELIA_DAEMON_SOCKET=" + text_encoding::utf8Lenient(endpoint)};
    // `stop` and `running` only read Aurelia's launch records and end the recorded process: run in
    // this child, not forwarded to the daemon, which (v0.1.40) answers nothing while it waits for a game.
    const bool local = request.command == Command::stop || request.command == Command::running;
    // The in-app sign-ins run in this child too, as the terminal's does: `login --json` would be
    // forwarded to the daemon, which sees only the command line (not AURELIA_PASSWORD) and its own
    // stdin. The session file they write is what the daemon reads on the next command.
    const bool interactive = request.command == Command::login_qr || request.command == Command::login_password;
    // A logged play runs here too: its -vv output is the game's log.
    const bool logged = request.command == Command::play && !request.log.empty();
    if (local || interactive || logged) own.push_back("AURELIA_NO_DAEMON=1");
    else if (request.command != Command::login && !daemonReady(endpoint, runtime.executable)) {
        result.error = tr(L"Nie można potwierdzić tożsamości daemona Aurelii. Spróbuj ponownie po zakończeniu jego uruchamiania."); return result;
    }
    own.push_back("NO_COLOR=1");
    if (request.command == Command::login) return terminalLogin(runtime, own, cancel);
    if (request.command == Command::login_password) own.push_back("AURELIA_PASSWORD=" + text_encoding::utf8Lenient(request.secret)); // Never in argv.
    std::vector<std::string> environment;
    for (char** entry = environ; entry && *entry; ++entry) {
        const std::string_view line(*entry);
        if (line.find('=') == std::string_view::npos || line.starts_with("AURELIA_") || line.starts_with("NO_COLOR=")) continue;
        environment.emplace_back(line);
    }
    environment.insert(environment.end(), own.begin(), own.end());
    std::vector<std::string> argv{runtime.executable.string()};
    for (const auto& arg : args) argv.push_back(text_encoding::utf8Lenient(arg));

    const bool live = request.command == Command::chat_live;
    Descriptor out_read, out_write, err_read, err_write, in_read, in_write;
    if (!pipeOf(out_read, out_write) || !pipeOf(err_read, err_write) || !pipeOf(in_read, in_write)) return result;
    // The child's stdin blocks as a terminal's does: a sign-in waiting for its next line must not see EAGAIN.
    ::fcntl(in_read.fd, F_SETFL, ::fcntl(in_read.fd, F_GETFL) & ~O_NONBLOCK);
    // Queries cannot wait for a prompt. Live chat and the in-app sign-ins keep stdin open (EOF ends a sign-in).
    if (!live && !interactive) in_write.reset();
    // A child that went away must not end Launchme with SIGPIPE when a line is written to it.
    static const bool ignore_broken_pipe = (::signal(SIGPIPE, SIG_IGN), true);
    (void)ignore_broken_pipe;
    pid_t pid = 0;
    if (spawnChild(argv, environment, runtime.profile, in_read.fd, out_write.fd, err_write.fd, pid) != 0) {
        result.error = tr(L"Nie można uruchomić Aurelii"); return result;
    }
    // Not in a shared process group with its daemon or launched games, which are independent:
    // cancellation ends only this CLI process, never the game/Steam. Also cleans up if an output
    // consumer throws.
    struct ChildScope { pid_t pid; bool exited = false; ~ChildScope() { if (!exited) stopChild(pid, 0ms); } } child{pid};
    out_write.reset(); err_write.reset(); in_read.reset();
    JsonStream stdout_json, stderr_json;
    std::optional<json::Value> stdout_result, stderr_result;
    size_t output_bytes = 0;
    bool overflow = false;
    constexpr size_t limit = 16 * 1024 * 1024;
    const auto drain = [&](Descriptor& pipe, bool stdout_pipe) {
        for (int reads = 0; pipe.fd >= 0 && reads < 64; ++reads) {
            char buffer[16384];
            const ssize_t received = ::read(pipe.fd, buffer, sizeof(buffer));
            if (received < 0 && errno == EINTR) continue;
            if (received < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return;
            if (received <= 0) { pipe.reset(); return; }
            if (logged) game_log::append(request.log, {buffer, static_cast<size_t>(received)});
            if (stdout_pipe && !live && (output_bytes += static_cast<size_t>(received)) > limit) { overflow = true; return; }
            auto& parser = stdout_pipe ? stdout_json : stderr_json;
            if (!parser.feed({buffer, static_cast<size_t>(received)}, [&](const json::Value& item) {
                if (event) event(item);
                if (item.get("error")) result.error = text_encoding::wideLenient(text(item, "error"));
                const auto kind = text(item, "event");
                if (kind.empty() || kind == "result" || item.get("error")) (stdout_pipe ? stdout_result : stderr_result) = item;
            })) { overflow = true; return; }
        }
    };
    const auto started = std::chrono::steady_clock::now();
    // A sign-in waits for the user; a cloud sync may move large saves: ten minutes each.
    const bool slow = interactive || request.command == Command::cloud_sync;
    const std::chrono::milliseconds timeout(slow ? 10 * 60 * 1000 : longCommand(request.command) ? 0 : runtime.query_timeout_ms);
    // Lines the UI wrote for the child's stdin (a Steam Guard code, cancel).
    const auto feed = [&] {
        if (!request.input || in_write.fd < 0) return;
        for (auto line : request.input->take()) {
            line += '\n';
            if (::write(in_write.fd, line.data(), line.size()) != static_cast<ssize_t>(line.size())) { in_write.reset(); return; }
        }
    };
    int code = 1;
    while (true) {
        feed();
        pollfd fds[2]{{out_read.fd, POLLIN, 0}, {err_read.fd, POLLIN, 0}};
        if (out_read.fd >= 0 || err_read.fd >= 0) ::poll(fds, 2, 40); else std::this_thread::sleep_for(40ms);
        drain(out_read, true); drain(err_read, false);
        if ((cancel && cancel()) || overflow || (timeout.count() && std::chrono::steady_clock::now() - started > timeout)) {
            result.cancelled = cancel && cancel();
            result.error = result.cancelled ? tr(L"Anulowano") : overflow ? tr(L"Odpowiedź Aurelii jest zbyt duża") : tr(L"Aurelia nie odpowiedziała na czas");
            // A sign-in is asked to stop (it answers "cancelled"), live chat sees its stdin end.
            if (interactive && in_write.fd >= 0) { const std::string stop = "cancel\n"; [[maybe_unused]] const auto sent = ::write(in_write.fd, stop.data(), stop.size()); }
            in_write.reset();
            child.exited = true; stopChild(pid, live || interactive ? 2000ms : 0ms);
            return result;
        }
        if (const auto exit = process_spawn::waitExit(pid, 0ms)) { code = *exit; child.exited = true; break; }
    }
    drain(out_read, true); drain(err_read, false);
    in_write.reset();
    auto parsed = stdout_result ? std::move(stdout_result) : std::move(stderr_result);
    if (parsed) result.data = std::move(*parsed);
    if (result.data.get("error")) result.error = text_encoding::wideLenient(text(result.data, "error"));
    result.ok = code == 0 && !overflow && !stdout_json.incomplete() && (parsed.has_value() || live) && result.error.empty();
    if (!result.ok && result.error.empty()) result.error = tr(L"Polecenie Aurelii nie powiodło się");
    return result;
}
Transport transport(Runtime runtime) {
    return [runtime = std::move(runtime)](const Request& r, const Cancel& c, const Event& e) { return invoke(runtime, r, c, e); };
}
}
#endif
