#include "gog_account.hpp"
#include "../net/json.hpp"
#include "../platform/system.hpp"
#include "../platform/text_encoding.hpp"
#ifdef _WIN32
#include <Windows.h>
#include <bcrypt.h>
#endif
#include <atomic>
#include <condition_variable>
#include <deque>
#include <future>
#include <mutex>
#include <thread>
#include <algorithm>

namespace gog {
namespace {
std::string nonce() {
    uint8_t bytes[32]{};
    if (!system_info::randomBytes(bytes, sizeof(bytes))) return {};
    return key_codec::encode({reinterpret_cast<const char*>(bytes), sizeof(bytes)});
}
class AccountService final : public Service {
    std::unique_ptr<Backend> backend_;
    std::unique_ptr<Vault> vault_;
    std::function<Config()> configuration_;
    std::mutex mutex_;
    std::condition_variable wake_;
    using Reply = std::promise<std::optional<HttpResponse>>;
    using CometReply = std::promise<std::shared_ptr<Comet>>;
    struct Work {
        Command command; std::wstring input; uint64_t generation; std::optional<InstallRequest> install;
        std::optional<AccountRequest> call; std::shared_ptr<Reply> reply; // request(): an account API call.
        std::optional<CometRequest> comet; std::shared_ptr<CometReply> comet_reply; // comet().
    };
    std::deque<Work> work_;
    std::optional<Snapshot> result_;
    std::atomic_uint64_t generation_{0};
    std::atomic_bool stopping_{false};
    std::optional<Session> session_;
    Config config_;
    std::string state_;
    std::chrono::steady_clock::time_point login_deadline_{};
    uint64_t login_generation_ = 0, retry_at_ = 0;
    std::chrono::milliseconds login_timeout_;
    InstallOperations install_operations_;
    CometStart comet_start_;
    InstallSnapshot install_status_;
    bool install_dirty_ = false;
    std::atomic_bool install_busy_{false};
    std::thread worker_;

