#include "emulation_scan.hpp"
#include "emulation_platforms.hpp"
#include "esde_gamelist.hpp"
#include "shortcuts.hpp"
#include "../net/json.hpp"
#include "../platform/file_io.hpp"
#include "../platform/system.hpp"
#include "../platform/text_encoding.hpp"
#include "../platform/unicode_text.hpp"

#include <algorithm>
#include <cwctype>
#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <unordered_map>
#include <unordered_set>

namespace emulation_scan {
namespace {
constexpr size_t most_games = 20000, most_files = 30000, most_depth = 6;

std::wstring lower(std::wstring_view text) {
    std::wstring result(text);
    for (auto& ch : result) ch = static_cast<wchar_t>(std::towlower(ch));
    return result;
}
std::wstring wide(std::string_view text) { return text_encoding::wideLenient(text); }
std::wstring trim(std::wstring_view text) {
    size_t first = 0, last = text.size();
    while (first < last && std::iswspace(text[first])) ++first;
    while (last > first && std::iswspace(text[last - 1])) --last;
    return std::wstring(text.substr(first, last - first));
}
bool folderExists(const std::filesystem::path& path) { return !path.empty() && file_io::isFolder(path); }
bool fileExists(const std::filesystem::path& path) { return !path.empty() && file_io::isFile(path); }
std::optional<json::Value> readJson(const std::filesystem::path& file) {
    const auto text = file_io::readTextWithoutBom(file, 32u << 20);
    if (text.empty()) return std::nullopt;
    return json::parse(text, 32u << 20);
}
std::wstring field(const json::Value* value, std::string_view name) {
    const auto* found = value ? value->get(name) : nullptr;
    return found && found->type == json::Value::Type::string ? wide(found->text) : std::wstring{};
}
std::filesystem::path normalPath(std::wstring text) {
    text = trim(text);
    if (text.size() >= 2 && text.front() == L'"' && text.back() == L'"') text = text.substr(1, text.size() - 2);
    if (text.empty()) return {};
    return std::filesystem::path(text).lexically_normal();
}
// `name=value`, `$name="value"` or `name="value"` lines of EmuDeck's settings.ps1 / settings.sh.
std::wstring settingsValue(const std::filesystem::path& file, std::wstring_view name) {
    const auto text = wide(file_io::readTextWithoutBom(file, 1u << 20));
    size_t at = 0;
    while (at < text.size()) {
        auto end = text.find(L'\n', at);
        if (end == std::wstring::npos) end = text.size();
        auto line = trim(std::wstring_view(text).substr(at, end - at));
        at = end + 1;
        if (!line.empty() && line.front() == L'$') line.erase(line.begin());
        if (line.size() <= name.size() || line.compare(0, name.size(), name) != 0 || line[name.size()] != L'=') continue;
        auto value = trim(std::wstring_view(line).substr(name.size() + 1));
        if (value.size() >= 2 && (value.front() == L'"' || value.front() == L'\'') && value.back() == value.front()) value = value.substr(1, value.size() - 2);
        return value;
    }
    return {};
}
std::filesystem::path emuDeckSettings(const Environment& environment) {
    return environment.windows ? environment.app_data / L"EmuDeck" / L"settings.ps1" : environment.app_data / L"EmuDeck" / L"settings.sh";
}
// SRM's userSettings.json environment variables (EmuDeck keeps a portable copy in <tools>\userData).
const json::Value* environmentVariables(const std::optional<json::Value>& settings) {
    return settings ? settings->get("environmentVariables") : nullptr;
}
std::filesystem::path esdeRomDirectory(const Environment& environment) {
    if (environment.esde_root.empty()) return {};
    const auto text = file_io::readTextWithoutBom(environment.esde_root / L"settings" / L"es_settings.xml", 1u << 20);
    const auto at = text.find("name=\"ROMDirectory\"");
    if (at == std::string::npos) return {};
    const auto value = text.find("value=\"", at);
    if (value == std::string::npos) return {};
    const auto end = text.find('"', value + 7);
    if (end == std::string::npos) return {};
    auto folder = wide(std::string_view(text).substr(value + 7, end - value - 7));
    if (folder.find(L'%') != std::wstring::npos) return {}; // %ESPATH% and the like: ES-DE's own relative forms.
    if (folder.starts_with(L"~")) folder = environment.home.wstring() + folder.substr(1);
    return normalPath(folder);
}
std::filesystem::path toolsFolder(const Environment& environment, const std::filesystem::path& roms) {
    if (auto tools = normalPath(settingsValue(emuDeckSettings(environment), L"toolsPath")); folderExists(tools)) return tools;
    if (!roms.empty() && folderExists(roms.parent_path() / L"tools")) return roms.parent_path() / L"tools";
    return {};
}

// --- Globs --------------------------------------------------------------------------------------
std::wstring regexEscape(wchar_t ch) {
    static constexpr std::wstring_view special = L".^$|()[]{}*+?\\";
    return special.find(ch) != std::wstring_view::npos ? std::wstring{L'\\', ch} : std::wstring(1, ch);
}
// The matching ')' or '}' for an opening at text[at].
size_t closing(std::wstring_view text, size_t at, wchar_t open, wchar_t close) {
    int depth = 0;
    for (size_t i = at; i < text.size(); ++i) {
        if (text[i] == L'\\' && i + 1 < text.size()) { ++i; continue; }
        if (text[i] == open) ++depth;
        else if (text[i] == close && --depth == 0) return i;
    }
    return std::wstring_view::npos;
}
std::vector<std::wstring_view> splitTop(std::wstring_view text, wchar_t separator) {
    std::vector<std::wstring_view> parts;
    int round = 0, curly = 0;
    size_t start = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        const auto ch = text[i];
        if (ch == L'\\' && i + 1 < text.size()) { ++i; continue; }
        if (ch == L'(') ++round; else if (ch == L')') --round;
        else if (ch == L'{') ++curly; else if (ch == L'}') --curly;
        else if (ch == separator && !round && !curly) { parts.push_back(text.substr(start, i - start)); start = i + 1; }
    }
    parts.push_back(text.substr(start));
    return parts;
}
// '\x01' stands for ${title}.
std::wstring globRegex(std::wstring_view glob) {
    std::wstring out;
    for (size_t i = 0; i < glob.size(); ++i) {
        const auto ch = glob[i];
        if (ch == L'\x01') { out += L"([^/]+?)"; continue; }
        if (ch == L'\\' && i + 1 < glob.size()) { out += regexEscape(glob[++i]); continue; }
        if ((ch == L'@' || ch == L'!' || ch == L'+' || ch == L'*' || ch == L'?') && i + 1 < glob.size() && glob[i + 1] == L'(') {
            const auto end = closing(glob, i + 1, L'(', L')');
            if (end != std::wstring_view::npos) {
                std::wstring group;
                for (const auto part : splitTop(glob.substr(i + 2, end - i - 2), L'|')) group += (group.empty() ? L"" : L"|") + globRegex(part);
                if (ch == L'!') out += L"(?:(?!(?:" + group + L")(?:/|$))[^/]*)";
                else out += L"(?:" + group + L")" + (ch == L'@' ? L"" : std::wstring(1, ch));
                i = end;
                continue;
            }
        }
        if (ch == L'{') {
            const auto end = closing(glob, i, L'{', L'}');
            if (end != std::wstring_view::npos) {
                std::wstring group;
                for (const auto part : splitTop(glob.substr(i + 1, end - i - 1), L',')) group += (group.empty() ? L"" : L"|") + globRegex(part);
                out += L"(?:" + group + L")";
                i = end;
                continue;
            }
        }
        if (ch == L'*') {
            if (i + 1 < glob.size() && glob[i + 1] == L'*') {
                if (i + 2 < glob.size() && glob[i + 2] == L'/') { out += L"(?:.*/)?"; i += 2; }
                else { out += L".*"; ++i; }
            } else out += L"[^/]*";
            continue;
        }
        if (ch == L'?') { out += L"[^/]"; continue; }
        if (ch == L'[') {
            const auto end = glob.find(L']', i + 1);
            if (end != std::wstring_view::npos) {
                auto inside = std::wstring(glob.substr(i + 1, end - i - 1));
                if (!inside.empty() && inside.front() == L'!') inside.front() = L'^';
                out += L"[" + inside + L"]";
                i = end;
                continue;
            }
        }
        out += regexEscape(ch);
    }
    return out;
}
struct CompiledGlob {
    std::wregex pattern;
    bool title = false, valid = false;
};
const CompiledGlob& compiled(std::wstring_view glob) {
    static thread_local std::unordered_map<std::wstring, CompiledGlob> cache;
    const std::wstring key(glob);
    if (const auto found = cache.find(key); found != cache.end()) return found->second;
    CompiledGlob result;
    auto text = key;
    for (size_t at = text.find(L"${title}"); at != std::wstring::npos; at = text.find(L"${title}", at + 1)) { text.replace(at, 8, L"\x01"); result.title = true; }
    // Other variables: ${/} and the like (a parser's own paths never sit in its glob).
    for (size_t at = text.find(L"${/}"); at != std::wstring::npos; at = text.find(L"${/}", at)) text.replace(at, 4, L"/");
    try {
        result.pattern = std::wregex(L"^" + globRegex(text) + L"$", std::regex_constants::ECMAScript | std::regex_constants::icase | std::regex_constants::optimize);
        result.valid = true;
    } catch (const std::regex_error&) {}
    if (cache.size() > 512) cache.clear();
    return cache.emplace(key, std::move(result)).first->second;
}

// --- Systems and files ----------------------------------------------------------------------------
// ES-DE / EmuDeck folders the platform table does not name, and folders that hold no games.
const std::pair<std::wstring_view, std::wstring_view> extra_systems[] = {
    {L"snesna", L"SNES"}, {L"sneshd", L"SNES"}, {L"satellaview", L"SNES"}, {L"sufami", L"SNES"}, {L"sgb", L"Game Boy"},
    {L"fds", L"NES"}, {L"n64dd", L"N64"}, {L"primehack", L"Wii"}, {L"megadrivejp", L"Mega Drive"}, {L"genesiswide", L"Mega Drive"},
    {L"megacd", L"Mega Drive"}, {L"megacdjp", L"Mega Drive"}, {L"segacd", L"Mega Drive"}, {L"saturnjp", L"Saturn"},
    {L"amiga600", L"Amiga"}, {L"amiga1200", L"Amiga"}, {L"cdtv", L"Amiga"},
    {L"fbneo", L"Arcade"}, {L"fba", L"Arcade"}, {L"mame-advmame", L"Arcade"}, {L"mame-mame4all", L"Arcade"}, {L"naomi", L"Arcade"},
    {L"naomigd", L"Arcade"}, {L"atomiswave", L"Arcade"}, {L"cps", L"Arcade"}, {L"cps1", L"Arcade"}, {L"cps2", L"Arcade"},
    {L"cps3", L"Arcade"}, {L"neogeo", L"Arcade"}, {L"model2", L"Arcade"}, {L"model3", L"Arcade"}, {L"daphne", L"Arcade"},
};
bool skippedSystem(std::wstring_view system) {
    static constexpr std::wstring_view skipped[] = {L"steam", L"desktop", L"cloud", L"remoteplay", L"moonlight", L"kodi", L"epic",
                                                    L"lutris", L"android", L"ports", L"pc", L"windows", L"bios", L"media"};
    return std::find(std::begin(skipped), std::end(skipped), system) != std::end(skipped);
}
bool skippedFolder(const std::wstring& name) {
    if (name.empty() || name.front() == L'.') return true;
    static constexpr std::wstring_view skipped[] = {L"media", L"bios", L"cheats", L"saves", L"manuals", L"images", L"videos",
                                                    L"downloaded_media", L"screenshots", L"covers", L"snaps", L"titles"};
    const auto key = lower(name);
    return std::find(std::begin(skipped), std::end(skipped), key) != std::end(skipped);
}
// Loose files at the top of the ROM folder: extensions only one system uses.
const std::pair<std::wstring_view, std::wstring_view> extension_systems[] = {
    {L".nsp", L"switch"}, {L".nsz", L"switch"}, {L".xci", L"switch"}, {L".xcz", L"switch"},
    {L".wux", L"wiiu"}, {L".wud", L"wiiu"}, {L".wua", L"wiiu"}, {L".rpx", L"wiiu"},
    {L".rvz", L"wii"}, {L".wbfs", L"wii"}, {L".wia", L"wii"}, {L".gcm", L"gc"}, {L".ciso", L"gc"},
    {L".3ds", L"n3ds"}, {L".cci", L"n3ds"}, {L".cia", L"n3ds"}, {L".nds", L"nds"},
    {L".gba", L"gba"}, {L".gbc", L"gbc"}, {L".gb", L"gb"}, {L".sfc", L"snes"}, {L".smc", L"snes"}, {L".nes", L"nes"},
    {L".z64", L"n64"}, {L".n64", L"n64"}, {L".v64", L"n64"}, {L".adf", L"amiga"}, {L".adz", L"amiga"}, {L".lha", L"amiga"},
    {L".hdf", L"amiga"}, {L".ipf", L"amiga"}, {L".dms", L"amiga"}, {L".cso", L"psp"}, {L".pbp", L"psp"}, {L".gdi", L"dreamcast"},
    {L".cdi", L"dreamcast"}, {L".xex", L"xbox360"}, {L".md", L"megadrive"}, {L".gen", L"megadrive"}, {L".smd", L"megadrive"},
    {L".sms", L"mastersystem"}, {L".gg", L"gamegear"}, {L".pce", L"pcengine"}, {L".32x", L"sega32x"}, {L".ws", L"wonderswan"},
    {L".wsc", L"wonderswancolor"}, {L".ngp", L"ngp"}, {L".ngc", L"ngpc"}, {L".lnx", L"atarilynx"}, {L".a26", L"atari2600"},
    {L".j64", L"atarijaguar"}, {L".vb", L"virtualboy"}, {L".tic", L"tic80"}, {L".p8", L"pico8"},
};
std::wstring extensionSystem(std::wstring_view extension) {
    for (const auto& [ext, system] : extension_systems) if (ext == extension) return std::wstring(system);
    return {};
}
// Images that need a look inside (or their size) to tell the console.
const std::wstring_view shared_images[] = {L".iso", L".bin", L".cue", L".chd", L".img", L".mdf", L".nrg", L".ccd", L".mds",
                                           L".m3u", L".zip", L".7z", L".ecm", L".toc", L".gcz", L".dol", L".elf", L".wad"};

struct Listing {
    std::vector<std::filesystem::path> files;
};
Listing listFolder(const std::filesystem::path& folder, const Cancelled& cancelled) {
    Listing result;
    std::error_code error;
    std::filesystem::recursive_directory_iterator it(folder, std::filesystem::directory_options::skip_permission_denied, error), end;
    for (; !error && it != end; it.increment(error)) {
        if (cancelled && cancelled()) break;
        std::error_code kind;
        if (it->is_directory(kind)) {
            if (static_cast<size_t>(it.depth()) >= most_depth || skippedFolder(it->path().filename().wstring())) it.disable_recursion_pending();
            continue;
        }
        if (!it->is_regular_file(kind)) continue;
        result.files.push_back(it->path());
        if (result.files.size() >= most_files) break;
    }
    return result;
}
std::wstring relativeText(const std::filesystem::path& file, const std::filesystem::path& folder) {
    auto text = file.lexically_relative(folder).generic_wstring();
    return text;
}
// Discs a playlist or a cue sheet lists (beside it), hidden behind it the way ES-DE and SRM do.
void listedFiles(const std::filesystem::path& sheet, std::unordered_set<std::wstring>& hidden) {
    const auto extension = lower(sheet.extension().wstring());
    std::ifstream input(sheet);
    std::string line;
    size_t lines = 0;
    while (std::getline(input, line) && ++lines < 4096) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) line.pop_back();
        std::string name;
        if (extension == L".m3u") {
            const auto first = line.find_first_not_of(" \t\xEF\xBB\xBF");
            if (first == std::string::npos || line[first] == '#') continue;
            name = line.substr(first);
        } else if (extension == L".cue") {
            const auto at = line.find("FILE ");
            if (at == std::string::npos) continue;
            const auto open = line.find('"', at), close = open == std::string::npos ? std::string::npos : line.find('"', open + 1);
            if (close == std::string::npos) continue;
            name = line.substr(open + 1, close - open - 1);
        } else if (extension == L".gdi") {
            // "1 0 4 2352 track01.bin 0" (a quoted name may hold spaces).
            const auto open = line.find('"');
            if (open != std::string::npos) { const auto close = line.find('"', open + 1); if (close != std::string::npos) name = line.substr(open + 1, close - open - 1); }
            else {
                std::vector<std::string> words;
                size_t at = 0;
                while (at < line.size()) {
                    const auto start = line.find_first_not_of(" \t", at);
                    if (start == std::string::npos) break;
                    const auto stop = line.find_first_of(" \t", start);
                    words.push_back(line.substr(start, stop == std::string::npos ? std::string::npos : stop - start));
                    at = stop == std::string::npos ? line.size() : stop;
                }
                if (words.size() >= 5) name = words[4];
            }
        }
        if (name.empty()) continue;
        std::filesystem::path listed(text_encoding::wideLegacy(name));
        if (listed.is_relative()) listed = sheet.parent_path() / listed;
        hidden.insert(romKey(listed.lexically_normal()));
    }
}
struct Exception {
    std::wstring title;
    bool exclude = false;
    double time = 0;
};
// SRM's userExceptions.json: "<extracted title> ${id:…}" -> exclude / newTitle, the newest per title.
std::unordered_map<std::wstring, Exception> loadExceptions(const Paths& paths) {
    std::unordered_map<std::wstring, Exception> result;
    for (const auto& folder : {paths.tools / L"userData", paths.srm}) {
        if (folder.empty()) continue;
        const auto document = readJson(folder / L"userExceptions.json");
        const auto* titles = document ? document->get("titles") : nullptr;
        if (!titles || titles->type != json::Value::Type::object) continue;
        for (const auto& [key, entry] : titles->properties) {
            auto title = wide(key);
            if (const auto id = title.rfind(L" ${id:"); id != std::wstring::npos) title.resize(id);
            Exception exception;
            exception.title = trim(field(&entry, "newTitle"));
            const auto* exclude = entry.get("exclude");
            exception.exclude = exclude && exclude->type == json::Value::Type::boolean && exclude->flag;
            const auto* time = entry.get("timeStamp");
            exception.time = time && time->type == json::Value::Type::number ? time->numeric : 0;
            auto& slot = result[lower(trim(title))];
            if (exception.time >= slot.time) slot = std::move(exception);
        }
    }
    return result;
}
// --- Built-in parsers ---------------------------------------------------------------------------
// EmuDeck's Steam ROM Manager parsers as launchme's own: each system folder, the files it takes and
// its emulators (EmuDeck launcher, its arguments before the ROM; the program in EmuDeck's Emulators
// folder with its own). RetroArch systems name their core; one table, no Steam ROM Manager needed.
struct Builtin {
    std::wstring_view system, extensions, core;
    std::wstring_view glob = {}; // Empty: "**/${title}@(<extensions>)".
    std::array<std::array<std::wstring_view, 5>, 4> emulators = {}; // launcher, arguments, folder, executable, direct.
};
using E = std::array<std::wstring_view, 5>;
constexpr E duckstation{L"duckstation", L"'-fullscreen'", L"duckstation", L"duckstation-qt-x64-ReleaseLTCG.exe", L"-fullscreen"};
constexpr E pcsx2{L"pcsx2", L"", L"PCSX2-Qt", L"pcsx2-qtx64.exe", L""};
constexpr E rpcs3{L"rpcs3", L"'--no-gui'", L"RPCS3", L"rpcs3.exe", L"--no-gui"};
constexpr E ppsspp{L"ppsspp", L"'-f' '-g'", L"PPSSPP", L"PPSSPPWindows64.exe", L"--fullscreen"};
constexpr E dolphin{L"dolphin", L"'-b' '-e'", L"Dolphin-x64", L"Dolphin.exe", L"-b -e"};
constexpr E primehack{L"primehack", L"-b -e", L"primehack", L"Dolphin.exe", L"-b -e"};
constexpr E cemu{L"cemu", L"'-f' '-g'", L"cemu", L"Cemu.exe", L"-f -g"};
constexpr E eden{L"eden", L"'-f' '-g'", L"eden", L"eden.exe", L"-f -g"};
constexpr E citron{L"citron", L"'-f' '-g'", L"citron", L"citron.exe", L"-f -g"};
constexpr E ryujinx{L"ryujinx", L"'-f'", L"Ryujinx", L"Ryujinx.exe", L"-f"};
constexpr E yuzu{L"yuzu", L"'-f' '-g'", L"yuzu", L"yuzu.exe", L"-f -g"};
constexpr E azahar{L"azahar", L"", L"azahar", L"azahar.exe", L""};
constexpr E melonds{L"melonds", L"'-f'", L"melonDS", L"melonDS.exe", L"-f"};
constexpr E xemu{L"xemu", L"-full-screen -dvd_path", L"xemu", L"xemu.exe", L"-full-screen -dvd_path"};
constexpr E xenia{L"xenia", L"", L"xenia", L"xenia_canary.exe", L""};
constexpr E retroarch{L"retroarch", L"", L"RetroArch", L"retroarch.exe", L""};
constexpr std::wstring_view disc = L".bin|.chd|.cso|.dump|.gz|.img|.iso|.mdf|.nrg";
constexpr std::wstring_view dolphin_discs = L".ciso|.dol|.elf|.gcm|.gcz|.iso|.json|.nkit.iso|.nkit.gcz|.rvz|.wad|.wia|.wbfs|.m3u";
constexpr std::wstring_view amiga = L".adf|.adz|.dms|.fdi|.ipf|.hdf|.hdz|.lha|.slave|.info|.cue|.ccd|.chd|.nrg|.mds|.iso|.uae|.m3u|.zip|.7z";
const Builtin builtin[] = {
    {L"psx", L".cue|.img|.chd|.ecm|.iso|.m3u|.mds|.pbp", {}, {}, {duckstation}},
    {L"ps2", disc, {}, {}, {pcsx2}},
    {L"ps3", {}, {}, L"**/${title}/PS3_GAME/USRDIR/@(eboot.bin|iso.bin.edat)", {rpcs3}},
    {L"psp", L".7z|.cso|.elf|.iso|.pbp|.prx", {}, {}, {ppsspp}},
    {L"gc", dolphin_discs, {}, {}, {dolphin}},
    {L"wii", dolphin_discs, {}, {}, {dolphin}},
    {L"primehack", dolphin_discs, {}, {}, {primehack}},
    {L"wiiu", {}, {}, L"${title}/**/*.rpx", {cemu}},
    {L"wiiu", L".wux|.wud|.wua|.wad|.iso|.7z|.zip", {}, L"${title}@(.wux|.wud|.wua|.wad|.iso|.7z|.zip)", {cemu}},
    {L"switch", L".kip|.nca|.nro|.nso|.nsp|.xci", {}, {}, {eden, citron, ryujinx, yuzu}},
    {L"n3ds", L".3ds|.3dsx|.app|.axf|.cii|.cxi|.elf|.cci|.zcci|.z3dsx|.zcxi", {}, {}, {azahar}},
    {L"nds", L".nds|.app", {}, {}, {melonds}},
    {L"xbox", L".iso", {}, {}, {xemu}},
    {L"xbox360", L".iso|.xex", {}, L"${title}@(.iso|.xex)", {xenia}},
    {L"gb", L".7z|.gb|.dmg|.zip", L"gambatte_libretro"},
    {L"gbc", L".7z|.gb|.gbc|.zip", L"gambatte_libretro"},
    {L"gba", L".7z|.gba|.zip", L"mgba_libretro"},
    {L"nes", L".7z|.nes|.fds|.unf|.unif|.zip", L"mesen_libretro"},
    {L"famicom", L".7z|.nes|.fds|.unf|.unif|.zip", L"mesen_libretro"},
    {L"snes", L".7z|.bs|.fig|.sfc|.smc|.swc|.zip", L"snes9x_libretro"},
    {L"sfc", L".7z|.bs|.fig|.sfc|.smc|.swc|.zip", L"snes9x_libretro"},
    {L"sneshd", L".7z|.bs|.fig|.sfc|.smc|.swc|.zip", L"bsnes_hd_beta_libretro"},
    {L"n64", L".7z|.bin|.n64|.ndd|.u1|.v64|.z64|.zip", L"mupen64plus_next_libretro"},
    {L"virtualboy", L".vb|.vboy|.bin|.7z|.zip", L"mednafen_vb_libretro"},
    {L"dreamcast", L".7z|.cdi|.chd|.cue|.gdi|.m3u", L"flycast_libretro"},
    {L"naomi", L".zip", L"flycast_libretro"},
    {L"saturn", L".7z|.cue|.iso|.zip|.chd|.m3u", L"mednafen_saturn_libretro"},
    {L"megadrive", L".7z|.bin|.gen|.md|.smd|.zip|.cue", L"genesis_plus_gx_libretro"},
    {L"genesis", L".7z|.bin|.gen|.md|.smd|.zip|.cue", L"genesis_plus_gx_libretro"},
    {L"mastersystem", L".7z|.sms|.zip", L"genesis_plus_gx_libretro"},
    {L"gamegear", L".7z|.gg|.zip", L"genesis_plus_gx_libretro"},
    {L"segacd", L".m3u|.7z|.iso|.cue|.chd|.zip", L"genesis_plus_gx_libretro"},
    {L"megacd", L".m3u|.7z|.iso|.cue|.chd|.zip", L"genesis_plus_gx_libretro"},
    {L"sega32x", L".7z|.32x|.bin|.zip", L"picodrive_libretro"},
    {L"amiga", amiga, L"puae_libretro"},
    {L"amiga600", amiga, L"puae_libretro"},
    {L"amiga1200", amiga, L"puae_libretro"},
    {L"amigacd32", amiga, L"puae_libretro"},
    {L"arcade", L".zip", L"mame_libretro"},
    {L"mame", L".zip", L"mame2003_plus_libretro"},
    {L"fbneo", L".7z|.zip", L"fbneo_libretro"},
    {L"dos", L".7z|.zip", L"dosbox_pure_libretro"},
    {L"pcengine", L".7z|.bin|.pce|.zip", L"mednafen_pce_libretro"},
    {L"pcenginecd", L".7z|.ccd|.chd|.cue|.iso|.zip", L"mednafen_pce_libretro"},
    {L"tg16", L".7z|.bin|.pce|.zip", L"mednafen_pce_libretro"},
    {L"pcfx", L".ccd|.chd|.cue|.m3u|.toc|.7z|.zip", L"mednafen_pcfx_libretro"},
    {L"atari2600", L".7z|.a26|.bin|.zip", L"stella_libretro"},
    {L"atarilynx", L".7z|.lnx|.zip", L"mednafen_lynx_libretro"},
    {L"atarijaguar", L".j64|.jag|.rom|.abs|.cof|.bin|.prg|.7z|.zip", L"virtualjaguar_libretro"},
    {L"wonderswan", L".7z|.pc2|.ws|.wsc|.zip", L"mednafen_wswan_libretro"},
    {L"wonderswancolor", L".7z|.pc2|.ws|.wsc|.zip", L"mednafen_wswan_libretro"},
    {L"ngp", L".7z|.ngc|.ngp|.zip", L"mednafen_ngp_libretro"},
    {L"ngpc", L".7z|.ngc|.ngp|.zip", L"mednafen_ngp_libretro"},
    {L"c64", L".d64|.d71|.d81|.g64|.t64|.tap|.prg|.p00|.crt|.m3u|.zip", L"vice_x64_libretro"},
    {L"zxspectrum", L".7z|.tzx|.tap|.z80|.rzx|.scl|.trd|.zip", L"fuse_libretro"},
    {L"3do", L".iso|.bin|.chd|.cue", L"opera_libretro"},
    {L"intellivision", L".int|.bin|.rom|.7z|.zip", L"freeintv_libretro"},
    {L"doom", L".7z|.wad|.iwad|.pwad|.zip", L"prboom_libretro"},
    {L"pico8", L".7z|.zip|.p8|.png", L"retro8_libretro"},
};
std::wstring builtinGlob(const Builtin& entry) {
    if (!entry.glob.empty()) return std::wstring(entry.glob);
    return L"**/${title}@(" + std::wstring(entry.extensions) + L")";
}
// RetroArch's own folder: EmuDeck's, then SRM's core folder's parent, then its usual Windows place.
std::filesystem::path retroArchFolder(const Paths& paths) {
    for (const auto& folder : {paths.emulators.empty() ? std::filesystem::path{} : paths.emulators / L"RetroArch",
                               paths.ra_cores.empty() ? std::filesystem::path{} : std::filesystem::path(paths.ra_cores).parent_path(),
                               std::filesystem::path(L"C:\\RetroArch-Win64")})
        if (fileExists(folder / L"retroarch.exe")) return folder;
    return {};
}
std::wstring powershellQuoted(std::wstring_view text) {
    std::wstring result(1, L'\'');
    for (const auto ch : text) { result += ch; if (ch == L'\'') result += L'\''; }
    return result + L'\'';
}
} // namespace

