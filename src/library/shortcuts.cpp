#include "shortcuts.hpp"
#include "visibility.hpp"
#include "emulation_platforms.hpp"
#include "../platform/unicode_text.hpp"
#include "../platform/text_encoding.hpp"

#ifdef _WIN32
#include <Windows.h>
#endif

#include <algorithm>
#include <cctype>
#include <cwctype>
#include <fstream>
#include <optional>
#include <string>
#include <unordered_map>

namespace {
constexpr uint64_t steam_id_offset = 76561197960265728ULL;

std::wstring wide(std::string_view text) { return text_encoding::wideLegacy(text); }

bool platformMarker(std::wstring_view source, std::wstring_view marker) {
    if (marker.empty()) return false;
    if (marker != L"wii" && marker != L"switch") return source.find(marker) != std::wstring_view::npos;
    // Console names are complete words/folders, never prefixes of the next generation.
    for (size_t at = source.find(marker); at != std::wstring_view::npos; at = source.find(marker, at + marker.size())) {
        const auto end = at + marker.size();
        if ((at && std::iswalnum(source[at - 1])) || (end < source.size() && std::iswalnum(source[end]))) continue;
        const auto next = source.find_first_not_of(L" \t", end);
        if (next != std::wstring_view::npos && source[next] == (marker == L"wii" ? L'u' : L'2') &&
            (next + 1 == source.size() || !std::iswalnum(source[next + 1]))) continue;
        return true;
    }
    return false;
}

class BinaryVdf {
public:
    explicit BinaryVdf(std::vector<uint8_t> bytes) : bytes_(std::move(bytes)) {}

    std::vector<ShortcutGame> shortcuts(size_t* parse_error) {
        uint8_t type = 0;
        std::string root;
        if (!byte(type) || type != 0 || !string(root) || root != "shortcuts") {
            if (parse_error) *parse_error = position_;
            return {};
        }
        while (position_ < bytes_.size() && byte(type) && type != 8) {
            std::string index;
            if (!string(index)) { if (parse_error) *parse_error = position_; return {}; }
            if (type != 0) { if (!skip(type, 1)) { if (parse_error) *parse_error = position_; return {}; } continue; }
            ShortcutGame shortcut;
            std::wstring exe, options, tags;
            if (!entry(shortcut, exe, options, tags, 1)) { if (parse_error) *parse_error = position_; return {}; }
            shortcut.game.platform = shortcutPlatform(exe, options, shortcut.game.name, tags);
            shortcut.game.rom = shortcutRom(exe, options);
            if (exe.size() >= 2 && exe.front() == L'"' && exe.back() == L'"') exe = exe.substr(1, exe.size() - 2);
            // Launchme's own shortcut of one of its games ("Dodaj do Steam"): the game is here already.
            if (options.find(L"--jump=play:") != std::wstring::npos) continue;
            const std::filesystem::path executable(exe);
            if (executable.is_absolute() && unicode_text::equalIgnoreCase(executable.extension().wstring(), L".exe")) shortcut.game.executable = executable;
            if (shortcut.game.app_id && !shortcut.game.name.empty()) {
                shortcut.game.kind = GameKind::nonsteam;
                shortcut.game.shortcut_id = (uint64_t(shortcut.game.app_id) << 32) | 0x02000000ULL;
                result_.push_back(std::move(shortcut));
            }
        }
        return result_;
    }

private:
    bool byte(uint8_t& value) {
        if (position_ >= bytes_.size()) return false;
        value = bytes_[position_++];
        return true;
    }
    bool string(std::string& value) {
        const size_t start = position_;
        const auto finish = std::find(bytes_.begin() + start, bytes_.end(), uint8_t{0});
        if (finish == bytes_.end() || static_cast<size_t>(finish - bytes_.begin()) - start > 64 * 1024) return false;
        value.assign(reinterpret_cast<const char*>(bytes_.data() + start),
                     static_cast<size_t>(finish - bytes_.begin()) - start);
        position_ = static_cast<size_t>(finish - bytes_.begin()) + 1;
        return true;
    }
    bool integer(uint32_t& value) {
        if (bytes_.size() - position_ < 4) return false;
        value = 0;
        for (unsigned i = 0; i < 4; ++i) value |= uint32_t(bytes_[position_++]) << (i * 8);
        return true;
    }
    bool skip(uint8_t type, int depth) {
        if (depth > 16) return false;
        if (type == 0) {
            uint8_t child = 0;
            while (byte(child) && child != 8) {
                std::string key;
                if (!string(key) || !skip(child, depth + 1)) return false;
            }
            return child == 8;
        }
        if (type == 1) { std::string value; return string(value); }
        const size_t size = type == 7 ? 8 : type == 5 ? 0 : 4;
        if (type == 5) { // UTF-16 wide string, null terminated.
            while (bytes_.size() - position_ >= 2) {
                const auto low = bytes_[position_], high = bytes_[position_ + 1];
                position_ += 2;
                if (!low && !high) return true;
            }
            return false;
        }
        if (type < 2 || type > 7 || bytes_.size() - position_ < size) return false;
        position_ += size;
        return true;
    }
    bool entry(ShortcutGame& shortcut, std::wstring& exe, std::wstring& options,
               std::wstring& tags, int depth) {
        uint8_t type = 0;
        while (byte(type) && type != 8) {
            std::string key;
            if (!string(key)) return false;
            for (auto& ch : key) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            if (type == 1) {
                std::string value;
                if (!string(value)) return false;
                if (key == "appname") shortcut.game.name = wide(value);
                else if (key == "exe") exe = wide(value);
                else if (key == "launchoptions") options = wide(value);
            } else if (type == 2) {
                uint32_t value = 0;
                if (!integer(value)) return false;
                if (key == "appid") shortcut.game.app_id = value;
                else if (key == "ishidden") shortcut.hidden = value != 0;
                else if (key == "lastplaytime") shortcut.game.last_played = value;
            } else if (type == 0 && key == "tags") {
                uint8_t child = 0;
                while (byte(child) && child != 8) {
                    std::string tag_index;
                    if (!string(tag_index)) return false;
                    if (child == 1) {
                        std::string tag;
                        if (!string(tag)) return false;
                        tags += L' ' + wide(tag);
                    } else if (!skip(child, depth + 1)) return false;
                }
                if (child != 8) return false;
            } else if (!skip(type, depth + 1)) return false;
        }
        return type == 8;
    }
    std::vector<uint8_t> bytes_;
    size_t position_ = 0;
    std::vector<ShortcutGame> result_;
};
} // namespace

