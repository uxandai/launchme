#include "nintendo_trailer.hpp"

#include "../library/emulation_scan.hpp"
#include "../library/metadata_sources.hpp"
#include "../net/json.hpp"
#include "../net/url_encoding.hpp"
#include "../platform/atomic_file.hpp"
#include "../platform/text_encoding.hpp"
#include "../platform/video_manifest.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <cwctype>
#include <fstream>
#include <set>

namespace {
uint64_t unixMs() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
}
constexpr uint64_t miss_lifetime_ms = 7ull * 24 * 60 * 60 * 1000;
constexpr uint64_t failed_download_ms = 3ull * 24 * 60 * 60 * 1000;
constexpr uintmax_t min_intro_clip_bytes = 24 * 1024; // Shorter trailers past so_10 return a tiny tail box (~13 KB).

// Public read-only search credentials embedded in nintendo.com/us's web storefront.
constexpr const wchar_t* algolia_url = L"https://u3b6gr4ua3-dsn.algolia.net/1/indexes/store_game_en_us?hitsPerPage=12&query=";
constexpr const wchar_t* algolia_headers =
    L"X-Algolia-Application-Id: U3B6GR4UA3\r\n"
    L"X-Algolia-API-Key: a29c6927638bfd8cee23993e51e721c9\r\n"
    L"Accept: application/json\r\n";

bool validPublicId(std::string_view id) {
    if (id.empty() || id.size() >= 256 || id.find("..") != std::string_view::npos || id.find("/Video/") == std::string_view::npos)
        return false;
    return std::all_of(id.begin(), id.end(), [](char ch) {
        return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') ||
               ch == '/' || ch == '_' || ch == '-';
    });
}

bool separatorChar(wchar_t ch) {
    return ch == L' ' || ch == L'\t' || ch == 0xA0 || ch == L'-' || ch == L'–' || ch == L'—' || ch == L':';
}

// Switch 2 re-releases append "– Nintendo Switch™ 2 Edition" (or "for Nintendo Switch 2"); stripping
// that store label lets "Hollow Knight: Silksong" match its Switch 2 entry without loosening sequels.
std::wstring stripNintendoEdition(std::wstring_view input) {
    std::wstring clean;
    clean.reserve(input.size());
    for (wchar_t ch : input) {
        if (ch == L'™' || ch == L'®' || ch == L'©') continue;
        clean += ch == 0xA0 ? L' ' : ch;
    }
    while (!clean.empty() && separatorChar(clean.back())) clean.pop_back();
    std::wstring lower = clean;
    for (auto& ch : lower) ch = static_cast<wchar_t>(std::towlower(ch));
    static constexpr std::wstring_view suffixes[] = {
        L"nintendo switch 2 edition",
        L"nintendo switch edition",
        L"for nintendo switch 2",
        L"for nintendo switch",
    };
    for (const auto suffix : suffixes) {
        if (lower.size() <= suffix.size() + 1 || !lower.ends_with(suffix)) continue;
        const size_t cut = lower.size() - suffix.size();
        if (!separatorChar(clean[cut - 1])) continue;
        size_t end = cut;
        while (end > 0 && separatorChar(clean[end - 1])) --end;
        if (end > 0) return clean.substr(0, end);
    }
    return clean;
}

int nintendoTitleScore(std::wstring_view query, std::wstring_view candidate) {
    return std::max(gridTitleScore(query, candidate),
                    gridTitleScore(stripNintendoEdition(query), stripNintendoEdition(candidate)));
}

std::string firstVideoPublicId(const json::Value& hit) {
    const auto* gallery = hit.get("productGallery");
    if (!gallery || gallery->type != json::Value::Type::array) return {};
    for (const auto& item : gallery->elements) {
        const auto* kind = item.get("resourceType");
        const auto* pub = item.get("publicId");
        if (!kind || kind->stringOr() != "video" || !pub || pub->type != json::Value::Type::string) continue;
        auto id = pub->text;
        while (!id.empty() && id.front() == '/') id.erase(id.begin());
        if (validPublicId(id)) return id;
    }
    return {};
}

std::string switchTitleIdForGame(const Game& game) {
    if (!game.rom.empty()) {
        if (auto id = metadata_sources::titleId(game.rom.filename().wstring()); !id.empty()) return id;
        if (auto id = metadata_sources::titleId(game.rom.parent_path().filename().wstring()); !id.empty()) return id;
        if (auto id = metadata_sources::titleId(emulation_scan::switch2Folder(game.rom).filename().wstring()); !id.empty()) return id;
    }
    if (!game.install_directory.empty())
        if (auto id = metadata_sources::titleId(game.install_directory.filename().wstring()); !id.empty()) return id;
    return metadata_sources::titleId(game.name);
}