Environment systemEnvironment() {
    Environment environment;
#ifdef _WIN32
    environment.windows = true;
    environment.app_data = system_info::environment(L"APPDATA", 4095).value_or(std::wstring{});
    environment.home = system_info::environment(L"USERPROFILE", 4095).value_or(std::wstring{});
    environment.system_root = system_info::environment(L"SystemRoot", 4095).value_or(std::wstring(L"C:\\Windows"));
#else
    environment.windows = false;
    environment.home = system_info::environment(L"HOME", 4095).value_or(std::wstring{});
    const auto config = system_info::environment(L"XDG_CONFIG_HOME", 4095).value_or(std::wstring{});
    environment.app_data = !config.empty() ? std::filesystem::path(config) : environment.home / L".config";
#endif
    environment.esde_root = esde::root();
    if (environment.esde_root.empty() && !environment.home.empty() && folderExists(environment.home / L"ES-DE")) environment.esde_root = environment.home / L"ES-DE";
    return environment;
}

std::filesystem::path detectRoms(const Environment& environment, std::wstring* origin) {
    const auto found = [&](const std::filesystem::path& folder, const wchar_t* from) {
        if (!folderExists(folder)) return false;
        if (origin) *origin = from;
        return true;
    };
    if (auto folder = esdeRomDirectory(environment); found(folder, L"ES-DE")) return folder;
    if (auto folder = normalPath(settingsValue(emuDeckSettings(environment), L"romsPath")); found(folder, L"EmuDeck")) return folder;
    const auto tools = normalPath(settingsValue(emuDeckSettings(environment), L"toolsPath"));
    for (const auto& folder : {tools / L"userData", environment.app_data / L"steam-rom-manager" / L"userData"}) {
        if (folder.empty() || folder == L"userData") continue;
        const auto settings = readJson(folder / L"userSettings.json");
        if (auto roms = normalPath(field(environmentVariables(settings), "romsDirectory")); found(roms, L"Steam ROM Manager")) return roms;
    }
    return {};
}

