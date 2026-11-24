// Settings 2.0 with an injected device service: Dźwięk's source tiles, picker, system volume and
// Bluetooth audio rows; Kontrolery's gallery with its Bluetooth actions; the button hints.
// Nothing reaches Windows: FakeDevices only records the commands.
int LauncherUiTest::settingsDevices(HWND window) {
    using namespace settings_ui;
    Launcher app; app.hwnd_ = window; app.execute_ = fakeExecute; app.settings_.accept_action = AppSettings::AcceptAction::launch; // These cases pick cards to launch them.
    if (!app.renderer_.initialize(window)) return 3200;
    auto fake = std::make_unique<FakeDevices>(); auto* backend = fake.get();
    backend->data.audio[0] = {0.42f, true, false, L"out1"};
    backend->data.audio[1] = {0.6f, true, true, L"in1"};
    backend->data.endpoints = {{L"out1", L"Głośniki · Realtek Audio", false, true}, {L"out2", L"Słuchawki bezprzewodowe", false, false},
                               {L"in1", L"Mikrofon · USB Audio", true, true}};
    backend->data.bluetooth = {{L"bt-head", L"WH-1000XM5", true, true, true}, {L"bt-pad", L"Kontroler testowy", false, false, false},
                               {L"bt-new", L"JBL Flip 6", false, false, true}};
    backend->data.controllers = {{L"usb1", L"DualSense", L"USB", devices::Picture::dualsense, 65, false, true},
                                 {L"bt1", L"Xbox Wireless Controller", L"Bluetooth", devices::Picture::xbox, 40, false, false, L"AA:BB", true},
                                 {L"bt2", L"Pro Controller", L"Bluetooth", devices::Picture::switch_pro, std::nullopt, false, false, L"CC:DD", false}};
    app.device_service_ = std::move(fake);
    auto library = std::make_unique<LibraryMessage>(); library->account = 1; library->games = {Game{620, L"Portal 2"}}; library->games[0].last_played = 100;
    app.onLibrary(std::move(library));
    const auto capture = [&](const wchar_t* name) {
        wchar_t folder[32768]{};
        if (!GetEnvironmentVariableW(L"LAUNCHME_UI_CAPTURE_DIR", folder, static_cast<DWORD>(std::size(folder)))) return true;
        app.settings_motion_.valid = false;
        return app.renderer_.captureForTest(std::filesystem::path(folder) / name, [&] { app.paint(); });
    };
    // The worker's next pass: a new revision, read on the next poll.
    const auto refresh = [&] { ++backend->data.revision; app.settings_device_poll_ = 0; app.pollSettingsDevices(); };
    const auto find = [&](Id id, const std::string& key = {}) {
        const auto row = std::find_if(app.settings_rows_.begin(), app.settings_rows_.end(), [&](const Row& r) { return r.id == id && (key.empty() || r.key == key); });
        return row == app.settings_rows_.end() ? -1 : static_cast<int>(row - app.settings_rows_.begin());
    };
    const auto select = [&](int row) {
        app.settings_state_.options = true; app.settings_state_.editing = false;
        app.settings_state_.selected[app.settings_state_.category] = row; app.ensureSettingVisible();
    };

    // Dźwięk: the service shows its sound panel; before the first pass the tiles say so.
    app.openSettings();
    app.settings_state_.selectCategory(static_cast<int>(Category::audio)); app.buildSettingsRows();
    app.syncSettingsDevices();
    if (app.settings_device_panel_ != devices::Panel::sound || app.settings_devices_ready_ ||
        app.settings_rows_[0].tiles.at(0).title != L"Wczytywanie…") return 3201;
    refresh();
    if (!app.settings_devices_ready_) return 3202;
    {
        const auto& sources = app.settings_rows_[0];
        if (sources.control != Control::sources || sources.tiles.size() != 2 || sources.tiles[0].title != L"Głośniki · Realtek Audio" ||
            sources.tiles[0].choices.size() != 2 || sources.tiles[0].choice != 0 || sources.tiles[0].action.empty() ||
            sources.tiles[1].title != L"Mikrofon · USB Audio" || !sources.tiles[1].muted || !sources.tiles[1].action.empty() ||
            std::abs(sources.tiles[0].level - 0.42f) > 0.001f) return 3203;
    }
    // Volume: the system output first; Bluetooth: headphones and the speaker, never the pad.
    const int volume = find(Id::system_volume);
    if (volume < 0 || app.settings_rows_[volume].value != L"42%" || app.settings_rows_[volume].section.empty()) return 3204;
    if (find(Id::bt_audio, "bt-head") < 0 || find(Id::bt_audio, "bt-new") < 0 || find(Id::bt_audio, "bt-pad") >= 0 ||
        app.settings_rows_[find(Id::bt_audio, "bt-head")].value != L"Ustawienia" || app.settings_rows_[find(Id::bt_audio, "bt-new")].value != L"Sparuj" ||
        find(Id::bt_audio, "bt-head") > find(Id::bt_audio, "bt-new") || find(Id::bt_devices) < 0 || find(Id::windows_sound) < 0) return 3205;
    if (!capture(L"settings-devices-audio.png")) return 3206;
    app.desktop_mode_ = true;
    if (!capture(L"desktop-settings-devices-audio.png")) return 3206;
    app.desktop_mode_ = false;
    // Right/Left between the tiles; A on the output lists its devices, picking one switches it.
    app.settings_state_.enter(static_cast<int>(app.settings_rows_.size())); select(0);
    app.settingsAdjust(1);
    if (app.settings_state_.tile != 1 || !app.settings_state_.options) return 3207;
    app.settingsAdjust(1); // The last tile: an edge, not a category change.
    if (app.settings_state_.tile != 1 || !app.settings_state_.options) return 3207;
    app.settingsAdjust(-1); app.settingsActivate();
    if (!app.settings_state_.editing || app.settings_state_.choice != 0) return 3208;
    if (!capture(L"settings-devices-output-picker.png")) return 3209;
    app.settingsMove(1); app.settingsActivate();
    if (app.settings_state_.editing || backend->actions.empty() || backend->actions.back().action != devices::Action::endpoint ||
        backend->actions.back().id != L"out2" || backend->actions.back().input ||
        app.settings_rows_[0].tiles[0].title != L"Słuchawki bezprzewodowe") return 3210;
    // B in the picker cancels without a command.
    const auto commands = backend->actions.size();
    app.settingsActivate(); app.settingsMove(-1); app.back();
    if (app.settings_state_.editing || backend->actions.size() != commands) return 3211;
    // The picker rises and fades in; its highlight glides to the next choice.
    {
        app.picker_motion_for_test_ = true;
        app.paint(); // The closed picker, as last painted.
        app.settingsActivate(); app.paint();
        if (!app.settings_picker_open_ || GetTickCount64() - app.settings_picker_changed_ > 150) return 3240;
        app.settings_picker_changed_ = GetTickCount64() - 85; // Half way.
        if (!capture(L"settings-devices-picker-rising.png")) return 3241;
        app.tick(); // Snaps the highlight onto the current choice.
        app.settingsMove(app.settings_state_.choice ? -1 : 1);
        const float target = static_cast<float>(app.settings_state_.choice);
        app.last_frame_ -= std::chrono::milliseconds(16); app.tick();
        const float glide = app.settings_choice_glide_.value.value;
        if (!app.settings_choice_glide_.valid || std::abs(glide - target) < 0.02f || std::abs(glide - target) > 0.99f) return 3242;
        app.back(); app.paint();
        if (app.settings_picker_open_ || app.settings_picker_row_ != 0) return 3243; // Fades out with its last list.
        app.picker_motion_for_test_ = false;
    }
    // LB/RB (PgUp/PgDn) on a source tile: its volume in 5% steps, with the hint shown.
    select(0); app.settings_state_.tile = 1;
    {
        ViewContext hints; app.inputHints(hints);
        if (std::none_of(hints.hints.begin(), hints.hints.end(), [](const InputHint& hint) {
                return hint.button == HintButton::shoulders && hint.label == L"Głośność"; })) return 3244;
    }
    app.settingsShoulder(-1);
    if (backend->actions.back().action != devices::Action::volume || !backend->actions.back().input ||
        std::abs(backend->actions.back().value - 0.55f) > 0.001f || std::abs(app.settings_rows_[0].tiles[1].level - 0.55f) > 0.001f) return 3245;
    app.settings_state_.tile = 0;
    // The system volume slider: one step down is a command for the default output.
    select(find(Id::system_volume)); app.settingsActivate(); app.settingsAdjust(-1);
    if (backend->actions.back().action != devices::Action::volume || std::abs(backend->actions.back().value - 0.4f) > 0.001f ||
        backend->actions.back().id != L"out1" || app.settings_rows_[find(Id::system_volume)].value != L"40%") return 3212;
    app.settings_state_.editing = false;
    // A stale snapshot does not move the slider back before Windows acknowledged it.
    refresh();
    if (app.settings_rows_[find(Id::system_volume)].value != L"40%") return 3213;
    // Pairing the speaker in pairing mode; the paired headphones open Windows' Bluetooth page.
    select(find(Id::bt_audio, "bt-new")); app.settingsActivate();
    if (backend->actions.back().action != devices::Action::pair || backend->actions.back().id != L"bt-new") return 3214;
    dispatched.clear();
    select(find(Id::bt_audio, "bt-head")); app.settingsActivate();
    if (dispatched != std::vector<std::wstring>{L"ms-settings:bluetooth"}) return 3215;

    // Kontrolery: the gallery of pads (Bluetooth ones with their action) and "Sparuj kontroler".
    app.settings_state_.selectCategory(static_cast<int>(Category::controllers)); app.buildSettingsRows();
    app.syncSettingsDevices(); refresh();
    if (app.settings_device_panel_ != devices::Panel::controllers) return 3216;
    {
        const auto& pads = app.settings_rows_[0];
        if (pads.control != Control::pads || pads.tiles.size() != 4 || pads.tiles[0].title != L"DualSense" || !pads.tiles[0].action.empty() ||
            pads.tiles[1].action != L"Rozłącz" || pads.tiles[2].action != L"Połącz" || pads.tiles[2].connected ||
            !pads.tiles[3].pair || pads.tiles[0].picture != devices::Picture::dualsense) return 3217;
    }
    app.settings_state_.enter(static_cast<int>(app.settings_rows_.size())); select(0);
    app.settingsAdjust(1);
    if (!capture(L"settings-devices-controllers.png")) return 3218;
    app.settingsActivate();
    if (backend->actions.back().action != devices::Action::disconnect || backend->actions.back().id != L"AA:BB") return 3219;
    app.settingsAdjust(1); app.settingsAdjust(1);
    if (app.settings_state_.tile != 3) return 3220;
    // "Sparuj kontroler" opens the Bluetooth panel; Back returns to Kontrolery, not to another panel.
    app.settingsActivate();
    if (!app.devicesOpen() || app.device_state_.panel != devices::Panel::bluetooth) return 3221;
    app.syncSettingsDevices();
    if (app.settings_device_panel_) return 3222;
    app.back();
    if (!app.settingsOpen() || app.settings_state_.category != static_cast<int>(Category::controllers)) return 3223;
    app.syncSettingsDevices();
    if (app.settings_device_panel_ != devices::Panel::controllers) return 3224;
    // Windows' Bluetooth off: a paired pad stays, greyed, and its action opens Windows' Bluetooth page.
    {
        devices::Controller off{L"bluetooth:4cb99b80d184", L"DualSense Wireless Controller", L"Bluetooth", devices::Picture::dualsense};
        off.connected = false; off.radio_off = true;
        backend->data.controllers.push_back(off); refresh();
        const auto& pads = app.settings_rows_[0];
        if (pads.tiles.size() != 5 || pads.tiles[3].value != L"wyłączony" || pads.tiles[3].connected ||
            pads.tiles[3].action != L"Ustawienia" || !pads.tiles[3].key.empty() || !pads.tiles[4].pair) return 3246;
        select(0); app.settings_state_.tile = 3; dispatched.clear();
        if (!capture(L"settings-devices-controllers-radio-off.png")) return 3247;
        app.settingsActivate();
        if (dispatched != std::vector<std::wstring>{L"ms-settings:bluetooth"} || app.devicesOpen()) return 3248;
        backend->data.controllers.pop_back(); refresh();
    }

    // Hints: the active pad's family by position, keys after the keyboard, none when switched off.
    ViewContext context;
    app.pad_connected_ = true; app.pad_input_ = true; app.controller_name_ = L"DualSense Wireless Controller";
    app.inputHints(context);
    if (context.hint_device != HintDevice::playstation || context.hints.empty() || context.hints.front().button != HintButton::accept ||
        context.hints.back().button != HintButton::back) return 3225;
    if (!capture(L"settings-devices-hints-playstation.png")) return 3226;
    app.settings_.hint_glyphs = AppSettings::HintGlyphs::nintendo;
    app.inputHints(context);
    if (context.hint_device != HintDevice::nintendo) return 3227;
    app.settings_.hint_glyphs = AppSettings::HintGlyphs::automatic;
    app.noteDesktopInput();
    app.inputHints(context);
    if (context.hint_device != HintDevice::keyboard || context.hints.empty()) return 3228;
    app.settings_.input_hints = false;
    app.inputHints(context);
    if (!context.hints.empty()) return 3229;
    app.settings_.input_hints = true;
    // Home: play, the game menu and search on the pad; the drawer and the menu with keys.
    app.settings_state_.options = false; app.back();
    if (app.settingsOpen()) return 3230;
    app.syncSettingsDevices();
    if (app.settings_device_panel_) return 3231; // Leaving Settings releases the service.
    app.showCategory(0);
    const auto played = std::find_if(app.cards_.begin(), app.cards_.end(), [](const GameCard& card) { return isGame(card.game); });
    if (played == app.cards_.end()) return 3232;
    app.focusCard(static_cast<size_t>(played - app.cards_.begin()));
    app.pad_input_ = true; app.controller_name_ = L"Xbox Wireless Controller";
    app.inputHints(context);
    if (context.hint_device != HintDevice::xbox || context.hints.size() < 4 || context.hints[0].label != app.actionLabel(app.cards_[app.selected_].game) ||
        context.hints[1].button != HintButton::menu || context.hints[2].button != HintButton::search) return 3232;
    for (size_t i = 0; i < app.cards_.size(); ++i) {
        auto& card = app.cards_[i];
        card.size = i == app.selected_ ? 330.0f : 165.0f; card.x = 57.0f + i * 185.0f; card.y = 735.0f - card.size; card.opacity = 1;
    }
    if (!capture(L"home-hints-xbox.png")) return 3233;
    app.openQuick();
    app.inputHints(context);
    if (!context.hints.empty()) return 3234; // The bottom bar must not overlap a second set of prompts.
    app.closeQuick();

    // Dźwięki interfejsu: one set (or Brak), its volume with a mute that keeps it, and a switch per moment.
    app.openSettings();
    app.settings_state_.selectCategory(static_cast<int>(Category::audio)); app.buildSettingsRows();
    app.openSettingsPage(Page::sounds);
    const int set_row = find(Id::ui_sound);
    if (set_row < 0 || app.settings_rows_[static_cast<size_t>(set_row)].choices.size() != ui_sounds.size() ||
        find(Id::ui_volume) < 0 || find(Id::ui_muted) < 0 || find(Id::ui_sound_moves) < 0 || find(Id::ui_sound_menus) < 0 ||
        find(Id::ui_sound_launch) < 0 || find(Id::toast_sound) < 0 || find(Id::boot_sound) < 0 ||
        app.settings_rows_[static_cast<size_t>(find(Id::toast_sound))].control != Control::toggle) return 3250;
    if (!app.audio_.ui_.on || !app.audio_.ui_.moves || std::abs(app.audio_.ui_.volume - 1.0f) > 0.001f) return 3251;
    select(find(Id::ui_muted)); app.settingsActivate();
    if (!app.settings_.ui_muted || app.audio_.ui_.on || app.settings_rows_[static_cast<size_t>(find(Id::ui_volume))].value != L"100%") return 3252;
    if (!capture(L"settings-sounds.png")) return 3253;
    select(find(Id::ui_muted)); app.settingsActivate();
    select(find(Id::ui_volume)); app.settingsSetLevel(0.5f);
    if (app.settings_.ui_muted || !app.audio_.ui_.on || app.settings_.ui_volume != 50 || std::abs(app.audio_.ui_.volume - 0.5f) > 0.001f) return 3254;
    select(find(Id::ui_sound_moves)); app.settingsActivate();
    select(find(Id::ui_sound_launch)); app.settingsActivate();
    if (app.settings_.ui_sound_moves || app.audio_.ui_.moves || app.settings_.ui_sound_launch || app.audio_.ui_.launch || !app.audio_.ui_.menus) return 3255;
    const int toast_before = app.settings_.toast_sound;
    select(find(Id::toast_sound)); app.settingsActivate();
    if (app.settings_.toast_sound == toast_before) return 3256;
    // Brak: silence, notifications included; the set's other rows go, the start sound stays.
    select(set_row); app.settingsActivate(); app.settings_state_.choice = ui_sound_none; app.settingsActivate();
    if (app.settings_.ui_sound != ui_sound_none || app.audio_.ui_.on || find(Id::ui_volume) >= 0 || find(Id::toast_sound) >= 0 ||
        find(Id::boot_sound) < 0) return 3257;
    select(find(Id::ui_sound)); app.settingsActivate(); app.settings_state_.choice = 0; app.settingsActivate();
    if (app.settings_.ui_sound != 0 || !app.audio_.ui_.on || find(Id::ui_volume) < 0) return 3258;
    return 0;
}
