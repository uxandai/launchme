#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>
#include <atomic>

struct HttpResponse {
    uint32_t status = 0;
    std::vector<uint8_t> body;
    uint32_t retry_after_seconds = 0;
    std::wstring content_type, location;
    bool truncated = false;
};

HttpResponse httpGet(const std::wstring& url, const std::wstring& headers, size_t max_bytes);
// Public link previews: anonymous, validated redirects, textual bodies only; a large page
// keeps its bounded prefix instead of failing. Shares the worker's cancellation/budget.
bool publicWebUrl(const std::wstring& url);
HttpResponse httpGetPage(const std::wstring& url, size_t max_bytes);
// Cancellation applies to a whole fallback chain on this worker. In-flight WinHTTP
// operations retain their bounded timeouts; later requests/body reads stop immediately.
class HttpCancellation {
public:
    explicit HttpCancellation(const std::atomic_bool& cancelled);
    ~HttpCancellation();
    HttpCancellation(const HttpCancellation&) = delete;
    HttpCancellation& operator=(const HttpCancellation&) = delete;
private:
    const std::atomic_bool* previous_;
};
bool httpCancelled();
uint32_t steamGridDbWaitSeconds();
// Credential-bearing queries must never follow even same-host redirects.
HttpResponse httpGetPrivate(const std::wstring& url, const std::wstring& headers, size_t max_bytes);
HttpResponse httpPostPrivate(const std::wstring& url, const std::wstring& headers, const std::string& body, size_t max_bytes);
// A slow private POST (language models): the whole request and each wait for data may take up
// to timeout_ms, instead of the usual 20 s cap with 8 s reads.
HttpResponse httpPostLong(const std::wstring& url, const std::wstring& headers, const std::string& body, size_t max_bytes, uint32_t timeout_ms);
// Like httpPostLong, HTTPS only, but each piece of the body goes to `piece` the moment it arrives
// (server-sent events); a 200 response still holds the whole body.
HttpResponse httpPostStream(const std::wstring& url, const std::wstring& headers, const std::string& body, size_t max_bytes, uint32_t timeout_ms,
                            const std::function<void(const uint8_t*, size_t)>& piece);
// This machine only (http://127.0.0.1 or http://localhost; local engines), no
// proxy: GET, or POST when `body` is given (empty bodies too).
HttpResponse httpLocal(const std::wstring& url, const std::wstring& headers, const std::string* body, size_t max_bytes, uint32_t timeout_ms);
// A service the user configured (Ollama): HTTPS anywhere, plain HTTP only to this machine
// or the home network (private IPv4, one-label names, .local/.lan/.home.arpa); slow like httpPostLong.
HttpResponse httpService(const std::wstring& url, const std::wstring& headers, const std::string* body, size_t max_bytes, uint32_t timeout_ms);
// A private HTTPS API call (GET, or POST when `body` is given), slow like httpPostLong, whose 4xx
// answers keep their short body (the JSON reason: ElevenLabs' invalid key, quota or plan).
HttpResponse httpApi(const std::wstring& url, const std::wstring& headers, const std::string* body, size_t max_bytes, uint32_t timeout_ms);
// The address rule above, for tests: this machine or the home network.
bool lanHost(std::wstring_view host);
// Large files (videos): streamed to <file>.part for up to timeout_ms, resumed with Range on the
// next call, renamed to file when complete. Status 200 = file is complete; the body stays empty.
HttpResponse httpDownloadFile(const std::wstring& url, const std::wstring& headers, const std::filesystem::path& file,
                              size_t max_bytes, uint32_t timeout_ms, bool private_request);

// The first `bytes` of a file (Range: bytes=0-…; a server that ignores Range has its body cut
// there), within timeout_ms. Status 200 with that start (shorter when the file is), else the failure.
HttpResponse httpGetPrefix(const std::wstring& url, const std::wstring& headers, size_t bytes, uint32_t timeout_ms);

// Bounds a whole fallback chain on the current worker, not just one HTTP read.
class HttpBudget {
public:
    explicit HttpBudget(uint32_t milliseconds);
    ~HttpBudget();
    HttpBudget(const HttpBudget&) = delete;
    HttpBudget& operator=(const HttpBudget&) = delete;
private:
    uint64_t previous_;
};
