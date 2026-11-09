// Desktop mode's poster grid: the header line, the empty library and each poster (artwork, badges,
// marks, progress and its caption).
#include "renderer.hpp"
#include "desktop_render_internal.hpp"
#include "brand_icons.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

using Microsoft::WRL::ComPtr;
using namespace desktop_ui;
using namespace desktop_draw;

// ---- Grid ------------------------------------------------------------------------------------

void Renderer::drawDesktopHeader(const Layout& layout, const View& view) {
    const float ty = layout.headerTitleY();
    const float title_room = layout.titleRight() - layout.gridLeft() - 70;
    float title_width = 0;
    if (!view.title_mark.empty() && hasPlatformMark(view.title_mark)) // A studio: its logo is the title.
        title_width = drawPlatformMark(view.title_mark, layout.gridLeft(), ty, 36, std::min(title_room, 360.0f), 1.0f / 0.9f, desktop_heading_font_.Get());
    else {
        title_width = std::min(textAdvance(view.title, desktop_heading_font_.Get()), title_room);
        textLine(view.title, layout.gridLeft(), ty, title_width + 2, desktop_heading_font_.Get(), palette::text());
    }
    if (!view.scanning) {
        const auto count = std::to_wstring(view.count);
        const float w = textAdvance(count, desktop_small_font_.Get()) + 16;
        const Rect pill{layout.gridLeft() + title_width + 12, ty - 11, layout.gridLeft() + title_width + 12 + w, ty + 11};
        desktopFill(pill, 11, gray(1, 0.07f));
        textLine(count, pill.left, ty, w, desktop_small_font_.Get(), gray(0.78f), DWRITE_TEXT_ALIGNMENT_CENTER);
    }
    // Sort, the installed filter and the poster size.
    const auto sort = layout.sortButton();
    const float sort_hover = view.hover.target == Target::sort || (view.menu && view.menu->kind == Menu::Kind::sort) ? 1.0f : 0.0f;
    desktopFill(sort, 8, gray(1, 0.04f + 0.04f * sort_hover));
    desktopStroke(sort, 8, gray(1, 0.085f + 0.06f * sort_hover));
    const float sy = (sort.top + sort.bottom) * 0.5f;
    desktopGlyph(glyph::sort, sort.left + 16, sy, 13, gray(0.72f));
    textLine(view.sort_label, sort.left + 32, sy, sort.right - sort.left - 56, desktop_font_.Get(), gray(0.9f));
    desktopGlyph(glyph::chevron_down, sort.right - 15, sy + 1, 9, gray(0.65f));
    {
        // The cells' shape: posters, squares or landscape (a menu).
        const auto shape = layout.viewButton();
        const float vh = view.hover.target == Target::view_mode || (view.menu && view.menu->kind == Menu::Kind::view) ? 1.0f : 0.0f;
        desktopFill(shape, 8, gray(1, 0.04f + 0.04f * vh));
        desktopStroke(shape, 8, gray(1, 0.085f + 0.06f * vh));
        const float vy = (shape.top + shape.bottom) * 0.5f, vx = shape.left + 16;
        if (view.cell_view == 3) { // The list: three lines, each with its dot.
            for (int line = -1; line <= 1; ++line) {
                const float ly = vy + static_cast<float>(line) * 4.5f;
                desktopFill({vx - 7, ly - 1, vx - 5, ly + 1}, 1, gray(0.78f));
                desktopFill({vx - 3, ly - 0.8f, vx + 7, ly + 0.8f}, 0.8f, gray(0.78f));
            }
        } else {
            const float w = view.cell_view == 2 ? 14.0f : view.cell_view == 1 ? 11.0f : 9.0f, h = view.cell_view == 2 ? 8.0f : view.cell_view == 1 ? 11.0f : 13.0f;
            desktopStroke({vx - w * 0.5f, vy - h * 0.5f, vx + w * 0.5f, vy + h * 0.5f}, 2, gray(0.78f), 1.6f); // The cell itself, drawn to its proportions.
        }
        if (layout.viewLabel()) {
            constexpr const wchar_t* names[] = {trKey(L"Plakaty"), trKey(L"Kwadraty"), trKey(L"Poziome"), trKey(L"Lista")};
            const auto label = std::wstring(tr(L"Widok:")) + L" " + tr(names[std::clamp(view.cell_view, 0, 3)]);
            textLine(label, shape.left + 32, vy, shape.right - shape.left - 56, desktop_font_.Get(), gray(0.9f));
        }
        desktopGlyph(glyph::chevron_down, shape.right - 15, vy + 1, 9, gray(0.65f));
    }
    const auto chip = layout.installedChip();
    const bool chip_hover = view.hover.target == Target::installed;
    if (view.installed_only) { desktopFill(chip, 8, uiAccent(0.16f)); desktopStroke(chip, 8, uiAccent(0.75f)); }
    else { desktopFill(chip, 8, gray(1, chip_hover ? 0.08f : 0.04f)); desktopStroke(chip, 8, gray(1, chip_hover ? 0.145f : 0.085f)); }
    const float cy = (chip.top + chip.bottom) * 0.5f;
    desktopGlyph(view.installed_only ? glyph::check : glyph::download, chip.left + 16, cy, 12, view.installed_only ? uiAccent() : gray(0.72f));
    textLine(tr(L"Zainstalowane"), chip.left + 30, cy, chip.right - chip.left - 38, desktop_font_.Get(), gray(0.9f));
    if (layout.list) return; // The list's rows have one height: no size slider.
    const auto slider = layout.sizeSlider();
    const float y = (slider.top + slider.bottom) * 0.5f, left = slider.left + 7, right = slider.right - 7;
    const float level = levelFromPoster(view.poster), knob = left + (right - left) * level;
    const bool slider_hover = view.hover.target == Target::size_slider;
    desktopFill({left, y - 2, right, y + 2}, 2, gray(1, 0.13f));
    desktopFill({left, y - 2, knob, y + 2}, 2, uiAccent(0.9f));
    brush(gray(0.97f));
    target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(knob, y), slider_hover ? 7.5f : 6.5f, slider_hover ? 7.5f : 6.5f), solid_.Get());
}

