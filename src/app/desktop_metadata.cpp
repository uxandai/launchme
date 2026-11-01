// Edytuj metadane (the desktop window; in Tryb TV from the game's menu): the player's own title, description, developer, publisher, release
// date and genres for one game, over what the stores and databases found (a PC port matched as the
// console game it came from). Only fields changed in the dialog become overrides; an emptied field
// goes back to the automatic value, and Przywróć automatyczne forgets them all.
#include "launcher.hpp"
#include "../platform/ascii_text.hpp"

#include "../library/steam_stats.hpp"
#include "../platform/text_encoding.hpp"

#include <algorithm>
#include <cmath>
#include <cwctype>
#include <unordered_map>

using namespace desktop_ui;

namespace {
constexpr size_t limits[meta_fields] = {120, 4000, 160, 160, 40, 200, 16, 20};
constexpr int text_fields = 7; // Kept as typed; Czas gry (the last field) is kept in minutes.
constexpr size_t playtime_field = 7;
std::wstring metadata::Override::* const members[text_fields] = {
    &metadata::Override::title, &metadata::Override::description, &metadata::Override::developer,
    &metadata::Override::publisher, &metadata::Override::release_date, &metadata::Override::genres, &metadata::Override::hours,
};
// 360 → "6", 450 → "7,5" (half hours).
std::wstring hoursText(uint32_t minutes) {
    const int halves = static_cast<int>(std::lround(minutes / 30.0));
    return std::to_wstring(halves / 2) + (halves % 2 ? L",5" : L"");
}
// "22", "7,5", "20-24 h" (the middle of a range) → minutes; none without a number.
std::optional<uint32_t> ownMinutes(const std::wstring& text) {
    std::vector<double> numbers;
    for (size_t i = 0; i < text.size() && numbers.size() < 2;) {
        if (!std::iswdigit(text[i])) { ++i; continue; }
        double value = 0;
        while (i < text.size() && std::iswdigit(text[i])) value = value * 10 + (text[i++] - L'0');
        if (i + 1 < text.size() && (text[i] == L',' || text[i] == L'.') && std::iswdigit(text[i + 1])) {
            double scale = 0.1;
            for (++i; i < text.size() && std::iswdigit(text[i]); ++i) { value += (text[i] - L'0') * scale; scale *= 0.1; }
        }
        numbers.push_back(value);
    }
    if (numbers.empty()) return std::nullopt;
    const double hours = numbers.size() == 2 ? (numbers[0] + numbers[1]) * 0.5 : numbers[0];
    if (hours <= 0 || hours > 2000) return std::nullopt;
    return static_cast<uint32_t>(std::lround(hours * 60));
}
// "2024", "12.05.2024", "2024-05-12" → a date (the year at least); unknown without a year.
game_index::Date ownDate(const std::wstring& text) {
    std::vector<int> parts;
    for (size_t i = 0; i < text.size() && parts.size() < 3;) {
        if (!std::iswdigit(text[i])) { ++i; continue; }
        int value = 0;
        while (i < text.size() && std::iswdigit(text[i]) && value < 100000) value = value * 10 + (text[i++] - L'0');
        parts.push_back(value);
    }
    game_index::Date date;
    const auto year = std::find_if(parts.begin(), parts.end(), [](int value) { return value >= 1950 && value <= 2100; });
    if (year == parts.end()) return date;
    date.year = *year;
    if (parts.size() == 3) {
        const bool first = year == parts.begin(); // yyyy-mm-dd, else dd.mm.yyyy.
        const int month = first ? parts[1] : parts[1], day = first ? parts[2] : parts[0];
        if (month >= 1 && month <= 12) { date.month = month; if (day >= 1 && day <= 31) date.day = day; }
    } else if (parts.size() == 2) {
        const int month = year == parts.begin() ? parts[1] : parts[0];
        if (month >= 1 && month <= 12) date.month = month;
    }
    return date;
}
std::vector<std::wstring> ownList(const std::wstring& text) {
    std::vector<std::wstring> items;
    std::wstring item;
    const auto flush = [&] {
        while (!item.empty() && item.back() == L' ') item.pop_back();
        size_t start = 0;
        while (start < item.size() && item[start] == L' ') ++start;
        if (start < item.size() && items.size() < 6) items.push_back(item.substr(start));
        item.clear();
    };
    for (const wchar_t c : text) { if (c == L',' || c == L';') flush(); else item += c; }
    flush();
    return items;
}
std::wstring trimmed(std::wstring text) { return std::wstring(ascii_text::trim(text)); }
} // namespace

