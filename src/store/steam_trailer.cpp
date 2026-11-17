#include "steam_trailer.hpp"
#include "../platform/file_io.hpp"
#include "game_details.hpp"
#include "../net/json.hpp"
#include "../platform/atomic_file.hpp"
#include "../platform/video_manifest.hpp"
#include "../platform/text_encoding.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>

namespace {
std::string readText(const std::filesystem::path& file) { return file_io::readText(file, 4 * 1024 * 1024); }
uint64_t unixMs() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
}
}

std::wstring steamTrailerUrl(uint32_t app_id, std::string_view appdetails) {
    const auto root = json::parse(appdetails);
    const auto* app = root ? root->get(std::to_string(app_id)) : nullptr;
    const auto* data = app ? app->get("data") : nullptr;
    const auto* movies = data ? data->get("movies") : nullptr;
    if (!movies || movies->type != json::Value::Type::array) return {};
    // The store's highlighted trailer first, then the others in their order.
    for (const bool highlight : {true, false})
        for (const auto& movie : movies->elements) {
            const auto* flag = movie.get("highlight");
            if (highlight && !(flag && flag->flag)) continue;
            const auto* mp4 = movie.get("mp4");
            if (!mp4) continue;
            for (const char* quality : {"480", "max"}) {
                auto url = mp4->get(quality) ? mp4->get(quality)->stringOr() : std::string{};
                if (url.starts_with("http://")) url.insert(4, 1, 's');
                if (url.starts_with("https://") && url.size() < 2048 && url.find_first_of(" \r\n\"<>") == std::string::npos)
                    return text_encoding::wide(url);
            }
        }
    return {};
}

std::wstring steamTrailerDashUrl(uint32_t app_id, std::string_view appdetails) {
    const auto root = json::parse(appdetails);
    const auto* app = root ? root->get(std::to_string(app_id)) : nullptr;
    const auto* data = app ? app->get("data") : nullptr;
    const auto* movies = data ? data->get("movies") : nullptr;
    if (!movies || movies->type != json::Value::Type::array) return {};
    for (const bool highlight : {true, false})
        for (const auto& movie : movies->elements) {
            const auto* flag = movie.get("highlight");
            if (highlight && !(flag && flag->flag)) continue;
            const auto* dash = movie.get("dash_h264");
            auto url = dash ? dash->stringOr() : std::string{};
            if (url.starts_with("http://")) url.insert(4, 1, 's');
            if (url.starts_with("https://") && url.size() < 2048 && url.find_first_of(" \r\n\"<>") == std::string::npos)
                return text_encoding::wide(url);
        }
    return {};
}

namespace {
std::string attribute(std::string_view element, std::string_view name) {
    const std::string key = " " + std::string(name) + "=\"";
    const auto start = element.find(key);
    if (start == std::string_view::npos) return {};
    const auto begin = start + key.size(), end = element.find('"', begin);
    return end == std::string_view::npos ? std::string{} : std::string(element.substr(begin, end - begin));
}
long long integer(const std::string& text) { try { return text.empty() ? 0 : std::stoll(text); } catch (...) { return 0; } }
// ISO 8601 durations as DASH writes them: PT1M45.6S, PT2H3M, PT30S.
double seconds(std::string_view duration) {
    if (!duration.starts_with("PT")) return 0;
    double total = 0, value = 0, fraction = 0;
    bool decimals = false;
    for (const char ch : duration.substr(2)) {
        if (ch >= '0' && ch <= '9') { if (decimals) { fraction /= 10; value += (ch - '0') * fraction; } else value = value * 10 + (ch - '0'); }
        else if (ch == '.') { decimals = true; fraction = 1; }
        else { total += value * (ch == 'H' ? 3600 : ch == 'M' ? 60 : 1); value = 0; decimals = false; }
    }
    return total;
}
std::string expand(std::string pattern, const std::string& id, long long number) {
    const auto replace = [&](std::string_view token, const std::string& with) {
        for (size_t at = 0; (at = pattern.find(token, at)) != std::string::npos; at += with.size()) pattern.replace(at, token.size(), with);
    };
    replace("$RepresentationID$", id);
    char padded[32]{};
    snprintf(padded, sizeof padded, "%05lld", number);
    replace("$Number%05d$", padded);
    replace("$Number$", std::to_string(number));
    return pattern;
}
}

