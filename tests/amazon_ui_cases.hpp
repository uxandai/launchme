// Amazon Games: Nile offered when the platform is switched on, the sign-in address, the library, the
// install dialog and launch/uninstall routing. A fake service: no real helper, account or game.
namespace {
class FakeAmazon final : public amazon::Service {
public:
    std::optional<amazon::Snapshot> result;
    std::optional<gog::InstallSnapshot> download;
    std::vector<amazon::Command> commands;
    std::vector<std::wstring> inputs;
    gog::InstallRequest last_request;
    int installs = 0, cancelled = 0;
    bool active = false;
    void submit(amazon::Command c, std::wstring input = {}) override { commands.push_back(c); inputs.push_back(std::move(input)); }
    std::optional<amazon::Snapshot> take() override { auto r = std::move(result); result.reset(); return r; }
    bool install(const Game&, gog::InstallRequest request) override { ++installs; active = true; last_request = std::move(request); return true; }
    std::optional<gog::InstallSnapshot> takeInstall() override { auto r = std::move(download); download.reset(); return r; }
    void cancelOperation() override { ++cancelled; active = false; }
    bool busy() const override { return active; }
};
}
int LauncherUiTest::amazonGames(HWND window) {
    using settings_ui::Id;
    Launcher app; app.hwnd_ = window; app.execute_ = fakeExecute;
    if (!app.renderer_.initialize(window)) return 9600;
    const auto capture = [&](const wchar_t* name) {
        wchar_t folder[4096]{}; if (!GetEnvironmentVariableW(L"LAUNCHME_UI_CAPTURE_DIR", folder, 4096)) return true;
        app.settings_motion_.valid = false;
        return app.renderer_.captureForTest(std::filesystem::path(folder) / name, [&] { app.paint(); });
    };
    const auto select = [&](Id id) {
        const auto row = std::find_if(app.settings_rows_.begin(), app.settings_rows_.end(), [&](const auto& r) { return r.id == id; });
        if (row == app.settings_rows_.end()) return false;
        app.settings_state_.selected[app.settings_state_.category] = static_cast<int>(row - app.settings_rows_.begin());
        app.settings_state_.options = true; app.settings_state_.editing = false; app.ensureSettingVisible(); return true;
    };
    auto service = std::make_unique<FakeAmazon>(); auto* fake = service.get(); app.amazon_service_ = std::move(service);
    app.onLibrary(std::make_unique<LibraryMessage>());
    app.openSettings(); app.settings_state_.selectCategory(static_cast<int>(settings_ui::Category::library)); app.buildSettingsRows();
    // Off, it waits in "Więcej platform".
    const auto fold = std::find_if(app.settings_rows_.begin(), app.settings_rows_.end(), [](const auto& r) { return r.id == Id::platforms_more; });
    if (fold == app.settings_rows_.end() || fold->checked) return 9690;
    app.settings_state_.selected[app.settings_state_.category] = static_cast<int>(fold - app.settings_rows_.begin());
    app.settings_state_.options = true; app.settings_state_.editing = false; app.settingsActivate();
    // Amazon Games is one of the platforms, by its name; switching it on without Nile offers the download.
    const auto platform = std::find_if(app.settings_rows_.begin(), app.settings_rows_.end(), [](const auto& r) { return r.id == Id::pc_platform && r.source == GameSource::amazon; });
    if (platform == app.settings_rows_.end() || platform->checked || platform->title != L"Amazon Games") return 9601;
    app.settings_state_.selected[app.settings_state_.category] = static_cast<int>(platform - app.settings_rows_.begin());
    app.settings_state_.options = true; app.settings_state_.editing = false; app.settingsActivate();
    if (!app.settings_.enabled_pc_sources.contains("amazon") || app.settings_state_.platform() != GameSource::amazon ||
        app.settings_rows_.empty() || app.settings_rows_[0].id != Id::amazon_helper || app.settings_state_.row() != 0 || app.settings_rows_[0].locked) return 9602;
    if (!capture(L"amazon-settings-helper.png")) return 9603;
    app.settingsActivate(); // Pobierz.
    if (fake->commands.empty() || fake->commands.back() != amazon::Command::download) return 9604;
    amazon::Snapshot ready; ready.helper = true;
    fake->result = ready; app.pollAmazon();
    if (!app.amazon_state_.helper || app.settings_rows_[0].value != tr(L"Usuń")) return 9605;
    // Konto → Połącz: Amazon's page opens in the browser and the address field takes the focus.
    if (!select(Id::pc_connect)) return 9606;
    app.settingsActivate();
    if (fake->commands.back() != amazon::Command::connect) return 9606;
    amazon::Snapshot signing = ready; signing.state = amazon::State::signing_in; signing.browser_url = L"https://amazon.com/ap/signin?fixture=1";
    fake->result = signing; app.pollAmazon();
    if (!app.editingSecret() || app.settings_rows_[app.settings_state_.row()].id != Id::amazon_callback || dispatched.empty() || dispatched.back() != signing.browser_url) return 9607;
    const std::wstring pasted = L"https://www.amazon.com/?openid.oa2.authorization_code=SYNTHETIC1234";
    for (const wchar_t c : pasted) app.onCharacter(c); // A long address fits (the field takes up to 8192 characters).
    if (app.settings_secret_input_ != pasted || !capture(L"amazon-signin.png")) return 9608;
    app.settingsActivate();
    if (fake->commands.back() != amazon::Command::complete || fake->inputs.back() != pasted || !app.settings_secret_input_.empty()) return 9609;
    amazon::Snapshot connected = ready; connected.state = amazon::State::connected; connected.account = "amzn1.account.TEST";
    connected.name = L"Pat"; connected.games = 1; connected.synced_at = 1790000000;
    fake->result = connected; app.pollAmazon();
    if (!app.amazonConnected() || app.settings_rows_.back().id != Id::amazon_logout ||
        std::none_of(app.settings_rows_.begin(), app.settings_rows_.end(), [](const auto& r) { return r.id == Id::amazon_refresh; })) return 9610;
    if (!capture(L"amazon-settings-connected.png")) return 9611;
    if (!select(Id::amazon_refresh)) return 9621;
    app.settingsActivate();
    if (fake->commands.back() != amazon::Command::sync) return 9621;
    // An account game: installed through the launcher's dialog, into one game folder.
    Game owned; owned.source = GameSource::amazon; owned.kind = GameKind::store; owned.source_id = "amzn1.adg.product.0b1d1c6e-3f1a-4a6b-9e0f-1c2d3e4f5a6b";
    owned.name = L"Moonlit: Archive"; owned.installed = false; owned.account_id = "amzn1.account.TEST";
    auto pc = std::make_unique<PcLibraryMessage>(); pc->generation = app.pc_token_->load(); pc->library.visible = {owned}; app.onPcLibrary(std::move(pc));
    app.openPcLibrary(GameSource::amazon);
    if (app.cards_.empty() || app.cards_[0].game.source != GameSource::amazon || !app.actionEnabled(app.cards_[0].game) ||
        std::wstring(primaryActionLabel(owned)) != tr(L"Instaluj")) return 9612;
    app.launchGame(owned);
    if (!app.gogInstallOpen() || app.gog_install_ui_.game.source != GameSource::amazon ||
        app.gog_install_ui_.request.install_directory.filename() != L"Amazon-Moonlit Archive") return 9613;
    app.gogInstallKey(VK_DOWN, false); // The folder, then the action: Nile writes straight into the game's folder.
    if (app.gog_install_ui_.row != 3) return 9614;
    app.gogInstallAccept();
    if (fake->installs != 1 || fake->last_request.install_directory != app.gog_install_ui_.request.install_directory) return 9615;
    gog::InstallSnapshot download; download.product = owned.source_id; download.state = gog::InstallState::downloading;
    download.done = 512ull * 1024 * 1024; download.total = 2048ull * 1024 * 1024;
    fake->download = download; app.pollAmazon();
    if (app.gog_install_ui_.snapshot.progress().percent() != 25 || !capture(L"amazon-download.png")) return 9616;
    app.gogInstallAccept(); // Przerwij.
    if (fake->cancelled != 1) return 9617;
    app.gogInstallKey(VK_ESCAPE, false);
    // An installed game: uninstalled by Nile after the question, started from its own executable.
    auto local = owned; local.installed = true; local.install_directory = L"C:\\Games\\Amazon-Moonlit Archive";
    local.play_action = {L"C:\\Games\\Amazon-Moonlit Archive\\Game.exe", {}, local.install_directory, ActionKind::play};
    local.play_action.environment = {{L"AMAZON_GAMES_FUEL_ENTITLEMENT_ID", L"ent-1"}};
    if (!app.canUninstall(local)) return 9618;
    app.uninstallGame(local);
    if (app.prompt_kind_ != Launcher::PromptKind::uninstall || app.uninstall_game_.source_id != local.source_id) return 9622;
    app.exit_prompt_ = 0; app.prompt_kind_ = Launcher::PromptKind::exit; app.uninstall_game_ = {};
    const auto before = dispatched.size();
    app.launchGame(local); // The launch animation's start; the action follows it.
    if (!app.pending_launch_ || app.pending_game_.play_action.environment.size() != 1) return 9619;
    app.finishLaunch();
    if (dispatched.size() != before + 1 || dispatched.back() != local.play_action.target) return 9623;
    app.amazonCommand(amazon::Command::logout);
    if (fake->commands.back() != amazon::Command::logout) return 9620;
    return 0;
}
