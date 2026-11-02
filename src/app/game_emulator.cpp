// Emulator per gra: one emulated game's own emulator, over its system's (Settings → Emulacja → a
// system). Details' "…" → Emulator (Tryb TV) and the desktop window's game menu walk the choices:
// Automatycznie (the system's), every emulator of the system found on this PC, the game's own
// program, and "Inny program…" (picks one). Kept by game key in library-state (game-emulator rows);
// the ROM folder's scan and the games added by hand start through it (a typed command still wins).
#include "launcher.hpp"
#include "../library/emulators.hpp"
#include "../platform/file_dialog.hpp"
#include "../ui/file_browser_view.hpp"

std::vector<Launcher::EmulatorChoice> Launcher::gameEmulatorChoices(const Game& game) const {
    std::vector<EmulatorChoice> choices;
    if (!emulation_setup_ || game.kind != GameKind::nonsteam || game.rom.empty()) return choices;
    const auto* rule = emulation_setup_->forFile(game.platform, game.rom);
    if (!rule) return choices;
    const auto name = [](std::wstring_view id) { const auto* info = emulators::find(id); return info ? std::wstring(info->name) : std::wstring(id); };
    const auto* system = emulation_setup_->emulatorFor(*rule);
    choices.push_back({std::wstring(tr(L"Automatycznie")) + (system ? L" · " + name(system->id) : std::wstring{}), {}, {}, false});
    for (const auto& id : rule->emulators) {
        const auto* found = emulation_setup_->found(id);
        const auto* info = emulators::find(id);
        if (!found || !found->found() || (info && info->manual_only)) continue; // Manual-only: the game's own program below.
        choices.push_back({name(id), text_encoding::utf8(id), {}, false});
    }
    if (const auto own = library_state_.game_emulators.find(gameKey(game)); own != library_state_.game_emulators.end() && !own->second.program.empty())
        choices.push_back({std::filesystem::path(own->second.program).filename().wstring(), {}, own->second.program, false});
    choices.push_back({tr(L"Inny program…"), {}, {}, true});
    return choices;
}

int Launcher::gameEmulatorIndex(const Game& game, const std::vector<EmulatorChoice>& choices) const {
    const auto own = library_state_.game_emulators.find(gameKey(game));
    if (own == library_state_.game_emulators.end()) return 0;
    for (size_t i = 1; i < choices.size(); ++i)
        if (!choices[i].pick && ((!own->second.id.empty() && choices[i].id == own->second.id) ||
                                 (!own->second.program.empty() && choices[i].program == own->second.program))) return static_cast<int>(i);
    return 0; // Its emulator is not found any more: the system's starts it.
}

bool Launcher::gameEmulatorEditable(const Game& game) const {
    if (gameEmulatorChoices(game).empty()) return false;
    if (game.source == GameSource::custom && !game.source_id.empty()) // Added by hand: its typed command wins.
        for (const auto& entry : custom_games_) if (entry.id == game.source_id) return entry.arguments.empty();
    return true;
}

std::wstring Launcher::gameEmulatorLabel(const Game& game) const {
    const auto choices = gameEmulatorChoices(game);
    return choices.empty() ? std::wstring{} : std::wstring(tr(L"Emulator: ")) + choices[static_cast<size_t>(gameEmulatorIndex(game, choices))].label;
}

GameAction Launcher::gameEmulatorAction(const Game& game) const {
    if (!emulation_setup_ || game.rom.empty()) return {};
    const auto own = library_state_.game_emulators.find(gameKey(game));
    if (own == library_state_.game_emulators.end()) return {};
    const auto* rule = emulation_setup_->forFile(game.platform, game.rom);
    if (!rule) return {};
    if (!own->second.id.empty()) {
        const auto* found = emulation_setup_->found(text_encoding::wide(own->second.id));
        return found && found->found() ? emulation_scan::action(*rule, *emulation_setup_, game.rom, found) : GameAction{};
    }
    // Its own program, with the arguments of the system's emulator (typed or its default).
    emulators::Found program;
    program.id = rule->emulators.empty() ? std::wstring{} : rule->emulators.front();
    program.via = emulators::Via::manual;
    program.program = own->second.program;
    return emulation_scan::action(*rule, *emulation_setup_, game.rom, &program);
}

void Launcher::applyGameEmulators(std::vector<Game>& games) const {
    if (library_state_.game_emulators.empty()) return;
    for (auto& game : games) if (auto own = gameEmulatorAction(game); !own.target.empty()) game.play_action = std::move(own);
}

void Launcher::setGameEmulator(const Game& game, std::optional<LibraryState::GameEmulator> choice) {
    const auto key = gameKey(game);
    if (key.empty()) return;
    if (choice) library_state_.game_emulators[key] = std::move(*choice);
    else library_state_.game_emulators.erase(key);
    library_state_dirty_ = true;
    saveLibraryPreferences();
    // Its command again: the ROM folder's game from the system's parser (then its own), one added by hand rebuilt.
    if (emulation_setup_)
        for (auto& scanned : rom_library_) {
            if (gameKey(scanned) != key) continue;
            if (const auto* rule = emulation_setup_->forFile(scanned.platform, scanned.rom)) scanned.play_action = emulation_scan::action(*rule, *emulation_setup_, scanned.rom);
            if (auto own = gameEmulatorAction(scanned); !own.target.empty()) scanned.play_action = std::move(own);
        }
    rebuildCustomLibrary();
    applyCustomGames();
    audio_.context();
}

void Launcher::cycleGameEmulator(const Game& game, int step) {
    const auto choices = gameEmulatorChoices(game);
    if (choices.empty() || !gameEmulatorEditable(game)) return;
    const int count = static_cast<int>(choices.size());
    const int next = ((gameEmulatorIndex(game, choices) + step) % count + count) % count;
    const auto& choice = choices[static_cast<size_t>(next)];
    if (!choice.pick) {
        if (next == 0) setGameEmulator(game, std::nullopt);
        else setGameEmulator(game, LibraryState::GameEmulator{choice.id, choice.program.wstring()});
        return;
    }
    // "Inny program…": the game's own program, picked (cancelled: as it was).
    const auto apply = [this, game](const std::filesystem::path& picked) {
        if (picked.empty() || !picked.is_absolute()) { InvalidateRect(hwnd_, nullptr, FALSE); return; }
        setGameEmulator(game, LibraryState::GameEmulator{{}, picked.wstring()});
    };
    std::filesystem::path initial;
    if (const auto own = library_state_.game_emulators.find(gameKey(game)); own != library_state_.game_emulators.end()) initial = own->second.program;
    if (pick_path_) { apply(pick_path_(hwnd_, false, initial)); return; } // Tests: an injected picker answers at once.
    file_browser::Request request;
    request.title = std::wstring(tr(L"Emulator dla gry")) + L" · " + game.name;
    if (emulation_setup_ && emulation_setup_->environment.windows) {
        request.extensions = {L".exe", L".bat", L".cmd", L".ps1"};
        request.filter_label = tr(L"Programy (.exe)");
    }
    request.all_files_option = true;
    request.initial = initial;
    request.native = [this, initial, extensions = request.extensions, label = request.filter_label, title = request.title] {
        return pickPath(hwnd_, false, title, extensions, label, true, initial);
    };
    openFileBrowser(std::move(request), apply);
}
