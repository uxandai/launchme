// Linux: a Steam game's own Proton version and launch options, edited where Steam keeps them (its
// menu's "Proton / Wine…"). Steam running with its CEF port (127.0.0.1:8080): SteamClient.Apps through
// SharedJSContext, at once. Running without it: the change waits (Launchme's pending file) until Steam
// closes, and the first such change offers .cef-enable-remote-debugging (live from Steam's next start).
// Steam closed: its files, each backed up first (src/library/steam_game_config.hpp).
#include "launcher.hpp"

#ifndef _WIN32
#include "compat_internal.hpp"
#include "../library/steam.hpp"
#include "../platform/atomic_file.hpp"
#include "../platform/linux_hid.hpp"
#include "../platform/steam_library_sync.hpp"
#include "../platform/user_config.hpp"

#include <algorithm>
#include <chrono>
#include <thread>
#endif

using namespace settings_ui;

#ifdef _WIN32
bool Launcher::steamGameConfigurable(const Game&) const { return false; }
bool Launcher::compatMenu(const Game&) const { return false; }
void Launcher::openSteamGameSettings(const Game&) {}
void Launcher::steamGamePendingWatch() {}
#else
using namespace compat_detail;
namespace config = steam_game_config;
using Mode = launcher_state::SteamGameSettings::Mode;

namespace {
std::filesystem::path pendingFile() { return userConfigDirectory() / L"steam_game_pending.txt"; }
bool steamRunning() { return linux_hid::processRunning("steam"); }

// The settings a live call reads back match the change.
bool applied(const config::Settings& now, const config::Pending& change) {
    return (!change.launch_options || now.launch_options == *change.launch_options) && (!change.compat_tool || now.compat_tool == *change.compat_tool);
}

// Steam took `change` some other way: what the pending file still holds for that game drops it.
void settle(const config::Pending& change) {
    auto list = config::loadPending(pendingFile());
    const auto before = list;
    for (auto& item : list) {
        if (item.app != change.app) continue;
        if (change.launch_options) item.launch_options.reset();
        if (change.compat_tool) item.compat_tool.reset();
    }
    std::erase_if(list, [](const config::Pending& item) { return !item.launch_options && !item.compat_tool; });
    if (list != before) config::savePending(pendingFile(), list);
}

enum class Outcome { live, files, failed, ask };
// One change, as Steam takes it now (a worker's call).
Outcome deliver(const std::filesystem::path& root, uint64_t account, const config::Pending& change, config::Settings& now) {
    if (steam_sync::endpoint(root)) {
        const auto reply = steam_sync::evaluate(root, config::writeExpression(change.app, change.launch_options, change.compat_tool));
        const auto parsed = reply.error.empty() ? config::parseReply(reply.value) : std::nullopt;
        if (!parsed || !applied(*parsed, change)) return Outcome::failed;
        now = *parsed;
        return Outcome::live;
    }
    if (steamRunning()) return Outcome::ask;
    const auto local = config::localConfig(root, account), install = config::installConfig(root);
    if (!config::write(local, install, change.app, change.launch_options, change.compat_tool)) return Outcome::failed;
    if (const auto read = config::read(local, install, change.app)) now = *read;
    return Outcome::files;
}
}

bool Launcher::steamGameConfigurable(const Game& game) const {
    return game.source == GameSource::steam && game.kind == GameKind::steam && game.app_id && !gameKey(game).empty();
}

bool Launcher::compatMenu(const Game& game) const { return (compatGame(game) && !gameKey(game).empty()) || steamGameConfigurable(game); }

