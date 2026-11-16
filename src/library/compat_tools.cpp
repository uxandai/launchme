#include "compat_tools.hpp"
#include "steam.hpp"
#include "steam_registry.hpp"
#include "xdg_paths.hpp"
#include "../net/http_download.hpp"
#include "../net/json.hpp"
#include "../platform/atomic_file.hpp"
#include "../platform/file_io.hpp"
#include "../platform/system.hpp"
#include "../platform/text_encoding.hpp"
#include "../platform/user_config.hpp"

#include <archive.h>
#include <archive_entry.h>
#include <openssl/evp.h>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <sys/stat.h>
#include <tuple>
#include <unistd.h>
#include <utility>

namespace compat_tools {
namespace {
namespace fs = std::filesystem;

constexpr std::string_view marker_file = ".launchme-release";

bool isFile(const fs::path& path) { std::error_code ec; return fs::is_regular_file(path, ec); }
bool isFolder(const fs::path& path) { std::error_code ec; return fs::is_directory(path, ec); }
std::string lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return text;
}
std::string utf8(const fs::path& path) { return text_encoding::utf8Lenient(path.wstring()); }
std::string name(const fs::path& path) { return utf8(path.filename()); }

std::string readSmall(const fs::path& file, size_t limit = 64 * 1024) {
    std::ifstream input(file, std::ios::binary);
    if (!input) return {};
    std::string text(limit, '\0');
    input.read(text.data(), static_cast<std::streamsize>(limit));
    text.resize(static_cast<size_t>(input.gcount()));
    return text;
}
int64_t modifiedAt(const fs::path& path) {
    struct stat info{};
    return ::stat(path.c_str(), &info) == 0 ? static_cast<int64_t>(info.st_mtime) : 0;
}
std::vector<fs::path> folders(const fs::path& parent) {
    std::vector<fs::path> result;
    std::error_code ec;
    for (fs::directory_iterator it(parent, ec), end; !ec && it != end; it.increment(ec))
        if (it->is_directory(ec) && !name(it->path()).starts_with('.')) result.push_back(it->path());
    std::sort(result.begin(), result.end());
    return result;
}
fs::path canonicalOr(const fs::path& path) {
    std::error_code ec;
    auto result = fs::canonical(path, ec);
    return ec ? path.lexically_normal() : result;
}
bool executable(const fs::path& path) { return isFile(path) && ::access(path.c_str(), X_OK) == 0; }

// compatibilitytool.vdf: compatibilitytools/compat_tools/<internal name>/display_name.
std::string displayName(const fs::path& folder) {
    const auto text = readSmall(folder / "compatibilitytool.vdf");
    if (text.empty()) return {};
    const auto root = steam_registry::parse(text);
    if (!root) return {};
    const auto child = [](const steam_registry::Node& node, std::string_view key) -> const steam_registry::Node* {
        for (const auto& entry : node.children) if (lower(entry.key) == key) return &entry;
        return nullptr;
    };
    const auto* tools = child(*root, "compatibilitytools");
    const auto* list = tools ? child(*tools, "compat_tools") : nullptr;
    if (!list) return {};
    for (const auto& tool : list->children)
        if (tool.block)
            if (const auto* display = child(tool, "display_name"); display && !display->value.empty()) return display->value;
    return {};
}
// `version`: "<build time> GE-Proton11-11".
std::string versionFile(const fs::path& folder) {
    auto text = readSmall(folder / "version", 4096);
    text = text.substr(0, text.find('\n'));
    while (!text.empty() && (text.back() == '\r' || text.back() == ' ')) text.pop_back();
    const auto space = text.find(' ');
    return space == std::string::npos ? text : text.substr(space + 1);
}
std::string releaseMarker(const fs::path& folder) {
    auto text = readSmall(folder / marker_file, 1024);
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.pop_back();
    return text;
}

std::string protonFamily(const std::string& folder, Origin origin) {
    const auto text = lower(folder);
    if (text.starts_with("ge-proton") || (text.starts_with("proton-") && text.find("-ge-") != std::string::npos)) return "GE-Proton";
    if (text.starts_with("umu-proton")) return "UMU-Proton";
    if (text.find("cachyos") != std::string::npos) return "Proton-CachyOS";
    (void)origin;
    return "Proton";
}
std::string wineFamily(const std::string& folder, bool& deprecated) {
    const auto text = lower(folder);
    deprecated = false;
    if (text.find("lutris-ge") != std::string::npos || text.find("wine-ge") != std::string::npos ||
        (text.find("-ge-") != std::string::npos && text.find("proton") != std::string::npos)) { deprecated = true; return "Wine-GE"; }
    if (text.find("tkg") != std::string::npos) return "Wine-Staging-TkG";
    if (text.find("staging") != std::string::npos) return "Wine-Staging";
    return "Wine";
}

std::optional<Tool> protonTool(const fs::path& folder, Origin origin, const char* prefix, bool removable) {
    if (!isFile(folder / "proton")) return std::nullopt;
    Tool tool;
    tool.kind = Kind::proton;
    tool.origin = origin;
    tool.path = folder;
    tool.proton = folder / "proton";
    for (const auto* dist : {"files", "dist"})
        if (isFile(folder / dist / "bin" / "wine")) {
            tool.wine = folder / dist / "bin" / "wine";
            tool.wineserver = folder / dist / "bin" / "wineserver";
            break;
        }
    const auto folder_name = name(folder);
    const auto display = displayName(folder);
    tool.name = text_encoding::wideLenient(display.empty() ? folder_name : display);
    tool.version = versionFile(folder);
    if (tool.version.empty()) tool.version = folder_name;
    tool.family = protonFamily(folder_name, origin);
    if (tool.family == "Proton" && (lower(tool.version).starts_with("ge-proton") || lower(display).starts_with("ge-proton"))) tool.family = "GE-Proton";
    tool.id = std::string(prefix) + ":" + folder_name;
    tool.release = releaseMarker(folder);
    tool.removable = removable;
    tool.modified = static_cast<uint64_t>(modifiedAt(folder));
    return tool;
}
std::optional<Tool> wineTool(const fs::path& folder, Origin origin, const char* prefix, bool removable) {
    if (!isFile(folder / "bin" / "wine")) return std::nullopt;
    Tool tool;
    tool.kind = Kind::wine;
    tool.origin = origin;
    tool.path = folder;
    tool.wine = folder / "bin" / "wine";
    tool.wineserver = folder / "bin" / "wineserver";
    const auto folder_name = name(folder);
    tool.name = text_encoding::wideLenient(folder_name);
    tool.version = folder_name;
    tool.family = wineFamily(folder_name, tool.deprecated);
    tool.id = std::string(prefix) + ":" + folder_name;
    tool.release = releaseMarker(folder);
    tool.removable = removable;
    tool.modified = static_cast<uint64_t>(modifiedAt(folder));
    return tool;
}
std::optional<Tool> componentTool(const fs::path& folder) {
    const auto folder_name = name(folder);
    const auto text = lower(folder_name);
    Tool tool;
    std::string_view head;
    if (text.starts_with("dxvk-nvapi-")) { tool.family = "DXVK-NVAPI"; head = "dxvk-nvapi-"; }
    else if (text.starts_with("vkd3d-proton-")) { tool.family = "VKD3D-Proton"; head = "vkd3d-proton-"; }
    else if (text.starts_with("dxvk-")) { tool.family = "DXVK"; head = "dxvk-"; }
    else return std::nullopt;
    if (!isFolder(folder / "x64")) return std::nullopt;
    tool.kind = Kind::component;
    tool.origin = Origin::launchme;
    tool.path = folder;
    tool.version = folder_name.substr(head.size());
    if (!tool.version.empty() && tool.version[0] == 'v') tool.version.erase(0, 1);
    tool.name = text_encoding::wideLenient(tool.family + " " + tool.version);
    tool.id = "component:" + folder_name;
    tool.release = releaseMarker(folder);
    tool.removable = true;
    tool.modified = static_cast<uint64_t>(modifiedAt(folder));
    return tool;
}

int familyRank(const Tool& tool) {
    if (tool.kind == Kind::proton) {
        if (tool.family == "GE-Proton") return 0;
        if (tool.family == "UMU-Proton") return 1;
        if (tool.family == "Proton-CachyOS") return 2;
        return tool.origin == Origin::steam_official ? 3 : 4;
    }
    if (tool.kind == Kind::wine) {
        if (tool.origin == Origin::system) return 9;
        if (tool.deprecated) return 8;
        return tool.family == "Wine-Staging-TkG" ? 5 : tool.family == "Wine-Staging" ? 6 : 7;
    }
    return 10;
}
int kindRank(Kind kind) { return kind == Kind::proton ? 0 : kind == Kind::wine ? 1 : kind == Kind::component ? 2 : 3; }

std::vector<fs::path> userSteamCompatFolders(const Roots& roots) {
    std::vector<fs::path> result;
    if (!roots.home.empty()) result.push_back(roots.home / ".steam" / "root" / "compatibilitytools.d");
    for (const auto& root : roots.steam_roots) result.push_back(root / "compatibilitytools.d");
    return result;
}

std::wstring wide(std::string_view text) { return text_encoding::wideLenient(text); }

// GitHub release downloads: github.com, then its asset hosts.
bool githubUrl(std::wstring_view url) {
    if (!url.starts_with(L"https://")) return false;
    auto host = url.substr(8);
    host = host.substr(0, std::min(host.find_first_of(L"/?#"), host.size()));
    if (host.find_first_of(L"@:") != std::wstring_view::npos) return false;
    return host == L"github.com" || host == L"raw.githubusercontent.com" || host == L"objects.githubusercontent.com" ||
           host == L"release-assets.githubusercontent.com" || (host.size() > 24 && host.ends_with(L".githubusercontent.com"));
}

std::string stripArchive(std::string text) {
    if (text.ends_with(".part")) text.resize(text.size() - 5);
    for (const auto* suffix : {".tar.gz", ".tar.xz", ".tar.zst", ".tgz", ".tar"})
        if (text.ends_with(suffix)) { text.resize(text.size() - std::string_view(suffix).size()); break; }
    return text;
}
std::string withoutV(std::string tag) { return !tag.empty() && (tag[0] == 'v' || tag[0] == 'V') ? tag.substr(1) : tag; }

// Where Launchme's install of a release lands (also the name other managers use for GE/UMU-Proton).
std::string expectedFolder(const Release& release) {
    if (release.source == "ge-proton" || release.source == "umu-proton") return release.tag;
    if (release.source == "umu-launcher") return "umu-launcher-" + release.tag;
    return stripArchive(release.asset);
}

std::string randomHex() {
    uint8_t bytes[8]{};
    if (!system_info::randomBytes(bytes, sizeof(bytes))) {
        const auto now = static_cast<uint64_t>(std::time(nullptr)) ^ static_cast<uint64_t>(::getpid());
        std::memcpy(bytes, &now, sizeof(bytes));
    }
    static constexpr char digits[] = "0123456789abcdef";
    std::string result;
    for (const auto byte : bytes) { result += digits[byte >> 4]; result += digits[byte & 15]; }
    return result;
}

// Every symlink below `root` resolves inside it (dangling ones by their path).
bool linksInside(const fs::path& root) {
    std::error_code ec;
    const auto base = fs::canonical(root, ec);
    if (ec) return false;
    const auto inside = [&](const fs::path& path) {
        const auto relative = path.lexically_relative(base);
        return !relative.empty() && *relative.begin() != "..";
    };
    for (fs::recursive_directory_iterator it(base, fs::directory_options::none, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_symlink(ec)) continue;
        const auto target = fs::read_symlink(it->path(), ec);
        if (ec || target.is_absolute()) return false;
        const auto resolved = fs::weakly_canonical(it->path().parent_path() / target, ec);
        if (ec || !inside(resolved)) return false;
    }
    return !ec;
}

// A relative archive path ("./a/b") as its clean form; nullopt when absolute, empty or with "..".
std::optional<fs::path> safeRelative(std::string_view raw) {
    if (raw.empty() || raw.front() == '/' || raw.find('\0') != std::string_view::npos) return std::nullopt;
    fs::path result;
    for (const auto& part : fs::path(std::string(raw))) {
        const auto text = part.string();
        if (text.empty() || text == ".") continue;
        if (text == "..") return std::nullopt;
        result /= part;
    }
    if (result.empty()) return fs::path(".");
    return result;
}
}

