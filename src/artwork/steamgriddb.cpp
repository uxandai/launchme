#include "steamgriddb.hpp"
#include "../i18n/i18n.hpp"
#include "../net/json.hpp"
#include "../platform/text_encoding.hpp"
#include "animated_image.hpp"
#include "image_codec.hpp"
#include <cmath>
#include <fstream>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

namespace {
using text_encoding::wide;
uint32_t number(const json::Value* value) {
    return value && value->type == json::Value::Type::number && std::isfinite(value->numeric) &&
           value->numeric > 0 && value->numeric <= UINT32_MAX && std::floor(value->numeric) == value->numeric ?
           static_cast<uint32_t>(value->numeric) : 0;
}
std::wstring url(const json::Value* value) {
    const auto text = value ? value->stringOr() : std::string{};
    if (!text.starts_with("https://") || text.size() > 2048 || text.find_first_of("\r\n\t ") != std::string::npos) return {};
    return wide(text);
}
std::wstring error(uint32_t status) {
    if (status == 401 || status == 403) return tr(L"SteamGridDB odrzucił klucz API.");
    if (status == 429) return tr(L"Limit zapytań SteamGridDB. Spróbuj ponownie za chwilę.");
    return tr(L"Nie można pobrać grafik ze SteamGridDB (HTTP ") + std::to_wstring(status) + L").";
}
std::string_view body(const HttpResponse& response) {
    return {reinterpret_cast<const char*>(response.body.data()), response.body.size()};
}

// How many animated assets a gallery query has (its first page tells), so the later pages know
// where the static ones start without asking again. For this run of the launcher only.
std::mutex& animatedCountsMutex() { static std::mutex value; return value; }
std::unordered_map<std::wstring, int>& animatedCounts() { static std::unordered_map<std::wstring, int> value; return value; }
int knownAnimated(const std::wstring& query) {
    std::lock_guard lock(animatedCountsMutex());
    const auto found = animatedCounts().find(query);
    return found == animatedCounts().end() ? -1 : found->second;
}
void rememberAnimated(const std::wstring& query, int count) {
    std::lock_guard lock(animatedCountsMutex());
    if (animatedCounts().size() > 512) animatedCounts().clear();
    animatedCounts()[query] = count;
}
}

