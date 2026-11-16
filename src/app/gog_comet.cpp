// GOG achievements without Galaxy (src/store/gog_comet.hpp): when a GOG game starts and the GOG
// account is linked, comet is fetched once (pinned, SHA-256 checked), then started by the account
// worker beside the game; the game's unlocks also slide in as Launchme's achievement card. Comet
// ends itself 15 s after the game leaves it; Launchme ends it once the game's session is over.
#include "launcher.hpp"
#include "../platform/command_line.hpp"
#include <algorithm>
#include <chrono>
#include <cwctype>

namespace {
constexpr uint64_t comet_grace_ms = 20000; // After the session ends: comet's own 15 s idle wait, and a little.
}

bool Launcher::gogCometWanted(const Game& game) const {
    if (!settings_.gog_comet || game.source != GameSource::gog || !game.installed || !gog_service_ || gog_state_.account.empty()) return false;
    // Started through GOG Galaxy: Galaxy's own service answers the game.
    auto program = std::filesystem::path(primaryGameAction(game).target).filename().wstring();
    std::transform(program.begin(), program.end(), program.begin(), towlower);
    return program != L"galaxyclient.exe";
}

void Launcher::startGogComet(const Game& game) {
    if (!gogCometWanted(game)) return;
    gog_comet_.reset(); // One at a time: the port is one.
    gog_overlay_.reset(); gog_overlay_wrapped_ = false;
    gog_comet_key_ = gameKey(game);
    gog_comet_product_ = game.source_id;
    gog_comet_retire_at_ = 0;
    const auto generation = ++gog_comet_generation_;
    const auto alive = alive_;
    const auto window = hwnd_;
    const auto root = userConfigDirectory();
    work_.start([=, prepare = gog_comet_prepare_] {
        const auto exe = prepare(root, [alive] { return !alive->load(); });
        if (!alive->load()) return;
        auto message = std::make_unique<RemoteMessage>();
        message->run = [generation, exe](Launcher& app) { app.gogCometPrepared(generation, exe); };
        if (PostMessageW(window, remote_message, 0, reinterpret_cast<LPARAM>(message.get()))) message.release();
    });
}

void Launcher::gogCometPrepared(uint64_t generation, const std::filesystem::path& exe) {
    if (generation != gog_comet_generation_ || !gog_service_) return;
    if (exe.empty()) { diagnostics::log("warning", "GOG: comet is not available"); return; }
    const auto alive = alive_;
    const auto window = hwnd_;
    gog::CometRequest request{exe, userConfigDirectory() / L"gog" / L"comet"};
    // The game's unlocks, from comet's reader thread to the UI thread.
    request.unlocked = [alive, window](std::wstring name) {
        if (!alive->load()) return;
        auto message = std::make_unique<RemoteMessage>();
        message->run = [name = std::move(name)](Launcher& app) {
            if (app.settings_.gog_comet) app.queueAchievementToast({name, L"GOG", {}});
        };
        if (PostMessageW(window, remote_message, 0, reinterpret_cast<LPARAM>(message.get()))) message.release();
    };
    // The game opened its Galaxy session: GOG's overlay attaches to that process.
    request.session = [alive, window, generation](uint32_t pid) {
        if (!alive->load()) return;
        auto message = std::make_unique<RemoteMessage>();
        message->run = [generation, pid](Launcher& app) { app.gogOverlaySession(generation, pid); };
        if (PostMessageW(window, remote_message, 0, reinterpret_cast<LPARAM>(message.get()))) message.release();
    };
    auto started = std::make_shared<std::future<std::shared_ptr<gog::Comet>>>(gog_service_->comet(std::move(request)));
    work_.start([=] {
        std::shared_ptr<gog::Comet> comet;
        try {
            if (started->wait_for(std::chrono::seconds(60)) == std::future_status::ready) comet = started->get();
        } catch (...) {} // The account worker shut down.
        if (!alive->load()) return;
        auto message = std::make_unique<RemoteMessage>();
        message->run = [generation, comet = std::move(comet)](Launcher& app) mutable { app.gogCometStarted(generation, std::move(comet)); };
        if (PostMessageW(window, remote_message, 0, reinterpret_cast<LPARAM>(message.get()))) message.release();
    });
}

