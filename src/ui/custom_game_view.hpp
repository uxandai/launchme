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
namespace custom_game_ui {
enum class Item { name, file, file_browse, arguments, directory, directory_browse, dll, wine, wine_page, platform, identify, remove, cancel, save };
// platform_field: an emulated game's console (Left/Right or Enter pick another; never typed). With it the
// file is the ROM, the options an emulator command of its own and the working directory has no use.
enum Field { name_field, file_field, arguments_field, directory_field, dll_field, wine_field, platform_field, field_count };
enum class Target { custom, drm_free, steam };
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
    bool editing() const { return !id.empty(); }
    bool launchOptions() const { return target != Target::custom; }
    bool posterShown() const { return target == Target::custom; }
    const std::string& gameId() const { return editing() ? id : pending_id; }
    bool emulated() const { return target == Target::custom && !fields[platform_field].empty(); }
    bool shows(int index) const {
        if (index == dll_field || index == wine_field) return compat;
        if (index == platform_field) return emulated();
        if (index == directory_field && emulated()) return false;
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
    if (index == arguments_field) return tr(L"Puste: emulator z EmuDecka. Np. \"C:\\Emu\\pcsx2.exe\" %ROM%");
    if (index == platform_field) return tr(L"Strzałki lub Enter: inna platforma");
    return nullptr;
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
    default: return -1;
    }
}
// Rows 0-6 hold the fields (1 and 3 with a browse button, 5 with "Proton / Wine…"), row 6 the
// buttons; "Zidentyfikuj grę" stands apart in the poster's column (7).
inline constexpr int buttons_row = field_count, identify_row = field_count + 1;
inline int row(Item item) {
    switch (item) {
    case Item::name: return 0;
    case Item::file: case Item::file_browse: return 1;
    case Item::arguments: return 2;
    case Item::directory: case Item::directory_browse: return 3;
    case Item::dll: return 4;
    case Item::wine: case Item::wine_page: return 5;
    case Item::platform: return 6;
    case Item::identify: return identify_row;
    default: return buttons_row;
    }
}
inline bool browseRow(int index) { return index == file_field || index == directory_field || index == wine_field; }
// The button beside a row's field.
inline Item browseItem(int index) { return index == file_field ? Item::file_browse : index == wine_field ? Item::wine_page : Item::directory_browse; }

struct Layout {
    float width, height;
    unsigned hidden = 0; // State::hidden(): left-out fields; the others close up.
    bool poster = false; // State::posterShown(): the poster column on the right.
    static constexpr float margin = 36, button_width = 176, gap = 16, poster_width = 200, poster_gap = 28;
    static Layout of(const State& state, float width, float height) { return {width, height, state.hidden(), state.posterShown()}; }
    bool shown(int index) const { return !(hidden & (1u << index)); }
    int slot(int index) const { // A field's row among the shown ones.
        int result = 0;
        for (int i = 0; i < index; ++i) result += shown(i);
        return result;
    }
    settings_ui::Rect panel() const {
        const float w = std::min(poster ? 1100.0f : 920.0f, width - 96), half = 118 + slot(field_count) * 52.0f;
        return {(width - w) * 0.5f, height * 0.5f - half, (width + w) * 0.5f, height * 0.5f + half};
    }
    float rowTop(int index) const { return panel().top + 96 + slot(index) * 104.0f; }
    float fieldsRight() const { return panel().right - margin - (poster ? poster_width + poster_gap : 0.0f); }
    settings_ui::Rect label(int index) const {
        const auto p = panel(); const float y = rowTop(index);
        return {p.left + margin, y, fieldsRight(), y + 28};
    }
    settings_ui::Rect input(int index) const {
        const auto p = panel(); const float y = rowTop(index) + 32;
        return {p.left + margin, y, fieldsRight() - (browseRow(index) ? button_width + gap : 0.0f), y + 56};
    }
    settings_ui::Rect browse(int index) const {
        const float y = rowTop(index) + 32;
        return {fieldsRight() - button_width, y + 6, fieldsRight(), y + 50};
    }
    // The poster (2:3) at the top of its column, "Zidentyfikuj grę" under it.
    settings_ui::Rect posterBox() const {
        const auto p = panel(); const float top = rowTop(0) + 4;
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
        for (const auto item : {Item::name, Item::file, Item::arguments, Item::directory, Item::dll, Item::wine, Item::platform})
            if (shown(row(item)) && input(row(item)).contains(x, y)) return item;
        for (const int index : {static_cast<int>(file_field), static_cast<int>(directory_field), static_cast<int>(wine_field)})
            if (shown(index) && browse(index).contains(x, y)) return browseItem(index);
        if (poster && (identify().contains(x, y) || posterBox().contains(x, y))) return Item::identify;
        for (const auto item : {Item::remove, Item::cancel, Item::save})
            if ((item != Item::remove || editing) && button(item).contains(x, y)) return item;
        return std::nullopt;
    }
};
} // namespace custom_game_ui
