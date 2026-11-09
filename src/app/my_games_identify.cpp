// PC games added by hand ("Moje gry") are identified the way the DRM-free catalogue's are
// (drm_free::lookUp): the Steam AppID or GOG product their folder names, else their name and the
// program's product name in RAWG (with a key) and the Steam store. The details are saved under the
// game, its artwork source chosen; its name stays the one the user saved. Once for a game: a game
// without a match is asked again after a week (a marker file), one the user identified never.
#include "launcher.hpp"
#include "../artwork/cover_cache.hpp"
#include "../library/drm_free.hpp"
#include "../library/game_ratings.hpp"
#include "../platform/atomic_file.hpp"
#include "../platform/text_encoding.hpp"
#include "../platform/unix_time.hpp"

#include <algorithm>
#include <chrono>
#include <cwctype>
#include <fstream>
#include <thread>

namespace {
constexpr uint64_t retry_unmatched = 7 * 24 * 3600;
std::filesystem::path missFile(const Game& game, const std::filesystem::path& metadata) {
    return metadata / (text_encoding::wide(gameCacheKey(game)) + L".identify-miss");
}
bool triedRecently(const Game& game, const std::filesystem::path& metadata, uint64_t now) {
    std::ifstream input(missFile(game, metadata));
    uint64_t at = 0;
    return input >> at && at <= now && now < at + retry_unmatched;
}
} // namespace

void Launcher::identifyCustomGames(const std::vector<custom_games::Entry>& entries) {
    if (!runtime_started_) return;
    std::vector<std::pair<Game, custom_games::Entry>> waiting;
    for (const auto& entry : entries)
        if (entry.platform.empty() && !entry.path.empty()) waiting.emplace_back(custom_games::makeGame(entry, true), entry); // PC games only.
    if (waiting.empty()) return;
    work_.start([waiting = std::move(waiting), alive = alive_, hwnd = hwnd_] {
        const auto root = userConfigDirectory();
        const drm_free::IdentifyPaths paths{root / L"metadata", root / L"ratings", coverCacheDirectory()};
        drm_free::IdentifyBackend backend;
        backend.sgdb_key = userSecret(L"STEAMGRIDDB_API_KEY", L"steamgriddb_api_key.txt");
        auto rawg_key = userSecret(L"RAWG_API_KEY", L"rawg_api_key.txt");
        bool requested = false;
        const auto pace = [&] {
            // The DRM-free scan's spacing: the stores and RAWG see a person's pace, not a burst.
            if (requested)
                for (int i = 0; i < 38 && alive->load(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(20));
            requested = true;
            return alive->load();
        };
        const auto cancelled = [&] { return !alive->load(); };
        for (const auto& [game, entry] : waiting) {
            if (!alive->load()) return;
            const auto now = unix_time::now();
            // Identified already (here, or by the user in Zidentyfikuj grę), or no match lately.
            if (rawg::load(game, paths.metadata) || drm_free::loadStoreDetails(game, paths.metadata) || triedRecently(game, paths.metadata, now)) continue;
            auto extension = entry.path.extension().wstring();
            for (auto& ch : extension) ch = static_cast<wchar_t>(std::towlower(ch));
            auto lookup = extension == L".exe" ? drm_free::describeExecutable(entry.path) : drm_free::Lookup{};
            // The user's name is the title; what the folder's own name said is the second query.
            const auto described = lookup.title.text;
            lookup.title = {entry.name, drm_free::cleanTitle(entry.name).year};
            if (lookup.product.empty() && ratings::normalizedTitle(described) != ratings::normalizedTitle(entry.name)) lookup.product = described;
            auto identity = drm_free::lookUp(lookup, rawg_key, backend, pace);
            if (identity.rawg_refused) {
                rawg_key.clear();
                if (!identity.failed && identity.source == drm_free::Identity::Source::none) identity = drm_free::lookUp(lookup, rawg_key, backend, pace);
            }
            if (identity.failed || !alive->load()) return; // The network: asked again at the next start.
            if (identity.source == drm_free::Identity::Source::none) {
                std::error_code ec;
                std::filesystem::create_directories(paths.metadata, ec);
                atomic_file::write(missFile(game, paths.metadata), std::to_string(now));
                continue;
            }
            const bool artwork = drm_free::applyIdentity(game, identity, paths, backend, cancelled);
            auto message = std::make_unique<RemoteMessage>();
            std::optional<rawg::Metadata> metadata;
            if (identity.rawg.data) metadata = *identity.rawg.data;
            message->run = [key = gameKey(game), metadata = std::move(metadata), details = identity.details, artwork](Launcher& app) {
                app.customGameIdentified(key, metadata, details, artwork);
            };
            if (alive->load() && PostMessageW(hwnd, remote_message, 0, reinterpret_cast<LPARAM>(message.get()))) message.release();
        }
    });
}

void Launcher::customGameIdentified(const std::string& key, const std::optional<rawg::Metadata>& metadata, const GameDetails& details, bool artwork) {
    if (metadata) { rawg_metadata_[key] = *metadata; emulation_metadata_.erase(key); }
    else { emulation_metadata_[key] = details; rawg_metadata_.erase(key); }
    const auto found = std::find_if(custom_library_.begin(), custom_library_.end(), [&](const Game& game) { return gameKey(game) == key; });
    if (artwork && found != custom_library_.end()) forgetCustomArtwork(*found, false); // Its new source's artwork loads next.
    if (screen_ == ScreenMode::details && !detail_games_.empty() && gameKey(detail_games_[0]) == key)
        if (const auto* local = localDetails(detail_games_[0])) details_ = *local;
    InvalidateRect(hwnd_, nullptr, FALSE);
}