Paths resolve(const Environment& environment, const std::filesystem::path& roms_override) {
    Paths paths;
    if (folderExists(roms_override)) { paths.roms = roms_override.lexically_normal(); paths.origin = L"manual"; }
    else paths.roms = detectRoms(environment, &paths.origin);
    paths.tools = toolsFolder(environment, paths.roms);
    if (!environment.app_data.empty() && folderExists(environment.app_data / L"EmuDeck" / L"Emulators")) paths.emulators = environment.app_data / L"EmuDeck" / L"Emulators";
    paths.srm = environment.app_data.empty() ? std::filesystem::path{} : environment.app_data / L"steam-rom-manager" / L"userData";
    for (const auto& folder : {paths.tools.empty() ? std::filesystem::path{} : paths.tools / L"userData", paths.srm}) {
        if (folder.empty()) continue;
        const auto settings = readJson(folder / L"userSettings.json");
        const auto* variables = environmentVariables(settings);
        if (!variables) continue;
        if (paths.ra_path.empty()) paths.ra_path = field(variables, "retroarchPath");
        if (paths.ra_cores.empty()) paths.ra_cores = field(variables, "raCoresDirectory");
        if (paths.steam.empty()) paths.steam = field(variables, "steamDirectory");
    }
    return paths;
}

