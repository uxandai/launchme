#include "steam_game_config.hpp"
#include "steam_registry.hpp"
#include "../net/json.hpp"
#include "../platform/ascii_text.hpp"
#include "../platform/atomic_file.hpp"
#include "../platform/file_io.hpp"
#include "../platform/text_encoding.hpp"

#include <algorithm>
#include <cctype>

namespace steam_game_config {
namespace fs = std::filesystem;
using steam_registry::Node;

namespace {
constexpr size_t max_file = 64 * 1024 * 1024; // localconfig.vdf grows with the library; tens of MB at most.

bool same(std::string_view a, std::string_view b) { return ascii_text::equalIgnoreCase(a, b); }

const Node* find(const Node& node, std::string_view key) {
    for (const auto& item : node.children) if (item.block && same(item.key, key)) return &item;
    return nullptr;
}
Node* child(Node& node, std::string_view key) {
    for (auto& item : node.children) if (item.block && same(item.key, key)) return &item;
    node.children.push_back({std::string(key), {}, true, {}});
    return &node.children.back();
}
const Node* path(const Node& root, std::initializer_list<std::string_view> keys) {
    const Node* node = &root;
    for (const auto key : keys) if (!(node = find(*node, key))) return nullptr;
    return node;
}
std::string valueOf(const Node* node, std::string_view key) {
    if (!node) return {};
    for (const auto& item : node->children) if (!item.block && same(item.key, key)) return item.value;
    return {};
}
void setValue(Node& node, std::string_view key, const std::string& value) {
    for (auto& item : node.children) if (!item.block && same(item.key, key)) { item.value = value; return; }
    node.children.push_back({std::string(key), value, false, {}});
}

// Steam's own layout, with the escapes steam_registry::parse reads back.
std::string vdfQuoted(std::string_view text) {
    std::string result = "\"";
    for (const char ch : text) {
        if (ch == '"' || ch == '\\') { result += '\\'; result += ch; }
        else if (ch == '\n') result += "\\n";
        else if (ch == '\t') result += "\\t";
        else result += ch;
    }
    return result + '"';
}
void format(const Node& node, int depth, std::string& out) {
    const std::string indent(static_cast<size_t>(depth), '\t');
    for (const auto& item : node.children) {
        if (item.block) {
            out += indent + vdfQuoted(item.key) + '\n' + indent + "{\n";
            format(item, depth + 1, out);
            out += indent + "}\n";
        } else out += indent + vdfQuoted(item.key) + "\t\t" + vdfQuoted(item.value) + '\n';
    }
}

// nullopt: unreadable; an empty tree for a missing file.
std::optional<Node> load(const fs::path& file) {
    std::error_code ec;
    if (!fs::exists(file, ec)) return Node{};
    const auto size = fs::file_size(file, ec);
    if (ec || size > max_file) return std::nullopt;
    const auto text = file_io::readText(file, max_file);
    if (text.size() != size) return std::nullopt;
    return steam_registry::parse(text);
}
bool save(const fs::path& file, const Node& root) {
    std::error_code ec;
    if (fs::exists(file, ec)) {
        auto backup = file;
        backup += ".launchme-backup";
        fs::copy_file(file, backup, fs::copy_options::overwrite_existing, ec);
        if (ec) return false;
    } else {
        fs::create_directories(file.parent_path(), ec);
        if (ec) return false;
    }
    std::string out;
    format(root, 0, out);
    return atomic_file::write(file, out);
}

// A JSON (and JavaScript) string literal.
std::string literal(std::string_view text) {
    std::string result = "\"";
    for (const unsigned char ch : text) {
        if (ch == '"' || ch == '\\') { result += '\\'; result += static_cast<char>(ch); }
        else if (ch < 0x20) {
            static constexpr char hex[] = "0123456789abcdef";
            result += "\\u00"; result += hex[ch >> 4]; result += hex[ch & 15];
        } else result += static_cast<char>(ch);
    }
    return result + '"';
}

// Words of the part before %command%, quotes kept ("VAR='a b'" is one word).
std::vector<std::string> words(std::string_view text) {
    std::vector<std::string> result;
    std::string word;
    char quote = 0;
    for (const char ch : text) {
        if (quote) { word += ch; if (ch == quote) quote = 0; continue; }
        if (ch == '"' || ch == '\'') { quote = ch; word += ch; continue; }
        if (ch == ' ' || ch == '\t') { if (!word.empty()) result.push_back(std::move(word)); word.clear(); continue; }
        word += ch;
    }
    if (!word.empty()) result.push_back(std::move(word));
    return result;
}
std::string trim(std::string_view text) {
    const auto first = text.find_first_not_of(" \t"), last = text.find_last_not_of(" \t");
    return first == std::string_view::npos ? std::string{} : std::string(text.substr(first, last - first + 1));
}
// "NAME=" of an environment word; empty for a wrapper.
std::string variable(std::string_view word) {
    const auto equals = word.find('=');
    if (equals == std::string_view::npos || equals == 0) return {};
    for (size_t i = 0; i < equals; ++i)
        if (!std::isalnum(static_cast<unsigned char>(word[i])) && word[i] != '_') return {};
    return std::string(word.substr(0, equals + 1));
}
struct Split { std::vector<std::string> before; std::string after; };
Split split(std::string_view options) {
    const auto command = options.find("%command%");
    if (command == std::string_view::npos) return {{}, trim(options)}; // Steam: options without %command% are the game's arguments.
    return {words(options.substr(0, command)), trim(options.substr(command + 9))};
}

const char* const read_function = R"((async (app) => {
  const details = await new Promise((resolve, reject) => {
    let registration = null, done = false;
    const timer = setTimeout(() => { if (registration) registration.unregister(); reject(new Error("timeout")); }, 5000);
    registration = SteamClient.Apps.RegisterForAppDetails(app, (data) => {
      if (done) return;
      done = true;
      clearTimeout(timer);
      setTimeout(() => { if (registration) registration.unregister(); }, 0);
      resolve(data);
    });
  });
  let tools = [];
  try {
    tools = (await SteamClient.Apps.GetAvailableCompatTools(app)).map((tool) => ({ name: String(tool.strToolName ?? ""), label: String(tool.strDisplayName ?? "") }));
  } catch (error) {}
  return JSON.stringify({ launch: String(details.strLaunchOptions ?? ""), tool: String(details.strCompatToolName ?? ""), tools });
}))";
}

