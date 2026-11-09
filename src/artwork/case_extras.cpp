#include "case_extras.hpp"
#include "esde_art.hpp"
#include "image_compact.hpp"
#include "../platform/atomic_file.hpp"
#include "../platform/file_io.hpp"
#include "../platform/unicode_text.hpp"

#include <algorithm>
#include <cstring>

namespace case_extras {
namespace {
constexpr size_t image_limit = 20 * 1024 * 1024, manual_limit = 160 * 1024 * 1024;
bool emulated(const Game& game) { return game.kind == GameKind::nonsteam && game.app_id && !game.platform.empty() && game.platform != L"PC"; }
// ScreenScraper's medium of this type, the player's region first.
const screenscraper::Media* pick(const screenscraper::GameInfo& info, std::wstring_view type, std::wstring_view format) {
    const screenscraper::Media* best = nullptr;
    int best_rank = 99;
    for (const auto& media : info.media) {
        if (media.type != type || media.url.empty()) continue;
        if (!format.empty() && !unicode_text::equalIgnoreCase(media.format, format)) continue;
        const int rank = media.region == L"eu" ? 0 : media.region == L"wor" ? 1 : media.region == L"us" ? 2 : media.region == L"ss" ? 3 : media.region == L"jp" ? 4 : 5;
        if (rank < best_rank) { best = &media; best_rank = rank; }
    }
    return best;
}
}

std::filesystem::path backCopy(const Game& game, const std::filesystem::path& config_root) {
    return config_root / L"emulation" / L"backs" / (std::to_wstring(game.app_id) + L".img");
}
std::filesystem::path sideCopy(const Game& game, const std::filesystem::path& config_root) {
    return config_root / L"emulation" / L"sides" / (std::to_wstring(game.app_id) + L".img");
}
std::filesystem::path manualCopy(const Game& game, const std::filesystem::path& config_root) {
    return config_root / L"emulation" / L"manuals" / (std::to_wstring(game.app_id) + L".pdf");
}

bool pdf(const std::vector<uint8_t>& bytes) { return bytes.size() > 64 && std::memcmp(bytes.data(), "%PDF", 4) == 0; }

namespace {
// A picture of the box: ES-DE's file in `esde_folder` (if any), the copy kept before, else ScreenScraper's `type`.
std::vector<uint8_t> boxImage(const Game& game, const std::filesystem::path& config_root, const screenscraper::Get& get, const wchar_t* esde_folder,
                              const std::filesystem::path& copy, std::wstring_view type) {
    if (!emulated(game)) return {};
    if (esde_folder)
        for (const auto& file : esdeMediaFiles(game.platform, game.name, game.rom, {esde_folder}))
            if (auto bytes = file_io::readBytes(file, image_limit); !bytes.empty()) return bytes;
    if (auto bytes = file_io::readBytes(copy, image_limit); !bytes.empty()) return bytes;
    const auto metadata = config_root / L"metadata";
    const auto cached = screenscraper::loadCached(game, metadata);
    if (!cached || !cached->game || screenscraper::paused(metadata)) return {};
    const auto* media = pick(*cached->game, type, {});
    const auto config = screenscraper::load();
    if (!media || !config.ready()) return {};
    uint32_t status = 0;
    auto bytes = screenscraper::downloadMedia(config, *media, get, &status);
    if (bytes.empty()) { screenscraper::pauseAfter(metadata, status); return {}; }
    writeCachedImage(copy, bytes);
    return bytes;
}
}

std::vector<uint8_t> back(const Game& game, const std::filesystem::path& config_root, const screenscraper::Get& get) {
    return boxImage(game, config_root, get, L"backcovers", backCopy(game, config_root), L"box-2D-back");
}

std::vector<uint8_t> side(const Game& game, const std::filesystem::path& config_root, const screenscraper::Get& get) {
    return boxImage(game, config_root, get, nullptr, sideCopy(game, config_root), L"box-2D-side");
}

std::filesystem::path manual(const Game& game, const std::filesystem::path& config_root, const screenscraper::Get& get) {
    if (!emulated(game)) return {};
    for (const auto& file : esdeMediaFiles(game.platform, game.name, game.rom, {L"manuals"}))
        if (unicode_text::equalIgnoreCase(file.extension().wstring(), L".pdf") && file_io::isFile(file)) return file;
    const auto copy = manualCopy(game, config_root);
    if (file_io::isFile(copy)) return copy;
    const auto metadata = config_root / L"metadata";
    const auto cached = screenscraper::loadCached(game, metadata);
    if (!cached || !cached->game || screenscraper::paused(metadata)) return {};
    const auto* media = pick(*cached->game, L"manuel", L"pdf");
    const auto config = screenscraper::load();
    if (!media || !config.ready()) return {};
    const auto address = screenscraper::mediaUrl(config, media->url);
    if (address.empty()) return {};
    auto response = get(address, L"Accept: application/pdf\r\n", manual_limit);
    if (response.status != 200 || !pdf(response.body)) { screenscraper::pauseAfter(metadata, response.status); return {}; }
    std::error_code ec;
    std::filesystem::create_directories(copy.parent_path(), ec);
    if (!atomic_file::write(copy, std::span<const uint8_t>(response.body))) return {};
    return copy;
}
}
