#include "cover_cache.hpp"
#include "../store/steam_app_match.hpp"
#include "battlenet_art.hpp"
#include "gog_art.hpp"
#include "image_compact.hpp"
#include "screenscraper_art.hpp"
#include "steamgriddb_search.hpp"
#include "../net/http.hpp"
#include "../net/json.hpp"
#include "../platform/user_config.hpp"
#include "../platform/atomic_file.hpp"
#include "../platform/text_encoding.hpp"

#ifdef _WIN32
#include <Windows.h>
#endif

#include <algorithm>
#include <cctype>
#include <fstream>
#include <mutex>
#include <string>
#include <unordered_map>
#include <atomic>

namespace {
bool atomicBytes(const std::filesystem::path& file, const uint8_t* bytes, size_t size);
thread_local bool retryable_artwork_error = false;
std::atomic<ULONGLONG> sgdb_resume_at{0};
HttpResponse request(const std::wstring& url, bool authorized, size_t max_bytes) {
    std::wstring headers = L"Accept: application/json, image/jpeg, image/png\r\n";
    if (authorized) {
        if (GetTickCount64() < sgdb_resume_at.load()) { retryable_artwork_error = true; return {}; }
        const auto key = userSecret(L"STEAMGRIDDB_API_KEY", L"steamgriddb_api_key.txt");
        if (key.empty()) return {};
        headers += L"Authorization: Bearer " + key + L"\r\n";
    }
    auto reply = httpGet(url, headers, max_bytes);
    if (authorized && reply.status == 429) {
        const auto until = GetTickCount64() + static_cast<ULONGLONG>(std::clamp(reply.retry_after_seconds ? reply.retry_after_seconds : 60u, 1u, 3600u)) * 1000;
        auto previous = sgdb_resume_at.load();
        while (previous < until && !sgdb_resume_at.compare_exchange_weak(previous, until)) {}
    }
    if (!reply.status || reply.status == 429 || reply.status >= 500) retryable_artwork_error = true;
    return reply;
}
std::vector<uint8_t> get(const std::wstring& url, bool authorized, size_t max_bytes) { return request(url, authorized, max_bytes).body; }

// Only the first asset of an SGDB list response is needed: data[0].url (or .thumb).
std::wstring firstUrl(const std::vector<uint8_t>& body, std::string_view key = "url") {
    const auto parsed = json::parse(std::string_view(reinterpret_cast<const char*>(body.data()), body.size()));
    const auto* data = parsed ? parsed->get("data") : nullptr;
    if (!data || data->type != json::Value::Type::array || data->elements.empty()) return {};
    const auto* url = data->elements.front().get(key);
    if (!url || url->type != json::Value::Type::string || !url->text.starts_with("https://")) return {};
    return text_encoding::wide(url->text);
}

bool isImage(const std::vector<uint8_t>& bytes) {
    if (bytes.size() < 128) return false;
    return (bytes[0] == 0xff && bytes[1] == 0xd8) ||
           (bytes[0] == 0x89 && bytes[1] == 'P' && bytes[2] == 'N' && bytes[3] == 'G');
}

// A cached or local image in one read (size known up front, no byte-by-byte growth); empty
// when missing, larger than `limit` or not a JPEG/PNG. Downloads are capped at 10 MiB (backgrounds
// at 32 MiB) and chosen variants at 20 MiB, so the default never rejects a file this cache wrote.
std::vector<uint8_t> readImage(const std::filesystem::path& file, size_t limit = 32 * 1024 * 1024) {
    std::ifstream input(file, std::ios::binary | std::ios::ate);
    if (!input) return {};
    const auto size = static_cast<long long>(input.tellg());
    if (size <= 0 || static_cast<unsigned long long>(size) > limit) return {};
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    input.seekg(0);
    if (!input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())) || !isImage(bytes)) return {};
    return bytes;
}

