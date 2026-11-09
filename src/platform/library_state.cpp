#include "library_state.hpp"
#include "atomic_file.hpp"
#include "text_encoding.hpp"
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <cmath>

namespace {
std::string encoded(std::string_view value) { return key_codec::encode(value); }
bool validField(std::string_view value) {
    return value.size() <= 1024 && value.find_first_of("\r\n\0", 0, 3) == std::string_view::npos;
}
// Wyklucz grę: any store's game, a scanned DRM-free one, an emulated game or a Steam shortcut (gameKey).
bool exclusionKey(std::string_view key) {
    for (const std::string_view prefix : {"steam:", "gog:", "epic:", "battlenet:", "ea:", "ubisoft:", "amazon:", "xbox:", "multimc:", "drmfree:", "shortcut:"})
        if (key.starts_with(prefix) && key.size() > prefix.size()) return true;
    return false;
}
bool endOfRow(std::istream& row) { row >> std::ws; return row.eof() && !row.bad(); }
bool field(std::istream& input, std::string& value) {
    std::string token;
    if (!(input >> std::quoted(token)) || token.size() > 2048) return false;
    const auto decoded = key_codec::decode(token);
    if (!decoded || decoded->find_first_of("\r\n\0", 0, 3) != std::string::npos) return false;
    value = *decoded; return true;
}
}
namespace {
// One row into `result`; false when it is malformed or of an unknown kind.
bool parseRow(const std::string& line, LibraryState& result) {
        std::istringstream row(line); row.imbue(std::locale::classic());
        std::string kind; if (!(row >> kind)) return true;
        if (kind == "bookmark") {
            LibraryBookmark bookmark; int place = 0; std::string platform;
            if (!(row >> bookmark.account >> place && place >= 0 && place <= 2 && field(row, bookmark.category) &&
                field(row, bookmark.selection) && field(row, bookmark.source) && field(row, platform) && row >> bookmark.scroll &&
                std::isfinite(bookmark.scroll) && bookmark.scroll >= 0)) return {};
            bookmark.place = static_cast<LibraryPlace>(place); bookmark.platform = text_encoding::wide(platform);
            if (!platform.empty() && bookmark.platform.empty()) return {};
            if (!endOfRow(row) && !field(row, bookmark.gog_account)) return {};
            if (!endOfRow(row) && !field(row, bookmark.epic_account)) return {};
            result.bookmark = std::move(bookmark);
        } else if (kind == "option") {
            std::string scope; int order = 0, installed = 0, view = 0;
            if (!(field(row, scope) && row >> order >> installed && order >= 0 && order < library_view::persisted_order_count && (installed == 0 || installed == 1))) return {};
            // The view column was added later; older files have none (grid). Removed views read as their nearest one.
            if (!endOfRow(row) && (!(row >> view) || view < 0 || view >= library_view::persisted_view_count)) return {};
            result.options[scope] = {library_view::orderFromPersisted(order), installed != 0, library_view::viewFromPersisted(view)};
            if (!endOfRow(row)) {
                int poster = 0, year = 0, time = 0, rating = 0, horizontal = 0, columns = 0, cd = 0;
                const auto bit = [](int value) { return value == 0 || value == 1; };
                if (!(row >> poster >> year >> time >> rating >> horizontal >> columns >> cd) || !bit(poster) || !bit(year) ||
                    !bit(time) || !bit(rating) || !bit(horizontal) || columns < 2 || columns > 5 || !bit(cd)) return {};
                auto& options = result.options[scope];
                options.grid_posters = poster != 0; options.list_year = year != 0; options.list_playtime = time != 0;
                options.list_rating = rating != 0; options.cinematic_horizontal = horizontal != 0;
                options.overview_columns = columns; options.coverflow_cd = cd != 0;
                // Lista+'s steelbook, appended later: older rows end before it (off).
                if (!endOfRow(row)) {
                    int steelbook = 0;
                    if (!(row >> steelbook) || !bit(steelbook)) return {};
                    options.list_plus_steelbook = steelbook != 0;
                }
                // Siatka's landscape tiles and Kinowy's column and facts, appended after that (off).
                if (!endOfRow(row)) {
                    int landscape = 0, column = 0, platform = 0, played = 0, playtime = 0;
                    if (!(row >> landscape >> column >> platform >> played >> playtime) || !bit(landscape) || !bit(column) ||
                        !bit(platform) || !bit(played) || !bit(playtime)) return {};
                    options.grid_landscape = landscape != 0 && !options.grid_posters;
                    options.cinematic_column = column != 0 && !options.cinematic_horizontal;
                    options.cinematic_platform = platform != 0; options.cinematic_played = played != 0; options.cinematic_playtime = playtime != 0;
                }
                // Półka's grey side cases and platform band, appended after that (on when absent).
                if (!endOfRow(row)) {
                    int gray = 1, platform = 1;
                    if (!(row >> gray >> platform) || !bit(gray) || !bit(platform)) return {};
                    options.bookshelf_gray = gray != 0; options.bookshelf_platform = platform != 0;
                }
            }
        } else if (kind == "collection-name") {
            std::string scope, name;
            if (!field(row, scope) || !field(row, name) || scope.empty() || name.empty()) return {};
            auto wide = text_encoding::wide(name);
            if (wide.empty() || wide.size() > 200) return {};
            result.collections.names[scope] = std::move(wide);
        } else if (kind == "collection-member") {
            std::string scope, key; int member = 0;
            if (!field(row, scope) || !field(row, key) || !(row >> member) || scope.empty() || key.empty() || (member != 0 && member != 1)) return {};
            result.collections.members[scope][key] = member != 0;
        } else if (kind == "collection-create") {
            std::string scope;
            if (!field(row, scope) || scope.find(":user-collections.uc-launchme-") == scope.npos) return {};
            result.collections.creates.insert(std::move(scope));
        } else if (kind == "preferred-version") {
            std::string group, key;
            if (!field(row, group) || !field(row, key) || group.empty() || key.empty()) return {};
            result.preferred_versions[group] = key;
        } else if (kind == "hidden") {
            std::string key;
            if (!field(row, key) || key.empty()) return {};
            result.hidden.insert(std::move(key));
        } else if (kind == "steam-hidden-pending") {
            std::string key; int hidden = 0;
            if (!field(row, key) || !key.starts_with("steam-user:") || key.find("/steam:") == key.npos ||
                !(row >> hidden) || (hidden != 0 && hidden != 1)) return {};
            result.steam_hidden_pending[key] = hidden != 0;
        } else if (kind == "steam-unsynced") {
            std::string key; int value = 0;
            if (!field(row, key) || key.empty() || !(row >> value) || (value != 0 && value != 1)) return {};
            result.steam_unsynced[key] = value != 0;
        } else if (kind == "excluded-game") {
            std::string key, name;
            if (!field(row, key) || !exclusionKey(key) || !field(row, name)) return {};
            auto title = text_encoding::wide(name);
            if ((!name.empty() && title.empty()) || title.size() > 200) return {};
            result.excluded_games[std::move(key)] = std::move(title);
        } else if (kind == "game-emulator") {
            std::string key, id, program;
            if (!field(row, key) || !field(row, id) || !field(row, program) || key.empty() || (id.empty() == program.empty()) || id.size() > 32) return {};
            auto path = text_encoding::wide(program);
            if (!program.empty() && path.empty()) return {};
            result.game_emulators[std::move(key)] = {std::move(id), std::move(path)};
        } else if (kind == "launch-arguments") {
            std::string key, options;
            if (!field(row, key) || !field(row, options) || key.empty() || options.empty()) return {};
            auto text = text_encoding::wide(options);
            if (text.empty()) return {};
            result.launch_arguments[std::move(key)] = std::move(text);
        } else if (kind == "launch-flags") {
            std::string key; unsigned bits = 0;
            if (!field(row, key) || key.empty() || !(row >> bits) || !bits || (bits & ~launch_flag::all)) return {};
            result.launch_flags[std::move(key)] = bits;
        } else if (kind == "recent-hidden") {
            std::string key; uint64_t time = 0;
            if (!(field(row, key) && row >> time && !key.empty())) return {};
            result.recent_hidden[std::move(key)] = time;
        } else if (kind == "seen" || kind == "played" || kind == "playtime-ms") {
            std::string key; uint64_t time = 0;
            if (!(field(row, key) && row >> time && !key.empty())) return {};
            (kind == "seen" ? result.first_seen : kind == "played" ? result.last_played : result.playtime_ms).emplace(std::move(key), time);
        } else return {};
        return endOfRow(row);
}
enum class Read { ok, missing, unreadable, malformed };
Read readState(const std::filesystem::path& file, bool lenient, LibraryState& result, size_t& skipped) {
    std::error_code error;
    if (!std::filesystem::exists(file, error)) return error ? Read::unreadable : Read::missing;
    std::ifstream input(file, std::ios::binary | std::ios::ate);
    if (!input || input.tellg() < 0) return Read::unreadable;
    if (input.tellg() > static_cast<std::streamoff>(max_library_state_bytes)) return Read::malformed;
    input.seekg(0); input.imbue(std::locale::classic());
    std::string line;
    if (!std::getline(input, line)) return Read::malformed;
    std::istringstream first(line); first.imbue(std::locale::classic());
    std::string header; int version = 0;
    // A newer version may add rows: salvage keeps the ones this build understands.
    if (!(first >> header >> version) || header != "launchme-library" || version < 1 || (version != 1 && !lenient) || !endOfRow(first)) return Read::malformed;
    while (std::getline(input, line)) {
        // Salvage: a bad row is skipped (one that failed only at its end may still have applied).
        if (parseRow(line, result)) continue;
        if (!lenient) return Read::malformed;
        ++skipped;
    }
    if (input.bad() || !input.eof()) return lenient ? Read::ok : Read::malformed;
    return Read::ok;
}
}
std::optional<LibraryState> loadLibraryState(const std::filesystem::path& file) {
    LibraryState result; size_t skipped = 0;
    const auto read = readState(file, false, result, skipped);
    if (read == Read::missing) return LibraryState{};
    if (read != Read::ok) return {};
    return result;
}
std::optional<SalvagedLibraryState> salvageLibraryState(const std::filesystem::path& file) {
    SalvagedLibraryState salvaged;
    const auto read = readState(file, true, salvaged.state, salvaged.skipped_rows);
    if (read == Read::unreadable) return {};
    if (read == Read::malformed) salvaged.skipped_rows = std::max<size_t>(salvaged.skipped_rows, 1);
    return salvaged;
}
bool saveLibraryState(const LibraryState& state, const std::filesystem::path& file) {
    std::ostringstream output; output.imbue(std::locale::classic()); output.precision(9);
    output << "launchme-library 1\n";
    if (state.bookmark) {
        const auto& b = *state.bookmark;
        const auto platform = text_encoding::utf8(b.platform);
        if (static_cast<int>(b.place) < 0 || static_cast<int>(b.place) > 2 || !std::isfinite(b.scroll) || b.scroll < 0 ||
            !validField(b.category) || !validField(b.selection) || !validField(b.source) || !validField(platform) ||
            (!b.platform.empty() && platform.empty()) || !validField(b.gog_account) || !validField(b.epic_account)) return false;
        output << "bookmark " << b.account << ' ' << static_cast<int>(b.place) << ' ' << std::quoted(encoded(b.category)) << ' '
            << std::quoted(encoded(b.selection)) << ' ' << std::quoted(encoded(b.source)) << ' ' << std::quoted(encoded(platform)) << ' ' << b.scroll << ' ' << std::quoted(encoded(b.gog_account)) << ' ' << std::quoted(encoded(b.epic_account)) << '\n';
    }
    const auto bounded = [&] { return output.good() && output.tellp() <= static_cast<std::streamoff>(max_library_state_bytes); };
    for (const auto& [scope, options] : state.options) {
        if (!validField(scope) || static_cast<int>(options.order) < 0 || static_cast<int>(options.order) >= library_view::order_count ||
            static_cast<int>(options.view) < 0 || static_cast<int>(options.view) >= library_view::persisted_view_count ||
            library_view::viewFromPersisted(static_cast<int>(options.view)) != options.view ||
            options.overview_columns < 2 || options.overview_columns > 5) return false;
        output << "option " << std::quoted(encoded(scope)) << ' ' << library_view::persistedOrder(options.order) << ' ' << options.installed_only
               << ' ' << static_cast<int>(options.view) << ' ' << options.grid_posters << ' ' << options.list_year << ' '
               << options.list_playtime << ' ' << options.list_rating << ' ' << options.cinematic_horizontal << ' '
               << options.overview_columns << ' ' << options.coverflow_cd << ' ' << options.list_plus_steelbook << ' '
               << options.grid_landscape << ' ' << options.cinematic_column << ' ' << options.cinematic_platform << ' '
               << options.cinematic_played << ' ' << options.cinematic_playtime << ' ' << options.bookshelf_gray << ' ' << options.bookshelf_platform << '\n';
        if (!bounded()) return false;
    }
    for (const auto* history : {&state.first_seen, &state.last_played, &state.playtime_ms}) for (const auto& [key, time] : *history) {
        if (key.empty() || !validField(key)) return false;
        output << (history == &state.first_seen ? "seen " : history == &state.last_played ? "played " : "playtime-ms ") << std::quoted(encoded(key)) << ' ' << time << '\n';
        if (!bounded()) return false;
    }
    for (const auto& [scope, name] : state.collections.names) {
        const auto text = text_encoding::utf8(name);
        if (scope.empty() || !validField(scope) || name.empty() || name.size() > 200 || text.empty() || !validField(text)) return false;
        output << "collection-name " << std::quoted(encoded(scope)) << ' ' << std::quoted(encoded(text)) << '\n';
        if (!bounded()) return false;
    }
    for (const auto& [scope, members] : state.collections.members) for (const auto& [key, value] : members) {
        if (scope.empty() || key.empty() || !validField(scope) || !validField(key)) return false;
        output << "collection-member " << std::quoted(encoded(scope)) << ' ' << std::quoted(encoded(key)) << ' ' << value << '\n';
        if (!bounded()) return false;
    }
    for (const auto& scope : state.collections.creates) {
        if (!validField(scope) || scope.find(":user-collections.uc-launchme-") == scope.npos) return false;
        output << "collection-create " << std::quoted(encoded(scope)) << '\n';
        if (!bounded()) return false;
    }
    for (const auto& [group, key] : state.preferred_versions) {
        if (group.empty() || key.empty() || !validField(group) || !validField(key)) return false;
        output << "preferred-version " << std::quoted(encoded(group)) << ' ' << std::quoted(encoded(key)) << '\n';
        if (!bounded()) return false;
    }
    for (const auto& key : state.hidden) {
        if (key.empty() || !validField(key)) return false;
        output << "hidden " << std::quoted(encoded(key)) << '\n';
        if (!bounded()) return false;
    }
    for (const auto& [key, name] : state.excluded_games) {
        const auto text = text_encoding::utf8(name);
        if (!exclusionKey(key) || !validField(key) || name.size() > 200 || !validField(text) || (!name.empty() && text.empty())) return false;
        output << "excluded-game " << std::quoted(encoded(key)) << ' ' << std::quoted(encoded(text)) << '\n';
        if (!bounded()) return false;
    }
    for (const auto& [key, emulator] : state.game_emulators) {
        const auto program = text_encoding::utf8(emulator.program);
        if (key.empty() || !validField(key) || emulator.id.empty() == program.empty() || !validField(emulator.id) || !validField(program)) return false;
        output << "game-emulator " << std::quoted(encoded(key)) << ' ' << std::quoted(encoded(emulator.id)) << ' ' << std::quoted(encoded(program)) << '\n';
        if (!bounded()) return false;
    }
    for (const auto& [key, options] : state.launch_arguments) {
        const auto text = text_encoding::utf8(options);
        if (key.empty() || !validField(key) || text.empty() || !validField(text)) return false;
        output << "launch-arguments " << std::quoted(encoded(key)) << ' ' << std::quoted(encoded(text)) << '\n';
        if (!bounded()) return false;
    }
    for (const auto& [key, bits] : state.launch_flags) {
        if (key.empty() || !validField(key) || !bits || (bits & ~launch_flag::all)) return false;
        output << "launch-flags " << std::quoted(encoded(key)) << ' ' << bits << '\n';
        if (!bounded()) return false;
    }
    for (const auto& [key, time] : state.recent_hidden) {
        if (key.empty() || !validField(key)) return false;
        output << "recent-hidden " << std::quoted(encoded(key)) << ' ' << time << '\n';
        if (!bounded()) return false;
    }
    for (const auto& [key, hidden] : state.steam_hidden_pending) {
        if (!validField(key) || !key.starts_with("steam-user:") || key.find("/steam:") == key.npos) return false;
        output << "steam-hidden-pending " << std::quoted(encoded(key)) << ' ' << hidden << '\n';
        if (!bounded()) return false;
    }
    for (const auto& [key, value] : state.steam_unsynced) {
        if (key.empty() || !validField(key)) return false;
        output << "steam-unsynced " << std::quoted(encoded(key)) << ' ' << value << '\n';
        if (!bounded()) return false;
    }
    return bounded() && atomic_file::writeDurable(file, std::move(output).str());
}