    Snapshot snapshot(State state, Result result = {}) const {
        Snapshot value; value.state = state; value.failure = result.failure; value.retry_after = result.retry_after;
        if (session_) { value.account = session_->tokens.user; value.games = session_->games; value.synced_at = session_->synced_at; }
        return value;
    }
    void publish(uint64_t generation, Snapshot value) {
        std::lock_guard lock(mutex_);
        if (!stopping_ && generation == generation_) result_ = std::move(value);
    }
    void failed(uint64_t generation, Result result, Stage stage = Stage::none) {
        if (result.failure == Failure::cancelled) return;
        if (result.failure == Failure::rate_limit) retry_at_ = nowSeconds() + result.retry_after;
        auto value = snapshot(result.failure == Failure::expired ? State::expired :
            result.failure == Failure::offline || result.failure == Failure::rate_limit ? State::offline : State::error, result);
        value.stage = stage;
        publish(generation, std::move(value));
    }
    void sync(uint64_t generation, const Cancel& cancel) {
        if (!session_) { publish(generation, {}); return; }
        if (nowSeconds() < retry_at_) { failed(generation, {Failure::rate_limit, static_cast<uint32_t>(retry_at_ - nowSeconds())}); return; }
        config_ = configuration_();
        if (!config_.valid() || config_.client_id != session_->tokens.client) { failed(generation, {Failure::configuration}); return; }
        publish(generation, snapshot(State::syncing));
        auto next = *session_;
        auto stage = Stage::profile;
        auto result = backend_->verify(next.tokens, config_, cancel);
        if (result && !cancel()) { stage = Stage::library; result = backend_->library(next, config_, cancel); }
        // Refresh tokens may rotate before a cancelled/failed library request.
        // Keep the old complete membership, but retain the new same-account token.
        if (!result || cancel()) { next.games = session_->games; next.synced_at = session_->synced_at; }
        if (!vault_->save(next)) { failed(generation, {Failure::storage}); return; }
        session_ = std::move(next);
        if (cancel()) return;
        if (!result) failed(generation, result, stage);
        else publish(generation, snapshot(State::connected));
    }
    void installProgress(uint64_t generation, InstallState state, InstallError error = InstallError::none, uint64_t done = 0, uint64_t total = 0) {
        std::lock_guard lock(mutex_);
        if (stopping_ || generation != generation_) return;
        install_status_.state = state; install_status_.error = error;
        install_status_.done = done; install_status_.total = total;
        install_dirty_ = true;
    }
    void processInstall(const InstallRequest& request, uint64_t generation, const Cancel& cancel) {
        const auto fail = [&](InstallError error) { installProgress(generation, InstallState::error, error); };
        if (!session_ || !std::any_of(session_->games.begin(), session_->games.end(), [&](const Game& game) { return game.source_id == request.product; })) {
            fail(InstallError::account); return;
        }
        config_ = configuration_();
        auto next = *session_;
        auto result = backend_->verify(next.tokens, config_, cancel);
        std::vector<InstallerVariant> variants;
        if (result && !cancel()) result = backend_->installers(request.product, next.tokens, config_, cancel, variants);
        std::vector<InstallerPart> parts;
        if (result && !cancel() && !request.variant.empty()) {
            const auto selected = std::find_if(variants.begin(), variants.end(), [&](const InstallerVariant& item) { return item.choice.id == request.variant; });
            if (selected == variants.end()) result = {Failure::invalid};
            else result = backend_->resolveInstaller(*selected, next.tokens, config_, cancel, parts);
        }
        // Exactly one account worker owns refresh and durable token rotation.
        // Signed CDN links go directly to this worker's downloader, never to UI.
        if (!vault_->save(next)) { fail(InstallError::storage); return; }
        session_ = std::move(next);
        if (cancel()) return;
        if (!result) {
            fail(result.failure == Failure::expired || result.failure == Failure::configuration ? InstallError::account :
                 result.failure == Failure::invalid ? InstallError::manifest : InstallError::network); return;
        }
        if (variants.empty()) { fail(InstallError::unavailable); return; }
        if (request.variant.empty()) {
            std::lock_guard lock(mutex_);
            if (generation != generation_ || stopping_) return;
            install_status_.choices.clear();
            for (const auto& variant : variants) install_status_.choices.push_back(variant.choice);
            install_status_.state = InstallState::ready; install_dirty_ = true; return;
        }
        auto last = std::chrono::steady_clock::time_point{};
        auto phase = InstallState::none;
        const auto error = runOfflineInstaller(request, parts, install_operations_, cancel, [&](InstallState state, uint64_t done, uint64_t total) {
            const auto now = std::chrono::steady_clock::now();
            if (state == phase && done != total && now - last < std::chrono::milliseconds(200)) return;
            phase = state; last = now;
            installProgress(generation, state, InstallError::none, done, total);
        });
        if (!cancel() && error != InstallError::none) fail(error);
    }
    // request(): the account's token stays here; a refresh or 401 rotation is kept in the vault.
    void serve(Work& work) {
        std::optional<HttpResponse> answer;
        try {
            if (session_ && nowSeconds() >= retry_at_) {
                config_ = configuration_();
                if (config_.valid() && config_.client_id == session_->tokens.client) {
                    const Cancel cancel = [&] { return stopping_.load(); };
                    auto next = *session_;
                    // "{account}" in the URL or body: this account's ID, which the caller need not know.
                    auto call = *work.call;
                    const auto fill = [&](auto& text, const auto& marker, const auto& value) {
                        for (size_t at = text.find(marker); at != text.npos; at = text.find(marker, at + value.size())) text.replace(at, std::size(marker) - 1, value);
                    };
                    fill(call.url, L"{account}", std::wstring(next.tokens.user.begin(), next.tokens.user.end()));
                    fill(call.body, "{account}", next.tokens.user);
                    HttpResponse response;
                    const auto result = backend_->call(call, next.tokens, config_, cancel, response);
                    if (result || response.status) answer = std::move(response);
                    if ((next.tokens.access != session_->tokens.access || next.tokens.refresh != session_->tokens.refresh) && vault_->save(next))
                        session_ = std::move(next);
                }
            }
        } catch (...) { answer.reset(); }
        work.reply->set_value(std::move(answer));
    }
    // comet(): a token valid for a while yet (comet refreshes the game's own ones itself), the
    // account's name for the game, then the process. A rotation is kept like any other.
    void startCometFor(Work& work) {
        std::shared_ptr<Comet> started;
        try {
            if (session_ && nowSeconds() >= retry_at_) {
                config_ = configuration_();
                if (config_.valid() && config_.client_id == session_->tokens.client && config_.client_id == galaxy_client_id) {
                    const Cancel cancel = [&] { return stopping_.load(); };
                    auto next = *session_;
                    Result result;
                    if (next.tokens.expires <= nowSeconds() + 15 * 60) result = backend_->refresh(config_, next.tokens, cancel);
                    std::string username;
                    if (result) {
                        HttpResponse response;
                        const AccountRequest profile{L"https://users.gog.com/users/" + text_encoding::wide(next.tokens.user)};
                        if (backend_->call(profile, next.tokens, config_, cancel, response) && response.status == 200)
                            if (const auto root = json::parse({reinterpret_cast<const char*>(response.body.data()), response.body.size()}))
                                if (const auto* name = root->get("username"); name && name->type == json::Value::Type::string && name->text.size() <= 256 &&
                                    std::all_of(name->text.begin(), name->text.end(), [](unsigned char c) { return c >= 32 && c != 127; }))
                                    username = name->text;
                    }
                    if ((next.tokens.access != session_->tokens.access || next.tokens.refresh != session_->tokens.refresh) && vault_->save(next))
                        session_ = std::move(next);
                    if (result && !cancel()) {
                        CometCredentials credentials{session_->tokens.access, session_->tokens.refresh, session_->tokens.user, std::move(username)};
                        started = comet_start_(*work.comet, credentials);
                        SecureZeroMemory(credentials.access.data(), credentials.access.size());
                        SecureZeroMemory(credentials.refresh.data(), credentials.refresh.size());
                    }
                }
            }
        } catch (...) { started.reset(); }
        work.comet_reply->set_value(std::move(started));
    }
    void process(Work& work) {
        if (work.call) { serve(work); return; }
        if (work.comet) { startCometFor(work); return; }
        const auto generation = work.generation;
        const Cancel cancel = [&] { return stopping_.load() || generation_.load() != generation; };
        // Logout is ordered and cannot be dropped by a subsequent Connect.
        if (work.command == Command::logout) {
            state_.clear(); session_.reset(); retry_at_ = 0;
            if (!vault_->clear()) failed(generation, {Failure::storage});
            else publish(generation, {});
            return;
        }
        if (cancel()) return;
        if (work.install) { state_.clear(); processInstall(*work.install, generation, cancel); return; }
        switch (work.command) {
        case Command::restore: {
            const auto result = vault_->load(session_);
            if (!result) { failed(generation, result); break; }
            if (session_) sync(generation, cancel); else publish(generation, {});
            break;
        }
        case Command::connect: {
            Snapshot preparing; preparing.state = State::preparing; publish(generation, preparing);
            config_ = configuration_(); state_ = nonce();
            if (cancel()) { state_.clear(); break; }
            auto url = authorizationUrl(config_, state_);
            if (url.empty()) { state_.clear(); failed(generation, {Failure::configuration}); break; }
            login_deadline_ = std::chrono::steady_clock::now() + login_timeout_;
            login_generation_ = generation;
            Snapshot value; value.state = State::signing_in; value.browser_url = std::move(url);
            publish(generation, std::move(value)); break;
        }
        case Command::complete: {
            auto code = callbackCode(work.input, config_, state_);
            SecureZeroMemory(work.input.data(), work.input.size() * sizeof(wchar_t)); work.input.clear();
            if (!code || state_.empty() || std::chrono::steady_clock::now() >= login_deadline_) {
                state_.clear(); failed(generation, {Failure::invalid}, Stage::callback); break;
            }
            state_.clear();
            Snapshot value; value.state = State::verifying; publish(generation, value);
            Session next;
            auto stage = Stage::token;
            auto result = backend_->exchange(config_, *code, next.tokens, cancel);
            SecureZeroMemory(code->data(), code->size());
            if (result && !cancel()) { stage = Stage::profile; result = backend_->verify(next.tokens, config_, cancel); }
            if (cancel()) break;
            if (!result) { failed(generation, result, stage); break; }
            // A same-account reconnect can reuse its last complete snapshot.
            if (session_ && session_->tokens.user == next.tokens.user) { next.games = session_->games; next.synced_at = session_->synced_at; }
            if (!vault_->save(next)) { failed(generation, {Failure::storage}); break; }
            if (cancel()) {
                // Roll back a cancellation racing the atomic DPAPI replacement.
                if (session_) vault_->save(*session_); else vault_->clear();
                break;
            }
            session_ = std::move(next);
            publish(generation, snapshot(State::connected));
            sync(generation, cancel); break;
        }
        case Command::sync: state_.clear(); sync(generation, cancel); break;
        case Command::cancel:
            state_.clear();
            if (!session_ && !vault_->clear()) failed(generation, {Failure::storage});
            else publish(generation, snapshot(session_ ? State::offline : State::disconnected));
            break;
        default: break;
        }
    }
    void run() {
        for (;;) {
            Work work;
            {
                std::unique_lock lock(mutex_);
                if (state_.empty()) wake_.wait(lock, [&] { return stopping_ || !work_.empty(); });
                else if (!wake_.wait_until(lock, login_deadline_, [&] { return stopping_ || !work_.empty(); })) {
                    state_.clear();
                    if (login_generation_ == generation_) result_ = snapshot(State::error, {Failure::expired});
                }
                if (stopping_) return;
                if (work_.empty()) continue;
                work = std::move(work_.front()); work_.pop_front();
            }
            try { process(work); }
            catch (...) {
                if (work.reply || work.comet_reply) continue;
                state_.clear();
                if (work.install) installProgress(work.generation, InstallState::error, InstallError::storage);
                else failed(work.generation, {Failure::invalid});
            }
            if (work.install) {
                std::lock_guard lock(mutex_);
                if (work.generation == install_status_.generation) install_busy_ = false;
            }
            SecureZeroMemory(work.input.data(), work.input.size() * sizeof(wchar_t));
        }
    }
public:
    AccountService(std::unique_ptr<Backend> backend, std::unique_ptr<Vault> vault, std::function<Config()> config, std::chrono::milliseconds timeout,
                   InstallOperations operations, CometStart comet_start)
        : backend_(std::move(backend)), vault_(std::move(vault)), configuration_(std::move(config)), login_timeout_(timeout),
          install_operations_(std::move(operations)), comet_start_(std::move(comet_start)), worker_([this] { run(); }) {}
    ~AccountService() override {
        // Serialize with the wait predicate: an atomic flag alone can lose a
        // notification between the worker's predicate check and its wait.
        { std::lock_guard lock(mutex_); stopping_ = true; ++generation_; }
        wake_.notify_all();
        if (worker_.joinable()) worker_.join();
        // A pending logout must still reach durable storage on normal shutdown.
        for (auto& work : work_) {
            if (work.command == Command::logout) vault_->clear();
            SecureZeroMemory(work.input.data(), work.input.size() * sizeof(wchar_t));
        }
    }
    void submit(Command command, std::wstring input) override {
        std::lock_guard lock(mutex_);
        if (command == Command::sync && install_busy_) return;
        const auto generation = ++generation_;
        if (command == Command::logout || command == Command::connect) {
            install_status_ = {}; install_status_.generation = generation; install_dirty_ = true; install_busy_ = false;
        }
        result_.reset();
        work_.push_back({command, std::move(input), generation});
        wake_.notify_one();
    }
    std::optional<Snapshot> take() override {
        std::lock_guard lock(mutex_); auto value = std::move(result_); result_.reset(); return value;
    }
    bool install(InstallRequest request) override {
        std::lock_guard lock(mutex_);
        if (install_busy_ || stopping_) return false;
        const auto generation = ++generation_;
        result_.reset();
        if (install_status_.product != request.product || request.variant.empty()) install_status_ = {};
        install_status_.product = request.product; install_status_.generation = generation;
        install_status_.state = InstallState::loading; install_status_.error = InstallError::none;
        install_status_.done = install_status_.total = 0;
        install_dirty_ = true; install_busy_ = true;
        work_.push_back({Command::sync, {}, generation, std::move(request)}); wake_.notify_one();
        return true;
    }
    void cancelInstall() override {
        std::lock_guard lock(mutex_);
        if (!install_busy_ || install_status_.state == InstallState::installing) return;
        install_status_.generation = ++generation_;
        install_status_.state = InstallState::cancelled; install_status_.error = InstallError::none;
        install_dirty_ = true; install_busy_ = false;
    }
    std::optional<InstallSnapshot> takeInstall() override {
        std::lock_guard lock(mutex_);
        if (!install_dirty_) return {};
        install_dirty_ = false; return install_status_;
    }
    bool installBusy() const override { return install_busy_; }
    std::future<std::optional<HttpResponse>> request(AccountRequest call) override {
        auto reply = std::make_shared<Reply>();
        auto answer = reply->get_future();
        {
            // No new generation: an account call never cancels a sync or an install.
            std::lock_guard lock(mutex_);
            if (stopping_) { reply->set_value(std::nullopt); return answer; }
            Work work{Command::sync, {}, generation_.load()};
            work.call = std::move(call); work.reply = reply;
            work_.push_back(std::move(work));
        }
        wake_.notify_one();
        return answer;
    }
    std::future<std::shared_ptr<Comet>> comet(CometRequest request) override {
        auto reply = std::make_shared<CometReply>();
        auto answer = reply->get_future();
        {
            // Like request(): no new generation, nothing else is cancelled.
            std::lock_guard lock(mutex_);
            if (stopping_ || !comet_start_) { reply->set_value(nullptr); return answer; }
            Work work{Command::sync, {}, generation_.load()};
            work.comet = std::move(request); work.comet_reply = reply;
            work_.push_back(std::move(work));
        }
        wake_.notify_one();
        return answer;
    }
};
}
std::unique_ptr<Service> makeService(std::unique_ptr<Backend> backend, std::unique_ptr<Vault> vault, std::function<Config()> config, std::chrono::milliseconds timeout,
                                     InstallOperations operations, CometStart comet_start) {
    return std::make_unique<AccountService>(std::move(backend), std::move(vault), std::move(config), timeout, std::move(operations), std::move(comet_start));
}
std::wstring statusText(const Snapshot& value) {
    switch (value.failure) {
    case Failure::configuration: return tr(L"Nie można przygotować OAuth GOG. Sprawdź sieć lub własną konfigurację.");
    case Failure::storage: return tr(L"Nie można odczytać lub zapisać sesji GOG.");
    case Failure::invalid:
        switch (value.stage) {
        case Stage::callback: return tr(L"Adres powrotu nie pasuje do tej próby. Wybierz Połącz i skopiuj nowy adres.");
        case Stage::token: return tr(L"GOG zwrócił nieprawidłową odpowiedź autoryzacji. Połącz ponownie.");
        case Stage::profile: return tr(L"Nieprawidłowa odpowiedź profilu GOG.");
        case Stage::library: return tr(L"Konto zweryfikowane; błąd danych biblioteki GOG. Wybierz Odśwież.");
        default: return tr(L"Nieprawidłowe dane GOG.");
        }
    case Failure::expired: return tr(L"Sesja GOG wygasła. Połącz konto ponownie.");
    case Failure::rate_limit: return tr(L"Limit GOG. Ponowienie za ") + std::to_wstring(value.retry_after) + L" s.";
    case Failure::offline: return tr(L"GOG offline. Ostatnia zapisana biblioteka.");
    default: break;
    }
    switch (value.state) {
    case State::preparing: return tr(L"Przygotowanie logowania GOG…");
    case State::signing_in: return tr(L"Logowanie w przeglądarce. Wklej pełny adres strony po logowaniu.");
    case State::verifying: return tr(L"Weryfikacja konta GOG…");
    case State::syncing: return tr(L"Pobieranie biblioteki GOG…");
    case State::connected: return tr(L"Połączono · gry: ") + std::to_wstring(value.games.size());
    case State::offline: return tr(L"Offline · ostatnia zapisana biblioteka");
    default: return tr(L"Konto GOG rozłączone");
    }
}
}
