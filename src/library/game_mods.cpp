#include "game_mods.hpp"
#include "pe_version.hpp"
#include "../net/json.hpp"
#include "../platform/ascii_text.hpp"
#include "../platform/atomic_file.hpp"
#include "../platform/file_io.hpp"
#include "../platform/text_encoding.hpp"
#include "../platform/unicode_text.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>

namespace fs = std::filesystem;

namespace game_mods {
namespace {
constexpr Method reshade_methods[] = {{"dxgi.dll", L"dxgi.dll (DirectX 10-12)"}, {"d3d11.dll", L"d3d11.dll (DirectX 11)"},
                                      {"d3d12.dll", L"d3d12.dll (DirectX 12)"}, {"d3d9.dll", L"d3d9.dll (DirectX 9)"},
                                      {"opengl32.dll", L"opengl32.dll (OpenGL)"}};
constexpr Method renodx_methods[] = {{"game", L"Dodatek tej gry"}, {"unrealengine", L"Unreal Engine (ogólny)"}, {"unityengine", L"Unity (ogólny)"}};
constexpr Method optiscaler_methods[] = {{"dxgi.dll", L"dxgi.dll"}, {"winmm.dll", L"winmm.dll"}, {"version.dll", L"version.dll"},
                                         {"dbghelp.dll", L"dbghelp.dll"}, {"d3d12.dll", L"d3d12.dll"}, {"wininet.dll", L"wininet.dll"},
                                         {"winhttp.dll", L"winhttp.dll"}};
constexpr Method dlss_methods[] = {{"all", L"Wszystkie DLL DLSS gry"}, {"sr", L"Tylko nvngx_dlss.dll"}};
constexpr Method dlss5_methods[] = {{"renodx", L"ReShade + RenoDX DLSS 5"}};
constexpr Method anr_methods[] = {{"auto", L"Automatycznie"}, {"full", L"Pełny (z OptiScalerem)"}, {"addon", L"Dodatek do OptiScalera"}};

std::string pathText(const fs::path& path) { return text_encoding::utf8Lenient(path.wstring()); }
fs::path textPath(const std::string& text) { return fs::path(text_encoding::wideLenient(text)); }

std::wstring lowerName(const fs::path& path) { return unicode_text::lower(path.filename().wstring()); }

bool pathExists(const fs::path& path) { std::error_code ec; return fs::exists(path, ec); }

// What the file's version resource says (the first table that has the key).
std::wstring versionValue(const fs::path& file, std::wstring_view key) {
    const auto info = pe_version::read(file);
    if (!info) return {};
    for (const auto& table : info->tables)
        for (const auto& [name, value] : table.values)
            if (name == key && !value.empty()) return value;
    return {};
}

bool containsIgnoreCase(std::wstring_view text, std::wstring_view part) {
    return unicode_text::lower(text).find(unicode_text::lower(part)) != std::wstring::npos;
}

void addAnticheat(State& state, std::wstring_view name) {
    if (std::find(state.anticheat.begin(), state.anticheat.end(), name) == state.anticheat.end()) state.anticheat.emplace_back(name);
}

// Anti-cheat beside the program or in the installation's first levels.
void findAnticheat(const fs::path& folder, State& state) {
    std::error_code ec;
    if (folder.empty() || !fs::is_directory(folder, ec)) return;
    fs::recursive_directory_iterator it(folder, fs::directory_options::skip_permission_denied, ec), end;
    int seen = 0;
    for (; !ec && it != end && seen < 4000; it.increment(ec), ++seen) {
        if (it.depth() > 2) { it.disable_recursion_pending(); continue; }
        const auto name = lowerName(it->path());
        if (name == L"easyanticheat" || name.starts_with(L"easyanticheat_") || name == L"start_protected_game.exe") addAnticheat(state, L"Easy Anti-Cheat");
        else if (name == L"battleye" || name.starts_with(L"beservice")) addAnticheat(state, L"BattlEye");
        else if (name == L"anticheatexpert") addAnticheat(state, L"ACE (Anti-Cheat Expert)");
        else if (name == L"gameguard") addAnticheat(state, L"nProtect GameGuard");
        else if (name.starts_with(L"eaanticheat")) addAnticheat(state, L"EA Javelin");
        else if (name == L"xigncode" || name == L"xigncode3") addAnticheat(state, L"XIGNCODE3");
    }
}

// Every nvngx_dlss*.dll of the installation, at most six levels down, OptiScaler's and ANR's own aside.
void findDlss(const fs::path& root, State& state) {
    std::error_code ec;
    if (root.empty() || !fs::is_directory(root, ec)) return;
    fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end;
    int seen = 0;
    for (; !ec && it != end && seen < 60000; it.increment(ec), ++seen) {
        const auto name = lowerName(it->path());
        if (it->is_directory(ec)) {
            if (it.depth() >= 6 || name == L"optiscaler" || name == L"apex neural rendering fg" || name == L"reshade-shaders" || name == L"__overlay")
                it.disable_recursion_pending();
            continue;
        }
        if (name == L"nvngx_dlss.dll" || name == L"nvngx_dlssd.dll" || name == L"nvngx_dlssg.dll") state.dlss_files.push_back(it->path());
    }
    std::sort(state.dlss_files.begin(), state.dlss_files.end());
}
} // namespace

const char* slug(Mod mod) {
    switch (mod) {
    case Mod::reshade: return "reshade";
    case Mod::renodx: return "renodx";
    case Mod::optiscaler: return "optiscaler";
    case Mod::dlss: return "dlss";
    case Mod::dlss5: return "dlss5";
    case Mod::anr: return "anr";
    }
    return "reshade";
}

std::optional<Mod> fromSlug(std::string_view text) {
    for (const auto mod : all_mods) if (text == slug(mod)) return mod;
    return std::nullopt;
}

const wchar_t* name(Mod mod) {
    switch (mod) {
    case Mod::reshade: return L"ReShade";
    case Mod::renodx: return L"RenoDX";
    case Mod::optiscaler: return L"OptiScaler";
    case Mod::dlss: return L"DLSS";
    case Mod::dlss5: return L"DLSS 5";
    case Mod::anr: return L"ANR FG";
    }
    return L"";
}

std::span<const Method> methods(Mod mod) {
    switch (mod) {
    case Mod::reshade: return reshade_methods;
    case Mod::renodx: return renodx_methods;
    case Mod::optiscaler: return optiscaler_methods;
    case Mod::dlss: return dlss_methods;
    case Mod::dlss5: return dlss5_methods;
    case Mod::anr: return anr_methods;
    }
    return {};
}

const Method* findMethod(Mod mod, std::string_view id) {
    for (const auto& method : methods(mod)) if (method.id == id) return &method;
    return nullptr;
}

std::string_view methodOr(Mod mod, std::string_view preferred) {
    if (const auto* method = findMethod(mod, preferred)) return method->id;
    return methods(mod).front().id;
}

bool forEmulators(Mod mod) { return mod == Mod::reshade; }

const RecordEntry* Record::find(Mod mod) const {
    const auto found = mods.find(slug(mod));
    return found == mods.end() ? nullptr : &found->second;
}

fs::path recordFile(const fs::path& data, const fs::path& folder) {
    // FNV-1a of the folder's lowercase path: one record per folder, however it is spelled.
    const auto text = text_encoding::utf8Lenient(unicode_text::lower(folder.lexically_normal().wstring()));
    uint64_t hash = 1469598103934665603ull;
    for (const unsigned char c : text) { hash ^= c; hash *= 1099511628211ull; }
    char name[24];
    std::snprintf(name, sizeof(name), "%016llx", static_cast<unsigned long long>(hash));
    return data / "installed" / (std::string(name) + ".json");
}

std::string serialize(const Record& record) {
    auto root = json::object();
    root.properties["folder"] = json::string(pathText(record.folder));
    auto mods = json::object();
    for (const auto& [key, entry] : record.mods) {
        auto item = json::object();
        item.properties["version"] = json::string(entry.version);
        item.properties["method"] = json::string(entry.method);
        if (!entry.detail.empty()) item.properties["detail"] = json::string(text_encoding::utf8Lenient(entry.detail));
        if (!entry.moved.empty()) item.properties["moved"] = json::string(entry.moved);
        auto files = json::array();
        for (const auto& file : entry.files) {
            auto placed = json::object();
            placed.properties["path"] = json::string(pathText(file.path));
            if (!file.backup.empty()) placed.properties["backup"] = json::string(pathText(file.backup));
            files.elements.push_back(std::move(placed));
        }
        item.properties["files"] = std::move(files);
        auto folders = json::array();
        for (const auto& folder : entry.folders) folders.elements.push_back(json::string(pathText(folder)));
        item.properties["folders"] = std::move(folders);
        mods.properties[key] = std::move(item);
    }
    root.properties["mods"] = std::move(mods);
    return json::serialize(root);
}

std::optional<Record> parseRecord(std::string_view text) {
    const auto root = json::parse(text, 4 * 1024 * 1024);
    if (!root || root->type != json::Value::Type::object) return std::nullopt;
    Record record;
    if (const auto* folder = root->get("folder")) record.folder = textPath(folder->stringOr());
    if (const auto* mods = root->get("mods"); mods && mods->type == json::Value::Type::object)
        for (const auto& [key, item] : mods->properties) {
            if (!fromSlug(key) || item.type != json::Value::Type::object) continue;
            RecordEntry entry;
            if (const auto* v = item.get("version")) entry.version = v->stringOr();
            if (const auto* v = item.get("method")) entry.method = v->stringOr();
            if (const auto* v = item.get("detail")) entry.detail = text_encoding::wideLenient(v->stringOr());
            if (const auto* v = item.get("moved")) entry.moved = v->stringOr();
            if (const auto* files = item.get("files"); files && files->type == json::Value::Type::array)
                for (const auto& file : files->elements) {
                    const auto* path = file.get("path");
                    if (!path || path->stringOr().empty()) continue;
                    const auto* backup = file.get("backup");
                    entry.files.push_back({textPath(path->stringOr()), backup ? textPath(backup->stringOr()) : fs::path{}});
                }
            if (const auto* folders = item.get("folders"); folders && folders->type == json::Value::Type::array)
                for (const auto& folder : folders->elements) if (!folder.stringOr().empty()) entry.folders.push_back(textPath(folder.stringOr()));
            record.mods[key] = std::move(entry);
        }
    return record;
}

std::optional<Record> loadRecord(const fs::path& data, const fs::path& folder) {
    const auto text = file_io::readText(recordFile(data, folder), 4 * 1024 * 1024);
    if (text.empty()) return std::nullopt;
    auto record = parseRecord(text);
    if (record && record->folder.empty()) record->folder = folder;
    return record;
}

bool saveRecord(const fs::path& data, const Record& record) {
    const auto file = recordFile(data, record.folder);
    std::error_code ec;
    if (record.empty()) { fs::remove(file, ec); return !ec; }
    fs::create_directories(file.parent_path(), ec);
    return atomic_file::write(file, serialize(record));
}

std::wstring productName(const fs::path& file) { return versionValue(file, L"ProductName"); }

std::wstring fileVersion(const fs::path& file) {
    auto version = versionValue(file, L"FileVersion");
    if (version.empty()) version = versionValue(file, L"ProductVersion");
    std::replace(version.begin(), version.end(), L',', L'.');
    version.erase(std::remove(version.begin(), version.end(), L' '), version.end());
    return version;
}

bool isReShade(const fs::path& file) {
    if (!file_io::isFile(file)) return false;
    return containsIgnoreCase(productName(file), L"ReShade") || containsIgnoreCase(versionValue(file, L"OriginalFilename"), L"ReShade");
}

bool isOptiScaler(const fs::path& file) {
    if (!file_io::isFile(file)) return false;
    return containsIgnoreCase(versionValue(file, L"OriginalFilename"), L"OptiScaler") || containsIgnoreCase(productName(file), L"OptiScaler");
}

std::wstring shortVersion(std::wstring_view version) {
    // Three numbers at most, a trailing ".0" of four dropped ("310.9.16.0", "6.8.0.2104").
    size_t dots = 0, end = 0;
    for (; end < version.size(); ++end) {
        if (version[end] == L'.' && ++dots == 3) break;
        if (version[end] != L'.' && (version[end] < L'0' || version[end] > L'9')) return std::wstring(version);
    }
    return std::wstring(version.substr(0, end));
}

int compareVersions(std::string_view a, std::string_view b) {
    const auto numbers = [](std::string_view text) {
        std::vector<long long> result;
        size_t i = 0;
        while (i < text.size() && (text[i] < '0' || text[i] > '9')) ++i; // "v0.9.5", "V.B20" (letters skipped up to a number).
        while (i < text.size()) {
            long long value = 0;
            bool digits = false;
            while (i < text.size() && text[i] >= '0' && text[i] <= '9') { value = value * 10 + (text[i] - '0'); ++i; digits = true; }
            if (!digits) break;
            result.push_back(value);
            if (i < text.size() && text[i] == '.') ++i; else break;
        }
        return result;
    };
    const auto x = numbers(a), y = numbers(b);
    for (size_t i = 0; i < std::max(x.size(), y.size()); ++i) {
        const long long l = i < x.size() ? x[i] : 0, r = i < y.size() ? y[i] : 0;
        if (l != r) return l < r ? -1 : 1;
    }
    return 0;
}

std::optional<std::string> iniValue(std::string_view text, std::string_view section, std::string_view key) {
    bool inside = false;
    size_t start = 0;
    while (start <= text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string_view::npos) end = text.size();
        const auto line = ascii_text::trim(text.substr(start, end - start));
        if (line.size() >= 2 && line.front() == '[' && line.back() == ']') inside = ascii_text::equalIgnoreCase(line.substr(1, line.size() - 2), section);
        else if (inside) {
            const auto equals = line.find('=');
            if (equals != std::string_view::npos && ascii_text::equalIgnoreCase(ascii_text::trim(line.substr(0, equals)), key))
                return std::string(ascii_text::trim(line.substr(equals + 1)));
        }
        if (end == text.size()) break;
        start = end + 1;
    }
    return std::nullopt;
}

