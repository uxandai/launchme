// The portable half of http.hpp: budgets, cancellation, the address rules and every public call.
// The request itself is the platform transport's (http_internal.hpp).
#include "http_internal.hpp"
#include "../platform/atomic_file.hpp"
#include "../platform/unicode_text.hpp"
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#endif

#include <algorithm>
#include <chrono>
#include <fstream>
#include <limits>
#include <cwctype>

namespace {
thread_local uint64_t worker_deadline = std::numeric_limits<uint64_t>::max();
thread_local const std::atomic_bool* worker_cancelled = nullptr;
} // namespace

namespace http_detail {
uint64_t now() {
#ifdef _WIN32
    return GetTickCount64();
#else
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
#endif
}
uint64_t workerDeadline() { return worker_deadline; }
std::mutex& steamGridDbMutex() { static std::mutex value; return value; }
ArtworkLimit& steamGridDbLimit() { static ArtworkLimit value; return value; }
bool steamGridDbApi(std::wstring_view host, std::wstring_view path) {
    return (unicode_text::equalIgnoreCase(host, L"www.steamgriddb.com") || unicode_text::equalIgnoreCase(host, L"steamgriddb.com")) &&
           path.starts_with(L"/api/");
}

bool publicAddress(const void* data) {
    const auto* address = static_cast<const sockaddr*>(data);
    if (address->sa_family == AF_INET) {
        const auto ip = ntohl(reinterpret_cast<const sockaddr_in*>(address)->sin_addr.s_addr);
        const auto a = ip >> 24, b = (ip >> 16) & 255;
        return a != 0 && a != 10 && a != 127 && a < 224 && !(a == 169 && b == 254) &&
               !(a == 172 && b >= 16 && b <= 31) && !(a == 192 && (b == 168 || b == 0)) &&
               !(a == 100 && b >= 64 && b <= 127) && !(a == 198 && (b == 18 || b == 19));
    }
    if (address->sa_family == AF_INET6) {
        const auto& ip = reinterpret_cast<const sockaddr_in6*>(address)->sin6_addr;
        // Global unicast only; mapped IPv4, local, multicast and translation prefixes excluded.
        return (ip.s6_addr[0] & 0xe0) == 0x20;
    }
    return false;
}
} // namespace http_detail

using namespace http_detail;

HttpBudget::HttpBudget(uint32_t milliseconds) : previous_(worker_deadline) {
    worker_deadline = std::min(worker_deadline, now() + milliseconds);
}
HttpBudget::~HttpBudget() { worker_deadline = previous_; }
HttpCancellation::HttpCancellation(const std::atomic_bool& cancelled) : previous_(worker_cancelled) { worker_cancelled = &cancelled; }
HttpCancellation::~HttpCancellation() { worker_cancelled = previous_; }
bool httpCancelled() { return worker_cancelled && worker_cancelled->load(); }
uint32_t steamGridDbWaitSeconds() {
    std::lock_guard lock(steamGridDbMutex());
    const auto current = now();
    const auto blocked = steamGridDbLimit().blocked;
    return blocked > current ? static_cast<uint32_t>((blocked - current + 999) / 1000) : 0;
}