void Launcher::gogCometStarted(uint64_t generation, std::shared_ptr<gog::Comet> comet) {
    if (generation != gog_comet_generation_) return; // A newer game's: this one ends here.
    if (!comet) { diagnostics::log("info", "GOG: comet not started (Galaxy running, other client profile or no session)"); return; }
    diagnostics::log("info", "GOG: comet started");
    gog_comet_ = std::move(comet);
    gog_comet_retire_at_ = 0;
}

// From tick(): comet stays while its game's session runs (or is about to), then gets its grace.
void Launcher::tickGogComet() {
    if (!gog_comet_) return;
    const auto now = GetTickCount64();
    const bool playing = game_sessions_.contains(gog_comet_key_) || (pending_launch_ && gameKey(pending_game_) == gog_comet_key_);
    if (playing && gog_comet_->running()) { gog_comet_retire_at_ = 0; return; }
    if (!gog_comet_->running()) { gog_comet_.reset(); gog_overlay_.reset(); gog_overlay_wrapped_ = false; return; }
    if (!gog_comet_retire_at_) gog_comet_retire_at_ = now + comet_grace_ms;
    else if (now >= gog_comet_retire_at_) { gog_comet_.reset(); gog_overlay_.reset(); gog_overlay_wrapped_ = false; gog_comet_retire_at_ = 0; }
}

void Launcher::stopGogComet() {
    ++gog_comet_generation_;
    gog_comet_.reset();
    gog_overlay_.reset(); gog_overlay_wrapped_ = false;
    gog_comet_retire_at_ = 0;
}

// GOG Galaxy's overlay: a switch on the GOG page; switched on, it is fetched once (comet's
// "overlay" command, GOG's own files; Linux also galaxy-helper).
bool Launcher::gogOverlayReady() const { return gog_overlay_present_(userConfigDirectory()); }

void Launcher::gogOverlayRow(std::vector<settings_ui::Row>& rows) {
    using namespace settings_ui;
    std::wstring detail = tr(L"Nakładka GOG w grach GOG (Shift+Tab): znajomi, czat i zaproszenia do gry. Pobierana raz z GOG (około 100 MB, na dysku około 290 MB); działa tylko online.");
    const bool ready = settings_.gog_overlay && !gog_overlay_busy_ && gogOverlayReady();
    if (!settings_.gog_comet) detail += L"\n" + std::wstring(tr(L"Wymaga osiągnięć bez Galaxy."));
    else if (gog_overlay_busy_) detail += L"\n" + std::wstring(tr(L"Pobieranie…"));
    else if (ready) detail += L"\n" + std::wstring(tr(L"Pobrana. W grze Shift+Tab otwiera nakładkę GOG, a nakładkę Launchme przycisk Guide."));
    else if (settings_.gog_overlay) detail += L"\n" + std::wstring(tr(L"Nie pobrano (brak sieci?). Wyłącz i włącz, aby spróbować ponownie."));
    Row row{Id::gog_overlay, Control::toggle, tr(L"Nakładka Galaxy"), std::move(detail), {}, settings_.gog_overlay && settings_.gog_comet};
    row.locked = !settings_.gog_comet || gog_overlay_busy_;
    row.lines = 3;
    rows.push_back(std::move(row));
}

