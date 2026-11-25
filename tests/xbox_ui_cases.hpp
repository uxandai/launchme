#include "../src/platform/unix_time.hpp"
// Xbox account: the helper offered on the platform's page, the sign-in, Instalacja przez Launchme,
// the Xbox app's install hand-off, the own install dialog and uninstall routing. A fake service: no
// real helper, Microsoft account or game.
namespace {
class FakeXbox final : public xbox_account::Service {
public:
    std::optional<xbox_account::Snapshot> result;
    std::optional<gog::InstallSnapshot> download;
    std::vector<xbox_account::Command> commands;
    gog::InstallRequest last_request;
    Game last_game;
    int installs = 0, cancelled = 0;
    bool active = false;
    void submit(xbox_account::Command c) override { commands.push_back(c); }
    std::optional<xbox_account::Snapshot> take() override { auto r = std::move(result); result.reset(); return r; }
    bool install(const Game& game, gog::InstallRequest request) override { ++installs; active = true; last_game = game; last_request = std::move(request); return true; }
    std::optional<gog::InstallSnapshot> takeInstall() override { auto r = std::move(download); download.reset(); return r; }
    void cancelOperation() override { ++cancelled; active = false; }
    bool busy() const override { return active; }
    std::vector<uint32_t> asked;
    std::optional<xbox_account::AchievementsAnswer> answer;
    void achievements(uint32_t title_id) override { asked.push_back(title_id); }
    std::optional<xbox_account::AchievementsAnswer> takeAchievements() override { auto r = std::move(answer); answer.reset(); return r; }
};
}
int LauncherUiTest::xboxAccount(HWND window) {
    using settings_ui::Id;
    Launcher app; app.hwnd_ = window; app.execute_ = fakeExecute;
    if (!app.renderer_.initialize(window)) return 9300;
    const auto capture = [&](const wchar_t* name) {
        wchar_t folder[4096]{}; if (!GetEnvironmentVariableW(L"LAUNCHME_UI_CAPTURE_DIR", folder, 4096)) return true;
        app.settings_motion_.valid = false;
        return app.renderer_.captureForTest(std::filesystem::path(folder) / name, [&] { app.paint(); });
    };
    const auto find = [&](Id id) {
        return std::find_if(app.settings_rows_.begin(), app.settings_rows_.end(), [&](const auto& r) { return r.id == id; });
    };
    const auto select = [&](Id id) {
        const auto row = find(id);
        if (row == app.settings_rows_.end()) return false;
        app.settings_state_.selected[app.settings_state_.category] = static_cast<int>(row - app.settings_rows_.begin());
        app.settings_state_.options = true; app.settings_state_.editing = false; app.ensureSettingVisible(); return true;
    };
    auto service = std::make_unique<FakeXbox>(); auto* fake = service.get(); app.xbox_service_ = std::move(service);
    app.onLibrary(std::make_unique<LibraryMessage>());
    app.openSettings(); app.settings_state_.selectCategory(static_cast<int>(settings_ui::Category::library)); app.buildSettingsRows();
    // The Xbox page: the helper first, the account, the library, the (locked) own install, the Xbox app.
    app.openPlatformSettings(GameSource::xbox);
    if (app.settings_state_.platform() != GameSource::xbox || app.settings_rows_.empty() || app.settings_rows_[0].id != Id::xbox_helper ||
        find(Id::pc_connect) == app.settings_rows_.end() || find(Id::xbox_own_install) == app.settings_rows_.end() || !find(Id::xbox_own_install)->locked ||
        app.settings_rows_.back().id != Id::pc_client || find(Id::xbox_logout) != app.settings_rows_.end()) return 9301;
    if (!capture(L"xbox-settings-helper.png")) return 9302;
    if (!select(Id::xbox_helper)) return 9303;
    app.settingsActivate(); // Pobierz.
    if (fake->commands.empty() || fake->commands.back() != xbox_account::Command::download) return 9303;
    xbox_account::Snapshot ready; ready.helper = true;
    fake->result = ready; app.pollXbox();
    if (!app.xbox_state_.helper || app.settings_rows_[0].value != tr(L"Usuń")) return 9304;
    // Konto → Połącz: the helper's own window signs in; connecting switches the platform on.
    if (!select(Id::pc_connect)) return 9305;
    app.settingsActivate();
    if (fake->commands.back() != xbox_account::Command::connect || app.xbox_state_.state != xbox_account::State::preparing) return 9305;
    xbox_account::Snapshot connected = ready; connected.state = xbox_account::State::connected; connected.account = "0123ABCD";
    connected.name = L"pat@example.com"; connected.games = 2; connected.synced_at = 1790000000; connected.game_pass = true;
    fake->result = connected; app.pollXbox();
    if (!app.xboxConnected() || !app.settings_.enabled_pc_sources.contains("xbox") || find(Id::xbox_logout) == app.settings_rows_.end() ||
        find(Id::xbox_refresh) == app.settings_rows_.end() || find(Id::xbox_own_install)->locked || find(Id::xbox_own_install)->checked) return 9306;
    if (!capture(L"xbox-settings-connected.png")) return 9307;
    if (!select(Id::xbox_refresh)) return 9308;
    app.settingsActivate();
    if (fake->commands.back() != xbox_account::Command::sync) return 9308;

    // Two account games: a GDK one the helper can install, a Game Pass one only the Xbox app can.
    Game owned; owned.source = GameSource::xbox; owned.kind = GameKind::store; owned.source_id = "Studio.Moonlit_8wekyb3d8bbwe";
    owned.name = L"Moonlit: Archive"; owned.installed = false; owned.account_id = "0123ABCD"; owned.xbox_product = "9NBLGGH4R315";
    owned.xbox_own_install = true; owned.install_action = xbox_account::xboxAppInstall(owned.xbox_product, true);
    Game pass = owned; pass.source_id = "Studio.PassQuest_8wekyb3d8bbwe"; pass.name = L"Pass Quest"; pass.xbox_product = "9PGAMEPASS01";
    pass.xbox_own_install = false; pass.install_action = xbox_account::xboxAppInstall(pass.xbox_product, true);
    auto pc = std::make_unique<PcLibraryMessage>(); pc->generation = app.pc_token_->load(); pc->library.visible = {owned, pass}; app.onPcLibrary(std::move(pc));
    app.openPcLibrary(GameSource::xbox);
    if (app.cards_.size() < 2 || !app.actionEnabled(owned) || !app.actionEnabled(pass) || std::wstring(primaryActionLabel(owned)) != tr(L"Instaluj")) return 9309;
    // Background work rebuilding this library (the game index learning studios, a store sync) keeps
    // the tiles where they are: no second entrance, the focus stays on its game.
    {
        app.focusCard(1); app.arrived_ = 5;
        const auto focused = gameKey(app.cards_[1].game);
        app.refreshRecentGames();
        if (app.arrived_ != 5 || app.selected_ >= app.cards_.size() || gameKey(app.cards_[app.selected_].game) != focused) return 9326;
        // A sync bringing the store's art for games shown without it: in place, the same tiles.
        auto art = std::make_unique<PcLibraryMessage>(); art->generation = app.pc_token_->load();
        auto owned_art = owned; owned_art.store_tall_art = "https://store-images.s-microsoft.com/poster.png";
        art->library.visible = {owned_art, pass}; app.onPcLibrary(std::move(art));
        const auto card = std::find_if(app.cards_.begin(), app.cards_.end(), [&](const auto& c) { return gameKey(c.game) == gameKey(owned); });
        if (card == app.cards_.end() || card->game.store_tall_art != owned_art.store_tall_art || app.arrived_ != 5) return 9327;
        app.focusCard(0);
    }
    // Default: Instaluj hands the product to the Xbox app.
    auto before = dispatched.size();
    app.launchGame(owned);
    if (app.gogInstallOpen() || !app.pending_launch_) return 9310;
    app.finishLaunch();
    if (dispatched.size() != before + 1 || dispatched.back() != L"msxbox://game/?productId=9NBLGGH4R315") return 9311;
    // Instalacja przez Launchme: the GDK title goes through the install dialog, the Game Pass one still to the app.
    app.openSettings(); app.openPlatformSettings(GameSource::xbox);
    if (!select(Id::xbox_own_install)) return 9312;
    app.settingsActivate();
    if (!app.settings_.xbox_own_install || !find(Id::xbox_own_install)->checked) return 9312;
    if (!capture(L"xbox-settings-own-install.png")) return 9313;
    app.launchGame(owned);
    if (!app.gogInstallOpen() || app.gog_install_ui_.game.source != GameSource::xbox ||
        app.gog_install_ui_.request.install_directory.filename() != L"Xbox-Moonlit Archive") return 9314;
    app.gogInstallKey(VK_DOWN, false); // The folder, then the action: the helper writes straight into the game's folder.
    if (app.gog_install_ui_.row != 3) return 9315;
    app.gogInstallAccept();
    if (fake->installs != 1 || fake->last_game.xbox_product != owned.xbox_product ||
        fake->last_request.install_directory != app.gog_install_ui_.request.install_directory) return 9316;
    gog::InstallSnapshot download; download.product = owned.source_id; download.state = gog::InstallState::downloading;
    download.done = 512ull * 1024 * 1024; download.total = 2048ull * 1024 * 1024;
    fake->download = download; app.pollXbox();
    if (app.gog_install_ui_.snapshot.progress().percent() != 25 || !capture(L"xbox-download.png")) return 9317;
    app.gogInstallAccept(); // Przerwij.
    if (fake->cancelled != 1) return 9318;
    app.gogInstallKey(VK_ESCAPE, false);
    before = dispatched.size();
    app.launchGame(pass);
    if (app.gogInstallOpen() || !app.pending_launch_) return 9319;
    app.finishLaunch();
    if (dispatched.size() != before + 1 || dispatched.back() != L"msxbox://game/?productId=9PGAMEPASS01") return 9319;

    // A game the helper installed: Launchme asks, then deletes its folder; Graj runs the helper.
    wchar_t temp[MAX_PATH]{}; GetTempPathW(MAX_PATH, temp);
    const auto folder = std::filesystem::path(temp) / (L"launchme-xbox-ui-" + std::to_wstring(GetCurrentProcessId()));
    std::error_code ec; std::filesystem::create_directories(folder, ec);
    { std::ofstream marker(folder / L".launchme-xbox.json"); marker << "{\"product_id\":\"9NBLGGH4R315\",\"executable\":\"Game.exe\"}"; }
    auto local = owned; local.installed = true; local.install_directory = folder; local.executable = folder / L"Game.exe";
    local.play_action = xbox_account::runAction(userConfigDirectory(), folder);
    if (!app.canUninstall(local) || !xbox_account::ownInstall(local)) return 9320;
    app.uninstallGame(local);
    if (app.prompt_kind_ != Launcher::PromptKind::uninstall || app.uninstall_game_.source_id != local.source_id) return 9321;
    app.exit_prompt_ = 0; app.prompt_kind_ = Launcher::PromptKind::exit; app.uninstall_game_ = {};
    before = dispatched.size();
    app.launchGame(local);
    if (!app.pending_launch_) return 9322;
    app.finishLaunch();
    if (dispatched.size() != before + 1 || dispatched.back() != xbox_account::helperPath(userConfigDirectory()).native()) return 9323;
    std::filesystem::remove_all(folder, ec);
    // An Xbox app installation still goes to Windows' Apps list.
    auto packaged = owned; packaged.installed = true; packaged.install_directory = L"C:\\XboxGames\\Moonlit\\Content";
    before = dispatched.size();
    app.uninstallGame(packaged);
    if (app.prompt_kind_ == Launcher::PromptKind::uninstall || dispatched.size() != before + 1 || dispatched.back() != L"ms-settings:appsfeatures") return 9324;
    app.xboxCommand(xbox_account::Command::logout);
    if (fake->commands.back() != xbox_account::Command::logout) return 9325;
    return 0;
}

