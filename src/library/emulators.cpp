#include "emulators.hpp"
#include "../platform/file_io.hpp"
#include "../platform/system.hpp"
#include "../platform/text_encoding.hpp"

#include <algorithm>
#include <cwctype>
#include <optional>
#include <unordered_map>

namespace emulators {
namespace {
using std::filesystem::path;

constexpr Info table[] = {
    // Independent manual profiles: no assumed emulator or command-line interface.
    {L"ps5", L"PS5", {}, {}, {}, {}, {}, {}, {}, {}, {}, false, false, true},
    {L"switch2", L"Switch 2", {}, {}, {}, {}, {}, {}, {}, {}, {}, false, false, true},
    // id, name, script, script_args, EmuDeck folder, Windows programs, ES-DE names, installer folders, flatpak, Linux programs, direct arguments.
    {L"duckstation", L"DuckStation", L"duckstation", L"'-fullscreen'", L"duckstation", {L"duckstation-qt-x64-ReleaseLTCG.exe", L"duckstation-qt.exe"},
     {L"DUCKSTATION"}, {L"DuckStation"}, L"org.duckstation.DuckStation", {L"duckstation-qt", L"duckstation"}, L"-fullscreen"},
    {L"pcsx2", L"PCSX2", L"pcsx2", L"", L"PCSX2-Qt", {L"pcsx2-qtx64.exe", L"pcsx2-qt.exe", L"pcsx2.exe"},
     {L"PCSX2"}, {L"PCSX2", L"PCSX2-Qt"}, L"net.pcsx2.PCSX2", {L"pcsx2-qt", L"pcsx2"}, L"-batch -fullscreen"},
    {L"rpcs3", L"RPCS3", L"rpcs3", L"'--no-gui'", L"RPCS3", {L"rpcs3.exe"},
     {L"RPCS3"}, {L"RPCS3"}, L"net.rpcs3.RPCS3", {L"rpcs3"}, L"--no-gui"},
    // A game installed by shadPS4 is its folder's eboot.bin; EmuDeck's launcher also takes the .lnk shortcuts shadPS4 makes.
    {L"shadps4", L"shadPS4", L"shadps4", L"", L"shadps4-qt", {L"shadPS4QtLauncher.exe", L"shadPS4.exe"},
     {L"SHADPS4"}, {L"shadPS4", L"shadps4-qt"}, L"net.shadps4.shadPS4", {L"shadps4", L"Shadps4-qt"}, L""},
    // Vita3K runs an installed game by its title ID: the name of the folder holding eboot.bin.
    {L"vita3k", L"Vita3K", L"vita3k", L"'-Fr'", L"vita3k", {L"Vita3K.exe"},
     {L"VITA3K"}, {L"Vita3K"}, L"", {L"Vita3K", L"vita3k"}, L"-F -r", false, true},
    {L"ppsspp", L"PPSSPP", L"ppsspp", L"'-f' '-g'", L"PPSSPP", {L"PPSSPPWindows64.exe", L"PPSSPPWindows.exe"},
     {L"PPSSPP"}, {L"PPSSPP"}, L"org.ppsspp.PPSSPP", {L"PPSSPPSDL", L"ppsspp"}, L"--fullscreen"},
    {L"dolphin", L"Dolphin", L"dolphin", L"'-b' '-e'", L"Dolphin-x64", {L"Dolphin.exe"},
     {L"DOLPHIN"}, {L"Dolphin", L"Dolphin-x64"}, L"org.DolphinEmu.dolphin-emu", {L"dolphin-emu"}, L"-b -e"},
    {L"primehack", L"PrimeHack", L"primehack", L"-b -e", L"primehack", {L"Dolphin.exe"},
     {L"PRIMEHACK"}, {L"PrimeHack"}, L"io.github.shiiion.primehack", {L"primehack"}, L"-b -e", true},
    {L"cemu", L"Cemu", L"cemu", L"'-f' '-g'", L"cemu", {L"Cemu.exe"},
     {L"CEMU"}, {L"Cemu"}, L"info.cemu.Cemu", {L"cemu", L"Cemu"}, L"-f -g"},
    {L"eden", L"Eden", L"eden", L"'-f' '-g'", L"eden", {L"eden.exe"},
     {L"EDEN"}, {L"Eden"}, L"dev.eden_emu.eden", {L"eden"}, L"-f -g"},
    {L"citron", L"Citron", L"citron", L"'-f' '-g'", L"citron", {L"citron.exe"},
     {L"CITRON"}, {L"Citron"}, L"org.citron_emu.citron", {L"citron"}, L"-f -g"},
    {L"ryujinx", L"Ryujinx", L"ryujinx", L"'-f'", L"Ryujinx", {L"Ryujinx.exe", L"Ryujinx.Ava.exe"},
     {L"RYUJINX"}, {L"Ryujinx"}, L"org.ryujinx.Ryujinx", {L"Ryujinx", L"ryujinx"}, L"--fullscreen"},
    {L"yuzu", L"yuzu", L"yuzu", L"'-f' '-g'", L"yuzu", {L"yuzu.exe"},
     {L"YUZU"}, {L"yuzu"}, L"org.yuzu_emu.yuzu", {L"yuzu"}, L"-f -g"},
    {L"azahar", L"Azahar", L"azahar", L"", L"azahar", {L"azahar.exe"},
     {L"AZAHAR", L"LIME3DS"}, {L"Azahar"}, L"org.azahar_emu.Azahar", {L"azahar"}, L""},
    {L"melonds", L"melonDS", L"melonds", L"'-f'", L"melonDS", {L"melonDS.exe"},
     {L"MELONDS"}, {L"melonDS"}, L"net.kuribo64.melonDS", {L"melonDS"}, L"-f"},
    {L"xemu", L"xemu", L"xemu", L"-full-screen -dvd_path", L"xemu", {L"xemu.exe"},
     {L"XEMU"}, {L"xemu"}, L"app.xemu.xemu", {L"xemu"}, L"-full-screen -dvd_path"},
    {L"xenia", L"Xenia", L"xenia", L"", L"xenia", {L"xenia_canary.exe", L"xenia.exe"},
     {L"XENIA"}, {L"Xenia", L"xenia"}, L"", {}, L""},
    // A game is a .scummvm file in its folder (ES-DE's and EmuDeck's layout): ScummVM finds the game beside it.
    {L"scummvm", L"ScummVM", L"scummvm", L"'--path=%ROM%' --auto-detect", L"scummvm", {L"scummvm.exe"},
     {L"SCUMMVM"}, {L"ScummVM"}, L"org.scummvm.ScummVM", {L"scummvm"}, L"--path=%ROM% --auto-detect"},
    {L"retroarch", L"RetroArch", L"retroarch", L"", L"RetroArch", {L"retroarch.exe"},
     {L"RETROARCH"}, {L"RetroArch-Win64", L"RetroArch"}, L"org.libretro.RetroArch", {L"retroarch"}, L""},
};

std::wstring lower(std::wstring_view text) {
    std::wstring result(text);
    for (auto& ch : result) ch = static_cast<wchar_t>(std::towlower(ch));
    return result;
}
bool fileExists(const path& file) { return !file.empty() && file_io::isFile(file); }
bool folderExists(const path& folder) { return !folder.empty() && file_io::isFolder(folder); }

// '*' and '?' in one file name, any case.
bool wildcard(std::wstring_view pattern, std::wstring_view name) {
    size_t p = 0, n = 0, star = std::wstring_view::npos, mark = 0;
    while (n < name.size()) {
        if (p < pattern.size() && (pattern[p] == L'?' || std::towlower(pattern[p]) == std::towlower(name[n]))) { ++p; ++n; }
        else if (p < pattern.size() && pattern[p] == L'*') { star = p++; mark = n; }
        else if (star != std::wstring_view::npos) { p = star + 1; n = ++mark; }
        else return false;
    }
    while (p < pattern.size() && pattern[p] == L'*') ++p;
    return p == pattern.size();
}
// A file, its last segment possibly with a wildcard (pcsx2-qt*.exe): the first match by name.
path existing(const path& candidate) {
    if (candidate.empty()) return {};
    const auto name = candidate.filename().wstring();
    if (name.find_first_of(L"*?") == std::wstring::npos) return fileExists(candidate) ? candidate.lexically_normal() : path{};
    std::vector<path> found;
    std::error_code error;
    for (std::filesystem::directory_iterator it(candidate.parent_path(), error), end; !error && it != end; it.increment(error)) {
        std::error_code kind;
        if (it->is_regular_file(kind) && wildcard(name, it->path().filename().wstring())) found.push_back(it->path());
        if (found.size() > 64) break;
    }
    if (found.empty()) return {};
    std::sort(found.begin(), found.end());
    return found.front().lexically_normal();
}
std::wstring environmentValue(std::wstring_view name) {
    return system_info::environment(std::wstring(name).c_str(), 4095).value_or(std::wstring{});
}
// %VAR%, $env:VAR, $VAR / ${VAR} and a leading ~ in a path written by ES-DE or EmuDeck.
std::wstring expand(std::wstring text, const Context& context, const path& esde = {}) {
    const auto value = [&](std::wstring name) -> std::wstring {
        const auto key = lower(name);
        if (key == L"espath") return esde.wstring();
        if (key == L"home" || key == L"userprofile") return context.home.wstring();
        if (key == L"appdata") return context.app_data.empty() ? environmentValue(name) : context.app_data.wstring();
        if (key == L"localappdata") return context.local_app_data.empty() ? environmentValue(name) : context.local_app_data.wstring();
        return environmentValue(name);
    };
    for (size_t at = text.find(L'%'); at != std::wstring::npos;) {
        const auto end = text.find(L'%', at + 1);
        if (end == std::wstring::npos) break;
        const auto name = text.substr(at + 1, end - at - 1);
        if (name.empty() || name.find_first_of(L" \\/") != std::wstring::npos) { at = text.find(L'%', at + 1); continue; }
        const auto replacement = value(name);
        text.replace(at, end - at + 1, replacement);
        at = text.find(L'%', at + replacement.size());
    }
    for (size_t at = text.find(L"$env:"); at != std::wstring::npos; at = text.find(L"$env:", at)) {
        size_t end = at + 5;
        while (end < text.size() && (std::iswalnum(text[end]) || text[end] == L'_' || text[end] == L'(' || text[end] == L')')) ++end;
        const auto replacement = value(text.substr(at + 5, end - at - 5));
        text.replace(at, end - at, replacement);
        at += replacement.size();
    }
    for (size_t at = text.find(L'$'); at != std::wstring::npos; at = text.find(L'$', at)) {
        const bool braced = at + 1 < text.size() && text[at + 1] == L'{';
        size_t start = at + (braced ? 2 : 1), end = start;
        while (end < text.size() && (std::iswalnum(text[end]) || text[end] == L'_')) ++end;
        if (end == start) { ++at; continue; }
        const auto replacement = value(text.substr(start, end - start));
        const size_t stop = braced && end < text.size() && text[end] == L'}' ? end + 1 : end;
        text.replace(at, stop - at, replacement);
        at += replacement.size();
    }
    if (text.starts_with(L"~")) text = context.home.wstring() + text.substr(1);
    return text;
}

#ifdef _WIN32
std::wstring registryText(HKEY root, const std::wstring& key, const wchar_t* value, REGSAM view = 0) {
    HKEY handle{};
    if (RegOpenKeyExW(root, key.c_str(), 0, KEY_READ | view, &handle) != ERROR_SUCCESS) return {};
    wchar_t buffer[2048]{};
    DWORD size = sizeof(buffer) - sizeof(wchar_t), type = 0;
    std::wstring result;
    if (RegQueryValueExW(handle, value, nullptr, &type, reinterpret_cast<BYTE*>(buffer), &size) == ERROR_SUCCESS && (type == REG_SZ || type == REG_EXPAND_SZ)) {
        result.assign(buffer, wcsnlen(buffer, std::size(buffer)));
        if (type == REG_EXPAND_SZ) {
            wchar_t expanded[2048]{};
            if (const auto length = ExpandEnvironmentStringsW(result.c_str(), expanded, static_cast<DWORD>(std::size(expanded))); length && length <= std::size(expanded))
                result = expanded;
        }
    }
    RegCloseKey(handle);
    return result;
}
path unquoted(std::wstring text) {
    while (!text.empty() && (text.back() == L' ' || text.back() == L'"')) text.pop_back();
    while (!text.empty() && (text.front() == L' ' || text.front() == L'"')) text.erase(text.begin());
    if (const auto comma = text.rfind(L','); comma != std::wstring::npos && comma > text.rfind(L'\\')) text.resize(comma); // "x.exe,0"
    return text.empty() ? path{} : path(text).lexically_normal();
}
// Windows' App Paths entry of a program.
path appPath(std::wstring_view exe) {
    const auto key = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\App Paths\\" + std::wstring(exe);
    for (const auto root : {HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE})
        if (const auto found = unquoted(registryText(root, key, nullptr)); fileExists(found)) return found;
    return {};
}
// ES-DE's winregistryvalue: "HKEY_…\Key\Value|appended path".
path registryValue(std::wstring entry) {
    std::wstring suffix;
    if (const auto bar = entry.find(L'|'); bar != std::wstring::npos) { suffix = entry.substr(bar + 1); entry.resize(bar); }
    const auto first = entry.find(L'\\'), last = entry.rfind(L'\\');
    if (first == std::wstring::npos || last == first) return {};
    const auto hive = entry.substr(0, first);
    const HKEY root = hive == L"HKEY_CURRENT_USER" ? HKEY_CURRENT_USER : hive == L"HKEY_LOCAL_MACHINE" ? HKEY_LOCAL_MACHINE :
                      hive == L"HKEY_CLASSES_ROOT" ? HKEY_CLASSES_ROOT : nullptr;
    if (!root) return {};
    const auto key = entry.substr(first + 1, last - first - 1), value = entry.substr(last + 1);
    const auto text = registryText(root, key, value.c_str());
    if (text.empty()) return {};
    return existing(path(unquoted(text).wstring() + suffix).lexically_normal());
}
struct Installed { std::wstring name; path location, icon; };
std::vector<Installed> installedPrograms() {
    std::vector<Installed> result;
    constexpr const wchar_t* uninstall = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall";
    const std::pair<HKEY, REGSAM> places[] = {{HKEY_LOCAL_MACHINE, KEY_WOW64_64KEY}, {HKEY_LOCAL_MACHINE, KEY_WOW64_32KEY}, {HKEY_CURRENT_USER, 0}};
    for (const auto& [root, view] : places) {
        HKEY handle{};
        if (RegOpenKeyExW(root, uninstall, 0, KEY_READ | view, &handle) != ERROR_SUCCESS) continue;
        wchar_t name[256];
        for (DWORD index = 0; index < 4096; ++index) {
            DWORD length = static_cast<DWORD>(std::size(name));
            if (RegEnumKeyExW(handle, index, name, &length, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS) break;
            const auto key = std::wstring(uninstall) + L"\\" + name;
            Installed program{registryText(root, key, L"DisplayName", view), unquoted(registryText(root, key, L"InstallLocation", view)),
                              unquoted(registryText(root, key, L"DisplayIcon", view))};
            if (!program.name.empty()) result.push_back(std::move(program));
        }
        RegCloseKey(handle);
    }
    return result;
}
#endif

// ES-DE's find rules: emulator name (upper case) -> its rules in order (type, entries).
struct EsdeRule { std::wstring type; std::vector<std::wstring> entries; };
std::wstring xmlText(std::string_view text) {
    std::string out;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '&') { out += text[i]; continue; }
        const auto end = text.find(';', i);
        if (end == std::string_view::npos) { out += text[i]; continue; }
        const auto entity = text.substr(i + 1, end - i - 1);
        if (entity == "amp") out += '&'; else if (entity == "lt") out += '<'; else if (entity == "gt") out += '>';
        else if (entity == "quot") out += '"'; else if (entity == "apos") out += '\''; else { out += text[i]; continue; }
        i = end;
    }
    auto wide = text_encoding::wideLenient(out);
    const auto first = wide.find_first_not_of(L" \t\r\n"), last = wide.find_last_not_of(L" \t\r\n");
    return first == std::wstring::npos ? std::wstring{} : wide.substr(first, last - first + 1);
}
std::unordered_map<std::wstring, std::vector<EsdeRule>> esdeRules(const Context& context) {
    std::unordered_map<std::wstring, std::vector<EsdeRule>> result;
    if (context.esde_install.empty()) return result;
    const auto file = context.esde_install / L"resources" / L"systems" / (context.windows ? L"windows" : L"linux") / L"es_find_rules.xml";
    const auto text = file_io::readTextWithoutBom(file, 4u << 20);
    // Comments out first: their text may hold tags.
    std::string clean;
    for (size_t at = 0; at < text.size();) {
        const auto open = text.find("<!--", at);
        if (open == std::string::npos) { clean.append(text, at, std::string::npos); break; }
        clean.append(text, at, open - at);
        const auto close = text.find("-->", open + 4);
        if (close == std::string::npos) break;
        at = close + 3;
    }
    for (size_t at = clean.find("<emulator name=\""); at != std::string::npos; at = clean.find("<emulator name=\"", at + 1)) {
        const auto name_end = clean.find('"', at + 16);
        const auto close = clean.find("</emulator>", at);
        if (name_end == std::string::npos || close == std::string::npos) break;
        auto& rules = result[lower(xmlText(std::string_view(clean).substr(at + 16, name_end - at - 16)))];
        const std::string_view body(clean.data() + name_end, close - name_end);
        for (size_t rule = body.find("<rule type=\""); rule != std::string_view::npos; rule = body.find("<rule type=\"", rule + 1)) {
            const auto type_end = body.find('"', rule + 12), rule_end = body.find("</rule>", rule);
            if (type_end == std::string_view::npos || rule_end == std::string_view::npos) break;
            EsdeRule entry{xmlText(body.substr(rule + 12, type_end - rule - 12)), {}};
            for (size_t item = body.find("<entry>", type_end); item != std::string_view::npos && item < rule_end; item = body.find("<entry>", item + 1)) {
                const auto item_end = body.find("</entry>", item);
                if (item_end == std::string_view::npos || item_end > rule_end) break;
                entry.entries.push_back(xmlText(body.substr(item + 7, item_end - item - 7)));
            }
            rules.push_back(std::move(entry));
        }
    }
    return result;
}

// One search over a context: what is read once (EmuDeck's launchers, ES-DE's rules, installed programs).
class Probe {
public:
    explicit Probe(const Context& context) : context_(context) {
        std::error_code error;
        if (!context.tools.empty())
            for (std::filesystem::directory_iterator it(context.tools / L"launchers", error), end; !error && it != end; it.increment(error)) {
                const auto extension = lower(it->path().extension().wstring());
                if (extension == (context.windows ? L".ps1" : L".sh")) scripts_.emplace(lower(it->path().stem().wstring()), it->path());
            }
        esde_ = esdeRules(context);
    }
    Found detect(const Info& info, const Override* manual) {
        Found found;
        found.id = std::wstring(info.id);
        const auto take = [&](Via via, path program, std::wstring detail = {}) {
            found.via = via; found.target = program; found.program = std::move(program); found.detail = std::move(detail);
            return found;
        };
        if (manual && fileExists(manual->program)) return take(Via::manual, manual->program.lexically_normal());
        if (info.manual_only) return found;
        // EmuDeck's launcher, unless the program it names is gone.
        if (const auto script = scripts_.find(lower(info.script)); !info.script.empty() && script != scripts_.end()) {
            const auto target = scriptTarget(script->second, context_);
            if (target.empty() || fileExists(target)) {
                found.via = Via::emudeck_script; found.program = script->second; found.target = target;
                found.detail = script->second.filename().wstring();
                return found;
            }
        }
        if (context_.windows && !context_.emudeck.empty())
            for (const auto exe : info.exe_names)
                if (!exe.empty() && fileExists(context_.emudeck / info.emudeck_folder / exe)) return take(Via::emudeck_exe, context_.emudeck / info.emudeck_folder / exe);
        if (auto esde = fromEsde(info); !esde.first.empty()) return take(Via::esde_rule, std::move(esde.first), std::move(esde.second));
        if (context_.windows) {
#ifdef _WIN32
            if (context_.registry) {
                if (!info.shared_exe)
                    for (const auto exe : info.exe_names)
                        if (!exe.empty()) if (auto program = appPath(exe); !program.empty()) return take(Via::registry, std::move(program), L"App Paths");
                if (!installed_) installed_ = installedPrograms();
                const auto name = lower(info.name);
                for (const auto& program : *installed_) {
                    if (lower(program.name).find(name) == std::wstring::npos) continue;
                    for (const auto exe : info.exe_names) {
                        if (exe.empty()) continue;
                        if (auto file = program.location.empty() ? path{} : existing(program.location / exe); !file.empty()) return take(Via::registry, std::move(file), program.name);
                        if (lower(program.icon.filename().wstring()) == lower(exe) && fileExists(program.icon)) return take(Via::registry, program.icon, program.name);
                    }
                }
            }
#endif
            for (const auto& base : knownBases())
                for (const auto dir : info.known_dirs)
                    for (const auto exe : info.exe_names)
                        if (!dir.empty() && !exe.empty()) if (auto file = existing(base / dir / exe); !file.empty()) return take(Via::known_path, std::move(file));
            if (!info.shared_exe)
                for (const auto& folder : context_.search_path)
                    for (const auto exe : info.exe_names)
                        if (!exe.empty()) if (auto file = existing(folder / exe); !file.empty()) return take(Via::path_env, std::move(file));
            return found;
        }
        for (const auto& folder : context_.search_path)
            for (const auto name : info.linux_names)
                if (!name.empty()) if (auto file = existing(folder / name); !file.empty()) return take(Via::path_env, std::move(file));
        if (!info.flatpak.empty())
            for (const auto& apps : {path(L"/var/lib/flatpak/app"), context_.home / L".local/share/flatpak/app"})
                if (folderExists(apps / info.flatpak)) {
                    auto flatpak = existing(L"/usr/bin/flatpak");
                    if (flatpak.empty()) flatpak = L"flatpak";
                    take(Via::flatpak, flatpak, std::wstring(info.flatpak));
                    found.prefix = L"run " + std::wstring(info.flatpak);
                    return found;
                }
        // An AppImage in ~/Applications whose name starts with the emulator's.
        std::error_code error;
        for (std::filesystem::directory_iterator it(context_.home / L"Applications", error), end; !error && it != end; it.increment(error)) {
            const auto file = lower(it->path().filename().wstring());
            if (!file.ends_with(L".appimage")) continue;
            for (const auto name : info.linux_names)
                if (!name.empty() && file.starts_with(lower(name))) return take(Via::appimage, it->path());
            if (file.starts_with(lower(info.id))) return take(Via::appimage, it->path());
        }
        return found;
    }

private:
    std::pair<path, std::wstring> fromEsde(const Info& info) {
        for (const auto name : info.esde_names) {
            if (name.empty()) continue;
            const auto rules = esde_.find(lower(name));
            if (rules == esde_.end()) continue;
            for (const auto& rule : rules->second)
                for (const auto& entry : rule.entries) {
                    path file;
                    if (rule.type == L"staticpath") file = existing(path(expand(entry, context_, context_.esde_install)).lexically_normal());
                    else if (rule.type == L"systempath") { for (const auto& folder : context_.search_path) if (file = existing(folder / entry); !file.empty()) break; }
#ifdef _WIN32
                    else if (rule.type == L"winregistrypath" && context_.registry) file = appPath(entry);
                    else if (rule.type == L"winregistryvalue" && context_.registry) file = registryValue(entry);
#endif
                    if (!file.empty()) return {file, L"ES-DE: " + std::wstring(name) + L" (" + rule.type + L")"};
                }
        }
        return {};
    }
    std::vector<path> knownBases() const {
        auto bases = context_.program_dirs;
        if (!context_.home.empty()) bases.push_back(context_.home / L"scoop" / L"apps");
        if (!context_.steam.empty()) bases.push_back(context_.steam / L"steamapps" / L"common");
        if (!context_.system_drive.empty()) bases.push_back(context_.system_drive);
        // Scoop keeps each app in apps\<name>\current.
        std::vector<path> result;
        for (const auto& base : bases) if (!base.empty()) result.push_back(base);
        return result;
    }

