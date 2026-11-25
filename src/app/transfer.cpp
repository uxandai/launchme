// Ustawienia → System → Kopia i przenoszenie (src/platform/profile_transfer.hpp, src/library/playnite_import.hpp):
// the export's parts and password, a backup picked to import, Playnite's library, and the one job running in
// the background (a worker posting its progress; Anuluj stops it). An import is staged; the next start applies
// it, so its status row offers Uruchom ponownie.
#include "launcher.hpp"
#include "../platform/diagnostics.hpp"
#include "../platform/file_dialog.hpp"
#include "../platform/text_encoding.hpp"

#include <chrono>

using namespace settings_ui;
using profile_transfer::Part;
using TJob = launcher_state::TransferState::Job;

namespace {
const wchar_t* partTitle(Part part) {
    switch (part) {
    case Part::settings: return trKey(L"Ustawienia");
    case Part::library: return trKey(L"Biblioteka");
    case Part::artwork: return trKey(L"Wybrane grafiki");
    case Part::artwork_cache: return trKey(L"Pobrane grafiki");
    case Part::keys: return trKey(L"Klucze usług");
    case Part::accounts: return trKey(L"Sesje sklepów");
    case Part::plugins: return trKey(L"Wtyczki");
    case Part::companion: return trKey(L"Asystent");
    }
    return L"";
}
const wchar_t* partDescription(Part part) {
    switch (part) {
    case Part::settings: return trKey(L"Wygląd, motywy, tapety, czcionki, foldery bibliotek i opcje. Okno i monitor zostają z tego komputera.");
    case Part::library: return trKey(L"Czas gry, ostatnio grane, kolekcje, ulubione, ukryte, gry dodane ręcznie i katalog DRM-free.");
    case Part::artwork: return trKey(L"Okładki, tła i loga wybrane lub dodane ręcznie.");
    case Part::artwork_cache: return trKey(L"Wszystkie pobrane grafiki: bez nich pobiorą się ponownie.");
    case Part::keys: return trKey(L"Steam Web API, SteamGridDB, RAWG, ScreenScraper i inne. Zaszyfrowane hasłem.");
    case Part::accounts: return trKey(L"Zalogowane sklepy (GOG, Epic, Amazon, Xbox): bez ponownego logowania. Zaszyfrowane hasłem.");
    case Part::plugins: return trKey(L"Zainstalowane wtyczki i ich dane. Zaszyfrowane hasłem.");
    case Part::companion: return trKey(L"Postać asystenta, jej pamięć i rozmowy. Zaszyfrowane hasłem.");
    }
    return L"";
}
std::wstring size(uint64_t bytes) {
    const double megabytes = bytes / (1024.0 * 1024.0);
    wchar_t text[32]{};
    if (bytes < 1024 * 1024) swprintf(text, std::size(text), L"%llu KB", static_cast<unsigned long long>((bytes + 1023) / 1024));
    else swprintf(text, std::size(text), megabytes < 10 ? L"%.1f MB" : L"%.0f MB", megabytes);
    return text;
}
std::wstring amount(size_t files, uint64_t bytes) { return tr(L"Pliki: ") + std::to_wstring(files) + L" · " + size(bytes); }
struct PlayniteOption { const char* key; const wchar_t* title; const wchar_t* description; bool playnite::Options::*flag; };
const PlayniteOption playnite_options[] = {
    {"playtime", trKey(L"Czas gry i ostatnio grane"), trKey(L"Także data dodania. Większy czas wygrywa z tym, co Launchme już zna."), &playnite::Options::playtime},
    {"favorites", trKey(L"Ulubione"), trKey(L"Gry Steam trafiają do ulubionych Twojego konta Steam."), &playnite::Options::favorites},
    {"hidden", trKey(L"Ukryte gry"), trKey(L"Gry ukryte w Playnite będą ukryte także tutaj."), &playnite::Options::hidden},
    {"categories", trKey(L"Kategorie jako kolekcje"), trKey(L"Każda kategoria Playnite staje się kolekcją Launchme."), &playnite::Options::categories},
    {"tags", trKey(L"Tagi jako kolekcje"), trKey(L"Każdy tag Playnite staje się kolekcją Launchme."), &playnite::Options::tags},
    {"completion", trKey(L"Status ukończenia jako kolekcje"), trKey(L"Np. „Ukończone” albo „W trakcie”."), &playnite::Options::completion},
    {"artwork", trKey(L"Okładki i tła"), trKey(L"Okładka Playnite jako plakat, tło jako grafika tła gry."), &playnite::Options::artwork},
    {"own_games", trKey(L"Gry bez sklepu"), trKey(L"Gry uruchamiane z pliku trafiają do Gier dodanych ręcznie."), &playnite::Options::own_games},
    {"emulated", trKey(L"Gry z emulatorów"), trKey(L"ROM-y z platformą Playnite stają się grami konsoli w Emulacji."), &playnite::Options::emulated},
};
}