uint32_t gameIdForTitle(uint32_t app_id, const std::wstring& title,
                        const std::filesystem::path& cache) {
    if (title.empty() || title.starts_with(L"AppID ") || placeholderGameName(title)) return 0;
    static std::mutex mutex;
    static std::unordered_map<std::wstring, uint32_t> known;
    // Titles SteamGridDB had no match for: each kind of artwork (cover, poster, hero, logo, …)
    // asked again would repeat the same searches; for half an hour they are not.
    static std::unordered_map<std::wstring, ULONGLONG> missed;
    constexpr ULONGLONG miss_lifetime = 30 * 60 * 1000;
    const auto mapping = cache / (std::to_wstring(app_id) + L".sgdbid");
    // The title is part of the in-memory key: a renamed user-added game must be matched
    // again (its stale mapping file is removed together with its artwork).
    const auto key = mapping.wstring() + L'\n' + title;
    {
        std::lock_guard lock(mutex);
        if (const auto existing = known.find(key); existing != known.end()) return existing->second;
        if (const auto miss = missed.find(key); miss != missed.end()) {
            if (GetTickCount64() - miss->second < miss_lifetime) return 0;
            missed.erase(miss);
        }
    }
    uint32_t result = 0;
    {
        std::ifstream saved(mapping);
        if (saved >> result && result) {
            std::lock_guard lock(mutex);
            known[key] = result;
            return result;
        }
    }
    const auto api_key = userSecret(L"STEAMGRIDDB_API_KEY", L"steamgriddb_api_key.txt");
    const bool failed_before = retryable_artwork_error;
    retryable_artwork_error = false;
    const auto match = resolveGridGame(title, api_key, [](const auto& url, const auto&, size_t limit) { return request(url, true, limit); });
    const bool unreachable = retryable_artwork_error; // A rate limit or no connection: not an answer.
    retryable_artwork_error = failed_before || unreachable;
    result = match.source.id;
    if (!result && !unreachable && !api_key.empty()) {
        std::lock_guard lock(mutex);
        missed[key] = GetTickCount64();
    }
    if (result) {
        {
            std::lock_guard lock(mutex);
            known[key] = result;
        }
        atomic_file::write(mapping, std::to_string(result));
    }
    return result;
}

std::vector<uint8_t> grid(uint32_t id, const wchar_t* source, const wchar_t* dimensions) {
    const auto endpoint = L"https://www.steamgriddb.com/api/v2/grids/" + std::wstring(source) +
                          L"/" + std::to_wstring(id) + L"?dimensions=" + dimensions + L"&limit=1";
    const auto url = firstUrl(get(endpoint, true, 512 * 1024));
    if (url.empty()) return {};
    return get(url, false, 10 * 1024 * 1024);
}

std::vector<uint8_t> asset(uint32_t id, const wchar_t* source, const wchar_t* kind) {
    const auto endpoint = L"https://www.steamgriddb.com/api/v2/" + std::wstring(kind) + L"/" +
                          source + L"/" + std::to_wstring(id) + L"?limit=1";
    const auto response = get(endpoint, true, 512 * 1024);
    auto url = firstUrl(response);
    if (url.empty()) url = firstUrl(response, "thumb");
    if (url.empty()) return {};
    return get(url, false, 10 * 1024 * 1024);
}

// A background: the sharpest of SteamGridDB's best rated heroes (preferredHeroUrl), JPEG or PNG only
// (isImage). A 3840×1240 PNG can pass 10 MiB; the cache keeps it re-encoded as a JPEG.
std::vector<uint8_t> heroAsset(uint32_t id, const wchar_t* source) {
    const auto endpoint = L"https://www.steamgriddb.com/api/v2/heroes/" + std::wstring(source) + L"/" + std::to_wstring(id) +
                          L"?mimes=image/png,image/jpeg&limit=12";
    const auto response = get(endpoint, true, 1024 * 1024);
    const auto url = preferredHeroUrl(std::string_view(reinterpret_cast<const char*>(response.data()), response.size()));
    if (url.empty()) return {};
    return get(url, false, 32 * 1024 * 1024);
}

std::vector<uint8_t> firstStoreScreenshot(uint32_t app_id) {
    const auto id = std::to_wstring(app_id);
    const auto response = get(L"https://store.steampowered.com/api/appdetails?appids=" + id +
                              L"&filters=screenshots", false, 512 * 1024);
    const auto parsed = json::parse(std::string_view(reinterpret_cast<const char*>(response.data()), response.size()));
    if (!parsed) return {};
    const auto* entry = parsed->get(std::to_string(app_id));
    const auto* success = entry ? entry->get("success") : nullptr;
    if (!success || success->type != json::Value::Type::boolean || !success->flag) return {};
    const auto* data = entry->get("data");
    const auto* screenshots = data ? data->get("screenshots") : nullptr;
    if (!screenshots || screenshots->type != json::Value::Type::array || screenshots->elements.empty()) return {};
    const auto& first = screenshots->elements.front();
    for (const auto* field : {"path_full", "path_thumbnail"}) {
        const auto* value = first.get(field);
        if (!value || !value->text.starts_with("https://")) continue;
        const std::wstring url(value->text.begin(), value->text.end());
        auto image = get(url, false, 10 * 1024 * 1024);
        if (isImage(image)) return image;
    }
    return {};
}

