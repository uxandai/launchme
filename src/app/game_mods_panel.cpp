// Opcje uruchamiania → Mody: the panel's rows (custom_game_ui::ModRow) from the game's last scan, the
// method chosen per row, and the one install or removal at a time on a worker (src/library/game_mods_install.hpp),
// whose progress is the panel's note and whose outcome is a toast too (the panel may be closed by then).
#include "launcher.hpp"
#include "../platform/text_encoding.hpp"

#include <algorithm>

using custom_game_ui::Item;
using game_mods::Mod;

namespace {
size_t modIndex(Mod mod) { return static_cast<size_t>(mod); }
} // namespace

void Launcher::modsPanelOpen(const Game& game) {
    auto& ui = custom_editor_;
    mods_.key.clear();
    mods_.anticheat_row = mods_.remove_row = -1;
    mods_.adopted = false;
    ui.mods_offered = false;
    if (!ui.open || !ui.tabs()) return;
    const auto target = modTarget(game);
    if (!target) return;
    mods_game_ = game;
    mods_.key = gameKey(game);
    mods_.emulator = target->emulator;
    for (const auto mod : game_mods::all_mods) mods_.methods[modIndex(mod)] = preferredModMethod(mod);
    ui.mods_offered = true;
    ui.mod_row = 0;
    modsPanelRefresh(false);
}

std::vector<Mod> Launcher::modsPanelMods() const {
    std::vector<Mod> result;
    for (const auto mod : game_mods::all_mods)
        if (!mods_.emulator || game_mods::forEmulators(mod)) result.push_back(mod);
    return result;
}

void Launcher::modsScanArrived(const std::string& key) {
    if (key == mods_.key && custom_editor_.open && custom_editor_.mods_offered) modsPanelRefresh(false);
    InvalidateRect(hwnd_, nullptr, FALSE); // The badges on the game's page.
}