void Launcher::gogOverlayActivate() {
    if (gog_overlay_busy_ || !settings_.gog_comet) return;
    settings_.gog_overlay = !settings_.gog_overlay;
    settingsChanged(); audio_.context();
    if (settings_.gog_overlay && !gogOverlayReady()) {
        gog_overlay_busy_ = true;
        const auto alive = alive_;
        const auto window = hwnd_;
        work_.start([=, prepare = gog_comet_prepare_, download = gog_overlay_download_, root = userConfigDirectory()] {
            const auto cancel = [alive] { return !alive->load(); };
            bool ok = false;
            try {
                const auto comet = prepare(root, cancel);
                ok = !comet.empty() && download(comet, root, cancel);
            } catch (...) {}
            if (!alive->load()) return;
            auto message = std::make_unique<RemoteMessage>();
            message->run = [ok](Launcher& app) { app.gogOverlayPrepared(ok); };
            if (PostMessageW(window, remote_message, 0, reinterpret_cast<LPARAM>(message.get()))) message.release();
        });
    }
    buildSettingsRows();
}

void Launcher::gogOverlayPrepared(bool ok) {
    gog_overlay_busy_ = false;
    if (ok) showNotification(NotificationKind::launch, tr(L"Nakładka Galaxy"), tr(L"Pobrana."));
    else showNotification(NotificationKind::error, tr(L"Nakładka Galaxy"), tr(L"Nie pobrano (brak sieci?)."));
    if (settingsOpen()) buildSettingsRows();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::gogOverlaySession(uint64_t generation, [[maybe_unused]] uint32_t pid) {
    if (generation != gog_comet_generation_ || !gog_comet_ || !settings_.gog_overlay || !settings_.gog_comet) return;
#ifdef _WIN32
    // Linux: galaxy-helper inside the game's Wine starts it (gogOverlayWrap).
    gog_overlay_ = gog_overlay_start_(userConfigDirectory() / L"gog" / L"comet", pid, gog_comet_product_, i18n::localeName());
    diagnostics::log(gog_overlay_ ? "info" : "warning", gog_overlay_ ? "GOG: Galaxy overlay attached" : "GOG: Galaxy overlay not started");
#endif
}

void Launcher::gogOverlayWrap([[maybe_unused]] GameAction& action) {
#ifndef _WIN32
    if (!settings_.gog_overlay || !settings_.gog_comet || !action.compat || !gogOverlayReady()) return;
    // galaxy.exe <game> <arguments>: it starts the game, finds its process by goggame-*.info in
    // STEAM_COMPAT_INSTALL_PATH, tells comet, starts the overlay and bridges its pipe.
    const auto helper = gog::galaxyHelperFolder(userConfigDirectory()) / L"galaxy.exe";
    if (action.working_directory.empty()) action.working_directory = std::filesystem::path(action.target).parent_path();
    action.arguments = command_line::quoteArgument(action.target) + (action.arguments.empty() ? L"" : L" " + action.arguments);
    action.target = helper.wstring();
    if (!action.compat->install_directory.empty())
        action.environment.emplace_back(L"STEAM_COMPAT_INSTALL_PATH", action.compat->install_directory.wstring());
    gog_overlay_wrapped_ = true;
#endif
}

bool Launcher::gogOverlayOwnsShiftTab() const {
    return settings_.gog_overlay && ((gog_overlay_ && gog_overlay_->running()) || (gog_overlay_wrapped_ && gog_comet_));
}

// GOG Galaxy's stand-in service. Windows: one row on the GOG page, its state read from Windows each
// time the page is built; installing or removing asks for administrator rights. Linux: a switch;
// the service goes into a GOG game's prefix as the game starts (comet's first start fetches it).
void Launcher::gogGalaxyServiceRow(std::vector<settings_ui::Row>& rows) {
    using namespace settings_ui;
    const std::wstring why = tr(L"Część gier GOG (np. Cuphead) łączy się z Comet dopiero, gdy w Windows jest usługa GOG Galaxy. Zastępcza nic nie robi, tylko jest.");
#ifdef _WIN32
    if (!gog_galaxy_service_busy_) gog_galaxy_service_state_ = gog_galaxy_service_query_();
    using State = gog::GalaxyServiceState;
    const auto state = gog_galaxy_service_state_;
    std::wstring detail = gog_galaxy_service_busy_ ? tr(L"Czekam na zgodę administratora…") :
        state == State::launchme ? tr(L"Zainstalowana.") : state == State::galaxy ? tr(L"Jest usługa GOG Galaxy: zastępcza niepotrzebna.") :
        state == State::missing ? tr(L"Instalacja i usunięcie pytają o uprawnienia administratora.") : tr(L"Nie można odczytać stanu usług Windows.");
    Row row{Id::gog_galaxy_service, Control::action, tr(L"Usługa zastępcza Galaxy"), why + L"\n" + detail,
            state == State::launchme ? tr(L"Usuń") : tr(L"Zainstaluj")};
    row.locked = gog_galaxy_service_busy_ || state == State::galaxy || state == State::unknown;
#else
    Row row{Id::gog_galaxy_service, Control::toggle, tr(L"Usługa zastępcza Galaxy"),
            why + L" " + tr(L"Launchme dodaje ją do prefiksu Wine/Proton gry GOG przed jej startem."), {}, settings_.gog_galaxy_service};
#endif
    row.lines = 3;
    rows.push_back(std::move(row));
}

void Launcher::gogGalaxyServiceActivate() {
#ifdef _WIN32
    if (gog_galaxy_service_busy_) return;
    const bool install = gog_galaxy_service_state_ != gog::GalaxyServiceState::launchme;
    gog_galaxy_service_busy_ = true;
    const auto alive = alive_;
    const auto window = hwnd_;
    work_.start([=, change = gog_galaxy_service_change_, root = userConfigDirectory()] {
        bool ok = false;
        try { ok = change(root, install); } catch (...) {}
        if (!alive->load()) return;
        auto message = std::make_unique<RemoteMessage>();
        message->run = [install, ok](Launcher& app) { app.gogGalaxyServiceFinished(install, ok); };
        if (PostMessageW(window, remote_message, 0, reinterpret_cast<LPARAM>(message.get()))) message.release();
    });
#else
    settings_.gog_galaxy_service = !settings_.gog_galaxy_service;
    settingsChanged();
#endif
    buildSettingsRows(); audio_.context();
}

void Launcher::gogGalaxyServiceFinished(bool install, bool ok) {
    gog_galaxy_service_busy_ = false;
    if (ok) showNotification(NotificationKind::launch, tr(L"Usługa zastępcza Galaxy"), install ? tr(L"Zainstalowana.") : tr(L"Usunięta."));
    else showNotification(NotificationKind::error, tr(L"Usługa zastępcza Galaxy"),
                          install ? tr(L"Nie zainstalowano (odmowa uprawnień lub brak sieci).") : tr(L"Nie usunięto (odmowa uprawnień)."));
    if (settingsOpen()) buildSettingsRows();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::gogGalaxyServiceIntoPrefix([[maybe_unused]] const CompatLaunch& launch) {
#ifndef _WIN32
    if (!settings_.gog_galaxy_service || launch.prefix.empty()) return;
    if (gog_galaxy_service_exe_.empty()) {
        // Fetched on a worker for the next start: this one goes on without it.
        const auto alive = alive_;
        const auto window = hwnd_;
        work_.start([=, prepare = gog_galaxy_service_prepare_, root = userConfigDirectory()] {
            auto exe = prepare(root, [alive] { return !alive->load(); });
            if (exe.empty() || !alive->load()) return;
            auto message = std::make_unique<RemoteMessage>();
            message->run = [exe = std::move(exe)](Launcher& app) { app.gog_galaxy_service_exe_ = exe; };
            if (PostMessageW(window, remote_message, 0, reinterpret_cast<LPARAM>(message.get()))) message.release();
        });
        return;
    }
    if (!gog::prefixHasGalaxyService(launch.prefix) && !gog::addGalaxyServiceToPrefix(launch.prefix, gog_galaxy_service_exe_))
        diagnostics::log("info", "GOG: no stand-in service in this prefix yet (it is made by the first start)");
#endif
}