// Steam's own copy of the app's library art (librarycache/<AppID>/<hash>/<name>), the first of `names`
// found, in their order. Read before SteamGridDB: it is already on disk for owned games, while each
// SGDB lookup waits for the shared 1.1 s spacing. Never downloads.
std::vector<uint8_t> localSteamFile(uint32_t app_id, const std::filesystem::path& steam_root,
                                    std::initializer_list<std::wstring_view> names) {
    if (steam_root.empty()) return {};
    std::vector<std::pair<size_t, std::filesystem::path>> found;
    std::error_code ec;
    std::filesystem::recursive_directory_iterator folder(steam_root / L"appcache" / L"librarycache" / std::to_wstring(app_id), ec);
    for (; !ec && folder != std::filesystem::recursive_directory_iterator{}; folder.increment(ec)) {
        if (folder.depth() > 2) { folder.disable_recursion_pending(); continue; }
        const auto name = folder->path().filename().wstring();
        const auto at = std::find(names.begin(), names.end(), std::wstring_view(name));
        if (at != names.end()) found.emplace_back(static_cast<size_t>(at - names.begin()), folder->path());
    }
    std::sort(found.begin(), found.end());
    for (const auto& [rank, path] : found) {
        (void)rank;
        if (auto image = readImage(path, 8 * 1024 * 1024); !image.empty()) return image;
    }
    return {};
}

std::vector<uint8_t> localSteamCover(uint32_t app_id, const std::filesystem::path& steam_root) {
    const auto folder = steam_root / L"appcache" / L"librarycache" / std::to_wstring(app_id);
    std::error_code ec;
    std::filesystem::recursive_directory_iterator iterator(folder, ec);
    if (ec) return {};
    std::vector<std::pair<int, std::filesystem::path>> candidates;
    for (const auto& entry : iterator) {
        if (iterator.depth() > 2) { iterator.disable_recursion_pending(); continue; }
        if (!entry.is_regular_file(ec) || ec) continue;
        const auto filename = entry.path().filename().wstring();
        const auto extension = entry.path().extension().wstring();
        if (extension != L".jpg" && extension != L".jpeg" && extension != L".png") continue;
        int priority = 6;
        if (filename == L"library_600x900.jpg") priority = 0;
        else if (filename == L"library_capsule.jpg") priority = 1;
        else if (filename == L"library_header.jpg") priority = 2;
        else if (filename == L"library_hero.jpg") priority = 3;
        else if (filename == L"icon.jpg") priority = 8;
        candidates.emplace_back(priority, entry.path());
    }
    std::sort(candidates.begin(), candidates.end());
    for (const auto& [priority, path] : candidates) {
        (void)priority;
        if (auto image = readImage(path, 8 * 1024 * 1024); !image.empty()) return image;
    }
    return {};
}
} // namespace

std::wstring preferredHeroUrl(std::string_view body) {
    const auto parsed = json::parse(body);
    const auto* data = parsed ? parsed->get("data") : nullptr;
    if (!data || data->type != json::Value::Type::array) return {};
    const auto url = [](const json::Value& item) -> std::wstring {
        const auto* value = item.get("url");
        return value && value->type == json::Value::Type::string && value->text.starts_with("https://") ? text_encoding::wide(value->text) : std::wstring{};
    };
    std::wstring first;
    for (const auto& item : data->elements) {
        auto link = url(item);
        if (link.empty()) continue;
        const auto* width = item.get("width");
        if (width && width->type == json::Value::Type::number && width->numeric >= 3000) return link;
        if (first.empty()) first = std::move(link);
    }
    return first;
}

bool consumeArtworkRetryableError() {
    const bool value = retryable_artwork_error;
    retryable_artwork_error = false;
    return value;
}
uint32_t artworkRateLimitSeconds() {
    const auto deadline = sgdb_resume_at.load(), now = GetTickCount64();
    return std::max(steamGridDbWaitSeconds(), deadline > now ? static_cast<uint32_t>((deadline - now + 999) / 1000) : 0u);
}

std::filesystem::path gameArtworkDirectory(const Game& game, const std::filesystem::path& cache) {
    if (steamArtwork(game)) return cache;
    return cache.parent_path() / L"stores" / sourceSlug(game.source) / gameCacheKey(game);
}