std::optional<std::wstring> match(std::wstring_view glob, std::wstring_view relative) {
    const auto& pattern = compiled(glob);
    if (!pattern.valid) return std::nullopt;
    std::wsmatch found;
    const std::wstring text(relative);
    if (!std::regex_match(text, found, pattern.pattern)) return std::nullopt;
    if (pattern.title && found.size() > 1) return found[1].str();
    auto name = std::filesystem::path(text).stem().wstring();
    return name;
}

const Rule* Setup::forFile(const std::wstring& platform, const std::filesystem::path& file) const {
    const auto extension = lower(file.extension().wstring());
    const auto relative = file.filename().generic_wstring();
    const Rule* fallback = nullptr;
    for (const auto& rule : rules) {
        if (rule.platform != platform) continue;
        if (!fallback) fallback = &rule;
        // The rule whose glob takes this file (Wii U: an .rpx in its folder or a .wux disc).
        if (match(rule.glob, relative) || (!extension.empty() && lower(rule.glob).find(extension) != std::wstring::npos)) return &rule;
        if (lower(rule.glob).find(L".rpx") != std::wstring::npos && extension == L".rpx") return &rule;
    }
    return fallback;
}

Setup load(const Environment& environment, const std::filesystem::path& roms_override) {
    Setup setup;
    setup.environment = environment;
    setup.paths = resolve(environment, roms_override);
    for (const auto& entry : builtin) {
        Rule rule;
        rule.system = std::wstring(entry.system);
        rule.platform = platformForSystem(rule.system);
        rule.glob = builtinGlob(entry);
        rule.core = std::wstring(entry.core);
        if (!setup.paths.roms.empty()) rule.folder = setup.paths.roms / rule.system;
        if (!rule.core.empty()) rule.emulators.push_back({L"retroarch", L"", L"RetroArch", L"retroarch.exe", L""});
        for (const auto& emulator : entry.emulators)
            if (!emulator[0].empty())
                rule.emulators.push_back({std::wstring(emulator[0]), std::wstring(emulator[1]), std::wstring(emulator[2]), std::wstring(emulator[3]), std::wstring(emulator[4])});
        // The one this setup has (an EmuDeck launcher, else the program) goes first.
        std::stable_partition(rule.emulators.begin(), rule.emulators.end(), [&](const Emulator& emulator) {
            const auto script = setup.paths.tools / L"launchers" / (emulator.launcher + (environment.windows ? L".ps1" : L".sh"));
            return (!setup.paths.tools.empty() && fileExists(script)) ||
                   (!setup.paths.emulators.empty() && fileExists(setup.paths.emulators / emulator.folder / emulator.executable));
        });
        setup.rules.push_back(std::move(rule));
    }
    return setup;
}

