// Ustawienia → Biblioteka → Emulacja: one tile under the DRM-free catalogue with everything about
// emulation behind it (the console row, where emulated games come from, the ROM folder, names and
// artwork by ROM) and a page per system: its emulator (src/library/emulators.hpp), how and where it
// was found, its program and launch arguments set by hand, and the command a game starts with.
// Kept apart from settings.cpp's long chains (MSVC's nesting limit).
#include "launcher.hpp"
#include "../library/emulation_scan.hpp"
#include "../library/emulators.hpp"
#include "../platform/file_dialog.hpp"
#include "../platform/text_encoding.hpp"
#include "../ui/file_browser_view.hpp"

#include <algorithm>
#include <set>

using namespace settings_ui;

namespace {
const wchar_t* viaLabel(emulators::Via via) {
    switch (via) {
    case emulators::Via::manual: return tr(L"Wskazany ręcznie");
    case emulators::Via::emudeck_script: return tr(L"Skrypt EmuDecka");
    case emulators::Via::emudeck_exe: return tr(L"EmuDeck (program)");
    case emulators::Via::esde_rule: return tr(L"Reguła ES-DE");
    case emulators::Via::registry: return tr(L"Rejestr Windows");
    case emulators::Via::known_path: return tr(L"Ścieżka bezpośrednia");
    case emulators::Via::path_env: return L"PATH";
    case emulators::Via::flatpak: return L"Flatpak";
    case emulators::Via::appimage: return L"AppImage";
    case emulators::Via::none: break;
    }
    return tr(L"Nie znaleziono");
}
std::wstring emulatorName(std::wstring_view id) {
    const auto* info = emulators::find(id);
    return info ? std::wstring(info->name) : std::wstring(id);
}
}

// The system a page or row stands for (Row::key / the page's key: its folder, UTF-8).
const emulation_scan::Rule* Launcher::emulationRule(const std::string& key) const {
    return emulation_setup_ ? emulation_setup_->forSystem(text_encoding::wideLenient(key)) : nullptr;
}

std::wstring Launcher::emulationSystemTitle(const std::wstring& system) const {
    const auto platform = emulation_scan::platformForSystem(system);
    // Folders sharing a platform (megadrive, genesis) or none naming one: the folder after it.
    size_t sharing = 0;
    if (emulation_setup_) {
        std::set<std::wstring> seen;
        for (const auto& rule : emulation_setup_->rules)
            if (rule.platform == platform && seen.insert(rule.system).second) ++sharing;
    }
    return sharing > 1 || platform == L"Retro" ? platform + L" (" + system + L")" : platform;
}

// "Skrypt EmuDecka (melonDS.ps1)", "RetroArch + rdzeń mgba_libretro"...
std::wstring Launcher::emulationFoundText(const emulation_scan::Rule& rule, const emulators::Found& found) const {
    std::wstring text = viaLabel(found.via);
    if (!found.detail.empty() && found.via != emulators::Via::esde_rule) text += L" (" + found.detail + L")";
    else if (found.via == emulators::Via::esde_rule) text = found.detail;
    if (found.id == L"retroarch" && !rule.core.empty())
        text += L" · " + ((emulation_setup_ && emulation_scan::coreMissing(rule, *emulation_setup_) ? tr(L"brak rdzenia ") : tr(L"rdzeń ")) + rule.core);
    return text;
}

Row Launcher::emulationTileRow() const {
    size_t found = 0;
    if (emulation_setup_)
        found = static_cast<size_t>(std::count_if(emulation_setup_->detected.begin(), emulation_setup_->detected.end(),
                                                  [](const emulators::Found& entry) { return entry.found(); }));
    std::wstring value = rom_scanning_ && !emulation_setup_ ? std::wstring(tr(L"Szukanie…")) :
        tr(L"Gry: ") + std::to_wstring(romLibraryActive() ? rom_library_.size() : 0) + L" · " + tr(L"Emulatory: ") + std::to_wstring(found);
    Row row{Id::page, Control::page, tr(L"Emulacja"), tr(L"Folder ROM-ów, emulatory dla każdego systemu i rząd konsol."), std::move(value)};
    row.page = Page::emulation;
    row.key = std::to_string(static_cast<int>(Page::emulation));
    return row;
}

