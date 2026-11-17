// PS3 and PS4 trophies from the emulators' own files: RPCS3 (TROPCONF.SFM, TROPUSR.DAT and the
// TROPnnn.PNG icons in its trophy folder) and shadPS4 (TROP.XML with the unlock state and time).
#include "achievements.hpp"
#include "../../src/platform/text_encoding.hpp"

#include <algorithm>
#include <charconv>
#include <fstream>
#include <map>

namespace achievements {
namespace {
std::string readText(const std::filesystem::path& file, size_t limit = 8 * 1024 * 1024) {
    std::error_code ec;
    const auto size = std::filesystem::file_size(file, ec);
    if (ec || !size || size > limit) return {};
    std::string out(static_cast<size_t>(size), '\0');
    std::ifstream in(file, std::ios::binary);
    if (!in.read(out.data(), static_cast<std::streamsize>(out.size()))) return {};
    return out;
}
std::string unescape(std::string_view text) {
    std::string out;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '&') { out += text[i]; continue; }
        const auto end = text.find(';', i);
        if (end == std::string_view::npos || end - i > 8) { out += '&'; continue; }
        const auto entity = text.substr(i + 1, end - i - 1);
        if (entity == "amp") out += '&'; else if (entity == "lt") out += '<'; else if (entity == "gt") out += '>';
        else if (entity == "quot") out += '"'; else if (entity == "apos") out += '\'';
        else if (entity.starts_with("#")) {
            uint32_t code = 0;
            const bool hex = entity.size() > 1 && (entity[1] == 'x' || entity[1] == 'X');
            std::from_chars(entity.data() + (hex ? 2 : 1), entity.data() + entity.size(), code, hex ? 16 : 10);
            if (code && code < 0x110000) out += text_encoding::utf8Lenient(std::wstring(1, static_cast<wchar_t>(code < 0x10000 ? code : L'?')));
        } else { out += '&'; continue; }
        i = end;
    }
    return out;
}
std::string attribute(std::string_view tag, std::string_view name) {
    for (size_t at = tag.find(name); at != std::string_view::npos; at = tag.find(name, at + 1)) {
        if (at && tag[at - 1] != ' ' && tag[at - 1] != '\t' && tag[at - 1] != '\n') continue;
        auto rest = tag.substr(at + name.size());
        while (!rest.empty() && rest.front() == ' ') rest.remove_prefix(1);
        if (rest.empty() || rest.front() != '=') continue;
        rest.remove_prefix(1);
        while (!rest.empty() && rest.front() == ' ') rest.remove_prefix(1);
        if (rest.empty() || (rest.front() != '"' && rest.front() != '\'')) continue;
        const auto quote = rest.front();
        const auto end = rest.find(quote, 1);
        if (end == std::string_view::npos) return {};
        return unescape(rest.substr(1, end - 1));
    }
    return {};
}
std::string element(std::string_view body, std::string_view name) {
    const auto open = "<" + std::string(name);
    const auto start = body.find(open);
    if (start == std::string_view::npos) return {};
    const auto content = body.find('>', start);
    const auto close = body.find("</" + std::string(name) + ">", content);
    if (content == std::string_view::npos || close == std::string_view::npos) return {};
    return unescape(body.substr(content + 1, close - content - 1));
}
uint32_t be32(const std::vector<uint8_t>& b, size_t at) {
    return at + 4 <= b.size() ? (static_cast<uint32_t>(b[at]) << 24) | (b[at + 1] << 16) | (b[at + 2] << 8) | b[at + 3] : 0;
}
uint64_t be64(const std::vector<uint8_t>& b, size_t at) { return (static_cast<uint64_t>(be32(b, at)) << 32) | be32(b, at + 4); }
std::string threeDigits(const std::string& id) {
    uint32_t number = 0;
    if (std::from_chars(id.data(), id.data() + id.size(), number).ec != std::errc{}) return {};
    char text[8]{};
    snprintf(text, sizeof(text), "%03u", number);
    return text;
}
bool communicationId(const std::wstring& name) {
    // NPWR01234_00 (and the NPWS/NPWT variants).
    if (name.size() != 12 || name.compare(0, 3, L"NPW") != 0 || name[9] != L'_') return false;
    for (size_t i = 4; i < 9; ++i) if (name[i] < L'0' || name[i] > L'9') return false;
    return name[10] >= L'0' && name[10] <= L'9' && name[11] >= L'0' && name[11] <= L'9';
}
// Every <trophy …>…</trophy> in a TROPCONF.SFM / TROP.XML, in file order.
template <typename Each> void trophies(std::string_view xml, Each each) {
    for (size_t at = xml.find("<trophy "); at != std::string_view::npos; at = xml.find("<trophy ", at + 8)) {
        const auto open_end = xml.find('>', at);
        const auto close = xml.find("</trophy>", at);
        if (open_end == std::string_view::npos || close == std::string_view::npos || close < open_end) break;
        each(xml.substr(at, open_end - at), xml.substr(open_end + 1, close - open_end - 1));
    }
}
}