std::filesystem::path gameCoverPath(const Game& game, const std::filesystem::path& cache) {
    return gameArtworkPath(game, ArtworkKind::cover, cache);
}
std::filesystem::path gameArtworkPath(const Game& game, ArtworkKind kind, const std::filesystem::path& cache) {
    const auto chosen = selectedArtworkId(game, kind, cache);
    if (chosen) return artworkVariantPath(game, kind, chosen, cache);
    if (steamArtwork(game)) {
        const auto folder = kind == ArtworkKind::cover ? cache : cache.parent_path() /
            (kind == ArtworkKind::logo ? L"logos" : kind == ArtworkKind::poster ? L"posters" :
             kind == ArtworkKind::landscape ? L"landscapes" : kind == ArtworkKind::disc ? L"discs" : L"heroes");
        return folder / (std::to_wstring(game.app_id) + (kind == ArtworkKind::hero ? L".v2.hd.img" : L".v2.img"));
    }
    return gameArtworkDirectory(game, cache) / (std::wstring(artworkKindSlug(kind)) + L".v2.img");
}
std::filesystem::path cachedArtworkFile(const Game& game, ArtworkKind kind, const std::filesystem::path& cache, bool rom_first) {
    const auto valid = [](const std::filesystem::path& file) {
        std::ifstream input(file, std::ios::binary);
        std::vector<uint8_t> header(128);
        return input.read(reinterpret_cast<char*>(header.data()), static_cast<std::streamsize>(header.size())) && isImage(header);
    };
    if (const auto chosen = selectedArtworkId(game, kind, cache)) {
        if (auto file = artworkVariantPath(game, kind, chosen, cache); valid(file)) return file;
    }
    // The ES-DE/ScreenScraper copies esde_art.cpp keeps (emulation/covers|backgrounds|logos).
    const auto emu = game.kind == GameKind::nonsteam && (kind == ArtworkKind::cover || kind == ArtworkKind::hero || kind == ArtworkKind::logo) ?
        cache.parent_path() / L"emulation" / (kind == ArtworkKind::cover ? L"covers" : kind == ArtworkKind::hero ? L"backgrounds" : L"logos") /
            (std::to_wstring(game.app_id) + L".img") :
        std::filesystem::path{};
    const bool emu_first = !emu.empty() && (kind == ArtworkKind::hero || rom_first);
    if (emu_first && valid(emu)) return emu;
    if (auto file = gameArtworkPath(game, kind, cache); valid(file)) return file;
    if (!emu.empty() && !emu_first && valid(emu)) return emu;
    return {};
}
bool cachedArtworkAvailable(const Game& game, ArtworkKind kind, const std::filesystem::path& cache, bool rom_first) {
    return !cachedArtworkFile(game, kind, cache, rom_first).empty();
}
std::vector<uint8_t> cachedArtwork(const Game& game, ArtworkKind kind, const std::filesystem::path& cache, bool rom_first) {
    const auto file = cachedArtworkFile(game, kind, cache, rom_first);
    return file.empty() ? std::vector<uint8_t>{} : readImage(file);
}

