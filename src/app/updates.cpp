// Launchme's own updates (src/platform/updater.hpp). Now and then (and from "Sprawdź teraz") a
// worker asks GitHub for the latest release; a newer one brings a notification pointing to
// Ustawienia → System → Aktualizacje, where its row (and the desktop top bar's icon) updates with
// one click: the package is downloaded next to launchme.exe, its SHA-256 checked, unpacked, the
// files swapped in, and the new version started while this one closes. Never while a game runs.
// The new version removes the old files and shows its changes once (Co nowego, Modal::whats_new).
#include "launcher.hpp"
#include "../net/http_download.hpp"
#include "../platform/atomic_file.hpp"
#include "../platform/diagnostics.hpp"
#include "../platform/text_encoding.hpp"
#ifndef _WIN32
#include "../platform/process_spawn.hpp"
#include "../platform/system.hpp"
#endif

#include <fstream>

namespace {
constexpr ULONGLONG first_check_ms = 30 * 1000, check_interval_ms = 6ULL * 60 * 60 * 1000, game_retry_ms = 10 * 60 * 1000;
std::filesystem::path whatsNewFile() { return userConfigDirectory() / L"whats-new.txt"; }
#ifdef _WIN32
std::filesystem::path moduleFolder() {
    wchar_t module[32768]{};
    GetModuleFileNameW(nullptr, module, static_cast<DWORD>(std::size(module)));
    return std::filesystem::path(module).parent_path();
}
#else
// The AppImage's folder (updater.hpp: only an AppImage updates itself), else the executable's.
std::filesystem::path moduleFolder() {
    if (const auto image = updater::appImage()) return image->parent_path();
    return system_info::executablePath().parent_path();
}
#endif
std::wstring megabytes(uint64_t bytes) { return std::to_wstring((bytes + 512 * 1024) / (1024 * 1024)); }
#ifdef _WIN32
bool restartInto(const std::filesystem::path& exe) {
    STARTUPINFOW startup{sizeof(startup)};
    PROCESS_INFORMATION process{};
    std::wstring command = L"\"" + exe.wstring() + L"\" --after-update=" + std::to_wstring(GetCurrentProcessId());
    if (!CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr, exe.parent_path().c_str(), &startup, &process)) return false;
    CloseHandle(process.hThread); CloseHandle(process.hProcess);
    return true;
}
#else
// The new AppImage, in a session of its own (it outlives this process, which it waits for).
bool restartInto(const std::filesystem::path& exe) {
    process_spawn::Options options;
    options.directory = exe.parent_path();
    options.session = true;
    pid_t pid = 0;
    if (process_spawn::spawn({exe.string(), "--after-update=" + std::to_string(system_info::processId())}, options, pid) != 0) return false;
    process_spawn::reapLater(pid);
    return true;
}
// The file the new version is started from: the running AppImage's name, as updater::unpack stages it.
std::filesystem::path restartTarget(const std::filesystem::path& app) {
    if (const auto image = updater::appImage()) return app / image->filename();
    return app / L"launchme.AppImage";
}
#endif
}

bool Launcher::restartForImport() {
    const std::function<bool(const std::filesystem::path&)> restart = update_restart_ ? update_restart_ : std::function<bool(const std::filesystem::path&)>(restartInto);
#ifdef _WIN32
    wchar_t module[32768]{};
    GetModuleFileNameW(nullptr, module, static_cast<DWORD>(std::size(module)));
    const std::filesystem::path exe = module;
#else
    const auto image = updater::appImage();
    const auto exe = image ? *image : system_info::executablePath();
#endif
    if (!restart(exe)) {
        showNotification(NotificationKind::error, tr(L"Nie można uruchomić ponownie"), tr(L"Zamknij i uruchom Launchme samodzielnie: import wczyta się przy starcie."));
        return false;
    }
    diagnostics::log("info", L"Restarting to apply an import");
    beginClose();
    return true;
}

