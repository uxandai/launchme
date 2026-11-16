#include "launch_options_store.hpp"
#include "../model/launch_options_catalog.hpp"
#include "../platform/atomic_file.hpp"
#include "../platform/system.hpp"
#include "../platform/text_encoding.hpp"
#include "../platform/unicode_text.hpp"
#include "../platform/user_config.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>
#include <mutex>

namespace launch_options_store {
namespace {
constexpr size_t max_document = 8 * 1024 * 1024;
constexpr size_t max_games = 20000;

std::mutex& lock() {
    static std::mutex mutex;
    return mutex;
}

std::string utf8(const std::filesystem::path& path) { return text_encoding::utf8Lenient(path.wstring()); }
std::filesystem::path absolutePath(const json::Value* value) {
    if (!value || value->type != json::Value::Type::string || value->text.empty() || value->text.size() > 4096) return {};
    const auto wide = text_encoding::wide(value->text);
    if (wide.empty()) return {};
    std::filesystem::path path(wide);
    return path.is_absolute() ? path.lexically_normal() : std::filesystem::path{};
}
bool validKey(std::string_view key) {
    return !key.empty() && key.size() <= 1024 && std::none_of(key.begin(), key.end(), [](char ch) { return static_cast<unsigned char>(ch) < 0x20; });
}

Document loadUnlocked() {
    std::ifstream input(file(), std::ios::binary);
    if (!input) return {};
    std::string text;
    text.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    if (text.size() > max_document) return {};
    return parse(text).value_or(Document{});
}
bool saveUnlocked(const Document& document) { return atomic_file::writeDurable(file(), serialize(document)); }

// Whether `prefix` is assigned to a game other than `key` (by its options or the prefixes table).
bool takenByOther(const Document& document, const std::filesystem::path& prefix, const std::string& key) {
    const auto normal = prefix.lexically_normal();
    for (const auto& [other, path] : document.prefixes)
        if (other != key && path.lexically_normal() == normal) return true;
    for (const auto& [other, options] : document.games)
        if (other != key && !options.prefix.empty() && options.prefix.lexically_normal() == normal) return true;
    return false;
}

std::filesystem::path rootOf(const Document& document) { return document.prefix_root.empty() ? defaultPrefixRoot() : document.prefix_root; }

std::filesystem::path assign(Document& document, const Game& game, const std::string& key, bool& changed) {
    if (const auto found = document.prefixes.find(key); found != document.prefixes.end()) return found->second;
    const auto root = rootOf(document);
    const auto base = slug(game.name, key);
    for (int attempt = 1; attempt < 1000; ++attempt) {
        const auto candidate = root / (attempt == 1 ? base : base + "-" + std::to_string(attempt));
        // <root>/default is the shared prefix of programs without a game.
        if ((attempt == 1 && base == "default") || takenByOther(document, candidate, key)) continue;
        document.prefixes[key] = candidate;
        changed = true;
        return candidate;
    }
    return root / (base + "-" + key_codec::encode(key));
}

LaunchOptions resolvedOptions(const Document& document, const std::string& key) {
    const auto found = document.games.find(key);
    auto options = found != document.games.end() ? found->second : document.defaults;
    if (options.compat_tool.empty() || options.compat_tool == "default")
        options.compat_tool = document.defaults.compat_tool.empty() ? std::string("default") : document.defaults.compat_tool;
    return options;
}

bool chosen(const std::string& tool) { return !tool.empty() && tool != "default"; }

// The game's own tool, else its prefix's, else the global choice.
std::string toolOf(const Document& document, const std::string& key, const std::filesystem::path& prefix) {
    if (const auto found = document.games.find(key); !key.empty() && found != document.games.end() && chosen(found->second.compat_tool))
        return found->second.compat_tool;
    if (const auto info = document.prefix_info.find(prefix.lexically_normal()); info != document.prefix_info.end() && chosen(info->second.compat_tool))
        return info->second.compat_tool;
    return chosen(document.defaults.compat_tool) ? document.defaults.compat_tool : std::string("default");
}
}

namespace {
namespace catalog = launch_options_catalog;
// Fields kept as values of their own (variable switches live in the environment list).
bool ownField(const catalog::Option& option) { return !option.variable && option.type != catalog::Type::environment; }

// One field's JSON (the type its catalog entry has).
json::Value fieldJson(const catalog::Option& option, const std::string& value) {
    switch (option.type) {
    case catalog::Type::toggle: return json::boolean(value == "1");
    case catalog::Type::number: return json::number(std::stod(value));
    default: return json::string(value);
    }
}
json::Value variablesJson(const std::vector<LaunchOptions::Variable>& variables) {
    auto list = json::array();
    for (const auto& variable : variables) {
        auto entry = json::object();
        entry.properties.emplace("name", json::string(variable.name));
        entry.properties.emplace("value", json::string(variable.value));
        list.elements.push_back(std::move(entry));
    }
    return list;
}
// Reads one field into `options`; false (unchanged) for a value of the wrong type or out of range.
bool readField(const catalog::Option& option, const json::Value& field, LaunchOptions& options) {
    switch (option.type) {
    case catalog::Type::toggle:
        return field.type == json::Value::Type::boolean && option.set(option, options, field.flag ? "1" : "0");
    case catalog::Type::number:
        return field.type == json::Value::Type::number && field.numeric >= 0 && field.numeric <= 4294967295.0 && std::floor(field.numeric) == field.numeric &&
               option.set(option, options, std::to_string(static_cast<uint32_t>(field.numeric)));
    default: return field.type == json::Value::Type::string && option.set(option, options, field.text);
    }
}
void readVariables(const catalog::Option& option, const json::Value& field, std::vector<LaunchOptions::Variable>& variables) {
    if (field.type != json::Value::Type::array) return;
    for (const auto& entry : field.elements) {
        const auto name = entry.get("name") ? entry.get("name")->stringOr() : std::string{};
        const auto text = entry.get("value") ? entry.get("value")->stringOr() : std::string{};
        // Each entry on its own: an invalid one is skipped, not the whole list.
        LaunchOptions single;
        if (variables.size() < 64 && option.set(option, single, name + "=" + text) && single.environment.size() == 1)
            variables.push_back(single.environment.front());
    }
}
const LaunchOptions::Variable* variableIn(const std::vector<LaunchOptions::Variable>& list, std::string_view name) {
    for (auto it = list.rbegin(); it != list.rend(); ++it) if (it->name == name) return &*it; // The last one wins, as in the environment.
    return nullptr;
}
std::string variableId(std::string_view name) { return "env:" + std::string(name); }

// A game's object (version 2): its overrides only.
json::Value gameJson(const LaunchOptions& game, const std::set<std::string>& overrides) {
    auto result = json::object();
    for (const auto& option : catalog::options())
        if (ownField(option) && overrides.contains(option.id)) result.properties.emplace(option.id, fieldJson(option, option.get(option, game)));
    std::vector<LaunchOptions::Variable> own;
    auto unset = json::array();
    for (const auto& id : overrides) {
        if (!id.starts_with("env:")) continue;
        const auto name = std::string_view(id).substr(4);
        if (const auto* variable = variableIn(game.environment, name)) own.push_back(*variable);
        else unset.elements.push_back(json::string(std::string(name)));
    }
    // In the game's own order.
    std::stable_sort(own.begin(), own.end(), [&](const auto& a, const auto& b) {
        const auto index = [&](const std::string& name) {
            for (size_t i = 0; i < game.environment.size(); ++i) if (game.environment[i].name == name) return i;
            return game.environment.size();
        };
        return index(a.name) < index(b.name);
    });
    if (!own.empty()) result.properties.emplace("environment", variablesJson(own));
    if (!unset.elements.empty()) result.properties.emplace("environment_unset", std::move(unset));
    return result;
}
// A game's object back: its options and overrides, on top of `defaults`.
std::pair<LaunchOptions, std::set<std::string>> gameFromJson(const json::Value& value, const LaunchOptions& defaults) {
    LaunchOptions own;
    std::set<std::string> overrides;
    if (value.type == json::Value::Type::object) {
        for (const auto& option : catalog::options()) {
            if (option.variable) continue;
            const auto* field = value.get(option.id);
            if (!field) continue;
            if (option.type == catalog::Type::environment) {
                readVariables(option, *field, own.environment);
                for (const auto& variable : own.environment) overrides.insert(variableId(variable.name));
            } else if (readField(option, *field, own)) overrides.insert(option.id);
        }
        if (const auto* unset = value.get("environment_unset"); unset && unset->type == json::Value::Type::array)
            for (const auto& name : unset->elements)
                if (name.type == json::Value::Type::string && catalog::variableName(name.text) && overrides.size() < 512) overrides.insert(variableId(name.text));
    }
    return {inherit(defaults, own, overrides), std::move(overrides)};
}
}

json::Value toJson(const LaunchOptions& options) {
    auto result = json::object();
    const LaunchOptions defaults;
    for (const auto& option : catalog::options()) {
        if (option.variable) continue; // Kept in the environment list.
        const auto value = option.get(option, options);
        if (value == option.get(option, defaults)) continue;
        if (option.type == catalog::Type::environment) result.properties.emplace(option.id, variablesJson(options.environment));
        else result.properties.emplace(option.id, fieldJson(option, value));
    }
    return result;
}

LaunchOptions fromJson(const json::Value& value) {
    LaunchOptions result;
    if (value.type != json::Value::Type::object) return result;
    for (const auto& option : catalog::options()) {
        if (option.variable) continue; // Read with the environment list.
        const auto* field = value.get(option.id);
        if (!field) continue;
        if (option.type == catalog::Type::environment) readVariables(option, *field, result.environment);
        else readField(option, *field, result);
    }
    return result;
}

LaunchOptions forGame(const LaunchOptions& defaults) {
    auto result = defaults;
    result.compat_tool = "default";
    result.prefix.clear();
    return result;
}

std::set<std::string> differences(const LaunchOptions& a, const LaunchOptions& b) {
    std::set<std::string> result;
    for (const auto& option : catalog::options())
        if (ownField(option) && option.get(option, a) != option.get(option, b)) result.insert(option.id);
    for (const auto* list : {&a.environment, &b.environment})
        for (const auto& variable : *list) {
            const auto* left = variableIn(a.environment, variable.name);
            const auto* right = variableIn(b.environment, variable.name);
            if (!left || !right || left->value != right->value) result.insert(variableId(variable.name));
        }
    return result;
}

LaunchOptions inherit(const LaunchOptions& defaults, const LaunchOptions& game, const std::set<std::string>& overrides) {
    auto result = forGame(defaults);
    for (const auto& option : catalog::options())
        if (ownField(option) && overrides.contains(option.id)) option.set(option, result, option.get(option, game));
    std::vector<LaunchOptions::Variable> environment;
    std::set<std::string> done;
    for (const auto& variable : defaults.environment) {
        if (!done.insert(variable.name).second) continue;
        if (!overrides.contains(variableId(variable.name))) environment.push_back(*variableIn(defaults.environment, variable.name));
        else if (const auto* own = variableIn(game.environment, variable.name)) environment.push_back(*own);
    }
    for (const auto& variable : game.environment)
        if (overrides.contains(variableId(variable.name)) && done.insert(variable.name).second) environment.push_back(*variableIn(game.environment, variable.name));
    result.environment = std::move(environment);
    return result;
}

std::string serialize(const Document& document) {
    auto root = json::object();
    root.properties.emplace("version", json::number(2));
    if (!document.prefix_root.empty()) root.properties.emplace("prefix_root", json::string(utf8(document.prefix_root)));
    root.properties.emplace("default", toJson(document.defaults));
    auto games = json::object();
    for (const auto& [key, options] : document.games) {
        const auto found = document.overrides.find(key);
        games.properties.emplace(key, gameJson(options, found != document.overrides.end() ? found->second : differences(options, forGame(document.defaults))));
    }
    root.properties.emplace("games", std::move(games));
    auto prefixes = json::object();
    for (const auto& [key, path] : document.prefixes) prefixes.properties.emplace(key, json::string(utf8(path)));
    root.properties.emplace("prefixes", std::move(prefixes));
    if (!document.prefix_info.empty()) {
        auto named = json::object();
        for (const auto& [path, info] : document.prefix_info) {
            auto entry = json::object();
            if (!info.name.empty()) entry.properties.emplace("name", json::string(text_encoding::utf8Lenient(info.name)));
            if (chosen(info.compat_tool)) entry.properties.emplace("compat_tool", json::string(info.compat_tool));
            named.properties.emplace(utf8(path), std::move(entry));
        }
        root.properties.emplace("prefix_info", std::move(named));
    }
    return json::serialize(root) + "\n";
}

std::optional<Document> parse(std::string_view text) {
    const auto root = json::parse(text, max_document);
    if (!root || root->type != json::Value::Type::object) return std::nullopt;
    const auto* version = root->get("version");
    if (!version || version->type != json::Value::Type::number || (version->numeric != 1 && version->numeric != 2)) return std::nullopt;
    const bool copies = version->numeric == 1; // Each game a full copy of its options.
    Document result;
    result.prefix_root = absolutePath(root->get("prefix_root"));
    if (const auto* defaults = root->get("default")) result.defaults = fromJson(*defaults);
    if (const auto* games = root->get("games"); games && games->type == json::Value::Type::object)
        for (const auto& [key, options] : games->properties) {
            if (!validKey(key) || options.type != json::Value::Type::object || result.games.size() >= max_games) continue;
            if (copies) {
                // What differs from the default becomes the game's override.
                const auto copy = fromJson(options);
                auto overrides = differences(copy, forGame(result.defaults));
                result.games.emplace(key, inherit(result.defaults, copy, overrides));
                result.overrides.emplace(key, std::move(overrides));
            } else {
                auto [game, overrides] = gameFromJson(options, result.defaults);
                result.games.emplace(key, std::move(game));
                result.overrides.emplace(key, std::move(overrides));
            }
        }
    if (const auto* prefixes = root->get("prefixes"); prefixes && prefixes->type == json::Value::Type::object)
        for (const auto& [key, path] : prefixes->properties)
            if (const auto prefix = absolutePath(&path); validKey(key) && !prefix.empty() && result.prefixes.size() < max_games) result.prefixes.emplace(key, prefix);
    if (const auto* named = root->get("prefix_info"); named && named->type == json::Value::Type::object)
        for (const auto& [path, entry] : named->properties) {
            const auto key = json::string(path);
            const auto prefix = absolutePath(&key);
            if (prefix.empty() || entry.type != json::Value::Type::object || result.prefix_info.size() >= max_games) continue;
            PrefixInfo info;
            if (const auto* name = entry.get("name"); name && name->type == json::Value::Type::string && name->text.size() <= 512)
                info.name = text_encoding::wideLenient(name->text);
            if (const auto* tool = entry.get("compat_tool"); tool && tool->type == json::Value::Type::string && !tool->text.empty() && tool->text.size() <= 512)
                info.compat_tool = tool->text;
            result.prefix_info.emplace(prefix, std::move(info));
        }
    return result;
}

std::string slug(std::wstring_view title, std::string_view key) {
    const auto make = [](std::wstring_view text) {
        std::string result;
        bool dash = false;
        for (const wchar_t ch : unicode_text::lower(unicode_text::decomposeCompatible(text))) {
            wchar_t plain = ch;
            if (ch == L'ł') plain = L'l';
            else if (ch == L'ø') plain = L'o';
            else if (ch == L'ß') { if (dash && !result.empty()) result += '-'; dash = false; result += "ss"; continue; }
            else if (ch == L'æ') plain = L'a';
            if ((plain >= L'a' && plain <= L'z') || (plain >= L'0' && plain <= L'9')) {
                if (dash && !result.empty()) result += '-';
                dash = false;
                result += static_cast<char>(plain);
            } else if (!unicode_text::isMark(ch) && ch != L'\'' && ch != L'’') dash = true;
            if (result.size() >= 64) break;
        }
        while (!result.empty() && result.back() == '-') result.pop_back();
        return result.substr(0, 64);
    };
    auto result = make(title);
    if (result.empty()) result = make(text_encoding::wideLenient(key));
    return result.empty() ? std::string("game") : result;
}

std::string umuStore(GameSource source) {
    switch (source) {
    case GameSource::epic: return "egs";
    case GameSource::gog: return "gog";
    case GameSource::amazon: return "amazon";
    case GameSource::ubisoft: return "ubisoft";
    case GameSource::ea: return "ea";
    case GameSource::battlenet: return "battlenet";
    case GameSource::steam: return "steam";
    default: return {};
    }
}

std::filesystem::path file() { return userConfigDirectory() / L"linux" / L"launch_options.json"; }

Document load() {
    std::lock_guard guard(lock());
    return loadUnlocked();
}
bool save(const Document& document) {
    std::lock_guard guard(lock());
    return saveUnlocked(document);
}

namespace {
// The game's overrides; a game without a list overrides whatever differs from the default.
std::set<std::string>& overridesOf(Document& document, const std::string& key) {
    if (const auto found = document.overrides.find(key); found != document.overrides.end()) return found->second;
    const auto game = document.games.find(key);
    return document.overrides[key] = game != document.games.end() ? differences(game->second, forGame(document.defaults)) : std::set<std::string>{};
}
// A new default: every game follows it, except in its overrides.
void rebase(Document& document, const LaunchOptions& defaults) {
    for (const auto& entry : document.games) overridesOf(document, entry.first);
    document.defaults = defaults;
    for (auto& [key, game] : document.games) game = inherit(document.defaults, game, document.overrides[key]);
}
}

LaunchOptions defaults() { return load().defaults; }
bool setDefaults(const LaunchOptions& options) {
    std::lock_guard guard(lock());
    auto document = loadUnlocked();
    auto defaults = options;
    defaults.prefix.clear(); // The default has no prefix of its own: each game gets one.
    rebase(document, defaults);
    return saveUnlocked(document);
}
std::string defaultTool() {
    const auto tool = defaults().compat_tool;
    return tool.empty() ? std::string("default") : tool;
}
bool setDefaultTool(const std::string& id) {
    std::lock_guard guard(lock());
    auto document = loadUnlocked();
    document.defaults.compat_tool = id.empty() ? std::string("default") : id;
    return saveUnlocked(document);
}
std::optional<LaunchOptions> gameOptions(const std::string& key) {
    const auto document = load();
    const auto found = document.games.find(key);
    return found == document.games.end() ? std::nullopt : std::optional<LaunchOptions>(found->second);
}
LaunchOptions effectiveOptions(const std::string& key) {
    const auto document = load();
    const auto found = document.games.find(key);
    return found == document.games.end() ? document.defaults : found->second;
}
bool setGameOptions(const std::string& key, const LaunchOptions& options) {
    if (!validKey(key)) return false;
    std::lock_guard guard(lock());
    auto document = loadUnlocked();
    const auto found = document.games.find(key);
    auto overrides = found != document.games.end() ? overridesOf(document, key) : std::set<std::string>{};
    const auto previous = found != document.games.end() ? found->second : forGame(document.defaults);
    for (const auto& id : differences(options, previous)) overrides.insert(id);
    // A variable neither the game nor the default has is nothing to override.
    std::erase_if(overrides, [&](const std::string& id) {
        return id.starts_with("env:") && !variableIn(options.environment, std::string_view(id).substr(4)) &&
               !variableIn(document.defaults.environment, std::string_view(id).substr(4));
    });
    if (overrides.empty()) { // Nothing of its own: it follows the default.
        if (found == document.games.end()) return true;
        document.games.erase(key);
        document.overrides.erase(key);
    } else {
        document.games[key] = inherit(document.defaults, options, overrides);
        document.overrides[key] = std::move(overrides);
    }
    return saveUnlocked(document);
}
bool resetGameOptions(const std::string& key) {
    std::lock_guard guard(lock());
    auto document = loadUnlocked();
    document.overrides.erase(key);
    if (!document.games.erase(key)) return true;
    return saveUnlocked(document);
}
std::set<std::string> gameOverrides(const std::string& key) {
    auto document = load();
    return document.games.contains(key) ? overridesOf(document, key) : std::set<std::string>{};
}
bool resetGameOption(const std::string& key, const std::string& id) {
    std::lock_guard guard(lock());
    auto document = loadUnlocked();
    const auto found = document.games.find(key);
    if (found == document.games.end()) return true;
    auto& overrides = overridesOf(document, key);
    if (!overrides.erase(id)) return true;
    if (overrides.empty()) {
        document.games.erase(found);
        document.overrides.erase(key);
    } else found->second = inherit(document.defaults, found->second, overrides);
    return saveUnlocked(document);
}

std::filesystem::path defaultPrefixRoot() {
    const auto home = system_info::environment(L"HOME", 4095);
    if (home && std::filesystem::path(*home).is_absolute()) return std::filesystem::path(*home) / L"Games" / L"launchme" / L"prefixes";
    return userConfigDirectory() / L"prefixes";
}
std::filesystem::path prefixRoot() { return rootOf(load()); }
bool setPrefixRoot(const std::filesystem::path& root) {
    if (!root.empty() && !root.is_absolute()) return false;
    std::lock_guard guard(lock());
    auto document = loadUnlocked();
    document.prefix_root = root.empty() ? std::filesystem::path{} : root.lexically_normal();
    return saveUnlocked(document);
}

std::filesystem::path gamePrefix(const Game& game) {
    const auto key = gameKey(game);
    std::lock_guard guard(lock());
    auto document = loadUnlocked();
    if (key.empty()) return rootOf(document) / L"default";
    if (const auto options = resolvedOptions(document, key); !options.prefix.empty()) return options.prefix;
    bool changed = false;
    const auto prefix = assign(document, game, key, changed);
    if (changed) saveUnlocked(document);
    return prefix;
}

std::optional<std::filesystem::path> existingPrefix(const std::string& key) {
    if (key.empty()) return std::nullopt;
    const auto document = load();
    if (const auto options = resolvedOptions(document, key); !options.prefix.empty()) return options.prefix;
    if (const auto found = document.prefixes.find(key); found != document.prefixes.end() && !found->second.empty()) return found->second;
    return std::nullopt;
}

std::vector<std::string> gamesUsingTool(const std::string& tool_id) {
    const auto document = load();
    std::vector<std::string> result;
    if (document.defaults.compat_tool == tool_id) result.push_back("default");
    for (const auto& [key, options] : document.games)
        if (options.compat_tool == tool_id) result.push_back(key);
    return result;
}

std::vector<std::string> gamesUsingPrefix(const std::filesystem::path& prefix) {
    const auto document = load();
    const auto normal = prefix.lexically_normal();
    std::vector<std::string> result;
    for (const auto& [key, path] : document.prefixes) {
        const auto found = document.games.find(key);
        const bool own = found != document.games.end() && !found->second.prefix.empty();
        if (!own && path.lexically_normal() == normal) result.push_back(key);
    }
    for (const auto& [key, options] : document.games)
        if (!options.prefix.empty() && options.prefix.lexically_normal() == normal && std::find(result.begin(), result.end(), key) == result.end())
            result.push_back(key);
    return result;
}

std::shared_ptr<const CompatLaunch> compatLaunch(const Game& game) {
    auto result = std::make_shared<CompatLaunch>();
    result->game_key = gameKey(game);
    result->title = game.name;
    result->store = umuStore(game.source);
    if (game.kind == GameKind::nonsteam) result->store.clear(); // A Steam shortcut: a program of its own.
    result->store_id = game.source == GameSource::steam ? (game.app_id ? std::to_string(game.app_id) : std::string{}) : game.source_id;
    result->steam_app_id = game.source == GameSource::steam && game.kind != GameKind::nonsteam ? game.app_id : 0;
    result->install_directory = game.install_directory;
    std::lock_guard guard(lock());
    auto document = loadUnlocked();
    result->options = resolvedOptions(document, result->game_key);
    if (!result->options.prefix.empty()) result->prefix = result->options.prefix;
    else if (result->game_key.empty()) result->prefix = rootOf(document) / L"default";
    else {
        bool changed = false;
        result->prefix = assign(document, game, result->game_key, changed);
        if (changed) saveUnlocked(document);
    }
    result->options.compat_tool = toolOf(document, result->game_key, result->prefix);
    return result;
}

CompatLaunch defaultCompatLaunch() {
    CompatLaunch result;
    const auto document = load();
    result.options = resolvedOptions(document, {});
    result.prefix = result.options.prefix.empty() ? rootOf(document) / L"default" : result.options.prefix;
    result.options.compat_tool = toolOf(document, {}, result.prefix);
    return result;
}

std::vector<PrefixEntry> prefixes() {
    const auto document = load();
    const auto root = rootOf(document);
    std::map<std::filesystem::path, PrefixEntry> found;
    const auto entry = [&](const std::filesystem::path& path) -> PrefixEntry& {
        const auto normal = path.lexically_normal();
        auto& item = found[normal];
        item.path = normal;
        return item;
    };
    for (const auto& [path, info] : document.prefix_info) { auto& item = entry(path); item.info = info; item.named = true; }
    for (const auto& [key, path] : document.prefixes) {
        const auto own = document.games.find(key);
        if (own != document.games.end() && !own->second.prefix.empty()) continue; // The game names another one.
        entry(path).games.push_back(key);
    }
    for (const auto& [key, options] : document.games)
        if (!options.prefix.empty()) entry(options.prefix).games.push_back(key);
    entry(root / L"default").shared = true;
    // Folders left in the root by games that went away (or made outside Launchme).
    std::error_code ec;
    for (std::filesystem::directory_iterator it(root, std::filesystem::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code type;
        if (it->is_directory(type) && !it->is_symlink(type)) entry(it->path());
    }
    std::vector<PrefixEntry> result;
    for (auto& [path, item] : found) result.push_back(std::move(item));
    std::stable_sort(result.begin(), result.end(), [](const PrefixEntry& a, const PrefixEntry& b) {
        if (a.named != b.named) return a.named;
        if (a.shared != b.shared) return a.shared;
        return a.path < b.path;
    });
    return result;
}

PrefixInfo prefixInfo(const std::filesystem::path& prefix) {
    const auto document = load();
    const auto found = document.prefix_info.find(prefix.lexically_normal());
    return found == document.prefix_info.end() ? PrefixInfo{} : found->second;
}

bool setPrefixInfo(const std::filesystem::path& prefix, const PrefixInfo& info) {
    if (!prefix.is_absolute() || info.name.size() > 256) return false;
    std::lock_guard guard(lock());
    auto document = loadUnlocked();
    auto stored = info;
    if (!chosen(stored.compat_tool)) stored.compat_tool = "default";
    document.prefix_info[prefix.lexically_normal()] = std::move(stored);
    return saveUnlocked(document);
}

std::filesystem::path newPrefix(const std::wstring& name, const std::string& compat_tool) {
    if (name.empty() || name.size() > 256) return {};
    std::lock_guard guard(lock());
    auto document = loadUnlocked();
    const auto root = rootOf(document);
    const auto base = slug(name, "prefix");
    for (int attempt = 1; attempt < 1000; ++attempt) {
        const auto candidate = (root / (attempt == 1 ? base : base + "-" + std::to_string(attempt))).lexically_normal();
        std::error_code ec;
        if ((attempt == 1 && base == "default") || document.prefix_info.contains(candidate) || takenByOther(document, candidate, {}) ||
            std::filesystem::exists(candidate, ec)) continue;
        if (!std::filesystem::create_directories(candidate, ec) || ec) return {};
        document.prefix_info[candidate] = PrefixInfo{name, chosen(compat_tool) ? compat_tool : std::string("default")};
        if (!saveUnlocked(document)) return {};
        return candidate;
    }
    return {};
}

bool forgetPrefix(const std::filesystem::path& prefix) {
    const auto normal = prefix.lexically_normal();
    std::lock_guard guard(lock());
    auto document = loadUnlocked();
    bool changed = document.prefix_info.erase(normal) > 0;
    changed |= std::erase_if(document.prefixes, [&](const auto& entry) { return entry.second.lexically_normal() == normal; }) > 0;
    for (auto& [key, options] : document.games)
        if (!options.prefix.empty() && options.prefix.lexically_normal() == normal) {
            overridesOf(document, key).erase("prefix"); // Back to its own prefix, as the default.
            options.prefix.clear();
            changed = true;
        }
    return !changed || saveUnlocked(document);
}

std::string effectiveTool(const std::string& key, const std::filesystem::path& prefix) { return toolOf(load(), key, prefix); }
}