namespace {
GridPage browse(const Game& game, ArtworkKind kind, int page, GridSource source, const std::wstring& key,
                const ArtworkHttp& get, bool animated) {
    if (kind == ArtworkKind::disc) return {{}, {}, false, tr(L"Grafiki płyt pochodzą ze ScreenScraper.")};
    if (!isGame(game) || gameKey(game).empty() || page < 0) return {{}, {}, false, tr(L"Brak gry.")};
    if (key.empty()) return {{}, {}, false, tr(L"Brak klucza SteamGridDB.")};
    if (key.size() > 1024 || key.find_first_of(L"\r\n") != std::wstring::npos) return {{}, {}, false, tr(L"Nieprawidłowy klucz SteamGridDB.")};
    const auto headers = L"Accept: application/json\r\nAuthorization: Bearer " + key + L"\r\n";
    // Covers, posters and wide tiles are all SGDB "grids", told apart by their dimensions.
    const auto endpoint = kind == ArtworkKind::hero ? L"heroes" : kind == ArtworkKind::logo ? L"logos" : L"grids";
    const auto dimensions = kind == ArtworkKind::cover ? L"&dimensions=512x512,600x900" :
                            kind == ArtworkKind::poster ? L"&dimensions=600x900,342x482,660x930" :
                            kind == ArtworkKind::landscape ? L"&dimensions=920x430,460x215" : L"";
    // Animated assets are animated WebP files (the only kind decoded: animated_image.hpp).
    const auto query = [&](GridSource selected, bool moving, int number) {
        const auto address = L"https://www.steamgriddb.com/api/v2/" + std::wstring(endpoint) + L'/' +
            (selected.steam ? L"steam/" : L"game/") + std::to_wstring(selected.id) +
            (moving ? std::wstring(L"?types=animated&mimes=image/webp") :
             L"?types=static&mimes=" + (kind == ArtworkKind::logo ? std::wstring(L"image/png") : std::wstring(L"image/png,image/jpeg"))) +
            L"&limit=" + std::to_wstring(grid_page_size) +
            L"&page=" + std::to_wstring(number) + dimensions;
        const auto response = get(address, headers, 2 * 1024 * 1024);
        GridPage result;
        if (response.status == 200) result = parseGridPage(body(response));
        else if (response.status != 404) result.error = error(response.status);
        result.source = selected;
        for (auto& asset : result.assets) asset.animated = moving;
        return result;
    };
    const auto fetch = [&](GridSource selected) {
        if (!animated) return query(selected, false, page);
        // The animated ones first: those of this page, then the static ones after the earlier pages' share.
        const auto counted = std::wstring(endpoint) + (selected.steam ? L"|s" : L"|g") + std::to_wstring(selected.id) + dimensions;
        int known = knownAnimated(counted);
        GridPage result;
        if (known < 0 || page * grid_page_size < known) {
            auto moving = query(selected, true, page);
            // A refused animated query leaves the static ones (its count is not kept); a rate limit stops here.
            if (moving.error.empty()) {
                result = std::move(moving);
                known = result.total >= 0 ? result.total : page * grid_page_size + static_cast<int>(result.assets.size());
                rememberAnimated(counted, known);
            } else if (moving.error == error(429)) return moving;
            else known = 0;
        }
        result.source = selected;
        result.error.clear();
        const int shown = static_cast<int>(result.assets.size());
        const int offset = page * grid_page_size - std::min(known, page * grid_page_size); // Static ones on earlier pages.
        bool more = known > (page + 1) * grid_page_size || shown == grid_page_size;
        if (shown < grid_page_size) {
            std::unordered_set<uint32_t> seen;
            for (const auto& asset : result.assets) seen.insert(asset.id);
            const int want = grid_page_size - shown;
            const int first = offset / grid_page_size, last = (offset + want - 1) / grid_page_size;
            for (int number = first; number <= last; ++number) {
                auto still = query(selected, false, number);
                if (!still.error.empty()) { if (result.assets.empty()) result.error = std::move(still.error); break; }
                const int from = number == first ? offset % grid_page_size : 0;
                for (int i = from; i < static_cast<int>(still.assets.size()) && result.assets.size() < grid_page_size; ++i)
                    if (seen.insert(still.assets[static_cast<size_t>(i)].id).second) result.assets.push_back(std::move(still.assets[static_cast<size_t>(i)]));
                more = more || (still.total >= 0 ? offset + want < still.total : still.more && number == last);
                if (still.total >= 0 ? (number + 1) * grid_page_size >= still.total : !still.more) break; // No static page after it.
            }
        }
        result.more = more;
        return result;
    };
    if (source.id) return fetch(source);
    if (steamArtwork(game)) {
        auto result = fetch({game.app_id, true});
        if (!result.assets.empty() || !result.error.empty() || result.more) return result;
    }
    const auto matched = resolveGridGame(artworkTitle(game), key, get);
    return matched.source.id ? fetch(matched.source) : GridPage{{}, {}, false, matched.error};
}
}

GridPage parseGridPage(std::string_view bytes) {
    GridPage result;
    const auto root = json::parse(bytes);
    const auto* data = root ? root->get("data") : nullptr;
    if (!data || data->type != json::Value::Type::array) {
        result.error = tr(L"SteamGridDB zwrócił nieprawidłową odpowiedź.");
        return result;
    }
    result.more = data->elements.size() >= grid_page_size;
    if (const auto* total = root->get("total"); total && total->type == json::Value::Type::number && std::isfinite(total->numeric) &&
        total->numeric >= 0 && total->numeric <= 1e7) result.total = static_cast<int>(total->numeric);
    std::unordered_set<uint32_t> seen;
    for (const auto& item : data->elements) {
        GridAsset asset;
        asset.id = number(item.get("id"));
        asset.url = url(item.get("url"));
        asset.thumbnail = url(item.get("thumb"));
        const auto width = number(item.get("width")), height = number(item.get("height"));
        if (!asset.id || asset.url.empty() || width > 8192 || height > 8192 || !seen.insert(asset.id).second) continue;
        asset.width = static_cast<int>(width); asset.height = static_cast<int>(height);
        const auto* author = item.get("author");
        const auto* name = author ? author->get("name") : nullptr;
        asset.author = name ? wide(name->stringOr()) : std::wstring{};
        if (asset.author.size() > 80) asset.author.resize(80);
        result.assets.push_back(std::move(asset));
        if (result.assets.size() == grid_page_size) break;
    }
    return result;
}

