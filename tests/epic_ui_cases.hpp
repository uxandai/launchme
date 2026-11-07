// Native settings / installer / launch routing. No real account, helper or game.
#include "../src/ui/launch_animation.hpp"
namespace {
class FakeEpic final : public epic::Service {
public:
    std::optional<epic::Snapshot> result;
    std::optional<gog::InstallSnapshot> download;
    std::optional<epic::LaunchResult> start;
    std::vector<epic::Command> commands;
    int installs = 0, launches = 0, cancelled = 0;
    bool active = false;
    void submit(epic::Command c, std::wstring = {}) override { commands.push_back(c); result.reset(); active = false; }
    std::optional<epic::Snapshot> take() override { auto r = std::move(result); result.reset(); return r; }
    bool install(const Game&, gog::InstallRequest) override { ++installs; active = true; return true; }
    bool launch(const Game&) override { ++launches; active = true; return true; }
    void cancelOperation() override { ++cancelled; active = false; }
    bool busy() const override { return active; }
    std::optional<gog::InstallSnapshot> takeInstall() override { auto r = std::move(download); download.reset(); return r; }
    std::optional<epic::LaunchResult> takeLaunch() override { auto r = std::move(start); start.reset(); if (r) active = false; return r; }
};
}
int LauncherUiTest::epicAccount(HWND window) {
    Launcher app; app.hwnd_ = window; app.execute_ = fakeExecute;
    if (!app.renderer_.initialize(window)) return 1700;
    const auto capture = [&](const wchar_t* name) {
        wchar_t folder[4096]{}; if (!GetEnvironmentVariableW(L"LAUNCHME_UI_CAPTURE_DIR", folder, 4096)) return true;
        return app.renderer_.captureForTest(std::filesystem::path(folder) / name, [&] { app.paint(); });
    };
    auto service = std::make_unique<FakeEpic>(); auto* fake = service.get(); app.epic_service_ = std::move(service);
    app.onLibrary(std::make_unique<LibraryMessage>());
    app.openSettings(); app.settings_state_.selectCategory(static_cast<int>(settings_ui::Category::library)); app.platforms_all_ = true; app.buildSettingsRows();
    const auto row = std::find_if(app.settings_rows_.begin(), app.settings_rows_.end(), [](auto& r) { return r.id == settings_ui::Id::pc_platform && r.source == GameSource::epic; });
    if (row == app.settings_rows_.end() || row->value != L"Opcje") return 1701;
    app.settings_state_.selected[app.settings_state_.category] = static_cast<int>(row - app.settings_rows_.begin());
    app.settings_state_.options = app.settings_state_.editing = true; app.settingsActivate();
    if (app.settings_state_.platform() != GameSource::epic || !fake->commands.empty() || app.settings_rows_[0].id != settings_ui::Id::pc_connect) return 1716;
    app.settingsActivate(); // Account -> Connect, inside Epic's options.
    if (fake->commands.back() != epic::Command::connect) return 1702;
    epic::Snapshot signing; signing.state = epic::State::signing_in; signing.browser_url = L"https://example.test/epic";
    fake->result = signing; app.pollEpic();
    if (!app.editingSecret() || app.settings_rows_[app.settings_state_.row()].id != settings_ui::Id::epic_callback || dispatched.back() != signing.browser_url) return 1703;
    app.settings_secret_input_ = L"{\"authorizationCode\":\"SYNTHETICCODE1234\"}";
    if (!capture(L"epic-signin.png")) return 1704;
    app.settingsActivate();
    if (fake->commands.back() != epic::Command::complete || !app.settings_secret_input_.empty()) return 1705;
    Game owned; owned.source = GameSource::epic; owned.kind = GameKind::store; owned.source_id = "TestEpic"; owned.name = L"Epic installation fixture"; owned.installed = false; owned.account_id = "account-A";
    epic::Snapshot connected; connected.state = epic::State::connected; connected.account = "account-A"; connected.games = {owned}; connected.synced_at = 1;
    fake->result = connected; app.pollEpic();
    if (!app.settings_.enabled_pc_sources.contains("epic")) return 1706;
    auto pc = std::make_unique<PcLibraryMessage>(); pc->generation = app.pc_token_->load(); pc->library.visible = {owned}; app.onPcLibrary(std::move(pc));
    app.openPcLibrary(GameSource::epic); app.openEpicInstall(owned);
    if (!app.gogInstallOpen() || fake->installs) return 1707;
    app.gogInstallKey(VK_DOWN, false);
    if (app.gog_install_ui_.row != 3) return 1708;
    app.gogInstallAccept(); if (fake->installs != 1) return 1709;
    gog::InstallSnapshot download; download.product = owned.source_id; download.state = gog::InstallState::downloading; download.done = 3750; download.total = 10000;
    fake->download = download; app.pollEpic();
    if (app.gog_install_ui_.snapshot.progress().percent() != 37 || !capture(L"epic-download.png")) return 1710;
    app.gogInstallAccept(); if (fake->cancelled != 1) return 1711;
    app.gogInstallKey(VK_ESCAPE, false);
    auto local = owned; local.source_id = "LocalEpic"; local.account_id.clear(); local.installed = true; local.epic_managed = true;
    app.store_library_.push_back(local);
    app.startEpicGame(local);
    if (fake->launches != 1) return 1712;
    const auto before = dispatched.size(); fake->start = epic::LaunchResult{local, {L"synthetic-game.exe"}}; app.pollEpic();
    if (dispatched.size() != before + 1 || dispatched.back() != L"synthetic-game.exe") return 1713;
    if (!app.launchAnimationActive() || app.launch_animation_title_ != local.name) return 1728;
    // The game never shows and the launcher stays in front: past its longest wait it fades out.
    app.overlay_backend_.foreground = [window] { return window; };
    const auto launched = *app.launch_animation_started_ + std::chrono::seconds(static_cast<int>(launch_animation::longest) + 1);
    app.tickLaunchAnimation(launched);
    app.tickLaunchAnimation(launched + std::chrono::seconds(1));
    if (app.launchAnimationActive() || app.screen_ != ScreenMode::shelves) return 1729;
    app.epicCommand(epic::Command::logout);
    if (!app.epic_state_.account.empty() || app.store_library_.size() != 1 || !app.store_library_[0].epic_managed) return 1714;
    const auto launches = fake->launches; app.startEpicGame(local); if (fake->launches != launches) return 1715;
    return 0;
}
