// Opcje uruchamiania → Mody, the launcher's side (src/library/game_mods*.hpp): where a game's mods go,
// what its folder holds (scanned on a worker once per session and after every install), the badges the
// game's page shows for them, and the preferred methods in Ustawienia → Biblioteka → Uruchamianie.
// Windows only: elsewhere no game offers the tab and no badges show.
//
// A PC game's mods go into its program's folder: the file it starts (DRM-free, added by hand, Epic…), else
// the installation's main program (an Unreal game's *-Win64-Shipping.exe) found by the scan, or the folder
// the user chose ("Zmień…", LibraryState::mod_folders). An emulated game's go into its emulator's folder
// (the game's own emulator, else its system's), shared by every game it runs.
#include "launcher.hpp"
#include "../library/emulators.hpp"
#include "../platform/text_encoding.hpp"

#include <algorithm>

std::filesystem::path Launcher::modsData() const { return userConfigDirectory() / L"mods"; }

std::optional<GameModsState::Scanned> Launcher::modTarget(const Game& game) const {
#ifndef _WIN32
    (void)game;
    return std::nullopt;
#else
    if (!isGame(game) || launcherTile(game) || game.kind == GameKind::catalog || gameKey(game).empty()) return std::nullopt;
    GameModsState::Scanned result;
    const auto key = gameKey(game);
    // An emulated game: its emulator's program.
    if (!game.rom.empty() && emulation_setup_) {
        const auto* rule = emulation_setup_->forFile(game.platform, game.rom);
        if (!rule) return std::nullopt;
        const emulators::Found* found = nullptr;
        std::filesystem::path program;
        if (const auto own = library_state_.game_emulators.find(key); own != library_state_.game_emulators.end()) {
            if (!own->second.id.empty()) found = emulation_setup_->found(text_encoding::wide(own->second.id));
            else if (!own->second.program.empty()) program = own->second.program;
        }
        if (!found && program.empty()) found = emulation_setup_->emulatorFor(*rule);
        if (found && found->found()) program = found->target.empty() ? found->program : found->target;
        if (program.empty() || program.extension() != L".exe") return std::nullopt; // A flatpak, a script without its program.
        const auto* info = found ? emulators::find(found->id) : nullptr;
        result.emulator = true;
        result.emulator_name = info ? std::wstring(info->name) : program.stem().wstring();
        result.target.kind = game_mods::TargetKind::emulator;
        result.target.exe = program;
        result.target.folder = result.target.root = program.parent_path();
        result.target.title = result.emulator_name;
        return result;
    }
    const auto root = gameFolder(game);
    if (root.empty()) return std::nullopt; // Not installed.
    result.target.root = root;
    result.target.title = game.name;
    result.target.steam_appid = game.source == GameSource::steam ? game.app_id : 0;
    if (game.executable.is_absolute() && game.executable.extension() == L".exe") {
        result.target.exe = game.executable;
        result.target.folder = game.executable.parent_path();
    }
    if (const auto chosen = library_state_.mod_folders.find(key); chosen != library_state_.mod_folders.end()) {
        result.target.folder = chosen->second;
        result.target.exe.clear(); // The scan finds the program in the chosen folder.
    }
    return result;
#endif
}

void Launcher::requestModScan(const Game& game) {
    auto target = modTarget(game);
    if (!target) return;
    const auto key = gameKey(game);
    if (!mods_.scanning.insert(key).second) return;
    const auto generation = mods_.scan_generation;
    const auto data = modsData();
    const auto alive = alive_;
    const auto window = hwnd_;
    const bool started = work_.start([alive, window, key, generation, data, scanned = std::move(*target)]() mutable {
        auto& target = scanned.target;
        // The program's folder: the main program of the installation when the game names none.
        if (target.folder.empty()) {
            target.exe = game_mods::mainExecutable(target.root);
            target.folder = target.exe.empty() ? target.root : target.exe.parent_path();
        } else if (target.exe.empty()) {
            target.exe = game_mods::mainExecutable(target.folder, 1);
        }
        const auto record = game_mods::loadRecord(data, target.folder);
        scanned.state = game_mods::scan(target, record ? &*record : nullptr);
        target.x86 = scanned.state.x86;
        if (!alive->load()) return;
        auto message = std::make_unique<RemoteMessage>();
        message->run = [key, generation, scanned = std::move(scanned)](Launcher& app) mutable {
            app.mods_.scanning.erase(key);
            if (generation != app.mods_.scan_generation) return; // Stale: an install or a new folder meanwhile.
            app.mods_.scanned[key] = std::move(scanned);
            app.modsScanArrived(key);
        };
        if (PostMessageW(window, remote_message, 0, reinterpret_cast<LPARAM>(message.get()))) message.release();
    });
    if (!started) mods_.scanning.erase(key);
}

void Launcher::forgetModScans(const std::string& key) {
    ++mods_.scan_generation;
    mods_.scanning.clear();
    if (key.empty()) mods_.scanned.clear(); else mods_.scanned.erase(key);
}

