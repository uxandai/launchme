#include "steam_appinfo.hpp"
#include "../platform/file_io.hpp"
#include "binary_kv.hpp"
#include "../platform/text_encoding.hpp"

#include <algorithm>
#include <charconv>
#include <cstring>
#include <cwctype>
#include <fstream>

namespace steam_appinfo {
namespace {
constexpr uint32_t v27 = 0x07564427, v28 = 0x07564428, v29 = 0x07564429;

std::vector<uint8_t> bytes(const std::filesystem::path& path, size_t limit) { return file_io::readBytes(path, limit); }
std::wstring wide(const binary_kv::Node* node) {
    return node && !node->object ? text_encoding::wideLenient(node->text) : std::wstring{};
}
int64_t integer(const binary_kv::Node* node) { return node ? node->integer() : -1; }
// {"0": "1", "1": "25"}: the values in index order.
std::vector<uint32_t> ids(const binary_kv::Node* node) {
    std::vector<std::pair<int64_t, uint32_t>> ordered;
    if (!node || !node->object) return {};
    for (const auto& entry : node->children) {
        const auto value = entry.integer();
        if (value <= 0 || value > UINT32_MAX) continue;
        int64_t index = 0;
        const auto [end, error] = std::from_chars(entry.name.data(), entry.name.data() + entry.name.size(), index);
        ordered.emplace_back(error == std::errc{} && end == entry.name.data() + entry.name.size() ? index : static_cast<int64_t>(ordered.size()) + 100000,
                             static_cast<uint32_t>(value));
    }
    std::stable_sort(ordered.begin(), ordered.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    std::vector<uint32_t> result;
    for (const auto& [index, value] : ordered)
        if (std::find(result.begin(), result.end(), value) == result.end()) result.push_back(value);
    return result;
}
void addUnique(std::vector<std::wstring>& list, std::wstring value) {
    while (!value.empty() && value.back() == L' ') value.pop_back();
    if (!value.empty() && std::find(list.begin(), list.end(), value) == list.end() && list.size() < 8) list.push_back(std::move(value));
}
// "Valve, Hidden Path" (the older extended/developer strings).
void addList(std::vector<std::wstring>& list, const std::wstring& text) {
    size_t start = 0;
    while (start < text.size()) {
        auto end = text.find(L',', start);
        if (end == std::wstring::npos) end = text.size();
        auto item = text.substr(start, end - start);
        const auto first = item.find_first_not_of(L' ');
        addUnique(list, first == std::wstring::npos ? std::wstring{} : item.substr(first));
        start = end + 1;
    }
}
App decode(uint32_t id, const binary_kv::Node& root) {
    App app;
    app.app_id = id;
    const auto* info = root.child("appinfo");
    if (!info) info = &root;
    const auto* common = info->child("common");
    if (!common) return app;
    app.name = wide(common->child("name"));
    app.type = wide(common->child("type"));
    for (auto& ch : app.type) ch = static_cast<wchar_t>(std::towlower(ch));
    if (const auto value = integer(common->child("steam_release_date")); value > 0) app.steam_release = static_cast<uint64_t>(value);
    if (const auto value = integer(common->child("original_release_date")); value > 0) app.original_release = static_cast<uint64_t>(value);
    if (const auto value = integer(common->child("review_percentage")); value >= 0 && value <= 100) app.review_percent = static_cast<int>(value);
    if (const auto value = integer(common->child("review_score")); value >= 1 && value <= 9) app.review_score = static_cast<int>(value);
    if (const auto value = integer(common->child("metacritic_score")); value > 0 && value <= 100) app.metacritic = static_cast<int>(value);
    app.genres = ids(common->child("genres"));
    app.tags = ids(common->child("store_tags"));
    if (app.tags.size() > 20) app.tags.resize(20);
    if (const auto value = integer(common->child("primary_genre")); value > 0 && value <= UINT32_MAX) app.primary_genre = static_cast<uint32_t>(value);
    if (const auto* category = common->child("category"); category && category->object)
        for (const auto& entry : category->children) {
            // "category_2": "1"
            if (!entry.name.starts_with("category_") || entry.integer() <= 0) continue;
            uint32_t number = 0;
            const auto* text = entry.name.data() + 9;
            const auto [end, error] = std::from_chars(text, entry.name.data() + entry.name.size(), number);
            if (error == std::errc{} && end == entry.name.data() + entry.name.size() && number) app.categories.push_back(number);
        }
    if (const auto* associations = common->child("associations"); associations && associations->object)
        for (const auto& entry : associations->children) {
            const auto type = entry.child("type") ? entry.child("type")->text : std::string{};
            auto name = wide(entry.child("name"));
            if (type == "developer") addUnique(app.developers, std::move(name));
            else if (type == "publisher") addUnique(app.publishers, std::move(name));
            else if (type == "franchise") addUnique(app.franchises, std::move(name));
        }
    if (const auto* extended = info->child("extended")) {
        if (app.developers.empty()) addList(app.developers, wide(extended->child("developer")));
        if (app.publishers.empty()) addList(app.publishers, wide(extended->child("publisher")));
    }
    app.controller_support = wide(common->child("controller_support"));
    if (const auto* deck = common->child("steam_deck_compatibility"))
        if (const auto value = integer(deck->child("category")); value >= 0 && value <= 3) app.deck = static_cast<int>(value);
    return app;
}
}

std::optional<std::vector<App>> parse(std::span<const uint8_t> data, const std::function<bool(uint32_t)>& wanted) {
    if (data.size() < 8) return std::nullopt;
    const auto u32 = [&](size_t at) { uint32_t n; std::memcpy(&n, data.data() + at, 4); return n; };
    const auto magic = u32(0);
    if (magic != v27 && magic != v28 && magic != v29) return std::nullopt;
    size_t cursor = 8, end = data.size();
    std::vector<std::string> table;
    if (magic == v29) {
        if (data.size() < 16) return std::nullopt;
        uint64_t offset = 0;
        std::memcpy(&offset, data.data() + 8, 8);
        // Subtracted, not added: an offset near UINT64_MAX would wrap `offset + 4` past the check.
        if (offset < 20 || offset > data.size() || data.size() - offset < 4) return std::nullopt;
        end = static_cast<size_t>(offset);
        cursor = 16;
        const auto count = u32(end);
        // Each name takes at least its NUL: a count the rest of the file cannot hold reserves nothing.
        if (count > 1'000'000 || count > data.size() - end - 4) return std::nullopt;
        table.reserve(count);
        size_t at = end + 4;
        for (uint32_t i = 0; i < count; ++i) {
            const auto* start = data.data() + at;
            const auto* stop = static_cast<const uint8_t*>(std::memchr(start, 0, data.size() - at));
            if (!stop) return std::nullopt;
            table.emplace_back(reinterpret_cast<const char*>(start), static_cast<size_t>(stop - start));
            at += static_cast<size_t>(stop - start) + 1;
        }
    }
    const size_t header = magic == v27 ? 40 : 60;
    std::vector<App> apps;
    while (cursor <= end && end - cursor >= 4) {
        const auto id = u32(cursor);
        cursor += 4;
        if (!id) return cursor == end ? std::optional(std::move(apps)) : std::nullopt;
        if (end - cursor < 4) return std::nullopt;
        const auto size = u32(cursor);
        cursor += 4;
        if (size <= header || size > end - cursor) return std::nullopt;
        if (wanted && wanted(id)) {
            const auto payload = data.subspan(cursor + header, size - header);
            if (const auto root = binary_kv::parse(payload, magic == v29 ? &table : nullptr)) {
                auto app = decode(id, *root);
                if (!app.name.empty()) apps.push_back(std::move(app));
            }
        }
        cursor += size;
    }
    return std::nullopt;
}

std::optional<std::vector<App>> read(const std::filesystem::path& steam_root, const std::function<bool(uint32_t)>& wanted) {
    if (steam_root.empty()) return std::nullopt;
    return parse(bytes(steam_root / L"appcache" / L"appinfo.vdf", 512ull * 1024 * 1024), wanted);
}

const wchar_t* genreName(uint32_t id) {
    switch (id) {
    case 1: return L"Action";
    case 2: return L"Strategy";
    case 3: return L"RPG";
    case 4: return L"Casual";
    case 9: return L"Racing";
    case 18: return L"Sports";
    case 23: return L"Indie";
    case 25: return L"Adventure";
    case 28: return L"Simulation";
    case 29: return L"Massively Multiplayer";
    case 37: return L"Free To Play";
    case 70: return L"Early Access";
    default: return nullptr; // Software, adult-content flags and the rest are not game genres.
    }
}
}
