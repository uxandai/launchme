// Linux: the Proton i Wine work off the UI thread: tools and their releases, downloads and the
// umu database, prefixes (provisioning, sizes, wineserver, deletion), Protontricks for any game,
// and a game or store launcher installed from its installer into a prefix.
#include "launcher.hpp"

#ifndef _WIN32
#include "compat_internal.hpp"
#include "../artwork/cover_cache.hpp"
#include "../library/shortcuts.hpp"
#include "../library/steam_accounts.hpp"
#include "../library/xdg_paths.hpp"
#include "../model/launch_options_catalog.hpp"
#include "../platform/atomic_file.hpp"
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

namespace {
// The bytes of every file under a prefix. Links are not followed: a prefix's dosdevices point at /
// and its pfx at itself.
uint64_t folderSize(const std::filesystem::path& root, const std::shared_ptr<std::atomic_bool>& alive) {
    uint64_t total = 0;
    std::error_code ec;
    for (std::filesystem::recursive_directory_iterator it(root, std::filesystem::directory_options::skip_permission_denied, ec), end;
         !ec && it != end && alive->load(); it.increment(ec)) {
        std::error_code type;
        const auto status = it->symlink_status(type);
        if (!type && std::filesystem::is_regular_file(status)) {
            const auto size = it->file_size(type);
            if (!type) total += size;
        }
    }
    return total;
}

const StoreInstaller* findStore(std::string_view id) {
    for (const auto& store : store_installers) if (id == store.id) return &store;
    return nullptr;
}

// A game's name from its installer's ("setup_the_witcher_3_wild_hunt_4.04_(64bit)_(62548).exe" →
// "The Witcher 3 Wild Hunt"): setup/installer words, versions and bracketed parts left out.
std::wstring installerName(const std::filesystem::path& installer) {
    std::wstring stem = installer.stem().wstring();
    std::vector<std::wstring> words;
    std::wstring word;
    const auto flush = [&] {
        std::wstring lower = word;
        for (auto& ch : lower) ch = static_cast<wchar_t>(std::towlower(ch));
        // "SteamSetup", "GameInstaller": the word without its setup part.
        for (const std::wstring_view part : {std::wstring_view(L"installer"), std::wstring_view(L"setup")}) {
            if (lower.size() > part.size() && lower.ends_with(part)) { word.resize(word.size() - part.size()); lower.resize(lower.size() - part.size()); }
            else if (lower.size() > part.size() && lower.starts_with(part)) { word.erase(0, part.size()); lower.erase(0, part.size()); }
        }
        static constexpr std::wstring_view noise_words[] = {L"setup", L"installer", L"install", L"x64", L"x86", L"win64", L"win32", L"gog",
                                                            L"repack", L"64bit", L"32bit"};
        const bool noise = lower.empty() || std::find(std::begin(noise_words), std::end(noise_words), lower) != std::end(noise_words);
        const bool version = !lower.empty() && (lower.front() == L'(' || lower.find(L'.') != std::wstring::npos ||
                                                (lower.front() == L'v' && lower.size() > 1 && std::iswdigit(lower[1])) ||
                                                (lower.size() >= 4 && std::all_of(lower.begin(), lower.end(), [](wchar_t ch) { return std::iswdigit(ch); })));
        if (!noise && !version) {
            if (!word.empty()) word[0] = static_cast<wchar_t>(std::towupper(word[0]));
            words.push_back(word);
        }
        word.clear();
    };
    for (const wchar_t ch : stem) {
        if (ch == L'_' || ch == L' ' || ch == L'-') flush();
        else word += ch;
    }
    flush();
    std::wstring name;
    for (const auto& item : words) name += (name.empty() ? L"" : L" ") + item;
    return name.empty() ? stem : name;
}

// Wine's own programs in a prefix (wordpad.exe, iexplore.exe...): its builtin and placeholder files
// carry the mark right after the DOS header.
bool wineStub(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    char head[128]{};
    in.read(head, sizeof head);
    const std::string_view bytes(head, static_cast<size_t>(in.gcount()));
    return bytes.find("Wine builtin DLL") != std::string_view::npos || bytes.find("Wine placeholder DLL") != std::string_view::npos;
}

// The program an installer most likely put in the prefix: the largest .exe written since it started,
// no uninstaller, setup, redistributable, crash reporter or Wine's own program; empty when none.
std::filesystem::path installedProgram(const std::filesystem::path& prefix, uint64_t started) {
    const auto drive = prefix / L"drive_c";
    std::filesystem::path best;
    uintmax_t best_size = 0;
    // What a new prefix brings itself.
    static constexpr std::wstring_view system_folders[] = {L"common files", L"internet explorer", L"windows media player", L"windows nt",
                                                           L"windows defender", L"microsoft.net", L"windowspowershell", L"temp", L"public"};
    for (const auto* folder : {L"Program Files", L"Program Files (x86)", L"GOG Games", L"Games", L"users"}) {
        std::error_code ec;
        std::filesystem::recursive_directory_iterator it(drive / folder, std::filesystem::directory_options::skip_permission_denied, ec), end;
        for (int count = 0; !ec && it != end && count < 20000; it.increment(ec), ++count) {
            if (it.depth() > 5) { it.disable_recursion_pending(); continue; }
            std::error_code type;
            if (it.depth() == 0 && it->is_directory(type)) {
                auto name = it->path().filename().wstring();
                for (auto& ch : name) ch = static_cast<wchar_t>(std::towlower(ch));
                if (std::find(std::begin(system_folders), std::end(system_folders), name) != std::end(system_folders)) {
                    it.disable_recursion_pending();
                    continue;
                }
            }
            if (!it->is_regular_file(type) || it->is_symlink(type)) continue;
            auto extension = it->path().extension().wstring();
            for (auto& ch : extension) ch = static_cast<wchar_t>(std::towlower(ch));
            if (extension != L".exe") continue;
            auto name = it->path().filename().wstring();
            for (auto& ch : name) ch = static_cast<wchar_t>(std::towlower(ch));
            if (name.starts_with(L"unins") || name.find(L"setup") != std::wstring::npos || name.find(L"redist") != std::wstring::npos ||
                name.find(L"crash") != std::wstring::npos || name.find(L"install") != std::wstring::npos || name.find(L"update") != std::wstring::npos)
                continue;
            const auto changed = std::filesystem::last_write_time(it->path(), type);
            const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::file_clock::to_sys(changed).time_since_epoch()).count();
            if (type || seconds + 120 < static_cast<int64_t>(started)) continue;
            const auto size = it->file_size(type);
            if (!type && size > best_size && !wineStub(it->path())) { best_size = size; best = it->path(); }
        }
    }
    return best;
}
} // namespace

