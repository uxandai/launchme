#include "game_mods_sources.hpp"
#include "game_mods.hpp"
#include "../net/json.hpp"
#include "../platform/ascii_text.hpp"
#include "../platform/text_encoding.hpp"
#include "../platform/unicode_text.hpp"

#include <algorithm>

namespace game_mods::sources {
namespace {
std::string text(const json::Value* value) { return value ? value->stringOr() : std::string{}; }

std::optional<Release> releaseOf(const json::Value& item) {
    if (item.type != json::Value::Type::object) return std::nullopt;
    Release result;
    result.tag = text(item.get("tag_name"));
    if (result.tag.empty()) return std::nullopt;
    if (const auto* assets = item.get("assets"); assets && assets->type == json::Value::Type::array)
        for (const auto& asset : assets->elements) {
            Asset entry;
            entry.name = text(asset.get("name"));
            const auto url = text(asset.get("browser_download_url"));
            if (entry.name.empty() || !url.starts_with("https://github.com/")) continue; // GitHub's own downloads only.
            entry.url = text_encoding::wideLenient(url);
            if (const auto* size = asset.get("size")) entry.size = static_cast<uint64_t>(std::max(0.0, size->numeric));
            result.assets.push_back(std::move(entry));
        }
    return result;
}

bool releaseCandidate(std::string_view tag) {
    const auto lower = ascii_text::lower(tag);
    return lower.find("-rc") != std::string::npos || lower.find("alpha") != std::string::npos || lower.find("beta") != std::string::npos;
}
} // namespace

const Asset* Release::find(std::string_view suffix) const {
    for (const auto& asset : assets)
        if (asset.name.size() >= suffix.size() && ascii_text::equalIgnoreCase(std::string_view(asset.name).substr(asset.name.size() - suffix.size()), suffix))
            return &asset;
    return nullptr;
}

Download reshadeSetup(std::string_view html) {
    // The newest add-on build's link, as the page lists it: downloads/ReShade_Setup_6.8.0_Addon.exe.
    constexpr std::string_view head = "downloads/ReShade_Setup_", tail = "_Addon.exe";
    Download best;
    for (size_t at = html.find(head); at != std::string_view::npos; at = html.find(head, at + 1)) {
        const size_t start = at + head.size();
        size_t end = start;
        while (end < html.size() && ((html[end] >= '0' && html[end] <= '9') || html[end] == '.')) ++end;
        if (end == start || html.substr(end, tail.size()) != tail) continue;
        const auto version = std::string(html.substr(start, end - start));
        if (best && compareVersions(version, best.version) <= 0) continue;
        best.version = version;
        best.file = "ReShade_Setup_" + version + "_Addon.exe";
        best.url = L"https://reshade.me/downloads/" + text_encoding::wideLenient(best.file);
    }
    return best;
}

std::optional<Release> release(std::string_view source) {
    const auto root = json::parse(source, 16 * 1024 * 1024);
    if (!root) return std::nullopt;
    return releaseOf(*root);
}

std::optional<Release> newestTagged(std::string_view source, std::string_view prefix) {
    const auto root = json::parse(source, 16 * 1024 * 1024);
    if (!root || root->type != json::Value::Type::array) return std::nullopt;
    std::optional<Release> best;
    for (const auto& item : root->elements) {
        const auto tag = text(item.get("tag_name"));
        if (!tag.starts_with(prefix) || releaseCandidate(tag.substr(prefix.size()))) continue;
        if (const auto* draft = item.get("draft"); draft && draft->flag) continue;
        auto found = releaseOf(item);
        if (!found || found->assets.empty()) continue;
        if (!best || compareVersions(tag.substr(prefix.size()), best->tag.substr(prefix.size())) > 0) best = std::move(found);
    }
    return best;
}

Download dlss(std::string_view source, std::string_view kind, std::string_view prefer) {
    const auto root = json::parse(source, 4 * 1024 * 1024);
    Download best;
    if (!root) return best;
    const auto* list = root->get(kind);
    if (!list || list->type != json::Value::Type::array) return best;
    bool best_preferred = false;
    for (const auto& item : list->elements) {
        const auto version = text(item.get("version")), url = text(item.get("url"));
        if (version.empty() || !url.starts_with("https://github.com/")) continue;
        const bool preferred = !prefer.empty() && version.find(prefer) != std::string::npos;
        if (best) {
            const int order = compareVersions(version, best.version);
            if (order < 0 || (order == 0 && (best_preferred || !preferred))) continue;
        }
        best.version = version;
        best.url = text_encoding::wideLenient(url);
        best.file = url.substr(url.rfind('/') + 1);
        best_preferred = preferred;
    }
    return best;
}

std::wstring titleKey(std::wstring_view title) {
    std::wstring key;
    for (const wchar_t c : unicode_text::lower(unicode_text::decompose(title)))
        if (unicode_text::isLetterOrDigit(c) && !unicode_text::isMark(c)) key += c;
    return key;
}

std::vector<RenoMod> renodxIndex(std::string_view source) {
    std::vector<RenoMod> result;
    const auto root = json::parse(source, 8 * 1024 * 1024);
    if (!root) return result;
    const auto* games = root->get("games");
    if (!games || games->type != json::Value::Type::array) return result;
    for (const auto& game : games->elements) {
        const auto title = text_encoding::wideLenient(text(game.get("title")));
        std::vector<std::wstring> aliases;
        if (const auto* list = game.get("aliases"); list && list->type == json::Value::Type::array)
            for (const auto& alias : list->elements) if (!alias.stringOr().empty()) aliases.push_back(text_encoding::wideLenient(alias.stringOr()));
        uint32_t game_appid = 0;
        if (const auto* appid = game.get("steam_appid")) game_appid = static_cast<uint32_t>(std::max(0.0, appid->numeric));
        const auto* mods = game.get("mods");
        if (!mods || mods->type != json::Value::Type::array) continue;
        for (const auto& mod : mods->elements) {
            const auto category = text(mod.get("category"));
            if (category != "game" && category != "engine") continue; // "related": other projects, no add-on here.
            uint32_t appid = game_appid;
            if (const auto* deploy = mod.get("deploy"))
                if (const auto* own = deploy->get("steam_appid")) appid = static_cast<uint32_t>(std::max(0.0, own->numeric));
            const auto* artifacts = mod.get("artifacts");
            if (!artifacts || artifacts->type != json::Value::Type::array) continue;
            for (const auto& artifact : artifacts->elements) {
                auto file = text(artifact.get("name"));
                // A file of the same release: no folders, no other hosts.
                if (!file.starts_with("renodx-") || file.find_first_of("/\\:") != std::string::npos ||
                    !(file.ends_with(".addon64") || file.ends_with(".addon32"))) continue;
                RenoMod entry;
                entry.id = text(mod.get("id"));
                entry.file = std::move(file);
                entry.title = title.empty() ? text_encoding::wideLenient(text(mod.get("title"))) : title;
                entry.aliases = aliases;
                entry.steam_appid = category == "engine" ? 0 : appid; // An engine's add-on names a sample game.
                entry.x86 = text(artifact.get("arch")) == "x86" || entry.file.ends_with(".addon32");
                entry.category = category;
                entry.status = text(mod.get("status"));
                result.push_back(std::move(entry));
            }
        }
    }
    return result;
}

std::optional<RenoMod> renodxMatch(const std::vector<RenoMod>& index, uint32_t steam_appid, std::wstring_view title, bool x86) {
    const auto pick = [&](auto&& matches) -> std::optional<RenoMod> {
        const RenoMod* any = nullptr;
        for (const auto& mod : index) {
            if (mod.category != "game" || !matches(mod)) continue;
            if (mod.x86 == x86) return mod;
            if (!any) any = &mod;
        }
        if (any) return *any; // The only build there is (the installer then says it does not fit).
        return std::nullopt;
    };
    if (steam_appid)
        if (auto found = pick([&](const RenoMod& mod) { return mod.steam_appid == steam_appid; })) return found;
    const auto key = titleKey(title);
    if (key.empty()) return std::nullopt;
    return pick([&](const RenoMod& mod) {
        if (titleKey(mod.title) == key) return true;
        return std::any_of(mod.aliases.begin(), mod.aliases.end(), [&](const std::wstring& alias) { return titleKey(alias) == key; });
    });
}

std::optional<RenoMod> renodxGeneric(const std::vector<RenoMod>& index, std::string_view id, bool x86) {
    const RenoMod* any = nullptr;
    for (const auto& mod : index) {
        if (mod.category != "engine" || mod.id != id) continue;
        if (mod.x86 == x86) return mod;
        if (!any) any = &mod;
    }
    if (any) return *any;
    return std::nullopt;
}

std::string checksum(std::string_view sums, std::string_view file) {
    for (size_t start = 0; start < sums.size();) {
        size_t end = sums.find('\n', start);
        if (end == std::string_view::npos) end = sums.size();
        auto line = ascii_text::trim(sums.substr(start, end - start));
        start = end + 1;
        const auto space = line.find_first_of(" \t");
        if (space == std::string_view::npos) continue;
        const auto digest = line.substr(0, space);
        auto name = ascii_text::trim(line.substr(space));
        if (!name.empty() && name.front() == '*') name.remove_prefix(1); // sha256sum's binary mark.
        if (name == file && digest.size() == 64) return ascii_text::lower(digest);
    }
    return {};
}
} // namespace game_mods::sources
