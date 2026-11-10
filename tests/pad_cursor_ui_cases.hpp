#include <xinput.h>

// Tryb TV's pad cursor (src/app/pad_cursor.cpp): L3 + R3 toggles it, the left stick moves the pointer, R2 is
// the left button (not Glance), L2 goes back (not Towarzyszka), and its mark is drawn beside the pointer.
int LauncherUiTest::padCursor(HWND window) {
    Launcher app;
    app.hwnd_ = window; app.execute_ = fakeExecute;
    app.browse_ = [](const std::wstring& url) { dispatched.push_back(url); };
    app.overlay_backend_.foreground = [window] { return window; };
    app.settings_.launch_animation = false;
    if (!app.renderer_.initialize(window)) return 31000;
    auto library = std::make_unique<LibraryMessage>(); library->account = 1;
    for (uint32_t i = 0; i < 8; ++i) { Game game{100 + i, L"Game " + std::to_wstring(i)}; game.last_played = 1000 - i; library->games.push_back(game); }
    app.onLibrary(std::move(library)); app.showCategory(0);
    for (size_t i = 0; i < app.cards_.size(); ++i) app.renderer_.setCover(i, pngFixture(static_cast<uint8_t>(30 + i * 20), 64, 64), {});
    const auto capture = [&](const wchar_t* name) {
        wchar_t folder[4096]{};
        if (!GetEnvironmentVariableW(L"LAUNCHME_UI_CAPTURE_DIR", folder, 4096)) return true;
        return app.renderer_.captureForTest(std::filesystem::path(folder) / name, [&] { app.paint(); });
    };
    PadReading pad{true, L"pad-cursor-fixture"};
    app.controller(pad); // Connection.
    const WORD pair = XINPUT_GAMEPAD_LEFT_THUMB | XINPUT_GAMEPAD_RIGHT_THUMB;
    const auto press = [&](WORD buttons) { pad.buttons = buttons; app.controller(pad); pad.buttons = 0; app.controller(pad); };

    // Off in Settings: the pair does nothing.
    app.settings_.pad_cursor = false;
    press(pair);
    if (app.pad_cursor_.on) return 31001;
    app.settings_.pad_cursor = true;
    // The desktop window has a mouse of its own.
    app.desktop_mode_ = true; press(pair);
    if (app.pad_cursor_.on) return 31002;
    app.desktop_mode_ = false;

    // L3 + R3: on.
    press(pair);
    if (!app.pad_cursor_.on) return 31003;
    // The left stick moves the pointer right and up.
    const float x0 = app.pad_cursor_.x, y0 = app.pad_cursor_.y;
    pad.x = 32767; pad.y = 32767;
    for (int i = 0; i < 4; ++i) { app.controller(pad); Sleep(20); }
    app.controller(pad);
    pad.x = pad.y = 0; app.controller(pad);
    if (!(app.pad_cursor_.x > x0 + 10) || !(app.pad_cursor_.y < y0 - 10)) return 31004;
    // The stick did not move the focus as well.
    if (app.controller_direction_ != 0) return 31005;
    // R2 clicks instead of opening Glance; L2 goes back instead of opening Towarzyszka.
    press(pad_right_trigger);
    if (app.glanceOpen() || app.pad_cursor_.clicking) return 31006;
    pad.left_trigger = true; app.controller(pad); pad.left_trigger = false; app.controller(pad);
    if (app.companionOpen()) return 31007;
    app.paint();
    if (!capture(L"pad-cursor.png")) return 31008;

    // L3 + R3 again: off; R3 held on from the pair reaches nothing until both are let go.
    pad.buttons = pair; app.controller(pad);
    if (app.pad_cursor_.on) return 31009;
    app.controller(pad); app.controller(pad); // Still held: it stays off.
    if (app.pad_cursor_.on) return 31015;
    pad.buttons = XINPUT_GAMEPAD_RIGHT_THUMB; app.controller(pad);
    if ((app.controller_buttons_ & XINPUT_GAMEPAD_RIGHT_THUMB) != 0) return 31010;
    pad.buttons = 0; app.controller(pad);
    if (app.pad_cursor_.sticks_latched) return 31011;
    // Off: R2 is Glance's again.
    pad.buttons = pad_right_trigger; app.controller(pad);
    if (app.glanceAvailable() && !app.glanceOpen()) return 31012;
    pad.buttons = 0; app.controller(pad);
    if (app.glanceOpen()) app.back();

    // A game in front ends it.
    press(pair);
    if (!app.pad_cursor_.on) return 31013;
    app.overlay_backend_.foreground = [] { return HWND{}; };
    app.controller(pad);
    if (app.pad_cursor_.on) return 31014;

    // The CRT filter (Dostosuj wygląd → Inne): the frame goes through it, and its sliders change the picture.
    app.renderer_.gpuCaptureForTest(true);
    app.settings_.crt = true; app.applyCrt();
    if (!capture(L"crt-default.png")) return 31016;
    app.settings_.crt_warp_x = app.settings_.crt_warp_y = 1.0f; app.settings_.crt_mask = 2; app.settings_.crt_mask_level = 1.0f;
    app.settings_.crt_scanlines = 1.0f; app.applyCrt();
    if (!capture(L"crt-strong.png")) return 31017;
    // A click on the bent picture lands where it shows: the centre stays, a corner moves outwards.
    float x = 400, y = 300; app.crtPoint(x, y);
    RECT client{}; GetClientRect(window, &client);
    if (client.right > 0 && !(x < 400)) return 31018;
    app.settings_.crt = false; app.applyCrt();
    return 0;
}