std::string setIniValue(std::string_view text, std::string_view section, std::string_view key, std::string_view value) {
    const std::string newline = text.find("\r\n") != std::string_view::npos ? "\r\n" : "\n";
    const std::string setting = std::string(key) + "=" + std::string(value);
    std::vector<std::string_view> lines;
    for (size_t start = 0;;) {
        size_t end = text.find('\n', start);
        if (end == std::string_view::npos) { lines.push_back(text.substr(start)); break; }
        auto line = text.substr(start, end - start);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        lines.push_back(line);
        start = end + 1;
    }
    std::vector<std::string> out;
    bool inside = false, done = false, seen = false;
    size_t last_in_section = 0; // Where a missing key goes: after the section's last non-empty line.
    for (const auto line : lines) {
        const auto trimmed = ascii_text::trim(line);
        if (trimmed.size() >= 2 && trimmed.front() == '[' && trimmed.back() == ']') {
            if (inside && !done) { out.insert(out.begin() + static_cast<std::ptrdiff_t>(last_in_section), setting); done = true; }
            inside = ascii_text::equalIgnoreCase(trimmed.substr(1, trimmed.size() - 2), section);
            if (inside) seen = true;
            out.emplace_back(line);
            if (inside) last_in_section = out.size();
            continue;
        }
        if (inside && !done) {
            const auto equals = trimmed.find('=');
            if (equals != std::string_view::npos && trimmed.front() != ';' && trimmed.front() != '#' &&
                ascii_text::equalIgnoreCase(ascii_text::trim(trimmed.substr(0, equals)), key)) {
                out.push_back(setting);
                done = true;
                continue;
            }
            if (!trimmed.empty()) last_in_section = out.size() + 1;
        }
        out.emplace_back(line);
    }
    if (inside && !done) { out.insert(out.begin() + static_cast<std::ptrdiff_t>(last_in_section), setting); done = true; }
    if (!done) {
        if (!seen) {
            while (!out.empty() && out.back().empty()) out.pop_back();
            if (!out.empty()) out.emplace_back();
            out.push_back("[" + std::string(section) + "]");
        }
        out.push_back(setting);
        out.emplace_back(); // The file ends with a line break.
    }
    std::string result;
    for (size_t i = 0; i < out.size(); ++i) {
        result += out[i];
        if (i + 1 < out.size()) result += newline;
    }
    return result;
}

