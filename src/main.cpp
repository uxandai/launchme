#include "app/launcher.hpp"
#include "app/message_dispatch.hpp"
#include "platform/diagnostics.hpp"
#include "platform/frame_clock.hpp"
#include "platform/text_encoding.hpp"

#include <Windows.h>
#include <windowsx.h>
#include <dwmapi.h>

#include <memory>
#include <string>
#ifndef _WIN32
#include "linux/window/window_system.hpp"
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <future>
#include <thread>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/eventfd.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/un.h>
#include <unistd.h>
#endif

namespace {
// WM_COPYDATA from a second launchme.exe started by a Jump List entry (src/app/jump_list.cpp),
// or by a plain second start ("show": the running launcher comes to the front instead).
constexpr ULONG_PTR jump_copy_data = 0x4C4D4A50;
HWND main_window = nullptr;
#ifdef _WIN32
// GUID_CONSOLE_DISPLAY_STATE (WinNT.h), defined here so no GUID library is needed.
constexpr GUID console_display_state{0x6fe69556, 0x704a, 0x47a0, {0x8f, 0x24, 0xc2, 0x8d, 0x93, 0x6f, 0xda, 0x47}};
#endif
constexpr WPARAM DBT_DEVNODES_CHANGED_VALUE = 0x0007; // Dbt.h: a device was added or removed.
#ifdef _WIN32

// EVENT_SYSTEM_FOREGROUND for every process but ours: a game that starts outside the launcher
// takes the foreground, so this replaces a periodic process scan (Launcher::foregroundChanged).
// It also dates each program's arrival in front, for the return after a game (returnFromGame).
void CALLBACK foregroundEvent(HWINEVENTHOOK, DWORD, HWND window, LONG, LONG, DWORD, DWORD) {
    if (!main_window || window == main_window) return;
    if (auto* app = reinterpret_cast<Launcher*>(GetWindowLongPtrW(main_window, GWLP_USERDATA)); app && !app->closing())
        app->foregroundChanged(window);
}

// A "play:" command's end (Launchme's shortcut in Steam, src/app/game_links.cpp): Steam sees the game
// run while the launchme.exe it started lives, so that one waits on this event, which the running
// launcher sets once the game's session ends (Launcher::finishedPlays).
std::wstring playEventName(const std::filesystem::path& data_folder, const std::wstring& command) {
    return L"Local\\Launchme-play-" + std::to_wstring(std::hash<std::wstring>{}(data_folder.lexically_normal().wstring())) + L"-" +
           std::to_wstring(std::hash<std::wstring>{}(command));
}

// One launcher per data folder: a second start brings the first one to the front (or hands it
// its Jump List command). The mutex lives until this process ends, also through its shutdown.
// nullptr: another launcher took over (this process ends; a play: command once its game ended).
// Otherwise this process continues, owning the returned mutex (INVALID_HANDLE_VALUE: none could be
// had; start anyway).
HANDLE singleInstance(const std::filesystem::path& data_folder, const std::wstring& command) {
    const std::wstring name = L"Local\\Launchme-" + std::to_wstring(std::hash<std::wstring>{}(data_folder.lexically_normal().wstring()));
    const HANDLE mutex = CreateMutexW(nullptr, TRUE, name.c_str());
    if (!mutex) return INVALID_HANDLE_VALUE;
    if (GetLastError() != ERROR_ALREADY_EXISTS) return mutex;
    std::wstring text = command.empty() ? std::wstring(L"show") : command;
    // Created before the command goes, so its end cannot come first.
    const HANDLE play_done = text.starts_with(L"play:") ? CreateEventW(nullptr, TRUE, FALSE, playEventName(data_folder, text).c_str()) : nullptr;
    if (play_done) ResetEvent(play_done); // An earlier sender's, already set: wait for this start's game.
    // The other launcher may still be starting (no window yet) or closing (window gone, mutex
    // released when its process ends): look for its window, or take the mutex once it is free.
    for (int attempt = 0; attempt < 100; ++attempt) {
        if (HWND running = FindWindowW(L"LaunchmeNativeWindow", nullptr)) {
            DWORD process = 0;
            GetWindowThreadProcessId(running, &process);
            AllowSetForegroundWindow(process); // Its window comes forward, as the click asked.
            COPYDATASTRUCT data{jump_copy_data, static_cast<DWORD>((text.size() + 1) * sizeof(wchar_t)), text.data()};
            DWORD_PTR accepted = 0;
            if (SendMessageTimeoutW(running, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&data), SMTO_ABORTIFHUNG, 1000, &accepted) && accepted) {
                CloseHandle(mutex);
                if (play_done) {
                    // Until its game ends, or the launcher does (then nothing would set the event).
                    const HANDLE launcher = OpenProcess(SYNCHRONIZE, FALSE, process);
                    const HANDLE handles[] = {play_done, launcher};
                    WaitForMultipleObjects(launcher ? 2 : 1, handles, FALSE, INFINITE);
                    if (launcher) CloseHandle(launcher);
                    CloseHandle(play_done);
                }
                return nullptr;
            }
        }
        const DWORD waited = WaitForSingleObject(mutex, 100);
        if (waited == WAIT_OBJECT_0 || waited == WAIT_ABANDONED) { // It ended: this one is the launcher now.
            if (play_done) CloseHandle(play_done);
            return mutex;
        }
    }
    if (play_done) CloseHandle(play_done);
    CloseHandle(mutex);
    return INVALID_HANDLE_VALUE; // It neither answers nor ends: start rather than leave the user with nothing.
}
#endif

LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    auto* app = reinterpret_cast<Launcher*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    const bool closing = app && app->closing();
    // Keep owning/freeing posted payloads, without changing the departing scene.
    if (dispatchWorkerMessage(closing ? nullptr : app, message, lparam)) return 0;
    if (closing) {
        if ((message >= WM_KEYFIRST && message <= WM_KEYLAST) ||
            (message >= WM_MOUSEFIRST && message <= WM_MOUSELAST) ||
            message == WM_HOTKEY || message == WM_TIMER || message == WM_INPUT_DEVICE_CHANGE) return 0;
        if (message == WM_INPUT) return DefWindowProcW(hwnd, message, wparam, lparam);
        if (message == WM_SETCURSOR && LOWORD(lparam) == HTCLIENT) { SetCursor(nullptr); return TRUE; }
    }
    // The notification-area icon while a game runs (src/app/tray.cpp), and Explorer's restart, after
    // which the icon must be added again.
    static const UINT taskbar_created = RegisterWindowMessageW(L"TaskbarCreated");
    if (app && message == Launcher::tray_message) { if (!closing) app->trayIconEvent(wparam, lparam); return 0; }
    if (app && taskbar_created && message == taskbar_created) { if (!closing) app->taskbarCreated(); return 0; }
    // Screensaver: while it runs, and for the rest of the input that ends it, keyboard
    // and mouse messages never reach the screen beneath.
    if (app && app->screensaverInput(message, wparam, lparam)) return 0;
    if (app && app->launchAnimationActive()) {
        // Back hides the starting game's picture; every other key and click waits for its end.
        if ((message == WM_KEYDOWN && (wparam == VK_ESCAPE || wparam == VK_BACK) && !(lparam & (1LL << 30))) ||
            (message == WM_XBUTTONUP && GET_XBUTTON_WPARAM(wparam) == XBUTTON1)) app->dismissLaunchAnimation();
        if ((message >= WM_KEYFIRST && message <= WM_KEYLAST) ||
            (message >= WM_MOUSEFIRST && message <= WM_MOUSELAST)) return 0;
        if (message == WM_SETCURSOR && LOWORD(lparam) == HTCLIENT) { SetCursor(nullptr); return TRUE; }
    }
    switch (message) {
    case WM_CREATE: {
        auto instance = std::make_unique<Launcher>();
        if (!instance->initialize(hwnd)) return -1;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(instance.release()));
        return 0;
    }