    const Context& context_;
    std::unordered_map<std::wstring, path> scripts_;
    std::unordered_map<std::wstring, std::vector<EsdeRule>> esde_;
#ifdef _WIN32
    std::optional<std::vector<Installed>> installed_;
#endif
};
} // namespace

std::span<const Info> catalog() { return table; }

const Info* find(std::wstring_view id) {
    for (const auto& info : table) if (info.id == id) return &info;
    return nullptr;
}

path scriptTarget(const path& script, const Context& context) {
    const auto text = text_encoding::wideLenient(file_io::readTextWithoutBom(script, 16384));
    const auto at = text.find(L"emulatorFile");
    if (at == std::wstring::npos) return {};
    const auto line_end = text.find(L'\n', at);
    const auto equal = text.find(L'=', at);
    if (equal == std::wstring::npos || (line_end != std::wstring::npos && equal > line_end)) return {};
    auto value = text.substr(equal + 1, line_end == std::wstring::npos ? std::wstring::npos : line_end - equal - 1);
    const auto first = value.find_first_not_of(L" \t"), last = value.find_last_not_of(L" \t\r;");
    if (first == std::wstring::npos) return {};
    value = value.substr(first, last - first + 1);
    if (value.size() >= 2 && (value.front() == L'"' || value.front() == L'\'') && value.back() == value.front()) value = value.substr(1, value.size() - 2);
    else if (value.find_first_of(L" (") != std::wstring::npos) return {}; // An expression, not a path.
    value = expand(value, context);
    if (value.empty()) return {};
    return path(value).lexically_normal();
}

std::wstring defaultArguments(const Info& info, bool script, bool core) {
    if (core) return L"-L %CORE%";
    return std::wstring(script ? info.script_args : info.direct_args);
}

Found detect(const Info& info, const Context& context, const Override* manual) {
    Probe probe(context);
    return probe.detect(info, manual);
}

std::vector<Found> detectAll(const Context& context, const std::map<std::string, Override>& overrides) {
    Probe probe(context);
    std::vector<Found> result;
    for (const auto& info : table) {
        const auto manual = overrides.find(text_encoding::utf8(std::wstring(info.id)));
        result.push_back(probe.detect(info, manual == overrides.end() ? nullptr : &manual->second));
    }
    return result;
}
} // namespace emulators