Roots systemRoots() {
    Roots roots;
    roots.config = userConfigDirectory();
    roots.home = xdg_paths::home();
    std::vector<fs::path> candidates;
    if (const auto steam = xdg_paths::variable(L"STEAM_DIR"); !steam.empty()) candidates.push_back(steam);
    if (!roots.home.empty()) {
        const auto data = xdg_paths::dataHome();
        candidates.insert(candidates.end(), {roots.home / ".steam" / "root", roots.home / ".steam" / "steam",
            data / "Steam", roots.home / ".local" / "share" / "Steam",
            roots.home / ".var" / "app" / "com.valvesoftware.Steam" / ".local" / "share" / "Steam",
            roots.home / ".var" / "app" / "com.valvesoftware.Steam" / "data" / "Steam",
            roots.home / "snap" / "steam" / "common" / ".local" / "share" / "Steam"});
    }
    std::set<fs::path> seen;
    for (const auto& candidate : candidates) {
        if (!isFolder(candidate / "steamapps")) continue;
        const auto real = canonicalOr(candidate);
        if (seen.insert(real).second) roots.steam_roots.push_back(real);
    }
    std::set<fs::path> libraries;
    for (const auto& root : roots.steam_roots)
        for (const auto& library : steamLibraryFolders(root)) {
            const auto real = canonicalOr(library);
            if (libraries.insert(real).second) roots.steam_libraries.push_back(real);
        }
    roots.system_compat = {"/usr/share/steam/compatibilitytools.d", "/usr/local/share/steam/compatibilitytools.d"};
    roots.system_wine = xdg_paths::findProgram({L"wine", L"wine64"});
    roots.system_wineserver = xdg_paths::findProgram({L"wineserver"});
    if (roots.system_wineserver.empty())
        for (const auto* candidate : {"/usr/lib/wine/wineserver64", "/usr/lib/wine/wineserver", "/usr/lib64/wine/wineserver", "/usr/libexec/wine/wineserver"})
            if (executable(candidate)) { roots.system_wineserver = candidate; break; }
    roots.umu_run = xdg_paths::findProgram({L"umu-run"});
    if (roots.umu_run.empty() && !xdg_paths::findProgram({L"python3"}).empty()) {
        // Launchme's own zipapp (Downloads → umu-launcher), the newest one.
        std::vector<fs::path> own;
        for (const auto& folder : folders(toolsFolder(roots.config)))
            if (name(folder).starts_with("umu-launcher") && executable(folder / "umu-run")) own.push_back(folder / "umu-run");
        std::sort(own.begin(), own.end(), [](const fs::path& a, const fs::path& b) { return compareVersions(name(a.parent_path()), name(b.parent_path())) > 0; });
        if (!own.empty()) roots.umu_run = own.front();
    }
    return roots;
}