std::vector<uint8_t> artworkForGame(const Game& game, ArtworkKind kind,
                                  const std::filesystem::path& cache,
                                  const std::filesystem::path& steam_root) {
    if (!isGame(game) || gameKey(game).empty()) return {};
    if (auto chosen = selectedArtwork(game, kind, cache); !chosen.empty()) return chosen;
    if (kind == ArtworkKind::disc) return discArtwork(game, cache).bytes;
    if (steamArtwork(game)) {
        switch (kind) {
        case ArtworkKind::cover: return coverForSteamApp(game.app_id, game.name, cache, steam_root);
        case ArtworkKind::logo: return logoForSteamApp(game.app_id, game.name, cache, steam_root);
        case ArtworkKind::hero: return heroForSteamApp(game.app_id, game.name, cache, steam_root);
        case ArtworkKind::poster: return posterForSteamApp(game.app_id, game.name, cache, steam_root);
        case ArtworkKind::landscape: return landscapeForSteamApp(game.app_id, game.name, cache, steam_root);
        case ArtworkKind::disc: break; // Discs have no Steam variant: the game's own folder below.
        }
    }
    const auto directory = gameArtworkDirectory(game, cache);
    const auto file = directory / (std::wstring(artworkKindSlug(kind)) + L".v2.img");
    if (auto bytes = readImage(file, kind == ArtworkKind::hero ? 32 * 1024 * 1024 : 10 * 1024 * 1024); !bytes.empty()) return bytes;
    // Store/shortcut IDs are never Steam AppIDs. Only a separately verified artwork
    // mapping may borrow Steam art; this never changes launch/install actions.
    const auto steamGridDb = [&]() -> std::vector<uint8_t> {
        auto source = selectedArtworkSource(game, cache);
        if (!source.id) source = {gameIdForTitle(0, artworkTitle(game), directory), false};
        if (!source.id) return {};
        const auto route = source.steam ? L"steam" : L"game";
        std::vector<uint8_t> found;
        if (kind == ArtworkKind::cover) {
            for (const auto* dimensions : {L"512x512", L"600x900"}) {
                found = grid(source.id, route, dimensions);
                if (isImage(found)) break;
            }
        } else if (kind == ArtworkKind::poster) found = grid(source.id, route, L"600x900");
        else if (kind == ArtworkKind::landscape) {
            for (const auto* dimensions : {L"920x430", L"460x215"}) {
                found = grid(source.id, route, dimensions);
                if (isImage(found)) break;
            }
        } else found = kind == ArtworkKind::logo ? asset(source.id, route, L"logos") : heroAsset(source.id, route);
        return found;
    };
    // The store's own art (its CDN, no key and no rate limit): Epic's catalog, Ubisoft Connect's, GOG's,
    // EA's or Battle.net's. The tall box art is the poster (Epic's first: it is large), and the cover
    // where SteamGridDB has none; the wide art and the logo stand in for SteamGridDB's background,
    // landscape tile and logo. Battle.net's (its client's catalog, no wide art) and GOG's (its public
    // product API) are looked up here.
    const auto storeArt = [&]() -> std::vector<uint8_t> {
        auto url = kind == ArtworkKind::poster || kind == ArtworkKind::cover ? game.store_tall_art :
                   kind == ArtworkKind::logo ? game.store_logo_art : game.store_wide_art;
        if (url.empty() && game.source == GameSource::battlenet && kind != ArtworkKind::hero && kind != ArtworkKind::landscape)
            url = battlenetArtUrl(game.source_id, kind == ArtworkKind::logo, cache);
        if (url.empty() && game.source == GameSource::gog) {
            const auto art = gogArt(game.source_id, cache);
            url = kind == ArtworkKind::poster || kind == ArtworkKind::cover ? art.tall : kind == ArtworkKind::logo ? art.logo : art.wide;
        }
        if (!url.starts_with("https://")) return {};
        auto bytes = get(text_encoding::wide(url), false, 10 * 1024 * 1024);
        return isImage(bytes) ? bytes : std::vector<uint8_t>{};
    };
    // Games added by hand and DRM-free folders have no store of their own: without a SteamGridDB match (or
    // key), the same game's Steam store art, the app matched by its title (steam_app_match.hpp: keyless,
    // only an unambiguous match, cached; the title is part of the key so a renamed game is matched again).
    const auto steamStoreArt = [&]() -> std::vector<uint8_t> {
        if (!localFileGame(game.source)) return {};
        const auto title = artworkTitle(game);
        const auto app = steamAppForTitle(title, cache.parent_path() / L"metadata",
                                          gameCacheKey(game) + "." + std::to_string(std::hash<std::wstring>{}(title)));
        if (!app) return {};
        const auto files = kind == ArtworkKind::poster ? std::vector<const wchar_t*>{L"library_600x900_2x.jpg", L"library_600x900.jpg"} :
                           kind == ArtworkKind::cover ? std::vector<const wchar_t*>{L"library_600x900_2x.jpg", L"library_600x900.jpg", L"header.jpg"} :
                           kind == ArtworkKind::landscape ? std::vector<const wchar_t*>{L"header.jpg", L"capsule_616x353.jpg"} :
                           kind == ArtworkKind::hero ? std::vector<const wchar_t*>{L"library_hero.jpg"} :
                           kind == ArtworkKind::logo ? std::vector<const wchar_t*>{L"logo.png"} : std::vector<const wchar_t*>{};
        for (const auto* name : files) {
            auto bytes = get(L"https://shared.fastly.steamstatic.com/store_item_assets/steam/apps/" + std::to_wstring(app) + L"/" + name,
                             false, 10 * 1024 * 1024);
            if (isImage(bytes)) return bytes;
        }
        return {};
    };
    std::vector<uint8_t> image;
    if (kind == ArtworkKind::poster && game.source == GameSource::epic) image = storeArt();
    if (!isImage(image)) image = steamGridDb();
    if (!isImage(image) && !(kind == ArtworkKind::poster && game.source == GameSource::epic)) image = storeArt();
    if (!isImage(image)) image = steamStoreArt();
    if (!isImage(image)) return {};
    if (!atomicBytes(file, image.data(), image.size())) { retryable_artwork_error = true; return {}; }
    return image;
}

std::vector<uint8_t> storeScreenshotForSteamApp(uint32_t app_id) {
    return firstStoreScreenshot(app_id);
}

std::filesystem::path coverCacheDirectory() {
    return userConfigDirectory() / L"covers";
}