bool lanHost(std::wstring_view host) {
    if (host.empty()) return false;
    if (host == L"127.0.0.1" || unicode_text::equalIgnoreCase(host, L"localhost")) return true;
    if (host.find(L':') != std::wstring_view::npos) {
        // IPv6 literals (with or without the URL's brackets): loopback, link-local fe80::/10 and
        // unique-local fc00::/7 only; every other address is public.
        if (host.starts_with(L'[') && host.ends_with(L']')) host = host.substr(1, host.size() - 2);
        std::wstring address(host);
        for (auto& ch : address) ch = static_cast<wchar_t>(std::towlower(ch));
        if (address == L"::1") return true;
        const auto group = std::wstring_view(address).substr(0, address.find(L':'));
        return group.size() == 4 && (group.starts_with(L"fc") || group.starts_with(L"fd") || group.starts_with(L"fe8") ||
                                     group.starts_with(L"fe9") || group.starts_with(L"fea") || group.starts_with(L"feb"));
    }
    // Private IPv4 literals: 10/8, 172.16/12, 192.168/16, 169.254/16.
    int parts[4]{}, count = 0;
    size_t at = 0;
    for (; count < 4 && at <= host.size(); ++count) {
        const auto dot = host.find(L'.', at);
        const auto piece = host.substr(at, dot == std::wstring_view::npos ? std::wstring_view::npos : dot - at);
        if (piece.empty() || piece.size() > 3 || !std::all_of(piece.begin(), piece.end(), [](wchar_t ch) { return ch >= L'0' && ch <= L'9'; })) { count = -1; break; }
        parts[count] = std::stoi(std::wstring(piece));
        if (parts[count] > 255) { count = -1; break; }
        if (dot == std::wstring_view::npos) { ++count; break; }
        if (count == 3) { count = -1; break; } // More after four numbers: "10.0.0.1.example.com" is a public name.
        at = dot + 1;
    }
    if (count == 4) return parts[0] == 10 || parts[0] == 127 || (parts[0] == 172 && parts[1] >= 16 && parts[1] <= 31) ||
                           (parts[0] == 192 && parts[1] == 168) || (parts[0] == 169 && parts[1] == 254);
    if (count == -1 && host.find(L'.') != std::wstring_view::npos && host.find_first_not_of(L"0123456789.") == std::wstring_view::npos) return false;
    // A machine's name on the home network: one label ("nas") or .local / .lan / .home.arpa.
    std::wstring lower(host);
    for (auto& ch : lower) ch = static_cast<wchar_t>(std::towlower(ch));
    return lower.find(L'.') == std::wstring::npos || lower.ends_with(L".local") || lower.ends_with(L".lan") || lower.ends_with(L".home.arpa");
}

HttpResponse httpGet(const std::wstring& url, const std::wstring& headers, size_t max_bytes) { return request(url, headers, max_bytes, false); }
HttpResponse httpGetPage(const std::wstring& url, size_t max_bytes) {
    HttpBudget budget(12000);
    auto current = url;
    for (int hop = 0; hop < 4 && !httpCancelled(); ++hop) {
        auto response = request(current, L"Accept: text/html, application/xhtml+xml, application/json, text/plain\r\n", max_bytes, true,
                                nullptr, 12000, {}, 4000, 0, false, false, true);
        if (response.status != 301 && response.status != 302 && response.status != 303 && response.status != 307 && response.status != 308) return response;
        if (response.location.empty()) return response;
        current = combineUrl(current, response.location);
        if (current.empty()) return {};
        // The next iteration revalidates every destination before requesting it.
    }
    return {};
}
HttpResponse httpGetPrivate(const std::wstring& url, const std::wstring& headers, size_t max_bytes) { return request(url, headers, max_bytes, true); }
HttpResponse httpPostPrivate(const std::wstring& url, const std::wstring& headers, const std::string& body, size_t max_bytes) { return request(url, headers, max_bytes, true, &body); }
HttpResponse httpPostLong(const std::wstring& url, const std::wstring& headers, const std::string& body, size_t max_bytes, uint32_t timeout_ms) {
    return request(url, headers, max_bytes, true, &body, timeout_ms, {}, static_cast<int>(std::min<uint32_t>(timeout_ms, 120000)));
}

HttpResponse httpPostStream(const std::wstring& url, const std::wstring& headers, const std::string& body, size_t max_bytes, uint32_t timeout_ms,
                            const std::function<void(const uint8_t*, size_t)>& piece) {
    std::vector<uint8_t> received;
    auto response = request(url, headers, max_bytes, true, &body, timeout_ms, [&](uint32_t, const uint8_t* data, size_t size) {
        received.insert(received.end(), data, data + size);
        if (piece) piece(data, size);
        return true;
    }, static_cast<int>(std::min<uint32_t>(timeout_ms, 120000)), 0, true);
    if (response.status == 200) response.body = std::move(received);
    return response;
}

