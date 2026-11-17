#include "boot_video_sources.hpp"
#include "image_codec.hpp"
#include "../i18n/i18n.hpp"
#include "../net/http_download.hpp"
#include "../net/json.hpp"
#include "../net/url_encoding.hpp"
#include "../platform/atomic_file.hpp"
#include "../platform/file_io.hpp"
#include "../platform/text_encoding.hpp"
#include "../platform/unicode_text.hpp"

#ifdef _WIN32
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>
#endif

#include <algorithm>
#include <charconv>
#include <cstring>
#include <cwctype>

namespace boot_video_sources {
namespace {
constexpr uint32_t repo_ids = 0x70000000u;
constexpr uint32_t local_ids = 0x78000000u;

bool storedAsset(const GridAsset& asset) { return (asset.id & 0xf8000000u) == local_ids || !asset.url.starts_with(L"https://"); }

uint32_t fnv(std::wstring_view text) {
    uint32_t hash = 2166136261u;
    for (const auto ch : text) { hash ^= static_cast<uint32_t>(ch); hash *= 16777619u; }
    return hash;
}

uint32_t repoAssetId(const std::wstring& post_id, const std::wstring& url) {
    return repo_ids | (fnv(post_id.empty() ? url : post_id) & 0x07ffffffu);
}

uint32_t localAssetId(const std::wstring& source) {
    return local_ids | (fnv(source) & 0x07ffffffu);
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

std::wstring trimmed(std::wstring text) {
    while (!text.empty() && std::iswspace(text.back())) text.pop_back();
    size_t start = 0;
    while (start < text.size() && std::iswspace(text[start])) ++start;
    return text.substr(start);
}

bool videoExtension(std::wstring extension) {
    for (auto& ch : extension) ch = static_cast<wchar_t>(std::towlower(ch));
    return extension == L".webm" || extension == L".mp4" || extension == L".m4v" ||
           extension == L".mkv" || extension == L".mov" || extension == L".wmv";
}

std::wstring videoExtFromUrl(std::wstring_view url) {
    const auto query = url.find(L'?');
    const auto clean = url.substr(0, query);
    const auto dot = clean.rfind(L'.');
    if (dot != std::wstring_view::npos && clean.size() - dot <= 6) {
        std::wstring ext(clean.substr(dot));
        if (videoExtension(ext)) {
            for (auto& ch : ext) ch = static_cast<wchar_t>(std::towlower(ch));
            return ext;
        }
    }
    return L".webm";
}

std::filesystem::path titlePath(const std::filesystem::path& video_file) {
    auto meta = video_file;
    meta.replace_extension(L".txt");
    return meta;
}

void writeTitle(const std::filesystem::path& video_file, std::wstring_view title) {
    if (title.empty()) return;
    const auto utf8 = text_encoding::utf8Lenient(title);
    atomic_file::write(titlePath(video_file), std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(utf8.data()), utf8.size()));
}

std::wstring readTitle(const std::filesystem::path& video_file) {
    const auto bytes = file_io::readBytes(titlePath(video_file), 4096);
    if (!bytes.empty()) {
        auto title = trimmed(wide(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size())));
        if (!title.empty()) return title;
    }
    return video_file.stem().wstring();
}

uint32_t parseStoredId(const std::filesystem::path& file) {
    const auto stem = text_encoding::utf8Lenient(file.stem().wstring());
    const std::string_view view(stem);
    for (const std::string_view prefix : {"repo-", "local-"}) {
        if (!view.starts_with(prefix)) continue;
        uint32_t id = 0;
        const auto digits = view.substr(prefix.size());
        const auto [end, ec] = std::from_chars(digits.data(), digits.data() + digits.size(), id);
        if (ec == std::errc{} && end == digits.data() + digits.size() && id != 0) return id;
    }
    return localAssetId(file.filename().wstring());
}
} // namespace

std::vector<StoredVideo> storedVideos(const std::filesystem::path& folder) {
    std::vector<std::pair<std::filesystem::file_time_type, StoredVideo>> entries;
    std::error_code ec;
    for (std::filesystem::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code file_ec;
        if (!it->is_regular_file(file_ec)) continue;
        const auto path = it->path();
        if (!videoExtension(path.extension().wstring())) continue;
        StoredVideo item;
        item.file = path;
        item.id = parseStoredId(path);
        item.title = readTitle(path);
        entries.emplace_back(it->last_write_time(file_ec), std::move(item));
    }
    std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
        return a.first != b.first ? a.first > b.first : a.second.file < b.second.file;
    });
    std::vector<StoredVideo> out;
    out.reserve(entries.size());
    for (auto& [time, item] : entries) { (void)time; out.push_back(std::move(item)); }
    return out;
}