void Launcher::openSteamGameSettings(const Game& game) {
    if (!steamGameConfigurable(game)) return;
    dismissMenu();
    steam_game_.game = game;
    steam_game_.app = game.app_id;
    steam_game_.settings = {};
    steam_game_.pending.reset();
    steam_game_.mode = Mode::unknown;
    if (!settingsOpen()) openSettings();
    if (!settingsOpen()) return;
    settings_state_.selectCategory(static_cast<int>(settings_ui::Category::compat));
    settings_state_.open(Page::steam_game, gameKey(game), 0, std::nullopt);
    settings_page_exits_ = true;
    clearSecretInput();
    settings_motion_.valid = false;
    if (!compat_.tools_loaded) compatLoadTools();
    steamGameLoad();
    buildSettingsRows(); settingsPreviewFocus(); audio_.context();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::steamGameLoad() {
    auto& state = steam_game_;
    state.loading = true;
    state.failed = false;
    const auto generation = ++state.generation;
    const auto alive = alive_; const auto hwnd = hwnd_;
    const auto app = state.app; const auto account = account_;
    work_.start([alive, hwnd, app, account, generation] {
        const auto root = findSteamRoot();
        Mode mode = Mode::files;
        std::optional<config::Settings> settings;
        if (steam_sync::endpoint(root)) {
            const auto reply = steam_sync::evaluate(root, config::readExpression(app));
            if (reply.error.empty()) settings = config::parseReply(reply.value);
            mode = settings ? Mode::live : Mode::waiting;
        } else mode = steamRunning() ? Mode::waiting : Mode::files;
        if (!settings) settings = config::read(config::localConfig(root, account), config::installConfig(root), app);
        std::error_code ec;
        const bool flag = std::filesystem::exists(config::cefFlag(root), ec);
        std::optional<config::Pending> pending;
        for (const auto& item : config::loadPending(pendingFile())) if (item.app == app) pending = item;
        if (!alive->load()) return;
        auto message = std::make_unique<RemoteMessage>();
        message->run = [=](Launcher& launcher) {
            auto& state = launcher.steam_game_;
            if (state.generation != generation) return;
            state.loading = false;
            state.failed = !settings;
            state.mode = mode;
            state.flag_created = flag;
            if (settings) state.settings = *settings;
            state.pending = pending;
            if (launcher.settingsOpen() && launcher.settings_state_.at(Page::steam_game)) launcher.buildSettingsRows();
            InvalidateRect(launcher.hwnd_, nullptr, FALSE);
        };
        if (PostMessageW(hwnd, remote_message, 0, reinterpret_cast<LPARAM>(message.get()))) message.release();
    });
}

void Launcher::steamGameApply(config::Pending change) {
    change.app = steam_game_.app;
    if (!change.app) return;
    const auto alive = alive_; const auto hwnd = hwnd_; const auto account = account_;
    const auto name = steam_game_.game.name;
    steam_game_.saving = true;
    if (!work_.start([alive, hwnd, account, change, name] {
        config::Settings now;
        const auto outcome = deliver(findSteamRoot(), account, change, now);
        if (outcome == Outcome::live || outcome == Outcome::files) settle(change);
        if (!alive->load()) return;
        auto message = std::make_unique<RemoteMessage>();
        message->run = [=](Launcher& launcher) {
            auto& state = launcher.steam_game_;
            state.saving = false;
            switch (outcome) {
            case Outcome::live: case Outcome::files:
                if (state.app == change.app) {
                    state.settings.launch_options = now.launch_options;
                    state.settings.compat_tool = now.compat_tool;
                    if (state.pending) {
                        if (change.launch_options) state.pending->launch_options.reset();
                        if (change.compat_tool) state.pending->compat_tool.reset();
                        if (!state.pending->launch_options && !state.pending->compat_tool) state.pending.reset();
                    }
                    state.mode = outcome == Outcome::live ? Mode::live : Mode::files;
                }
                launcher.showNotification(NotificationKind::launch, name,
                    outcome == Outcome::live ? tr(L"Zapisano w Steamie.") : tr(L"Zapisano w plikach Steama (z kopią zapasową)."));
                break;
            case Outcome::failed:
                launcher.showNotification(NotificationKind::error, name, tr(L"Steam nie przyjął zmiany. Spróbuj ponownie albo zmień ją w Steamie."));
                break;
            case Outcome::ask:
                state.asked = change;
                state.mode = Mode::waiting;
                if (state.answered) { launcher.steamGameAnswer(1); break; }
                launcher.prompt_kind_ = PromptKind::steam_game_save;
                launcher.exit_prompt_ = 1; // Zapisz po zamknięciu Steama: nothing outside Launchme changes.
                launcher.audio_.context();
                break;
            }
            if (launcher.settingsOpen() && launcher.settings_state_.at(Page::steam_game)) launcher.buildSettingsRows();
            InvalidateRect(launcher.hwnd_, nullptr, FALSE);
        };
        if (PostMessageW(hwnd, remote_message, 0, reinterpret_cast<LPARAM>(message.get()))) message.release();
    })) steam_game_.saving = false;
}

// The prompt's answer: 0 live saving from Steam's next start (and the change waits), 1 it only waits, 2 nothing.
void Launcher::steamGameAnswer(int index) {
    const auto change = std::exchange(steam_game_.asked, {});
    if (index == 2 || !change.app) return;
    steam_game_.answered = true;
    if (index == 0) {
        const auto flag = config::cefFlag(findSteamRoot());
        std::error_code ec;
        if (!flag.empty() && (std::filesystem::exists(flag, ec) || atomic_file::write(flag, ""))) steam_game_.flag_created = true;
        else showNotification(NotificationKind::error, tr(L"Nie udało się włączyć zapisu na żywo"), flag.wstring());
    }
    auto list = config::loadPending(pendingFile());
    config::queue(list, change);
    if (!config::savePending(pendingFile(), list)) {
        showNotification(NotificationKind::error, tr(L"Nie udało się zapisać zmiany"), pendingFile().wstring());
        return;
    }
    if (steam_game_.app == change.app) {
        auto pending = steam_game_.pending.value_or(config::Pending{change.app, {}, {}});
        if (change.launch_options) pending.launch_options = change.launch_options;
        if (change.compat_tool) pending.compat_tool = change.compat_tool;
        steam_game_.pending = pending;
    }
    showNotification(NotificationKind::launch, steam_game_.game.name,
        index == 0 ? tr(L"Zapis na żywo zadziała po restarcie Steama; do tego czasu zmiana czeka na zamknięcie Steama.")
                   : tr(L"Zmiana zapisze się, gdy zamkniesz Steam."));
    steamGamePendingWatch();
    if (settingsOpen() && settings_state_.at(Page::steam_game)) buildSettingsRows();
}

// While changes wait: every few seconds, Steam with its CEF port takes them live; Steam closed, its files do.
void Launcher::steamGamePendingWatch() {
    if (config::loadPending(pendingFile()).empty() || steam_game_.watching->exchange(true)) return;
    const auto alive = alive_; const auto hwnd = hwnd_; const auto watching = steam_game_.watching; const auto account = account_;
    if (!work_.start([alive, hwnd, watching, account] {
        struct Done { std::shared_ptr<std::atomic<bool>> flag; ~Done() { flag->store(false); } } done{watching};
        const auto pause = [&](int seconds) {
            for (int i = 0; i < seconds * 4 && alive->load(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(250));
        };
        while (alive->load()) {
            auto list = config::loadPending(pendingFile());
            if (list.empty()) return;
            const auto root = findSteamRoot();
            const bool live = steam_sync::endpoint(root);
            if (!live) {
                if (steamRunning()) { pause(3); continue; }
                pause(2); // Steam writes its files as it quits: after it is gone.
                if (steamRunning()) continue;
            }
            int saved = 0, failed = 0;
            for (const auto& change : list) {
                config::Settings now;
                const auto outcome = deliver(root, account, change, now);
                if (outcome == Outcome::live || outcome == Outcome::files) { settle(change); ++saved; }
                else if (outcome == Outcome::failed) ++failed;
            }
            if (!alive->load()) return;
            auto message = std::make_unique<RemoteMessage>();
            message->run = [saved, failed](Launcher& launcher) {
                if (saved) launcher.showNotification(NotificationKind::launch, tr(L"Ustawienia gier Steama zapisane"),
                                                     tr(L"Zmiany, które czekały na Steam: ") + std::to_wstring(saved));
                if (failed) launcher.showNotification(NotificationKind::error, tr(L"Steam nie przyjął zmiany. Spróbuj ponownie albo zmień ją w Steamie."),
                                                      tr(L"Zmiany, które czekały na Steam: ") + std::to_wstring(failed));
                if (launcher.settingsOpen() && launcher.settings_state_.at(Page::steam_game)) launcher.steamGameLoad();
            };
            if (PostMessageW(hwnd, remote_message, 0, reinterpret_cast<LPARAM>(message.get()))) message.release();
            if (failed) return; // Not retried in a loop: the next start or change tries again.
        }
    })) watching->store(false);
}

// ---- Rows ------------------------------------------------------------------------------------------

void Launcher::steamGameRows() {
    const auto& state = steam_game_;
    const auto launch = state.pending && state.pending->launch_options ? *state.pending->launch_options : state.settings.launch_options;
    const auto tool = state.pending && state.pending->compat_tool ? *state.pending->compat_tool : state.settings.compat_tool;

    std::wstring mode, about;
    switch (state.loading ? Mode::unknown : state.mode) {
    case Mode::unknown: mode = tr(L"Sprawdzanie…"); about = tr(L"Czytanie ustawień ze Steama."); break;
    case Mode::live: mode = tr(L"Na żywo przez Steam"); about = tr(L"Steam działa z portem CEF (8080): zmiany działają od razu."); break;
    case Mode::waiting:
        mode = tr(L"Po zamknięciu Steama");
        about = state.flag_created ? tr(L"Plik .cef-enable-remote-debugging już jest: zapis na żywo zadziała po restarcie Steama. Do tego czasu zmiany czekają na zamknięcie Steama.")
                                   : tr(L"Steam działa bez portu CEF (8080), więc nie przyjmie zmian od razu. Przy zmianie możesz włączyć zapis na żywo (może wymagać restartu Steama) albo poczekać, aż zamkniesz Steam.");
        break;
    case Mode::files: mode = tr(L"Do plików Steama"); about = tr(L"Steam jest zamknięty: zmiany trafią do jego plików, każdy z kopią zapasową."); break;
    }
    if (state.failed) about = tr(L"Nie udało się odczytać ustawień Steama dla tej gry.");
    if (state.pending) {
        std::wstring what;
        if (state.pending->compat_tool) what = tr(L"wersja Protona");
        if (state.pending->launch_options) what += (what.empty() ? L"" : L", ") + std::wstring(tr(L"opcje uruchamiania"));
        about += L" " + std::wstring(tr(L"Czeka na zamknięcie Steama: ")) + what + L".";
    }
    Row status{Id::steam_game_mode, Control::action, tr(L"Zapis"), about, mode};
    status.lines = 3;
    settings_rows_.push_back(std::move(status));

    // Steam's own list when it gave one; else the Proton builds Steam can see here, by their internal names.
    std::vector<config::Tool> tools = state.settings.tools;
    if (tools.empty())
        for (const auto& item : compat_.tools) {
            if (item.kind != compat_tools::Kind::proton || item.path.empty()) continue;
            if (item.origin != compat_tools::Origin::steam_compat && item.origin != compat_tools::Origin::steam_official && item.origin != compat_tools::Origin::steam_system) continue;
            const auto name = config::internalName(item.path);
            if (!name.empty() && std::none_of(tools.begin(), tools.end(), [&](const config::Tool& known) { return known.name == name; }))
                tools.push_back({name, item.name});
        }
    if (!tool.empty() && std::none_of(tools.begin(), tools.end(), [&](const config::Tool& known) { return known.name == tool; }))
        tools.push_back({tool, wide(tool)});
    Row version{Id::steam_game_tool, Control::choice, tr(L"Wersja Protona"), tr(L"Warstwa zgodności, której Steam używa dla tej gry."), {}};
    version.choices.push_back({tr(L"Domyślna Steama"), 0, false, L""});
    for (const auto& item : tools) {
        version.choices.push_back({item.label, 0, false, wide(item.name)});
        if (item.name == tool) version.choice = static_cast<int>(version.choices.size()) - 1;
    }
    version.value = version.choices[static_cast<size_t>(version.choice)].label;
    version.section = tr(L"Proton");
    settings_rows_.push_back(std::move(version));

    bool first = true;
    for (const auto& item : config::switches()) {
        Row row{Id::steam_game_switch, Control::toggle, label(item.title), label(item.description), {}, config::has(launch, item)};
        row.key = item.id;
        row.detail = wide(item.word);
        row.dense = true;
        if (first) { row.section = tr(L"Przełączniki (opcje uruchamiania Steama)"); first = false; }
        settings_rows_.push_back(std::move(row));
    }
    Row options{Id::steam_game_options, Control::text, tr(L"Opcje uruchamiania Steama"),
        tr(L"Cała linia, jak we właściwościach gry w Steamie: zmienne i polecenia przed %command%, argumenty gry po nim."),
        launch.empty() ? std::wstring(L"—") : wide(launch)};
    options.lines = 3;
    settings_rows_.push_back(std::move(options));
}

bool Launcher::steamGameActivate(const Row& row) {
    const auto& state = steam_game_;
    const auto launch = state.pending && state.pending->launch_options ? *state.pending->launch_options : state.settings.launch_options;
    switch (row.id) {
    case Id::steam_game_mode: steamGameLoad(); buildSettingsRows(); audio_.context(); return true;
    case Id::steam_game_tool: {
        if (state.loading || state.failed || state.saving) { navigationBoundary(); return true; }
        if (!settings_state_.editing) {
            settings_state_.choice = std::clamp(row.choice, 0, std::max(0, static_cast<int>(row.choices.size()) - 1));
            settings_state_.editing = true;
            audio_.context(); InvalidateRect(hwnd_, nullptr, FALSE);
            return true;
        }
        settings_state_.editing = false;
        const int picked = settings_state_.choice;
        if (picked >= 0 && picked < static_cast<int>(row.choices.size()) && picked != row.choice)
            steamGameApply({0, std::nullopt, narrow(row.choices[static_cast<size_t>(picked)].key)});
        audio_.context(); InvalidateRect(hwnd_, nullptr, FALSE);
        return true;
    }
    case Id::steam_game_switch: {
        if (state.loading || state.failed || state.saving) { navigationBoundary(); return true; }
        const auto& list = config::switches();
        const auto found = std::find_if(list.begin(), list.end(), [&](const config::Switch& item) { return row.key == item.id; });
        if (found == list.end()) return true;
        steamGameApply({0, config::toggle(launch, *found, !row.checked), std::nullopt});
        audio_.context();
        return true;
    }
    case Id::steam_game_options:
        if (state.loading || state.failed || state.saving) { navigationBoundary(); return true; }
        settingsTextRow(wide(launch), [this, launch](const std::wstring& value) {
            const auto text = narrow(value);
            if (text.size() > 4096 || text.find_first_of("\r\n") != std::string::npos) return false;
            if (text != launch) steamGameApply({0, text, std::nullopt});
            return true;
        }, false);
        return true;
    default: return false;
    }
}
#endif