HttpResponse httpLocal(const std::wstring& url, const std::wstring& headers, const std::string* body, size_t max_bytes, uint32_t timeout_ms) {
    return request(url, headers, max_bytes, true, body, timeout_ms, {}, static_cast<int>(std::min<uint32_t>(timeout_ms, 120000)), 1);
}

HttpResponse httpService(const std::wstring& url, const std::wstring& headers, const std::string* body, size_t max_bytes, uint32_t timeout_ms) {
    return request(url, headers, max_bytes, true, body, timeout_ms, {}, static_cast<int>(std::min<uint32_t>(timeout_ms, 180000)), 2);
}

HttpResponse httpApi(const std::wstring& url, const std::wstring& headers, const std::string* body, size_t max_bytes, uint32_t timeout_ms) {
    return request(url, headers, max_bytes, true, body, timeout_ms, {}, static_cast<int>(std::min<uint32_t>(timeout_ms, 120000)), 0, false, true);
}

HttpResponse httpGetPrefix(const std::wstring& url, const std::wstring& headers, size_t bytes, uint32_t timeout_ms) {
    if (!bytes) return {};
    std::vector<uint8_t> received;
    uint32_t answered = 0;
    HttpBudget budget(timeout_ms);
    // The sink stops the transfer once the start is here (the transport then returns nothing: the
    // bytes and the status are kept here).
    const auto response = request(url, headers + L"Range: bytes=0-" + std::to_wstring(bytes - 1) + L"\r\n",
                                  std::numeric_limits<size_t>::max(), false, nullptr, timeout_ms,
        [&](uint32_t status, const uint8_t* data, size_t size) {
            answered = status;
            received.insert(received.end(), data, data + std::min(size, bytes - received.size()));
            return received.size() < bytes;
        });
    if ((answered == 200 || answered == 206) && !received.empty()) return {200, std::move(received)};
    return {response.status};
}

HttpResponse httpDownloadFile(const std::wstring& url, const std::wstring& headers, const std::filesystem::path& file,
                              size_t max_bytes, uint32_t timeout_ms, bool private_request) {
    // <file>.part keeps what arrived; the next call asks for the rest (Range) and appends.
    auto part = file; part += L".part";
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    uint64_t have = std::filesystem::is_regular_file(part, ec) ? std::filesystem::file_size(part, ec) : 0;
    if (ec) have = 0;
    // max_bytes caps the whole file, not each resumed request: a part past it is dropped.
    if (have > max_bytes) { std::filesystem::remove(part, ec); return {}; }
    std::wstring request_headers = headers;
    if (have) request_headers += L"Range: bytes=" + std::to_wstring(have) + L"-\r\n";
    std::ofstream output;
    bool opened = false, too_big = false;
    uint64_t received = 0;
    HttpResponse response;
    {
        HttpBudget budget(timeout_ms);
        // The sink checks the cap (the part it continues plus this response); the transport's own
        // limit is one byte larger, so it never fires first.
        response = request(url, request_headers, max_bytes < std::numeric_limits<size_t>::max() ? max_bytes + 1 : max_bytes, private_request, nullptr, timeout_ms,
            [&](uint32_t status, const uint8_t* data, size_t size) {
                if ((status == 206 ? have : 0) + received + size > max_bytes) { too_big = true; return false; }
                if (!opened) { // 206 continues the part file, 200 (Range ignored) starts it over.
                    output.open(part, std::ios::binary | (status == 206 ? std::ios::app : std::ios::trunc));
                    opened = output.is_open();
                    if (!opened) return false;
                }
                output.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
                received += size;
                return output.good();
            });
    }
    const bool written = opened && output.good();
    if (opened) output.close();
    if (too_big) { std::filesystem::remove(part, ec); return {}; } // Larger than allowed: not kept for a retry.
    if ((response.status == 200 || response.status == 206) && written && atomic_file::replace(part, file)) return {200};
    if (response.status == 416) std::filesystem::remove(part, ec); // A part that no longer fits: start over next time.
    return {response.status == 200 || response.status == 206 ? 0u : response.status};
}