bool sameHexId(std::wstring_view title, std::string_view hex_id) {
    if (hex_id.empty()) return false;
    std::string compact;
    for (wchar_t ch : title) {
        if (ch == L' ' || ch == L'[' || ch == L']' || ch == L'(' || ch == L')' || ch == L'-' || ch == L'_') continue;
        if (ch > 0x7F) return false;
        compact += static_cast<char>(std::towupper(ch));
    }
    return compact == hex_id;
}

bool validMp4File(const std::filesystem::path& file, uintmax_t min_bytes) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(file, ec) || std::filesystem::file_size(file, ec) < min_bytes) return false;
    char head[8]{};
    std::ifstream input(file, std::ios::binary);
    input.read(head, sizeof head);
    return input.gcount() == sizeof head && std::memcmp(head + 4, "ftyp", 4) == 0;
}
} // namespace

std::string matchNintendoTrailerId(std::wstring_view title, bool switch2, std::string_view algolia_json) {
    const auto parsed = json::parse(algolia_json, 2 * 1024 * 1024);
    const auto* hits = parsed ? parsed->get("hits") : nullptr;
    if (!hits || hits->type != json::Value::Type::array) return {};

    struct Candidate {
        std::string nsuid;
        std::string video_id;
        int score = 0;
        bool switch2 = false;
    };
    std::vector<Candidate> candidates;
    std::set<std::string> seen;
    for (const auto& hit : hits->elements) {
        if (const auto* upgrade = hit.get("isUpgrade"); upgrade && upgrade->type == json::Value::Type::boolean && upgrade->flag)
            continue;
        if (const auto* dlc = hit.get("dlcType"); dlc && dlc->type == json::Value::Type::string && !dlc->text.empty())
            continue;
        const auto nsuid = hit.get("nsuid") ? hit.get("nsuid")->stringOr() : std::string{};
        // Full digital games in the eShop carry 7001… NSUIDs; 7005… are DLC/upgrade packs, 7007… bundles,
        // and physical-only merchandise entries carry no NSUID.
        if (!nsuid.starts_with("7001") || !seen.insert(nsuid).second) continue;
        const auto platform = hit.get("platformCode") ? hit.get("platformCode")->stringOr() : std::string{};
        if (platform != "NINTENDO_SWITCH" && platform != "NINTENDO_SWITCH_2") continue;
        const auto* name = hit.get("title");
        if (!name || name->type != json::Value::Type::string) continue;
        const int score = nintendoTitleScore(title, text_encoding::wide(name->text));
        if (score < 880) continue;
        candidates.push_back({nsuid, firstVideoPublicId(hit), score, platform == "NINTENDO_SWITCH_2"});
    }

    const auto pickForPlatform = [&](bool wanted_switch2) -> std::string {
        int first = 0, second = 0;
        std::string chosen;
        for (const auto& item : candidates) {
            if (item.switch2 != wanted_switch2) continue;
            if (item.score > first) {
                second = first;
                first = item.score;
                chosen = item.video_id;
            } else {
                second = std::max(second, item.score);
            }
        }
        return first >= 880 && first - second >= 35 ? chosen : std::string{};
    };
    if (auto id = pickForPlatform(switch2); !id.empty()) return id;
    return pickForPlatform(!switch2);
}