fs::path localConfig(const fs::path& steam_root, uint64_t account) {
    const auto id = static_cast<uint32_t>(account & 0xFFFFFFFFu);
    if (steam_root.empty() || !id) return {};
    return steam_root / "userdata" / std::to_string(id) / "config" / "localconfig.vdf";
}

fs::path installConfig(const fs::path& steam_root) { return steam_root.empty() ? fs::path{} : steam_root / "config" / "config.vdf"; }

fs::path cefFlag(const fs::path& steam_root) { return steam_root.empty() ? fs::path{} : steam_root / ".cef-enable-remote-debugging"; }

std::optional<Settings> read(const fs::path& local_config, const fs::path& install_config, uint32_t app) {
    if (!app) return std::nullopt;
    const auto local = local_config.empty() ? std::optional<Node>(Node{}) : load(local_config);
    const auto install = install_config.empty() ? std::optional<Node>(Node{}) : load(install_config);
    if (!local || !install) return std::nullopt;
    const auto key = std::to_string(app);
    Settings result;
    if (const auto* apps = path(*local, {"UserLocalConfigStore", "Software", "Valve", "Steam", "apps"}))
        result.launch_options = valueOf(find(*apps, key), "LaunchOptions");
    if (const auto* mapping = path(*install, {"InstallConfigStore", "Software", "Valve", "Steam", "CompatToolMapping"}))
        result.compat_tool = valueOf(find(*mapping, key), "name");
    return result;
}

bool write(const fs::path& local_config, const fs::path& install_config, uint32_t app,
           const std::optional<std::string>& launch_options, const std::optional<std::string>& compat_tool) {
    if (!app) return false;
    const auto key = std::to_string(app);
    if (launch_options) {
        if (local_config.empty()) return false;
        auto root = load(local_config);
        if (!root) return false;
        Node* node = &*root;
        for (const auto* name : {"UserLocalConfigStore", "Software", "Valve", "Steam", "apps"}) node = child(*node, name);
        setValue(*child(*node, key), "LaunchOptions", *launch_options);
        if (!save(local_config, *root)) return false;
    }
    if (compat_tool) {
        if (install_config.empty()) return false;
        auto root = load(install_config);
        if (!root) return false;
        Node* node = &*root;
        for (const auto* name : {"InstallConfigStore", "Software", "Valve", "Steam", "CompatToolMapping"}) node = child(*node, name);
        auto& entries = node->children;
        if (compat_tool->empty()) {
            entries.erase(std::remove_if(entries.begin(), entries.end(), [&](const Node& item) { return item.block && item.key == key; }), entries.end());
        } else {
            auto& entry = *child(*node, key);
            setValue(entry, "name", *compat_tool);
            setValue(entry, "config", valueOf(&entry, "config"));
            setValue(entry, "priority", "250"); // What Steam writes for a tool the user picked.
        }
        if (!save(install_config, *root)) return false;
    }
    return true;
}

