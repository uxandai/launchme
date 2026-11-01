#include "custom_games.hpp"
#include "emulation_platforms.hpp"
#include "emulation_scan.hpp"
#include "../platform/system.hpp"
#include "../net/json.hpp"
#include "../platform/atomic_file.hpp"
#include "../platform/text_encoding.hpp"

#ifdef _WIN32
#include <Windows.h>
#include <bcrypt.h>
#include <ShlObj.h>
#include <wrl/client.h>
#endif

#include <algorithm>
#include <cwctype>
#include <fstream>
#include <unordered_set>

namespace custom_games {
namespace {
constexpr size_t max_document = 2 * 1024 * 1024; // json::parse refuses anything larger.

// One line of display text: no control characters, no lone surrogates.
bool plainText(std::wstring_view text) {
    for (const wchar_t ch : text) if (ch < L' ' || ch == 127) return false;
    return text.empty() || !text_encoding::utf8(text).empty();
}

bool validPath(const std::filesystem::path& path, bool file) {
    const auto& text = path.wstring();
    if (text.empty() || text.size() > max_path || !plainText(text) ||
        text.find_first_of(L"\"<>|*?") != std::wstring::npos || !path.is_absolute()) return false;
    return !file || path.has_filename();
}

std::string field(const json::Value& value, const char* key) {
    const auto* item = value.get(key);
    return item && item->type == json::Value::Type::string ? item->text : std::string{};
}

void appendString(std::string& output, std::wstring_view text) {
    constexpr char hex[] = "0123456789abcdef";
    output += '"';
    for (const unsigned char ch : text_encoding::utf8(text)) {
        if (ch == '"' || ch == '\\') { output += '\\'; output += static_cast<char>(ch); }
        else if (ch < 0x20) { output += "\\u00"; output += hex[ch >> 4]; output += hex[ch & 15]; }
        else output += static_cast<char>(ch);
    }
    output += '"';
}
} // namespace

std::wstring trim(std::wstring_view text) {
    const auto first = text.find_first_not_of(L" \t\u00a0");
    if (first == std::wstring_view::npos) return {};
    const auto last = text.find_last_not_of(L" \t\u00a0");
    return std::wstring(text.substr(first, last - first + 1));
}

std::wstring cleanPath(std::wstring_view text) {
    auto result = trim(text);
    if (result.size() >= 2 && result.front() == L'"' && result.back() == L'"') result = trim(std::wstring_view(result).substr(1, result.size() - 2));
    return result;
}

bool validId(std::string_view id) {
    return id.size() == 16 && std::all_of(id.begin(), id.end(), [](char ch) { return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f'); });
}

Problem validate(const Entry& entry) {
    if (entry.name.size() > max_name || trim(entry.name).empty() || !plainText(entry.name)) return Problem::name;
    // No file yet is fine (a game to play next, or one played elsewhere): listed, not playable.
    if (!entry.path.empty() && !validPath(entry.path, true)) return Problem::path;
    if (entry.arguments.size() > max_arguments || !plainText(entry.arguments)) return Problem::arguments;
    if (!entry.directory.empty() && !validPath(entry.directory, false)) return Problem::directory;
    if (!entry.platform.empty() && (!findEmulationPlatform(entry.platform) || entry.platform == L"PC")) return Problem::path;
    return Problem::none;
}

List parse(std::string_view bytes) {
    List result;
    if (bytes.starts_with("\xef\xbb\xbf")) bytes.remove_prefix(3);
    const auto root = bytes.size() <= max_document ? json::parse(bytes) : std::nullopt;
    const auto* version = root ? root->get("version") : nullptr;
    const auto* games = root ? root->get("games") : nullptr;
    if (!version || version->type != json::Value::Type::number || version->numeric != 1 ||
        !games || games->type != json::Value::Type::array) {
        result.readable = false;
        return result;
    }
    std::unordered_set<std::string> ids;
    for (const auto& item : games->elements) {
        Entry entry;
        entry.id = field(item, "id");
        entry.name = trim(text_encoding::wide(field(item, "name")));
        entry.path = cleanPath(text_encoding::wide(field(item, "path")));
        entry.arguments = text_encoding::wide(field(item, "arguments"));
        entry.directory = cleanPath(text_encoding::wide(field(item, "directory")));
        entry.platform = text_encoding::wide(field(item, "platform"));
        if (item.type != json::Value::Type::object || !validId(entry.id) || validate(entry) != Problem::none ||
            result.games.size() >= max_games || !ids.insert(entry.id).second) { ++result.skipped; continue; }
        result.games.push_back(std::move(entry));
    }
    return result;
}

std::string format(const std::vector<Entry>& games) {
    std::string output = "{\n  \"version\": 1,\n  \"games\": [";
    for (size_t i = 0; i < games.size(); ++i) {
        const auto& entry = games[i];
        output += i ? ",\n    {" : "\n    {";
        output += "\"id\": "; appendString(output, text_encoding::wide(entry.id));
        output += ", \"name\": "; appendString(output, entry.name);
        output += ", \"path\": "; appendString(output, entry.path.wstring());
        output += ", \"arguments\": "; appendString(output, entry.arguments);
        output += ", \"directory\": "; appendString(output, entry.directory.wstring());
        if (!entry.platform.empty()) { output += ", \"platform\": "; appendString(output, entry.platform); }
        output += '}';
    }
    output += games.empty() ? "]\n}\n" : "\n  ]\n}\n";
    return output;
}

std::string newId(const std::vector<Entry>& existing) {
    constexpr char hex[] = "0123456789abcdef";
    for (int attempt = 0; attempt < 8; ++attempt) {
        unsigned char random[8]{};
        if (!system_info::randomBytes(random, sizeof(random))) return {};
        std::string id;
        for (const unsigned char byte : random) { id += hex[byte >> 4]; id += hex[byte & 15]; }
        if (std::none_of(existing.begin(), existing.end(), [&](const Entry& entry) { return entry.id == id; })) return id;
    }
    return {};
}

std::filesystem::path workingDirectory(const Entry& entry) {
    return entry.directory.empty() ? entry.path.parent_path() : entry.directory;
}

GameAction action(const Entry& entry) {
    return {entry.path.wstring(), entry.arguments, workingDirectory(entry), ActionKind::play};
}

namespace {
// A Windows shortcut's target program (.lnk → .exe), so a game added as a shortcut is seen
// running like one added as its .exe. Empty when it is not resolvable without searching.
std::filesystem::path shortcutTarget(const std::filesystem::path& link) {
#ifndef _WIN32
    // .lnk files are Explorer's; elsewhere they are not resolved (the game is listed and
    // launches through its handler, but is not tracked as a running program).
    (void)link;
    return {};
#else
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    std::filesystem::path result;
    {
        Microsoft::WRL::ComPtr<IShellLinkW> shell;
        Microsoft::WRL::ComPtr<IPersistFile> file;
        wchar_t target[MAX_PATH * 2]{};
        if (SUCCEEDED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(shell.GetAddressOf()))) &&
            SUCCEEDED(shell.As(&file)) && SUCCEEDED(file->Load(link.c_str(), STGM_READ)) &&
            SUCCEEDED(shell->GetPath(target, static_cast<int>(std::size(target)), nullptr, SLGP_RAWPATH)) && target[0]) {
            wchar_t expanded[MAX_PATH * 2]{};
            const DWORD length = ExpandEnvironmentStringsW(target, expanded, static_cast<DWORD>(std::size(expanded)));
            result = length && length <= std::size(expanded) ? std::filesystem::path(expanded) : std::filesystem::path(target);
        }
    }
    if (SUCCEEDED(com)) CoUninitialize();
    return result.is_absolute() ? result : std::filesystem::path{};
#endif
}
}

Game makeGame(const Entry& entry, bool installed) {
    if (!entry.platform.empty()) {
        // Emulated: on its console's shelf, keyed by its ROM; the launcher fills in how it starts
        // (Launcher::emulationAction: its own command, else the platform's parser).
        auto game = emulation_scan::makeGame(entry.name, entry.platform, entry.path, {});
        if (entry.platform == L"Switch 2" && !entry.path.empty()) {
            // An unpacked dump (its folder or an exefs\main picked): started and keyed as the ROM
            // folder's scan does it, so an update or a later scan keeps its identification.
            const auto folder = emulation_scan::switch2Folder(entry.path);
            if (auto main = emulation_scan::switch2Main(folder); !main.empty()) {
                game = emulation_scan::makeGame(entry.name, entry.platform, main, {});
                game.app_id = emulation_scan::romAppId(folder);
                game.shortcut_id = (static_cast<uint64_t>(game.app_id) << 32) | 0x02000000ULL;
                game.install_directory = folder;
            }
        }
        game.source_id = entry.id;
        game.installed = installed && !entry.path.empty();
        return game;
    }
    Game game;
    game.kind = GameKind::store;
    game.source = GameSource::custom;
    game.source_id = entry.id;
    game.name = entry.name;
    game.installed = installed && !entry.path.empty();
    if (!entry.path.empty()) game.play_action = action(entry);
    // "Otwórz folder" uses the working directory; process tracking for custom
    // games uses only executable, never ownership of this possibly shared folder.
    game.install_directory = workingDirectory(entry);
    auto extension = entry.path.extension().wstring();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
    if (extension == L".exe") game.executable = entry.path;
    else if (extension == L".lnk" && game.installed) {
        auto target = shortcutTarget(entry.path);
        auto target_extension = target.extension().wstring();
        std::transform(target_extension.begin(), target_extension.end(), target_extension.begin(), [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
        if (target_extension == L".exe") game.executable = std::move(target);
    }
    return game;
}

List load(const std::filesystem::path& file) {
    std::error_code error;
    if (!std::filesystem::exists(file, error)) return {};
    std::ifstream input(file, std::ios::binary | std::ios::ate);
    const auto size = input ? static_cast<long long>(input.tellg()) : -1;
    std::string bytes;
    if (size > 0 && static_cast<size_t>(size) <= max_document) {
        bytes.resize(static_cast<size_t>(size));
        input.seekg(0);
        if (!input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()))) bytes.clear();
    }
    input.close();
    auto result = parse(bytes);
    if (!result.readable) {
        auto backup = file;
        backup += L".bak";
        std::filesystem::copy_file(file, backup, std::filesystem::copy_options::overwrite_existing, error);
    }
    return result;
}

bool save(const std::vector<Entry>& games, const std::filesystem::path& file) {
    if (games.size() > max_games) return false;
    const auto text = format(games);
    // A list the parser would refuse must never replace a readable one.
    return text.size() <= max_document && atomic_file::write(file, text);
}
} // namespace custom_games