bool x86Image(const fs::path& exe) {
    std::ifstream in(exe, std::ios::binary);
    if (!in) return false;
    char head[64]{};
    if (!in.read(head, sizeof(head)) || head[0] != 'M' || head[1] != 'Z') return false;
    uint32_t offset = 0;
    std::memcpy(&offset, head + 0x3c, 4);
    if (offset > 64 * 1024 * 1024) return false;
    in.seekg(offset);
    char pe[6]{};
    if (!in.read(pe, sizeof(pe)) || std::memcmp(pe, "PE\0\0", 4) != 0) return false;
    const auto machine = static_cast<uint16_t>(static_cast<unsigned char>(pe[4]) | (static_cast<unsigned char>(pe[5]) << 8));
    return machine == 0x014c; // IMAGE_FILE_MACHINE_I386
}

fs::path mainExecutable(const fs::path& root, int depth) {
    std::error_code ec;
    if (root.empty() || !fs::is_directory(root, ec)) return {};
    static constexpr std::wstring_view tools[] = {L"unins", L"crash", L"setup", L"redist", L"vc_", L"vcredist", L"dxsetup", L"dotnet", L"ue4prereq",
                                                  L"ueprereq", L"easyanticheat", L"start_protected", L"beservice", L"battleye", L"cefprocess",
                                                  L"helper", L"report", L"touchup", L"cleanup", L"uploader", L"activation", L"launcherpatcher"};
    fs::path shipping, largest;
    uintmax_t largest_size = 0;
    fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end;
    int seen = 0;
    for (; !ec && it != end && seen < 20000; it.increment(ec), ++seen) {
        if (it->is_directory(ec)) {
            const auto name = lowerName(it->path());
            if (it.depth() >= depth - 1 || name == L"engine" || name == L"redist" || name == L"_commonredist" || name == L"directx" ||
                name == L"easyanticheat" || name == L"battleye" || name == L"optiscaler" || name == L"reshade-shaders")
                it.disable_recursion_pending();
            continue;
        }
        const auto name = lowerName(it->path());
        if (!name.ends_with(L".exe")) continue;
        if (std::any_of(std::begin(tools), std::end(tools), [&](std::wstring_view tool) { return name.find(tool) != std::wstring::npos; })) continue;
        if (name.ends_with(L"-win64-shipping.exe") || name.ends_with(L"-wingdk-shipping.exe")) { if (shipping.empty()) shipping = it->path(); continue; }
        const auto size = it->file_size(ec);
        if (!ec && size > largest_size) { largest_size = size; largest = it->path(); }
    }
    return shipping.empty() ? largest : shipping;
}