void Launcher::emulationSettingsRows() {
    std::wstring next_section;
    const auto section = [&](const wchar_t* title) { next_section = title; };
    const auto add = [&](Row row) {
        if (!next_section.empty()) { row.section = std::move(next_section); next_section.clear(); }
        settings_rows_.push_back(std::move(row));
    };
    if (settings_state_.at(Page::emulation_system)) {
        const auto key = settings_state_.top()->key;
        const auto* rule = emulationRule(key);
        if (!rule) return; // The setup is still being found: the page fills when it is.
        const auto& setup = *emulation_setup_;
        const auto chosen = setup.chosen(*rule);
        const auto* found = setup.emulatorFor(*rule);
        section(tr(L"Gry emulowane"));
        const auto own_folder = settings_.system_roms.find(key);
        const bool custom_folder = own_folder != settings_.system_roms.end() && !own_folder->second.empty();
        {
            const auto folder = custom_folder ? own_folder->second : rule->folder;
            std::wstring description = folder.empty() ? std::wstring(tr(L"Nie wybrano folderu.")) : folder.wstring();
            if (!folder.empty() && !rule->present) description += L" · " + std::wstring(tr(L"Tego folderu nie ma."));
            description += L"\n" + std::wstring(tr(L"Wybierz katalog z grami tylko tego systemu. Zastępuje jego podfolder w głównym folderze ROM-ów."));
            Row folder_row{Id::emulation_system_folder, Control::action, tr(L"Folder ROM-ów"), std::move(description), tr(L"Zmień")};
            folder_row.key = key;
            folder_row.lines = 3;
            add(std::move(folder_row));
        }
        {
            // Its folder read again (new games, an update, games removed), with what it holds now.
            const auto count = rom_system_counts_.find(rule->system);
            std::wstring description = rom_scanning_ ? std::wstring(tr(L"Szukanie gier…")) :
                std::wstring(tr(L"Gier: ")) + std::to_wstring(count == rom_system_counts_.end() ? 0 : count->second);
            description += L" · " + std::wstring(tr(L"Czyta folder jeszcze raz: nowe gry, aktualizacje i usunięte."));
            Row rescan{Id::emulation_system_rescan, Control::action, tr(L"Szukaj gier ponownie"), std::move(description), tr(L"Szukaj")};
            rescan.key = key;
            add(std::move(rescan));
        }
        if (custom_folder) {
            Row reset{Id::emulation_system_folder_reset, Control::action, tr(L"Przywróć domyślny folder ROM-ów"),
                      tr(L"Użyj podfolderu tego systemu w głównym folderze ROM-ów."), tr(L"Przywróć")};
            reset.key = key;
            add(std::move(reset));
        }
        section(tr(L"Emulator"));
        const auto* profile = rule->emulators.empty() ? nullptr : emulators::find(rule->emulators.front());
        // The emulator: automatically the first one found, or one chosen.
        if (!profile || !profile->manual_only) {
            Row choice{Id::emulation_emulator, Control::choice, tr(L"Emulator"),
                       tr(L"Automatycznie: pierwszy znaleziony z listy. Inny wybór dotyczy wszystkich gier tego systemu.")};
            choice.key = key;
            choice.choices.push_back({found && chosen.empty() ? tr(L"Automatycznie") + (L" · " + emulatorName(found->id)) : std::wstring(tr(L"Automatycznie"))});
            for (const auto& id : rule->emulators) {
                const auto* entry = setup.found(id);
                Choice option{emulatorName(id) + L" · " + (entry && entry->found() ? tr(L"znaleziony") : tr(L"brak"))};
                option.key = id;
                choice.choices.push_back(std::move(option));
            }
            choice.choice = 0;
            for (size_t i = 0; i < rule->emulators.size(); ++i) if (rule->emulators[i] == chosen) choice.choice = static_cast<int>(i) + 1;
            choice.value = choice.choice ? emulatorName(chosen) : choice.choices.front().label;
            add(std::move(choice));
        }
        const auto id = !chosen.empty() ? chosen : found ? found->id : rule->emulators.empty() ? std::wstring{} : rule->emulators.front();
        const auto* info = emulators::find(id);
        const auto* entry = setup.found(id);
        if (!info) return;
        // How and where it was found.
        {
            std::wstring where;
            if (entry && entry->found()) {
                where = entry->program.wstring();
                if (!entry->prefix.empty()) where += L" " + entry->prefix;
                if (entry->via == emulators::Via::emudeck_script && !entry->target.empty()) where += L" → " + entry->target.wstring();
                // RetroArch without the system's core (its title says so): its games would not start.
                if (found == entry && emulation_scan::coreMissing(*rule, setup))
                    where = std::wstring(tr(L"Pobierz go w RetroArch (Główne menu → Aktualizacje online → Pobieranie rdzeni).")) + L" " +
                            emulation_scan::core(*rule, setup).wstring();
            } else where = info->manual_only ? tr(L"Wskaż własny program i jego parametry uruchamiania. Ten system jest konfigurowany ręcznie.") :
                                              tr(L"Nie znaleziono programu. Wskaż go niżej albo zainstaluj emulator (np. przez EmuDecka).");
            Row detected{Id::emulation_detected, Control::action,
                          (info->manual_only ? std::wstring(tr(L"Program własny")) : std::wstring(info->name)) + L" · " +
                              (entry ? emulationFoundText(*rule, *entry) : std::wstring(tr(L"Nie znaleziono"))),
                         std::move(where), entry && entry->found() && entry->via != emulators::Via::flatpak ? tr(L"Otwórz folder") : std::wstring{}};
            detected.key = key;
            detected.lines = 3;
            add(std::move(detected));
        }
        const auto id_key = text_encoding::utf8(id);
        const auto own = settings_.emulator_overrides.find(id_key);
        const bool manual = own != settings_.emulator_overrides.end() && !own->second.program.empty();
        section(tr(L"Ręcznie"));
        {
            Row program{Id::emulation_program, Control::action, tr(L"Program"),
                         manual ? own->second.program.wstring() :
                         info->manual_only ? tr(L"Wskaż własny program i jego parametry uruchamiania. Ten system jest konfigurowany ręcznie.") :
                             tr(L"Wskaż plik emulatora (.exe), jeśli jest gdzie indziej. Dotyczy każdego systemu z ") + std::wstring(info->name) + L".",
                        tr(L"Wybierz…")};
            program.key = key;
            add(std::move(program));
        }
        if (manual) {
            Row reset{Id::emulation_program_reset, Control::action, info->manual_only ? tr(L"Usuń wskazany program") : tr(L"Przywróć wykrywanie"),
                       info->manual_only ? tr(L"Wskaż własny program i jego parametry uruchamiania. Ten system jest konfigurowany ręcznie.") :
                           tr(L"Program znajdowany automatycznie (EmuDeck, ES-DE, rejestr, typowe foldery, PATH)."), tr(L"Przywróć")};
            reset.key = key;
            add(std::move(reset));
        }
        const bool script = entry && entry->via == emulators::Via::emudeck_script;
        const bool core = id == L"retroarch" && !rule->core.empty();
        const auto defaults = emulators::defaultArguments(*info, script, core);
        {
            const auto typed = own == settings_.emulator_overrides.end() ? std::wstring{} : own->second.arguments;
            std::wstring help = tr(L"Domyślnie: ") + (defaults.empty() ? std::wstring(tr(L"bez parametrów")) : defaults) + L". " +
                                tr(L"%ROM% = plik gry (bez niego na końcu).");
            if (core) help += L" " + std::wstring(tr(L"%CORE% = rdzeń RetroArch."));
            Row arguments{Id::emulation_arguments, Control::text, tr(L"Parametry uruchamiania"), std::move(help),
                          typed.empty() ? std::wstring(tr(L"Domyślne")) : typed};
            arguments.key = key;
            arguments.lines = 3;
            add(std::move(arguments));
        }
        {
            // The command a game of this system starts with, for an example ROM.
            const auto play = emulation_scan::action(*rule, setup, std::filesystem::path(L"<ROM>"));
            Row preview{Id::emulation_preview, Control::action, tr(L"Polecenie"),
                        play.target.empty() ? std::wstring(tr(L"Brak: emulator nie jest znaleziony.")) : play.target + L" " + play.arguments};
            preview.key = key;
            preview.lines = 4;
            preview.locked = true; // Information only: no button.
            add(std::move(preview));
        }
        return;
    }
    // The page: the console row, where emulated games come from, then the systems.
    section(tr(L"Ekran główny"));
    add({Id::emulation, Control::toggle, tr(L"Rząd konsol na ekranie głównym"),
        tr(L"Osobny rząd konsol z grami z emulatorów, zamiast kolekcji Steam ROM Manager."), {}, settings_.show_emulation});
    section(tr(L"Gry emulowane"));
    {
        static constexpr const wchar_t* sources[] = {L"Folder ROM-ów (własne parsery)", L"Skróty Steam (Steam ROM Manager)"};
        Row source{Id::emulation_source, Control::choice, tr(L"Gry emulowane"),
                   tr(L"Folder ROM-ów: gry prosto z dysku (EmuDeck, ES-DE), bez skrótów i kolekcji Steam ROM Manager.")};
        for (const auto* name : sources) source.choices.push_back({tr(name)});
        source.choice = settings_.emulation_source == AppSettings::EmulationSource::steam ? 1 : 0;
        source.value = source.choices[source.choice].label;
        add(std::move(source));
    }
    if (settings_.emulation_source == AppSettings::EmulationSource::roms) {
        add({Id::emulation_folder, Control::action, tr(L"Folder ROM-ów"), emulationFolderSummary(), tr(L"Zmień")});
        if (!settings_.emulation_roms.empty())
            add({Id::emulation_folder_reset, Control::action, tr(L"Wykryj folder ROM-ów"),
                tr(L"Ten, który wskazuje ES-DE lub EmuDeck."), tr(L"Przywróć")});
    }
    add({Id::emulation_names, Control::toggle, tr(L"Nazwy gier z ES-DE / ScreenScraper"),
        tr(L"Gry z emulatorów pod nazwą dopasowaną do pliku ROM zamiast nazwy skrótu Steam."), {}, settings_.emulation_names});
    add({Id::emulation_rom_art, Control::toggle, tr(L"Grafiki emulacji dopasowane po ROM-ie"),
        tr(L"Okładki i logo z ES-DE i ScreenScraper przed SteamGridDB (szukającym po tytule)."), {}, settings_.emulation_rom_art});
    if (!emulation_setup_) {
        section(tr(L"Systemy"));
        add({Id::emulation_preview, Control::action, tr(L"Szukanie emulatorów…"), tr(L"Folder ROM-ów i programy emulatorów są sprawdzane w tle.")});
        return;
    }
    // Systems with games first (from the ROM folder and added by hand), the others folded below.
    const auto& setup = *emulation_setup_;
    std::map<std::wstring, size_t> games = rom_system_counts_;
    if (!romLibraryActive()) games.clear();
    for (const auto& entry : custom_games_) {
        if (entry.platform.empty()) continue;
        if (const auto* rule = setup.forFile(entry.platform, entry.path)) ++games[rule->system];
    }
    std::vector<const emulation_scan::Rule*> used, other;
    std::set<std::wstring> seen;
    for (const auto& rule : setup.rules) {
        if (!seen.insert(rule.system).second) continue;
        (games.contains(rule.system) ? used : other).push_back(&rule);
    }
    const auto systemRow = [&](const emulation_scan::Rule& rule, int indent) {
        const auto* found = setup.emulatorFor(rule);
        const auto chosen = setup.chosen(rule);
        std::wstring description;
        if (const auto count = games.find(rule.system); count != games.end()) description = tr(L"Gry: ") + std::to_wstring(count->second) + L" · ";
        if (found) description += emulationFoundText(rule, *found);
        else description += !chosen.empty() ? emulatorName(chosen) + L" · " + tr(L"nie znaleziono") : std::wstring(tr(L"Nie znaleziono emulatora"));
        Row row{Id::emulation_system, Control::page, emulationSystemTitle(rule.system), std::move(description),
                !found ? std::wstring(tr(L"Brak emulatora")) : emulation_scan::coreMissing(rule, setup) ? std::wstring(tr(L"Brak rdzenia")) : emulatorName(found->id)};
        row.page = Page::emulation_system;
        row.key = text_encoding::utf8(rule.system);
        row.indent = indent;
        add(std::move(row));
    };
    section(tr(L"Systemy"));
    for (const auto* rule : used) systemRow(*rule, 0);
    if (other.empty()) return;
    Row fold{Id::emulation_more, Control::fold, used.empty() ? tr(L"Wszystkie systemy") : tr(L"Pozostałe systemy"),
             tr(L"Systemy bez gier: ich emulatory i parametry."), std::to_wstring(other.size()), emulation_all_systems_ || used.empty()};
    const bool open = fold.checked;
    add(std::move(fold));
    if (open) for (const auto* rule : other) systemRow(*rule, 1);
}

