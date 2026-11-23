// Opcje uruchamiania → Uruchamianie, the options under the fields (custom_game_ui::Option): an emulated
// game's emulator (game_emulator.cpp), a Steam game's remembered account and Aurelia instead of the
// Steam client (with "…": Zarządzaj · Aurelia), and the switches (logs, HDR; launch_flags.cpp). Like the
// fields they wait for Zapisz (launchChoicesSave); the line under the panel's title says what starts the
// game. A choice, and a mod's method, opens a list (custom_game_ui::Dropdown).
#include "launcher.hpp"
#include "../library/game_log.hpp"
#include <shellapi.h>
#include "../library/custom_games.hpp"
#include "../library/emulators.hpp"
#include "../store/aurelia.hpp"

#include <algorithm>

using custom_game_ui::Item;
using custom_game_ui::Option;
using Kind = custom_game_ui::Option::Kind;

namespace {
std::wstring fileName(const std::filesystem::path& program) { return program.filename().wstring(); }
// A remembered account by its name on Steam (0: the one signed in).
std::wstring accountName(const std::vector<steam_accounts::Account>& remembered, uint64_t id) {
    for (const auto& account : remembered)
        if (id && account.steam_id == id) return account.persona.empty() ? text_encoding::wideLenient(account.account_name) : account.persona;
    return tr(L"domyślne");
}
}

// The options of the game the panel opens for, from what is saved now.
void Launcher::customEditorFlags(custom_game_ui::State& ui, const Game& game) {
    const auto key = gameKey(game);
    ui.flags_key = key;
    ui.flags = !key.empty();
    ui.options.clear();
    ui.option = 0;
    if (const auto* scripts = launchScripts(key)) { // Its scripts' tab.
        ui.fields[custom_game_ui::before_field] = scripts->before;
        ui.fields[custom_game_ui::started_field] = scripts->started;
        ui.fields[custom_game_ui::exited_field] = scripts->exited;
    }
    if (!ui.flags) return;
    auto& choices = launch_choices_;
    choices = {};
    choices.game = game;
    if (gameEmulatorEditable(game)) { // A typed emulator command (a game added by hand) wins over the choice.
        choices.emulators = gameEmulatorChoices(game);
        choices.emulator = gameEmulatorIndex(game, choices.emulators);
        ui.options.push_back({Kind::emulator, tr(L"Emulator")});
    }
    choices.accounts = steamAccountChoices(game);
    if (!choices.accounts.empty()) {
        const auto found = settings_.steam_launch_accounts.find(std::to_string(game.app_id));
        const auto at = found == settings_.steam_launch_accounts.end() ? choices.accounts.end() :
                        std::find(choices.accounts.begin(), choices.accounts.end(), found->second);
        choices.account = at == choices.accounts.end() ? 0 : static_cast<int>(at - choices.accounts.begin());
        ui.options.push_back({Kind::steam_account, tr(L"Konto Steam")});
    }
    // Aurelia or the Steam client: only where both can (a borrowed game keeps the client that lent it;
    // without a Steam client every game goes through Aurelia).
    choices.aurelia_shown = aurelia_active_ && aurelia::validGame(game) && !(game.family_shared && !aurelia_library_source_) &&
                            !(aurelia_library_source_ && !aurelia_steam_client_);
    choices.aurelia = aureliaManaged(game);
    if (choices.aurelia_shown) ui.options.push_back({Kind::aurelia, tr(L"Zarządzaj przez Aurelię"), {}, choices.aurelia, true});
    // A plugin launch provider offered for the game: a switch choosing it as the launch route.
    choices.launch_provider.clear(); choices.launch_on = false;
    if (game.app_id && game.installed) {
        const auto providers = plugins_.contributions("launch", &game);
        if (!providers.empty()) {
            const auto chosen = selectedLaunchProvider(game);
            const auto& provider = chosen ? *chosen : providers.front();
            choices.launch_provider = provider.plugin;
            choices.launch_on = chosen.has_value();
            auto label = provider.item.title.localized();
            if (label.empty()) label = tr(L"Uruchom przez wtyczkę");
            ui.options.push_back({Kind::launch, label, {}, choices.launch_on});
        }
    }
    const unsigned bits = launchFlags(game);
    ui.options.push_back({Kind::log, tr(L"Zapisuj logi"), {}, (bits & launch_flag::log) != 0});
    // Aurelia's own output (-vv) of its launches and saves' syncs, where Aurelia can run the game.
    if (aurelia_active_ && aurelia::validGame(game) && (choices.aurelia_shown || choices.aurelia))
        ui.options.push_back({Kind::aurelia_log, tr(L"Zapisuj log Aurelii"), {}, (bits & launch_flag::aurelia_log) != 0});
#ifdef _WIN32
    ui.options.push_back({Kind::hdr, tr(L"HDR na czas gry"), {}, (bits & launch_flag::hdr) != 0});
#endif
    launchChoicesShow(ui);
}

