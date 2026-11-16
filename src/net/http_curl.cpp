// The transport for http.cpp on systems without WinHTTP: libcurl, with one share for DNS and
// TLS sessions (resumed handshakes across workers) and no cookie engine (requests stay isolated).
// The connection pool is not shared: libcurl does not support sharing it between concurrent
// threads, and with the threaded resolver one worker's handle could close a connection whose
// lookup belonged to another (a crash in destroy_async_data under Curl_disconnect, curl 8.5).
#include "http_internal.hpp"
#include "http_download.hpp"
#include "../platform/text_encoding.hpp"
#include "../platform/unicode_text.hpp"

#include <curl/curl.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cwctype>
#include <memory>
#include <optional>
#include <string_view>
#include <thread>

using namespace http_detail;

namespace {
constexpr char user_agent[] = "launchme/0.1";

struct Share {
    CURLSH* handle = nullptr;
    std::array<std::mutex, CURL_LOCK_DATA_LAST> locks;
    Share() {
        curl_global_init(CURL_GLOBAL_DEFAULT);
        handle = curl_share_init();
        curl_share_setopt(handle, CURLSHOPT_LOCKFUNC, +[](CURL*, curl_lock_data data, curl_lock_access, void* self) {
            static_cast<Share*>(self)->locks[data].lock();
        });
        curl_share_setopt(handle, CURLSHOPT_UNLOCKFUNC, +[](CURL*, curl_lock_data data, void* self) {
            static_cast<Share*>(self)->locks[data].unlock();
        });
        curl_share_setopt(handle, CURLSHOPT_USERDATA, this);
        curl_share_setopt(handle, CURLSHOPT_SHARE, CURL_LOCK_DATA_DNS);
        curl_share_setopt(handle, CURLSHOPT_SHARE, CURL_LOCK_DATA_SSL_SESSION);
    }
    // Deliberately never cleaned up: detached workers may still be finishing a request during
    // process exit (as with the WinHTTP session).
};
CURLSH* share() {
    static Share* value = new Share();
    return value->handle;
}

std::string utf8(std::wstring_view text) { return text_encoding::utf8Lenient(text); }
std::wstring wide(std::string_view text) { return text_encoding::wideLenient(text); }
std::wstring lowered(std::wstring text) {
    for (auto& ch : text) ch = static_cast<wchar_t>(std::towlower(ch));
    return text;
}

struct Url {
    std::wstring scheme, host, path; // path includes the query
    uint32_t port = 0;
    bool credentials = false;
};
std::optional<Url> crack(const std::wstring& url) {
    std::unique_ptr<CURLU, decltype(&curl_url_cleanup)> handle(curl_url(), curl_url_cleanup);
    if (!handle || curl_url_set(handle.get(), CURLUPART_URL, utf8(url).c_str(), CURLU_NON_SUPPORT_SCHEME) != CURLUE_OK) return std::nullopt;
    const auto part = [&](CURLUPart which, unsigned flags = 0) -> std::optional<std::string> {
        char* value = nullptr;
        if (curl_url_get(handle.get(), which, &value, flags) != CURLUE_OK || !value) return std::nullopt;
        std::string result(value);
        curl_free(value);
        return result;
    };
    Url result;
    result.scheme = lowered(wide(part(CURLUPART_SCHEME).value_or("")));
    result.host = wide(part(CURLUPART_HOST).value_or(""));
    result.path = wide(part(CURLUPART_PATH).value_or("/"));
    if (const auto query = part(CURLUPART_QUERY)) result.path += L'?' + wide(*query);
    result.port = static_cast<uint32_t>(std::strtoul(part(CURLUPART_PORT, CURLU_DEFAULT_PORT).value_or("0").c_str(), nullptr, 10));
    result.credentials = part(CURLUPART_USER).has_value() || part(CURLUPART_PASSWORD).has_value();
    if (result.host.empty()) return std::nullopt;
    return result;
}

bool pageHost(const std::wstring& url, std::wstring& host) {
    if (url.empty() || url.size() > 2048 || url.find_first_of(L"\r\n\t\\") != url.npos) return false;
    const auto parts = crack(url);
    if (!parts || parts->credentials || (parts->scheme != L"http" && parts->scheme != L"https") ||
        (parts->port != 80 && parts->port != 443)) return false;
    host = lowered(parts->host);
    while (host.ends_with(L".")) host.pop_back();
    if (host.empty() || lanHost(host) || host.ends_with(L".localhost") || host.ends_with(L".internal") ||
        host.ends_with(L".invalid")) return false;
    if (host.front() == L'[' && host.back() == L']') host = host.substr(1, host.size() - 2);
    const auto narrow = utf8(host);
    sockaddr_in v4{}; v4.sin_family = AF_INET;
    if (inet_pton(AF_INET, narrow.c_str(), &v4.sin_addr) == 1) return publicAddress(&v4);
    sockaddr_in6 v6{}; v6.sin6_family = AF_INET6;
    if (inet_pton(AF_INET6, narrow.c_str(), &v6.sin6_addr) == 1) return publicAddress(&v6);
    return host.find_first_not_of(L"0123456789.") != host.npos && host.find(L':') == host.npos;
}

// The host's addresses when every one of them is public, as "host:port:addr,addr" for
// CURLOPT_RESOLVE so the connection goes to exactly what was checked; nullopt otherwise.
// getaddrinfo cannot be cancelled: a lookup past the deadline is left to finish on its own thread.
std::optional<std::string> resolvePublicly(const std::wstring& host, uint32_t port) {
    struct Lookup {
        std::mutex lock;
        std::condition_variable done;
        bool finished = false;
        std::optional<std::string> addresses;
    };
    auto lookup = std::make_shared<Lookup>();
    const auto name = utf8(host);
    std::thread([lookup, name] {
        addrinfo hints{}; hints.ai_socktype = SOCK_STREAM; hints.ai_family = AF_UNSPEC;
        addrinfo* found = nullptr;
        std::optional<std::string> result;
        if (getaddrinfo(name.c_str(), nullptr, &hints, &found) == 0 && found) {
            std::string list;
            bool ok = true;
            for (auto* a = found; a; a = a->ai_next) {
                if (!publicAddress(a->ai_addr)) { ok = false; break; }
                char text[INET6_ADDRSTRLEN]{};
                const void* raw = a->ai_family == AF_INET ? static_cast<const void*>(&reinterpret_cast<sockaddr_in*>(a->ai_addr)->sin_addr)
                                                          : static_cast<const void*>(&reinterpret_cast<sockaddr_in6*>(a->ai_addr)->sin6_addr);
                if (!inet_ntop(a->ai_family, raw, text, sizeof(text))) { ok = false; break; }
                if (!list.empty()) list += ',';
                list += a->ai_family == AF_INET6 ? "[" + std::string(text) + "]" : std::string(text);
            }
            freeaddrinfo(found);
            if (ok && !list.empty()) result = list;
        }
        std::lock_guard guard(lookup->lock);
        lookup->addresses = std::move(result);
        lookup->finished = true;
        lookup->done.notify_all();
    }).detach();
    const auto deadline = std::min(workerDeadline(), now() + 3000);
    std::unique_lock guard(lookup->lock);
    while (!lookup->finished) {
        if (httpCancelled() || now() >= deadline) return std::nullopt;
        lookup->done.wait_for(guard, std::chrono::milliseconds(25));
    }
    if (!lookup->addresses || httpCancelled()) return std::nullopt;
    return name + ':' + std::to_string(port) + ':' + *lookup->addresses;
}

// Retry-After: seconds, or an HTTP date; capped at a week.
uint32_t retryAfter(const std::string& value) {
    char* end = nullptr;
    const auto seconds = std::strtoul(value.c_str(), &end, 10);
    if (end != value.c_str() && !*end) return static_cast<uint32_t>(std::min<unsigned long>(seconds, 7 * 86400));
    const time_t target = curl_getdate(value.c_str(), nullptr);
    const time_t current = time(nullptr);
    return target > current ? static_cast<uint32_t>(std::min<int64_t>(7 * 86400, target - current)) : 0;
}

struct Transfer {
    // In
    size_t max_bytes = 0;
    bool page = false;
    const std::function<bool(uint32_t, const uint8_t*, size_t)>* sink = nullptr;
    std::function<int()> remaining;
    // Reads the final status and headers into `response` and sets body_wanted/refusal: called
    // once, before the first body byte or after a bodiless answer.
    std::function<void()> decide;
    bool decided = false;
    // Headers of the final response (each status line starts them over), names lowercased.
    std::vector<std::pair<std::string, std::string>> headers;
    long status = 0;
    // Out
    HttpResponse response;
    size_t total = 0;
    bool body_wanted = false, refusal = false, stopped = false, failed = false;

