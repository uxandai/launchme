// First-run walkthrough. A card with a stepper at the top of the home screen; every step opens
// the real panel it describes (the categories, the right drawer, the quick bar icon by icon,
// the game menu) and the renderer dims everything around it (src/ui/tour_renderer.cpp). Then
// a word on suspending games and the choice of the home layout. While open, the tour takes
// all input (main.cpp, input.cpp, pointer.cpp). Finishing or skipping stores
// AppSettings::tour_done; Settings → Wygląd → "Przewodnik" runs it again.
#include "launcher.hpp"
#include "launcher_internal.hpp"
#include "../ui/quick_menu.hpp"
#include "../ui/tour_view.hpp"

#include <algorithm>
#include <cmath>

using tour::Step;

namespace {
constexpr uint64_t icon_ms = 2400;   // Quick bar step: the next icon after this long.
constexpr float fade_seconds = 0.3f; // Step content and spotlight fade-in.
Step stepOf(int step) { return static_cast<Step>(std::clamp(step, 0, tour::step_count - 1)); }

const wchar_t* iconText(quick_menu::Action action) {
    switch (action) {
    case quick_menu::Action::search: return tr(L"Szukanie po tytule we wszystkich bibliotekach.");
    case quick_menu::Action::library: return tr(L"Cała biblioteka w wybranym widoku, z sortowaniem i filtrami.");
    case quick_menu::Action::store: return tr(L"Strona dodana przez plugin.");
    case quick_menu::Action::downloads: return tr(L"Pobierania i aktualizacje gier ze Steam, GOG, Epic i Battle.net.");
    case quick_menu::Action::controllers: return tr(L"Podłączone kontrolery, baterie i parowanie Bluetooth.");
    case quick_menu::Action::audio: return tr(L"Wyjście i wejście dźwięku oraz ich głośność.");
    case quick_menu::Action::widgets: return tr(L"Zegar, znajomi Steam, Discord, temperatury i widgety pluginów. Ikona obok zegara zmienia układ kafelków.");
    case quick_menu::Action::settings: return tr(L"Interfejs, biblioteka i platformy, media, dźwięk, kontrolery, pluginy i system.");
    default: return tr(L"Zamknięcie launchera, uśpienie, ponowne uruchomienie i wyłączenie komputera.");
    }
}
}

int Launcher::tourIcons() const { return quick_menu::count(static_cast<int>(plugin_menu_.size())); }

// XMB keeps its top row, and the game menu beside it, at the top of the screen; so does PSfive.
bool Launcher::tourCardAtBottom() const {
    const auto step = stepOf(tour_.step);
    return (xmbActive() && (step == Step::categories || step == Step::menu)) || (psfiveActive() && step == Step::menu);
}

void Launcher::startTour() {
    if (closing_ || exit_prompt_ || desktop_mode_) return; // Tryb TV's walkthrough: the desktop window has none.
    power_menu_.reset();
    if (settingsOpen()) {
        settings_state_.closeAll();
        settings_state_.options = settings_state_.editing = settings_state_.back_focused = false;
        clearSecretInput();
        saveSettings();
    }
    if (screen_ != ScreenMode::shelves) {
        screen_ = ScreenMode::shelves;
        if (!categories_.empty()) showCategory(std::min(category_, categories_.size() - 1));
    }
    const float alpha = tour_.alpha;
    tour_ = {};
    tour_.alpha = alpha;
    tour_.open = true;
    tourEnter(0);
}

void Launcher::tourClosePanels() {
    if (quick_open_) closeQuick();
    if (category_menu_open_) closeCategoryMenu();
    if (menu_open_) dismissMenu();
    if (friends_open_) closeFriends();
    category_rail_focus_ = false;
    metro_tabs_ = false;
}

void Launcher::tourEnter(int step, bool backwards) {
    const auto now = GetTickCount64();
    tour_.step = std::clamp(step, 0, tour::step_count - 1);
    tour_.button = tour::next;
    tour_.tiles = true;
    tour_.step_at = tour_.icon_at = now;
    tour_.icon = backwards && stepOf(tour_.step) == Step::quick_bar ? tourIcons() - 1 : 0;
    tourClosePanels();
    tour_.preview.focus(stepOf(tour_.step) == Step::layout ? static_cast<int>(settings_.home_layout) : -1, now);
    if (!tourOpenPanel()) audio_.navigate(); // Panels play their own sound.
    InvalidateRect(hwnd_, nullptr, FALSE);
}

