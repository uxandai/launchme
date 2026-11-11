#include "../src/artwork/animated_image.hpp"
#include "../src/artwork/steamgriddb.hpp"
#include "../src/platform/user_config.hpp"
#include <Windows.h>
#include <chrono>
#include <iostream>
#include <thread>

int main(int argc, char**) {
    const auto key = userSecret(L"STEAMGRIDDB_API_KEY", L"steamgriddb_api_key.txt");
    if (key.empty()) { std::cout << "SKIP: no SteamGridDB key configured\n"; return 0; }
    const auto root = std::filesystem::temp_directory_path() / (L"launchme-grid-live-" + std::to_wstring(GetCurrentProcessId()));
    struct Cleanup { std::filesystem::path root; ~Cleanup() { std::error_code ec; std::filesystem::remove_all(root, ec); } } cleanup{root};
    const auto cache = root / "covers";
    if (argc > 1 && std::string_view(__argv[1]) == "--animated") {
        // Hollow Knight: Silksong has animated grids and a hero: the gallery lists them first, a tile
        // plays the ranged start, and a whole hero plays at its size.
        using clock = std::chrono::steady_clock;
        const auto ms = [](clock::time_point since) { return std::chrono::duration_cast<std::chrono::milliseconds>(clock::now() - since).count(); };
        Game silksong{1030300, L"Hollow Knight: Silksong"};
        for (const auto kind : {ArtworkKind::cover, ArtworkKind::hero}) {
            const auto page = browseSteamGridDBGallery(silksong, kind, 0, {}, key);
            if (!page.error.empty() || page.assets.empty() || !page.assets.front().animated) { std::cerr << "no animated asset first\n"; return 10; }
            size_t animated = 0;
            for (const auto& asset : page.assets) animated += asset.animated;
            std::cout << "kind=" << static_cast<int>(kind) << " assets=" << page.assets.size() << " animated=" << animated << " more=" << page.more << '\n';
            auto started = clock::now();
            const auto preview = downloadAnimatedPreview(page.assets.front());
            const auto fetched = ms(started);
            started = clock::now();
            const auto loop = animated_image::decodeLoop(preview, 360, 360, 8u << 20, 30, 100);
            if (!loop || loop->pixels.size() < 2) { std::cerr << "preview does not play\n"; return 11; }
            std::cout << "  preview " << preview.size() << " B in " << fetched << " ms; loop " << loop->pixels.size() << " frames "
                      << loop->width << 'x' << loop->height << ' ' << loop->total << " ms, decoded in " << ms(started) << " ms\n";
            if (kind != ArtworkKind::hero) continue;
            const auto file = root / L"hero.webp";
            started = clock::now();
            if (!downloadAnimatedGrid(page.assets.front(), file)) { std::cerr << "whole animation not kept\n"; return 12; }
            std::cout << "  whole " << std::filesystem::file_size(file) << " B in " << ms(started) << " ms, still " << animatedStill(file).size() << " B\n";
            animated_image::Player player;
            if (!player.open(file, 1920, 1080)) return 13;
            std::vector<uint8_t> frame; uint32_t width = 0, height = 0; int frames = 0;
            started = clock::now();
            while (ms(started) < 3000 && !player.failed()) {
                if (player.frame(frame, width, height)) ++frames;
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
            std::cout << "  player " << width << 'x' << height << ": " << frames << " frames in 3 s\n";
            if (frames < 30) return 14;
        }
        std::cout << "Live animated gallery, preview and player passed (temporary cache only)\n";
        return 0;
    }
    Game game{620, L"Portal 2"};
    const auto names = searchGridGames(L"Alan Wake 2", key);
    const auto steam_id = searchGridGames(L"620", key);
    if (!names.error.empty() || names.games.empty() || !steam_id.error.empty() || steam_id.games.empty()) return 4;
    auto alan = std::find_if(names.games.begin(), names.games.end(), [](const GridGame& game) { return game.name == L"Alan Wake 2" || game.name == L"Alan Wake II"; });
    if (alan == names.games.end()) return 5;
    Game external; external.source = GameSource::epic; external.kind = GameKind::store; external.source_id = "fixture-alan"; external.name = L"Alan Wake II";
    external.name = L"Alan Wake II Deluxe Edition";
    const auto external_page = browseSteamGridDB(external, ArtworkKind::cover, 0, {}, key);
    if (!external_page.error.empty() || external_page.assets.empty() || external_page.source.id != alan->source.id) return 6;
    const auto auto_image = artworkForGame(external, ArtworkKind::cover, cache, {});
    if (auto_image.empty()) return 8;
    const auto external_image = downloadGridImage(external_page.assets.front(), false);
    if (external_image.empty() || !cacheArtworkVariant(external, ArtworkKind::cover, external_page.assets.front().id, external_image, cache) ||
        !selectArtworkVariant(external, ArtworkKind::cover, external_page.assets.front().id, cache) || selectedArtwork(external, ArtworkKind::cover, cache) != external_image) return 7;
    std::cout << "Non-Steam fuzzy title matching, automatic cover, browser and persistent selection passed; Steam AppID lookup passed\n";
    for (const auto kind : {ArtworkKind::cover, ArtworkKind::hero, ArtworkKind::logo}) {
        if (argc > 1 && kind != ArtworkKind::logo) continue;
        const auto page = browseSteamGridDB(game, kind, 0, {}, key);
        if (!page.error.empty() || page.assets.empty()) {
            std::cerr << "Live SGDB page failed, kind=" << static_cast<int>(kind) << '\n';
            std::wcerr << page.error << L'\n';
            return 1;
        }
        std::cout << "kind=" << static_cast<int>(kind) << " assets=" << page.assets.size() << " more=" << page.more << '\n';
        const auto preview = downloadGridImage(page.assets.front(), true);
        const auto image = downloadGridImage(page.assets.front(), false);
        if (preview.empty() || image.empty() || !cacheArtworkVariant(game, kind, page.assets.front().id, image, cache) ||
            !selectArtworkVariant(game, kind, page.assets.front().id, cache) || artworkForGame(game, kind, cache, {}) != image) return 2;
        if (kind == ArtworkKind::cover && page.more) {
            const auto next = browseSteamGridDB(game, kind, 1, page.source, key);
            if (!next.error.empty() || next.assets.empty() || next.assets.front().id == page.assets.front().id) return 3;
        }
    }
    std::cout << "Live previews, full images, paging and persistent selections passed (temporary cache only)\n";
}