void Renderer::drawDesktopEmpty(const Layout& layout, const View& view) {
    const auto m = layout.main();
    const float cx = (m.left + m.right) * 0.5f;
    if (view.scanning) {
        // Loading: the grid's shape, gently pulsing.
        const int columns = layout.columns();
        const int rows = std::max(1, static_cast<int>(layout.viewHeight() / layout.rowStep()) + 1);
        for (int i = 0; i < columns * rows; ++i) {
            const auto p = layout.poster(static_cast<size_t>(i), 0);
            const float pulse = 0.035f + 0.025f * (0.5f + 0.5f * std::sin(elapsed_ * 2.6f - static_cast<float>(i) * 0.35f));
            desktopFill(p, 10, gray(1, pulse));
            desktopFill({p.left, p.bottom + 10, p.left + (p.right - p.left) * 0.8f, p.bottom + 20}, 4, gray(1, pulse * 0.8f));
            desktopFill({p.left, p.bottom + 26, p.left + (p.right - p.left) * 0.45f, p.bottom + 34}, 4, gray(1, pulse * 0.6f));
        }
        return;
    }
    const auto& empty = view.empty;
    if (empty.title.empty()) return;
    const float cy = layout.emptyMessageY(!empty.primary.empty(), !empty.body.empty());
    brush(gray(1, 0.05f));
    target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy - 40), 32, 32), solid_.Get());
    if (empty.icon) desktopGlyph(empty.icon, cx, cy - 40, 24, gray(0.7f));
    const float width = std::min(460.0f, m.right - m.left - 80);
    textLine(empty.title, cx - width * 0.5f, cy + 14, width, desktop_title_font_.Get(), palette::text(), DWRITE_TEXT_ALIGNMENT_CENTER);
    desktopCenteredText(empty.body, cx, cy + 32, width, desktop_font_.Get(), palette::muted(), 3);
    const bool two = !empty.secondary.empty();
    if (!empty.primary.empty())
        desktopButton(layout.emptyButton(0, two), 2, view.hover.target == Target::empty_primary ? 1.0f : 0.0f, view.pressed.target == Target::empty_primary,
                      true, 0, empty.primary);
    if (two)
        desktopButton(layout.emptyButton(1, true), 1, view.hover.target == Target::empty_secondary ? 1.0f : 0.0f, view.pressed.target == Target::empty_secondary,
                      true, 0, empty.secondary);
}

