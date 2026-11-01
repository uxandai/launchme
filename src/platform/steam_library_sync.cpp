#include "steam_library_sync.hpp"
#include "ascii_text.hpp"
#include "steam_library_bridge.hpp"
#include "text_encoding.hpp"
#include "../net/json.hpp"
#ifdef _WIN32
#include <WinSock2.h>
#include <Windows.h>
#include <iphlpapi.h>
#include <winhttp.h>
#else
#include "system.hpp"
#include <arpa/inet.h>
#include <dirent.h>
#include <netinet/in.h>
#include <openssl/evp.h>
#include <openssl/sha.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <fstream>
#endif
#include <algorithm>
#include <charconv>
#include <cmath>
#include <memory>

namespace steam_sync {
namespace {
constexpr size_t max_reply = 4 * 1024 * 1024;
#ifdef _WIN32
struct Handle {
    HINTERNET value = nullptr;
    explicit Handle(HINTERNET h = nullptr) : value(h) {}
    ~Handle() { if (value) WinHttpCloseHandle(value); }
    Handle(const Handle&) = delete;
};
#endif
std::string quote(std::string_view text) {
    std::string out = "\"";
    constexpr char hex[] = "0123456789abcdef";
    for (const unsigned char c : text) {
        if (c == '"' || c == '\\') { out += '\\'; out += static_cast<char>(c); }
        else if (c < 32) { out += "\\u00"; out += hex[c >> 4]; out += hex[c & 15]; }
        else out += static_cast<char>(c);
    }
    return out + '"';
}
const json::Value* field(const json::Value* value, std::string_view key) { return value ? value->get(key) : nullptr; }
std::string text(const json::Value* value) { return value ? value->stringOr() : std::string{}; }
bool flag(const json::Value* value) { return value && value->type == json::Value::Type::boolean && value->flag; }
bool ids(const json::Value* value, std::vector<uint32_t>& output) {
    if (!value || value->type != json::Value::Type::array || value->elements.size() > 200000) return false;
    for (const auto& entry : value->elements) {
        if (entry.type != json::Value::Type::number || entry.numeric < 1 || entry.numeric > UINT32_MAX || std::floor(entry.numeric) != entry.numeric) return false;
        output.push_back(static_cast<uint32_t>(entry.numeric));
    }
    return true;
}
#ifdef _WIN32
// A page title/URL alone is not process identity. Admit only a loopback listener of the installed Steam web helper.
bool steamEndpoint(const std::filesystem::path& root) {
    DWORD size = 0;
    GetExtendedTcpTable(nullptr, &size, FALSE, AF_INET, TCP_TABLE_OWNER_PID_LISTENER, 0);
    if (!size || size > 4 * 1024 * 1024) return false;
    std::vector<uint8_t> bytes(size);
    if (GetExtendedTcpTable(bytes.data(), &size, FALSE, AF_INET, TCP_TABLE_OWNER_PID_LISTENER, 0) != NO_ERROR) return false;
    const auto* table = reinterpret_cast<const MIB_TCPTABLE_OWNER_PID*>(bytes.data());
    for (DWORD i = 0; i < table->dwNumEntries; ++i) {
        const auto& row = table->table[i];
        if (row.dwLocalAddr != htonl(INADDR_LOOPBACK) || ntohs(static_cast<u_short>(row.dwLocalPort)) != 8080) continue;
        const auto process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, row.dwOwningPid);
        if (!process) return false;
        wchar_t path[32768]{}; DWORD length = static_cast<DWORD>(std::size(path));
        const bool read = QueryFullProcessImageNameW(process, 0, path, &length); CloseHandle(process);
        auto expected = (root / L"bin" / L"cef" / L"cef.win64" / L"steamwebhelper.exe").lexically_normal();
        expected.make_preferred();
        return read && _wcsicmp(path, expected.c_str()) == 0;
    }
    return false;
}
std::optional<std::string> targets(HINTERNET connection) {
    Handle request(WinHttpOpenRequest(connection, L"GET", L"/json/list", nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0));
    if (!request.value || !WinHttpSendRequest(request.value, nullptr, 0, nullptr, 0, 0, 0) || !WinHttpReceiveResponse(request.value, nullptr)) return {};
    DWORD status = 0, size = sizeof(status);
    if (!WinHttpQueryHeaders(request.value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, nullptr, &status, &size, nullptr) || status != 200) return {};
    std::string result; char bytes[8192]; DWORD read = 0;
    do {
        if (!WinHttpReadData(request.value, bytes, sizeof(bytes), &read) || result.size() + read > max_reply) return {};
        result.append(bytes, read);
    } while (read);
    return result;
}
#else
// The socket inode of 127.0.0.1:8080's listener (/proc/net/tcp: hex address:port, state 0A = LISTEN).
std::optional<unsigned long> listenerInode() {
    std::ifstream table("/proc/net/tcp");
    std::string line;
    std::getline(table, line); // Header.
    while (std::getline(table, line)) {
        char local[64]{}, remote[64]{}; unsigned state = 0; unsigned long inode = 0;
        if (std::sscanf(line.c_str(), " %*u: %63s %63s %x %*s %*s %*s %*u %*u %lu", local, remote, &state, &inode) != 4) continue;
        if (std::string_view(local) == "0100007F:1F90" && state == 0x0A && inode) return inode;
    }
    return std::nullopt;
}
// A page title/URL alone is not process identity. Admit only a loopback listener of the installed Steam web helper:
// the process holding that socket must run <root>/ubuntu12_64/steamwebhelper.
bool steamEndpoint(const std::filesystem::path& root) {
    const auto inode = listenerInode();
    if (!inode) return false;
    std::error_code error;
    const auto expected = std::filesystem::weakly_canonical(root / "ubuntu12_64" / "steamwebhelper", error);
    if (error) return false;
    const auto socket = "socket:[" + std::to_string(*inode) + "]";
    DIR* processes = opendir("/proc");
    if (!processes) return false;
    bool found = false, admitted = false;
    while (const dirent* process = found ? nullptr : readdir(processes)) {
        const std::string_view pid = process->d_name;
        if (pid.empty() || pid.find_first_not_of("0123456789") != std::string_view::npos) continue;
        const auto folder = std::filesystem::path("/proc") / std::string(pid);
        for (std::filesystem::directory_iterator it(folder / "fd", error), end; !error && it != end && !found; it.increment(error)) {
            std::error_code link_error;
            if (std::filesystem::read_symlink(it->path(), link_error).native() != socket) continue;
            found = true;
            const auto executable = std::filesystem::read_symlink(folder / "exe", link_error);
            admitted = !link_error && std::filesystem::weakly_canonical(executable, link_error) == expected && !link_error;
        }
        error.clear(); // Other users' processes: not readable.
    }
    closedir(processes);
    return admitted;
}
// One connection to 127.0.0.1:8080 (Steam's CEF debugging endpoint): plain HTTP and a WebSocket.
class Loopback {
public:
    Loopback() {
        socket_ = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, IPPROTO_TCP);
        if (socket_ < 0) return;
        const timeval wait{3, 0};
        setsockopt(socket_, SOL_SOCKET, SO_SNDTIMEO, &wait, sizeof(wait));
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = htons(8080);
        if (connect(socket_, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) { ::close(socket_); socket_ = -1; }
    }
    ~Loopback() { if (socket_ >= 0) ::close(socket_); }
    Loopback(const Loopback&) = delete;
    Loopback& operator=(const Loopback&) = delete;
    bool connected() const { return socket_ >= 0; }

