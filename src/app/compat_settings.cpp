// Linux: Ustawienia → Biblioteka → Proton i Wine. The compatibility tools (src/library/compat_tools.hpp)
// with their downloads, the prefix manager (src/library/launch_options_store.hpp: named prefixes, each
// game's, their tools and sizes, the prefix tools of src/platform/wine_runner.hpp) and the Wine/Proton
// options of one game or of every game (src/model/launch_options_catalog.hpp) as an accordion of
// sections. Blocking work (discovery, GitHub's lists, downloads, sizes, wineserver -k, deleting a
// prefix) runs on workers and comes back as RemoteMessages. On Windows only the stubs remain.
// Here: the game editor's launch options (Steam-style text), the page's names and launches.
// Workers and installers: compat_workers.cpp; rows: compat_rows.cpp; actions: compat_activate.cpp;
// shared helpers: compat_internal.hpp.
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
#endif

using namespace settings_ui;

#ifdef _WIN32
bool Launcher::compatGame(const Game&) const { return false; }
void Launcher::openGameCompatSettings(const Game&) {}
void Launcher::compatEditorFill(custom_game_ui::State&, const Game&) const {}
bool Launcher::compatEditorApply(const custom_game_ui::State&, bool, std::wstring&, custom_game_ui::Item&) { return true; }
#else
using namespace compat_detail;

namespace {
bool windowsProgram(const std::wstring& target) {
    if (target.empty() || target.find(L"://") != std::wstring::npos) return false;
    auto extension = std::filesystem::path(target).extension().wstring();
    for (auto& ch : extension) ch = static_cast<wchar_t>(std::towlower(ch));
    return extension == L".exe" || extension == L".msi" || extension == L".bat" || extension == L".cmd" || extension == L".com" || extension == L".lnk";
}
} // namespace

bool Launcher::compatGame(const Game& game) const {
    if (!isGame(game) || game.kind == GameKind::nonsteam || game.source == GameSource::steam || game.source == GameSource::multimc) return false;
    const auto& target = game.play_action.target;
    if (localFileGame(game.source)) return fileNotChosen(game) || windowsProgram(target) || windowsProgram(game.executable.wstring());
    // A store's game: its Windows build, unless the action starts a native program.
    if (target.empty() || target.find(L"://") != std::wstring::npos) return true;
    return windowsProgram(target) || !std::filesystem::path(target).has_extension();
}