std::vector<std::wstring> dashVideoSegments(std::string_view mpd, std::wstring_view mpd_url, int max_height,
                                            double start_seconds, double clip_seconds) {
    // Segments sit beside the MPD: its URL without the file name and query.
    std::wstring base(mpd_url.substr(0, mpd_url.find(L'?')));
    base = base.substr(0, base.rfind(L'/') + 1);
    const double total = seconds(attribute(mpd.substr(0, mpd.find('>', mpd.find("<MPD"))), "mediaPresentationDuration"));
    std::string chosen_id, chosen_template;
    int chosen_height = 0;
    for (size_t at = mpd.find("<AdaptationSet"); at != std::string_view::npos; at = mpd.find("<AdaptationSet", at + 1)) {
        const auto end = mpd.find("</AdaptationSet>", at);
        if (end == std::string_view::npos) break;
        const auto set = mpd.substr(at, end - at);
        if (attribute(set.substr(0, set.find('>')), "contentType") != "video") continue;
        for (size_t r = set.find("<Representation"); r != std::string_view::npos; r = set.find("<Representation", r + 1)) {
            const auto close = set.find("</Representation>", r);
            const auto block = set.substr(r, close == std::string_view::npos ? std::string_view::npos : close - r);
            const auto head = block.substr(0, block.find('>'));
            const int height = static_cast<int>(integer(attribute(head, "height")));
            // The tallest at most max_height; failing that, the smallest there is.
            const bool better = height <= max_height ? (chosen_height > max_height || height > chosen_height) : (!chosen_height || height < chosen_height);
            if (!chosen_id.empty() && !better) continue;
            const auto segment = block.find("<SegmentTemplate");
            if (segment == std::string_view::npos) continue;
            chosen_id = attribute(head, "id");
            chosen_template = std::string(block.substr(segment, block.find('>', segment) - segment));
            chosen_height = height;
        }
    }
    if (chosen_id.empty() || total <= 0) return {};
    const auto timescale = std::max<long long>(1, integer(attribute(chosen_template, "timescale")));
    const auto duration = integer(attribute(chosen_template, "duration"));
    const auto first = std::max<long long>(1, integer(attribute(chosen_template, "startNumber")));
    const auto init = attribute(chosen_template, "initialization"), media = attribute(chosen_template, "media");
    if (duration <= 0 || init.empty() || media.empty()) return {};
    const auto count = static_cast<long long>(std::ceil(total * timescale / duration));
    if (count <= 0 || count > 2000) return {};
    // The clip: past the opening logos, at most clip_seconds; a short trailer gives its last part.
    const double segment = static_cast<double>(duration) / timescale;
    const auto wanted = std::max<long long>(1, static_cast<long long>(std::ceil(clip_seconds / segment)));
    auto skip = static_cast<long long>(std::floor(start_seconds / segment));
    if (skip + wanted > count) skip = std::max<long long>(0, count - wanted);
    const auto last = std::min(count, skip + wanted);
    std::vector<std::wstring> urls{base + text_encoding::wide(expand(init, chosen_id, 0))};
    for (long long number = first + skip; number < first + last; ++number) urls.push_back(base + text_encoding::wide(expand(media, chosen_id, number)));
    for (const auto& url : urls) if (!url.starts_with(L"https://") || url.find_first_of(L" \r\n\"<>") != std::wstring::npos) return {};
    return urls;
}

