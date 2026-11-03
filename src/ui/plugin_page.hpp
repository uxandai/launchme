#pragma once
#include "../plugins/json.hpp"
#include "../platform/text_encoding.hpp"
#include "grid_columns.hpp"
#include <algorithm>

namespace plugin_ui {
struct Row {
    std::wstring title, subtitle, uri;
    std::string game_key;
    bool heading = false;
    float top = 0;
    float extent = 0;
    std::string command;
    bool enabled = true;
    // The page's search field (always the first row): typed text, Enter sends `command` with it.
    // Its title is the placeholder.
    bool input = false;
    std::string download_id; // Artwork from this owner's download snapshot, not a library entry.
    bool actionable() const { return enabled && (input || !uri.empty() || !command.empty()); }
    bool hasSubtitle() const { return !subtitle.empty() && subtitle != title; }
    float height() const { return extent ? extent : input ? 72.0f : heading ? 48.0f : !download_id.empty() ? 128.0f : !game_key.empty() ? 92.0f : hasSubtitle() ? 82.0f : 58.0f; }
};
constexpr size_t query_limit = 100; // Characters typed into a page's search field.
struct Page {
    std::string plugin;
    std::wstring title, status;
    std::vector<Row> rows;
    int focus = -1;
    float scroll = 0, total = 0;
    std::wstring query;       // The search field's text (Row::input), edited by the host.
    std::string back_command; // Nonempty: Esc/B and Back send it to the plugin instead of leaving.
    uint32_t refresh = 0;     // Seconds: the opening command repeats with refresh:true while shown.
    bool dialog = false;     // Centered decision panel; same command and input routing.
    bool typing() const { return focus >= 0 && focus < static_cast<int>(rows.size()) && rows[focus].input; }
    void asDrawer() {
        total = 0;
        for (auto& row : rows) { row.extent = row.input ? 60.0f : row.heading ? 44.0f : (!row.game_key.empty() || !row.download_id.empty()) ? 80.0f : row.hasSubtitle() ? 78.0f : 54.0f; row.top = total; total += row.height(); }
    }
    int next(int direction) const {
        for (int i = focus + direction; i >= 0 && i < static_cast<int>(rows.size()); i += direction)
            if (rows[i].actionable()) return i;
        return direction < 0 ? -1 : focus;
    }
};
struct Layout {
    float width, height;
    bool drawer;
    bool dialog;
    float top, left;
    Layout(float width, float height, bool drawer = false, bool dialog = false) : width(width), height(height), drawer(drawer), dialog(dialog && !drawer), top(drawer ? 112.0f : 190.0f), left(drawer ? 24.0f : grid_layout::left(width, 0)) {
        if (this->dialog) { left = (width - std::min(1000.0f, width - 80)) * 0.5f + 32; top = panelTop() + 144; }
    }
    // The Stores' tab strip sits at the height of the back button (centre 82), not the library's 55.
    static constexpr float tabs_drop = 27;
    // The frame every page and the stores' catalog share: back circle, state line, search field, content from `top`.
    static float columnLeft(float width) { return grid_layout::left(width, 0); }
    static constexpr float status_top = 108;
    float fieldTop() const { return 134; }
    // The search field of a full page (Page::query): a pill across the column, above the list.
    struct Box { float left, top, right, bottom; bool contains(float x, float y) const { return x >= left && x < right && y >= top && y < bottom; } };
    Box field() const { return {left - 6, fieldTop(), width - left + 6, fieldTop() + 50}; }
    bool framed() const { return !drawer && !dialog; }
    static Box backBox(float width) { const float l = columnLeft(width); return {l, 58, l + 48, 106}; }
    float panelTop() const { return std::max(32.0f, (height - 640) * 0.5f); }
    float bottom() const { return dialog ? std::min(height - 40, panelTop() + 640) - 24 : height - (drawer ? 16 : 36); }
    float backLeft() const { return dialog ? left : drawer ? 24.0f : left; }
    float backTop() const { return dialog ? panelTop() + 24 : drawer ? 38.0f : 58.0f; }
    float maxScroll(const Page& page) const { return std::max(0.0f, page.total - (bottom() - top)); }
    float rowTop(const Page& page, size_t i) const { return top + page.rows[i].top - page.scroll; }
    bool back(float x, float y) const { const float size = drawer ? 36.0f : 48.0f; return x >= backLeft() && x < backLeft() + size && y >= backTop() && y < backTop() + size; }
    int hit(const Page& page, float x, float y) const {
        if (framed() && !page.rows.empty() && page.rows.front().input && field().contains(x, y)) return 0;
        if (x < left || x >= width - left || y < top || y >= bottom()) return -2;
        for (size_t i = 0; i < page.rows.size(); ++i) if (page.rows[i].actionable() && y >= rowTop(page, i) && y < rowTop(page, i) + page.rows[i].height() - 6) return static_cast<int>(i);
        return -2;
    }
};
inline bool validCommand(std::string_view command) {
    return command.size() <= 64 && std::all_of(command.begin(), command.end(), [](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
    });
}
inline Page parse(const json::Value& input, std::string plugin) {
    using namespace plugins::wire;
    Page page; page.plugin = std::move(plugin);
    if (const auto* dialog = input.get("dialog")) {
        if (dialog->type != json::Value::Type::boolean) throw std::runtime_error("Invalid plugin dialog");
        page.dialog = dialog->flag;
    }
    page.title = text_encoding::wide(text(input, "title")); page.status = text_encoding::wide(text(input, "status"));
    const auto& sections = field(input, "sections");
    if (page.title.empty() || page.title.size() > 256 || page.status.size() > 1024 || sections.type != json::Value::Type::array || sections.elements.size() > 8)
        throw std::runtime_error("Invalid plugin page");
    const auto add = [&](Row row) { row.top = page.total; page.total += row.height(); page.rows.push_back(std::move(row)); };
    if (const auto* back = input.get("back")) {
        page.back_command = back->stringOr();
        if (page.back_command.empty() || !validCommand(page.back_command)) throw std::runtime_error("Invalid plugin page back command");
    }
    if (const auto* refresh = input.get("refresh")) {
        if (refresh->type != json::Value::Type::number || refresh->numeric < 1 || refresh->numeric > 60 || std::floor(refresh->numeric) != refresh->numeric)
            throw std::runtime_error("Invalid plugin page refresh");
        page.refresh = static_cast<uint32_t>(refresh->numeric);
    }
    if (const auto* search = input.get("search")) {
        Row row; row.input = true;
        row.command = text(*search, "command"); row.title = text_encoding::wide(text(*search, "placeholder"));
        if (!page.dialog) row.extent = 0.001f; // A page's field sits in the frame above the list (Layout::field); a dialog keeps it in the flow.
        page.query = text_encoding::wide(text(*search, "query"));
        if (search->type != json::Value::Type::object || row.command.empty() || !validCommand(row.command) || row.title.size() > 256 ||
            page.query.size() > query_limit || page.query.find_first_of(std::wstring(L"\0\r\n", 3)) != page.query.npos)
            throw std::runtime_error("Invalid plugin page search");
        add(std::move(row));
    }
    for (const auto& section : sections.elements) {
        const auto title = text_encoding::wide(text(section, "title"));
        if (title.size() > 256) throw std::runtime_error("Invalid section title");
        add({title, {}, {}, {}, true});
        const auto& items = field(section, "items");
        if (items.type != json::Value::Type::array || items.elements.size() + page.rows.size() > 4000) throw std::runtime_error("Plugin page exceeds limit");
        if (items.elements.empty()) add({text_encoding::wide(text(section, "empty")), {}, {}, {}, true});
        for (const auto& item : items.elements) {
            Row row{text_encoding::wide(text(item, "title")), text_encoding::wide(text(item, "subtitle")), text_encoding::wide(text(item, "uri")), text(item, "gameKey")};
            row.command = text(item, "command");
            if (item.get("downloadId")) {
                row.download_id = text(item, "downloadId");
                if (row.download_id.empty() || !validCommand(row.download_id) || item.get("gameKey")) throw std::runtime_error("Invalid download artwork reference");
            }
            if ((item.get("command") && row.command.empty()) || (item.get("uri") && row.uri.empty()) ||
                (item.get("command") && item.get("uri"))) throw std::runtime_error("Invalid plugin page action");
            if (const auto* enabled = item.get("enabled")) {
                if (enabled->type != json::Value::Type::boolean) throw std::runtime_error("Invalid item enabled flag");
                row.enabled = enabled->flag;
            }
            const bool valid_uri = row.uri.empty() || ((row.uri.starts_with(L"https://") || row.uri.starts_with(L"http://")) &&
                row.uri.find_first_of(std::wstring(L"\0\r\n\" ", 5)) == row.uri.npos);
            if (row.title.empty() || row.title.size() > 512 || row.subtitle.size() > 1024 || row.uri.size() > 2048 ||
                !valid_uri || !validCommand(row.command) || (!row.uri.empty() && !row.command.empty()))
                throw std::runtime_error("Invalid plugin page item");
            add(std::move(row));
        }
    }
    page.focus = page.next(1);
    // The search field waits above the list (Up or X reaches it): a page starts on its first
    // row, or in the field when it has nothing else to pick.
    if (page.typing()) { const int first = page.next(1); if (first != page.focus) page.focus = first; }
    return page;
}
}
