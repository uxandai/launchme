// Kopia i przenoszenie: password sealing (RFC 7914 / NIST vectors), the ZIP backup's round trip between
// two profiles, merging, the password and path rules, and a staged import applied at the next start.
#include "../src/platform/password_crypto.hpp"
#include "../src/platform/profile_transfer.hpp"
#include "../src/platform/protected_file.hpp"
#include "../src/platform/zip_archive.hpp"
#include "../src/platform/hashing.hpp"
#include "../src/library/custom_games.hpp"
#include "../src/library/play_journal.hpp"

#include <Windows.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace {
std::string hex(const std::string& bytes) { return hashing::hex(std::span(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size())); }
std::string hex(const std::vector<uint8_t>& bytes) { return hashing::hex(bytes); }
void put(const std::filesystem::path& file, std::string_view text) {
    std::filesystem::create_directories(file.parent_path());
    std::ofstream(file, std::ios::binary) << text;
}
std::string get(const std::filesystem::path& file) {
    std::ifstream input(file, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), {}};
}
void useConfig(const std::filesystem::path& folder) {
#ifdef _WIN32
    _wputenv_s(L"LAUNCHME_CONFIG_DIR", folder.c_str());
#else
    setenv("LAUNCHME_CONFIG_DIR", folder.c_str(), 1);
#endif
}
} // namespace