GameAction action(const Rule& rule, const Setup& setup, const std::filesystem::path& rom) {
    const auto& paths = setup.paths;
    const bool windows = setup.environment.windows;
    const auto core = [&](bool launcher) -> std::wstring {
        if (rule.core.empty()) return {};
        if (windows) return launcher ? L"-L " + powershellQuoted(L"cores\\" + rule.core + L".dll") : L"-L \"cores\\" + rule.core + L".dll\"";
        const auto folder = paths.ra_cores.empty() ? std::wstring(L"cores") : paths.ra_cores;
        return L"-L '" + folder + L"/" + rule.core + L".so'";
    };
    for (const auto& emulator : rule.emulators) {
        GameAction play;
        play.kind = ActionKind::play;
        // EmuDeck's launcher: its cloud sync and emulator choice stay, without SRM's cmd.exe window.
        const auto script = paths.tools.empty() ? std::filesystem::path{} : paths.tools / L"launchers" / (emulator.launcher + (windows ? L".ps1" : L".sh"));
        if (fileExists(script)) {
            std::wstring words = rule.core.empty() ? emulator.arguments : core(true);
            if (windows) {
                const auto root = setup.environment.system_root.empty() ? std::filesystem::path(L"C:\\Windows") : setup.environment.system_root;
                play.target = (root / L"System32" / L"WindowsPowerShell" / L"v1.0" / L"powershell.exe").wstring();
                play.arguments = L"-NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -Command \"& " + powershellQuoted(script.wstring()) +
                                 (words.empty() ? std::wstring{} : L" " + words) + L" " + powershellQuoted(rom.wstring()) + L"\"";
            } else {
                play.target = script.wstring();
                play.arguments = (words.empty() ? std::wstring{} : words + L" ") + L"'" + rom.wstring() + L"'";
            }
            const auto retro = retroArchFolder(paths);
            play.working_directory = !rule.core.empty() && !retro.empty() ? retro : script.parent_path();
            return play;
        }
        // The program itself.
        std::filesystem::path program;
        if (!rule.core.empty()) { if (const auto retro = retroArchFolder(paths); !retro.empty()) program = retro / L"retroarch.exe"; }
        else if (!paths.emulators.empty()) program = paths.emulators / emulator.folder / emulator.executable;
        if (!fileExists(program)) continue;
        const auto words = rule.core.empty() ? emulator.direct : core(false);
        play.target = program.wstring();
        play.arguments = (words.empty() ? std::wstring{} : words + L" ") + L"\"" + rom.wstring() + L"\"";
        play.working_directory = program.parent_path();
        return play;
    }
    return {};
}

