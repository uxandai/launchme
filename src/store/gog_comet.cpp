#include "gog_comet.hpp"
#include "../net/http_download.hpp"
#include "../platform/atomic_file.hpp"
#include "../platform/command_line.hpp"
#include "../platform/hashing.hpp"
#include "../platform/text_encoding.hpp"
#ifdef _WIN32
#include <WinSock2.h>
#include <WS2tcpip.h>
#include <Windows.h>
#else
#include "../platform/process_spawn.hpp"
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
extern char** environ;
#endif
#include <algorithm>
#include <chrono>
#include <map>
#include <thread>

namespace gog {
namespace {
constexpr uint16_t galaxy_port = 9977;

bool regular(const std::filesystem::path& path) {
    std::error_code error;
    return std::filesystem::is_regular_file(path, error) && !error;
}

// Reads comet's log line by line until the process closes it, and hands each line on.
class LineReader {
    std::string pending_;
public:
    template <class Each> void feed(const char* data, size_t size, Each&& each) {
        pending_.append(data, size);
        for (size_t end; (end = pending_.find_first_of("\r\n")) != pending_.npos;) {
            if (end) each(std::string_view(pending_).substr(0, end));
            pending_.erase(0, end + 1);
        }
        if (pending_.size() > 65536) pending_.clear();
    }
};

#ifdef _WIN32
struct Handle {
    HANDLE h = nullptr;
    Handle() = default;
    explicit Handle(HANDLE value) : h(value) {}
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    ~Handle() { if (h && h != INVALID_HANDLE_VALUE) CloseHandle(h); }
    explicit operator bool() const { return h && h != INVALID_HANDLE_VALUE; }
};

class CometProcess final : public Comet {
    Handle job_, process_, log_;
    std::thread reader_;
public:
    CometProcess(HANDLE job, HANDLE process, HANDLE pipe, std::function<void(std::string_view)> each)
        : job_(job), process_(process), log_(pipe) {
        reader_ = std::thread([pipe = log_.h, each = std::move(each)] {
            LineReader lines; char buffer[8192]; DWORD received = 0;
            while (ReadFile(pipe, buffer, sizeof(buffer), &received, nullptr) && received)
                lines.feed(buffer, received, [&](std::string_view line) { if (each) each(line); });
        });
    }
    ~CometProcess() override {
        // Closing the job ends comet; its end closes the log, which ends the reader.
        TerminateJobObject(job_.h, 0);
        WaitForSingleObject(process_.h, 5000);
        CancelSynchronousIo(reader_.native_handle());
        if (reader_.joinable()) reader_.join();
    }
    bool running() const override { return WaitForSingleObject(process_.h, 0) == WAIT_TIMEOUT; }
};

// GalaxyOverlay.exe beside a game: it ends with the game; Launchme ends it with comet.
class OverlayProcess final : public Comet {
    Handle job_, process_;
public:
    OverlayProcess(HANDLE job, HANDLE process) : job_(job), process_(process) {}
    ~OverlayProcess() override { TerminateJobObject(job_.h, 0); }
    bool running() const override { return WaitForSingleObject(process_.h, 0) == WAIT_TIMEOUT; }
};
#else
class CometProcess final : public Comet {
    pid_t pid_;
    int log_;
    std::thread reader_;
    mutable bool ended_ = false;
public:
    CometProcess(pid_t pid, int fd, std::function<void(std::string_view)> each) : pid_(pid), log_(fd) {
        reader_ = std::thread([fd = log_, each = std::move(each)] {
            LineReader lines; char buffer[8192];
            for (;;) {
                const ssize_t got = ::read(fd, buffer, sizeof(buffer));
                if (got < 0 && errno == EINTR) continue;
                if (got <= 0) break;
                lines.feed(buffer, static_cast<size_t>(got), [&](std::string_view line) { if (each) each(line); });
            }
        });
    }
    ~CometProcess() override {
        if (!ended_) process_spawn::killGroup(pid_); // Its end closes the log, which ends the reader.
        if (reader_.joinable()) reader_.join();
        ::close(log_);
    }
    // Reaped once it ends; the UI thread is the only caller.
    bool running() const override { return !ended_ && !(ended_ = process_spawn::waitExit(pid_, std::chrono::milliseconds(0)).has_value()); }
};
#endif
}

namespace {
// A pinned GitHub release asset at `file`, SHA-256 checked (also when already there).
bool fetchPinned(const std::wstring& url, const std::filesystem::path& file, uint64_t bytes, std::string_view digest,
                 const std::function<bool()>& cancel, [[maybe_unused]] bool executable) {
    const auto verified = [&](const std::filesystem::path& path) {
        const auto value = hashing::fileHex(hashing::Algorithm::sha256, path, cancel);
        return !value.empty() && !cancel() && value == digest;
    };
    std::error_code error;
    if (regular(file) && std::filesystem::file_size(file, error) == bytes && !error && verified(file)) return true;
    if (cancel()) return false;
    std::filesystem::create_directories(file.parent_path(), error); if (error) return false;
    const auto part = std::filesystem::path(file.wstring() + L".part");
    const auto result = httpDownload(url, part, bytes, cancel, [](uint64_t) {}, [&](std::wstring_view to) {
        return to.starts_with(L"https://github.com/imLinguin/") || to.starts_with(L"https://release-assets.githubusercontent.com/") ||
               to.starts_with(L"https://objects.githubusercontent.com/");
    });
    if (result != DownloadError::none || cancel()) return false;
    if (!verified(part)) { if (!cancel()) std::filesystem::remove(part, error); return false; }
#ifndef _WIN32
    if (executable) {
        std::filesystem::permissions(part, std::filesystem::perms::owner_all, std::filesystem::perm_options::replace, error);
        if (error) return false;
    }
#endif
    return atomic_file::replace(part, file);
}

// Comet with its folders under `data` and `arguments`; `each` gets every log line (reader thread).
std::shared_ptr<Comet> launch(const std::filesystem::path& exe, const std::filesystem::path& data, const std::vector<std::string>& arguments,
                              std::function<void(std::string_view)> each) {
    std::error_code error;
    for (const auto* folder : {L"local", L"roaming", L"config", L"cache"}) std::filesystem::create_directories(data / folder, error);
    if (error) return {};
#ifdef _WIN32
    // Its folders: comet reads them from LOCALAPPDATA and APPDATA.
    std::map<std::wstring, std::wstring> environment;
    if (auto* original = GetEnvironmentStringsW()) {
        for (const wchar_t* p = original; *p; p += wcslen(p) + 1) {
            const std::wstring entry(p); const auto equals = entry.find(L'=', 1);
            if (equals == entry.npos) continue;
            auto key = entry.substr(0, equals); std::transform(key.begin(), key.end(), key.begin(), towupper);
            environment[key] = entry.substr(equals + 1);
        }
        FreeEnvironmentStringsW(original);
    }
    environment[L"LOCALAPPDATA"] = (data / L"local").wstring();
    environment[L"APPDATA"] = (data / L"roaming").wstring();
    environment[L"COMET_LOG"] = L"info";
    std::vector<wchar_t> block;
    for (const auto& [key, value] : environment) {
        const auto entry = key + L'=' + value; block.insert(block.end(), entry.begin(), entry.end()); block.push_back(0);
    }
    block.push_back(0);
    std::wstring command = command_line::quoteArgument(exe.wstring());
    for (const auto& argument : arguments) command += L' ' + command_line::quoteArgument(text_encoding::wideLenient(argument));
    SECURITY_ATTRIBUTES inherit{sizeof(inherit), nullptr, TRUE};
    Handle log_read, log_write, input(CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &inherit, OPEN_EXISTING, 0, nullptr));
    if (!input || !CreatePipe(&log_read.h, &log_write.h, &inherit, 0)) { SecureZeroMemory(command.data(), command.size() * sizeof(wchar_t)); return {}; }
    SetHandleInformation(log_read.h, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOEXW start{}; start.StartupInfo.cb = sizeof(start); start.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    start.StartupInfo.hStdInput = input.h; start.StartupInfo.hStdOutput = log_write.h; start.StartupInfo.hStdError = log_write.h;
    SIZE_T size = 0; InitializeProcThreadAttributeList(nullptr, 1, 0, &size);
    std::vector<uint8_t> attributes(size);
    start.lpAttributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributes.data());
    if (!InitializeProcThreadAttributeList(start.lpAttributeList, 1, 0, &size)) { SecureZeroMemory(command.data(), command.size() * sizeof(wchar_t)); return {}; }
    HANDLE handles[]{input.h, log_write.h};
    Handle job(CreateJobObjectW(nullptr, nullptr));
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    PROCESS_INFORMATION started{};
    const auto directory = data.wstring();
    const bool created = UpdateProcThreadAttribute(start.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, handles, sizeof(handles), nullptr, nullptr) &&
        job && SetInformationJobObject(job.h, JobObjectExtendedLimitInformation, &limits, sizeof(limits)) &&
        CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, TRUE,
                       CREATE_NO_WINDOW | CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT | EXTENDED_STARTUPINFO_PRESENT,
                       block.data(), directory.c_str(), &start.StartupInfo, &started);
    DeleteProcThreadAttributeList(start.lpAttributeList);
    SecureZeroMemory(command.data(), command.size() * sizeof(wchar_t));
    if (!created) return {};
    Handle process(started.hProcess), thread(started.hThread);
    if (!AssignProcessToJobObject(job.h, process.h)) { TerminateProcess(process.h, 1); WaitForSingleObject(process.h, 5000); return {}; }
    CloseHandle(log_write.h); log_write.h = nullptr;
    ResumeThread(thread.h);
    auto comet = std::make_shared<CometProcess>(job.h, process.h, log_read.h, std::move(each));
    job.h = process.h = log_read.h = nullptr; // Owned by the CometProcess now.
    return comet;
#else
    std::vector<std::string> environment;
    for (char** entry = environ; *entry; ++entry) {
        const std::string_view text(*entry);
        const auto key = text.substr(0, text.find('='));
        if (key != "XDG_DATA_HOME" && key != "XDG_CONFIG_HOME" && key != "XDG_CACHE_HOME" && key != "COMET_LOG") environment.emplace_back(text);
    }
    environment.push_back("XDG_DATA_HOME=" + (data / "local").string());
    environment.push_back("XDG_CONFIG_HOME=" + (data / "config").string());
    environment.push_back("XDG_CACHE_HOME=" + (data / "cache").string());
    environment.push_back("COMET_LOG=info");
    int log[2]{-1, -1};
    if (::pipe2(log, O_CLOEXEC) != 0) return {};
    std::vector<std::string> command{exe.string()};
    command.insert(command.end(), arguments.begin(), arguments.end());
    process_spawn::Options options;
    options.directory = data; options.session = true; options.output = log[1]; options.error = log[1]; options.environment = &environment;
    pid_t pid = 0;
    const int failed = process_spawn::spawn(command, options, pid);
    ::close(log[1]);
    for (auto& part : command) std::fill(part.begin(), part.end(), '\0');
    if (failed) { ::close(log[0]); return {}; }
    return std::make_shared<CometProcess>(pid, log[0], std::move(each));
#endif
}
}