GridPage browseSteamGridDB(const Game& game, ArtworkKind kind, int page, GridSource source,
                           const std::wstring& key, const ArtworkHttp& get) {
    return browse(game, kind, page, source, key, get, false);
}

GridPage browseSteamGridDBGallery(const Game& game, ArtworkKind kind, int page, GridSource source,
                                  const std::wstring& key, const ArtworkHttp& get) {
    return browse(game, kind, page, source, key, get, animated_image::supported() && kind != ArtworkKind::logo);
}

bool decodableArtwork(const std::vector<uint8_t>& bytes) {
    if (bytes.size() < 128 || bytes.size() > 20 * 1024 * 1024 ||
        !((bytes[0] == 0x89 && bytes[1] == 'P') || (bytes[0] == 0xff && bytes[1] == 0xd8))) return false;
    // Fully decoded (to a 32 px thumbnail), so a truncated download is refused.
    return image_codec::decode(bytes, [](const image_codec::Info& info) -> std::optional<image_codec::Size> {
        if (info.width > 8192 || info.height > 8192 || static_cast<uint64_t>(info.width) * info.height > 32 * 1024 * 1024) return {};
        return image_codec::Size{32, 32};
    }, image_codec::Filter::fant, true).has_value();
}

std::vector<uint8_t> downloadGridImage(const GridAsset& asset, bool thumbnail, const ArtworkHttp& get) {
    for (const auto& address : {thumbnail ? asset.thumbnail : asset.url, thumbnail ? asset.url : std::wstring{}}) {
        if (address.empty() || !address.starts_with(L"https://")) continue;
        auto response = get(address, L"Accept: image/png, image/jpeg\r\n", 20 * 1024 * 1024);
        if (response.status == 200 && decodableArtwork(response.body)) return std::move(response.body);
    }
    return {};
}

std::vector<uint8_t> downloadAnimatedPreview(const GridAsset& asset, const ArtworkHttp& get) {
    if (!asset.animated || !asset.url.starts_with(L"https://")) return {};
    const auto response = get ? get(asset.url, L"Accept: image/webp\r\n", animated_preview_bytes) :
                                httpGetPrefix(asset.url, L"Accept: image/webp\r\n", animated_preview_bytes, 30000);
    if (response.status != 200) return {};
    return animated_image::wholeFrames(response.body);
}

bool downloadAnimatedGrid(const GridAsset& asset, const std::filesystem::path& file, const ArtworkFileHttp& download) {
    if (!asset.animated || !asset.url.starts_with(L"https://")) return false;
    const auto headers = L"Accept: image/webp\r\n";
    const auto response = download ? download(asset.url, headers, file, animated_artwork_limit) :
                                      httpDownloadFile(asset.url, headers, file, animated_artwork_limit, 180000, false);
    std::error_code error;
    const auto size = std::filesystem::file_size(file, error);
    bool valid = false;
    if (response.status == 200 && !error && size >= 30 && size <= animated_artwork_limit) {
        std::ifstream input(file, std::ios::binary);
        std::vector<uint8_t> head(30);
        valid = input.read(reinterpret_cast<char*>(head.data()), static_cast<std::streamsize>(head.size())) && animated_image::animatedWebp(head);
    }
    if (valid && !animatedStill(file).empty()) return true;
    if (response.status == 200) std::filesystem::remove(file, error); // Complete but not an animation: not kept.
    return false;
}

namespace {
std::optional<image_codec::Size> stillSize(const image_codec::Info& info) {
    if (info.width > 8192 || info.height > 8192) return {};
    return image_codec::Size{info.width, info.height};
}
std::vector<uint8_t> encodeStill(const std::optional<image_codec::Image>& image) {
    if (!image) return {};
    bool opaque = true;
    for (size_t i = 3; i < image->pixels.size() && opaque; i += 4) opaque = image->pixels[i] == 255;
    auto bytes = opaque ? image_codec::encodeJpeg(*image, 0.9f) : image_codec::encodePng(*image);
    return decodableArtwork(bytes) ? bytes : std::vector<uint8_t>{};
}
} // namespace

std::vector<uint8_t> animatedStill(const std::filesystem::path& file) { return encodeStill(image_codec::decodeFile(file, stillSize)); }
std::vector<uint8_t> animatedStill(std::span<const uint8_t> bytes) { return encodeStill(image_codec::decode(bytes, stillSize)); }