std::wstring shortcutPlatform(const std::wstring& executable, const std::wstring& options,
                              const std::wstring& title, const std::wstring& tags) {
    std::wstring source = executable + L' ' + options + L' ' + tags + L' ' + title;
    for (auto& ch : source) ch = ch == L'/' ? L'\\' : static_cast<wchar_t>(std::towlower(ch));
    for (const auto& platform : emulation_platforms)
        for (const auto marker : platform.markers)
            if (platformMarker(source, marker)) return std::wstring(platform.name);
    return std::wstring(emulation_platforms.back().name);
}

std::filesystem::path shortcutRom(const std::wstring& executable, const std::wstring& options) {
    // Emulator and launcher files never are the game itself.
    constexpr std::wstring_view tools[] = {L".exe", L".dll", L".ps1", L".bat", L".cmd", L".lnk", L".vbs",
                                           L".ini", L".cfg", L".json", L".xml", L".yml", L".txt", L".log"};
    std::wstring found;
    const auto scan = [&](const auto& self, std::wstring_view command, int depth) -> void {
        for (size_t at = 0; at < command.size();) {
            if (std::iswspace(command[at])) { ++at; continue; }
            std::wstring token;
            bool quoted = false;
            if (command[at] == L'"' || command[at] == L'\'') { // "K:\roms\ps2\A B.chd" or PowerShell 'K:\...'
                const auto quote = command[at];
                const auto end = command.find(quote, at + 1);
                token = command.substr(at + 1, end == std::wstring_view::npos ? std::wstring_view::npos : end - at - 1);
                at = end == std::wstring_view::npos ? command.size() : end + 1;
                quoted = true;
            } else {
                size_t end = at;
                while (end < command.size() && !std::iswspace(command[end])) ++end;
                token = command.substr(at, end - at);
                at = end;
            }
            while (!token.empty() && (token.front() == L'{' || token.front() == L'&')) token.erase(0, 1);
            while (!token.empty() && (token.back() == L'}' || token.back() == L';')) token.pop_back();
            const bool drive = token.size() > 3 && std::iswalpha(token[0]) && token[1] == L':' && (token[2] == L'\\' || token[2] == L'/');
            const bool unc = token.size() > 3 && token[0] == L'\\' && token[1] == L'\\';
            if (!drive && !unc) {
                // PowerShell wrappers quote a whole script line: -Command "& {launcher.ps1 'rom'}".
                if (quoted && depth < 2 && token.find_first_of(L" \t") != std::wstring::npos) self(self, token, depth + 1);
                continue;
            }
            const auto extension = std::filesystem::path(token).extension().wstring();
            if (extension.size() < 2) continue;
            if (std::any_of(std::begin(tools), std::end(tools), [&](std::wstring_view tool) {
                    return unicode_text::equalIgnoreCase(extension, tool); })) continue;
            found = std::move(token); // The last file argument: emulator options come first.
        }
    };
    scan(scan, executable + L' ' + options, 0);
    if (found.empty()) return {};
    for (auto& ch : found) if (ch == L'/') ch = L'\\';
    return std::filesystem::path(found);
}

