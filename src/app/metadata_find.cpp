// "Identyfikuj grę" in Edytuj metadane (desktop window and Tryb TV, the same pane): one list of what the
// databases (RAWG; ScreenScraper for an emulated game) and the stores (src/library/metadata_sources.hpp) know of
// the title, best first, each with its picture. A database's game identifies the game at once (its details,
// rating and artwork: startIdentification, src/app/identify.cpp); a store's details (Opis, Producent, Wydawca,
// Data premiery, Gatunki) go into the dialog's fields as ticked, and become the player's own with Zapisz.
// Each source is asked on its own worker and answers when it can (RemoteMessage → metaFindResults).
#include "launcher.hpp"
#include "../library/metadata_sources.hpp"
#include "../library/rawg.hpp"
#include "../library/screenscraper.hpp"
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
    std::vector<int> last{mf::item::back};
    if (state.canApply()) last.push_back(mf::item::insert);
    rows.push_back(last);
    return rows;
}
bool isResult(int item) { return item >= mf::item::result_first && item < mf::item::result_first + mf::visible; }
bool isTake(int item) { return item >= mf::item::take_first && item < mf::item::take_first + mf::take_count; }
constexpr size_t query_limit = 120;
using Scored = std::vector<std::pair<int, mf::Result>>;
// "2019 · PC, PlayStation 4": a database's year and platforms.
std::wstring yearAnd(const std::wstring& released, const std::wstring& other) {
    std::wstring note = released.size() >= 4 ? released.substr(0, 4) : std::wstring{};
    if (!other.empty()) note += (note.empty() ? L"" : L" · ") + other;
    return note;
}
// RAWG's pictures are large: its resized copy (640 px wide) is enough for the list.
std::wstring rawgThumb(std::wstring url) {
    for (const wchar_t* folder : {L"/media/games/", L"/media/screenshots/"})
        if (const auto at = url.find(folder); at != std::wstring::npos && url.find(L"/media/resize/") == std::wstring::npos) {
            url.replace(at, 7, L"/media/resize/640/-/");
            break;
        }
    return url;
}
// A database's rows scored by their title; with no title matching, its first few (the list shows their titles).
// A database's game is a little ahead of a store's of the same score: it identifies the game.
void keepMatching(Scored& rows) {
    const bool any = std::any_of(rows.begin(), rows.end(), [](const auto& row) { return row.first > 0; });
    if (any) rows.erase(std::remove_if(rows.begin(), rows.end(), [](const auto& row) { return row.first <= 0; }), rows.end());
    else { if (rows.size() > 3) rows.resize(3); for (auto& row : rows) row.first = 1; }
    for (auto& row : rows) row.first += 3;
}
} // namespace