// Also called every frame: a library that is still loading, or a panel closed by a library
// refresh, gets its panel as soon as it can show one. Without it the card carries the text.
bool Launcher::tourOpenPanel() {
    if (!tour_.open || screen_ != ScreenMode::shelves || pending_launch_) return false;
    switch (stepOf(tour_.step)) {
    case Step::categories:
        if (snitchActive()) return false;
        if (xmbActive()) return false; // The top row is the category list.
        if (metroActive()) { // The tabs are the category list.
            if (!metro_tabs_) metroFocusTabs(true);
            return false;
        }
        if (categoryRailActive()) {
            if (!category_rail_focus_) { category_rail_focus_ = true; InvalidateRect(hwnd_, nullptr, FALSE); }
            return false;
        }
        if (category_menu_open_ || categories_.size() < 2) return false;
        openCategoryMenu();
        return category_menu_open_;
    case Step::drawer:
        if (friends_open_) return false;
        toggleFriendsDrawer();
        return friends_open_;
    case Step::quick_bar:
        if (quick_open_) return false;
        // Like the Guide button: the bar belongs to the first row.
        if (xmbActive() && xmb_row_ >= 0) xmbFocus(xmb_column_, -1);
        if (flixActive() && flix_row_ > 0 && !flix_rows_.empty()) flixFocus(0, categories_[flix_rows_[0].category].selected);
        if (!xmbActive() && !flixActive() && category_ != 0 && !categories_.empty()) showCategory(0);
        openQuick();
        if (!quick_open_) return false;
        quick_selected_ = std::clamp(tour_.icon, 0, tourIcons() - 1);
        return true;
    case Step::menu: {
        if (menu_open_ || cards_.empty() || scanning_) return false;
        if (selected_ >= cards_.size() || !isGame(cards_[selected_].game)) {
            const auto game = std::find_if(cards_.begin(), cards_.end(), [](const GameCard& card) { return isGame(card.game); });
            if (game == cards_.end()) return false;
            focusCard(static_cast<size_t>(game - cards_.begin()));
        }
        openContextMenu();
        return menu_open_;
    }
    default: return false;
    }
}

void Launcher::tourNext() {
    const auto step = stepOf(tour_.step);
    if (step == Step::quick_bar && quick_open_ && tour_.icon + 1 < tourIcons()) {
        quick_selected_ = ++tour_.icon;
        tour_.icon_at = GetTickCount64();
        audio_.navigate();
        return;
    }
    if (step == Step::layout) { tourFinish(); return; }
    tourEnter(tour_.step + 1);
}

void Launcher::tourPrevious() {
    if (stepOf(tour_.step) == Step::quick_bar && quick_open_ && tour_.icon > 0) {
        quick_selected_ = --tour_.icon;
        tour_.icon_at = GetTickCount64();
        audio_.navigate();
        return;
    }
    if (tour_.step == 0) { navigationBoundary(); return; }
    tourEnter(tour_.step - 1, true);
}

void Launcher::tourPress(int button) {
    if (button == tour::back) tourPrevious();
    else if (button == tour::skip) tourFinish();
    else tourNext();
}