std::vector<uint8_t> coverForSteamApp(uint32_t app_id, const std::wstring& title,
                                      const std::filesystem::path& cache,
                                      const std::filesystem::path& steam_root) {
    const auto file = cache / (std::to_wstring(app_id) + L".v2.img");
    if (auto bytes = readImage(file); !bytes.empty()) return bytes;
    const auto id = std::to_wstring(app_id);
    // Steam's portrait capsule first; its wide header (460×215) is a poorer cover than SGDB's grids.
    auto image = localSteamFile(app_id, steam_root, {L"library_600x900.jpg", L"library_capsule.jpg"});
    if (!isImage(image))
        for (const auto* dimensions : {L"512x512", L"600x900"}) {
            image = grid(app_id, L"steam", dimensions);
            if (isImage(image)) break;
        }
    if (!isImage(image)) {
        // SteamGridDB game IDs are separate from Steam AppIDs; new entries may
        // have artwork but no Steam mapping yet. Match the actual manifest name.
        const auto game_id = gameIdForTitle(app_id, title, cache);
        if (game_id) for (const auto* dimensions : {L"512x512", L"600x900"}) {
            image = grid(game_id, L"game", dimensions);
            if (isImage(image)) break;
        }
    }
    if (!isImage(image)) image = localSteamCover(app_id, steam_root);
    if (!isImage(image))
        for (const auto* file_name : {L"library_600x900.jpg", L"header.jpg", L"capsule_616x353.jpg"}) {
            image = get(L"https://shared.fastly.steamstatic.com/store_item_assets/steam/apps/" +
                        id + L"/" + file_name, false, 4 * 1024 * 1024);
            if (isImage(image)) break;
        }
    if (!isImage(image))
        image = get(L"https://cdn.cloudflare.steamstatic.com/steam/apps/" + id + L"/header.jpg",
                    false, 4 * 1024 * 1024);
    if (!isImage(image)) image = heroForSteamApp(app_id, title, cache, steam_root);
    if (!isImage(image)) return {};
    if (!atomicBytes(file, image.data(), image.size())) { retryable_artwork_error = true; return {}; }
    return image;
}

std::vector<uint8_t> logoForSteamApp(uint32_t app_id, const std::wstring& title,
                                     const std::filesystem::path& cache,
                                     const std::filesystem::path& steam_root) {
    const auto logos = cache.parent_path() / L"logos";
    const auto file = logos / (std::to_wstring(app_id) + L".v2.img");
    if (auto bytes = readImage(file); !bytes.empty()) return bytes;
    auto image = localSteamFile(app_id, steam_root, {L"logo.png"});
    if (!isImage(image)) image = asset(app_id, L"steam", L"logos");
    if (!isImage(image)) {
        const auto game_id = gameIdForTitle(app_id, title, cache);
        if (game_id) image = asset(game_id, L"game", L"logos");
    }
    if (!isImage(image)) return {};
    if (!atomicBytes(file, image.data(), image.size())) { retryable_artwork_error = true; return {}; }
    return image;
}

std::vector<uint8_t> heroForSteamApp(uint32_t app_id, const std::wstring& title,
                                     const std::filesystem::path& cache,
                                     const std::filesystem::path& steam_root) {
    const auto heroes = cache.parent_path() / L"heroes";
    const auto file = heroes / (std::to_wstring(app_id) + L".v2.hd.img");
    if (auto bytes = readImage(file); !bytes.empty()) return bytes;
    // Steam's library hero (1920×620) is already on disk; otherwise SGDB at full resolution
    // (preferably 3840 px): the decoder keeps what covers the screen.
    auto image = localSteamFile(app_id, steam_root, {L"library_hero.jpg"});
    if (!isImage(image)) image = heroAsset(app_id, L"steam");
    if (!isImage(image)) {
        const auto game_id = gameIdForTitle(app_id, title, cache);
        if (game_id) image = heroAsset(game_id, L"game");
    }
    if (!isImage(image)) image = storeScreenshotForSteamApp(app_id);
    if (!isImage(image)) return {};
    if (!atomicBytes(file, image.data(), image.size())) { retryable_artwork_error = true; return {}; }
    return image;
}

std::vector<uint8_t> posterForSteamApp(uint32_t app_id, const std::wstring& title,
                                       const std::filesystem::path& cache,
                                       const std::filesystem::path& steam_root) {
    const auto file = cache.parent_path() / L"posters" / (std::to_wstring(app_id) + L".v2.img");
    if (auto bytes = readImage(file); !bytes.empty()) return bytes;
    // Steam keeps the official vertical capsule of most owned games locally.
    auto image = localSteamFile(app_id, steam_root, {L"library_600x900.jpg", L"library_capsule.jpg"});
    if (!isImage(image)) image = grid(app_id, L"steam", L"600x900");
    if (!isImage(image)) {
        const auto game_id = gameIdForTitle(app_id, title, cache);
        if (game_id) image = grid(game_id, L"game", L"600x900");
    }
    if (!isImage(image))
        image = get(L"https://shared.fastly.steamstatic.com/store_item_assets/steam/apps/" + std::to_wstring(app_id) +
                    L"/library_600x900.jpg", false, 4 * 1024 * 1024);
    if (!isImage(image)) return {};
    if (!atomicBytes(file, image.data(), image.size())) { retryable_artwork_error = true; return {}; }
    return image;
}

