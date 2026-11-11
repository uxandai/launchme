#include "launcher.hpp"
#include "../artwork/animated_image.hpp"
#include "../artwork/artwork_sources.hpp"
#include "../artwork/cover_cache.hpp"
#include "../artwork/esde_art.hpp"
#include "../artwork/screenscraper_art.hpp"
#include "../artwork/wallpaper_sources.hpp"
#include "../platform/atomic_file.hpp"
#include "../platform/file_io.hpp"
#include "../platform/text_encoding.hpp"
#include <algorithm>
#include <cstdlib>
#include <fstream>

namespace {
std::vector<uint8_t> cachedImage(const std::filesystem::path& file) {
    std::ifstream input(file, std::ios::binary | std::ios::ate);
    if (!input || input.tellg() < 128 || input.tellg() > 20 * 1024 * 1024) return {};
    std::vector<uint8_t> bytes(static_cast<size_t>(input.tellg()));
    input.seekg(0);
    if (!input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())) || !decodableArtwork(bytes)) return {};
    return bytes;
}
void saveThumbnail(const std::filesystem::path& file, const std::vector<uint8_t>& bytes) {
    atomic_file::write(file, bytes);
}
// An animated SteamGridDB asset's tile: its first seconds (kept in thumbnails\<type>\<id>.webp)
// decoded as a short loop about the tile's size, or its first frame where it does not decode.
void animatedThumbnail(const GridAsset& asset, ArtworkKind kind, const std::filesystem::path& folder, ArtworkBrowserMessage& result) {
    const auto file = folder / (std::to_wstring(asset.id) + L".webp");
    auto bytes = file_io::readBytes(file, animated_preview_bytes + 1024);
    if (!animated_image::animatedWebp(bytes) || animated_image::wholeFrames(bytes).empty()) {
        bytes = downloadAnimatedPreview(asset);
        if (!bytes.empty()) saveThumbnail(file, bytes);
    }
    if (bytes.empty()) return;
    const uint32_t edge = kind == ArtworkKind::hero ? 640 : kind == ArtworkKind::landscape ? 480 : 360;
    // About two to three seconds at 10 frames a second, at most 8 MB of pixels a tile.
    if (auto loop = animated_image::decodeLoop(bytes, edge, edge, 8u << 20, 30, 100))
        result.animation = std::make_shared<const animated_image::Frames>(std::move(*loop));
    else result.bytes = animatedStill(bytes);
}
CoverMessage::Kind coverKind(ArtworkKind kind) {
    switch (kind) {
    case ArtworkKind::cover: return CoverMessage::Kind::cover;
    case ArtworkKind::hero: return CoverMessage::Kind::hero;
    case ArtworkKind::poster: return CoverMessage::Kind::poster;
    case ArtworkKind::landscape: return CoverMessage::Kind::landscape;
    case ArtworkKind::disc: return CoverMessage::Kind::disc;
    default: return CoverMessage::Kind::logo;
    }
}
uint32_t fnv(std::wstring_view text) {
    uint32_t hash = 2166136261u;
    for (const auto ch : text) { hash ^= static_cast<uint32_t>(ch); hash *= 16777619u; }
    return hash;
}
// Variant IDs of the other sources stay apart from SteamGridDB's and from disk picks (0x80000000).
constexpr uint32_t screenscraper_ids = 0x40000000u, esde_ids = 0x20000000u;

// ScreenScraper media types and ES-DE media folders per artwork slot, best first.
std::vector<std::wstring_view> screenscraperTypes(ArtworkKind kind) {
    switch (kind) {
    case ArtworkKind::cover: return {L"box-2D", L"box-3D", L"support-2D", L"mixrbv2", L"mixrbv1"};
    case ArtworkKind::poster: return {L"box-2D", L"box-3D"};
    case ArtworkKind::logo: return {L"wheel-hd", L"wheel", L"wheel-carbon", L"wheel-steel"};
    default: return {L"fanart", L"ss", L"sstitle"};
    }
}
std::vector<const wchar_t*> esdeFolders(ArtworkKind kind) {
    switch (kind) {
    case ArtworkKind::cover: return {L"covers", L"box2dfront", L"3dboxes", L"miximages", L"physicalmedia"};
    case ArtworkKind::poster: return {L"covers", L"box2dfront", L"3dboxes"};
    case ArtworkKind::logo: return {L"marquees", L"wheel"};
    default: return {L"fanart", L"screenshots", L"titlescreens", L"miximages"};
    }
}
GridPage providerPage(const Game& game, ArtworkKind kind, ArtworkProvider provider) {
    GridPage page;
    page.provider = provider;
    std::unordered_set<uint32_t> seen;
    if (provider == ArtworkProvider::local) {
        // The player's own files of this type, newest first; the picture is the variant itself.
        const auto cache = coverCacheDirectory();
        for (const auto id : localArtworkIds(game, kind, cache)) {
            GridAsset asset;
            asset.id = id;
            asset.url = asset.thumbnail = artworkVariantPath(game, kind, id, cache).wstring();
            asset.author = tr(L"Z dysku");
            page.assets.push_back(std::move(asset));
        }
        if (page.assets.empty()) page.error = tr(L"Brak własnych grafik tego typu. Dodaj plik przez „Z dysku…”.");
    } else if (provider == ArtworkProvider::screenscraper) {
        auto gallery = kind == ArtworkKind::disc ? screenscraperDiscGallery(game, userConfigDirectory()) :
            screenscraperGallery(game, screenscraperTypes(kind), userConfigDirectory());
        page.error = std::move(gallery.error);
        for (const auto& media : gallery.media) {
            if (kind == ArtworkKind::logo && _wcsicmp(media.format.c_str(), L"png") != 0) continue; // Drawn over art: transparency.
            GridAsset asset;
            asset.id = screenscraper_ids | (fnv(media.url) & 0x1fffffffu);
            if (!seen.insert(asset.id).second) continue;
            asset.url = asset.thumbnail = media.url;
            asset.author = L"ScreenScraper · " + media.type + (media.region.empty() ? L"" : L" (" + media.region + L")");
            page.assets.push_back(std::move(asset));
        }
    } else {
        for (const auto& file : esdeMediaFiles(game.platform, game.name, game.rom, esdeFolders(kind))) {
            GridAsset asset;
            asset.id = esde_ids | (fnv(file.wstring()) & 0x1fffffffu);
            if (!seen.insert(asset.id).second) continue;
            asset.url = asset.thumbnail = file.wstring();
            asset.author = L"ES-DE · " + file.parent_path().filename().wstring();
            page.assets.push_back(std::move(asset));
        }
        if (page.assets.empty()) page.error = tr(L"ES-DE nie ma grafik tej gry. Zescrapuj ją w ES-DE.");
    }
    return page;
}
}