namespace {
// A word of Steam-style launch options, quoted only when the shell would split or expand it.
std::string launchWord(const std::string& text) {
    const bool plain = !text.empty() && std::all_of(text.begin(), text.end(), [](char ch) {
        return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || std::string_view("_-.,:/@%+=").find(ch) != std::string_view::npos;
    });
    return plain ? text : wine_runner::shellQuote(text);
}
// A game argument as process_spawn::splitArguments reads it (Windows rules), quoted only when needed.
std::string argumentWord(const std::string& text) {
    return !text.empty() && text.find_first_of(" \t\r\n\"") == std::string::npos ? text : command_line::quoteArgument(text);
}
// Where the %command% word starts outside the shell's quotes (it may also appear inside a quoted
// variable value), else npos.
size_t commandWordAt(const std::string& text) {
    constexpr std::string_view word = "%command%";
    char quote = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        const char ch = text[i];
        if (quote) {
            if (ch == quote) quote = 0;
            else if (quote == '"' && ch == '\\') ++i;
            continue;
        }
        if (ch == '\\') { ++i; continue; }
        if (ch == '\'' || ch == '"') { quote = ch; continue; }
        const size_t end = i + word.size();
        if (end > text.size()) break;
        const bool starts = i == 0 || text[i - 1] == ' ' || text[i - 1] == '\t';
        const bool ends = end == text.size() || text[end] == ' ' || text[end] == '\t';
        if (starts && ends && text.compare(i, word.size(), word) == 0) return i;
    }
    return std::string::npos;
}
// The options as Steam's launch options: VAR=value, the wrappers, %command%, the game's arguments.
// (Switches that set no variable, gamescope among them, stay on the game's Proton / Wine page.)
std::string launchText(const LaunchOptions& options) {
    std::string text;
    const auto add = [&](const std::string& word) { if (!word.empty()) text += (text.empty() ? "" : " ") + word; };
    for (const auto& variable : options.environment) add(variable.name + "=" + (variable.value.empty() ? std::string("''") : launchWord(variable.value)));
    if (options.game_performance) add("game-performance");
    if (options.dlss_swapper) add("dlss-swapper");
    if (options.gamemode) add("gamemoderun");
    if (options.mangohud) add("mangohud");
    add(options.wrapper);
    add("%command%");
    add(options.arguments);
    return text;
}
// Back into the options: VAR=value words before %command% are variables, other words wrappers
// (gamemoderun, mangohud, game-performance and dlss-swapper are their switches); after it the game's
// arguments as typed. Without %command% (Steam: arguments only) leading VAR=value words are still
// variables. The error, else nullopt.
std::optional<std::wstring> applyLaunchText(LaunchOptions& options, const std::string& text) {
    const auto words = wine_runner::shellWords(text);
    const auto command = std::find(words.begin(), words.end(), std::string("%command%"));
    const bool has_command = command != words.end();
    std::vector<LaunchOptions::Variable> environment;
    std::vector<std::string> wrappers;
    bool gamemode = false, mangohud = false, performance = false, swapper = false;
    auto it = words.begin();
    for (; it != command; ++it) {
        const auto equals = it->find('=');
        if (equals != std::string::npos && equals > 0 && catalog::variableName(std::string_view(*it).substr(0, equals))) {
            const auto name = it->substr(0, equals);
            std::erase_if(environment, [&](const auto& variable) { return variable.name == name; });
            environment.push_back({name, it->substr(equals + 1)});
            continue;
        }
        if (!has_command) break; // The game's arguments from here, as in Steam.
        if (*it == "gamemoderun") gamemode = true;
        else if (*it == "mangohud") mangohud = true;
        else if (*it == "game-performance") performance = true;
        else if (*it == "dlss-swapper") swapper = true;
        else wrappers.push_back(*it);
    }
    std::string arguments;
    if (has_command) {
        const auto at = commandWordAt(text);
        if (at != std::string::npos) arguments = text.substr(at + 9);
    } else for (; it != words.end(); ++it) arguments += (arguments.empty() ? "" : " ") + argumentWord(*it);
    while (!arguments.empty() && (arguments.front() == ' ' || arguments.front() == '\t')) arguments.erase(arguments.begin());
    while (!arguments.empty() && (arguments.back() == ' ' || arguments.back() == '\t')) arguments.pop_back();
    if (environment.size() > 64) return std::wstring(tr(L"Najwyżej 64 zmienne środowiska."));
    std::string wrapper;
    for (const auto& word : wrappers) wrapper += (wrapper.empty() ? "" : " ") + launchWord(word);
    auto next = options;
    next.environment = std::move(environment);
    next.gamemode = gamemode;
    next.mangohud = mangohud;
    next.game_performance = performance;
    next.dlss_swapper = swapper;
    if (!catalog::setValue(next, "wrapper", wrapper)) return std::wstring(tr(L"Nieprawidłowe polecenie przed %command%."));
    if (!catalog::setValue(next, "arguments", arguments)) return std::wstring(tr(L"Nieprawidłowe argumenty po %command%."));
    options = std::move(next);
    return std::nullopt;
}
}

void Launcher::compatEditorFill(custom_game_ui::State& ui, const Game& game) const {
    const auto key = gameKey(game);
    if (key.empty() || !compatGame(game)) return;
    const auto options = store::gameOptions(key).value_or(gameDefaults());
    ui.compat = true;
    ui.game_key = key;
    ui.fields[custom_game_ui::dll_field] = ui.dll_opened = wide(options.dll_overrides);
    ui.fields[custom_game_ui::wine_field] = ui.wine_opened = wide(launchText(options));
}

bool Launcher::compatEditorApply(const custom_game_ui::State& ui, bool write, std::wstring& error, custom_game_ui::Item& field) {
    if (!ui.compat || ui.game_key.empty()) return true;
    const auto dll = ui.fields[custom_game_ui::dll_field], launch = ui.fields[custom_game_ui::wine_field];
    const bool dll_changed = dll != ui.dll_opened, launch_changed = launch != ui.wine_opened;
    if (!dll_changed && !launch_changed) return true;
    const auto change = [&](LaunchOptions& options) {
        if (dll_changed && !catalog::setValue(options, "dll_overrides", narrow(dll))) {
            error = tr(L"Nieprawidłowe WINEDLLOVERRIDES (np. dinput8=n,b;xinput1_3=n).");
            field = custom_game_ui::Item::dll;
            return false;
        }
        if (launch_changed)
            if (const auto problem = applyLaunchText(options, narrow(launch))) {
                error = *problem;
                field = custom_game_ui::Item::wine;
                return false;
            }
        return true;
    };
    if (!write) {
        auto copy = store::gameOptions(ui.game_key).value_or(gameDefaults());
        return change(copy);
    }
    return compatSaveOptions(ui.game_key, change);
}

