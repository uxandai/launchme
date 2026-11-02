// Steam's "Uruchamianie…" window: what counts as the dialog (observed: class SDL_app, 600 x 286, a captioned
// window of steamwebhelper.exe), and that the real scan runs and hides nothing here.
#include "../src/platform/steam_launch_window.hpp"

#include <cstdio>

int main() {
    using steam_launch_window::Candidate;
    using steam_launch_window::isLaunchDialog;
    Candidate dialog;
    dialog.window_class = L"SDL_app"; dialog.image = L"steamwebhelper.exe"; dialog.width = 600; dialog.height = 286;
    if (!isLaunchDialog(dialog)) return 1;
    auto other = dialog; other.image = L"Steam.EXE";
    if (!isLaunchDialog(other)) return 2; // Without case.
    other = dialog; other.width = 1280; other.height = 800;
    if (isLaunchDialog(other)) return 3; // The client's own window is a lot larger.
    other = dialog; other.image = L"hollow_knight.exe";
    if (isLaunchDialog(other)) return 4; // A game's SDL window is not ours to hide.
    other = dialog; other.window_class = L"UnityWndClass";
    if (isLaunchDialog(other)) return 5;
    other = dialog; other.caption = false;
    if (isLaunchDialog(other)) return 6; // Overlays and popups have no title bar.
    other = dialog; other.visible = false;
    if (isLaunchDialog(other)) return 7;
    other = dialog; other.width = 40; other.height = 20;
    if (isLaunchDialog(other)) return 8;
    steam_launch_window::hideNow(); // Must not throw or touch other programs' windows.
    std::printf("steam_launch_window_tests: ok\n");
    return 0;
}
