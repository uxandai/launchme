#include "esde_gamelist.hpp"
#include "../platform/file_io.hpp"
#include "../platform/system.hpp"
#include "../platform/unicode_text.hpp"
#include "emulation_platforms.hpp"
#include "../platform/text_encoding.hpp"

#ifdef _WIN32
#include <Windows.h>
#endif

#include <algorithm>
#include <charconv>
#include <cwctype>
#include <fstream>

namespace esde {
namespace {
std::string fileText(const std::filesystem::path& path, size_t limit) { return file_io::readText(path, limit); }

void appendUtf8(std::string& out, uint32_t code) {
    if (!code || code > 0x10FFFF || (code >= 0xD800 && code <= 0xDFFF)) return;
    if (code < 0x80) out += static_cast<char>(code);
    else if (code < 0x800) { out += static_cast<char>(0xC0 | (code >> 6)); out += static_cast<char>(0x80 | (code & 0x3F)); }
    else if (code < 0x10000) {
        out += static_cast<char>(0xE0 | (code >> 12)); out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (code & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (code >> 18)); out += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((code >> 6) & 0x3F)); out += static_cast<char>(0x80 | (code & 0x3F));
    }
}

// XML text: the five named entities and numeric references (ES-DE writes &#x0A; style newlines too).
std::wstring decoded(std::string_view text, size_t limit) {
    std::string out;
    out.reserve(text.size());
    for (size_t at = 0; at < text.size();) {
        if (text[at] != '&') { out += text[at++]; continue; }
        const auto end = text.find(';', at);
        if (end == std::string_view::npos || end - at > 10) { out += text[at++]; continue; }
        const auto name = text.substr(at + 1, end - at - 1);
        if (name == "amp") out += '&';
        else if (name == "lt") out += '<';
        else if (name == "gt") out += '>';
        else if (name == "quot") out += '"';
        else if (name == "apos") out += '\'';
        else if (name.size() > 1 && name[0] == '#') {
            const bool hex = name[1] == 'x' || name[1] == 'X';
            const auto digits = name.substr(hex ? 2 : 1);
            uint32_t code = 0;
            const auto [ptr, ec] = std::from_chars(digits.data(), digits.data() + digits.size(), code, hex ? 16 : 10);
            if (ec == std::errc{} && ptr == digits.data() + digits.size()) appendUtf8(out, code);
        } else { out += text[at++]; continue; }
        at = end + 1;
    }
    auto result = text_encoding::wideLenient(out);
    // Trim surrounding whitespace; keep paragraph breaks inside descriptions.
    const auto first = result.find_first_not_of(L" \t\r\n");
    if (first == std::wstring::npos) return {};
    result = result.substr(first, result.find_last_not_of(L" \t\r\n") - first + 1);
    if (result.size() > limit) result.resize(limit);
    return result;
}

std::string_view element(std::string_view game, std::string_view tag) {
    const std::string open = "<" + std::string(tag) + ">";
    const std::string close = "</" + std::string(tag) + ">";
    const auto start = game.find(open);
    if (start == std::string_view::npos) return {};
    const auto finish = game.find(close, start + open.size());
    return finish == std::string_view::npos ? std::string_view{} :
           game.substr(start + open.size(), finish - start - open.size());
}

// ES-DE: 19990131T000000. Partial dates keep what is known.
std::wstring releaseDate(std::wstring_view value) {
    const auto digits = [&](size_t from, size_t to) {
        return std::all_of(value.begin() + from, value.begin() + to, [](wchar_t ch) { return ch >= L'0' && ch <= L'9'; });
    };
    // 1970: ES-DE's "no date" written as an epoch timestamp.
    if (value.size() < 4 || !digits(0, 4) || value.starts_with(L"1970")) return {};
    std::wstring result(value.substr(0, 4));
    if (value.size() >= 8 && digits(4, 8) && value.substr(4, 4) != L"0101")
        result += L'-' + std::wstring(value.substr(4, 2)) + L'-' + std::wstring(value.substr(6, 2));
    return result;
}

std::wstring normalized(std::wstring_view path) {
    std::wstring result(path);
    for (auto& ch : result) ch = ch == L'/' ? L'\\' : static_cast<wchar_t>(std::towlower(ch));
    while (result.size() > 1 && result.back() == L'\\') result.pop_back();
    return result;
}

std::wstring relative(std::wstring_view path) {
    auto result = normalized(path);
    while (result.starts_with(L".\\")) result.erase(0, 2);
    return result;
}

std::wstring environment(const wchar_t* name) {
    return system_info::environment(name, 4095).value_or(std::wstring{});
}
} // namespace

std::wstring Entry::stem() const {
    return std::filesystem::path(path).stem().wstring();
}

std::wstring cleanedTitle(std::wstring_view title) {
    std::wstring result;
    for (wchar_t ch : title) {
        if (ch == L'(' || ch == L'[') break; // Region/language/revision suffix of the ROM filename.
        if (std::iswalnum(ch) || ch >= 128) result += static_cast<wchar_t>(std::towlower(ch));
    }
    return result;
}