void Launcher::initializeArtworkBrowser() {
    const auto queue = artwork_jobs_;
    const auto alive = alive_;
    const auto window = hwnd_;
    for (int i = 0; i < 2; ++i) work_.start([queue, alive, window] {
        const auto cache = coverCacheDirectory();
        while (alive->load()) {
            ArtworkBrowserJob job;
            {
                std::unique_lock lock(queue->mutex);
                queue->wake.wait(lock, [&] { return !alive->load() || !queue->jobs.empty(); });
                if (!alive->load()) return;
                job = std::move(queue->jobs.front());
                queue->jobs.pop_front();
            }
            if (queue->generation.load() != job.generation) continue;
            auto result = std::make_unique<ArtworkBrowserMessage>();
            result->type = job.type;
            result->generation = job.generation;
            result->asset_id = job.asset.id;
            result->kind = job.kind;
            result->wallpaper = job.wallpaper;
            const auto wallpapers = userConfigDirectory() / L"wallpapers";
            if (job.type == ArtworkBrowserMessage::Type::search) {
                result->search = searchGridGames(job.query, userSecret(L"STEAMGRIDDB_API_KEY", L"steamgriddb_api_key.txt"));
            } else if (job.type == ArtworkBrowserMessage::Type::preview) {
                result->file = wallpaper_sources::preview(job.asset, wallpapers);
                result->video = true;
            } else if (job.wallpaper && job.type == ArtworkBrowserMessage::Type::local) {
                // A picture or video from disk becomes the wallpaper (copied into <config>\wallpapers).
                bool video = false;
                result->file = wallpaper_sources::storeLocal(std::filesystem::path(job.query), wallpapers, video, result->error);
                result->video = video;
                result->asset_id = 0;
            } else if (job.wallpaper && job.type == ArtworkBrowserMessage::Type::apply) {
                result->file = wallpaper_sources::store(job.provider, job.asset, wallpapers, result->error);
                result->video = wallpaper_sources::video(job.provider);
            } else if (job.type == ArtworkBrowserMessage::Type::local) {
                // An image from disk becomes one more variant of this game, kept like a SteamGridDB pick.
                auto bytes = cachedImage(std::filesystem::path(job.query));
                if (bytes.empty()) result->error = tr(L"Wybierz obraz PNG lub JPEG (do 20 MB).");
                else {
                    uint32_t hash = 2166136261u;
                    for (const auto byte : bytes) { hash ^= byte; hash *= 16777619u; }
                    result->asset_id = 0x80000000u | (hash & 0x7fffffffu); // Above SteamGridDB asset IDs.
                    if (!cacheArtworkVariant(job.game, job.kind, result->asset_id, bytes, cache)) result->error = tr(L"Nie można zapisać grafiki w cache.");
                    else {
                        if (job.kind == ArtworkKind::cover) {
                            const auto file = artworkVariantPath(job.game, job.kind, result->asset_id, cache);
                            auto focus = file; focus.replace_extension(L".focus");
                            result->focus = cachedTextCrop(file, focus, job.game.name);
                        }
                        result->bytes = std::move(bytes);
                    }
                }
            } else if (job.type == ArtworkBrowserMessage::Type::list) {
                if (job.wallpaper) result->page = wallpaper_sources::browse(job.provider, job.query, job.page);
                else if (artwork_sources::own(job.provider)) {
                    artwork_sources::Context context{userConfigDirectory(), job.steam_app_id, job.query.empty() ? artworkTitle(job.game) : job.query,
                                                     userSecret(L"THEGAMESDB_API_KEY", L"thegamesdb_api_key.txt")};
                    result->page = artwork_sources::browse(job.provider, job.game, job.kind, context);
                } else result->page = job.provider == ArtworkProvider::steamgriddb ?
                    browseSteamGridDBGallery(job.game, job.kind, job.page, job.source, userSecret(L"STEAMGRIDDB_API_KEY", L"steamgriddb_api_key.txt")) :
                    providerPage(job.game, job.kind, job.provider);
                if (!job.wallpaper) result->applied_id = selectedArtworkId(job.game, job.kind, cache);
            } else if (job.wallpaper) {
                // A wallpaper's thumbnail, kept apart from the games' (thumbnails\wallpaper).
                const auto file = cache.parent_path() / L"artwork" / L"thumbnails" / L"wallpaper" / (std::to_wstring(job.asset.id) + L".img");
                result->bytes = cachedImage(file);
                if (result->bytes.empty()) {
                    result->bytes = wallpaper_sources::thumbnail(job.asset);
                    if (!result->bytes.empty()) saveThumbnail(file, result->bytes);
                }
            } else if (job.asset.animated && job.provider == ArtworkProvider::steamgriddb && job.type == ArtworkBrowserMessage::Type::thumbnail) {
                animatedThumbnail(job.asset, job.kind, cache.parent_path() / L"artwork" / L"thumbnails" / artworkKindSlug(job.kind), *result);
            } else if (job.asset.animated && job.provider == ArtworkProvider::steamgriddb) {
                // An animated pick: the whole animation beside its still first frame, the variant every view shows.
                const auto still = artworkVariantPath(job.game, job.kind, job.asset.id, cache);
                const auto motion = artworkAnimationPath(job.game, job.kind, job.asset.id, cache);
                std::error_code missing;
                result->bytes = std::filesystem::is_regular_file(motion, missing) ? cachedImage(still) : std::vector<uint8_t>{};
                if (result->bytes.empty()) {
                    if (!downloadAnimatedGrid(job.asset, motion)) result->error = tr(L"Nie można pobrać animowanej grafiki.");
                    else if (result->bytes = animatedStill(motion); result->bytes.empty() ||
                             !cacheArtworkVariant(job.game, job.kind, job.asset.id, result->bytes, cache)) {
                        result->bytes.clear();
                        result->error = tr(L"Nie można zapisać grafiki w cache.");
                    }
                }
                if (!result->bytes.empty() && job.kind == ArtworkKind::cover) {
                    auto focus = still;
                    focus.replace_extension(L".focus");
                    result->focus = cachedTextCrop(still, focus, job.game.name);
                }
            } else {
                const bool thumbnail = job.type == ArtworkBrowserMessage::Type::thumbnail;
                // ES-DE files are on disk already: no thumbnail copies of them.
                const bool esde = job.provider == ArtworkProvider::esde;
                const bool own_file = job.provider == ArtworkProvider::local; // The player's file, already a variant: no copy, no download.
                const auto file = (thumbnail && esde) || own_file ? (own_file ? artworkVariantPath(job.game, job.kind, job.asset.id, cache) : std::filesystem::path(job.asset.url)) : thumbnail ? cache.parent_path() / L"artwork" / L"thumbnails" /
                    artworkKindSlug(job.kind) / (std::to_wstring(job.asset.id) + L".img") :
                    artworkVariantPath(job.game, job.kind, job.asset.id, cache);
                result->bytes = cachedImage(file);
                if (result->bytes.empty() && !own_file) {
                    result->bytes = job.provider == ArtworkProvider::screenscraper ? screenscraperImage(job.asset.url, thumbnail) :
                                    esde ? cachedImage(std::filesystem::path(job.asset.url)) :
                                    artwork_sources::own(job.provider) ? artwork_sources::download(job.provider, job.asset, thumbnail) :
                                    downloadGridImage(job.asset, thumbnail);
                    if (job.kind == ArtworkKind::disc && !thumbnail && !result->bytes.empty() && !discImage(result->bytes)) {
                        result->bytes.clear(); result->error = tr(L"Wybrana grafika nie jest skanem płyty.");
                    }
                    if (!result->bytes.empty()) {
                        if (thumbnail) { if (!esde) saveThumbnail(file, result->bytes); } // Never write into ES-DE's folders.
                        else if (!cacheArtworkVariant(job.game, job.kind, job.asset.id, result->bytes, cache)) {
                            result->bytes.clear();
                            result->error = tr(L"Nie można zapisać grafiki w cache.");
                        }
                    }
                }
                if (!thumbnail && result->bytes.empty() && result->error.empty()) result->error = tr(L"Nie można pobrać wybranej grafiki.");
                if (!thumbnail && !result->bytes.empty() && job.kind == ArtworkKind::cover) {
                    auto focus = file;
                    focus.replace_extension(L".focus");
                    result->focus = cachedTextCrop(file, focus, job.game.name);
                }
            }
            if (!alive->load() || queue->generation.load() != job.generation) continue;
            if (PostMessageW(window, artwork_browser_message, 0, reinterpret_cast<LPARAM>(result.get()))) result.release();
        }
    });
}