// ---- Workers ---------------------------------------------------------------------------------------

void Launcher::compatLoadTools() {
    if (compat_.tools_loading) return;
    compat_.tools_loading = true;
    const auto alive = alive_; const auto post = uiPost();
    if (!work_.start([alive, post] {
        auto roots = compat_tools::systemRoots();
        auto tools = compat_tools::discover(roots);
        auto prefixes = store::prefixes();
        if (!alive->load()) return;
        post([tools = std::move(tools), prefixes = std::move(prefixes), umu = roots.umu_run](Launcher& app) mutable {
            app.compat_.tools = std::move(tools);
            app.compat_.prefixes = std::move(prefixes);
            app.compat_.umu_run = std::move(umu);
            app.compat_.tools_loading = false;
            app.compat_.tools_loaded = true;
            if (app.settingsOpen()) app.buildSettingsRows(); // Also the overview's summary of the default tool.
        });
    })) compat_.tools_loading = false;
}

void Launcher::compatLoadReleases(const std::string& id, bool refresh) {
    const auto* source = compat_tools::findSource(id);
    if (!source || compat_.releases_loading.contains(id)) return;
    compat_.releases_loading.insert(id);
    const auto alive = alive_; const auto post = uiPost();
    if (!work_.start([alive, post, source, id, refresh] {
        auto list = compat_tools::releases(*source, refresh);
        if (!alive->load()) return;
        post([id, list = std::move(list)](Launcher& app) mutable {
            app.compat_.releases[id] = std::move(list);
            app.compat_.releases_loading.erase(id);
            if (app.settingsOpen() && app.compatSettingsPage()) app.buildSettingsRows();
        });
    })) compat_.releases_loading.erase(id);
}

