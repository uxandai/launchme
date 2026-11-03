#include "launcher.hpp"
#include "../ui/snitch_layout.hpp"

bool Launcher::snitchActive() const {
    return settings_.home_layout == AppSettings::HomeLayout::snitch && screen_ == ScreenMode::shelves && !desktop_mode_;
}

void Launcher::snitchTarget(size_t index, float& x, float& y, float& size, float& aspect, float& delay) const {
    const auto view = renderer_.viewport();
    size = snitch_layout::tileSize(view.height);
    x = snitch_layout::tileLeft(index, selected_, view.height);
    y = snitch_layout::rowTop(view.height);
    aspect = 1;
    delay = static_cast<float>(std::min<size_t>(index > selected_ ? index - selected_ : 0, 5)) * 0.04f;
}

bool Launcher::snitchClick(float x, float y, bool double_click) {
    if (!snitchActive() || menu_open_ || friends_open_ || category_menu_open_ || pending_launch_) return false;
    const auto view = renderer_.viewport();
    const int item = snitch_layout::hitDock(view.width, view.height, x, y, quick_menu::store_slots);
    if (item >= 0) {
        if (!double_click) { quick_open_ = true; quick_selected_ = item; quickAccept(); }
        return true;
    }
    if (snitch_layout::profile().contains(x, y)) {
        if (!double_click) { closeQuick(); toggleFriendsDrawer(); }
        return true;
    }
    return false;
}