std::string ps3CommunicationId(const std::filesystem::path& rom, const std::filesystem::path& install) {
    std::error_code ec;
    if (!rom.empty() && std::filesystem::is_regular_file(rom, ec)) {
        auto ext = rom.extension().wstring();
        for (auto& ch : ext) if (ch >= L'A' && ch <= L'Z') ch = static_cast<wchar_t>(ch - L'A' + L'a');
        if (ext == L".iso") {
            // ISO 9660 path tables and PS3_GAME/TROPDIR/NPWRxxxxx_00 directory records sit near the front of the image.
            std::ifstream in(rom, std::ios::binary);
            std::string head(2 * 1024 * 1024, '\0');
            in.read(head.data(), static_cast<std::streamsize>(head.size()));
            head.resize(static_cast<size_t>(in.gcount()));
            for (size_t at = head.find("NPW"); at != std::string::npos && at + 12 <= head.size(); at = head.find("NPW", at + 1)) {
                if (head[at + 3] >= 'A' && head[at + 3] <= 'Z' &&
                    communicationId(text_encoding::wideLenient(std::string_view(head).substr(at, 12))))
                    return head.substr(at, 12);
            }
        }
    }
    for (const auto& start : {rom, install}) {
        if (start.empty()) continue;
        auto folder = std::filesystem::is_directory(start, ec) ? start : start.parent_path();
        for (int level = 0; level < 5 && !folder.empty(); ++level) {
            for (const auto& candidate : {folder / L"TROPDIR", folder / L"PS3_GAME" / L"TROPDIR"}) {
                for (const auto& entry : std::filesystem::directory_iterator(candidate, ec)) {
                    const auto name = entry.path().filename().wstring();
                    if (entry.is_directory(ec) && communicationId(name)) return text_encoding::utf8Lenient(name);
                }
                ec.clear();
            }
            if (folder == folder.parent_path()) break;
            folder = folder.parent_path();
        }
    }
    return {};
}

std::optional<std::filesystem::path> rpcs3TrophyFolder(const std::filesystem::path& rpcs3, const std::string& npcommid) {
    if (rpcs3.empty() || npcommid.empty()) return {};
    std::error_code ec;
    const auto id = text_encoding::wideLenient(npcommid);
    std::vector<std::filesystem::path> candidates{rpcs3 / L"trophy" / id};
    for (const auto& user : std::filesystem::directory_iterator(rpcs3 / L"dev_hdd0" / L"home", ec)) candidates.push_back(user.path() / L"trophy" / id);
    std::sort(candidates.begin() + 1, candidates.end()); // 00000001 first.
    for (const auto& folder : candidates) if (std::filesystem::is_regular_file(folder / L"TROPCONF.SFM", ec)) return folder;
    return {};
}

std::optional<std::vector<std::pair<uint32_t, std::optional<uint64_t>>>> parseTropusr(const std::vector<uint8_t>& bytes) {
    // Big-endian: a 48-byte header (magic 818F54AD, table count), 32-byte table headers
    // {type, entry size, -, count, offset}; table 6 holds one 0x60 entry per trophy:
    // {6, 0x60, entry, -, trophy id, state (1: unlocked), -, -, time (µs since 0001-01-01)…}.
    if (be32(bytes, 0) != 0x818F54AD) return {};
    const auto tables = be32(bytes, 8);
    if (!tables || tables > 64) return {};
    std::vector<std::pair<uint32_t, std::optional<uint64_t>>> out;
    for (uint32_t t = 0; t < tables; ++t) {
        const size_t header = 48 + static_cast<size_t>(t) * 32;
        if (header + 32 > bytes.size()) return {};
        if (be32(bytes, header) != 6) continue;
        const auto size = be32(bytes, header + 4), count = be32(bytes, header + 12);
        const auto offset = be64(bytes, header + 16);
        if (size < 0x30 || count > 4096 || offset + static_cast<uint64_t>(size) * count > bytes.size()) return {};
        for (uint32_t i = 0; i < count; ++i) {
            const size_t entry = static_cast<size_t>(offset) + static_cast<size_t>(i) * size;
            if (be32(bytes, entry) != 6) continue;
            const auto id = be32(bytes, entry + 16), state = be32(bytes, entry + 20);
            if (state != 1) { out.emplace_back(id, std::nullopt); continue; }
            const auto ticks = be64(bytes, entry + 32);
            constexpr uint64_t unix_epoch = 62135596800ULL * 1000000ULL; // 0001-01-01 → 1970-01-01, in µs.
            out.emplace_back(id, ticks > unix_epoch ? (ticks - unix_epoch) / 1000000ULL : 0);
        }
        return out;
    }
    return out;
}