std::filesystem::path prepareComet(const std::filesystem::path& root, const std::function<bool()>& cancel) {
    constexpr auto base = L"https://github.com/imLinguin/comet/releases/download/v0.4.1/";
#if defined(_WIN32) && defined(_M_ARM64)
    constexpr auto digest = "ef57dda0f5f1513a0b9f068818f58f8c29528b46d5290ad01f6260af30e04127";
    constexpr auto asset = L"comet-aarch64-pc-windows-msvc.exe";
    constexpr uint64_t bytes = 11910656;
    const auto bin = root / L"tools" / L"comet-0.4.1.exe";
#elif defined(_WIN32)
    constexpr auto digest = "35c8b1a7d82b9a03839bca9238ed1e68d1c2718ebeb1a0530b26c5f89885b876";
    constexpr auto asset = L"comet-x86_64-pc-windows-msvc.exe";
    constexpr uint64_t bytes = 13716480;
    const auto bin = root / L"tools" / L"comet-0.4.1.exe";
#elif defined(__linux__) && defined(__aarch64__)
    constexpr auto digest = "33c52a7f4747e4e3340b1de955683679e33d01ba1a4fd2f9d79d7eb4dc229fc8";
    constexpr auto asset = L"comet-aarch64-unknown-linux-gnu";
    constexpr uint64_t bytes = 12612400;
    const auto bin = root / L"tools" / L"comet-0.4.1";
#elif defined(__linux__) && defined(__x86_64__)
    constexpr auto digest = "868dd0b510f2c1c63a524b8e0466dd92bb2aecdb697217a690bf9effd04d3a5a";
    constexpr auto asset = L"comet-x86_64-unknown-linux-gnu";
    constexpr uint64_t bytes = 14660192;
    const auto bin = root / L"tools" / L"comet-0.4.1";
#else
    return {};
#endif
    return fetchPinned(std::wstring(base) + asset, bin, bytes, digest, cancel, true) ? bin : std::filesystem::path{};
}

