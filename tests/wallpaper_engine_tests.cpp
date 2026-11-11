// Wallpaper Engine (Video): the scan of Steam libraries' Workshop subscriptions (app 431960) and
// Wallpaper Engine's own projects keeps video items only (no scenes, web pages, Mature ratings,
// missing files or paths leaving the item's folder), sorted by title and paged; their tiles,
// previews and the chosen wallpaper come from disk, played where they are (never copied).
#include "../src/artwork/wallpaper_sources.hpp"
#include "image_fixture.hpp"

#include <fstream>
#include <iostream>
#include <set>

namespace {
bool check(bool condition, const char* what) {
    if (!condition) std::cerr << "FAILED: " << what << '\n';
    return condition;
}
void write(const std::filesystem::path& file, std::string_view text) {
    std::filesystem::create_directories(file.parent_path());
    std::ofstream output(file, std::ios::binary);
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
}
void write(const std::filesystem::path& file, const std::vector<uint8_t>& bytes) {
    write(file, std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
}
// One Workshop item: its project.json and, when `video` is set, that file.
void item(const std::filesystem::path& folder, std::string_view project, const std::wstring& video = {}) {
    write(folder / L"project.json", project);
    if (!video.empty()) write(folder / video, "not really a video");
}
std::vector<std::wstring> titles(const GridPage& page) {
    std::vector<std::wstring> out;
    for (const auto& asset : page.assets) out.push_back(asset.author);
    return out;
}
} // namespace

int main(int argc, char** argv) {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (argc == 2 && std::string_view(argv[1]) == "--live") {
        // This PC's Steam libraries (not run by ctest): what the picker lists, and whether each tile decodes.
        const auto page = wallpaper_sources::browse(ArtworkProvider::wallpaper_engine, L"", 0);
        std::wcout << L"assets: " << page.assets.size() << L" error: " << page.error << L'\n';
        for (const auto& asset : page.assets)
            std::wcout << asset.author << L" | " << asset.url << L" | tile bytes " << wallpaper_sources::thumbnail(asset).size() << L'\n';
        return page.assets.empty() ? 1 : 0;
    }
    const auto root = std::filesystem::temp_directory_path() / (L"launchme-wallpaper-engine-tests-" + std::to_wstring(GetCurrentProcessId()));
    struct Cleanup { std::filesystem::path root; ~Cleanup() { std::error_code ec; std::filesystem::remove_all(root, ec); CoUninitialize(); } } cleanup{root};
    const auto library = root / L"SteamLibrary", other = root / L"Other", empty = root / L"Empty";
    const auto workshop = library / L"steamapps" / L"workshop" / L"content" / L"431960";
    std::filesystem::create_directories(empty / L"steamapps");

    item(workshop / L"100", R"({"type":"video","file":"ocean.mp4","title":"Ocean Waves","preview":"preview.png","contentrating":"Everyone"})", L"ocean.mp4");
    write(workshop / L"100" / L"preview.png", pngFixture());
    item(workshop / L"101", R"({"type":"scene","file":"scene.json","title":"A Scene"})");
    item(workshop / L"102", R"({"type":"web","file":"index.html","title":"A Page"})");
    write(workshop / L"escape.mp4", "outside");
    item(workshop / L"103", R"({"type":"video","file":"../escape.mp4","title":"Escape"})");
    item(workshop / L"104", R"({"type":"video","file":"missing.mp4","title":"Missing"})");
    item(workshop / L"105", R"({"type":"video","file":"adult.mp4","title":"Adult","contentrating":"Mature"})", L"adult.mp4");
    item(workshop / L"106", R"({"type":"Video","file":"clips/ten.webm","title":"Clip 10"})", L"clips/ten.webm");
    item(workshop / L"107", "\xef\xbb\xbf{\"type\":\"video\",\"file\":\"two.mp4\",\"title\":\"Clip 2\"}", L"two.mp4"); // Saved with a BOM.
    item(workshop / L"108", R"({"type":"video","file":"C:\\Windows\\win.mp4","title":"Absolute"})");
    item(workshop / L"109", R"({"type":"video","file":"notes.txt","title":"Not a video"})", L"notes.txt");
    item(workshop / L"110", "{ not json");
    item(other / L"steamapps" / L"common" / L"wallpaper_engine" / L"projects" / L"myprojects" / L"mine",
         R"({"type":"video","file":"mine.mp4","title":"My own"})", L"mine.mp4");

    // Videos only, by title (numbers by value), each once though a library is listed twice.
    const auto all = wallpaper_sources::wallpaperEngine(L"", 0, {library, other, library});
    const std::vector<std::wstring> expected{L"Wallpaper Engine · Clip 2", L"Wallpaper Engine · Clip 10", L"Wallpaper Engine · My own", L"Wallpaper Engine · Ocean Waves"};
    if (!check(all.error.empty() && titles(all) == expected && !all.more && all.provider == ArtworkProvider::wallpaper_engine, "video items only, sorted")) return 1;
    std::set<uint32_t> ids;
    for (const auto& asset : all.assets) {
        if (!check((asset.id & 0xf0000000u) == 0x50000000u && ids.insert(asset.id).second, "ids of their own")) return 2;
        if (!check(std::filesystem::path(asset.url).is_absolute() && std::filesystem::is_regular_file(asset.url) && asset.preview == asset.url, "the file itself, played as the preview")) return 3;
    }
    const auto& ocean = all.assets[3];
    if (!check(std::filesystem::equivalent(ocean.url, workshop / L"100" / L"ocean.mp4") &&
               std::filesystem::equivalent(ocean.thumbnail, workshop / L"100" / L"preview.png") && all.assets[0].thumbnail.empty(), "file and preview picture")) return 4;
    if (!check(std::filesystem::equivalent(all.assets[1].url, workshop / L"106" / L"clips" / L"ten.webm"), "a file in a subfolder of the item")) return 5;
    // The same item keeps its id between scans (the applied wallpaper stays marked).
    if (!check(wallpaper_sources::wallpaperEngine(L"", 0, {library}).assets.back().id == ocean.id, "stable ids")) return 6;

    // The search: words of the title, any case.
    const auto clips = wallpaper_sources::wallpaperEngine(L"  CLIP ", 0, {library, other});
    if (!check(clips.assets.size() == 2 && clips.error.empty(), "search by title")) return 7;
    if (!check(wallpaper_sources::wallpaperEngine(L"nothing like it", 0, {library}).assets.empty() &&
               !wallpaper_sources::wallpaperEngine(L"nothing like it", 0, {library}).error.empty(), "no match: a message")) return 8;
    if (!check(!wallpaper_sources::wallpaperEngine(L"", 0, {empty}).error.empty() && !wallpaper_sources::wallpaperEngine(L"", 0, {}).error.empty(),
               "no Wallpaper Engine: a message")) return 9;

    // Pages of 24.
    const auto many = root / L"Many";
    for (int i = 1; i <= 25; ++i)
        item(many / L"steamapps" / L"workshop" / L"content" / L"431960" / std::to_wstring(i),
             R"({"type":"video","file":"v.mp4","title":"Loop )" + std::to_string(i) + R"("})", L"v.mp4");
    const auto first = wallpaper_sources::wallpaperEngine(L"", 0, {many}), second = wallpaper_sources::wallpaperEngine(L"", 1, {many});
    if (!check(first.assets.size() == 24 && first.more && second.assets.size() == 1 && !second.more &&
               second.assets[0].author == L"Wallpaper Engine · Loop 25", "paged")) return 10;

    // The tile: the preview picture from disk, as a small JPEG; never a path another source sent.
    const auto tile = wallpaper_sources::thumbnail(ocean, [](const std::wstring&, const std::wstring&, size_t) { return HttpResponse{}; });
    if (!check(decodableArtwork(tile) && tile.size() > 2 && tile[0] == 0xff && tile[1] == 0xd8, "the preview picture as a tile")) return 11;
    GridAsset foreign = ocean;
    foreign.id = 0x10000001u;
    bool asked = false;
    const auto refused = wallpaper_sources::thumbnail(foreign, [&](const std::wstring&, const std::wstring&, size_t) { asked = true; return HttpResponse{}; });
    if (!check(refused.empty() && !asked, "a local path from a site is not read")) return 12;
    if (!check(wallpaper_sources::thumbnail(all.assets[0]).empty(), "no preview picture: no tile")) return 13;

    // The moving preview and the chosen wallpaper: the file where it is; earlier downloads go.
    const auto folder = root / L"config" / L"wallpapers";
    write(folder / L"video-1.mp4", "an earlier download");
    if (!check(wallpaper_sources::preview(ocean, folder) == std::filesystem::path(ocean.url) && !std::filesystem::exists(folder / L"previews"), "preview from disk")) return 14;
    if (!check(wallpaper_sources::preview(foreign, folder).empty(), "a foreign local preview is not played")) return 15;
    std::wstring error;
    const auto chosen = wallpaper_sources::store(ArtworkProvider::wallpaper_engine, ocean, folder, error);
    if (!check(chosen == std::filesystem::path(ocean.url) && error.empty() && !std::filesystem::exists(folder / L"video-1.mp4") &&
               std::filesystem::exists(ocean.url), "played where it is, not copied")) return 16;
    if (!check(wallpaper_sources::video(ArtworkProvider::wallpaper_engine) && wallpaper_sources::own(ArtworkProvider::wallpaper_engine), "a video source")) return 17;
    if (!check(wallpaper_sources::store(ArtworkProvider::wallpaper_engine, foreign, folder, error).empty() && !error.empty(), "only its own files")) return 18;
    GridAsset gone = ocean;
    gone.url = (workshop / L"104" / L"missing.mp4").wstring();
    error.clear();
    if (!check(wallpaper_sources::store(ArtworkProvider::wallpaper_engine, gone, folder, error).empty() && !error.empty(), "an unsubscribed file")) return 19;
    std::cout << "Wallpaper Engine video scan, tiles, previews and choice passed\n";
}