// Which types of this game have an artwork in use (a filled dot; an empty one when none): the one the player
// picked, or the one found by itself (Steam's, SteamGridDB's by the game's ID, the store's own).
void Launcher::refreshArtworkSet() {
    auto& state = artwork_state_;
    if (state.wallpaper) return;
    const auto cache = coverCacheDirectory();
    for (size_t i = 0; i < artwork_ui::kinds.size(); ++i)
        state.custom[i] = selectedArtworkId(state.game, artwork_ui::kinds[i], cache) != 0 || cachedArtworkAvailable(state.game, artwork_ui::kinds[i], cache, false);
}

void Launcher::openArtworkBrowser(const Game& game) {
    if (!isGame(game) || gameKey(game).empty() || pending_launch_) return;
    desktopLeaveLibrary(); rememberPlace();
    artwork_return_ = screen_;
    artwork_return_selected_ = selected_;
    artwork_return_scroll_ = grid_scroll_;
    // Open on the artwork displayed by the current home layout. The desktop window's grid shows
    // posters (the square cover only stands in for a missing one), whichever page it was opened from.
    const auto opened_from = desktop_mode_ || library_view::posterArt(libraryPresentation()) ? ArtworkKind::poster :
        libraryLandscape() ? ArtworkKind::landscape : flixActive() && selected_ < flix_card_rows_.size() ?
        (flix_card_rows_[selected_] == 0 ? ArtworkKind::landscape : ArtworkKind::poster) :
        metroActive() && selected_ < metro_tiles_.size() ?
        (metro_layout::landscape(metro_tiles_[selected_].role) ? ArtworkKind::landscape : ArtworkKind::poster) :
        shelfWideCard(selected_) ? ArtworkKind::landscape :
        (xmbActive() && settings_.xmb_posters) || shelfPosters() ? ArtworkKind::poster : ArtworkKind::cover;
    artwork_state_ = {};
    artwork_pages_.clear(); artwork_page_key_.clear();
    artwork_state_.game = game;
    artwork_state_.source = artwork_state_.override_source = selectedArtworkSource(game, coverCacheDirectory());
    artwork_state_.query = game.name;
    artwork_state_.category = artwork_ui::category(opened_from);
    artwork_state_.choices = artworkChoices(artwork_state_);
    artwork_state_.provider = artwork_state_.choices.front();
    refreshArtworkSet();
    dismissMenu();
    menu_progress_ = 0;
    closeFriends();
    closeCategoryMenu();
    suspendDetails();
    screen_ = ScreenMode::artwork;
    rebuildCards(false);
    loadArtworkPage();
}

// Interfejs → Tapeta: "Zmień grafikę" with Zdjęcia (Wallhaven, Danbooru) and Animowane (MotionBGs,
// Steam's animated profile backgrounds); the focused animated one plays behind the gallery.
void Launcher::openWallpaperBrowser() {
    if (pending_launch_ || desktop_mode_) return;
    artwork_return_ = screen_;
    artwork_return_selected_ = selected_;
    artwork_return_scroll_ = grid_scroll_;
    artwork_state_ = {};
    artwork_pages_.clear(); artwork_page_key_.clear();
    artwork_state_.wallpaper = true;
    artwork_state_.game.name = tr(L"Tapeta");
    artwork_state_.category = settings_.wallpaper == AppSettings::WallpaperKind::video ? 1 : 0;
    artwork_state_.choices = artworkChoices(artwork_state_);
    artwork_state_.provider = artwork_state_.choices.front();
    wallpaper_preview_id_ = 0;
    dismissMenu();
    screen_ = ScreenMode::artwork;
    loadArtworkPage();
    audio_.context();
}

// Another rail entry: its gallery; in the wallpaper picker also its sources (the typed query kept).
void Launcher::showArtworkCategory(int category) {
    auto& state = artwork_state_;
    state.category = category; state.page = 0; state.source = state.override_source;
    if (state.wallpaper) {
        state.choices = artworkChoices(state);
        state.provider = state.choices.front();
        stopWallpaperPreview();
    }
    loadArtworkPage();
}

// The "Źródło" list: the sources switched on (Media i dane → Źródła grafik) that can have this
// game's art, or in the wallpaper picker the sources of the type in view; never empty.
std::vector<ArtworkProvider> Launcher::artworkChoices(const artwork_ui::State& state) const {
    std::vector<ArtworkProvider> choices;
    const auto on = [&](ArtworkProvider provider) { return !settings_.artwork_sources_off.contains(artwork_sources::slug(provider)); };
    if (state.wallpaper) {
        if (state.videoWallpapers()) choices = {ArtworkProvider::motionbgs, ArtworkProvider::steam_backgrounds, ArtworkProvider::wallpaper_engine};
        else choices = {ArtworkProvider::wallhaven, ArtworkProvider::danbooru};
        return choices;
    }
    // ScreenScraper and ES-DE right after SteamGridDB for emulated games, as before.
    for (const auto provider : {ArtworkProvider::steamgriddb, ArtworkProvider::screenscraper, ArtworkProvider::esde, ArtworkProvider::steam,
                                ArtworkProvider::gog, ArtworkProvider::xbox, ArtworkProvider::nintendo, ArtworkProvider::gametdb,
                                ArtworkProvider::titledb, ArtworkProvider::thegamesdb, ArtworkProvider::wikimedia})
        if (on(provider) && artwork_sources::applicable(provider, state.game)) choices.push_back(provider);
    if (choices.empty()) choices.push_back(ArtworkProvider::steamgriddb);
    if (hasLocalArtwork(state.game, coverCacheDirectory())) choices.push_back(ArtworkProvider::local); // What was added from disk.
    return choices;
}

