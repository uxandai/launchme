#include "launcher.hpp"
#include "../ui/blades_layout.hpp"

// Blades: the default home's shelves (categories_ / category_ / cards_), so navigation, art and
// menus are the default layout's; only the geometry (blades_layout.hpp) and the drawing differ.

bool Launcher::bladesActive() const {
    return settings_.home_layout == AppSettings::HomeLayout::blades && screen_ == ScreenMode::shelves && !desktop_mode_;
}

void Launcher::bladesTarget(size_t index, float& x, float& y, float& size, float& aspect, float& delay) const {
    const auto view = renderer_.viewport();
    const int offset = static_cast<int>(index) - static_cast<int>(selected_);
    size = blades_layout::panelSize(offset, view.height);
    x = blades_layout::panelLeft(offset, view.height);
    y = blades_layout::panelTop(offset, view.height);
    aspect = 1;
    delay = static_cast<float>(std::clamp(offset, 0, 6)) * 0.035f;
}