// Posts `run` to the UI thread from a worker (UiPost, worker_messages.hpp).
static void postToLauncher(HWND hwnd, const std::shared_ptr<std::atomic_bool>& alive, std::function<void(Launcher&)> run) {
    UiPost{hwnd, alive}(std::move(run));
}

bool Launcher::transferPage() const {
    return settings_state_.at(Page::transfer) || settings_state_.at(Page::transfer_import) || settings_state_.at(Page::playnite);
}

std::vector<Row> Launcher::transferSettingsRows() const {
    const auto& t = transfer_;
    std::vector<Row> rows;
    std::wstring section;
    const auto add = [&](Row row) {
        if (!section.empty()) { row.section = std::move(section); section.clear(); }
        rows.push_back(std::move(row));
    };
    // The job (with its bar and Anuluj), the staged import (Uruchom ponownie) or the last result.
    if (t.job == TJob::exporting || t.job == TJob::staging || t.job == TJob::reading_playnite) {
        Row row{Id::transfer_status, Control::action,
                t.job == TJob::exporting ? tr(L"Tworzenie kopii…") : t.job == TJob::staging ? tr(L"Przygotowanie importu…") : tr(L"Wczytywanie biblioteki Playnite…"),
                t.total ? size(t.done) + L" / " + size(t.total) : std::wstring{},
                t.job == TJob::reading_playnite ? std::wstring{} : std::wstring(tr(L"Anuluj"))};
        row.progress = t.total ? static_cast<float>(static_cast<double>(t.done) / static_cast<double>(t.total)) : 0.0f;
        row.locked = t.job == TJob::reading_playnite;
        add(std::move(row));
    } else if (t.restart_ready) {
        add({Id::transfer_status, Control::action, tr(L"Import gotowy"),
             tr(L"Wczyta się przy następnym starcie Launchme. Pliki, które zmieni, trafią do folderu backups."), tr(L"Uruchom ponownie")});
    } else if (!t.status.empty()) {
        Row row{Id::transfer_status, Control::action, t.status_error ? tr(L"Nie udało się") : tr(L"Gotowe"), t.status, {}};
        row.locked = true; row.lines = 3;
        add(std::move(row));
    }
    const bool busy = t.job != TJob::none && t.job != TJob::measuring;

    if (settings_state_.at(Page::transfer_import) && t.manifest) {
        const auto& m = *t.manifest;
        Row info{Id::transfer_status, Control::action, t.zip.filename().wstring(),
                 (m.app_version.empty() ? std::wstring(L"Launchme") : L"Launchme " + text_encoding::wideLenient(m.app_version)) + L" · " + (m.system == "linux" ? L"Linux" : L"Windows") + L" · " +
                     text_encoding::wideLenient(m.created.substr(0, 10)), {}};
        info.locked = true;
        if (t.job == TJob::none && !t.restart_ready && t.status.empty()) add(std::move(info));
        section = tr(L"Co wczytać");
        bool sealed = false;
        for (const auto& [part, part_info] : m.parts) {
            Row row{Id::transfer_part, Control::toggle, tr(partTitle(part)), tr(partDescription(part)) + std::wstring(L" · ") + amount(part_info.files, part_info.bytes),
                    {}, t.import_parts.contains(part)};
            row.key = profile_transfer::slug(part);
            sealed |= profile_transfer::needsPassword(part) && t.import_parts.contains(part);
            add(std::move(row));
        }
        section = tr(L"Jak wczytać");
        if (sealed) add({Id::transfer_password, Control::secret, tr(L"Hasło kopii"), tr(L"To, które podano przy eksporcie."),
                         t.password.empty() ? tr(L"Wpisz hasło") : tr(L"Hasło wpisane")});
        add({Id::transfer_mode, Control::toggle, tr(L"Zastąp zamiast łączyć"),
             tr(L"Wyłączone: kopia dopisuje to, czego tu brakuje (czas gry - większy, kolekcje - suma). Włączone: kopia zastępuje te części; obecne pliki trafią do folderu backups."),
             {}, t.mode == profile_transfer::Mode::replace});
        add({Id::transfer_paths, Control::text, tr(L"Zmiana ścieżek"),
             tr(L"Gdy dyski lub system są inne: „D:\\ROMs => /home/deck/ROMs; E:\\Gry => F:\\Gry”. Folder danych Launchme przenosi się sam."),
             t.paths.empty() ? std::wstring(tr(L"Brak")) : t.paths});
        Row apply{Id::transfer_apply, Control::action, tr(L"Importuj"), tr(L"Kopia zostanie sprawdzona i przygotowana; Launchme wczyta ją po ponownym uruchomieniu."), tr(L"Importuj")};
        apply.locked = busy || t.import_parts.empty() || (sealed && t.password.empty());
        add(std::move(apply));
        return rows;
    }

    if (settings_state_.at(Page::playnite)) {
        section = tr(L"Biblioteka Playnite");
        const auto found = t.playnite_source.empty() ? playnite::findLibrary() : std::filesystem::path{};
        if (!found.empty() && !t.playnite) add({Id::playnite_find, Control::action, tr(L"Znaleziono Playnite na tym komputerze"), found.wstring(), tr(L"Wczytaj")});
        add({Id::playnite_source, Control::action, tr(L"Folder biblioteki"),
             tr(L"Folder „library” Playnite (np. %AppData%\\Playnite\\library). Zamknij Playnite przed wczytaniem."), tr(L"Wybierz folder…")});
        Row zip{Id::playnite_source, Control::action, tr(L"Kopia zapasowa Playnite"),
                tr(L"Plik ZIP z Playnite (Ustawienia → Kopia zapasowa): także na Linuksie i Steam Decku."), tr(L"Wybierz plik…")};
        zip.key = "zip";
        add(std::move(zip));
        if (!t.playnite) return rows;
        const auto preview = playnite::plan(*t.playnite, t.playnite_options, {account_, gog_state_.account, epic_state_.account}, {});
        std::wstring skipped;
        for (size_t i = 0; i < preview.skipped.size() && i < 6; ++i) skipped += (i ? L", " : L"") + preview.skipped[i];
        if (preview.skipped.size() > 6) skipped += L"…";
        Row summary{Id::transfer_status, Control::action, tr(L"Gry w Playnite: ") + std::to_wstring(t.playnite->games.size()),
                    tr(L"Ze sklepów: ") + std::to_wstring(preview.store) + tr(L" · bez sklepu: ") + std::to_wstring(preview.own) +
                        tr(L" · z emulatorów: ") + std::to_wstring(preview.emulated) + tr(L" · kolekcje: ") + std::to_wstring(preview.collections) +
                        (skipped.empty() ? std::wstring{} : tr(L" · pominięte: ") + skipped), {}};
        summary.locked = true; summary.lines = 3;
        add(std::move(summary));
        section = tr(L"Co przenieść");
        for (const auto& option : playnite_options) {
            Row row{Id::playnite_option, Control::toggle, tr(option.title), tr(option.description), {}, t.playnite_options.*option.flag};
            row.key = option.key;
            add(std::move(row));
        }
        Row apply{Id::playnite_apply, Control::action, tr(L"Importuj z Playnite"),
                  tr(L"Dopisuje do tego, co już jest; nic nie zostanie usunięte. Launchme wczyta import po ponownym uruchomieniu."), tr(L"Importuj")};
        apply.locked = busy;
        add(std::move(apply));
        return rows;
    }

    section = tr(L"Eksport");
    bool sealed = false;
    for (const auto part : profile_transfer::parts) {
        std::wstring description = tr(partDescription(part));
        if (const auto size = t.sizes.find(part); size != t.sizes.end()) description += L" · " + amount(size->second.files, size->second.bytes);
        Row row{Id::transfer_part, Control::toggle, tr(partTitle(part)), std::move(description), {}, t.export_parts.contains(part)};
        row.key = profile_transfer::slug(part);
        sealed |= profile_transfer::needsPassword(part) && t.export_parts.contains(part);
        add(std::move(row));
    }
    if (sealed) add({Id::transfer_password, Control::secret, tr(L"Hasło kopii"),
                     tr(L"Szyfruje klucze, sesje i dane wtyczek. Bez niego kopii nie da się wczytać - zapisz je."),
                     t.password.empty() ? tr(L"Ustaw hasło") : tr(L"Hasło ustawione")});
    Row save{Id::transfer_export, Control::action, tr(L"Eksportuj"), tr(L"Jeden plik launchme_backup_RRRR-MM-DD--GG-MM.zip w wybranym folderze."), tr(L"Zapisz…")};
    save.locked = busy || t.export_parts.empty() || (sealed && t.password.empty());
    add(std::move(save));
    section = tr(L"Import");
    add({Id::transfer_import, Control::action, tr(L"Wczytaj kopię"),
         tr(L"Kopia z tego lub innego komputera, z Windows albo Linuksa. Wybierzesz, które części wczytać."), tr(L"Wybierz plik…")});
    Row playnite{Id::page, Control::page, tr(L"Import z Playnite"),
                 tr(L"Czas gry, ulubione, ukryte, kategorie i tagi jako kolekcje, okładki oraz gry bez sklepu i z emulatorów."), {}};
    playnite.page = Page::playnite;
    playnite.key = std::to_string(static_cast<int>(Page::playnite));
    add(std::move(playnite));
    return rows;
}