fs::path toolsFolder(const fs::path& config) { return config / "compatibilitytools"; }
fs::path componentsFolder(const fs::path& config) { return toolsFolder(config) / "components"; }

fs::path steamCompatFolder(const Roots& roots) {
    if (!roots.home.empty() && isFolder(roots.home / ".steam" / "root")) return roots.home / ".steam" / "root" / "compatibilitytools.d";
    if (!roots.steam_roots.empty()) return roots.steam_roots.front() / "compatibilitytools.d";
    return {};
}

std::vector<Tool> discover(const Roots& roots) {
    std::vector<Tool> result;
    std::set<fs::path> seen;
    std::set<std::string> ids;
    const auto add = [&](std::optional<Tool> tool) {
        if (!tool) return;
        if (!tool->path.empty() && !seen.insert(canonicalOr(tool->path)).second) return;
        auto id = tool->id;
        for (int n = 2; ids.contains(id); ++n) id = tool->id + "#" + std::to_string(n);
        tool->id = id;
        ids.insert(id);
        result.push_back(std::move(*tool));
    };
    // Launchme's own tools, components and umu-launcher.
    if (!roots.config.empty()) {
        for (const auto& folder : folders(toolsFolder(roots.config))) {
            const auto folder_name = name(folder);
            if (folder_name == "components" || folder_name == "cache" || folder_name == "downloads" || folder_name == "runtimes") continue;
            if (folder_name.starts_with("umu-launcher") && isFile(folder / "umu-run")) {
                Tool tool;
                tool.kind = Kind::umu;
                tool.origin = Origin::launchme;
                tool.family = "umu-launcher";
                tool.path = folder;
                tool.version = folder_name.substr(std::min<size_t>(folder_name.size(), 13));
                tool.name = text_encoding::wideLenient("umu-launcher " + tool.version);
                tool.id = "launchme:" + folder_name;
                tool.release = releaseMarker(folder);
                tool.removable = true;
                tool.modified = static_cast<uint64_t>(modifiedAt(folder));
                add(std::move(tool));
                continue;
            }
            if (auto proton = protonTool(folder, Origin::launchme, "launchme", true)) add(std::move(proton));
            else add(wineTool(folder, Origin::launchme, "launchme", true));
        }
        for (const auto& folder : folders(componentsFolder(roots.config))) add(componentTool(folder));
    }
    // Steam's compatibilitytools.d (ProtonUp-Qt, Heroic and Faugus put theirs there too), then the system's.
    for (const auto& folder : userSteamCompatFolders(roots))
        for (const auto& tool : folders(folder)) add(protonTool(tool, Origin::steam_compat, "steam", true));
    for (const auto& folder : roots.system_compat)
        for (const auto& tool : folders(folder)) add(protonTool(tool, Origin::steam_system, "steam-system", false));
    // Steam's own Protons (Proton 10.0, Experimental, Hotfix) in every library.
    for (const auto& library : roots.steam_libraries)
        for (const auto& tool : folders(library / "steamapps" / "common"))
            if (name(tool).starts_with("Proton")) add(protonTool(tool, Origin::steam_official, "steam-common", false));
    // Heroic's and Lutris' downloads (native and Flatpak).
    if (!roots.home.empty()) {
        for (const auto& base : {roots.home / ".config" / "heroic" / "tools", roots.home / ".var" / "app" / "com.heroicgameslauncher.hgl" / "config" / "heroic" / "tools"}) {
            for (const auto& tool : folders(base / "proton")) add(protonTool(tool, Origin::heroic, "heroic", false));
            for (const auto& tool : folders(base / "wine")) add(wineTool(tool, Origin::heroic, "heroic", false));
        }
        for (const auto& base : {roots.home / ".local" / "share" / "lutris" / "runners", roots.home / ".var" / "app" / "net.lutris.Lutris" / "data" / "lutris" / "runners"}) {
            for (const auto& tool : folders(base / "proton")) add(protonTool(tool, Origin::lutris, "lutris", false));
            for (const auto& tool : folders(base / "wine")) add(wineTool(tool, Origin::lutris, "lutris", false));
        }
    }
    // Wine from the system's packages.
    if (!roots.system_wine.empty()) {
        Tool tool;
        tool.kind = Kind::wine;
        tool.origin = Origin::system;
        tool.family = "Wine";
        tool.wine = roots.system_wine;
        tool.wineserver = roots.system_wineserver;
        tool.name = L"Wine";
        tool.version = "system";
        tool.id = "system:wine";
        add(std::move(tool));
    }
    // umu-launcher downloads (and updates) these itself.
    if (!roots.umu_run.empty()) {
        for (const auto& [id, family, protonpath, label] : {std::tuple{"umu:GE-Proton", "GE-Proton", "GE-Proton", L"GE-Proton (umu)"},
                                                            std::tuple{"umu:UMU-Proton", "UMU-Proton", "", L"UMU-Proton (umu)"}}) {
            Tool tool;
            tool.kind = Kind::proton;
            tool.origin = Origin::umu;
            tool.family = family;
            tool.name = label;
            tool.version = "latest";
            tool.id = id;
            tool.umu_protonpath = protonpath;
            add(std::move(tool));
        }
    }
    std::stable_sort(result.begin(), result.end(), [](const Tool& a, const Tool& b) {
        if (kindRank(a.kind) != kindRank(b.kind)) return kindRank(a.kind) < kindRank(b.kind);
        const bool auto_a = a.origin == Origin::umu, auto_b = b.origin == Origin::umu;
        if (auto_a != auto_b) return auto_b;
        if (a.kind == Kind::component && a.family != b.family) return a.family < b.family;
        if (familyRank(a) != familyRank(b)) return familyRank(a) < familyRank(b);
        const auto order = compareVersions(a.version, b.version);
        if (order) return order > 0;
        return a.id < b.id;
    });
    return result;
}

std::vector<Tool> runners(const std::vector<Tool>& tools) {
    std::vector<Tool> result;
    std::copy_if(tools.begin(), tools.end(), std::back_inserter(result), [](const Tool& tool) { return tool.kind == Kind::proton || tool.kind == Kind::wine; });
    return result;
}

std::optional<Tool> automatic(const std::vector<Tool>& tools) {
    const auto best = [&](auto&& accept) -> std::optional<Tool> {
        const Tool* chosen = nullptr;
        for (const auto& tool : tools)
            if (accept(tool) && (!chosen || compareVersions(tool.version, chosen->version) > 0)) chosen = &tool;
        return chosen ? std::optional<Tool>(*chosen) : std::nullopt;
    };
    const auto real = [](const Tool& tool) { return tool.kind == Kind::proton && tool.origin != Origin::umu; };
    if (auto tool = best([&](const Tool& t) { return real(t) && t.family == "GE-Proton"; })) return tool;
    if (auto tool = best([&](const Tool& t) { return real(t) && t.family == "UMU-Proton"; })) return tool;
    for (const auto& tool : tools) if (tool.id == "umu:UMU-Proton") return tool;
    // Steam's numbered Proton ("Proton 10.0", by its folder) before Experimental and Hotfix.
    const Tool* valve = nullptr;
    for (const auto& tool : tools) {
        const auto folder = name(tool.path);
        if (!real(tool) || tool.origin != Origin::steam_official || std::none_of(folder.begin(), folder.end(), [](unsigned char ch) { return std::isdigit(ch); })) continue;
        if (!valve || compareVersions(folder, name(valve->path)) > 0) valve = &tool;
    }
    if (valve) return *valve;
    if (auto tool = best([&](const Tool& t) { return real(t) && t.origin == Origin::steam_official; })) return tool;
    if (auto tool = best(real)) return tool;
    if (auto tool = best([](const Tool& t) { return t.kind == Kind::wine && t.origin != Origin::system && !t.deprecated; })) return tool;
    if (auto tool = best([](const Tool& t) { return t.kind == Kind::wine && t.origin != Origin::system; })) return tool;
    return best([](const Tool& t) { return t.kind == Kind::wine; });
}