namespace {
uint32_t be32(const uint8_t* at) { return uint32_t(at[0]) << 24 | uint32_t(at[1]) << 16 | uint32_t(at[2]) << 8 | at[3]; }
uint64_t be64(const uint8_t* at) { return uint64_t(be32(at)) << 32 | be32(at + 4); }
void put32(uint8_t* at, uint32_t value) { for (int i = 0; i < 4; ++i) at[i] = static_cast<uint8_t>(value >> (24 - 8 * i)); }
void put64(uint8_t* at, uint64_t value) { put32(at, static_cast<uint32_t>(value >> 32)); put32(at + 4, static_cast<uint32_t>(value)); }
// Every tfdt inside moof/traf: its baseMediaDecodeTime (v0 32-bit, v1 64-bit).
bool rebaseBoxes(uint8_t* data, size_t size, std::optional<uint64_t>& origin, int depth) {
    for (size_t at = 0; at + 8 <= size;) {
        const uint32_t length = be32(data + at);
        if (length < 8 || at + length > size || depth > 4) return false;
        const std::string_view type(reinterpret_cast<const char*>(data + at + 4), 4);
        if (type == "traf" && !rebaseBoxes(data + at + 8, length - 8, origin, depth + 1)) return false;
        if (type == "tfdt" && length >= 16) {
            const bool wide = data[at + 8] == 1;
            if (wide && length < 20) return false;
            const uint64_t time = wide ? be64(data + at + 12) : be32(data + at + 12);
            if (!origin) origin = time;
            const uint64_t moved = time >= *origin ? time - *origin : 0;
            if (wide) put64(data + at + 12, moved); else put32(data + at + 12, static_cast<uint32_t>(moved));
        }
        at += length;
    }
    return true;
}
}

std::vector<uint8_t> rebaseDashSegment(std::span<const uint8_t> segment, std::optional<uint64_t>& origin) {
    std::vector<uint8_t> result;
    for (size_t at = 0; at + 8 <= segment.size();) {
        const uint32_t length = be32(segment.data() + at);
        if (length < 8 || at + length > segment.size()) return {}; // 64-bit sizes are not used by these segments.
        const std::string_view type(reinterpret_cast<const char*>(segment.data() + at + 4), 4);
        if (type == "moof" || type == "mdat") {
            const auto start = result.size();
            result.insert(result.end(), segment.begin() + at, segment.begin() + at + length);
            if (type == "moof" && !rebaseBoxes(result.data() + start + 8, length - 8, origin, 0)) return {};
        }
        at += length;
    }
    return result;
}

namespace {
std::atomic_bool trailer_downloading{false};
}

bool tryBeginTrailerDownload() { return !trailer_downloading.exchange(true); }
void endTrailerDownload() { trailer_downloading.store(false); }

void trimTrailers(std::span<const std::filesystem::path> folders, uint64_t limit_bytes) {
    struct Item { std::filesystem::path file; std::filesystem::file_time_type played; uint64_t size; };
    std::vector<Item> items;
    uint64_t total = 0;
    for (const auto& videos : folders) {
        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator(videos, ec)) {
            std::error_code item;
            if (!entry.is_regular_file(item) || entry.path().extension() != L".mp4") continue;
            const auto size = entry.file_size(item);
            items.push_back({entry.path(), entry.last_write_time(item), size});
            total += size;
        }
    }
    std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) { return a.played < b.played; });
    for (const auto& item : items) {
        if (total <= limit_bytes) break;
        std::error_code removed;
        if (std::filesystem::remove(item.file, removed)) total -= item.size;
    }
}

void trimTrailers(const std::filesystem::path& videos, uint64_t limit_bytes) {
    const std::array folders{videos};
    trimTrailers(folders, limit_bytes);
}