// Xbox 0.2: the offline PC (asked once, changed only on a second press), the Game Pass badge's
// facts (filter, the Nowe w Game Pass row), play history, Xbox Live achievements through the
// plugin cache, and Aktualizuj for a game the helper installed. A fake service, as above.
int LauncherUiTest::xboxGamePass(HWND window) {
    using settings_ui::Id;
    Launcher app; app.hwnd_ = window; app.execute_ = fakeExecute;
    if (!app.renderer_.initialize(window)) return 9340;
    const auto capture = [&](const wchar_t* name) {
        wchar_t folder[4096]{}; if (!GetEnvironmentVariableW(L"LAUNCHME_UI_CAPTURE_DIR", folder, 4096)) return true;
        app.settings_motion_.valid = false;
        return app.renderer_.captureForTest(std::filesystem::path(folder) / name, [&] { app.paint(); });
    };
    const auto find = [&](Id id) {
        return std::find_if(app.settings_rows_.begin(), app.settings_rows_.end(), [&](const auto& r) { return r.id == id; });
    };
    const auto select = [&](Id id) {
        const auto row = find(id);
        if (row == app.settings_rows_.end()) return false;
        app.settings_state_.selected[app.settings_state_.category] = static_cast<int>(row - app.settings_rows_.begin());
        app.settings_state_.options = true; app.settings_state_.editing = false; app.ensureSettingVisible(); return true;
    };
    auto service = std::make_unique<FakeXbox>(); auto* fake = service.get(); app.xbox_service_ = std::move(service);
    app.settings_.enabled_pc_sources.insert("xbox"); app.settings_.xbox_own_install = true;
    app.onLibrary(std::make_unique<LibraryMessage>());

    // Signed in: Launchme asks once whether this PC is the account's offline PC.
    xbox_account::Snapshot connected; connected.helper = true; connected.state = xbox_account::State::connected; connected.account = "0123ABCD";
    connected.name = L"pat@example.com"; connected.games = 2; connected.synced_at = 1790000000; connected.game_pass = true;
    fake->result = connected; app.pollXbox();
    if (std::count(fake->commands.begin(), fake->commands.end(), xbox_account::Command::offline_status) != 1) return 9341;
    fake->result = connected; app.pollXbox();
    if (std::count(fake->commands.begin(), fake->commands.end(), xbox_account::Command::offline_status) != 1) return 9341; // Not again.
    app.openSettings(); app.settings_state_.selectCategory(static_cast<int>(settings_ui::Category::library)); app.buildSettingsRows();
    app.openPlatformSettings(GameSource::xbox);
    if (find(Id::xbox_offline) == app.settings_rows_.end() || find(Id::xbox_offline)->value != tr(L"Sprawdź")) return 9342;
    // Microsoft says: not the offline PC, two changes of three left. Włącz needs a second press.
    auto answered = connected; answered.offline.enabled = false; answered.offline.remaining = 2; answered.offline.max = 3;
    answered.offline.checked_at = unix_time::now();
    fake->result = answered; app.pollXbox();
    if (!select(Id::xbox_offline) || find(Id::xbox_offline)->value != tr(L"Włącz") || find(Id::xbox_offline)->description.find(L"2/3") == std::wstring::npos) return 9343;
    const auto commands = fake->commands.size();
    app.settingsActivate();
    if (fake->commands.size() != commands || find(Id::xbox_offline)->value != tr(L"Potwierdź")) return 9344;
    if (!capture(L"xbox-offline-confirm.png")) return 9345;
    app.settingsActivate();
    if (fake->commands.size() != commands + 1 || fake->commands.back() != xbox_account::Command::offline_on || !app.xbox_state_.offline_busy) return 9346;
    // Moving away cancels an armed change.
    auto enabled = answered; enabled.offline.enabled = true; enabled.offline.remaining = 1;
    enabled.offline.games["9NBLGGH4R315"] = unix_time::now() + 20 * 86400;
    enabled.updates.insert("9NBLGGH4R315");
    enabled.gamepass_recent = {"9PGAMEPASS01", "9NOTINLIB0X1"};
    enabled.activity[1717113201] = {1789990000, 754u, 12, 40, 250, 1000};
    enabled.addons["9NBLGGH4R315"] = {{"9NEXPANSION1", L"Moonlit: Deep Archive", true, {}}, {"9NSOUNDTRAK1", L"Soundtrack", false, {}}};
    fake->result = enabled; app.pollXbox();
    if (find(Id::xbox_offline)->value != tr(L"Wyłącz") || app.xbox_state_.offline_busy) return 9347;
    app.settingsActivate(); app.settingsMove(1);
    if (!app.drm_confirm_.empty()) return 9348;
    if (!capture(L"xbox-offline-on.png")) return 9349;
    app.screen_ = ScreenMode::shelves;

    // The account's games: one bought and installed here by the helper, one through Game Pass.
    wchar_t temp[MAX_PATH]{}; GetTempPathW(MAX_PATH, temp);
    const auto folder = std::filesystem::path(temp) / (L"launchme-xbox-pass-" + std::to_wstring(GetCurrentProcessId()));
    std::error_code ec; std::filesystem::create_directories(folder, ec);
    { std::ofstream marker(folder / L".launchme-xbox.json"); marker << "{\"product_id\":\"9NBLGGH4R315\",\"executable\":\"Game.exe\"}"; }
    Game owned; owned.source = GameSource::xbox; owned.kind = GameKind::store; owned.source_id = "Studio.Moonlit_8wekyb3d8bbwe";
    owned.name = L"Moonlit: Archive"; owned.account_id = "0123ABCD"; owned.xbox_product = "9NBLGGH4R315"; owned.xbox_own_install = true;
    owned.xbox_title_id = 1717113201; owned.installed = true; owned.install_directory = folder; owned.executable = folder / L"Game.exe";
    owned.play_action = xbox_account::runAction(userConfigDirectory(), folder);
    owned.store_playtime_minutes = 754; owned.last_played = 1789990000;
    Game pass; pass.source = GameSource::xbox; pass.kind = GameKind::store; pass.source_id = "Studio.PassQuest_8wekyb3d8bbwe";
    pass.name = L"Pass Quest"; pass.account_id = "0123ABCD"; pass.xbox_product = "9PGAMEPASS01"; pass.xbox_game_pass = true;
    pass.install_action = xbox_account::xboxAppInstall(pass.xbox_product, true);
    auto pc = std::make_unique<PcLibraryMessage>(); pc->generation = app.pc_token_->load(); pc->library.visible = {owned, pass}; app.onPcLibrary(std::move(pc));

    // Nowe w Game Pass: the account's Game Pass games in the catalog's order, unknown products left out.
    const auto* row = app.findCategory("gamepass-new");
    if (!row || row->games.size() != 1 || row->games[0].xbox_product != pass.xbox_product || app.homeRowTitle("gamepass-new") != tr(L"Nowe w Game Pass")) return 9350;
    const auto entries = app.homeRowEntries();
    if (std::none_of(entries.begin(), entries.end(), [](const auto& e) { return e.id == "gamepass-new"; })) return 9351;
    // Filtry: Game Pass and Kupione split the two.
    {
        std::vector<Game> games{owned, pass};
        app.applyLibraryFilters(games, library_view::filterBit(library_view::Filter::game_pass));
        if (games.size() != 1 || games[0].xbox_product != pass.xbox_product) return 9352;
        games = {owned, pass};
        app.applyLibraryFilters(games, library_view::filterBit(library_view::Filter::owned));
        if (games.size() != 1 || games[0].xbox_product != owned.xbox_product) return 9353;
        if (std::wstring(library_view::label(library_view::Filter::game_pass)) != L"Game Pass" ||
            std::wstring(library_view::label(library_view::Filter::owned)) != tr(L"Kupione")) return 9354;
    }
    // Offline do, Aktualizuj: the bought game installed here; nothing for the Game Pass one.
    if (!app.xboxOfflineUntil(owned) || app.xboxOfflineUntil(pass) || !app.xboxUpdateAvailable(owned) || app.xboxUpdateAvailable(pass)) return 9355;
    // The TV game menu offers Aktualizuj for it (and not for the Game Pass one).
    app.openPcLibrary(GameSource::xbox);
    for (size_t i = 0; i < app.cards_.size(); ++i) {
        app.focusCard(i);
        const bool mine = app.cards_[i].game.xbox_product == owned.xbox_product;
        if (app.menuRows().update != mine || (mine && context_menu::row(context_menu::Item::update, app.menuRows()) < 0)) return 9362;
    }
    app.openXboxUpdate(owned);
    if (!app.gogInstallOpen() || app.gog_install_ui_.game.xbox_product != owned.xbox_product || app.gog_install_ui_.request.install_directory != folder) return 9356;
    app.gogInstallKey(VK_ESCAPE, false);
    // Xbox Live's achievements fill the same cache as a plugin's: asked by title, answered once.
    if (!app.externalAchievements(owned) || app.externalAchievements(pass)) return 9357;
    app.requestXboxAchievements(owned, false);
    if (fake->asked != std::vector<uint32_t>{1717113201}) return 9358;
    app.requestXboxAchievements(owned, false); // Pending: not asked twice.
    if (fake->asked.size() != 1) return 9358;
    xbox_account::AchievementsAnswer answer; answer.title_id = 1717113201;
    SteamAchievement first; first.id = "1"; first.name = L"First Light"; first.achieved = true;
    SteamAchievement second; second.id = "2"; second.name = L"Deep Archive";
    answer.list.items = {first, second}; answer.list.unlocked = 1; answer.list.total = 2;
    fake->answer = answer; app.pollXbox();
    const auto* known = app.pluginAchievements(owned);
    if (!known || known->list.total != 2 || known->list.unlocked != 1 || known->source != L"Xbox" || known->pending) return 9359;
    // A failure later keeps the list.
    app.requestXboxAchievements(owned, true);
    xbox_account::AchievementsAnswer failed; failed.title_id = 1717113201; failed.failed = true;
    fake->answer = failed; app.pollXbox();
    known = app.pluginAchievements(owned);
    if (!known || known->list.total != 2) return 9360;
    // Szczegóły' badges: the subscription, the offline lease and the update.
    {
        const auto mine = app.xboxBadges(owned), theirs = app.xboxBadges(pass);
        if (mine.size() != 3 || !mine[0].starts_with(tr(L"Offline do ")) || mine[1] != tr(L"Dostępna aktualizacja") ||
            mine[2] != std::wstring(tr(L"Dodatki: ")) + L"2" ||
            theirs != std::vector<std::wstring>{L"Game Pass"}) return 9363;
        app.openDetails(owned);
        if (!capture(L"xbox-details-badges.png")) return 9364;
        app.back();
    }
    // The Game Pass row on the home screen.
    for (size_t i = 0; i < app.categories_.size(); ++i)
        if (app.categories_[i].id == "gamepass-new") { app.showCategory(i); break; }
    if (!capture(L"xbox-gamepass-row.png")) return 9361;
    std::filesystem::remove_all(folder, ec);
    return 0;
}
