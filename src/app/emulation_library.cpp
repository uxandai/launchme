// Gry emulowane straight from the ROM folder (Ustawienia → Biblioteka → Gry emulowane): launchme's own
// parsers (src/library/emulation_scan.hpp) read it on a worker; the games join the library and the
// Emulacja shelves in place of Steam ROM Manager's shortcuts of emulated platforms and its collections
// (buildShelves). With no ROM folder found, those shortcuts stay. A ROM added by hand (Dodaj grę,
// a file dropped on the window) starts through the same parsers (emulationAction).
#include "launcher.hpp"
#include "../library/emulation_scan.hpp"
#include "../platform/file_dialog.hpp"
#include "../ui/file_browser_view.hpp"

#include <algorithm>

using namespace settings_ui;

bool Launcher::romLibraryActive() const {
    return settings_.emulation_source == AppSettings::EmulationSource::roms && emulation_setup_ && !emulation_setup_->paths.roms.empty();
}

bool Launcher::srmCollectionsHidden() const { return settings_.show_emulation || romLibraryActive(); }

void Launcher::startRomScan() {
    if (!runtime_started_ && !hwnd_) return;
    const auto generation = ++rom_scan_generation_;
    if (settings_.emulation_source != AppSettings::EmulationSource::roms) {
        // Steam's shortcuts again: the folder's games leave (the setup stays for games added by hand).
        if (!rom_library_.empty()) { rom_library_.clear(); buildShelves(); rebuildCards(); InvalidateRect(hwnd_, nullptr, FALSE); }
    }
    rom_scanning_ = true;
    const auto folder = settings_.emulation_roms;
    const bool scan_games = settings_.emulation_source == AppSettings::EmulationSource::roms;
    const bool hidden = settings_.show_private;
    work_.start([alive = alive_, window = hwnd_, generation, folder, scan_games, hidden] {
        auto setup = std::make_shared<emulation_scan::Setup>(emulation_scan::load(emulation_scan::systemEnvironment(), folder));
        auto games = std::make_shared<std::vector<Game>>();
        if (scan_games) *games = emulation_scan::scan(*setup, hidden, [&] { return !alive->load(); }).games;
        if (!alive->load()) return;
        auto message = std::make_unique<RemoteMessage>();
        message->run = [generation, setup, games](Launcher& app) mutable { app.onRomScan(generation, std::move(setup), std::move(*games)); };
        if (PostMessageW(window, remote_message, 0, reinterpret_cast<LPARAM>(message.get()))) message.release();
    });
}

void Launcher::onRomScan(uint64_t generation, std::shared_ptr<const emulation_scan::Setup> setup, std::vector<Game> games) {
    if (generation != rom_scan_generation_) return;
    rom_scanning_ = false;
    const bool was_active = romLibraryActive();
    emulation_setup_ = std::move(setup);
    // Games added by hand start through the setup's parsers: their commands as it now finds them.
    rebuildCustomLibrary();
    std::sort(games.begin(), games.end(), [](const Game& a, const Game& b) { return gameKey(a) < gameKey(b); });
    const auto same = [&] {
        if (games.size() != rom_library_.size()) return false;
        for (size_t i = 0; i < games.size(); ++i)
            if (gameKey(games[i]) != gameKey(rom_library_[i]) || games[i].name != rom_library_[i].name || games[i].platform != rom_library_[i].platform ||
                games[i].play_action.target != rom_library_[i].play_action.target || games[i].play_action.arguments != rom_library_[i].play_action.arguments)
                return false;
        return true;
    };
    if (same() && was_active == romLibraryActive()) { if (settingsOpen()) buildSettingsRows(); return; }
    rom_library_ = std::move(games);
    applyCustomGames(); // Shelves, cards and the focus by identity.
    if (settingsOpen()) buildSettingsRows();
}

std::wstring Launcher::emulationFolderSummary() const {
    if (!emulation_setup_) return tr(L"Szukanie folderu ROM-ów…");
    const auto& paths = emulation_setup_->paths;
    if (paths.roms.empty()) return settings_.emulation_roms.empty() ? tr(L"Nie znaleziono. Wybierz folder ROM-ów (np. Emulation\\roms).") : tr(L"Tego folderu nie ma.");
    const std::wstring origin = paths.origin == L"manual" ? std::wstring(tr(L"wybrany")) : paths.origin;
    std::wstring text = paths.roms.wstring() + L" · " + origin;
    if (romLibraryActive()) text += L" · " + std::wstring(tr(L"gier: ")) + std::to_wstring(rom_library_.size());
    return text;
}

// The command for an emulated game added by hand: its own typed command, else its platform's parser.
GameAction Launcher::emulationAction(const std::wstring& platform, const std::filesystem::path& rom, const std::wstring& command) const {
    if (!command.empty()) return emulation_scan::commandAction(command, rom);
    if (!emulation_setup_) return {};
    const auto* rule = emulation_setup_->forFile(platform, rom);
    return rule ? emulation_scan::action(*rule, *emulation_setup_, rom) : GameAction{};
}

bool Launcher::emulationSettingsChoose(const Row& row) {
    if (row.id != Id::emulation_source) return false;
    settings_.emulation_source = settings_state_.choice == 1 ? AppSettings::EmulationSource::steam : AppSettings::EmulationSource::roms;
    settings_state_.editing = false;
    settingsChanged(true);
    startRomScan();
    return true;
}

bool Launcher::emulationSettingsActivate(const Row& row) {
    if (row.id == Id::emulation_folder_reset) {
        settings_.emulation_roms.clear();
        settingsChanged(); audio_.context();
        settings_state_.selected[settings_state_.category] = std::max(0, settings_state_.row() - 1); // Its row is gone.
        startRomScan();
        return true;
    }
    if (row.id != Id::emulation_folder) return false;
    const auto apply = [this](const std::filesystem::path& picked) {
        if (!settingsOpen() || picked.empty()) return;
        settings_.emulation_roms = picked;
        settingsChanged(); audio_.context();
        startRomScan();
    };
    file_browser::Request request;
    request.mode = file_browser::Mode::folder;
    request.title = tr(L"Wybierz folder ROM-ów");
    request.initial = !settings_.emulation_roms.empty() ? settings_.emulation_roms :
                      emulation_setup_ && !emulation_setup_->paths.roms.empty() ? emulation_setup_->paths.roms : std::filesystem::path{};
    request.native = [this, initial = request.initial] { return pickPath(hwnd_, true, tr(L"Wybierz folder ROM-ów"), {}, {}, false, initial); };
    openFileBrowser(std::move(request), apply);
    return true;
}