Gamelist parseGamelist(std::string_view xml) {
    Gamelist result;
    size_t at = 0;
    while ((at = xml.find("<game", at)) != std::string_view::npos) {
        // <game> or ES's <game id="..." source="ScreenScraper.fr">, never <gameList>.
        const auto after = at + 5;
        if (after >= xml.size() || (xml[after] != '>' && xml[after] != ' ')) { at = after; continue; }
        const auto end = xml.find("</game>", after);
        if (end == std::string_view::npos) break;
        const auto game = xml.substr(at, end - at);
        at = end + 7;
        Entry entry;
        entry.path = decoded(element(game, "path"), 1024);
        if (entry.path.empty()) continue;
        entry.name = decoded(element(game, "name"), 200);
        entry.details.description = decoded(element(game, "desc"), 1800);
        entry.details.release_date = releaseDate(decoded(element(game, "releasedate"), 32));
        entry.details.developer = decoded(element(game, "developer"), 200);
        entry.details.publisher = decoded(element(game, "publisher"), 200);
        entry.details.genres = decoded(element(game, "genre"), 200);
        entry.players = decoded(element(game, "players"), 16);
        const auto rating = std::string(element(game, "rating"));
        double score = -1;
        if (!rating.empty()) {
            const auto [ptr, ec] = std::from_chars(rating.data(), rating.data() + rating.size(), score);
            if (ec != std::errc{} || score < 0 || score > 1) score = -1;
        }
        entry.rating = score;
        entry.hidden = element(game, "hidden") == "true";
        entry.details.players = entry.players;
        if (score > 0) entry.details.score = std::to_wstring(static_cast<int>(score * 20 + 0.5)) + L"/20";
        result.push_back(std::move(entry));
        if (result.size() >= 100000) break;
    }
    return result;
}

Gamelist loadGamelist(const std::filesystem::path& file) {
    return parseGamelist(fileText(file, 64 * 1024 * 1024));
}

const Entry* match(const Gamelist& entries, const std::filesystem::path& rom, const std::wstring& title) {
    const Entry* best = nullptr;
    size_t best_length = 0;
    if (!rom.empty()) {
        const auto file = normalized(rom.wstring());
        for (const auto& entry : entries) {
            const auto wanted = relative(entry.path);
            if (wanted.empty() || wanted.size() <= best_length) continue;
            const bool absolute = wanted.size() > 2 && wanted[1] == L':';
            const bool same = file == wanted ||
                (!absolute && file.size() > wanted.size() && file.ends_with(wanted) && file[file.size() - wanted.size() - 1] == L'\\') ||
                // A folder game (PS3 "Game.ps3", Switch/Wii U folders) started through a file inside it.
                (absolute ? file.starts_with(wanted + L'\\') : file.find(L'\\' + wanted + L'\\') != std::wstring::npos);
            if (same) { best = &entry; best_length = wanted.size(); }
        }
        if (best) return best;
    }
    const auto wanted = cleanedTitle(title);
    if (wanted.empty()) return nullptr;
    for (const auto& entry : entries)
        if (cleanedTitle(entry.name) == wanted || cleanedTitle(entry.stem()) == wanted) return &entry;
    return nullptr;
}

std::filesystem::path root() {
    const auto appdata = environment(L"APPDATA");
    if (!appdata.empty()) {
        const auto emudeck = std::filesystem::path(appdata) / L"EmuDeck" / L"EmulationStation-DE" / L"ES-DE";
        std::error_code ec;
        if (std::filesystem::exists(emudeck / L"settings" / L"es_settings.xml", ec)) return emudeck;
    }
    const auto profile = environment(L"USERPROFILE");
    return profile.empty() ? std::filesystem::path{} : std::filesystem::path(profile) / L"ES-DE";
}

std::filesystem::path mediaDirectory(const std::filesystem::path& root) {
    const auto xml = fileText(root / L"settings" / L"es_settings.xml", 256 * 1024);
    const auto key = xml.find("name=\"MediaDirectory\"");
    if (key != std::string::npos) {
        const auto start = xml.find("value=\"", key);
        if (start != std::string::npos) {
            const auto begin = start + 7, end = xml.find('"', begin);
            if (end != std::string::npos && end > begin && end - begin < 4096) {
                const auto path = decoded(std::string_view(xml).substr(begin, end - begin), 4096);
                if (!path.empty()) return std::filesystem::path(path);
            }
        }
    }
    return root / L"downloaded_media";
}

std::vector<std::wstring> systems(const std::wstring& platform, const std::filesystem::path& rom) {
    std::vector<std::wstring> result;
    const auto add = [&](std::wstring system) {
        for (auto& ch : system) ch = static_cast<wchar_t>(std::towlower(ch));
        // A folder name, never a path: it is joined below gamelists/ and downloaded_media/.
        if (system.empty() || system.size() > 64 || system == L"." || system == L".." ||
            system.find_first_of(L"\\/:*?\"<>|") != std::wstring::npos) return;
        if (std::find(result.begin(), result.end(), system) == result.end()) result.push_back(std::move(system));
    };
    if (const auto* found = findEmulationPlatform(platform))
        for (const auto system : found->esde) if (!system.empty()) add(std::wstring(system));
    // ES-DE's layout is <ROMDirectory>\<system>\[subfolders\]<rom>: the folder below "roms",
    // else the ROM's own folder.
    std::vector<std::wstring> parts;
    for (const auto& part : rom.parent_path().relative_path()) parts.push_back(part.wstring());
    for (size_t i = 0; i + 1 < parts.size(); ++i)
        if (unicode_text::equalIgnoreCase(parts[i], L"roms")) { add(parts[i + 1]); break; }
    if (!parts.empty()) add(parts.back());
    return result;
}

std::optional<Match> Library::find(const std::wstring& platform, const std::filesystem::path& rom, const std::wstring& title) {
    const auto candidates = systems(platform, rom);
    // A ROM match in any candidate system beats a title match in the platform's own system.
    std::optional<Match> by_title;
    for (const auto& system : candidates) {
        auto [list, inserted] = lists_.try_emplace(system);
        if (inserted) list->second = loadGamelist(root_ / system / L"gamelist.xml");
        if (const auto* entry = match(list->second, rom, {})) return Match{system, *entry, true};
        if (!by_title) if (const auto* entry = match(list->second, {}, title)) by_title = Match{system, *entry, false};
    }
    return by_title;
}
}
