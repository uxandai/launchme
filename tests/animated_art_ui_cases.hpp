// Animated SteamGridDB artwork: Zmień grafikę's tile moves at once (with its label), the pick keeps
// its animation beside the still, and on the shelf the card moves only once it rests in focus; the
// still comes back when the focus leaves. LAUNCHME_UI_ANIMATED_IMAGE=<animated .webp> uses a real
// animation for the captures (animated-gallery.png, animated-card.png) instead of the 8 x 12 fixture.
#include "animated_webp_fixture.hpp"

int LauncherUiTest::animatedArt(HWND window) {
    if (!animated_image::supported()) return 0;
    Launcher app; app.hwnd_ = window; app.execute_ = fakeExecute;
    app.foreground_ = [](HWND) { return true; };
    if (!app.renderer_.initialize(window)) return 4300;
    const auto capture = [&](const wchar_t* name) {
        wchar_t folder[4096]{};
        if (!GetEnvironmentVariableW(L"LAUNCHME_UI_CAPTURE_DIR", folder, 4096)) return true;
        return app.renderer_.captureForTest(std::filesystem::path(folder) / name, [&] { app.paint(); });
    };
    auto webp = animatedWebpFixture();
    if (wchar_t file[4096]{}; GetEnvironmentVariableW(L"LAUNCHME_UI_ANIMATED_IMAGE", file, 4096)) {
        std::ifstream input(file, std::ios::binary); webp.assign(std::istreambuf_iterator<char>{input}, {});
    }
    if (!animated_image::animatedWebp(webp)) return 4301;

    auto library = std::make_unique<LibraryMessage>(); library->account = 1;
    for (uint32_t i = 0; i < 4; ++i) { Game game{4300 + i, L"Animated game " + std::to_wstring(i)}; game.last_played = 1700000000 - i; library->games.push_back(game); }
    app.onLibrary(std::move(library)); app.showCategory(0);
    app.snapCardsForTransition(); app.detail_ = 1;
    std::vector<size_t> games;
    for (size_t i = 0; i < app.cards_.size(); ++i) if (isGame(app.cards_[i].game)) games.push_back(i);
    if (games.size() < 2) return 4302;
    const auto chosen = app.cards_[games[0]].game;

    // Zmień grafikę: the animated asset first, moving at once.
    app.openArtworkBrowser(chosen);
    if (!app.artworkOpen()) return 4303;
    auto listed = std::make_unique<ArtworkBrowserMessage>();
    listed->generation = app.artwork_jobs_->generation.load();
    listed->page = parseGridPage(R"({"data":[{"id":4310,"width":600,"height":900,"url":"https://example.invalid/a.webp","thumb":"https://example.invalid/a.webm"},)"
                                 R"({"id":4311,"width":600,"height":900,"url":"https://example.invalid/s.png","thumb":"https://example.invalid/s.png"}]})");
    if (listed->page.assets.size() != 2) return 4304;
    listed->page.assets[0].animated = true;
    app.onArtworkBrowser(std::move(listed));
    // The tiles' jobs: the animated one carries its flag to the worker.
    {
        std::lock_guard lock(app.artwork_jobs_->mutex);
        const auto moving = std::find_if(app.artwork_jobs_->jobs.begin(), app.artwork_jobs_->jobs.end(), [](const auto& job) { return job.asset.id == 4310; });
        if (moving == app.artwork_jobs_->jobs.end() || !moving->asset.animated || moving->type != ArtworkBrowserMessage::Type::thumbnail) return 4305;
    }
    auto loop = animated_image::decodeLoop(webp, 360, 360, 8u << 20, 30, 100);
    if (!loop || loop->pixels.size() < 2) return 4306;
    auto tile = std::make_unique<ArtworkBrowserMessage>();
    tile->type = ArtworkBrowserMessage::Type::thumbnail; tile->generation = app.artwork_jobs_->generation.load(); tile->asset_id = 4310;
    tile->animation = std::make_shared<const animated_image::Frames>(std::move(*loop));
    app.onArtworkBrowser(std::move(tile));
    app.paint();
    size_t first_frame = 0, later_frame = 0;
    if (!app.renderer_.artworkThumbnailMovingForTest(0, &first_frame)) return 4307;
    // The next frame comes due (the launcher paints then) and the tile shows it.
    bool due = false;
    for (const auto end = GetTickCount64() + 1000; GetTickCount64() < end && !due; Sleep(5)) due = app.renderer_.artworkAnimationDue();
    if (!due) return 4308;
    app.paint();
    if (!app.renderer_.artworkThumbnailMovingForTest(0, &later_frame) || later_frame == first_frame) return 4309;
    if (!capture(L"animated-gallery.png")) return 4312;
    // Seen again within the visit: the loop comes back from the page cache.
    app.artwork_state_.page = 0; app.loadArtworkPage();
    app.paint();
    if (!app.renderer_.artworkThumbnailMovingForTest(0)) return 4313;

    // Applied: the still is the variant every view binds; the animation stays beside it.
    const auto cache = coverCacheDirectory();
    const auto motion = artworkAnimationPath(chosen, ArtworkKind::cover, 4310, cache);
    std::filesystem::create_directories(motion.parent_path());
    { std::ofstream output(motion, std::ios::binary); output.write(reinterpret_cast<const char*>(webp.data()), static_cast<std::streamsize>(webp.size())); }
    const auto still = animatedStill(motion);
    if (still.empty() || !cacheArtworkVariant(chosen, ArtworkKind::cover, 4310, still, cache)) return 4314;
    app.animatedArtFile(chosen, ArtworkKind::cover); // Looked up before the pick: forgotten once it applies.
    app.artwork_state_.gallery = true; app.artwork_state_.selected = 0; app.artwork_state_.applying = true;
    auto applied = std::make_unique<ArtworkBrowserMessage>();
    applied->type = ArtworkBrowserMessage::Type::apply; applied->generation = app.artwork_jobs_->generation.load();
    applied->asset_id = 4310; applied->bytes = still;
    app.onArtworkBrowser(std::move(applied));
    if (selectedArtworkId(chosen, ArtworkKind::cover, cache) != 4310 || selectedArtworkAnimation(chosen, ArtworkKind::cover, cache) != motion ||
        app.animated_art_files_.contains(gameKey(chosen))) return 4315;
    app.back(); app.back();
    if (app.artworkOpen()) return 4316;

    // The shelf: the still until the card rests in focus, then frames in its cover slot.
    const size_t card = std::find_if(app.cards_.begin(), app.cards_.end(), [&](const GameCard& c) { return gameKey(c.game) == gameKey(chosen); }) - app.cards_.begin();
    if (card >= app.cards_.size()) return 4317;
    app.navigate(static_cast<int>(card) - static_cast<int>(app.selected_), false);
    app.snapCardsForTransition(); app.detail_ = 1;
    const auto* art = app.art_.find(gameKey(chosen));
    if (!art || !art->cover || *art->cover != still) return 4318;
    app.renderer_.setCover(card, art->cover, art->focus);
    app.paint(); app.paint();
    auto* still_bitmap = app.renderer_.coverBitmapForTest(card);
    if (!still_bitmap) return 4319;
    bool painted = false;
    app.tickAnimatedArt(painted);
    if (app.renderer_.animatedArtShown() || !app.animated_art_plays_.empty()) return 4320; // A focus only passing through.
    app.animated_art_since_ -= std::chrono::seconds(1);
    const auto end = GetTickCount64() + 3000;
    while (GetTickCount64() < end && !app.renderer_.animatedArtShown()) { app.tickAnimatedArt(painted); Sleep(10); }
    if (!app.renderer_.animatedArtShown() || !painted || app.renderer_.coverBitmapForTest(card) == still_bitmap) return 4321;
    auto* moving_bitmap = app.renderer_.coverBitmapForTest(card);
    const auto moving_size = moving_bitmap->GetPixelSize(), still_size = still_bitmap->GetPixelSize();
    if (moving_size.width > still_size.width || moving_size.height > still_size.height ||
        std::abs(static_cast<float>(moving_size.width) / moving_size.height - static_cast<float>(still_size.width) / still_size.height) > 0.05f) return 4322;
    // Later frames go into the same bitmap.
    painted = false;
    const auto more = GetTickCount64() + 1000;
    while (GetTickCount64() < more && !painted) { app.tickAnimatedArt(painted); Sleep(10); }
    if (!painted || app.renderer_.coverBitmapForTest(card) != moving_bitmap) return 4323;
    // The focus leaves: the still comes back at once.
    const size_t other = card == games[0] ? games[1] : games[0];
    app.navigate(static_cast<int>(other) - static_cast<int>(app.selected_), false);
    app.tickAnimatedArt(painted);
    if (app.renderer_.animatedArtShown() || !app.animated_art_plays_.empty() || app.renderer_.coverBitmapForTest(card) != still_bitmap) return 4325;
    // Back on it and resting: it moves again (the capture shows a frame).
    app.navigate(static_cast<int>(card) - static_cast<int>(app.selected_), false);
    app.tickAnimatedArt(painted);
    app.animated_art_since_ -= std::chrono::seconds(1);
    const auto again = GetTickCount64() + 3000;
    while (GetTickCount64() < again && !app.renderer_.animatedArtShown()) { app.tickAnimatedArt(painted); Sleep(10); }
    if (!app.renderer_.animatedArtShown()) return 4326;
    if (wchar_t folder[4096]{}; GetEnvironmentVariableW(L"LAUNCHME_UI_CAPTURE_DIR", folder, 4096)) {
        // The shelf settled (its entrance done) and the card moving again for the picture.
        for (int i = 0; i < 40; ++i) { app.last_frame_ -= std::chrono::milliseconds(50); app.tick(); }
        app.animated_art_since_ -= std::chrono::seconds(1);
        for (const auto until = GetTickCount64() + 3000; GetTickCount64() < until && !app.renderer_.animatedArtShown(); Sleep(10)) app.tickAnimatedArt(painted);
        Sleep(400); app.tickAnimatedArt(painted); // A later frame than the still.
        if (!capture(L"animated-card.png")) return 4324;
    }
    app.paint(); // The capture made its own target: the stills are bound again, frames follow on the window's.
    app.navigate(static_cast<int>(other) - static_cast<int>(app.selected_), false);
    app.tickAnimatedArt(painted);
    // A still pick on another card never plays.
    app.animated_art_since_ -= std::chrono::seconds(1);
    for (int i = 0; i < 20; ++i) { app.tickAnimatedArt(painted); Sleep(5); }
    if (app.renderer_.animatedArtShown() || !app.animated_art_plays_.empty()) return 4327;
    return 0;
}