void Launcher::tourFinish() {
    if (!tour_.open) return;
    tourClosePanels();
    tour_.open = false;
    tour_.hover_button = tour_.hover_tile = -1;
    settings_.tour_done = true;
    settings_dirty_ = true;
    settings_save_failed_ = false;
    settings_edited_ = std::chrono::steady_clock::now();
    audio_.context();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

// The chosen layout applies at once; the home screen behind the tour is rebuilt in it.
void Launcher::tourLayout(int option, bool keyed) {
    const int current = static_cast<int>(settings_.home_layout);
    option = std::clamp(option, 0, static_cast<int>(settings_ui::home_layouts.size()) - 1);
    tour_.tiles = true;
    tour_.preview.focus(option, GetTickCount64());
    if (option == current) { if (keyed) navigationBoundary(); return; }
    setHomeLayout(option);
    if (!categories_.empty()) rebuildCards();
    audio_.navigate();
}

void Launcher::tourFocusButton(int amount) {
    const int first = tour_.step > 0 ? tour::back : tour::skip;
    const int next = std::clamp(tour_.button + amount, first, tour::button_count - 1);
    if (next == tour_.button) { navigationBoundary(); return; }
    tour_.button = next;
    audio_.navigate();
}

void Launcher::tourKey(WPARAM key, bool repeated) {
    if (!tour_.open) return;
    const bool layouts = stepOf(tour_.step) == Step::layout;
    if (key == VK_F11) { if (!repeated) toggleFullscreen(); return; }
    if (key == VK_LEFT || key == VK_RIGHT || key == 'A' || key == 'D') {
        const int amount = key == VK_LEFT || key == 'A' ? -1 : 1;
        if (layouts && tour_.tiles) tourLayout(static_cast<int>(settings_.home_layout) + amount, true);
        else tourFocusButton(amount);
    } else if (key == VK_DOWN && layouts && tour_.tiles) {
        tour_.tiles = false; tour_.button = tour::next; audio_.navigate();
    } else if (key == VK_UP && layouts && !tour_.tiles) {
        tour_.tiles = true; tour_.preview.focus(static_cast<int>(settings_.home_layout), GetTickCount64()); audio_.navigate();
    } else if ((key == VK_RETURN || key == VK_SPACE) && !repeated) {
        if (layouts && tour_.tiles) tourFinish(); else tourPress(tour_.button);
    } else if ((key == VK_ESCAPE || key == VK_BACK) && !repeated) tourPrevious();
    else return;
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::tourClick(float x, float y) {
    if (!tour_.open) return;
    const auto view = renderer_.viewport();
    const tour::Layout layout{view.width, view.height, tourCardAtBottom(), tour::Layout::firstFor(static_cast<int>(settings_.home_layout))};
    if (stepOf(tour_.step) == Step::layout) {
        if (const int tile = layout.hitTile(x, y); tile >= 0) { tourLayout(tile, false); InvalidateRect(hwnd_, nullptr, FALSE); return; }
    }
    const int button = layout.hitButton(x, y, tour_.step > 0);
    if (button < 0) return;
    tour_.button = button;
    tourPress(button);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::tourHover(float x, float y) {
    if (!tour_.open) return;
    const auto view = renderer_.viewport();
    const tour::Layout layout{view.width, view.height, tourCardAtBottom(), tour::Layout::firstFor(static_cast<int>(settings_.home_layout))};
    const int button = layout.hitButton(x, y, tour_.step > 0);
    const int tile = stepOf(tour_.step) == Step::layout ? layout.hitTile(x, y) : -1;
    if (button == tour_.hover_button && tile == tour_.hover_tile) return;
    if (tile != tour_.hover_tile) // Hovering previews a layout without choosing it, as in Settings.
        tour_.preview.focus(tile >= 0 ? tile : tour_.tiles ? static_cast<int>(settings_.home_layout) : -1, GetTickCount64());
    tour_.hover_button = button;
    tour_.hover_tile = tile;
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::tickTour(float dt, bool& needs_paint) {
    const float target = tour_.open ? 1.0f : 0.0f;
    if (std::abs(target - tour_.alpha) > 0.003f) { tour_.alpha += (target - tour_.alpha) * ease(dt, 10.0f); needs_paint = true; }
    else tour_.alpha = target;
    if (!tour_.open) return;
    const auto now = GetTickCount64();
    if (now - tour_.step_at < static_cast<uint64_t>(fade_seconds * 1000) + 50) needs_paint = true;
    tourOpenPanel();
    if (stepOf(tour_.step) == Step::quick_bar && quick_open_) {
        if (tour_.icon + 1 < tourIcons() && now - tour_.icon_at >= icon_ms) {
            ++tour_.icon; tour_.icon_at = now;
            audio_.navigate();
        }
        if (quick_selected_ != tour_.icon) { quick_selected_ = std::clamp(tour_.icon, 0, tourIcons() - 1); needs_paint = true; }
    }
    if (tour_.preview.tick(now)) needs_paint = true;
}

void Launcher::tourView(TourView& view) const {
    using Target = TourView::Target;
    const auto step = stepOf(tour_.step);
    const auto now = GetTickCount64();
    view.step = tour_.step;
    view.steps = tour::step_count;
    view.alpha = tour_.alpha;
    view.fade = std::clamp(static_cast<float>(now - tour_.step_at) / (fade_seconds * 1000), 0.0f, 1.0f);
    view.card_bottom = tourCardAtBottom();
    view.back = tour_.step > 0;
    view.last = step == Step::layout;
    view.button = step == Step::layout && tour_.tiles ? -1 : tour_.button;
    view.hover_button = tour_.hover_button;
    // `how` opens the panel, `what` is in it: the bubble beside the open panel shows `what`;
    // when the panel cannot open (no games yet), the card carries both.
    std::wstring how, what;
    Target target = Target::none;
    switch (step) {
    case Step::welcome:
        view.title = tr(L"Witaj w Launchme");
        how = tr(L"Krótki przewodnik po ekranie głównym: panele po bokach, pasek na dole, menu gry i wybór układu. Uruchomisz go ponownie w Ustawieniach → System.");
        break;
    case Step::categories:
        view.title = tr(L"Kategorie");
        if (snitchActive()) {
            view.title = tr(L"Wszystkie gry");
            how = tr(L"Dwanaście ostatnich gier i kafel Wszystkie otwierający pełną bibliotekę.");
            break;
        }
        if (xmbActive()) {
            target = Target::xmb_row;
            how = tr(L"Górny rząd kafelków.");
            what = tr(L"Ostatnio grane, wszystkie gry, kolekcje Steam, platformy PC i konsole. Gry wybranego kafelka są listą pod nim.");
            break;
        }
        what = tr(L"Ostatnio grane, platformy PC, emulacja i kolekcje Steam.");
        if (metroActive()) { how = tr(L"Zakładki u góry: ruch w górę z górnego rzędu kafli, potem w lewo i w prawo. LB/RB zmieniają stronę od razu."); break; }
        if (categoryRailActive()) { target = Target::category_rail; how = tr(L"Lista po lewej stronie jest zawsze widoczna."); }
        else {
            how = tr(L"Ruch w lewo z pierwszej okładki rzędu.");
            if (category_menu_open_ || category_menu_progress_ > 0.01f) target = Target::category_menu;
        }
        break;
    case Step::drawer:
        view.title = tr(L"Pulpit");
        how = tr(L"Klawisz Tab albo przycisk Start / Options.");
        what = tr(L"Zegar, znajomi Steam, Discord, temperatury i widgety pluginów. Ikona obok zegara zmienia układ kafelków.");
        if (friends_open_ || friends_progress_ > 0.01f) target = Target::drawer;
        break;
    case Step::quick_bar: {
        view.title = tr(L"Pasek na dole");
        how = metroActive() || snitchActive() ? tr(L"Ruch w dół z dolnego rzędu kafli albo przycisk Guide / PS (klawisz Home).") :
              psfiveActive() ? tr(L"Ikona menu w prawym górnym rogu, ruch w dół z przycisków Graj / … albo przycisk Guide / PS (klawisz Home).") :
                              tr(L"Ruch w górę z pierwszego rzędu albo przycisk Guide / PS (klawisz Home).");
        const int plugins = static_cast<int>(plugin_menu_.size()), icon = std::clamp(tour_.icon, 0, tourIcons() - 1);
        const auto action = quick_menu::action(icon, plugins);
        view.progress = static_cast<float>(icon + 1) / static_cast<float>(tourIcons());
        if (quick_open_ || quick_progress_ > 0.01f) {
            target = Target::quick_bar;
            const int plugin = quick_menu::pluginIndex(icon);
            view.bubble_title = action == quick_menu::Action::store && plugin >= 0 && plugin < plugins ?
                plugin_menu_[static_cast<size_t>(plugin)].item.title.localized() : std::wstring(quick_menu::caption(action));
        }
        what = iconText(action);
        break;
    }
    case Step::menu:
        view.title = tr(L"Menu gry");
        how = tr(L"Przytrzymanie Enter lub A, przycisk Y / Trójkąt albo prawy przycisk myszy.");
        what = tr(L"Graj, szczegóły i osiągnięcia, zmiana grafiki i poprawa kadru okładki. Pluginy dodają tu własne akcje.");
        if (menu_open_ || menu_progress_ > 0.01f) target = Target::menu;
        break;
    case Step::suspend:
        view.title = tr(L"Wstrzymywanie gier");
        view.tag = tr(L"Eksperymentalne");
        how = tr(L"Uruchomioną grę można zamrozić z menu gry i wrócić do niej później, także po uruchomieniu innej. Włączysz to w Ustawieniach → System → Szybkie wznawianie. Gry online mogą się rozłączyć.");
        break;
    case Step::layout:
        view.title = tr(L"Układ ekranu głównego");
        how = tr(L"Zmienisz go w Ustawieniach → Interfejs. W Ustawieniach są też konta GOG, Epic, EA, Ubisoft, Battle.net i Xbox, katalogi DRM-free (Biblioteka), klucze API, dźwięk, kontrolery, pluginy i wygaszacz.");
        view.layouts = true;
        view.layout_choice = static_cast<int>(settings_.home_layout);
        view.focus_tile = tour_.tiles ? view.layout_choice : -1;
        view.hover_tile = tour_.hover_tile;
        view.preview_option = tour_.preview.option;
        view.preview_seconds = tour_.preview.seconds(now);
        view.layout_descriptions = {tr(L"Duża okładka wybranej gry i rząd kart."), tr(L"Rząd kafelków, gry jako lista pod nimi."),
                                    tr(L"Rzędy plakatów, ostatnio grane na górze."),
                                    tr(L"Strony kafli: ostatnie gry, dalej kategorie."),
                                    tr(L"Rząd ostatnich gier nad dużym tłem."),
                                    tr(L"Rząd okładek i małe białe ikony na dole.")};
        break;
    }
    view.target = target;
    if (target != Target::none) { view.body = how; view.bubble_body = what; }
    else view.body = what.empty() ? how : how + L" " + what;
}