LibraryStateWriter::LibraryStateWriter(std::filesystem::path file) : file_(std::move(file)), worker_([this] {
    for (;;) {
        std::optional<LibraryState> state;
        {
            std::unique_lock lock(mutex_);
            busy_ = false;
            idle_.notify_all();
            wake_.wait(lock, [&] { return stopping_ || pending_.has_value(); });
            if (!pending_) return;
            state = std::move(pending_); pending_.reset();
            busy_ = true;
        }
        try { result_.store(saveLibraryState(*state, file_) ? 1 : -1); }
        catch (...) { result_.store(-1); } // Preserve the previous file and report the failure to UI.
    }
}) {}
bool LibraryStateWriter::flush(std::chrono::milliseconds timeout) {
    std::unique_lock lock(mutex_);
    return idle_.wait_for(lock, timeout, [&] { return !busy_ && !pending_.has_value(); });
}
LibraryStateWriter::~LibraryStateWriter() {
    { std::lock_guard lock(mutex_); stopping_ = true; }
    wake_.notify_one();
    if (worker_.joinable()) worker_.join();
}
void LibraryStateWriter::save(const LibraryState& state) {
    { std::lock_guard lock(mutex_); pending_ = state; }
    wake_.notify_one();
}
std::optional<bool> LibraryStateWriter::takeResult() {
    const int value = result_.exchange(0);
    return value ? std::optional<bool>(value > 0) : std::nullopt;
}