    void decideOnce() { if (!decided) { decided = true; decide(); } }

    std::optional<std::string> header(std::string_view name) const {
        for (auto it = headers.rbegin(); it != headers.rend(); ++it) if (it->first == name) return it->second;
        return std::nullopt;
    }
};

size_t onHeader(char* data, size_t size, size_t count, void* user) {
    auto& t = *static_cast<Transfer*>(user);
    std::string_view line(data, size * count);
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.remove_suffix(1);
    if (line.starts_with("HTTP/")) { t.headers.clear(); return size * count; }
    if (line.empty()) return size * count;
    const auto colon = line.find(':');
    if (colon == std::string_view::npos) return size * count;
    std::string name(line.substr(0, colon));
    for (auto& ch : name) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    auto value = line.substr(colon + 1);
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) value.remove_prefix(1);
    t.headers.emplace_back(std::move(name), std::string(value));
    return size * count;
}

size_t onBody(char* data, size_t size, size_t count, void* user) {
    auto& t = *static_cast<Transfer*>(user);
    const size_t bytes = size * count;
    t.decideOnce();
    if (!t.body_wanted) { t.stopped = true; return CURL_WRITEFUNC_ERROR; }
    if (!t.remaining()) { t.failed = true; return CURL_WRITEFUNC_ERROR; }
    const auto* begin = reinterpret_cast<const uint8_t*>(data);
    t.total += bytes;
    if (t.page && t.total > t.max_bytes) {
        t.response.body.insert(t.response.body.end(), begin, begin + (t.max_bytes - t.response.body.size()));
        t.response.truncated = true;
        t.stopped = true;
        return CURL_WRITEFUNC_ERROR;
    }
    if (t.total > t.max_bytes) {
        // A refusal keeps its status and what came.
        if (t.refusal) t.stopped = true; else t.failed = true;
        return CURL_WRITEFUNC_ERROR;
    }
    if (t.sink && *t.sink) {
        if (!(*t.sink)(static_cast<uint32_t>(t.status), begin, bytes)) { t.failed = true; return CURL_WRITEFUNC_ERROR; }
    } else t.response.body.insert(t.response.body.end(), begin, begin + bytes);
    return bytes;
}

