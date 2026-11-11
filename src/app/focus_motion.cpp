// Focus motion: highlights glide between items (quick bar circle, Settings pill/row/scroll,
// context menu, power menu and exit prompt, devices panels, drawer tiles and friends), and the
// experimental spring outline (settings focus_spring) follows the focused cover on its own,
// PS5-like: it reaches the new card first, overshoots and settles, glows faintly on the side it
// moves toward, and at a list edge bumps toward the edge with a full inner glow. With
// focus_spring the highlights overshoot a little too. Springs: src/ui/spring.hpp.
#include "launcher.hpp"
#include "../ui/friends_layout.hpp"
#include "../ui/quick_menu.hpp"
#include "../ui/grid_layout.hpp"
#include "../ui/psfive_layout.hpp"
#include "../ui/artwork_browser.hpp"

#include <algorithm>
#include <cmath>

namespace {
constexpr float slot_kick = 240.0f;   // px/s: a focus change in a fixed slot (the cards slide under it).
constexpr float breath_kick = 70.0f;  // px/s: the outline swells a little on every focus change.
constexpr float edge_kick = 300.0f;   // px/s toward a hit list edge.
constexpr float glow_seconds = 0.45f; // Inner glow fade-out.
constexpr float move_glow = 0.5f;     // A plain move glows half as strongly as a list edge.
constexpr ULONGLONG move_window_ms = 400; // Key/pad input this recent explains a focus change.
}

bool Launcher::cardFocusVisible() const {
    if (desktopActive()) return false; // The desktop grid draws its own selection ring.
    const bool cards_screen = screen_ == ScreenMode::shelves || screen_ == ScreenMode::platform_games || gridScreen(screen_);
    return cards_screen && !scanning_ && !cards_.empty() && selected_ < cards_.size() && !libraryList() &&
           !menu_open_ && !quick_open_ && !friends_open_ && !category_menu_open_ && !category_rail_focus_ &&
           !pending_launch_ && !exit_prompt_ && !power_menu_ && !gogInstallOpen() && !customEditorOpen() && !identifyOpen() && !psfiveActions() && !psfiveTopFocused() &&
           !(libraryControls() && (library_controls_.focus >= 0 || library_controls_.action >= 0 || library_controls_.open())) && !searchBackFocused();
}

void Launcher::focusBoundary() {
    if (!settings_.focus_spring || !focus_ring_.valid || !nav_direction_ || !cardFocusVisible()) return;
    auto& ring = focus_ring_;
    if (std::abs(nav_direction_) == 1) ring.x.velocity += static_cast<float>(nav_direction_) * edge_kick;
    else ring.y.velocity += static_cast<float>(nav_direction_ / 2) * edge_kick;
    ring.glow = 1;
    ring.glow_peak = 1;
    ring.glow_direction = nav_direction_;
    InvalidateRect(hwnd_, nullptr, FALSE);
}

