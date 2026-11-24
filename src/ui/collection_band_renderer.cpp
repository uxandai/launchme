// Renderer: Siatka's "Kolekcje jako kafle" above the games (collection_band in src/ui/grid_layout.hpp; the
// Launcher side is src/app/library_collections.cpp). "Kolekcje: N", the tiles (each one of the grid's cells
// in its shape: four covers, or one for a single game; the name and how many games under it; the focused
// one with the white ring, springing between them), then the shown games' count and their order.
#include "renderer.hpp"
#include "renderer_internal.hpp"
#include "grid_layout.hpp"

#include <algorithm>

using namespace render_detail;

namespace {
using palette::gray;
}

void Renderer::drawCollectionBand() {
    if (!context_ || !context_->band) return;
    namespace cb = collection_band;
    const auto& options = context_->library_options;
    const float width = viewport_.width, scroll = context_->grid_scroll;
    const float top = cb::top(scroll);
    if (top + cb::height(width, options) < 40) return; // Scrolled away under the header.
    const float left = cb::left(width, options), right = cb::right(width, options);
    const auto& tiles = context_->band_tiles;
    textLine(std::wstring(tr(L"Kolekcje")) + L": " + std::to_wstring(tiles.size()), left + 2, top + cb::caption * 0.5f - 4, right - left,
             menu_font_.Get(), gray(0.92f));
    if (band_covers_.size() != tiles.size() * 4) band_covers_.resize(tiles.size() * 4);

    // The tiles, clipped to the band's width (the focused one's ring included); one cut at the right edge
    // shows that more follow. Each is the grid's cell: its art's shape, its corners (Kształt kafelków).
    const float tiles_top = cb::tilesTop(scroll), tile_h = cb::tileHeight(width, options);
    const bool posters = options.grid_posters && !options.grid_landscape;
    target_->PushAxisAlignedClip(D2D1::RectF(left - 12, tiles_top - 12, right + 12, tiles_top + tile_h + cb::text + 4), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    for (size_t i = 0; i < tiles.size(); ++i) {
        const auto r = cb::tile(static_cast<int>(i), width, scroll, context_->band_slide, options);
        if (r.right < left - 12 || r.left > right + 12) continue;
        const bool focused = context_->band_focus == static_cast<int>(i), hover = context_->band_hover == static_cast<int>(i);
        const auto shape = tileShape(box(r), tile_shape_);
        if (!shape) continue;
        brush(gray(1, focused ? 0.12f : hover ? 0.1f : 0.06f));
        target_->FillGeometry(shape.Get(), solid_.Get());
        // The covers inside the tile's shape: four quarters of it, or one cover for a single game.
        target_->PushLayer(D2D1::LayerParameters(box(r), shape.Get()), nullptr);
        const size_t games = tiles[i].games, slots = games <= 1 ? 1 : 4;
        for (size_t slot = 0; slot < slots; ++slot) {
            const auto c = cb::cover(r, static_cast<int>(slot), games);
            auto& image = band_covers_[i * 4 + slot];
            if (auto* shown = dialogImage(image, tiles[i].covers[slot])) {
                if (posters) fillPoster(*shown, box(c), 0, reveal(*shown), false, nullptr);
                else fillImage(*shown, box(c), 0, reveal(*shown));
            } else {
                brush(gray(1, 0.04f)); // A missing cover (or a collection of fewer games).
                target_->FillRectangle(box(c), solid_.Get());
            }
        }
        target_->PopLayer();
        // Under it, as a game's caption: the name, then how many games.
        const float mid = (r.left + r.right) * 0.5f, text_width = r.right - r.left + 20;
        textLine(tiles[i].name, mid - text_width * 0.5f, r.bottom + 20, text_width, menu_font_.Get(), gray(focused || hover ? 0.98f : 0.88f),
                 DWRITE_TEXT_ALIGNMENT_CENTER);
        textLine(tiles[i].count, mid - text_width * 0.5f, r.bottom + 43, text_width, toast_sub_font_.Get(), gray(0.6f), DWRITE_TEXT_ALIGNMENT_CENTER);
    }
    // The ring in the tiles' shape, springing between them (Animacja fokusu) as the row slides.
    if (context_->band_focus >= 0 && context_->band_focus < static_cast<int>(tiles.size())) {
        auto r = cb::tile(context_->band_focus, width, scroll, context_->band_slide, options);
        if (const auto& glide = context_->band_glide) r = {glide->left, glide->top, glide->right, glide->bottom};
        if (const auto ring = tileShape(D2D1::RectF(r.left - 6, r.top - 6, r.right + 6, r.bottom + 6), tile_shape_)) {
            brush(gray(0.96f));
            target_->DrawGeometry(ring.Get(), solid_.Get(), 2.5f);
        }
    }
    target_->PopAxisAlignedClip();

    // Under the tiles: the shown games and their order.
    const float cy = cb::summaryMiddle(width, scroll, options), half = (right - left) * 0.5f;
    textLine(context_->band_summary, left + 2, cy, half, menu_font_.Get(), gray(0.92f));
    textLine(context_->band_order, left + half, cy, half - 2, toast_sub_font_.Get(), gray(0.62f), DWRITE_TEXT_ALIGNMENT_TRAILING);
}
