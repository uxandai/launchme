// Modal layers have one order, the one they are drawn in: the topmost open layer takes keys, pad
// buttons and clicks, and the layer beneath keeps its state until it is on top again.
int LauncherUiTest::modalLayers(HWND window) {
    using Modal = Launcher::Modal;
    Launcher app;
    app.hwnd_ = window; app.execute_ = fakeExecute;
    app.settings_.launch_animation = false;
    app.overlay_backend_.foreground = [window] { return window; };
    app.power_actions_ = [] { return std::vector<PowerAction>{PowerAction::sleep}; };
    app.power_ = [](PowerAction) { return true; };
    if (!app.renderer_.initialize(window)) return 6900;
    auto library = std::make_unique<LibraryMessage>(); library->account = 1;
    library->games = {Game{6901, L"Modal fixture"}};
    app.onLibrary(std::move(library));
    app.showCategory(0);
    if (app.topModal() != Modal::none || app.modalKey(VK_ESCAPE, false)) return 6901;

    // Paint order, bottom first; every layer hides the home widgets and the hints.
    const std::vector<std::pair<Modal, std::function<void(bool)>>> layers{
        {Modal::game_case, [&](bool on) { app.game_case_.open = on; }}, // Półka's open case: the library's own layer.
        {Modal::profile, [&](bool on) { app.profile_.open = on; }},
        {Modal::tarot, [&](bool on) { app.tarot_.open = on; }},
        {Modal::gog_install, [&](bool on) { app.gog_install_ui_.open = on; }},
        {Modal::bulk_covers, [&](bool on) { app.bulk_ui_.open = on; }},
        {Modal::metadata, [&](bool on) { app.desktop_meta_.open = on; }},
        {Modal::custom_editor, [&](bool on) { app.custom_editor_.open = on; }},
        {Modal::identify, [&](bool on) { app.identify_state_.open = on; }}, // "Zidentyfikuj grę" of the editor opens over it.
        {Modal::library_dialog, [&](bool on) { app.library_dialog_.open = on; }},
        {Modal::aurelia, [&](bool on) { app.aurelia_ui_.open = on; }},
        {Modal::file_browser, [&](bool on) { app.file_browser_.open = on; }}, // Aurelia's "Inny folder…" opens it over Aurelia.
        {Modal::whats_new, [&](bool on) { app.whats_new_.open = on; }},
        {Modal::tour, [&](bool on) { app.tour_.open = on; }},
        {Modal::power_menu, [&](bool on) { if (on) app.power_menu_ = 0; else app.power_menu_.reset(); }},
        {Modal::exit_prompt, [&](bool on) { if (on) app.exit_prompt_ = 0; else app.exit_prompt_.reset(); }},
    };
    for (const auto& [modal, open] : layers) {
        open(true);
        if (app.topModal() != modal || app.homeOverlayAvailable()) return 6902;
        ViewContext context; app.inputHints(context);
        if (!context.hints.empty()) return 6903;
    }
    if (app.topDialog() != Modal::whats_new) return 6904; // Back and typing skip the prompts and the tour.
    for (auto layer = layers.rbegin(); layer != layers.rend(); ++layer) {
        if (app.topModal() != layer->first) return 6905;
        layer->second(false);
    }
    if (app.topModal() != Modal::none || !app.homeOverlayAvailable()) return 6906;

    // The power menu over the installer (Guide held on the pad): Home stays the Guide gesture, and
    // the menu, drawn on top, takes keys and clicks; the installer stays open beneath it.
    app.gog_install_ui_.open = true;
    if (app.topModal() != Modal::gog_install || app.modalKey(VK_HOME, false) || !app.homeKeyIsGuide()) return 6907;
    app.openPowerMenu();
    if (app.topModal() != Modal::power_menu || app.modalKey(VK_HOME, false)) return 6908;
    const auto view = app.renderer_.viewport();
    app.click(4 * view.scale, 4 * view.scale, false); // Outside the menu: it closes.
    if (app.powerMenuOpen() || !app.gogInstallOpen() || app.topModal() != Modal::gog_install) return 6909;
    app.openPowerMenu();
    if (!app.modalKey(VK_ESCAPE, false) || app.powerMenuOpen() || !app.gogInstallOpen()) return 6910;
    app.gog_install_ui_ = {};

    // The exit prompt over the power menu (closing the window while it is open): pad B answers
    // the prompt first, as Esc and a click do; the menu stays beneath it.
    PadReading pad{true, L"modal-fixture"}; app.controller(pad); // Connection: swallowed.
    const auto press = [&](WORD button) { pad.buttons = button; app.controller(pad); pad.buttons = 0; app.controller(pad); };
    app.openPowerMenu(); app.exit_prompt_ = 0;
    if (app.topModal() != Modal::exit_prompt) return 6911;
    press(XINPUT_GAMEPAD_B);
    if (app.exitPromptOpen() || !app.powerMenuOpen() || app.topModal() != Modal::power_menu) return 6912;
    press(XINPUT_GAMEPAD_B);
    if (app.powerMenuOpen() || app.topModal() != Modal::none) return 6913;
    // D-pad moves the prompt's choice, not the menu beneath it.
    app.openPowerMenu(); app.exit_prompt_ = 0;
    press(XINPUT_GAMEPAD_DPAD_DOWN);
    if (app.exit_prompt_ != 1 || app.power_menu_ != 0) return 6914;
    app.exit_prompt_.reset(); app.closePowerMenu();
    return 0;
}