// The choices of the Emulacja page's rows; false: not one of them.
bool Launcher::emulationSettingsChoose(const Row& row) {
    if (row.id == Id::emulation_source) {
        settings_.emulation_source = settings_state_.choice == 1 ? AppSettings::EmulationSource::steam : AppSettings::EmulationSource::roms;
        settings_state_.editing = false;
        settingsChanged(true);
        startRomScan();
        return true;
    }
    if (row.id != Id::emulation_emulator) return false;
    settings_state_.editing = false;
    const int index = std::clamp(settings_state_.choice, 0, static_cast<int>(row.choices.size()) - 1);
    if (index == 0 || row.choices[static_cast<size_t>(index)].key.empty()) settings_.system_emulator.erase(row.key);
    else settings_.system_emulator[row.key] = text_encoding::utf8(row.choices[static_cast<size_t>(index)].key);
    settingsChanged();
    startRomScan(); // Every game of the system starts with it now.
    return true;
}

// The action rows of the Emulacja page and its systems; false: not one of them.
bool Launcher::emulationSettingsActivate(const Row& row) {
    if (row.id == Id::emulation_system_rescan) {
        const auto* rule = emulationRule(row.key);
        if (!rule) { navigationBoundary(); return true; }
        rom_rescan_system_ = rule->system;
        audio_.context();
        startRomScan();
        buildSettingsRows(); // Szukanie gier… until the scan ends.
        return true;
    }
    if (row.id == Id::emulation_system_folder_reset) {
        settings_.system_roms.erase(row.key);
        settings_state_.selected[settings_state_.category] = std::max(0, settings_state_.row() - 1);
        settingsChanged(); audio_.context();
        startRomScan();
        return true;
    }
    if (row.id == Id::emulation_system_folder) {
        const auto* rule = emulationRule(row.key);
        if (!rule) { navigationBoundary(); return true; }
        const auto apply = [this, key = row.key](const std::filesystem::path& picked) {
            if (!settingsOpen() || picked.empty() || !picked.is_absolute()) return;
            settings_.system_roms[key] = picked;
            settingsChanged(); audio_.context();
            startRomScan();
        };
        file_browser::Request request;
        request.mode = file_browser::Mode::folder;
        request.title = std::wstring(tr(L"Wybierz folder ROM-ów")) + L" · " + emulationSystemTitle(rule->system);
        request.initial = rule->folder;
        if (pick_path_) {
            const auto picked = pick_path_(hwnd_, true, request.initial);
            mouse_transition_until_ = GetTickCount64() + GetDoubleClickTime();
            apply(picked);
            return true;
        }
        request.native = [this, initial = request.initial, title = request.title] { return pickPath(hwnd_, true, title.c_str(), {}, {}, false, initial); };
        openFileBrowser(std::move(request), apply);
        return true;
    }
    if (row.id == Id::emulation_folder_reset) {
        settings_.emulation_roms.clear();
        settingsChanged(); audio_.context();
        settings_state_.selected[settings_state_.category] = std::max(0, settings_state_.row() - 1); // Its row is gone.
        startRomScan();
        return true;
    }
    if (row.id == Id::emulation_folder) {
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
    if (row.id == Id::emulation_system) { openSettingsPage(Page::emulation_system, row.key); return true; }
    if (row.id == Id::emulation_more) {
        emulation_all_systems_ = !emulation_all_systems_;
        buildSettingsRows(); audio_.context(); InvalidateRect(hwnd_, nullptr, FALSE);
        return true;
    }
    if (row.id == Id::emulation_preview) { navigationBoundary(); return true; }
    // The rows below stand for the system's emulator (the chosen one, else the one found, else its first).
    const auto* rule = emulationRule(row.key);
    if (!rule || !emulation_setup_) {
        if (row.id == Id::emulation_detected || row.id == Id::emulation_program || row.id == Id::emulation_program_reset || row.id == Id::emulation_arguments) {
            navigationBoundary(); return true;
        }
        return false;
    }
    const auto chosen = emulation_setup_->chosen(*rule);
    const auto* found = emulation_setup_->emulatorFor(*rule);
    const auto id = !chosen.empty() ? chosen : found ? found->id : rule->emulators.empty() ? std::wstring{} : rule->emulators.front();
    const auto key = text_encoding::utf8(id);
    if (id.empty()) return false;
    const auto changed = [this] {
        // Overrides with nothing left go; every game is started the new way after the scan.
        std::erase_if(settings_.emulator_overrides, [](const auto& entry) { return entry.second.program.empty() && entry.second.arguments.empty(); });
        settingsChanged(); audio_.context();
        startRomScan();
    };
    if (row.id == Id::emulation_detected) {
        const auto* entry = emulation_setup_->found(id);
        if (!entry || !entry->found() || entry->via == emulators::Via::flatpak) { navigationBoundary(); return true; }
        const auto folder = (entry->target.empty() ? entry->program : entry->target).parent_path();
        if (execute_(hwnd_, GameAction{folder.wstring(), {}, {}, ActionKind::open_client}) <= 32)
            showNotification(NotificationKind::error, tr(L"Nie można otworzyć folderu"), folder.wstring());
        return true;
    }
    if (row.id == Id::emulation_program_reset) {
        if (const auto own = settings_.emulator_overrides.find(key); own != settings_.emulator_overrides.end()) own->second.program.clear();
        settings_state_.selected[settings_state_.category] = std::max(0, settings_state_.row() - 1); // Its row is gone.
        changed();
        return true;
    }
    if (row.id == Id::emulation_program) {
        const auto apply = [this, key, changed](const std::filesystem::path& picked) {
            if (!settingsOpen() || picked.empty() || !picked.is_absolute()) { InvalidateRect(hwnd_, nullptr, FALSE); return; }
            settings_.emulator_overrides[key].program = picked;
            changed();
        };
        const auto* entry = emulation_setup_->found(id);
        std::filesystem::path initial;
        if (const auto own = settings_.emulator_overrides.find(key); own != settings_.emulator_overrides.end() && !own->second.program.empty()) initial = own->second.program;
        else if (entry && entry->found()) initial = entry->target.empty() ? entry->program : entry->target;
        if (pick_path_) { // Tests: an injected picker answers at once.
            const auto picked = pick_path_(hwnd_, false, initial);
            mouse_transition_until_ = GetTickCount64() + GetDoubleClickTime();
            apply(picked);
            return true;
        }
        file_browser::Request request;
        request.title = tr(L"Wskaż program emulatora");
        if (emulation_setup_->environment.windows) {
            request.extensions = {L".exe", L".bat", L".cmd", L".ps1"};
            request.filter_label = tr(L"Programy (.exe)");
        }
        request.all_files_option = true;
        request.initial = initial;
        request.native = [this, initial, extensions = request.extensions, label = request.filter_label] {
            return pickPath(hwnd_, false, tr(L"Wskaż program emulatora"), extensions, label, true, initial);
        };
        openFileBrowser(std::move(request), apply);
        return true;
    }
    if (row.id == Id::emulation_arguments) {
        const auto own = settings_.emulator_overrides.find(key);
        settingsTextRow(own == settings_.emulator_overrides.end() ? std::wstring{} : own->second.arguments, [this, key](const std::wstring& value) {
            if (value.size() > 2000) return false;
            settings_.emulator_overrides[key].arguments = value;
            std::erase_if(settings_.emulator_overrides, [](const auto& entry) { return entry.second.program.empty() && entry.second.arguments.empty(); });
            startRomScan();
            return true;
        }, false);
        return true;
    }
    return false;
}
