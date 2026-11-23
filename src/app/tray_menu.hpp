#pragma once
// The notification-area icon's menu (src/app/tray.cpp): built from the launcher's state each time it
// opens on Windows; on Linux, whose tray hosts show the menu themselves, sent again whenever it changes.
#include <string>
#include <vector>

namespace tray_menu {
enum Command : unsigned {
    none = 0,
    show = 1,        // Pokaż launchme (a click on the icon does the same).
    close = 2,       // Zamknij launchme: quits, even with "Zamknięcie okna chowa do zasobnika".
    switch_mode = 3, // Tryb TV, or Okno desktopowe from Tryb TV.
    downloads = 4,   // Pobierania (only while something downloads).
    settings = 5,
    end_game = 6,    // Zamknij grę: the game running now.
    recent_first = 100, // Ostatnio grane: recent_first + its index.
};
constexpr unsigned recent_limit = 5;
constexpr size_t label_limit = 48; // Longer names end with "…".

struct Entry {
    unsigned command = none;
    std::wstring label;
    bool bold = false;          // The default entry.
    std::vector<Entry> children; // A submenu (Ostatnio grane).
    bool separator() const { return command == none && label.empty() && children.empty(); }
    bool operator==(const Entry&) const = default;
};

inline std::wstring label(const std::wstring& text) {
    return text.size() <= label_limit ? text : text.substr(0, label_limit - 1) + L"…";
}
} // namespace tray_menu