std::optional<Tool> resolve(const std::vector<Tool>& tools, std::string_view id, bool* fell_back) {
    if (fell_back) *fell_back = false;
    if (!id.empty() && id != "default") {
        for (const auto& tool : tools)
            if (tool.id == id && (tool.kind == Kind::proton || tool.kind == Kind::wine)) return tool;
        if (fell_back) *fell_back = true;
    }
    return automatic(tools);
}

std::optional<Tool> component(const std::vector<Tool>& tools, std::string_view family) {
    const Tool* chosen = nullptr;
    for (const auto& tool : tools)
        if (tool.kind == Kind::component && tool.family == family && (!chosen || compareVersions(tool.version, chosen->version) > 0)) chosen = &tool;
    return chosen ? std::optional<Tool>(*chosen) : std::nullopt;
}

fs::path steamApp(const Roots& roots, std::wstring_view folder) {
    for (const auto& library : roots.steam_libraries)
        if (const auto path = library / "steamapps" / "common" / folder; isFolder(path)) return path;
    return {};
}

int compareVersions(std::string_view a, std::string_view b) {
    const auto numbers = [](std::string_view text) {
        std::vector<uint64_t> result;
        for (size_t i = 0; i < text.size();) {
            if (!std::isdigit(static_cast<unsigned char>(text[i]))) { ++i; continue; }
            uint64_t value = 0;
            while (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i])))
                value = std::min<uint64_t>(value * 10 + static_cast<uint64_t>(text[i++] - '0'), UINT64_MAX / 16);
            result.push_back(value);
        }
        return result;
    };
    const auto left = numbers(a), right = numbers(b);
    for (size_t i = 0; i < std::min(left.size(), right.size()); ++i)
        if (left[i] != right[i]) return left[i] < right[i] ? -1 : 1;
    if (left.size() != right.size()) return left.size() < right.size() ? -1 : 1;
    return a.compare(b) < 0 ? -1 : a.compare(b) > 0 ? 1 : 0;
}

// umu-database.
fs::path umuDatabaseFile(const fs::path& config) { return toolsFolder(config) / "cache" / "umu-database.csv"; }

std::string umuId(std::string_view csv, std::string_view store, std::string_view codename) {
    if (store.empty() || codename.empty()) return {};
    auto wanted_code = std::string(codename);
    if (store == "amazon" && wanted_code.starts_with("amzn1.adg.product.")) wanted_code.erase(0, 18);
    const auto wanted_store = lower(std::string(store));
    const auto wanted_folded = lower(wanted_code);
    std::string exact, folded;
    bool header = true;
    // One row: TITLE,STORE,CODENAME,UMU_ID,...; an exact code wins over one that differs in case.
    const auto row = [&](std::vector<std::string>& fields) {
        if (std::exchange(header, false) || fields.size() < 4 || lower(fields[1]) != wanted_store || !exact.empty()) return;
        auto code = fields[2];
        if (wanted_store == "amazon" && code.starts_with("amzn1.adg.product.")) code.erase(0, 18);
        if (code == wanted_code) exact = fields[3];
        else if (folded.empty() && lower(code) == wanted_folded) folded = fields[3];
    };
    // RFC 4180: quoted fields may hold commas, "" and line breaks.
    std::vector<std::string> fields;
    std::string field;
    bool quoted = false;
    for (size_t i = 0; i < csv.size(); ++i) {
        const char ch = csv[i];
        if (quoted) {
            if (ch != '"') field += ch;
            else if (i + 1 < csv.size() && csv[i + 1] == '"') { field += '"'; ++i; }
            else quoted = false;
        } else if (ch == '"') quoted = true;
        else if (ch == ',') fields.push_back(std::exchange(field, {}));
        else if (ch == '\n') {
            fields.push_back(std::exchange(field, {}));
            row(fields);
            fields.clear();
        } else if (ch != '\r') field += ch;
    }
    if (!field.empty() || !fields.empty()) {
        fields.push_back(std::move(field));
        row(fields);
    }
    return exact.empty() ? folded : exact;
}

std::string gameId(const CompatLaunch& launch, const fs::path& config) {
    if (!launch.options.umu_id.empty()) return launch.options.umu_id;
    if (launch.store == "steam" && launch.steam_app_id) return "umu-" + std::to_string(launch.steam_app_id);
    if (!launch.store.empty() && !launch.store_id.empty()) {
        const auto csv = readSmall(umuDatabaseFile(config), 8 * 1024 * 1024);
        if (auto id = umuId(csv, launch.store, launch.store_id); !id.empty() && id.size() <= 128) return id;
    }
    return "umu-default";
}

bool refreshUmuDatabase(const fs::path& config, bool force) {
    const auto file = umuDatabaseFile(config);
    if (!force && isFile(file) && std::time(nullptr) - modifiedAt(file) < 24 * 3600) return true;
    const auto response = httpGet(L"https://raw.githubusercontent.com/Open-Wine-Components/umu-database/main/umu-database.csv", L"", 8 * 1024 * 1024);
    if (response.status != 200 || response.truncated || response.body.size() < 6 ||
        std::string_view(reinterpret_cast<const char*>(response.body.data()), 6) != "TITLE,") return false;
    return atomic_file::write(file, std::span<const uint8_t>(response.body));
}

// Releases.
namespace {
constexpr Source source_table[] = {
    {"ge-proton", L"GE-Proton", "GloriousEggroll/proton-ge-custom", Kind::proton, "GE-Proton"},
    {"umu-proton", L"UMU-Proton", "Open-Wine-Components/umu-proton", Kind::proton, "UMU-Proton"},
    {"proton-cachyos", L"Proton-CachyOS", "CachyOS/proton-cachyos", Kind::proton, "Proton-CachyOS"},
    {"wine-staging-tkg-kron4ek", L"Wine-Staging-TkG (Kron4ek)", "Kron4ek/Wine-Builds", Kind::wine, "Wine-Staging-TkG"},
    {"wine-kron4ek", L"Wine (Kron4ek)", "Kron4ek/Wine-Builds", Kind::wine, "Wine"},
    {"dxvk", L"DXVK", "doitsujin/dxvk", Kind::component, "DXVK"},
    {"vkd3d-proton", L"VKD3D-Proton", "HansKristian-Work/vkd3d-proton", Kind::component, "VKD3D-Proton"},
    {"dxvk-nvapi", L"DXVK-NVAPI", "jp7677/dxvk-nvapi", Kind::component, "DXVK-NVAPI"},
    {"umu-launcher", L"umu-launcher", "Open-Wine-Components/umu-launcher", Kind::umu, "umu-launcher"},
};
struct Asset { std::string name; uint64_t size = 0; std::wstring url; };
bool otherArchitecture(const std::string& asset) {
    const auto text = lower(asset);
    return text.find("aarch64") != std::string::npos || text.find("arm64") != std::string::npos;
}
}