// The motion shared with the Guide overlay (tickGameOverlay): the drawer's focus frame and scroll, and the
// highlight of Zasilanie (and of the widget options' dialog and the close menu, which are its layer).
void Launcher::animateMenuFocus(float dt, bool& needs_paint) {
    const auto tuning = settings_.focus_spring ? spring::bounce : spring::glide;
    if (!power_menu_) power_glide_.valid = false;
    else {
        const float value = static_cast<float>(*power_menu_);
        if (!power_glide_.valid) { power_glide_.value.snap(value); power_glide_.valid = true; }
        else power_glide_.value.step(value, dt, tuning.omega, tuning.zeta);
        if (!power_glide_.value.settled(value, 0.004f)) needs_paint = true;
    }
    const auto glideBox = [&](BoxGlide& glide, bool open, int key, float l, float t, float r, float b) {
        if (!open) { glide.valid = false; return; }
        if (!glide.valid || glide.key != key) { glide.box.snap(l, t, r, b); glide.key = key; glide.valid = true; }
        else glide.box.step(l, t, r, b, dt, tuning);
        if (!glide.box.settled(l, t, r, b)) needs_paint = true;
    };
    // Drawer: a tile, or in the friends list a row (following its gliding scroll) or Back.
    if (friends_open_ || friends_progress_ > 0.003f) {
        using namespace friends_layout;
        if (friends_list_) {
            const float scroll = static_cast<float>(friends_scroll_);
            if (!friends_scroll_valid_) { friends_scroll_glide_.snap(scroll); friends_scroll_valid_ = true; }
            else friends_scroll_glide_.step(scroll, dt, spring::glide.omega, spring::glide.zeta);
            if (!friends_scroll_glide_.settled(scroll)) needs_paint = true;
        } else friends_scroll_valid_ = false;
        if (plugin_drawer_active_) { drawer_glide_.valid = false; }
        else if (!friends_list_) {
            const auto tile = dashboardLayout().tile(dashboard_tile_, 0, dashboard_scroll_);
            glideBox(drawer_glide_, true, 0, tile.left, tile.top, tile.right, tile.bottom);
        } else if (friends_back_ || friends_.friends.empty() || friends_cursor_ >= friends_.friends.size()) {
            const auto back = listBack(0, friendsDocked());
            glideBox(drawer_glide_, true, 1, back.left, back.top, back.right, back.bottom);
        } else {
            const float top = rowTop(friends_.friends, friends_cursor_) - friends_scroll_glide_.value;
            glideBox(drawer_glide_, true, 1, 18, top + row_padding, drawer_width - 17, top + row_height - row_padding);
        }
    } else { drawer_glide_.valid = false; friends_scroll_valid_ = false; }
}