State scan(const Target& target, const Record* record) {
    State state;
    state.x86 = target.x86 || (!target.exe.empty() && x86Image(target.exe));
    const auto& folder = target.folder;
    std::error_code ec;
    if (folder.empty() || !fs::is_directory(folder, ec)) return state;
    // Launchme's own installs: present while their files are there.
    if (record)
        for (const auto mod : all_mods) {
            const auto* entry = record->find(mod);
            if (!entry) continue;
            const bool there = entry->files.empty() ? mod == Mod::anr :
                               std::any_of(entry->files.begin(), entry->files.end(), [](const PlacedFile& file) { return pathExists(file.path); });
            if (!there && mod != Mod::anr) continue;
            auto& found = state[mod];
            found.present = found.ours = true;
            found.method = entry->method;
            found.version = text_encoding::wideLenient(entry->version);
            if (mod == Mod::renodx) found.detail = entry->detail;
        }
    // ReShade: a known proxy whose version resource says ReShade.
    if (!state[Mod::reshade].present)
        for (const auto name : reshade_names) {
            const auto file = folder / name;
            if (!isReShade(file)) continue;
            auto& found = state[Mod::reshade];
            found.present = true;
            found.method = text_encoding::utf8Lenient(name);
            found.version = shortVersion(fileVersion(file));
            break;
        }
    // OptiScaler: its proxy, else its OptiScaler.ini beside the program.
    if (!state[Mod::optiscaler].present) {
        for (const auto name : optiscaler_names) {
            const auto file = folder / name;
            if (!isOptiScaler(file)) continue;
            auto& found = state[Mod::optiscaler];
            found.present = true;
            found.method = text_encoding::utf8Lenient(name);
            found.version = shortVersion(fileVersion(file));
            break;
        }
        if (!state[Mod::optiscaler].present && file_io::isFile(folder / L"OptiScaler.ini")) state[Mod::optiscaler].present = true;
    }
    // ANR FG: its record in OptiScaler's folder (the full package) or in OptiScaler's custom folder (the add-on).
    const bool anr_full = file_io::isFile(folder / L"OptiScaler" / L"nr_fg_install.json");
    const bool anr_addon = file_io::isFile(folder / L"OptiScaler" / L"custom" / L"Apex Neural Rendering FG" / L"nr_fg_install.json") ||
                           file_io::isFile(folder / L"custom" / L"Apex Neural Rendering FG" / L"nr_fg_install.json");
    if (anr_full || anr_addon) {
        auto& found = state[Mod::anr];
        found.present = true;
        if (found.method.empty() || found.method == "auto") found.method = anr_full ? "full" : "addon";
        if (anr_full) { // Its OptiScaler is the package's: shown as OptiScaler too, never removed on its own.
            auto& opti = state[Mod::optiscaler];
            opti.present = true;
            opti.ours = false;
            opti.detail = L"ANR FG";
        }
    } else if (state[Mod::anr].ours) state[Mod::anr] = {}; // Removed by hand.
    // RenoDX: its add-ons beside the program (the DLSS 5 add-ons belong to DLSS 5).
    const bool dlss5_addon = file_io::isFile(folder / L"renodx-dlss5.addon64");
    if (!state[Mod::renodx].present)
        for (fs::directory_iterator it(folder, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec)) {
            const auto name = lowerName(it->path());
            if (!name.starts_with(L"renodx-") || !(name.ends_with(L".addon64") || name.ends_with(L".addon32"))) continue;
            if (name.starts_with(L"renodx-dlss")) continue;
            state[Mod::renodx].present = true;
            state[Mod::renodx].detail = it->path().filename().wstring();
            break;
        }
    // DLSS 5: the neural rendering DLL with its add-on.
    if (!state[Mod::dlss5].present && dlss5_addon && file_io::isFile(folder / L"nvngx_dlssnr.dll")) {
        state[Mod::dlss5].present = true;
        state[Mod::dlss5].version = shortVersion(fileVersion(folder / L"nvngx_dlssnr.dll"));
    }
    // DLSS: the game's own DLLs (what it runs with), replaced or not.
    findDlss(target.root.empty() ? folder : target.root, state);
    if (target.root.empty() || target.root != folder) { // The program's folder may sit outside the root (an emulator).
        for (const auto name : {L"nvngx_dlss.dll", L"nvngx_dlssd.dll", L"nvngx_dlssg.dll"})
            if (const auto file = folder / name; file_io::isFile(file) && std::find(state.dlss_files.begin(), state.dlss_files.end(), file) == state.dlss_files.end())
                state.dlss_files.push_back(file);
    }
    for (const auto& file : state.dlss_files)
        if (lowerName(file) == L"nvngx_dlss.dll") {
            state.has_dlss = true;
            auto& found = state[Mod::dlss];
            const auto version = shortVersion(fileVersion(file));
            if (!found.present || compareVersions(text_encoding::utf8Lenient(version), text_encoding::utf8Lenient(found.version)) > 0) found.version = version;
            found.present = true;
        }
    if (state[Mod::dlss].ours && !state.has_dlss) state[Mod::dlss] = {};
    findAnticheat(target.root.empty() ? folder : target.root, state);
    if (!target.root.empty() && target.root != folder) findAnticheat(folder, state);
    return state;
}