// The line of badges on a game's page: its mods (Windows, once its folder was scanned), then what its
// launches switch on (Opcje uruchamiania: HDR, Logi).
std::vector<std::wstring> Launcher::modBadges(const Game& game) {
    if (!settings_.mod_badges) return {};
    const auto key = gameKey(game);
    if (key.empty()) return {};
    std::vector<std::wstring> badges;
#ifdef _WIN32
    if (const auto found = mods_.scanned.find(key); found != mods_.scanned.end()) badges = game_mods::badges(found->second.state);
    else requestModScan(game);
#endif
    const unsigned flags = launchFlags(game);
    if (flags & launch_flag::hdr) badges.push_back(L"HDR");
    if (flags & launch_flag::log) badges.push_back(tr(L"Logi"));
    return badges;
}

bool Launcher::modBadgesShown(const Game& game) const {
    if (!settings_.mod_badges || gameKey(game).empty()) return false;
    if (launchFlags(game) & (launch_flag::hdr | launch_flag::log)) return true;
#ifdef _WIN32
    const auto found = mods_.scanned.find(gameKey(game));
    return found != mods_.scanned.end() && !game_mods::badges(found->second.state).empty();
#else
    return false;
#endif
}

std::vector<settings_ui::Row> Launcher::gameModsSettingsRows() const {
    using settings_ui::Control;
    using settings_ui::Id;
    using settings_ui::Row;
    std::vector<Row> rows;
#ifdef _WIN32
    rows.push_back({Id::mod_badges, Control::toggle, tr(L"Plakietki modów"),
                    tr(L"Strona gry pokazuje w osobnej linii, co jest w niej włączone: ReShade, RenoDX, OptiScaler, DLSS, DLSS 5, ANR FG, a także HDR i logi."), {}, settings_.mod_badges});
    for (const auto mod : {game_mods::Mod::reshade, game_mods::Mod::optiscaler, game_mods::Mod::dlss, game_mods::Mod::anr}) {
        Row row{Id::mod_method, Control::choice, std::wstring(game_mods::name(mod)) + L" · " + tr(L"metoda"), {}};
        row.description = mod == game_mods::Mod::reshade ? tr(L"Plik, którym gra wczyta ReShade (też RenoDX i DLSS 5). Obok OptiScalera zawsze ReShade64.dll.") :
                          mod == game_mods::Mod::optiscaler ? tr(L"Plik, którym gra wczyta OptiScalera; dxgi.dll działa w większości gier DirectX 11 i 12.") :
                          mod == game_mods::Mod::dlss ? tr(L"Które pliki DLSS gry zastąpić najnowszymi od NVIDII.") :
                          tr(L"Pełny pakiet ma własny OptiScaler; dodatek wchodzi do OptiScalera, który gra już ma.");
        row.key = game_mods::slug(mod);
        const auto found = settings_.mod_methods.find(row.key);
        const auto chosen = game_mods::methodOr(mod, found == settings_.mod_methods.end() ? std::string_view{} : std::string_view(found->second));
        int index = 0;
        for (const auto& method : game_mods::methods(mod)) {
            if (method.id == chosen) row.choice = index;
            row.choices.push_back({tr(std::wstring(method.label).c_str())});
            row.choices.back().key = text_encoding::wide(method.id);
            ++index;
        }
        row.value = row.choices[static_cast<size_t>(row.choice)].label;
        rows.push_back(std::move(row));
    }
    Row cache{Id::mod_cache, Control::action, tr(L"Pobrane mody"),
              tr(L"Wydania pobrane do instalacji (model DLSS 5 ma ponad 250 MB). Mody zainstalowane w grach zostają."), tr(L"Wyczyść")};
    cache.locked = mods_.busy;
    rows.push_back(std::move(cache));
#endif
    return rows;
}

bool Launcher::gameModsSettingsToggle(const settings_ui::Row& row) {
    if (row.id != settings_ui::Id::mod_badges) return false;
    settings_.mod_badges = !settings_.mod_badges;
    settingsChanged();
    audio_.context();
    return true;
}

bool Launcher::gameModsSettingsChoose(const settings_ui::Row& row) {
    if (row.id != settings_ui::Id::mod_method) return false;
    const auto mod = game_mods::fromSlug(row.key);
    const auto list = mod ? game_mods::methods(*mod) : std::span<const game_mods::Method>{};
    if (mod && settings_state_.choice >= 0 && settings_state_.choice < static_cast<int>(list.size()))
        settings_.mod_methods[row.key] = std::string(list[static_cast<size_t>(settings_state_.choice)].id);
    settings_state_.editing = false;
    settingsChanged();
    return true;
}

bool Launcher::gameModsSettingsActivate(const settings_ui::Row& row) {
    if (row.id != settings_ui::Id::mod_cache) return false;
    if (mods_.busy) return true;
    const auto cache = modsData() / L"cache";
    work_.start([cache] { std::error_code ec; std::filesystem::remove_all(cache, ec); });
    showNotification(NotificationKind::launch, tr(L"Pobrane mody"), tr(L"Wyczyszczono. Następna instalacja pobierze wydania od nowa."));
    audio_.context();
    return true;
}

std::string Launcher::preferredModMethod(game_mods::Mod mod) const {
    const auto found = settings_.mod_methods.find(game_mods::slug(mod));
    return std::string(game_mods::methodOr(mod, found == settings_.mod_methods.end() ? std::string_view{} : std::string_view(found->second)));
}
