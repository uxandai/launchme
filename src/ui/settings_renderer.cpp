#include "renderer.hpp"
#include "renderer_internal.hpp"
#include "brand_icons.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace {
using palette::gray;
using render_detail::box;
D2D1_COLOR_F rgb(uint32_t color) {
    return D2D1::ColorF(((color >> 16) & 255) / 255.0f, ((color >> 8) & 255) / 255.0f, (color & 255) / 255.0f);
}
float middle(settings_ui::Rect r) { return (r.top + r.bottom) * 0.5f; }
}

// Type scale: heading (settings_heading_font_), row title (category_font_), value
// (menu_font_), description (settings_body_font_), control caption (button_font_).
// Every row is a title/description group centred on the row's centre line; the
// controls on the right share that line. No rules or dividers: fills and focus
// outlines carry the structure.
void Renderer::drawSettings() {
    using namespace settings_ui;
    target_->Clear(D2D1::ColorF(0.022f, 0.024f, 0.030f));
    // The last game's background, frosted and dimmed behind the whole screen (Efekt szkła).
    drawBackground();
    drawFrostedBackdrop(1);
    if (!context_->settings_rows) return;
    const auto& rows = *context_->settings_rows;
    const auto& state = context_->settings_state;
    const auto layout = Layout::of(viewport_.width, viewport_.height, state, rows);
    const float left = layout.contentLeft(), right = layout.contentRight();
    const float list_top = layout.listTop(), list_bottom = layout.listBottom();
    const auto accent = uiAccent();
    const auto fill = [&](Rect r, D2D1_COLOR_F ink, float radius = 12) {
        brush(ink);
        target_->FillRoundedRectangle(D2D1::RoundedRect(box(r), radius, radius), solid_.Get());
    };
    const auto outline = [&](Rect r, D2D1_COLOR_F ink, float thickness = 1.0f, float radius = 12) {
        brush(ink);
        target_->DrawRoundedRectangle(D2D1::RoundedRect(box(r), radius, radius), solid_.Get(), thickness);
    };
    // button_font_ is centre-aligned: a caption centred in its rectangle.
    const auto centered = [&](const std::wstring& value, Rect r, D2D1_COLOR_F ink) {
        textLine(value, r.left, middle(r), r.right - r.left, settings_body_font_.Get(), ink, DWRITE_TEXT_ALIGNMENT_CENTER);
    };
    // PSfive's pills: glass over the frosted backdrop, the focused one white. Returns the ink.
    const auto button = [&](Rect r, bool focused, bool hovered, bool enabled = true) {
        return drawButton(box(r), focused && enabled ? 1.0f : 0.0f, 1, false, hovered, enabled);
    };
    const auto faint = [](D2D1_COLOR_F ink, float alpha) { ink.a *= alpha; return ink; };
    const auto chevron = [&](float x, float y, int direction, D2D1_COLOR_F ink) {
        line(x - 4 * direction, y - 6, x + 2 * direction, y, ink, 1.8f);
        line(x + 2 * direction, y, x - 4 * direction, y + 6, ink, 1.8f);
    };
    const auto shapePreview = [&](float x, float y, int choice) {
        const auto shape = tileShape(D2D1::RectF(x, y - 17, x + 34, y + 17), static_cast<AppSettings::TileShape>(choice));
        if (!shape) return;
        brush(uiAccent(0.22f)); target_->FillGeometry(shape.Get(), solid_.Get());
        brush(accent); target_->DrawGeometry(shape.Get(), solid_.Get(), 1.6f);
    };
    // locked: the state cannot be changed here (drawn fainter).
    const auto toggle = [&](float toggle_right, float y, float on, bool locked = false) {
        on = std::clamp(on, 0.0f, 1.0f);
        const auto off_bg = gray(locked ? 0.15f : 0.23f);
        fill({toggle_right - 52, y - 14, toggle_right, y + 14},
             D2D1::ColorF(std::lerp(off_bg.r, accent.r, on), std::lerp(off_bg.g, accent.g, on), std::lerp(off_bg.b, accent.b, on), 1.0f), 14);
        brush(gray(std::lerp(locked ? 0.36f : 0.67f, 0.08f, on)));
        target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(toggle_right - std::lerp(38.0f, 14.0f, on), y), 10, 10), solid_.Get());
    };
    // Glyphs drawn as strokes so they sit exactly on the centre of their button.
    const auto sign = [&](Rect r, bool plus, D2D1_COLOR_F ink) {
        const float x = (r.left + r.right) * 0.5f, y = middle(r);
        line(x - 6, y, x + 6, y, ink, 1.8f);
        if (plus) line(x, y - 6, x, y + 6, ink, 1.8f);
    };
    // The platforms' and plugins' cog (Opcje) centred in its button.
    const auto drawCog = [&](Rect cog, D2D1_COLOR_F ink) {
        const float cx = (cog.left + cog.right) * 0.5f, cy = middle(cog);
        brush(ink);
        target_->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), 10, 10), solid_.Get(), 2);
        target_->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), 3.5f, 3.5f), solid_.Get(), 1.7f);
        for (int spoke = 0; spoke < 8; ++spoke) {
            const float a = spoke * std::numbers::pi_v<float> / 4;
            line(cx + std::cos(a) * 12, cy + std::sin(a) * 12, cx + std::cos(a) * 16, cy + std::sin(a) * 16, ink, 2.4f);
        }
    };
    const auto arrow = [&](Rect r, bool down, D2D1_COLOR_F ink) {
        const float x = (r.left + r.right) * 0.5f, y = middle(r), d = down ? 1.0f : -1.0f;
        line(x, y - 7 * d, x, y + 7 * d, ink, 1.8f);
        line(x - 5, y + 2 * d, x, y + 7 * d, ink, 1.8f);
        line(x + 5, y + 2 * d, x, y + 7 * d, ink, 1.8f);
    };

    const auto heading = settings_heading_font_.Get(), body = settings_body_font_.Get();
    // "Włączone"/"Wyłączone" right-aligned to `label_right`, the same width and ink for every switch.
    const auto stateLabel = [&](bool on, bool locked, float label_right, float y) {
        textLine(on ? tr(L"Włączone") : tr(L"Wyłączone"), label_right - 148, y, 148, body, palette::stateLabel(locked),
                 DWRITE_TEXT_ALIGNMENT_TRAILING);
    };
    // The heading beside Back (a dot at the top, as on the other screens).
    textGroup(tr(L"Ustawienia"), {}, 108, Layout::heading_cap_top, layout.railRight() - 114, heading, gray(0.96f), body, gray(0.55f), 1, true);
    // Rail pill: solid white while the rail has focus, a faint fill while the rows do. It glides
    // between categories and down to Back (context_->settings_motion, src/app/focus_motion.cpp);
    // labels under it turn dark as it passes.
    const auto& motion = context_->settings_motion;
    const auto back = layout.backButton();
    const auto pill_target = state.backFocused() ? back : layout.categoryRail(state.category);
    const float solid = motion ? motion->rail_focus : state.options ? 0.0f : 1.0f;
    const Rect pill{pill_target.left, motion ? motion->pill_top : pill_target.top, pill_target.right, motion ? motion->pill_bottom : pill_target.bottom};
    drawButtonGlass(box(back), 1, false); // Back: a glass pill, the rail's white pill glides onto it.
    fill(pill, gray(std::lerp(1.0f, 0.91f, solid), std::lerp(0.07f, 1.0f, solid)), (pill.bottom - pill.top) * 0.5f);
    for (int i = 0; i < static_cast<int>(categories.size()); ++i) {
        const auto r = layout.categoryRail(i);
        const float y = middle(r);
        if (state.category == i && solid < 0.999f)
            fill({r.left + 10, y - 10, r.left + 13, y + 10}, D2D1::ColorF(accent.r, accent.g, accent.b, 1.0f - solid), 1.5f);
    }
    // Labels light everywhere, then dark again clipped to the solid pill: the colour flips exactly
    // under the pill's edge while it slides.
    const auto labels = [&](bool on_pill) {
        for (int i = 0; i < static_cast<int>(categories.size()); ++i) {
            const auto r = layout.categoryRail(i);
            const float y = middle(r);
            const bool active = state.category == i;
            const float base = active ? 0.96f : 0.57f;
            const auto ink = gray(on_pill ? std::lerp(base, 0.08f, solid) : base);
            drawSettingsIcon(i, r.left + 34, y, ink);
            textLine(tr(categories[i].title), r.left + 62, y, r.right - r.left - 80, category_font_.Get(), ink);
        }
        brush(gray(on_pill ? std::lerp(0.96f, 0.08f, solid) : 0.96f, 0.92f));
        target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F((back.left + back.right) * 0.5f, middle(back)), 4, 4), solid_.Get());
    };
    labels(false);
    if (solid > 0.004f) {
        target_->PushAxisAlignedClip(box(pill), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        labels(true);
        target_->PopAxisAlignedClip();
    }

    // Heading with its description directly beneath (one line); Layout::listTop() leaves the same gap below either.
    // A page names its category there, the way back.
    textGroup(context_->settings_title.empty() ? tr(categories[state.category].title) : context_->settings_title,
              context_->settings_title.empty() ? std::wstring(tr(categories[state.category].description)) :
                                                 L"‹  " + (context_->settings_parent.empty() ? std::wstring(tr(categories[state.category].title)) : context_->settings_parent),
              left, Layout::heading_cap_top,
              right - left, heading, gray(0.96f), body, gray(0.55f), 1, true, heading_group_gap);
    target_->PushAxisAlignedClip(D2D1::RectF(left - 3, list_top - 2, right + 3, list_bottom), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    // The list scrolls and the row highlight moves smoothly (settings_motion); hit tests use the targets.
    const bool row_motion = motion && motion->rows;
    const float scroll = row_motion ? motion->scroll : state.offset();
    if (state.pluginList())
        textLine(tr(L"Dostępne pluginy"), left + 4, list_top + Layout::row_step + 46 - scroll,
                 right - left, friend_section_font_.Get(), gray(0.58f));
    for (const int i : layout.sections) {
        const float y = layout.row(i, scroll).top - Layout::section_label;
        if (y > list_top - 20 && y < list_bottom + 20)
            textLine(rows[i].section, left + 4, y, right - left, friend_section_font_.Get(), gray(0.58f));
    }
    for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
        auto r = layout.row(i, scroll);
        if (tiled(rows[i].control)) continue; // Its tiles are the cards.
        if (r.bottom < list_top || r.top >= list_bottom) continue;
        // An open accordion section's rows sit inset under its heading, joined by a thin line.
        if (rows[i].indent > 0) {
            fill({r.left + 14, r.top - (layout.rowStep() - (layout.compact ? Layout::compact_height : Layout::row_height)), r.left + 17, r.bottom},
                 gray(1, 0.08f), 1.5f);
            r.left += 36.0f * static_cast<float>(rows[i].indent);
        }
        fill(r, gray(1, rows[i].control == Control::fold ? 0.07f : 0.034f), 16);
    }
    if (!rows.empty() && 1.0f - solid > 0.004f && !tiled(rows[state.row()].control)) {
        const float row_top = list_top + (row_motion ? motion->row : layout.rowOffset(state.row())) - scroll;
        const Rect highlight{left, row_top, right, row_top + (row_motion ? motion->height : layout.rowHeight(state.row()))};
        const float shown = 1.0f - solid;
        // A lighter card, no outline; the accent frame only while the row's value is being edited.
        fill(highlight, gray(1, 0.10f * shown), 16);
        if (state.editing && rows[state.row()].control != Control::collection) outline(highlight, accent, 1.5f, 16);
    }
    for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
        const auto& item = rows[i];
        const auto r = layout.row(i, scroll);
        if (r.bottom < list_top || r.top >= list_bottom) continue;
        const float y = Layout::middle(r);
        const bool selected = state.options && state.row() == i;
        const bool hovered = context_->settings_hover_row == i;
        const bool control_focus = selected && (state.editing || item.control == Control::action);
        float text_left = r.left + 24 + 36.0f * static_cast<float>(item.indent);
        // Platforms: the store's mark in a fixed-width column, so every name starts at one line.
        if (item.control == Control::platform && item.id == Id::pc_platform) {
            desktopBrand(brand_icons::sourceKey(item.source), text_left + 18, y, 34, gray(item.locked ? 0.5f : 0.9f));
            text_left += 60;
        } else if (optionsTile(item)) {
            desktopBrand("retroarch", text_left + 18, y, 34, gray(0.9f)); // Emulacja among the platforms.
            text_left += 60;
        }
        const int size_steps = sizeSteps(item.id);
        const bool stepped_size = size_steps > 1;
        if (item.control == Control::avatar) {
            drawCompanionPreviews(layout, item, i, scroll, selected);
            continue;
        }
        if (item.control == Control::layout) {
            textGroup(item.title, item.description, text_left, r.top + 22, r.right - text_left - 24,
                      category_font_.Get(), gray(0.93f), body, gray(0.52f), 1, true);
            drawSettingsLayouts(layout, i, scroll, selected);
            continue;
        }
        if (tiled(item.control)) {
            drawSettingsTiles(layout, item, i, scroll, selected && 1.0f - solid > 0.004f);
            continue;
        }
        const float controls_left = hasOptionsButton(item.id) ? r.right - 324 : layout.controlsLeft(item.control);
        // What the row has room for: thin rows (DRM-free games) one line of description; a row with
        // tags (plugins) the lines fitSettingsDescriptions made room for, the tags under them; a dense
        // row without the focus its detail line; the rest ends with an ellipsis.
        const bool dense = item.dense && !selected;
        const int description_lines = layout.compact || dense ? 1 : !item.tags.empty() ? (item.lines > 2 ? item.lines - 1 : 1) : std::max(2, item.lines);
        const float badge_room = item.badge.empty() ? 0.0f : Layout::badge_space;
        textGroup(item.title, dense ? item.detail : item.description, text_left, y - (item.tags.empty() ? 0 : 12),
                  controls_left - (stepped_size ? 72 : 24) - badge_room - text_left, category_font_.Get(), gray(item.locked ? 0.56f : 0.93f), body,
                  gray(dense ? 0.46f : 0.52f), description_lines);
        // The badge: the game's own value; a button back to the default (the pad's X, Delete).
        if (!item.badge.empty()) {
            const auto b = layout.badge(i, scroll, item.control);
            const bool resettable = !item.reset.empty();
            const bool badge_hover = resettable && context_->settings_hover_badge == i;
            fill(b, uiAccent(badge_hover ? 0.32f : 0.16f), (b.bottom - b.top) * 0.5f);
            if (badge_hover) outline(b, uiAccent(0.8f), 1.2f, (b.bottom - b.top) * 0.5f);
            textLine((resettable ? L"\u21BA  " : L"") + item.badge, b.left + 14, y, b.right - b.left - 28, toast_sub_font_.Get(), uiAccent(0.98f),
                     DWRITE_TEXT_ALIGNMENT_CENTER);
        }
        // Each further line grows the centred group by half a line downwards: the tags follow it.
        const float tag_top = y + 24 + (item.description.empty() ? 0.0f : (description_lines - 1) * typeMetrics(body).line * 0.5f);
        float tag_left = text_left;
        for (const auto& tag : item.tags) {
            DWRITE_TEXT_METRICS metrics{};
            const auto measured = text_layouts_.get(dwrite_.Get(), tag, toast_sub_font_.Get(), 1024, 32);
            const float ink = measured && SUCCEEDED(measured->GetMetrics(&metrics)) ? metrics.widthIncludingTrailingWhitespace : tag.size() * 10.0f;
            const float width = 22.0f + std::ceil(ink);
            if (tag_left + width > layout.controlsLeft(item.control) - 24) break;
            fill({tag_left, tag_top, tag_left + width, tag_top + 20}, gray(1, 0.055f), 10);
            textLine(tag, tag_left + 10, tag_top + 10, width - 20, toast_sub_font_.Get(), gray(0.62f));
            tag_left += width + 7;
        }
        if (item.control == Control::choice || item.control == Control::secret || item.control == Control::text || item.control == Control::action) {
            if (item.control == Control::action) {
                if (item.locked && item.value.empty()) continue; // Information only (a status line): no button.
                const auto action = layout.actionButton(i, scroll);
                const auto ink = button(action, control_focus, hovered, !item.locked);
                centered(item.value.empty() ? tr(L"Otwórz") : item.value, {action.left + 16, action.top, action.right - 16, action.bottom}, ink);
                continue;
            }
            const auto field = layout.field(i, scroll);
            const auto ink = button(field, control_focus, hovered);
            const bool shape = item.id == Id::tile_shape;
            if (shape) shapePreview(field.left + 18, y, item.choice);
            const float value_left = field.left + (shape ? 66 : 22);
            textLine(item.value, value_left, y, field.right - value_left - 44, body, ink);
            chevron(field.right - 24, y, 1, faint(ink, 0.75f));
        } else if (item.control == Control::count) {
            // A total that opens its list: the number large and bold, a chevron after it.
            const float chevron_x = r.right - 34;
            textLine(item.value, r.right - 220, y, 160, settings_heading_font_.Get(), gray(0.96f), DWRITE_TEXT_ALIGNMENT_TRAILING);
            chevron(chevron_x, y, 1, selected ? accent : gray(0.68f));
        } else if (item.control == Control::page && optionsTile(item)) {
            // Emulacja among the platforms: its summary, then the platforms' cog.
            const auto cog = layout.pluginCog(i, scroll);
            textLine(item.value, controls_left - 40, y, cog.left - 16 - (controls_left - 40), body, gray(0.78f), DWRITE_TEXT_ALIGNMENT_TRAILING);
            drawCog(cog, faint(button(cog, selected, hovered), 0.85f));
        } else if (item.control == Control::page) {
            // A page of its own: its summary, then a chevron.
            textLine(item.value, controls_left, y, r.right - 58 - controls_left, body, gray(0.78f), DWRITE_TEXT_ALIGNMENT_TRAILING);
            chevron(r.right - 34, y, 1, selected ? accent : gray(0.68f));
        } else if (item.control == Control::fold) {
            // An accordion heading: its summary, then a chevron pointing down while it is open.
            textLine(item.value, controls_left, y, r.right - 58 - controls_left, body, item.value.empty() ? gray(0.6f) : accent, DWRITE_TEXT_ALIGNMENT_TRAILING);
            const auto ink = selected ? accent : gray(0.68f);
            if (item.checked) { line(r.right - 40, y - 3, r.right - 34, y + 3, ink, 1.8f); line(r.right - 34, y + 3, r.right - 28, y - 3, ink, 1.8f); }
            else chevron(r.right - 34, y, 1, ink);
        } else if (item.control == Control::slider) {
            auto readout = layout.sliderValue(i, scroll);
            if (stepped_size) readout.left -= 48;
            textLine(item.value, readout.left, y, readout.right - readout.left, body, gray(0.86f), DWRITE_TEXT_ALIGNMENT_TRAILING);
            const auto minus = layout.minus(i, scroll), plus = layout.plus(i, scroll);
            sign(minus, false, button(minus, control_focus, hovered, item.level > 0));
            sign(plus, true, button(plus, control_focus, hovered, item.level < 1));
            const auto track = layout.track(i, scroll);
            const float end = std::lerp(track.left, track.right, std::clamp(item.level, 0.0f, 1.0f));
            fill({track.left, y - 2, track.right, y + 2}, gray(1, 0.15f), 2);
            if (end > track.left) fill({track.left, y - 2, end, y + 2}, accent, 2);
            if (stepped_size) for (int step = 0; step < size_steps; ++step) {
                const float tick = std::lerp(track.left, track.right, step / static_cast<float>(size_steps - 1));
                line(tick, y - 5, tick, y + 5, gray(0.7f), 2);
            }
            brush(gray(0.95f));
            target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(end, y), selected ? 7.0f : 5.0f, selected ? 7.0f : 5.0f), solid_.Get());
        } else if (item.control == Control::platform) {
            const float toggle_on = i < static_cast<int>(context_->settings_toggles.size()) ?
                                    context_->settings_toggles[static_cast<size_t>(i)] : (item.checked ? 1.0f : 0.0f);
            if (hasOptionsButton(item.id)) {
                stateLabel(item.checked, false, r.right - 184, y);
                toggle(r.right - 114, y, toggle_on);
                const auto cog = layout.pluginCog(i, scroll);
                drawCog(cog, faint(button(cog, selected && state.editing, hovered), 0.85f));
                continue;
            }
            stateLabel(item.checked, false, r.right - 292, y);
            toggle(r.right - 228, y, toggle_on);
            const auto connect = layout.connect(i, scroll);
            centered(item.value, connect, button(connect, selected && state.editing, hovered));
        } else {
            const float toggle_on = i < static_cast<int>(context_->settings_toggles.size()) ?
                                    context_->settings_toggles[static_cast<size_t>(i)] : (item.checked ? 1.0f : 0.0f);
            const bool collection = item.control == Control::collection;
            const float toggle_right = r.right - (collection ? 192.0f : 28.0f);
            // State label right-aligned next to its switch.
            stateLabel(item.checked, item.locked, toggle_right - 66, y);
            toggle(toggle_right, y, toggle_on, item.locked);
            if (collection) {
                const auto up = layout.reorder(i, scroll, false), down = layout.reorder(i, scroll, true);
                const bool earlier = i > 0 && rows[i - 1].control == Control::collection;
                const bool later = i + 1 < static_cast<int>(rows.size()) && rows[i + 1].control == Control::collection;
                arrow(up, false, button(up, selected && state.subcontrol == 1, hovered, earlier));
                arrow(down, true, button(down, selected && state.subcontrol == 2, hovered, later));
                if (item.removable) {
                    // The trash of an own collection: red with the focus or the pointer on it; its question follows.
                    const auto bin = layout.trash(i, scroll);
                    const bool hot = (selected && state.subcontrol == 3) || context_->settings_hover_trash == i;
                    const float radius = (bin.bottom - bin.top) * 0.5f;
                    if (hot) { fill(bin, faint(palette::danger(), 0.18f), radius); outline(bin, palette::danger(), 1.5f, radius); }
                    else button(bin, false, hovered);
                    const auto ink = hot ? palette::danger() : gray(0.78f);
                    const float cx = (bin.left + bin.right) * 0.5f;
                    line(cx - 8, y - 6, cx + 8, y - 6, ink, 1.8f);          // The lid,
                    line(cx - 3, y - 9, cx + 3, y - 9, ink, 1.8f);          // its handle,
                    line(cx - 6, y - 4, cx - 5, y + 9, ink, 1.8f);          // the can's sides
                    line(cx + 6, y - 4, cx + 5, y + 9, ink, 1.8f);
                    line(cx - 5, y + 9, cx + 5, y + 9, ink, 1.8f);          // and bottom,
                    line(cx - 2, y - 1, cx - 2, y + 6, ink, 1.4f);          // its ribs.
                    line(cx + 2, y - 1, cx + 2, y + 6, ink, 1.4f);
                }
                if (selected && state.subcontrol == 0) {
                    const auto control = layout.collectionControl(i, scroll, 0);
                    outline(control, accent, 1.5f, (control.bottom - control.top) * 0.5f);
                }
            }
        }
    }
    if (rows.empty()) {
        const Rect card{left, list_top, right, list_top + Layout::row_height};
        fill(card, gray(1, 0.034f));
        textGroup(tr(L"Brak nazwanych kolekcji"), tr(L"Utwórz kolekcje w Steam. Pojawią się tutaj po odświeżeniu biblioteki."),
                  left + 24, middle(card), right - left - 48, category_font_.Get(), gray(0.82f), body, gray(0.53f));
    }
    target_->PopAxisAlignedClip();
    const float maximum = layout.maxScroll(static_cast<int>(rows.size()));
    if (maximum > 0) {
        const float height = list_bottom - list_top;
        const float thumb = std::max(30.0f, height * height / (maximum + height));
        const float y = list_top + (height - thumb) * std::clamp(scroll / maximum, 0.0f, 1.0f);
        fill({right + 17, list_top, right + 20, list_bottom}, gray(1, 0.06f), 1.5f);
        fill({right + 17, y, right + 20, y + thumb}, gray(1, 0.35f), 1.5f);
    }
    if (context_->settings_save_failed) {
        const auto status = layout.saveStatus();
        centered(tr(L"Błąd zapisu · F5, aby ponowić"), status, palette::danger());
    }
    // System signs off with the logo at the very bottom, level with Back.
    if (state.category == static_cast<int>(Category::system) && !state.subpage())
        drawLaunchmeLogo(left + 4, middle(layout.backBar()), Layout::footer_logo, gray(0.96f, 0.5f));

    if (const auto* editor = context_->color_editor) {
        // "Inny…": the swatch with its hex, three gradient sliders, then Cancel and Save.
        brush(gray(0, 0.80f));
        target_->FillRectangle(D2D1::RectF(0, 0, viewport_.width, viewport_.height), solid_.Get());
        const auto panel = layout.colorPanel();
        fill(panel, gray(0.07f), 18);
        if (panel_borders_) outline(panel, gray(1, 0.16f), 1.0f, 18);
        textGroup(editor->title, {}, panel.left + 28, panel.top + 28, panel.right - panel.left - 56,
                  category_font_.Get(), gray(0.96f), body, gray(0.62f), 1, true);
        const uint32_t current = editor->rgb();
        const auto swatch = layout.colorSwatch();
        fill(swatch, rgb(current), 14);
        outline(swatch, gray(1, 0.16f), 1.0f, 14);
        wchar_t hex[16]{};
        swprintf_s(hex, L"#%06X", current);
        textLine(hex, swatch.left, swatch.bottom + 28, swatch.right - swatch.left, body, gray(0.86f), DWRITE_TEXT_ALIGNMENT_CENTER);
        const std::array<const wchar_t*, 3> slider_labels{tr(L"Odcień"), tr(L"Nasycenie"), tr(L"Jasność")};
        for (int slider = 0; slider < settings_ui::ColorEditor::sliders; ++slider) {
            const auto track = layout.colorTrack(slider);
            const float y = middle(track);
            const bool focused = editor->focus == slider;
            const auto label = layout.colorLabel(slider);
            textLine(slider_labels[slider], label.left, y, label.right - label.left, body, gray(focused ? 0.96f : 0.66f));
            // The bar shows what moving the knob does: the full spectrum, or this colour from grey
            // (saturation) or from black (brightness).
            std::array<D2D1_GRADIENT_STOP, 7> stops{};
            UINT32 count = 2;
            if (slider == 0) {
                count = 7;
                for (UINT32 i = 0; i < count; ++i) {
                    const auto ink = rgb(settings_ui::ColorEditor::rgb(i * 60.0f, 1, 1));
                    stops[i] = {i / 6.0f, ink};
                }
            } else if (slider == 1) {
                stops[0] = {0, rgb(settings_ui::ColorEditor::rgb(editor->hue, 0, editor->value))};
                stops[1] = {1, rgb(settings_ui::ColorEditor::rgb(editor->hue, 1, editor->value))};
            } else {
                stops[0] = {0, rgb(0)};
                stops[1] = {1, rgb(settings_ui::ColorEditor::rgb(editor->hue, editor->saturation, 1))};
            }
            Microsoft::WRL::ComPtr<ID2D1GradientStopCollection> collection;
            Microsoft::WRL::ComPtr<ID2D1LinearGradientBrush> gradient;
            const auto bar = D2D1::RoundedRect(D2D1::RectF(track.left, y - 6, track.right, y + 6), 6, 6);
            if (SUCCEEDED(target_->CreateGradientStopCollection(stops.data(), count, collection.GetAddressOf())) &&
                SUCCEEDED(target_->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(D2D1::Point2F(track.left, y), D2D1::Point2F(track.right, y)),
                                                             collection.Get(), gradient.GetAddressOf())))
                target_->FillRoundedRectangle(bar, gradient.Get());
            brush(gray(1, focused ? 0.45f : 0.14f));
            target_->DrawRoundedRectangle(bar, solid_.Get(), 1.0f);
            const float knob = std::lerp(track.left, track.right, editor->level(slider));
            const float radius = focused ? 11.0f : 9.0f;
            brush(gray(0.96f));
            target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(knob, y), radius, radius), solid_.Get());
            brush(slider == 0 ? rgb(settings_ui::ColorEditor::rgb(editor->hue, 1, 1)) : rgb(current));
            target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(knob, y), radius - 3.5f, radius - 3.5f), solid_.Get());
            wchar_t readout[16]{};
            if (slider == 0) swprintf_s(readout, L"%d°", static_cast<int>(std::lround(editor->hue)));
            else swprintf_s(readout, L"%d%%", static_cast<int>(std::lround(editor->level(slider) * 100)));
            const auto value = layout.colorValue(slider);
            textLine(readout, value.left, y, value.right - value.left, body, gray(0.86f), DWRITE_TEXT_ALIGNMENT_TRAILING);
        }
        const auto save = layout.colorSave(), cancel = layout.colorCancel();
        centered(tr(L"Zapisz"), save, button(save, editor->focus == settings_ui::ColorEditor::save, false));
        brush(button(cancel, editor->focus == settings_ui::ColorEditor::cancel, false));
        target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F((cancel.left + cancel.right) * 0.5f, middle(cancel)), 3.5f, 3.5f), solid_.Get());
        return;
    }
    if (rows.empty()) return;
    // Modal panels: 28 px padding, the title's capitals start at the padding line.
    if (state.editing && (rows[state.row()].control == Control::secret || rows[state.row()].control == Control::text)) {
        const auto& item = rows[state.row()];
        const auto panel = layout.secretPanel();
        const auto glass = glassPanel(D2D1::RoundedRect(D2D1::RectF(panel.left, panel.top, panel.right, panel.bottom), 18, 18));
        brush(gray(0, 0.80f));
        target_->FillRectangle(D2D1::RectF(0, 0, viewport_.width, viewport_.height), solid_.Get());
        glassFill(glass, gray(0.07f));
        if (panel_borders_) outline(panel, gray(1, 0.16f), 1.0f, 18);
        std::wstring hint;
        if (item.id == Id::gog_callback)
            hint = context_->secret_length ? tr(L"Wklejono znaków: ") + std::to_wstring(context_->secret_length) + tr(L" · adres ukryty, bez skracania") :
                   std::wstring(tr(L"Wklej tutaj cały adres z przeglądarki (Ctrl+V)."));
        if (item.id == Id::epic_callback)
            hint = context_->secret_length ? tr(L"Wklejono znaków: ") + std::to_wstring(context_->secret_length) :
                std::wstring(tr(L"Wklej authorizationCode lub całą odpowiedź JSON po logowaniu."));
        if (item.id == Id::ea_callback || item.id == Id::battlenet_callback || item.id == Id::amazon_callback)
            hint = context_->secret_length ? tr(L"Wklejono znaków: ") + std::to_wstring(context_->secret_length) : item.description;
        textGroup(item.title, hint, panel.left + 28, panel.top + 28, panel.right - panel.left - 56,
                  category_font_.Get(), gray(0.96f), body, gray(0.62f), 1, true);
        const Rect input{panel.left + 28, panel.top + 90, panel.right - 28, panel.top + 153};
        const float round = (input.bottom - input.top) * 0.5f; // A pill field; the accent: it is being typed into.
        fill(input, context_->secret_selected ? gray(1, 0.13f) : gray(0.03f), round);
        outline(input, accent, 1.5f, round);
        // Masked input: one dot per character, on the field's centre line.
        brush(gray(0.80f));
        if (item.control == Control::text)
            text(context_->settings_text, input.left + 26, input.top + 16, input.right - input.left - 52, 40, body, gray(0.95f));
        if (layout.import_help) paragraph(companionImportHelp(), panel.left + 28, input.bottom + 24, panel.right - panel.left - 56, body, gray(0.68f), 12);
        const int dots = item.control == Control::text ? 0 : static_cast<int>(std::min<size_t>(context_->secret_length, 34));
        for (int dot = 0; dot < dots; ++dot)
            target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(input.left + 32 + dot * 13.0f, middle(input)), 3.5f, 3.5f), solid_.Get());
        const auto save = layout.secretSave(), cancel = layout.secretCancel();
        centered(tr(L"Zapisz"), save, button(save, !context_->secret_cancel && !context_->secret_browse, false));
        if (layout.import_help) {
            const auto browse = layout.secretBrowse();
            centered(tr(L"Przeglądaj…"), browse, button(browse, context_->secret_browse, false));
            centered(tr(L"Anuluj"), cancel, button(cancel, context_->secret_cancel, false));
        } else {
            brush(button(cancel, context_->secret_cancel, false));
            target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F((cancel.left + cancel.right) * 0.5f, middle(cancel)), 3.5f, 3.5f), solid_.Get());
        }
        return;
    }
    // Choice picker: fades and rises in on opening, fades out with its last list after closing
    // (ViewContext::settings_picker_*); the highlight glides between the choices.
    const int picker_row = context_->settings_picker_row;
    if (picker_row < 0 || picker_row >= static_cast<int>(rows.size()) || !rows[picker_row].picker()) return;
    const float shown = std::clamp(context_->settings_picker_fade, 0.0f, 1.0f);
    const float ease = shown * shown * (3 - 2 * shown);
    if (ease <= 0.004f) return;
    const auto& item = rows[picker_row];
    const int tile = context_->settings_picker_tile;
    const int current = state.editing ? state.choice : context_->settings_picker_choice;
    const auto& choices = item.pickList(tile);
    const int count = static_cast<int>(choices.size());
    const float lift = (1 - ease) * 22;
    const auto shift = [&](Rect r) { r.top += lift; r.bottom += lift; return r; };
    const auto faded = [&](float value, float alpha = 1) { return gray(value, alpha * ease); };
    const auto panel = shift(layout.picker(count));
    const auto glass = glassPanel(D2D1::RoundedRect(D2D1::RectF(panel.left, panel.top, panel.right, panel.bottom), 18, 18));
    brush(gray(0, 0.62f * ease));
    target_->FillRectangle(D2D1::RectF(0, 0, viewport_.width, viewport_.height), solid_.Get());
    glassFill(glass, gray(0.07f), ease);
    if (panel_borders_) outline(panel, faded(1, 0.16f), 1.0f, 18);
    textGroup(item.pickTitle(tile), {}, panel.left + 28, panel.top + 28, panel.right - panel.left - 56,
              category_font_.Get(), faded(0.95f), body, faded(0.62f), 1, true);
    const int picked = item.picked(tile);
    const int first = layout.firstChoice(count, current), end = std::min(count, first + layout.visibleChoices());
    if (current >= 0 && current < count) {
        // The highlight at its gliding position, kept inside the visible choices.
        const float glide = std::clamp(context_->settings_choice_glide.value_or(static_cast<float>(current)),
                                       static_cast<float>(first), static_cast<float>(end - 1));
        auto r = shift(layout.choice(first, count, current));
        const float offset = (glide - first) * 54.0f;
        r.top += offset; r.bottom += offset;
        fill(r, faded(1, 0.13f), (r.bottom - r.top) * 0.5f);
    }
    for (int i = first; i < end; ++i) {
        const auto r = shift(layout.choice(i, count, current));
        const float y = middle(r);
        float label_left = r.left + 18;
        if (item.control == Control::sources) {
            // Devices: the direction's icon before every name.
            drawAudioIcon(label_left + 10, y, item.tiles[std::clamp(tile, 0, static_cast<int>(item.tiles.size()) - 1)].input, false, faded(0.8f));
            label_left += 38;
        }
        if (item.id == Id::tile_shape) {
            shapePreview(label_left, y, i);
            label_left += 50;
        }
        const bool colour_list = item.id == Id::accent || item.id == Id::ui_accent || item.id == Id::button_color;
        if (colour_list && choices[i].custom) {
            // "Inny…": a small colour wheel.
            for (int segment = 0; segment < 12; ++segment) {
                const float from = segment * std::numbers::pi_v<float> / 6, to = from + std::numbers::pi_v<float> / 6 + 0.05f;
                auto ink = rgb(settings_ui::ColorEditor::rgb(segment * 30.0f, 0.85f, 1)); ink.a = ease;
                line(label_left + 10 + std::cos(from) * 6.5f, y + std::sin(from) * 6.5f,
                     label_left + 10 + std::cos(to) * 6.5f, y + std::sin(to) * 6.5f, ink, 5);
            }
            label_left += 34;
        } else if (colour_list) {
            // "Z okładki" has no fixed colour: show the neutral fallback used before a cover is analysed.
            // Buttons "as the interface accent": that accent.
            auto ink = choices[i].color ? rgb(choices[i].color) : item.id == Id::button_color ? uiAccent() : D2D1::ColorF(0.94f, 0.96f, 0.97f); ink.a = ease;
            brush(ink);
            target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(label_left + 10, y), 9, 9), solid_.Get());
            label_left += 34;
        }
        textLine(choices[i].label, label_left, y, r.right - label_left - 48, body, faded(current == i ? 0.97f : 0.86f));
        if (picked == i) {
            auto check = accent; check.a = ease;
            line(r.right - 31, y, r.right - 26, y + 5, check, 2);
            line(r.right - 26, y + 5, r.right - 17, y - 5, check, 2);
        }
    }
}