void Launcher::compatInstall(const compat_tools::Release& release) {
    if (compat_.install) return;
    const auto* source = compat_tools::findSource(release.source);
    if (!source) return;
    const auto target = compat_.steam_target && source->kind == compat_tools::Kind::proton ? compat_tools::Target::steam : compat_tools::Target::launchme;
    auto cancel = std::make_shared<std::atomic<bool>>(false);
    compat_.install = CompatSettings::Install{release.source, release.tag, {}, cancel};
    compat_.status.clear();
    const auto alive = alive_; const auto post = uiPost();
    if (!work_.start([release, target, cancel, post, alive] {
        uint64_t last = 0;
        const auto result = compat_tools::install(release, target, [cancel, alive] { return cancel->load() || !alive->load(); },
            [&](const compat_tools::Progress& progress) {
                const auto now = GetTickCount64();
                if (now - last < 200 && progress.done != progress.total) return;
                last = now;
                post([progress](Launcher& app) {
                    if (!app.compat_.install) return;
                    app.compat_.install->progress = progress;
                    if (app.settingsOpen() && app.settings_state_.at(Page::compat_source)) app.buildSettingsRows();
                });
            });
        post([result, release](Launcher& app) {
            app.compat_.install.reset();
            const auto waiting = std::exchange(app.compat_.launch_after, std::string{});
            if (result.error == compat_tools::Error::none) {
                app.compat_.status.clear();
                app.showNotification(NotificationKind::download, release.name, tr(L"Zainstalowano"));
            } else if (result.error != compat_tools::Error::cancelled) {
                app.compat_.status = label(compat_tools::errorText(result.error));
                app.showNotification(NotificationKind::error, tr(L"Nie udało się zainstalować ") + release.name, app.compat_.status);
            }
            app.compatLoadTools();
            if (app.settingsOpen() && app.compatSettingsPage()) app.buildSettingsRows();
            // The game that was waiting for it (compatProvision) starts now.
            if (result.error == compat_tools::Error::none && !waiting.empty())
                for (const auto* games : {&app.public_library_, &app.private_category_.games})
                    for (const auto& game : *games)
                        if (gameKey(game) == waiting) { const auto copy = game; app.launchGame(copy); return; }
        });
    })) compat_.install.reset();
}

void Launcher::compatAutoUpdate(bool now) {
    if (!settings_.compat_auto_update || compat_.install) return;
    const auto alive = alive_; const auto post = uiPost();
    work_.start([alive, post, now] {
        const auto roots = compat_tools::systemRoots();
        if (!now && !compat_tools::autoUpdateDue(roots.config, static_cast<int64_t>(std::time(nullptr)))) return;
        const auto* source = compat_tools::findSource("ge-proton");
        if (!source) return;
        auto release = compat_tools::autoUpdate(compat_tools::releases(*source), compat_tools::discover(roots));
        if (!release || !alive->load()) return;
        post([release = std::move(*release)](Launcher& app) {
            if (!app.settings_.compat_auto_update || app.compat_.install) return;
            app.showNotification(NotificationKind::download, release.name, tr(L"Nowy GE-Proton: pobieranie…"));
            app.compatInstall(release);
        });
    });
}

void Launcher::compatRefreshDatabase() {
    // umu-database: store games' protonfixes IDs (GAMEID / UMU_ID), fetched at most once a day.
    work_.start([] { compat_tools::refreshUmuDatabase(compat_tools::systemRoots().config); });
}

