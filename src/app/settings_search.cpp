// Search in Settings: one field over every setting of every category and of the pages behind their tiles.
// The index is read from the rows themselves (buildSettingsRows) when the search opens, so it never drifts
// from what the lists show; the page lists the settings that have every typed word, a click or A jumps to one.
#include "launcher.hpp"

#include <algorithm>
#include <functional>

#include "../platform/unicode_text.hpp"

using namespace settings_ui;

namespace {
// Rows that are content, not settings: games, folders, collections and the like would drown the real ones.
bool content(Id id) {
    switch (id) {
    case Id::collection: case Id::home_section: case Id::library_tab: case Id::home_row: case Id::home_row_games: case Id::home_row_rename:
    case Id::home_row_move: case Id::home_row_steam: case Id::home_row_delete: case Id::home_row_reset: case Id::custom_game: case Id::drm_game: case Id::drm_root: case Id::excluded_game:
    case Id::studio_hidden_entry: case Id::prefix_entry: case Id::launch_env: case Id::launch_dll: case Id::launch_option:
    case Id::launch_fold: case Id::artwork_source: case Id::settings_search: case Id::settings_hit:
        return true;
    default: return false;
    }
}
} // namespace

void Launcher::buildSettingsSearchIndex() {
    const auto saved = settings_state_;
    settings_hits_.clear();
    const std::function<void(int, int, std::vector<Page>, std::wstring)> walk = [&](int category, int depth, std::vector<Page> path, std::wstring where) {
        const auto rows = settings_rows_; // The list is rebuilt when a page opens.
        std::wstring section;
        for (size_t i = 0; i < rows.size(); ++i) {
            const Row& row = rows[i];
            if (!row.section.empty()) section = row.section;
            if (row.title.empty() || content(row.id)) continue;
            const bool tile = row.control == Control::page && row.id == Id::page && row.key == std::to_string(static_cast<int>(row.page));
            if (row.control != Control::fold) {
                SettingsHit hit;
                hit.category = category; hit.path = path; hit.id = row.id; hit.key = row.key; hit.plugin_setting = row.plugin_setting;
                hit.title = row.title; hit.description = row.description;
                hit.where = section.empty() ? where : where + L" › " + section;
                hit.value = row.control == Control::toggle ? row.checked ? std::wstring(tr(L"Włączone")) : std::wstring(tr(L"Wyłączone")) : row.value;
                settings_hits_.push_back(std::move(hit));
            }
            if (tile && depth < 2) {
                settings_state_.selected[category] = static_cast<int>(i);
                settings_state_.open(row.page, {}, 0, RowRef::of(row));
                buildSettingsRows();
                auto next = path;
                next.push_back(row.page);
                walk(category, depth + 1, std::move(next), where + L" › " + row.title);
                settings_state_.close();
                buildSettingsRows();
            }
        }
    };
    for (int category = 0; category < static_cast<int>(categories.size()); ++category) {
        settings_state_.pages.clear();
        settings_state_.category = category;
        settings_state_.options = settings_state_.editing = false;
        buildSettingsRows();
        walk(category, 0, {}, tr(categories[static_cast<size_t>(category)].title));
    }
    settings_state_ = saved;
    settings_rows_page_.clear();
    buildSettingsRows(); // The list the search was opened from, for the page's opener.
}

void Launcher::openSettingsSearch() {
    if (!settingsOpen() || settings_state_.at(Page::settings_search) || color_editor_.open || editingSecret() || settingsPicking()) return;
    settings_search_query_.clear();
    buildSettingsSearchIndex();
    openSettingsPage(Page::settings_search);
    settingsActivate(); // Straight into the field: type.
}

// The field and the settings that have every typed word (titles first).
void Launcher::settingsSearchRows(const std::function<void(Row)>& add) {
    add({Id::settings_search, Control::text, tr(L"Szukaj w ustawieniach"), tr(L"Nazwa albo opis ustawienia, np. tapeta, dźwięk, Proton."),
         settings_search_query_.empty() ? std::wstring(tr(L"Wpisz…")) : settings_search_query_});
    std::vector<std::wstring> words;
    {
        const auto lowered = unicode_text::lower(settings_search_query_);
        size_t start = 0;
        while (start < lowered.size()) {
            const auto end = lowered.find_first_of(L" \t", start);
            if (end == std::wstring::npos) { words.push_back(lowered.substr(start)); break; }
            if (end > start) words.push_back(lowered.substr(start, end - start));
            start = end + 1;
        }
    }
    if (words.empty()) return;
    struct Scored { size_t index; int rank; };
    std::vector<Scored> found;
    for (size_t i = 0; i < settings_hits_.size(); ++i) {
        const auto& hit = settings_hits_[i];
        const auto title = unicode_text::lower(hit.title);
        const auto rest = title + L' ' + unicode_text::lower(hit.description) + L' ' + unicode_text::lower(hit.where);
        const auto all = [&](const std::wstring& text) {
            return std::all_of(words.begin(), words.end(), [&](const std::wstring& word) { return text.find(word) != std::wstring::npos; });
        };
        if (!all(rest)) continue;
        found.push_back({i, all(title) ? 0 : 1});
    }
    std::stable_sort(found.begin(), found.end(), [](const Scored& a, const Scored& b) { return a.rank < b.rank; });
    if (found.empty()) {
        Row none{Id::settings_hit, Control::action, tr(L"Nic nie pasuje"), tr(L"Spróbuj innej nazwy."), {}};
        none.key = "-1"; none.locked = true;
        add(std::move(none));
        return;
    }
    found.resize(std::min<size_t>(found.size(), 80));
    bool first = true;
    for (const auto& item : found) {
        const auto& hit = settings_hits_[item.index];
        Row row{Id::settings_hit, Control::action, hit.title, hit.where, hit.value};
        row.key = std::to_string(item.index);
        if (first) { row.section = tr(L"Wyniki"); first = false; }
        add(std::move(row));
    }
}

// Every typed letter: the list follows at once (the field stays in editing).
void Launcher::settingsSearchEdited() {
    if (!editingSecret() || settings_rows_[settings_state_.row()].id != Id::settings_search) return;
    settings_search_query_ = settings_secret_input_;
    buildSettingsRows();
}

// To the setting: its category, the tiles on the way, the row focused (and scrolled into view).
void Launcher::settingsSearchJump(size_t index) {
    if (index >= settings_hits_.size()) return;
    const auto hit = settings_hits_[index];
    settings_search_query_.clear();
    clearSecretInput();
    settings_state_.selectCategory(hit.category);
    settings_motion_.valid = false;
    buildSettingsRows();
    const auto select = [&](const std::function<bool(const Row&)>& wanted) {
        const auto found = std::find_if(settings_rows_.begin(), settings_rows_.end(), wanted);
        if (found == settings_rows_.end()) return false;
        settings_state_.selected[settings_state_.category] = static_cast<int>(found - settings_rows_.begin());
        settings_state_.options = true;
        return true;
    };
    for (const auto page : hit.path) {
        if (!select([&](const Row& row) { return row.control == Control::page && row.id == Id::page && row.page == page; })) break;
        openSettingsPage(page);
    }
    if (!select([&](const Row& row) { return row.id == hit.id && row.key == hit.key && row.plugin_setting == hit.plugin_setting && row.title == hit.title; }))
        select([&](const Row& row) { return row.id == hit.id && row.key == hit.key && row.plugin_setting == hit.plugin_setting; });
    settings_state_.editing = false;
    ensureSettingVisible();
    settingsPreviewFocus();
    audio_.context();
    InvalidateRect(hwnd_, nullptr, FALSE);
}