std::filesystem::path steamTrailerFile(uint32_t app_id, const std::filesystem::path& config_root, const std::filesystem::path& folder,
                                       uint64_t limit_bytes, const TrailerDownload& download) {
    if (!app_id || folder.empty()) return {};
    const auto videos = folder / L"steam" / L"videos";
    const auto target = videos / (std::to_wstring(app_id) + L".clip.mp4");
    std::error_code ec;
    std::filesystem::remove(videos / (std::to_wstring(app_id) + L".mp4"), ec); // A whole trailer from before the clips.
    if (std::filesystem::is_regular_file(target, ec) && std::filesystem::file_size(target, ec) > 1024) {
        std::filesystem::last_write_time(target, std::filesystem::file_time_type::clock::now(), ec); // Played: kept longest.
        return target;
    }
    auto failed = target; failed += L".none";
    {
        std::ifstream marker(failed);
        uint64_t since = 0;
        if (marker >> since && unixMs() - since < 3ull * 24 * 60 * 60 * 1000) return {};
    }
    const auto metadata = config_root / L"metadata";
    // Any language's copy (fetchGameDetails keeps one per UI language): the movies are the same.
    const auto cached = [&] {
        std::string text;
        for (const auto* suffix : {L".details.json", L".en.details.json", L".pt-BR.details.json", L".es.details.json"})
            if (text = readText(metadata / (std::to_wstring(app_id) + suffix)); !text.empty()) break;
        return text;
    };
    auto details = cached();
    if (details.empty()) { // The store page's details, cached for the details screen as well.
        fetchGameDetails(app_id, metadata);
        details = cached();
    }
    const auto url = steamTrailerUrl(app_id, details);
    const auto dash = url.empty() ? steamTrailerDashUrl(app_id, details) : std::wstring{};
    if (url.empty() && dash.empty()) {
        if (!details.empty()) atomic_file::write(failed, std::to_string(unixMs())); // No trailer on the page.
        return {};
    }
    if (!tryBeginTrailerDownload()) return {};
    struct Release { ~Release() { endTrailerDownload(); } } release;
    HttpResponse response;
    if (!url.empty()) response = download ? download(url, target)
                                          : httpDownloadFile(url, L"Accept: video/mp4\r\n", target, 160 * 1024 * 1024, 5 * 60 * 1000, false);
    else {
        // DASH: the video track's segments joined into one fragmented MP4 (init segment first).
        const auto manifest = httpGet(dash, L"Accept: application/dash+xml\r\n", 1024 * 1024);
        const auto segments = manifest.status == 200 ?
            dashVideoSegments(std::string_view(reinterpret_cast<const char*>(manifest.body.data()), manifest.body.size()), dash) : std::vector<std::wstring>{};
        auto part = target; part += L".part";
        std::filesystem::create_directories(target.parent_path(), ec);
        std::ofstream output(part, std::ios::binary | std::ios::trunc);
        size_t written = 0, index = 0;
        bool complete = segments.size() > 1 && output.is_open();
        std::optional<uint64_t> origin;
        for (const auto& segment : segments) {
            auto piece = download ? download(segment, {}) : httpGet(segment, L"Accept: video/mp4\r\n", 16 * 1024 * 1024);
            // The last segment the duration promises may not exist: a 404 after the first ones ends the file.
            if (piece.status == 404 && index > 1) break;
            if (piece.status != 200 || piece.body.empty() || (written += piece.body.size()) > 64 * 1024 * 1024) { complete = false; break; }
            // The init segment as it is; media segments moved to start at 0, or the clip would wait for its time.
            if (index) piece.body = rebaseDashSegment(piece.body, origin);
            if (piece.body.empty()) { complete = false; break; }
            output.write(reinterpret_cast<const char*>(piece.body.data()), static_cast<std::streamsize>(piece.body.size()));
            ++index;
        }
        output.close();
        complete = complete && !output.fail() && atomic_file::replace(part, target);
        if (!complete) std::filesystem::remove(part, ec);
        response.status = complete ? 200 : 0;
    }
    auto part = target; part += L".part";
    if (response.status != 200) {
        if (!std::filesystem::exists(part, ec)) atomic_file::write(failed, std::to_string(unixMs())); // Interrupted ones resume.
        return {};
    }
    char head[8]{};
    { std::ifstream input(target, std::ios::binary); input.read(head, 8); }
    if (std::filesystem::file_size(target, ec) > 1024 && std::memcmp(head + 4, "ftyp", 4) == 0) {
        video_manifest::add(folder, target);
        const std::array trailer_folders{videos, folder / L"nintendo" / L"videos"};
        trimTrailers(trailer_folders, limit_bytes); // Oldest-played go first; this one is the newest.
        return target;
    }
    std::filesystem::remove(target, ec);
    atomic_file::write(failed, std::to_string(unixMs()));
    return {};
}