std::span<const Source> sources() { return source_table; }
const Source* findSource(std::string_view id) {
    for (const auto& source : source_table) if (id == source.id) return &source;
    return nullptr;
}

Platform thisPlatform() {
    Platform platform;
    std::ifstream input("/proc/cpuinfo");
    std::string line;
    while (std::getline(input, line)) {
        if (!line.starts_with("flags")) continue;
        line = " " + line + " ";
        platform.x86_64_v3 = true;
        for (const auto* flag : {" avx ", " avx2 ", " bmi1 ", " bmi2 ", " f16c ", " fma ", " abm ", " movbe ", " xsave "})
            if (line.find(flag) == std::string::npos) { platform.x86_64_v3 = false; break; }
        break;
    }
    return platform;
}

std::vector<Release> parseReleases(const Source& source, std::string_view text, const Platform& platform) {
    std::vector<Release> result;
    const auto root = json::parse(text, 16 * 1024 * 1024);
    if (!root || root->type != json::Value::Type::array) return result;
    const std::string id = source.id;
    for (const auto& entry : root->elements) {
        if (entry.type != json::Value::Type::object) continue;
        if ((entry.get("draft") && entry.get("draft")->flag) || (entry.get("prerelease") && entry.get("prerelease")->flag)) continue;
        const auto tag = entry.get("tag_name") ? entry.get("tag_name")->stringOr() : std::string{};
        if (tag.empty() || tag.size() > 200 || tag.find_first_of("/\\") != std::string::npos) continue;
        std::vector<Asset> assets;
        if (const auto* list = entry.get("assets"); list && list->type == json::Value::Type::array)
            for (const auto& asset : list->elements) {
                Asset item;
                item.name = asset.get("name") ? asset.get("name")->stringOr() : std::string{};
                const auto* size = asset.get("size");
                item.size = size && size->type == json::Value::Type::number && size->numeric > 0 ? static_cast<uint64_t>(size->numeric) : 0;
                item.url = wide(asset.get("browser_download_url") ? asset.get("browser_download_url")->stringOr() : std::string{});
                if (!item.name.empty() && item.name.find('/') == std::string::npos && item.size && githubUrl(item.url) &&
                    item.url.starts_with(L"https://github.com/")) assets.push_back(std::move(item));
            }
        const auto named = [&](const std::string& wanted) -> const Asset* {
            for (const auto& asset : assets) if (asset.name == wanted) return &asset;
            return nullptr;
        };
        const auto first = [&](auto&& accept) -> const Asset* {
            for (const auto& asset : assets) if (accept(asset.name)) return &asset;
            return nullptr;
        };
        const Asset* archive = nullptr;
        std::string checksum;
        Digest digest = Digest::none;
        std::wstring display;
        const auto version = withoutV(tag);
        if (id == "ge-proton") {
            archive = named(tag + "-x86_64.tar.gz");
            if (!archive) archive = named(tag + ".tar.gz");
            if (!archive) archive = first([](const std::string& n) { return n.ends_with(".tar.gz") && !otherArchitecture(n); });
            if (archive) { checksum = stripArchive(archive->name) + ".sha512sum"; digest = Digest::sha512; }
            display = wide(tag);
        } else if (id == "umu-proton") {
            archive = first([](const std::string& n) { return n.ends_with(".tar.gz") && !otherArchitecture(n); });
            if (archive) { checksum = stripArchive(archive->name) + ".sha512sum"; digest = Digest::sha512; }
            display = wide(tag);
        } else if (id == "proton-cachyos") {
            if (platform.x86_64_v3) archive = first([](const std::string& n) { return n.ends_with("-x86_64_v3.tar.xz"); });
            if (!archive) archive = first([](const std::string& n) { return n.ends_with("-x86_64.tar.xz"); });
            if (archive) { checksum = stripArchive(archive->name) + ".sha512sum"; digest = Digest::sha512; }
            display = L"Proton-CachyOS " + wide(tag.starts_with("cachyos-") ? tag.substr(8) : tag) +
                      (archive && archive->name.ends_with("_v3.tar.xz") ? L" (x86-64-v3)" : L"");
        } else if (id == "wine-kron4ek" || id == "wine-staging-tkg-kron4ek") {
            const bool tkg = id == "wine-staging-tkg-kron4ek";
            archive = named("wine-" + tag + (tkg ? "-staging-tkg-amd64-wow64.tar.xz" : "-amd64-wow64.tar.xz"));
            if (archive) { checksum = "sha256sums.txt"; digest = Digest::sha256; }
            display = (tkg ? L"Wine-Staging-TkG " : L"Wine ") + wide(tag);
        } else if (id == "dxvk") {
            archive = named("dxvk-" + version + ".tar.gz");
            display = L"DXVK " + wide(version);
        } else if (id == "vkd3d-proton") {
            archive = named("vkd3d-proton-" + version + ".tar.zst");
            display = L"VKD3D-Proton " + wide(version);
        } else if (id == "dxvk-nvapi") {
            archive = named("dxvk-nvapi-" + tag + ".tar.gz");
            if (!archive) archive = named("dxvk-nvapi-v" + version + ".tar.gz");
            display = L"DXVK-NVAPI " + wide(version);
        } else if (id == "umu-launcher") {
            archive = first([](const std::string& n) { return n.ends_with("-zipapp.tar"); });
            display = L"umu-launcher " + wide(tag);
        }
        if (!archive) continue;
        Release release;
        release.source = id;
        release.tag = tag;
        release.name = display;
        release.published = entry.get("published_at") ? entry.get("published_at")->stringOr().substr(0, 10) : std::string{};
        release.asset = archive->name;
        release.url = archive->url;
        release.size = archive->size;
        if (digest != Digest::none)
            if (const auto* sum = named(checksum)) {
                release.checksum_asset = checksum;
                release.checksum_url = sum->url;
                release.digest = digest;
            }
        const auto page = wide(entry.get("html_url") ? entry.get("html_url")->stringOr() : std::string{});
        if (page.starts_with(L"https://github.com/")) release.page = page;
        result.push_back(std::move(release));
    }
    return result;
}

ReleaseList releases(const Source& source, bool refresh) {
    const auto config = userConfigDirectory();
    return releases(source, toolsFolder(config) / "cache", [](const std::wstring& url) {
        return httpGet(url, L"Accept: application/vnd.github+json\r\nX-GitHub-Api-Version: 2022-11-28\r\n", 8 * 1024 * 1024);
    }, refresh, static_cast<int64_t>(std::time(nullptr)), thisPlatform());
}

ReleaseList releases(const Source& source, const fs::path& cache_folder, const Fetch& fetch, bool refresh, int64_t now, const Platform& platform) {
    ReleaseList result;
    std::string repository = source.repository;
    std::replace(repository.begin(), repository.end(), '/', '_');
    const auto cache = cache_folder / (repository + ".json");
    const auto cached = readSmall(cache, 16 * 1024 * 1024);
    const auto age = cached.empty() ? INT64_MAX : now - modifiedAt(cache);
    // Within the hour the cached list answers; a refresh asks GitHub again, at most once a minute.
    if (!cached.empty() && age >= 0 && age < 3600 && (!refresh || age < 60)) {
        result.releases = parseReleases(source, cached, platform);
        return result;
    }
    const auto response = fetch(L"https://api.github.com/repos/" + wide(source.repository) + L"/releases?per_page=15");
    if (response.status == 200 && !response.truncated) {
        const std::string_view body(reinterpret_cast<const char*>(response.body.data()), response.body.size());
        if (const auto parsed = json::parse(body, 16 * 1024 * 1024); parsed && parsed->type == json::Value::Type::array) {
            atomic_file::write(cache, std::span<const uint8_t>(response.body));
            result.releases = parseReleases(source, body, platform);
            return result;
        }
    }
    if (!cached.empty()) {
        result.releases = parseReleases(source, cached, platform);
        result.stale = true;
        return result;
    }
    result.available = false;
    return result;
}

