#include "epic_account.hpp"
#ifdef _WIN32
#include <Windows.h>
#endif
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <future>
#include <mutex>
#include <thread>

namespace epic {
namespace {
class AccountService final : public Service {
    std::unique_ptr<Backend> backend_;
    std::unique_ptr<Vault> vault_;
    std::unique_ptr<Runner> runner_;
    std::function<Config()> configuration_;
    std::chrono::milliseconds timeout_;
    std::mutex mutex_;
    std::condition_variable wake_;
    using Reply = std::promise<std::optional<HttpResponse>>;
    struct Work {
        Command command; std::wstring input; uint64_t generation; std::optional<Game> game; std::optional<gog::InstallRequest> install;
        std::optional<AccountRequest> call; std::shared_ptr<Reply> reply; // request(): an account API call.
    };
    std::deque<Work> queue_;
    std::atomic_uint64_t generation_{0};
    std::atomic_bool stop_{false}, busy_{false};
    std::optional<Snapshot> result_;
    std::optional<LaunchResult> launch_;
    gog::InstallSnapshot install_;
    bool install_dirty_ = false, login_ = false;
    uint64_t login_generation_ = 0, retry_at_ = 0;
    std::chrono::steady_clock::time_point deadline_;
    std::optional<Session> session_;
    Config config_;
    std::thread worker_;
    Snapshot snapshot(State state, Result result = {}) const {
        Snapshot s; s.state = state; s.failure = result.failure;
        if (session_) { s.account = session_->tokens.account; s.games = session_->games; s.synced_at = session_->synced_at; }
        return s;
    }
    void publish(uint64_t generation, Snapshot s) {
        std::lock_guard lock(mutex_); if (!stop_ && generation == generation_) result_ = std::move(s);
    }
    void failed(uint64_t generation, Result result) {
        if (result.failure == Failure::cancelled) return;
        if (result.failure == Failure::rate_limit) retry_at_ = nowSeconds() + result.retry_after;
        publish(generation, snapshot(result.failure == Failure::offline || result.failure == Failure::rate_limit ? State::offline : State::error, result));
    }
    Result verify(const Cancel& cancel) {
        if (!session_) return {Failure::expired};
        if (nowSeconds() < retry_at_) return {Failure::rate_limit, static_cast<uint32_t>(retry_at_ - nowSeconds())};
        config_ = configuration_();
        if (!config_.valid()) return {Failure::configuration};
        auto result = backend_->verify(config_, session_->tokens, cancel);
        if (!vault_->save(*session_)) return {Failure::storage};
        return result;
    }
    void sync(uint64_t generation, const Cancel& cancel) {
        if (!session_) { publish(generation, {}); return; }
        publish(generation, snapshot(State::syncing));
        auto result = verify(cancel);
        if (result && !cancel()) {
            auto next = *session_;
            result = backend_->library(config_, next, cancel);
            if (!result || cancel()) { next.games = session_->games; next.synced_at = session_->synced_at; }
            // Commit rotating tokens together with the last complete membership.
            if (!vault_->save(next)) { failed(generation, {Failure::storage}); return; }
            session_ = std::move(next);
        }
        if (cancel()) return;
        if (!result) failed(generation, result); else publish(generation, snapshot(State::connected));
    }
    void progress(uint64_t generation, gog::InstallState state, uint64_t done = 0, uint64_t total = 0, gog::InstallError error = gog::InstallError::none) {
        std::lock_guard lock(mutex_);
        if (stop_ || generation != generation_) return;
        install_.state = state; install_.done = done; install_.total = total; install_.error = error; install_dirty_ = true;
    }
    void operation(const Work& work, const Cancel& cancel) {
        Result result{Failure::expired};
        const auto& game = *work.game;
        // Never trust UI/catalog membership to authorize an installation or game token.
        if (session_ && std::any_of(session_->games.begin(), session_->games.end(), [&](const Game& g) { return g.source_id == game.source_id; })) {
            result = verify(cancel);
            if (result || (!work.install && result.failure == Failure::offline)) {
                if (work.install) result = runner_->install(session_->tokens, game, *work.install, cancel,
                    [&](auto state, auto done, auto total) { progress(work.generation, state, done, total); });
                else {
                    GameAction action;
                    result = runner_->launch(session_->tokens, game, action, cancel, result.failure == Failure::offline);
                    std::lock_guard lock(mutex_);
                    if (!cancel()) launch_ = LaunchResult{game, result ? std::move(action) : GameAction{}, result.failure};
                    return;
                }
            }
        }
        if (cancel()) return;
        if (work.install) progress(work.generation, result ? gog::InstallState::completed : gog::InstallState::error, 0, 0,
            result ? gog::InstallError::none : result.failure == Failure::expired ? gog::InstallError::account : result.failure == Failure::unsupported ? gog::InstallError::unavailable :
            result.failure == Failure::storage ? gog::InstallError::storage : result.failure == Failure::invalid ? gog::InstallError::detection : gog::InstallError::network);
        else { std::lock_guard lock(mutex_); if (!cancel()) launch_ = LaunchResult{game, {}, result.failure}; }
    }
    // request(): the account's token stays here; a refresh or 401 rotation is kept in the vault.
    void serve(Work& work) {
        std::optional<HttpResponse> answer;
        try {
            if (session_ && nowSeconds() >= retry_at_) {
                config_ = configuration_();
                if (config_.valid()) {
                    const Cancel cancel = [&] { return stop_.load(); };
                    auto next = *session_;
                    // "{account}" in the URL or body: this account's ID, which the caller need not know.
                    auto call = *work.call;
                    const auto fill = [&](auto& text, const auto& marker, const auto& value) {
                        for (size_t at = text.find(marker); at != text.npos; at = text.find(marker, at + value.size())) text.replace(at, std::size(marker) - 1, value);
                    };
                    fill(call.url, L"{account}", std::wstring(next.tokens.account.begin(), next.tokens.account.end()));
                    fill(call.body, "{account}", next.tokens.account);
                    HttpResponse response;
                    const auto result = backend_->call(config_, call, next.tokens, cancel, response);
                    if (result || response.status) answer = std::move(response);
                    if ((next.tokens.access != session_->tokens.access || next.tokens.refresh != session_->tokens.refresh) && vault_->save(next))
                        session_ = std::move(next);
                }
            }
        } catch (...) { answer.reset(); }
        work.reply->set_value(std::move(answer));
    }
    void process(Work& work) {
        if (work.call) { serve(work); return; }
        const auto generation = work.generation;
        const Cancel cancel = [&] { return stop_.load() || generation != generation_.load(); };
        // Logout stays ordered even when immediately followed by a new Connect.
        if (work.command == Command::logout) {
            runner_->cleanup();
            login_ = false; session_.reset(); retry_at_ = 0;
            if (!vault_->clear()) failed(generation, {Failure::storage}); else publish(generation, {});
            return;
        }
        if (cancel()) return;
        if (work.game) { login_ = false; operation(work, cancel); return; }
        switch (work.command) {
        case Command::restore: {
            runner_->cleanup();
            const auto r = vault_->load(session_);
            if (!r) failed(generation, r);
            else { if (session_) publish(generation, snapshot(State::offline)); sync(generation, cancel); }
            break;
        }
        case Command::connect: {
            // An explicit account switch cannot restore old membership on Cancel/restart.
            login_ = false; session_.reset(); retry_at_ = 0;
            if (!vault_->clear()) { failed(generation, {Failure::storage}); break; }
            publish(generation, snapshot(State::preparing)); config_ = configuration_();
            if (cancel()) break;
            if (!config_.valid()) { failed(generation, {Failure::configuration}); break; }
            login_ = true; login_generation_ = generation; deadline_ = std::chrono::steady_clock::now() + timeout_;
            Snapshot s; s.state = State::signing_in; s.browser_url = authorizationUrl(config_); publish(generation, std::move(s)); break;
        }
        case Command::complete: {
            auto code = authorizationCode(work.input);
            SecureZeroMemory(work.input.data(), work.input.size() * sizeof(wchar_t)); work.input.clear();
            if (!login_ || std::chrono::steady_clock::now() >= deadline_ || !code) { login_ = false; failed(generation, {Failure::invalid}); break; }
            login_ = false; publish(generation, snapshot(State::verifying));
            Session next;
            auto result = backend_->exchange(config_, *code, next.tokens, cancel); SecureZeroMemory(code->data(), code->size());
            if (result && !cancel()) result = backend_->verify(config_, next.tokens, cancel);
            if (cancel()) break;
            if (!result) { failed(generation, result); break; }
            if (!vault_->save(next)) { failed(generation, {Failure::storage}); break; }
            if (cancel()) { vault_->clear(); break; }
            session_ = std::move(next); sync(generation, cancel); break;
        }
        case Command::sync: login_ = false; sync(generation, cancel); break;
        case Command::cancel: login_ = false; publish(generation, snapshot(session_ ? State::offline : State::disconnected)); break;
        default: break;
        }
    }
    void run() {
        for (;;) {
            Work work;
            {
                std::unique_lock lock(mutex_);
                if (!login_) wake_.wait(lock, [&] { return stop_ || !queue_.empty(); });
                else if (!wake_.wait_until(lock, deadline_, [&] { return stop_ || !queue_.empty(); })) {
                    login_ = false; if (generation_ == login_generation_) result_ = snapshot(State::error, {Failure::expired});
                }
                if (stop_) return;
                if (queue_.empty()) continue;
                work = std::move(queue_.front()); queue_.pop_front();
            }
            try { process(work); }
            catch (...) {
                if (work.reply) continue;
                if (work.install) progress(work.generation, gog::InstallState::error, 0, 0, gog::InstallError::storage);
                else if (work.game) { std::lock_guard lock(mutex_); if (work.generation == generation_) launch_ = LaunchResult{*work.game, {}, Failure::invalid}; }
                else failed(work.generation, {Failure::invalid});
            }
            SecureZeroMemory(work.input.data(), work.input.size() * sizeof(wchar_t));
            if (work.game) { std::lock_guard lock(mutex_); if (work.generation == generation_) busy_ = false; }
        }
    }
public:
    AccountService(std::unique_ptr<Backend> b, std::unique_ptr<Vault> v, std::unique_ptr<Runner> r, std::function<Config()> cfg, std::chrono::milliseconds timeout)
        : backend_(std::move(b)), vault_(std::move(v)), runner_(std::move(r)), configuration_(std::move(cfg)), timeout_(timeout), worker_([this] { run(); }) {}
    ~AccountService() override {
        // The stop predicate and wait must share this mutex, including shutdown.
        { std::lock_guard lock(mutex_); stop_ = true; ++generation_; }
        wake_.notify_all(); worker_.join();
        for (auto& work : queue_) { if (work.command == Command::logout) vault_->clear(); SecureZeroMemory(work.input.data(), work.input.size() * sizeof(wchar_t)); }
    }
    void submit(Command command, std::wstring input) override {
        std::lock_guard lock(mutex_);
        if (busy_ && command == Command::sync) return;
        const auto generation = ++generation_; result_.reset(); launch_.reset();
        if (command == Command::connect || command == Command::logout || command == Command::cancel) {
            busy_ = false; install_ = {}; install_.generation = generation; install_dirty_ = true;
        } else if (busy_) {
            // The new generation cancels the running install or launch (its cancel predicate), and its
            // end no longer matches generation_ to clear busy_: ended here, as cancelOperation does.
            busy_ = false;
            if (install_.busy()) { install_.state = gog::InstallState::cancelled; install_dirty_ = true; }
        }
        queue_.push_back({command, std::move(input), generation}); wake_.notify_one();
    }
    std::optional<Snapshot> take() override { std::lock_guard lock(mutex_); auto r = std::move(result_); result_.reset(); return r; }
    bool install(const Game& g, gog::InstallRequest request) override {
        std::lock_guard lock(mutex_); if (stop_ || busy_) return false;
        const auto generation = ++generation_; busy_ = true; install_ = {}; install_.product = g.source_id;
        install_.generation = generation; install_.state = gog::InstallState::loading; install_dirty_ = true;
        queue_.push_back({Command::sync, {}, generation, g, std::move(request)}); wake_.notify_one(); return true;
    }
    bool launch(const Game& g) override {
        std::lock_guard lock(mutex_); if (stop_ || busy_) return false;
        busy_ = true; launch_.reset(); queue_.push_back({Command::sync, {}, ++generation_, g}); wake_.notify_one(); return true;
    }
    void cancelOperation() override {
        std::lock_guard lock(mutex_); if (!busy_) return;
        ++generation_; busy_ = false; launch_.reset(); install_.state = gog::InstallState::cancelled; install_dirty_ = true;
    }
    bool busy() const override { return busy_; }
    std::optional<gog::InstallSnapshot> takeInstall() override {
        std::lock_guard lock(mutex_); if (!install_dirty_) return {}; install_dirty_ = false; return install_;
    }
    std::optional<LaunchResult> takeLaunch() override { std::lock_guard lock(mutex_); auto r = std::move(launch_); launch_.reset(); return r; }
    std::future<std::optional<HttpResponse>> request(AccountRequest call) override {
        auto reply = std::make_shared<Reply>();
        auto answer = reply->get_future();
        {
            // No new generation: an account call never cancels a sync, an install or a launch.
            std::lock_guard lock(mutex_);
            if (stop_) { reply->set_value(std::nullopt); return answer; }
            Work work{Command::sync, {}, generation_.load()};
            work.call = std::move(call); work.reply = reply;
            queue_.push_back(std::move(work));
        }
        wake_.notify_one();
        return answer;
    }
};
}
std::unique_ptr<Service> makeService(std::unique_ptr<Backend> b, std::unique_ptr<Vault> v, std::unique_ptr<Runner> r, std::function<Config()> cfg, std::chrono::milliseconds timeout) {
    return std::make_unique<AccountService>(std::move(b), std::move(v), std::move(r), std::move(cfg), timeout);
}
std::wstring statusText(const Snapshot& s) {
    switch (s.failure) {
    case Failure::configuration: return tr(L"Nie można przygotować logowania Epic. Sprawdź połączenie z siecią.");
    case Failure::storage: return tr(L"Nie można odczytać lub zapisać sesji Epic.");
    case Failure::expired: return tr(L"Sesja Epic wygasła. Połącz konto ponownie.");
    case Failure::invalid: return tr(L"Nieprawidłowa odpowiedź Epic. Ponów logowanie lub synchronizację.");
    case Failure::rate_limit: return tr(L"Limit zapytań Epic. Spróbuj ponownie później.");
    case Failure::offline: return tr(L"Epic offline. Ostatnia zapisana biblioteka.");
    default: break;
    }
    switch (s.state) {
    case State::preparing: return tr(L"Przygotowanie logowania Epic…");
    case State::signing_in: return tr(L"Wklej authorizationCode lub całą odpowiedź JSON po logowaniu.");
    case State::verifying: return tr(L"Weryfikacja konta Epic…");
    case State::syncing: return tr(L"Pobieranie biblioteki Epic…");
    case State::connected: return tr(L"Połączono · gry: ") + std::to_wstring(s.games.size());
    case State::offline: return tr(L"Offline · ostatnia zapisana biblioteka");
    default: return tr(L"Konto Epic rozłączone");
    }
}
std::wstring installStatusText(const gog::InstallSnapshot& s) {
    if (s.state == gog::InstallState::error) {
        if (s.error == gog::InstallError::unavailable) return tr(L"Ta gra wymaga instalacji przez sklep wydawcy.");
        if (s.error == gog::InstallError::account) return tr(L"Połącz konto Epic ponownie.");
        return tr(L"Instalacja Epic nie powiodła się. Możesz wznowić pobieranie.");
    }
    switch (s.state) {
    case gog::InstallState::loading: return tr(L"Przygotowanie pobierania Epic…");
    case gog::InstallState::downloading: return tr(L"Pobieranie gry z Epic…");
    case gog::InstallState::verifying: return tr(L"Sprawdzanie plików gry…");
    case gog::InstallState::installing: return tr(L"Instalowanie składników gry…");
    case gog::InstallState::completed: return tr(L"Gra jest zainstalowana.");
    case gog::InstallState::cancelled: return tr(L"Pobieranie przerwane. Możesz je wznowić.");
    default: return tr(L"Instalacja bez klienta Epic Games Launcher.");
    }
}
}