// The choices' values and the line under the title, as chosen so far.
void Launcher::launchChoicesShow(custom_game_ui::State& ui) const {
    const auto& choices = launch_choices_;
    const auto& game = choices.game;
    std::wstring emulator; // The one that starts it.
    if (!choices.emulators.empty() && choices.emulator >= 0 && choices.emulator < static_cast<int>(choices.emulators.size())) {
        const auto& choice = choices.emulators[static_cast<size_t>(choices.emulator)];
        emulator = choice.pick ? (choices.picked.empty() ? std::wstring{} : fileName(choices.picked)) : choice.label;
        if (choices.emulator == 0) { // "Automatycznie · Cemu": the system's.
            const auto dot = emulator.find(L" · ");
            emulator = dot == std::wstring::npos ? std::wstring{} : emulator.substr(dot + 3);
        }
    }
    for (auto& option : ui.options) {
        // Zapisuj logi, Zapisuj log Aurelii: while on, "…" opens their folder.
        if (option.kind == Kind::log || option.kind == Kind::aurelia_log) option.more = option.on && !gameKey(game).empty();
        if (option.kind == Kind::emulator && choices.emulator >= 0 && choices.emulator < static_cast<int>(choices.emulators.size())) {
            const auto& choice = choices.emulators[static_cast<size_t>(choices.emulator)];
            option.value = choice.pick && !choices.picked.empty() ? fileName(choices.picked) : choice.label;
        } else if (option.kind == Kind::steam_account && choices.account >= 0 && choices.account < static_cast<int>(choices.accounts.size())) {
            option.value = accountName(rememberedSteamAccounts(), choices.accounts[static_cast<size_t>(choices.account)]);
        }
    }
    const bool aurelia = choices.aurelia_shown ? ui.switchOn(Kind::aurelia) : choices.aurelia;
    std::wstring line;
    if (game.source == GameSource::steam && game.kind == GameKind::steam)
#ifdef _WIN32
        line = aurelia ? tr(L"Uruchamia Aurelia: czas gry i osiągnięcia tylko przy Steamie włączonym na tym koncie; opcje niżej działają tylko przez Steam.") :
#else
        line = aurelia ? tr(L"Uruchamia Aurelia, bez Steam: bez czasu gry i osiągnięć; opcje niżej działają tylko przez Steam.") :
#endif
                         tr(L"Uruchamia Steam: opcje niżej dopisuje do steam.exe -applaunch.");
    else if (!game.rom.empty()) {
        if (ui.target == custom_game_ui::Target::custom && !custom_games::trim(ui.fields[custom_game_ui::arguments_field]).empty())
            line = tr(L"Uruchamia polecenie emulatora wpisane niżej.");
        else if (!emulator.empty()) line = std::wstring(tr(L"Uruchamia emulator: ")) + emulator + L".";
        else if (!choices.emulators.empty() && choices.emulators[static_cast<size_t>(std::max(0, choices.emulator))].pick)
            line = tr(L"Uruchomi go program wskazany w „Emulator”.");
        else line = tr(L"Uruchamia emulator systemu (Ustawienia → Emulacja).");
    } else if (game.source == GameSource::custom) line = tr(L"Uruchamia plik gry wskazany niżej.");
    else if (game.source == GameSource::drmfree) line = tr(L"Uruchamia plik gry wskazany niżej (DRM-free).");
    else line = std::wstring(tr(L"Uruchamia klient ")) + sourceName(game.source) + tr(L" (bez opcji uruchamiania).");
    ui.managed = std::move(line);
}

