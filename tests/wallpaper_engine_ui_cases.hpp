// Interfejs → Tapeta → Animowane: "Wallpaper Engine (Video)" is the third source; its gallery lists
// the subscribed videos (wallpaper_engine_tests.cpp covers the scan) with their preview pictures, and
// the focused one's moving preview is the file itself. LAUNCHME_UI_WALLPAPER_ENGINE_LIBRARY=<Steam
// library folder> lists that library's real items for the captures (wallpaper-engine*.png) instead
// of the two made up here.
#include "../src/artwork/wallpaper_sources.hpp"

int LauncherUiTest::wallpaperEngine(HWND window) {
    Launcher app; app.hwnd_ = window; app.execute_ = fakeExecute;
    app.foreground_ = [](HWND) { return true; };
    if (!app.renderer_.initialize(window)) return 4400;
    const auto capture = [&](const wchar_t* name) {
        wchar_t folder[4096]{};
        if (!GetEnvironmentVariableW(L"LAUNCHME_UI_CAPTURE_DIR", folder, 4096)) return true;
        return app.renderer_.captureForTest(std::filesystem::path(folder) / name, [&] { app.paint(); });
    };
    auto library = std::filesystem::temp_directory_path() / (L"launchme-ui-wallpaper-engine-" + std::to_wstring(GetCurrentProcessId()));
    struct Cleanup { std::filesystem::path root; ~Cleanup() { std::error_code ec; std::filesystem::remove_all(root, ec); } } cleanup{library};
    const auto workshop = library / L"steamapps" / L"workshop" / L"content" / L"431960";
    const auto write = [](const std::filesystem::path& file, std::string_view text) {
        std::filesystem::create_directories(file.parent_path());
        std::ofstream output(file, std::ios::binary); output.write(text.data(), static_cast<std::streamsize>(text.size()));
    };
    const auto picture = pngFixture();
    for (const auto& [id, title, type] : {std::tuple{"1", "Neon City Rain", "video"}, std::tuple{"2", "Forest Loop", "video"}, std::tuple{"3", "A Scene", "scene"}}) {
        write(workshop / id / L"project.json", std::string(R"({"type":")") + type + R"(","file":"clip.mp4","preview":"preview.png","title":")" + title + R"("})");
        write(workshop / id / L"clip.mp4", "video");
        write(workshop / id / L"preview.png", std::string_view(reinterpret_cast<const char*>(picture.data()), picture.size()));
    }
    if (wchar_t real[4096]{}; GetEnvironmentVariableW(L"LAUNCHME_UI_WALLPAPER_ENGINE_LIBRARY", real, 4096)) library = real;

    app.openWallpaperBrowser();
    if (!app.artworkOpen() || !app.artwork_state_.wallpaper) return 4401;
    app.showArtworkCategory(1);
    const auto& choices = app.artwork_state_.choices;
    if (choices != std::vector{ArtworkProvider::motionbgs, ArtworkProvider::steam_backgrounds, ArtworkProvider::wallpaper_engine}) return 4402;
    app.chooseArtworkProvider(2);
    if (app.artwork_state_.activeProvider() != ArtworkProvider::wallpaper_engine) return 4403;
    {
        std::lock_guard lock(app.artwork_jobs_->mutex);
        const auto& jobs = app.artwork_jobs_->jobs;
        if (std::none_of(jobs.begin(), jobs.end(), [](const auto& job) {
                return job.type == ArtworkBrowserMessage::Type::list && job.wallpaper && job.provider == ArtworkProvider::wallpaper_engine; })) return 4404;
        app.artwork_jobs_->jobs.clear();
    }
    auto listed = std::make_unique<ArtworkBrowserMessage>();
    listed->generation = app.artwork_jobs_->generation.load(); listed->wallpaper = true;
    listed->page = wallpaper_sources::wallpaperEngine(L"", 0, {library});
    const auto assets = listed->page.assets;
    if (assets.empty() || (!GetEnvironmentVariableW(L"LAUNCHME_UI_WALLPAPER_ENGINE_LIBRARY", nullptr, 0) && assets.size() != 2)) return 4405;
    app.onArtworkBrowser(std::move(listed));
    if (app.artwork_state_.assets.size() != assets.size() || !app.artwork_state_.status.empty()) return 4406;
    // Tiles from the preview pictures; the focused video's preview is the file where it is.
    {
        std::lock_guard lock(app.artwork_jobs_->mutex);
        const auto& jobs = app.artwork_jobs_->jobs;
        const auto preview = std::find_if(jobs.begin(), jobs.end(), [](const auto& job) { return job.type == ArtworkBrowserMessage::Type::preview; });
        if (preview == jobs.end() || preview->provider != ArtworkProvider::wallpaper_engine || preview->asset.id != assets[0].id) return 4407;
        if (wallpaper_sources::preview(preview->asset, library / L"unused") != std::filesystem::path(assets[0].url)) return 4408;
    }
    for (const auto& asset : assets) {
        auto tile = std::make_unique<ArtworkBrowserMessage>();
        tile->type = ArtworkBrowserMessage::Type::thumbnail; tile->generation = app.artwork_jobs_->generation.load();
        tile->asset_id = asset.id; tile->wallpaper = true;
        tile->bytes = wallpaper_sources::thumbnail(asset);
        if (tile->bytes.empty()) return 4409;
        app.onArtworkBrowser(std::move(tile));
    }
    app.paint();
    if (!capture(L"wallpaper-engine.png")) return 4410;
    app.openArtworkProviders();
    if (!app.artwork_state_.provider_open || app.artwork_state_.provider_row != 2) return 4411;
    app.paint();
    if (!capture(L"wallpaper-engine-sources.png")) return 4412;
    return 0;
}