std::vector<std::wstring> badges(const State& state) {
    std::vector<std::wstring> result;
    for (const auto mod : all_mods) {
        const auto& found = state[mod];
        if (!found.present) continue;
        std::wstring badge = name(mod);
        if (mod == Mod::dlss || mod == Mod::dlss5 || mod == Mod::anr) {
            if (!found.version.empty()) badge += L" " + found.version;
        } else if (mod == Mod::optiscaler && !found.detail.empty()) {
            continue; // ANR's own OptiScaler: the ANR badge says it.
        }
        result.push_back(std::move(badge));
    }
    return result;
}

bool placeBytes(std::string_view bytes, const fs::path& target, RecordEntry& entry) {
    std::error_code ec;
    fs::path created;
    for (auto parent = target.parent_path(); !parent.empty() && !fs::exists(parent, ec); parent = parent.parent_path()) created = parent;
    fs::create_directories(target.parent_path(), ec);
    if (!created.empty() && std::find(entry.folders.begin(), entry.folders.end(), created) == entry.folders.end()) entry.folders.push_back(created);
    PlacedFile placed{target, {}};
    // Already placed by this entry (an update): its first backup stays the original.
    const auto own = std::find_if(entry.files.begin(), entry.files.end(), [&](const PlacedFile& file) { return file.path == target; });
    if (own != entry.files.end()) placed.backup = own->backup;
    else if (fs::exists(target, ec)) {
        fs::path backup = target;
        backup += backup_suffix;
        if (!fs::exists(backup, ec)) {
            fs::rename(target, backup, ec);
            if (ec) return false;
        }
        placed.backup = backup;
    }
    if (!atomic_file::write(target, std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size()))) return false;
    if (own == entry.files.end()) entry.files.push_back(placed);
    return true;
}

