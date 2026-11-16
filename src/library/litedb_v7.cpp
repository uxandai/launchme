#include "litedb_v7.hpp"
#include <cstring>
#include <fstream>
#include <set>

namespace litedb_v7 {
namespace {
constexpr size_t header_size = 25; // pageID, type, prev, next, item count, 10 bytes kept by LiteDB.
constexpr uint32_t none = 0xFFFFFFFFu;
enum Type : uint8_t { header_page = 1, collection_page = 2, index_page = 3, data_page = 4, extend_page = 5 };

// A bounds-checked little-endian reader; any read past the end marks it bad and yields zeros.
struct Reader {
    std::string_view bytes;
    size_t at = 0;
    bool bad = false;
    bool need(size_t count) { if (bad || bytes.size() - at < count || at > bytes.size()) { bad = true; return false; } return true; }
    template <typename T> T number() {
        if (!need(sizeof(T))) return T{};
        T value{}; std::memcpy(&value, bytes.data() + at, sizeof(T)); at += sizeof(T); return value;
    }
    uint8_t byte() { return number<uint8_t>(); }
    void skip(size_t count) { if (need(count)) at += count; }
    std::string_view take(size_t count) { if (!need(count)) return {}; const auto out = bytes.substr(at, count); at += count; return out; }
    std::string text() { const auto length = number<int32_t>(); return length < 0 || length > 4096 ? (bad = true, std::string{}) : std::string(take(static_cast<size_t>(length))); }
};

std::string_view page(std::string_view file, uint32_t id) {
    const uint64_t start = static_cast<uint64_t>(id) * page_size;
    if (id == none || start + page_size > file.size()) return {};
    return file.substr(static_cast<size_t>(start), page_size);
}
uint8_t pageType(std::string_view bytes) { return bytes.size() >= header_size ? static_cast<uint8_t>(bytes[4]) : 0; }
uint16_t itemCount(std::string_view bytes) { uint16_t count = 0; if (bytes.size() >= header_size) std::memcpy(&count, bytes.data() + 13, 2); return count; }
uint32_t nextPage(std::string_view bytes) { uint32_t next = none; if (bytes.size() >= header_size) std::memcpy(&next, bytes.data() + 9, 4); return next; }

std::string hexText(std::string_view bytes) {
    constexpr char digits[] = "0123456789abcdef";
    std::string out;
    for (const char c : bytes) { out += digits[static_cast<uint8_t>(c) >> 4]; out += digits[static_cast<uint8_t>(c) & 15]; }
    return out;
}

struct Bson {
    Reader reader;
    int depth = 0;
    bool element(json::Value& parent, bool array) {
        const auto type = reader.byte();
        std::string name;
        while (true) {
            if (!reader.need(1)) return false;
            const char c = reader.bytes[reader.at++];
            if (c == '\0') break;
            name += c;
        }
        json::Value value;
        switch (type) {
        case 0x01: value = json::number(reader.number<double>()); break;
        case 0x02: {
            const auto length = reader.number<int32_t>();
            if (length <= 0) return false;
            value = json::string(std::string(reader.take(static_cast<size_t>(length) - 1)));
            reader.skip(1);
            break;
        }
        case 0x03: case 0x04: if (!document(value, type == 0x04)) return false; break;
        case 0x05: {
            const auto length = reader.number<int32_t>();
            const auto subtype = reader.byte();
            if (length < 0) return false;
            const auto data = reader.take(static_cast<size_t>(length));
            value = json::string(subtype == 0x04 && data.size() == 16 ? guidText(reinterpret_cast<const uint8_t*>(data.data())) : hexText(data));
            break;
        }
        case 0x07: value = json::string(hexText(reader.take(12))); break;
        case 0x08: value = json::boolean(reader.byte() != 0); break;
        case 0x09: value = json::number(static_cast<double>(reader.number<int64_t>())); break;
        case 0x0A: break;
        case 0x10: value = json::number(reader.number<int32_t>()); break;
        case 0x12: value = json::number(static_cast<double>(reader.number<int64_t>())); break;
        case 0x13: { // .NET decimal: lo, mid, hi, flags (scale in bits 16-23, sign in bit 31).
            const auto lo = reader.number<uint32_t>(), mid = reader.number<uint32_t>(), hi = reader.number<uint32_t>(), flags = reader.number<uint32_t>();
            double magnitude = (static_cast<double>(hi) * 4294967296.0 + mid) * 4294967296.0 + lo;
            for (uint32_t scale = (flags >> 16) & 0xFF; scale; --scale) magnitude /= 10;
            value = json::number(flags & 0x80000000u ? -magnitude : magnitude);
            break;
        }
        case 0xFF: case 0x7F: break;
        default: return false;
        }
        if (reader.bad) return false;
        if (array) parent.elements.push_back(std::move(value)); else parent.properties[name] = std::move(value);
        return true;
    }
    bool document(json::Value& out, bool array) {
        if (++depth > 64) return false;
        const size_t start = reader.at;
        const auto length = reader.number<int32_t>();
        if (length < 5 || static_cast<size_t>(length) > reader.bytes.size() - start) return false;
        const size_t end = start + static_cast<size_t>(length) - 1;
        out = array ? json::array() : json::object();
        while (reader.at < end) if (!element(out, array)) return false;
        if (reader.at != end || reader.byte() != 0) return false;
        --depth;
        return !reader.bad;
    }
};

struct Node { uint32_t data_page = none; uint16_t data_index = 0; uint32_t prev = none, next = none; };
std::vector<Node> nodes(std::string_view bytes) {
    std::vector<Node> out;
    Reader reader{bytes};
    reader.at = header_size;
    for (uint16_t i = 0, count = itemCount(bytes); i < count && !reader.bad; ++i) {
        reader.number<uint16_t>(); // Its slot.
        const auto levels = reader.byte();
        reader.skip(1 + 6 + 6);
        const auto key_length = reader.number<uint16_t>();
        reader.skip(1 + static_cast<size_t>(key_length));
        Node node;
        node.data_page = reader.number<uint32_t>(); node.data_index = reader.number<uint16_t>();
        node.prev = reader.number<uint32_t>(); reader.number<uint16_t>();
        node.next = reader.number<uint32_t>(); reader.number<uint16_t>();
        if (levels > 1) reader.skip(static_cast<size_t>(levels - 1) * 12);
        if (!reader.bad && levels > 0) out.push_back(node);
    }
    return out;
}
}

std::string guidText(const uint8_t* b) {
    constexpr char digits[] = "0123456789abcdef";
    static constexpr int order[16] = {3, 2, 1, 0, 5, 4, 7, 6, 8, 9, 10, 11, 12, 13, 14, 15};
    std::string out;
    for (int i = 0; i < 16; ++i) {
        if (i == 4 || i == 6 || i == 8 || i == 10) out += '-';
        const auto byte = b[order[i]];
        out += digits[byte >> 4]; out += digits[byte & 15];
    }
    return out;
}

std::optional<json::Value> bson(std::string_view bytes) {
    Bson parser{Reader{bytes}};
    json::Value value;
    if (!parser.document(value, false)) return std::nullopt;
    return value;
}

std::optional<std::string> load(const std::filesystem::path& path, std::wstring& error) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) { error = L"Cannot open " + path.filename().wstring() + L" (is Playnite still running?)"; return std::nullopt; }
    const auto size = static_cast<uint64_t>(input.tellg());
    if (size < page_size || size > max_file) { error = path.filename().wstring() + L" is not a Playnite database."; return std::nullopt; }
    std::string bytes(static_cast<size_t>(size), '\0');
    input.seekg(0);
    if (!input.read(bytes.data(), static_cast<std::streamsize>(size))) { error = L"Cannot read " + path.filename().wstring(); return std::nullopt; }
    const auto first = page(bytes, 0);
    if (pageType(first) != header_page || first.substr(header_size, 27) != "** This is a LiteDB file **" || static_cast<uint8_t>(first[header_size + 27]) != 7) {
        error = path.filename().wstring() + L" is no LiteDB 4 file (a Playnite version this import does not know)."; return std::nullopt;
    }
    const auto salt = first.substr(header_size + 27 + 1 + 10 + 2 + 20, 16);
    if (salt.find_first_not_of('\0') != std::string_view::npos) { error = path.filename().wstring() + L" is protected by a password."; return std::nullopt; }
    return bytes;
}