void Launcher::openGameCompatSettings(const Game& game) {
    if (steamGameConfigurable(game)) { openSteamGameSettings(game); return; } // Steam keeps its games' own (steam_game.cpp).
    const auto key = gameKey(game);
    if (!compatGame(game) || key.empty()) return;
    dismissMenu();
    compat_.game = game;
    // What the game sets itself first, when it sets anything.
    compat_.expanded = store::gameOverrides(key).empty() ? std::set<std::string>{"runner"} : std::set<std::string>{"own"};
    compat_.query.clear();
    compat_.confirm.clear();
    if (!settingsOpen()) openSettings();
    if (!settingsOpen()) return;
    settings_state_.selectCategory(static_cast<int>(settings_ui::Category::compat));
    settings_state_.open(Page::launch_options, key, 0, std::nullopt);
    settings_page_exits_ = true;
    clearSecretInput();
    settings_motion_.valid = false;
    if (!compat_.tools_loaded) compatLoadTools();
    buildSettingsRows(); settingsPreviewFocus(); audio_.context();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

// Ustawienia → Zgodność, its own list or one of its pages.
bool Launcher::compatSettingsPage() const { return settings_state_.category == static_cast<int>(settings_ui::Category::compat); }

std::wstring Launcher::compatToolName(const std::string& id) const {
    if (!chosen(id)) {
        if (const auto tool = compat_tools::automatic(compat_.tools)) return tool->name + L" (" + tr(L"automatycznie") + L")";
        return compat_.tools_loaded ? std::wstring(tr(L"Automatycznie (brak narzędzia)")) : std::wstring(tr(L"Automatycznie"));
    }
    for (const auto& tool : compat_.tools) if (tool.id == id) return tool.name;
    return compat_.tools_loaded ? wide(id) + tr(L" (nie ma go już)") : wide(id);
}

std::wstring Launcher::compatPrefixName(const store::PrefixEntry& entry) const {
    if (!entry.info.name.empty()) return entry.info.name;
    if (entry.shared) return tr(L"Wspólny prefiks");
    if (!entry.games.empty()) {
        std::wstring title;
        const auto& key = entry.games.front();
        const auto found = std::find_if(public_library_.begin(), public_library_.end(), [&](const Game& game) { return gameKey(game) == key; });
        title = found != public_library_.end() ? found->name : wide(key);
        if (entry.games.size() > 1) title += L" +" + std::to_wstring(entry.games.size() - 1);
        return title;
    }
    return entry.path.filename().wstring();
}

CompatLaunch Launcher::compatPrefixLaunch(const std::filesystem::path& prefix) const {
    CompatLaunch launch;
    launch.prefix = prefix;
    const auto normal = prefix.lexically_normal();
    const auto found = std::find_if(compat_.prefixes.begin(), compat_.prefixes.end(), [&](const auto& entry) { return entry.path == normal; });
    // The first game in it: its options and identity (protontricks-like tools use its Steam app ID).
    const std::string key = found != compat_.prefixes.end() && !found->games.empty() ? found->games.front() : std::string{};
    launch.game_key = key;
    launch.title = found != compat_.prefixes.end() ? compatPrefixName(*found) : prefix.filename().wstring();
    launch.options = key.empty() ? store::defaults() : store::effectiveOptions(key);
    launch.options.prefix = prefix;
    launch.options.compat_tool = store::effectiveTool(key, prefix);
    return launch;
}

bool Launcher::compatSaveOptions(const std::string& key, const std::function<bool(LaunchOptions&)>& change) {
    auto options = key.empty() ? store::defaults() : store::gameOptions(key).value_or(gameDefaults());
    if (!change(options)) return false;
    const bool saved = key.empty() ? store::setDefaults(options) : store::setGameOptions(key, options);
    if (!saved) showNotification(NotificationKind::error, tr(L"Nie udało się zapisać opcji uruchamiania"), store::file().wstring());
    return saved;
}
#endif