namespace {
std::string hexText(std::string_view text) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string result;
    for (const unsigned char ch : text) { result += digits[ch >> 4]; result += digits[ch & 15]; }
    return result;
}
std::optional<std::string> fromHex(std::string_view text) {
    if (text.size() % 2) return std::nullopt;
    const auto digit = [](char ch) { return ch >= '0' && ch <= '9' ? ch - '0' : ch >= 'a' && ch <= 'f' ? ch - 'a' + 10 : -1; };
    std::string result;
    for (size_t i = 0; i < text.size(); i += 2) {
        const int high = digit(text[i]), low = digit(text[i + 1]);
        if (high < 0 || low < 0) return std::nullopt;
        result += static_cast<char>(high * 16 + low);
    }
    return result;
}
}

// One line per setting: "<app> launch|tool <hex of the value>".
std::vector<Pending> loadPending(const fs::path& file) {
    std::vector<Pending> list;
    const auto text = file_io::readText(file, 1024 * 1024);
    size_t at = 0;
    while (at < text.size()) {
        auto end = text.find('\n', at);
        if (end == std::string::npos) end = text.size();
        const std::string_view line(text.data() + at, end - at);
        at = end + 1;
        const auto first = line.find(' '), second = first == std::string_view::npos ? first : line.find(' ', first + 1);
        if (second == std::string_view::npos) continue;
        const auto number = line.substr(0, first);
        if (number.empty() || number.size() > 10 || number.find_first_not_of("0123456789") != std::string_view::npos) continue;
        const auto app = std::stoull(std::string(number));
        const auto field = line.substr(first + 1, second - first - 1);
        const auto value = fromHex(line.substr(second + 1));
        if (!app || app > UINT32_MAX || !value || (field != "launch" && field != "tool")) continue;
        Pending change;
        change.app = static_cast<uint32_t>(app);
        (field == "launch" ? change.launch_options : change.compat_tool) = *value;
        queue(list, change);
    }
    return list;
}

bool savePending(const fs::path& file, const std::vector<Pending>& list) {
    std::string text;
    for (const auto& change : list) {
        if (change.launch_options) text += std::to_string(change.app) + " launch " + hexText(*change.launch_options) + "\n";
        if (change.compat_tool) text += std::to_string(change.app) + " tool " + hexText(*change.compat_tool) + "\n";
    }
    std::error_code ec;
    if (text.empty()) { fs::remove(file, ec); return !fs::exists(file, ec); }
    return atomic_file::write(file, text);
}

void queue(std::vector<Pending>& list, const Pending& change) {
    if (!change.app || (!change.launch_options && !change.compat_tool)) return;
    auto found = std::find_if(list.begin(), list.end(), [&](const Pending& item) { return item.app == change.app; });
    if (found == list.end()) { list.push_back(change); return; }
    if (change.launch_options) found->launch_options = change.launch_options;
    if (change.compat_tool) found->compat_tool = change.compat_tool;
}

std::string readExpression(uint32_t app) { return std::string(read_function) + "(" + std::to_string(app) + ")"; }

std::string writeExpression(uint32_t app, const std::optional<std::string>& launch_options, const std::optional<std::string>& compat_tool) {
    std::string body = "(async () => {\n";
    if (launch_options) body += "  SteamClient.Apps.SetAppLaunchOptions(" + std::to_string(app) + ", " + literal(*launch_options) + ");\n";
    if (compat_tool) body += "  SteamClient.Apps.SpecifyCompatTool(" + std::to_string(app) + ", " + literal(*compat_tool) + ");\n";
    body += "  await new Promise((resolve) => setTimeout(resolve, 400));\n";
    body += "  return await " + readExpression(app) + ";\n})()";
    return body;
}

std::optional<Settings> parseReply(std::string_view text) {
    const auto root = json::parse(text, 1024 * 1024);
    if (!root || root->type != json::Value::Type::object) return std::nullopt;
    const auto* launch = root->get("launch");
    const auto* tool = root->get("tool");
    if (!launch || launch->type != json::Value::Type::string || !tool || tool->type != json::Value::Type::string) return std::nullopt;
    Settings result;
    result.launch_options = launch->text;
    result.compat_tool = tool->text;
    if (const auto* tools = root->get("tools"); tools && tools->type == json::Value::Type::array)
        for (const auto& entry : tools->elements) {
            const auto* name = entry.get("name");
            const auto* label = entry.get("label");
            if (!name || name->type != json::Value::Type::string || name->text.empty() || name->text.size() > 200) continue;
            const auto shown = label && label->type == json::Value::Type::string && !label->text.empty() ? label->text : name->text;
            result.tools.push_back({name->text, text_encoding::wideLenient(shown)});
        }
    return result;
}

