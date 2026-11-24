// Renderer: Siatka's "Kolekcje jako kafle" above the games (collection_band in src/ui/grid_layout.hpp; the
// Launcher side is src/app/library_collections.cpp). "Kolekcje: N", the tiles (three covers, the name, how
// many games; the focused one with the white ring, springing between them), then the shown games' count and their order.
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
    if (band_covers_.size() != tiles.size() * 3) band_covers_.resize(tiles.size() * 3);

    // The tiles, clipped to the band's width (the focused one's ring included); one cut at the right edge
    // shows that more follow. Covers take the grid's art and the tiles' shape (Kształt kafelków).
    const float tiles_top = cb::tilesTop(scroll), tile_h = cb::tileHeight(width, options), cover_h = cb::coverHeight(width, options);
    const float radius = tile_shape_ == AppSettings::TileShape::square ? 3.0f : tile_shape_ == AppSettings::TileShape::rounded ? 12.0f : 18.0f;
    target_->PushAxisAlignedClip(D2D1::RectF(left - 10, tiles_top - 10, right + 10, tiles_top + tile_h + 10), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    for (size_t i = 0; i < tiles.size(); ++i) {
        const auto r = cb::tile(static_cast<int>(i), width, scroll, context_->band_slide, options);
        if (r.right < left - 10 || r.left > right + 10) continue;
        const bool focused = context_->band_focus == static_cast<int>(i), hover = context_->band_hover == static_cast<int>(i);
        brush(gray(1, focused ? 0.15f : hover ? 0.11f : 0.065f));
        target_->FillRoundedRectangle(D2D1::RoundedRect(box(r), radius, radius), solid_.Get());
        for (int slot = 0; slot < 3; ++slot) {
            const auto c = cb::cover(r, slot, width, options);
            const auto shape = tileShape(box(c), tile_shape_);
            brush(gray(1, 0.05f)); // The frame of a missing cover (or a collection of fewer games).
            if (shape) target_->FillGeometry(shape.Get(), solid_.Get());
            auto& image = band_covers_[i * 3 + static_cast<size_t>(slot)];
            if (auto* shown = dialogImage(image, tiles[i].covers[static_cast<size_t>(slot)])) {
                if (options.grid_posters && !options.grid_landscape) fillPoster(*shown, box(c), 10, reveal(*shown), false, shape.Get());
                else fillImage(*shown, box(c), 10, reveal(*shown), false, shape.Get());
            }
        }
        const float text_left = r.left + cb::pad + 4, text_width = r.right - r.left - 2 * cb::pad - 8, below = r.top + cb::pad + cover_h;
        textLine(tiles[i].name, text_left, below + 22, text_width, menu_font_.Get(), gray(focused || hover ? 0.98f : 0.9f));
        textLine(tiles[i].count, text_left, below + 46, text_width, toast_sub_font_.Get(), gray(0.6f));
    }
    // The ring springs between the tiles (Animacja fokusu), following the row as it slides.
    if (context_->band_focus >= 0 && context_->band_focus < static_cast<int>(tiles.size()))
        tvFocusRing(cb::tile(context_->band_focus, width, scroll, context_->band_slide, options), radius, context_->band_glide);
    target_->PopAxisAlignedClip();

    // Under the tiles: the shown games and their order.
    const float cy = cb::summaryMiddle(width, scroll, options), half = (right - left) * 0.5f;
    textLine(context_->band_summary, left + 2, cy, half, menu_font_.Get(), gray(0.92f));
    textLine(context_->band_order, left + half, cy, half - 2, toast_sub_font_.Get(), gray(0.62f), DWRITE_TEXT_ALIGNMENT_TRAILING);
}