std::vector<uint8_t> landscapeForSteamApp(uint32_t app_id, const std::wstring& title,
                                          const std::filesystem::path& cache,
                                          const std::filesystem::path& steam_root) {
    const auto file = cache.parent_path() / L"landscapes" / (std::to_wstring(app_id) + L".v2.img");
    if (auto bytes = readImage(file); !bytes.empty()) return bytes;
    // Steam keeps the store header capsule of owned games in its library cache.
    auto image = localSteamFile(app_id, steam_root, {L"header.jpg", L"library_header.jpg"});
    if (!isImage(image))
        for (const auto* dimensions : {L"920x430", L"460x215"}) {
            image = grid(app_id, L"steam", dimensions);
            if (isImage(image)) break;
        }
    if (!isImage(image)) {
        const auto game_id = gameIdForTitle(app_id, title, cache);
        if (game_id) for (const auto* dimensions : {L"920x430", L"460x215"}) {
            image = grid(game_id, L"game", dimensions);
            if (isImage(image)) break;
        }
    }
    if (!isImage(image))
        for (const auto* file_name : {L"header.jpg", L"capsule_616x353.jpg"}) {
            image = get(L"https://shared.fastly.steamstatic.com/store_item_assets/steam/apps/" + std::to_wstring(app_id) +
                        L"/" + file_name, false, 4 * 1024 * 1024);
            if (isImage(image)) break;
        }
    if (!isImage(image)) return {};
    if (!atomicBytes(file, image.data(), image.size())) { retryable_artwork_error = true; return {}; }
    return image;
}

const wchar_t* artworkKindSlug(ArtworkKind kind) {
    return kind == ArtworkKind::cover ? L"cover" : kind == ArtworkKind::hero ? L"hero" :
           kind == ArtworkKind::poster ? L"poster" : kind == ArtworkKind::landscape ? L"landscape" : kind == ArtworkKind::disc ? L"disc" : L"logo";
}

namespace {
std::filesystem::path selectionFolder(const Game& game, const std::filesystem::path& cache) {
    return cache.parent_path() / L"artwork" / L"selections" / gameCacheKey(game);
}
bool atomicBytes(const std::filesystem::path& file, const uint8_t* bytes, size_t size) {
    // Artwork is kept at the size the launcher draws (image_compact.hpp); other files as they are.
    return writeCachedImage(file, std::span(bytes, size));
}
}

std::filesystem::path artworkVariantPath(const Game& game, ArtworkKind kind, uint32_t asset_id,
                                         const std::filesystem::path& cache) {
    return selectionFolder(game, cache) / artworkKindSlug(kind) / (std::to_wstring(asset_id) + L".img");
}

std::filesystem::path artworkAnimationPath(const Game& game, ArtworkKind kind, uint32_t asset_id,
                                           const std::filesystem::path& cache) {
    auto file = artworkVariantPath(game, kind, asset_id, cache);
    file.replace_extension(L".webp");
    return file;
}

std::filesystem::path selectedArtworkAnimation(const Game& game, ArtworkKind kind, const std::filesystem::path& cache) {
    const auto id = selectedArtworkId(game, kind, cache);
    if (!id) return {};
    auto file = artworkAnimationPath(game, kind, id, cache);
    std::error_code error;
    const auto size = std::filesystem::file_size(file, error);
    return !error && size >= 30 ? file : std::filesystem::path{};
}

uint32_t selectedArtworkId(const Game& game, ArtworkKind kind, const std::filesystem::path& cache) {
    if (!isGame(game) || gameKey(game).empty()) return 0;
    std::ifstream input(selectionFolder(game, cache) / (std::wstring(artworkKindSlug(kind)) + L".selected"));
    unsigned version = 0;
    uint32_t id = 0;
    if (!(input >> version >> id) || version != 1 || !id) return 0;
    std::error_code ec;
    const auto size = std::filesystem::file_size(artworkVariantPath(game, kind, id, cache), ec);
    return !ec && size >= 128 && size <= 20 * 1024 * 1024 ? id : 0;
}