void Launcher::initializeUpdates() {
    if (app_version_.empty()) app_version_ = diagnostics::version();
    if (update_app_dir_.empty()) update_app_dir_ = moduleFolder();
    std::error_code ec;
    // The previous version's files, renamed when this one moved in (and its staging folder).
    if (std::filesystem::exists(update_app_dir_ / updater::work_folder, ec)) {
        updater::cleanup(update_app_dir_);
#ifdef _WIN32
        updater::recordInstalledVersion(update_app_dir_, app_version_); // An installed copy's uninstall entry.
#endif
    }
#ifdef _WIN32
    // A system-wide install updates through its Setup, downloaded beside the settings.
    update_.system = updater::systemInstall(update_app_dir_);
    if (std::filesystem::exists(userConfigDirectory() / updater::work_folder, ec)) std::filesystem::remove_all(userConfigDirectory() / updater::work_folder, ec);
#endif
    // Co nowego: written by the version that installed this one, shown once.
    const auto file = whatsNewFile();
    if (std::filesystem::exists(file, ec)) {
        std::string content;
        { std::ifstream input(file, std::ios::binary); content.assign(std::istreambuf_iterator<char>(input), {}); }
        DeleteFileW(file.wstring().c_str());
        const auto newline = content.find('\n');
        const auto version = text_encoding::wideLenient(content.substr(0, newline));
        if (newline != std::string::npos && !version.empty() && version == app_version_)
            openWhatsNew(version, text_encoding::wideLenient(content.substr(newline + 1)));
    }
    update_.next_check = GetTickCount64() + first_check_ms;
}

void Launcher::tickUpdates() {
    if (!runtime_started_ || !settings_.update_check || !update_.next_check) return;
    const auto now = GetTickCount64();
    if (now < update_.next_check) return;
    if (anyActiveSession()) { update_.next_check = now + game_retry_ms; return; } // Never while a game runs.
    update_.next_check = now + check_interval_ms;
    checkForUpdate(false);
}

bool Launcher::updateAvailable() const { return update_.available && updater::newer(update_.available->version, app_version_); }

float Launcher::updateFraction() const {
    if (update_.phase != UpdateState::Phase::downloading || !update_.available || !update_.available->package_size) return -1;
    return std::clamp(static_cast<float>(static_cast<double>(update_.done) / static_cast<double>(update_.available->package_size)), 0.0f, 1.0f);
}