void Launcher::transferMeasure() {
    if (!settings_state_.at(Page::transfer) || !transfer_.sizes.empty() || transfer_.job != TJob::none) return;
    transfer_.job = TJob::measuring;
    const auto generation = ++transfer_.generation;
    work_.start([hwnd = hwnd_, alive = alive_, generation] {
        std::map<Part, profile_transfer::PartSize> sizes;
        const auto config = userConfigDirectory();
        for (const auto part : profile_transfer::parts) {
            if (!alive->load()) return;
            sizes[part] = profile_transfer::measure(config, part);
        }
        postToLauncher(hwnd, alive, [generation, sizes = std::move(sizes)](Launcher& app) {
            if (app.transfer_.generation != generation || app.transfer_.job != TJob::measuring) return;
            app.transfer_.job = TJob::none;
            app.transfer_.sizes = sizes;
            if (app.settingsOpen() && app.transferPage()) app.buildSettingsRows();
            InvalidateRect(app.hwnd_, nullptr, FALSE);
        });
    });
}

// The job's bar, changed in place (rebuilding every row many times a second is not needed).
void Launcher::transferProgress(uint32_t generation, uint64_t done, uint64_t total) {
    if (transfer_.generation != generation) return;
    transfer_.done = done; transfer_.total = total;
    if (!settingsOpen() || !transferPage()) return;
    for (auto& row : settings_rows_)
        if (row.id == Id::transfer_status && !row.locked) {
            row.progress = total ? static_cast<float>(static_cast<double>(done) / static_cast<double>(total)) : 0.0f;
            row.description = size(done) + L" / " + size(total);
        }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::transferFinished(uint32_t generation, const profile_transfer::Result& result, bool staged) {
    if (transfer_.generation != generation) return;
    transfer_.job = TJob::none;
    transfer_.cancel.reset();
    transfer_.status_error = !result.ok && !result.cancelled;
    if (result.cancelled) transfer_.status = tr(L"Przerwano.");
    else if (!result.ok) transfer_.status = result.error;
    else if (staged) {
        transfer_.status.clear();
        transfer_.restart_ready = true;
        showNotification(NotificationKind::launch, tr(L"Import gotowy"), tr(L"Uruchom ponownie Launchme, aby go wczytać."));
    } else {
        transfer_.status = tr(L"Kopia zapisana: ") + result.path.wstring() + L" (" + amount(result.files, result.bytes) + L")";
        showNotification(NotificationKind::launch, tr(L"Kopia gotowa"), result.path.filename().wstring());
    }
    if (!result.ok && !result.cancelled) showNotification(NotificationKind::error, tr(L"Kopia i przenoszenie"), result.error);
    if (settingsOpen()) buildSettingsRows();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

namespace {
// A worker's progress, at most ten times a second.
profile_transfer::ProgressFn throttled(HWND hwnd, std::shared_ptr<std::atomic_bool> alive, uint32_t generation) {
    auto last = std::make_shared<std::chrono::steady_clock::time_point>();
    return [hwnd, alive, generation, last](uint64_t done, uint64_t total) {
        const auto now = std::chrono::steady_clock::now();
        if (now - *last < std::chrono::milliseconds(100) && done < total) return;
        *last = now;
        postToLauncher(hwnd, alive, [generation, done, total](Launcher& app) { app.transferProgress(generation, done, total); });
    };
}
}

void Launcher::transferExport(const std::filesystem::path& folder) {
    if (transfer_.job != TJob::none && transfer_.job != TJob::measuring) { navigationBoundary(); return; }
    profile_transfer::ExportInput input;
    input.config = userConfigDirectory();
    input.folder = folder;
    input.parts = transfer_.export_parts;
    input.password = text_encoding::utf8Lenient(transfer_.password);
    input.settings = settings_;
    input.library = library_state_;
    input.app_version = text_encoding::utf8Lenient(diagnostics::version());
    transfer_.job = TJob::exporting;
    transfer_.done = transfer_.total = 0;
    transfer_.status.clear();
    transfer_.cancel = std::make_shared<std::atomic_bool>(false);
    const auto generation = ++transfer_.generation;
    input.progress = throttled(hwnd_, alive_, generation);
    input.cancel = [cancel = transfer_.cancel, alive = alive_] { return cancel->load() || !alive->load(); };
    work_.start([input = std::move(input), hwnd = hwnd_, alive = alive_, generation]() mutable {
        auto result = profile_transfer::exportProfile(input);
        std::fill(input.password.begin(), input.password.end(), '\0');
        postToLauncher(hwnd, alive, [generation, result = std::move(result)](Launcher& app) { app.transferFinished(generation, result, false); });
    });
    if (settingsOpen()) buildSettingsRows();
    audio_.context();
}

void Launcher::transferStage() {
    if (transfer_.job != TJob::none && transfer_.job != TJob::measuring) { navigationBoundary(); return; }
    profile_transfer::ImportInput input;
    input.config = userConfigDirectory();
    input.zip = transfer_.zip;
    input.parts = transfer_.import_parts;
    input.password = text_encoding::utf8Lenient(transfer_.password);
    input.mode = transfer_.mode;
    input.paths = profile_transfer::parsePathMap(transfer_.paths);
    transfer_.job = TJob::staging;
    transfer_.done = transfer_.total = 0;
    transfer_.status.clear();
    transfer_.cancel = std::make_shared<std::atomic_bool>(false);
    const auto generation = ++transfer_.generation;
    input.progress = throttled(hwnd_, alive_, generation);
    input.cancel = [cancel = transfer_.cancel, alive = alive_] { return cancel->load() || !alive->load(); };
    work_.start([input = std::move(input), hwnd = hwnd_, alive = alive_, generation]() mutable {
        auto result = profile_transfer::stageImport(input);
        std::fill(input.password.begin(), input.password.end(), '\0');
        postToLauncher(hwnd, alive, [generation, result = std::move(result)](Launcher& app) { app.transferFinished(generation, result, true); });
    });
    if (settingsOpen()) buildSettingsRows();
    audio_.context();
}

void Launcher::transferPickBackup() {
    file_browser::Request request;
    request.mode = file_browser::Mode::file;
    request.title = tr(L"Kopia Launchme do wczytania");
    request.extensions = {L".zip"};
    request.filter_label = L"ZIP";
    request.native = [this, title = request.title] { return pickPath(hwnd_, false, title, {L".zip"}, L"ZIP", false, {}); };
    openFileBrowser(std::move(request), [this](const std::filesystem::path& zip) {
        if (zip.empty()) return;
        std::wstring error;
        auto manifest = profile_transfer::inspect(zip, error);
        if (!manifest) { showNotification(NotificationKind::error, tr(L"To nie jest kopia Launchme"), error); return; }
        transfer_.zip = zip;
        transfer_.import_parts.clear();
        for (const auto& [part, info] : manifest->parts) if (part != Part::artwork_cache) transfer_.import_parts.insert(part);
        if (transfer_.import_parts.empty() && !manifest->parts.empty()) transfer_.import_parts.insert(manifest->parts.front().first);
        transfer_.manifest = std::move(manifest);
        transfer_.mode = profile_transfer::Mode::merge;
        transfer_.status.clear();
        openSettingsPage(Page::transfer_import);
    });
}

void Launcher::transferReadPlaynite(const std::filesystem::path& source) {
    if (source.empty() || (transfer_.job != TJob::none && transfer_.job != TJob::measuring)) return;
    transfer_.job = TJob::reading_playnite;
    transfer_.done = transfer_.total = 0;
    transfer_.status.clear();
    transfer_.playnite.reset();
    transfer_.playnite_source = source;
    const auto generation = ++transfer_.generation;
    work_.start([source, hwnd = hwnd_, alive = alive_, generation] {
        std::wstring error;
        auto library = playnite::read(source, userConfigDirectory() / L"playnite-scratch", error);
        std::shared_ptr<const playnite::Library> shared = library ? std::make_shared<const playnite::Library>(std::move(*library)) : nullptr;
        postToLauncher(hwnd, alive, [generation, shared, error](Launcher& app) {
            auto& t = app.transfer_;
            if (t.generation != generation) return;
            t.job = TJob::none;
            t.playnite = shared;
            t.status_error = !shared;
            t.status = shared ? std::wstring{} : error;
            if (!shared) app.showNotification(NotificationKind::error, tr(L"Nie udało się wczytać Playnite"), error);
            if (app.settingsOpen()) app.buildSettingsRows();
            InvalidateRect(app.hwnd_, nullptr, FALSE);
        });
    });
    if (settingsOpen()) buildSettingsRows();
}

void Launcher::transferStagePlaynite() {
    if (!transfer_.playnite || (transfer_.job != TJob::none && transfer_.job != TJob::measuring)) { navigationBoundary(); return; }
    const auto config = userConfigDirectory();
    auto plan = playnite::plan(*transfer_.playnite, transfer_.playnite_options, {account_, gog_state_.account, epic_state_.account},
                               custom_games::load(config / L"custom-games.json").games);
    transfer_.job = TJob::staging;
    transfer_.done = 0; transfer_.total = 0;
    transfer_.status.clear();
    const auto generation = ++transfer_.generation;
    work_.start([plan = std::move(plan), config, hwnd = hwnd_, alive = alive_, generation] {
        profile_transfer::Result result;
        result.ok = playnite::stage(plan, config, result.error);
        result.files = plan.games.size() + plan.pictures.size();
        std::error_code ec;
        std::filesystem::remove_all(config / L"playnite-scratch", ec);
        postToLauncher(hwnd, alive, [generation, result = std::move(result)](Launcher& app) {
            app.transfer_.playnite.reset(); // Its pictures went with the scratch folder.
            app.transferFinished(generation, result, true);
        });
    });
    if (settingsOpen()) buildSettingsRows();
    audio_.context();
}

bool Launcher::transferSetting(const Row& row) {
    auto& t = transfer_;
    const bool busy = t.job != TJob::none && t.job != TJob::measuring;
    switch (row.id) {
    case Id::transfer_part: {
        const auto part = profile_transfer::partFromSlug(row.key);
        if (!part) return true;
        auto& set = settings_state_.at(Page::transfer_import) ? t.import_parts : t.export_parts;
        if (!set.erase(*part)) set.insert(*part);
        break;
    }
    case Id::transfer_password:
        settingsTextRow(t.password, [this](const std::wstring& value) { transfer_.password = value; return true; }, false);
        if (!settings_state_.editing) buildSettingsRows();
        return true;
    case Id::transfer_paths:
        settingsTextRow(t.paths, [this](const std::wstring& value) { transfer_.paths = value; return true; });
        if (!settings_state_.editing) buildSettingsRows();
        return true;
    case Id::transfer_mode:
        t.mode = t.mode == profile_transfer::Mode::merge ? profile_transfer::Mode::replace : profile_transfer::Mode::merge;
        break;
    case Id::transfer_export: {
        if (row.locked) { navigationBoundary(); return true; }
        file_browser::Request request;
        request.mode = file_browser::Mode::folder;
        request.title = tr(L"Gdzie zapisać kopię?");
        request.native = [this, title = request.title] { return pickPath(hwnd_, true, title, {}, {}, false, {}); };
        openFileBrowser(std::move(request), [this](const std::filesystem::path& folder) { if (!folder.empty()) transferExport(folder); });
        return true;
    }
    case Id::transfer_import:
        if (busy) { navigationBoundary(); return true; }
        transferPickBackup();
        return true;
    case Id::transfer_apply:
        if (row.locked) { navigationBoundary(); return true; }
        transferStage();
        return true;
    case Id::transfer_status:
        if (row.locked) navigationBoundary();
        else if (busy && t.cancel) t.cancel->store(true);
        else if (t.restart_ready) restartForImport();
        else navigationBoundary();
        return true;
    case Id::playnite_find:
        transferReadPlaynite(playnite::findLibrary());
        return true;
    case Id::playnite_source: {
        if (busy) { navigationBoundary(); return true; }
        file_browser::Request request;
        const bool zip = row.key == "zip";
        request.mode = zip ? file_browser::Mode::file : file_browser::Mode::folder;
        request.title = zip ? tr(L"Kopia zapasowa Playnite") : tr(L"Folder biblioteki Playnite");
        if (zip) { request.extensions = {L".zip"}; request.filter_label = L"ZIP"; }
        request.native = [this, zip, title = request.title] {
            return zip ? pickPath(hwnd_, false, title, {L".zip"}, L"ZIP", false, {}) : pickPath(hwnd_, true, title, {}, {}, false, {});
        };
        openFileBrowser(std::move(request), [this](const std::filesystem::path& source) { transferReadPlaynite(source); });
        return true;
    }
    case Id::playnite_option:
        for (const auto& option : playnite_options)
            if (row.key == option.key) t.playnite_options.*option.flag = !(t.playnite_options.*option.flag);
        break;
    case Id::playnite_apply:
        if (row.locked) { navigationBoundary(); return true; }
        transferStagePlaynite();
        return true;
    default: return false;
    }
    buildSettingsRows();
    audio_.context();
    InvalidateRect(hwnd_, nullptr, FALSE);
    return true;
}

// The first start after an import: what went in (profile_transfer::applyPending ran in main).
void Launcher::announceImport() {
    const auto applied = profile_transfer::takeApplied(userConfigDirectory());
    if (!applied) return;
    if (applied->ok) showNotification(NotificationKind::launch, tr(L"Import wczytany"), tr(L"Pliki: ") + std::to_wstring(applied->files));
    else showNotification(NotificationKind::error, tr(L"Import nie powiódł się"), applied->error);
}