GridSource selectedArtworkSource(const Game& game, const std::filesystem::path& cache) {
    if (!isGame(game) || gameKey(game).empty()) return {};
    std::ifstream input(selectionFolder(game, cache) / L"source.selected");
    unsigned version = 0, steam = 0; uint32_t id = 0;
    if (!(input >> version >> steam >> id) || version != 1 || steam > 1 || !id) return {};
    return {id, steam == 1};
}
bool selectArtworkSource(const Game& game, GridSource source, const std::filesystem::path& cache) {
    return isGame(game) && !gameKey(game).empty() && source.id &&
        atomic_file::write(selectionFolder(game, cache) / L"source.selected",
                           "1 " + std::to_string(source.steam ? 1 : 0) + ' ' + std::to_string(source.id) + '\n');
}

std::vector<uint8_t> selectedArtwork(const Game& game, ArtworkKind kind, const std::filesystem::path& cache) {
    const auto id = selectedArtworkId(game, kind, cache);
    return id ? readImage(artworkVariantPath(game, kind, id, cache)) : std::vector<uint8_t>{};
}

bool cacheArtworkVariant(const Game& game, ArtworkKind kind, uint32_t asset_id,
                         const std::vector<uint8_t>& bytes, const std::filesystem::path& cache) {
    return isGame(game) && !gameKey(game).empty() && asset_id && bytes.size() <= 20 * 1024 * 1024 && isImage(bytes) &&
           atomicBytes(artworkVariantPath(game, kind, asset_id, cache), bytes.data(), bytes.size());
}

std::vector<uint32_t> localArtworkIds(const Game& game, ArtworkKind kind, const std::filesystem::path& cache) {
    std::vector<std::pair<std::filesystem::file_time_type, uint32_t>> found;
    if (!isGame(game) || gameKey(game).empty()) return {};
    std::error_code error;
    const auto folder = selectionFolder(game, cache) / artworkKindSlug(kind);
    for (std::filesystem::directory_iterator it(folder, error), end; !error && it != end; it.increment(error)) {
        const auto& path = it->path();
        if (path.extension() != L".img") continue;
        const auto stem = path.stem().wstring();
        if (stem.empty() || stem.size() > 10 || stem.find_first_not_of(L"0123456789") != stem.npos) continue;
        const auto id = static_cast<uint64_t>(std::stoull(stem));
        if (id < local_artwork_first_id || id > 0xFFFFFFFFull) continue;
        std::error_code stat;
        const auto size = it->file_size(stat);
        if (stat || size < 128 || size > 20 * 1024 * 1024) continue;
        found.emplace_back(it->last_write_time(stat), static_cast<uint32_t>(id));
    }
    std::sort(found.begin(), found.end(), [](const auto& a, const auto& b) { return a.first != b.first ? a.first > b.first : a.second < b.second; });
    std::vector<uint32_t> ids;
    for (const auto& item : found) ids.push_back(item.second);
    return ids;
}

bool hasLocalArtwork(const Game& game, const std::filesystem::path& cache) {
    for (const auto kind : {ArtworkKind::cover, ArtworkKind::hero, ArtworkKind::logo, ArtworkKind::poster, ArtworkKind::landscape, ArtworkKind::disc})
        if (!localArtworkIds(game, kind, cache).empty()) return true;
    return false;
}

bool removeGameArtwork(const Game& game, const std::filesystem::path& cache, bool selections) {
    // Steam artwork shares one cache folder; only per-game folders can be removed.
    if (!isGame(game) || gameKey(game).empty() || steamArtwork(game)) return false;
    std::error_code error;
    std::filesystem::remove_all(gameArtworkDirectory(game, cache), error);
    bool removed = !error;
    if (selections) {
        std::filesystem::remove_all(selectionFolder(game, cache), error);
        removed = removed && !error;
    }
    return removed;
}

bool selectArtworkVariant(const Game& game, ArtworkKind kind, uint32_t asset_id,
                          const std::filesystem::path& cache) {
    if (!isGame(game) || gameKey(game).empty() || !asset_id) return false;
    std::ifstream image(artworkVariantPath(game, kind, asset_id, cache), std::ios::binary);
    uint8_t signature[128]{};
    if (!image.read(reinterpret_cast<char*>(signature), sizeof(signature)) ||
        !isImage(std::vector<uint8_t>(std::begin(signature), std::end(signature)))) return false;
    const auto value = "1 " + std::to_string(asset_id) + '\n';
    return atomicBytes(selectionFolder(game, cache) / (std::wstring(artworkKindSlug(kind)) + L".selected"),
                       reinterpret_cast<const uint8_t*>(value.data()), value.size());
}