bool Launcher::compatToolReady(const Game& game) const {
    const auto launch = store::compatLaunch(game);
    return compat_tools::resolve(compat_tools::discover(), launch ? launch->options.compat_tool : std::string("default")).has_value();
}

void Launcher::compatProvision(const Game& game) {
    const auto key = gameKey(game);
    if (compat_.install) { // A download is on already: the game starts after it.
        compat_.launch_after = key;
        showNotification(NotificationKind::download, game.name, tr(L"Gra uruchomi się po zakończeniu pobierania."));
        return;
    }
    const auto* source = compat_tools::findSource("ge-proton");
    if (!source) return;
    compat_.launch_after = key;
    showNotification(NotificationKind::download, tr(L"Pobieranie GE-Proton"),
                     game.name + L" · " + tr(L"To gra Windows: najpierw Proton, potem gra uruchomi się sama."));
    const auto alive = alive_; const auto post = uiPost();
    if (!work_.start([alive, post, source] {
        auto list = compat_tools::releases(*source, false);
        if (!alive->load()) return;
        post([list = std::move(list)](Launcher& app) mutable {
            app.compat_.releases["ge-proton"] = list;
            if (list.releases.empty() || app.compat_.launch_after.empty()) {
                if (!app.compat_.launch_after.empty())
                    app.showNotification(NotificationKind::error, tr(L"Nie udało się pobrać GE-Proton"),
                                         tr(L"Sprawdź połączenie albo pobierz Protona w Ustawieniach → Zgodność."));
                app.compat_.launch_after.clear();
                return;
            }
            app.compatInstall(list.releases.front()); // The newest; its progress is in Ustawienia → Zgodność.
        });
    })) compat_.launch_after.clear();
}

std::string Launcher::compatMissingRuntime(const Game& game) const {
    const auto launch = store::compatLaunch(game);
    if (!launch) return {};
    const auto tool = compat_tools::resolve(compat_tools::discover(), launch->options.compat_tool);
    if (!tool || tool->kind != compat_tools::Kind::proton) return {}; // Only Proton reads them.
    const auto roots = compat_tools::systemRoots();
    if (launch->options.eac_runtime && compat_tools::steamApp(roots, L"Proton EasyAntiCheat Runtime").empty() &&
        compat_tools::installedRuntime(roots.config, compat_tools::eac_runtime).empty()) return std::string(compat_tools::eac_runtime);
    if (launch->options.battleye_runtime && compat_tools::steamApp(roots, L"Proton BattlEye Runtime").empty() &&
        compat_tools::installedRuntime(roots.config, compat_tools::battleye_runtime).empty()) return std::string(compat_tools::battleye_runtime);
    return {};
}

void Launcher::compatProvisionRuntime(const Game& game, const std::string& name) {
    const std::wstring title = name == compat_tools::eac_runtime ? L"Easy Anti-Cheat Runtime" : L"BattlEye Runtime";
    showNotification(NotificationKind::download, title, game.name + L" · " + tr(L"Pobieranie środowiska anti-cheat; gra uruchomi się sama."));
    const auto alive = alive_; const auto post = uiPost();
    work_.start([alive, post, name, title, key = gameKey(game)] {
        const auto error = compat_tools::installRuntime(name, [alive] { return !alive->load(); });
        if (!alive->load()) return;
        post([error, title, key](Launcher& app) {
            if (error != compat_tools::Error::none) {
                app.showNotification(NotificationKind::error, tr(L"Nie udało się pobrać ") + title, label(compat_tools::errorText(error)));
                return;
            }
            for (const auto* games : {&app.public_library_, &app.private_category_.games})
                for (const auto& game : *games)
                    if (gameKey(game) == key) { const auto copy = game; app.launchGame(copy); return; }
        });
    });
}

