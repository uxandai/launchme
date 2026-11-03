// Settings → Interfejs → Dostosuj wygląd: the theme studio (src/ui/theme_studio.hpp). Every choice is
// applied to the running launcher at once through the settings' own handlers, so the screen behind and
// the preview on the right are the real thing; Cofnij restores the look the studio was opened with.
// Looks are saved by name in <config>\themes (src/platform/theme_preset.hpp).
#include "launcher.hpp"
#include "../platform/theme_preset.hpp"

using namespace settings_ui;

namespace {
std::wstring percent(float value) { return std::to_wstring(static_cast<int>(std::round(value * 100))) + L"%"; }

// Short names of the cards that have no short name in the settings lists.
const wchar_t* gapName(int index) {
    static constexpr std::array<const wchar_t*, 3> names{trKey(L"Brak"), trKey(L"Na zewnątrz"), trKey(L"Do środka")};
    return names[static_cast<size_t>(std::clamp(index, 0, 2))];
}
const wchar_t* entranceName(int index) {
    static constexpr std::array<const wchar_t*, 3> names{trKey(L"Rosną"), trKey(L"Wysuwają się"), trKey(L"Od razu")};
    return names[static_cast<size_t>(std::clamp(index, 0, 2))];
}
const wchar_t* placeName(int index) {
    static constexpr std::array<const wchar_t*, 3> names{trKey(L"W układzie"), trKey(L"Na początku"), trKey(L"Na końcu")};
    return names[static_cast<size_t>(std::clamp(index, 0, 2))];
}
const wchar_t* artName(int index) {
    static constexpr std::array<const wchar_t*, 3> names{trKey(L"Okładki"), trKey(L"Plakaty"), trKey(L"Kafle")};
    return names[static_cast<size_t>(std::clamp(index, 0, 2))];
}
const wchar_t* idleName(int index) {
    static constexpr std::array<const wchar_t*, 3> names{trKey(L"Szkło"), trKey(L"Barwione"), trKey(L"Ciemne")};
    return names[static_cast<size_t>(std::clamp(index, 0, 2))];
}
const wchar_t* motionName(int index) {
    static constexpr std::array<const wchar_t*, 3> names{trKey(L"Bez animacji"), trKey(L"Pulsowanie"), trKey(L"Wędrujący obrys")};
    return names[static_cast<size_t>(std::clamp(index, 0, 2))];
}
}

bool Launcher::themeStudioOpen() const { return theme_.open; }