void Renderer::drawDesktopGrid(const Layout& layout, const View& view) {
    const auto m = layout.main();
    drawDesktopHeader(layout, view);
    target_->PushAxisAlignedClip(D2D1::RectF(m.left, layout.gridTop(), m.right, m.bottom), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    if (!cards_ || cards_->empty()) drawDesktopEmpty(layout, view);
    else if (layout.list) {
        if (!view.continue_tiles.empty()) drawDesktopContinue(layout, view);
        const auto [first, end] = layout.visibleRange(cards_->size(), view.scroll, 1);
        for (size_t i = first; i < end; ++i) drawDesktopListRow(layout, view, i);
    } else {
        if (!view.continue_tiles.empty()) drawDesktopContinue(layout, view);
        const auto [first, end] = layout.visibleRange(cards_->size(), view.scroll, 1);
        // The hovered poster lifts above its neighbours: drawn last.
        size_t lifted = SIZE_MAX;
        for (size_t i = first; i < end; ++i) {
            if (i < view.card_hover.size() && view.card_hover[i] > 0.01f && lifted == SIZE_MAX) { lifted = i; continue; }
            drawDesktopPoster(layout, view, i);
        }
        if (lifted != SIZE_MAX) drawDesktopPoster(layout, view, lifted);
    }
    target_->PopAxisAlignedClip();
    // Scrolled: a soft edge under the header.
    if (view.scroll > 2)
        if (auto* shade = desktopGradient(desktop_shade_, gray(0, 0.32f), gray(0, 0), layout.gridTop(), layout.gridTop() + 16))
            target_->FillRectangle(D2D1::RectF(m.left, layout.gridTop(), m.right, layout.gridTop() + 16), shade);
    if (cards_ && view.scrollbar > 0.01f) {
        const auto thumb = layout.scrollThumb(cards_->size(), view.scroll);
        if (thumb.bottom > thumb.top) {
            const bool hover = view.hover.target == Target::scrollbar;
            const Rect bar{hover ? thumb.left : thumb.left + 2, thumb.top, thumb.right, thumb.bottom};
            desktopFill(bar, 4, gray(1, (hover ? 0.42f : 0.26f) * view.scrollbar));
        }
    }
}

// ---- Kontynuuj grę -----------------------------------------------------------------------------

void Renderer::setDesktopRecent(size_t slot, ImageBytes pixels) {
    if (slot >= desktop_recent_.size() || !pixels || pixels->empty()) return;
    auto& image = desktop_recent_[slot];
    if (image.pixels == pixels) return;
    image = {};
    image.short_edge = slot < 3 ? 160 : 420; // Sidebar thumbnails; the tiles' pictures.
    image.pixels = std::move(pixels);
    createBitmap(image);
}

bool Renderer::drawDesktopRecentImage(int slot, D2D1_RECT_F target, float radius, float opacity) {
    if (slot < 0 || slot >= static_cast<int>(desktop_recent_.size())) return false;
    auto& image = desktop_recent_[static_cast<size_t>(slot)];
    if (!image.bitmap) return false;
    fillImage(image, target, radius, opacity * reveal(image));
    return true;
}

// The last games played as wide tiles above Wszystkie gry: the picture, the name and where and when;
// hovered, Graj in its corner.
void Renderer::drawDesktopContinue(const Layout& layout, const View& view) {
    const float label_y = layout.continueLabelY(view.scroll);
    if (label_y > layout.gridTop() - 20)
        textLine(upper(tr(L"Kontynuuj grę")), layout.gridLeft() + 2, label_y, 400, desktop_label_font_.Get(), gray(0.6f));
    for (int i = 0; i < layout.continue_count && i < static_cast<int>(view.continue_tiles.size()); ++i) {
        const auto& tile = view.continue_tiles[static_cast<size_t>(i)];
        const auto r = layout.continueTile(i, view.scroll);
        if (r.bottom < layout.gridTop() || r.top > layout.height || tile.opacity < 0.01f) continue;
        const bool over = (view.hover.target == Target::continue_tile || view.hover.target == Target::continue_play) && view.hover.index == i;
        const float eased = smoothFade(tile.hover);
        const float dy = 10.0f * (1.0f - tile.opacity) - 4.0f * eased; // Rises into place as it fades in; lifts under the pointer.
        const Rect card{r.left, r.top + dy, r.right, r.bottom + dy};
        ComPtr<ID2D1Layer> entrance;
        const bool faded = tile.opacity < 0.995f && SUCCEEDED(target_->CreateLayer(nullptr, entrance.GetAddressOf()));
        if (faded) target_->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), nullptr, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE, D2D1::IdentityMatrix(), tile.opacity), entrance.Get());
        desktopShadow(card, 12, 0.45f + 0.55f * eased);
        desktopFill(card, 12, card_bg);
        const auto picture = box(card);
        if (!drawDesktopRecentImage(tile.image, picture, 12, 1)) {
            // Its square cover, dimmed across the tile until the wide picture arrives.
            if (drawDesktopRecentImage(tile.cover, picture, 12, 0.55f)) desktopFill(card, 12, gray(0, 0.25f));
        }
        if (!tile.installed) desktopFill(card, 12, gray(0, 0.3f));
        if (eased > 0.01f) desktopFill(card, 12, gray(1, 0.05f * eased)); // A touch brighter under the pointer.
        // The name over a dark fade at the bottom.
        if (auto* fade = desktopGradient(desktop_continue_fade_, gray(0, 0), gray(0, 0.78f), card.bottom - 74, card.bottom - 4)) {
            target_->PushAxisAlignedClip(D2D1::RectF(card.left, card.bottom - 74, card.right, card.bottom), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
            target_->FillRoundedRectangle(D2D1::RoundedRect(picture, 12, 12), fade);
            target_->PopAxisAlignedClip();
        }
        const auto play = Layout::playButton(card);
        const float text_right = card.right - 14 - (play.right - play.left + 10) * eased;
        textLine(tile.name, card.left + 14, card.bottom - 34, std::max(1.0f, text_right - card.left - 14), desktop_strong_font_.Get(), gray(0.98f));
        textLine(tile.detail, card.left + 14, card.bottom - 15, std::max(1.0f, text_right - card.left - 14), desktop_small_font_.Get(), gray(0.8f));
        desktopStroke(card, 12, eased > 0.01f ? uiAccent(0.85f * eased) : gray(1, 0.08f), 1.0f + 0.5f * eased);
        if (eased > 0.01f) { // Graj grows in from the corner.
            const bool on_play = over && view.hover.target == Target::continue_play;
            const float cx = (play.left + play.right) * 0.5f, cy = (play.top + play.bottom) * 0.5f, radius = (play.right - play.left) * 0.5f * (0.6f + 0.4f * eased);
            brush(on_play ? uiAccent(eased) : gray(0, 0.62f * eased));
            target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), radius, radius), solid_.Get());
            const auto accent = uiAccent();
            const bool light = 0.2126f * accent.r + 0.7152f * accent.g + 0.0722f * accent.b > 0.6f;
            desktopGlyph(tile.installed ? glyph::play : glyph::download, cx + (tile.installed ? 1.0f : 0.0f), cy, 15 * (0.6f + 0.4f * eased),
                         on_play && light ? gray(0.06f, eased) : gray(0.98f, eased));
        }
        if (faded) target_->PopLayer();
    }
}