namespace {
constexpr auto vc_runtime_marker = ".launchme-vcrun2022"; // In the prefix: winetricks finished vcrun2022.
}

bool Launcher::compatNeedsVcRuntime(const Game& game) const {
    if (game.source != GameSource::xbox || !xbox_account::ownInstall(game) || compat_.vc_runtime_tried.contains(gameKey(game))) return false;
    const auto launch = store::compatLaunch(game);
    if (!launch || launch->prefix.empty()) return false;
    std::error_code ec;
    if (std::filesystem::exists(launch->prefix / vc_runtime_marker, ec)) return false;
    return wine_runner::winetricksAvailable(*launch);
}

void Launcher::compatProvisionVcRuntime(const Game& game) {
    const auto launch = store::compatLaunch(game);
    const auto key = gameKey(game);
    compat_.vc_runtime_tried.insert(key);
    pid_t pid = 0;
    if (!launch || wine_runner::winetricks(*launch, {"vcrun2022"}, pid)) { const auto copy = game; launchGame(copy); return; }
    showNotification(NotificationKind::download, game.name, tr(L"Instalowanie Visual C++ w prefiksie gry; gra uruchomi się sama."));
    const auto alive = alive_; const auto post = uiPost();
    if (!work_.start([alive, post, pid, prefix = launch->prefix, key, name = game.name] {
        auto code = process_spawn::waitExit(pid, std::chrono::minutes(20));
        if (!code) { process_spawn::killGroup(pid); process_spawn::reapLater(pid); }
        const bool ok = code && *code == 0;
        if (ok) atomic_file::write(prefix / vc_runtime_marker, std::string_view("vcrun2022\n"));
        if (!alive->load()) return;
        post([ok, key, name](Launcher& app) {
            if (!ok) app.showNotification(NotificationKind::error, name, tr(L"Nie udało się zainstalować Visual C++; gra uruchomi się bez niego."));
            for (const auto* games : {&app.public_library_, &app.private_category_.games})
                for (const auto& game : *games)
                    if (gameKey(game) == key) { const auto copy = game; app.launchGame(copy); return; }
        });
    })) process_spawn::reapLater(pid);
}

void Launcher::compatMeasure(const std::filesystem::path& prefix) {
    const auto normal = prefix.lexically_normal();
    if (compat_.measuring.contains(normal)) return;
    compat_.measuring.insert(normal);
    const auto alive = alive_; const auto post = uiPost();
    if (!work_.start([alive, post, normal] {
        const uint64_t size = folderSize(normal, alive);
        if (!alive->load()) return;
        post([normal, size](Launcher& app) {
            app.compat_.measuring.erase(normal);
            app.compat_.sizes[normal] = size;
            if (app.settingsOpen() && app.compatSettingsPage()) app.buildSettingsRows();
        });
    })) compat_.measuring.erase(normal);
}

