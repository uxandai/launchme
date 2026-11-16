// Linux: what the Proton i Wine rows do when chosen (tools, prefixes, options and their resets).
#include "launcher.hpp"

#ifndef _WIN32
#include "compat_internal.hpp"
#include "../artwork/cover_cache.hpp"
#include "../library/shortcuts.hpp"
#include "../library/steam_accounts.hpp"
#include "../library/xdg_paths.hpp"
#include "../model/launch_options_catalog.hpp"
#include "../platform/command_line.hpp"
#include "../platform/process_spawn.hpp"
#include "../platform/system.hpp"
#include "../platform/text_encoding.hpp"
#include "../platform/unicode_text.hpp"
#include "../platform/updater.hpp"
#include "../platform/wine_runner.hpp"

#include <functional>

#include <sys/wait.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <ctime>
#include <cwchar>
#include <cwctype>
#include <fstream>

using namespace settings_ui;
using namespace compat_detail;

// A badge's reset (Row::reset): one override ("mangohud", "env:NAME"), or a section's ("section:<fold>").
bool Launcher::compatResetOption(const Row& row) {
    const auto* frame = settings_state_.top();
    const std::string key = frame && settings_state_.at(Page::launch_options) ? frame->key : std::string{};
    if (key.empty() || row.reset.empty()) return false;
    if (row.reset.starts_with("section:")) {
        const auto id = std::string_view(row.reset).substr(8);
        const auto fold = std::find_if(std::begin(folds), std::end(folds), [&](const Fold& item) { return id == item.id; });
        if (fold == std::end(folds)) return false;
        const auto overrides = store::gameOverrides(key);
        for (const auto& option : catalog::options()) {
            const bool in = fold->section ? option.section == *fold->section && std::string_view(option.id) != "dll_overrides" :
                                            std::string_view(option.id) == "dll_overrides";
            const auto override_id = option.variable ? "env:" + std::string(option.variable) : std::string(option.id);
            if (in && overrides.contains(override_id)) store::resetGameOption(key, override_id);
        }
        if (fold->section == catalog::Section::game)
            for (const auto& override_id : overrides)
                if (override_id.starts_with("env:") && !catalog::forVariable(std::string_view(override_id).substr(4))) store::resetGameOption(key, override_id);
    } else store::resetGameOption(key, row.reset);
    if (store::gameOverrides(key).empty()) compat_.expanded.erase("own");
    buildSettingsRows(); audio_.context();
    InvalidateRect(hwnd_, nullptr, FALSE);
    return true;
}

// ---- Activation ------------------------------------------------------------------------------------