const metadata::Override* Launcher::metadataOverride(const Game& game) const {
    if (!metadata_loaded_) {
        metadata_loaded_ = true;
        metadata_overrides_.load(userConfigDirectory() / L"metadata_overrides.json");
    }
    const auto key = gameKey(game);
    return key.empty() ? nullptr : metadata_overrides_.find(key);
}

// The game index (Towarzyszka's tools, sorting, plan_grania) takes them ahead of every source.
void Launcher::applyMetadataToIndex(game_index::LibraryGame& item, const Game& game) const {
    const auto* own = metadataOverride(game);
    if (!own) return;
    item.own_description = own->description;
    item.own_developers = ownList(own->developer);
    item.own_publishers = ownList(own->publisher);
    item.own_genres = ownList(own->genres);
    item.own_release = ownDate(own->release_date);
    item.own_hltb_main = ownMinutes(own->hours);
}

// A new title: what was looked up under the old one (HowLongToBeat, RAWG, its misses) is dropped and
// looked up again by the new name.
void Launcher::forgetTitleLookups(const std::string& key) {
    std::string cache_key;
    for (const auto* games : {&public_library_, &private_category_.games, &grouped_library_})
        for (const auto& game : *games) if (cache_key.empty() && gameKey(game) == key) cache_key = gameCacheKey(game);
    if (cache_key.empty()) return;
    const auto config = userConfigDirectory();
    const auto name = text_encoding::wideLenient(cache_key);
    std::error_code error;
    // Lookups made by the old title go (HowLongToBeat, the automatic RAWG answer, their misses); the
    // game's identification (Zidentyfikuj, metadata\<key>.rawg.json) stays: it was chosen, not looked
    // up by the title, and a new title must not undo it.
    for (const auto& file : {config / L"plugins" / L"data" / L"launchme.howlongtobeat" / (name + L".json"), config / L"knowledge" / L"rawg" / (name + L".json"),
                             config / L"knowledge" / L"miss" / (name + L".rawg"), config / L"knowledge" / L"miss" / (name + L".hltb")})
        std::filesystem::remove(file, error);
    if (!std::filesystem::exists(config / L"metadata" / (name + L".rawg.json"), error)) {
        rawg_metadata_.erase(key);
        gameIndexMerge(key, game_index::Source::rawg, {});
    }
    gameIndexMerge(key, game_index::Source::hltb, {});
    gameIndexTouch({key});
}

// Once, when the library is there: titles set before their lookups were redone (an older version)
// drop what was found under the old name.
void Launcher::refreshTitleLookups() {
    if (metadata_lookups_checked_ || public_library_.empty() || scanning_) return;
    metadata_lookups_checked_ = true;
    if (!metadata_loaded_) { metadata_loaded_ = true; metadata_overrides_.load(userConfigDirectory() / L"metadata_overrides.json"); }
    std::vector<std::string> stale;
    for (const auto& [key, value] : metadata_overrides_.items()) if (!value.title.empty() && !value.looked_up) stale.push_back(key);
    if (stale.empty()) return;
    for (const auto& key : stale) {
        forgetTitleLookups(key);
        auto value = *metadata_overrides_.find(key);
        value.looked_up = true;
        metadata_overrides_.set(key, std::move(value));
    }
    metadata_overrides_.save(userConfigDirectory() / L"metadata_overrides.json");
    syncGameIndex();
}

void Launcher::applyMetadataTitles(std::vector<Game>& games) const {
    for (auto& game : games)
        if (const auto* value = metadataOverride(game); value && !value->title.empty()) game.name = value->title;
}

// Steam's own count for a Steam game, from what was already read (the list preview or Szczegóły).
std::optional<uint32_t> Launcher::desktopSteamPlaytime(const Game& game) const {
    if (!steamArtwork(game)) return std::nullopt;
    if (const auto* preview = preview_.find(game); preview && preview->playtime_minutes) return preview->playtime_minutes;
    if (!detail_games_.empty() && gameKey(detail_games_[0]) == gameKey(game)) return details_.playtime_minutes;
    return std::nullopt;
}