void Launcher::animateFocus(float dt, float target_x, float target_y, float target_w, float target_h, bool& needs_paint) {
    const auto tuning = settings_.focus_spring ? spring::bounce : spring::glide;
    const auto view = renderer_.viewport();

    // Quick bar: the circle glides to the focused icon (snapped when the bar opens).
    if (quick_open_ || quick_progress_ > 0.003f) {
        const float target = quick_menu::center(view.width, quick_selected_, quick_menu::store_slots);
        if (!quick_circle_valid_) { quick_circle_.snap(target); quick_circle_valid_ = true; }
        else quick_circle_.step(target, dt, tuning.omega, tuning.zeta);
        if (!quick_circle_.settled(target)) needs_paint = true;
    } else quick_circle_valid_ = false;

    // Settings: the rail pill (a category or Back), the focused row and the list scroll.
    if (settingsOpen()) {
        using settings_ui::Layout;
        auto& motion = settings_motion_;
        const auto& state = settings_state_;
        const auto layout = Layout::of(view.width, view.height, state, settings_rows_);
        const auto pill = state.backFocused() ? layout.backButton() : layout.categoryRail(state.category);
        const float rail_focus = state.options ? 0.0f : 1.0f;
        const float row = layout.rowOffset(state.row()), scroll = state.offset(), height = layout.rowHeight(state.row());
        if (!motion.valid) {
            motion.pill_top.snap(pill.top); motion.pill_bottom.snap(pill.bottom); motion.rail_focus.snap(rail_focus);
            motion.row.snap(row); motion.scroll.snap(scroll); motion.height.snap(height);
            motion.category = state.category;
            motion.valid = true;
        } else {
            // Another category is another list: its row and scroll start in place.
            if (motion.category != state.category) { motion.row.snap(row); motion.scroll.snap(scroll); motion.height.snap(height); motion.category = state.category; }
            motion.pill_top.step(pill.top, dt, tuning.omega, tuning.zeta);
            motion.pill_bottom.step(pill.bottom, dt, tuning.omega, tuning.zeta);
            motion.row.step(row, dt, tuning.omega, tuning.zeta);
            motion.height.step(height, dt, spring::glide.omega, spring::glide.zeta);
            // A fade factor and the clipped list never overshoot.
            motion.rail_focus.step(rail_focus, dt, spring::glide.omega, spring::glide.zeta);
            motion.scroll.step(scroll, dt, spring::glide.omega, spring::glide.zeta);
        }
        if (!motion.pill_top.settled(pill.top) || !motion.pill_bottom.settled(pill.bottom) || !motion.row.settled(row) ||
            !motion.scroll.settled(scroll) || !motion.height.settled(height) || !motion.rail_focus.settled(rail_focus, 0.004f)) needs_paint = true;
    } else settings_motion_.valid = false;

    // PSfive overflow: glide both the selection and the clipped list, like Settings/friends.
    // Opening snaps to the current category; closing keeps the last motion while it folds up.
    if (psfiveActive() && (category_menu_open_ || category_menu_progress_ > 0.003f)) {
        auto& motion = psfive_menu_motion_;
        const float row = static_cast<float>(category_cursor_), scroll = category_menu_scroll_;
        if (!motion.valid) { motion.row.snap(row); motion.scroll.snap(scroll); motion.valid = true; }
        else {
            motion.row.step(row, dt, tuning.omega, tuning.zeta);
            motion.scroll.step(scroll, dt, spring::glide.omega, spring::glide.zeta);
        }
        if (!motion.row.settled(row, 0.004f) || !motion.scroll.settled(scroll)) needs_paint = true;
    } else psfive_menu_motion_.valid = false;
    // All top-bar elements fade together; saved focus behind a drawer/dialog is inactive.
    if (psfiveActive()) {
        const float opacity = psfiveTopActive() ? 1.0f : 0.66f;
        psfive_top_opacity_.step(opacity, dt, spring::glide.omega, spring::glide.zeta);
        if (!psfive_top_opacity_.settled(opacity, 0.001f)) needs_paint = true;
        else psfive_top_opacity_.snap(opacity);
    } else psfive_top_opacity_.snap(0.66f);

    // Row menus: the highlight glides by fractional row (snapped when the menu opens).
    const auto glideIndex = [&](IndexGlide& glide, bool open, int target) {
        if (!open) { glide.valid = false; return; }
        const float value = static_cast<float>(target);
        if (!glide.valid) { glide.value.snap(value); glide.valid = true; }
        else glide.value.step(value, dt, tuning.omega, tuning.zeta);
        if (!glide.value.settled(value, 0.004f)) needs_paint = true;
    };
    glideIndex(menu_glide_, menu_open_ || menu_progress_ > 0.003f, menu_selected_);
    glideIndex(flix_rail_glide_, flixRailFocused(), flix_rail_focus_);
    glideIndex(exit_glide_, exit_prompt_.has_value(), exit_prompt_.value_or(0));
    glideIndex(settings_choice_glide_, settingsPicking(), settings_state_.choice);
    if (libraryTabsShown()) glideIndex(library_tab_glide_, true, libraryTabIndex(libraryTabs()));
    else library_tab_glide_.valid = false;
    glideIndex(home_tab_glide_, homeTabsShown() && !metroActive(), homeTabIndex());
    glideIndex(library_choice_glide_, libraryControls() && (library_controls_.open() ||
        library_views_progress_ > 0.003f || library_menu_progress_ > 0.003f), library_controls_.choice);
    // Półka: Cover Flow's centre glides to the focused cover without overshooting (a cover never
    // turns back). Entering the view the covers sweep in from the right; a long jump (search, a
    // click far away) glides over the last few covers only.
    if (libraryBookshelf() && !cards_.empty()) {
        const float target = static_cast<float>(selected_);
        auto& flow = library_flow_.value;
        if (!library_flow_.valid) { flow.snap(target - 2.5f); library_flow_.valid = true; }
        else {
            flow.value = std::clamp(flow.value, target - 6, target + 6);
            flow.step(target, dt, 12.0f, 1.0f);
        }
        if (!flow.settled(target, 0.002f)) needs_paint = true;
        else flow.snap(target);
    } else library_flow_.valid = false;

    // Focus frames that change size between items. key: a different screen/list snaps.
    const auto glideBox = [&](BoxGlide& glide, bool open, int key, float l, float t, float r, float b) {
        if (!open) { glide.valid = false; return; }
        if (!glide.valid || glide.key != key) { glide.box.snap(l, t, r, b); glide.key = key; glide.valid = true; }
        else glide.box.step(l, t, r, b, dt, tuning);
        if (!glide.box.settled(l, t, r, b)) needs_paint = true;
    };
    if (libraryList() && !cards_.empty()) {
        const float left = library_layout::rowLeft(libraryView(), view.width, view.height);
        const float right = library_layout::rowRight(libraryView(), view.width, view.height);
        const float top = list_layout::rowTop(cards_[selected_].y);
        if (arrived_ <= 0.06f) library_row_glide_.valid = false;
        glideBox(library_row_glide_, true, static_cast<int>(libraryView()), left, top, right, top + list_layout::row_height);
    } else library_row_glide_.valid = false;
    // Kolekcje / Wersje dialog: Back, a row (scrolled), a version card or "Zapamiętaj wybór"; the
    // text field keeps its own accent outline.
    if (libraryDialogOpen() && library_dialog_.focus != -2) {
        const auto& ui = library_dialog_;
        const auto layout = library_manager_ui::Layout::of(view.width, view.height, ui);
        std::optional<settings_ui::Rect> frame;
        if (ui.focus == -1) { const auto b = layout.back(); frame = settings_ui::Rect{b.left, b.top, b.left + (b.bottom - b.top), b.bottom}; }
        else if (ui.page == library_manager_ui::Page::versions) {
            if (ui.focus == static_cast<int>(ui.versions.size())) frame = layout.remember();
            else if (ui.focus >= ui.scroll && ui.focus < ui.scroll + layout.versionCapacity()) frame = layout.version(ui.focus - ui.scroll);
        } else if (ui.focus >= 0 && ui.focus < static_cast<int>(ui.rows.size())) frame = layout.row(ui, ui.focus);
        if (frame) glideBox(manager_glide_, true, static_cast<int>(ui.page), frame->left, frame->top, frame->right, frame->bottom);
        else manager_glide_.valid = false;
    } else manager_glide_.valid = false;
    // Pobierania: Back or a row.
    if (downloadsOpen()) {
        const downloads_ui::Layout layout{view.width, view.height};
        const auto r = downloads_state_.back || downloads_games_.empty() ? layout.back() :
                       layout.row(static_cast<int>(selected_), downloads_state_.scroll);
        glideBox(downloads_glide_, true, 0, r.left, r.top, r.right, r.bottom);
    } else downloads_glide_.valid = false;
    // Library top bar: Back, the filters, the magnifier.
    if (libraryControls() && library_controls_.focus >= 0 && !library_controls_.open()) {
        const auto r = library_ui::Layout{view.width}.button(library_controls_.focus);
        glideBox(library_bar_glide_, true, 0, r.left, r.top, r.right, r.bottom);
    } else library_bar_glide_.valid = false;
    if (libraryButtons() && library_controls_.action >= 0) {
        const auto r = libraryButton(library_controls_.action);
        glideBox(library_action_glide_, true, libraryOverview() ? 1 : 0, r.left, r.top, r.right, r.bottom);
    } else library_action_glide_.valid = false;
    if (psfiveActions()) {
        const auto r = psfive_layout::action(psfive_action_, view.height, psfiveScale());
        glideBox(psfive_action_glide_, true, 0, r.left, r.top, r.right, r.bottom);
    } else psfive_action_glide_.valid = false;
    // Szczegóły: Back, the rating, a button or a card.
    if (screen_ == ScreenMode::details && !cards_.empty()) {
        const details_layout::Layout layout{view.width, view.height};
        const auto all = layout.items(detailsActions());
        if (const auto* item = layout.find(all, detail_action_))
            glideBox(details_glide_, true, 0, item->rect.left, item->rect.top, item->rect.right, item->rect.bottom);
        else details_glide_.valid = false;
    } else details_glide_.valid = false;
    // Zmień grafikę: the rail's types, its buttons and Back (not while the gallery or the search has the focus).
    // Back is its own circle at the top left (drawn white when focused): the pill does not glide there.
    if (artworkOpen() && artwork_state_.railFocus() && !artwork_state_.back_focus && !artwork_state_.page_focus) {
        const auto& state = artwork_state_;
        const auto layout = state.layout(view.width, view.height);
        const auto r = state.local_focus ? layout.localButton() : layout.base().rail(state.category);
        glideBox(artwork_glide_, true, 0, r.left, r.top, r.right, r.bottom);
    } else artwork_glide_.valid = false;
    // Devices: Back, a list row, or in Audio the slider, mute button, an output/input or a footer.
    {
        const auto& state = device_state_;
        const device_ui::Layout layout{view.width, view.height, state.panel};
        std::optional<settings_ui::Rect> frame;
        if (devicesOpen()) {
            if (state.selected < 0) frame = layout.back();
            else if (state.selected < static_cast<int>(state.rows.size())) {
                if (state.panel == devices::Panel::audio) frame = device_ui::AudioLayout{view.width, view.height}.row(state, state.selected);
                else if (state.gallery()) {
                    const int focus = layout.focusTile(state);
                    frame = layout.tile(focus, static_cast<int>(state.rows.size()), focus);
                }
                else if (state.panel == devices::Panel::controllers && state.data.controllers.empty()) frame = layout.controllerEmptyButton();
                else frame = layout.row(state.selected, state.scroll);
            }
        }
        if (frame) glideBox(device_glide_, true, static_cast<int>(state.panel), frame->left, frame->top, frame->right, frame->bottom);
        else device_glide_.valid = false;
    }
    animateMenuFocus(dt, needs_paint); // The drawer and Zasilanie, which the Guide overlay animates as well.

    // Experimental spring outline: its centre and size spring toward the focused card's resting
    // rectangle, so it arrives before the easing cover and swings past it.
    auto& ring = focus_ring_;
    if (!settings_.focus_spring || target_w <= 0 || target_h <= 0 || cards_.empty() || selected_ >= cards_.size()) {
        ring.valid = false;
        return;
    }
    const float cx = target_x + target_w * 0.5f, cy = target_y + target_h * 0.5f;
    // First frame, or a rebuilt screen whose cards are rising in: start at the card, no flight.
    if (!ring.valid || arrived_ <= 0.06f) {
        ring.x.snap(cx); ring.y.snap(cy); ring.width.snap(target_w); ring.height.snap(target_h);
        ring.selected = selected_;
        ring.glow = 0;
        ring.valid = true;
        return;
    }
    if (ring.selected != selected_) {
        const bool keyed = nav_direction_ && GetTickCount64() - nav_direction_at_ < move_window_ms;
        // The resting slot may stay put while the cards slide under it (shelves, XMB lists):
        // a kick along the move keeps the outline visibly on its own.
        if (keyed && std::abs(cx - ring.x.value) + std::abs(cy - ring.y.value) < 6.0f) {
            if (std::abs(nav_direction_) == 1) ring.x.velocity += static_cast<float>(nav_direction_) * slot_kick;
            else ring.y.velocity += static_cast<float>(nav_direction_ / 2) * slot_kick;
        }
        // Every key/pad move glows at half strength on the side it goes toward; an edge hit
        // still in view keeps its full glow.
        if (keyed && ring.glow * ring.glow_peak < move_glow) {
            ring.glow = 1;
            ring.glow_peak = move_glow;
            ring.glow_direction = nav_direction_;
        }
        ring.width.velocity += breath_kick;
        ring.height.velocity += breath_kick;
        ring.selected = selected_;
    }
    ring.x.step(cx, dt, tuning.omega, tuning.zeta);
    ring.y.step(cy, dt, tuning.omega, tuning.zeta);
    ring.width.step(target_w, dt, tuning.omega, tuning.zeta);
    ring.height.step(target_h, dt, tuning.omega, tuning.zeta);
    ring.glow = std::max(0.0f, ring.glow - dt / glow_seconds);
    if (ring.glow > 0 || !ring.x.settled(cx) || !ring.y.settled(cy) || !ring.width.settled(target_w) || !ring.height.settled(target_h))
        needs_paint = true;
}

