// Aurelia's management pages (src/ui/aurelia_view.hpp): setup, a game's management and its cloud,
// DLC, versions, Workshop and library pages. One modal for Tryb TV and the desktop window; the
// friends list and chat live in the drawer instead (src/app/friend_chat.cpp).
#include "launcher.hpp"
#include "../platform/text_encoding.hpp"
#include "qrcodegen.hpp"
#include <algorithm>
#include <atomic>
#include <sstream>

namespace {
using C = aurelia::Command;
using A = aurelia_ui::Action;
using P = aurelia_ui::Page;
using Row = aurelia_ui::Row;
using text_encoding::wideLenient;
const std::vector<json::Value>& array(const json::Value& value, const char* key = nullptr) {
    static const std::vector<json::Value> empty;
    const auto* v = key ? value.get(key) : &value;
    return v && v->type == json::Value::Type::array ? v->elements : empty;
}
std::wstring sizeText(uint64_t bytes) {
    wchar_t buffer[48]{};
    const double gb = static_cast<double>(bytes) / (1024.0 * 1024 * 1024);
    if (gb >= 1000) swprintf_s(buffer, L"%.1f TB", gb / 1024); else swprintf_s(buffer, L"%.0f GB", gb);
    return buffer;
}
// Aurelia reports canonical paths (\\?\C:\…); shown and passed on as the plain path.
std::wstring plainPath(std::wstring path) {
    if (path.starts_with(L"\\\\?\\UNC\\")) path = L"\\\\" + path.substr(8);
    else if (path.starts_with(L"\\\\?\\")) path.erase(0, 4);
    return path;
}
// A Steam library holds games or was made by Steam: a stray steamapps folder with only a copied
// libraryfolders.vdf (an old install's parent folder) is not one.
bool steamLibrary(const std::filesystem::path& root) {
    std::error_code error;
    if (std::filesystem::exists(root / L"libraryfolder.vdf", error) || std::filesystem::is_directory(root / L"steamapps" / L"common", error)) return true;
    for (std::filesystem::directory_iterator item(root / L"steamapps", std::filesystem::directory_options::skip_permission_denied, error), end;
         !error && item != end; item.increment(error))
        if (item->path().filename().wstring().starts_with(L"appmanifest_")) return true;
    return false;
}
}

// A game Aurelia manages: installed, updated, uninstalled and launched through it (without the
// Steam client). Every other Steam game keeps the Steam client for all of that. Per game: chosen
// (aurelia_standalone), or with "Domyślnie zarządzaj przez Aurelię" every game but the ones chosen
// for Steam (aurelia_steam).
bool Launcher::aureliaManaged(const Game& game) const {
    if (!aurelia_active_ || !aurelia::validGame(game)) return false;
    // A borrowed game needs the Steam client to authorise its licence: with one here, it keeps it.
    if (game.family_shared && !aurelia_library_source_) return false;
    if (aurelia_library_source_ && !aurelia_steam_client_) return true; // No Steam client: every game through Aurelia.
    const auto id = std::to_string(game.app_id);
    return settings_.aurelia_default ? !settings_.aurelia_steam.contains(id) : settings_.aurelia_standalone.contains(id);
}
bool Launcher::aureliaCloudEnabled() const { return aurelia_active_ && settings_.aurelia_cloud; }
bool Launcher::aureliaCloudFor(const Game& game) const { return aureliaCloudEnabled() && aureliaManaged(game); }

int Launcher::aureliaVisibleRows() const {
    if (aurelia_ui_.visible > 0) return aurelia_ui_.visible;
    const auto v = renderer_.viewport();
    return aurelia_ui::Layout{v.width, v.height}.visible(aurelia_ui_);
}