bool installed(const Release& release, const std::vector<Tool>& tools) {
    const auto marker = release.source + ":" + release.tag;
    // The tag ("GE-Proton9-20", umu's and older archives' folder) or the archive's own top folder
    // ("GE-Proton11-11-x86_64", as ProtonUp-Qt and Steam keep it).
    const auto folder = expectedFolder(release), archive = stripArchive(release.asset);
    return std::any_of(tools.begin(), tools.end(), [&](const Tool& tool) {
        const auto own = name(tool.path);
        return tool.origin != Origin::umu && (tool.release == marker || (!tool.path.empty() && (own == folder || own == archive)));
    });
}

std::optional<Release> autoUpdate(const ReleaseList& list, const std::vector<Tool>& tools) {
    if (!list.available || list.releases.empty()) return std::nullopt;
    const bool used = std::any_of(tools.begin(), tools.end(), [](const Tool& tool) { return tool.family == "GE-Proton" && tool.origin != Origin::umu; });
    if (!used || installed(list.releases.front(), tools)) return std::nullopt;
    return list.releases.front();
}

bool autoUpdateDue(const std::filesystem::path& config, int64_t now) {
    const auto stamp = toolsFolder(config) / "cache" / "auto-update";
    std::error_code ec;
    std::filesystem::create_directories(stamp.parent_path(), ec);
    const auto text = file_io::readText(stamp, 64);
    int64_t last = 0;
    try { last = text.empty() ? 0 : std::stoll(text); } catch (...) { last = 0; }
    if (last > 0 && now >= last && now - last < 24 * 3600) return false;
    return atomic_file::write(stamp, std::to_string(now));
}

std::string checksumFor(std::string_view text, std::string_view asset) {
    const auto hexDigest = [](std::string_view value) {
        return (value.size() == 64 || value.size() == 128) &&
               std::all_of(value.begin(), value.end(), [](unsigned char ch) { return std::isxdigit(ch); });
    };
    std::string single;
    size_t lines = 0;
    while (!text.empty()) {
        const auto end = std::min(text.find('\n'), text.size());
        auto line = text.substr(0, end);
        text.remove_prefix(std::min(end + 1, text.size()));
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        const auto first = line.find_first_not_of(" \t");
        if (first == std::string_view::npos) continue;
        line.remove_prefix(first);
        ++lines;
        const auto space = line.find_first_of(" \t");
        const auto digest = line.substr(0, space);
        if (!hexDigest(digest)) continue;
        if (space == std::string_view::npos) { single = std::string(digest); continue; }
        auto file = line.substr(space);
        file.remove_prefix(std::min(file.find_first_not_of(" \t"), file.size()));
        if (!file.empty() && file.front() == '*') file.remove_prefix(1); // Binary mode marker.
        if (const auto slash = file.find_last_of('/'); slash != std::string_view::npos) file.remove_prefix(slash + 1);
        if (file == asset) return lower(std::string(digest));
    }
    return lines == 1 ? lower(single) : std::string{};
}

std::string fileDigest(const fs::path& file, Digest digest, const Cancel& cancel) {
    if (digest == Digest::none) return {};
    std::ifstream input(file, std::ios::binary);
    if (!input) return {};
    std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> context(EVP_MD_CTX_new(), EVP_MD_CTX_free);
    if (!context || EVP_DigestInit_ex(context.get(), digest == Digest::sha512 ? EVP_sha512() : EVP_sha256(), nullptr) != 1) return {};
    std::vector<char> buffer(1 << 20);
    while (input) {
        if (cancel && cancel()) return {};
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto count = static_cast<size_t>(input.gcount());
        if (count && EVP_DigestUpdate(context.get(), buffer.data(), count) != 1) return {};
    }
    if (!input.eof()) return {};
    unsigned char out[EVP_MAX_MD_SIZE];
    unsigned int length = 0;
    if (EVP_DigestFinal_ex(context.get(), out, &length) != 1) return {};
    static constexpr char digits[] = "0123456789abcdef";
    std::string result;
    for (unsigned int i = 0; i < length; ++i) { result += digits[out[i] >> 4]; result += digits[out[i] & 15]; }
    return result;
}

Error extractArchive(const fs::path& archive, const fs::path& destination, const Cancel& cancel, const std::function<void(uint64_t, uint64_t)>& progress) {
    std::error_code ec;
    // No symlink above the destination: libarchive refuses to write through any (SECURE_SYMLINKS).
    const auto root = fs::canonical(destination, ec);
    if (ec || !isFolder(root)) return Error::storage;
    const auto total = static_cast<uint64_t>(fs::file_size(archive, ec));
    if (ec) return Error::storage;
    std::unique_ptr<struct archive, decltype(&archive_read_free)> reader(archive_read_new(), archive_read_free);
    std::unique_ptr<struct archive, decltype(&archive_write_free)> writer(archive_write_disk_new(), archive_write_free);
    if (!reader || !writer) return Error::storage;
    archive_read_support_filter_all(reader.get());
    archive_read_support_format_tar(reader.get());
    archive_write_disk_set_options(writer.get(), ARCHIVE_EXTRACT_TIME | ARCHIVE_EXTRACT_PERM | ARCHIVE_EXTRACT_SECURE_NODOTDOT |
                                                 ARCHIVE_EXTRACT_SECURE_SYMLINKS);
    if (archive_read_open_filename(reader.get(), archive.c_str(), 1 << 16) != ARCHIVE_OK) return Error::archive;
    for (;;) {
        if (cancel && cancel()) return Error::cancelled;
        struct archive_entry* entry = nullptr;
        const int status = archive_read_next_header(reader.get(), &entry);
        if (status == ARCHIVE_EOF) break;
        if (status < ARCHIVE_WARN) return Error::archive;
        const char* raw = archive_entry_pathname(entry);
        const auto relative = safeRelative(raw ? raw : "");
        if (!relative) return Error::unsafe_archive;
        const auto type = archive_entry_filetype(entry);
        const char* hardlink = archive_entry_hardlink(entry);
        if (hardlink) {
            const auto link = safeRelative(hardlink);
            if (!link || *link == ".") return Error::unsafe_archive;
            archive_entry_copy_hardlink(entry, (root / *link).c_str());
        } else if (type == AE_IFLNK) {
            const char* target = archive_entry_symlink(entry);
            if (!target || !*target || target[0] == '/') return Error::unsafe_archive;
            const auto resolved = (relative->parent_path() / target).lexically_normal();
            if (resolved.is_absolute() || (!resolved.empty() && *resolved.begin() == "..")) return Error::unsafe_archive;
        } else if (type != AE_IFREG && type != AE_IFDIR) return Error::unsafe_archive; // Devices, FIFOs, sockets.
        if (*relative == "." && type == AE_IFDIR) continue; // The archive's "./".
        archive_entry_copy_pathname(entry, (root / *relative).c_str());
        // No set-ID bits; the owner can always read and write (and enter folders).
        const auto perm = archive_entry_perm(entry) & 0777;
        archive_entry_set_perm(entry, perm | (type == AE_IFDIR ? 0700 : 0600));
        if (archive_write_header(writer.get(), entry) < ARCHIVE_WARN) return Error::storage;
        if (!hardlink && type == AE_IFREG) {
            const void* block = nullptr;
            size_t size = 0;
            la_int64_t offset = 0;
            for (;;) {
                if (cancel && cancel()) return Error::cancelled;
                const int read = archive_read_data_block(reader.get(), &block, &size, &offset);
                if (read == ARCHIVE_EOF) break;
                if (read < ARCHIVE_WARN) return Error::archive;
                if (archive_write_data_block(writer.get(), block, size, offset) < ARCHIVE_WARN) return Error::storage;
                if (progress) progress(static_cast<uint64_t>(std::max<la_int64_t>(0, archive_filter_bytes(reader.get(), -1))), total);
            }
        }
        if (archive_write_finish_entry(writer.get()) < ARCHIVE_WARN) return Error::storage;
    }
    if (archive_write_close(writer.get()) != ARCHIVE_OK) return Error::storage;
    if (progress) progress(total, total);
    return linksInside(root) ? Error::none : Error::unsafe_archive;
}