GameAction commandAction(std::wstring_view command_text, const std::filesystem::path& rom) {
    auto command = trim(command_text);
    if (command.empty()) return {};
    std::wstring target, rest;
    if (command.front() == L'"') {
        const auto close = command.find(L'"', 1);
        target = command.substr(1, close == std::wstring::npos ? std::wstring::npos : close - 1);
        rest = close == std::wstring::npos ? std::wstring{} : trim(std::wstring_view(command).substr(close + 1));
    } else {
        // An unquoted path with spaces: the longest prefix naming an existing file.
        size_t split = command.find(L' ');
        while (split != std::wstring::npos && !fileExists(command.substr(0, split))) split = command.find(L' ', split + 1);
        if (split == std::wstring::npos && !fileExists(command)) split = command.find(L' ');
        target = command.substr(0, split);
        rest = split == std::wstring::npos ? std::wstring{} : trim(std::wstring_view(command).substr(split + 1));
    }
    const auto quoted = L"\"" + rom.wstring() + L"\"";
    bool named = false;
    for (const std::wstring_view placeholder : {std::wstring_view(L"\"%ROM%\""), std::wstring_view(L"%ROM%"), std::wstring_view(L"%rom%")})
        for (size_t at = rest.find(placeholder); at != std::wstring::npos; at = rest.find(placeholder, at + quoted.size())) {
            rest.replace(at, placeholder.size(), quoted);
            named = true;
        }
    if (!named) rest += (rest.empty() ? L"" : L" ") + quoted;
    GameAction action{target, rest, std::filesystem::path(target).parent_path(), ActionKind::play};
    return action;
}