// Called by curl often during a transfer: stops it on cancellation or past the deadline.
int onProgress(void* user, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
    return static_cast<Transfer*>(user)->remaining() ? 0 : 1;
}

curl_slist* headerList(const std::wstring& headers) {
    curl_slist* list = nullptr;
    size_t at = 0;
    while (at < headers.size()) {
        auto end = headers.find(L"\r\n", at);
        if (end == std::wstring::npos) end = headers.size();
        if (end > at) list = curl_slist_append(list, utf8(std::wstring_view(headers).substr(at, end - at)).c_str());
        at = end + 2;
    }
    // WinHTTP sends no Accept or Expect by default; neither does this transport.
    if (headers.find(L"Accept:") == std::wstring::npos) list = curl_slist_append(list, "Accept:");
    return curl_slist_append(list, "Expect:");
}
} // namespace

bool publicWebUrl(const std::wstring& url) { std::wstring host; return pageHost(url, host); }

std::wstring http_detail::combineUrl(const std::wstring& base, const std::wstring& location) {
    std::unique_ptr<CURLU, decltype(&curl_url_cleanup)> handle(curl_url(), curl_url_cleanup);
    if (!handle || curl_url_set(handle.get(), CURLUPART_URL, utf8(base).c_str(), 0) != CURLUE_OK ||
        curl_url_set(handle.get(), CURLUPART_URL, utf8(location).c_str(), CURLU_URLENCODE) != CURLUE_OK) return {};
    char* combined = nullptr;
    if (curl_url_get(handle.get(), CURLUPART_URL, &combined, 0) != CURLUE_OK || !combined) return {};
    std::wstring result = wide(combined);
    curl_free(combined);
    return result;
}