void Launcher::closeArtworkBrowser() {
    {
        std::lock_guard lock(artwork_jobs_->mutex);
        artwork_jobs_->generation.fetch_add(1);
        artwork_jobs_->jobs.clear();
    }
    const bool wallpaper = artwork_state_.wallpaper;
    artwork_state_.assets.clear();
    artwork_state_.applying = false;
    artwork_state_.wallpaper = false;
    artwork_pages_.clear(); artwork_page_key_.clear(); // The visit's pages and thumbnails go with it.
    renderer_.resetArtworkThumbnails(0);
    screen_ = artwork_return_;
    grid_scroll_ = artwork_return_scroll_;
    if (wallpaper) { // Back to Tło i szkło, the wallpaper chosen (or the one before) behind it again.
        stopWallpaperPreview();
        buildSettingsRows();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    rebuildCards(false);
    if (screen_ == ScreenMode::details) { requestDetails(); return; }
    if (!cards_.empty()) navigate(static_cast<int>(std::min(artwork_return_selected_, cards_.size() - 1)) - static_cast<int>(selected_), false);
}

void Launcher::closeEditorsFor(const std::function<bool(const Game&)>& gone) {
    if (artworkOpen() && !artwork_state_.wallpaper && gone(artwork_state_.game)) closeArtworkBrowser();
    if (cropOpen() && !crop_games_.empty() && gone(crop_games_[0])) closeCropEditor();
}

std::string Launcher::artworkPageKey() const {
    const auto& state = artwork_state_;
    const auto provider = state.activeProvider();
    auto key = std::string(state.wallpaper ? "w|" : "") + std::to_string(state.category) + '|' + std::to_string(state.page) + '|' +
               std::to_string(static_cast<int>(provider)) + '|' + std::to_string(state.source.id) + (state.source.steam ? "s" : "g");
    // Płyta, the sources beside SteamGridDB and the wallpapers follow the field.
    if (state.wallpaper || state.kind() == ArtworkKind::disc || provider != ArtworkProvider::steamgriddb) key += '|' + text_encoding::utf8(state.query);
    return key;
}

void Launcher::loadArtworkPage() {
    auto& state = artwork_state_;
    state.search_focus = state.results_mode = false;
    state.assets.clear();
    state.requested.clear();
    state.selected = 0;
    state.scroll = 0;
    state.loading = true;
    state.more = state.applying = false;
    state.status = tr(L"Wczytywanie…");
    state.provider = state.activeProvider();
    renderer_.resetArtworkThumbnails(0);
    artwork_page_key_ = artworkPageKey();
    if (const auto cached = artwork_pages_.find(artwork_page_key_); cached != artwork_pages_.end()) {
        // Seen while the browser is open: shown at once, its thumbnails from memory.
        { std::lock_guard lock(artwork_jobs_->mutex); artwork_jobs_->generation.fetch_add(1); artwork_jobs_->jobs.clear(); }
        const auto& page = cached->second;
        state.loading = false;
        state.assets = page.assets; state.provider = page.provider; state.more = page.more; state.status = page.status;
        if (page.provider == ArtworkProvider::steamgriddb) state.source = page.source;
        state.applied_id = state.wallpaper ? settings_.wallpaper_id : selectedArtworkId(state.game, state.kind(), coverCacheDirectory()); // A pick made since.
        renderer_.resetArtworkThumbnails(state.assets.size());
        for (size_t i = 0; i < state.assets.size(); ++i) {
            if (const auto bytes = page.thumbnails.find(state.assets[i].id); bytes != page.thumbnails.end()) {
                state.requested.insert(state.assets[i].id);
                renderer_.setArtworkThumbnail(i, bytes->second, state.kind() == ArtworkKind::logo);
            }
            if (const auto moving = page.animations.find(state.assets[i].id); moving != page.animations.end()) {
                state.requested.insert(state.assets[i].id);
                renderer_.setArtworkThumbnailAnimation(i, moving->second);
            }
        }
        requestArtworkThumbnails(); // Those that had not arrived yet.
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    {
        std::lock_guard lock(artwork_jobs_->mutex);
        const auto generation = artwork_jobs_->generation.fetch_add(1) + 1;
        artwork_jobs_->jobs.clear();
        ArtworkBrowserJob job{ArtworkBrowserMessage::Type::list, generation, state.game, state.kind(), state.page, state.source};
        job.provider = state.provider;
        job.wallpaper = state.wallpaper;
        if (state.kind() == ArtworkKind::disc && !state.query.empty() && !state.wallpaper) job.game.name = state.query;
        // The other sources look the field's text up (the game's name until it is edited).
        if (state.wallpaper || artwork_sources::own(job.provider)) job.query = state.query;
        job.steam_app_id = steamArtwork(state.game) ? state.game.app_id : state.source.steam ? state.source.id : 0;
        if (!job.steam_app_id) if (const auto found = rawg_metadata_.find(gameKey(state.game)); found != rawg_metadata_.end()) job.steam_app_id = found->second.steam_app_id;
        if (state.query != state.game.name) job.steam_app_id = 0; // A typed name (or AppID) is looked up instead.
        artwork_jobs_->jobs.push_back(std::move(job));
    }
    artwork_jobs_->wake.notify_one();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::editArtworkQuery() {
    auto& state = artwork_state_;
    if (state.applying) return;
    state.search_focus = true; state.gallery = false; state.loading = false;
    state.results_mode = true; state.games.clear(); state.assets.clear(); state.status.clear();
    { std::lock_guard lock(artwork_jobs_->mutex); ++artwork_jobs_->generation; artwork_jobs_->jobs.clear(); }
    renderer_.resetArtworkThumbnails(0);
    InvalidateRect(hwnd_, nullptr, FALSE);
}
void Launcher::leaveArtworkSearch() {
    auto& state = artwork_state_;
    state.search_focus = state.query_selected = false;
    // An edit that was never searched left an empty results page: show the gallery again.
    if (state.results_mode && state.games.empty() && !state.loading) { state.source = state.override_source; loadArtworkPage(); }
    InvalidateRect(hwnd_, nullptr, FALSE);
}
void Launcher::clearArtworkQuery() {
    if (!artworkOpen() || artwork_state_.applying) return;
    auto& state = artwork_state_;
    state.query.clear(); state.query_selected = false;
    editArtworkQuery();
    audio_.context();
}
void Launcher::searchArtwork() {
    if (artwork_state_.applying) return;
    // Płyta, wallpapers and the sources beside SteamGridDB: the gallery of the typed text at once.
    if (artwork_state_.kind() == ArtworkKind::disc || artwork_state_.wallpaper || artwork_state_.activeProvider() != ArtworkProvider::steamgriddb) {
        artwork_state_.page = 0;
        loadArtworkPage();
        artwork_state_.gallery = false;
        return;
    }
    editArtworkQuery();
    auto& state = artwork_state_; state.loading = true; state.status = tr(L"Szukanie gry…"); state.selected = 0; state.scroll = 0;
    ArtworkBrowserJob job; job.type = ArtworkBrowserMessage::Type::search; job.query = state.query;
    { std::lock_guard lock(artwork_jobs_->mutex); job.generation = artwork_jobs_->generation; artwork_jobs_->jobs.push_back(std::move(job)); }
    artwork_jobs_->wake.notify_one();
}

void Launcher::requestArtworkThumbnails() {
    if (!artworkOpen() || artwork_state_.assets.empty()) return;
    const auto view = renderer_.viewport();
    auto& state = artwork_state_;
    if (desktopSheet()) {
        // The desktop dialog's gallery scrolls on its own: the page's thumbnails, the selected first.
        std::lock_guard lock(artwork_jobs_->mutex);
        for (size_t i = 0; i < state.assets.size(); ++i) {
            const size_t index = i == 0 ? std::min(state.selected, state.assets.size() - 1) : (i <= state.selected ? i - 1 : i);
            const auto& asset = state.assets[index];
            if (!state.requested.insert(asset.id).second) continue;
            ArtworkBrowserJob job{ArtworkBrowserMessage::Type::thumbnail, artwork_jobs_->generation.load(), state.game,
                                  state.kind(), state.page, state.source, asset};
            job.provider = state.provider;
            artwork_jobs_->jobs.push_back(std::move(job));
        }
        artwork_jobs_->wake.notify_all();
        return;
    }
    const artwork_ui::Layout layout{view.width, view.height, state.kind()};
    state.scroll = layout.ensureVisible(state.selected, state.assets.size(), state.scroll);
    std::lock_guard lock(artwork_jobs_->mutex);
    const auto request = [&](size_t index) {
        const auto& asset = state.assets[index];
        if (!state.requested.insert(asset.id).second) return;
        ArtworkBrowserJob job{ArtworkBrowserMessage::Type::thumbnail, artwork_jobs_->generation.load(), state.game,
                              state.kind(), state.page, state.source, asset};
        job.provider = state.provider;
        job.wallpaper = state.wallpaper;
        if (index == state.selected) artwork_jobs_->jobs.push_front(std::move(job));
        else artwork_jobs_->jobs.push_back(std::move(job));
    };
    request(state.selected);
    for (size_t i = 0; i < state.assets.size(); ++i) {
        const auto r = layout.cell(i, state.scroll);
        if (r.bottom >= layout.top && r.top < layout.bottom()) request(i);
    }
    // Animated wallpapers: the focused one's moving preview plays behind the gallery before it is chosen.
    if (state.videoWallpapers() && state.selected < state.assets.size() && !state.assets[state.selected].preview.empty() &&
        wallpaper_preview_id_ != state.assets[state.selected].id) {
        wallpaper_preview_id_ = state.assets[state.selected].id;
        ArtworkBrowserJob job{ArtworkBrowserMessage::Type::preview, artwork_jobs_->generation.load(), state.game,
                              state.kind(), state.page, state.source, state.assets[state.selected]};
        job.provider = state.provider;
        job.wallpaper = true;
        artwork_jobs_->jobs.push_front(std::move(job));
    }
    artwork_jobs_->wake.notify_all();
}

void Launcher::onArtworkBrowser(std::unique_ptr<ArtworkBrowserMessage> message) {
    bool local_added = false;
    if (!artworkOpen() || message->generation != artwork_jobs_->generation.load()) return;
    auto& state = artwork_state_;
    if (message->type == ArtworkBrowserMessage::Type::preview) {
        // Still the focused animated wallpaper: its preview plays behind the gallery.
        if (state.wallpaper && !message->file.empty() && state.selected < state.assets.size() && state.assets[state.selected].id == message->asset_id)
            showWallpaperPreview(message->file);
        return;
    }
    if (message->wallpaper && (message->type == ArtworkBrowserMessage::Type::apply || message->type == ArtworkBrowserMessage::Type::local)) {
        if (!state.applying) return;
        state.applying = false;
        if (message->file.empty()) { state.status = message->error.empty() ? std::wstring(tr(L"Nie można pobrać tapety.")) : message->error; InvalidateRect(hwnd_, nullptr, FALSE); return; }
        settings_.wallpaper = message->video ? AppSettings::WallpaperKind::video : AppSettings::WallpaperKind::picture;
        settings_.wallpaper_file = message->file;
        settings_.wallpaper_id = message->asset_id;
        state.applied_id = message->asset_id;
        state.status = tr(L"Tapeta ustawiona.");
        settingsChanged();
        stopWallpaperPreview();
        applyWallpaper();
        audio_.context();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (message->type == ArtworkBrowserMessage::Type::search) {
        state.loading = false; state.games = std::move(message->search.games); state.status = std::move(message->search.error);
        // Equal titles: the release year known for this game (RAWG, DRM-free folder) comes first.
        int year = 0;
        if (const auto found = rawg_metadata_.find(gameKey(state.game)); found != rawg_metadata_.end() && found->second.details.release_date.size() >= 4)
            year = _wtoi(found->second.details.release_date.substr(0, 4).c_str());
        if (!year && state.game.source == GameSource::drmfree)
            for (const auto& entry : drm_catalog_.games) if (entry.id == state.game.source_id) year = entry.year;
        if (year) {
            const auto title = state.query;
            std::stable_sort(state.games.begin(), state.games.end(), [&](const GridGame& a, const GridGame& b) {
                const int left = gridTitleScore(title, a.name), right = gridTitleScore(title, b.name);
                if (left != right) return left > right;
                const auto distance = [&](const GridGame& game) { return game.year ? std::abs(game.year - year) : 10000; };
                return distance(a) < distance(b);
            });
        }
        state.search_focus = state.games.empty(); state.gallery = !state.games.empty(); state.selected = 0;
        InvalidateRect(hwnd_, nullptr, FALSE); return;
    }
    if (message->type == ArtworkBrowserMessage::Type::list) {
        state.loading = false;
        state.assets = std::move(message->page.assets);
        state.provider = message->page.provider;
        if (message->page.provider == ArtworkProvider::steamgriddb) state.source = message->page.source;
        state.more = message->page.more;
        // A focused page arrow that has no page now: the other one, else "Z dysku…".
        if (state.page_focus) {
            if (state.page_focus > 0 && !state.more) state.page_focus = state.page > 0 ? -1 : 0;
            else if (state.page_focus < 0 && state.page == 0) state.page_focus = state.more ? 1 : 0;
            if (!state.page_focus) state.local_focus = true;
        }
        state.applied_id = state.wallpaper ? settings_.wallpaper_id : message->applied_id;
        state.status = message->page.error;
        if (state.status.empty() && state.assets.empty()) state.status = tr(L"Brak grafik.");
        // Kept for the rest of this visit; a failed answer (no connection, a rate limit) is asked again.
        // Under the key it was asked by and, the answer naming the SteamGridDB game, the key it has now.
        if (message->page.error.empty() && !artwork_page_key_.empty()) {
            ArtworkPageCache page{state.assets, state.provider, state.source, state.more, state.status, {}};
            artwork_pages_[artwork_page_key_] = page;
            artwork_page_key_ = artworkPageKey();
            artwork_pages_[artwork_page_key_] = std::move(page);
        }
        renderer_.resetArtworkThumbnails(state.assets.size());
        requestArtworkThumbnails();
        // SteamGridDB has no game under this exact title: the games its search finds are listed at
        // once (as "Szukaj" would), instead of an empty gallery. Once per visit.
        const bool unmatched = !message->page.source.id && (message->page.error.empty() || message->page.error == tr(L"Nie znaleziono tej gry w SteamGridDB."));
        if (unmatched && state.assets.empty() && !state.auto_searched && !state.wallpaper && state.page == 0 &&
            state.provider == ArtworkProvider::steamgriddb && state.kind() != ArtworkKind::disc && !state.query.empty()) {
            state.auto_searched = true;
            searchArtwork();
        }
    } else if (message->type == ArtworkBrowserMessage::Type::thumbnail) {
        if (const auto page = artwork_pages_.find(artwork_page_key_); page != artwork_pages_.end()) {
            if (!message->bytes.empty()) page->second.thumbnails[message->asset_id] = message->bytes;
            if (message->animation) page->second.animations[message->asset_id] = message->animation;
        }
        for (size_t i = 0; i < state.assets.size(); ++i)
            if (state.assets[i].id == message->asset_id) {
                if (message->animation) renderer_.setArtworkThumbnailAnimation(i, message->animation);
                else renderer_.setArtworkThumbnail(i, std::move(message->bytes), state.kind() == ArtworkKind::logo);
            }
    } else {
        const bool local = message->type == ArtworkBrowserMessage::Type::local;
        if (!state.applying || (!local && (state.assets.empty() || state.assets[state.selected].id != message->asset_id))) return;
        state.applying = false;
        state.status = message->error;
        const auto kind = local ? message->kind : state.kind();
        if (state.status.empty() && (message->bytes.empty() ||
            !selectArtworkVariant(state.game, kind, message->asset_id, coverCacheDirectory())))
            state.status = tr(L"Nie można zastosować grafiki.");
        if (state.status.empty()) {
            state.applied_id = message->asset_id;
            refreshArtworkSet();
            if (local) { state.status = tr(L"Zastosowano grafikę z dysku."); local_added = true; }
            const auto key = gameKey(state.game);
            forgetAnimatedArt(key); // A pick that moves (or no longer does).
            invalidateArtwork(state.game, coverKind(kind));
            auto& cached = art_.at(key);
            switch (kind) {
            case ArtworkKind::cover: cached.cover = shareImage(std::move(message->bytes)); cached.focus = message->focus; break;
            case ArtworkKind::logo: cached.logo = shareImage(std::move(message->bytes)); break;
            case ArtworkKind::poster: cached.poster = shareImage(std::move(message->bytes)); desktop_poster_missing_.erase(key); break;
            case ArtworkKind::landscape: cached.landscape = shareImage(std::move(message->bytes)); break;
            case ArtworkKind::disc: cached.disc = shareImage(std::move(message->bytes)); break;
            default: cached.hero = shareImage(std::move(message->bytes)); cached.hero_tone = HeroTone::full; cached.hero_focus = {}; break; // A new picture: centred.
            }
            art_.resize(key);
            trimArt();
            audio_.context();
        }
    }
    if (local_added) { // The added picture in the "Lokalne" gallery of its type, checked as the one in use.
        const auto done = state.status;
        artwork_pages_.clear();
        state.provider = ArtworkProvider::local;
        loadArtworkPage();
        state.status = done;
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::openLocalArtwork() {
    auto& state = artwork_state_;
    if (!artworkOpen() || state.applying) return;
    if (state.wallpaper) { pickLocalWallpaper(); return; } // One kind of file: the picker at once.
    state.local_open = true; state.local_focus = true; state.gallery = state.search_focus = false;
    state.local_kind = state.local_row = state.category; // The type in view is the likely one.
    audio_.context();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

// "Z dysku…" in the wallpaper picker: a picture or a video file becomes the wallpaper.
void Launcher::pickLocalWallpaper() {
    auto& state = artwork_state_;
    state.local_focus = true; state.gallery = state.search_focus = false;
    const auto apply = [this](const std::filesystem::path& picked) {
        auto& browser = artwork_state_;
        if (!artworkOpen() || !browser.wallpaper || browser.applying || picked.empty()) return;
        browser.applying = true;
        browser.status = tr(L"Wczytywanie tapety…");
        ArtworkBrowserJob job;
        job.type = ArtworkBrowserMessage::Type::local; job.wallpaper = true; job.query = picked.wstring();
        {
            std::lock_guard lock(artwork_jobs_->mutex);
            job.generation = artwork_jobs_->generation.load();
            artwork_jobs_->jobs.push_front(std::move(job));
        }
        artwork_jobs_->wake.notify_one();
        audio_.context();
        InvalidateRect(hwnd_, nullptr, FALSE);
    };
    if (pick_image_) { // Tests: an injected picker answers at once.
        const auto picked = pick_image_(hwnd_);
        mouse_transition_until_ = GetTickCount64() + GetDoubleClickTime();
        apply(picked);
        return;
    }
    file_browser::Request request;
    request.title = tr(L"Wybierz tapetę");
    request.extensions = {L".png", L".jpg", L".jpeg", L".mp4", L".m4v", L".webm", L".mkv", L".mov", L".wmv"};
    request.images = true;
    openFileBrowser(std::move(request), apply);
}

void Launcher::cycleArtworkProvider() {
    auto& state = artwork_state_;
    const auto offered = state.shown();
    if (!artworkOpen() || !state.providerChoice() || state.applying || offered.empty()) return;
    const auto at = std::find(offered.begin(), offered.end(), state.activeProvider()) - offered.begin();
    state.provider = offered[static_cast<size_t>(at + 1) % offered.size()];
    state.page = 0;
    state.source = state.override_source;
    loadArtworkPage();
    audio_.context();
}

void Launcher::chooseArtworkProvider(int index) {
    auto& state = artwork_state_;
    state.provider_open = false;
    const auto offered = state.shown();
    if (!artworkOpen() || !state.providerChoice() || state.applying || index < 0 || index >= static_cast<int>(offered.size())) return;
    const auto chosen = offered[static_cast<size_t>(index)];
    audio_.context();
    if (chosen != state.activeProvider()) {
        state.provider = chosen;
        state.page = 0;
        state.source = state.override_source;
        stopWallpaperPreview();
        loadArtworkPage();
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::openArtworkProviders() {
    auto& state = artwork_state_;
    if (!artworkOpen() || !state.providerChoice() || state.applying) return;
    state.local_focus = state.crop_focus = state.back_focus = state.gallery = state.search_focus = false;
    state.provider_focus = state.provider_open = true;
    const auto offered = state.shown();
    state.provider_row = std::max(0, static_cast<int>(std::find(offered.begin(), offered.end(), state.activeProvider()) - offered.begin()));
    audio_.context();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

// "Kadruj" (covers and backgrounds): the crop editor of the same game; Back there returns to "Zmień grafikę".
void Launcher::openArtworkCrop() {
    auto& state = artwork_state_;
    if (!artworkOpen() || state.applying || !state.croppable()) return; // Covers and backgrounds only.
    state.local_focus = state.provider_focus = state.back_focus = false;
    state.crop_focus = true; state.gallery = state.search_focus = false;
    audio_.context();
    openCropEditor(state.game, state.kind() == ArtworkKind::hero); // On Tło: the part of the background a screen shows.
}

void Launcher::closeLocalArtwork() {
    artwork_state_.local_open = false;
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::pickLocalArtwork() {
    auto& state = artwork_state_;
    if (!artworkOpen() || !state.local_open || state.applying) return;
    if (!pick_image_) {
        file_browser::Request request;
        request.title = tr(L"Wybierz grafikę");
        request.extensions = {L".png", L".jpg", L".jpeg"};
        request.images = true;
        request.native = [this] { return pickArtworkImage(hwnd_); };
        openFileBrowser(std::move(request), [this](const std::filesystem::path& picked) { applyLocalArtwork(picked); });
        return;
    }
    // Tests: an injected picker answers at once.
    const auto picked = pick_image_(hwnd_);
    mouse_transition_until_ = GetTickCount64() + GetDoubleClickTime();
    if (picked.empty()) { InvalidateRect(hwnd_, nullptr, FALSE); return; }
    applyLocalArtwork(picked);
}

void Launcher::applyLocalArtwork(const std::filesystem::path& picked) {
    auto& state = artwork_state_;
    if (!artworkOpen() || !state.local_open || state.applying) return;
    const int kind = std::clamp(state.local_kind, 0, static_cast<int>(artwork_ui::kinds.size()) - 1);
    state.local_open = false;
    // The chosen type's gallery behind it; the file is read and applied on a worker.
    if (state.category != kind || state.results_mode) { state.category = kind; state.page = 0; state.source = state.override_source; loadArtworkPage(); }
    // The file becomes one of the game's "Lokalne" pictures: that source is offered from now on.
    if (!state.hasLocal()) state.choices.push_back(ArtworkProvider::local);
    state.applying = true;
    state.status = tr(L"Wczytywanie grafiki…");
    ArtworkBrowserJob job;
    job.type = ArtworkBrowserMessage::Type::local; job.game = state.game; job.kind = artwork_ui::kinds[kind]; job.query = picked.wstring();
    {
        std::lock_guard lock(artwork_jobs_->mutex);
        job.generation = artwork_jobs_->generation.load();
        artwork_jobs_->jobs.push_front(std::move(job));
    }
    artwork_jobs_->wake.notify_one();
    audio_.context();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::artworkMove(int horizontal, int vertical) {
    if (!artworkOpen() || artwork_state_.applying) return;
    auto& state = artwork_state_;
    const auto view = renderer_.viewport();
    const auto layout = state.layout(view.width, view.height);
    if (state.local_open) {
        // Panel: the board of types (3 x 2), then "Wskaż plik…" under it.
        constexpr int columns = artwork_ui::Layout::board_columns, pick = artwork_ui::State::local_pick_row;
        int next = state.local_row;
        if (horizontal && next != pick) next = std::clamp(next + (horizontal > 0 ? 1 : -1), 0, pick - 1);
        if (vertical) {
            const int rows = (pick + columns - 1) / columns; // The tile rows; the button is one more.
            const int column = next == pick ? std::clamp(state.local_kind, 0, pick - 1) % columns : next % columns;
            const int row = std::clamp((next == pick ? rows : next / columns) + vertical, 0, rows);
            next = row >= rows ? pick : std::min(pick - 1, row * columns + column);
        }
        if (next == state.local_row) { if (horizontal || vertical) navigationBoundary(); }
        else audio_.navigate();
        state.local_row = next;
        InvalidateRect(hwnd_, nullptr, FALSE); return;
    }
    // "Źródło"'s open list: Up/Down between the providers (A picks, Back closes).
    if (state.provider_open) {
        if (vertical) {
            const int next = std::clamp(state.provider_row + vertical, 0, std::max(0, static_cast<int>(state.shown().size()) - 1));
            if (next == state.provider_row) navigationBoundary(); else audio_.navigate();
            state.provider_row = next;
        }
        InvalidateRect(hwnd_, nullptr, FALSE); return;
    }
    // "Kadruj" (right of "Szukaj"): Left back to the field, Right to "Źródło", Down into the gallery.
    if (state.crop_focus) {
        if (!state.croppable()) state.crop_focus = false;
        else if (horizontal < 0) { state.crop_focus = false; state.search_focus = state.query_selected = true; audio_.navigate(); }
        else if (horizontal > 0 && state.providerChoice()) { state.crop_focus = false; state.provider_focus = true; audio_.navigate(); }
        else if (vertical > 0) {
            state.crop_focus = false;
            if ((state.results_mode && !state.games.empty()) || (!state.results_mode && !state.assets.empty())) state.gallery = true;
            audio_.navigate();
        } else navigationBoundary();
        InvalidateRect(hwnd_, nullptr, FALSE); return;
    }
    // "Źródło" (right end of the search row): Left back to "Kadruj" or the field, Down into the gallery.
    if (state.provider_focus) {
        if (!state.providerChoice()) state.provider_focus = false;
        else if (horizontal < 0) {
            state.provider_focus = false;
            if (state.croppable()) state.crop_focus = true; else state.search_focus = state.query_selected = true;
            audio_.navigate();
        }
        else if (vertical > 0) {
            state.provider_focus = false;
            if ((state.results_mode && !state.games.empty()) || (!state.results_mode && !state.assets.empty())) state.gallery = true;
            audio_.navigate();
        } else navigationBoundary();
        InvalidateRect(hwnd_, nullptr, FALSE); return;
    }
    // The page arrows under "Z dysku…": Left/Right between them (only an enabled one), Up back to "Z dysku…".
    if (state.page_focus && !state.gallery && !state.search_focus) {
        const bool previous = state.page > 0, next = state.more;
        if (vertical < 0) { state.page_focus = 0; state.local_focus = true; audio_.navigate(); }
        else if (horizontal < 0 && state.page_focus > 0 && previous) { state.page_focus = -1; audio_.navigate(); }
        else if (horizontal > 0 && state.page_focus < 0 && next) { state.page_focus = 1; audio_.navigate(); }
        else if (horizontal > 0) { state.page_focus = 0; state.search_focus = state.query_selected = true; audio_.navigate(); }
        else navigationBoundary();
        InvalidateRect(hwnd_, nullptr, FALSE); return;
    }
    // Right from any rail entry reaches the search field even while the gallery is loading/empty.
    // Keep the selected rail entry so Left returns there; Down enters the results or gallery.
    if (horizontal > 0 && !state.gallery && !state.search_focus) {
        state.search_focus = state.query_selected = true;
        audio_.navigate(); InvalidateRect(hwnd_, nullptr, FALSE); return;
    }
    // Back sits at the top left, above the types; "Z dysku…" under the last type.
    if (state.back_focus && !state.gallery && !state.search_focus) {
        if (vertical > 0) { state.back_focus = false; audio_.navigate(); } // The selected type.
        else { navigationBoundary(); return; }
        InvalidateRect(hwnd_, nullptr, FALSE); return;
    }
    if (state.local_focus && !state.gallery && !state.search_focus) {
        if (vertical < 0) { state.local_focus = false; audio_.navigate(); } // Back to the last type.
        else if (vertical > 0 && !state.results_mode && (state.more || state.page > 0)) { // Down: the page arrows, the next one first.
            state.local_focus = false; state.page_focus = state.more ? 1 : -1; audio_.navigate();
        }
        else { navigationBoundary(); return; }
        InvalidateRect(hwnd_, nullptr, FALSE); return;
    }
    if (state.search_focus) {
        if (horizontal < 0) { leaveArtworkSearch(); audio_.navigate(); }
        else if (horizontal > 0 && state.croppable()) { state.search_focus = state.query_selected = false; state.crop_focus = true; audio_.navigate(); }
        else if (horizontal > 0 && state.providerChoice()) { state.search_focus = state.query_selected = false; state.provider_focus = true; audio_.navigate(); }
        else if (horizontal > 0) navigationBoundary();
        else if (vertical > 0) {
            if ((state.results_mode && !state.games.empty()) || (!state.results_mode && !state.assets.empty())) {
                state.search_focus = state.query_selected = false; state.gallery = true;
            }
            else leaveArtworkSearch();
            audio_.navigate();
        } else if (vertical < 0) navigationBoundary();
        InvalidateRect(hwnd_, nullptr, FALSE); return;
    }
    if (state.results_mode && state.gallery) {
        if (horizontal < 0) state.gallery = false;
        else if (vertical < 0 && state.selected == 0) { state.search_focus = true; state.gallery = false; }
        else if (!state.games.empty()) {
            const auto before = state.selected;
            state.selected = static_cast<size_t>(std::clamp(static_cast<int>(state.selected) + vertical, 0, static_cast<int>(state.games.size()) - 1));
            if (vertical && before == state.selected) navigationBoundary();
            const auto row = layout.gameRow(state.selected, state.scroll);
            if (row.bottom > layout.bottom()) state.scroll += row.bottom - layout.bottom();
            if (row.top < layout.top) state.scroll = std::max(0.0f, state.scroll - layout.top + row.top);
        }
        InvalidateRect(hwnd_, nullptr, FALSE); return;
    }
    if (!state.gallery) {
        if (vertical) {
            const int next = std::clamp(state.category + vertical, 0, state.railCount() - 1);
            if (vertical < 0 && state.category == 0) state.back_focus = true; // Up from the first type: Back, above it.
            else if (next != state.category) showArtworkCategory(next);
            else if (vertical > 0) state.local_focus = true; // Below the last type: "Z dysku…".
            else navigationBoundary();
        }
    } else if (horizontal < 0 && state.selected % layout.columns() == 0) state.gallery = false;
    else if (vertical < 0 && static_cast<int>(state.selected) < layout.columns()) {
        state.gallery = false; state.search_focus = true; state.query_selected = true; // Top row → the search field.
    } else if (!state.assets.empty()) {
        const int next = std::clamp(static_cast<int>(state.selected) + horizontal + vertical * layout.columns(),
                                    0, static_cast<int>(state.assets.size()) - 1);
        if (next == static_cast<int>(state.selected)) navigationBoundary();
        state.selected = static_cast<size_t>(next);
        requestArtworkThumbnails();
    }
    audio_.navigate();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::artworkAccept() {
    if (editingArtworkSearch()) { if (!artwork_state_.loading) searchArtwork(); return; }
    if (!artworkOpen() || artwork_state_.applying) return;
    auto& state = artwork_state_;
    if (state.local_open) {
        // A type is a choice, like a radio button; then focus moves to "Wskaż plik…".
        if (state.local_row == artwork_ui::State::local_pick_row) pickLocalArtwork();
        else { state.local_kind = state.local_row; state.local_row = artwork_ui::State::local_pick_row; audio_.context(); InvalidateRect(hwnd_, nullptr, FALSE); }
        return;
    }
    if (state.page_focus && !state.gallery && !state.search_focus) { artworkPage(state.page_focus); return; } // The focused page arrow.
    if (state.local_focus && !state.gallery && !state.search_focus) { openLocalArtwork(); return; }
    if (state.provider_open) { chooseArtworkProvider(state.provider_row); return; }
    if (state.provider_focus && !state.gallery && !state.search_focus) { openArtworkProviders(); return; }
    if (state.crop_focus && state.croppable() && !state.gallery && !state.search_focus) { openArtworkCrop(); return; }
    if (state.back_focus && !state.gallery && !state.search_focus) { closeArtworkBrowser(); return; }
    if (artwork_state_.loading) return;
    if (state.results_mode) {
        if (!state.games.empty()) {
            if (!state.gallery) { state.gallery = true; return; }
            state.source = state.override_source = state.games[state.selected].source; state.page = 0;
            state.provider = ArtworkProvider::steamgriddb; // A SteamGridDB search result shows its gallery.
            loadArtworkPage(); state.gallery = true;
        }
        return;
    }
    if (!state.gallery) {
        state.gallery = !state.assets.empty();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (state.assets.empty() || state.assets[state.selected].id == state.applied_id) return;
    state.applying = true;
    state.status = state.wallpaper ? tr(L"Pobieranie tapety…") : tr(L"Pobieranie grafiki…");
    {
        std::lock_guard lock(artwork_jobs_->mutex);
        ArtworkBrowserJob job{ArtworkBrowserMessage::Type::apply, artwork_jobs_->generation.load(),
            state.game, state.kind(), state.page, state.source, state.assets[state.selected]};
        job.provider = state.provider;
        job.wallpaper = state.wallpaper;
        artwork_jobs_->jobs.push_front(std::move(job));
    }
    artwork_jobs_->wake.notify_one();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::artworkPage(int amount) {
    if (artwork_state_.results_mode || artwork_state_.search_focus) return;
    if (!artworkOpen() || artwork_state_.loading || artwork_state_.applying || !amount) return;
    if ((amount < 0 && artwork_state_.page == 0) || (amount > 0 && !artwork_state_.more)) return;
    artwork_state_.page += amount > 0 ? 1 : -1;
    loadArtworkPage();
}

void Launcher::artworkWheel(float mouse_x, int amount) {
    const auto view = renderer_.viewport();
    if (!artworkOpen() || artwork_state_.applying) return;
    artwork_state_.gallery = mouse_x / view.scale > settings_ui::Layout{view.width, view.height}.railRight() &&
        (artwork_state_.results_mode ? !artwork_state_.games.empty() : !artwork_state_.assets.empty());
    if (artwork_state_.gallery) artwork_state_.search_focus = false;
    artworkMove(0, amount);
}

void Launcher::artworkClick(float x, float y, bool double_click) {
    const auto view = renderer_.viewport();
    auto& state = artwork_state_;
    const auto layout = state.layout(view.width, view.height);
    if (state.local_open) {
        const int row = layout.hitLocal(x, y);
        if (row == artwork_ui::State::local_pick_row) { state.local_row = row; pickLocalArtwork(); return; }
        if (row >= 0) { state.local_kind = state.local_row = row; audio_.context(); }
        else if (!layout.localPanel().contains(x, y)) closeLocalArtwork();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (state.provider_open) { // The open list takes the click; outside it closes.
        const int row = layout.hitProvider(x, y);
        if (row >= 0) chooseArtworkProvider(row);
        else { state.provider_open = false; InvalidateRect(hwnd_, nullptr, FALSE); }
        return;
    }
    if (layout.backButton().contains(x, y)) { closeArtworkBrowser(); return; }
    if (state.applying) return;
    if (layout.localButton().contains(x, y)) { openLocalArtwork(); return; }
    if (state.providerChoice() && layout.providerButton().contains(x, y)) { openArtworkProviders(); return; }
    if (state.croppable() && layout.cropButton().contains(x, y)) { openArtworkCrop(); return; }
    state.local_focus = state.provider_focus = state.crop_focus = state.back_focus = false;
    state.page_focus = 0;
    if (!state.query.empty() && layout.clearButton().contains(x, y)) { clearArtworkQuery(); return; }
    if (layout.searchBox().contains(x, y)) { state.search_focus = true; state.gallery = false; state.query_selected = true; InvalidateRect(hwnd_, nullptr, FALSE); return; }
    if (layout.searchButton().contains(x, y)) { searchArtwork(); return; }
    for (int i = 0; i < state.railCount(); ++i) if (layout.base().rail(i).contains(x, y)) {
        state.gallery = false;
        if (i != state.category || state.results_mode) showArtworkCategory(i);
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (layout.pageButton(false).contains(x, y)) { artworkPage(-1); return; }
    if (layout.pageButton(true).contains(x, y)) { artworkPage(1); return; }
    if (state.results_mode) {
        for (size_t i = 0; i < state.games.size(); ++i) if (y >= layout.top && y < layout.bottom() && layout.gameRow(i, state.scroll).contains(x, y)) {
            state.selected = i; state.search_focus = false; state.gallery = true; artworkAccept(); return;
        }
        return;
    }
    const int index = layout.hit(x, y, state.assets.size(), state.scroll);
    if (index >= 0) {
        state.selected = static_cast<size_t>(index);
        state.gallery = true;
        if (double_click) artworkAccept();
        requestArtworkThumbnails();
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}