void Renderer::fitSettingsDescriptions(std::vector<settings_ui::Row>& rows, const settings_ui::State& state, bool desktop) {
    using namespace settings_ui;
    const auto layout = Layout::of(viewport_.width, viewport_.height, state);
    const auto body = settings_body_font_.Get();
    const float line = typeMetrics(body).line;
    for (auto& row : rows) {
        if (row.tags.empty() && row.id != Id::plugin_enabled) continue; // Plugins: a long description grows the row.
        if (desktop || layout.compact || row.description.empty() || line <= 0) { row.lines = 0; continue; }
        // The text width drawSettings gives the row (its title and description end before the controls).
        const float text_left = layout.contentLeft() + 24;
        const float controls_left = hasOptionsButton(row.id) ? layout.contentRight() - 324 : layout.controlsLeft(row.control);
        const int wrapped = static_cast<int>(std::lround(paragraph(row.description, 0, 0, controls_left - 24 - text_left, body, {}, 8, false) / line));
        // A standard row holds the title, one line of description and the tags; up to three lines grow it.
        if (row.tags.empty()) row.lines = wrapped > 2 ? std::min(wrapped, 4) : 0; // Two lines fit a standard row.
        else row.lines = wrapped >= 2 ? std::min(wrapped, 3) + 1 : 0;
    }
}