HttpResponse http_detail::request(const std::wstring& url, const std::wstring& headers, size_t max_bytes, bool private_request, const std::string* body,
                                  uint32_t cap_ms, const std::function<bool(uint32_t status, const uint8_t*, size_t)>& sink,
                                  int receive_ms, int plain, bool live, bool refusals, bool page) {
    const auto deadline = std::min(workerDeadline(), now() + cap_ms);
    const auto remaining = [&]() -> int { const auto current = now(); return httpCancelled() || current >= deadline ? 0 : static_cast<int>(deadline - current); };
    if (!remaining()) return {};
    std::optional<std::string> pinned;
    if (page) {
        std::wstring host;
        if (!pageHost(url, host)) return {};
        const auto parts = crack(url);
        if (!parts || !(pinned = resolvePublicly(host, parts->port)) || !remaining()) return {};
    }
    const auto parts = crack(url);
    if (!parts) return {};
    const auto& host = parts->host;
    // Plain HTTP only to this machine (local engines) or, for services the user named, the home
    // network; everything else is HTTPS.
    const bool http = parts->scheme == L"http";
    const bool loopback = host == L"127.0.0.1" || unicode_text::equalIgnoreCase(host, L"localhost");
    const bool local = http && ((plain >= 1 && loopback) || (plain >= 2 && lanHost(host)));
    const bool insecure = local || (page && http);
    if (parts->scheme != L"https" && !insecure) return {};
    const bool sgdb = steamGridDbApi(host, parts->path);
    if (sgdb) for (;;) {
        if (!remaining()) return {};
        uint64_t wait = 0;
        {
            std::lock_guard lock(steamGridDbMutex());
            const auto current = now();
            if (steamGridDbLimit().blocked > current) return {429, {}, static_cast<uint32_t>((steamGridDbLimit().blocked - current + 999) / 1000)};
            wait = steamGridDbLimit().delay(current);
            if (!wait) { steamGridDbLimit().reserve(current); break; }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(std::min<uint64_t>(wait, 100)));
    }

    std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> easy(curl_easy_init(), curl_easy_cleanup);
    if (!easy) return {};
    CURL* curl = easy.get();
    Transfer transfer;
    transfer.max_bytes = max_bytes;
    transfer.page = page;
    transfer.sink = &sink;
    transfer.remaining = remaining;
    std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)> header_list(headerList(headers), curl_slist_free_all);
    std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)> resolve(
        pinned ? curl_slist_append(nullptr, pinned->c_str()) : nullptr, curl_slist_free_all);
    const auto target = utf8(url);

    curl_easy_setopt(curl, CURLOPT_SHARE, share());
    curl_easy_setopt(curl, CURLOPT_URL, target.c_str());
    curl_easy_setopt(curl, CURLOPT_USERAGENT, user_agent);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, header_list.get());
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, insecure ? "http,https" : "https");
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, ""); // gzip/deflate/br, as the server offers
    if (resolve) curl_easy_setopt(curl, CURLOPT_RESOLVE, resolve.get());
    if (local) curl_easy_setopt(curl, CURLOPT_NOPROXY, "*"); // Never through a proxy.
    // Credential-bearing requests never follow redirects, even to the same host.
    if (!private_request && headers.find(L"Authorization:") == std::wstring::npos) {
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 3L);
        curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, insecure ? "http,https" : "https");
    }
    if (body) {
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body->data());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(body->size()));
    }
    // Timeouts: connecting gets a share of the budget like WinHTTP's resolve/connect phases; an
    // idle connection fails after receive_ms (low-speed check); the whole transfer ends at the deadline.
    const int phase = std::max(1, remaining() / 4);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, static_cast<long>(std::min(6000, 2 * phase)));
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, static_cast<long>(remaining()));
    const int idle_ms = receive_ms <= 8000 ? std::min(receive_ms, phase) : std::min(receive_ms, remaining());
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, static_cast<long>(std::max(1, (idle_ms + 999) / 1000)));
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, onHeader);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &transfer);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, onBody);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &transfer);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, onProgress);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &transfer);
    if (live) curl_easy_setopt(curl, CURLOPT_BUFFERSIZE, 16384L);

    // curl hands the final head over completely before any body.
    transfer.decide = [&] {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &transfer.status);
        HttpResponse& response = transfer.response;
        response.status = static_cast<uint32_t>(transfer.status);
        if (page) {
            response.content_type = wide(transfer.header("content-type").value_or(""));
            response.location = wide(transfer.header("location").value_or(""));
            const auto type = lowered(response.content_type);
            if (transfer.status == 200 && !type.starts_with(L"text/html") && !type.starts_with(L"application/xhtml+xml") &&
                !type.starts_with(L"text/plain") && !type.starts_with(L"application/json")) return;
        }
        if (transfer.status == 429 || transfer.status == 503)
            if (const auto retry = transfer.header("retry-after")) response.retry_after_seconds = retryAfter(*retry);
        transfer.refusal = refusals && !sink && transfer.status >= 400 && transfer.status < 500;
        if (transfer.refusal) transfer.max_bytes = std::min<size_t>(transfer.max_bytes, 64 * 1024);
        transfer.body_wanted = transfer.status == 200 || (sink && transfer.status == 206) || transfer.refusal;
    };
    const CURLcode result = curl_easy_perform(curl);
    if (result == CURLE_OK) transfer.decideOnce();
    if (transfer.failed || httpCancelled()) return {};
    if (result != CURLE_OK && !(result == CURLE_WRITE_ERROR && transfer.stopped)) return {};
    if (sgdb) {
        const auto number = [&](std::string_view name) -> std::optional<uint64_t> {
            const auto text = transfer.header(name);
            if (!text) return {};
            char* end = nullptr; const auto n = std::strtoull(text->c_str(), &end, 10);
            return end != text->c_str() && !*end ? std::optional<uint64_t>(n) : std::nullopt;
        };
        const auto reset = number("x-ratelimit-reset");
        const auto now_epoch = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count());
        const uint64_t reset_ms = reset && *reset > now_epoch ? std::min<uint64_t>(*reset - now_epoch, 7 * 86400) * 1000 : 0;
        std::lock_guard lock(steamGridDbMutex());
        steamGridDbLimit().observe(now(), static_cast<uint32_t>(transfer.status), uint64_t{transfer.response.retry_after_seconds} * 1000,
                                   number("x-ratelimit-remaining"), reset_ms);
    }
    if (!transfer.body_wanted) transfer.response.body.clear();
    return std::move(transfer.response);
}