void Launcher::compatPrefixAction(const std::filesystem::path& prefix, const std::string& action, const std::vector<std::string>& arguments) {
    const auto launch = compatPrefixLaunch(prefix);
    const auto title = launch.title;
    const auto failed = [&](int error) {
        showNotification(NotificationKind::error, tr(L"Nie udało się uruchomić narzędzia prefiksu"),
                         error == ENOENT ? std::wstring(tr(L"Brak Protona lub Wine: pobierz go w Ustawieniach → Proton i Wine.")) : wide(std::strerror(error)));
    };
    pid_t pid = 0;
    int error = 0;
    if (action == "create") error = wine_runner::createPrefix(launch, pid);
    else if (action == "winetricks") error = wine_runner::winetricks(launch, arguments, pid);
    else if (action == "run" && !arguments.empty()) error = wine_runner::runInPrefix(launch, pathOf(arguments.front()), {}, pid);
    else if (action == "kill" || action == "delete") {
        const auto alive = alive_; const auto post = uiPost();
        const bool remove = action == "delete";
        if (!work_.start([alive, post, launch, prefix, remove] {
            bool done = wine_runner::killPrefix(launch);
            if (remove) done = wine_runner::deletePrefix(prefix) && store::forgetPrefix(prefix);
            if (!alive->load()) return;
            post([done, remove, title = launch.title](Launcher& app) {
                if (remove && done) {
                    app.showNotification(NotificationKind::launch, title, tr(L"Prefiks usunięty"));
                    if (app.settingsOpen() && app.settings_state_.at(Page::prefix)) app.closeSettingsPage();
                } else if (remove) app.showNotification(NotificationKind::error, tr(L"Nie udało się usunąć prefiksu"), title);
                else app.showNotification(NotificationKind::launch, title, done ? tr(L"Procesy prefiksu zakończone") : tr(L"Nie wszystkie procesy się zakończyły"));
                app.compatLoadTools();
            });
        })) failed(EAGAIN);
        return;
    } else error = wine_runner::runWineProgram(launch, action, pid);
    if (error) { failed(error); return; }
    process_spawn::reapLater(pid); // Nobody else waits for it.
    showNotification(NotificationKind::launch, title, tr(L"Uruchomiono w prefiksie"));
}

// Protontricks for any game, not only Steam's: the game's own launch (its tool, prefix, umu ID and
// options) runs winetricks or a Wine program, as protontricks does for a Steam game.
void Launcher::compatGameTool(const Game& game, const std::string& action, const std::vector<std::string>& arguments) {
    const auto launch = store::compatLaunch(game);
    if (!launch) return;
    pid_t pid = 0;
    const int error = action == "winetricks" ? wine_runner::winetricks(*launch, arguments, pid) : wine_runner::runWineProgram(*launch, action, pid);
    if (error) {
        showNotification(NotificationKind::error, tr(L"Nie udało się uruchomić narzędzia prefiksu"),
                         error == ENOENT ? std::wstring(tr(L"Brak Protona lub Wine: pobierz go w Ustawieniach → Proton i Wine.")) : wide(std::strerror(error)));
        return;
    }
    process_spawn::reapLater(pid);
    showNotification(NotificationKind::launch, game.name, action == "winetricks" ? (arguments.empty() ? tr(L"Otwieranie winetricks…") : tr(L"Instalowanie: ") + wide(arguments.front()))
                                                                               : std::wstring(tr(L"Uruchomiono w prefiksie")));
}

// ---- A game from its installer -----------------------------------------------------------------------

void Launcher::compatInstallGame() {
    if (compat_.installer) { showNotification(NotificationKind::launch, compat_.installer->name, tr(L"Instalator już działa")); return; }
    file_browser::Request request;
    request.mode = file_browser::Mode::file;
    request.title = tr(L"Wybierz instalator gry");
    request.extensions = {L".exe", L".msi"};
    request.filter_label = tr(L"Instalatory Windows (.exe, .msi)");
    request.all_files_option = true;
    const auto home = xdg_paths::home();
    std::error_code ec;
    for (const auto* folder : {L"Downloads", L"Pobrane"})
        if (!home.empty() && std::filesystem::is_directory(home / folder, ec)) { request.initial = home / folder; break; }
    openFileBrowser(std::move(request), [this](const std::filesystem::path& picked) { if (!picked.empty()) compatRunInstaller(picked); });
}

