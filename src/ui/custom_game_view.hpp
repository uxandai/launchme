#pragma once
#include "settings_view.hpp"
#include "../i18n/i18n.hpp"

#include <algorithm>
#include <array>
#include <optional>
#include <string>
#include <vector>

// Add/edit panel of a user-added game (Settings → Library). One focus moves through
// the text fields, their "Wybierz…" buttons and the bottom buttons; geometry is shared
// by the renderer, pointer hit testing and the tests.
// A user-added game's panel shows its poster beside the fields (matched by the typed name) with
// "Zidentyfikuj grę" under it.
// The same panel edits how another game starts ("Opcje uruchamiania…" in the game's menu): a
// DRM-free game's file, options and working directory, a Steam game's launch options only.
// Linux, a Windows game (State::compat): also its WINEDLLOVERRIDES and its Wine/Proton launch options
// as Steam writes them (VAR=value wrapper %command% arguments), with "Proton / Wine…" to its page.
// Under the fields of "Uruchamianie" (State::options): the game's emulator and its Steam account (choices,
// ‹ ›), Aurelia instead of the Steam client, the logs and HDR (switches); under the title what starts the
// game (State::managed): Steam, Aurelia, an emulator, the game's own file or another store's client.
// Every user-added game (a new one too: its scripts are saved with it) and every game's launch options
// (State::tabs()) have two tabs: "Uruchamianie" (the above) and "Skrypty", its
// commands before the launch, after the start and after the exit, each with "Testuj"
// (src/platform/launch_scripts.hpp).
// Windows, a game with a folder (or an emulated game, its emulator's): a third tab, "Mody" (Launcher's
// game_mods*.cpp): its folder with "Zmień…", then one row per mod (ReShade, RenoDX, OptiScaler, DLSS,
// DLSS 5, ANR FG; an emulator ReShade alone): its state, the method (‹ › / Left-Right), install or update,
// and remove. They act at once; Zapisz / Anuluj keep their meaning for the other tabs.
namespace custom_game_ui {
// pick_file / pick_folder: a new game's two pills above the fields ("Wskaż plik", "Wskaż folder"); they
// take the file row's "Wybierz…" place.
// option: one of the options under the fields (State::options, the focused one State::option); option_more:
// its "…" (the Aurelia row's: Zarządzaj · Aurelia).
// tab_launch, tab_scripts: the tabs above the fields; before…exited: the scripts' fields, test_*: their "Testuj".
enum class Item { name, file, file_browse, arguments, directory, directory_browse, dll, wine, wine_page, platform, identify, remove, cancel, save,
                  pick_file, pick_folder, option, option_more, tab_launch, tab_scripts, before, test_before, started, test_started, exited, test_exited,
                  tab_mods, mods_folder, mod_method, mod_action, mod_remove };
// mod_method, mod_action, mod_remove: the focused mod row's (State::mod_row) method, its install/update button and "Usuń".
inline bool modItem(Item item) { return item == Item::mod_method || item == Item::mod_action || item == Item::mod_remove; }
inline bool tabItem(Item item) { return item == Item::tab_launch || item == Item::tab_scripts || item == Item::tab_mods; }
// platform_field: an emulated game's console (Left/Right or Enter pick another; never typed). With it the
// file is the ROM, the options an emulator command of its own and the working directory has no use.
// before_field…exited_field: the "Skrypty" tab's (launch_scripts::Phase order).
enum Field { name_field, file_field, arguments_field, directory_field, dll_field, wine_field, platform_field,
             before_field, started_field, exited_field, field_count };
inline bool scriptField(int index) { return index >= before_field && index <= exited_field; }
enum class Tab { launch, scripts, mods };
// One mod's row on the Mody tab, as the launcher describes it.
struct ModRow {
    std::wstring name;    // "ReShade"
    std::wstring status;  // "Zainstalowany przez Launchme · 6.8.0 · dxgi.dll", "Nie zainstalowano"…
    std::wstring method;  // The chosen method's label.
    std::wstring action;  // "Zainstaluj", "Aktualizuj", "Zastąp", "Zainstaluj mimo to"
    bool methods = false;   // More than one method to pick from.
    bool removable = false; // Launchme's own install: "Usuń".
    bool available = true;  // Greyed when it cannot go into this game (no DLSS, 32-bit…).
    bool installed = false;
    bool confirm_remove = false; // "Usuń" pressed once.
};
// An option under the launch tab's fields, a row each: a choice opens a list of its values (Left/Right step
// it), a switch flips. emulator: an emulated game's own emulator (Launcher's game_emulator.cpp);
// steam_account: the remembered account it starts from; aurelia: managed by Aurelia instead of the Steam
// client, with "…" (Zarządzaj · Aurelia); log, hdr: its switches.
struct Option {
    enum class Kind { emulator, steam_account, aurelia, log, hdr } kind = Kind::log;
    std::wstring label;
    std::wstring value; // A choice's current value.
    bool on = false;    // A switch's state.
    bool more = false;  // "…" at the row's end.
    bool choice() const { return kind == Kind::emulator || kind == Kind::steam_account; }
};
// A list opened from a choice (Item::option, its index) or a mod's method (Item::mod_method, its row):
// its values, the chosen one ticked, the focused one lit.
struct Dropdown {
    bool open = false;
    Item owner = Item::option;
    int index = 0;
    std::vector<std::wstring> items;
    int chosen = 0, focus = 0;
};
// other: a game of another store or a ROM folder's, whose client or emulator takes no options here.
enum class Target { custom, drm_free, steam, other };
// The poster beside the fields: nothing to look for (no name), being looked for, shown, none found.
enum class Poster { none, loading, shown, missing };

struct State {
    bool open = false;
    std::string id; // Empty while adding a game.
    Target target = Target::custom;
    std::string key;    // drm_free: its catalog entry ID; steam: the game's gameKey.
    std::wstring title; // The edited game's name (launch options).
    std::array<std::wstring, field_count> fields;
    Item focus = Item::name;
    size_t caret = 0;            // In the focused text field.
    bool selected = false;       // Ctrl+A: the next edit replaces the whole field.
    bool confirm_remove = false; // "Usuń z biblioteki" pressed once; the second press removes.
    std::wstring error;
    // A user-added game: the ID a new game is saved under (so Zidentyfikuj grę can key it before
    // it is saved), the name the game's stored artwork belongs to (matched for or identified as),
    // whether a new game was identified (Anuluj drops what that stored), and its poster's state.
    std::string pending_id;
    std::wstring art_name;
    bool identified_new = false;
    Poster poster = Poster::none;
    Item identify_from = Item::name; // Left from "Zidentyfikuj grę" goes back where Right came from.
    // Linux, a Windows game: its Wine/Proton fields (dll_field, wine_field), its key and what they held
    // when the panel opened (saved only when changed).
    bool compat = false;
    std::string game_key;
    std::wstring dll_opened, wine_opened;
    // Opcje uruchamiania's options of the game (its switches in LibraryState::launch_flags by flags_key):
    // not for a new game, which has no key yet. managed: what starts it, under the title.
    bool flags = false;
    std::string flags_key;
    std::vector<Option> options;
    int option = 0;
    std::wstring managed;
    bool switchOn(Option::Kind kind) const {
        for (const auto& item : options) if (item.kind == kind) return item.on;
        return false;
    }
    // The tabs: a game with a key (an edited one or another's launch options), and a new user-added game
    // (its scripts saved under the key it gets).
    Tab tab = Tab::launch;
    std::wstring note; // A script test's outcome under the fields (ui.error wins).
    bool note_failed = false;
    bool testing = false; // A test runs: "Testuj" waits for it.
    // The Mody tab: offered (a Windows game with a folder), its rows, the focused one, where they go.
    bool mods_offered = false;
    std::vector<ModRow> mod_rows;
    int mod_row = 0;
    std::wstring mod_folder;  // The program's folder ("" while unknown).
    std::wstring mod_target;  // "Emulator: PCSX2" for an emulated game; empty for a game.
    bool mod_busy = false;    // An install or removal runs (the note says what).
    bool tabs() const { return flags || (target == Target::custom && !editing()); }
    bool scripts() const { return tabs() && tab == Tab::scripts; }
    bool mods() const { return tabs() && mods_offered && tab == Tab::mods; }
    int tabCount() const { return tabs() ? (mods_offered ? 3 : 2) : 0; }
    Item tabItemOf(Tab which) const { return which == Tab::mods ? Item::tab_mods : which == Tab::scripts ? Item::tab_scripts : Item::tab_launch; }
    int optionCount() const { return flags && !scripts() && !mods() ? static_cast<int>(options.size()) : 0; }
    Dropdown dropdown;
    bool editing() const { return !id.empty(); }
    bool launchOptions() const { return target != Target::custom; }
    // The desktop window: a new game has the file row's "Wybierz" like an edited one, and "Dodaj folder jako bibliotekę" at the bottom.
    bool desktop = false;
    // Tryb TV: "Wskaż plik" / "Wskaż folder" (the launch tab's).
    bool pills() const { return target == Target::custom && !editing() && !desktop && tab == Tab::launch; }
    int modCount() const { return static_cast<int>(mod_rows.size()); }
    bool folderButton() const { return target == Target::custom && !editing() && desktop && tab == Tab::launch; }
    // The file row's "Wybierz…" (a game being edited; a new one has the pills).
    bool browses(int index) const { return index != file_field || !pills(); }
    bool posterShown() const { return target == Target::custom; }
    bool posterVisible() const { return posterShown() && !scripts() && !mods(); } // The scripts' and mods' tabs use the whole width.
    const std::string& gameId() const { return editing() ? id : pending_id; }
    bool emulated() const { return target == Target::custom && !fields[platform_field].empty(); }
    bool shows(int index) const {
        if (scriptField(index)) return scripts();
        if (scripts() || mods()) return false;
        if (index == dll_field || index == wine_field) return compat;
        if (index == platform_field) return emulated();
        if (index == directory_field && emulated()) return false;
        if (target == Target::other) return false;
        return target == Target::custom || (target == Target::drm_free ? index != name_field : index == arguments_field);
    }
    unsigned hidden() const { // Bit per field the panel leaves out.
        unsigned mask = 0;
        for (int index = 0; index < field_count; ++index) if (!shows(index)) mask |= 1u << index;
        return mask;
    }
};

// An emulated game's file is its ROM and its options an emulator command of its own.
inline const wchar_t* emulatedLabel(const State& state, int index, const wchar_t* label) {
    if (!state.emulated()) return label;
    if (index == file_field) return tr(L"Plik ROM lub obraz płyty");
    if (index == arguments_field) return tr(L"Polecenie emulatora");
    return label;
}
inline const wchar_t* emulatedHint(const State& state, int index) {
    if (!state.emulated()) return nullptr;
    if (index == arguments_field) return tr(L"Puste: emulator systemu (Ustawienia → Emulacja). Np. \"C:\\Emu\\pcsx2.exe\" %ROM%");
    if (index == platform_field) return tr(L"Strzałki lub Enter: inna platforma");
    return nullptr;
}

// What a script's empty field suggests.
inline const wchar_t* scriptHint(int index) {
#ifdef _WIN32
    if (index == before_field) return tr(L"Np. C:\\Narzędzia\\przed.bat -cicho albo Start-Process notepad");
    if (index == started_field) return tr(L"Np. Start-Process \"C:\\Narzędzia\\trener.exe\"");
    return tr(L"Np. C:\\Narzędzia\\po.vbs albo Stop-Process -Name trener");
#else
    if (index == before_field) return tr(L"Np. ~/skrypty/przed.sh --cicho");
    if (index == started_field) return tr(L"Np. notify-send \"Gra wystartowała\"");
    return tr(L"Np. ~/skrypty/po.sh; pkill trener");
#endif
}

// Text field behind an item, -1 for buttons.
inline int field(Item item) {
    switch (item) {
    case Item::name: return name_field;
    case Item::file: return file_field;
    case Item::arguments: return arguments_field;
    case Item::directory: return directory_field;
    case Item::dll: return dll_field;
    case Item::wine: return wine_field;
    case Item::platform: return platform_field;
    case Item::before: return before_field;
    case Item::started: return started_field;
    case Item::exited: return exited_field;
    default: return -1;
    }
}
// Rows 0-9 hold the fields (1 and 3 with a browse button, 5 with "Proton / Wine…", 7-9 with "Testuj"),
// then the buttons; "Zidentyfikuj grę" stands apart in the poster's column, the tabs above the fields.
inline constexpr int buttons_row = field_count, identify_row = field_count + 1, tabs_row = field_count + 2, mods_row = field_count + 3;
inline int row(Item item) {
    switch (item) {
    case Item::name: return 0;
    case Item::file: case Item::file_browse: return 1;
    case Item::arguments: return 2;
    case Item::directory: case Item::directory_browse: return 3;
    case Item::dll: return 4;
    case Item::wine: case Item::wine_page: return 5;
    case Item::platform: return 6;
    case Item::before: case Item::test_before: return before_field;
    case Item::started: case Item::test_started: return started_field;
    case Item::exited: case Item::test_exited: return exited_field;
    case Item::identify: return identify_row;
    case Item::tab_launch: case Item::tab_scripts: case Item::tab_mods: return tabs_row;
    case Item::mods_folder: case Item::mod_method: case Item::mod_action: case Item::mod_remove: return mods_row;
    default: return buttons_row;
    }
}
inline bool browseRow(int index) { return index == file_field || index == directory_field || index == wine_field || scriptField(index); }
// The button beside a row's field.
inline Item browseItem(int index) {
    switch (index) {
    case file_field: return Item::file_browse;
    case wine_field: return Item::wine_page;
    case before_field: return Item::test_before;
    case started_field: return Item::test_started;
    case exited_field: return Item::test_exited;
    default: return Item::directory_browse;
    }
}
// A row's field item (rows 0-9).
inline Item fieldItem(int index) {
    constexpr Item items[field_count] = {Item::name, Item::file, Item::arguments, Item::directory, Item::dll, Item::wine, Item::platform,
                                         Item::before, Item::started, Item::exited};
    return items[index];
}
inline bool testItem(Item item) { return item == Item::test_before || item == Item::test_started || item == Item::test_exited; }

// Tryb TV's panel, laid out as Settings: the title with what starts the game under it, the tabs, then a
// card per row over the width (a field with its label, an option, the folder or a mod), the buttons at
// the bottom. A user-added game keeps its poster in a column on the right.
struct Layout {
    float width, height;
    unsigned hidden = 0;   // State::hidden(): left-out fields; the others close up.
    bool poster = false;   // State::posterVisible(): the poster column on the right.
    bool pills = false;    // State::pills(): "Wskaż plik" / "Wskaż folder", the first row.
    bool tabs = false;     // State::tabs(): "Uruchamianie" / "Skrypty" / "Mody" above the rows.
    int tab_count = 0;
    bool mods = false;     // State::mods(): the Mody tab is shown.
    int mod_count = 0;
    int option_count = 0;
    std::vector<bool> removable, more; // Per mod row: its "Usuń"; per option: its "…".
    int rows = 0;          // The tallest tab's rows: the panel keeps its size across the tabs.
    float step = 76;       // A row and the gap under it, smaller when the tallest tab would not fit.
    Dropdown dropdown;
    static constexpr float pad = 40, gap = 10, row_gap = 10, tabs_top = 136, tabs_height = 48, tab_width = 190, poster_width = 200,
                           poster_gap = 24, button_width = 170, label_width = 320, value_width = 400, method_width = 300, remove_width = 110,
                           more_width = 46, bottom_space = 124, drop_row = 50;
    static constexpr int drop_visible = 7;
    static int rowsOf(const State& s) {
        if (s.mods()) return 1 + s.modCount();
        int count = s.pills() ? 1 : 0;
        for (int index = 0; index < field_count; ++index) count += s.shows(index);
        return count + s.optionCount();
    }
    static Layout of(const State& state, float width, float height) {
        Layout layout{width, height, state.hidden(), state.posterVisible(), state.pills(), state.tabs()};
        layout.tab_count = state.tabCount();
        layout.mods = state.mods();
        layout.mod_count = state.modCount();
        layout.option_count = state.optionCount();
        for (const auto& row : state.mod_rows) layout.removable.push_back(row.removable);
        for (const auto& option : state.options) layout.more.push_back(option.more);
        layout.dropdown = state.dropdown;
        layout.rows = rowsOf(state);
        if (layout.tabs)
            for (const auto which : {Tab::launch, Tab::scripts, Tab::mods}) {
                if (which == Tab::mods && !state.mods_offered) continue;
                auto other = state;
                other.tab = which;
                layout.rows = std::max(layout.rows, rowsOf(other));
            }
        const float room = height - 40 - layout.bodyOffset() - bottom_space;
        if (layout.rows > 0) layout.step = std::clamp((room + row_gap) / static_cast<float>(layout.rows), 58.0f, 78.0f);
        return layout;
    }
    float bodyOffset() const { return tabs ? tabs_top + tabs_height + 22 : tabs_top; }
    float rowHeight() const { return step - row_gap; }
    float bodyHeight() const {
        const float own = rows > 0 ? static_cast<float>(rows) * step - row_gap : 0.0f;
        return poster ? std::max(own, poster_width * 1.5f + 16 + 52) : own;
    }
    settings_ui::Rect panel() const {
        const float w = std::min(1240.0f, width - 120), h = std::min(height - 40, bodyOffset() + bodyHeight() + bottom_space);
        return {(width - w) * 0.5f, (height - h) * 0.5f, (width + w) * 0.5f, (height + h) * 0.5f};
    }
    bool shown(int index) const { return !(hidden & (1u << index)); }
    bool browses(int index) const { return browseRow(index) && (index != file_field || !pills); }
    int slot(int index) const { // A field's row among the shown ones (after the pills).
        int result = pills ? 1 : 0;
        for (int i = 0; i < index; ++i) result += shown(i);
        return result;
    }
    float bodyTop() const { return panel().top + bodyOffset(); }
    float rowsRight() const { return panel().right - pad - (poster ? poster_width + poster_gap : 0.0f); }
    // Row `index` of the shown tab: its card.
    settings_ui::Rect card(int index) const {
        const auto p = panel(); const float top = bodyTop() + static_cast<float>(index) * step;
        return {p.left + pad, top, rowsRight(), top + rowHeight()};
    }
    // A control inside a card, `right` from its end: as high as fits, centred.
    settings_ui::Rect control(settings_ui::Rect row, float right, float w) const {
        const float h = std::min(46.0f, row.bottom - row.top - 16), y = (row.top + row.bottom - h) * 0.5f;
        return {row.right - 12 - right - w, y, row.right - 12 - right, y + h};
    }
    // The tabs at the top left of the rows, in one track: 0 "Uruchamianie", 1 "Skrypty", 2 "Mody".
    settings_ui::Rect tabTrack() const {
        const auto p = panel(); const float top = p.top + tabs_top;
        return {p.left + pad, top, p.left + pad + 8 + static_cast<float>(tab_count) * tab_width, top + tabs_height};
    }
    settings_ui::Rect tab(int which) const {
        const auto p = panel(); const float top = p.top + tabs_top + 4, left = p.left + pad + 4 + static_cast<float>(which) * tab_width;
        return {left, top, left + tab_width, top + tabs_height - 8};
    }
    // The pills side by side in the first row: 0 "Wskaż plik", 1 "Wskaż folder".
    settings_ui::Rect pill(int which) const {
        const auto row = card(0); const float w = (row.right - row.left - gap) * 0.5f, left = row.left + static_cast<float>(which) * (w + gap);
        return {left, row.top, left + w, row.bottom};
    }
    // A field's card, its label on the left, the field and its button ("Wybierz…", "Testuj") on the right.
    settings_ui::Rect fieldRow(int index) const { return card(slot(index)); }
    settings_ui::Rect label(int index) const { const auto row = fieldRow(index); return {row.left + 24, row.top, row.left + label_width, row.bottom}; }
    settings_ui::Rect input(int index) const {
        const auto row = fieldRow(index);
        const auto r = control(row, browses(index) ? button_width + gap : 0.0f, 0);
        return {row.left + label_width + 12, r.top, r.right, r.bottom};
    }
    settings_ui::Rect browse(int index) const { return control(fieldRow(index), 0, button_width); }
    // An option's card; a choice's value (it opens the list) and the "…" at its end.
    settings_ui::Rect option(int index) const {
        if (index < 0 || index >= option_count) return {};
        return card(slot(field_count) + index);
    }
    settings_ui::Rect optionMore(int index) const { return control(option(index), 0, more_width); }
    settings_ui::Rect optionValue(int index) const { return control(option(index), 0, value_width); }
    int optionAt(float x, float y) const {
        for (int index = 0; index < option_count; ++index) if (option(index).contains(x, y)) return index;
        return -1;
    }
    // The poster (2:3) at the top of its column, "Zidentyfikuj grę" under it.
    settings_ui::Rect posterBox() const {
        const auto p = panel(); const float top = bodyTop();
        return {p.right - pad - poster_width, top, p.right - pad, top + poster_width * 1.5f};
    }
    settings_ui::Rect identify() const {
        const auto box = posterBox();
        return {box.left, box.bottom + 16, box.right, box.bottom + 16 + 52};
    }
    settings_ui::Rect error() const { const auto p = panel(); return {p.left + pad, p.bottom - 32 - 52 - 38, p.right - pad, p.bottom - 32 - 52 - 8}; }
    // The Mody tab: the folder's card (its path and "Zmień…"), then a card per mod: its name and state on
    // the left; its method (a list), "Usuń" for Launchme's own, and install at the row's end.
    settings_ui::Rect modsFolderRow() const { return card(0); }
    settings_ui::Rect modsFolderButton() const { return control(card(0), 0, button_width); }
    settings_ui::Rect modRow(int row) const { return card(1 + row); }
    settings_ui::Rect modAction(int row) const { return control(modRow(row), 0, button_width); }
    settings_ui::Rect modRemove(int row) const { return control(modRow(row), button_width + gap, remove_width); }
    settings_ui::Rect modMethod(int row) const { return control(modRow(row), remove_width + button_width + 2 * gap, method_width); }
    settings_ui::Rect modName(int row) const { const auto r = modRow(row); return {r.left + 24, r.top, modMethod(row).left - gap, r.bottom}; }
    int modAt(float x, float y) const {
        if (!mods) return -1;
        for (int row = 0; row < mod_count; ++row) if (modRow(row).contains(x, y)) return row;
        return -1;
    }
    settings_ui::Rect button(Item item) const {
        const auto p = panel(); const float bottom = p.bottom - 32, top = bottom - 52;
        if (item == Item::remove) return {p.left + pad, top, p.left + pad + button_width + 70, bottom}; // "Usuń z biblioteki" is longer.
        const float right = item == Item::save ? p.right - pad : p.right - pad - button_width - gap;
        return {right - button_width, top, right, bottom};
    }
    // The list a choice or a method opens: under its control (above when it would not fit), as wide as it.
    settings_ui::Rect dropdownAnchor() const { return dropdown.owner == Item::mod_method ? modMethod(dropdown.index) : optionValue(dropdown.index); }
    int dropdownFirst() const {
        const int count = static_cast<int>(dropdown.items.size());
        return std::clamp(dropdown.focus - drop_visible / 2, 0, std::max(0, count - drop_visible));
    }
    settings_ui::Rect dropdownBox() const {
        const auto anchor = dropdownAnchor();
        const int shown = std::min(drop_visible, static_cast<int>(dropdown.items.size()));
        const float h = static_cast<float>(shown) * drop_row + 12, w = std::max(anchor.right - anchor.left, 360.0f);
        float top = anchor.bottom + 6;
        if (top + h > height - 16) top = anchor.top - 6 - h;
        return {anchor.right - w, top, anchor.right, top + h};
    }
    settings_ui::Rect dropdownRow(int item) const {
        const auto box = dropdownBox(); const float top = box.top + 6 + static_cast<float>(item - dropdownFirst()) * drop_row;
        return {box.left + 6, top, box.right - 6, top + drop_row};
    }
    int dropdownAt(float x, float y) const {
        const int count = static_cast<int>(dropdown.items.size()), first = dropdownFirst();
        for (int item = first; item < std::min(count, first + drop_visible); ++item) if (dropdownRow(item).contains(x, y)) return item;
        return -1;
    }
    std::optional<Item> hit(float x, float y, bool editing) const {
        if (tabs)
            for (int which = 0; which < tab_count; ++which)
                if (tab(which).contains(x, y)) return which == 2 ? Item::tab_mods : which ? Item::tab_scripts : Item::tab_launch;
        if (mods) {
            if (modsFolderButton().contains(x, y)) return Item::mods_folder;
            for (int row = 0; row < mod_count; ++row) {
                if (modMethod(row).contains(x, y)) return Item::mod_method;
                if (modAction(row).contains(x, y)) return Item::mod_action;
                if (row < static_cast<int>(removable.size()) && removable[static_cast<size_t>(row)] && modRemove(row).contains(x, y)) return Item::mod_remove;
            }
        }
        if (pills) {
            if (pill(0).contains(x, y)) return Item::pick_file;
            if (pill(1).contains(x, y)) return Item::pick_folder;
        }
        for (int index = 0; index < field_count; ++index)
            if (shown(index) && browses(index) && browse(index).contains(x, y)) return browseItem(index);
        for (int index = 0; index < field_count; ++index)
            if (shown(index) && fieldRow(index).contains(x, y)) return fieldItem(index);
        if (const int index = optionAt(x, y); index >= 0)
            return index < static_cast<int>(more.size()) && more[static_cast<size_t>(index)] && optionMore(index).contains(x, y) ? Item::option_more : Item::option;
        if (poster && (identify().contains(x, y) || posterBox().contains(x, y))) return Item::identify;
        for (const auto item : {Item::remove, Item::cancel, Item::save})
            if ((item != Item::remove || editing) && button(item).contains(x, y)) return item;
        return std::nullopt;
    }
    // The focused item's place (the focus outline springs to it in Tryb TV); none while it is not drawn.
    std::optional<settings_ui::Rect> focusRect(const State& state) const {
        if (state.dropdown.open) return dropdownRow(state.dropdown.focus);
        switch (state.focus) {
        case Item::tab_launch: return tab(0);
        case Item::tab_scripts: return tab(1);
        case Item::tab_mods: return tab(2);
        case Item::pick_file: if (pills) return pill(0); return std::nullopt;
        case Item::pick_folder: if (pills) return pill(1); return std::nullopt;
        case Item::identify: if (poster) return identify(); return std::nullopt;
        case Item::option: if (state.option < option_count) return option(state.option); return std::nullopt;
        case Item::option_more: if (state.option < option_count) return optionMore(state.option); return std::nullopt;
        case Item::mods_folder: if (mods) return modsFolderButton(); return std::nullopt;
        case Item::mod_method: if (mods) return modMethod(state.mod_row); return std::nullopt;
        case Item::mod_action: if (mods) return modAction(state.mod_row); return std::nullopt;
        case Item::mod_remove: if (mods) return modRemove(state.mod_row); return std::nullopt;
        case Item::remove: case Item::cancel: case Item::save: return button(state.focus);
        case Item::file_browse: case Item::directory_browse: case Item::wine_page:
        case Item::test_before: case Item::test_started: case Item::test_exited: {
            const int index = row(state.focus);
            if (shown(index) && browses(index)) return browse(index);
            return std::nullopt;
        }
        default: {
            const int index = field(state.focus);
            if (index >= 0 && shown(index)) return input(index);
            return std::nullopt;
        }
        }
    }
};
} // namespace custom_game_ui