DownloadError httpDownload(const std::wstring& original_url, const std::filesystem::path& partial, uint64_t total,
    const std::function<bool()>& cancel, const std::function<void(uint64_t)>& progress,
    const std::function<bool(std::wstring_view)>& allowed_url) {
    if (!total || !allowed_url(original_url)) return DownloadError::invalid_response;
    // Never through a link: the part file must be this task's own regular file.
    struct File { int value; ~File() { if (value >= 0) ::close(value); } } file{
        ::open(partial.c_str(), O_RDWR | O_CREAT | O_NOFOLLOW | O_CLOEXEC, 0644)};
    if (file.value < 0) return DownloadError::storage;
    struct stat info{};
    if (::fstat(file.value, &info) != 0 || !S_ISREG(info.st_mode) || info.st_nlink != 1) return DownloadError::storage;
    uint64_t offset = static_cast<uint64_t>(info.st_size);
    if (offset > total) return DownloadError::invalid_response;
    if (offset == total) { progress(offset); return DownloadError::none; }

    struct Stream {
        int file = -1;
        uint64_t total = 0, offset = 0, begin = 0;
        const std::function<bool()>* cancel = nullptr;
        const std::function<void(uint64_t)>* progress = nullptr;
        std::vector<std::pair<std::string, std::string>> headers;
        long status = 0;
        bool started = false;
        DownloadError error = DownloadError::none;
        std::optional<std::string> header(std::string_view name) const {
            for (auto it = headers.rbegin(); it != headers.rend(); ++it) if (it->first == name) return it->second;
            return std::nullopt;
        }
    };
    for (int attempt = 0; attempt < 3 && !cancel(); ++attempt) {
        uint32_t retry_seconds = 1u << attempt;
        auto url = original_url;
        for (int redirects = 0; redirects <= 3 && !cancel(); ++redirects) {
            if (!allowed_url(url)) return DownloadError::invalid_response;
            const auto parts = crack(url);
            if (!parts || parts->scheme != L"https") return DownloadError::invalid_response;
            std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> easy(curl_easy_init(), curl_easy_cleanup);
            if (!easy) break;
            CURL* curl = easy.get();
            Stream stream{file.value, total, offset, offset, &cancel, &progress};
            const auto target = utf8(url);
            curl_slist* list = curl_slist_append(nullptr, "Accept-Encoding: identity");
            list = curl_slist_append(list, "Accept:");
            if (offset) list = curl_slist_append(list, ("Range: bytes=" + std::to_string(offset) + "-").c_str());
            std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)> headers(list, curl_slist_free_all);
            curl_easy_setopt(curl, CURLOPT_SHARE, share());
            curl_easy_setopt(curl, CURLOPT_URL, target.c_str());
            curl_easy_setopt(curl, CURLOPT_USERAGENT, user_agent);
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers.get());
            curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
            curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "https");
            curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 10000L);
            curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1L);
            curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 10L);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &stream);
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, +[](char* data, size_t size, size_t count, void* user) -> size_t {
                auto& s = *static_cast<Stream*>(user);
                const size_t bytes = size * count;
                if ((*s.cancel)()) return CURL_WRITEFUNC_ERROR;
                if (!s.started) {
                    // Only a 200 or a 206 with the requested range carries the file.
                    if (s.status != 200 && s.status != 206) return CURL_WRITEFUNC_ERROR;
                    if (s.status == 206 && !validDownloadRange(wide(s.header("content-range").value_or("")), s.offset, s.total)) {
                        s.error = DownloadError::invalid_response; return CURL_WRITEFUNC_ERROR;
                    }
                    s.begin = s.status == 200 ? 0 : s.offset;
                    const auto length = downloadNumber(wide(s.header("content-length").value_or("")));
                    if (!length || *length != s.total - s.begin) { s.error = DownloadError::invalid_response; return CURL_WRITEFUNC_ERROR; }
                    if (::lseek(s.file, static_cast<off_t>(s.begin), SEEK_SET) < 0 || (s.status == 200 && ::ftruncate(s.file, 0) != 0)) {
                        s.error = DownloadError::storage; return CURL_WRITEFUNC_ERROR;
                    }
                    s.offset = s.begin; (*s.progress)(s.offset);
                    s.started = true;
                }
                if (bytes > s.total - s.offset) { s.error = DownloadError::invalid_response; return CURL_WRITEFUNC_ERROR; }
                for (size_t done = 0; done < bytes;) {
                    const ssize_t written = ::write(s.file, data + done, bytes - done);
                    if (written < 0 && errno == EINTR) continue;
                    if (written <= 0) { s.error = DownloadError::storage; return CURL_WRITEFUNC_ERROR; }
                    done += static_cast<size_t>(written);
                }
                s.offset += bytes; (*s.progress)(s.offset);
                return bytes;
            });
            curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &stream);
            curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
            curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, +[](void* user, curl_off_t, curl_off_t, curl_off_t, curl_off_t) -> int {
                return (*static_cast<Stream*>(user)->cancel)() ? 1 : 0;
            });
            // Each status line (redirects are not followed here) starts the headers over; the status is
            // known before the first body byte.
            curl_easy_setopt(curl, CURLOPT_HEADERDATA, &stream);
            curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, +[](char* data, size_t size, size_t count, void* user) -> size_t {
                    auto& s = *static_cast<Stream*>(user);
                    std::string_view line(data, size * count);
                    while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.remove_suffix(1);
                    if (line.starts_with("HTTP/")) {
                        s.headers.clear();
                        const auto space = line.find(' ');
                        s.status = space == std::string_view::npos ? 0 : std::strtol(std::string(line.substr(space + 1, 3)).c_str(), nullptr, 10);
                        return size * count;
                    }
                    if (const auto colon = line.find(':'); colon != std::string_view::npos) {
                        std::string name(line.substr(0, colon));
                        for (auto& ch : name) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
                        auto value = line.substr(colon + 1);
                        while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) value.remove_prefix(1);
                        s.headers.emplace_back(std::move(name), std::string(value));
                    }
                    return size * count;
                });
            const CURLcode result = curl_easy_perform(curl);
            if (stream.error != DownloadError::none) return stream.error;
            offset = stream.offset;
            const long status = stream.status;
            if (status == 301 || status == 302 || status == 303 || status == 307 || status == 308) {
                char* location = nullptr;
                curl_easy_getinfo(curl, CURLINFO_REDIRECT_URL, &location);
                url = location ? wide(location) : std::wstring{};
                continue;
            }
            if (status == 429 || status == 503) {
                if (const auto seconds = downloadNumber(wide(stream.header("retry-after").value_or("")))) {
                    if (*seconds > 60) return DownloadError::network;
                    retry_seconds = static_cast<uint32_t>(std::max<uint64_t>(1, *seconds));
                }
                break;
            }
            if (!status || status >= 500) break;
            if (status != 200 && status != 206) return DownloadError::network;
            if (!stream.started) return DownloadError::invalid_response; // An empty body where the file was expected.
            if (::fsync(file.value) != 0) return DownloadError::storage;
            const bool finished = result == CURLE_OK && offset == total;
            if (finished || (cancel() && offset <= total)) return DownloadError::none;
            break;
        }
        if (attempt < 2) for (uint32_t tick = 0; tick < retry_seconds * 10 && !cancel(); ++tick) std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return cancel() ? DownloadError::none : DownloadError::network;
}
