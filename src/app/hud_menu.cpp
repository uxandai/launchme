// The options of the widget on the game's screen, as a dialog like Zasilanie: the FPS (CPU, GPU) tile of
// the drawer opens it instead of switching the widget. The first row shows or hides the widget the tile
// belongs to; the others change the look of all of them (layout, coloured labels, size, frame graph).
// It reuses the power menu's layer (power_menu_ + the rows below instead of power_items_).
#include "launcher.hpp"

namespace {
struct Names { const wchar_t* show; const wchar_t* hide; };
Names namesOf(const std::string& id) {
    if (id == "cpu") return {trKey(L"Pokaż temperaturę CPU na ekranie gry"), trKey(L"Ukryj temperaturę CPU na ekranie gry")};
    if (id == "gpu") return {trKey(L"Pokaż temperaturę GPU na ekranie gry"), trKey(L"Ukryj temperaturę GPU na ekranie gry")};
    return {trKey(L"Pokaż FPS na ekranie gry"), trKey(L"Ukryj FPS na ekranie gry")};
}
}

size_t Launcher::powerMenuCount() const { return hud_menu_ ? hud_menu_rows_.size() : power_items_.size(); }

std::vector<bool> Launcher::powerMenuGaps() const {
    if (!hud_menu_) return power_menu::gaps(power_items_);
    std::vector<bool> gaps;
    for (const auto& row : hud_menu_rows_) gaps.push_back(row.gap);
    return gaps;
}

void Launcher::buildHudMenu() {
    hud_menu_rows_.clear();
    const auto& pins = settings_.hud_pins;
    const bool shown = std::find(pins.begin(), pins.end(), hud_menu_pin_) != pins.end();
    const auto names = namesOf(hud_menu_pin_);
    hud_menu_rows_.push_back({HudAction::visibility, tr(shown ? names.hide : names.show), 0xE7B3, false});
    hud_menu_rows_.push_back({HudAction::layout, settings_.hud_layout == 1 ? tr(L"Układ: pasek") : tr(L"Układ: lista"), 0xE8A9, false});
    hud_menu_rows_.push_back({HudAction::colors, settings_.hud_label_colors ? tr(L"Kolorowe etykiety: włączone") : tr(L"Kolorowe etykiety: wyłączone"), 0xE790, false});
    hud_menu_rows_.push_back({HudAction::size, std::wstring(tr(L"Rozmiar widgetu: ")) + std::to_wstring(std::clamp(settings_.hud_size, 50, 100)) + L"%", 0xE8E9, false});
    if (settings_.game_fps)
        hud_menu_rows_.push_back({HudAction::graph, settings_.fps_graph ? tr(L"Wykres klatek: włączony") : tr(L"Wykres klatek: wyłączony"), 0xE9D9, false});
    hud_menu_rows_.push_back({HudAction::close, tr(L"Zamknij"), 0xE72B, true});
}

void Launcher::openHudMenu(const std::string& pin) {
    if (power_menu_) return;
    hud_menu_ = true;
    hud_menu_pin_ = pin;
    power_items_.clear();
    buildHudMenu();
    power_menu_ = 0;
    audio_.context();
    invalidateGameOverlay();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

// One row chosen. Everything but "Zamknij" keeps the dialog open with the row's new state in its label.
void Launcher::hudMenuAccept() {
    if (!hud_menu_ || !power_menu_ || *power_menu_ >= static_cast<int>(hud_menu_rows_.size())) return;
    const int row = *power_menu_;
    switch (hud_menu_rows_[static_cast<size_t>(row)].action) {
    case HudAction::visibility: toggleHudPin(hud_menu_pin_); break;
    case HudAction::layout: settings_.hud_layout = settings_.hud_layout == 1 ? 0 : 1; break;
    case HudAction::colors: settings_.hud_label_colors = !settings_.hud_label_colors; break;
    case HudAction::size: settings_.hud_size = settings_.hud_size <= 50 ? 100 : settings_.hud_size - 10; break;
    case HudAction::graph: settings_.fps_graph = !settings_.fps_graph; break;
    case HudAction::close: closePowerMenu(); return;
    }
    next_hud_at_ = 0;
    settingsChanged();
    buildHudMenu();
    power_menu_ = std::min(row, static_cast<int>(hud_menu_rows_.size()) - 1);
    audio_.context();
    invalidateGameOverlay();
    InvalidateRect(hwnd_, nullptr, FALSE);
}