void Launcher::compatInstallStore(const std::string& id) {
    const auto* store_installer = findStore(id);
    if (!store_installer || compat_.installer || !compat_.store_download.empty()) return;
    compat_.store_download = id;
    showNotification(NotificationKind::download, store_installer->name, tr(L"Pobieranie instalatora…"));
    if (settingsOpen()) buildSettingsRows();
    const auto folder = compat_tools::toolsFolder(compat_tools::systemRoots().config) / "cache" / "installers";
    const auto file = folder / store_installer->file;
    const std::string url = text_encoding::utf8Lenient(store_installer->url);
    const auto alive = alive_; const auto post = uiPost();
    const auto finish = [alive, post, id, file](bool ok) {
        if (!alive->load()) return;
        post([id, file, ok](Launcher& app) {
            app.compat_.store_download.clear();
            const auto* found = findStore(id);
            if (!ok || !found) {
                app.showNotification(NotificationKind::error, tr(L"Nie udało się pobrać instalatora"), found ? std::wstring(found->url) : std::wstring{});
                if (app.settingsOpen()) app.buildSettingsRows();
                return;
            }
            app.compatRunInstaller(file, id);
        });
    };
    if (!work_.start([folder, file, url, finish] {
        // A recent copy is reused (a week); otherwise curl fetches the current installer.
        std::error_code ec;
        const auto written = std::filesystem::last_write_time(file, ec);
        if (!ec && std::filesystem::file_size(file, ec) > 0 && !ec &&
            std::filesystem::file_time_type::clock::now() - written < std::chrono::hours(24 * 7)) { finish(true); return; }
        std::filesystem::create_directories(folder, ec);
        const auto part = file.string() + ".part";
        pid_t pid = 0;
        process_spawn::Options options;
        options.search = true;
        if (process_spawn::spawn({"curl", "-fsSL", "--retry", "2", "--connect-timeout", "20", "-o", part, url}, options, pid)) { finish(false); return; }
        int status = 0;
        while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
        const bool ok = WIFEXITED(status) && WEXITSTATUS(status) == 0;
        if (ok) std::filesystem::rename(part, file, ec);
        else std::filesystem::remove(part, ec);
        finish(ok && !ec);
    })) { compat_.store_download.clear(); showNotification(NotificationKind::error, tr(L"Nie udało się pobrać instalatora"), wide(url)); }
}

void Launcher::compatRunInstaller(const std::filesystem::path& installer, const std::string& store_id) {
    const auto* store_installer = findStore(store_id);
    const auto name = store_installer ? std::wstring(store_installer->name) : installerName(installer);
    const auto prefix = store::newPrefix(name, "default");
    if (prefix.empty()) { showNotification(NotificationKind::error, tr(L"Nie udało się utworzyć prefiksu"), store::prefixRoot().wstring()); return; }
    compat_.prefixes = store::prefixes();
    auto launch = compatPrefixLaunch(prefix);
    launch.title = name;
    pid_t pid = 0;
    const uint64_t started = static_cast<uint64_t>(std::time(nullptr));
    if (const int error = wine_runner::runInPrefix(launch, installer, {}, pid); error) {
        showNotification(NotificationKind::error, tr(L"Nie udało się uruchomić instalatora"),
                         error == ENOENT ? std::wstring(tr(L"Brak Protona lub Wine: pobierz go w Ustawieniach → Proton i Wine.")) : wide(std::strerror(error)));
        return;
    }
    compat_.installer = CompatSettings::Installer{prefix, installer, name, store_id};
    showNotification(NotificationKind::download, name, store_installer ? std::wstring(tr(L"Instalator działa w nowym prefiksie. Po instalacji sklep trafi do biblioteki.")) :
                                                                          std::wstring(tr(L"Instalator działa w nowym prefiksie. Po instalacji wskażesz plik gry.")));
    if (settingsOpen()) buildSettingsRows();
    const auto alive = alive_; const auto post = uiPost();
    // The runner lasts as long as the installer (and what it starts in the prefix).
    if (!work_.start([alive, post, pid, prefix, name, started, store_id] {
        int status = 0;
        while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
        if (!alive->load()) return;
        post([prefix, name, started, store_id](Launcher& app) { app.compatInstallerFinished(prefix, name, started, store_id); });
    })) process_spawn::reapLater(pid);
}