GridPage browse(int category, const std::wstring& query, int page, const std::filesystem::path& folder, const ArtworkHttp& get) {
    GridPage out;
    out.provider = category == 3 ? ArtworkProvider::local : ArtworkProvider::steamdeckrepo;
    if (category == 3) {
        const auto needle = unicode_text::lower(trimmed(query));
        const auto all = storedVideos(folder);
        std::vector<GridAsset> matched;
        for (const auto& item : all) {
            if (!needle.empty() && unicode_text::lower(item.title).find(needle) == std::wstring::npos) continue;
            GridAsset asset;
            asset.id = item.id;
            asset.url = item.file.wstring();
            asset.preview = item.file.wstring();
            asset.title = item.title;
            asset.author = item.title + L" · " + tr(L"Pobrane");
            matched.push_back(std::move(asset));
        }
        constexpr size_t page_size = 24;
        const size_t start = static_cast<size_t>(std::max(0, page)) * page_size;
        for (size_t i = start; i < matched.size() && out.assets.size() < page_size; ++i)
            out.assets.push_back(std::move(matched[i]));
        out.more = start + out.assets.size() < matched.size();
        if (out.assets.empty())
            out.error = all.empty() ? tr(L"Brak pobranych wideo startowych. Pobierz z listy obok albo wybierz „Z dysku…”.")
                                    : tr(L"Brak wideo dla tego hasła.");
        return out;
    }

    const wchar_t* post_type = category == 2 ? L"suspend_video" : L"boot_video";
    const wchar_t* sort = category == 1 ? L"trending" : L"likes-desc";
    std::wstring url = L"https://steamdeckrepo.com/api/posts?page=" + std::to_wstring(std::max(0, page) + 1) +
                       L"&type=" + post_type + L"&sort=" + sort;
    const auto clean_query = trimmed(query);
    if (!clean_query.empty()) url += L"&search=" + url_encoding::component(clean_query);

    const auto response = get(url, L"Accept: application/json\r\n", 8 * 1024 * 1024);
    if (response.status != 200 || response.body.empty()) {
        out.error = tr(L"Steam Deck Repo nie odpowiada.");
        return out;
    }
    const auto parsed = json::parse(std::string_view(reinterpret_cast<const char*>(response.body.data()), response.body.size()), 8 * 1024 * 1024);
    const auto* posts = parsed ? parsed->get("posts") : nullptr;
    if (!posts || posts->type != json::Value::Type::array) {
        out.error = tr(L"Steam Deck Repo nie odpowiada.");
        return out;
    }
    for (const auto& item : posts->elements) {
        const auto video = wide(field(&item, "video"));
        const auto thumb = wide(field(&item, "thumbnail"));
        if (!video.starts_with(L"https://") || !thumb.starts_with(L"https://")) continue;
        const auto post_id = wide(field(&item, "id"));
        auto title = trimmed(wide(field(&item, "title")));
        if (title.empty()) title = post_id;
        std::wstring uploader;
        if (const auto* user = item.get("user")) uploader = trimmed(wide(field(user, "steam_name")));
        const int duration = number(&item, "video_duration");

        GridAsset asset;
        asset.id = repoAssetId(post_id, video);
        asset.url = video;
        asset.thumbnail = thumb;
        // SteamDeckRepo's .webm clips on cdn.steamdeckrepo.com are ~300-800 KB and carry real audio.
        asset.preview = video;
        asset.title = title;
        asset.author = title;
        if (duration > 0) asset.author += L" · " + std::to_wstring(duration) + L" s";
        if (!uploader.empty()) asset.author += L" · " + uploader;
        out.assets.push_back(std::move(asset));
    }
    out.more = posts->elements.size() >= 12;
    if (out.assets.empty() && out.error.empty()) out.error = tr(L"Brak wideo dla tego hasła.");
    return out;
}