std::string nintendoTrailerIdForGame(const Game& game, const std::filesystem::path& metadata, const ArtworkHttp& get) {
    const auto cache_key = gameCacheKey(game);
    const auto raw_title = artworkTitle(game);
    if (cache_key.empty() || raw_title.empty()) return {};

    const auto cache_file = metadata / (text_encoding::wide(cache_key) + L".nintendotrailer");
    {
        std::ifstream saved(cache_file);
        std::string id;
        uint64_t at = 0;
        if (saved >> id >> at) {
            if (id != "-" && validPublicId(id)) return id;
            if (id == "-" && unixMs() - at < miss_lifetime_ms) return {};
        }
    }

    const bool switch2 = game.platform == L"Switch 2";
    const auto switch_id = switchTitleIdForGame(game);
    auto title = emulation_scan::displayTitle(raw_title);
    if (title.empty()) title = raw_title;

    bool answered = false;
    const auto askCatalog = [&](const std::wstring& query_title) -> std::optional<std::string> {
        const auto reply = get(algolia_url + url_encoding::component(std::wstring_view(query_title)),
                               algolia_headers, 1024 * 1024);
        if (reply.status != 200) return std::nullopt;
        answered = true;
        return matchNintendoTrailerId(query_title, switch2,
                                      std::string_view(reinterpret_cast<const char*>(reply.body.data()), reply.body.size()));
    };

    std::string matched;
    if (!sameHexId(title, switch_id)) {
        const auto first = askCatalog(title);
        if (!first) return {}; // Transient network error: retry next time.
        matched = *first;
    }

    if (matched.empty() && !switch_id.empty()) {
        const auto shop = metadata_sources::find(metadata_sources::Source::nintendo, metadata_sources::Query{{}, switch_id}, get);
        if (!shop.offers.empty() && !shop.offers.front().title.empty()) {
            const auto& shop_title = shop.offers.front().title;
            if (sameHexId(title, switch_id) || nintendoTitleScore(title, shop_title) < 960) {
                const auto second = askCatalog(shop_title);
                if (!second) return {};
                matched = *second;
            }
        }
    }

    if (!answered) return {};
    std::error_code ec;
    std::filesystem::create_directories(metadata, ec);
    atomic_file::write(cache_file, (matched.empty() ? std::string("-") : matched) + " " + std::to_string(unixMs()));
    return matched;
}

std::wstring nintendoTrailerClipUrl(std::string_view public_id, bool skip_intro) {
    while (!public_id.empty() && public_id.front() == '/') public_id.remove_prefix(1);
    if (!validPublicId(public_id)) return {};
    return std::wstring(L"https://assets.nintendo.com/video/upload/") +
           (skip_intro ? L"so_10,du_30,h_480,ac_none/" : L"du_30,h_480,ac_none/") +
           text_encoding::wide(public_id) + L".mp4";
}

std::filesystem::path nintendoTrailerFile(const Game& game, const std::filesystem::path& config_root,
                                          const std::filesystem::path& folder, uint64_t limit_bytes,
                                          const ArtworkHttp& get, const TrailerDownload& download) {
    const auto cache_key = gameCacheKey(game);
    if (!nintendoSwitchGame(game) || folder.empty() || cache_key.empty()) return {};

    const auto videos = folder / L"nintendo" / L"videos";
    const auto target = videos / (text_encoding::wide(cache_key) + L".clip.mp4");
    std::error_code ec;
    if (std::filesystem::is_regular_file(target, ec) && std::filesystem::file_size(target, ec) > 1024) {
        std::filesystem::last_write_time(target, std::filesystem::file_time_type::clock::now(), ec);
        return target;
    }

    auto failed = target;
    failed += L".none";
    {
        std::ifstream marker(failed);
        uint64_t since = 0;
        if (marker >> since && unixMs() - since < failed_download_ms) return {};
    }

    const auto metadata = config_root / L"metadata";
    const auto public_id = nintendoTrailerIdForGame(game, metadata, get);
    if (public_id.empty()) return {};

    if (!tryBeginTrailerDownload()) return {};
    struct Release { ~Release() { endTrailerDownload(); } } release;

    const auto fetch = [&](bool skip_intro) {
        const auto url = nintendoTrailerClipUrl(public_id, skip_intro);
        if (url.empty()) return HttpResponse{};
        return download ? download(url, target)
                        : httpDownloadFile(url, L"Accept: video/mp4\r\n", target, 96 * 1024 * 1024, 5 * 60 * 1000, false);
    };

    auto response = fetch(true);
    bool ok = response.status == 200 && validMp4File(target, min_intro_clip_bytes);
    if (!ok && (response.status == 200 || response.status == 400)) {
        std::filesystem::remove(target, ec);
        response = fetch(false);
        ok = response.status == 200 && validMp4File(target, 1025);
    }

    if (ok) {
        video_manifest::add(folder, target);
        const std::array trailer_folders{folder / L"steam" / L"videos", videos};
        trimTrailers(trailer_folders, limit_bytes);
        return target;
    }

    std::filesystem::remove(target, ec);
    if (response.status == 404)
        std::filesystem::remove(metadata / (text_encoding::wide(cache_key) + L".nintendotrailer"), ec);
    auto part = target;
    part += L".part";
    if (!std::filesystem::exists(part, ec) && response.status != 0)
        atomic_file::write(failed, std::to_string(unixMs()));
    return {};
}