void Launcher::themeStudio(bool open) {
    auto& state = theme_;
    if (open == state.open) return;
    if (open) {
        if (!settingsOpen() && !desktop_mode_) return;
        const bool from_settings = settingsOpen() && !desktop_mode_;
        if (from_settings) { saveSettings(); showCategory(category_); } // The real home behind the studio is its preview.
        state = {};
        state.open = true;
        state.from_settings = from_settings;
        theme_backup_ = settings_;
        state.saved = theme_preset::list();
        themeBuildOptions();
        themeRefreshActive();
        closeKeyboard(); cancelEnterHold(); cancelTabHold();
        audio_.context();
    } else {
        themeTouch(); // The home leaves with its focus where it was.
        if (state.example != 0 && !desktop_mode_) { state.example = 0; themeApplyExample(); }
        const bool back_to_settings = state.from_settings;
        state = {};
        saveSettings();
        audio_.navigate();
        if (back_to_settings) { // Back to Interfejs, on the studio's own row.
            openSettings(); settings_state_.selectCategory(0); buildSettingsRows();
            revealSetting(Id::theme_studio);
        }
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

// Which saved look equals the current one (its chip is marked).
void Launcher::themeRefreshActive() {
    auto& state = theme_;
    state.active = -1;
    for (size_t i = 0; i < state.saved.size(); ++i) {
        AppSettings look = settings_;
        if (theme_preset::load(state.saved[i], look) && theme_preset::sameLook(look, settings_)) { state.active = static_cast<int>(i); break; }
    }
}

void Launcher::themeBuildOptions() {
    auto& state = theme_;
    std::vector<theme_ui::Option> options;
    using theme_ui::Kind; using theme_ui::Look; using Option = theme_ui::Option;
    const auto toggle = [&](Id id, const wchar_t* title, bool on) {
        Option o; o.id = id; o.kind = Kind::toggle; o.title = tr(title); o.on = on;
        options.push_back(std::move(o));
    };
    const auto cards = [&](Id id, Look look, const wchar_t* title, int current, std::vector<theme_ui::Card> list) {
        Option o; o.id = id; o.kind = Kind::cards; o.look = look; o.title = tr(title);
        o.cards = std::move(list); o.current = std::clamp(current, 0, std::max(0, static_cast<int>(o.cards.size()) - 1));
        options.push_back(std::move(o));
    };
    const auto slider = [&](Id id, const wchar_t* title, float level, std::wstring value) {
        Option o; o.id = id; o.kind = Kind::slider; o.title = tr(title); o.level = std::clamp(level, 0.0f, 1.0f); o.value = std::move(value);
        options.push_back(std::move(o));
    };
    const auto action = [&](Id id, const wchar_t* title, std::wstring caption) {
        Option o; o.id = id; o.kind = Kind::action; o.title = tr(title); o.value = std::move(caption);
        options.push_back(std::move(o));
    };
    const auto names = [&](const auto& list, size_t count) {
        std::vector<theme_ui::Card> result;
        for (size_t i = 0; i < count; ++i) result.push_back({tr(list[i])});
        return result;
    };
    // A colour list: the presets and, when the current colour is none of them, it as "Własny".
    const auto colors = [&](Id id, const wchar_t* title, uint32_t current, const auto& values, const auto& labels) {
        std::vector<theme_ui::Card> list;
        int chosen = -1;
        for (size_t i = 0; i < values.size(); ++i) {
            list.push_back({tr(labels[i]), values[i]});
            if (values[i] == current) chosen = static_cast<int>(i);
        }
        if (chosen < 0) { wchar_t custom[32]{}; swprintf_s(custom, L"#%06X", current); list.push_back({custom, current}); chosen = static_cast<int>(list.size()) - 1; }
        cards(id, Look::swatch, title, chosen, std::move(list));
    };
    using HomeLayout = AppSettings::HomeLayout;
    const auto& s = settings_;
    switch (state.current()) {
    case theme_ui::Tab::layout: {
        cards(Id::home_layout, Look::layout, L"Układ trybu TV", static_cast<int>(s.home_layout), names(home_layouts, home_layouts.size()));
        if (s.home_layout == HomeLayout::shelves) {
            toggle(Id::show_categories, L"Kategorie zawsze widoczne", s.show_categories);
            toggle(Id::shelves_posters, L"Pionowe plakaty", s.shelves_posters);
            toggle(Id::shelves_steam, L"Wygląd Steam", s.shelves_steam);
            toggle(Id::shelves_category_all, L"Kafelek „Wszystkie” w rzędach", s.shelves_category_all);
            toggle(Id::shelves_hero_facts, L"Informacje pod logo", s.shelves_hero_facts);
        } else if (s.home_layout == HomeLayout::xmb) {
            cards(Id::xmb_size, Look::size, L"Rozmiar kafelków XMB", static_cast<int>(s.xmb_size), names(xmb_sizes, xmb_sizes.size()));
            toggle(Id::xmb_posters, L"Pionowe plakaty", s.xmb_posters);
        } else if (s.home_layout == HomeLayout::flix) {
            cards(Id::flix_size, Look::size, L"Rozmiar kafelków Flix", static_cast<int>(s.flix_size), names(flix_sizes, flix_sizes.size()));
            toggle(Id::flix_category_all, L"Kafelek „Wszystkie” w rzędach", s.flix_category_all);
            toggle(Id::flix_category_tabs, L"Pasek kategorii", s.flix_category_tabs);
            toggle(Id::flix_rail, L"Pasek boczny", s.flix_rail);
        } else if (s.home_layout == HomeLayout::metro) {
            toggle(Id::metro_category_tabs, L"Pasek kategorii", s.metro_category_tabs);
            if (!s.metro_category_tabs) toggle(Id::home_tab_icons, L"Ikony zamiast nazw", s.home_tab_icons);
            toggle(Id::metro_3d, L"Efekt 3D", s.metro_3d);
            toggle(Id::metro_light, L"Jasny motyw", s.metro_light);
        } else if (s.home_layout == HomeLayout::psfive) {
            cards(Id::psfive_size, Look::size, L"Rozmiar kafelków PSfive", static_cast<int>(s.psfive_size), names(xmb_sizes, xmb_sizes.size()));
            toggle(Id::psfive_clock, L"Zegar", s.psfive_clock);
            slider(Id::psfive_dim, L"Przyciemnienie tła", s.psfive_dim, percent(s.psfive_dim));
            toggle(Id::psfive_details, L"Szczegóły pod rzędem", s.psfive_details);
            toggle(Id::psfive_category_tabs, L"Pasek kategorii", s.psfive_category_tabs);
            if (!s.psfive_category_tabs) toggle(Id::home_tab_icons, L"Ikony zamiast nazw", s.home_tab_icons);
        }
        cards(Id::home_tiles, Look::place, L"Kafle Wszystkie, Glance, Tarot i Towarzyszka", static_cast<int>(s.home_tiles),
              {{tr(placeName(0))}, {tr(placeName(1))}, {tr(placeName(2))}});
        toggle(Id::add_game_tile, L"Kafel „Dodaj grę”", s.add_game_tile);
        break;
    }
    case theme_ui::Tab::wallpaper:
        if (!desktop_mode_) {
            action(Id::wallpaper_pick, L"Tapeta", wallpaperLabel());
            options.back().trash = s.wallpaper != AppSettings::WallpaperKind::none;
            if (s.wallpaper != AppSettings::WallpaperKind::none) {
                toggle(Id::wallpaper_tiles, L"Pod kategoriami i kaflami bez grafiki", s.wallpaper_tiles);
                toggle(Id::wallpaper_snitch, L"Na ekranie snitch", s.wallpaper_snitch);
                toggle(Id::wallpaper_menus, L"W Ustawieniach i menu", s.wallpaper_menus);
                toggle(Id::wallpaper_artwork, L"W „Zmień grafikę”", s.wallpaper_artwork);
            }
        }
        slider(Id::glass, L"Efekt szkła", s.glass, percent(s.glass));
        slider(Id::background_grid, L"Siatka na tle", s.background_grid, percent(s.background_grid));
        {
            std::vector<theme_ui::Card> patterns{{tr(L"Brak")}};
            for (const auto* name : grid_styles) patterns.push_back({tr(name)});
            cards(Id::background_grid_style, Look::grid, L"Wzór siatki tła wideo", s.background_grid <= 0.001f ? 0 : static_cast<int>(s.background_grid_style) + 1, std::move(patterns));
        }
        slider(Id::opacity, L"Krycie tła", s.drawer.opacity / 0.98f, percent(s.drawer.opacity));
        slider(Id::fade, L"Szerokość zanikania", (s.drawer.fade_fraction - 0.06f) / 0.69f, percent(s.drawer.fade_fraction));
        break;
    case theme_ui::Tab::tiles:
        cards(Id::tile_shape, Look::shape, L"Kształt kafelków", static_cast<int>(s.tile_shape), names(tile_shapes, tile_shapes.size()));
        cards(Id::tile_entrance, Look::entrance, L"Pojawianie się kafli", static_cast<int>(s.tile_entrance),
              {{tr(entranceName(0))}, {tr(entranceName(1))}, {tr(entranceName(2))}});
        cards(Id::focus_gap, Look::gap, L"Odstęp obrysu od grafiki", static_cast<int>(s.focus_gap), {{tr(gapName(0))}, {tr(gapName(1))}, {tr(gapName(2))}});
        slider(Id::glow_level, L"Poświata", s.glow_level / 2.0f, percent(s.glow_level));
        cards(Id::glow_motion, Look::motion, L"Animacja poświaty", s.glow_pulse ? 1 : s.glow_trace ? 2 : 0,
              {{tr(motionName(0))}, {tr(motionName(1))}, {tr(motionName(2))}});
        toggle(Id::focus_spring, L"Sprężysty obrys fokusu", s.focus_spring);
        toggle(Id::console_colors, L"Kolorowe kafelki konsol", s.console_colors);
        break;
    case theme_ui::Tab::colors:
        colors(Id::ui_accent, L"Kolor akcentu interfejsu", s.ui_accent_rgb, ui_accents, ui_accent_names);
        colors(Id::button_color, L"Kolor przycisków", s.button_rgb, button_colors, button_color_names);
        cards(Id::button_idle, Look::idle, L"Pozostałe przyciski", static_cast<int>(s.button_idle), {{tr(idleName(0))}, {tr(idleName(1))}, {tr(idleName(2))}});
        colors(Id::accent, L"Kolor akcentu okładek", s.cover_accent_rgb, accents, accent_names);
        cards(Id::hint_style, Look::hint, L"Wygląd podpowiedzi", static_cast<int>(s.hint_style), names(hint_styles, hint_styles.size()));
        break;
    case theme_ui::Tab::fonts: {
        std::vector<theme_ui::Card> list;
        int chosen = 0;
        for (size_t i = 0; i < fonts.size(); ++i) { list.push_back({fonts[i]}); if (s.font_family == fonts[i]) chosen = static_cast<int>(i); }
        cards(Id::font, Look::font, L"Czcionka", chosen, std::move(list));
        break;
    }
    case theme_ui::Tab::looks: break; // Its rows are Cofnij, Zapisz and the saved looks (theme_studio.hpp).
    case theme_ui::Tab::other:
        cards(Id::search_art, Look::art, L"Grafiki w wyszukiwarce", static_cast<int>(s.search_art), {{tr(artName(0))}, {tr(artName(1))}, {tr(artName(2))}});
        toggle(Id::panel_borders, L"Ramki okien", s.panel_borders);
        toggle(Id::screen_transitions, L"Animacje przejść ekranów", s.screen_transitions);
        toggle(Id::menu_dialog, L"Duże menu gry", s.menu_dialog);
        toggle(Id::launch_tv_mode, L"Uruchamiaj w trybie TV", s.launch_tv_mode);
        if (desktop_mode_) {
            toggle(Id::desktop_platform_badges, L"Ikona platformy na okładkach", s.desktop_platform_badges);
            toggle(Id::desktop_gray_uninstalled, L"Niezainstalowane w czerni i bieli", s.desktop_gray_uninstalled);
        }
        break;
    }
    for (auto& o : options) { // What is drawn glides from where the same option stood.
        o.on_shown = o.on ? 1.0f : 0.0f; o.level_shown = o.level;
        for (const auto& before : state.options)
            if (before.id == o.id && before.kind == o.kind) { o.on_shown = before.on_shown; o.level_shown = before.level_shown; break; }
    }
    {
        const auto view = renderer_.viewport();
        const theme_ui::Layout layout{view.width, view.height};
        for (auto& o : options) {
            o.strip_shown = layout.stripTarget(o);
            for (const auto& before : state.options)
                if (before.id == o.id && before.kind == o.kind) { o.strip_shown = before.strip_shown; break; }
        }
    }
    state.options = std::move(options);
    state.layout = static_cast<int>(settings_.home_layout);
    const int count = static_cast<int>(state.options.size());
    state.row = std::clamp(state.row, 0, std::max(0, count - 1));
    if (count && state.options[static_cast<size_t>(state.row)].kind == theme_ui::Kind::cards)
        state.card = state.options[static_cast<size_t>(state.row)].current;
}

void Launcher::themeEnsureVisible() {
    auto& state = theme_;
    const auto view = renderer_.viewport();
    const theme_ui::Layout layout{view.width, view.height};
    if (state.current() == theme_ui::Tab::looks) { // The focused row of Wyglądy.
        const float top = layout.lookTop(state.item), bottom = top + layout.row_h, room = layout.list().bottom - layout.list().top;
        if (top < state.scroll) state.scroll = top;
        else if (bottom > state.scroll + room) state.scroll = bottom - room;
        state.scroll = std::clamp(state.scroll, 0.0f, layout.scrollLimit(state));
        return;
    }
    if (state.options.empty()) { state.scroll = 0; return; }
    const int row = std::clamp(state.row, 0, static_cast<int>(state.options.size()) - 1);
    const float top = layout.optionTop(state.options, row), bottom = top + layout.optionHeight(state.options[static_cast<size_t>(row)]);
    const float room = layout.list().bottom - layout.list().top;
    if (top < state.scroll) state.scroll = top;
    else if (bottom > state.scroll + room) state.scroll = bottom - room;
    state.scroll = std::clamp(state.scroll, 0.0f, layout.maxScroll(state.options));
}

void Launcher::themeSetTab(int tab) {
    auto& state = theme_;
    tab = std::clamp(tab, 0, theme_ui::tab_count - 1);
    if (tab == state.tab) { navigationBoundary(); return; }
    state.tab = tab; state.tab_fade = 0; state.row = 0; state.card = 0; state.scroll = 0; state.shown_scroll = 0; state.bar = state.current() == theme_ui::Tab::looks; state.item = theme_ui::State::undo_item;
    state.naming = false; state.delete_armed = false; state.status.clear();
    themeBuildOptions();
    themeSyncExample();
    audio_.navigate();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

// A card chosen, a switch flipped, a slider moved: applied to the launcher, the options drawn again.
void Launcher::themeChooseCard(int option, int card) {
    auto& state = theme_;
    if (option < 0 || option >= static_cast<int>(state.options.size())) return;
    const auto& o = state.options[static_cast<size_t>(option)];
    if (o.kind != theme_ui::Kind::cards || card < 0 || card >= static_cast<int>(o.cards.size()) || card == o.current) return;
    const Id id = o.id;
    if (id == Id::home_layout) setHomeLayout(card);
    else if (id == Id::background_grid_style) { // Brak first: no grid; a pattern brings it back.
        if (card == 0) settings_.background_grid = 0;
        else {
            settings_.background_grid_style = static_cast<AppSettings::GridStyle>(std::clamp(card - 1, 0, 2));
            if (settings_.background_grid <= 0.001f) settings_.background_grid = 0.5f;
        }
        settingsChanged();
    }
    else if (id == Id::xmb_size || id == Id::psfive_size) {
        (id == Id::xmb_size ? settings_.xmb_size : settings_.psfive_size) = static_cast<AppSettings::XmbSize>(std::clamp(card, 0, 2));
        settingsChanged(true);
    } else if (id == Id::flix_size) {
        settings_.flix_size = card ? AppSettings::FlixSize::large : AppSettings::FlixSize::normal;
        settingsChanged(true);
    } else {
        // The rest go through the settings' own choice handler.
        Row row{id, Control::choice, o.title, {}};
        for (const auto& c : o.cards) row.choices.push_back({c.label, c.color});
        row.choice = card;
        const int keep = settings_state_.choice;
        settings_state_.choice = card;
        if (id == Id::font) row.choices[static_cast<size_t>(card)].label = fonts[static_cast<size_t>(card)];
        settingsChoose(row);
        settings_state_.choice = keep;
    }
    state.active = -1;
    if (state.current() == theme_ui::Tab::layout || id == Id::tile_shape) themeRefreshHome(id == Id::home_layout); // Only another layout goes through black.
    themeBuildOptions();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::themeToggle(int option) {
    auto& state = theme_;
    if (option < 0 || option >= static_cast<int>(state.options.size())) return;
    const auto& o = state.options[static_cast<size_t>(option)];
    if (o.kind != theme_ui::Kind::toggle) return;
    Row row{o.id, Control::toggle, o.title, {}};
    settingsToggle(row);
    state.active = -1;
    if (state.current() == theme_ui::Tab::layout) themeRefreshHome(false);
    themeBuildOptions();
    themeEnsureVisible(); // Options may appear or go.
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::themeSetLevel(int option, float level) {
    auto& state = theme_;
    if (option < 0 || option >= static_cast<int>(state.options.size())) return;
    const auto id = state.options[static_cast<size_t>(option)].id;
    level = std::clamp(level, 0.0f, 1.0f);
    switch (id) {
    case Id::glass: settings_.glass = std::round(level * 20) / 20.0f; break;
    case Id::psfive_dim: settings_.psfive_dim = std::round(level * 20) / 20.0f; break;
    case Id::background_grid: settings_.background_grid = std::round(level * 20) / 20.0f; break;
    case Id::glow_level: settings_.glow_level = std::round(level * 20) / 10.0f; break;
    case Id::opacity: settings_.drawer.opacity = std::round(level * 98) / 100.0f; break;
    case Id::fade: settings_.drawer.fade_fraction = (6 + std::round(level * 69)) / 100.0f; break;
    default: return;
    }
    settingsChanged();
    state.active = -1;
    themeBuildOptions();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::themeAction(int option) {
    auto& state = theme_;
    if (option < 0 || option >= static_cast<int>(state.options.size())) return;
    const auto id = state.options[static_cast<size_t>(option)].id;
    Row row{id, Control::action, {}, {}};
    if (id == Id::wallpaper_pick && !desktop_mode_) { // The picker is a screen of its own: the studio waits and comes back.
        themeTouch();
        state.resume = true; state.open = false;
        openWallpaperBrowser();
        if (screen_ != ScreenMode::artwork) { state.resume = false; state.open = true; }
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (id == Id::wallpaper_pick) {
        themeStudio(false);
        settingsExtraActivate(row);
        return;
    }
    settingsExtraActivate(row);
    state.active = -1;
    themeBuildOptions();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

// The appearance of `look` over the running launcher: layout through its own switch (it rebuilds the screen).
void Launcher::themeApplyLook(const AppSettings& look) {
    const auto layout = look.home_layout, previous = settings_.home_layout;
    theme_preset::copyLook(look, settings_);
    settings_.home_layout = previous;
    applyWallpaper();
    setHomeLayout(static_cast<int>(layout));
    settingsChanged(true);
    prefetchPosters();
    themeRefreshHome(layout != previous);
}

// Tapeta's bin: the wallpaper off, as the settings' "Bez tapety".
void Launcher::themeClearWallpaper() {
    auto& state = theme_;
    Row row{Id::wallpaper_clear, Control::action, {}, {}};
    settingsExtraActivate(row);
    state.active = -1; state.card = 0;
    themeBuildOptions();
    themeEnsureVisible();
    audio_.context();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

// The home behind the preview built again after a change of the layout or of what it shows (the settings do
// that on leaving). The preview comes back from black instead of the tiles flying about.
void Launcher::themeRefreshHome(bool black) {
    if (desktop_mode_ || categories_.empty()) return;
    buildShelves();
    themeApplyExample();
    if (black) theme_.scene_in = 0;
}

// What the preview shows suits the option in focus: a collection's row for "Kafelek Wszystkie", the end of the
// first row for the home's own tiles, the library for fonts, the tile without art for the wallpaper; the home otherwise.
int Launcher::themeWantedExample() const {
    const auto& state = theme_;
    if (state.current() == theme_ui::Tab::wallpaper) return 4; // The wallpaper shows behind a tile without art.
    if (state.bar || state.row < 0 || state.row >= static_cast<int>(state.options.size())) return state.current() == theme_ui::Tab::fonts ? 2 : 0;
    const auto id = state.options[static_cast<size_t>(state.row)].id;
    if (id == Id::shelves_category_all || id == Id::flix_category_all) return 1;
    if (id == Id::home_tiles || id == Id::add_game_tile) return 3;
    return state.current() == theme_ui::Tab::fonts ? 2 : 0;
}

void Launcher::themeSyncExample() {
    if (desktop_mode_ || !theme_.open) return;
    const int want = themeWantedExample();
    if (want == theme_.example) return;
    theme_.example = want;
    themeApplyExample();
}

void Launcher::themeApplyExample() {
    if (desktop_mode_ || categories_.empty()) return;
    dismissMenu();
    size_t row = 0;
    if (theme_.example == 1)
        for (size_t i = 1; i < categories_.size(); ++i)
            if (std::any_of(categories_[i].games.begin(), categories_[i].games.end(), [](const Game& game) { return isGame(game); })) { row = i; break; }
    if (theme_.example == 2 && !public_library_.empty()) { showCategory(0); openLibrary(); return; }
    showCategory(row);
    if (theme_.example == 3 && !cards_.empty()) focusCard(cards_.size() - 1);
    if (theme_.example == 4 && !cards_.empty()) { // Wszystkie gry has no art: the wallpaper (a picture or a video) is its background.
        size_t tile = cards_.size() - 1;
        for (size_t i = 0; i < cards_.size(); ++i) if (cards_[i].game.kind == GameKind::all) { tile = i; break; }
        focusCard(tile);
    }
    theme_.idle = 0; theme_.demoing = false;
}

void Launcher::themeRevert() {
    auto& state = theme_;
    if (theme_preset::sameLook(theme_backup_, settings_)) { state.status = tr(L"Nic do cofnięcia."); navigationBoundary(); InvalidateRect(hwnd_, nullptr, FALSE); return; }
    themeApplyLook(theme_backup_);
    themeBuildOptions();
    themeRefreshActive();
    state.status = tr(L"Przywrócono wygląd sprzed otwarcia.");
    audio_.context();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::themeBeginSave() {
    auto& state = theme_;
    state.naming = true;
    state.name = theme_ui::freshName(state.saved, tr(L"Wygląd"));
    state.status.clear();
    audio_.context();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::themeSaveLook() {
    auto& state = theme_;
    const auto name = theme_preset::cleanName(state.name);
    if (name.empty() || !theme_preset::save(name, settings_)) {
        state.status = tr(L"Nie udało się zapisać wyglądu.");
        navigationBoundary();
    } else {
        state.naming = false;
        state.saved = theme_preset::list();
        themeRefreshActive();
        state.item = theme_ui::State::first_saved + std::max(0, state.active);
        state.bar = true;
        state.status = tr(L"Zapisano: ") + name;
        audio_.context();
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::themeLoadLook(int index) {
    auto& state = theme_;
    if (index < 0 || index >= static_cast<int>(state.saved.size())) return;
    AppSettings look = settings_;
    if (!theme_preset::load(state.saved[static_cast<size_t>(index)], look)) { state.status = tr(L"Nie udało się wczytać wyglądu."); navigationBoundary(); return; }
    themeApplyLook(look);
    themeBuildOptions();
    themeEnsureVisible();
    state.active = index;
    state.status = tr(L"Wczytano: ") + state.saved[static_cast<size_t>(index)];
    audio_.context();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::themeDeleteLook(int index) {
    auto& state = theme_;
    if (index < 0 || index >= static_cast<int>(state.saved.size())) return;
    const auto name = state.saved[static_cast<size_t>(index)];
    if (!state.delete_armed) { // Twice: the first press only asks.
        state.delete_armed = true;
        state.item = theme_ui::State::first_saved + index; state.bar = true;
        state.status = tr(L"Usunąć „") + name + tr(L"”? Naciśnij jeszcze raz.");
        navigationBoundary();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    state.delete_armed = false;
    theme_preset::remove(name);
    state.saved = theme_preset::list();
    state.item = std::min(state.item, state.items() - 1);
    themeRefreshActive();
    state.status = tr(L"Usunięto: ") + name;
    audio_.context();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::themeActivateBar() {
    auto& state = theme_;
    if (state.item == theme_ui::State::undo_item) themeRevert();
    else if (state.item == theme_ui::State::save_item) themeBeginSave();
    else themeLoadLook(state.item - theme_ui::State::first_saved);
}

void Launcher::themeStudioKey(WPARAM key, bool repeated) {
    auto& state = theme_;
    if (!state.open) return;
    themeTouch();
    if (state.naming) {
        if (key == VK_ESCAPE) { state.naming = false; audio_.navigate(); }
        else if (key == VK_RETURN) { if (!repeated) themeSaveLook(); return; }
        else if (key == VK_BACK) { if (!state.name.empty()) state.name.pop_back(); }
        else return;
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (key != VK_DELETE) state.delete_armed = false;
    state.hover = {};
    const int count = static_cast<int>(state.options.size());
    const auto changed = [&] { themeSyncExample(); audio_.navigate(); InvalidateRect(hwnd_, nullptr, FALSE); };
    switch (key) {
    case VK_ESCAPE: case VK_BACK: if (!repeated) themeStudio(false); return;
    case VK_PRIOR: case 'Q': themeSetTab(state.tab - 1); return;
    case VK_NEXT: case 'E': themeSetTab(state.tab + 1); return;
    case VK_UP:
        if (state.bar) { // Wyglądy: back up the rows.
            if (state.item < theme_ui::State::first_saved) { navigationBoundary(); return; }
            state.item = state.item == theme_ui::State::first_saved ? theme_ui::State::save_item : state.item - 1;
            themeEnsureVisible(); changed();
        } else if (state.row > 0) {
            --state.row;
            const auto& o = state.options[static_cast<size_t>(state.row)];
            state.card = o.kind == theme_ui::Kind::cards ? o.current : 0;
            themeEnsureVisible(); changed();
        } else navigationBoundary();
        return;
    case VK_DOWN:
        if (state.bar) {
            if (state.item < theme_ui::State::first_saved && state.saved.empty()) { navigationBoundary(); return; }
            if (state.item < theme_ui::State::first_saved) state.item = theme_ui::State::first_saved;
            else if (state.item + 1 < state.items()) ++state.item;
            else { navigationBoundary(); return; }
            themeEnsureVisible(); changed();
        } else if (state.row + 1 < count) {
            ++state.row;
            const auto& o = state.options[static_cast<size_t>(state.row)];
            state.card = o.kind == theme_ui::Kind::cards ? o.current : 0;
            themeEnsureVisible(); changed();
        } else navigationBoundary(); // The preview is not part of the navigation: nothing below the last option.
        return;
    case VK_LEFT: case VK_RIGHT: {
        const int step = key == VK_LEFT ? -1 : 1;
        if (state.bar) {
            const int next = std::clamp(state.item + step, 0, theme_ui::State::save_item); // Cofnij and Zapisz side by side.
            if (state.item >= theme_ui::State::first_saved || next == state.item) { navigationBoundary(); return; }
            state.item = next; changed();
            return;
        }
        if (!count) return;
        const auto& o = state.options[static_cast<size_t>(state.row)];
        if (o.kind == theme_ui::Kind::cards) {
            const int next = std::clamp(o.current + step, 0, static_cast<int>(o.cards.size()) - 1);
            if (next == o.current) { navigationBoundary(); return; }
            state.card = next;
            themeChooseCard(state.row, next);
            audio_.navigate();
        } else if (o.kind == theme_ui::Kind::toggle) {
            if (o.on == (step > 0)) { navigationBoundary(); return; }
            themeToggle(state.row);
        } else if (o.kind == theme_ui::Kind::slider) {
            const float next = std::clamp(o.level + step * 0.05f, 0.0f, 1.0f);
            if (next == o.level) { navigationBoundary(); return; }
            themeSetLevel(state.row, next);
            audio_.navigate();
        } else if (o.kind == theme_ui::Kind::action && o.trash) { // The bin is left of the button.
            const int next = step < 0 ? 1 : 0;
            if (next == state.card) { navigationBoundary(); return; }
            state.card = next; changed();
        } else navigationBoundary();
        return;
    }
    case VK_RETURN: case VK_SPACE:
        if (repeated) return;
        if (state.bar) { themeActivateBar(); return; }
        if (count) {
            const auto kind = state.options[static_cast<size_t>(state.row)].kind;
            if (kind == theme_ui::Kind::toggle) { themeToggle(state.row); audio_.context(); }
            else if (kind == theme_ui::Kind::action) { if (state.card == 1 && state.options[static_cast<size_t>(state.row)].trash) themeClearWallpaper(); else themeAction(state.row); }
        }
        return;
    case VK_DELETE:
        if (!state.bar && count && state.options[static_cast<size_t>(state.row)].trash) { themeClearWallpaper(); return; }
        if (state.bar && state.item >= theme_ui::State::first_saved) themeDeleteLook(state.item - theme_ui::State::first_saved);
        return;
    case VK_HOME: case VK_END:
        if (!count || state.bar) return;
        state.row = key == VK_HOME ? 0 : count - 1;
        { const auto& o = state.options[static_cast<size_t>(state.row)]; state.card = o.kind == theme_ui::Kind::cards ? o.current : 0; }
        themeEnsureVisible(); changed();
        return;
    default: return;
    }
}

void Launcher::themeStudioChar(wchar_t ch) {
    auto& state = theme_;
    if (!state.open || !state.naming) return;
    if (ch < 32 || ch == 127 || state.name.size() >= 40 || std::wstring_view(L"<>:\"/\\|?*").find(ch) != std::wstring_view::npos) return;
    state.name.push_back(ch);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::themeStudioWheel(float amount) {
    auto& state = theme_;
    if (!state.open) return;
    themeTouch();
    const auto view = renderer_.viewport();
    const theme_ui::Layout layout{view.width, view.height};
    state.scroll = std::clamp(state.scroll + amount, 0.0f, layout.scrollLimit(state));
    InvalidateRect(hwnd_, nullptr, FALSE);
}

// A press: the thing under the pointer. Sliders follow the pointer until it is released (themeStudioDrag).
void Launcher::themeStudioClick(float x, float y) {
    auto& state = theme_;
    themeTouch();
    const auto view = renderer_.viewport();
    const theme_ui::Layout layout{view.width, view.height};
    const auto hit = layout.hit(state, x, y);
    state.pressed = hit;
    using H = theme_ui::Hit;
    if (hit.kind >= H::option && hit.kind <= H::trash) { state.bar = false; state.row = hit.index; themeSyncExample(); }
    switch (hit.kind) {
    case H::close: themeStudio(false); return;
    case H::tab: themeSetTab(hit.index); return;
    case H::card: state.bar = false; state.row = hit.index; state.card = hit.slot; themeChooseCard(hit.index, hit.slot); audio_.navigate(); return;
    case H::toggle: state.bar = false; state.row = hit.index; themeToggle(hit.index); audio_.context(); return;
    case H::minus: case H::plus: {
        state.bar = false; state.row = hit.index;
        themeSetLevel(hit.index, state.options[static_cast<size_t>(hit.index)].level + (hit.kind == H::plus ? 0.05f : -0.05f));
        audio_.navigate();
        return;
    }
    case H::track: state.bar = false; state.row = hit.index; state.dragging = true; themeSetLevel(hit.index, hit.level); return;
    case H::action: state.bar = false; state.row = hit.index; themeAction(hit.index); return;
    case H::trash: state.row = hit.index; themeClearWallpaper(); return;
    case H::revert: state.bar = true; state.item = theme_ui::State::undo_item; themeRevert(); return;
    case H::save: state.bar = true; state.item = theme_ui::State::save_item; themeBeginSave(); return;
    case H::chip: state.bar = true; state.item = theme_ui::State::first_saved + hit.index; themeLoadLook(hit.index); return;
    case H::chip_delete: themeDeleteLook(hit.index); return;
    case H::name_ok: themeSaveLook(); return;
    case H::name_cancel: state.naming = false; audio_.navigate(); InvalidateRect(hwnd_, nullptr, FALSE); return;
    default: return;
    }
}

void Launcher::themeStudioDrag(float x, float y) {
    auto& state = theme_;
    if (!state.open || !state.dragging) return;
    themeTouch();
    const auto view = renderer_.viewport();
    const theme_ui::Layout layout{view.width, view.height};
    if (state.row < 0 || state.row >= static_cast<int>(state.options.size())) return;
    const auto track = layout.track(layout.option(state.options, state.row, state.scroll));
    (void)y;
    themeSetLevel(state.row, (x - track.left) / (track.right - track.left));
}

void Launcher::themeStudioRelease() { theme_.dragging = false; theme_.pressed = {}; }

void Launcher::themeStudioHover(float x, float y) {
    auto& state = theme_;
    if (!state.open) return;
    themeTouch();
    const auto view = renderer_.viewport();
    const theme_ui::Layout layout{view.width, view.height};
    const auto hit = layout.hit(state, x, y);
    if (hit == state.hover) return;
    state.hover = hit;
    InvalidateRect(hwnd_, nullptr, FALSE);
}

bool Launcher::themeStudioHot(float x, float y) const {
    if (!theme_.open) return false;
    const auto view = renderer_.viewport();
    const theme_ui::Layout layout{view.width, view.height};
    const auto hit = layout.hit(theme_, x, y);
    return hit.kind != theme_ui::Hit::none && hit.kind != theme_ui::Hit::option && hit.kind != theme_ui::Hit::name_field;
}

// Per frame: the opening, the tab pill, the arriving options, the scroll, the switches and sliders; and the
// studio's return from the wallpaper picker. True while something moves (the real home behind it always does).
bool Launcher::themeAnimate(float dt) {
    auto& state = theme_;
    if (state.resume) {
        if (screen_ == ScreenMode::shelves) {
            state.resume = false; state.open = true; state.appear = 1;
            themeBuildOptions(); themeRefreshActive();
            return true;
        }
        if (screen_ != ScreenMode::artwork) { state = {}; saveSettings(); }
        return false;
    }
    if (!state.open) return false;
    const float k = 1.0f - std::exp(-dt * 14.0f);
    bool moving = false;
    const auto glide = [&](float& shown, float target, float rate) {
        if (std::abs(target - shown) < 0.002f) { shown = target; return; }
        shown += (target - shown) * rate; moving = true;
    };
    const auto rise = [&](float& value, float seconds) {
        if (value >= 1.0f) return;
        value = std::min(1.0f, value + dt / seconds); moving = true;
    };
    rise(state.appear, 0.34f);
    rise(state.tab_fade, 0.24f);
    rise(state.scene_in, 0.5f);
    glide(state.tab_pos, static_cast<float>(state.tab), k);
    glide(state.shown_scroll, state.scroll, k);
    const auto view = renderer_.viewport();
    const theme_ui::Layout layout{view.width, view.height};
    for (auto& o : state.options) {
        if (layout.strip(o)) glide(o.strip_shown, layout.stripTarget(o), k);
        glide(o.on_shown, o.on ? 1.0f : 0.0f, k);
        glide(o.level_shown, o.level, std::min(1.0f, k * 1.6f));
    }
    if (!desktop_mode_ && !state.naming) { // Quiet for 1.5 s: the preview walks along the tiles by itself.
        state.idle += dt;
        if (state.idle >= 1.5f) {
            if (!state.demoing) { state.demoing = true; state.demo_origin = selected_; state.demo_clock = 1.2f; state.demo_dir = 1; }
            state.demo_clock += dt;
            if (state.demo_clock >= 1.7f) { state.demo_clock = 0; themeDemoStep(); }
        }
    }
    return moving || !desktop_mode_; // The live scene is always in motion.
}

void Launcher::themeTouch() {
    auto& state = theme_;
    state.idle = 0;
    if (!state.demoing) return;
    state.demoing = false;
    if (!desktop_mode_ && screen_ == ScreenMode::shelves && !cards_.empty()) focusCard(std::min(state.demo_origin, cards_.size() - 1));
}

// One step of the demo: along one row of the layout (a Flix row, a Metro page, XMB's tile row, the shelf), a few
// tiles to the right and back. Silent, and never into another row or page: that would scroll the whole layout.
void Launcher::themeDemoStep() {
    auto& state = theme_;
    if (cards_.size() < 2 || screen_ != ScreenMode::shelves) return;
    std::vector<size_t> track;
    if (flixActive()) {
        if (flix_row_ >= flix_rows_.size()) return;
        const auto& row = flix_rows_[flix_row_];
        for (size_t i = 0; i < row.count && row.begin + i < cards_.size(); ++i) track.push_back(row.begin + i);
    } else if (metroActive()) {
        if (selected_ >= metro_tiles_.size() || metro_tiles_.size() != cards_.size()) return;
        const auto& page = metro_pages_[metro_tiles_[selected_].page];
        for (size_t i = page.begin; i < page.begin + page.count && i < cards_.size(); ++i) track.push_back(i);
        std::stable_sort(track.begin(), track.end(), [&](size_t a, size_t b) {
            const auto &x = metro_tiles_[a].rect, &y = metro_tiles_[b].rect;
            return x.left != y.left ? x.left < y.left : x.top < y.top;
        });
    } else if (xmbActive()) {
        for (size_t i = 0; i < xmb_top_.size() && i < cards_.size(); ++i) track.push_back(i);
    } else {
        for (size_t i = 0; i < cards_.size(); ++i) track.push_back(i);
    }
    const auto here = std::find(track.begin(), track.end(), selected_);
    if (here == track.end() || track.size() < 2) return;
    const auto origin = std::find(track.begin(), track.end(), state.demo_origin);
    const int at = static_cast<int>(here - track.begin());
    const int low = origin == track.end() ? 0 : static_cast<int>(origin - track.begin());
    const int high = std::min(static_cast<int>(track.size()) - 1, low + 5);
    int next = at + state.demo_dir;
    if (next > high || next < low) { state.demo_dir = -state.demo_dir; next = at + state.demo_dir; }
    next = std::clamp(next, 0, static_cast<int>(track.size()) - 1);
    if (next != at) focusCard(track[static_cast<size_t>(next)]);
    InvalidateRect(hwnd_, nullptr, FALSE);
}
