// Backgrounds (0.3.0): the sharpest SteamGridDB hero, cover-fit cache limits, the one-time release of
// shrunken backgrounds, the per-game background crop, and other stores' games' Steam trailer match.
#include "../src/artwork/background_focus.hpp"
#include "../src/artwork/cover_cache.hpp"
#include "../src/artwork/image_compact.hpp"
#include "../src/artwork/text_crop.hpp"
#include "../src/net/http.hpp"
#include "../src/platform/atomic_file.hpp"
#include "../src/store/nintendo_trailer.hpp"
#include "../src/store/steam_app_match.hpp"
#include "image_fixture.hpp"

#include <Windows.h>

#include <array>
#include <atomic>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace {
std::filesystem::path temporaryFolder() {
#ifdef _WIN32
    wchar_t base[MAX_PATH]{};
    GetTempPathW(MAX_PATH, base);
#else
    const auto base = std::filesystem::temp_directory_path();
#endif
    auto folder = std::filesystem::path(base) / (L"launchme-background-" + std::to_wstring(GetCurrentProcessId()));
    std::error_code ec;
    std::filesystem::remove_all(folder, ec);
    std::filesystem::create_directories(folder, ec);
    return folder;
}
}

int main() {
    if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) return 1;
    // The best rated hero at least 3000 px wide wins; else the best rated one; links must be HTTPS.
    const std::string heroes = R"({"success":true,"data":[
        {"id":1,"width":1920,"height":620,"url":"https://cdn2.steamgriddb.com/hero/a.png"},
        {"id":2,"width":3840,"height":1240,"url":"https://cdn2.steamgriddb.com/hero/b.png"},
        {"id":3,"width":3840,"height":1240,"url":"https://cdn2.steamgriddb.com/hero/c.png"}]})";
    if (preferredHeroUrl(heroes) != L"https://cdn2.steamgriddb.com/hero/b.png") return 2;
    if (preferredHeroUrl(R"({"data":[{"width":1920,"url":"https://x.test/a.jpg"},{"width":1600,"url":"https://x.test/b.jpg"}]})") != L"https://x.test/a.jpg") return 3;
    if (!preferredHeroUrl(R"({"data":[{"width":3840,"url":"http://x.test/a.jpg"}]})").empty() || !preferredHeroUrl("{}").empty() ||
        !preferredHeroUrl("not json").empty()) return 4;

    // Cache limits: a background keeps what covers 2560×1440 (cover-fit), the other roles fit inside.
    if (compactScale(3840, 1240, ImageRole::background) != 1.0) return 5;
    if (std::abs(compactScale(3840, 2160, ImageRole::background) - 2560.0 / 3840) > 1e-9) return 6;
    if (std::abs(compactScale(3840, 1240, ImageRole::landscape) - 1280.0 / 3840) > 1e-9) return 7;
    if (compactScale(1000, 500, ImageRole::cover) != 1.0) return 8;

    const auto root = temporaryFolder();
    std::error_code ec;
    // One-time release: a background the old compaction shrank (2560 wide, under 1440 high) goes; a
    // full one, a 2560×1440 one, a manually cropped one and a user's pick stay.
    const auto heroes_folder = root / L"heroes", store = root / L"stores" / L"epic" / L"abc", picks = root / L"artwork" / L"selections" / L"abc" / L"hero";
    for (const auto& folder : {heroes_folder, store, picks}) std::filesystem::create_directories(folder, ec);
    const auto shrunk = pngFixture(30, 2560, 827), full = pngFixture(30, 3840, 1240), screen = pngFixture(30, 2560, 1440);
    if (shrunk.empty() || full.empty() || screen.empty()) return 9;
    atomic_file::write(heroes_folder / L"10.v2.hd.img", shrunk);
    atomic_file::write(heroes_folder / L"10.v2.hd.focus", std::string("5"));
    atomic_file::write(heroes_folder / L"11.v2.hd.img", full);
    atomic_file::write(heroes_folder / L"12.v2.hd.img", screen);
    atomic_file::write(heroes_folder / L"13.v2.hd.img", shrunk);
    if (!saveManualTextCrop(heroes_folder / L"13.v2.hd.img", {0.3f, 0.5f})) return 10;
    atomic_file::write(store / L"hero.v2.img", shrunk);
    atomic_file::write(picks / L"7.img", shrunk);
    if (releaseShrunkenBackgrounds(root, [] { return true; }) != 2) return 11;
    if (std::filesystem::exists(heroes_folder / L"10.v2.hd.img") || std::filesystem::exists(heroes_folder / L"10.v2.hd.focus") ||
        std::filesystem::exists(store / L"hero.v2.img")) return 12;
    if (!std::filesystem::exists(heroes_folder / L"11.v2.hd.img") || !std::filesystem::exists(heroes_folder / L"12.v2.hd.img") ||
        !std::filesystem::exists(heroes_folder / L"13.v2.hd.img") || !std::filesystem::exists(picks / L"7.img")) return 13;
    if (releaseShrunkenBackgrounds(root, [] { return false; }) != 0) return 14;

    // Background crop: saved per game, bound to the picture's bytes; another picture starts centred.
    const auto cache = root / L"covers";
    Game game; game.kind = GameKind::store; game.source = GameSource::epic; game.source_id = "fortnite"; game.name = L"Fortnite";
    if (backgroundFocus(game, full, cache)) return 15;
    if (!saveBackgroundFocus(game, full, {0.25f, 0.5f}, cache)) return 16;
    const auto focus = backgroundFocus(game, full, cache);
    if (!focus || std::abs(focus->x - 0.25f) > 0.001f || std::abs(focus->y - 0.5f) > 0.001f) return 17;
    if (backgroundFocus(game, shrunk, cache)) return 18; // Another picture of the game.
    Game other = game; other.source_id = "other";
    if (backgroundFocus(other, full, cache)) return 19;
    if (!clearBackgroundFocus(game, cache) || backgroundFocus(game, full, cache)) return 20;

    // Trailers: a storesearch answer's app by the shared title rules, never an ambiguous or edition match.
    const std::string search = R"({"total":3,"items":[
        {"type":"app","name":"Alan Wake 2 Soundtrack","id":3},
        {"type":"app","name":"Hades II","id":1145350},
        {"type":"app","name":"Hades","id":1145360}]})";
    if (matchSteamApp(L"Hades", search) != 1145360 || matchSteamApp(L"Hades II", search) != 1145350 || matchSteamApp(L"Hades 2", search) != 1145350) return 21;
    if (matchSteamApp(L"Celeste", search) || matchSteamApp(L"Hades", "garbage") || matchSteamApp(L"Hades", R"({"items":[{"type":"sub","name":"Hades","id":9}]})")) return 22;
    if (matchSteamApp(L"Twins", R"({"items":[{"type":"app","name":"Twins","id":5},{"type":"app","name":"Twins","id":6}]})")) return 23;

    // Cached: a found app for good, a miss for a week, a failed request not at all.
    int calls = 0;
    const auto metadata = root / L"metadata";
    HttpResponse reply; reply.status = 200;
    const auto answer = [&](const std::wstring& url, const std::wstring&, size_t) {
        ++calls;
        if (url.find(L"store.steampowered.com/api/storesearch/?term=Hades") == std::wstring::npos) return HttpResponse{};
        return reply;
    };
    reply.status = 503;
    if (steamAppForTitle(L"Hades", metadata, "epic%3Ahades", answer) || calls != 1) return 24;
    reply.status = 200; reply.body.assign(search.begin(), search.end());
    if (steamAppForTitle(L"Hades", metadata, "epic%3Ahades", answer) != 1145360 || calls != 2) return 25;
    if (steamAppForTitle(L"Hades", metadata, "epic%3Ahades", answer) != 1145360 || calls != 2) return 26;
    if (steamAppForTitle(L"Hades III", metadata, "epic%3Ahades3", answer) || calls != 3) return 27;
    if (steamAppForTitle(L"Hades III", metadata, "epic%3Ahades3", answer) || calls != 3) return 28; // The miss is remembered.

    // Nintendo Switch & Switch 2 trailers: platform-aware match, Switch 2 Edition label stripping,
    // rejection of DLC / upgrade packs / bundles / sequels, Title ID resolution and MP4 clip fallback.
    {
        const std::string nintendo_hits = R"json({"hits":[
            {"title":"Xenoblade Chronicles™ 3 – Nintendo Switch™ 2 Edition Upgrade Pack","platformCode":"NINTENDO_SWITCH_2","nsuid":"70050000075214","isUpgrade":true,
             "productGallery":[{"resourceType":"video","publicId":"/store/software/switch2/70050000075214/Video/upgradebad"}]},
            {"title":"Xenoblade Chronicles™ 3 Expansion Pass","platformCode":"NINTENDO_SWITCH","nsuid":"70070000014643","dlcType":"Bundle",
             "productGallery":[{"resourceType":"video","publicId":"/store/software/switch/70070000014643/Video/bundlebad"}]},
            {"title":"Xenoblade Chronicles™ 3","platformCode":"NINTENDO_SWITCH","nsuid":"70010000053336","isUpgrade":false,
             "productGallery":[{"resourceType":"image","publicId":"img1"},{"resourceType":"video","publicId":"/store/software/switch/70010000053336/Video/sw1good"}]},
            {"title":"Xenoblade Chronicles™ 3 – Nintendo Switch™ 2 Edition","platformCode":"NINTENDO_SWITCH_2","nsuid":"70010000122696","isUpgrade":false,
             "productGallery":[{"resourceType":"video","publicId":"/store/software/switch2/70010000122696/Video/sw2good"}]}
        ]})json";
        if (matchNintendoTrailerId(L"Xenoblade Chronicles 3", false, nintendo_hits) !=
            "store/software/switch/70010000053336/Video/sw1good") return 36;
        if (matchNintendoTrailerId(L"Xenoblade Chronicles 3", true, nintendo_hits) !=
            "store/software/switch2/70010000122696/Video/sw2good") return 37;
        if (!matchNintendoTrailerId(L"Xenoblade Chronicles 2", false, nintendo_hits).empty()) return 38;
        if (!matchNintendoTrailerId(L"Twins", false, R"json({"hits":[
            {"title":"Twins","platformCode":"NINTENDO_SWITCH","nsuid":"70010000000001","productGallery":[{"resourceType":"video","publicId":"/store/software/switch/70010000000001/Video/a"}]},
            {"title":"Twins","platformCode":"NINTENDO_SWITCH","nsuid":"70010000000002","productGallery":[{"resourceType":"video","publicId":"/store/software/switch/70010000000002/Video/b"}]}
        ]})json").empty()) return 39;
        if (nintendoTrailerClipUrl("/store/software/switch2/70010000095431/Video/abc123", true) !=
            L"https://assets.nintendo.com/video/upload/so_10,du_30,h_480,ac_none/store/software/switch2/70010000095431/Video/abc123.mp4") return 40;
        if (!nintendoTrailerClipUrl("../bad/Video/abc", true).empty()) return 41;

        // A Switch 2 dump named only by its 16-hex Title ID resolves the title via the European eShop,
        // then fetches the clip (falling back from a short <24 KB so_10 tail to du_30) and trims across stores.
        Game dump;
        dump.kind = GameKind::nonsteam;
        dump.source = GameSource::custom;
        dump.platform = L"Switch 2";
        dump.name = L"[0400C3F00006E000]";
        dump.rom = root / L"roms" / L"switch2" / L"0400C3F00006E800" / L"exefs" / L"main";
        dump.app_id = 777;
        dump.shortcut_id = (uint64_t(777) << 32) | 0x02000000ULL;

        int shop_calls = 0, algolia_calls = 0, clip_calls = 0;
        const auto nintendo_http = [&](const std::wstring& url, const std::wstring&, size_t) {
            HttpResponse r;
            r.status = 200;
            if (url.find(L"searching.nintendo-europe.com") != std::wstring::npos &&
                url.find(L"application_id_s:0400c3f00006e000") != std::wstring::npos) {
                ++shop_calls;
                const std::string body = R"json({"response":{"docs":[{"title":"Mario Kart World","application_id_s":"0400c3f00006e000"}]}})json";
                r.body.assign(body.begin(), body.end());
            } else if (url.find(L"u3b6gr4ua3-dsn.algolia.net") != std::wstring::npos &&
                       url.find(L"Mario%20Kart%20World") != std::wstring::npos) {
                ++algolia_calls;
                const std::string body = R"json({"hits":[{"title":"Mario Kart™ World","platformCode":"NINTENDO_SWITCH_2","nsuid":"70010000095431",
                    "productGallery":[{"resourceType":"video","publicId":"/store/software/switch2/70010000095431/Video/mkwclip"}]}]})json";
                r.body.assign(body.begin(), body.end());
            } else {
                r.status = 404;
            }
            return r;
        };
        const auto media = root / L"media";
        std::filesystem::create_directories(media / L"steam" / L"videos");
        const auto old_steam = media / L"steam" / L"videos" / L"620.clip.mp4";
        {
            std::ofstream out(old_steam, std::ios::binary);
            out << std::string(40 * 1024, 's');
        }
        std::filesystem::last_write_time(old_steam, std::filesystem::file_time_type::clock::now() - std::chrono::hours(2));

        const auto clip_download = [&](const std::wstring& url, const std::filesystem::path& target) {
            ++clip_calls;
            std::filesystem::create_directories(target.parent_path());
            std::string mp4(url.find(L"so_10,") != std::wstring::npos ? 4096 : 30 * 1024, '\0');
            mp4[3] = 20;
            std::memcpy(mp4.data() + 4, "ftyp", 4);
            std::ofstream(target, std::ios::binary).write(mp4.data(), static_cast<std::streamsize>(mp4.size()));
            HttpResponse r;
            r.status = 200;
            return r;
        };
        const auto saved_clip = nintendoTrailerFile(dump, root, media, 35 * 1024, nintendo_http, clip_download);
        if (saved_clip.empty() || !std::filesystem::exists(saved_clip) || shop_calls != 1 || algolia_calls != 1 || clip_calls != 2) return 42;
        if (std::filesystem::exists(old_steam)) return 43; // Shared trailer limit trimmed the older Steam clip.
        // Cached on disk: a second focus touches neither Algolia nor the CDN.
        if (nintendoTrailerFile(dump, root, media, 35 * 1024, nintendo_http, clip_download) != saved_clip ||
            shop_calls != 1 || algolia_calls != 1 || clip_calls != 2) return 44;
    }

    // A Steam game's art comes from Steam's own library cache before SteamGridDB (no request at all:
    // HTTP is cancelled). Steam keeps it in hash-named subfolders; the portrait capsule wins the cover.
    {
        const std::atomic_bool offline{true};
        HttpCancellation no_network(offline);
        constexpr uint32_t app = 4290000001u;
        const auto steam_root = root / L"Steam";
        const auto library = steam_root / L"appcache" / L"librarycache" / std::to_wstring(app);
        const auto put = [&](const wchar_t* folder, const wchar_t* name, const std::vector<uint8_t>& bytes) {
            std::filesystem::create_directories(library / folder);
            return atomic_file::write(library / folder / name, std::span<const uint8_t>(bytes));
        };
        const auto capsule = jpegFixture(300, 450, 1), old_capsule = jpegFixture(300, 450, 2), header = jpegFixture(460, 215, 3),
                   hero = jpegFixture(1920, 620, 4), logo = pngFixture(90, 640, 150, true);
        if (capsule.empty() || logo.empty() || !put(L"aa", L"library_600x900.jpg", capsule) || !put(L"bb", L"library_capsule.jpg", old_capsule) ||
            !put(L"cc", L"header.jpg", header) || !put(L"dd", L"library_hero.jpg", hero) || !put(L"ee", L"logo.png", logo)) return 29;
        const auto covers = root / L"steam-covers";
        std::filesystem::create_directories(covers);
        if (coverForSteamApp(app, L"Local Art", covers, steam_root) != capsule) return 30;
        if (posterForSteamApp(app, L"Local Art", covers, steam_root) != capsule) return 31;
        if (landscapeForSteamApp(app, L"Local Art", covers, steam_root) != header) return 32;
        if (heroForSteamApp(app, L"Local Art", covers, steam_root) != hero) return 33;
        if (logoForSteamApp(app, L"Local Art", covers, steam_root) != logo) return 34;
        if (!std::filesystem::exists(covers / (std::to_wstring(app) + L".v2.img"))) return 35; // Cached like a download.
    }

    std::filesystem::remove_all(root, ec);
    CoUninitialize();
    std::cout << "Background art, cache release, background crop and trailer match passed\n";
    return 0;
}