std::wstring collectionPlatform(std::wstring_view collection_name) {
    std::wstring name(collection_name);
    for (auto& ch : name) ch = static_cast<wchar_t>(std::towlower(ch));
    // "System - Emulator" and "System (Emulator)": the system part names the platform.
    for (const auto* separator : {L" - ", L" ("})
        if (const auto cut = name.find(separator); cut != std::wstring::npos) name.resize(cut);
    const auto first = name.find_first_not_of(L" \t"), last = name.find_last_not_of(L" \t");
    if (first == std::wstring::npos) return {};
    name = name.substr(first, last - first + 1);
    for (const auto& platform : emulation_platforms)
        for (const auto known : platform.collections)
            if (!known.empty() && name == known) return std::wstring(platform.name);
    // Other wording ("Switch games"): the executable markers, never the PC fallback.
    for (const auto& platform : emulation_platforms)
        for (const auto marker : platform.markers)
            if (platformMarker(name, marker)) return std::wstring(platform.name);
    return {};
}

void applyCollectionPlatforms(std::vector<ShortcutGame>& shortcuts, const std::vector<SteamCollection>& collections) {
    std::unordered_map<uint32_t, std::wstring> assigned;
    for (const auto& collection : collections) {
        if (!srmCollection(collection)) continue;
        const auto platform = collectionPlatform(collection.name);
        if (platform.empty()) continue;
        for (const auto id : collection.app_ids) {
            if (!steamShortcutId(id)) continue;
            const auto [found, inserted] = assigned.emplace(id, platform);
            // A shared Dolphin collection must not hide a later, specific console collection.
            if (!inserted && found->second == L"Wii / GameCube") found->second = platform;
        }
    }
    for (auto& shortcut : shortcuts) {
        const auto found = assigned.find(shortcut.game.app_id);
        if (found == assigned.end()) continue;
        if (found->second == L"Wii / GameCube" &&
            (shortcut.game.platform == L"Wii" || shortcut.game.platform == L"GameCube")) continue;
        shortcut.game.platform = found->second;
    }
}

std::vector<ShortcutGame> scanSteamShortcuts(const std::filesystem::path& steam_root, size_t* parse_error) {
    const uint64_t id = activeSteamId(steam_root);
    if (id <= steam_id_offset) return {};
    const auto file = steam_root / "userdata" / std::to_wstring(id - steam_id_offset) /
                      "config" / "shortcuts.vdf";
    std::ifstream input(file, std::ios::binary | std::ios::ate);
    if (!input || input.tellg() <= 0 || input.tellg() > 16 * 1024 * 1024) return {};
    const size_t size = static_cast<size_t>(input.tellg());
    input.seekg(0);
    std::vector<uint8_t> bytes(size);
    if (!input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size))) return {};
    return BinaryVdf(std::move(bytes)).shortcuts(parse_error);
}

// ---- Writing shortcuts.vdf ----------------------------------------------------------------------

