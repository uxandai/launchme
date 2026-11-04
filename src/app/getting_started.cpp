// "Jak zacząć" in the desktop window: a card in the sidebar above the companion with the first steps (platforms
// and store accounts, service keys and how to get them, a ROM folder, a first game, Tryb TV) and a progress
// bar. A click opens the list; a step takes the player to the place that does it. Steps count as done when the
// player has taken them or the library already shows it (a ROM folder is set, a game was added by hand).
// Ustawienia → System → "Jak zacząć" brings the card back and resets the progress.
#include "launcher.hpp"

using namespace desktop_ui;

namespace {
struct Step {
    const wchar_t* title;
    const wchar_t* hint; // What to do and, for keys, where to get them; shown when the step is chosen.
    wchar_t icon;
};
constexpr Step steps[] = {
    {trKey(L"Platformy i konta"),
     trKey(L"Ustawienia → Biblioteka: włącz sklepy i konsole, których gry mają być w bibliotece, i zaloguj się do nich."), glyph::library},
    {trKey(L"Klucze usług"),
     trKey(L"Opcjonalne, dają okładki i opisy. RAWG: rawg.io/apidocs · SteamGridDB: steamgriddb.com/profile/preferences/api · ScreenScraper: konto na screenscraper.fr."),
     glyph::settings},
    {trKey(L"Folder z ROM-ami"),
     trKey(L"Wskaż folder z grami na konsole (ES-DE i EmuDeck są wykrywane same). Emulatory dobierze Launchme."), glyph::folder_open},
    {trKey(L"Dodaj grę z dysku"), nullptr, glyph::game},
    {trKey(L"Poznaj Tryb TV"), nullptr, glyph::tv},
};
constexpr int step_count = static_cast<int>(sizeof(steps) / sizeof(steps[0]));
} // namespace

int Launcher::desktopStartCount() { return step_count; }

bool Launcher::desktopStartStepDone(int step) const {
    if (step < 0 || step >= step_count) return false;
    if (settings_.start_done & (1u << step)) return true;
    if (step == 2) return !settings_.emulation_roms.empty();
    if (step == 3) return !custom_games_.empty();
    return false;
}

int Launcher::desktopStartDone() const {
    int done = 0;
    for (int i = 0; i < step_count; ++i) done += desktopStartStepDone(i) ? 1 : 0;
    return done;
}

// Shown in the desktop window until every step is done or the player hides it.
bool Launcher::desktopStartShown() const {
    return desktop_mode_ && !settings_.start_hidden && !settings_.desktop_narrow && desktopStartDone() < step_count;
}

std::wstring Launcher::desktopStartTitle(int step) const {
    return step >= 0 && step < step_count ? std::wstring(tr(steps[step].title)) : std::wstring{};
}

void Launcher::desktopOpenStartMenu() {
    if (!desktopStartShown()) return;
    const auto r = desktopLayout().startCard();
    Menu menu;
    menu.kind = Menu::Kind::start;
    for (int i = 0; i < step_count; ++i) {
        MenuItem item{Action::start_step, tr(steps[i].title), {}, steps[i].icon};
        item.value = i;
        item.checked = desktopStartStepDone(i);
        menu.items.push_back(std::move(item));
    }
    MenuItem line;
    line.separator = true;
    menu.items.push_back(std::move(line));
    menu.items.push_back({Action::start_hide, tr(L"Ukryj ten panel"), {}, glyph::hide});
    menu.x = r.left;
    menu.y = std::max(desktopLayout().sidebarTop(), r.top - menuHeight(menu.items) - 6);
    desktop_menu_ = std::move(menu);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::desktopStartPick(int step) {
    if (step < 0 || step >= step_count) return;
    settings_.start_done |= 1u << step;
    settings_dirty_ = true; settings_edited_ = std::chrono::steady_clock::now();
    InvalidateRect(hwnd_, nullptr, FALSE);
    switch (step) {
    case 0:
    case 1:
    case 2:
        openSettings();
        if (!settingsOpen()) return;
        settings_state_.selectCategory(static_cast<int>(step == 1 ? settings_ui::Category::media : settings_ui::Category::library));
        buildSettingsRows();
        if (step == 2) openSettingsPage(settings_ui::Page::emulation);
        showNotification(NotificationKind::launch, tr(steps[step].title), tr(steps[step].hint));
        return;
    case 3: openCustomEditor({}); return;
    default: setDesktopMode(false); return;
    }
}

// Ustawienia → System → Jak zacząć: the card again, from the first step.
void Launcher::startReset() {
    settings_.start_done = 0;
    settings_.start_hidden = false;
    settings_dirty_ = true; settings_edited_ = std::chrono::steady_clock::now();
    showNotification(NotificationKind::launch, tr(L"Jak zacząć"), tr(L"Panel wraca do paska bocznego okna desktopowego."));
    InvalidateRect(hwnd_, nullptr, FALSE);
}