std::vector<uint8_t> thumbnail(const GridAsset& asset, const ArtworkHttp& get) {
    if (!asset.thumbnail.empty() && asset.thumbnail.starts_with(L"https://")) {
        // Ask for PNG/JPEG only: offering image/webp makes Cloudflare Polish transcode PNGs to WebP,
        // which decodableArtwork() refuses. If a server still sends WebP/BMP, re-encode to JPEG.
        auto response = get(asset.thumbnail, L"Accept: image/png, image/jpeg\r\n", 16 * 1024 * 1024);
        if (response.status == 200 && !response.body.empty()) {
            if (decodableArtwork(response.body)) return std::move(response.body);
            if (const auto decoded = image_codec::decode(response.body, [](const image_codec::Info& info) -> std::optional<image_codec::Size> {
                    if (!info.width || !info.height || info.width > 8192 || info.height > 8192) return std::nullopt;
                    constexpr uint32_t max_w = 960;
                    if (info.width <= max_w) return image_codec::Size{info.width, info.height};
                    return image_codec::Size{max_w, std::max<uint32_t>(1, static_cast<uint32_t>(uint64_t(info.height) * max_w / info.width))};
                })) {
                auto jpeg = image_codec::encodeJpeg(*decoded, 0.88f);
                if (decodableArtwork(jpeg)) return jpeg;
            }
        }
        return {};
    }
#ifdef _WIN32
    // Local / downloaded video without a cached thumbnail: grab a frame around 1 s via Media Foundation.
    if (storedAsset(asset) && file_io::isFile(asset.url)) {
        using Microsoft::WRL::ComPtr;
        const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        const bool uninit_com = SUCCEEDED(com);
        std::vector<uint8_t> encoded;
        if (SUCCEEDED(MFStartup(MF_VERSION, MFSTARTUP_LITE))) {
            ComPtr<IMFAttributes> attrs;
            ComPtr<IMFSourceReader> reader;
            if (SUCCEEDED(MFCreateAttributes(&attrs, 2)) &&
                SUCCEEDED(attrs->SetUINT32(MF_SOURCE_READER_ENABLE_ADVANCED_VIDEO_PROCESSING, TRUE)) &&
                SUCCEEDED(MFCreateSourceReaderFromURL(asset.url.c_str(), attrs.Get(), &reader))) {
                ComPtr<IMFMediaType> type;
                if (SUCCEEDED(MFCreateMediaType(&type)) &&
                    SUCCEEDED(type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video)) &&
                    SUCCEEDED(type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32)) &&
                    SUCCEEDED(reader->SetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), nullptr, type.Get()))) {
                    PROPVARIANT seek{};
                    seek.vt = VT_I8;
                    seek.hVal.QuadPart = 10'000'000LL; // 1.0 s (past black fade-ins); falls back if shorter.
                    reader->SetCurrentPosition(GUID_NULL, seek);
                    ComPtr<IMFMediaType> actual;
                    UINT32 w = 0, h = 0;
                    if (SUCCEEDED(reader->GetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), &actual)) &&
                        SUCCEEDED(MFGetAttributeSize(actual.Get(), MF_MT_FRAME_SIZE, &w, &h)) && w > 0 && h > 0 && w <= 4096 && h <= 4096) {
                        INT32 default_stride = static_cast<INT32>(w * 4);
                        (void)actual->GetUINT32(MF_MT_DEFAULT_STRIDE, reinterpret_cast<UINT32*>(&default_stride));
                        for (int tries = 0; tries < 8 && encoded.empty(); ++tries) {
                            DWORD flags = 0;
                            ComPtr<IMFSample> sample;
                            if (FAILED(reader->ReadSample(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), 0, nullptr, &flags, nullptr, &sample))) break;
                            if (flags & MF_SOURCE_READERF_ENDOFSTREAM) {
                                if (tries == 0) { seek.hVal.QuadPart = 0; reader->SetCurrentPosition(GUID_NULL, seek); continue; }
                                break;
                            }
                            if (!sample) continue;
                            ComPtr<IMFMediaBuffer> buffer;
                            BYTE* data = nullptr;
                            DWORD len = 0;
                            if (SUCCEEDED(sample->ConvertToContiguousBuffer(&buffer)) && SUCCEEDED(buffer->Lock(&data, nullptr, &len))) {
                                const size_t row_bytes = size_t(w) * 4;
                                const ptrdiff_t step = default_stride;
                                if (len >= row_bytes * h) {
                                    image_codec::Image frame{image_codec::Format::jpeg, w, h, std::vector<uint8_t>(row_bytes * h)};
                                    const BYTE* src = step < 0 ? data + size_t(h - 1) * static_cast<size_t>(-step) : data;
                                    for (UINT32 y = 0; y < h; ++y, src += step) {
                                        auto* dst = frame.pixels.data() + size_t(y) * row_bytes;
                                        std::memcpy(dst, src, row_bytes);
                                        for (UINT32 x = 0; x < w; ++x) dst[x * 4 + 3] = 255;
                                    }
                                    encoded = image_codec::encodeJpeg(frame, 0.86f);
                                }
                                buffer->Unlock();
                            }
                        }
                    }
                }
            }
            MFShutdown();
        }
        if (uninit_com) CoUninitialize();
        if (decodableArtwork(encoded)) return encoded;
    }
#endif
    return {};
}