namespace {
// Binary KeyValues as a tree that writes back byte for byte: maps (0), strings (1), 32-bit (2, 3,
// 4, 6) and 64-bit values (7, 10), UTF-16 strings (5) kept as their bytes.
struct KvNode {
    uint8_t type = 0;
    std::string key;
    std::string text;
    std::vector<uint8_t> raw;
    std::vector<KvNode> children;
};

class KvReader {
public:
    explicit KvReader(const std::vector<uint8_t>& bytes) : bytes_(bytes) {}
    // A map's entries up to its end marker (the file itself is one: its "shortcuts" map, then 8).
    bool map(std::vector<KvNode>& out, int depth) {
        if (depth > 32) return false;
        while (position_ < bytes_.size()) {
            KvNode node;
            node.type = bytes_[position_++];
            if (node.type == 8) return true;
            if (!cstring(node.key)) return false;
            switch (node.type) {
            case 0: if (!map(node.children, depth + 1)) return false; break;
            case 1: if (!cstring(node.text)) return false; break;
            case 2: case 3: case 4: case 6: if (!take(node.raw, 4)) return false; break;
            case 7: case 10: if (!take(node.raw, 8)) return false; break;
            case 5:
                while (true) {
                    if (bytes_.size() - position_ < 2) return false;
                    const uint8_t low = bytes_[position_], high = bytes_[position_ + 1];
                    node.raw.push_back(low); node.raw.push_back(high);
                    position_ += 2;
                    if (!low && !high) break;
                }
                break;
            default: return false;
            }
            out.push_back(std::move(node));
        }
        return false;
    }
    bool finished() const { return position_ == bytes_.size(); }

private:
    bool cstring(std::string& value) {
        const auto end = std::find(bytes_.begin() + static_cast<std::ptrdiff_t>(position_), bytes_.end(), uint8_t{0});
        if (end == bytes_.end()) return false;
        value.assign(reinterpret_cast<const char*>(bytes_.data() + position_), static_cast<size_t>(end - bytes_.begin()) - position_);
        position_ = static_cast<size_t>(end - bytes_.begin()) + 1;
        return true;
    }
    bool take(std::vector<uint8_t>& out, size_t count) {
        if (bytes_.size() - position_ < count) return false;
        out.assign(bytes_.begin() + static_cast<std::ptrdiff_t>(position_), bytes_.begin() + static_cast<std::ptrdiff_t>(position_ + count));
        position_ += count;
        return true;
    }
    const std::vector<uint8_t>& bytes_;
    size_t position_ = 0;
};

void writeMap(std::string& out, const std::vector<KvNode>& nodes) {
    for (const auto& node : nodes) {
        out += static_cast<char>(node.type);
        out += node.key;
        out += '\0';
        if (node.type == 0) writeMap(out, node.children);
        else if (node.type == 1) { out += node.text; out += '\0'; }
        else out.append(reinterpret_cast<const char*>(node.raw.data()), node.raw.size());
    }
    out += '\x08';
}

KvNode textNode(const char* key, const std::wstring& value) {
    KvNode node;
    node.type = 1; node.key = key; node.text = text_encoding::utf8Lenient(value);
    return node;
}
KvNode numberNode(const char* key, uint32_t value) {
    KvNode node;
    node.type = 2; node.key = key;
    for (int i = 0; i < 4; ++i) node.raw.push_back(static_cast<uint8_t>(value >> (8 * i)));
    return node;
}
bool sameKey(const std::string& key, std::string_view wanted) {
    return key.size() == wanted.size() && std::equal(key.begin(), key.end(), wanted.begin(), [](char a, char b) {
        return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
    });
}
const KvNode* child(const KvNode& node, std::string_view key) {
    for (const auto& item : node.children) if (sameKey(item.key, key)) return &item;
    return nullptr;
}
uint32_t number(const KvNode* node) {
    if (!node || node->raw.size() < 4) return 0;
    return uint32_t(node->raw[0]) | uint32_t(node->raw[1]) << 8 | uint32_t(node->raw[2]) << 16 | uint32_t(node->raw[3]) << 24;
}

// The file's tree; nullopt when it exists but is not shortcuts (never overwritten then).
std::optional<std::vector<KvNode>> readShortcuts(const std::filesystem::path& file) {
    std::error_code ec;
    if (!std::filesystem::exists(file, ec)) {
        KvNode root;
        root.type = 0; root.key = "shortcuts";
        return std::vector<KvNode>{root};
    }
    std::ifstream input(file, std::ios::binary | std::ios::ate);
    if (!input || input.tellg() < 0 || input.tellg() > 16 * 1024 * 1024) return std::nullopt;
    std::vector<uint8_t> bytes(static_cast<size_t>(input.tellg()));
    input.seekg(0);
    if (!bytes.empty() && !input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()))) return std::nullopt;
    std::vector<KvNode> root;
    KvReader reader(bytes);
    if (!reader.map(root, 0) || !reader.finished() || root.size() != 1 || root.front().type != 0 || !sameKey(root.front().key, "shortcuts"))
        return std::nullopt;
    return root;
}

