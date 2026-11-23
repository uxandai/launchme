#include "manifest.hpp"
#include "../platform/text_encoding.hpp"
#include <algorithm>
#include <set>

namespace plugins {
using namespace wire;
namespace {
// Packages carry one native library for one system; others are refused when installed.
#ifdef _WIN32
constexpr std::string_view host_architecture = "windows-x64", library_extension = ".dll";
#else
constexpr std::string_view host_architecture = "linux-x64", library_extension = ".so";
#endif
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
Text label(const json::Value& value, bool optional = false) {
    Text out;
    if (value.type == json::Value::Type::string) out.pl = out.en = text_encoding::wide(value.text);
    else if (value.type == json::Value::Type::object) {
        out.pl = text_encoding::wide(text(value, "pl")); out.en = text_encoding::wide(text(value, "en"));
    }
    require((optional || (!out.pl.empty() && !out.en.empty())) && out.pl.size() <= 512 && out.en.size() <= 512 &&
            out.pl.find(L'\0') == std::wstring::npos && out.en.find(L'\0') == std::wstring::npos, "Invalid localized text");
    return out;
}
const std::vector<json::Value>& array(const json::Value& value, size_t max) {
    require(value.type == json::Value::Type::array || value.type == json::Value::Type::null_value, "Expected array");
    require(value.elements.size() <= max, "Too many entries"); return value.elements;
}
}
bool identifier(std::string_view value) {
    return !value.empty() && value.size() <= 80 && value.front() >= 'a' && value.front() <= 'z' &&
        std::all_of(value.begin(), value.end(), [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_'; }) &&
        value.back() != '.' && value.find("..") == std::string_view::npos;
}
bool packagePath(std::string_view value) {
    if (value.empty() || value.size() > 200 || value.front() == '/' || value.back() == '/') return false;
    for (size_t start = 0; start < value.size();) {
        const auto end = value.find('/', start);
        const auto part = value.substr(start, end == value.npos ? value.size() - start : end - start);
        if (part.empty() || part == "." || part == ".." || part.back() == '.' || part.back() == ' ') return false;
        std::string lower(part);
        for (char& c : lower) {
            if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.')) return false;
            if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
        }
        const auto stem = lower.substr(0, lower.find('.'));
        if (stem == "con" || stem == "prn" || stem == "aux" || stem == "nul" ||
            (stem.size() == 4 && (stem.starts_with("com") || stem.starts_with("lpt")) && stem[3] >= '0' && stem[3] <= '9')) return false;
        if (end == value.npos) break;
        start = end + 1;
    }
    return true;
}
bool Setting::accepts(const json::Value& value) const {
    if (type == "boolean") return value.type == json::Value::Type::boolean;
    if (value.type != json::Value::Type::string || value.text.size() > 512 || value.text.find('\0') != value.text.npos ||
        (value.text.size() && text_encoding::wide(value.text).empty())) return false;
    if (type == "choice") return std::any_of(choices.begin(), choices.end(), [&](const auto& c) { return c.first == value.text; });
    // folder: an absolute path picked in the launcher's folder browser (or empty).
    return type == "text" || type == "secret" || type == "folder";
}
Manifest parseManifest(std::string_view bytes) {
    require(bytes.size() <= 64 * 1024, "Manifest exceeds 64 KiB");
    const auto root = json::parse(bytes);
    require(root && root->type == json::Value::Type::object, "Invalid plugin.json");
    require(field(*root, "apiVersion").numeric == 1 && field(*root, "apiVersion").type == json::Value::Type::number, "Unsupported plugin API version");
#ifdef _WIN32
    require(text(*root, "architecture") == host_architecture, "Plugin must target windows-x64");
#else
    require(text(*root, "architecture") == host_architecture, "Plugin must target linux-x64");
#endif
    Manifest out;
    out.library = field(*root, "library").type == json::Value::Type::boolean && field(*root, "library").flag;
    out.id = text(*root, "id"); out.version = text(*root, "version"); out.entry = text(*root, "entry");
    require(identifier(out.id), "Invalid plugin ID");
    require(!out.version.empty() && out.version.size() <= 40 && std::all_of(out.version.begin(), out.version.end(), [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || c == '.' || c == '-'; }), "Invalid version");
    require(packagePath(out.entry) && out.entry.ends_with(library_extension), "Invalid library entry path");
    if (root->get("icon")) {
        out.icon = text(*root, "icon");
        require(packagePath(out.icon) && out.icon.ends_with(".png"), "Invalid plugin icon path");
    }
    if (root->get("iconText")) {
        const auto value = text(*root, "iconText");
        require(!value.empty() && value.size() <= 3 && std::all_of(value.begin(), value.end(), [](char c) {
            return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
        }), "Invalid plugin icon text");
        out.icon_text = text_encoding::wide(value);
    }
    out.name = label(field(*root, "name")); out.description = label(field(*root, "description"), true);
    std::set<std::string> ids;
    for (const auto& tag : array(field(*root, "tags"), 6)) {
        const auto value = tag.stringOr();
        require((value == "widget" || value == "screen" || value == "integration" || value == "menu" || value == "context" || value == "drawer" || value == "details") && ids.insert(value).second, "Unknown or duplicate plugin tag");
        out.tags.push_back(value);
    }
    ids.clear();
    for (const auto& v : array(field(*root, "settings"), 32)) {
        Setting s; s.id = text(v, "id"); s.type = text(v, "type"); s.title = label(field(v, "title")); s.initial = field(v, "default");
        require(identifier(s.id) && ids.insert(s.id).second, "Duplicate or invalid setting ID");
        for (const auto& c : array(field(v, "choices"), 12)) {
            const auto id = text(c, "value");
            require(identifier(id) && std::none_of(s.choices.begin(), s.choices.end(), [&](const auto& x) { return x.first == id; }), "Invalid choice");
            s.choices.emplace_back(id, label(field(c, "title")));
        }
        require(s.accepts(s.initial), "Invalid setting type or default"); out.settings.push_back(std::move(s));
    }
    ids.clear();
    for (const auto& v : array(field(*root, "contributions"), 12)) {
        Contribution c; c.id = text(v, "id"); c.command = text(v, "command"); c.kind = text(v, "kind");
        c.source = text(v, "source"); c.title = label(field(v, "title")); c.text = label(field(v, "text"), true);
        c.glyph = text(v, "glyph");
        require(!v.get("glyph") || (c.kind == "menu" && (c.glyph == "store" || c.glyph == "server-download")), "Invalid menu glyph");
        if (v.get("activeGame")) {
            require(field(v, "activeGame").type == json::Value::Type::boolean, "Invalid activeGame flag");
            c.active_game = field(v, "activeGame").flag;
            require(!c.active_game || c.kind == "widget", "activeGame requires a widget");
        }
        require(identifier(c.id) && ids.insert(c.id).second && identifier(c.command), "Invalid contribution ID or command");
        require(c.kind == "menu" || c.kind == "context" || c.kind == "catalog" || c.kind == "widget" || c.kind == "details" || c.kind == "library" || c.kind == "downloads" || c.kind == "achievements" || c.kind == "launch", "Unknown contribution kind");
        require(c.kind != "library" || c.source == "steam", "Library supplements require a Steam source");
        require(c.kind != "launch" || !c.source.empty(), "Launch providers require a game source");
        require(c.source.empty() || c.source == "steam" || c.source == "gog" || c.source == "epic" || c.source == "ea" ||
            c.source == "ubisoft" || c.source == "battlenet" || c.source == "custom" || c.source == "xbox" || c.source == "drmfree", "Unknown game source");
        out.contributions.push_back(std::move(c));
    }
    require(std::count_if(out.contributions.begin(), out.contributions.end(), [](const auto& c) { return c.kind == "catalog"; }) <= 1, "One catalog per plugin");
    require(std::count_if(out.contributions.begin(), out.contributions.end(), [](const auto& c) { return c.kind == "details"; }) <= 1, "One details provider per plugin");
    require(std::count_if(out.contributions.begin(), out.contributions.end(), [](const auto& c) { return c.kind == "library"; }) <= 1, "One library supplement per plugin");
    require(std::count_if(out.contributions.begin(), out.contributions.end(), [](const auto& c) { return c.kind == "downloads"; }) <= 1, "One download provider per plugin");
    require(std::count_if(out.contributions.begin(), out.contributions.end(), [](const auto& c) { return c.kind == "achievements"; }) <= 1, "One achievements provider per plugin");
    require(std::count_if(out.contributions.begin(), out.contributions.end(), [](const auto& c) { return c.kind == "launch"; }) <= 1, "One launch provider per plugin");
    return out;
}
bool matches(const Contribution& c, const Game& game) {
    return isGame(game) && game.kind != GameKind::catalog && (c.source.empty() ||
        (c.source == sourceSlug(game.source) && (c.source != "steam" || (game.kind == GameKind::steam && game.app_id))));
}
std::string gameJson(const Game& game) {
    return "{\"key\":" + quote(gameKey(game)) + ",\"name\":" + quote(text_encoding::utf8(game.name)) +
        ",\"source\":" + quote(sourceSlug(game.source)) + ",\"appId\":" + std::to_string(steamArtwork(game) ? game.app_id : 0) +
        ",\"shortcut\":" + (game.kind == GameKind::nonsteam ? "true" : "false") +
        ",\"installed\":" + (game.installed ? "true" : "false") + ",\"available\":" + (game.catalog_available ? "true" : "false") + '}';
}
std::string achievementGameJson(const Game& game) {
    // Achievements alone need to find the game outside the store: its platform, the ROM an
    // emulator runs (RetroAchievements hashes it) and the install folder (emulator trophy files).
    auto out = gameJson(game);
    out.pop_back();
    out += ",\"sourceId\":" + quote(game.source_id) + ",\"epicNamespace\":" + quote(game.epic_namespace) + ",\"platform\":" + quote(text_encoding::utf8Lenient(game.platform)) +
        ",\"rom\":" + quote(text_encoding::utf8Lenient(game.rom.wstring())) +
        ",\"installDirectory\":" + quote(text_encoding::utf8Lenient(game.install_directory.wstring())) +
        ",\"executable\":" + quote(text_encoding::utf8Lenient(game.executable.wstring())) + '}';
    return out;
}
CatalogResult parseCatalog(const json::Value& value, const std::string& owner) {
    CatalogResult out; out.error = text_encoding::wide(text(value, "error"));
    const auto& games = field(value, "games");
    require(games.type == json::Value::Type::array && games.elements.size() <= 100, "Invalid catalog response");
    std::set<uint32_t> seen;
    for (const auto& item : games.elements) {
        const auto& id = field(item, "appId");
        require(id.type == json::Value::Type::number && id.numeric >= 1 && id.numeric <= UINT32_MAX && std::floor(id.numeric) == id.numeric, "Invalid Steam AppID");
        Game g; g.app_id = static_cast<uint32_t>(id.numeric); g.name = text_encoding::wide(text(item, "name"));
        require(!g.name.empty() && g.name.size() <= 512 && g.name.find_first_of(std::wstring(L"\0\r\n", 3)) == g.name.npos, "Invalid catalog title");
        g.kind = GameKind::catalog; g.installed = false; g.catalog_available = field(item, "available").flag; g.catalog_plugin = owner;
        if (seen.insert(g.app_id).second) out.games.push_back(std::move(g));
    }
    return out;
}
std::optional<std::vector<uint32_t>> parseLibrarySupplement(const json::Value& value) {
    const auto* ids = value.get("steamAppIds");
    if (!ids && !text(value, "message").empty()) return {}; // Transient scan failure: retain the last hints.
    require(ids && ids->type == json::Value::Type::array && ids->elements.size() <= 20000, "Invalid library supplement");
    std::set<uint32_t> unique;
    for (const auto& id : ids->elements) {
        require(id.type == json::Value::Type::number && id.numeric >= 1 && id.numeric <= INT32_MAX && std::floor(id.numeric) == id.numeric, "Invalid library Steam AppID");
        unique.insert(static_cast<uint32_t>(id.numeric));
    }
    return std::vector<uint32_t>(unique.begin(), unique.end());
}
}
