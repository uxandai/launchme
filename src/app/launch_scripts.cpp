// Opcje uruchamiania → Skrypty (LibraryState::launch_scripts): a game's commands before its launch,
// after its start and after its exit, run hidden by src/platform/launch_scripts.hpp on a worker. The
// launch waits for the script before it (at most two minutes, then it goes on and leaves the script
// running); the others are not waited for. A failed script says so in a toast; with "Zapisuj logi" its
// outcome and output's last line go to the game's log. "Testuj" in the panel runs one now.
#include "launcher.hpp"
#include "../platform/diagnostics.hpp"
#include "../platform/text_encoding.hpp"

#include <chrono>
#include <cwctype>

namespace {
using launch_scripts::Phase;
constexpr std::chrono::milliseconds before_timeout = std::chrono::minutes(2), after_timeout = std::chrono::minutes(2), test_timeout = std::chrono::seconds(60);

const std::wstring& command(const LibraryState::LaunchScripts& scripts, Phase phase) {
    return phase == Phase::before ? scripts.before : phase == Phase::started ? scripts.started : scripts.exited;
}
const wchar_t* phaseTitle(Phase phase) {
    return phase == Phase::before ? tr(L"Skrypt przed uruchomieniem") : phase == Phase::started ? tr(L"Skrypt po uruchomieniu") : tr(L"Skrypt po wyjściu");
}
// What the run ended with, for the panel's note, a toast and the game's log.
std::wstring outcome(const launch_scripts::Result& result, std::chrono::milliseconds timeout) {
    const auto line = launch_scripts::lastLine(result.output);
    if (!result.started) return tr(L"Nie udało się uruchomić skryptu");
    if (!result.finished)
        return std::wstring(tr(L"Skrypt nadal działa; Launchme przestał czekać po ")) +
               std::to_wstring(std::chrono::duration_cast<std::chrono::seconds>(timeout).count()) + L" s";
    std::wstring text = result.exit_code == 0 ? std::wstring(tr(L"Skrypt zakończony (kod 0)")) :
                        std::wstring(tr(L"Skrypt zakończony z kodem ")) + std::to_wstring(result.exit_code);
    if (!line.empty()) text += L": " + line;
    return text;
}
}

const LibraryState::LaunchScripts* Launcher::launchScripts(const std::string& key) const {
    const auto found = library_state_.launch_scripts.find(key);
    return found == library_state_.launch_scripts.end() ? nullptr : &found->second;
}

void Launcher::setLaunchScripts(const std::string& key, LibraryState::LaunchScripts scripts) {
    if (key.empty()) return;
    const auto* current = launchScripts(key);
    if (current ? *current == scripts : scripts.empty()) return;
    if (scripts.empty()) library_state_.launch_scripts.erase(key);
    else library_state_.launch_scripts[key] = std::move(scripts);
    library_state_dirty_ = true;
    saveLibraryPreferences();
}

bool Launcher::runLaunchScript(const std::string& key, const std::wstring& name, Phase phase, std::function<void(Launcher&)> then) {
    const auto* scripts = launchScripts(key);
    const std::wstring text = scripts ? command(*scripts, phase) : std::wstring{};
    if (text.empty()) { if (then) then(*this); return false; } // An empty field runs nothing.
    launchFlagsNote(key, std::wstring(L"Script ") + text_encoding::wideLenient(launch_scripts::phaseName(phase)) + L": " + text);
    launch_scripts::Context context{phase, name, key};
    const auto timeout = phase == Phase::before ? before_timeout : after_timeout;
    const auto alive = alive_;
    const auto window = hwnd_;
    auto deliver = [alive, window, key, name, phase, timeout, then](launch_scripts::Result result) {
        if (!alive->load()) return;
        auto message = std::make_unique<RemoteMessage>();
        message->run = [key, name, phase, timeout, then, result = std::move(result)](Launcher& app) {
            const auto text = outcome(result, timeout);
            app.launchFlagsNote(key, std::wstring(L"Script ") + text_encoding::wideLenient(launch_scripts::phaseName(phase)) + L": " + text);
            if (!result.ok()) {
                diagnostics::log("warning", std::wstring(L"Launch script (") + text_encoding::wideLenient(launch_scripts::phaseName(phase)) + L") of " + name + L": " + text);
                if (!result.started || result.finished) app.showNotification(NotificationKind::error, std::wstring(phaseTitle(phase)) + L" · " + name, text);
            }
            if (then) then(app);
        };
        if (PostMessageW(window, remote_message, 0, reinterpret_cast<LPARAM>(message.get()))) message.release();
    };
    auto runner = launch_script_run_;
    const bool started = work_.start([text, context, timeout, alive, runner, deliver] {
        deliver(runner ? runner(text, context) : launch_scripts::run(text, context, timeout, [alive] { return !alive->load(); }));
    });
    if (!started) { if (then) then(*this); return false; }
    return true;
}

