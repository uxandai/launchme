#include "wallpaper_sources.hpp"
#include "image_codec.hpp"
#include "../i18n/i18n.hpp"
#include "../net/json.hpp"
#include "../net/url_encoding.hpp"
#include "../platform/atomic_file.hpp"
#include "../platform/text_encoding.hpp"

#include <algorithm>
#include <cmath>
#include <cwctype>
#include <fstream>
#include <map>
#include <mutex>
#include <set>

namespace wallpaper_sources {
namespace {
uint32_t fnv(std::wstring_view text) {
    uint32_t hash = 2166136261u;
    for (const auto ch : text) { hash ^= static_cast<uint32_t>(ch); hash *= 16777619u; }
    return hash;
}
uint32_t assetId(ArtworkProvider provider, const std::wstring& url) {
    const uint32_t prefix = provider == ArtworkProvider::wallhaven ? 0x10000000u : provider == ArtworkProvider::danbooru ? 0x20000000u :
                            provider == ArtworkProvider::motionbgs ? 0x30000000u : 0x40000000u;
    return prefix | (fnv(url) & 0x0fffffffu);
}
std::wstring wide(std::string_view text) { return text_encoding::wideLenient(text); }
std::string field(const json::Value* value, std::string_view name) {
    const auto* found = value ? value->get(name) : nullptr;
    return found && found->type == json::Value::Type::string ? found->text : std::string{};
}
int number(const json::Value* value, std::string_view name) {
    const auto* found = value ? value->get(name) : nullptr;
    return found && found->type == json::Value::Type::number ? static_cast<int>(found->numeric) : 0;
}
bool flag(const json::Value* value, std::string_view name) {
    const auto* found = value ? value->get(name) : nullptr;
    return found && found->type == json::Value::Type::boolean && found->flag;
}
std::optional<json::Value> getJson(const ArtworkHttp& get, const std::wstring& url) {
    const auto response = get(url, L"Accept: application/json\r\n", 8 * 1024 * 1024);
    if (response.status != 200 || response.body.empty()) return std::nullopt;
    return json::parse(std::string_view(reinterpret_cast<const char*>(response.body.data()), response.body.size()), 8 * 1024 * 1024);
}
std::wstring trimmed(std::wstring text) {
    while (!text.empty() && std::iswspace(text.back())) text.pop_back();
    size_t start = 0;
    while (start < text.size() && std::iswspace(text[start])) ++start;
    return text.substr(start);
}

// --- Wallhaven --------------------------------------------------------------
GridPage wallhaven(const std::wstring& query, int page, const ArtworkHttp& get) {
    GridPage out;
    const auto found = getJson(get, L"https://wallhaven.cc/api/v1/search?categories=110&purity=100&ratios=landscape&atleast=1920x1080&sorting=" +
                                        std::wstring(query.empty() ? L"toplist" : L"relevance") + L"&page=" + std::to_wstring(page + 1) +
                                        L"&q=" + url_encoding::component(query));
    if (!found) { out.error = tr(L"Wallhaven nie odpowiada."); return out; }
    if (const auto* data = found->get("data"); data && data->type == json::Value::Type::array)
        for (const auto& item : data->elements) {
            if (field(&item, "purity") != "sfw") continue; // purity=100 asks for these only; never trust it blindly.
            GridAsset asset;
            asset.url = wide(field(&item, "path"));
            const auto* thumbs = item.get("thumbs");
            asset.thumbnail = wide(field(thumbs, "large"));
            if (!asset.url.starts_with(L"https://")) continue;
            asset.id = assetId(ArtworkProvider::wallhaven, asset.url);
            asset.width = number(&item, "dimension_x"); asset.height = number(&item, "dimension_y");
            asset.author = L"Wallhaven · " + wide(field(&item, "category"));
            out.assets.push_back(std::move(asset));
        }
    if (const auto* meta = found->get("meta")) out.more = number(meta, "current_page") < number(meta, "last_page");
    return out;
}

// --- Danbooru ---------------------------------------------------------------
GridPage danbooru(const std::wstring& query, int page, const ArtworkHttp& get) {
    GridPage out;
    // One tag besides the rating (an anonymous search allows two): words joined by underscores.
    std::wstring tag;
    for (const auto ch : trimmed(query)) tag += std::iswspace(ch) ? L'_' : static_cast<wchar_t>(std::towlower(ch));
    if (tag.empty()) tag = L"scenery";
    const auto found = getJson(get, L"https://danbooru.donmai.us/posts.json?limit=100&page=" + std::to_wstring(page + 1) +
                                        L"&tags=rating%3Ageneral+" + url_encoding::component(tag));
    if (!found || found->type != json::Value::Type::array) { out.error = tr(L"Danbooru nie odpowiada."); return out; }
    for (const auto& post : found->elements) {
        if (field(&post, "rating") != "g" || flag(&post, "is_deleted") || flag(&post, "is_banned")) continue;
        const auto extension = field(&post, "file_ext");
        if (extension != "jpg" && extension != "png") continue;
        const int width = number(&post, "image_width"), height = number(&post, "image_height");
        if (width < 1600 || width < height * 1.3f) continue; // Landscape pictures fill a screen.
        GridAsset asset;
        asset.url = wide(field(&post, "file_url"));
        if (!asset.url.starts_with(L"https://")) continue;
        std::wstring small;
        if (const auto* media = post.get("media_asset"))
            if (const auto* variants = media->get("variants"); variants && variants->type == json::Value::Type::array)
                for (const auto& variant : variants->elements)
                    if (field(&variant, "type") == "360x360" && field(&variant, "file_ext") == "jpg") small = wide(field(&variant, "url"));
        asset.thumbnail = small.empty() ? wide(field(&post, "large_file_url")) : small;
        asset.id = assetId(ArtworkProvider::danbooru, asset.url);
        asset.width = width; asset.height = height;
        auto artist = wide(field(&post, "tag_string_artist"));
        if (const auto space = artist.find(L' '); space != std::wstring::npos) artist.resize(space);
        asset.author = L"Danbooru" + (artist.empty() ? std::wstring{} : L" · " + artist);
        out.assets.push_back(std::move(asset));
        if (out.assets.size() >= 48) break;
    }
    out.more = found->elements.size() >= 100;
    return out;
}

// --- MotionBGs --------------------------------------------------------------
std::wstring attribute(std::string_view html, std::string_view name) {
    const auto at = html.find(name);
    if (at == std::string_view::npos) return {};
    auto value = html.substr(at + name.size());
    std::string_view text;
    if (!value.empty() && value.front() == '"') {
        value.remove_prefix(1);
        text = value.substr(0, value.find('"'));
    } else text = value.substr(0, value.find_first_of(" >"));
    return wide(text);
}
std::wstring entities(std::wstring text) {
    for (const auto& [from, to] : std::initializer_list<std::pair<std::wstring_view, std::wstring_view>>{
             {L"&amp;", L"&"}, {L"&quot;", L"\""}, {L"&#x27;", L"'"}, {L"&#39;", L"'"}, {L"&lt;", L"<"}, {L"&gt;", L">"}})
        for (size_t at = text.find(from); at != std::wstring::npos; at = text.find(from, at + to.size())) text.replace(at, from.size(), to);
    return text;
}
struct MotionPage { std::vector<GridAsset> assets; std::wstring next; };
MotionPage motionbgsPage(const std::wstring& url, const ArtworkHttp& get) {
    MotionPage out;
    const auto response = get(url, L"Accept: text/html\r\n", 4 * 1024 * 1024);
    if (response.status != 200) return out;
    const std::string html(response.body.begin(), response.body.end());
    const auto list_start = html.find("class=tmb");
    const auto list_end = html.find("class=pag");
    for (size_t at = html.find("<a title=", list_start == std::string::npos ? 0 : list_start); at != std::string::npos; at = html.find("<a title=", at + 9)) {
        if (list_end != std::string::npos && at > list_end) break;
        const auto end = html.find("</a>", at);
        if (end == std::string::npos) break;
        const std::string_view anchor(html.data() + at, end - at);
        const auto href = attribute(anchor, "href=");
        const auto image = attribute(anchor.substr(std::min(anchor.size(), anchor.find("<img"))), "src=");
        if (href.empty() || !href.starts_with(L"/") || image.find(L"/media/") == std::wstring::npos) continue;
        GridAsset asset;
        asset.thumbnail = L"https://motionbgs.com" + image;
        asset.preview = motionbgsPreview(asset.thumbnail);
        // The HD (1920×1080) MP4 of the entry, by its media number.
        const auto media = image.find(L"/media/") + 7;
        const auto number_end = image.find(L'/', media);
        if (number_end == std::wstring::npos || asset.preview.empty()) continue;
        asset.url = L"https://motionbgs.com/dl/hd/" + image.substr(media, number_end - media);
        asset.id = assetId(ArtworkProvider::motionbgs, asset.url);
        auto title = std::wstring{};
        if (const auto ttl = anchor.find("class=ttl>"); ttl != std::string_view::npos) {
            const auto from = ttl + 10;
            title = entities(wide(anchor.substr(from, anchor.find('<', from) - from)));
        }
        asset.author = L"MotionBGs · " + trimmed(title);
        asset.width = 1920; asset.height = 1080;
        out.assets.push_back(std::move(asset));
    }
    if (list_end != std::string::npos) {
        // "Next" in the pagination: its link, quoted or not.
        const std::string_view pagination(html.data() + list_end, std::min<size_t>(html.size() - list_end, 2000));
        for (size_t link = pagination.find("<a href="); link != std::string_view::npos; link = pagination.find("<a href=", link + 8)) {
            const auto close = pagination.find("</a>", link);
            const auto anchor = pagination.substr(link, close == std::string_view::npos ? std::string_view::npos : close - link);
            if (anchor.find("Next") == std::string_view::npos) continue;
            const auto next = entities(attribute(anchor, "href="));
            if (next.starts_with(L"/")) {
                std::wstring encoded;
                for (const auto ch : next) encoded += ch == L' ' ? std::wstring(L"%20") : std::wstring(1, ch);
                out.next = L"https://motionbgs.com" + encoded;
            }
        }
    }
    return out;
}
GridPage motionbgs(const std::wstring& query, int page, const ArtworkHttp& get) {
    // Page addresses are learnt from each page's "Next" (searches may land on a tag's pages).
    static std::mutex guard;
    static std::map<std::wstring, std::vector<std::wstring>> chains;
    const auto first = query.empty() ? std::wstring(L"https://motionbgs.com/4k/") : L"https://motionbgs.com/search?q=" + url_encoding::component(query);
    GridPage out;
    MotionPage current;
    for (int index = 0; index <= page; ++index) {
        std::wstring url;
        {
            std::lock_guard lock(guard);
            auto& chain = chains[first];
            if (chain.empty()) chain.push_back(first);
            if (index < static_cast<int>(chain.size())) url = chain[static_cast<size_t>(index)];
        }
        if (url.empty()) { out.error = tr(L"Brak dalszych stron."); return out; }
        if (index < page) {
            std::lock_guard lock(guard);
            if (index + 1 < static_cast<int>(chains[first].size())) continue; // Known already.
        }
        current = motionbgsPage(url, get);
        std::lock_guard lock(guard);
        auto& chain = chains[first];
        if (!current.next.empty() && static_cast<int>(chain.size()) == index + 1) chain.push_back(current.next);
        if (index < page && current.next.empty()) { out.error = tr(L"Brak dalszych stron."); return out; }
    }
    out.assets = std::move(current.assets);
    out.more = !current.next.empty();
    if (out.assets.empty()) out.error = tr(L"MotionBGs nie ma tapet dla tego hasła.");
    return out;
}

// --- Steam animated profile backgrounds ----------------------------------------
GridPage steamBackgrounds(const std::wstring& query, int page, const ArtworkHttp& get) {
    GridPage out;
    const size_t wanted = static_cast<size_t>(page + 1) * grid_page_size;
    std::vector<GridAsset> animated;
    std::string cursor;
    bool more = true;
    for (int request = 0; request < 12 && animated.size() < wanted + 1 && more; ++request) {
        std::string input = "{\"community_item_classes\":[3],\"count\":100,\"language\":\"english\"";
        if (!query.empty()) input += ",\"search_term\":" + json::quote(text_encoding::utf8Lenient(trimmed(query)));
        if (!cursor.empty()) input += ",\"cursor\":" + json::quote(cursor);
        input += "}";
        const auto found = getJson(get, L"https://api.steampowered.com/ILoyaltyRewardsService/QueryRewardItems/v1/?input_json=" + url_encoding::component(input));
        const auto* response = found ? found->get("response") : nullptr;
        if (!response) { if (animated.empty()) out.error = tr(L"Sklep punktów Steam nie odpowiada."); break; }
        if (const auto* definitions = response->get("definitions"); definitions && definitions->type == json::Value::Type::array) {
            for (const auto& item : definitions->elements) {
                const auto* data = item.get("community_item_data");
                if (!flag(data, "animated")) continue;
                const auto app = std::to_wstring(number(&item, "appid"));
                const auto base = L"https://shared.fastly.steamstatic.com/community_assets/images/items/" + app + L"/";
                const auto movie = field(data, "item_movie_mp4"), small = field(data, "item_movie_mp4_small"), image = field(data, "item_image_large");
                if (movie.empty() || image.empty()) continue;
                GridAsset asset;
                asset.url = base + wide(movie);
                asset.preview = base + wide(small.empty() ? movie : small);
                asset.thumbnail = base + wide(image);
                asset.id = assetId(ArtworkProvider::steam_backgrounds, asset.url);
                asset.author = L"Steam · " + wide(field(data, "item_title"));
                asset.width = 1920; asset.height = 1080;
                animated.push_back(std::move(asset));
            }
            if (definitions->elements.empty()) more = false;
        }
        cursor = field(response, "next_cursor");
        if (cursor.empty()) more = false;
    }
    const size_t from = static_cast<size_t>(page) * grid_page_size;
    for (size_t i = from; i < animated.size() && i < from + grid_page_size; ++i) out.assets.push_back(animated[i]);
    out.more = animated.size() > from + grid_page_size || (more && animated.size() >= from + grid_page_size);
    if (out.assets.empty() && out.error.empty()) out.error = tr(L"Brak animowanych teł dla tego hasła.");
    return out;
}

bool videoFile(const std::filesystem::path& file) {
    auto extension = file.extension().wstring();
    for (auto& ch : extension) ch = static_cast<wchar_t>(std::towlower(ch));
    return extension == L".mp4" || extension == L".m4v" || extension == L".webm" || extension == L".mkv" || extension == L".mov" || extension == L".wmv" || extension == L".avi";
}
// A picture scaled to cover 2560×1440 (never enlarged), as JPEG; empty when it cannot be read.
std::vector<uint8_t> screenPicture(std::span<const uint8_t> bytes) {
    const auto image = image_codec::decode(bytes, [](const image_codec::Info& info) -> std::optional<image_codec::Size> {
        if (!info.width || !info.height || static_cast<uint64_t>(info.width) * info.height > 200ull * 1024 * 1024) return std::nullopt;
        const double scale = std::min(1.0, std::max(2560.0 / info.width, 1440.0 / info.height));
        return image_codec::Size{std::max(1u, static_cast<uint32_t>(std::lround(info.width * scale))),
                                 std::max(1u, static_cast<uint32_t>(std::lround(info.height * scale)))};
    }, image_codec::Filter::cubic);
    if (!image) return {};
    return image_codec::encodeJpeg(*image, 0.9f);
}
// Earlier wallpapers go when a new one is kept (a playing video stays until it is let go).
void removeOthers(const std::filesystem::path& folder, const std::filesystem::path& keep) {
    std::error_code ec;
    for (std::filesystem::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code file;
        if (!it->is_regular_file(file) || it->path().filename() == keep.filename()) continue;
        std::filesystem::remove(it->path(), file);
    }
}
}

bool own(ArtworkProvider provider) {
    return provider == ArtworkProvider::wallhaven || provider == ArtworkProvider::danbooru || provider == ArtworkProvider::motionbgs ||
           provider == ArtworkProvider::steam_backgrounds;
}
bool video(ArtworkProvider provider) { return provider == ArtworkProvider::motionbgs || provider == ArtworkProvider::steam_backgrounds; }

GridPage browse(ArtworkProvider provider, const std::wstring& query, int page, const ArtworkHttp& get) {
    GridPage out;
    switch (provider) {
    case ArtworkProvider::wallhaven: out = wallhaven(query, page, get); break;
    case ArtworkProvider::danbooru: out = danbooru(query, page, get); break;
    case ArtworkProvider::motionbgs: out = motionbgs(query, page, get); break;
    case ArtworkProvider::steam_backgrounds: out = steamBackgrounds(query, page, get); break;
    default: break;
    }
    out.provider = provider;
    if (out.assets.empty() && out.error.empty()) out.error = tr(L"Brak tapet dla tego hasła.");
    return out;
}

std::vector<uint8_t> thumbnail(const GridAsset& asset, const ArtworkHttp& get) {
    for (const auto& address : {asset.thumbnail, asset.url}) {
        // A video's own address is never a picture.
        if (address.empty() || !address.starts_with(L"https://") || address.find(L"/dl/") != std::wstring::npos || address.ends_with(L".mp4")) continue;
        auto response = get(address, L"Accept: image/png, image/jpeg\r\n", 20 * 1024 * 1024);
        if (response.status == 200 && decodableArtwork(response.body)) return std::move(response.body);
    }
    return {};
}

std::filesystem::path store(ArtworkProvider provider, const GridAsset& asset, const std::filesystem::path& folder, std::wstring& error) {
    std::error_code ec;
    std::filesystem::create_directories(folder, ec);
    const auto id = std::to_wstring(asset.id);
    if (video(provider)) {
        const auto file = folder / (L"video-" + id + L".mp4");
        if (!std::filesystem::exists(file, ec)) {
            const auto response = httpDownloadFile(asset.url, L"Accept: video/mp4\r\n", file, 400ull * 1024 * 1024, 240000, false);
            if (response.status != 200 || !std::filesystem::exists(file, ec)) { error = tr(L"Nie można pobrać tapety."); return {}; }
        }
        removeOthers(folder, file);
        return file;
    }
    std::vector<uint8_t> bytes;
    for (const auto& address : {asset.url, asset.thumbnail}) {
        if (address.empty() || !address.starts_with(L"https://")) continue;
        auto response = httpGet(address, L"Accept: image/png, image/jpeg\r\n", 60 * 1024 * 1024);
        if (response.status == 200 && !response.body.empty()) { bytes = screenPicture(response.body); if (!bytes.empty()) break; }
    }
    if (bytes.empty()) { error = tr(L"Nie można pobrać tapety."); return {}; }
    const auto file = folder / (L"picture-" + id + L".jpg");
    if (!atomic_file::write(file, bytes)) { error = tr(L"Nie można zapisać tapety."); return {}; }
    removeOthers(folder, file);
    return file;
}

std::filesystem::path storeLocal(const std::filesystem::path& source, const std::filesystem::path& folder, bool& is_video, std::wstring& error) {
    std::error_code ec;
    std::filesystem::create_directories(folder, ec);
    is_video = videoFile(source);
    const auto id = std::to_wstring(fnv(source.wstring()));
    if (is_video) {
        auto extension = source.extension().wstring();
        for (auto& ch : extension) ch = static_cast<wchar_t>(std::towlower(ch));
        const auto file = folder / (L"local-" + id + extension);
        if (!std::filesystem::copy_file(source, file, std::filesystem::copy_options::overwrite_existing, ec) || ec) {
            error = tr(L"Nie można skopiować filmu."); return {};
        }
        removeOthers(folder, file);
        return file;
    }
    std::ifstream input(source, std::ios::binary | std::ios::ate);
    if (!input || input.tellg() <= 0 || input.tellg() > 80 * 1024 * 1024) { error = tr(L"Wybierz obraz PNG lub JPEG albo film MP4."); return {}; }
    std::vector<uint8_t> bytes(static_cast<size_t>(input.tellg()));
    input.seekg(0);
    input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    bytes = screenPicture(bytes);
    if (bytes.empty()) { error = tr(L"Wybierz obraz PNG lub JPEG albo film MP4."); return {}; }
    const auto file = folder / (L"local-" + id + L".jpg");
    if (!atomic_file::write(file, bytes)) { error = tr(L"Nie można zapisać tapety."); return {}; }
    removeOthers(folder, file);
    return file;
}

std::filesystem::path preview(const GridAsset& asset, const std::filesystem::path& folder) {
    if (asset.preview.empty() || !asset.preview.starts_with(L"https://")) return {};
    const auto previews = folder / L"previews";
    std::error_code ec;
    std::filesystem::create_directories(previews, ec);
    const auto file = previews / (std::to_wstring(asset.id) + L".mp4");
    if (std::filesystem::exists(file, ec) && std::filesystem::file_size(file, ec) > 0) return file;
    // Kept small: the oldest previews go past 48.
    std::vector<std::pair<std::filesystem::file_time_type, std::filesystem::path>> kept;
    for (std::filesystem::directory_iterator it(previews, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code file_ec;
        if (it->is_regular_file(file_ec)) kept.emplace_back(it->last_write_time(file_ec), it->path());
    }
    if (kept.size() > 48) {
        std::sort(kept.begin(), kept.end());
        for (size_t i = 0; i + 48 < kept.size(); ++i) std::filesystem::remove(kept[i].second, ec);
    }
    const auto response = httpDownloadFile(asset.preview, L"Accept: video/mp4\r\n", file, 40ull * 1024 * 1024, 60000, false);
    return response.status == 200 && std::filesystem::exists(file, ec) ? file : std::filesystem::path{};
}

std::wstring motionbgsPreview(std::wstring_view thumbnail) {
    // https://motionbgs.com/i/c/364x205/media/3510/rainy-forest.jpg → /media/3510/rainy-forest.960x540.mp4
    // (a "<name>.3840x2160.jpg" thumbnail drops its size first).
    const auto media = thumbnail.find(L"/media/");
    if (media == std::wstring_view::npos) return {};
    std::wstring path(thumbnail.substr(media));
    if (path.size() < 5 || path.substr(path.size() - 4) != L".jpg") return {};
    path.resize(path.size() - 4);
    if (const auto dot = path.rfind(L'.'); dot != std::wstring::npos && dot > path.rfind(L'/')) {
        const auto size = path.substr(dot + 1);
        if (size.find(L'x') != std::wstring::npos && std::all_of(size.begin(), size.end(), [](wchar_t ch) { return std::iswdigit(ch) || ch == L'x'; }))
            path.resize(dot);
    }
    return L"https://motionbgs.com" + path + L".960x540.mp4";
}
}
