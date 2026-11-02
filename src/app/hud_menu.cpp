// The options of the FPS overlay (the widget on the game's screen), as a dialog like Zasilanie: the FPS, CPU
// and GPU tiles of the drawer all open this one dialog. The first row is the master switch: off, it hides the
// rows of the widget below it and the widget is gone from the game altogether. Then a switch for each line
// (FPS, CPU, GPU, RAM, VRAM), the frame graph and the look (layout, coloured labels, size), and the limiter:
// its switch, the limit (Left/Right through 30, 60, 72, 144...) and its mode.
// Each row is a switch on the right (Left = off, Right = on) or a value with arrows (Left/Right change it).
// It reuses the power menu's layer (power_menu_ + the rows below instead of power_items_); more rows than
// hud_menu_window scroll.
#include "launcher.hpp"

namespace {
struct Pin { const char* id; const wchar_t* name; wchar_t icon; };
const Pin pins[] = {
    {"fps", trKey(L"FPS"), 0xE9D9},
    {"cpu", trKey(L"Temperatura CPU"), 0xE950},
    {"gpu", trKey(L"Temperatura GPU"), 0xE7F4},
    {"ram", trKey(L"Pamięć RAM"), 0xE964},
    {"vram", trKey(L"Pamięć karty (VRAM)"), 0xE7F4},
};
constexpr int limits[] = {0, 30, 40, 45, 60, 72, 90, 120, 144, 165, 240};
constexpr int limit_count = static_cast<int>(sizeof(limits) / sizeof(limits[0]));
int limitIndex(int limit) { // The exact entry, else the nearest one.
    int best = 0;
    for (int i = 0; i < limit_count; ++i) if (std::abs(limits[i] - limit) < std::abs(limits[best] - limit)) best = i;
    return best;
}
std::wstring limitText(int limit) { return limit ? std::to_wstring(limit) + L" FPS" : std::wstring(tr(L"Bez limitu")); }
}

size_t Launcher::powerMenuCount() const { return hud_menu_ ? hud_menu_rows_.size() : power_items_.size(); }

// The rows shown at once: the window of the dialog, hud_menu_first_ on.
int Launcher::hudMenuVisible() const {
    return std::min(hud_menu_window, std::max(0, static_cast<int>(hud_menu_rows_.size()) - hud_menu_first_));
}

std::vector<bool> Launcher::powerMenuGaps() const {
    if (!hud_menu_) return power_menu::gaps(power_items_);
    std::vector<bool> gaps;
    for (int i = 0; i < hudMenuVisible(); ++i) gaps.push_back(hud_menu_rows_[static_cast<size_t>(hud_menu_first_ + i)].gap);
    return gaps;
}

power_menu::Geometry Launcher::powerMenuGeometryFor(float width, float height) const {
    return {width, height, powerMenuGaps(), hud_menu_ ? 150.0f : 0.0f};
}

// Keeps the chosen row in the window.
void Launcher::hudMenuFollow() {
    if (!hud_menu_ || !power_menu_) return;
    const int rows = static_cast<int>(hud_menu_rows_.size());
    if (*power_menu_ < hud_menu_first_) hud_menu_first_ = *power_menu_;
    if (*power_menu_ >= hud_menu_first_ + hud_menu_window) hud_menu_first_ = *power_menu_ - hud_menu_window + 1;
    hud_menu_first_ = std::clamp(hud_menu_first_, 0, std::max(0, rows - hud_menu_window));
}