bool Launcher::launchScriptBefore(const Game& game, std::function<void(Launcher&)> then) {
    const auto key = gameKey(game);
    const auto* scripts = launchScripts(key);
    if (!scripts || scripts->before.empty()) return false;
    launch_script_wait_ = true;
    showNotification(NotificationKind::launch, game.name, tr(L"Skrypt przed uruchomieniem…"));
    if (!runLaunchScript(key, game.name, Phase::before, [then = std::move(then)](Launcher& app) {
            app.launch_script_wait_ = false;
            if (then) then(app);
        })) launch_script_wait_ = false;
    return true;
}

void Launcher::launchScriptsStarted(const Game& game) {
    const auto key = gameKey(game);
    const auto* scripts = launchScripts(key);
    if (!scripts) return;
    if (!scripts->exited.empty()) script_sessions_[key] = game.name;
    runLaunchScript(key, game.name, Phase::started);
}

void Launcher::launchScriptsEnded(const std::string& key) {
    const auto found = script_sessions_.find(key);
    if (found == script_sessions_.end()) return;
    const auto name = found->second;
    script_sessions_.erase(found);
    runLaunchScript(key, name, Phase::exited);
}

void Launcher::customEditorTab(custom_game_ui::Tab tab) {
    auto& ui = custom_editor_;
    if (!ui.open || !ui.tabs()) return;
    const auto item = tab == custom_game_ui::Tab::scripts ? custom_game_ui::Item::tab_scripts : custom_game_ui::Item::tab_launch;
    if (ui.tab == tab && ui.focus == item) return;
    if (ui.tab != tab) audio_.navigate();
    ui.tab = tab;
    ui.focus = item;
    ui.selected = ui.confirm_remove = false;
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::customEditorTestScript(int field) {
    auto& ui = custom_editor_;
    if (!ui.open || !custom_game_ui::scriptField(field) || ui.testing) return;
    std::wstring text = ui.fields[static_cast<size_t>(field)];
    while (!text.empty() && iswspace(text.back())) text.pop_back();
    while (!text.empty() && iswspace(text.front())) text.erase(text.begin());
    ui.error.clear();
    if (text.empty()) {
        ui.note = tr(L"Pole jest puste: nic się nie uruchomi");
        ui.note_failed = false;
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    const auto phase = static_cast<Phase>(field - custom_game_ui::before_field);
    launch_scripts::Context context{phase, ui.title.empty() ? ui.fields[custom_game_ui::name_field] : ui.title,
                                    ui.flags_key.empty() ? gameKey(customEditorGame()) : ui.flags_key}; // A new game: the key it will have.
    ui.testing = true;
    ui.note = tr(L"Testowanie skryptu…");
    ui.note_failed = false;
    audio_.context();
    InvalidateRect(hwnd_, nullptr, FALSE);
    const auto generation = ++script_test_generation_;
    const auto alive = alive_;
    const auto window = hwnd_;
    auto runner = launch_script_run_;
    const auto deliver = [alive, window, generation](launch_scripts::Result result) {
        if (!alive->load()) return;
        auto message = std::make_unique<RemoteMessage>();
        message->run = [generation, result = std::move(result)](Launcher& app) {
            auto& editor = app.custom_editor_;
            if (generation != app.script_test_generation_ || !editor.open) return; // Closed or another test meanwhile.
            editor.testing = false;
            editor.note = outcome(result, test_timeout);
            editor.note_failed = !result.ok() && !(result.started && !result.finished);
            app.audio_.context();
            InvalidateRect(app.hwnd_, nullptr, FALSE);
        };
        if (PostMessageW(window, remote_message, 0, reinterpret_cast<LPARAM>(message.get()))) message.release();
    };
    if (!work_.start([text, context, alive, runner, deliver] {
            deliver(runner ? runner(text, context) : launch_scripts::run(text, context, test_timeout, [alive] { return !alive->load(); }));
        })) {
        ui.testing = false;
        ui.note = tr(L"Nie udało się uruchomić skryptu");
        ui.note_failed = true;
    }
}
