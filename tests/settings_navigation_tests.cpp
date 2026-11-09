#include "../src/ui/settings_view.hpp"
#include "../src/ui/quick_menu.hpp"
#include "../src/ui/grid_layout.hpp"
#include "../src/ui/spring.hpp"
#include <iostream>

int main() {
    using namespace settings_ui;
    State state;
    state.move(2, 2);
    if (state.category != 2 || state.options) return 1;
    state.enter(60);
    state.move(45, 60);
    const auto layout = Layout::of(1600, 900, 2);
    state.scroll[2] = layout.ensureVisible(state.row(), 60, 0);
    const auto row = layout.row(state.row(), state.offset());
    if (row.top < layout.listTop() || row.bottom > layout.listBottom() + 0.1f ||
        layout.hitRow(row.left + 8, row.top + 8, 60, state.offset()) != 45 ||
        layout.hitRow(row.left + 8, row.bottom + 4, 60, state.offset()) != -1) return 2;
    state.editing = true;
    if (!state.back() || !state.options || state.editing || !state.back() || state.options) return 3;
    state.selectCategory(0);
    state.selectCategory(2);
    state.enter(60);
    if (state.row() != 45 || state.offset() <= 0) return 4;
    state.clamp(0);
    state.options = false;
    state.enter(0);
    if (state.options || state.row() || state.back()) return 5;
    // Keyboard/pad reach Back (a dot at the top): Up past the first category focuses it, Down returns, rows never do.
    const int last_category = static_cast<int>(categories.size()) - 1;
    State rail;
    rail.selectCategory(0);
    rail.move(-1, 3);
    if (!rail.backFocused() || rail.category != 0) return 11;
    rail.move(-1, 3);
    if (!rail.backFocused() || rail.category != 0) return 12;
    rail.move(1, 3);
    if (rail.backFocused() || rail.category != 0) return 13;
    rail.move(-1, 3);
    rail.enter(3);
    if (rail.backFocused() || rail.back_focused || !rail.options) return 14;
    rail.move(5, 3);
    if (rail.backFocused() || rail.row() != 2) return 15;
    rail.options = false;
    rail.selectCategory(last_category);
    rail.move(1, 3);
    if (rail.backFocused() || rail.category != last_category) return 16; // Down at the last category stays there.
    rail.selectCategory(0);
    if (rail.backFocused() || rail.category != 0) return 17;    for (const auto size : {std::pair{1600.0f, 900.0f}, {2400.0f, 900.0f}, {1200.0f, 900.0f}}) {
        Layout scaled{size.first, size.second};
        for (int i = 0; i < static_cast<int>(categories.size()); ++i) {
            auto r = scaled.rail(i);
            if (scaled.hitRail(r.left + 10, r.top + 10) != i || r.right >= scaled.contentLeft() ||
                r.top < scaled.backButton().bottom + 12) return 6; // Back sits above the rail.
            // A description line moves the list down; the first row still hits at the list top.
            const auto category = Layout::of(size.first, size.second, i);
            if (category.listTop() != (described(i) ? Layout::described_top : Layout::plain_top) ||
                category.hitRow(category.contentLeft() + 8, category.listTop() + 1, 3, 0) != 0 ||
                category.hitRow(category.contentLeft() + 8, category.listTop() - 1, 3, 0) != -1) return 6;
        }
        // Row controls sit on the row's centre line and never reach into the text column.
        const auto slider = scaled.track(0, 0), minus = scaled.minus(0, 0), row0 = scaled.row(0, 0);
        if (std::abs((slider.top + slider.bottom) * 0.5f - (row0.top + Layout::row_center)) > 0.01f ||
            std::abs((minus.top + minus.bottom) * 0.5f - (row0.top + Layout::row_center)) > 0.01f ||
            scaled.sliderValue(0, 0).left < scaled.controlsLeft(Control::slider) ||
            scaled.layoutOption(0, 0, 0).left < scaled.controlsLeft(Control::layout) ||
            scaled.connect(0, 0).left < scaled.controlsLeft(Control::platform)) return 6;
        auto last = scaled.choice(8, 9);
        if (scaled.hitChoice(last.left + 5, last.top + 5, 9) != 8 || last.bottom > scaled.picker(9).bottom) return 7;
    }
    if (quick_menu::action(1, true) != quick_menu::Action::library ||
        quick_menu::action(1, false) != quick_menu::Action::library ||
        quick_menu::action(2, true) != quick_menu::Action::store ||
        quick_menu::action(3, true) != quick_menu::Action::downloads || // After the plugins' pages.
        quick_menu::action(2, false) != quick_menu::Action::downloads ||
        quick_menu::action(3, false) != quick_menu::Action::controllers ||
        quick_menu::action(4, false) != quick_menu::Action::audio ||
        quick_menu::action(5, false) != quick_menu::Action::widgets ||
        quick_menu::action(6, false) != quick_menu::Action::settings ||
        quick_menu::action(quick_menu::count(true) - 1, true) != quick_menu::Action::power) return 8;
    const auto [first, end] = grid_layout::visibleRange(100, 1600, 900, 2000);
    if (first <= 28 || end <= first || end > 100) return 9;
    for (size_t i = 0; i < 100; ++i) {
        const float y = grid_layout::top(i, 1600, 2000);
        if (y < 900 && y + grid_layout::cell(1600) > 0 && (i < first || i >= end)) return 10;
    }
    // Springs: glide never overshoots, bounce overshoots a little; both settle, also at the 50 ms dt cap.
    for (const float dt : {1.0f / 240, 1.0f / 60, 0.05f}) {
        Spring glide, bounce;
        float glide_max = 0, bounce_max = 0;
        for (float t = 0; t < 1.0f; t += dt) {
            glide.step(100, dt, spring::glide.omega, spring::glide.zeta);
            bounce.step(100, dt, spring::bounce.omega, spring::bounce.zeta);
            glide_max = std::max(glide_max, glide.value);
            bounce_max = std::max(bounce_max, bounce.value);
        }
        if (glide_max > 100.01f || bounce_max < 104 || bounce_max > 116 || !glide.settled(100) || !bounce.settled(100)) return 18;
        Spring half;
        for (float t = 0; t < 0.12f; t += dt) half.step(100, dt, spring::glide.omega, spring::glide.zeta);
        if (half.value < 50 || half.value > 95) return 19; // Visible glide, not a jump.
    }
    Spring snapped; snapped.velocity = 50; snapped.snap(7);
    if (snapped.value != 7 || snapped.velocity != 0 || !snapped.settled(7)) return 20;
    // A tall appearance row must remain hittable throughout its preview, while
    // scrolling later rows and the plugin section spacer use the same geometry.
    const auto appearance = Layout::of(1600, 900, static_cast<int>(Category::appearance));
    for (int i = 0; i < 12; ++i) {
        const auto offset = appearance.ensureVisible(i, 12, 0);
        const auto r = appearance.row(i, offset);
        if (r.top < appearance.listTop() - 0.01f || r.bottom > appearance.listBottom() + 0.01f ||
            appearance.hitRow(r.left + 10, r.bottom - 2, 12, offset) != i) return 21;
    }
    // Section headings (Row::section): a gap above each section's first row, never a hit target,
    // and ensureVisible brings a heading into view with its row.
    {
        std::vector<Row> rows(12, Row{Id::font, Control::toggle});
        rows[0].control = Control::layout;
        rows[0].section = L"A"; rows[3].section = L"B"; rows[8].section = L"C";
        State page; page.category = static_cast<int>(Category::appearance);
        const auto sectioned = Layout::of(1600, 900, page, rows);
        if (sectioned.layout_index != 0 || sectioned.sections != std::vector<int>{0, 3, 8}) return 25;
        const auto plain = Layout::of(1600, 900, page);
        if (sectioned.rowOffset(0) != Layout::section_gap || sectioned.rowOffset(2) != plain.rowOffset(2) + Layout::section_gap ||
            sectioned.rowOffset(3) != plain.rowOffset(3) + 2 * Layout::section_gap ||
            sectioned.rowOffset(11) != plain.rowOffset(11) + 3 * Layout::section_gap) return 26;
        const auto third = sectioned.row(3, 0);
        if (sectioned.hitRow(third.left + 10, third.top - Layout::section_label, 12, 0) != -1) return 27; // The heading.
        for (int i = 0; i < 12; ++i) {
            const auto offset = sectioned.ensureVisible(i, 12, 0);
            const auto r = sectioned.row(i, offset);
            if (r.top < sectioned.listTop() - 0.01f || r.bottom > sectioned.listBottom() + 0.01f ||
                sectioned.hitRow(r.left + 10, r.top + 2, 12, offset) != i) return 28;
        }
        // Scrolling back up to a section's first row shows its heading too.
        const float bottom = sectioned.maxScroll(12);
        const float up = sectioned.ensureVisible(8, 12, bottom);
        if (sectioned.row(8, up).top - Layout::section_gap < sectioned.listTop() - 0.01f) return 29;
    }
    // Tiled rows (Settings 2.0): the sound sources share the row in halves, the controller gallery
    // keeps its focused tile inside the content column and hittable.
    {
        std::vector<Row> rows(4, Row{Id::font, Control::toggle});
        rows[0].control = Control::pads;
        State page; page.category = static_cast<int>(Category::controllers);
        const auto gallery = Layout::of(1600, 900, page, rows);
        if (gallery.layout_index != 0 || gallery.rowHeight(0) != Layout::pads_height ||
            gallery.rowOffset(1) != Layout::row_step + Layout::pads_height - Layout::row_height) return 32;
        constexpr int count = 12;
        for (int focus = 0; focus < count; ++focus) {
            const auto t = gallery.tile(0, 0, Control::pads, focus, count, focus);
            if (t.left < gallery.contentLeft() - 0.01f || t.right > gallery.contentRight() + 0.01f ||
                gallery.hitTile(0, 0, Control::pads, count, focus, t.left + 5, t.top + 5) != focus) return 33;
        }
        rows[0].control = Control::sources;
        const auto sources = Layout::of(1600, 900, page, rows);
        const auto left = sources.tile(0, 0, Control::sources, 0, 2, 0), right = sources.tile(0, 0, Control::sources, 1, 2, 0);
        if (sources.rowHeight(0) != Layout::sources_height || left.right >= right.left ||
            std::abs(right.right - sources.contentRight()) > 0.01f ||
            sources.hitTile(0, 0, Control::sources, 2, 0, right.left + 4, right.top + 4) != 1) return 34;
    }
    // A long choice list (the sound folder's files) fits the screen and scrolls with the highlight.
    {
        const Layout screen{1600, 900};
        const int visible = screen.visibleChoices(), count = 40;
        const auto panel = screen.picker(count);
        if (visible < 3 || panel.top < 0 || panel.bottom > 900 || screen.firstChoice(count, 0) != 0 ||
            screen.firstChoice(count, count - 1) != count - visible) return 30;
        const int scrolled = screen.firstChoice(count, 30);
        const auto shown = screen.choice(30, count, 30), top = screen.choice(scrolled, count, 30);
        if (shown.top < panel.top || shown.bottom > panel.bottom ||
            screen.hitChoice(top.left + 5, top.top + 5, count, 30) != scrolled || screen.firstChoice(5, 4) != 0) return 31;
    }
    LayoutPreview preview;
    preview.focus(1, 1000); preview.focus(1, 1900);
    if (preview.started != 1000 || !preview.tick(2999) || preview.seconds(2999) < 1.99f) return 22;
    if (!preview.tick(3000) || preview.tick(3001) || preview.seconds(10000) != 2) return 23;
    preview.focus(-1, 4000); preview.focus(1, 5000);
    if (!preview.tick(5000) || preview.seconds(5000) != 0) return 24;
    std::cout << "Settings focus, scroll, hit targets, visible artwork range and springs passed\n";
}
