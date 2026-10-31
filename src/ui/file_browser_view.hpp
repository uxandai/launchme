#pragma once
#include "settings_view.hpp"

#include <algorithm>
#include <cstdint>
#include <cwchar>
#include <cwctype>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

// The launcher's own file and folder picker (src/app/file_browser.cpp), drawn like Settings:
// places on the left rail, the folder's entries as rows, action buttons along the bottom.
// Every control is reachable with the D-pad; geometry is shared by the renderer, pointer
// hit testing and the tests.
namespace file_browser {
enum class Mode { file, folder };

struct Request {
    Mode mode = Mode::file;
    std::wstring title;
    // Lower case, with the dot (".exe"); empty = every file. A folder picker lists no files.
    std::vector<std::wstring> extensions;
    std::wstring filter_label;  // The type button's label while `extensions` apply.
    bool all_files_option = false; // The type button switches to every file.
    bool images = false;           // Preview of the highlighted PNG/JPEG on the right.
    bool folder_option = false;    // A file picker that also takes the open folder ("Wybierz ten folder": Dodaj grę).
    std::filesystem::path initial; // Opened first when it exists; else the last folder.
    // "Okno systemowe…": the Windows dialog for the same request; empty = no such button.
    std::function<std::filesystem::path()> native;
};

enum class Kind { up, drive, folder, file };
struct Entry {
    Kind kind = Kind::file;
    std::wstring name, detail; // detail: a drive's label / type; files: their size.
    std::filesystem::path path;
    uint64_t size = 0, modified = 0; // FILETIME ticks (files and folders).
    bool hidden = false;
};

enum class Icon { computer, recent, desktop, downloads, documents, pictures, music, videos, steam, folder };
struct Place {
    std::wstring label;
    std::filesystem::path path; // Empty: "Ten komputer" (the drives).
    Icon icon = Icon::folder;
};

enum class Button { choose_folder, file_type, hidden, system_dialog };
enum class Focus { places, list, buttons, back };

struct State {
    bool open = false;
    Request request;
    std::filesystem::path folder; // Empty: "Ten komputer".
    std::vector<Entry> entries;
    std::vector<Place> places;
    std::wstring error; // The folder could not be listed.
    Focus focus = Focus::list;
    int selected = 0, place = 0, button = 0;
    float scroll = 0;
    bool show_hidden = false, all_files = false;
    int hover = -1; // Row under the mouse.
    std::wstring typed; // Type-ahead prefix and when its last character came.
    uint64_t typed_at = 0;

    bool root() const { return folder.empty(); }
    bool folderMode() const { return request.mode == Mode::folder; }
    std::vector<Button> buttons() const {
        std::vector<Button> result;
        if (folderMode() || request.folder_option) result.push_back(Button::choose_folder);
        if (!folderMode() && request.all_files_option && !request.extensions.empty()) result.push_back(Button::file_type);
        result.push_back(Button::hidden);
        if (request.native) result.push_back(Button::system_dialog);
        return result;
    }
    const Entry* current() const { return selected >= 0 && selected < static_cast<int>(entries.size()) ? &entries[selected] : nullptr; }
    // The place whose folder is open (or holds it); -1 for none.
    int activePlace() const {
        int best = -1; size_t best_length = 0;
        for (int i = 0; i < static_cast<int>(places.size()); ++i) {
            const auto& path = places[i].path;
            if (path.empty()) { if (root() && best < 0) best = i; continue; }
            const auto prefix = path.wstring(), current_folder = folder.wstring();
            if (current_folder.size() < prefix.size() || prefix.size() <= best_length) continue;
#ifdef _WIN32 // Paths ignore case on Windows.
            if (_wcsnicmp(current_folder.c_str(), prefix.c_str(), prefix.size()) != 0) continue;
#else
            if (current_folder.compare(0, prefix.size(), prefix) != 0) continue;
#endif
            constexpr wchar_t separator = std::filesystem::path::preferred_separator;
            if (current_folder.size() > prefix.size() && current_folder[prefix.size()] != separator && prefix.back() != separator) continue;
            best = i; best_length = prefix.size();
        }
        return best;
    }
    // A matching file for this request (folders always pass).
    bool accepts(const std::filesystem::path& file) const {
        if (folderMode()) return false;
        if (request.extensions.empty() || all_files) return true;
        auto extension = file.extension().wstring();
        std::transform(extension.begin(), extension.end(), extension.begin(), ::towlower);
        return std::find(request.extensions.begin(), request.extensions.end(), extension) != request.extensions.end();
    }
};

struct Layout {
    float width, height;
    bool preview = false; // State::request.images: the list leaves room for the preview pane.
    static constexpr float row_height = 62, row_step = 68, button_width = 236;
    settings_ui::Layout base() const { return {width, height}; }
    float listTop() const { return settings_ui::Layout::described_top; }
    float listBottom() const { return height - 138; }
    float listLeft() const { return base().contentLeft(); }
    float listRight() const { return preview ? base().contentRight() - previewWidth() - 32 : base().contentRight(); }
    float previewWidth() const { return std::clamp((base().contentRight() - base().contentLeft()) * 0.36f, 260.0f, 520.0f); }
    settings_ui::Rect previewBox() const {
        const float right = base().contentRight(), left = right - previewWidth();
        return {left, listTop(), right, std::min(listBottom(), listTop() + previewWidth() * 1.25f)};
    }
    settings_ui::Rect row(int index, float scroll) const {
        const float y = listTop() + index * row_step - scroll;
        return {listLeft(), y, listRight(), y + row_height};
    }
    int visibleRows() const { return std::max(1, static_cast<int>((listBottom() - listTop() + (row_step - row_height)) / row_step)); }
    float maxScroll(int count) const { return std::max(0.0f, count * row_step - (row_step - row_height) - (listBottom() - listTop())); }
    float ensureVisible(int index, int count, float scroll) const {
        const auto r = row(index, scroll);
        if (r.top < listTop()) scroll -= listTop() - r.top;
        else if (r.bottom > listBottom()) scroll += r.bottom - listBottom();
        return std::clamp(scroll, 0.0f, maxScroll(count));
    }
    int hitRow(float x, float y, int count, float scroll) const {
        if (y < listTop() || y >= listBottom() || x < listLeft() || x >= listRight()) return -1;
        const int index = static_cast<int>((y - listTop() + scroll) / row_step);
        return index >= 0 && index < count && row(index, scroll).contains(x, y) ? index : -1;
    }
    // Places use the Settings rail rows; as many as fit above Back.
    settings_ui::Rect place(int index) const { return base().rail(index); }
    int placeCapacity() const {
        const auto back = base().backBar();
        return std::max(1, static_cast<int>((back.top - 16 - settings_ui::Layout::rail_top + 10) / 74));
    }
    int hitPlace(float x, float y, int count) const {
        for (int i = 0; i < std::min(count, placeCapacity()); ++i) if (place(i).contains(x, y)) return i;
        return -1;
    }
    settings_ui::Rect back() const { return base().backBar(); }
    // Bottom bar, right-aligned: the last button sits at the content's right edge.
    settings_ui::Rect button(int index, int count) const {
        const auto bar = base().backBar();
        const float right = base().contentRight() - (count - 1 - index) * (button_width + 16);
        return {right - button_width, bar.top, right, bar.bottom};
    }
    int hitButton(float x, float y, int count) const {
        for (int i = 0; i < count; ++i) if (button(i, count).contains(x, y)) return i;
        return -1;
    }
};
} // namespace file_browser