Error installArchive(const fs::path& archive, const fs::path& folder, Kind kind, const std::string& wanted, const Cancel& cancel,
                     const std::function<void(uint64_t, uint64_t)>& progress, fs::path* installed) {
    std::error_code ec;
    fs::create_directories(folder, ec);
    const auto base = fs::canonical(folder, ec);
    if (ec) return Error::storage;
    const auto staging = base / (".launchme-install-" + randomHex());
    if (!fs::create_directory(staging, ec) || ec) return Error::storage;
    const auto cleanup = [&] { std::error_code ignored; fs::remove_all(staging, ignored); };
    if (const auto error = extractArchive(archive, staging, cancel, progress); error != Error::none) { cleanup(); return error; }
    std::vector<fs::path> top;
    for (fs::directory_iterator it(staging, ec), end; !ec && it != end; it.increment(ec)) top.push_back(it->path());
    auto root = staging;
    if (top.size() == 1 && fs::is_directory(fs::symlink_status(top.front(), ec))) root = top.front();
    bool valid = false;
    switch (kind) {
    case Kind::proton: valid = isFile(root / "proton"); break;
    case Kind::wine: valid = isFile(root / "bin" / "wine"); break;
    case Kind::component: valid = isFolder(root / "x64"); break;
    case Kind::umu: valid = isFile(root / "umu-run"); break;
    }
    if (!valid) { cleanup(); return Error::layout; }
    auto final_name = !wanted.empty() ? wanted : root == staging ? stripArchive(name(archive)) : name(root);
    if (final_name.empty() || final_name == "." || final_name == ".." || final_name.front() == '.' || final_name.find('/') != std::string::npos) {
        cleanup();
        return Error::layout;
    }
    const auto target = base / final_name;
    if (fs::exists(fs::symlink_status(target, ec))) { cleanup(); return Error::exists; }
    fs::rename(root, target, ec);
    if (ec) { cleanup(); return Error::storage; }
    cleanup();
    if (installed) *installed = target;
    return Error::none;
}

InstallResult install(const Release& release, Target target, const Cancel& cancel, const OnProgress& progress) {
    return install(release, target, cancel, progress, systemRoots());
}

InstallResult install(const Release& release, Target target, const Cancel& cancel, const OnProgress& progress, const Roots& roots) {
    const auto* source = findSource(release.source);
    if (!source || release.url.empty() || !release.size || release.asset.empty() || release.asset.find('/') != std::string::npos ||
        release.asset.front() == '.' || roots.config.empty())
        return {Error::unsupported};
    fs::path folder;
    switch (source->kind) {
    case Kind::proton: folder = target == Target::steam ? steamCompatFolder(roots) : toolsFolder(roots.config); break;
    case Kind::wine: case Kind::umu: folder = target == Target::steam ? fs::path{} : toolsFolder(roots.config); break;
    case Kind::component: folder = componentsFolder(roots.config); break;
    }
    if (folder.empty()) return {Error::unsupported};
    const auto downloads = toolsFolder(roots.config) / "downloads";
    std::error_code ec;
    fs::create_directories(downloads, ec);
    fs::create_directories(folder, ec);
    if (!isFolder(downloads) || !isFolder(folder)) return {Error::storage};
    const auto lock_file = downloads / (release.asset + ".lock");
    atomic_file::ExclusiveLock lock(lock_file);
    if (!lock.locked()) return {Error::busy};
    // Gone before the lock is released (declared after it).
    struct Unlink { fs::path file; ~Unlink() { std::error_code ignored; fs::remove(file, ignored); } } unlink{lock_file};
    const auto report = [&](Progress::Phase phase, uint64_t done, uint64_t total) { if (progress) progress({phase, done, total}); };
    const auto cancelled = [&] { return cancel && cancel(); };
    const auto part = downloads / (release.asset + ".part");
    report(Progress::Phase::download, 0, release.size);
    const auto downloaded = httpDownload(release.url, part, release.size, [&] { return cancelled(); },
                                         [&](uint64_t done) { report(Progress::Phase::download, done, release.size); }, githubUrl);
    if (cancelled()) return {Error::cancelled};
    if (downloaded == DownloadError::storage) return {Error::storage};
    if (downloaded != DownloadError::none || fs::file_size(part, ec) != release.size || ec) {
        if (downloaded == DownloadError::invalid_response) fs::remove(part, ec);
        return {Error::network};
    }
    const auto drop = [&] { std::error_code ignored; fs::remove(part, ignored); };
    if (release.digest != Digest::none && !release.checksum_url.empty()) {
        report(Progress::Phase::verify, 0, release.size);
        const auto response = httpGet(release.checksum_url, L"", 256 * 1024);
        if (response.status != 200 || response.truncated) return {Error::network};
        const auto expected = checksumFor(std::string_view(reinterpret_cast<const char*>(response.body.data()), response.body.size()), release.asset);
        if (expected.empty()) { drop(); return {Error::checksum}; }
        const auto actual = fileDigest(part, release.digest, [&] { return cancelled(); });
        if (cancelled()) return {Error::cancelled};
        if (actual != expected) { drop(); return {Error::checksum}; }
        report(Progress::Phase::verify, release.size, release.size);
    }
    InstallResult result;
    result.error = installArchive(part, folder, source->kind, source->kind == Kind::umu ? "umu-launcher-" + release.tag : std::string{},
                                  [&] { return cancelled(); }, [&](uint64_t done, uint64_t total) { report(Progress::Phase::extract, done, total); },
                                  &result.folder);
    if (result.error == Error::cancelled || result.error == Error::storage) return result;
    drop(); // Installed, or an archive no retry can install.
    if (result.error == Error::none) atomic_file::write(result.folder / marker_file, release.source + ":" + release.tag + "\n");
    return result;
}

std::vector<Runtime> parseRuntimes(std::string_view text) {
    std::vector<Runtime> result;
    const auto root = json::parse(text, 4 * 1024 * 1024);
    if (!root || root->type != json::Value::Type::array) return result;
    for (const auto& entry : root->elements) {
        if (entry.type != json::Value::Type::object) continue;
        Runtime item;
        item.name = entry.get("name") ? entry.get("name")->stringOr() : std::string{};
        if (item.name != eac_runtime && item.name != battleye_runtime) continue;
        item.url = wide(entry.get("url") ? entry.get("url")->stringOr() : std::string{});
        item.created = entry.get("created_at") ? entry.get("created_at")->stringOr() : std::string{};
        const auto file = item.url.substr(item.url.find_last_of(L'/') + 1);
        if (!item.url.starts_with(L"https://github.com/lutris/") || !githubUrl(item.url) || item.created.empty() || item.created.size() > 64 ||
            !file.ends_with(L".tar.xz") || std::any_of(result.begin(), result.end(), [&](const Runtime& known) { return known.name == item.name; })) continue;
        result.push_back(std::move(item));
    }
    return result;
}