void Launcher::checkForUpdate(bool manual) {
    using Phase = UpdateState::Phase;
    if (update_.phase == Phase::checking || update_.phase == Phase::downloading || update_.phase == Phase::installing) return;
    if (app_version_.empty()) app_version_ = diagnostics::version();
    if (update_.phase != Phase::ready) update_.phase = Phase::checking;
    update_.manual = manual;
    const auto generation = ++update_.generation;
    std::function<HttpResponse(const std::wstring&)> get = update_get_;
    if (!get) get = [](const std::wstring& url) {
        HttpBudget budget(20000);
        return httpGet(url, L"Accept: application/vnd.github+json\r\nX-GitHub-Api-Version: 2022-11-28\r\n", 4 * 1024 * 1024);
    };
    const auto alive = alive_;
    const auto window = hwnd_;
    work_.start([generation, get = std::move(get), alive, window] {
        const auto reply = get(updater::latest_url);
        // No release yet (404) is an answer; no network, a limit (403/429) or a server error is not.
        const bool failed = reply.status == 0 || reply.status == 403 || reply.status == 429 || reply.status >= 500;
        std::optional<updater::Release> release;
        if (reply.status == 200) release = updater::parseRelease(std::string_view(reinterpret_cast<const char*>(reply.body.data()), reply.body.size()));
        if (!alive->load()) return;
        auto message = std::make_unique<RemoteMessage>();
        message->run = [generation, release = std::move(release), failed](Launcher& app) mutable { app.updateFound(generation, std::move(release), failed); };
        if (PostMessageW(window, remote_message, 0, reinterpret_cast<LPARAM>(message.get()))) message.release();
    });
    if (settingsOpen()) buildSettingsRows();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::updateFound(uint32_t generation, std::optional<updater::Release> release, bool failed) {
    using Phase = UpdateState::Phase;
    if (generation != update_.generation) return;
    if (update_.phase == Phase::checking) update_.phase = Phase::idle;
    const bool manual = std::exchange(update_.manual, false);
    if (failed) {
        if (manual) showNotification(NotificationKind::error, tr(L"Nie udało się sprawdzić aktualizacji"), tr(L"GitHub nie odpowiada. Spróbuj później."));
    } else if (release && updater::newer(release->version, app_version_)) {
        const auto version = release->version;
        if (!update_.available || update_.available->version != version) { update_.files.clear(); if (update_.phase == Phase::ready) update_.phase = Phase::idle; }
        update_.available = std::move(release);
        if (update_.announced != version && (manual || version != settings_.update_skipped)) {
            update_.announced = version;
            showNotification(NotificationKind::download, tr(L"Dostępna aktualizacja: Launchme ") + version,
                             desktop_mode_ ? tr(L"Czeka w Ustawienia → System (albo ikona na górnym pasku)") : tr(L"Czeka w Ustawienia → System → Aktualizacje"));
        }
    } else {
        update_.available.reset();
        if (manual) showNotification(NotificationKind::launch, tr(L"Masz najnowszą wersję"), L"Launchme " + app_version_);
    }
    if (settingsOpen()) buildSettingsRows();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::updateAction() {
    using Phase = UpdateState::Phase;
    if (update_.phase == Phase::checking || update_.phase == Phase::downloading || update_.phase == Phase::installing) return;
    if (!updateAvailable()) { checkForUpdate(true); return; }
    if (!game_sessions_.empty()) { // It restarts Launchme: not under a running or suspended game.
        showNotification(NotificationKind::error, tr(L"Najpierw zamknij grę"), tr(L"Aktualizacja uruchomi Launchme ponownie."));
        return;
    }
    if (update_.phase == Phase::ready && !update_.files.empty()) { installUpdate(); return; }
    startUpdate();
}

void Launcher::startUpdate() {
    using Phase = UpdateState::Phase;
    if (!update_.available) return;
    update_.phase = Phase::downloading;
    update_.done = 0;
    update_.error.clear();
    update_.files.clear();
    const auto generation = ++update_.generation;
    if (update_.system) { // The download is the release's Setup (also for the row's percent).
        auto& available = *update_.available;
        if (available.installer_url.empty()) {
            update_.phase = Phase::failed;
            update_.error = tr(L"Ta wersja nie ma instalatora. Pobierz ją ze strony wydania.");
            showNotification(NotificationKind::error, tr(L"Aktualizacja nie powiodła się"), update_.error);
            if (settingsOpen()) buildSettingsRows();
            InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        }
        available.package_url = available.installer_url;
        available.package_size = available.installer_size;
        available.sha256 = available.installer_sha256;
    }
    const auto release = *update_.available;
    const bool machine = update_.system;
    const auto app = update_app_dir_.empty() ? moduleFolder() : update_app_dir_;
    const auto setup_folder = userConfigDirectory() / updater::work_folder; // Program Files is not writable.
    const auto alive = alive_;
    const auto window = hwnd_;
    const auto post = [alive, window](std::function<void(Launcher&)> run) {
        if (!alive->load()) return;
        auto message = std::make_unique<RemoteMessage>();
        message->run = std::move(run);
        if (PostMessageW(window, remote_message, 0, reinterpret_cast<LPARAM>(message.get()))) message.release();
    };
    work_.start([generation, release, machine, app, setup_folder, alive, post, download = update_download_] {
        const auto folder = machine ? setup_folder : app / updater::work_folder;
#ifdef _WIN32
        const auto zip = folder / (L"launchme-" + release.version + (machine ? L"-win64-setup.exe" : L"-win64.zip"));
#else
        const auto zip = folder / (L"launchme-" + release.version + L"-x86_64.AppImage"); // The package is the AppImage.
#endif
        const std::filesystem::path part = zip.wstring() + L".part";
        std::wstring error;
        std::vector<std::filesystem::path> files;
        std::error_code ec;
        std::filesystem::create_directories(folder, ec);
        ULONGLONG last = 0;
        const auto progress = [&](uint64_t done) {
            const auto now = GetTickCount64();
            if (now - last < 200 && done < release.package_size) return;
            last = now;
            post([generation, done](Launcher& launcher) { launcher.updateProgress(generation, done); });
        };
        if (ec || !std::filesystem::is_directory(folder, ec)) error = tr(L"Brak zapisu w folderze Launchme: ") + app.wstring();
        else if (!(download ? download(release, part, progress) :
                   httpDownload(release.package_url, part, release.package_size, [alive] { return !alive->load(); }, progress, updater::allowedDownload) == DownloadError::none))
            error = tr(L"Nie udało się pobrać paczki z GitHuba.");
        else if (updater::sha256File(part) != release.sha256) { error = tr(L"Pobrana paczka jest uszkodzona (inna suma SHA-256)."); DeleteFileW(part.wstring().c_str()); }
        else if (machine) {
            if (MoveFileExW(part.wstring().c_str(), zip.wstring().c_str(), MOVEFILE_REPLACE_EXISTING)) files = {zip}; // Run by installUpdate.
            else error = tr(L"Brak zapisu w folderze Launchme: ") + folder.wstring();
        } else {
            MoveFileExW(part.wstring().c_str(), zip.wstring().c_str(), MOVEFILE_REPLACE_EXISTING);
            std::wstring detail;
            if (auto unpacked = updater::unpack(zip, folder / L"staging", detail)) files = std::move(*unpacked);
            else error = tr(L"Nie udało się rozpakować paczki: ") + detail;
            DeleteFileW(zip.wstring().c_str());
        }
        post([generation, error, files](Launcher& launcher) { launcher.updateStaged(generation, error, files); });
    });
    if (settingsOpen()) buildSettingsRows();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::updateProgress(uint32_t generation, uint64_t done) {
    if (generation != update_.generation || update_.phase != UpdateState::Phase::downloading) return;
    const auto before = static_cast<int>(updateFraction() * 100);
    update_.done = done;
    if (settingsOpen() && static_cast<int>(updateFraction() * 100) != before) buildSettingsRows(); // Its row's percent.
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::updateStaged(uint32_t generation, std::wstring error, std::vector<std::filesystem::path> files) {
    using Phase = UpdateState::Phase;
    if (generation != update_.generation || update_.phase != Phase::downloading) return;
    if (!error.empty()) {
        update_.phase = Phase::failed;
        update_.error = error;
        diagnostics::log("error", L"Update failed: " + error);
        showNotification(NotificationKind::error, tr(L"Aktualizacja nie powiodła się"), error);
    } else {
        update_.phase = Phase::ready;
        update_.files = std::move(files);
        if (!game_sessions_.empty()) showNotification(NotificationKind::download, tr(L"Aktualizacja gotowa"), tr(L"Zamknij grę i kliknij Aktualizuj."));
        else installUpdate();
    }
    if (settingsOpen()) buildSettingsRows();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::installUpdate() {
    using Phase = UpdateState::Phase;
    if (update_.phase != Phase::ready || update_.files.empty() || !update_.available || !game_sessions_.empty()) return;
    update_.phase = Phase::installing;
    const auto app = update_app_dir_.empty() ? moduleFolder() : update_app_dir_;
    const auto& release = *update_.available;
    const auto fail = [&](std::wstring error) {
        update_.phase = Phase::failed;
        update_.files.clear(); // Downloaded again on the next try.
        update_.error = error;
        diagnostics::log("error", L"Update failed: " + error);
        showNotification(NotificationKind::error, tr(L"Aktualizacja nie powiodła się"), error);
        if (settingsOpen()) buildSettingsRows();
        InvalidateRect(hwnd_, nullptr, FALSE);
    };
#ifdef _WIN32
    if (update_.system) {
        // The new Setup, elevated, closes this launcher (Restart Manager),
        // installs for all users and starts the new version as this user (/RELAUNCH, runasoriginaluser).
        atomic_file::write(whatsNewFile(), text_encoding::utf8Lenient(release.version + L"\n" + release.notes));
        const auto setup = update_.files.front().wstring();
        SHELLEXECUTEINFOW info{sizeof(info)};
        info.fMask = SEE_MASK_NOASYNC;
        info.lpVerb = L"runas";
        info.lpFile = setup.c_str();
        info.lpParameters = L"/SILENT /SUPPRESSMSGBOXES /NORESTART /ALLUSERS /RELAUNCH=1";
        info.nShow = SW_SHOWNORMAL;
        if (!ShellExecuteExW(&info)) { // Also when the administrator prompt was declined.
            DeleteFileW(whatsNewFile().wstring().c_str());
            fail(tr(L"Aktualizacja wymaga zgody administratora."));
            return;
        }
        diagnostics::log("info", L"Running the " + release.version + L" installer; closing");
        beginClose();
        return;
    }
#endif
    std::wstring error;
    if (!updater::apply(app / updater::work_folder / L"staging", update_.files, app, error)) { fail(error); return; }
    // Its changes, for the new version's first start.
    atomic_file::write(whatsNewFile(), text_encoding::utf8Lenient(release.version + L"\n" + release.notes));
    const std::function<bool(const std::filesystem::path&)> restart = update_restart_ ? update_restart_ : std::function<bool(const std::filesystem::path&)>(restartInto);
#ifdef _WIN32
    if (!restart(app / L"launchme.exe")) {
#else
    if (!restart(restartTarget(app))) {
#endif
        updater::rollback(app, update_.files);
        DeleteFileW(whatsNewFile().wstring().c_str());
        fail(tr(L"Nie można uruchomić nowej wersji; przywrócono poprzednią."));
        return;
    }
    diagnostics::log("info", L"Updated to " + release.version + L"; restarting");
    beginClose(); // The new process waits for this one to end.
}

std::tuple<std::wstring, std::wstring, std::wstring> Launcher::updateRow() const {
    using Phase = UpdateState::Phase;
    const bool available = updateAvailable();
    std::wstring title = available ? tr(L"Dostępna aktualizacja: Launchme ") + update_.available->version : L"Launchme " + app_version_;
    std::wstring help, button;
    switch (update_.phase) {
    case Phase::checking: help = tr(L"Sprawdzanie GitHuba…"); button = tr(L"Sprawdzanie…"); break;
    case Phase::downloading: {
        const auto fraction = updateFraction();
        help = tr(L"Pobieranie ") + std::to_wstring(static_cast<int>(std::max(0.0f, fraction) * 100)) + L"% · " + megabytes(update_.done) + L" / " +
               megabytes(update_.available ? update_.available->package_size : 0) + L" MB";
        button = tr(L"Pobieranie…");
        break;
    }
    case Phase::installing: help = tr(L"Instalowanie i ponowne uruchamianie…"); button = tr(L"Instalowanie…"); break;
    case Phase::ready: help = tr(L"Gotowa do instalacji. Launchme uruchomi się ponownie."); button = tr(L"Aktualizuj"); break;
    case Phase::failed:
        help = update_.error;
        button = available ? tr(L"Spróbuj ponownie") : tr(L"Sprawdź teraz");
        break;
    case Phase::idle:
        if (available) {
            help = update_.system ? tr(L"Pobierze instalator z GitHuba, sprawdzi go i poprosi o zgodę administratora (instalacja dla wszystkich użytkowników).")
                                  : tr(L"Pobierze paczkę z GitHuba, sprawdzi ją i uruchomi Launchme ponownie. Zmiany zobaczysz po aktualizacji.");
            button = tr(L"Aktualizuj");
        } else {
            help = tr(L"Sprawdza, czy na GitHubie jest nowsza wersja.");
            button = tr(L"Sprawdź teraz");
        }
        break;
    }
    return {title, help, button};
}

// ---- Co nowego ---------------------------------------------------------------------------------

void Launcher::openWhatsNew(std::wstring version, std::wstring text) {
    whats_new_ = {};
    whats_new_.open = true;
    whats_new_.version = std::move(version);
    whats_new_.text = std::move(text);
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::closeWhatsNew() {
    if (!whats_new_.open) return;
    whats_new_.open = false;
    audio_.navigate();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::whatsNewScroll(float amount) {
    if (!whats_new_.open) return;
    const auto view = renderer_.viewport();
    const update_ui::WhatsNewLayout layout{view.width, view.height};
    const float next = std::clamp(whats_new_.scroll + amount, 0.0f, layout.maxScroll(whats_new_.content));
    if (next == whats_new_.scroll) return;
    whats_new_.scroll = next;
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::whatsNewKey(WPARAM key) {
    const auto view = renderer_.viewport();
    const auto text = update_ui::WhatsNewLayout{view.width, view.height}.text();
    const float page = (text.bottom - text.top) * 0.85f;
    if (key == VK_UP || key == 'W') whatsNewScroll(-48);
    else if (key == VK_DOWN || key == 'S') whatsNewScroll(48);
    else if (key == VK_PRIOR) whatsNewScroll(-page);
    else if (key == VK_NEXT) whatsNewScroll(page);
    else if (key == VK_HOME) whatsNewScroll(-1e9f);
    else if (key == VK_END) whatsNewScroll(1e9f);
    else if (key == VK_RETURN || key == VK_SPACE || key == VK_ESCAPE || key == VK_BACK) closeWhatsNew();
}

void Launcher::whatsNewClick(float x, float y) {
    const auto view = renderer_.viewport();
    const update_ui::WhatsNewLayout layout{view.width, view.height};
    if (layout.ok().contains(x, y) || !layout.panel().contains(x, y)) closeWhatsNew();
}