std::vector<std::string> cometArguments(const CometCredentials& credentials) {
    return {"--access-token", credentials.access, "--refresh-token", credentials.refresh,
            "--user-id", credentials.user, "--username", credentials.username.empty() ? credentials.user : credentials.username, "--quit"};
}

std::optional<std::wstring> cometUnlocked(std::string_view line) {
    constexpr std::string_view marker = "Unlocking achievement ";
    const auto at = line.find(marker);
    if (at == line.npos) return {};
    auto rest = line.substr(at + marker.size());
    const auto comma = rest.find(", ");
    auto name = comma == rest.npos ? rest : rest.substr(comma + 2);
    while (!name.empty() && (name.back() == ' ' || name.back() == '\t')) name.remove_suffix(1);
    if (name.empty()) name = rest.substr(0, comma);
    if (name.empty() || name.size() > 512) return {};
    return text_encoding::wideLenient(std::string(name));
}

std::optional<uint32_t> cometSession(std::string_view line) {
    constexpr std::string_view marker = "Requested session start for ";
    const auto at = line.find(marker);
    if (at == line.npos) return {};
    const auto digits = line.substr(at + marker.size());
    uint64_t pid = 0; size_t count = 0;
    for (; count < digits.size() && count < 10 && digits[count] >= '0' && digits[count] <= '9'; ++count) pid = pid * 10 + (digits[count] - '0');
    if (!count || !pid || pid > UINT32_MAX) return {};
    return static_cast<uint32_t>(pid);
}