std::optional<List> readRpcs3(const std::filesystem::path& folder) {
    const auto config = readText(folder / L"TROPCONF.SFM");
    if (config.empty()) return {};
    std::map<uint32_t, std::optional<uint64_t>> state;
    {
        const auto raw = readText(folder / L"TROPUSR.DAT");
        if (const auto parsed = parseTropusr(std::vector<uint8_t>(raw.begin(), raw.end()))) for (const auto& [id, at] : *parsed) state[id] = at;
    }
    List list; list.source = "RPCS3";
    trophies(config, [&](std::string_view tag, std::string_view body) {
        Item item;
        item.id = attribute(tag, "id");
        uint32_t number = 0;
        if (item.id.empty() || std::from_chars(item.id.data(), item.id.data() + item.id.size(), number).ec != std::errc{}) return;
        item.id = std::to_string(number);
        item.name = element(body, "name"); item.description = element(body, "detail");
        item.hidden = attribute(tag, "hidden") == "yes";
        if (const auto found = state.find(number); found != state.end() && found->second) { item.achieved = true; item.unlocked_at = *found->second; }
        const auto icon = folder / text_encoding::wideLenient("TROP" + threeDigits(item.id) + ".PNG");
        std::error_code ec;
        if (std::filesystem::is_regular_file(icon, ec)) item.icon = text_encoding::utf8Lenient(icon.wstring());
        list.items.push_back(std::move(item));
    });
    if (list.items.empty()) return {};
    return list;
}

std::optional<List> parseTropXml(std::string_view xml, const std::filesystem::path& icons) {
    List list; list.source = "shadPS4";
    trophies(xml, [&](std::string_view tag, std::string_view body) {
        Item item;
        item.id = attribute(tag, "id");
        uint32_t number = 0;
        if (item.id.empty() || std::from_chars(item.id.data(), item.id.data() + item.id.size(), number).ec != std::errc{}) return;
        item.id = std::to_string(number);
        item.name = element(body, "name"); item.description = element(body, "detail");
        item.hidden = attribute(tag, "hidden") == "yes";
        const auto unlocked = attribute(tag, "unlockstate");
        item.achieved = unlocked == "true" || unlocked == "1";
        if (item.achieved) {
            const auto stamp = attribute(tag, "timestamp");
            uint64_t value = 0;
            if (std::from_chars(stamp.data(), stamp.data() + stamp.size(), value).ec == std::errc{})
                item.unlocked_at = value > 100000000000ULL ? value / 1000 : value; // Milliseconds in newer builds.
        }
        const auto icon = icons / text_encoding::wideLenient("TROP" + threeDigits(item.id) + ".PNG");
        std::error_code ec;
        if (!icons.empty() && std::filesystem::is_regular_file(icon, ec)) item.icon = text_encoding::utf8Lenient(icon.wstring());
        list.items.push_back(std::move(item));
    });
    if (list.items.empty()) return {};
    return list;
}

std::optional<List> readShadps4(const std::filesystem::path& shadps4, const GameInfo& game) {
    if (shadps4.empty()) return {};
    std::error_code ec;
    // The game's title ID (CUSA01234) from its folder, else its title in each TROP.XML.
    std::wstring title_id;
    for (const auto& path : {game.rom, game.install_directory, game.executable}) {
        const auto text = path.wstring();
        for (size_t at = text.find(L"CUSA"); at != std::wstring::npos && title_id.empty(); at = text.find(L"CUSA", at + 4))
            if (at + 9 <= text.size() && std::all_of(text.begin() + at + 4, text.begin() + at + 9, [](wchar_t c) { return c >= L'0' && c <= L'9'; }))
                title_id = text.substr(at, 9);
    }
    for (const auto& root : {shadps4 / L"user" / L"game_data", shadps4 / L"launcher" / L"game_data", shadps4 / L"shadPS4" / L"game_data", shadps4 / L"game_data"}) {
        for (const auto& entry : std::filesystem::directory_iterator(root, ec)) {
            if (!entry.is_directory(ec)) continue;
            const auto trophy = entry.path() / L"trophyfiles" / L"trophy00";
            const auto xml = readText(trophy / L"Xml" / L"TROP.XML");
            if (xml.empty()) continue;
            const bool match = !title_id.empty() ? entry.path().filename().wstring() == title_id :
                text_encoding::wideLenient(element(xml, "title-name")) == text_encoding::wideLenient(game.name);
            if (match) return parseTropXml(xml, trophy / L"Icons");
        }
        ec.clear();
    }
    return {};
}
}