bool writeShortcuts(const std::filesystem::path& file, const std::vector<KvNode>& root) {
    std::string bytes;
    writeMap(bytes, root);
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    // The user's own file is kept once beside it, as it was before Launchme first changed it.
    auto backup = file;
    backup += L".launchme-backup";
    if (std::filesystem::exists(file, ec) && !std::filesystem::exists(backup, ec)) std::filesystem::copy_file(file, backup, ec);
    const auto temporary = std::filesystem::path(file.wstring() + L".launchme-new");
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output.write(bytes.data(), static_cast<std::streamsize>(bytes.size())) || !output.flush()) { std::filesystem::remove(temporary, ec); return false; }
    }
    std::filesystem::rename(temporary, file, ec);
    if (ec) { std::filesystem::remove(temporary, ec); return false; }
    return true;
}

// The options themselves, or ending with them after a prefix such as "VAR=value %command%".
bool launchOptionsMatch(const KvNode& entry, std::wstring_view launch_options) {
    const auto* options = child(entry, "LaunchOptions");
    if (!options || options->type != 1 || launch_options.empty()) return false;
    const auto text = text_encoding::wideLenient(options->text);
    return text == launch_options || (text.size() > launch_options.size() && text.ends_with(launch_options) &&
                                      text[text.size() - launch_options.size() - 1] == L' ');
}
} // namespace

namespace steam_shortcuts {
uint32_t appId(const std::wstring& exe, const std::wstring& name) {
    const auto text = text_encoding::utf8Lenient(L"\"" + exe + L"\"" + name);
    uint32_t crc = 0xFFFFFFFFu;
    for (const unsigned char byte : text) {
        crc ^= byte;
        for (int bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc | 0x80000000u;
}

std::filesystem::path configFolder(const std::filesystem::path& steam_root) {
    const uint64_t id = activeSteamId(steam_root);
    if (id <= steam_id_offset) return {};
    return steam_root / "userdata" / std::to_wstring(id - steam_id_offset) / "config";
}

uint32_t find(const std::filesystem::path& shortcuts_file, std::wstring_view launch_options) {
    const auto root = readShortcuts(shortcuts_file);
    if (!root) return 0;
    for (const auto& entry : root->front().children)
        if (entry.type == 0 && launchOptionsMatch(entry, launch_options)) return number(child(entry, "appid"));
    return 0;
}

uint32_t add(const std::filesystem::path& shortcuts_file, const Shortcut& shortcut) {
    auto root = readShortcuts(shortcuts_file);
    if (!root || shortcut.name.empty() || shortcut.exe.empty()) return 0;
    auto& entries = root->front().children;
    std::erase_if(entries, [&](const KvNode& entry) { return entry.type == 0 && launchOptionsMatch(entry, shortcut.launch_options); });
    const uint32_t id = appId(shortcut.exe, shortcut.name);
    KvNode entry;
    entry.type = 0;
    // The fields and order Steam itself writes.
    entry.children = {numberNode("appid", id), textNode("AppName", shortcut.name), textNode("Exe", L"\"" + shortcut.exe + L"\""),
                      textNode("StartDir", L"\"" + shortcut.start_dir + L"\""), textNode("icon", shortcut.icon), textNode("ShortcutPath", {}),
                      textNode("LaunchOptions", shortcut.launch_options), numberNode("IsHidden", 0), numberNode("AllowDesktopConfig", 1),
                      numberNode("AllowOverlay", 1), numberNode("OpenVR", 0), numberNode("Devkit", 0), textNode("DevkitGameID", {}),
                      numberNode("DevkitOverrideAppID", 0), numberNode("LastPlayTime", 0), textNode("FlatpakAppID", {})};
    KvNode tags;
    tags.type = 0; tags.key = "tags";
    entry.children.push_back(std::move(tags));
    entries.push_back(std::move(entry));
    for (size_t i = 0; i < entries.size(); ++i) entries[i].key = std::to_string(i); // Steam numbers them 0, 1, 2…
    return writeShortcuts(shortcuts_file, *root) ? id : 0;
}

bool remove(const std::filesystem::path& shortcuts_file, std::wstring_view launch_options) {
    auto root = readShortcuts(shortcuts_file);
    if (!root) return false;
    auto& entries = root->front().children;
    const auto before = entries.size();
    std::erase_if(entries, [&](const KvNode& entry) { return entry.type == 0 && launchOptionsMatch(entry, launch_options); });
    if (entries.size() == before) return true;
    for (size_t i = 0; i < entries.size(); ++i) entries[i].key = std::to_string(i);
    return writeShortcuts(shortcuts_file, *root);
}
}