fs::path runtimesFolder(const fs::path& config) { return toolsFolder(config) / "runtimes"; }

fs::path installedRuntime(const fs::path& config, std::string_view name) {
    if (name != eac_runtime && name != battleye_runtime) return {};
    const auto folder = runtimesFolder(config) / std::string(name);
    return isFile(folder / "version") ? folder : fs::path{};
}

Error installRuntime(std::string_view name, const fs::path& config, const Fetch& fetch, const Cancel& cancel) {
    if ((name != eac_runtime && name != battleye_runtime) || config.empty() || !fetch) return Error::unsupported;
    const auto cancelled = [&] { return cancel && cancel(); };
    const auto list = fetch(runtimes_url);
    if (cancelled()) return Error::cancelled;
    if (list.status != 200 || list.truncated) return Error::network;
    const auto runtimes = parseRuntimes(std::string_view(reinterpret_cast<const char*>(list.body.data()), list.body.size()));
    const auto found = std::find_if(runtimes.begin(), runtimes.end(), [&](const Runtime& item) { return item.name == name; });
    if (found == runtimes.end()) return Error::network;
    const auto folder = runtimesFolder(config), target = folder / std::string(name);
    if (file_io::readText(target / "version", 128) == found->created) return Error::none; // This version is there.
    std::error_code ec;
    fs::create_directories(folder, ec);
    if (!isFolder(folder)) return Error::storage;
    const auto downloads = toolsFolder(config) / "downloads";
    fs::create_directories(downloads, ec);
    if (!isFolder(downloads)) return Error::storage;
    const auto lock_file = downloads / (std::string(name) + ".lock");
    atomic_file::ExclusiveLock lock(lock_file);
    if (!lock.locked()) return Error::busy;
    struct UnlinkLock { fs::path file; ~UnlinkLock() { std::error_code ignored; fs::remove(file, ignored); } } unlink_lock{lock_file};
    const auto archive = fetch(found->url);
    if (cancelled()) return Error::cancelled;
    if (archive.status != 200 || archive.truncated || archive.body.empty()) return Error::network;
    const auto file = downloads / (std::string(name) + ".tar.xz.part");
    struct Unlink { fs::path file; ~Unlink() { std::error_code ignored; fs::remove(file, ignored); } } unlink{file};
    if (!atomic_file::write(file, std::span<const uint8_t>(archive.body))) return Error::storage;
    const auto staging = folder / (".launchme-install-" + randomHex());
    if (!fs::create_directory(staging, ec) || ec) return Error::storage;
    const auto cleanup = [&] { std::error_code ignored; fs::remove_all(staging, ignored); };
    if (const auto error = extractArchive(file, staging, [&] { return cancelled(); }, {}); error != Error::none) { cleanup(); return error; }
    std::vector<fs::path> top;
    for (fs::directory_iterator it(staging, ec), end; !ec && it != end; it.increment(ec)) top.push_back(it->path());
    auto root = staging;
    if (top.size() == 1 && fs::is_directory(fs::symlink_status(top.front(), ec))) root = top.front();
    if (top.empty()) { cleanup(); return Error::layout; }
    if (!atomic_file::write(root / "version", found->created)) { cleanup(); return Error::storage; }
    // The older copy goes aside first, so a failed rename leaves it in place.
    const auto old = folder / ("." + std::string(name) + ".old-" + randomHex());
    const bool had = fs::exists(fs::symlink_status(target, ec));
    if (had) { fs::rename(target, old, ec); if (ec) { cleanup(); return Error::storage; } }
    fs::rename(root, target, ec);
    if (ec) {
        if (had) { std::error_code ignored; fs::rename(old, target, ignored); }
        cleanup();
        return Error::storage;
    }
    cleanup();
    if (had) fs::remove_all(old, ec);
    return Error::none;
}

Error installRuntime(std::string_view name, const Cancel& cancel) {
    return installRuntime(name, systemRoots().config, [](const std::wstring& url) {
        // GitHub's archive redirects to its asset host; the anti-cheat runtimes are tens of MB.
        return httpGet(url, L"", url == runtimes_url ? 4u * 1024 * 1024 : 512u * 1024 * 1024);
    }, cancel);
}

launch_options_catalog::Text originName(Origin origin) {
    switch (origin) {
    case Origin::launchme: return {L"Launchme", L"Launchme"};
    case Origin::steam_compat: return {L"Steam (compatibilitytools.d)", L"Steam (compatibilitytools.d)"};
    case Origin::steam_system: return {L"Pakiet systemowy", L"System package"};
    case Origin::steam_official: return {L"Steam", L"Steam"};
    case Origin::heroic: return {L"Heroic", L"Heroic"};
    case Origin::lutris: return {L"Lutris", L"Lutris"};
    case Origin::system: return {L"System", L"System"};
    case Origin::umu: return {L"umu-launcher (pobiera sam)", L"umu-launcher (downloads it itself)"};
    }
    return {L"", L""};
}

launch_options_catalog::Text errorText(Error error) {
    switch (error) {
    case Error::none: return {L"Zainstalowano", L"Installed"};
    case Error::cancelled: return {L"Anulowano", L"Cancelled"};
    case Error::network: return {L"Nie udało się pobrać", L"The download failed"};
    case Error::checksum: return {L"Suma kontrolna się nie zgadza", L"The checksum does not match"};
    case Error::archive: return {L"Uszkodzone archiwum", L"A damaged archive"};
    case Error::unsafe_archive: return {L"Archiwum zawiera niebezpieczne ścieżki", L"The archive contains unsafe paths"};
    case Error::layout: return {L"To nie jest narzędzie tego rodzaju", L"Not a tool of this kind"};
    case Error::storage: return {L"Brak miejsca lub dostępu do dysku", L"No disk space or no access"};
    case Error::exists: return {L"Ta wersja jest już zainstalowana", L"This version is already installed"};
    case Error::busy: return {L"Ta wersja jest już pobierana", L"This version is already downloading"};
    case Error::unsupported: return {L"Nieobsługiwane", L"Not supported"};
    }
    return {L"", L""};
}

bool remove(const Tool& tool, const Roots& roots) {
    if (!tool.removable || tool.path.empty() || tool.origin == Origin::umu) return false;
    std::vector<fs::path> allowed{toolsFolder(roots.config), componentsFolder(roots.config)};
    const auto steam = userSteamCompatFolders(roots);
    allowed.insert(allowed.end(), steam.begin(), steam.end());
    std::error_code ec;
    const auto parent = canonicalOr(tool.path.parent_path());
    const auto folder_name = tool.path.filename();
    if (folder_name.empty() || folder_name == "." || folder_name == "..") return false;
    if (std::none_of(allowed.begin(), allowed.end(), [&](const fs::path& folder) { return isFolder(folder) && canonicalOr(folder) == parent; })) return false;
    const auto path = parent / folder_name;
    if (fs::is_symlink(path, ec)) return fs::remove(path, ec) && !ec; // A link another manager made: only the link goes.
    if (!isFolder(path)) return false;
    const auto doomed = parent / ("." + name(path) + ".removing-" + randomHex());
    fs::rename(path, doomed, ec);
    if (ec) return false;
    fs::remove_all(doomed, ec);
    return !fs::exists(path, ec);
}
}