std::filesystem::path store(const GridAsset& asset, const std::filesystem::path& folder, std::wstring& title, std::wstring& error) {
    std::error_code ec;
    std::filesystem::create_directories(folder, ec);
    title = asset.title.empty() ? asset.author : asset.title;
    if (storedAsset(asset)) {
        const std::filesystem::path existing(asset.url);
        if (!file_io::isFile(existing)) { error = tr(L"Nie można odczytać pliku wideo."); return {}; }
        if (title.empty()) title = readTitle(existing);
        return existing;
    }
    if (!asset.url.starts_with(L"https://")) { error = tr(L"Nie można pobrać wideo startowego."); return {}; }
    const auto ext = videoExtFromUrl(asset.url);
    const auto file = folder / (L"repo-" + std::to_wstring(asset.id) + ext);
    if (!std::filesystem::exists(file, ec) || std::filesystem::file_size(file, ec) == 0) {
        const auto cached_preview = folder / L"previews" / (std::to_wstring(asset.id) + ext);
        if (std::filesystem::exists(cached_preview, ec) && std::filesystem::file_size(cached_preview, ec) > 0) {
            std::filesystem::copy_file(cached_preview, file, std::filesystem::copy_options::overwrite_existing, ec);
        } else {
            const auto response = httpDownloadFile(asset.url, L"Accept: video/webm, video/mp4, */*\r\n", file, 120ull * 1024 * 1024, 120000, false);
            if (response.status != 200 || !std::filesystem::exists(file, ec) || std::filesystem::file_size(file, ec) == 0) {
                std::filesystem::remove(file, ec);
                error = tr(L"Nie można pobrać wideo startowego.");
                return {};
            }
        }
    }
    writeTitle(file, title);
    return file;
}

std::filesystem::path storeLocal(const std::filesystem::path& source, const std::filesystem::path& folder,
                                 uint32_t& asset_id, std::wstring& title, std::wstring& error) {
    std::error_code ec;
    std::filesystem::create_directories(folder, ec);
    if (!file_io::isFile(source) || !videoExtension(source.extension().wstring())) {
        error = tr(L"Wybierz plik wideo WebM lub MP4.");
        return {};
    }
    auto ext = source.extension().wstring();
    for (auto& ch : ext) ch = static_cast<wchar_t>(std::towlower(ch));
    asset_id = localAssetId(source.wstring());
    title = source.stem().wstring();
    const auto file = folder / (L"local-" + std::to_wstring(asset_id) + ext);
    if (std::filesystem::weakly_canonical(source, ec) != std::filesystem::weakly_canonical(file, ec)) {
        ec.clear();
        if (!std::filesystem::copy_file(source, file, std::filesystem::copy_options::overwrite_existing, ec) || ec) {
            error = tr(L"Nie można skopiować filmu.");
            return {};
        }
    }
    writeTitle(file, title);
    return file;
}

std::filesystem::path preview(const GridAsset& asset, const std::filesystem::path& folder) {
    if (storedAsset(asset)) {
        const std::filesystem::path local(asset.preview.empty() ? asset.url : asset.preview);
        return file_io::isFile(local) ? local : std::filesystem::path{};
    }
    const auto& url = asset.preview.empty() ? asset.url : asset.preview;
    if (url.empty() || !url.starts_with(L"https://")) return {};
    const auto ext = videoExtFromUrl(url);
    // Already downloaded into `folder`: play it directly without copying to previews\.
    std::error_code ec;
    const auto saved = folder / (L"repo-" + std::to_wstring(asset.id) + ext);
    if (std::filesystem::exists(saved, ec) && std::filesystem::file_size(saved, ec) > 0) return saved;

    const auto previews = folder / L"previews";
    std::filesystem::create_directories(previews, ec);
    const auto file = previews / (std::to_wstring(asset.id) + ext);
    if (std::filesystem::exists(file, ec) && std::filesystem::file_size(file, ec) > 0) return file;

    std::vector<std::pair<std::filesystem::file_time_type, std::filesystem::path>> kept;
    for (std::filesystem::directory_iterator it(previews, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code file_ec;
        if (it->is_regular_file(file_ec)) kept.emplace_back(it->last_write_time(file_ec), it->path());
    }
    if (kept.size() > 36) {
        std::sort(kept.begin(), kept.end());
        for (size_t i = 0; i + 36 < kept.size(); ++i) std::filesystem::remove(kept[i].second, ec);
    }
    const auto response = httpDownloadFile(url, L"Accept: video/webm, video/mp4, */*\r\n", file, 40ull * 1024 * 1024, 60000, false);
    if (response.status != 200 || !std::filesystem::exists(file, ec) || std::filesystem::file_size(file, ec) == 0) {
        std::filesystem::remove(file, ec);
        return {};
    }
    return file;
}
} // namespace boot_video_sources