void Launcher::modsPanelRefresh(bool scan) {
    auto& ui = custom_editor_;
    if (!ui.open || !ui.mods_offered || mods_.key.empty()) return;
    if (scan && !mods_.busy) { forgetModScans(mods_.key); requestModScan(mods_game_); }
    const auto found = mods_.scanned.find(mods_.key);
    const GameModsState::Scanned* scanned = found == mods_.scanned.end() ? nullptr : &found->second;
    if (!scanned && !mods_.scanning.contains(mods_.key)) requestModScan(mods_game_);
    if (scanned && !mods_.adopted) {
        // An installed mod keeps its own method in the panel (another one would move it).
        for (const auto mod : game_mods::all_mods) {
            const auto& installed = scanned->state[mod];
            if (installed.present && game_mods::findMethod(mod, installed.method)) mods_.methods[modIndex(mod)] = installed.method;
        }
        mods_.adopted = true;
    }
    const auto mods = modsPanelMods();
    ui.mod_rows.clear();
    for (size_t row = 0; row < mods.size(); ++row) {
        const auto mod = mods[row];
        custom_game_ui::ModRow line;
        line.name = game_mods::name(mod);
        const auto list = game_mods::methods(mod);
        line.methods = list.size() > 1;
        if (const auto* method = game_mods::findMethod(mod, mods_.methods[modIndex(mod)])) line.method = tr(std::wstring(method->label).c_str());
        if (!scanned) {
            line.status = tr(L"Sprawdzanie folderu…");
            line.action = tr(L"Zainstaluj");
            line.available = false;
            ui.mod_rows.push_back(std::move(line));
            continue;
        }
        const auto& state = scanned->state;
        const auto& installed = state[mod];
        line.installed = installed.present;
        line.removable = installed.present && installed.ours;
        std::wstring status;
        const bool own_dlss = mod == Mod::dlss && installed.present && !installed.ours; // The game's own DLSS.
        if (own_dlss) {
            status = tr(L"Pliki gry");
            if (!installed.version.empty()) status += L" · " + installed.version;
        } else if (installed.present) {
            status = installed.ours ? tr(L"Zainstalowany przez Launchme") : tr(L"Zainstalowany innym narzędziem");
            if (!installed.version.empty()) status += L" · " + installed.version;
            if (!installed.method.empty() && mod != Mod::dlss) status += L" · " + text_encoding::wideLenient(installed.method);
            if (!installed.detail.empty()) status += L" · " + installed.detail;
        } else status = tr(L"Nie zainstalowano");
        // What keeps a mod out of this game.
        if (mod == Mod::dlss && !state.has_dlss) { line.available = false; status = tr(L"Gra nie ma plików DLSS"); }
        if ((mod == Mod::dlss5 || mod == Mod::anr) && state.x86 && !installed.present) { line.available = false; status = tr(L"Tylko gry 64-bitowe"); }
        line.status = std::move(status);
        line.action = !installed.present ? tr(L"Zainstaluj") : installed.ours || own_dlss ? tr(L"Aktualizuj") : tr(L"Zastąp");
        if (mods_.anticheat_row == static_cast<int>(row)) line.action = tr(L"Zainstaluj mimo to");
        line.confirm_remove = mods_.remove_row == static_cast<int>(row);
        ui.mod_rows.push_back(std::move(line));
    }
    ui.mod_row = std::clamp(ui.mod_row, 0, std::max(0, ui.modCount() - 1));
    ui.mod_busy = mods_.busy;
    if (scanned) {
        ui.mod_folder = scanned->target.folder.wstring();
        ui.mod_target = scanned->emulator ? std::wstring(tr(L"Emulator: ")) + scanned->emulator_name + tr(L" · mody działają we wszystkich jego grach") : std::wstring{};
        if (ui.mods() && !mods_.busy && ui.note.empty() && !scanned->state.anticheat.empty()) {
            std::wstring names;
            for (const auto& name : scanned->state.anticheat) names += (names.empty() ? L"" : L", ") + name;
            ui.note = std::wstring(tr(L"Zabezpieczenie przed oszustwami: ")) + names + tr(L". Mody tylko w trybie offline.");
            ui.note_failed = true;
        }
    } else if (ui.mod_folder.empty()) ui.mod_folder = tr(L"Sprawdzanie…");
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::modsPanelMethod(int step) {
    auto& ui = custom_editor_;
    const auto mods = modsPanelMods();
    if (!ui.mods() || ui.mod_row < 0 || ui.mod_row >= static_cast<int>(mods.size()) || mods_.busy) return;
    const auto mod = mods[static_cast<size_t>(ui.mod_row)];
    const auto list = game_mods::methods(mod);
    if (list.size() < 2) return;
    auto& chosen = mods_.methods[modIndex(mod)];
    int index = 0;
    for (int i = 0; i < static_cast<int>(list.size()); ++i) if (list[static_cast<size_t>(i)].id == chosen) index = i;
    index = (index + step + static_cast<int>(list.size())) % static_cast<int>(list.size());
    chosen = std::string(list[static_cast<size_t>(index)].id);
    mods_.anticheat_row = mods_.remove_row = -1;
    audio_.navigate();
    modsPanelRefresh(false);
}

std::vector<std::wstring> Launcher::modsPanelMethodLabels(int* chosen) const {
    std::vector<std::wstring> labels;
    const auto mods = modsPanelMods();
    const auto& ui = custom_editor_;
    if (chosen) *chosen = 0;
    if (ui.mod_row < 0 || ui.mod_row >= static_cast<int>(mods.size())) return labels;
    const auto mod = mods[static_cast<size_t>(ui.mod_row)];
    const auto list = game_mods::methods(mod);
    for (size_t i = 0; i < list.size(); ++i) {
        labels.push_back(tr(std::wstring(list[i].label).c_str()));
        if (chosen && list[i].id == mods_.methods[modIndex(mod)]) *chosen = static_cast<int>(i);
    }
    return labels;
}

// A method picked from the row's list: kept for its install.
void Launcher::modsPanelChooseMethod(int index) {
    auto& ui = custom_editor_;
    const auto mods = modsPanelMods();
    if (!ui.mods() || ui.mod_row < 0 || ui.mod_row >= static_cast<int>(mods.size()) || mods_.busy) return;
    const auto mod = mods[static_cast<size_t>(ui.mod_row)];
    const auto list = game_mods::methods(mod);
    if (index < 0 || index >= static_cast<int>(list.size())) return;
    mods_.methods[modIndex(mod)] = std::string(list[static_cast<size_t>(index)].id);
    mods_.anticheat_row = mods_.remove_row = -1;
    modsPanelRefresh(false);
}

void Launcher::modsPanelAction(bool remove) {
    auto& ui = custom_editor_;
    const auto mods = modsPanelMods();
    if (!ui.mods() || mods_.busy || ui.mod_row < 0 || ui.mod_row >= static_cast<int>(mods.size())) return;
    const int row = ui.mod_row;
    const auto mod = mods[static_cast<size_t>(row)];
    const auto found = mods_.scanned.find(mods_.key);
    if (found == mods_.scanned.end()) { navigationBoundary(); return; } // Still scanning.
    const auto& line = ui.mod_rows[static_cast<size_t>(row)];
    if (remove && !line.removable) return;
    if (!remove && !line.available) { ui.note = line.status; ui.note_failed = true; navigationBoundary(); InvalidateRect(hwnd_, nullptr, FALSE); return; }
    if (remove && mods_.remove_row != row) { // Asked twice: the files go back to what they were.
        mods_.remove_row = row;
        ui.note = std::wstring(tr(L"Naciśnij „Usuń” jeszcze raz, by usunąć ")) + game_mods::name(mod) + tr(L" z gry.");
        ui.note_failed = false;
        audio_.context();
        modsPanelRefresh(false);
        return;
    }
    game_mods::Request request;
    request.mod = mod;
    request.remove = remove;
    request.method = mods_.methods[modIndex(mod)];
    request.target = found->second.target;
    request.reshade_method = mods_.methods[modIndex(Mod::reshade)];
    const auto& opti = mods_.methods[modIndex(Mod::optiscaler)];
    request.optiscaler_method = opti == "winmm.dll" || opti == "version.dll" ? opti : std::string("dxgi.dll");
    request.allow_anticheat = mods_.anticheat_row == row;
    mods_.remove_row = -1;
    mods_.busy = true;
    ui.mod_busy = true;
    const auto generation = ++mods_.job_generation;
    mods_.job_key = mods_.key;
    mods_.cancel = std::make_shared<std::atomic_bool>(false);
    ui.note = std::wstring(remove ? tr(L"Usuwanie: ") : tr(L"Instalowanie: ")) + game_mods::name(mod) + L"…";
    ui.note_failed = false;
    audio_.context();
    InvalidateRect(hwnd_, nullptr, FALSE);
    const auto alive = alive_;
    const auto window = hwnd_;
    const auto cancel = mods_.cancel;
    const auto post = [alive, window](std::function<void(Launcher&)> run) {
        if (!alive->load()) return;
        auto message = std::make_unique<RemoteMessage>();
        message->run = std::move(run);
        if (PostMessageW(window, remote_message, 0, reinterpret_cast<LPARAM>(message.get()))) message.release();
    };
    auto services = mod_services_ ? mod_services_(cancel) :
        game_mods::defaultServices([alive, cancel] { return !alive->load() || cancel->load(); },
                                   [post, generation](std::wstring_view text) {
                                       post([generation, text = std::wstring(text)](Launcher& app) {
                                           if (generation != app.mods_.job_generation || !app.custom_editor_.open || app.mods_.key != app.mods_.job_key) return;
                                           app.custom_editor_.note = text;
                                           app.custom_editor_.note_failed = false;
                                           InvalidateRect(app.hwnd_, nullptr, FALSE);
                                       });
                                   });
    const auto data = modsData();
    if (mods_wait_) { modsJobDone(generation, mod, remove, game_mods::apply(request, data, services)); return; }
    const bool started = work_.start([post, generation, mod, remove, request = std::move(request), data, services = std::move(services)] {
        auto outcome = game_mods::apply(request, data, services);
        post([generation, mod, remove, outcome = std::move(outcome)](Launcher& app) mutable { app.modsJobDone(generation, mod, remove, std::move(outcome)); });
    });
    if (!started) modsJobDone(generation, mod, remove, game_mods::Outcome{game_mods::Problem::failed, tr(L"Nie udało się.")});
}

void Launcher::modsJobDone(uint64_t generation, Mod mod, bool remove, game_mods::Outcome outcome) {
    if (generation != mods_.job_generation) return;
    mods_.busy = false;
    mods_.cancel.reset();
    auto& ui = custom_editor_;
    const bool shown = ui.open && ui.mods_offered && !mods_.key.empty() && mods_.key == mods_.job_key; // Not another game's panel.
    const auto rows = modsPanelMods();
    const auto row = static_cast<int>(std::find(rows.begin(), rows.end(), mod) - rows.begin());
    mods_.anticheat_row = outcome.problem == game_mods::Problem::anticheat ? row : -1;
    // Every game sharing the folder (an emulator's) shows the change: every scan goes.
    const auto key = mods_.key;
    forgetModScans({});
    if (outcome.problem != game_mods::Problem::anticheat)
        showNotification(outcome.ok() ? NotificationKind::download : NotificationKind::error,
                         std::wstring(game_mods::name(mod)) + (remove ? tr(L" · usuwanie") : tr(L" · instalacja")), outcome.message);
    audio_.context();
    if (!shown) { modsPanelRefresh(false); InvalidateRect(hwnd_, nullptr, FALSE); return; } // Another game's panel: no longer busy.
    ui.mod_busy = false;
    ui.note = std::move(outcome.message);
    ui.note_failed = !outcome.ok();
    mods_.adopted = false; // What is there now (a changed method) is what the row shows.
    if (!key.empty()) requestModScan(mods_game_);
    modsPanelRefresh(false);
}

void Launcher::modsPanelFolder() {
    auto& ui = custom_editor_;
    if (!ui.mods() || mods_.busy || mods_.emulator || mods_.key.empty()) return;
    const auto found = mods_.scanned.find(mods_.key);
    const auto initial = found == mods_.scanned.end() ? std::filesystem::path{} : found->second.target.folder;
    const auto apply = [this, key = mods_.key](const std::filesystem::path& picked) {
        if (picked.empty() || key != mods_.key) { InvalidateRect(hwnd_, nullptr, FALSE); return; }
        // The program's own folder again: no choice to keep.
        const auto& game = mods_game_;
        if (game.executable.is_absolute() && game.executable.parent_path() == picked) library_state_.mod_folders.erase(key);
        else library_state_.mod_folders[key] = picked.wstring();
        library_state_dirty_ = true;
        saveLibraryPreferences();
        mods_.adopted = false;
        custom_editor_.note.clear();
        modsPanelRefresh(true);
    };
    if (pick_path_) {
        const auto picked = pick_path_(hwnd_, true, initial);
        mouse_transition_until_ = GetTickCount64() + GetDoubleClickTime();
        apply(picked);
        return;
    }
    file_browser::Request request;
    request.mode = file_browser::Mode::folder;
    request.title = tr(L"Folder programu gry");
    request.initial = initial;
    request.native = [this, initial] { return pickGamePath(hwnd_, true, initial); };
    openFileBrowser(std::move(request), apply);
}

void Launcher::customEditorTabStep(int step) {
    auto& ui = custom_editor_;
    if (!ui.open || !ui.tabs()) return;
    std::vector<custom_game_ui::Tab> tabs{custom_game_ui::Tab::launch, custom_game_ui::Tab::scripts};
    if (ui.mods_offered) tabs.push_back(custom_game_ui::Tab::mods);
    const auto at = static_cast<int>(std::find(tabs.begin(), tabs.end(), ui.tab) - tabs.begin());
    const int next = at + step;
    if (next < 0 || next >= static_cast<int>(tabs.size())) { navigationBoundary(); return; }
    customEditorTab(tabs[static_cast<size_t>(next)]);
}