std::filesystem::path overlayFolder(const std::filesystem::path& data) { return data / L"local" / L"comet" / L"redist"; }

bool overlayInstalled(const std::filesystem::path& data) {
    const auto redist = overlayFolder(data);
    return regular(redist / L"overlay" / L"GalaxyOverlay.exe") && regular(redist / L"web" / L"overlay.html");
}

bool downloadOverlay(const std::filesystem::path& exe, const std::filesystem::path& data, const std::function<bool()>& cancel) {
    if (!regular(exe)) return false;
    // The command needs no account: the values only satisfy comet's argument parser.
    std::vector<std::string> arguments{"--access-token", "-", "--refresh-token", "-", "--user-id", "0", "--username", "launchme", "overlay"};
#ifndef _WIN32
    arguments.push_back("--force"); // GOG has no Linux overlay: the Windows one runs in the game's Wine.
#endif
    auto comet = launch(exe, data, arguments, {});
    if (!comet) return false;
    const auto end = std::chrono::steady_clock::now() + std::chrono::minutes(30);
    while (comet->running()) {
        if (cancel() || std::chrono::steady_clock::now() > end) return false; // Destroying it ends the download.
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }
    return overlayInstalled(data);
}

std::wstring overlayArguments(const std::filesystem::path& redist, uint32_t pid, std::string_view product,
                              const std::filesystem::path& cache, std::wstring_view language) {
    auto page = (redist / L"web" / L"overlay.html").wstring();
    std::replace(page.begin(), page.end(), L'\\', L'/');
    return L"--advanced-features --startoverlay --client-language-code=" + std::wstring(language) +
           L" --startup-url=file://" + page + L" --attached-pid=" + std::to_wstring(pid) +
           L" --product-id=" + text_encoding::wideLenient(std::string(product)) + L" -cache-path=\"" + cache.wstring() + L"\"";
}

