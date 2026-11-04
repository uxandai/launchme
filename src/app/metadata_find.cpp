// "Znajdź" in Edytuj metadane (desktop window and Tryb TV, the same dialog): a pane over the fields where the
// player searches the stores and databases (src/library/metadata_sources.hpp), picks a game and chooses
// which of its details (Opis, Producent, Wydawca, Data premiery, Gatunki) to take. "Wstaw" copies them into
// the dialog's fields; they become the player's own only with the dialog's Zapisz. Text only: no artwork.
// Each source is asked on its own worker and answers when it can (RemoteMessage → metaFindOffers).
#include "launcher.hpp"
#include "../library/metadata_sources.hpp"
#include "../platform/ascii_text.hpp"

#include <algorithm>

using namespace desktop_ui;
namespace mf = meta_find;

namespace {
// The pane's items as the pad walks them: the title and its button; each result; each detail that can be
// taken; the buttons.
std::vector<std::vector<int>> findRows(const mf::State& state) {
    std::vector<std::vector<int>> rows{{mf::item::query, mf::item::search}};
    for (int i = 0; i < static_cast<int>(state.results.size()) && i < mf::visible; ++i) rows.push_back({mf::item::result_first + i});
    for (int i = 0; i < mf::take_count; ++i) if (state.canTake(i)) rows.push_back({mf::item::take_first + i});
    std::vector<int> last;
    if (state.anyTaken()) last.push_back(mf::item::insert);
    last.push_back(mf::item::back);
    rows.push_back(last);
    return rows;
}
bool isResult(int item) { return item >= mf::item::result_first && item < mf::item::result_first + mf::visible; }
bool isTake(int item) { return item >= mf::item::take_first && item < mf::item::take_first + mf::take_count; }
constexpr size_t query_limit = 120;
} // namespace