void Launcher::metaFindOpen(bool direct) {
    auto& meta = desktop_meta_;
    if (!meta.open || meta.find.open) return;
    auto& find = meta.find;
    find = {};
    find.open = true;
    find.direct = direct;
    find.databases = meta.identify;
    find.emulated = desktop_meta_game_.kind == GameKind::nonsteam && desktop_meta_game_.platform != L"PC" && !desktop_meta_game_.rom.empty();
    find.query = std::wstring(ascii_text::trim(meta.fields[0]));
    if (find.query.empty()) find.query = desktop_meta_game_.name;
    find.seed = find.query;
    find.caret = find.query.size();
    // The pad starts on the results (the search runs at once); a keyboard in the title.
    find.focus = !desktop_mode_ && pad_input_ ? mf::item::result_first : mf::item::query;
    meta.selected = false;
    metaFindSearch();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

// The game's menu, Szczegóły, Dodaj / Edytuj grę: Edytuj metadane opened on its Identyfikuj grę.
void Launcher::metaIdentifyOpen(const Game& game) {
    if (desktop_meta_.open) desktopMetaClose();
    if (desktop_mode_) desktopMetaOpen(game); else openMetadata(game);
    if (!desktop_meta_.open) return;
    metaFindOpen(true);
}

void Launcher::metaFindClose() {
    auto& find = desktop_meta_.find;
    if (!find.open || find.applying) return;
    if (find.direct) { desktopMetaClose(); return; } // Opened for it: back where it was opened from.
    ++metadata_find_generation_; // Answers still on their way are dropped.
    find = {};
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::metaFindSearch() {
    auto& find = desktop_meta_.find;
    if (!find.open || find.applying) return;
    const std::wstring title(ascii_text::trim(find.query));
    find.results.clear(); find.scores.clear(); find.chosen = 0; find.take = {}; find.fetching.clear();
    find.generation = ++metadata_find_generation_;
    if (title.empty()) { find.busy = false; find.waiting = 0; find.status = tr(L"Wpisz tytuł gry."); InvalidateRect(hwnd_, nullptr, FALSE); return; }
    metadata_sources::Query query;
    query.title = title;
    query.polish = i18n::language() == i18n::Language::pl;
    query.switch_id = metadata_sources::titleId(title);
    const auto& game = desktop_meta_game_;
    if (title == find.seed) { // The game's own ids: its file name, its store.
        if (query.switch_id.empty() && !game.rom.empty()) query.switch_id = metadata_sources::titleId(game.rom.filename().wstring());
        if (query.switch_id.empty() && !game.rom.empty()) query.switch_id = metadata_sources::titleId(game.rom.parent_path().filename().wstring());
        if (query.switch_id.empty()) query.switch_id = metadata_sources::titleId(game.name);
        query.steam_app = game.app_id;
        if (game.source == GameSource::gog) query.gog_id = game.source_id;
        query.xbox_product = game.xbox_product;
    }
    std::vector<metadata_sources::Source> sources;
    for (const auto source : metadata_sources::all) if (metadata_sources::applicable(source, query)) sources.push_back(source);
    // The databases: RAWG, and ScreenScraper for an emulated game when it is set up (never in UI tests: no network).
    const bool rawg = find.databases && runtime_started_;
    const bool scraper = rawg && find.emulated && screenscraper::load().ready();
    find.waiting = static_cast<int>(sources.size()) + (rawg ? 1 : 0) + (scraper ? 1 : 0);
    find.busy = find.waiting > 0;
    find.status = find.busy ? tr(L"Szukanie…") : tr(L"Brak źródeł dla tej gry.");
    const auto generation = find.generation;
    const auto alive = alive_; const auto hwnd = hwnd_;
    const auto deliver = [alive, hwnd, generation](Scored rows, std::wstring error) {
        if (!alive->load()) return;
        auto message = std::make_unique<RemoteMessage>();
        message->run = [generation, rows = std::move(rows), error = std::move(error)](Launcher& app) mutable {
            app.metaFindResults(generation, std::move(rows), std::move(error));
        };
        if (PostMessageW(hwnd, remote_message, 0, reinterpret_cast<LPARAM>(message.get()))) message.release();
    };
    const auto offers = [](metadata_sources::Found found) {
        Scored rows;
        for (auto& offer : found.offers) {
            mf::Result result;
            result.title = std::move(offer.title);
            result.source = metadata_sources::name(offer.source);
            result.note = std::move(offer.note);
            result.image = std::move(offer.image);
            result.values = {offer.details.description, offer.details.developer, offer.details.publisher, offer.details.release_date, offer.details.genres};
            rows.emplace_back(offer.score, std::move(result));
        }
        return rows;
    };
    if (rawg && !work_.start([title, deliver] {
            auto search = rawg::search(title, userSecret(L"RAWG_API_KEY", L"rawg_api_key.txt"));
            Scored rows;
            for (auto& candidate : search.games) {
                mf::Result result;
                result.kind = mf::Result::Kind::rawg;
                result.rawg_id = candidate.id;
                result.title = candidate.name;
                result.source = L"RAWG";
                result.note = yearAnd(candidate.released, candidate.platforms);
                result.image = rawgThumb(std::move(candidate.image));
                rows.emplace_back(metadata_sources::titleScore(title, candidate.name), std::move(result));
            }
            keepMatching(rows);
            deliver(std::move(rows), std::move(search.error));
        }, [deliver](std::exception_ptr) { deliver({}, {}); }))
        deliver({}, {});
    if (scraper && !work_.start([title, game, deliver] {
            namespace ss = screenscraper;
            Scored rows;
            std::wstring error;
            const auto metadata = userConfigDirectory() / L"metadata";
            if (ss::paused(metadata)) error = tr(L"ScreenScraper jest chwilowo niedostępny. Spróbuję później.");
            else {
                HttpBudget budget(20000);
                const auto systems = ss::systemIds(game.platform, game.rom);
                auto found = ss::search(ss::load(), title, systems.empty() ? 0 : systems.front(), i18n::language() == i18n::Language::pl ? L"pl" : L"en");
                switch (found.status) {
                case ss::Status::found:
                case ss::Status::missing:
                    for (auto& info : found.games) {
                        if (rows.size() >= 10) break;
                        mf::Result result;
                        result.kind = mf::Result::Kind::screenscraper;
                        result.title = info.name;
                        result.source = L"ScreenScraper";
                        result.note = yearAnd(info.details.release_date, info.details.developer.empty() ? info.details.publisher : info.details.developer);
                        const int score = metadata_sources::titleScore(title, info.name);
                        result.info = std::move(info);
                        rows.emplace_back(score, std::move(result));
                    }
                    keepMatching(rows);
                    break;
                case ss::Status::rejected:
                    ss::pause(metadata, 60 * 60 * 1000);
                    error = tr(L"ScreenScraper odrzucił dane dewelopera (devid/devpassword) lub konto. Sprawdź Ustawienia → Biblioteka."); break;
                case ss::Status::quota:
                    ss::pause(metadata, 12ull * 60 * 60 * 1000);
                    error = tr(L"Dzienny limit zapytań ScreenScraper wyczerpany. Spróbuję jutro."); break;
                case ss::Status::throttled: ss::pause(metadata, 2 * 60 * 1000); [[fallthrough]];
                default: error = tr(L"ScreenScraper jest chwilowo niedostępny. Spróbuję później.");
                }
            }
            deliver(std::move(rows), std::move(error));
        }, [deliver](std::exception_ptr) { deliver({}, {}); }))
        deliver({}, {});
    for (const auto source : sources) {
        if (!runtime_started_) { // UI tests: answered at once, by the injected transport.
            metaFindResults(generation, offers(metadata_sources::find(source, query, metadata_find_get_)), {});
            continue;
        }
        const bool started = work_.start([source, query, get = metadata_find_get_, deliver, offers] { deliver(offers(metadata_sources::find(source, query, get)), {}); },
                                         [deliver](std::exception_ptr) { deliver({}, {}); });
        if (!started) deliver({}, {});
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

// One source's answer, merged by score (the older answer stays above an equal one); the list keeps the best.
void Launcher::metaFindResults(uint32_t generation, std::vector<std::pair<int, meta_find::Result>> rows, std::wstring error) {
    auto& find = desktop_meta_.find;
    if (!desktop_meta_.open || !find.open || generation != find.generation) return;
    const mf::Result* kept = find.focus >= mf::item::result_first ? find.current() : nullptr; // The player is already choosing.
    const std::wstring kept_title = kept ? kept->title : std::wstring{}, kept_source = kept ? kept->source : std::wstring{};
    for (auto& [score, result] : rows) {
        size_t at = 0;
        while (at < find.scores.size() && find.scores[at] >= score) ++at;
        find.scores.insert(find.scores.begin() + static_cast<std::ptrdiff_t>(at), score);
        find.results.insert(find.results.begin() + static_cast<std::ptrdiff_t>(at), std::move(result));
    }
    if (static_cast<int>(find.results.size()) > mf::visible) { find.results.resize(mf::visible); find.scores.resize(mf::visible); }
    if (find.waiting > 0) --find.waiting;
    find.busy = find.waiting > 0;
    if (!error.empty()) find.errors.push_back(std::move(error));
    int index = 0;
    if (kept)
        for (int i = 0; i < static_cast<int>(find.results.size()); ++i)
            if (find.results[static_cast<size_t>(i)].title == kept_title && find.results[static_cast<size_t>(i)].source == kept_source) { index = i; break; }
    if (!kept || index != find.chosen || !find.has(find.chosen)) metaFindChoose(index);
    if (!find.results.empty()) find.status.clear();
    else if (find.busy) find.status = tr(L"Szukanie…");
    else find.status = !find.errors.empty() ? find.errors.front() : std::wstring(tr(L"Nic nie znaleziono. Zmień tytuł i szukaj ponownie."));
    metaFindThumbs();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

// The pictures of the rows shown, each asked for once.
void Launcher::metaFindThumbs() {
    auto& find = desktop_meta_.find;
    if (!runtime_started_) return;
    for (const auto& result : find.results) {
        if (result.image.empty() || result.thumb || std::find(find.fetching.begin(), find.fetching.end(), result.image) != find.fetching.end()) continue;
        find.fetching.push_back(result.image);
        const auto alive = alive_; const auto hwnd = hwnd_; const auto generation = find.generation;
        work_.start([url = result.image, get = metadata_find_get_, alive, hwnd, generation] {
            auto response = get(url, L"Accept: image/*\r\n", size_t{6} << 20);
            if (response.status != 200 || response.body.empty() || response.truncated || !alive->load()) return;
            auto message = std::make_unique<RemoteMessage>();
            message->run = [generation, url, bytes = shareImage(std::move(response.body))](Launcher& app) { app.metaFindThumb(generation, url, bytes); };
            if (PostMessageW(hwnd, remote_message, 0, reinterpret_cast<LPARAM>(message.get()))) message.release();
        });
    }
}
void Launcher::metaFindThumb(uint32_t generation, const std::wstring& url, ImageBytes bytes) {
    auto& find = desktop_meta_.find;
    if (!desktop_meta_.open || !find.open || find.generation != generation) return;
    for (auto& result : find.results) if (result.image == url) result.thumb = bytes;
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

// The primary button: a database's game identifies the game; a store's ticked details go into the fields.
void Launcher::metaFindApply() {
    auto& find = desktop_meta_.find;
    const auto* result = find.current();
    if (!desktop_meta_.open || !find.open || !result || !find.canApply()) return;
    if (!result->database()) { metaFindInsert(); return; }
    find.applying = true;
    find.status = tr(L"Pobieranie metadanych i brakujących grafik…");
    if (!desktop_mode_) audio_.navigate();
    if (result->kind == mf::Result::Kind::screenscraper && result->info) startIdentification(desktop_meta_game_, 0, *result->info);
    else startIdentification(desktop_meta_game_, result->rawg_id, std::nullopt);
    if (!runtime_started_) find.applying = false; // UI tests: nothing answers.
    InvalidateRect(hwnd_, nullptr, FALSE);
}

// The identification saved: the game takes it everywhere. Opened for it, the dialog closes; else the
// fields the player did not change show the identified game's details.
void Launcher::metaFindIdentified(IdentifyMessage& message) {
    auto& meta = desktop_meta_;
    auto& find = meta.find;
    find.applying = false;
    if (!message.picked && !message.metadata) {
        find.status = message.error.empty() ? std::wstring(tr(L"Nie udało się zidentyfikować gry.")) : message.error;
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    const auto game = desktop_meta_game_;
    const std::wstring name = message.picked ? message.picked->name : message.metadata->name;
    identifyApplied(game, message);
    audio_.context();
    showNotification(NotificationKind::launch, tr(L"Gra zidentyfikowana"), name);
    if (find.direct) { desktopMetaClose(); return; }
    metaFindClose();
    desktop_details_key_.clear(); // The details are read again.
    bool loading = false;
    const auto details = desktopDetails(game, loading);
    const std::array<const std::wstring*, mf::take_count> fresh{&details.description, &details.developer, &details.publisher, &details.release_date,
                                                                &details.genres};
    for (size_t slot = 0; slot < fresh.size(); ++slot) {
        const auto field = static_cast<size_t>(mf::target_field[slot]);
        if (meta.fields[field] == meta.shown[field]) meta.fields[field] = meta.shown[field] = *fresh[slot]; // Unchanged: the new automatic value.
    }
    meta.identity = identityLabel(game);
    meta.focus = static_cast<int>(MetaItem::save);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::metaFindActivate(int item) {
    auto& find = desktop_meta_.find;
    if (item == mf::item::search) metaFindSearch();
    else if (isResult(item)) {
        metaFindChoose(item - mf::item::result_first);
        if (find.canApply()) find.focus = mf::item::insert;
    } else if (isTake(item)) {
        const int slot = item - mf::item::take_first;
        if (find.canTake(slot)) find.take[static_cast<size_t>(slot)] = !find.take[static_cast<size_t>(slot)];
        if (!find.canApply() && find.focus == mf::item::insert) find.focus = mf::item::back;
    } else if (item == mf::item::insert) metaFindApply();
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
    if (find.applying) return true; // The identification is on its way.
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
        // Down onto the buttons: the primary one (Zidentyfikuj, Wstaw) when it can act.
        if (next != row && next + 1 == static_cast<int>(rows.size()) && line.back() == mf::item::insert) { focus(mf::item::insert); return true; }
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
        if (isResult(find.focus) && key == VK_RIGHT) { // The list → the chosen game's details on the right.
            int next = find.canApply() ? mf::item::insert : -1;
            for (int i = 0; i < mf::take_count; ++i) if (find.canTake(i)) { next = mf::item::take_first + i; break; }
            if (next >= 0) { focus(next); return true; }
        }
        if (isTake(find.focus) && key == VK_LEFT) { focus(mf::item::result_first + find.chosen); return true; }
        {
            const auto& line = rows[static_cast<size_t>(row)];
            focus(line[static_cast<size_t>(std::clamp(column + (key == VK_RIGHT ? 1 : -1), 0, static_cast<int>(line.size()) - 1))]);
        }
        return true;
    case VK_RETURN:
        if (control && find.canApply()) { metaFindApply(); return true; }
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
    const mf::Layout layout{view.width, view.height, desktop_meta_.pad};
    if (find.applying) return true;
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
    if (isResult(hit)) { // One click chooses; a double click applies it (Zidentyfikuj, or Wstaw).
        metaFindChoose(hit - mf::item::result_first);
        find.focus = hit;
        if (double_click) metaFindApply();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return true;
    }
    find.focus = hit;
    metaFindActivate(hit);
    return true;
}