// ---- The list ----------------------------------------------------------------------------------

// One game per row: its small cover, its name with its state, then its platform, when it was last
// played and its play time in columns (the narrower window drops them from the left).
void Renderer::drawDesktopListRow(const Layout& layout, const View& view, size_t index) {
    const auto& card = (*cards_)[index];
    const auto& game = card.game;
    const auto row = layout.poster(index, view.scroll);
    if (row.bottom < layout.gridTop() || row.top > layout.height || card.opacity < 0.01f) return;
    const float hover = index < view.card_hover.size() ? view.card_hover[index] : 0.0f;
    const bool selected = index == selected_;
    const bool marked = index < view.marked.size() && view.marked[index];
    const float alpha = card.opacity;
    if (selected) desktopFill(row, 8, gray(1, 0.075f * alpha));
    else if (marked) desktopFill(row, 8, uiAccent(0.12f * alpha));
    else if (hover > 0.01f) desktopFill(row, 8, gray(1, 0.045f * hover * alpha));
    if (selected) {
        const bool keyboard_here = view.keyboard && view.zone == 1;
        if (keyboard_here) desktopStroke(row, 8, uiAccent(alpha), 2);
        else desktopFill({row.left, row.top + 12, row.left + 3, row.bottom - 12}, 1.5f, uiAccent(0.95f * alpha));
    }
    // The cover.
    const auto thumb = Layout::listThumb(row);
    const auto session = sessionOf(game);
    const bool frozen = session == SessionState::frozen;
    const bool installing = game.installation.active();
    const bool colourless = frozen || (view.gray_uninstalled && !game.installed && !installing && game.kind != GameKind::catalog);
    Cover* art = index < posters_.size() && posters_[index].bitmap ? &posters_[index] : index < covers_.size() && covers_[index].bitmap ? &covers_[index] : nullptr;
    desktopFill(thumb, 6, gray(1, 0.06f * alpha));
    if (art) fillImage(*art, box(thumb), 6, alpha * card.cover_opacity * reveal(*art), colourless);
    else desktopGlyph(glyph::game, (thumb.left + thumb.right) * 0.5f, (thumb.top + thumb.bottom) * 0.5f, 14, gray(0.5f, alpha));
    if (marked || hover > 0.5f) { // Marked for a group action: its check over the cover.
        const auto check = Layout::checkBox(thumb);
        const float cx = (check.left + check.right) * 0.5f - 4, cy = (check.top + check.bottom) * 0.5f - 4;
        brush(marked ? uiAccent(alpha) : gray(0, 0.55f * alpha));
        target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), 8, 8), solid_.Get());
        if (marked) desktopGlyph(glyph::check, cx, cy, 9, gray(0.98f, alpha));
        else { brush(gray(1, 0.8f * alpha)); target_->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), 7, 7), solid_.Get(), 1.2f); }
    }
    // The columns from the right: play time, last played, platform.
    const float cy = (row.top + row.bottom) * 0.5f, width = row.right - row.left;
    float right = row.right - 16;
    const auto column = [&](const std::wstring& value, float w) {
        textLine(value, right - w, cy, w, desktop_font_.Get(), gray(0.72f, alpha), DWRITE_TEXT_ALIGNMENT_TRAILING);
        right -= w + 20;
    };
    if (view.list_playtime && index < view.list_playtime->size()) column((*view.list_playtime)[index], 90);
    if (width > 520 && view.list_played && index < view.list_played->size()) column((*view.list_played)[index], 120);
    if (width > 680) {
        std::wstring platform = activePlatformName(game);
        if (platform.empty() && !game.version_group.empty()) platform = tr(L"Kilka wersji");
        auto mark = game.platform.empty() ? std::string_view{} : brand_icons::consoleKey(game.platform);
        if (mark.empty()) mark = brand_icons::sourceKey(game.source);
        const float w = 150;
        const float left = right - w;
        float text_left = left;
        if (!mark.empty() && desktopBrand(mark, left + 8, cy, 14, gray(0.7f, alpha))) text_left = left + 22;
        textLine(platform, text_left, cy, right - text_left, desktop_font_.Get(), gray(0.72f, alpha));
        right = left - 20;
    }
    // The name and its state.
    const float name_left = thumb.right + 14, name_width = std::max(1.0f, right - name_left);
    textLine(game.name, name_left, cy - 9, name_width, selected ? desktop_strong_font_.Get() : desktop_font_.Get(), palette::text(alpha));
    std::wstring state = session == SessionState::running ? std::wstring(tr(L"W grze")) : frozen ? std::wstring(tr(L"Wstrzymana")) :
                         installing ? installationText(game.installation) :
                         game.installed || game.kind == GameKind::catalog ? std::wstring(tr(L"Zainstalowana")) :
                         localFileGame(game.source) ? missingFileLabel(game) : std::wstring(tr(L"Do instalacji"));
    const auto state_ink = session == SessionState::running ? rgb(0.36f, 0.86f, 0.52f, alpha) : installing ? uiAccent(alpha) : palette::muted(alpha);
    textLine(state, name_left, cy + 10, name_width, desktop_small_font_.Get(), state_ink);
    if (installing) {
        const auto percent = game.installation.percent();
        const float bar_left = name_left + std::min(name_width, textAdvance(state, desktop_small_font_.Get()) + 12), bar_right = std::min(right, bar_left + 140);
        if (percent && bar_right > bar_left + 20) {
            desktopFill({bar_left, cy + 9, bar_right, cy + 12}, 1.5f, gray(1, 0.15f * alpha));
            desktopFill({bar_left, cy + 9, bar_left + (bar_right - bar_left) * std::clamp(*percent / 100.0f, 0.0f, 1.0f), cy + 12}, 1.5f, uiAccent(alpha));
        }
    }
}