Result scan(const Setup& setup, bool show_hidden, const Cancelled& cancelled) {
    Result result;
    if (setup.paths.roms.empty()) return result;
    const auto exceptions = loadExceptions(setup.paths);
    std::map<std::wstring, Listing> listings; // Per folder, shared by its rules (Wii U: folders and discs).
    const auto listing = [&](const std::filesystem::path& folder) -> const Listing& {
        const auto key = romKey(folder);
        auto found = listings.find(key);
        if (found == listings.end()) found = listings.emplace(key, listFolder(folder, cancelled)).first;
        return found->second;
    };
    struct Found {
        std::filesystem::path rom;
        std::wstring title, platform;
        const Rule* rule = nullptr;
    };
    std::vector<Found> found;
    std::unordered_set<std::wstring> claimed;
    std::set<std::wstring> systems;
    for (const auto& rule : setup.rules) {
        if (cancelled && cancelled()) break;
        if (rule.folder.empty() || !folderExists(rule.folder)) continue;
        for (const auto& file : listing(rule.folder).files) {
            const auto key = romKey(file);
            if (claimed.contains(key)) continue;
            const auto title = match(rule.glob, relativeText(file, rule.folder));
            if (!title || title->empty()) continue;
            const auto& platform = rule.platform;
            claimed.insert(key);
            found.push_back({file, *title, platform, &rule});
            systems.insert(platform);
            if (found.size() >= most_games) break;
        }
    }
    // Loose files at the top of the ROM folder (a Switch .nsp left beside the system folders): the
    // parser of the system its extension belongs to.
    {
        std::error_code error;
        for (std::filesystem::directory_iterator it(setup.paths.roms, error), end; !error && it != end && found.size() < most_games; it.increment(error)) {
            std::error_code kind;
            if (!it->is_regular_file(kind)) continue;
            const auto key = romKey(it->path());
            if (claimed.contains(key)) continue;
            const auto system = extensionSystem(lower(it->path().extension().wstring()));
            if (system.empty()) continue;
            const auto platform = platformForSystem(system);
            const Rule* rule = nullptr;
            // The system's own glob must take it (Switch: .nsp and .xci, not an .nsz its emulator cannot open).
            for (const auto& candidate : setup.rules) if (candidate.system == system && match(candidate.glob, it->path().filename().wstring())) { rule = &candidate; break; }
            if (!rule) continue;
            claimed.insert(key);
            found.push_back({it->path(), it->path().stem().wstring(), platform, rule});
            systems.insert(platform);
        }
    }
    // Discs listed by a playlist or a cue sheet stay behind it.
    std::unordered_set<std::wstring> listed;
    for (const auto& entry : found) {
        const auto extension = lower(entry.rom.extension().wstring());
        if (extension == L".m3u" || extension == L".cue" || extension == L".gdi") listedFiles(entry.rom, listed);
    }
    esde::Library gamelists(setup.environment.esde_root.empty() ? std::filesystem::path{} : setup.environment.esde_root / L"gamelists");
    for (const auto& entry : found) {
        if (cancelled && cancelled()) break;
        if (listed.contains(romKey(entry.rom)) && lower(entry.rom.extension().wstring()) != L".m3u") continue;
        std::wstring name;
        if (const auto exception = exceptions.find(lower(trim(entry.title))); exception != exceptions.end()) {
            if (exception->second.exclude) { ++result.excluded; continue; }
            name = exception->second.title;
        }
        if (!setup.environment.esde_root.empty())
            if (const auto esde = gamelists.find(entry.platform, entry.rom, entry.title); esde && esde->by_rom && esde->entry.hidden && !show_hidden) {
                ++result.hidden;
                continue;
            }
        if (name.empty()) name = displayTitle(entry.title);
        auto play = action(*entry.rule, setup, entry.rom);
        result.games.push_back(makeGame(name, entry.platform, entry.rom, std::move(play)));
    }
    result.systems = systems.size();
    return result;
}

bool romFile(const std::filesystem::path& file) {
    const auto extension = lower(file.extension().wstring());
    if (extension.empty()) return false;
    if (!extensionSystem(extension).empty()) return true;
    return std::find(std::begin(shared_images), std::end(shared_images), extension) != std::end(shared_images) ||
           extension == L".xiso" || extension == L".nkit" || extension == L".chd";
}

std::vector<std::wstring> romExtensions() {
    std::vector<std::wstring> result;
    for (const auto& [extension, system] : extension_systems) result.emplace_back(extension);
    for (const auto extension : shared_images) result.emplace_back(extension);
    return result;
}

std::wstring systemOf(const std::filesystem::path& file, const std::filesystem::path& roms) {
    if (!roms.empty()) {
        const auto key = romKey(file), base = romKey(roms);
        if (key.starts_with(base + L"\\")) {
            const auto relative = file.lexically_relative(roms);
            if (std::distance(relative.begin(), relative.end()) >= 2) return lower(relative.begin()->wstring());
            return {};
        }
    }
    // Any "…\roms\<system>\…" path, as ES-DE and EmuDeck lay them out.
    std::wstring previous;
    std::vector<std::wstring> parts;
    for (const auto& part : file.parent_path()) parts.push_back(lower(part.wstring()));
    for (size_t i = 0; i + 1 < parts.size(); ++i)
        if (parts[i] == L"roms") return parts[i + 1];
    return {};
}

std::wstring platformForSystem(std::wstring_view system) {
    const auto key = lower(system);
    for (const auto& platform : emulation_platforms)
        for (const auto& folder : platform.esde)
            if (!folder.empty() && folder == key) return std::wstring(platform.name);
    for (const auto& [folder, platform] : extra_systems) if (folder == key) return std::wstring(platform);
    return L"Retro";
}

std::wstring guessPlatform(const std::filesystem::path& file, const std::filesystem::path& roms) {
    if (const auto system = systemOf(file, roms); !system.empty() && !skippedSystem(system)) return platformForSystem(system);
    const auto extension = lower(file.extension().wstring());
    if (const auto system = extensionSystem(extension); !system.empty()) return platformForSystem(system);
    // A disc image: what is inside it says the console where it can.
    std::ifstream input(file, std::ios::binary);
    std::error_code ec;
    const auto size = std::filesystem::file_size(file, ec);
    if (input && (extension == L".iso" || extension == L".gcz" || extension == L".img" || extension == L".bin")) {
        std::array<unsigned char, 0x20> head{};
        input.read(reinterpret_cast<char*>(head.data()), head.size());
        if (head[0x18] == 0x5D && head[0x19] == 0x1C && head[0x1A] == 0x9E && head[0x1B] == 0xA3) return L"Wii";
        if (head[0x1C] == 0xC2 && head[0x1D] == 0x33 && head[0x1E] == 0x9F && head[0x1F] == 0x3D) return L"GameCube";
        // ISO 9660's system identifier: 2048-byte sectors (DVD images) or raw 2352-byte CD sectors.
        for (const std::streamoff at : {std::streamoff(0x8008), std::streamoff(0x9320)}) {
            input.clear(); input.seekg(at);
            std::array<char, 32> system{};
            if (!input.read(system.data(), system.size())) continue;
            const std::string_view text(system.data(), system.size());
            if (text.starts_with("PSP GAME")) return L"PSP";
            if (text.starts_with("PLAYSTATION")) return at == 0x9320 || size < 700ull * 1024 * 1024 ? L"PS1" : L"PS2";
        }
        input.clear(); input.seekg(0x10000);
        std::array<char, 20> xbox{};
        if (input.read(xbox.data(), xbox.size()) && std::string_view(xbox.data(), xbox.size()).starts_with("MICROSOFT*XBOX*MEDIA"))
            return size > 7ull * 1024 * 1024 * 1024 ? L"Xbox 360" : L"Xbox";
    }
    if (extension == L".cue" || extension == L".bin" || extension == L".ecm" || extension == L".ccd" || extension == L".mds") return L"PS1";
    if (extension == L".chd") return !ec && size > 800ull * 1024 * 1024 ? L"PS2" : L"PS1";
    if (extension == L".iso" || extension == L".mdf" || extension == L".nrg") return L"PS2";
    return L"Retro";
}