bool Launcher::compatSettingsActivate(const Row& row) {
    if (!compatSettingsPage()) return false;
    if (settings_state_.at(Page::steam_game)) return steamGameActivate(row);
    const auto* frame = settings_state_.top();
    const std::string page_key = frame ? frame->key : std::string{};
    // A picker: the first press opens the list, the second applies the focused entry.
    const auto pick = [&](const std::function<void(const Choice&)>& apply) {
        if (row.locked) { navigationBoundary(); return true; }
        if (!settings_state_.editing) {
            settings_state_.choice = std::clamp(row.choice, 0, std::max(0, static_cast<int>(row.choices.size()) - 1));
            settings_state_.editing = true;
            audio_.context(); InvalidateRect(hwnd_, nullptr, FALSE);
            return true;
        }
        settings_state_.editing = false;
        const int picked = settings_state_.choice;
        if (picked >= 0 && picked < static_cast<int>(row.choices.size())) apply(row.choices[static_cast<size_t>(picked)]);
        buildSettingsRows(); audio_.context(); InvalidateRect(hwnd_, nullptr, FALSE);
        return true;
    };
    const auto openFolder = [&](const std::filesystem::path& folder) {
        if (execute_(hwnd_, GameAction{folder.wstring(), {}, {}, ActionKind::open_client}) <= 32)
            showNotification(NotificationKind::error, tr(L"Nie można otworzyć folderu"), folder.wstring());
    };
    // Any other row cancels a pending confirmation.
    const auto confirmed = [&](const std::string& what) {
        if (compat_.confirm == what) { compat_.confirm.clear(); return true; }
        compat_.confirm = what;
        buildSettingsRows(); audio_.context();
        return false;
    };
    if (row.id != Id::compat_tool && row.id != Id::prefix_delete && row.id != Id::launch_scope && !compat_.confirm.empty()) compat_.confirm.clear();
    switch (row.id) {
    // Proton i Wine.
    case Id::compat_install_game:
        if (row.locked) { navigationBoundary(); return true; }
        compatInstallGame();
        return true;
    case Id::launch_protondb: openProtonDb(compat_.game); return true;
    case Id::compat_stores_open: openSettingsPage(Page::compat_stores); return true;
    case Id::compat_store_install:
        if (row.locked) { navigationBoundary(); return true; }
        compatInstallStore(row.key);
        return true;
    case Id::launch_log:
        if (row.locked || row.key.empty()) { navigationBoundary(); return true; }
        if (execute_(hwnd_, GameAction{wide(row.key), {}, {}, ActionKind::open_client}) <= 32)
            showNotification(NotificationKind::error, tr(L"Nie można otworzyć pliku"), wide(row.key));
        return true;
    case Id::launch_steam: case Id::launch_shortcut_desktop: case Id::launch_shortcut_menu: {
        const auto place = row.id == Id::launch_steam ? ShortcutPlace::steam : row.id == Id::launch_shortcut_desktop ? ShortcutPlace::desktop : ShortcutPlace::menu;
        setShortcut(compat_.game, place, !hasShortcut(compat_.game, place));
        buildSettingsRows(); audio_.context();
        return true;
    }
    case Id::compat_default_tool:
        return pick([&](const Choice& choice) { store::setDefaultTool(narrow(choice.key)); });
    case Id::compat_defaults:
        compat_.expanded = {"runner"};
        compat_.query.clear();
        openSettingsPage(Page::launch_options, {});
        return true;
    case Id::compat_prefixes: compatLoadTools(); openSettingsPage(Page::prefixes); return true;
    case Id::compat_prefix_root: {
        file_browser::Request request;
        request.mode = file_browser::Mode::folder;
        request.title = tr(L"Wybierz folder prefiksów");
        request.initial = store::prefixRoot();
        openFileBrowser(std::move(request), [this](const std::filesystem::path& picked) {
            if (picked.empty() || !store::setPrefixRoot(picked)) return;
            compatLoadTools();
            if (settingsOpen()) buildSettingsRows();
        });
        return true;
    }
    case Id::compat_umu:
        openSettingsPage(Page::compat_source, "umu-launcher");
        return true;
    case Id::compat_tool: {
        const auto tool = std::find_if(compat_.tools.begin(), compat_.tools.end(), [&](const auto& item) { return item.id == row.key; });
        if (tool == compat_.tools.end()) return true;
        return pick([&](const Choice& choice) {
            if (choice.key == L"default") store::setDefaultTool(tool->id);
            else if (choice.key == L"folder") openFolder(tool->path);
            else if (choice.key == L"remove") {
                if (!confirmed("tool:" + tool->id)) return;
                const auto users = store::gamesUsingTool(tool->id);
                if (!compat_tools::remove(*tool)) showNotification(NotificationKind::error, tr(L"Nie udało się usunąć"), tool->name);
                else {
                    showNotification(NotificationKind::download, tool->name, users.empty() ? std::wstring(tr(L"Usunięto")) :
                                     tr(L"Usunięto. Gry, które jej używały, dostaną wersję automatyczną: ") + std::to_wstring(users.size()));
                    compatLoadTools();
                }
            }
        });
    }
    case Id::compat_source:
        compatLoadReleases(row.key, false);
        openSettingsPage(Page::compat_source, row.key);
        return true;
    case Id::compat_auto_update:
        settings_.compat_auto_update = !settings_.compat_auto_update;
        settingsChanged(); buildSettingsRows(); audio_.context();
        if (settings_.compat_auto_update) compatAutoUpdate(true);
        return true;
    case Id::compat_steam_target:
        compat_.steam_target = !compat_.steam_target;
        buildSettingsRows(); audio_.context();
        return true;
    case Id::compat_source_refresh:
        compat_.releases.erase(page_key);
        compatLoadReleases(page_key, true);
        buildSettingsRows(); audio_.context();
        return true;
    case Id::compat_release: {
        if (compat_.install && compat_.install->tag == row.key && compat_.install->source == page_key) {
            compat_.install->cancel->store(true); // The partial download stays for the next attempt.
            return true;
        }
        if (row.locked) { navigationBoundary(); return true; }
        const auto list = compat_.releases.find(page_key);
        if (list == compat_.releases.end()) return true;
        const auto release = std::find_if(list->second.releases.begin(), list->second.releases.end(), [&](const auto& item) { return item.tag == row.key; });
        if (release != list->second.releases.end()) { compatInstall(*release); buildSettingsRows(); audio_.context(); }
        return true;
    }
    case Id::compat_status: case Id::prefix_games: navigationBoundary(); return true;
    // Prefiksy.
    case Id::prefix_new:
        settingsTextRow({}, [this](const std::wstring& value) {
            std::wstring name = value;
            name.erase(0, name.find_first_not_of(L" \t"));
            while (!name.empty() && (name.back() == L' ' || name.back() == L'\t')) name.pop_back();
            if (name.empty()) return false;
            const auto created = store::newPrefix(name, "default");
            if (created.empty()) {
                showNotification(NotificationKind::error, tr(L"Nie udało się utworzyć prefiksu"), store::prefixRoot().wstring());
                return false;
            }
            compat_.prefixes = store::prefixes();
            return true;
        });
        if (!settings_state_.editing) buildSettingsRows();
        return true;
    case Id::prefix_entry:
        openSettingsPage(Page::prefix, row.key);
        return true;
    case Id::prefix_name: {
        const auto path = pathOf(row.key);
        auto info = store::prefixInfo(path);
        settingsTextRow(info.name, [this, path, info](const std::wstring& value) mutable {
            if (value.size() > 256) return false;
            info.name = value;
            if (!store::setPrefixInfo(path, info)) return false;
            compat_.prefixes = store::prefixes();
            return true;
        });
        if (!settings_state_.editing) buildSettingsRows();
        return true;
    }
    case Id::prefix_tool: {
        const auto path = pathOf(row.key);
        return pick([&](const Choice& choice) {
            auto info = store::prefixInfo(path);
            info.compat_tool = narrow(choice.key);
            store::setPrefixInfo(path, info);
            compat_.prefixes = store::prefixes();
            if (const auto warning = runnerWarning(compat_.tools, store::effectiveTool({}, path), path); !warning.empty())
                showNotification(NotificationKind::error, path.filename().wstring(), warning);
        });
    }
    case Id::prefix_folder: {
        const auto path = pathOf(row.key);
        std::error_code ec;
        std::filesystem::create_directories(path, ec); // A prefix not made yet opens as an empty folder.
        openFolder(path);
        return true;
    }
    case Id::prefix_size:
        compat_.sizes.erase(pathOf(row.key).lexically_normal());
        compatMeasure(pathOf(row.key));
        buildSettingsRows();
        return true;
    case Id::prefix_create: compatPrefixAction(pathOf(row.key), "create"); return true;
    case Id::prefix_program: {
        const auto path = pathOf(row.key);
        return pick([&](const Choice& choice) { compatPrefixAction(path, narrow(choice.key)); });
    }
    case Id::prefix_winetricks: {
        const auto path = pathOf(row.key);
        return pick([&](const Choice& choice) {
            std::vector<std::string> verbs;
            if (!choice.key.empty()) verbs.push_back(narrow(choice.key));
            compatPrefixAction(path, "winetricks", verbs);
        });
    }
    case Id::prefix_run: {
        const auto path = pathOf(row.key);
        file_browser::Request request;
        request.mode = file_browser::Mode::file;
        request.title = tr(L"Wybierz program do uruchomienia w prefiksie");
        request.extensions = {L".exe", L".msi", L".bat", L".cmd", L".lnk"};
        request.filter_label = tr(L"Programy Windows");
        request.all_files_option = true;
        openFileBrowser(std::move(request), [this, path](const std::filesystem::path& picked) {
            if (!picked.empty()) compatPrefixAction(path, "run", {keyOf(picked)});
        });
        return true;
    }
    case Id::prefix_kill: compatPrefixAction(pathOf(row.key), "kill"); return true;
    case Id::prefix_delete:
        if (confirmed("prefix:" + row.key)) compatPrefixAction(pathOf(row.key), "delete");
        return true;
    // Opcje uruchamiania.
    case Id::launch_fold:
        if (!compat_.expanded.erase(row.key)) compat_.expanded.insert(row.key);
        buildSettingsRows(); audio_.context();
        return true;
    case Id::launch_scope:
        if (!store::gameOptions(page_key)) { compat_.expanded = {"runner"}; compat_.query.clear(); openSettingsPage(Page::launch_options, {}); return true; }
        if (confirmed("reset:" + page_key)) {
            store::resetGameOptions(page_key);
            buildSettingsRows(); audio_.context();
        }
        return true;
    case Id::launch_edit_game: openLaunchEditor(compat_.game); return true;
    case Id::launch_tricks:
        return pick([&](const Choice& choice) {
            const auto action = narrow(choice.key);
            if (action.starts_with("verb:")) compatGameTool(compat_.game, "winetricks", {action.substr(5)});
            else compatGameTool(compat_.game, action);
        });
    case Id::launch_search:
        settingsTextRow(compat_.query, [this](const std::wstring& value) {
            compat_.query = value;
            while (!compat_.query.empty() && compat_.query.back() == L' ') compat_.query.pop_back();
            if (settingsOpen()) buildSettingsRows();
            return true;
        }, false);
        if (!settings_state_.editing) buildSettingsRows();
        return true;
    case Id::launch_search_clear:
        compat_.query.clear();
        buildSettingsRows(); audio_.context();
        return true;
    // An entry of WINEDLLOVERRIDES (Row::key its DLLs): its mode, or out of the list.
    case Id::launch_dll:
        return pick([&](const Choice& choice) {
            compatSaveOptions(page_key, [&](LaunchOptions& options) {
                const auto mode = narrow(choice.key);
                options.dll_overrides = catalog::withDllOverride(options.dll_overrides, row.key,
                    mode == "remove" ? std::nullopt : std::optional<std::string_view>(mode));
                return true;
            });
        });
    case Id::launch_dll_preset:
        return pick([&](const Choice& choice) {
            const auto dlls = narrow(choice.key);
            const auto presets = catalog::dllPresets();
            const auto preset = std::find_if(presets.begin(), presets.end(), [&](const auto& item) { return dlls == item.dlls; });
            if (preset == presets.end()) return;
            compatSaveOptions(page_key, [&](LaunchOptions& options) {
                options.dll_overrides = catalog::withDllOverride(options.dll_overrides, preset->dlls, std::string_view(preset->mode));
                return true;
            });
        });
    case Id::launch_dll_add:
        settingsTextRow({}, [this, page_key](const std::wstring& value) {
            const auto text = narrow(value);
            const auto equals = text.find('=');
            const auto dlls = text.substr(0, equals);
            if (equals == std::string::npos || catalog::dllEntries(dlls + "=x").empty()) {
                showNotification(NotificationKind::error, tr(L"Nieprawidłowy wpis"), tr(L"Wpisz nazwa=tryb, np. dinput8=n,b."));
                return false;
            }
            return compatSaveOptions(page_key, [&](LaunchOptions& options) {
                options.dll_overrides = catalog::withDllOverride(options.dll_overrides, dlls, std::string_view(text).substr(equals + 1));
                return true;
            });
        }, false);
        if (!settings_state_.editing) buildSettingsRows();
        return true;
    // A variable (Row::key its name): a new value; an emptied field takes it away (a game: also the default's).
    case Id::launch_env: {
        const auto name = row.key;
        const auto current = row.value == tr(L"Wyłączona") ? std::wstring{} : row.value;
        settingsTextRow(current, [this, page_key, name](const std::wstring& value) {
            return compatSaveOptions(page_key, [&](LaunchOptions& options) {
                std::erase_if(options.environment, [&](const auto& variable) { return variable.name == name; });
                if (!value.empty()) options.environment.push_back({name, narrow(value)});
                return options.environment.size() <= 64;
            });
        }, false);
        if (!settings_state_.editing) buildSettingsRows();
        return true;
    }
    case Id::launch_env_add:
        settingsTextRow({}, [this, page_key](const std::wstring& value) {
            const auto text = narrow(value);
            const auto equals = text.find('=');
            if (equals == std::string::npos || !catalog::variableName(text.substr(0, equals))) {
                showNotification(NotificationKind::error, tr(L"Nieprawidłowa zmienna"), tr(L"Wpisz NAZWA=wartość, np. DXVK_HUD=fps."));
                return false;
            }
            return compatSaveOptions(page_key, [&](LaunchOptions& options) {
                std::erase_if(options.environment, [&](const auto& variable) { return variable.name == text.substr(0, equals); });
                options.environment.push_back({text.substr(0, equals), text.substr(equals + 1)});
                return options.environment.size() <= 64;
            });
        }, false);
        if (!settings_state_.editing) buildSettingsRows();
        return true;
    case Id::launch_env_preset:
        return pick([&](const Choice& choice) {
            const auto name = narrow(choice.key);
            const auto presets = catalog::variablePresets();
            const auto preset = std::find_if(presets.begin(), presets.end(), [&](const auto& item) { return name == item.name; });
            if (preset == presets.end()) return;
            compatSaveOptions(page_key, [&](LaunchOptions& options) {
                std::erase_if(options.environment, [&](const auto& variable) { return variable.name == preset->name; });
                options.environment.push_back({preset->name, preset->value});
                return true;
            });
        });
    case Id::launch_option: {
        const auto* option = catalog::find(row.key);
        if (!option) return true;
        if (row.key == "prefix") {
            return pick([&](const Choice& choice) {
                if (choice.key == L"browse") {
                    file_browser::Request request;
                    request.mode = file_browser::Mode::folder;
                    request.title = tr(L"Wybierz folder prefiksu");
                    request.initial = store::prefixRoot();
                    openFileBrowser(std::move(request), [this, page_key](const std::filesystem::path& picked) {
                        if (picked.empty()) return;
                        compatSaveOptions(page_key, [&](LaunchOptions& options) { options.prefix = picked.lexically_normal(); return true; });
                        compatLoadTools();
                        if (settingsOpen()) buildSettingsRows();
                    });
                    return;
                }
                compatSaveOptions(page_key, [&](LaunchOptions& options) { options.prefix = std::filesystem::path(choice.key); return true; });
                compatLoadTools();
            });
        }
        if (row.control == Control::choice)
            return pick([&](const Choice& choice) {
                compatSaveOptions(page_key, [&](LaunchOptions& options) { return catalog::setValue(options, option->id, narrow(choice.key)); });
                // A game's version of the other kind than the one that made its prefix: said at once (the row keeps saying it).
                if (row.key == "compat_tool" && !page_key.empty())
                    if (const auto prefix = store::existingPrefix(page_key))
                        if (const auto warning = runnerWarning(compat_.tools, store::effectiveTool(page_key, *prefix), *prefix); !warning.empty())
                            showNotification(NotificationKind::error, compat_.game.name, warning);
            });
        if (row.control == Control::toggle) {
            if (row.locked) { navigationBoundary(); return true; }
            compatSaveOptions(page_key, [&](LaunchOptions& options) { return catalog::setValue(options, option->id, row.checked ? "0" : "1"); });
            buildSettingsRows(); audio_.context();
            return true;
        }
        // A text, number or path: the dialog shows the current value; an emptied field is the default.
        const auto current = catalog::value(page_key.empty() ? store::defaults() : store::gameOptions(page_key).value_or(gameDefaults()), option->id);
        settingsTextRow(wide(current), [this, page_key, option](const std::wstring& value) {
            auto text = narrow(value);
            if (text.empty() && option->type == catalog::Type::number) text = catalog::value(LaunchOptions{}, option->id);
            const bool saved = compatSaveOptions(page_key, [&](LaunchOptions& options) { return catalog::setValue(options, option->id, text); });
            if (!saved) showNotification(NotificationKind::error, tr(L"Nieprawidłowa wartość"), label(option->label));
            return saved;
        }, option->type == catalog::Type::path);
        if (!settings_state_.editing) buildSettingsRows();
        return true;
    }
    default: return false;
    }
}
#endif