void Renderer::drawDesktopPoster(const Layout& layout, const View& view, size_t index) {
    const auto& card = (*cards_)[index];
    const auto& game = card.game;
    const auto cell = layout.poster(index, view.scroll);
    if (cell.bottom + caption_h < layout.gridTop() || cell.top > layout.height || card.opacity < 0.01f) return;
    const float hover = index < view.card_hover.size() ? view.card_hover[index] : 0.0f;
    const bool selected = index == selected_;
    const bool marked = index < view.marked.size() && view.marked[index];
    const float alpha = card.opacity;
    const float lift = 4 * smoothFade(hover);
    const auto rect = D2D1::RectF(cell.left, cell.top - lift, cell.right, cell.bottom - lift);
    const Rect poster{rect.left, rect.top, rect.right, rect.bottom};
    const float previous_corner = squircle_corner_, previous_reference = corner_reference_;
    squircle_corner_ = 0.4f; corner_reference_ = 64;
    const auto ring_shape = selected ? tileShape(box(inflate(poster, 3.5f + (focus_gap_ != AppSettings::FocusGap::none ? 2.5f : 0.0f))), tile_shape_) : ComPtr<ID2D1Geometry>{};
    // The poster's own shape, shadow and outline: tessellated once for this size, drawn at its place.
    const auto* shaped = posterShape(rect.right - rect.left, rect.bottom - rect.top);
    squircle_corner_ = previous_corner; corner_reference_ = previous_reference;
    if (!shaped) return;
    const float shadow = alpha * (0.35f + 0.65f * std::max(hover, selected ? 0.5f : 0.0f));
    D2D1_MATRIX_3X2_F base{};
    target_->GetTransform(&base);
    target_->SetTransform(D2D1::Matrix3x2F::Translation(rect.left, rect.top) * base);
    const auto local = D2D1::RectF(0, 0, rect.right - rect.left, rect.bottom - rect.top);
    auto* shape = shaped->geometry.Get();
    const auto fill = [&](D2D1_COLOR_F ink) {
        brush(ink);
        if (shaped->fill) realizer_->DrawGeometryRealization(shaped->fill.Get(), solid_.Get());
        else target_->FillGeometry(shape, solid_.Get());
    };
    if (shadow > 0.003f) {
        if (shaped->shadow) target_->DrawBitmap(shaped->shadow.Get(), shaped->shadow_box, std::min(1.0f, shadow), D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
        else desktopShadow({0, 0, local.right, local.bottom}, 12, shadow);
    }
    constexpr float tones[4][3] = {{0.13f, 0.21f, 0.24f}, {0.20f, 0.20f, 0.30f}, {0.28f, 0.20f, 0.17f}, {0.15f, 0.24f, 0.20f}};
    const int tone = static_cast<int>(game.app_id % 4);
    fill(rgb(tones[tone][0] * 0.7f, tones[tone][1] * 0.7f, tones[tone][2] * 0.7f, alpha));
    const auto session = sessionOf(game);
    const float status_fade = game.installation.active() ? 1.0f : statusFade(game);
    const bool frozen = session == SessionState::frozen;
    // Black and white: a suspended game, and (when chosen) one that is not on the disk.
    const bool colourless = frozen || (view.gray_uninstalled && !game.installed && !game.installation.active() && game.kind != GameKind::catalog);
    Cover* art = index < posters_.size() && posters_[index].bitmap ? &posters_[index] : nullptr;
    Cover* cover = index < covers_.size() && covers_[index].bitmap ? &covers_[index] : nullptr;
    auto* realized = shaped->fill.Get();
    if (art || cover) {
        const float shown = art ? reveal(*art) : 0.0f;
        if (cover && shown < 1) {
            // A square cover in a poster's frame: whole, over a dimmed fill of itself.
            const float opacity = alpha * card.cover_opacity * reveal(*cover);
            fillImage(*cover, local, 10, opacity, colourless, shape, false, realized);
            fill(rgb(0.008f, 0.011f, 0.015f, 0.62f * opacity));
            fillImage(*cover, local, 10, opacity, colourless, shape, true, realized);
        }
        if (art) fillImage(*art, local, 10, alpha * shown, colourless, shape, false, realized);
    } else if (!(index < posters_.size() && posters_[index].decoding) && !(index < covers_.size() && covers_[index].decoding)) {
        desktopCenteredText(game.name, local.right * 0.5f, local.bottom * 0.38f, local.right - 24, desktop_title_font_.Get(), gray(0.92f, alpha), 4);
    }
    const bool installing = game.installation.active();
    if (!game.installed && !installing && game.kind != GameKind::catalog) fill(gray(0, 0.42f * (1 - 0.6f * hover) * alpha));
    if (frozen) fill(rgb(0.015f, 0.018f, 0.022f, 0.34f * alpha));
    if (installing || session != SessionState::none) drawCardStatusShade(0, 0, local.right, local.bottom, installing ? alpha : alpha * status_fade, shape);
    if (selected && view.launch > 0.01f) fill(gray(1, 0.18f * std::sin(smoothFade(view.launch) * std::numbers::pi_v<float>) * alpha));
    // Marked for a group action: a tint over the poster and its check (top left), not the
    // selection's ring, so marked, selected and keyboard focus stay three different things.
    if (marked && !selected) { const auto tint = uiAccent(); fill(rgb(tint.r, tint.g, tint.b, 0.16f * alpha)); }
    if (perfectGame(game)) { // Every achievement unlocked: a gold edge.
        brush(rgb(perfect_gold.r, perfect_gold.g, perfect_gold.b, 0.95f * alpha));
        target_->DrawGeometry(shape, solid_.Get(), 2.2f);
    } else {
        brush(gray(1, (0.07f + 0.16f * hover) * alpha));
        if (shaped->outline) realizer_->DrawGeometryRealization(shaped->outline.Get(), solid_.Get());
        else target_->DrawGeometry(shape, solid_.Get(), 1);
    }
    target_->SetTransform(base);
    if (ring_shape && selected) {
        // The selection: a thin ring while the keyboard works elsewhere (sidebar, pane, search), a
        // thick one when the grid has the keyboard's focus.
        const bool keyboard_here = view.keyboard && view.zone == 1;
        const bool keyboard_away = view.keyboard && view.zone != 1;
        brush(uiAccent((keyboard_here ? 1.0f : keyboard_away ? 0.55f : 0.92f) * alpha));
        target_->DrawGeometry(ring_shape.Get(), solid_.Get(), keyboard_here ? 3.0f : keyboard_away ? 1.5f : 2.0f);
    }
    // Badges: in play, suspended, downloading, to install.
    if (session != SessionState::none) {
        const auto label = frozen ? std::wstring(tr(L"Wstrzymana")) : std::wstring(tr(L"W grze"));
        const float w = textAdvance(label, desktop_label_font_.Get()) + 28;
        const Rect pill{rect.left + 8, rect.top + 8, rect.left + 8 + w, rect.top + 30};
        const float shown = alpha * status_fade;
        desktopFill(pill, 11, gray(0, 0.72f * shown));
        brush(frozen ? gray(0.8f, shown) : rgb(0.36f, 0.86f, 0.52f, shown));
        target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(pill.left + 11, (pill.top + pill.bottom) * 0.5f), 3.5f, 3.5f), solid_.Get());
        textLine(label, pill.left + 20, (pill.top + pill.bottom) * 0.5f, w - 22, desktop_label_font_.Get(), gray(0.96f, shown));
    }
    if (installing) {
        const auto percent = game.installation.percent();
        const float left = rect.left + 10, right = rect.right - 10, y = rect.bottom - 12;
        desktopFill({left, y - 2, right, y + 2}, 2, gray(1, 0.2f * alpha));
        if (percent) desktopFill({left, y - 2, left + (right - left) * std::clamp(*percent / 100.0f, 0.0f, 1.0f), y + 2}, 2, uiAccent(alpha));
        else {
            const float span = (right - left) * 0.3f, phase = std::fmod(elapsed_ * 0.6f, 1.0f);
            const float x0 = left + (right - left + span) * phase - span;
            desktopFill({std::max(left, x0), y - 2, std::min(right, x0 + span), y + 2}, 2, uiAccent(alpha));
        }
        textLine(installationText(game.installation), left, y - 16, right - left, desktop_label_font_.Get(), gray(0.96f, alpha));
    } else if (!view.platform_badges && !game.installed && game.kind != GameKind::catalog && hover < 0.5f) {
        const float cx = rect.right - 20, cy = rect.top + 20;
        brush(gray(0, 0.6f * alpha));
        target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), 12, 12), solid_.Get());
        desktopGlyph(fileNotChosen(game) ? glyph::folder_open : localFileGame(game.source) ? glyph::remove : glyph::download, cx, cy, 11, gray(0.92f, alpha));
    }
    // The store's or console's mark, top right (Ustawienia → Okno desktopowe); the check box is top left.
    if (view.platform_badges && isGame(game)) {
        auto mark = game.platform.empty() ? std::string_view{} : brand_icons::consoleKey(game.platform);
        if (mark.empty()) mark = brand_icons::sourceKey(game.source);
        if (!mark.empty() && rect.right - rect.left > 80) {
            const Rect chip{rect.right - 33, rect.top + 9, rect.right - 9, rect.top + 33};
            desktopFill(chip, 7, gray(0, 0.62f * alpha));
            desktopBrand(mark, (chip.left + chip.right) * 0.5f, (chip.top + chip.bottom) * 0.5f, 14, gray(0.96f, alpha));
        }
    }
    // On hover: the check box for marking several (playing is the pane's button or a double click).
    const float check_alpha = marked ? 1.0f : std::max(hover, view.marked_count ? 0.55f : 0.0f);
    if (check_alpha > 0.01f && isGame(game)) {
        const auto check = Layout::checkBox(poster);
        const float cx = (check.left + check.right) * 0.5f, cy = (check.top + check.bottom) * 0.5f;
        const bool over = view.hover == Hit{Target::card_check, static_cast<int>(index)};
        const float mark_pulse = index < view.card_mark.size() ? view.card_mark[index] : 0.0f;
        const float pop = std::sin(mark_pulse * std::numbers::pi_v<float>);
        if (marked) {
            const float scale = 1.0f + 0.25f * pop;
            brush(uiAccent(alpha));
            target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), 11 * scale, 11 * scale), solid_.Get());
            const auto accent = uiAccent();
            const float luminance = 0.2126f * accent.r + 0.7152f * accent.g + 0.0722f * accent.b;
            desktopGlyph(glyph::check, cx, cy, 11 * scale, luminance > 0.6f ? gray(0.06f, alpha) : gray(0.98f, alpha));
        } else {
            const float scale = (0.76f + 0.24f * smoothFade(check_alpha)) * (1.0f - 0.16f * pop);
            brush(gray(0, (over ? 0.62f : 0.45f) * check_alpha * alpha));
            target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), 11 * scale, 11 * scale), solid_.Get());
            brush(gray(1, (over ? 1.0f : 0.8f) * check_alpha * alpha));
            target_->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), 10 * scale, 10 * scale), solid_.Get(), 1.5f);
        }
    }
    // Caption: the name, then where it comes from and what state it is in.
    const float name_y = cell.bottom + 15, meta_y = cell.bottom + 33;
    textLine(game.name, cell.left + 2, name_y, cell.right - cell.left - 4, desktop_strong_font_.Get(), palette::text((selected ? 1.0f : 0.88f) * alpha));
    std::wstring meta = activePlatformName(game);
    if (meta.empty() && !game.version_group.empty()) meta = tr(L"Kilka wersji");
    if (!game.installed && !installing && game.kind != GameKind::catalog)
        meta += (meta.empty() ? L"" : L" · ") + std::wstring(localFileGame(game.source) ? missingFileLabel(game) : tr(L"Do instalacji"));
    textLine(meta, cell.left + 2, meta_y, cell.right - cell.left - 4, desktop_small_font_.Get(), palette::muted(alpha));
}