void Launcher::compatInstallerFinished(const std::filesystem::path& prefix, const std::wstring& name, uint64_t started, const std::string& store_id) {
    compat_.installer.reset();
    if (settingsOpen()) buildSettingsRows();
    // A store launcher: its known program, added at once.
    if (const auto* store_installer = findStore(store_id)) {
        std::error_code drive_error;
        const auto drive = std::filesystem::is_directory(prefix / L"drive_c", drive_error) ? prefix / L"drive_c" : prefix / L"pfx" / L"drive_c";
        for (const auto* program : store_installer->programs) {
            std::error_code ec;
            if (!program || !std::filesystem::is_regular_file(drive / program, ec)) continue;
            custom_games::Entry entry;
            entry.name = name;
            entry.path = drive / program;
            const auto id = addCustomGame(entry);
            if (id.empty()) { showNotification(NotificationKind::error, tr(L"Nie udało się dodać gry"), entry.path.wstring()); return; }
            compatSaveOptions("custom:" + id, [&](LaunchOptions& options) { options.prefix = prefix; return true; });
            compatLoadTools();
            showNotification(NotificationKind::download, name, tr(L"Dodano do biblioteki: gry instalujesz już w nim."));
            if (settingsOpen()) buildSettingsRows();
            return;
        }
        // Installed somewhere else (or the installer was closed): picked by hand, as any game.
    }
    // The game's program: the installer's newest program file is offered first.
    file_browser::Request request;
    request.mode = file_browser::Mode::file;
    request.title = tr(L"Wybierz plik gry: ") + name;
    request.extensions = {L".exe", L".lnk", L".bat"};
    request.filter_label = tr(L"Programy Windows");
    request.all_files_option = true;
    const auto guess = installedProgram(prefix, started);
    std::error_code ec;
    // The guess highlighted in its folder: Enter takes it.
    request.initial = !guess.empty() ? guess : std::filesystem::is_directory(prefix / L"drive_c", ec) ? prefix / L"drive_c" : prefix;
    openFileBrowser(std::move(request), [this, prefix, name](const std::filesystem::path& picked) {
        if (picked.empty()) {
            showNotification(NotificationKind::launch, name, tr(L"Gry nie dodano. Prefiks został na liście prefiksów."));
            return;
        }
        custom_games::Entry entry;
        entry.name = name;
        entry.path = picked;
        const auto id = addCustomGame(entry);
        if (id.empty()) { showNotification(NotificationKind::error, tr(L"Nie udało się dodać gry"), picked.wstring()); return; }
        // The game runs in the prefix it was installed into.
        compatSaveOptions("custom:" + id, [&](LaunchOptions& options) { options.prefix = prefix; return true; });
        compatLoadTools();
        showNotification(NotificationKind::download, name, tr(L"Dodano do biblioteki"));
        openCustomEditor(id); // Its name and artwork, at once.
    });
}

// Odinstaluj, then this: a game with a prefix on disk is asked whether it goes too. Another game still
// using it is named in the question; the shared <root>/default of programs without a game is never offered.
bool Launcher::askUninstallPrefix(const Game& game, const GameAction& silent) {
    const auto key = gameKey(game);
    const auto prefix = store::existingPrefix(key);
    if (!prefix || !wine_runner::prefixExists(*prefix)) return false;
    if (prefix->lexically_normal() == (store::prefixRoot() / L"default").lexically_normal()) return false;
    uninstall_game_ = game;
    uninstall_silent_ = silent;
    uninstall_prefix_ = prefix->lexically_normal();
    uninstall_prefix_users_.clear();
    for (const auto& other : store::gamesUsingPrefix(*prefix)) {
        if (other == key) continue;
        const auto found = std::find_if(grouped_library_.begin(), grouped_library_.end(), [&](const Game& item) { return gameKey(item) == other; });
        uninstall_prefix_users_.push_back(found != grouped_library_.end() ? found->name : wide(other));
    }
    prompt_kind_ = PromptKind::uninstall_prefix;
    exit_prompt_ = 1; // "Zostaw prefiks": deleting it is a deliberate step.
    audio_.context();
    InvalidateRect(hwnd_, nullptr, FALSE);
    return true;
}
#endif