std::optional<FolderGame> folderGame(const std::filesystem::path& folder) {
    if (!folderExists(folder)) return std::nullopt;
    auto name = folder.filename().wstring();
    if (unicode_text::equalIgnoreCase(name, L"PS3_GAME")) return folderGame(folder.parent_path());
    if (unicode_text::endsWithIgnoreCase(name, L".ps3")) name.resize(name.size() - 4);
    for (const auto* eboot : {L"EBOOT.BIN", L"eboot.bin"})
        if (const auto rom = folder / L"PS3_GAME" / L"USRDIR" / eboot; fileExists(rom)) return FolderGame{rom, L"PS3", displayTitle(name)};
    std::error_code error;
    for (std::filesystem::directory_iterator it(folder / L"code", error), end; !error && it != end; it.increment(error))
        if (lower(it->path().extension().wstring()) == L".rpx") return FolderGame{it->path(), L"Wii U", displayTitle(name)};
    // A folder holding one game: its playlist or cue sheet first, else its only ROM.
    std::vector<std::filesystem::path> roms;
    for (std::filesystem::directory_iterator it(folder, error), end; !error && it != end; it.increment(error)) {
        std::error_code kind;
        if (it->is_regular_file(kind) && romFile(it->path())) roms.push_back(it->path());
        if (roms.size() > 64) break;
    }
    for (const auto* lead : {L".m3u", L".cue", L".gdi"})
        for (const auto& rom : roms)
            if (lower(rom.extension().wstring()) == lead) return FolderGame{rom, guessPlatform(rom), displayTitle(name)};
    if (roms.size() == 1) return FolderGame{roms.front(), guessPlatform(roms.front()), displayTitle(roms.front().stem().wstring())};
    return std::nullopt;
}

std::wstring displayTitle(std::wstring_view raw) {
    std::wstring text(raw);
    // WHDLoad style ("CannonFodder_v1.2_CD32"): the name before its version, its words apart.
    if (text.find(L' ') == std::wstring::npos && text.find(L'_') != std::wstring::npos) {
        for (size_t at = text.find(L'_'); at != std::wstring::npos; at = text.find(L'_', at + 1))
            if (at + 2 < text.size() && (text[at + 1] == L'v' || text[at + 1] == L'V') && std::iswdigit(text[at + 2])) { text.resize(at); break; }
        std::wstring spaced;
        for (size_t i = 0; i < text.size(); ++i) {
            const auto ch = text[i];
            if (i && std::iswupper(ch) && std::iswlower(text[i - 1])) spaced += L' ';
            spaced += ch;
        }
        text = spaced;
    }
    for (auto& ch : text) if (ch == L'_') ch = L' ';
    // Region, language and dump tags: "(Europe) (En,Fr)", "[0100606026F12000][v0]".
    for (const auto [open, close] : {std::pair{L'(', L')'}, std::pair{L'[', L']'}})
        for (size_t at = text.find(open); at != std::wstring::npos; at = text.find(open, at)) {
            const auto end = text.find(close, at);
            if (end == std::wstring::npos) break;
            text.erase(at, end - at + 1);
        }
    std::wstring collapsed;
    for (const auto ch : text) {
        if (std::iswspace(ch)) { if (!collapsed.empty() && collapsed.back() != L' ') collapsed += L' '; }
        else collapsed += ch;
    }
    text = trim(collapsed);
    while (!text.empty() && (text.back() == L'-' || text.back() == L' ' || text.back() == L',')) text.pop_back();
    // "Big Brain Academy 0527500000a70000": a trailing title ID.
    if (const auto at = text.rfind(L' '); at != std::wstring::npos && text.size() - at - 1 >= 12 &&
        std::all_of(text.begin() + static_cast<std::ptrdiff_t>(at + 1), text.end(), [](wchar_t ch) { return std::iswxdigit(ch) != 0; }))
        text.resize(at);
    // "Yoshis Crafted World v1.0.1": a trailing version.
    if (const auto at = text.rfind(L" v"); at != std::wstring::npos && at + 2 < text.size() && std::iswdigit(text[at + 2]) &&
        std::all_of(text.begin() + static_cast<std::ptrdiff_t>(at + 2), text.end(), [](wchar_t ch) { return std::iswdigit(ch) || ch == L'.'; }))
        text.resize(at);
    // No-Intro's "Legend of Zelda, The - The Wind Waker": the article back in front.
    for (const std::wstring_view article : {std::wstring_view(L"The"), std::wstring_view(L"A"), std::wstring_view(L"An")}) {
        const auto marker = L", " + std::wstring(article);
        const auto at = text.find(marker);
        if (at == std::wstring::npos) continue;
        const auto after = at + marker.size();
        if (after != text.size() && text.compare(after, 3, L" - ") != 0) continue;
        text = std::wstring(article) + L" " + text.substr(0, at) + text.substr(after);
        break;
    }
    return text.empty() ? std::wstring(raw) : text;
}

std::wstring romKey(const std::filesystem::path& rom) {
    auto text = lower(rom.lexically_normal().wstring());
    for (auto& ch : text) if (ch == L'/') ch = L'\\';
    while (text.size() > 3 && text.back() == L'\\') text.pop_back();
    return text;
}

uint32_t romAppId(const std::filesystem::path& rom) { return steam_shortcuts::appId(romKey(rom), L"launchme-rom"); }

Game makeGame(const std::wstring& name, const std::wstring& platform, const std::filesystem::path& rom, GameAction action) {
    Game game;
    game.kind = GameKind::nonsteam;
    game.source = GameSource::custom;
    game.name = name;
    game.platform = platform;
    game.rom = rom;
    game.app_id = romAppId(rom);
    game.shortcut_id = (static_cast<uint64_t>(game.app_id) << 32) | 0x02000000ULL;
    game.play_action = std::move(action);
    game.installed = true;
    // "Otwórz folder": the game's own folder for folder games (PS3, Wii U), else the ROM's.
    auto folder = rom.parent_path();
    for (auto up = rom.parent_path(); up.has_relative_path(); up = up.parent_path())
        if (unicode_text::equalIgnoreCase(up.filename().wstring(), L"PS3_GAME") || unicode_text::equalIgnoreCase(up.filename().wstring(), L"code")) {
            folder = up.parent_path();
            break;
        }
    game.install_directory = folder;
    return game;
}
} // namespace emulation_scan