#ifdef _WIN32
    case WM_INPUT:
        if (app) app->rawControllerInput(reinterpret_cast<HRAWINPUT>(lparam));
        break; // DefWindowProc releases foreground Raw Input resources.
    case WM_INPUT_DEVICE_CHANGE:
        if (app) app->controllerDeviceChanged(reinterpret_cast<HANDLE>(lparam));
        return 0;
#endif
    case WM_SIZE:
        if (app) {
            app->resize();
            if (wparam == SIZE_MAXIMIZED || wparam == SIZE_RESTORED) app->windowPlacementChanged();
        }
        return 0;
    // Desktop mode draws its own top bar: the caption becomes client area (the side and bottom
    // frames keep resizing), the top bar answers as caption and maximize button (snap layouts).
    case WM_NCCALCSIZE:
        if (app && app->desktopMode() && wparam) {
            auto* params = reinterpret_cast<NCCALCSIZE_PARAMS*>(lparam);
            const LONG top = params->rgrc[0].top;
            const LRESULT result = DefWindowProcW(hwnd, message, wparam, lparam);
            params->rgrc[0].top = top;
            if (IsZoomed(hwnd)) { // A maximized window reaches past the monitor by its frame.
                const UINT dpi = GetDpiForWindow(hwnd);
                params->rgrc[0].top += GetSystemMetricsForDpi(SM_CYFRAME, dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
            }
            return result;
        }
        break;
    case WM_NCHITTEST:
        if (app && app->desktopMode()) {
            const LRESULT hit = DefWindowProcW(hwnd, message, wparam, lparam);
            if (hit != HTCLIENT) return hit;
            POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
            ScreenToClient(hwnd, &point);
            if (!IsZoomed(hwnd)) { // The top resize edge now lies inside the client area.
                const UINT dpi = GetDpiForWindow(hwnd);
                const int border = GetSystemMetricsForDpi(SM_CYFRAME, dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
                if (point.y < border) {
                    RECT client{};
                    GetClientRect(hwnd, &client);
                    if (point.x < border * 2) return HTTOPLEFT;
                    if (point.x >= client.right - border * 2) return HTTOPRIGHT;
                    return HTTOP;
                }
            }
            return app->desktopNonClientHit(point.x, point.y);
        }
        break;
    case WM_NCMOUSEMOVE:
        if (app && app->desktopMode()) {
            TRACKMOUSEEVENT tracking{sizeof(TRACKMOUSEEVENT), TME_LEAVE | TME_NONCLIENT, hwnd, 0};
            TrackMouseEvent(&tracking);
            app->desktopMaximizeHover(wparam == HTMAXBUTTON);
        }
        break;
    case WM_NCMOUSELEAVE:
        if (app) app->desktopMaximizeHover(false);
        break;
    case WM_NCLBUTTONDOWN:
    case WM_NCLBUTTONDBLCLK:
        // Its own maximize button: not the classic one DefWindowProc would draw.
        if (app && app->desktopMode() && wparam == HTMAXBUTTON) { app->noteUserInput(); app->desktopMaximizePress(true); return 0; }
        break;
    case WM_NCLBUTTONUP:
        if (app && app->desktopMode() && wparam == HTMAXBUTTON) { app->desktopMaximizePress(false); return 0; }
        break;
    case WM_GETMINMAXINFO:
        if (app) app->minimumWindowSize(*reinterpret_cast<MINMAXINFO*>(lparam));
        return 0;
    case WM_ACTIVATE:
        if (app) app->windowActivated(LOWORD(wparam) != WA_INACTIVE);
        break;
    case WM_ENTERSIZEMOVE:
        if (app) app->sizeMove(true); // The system's loop runs no frames: paint as it asks.
        return 0;
    case WM_EXITSIZEMOVE:
        if (app) { app->sizeMove(false); app->windowPlacementChanged(); }
        return 0;
    case WM_COPYDATA: {
        const auto* data = reinterpret_cast<const COPYDATASTRUCT*>(lparam);
        if (!app || !data || data->dwData != jump_copy_data || !data->lpData || data->cbData < sizeof(wchar_t) || data->cbData > 4096) break;
        std::wstring command(static_cast<const wchar_t*>(data->lpData), data->cbData / sizeof(wchar_t));
        while (!command.empty() && command.back() == L'\0') command.pop_back();
        app->showFromTray(); // Hidden while a game runs: back first.
        if (IsIconic(hwnd)) ShowWindow(hwnd, SW_RESTORE);
        SetForegroundWindow(hwnd);
        app->jumpCommand(command);
        return TRUE;
    }
    case WM_DROPFILES:
        if (app) app->dropFiles(reinterpret_cast<HDROP>(wparam));
        else DragFinish(reinterpret_cast<HDROP>(wparam));
        return 0;
    case WM_PAINT:
        if (app && !app->gameOverlayOpen()) {
            if (app->deferPaint()) ValidateRect(hwnd, nullptr); // The next frame paints it.
            else app->paint();
        } else {
            PAINTSTRUCT ps; BeginPaint(hwnd, &ps); EndPaint(hwnd, &ps);
            if (app) InvalidateRect(app->frameWindow(), nullptr, FALSE);
        }
        return 0;
    case WM_TIMER:
        if (app && wparam == Launcher::guide_timer) { app->pollGuideInput(); return 0; }
        if (app && wparam == Launcher::foreground_scan_timer) { app->foregroundScanDue(); return 0; }
        break;
    case WM_HOTKEY:
        if (app && wparam == Launcher::guide_hotkey) { app->guideKeyboardHotkey(); return 0; }
        break;
    case WM_CLOSE:
        if (app && app->requestClose()) return 0; // Confirmation, then the one-second exit.
        break;
    case WM_QUERYENDSESSION:
        // Windows is shutting down, restarting or signing out: nothing is lost even if it never
        // gets as far as WM_ENDSESSION (another program refused, or it is killed).
        if (app) app->saveSessionState();
        return TRUE;
    case WM_ENDSESSION:
        if (app && wparam) {
            app->resumeFrozenGames(); // Let games handle logoff/shutdown themselves.
            app->saveSessionState();  // The process ends after this; ~Launcher never runs.
        }
        return 0;
    case WM_SYSCOMMAND:
        // A lone Alt or F10 would enter the system menu's modal loop: frames, the pad and the
        // whole launcher would stop until the next key. Alt+F4 (SC_CLOSE) is unaffected; the
        // desktop window keeps Alt+Space for its system menu.
        if ((wparam & 0xFFF0) == SC_KEYMENU && (lparam == 0 || !app || !app->desktopMode())) return 0;
        break;
    case WM_DEVICECHANGE:
        if (app && wparam == DBT_DEVNODES_CHANGED_VALUE) app->devicesChanged();
        return TRUE;
    case WM_DISPLAYCHANGE:
        if (app) app->displayChanged(); // A game changed the resolution, a TV switched mode, a monitor left.
        break;
    case WM_POWERBROADCAST:
        if (app && wparam == PBT_APMRESUMEAUTOMATIC) app->resumedFromSleep();
#ifdef _WIN32
        if (app && wparam == PBT_POWERSETTINGCHANGE && lparam) {
            const auto* setting = reinterpret_cast<const POWERBROADCAST_SETTING*>(lparam);
            if (IsEqualGUID(setting->PowerSetting, console_display_state) && setting->DataLength >= sizeof(DWORD))
                app->displayPower(*reinterpret_cast<const DWORD*>(setting->Data) != 0); // 0 off, 1 on, 2 dimmed.
        }
#endif
        return TRUE;
    case WM_XBUTTONUP:
        // The mouse's Back button: what Esc does on every screen.
        if (app && GET_XBUTTON_WPARAM(wparam) == XBUTTON1) {
            SendMessageW(hwnd, WM_KEYDOWN, VK_ESCAPE, 1);
            SendMessageW(hwnd, WM_KEYUP, VK_ESCAPE, 0xC0000001);
        }
        return TRUE;
    case WM_KEYDOWN:
        if (app) {
            if (wparam != VK_F8 && wparam != VK_F11) { app->noteUserInput(); app->noteDesktopInput(); }
            // The topmost modal layer (exit prompt, power menu, walkthrough, dialogs) takes the key first.
            if (app->modalKey(wparam, (lparam & (1LL << 30)) != 0)) return 0;
            // A friend's conversation in the drawer: its field takes the keys (Tab, F8, F11 and the Guide stay global).
            if (app->friendChatOpen() && wparam != VK_TAB && wparam != VK_F8 && wparam != VK_F11 && !(wparam == VK_HOME && app->homeKeyIsGuide())) {
                app->friendChatKey(wparam, (lparam & (1LL << 30)) != 0);
                return 0;
            }
            if (app->companionPasteKey(wparam, (lparam & (1LL << 30)) != 0)) return 0;
            // The desktop library: its keys, shortcuts and search field (false: the shared handling below).
            if (app->desktopKey(wparam, (lparam & (1LL << 30)) != 0)) return 0;
            if (wparam == VK_HOME && app->homeKeyIsGuide()) app->homeDown((lparam & (1LL << 30)) != 0); // Guide: short = menu, hold = power.
            else if (wparam == 'G' && !(lparam & (1LL << 30)) && !(GetKeyState(VK_CONTROL) & 0x8000) &&
                     !(GetKeyState(VK_MENU) & 0x8000) && app->glanceAvailable()) app->openGlance();
            else if (wparam == 'C' && !(lparam & (1LL << 30)) && !(GetKeyState(VK_CONTROL) & 0x8000) &&
                     !(GetKeyState(VK_MENU) & 0x8000) && app->companionAvailable()) app->openCompanion();
            else if (wparam == 'T' && !(lparam & (1LL << 30)) && !(GetKeyState(VK_CONTROL) & 0x8000) &&
                     !(GetKeyState(VK_MENU) & 0x8000) && app->tarotAvailable()) app->openTarot();
            else if ((app->editingSecret() || app->editingArtworkSearch()) && wparam == 'V' && (GetKeyState(VK_CONTROL) & 0x8000)) app->pasteSecret();
            else if ((app->editingSecret() || app->editingArtworkSearch()) && wparam == 'A' && (GetKeyState(VK_CONTROL) & 0x8000)) app->selectSecret();
            else if (app->editingArtworkSearch() && wparam == VK_DELETE) app->clearArtworkQuery(); // Delete = "Wyczyść".
            else if ((app->editingSecret() || app->editingArtworkSearch()) && (wparam == VK_BACK || wparam == VK_DELETE)) app->eraseSecret();
            // Delete on a row with its own value (a game's override): back to the default.
            else if (wparam == VK_DELETE && !(lparam & (1LL << 30)) && app->settingsOpen() && app->settingsResetFocused()) {}
            else if (wparam == VK_RETURN) app->enterDown((lparam & (1LL << 30)) != 0);
            else if (wparam == VK_TAB) app->tabDown((lparam & (1LL << 30)) != 0);
            // A/D double as Left/Right, except where letters are text (search fields, and the
            // library screen, where typing opens its search).
            else if (wparam == VK_LEFT || (wparam == 'A' && !app->searching() && !app->artworkOpen() && !app->libraryTypeAhead() && !app->editingSecret())) {
                if (app->settingsOpen()) app->settingsAdjust(-1);
                else if (app->quickOpen()) app->quickMove(-1);
                else if (app->categoryMenuOpen()) app->categoryMenuHorizontal(-1, (lparam & (1LL << 30)) != 0);
                else app->navigate(-1);
            }
            else if (wparam == VK_RIGHT || (wparam == 'D' && !app->searching() && !app->artworkOpen() && !app->libraryTypeAhead() && !app->editingSecret())) {
                if (app->settingsOpen()) app->settingsAdjust(1);
                else if (app->quickOpen()) app->quickMove(1);
                else if (app->categoryMenuOpen()) app->categoryMenuHorizontal(1, (lparam & (1LL << 30)) != 0);
                else app->navigate(1);
            }
            else if (wparam == VK_DOWN) {
                if (app->settingsOpen() && (GetKeyState(VK_SHIFT) & 0x8000)) app->settingsReorder(1);
                else if (app->friendsOpen()) app->friendsMove(1);
                else if (app->categoryMenuOpen()) app->categoryMenuMove(1);
                else if (app->menuOpen()) app->menuMove(1);
                else app->changeCategory(1);
            } else if (wparam == VK_UP) {
                if (app->settingsOpen() && (GetKeyState(VK_SHIFT) & 0x8000)) app->settingsReorder(-1);
                else if (app->friendsOpen()) app->friendsMove(-1);
                else if (app->categoryMenuOpen()) app->categoryMenuMove(-1);
                else if (app->menuOpen()) app->menuMove(-1);
                else app->changeCategory(-1);
            } else if (wparam == VK_SPACE && !app->searching() && !app->artworkOpen() && !app->libraryScreen()) { // Not a pick in the library.
                if (lparam & (1LL << 30)) return 0;
                if (app->settingsOpen()) app->settingsActivate();
                else if (app->quickOpen()) app->quickAccept();
                else if (app->friendsOpen()) app->friendsAccept();
                else if (app->categoryMenuOpen()) app->categoryMenuAccept();
                else if (app->menuOpen()) app->menuAccept();
                else app->accept();
            }
            else if (wparam == VK_F11) app->toggleFullscreen();
            else if (wparam == VK_F6 && !(lparam & (1LL << 30))) app->focusLibraryControls();
            else if (wparam == VK_F8 && !(lparam & (1LL << 30))) app->togglePerformance();
            else if (wparam == VK_F5 && app->settingsOpen()) { app->saveSettings(); app->retryStore(); }
            else if (wparam == VK_F5) app->retryStore();
            else if ((wparam == VK_PRIOR || wparam == VK_NEXT) && app->glanceOpen() && !app->friendsOpen()) app->glanceStepSection(wparam == VK_PRIOR ? -1 : 1);
            else if ((wparam == VK_PRIOR || wparam == VK_NEXT) && app->companionOpen() && !app->friendsOpen()) app->companionScroll(wparam == VK_PRIOR ? 1 : -1);
            else if (wparam == VK_PRIOR && app->artworkOpen()) app->artworkPage(-1);
            else if (wparam == VK_NEXT && app->artworkOpen()) app->artworkPage(1);
            else if ((wparam == VK_PRIOR || wparam == VK_NEXT) && app->settingsOpen()) app->settingsShoulder(wparam == VK_PRIOR ? -1 : 1);
            else if ((wparam == VK_PRIOR || wparam == VK_NEXT) && app->libraryTabsShown()) app->switchLibraryTab(wparam == VK_PRIOR ? -1 : 1);
            else if (wparam == VK_PRIOR || wparam == VK_NEXT) app->homePage(wparam == VK_PRIOR ? -1 : 1); // Metro pages; elsewhere nothing.
            else if (wparam == 'H' && !app->searching() && (GetKeyState(VK_CONTROL) & 0x8000)) app->togglePrivateCategory();
            else if (wparam == VK_DELETE && app->companionOpen() && !app->friendsOpen() && !(lparam & (1LL << 30))) app->companionMindDeleteConfirm(true);
            else if (wparam == 'F' && app->companionOpen() && !app->searching() && !app->friendsOpen() && !(lparam & (1LL << 30))) app->companionMindNextFilter();
            else if (wparam == VK_BACK && app->searching()) app->eraseSearchCharacter((lparam & (1LL << 30)) != 0);
            // Back takes one step per press: a held Backspace that just emptied a search must not
            // go on to close it, leave the bar and then the library.
            else if ((wparam == VK_ESCAPE || wparam == VK_BACK) && (lparam & (1LL << 30))) {}
            else if (wparam == VK_ESCAPE || wparam == VK_BACK) {
                if (app->quickOpen()) app->closeQuick();
                else if (app->categoryMenuOpen()) app->closeCategoryMenu();
                else if (app->friendsOpen()) app->friendsBack(); // Friends list -> tiles -> closed.
                else if (app->menuOpen()) app->dismissMenu();
                else app->back();
            }
        }
        return 0;
    case WM_PASTE:
        if (app) app->companionPaste();
        return 0;
    case WM_CHAR:
        if (app && !app->exitPromptOpen() && !app->powerMenuOpen() && !app->tourOpen()) app->onCharacter(static_cast<wchar_t>(wparam));
        return 0;
    case WM_KEYUP:
        if (app && wparam == VK_RETURN) app->enterUp();
        if (app && wparam == VK_TAB) app->tabUp();
        if (app && wparam == VK_HOME) app->homeUp();
        return 0;
    case WM_KILLFOCUS:
        if (app) { app->cancelEnterHold(); app->cancelTabHold(); app->pointerUp(); }
        return 0;
    case WM_SETFOCUS:
        if (app) app->focusRegained();
        return 0;
    case WM_MOUSEWHEEL:
        if (app && !app->exitPromptOpen() && !app->powerMenuOpen() && !app->tourOpen()) {
            app->noteUserInput();
            POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
            ScreenToClient(hwnd, &point);
            if (app->desktopEmbedded()) { // Urządzenia, kadrowanie, plugin pages in a desktop sheet: their canvas.
                float x = static_cast<float>(point.x), y = static_cast<float>(point.y);
                app->desktopEmbedMap(x, y);
                point = POINT{static_cast<LONG>(x), static_cast<LONG>(y)};
            }
            const auto modal = app->topModal();
            const int raw = GET_WHEEL_DELTA_WPARAM(wparam);
            // Lists move one row per notch: a precision touchpad's many small deltas add up to
            // whole notches instead of each one moving a row (which overshot every list).
            static int accumulated = 0;
            const auto notch = [&] {
                if ((accumulated < 0) != (raw < 0)) accumulated = 0; // Turned the other way.
                accumulated += raw;
                if (std::abs(accumulated) < WHEEL_DELTA) return 0;
                const int step = accumulated < 0 ? 1 : -1;
                accumulated += step * WHEEL_DELTA;
                return step;
            };
            if (app->desktopWheel(static_cast<float>(point.x), static_cast<float>(point.y), raw,
                                  (GET_KEYSTATE_WPARAM(wparam) & MK_CONTROL) != 0)) {} // Grid, sidebar, pane, poster size.
            else if (app->companionOpen() && !app->friendsOpen() && modal == Launcher::Modal::none) app->companionWheel(raw);
            else if (modal == Launcher::Modal::tarot) {} // The reading has nothing to scroll.
            else if (modal == Launcher::Modal::profile) app->profileScroll(-static_cast<float>(raw) / WHEEL_DELTA * 96.0f); // Smooth, as a page.
            else if (modal == Launcher::Modal::aurelia) app->aureliaScroll(raw > 0 ? -2 : 2);
            else if (modal == Launcher::Modal::whats_new) app->whatsNewScroll(-static_cast<float>(raw) / WHEEL_DELTA * 60.0f); // Smooth, as text scrolls.
            else if (const int step = notch(); !step) {}
            else if (modal == Launcher::Modal::file_browser) app->fileBrowserWheel(step);
            else if (modal == Launcher::Modal::library_dialog) app->libraryDialogKey(step > 0 ? VK_DOWN : VK_UP, false);
            else if (modal == Launcher::Modal::companion_picker) app->companionPickerKey(step > 0 ? VK_NEXT : VK_PRIOR, false);
            else if (app->cropOpen()) app->changeCategory(step);
            else if (app->devicesOpen()) app->deviceWheel(static_cast<float>(point.x), step);
            else if (app->artworkOpen()) app->artworkWheel(static_cast<float>(point.x), step);
            else if (app->settingsOpen()) app->settingsWheel(static_cast<float>(point.x), step);
            else if (app->friendsOpen() && app->overFriends(static_cast<float>(point.x))) app->scrollFriends(step);
            else if (app->categoryMenuOpen()) app->scrollCategoryMenu(step);
            else app->wheel(step, static_cast<float>(point.x));
        }
        return 0;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
        if (app) { app->noteUserInput(); app->noteDesktopInput(); }
        if (app) app->click(static_cast<float>(GET_X_LPARAM(lparam)), static_cast<float>(GET_Y_LPARAM(lparam)),
                             message == WM_LBUTTONDBLCLK);
        return 0;
    case WM_RBUTTONUP:
        if (app) { app->noteUserInput(); app->noteDesktopInput(); app->contextClick(static_cast<float>(GET_X_LPARAM(lparam)), static_cast<float>(GET_Y_LPARAM(lparam))); }
        return 0;
    case WM_MOUSEMOVE: {
        TRACKMOUSEEVENT tracking{sizeof(TRACKMOUSEEVENT), TME_LEAVE, hwnd, 0};
        TrackMouseEvent(&tracking);
        if (app) {
            app->desktopMaximizeHover(false); // Over the client area, not the maximize button.
            app->mouseMove(static_cast<float>(GET_X_LPARAM(lparam)), static_cast<float>(GET_Y_LPARAM(lparam)));
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        if (app) app->mouseLeave();
        return 0;
    case WM_LBUTTONUP:
    case WM_CAPTURECHANGED:
        // Desktop buttons act on release over the same target; drags drop here.
        if (app && message == WM_LBUTTONUP) app->desktopPointerUp(static_cast<float>(GET_X_LPARAM(lparam)), static_cast<float>(GET_Y_LPARAM(lparam)));
        if (app) app->pointerUp(message == WM_CAPTURECHANGED);
        return 0;
    case WM_SETCURSOR:
        if (app && app->screensaverActive() && LOWORD(lparam) == HTCLIENT) { SetCursor(nullptr); return TRUE; }
        if (app && LOWORD(lparam) == HTCLIENT) {
            POINT cursor{};
            GetCursorPos(&cursor);
            ScreenToClient(hwnd, &cursor);
            if (const auto shape = app->desktopCursor(static_cast<float>(cursor.x), static_cast<float>(cursor.y))) {
                SetCursor(LoadCursorW(nullptr, shape)); // Text beam over the search, a hand over what acts.
                return TRUE;
            }
            SetCursor(LoadCursorW(nullptr, app->hoverButton(static_cast<float>(cursor.x),
                                                            static_cast<float>(cursor.y)) ? IDC_HAND : IDC_ARROW));
            return TRUE;
        }
        break;
    case WM_DPICHANGED:
        // Full-screen Tryb TV keeps covering its monitor; a window takes Windows' suggestion.
        if (app && app->coversMonitor()) app->displayChanged();
        else if (const auto* recommended = reinterpret_cast<RECT*>(lparam))
            SetWindowPos(hwnd, nullptr, recommended->left, recommended->top,
                         recommended->right - recommended->left, recommended->bottom - recommended->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    case WM_DESTROY: {
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        delete app;
        // Workers own their payloads until PostMessage succeeds. Drain any
        // posted payloads still queued as the window shuts down.
        discardWorkerMessages<Launcher>(hwnd);
        PostQuitMessage(0);
        return 0;
    }
    default: break;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}
} // namespace

#ifdef _WIN32
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show_command) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    // A Jump List entry (--jump=<command>): a running launcher takes it and this process ends; else it
    // waits in this one until its library is ready.
    std::wstring jump;
    std::filesystem::path companion_copy;
    bool start_minimized = false;
    DWORD updated_from = 0;
    int argument_count = 0;
    if (auto** arguments = CommandLineToArgvW(GetCommandLineW(), &argument_count)) {
        for (int i = 1; i < argument_count; ++i) {
            if (std::wstring_view(arguments[i]).starts_with(L"--jump=")) jump = arguments[i] + 7;
            else if (std::wstring_view(arguments[i]).starts_with(L"--after-update=")) updated_from = static_cast<DWORD>(_wtoi(arguments[i] + 15));
            else if (std::wstring_view(arguments[i]) == L"--minimized") start_minimized = true;
            else if (std::wstring_view(arguments[i]) == L"--companion-copy" && i + 1 < argument_count) companion_copy = arguments[++i];
        }
        LocalFree(arguments);
    }
    if (companion_copy.empty()) {
        wchar_t module[32768]{}; GetModuleFileNameW(nullptr, module, 32768);
        const auto beside = std::filesystem::path(module).parent_path();
        std::error_code ec;
        if (std::filesystem::is_regular_file(beside / L"backup.json", ec)) companion_copy = beside;
    }
    if (!companion_copy.empty()) {
        std::error_code ec; companion_copy = std::filesystem::weakly_canonical(companion_copy, ec);
        const auto restored = ec ? companion::backup::Result{} : companion::backup::openCopy(companion_copy);
        if (!restored.ok || !SetEnvironmentVariableW(L"LAUNCHME_CONFIG_DIR", restored.path.c_str())) {
            MessageBoxW(nullptr, restored.error.empty() ? L"Cannot open companion backup." : restored.error.c_str(), L"Launchme — companion backup", MB_ICONERROR | MB_OK);
            return 1;
        }
    }
    // Started by an update (src/app/updates.cpp): the previous version closes first (up to 30 s), so
    // this one takes the single-instance lock and its old files can be removed.
    if (updated_from) {
        if (const HANDLE previous = OpenProcess(SYNCHRONIZE, FALSE, updated_from)) {
            WaitForSingleObject(previous, 30000);
            CloseHandle(previous);
        }
    }
    const auto data_folder = userConfigDirectory();
    diagnostics::install(data_folder);
    const HANDLE instance_mutex = singleInstance(data_folder, jump);
    if (!instance_mutex) return 0; // The running launcher came to the front (or took the Jump List command).
    struct MutexRelease { HANDLE handle; ~MutexRelease() { if (handle != INVALID_HANDLE_VALUE) { ReleaseMutex(handle); CloseHandle(handle); } } } release{instance_mutex};
    // A start that fails says why (and logs it) instead of ending without a word.
    const auto startupFailure = [](const wchar_t* what, HRESULT code) {
        wchar_t text[512]{};
        swprintf_s(text, L"%s (0x%08lX)", what, static_cast<unsigned long>(code));
        diagnostics::log("error", std::wstring_view(text));
        MessageBoxW(nullptr, text, L"Launchme", MB_ICONERROR | MB_OK);
        return 1;
    };
    if (const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED); FAILED(com))
        return startupFailure(L"Launchme cannot start: COM initialization failed", com);
    WNDCLASSEXW wc{sizeof(wc)};
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc = windowProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    // Resource 1 (res/launchme.rc.in): taskbar, Alt+Tab and the EXE in Explorer.
    wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(1));
    if (!wc.hIcon) wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.hIconSm = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(1), IMAGE_ICON,
        GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR));
    wc.lpszClassName = L"LaunchmeNativeWindow";
    if (!RegisterClassExW(&wc)) { const auto error = HRESULT_FROM_WIN32(GetLastError()); CoUninitialize(); return startupFailure(L"Launchme cannot start: window class registration failed", error); }
    const int width = GetSystemMetrics(SM_CXSCREEN), height = GetSystemMetrics(SM_CYSCREEN);
    const int x = 0, y = 0;
    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"launchme", WS_POPUP | WS_SYSMENU | WS_MINIMIZEBOX,
                                x, y, width, height, nullptr, nullptr, instance, nullptr);
    if (!hwnd) {
        const auto error = HRESULT_FROM_WIN32(GetLastError());
        CoUninitialize();
        // Launcher::initialize logged which part failed (graphics, mostly: a missing or broken GPU driver).
        return startupFailure(L"Launchme cannot start: the window or its graphics could not be created. See launchme.log in %LOCALAPPDATA%\\Launchme", error);
    }
    main_window = hwnd;
    // Running elevated (a user's "Run as administrator"): a second start, the Jump List and dropped
    // files come from normal-integrity processes and would otherwise be blocked (UIPI).
    for (const UINT allowed : {static_cast<UINT>(WM_COPYDATA), static_cast<UINT>(WM_DROPFILES), 0x0049u /* WM_COPYGLOBALDATA */})
        ChangeWindowMessageFilterEx(hwnd, allowed, MSGFLT_ALLOW, nullptr);
    const HWINEVENTHOOK foreground_hook = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, nullptr, foregroundEvent,
                                                          0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    const HPOWERNOTIFY display_notify = RegisterPowerSettingNotification(hwnd, &console_display_state, DEVICE_NOTIFY_WINDOW_HANDLE);
    const auto* launcher = reinterpret_cast<Launcher*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (launcher && launcher->desktopMode()) {
        // The desktop window (Launcher::applyWindowMode set its style and place): now that the
        // launcher answers WM_NCCALCSIZE, its own top bar replaces the caption.
        SetWindowPos(hwnd, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    } else {
        const DWM_WINDOW_CORNER_PREFERENCE corner = DWMWCP_DONOTROUND;
        DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));
    }
    // A shortcut set to "Run: Minimized", or --minimized (Uruchamiaj z systemem Windows): on the
    // taskbar, ready, without taking the screen.
    const bool minimized = start_minimized || show_command == SW_SHOWMINNOACTIVE || show_command == SW_MINIMIZE ||
                           show_command == SW_SHOWMINIMIZED || show_command == SW_FORCEMINIMIZE;
    ShowWindow(hwnd, minimized ? SW_SHOWMINNOACTIVE : launcher && launcher->startMaximized() ? SW_SHOWMAXIMIZED : SW_SHOW);
    UpdateWindow(hwnd);
    if (auto* app = reinterpret_cast<Launcher*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA)); app && !jump.empty()) app->jumpCommand(jump);
    MSG message{};
    FrameClock frame_clock;
    bool running = true;
    while (running) {
        for (int batch = 0; batch < 64 && PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE); ++batch) {
            if (message.message == WM_QUIT) { running = false; break; }
            TranslateMessage(&message); DispatchMessageW(&message);
        }
        if (!running) break;
        auto* active = reinterpret_cast<Launcher*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        // Nobody watches a switched-off display; the screensaver's slow pans need no 144 Hz; a still
        // screen or a game in front needs fewer ticks (Launcher::frameLimit).
        frame_clock.limit(active && active->displayOff() ? 5 : active && active->screensaverActive() ? 30 : active ? active->frameLimit() : 240);
        frame_clock.update(active ? active->frameWindow() : hwnd);
        if (frame_clock.ready()) {
            frame_clock.consume();
            if (auto* app = reinterpret_cast<Launcher*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA))) {
                app->setFrameRate(frame_clock.targetHz(), frame_clock.refreshHz());
                app->tick();
                app->updateFrame();
                // play: commands whose game ended: their senders (Steam's launchme.exe) end too, and
                // so does this launcher when Steam started it for its game.
                bool own_ended = false;
                for (const auto& command : app->finishedPlays()) {
                    const auto wide = text_encoding::wideLenient(command);
                    if (const HANDLE done = OpenEventW(EVENT_MODIFY_STATE, FALSE, playEventName(data_folder, wide).c_str())) { SetEvent(done); CloseHandle(done); }
                    if (jump.starts_with(L"play:") && wide == jump) own_ended = true;
                }
                if (own_ended) {
                    SendMessageW(hwnd, WM_QUERYENDSESSION, 0, 0); // Saves its state, as on a sign-out.
                    DestroyWindow(hwnd);
                    message.wParam = 0;
                    break;
                }
            }
        }
        frame_clock.wait();
    }
    if (foreground_hook) UnhookWinEvent(foreground_hook);
    if (display_notify) UnregisterPowerSettingNotification(display_notify);
    main_window = nullptr;
    CoUninitialize();
    return static_cast<int>(message.wParam);
}
#else
namespace {
// One launcher per data folder, as the Windows mutex makes it: an flock(2) on a lock file, and a
// Unix socket on which the running launcher takes a second start's command (--jump=, or "show":
// it comes to the front), delivered to its window as the WM_COPYDATA a second launchme.exe sends.
// Both live in $XDG_RUNTIME_DIR (the user's own 0700 folder), else in the data folder.
class SingleInstance {
public:
    SingleInstance() = default;
    SingleInstance(const SingleInstance&) = delete;
    SingleInstance& operator=(const SingleInstance&) = delete;
    ~SingleInstance() {
        if (thread_.joinable()) {
            const uint64_t one = 1;
            [[maybe_unused]] const auto written = ::write(stop_, &one, sizeof(one));
            thread_.join();
        }
        {
            // Senders still waiting for a game (play:) see the connection close and end too.
            std::lock_guard guard(waiting_mutex_);
            for (const auto& [command, clients] : waiting_) for (const int client : clients) ::close(client);
            waiting_.clear();
        }
        if (listener_ >= 0) { ::close(listener_); ::unlink(socket_path_.c_str()); }
        if (stop_ >= 0) ::close(stop_);
        if (lock_ >= 0) ::close(lock_); // Releases the lock; the file stays for the next start.
    }
    // false: the running launcher took `command` (this process ends). true: this one continues,
    // owning the lock (or without it when the other neither answers nor ends).
    bool acquire(const std::filesystem::path& data_folder, const std::string& command) {
        std::error_code error;
        std::filesystem::path folder = data_folder;
        if (const char* runtime = std::getenv("XDG_RUNTIME_DIR"); runtime && *runtime == '/' && std::filesystem::is_directory(runtime, error))
            folder = runtime;
        else std::filesystem::create_directories(folder, error);
        wchar_t id[32]{};
        swprintf_s(id, L"%016zx", std::hash<std::wstring>{}(data_folder.lexically_normal().wstring()));
        lock_path_ = folder / (L"launchme-" + std::wstring(id) + L".lock");
        socket_path_ = folder / (L"launchme-" + std::wstring(id) + L".socket");
        if (lock()) return true;
        // The other launcher may still be starting (no socket yet) or closing (the lock goes with
        // its process): hand it the command, or take the lock once it is free.
        for (int attempt = 0; attempt < 100; ++attempt) {
            if (deliver(command.empty() ? std::string("show") : command)) return false;
            Sleep(100);
            if (lock()) return true;
        }
        return true; // It neither answers nor ends: start rather than leave the user with nothing.
    }
    // Once the window exists: second starts are answered from here on.
    void serve() {
        if (lock_ < 0 || socket_path_.native().size() >= sizeof(sockaddr_un::sun_path)) return;
        ::unlink(socket_path_.c_str()); // A crashed launcher's.
        listener_ = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
        sockaddr_un address{};
        address.sun_family = AF_UNIX;
        std::memcpy(address.sun_path, socket_path_.c_str(), socket_path_.native().size());
        const mode_t mask = ::umask(0077);
        const bool bound = listener_ >= 0 && ::bind(listener_, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0;
        ::umask(mask);
        if (!bound || ::listen(listener_, 4) != 0 || (stop_ = eventfd(0, EFD_CLOEXEC)) < 0) {
            if (listener_ >= 0) ::close(listener_);
            listener_ = -1;
            return;
        }
        thread_ = std::thread([this] { answer(); });
    }
    // A play: command's game ended: its senders get "2" and their connection closes.
    void finish(const std::string& command) {
        std::lock_guard guard(waiting_mutex_);
        const auto found = waiting_.find(command);
        if (found == waiting_.end()) return;
        for (const int client : found->second) { ::send(client, "2", 1, MSG_NOSIGNAL); ::close(client); }
        waiting_.erase(found);
    }
private:
    std::filesystem::path lock_path_, socket_path_;
    int lock_ = -1, listener_ = -1, stop_ = -1;
    std::thread thread_;
    std::mutex waiting_mutex_;
    std::map<std::string, std::vector<int>> waiting_; // play: commands → their senders' connections.

    bool lock() {
        const int file = ::open(lock_path_.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
        if (file < 0) return false;
        if (::flock(file, LOCK_EX | LOCK_NB) == 0) { lock_ = file; return true; }
        ::close(file);
        return false;
    }
    // The running launcher's answer to `command`: true when its window took it.
    bool deliver(const std::string& command) const {
        if (socket_path_.native().size() >= sizeof(sockaddr_un::sun_path)) return false;
        const int connection = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
        if (connection < 0) return false;
        sockaddr_un address{};
        address.sun_family = AF_UNIX;
        std::memcpy(address.sun_path, socket_path_.c_str(), socket_path_.native().size());
        const timeval timeout{3, 0};
        ::setsockopt(connection, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        ::setsockopt(connection, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
        bool accepted = false;
        if (::connect(connection, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0 &&
            ::send(connection, command.data(), command.size(), MSG_NOSIGNAL) == static_cast<ssize_t>(command.size())) {
            ::shutdown(connection, SHUT_WR);
            char reply = 0;
            accepted = ::recv(connection, &reply, 1, 0) == 1 && reply == '1';
            // Started by Steam (play:): this process lasts as long as the game, so Steam sees it play.
            if (accepted && command.starts_with("play:")) {
                const timeval forever{0, 0};
                ::setsockopt(connection, SOL_SOCKET, SO_RCVTIMEO, &forever, sizeof(forever));
                while (::recv(connection, &reply, 1, 0) < 0 && errno == EINTR) {}
            }
        }
        ::close(connection);
        return accepted;
    }
    void answer() {
        while (true) {
            pollfd items[2]{{listener_, POLLIN, 0}, {stop_, POLLIN, 0}};
            if (::poll(items, 2, -1) < 0) { if (errno == EINTR) continue; return; }
            if (items[1].revents) return;
            if (!(items[0].revents & POLLIN)) continue;
            const int client = ::accept4(listener_, nullptr, nullptr, SOCK_CLOEXEC);
            if (client < 0) continue;
            // This user's own starts only (the data folder's socket may be reachable by others).
            ucred peer{};
            socklen_t size = sizeof(peer);
            if (::getsockopt(client, SOL_SOCKET, SO_PEERCRED, &peer, &size) != 0 || peer.uid != ::getuid()) { ::close(client); continue; }
            const timeval timeout{2, 0};
            ::setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
            std::string text;
            char buffer[1024];
            for (ssize_t got; text.size() <= 4096 && (got = ::recv(client, buffer, sizeof(buffer), 0)) > 0;) text.append(buffer, static_cast<size_t>(got));
            bool accepted = false;
            if (!text.empty() && text.size() <= 4096) {
                auto done = std::make_shared<std::promise<bool>>();
                auto result = done->get_future();
                lm_window::post([command = text_encoding::wideLenient(text), done] {
                    bool taken = false;
                    if (main_window && IsWindow(main_window)) {
                        std::wstring copy = command;
                        COPYDATASTRUCT data{jump_copy_data, static_cast<DWORD>((copy.size() + 1) * sizeof(wchar_t)), copy.data()};
                        taken = SendMessageW(main_window, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&data)) != 0;
                    }
                    done->set_value(taken);
                });
                accepted = result.wait_for(std::chrono::seconds(2)) == std::future_status::ready && result.get();
            }
            ::send(client, accepted ? "1" : "0", 1, MSG_NOSIGNAL);
            if (accepted && text.starts_with("play:")) { // Kept until the game ends (finish).
                std::lock_guard guard(waiting_mutex_);
                waiting_[text].push_back(client);
                continue;
            }
            ::close(client);
        }
    }
};

// Started by an update (src/app/updates.cpp): the previous version, process `pid`, closes first
// (up to 30 s), so this one takes the single-instance lock.
void waitForExit(pid_t pid) {
    if (pid <= 0) return;
#ifdef SYS_pidfd_open
    if (const int process = static_cast<int>(::syscall(SYS_pidfd_open, pid, 0)); process >= 0) {
        pollfd item{process, POLLIN, 0};
        while (::poll(&item, 1, 30000) < 0 && errno == EINTR) {}
        ::close(process);
        return;
    }
#endif
    for (int i = 0; i < 300 && (::kill(pid, 0) == 0 || errno == EPERM); ++i) Sleep(100);
}
} // namespace

int main(int argc, char** argv) {
    // A second start (--jump=<command>): a running launcher takes it and this process ends; else it
    // waits in this one until its library is ready.
    std::wstring jump;
    std::filesystem::path companion_copy;
    bool start_minimized = false;
    pid_t updated_from = 0;
    for (int i = 1; i < argc; ++i) {
        const std::string_view argument(argv[i]);
        if (argument.starts_with("--jump=")) jump = text_encoding::wideLenient(argument.substr(7));
        else if (argument.starts_with("--after-update=")) updated_from = static_cast<pid_t>(std::strtol(argv[i] + 15, nullptr, 10));
        else if (argument == "--minimized") start_minimized = true;
        else if (argument == "--companion-copy" && i + 1 < argc) companion_copy = std::filesystem::path(argv[++i]);
    }
    if (companion_copy.empty()) {
        wchar_t module[32768]{}; GetModuleFileNameW(nullptr, module, 32768);
        const auto beside = std::filesystem::path(module).parent_path();
        std::error_code ec;
        if (std::filesystem::is_regular_file(beside / L"backup.json", ec)) companion_copy = beside;
    }
    if (!companion_copy.empty()) {
        std::error_code ec; companion_copy = std::filesystem::weakly_canonical(companion_copy, ec);
        const auto restored = ec ? companion::backup::Result{} : companion::backup::openCopy(companion_copy);
        if (!restored.ok || !SetEnvironmentVariableW(L"LAUNCHME_CONFIG_DIR", restored.path.wstring().c_str())) {
            MessageBoxW(nullptr, restored.error.empty() ? L"Cannot open companion backup." : restored.error.c_str(), L"Launchme — companion backup", MB_ICONERROR | MB_OK);
            return 1;
        }
    }
    waitForExit(updated_from);
    const auto data_folder = userConfigDirectory();
    diagnostics::install(data_folder);
    SingleInstance instance;
    if (!instance.acquire(data_folder, text_encoding::utf8Lenient(jump))) return 0; // The running launcher came to the front (or took the command).
    // A start that fails says why (and logs it) instead of ending without a word.
    const auto startupFailure = [](const wchar_t* what, const std::string& detail) {
        std::wstring text = what;
        if (!detail.empty()) text += L" (" + text_encoding::wideLenient(detail) + L")";
        diagnostics::log("error", std::wstring_view(text));
        std::fprintf(stderr, "launchme: %s\n", text_encoding::utf8Lenient(text).c_str());
        MessageBoxW(nullptr, text.c_str(), L"Launchme", MB_ICONERROR | MB_OK);
        return 1;
    };
    // SDL's video, events and gamepads on this (the main) thread: Wayland or X11.
    if (std::string error; !lm_window::start(error)) return startupFailure(L"Launchme cannot start: no display (SDL video)", error);
    if (const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED); FAILED(com))
        return startupFailure(L"Launchme cannot start: COM initialization failed", {});
    WNDCLASSEXW wc{sizeof(wc)};
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc = windowProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = L"LaunchmeNativeWindow";
    if (!RegisterClassExW(&wc)) { CoUninitialize(); return startupFailure(L"Launchme cannot start: window class registration failed", {}); }
    const int width = GetSystemMetrics(SM_CXSCREEN), height = GetSystemMetrics(SM_CYSCREEN);
    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"launchme", WS_POPUP | WS_SYSMENU | WS_MINIMIZEBOX,
                                0, 0, width, height, nullptr, nullptr, wc.hInstance, nullptr);
    if (!hwnd) {
        const auto detail = lm_window::lastError();
        CoUninitialize();
        // Launcher::initialize logged which part failed (graphics, mostly).
        return startupFailure(L"Launchme cannot start: the window or its graphics could not be created. See launchme.log in the data folder",
                              detail);
    }
    main_window = hwnd;
    instance.serve();
    const auto* launcher = reinterpret_cast<Launcher*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    // The desktop window (Launcher::applyWindowMode set its style and place): its own top bar.
    if (launcher && launcher->desktopMode()) SetWindowPos(hwnd, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    // --minimized (autostart, Uruchamiaj z systemem): ready, without taking the screen.
    ShowWindow(hwnd, start_minimized ? SW_SHOWMINNOACTIVE : launcher && launcher->startMaximized() ? SW_SHOWMAXIMIZED : SW_SHOW);
    UpdateWindow(hwnd);
    if (auto* app = reinterpret_cast<Launcher*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA)); app && !jump.empty()) app->jumpCommand(jump);
    // Started by Launchme's shortcut in Steam (--jump=play:): it ends with its game, as Steam expects.
    const std::string own_play = jump.starts_with(L"play:") ? text_encoding::utf8Lenient(jump) : std::string{};
    MSG message{};
    FrameClock frame_clock;
    bool running = true;
    while (running) {
        for (int batch = 0; batch < 64 && PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE); ++batch) {
            if (message.message == WM_QUIT) { running = false; break; }
            TranslateMessage(&message); DispatchMessageW(&message);
        }
        if (!running) break;
        if (auto* app = reinterpret_cast<Launcher*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA)))
            for (const auto& command : app->finishedPlays()) {
                instance.finish(command); // A second start from Steam waiting for this game.
                if (command == own_play) { SendMessageW(hwnd, WM_QUERYENDSESSION, 0, 0); SendMessageW(hwnd, WM_ENDSESSION, TRUE, 0); running = false; }
            }
        if (!running) break;
        auto* active = reinterpret_cast<Launcher*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        // Nobody watches a switched-off display; the screensaver's slow pans need no 144 Hz; a still
        // screen or a game in front needs fewer ticks (Launcher::frameLimit).
        frame_clock.limit(active && active->displayOff() ? 5 : active && active->screensaverActive() ? 30 : active ? active->frameLimit() : 240);
        frame_clock.update(active ? active->frameWindow() : hwnd);
        if (frame_clock.ready()) {
            frame_clock.consume();
            if (auto* app = reinterpret_cast<Launcher*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA))) {
                app->setFrameRate(frame_clock.targetHz(), frame_clock.refreshHz());
                app->tick();
                app->updateFrame();
            }
        }
        frame_clock.wait();
    }
    main_window = nullptr;
    if (IsWindow(hwnd)) DestroyWindow(hwnd); // A quit that did not come through WM_DESTROY.
    CoUninitialize();
    lm_window::stop();
    return static_cast<int>(message.wParam);
}
#endif