void Launcher::buildHudMenu() {
    hud_menu_rows_.clear();
    const auto target = overlay_snapshot_.target;
    const auto add = [&](HudAction action, std::wstring label, wchar_t icon, bool gap, std::string key = {}, int toggle = -1, std::wstring value = {}) {
        hud_menu_rows_.push_back({action, std::move(label), icon, gap, std::move(key), toggle, std::move(value)});
    };
    add(HudAction::master, tr(L"Widget w grze"), 0xE7B3, false, {}, settings_.hud_enabled);
    if (settings_.hud_enabled) {
        for (const auto& pin : pins) {
            if (std::string_view(pin.id) == "fps" && !settings_.game_fps) continue; // Its measuring is off.
            const bool shown = std::find(settings_.hud_pins.begin(), settings_.hud_pins.end(), pin.id) != settings_.hud_pins.end();
            add(HudAction::pin, tr(pin.name), pin.icon, false, pin.id, shown);
        }
        if (settings_.game_fps) add(HudAction::graph, tr(L"Wykres klatek"), 0xE9D2, false, {}, settings_.fps_graph);
        add(HudAction::layout, tr(L"Układ"), 0xE8A9, false, {}, -1, settings_.hud_layout == 1 ? tr(L"Pasek") : tr(L"Lista"));
        add(HudAction::colors, tr(L"Kolorowe etykiety"), 0xE790, false, {}, settings_.hud_label_colors);
        add(HudAction::size, tr(L"Rozmiar widgetu"), 0xE8E9, false, {}, -1, std::to_wstring(std::clamp(settings_.hud_size, 50, 100)) + L"%");
    }
#ifdef _WIN32
    add(HudAction::limiter, tr(L"Limiter klatek (biblioteka ładowana do gry)"), 0xE916, true, {}, settings_.fps_limiter);
    if (settings_.fps_limiter) {
        const int limit = target ? gameFpsLimit(target->key) : settings_.fps_limit;
        add(HudAction::limit, target ? tr(L"Limit FPS tej gry") : tr(L"Limit FPS"), 0xE916, false, {}, -1, limitText(limits[limitIndex(limit)]));
        add(HudAction::mode, tr(L"Tryb limitera"), 0xE9D9, false, {}, -1,
            settings_.fps_limit_mode == 1 ? tr(L"Niskie opóźnienie (adaptacyjny)") : tr(L"Równe klatki (smooth)"));
    }
#endif
    add(HudAction::close, tr(L"Zamknij"), 0xE72B, true);
    hud_menu_first_ = std::clamp(hud_menu_first_, 0, std::max(0, static_cast<int>(hud_menu_rows_.size()) - hud_menu_window));
}

void Launcher::openHudMenu() {
    if (power_menu_) return;
    hud_menu_ = true;
    hud_menu_first_ = 0;
    power_items_.clear();
    buildHudMenu();
    power_menu_ = 0;
    audio_.context();
    invalidateGameOverlay();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

// A row chosen (direction 0: Enter, A or a click) or adjusted (-1 Left, +1 Right). Everything but "Zamknij"
// keeps the dialog open with the row's new state.
void Launcher::hudMenuApply(int direction) {
    if (!hud_menu_ || !power_menu_ || *power_menu_ >= static_cast<int>(hud_menu_rows_.size())) return;
    const int row = *power_menu_;
    const auto item = hud_menu_rows_[static_cast<size_t>(row)];
    const auto target = overlay_snapshot_.target;
    // A switch: Left off, Right on, Enter flips.
    const auto flip = [&](bool& flag) { const bool next = direction == 0 ? !flag : direction > 0; if (next == flag) return false; flag = next; return true; };
    bool changed = true;
    switch (item.action) {
    case HudAction::master: changed = flip(settings_.hud_enabled); break;
    case HudAction::pin: {
        const auto& list = settings_.hud_pins;
        const bool shown = std::find(list.begin(), list.end(), item.key) != list.end();
        changed = (direction == 0 ? !shown : direction > 0) != shown;
        if (changed) toggleHudPin(item.key);
        break;
    }
    case HudAction::graph: changed = flip(settings_.fps_graph); break;
    case HudAction::colors: changed = flip(settings_.hud_label_colors); break;
    case HudAction::limiter: changed = flip(settings_.fps_limiter); break;
    case HudAction::layout: {
        const int next = direction == 0 ? 1 - settings_.hud_layout : direction > 0 ? 1 : 0;
        changed = next != settings_.hud_layout; settings_.hud_layout = next; break;
    }
    case HudAction::mode: {
        const int next = direction == 0 ? 1 - settings_.fps_limit_mode : direction > 0 ? 1 : 0;
        changed = next != settings_.fps_limit_mode; settings_.fps_limit_mode = next; break;
    }
    case HudAction::size: {
        const int current = std::clamp(settings_.hud_size, 50, 100);
        const int next = direction == 0 ? (current <= 50 ? 100 : current - 10) : std::clamp(current + 10 * direction, 50, 100);
        changed = next != current; settings_.hud_size = next; break;
    }
    case HudAction::limit: {
        const int current = limitIndex(target ? gameFpsLimit(target->key) : settings_.fps_limit);
        const int next = direction == 0 ? (current + 1) % limit_count : std::clamp(current + direction, 0, limit_count - 1);
        changed = next != current;
        if (target) settings_.fps_game_limits[target->key] = limits[next]; // This game's own limit (0: none for it).
        else settings_.fps_limit = limits[next];
        break;
    }
    case HudAction::close: closePowerMenu(); return;
    }
    if (!changed) { navigationBoundary(); return; }
    next_hud_at_ = 0;
    settingsChanged();
    buildHudMenu();
    power_menu_ = std::min(row, static_cast<int>(hud_menu_rows_.size()) - 1);
    hudMenuFollow();
    audio_.context();
    invalidateGameOverlay();
    InvalidateRect(hwnd_, nullptr, FALSE);
}