int main() {
    // The other computer's games folder: a path valid on this system (own games keep only such paths).
#ifdef _WIN32
    const std::filesystem::path other_games = L"D:\\Gry";
#else
    const std::filesystem::path other_games = L"/media/other/Gry";
#endif
    using namespace profile_transfer;
    // (1) PBKDF2-HMAC-SHA256: RFC 7914 §11, the same bytes from CNG and OpenSSL.
    {
        const std::string salt = "salt";
        const auto key = password_crypto::pbkdf2Sha256("passwd", reinterpret_cast<const uint8_t*>(salt.data()), salt.size(), 1, 64);
        if (!key || hex(*key) != "55ac046e56e3089fec1691c22544b605f94185216dde0465e68b9d57c20dacbc"
                                 "49ca9cccf179b645991664b39d77ef317c71b845b1e30bd509112041d3a19783") return 1;
    }
    // (2) AES-256-GCM: NIST GCM test cases 13 and 14 (zero key and nonce).
    {
        const uint8_t key[32]{}, nonce[12]{};
        const auto empty = password_crypto::aesGcmEncrypt(key, nonce, {});
        if (!empty || hex(*empty) != "530f8afbc74536b9a963b4f1c4cb738b") return 2;
        const auto block = password_crypto::aesGcmEncrypt(key, nonce, std::string(16, '\0'));
        if (!block || hex(*block) != "cea7403d4d606b6e074ec5d3baf39d18d0d1c8a799996bf0265b98b5d48ab919") return 3;
        const auto back = password_crypto::aesGcmDecrypt(key, nonce, *block);
        if (!back || *back != std::string(16, '\0')) return 4;
        auto damaged = *block; damaged[3] ^= 1;
        if (password_crypto::aesGcmDecrypt(key, nonce, damaged)) return 5;
    }
    // (3) Sealing: fixed salt and nonce give fixed bytes; a wrong password or a changed byte opens nothing.
    {
        std::array<uint8_t, password_crypto::salt_size> salt{};
        std::array<uint8_t, password_crypto::nonce_size> nonce{};
        for (size_t i = 0; i < salt.size(); ++i) salt[i] = static_cast<uint8_t>(i);
        const auto a = password_crypto::sealWith("hasło", "sekret", 1000, salt, nonce);
        const auto b = password_crypto::sealWith("hasło", "sekret", 1000, salt, nonce);
        if (!a || !b || *a != *b || !password_crypto::sealed(*a)) return 6;
        if (password_crypto::open("hasło", *a) != std::optional<std::string>("sekret")) return 7;
        if (password_crypto::open("haslo", *a) || password_crypto::open("", *a)) return 8;
        auto changed = *a; changed.back() ^= 1;
        if (password_crypto::open("hasło", changed)) return 9;
        const auto random = password_crypto::seal("x", "y", 1000);
        if (!random || *random == *password_crypto::seal("x", "y", 1000) || password_crypto::open("x", *random) != std::optional<std::string>("y")) return 10;
        if (password_crypto::seal("", "y")) return 11;
    }
    // (4) Entry names and path rules.
    {
        for (const auto* bad : {"", "/a", "a/", "../a", "a/../b", "a\\b", "C:/a", "a/./b", "a//b", "a/b."})
            if (zip_archive::safeName(bad)) return 12;
        if (!zip_archive::safeName("artwork/files/artwork/selections/steam%3A10/cover.selected")) return 13;
        const auto rules = parsePathMap(L"D:\\ROMs => /mnt/roms ; \"E:\\Gry\"=>\"F:\\Gry\"; zle; =>x");
        if (rules.size() != 2 || rules[0].first != L"D:\\ROMs" || rules[1].second != L"F:\\Gry") return 14;
        const auto moved = mapPath(L"D:\\ROMs\\snes\\Zelda.sfc", rules).generic_wstring();
        if (moved != L"/mnt/roms/snes/Zelda.sfc") return 15;
        if (mapPath(L"D:\\ROMsy\\a.sfc", rules) != std::filesystem::path(L"D:\\ROMsy\\a.sfc")) return 16; // A longer name is no match.
        if (mapPath(L"e:\\gry\\x.exe", rules).generic_wstring() != L"F:/Gry/x.exe" &&
            mapPath(L"e:\\gry\\x.exe", rules).wstring() != L"F:\\Gry\\x.exe") return 17;
        if (backupName(0).find(L"launchme_backup_19") != 0 || !backupName(0).ends_with(L".zip")) return 18;
    }
    // (5) Merging the library: larger play times, both sets of collections and hidden games; replace keeps the place.
    {
        LibraryState local, imported;
        local.playtime_ms["steam:10"] = 500; imported.playtime_ms["steam:10"] = 200; imported.playtime_ms["gog:7"] = 90;
        local.first_seen["steam:10"] = 2000; imported.first_seen["steam:10"] = 1000;
        local.hidden.insert("steam:1"); imported.hidden.insert("gog:7");
        imported.collections.names["local:abc"] = L"RPG";
        imported.collections.members["local:abc"]["gog:7"] = true;
        local.bookmark = LibraryBookmark{}; local.bookmark->category = "here";
        auto merged = local;
        mergeLibraryState(merged, imported, Mode::merge);
        if (merged.playtime_ms["steam:10"] != 500 || merged.playtime_ms["gog:7"] != 90 || merged.first_seen["steam:10"] != 1000 ||
            merged.hidden.size() != 2 || merged.collections.names["local:abc"] != L"RPG" || !merged.collections.members["local:abc"]["gog:7"]) return 19;
        auto replaced = local;
        mergeLibraryState(replaced, imported, Mode::replace);
        if (replaced.playtime_ms["steam:10"] != 200 || replaced.hidden.contains("steam:1") || !replaced.bookmark || replaced.bookmark->category != "here") return 20;
    }

    // (6) Export from one profile, import into another (merge), applied as at the next start.
    const auto base = std::filesystem::temp_directory_path() / (L"launchme-transfer-" + std::to_wstring(GetCurrentProcessId()));
    std::filesystem::remove_all(base);
    const auto from = base / L"from", to = base / L"to", out = base / L"out";
    std::filesystem::create_directories(from); std::filesystem::create_directories(to); std::filesystem::create_directories(out);
    {
        useConfig(from);
        AppSettings settings;
        settings.metro_light = true;
        settings.tv_monitor = L"\\\\.\\DISPLAY9";
        settings.sound_folder = L"Q:\\nowhere\\sounds";
        settings.wallpaper_file = from / L"wallpapers" / L"wall.png";
        if (!saveAppSettings(settings, from / L"settings.ini")) return 21;
        put(from / L"wallpapers" / L"wall.png", "png");
        LibraryState state;
        state.playtime_ms["steam:10"] = 7000;
        state.collections.names["local:x1"] = L"Ulubione RPG";
        if (!saveLibraryState(state, from / L"library-state.ini")) return 22;
        custom_games::Entry game{"0123456789abcdef", L"Gra", other_games / L"gra.exe"};
        if (!custom_games::save({game}, from / L"custom-games.json")) return 23;
        put(from / L"themes" / L"Mój.ini", "glass=0.2\n");
        put(from / L"artwork" / L"selections" / L"steam%3A10" / L"cover.selected", "1 5\n");
        put(from / L"covers" / L"10.v2.img", "image");
        put(from / L"covers" / L"20.v2.img", "image2");
        put(from / L"covers" / L"20.v2.img.manual-focus", "0.5 0.5");
        put(from / L"steam_web_api_key.txt", "PLAINKEY");
        if (!protected_file::write(from / L"companion_claude_key.dat", "CLAUDEKEY", L"test")) return 24;
        if (!protected_file::write(from / L"accounts" / L"gog.session", "SESSION", L"test")) return 25;
        put(from / L"companion" / L"feed" / L"saki" / L"posts" / L"a.json", "{}");
        play_journal::Journal journal; play_journal::played(journal, 20261101, "steam:10", L"Gra", 60000);
        if (!play_journal::save(journal, from / L"play-journal.tsv")) return 26;
#ifdef _WIN32 // Linux keeps this user's protection key there (protected_file.cpp): a test must not touch it.
        put(from / L"protected.key", "NEVER");
#endif
        put(from / L"logs" / L"x.log", "NEVER");
    }
    ExportInput exported;
    exported.config = from; exported.folder = out; exported.app_version = "0.7.17";
    for (const auto part : parts) exported.parts.insert(part);
    if (exportProfile(exported).ok) return 27; // Secrets without a password: refused.
    exported.password = "tajne hasło";
    uint64_t last_done = 0, last_total = 0;
    exported.progress = [&](uint64_t done, uint64_t total) { last_done = done; last_total = total; };
    const auto result = exportProfile(exported);
    if (!result.ok || !std::filesystem::exists(result.path) || result.path.filename().wstring().find(L"launchme_backup_") != 0) { std::wcerr << result.error << L"\n"; return 28; }
    if (last_total == 0 || last_done != last_total) return 29;
    if (!exportProfile(exported).ok || std::distance(std::filesystem::directory_iterator(out), {}) != 2) return 30; // Never over the first one.
    {
        std::wstring error;
        const auto manifest = inspect(result.path, error);
        if (!manifest || !manifest->sealed || !manifest->has(Part::keys) || manifest->system.empty() || manifest->config != from.wstring()) return 31;
        zip_archive::Reader zip;
        if (!zip.open(result.path)) return 32;
        for (const auto& entry : zip.entries())
            if (entry.name.find("protected.key") != std::string::npos || entry.name.find("logs/") != std::string::npos ||
                entry.name.find("steam_web_api_key") != std::string::npos || entry.name.find("gog.session") != std::string::npos) return 33;
        if (zip.find("artwork/files/covers/20.v2.img.manual-focus") < 0 || zip.find("artwork/files/covers/20.v2.img") < 0 ||
            zip.find("artwork/files/covers/10.v2.img") >= 0 || zip.find("artwork-cache/files/covers/10.v2.img") < 0) return 34;
        const auto sealed = zip.read("secure.bin", 1 << 20);
        if (!sealed || sealed->find("PLAINKEY") != std::string::npos || sealed->find("CLAUDEKEY") != std::string::npos) return 35;
        if (inspect(base / L"none.zip", error) || error.empty()) return 36;
        put(base / L"other.zip", "PK not really");
        if (inspect(base / L"other.zip", error)) return 37;
    }
    {
        useConfig(to);
        AppSettings settings;
        settings.tv_monitor = L"\\\\.\\DISPLAY1";
        if (!saveAppSettings(settings, to / L"settings.ini")) return 38;
        LibraryState state;
        state.playtime_ms["steam:10"] = 9000;
        if (!saveLibraryState(state, to / L"library-state.ini")) return 39;
        put(to / L"themes" / L"Mój.ini", "local\n");
    }
    ImportInput imported;
    imported.config = to; imported.zip = result.path; imported.mode = Mode::merge;
    for (const auto part : parts) imported.parts.insert(part);
    imported.password = "zle";
    if (stageImport(imported).ok || pending(to)) return 40;
    imported.password = "tajne hasło";
    imported.paths = parsePathMap(other_games.wstring() + L" => " + (base / L"games").wstring());
    std::filesystem::create_directories(base / L"games");
    const auto staged = stageImport(imported);
    if (!staged.ok || !pending(to)) { std::wcerr << staged.error << L"\n"; return 41; }
    if (get(to / L"settings.ini").find("metro_light=1") != std::string::npos) return 42; // Nothing applied before the restart.
    const auto applied = applyPending(to);
    if (!applied.ok || pending(to) || applied.files == 0 || applied.backup.empty()) { std::wcerr << applied.error << L"\n"; return 43; }
    {
        const auto settings = loadAppSettings(to / L"settings.ini");
        if (!settings.metro_light || settings.tv_monitor != L"\\\\.\\DISPLAY1" || !settings.sound_folder.empty()) return 44;
        if (settings.wallpaper_file != to / L"wallpapers" / L"wall.png" || get(to / L"wallpapers" / L"wall.png") != "png") return 45;
        const auto state = loadLibraryState(to / L"library-state.ini");
        if (!state || state->playtime_ms.at("steam:10") != 9000 || state->collections.names.at("local:x1") != L"Ulubione RPG") return 46;
        const auto games = custom_games::load(to / L"custom-games.json").games;
        if (games.size() != 1 || games[0].path != (base / L"games" / L"gra.exe").lexically_normal()) return 47;
        if (get(to / L"themes" / L"Mój.ini") != "local\n") return 48; // Merging keeps this computer's file.
        if (get(to / L"steam_web_api_key.txt") != "PLAINKEY") return 49;
        useConfig(to);
        if (protected_file::read(to / L"companion_claude_key.dat", 1024) != std::optional<std::string>("CLAUDEKEY")) return 50;
        if (protected_file::read(to / L"accounts" / L"gog.session", 1024) != std::optional<std::string>("SESSION")) return 51;
        if (get(to / L"covers" / L"10.v2.img") != "image" || !std::filesystem::exists(to / L"covers" / L"20.v2.img.manual-focus")) return 52;
        if (play_journal::load(to / L"play-journal.tsv").days.at(20261101).at("steam:10").ms != 60000) return 53;
        if (std::filesystem::exists(to / L"protected.key") && get(to / L"protected.key") == "NEVER") return 54;
        if (!std::filesystem::exists(applied.backup / L"settings.ini") || !std::filesystem::exists(applied.backup / L"library-state.ini")) return 55;
        const auto taken = takeApplied(to);
        if (!taken || !taken->ok || taken->files != applied.files || takeApplied(to)) return 56;
    }
    // (7) Replace: the theme is the backup's now; only the chosen part comes in.
    imported.mode = Mode::replace;
    imported.parts = {Part::settings};
    if (!stageImport(imported).ok || !applyPending(to).ok) return 57;
    if (get(to / L"themes" / L"Mój.ini") != "glass=0.2\n") return 58;
    // (8) A damaged stage is dropped, never half applied.
    std::filesystem::create_directories(to / pending_folder);
    put(to / pending_folder / L"plan.json", "{nope");
    if (applyPending(to).ok || pending(to)) return 59;
    std::filesystem::remove_all(base);
    std::cout << "profile transfer tests passed\n";
    return 0;
}
