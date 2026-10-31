#include "user_config.hpp"
#include "atomic_file.hpp"
#include "sound_cues.hpp"
#include "music_themes.hpp"
#include "text_encoding.hpp"
#include "system.hpp"
#include "unicode_text.hpp"

#ifdef _WIN32
#include <Windows.h>
#ifdef _WIN32
#include <ShlObj.h>
#endif
#endif

#include <fstream>
#include <algorithm>
#include <charconv>
#include <cmath>
#include <iterator>
#include <locale>
#include <optional>
#include <sstream>
#include <string_view>
#include <vector>
#include <chrono>
#include <thread>

std::filesystem::path userConfigDirectory() {
    if (const auto override_path = system_info::environment(L"LAUNCHME_CONFIG_DIR", 4095);
        override_path && std::filesystem::path(*override_path).is_absolute()) return *override_path;
#ifndef _WIN32
    // XDG: $XDG_DATA_HOME/launchme, else ~/.local/share/launchme (settings and data stay together, as in LocalAppData).
    if (const auto data = system_info::environment(L"XDG_DATA_HOME"); data && std::filesystem::path(*data).is_absolute())
        return std::filesystem::path(*data) / L"launchme";
    if (const auto home = system_info::environment(L"HOME"); home && std::filesystem::path(*home).is_absolute())
        return std::filesystem::path(*home) / L".local" / L"share" / L"launchme";
#else
    PWSTR folder = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &folder))) {
        std::filesystem::path result = std::filesystem::path(folder) / L"Launchme";
        CoTaskMemFree(folder);
        return result;
    }
#endif
    return std::filesystem::temp_directory_path() / L"Launchme";
}

std::wstring userSecret(const wchar_t* environment, const wchar_t* filename) {
    if (auto value = system_info::environment(environment, 1023); value && !value->empty()) return *value;
    std::ifstream input(userConfigDirectory() / filename, std::ios::binary);
    std::string bytes;
    std::getline(input, bytes);
    if (bytes.empty() || bytes.size() > 1024) return {};
    if (bytes.size() >= 3 && bytes.substr(0, 3) == "\xef\xbb\xbf") bytes.erase(0, 3);
    while (!bytes.empty() && (bytes.back() == '\r' || bytes.back() == ' ')) bytes.pop_back();
    return text_encoding::wide(bytes);
}

bool saveUserSecret(const wchar_t* filename, std::wstring_view value) {
    const auto first = value.find_first_not_of(L" \t\r\n");
    const auto file = userConfigDirectory() / filename;
    if (first == std::wstring_view::npos) { // Empty input removes the key.
        std::error_code ec;
        std::filesystem::remove(file, ec);
        return !ec;
    }
    value = value.substr(first, value.find_last_not_of(L" \t\r\n") - first + 1);
    if (value.size() > 1024 || value.find_first_of(L"\r\n") != std::wstring_view::npos) return false;
    const auto bytes = text_encoding::utf8(std::wstring(value));
    return !bytes.empty() && atomic_file::write(file, bytes + '\n');
}

bool storedUserSecret(const wchar_t* filename) {
    std::error_code ec;
    return std::filesystem::file_size(userConfigDirectory() / filename, ec) > 1 && !ec;
}

DrawerStyle parseDrawerStyle(std::string_view text) {
    DrawerStyle style;
    while (!text.empty()) {
        const size_t end = text.find('\n');
        auto line = text.substr(0, end);
        text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1);
        const size_t split = line.find('=');
        if (split == std::string_view::npos) continue;
        auto key = line.substr(0, split);
        auto value = line.substr(split + 1);
        const auto trim = [](std::string_view& part) {
            while (!part.empty() && (part.front() == ' ' || part.front() == '\t')) part.remove_prefix(1);
            while (!part.empty() && (part.back() == ' ' || part.back() == '\r' || part.back() == '\t')) part.remove_suffix(1);
        };
        trim(key); trim(value);
        float number = 0;
        const auto parsed = std::from_chars(value.data(), value.data() + value.size(), number);
        if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() || !std::isfinite(number)) continue;
        if (key == "drawer_opacity") style.opacity = std::clamp(number, 0.0f, 0.98f);
        else if (key == "drawer_fade_fraction") style.fade_fraction = std::clamp(number, 0.06f, 0.75f);
    }
    return style;
}

std::wstring companionUserName(std::wstring_view text) {
    std::wstring out;
    for (const wchar_t ch : text) {
        if (ch < L' ' || ch == 0x7F || ch == L'\u2039' || ch == L'\u203A' || ch == L'|') continue;
        if (ch == L' ' && (out.empty() || out.back() == L' ')) continue;
        out += ch;
    }
    while (!out.empty() && out.back() == L' ') out.pop_back();
    unicode_text::truncate(out, 40);
    while (!out.empty() && out.back() == L' ') out.pop_back();
    return out;
}

namespace {
// Interface look and layout options added after the main list (button colours, tile entrance,
// focus gap, search artwork, Flix icons, the default home's facts). True: the key was one of them.
bool parseLookSetting(AppSettings& settings, std::string_view key, std::string_view value) {
    const auto flag = [&](bool& target) { if (value == "0" || value == "1") target = value == "1"; return true; };
    if (key == "flix_top_icons") return flag(settings.flix_top_icons);
    if (key == "hide_while_playing") return flag(settings.hide_while_playing);
    if (key == "shelves_hero_facts") return flag(settings.shelves_hero_facts);
    if (key == "focus_gap") {
        // 0 / 1 were the first version's off / outside.
        if (value == "0" || value == "none") settings.focus_gap = AppSettings::FocusGap::none;
        if (value == "1" || value == "outside") settings.focus_gap = AppSettings::FocusGap::outside;
        if (value == "inside") settings.focus_gap = AppSettings::FocusGap::inside;
        return true;
    }
    if (key == "search_art") {
        if (value == "square") settings.search_art = AppSettings::SearchArt::square;
        if (value == "poster") settings.search_art = AppSettings::SearchArt::poster;
        if (value == "landscape") settings.search_art = AppSettings::SearchArt::landscape;
        return true;
    }
    if (key == "tile_entrance") {
        if (value == "grow") settings.tile_entrance = AppSettings::TileEntrance::grow;
        if (value == "rise") settings.tile_entrance = AppSettings::TileEntrance::rise;
        if (value == "none") settings.tile_entrance = AppSettings::TileEntrance::none;
        return true;
    }
    if (key == "button_idle") {
        if (value == "gray") settings.button_idle = AppSettings::ButtonIdle::gray;
        if (value == "tinted") settings.button_idle = AppSettings::ButtonIdle::tinted;
        if (value == "dark") settings.button_idle = AppSettings::ButtonIdle::dark;
        return true;
    }
    if (key == "button_color") {
        // "accent": the interface accent; otherwise #RRGGBB (pure black is kept as #010101, 0 means accent).
        if (value == "accent") settings.button_rgb = 0;
        else if (value.size() == 7 && value.front() == '#') {
            uint32_t rgb = 0;
            const auto result = std::from_chars(value.data() + 1, value.data() + 7, rgb, 16);
            if (result.ec == std::errc{} && result.ptr == value.data() + 7) settings.button_rgb = rgb ? rgb : 0x010101;
        }
        return true;
    }
    return false;
}
// 0.4.9: artwork sources, the wallpaper, home tiles, PSfive's details, the menu dialog, Metro's light theme.
bool parseExtraSetting(AppSettings& settings, std::string_view key, std::string_view value) {
    const auto flag = [&](bool& target) { if (value == "0" || value == "1") target = value == "1"; return true; };
    if (key == "artwork_sources_off") {
        settings.artwork_sources_off.clear();
        while (!value.empty()) {
            const auto comma = value.find(',');
            const auto item = value.substr(0, comma);
            if (!item.empty() && item.size() <= 32) settings.artwork_sources_off.insert(std::string(item));
            value = comma == std::string_view::npos ? std::string_view{} : value.substr(comma + 1);
        }
        return true;
    }
    if (key == "wallpaper") {
        if (value == "none") settings.wallpaper = AppSettings::WallpaperKind::none;
        if (value == "picture") settings.wallpaper = AppSettings::WallpaperKind::picture;
        if (value == "video") settings.wallpaper = AppSettings::WallpaperKind::video;
        return true;
    }
    if (key == "wallpaper_file") { settings.wallpaper_file = std::filesystem::path(text_encoding::wideLenient(value)); return true; }
    if (key == "wallpaper_id") {
        uint32_t id = 0;
        const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), id);
        if (error == std::errc{} && end == value.data() + value.size()) settings.wallpaper_id = id;
        return true;
    }
    if (key == "wallpaper_snitch") return flag(settings.wallpaper_snitch);
    if (key == "wallpaper_menus") return flag(settings.wallpaper_menus);
    if (key == "wallpaper_artwork") return flag(settings.wallpaper_artwork);
    if (key == "wallpaper_tiles") return flag(settings.wallpaper_tiles);
    if (key == "home_tiles") {
        if (value == "layout") settings.home_tiles = AppSettings::HomeTiles::layout;
        if (value == "first") settings.home_tiles = AppSettings::HomeTiles::first;
        if (value == "last") settings.home_tiles = AppSettings::HomeTiles::last;
        return true;
    }
    if (key == "add_game_tile") return flag(settings.add_game_tile);
    if (key == "psfive_details") return flag(settings.psfive_details);
    if (key == "menu_dialog") return flag(settings.menu_dialog);
    if (key == "metro_light") return flag(settings.metro_light);
    return false;
}
}

