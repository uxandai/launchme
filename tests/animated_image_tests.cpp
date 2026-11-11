// Animated SteamGridDB artwork: the WebP container (animation flag, durations, whole frames of a
// cut file), WIC's composed frames, the short gallery loop, the looping player, the still kept for
// the views, and the gallery's order (animated first, the static ones after them across pages).
#include "../src/artwork/animated_image.hpp"
#include "../src/artwork/steamgriddb.hpp"
#include "animated_webp_fixture.hpp"
#include "image_fixture.hpp"

#include <chrono>
#include <fstream>
#include <iostream>
#include <set>
#include <thread>

namespace {
HttpResponse jsonReply(uint32_t status, const std::string& text) { return {status, {text.begin(), text.end()}}; }
bool check(bool condition, const char* what) {
    if (!condition) std::cerr << "FAILED: " << what << '\n';
    return condition;
}
// A SteamGridDB page of `count` assets from `first` on, out of `total`.
std::string assets(uint32_t first, int count, int total, bool animated) {
    std::string items;
    for (int i = 0; i < count; ++i) {
        const auto id = std::to_string(first + i);
        if (!items.empty()) items += ',';
        items += R"({"id":)" + id + R"(,"width":600,"height":900,"url":"https://cdn.example/)" + id + (animated ? ".webp" : ".png") +
                 R"(","thumb":"https://cdn.example/thumb/)" + id + (animated ? ".webm" : ".png") + R"("})";
    }
    return R"({"success":true,"total":)" + std::to_string(total) + R"(,"data":[)" + items + "]}";
}
uint32_t pixel(const std::vector<uint8_t>& frame, uint32_t width, uint32_t x, uint32_t y) {
    const auto* p = frame.data() + (size_t(y) * width + x) * 4;
    return p[0] | (p[1] << 8) | (p[2] << 16) | (uint32_t{p[3]} << 24);
}
} // namespace