void Launcher::openAurelia(const Game* game, bool intro) {
    if (game && (!aurelia_active_ || !aurelia::validGame(*game))) return;
    const auto copy = game ? *game : Game{};
    dismissMenu(); closeQuick(); closeFriends(); closeKeyboard(); closeCategoryMenu(); cancelEnterHold(); cancelTabHold();
    desktop_menu_.reset();
    aurelia_ui_ = {}; aurelia_ui_.open = true; aurelia_ui_.intro = intro; aurelia_ui_.game = copy;
    aureliaPage(game ? P::game : P::setup);
    audio_.context();
}
// Odinstaluj of a game Aurelia manages (the game's menu): its question alone, then closed.
void Launcher::aureliaUninstall(const Game& game) {
    openAurelia(&game);
    if (!aurelia_ui_.open) return;
    aurelia_ui_.confirmation = Row{tr(L"Odinstalować grę?"), tr(L"Zapisy w chmurze pozostają."), {}, A::uninstall};
    aurelia_ui_.confirm_only = true; aurelia_ui_.confirm_focus = false;
    InvalidateRect(hwnd_, nullptr, FALSE);
}
void Launcher::aureliaAnswer(bool yes) {
    auto& s = aurelia_ui_; if (!s.open || !s.confirmation) return;
    const auto row = *s.confirmation; const bool only = s.confirm_only;
    s.confirmation.reset(); s.confirm_only = false;
    if (yes) aureliaActivate(row, true);
    if (only) closeAurelia();
    InvalidateRect(hwnd_, nullptr, FALSE);
}
void Launcher::closeAurelia() {
    if (!aurelia_ui_.open) return;
    if (aurelia_ui_.confirmation) { aureliaAnswer(false); return; }
    const bool intro = aurelia_ui_.intro;
    aureliaCancelLogin(); // Closing the page ends a sign-in in progress.
    ++aurelia_page_generation_; closeKeyboard();
    aurelia_ui_ = {};
    if (intro) { settings_.aurelia_intro_done = true; settings_dirty_ = true; saveSettings(); if (!desktop_mode_ && !settings_.tour_done) startTour(); }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::aureliaRows() {
    auto& s = aurelia_ui_; if (!s.open) return;
    s.enabled = settings_.aurelia; s.active = aurelia_active_; s.installed = !aurelia_version_.empty(); s.authenticated = aurelia_authenticated_;
    s.rows.clear(); s.input_hint.clear(); s.submit.clear(); s.input_secret = false; s.title = L"Aurelia"; s.subtitle = s.game.name;
    const auto add = [&](A action, std::wstring title, std::wstring value = {}, uint64_t id = 0, std::wstring payload = {}) -> Row& {
        Row row{std::move(title), {}, std::move(value), action, id, std::move(payload)};
        row.page = aurelia_ui::opensPage(action);
        s.rows.push_back(std::move(row)); return s.rows.back();
    };
    const auto toggle = [&](A action, std::wstring title, bool on) -> Row& {
        auto& row = add(action, std::move(title)); row.toggle = true; row.checked = on; return row;
    };
    const auto& data = aurelia_page_data_;
    const auto cloud = aurelia_cloud_.find(s.game.app_id);
    const auto cloud_state = cloud == aurelia_cloud_.end() ? aurelia::CloudState::unknown : cloud->second.state;
    switch (s.page) {
    case P::setup: {
        s.subtitle.clear();
        s.status = aurelia_setup_status_;
        if (s.login != aurelia_ui::State::Login::none) {
            // The sign-in page: the QR code above, the account's name and then its password in the
            // field (a Steam Guard code when Steam asks), Aurelia's own console and the way out.
            using L = aurelia_ui::State::Login;
            if (s.login == L::form) {
                const bool password = !s.login_account.empty();
                s.input_hint = password ? tr(L"Hasło") : tr(L"Nazwa konta Steam");
                s.submit = password ? tr(L"Zaloguj") : tr(L"Dalej");
                s.input_secret = password;
                if (s.qr_failed) add(A::login_refresh, tr(L"Nowy kod QR"));
                add(A::login, tr(L"Zaloguj w oknie Aurelii"));
            } else if (s.login == L::guard) {
                s.input_hint = s.guard_email ? tr(L"Kod Steam Guard z e-maila") : tr(L"Kod Steam Guard"); s.submit = tr(L"Wyślij");
            }
            add(A::login_cancel, tr(L"Anuluj logowanie"));
            break;
        }
        // The account first: who is signed in, or the way to sign in.
        if (aurelia_authenticated_) {
            auto& account = add(A::reconnect, tr(L"Konto Steam"), aurelia_account_name_.empty() ? std::wstring(tr(L"Zalogowano")) : aurelia_account_name_);
            account.description = tr(L"Zalogowano · połącz ponownie");
            account.locked = s.busy;
            add(A::logout, tr(L"Wyloguj")).danger = true;
        } else {
            auto& sign_in = add(A::login_start, tr(L"Zaloguj się"),
                                !aurelia_transport_ ? std::wstring(tr(L"Najpierw zainstaluj Aurelię")) : aurelia_session_known_ ? std::wstring(tr(L"Nie zalogowano")) : std::wstring(tr(L"Stan nieznany")));
            sign_in.description = tr(L"Kod QR z aplikacji Steam Mobile albo nazwa konta i hasło");
            sign_in.locked = !aurelia_transport_;
        }
        toggle(A::toggle, tr(L"Rozszerzona integracja Aurelia"), settings_.aurelia);
        toggle(A::cloud_toggle, tr(L"Zapisy w chmurze"), settings_.aurelia_cloud).locked = !settings_.aurelia;
        {
            auto& replaces = toggle(A::default_launch, tr(L"Aurelia jako zamiennik Steam"), settings_.aurelia_default);
            replaces.description = tr(L"Konto, gry, kolekcje i czas gry z Aurelii; gry bez klienta Steam.");
            replaces.locked = !settings_.aurelia;
        }
        add(A::helper, s.installed ? tr(L"Sprawdź aktualizację") : tr(L"Zainstaluj Aurelię"),
            aurelia_helper_busy_ ? tr(L"Pobieranie…") : aurelia_version_).locked = aurelia_helper_busy_;
        if (aurelia_authenticated_ && s.installed) {
            auto& duplicates = add(A::duplicates, tr(L"Sprawdź zdublowane instalacje"));
            duplicates.description = tr(L"Gry zainstalowane w kilku bibliotekach naraz; nic nie jest usuwane.");
            duplicates.locked = s.busy;
        }
        add(A::repository, tr(L"Repozytorium Aurelii"), L"GitHub");
        if (s.intro) add(A::close, tr(L"Dalej"));
        break;
    }
    case P::game: {
        s.title = tr(L"Zarządzaj · Aurelia");
        // The choice first; a game the Steam client manages has nothing else here.
        if (settings_.aurelia_default)
            toggle(A::steam, tr(L"Zarządzaj przez Steam"), settings_.aurelia_steam.contains(std::to_string(s.game.app_id)));
        else toggle(A::steam, tr(L"Zarządzaj przez Aurelię"), settings_.aurelia_standalone.contains(std::to_string(s.game.app_id)));
        const auto transfer = aurelia_downloads_.find(s.game.app_id);
        const bool downloading = transfer != aurelia_downloads_.end();
        // Its transfer: running (pause, cancel) or waiting in Aurelia's backlog (resume, forget).
        const auto transferRows = [&] {
            if (transfer->second.backlog) {
                add(A::resume, tr(L"Wznów pobieranie"));
                add(A::forget, tr(L"Usuń z kolejki"));
                add(A::forget_files, tr(L"Usuń z kolejki wraz z pobranymi plikami")).danger = true;
            } else {
                add(A::pause, tr(L"Wstrzymaj pobieranie"));
                add(A::cancel, tr(L"Anuluj pobieranie"));
            }
        };
        if (!aureliaManaged(s.game)) { if (downloading) transferRows(); break; }
        if (settings_.aurelia_cloud) add(A::cloud, tr(L"Zapisy w chmurze"), aurelia::cloudShort(cloud_state));
        if (s.game.installed) {
            add(A::dlc, L"DLC");
            add(A::versions, tr(L"Wersje"), s.pinned == true ? tr(L"Przypięta") : L"");
            add(A::workshop, L"Steam Workshop");
            if (downloading) transferRows();
            else {
                add(A::update, tr(L"Aktualizuj"));
                add(A::verify, tr(L"Sprawdź pliki"));
                add(A::move, tr(L"Przenieś do innej biblioteki"));
                add(A::relink, tr(L"Wskaż przeniesione pliki")).description = tr(L"Steam znajdzie grę w innej bibliotece, bez kopiowania.");
            }
            add(A::uninstall, tr(L"Odinstaluj")).danger = true;
        } else if (downloading) transferRows();
        else {
            add(A::install, tr(L"Zainstaluj"));
            add(A::import_files, tr(L"Zarejestruj istniejące pliki")).description = tr(L"Gra już leży w bibliotece Steam; nic nie zostanie pobrane.");
        }
        break;
    }
    case P::cloud: {
        s.title = tr(L"Zapisy w chmurze");
        add(A::sync, tr(L"Synchronizuj teraz"), aurelia::cloudShort(cloud_state)).locked = cloud_state == aurelia::CloudState::syncing;
        if (cloud_state == aurelia::CloudState::conflicts) {
            auto& from_cloud = add(A::resolve_cloud, tr(L"Użyj zapisów z chmury")); from_cloud.danger = true;
            from_cloud.description = tr(L"Zastępuje konfliktujące zapisy lokalne.");
            auto& from_local = add(A::resolve_local, tr(L"Użyj zapisów lokalnych")); from_local.danger = true;
            from_local.description = tr(L"Zastępuje konfliktujące zapisy w chmurze.");
        }
        toggle(A::auto_cloud, tr(L"Synchronizuj przed grą i po niej"), !settings_.aurelia_cloud_skip.contains(std::to_string(s.game.app_id)));
        break;
    }
    case P::dlc:
        s.title = L"DLC";
        if (const auto list = aurelia::dlcs(data)) for (const auto& d : *list) {
            const auto action = d.owned != true ? A::none : d.installed != true ? A::install : d.disabled == true ? A::enable_dlc : A::disable_dlc;
            auto& row = add(action, d.name, action == A::install ? tr(L"Zainstaluj") : action == A::enable_dlc ? tr(L"Wyłączony") :
                            action == A::disable_dlc ? tr(L"Włączony") : d.owned == false ? tr(L"Nie posiadasz") : L"", d.app);
            row.locked = !d.owned.has_value() || !d.installed.has_value() || !d.disabled.has_value() || d.owned == false;
        }
        break;
    case P::versions:
        s.title = tr(L"Wersje");
        if (s.pinned != true) add(A::pin, tr(L"Przypnij obecną wersję"));
        if (s.pinned != false) add(A::unpin, tr(L"Odblokuj aktualizacje"));
        add(A::branches, tr(L"Gałęzie beta"));
        for (const auto& m : array(data)) {
            const auto depot = aurelia::integer(m.get("depot_id")).value_or(0), manifest = aurelia::integer(m.get("manifest_id")).value_or(0);
            auto name = wideLenient(aurelia::text(m, "depot_name"));
            if (const auto branch = wideLenient(aurelia::text(m, "branch")); !branch.empty()) name += L" · " + branch;
            add(A::none, name, std::to_wstring(depot) + L" : " + std::to_wstring(manifest));
        }
        s.input_hint = L"depot_id manifest_id"; s.submit = tr(L"Zainstaluj"); break;
    case P::branches:
        s.title = tr(L"Gałęzie beta");
        for (const auto& b : array(data, "branches")) if (b.type == json::Value::Type::string)
            add(A::set_branch, wideLenient(b.text), {}, 0, wideLenient(b.text));
        break;
    case P::libraries: {
        s.title = aureliaLibraryTitle();
        for (const auto& l : array(data, "libraries")) {
            const auto path = wideLenient(aurelia::text(l, "path"));
            const auto free = aurelia::integer(l.get("free_bytes"));
            add(A::choose_library, path, free ? sizeText(*free) + tr(L" wolne") : L"", 0, path);
        }
        add(A::browse_library, tr(L"Inny folder…"));
        break;
    }
    case P::workshop:
        s.title = L"Steam Workshop";
        for (const auto& item : array(data, "items")) {
            const auto id = aurelia::integer(item.get("id")).value_or(0);
            if (!id) continue;
            add(A::workshop_subscribe, wideLenient(aurelia::text(item, "title")), tr(L"Subskrybuj"), id);
        }
        s.input_hint = tr(L"ID przedmiotu Workshop"); s.submit = tr(L"Zainstaluj"); break;
    }
    if (s.input_hint.empty()) s.input_focus = false;
    const int visible = aureliaVisibleRows();
    s.focus = std::clamp(s.focus, 0, std::max(0, static_cast<int>(s.rows.size()) - 1));
    s.first = std::clamp(s.first, 0, std::max(0, static_cast<int>(s.rows.size()) - visible));
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::aureliaPage(P page) {
    closeKeyboard();
    auto& s = aurelia_ui_; const auto generation = ++aurelia_page_generation_;
    s.page = page; s.focus = s.first = 0; s.input_focus = false; s.back_focus = false; s.input.clear(); s.confirmation.reset(); s.status.clear(); s.busy = false;
    s.hover = -9; s.visible = 0;
    aurelia_page_data_ = {}; aureliaRows();
    if (!aurelia_transport_ || (page != P::setup && !aurelia_authenticated_)) return;
    aurelia::Request request; request.app = s.game.app_id;
    switch (page) {
    case P::setup: request.command = C::health; break;
    case P::game: request.command = C::available; break;
    case P::cloud: aureliaCheckCloud(s.game, true); return; // Its state comes from the shared cloud map.
    case P::dlc: request.command = C::dlc; break;
    case P::versions: request.command = C::manifests; break;
    case P::branches: request.command = C::branches; break;
    case P::workshop: request.command = C::workshop_browse; break;
    case P::libraries: request.command = C::libraries; break;
    }
    s.busy = true; s.status = tr(L"Wczytywanie…");
    aureliaRequest(request, [generation, page](Launcher& app, aurelia::Reply reply) {
        if (!app.aurelia_ui_.open || app.aurelia_page_generation_ != generation) return;
        auto& s = app.aurelia_ui_; s.busy = false; s.status = reply.error;
        if (reply.ok) {
            app.aurelia_page_data_ = reply.data;
            if (page == P::setup) {
                app.aurelia_authenticated_ = aurelia::flag(reply.data, "logged_in"); app.aurelia_session_known_ = true;
                app.aurelia_account_name_ = wideLenient(aurelia::text(reply.data, "account"));
                app.aurelia_setup_status_ = app.settings_.aurelia != app.aurelia_active_ ? tr(L"Wymaga ponownego uruchomienia Launchme") : L"";
            } else if (page == P::game) {
                s.game.installed = aurelia::flag(reply.data, "available");
                if (const auto* pinned = reply.data.get("pinned"); pinned && pinned->type == json::Value::Type::boolean) s.pinned = pinned->flag;
            } else if (page == P::libraries) {
                // Plain paths, Steam's own libraries only, and the free space Aurelia could not tell.
                const auto entry = app.aurelia_page_data_.properties.find("libraries");
                auto* list = entry == app.aurelia_page_data_.properties.end() ? nullptr : &entry->second;
                if (list && list->type == json::Value::Type::array) {
                    std::vector<json::Value> kept;
                    for (auto item : list->elements) {
                        const auto path = plainPath(wideLenient(aurelia::text(item, "path")));
                        if (path.empty()) continue;
                        item.properties["path"] = json::string(text_encoding::utf8(path));
                        ULARGE_INTEGER free{};
                        if (!aurelia::integer(item.get("free_bytes")) && GetDiskFreeSpaceExW(path.c_str(), &free, nullptr, nullptr))
                            item.properties["free_bytes"] = json::string(std::to_string(free.QuadPart));
                        if (steamLibrary(path)) kept.push_back(std::move(item));
                    }
                    if (!kept.empty()) list->elements = std::move(kept);
                    else for (auto& item : list->elements) item.properties["path"] = json::string(text_encoding::utf8(plainPath(wideLenient(aurelia::text(item, "path")))));
                }
            }
        } else if (page == P::setup) { app.aurelia_setup_status_ = reply.error; app.aurelia_session_known_ = false; }
        app.aureliaRows();
    });
}

const wchar_t* Launcher::aureliaLibraryTitle() const {
    switch (aurelia_library_action_) {
    case A::move: return tr(L"Przenieś do innej biblioteki");
    case A::relink: return tr(L"Biblioteka, do której przeniesiono pliki");
    case A::import_files: return tr(L"Biblioteka z plikami gry");
    default: return tr(L"Wybierz bibliotekę Steam");
    }
}

void Launcher::aureliaBrowseLibrary() {
    file_browser::Request request;
    request.mode = file_browser::Mode::folder;
    request.title = aureliaLibraryTitle();
    request.native = [this, title = request.title] { return pickPath(hwnd_, true, title, {}, {}, false, {}); };
    const auto app_id = aurelia_ui_.game.app_id, generation = aurelia_page_generation_;
    openFileBrowser(std::move(request), [this, app_id, generation](const std::filesystem::path& folder) {
        auto& s = aurelia_ui_;
        if (!s.open || s.page != P::libraries || s.game.app_id != app_id || aurelia_page_generation_ != generation || !folder.is_absolute()) return;
        aureliaActivate({{}, {}, {}, A::choose_library, 0, folder.wstring()});
    });
}

// By value: some actions rebuild aurelia_ui_.rows, which the callers' row may point into.
void Launcher::aureliaActivate(Row row, bool confirmed) {
    auto& s = aurelia_ui_; if (!s.open || row.locked || row.action == A::none) return;
    if (row.action == A::close) { closeAurelia(); return; }
    if (row.action == A::toggle) { toggleAurelia(); return; }
    if (row.action == A::cloud_toggle || row.action == A::default_launch) {
        auto& value = row.action == A::cloud_toggle ? settings_.aurelia_cloud : settings_.aurelia_default;
        value = !value; settings_dirty_ = true; saveSettings();
        if (row.action == A::cloud_toggle && !value) aurelia_cloud_.clear(); // No status is shown or checked while off.
        if (settingsOpen()) buildSettingsRows();
        aureliaRows(); audio_.context(); return;
    }
    if (row.action == A::helper) { installAurelia(); return; }
    if (row.action == A::login_cancel) { aureliaCancelLogin(); audio_.context(); return; }
    if (row.action == A::login_start) { aureliaOpenSignIn(); return; }
    if (row.action == A::login_refresh) { aureliaLoginProcess({C::login_qr}); audio_.context(); return; }
    if (row.action == A::login) aureliaCancelLogin(); // Aurelia's own console instead: the page's sign-in stops.
    if (row.action == A::repository) {
        execute_(hwnd_, GameAction{L"https://github.com/Drackrath/Aurelia", {}, {}, ActionKind::open_client}); return;
    }
    if (s.busy && row.action != A::cancel && row.action != A::pause && !confirmed) return;
    switch (row.action) {
    case A::cloud: aureliaPage(P::cloud); return;
    case A::dlc: aureliaPage(P::dlc); return;
    case A::versions: aureliaPage(P::versions); return;
    case A::branches: aureliaPage(P::branches); return;
    case A::workshop: aureliaPage(P::workshop); return;
    case A::refresh: aureliaPage(s.page); aureliaRescan(); return;
    case A::browse_library: aureliaBrowseLibrary(); return;
    case A::steam: {
        const auto id = std::to_string(s.game.app_id);
        auto& games = settings_.aurelia_default ? settings_.aurelia_steam : settings_.aurelia_standalone;
        if (!games.erase(id)) games.insert(id);
        settings_dirty_ = true; saveSettings(); aureliaRows(); return;
    }
    case A::auto_cloud: {
        const auto id = std::to_string(s.game.app_id);
        if (!settings_.aurelia_cloud_skip.erase(id)) settings_.aurelia_cloud_skip.insert(id);
        settings_dirty_ = true; saveSettings(); aureliaRows(); return;
    }
    case A::install: if (s.page != P::dlc) { aurelia_library_action_ = A::install; aureliaPage(P::libraries); return; } break;
    case A::move: aurelia_library_action_ = A::move; aureliaPage(P::libraries); return;
    case A::relink: aurelia_library_action_ = A::relink; aureliaPage(P::libraries); return;
    case A::import_files: aurelia_library_action_ = A::import_files; aureliaPage(P::libraries); return;
    case A::duplicates: aureliaReportDuplicates(); return;
    default: break;
    }
    const bool destructive = row.action == A::uninstall || row.action == A::downgrade || row.action == A::resolve_cloud || row.action == A::resolve_local || row.action == A::logout || row.action == A::forget_files;
    if (destructive && !confirmed) {
        auto question = row;
        if (row.action == A::uninstall) { question.title = tr(L"Odinstalować grę?"); question.description = tr(L"Zapisy w chmurze pozostają."); }
        else if (row.action == A::forget_files) { question.title = tr(L"Usunąć pobrane pliki?"); question.description = tr(L"Częściowo pobrane pliki tej gry zostaną skasowane."); }
        else if (row.action == A::logout) { question.title = tr(L"Wylogować z Aurelii?"); question.description.clear(); }
        s.confirmation = question; s.input_focus = false; s.confirm_focus = false; s.hover = -9;
        closeKeyboard(); InvalidateRect(hwnd_, nullptr, FALSE); return;
    }
    aurelia::Request request; request.app = s.game.app_id;
    switch (row.action) {
    case A::login: request.command = C::login; break;
    case A::logout: request.command = C::logout; break;
    case A::reconnect: request.command = C::reconnect; break;
    case A::sync: case A::resolve_cloud: case A::resolve_local:
        request.command = C::cloud_sync; request.text = row.action == A::resolve_cloud ? L"cloud" : row.action == A::resolve_local ? L"local" : L""; break;
    case A::install: request.command = C::install; request.app = static_cast<uint32_t>(row.id); break;
    case A::choose_library:
        request.command = aurelia_library_action_ == A::move ? C::move : aurelia_library_action_ == A::relink ? C::relink :
                          aurelia_library_action_ == A::import_files ? C::import_files : C::install;
        request.folder = plainPath(row.payload); break;
    case A::update: request.command = C::update; break;
    case A::verify: request.command = C::verify; break;
    case A::cancel: request.command = C::cancel_install; break;
    case A::pause: request.command = C::install_pause; break;
    case A::resume: request.command = C::install_resume; break;
    case A::forget: case A::forget_files: request.command = C::install_forget; request.delete_files = row.action == A::forget_files; break;
    case A::uninstall: request.command = C::uninstall; break;
    case A::enable_dlc: request.command = C::enable_dlc; request.app = static_cast<uint32_t>(row.id); break;
    case A::disable_dlc: request.command = C::disable_dlc; request.app = static_cast<uint32_t>(row.id); break;
    case A::pin: request.command = C::pin; break;
    case A::unpin: request.command = C::unpin; break;
    case A::downgrade: {
        std::wistringstream values(row.payload); std::wstring depot, manifest, extra; values >> depot >> manifest;
        const auto d = json::string(text_encoding::utf8(depot)), m = json::string(text_encoding::utf8(manifest));
        const auto di = aurelia::integer(&d), mi = aurelia::integer(&m);
        if (!di || !*di || *di > UINT32_MAX || !mi || !*mi || (values >> extra)) { s.status = tr(L"Wpisz poprawny depot_id i manifest_id"); InvalidateRect(hwnd_, nullptr, FALSE); return; }
        request.command = C::downgrade; request.depot = static_cast<uint32_t>(*di); request.id = *mi; break;
    }
    case A::set_branch: request.command = C::set_branch; request.text = row.payload; break;
    case A::workshop_install: request.command = C::workshop_install; request.id = row.id; break;
    case A::workshop_subscribe: request.command = C::workshop_subscribe; request.id = row.id; break;
    case A::workshop_unsubscribe: request.command = C::workshop_unsubscribe; request.id = row.id; break;
    default: return;
    }
    s.confirmation.reset();
    if (row.action == A::login || row.action == A::logout || row.action == A::reconnect) {
        if (!aurelia_operations_.empty() || !aurelia_playing_.empty() || !aurelia_downloads_.empty()) { s.status = tr(L"Najpierw zakończ gry i pobierania Aurelii"); InvalidateRect(hwnd_, nullptr, FALSE); return; }
        s.busy = true; s.status.clear();
        const auto gen = aurelia_page_generation_;
        aureliaRequest(request, [gen, logout = row.action == A::logout](Launcher& app, aurelia::Reply reply) {
            if (reply.ok && logout) {
                ++*app.aurelia_epoch_; app.aurelia_authenticated_ = false; app.aurelia_account_ = 0;
                app.aurelia_cloud_.clear(); app.closeFriendChat(); app.friend_chat_drafts_.clear();
            }
            app.aureliaRescan();
            if (!app.aurelia_ui_.open || app.aurelia_page_generation_ != gen) return;
            app.aurelia_ui_.busy = false;
            if (reply.ok) app.aureliaPage(app.aurelia_ui_.page);
            else { app.aurelia_setup_status_ = app.aurelia_ui_.status = reply.error; app.aureliaRows(); }
        });
    } else {
        if (game_sessions_.contains(gameKey(s.game)) || aurelia_playing_.contains(s.game.app_id)) {
            s.status = tr(L"Najpierw zamknij grę, zanim zmienisz jej pliki lub zapisy"); InvalidateRect(hwnd_, nullptr, FALSE); return;
        }
        if (request.command == C::cancel_install || request.command == C::install_pause || request.command == C::install_forget) {
            // Said at once; the next poll shows where the transfer is (paused and stopped ones stay in the backlog).
            const bool pausing = request.command == C::install_pause, forgetting = request.command == C::install_forget;
            aureliaRequest(request, [pausing, forgetting](Launcher& app, aurelia::Reply r) {
                app.aurelia_ui_.status = !r.ok ? r.error : pausing ? tr(L"Wstrzymywanie pobierania…") : forgetting ? tr(L"Usunięto z kolejki") : tr(L"Zatrzymywanie pobierania…");
                app.aurelia_poll_at_ = 0;
                if (app.aurelia_ui_.open && app.aurelia_ui_.page == P::game) app.aureliaRows();
                InvalidateRect(app.hwnd_, nullptr, FALSE);
            });
        } else aureliaOperation(request, s.game);
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

// Games installed in several libraries (read only: Aurelia reports what to delete, it removes nothing).
void Launcher::aureliaReportDuplicates() {
    auto& s = aurelia_ui_; if (!s.open || !aurelia_transport_ || s.busy) return;
    const auto generation = aurelia_page_generation_;
    s.busy = true; s.status = tr(L"Wczytywanie…"); aureliaRows();
    aureliaRequest({C::duplicates}, [generation](Launcher& app, aurelia::Reply reply) {
        const auto list = reply.ok ? aurelia::duplicates(reply.data) : std::nullopt;
        std::wstring message;
        if (!reply.ok) message = reply.error;
        else if (!list) message = tr(L"Nieprawidłowa odpowiedź Aurelii");
        else if (list->empty()) message = tr(L"Żadna gra nie jest zainstalowana w kilku bibliotekach.");
        else {
            uint64_t bytes = 0; std::wstring names;
            for (size_t i = 0; i < list->size(); ++i) {
                const auto& d = (*list)[i];
                bytes += d.reclaimable;
                if (i < 3) names += (names.empty() ? L"" : L", ") + (d.name.empty() ? std::to_wstring(d.app) : d.name);
            }
            message = std::to_wstring(list->size()) + tr(L" gier w kilku bibliotekach: ") + names + (list->size() > 3 ? L"…" : L"") +
                      L" · " + sizeText(bytes) + tr(L" do odzyskania. Usuń zbędne kopie ręcznie po zamknięciu Steam.");
        }
        if (app.aurelia_ui_.open && app.aurelia_page_generation_ == generation) { app.aurelia_ui_.busy = false; app.aurelia_ui_.status = message; app.aureliaRows(); }
        else if (!message.empty()) app.showNotification(reply.ok ? NotificationKind::launch : NotificationKind::error, L"Aurelia", message);
        InvalidateRect(app.hwnd_, nullptr, FALSE);
    });
}

// The sign-in page: Steam's QR code for the Steam Mobile app and, in the same page, the field for the
// account's name and then its password, as Steam's own sign-in. One `login --json` runs at a time with
// its stdin kept open: the QR code's until the password is given, then the password's. Their events
// show the code, ask for a Steam Guard code or say to approve in the app; the last line is the result.
void Launcher::aureliaOpenSignIn() {
    auto& s = aurelia_ui_; if (!s.open || !aurelia_transport_) return;
    if (!aurelia_operations_.empty() || !aurelia_playing_.empty() || !aurelia_downloads_.empty()) {
        s.status = tr(L"Najpierw zakończ gry i pobierania Aurelii"); InvalidateRect(hwnd_, nullptr, FALSE); return;
    }
    aureliaCancelLogin();
    s.login = aurelia_ui::State::Login::form; s.login_account.clear(); s.input.clear();
    s.focus = s.first = 0; s.back_focus = false;
    s.input_focus = desktop_mode_ || !pad_input_; // The keyboard types the account's name at once; a pad starts on the rows.
    aureliaLoginProcess({C::login_qr});
    audio_.context();
}

void Launcher::aureliaLoginProcess(aurelia::Request request) {
    using L = aurelia_ui::State::Login;
    auto& s = aurelia_ui_; if (!s.open) return;
    aureliaStopLoginProcess(); // The earlier one goes.
    const bool qr = request.command == C::login_qr;
    aurelia_login_input_ = std::make_shared<aurelia::Input>();
    aurelia_login_cancel_ = std::make_shared<std::atomic_bool>(false);
    request.input = aurelia_login_input_;
    const auto generation = ++aurelia_login_generation_;
    s.qr.clear(); s.qr_size = 0; s.qr_scanned = false; s.qr_failed = false;
    if (qr) aurelia_setup_status_ = s.login_account.empty() ? tr(L"Pobieranie kodu QR…") : tr(L"Konto: ") + s.login_account;
    else { s.login = L::waiting; s.busy = true; s.input_focus = false; s.focus = 0; aurelia_setup_status_ = tr(L"Logowanie…"); }
    aureliaRows();
    aureliaRequest(std::move(request), [generation, qr](Launcher& app, aurelia::Reply reply) {
        if (app.aurelia_login_generation_ != generation) return;
        auto& s = app.aurelia_ui_;
        app.aurelia_login_input_.reset(); app.aurelia_login_cancel_.reset();
        if (reply.ok) {
            std::fill(s.input.begin(), s.input.end(), L'\0');
            s.login = L::none; s.qr.clear(); s.qr_size = 0; s.qr_scanned = s.qr_failed = false; s.busy = false; s.input.clear(); s.login_account.clear();
            const auto account = wideLenient(aurelia::text(reply.data, "account"));
            app.aurelia_setup_status_.clear();
            if (!account.empty()) app.aurelia_account_name_ = account;
            app.showNotification(NotificationKind::launch, L"Aurelia", account.empty() ? std::wstring(tr(L"Zalogowano")) : tr(L"Zalogowano: ") + account);
            // The daemon keeps the session it had: it takes the new one, then the page asks again.
            app.aureliaRequest({C::reconnect}, [](Launcher& app, aurelia::Reply) {
                app.aureliaRescan();
                if (app.aurelia_ui_.open && app.aurelia_ui_.page == P::setup && app.aurelia_ui_.login == L::none) app.aureliaPage(P::setup);
            });
            if (s.open) app.aureliaRows();
            return;
        }
        if (reply.cancelled) return;
        const auto type = aurelia::text(reply.data, "type");
        const std::wstring message =
            type == "login_denied" ? tr(L"Logowanie odrzucone w aplikacji Steam Mobile.") :
            type == "login_expired" ? tr(L"Kod wygasł przed zatwierdzeniem. Spróbuj ponownie.") :
            type == "timeout" ? tr(L"Logowanie nie zostało zatwierdzone na czas.") :
            type == "auth_required" ? tr(L"Nieprawidłowa nazwa konta, hasło lub kod Steam Guard.") :
            type == "rate_limited" ? tr(L"Steam chwilowo ogranicza logowanie. Spróbuj za kilka minut.") :
            type == "offline" || type == "network_timeout" ? tr(L"Brak połączenia ze Steam.") :
            reply.error.empty() ? std::wstring(tr(L"Logowanie Aurelii nie powiodło się")) : reply.error;
        if (!s.open || s.login == L::none) return;
        if (qr) {
            // The QR code's sign-in ended without one: a new code when it only expired, else the
            // reason and Nowy kod QR; the field goes on taking the account and password.
            s.qr.clear(); s.qr_size = 0; s.qr_scanned = false;
            if (s.login == L::form && (type == "login_expired" || type == "timeout")) { app.aureliaLoginProcess({C::login_qr}); return; }
            s.qr_failed = true;
            app.aurelia_setup_status_ = message;
        } else {
            // The password's sign-in failed: the page again, a new QR code, the password asked once more.
            s.login = L::form; s.busy = false; s.input.clear(); s.input_focus = app.desktop_mode_ || !app.pad_input_;
            app.aureliaLoginProcess({C::login_qr});
            app.aurelia_setup_status_ = message;
        }
        app.aureliaRows();
    }, [generation, qr](Launcher& app, json::Value event) {
        auto& s = app.aurelia_ui_;
        if (app.aurelia_login_generation_ != generation || !s.open || s.login == L::none) return;
        const auto kind = aurelia::text(event, "event");
        if (kind == "qr_challenge" && qr) {
            if (s.login != L::form) return;
            // Steam's challenge as a QR code; a new one replaces it when Steam rotates the code.
            s.qr.clear(); s.qr_size = 0;
            try {
                const auto code = qrcodegen::QrCode::encodeText(aurelia::text(event, "url").c_str(), qrcodegen::QrCode::Ecc::MEDIUM);
                s.qr_size = code.getSize();
                s.qr.reserve(static_cast<size_t>(s.qr_size) * static_cast<size_t>(s.qr_size));
                for (int y = 0; y < s.qr_size; ++y) for (int x = 0; x < s.qr_size; ++x) s.qr.push_back(code.getModule(x, y) ? 1 : 0);
            } catch (...) { s.qr.clear(); s.qr_size = 0; }
            s.qr_scanned = s.qr_failed = false;
            if (app.aurelia_setup_status_ == tr(L"Pobieranie kodu QR…")) app.aurelia_setup_status_ = tr(L"Steam Mobile → Steam Guard → Zeskanuj kod QR");
        } else if (kind == "qr_scanned" && qr) {
            s.qr_scanned = true;
            app.aurelia_setup_status_ = tr(L"Kod zeskanowany. Zatwierdź logowanie w aplikacji.");
        } else if (kind == "awaiting_confirmation") {
            app.aurelia_setup_status_ = tr(L"Logowanie… Jeśli Steam o to poprosi, zatwierdź je w aplikacji Steam Mobile.");
        } else if (kind == "guard_required") {
            if (aurelia::text(event, "type") == "device_confirmation") app.aurelia_setup_status_ = tr(L"Zatwierdź logowanie w aplikacji Steam Mobile.");
            else {
                // A typed code: the field asks for it; the sign-in waits on its stdin.
                s.login = L::guard; s.guard_email = aurelia::text(event, "type") == "email";
                s.busy = false; s.input.clear(); s.input_focus = true;
                app.aurelia_setup_status_ = s.guard_email ? tr(L"Wpisz kod Steam Guard z e-maila.") : tr(L"Wpisz kod Steam Guard z aplikacji Steam Mobile.");
                if (!app.desktop_mode_ && app.pad_input_) app.openKeyboard();
            }
        }
        app.aureliaRows();
    }, aurelia_login_cancel_);
}

void Launcher::aureliaStopLoginProcess() {
    ++aurelia_login_generation_;
    if (aurelia_login_cancel_) aurelia_login_cancel_->store(true); // The CLI is asked to stop ("cancel"), then ended.
    aurelia_login_input_.reset(); aurelia_login_cancel_.reset();
}

// Leaves the sign-in page: its `login` stops, what was typed goes.
void Launcher::aureliaCancelLogin() {
    aureliaStopLoginProcess();
    auto& s = aurelia_ui_;
    if (s.login == aurelia_ui::State::Login::none) return;
    std::fill(s.input.begin(), s.input.end(), L'\0'); s.input.clear(); s.input_focus = false;
    s.login = aurelia_ui::State::Login::none; s.login_account.clear();
    s.qr.clear(); s.qr_size = 0; s.qr_scanned = s.qr_failed = false; s.busy = false; s.focus = s.first = 0;
    aurelia_setup_status_.clear();
    if (s.open) aureliaRows();
}

void Launcher::aureliaSubmit() {
    auto& s = aurelia_ui_;
    using L = aurelia_ui::State::Login;
    // The sign-in's steps: the account's name, its password (then its sign-in starts), a Steam Guard code.
    if (s.open && s.page == P::setup && (s.login == L::form || s.login == L::guard)) {
        if (s.input.empty()) return;
        if (s.login == L::form && s.login_account.empty()) {
            if (!aurelia::validAccount(s.input)) { aurelia_setup_status_ = tr(L"Nieprawidłowa nazwa konta"); aureliaRows(); return; }
            s.login_account = s.input; s.input.clear(); s.input_focus = true;
            aurelia_setup_status_ = tr(L"Konto: ") + s.login_account;
            aureliaRows(); return;
        }
        if (s.login == L::form) {
            aurelia::Request request{C::login_password}; request.text = s.login_account; request.secret = s.input;
            std::fill(s.input.begin(), s.input.end(), L'\0'); s.input.clear();
            aureliaLoginProcess(std::move(request));
            return;
        }
        if (aurelia_login_input_) aurelia_login_input_->write(text_encoding::utf8(s.input));
        s.input.clear(); s.login = L::waiting; s.busy = true; s.input_focus = false;
        aurelia_setup_status_ = tr(L"Sprawdzanie kodu…");
        aureliaRows(); return;
    }
    if (!s.open || s.input.empty() || s.busy) return;
    if (s.page == P::versions) aureliaActivate({tr(L"Zainstalować i przypiąć wskazany manifest?"), tr(L"Starsza wersja może nie obsługiwać nowszych zapisów gry."), {}, A::downgrade, 0, s.input});
    else if (s.page == P::workshop) {
        auto input = s.input; A action = A::workshop_install;
        if (input.starts_with(L"+")) { action = A::workshop_subscribe; input.erase(0, 1); }
        else if (input.starts_with(L"-")) { action = A::workshop_unsubscribe; input.erase(0, 1); }
        const auto value = json::string(text_encoding::utf8(input)); const auto id = aurelia::integer(&value);
        if (id && *id) aureliaActivate({{}, {}, {}, action, *id});
        else s.status = tr(L"Wpisz poprawny identyfikator Workshop");
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::aureliaCharacter(wchar_t c) {
    if (!aureliaTyping() || c < 32 || c == 127 || aurelia_ui_.input.size() >= 4000) return;
    aurelia_ui_.input.push_back(c); InvalidateRect(hwnd_, nullptr, FALSE);
}
// The wheel scrolls the rows; the focus stays on a visible one.
void Launcher::aureliaScroll(int rows) {
    auto& s = aurelia_ui_; if (!s.open || s.confirmation) return;
    const int visible = aureliaVisibleRows(), count = static_cast<int>(s.rows.size());
    s.first = std::clamp(s.first + rows, 0, std::max(0, count - visible));
    if (count) s.focus = std::clamp(s.focus, s.first, std::min(count - 1, s.first + visible - 1));
    InvalidateRect(hwnd_, nullptr, FALSE);
}
void Launcher::aureliaKey(WPARAM key, bool repeated) {
    auto& s = aurelia_ui_; if (!s.open) return;
    if (s.confirmation) {
        if (key == VK_ESCAPE || key == VK_BACK) { aureliaAnswer(false); return; }
        if (key == VK_LEFT || key == VK_RIGHT || key == VK_TAB || key == VK_UP || key == VK_DOWN) { s.confirm_focus = !s.confirm_focus; audio_.navigate(); }
        else if ((key == VK_RETURN || key == VK_SPACE) && !repeated) { aureliaAnswer(s.confirm_focus); return; }
        InvalidateRect(hwnd_, nullptr, FALSE); return;
    }
    if (key == VK_ESCAPE || (key == VK_BACK && !s.input_focus)) {
        if (s.page == P::setup && s.login != aurelia_ui::State::Login::none) { aureliaCancelLogin(); return; } // Back from a sign-in: the page's choices again.
        if (s.page == P::setup || s.page == P::game) closeAurelia();
        else if (s.page == P::branches) aureliaPage(P::versions);
        else aureliaPage(P::game);
        return;
    }
    if (key == VK_F5 && !repeated && s.login == aurelia_ui::State::Login::none) { aureliaPage(s.page); return; }
    // Back (✕ on the desktop's first pages) above the rows: Up from the first row reaches it.
    if (s.back_focus) {
        if (key == VK_DOWN || key == VK_TAB) { s.back_focus = false; audio_.navigate(); }
        else if ((key == VK_RETURN || key == VK_SPACE) && !repeated) { s.back_focus = false; aureliaKey(VK_ESCAPE, false); return; }
        else if (key == VK_UP) { navigationBoundary(); return; }
        InvalidateRect(hwnd_, nullptr, FALSE); return;
    }
    if (key == VK_TAB && !s.input_hint.empty()) { s.input_focus = !s.input_focus; InvalidateRect(hwnd_, nullptr, FALSE); return; }
    if (key == VK_PRIOR || key == VK_NEXT) { aureliaScroll(key == VK_PRIOR ? -4 : 4); return; }
    if (s.input_focus) {
        if (key == VK_BACK && s.input.empty() && s.login == aurelia_ui::State::Login::form && !s.login_account.empty()) {
            // Back from the password to the account's name.
            s.input = s.login_account; s.login_account.clear();
            aurelia_setup_status_.clear(); aureliaRows(); return;
        }
        if (key == VK_BACK && !s.input.empty()) {
            const auto last = s.input.back(); s.input.pop_back();
            if (last >= 0xdc00 && last <= 0xdfff && !s.input.empty() && s.input.back() >= 0xd800 && s.input.back() <= 0xdbff) s.input.pop_back();
        } else if (key == VK_RETURN && !repeated) aureliaSubmit();
        else if (key == VK_UP) {
            s.input_focus = false;
            if (s.rows.empty()) s.back_focus = true; else s.focus = static_cast<int>(s.rows.size()) - 1;
        }
        else if (key == 'V' && (GetKeyState(VK_CONTROL) & 0x8000) && OpenClipboard(hwnd_)) {
            if (const auto handle = GetClipboardData(CF_UNICODETEXT)) {
                if (const auto* text = static_cast<const wchar_t*>(GlobalLock(handle))) {
                    const auto count = std::min<size_t>(GlobalSize(handle) / sizeof(wchar_t), 4001);
                    for (size_t i = 0; i < count && text[i] && s.input.size() < 4000; ++i) if (text[i] >= 32) s.input.push_back(text[i]);
                    GlobalUnlock(handle);
                }
            }
            CloseClipboard();
        }
        InvalidateRect(hwnd_, nullptr, FALSE); return;
    }
    if (key == VK_UP || key == VK_DOWN) {
        const int last = static_cast<int>(s.rows.size()) - 1;
        if (key == VK_DOWN && s.focus >= last && !s.input_hint.empty()) { s.input_focus = true; audio_.navigate(); InvalidateRect(hwnd_, nullptr, FALSE); return; }
        const int next = std::clamp(s.focus + (key == VK_UP ? -1 : 1), 0, std::max(0, last));
        if (key == VK_UP && next == s.focus) { s.back_focus = true; audio_.navigate(); InvalidateRect(hwnd_, nullptr, FALSE); return; }
        if (next == s.focus) { navigationBoundary(); return; }
        s.focus = next; audio_.navigate();
        const int visible = aureliaVisibleRows();
        if (s.focus < s.first) s.first = s.focus;
        if (s.focus >= s.first + visible) s.first = s.focus - visible + 1;
    } else if ((key == VK_RETURN || key == VK_SPACE) && !repeated && s.focus >= 0 && s.focus < static_cast<int>(s.rows.size())) aureliaActivate(s.rows[s.focus]);
    InvalidateRect(hwnd_, nullptr, FALSE);
}
void Launcher::aureliaClick(float x, float y) {
    auto& s = aurelia_ui_; const auto v = renderer_.viewport(); const aurelia_ui::Layout layout{v.width, v.height};
    const auto hit = layout.hit(s, x, y);
    if (hit == -1) { aureliaKey(VK_ESCAPE, false); return; }
    if (s.confirmation) {
        if (hit == -4 || hit == -5) { aureliaAnswer(hit == -5); return; }
    } else if (hit == -2) { s.input_focus = true; s.back_focus = false; if (!desktop_mode_ && pad_input_) openKeyboard(); }
    else if (hit == -3) aureliaSubmit();
    else if (hit >= 0) { s.input_focus = false; s.back_focus = false; s.focus = hit; aureliaActivate(s.rows[hit]); }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

// The desktop dialog (Renderer::drawDesktopAurelia) records its controls: rows, the field, its
// button, Back and ✕, and the confirmation's two answers.
void Launcher::aureliaDesktopClick(desktop_ui::SheetHit hit) {
    using K = desktop_ui::SheetHit::Kind;
    auto& s = aurelia_ui_;
    if (hit.kind == K::close) { closeAurelia(); return; }
    if (hit.kind == K::back) { aureliaKey(VK_ESCAPE, false); return; }
    if (s.confirmation) {
        if (hit.kind == K::action && (hit.index == 1 || hit.index == 2)) aureliaAnswer(hit.index == 2);
        return;
    }
    if (hit.kind == K::field) { s.input_focus = true; s.back_focus = false; }
    else if (hit.kind == K::action && hit.index == 0) aureliaSubmit();
    else if (hit.kind == K::row && hit.index >= 0 && hit.index < static_cast<int>(s.rows.size())) {
        s.input_focus = false; s.back_focus = false; s.focus = hit.index; aureliaActivate(s.rows[static_cast<size_t>(hit.index)]);
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}