AppSettings parseAppSettings(std::string_view text) {
    if (text.starts_with("\xef\xbb\xbf")) text.remove_prefix(3);
    AppSettings settings;
    settings.drawer = parseDrawerStyle(text);
    bool music_track = false;
    // A companion's key: her folder under res/companion/ (lowercase letters, digits, _ or -).
    const auto companionKey = [](std::string_view value) {
        return !value.empty() && value.size() <= 32 && std::all_of(value.begin(), value.end(), [](char ch) {
            return (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '_' || ch == '-'; });
    };
    while (!text.empty()) {
        const auto end = text.find('\n');
        auto line = text.substr(0, end);
        text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1);
        const auto equal = line.find('=');
        if (equal == std::string_view::npos) continue;
        const auto trim = [](std::string_view value) {
            const auto begin = value.find_first_not_of(" \t\r");
            if (begin == std::string_view::npos) return std::string_view{};
            return value.substr(begin, value.find_last_not_of(" \t\r") - begin + 1);
        };
        const auto key = trim(line.substr(0, equal));
        auto value = trim(line.substr(equal + 1));
        if (parseLookSetting(settings, key, value)) continue; // Kept apart: MSVC caps one if/else chain's depth.
        if (parseExtraSetting(settings, key, value)) continue;
        if (key == "xbox_own_install") { if (value == "0" || value == "1") settings.xbox_own_install = value == "1"; continue; }
        if (key == "texture_compression") { if (value == "0" || value == "1") settings.texture_compression = value == "1"; continue; }
        // Before 0.4.6 (opt-in under the low memory mode): on stays on; off takes the new default.
        if (key == "low_memory_bc1") { if (value == "1") settings.texture_compression = true; continue; }
        if (key == "aurelia") { if (value == "0" || value == "1") settings.aurelia = value == "1"; continue; }
        if (key == "aurelia_intro_done") { if (value == "0" || value == "1") settings.aurelia_intro_done = value == "1"; continue; }
        if (key == "steam_account") { // <app id>:<SteamID64>
            const auto colon = value.find(':');
            const auto app = value.substr(0, colon == std::string_view::npos ? 0 : colon), id = colon == std::string_view::npos ? std::string_view{} : value.substr(colon + 1);
            uint64_t steam_id = 0;
            const auto [parsed, error] = std::from_chars(id.data(), id.data() + id.size(), steam_id);
            if (!app.empty() && app.size() <= 10 && std::all_of(app.begin(), app.end(), [](char c) { return c >= '0' && c <= '9'; }) &&
                error == std::errc{} && parsed == id.data() + id.size() && steam_id) settings.steam_launch_accounts[std::string(app)] = steam_id;
            continue;
        }
        if (key == "steam_enabled") { if (value == "0" || value == "1") settings.steam_enabled = value == "1"; continue; }
        if (key == "aurelia_default") { if (value == "0" || value == "1") settings.aurelia_default = value == "1"; continue; }
        if (key == "aurelia_cloud") { if (value == "0" || value == "1") settings.aurelia_cloud = value == "1"; continue; }
        if (key == "aurelia_auto_cloud") continue; // 0.4.1 and earlier: an opt-in; syncing is on by default now.
        if (key == "aurelia_standalone" || key == "aurelia_cloud_skip" || key == "aurelia_steam") {
            if (!value.empty() && value.size() <= 10 && std::all_of(value.begin(), value.end(), [](char c) { return c >= '0' && c <= '9'; }))
                (key == "aurelia_standalone" ? settings.aurelia_standalone : key == "aurelia_steam" ? settings.aurelia_steam : settings.aurelia_cloud_skip).insert(std::string(value));
            continue;
        }
        // Outside the else-if chain below: MSVC refuses one level more of it (C1061).
        if (key == "recent_uninstalled") { if (value == "0" || value == "1") settings.recent_uninstalled = value == "1"; continue; }
        if (key == "save_backup_after_game") { if (value == "0" || value == "1") settings.save_backup_after_game = value == "1"; continue; }
        if (key == "default_view_console" || key == "default_view_collection" || key == "default_view_studio") {
            // library_view::View's saved numbers, 0..11 (library_view::persisted_view_count).
            if (!value.empty() && value.size() <= 2 && std::all_of(value.begin(), value.end(), [](char c) { return c >= '0' && c <= '9'; })) {
                const int view = value.size() == 2 ? (value[0] - '0') * 10 + (value[1] - '0') : value[0] - '0';
                if (view < 12) (key == "default_view_console" ? settings.default_view_console :
                                key == "default_view_collection" ? settings.default_view_collection : settings.default_view_studio) = view;
            }
            continue;
        }
        if (key == "show_uninstalled" && (value == "0" || value == "1")) settings.show_uninstalled = value == "1";
        else if (key == "windowed" && (value == "0" || value == "1")) settings.windowed = value == "1";
        else if (key == "launch_tv_mode" && (value == "0" || value == "1")) settings.launch_tv_mode = value == "1";
        else if (key == "pc_sources_detected" && (value == "0" || value == "1")) settings.pc_sources_detected = value == "1";
        else if (key == "autostart" && (value == "0" || value == "1")) settings.autostart = value == "1";
        else if (key == "low_memory" && (value == "0" || value == "1")) settings.low_memory = value == "1";
        else if (key == "tv_monitor" && value.size() < 64 && value.starts_with("\\\\.\\")) settings.tv_monitor = text_encoding::wideLenient(value);
        else if (key == "desktop_platform_badges" && (value == "0" || value == "1")) settings.desktop_platform_badges = value == "1";
        else if (key == "desktop_gray_uninstalled" && (value == "0" || value == "1")) settings.desktop_gray_uninstalled = value == "1";
        else if (key == "desktop_maximized" && (value == "0" || value == "1")) settings.desktop_maximized = value == "1";
        else if (key == "desktop_installed" && (value == "0" || value == "1")) settings.desktop_installed = value == "1";
        else if (key == "desktop_narrow" && (value == "0" || value == "1")) settings.desktop_narrow = value == "1";
        else if (key == "desktop_pane") {
            int width = 0;
            const auto parsed = std::from_chars(value.data(), value.data() + value.size(), width);
            if (parsed.ec == std::errc{} && parsed.ptr == value.data() + value.size()) settings.desktop_pane = width <= 0 ? 0 : std::clamp(width, 300, 4000);
        }
        else if (key == "desktop_window") {
            // x,y,width,height in physical pixels; anything else keeps the centred default.
            std::array<int, 4> parts{};
            size_t index = 0;
            bool valid = true;
            auto rest = value;
            while (valid && index < parts.size()) {
                const auto comma = rest.find(',');
                const auto item = trim(rest.substr(0, comma));
                const auto parsed = std::from_chars(item.data(), item.data() + item.size(), parts[index]);
                valid = parsed.ec == std::errc{} && parsed.ptr == item.data() + item.size() && !item.empty();
                ++index;
                if (comma == std::string_view::npos) break;
                rest = rest.substr(comma + 1);
            }
            if (valid && index == parts.size() && parts[2] >= 320 && parts[3] >= 240 && parts[2] <= 32768 && parts[3] <= 32768 &&
                parts[0] >= -65536 && parts[0] <= 65536 && parts[1] >= -65536 && parts[1] <= 65536) settings.desktop_window = parts;
        }
        else if (key == "desktop_poster") {
            int width = 0;
            const auto parsed = std::from_chars(value.data(), value.data() + value.size(), width);
            if (parsed.ec == std::errc{} && parsed.ptr == value.data() + value.size()) settings.desktop_poster = std::clamp(width, 112, 240);
        }
        else if (key == "desktop_sort" && value.size() == 1 && value[0] >= '1' && value[0] <= '4') settings.desktop_sort = value[0] - '0';
        else if ((key == "desktop_section" || key == "desktop_game") && value.size() < 400 &&
                 value.find_first_of("\r\n") == std::string_view::npos) (key == "desktop_section" ? settings.desktop_section : settings.desktop_game) = std::string(value);
        else if (key == "desktop_collapsed" || key == "desktop_expanded") {
            while (!value.empty()) {
                const auto comma = value.find(',');
                const auto group = trim(value.substr(0, comma));
                if (group == "library" || group == "platforms" || group == "emulation" || group == "collections" || group == "publishers" || group == "developers")
                    (key == "desktop_collapsed" ? settings.desktop_collapsed : settings.desktop_expanded).emplace(group);
                value = comma == std::string_view::npos ? std::string_view{} : value.substr(comma + 1);
            }
        }
        else if (key == "show_emulation" && (value == "0" || value == "1")) settings.show_emulation = value == "1";
        else if (key == "studio_libraries" && (value == "0" || value == "1")) settings.studio_libraries = value == "1";
        else if (key == "show_private" && (value == "0" || value == "1")) settings.show_private = value == "1";
        else if (key == "show_store" && (value == "0" || value == "1")) settings.show_store = value == "1";
        else if (key == "show_categories" && (value == "0" || value == "1")) settings.show_categories = value == "1";
        else if (key == "xmb_posters" && (value == "0" || value == "1")) settings.xmb_posters = value == "1";
        else if (key == "shelves_posters" && (value == "0" || value == "1")) settings.shelves_posters = value == "1";
        else if (key == "shelves_category_all" && (value == "0" || value == "1")) settings.shelves_category_all = value == "1";
        else if (key == "panel_borders" && (value == "0" || value == "1")) settings.panel_borders = value == "1";
        else if (key == "screen_transitions" && (value == "0" || value == "1")) settings.screen_transitions = value == "1";
        else if (key == "input_hints" && (value == "0" || value == "1")) settings.input_hints = value == "1";
        else if (key == "glance" && (value == "0" || value == "1")) settings.glance = value == "1";
        else if (key == "tarot" && (value == "0" || value == "1")) settings.tarot = value == "1";
        else if (key == "companion" && (value == "0" || value == "1")) settings.companion = value == "1";
        // The first version's two looks were white and red: Yuki and Akane (res/companion/yuki, akane).
        else if (key == "companion_look" && companionKey(value))
            settings.companion_look = value == "white" ? std::string("yuki") : value == "red" ? std::string("akane") : std::string(value);
        else if (key == "companion_guests" && (value == "0" || value == "1")) settings.companion_guests = value == "1";
        else if (key == "companion_gamification" && (value == "0" || value == "1")) settings.companion_gamification = value == "1";
        else if (key == "companion_user_name") settings.companion_user_name = companionUserName(text_encoding::wideLenient(value));
        else if (key == "companion_user_gender")
            settings.companion_user_gender = value == "male" ? AppSettings::CompanionUserGender::male :
                                             value == "female" ? AppSettings::CompanionUserGender::female : AppSettings::CompanionUserGender::automatic;
        else if (key.starts_with("companion_costume_") && companionKey(key.substr(18))) {
            const bool valid = value.size() <= 64 && value.starts_with("costume_") &&
                std::all_of(value.begin(), value.end(), [](char ch) { return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '_' || ch == '-'; });
            if (valid) settings.companion_costumes[std::string(key.substr(18))] = std::string(value);
        }
        else if (key == "companion_debug" && (value == "0" || value == "1")) settings.companion_debug = value == "1";
        else if (key == "companion_model" || key == "companion_xai_model") {
            // A model ID: letters, digits, dots and dashes (a full RID is accepted too).
            if (!value.empty() && value.size() < 120 && std::all_of(value.begin(), value.end(), [](char ch) {
                    return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '-' || ch == '.' || ch == '_'; }))
                (key == "companion_model" ? settings.companion_model : settings.companion_xai_model) = std::string(value);
        }
        else if (key == "companion_host") {
            const auto decoded = text_encoding::wide(value);
            if (decoded.starts_with(L"https://") && decoded.size() < 300 && decoded.find_first_of(L" \t\"") == std::wstring::npos) settings.companion_host = decoded;
        }
        else if (key == "companion_provider") {
            const auto& keys = AppSettings::companion_provider_keys;
            const auto found = std::find(keys.begin(), keys.end(), value);
            if (found != keys.end()) settings.companion_provider = static_cast<AppSettings::CompanionProvider>(found - keys.begin());
        }
        else if (key == "companion_claude_model" || key == "companion_ollama_model") {
            // A model name: letters, digits and . - _ : / (Ollama's "llama3.1:8b", "library/qwen3").
            if (value.size() < 160 && std::all_of(value.begin(), value.end(), [](char ch) {
                    return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '-' || ch == '.' || ch == '_' || ch == ':' || ch == '/'; }))
                (key == "companion_claude_model" ? settings.companion_claude_model : settings.companion_ollama_model) = std::string(value);
            if (key == "companion_claude_model" && settings.companion_claude_model.empty()) settings.companion_claude_model = "auto-opus";
        }
        else if (key == "companion_ollama_host") {
            const auto decoded = text_encoding::wide(value);
            if ((decoded.empty() || decoded.starts_with(L"https://") || decoded.starts_with(L"http://")) && decoded.size() < 300 &&
                decoded.find_first_of(L" \t\"") == std::wstring::npos) settings.companion_ollama_host = decoded;
        }
        else if (key == "companion_model_info" && (value == "0" || value == "1")) settings.companion_model_info = value == "1";
        else if (key == "companion_typing" && (value == "0" || value == "1")) settings.companion_typing = value == "1";
        else if (key == "companion_weather" && (value == "0" || value == "1")) settings.companion_weather = value == "1";
        else if (key == "companion_remote" && (value == "0" || value == "1")) settings.companion_remote = value == "1";
        else if (key == "companion_remote_port") {
            int port = 0;
            if (std::from_chars(value.data(), value.data() + value.size(), port).ec == std::errc{} && port >= 1024 && port <= 65535) settings.companion_remote_port = port;
        }
        else if (key == "companion_remote_key" && value.size() >= 8 && value.size() <= 64 &&
                 std::all_of(value.begin(), value.end(), [](char ch) { return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9'); }))
            settings.companion_remote_key = std::string(value);
        else if (key == "companion_reply_length") {
            if (const auto length = companion::replyLengthFromKey(value)) settings.companion_reply_length = *length;
        }
        else if (key == "companion_photos") // ComfyUI's ("comfyui") is gone: off.
            settings.companion_photos = value == "folder" ? AppSettings::CompanionPhotos::folder : AppSettings::CompanionPhotos::off;
        else if (key == "companion_import_folder") {
            const auto decoded = text_encoding::wide(value);
            if (decoded.size() < 1024) settings.companion_import_folder = decoded;
        }
        else if (key == "companion_photo_folder" || key == "companion_feed_folder") {
            const auto decoded = text_encoding::wide(value);
            if (decoded.size() < 1024) (key == "companion_feed_folder" ? settings.companion_feed_folder : settings.companion_photo_folder) = decoded;
        }
        else if (key == "companion_voice") {
            if (value == "off") settings.companion_voice = AppSettings::CompanionVoice::off;
            else if (value == "chat") settings.companion_voice = AppSettings::CompanionVoice::chat;
            else if (value == "all") settings.companion_voice = AppSettings::CompanionVoice::everywhere;
        }
        // VOICEVOX and the local TTS + RVC server are gone: their companion_voice_engine, _style and _path are skipped.
        else if (key == "companion_voice_model") {
            if (value.size() <= 64 && std::all_of(value.begin(), value.end(), [](char ch) {
                    return (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '_' || ch == '.' || ch == '-'; }))
                settings.companion_voice_model = std::string(value);
        }
        else if (key.starts_with("companion_voice_id_") && companionKey(key.substr(19))) {
            if (!value.empty() && value.size() <= 64 && std::all_of(value.begin(), value.end(), [](char ch) {
                    return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9'); }))
                settings.companion_voice_ids[std::string(key.substr(19))] = std::string(value);
        }
        else if (key == "companion_city" || (key.starts_with("companion_city_") && companionKey(key.substr(15)))) {
            const bool valid = value.size() <= 32 && std::all_of(value.begin(), value.end(), [](char ch) { return (ch >= 'a' && ch <= 'z') || ch == '-'; });
            if (valid && key == "companion_city") settings.companion_city = std::string(value);
            else if (valid && !value.empty()) settings.companion_cities[std::string(key.substr(15))] = std::string(value);
        }
        else if (key == "companion_memory" && (value == "0" || value == "1")) settings.companion_memory = value == "1";
        else if (key == "companion_notes" && (value == "0" || value == "1")) settings.companion_notes = value == "1";
        else if (key == "companion_semantic_memory" && (value == "0" || value == "1")) settings.companion_semantic_memory = value == "1";
        else if (key == "hint_glyphs") {
            if (value == "auto") settings.hint_glyphs = AppSettings::HintGlyphs::automatic;
            else if (value == "xbox") settings.hint_glyphs = AppSettings::HintGlyphs::xbox;
            else if (value == "playstation") settings.hint_glyphs = AppSettings::HintGlyphs::playstation;
            else if (value == "nintendo") settings.hint_glyphs = AppSettings::HintGlyphs::nintendo;
        }
        else if (key == "glass") {
            float level = 0;
            const auto result = std::from_chars(value.data(), value.data() + value.size(), level);
            if (result.ec == std::errc{} && result.ptr == value.data() + value.size() && std::isfinite(level))
                settings.glass = std::clamp(level, 0.0f, 1.0f);
        }
        else if (key == "steam_silent_launch" && (value == "0" || value == "1")) settings.steam_silent_launch = value == "1";
        else if (key == "steam_account_switch" && (value == "0" || value == "1")) settings.steam_account_switch = value == "1";
        else if (key == "steam_install_dialog" && (value == "0" || value == "1")) settings.steam_install_dialog = value == "1";
        else if (key == "game_videos" && (value == "0" || value == "1")) settings.game_videos = value == "1";
        else if (key == "video_delay" && (value == "1" || value == "3" || value == "5" || value == "10")) settings.video_delay = value == "1" ? 1 : value == "3" ? 3 : value == "5" ? 5 : 10;
        else if (key == "video_volume" && value.size() == 1 && value[0] >= '0' && value[0] <= '3') settings.video_volume = value[0] - '0';
        else if (key == "steam_trailers" && (value == "0" || value == "1")) settings.steam_trailers = value == "1";
        else if (key == "trailer_space_mb" && (value == "100" || value == "250" || value == "500" || value == "1000")) settings.trailer_space_mb = std::stoi(std::string(value));
        else if (key == "emulation_names" && (value == "0" || value == "1")) settings.emulation_names = value == "1";
        else if (key == "emulation_rom_art" && (value == "0" || value == "1")) settings.emulation_rom_art = value == "1";
        else if (key == "console_colors" && (value == "0" || value == "1")) settings.console_colors = value == "1";
        else if (key == "metro_3d" && (value == "0" || value == "1")) settings.metro_3d = value == "1";
        else if (key == "psfive_clock" && (value == "0" || value == "1")) settings.psfive_clock = value == "1";
        else if (key == "flix_category_all" && (value == "0" || value == "1")) settings.flix_category_all = value == "1";
        // game_action: the earlier key (accept_action) saved its old default for everyone, so the new
        // default (details) applies once and choices made since are kept under this key.
        else if (key == "game_action" && (value == "launch" || value == "details"))
            settings.accept_action = value == "details" ? AppSettings::AcceptAction::details : AppSettings::AcceptAction::launch;
        else if (key == "quick_resume" && (value == "0" || value == "1")) settings.quick_resume = value == "1";
        else if (key == "guide_menu" && (value == "0" || value == "1")) settings.guide_menu = value == "1";
        else if (key == "guide_overlay" && (value == "0" || value == "1")) settings.guide_overlay = value == "1";
        else if (key == "game_fps" && (value == "0" || value == "1")) settings.game_fps = value == "1";
        else if (key == "game_fps_low" && (value == "0" || value == "1")) settings.game_fps_low = value == "1";
        else if (key == "steam_controller" && (value == "0" || value == "1")) settings.steam_controller = value == "1";
        else if (key == "focus_spring" && (value == "0" || value == "1")) settings.focus_spring = value == "1";
        else if (key == "glow_pulse" && (value == "0" || value == "1")) settings.glow_pulse = value == "1";
        else if (key == "glow_trace" && (value == "0" || value == "1")) settings.glow_trace = value == "1";
        else if (key == "glow_level") {
            float level = 0;
            const auto result = std::from_chars(value.data(), value.data() + value.size(), level);
            if (result.ec == std::errc{} && result.ptr == value.data() + value.size() && std::isfinite(level))
                settings.glow_level = std::clamp(level, 0.0f, 2.0f);
        }
        else if (key == "background_grid") {
            float level = 0;
            const auto result = std::from_chars(value.data(), value.data() + value.size(), level);
            if (result.ec == std::errc{} && result.ptr == value.data() + value.size() && std::isfinite(level))
                settings.background_grid = std::clamp(level, 0.0f, 1.0f);
        }
        else if (key == "screensaver" && (value == "0" || value == "1")) settings.screensaver = value == "1";
        else if (key == "drmfree_background" && (value == "0" || value == "1")) settings.drm_background = value == "1";
        else if (key == "tour_done" && (value == "0" || value == "1")) settings.tour_done = value == "1";
        else if (key == "update_check" && (value == "0" || value == "1")) settings.update_check = value == "1";
        else if (key == "update_skipped" && value.size() <= 32 && value.find_first_not_of("0123456789.") == std::string_view::npos)
            settings.update_skipped = text_encoding::wideLenient(value);
        else if (key == "screensaver_style") {
            if (value == "backgrounds") settings.screensaver_style = AppSettings::ScreensaverStyle::backgrounds;
            else if (value == "boxes") settings.screensaver_style = AppSettings::ScreensaverStyle::boxes;
            else if (value == "discs") settings.screensaver_style = AppSettings::ScreensaverStyle::discs;
        } else if (key == "screensaver_minutes") {
            if (value == "1" || value == "3" || value == "5" || value == "10") settings.screensaver_minutes = std::stoi(std::string(value));
        }
        else if (key == "video_folder") {
            const auto decoded = text_encoding::wide(value);
            if (std::filesystem::path(decoded).is_absolute()) settings.video_folder = decoded;
        }
        else if (key == "sound_folder" || key == "file_browser_folder" || key == "music_file" || key == "boot_file" || key == "multimc_path") {
            // UTF-8; a folder must be absolute, a file a plain name (no folders, no line breaks).
            const auto decoded = text_encoding::wide(value);
            if (key == "sound_folder") { if (std::filesystem::path(decoded).is_absolute()) settings.sound_folder = decoded; }
            else if (key == "multimc_path") { if (std::filesystem::path(decoded).is_absolute()) settings.multimc_path = decoded; }
            else if (key == "file_browser_folder") { if (std::filesystem::path(decoded).is_absolute()) settings.file_browser_folder = decoded; }
            else if (!decoded.empty() && decoded.find_first_of(L"\\/:\r\n") == std::wstring::npos) (key == "music_file" ? settings.music_file : settings.boot_file) = decoded;
        }
        else if (key == "music_track") {
            if (const int theme = musicThemeIndex(value); theme >= 0) { settings.music_theme = theme; music_track = true; }
        }
        else if (key == "music_theme") { // The old numbered list; music_track wins wherever it stands.
            try {
                size_t used = 0; const int theme = std::stoi(std::string(value), &used);
                if (used == value.size() && theme >= 0 && theme <= 11 && !music_track) settings.music_theme = legacyMusicTheme(theme);
            } catch (...) {}
        }
        else if (key == "ui_sound" || key == "toast_sound" || key == "boot_sound") {
            try {
                size_t used = 0;
                const int index = std::stoi(std::string(value), &used);
                const int limit = key == "boot_sound" ? static_cast<int>(boot_sounds.size()) :
                                  key == "toast_sound" ? static_cast<int>(toast_sounds.size()) :
                                  static_cast<int>(ui_sounds.size());
                if (used == value.size() && index >= 0 && index < limit) {
                    if (key == "ui_sound") settings.ui_sound = index;
                    else if (key == "toast_sound") settings.toast_sound = index;
                    else settings.boot_sound = index;
                }
            } catch (...) {}
        }
        else if (key == "music_volume") {
            float volume = 0;
            const auto result = std::from_chars(value.data(), value.data() + value.size(), volume);
            if (result.ec == std::errc{} && result.ptr == value.data() + value.size() && std::isfinite(volume))
                settings.music_volume = std::clamp(volume, 0.0f, 0.4f);
        } else if (key == "font_family") {
            if (value == "SST") settings.font_family = L"SST";
            else if (value == "Segoe UI") settings.font_family = L"Segoe UI";
            else if (value == "Consolas") settings.font_family = L"Consolas";
            else if (value == "Arial") settings.font_family = L"Arial";
        } else if (key == "home_layout") {
            if (value == "xmb") settings.home_layout = AppSettings::HomeLayout::xmb;
            else if (value == "flix") settings.home_layout = AppSettings::HomeLayout::flix;
            else if (value == "metro") settings.home_layout = AppSettings::HomeLayout::metro;
            else if (value == "psfive") settings.home_layout = AppSettings::HomeLayout::psfive;
            else if (value == "snitch") settings.home_layout = AppSettings::HomeLayout::snitch;
            else if (value == "default") settings.home_layout = AppSettings::HomeLayout::shelves;
        } else if (key == "xmb_size" || key == "psfive_size") {
            auto& size = key == "xmb_size" ? settings.xmb_size : settings.psfive_size;
            if (value == "small") size = AppSettings::XmbSize::compact;
            else if (value == "default") size = AppSettings::XmbSize::normal;
            else if (value == "large") size = AppSettings::XmbSize::large;
        } else if (key == "flix_size") {
            if (value == "default") settings.flix_size = AppSettings::FlixSize::normal;
            else if (value == "large") settings.flix_size = AppSettings::FlixSize::large;
        } else if (key == "background_grid_style") {
            if (value == "dots") settings.background_grid_style = AppSettings::GridStyle::dots;
            else if (value == "lines") settings.background_grid_style = AppSettings::GridStyle::lines;
            else if (value == "mesh") settings.background_grid_style = AppSettings::GridStyle::mesh;
        } else if (key == "tile_shape") {
            if (value == "squircle") settings.tile_shape = AppSettings::TileShape::squircle;
            else if (value == "rounded") settings.tile_shape = AppSettings::TileShape::rounded;
            else if (value == "square") settings.tile_shape = AppSettings::TileShape::square;
        } else if (key == "language") {
            if (value == "system") settings.language = AppSettings::LanguageChoice::system;
            else if (value == "pl") settings.language = AppSettings::LanguageChoice::pl;
            else if (value == "en") settings.language = AppSettings::LanguageChoice::en;
            else if (value == "pt-BR") settings.language = AppSettings::LanguageChoice::pt_br;
            else if (value == "es") settings.language = AppSettings::LanguageChoice::es;
        } else if ((key == "accent_color" || key == "ui_accent_color") && value.size() == 7 && value.front() == '#') {
            // accent_color keeps its original meaning (cover highlight) for existing settings files.
            uint32_t rgb = 0;
            const auto result = std::from_chars(value.data() + 1, value.data() + 7, rgb, 16);
            if (result.ec == std::errc{} && result.ptr == value.data() + 7) {
                if (key == "accent_color") settings.cover_accent_rgb = rgb;
                // The former default (sage, always written) gives way to the new white default.
                else if (rgb && rgb != 0xA3D1C7) settings.ui_accent_rgb = rgb;
            }
        } else if (key == "widget_layout") {
            while (!value.empty() && settings.widget_layout.size() < 128) {
                const auto comma = value.find(',');
                const auto item = trim(value.substr(0, comma));
                const auto colon = item.find(':');
                if (colon != item.npos) {
                    const auto id = item.substr(0, colon), size = item.substr(colon + 1);
                    if (widgets::validId(id) && (size == "icon" || size == "square" || size == "wide") &&
                        std::none_of(settings.widget_layout.begin(), settings.widget_layout.end(), [&](const auto& p) { return p.id == id; }))
                        settings.widget_layout.push_back({std::string(id), size == "icon" ? widgets::Size::icon : size == "wide" ? widgets::Size::wide : widgets::Size::square});
                }
                value = comma == value.npos ? std::string_view{} : value.substr(comma + 1);
            }
        } else if (key == "hidden_collections" || key == "collection_order" || key == "pc_platforms" || key == "hidden_studios" || key == "studio_order") {
            while (!value.empty()) {
                const auto comma = value.find(',');
                const auto id = trim(value.substr(0, comma));
                if (!id.empty() && id.size() < 120) {
                    if (key == "pc_platforms") {
                        if (id == "gog" || id == "battlenet" || id == "ea" || id == "ubisoft" || id == "epic" || id == "xbox" || id == "drmfree" ||
                            id == "multimc" || id == "amazon")
                            settings.enabled_pc_sources.emplace(id);
                    } else if (key == "hidden_collections") settings.hidden_collections.emplace(id);
                    else if (key == "hidden_studios") settings.hidden_studios.emplace(id);
                    else if (key == "studio_order") { if (std::find(settings.studio_order.begin(), settings.studio_order.end(), id) == settings.studio_order.end()) settings.studio_order.emplace_back(id); }
                    else settings.collection_order.emplace_back(id);
                }
                value = comma == std::string_view::npos ? std::string_view{} : value.substr(comma + 1);
            }
        }
    }
    // A hand-edited file cannot enable both animations; tracing wins independently of key order.
    if (settings.glow_trace) settings.glow_pulse = false;
    if (!settings.companion_import_folder.empty()) settings.setCompanionImportFolder(settings.companion_import_folder);
    return settings;
}

AppSettings loadAppSettings() {
    return loadAppSettings(userConfigDirectory() / L"settings.ini");
}

AppSettings loadAppSettings(const std::filesystem::path& file) {
    std::error_code missing;
    if (!std::filesystem::exists(file, missing) && !missing) {
        AppSettings first_start; // No settings yet: the walkthrough runs once.
        first_start.tour_done = false;
        first_start.first_start = true;
        return first_start;
    }
    // A file another program holds for a moment (an antivirus scan, a sync client) is tried
    // again; only then are these the defaults, marked so the user's file is never overwritten.
    for (int attempt = 0; attempt < 5; ++attempt) {
        if (attempt) std::this_thread::sleep_for(std::chrono::milliseconds(60));
        std::ifstream input(file, std::ios::binary | std::ios::ate);
        if (!input || input.tellg() < 0) continue;
        if (input.tellg() > 1024 * 1024) break;
        std::string text(static_cast<size_t>(input.tellg()), '\0');
        input.seekg(0);
        if (!input.read(text.data(), static_cast<std::streamsize>(text.size()))) continue;
        if (text.empty() || text.find_first_not_of(std::string_view("\0 \t\r\n", 5)) == std::string::npos) break; // Zeroed by a power cut.
        return parseAppSettings(text);
    }
    AppSettings defaults;
    defaults.load_failed = true;
    return defaults;
}

bool saveAppSettings(const AppSettings& settings) {
    return saveAppSettings(settings, userConfigDirectory() / L"settings.ini");
}

bool saveAppSettings(const AppSettings& settings, const std::filesystem::path& file) {
    std::ostringstream output;
    output.imbue(std::locale::classic());
    output << "drawer_opacity=" << settings.drawer.opacity << '\n'
           << "windowed=" << settings.windowed << '\n'
           << "launch_tv_mode=" << settings.launch_tv_mode << '\n'
           << "pc_sources_detected=" << settings.pc_sources_detected << '\n'
           << "autostart=" << settings.autostart << '\n'
           << "low_memory=" << settings.low_memory << '\n'
           << "texture_compression=" << settings.texture_compression << '\n'
           << "tv_monitor=" << (settings.tv_monitor.find_first_of(L"\r\n") == std::wstring::npos ? text_encoding::utf8(settings.tv_monitor) : std::string()) << '\n'
         << "desktop_platform_badges=" << settings.desktop_platform_badges << '\n'
         << "desktop_gray_uninstalled=" << settings.desktop_gray_uninstalled << '\n'
           << "desktop_maximized=" << settings.desktop_maximized << '\n'
           << "desktop_poster=" << std::clamp(settings.desktop_poster, 112, 240) << '\n'
           << "desktop_sort=" << std::clamp(settings.desktop_sort, 1, 4) << '\n'
           << "desktop_installed=" << settings.desktop_installed << '\n'
           << "desktop_narrow=" << settings.desktop_narrow << '\n'
           << "desktop_pane=" << (settings.desktop_pane <= 0 ? 0 : std::clamp(settings.desktop_pane, 300, 4000)) << '\n'
           << "desktop_section=" << (settings.desktop_section.find_first_of("\r\n") == std::string::npos ? settings.desktop_section : std::string("all")) << '\n'
           << "desktop_game=" << (settings.desktop_game.find_first_of("\r\n") == std::string::npos ? settings.desktop_game : std::string()) << '\n'
           << "drawer_fade_fraction=" << settings.drawer.fade_fraction << '\n'
           << "show_uninstalled=" << settings.show_uninstalled << '\n'
           << "recent_uninstalled=" << settings.recent_uninstalled << '\n'
           << "save_backup_after_game=" << settings.save_backup_after_game << '\n'
           << "default_view_console=" << std::clamp(settings.default_view_console, 0, 11) << '\n'
           << "default_view_collection=" << std::clamp(settings.default_view_collection, 0, 11) << '\n'
           << "default_view_studio=" << std::clamp(settings.default_view_studio, 0, 11) << '\n'
           << "show_emulation=" << settings.show_emulation << '\n'
           << "studio_libraries=" << settings.studio_libraries << '\n'
           << "show_private=" << settings.show_private << '\n'
           << "show_store=" << settings.show_store << '\n'
           << "quick_resume=" << settings.quick_resume << '\n'
           << "guide_menu=" << settings.guide_menu << '\n'
           << "guide_overlay=" << settings.guide_overlay << '\n'
           << "game_fps=" << settings.game_fps << '\n'
           << "game_fps_low=" << settings.game_fps_low << '\n'
           << "steam_controller=" << settings.steam_controller << '\n'
           << "focus_spring=" << settings.focus_spring << '\n'
           << "focus_gap=" << (settings.focus_gap == AppSettings::FocusGap::outside ? "outside" :
                               settings.focus_gap == AppSettings::FocusGap::inside ? "inside" : "none") << '\n'
           << "tile_entrance=" << (settings.tile_entrance == AppSettings::TileEntrance::rise ? "rise" :
                                   settings.tile_entrance == AppSettings::TileEntrance::none ? "none" : "grow") << '\n'
           << "glow_level=" << std::clamp(settings.glow_level, 0.0f, 2.0f) << '\n'
           << "glow_pulse=" << (settings.glow_pulse && !settings.glow_trace) << '\n'
           << "glow_trace=" << settings.glow_trace << '\n'
           << "background_grid=" << std::clamp(settings.background_grid, 0.0f, 1.0f) << '\n'
           << "background_grid_style=" << (settings.background_grid_style == AppSettings::GridStyle::lines ? "lines" :
                                           settings.background_grid_style == AppSettings::GridStyle::mesh ? "mesh" : "dots") << '\n'
           << "screensaver=" << settings.screensaver << '\n'
           << "drmfree_background=" << settings.drm_background << '\n'
           << "tour_done=" << settings.tour_done << '\n'
           << "update_check=" << settings.update_check << '\n'
           << "update_skipped=" << text_encoding::utf8Lenient(settings.update_skipped) << '\n'
           << "screensaver_style=" << (settings.screensaver_style == AppSettings::ScreensaverStyle::boxes ? "boxes" :
                                       settings.screensaver_style == AppSettings::ScreensaverStyle::discs ? "discs" : "backgrounds") << '\n'
           << "screensaver_minutes=" << (settings.screensaver_minutes == 3 || settings.screensaver_minutes == 5 ||
                                         settings.screensaver_minutes == 10 ? settings.screensaver_minutes : 1) << '\n'
           << "music_volume=" << settings.music_volume << '\n'
           << "music_track=" << music_themes[static_cast<size_t>(std::clamp(settings.music_theme, 0, static_cast<int>(music_themes.size()) - 1))].id << '\n'
           << "ui_sound=" << std::clamp(settings.ui_sound, 0, static_cast<int>(ui_sounds.size()) - 1) << '\n'
           << "toast_sound=" << std::clamp(settings.toast_sound, 0, static_cast<int>(toast_sounds.size()) - 1) << '\n'
           << "boot_sound=" << std::clamp(settings.boot_sound, 0, static_cast<int>(boot_sounds.size()) - 1) << '\n'
           << "sound_folder=" << text_encoding::utf8Lenient(settings.sound_folder.wstring()) << '\n'
           << "file_browser_folder=" << text_encoding::utf8Lenient(settings.file_browser_folder.wstring()) << '\n'
           << "multimc_path=" << text_encoding::utf8Lenient(settings.multimc_path.wstring()) << '\n'
           << "music_file=" << text_encoding::utf8Lenient(settings.music_file) << '\n'
           << "boot_file=" << text_encoding::utf8Lenient(settings.boot_file) << '\n'
           << "font_family=";
    for (wchar_t ch : settings.font_family) output << static_cast<char>(ch); // Supported font names are ASCII.
    output << '\n';
    const auto hex_color = [&](uint32_t rgb) {
        constexpr char hex[] = "0123456789ABCDEF";
        output << '#';
        for (int shift = 20; shift >= 0; shift -= 4) output << hex[(rgb >> shift) & 15];
        output << '\n';
    };
    output << "accent_color=";
    if (settings.cover_accent_rgb) hex_color(settings.cover_accent_rgb); else output << "auto\n";
    output << "ui_accent_color=";
    hex_color(settings.ui_accent_rgb ? settings.ui_accent_rgb : AppSettings{}.ui_accent_rgb);
    output << "button_color=";
    if (settings.button_rgb) hex_color(settings.button_rgb); else output << "accent\n";
    output << "button_idle=" << (settings.button_idle == AppSettings::ButtonIdle::tinted ? "tinted" :
                                 settings.button_idle == AppSettings::ButtonIdle::dark ? "dark" : "gray") << '\n';
    output << "home_layout=" << (settings.home_layout == AppSettings::HomeLayout::xmb ? "xmb" :
                                 settings.home_layout == AppSettings::HomeLayout::flix ? "flix" :
                                 settings.home_layout == AppSettings::HomeLayout::metro ? "metro" :
                                 settings.home_layout == AppSettings::HomeLayout::psfive ? "psfive" :
                                 settings.home_layout == AppSettings::HomeLayout::snitch ? "snitch" : "default") << '\n';
    output << "psfive_size=" << (settings.psfive_size == AppSettings::XmbSize::compact ? "small" :
                                settings.psfive_size == AppSettings::XmbSize::large ? "large" : "default") << '\n';
    output << "psfive_clock=" << settings.psfive_clock << '\n';
    output << "show_categories=" << settings.show_categories << '\n';
    output << "xmb_posters=" << settings.xmb_posters << '\n';
    output << "shelves_posters=" << settings.shelves_posters << '\n';
    output << "shelves_category_all=" << settings.shelves_category_all << '\n';
    output << "xmb_size=" << (settings.xmb_size == AppSettings::XmbSize::compact ? "small" :
                             settings.xmb_size == AppSettings::XmbSize::large ? "large" : "default") << '\n';
    output << "flix_size=" << (settings.flix_size == AppSettings::FlixSize::large ? "large" : "default") << '\n';
    output << "flix_category_all=" << settings.flix_category_all << '\n';
    output << "flix_top_icons=" << settings.flix_top_icons << '\n';
    output << "shelves_hero_facts=" << settings.shelves_hero_facts << '\n';
    output << "search_art=" << (settings.search_art == AppSettings::SearchArt::poster ? "poster" :
                                settings.search_art == AppSettings::SearchArt::landscape ? "landscape" : "square") << '\n';
    {
        std::vector<std::string> off(settings.artwork_sources_off.begin(), settings.artwork_sources_off.end());
        std::sort(off.begin(), off.end());
        output << "artwork_sources_off=";
        for (size_t i = 0; i < off.size(); ++i) output << (i ? "," : "") << off[i];
        output << '\n';
    }
    output << "wallpaper=" << (settings.wallpaper == AppSettings::WallpaperKind::picture ? "picture" :
                               settings.wallpaper == AppSettings::WallpaperKind::video ? "video" : "none") << '\n';
    output << "wallpaper_file=" << text_encoding::utf8Lenient(settings.wallpaper_file.wstring()) << '\n';
    output << "wallpaper_id=" << settings.wallpaper_id << '\n';
    output << "wallpaper_snitch=" << settings.wallpaper_snitch << '\n';
    output << "wallpaper_menus=" << settings.wallpaper_menus << '\n';
    output << "wallpaper_artwork=" << settings.wallpaper_artwork << '\n';
    output << "wallpaper_tiles=" << settings.wallpaper_tiles << '\n';
    output << "home_tiles=" << (settings.home_tiles == AppSettings::HomeTiles::first ? "first" :
                                settings.home_tiles == AppSettings::HomeTiles::last ? "last" : "layout") << '\n';
    output << "add_game_tile=" << settings.add_game_tile << '\n';
    output << "psfive_details=" << settings.psfive_details << '\n';
    output << "menu_dialog=" << settings.menu_dialog << '\n';
    output << "metro_light=" << settings.metro_light << '\n';
    output << "language=" << (settings.language == AppSettings::LanguageChoice::pl ? "pl" :
                              settings.language == AppSettings::LanguageChoice::en ? "en" :
                              settings.language == AppSettings::LanguageChoice::pt_br ? "pt-BR" :
                              settings.language == AppSettings::LanguageChoice::es ? "es" : "system") << '\n';
    output << "tile_shape=" << (settings.tile_shape == AppSettings::TileShape::rounded ? "rounded" :
                                settings.tile_shape == AppSettings::TileShape::square ? "square" : "squircle") << '\n';
    output << "panel_borders=" << settings.panel_borders << '\n';
    output << "screen_transitions=" << settings.screen_transitions << '\n';
    output << "glass=" << std::clamp(settings.glass, 0.0f, 1.0f) << '\n';
    output << "input_hints=" << settings.input_hints << '\n';
    output << "glance=" << settings.glance << '\n';
    output << "tarot=" << settings.tarot << '\n';
    output << "companion=" << settings.companion << '\n';
    output << "companion_look=" << settings.companion_look << '\n';
    output << "companion_guests=" << settings.companion_guests << '\n';
    output << "companion_gamification=" << settings.companion_gamification << '\n';
    output << "companion_user_name=" << text_encoding::utf8Lenient(settings.companion_user_name) << '\n';
    output << "companion_user_gender=" << (settings.companion_user_gender == AppSettings::CompanionUserGender::male ? "male" :
                                           settings.companion_user_gender == AppSettings::CompanionUserGender::female ? "female" : "auto") << '\n';
    for (const auto& [look, costume] : settings.companion_costumes)
        if (!costume.empty()) output << "companion_costume_" << look << '=' << costume << '\n';
    output << "companion_debug=" << settings.companion_debug << '\n';
    output << "companion_model=" << settings.companion_model << '\n';
    output << "companion_xai_model=" << settings.companion_xai_model << '\n';
    output << "companion_host=" << text_encoding::utf8Lenient(settings.companion_host) << '\n';
    output << "companion_voice=" << (settings.companion_voice == AppSettings::CompanionVoice::chat ? "chat" :
                                     settings.companion_voice == AppSettings::CompanionVoice::everywhere ? "all" : "off") << '\n';
    output << "companion_voice_model=" << settings.companion_voice_model << '\n';
    for (const auto& [look, voice] : settings.companion_voice_ids)
        if (!voice.empty()) output << "companion_voice_id_" << look << '=' << voice << '\n';
    output << "companion_city=" << settings.companion_city << '\n';
    for (const auto& [look, city] : settings.companion_cities)
        if (!city.empty()) output << "companion_city_" << look << '=' << city << '\n';
    output << "companion_provider=" << AppSettings::companion_provider_keys[static_cast<size_t>(settings.companion_provider)] << '\n';
    output << "companion_claude_model=" << settings.companion_claude_model << '\n';
    output << "companion_ollama_model=" << settings.companion_ollama_model << '\n';
    output << "companion_ollama_host=" << text_encoding::utf8Lenient(settings.companion_ollama_host) << '\n';
    output << "companion_model_info=" << settings.companion_model_info << '\n';
    output << "companion_typing=" << settings.companion_typing << '\n';
    output << "companion_weather=" << settings.companion_weather << '\n';
    output << "companion_remote=" << settings.companion_remote << '\n';
    output << "companion_remote_port=" << settings.companion_remote_port << '\n';
    output << "companion_remote_key=" << settings.companion_remote_key << '\n';
    output << "companion_photos=" << (settings.companion_photos == AppSettings::CompanionPhotos::folder ? "folder" : "off") << '\n';
    output << "companion_photo_folder=" << text_encoding::utf8Lenient(settings.companion_photo_folder) << '\n';
    output << "companion_reply_length=" << companion::replyStyle(settings.companion_reply_length).key << '\n';
    output << "companion_feed_folder=" << text_encoding::utf8Lenient(settings.companion_feed_folder) << '\n';
    output << "companion_import_folder=" << text_encoding::utf8Lenient(settings.companion_import_folder) << '\n';
    output << "companion_memory=" << settings.companion_memory << '\n';
    output << "companion_notes=" << settings.companion_notes << '\n';
    output << "companion_semantic_memory=" << settings.companion_semantic_memory << '\n';
    output << "hint_glyphs=" << (settings.hint_glyphs == AppSettings::HintGlyphs::xbox ? "xbox" :
                                 settings.hint_glyphs == AppSettings::HintGlyphs::playstation ? "playstation" :
                                 settings.hint_glyphs == AppSettings::HintGlyphs::nintendo ? "nintendo" : "auto") << '\n';
    output << "steam_silent_launch=" << settings.steam_silent_launch << '\n';
    output << "xbox_own_install=" << settings.xbox_own_install << '\n';
    output << "hide_while_playing=" << settings.hide_while_playing << '\n';
    output << "steam_account_switch=" << settings.steam_account_switch << '\n';
    output << "steam_install_dialog=" << settings.steam_install_dialog << '\n';
    output << "aurelia=" << settings.aurelia << '\n' << "aurelia_intro_done=" << settings.aurelia_intro_done << '\n';
    for (const auto& id : settings.aurelia_standalone) output << "aurelia_standalone=" << id << '\n';
    for (const auto& id : settings.aurelia_cloud_skip) output << "aurelia_cloud_skip=" << id << '\n';
    output << "steam_enabled=" << settings.steam_enabled << '\n';
    for (const auto& [app, steam_id] : settings.steam_launch_accounts) output << "steam_account=" << app << ':' << steam_id << '\n';
    output << "aurelia_default=" << settings.aurelia_default << '\n' << "aurelia_cloud=" << settings.aurelia_cloud << '\n';
    for (const auto& id : settings.aurelia_steam) output << "aurelia_steam=" << id << '\n';
    output << "game_videos=" << settings.game_videos << '\n';
    output << "video_delay=" << settings.video_delay << '\n';
    output << "video_volume=" << settings.video_volume << '\n';
    output << "steam_trailers=" << settings.steam_trailers << '\n';
    output << "trailer_space_mb=" << settings.trailer_space_mb << '\n';
    output << "emulation_names=" << settings.emulation_names << '\n';
    output << "emulation_rom_art=" << settings.emulation_rom_art << '\n';
    output << "video_folder=" << text_encoding::utf8Lenient(settings.video_folder.wstring()) << '\n';
    output << "console_colors=" << settings.console_colors << '\n';
    output << "metro_3d=" << settings.metro_3d << '\n';
    output << "game_action=" << (settings.accept_action == AppSettings::AcceptAction::details ? "details" : "launch") << '\n';
    output << "hidden_collections=";
    bool first = true;
    std::vector<std::string> hidden(settings.hidden_collections.begin(), settings.hidden_collections.end());
    std::sort(hidden.begin(), hidden.end());
    for (const auto& id : hidden) {
        if (!first) output << ',';
        output << id;
        first = false;
    }
    output << "\ncollection_order=";
    first = true;
    for (const auto& id : settings.collection_order) {
        if (!first) output << ',';
        output << id;
        first = false;
    }
    output << "\nhidden_studios=";
    std::vector<std::string> hidden_studios(settings.hidden_studios.begin(), settings.hidden_studios.end());
    std::sort(hidden_studios.begin(), hidden_studios.end());
    for (size_t i = 0; i < hidden_studios.size(); ++i) output << (i ? "," : "") << hidden_studios[i];
    output << "\nstudio_order=";
    for (size_t i = 0; i < settings.studio_order.size(); ++i) output << (i ? "," : "") << settings.studio_order[i];
    output << '\n';
    if (settings.desktop_window[2] > 0 && settings.desktop_window[3] > 0)
        output << "desktop_window=" << settings.desktop_window[0] << ',' << settings.desktop_window[1] << ','
               << settings.desktop_window[2] << ',' << settings.desktop_window[3] << '\n';
    output << "desktop_collapsed=";
    first = true;
    for (const auto* group : {"library", "platforms", "emulation", "collections", "publishers", "developers"}) if (settings.desktop_collapsed.contains(group)) {
        if (!first) output << ',';
        output << group;
        first = false;
    }
    output << "\ndesktop_expanded=";
    first = true;
    for (const auto* group : {"publishers", "developers"}) if (settings.desktop_expanded.contains(group)) {
        if (!first) output << ',';
        output << group;
        first = false;
    }
    output << '\n';
    output << "pc_platforms=";
    bool first_platform = true;
    for (const auto* source : {"drmfree", "gog", "battlenet", "ea", "ubisoft", "epic", "xbox", "multimc", "amazon"}) if (settings.enabled_pc_sources.contains(source)) {
        if (!first_platform) output << ',';
        output << source;
        first_platform = false;
    }
    output << '\n';
    output << "widget_layout=";
    for (size_t i = 0; i < settings.widget_layout.size(); ++i) {
        if (i) output << ',';
        output << settings.widget_layout[i].id << ':' << widgets::name(settings.widget_layout[i].size);
    }
    output << '\n';
    if (settings.load_failed) {
        // The unreadable file stays for the user (or a later fix) before these settings replace it.
        std::error_code ec;
        if (std::filesystem::exists(file, ec) &&
            !std::filesystem::copy_file(file, file.parent_path() / L"settings.unreadable.ini", std::filesystem::copy_options::overwrite_existing, ec)) return false;
    }
    return atomic_file::writeDurable(file, output.str());
}