int main() {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const auto root = std::filesystem::temp_directory_path() / (L"launchme-animated-tests-" + std::to_wstring(GetCurrentProcessId()));
    struct Cleanup { std::filesystem::path root; ~Cleanup() { std::error_code ec; std::filesystem::remove_all(root, ec); CoUninitialize(); } } cleanup{root};
    std::filesystem::create_directories(root);
    const auto webp = animatedWebpFixture();

    // The container.
    if (!check(animated_image::animatedWebp(webp) && !animated_image::animatedWebp(pngFixture()), "animation flag")) return 1;
    if (!check(animated_image::frameDurations(webp) == std::vector<uint32_t>{100, 100, 100}, "frame durations")) return 2;
    if (!check(animated_image::wholeFrames(webp) == webp, "a whole file stays as it is")) return 3;
    // Cut inside the third frame: two whole frames, a valid file of its own.
    const std::vector<uint8_t> cut(webp.begin(), webp.begin() + 300);
    const auto two = animated_image::wholeFrames(cut);
    if (!check(two.size() < 300 && animated_image::frameDurations(two).size() == 2, "a cut file keeps its whole frames")) return 4;
    const uint32_t riff = two[4] | (two[5] << 8) | (two[6] << 16) | (uint32_t{two[7]} << 24);
    if (!check(riff + 8 == two.size(), "the RIFF size written anew")) return 5;
    if (!check(animated_image::wholeFrames(std::vector<uint8_t>(webp.begin(), webp.begin() + 80)).empty(), "no whole first frame")) return 6;

    // Frames, composed on the canvas, as a short loop.
    if (!animated_image::supported()) { std::cout << "SKIP: animated frames are not decoded on this platform\n"; return 0; }
    const auto loop = animated_image::decodeLoop(webp, 64, 64, 1 << 20, 30, 0);
    if (!check(loop && loop->pixels.size() == 3 && loop->width == 8 && loop->height == 12 && loop->total == 300, "every frame, never enlarged")) return 7;
    for (const auto& frame : loop->pixels)
        for (size_t i = 3; i < frame.size(); i += 4) if (!check(frame[i] == 255, "frames composed on the opaque canvas")) return 8;
    if (!check(loop->pixels[0] != loop->pixels[1] && loop->pixels[1] != loop->pixels[2], "frames differ")) return 9;
    // Frame 2 redraws only rows 4..6 of frame 1: the rest is frame 1's.
    if (!check(pixel(loop->pixels[1], 8, 1, 0) == pixel(loop->pixels[0], 8, 1, 0), "unchanged rows kept")) return 10;
    if (!check(loop->at(0) == 0 && loop->at(150) == 1 && loop->at(299) == 2 && loop->at(300) == 0, "the frame of a moment")) return 11;
    const auto sparse = animated_image::decodeLoop(webp, 4, 4, 1 << 20, 30, 150);
    if (!check(sparse && sparse->pixels.size() == 2 && sparse->durations == std::vector<uint32_t>{200, 100} &&
               sparse->width == 3 && sparse->height == 4, "frames merged to the shortest time, scaled down")) return 12;
    const auto capped = animated_image::decodeLoop(webp, 64, 64, 8 * 12 * 4 * 2, 30, 0);
    if (!check(capped && capped->pixels.size() == 2 && capped->total == 200, "the byte budget ends the loop")) return 13;
    if (!check(!animated_image::decodeLoop(pngFixture(), 64, 64, 1 << 20, 30, 0), "a still is no loop")) return 14;
    const auto partial = animated_image::decodeLoop(two, 64, 64, 1 << 20, 30, 0);
    if (!check(partial && partial->pixels.size() == 2, "a cut download plays its whole frames")) return 15;

    // The player loops a file on its own thread.
    const auto file = root / L"anim.webp";
    { std::ofstream output(file, std::ios::binary); output.write(reinterpret_cast<const char*>(webp.data()), static_cast<std::streamsize>(webp.size())); }
    if (!check(animated_image::frameDurations(file) == std::vector<uint32_t>{100, 100, 100}, "durations read from the file's headers")) return 16;
    animated_image::Player player;
    if (!check(player.open(file, 4, 100), "player opens")) return 17;
    std::set<std::vector<uint8_t>> seen;
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    std::vector<uint8_t> frame;
    uint32_t width = 0, height = 0;
    while (seen.size() < 3 && std::chrono::steady_clock::now() < until && !player.failed()) {
        if (player.frame(frame, width, height)) {
            if (!check(width == 4 && height == 6 && frame.size() == 4 * 6 * 4, "frames at the asked size, aspect kept")) return 18;
            seen.insert(frame);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (!check(seen.size() == 3, "the player shows every frame in turn")) return 19;
    player.stop();
    if (!check(!player.active() && !player.frame(frame, width, height), "stopped")) return 20;
    if (!check(!player.open(root / L"missing.webp", 4, 4), "a missing file does not open")) return 21;

    // Downloads: a ranged start cut to whole frames; the whole file kept with its still.
    GridAsset asset;
    asset.id = 5; asset.animated = true; asset.url = L"https://cdn.example/5.webp";
    std::wstring asked;
    const auto preview = downloadAnimatedPreview(asset, [&](const std::wstring& url, const std::wstring&, size_t bytes) {
        asked = url;
        return bytes == animated_preview_bytes ? HttpResponse{200, cut} : HttpResponse{};
    });
    if (!check(asked == asset.url && preview == two, "preview: the start of the animation, whole frames")) return 22;
    GridAsset still_asset = asset; still_asset.animated = false;
    if (!check(downloadAnimatedPreview(still_asset, [&](const auto&, const auto&, size_t) { return HttpResponse{200, cut}; }).empty(), "a still asset has no preview")) return 23;
    const auto saved = root / L"picks" / L"5.webp";
    const auto writer = [&](const std::vector<uint8_t>& bytes) {
        return [&, bytes](const std::wstring&, const std::wstring&, const std::filesystem::path& target, size_t limit) {
            if (limit != animated_artwork_limit) return HttpResponse{};
            std::filesystem::create_directories(target.parent_path());
            std::ofstream output(target, std::ios::binary);
            output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            return HttpResponse{200};
        };
    };
    if (!check(downloadAnimatedGrid(asset, saved, writer(webp)) && std::filesystem::file_size(saved) == webp.size(), "the whole animation kept")) return 24;
    const auto still = animatedStill(saved);
    if (!check(decodableArtwork(still) && still[0] == 0xff, "its first frame as an opaque JPEG still")) return 25;
    if (!check(!downloadAnimatedGrid(asset, saved, writer(pngFixture())) && !std::filesystem::exists(saved), "a still in its place is not kept")) return 26;

    // The gallery: animated first, then the static ones after the animated share of earlier pages.
    Game game{620, L"Portal 2"};
    std::vector<std::wstring> urls;
    const ArtworkHttp gallery = [&](const std::wstring& url, const std::wstring&, size_t) {
        urls.push_back(url);
        const bool moving = url.find(L"types=animated") != std::wstring::npos;
        if (url.find(L"/steam/620") == std::wstring::npos) return jsonReply(404, "{}");
        if (moving) return url.find(L"page=0") != std::wstring::npos ? jsonReply(200, assets(1000, 2, 2, true)) : jsonReply(200, assets(0, 0, 2, true));
        if (url.find(L"page=0") != std::wstring::npos) return jsonReply(200, assets(1, 24, 30, false));
        if (url.find(L"page=1") != std::wstring::npos) return jsonReply(200, assets(25, 6, 30, false));
        return jsonReply(200, assets(0, 0, 30, false));
    };
    const auto first = browseSteamGridDBGallery(game, ArtworkKind::cover, 0, {}, L"synthetic", gallery);
    if (!check(first.error.empty() && first.assets.size() == grid_page_size && first.more && urls.size() == 2, "first page: two queries")) return 27;
    if (!check(first.assets[0].animated && first.assets[1].animated && first.assets[0].id == 1000 && !first.assets[2].animated &&
               first.assets[2].id == 1 && first.assets.back().id == 22, "animated first, then static")) return 28;
    urls.clear();
    const auto second = browseSteamGridDBGallery(game, ArtworkKind::cover, 1, first.source, L"synthetic", gallery);
    if (!check(second.error.empty() && second.assets.size() == 8 && !second.more && second.assets.front().id == 23 &&
               second.assets.back().id == 30, "second page: the static ones from where the first ended")) return 29;
    for (const auto& url : urls) if (!check(url.find(L"types=animated") == std::wstring::npos, "the animated count remembered")) return 30;
    // Automatic artwork and logos stay static.
    urls.clear();
    browseSteamGridDB(game, ArtworkKind::cover, 0, {}, L"synthetic", gallery);
    browseSteamGridDBGallery(game, ArtworkKind::logo, 0, {}, L"synthetic", gallery);
    for (const auto& url : urls) if (!check(url.find(L"types=animated") == std::wstring::npos, "static only")) return 31;
    // A refused animated query leaves the static gallery.
    Game other{730, L"Counter-Strike 2"};
    const auto refused = browseSteamGridDBGallery(other, ArtworkKind::hero, 0, {}, L"synthetic", [&](const std::wstring& url, const std::wstring&, size_t) {
        if (url.find(L"/steam/730") == std::wstring::npos) return jsonReply(404, "{}");
        return url.find(L"types=animated") != std::wstring::npos ? jsonReply(400, "{}") : jsonReply(200, assets(1, 3, 3, false));
    });
    if (!check(refused.error.empty() && refused.assets.size() == 3 && !refused.assets[0].animated && !refused.more, "static after a refusal")) return 32;
    std::cout << "Animated WebP container, frames, loop, player, downloads and gallery order passed\n";
}