namespace {
std::vector<std::pair<std::string, uint32_t>> collectionPages(std::string_view file) {
    std::vector<std::pair<std::string, uint32_t>> out;
    Reader reader{page(file, 0)};
    reader.at = header_size + 27 + 1 + 10 + 2 + 20 + 16;
    for (uint8_t i = 0, count = reader.byte(); i < count && !reader.bad; ++i) {
        auto name = reader.text();
        const auto id = reader.number<uint32_t>();
        if (!reader.bad) out.emplace_back(std::move(name), id);
    }
    return out;
}
}

std::vector<std::string> collections(std::string_view file) {
    std::vector<std::string> out;
    for (auto& [name, id] : collectionPages(file)) out.push_back(std::move(name));
    return out;
}

std::vector<json::Value> documents(std::string_view file, std::string_view collection) {
    std::vector<json::Value> out;
    uint32_t collection_id = none;
    for (const auto& [name, id] : collectionPages(file))
        if (collection.empty() || name == collection) { collection_id = id; break; }
    const auto collection_bytes = page(file, collection_id);
    if (pageType(collection_bytes) != collection_page) return out;
    // The first index (_id): its head node's page.
    Reader reader{collection_bytes};
    reader.at = header_size;
    reader.text();
    reader.skip(12);
    reader.text();
    reader.byte();
    const auto head = reader.number<uint32_t>();
    if (reader.bad) return out;
    // Every index page reachable through the nodes' neighbours.
    std::set<uint32_t> visited, waiting{head};
    while (!waiting.empty() && visited.size() < 1'000'000) {
        const auto id = *waiting.begin(); waiting.erase(waiting.begin());
        const auto bytes = page(file, id);
        if (pageType(bytes) != index_page || !visited.insert(id).second) continue;
        for (const auto& node : nodes(bytes))
            for (const auto next : {node.prev, node.next})
                if (next != none && !visited.contains(next)) waiting.insert(next);
    }
    std::set<std::pair<uint32_t, uint16_t>> seen;
    for (const auto id : visited)
        for (const auto& node : nodes(page(file, id))) {
            if (node.data_page == none || !seen.insert({node.data_page, node.data_index}).second) continue;
            const auto data = page(file, node.data_page);
            if (pageType(data) != data_page) continue;
            Reader blocks{data};
            blocks.at = header_size;
            std::string document;
            for (uint16_t i = 0, count = itemCount(data); i < count && !blocks.bad; ++i) {
                const auto index = blocks.number<uint16_t>();
                const auto extend = blocks.number<uint32_t>();
                const auto length = blocks.number<uint16_t>();
                const auto bytes = blocks.take(length);
                if (blocks.bad || index != node.data_index) continue;
                if (extend == none) document = bytes;
                else // A large document continues on extend pages.
                    for (uint32_t next = extend, guard = 0; next != none && guard < 300000; ++guard) {
                        const auto part = page(file, next);
                        if (pageType(part) != extend_page) { document.clear(); break; }
                        document.append(part.substr(header_size, std::min<size_t>(itemCount(part), page_size - header_size)));
                        next = nextPage(part);
                    }
                break;
            }
            if (document.empty()) continue;
            if (auto value = bson(document)) out.push_back(std::move(*value));
        }
    return out;
}
}
