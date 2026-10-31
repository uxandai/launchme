// Dodaj grę picks what it is adding by itself (the "+" tile, Moje gry, a file or folder dropped on the
// window): a program or shortcut is a PC game; a disc image or ROM an emulated game with its console
// guessed (its folder under roms\, its extension, what the image holds) and changeable on the
// Platforma row; a folder is a PS3 or Wii U game kept as a folder, a folder holding one game's ROM, or
// a PC game's folder whose program is found the way the DRM-free scan finds it.
#include "launcher.hpp"
#include "../library/drm_free.hpp"
#include "../library/emulation_platforms.hpp"
#include "../library/emulation_scan.hpp"

#include <algorithm>
#include <cwctype>

namespace {
bool programFile(const std::filesystem::path& file) {
    auto extension = file.extension().wstring();
    for (auto& ch : extension) ch = static_cast<wchar_t>(std::towlower(ch));
    return extension == L".exe" || extension == L".lnk" || extension == L".bat" || extension == L".cmd" || extension == L".url";
}
// The consoles the Platforma row walks: every emulation platform but the shortcut-only ones.
std::vector<std::wstring> platformChoices() {
    std::vector<std::wstring> result;
    for (const auto& platform : emulation_platforms)
        if (platform.name != L"PC" && platform.name != L"Wii / GameCube") result.emplace_back(platform.name);
    return result;
}
}

bool Launcher::addableFile(const std::filesystem::path& path) const {
    std::error_code error;
    return programFile(path) || emulation_scan::romFile(path) || std::filesystem::is_directory(path, error);
}

void Launcher::customEditorApplyFile(const std::filesystem::path& picked) {
    using namespace custom_game_ui;
    auto& ui = custom_editor_;
    if (!ui.open || picked.empty()) return;
    ui.error.clear();
    const std::filesystem::path previous = custom_games::cleanPath(ui.fields[file_field]);
    const std::filesystem::path directory = custom_games::cleanPath(ui.fields[directory_field]);
    const bool unnamed = custom_games::trim(ui.fields[name_field]).empty();
    const auto roms = emulation_setup_ ? emulation_setup_->paths.roms : std::filesystem::path{};
    const auto emulated = [&](const std::filesystem::path& rom, const std::wstring& platform, const std::wstring& title) {
        if (!ui.emulated()) ui.fields[arguments_field].clear(); // A PC game's options mean nothing to an emulator.
        ui.fields[file_field] = rom.wstring();
        ui.fields[platform_field] = platform.empty() || platform == L"PC" ? std::wstring(L"Retro") : platform;
        ui.fields[directory_field].clear();
        if (unnamed) ui.fields[name_field] = title.substr(0, custom_games::max_name);
    };
    std::error_code error;
    if (std::filesystem::is_directory(picked, error)) {
        if (const auto game = emulation_scan::folderGame(picked)) { emulated(game->rom, game->platform, game->title); }
        else {
            const auto found = drm_free::detect(picked);
            if (found.executable.empty()) {
                ui.error = found.container ? tr(L"W tym folderze jest wiele gier: dodaj go w DRM-free (Biblioteka).")
                                           : tr(L"W tym folderze nie ma programu gry ani pliku ROM.");
                InvalidateRect(hwnd_, nullptr, FALSE);
                return;
            }
            ui.fields[platform_field].clear();
            ui.fields[file_field] = found.executable.wstring();
            ui.fields[arguments_field] = found.arguments;
            ui.fields[directory_field] = (found.working_directory.empty() ? picked : found.working_directory).wstring();
            if (unnamed) ui.fields[name_field] = (found.title.empty() ? drm_free::cleanTitle(picked.filename().wstring()).text : found.title).substr(0, custom_games::max_name);
        }
    } else if (!programFile(picked) && emulation_scan::romFile(picked)) {
        // A PS3 game's EBOOT.BIN or a Wii U .rpx picked inside the game's folder: the folder is the game.
        if (const auto up = picked.parent_path().parent_path(); !up.empty())
            if (const auto game = emulation_scan::folderGame(up.parent_path()); game && game->rom == picked) { emulated(game->rom, game->platform, game->title); }
        if (!ui.emulated() || custom_games::cleanPath(ui.fields[file_field]) != picked)
            emulated(picked, emulation_scan::guessPlatform(picked, roms), emulation_scan::displayTitle(picked.stem().wstring()));
    } else {
        ui.fields[platform_field].clear();
        ui.fields[file_field] = picked.wstring();
        if (unnamed) ui.fields[name_field] = picked.stem().wstring().substr(0, custom_games::max_name);
        // A working directory the user chose stays; the default follows the file.
        if (directory.empty() || directory == previous.parent_path()) ui.fields[directory_field] = picked.parent_path().wstring();
    }
    // The focus may have stood on a row this change hides (the working directory of an emulated game).
    if (const int row = custom_game_ui::row(ui.focus); row < field_count && !ui.shows(row)) ui.focus = Item::file;
    if (!custom_games::trim(ui.fields[name_field]).empty()) customEditorFocus(Item::save);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::customEditorCyclePlatform(int step) {
    using namespace custom_game_ui;
    auto& ui = custom_editor_;
    if (!ui.open || !ui.emulated()) return;
    const auto choices = platformChoices();
    auto at = std::find(choices.begin(), choices.end(), ui.fields[platform_field]);
    const int count = static_cast<int>(choices.size());
    int index = at == choices.end() ? 0 : static_cast<int>(at - choices.begin());
    index = ((index + step) % count + count) % count;
    ui.fields[platform_field] = choices[static_cast<size_t>(index)];
    ui.error.clear();
    audio_.navigate();
    InvalidateRect(hwnd_, nullptr, FALSE);
}