    // GET `path`; the body when the status is 200.
    std::optional<std::string> get(std::string_view path) {
        if (!sendAll("GET " + std::string(path) + " HTTP/1.1\r\nHost: 127.0.0.1:8080\r\nConnection: close\r\n\r\n")) return {};
        const auto head = readHead(GetTickCount64() + 3000);
        if (!head || !head->starts_with("HTTP/1.1 200")) return {};
        std::string body = std::move(pending_);
        const auto length = contentLength(*head);
        const auto deadline = GetTickCount64() + 3000;
        while (!length || body.size() < *length) {
            if (body.size() > max_reply) return {};
            const auto got = readSome(body, deadline);
            if (got < 0) return {};
            if (got == 0) break; // Closed: the whole body without a Content-Length.
        }
        if (length && body.size() < *length) return {};
        if (length) body.resize(*length);
        return body;
    }
    // Upgrades this connection (RFC 6455) and checks the server's Sec-WebSocket-Accept.
    bool upgrade(std::string_view path) {
        uint8_t nonce[16]{};
        if (!system_info::randomBytes(nonce, sizeof(nonce))) return false;
        const auto key = base64(nonce, sizeof(nonce));
        if (!sendAll("GET " + std::string(path) + " HTTP/1.1\r\nHost: 127.0.0.1:8080\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                     "Sec-WebSocket-Key: " + key + "\r\nSec-WebSocket-Version: 13\r\n\r\n")) return false;
        const auto head = readHead(GetTickCount64() + 3000);
        if (!head || !head->starts_with("HTTP/1.1 101")) return false;
        const auto expected = key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
        uint8_t digest[SHA_DIGEST_LENGTH]{};
        SHA1(reinterpret_cast<const unsigned char*>(expected.data()), expected.size(), digest);
        return header(*head, "sec-websocket-accept") == base64(digest, sizeof(digest));
    }
    // One masked text frame.
    bool sendText(std::string_view payload) {
        std::string frame(1, static_cast<char>(0x81));
        if (payload.size() < 126) frame += static_cast<char>(0x80 | payload.size());
        else if (payload.size() <= 0xFFFF) { frame += static_cast<char>(0x80 | 126); for (int shift = 8; shift >= 0; shift -= 8) frame += static_cast<char>(payload.size() >> shift); }
        else { frame += static_cast<char>(0x80 | 127); for (int shift = 56; shift >= 0; shift -= 8) frame += static_cast<char>(static_cast<uint64_t>(payload.size()) >> shift); }
        uint8_t mask[4]{};
        if (!system_info::randomBytes(mask, sizeof(mask))) return false;
        frame.append(reinterpret_cast<const char*>(mask), 4);
        for (size_t i = 0; i < payload.size(); ++i) frame += static_cast<char>(payload[i] ^ mask[i % 4]);
        return sendAll(frame);
    }
    // The next complete text message (continuations joined, pings answered); nullopt on close, error or timeout.
    std::optional<std::string> receiveText(uint64_t deadline) {
        std::string message;
        while (true) {
            uint8_t head[2]{};
            if (!readExact(head, 2, deadline)) return {};
            const bool final = head[0] & 0x80, masked = head[1] & 0x80;
            const int opcode = head[0] & 0x0F;
            uint64_t length = head[1] & 0x7F;
            if (length >= 126) {
                uint8_t bytes[8]{};
                const size_t count = length == 126 ? 2 : 8;
                if (!readExact(bytes, count, deadline)) return {};
                length = 0;
                for (size_t i = 0; i < count; ++i) length = length << 8 | bytes[i];
            }
            uint8_t mask[4]{};
            if (masked && !readExact(mask, 4, deadline)) return {};
            if (length > max_reply || message.size() + length > max_reply) return {};
            std::string payload(static_cast<size_t>(length), '\0');
            if (length && !readExact(payload.data(), payload.size(), deadline)) return {};
            if (masked) for (size_t i = 0; i < payload.size(); ++i) payload[i] = static_cast<char>(payload[i] ^ mask[i % 4]);
            if (opcode == 0x8) return {};
            if (opcode == 0x9) { if (!sendControl(0xA, payload)) return {}; continue; }
            if (opcode == 0xA) continue;
            if (opcode != 0x0 && opcode != 0x1) return {}; // Binary messages are not CDP replies.
            message += payload;
            if (final) return message;
        }
    }
    void close() { sendControl(0x8, std::string_view("\x03\xE8", 2)); }

private:
    int socket_ = -1;
    std::string pending_; // Bytes read past the HTTP head.

    static std::string base64(const uint8_t* data, size_t size) {
        std::string out(4 * ((size + 2) / 3) + 1, '\0');
        const int length = EVP_EncodeBlock(reinterpret_cast<unsigned char*>(out.data()), data, static_cast<int>(size));
        out.resize(length > 0 ? static_cast<size_t>(length) : 0);
        return out;
    }
    static std::string lower(std::string_view text) { return ascii_text::lower(text); }
    static std::string header(std::string_view head, std::string_view name) {
        for (size_t at = head.find("\r\n"); at != std::string_view::npos;) {
            const auto end = head.find("\r\n", at + 2);
            const auto line = head.substr(at + 2, end == std::string_view::npos ? std::string_view::npos : end - at - 2);
            if (const auto colon = line.find(':'); colon != std::string_view::npos && lower(line.substr(0, colon)) == name) {
                auto value = line.substr(colon + 1);
                while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) value.remove_prefix(1);
                while (!value.empty() && (value.back() == ' ' || value.back() == '\t')) value.remove_suffix(1);
                return std::string(value);
            }
            at = end;
        }
        return {};
    }
    static std::optional<size_t> contentLength(std::string_view head) {
        const auto value = header(head, "content-length");
        size_t length = 0;
        const auto [end, failure] = std::from_chars(value.data(), value.data() + value.size(), length);
        if (value.empty() || failure != std::errc{} || end != value.data() + value.size()) return std::nullopt;
        return length;
    }
    bool sendAll(std::string_view data) {
        while (!data.empty()) {
            const auto sent = ::send(socket_, data.data(), data.size(), MSG_NOSIGNAL);
            if (sent < 0 && errno == EINTR) continue;
            if (sent <= 0) return false;
            data.remove_prefix(static_cast<size_t>(sent));
        }
        return true;
    }
    bool sendControl(int opcode, std::string_view payload) {
        std::string frame{static_cast<char>(0x80 | opcode), static_cast<char>(0x80 | payload.size()), 0, 0, 0, 0};
        frame.append(payload); // Zero mask: the payload is sent as is.
        return sendAll(frame);
    }
    // Appends what arrives before `deadline` (GetTickCount64): >0 bytes, 0 closed, -1 error or timeout.
    long readSome(std::string& out, uint64_t deadline) {
        while (true) {
            const auto now = GetTickCount64();
            if (now >= deadline) return -1;
            pollfd item{socket_, POLLIN, 0};
            const int ready = poll(&item, 1, static_cast<int>(std::min<uint64_t>(deadline - now, 1000)));
            if (ready < 0 && errno != EINTR) return -1;
            if (ready <= 0) continue;
            char buffer[16384];
            const auto got = ::recv(socket_, buffer, sizeof(buffer), 0);
            if (got < 0 && errno == EINTR) continue;
            if (got < 0) return -1;
            out.append(buffer, static_cast<size_t>(got));
            return static_cast<long>(got);
        }
    }
    bool readExact(void* data, size_t size, uint64_t deadline) {
        while (pending_.size() < size) if (readSome(pending_, deadline) <= 0) return false;
        std::copy_n(pending_.data(), size, static_cast<char*>(data));
        pending_.erase(0, size);
        return true;
    }
    std::optional<std::string> readHead(uint64_t deadline) {
        while (true) {
            if (const auto end = pending_.find("\r\n\r\n"); end != std::string::npos) {
                auto head = pending_.substr(0, end);
                pending_.erase(0, end + 4);
                return head;
            }
            if (pending_.size() > 64 * 1024 || readSome(pending_, deadline) <= 0) return std::nullopt;
        }
    }
};
#endif
}
std::string requestExpression(uint64_t account, const std::vector<Change>& changes) {
    std::string request = "{\"account\":" + quote(std::to_string(account)) + ",\"changes\":[";
    for (size_t i = 0; i < changes.size(); ++i) {
        if (i) request += ',';
        const auto& c = changes[i];
        request += "{\"kind\":" + quote(c.kind == Kind::name ? "name" : c.kind == Kind::hidden ? "hidden" : c.kind == Kind::create ? "create" : "member") +
            ",\"collection\":" + quote(c.collection) + ",\"app\":" + std::to_string(c.app) + ",\"value\":" + (c.value ? "true" : "false") +
            ",\"name\":" + quote(text_encoding::utf8(c.name)) + '}';
    }
    return "(" + std::string(steam_library_bridge) + ")(" + request + "]}).then(JSON.stringify)";
}
Result parseResult(uint64_t account, const std::vector<Change>& changes, std::string_view source) {
    Result out; out.account = account;
    const auto value = json::parse(source, max_reply);
    if (!value) { out.error = "invalid-reply"; return out; }
    if (!flag(value->get("ok"))) { out.error = text(value->get("error")); if (out.error.empty()) out.error = "invalid-reply"; return out; }
    if (text(value->get("account")) != std::to_string(account)) { out.error = "account-changed"; return out; }
    Snapshot snapshot; snapshot.account = account;
    const auto* collections = value->get("collections");
    if (!collections || collections->type != json::Value::Type::array || collections->elements.size() > 10000) { out.error = "invalid-reply"; return out; }
    std::set<std::string> seen;
    for (const auto& item : collections->elements) {
        SteamCollection c; c.id = text(item.get("id")); c.name = text_encoding::wide(text(item.get("name")));
        if (!c.id.starts_with("user-collections.") || c.id == "user-collections.hidden" || c.id.size() > 1024 || c.name.empty() ||
            !seen.insert(c.id).second || !ids(item.get("members"), c.app_ids)) { out.error = "invalid-reply"; return out; }
        if (c.id == "user-collections.favorite") c.name = tr(L"Ulubione");
        std::sort(c.app_ids.begin(), c.app_ids.end());
        c.app_ids.erase(std::unique(c.app_ids.begin(), c.app_ids.end()), c.app_ids.end());
        snapshot.collections.push_back(std::move(c));
    }
    std::vector<uint32_t> hidden, private_apps;
    if (!ids(value->get("hidden"), hidden) || !ids(value->get("privateApps"), private_apps)) { out.error = "invalid-reply"; return out; }
    snapshot.hidden.insert(hidden.begin(), hidden.end()); snapshot.private_apps.insert(private_apps.begin(), private_apps.end());
    const auto* ack = value->get("ack");
    if (!ack || ack->type != json::Value::Type::array) { out.error = "invalid-reply"; return out; }
    std::set<size_t> applied;
    for (const auto& index : ack->elements) {
        if (index.type != json::Value::Type::number || index.numeric < 0 || index.numeric >= changes.size() || std::floor(index.numeric) != index.numeric) {
            out.error = "invalid-reply"; out.applied.clear(); return out;
        }
        const auto at = static_cast<size_t>(index.numeric);
        if (!applied.insert(at).second) { out.error = "invalid-reply"; out.applied.clear(); return out; }
        out.applied.push_back(changes[at]);
    }
    if (const auto* unknown = value->get("unavailable")) {
        if (unknown->type != json::Value::Type::array) { out.error = "invalid-reply"; out.applied.clear(); return out; }
        for (const auto& index : unknown->elements) {
            if (index.type != json::Value::Type::number || index.numeric < 0 || index.numeric >= changes.size() || std::floor(index.numeric) != index.numeric ||
                !applied.insert(static_cast<size_t>(index.numeric)).second) {
                out.error = "invalid-reply"; out.applied.clear(); out.unavailable.clear(); return out;
            }
            const auto& change = changes[static_cast<size_t>(index.numeric)];
            if (change.kind != Kind::hidden && change.kind != Kind::member) { out.error = "invalid-reply"; out.applied.clear(); out.unavailable.clear(); return out; }
            out.unavailable.push_back(change);
        }
    }
    out.snapshot = std::move(snapshot); out.error = text(value->get("error")); out.cloud_pending = flag(value->get("cloudPending"));
    return out;
}
#ifdef _WIN32
Result exchange(const std::filesystem::path& root, uint64_t account, const std::vector<Change>& changes) {
    Result failed; failed.account = account; failed.error = "unavailable";
    if (!root.is_absolute() || account <= 76561197960265728ULL || changes.size() > 16 || !steamEndpoint(root)) return failed;
    Handle session(WinHttpOpen(L"Launchme Steam library sync", WINHTTP_ACCESS_TYPE_NO_PROXY, nullptr, nullptr, 0));
    if (!session.value) return failed;
    WinHttpSetTimeouts(session.value, 1500, 1500, 3000, 10000);
    Handle connection(WinHttpConnect(session.value, L"127.0.0.1", 8080, 0));
    const auto listing = connection.value ? targets(connection.value) : std::nullopt;
    const auto parsed = listing ? json::parse(*listing, max_reply) : std::nullopt;
    if (!parsed || parsed->type != json::Value::Type::array) return failed;
    std::string path;
    for (const auto& page : parsed->elements) {
        if (text(page.get("title")) != "SharedJSContext" || !text(page.get("url")).starts_with("https://steamloopback.host/")) continue;
        const auto id = text(page.get("id"));
        if (id.empty() || id.size() > 128 || id.find_first_not_of("0123456789abcdefABCDEF-") != id.npos || !path.empty()) return failed;
        path = "/devtools/page/" + id;
    }
    if (path.empty()) return failed;
    Handle request(WinHttpOpenRequest(connection.value, L"GET", text_encoding::wide(path).c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0));
    if (!request.value || !WinHttpSetOption(request.value, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, nullptr, 0) ||
        !WinHttpSendRequest(request.value, nullptr, 0, nullptr, 0, 0, 0) || !WinHttpReceiveResponse(request.value, nullptr)) return failed;
    Handle socket(WinHttpWebSocketCompleteUpgrade(request.value, 0));
    if (!socket.value) return failed;
    DWORD timeout = 10000; WinHttpSetOption(socket.value, WINHTTP_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));
    const auto payload = "{\"id\":1,\"method\":\"Runtime.evaluate\",\"params\":{\"expression\":" + quote(requestExpression(account, changes)) +
        ",\"returnByValue\":true,\"awaitPromise\":true,\"timeout\":9000}}";
    if (payload.size() > max_reply || WinHttpWebSocketSend(socket.value, WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE,
        const_cast<char*>(payload.data()), static_cast<DWORD>(payload.size())) != NO_ERROR) return failed;
    std::string message;
    const auto deadline = GetTickCount64() + 10000;
    while (GetTickCount64() < deadline) {
        char buffer[16384]; DWORD size = 0; WINHTTP_WEB_SOCKET_BUFFER_TYPE kind;
        if (WinHttpWebSocketReceive(socket.value, buffer, sizeof(buffer), &size, &kind) != NO_ERROR || kind == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE || message.size() + size > max_reply) return failed;
        message.append(buffer, size);
        if (kind != WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE) continue;
        const auto reply = json::parse(message, max_reply);
        if (!reply) return failed;
        if (reply->get("id") && reply->get("id")->intOr() == 1) {
            const auto* result = reply->get("result");
            if (reply->get("error") || field(result, "exceptionDetails")) { failed.error = "unsupported"; return failed; }
            // stringify inside CDP keeps the existing parser simple and preserves integer IDs exactly.
            const auto* body = field(field(result, "result"), "value");
            if (!body) return failed;
            // The bridge result is embedded JSON. Locate it by asking the expression to return JSON text instead.
            if (body->type != json::Value::Type::string) { failed.error = "invalid-reply"; return failed; }
            WinHttpWebSocketClose(socket.value, WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS, nullptr, 0);
            return parseResult(account, changes, body->text);
        }
        message.clear();
    }
    return failed;
}
#else
Result exchange(const std::filesystem::path& root, uint64_t account, const std::vector<Change>& changes) {
    Result failed; failed.account = account; failed.error = "unavailable";
    if (!root.is_absolute() || account <= 76561197960265728ULL || changes.size() > 16 || !steamEndpoint(root)) return failed;
    std::optional<std::string> listing;
    { Loopback connection; if (connection.connected()) listing = connection.get("/json/list"); }
    const auto parsed = listing ? json::parse(*listing, max_reply) : std::nullopt;
    if (!parsed || parsed->type != json::Value::Type::array) return failed;
    std::string path;
    for (const auto& page : parsed->elements) {
        if (text(page.get("title")) != "SharedJSContext" || !text(page.get("url")).starts_with("https://steamloopback.host/")) continue;
        const auto id = text(page.get("id"));
        if (id.empty() || id.size() > 128 || id.find_first_not_of("0123456789abcdefABCDEF-") != id.npos || !path.empty()) return failed;
        path = "/devtools/page/" + id;
    }
    if (path.empty()) return failed;
    Loopback socket;
    if (!socket.connected() || !socket.upgrade(path)) return failed;
    const auto payload = "{\"id\":1,\"method\":\"Runtime.evaluate\",\"params\":{\"expression\":" + quote(requestExpression(account, changes)) +
        ",\"returnByValue\":true,\"awaitPromise\":true,\"timeout\":9000}}";
    if (payload.size() > max_reply || !socket.sendText(payload)) return failed;
    const auto deadline = GetTickCount64() + 10000;
    while (GetTickCount64() < deadline) {
        const auto message = socket.receiveText(deadline);
        if (!message) return failed;
        const auto reply = json::parse(*message, max_reply);
        if (!reply) return failed;
        if (!reply->get("id") || reply->get("id")->intOr() != 1) continue; // CDP events.
        const auto* result = reply->get("result");
        if (reply->get("error") || field(result, "exceptionDetails")) { failed.error = "unsupported"; return failed; }
        // The bridge returns JSON text (stringified in the page): integer IDs stay exact.
        const auto* body = field(field(result, "result"), "value");
        if (!body) return failed;
        if (body->type != json::Value::Type::string) { failed.error = "invalid-reply"; return failed; }
        socket.close();
        return parseResult(account, changes, body->text);
    }
    return failed;
}
#endif
}