bool placeFile(const fs::path& source, const fs::path& target, RecordEntry& entry) {
    std::error_code ec;
    fs::path created;
    for (auto parent = target.parent_path(); !parent.empty() && !fs::exists(parent, ec); parent = parent.parent_path()) created = parent;
    fs::create_directories(target.parent_path(), ec);
    if (!created.empty() && std::find(entry.folders.begin(), entry.folders.end(), created) == entry.folders.end()) entry.folders.push_back(created);
    PlacedFile placed{target, {}};
    const auto own = std::find_if(entry.files.begin(), entry.files.end(), [&](const PlacedFile& file) { return file.path == target; });
    if (own != entry.files.end()) placed.backup = own->backup;
    else if (fs::exists(target, ec)) {
        fs::path backup = target;
        backup += backup_suffix;
        if (!fs::exists(backup, ec)) {
            fs::rename(target, backup, ec);
            if (ec) return false;
        }
        placed.backup = backup;
    }
    // Copied beside it first, then renamed over: a game holding the old file fails here, not halfway.
    fs::path temporary = target;
    temporary += L".launchme-new";
    fs::copy_file(source, temporary, fs::copy_options::overwrite_existing, ec);
    if (ec) return false;
    fs::rename(temporary, target, ec);
    if (ec) { fs::remove(temporary, ec); return false; }
    if (own == entry.files.end()) entry.files.push_back(placed);
    return true;
}

bool removeFiles(RecordEntry& entry) {
    std::error_code ec;
    std::vector<PlacedFile> left;
    // Newest first: a file placed twice (a backup of a backup) unwinds in order.
    for (auto it = entry.files.rbegin(); it != entry.files.rend(); ++it) {
        ec.clear();
        if (fs::exists(it->path, ec)) fs::remove(it->path, ec);
        if (ec) { left.insert(left.begin(), *it); continue; }
        if (!it->backup.empty() && fs::exists(it->backup, ec)) {
            fs::rename(it->backup, it->path, ec);
            if (ec) { left.insert(left.begin(), *it); continue; }
        }
    }
    entry.files = std::move(left);
    if (!entry.files.empty()) return false;
    // Its own folders: deepest first, only what it made.
    auto folders = entry.folders;
    std::sort(folders.begin(), folders.end(), [](const fs::path& a, const fs::path& b) { return a.native().size() > b.native().size(); });
    for (const auto& folder : folders) { ec.clear(); fs::remove_all(folder, ec); }
    entry.folders.clear();
    return true;
}
} // namespace game_mods
