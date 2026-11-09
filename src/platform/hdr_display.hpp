#pragma once
// Windows HDR of one display, for the games that ask for it (Opcje uruchamiania → "Włącz HDR na czas
// gry"): Launchme switches HDR on before such a game starts and back off once it ends, on the
// display its window is on. Through DisplayConfig: the "HDR state" calls of Windows 11 24H2 where
// they exist, else the older "advanced colour" ones. Linux: not supported (nothing happens).
#include <optional>
#include <string>

namespace hdr_display {
struct State {
    std::wstring device; // The display's GDI name ("\.\DISPLAY1"): the same display later.
    bool supported = false, enabled = false;
};
// The display a window is on (null: the primary one); nullopt when it cannot be read.
std::optional<State> query(void* window);
// HDR on or off for a display named by query(); true when Windows took it.
bool set(const std::wstring& device, bool enabled);
}