void Launcher::customEditorOption(int index) {
    auto& ui = custom_editor_;
    if (!ui.open || index < 0 || index >= ui.optionCount()) return;
    if (ui.focus == custom_game_ui::Item::option && ui.option == index) return;
    ui.option = index;
    ui.focus = custom_game_ui::Item::option;
    ui.selected = ui.confirm_remove = false;
    audio_.navigate();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

// Left/Right on an option: a choice steps back or on (never into the picker), a switch flips.
void Launcher::customEditorOptionStep(int step) {
    auto& ui = custom_editor_;
    if (ui.option < 0 || ui.option >= ui.optionCount()) return;
    auto& option = ui.options[static_cast<size_t>(ui.option)];
    auto& choices = launch_choices_;
    const auto walk = [&](int& index, int count) {
        if (count < 2) { navigationBoundary(); return false; }
        index = ((index + step) % count + count) % count;
        return true;
    };
    if (option.kind == Kind::emulator) { if (!walk(choices.emulator, static_cast<int>(choices.emulators.size()))) return; }
    else if (option.kind == Kind::steam_account) { if (!walk(choices.account, static_cast<int>(choices.accounts.size()))) return; }
    else option.on = !option.on;
    launchChoicesShow(ui);
    audio_.navigate();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

// A or a click: a switch flips, a choice opens its list.
void Launcher::customEditorOptionAccept() {
    auto& ui = custom_editor_;
    if (ui.option < 0 || ui.option >= ui.optionCount()) return;
    if (ui.options[static_cast<size_t>(ui.option)].choice()) customEditorDropdown(Item::option, ui.option);
    else customEditorOptionStep(1);
}

void Launcher::customEditorDropdown(Item owner, int index) {
    auto& ui = custom_editor_;
    if (!ui.open) return;
    custom_game_ui::Dropdown list;
    list.owner = owner;
    list.index = index;
    if (owner == Item::mod_method) {
        if (!ui.mods() || mods_.busy || ui.mod_row < 0 || ui.mod_row >= ui.modCount()) return;
        list.index = ui.mod_row;
        list.items = modsPanelMethodLabels(&list.chosen);
    } else {
        if (index < 0 || index >= ui.optionCount()) return;
        const auto& choices = launch_choices_;
        const auto kind = ui.options[static_cast<size_t>(index)].kind;
        if (kind == Kind::emulator) {
            for (const auto& choice : choices.emulators)
                list.items.push_back(choice.pick && !choices.picked.empty() ? choice.label + L"  ·  " + fileName(choices.picked) : choice.label);
            list.chosen = choices.emulator;
        } else if (kind == Kind::steam_account) {
            for (const auto id : choices.accounts) list.items.push_back(accountName(rememberedSteamAccounts(), id));
            list.chosen = choices.account;
        } else return;
    }
    if (list.items.empty()) return;
    list.chosen = std::clamp(list.chosen, 0, static_cast<int>(list.items.size()) - 1);
    list.focus = list.chosen;
    list.open = true;
    ui.dropdown = std::move(list);
    audio_.context();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::customEditorDropdownMove(int step) {
    auto& list = custom_editor_.dropdown;
    if (!list.open) return;
    const int next = list.focus + step;
    if (next < 0 || next >= static_cast<int>(list.items.size())) { navigationBoundary(); return; }
    list.focus = next;
    audio_.navigate();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::customEditorDropdownClose() {
    if (!custom_editor_.dropdown.open) return;
    custom_editor_.dropdown = {};
    audio_.navigate();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

// A value picked: an emulator ("Inny program…" opens the picker), an account, a mod's method (then on to
// its install).
void Launcher::customEditorDropdownChoose(int item) {
    auto& ui = custom_editor_;
    if (!ui.dropdown.open) return;
    const auto list = ui.dropdown;
    ui.dropdown = {};
    InvalidateRect(hwnd_, nullptr, FALSE);
    if (item < 0 || item >= static_cast<int>(list.items.size())) return;
    audio_.navigate();
    if (list.owner == Item::mod_method) {
        modsPanelChooseMethod(item);
        customEditorFocus(Item::mod_action);
        return;
    }
    if (list.index < 0 || list.index >= ui.optionCount()) return;
    auto& choices = launch_choices_;
    const auto kind = ui.options[static_cast<size_t>(list.index)].kind;
    if (kind == Kind::steam_account) choices.account = item;
    else if (kind == Kind::emulator && item < static_cast<int>(choices.emulators.size())) {
        choices.emulator = item;
        if (choices.emulators[static_cast<size_t>(item)].pick) {
            pickGameEmulator(choices.game, choices.picked, [this](const std::filesystem::path& picked) {
                if (!custom_editor_.open || picked.empty() || !picked.is_absolute()) { InvalidateRect(hwnd_, nullptr, FALSE); return; }
                launch_choices_.picked = picked;
                launchChoicesShow(custom_editor_);
                InvalidateRect(hwnd_, nullptr, FALSE);
            });
        }
    }
    launchChoicesShow(ui);
}

void Launcher::customEditorOptionMore() {
    auto& ui = custom_editor_;
    if (!ui.open || ui.option < 0 || ui.option >= ui.optionCount()) return;
    const auto kind = ui.options[static_cast<size_t>(ui.option)].kind;
    const auto game = launch_choices_.game;
    if (kind == Kind::log || kind == Kind::aurelia_log) { // The logs' folder (made when there are none yet).
        const auto key = gameKey(game);
        if (key.empty()) return;
        const auto folder = game_log::folder(userConfigDirectory(), game.name, key, kind == Kind::aurelia_log ? L"aurelia" : L"");
        std::error_code error;
        std::filesystem::create_directories(folder, error);
        const bool shown = !error && (reveal_download_ ? reveal_download_(folder) :
                           reinterpret_cast<INT_PTR>(ShellExecuteW(hwnd_, L"open", folder.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL)) > 32);
        if (!shown) showNotification(NotificationKind::error, tr(L"Nie można otworzyć folderu"), folder.wstring());
        return;
    }
    if (kind != Kind::aurelia || !launch_choices_.aurelia_shown) return;
    ui.dropdown = {};
    openAurelia(&game);
}

// Zarządzaj · Aurelia closed over the panel: a change made there shows on the switch (Zapisz keeps it).
void Launcher::launchChoicesAureliaClosed() {
    auto& ui = custom_editor_;
    auto& choices = launch_choices_;
    if (!ui.open || !ui.flags || !choices.aurelia_shown) return;
    const bool now = aureliaManaged(choices.game);
    if (now == choices.aurelia) return;
    choices.aurelia = now;
    for (auto& option : ui.options) if (option.kind == Kind::aurelia) option.on = now;
    launchChoicesShow(ui);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

unsigned Launcher::customEditorFlagBits() const {
    const auto& ui = custom_editor_;
    // Not offered now (Aurelia off): the Aurelia log switch stays as it was.
    const bool offered = std::any_of(ui.options.begin(), ui.options.end(), [](const auto& o) { return o.kind == Kind::aurelia_log; });
    const unsigned aurelia_log = offered ? (ui.switchOn(Kind::aurelia_log) ? launch_flag::aurelia_log : 0u) :
                                           launchFlags(launch_choices_.game) & launch_flag::aurelia_log;
    return (ui.switchOn(Kind::log) ? launch_flag::log : 0u) | (ui.switchOn(Kind::hdr) ? launch_flag::hdr : 0u) | aurelia_log;
}

// Zapisz: the switches, the scripts and the choices that changed.
void Launcher::launchChoicesSave(const std::string& key) {
    const auto& ui = custom_editor_;
    if (!ui.flags || key.empty()) return;
    setLaunchFlags(key, customEditorFlagBits());
    setLaunchScripts(key, customEditorScripts());
    const auto choices = launch_choices_;
    const auto& game = choices.game;
    if (gameKey(game) != key) return; // A new game added by hand: no choices yet.
    bool settings_changed = false;
    if (choices.emulator >= 0 && choices.emulator < static_cast<int>(choices.emulators.size())) {
        const auto& choice = choices.emulators[static_cast<size_t>(choices.emulator)];
        if (choice.pick) {
            if (!choices.picked.empty()) setGameEmulator(game, LibraryState::GameEmulator{{}, choices.picked.wstring()});
        } else if (choices.emulator != gameEmulatorIndex(game, gameEmulatorChoices(game))) {
            if (choices.emulator == 0) setGameEmulator(game, std::nullopt);
            else setGameEmulator(game, LibraryState::GameEmulator{choice.id, choice.program.wstring()});
        }
    }
    if (choices.account >= 0 && choices.account < static_cast<int>(choices.accounts.size())) {
        const auto id = std::to_string(game.app_id);
        const uint64_t wanted = choices.accounts[static_cast<size_t>(choices.account)];
        const auto found = settings_.steam_launch_accounts.find(id);
        const uint64_t current = found == settings_.steam_launch_accounts.end() ? 0 : found->second;
        if (wanted != current) {
            if (wanted) settings_.steam_launch_accounts[id] = wanted; else settings_.steam_launch_accounts.erase(id);
            settings_changed = true;
        }
    }
    if (choices.aurelia_shown) {
        const bool wanted = ui.switchOn(Kind::aurelia);
        if (wanted != aureliaManaged(game)) {
            const auto id = std::to_string(game.app_id);
            // As Zarządzaj · Aurelia's own switch: the exceptions to the default.
            if (settings_.aurelia_default) { if (wanted) settings_.aurelia_steam.erase(id); else settings_.aurelia_steam.insert(id); }
            else { if (wanted) settings_.aurelia_standalone.insert(id); else settings_.aurelia_standalone.erase(id); }
            settings_changed = true;
            if (wanted) aureliaManagedNow(game); // Its cloud saves synced at once (aurelia.cpp).
        }
    }
    if (!choices.launch_provider.empty()) {
        const bool wanted = ui.switchOn(Kind::launch);
        const auto id = std::to_string(game.app_id);
        const auto found = settings_.plugin_launch.find(id);
        const bool current = found != settings_.plugin_launch.end();
        if (wanted != current) {
            if (wanted) settings_.plugin_launch[id] = choices.launch_provider; else settings_.plugin_launch.erase(id);
            settings_changed = true;
        } else if (wanted && found->second != choices.launch_provider) {
            found->second = choices.launch_provider; settings_changed = true;
        }
    }
    if (settings_changed) { settings_dirty_ = true; saveSettings(); }
}