std::string internalName(const fs::path& folder) {
    const auto text = file_io::readText(folder / "compatibilitytool.vdf", 64 * 1024);
    if (!text.empty())
        if (const auto root = steam_registry::parse(text))
            if (const auto* list = path(*root, {"compatibilitytools", "compat_tools"}))
                for (const auto& tool : list->children) if (tool.block && !tool.key.empty()) return tool.key;
    // Valve's Proton in steamapps/common: "Proton 9.0" → proton_9, "Proton 6.3" → proton_63.
    const auto name = folder.filename().string();
    if (name == "Proton - Experimental") return "proton_experimental";
    if (name == "Proton Hotfix") return "proton_hotfix";
    if (name.starts_with("Proton ")) {
        auto version = name.substr(7);
        if (version.empty() || version.find_first_not_of("0123456789.") != std::string::npos) return {};
        if (version.ends_with(".0")) version.resize(version.size() - 2);
        version.erase(std::remove(version.begin(), version.end(), '.'), version.end());
        return version.empty() ? std::string{} : "proton_" + version;
    }
    return {};
}

const std::vector<Switch>& switches() {
    static const std::vector<Switch> list{
        {"nvapi", "PROTON_ENABLE_NVAPI=1", {L"NVAPI i DLSS", L"NVAPI and DLSS"},
         {L"Karty NVIDIA: DLSS, Reflex i PhysX tam, gdzie gra o nie pyta.", L"NVIDIA cards: DLSS, Reflex and PhysX where the game asks for them."}},
        {"wayland", "PROTON_ENABLE_WAYLAND=1", {L"Wayland", L"Wayland"},
         {L"Okno gry bez XWayland (Proton 10 i nowsze).", L"The game's window without XWayland (Proton 10 and newer)."}},
        {"hdr", "PROTON_ENABLE_HDR=1", {L"HDR", L"HDR"},
         {L"HDR w grach, które go mają (razem z Waylandem albo gamescope).", L"HDR in games that have it (with Wayland or gamescope)."}},
        {"dxr", "VKD3D_CONFIG=dxr", {L"Ray tracing w DX12", L"DX12 ray tracing"},
         {L"VKD3D-Proton włącza DXR, gdy karta go obsługuje.", L"VKD3D-Proton turns DXR on when the card supports it."}},
        {"wined3d", "PROTON_USE_WINED3D=1", {L"WineD3D zamiast DXVK", L"WineD3D instead of DXVK"},
         {L"Na starsze karty i gry, które z DXVK się psują.", L"For older cards and games DXVK breaks."}},
        {"no_esync", "PROTON_NO_ESYNC=1", {L"Bez esync", L"No esync"}, {L"Gdy gra się zawiesza albo nie startuje.", L"When the game hangs or does not start."}},
        {"no_fsync", "PROTON_NO_FSYNC=1", {L"Bez fsync", L"No fsync"}, {L"Gdy gra się zawiesza albo nie startuje.", L"When the game hangs or does not start."}},
        {"log", "PROTON_LOG=1", {L"Log Protona", L"Proton log"}, {L"Zapisuje ~/steam-<AppID>.log przy każdym uruchomieniu.", L"Writes ~/steam-<AppID>.log on every launch."}},
        {"gamemode", "gamemoderun", {L"GameMode", L"GameMode"}, {L"Feral GameMode na czas gry (gamemoderun).", L"Feral GameMode while the game runs (gamemoderun)."}},
        {"mangohud", "mangohud", {L"MangoHud", L"MangoHud"}, {L"Nakładka z FPS i obciążeniem (mangohud).", L"An overlay with FPS and load (mangohud)."}},
    };
    return list;
}

bool has(std::string_view options, const Switch& item) {
    const auto parts = split(options);
    return std::find(parts.before.begin(), parts.before.end(), item.word) != parts.before.end();
}

std::string toggle(std::string_view options, const Switch& item, bool on) {
    auto [before, after] = split(options);
    const auto name = variable(item.word);
    before.erase(std::remove_if(before.begin(), before.end(), [&](const std::string& word) {
        return name.empty() ? word == item.word : word.starts_with(name);
    }), before.end());
    if (on) {
        if (name.empty()) before.push_back(item.word); // Wrappers: just before %command%.
        else {
            // Variables: after the ones already leading, before any wrapper.
            auto at = std::find_if(before.begin(), before.end(), [](const std::string& word) { return variable(word).empty(); });
            before.insert(at, item.word);
        }
    }
    if (before.empty()) return after;
    std::string result;
    for (const auto& word : before) result += word + ' ';
    result += "%command%";
    if (!after.empty()) result += ' ' + after;
    return result;
}
}
