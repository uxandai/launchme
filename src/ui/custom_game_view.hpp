#pragma once
#include "settings_view.hpp"
#include "../i18n/i18n.hpp"

#include <algorithm>
#include <array>
#include <optional>
#include <string>

// Add/edit panel of a user-added game (Settings → Library). One focus moves through
// the text fields, their "Wybierz…" buttons and the bottom buttons; geometry is shared
// by the renderer, pointer hit testing and the tests.
// A user-added game's panel shows its poster beside the fields (matched by the typed name) with
// "Zidentyfikuj grę" under it.
// The same panel edits how another game starts ("Opcje uruchamiania…" in the game's menu): a
// DRM-free game's file, options and working directory, a Steam game's launch options only.
// Linux, a Windows game (State::compat): also its WINEDLLOVERRIDES and its Wine/Proton launch options
// as Steam writes them (VAR=value wrapper %command% arguments), with "Proton / Wine…" to its page.
// Every user-added game (a new one too: its scripts are saved with it) and every game's launch options
// (State::tabs()) have two tabs: "Uruchamianie" (the above) and "Skrypty", its
// commands before the launch, after the start and after the exit, each with "Testuj"
// (src/platform/launch_scripts.hpp).
namespace custom_game_ui {
// pick_file / pick_folder: a new game's two pills above the fields ("Wskaż plik", "Wskaż folder"); they
// take the file row's "Wybierz…" place.
// log_toggle, hdr_toggle: the switches' row under the fields (State::flags): "Zapisuj logi", "HDR na czas gry" (Windows).
// tab_launch, tab_scripts: the tabs above the fields; before…exited: the scripts' fields, test_*: their "Testuj".
enum class Item { name, file, file_browse, arguments, directory, directory_browse, dll, wine, wine_page, platform, identify, remove, cancel, save,
                  pick_file, pick_folder, log_toggle, hdr_toggle, tab_launch, tab_scripts, before, test_before, started, test_started, exited, test_exited };
// platform_field: an emulated game's console (Left/Right or Enter pick another; never typed). With it the
// file is the ROM, the options an emulator command of its own and the working directory has no use.
// before_field…exited_field: the "Skrypty" tab's (launch_scripts::Phase order).
enum Field { name_field, file_field, arguments_field, directory_field, dll_field, wine_field, platform_field,
             before_field, started_field, exited_field, field_count };
inline bool scriptField(int index) { return index >= before_field && index <= exited_field; }
enum class Tab { launch, scripts };
// other: a game of another store, whose client takes no options: the switches alone.
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
    // Opcje uruchamiania's switches of the game (LibraryState::launch_flags by flags_key); hdr_option:
    // Windows (HDR switching), and not for a new game, which has no key yet.
    bool flags = false, log = false, hdr = false, hdr_option = false;
    std::string flags_key;
    // The tabs: a game with a key (an edited one or another's launch options), and a new user-added game
    // (its scripts saved under the key it gets).
    Tab tab = Tab::launch;
    std::wstring note; // A script test's outcome under the fields (ui.error wins).
    bool note_failed = false;
    bool testing = false; // A test runs: "Testuj" waits for it.
    bool tabs() const { return flags || (target == Target::custom && !editing()); }
    bool scripts() const { return tabs() && tab == Tab::scripts; }
    int toggles() const { return flags && !scripts() ? (hdr_option ? 2 : 1) : 0; }
    bool editing() const { return !id.empty(); }
    bool launchOptions() const { return target != Target::custom; }
    // The desktop window: a new game has the file row's "Wybierz" like an edited one, and "Dodaj folder jako bibliotekę" at the bottom.
    bool desktop = false;
    // Tryb TV: "Wskaż plik" / "Wskaż folder" (the launch tab's).
    bool pills() const { return target == Target::custom && !editing() && !desktop && tab == Tab::launch; }
    bool folderButton() const { return target == Target::custom && !editing() && desktop && tab == Tab::launch; }
    // The file row's "Wybierz…" (a game being edited; a new one has the pills).
    bool browses(int index) const { return index != file_field || !pills(); }
    bool posterShown() const { return target == Target::custom; }
    bool posterVisible() const { return posterShown() && !scripts(); } // The scripts' tab uses the whole width.
    const std::string& gameId() const { return editing() ? id : pending_id; }
    bool emulated() const { return target == Target::custom && !fields[platform_field].empty(); }
    bool shows(int index) const {
        if (scriptField(index)) return scripts();
        if (scripts()) return false;
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
inline constexpr int buttons_row = field_count, identify_row = field_count + 1, tabs_row = field_count + 2;
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
    case Item::tab_launch: case Item::tab_scripts: return tabs_row;
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

struct Layout {
    float width, height;
    unsigned hidden = 0; // State::hidden(): left-out fields; the others close up.
    bool poster = false; // State::posterShown(): the poster column on the right.
    bool pills = false;  // State::pills(): "Wskaż plik" / "Wskaż folder" above the fields.
    int toggles = 0;     // State::toggles(): the switches' row under the fields.
    bool tabs = false;   // State::tabs(): "Uruchamianie" / "Skrypty" above the fields.
    float body = 0;      // With tabs: the taller tab's fields and switches, so switching keeps the panel's size.
    static constexpr float margin = 36, button_width = 176, gap = 16, poster_width = 200, poster_gap = 28, pills_height = 76, toggles_height = 68,
                           tabs_height = 72, row_height = 104;
    static Layout of(const State& state, float width, float height) {
        Layout layout{width, height, state.hidden(), state.posterVisible(), state.pills(), state.toggles(), state.tabs()};
        if (layout.tabs) {
            auto other = state;
            other.tab = state.scripts() ? Tab::launch : Tab::scripts;
            const auto rows = [](const State& s) {
                int count = 0;
                for (int index = 0; index < field_count; ++index) count += s.shows(index);
                return static_cast<float>(count) * row_height + (s.toggles() ? toggles_height : 0.0f) + (s.pills() ? pills_height : 0.0f);
            };
            layout.body = std::max(rows(state), rows(other));
        }
        return layout;
    }
    bool shown(int index) const { return !(hidden & (1u << index)); }
    bool browses(int index) const { return browseRow(index) && (index != file_field || !pills); }
    int slot(int index) const { // A field's row among the shown ones.
        int result = 0;
        for (int i = 0; i < index; ++i) result += shown(i);
        return result;
    }
    float tabsSpace() const { return tabs ? tabs_height : 0.0f; }
    float pillsSpace() const { return (pills ? pills_height : 0.0f) + tabsSpace(); } // The tabs, then the pills.
    settings_ui::Rect panel() const {
        const float w = std::min(poster || tabs ? 1100.0f : 920.0f, width - 96),
                    own = slot(field_count) * row_height + (toggles ? toggles_height : 0.0f) + (pills ? pills_height : 0.0f),
                    half = 118 + std::max(own, body) * 0.5f + tabsSpace() * 0.5f;
        return {(width - w) * 0.5f, height * 0.5f - half, (width + w) * 0.5f, height * 0.5f + half};
    }
    // The tabs at the top left of the fields: 0 "Uruchamianie", 1 "Skrypty".
    settings_ui::Rect tab(int which) const {
        const auto p = panel(); const float top = p.top + 96, w = 220;
        const float left = p.left + margin + static_cast<float>(which) * (w + 10);
        return {left, top, left + w, top + 52};
    }
    float rowTop(int index) const { return panel().top + 96 + pillsSpace() + slot(index) * 104.0f; }
    float fieldsRight() const { return panel().right - margin - (poster ? poster_width + poster_gap : 0.0f); }
    // The two pills side by side over the fields' width: 0 "Wskaż plik", 1 "Wskaż folder".
    settings_ui::Rect pill(int which) const {
        const auto p = panel(); const float top = p.top + 96 + tabsSpace(), w = (fieldsRight() - p.left - margin - gap) * 0.5f;
        const float left = p.left + margin + static_cast<float>(which) * (w + gap);
        return {left, top, left + w, top + 56};
    }
    // The switches side by side over the fields' width, under the last field: 0 logs, 1 HDR.
    settings_ui::Rect toggle(int which) const {
        const auto p = panel(); const float top = p.top + 96 + pillsSpace() + slot(field_count) * 104.0f + 4;
        const float w = (fieldsRight() - p.left - margin - gap) * 0.5f, left = p.left + margin + static_cast<float>(which) * (w + gap);
        return {left, top, left + w, top + 52};
    }
    settings_ui::Rect label(int index) const {
        const auto p = panel(); const float y = rowTop(index);
        return {p.left + margin, y, fieldsRight(), y + 28};
    }
    settings_ui::Rect input(int index) const {
        const auto p = panel(); const float y = rowTop(index) + 32;
        return {p.left + margin, y, fieldsRight() - (browses(index) ? button_width + gap : 0.0f), y + 56};
    }
    settings_ui::Rect browse(int index) const {
        const float y = rowTop(index) + 32;
        return {fieldsRight() - button_width, y + 6, fieldsRight(), y + 50};
    }
    // The poster (2:3) at the top of its column, "Zidentyfikuj grę" under it.
    settings_ui::Rect posterBox() const {
        const auto p = panel(); const float top = panel().top + 96 + 4;
        return {p.right - margin - poster_width, top, p.right - margin, top + poster_width * 1.5f};
    }
    settings_ui::Rect identify() const {
        const auto box = posterBox();
        return {box.left, box.bottom + 16, box.right, box.bottom + 16 + settings_ui::Layout::button_height};
    }
    settings_ui::Rect error() const { const auto p = panel(); return {p.left + margin, p.bottom - 126, p.right - margin, p.bottom - 94}; }
    settings_ui::Rect button(Item item) const {
        const auto p = panel(); const float bottom = p.bottom - 30, top = bottom - settings_ui::Layout::button_height;
        // "Usuń z biblioteki" is longer than the other captions.
        if (item == Item::remove) return {p.left + margin, top, p.left + margin + button_width + 60, bottom};
        const float right = item == Item::save ? p.right - margin : p.right - margin - button_width - gap;
        return {right - button_width, top, right, bottom};
    }
    std::optional<Item> hit(float x, float y, bool editing) const {
        if (tabs) {
            if (tab(0).contains(x, y)) return Item::tab_launch;
            if (tab(1).contains(x, y)) return Item::tab_scripts;
        }
        if (pills) {
            if (pill(0).contains(x, y)) return Item::pick_file;
            if (pill(1).contains(x, y)) return Item::pick_folder;
        }
        for (int index = 0; index < field_count; ++index)
            if (shown(index) && input(index).contains(x, y)) return fieldItem(index);
        for (int index = 0; index < field_count; ++index)
            if (shown(index) && browses(index) && browse(index).contains(x, y)) return browseItem(index);
        if (poster && (identify().contains(x, y) || posterBox().contains(x, y))) return Item::identify;
        for (int which = 0; which < toggles; ++which) if (toggle(which).contains(x, y)) return which ? Item::hdr_toggle : Item::log_toggle;
        for (const auto item : {Item::remove, Item::cancel, Item::save})
            if ((item != Item::remove || editing) && button(item).contains(x, y)) return item;
        return std::nullopt;
    }
};
} // namespace custom_game_ui
