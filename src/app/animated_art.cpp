// Animated SteamGridDB picks in the views: the still first frame is what every layout binds (it is
// the variant on disk); only the card with the focus, after a short rest, plays the animated WebP
// kept beside it (cover_cache.hpp: selectedArtworkAnimation), its frames standing in for the still
// in whichever renderer slot shows it (animated_art_renderer.cpp). In the desktop window the card
// under the pointer plays first.
#include "launcher.hpp"
#include "../artwork/cover_cache.hpp"

namespace {
// A focus that only passes through (holding a direction) starts nothing.
constexpr auto animated_art_rest = std::chrono::milliseconds(350);
constexpr std::array<ArtworkKind, 4> animated_kinds{ArtworkKind::cover, ArtworkKind::poster, ArtworkKind::landscape, ArtworkKind::hero};
} // namespace

const std::filesystem::path& Launcher::animatedArtFile(const Game& game, ArtworkKind kind) {
    const auto key = gameKey(game);
    auto found = animated_art_files_.find(key);
    if (found == animated_art_files_.end()) {
        // Looked up once per game (two small files a type), when its card first rests in focus.
        std::array<std::filesystem::path, 6> files;
        const auto cache = coverCacheDirectory();
        for (const auto each : animated_kinds) files[static_cast<size_t>(each)] = selectedArtworkAnimation(game, each, cache);
        if (animated_art_files_.size() > 4096) animated_art_files_.clear();
        found = animated_art_files_.emplace(key, std::move(files)).first;
    }
    return found->second[static_cast<size_t>(kind)];
}

void Launcher::forgetAnimatedArt(const std::string& key) {
    animated_art_files_.erase(key);
    if (key == animated_art_key_) { stopAnimatedArt(); animated_art_index_ = SIZE_MAX; animated_art_key_.clear(); }
}

void Launcher::stopAnimatedArt() {
    animated_art_plays_.clear(); // Each player's thread ends after the frame it is decoding.
    animated_art_failed_.clear();
    if (renderer_.animatedArtShown()) {
        renderer_.clearAnimatedArt();
        InvalidateRect(hwnd_, nullptr, FALSE); // The still back where a frame was.
    }
}

void Launcher::tickAnimatedArt(bool& needs_paint) {
    using desktop_ui::Target;
    size_t index = SIZE_MAX;
    const bool seen = windowWatched() && !IsIconic(hwnd_) && !background_mode_ && !screensaverActive() && !pending_launch_ &&
                      !artworkOpen() && !settingsOpen() && !cropOpen() && !closing_;
    if (seen && !cards_.empty()) {
        const bool hovered = desktop_mode_ && !desktop_drag_.active && desktop_hover_.index >= 0 &&
            (desktop_hover_.target == Target::card || desktop_hover_.target == Target::card_play || desktop_hover_.target == Target::card_check);
        index = hovered ? static_cast<size_t>(desktop_hover_.index) : selected_;
    }
    if (index >= cards_.size() || !isGame(cards_[index].game)) index = SIZE_MAX;
    const auto key = index == SIZE_MAX ? std::string{} : gameKey(cards_[index].game);
    const auto now = std::chrono::steady_clock::now();
    if (index != animated_art_index_ || key != animated_art_key_) {
        stopAnimatedArt();
        animated_art_index_ = index;
        animated_art_key_ = key;
        animated_art_since_ = now;
    }
    if (index == SIZE_MAX || key.empty() || now - animated_art_since_ < animated_art_rest) return;
    const auto* art = art_.find(key);
    if (!art) return;
    const auto stillOf = [](const CachedArt& cached, ArtworkKind kind) {
        return kind == ArtworkKind::cover ? cached.cover : kind == ArtworkKind::poster ? cached.poster :
               kind == ArtworkKind::landscape ? cached.landscape : kind == ArtworkKind::hero ? cached.hero : ImageBytes{};
    };
    // A picture bound anew (another pick, the cache read again): its animation starts over.
    for (const auto& play : animated_art_plays_)
        if (stillOf(*art, play->kind) != play->still) { stopAnimatedArt(); animated_art_since_ = now; return; }
    const auto& game = cards_[index].game;
    for (const auto kind : animated_kinds) {
        if (std::find(animated_art_failed_.begin(), animated_art_failed_.end(), kind) != animated_art_failed_.end()) continue;
        if (std::any_of(animated_art_plays_.begin(), animated_art_plays_.end(), [&](const auto& play) { return play->kind == kind; })) continue;
        auto still = stillOf(*art, kind);
        if (!still) continue;
        // Played only where a slot of this card shows that still now (its size: the frames' size).
        const auto size = renderer_.animatedArtSize(index, still);
        if (!size.width || !size.height) continue;
        const auto& file = animatedArtFile(game, kind);
        if (file.empty()) continue;
        auto play = std::make_unique<AnimatedArtPlay>();
        play->kind = kind;
        play->still = std::move(still);
        if (!play->player.open(file, size.width, size.height)) { animated_art_failed_.push_back(kind); continue; }
        animated_art_plays_.push_back(std::move(play));
    }
    for (auto it = animated_art_plays_.begin(); it != animated_art_plays_.end();) {
        auto& play = **it;
        if (play.player.failed()) { animated_art_failed_.push_back(play.kind); it = animated_art_plays_.erase(it); continue; }
        uint32_t width = 0, height = 0;
        if (play.player.frame(play.frame, width, height) && renderer_.setAnimatedArtFrame(index, play.still, play.frame.data(), width, height))
            needs_paint = true;
        ++it;
    }
}
