#pragma once
#include "../model/game.hpp"
#include "../net/http.hpp"
#include "account_request.hpp"
#include "gog_comet.hpp"
#include "gog_galaxy_service.hpp"
#include "gog_install.hpp"
#include <functional>
#include <future>
#include <memory>
#include <optional>
#include <chrono>

namespace gog {
using Cancel = std::function<bool()>;
using Http = std::function<HttpResponse(const std::wstring&, const std::wstring&, size_t)>;
struct Config {
    std::string client_id, client_secret, redirect_uri;
    bool legacy_manual_callback = false;
    bool valid() const;
};
Config loadConfig();
Config parsePublicClientProfile(std::string_view json);
Config prepareClientProfile(const std::filesystem::path& file, const Http& get = httpGetPrivate);
std::wstring authorizationUrl(const Config& config, const std::string& state);
std::optional<std::string> callbackCode(std::wstring_view input, const Config& config, const std::string& state);
uint64_t nowSeconds();
struct Tokens {
    std::string access, refresh, user, client;
    uint64_t expires = 0;
};
struct Session {
    Tokens tokens;
    std::vector<Game> games;
    uint64_t synced_at = 0;
};
enum class Failure { none, cancelled, offline, expired, rate_limit, invalid, storage, configuration };
enum class Stage { none, callback, token, profile, library };
struct Result {
    Failure failure = Failure::none;
    uint32_t retry_after = 0;
    explicit operator bool() const { return failure == Failure::none; }
};
// No raw response, token or server-supplied error is ever exposed to the UI.
class Backend {
public:
    virtual ~Backend() = default;
    virtual Result exchange(const Config&, const std::string& code, Tokens&, const Cancel&) = 0;
    virtual Result refresh(const Config&, Tokens&, const Cancel&) = 0;
    virtual Result verify(Tokens&, const Config&, const Cancel&) = 0;
    virtual Result library(Session&, const Config&, const Cancel&) = 0;
    virtual Result installers(const std::string&, Tokens&, const Config&, const Cancel&, std::vector<InstallerVariant>&) { return {Failure::configuration}; }
    virtual Result resolveInstaller(const InstallerVariant&, Tokens&, const Config&, const Cancel&, std::vector<InstallerPart>&) { return {Failure::configuration}; }
    // An account API call (AccountRequest, GET only): the bearer token added, refreshed when due
    // or on 401. `response` is the server's answer whatever its status.
    virtual Result call(const AccountRequest&, Tokens&, const Config&, const Cancel&, HttpResponse&) { return {Failure::configuration}; }
};
std::unique_ptr<Backend> makeBackend(Http get = {});
class Vault {
public:
    virtual ~Vault() = default;
    virtual Result load(std::optional<Session>&) = 0;
    virtual bool save(const Session&) = 0;
    virtual bool clear() = 0;
};
std::unique_ptr<Vault> makeVault(const std::filesystem::path& file);

enum class State { disconnected, preparing, signing_in, verifying, connected, syncing, offline, expired, error };
struct Snapshot {
    State state = State::disconnected;
    Failure failure = Failure::none;
    std::string account;
    std::vector<Game> games;
    uint64_t synced_at = 0;
    uint32_t retry_after = 0;
    std::wstring browser_url; // Only the public authorization request, never the callback.
    Stage stage = Stage::none;
};
enum class Command { restore, connect, complete, sync, cancel, logout };
class Service {
public:
    virtual ~Service() = default;
    virtual void submit(Command command, std::wstring input = {}) = 0;
    virtual std::optional<Snapshot> take() = 0;
    virtual bool install(InstallRequest) { return false; }
    virtual void cancelInstall() {}
    virtual std::optional<InstallSnapshot> takeInstall() { return {}; }
    virtual bool installBusy() const { return false; }
    // An account API call queued for the account worker (which owns the token); its answer is
    // nullopt without a session or on a failed refresh. Shutdown breaks the promise.
    virtual std::future<std::optional<HttpResponse>> request(AccountRequest) {
        std::promise<std::optional<HttpResponse>> none; none.set_value(std::nullopt); return none.get_future();
    }
    // Comet beside a GOG game (gog_comet.hpp): the account worker brings the token up to date and
    // starts it with the account's credentials. Null without a session, with another client
    // profile than Galaxy's, when GOG refuses the token or the port is taken.
    virtual std::future<std::shared_ptr<Comet>> comet(CometRequest) {
        std::promise<std::shared_ptr<Comet>> none; none.set_value(nullptr); return none.get_future();
    }
};
std::unique_ptr<Service> makeService(std::unique_ptr<Backend> backend, std::unique_ptr<Vault> vault,
                                  std::function<Config()> config = loadConfig,
                                  std::chrono::milliseconds login_timeout = std::chrono::minutes(5),
                                  InstallOperations install_operations = nativeInstallOperations(),
                                  CometStart comet_start = startComet);
std::wstring statusText(const Snapshot& snapshot);
}
