// Linux side of discord.hpp: /proc and the clients' IPC sockets (only connected to and closed,
// nothing is sent). discord.cpp is the Windows one.
#include "discord.hpp"
#include "system.hpp"

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <string>
#include <vector>

namespace discord {
namespace {
std::filesystem::path environmentPath(const wchar_t* name) {
    return std::filesystem::path(system_info::environment(name, 4095).value_or(L""));
}

std::string firstLine(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    std::string line;
    std::getline(input, line);
    return line;
}

bool listening(const std::filesystem::path& socket_path) {
    const auto text = socket_path.string();
    sockaddr_un address{};
    if (text.size() >= sizeof(address.sun_path)) return false;
    address.sun_family = AF_UNIX;
    text.copy(address.sun_path, text.size());
    const int handle = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (handle < 0) return false;
    // A stale socket file left by a crashed client refuses the connection.
    const bool connected = connect(handle, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0;
    close(handle);
    return connected;
}

// program: a chosen client, known by its file name (the kernel keeps 15 characters of it); empty: Discord's own.
bool processRunning(const std::filesystem::path& program) {
    std::string wanted = program.filename().string().substr(0, 15);
    for (auto& c : wanted) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    std::error_code error;
    for (std::filesystem::directory_iterator entry("/proc", error), end; !error && entry != end; entry.increment(error)) {
        const auto name = entry->path().filename().string();
        if (name.empty() || !std::isdigit(static_cast<unsigned char>(name[0]))) continue;
        auto comm = firstLine(entry->path() / "comm");
        if (wanted.empty()) { if (clientProcessName(comm)) return true; continue; }
        while (!comm.empty() && (comm.back() == '\n' || comm.back() == '\r')) comm.pop_back();
        for (auto& c : comm) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (comm == wanted) return true;
    }
    return false;
}

bool executable(const std::filesystem::path& path) {
    std::error_code error;
    return std::filesystem::is_regular_file(path, error) && access(path.c_str(), X_OK) == 0;
}

// A desktop file under <data>/applications that declares the discord:// scheme.
bool schemeHandler(const std::filesystem::path& data) {
    std::error_code error;
    for (std::filesystem::directory_iterator entry(data / "applications", error), end; !error && entry != end; entry.increment(error)) {
        if (entry->path().extension() != ".desktop") continue;
        std::ifstream input(entry->path(), std::ios::binary);
        std::string line;
        while (std::getline(input, line))
            if (line.starts_with("MimeType=") && line.find("x-scheme-handler/discord;") != std::string::npos) return true;
    }
    return false;
}
} // namespace

bool clientProcessName(std::string_view comm) {
    while (!comm.empty() && (comm.back() == '\n' || comm.back() == '\r')) comm.remove_suffix(1);
    std::string lower(comm);
    for (auto& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return lower == "discord" || lower == "discordptb" || lower == "discordcanary";
}

bool ipcListening(const std::filesystem::path& runtime) {
    if (runtime.empty()) return false;
    for (const auto& folder : {runtime, runtime / "app" / "com.discordapp.Discord", runtime / "app" / "com.discordapp.DiscordCanary",
                               runtime / "snap.discord"})
        for (int index = 0; index < 10; ++index)
            if (listening(folder / ("discord-ipc-" + std::to_string(index)))) return true;
    return false;
}

bool running(const std::filesystem::path& program) { return processRunning(program) || ipcListening(environmentPath(L"XDG_RUNTIME_DIR")); }

bool focus(const std::filesystem::path&) { return false; }

std::optional<GameAction> launchAction() {
    const auto start = [](const std::filesystem::path& path) {
        return GameAction{path.wstring(), {}, path.parent_path(), ActionKind::open_client};
    };
    // Stable first, then PTB and Canary: PATH (distribution packages, the official tarball's link),
    // the tarball's own folders, the Flatpak's exported launchers, then the Snap.
    const auto path = system_info::environment(L"PATH").value_or(L"/usr/local/bin:/usr/bin:/bin");
    std::vector<std::filesystem::path> folders;
    for (size_t at = 0; at <= path.size();) {
        const auto end = std::min(path.find(L':', at), path.size());
        if (end > at) folders.emplace_back(path.substr(at, end - at));
        at = end + 1;
    }
    for (const auto* name : {L"discord", L"Discord", L"discord-ptb", L"DiscordPTB", L"discord-canary", L"DiscordCanary"})
        for (const auto& folder : folders)
            if (executable(folder / name)) return start(folder / name);
    for (const auto* candidate : {L"/opt/discord/Discord", L"/opt/Discord/Discord", L"/usr/share/discord/Discord",
                                  L"/opt/discord-ptb/DiscordPTB", L"/usr/share/discord-ptb/DiscordPTB",
                                  L"/opt/discord-canary/DiscordCanary", L"/usr/share/discord-canary/DiscordCanary"})
        if (executable(candidate)) return start(candidate);
    const auto home = environmentPath(L"HOME");
    auto data_home = environmentPath(L"XDG_DATA_HOME");
    if (data_home.empty() && !home.empty()) data_home = home / ".local" / "share";
    std::vector<std::filesystem::path> data{data_home};
    const auto dirs = system_info::environment(L"XDG_DATA_DIRS").value_or(L"/usr/local/share:/usr/share");
    for (size_t at = 0; at <= dirs.size();) {
        const auto end = std::min(dirs.find(L':', at), dirs.size());
        if (end > at) data.emplace_back(dirs.substr(at, end - at));
        at = end + 1;
    }
    for (const auto& root : {data_home / "flatpak", std::filesystem::path("/var/lib/flatpak")})
        for (const auto* id : {L"com.discordapp.Discord", L"com.discordapp.DiscordCanary"})
            if (!root.empty() && executable(root / "exports" / "bin" / id)) return start(root / "exports" / "bin" / id);
    if (executable("/snap/bin/discord")) return start("/snap/bin/discord");
    for (const auto& root : data)
        if (!root.empty() && schemeHandler(root)) return GameAction{L"discord://", {}, {}, ActionKind::open_client};
    return std::nullopt;
}
} // namespace discord