void Launcher::desktopMetaOpen(const Game& game) {
    if ((desktop_mode_ && !desktopActive()) || !isGame(game) || gameKey(game).empty()) return;
    if (desktop_mode_) {
        desktopCloseMenu();
        desktopQuickClose();
        desktop_downloads_open_ = false;
        desktopSearchFocus(false);
    }
    bool loading = false;
    const GameDetails details = desktopDetails(game, loading); // As the pane shows them, overrides included.
    auto& meta = desktop_meta_;
    meta = {};
    meta.open = true;
    meta.game = game.name;
    meta.fields = {game.name, details.description, details.developer, details.publisher, details.release_date, details.genres, {}, {}};
    // Czas przejścia: their own, else what HowLongToBeat gave the index.
    if (const auto* own = metadataOverride(game); own && !own->hours.empty()) meta.fields[6] = own->hours;
    else if (game_index_)
        meta.fields[6] = game_index_->read([key = gameKey(game)](const game_index::Index& index) {
            const auto* record = index.get(key);
            return record && record->hltb_main ? hoursText(*record->hltb_main) : std::wstring{};
        });
    // Czas gry: as shown (their own counted on, or the automatic one); beside it where the automatic one comes from.
    const auto automatic = automaticPlaytime(game, desktopSteamPlaytime(game));
    if (const auto shown = gamePlaytime(game, desktopSteamPlaytime(game)); shown && *shown) meta.fields[playtime_field] = metadata::playtimeText(*shown);
    if (automatic) {
        const auto* origin = steamArtwork(game) ? L"Steam" : game.store_playtime_minutes > localPlaytime(game) ? sourceName(game.source) : L"Launchme";
        meta.playtime_note = tr(L"Automatycznie: ") + formatPlaytime(*automatic) + L" (" + origin + L")";
    } else meta.playtime_note = tr(L"Steam: czas gry jeszcze się wczytuje");
    meta.shown = meta.fields;
    meta.overridden = metadataOverride(game) != nullptr;
    meta.identify = !steamArtwork(game); // Steam's own games are identified by their AppID already.
    meta.caret = meta.fields[0].size();
    meta.pad = !desktop_mode_;
    desktop_meta_key_ = gameKey(game);
    desktop_meta_game_ = game;
    MSG pending; // The key that opened it must not type into the title.
    while (PeekMessageW(&pending, hwnd_, WM_CHAR, WM_CHAR, PM_REMOVE)) {}
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::desktopMetaClose() {
    if (!desktop_meta_.open) return;
    desktop_meta_ = {};
    desktop_meta_key_.clear();
    desktop_meta_game_ = {};
    InvalidateRect(hwnd_, nullptr, FALSE);
}

// Titles, the pane and Szczegóły take the new values (the library is built again: a title also
// orders, groups and finds the game).
// The screens Tryb TV keeps open hold copies of their games (Szczegóły, a system's games, the search,
// the cards drawn): after a rename they take the name the library has now, so Tryb TV and the desktop
// show the same title.
void Launcher::refreshShownNames() {
    std::unordered_map<std::string, std::wstring> names;
    for (const auto* games : {&grouped_library_, &public_library_, &private_category_.games})
        for (const auto& game : *games) if (const auto key = gameKey(game); !key.empty()) names.emplace(key, game.name);
    const auto rename = [&](Game& game) {
        if (const auto found = names.find(gameKey(game)); found != names.end()) game.name = found->second;
    };
    for (auto* games : {&detail_games_, &nested_games_, &search_results_, &crop_games_, &downloads_games_})
        for (auto& game : *games) rename(game);
    for (auto& card : cards_) if (isGame(card.game)) rename(card.game);
}

void Launcher::desktopMetadataChanged() {
    desktop_details_key_.clear();
    buildShelves();
    syncGameIndex(); // Towarzyszka's index takes the new facts too.
    refreshShownNames();
    if (screen_ == ScreenMode::shelves || screen_ == ScreenMode::library) rebuildCards(true);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::desktopMetaSave() {
    auto& meta = desktop_meta_;
    if (!meta.open || desktop_meta_key_.empty()) return;
    metadata::Override value;
    if (const auto* existing = metadata_overrides_.find(desktop_meta_key_)) value = *existing;
    for (int i = 0; i < text_fields; ++i)
        if (meta.fields[static_cast<size_t>(i)] != meta.shown[static_cast<size_t>(i)]) // Edited: its own text; emptied: automatic again.
            value.*members[i] = trimmed(meta.fields[static_cast<size_t>(i)]);
    if (meta.fields[playtime_field] != meta.shown[playtime_field]) {
        // Czas gry: counted on from the typed value; emptied: the automatic one again.
        const auto typed = trimmed(meta.fields[playtime_field]);
        const Game* game = nullptr;
        for (const auto* games : {&public_library_, &private_category_.games, &grouped_library_})
            for (const auto& item : *games) if (!game && gameKey(item) == desktop_meta_key_) game = &item;
        const auto automatic = game ? automaticPlaytime(*game, desktopSteamPlaytime(*game)) : std::nullopt;
        if (typed.empty()) value.playtime.reset();
        else if (const auto minutes = metadata::parsePlaytime(typed); !minutes) {
            showNotification(NotificationKind::error, tr(L"Nie rozpoznano czasu gry"), tr(L"Wpisz np. 12, 12,5 albo 12:30."));
            meta.focus = static_cast<int>(playtime_field); meta.caret = meta.fields[playtime_field].size();
            InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        } else if (!automatic) {
            showNotification(NotificationKind::error, tr(L"Czas gry ze Steam jeszcze się wczytuje"), tr(L"Spróbuj ponownie za chwilę."));
            return;
        } else { value.playtime = *minutes; value.playtime_base = *automatic; }
    }
    const bool retitled = meta.fields[0] != meta.shown[0];
    const auto key = desktop_meta_key_;
    if (retitled) value.looked_up = !value.title.empty(); // Looked up afresh below.
    metadata_overrides_.set(desktop_meta_key_, std::move(value));
    if (!metadata_overrides_.save(userConfigDirectory() / L"metadata_overrides.json")) {
        showNotification(NotificationKind::error, tr(L"Nie udało się zapisać metadanych"), tr(L"Sprawdź dostęp do katalogu konfiguracji."));
        return;
    }
    desktopMetaClose();
    if (retitled) forgetTitleLookups(key);
    desktopMetadataChanged();
}

void Launcher::desktopMetaActivate(int item) {
    switch (static_cast<MetaItem>(item)) {
    case MetaItem::save: desktopMetaSave(); return;
    case MetaItem::restore: {
        if (!desktop_meta_.overridden) return;
        const auto key = desktop_meta_key_;
        const auto* own = metadata_overrides_.find(key);
        const bool titled = own && !own->title.empty(); // Lookups made under that title go too.
        metadata_overrides_.erase(key);
        if (!metadata_overrides_.save(userConfigDirectory() / L"metadata_overrides.json")) {
            showNotification(NotificationKind::error, tr(L"Nie udało się zapisać metadanych"), tr(L"Sprawdź dostęp do katalogu konfiguracji."));
            return;
        }
        desktopMetaClose();
        if (titled) forgetTitleLookups(key);
        desktopMetadataChanged();
        return;
    }
    case MetaItem::cancel: case MetaItem::close: desktopMetaClose(); return;
    case MetaItem::identify: {
        // The game's match in RAWG (its details and artwork), in place of this dialog: what was typed here is not kept.
        if (!desktop_meta_.identify) return;
        const auto game = desktop_meta_game_;
        desktopMetaClose();
        openIdentifyFor(game);
        return;
    }
    default: return;
    }
}

void Launcher::desktopMetaInsert(std::wstring_view text) {
    auto& meta = desktop_meta_;
    if (meta.focus < 0 || meta.focus >= meta_fields) return;
    const auto index = static_cast<size_t>(meta.focus);
    auto& field = meta.fields[index];
    std::wstring clean;
    for (const wchar_t c : text) {
        if (c == L'\r') continue;
        if (c == L'\n' || c == L'\t') { clean += index == 1 && c == L'\n' ? L'\n' : L' '; continue; } // Lines only in Opis.
        if (c >= L' ' && c != 127) clean += c;
    }
    if (clean.empty()) return;
    if (meta.selected) { field.clear(); meta.caret = 0; meta.selected = false; }
    meta.caret = std::min(meta.caret, field.size());
    const size_t room = limits[index] > field.size() ? limits[index] - field.size() : 0;
    if (clean.size() > room) clean.resize(room);
    field.insert(meta.caret, clean);
    meta.caret += clean.size();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::desktopMetaChar(wchar_t character) {
    if (character < L' ' || character == 127) return; // Enter, Tab and Backspace are keys (desktopMetaKey).
    desktopMetaInsert(std::wstring_view(&character, 1));
}

bool Launcher::desktopMetaKey(WPARAM key) {
    auto& meta = desktop_meta_;
    const bool control = GetKeyState(VK_CONTROL) < 0, shift = GetKeyState(VK_SHIFT) < 0;
    const int field = meta.focus >= 0 && meta.focus < meta_fields ? meta.focus : -1;
    auto* text = field >= 0 ? &meta.fields[static_cast<size_t>(field)] : nullptr;
    if (text) meta.caret = std::min(meta.caret, text->size());
    switch (key) {
    case VK_ESCAPE: desktopMetaClose(); return true;
    case VK_TAB: { // The fields, then Zapisz, Przywróć automatyczne, Anuluj and Zidentyfikuj.
        std::vector<int> order;
        for (int i = 0; i < meta_fields; ++i) order.push_back(i);
        for (const auto item : {MetaItem::save, MetaItem::restore, MetaItem::cancel}) order.push_back(static_cast<int>(item));
        if (meta.identify) order.push_back(static_cast<int>(MetaItem::identify));
        const auto at = std::find(order.begin(), order.end(), meta.focus);
        const int count = static_cast<int>(order.size()), position = at == order.end() ? 0 : static_cast<int>(at - order.begin());
        meta.focus = order[static_cast<size_t>((position + (shift ? count - 1 : 1)) % count)];
        meta.selected = false;
        if (meta.focus < meta_fields) meta.caret = meta.fields[static_cast<size_t>(meta.focus)].size();
        break;
    }
    case VK_RETURN:
        if (control || (field >= 0 && field != 1)) desktopMetaSave(); // Ctrl+Enter anywhere; Enter in a one-line field.
        else if (field == 1) desktopMetaInsert(L"\n");
        else desktopMetaActivate(meta.focus);
        return true;
    case VK_SPACE:
        if (field < 0) desktopMetaActivate(meta.focus);
        return true; // In a field its character types the space.
    case 'A':
        if (!control || !text) return true;
        meta.selected = !text->empty();
        break;
    case 'V': {
        if (!control || !text || !OpenClipboard(hwnd_)) return true;
        std::wstring pasted;
        if (const auto handle = GetClipboardData(CF_UNICODETEXT))
            if (const auto* data = static_cast<const wchar_t*>(GlobalLock(handle))) { pasted = data; GlobalUnlock(handle); }
        CloseClipboard();
        desktopMetaInsert(pasted);
        return true;
    }
    case VK_LEFT: if (text && meta.caret) --meta.caret; meta.selected = false; break;
    case VK_RIGHT: if (text && meta.caret < text->size()) ++meta.caret; meta.selected = false; break;
    case VK_HOME: meta.caret = 0; meta.selected = false; break;
    case VK_END: if (text) meta.caret = text->size(); meta.selected = false; break;
    case VK_BACK:
        if (!text) return true;
        if (meta.selected) { text->clear(); meta.caret = 0; meta.selected = false; }
        else if (meta.caret) text->erase(--meta.caret, 1);
        break;
    case VK_DELETE:
        if (!text) return true;
        if (meta.selected) { text->clear(); meta.caret = 0; meta.selected = false; }
        else if (meta.caret < text->size()) text->erase(meta.caret, 1);
        break;
    default: return true; // Its own keys only while it is open; text comes as WM_CHAR.
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
    return true;
}

bool Launcher::desktopMetaPointer(float mouse_x, float mouse_y, bool press, bool double_click) {
    auto& meta = desktop_meta_;
    if (!meta.open) return false;
    const auto view = renderer_.desktopViewport();
    const float scale = desktopScale();
    const MetaLayout layout{view.width, view.height};
    const int hit = layout.hit(mouse_x / scale, mouse_y / scale, meta.identify);
    if (!press) {
        if (hit != meta.hover) { meta.hover = hit; InvalidateRect(hwnd_, nullptr, FALSE); }
        return true;
    }
    if (hit < 0) return true; // Outside: it stays (its edits are not lost to a stray click).
    if (hit < meta_fields) {
        meta.focus = hit;
        meta.caret = meta.fields[static_cast<size_t>(hit)].size();
        meta.selected = false;
        // One-line fields: the caret where the click falls, a drag or double click selects.
        if (hit != static_cast<int>(MetaItem::description)) desktopTextPress(layout.field(hit).left + 12, mouse_x / scale, scale, double_click, false);
        InvalidateRect(hwnd_, nullptr, FALSE);
        return true;
    }
    desktopMetaActivate(hit);
    return true;
}

// ---- Tryb TV ------------------------------------------------------------------------------------
// The same dialog as a layer over the TV canvas: the D-pad moves between its fields (rows as
// drawn) and buttons, A types into a field with the on-screen keyboard (a physical keyboard types
// straight in), B closes it.

void Launcher::openMetadata(const Game& game) {
    if (desktop_mode_ || metadataOpen()) return;
    dismissMenu(); closeQuick(); closeKeyboard();
    desktopMetaOpen(game);
    if (!desktop_meta_.open) return;
    desktop_meta_.focus = 0;
    audio_.drawer();
}

namespace {
// Rows of the dialog as drawn: Tytuł; Opis; Producent | Wydawca; Data premiery | Czas przejścia | Gatunki;
// Czas gry; Przywróć | Zidentyfikuj | Zapisz | Anuluj.
std::vector<std::vector<int>> metaRows(bool identify) {
    std::vector<std::vector<int>> rows{{0}, {1}, {2, 3}, {4, 6, 5}, {7}, {static_cast<int>(MetaItem::restore)}};
    if (identify) rows.back().push_back(static_cast<int>(MetaItem::identify));
    rows.back().push_back(static_cast<int>(MetaItem::save));
    rows.back().push_back(static_cast<int>(MetaItem::cancel));
    return rows;
}
}

void Launcher::metadataKey(WPARAM key, bool repeated) {
    auto& meta = desktop_meta_;
    if (!metadataOpen()) return;
    const bool field = meta.focus >= 0 && meta.focus < meta_fields;
    const auto rows = metaRows(meta.identify);
    int row = 0, column = 0;
    for (int r = 0; r < static_cast<int>(rows.size()); ++r)
        for (int c = 0; c < static_cast<int>(rows[static_cast<size_t>(r)].size()); ++c)
            if (rows[static_cast<size_t>(r)][static_cast<size_t>(c)] == meta.focus) { row = r; column = c; }
    const auto focus = [&](int item) {
        if (item == meta.focus) { navigationBoundary(); return; }
        meta.focus = item;
        meta.selected = false;
        if (item < meta_fields) meta.caret = meta.fields[static_cast<size_t>(item)].size();
        audio_.navigate();
        InvalidateRect(hwnd_, nullptr, FALSE);
    };
    switch (key) {
    case VK_ESCAPE: if (!repeated) { desktopMetaClose(); audio_.navigate(); } return;
    case VK_UP: case VK_DOWN: {
        const int next = std::clamp(row + (key == VK_DOWN ? 1 : -1), 0, static_cast<int>(rows.size()) - 1);
        const auto& line = rows[static_cast<size_t>(next)];
        focus(line[std::min(static_cast<size_t>(column), line.size() - 1)]);
        return;
    }
    case VK_LEFT: case VK_RIGHT:
        // A physical keyboard moves the caret in a field; the pad moves between the row's items.
        if (field && !pad_input_) { desktopMetaKey(key); return; }
        {
            const auto& line = rows[static_cast<size_t>(row)];
            focus(line[static_cast<size_t>(std::clamp(column + (key == VK_RIGHT ? 1 : -1), 0, static_cast<int>(line.size()) - 1))]);
        }
        return;
    case VK_RETURN: case VK_SPACE:
        if (repeated) return;
        if (!field) { desktopMetaActivate(meta.focus); return; }
        if (key == VK_SPACE) return; // Typed as a character.
        if (pad_input_) { openKeyboard(); return; }
        desktopMetaKey(key); // Enter saves (a new line in Opis).
        return;
    default: desktopMetaKey(key); return; // Backspace, Delete, Home, End, Tab, Ctrl+A, Ctrl+V.
    }
}

void Launcher::metadataClick(float x, float y) {
    if (!metadataOpen()) return;
    const auto view = renderer_.viewport();
    const int hit = MetaLayout{view.width, view.height}.hit(x, y, desktop_meta_.identify); // Logical coordinates.
    if (hit < 0) return; // Outside: it stays.
    if (hit < meta_fields) {
        desktop_meta_.focus = hit;
        desktop_meta_.caret = desktop_meta_.fields[static_cast<size_t>(hit)].size();
        desktop_meta_.selected = false;
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    desktopMetaActivate(hit);
}

void Launcher::metadataPointer(float x, float y) {
    if (!metadataOpen()) return;
    const auto view = renderer_.viewport();
    const int hit = MetaLayout{view.width, view.height}.hit(x, y, desktop_meta_.identify); // Logical coordinates.
    if (hit != desktop_meta_.hover) { desktop_meta_.hover = hit; InvalidateRect(hwnd_, nullptr, FALSE); }
}