void Launcher::metaFindOpen() {
    auto& meta = desktop_meta_;
    if (!meta.open || meta.find.open) return;
    auto& find = meta.find;
    find = {};
    find.open = true;
    find.focus = mf::item::query;
    find.query = std::wstring(ascii_text::trim(meta.fields[0]));
    if (find.query.empty()) find.query = desktop_meta_game_.name;
    find.seed = find.query;
    find.caret = find.query.size();
    meta.selected = false;
    metaFindSearch();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::metaFindClose() {
    auto& find = desktop_meta_.find;
    if (!find.open) return;
    ++metadata_find_generation_; // Answers still on their way are dropped.
    find = {};
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::metaFindSearch() {
    auto& find = desktop_meta_.find;
    if (!find.open) return;
    const std::wstring title(ascii_text::trim(find.query));
    find.results.clear(); find.scores.clear(); find.chosen = 0; find.take = {};
    find.generation = ++metadata_find_generation_;
    if (title.empty()) { find.busy = false; find.waiting = 0; find.status = tr(L"Wpisz tytuł gry."); InvalidateRect(hwnd_, nullptr, FALSE); return; }
    metadata_sources::Query query;
    query.title = title;
    query.polish = i18n::language() == i18n::Language::pl;
    query.switch_id = metadata_sources::titleId(title);
    if (title == find.seed) { // The game's own ids: its file name, its store.
        const auto& game = desktop_meta_game_;
        if (query.switch_id.empty() && !game.rom.empty()) query.switch_id = metadata_sources::titleId(game.rom.filename().wstring());
        if (query.switch_id.empty() && !game.rom.empty()) query.switch_id = metadata_sources::titleId(game.rom.parent_path().filename().wstring());
        if (query.switch_id.empty()) query.switch_id = metadata_sources::titleId(game.name);
        query.steam_app = game.app_id;
        if (game.source == GameSource::gog) query.gog_id = game.source_id;
        query.xbox_product = game.xbox_product;
    }
    std::vector<metadata_sources::Source> sources;
    for (const auto source : metadata_sources::all) if (metadata_sources::applicable(source, query)) sources.push_back(source);
    find.waiting = static_cast<int>(sources.size());
    find.busy = !sources.empty();
    find.status = sources.empty() ? tr(L"Brak źródeł dla tej gry.") : tr(L"Szukanie…");
    const auto generation = find.generation;
    for (const auto source : sources) {
        if (!runtime_started_) { // UI tests: answered at once, by the injected transport.
            metaFindOffers(generation, metadata_sources::find(source, query, metadata_find_get_));
            continue;
        }
        const auto alive = alive_; const auto hwnd = hwnd_;
        const auto deliver = [alive, hwnd, generation](metadata_sources::Found found) {
            if (!alive->load()) return;
            auto message = std::make_unique<RemoteMessage>();
            message->run = [generation, found = std::move(found)](Launcher& app) mutable { app.metaFindOffers(generation, std::move(found)); };
            if (PostMessageW(hwnd, remote_message, 0, reinterpret_cast<LPARAM>(message.get()))) message.release();
        };
        const bool started = work_.start([source, query, get = metadata_find_get_, deliver] { deliver(metadata_sources::find(source, query, get)); },
                                         [deliver](std::exception_ptr) { deliver({}); });
        if (!started) deliver({});
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::metaFindOffers(uint32_t generation, metadata_sources::Found found) {
    auto& find = desktop_meta_.find;
    if (!desktop_meta_.open || !find.open || generation != find.generation) return;
    const mf::Result* kept = find.focus >= mf::item::result_first ? find.current() : nullptr; // The player is already choosing.
    const std::wstring kept_title = kept ? kept->title : std::wstring{}, kept_source = kept ? kept->source : std::wstring{};
    for (auto& offer : found.offers) {
        mf::Result result;
        result.title = std::move(offer.title);
        result.source = metadata_sources::name(offer.source);
        result.note = std::move(offer.note);
        result.values = {offer.details.description, offer.details.developer, offer.details.publisher, offer.details.release_date, offer.details.genres};
        size_t at = 0; // After the ones that score the same: the older answer stays above.
        while (at < find.scores.size() && find.scores[at] >= offer.score) ++at;
        find.scores.insert(find.scores.begin() + static_cast<std::ptrdiff_t>(at), offer.score);
        find.results.insert(find.results.begin() + static_cast<std::ptrdiff_t>(at), std::move(result));
    }
    if (static_cast<int>(find.results.size()) > mf::visible) { find.results.resize(mf::visible); find.scores.resize(mf::visible); }
    if (find.waiting > 0) --find.waiting;
    find.busy = find.waiting > 0;
    if (kept) {
        int index = 0;
        for (int i = 0; i < static_cast<int>(find.results.size()); ++i)
            if (find.results[static_cast<size_t>(i)].title == kept_title && find.results[static_cast<size_t>(i)].source == kept_source) { index = i; break; }
        find.chosen = index;
    } else metaFindChoose(0);
    if (!find.results.empty()) find.status.clear();
    else find.status = find.busy ? tr(L"Szukanie…") : tr(L"Nic nie znaleziono. Zmień tytuł i szukaj ponownie.");
    InvalidateRect(hwnd_, nullptr, FALSE);
}

// The highlighted result; every detail it has is ticked.
void Launcher::metaFindChoose(int index) {
    auto& find = desktop_meta_.find;
    if (!find.has(index)) { find.chosen = 0; find.take = {}; return; }
    find.chosen = index;
    for (int i = 0; i < mf::take_count; ++i) find.take[static_cast<size_t>(i)] = find.canTake(i);
}

void Launcher::metaFindInsert() {
    auto& meta = desktop_meta_;
    auto& find = meta.find;
    const auto* result = find.current();
    if (!meta.open || !find.open || !result) return;
    int first = -1;
    for (int slot = 0; slot < mf::take_count; ++slot) {
        if (!find.take[static_cast<size_t>(slot)] || !find.canTake(slot)) continue;
        const auto field = static_cast<size_t>(mf::target_field[static_cast<size_t>(slot)]);
        auto value = result->values[slot];
        if (field != 1) std::replace(value.begin(), value.end(), L'\n', L' '); // Lines only in Opis.
        if (value.size() > meta_limits[field]) value.resize(meta_limits[field]);
        meta.fields[field] = std::move(value);
        if (first < 0) first = static_cast<int>(field);
    }
    if (first < 0) return;
    metaFindClose();
    meta.focus = first;
    meta.caret = meta.fields[static_cast<size_t>(first)].size();
    meta.selected = false;
    if (!desktop_mode_) audio_.navigate();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::metaFindActivate(int item) {
    auto& find = desktop_meta_.find;
    if (item == mf::item::search) metaFindSearch();
    else if (isResult(item)) {
        metaFindChoose(item - mf::item::result_first);
        if (find.anyTaken()) find.focus = mf::item::insert;
    } else if (isTake(item)) {
        const int slot = item - mf::item::take_first;
        if (find.canTake(slot)) find.take[static_cast<size_t>(slot)] = !find.take[static_cast<size_t>(slot)];
        if (!find.anyTaken() && find.focus == mf::item::insert) find.focus = mf::item::back;
    } else if (item == mf::item::insert) metaFindInsert();
    else if (item == mf::item::back || item == mf::item::close) metaFindClose();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::metaFindText(std::wstring_view text) {
    auto& find = desktop_meta_.find;
    if (find.focus != mf::item::query) return;
    std::wstring clean;
    for (const wchar_t c : text) {
        if (c == L'\r') continue;
        if (c == L'\n' || c == L'\t') clean += L' ';
        else if (c >= L' ' && c != 127) clean += c;
    }
    if (clean.empty()) return;
    if (find.selected) { find.query.clear(); find.caret = 0; find.selected = false; }
    find.caret = std::min(find.caret, find.query.size());
    const size_t room = query_limit > find.query.size() ? query_limit - find.query.size() : 0;
    if (clean.size() > room) clean.resize(room);
    find.query.insert(find.caret, clean);
    find.caret += clean.size();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

bool Launcher::metaFindKey(WPARAM key, bool repeated) {
    auto& meta = desktop_meta_;
    auto& find = meta.find;
    const bool control = GetKeyState(VK_CONTROL) < 0, shift = GetKeyState(VK_SHIFT) < 0;
    const bool in_query = find.focus == mf::item::query;
    const bool pad = !desktop_mode_ && pad_input_;
    find.caret = std::min(find.caret, find.query.size());
    const auto rows = findRows(find);
    int row = 0, column = 0;
    for (int r = 0; r < static_cast<int>(rows.size()); ++r)
        for (int c = 0; c < static_cast<int>(rows[static_cast<size_t>(r)].size()); ++c)
            if (rows[static_cast<size_t>(r)][static_cast<size_t>(c)] == find.focus) { row = r; column = c; }
    const auto focus = [&](int item) {
        if (item == find.focus) { if (!desktop_mode_) navigationBoundary(); return; }
        find.focus = item;
        find.selected = false;
        if (item == mf::item::query) find.caret = find.query.size();
        else if (isResult(item)) metaFindChoose(item - mf::item::result_first);
        if (!desktop_mode_) audio_.navigate();
        InvalidateRect(hwnd_, nullptr, FALSE);
    };
    switch (key) {
    case VK_ESCAPE: if (!repeated) { metaFindClose(); if (!desktop_mode_) audio_.navigate(); } return true;
    case VK_TAB: {
        std::vector<int> order;
        for (const auto& line : rows) order.insert(order.end(), line.begin(), line.end());
        const auto at = std::find(order.begin(), order.end(), find.focus);
        const int count = static_cast<int>(order.size()), position = at == order.end() ? 0 : static_cast<int>(at - order.begin());
        focus(order[static_cast<size_t>((position + (shift ? count - 1 : 1)) % count)]);
        return true;
    }
    case VK_UP: case VK_DOWN: {
        const int next = std::clamp(row + (key == VK_DOWN ? 1 : -1), 0, static_cast<int>(rows.size()) - 1);
        const auto& line = rows[static_cast<size_t>(next)];
        focus(line[std::min(static_cast<size_t>(column), line.size() - 1)]);
        return true;
    }
    case VK_LEFT: case VK_RIGHT:
        if (in_query && !pad) { // A keyboard moves the caret; the pad moves to the button.
            if (key == VK_LEFT && find.caret) --find.caret;
            else if (key == VK_RIGHT && find.caret < find.query.size()) ++find.caret;
            find.selected = false;
            InvalidateRect(hwnd_, nullptr, FALSE);
            return true;
        }
        {
            const auto& line = rows[static_cast<size_t>(row)];
            focus(line[static_cast<size_t>(std::clamp(column + (key == VK_RIGHT ? 1 : -1), 0, static_cast<int>(line.size()) - 1))]);
        }
        return true;
    case VK_RETURN:
        if (control && find.anyTaken()) { metaFindInsert(); return true; }
        if (repeated) return true;
        if (!in_query) metaFindActivate(find.focus);
        else if (pad) openKeyboard();
        else metaFindSearch();
        return true;
    case VK_SPACE:
        if (!in_query && !repeated) metaFindActivate(find.focus);
        return true; // In the title field its character types the space.
    case VK_HOME: if (in_query) { find.caret = 0; find.selected = false; } break;
    case VK_END: if (in_query) { find.caret = find.query.size(); find.selected = false; } break;
    case VK_BACK:
        if (!in_query) return true;
        if (find.selected) { find.query.clear(); find.caret = 0; find.selected = false; }
        else if (find.caret) find.query.erase(--find.caret, 1);
        break;
    case VK_DELETE:
        if (!in_query) return true;
        if (find.selected) { find.query.clear(); find.caret = 0; find.selected = false; }
        else if (find.caret < find.query.size()) find.query.erase(find.caret, 1);
        break;
    case 'A':
        if (control && in_query) find.selected = !find.query.empty();
        break;
    case 'V': {
        if (!control || !in_query || !OpenClipboard(hwnd_)) return true;
        std::wstring pasted;
        if (const auto handle = GetClipboardData(CF_UNICODETEXT))
            if (const auto* data = static_cast<const wchar_t*>(GlobalLock(handle))) { pasted = data; GlobalUnlock(handle); }
        CloseClipboard();
        metaFindText(pasted);
        return true;
    }
    default: return true; // Its own keys only while it is open; text comes as WM_CHAR.
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
    return true;
}

void Launcher::metaFindChar(wchar_t character) {
    if (character < L' ' || character == 127) return; // Enter, Tab and Backspace are keys.
    metaFindText(std::wstring_view(&character, 1));
}

// Logical coordinates (the desktop window's divided by its scale, Tryb TV's as they are); `scale` > 0 in the window.
bool Launcher::metaFindPointer(float x, float y, bool press, bool double_click, float scale) {
    auto& find = desktop_meta_.find;
    if (!find.open) return false;
    const auto view = desktop_mode_ ? renderer_.desktopViewport() : renderer_.viewport();
    const mf::Layout layout{view.width, view.height};
    const int hit = layout.hit(x, y, find);
    if (!press) {
        if (hit != find.hover) { find.hover = hit; InvalidateRect(hwnd_, nullptr, FALSE); }
        return true;
    }
    if (hit < 0) return true; // Outside: it stays.
    if (hit == mf::item::query) {
        find.focus = hit; find.caret = find.query.size(); find.selected = false;
        if (scale > 0) desktopTextPress(layout.field().left + 12, x, scale, double_click, false);
        InvalidateRect(hwnd_, nullptr, FALSE);
        return true;
    }
    find.selected = false;
    if (isResult(hit)) { // One click chooses; a double click takes it to Wstaw.
        metaFindChoose(hit - mf::item::result_first);
        find.focus = hit;
        if (double_click) metaFindActivate(hit);
        InvalidateRect(hwnd_, nullptr, FALSE);
        return true;
    }
    find.focus = hit;
    metaFindActivate(hit);
    return true;
}