std::shared_ptr<Comet> startOverlay(const std::filesystem::path& data, uint32_t pid, std::string_view product, [[maybe_unused]] std::wstring_view language) {
    if (!pid || product.empty() || product.size() > 20 || !std::all_of(product.begin(), product.end(), [](char c) { return c >= '0' && c <= '9'; }) ||
        !overlayInstalled(data)) return {};
#ifdef _WIN32
    const auto redist = overlayFolder(data);
    const auto exe = redist / L"overlay" / L"GalaxyOverlay.exe";
    const auto cache = data / L"overlay-cache" / text_encoding::wideLenient(std::string(product));
    std::error_code error;
    std::filesystem::create_directories(cache, error);
    // The overlay writes its log under GOG's ProgramData folder (as Galaxy leaves it).
    if (wchar_t program_data[MAX_PATH]{}; GetEnvironmentVariableW(L"ProgramData", program_data, MAX_PATH))
        std::filesystem::create_directories(std::filesystem::path(program_data) / L"GOG.com" / L"Galaxy" / L"logs", error);
    std::wstring command = command_line::quoteArgument(exe.wstring()) + L' ' + overlayArguments(redist, pid, product, cache, language);
    const auto directory = (redist / L"overlay").wstring();
    Handle job(CreateJobObjectW(nullptr, nullptr));
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    STARTUPINFOW start{sizeof(start)};
    PROCESS_INFORMATION started{};
    if (!job || !SetInformationJobObject(job.h, JobObjectExtendedLimitInformation, &limits, sizeof(limits)) ||
        !CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_SUSPENDED, nullptr, directory.c_str(), &start, &started)) return {};
    Handle process(started.hProcess), thread(started.hThread);
    if (!AssignProcessToJobObject(job.h, process.h)) { TerminateProcess(process.h, 1); return {}; }
    ResumeThread(thread.h);
    auto overlay = std::make_shared<OverlayProcess>(job.h, process.h);
    job.h = process.h = nullptr;
    return overlay;
#else
    return {};
#endif
}

std::filesystem::path galaxyHelperFolder(const std::filesystem::path& root) { return root / L"tools" / L"galaxy-helper-0.1.0"; }

bool galaxyHelperInstalled(const std::filesystem::path& root) {
    const auto folder = galaxyHelperFolder(root);
    return regular(folder / L"galaxy.exe") && regular(folder / L"libgalaxyunixlib.dll.so");
}

std::filesystem::path prepareGalaxyHelper(const std::filesystem::path& root, const std::function<bool()>& cancel) {
    constexpr auto base = L"https://github.com/imLinguin/galaxy-helper/releases/download/v0.1.0/";
    const auto folder = galaxyHelperFolder(root);
    const bool ok =
        fetchPinned(std::wstring(base) + L"galaxy.exe", folder / L"galaxy.exe", 779538,
                    "82d841ec9e2a78a3655f7535f465404ff7d047a7b868ff27228d85c1233b5302", cancel, false) &&
        fetchPinned(std::wstring(base) + L"libgalaxyunixlib.dll.so", folder / L"libgalaxyunixlib.dll.so", 87296,
                    "0a678bc3f5e6de7e7aa7d9952fd7c2d5c370410d26f68d7fa9e07214626e6be6", cancel, false);
    return ok ? folder : std::filesystem::path{};
}

bool galaxyPortBusy() {
#ifdef _WIN32
    static const bool ready = [] { WSADATA data{}; return WSAStartup(MAKEWORD(2, 2), &data) == 0; }();
    if (!ready) return false;
    const SOCKET probe = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (probe == INVALID_SOCKET) return false;
    BOOL exclusive = TRUE; setsockopt(probe, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&exclusive), sizeof(exclusive));
    sockaddr_in address{}; address.sin_family = AF_INET; address.sin_port = htons(galaxy_port);
    inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);
    const bool busy = bind(probe, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR &&
                      (WSAGetLastError() == WSAEADDRINUSE || WSAGetLastError() == WSAEACCES);
    closesocket(probe);
    return busy;
#else
    const int probe = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (probe < 0) return false;
    sockaddr_in address{}; address.sin_family = AF_INET; address.sin_port = htons(galaxy_port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    const bool busy = ::bind(probe, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0 && errno == EADDRINUSE;
    ::close(probe);
    return busy;
#endif
}

std::shared_ptr<Comet> startComet(const CometRequest& request, const CometCredentials& credentials) {
    if (!regular(request.exe) || credentials.access.empty() || credentials.refresh.empty() || credentials.user.empty() || galaxyPortBusy()) return {};
    return launch(request.exe, request.data, cometArguments(credentials),
        [log = request.log, unlocked = request.unlocked, session = request.session](std::string_view line) {
            if (log) log(line);
            if (const auto name = cometUnlocked(line); name && unlocked) unlocked(*name);
            if (const auto pid = cometSession(line); pid && session) session(*pid);
        });
}
}