void Launcher::focusContext(ViewContext& context) const {
    if (psfiveActive()) {
        context.psfive_top_opacity = std::clamp(psfive_top_opacity_.value, 0.66f, 1.0f);
        context.category_scroll = psfiveMenuScroll();
        if (psfive_menu_motion_.valid) context.psfive_menu_glide = psfive_menu_motion_.row.value;
    }
    if (quick_circle_valid_) context.quick_circle_x = quick_circle_.value;
    // The pill never waits for a tick: a frame at the target would jump. The row and scroll of a
    // just-changed category are drawn at their targets until the next tick snaps them there.
    const auto& motion = settings_motion_;
    if (settingsOpen() && motion.valid)
        context.settings_motion = ViewContext::SettingsMotion{motion.pill_top.value, motion.pill_bottom.value,
            std::clamp(motion.rail_focus.value, 0.0f, 1.0f), motion.row.value, motion.scroll.value,
            motion.category == settings_state_.category, motion.height.value};
    if (menu_glide_.valid) context.menu_glide = menu_glide_.value.value;
    if (flix_rail_glide_.valid && flixRailFocused()) context.flix_rail_glide = flix_rail_glide_.value.value;
    bool picker_motion = true;
#ifdef LAUNCHME_UI_TEST
    picker_motion = picker_motion_for_test_;
#endif
    if (picker_motion && settings_choice_glide_.valid && settingsPicking()) context.settings_choice_glide = settings_choice_glide_.value.value;
    if (library_choice_glide_.valid) context.library_choice_glide = library_choice_glide_.value.value;
    if (library_flow_.valid && libraryBookshelf()) context.library_flow = library_flow_.value.value;
    if (exit_prompt_ && exit_glide_.valid) context.picker_glide = exit_glide_.value.value;
    else if (power_menu_ && power_glide_.valid) context.picker_glide = power_glide_.value.value - (hud_menu_ ? static_cast<float>(hud_menu_first_) : 0.0f);
    const auto box = [](const SpringBox& spring) {
        return ViewContext::GlideBox{spring.left.value, spring.top.value, spring.right.value, spring.bottom.value};
    };
    if (library_row_glide_.valid && library_row_glide_.key == static_cast<int>(libraryView()))
        context.library_row_glide = box(library_row_glide_.box);
    if (library_action_glide_.valid) context.library_action_glide = box(library_action_glide_.box);
    if (psfive_action_glide_.valid && psfiveActions()) context.psfive_action_glide = box(psfive_action_glide_.box);
    if (details_glide_.valid && screen_ == ScreenMode::details) context.details_glide = box(details_glide_.box);
    if (library_bar_glide_.valid && libraryControls()) context.library_bar_glide = box(library_bar_glide_.box);
    if (downloads_glide_.valid && downloadsOpen()) context.downloads_glide = box(downloads_glide_.box);
    if (manager_glide_.valid && libraryDialogOpen()) context.manager_glide = box(manager_glide_.box);
    if (artwork_glide_.valid && artworkOpen()) context.artwork_glide = box(artwork_glide_.box);
    if (devicesOpen() && device_glide_.valid && device_glide_.key == static_cast<int>(device_state_.panel)) context.device_glide = box(device_glide_.box);
    if (drawer_glide_.valid && drawer_glide_.key == (friends_list_ ? 1 : 0)) context.drawer_glide = box(drawer_glide_.box);
    if (friends_list_ && friends_scroll_valid_) context.friends_scroll_glide = friends_scroll_glide_.value;
    context.friends_back = friends_list_ && friends_back_;
    if (settings_.focus_spring && focus_ring_.valid) {
        const float width = std::max(1.0f, focus_ring_.width.value), height = std::max(1.0f, focus_ring_.height.value);
        context.focus_ring = ViewContext::FocusRing{focus_ring_.x.value - width * 0.5f, focus_ring_.y.value - height * 0.5f,
                                                    width, height, focus_ring_.glow * focus_ring_.glow_peak, focus_ring_.glow_direction};
    }
}
