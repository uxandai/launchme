// Settings row definitions and presentation; interaction and persistence live in settings.cpp.
// Settings 2.0: every category is split into titled sections (Row::section on a section's first row).
#include "launcher.hpp"
#include "../platform/etw_access.hpp"
#include "../platform/flac.hpp"
#include "../platform/music_themes.hpp"
#include "../platform/sound_cues.hpp"
#include "../platform/text_encoding.hpp"
#include "../library/screenscraper_config.hpp"
#include "../library/search.hpp"
#include "../artwork/artwork_sources.hpp"
#include <algorithm>
#include <cmath>

using namespace settings_ui;
namespace {
std::wstring percent(float value) { return std::to_wstring(static_cast<int>(std::round(value * 100))) + L"%"; }
// An excluded game's store, from its key ("steam:620", "drmfree:…", "shortcut:…" an emulated or Steam shortcut).
std::wstring excludedSourceName(std::string_view key) {
    const auto slug = key.substr(0, key.find(':'));
    if (slug == "shortcut") return tr(L"Emulacja lub skrót Steam");
    for (const auto source : {GameSource::steam, GameSource::gog, GameSource::epic, GameSource::battlenet, GameSource::ea, GameSource::ubisoft,
                              GameSource::amazon, GameSource::xbox, GameSource::multimc, GameSource::drmfree})
        if (slug == sourceSlug(source)) return sourceName(source);
    return {};
}
}

// Which page of Settings the rows describe: a rebuild of the same page keeps the focused row by
// its identity, so rows appearing or disappearing above it never move the focus.
std::string Launcher::settingsPage() const {
    const auto& state = settings_state_;
    std::string page = std::to_string(state.category);
    for (const auto& frame : state.pages)
        page += '|' + std::to_string(static_cast<int>(frame.page)) + ':' + frame.key + ':' + std::to_string(frame.value);
    return page;
}

// The title of the open page (its tile's or plugin's name); empty on a category's own list.
std::wstring Launcher::settingsPageTitle() const { return settingsPageTitle(settings_state_.top()); }

// The way back under the heading: the page this one opened from (Emulacja under a system's page);
// empty on a category's first page (the category's name).
std::wstring Launcher::settingsParentTitle() const {
    const auto& pages = settings_state_.pages;
    return pages.size() >= 2 ? settingsPageTitle(&pages[pages.size() - 2]) : std::wstring{};
}

std::wstring Launcher::settingsPageTitle(const settings_ui::PageFrame* frame) const {
    if (!frame) return {};
    switch (frame->page) {
    case Page::plugin: if (const auto* plugin = plugins_.find(frame->key)) return plugin->manifest.name.localized(); return {};
    case Page::platform: {
        const auto source = static_cast<GameSource>(frame->value);
        return source != GameSource::drmfree ? std::wstring(sourceName(source)) : std::wstring(tr(L"Katalog DRM-free"));
    }
    case Page::drm_games: return tr(L"Znalezione gry");
    case Page::screenscraper: return L"ScreenScraper";
    case Page::custom_games: return tr(L"Gry dodane ręcznie");
    case Page::collections: return tr(L"Kolekcje");
    case Page::hidden_studios: return tr(L"Ukryci wydawcy i developerzy");
    case Page::excluded_games: return tr(L"Wykluczone gry");
    case Page::layout_options: return tr(L"Opcje układu: ") + std::wstring(tr(home_layouts[std::clamp(static_cast<int>(settings_.home_layout), 0, home_layout_count - 1)]));
    case Page::cover_focus: return tr(L"Zaznaczona okładka");
    case Page::backdrop: return tr(L"Tło i szkło");
    case Page::videos: return tr(L"Filmy w tle");
    case Page::service_keys: return tr(L"Klucze usług");
    case Page::sounds: return tr(L"Dźwięki interfejsu");
    case Page::screensaver: return tr(L"Wygaszacz ekranu");
    case Page::companion_model: return tr(L"Model AI");
    case Page::companion_voice: return tr(L"Głos (ElevenLabs)");
    case Page::companion_remote: return tr(L"Dostęp z telefonu");
    case Page::compat: return tr(L"Proton i Wine");
    case Page::compat_source: {
#ifndef _WIN32
        if (const auto* source = compat_tools::findSource(frame->key)) return source->name;
#endif
        return tr(L"Pobierz");
    }
    case Page::prefixes: return tr(L"Prefiksy");
    case Page::compat_stores: return tr(L"Sklepy i launchery");
    case Page::game_shortcuts: return tr(L"Skróty: ") + shortcuts_game_.name;
    case Page::artwork_sources: return tr(L"Źródła grafik");
    case Page::emulation: return tr(L"Emulacja");
    case Page::drm_roots: return tr(L"Katalogi");
    case Page::emulation_system: return tr(L"Emulacja: ") + emulationSystemTitle(text_encoding::wideLenient(frame->key));
    case Page::prefix: {
#ifndef _WIN32
        const auto path = std::filesystem::path(text_encoding::wideLenient(frame->key)).lexically_normal();
        for (const auto& entry : compat_.prefixes) if (entry.path == path) return compatPrefixName(entry);
        return path.filename().wstring();
#else
        return tr(L"Prefiks");
#endif
    }
    case Page::launch_options:
#ifndef _WIN32
        if (!frame->key.empty()) return L"Proton / Wine: " + compat_.game.name;
#endif
        return tr(L"Domyślne opcje uruchamiania");
    }
    return {};
}

void Launcher::buildSettingsRows() {
    if (settings_state_.pages.empty()) settings_page_exits_ = false; // Left for a category: Back is Settings' own again.
    // Rows of one kind (DRM-free folders and games, collections) are told apart by their key.
    std::optional<RowRef> selected_row;
    const auto page = settingsPage();
    if (page == settings_rows_page_ && !settings_rows_.empty() && settings_state_.row() < static_cast<int>(settings_rows_.size()))
        selected_row = RowRef::of(settings_rows_[settings_state_.row()]);
    // Back from a page: the row it was opened from, wherever it is now.
    bool reopened = false;
    if (!selected_row && settings_state_.reopen) { selected_row = std::move(settings_state_.reopen); reopened = true; }
    settings_state_.reopen.reset();
    settings_rows_page_ = page;
    settings_rows_.clear();
    // The next row pushed starts a section with this heading (Row::section, Layout::sections).
    std::wstring next_section;
    const auto section = [&](const wchar_t* title) { next_section = title; };
    const auto add = [&](Row row) {
        if (!next_section.empty()) { row.section = std::move(next_section); next_section.clear(); }
        settings_rows_.push_back(std::move(row));
    };
    const auto choiceRow = [&](Id id, const wchar_t* title, const wchar_t* description, const auto& names, int current) {
        Row row{id, Control::choice, title, description};
        for (const auto* name : names) row.choices.push_back({tr(name)});
        row.choice = std::clamp(current, 0, static_cast<int>(row.choices.size()) - 1);
        row.value = row.choices[row.choice].label;
        add(std::move(row));
    };
    // A tile opening a page of related settings: what is on it, and a short summary on the right.
    const auto pageRow = [&](Page target, const wchar_t* title, std::wstring description, std::wstring value) {
        Row row{Id::page, Control::page, title, std::move(description), std::move(value)};
        row.page = target;
        row.key = std::to_string(static_cast<int>(target));
        add(std::move(row));
    };
    switch (static_cast<settings_ui::Category>(settings_state_.category)) {
    case settings_ui::Category::appearance: {
        const auto accentRow = [&](Id id, const wchar_t* title, const wchar_t* description, uint32_t current,
                                   const auto& colors, const auto& names) {
            Row accent{id, Control::choice, title, description};
            accent.choice = -1;
            for (int i = 0; i < static_cast<int>(colors.size()); ++i) {
                accent.choices.push_back({tr(names[i]), colors[i]});
                if (current == colors[i]) accent.choice = i;
            }
            if (accent.choice < 0) {
                wchar_t custom[64]{};
                swprintf_s(custom, L"%ls · #%06X", tr(L"Własny"), current);
                accent.choice = static_cast<int>(accent.choices.size());
                accent.choices.push_back({custom, current});
            }
            accent.value = accent.choices[accent.choice].label;
            // Last: any colour through the HSV editor, starting from the current one.
            accent.choices.push_back({tr(L"Inny…"), current, true});
            add(std::move(accent));
        };
        using HomeLayout = AppSettings::HomeLayout;
        if (settings_state_.at(Page::layout_options)) {
            // The chosen Tryb TV layout's own options.
            if (settings_.home_layout == HomeLayout::shelves) {
                add({Id::show_categories, Control::toggle, tr(L"Kategorie zawsze widoczne"),
                    tr(L"Stała lista po lewej stronie zamiast wysuwanej szuflady."), {}, settings_.show_categories});
                add({Id::shelves_posters, Control::toggle, tr(L"Pionowe plakaty"),
                    tr(L"Plakaty gier zamiast kwadratowych okładek na półkach. Sklepy i konsole zostają kwadratowe."), {}, settings_.shelves_posters});
                add({Id::shelves_category_all, Control::toggle, tr(L"Kafelek „Wszystkie” w rzędach"),
                    tr(L"Pierwszy na każdej półce z grami; otwiera bibliotekę jej kategorii."), {}, settings_.shelves_category_all});
                add({Id::shelves_hero_facts, Control::toggle, tr(L"Informacje pod logo"),
                    tr(L"Czas gry, ostatnie uruchomienie, data wydania i platforma wyśrodkowane pod logo zaznaczonej gry."), {}, settings_.shelves_hero_facts});
            }
            if (settings_.home_layout == HomeLayout::xmb) {
                add({Id::xmb_posters, Control::toggle, tr(L"Pionowe plakaty"),
                    tr(L"Plakaty gier zamiast kwadratowych okładek w układzie XMB."), {}, settings_.xmb_posters});
                const int size = std::clamp(static_cast<int>(settings_.xmb_size), 0, 2);
                add({Id::xmb_size, Control::slider, tr(L"Rozmiar kafelków XMB"),
                    tr(L"Mały, domyślny lub duży — dla okładek i plakatów."), tr(xmb_sizes[size]), false, size / 2.0f});
            }
            if (settings_.home_layout == HomeLayout::flix) {
                const int size = std::clamp(static_cast<int>(settings_.flix_size), 0, 1);
                add({Id::flix_size, Control::slider, tr(L"Rozmiar kafelków Flix"),
                    tr(L"Domyślny lub duży — dla plakatów i kafelków ostatnio granych."), tr(flix_sizes[size]), false, static_cast<float>(size)});
                add({Id::flix_category_all, Control::toggle, tr(L"Kafelek „Wszystkie” w rzędach"),
                    tr(L"Pierwszy w każdym rzędzie gier, z losowym tłem kategorii; otwiera jej bibliotekę."), {}, settings_.flix_category_all});
                add({Id::flix_top_icons, Control::toggle, tr(L"Ikony menu w prawym górnym rogu"),
                    tr(L"Wszystkie ikony dolnego menu, małe, obok znaku platformy. Towarzyszka jako awatar, Glance jako ikona."), {}, settings_.flix_top_icons});
                add({Id::flix_category_tabs, Control::toggle, tr(L"Pasek kategorii"),
                    tr(L"Kategorie jak w bibliotece, na środku u góry; zmieniają je LB/RB albo Page Up/Down."), {}, settings_.flix_category_tabs});
                add({Id::flix_rail, Control::toggle, tr(L"Pasek boczny"),
                    tr(L"Ikony po lewej, jak w serwisach z filmami: start, kategorie (przewijają do swojego rzędu), szukaj i wszystkie gry; na dole profil, menu i widżety."),
                    {}, settings_.flix_rail});
            }
            if (settings_.home_layout == HomeLayout::metro) {
                add({Id::metro_category_tabs, Control::toggle, tr(L"Pasek kategorii"),
                    tr(L"Kategorie jak w bibliotece, na środku u góry, zamiast zakładek; zmieniają je LB/RB albo Page Up/Down."), {}, settings_.metro_category_tabs});
                if (!settings_.metro_category_tabs)
                    add({Id::home_tab_icons, Control::toggle, tr(L"Ikony zamiast nazw"),
                        tr(L"Zakładki Platformy PC, Emulacja, Wydawcy, Developerzy i Ulubione jako ikony; inne kolekcje zostają z nazwą."), {}, settings_.home_tab_icons});
                add({Id::metro_3d, Control::toggle, tr(L"Efekt 3D"),
                    tr(L"Strony obok bieżącej są mniejsze (80%) i powiększają się po przejściu na nie."), {}, settings_.metro_3d});
                add({Id::metro_light, Control::toggle, tr(L"Jasny motyw"),
                    tr(L"Białe tło i ciemne napisy, jak jasny motyw Windows Phone. Kafle zostają kolorowe."), {}, settings_.metro_light});
            }
            if (settings_.home_layout == HomeLayout::psfive) {
                const int size = std::clamp(static_cast<int>(settings_.psfive_size), 0, 2);
                add({Id::psfive_size, Control::slider, tr(L"Rozmiar kafelków PSfive"),
                    tr(L"Mały, domyślny lub duży — dla kafelków w rzędzie."), tr(xmb_sizes[size]), false, size / 2.0f});
                add({Id::psfive_clock, Control::toggle, tr(L"Zegar"),
                    tr(L"Godzina w prawym górnym rogu, obok ikon szukania i menu."), {}, settings_.psfive_clock});
                add({Id::psfive_details, Control::toggle, tr(L"Szczegóły pod rzędem"),
                    tr(L"W dół z przycisków Graj / …: opis, informacje, zrzuty ekranu i podobne gry. Menu otwiera się wtedy ruchem w górę nad zakładkami."),
                    {}, settings_.psfive_details});
                add({Id::psfive_category_tabs, Control::toggle, tr(L"Pasek kategorii"),
                    tr(L"Kategorie jak w bibliotece, na środku u góry, zamiast zakładek; zmieniają je LB/RB albo Page Up/Down."), {}, settings_.psfive_category_tabs});
                if (!settings_.psfive_category_tabs)
                    add({Id::home_tab_icons, Control::toggle, tr(L"Ikony zamiast nazw"),
                        tr(L"Zakładki Platformy PC, Emulacja, Wydawcy, Developerzy i Ulubione jako ikony; inne kolekcje zostają z nazwą."), {}, settings_.home_tab_icons});
            }
            break;
        }
        if (settings_state_.at(Page::cover_focus)) {
            // The focused cover: its accent, the glow around it and how the focus moves.
            accentRow(Id::accent, tr(L"Kolor akcentu okładek"), tr(L"Poświata i obramowanie zaznaczonej okładki."),
                      settings_.cover_accent_rgb, accents, accent_names);
            add({Id::glow_level, Control::slider, tr(L"Poświata"),
                tr(L"Blask wokół obrysu zaznaczonej okładki. 0%: sam obrys."), percent(settings_.glow_level), false,
                std::clamp(settings_.glow_level, 0.0f, 2.0f) / 2.0f});
            static constexpr std::array<const wchar_t*, 3> motions{trKey(L"Bez animacji"), trKey(L"Pulsowanie"), trKey(L"Wędrujący obrys")};
            choiceRow(Id::glow_motion, tr(L"Animacja poświaty"),
                      tr(L"Pulsowanie: blask rozjaśnia się i przygasa. Wędrujący obrys: jaśniejszy, grubszy fragment obiega ramkę."),
                      motions, settings_.glow_pulse ? 1 : settings_.glow_trace ? 2 : 0);
            add({Id::focus_spring, Control::toggle, tr(L"Sprężysty obrys fokusu"),
                tr(L"Obrys przeskakuje osobno od okładki i świeci od strony krawędzi listy, jak w PS5."), {}, settings_.focus_spring});
            choiceRow(Id::focus_gap, tr(L"Odstęp obrysu od grafiki"),
                      tr(L"Pusty pasek między obrysem a grafiką: obrys odsunięty na zewnątrz o swoją grubość albo grafika zmniejszona do środka ramki."),
                      focus_gaps, static_cast<int>(settings_.focus_gap));
            break;
        }
        if (settings_state_.at(Page::backdrop)) {
            // The wallpaper (src/app/wallpaper.cpp): which one, and where it shows.
            if (!desktop_mode_) section(tr(L"Tapeta"));
            if (!desktop_mode_) add({Id::wallpaper_pick, Control::action, tr(L"Tapeta"), wallpaperDescription(), wallpaperLabel()});
            if (!desktop_mode_ && settings_.wallpaper != AppSettings::WallpaperKind::none) {
                add({Id::wallpaper_clear, Control::action, tr(L"Bez tapety"),
                    tr(L"Wraca animowane tło launchera z kropkami krążącymi po liniach."), tr(L"Usuń")});
                add({Id::wallpaper_tiles, Control::toggle, tr(L"Pod kategoriami i kaflami bez grafiki"),
                    tr(L"Ekran główny: kategorie XMB, Wszystkie, Glance, Towarzyszka i gry bez tła."), {}, settings_.wallpaper_tiles});
                add({Id::wallpaper_snitch, Control::toggle, tr(L"Na ekranie snitch"),
                    tr(L"Zamiast jednolitego szarego tła, lekko przyciemniona."), {}, settings_.wallpaper_snitch});
                add({Id::wallpaper_menus, Control::toggle, tr(L"W Ustawieniach i menu"),
                    tr(L"Pod rozmytym szkłem Ustawień, Pobierania, Urządzeń, Glance i czatu."), {}, settings_.wallpaper_menus});
                add({Id::wallpaper_artwork, Control::toggle, tr(L"W „Zmień grafikę”"),
                    tr(L"Przyciemniona za galerią zamiast czarnego tła."), {}, settings_.wallpaper_artwork});
            }
            section(tr(L"Szkło i siatka"));
            // Glass under panels, the grid over background videos and the darkening under drawers.
            add({Id::glass, Control::slider, tr(L"Efekt szkła"),
                tr(L"Rozmycie pod oknami, menu, szufladami i Ustawieniami. 0%: pełne panele."), percent(settings_.glass), false,
                std::clamp(settings_.glass, 0.0f, 1.0f)});
            add({Id::background_grid, Control::slider, tr(L"Siatka na tle"),
                tr(L"Drobne czarne kropki na wideo w tle: ukrywają niską rozdzielczość zwiastunów. 0%: wyłączona."),
                percent(settings_.background_grid), false, std::clamp(settings_.background_grid, 0.0f, 1.0f)});
            choiceRow(Id::background_grid_style, tr(L"Wzór siatki"),
                      tr(L"Kropki jak ekran LED, linie poziome jak kineskop albo kratka wokół każdego punktu."),
                      grid_styles, static_cast<int>(settings_.background_grid_style));
            add({Id::opacity, Control::slider, tr(L"Krycie tła"),
                tr(L"Czarne tło pod listą znajomych i kolekcjami."), percent(settings_.drawer.opacity), false,
                settings_.drawer.opacity / 0.98f});
            add({Id::fade, Control::slider, tr(L"Szerokość zanikania"),
                tr(L"Jak daleko przyciemnienie sięga od krawędzi panelu."), percent(settings_.drawer.fade_fraction), false,
                (settings_.drawer.fade_fraction - 0.06f) / 0.69f});
            break;
        }
        // In the desktop window its own options come first; the console layouts (Tryb TV) follow.
        if (desktop_mode_) {
            section(tr(L"Okno desktopowe"));
            add({Id::desktop_platform_badges, Control::toggle, tr(L"Ikona platformy na okładkach"),
                tr(L"Mały znak sklepu lub konsoli w prawym górnym rogu każdej okładki."), {}, settings_.desktop_platform_badges});
            add({Id::desktop_gray_uninstalled, Control::toggle, tr(L"Niezainstalowane w czerni i bieli"),
                tr(L"Okładki gier, których nie ma na dysku, bez kolorów - zainstalowane od razu się wyróżniają."), {}, settings_.desktop_gray_uninstalled});
        }
        // The console layouts are Tryb TV; the desktop window is the other mode (src/app/desktop*.cpp).
        section(tr(L"Tryb TV"));
        Row layout{Id::home_layout, Control::layout, tr(L"Układ trybu TV"), tr(L"Półki, XMB, Flix, Metro, PSfive lub snitch z rzędem okładek i białymi ikonami.")};
        layout.choice = std::clamp(static_cast<int>(settings_.home_layout), 0, static_cast<int>(home_layouts.size()) - 1);
        layout.value = tr(home_layouts[layout.choice]);
        const auto layout_name = layout.value;
        add(std::move(layout));
        {
            // The chosen layout's own options on their page (snitch has none).
            const wchar_t* options = settings_.home_layout == HomeLayout::shelves ? trKey(L"Stała lista kategorii, pionowe plakaty, kafelek „Wszystkie” i informacje pod logo.") :
                                     settings_.home_layout == HomeLayout::xmb ? trKey(L"Pionowe plakaty i rozmiar kafelków.") :
                                     settings_.home_layout == HomeLayout::flix ? trKey(L"Rozmiar kafelków, kafelek „Wszystkie” i ikony menu u góry.") :
                                     settings_.home_layout == HomeLayout::metro ? trKey(L"Efekt 3D stron obok bieżącej.") :
                                     settings_.home_layout == HomeLayout::psfive ? trKey(L"Rozmiar kafelków i zegar.") : nullptr;
            if (options) pageRow(Page::layout_options, tr(L"Opcje układu"), tr(options), layout_name);
        }
        add({Id::launch_tv_mode, Control::toggle, tr(L"Uruchamiaj w trybie TV"),
            tr(L"Po starcie pełny ekran z układem powyżej zamiast okna desktopowego. Przełączysz też ikoną telewizora lub w menu zasilania."),
            {}, settings_.launch_tv_mode});
        choiceRow(Id::search_art, tr(L"Grafiki w wyszukiwarce"),
                  tr(L"Wyniki wyszukiwania jako kwadratowe okładki, pionowe plakaty albo poziome kafle."),
                  search_arts, static_cast<int>(settings_.search_art));
        choiceRow(Id::home_tiles, tr(L"Kafle Wszystkie, Glance, Tarot i Towarzyszka"),
                  tr(L"W każdym układzie jako kafle pierwszego rzędu: na jego początku albo końcu. Jak w układzie: pływające skróty jak dotąd."),
                  home_tile_places, static_cast<int>(settings_.home_tiles));
        add({Id::add_game_tile, Control::toggle, tr(L"Kafel „Dodaj grę”"),
            tr(L"Kafel z plusem w pierwszym rzędzie: dodasz grę spoza sklepów prosto z trybu TV."), {}, settings_.add_game_tile});
        section(tr(L"Wygląd"));
        if (!desktop_mode_) add({Id::wallpaper_pick, Control::action, tr(L"Tapeta"), wallpaperDescription(), wallpaperLabel()});
        accentRow(Id::ui_accent, tr(L"Kolor akcentu interfejsu"), tr(L"Przełączniki, suwaki, zaznaczenia i powiadomienia."),
                  settings_.ui_accent_rgb, ui_accents, ui_accent_names);
        accentRow(Id::button_color, tr(L"Kolor przycisków"),
                  tr(L"Wypełnienie zaznaczonego przycisku. Kolor napisów dobiera się sam, żeby zachować kontrast."),
                  settings_.button_rgb, button_colors, button_color_names);
        choiceRow(Id::button_idle, tr(L"Pozostałe przyciski"), tr(L"Tło przycisków bez zaznaczenia: szare szkło, odcień koloru przycisku albo ciemne."),
                  button_idles, static_cast<int>(settings_.button_idle));
        {
            Row font{Id::font, Control::choice, tr(L"Czcionka"), tr(L"Krój pisma w całym interfejsie."), settings_.font_family};
            for (int i = 0; i < static_cast<int>(fonts.size()); ++i) {
                font.choices.push_back({fonts[i]});
                if (settings_.font_family == fonts[i]) font.choice = i;
            }
            add(std::move(font));
        }
        choiceRow(Id::tile_shape, tr(L"Kształt kafelków"), tr(L"Okładki i kafelki w bibliotece, na ekranie głównym i w szufladzie."),
                  tile_shapes, static_cast<int>(settings_.tile_shape));
        {
            const auto found = std::find(accents.begin(), accents.end(), settings_.cover_accent_rgb);
            wchar_t custom[16]{};
            swprintf_s(custom, L"#%06X", settings_.cover_accent_rgb);
            pageRow(Page::cover_focus, tr(L"Zaznaczona okładka"), tr(L"Kolor akcentu, poświata i jej animacja, sprężysty obrys fokusu."),
                    found != accents.end() ? std::wstring(tr(accent_names[found - accents.begin()])) : std::wstring(custom));
        }
        pageRow(Page::backdrop, tr(L"Tło i szkło"), tr(L"Gdzie widać tapetę, efekt szkła, siatka na wideo w tle i przyciemnienie pod panelami."),
                tr(L"Szkło ") + percent(settings_.glass));
        add({Id::console_colors, Control::toggle, tr(L"Kolorowe kafelki konsol"),
            tr(L"Kolor platformy pod logo w rzędzie Emulacja. Wyłączone: ciemne tło."), {}, settings_.console_colors});
        section(tr(L"Okna i podpowiedzi"));
        add({Id::panel_borders, Control::toggle, tr(L"Ramki okien"),
            tr(L"Cienka ramka wokół okien dialogowych, menu kontekstowych i przewodnika."), {}, settings_.panel_borders});
        add({Id::menu_dialog, Control::toggle, tr(L"Duże menu gry"),
            tr(L"Menu gry (ekran główny, Szczegóły, biblioteki) jako duże przewijane okno z ikoną przy każdej opcji."), {}, settings_.menu_dialog});
        add({Id::screen_transitions, Control::toggle, tr(L"Animacje przejść ekranów"),
            tr(L"Płynne pojawianie się z powiększeniem i lekkim odgięciem brzegów."), {}, settings_.screen_transitions});
        choiceRow(Id::tile_entrance, tr(L"Pojawianie się kafli"),
                  tr(L"Na nowym ekranie trybu TV kafle powiększają się po kolei jak w PS5, wysuwają się z dołu albo są od razu."),
                  tile_entrances, static_cast<int>(settings_.tile_entrance));
        {
            // Off, or on with the symbols of a pad family (automatically: the last used pad's).
            Row hints{Id::input_hints, Control::choice, tr(L"Podpowiedzi przycisków"),
                tr(L"W prawym dolnym rogu: przyciski pada po jego użyciu, klawisze po klawiaturze. Automatycznie: symbole aktywnego pada.")};
            hints.choices.push_back({tr(L"Wyłączone")});
            for (const auto* style : hint_glyph_styles) hints.choices.push_back({tr(style)});
            hints.choice = settings_.input_hints ? 1 + std::clamp(static_cast<int>(settings_.hint_glyphs), 0, static_cast<int>(hint_glyph_styles.size()) - 1) : 0;
            hints.value = hints.choices[hints.choice].label;
            add(std::move(hints));
        }
        choiceRow(Id::hint_style, tr(L"Wygląd podpowiedzi"),
                  tr(L"Klawisze i przyciski pada w podpowiedziach, także przy paskach kategorii: szklane albo białe z ciemnym napisem."),
                  hint_styles, static_cast<int>(settings_.hint_style));
        section(tr(L"Widoki biblioteki w trybie TV"));
        {
            // A console's, a collection's and a publisher's library without a view of its own; its badge
            // (↺ Zresetuj, Delete, X) gives every one of the kind this view again (their own view, order and filter go).
            const auto viewRow = [&](Id id, const wchar_t* title, const wchar_t* description, int saved, const char* kind) {
                Row row{id, Control::choice, title, description};
                for (const auto view : library_view::drawer_order) row.choices.push_back({library_view::label(view)});
                row.choice = library_view::drawerRow(library_view::viewFromPersisted(std::clamp(saved, 0, library_view::persisted_view_count - 1)));
                row.value = row.choices[row.choice].label;
                if (const int own = libraryViewOverrides(kind)) row.description += L" · " + (tr(L"Z własnym widokiem: ") + std::to_wstring(own));
                row.badge = tr(L"Zresetuj"); row.reset = std::string("views:") + kind;
                add(std::move(row));
            };
            viewRow(Id::default_view_console, tr(L"Domyślny widok emulatorów"),
                    tr(L"Biblioteki konsol bez własnego widoku. Zresetuj: wszystkie wracają do niego."), settings_.default_view_console, "console");
            viewRow(Id::default_view_collection, tr(L"Domyślny widok kolekcji"),
                    tr(L"Kolekcje Steam i własne bez własnego widoku. Zresetuj: wszystkie wracają do niego."), settings_.default_view_collection, "collection");
            viewRow(Id::default_view_studio, tr(L"Domyślny widok wydawców"),
                    tr(L"Wydawcy i developerzy bez własnego widoku. Zresetuj: wszystkie wracają do niego."), settings_.default_view_studio, "studio");
        }
        break;
    }
#ifndef _WIN32
    case settings_ui::Category::compat: compatSettingsRows(); break; // Proton i Wine, its pages, a game's options.
#endif
    case settings_ui::Category::library:
        if (settings_state_.in(Page::emulation)) { emulationSettingsRows(); break; } // Emulacja and its systems (emulation_settings.cpp).
        if (settings_state_.at(Page::custom_games)) { customGamesSettingsRows(); break; } // Under DRM-free: the games added by hand.
        if (settings_state_.platform()) { pcSettingsRows(); break; } // A platform's page (DRM-free: its folders and games).
        if (settings_state_.at(Page::game_shortcuts)) { // One game's shortcuts (its menu → Skróty in Tryb TV).
            for (const auto place : {ShortcutPlace::desktop, ShortcutPlace::menu, ShortcutPlace::steam}) {
                if (!shortcutAvailable(shortcuts_game_, place)) continue;
                const bool there = hasShortcut(shortcuts_game_, place);
                Row row{place == ShortcutPlace::desktop ? Id::launch_shortcut_desktop : place == ShortcutPlace::menu ? Id::launch_shortcut_menu : Id::launch_steam,
                        Control::toggle, shortcutPlaceName(place),
                        place == ShortcutPlace::steam ? std::wstring(tr(L"Skrót w bibliotece Steam (także w trybie gry na Steam Decku), z grafikami; gra startuje przez Launchme."))
                                                      : std::wstring(tr(L"Uruchamia grę przez Launchme, z jej opcjami uruchamiania.")),
                        {}, there};
                add(std::move(row));
            }
            break;
        }
        if (settings_state_.at(Page::collections)) {
            // "Kolekcje" page: the manager first, then one row per collection (shown on the home screen, its order).
            add({Id::collection_manage, Control::action, tr(L"Zarządzaj kolekcjami"), tr(L"Nowe kolekcje, nazwy i gry w nich."), tr(L"Otwórz")});
            for (size_t i = 0; i < settings_collections_.size(); ++i) {
                const auto& collection = settings_collections_[i];
                // Steam ROM Manager collections stay off while Emulacja shows their games by platform.
                const bool emulated = srmCollectionsHidden() && srmCollection(collection);
                Row row{Id::collection, Control::collection, collection.name,
                    tr(L"Pozycja ") + std::to_wstring(i + 1) + (emulated ? tr(L" · Steam ROM Manager: gry są w Emulacji") :
                                                                           tr(L" · kolejność na ekranie głównym")), {},
                    !emulated && !settings_.hidden_collections.contains(collectionKey(account_, collection))};
                row.locked = emulated;
                row.key = collectionKey(account_, collection);
                row.removable = localCollection(collection); // Steam's own cannot be deleted from here.
                add(std::move(row));
            }
            break;
        }
        if (settings_state_.at(Page::excluded_games)) {
            // Wykluczone gry: a thin list of titles (with their store), each with Przywróć.
            add({Id::excluded_restore_all, Control::action, tr(L"Przywróć wszystkie"), tr(L"Wszystkie wykluczone gry wracają do biblioteki."), tr(L"Przywróć")});
            std::vector<std::pair<std::wstring, std::string>> excluded;
            for (const auto& [key, name] : library_state_.excluded_games) excluded.emplace_back(name.empty() ? text_encoding::wide(key) : name, key);
            std::sort(excluded.begin(), excluded.end(), [](const auto& a, const auto& b) { return foldGameTitle(a.first) < foldGameTitle(b.first); });
            for (const auto& [name, key] : excluded) {
                Row row{Id::excluded_game, Control::action, name, excludedSourceName(key), tr(L"Przywróć")};
                row.key = key;
                add(std::move(row));
            }
            break;
        }
        if (settings_state_.at(Page::hidden_studios)) {
            // Ukryci wydawcy i developerzy: each one with a switch that shows it again, by name.
            add({Id::studio_show_all, Control::action, tr(L"Pokaż wszystkich"), tr(L"Przywraca każdego ukrytego wydawcę i developera."), tr(L"Pokaż")});
            std::vector<std::pair<std::wstring, std::string>> hidden;
            for (const auto& key : settings_.hidden_studios)
                if (const auto studio = studios::parse(key)) hidden.emplace_back(std::wstring(studio->second->name), key);
            std::sort(hidden.begin(), hidden.end());
            for (const auto& [name, key] : hidden) {
                const bool publisher = studios::parse(key)->first == studios::Role::publisher;
                Row row{Id::studio_hidden_entry, Control::toggle, name, publisher ? tr(L"Wydawca") : tr(L"Developer"), {}, false};
                row.key = key;
                add(std::move(row));
            }
            break;
        }
        {
            // Overview: the platforms first (the DRM-free catalogue with the games added by hand,
            // Emulacja, Steam, then the others by name, each with its page behind the cog).
            section(tr(L"Platformy"));
            const size_t first_platform = settings_rows_.size();
            pcSettingsRows();
            if (settings_rows_.size() > first_platform && !next_section.empty()) {
                settings_rows_[first_platform].section = std::move(next_section); next_section.clear();
            }
        }
        section(tr(L"Kolekcje"));
        {
            const auto shown = std::count_if(settings_collections_.begin(), settings_collections_.end(), [&](const auto& collection) {
                return !(srmCollectionsHidden() && srmCollection(collection)) && !settings_.hidden_collections.contains(collectionKey(account_, collection));
            });
            add({Id::collections_open, Control::count, tr(L"Kolekcje"),
                tr(L"Kolekcje Steam i własne: widoczność i kolejność na ekranie głównym.") +
                    (settings_collections_.empty() ? std::wstring{} : L" · " + (tr(L"Widoczne: ") + std::to_wstring(shown))),
                std::to_wstring(settings_collections_.size())});
        }
        // The Steam rows of an account with no collections of its own.
        add({Id::updated_row, Control::toggle, tr(L"Ostatnio zaktualizowane"),
            tr(L"Rząd na ekranie głównym, gdy konto Steam nie ma kolekcji."), {}, settings_.show_updated_row});
        add({Id::discovery_row, Control::toggle, tr(L"Jeszcze niegrane"),
            tr(L"Rząd na ekranie głównym, gdy konto Steam nie ma kolekcji."), {}, settings_.show_discovery_row});
        // Wydawcy i developerzy: their libraries are collections of a kind, each row on its own.
        section(tr(L"Wydawcy i developerzy"));
        add({Id::studio_publishers, Control::toggle, tr(L"Wydawcy"),
            tr(L"Gry wydawców z logo: rząd na ekranie głównym i sekcja okna desktopowego."), {}, settings_.show_publishers});
        add({Id::studio_developers, Control::toggle, tr(L"Developerzy"),
            tr(L"Gry twórców z logo: rząd na ekranie głównym i sekcja okna desktopowego."), {}, settings_.show_developers});
        if (studiosShown()) {
            Row least{Id::studio_min_games, Control::choice, tr(L"Najmniej gier"),
                      tr(L"Studio pokazuje się, gdy ma tyle gier w bibliotece. Kolejność: najwięcej gier najpierw.")};
            for (const int count : studio_min_counts) least.choices.push_back({std::to_wstring(count)});
            for (size_t i = 0; i < studio_min_counts.size(); ++i) if (studio_min_counts[i] == settings_.studio_min_games) least.choice = static_cast<int>(i);
            least.value = std::to_wstring(settings_.studio_min_games);
            add(std::move(least));
        }
        if (!settings_.hidden_studios.empty())
            add({Id::studio_hidden, Control::count, tr(L"Ukryci wydawcy i developerzy"), tr(L"Ukryte w menu ich kafla albo w oknie desktopowym; tu można je przywrócić."),
                 std::to_wstring(settings_.hidden_studios.size())});
        section(tr(L"Widoczność"));
        add({Id::uninstalled, Control::toggle, tr(L"Niezainstalowane gry"),
            tr(L"Pokazuj je w kolekcjach i bibliotekach sklepów."), {}, settings_.show_uninstalled});
        add({Id::recent_uninstalled, Control::toggle, tr(L"Niezainstalowane w Ostatnio granych"),
            tr(L"Także gry bez instalacji i dodane bez pliku, według daty dodania do biblioteki."), {}, settings_.recent_uninstalled});
        add({Id::show_private, Control::toggle, tr(L"Pokaż ukryte/prywatne gry"),
            tr(L"Gry oznaczone w Steam jako ukryte lub prywatne pojawią się w kolekcjach i bibliotece."), {}, settings_.show_private});
        if (!library_state_.excluded_games.empty())
            add({Id::excluded_games, Control::count, tr(L"Wykluczone gry"),
                 tr(L"Wykluczone z menu gry (prawy przycisk: Wyklucz grę). Nie ma ich w bibliotece, kolekcjach ani wyszukiwaniu; tu je przywrócisz."),
                 std::to_wstring(library_state_.excluded_games.size())});
        section(tr(L"Uruchamianie"));
        choiceRow(Id::accept_action, tr(L"Domyślna akcja gry"),
                  tr(L"Wybranie gry w każdym widoku. Menu gry i ekran szczegółów zawsze uruchamiają."),
                  accept_actions, static_cast<int>(settings_.accept_action));
        add({Id::hide_while_playing, Control::toggle, tr(L"Chowaj do zasobnika podczas gry"),
            tr(L"Gdy gra działa, launcher znika z paska zadań i Alt+Tab, a jego ikona czeka w zasobniku. Wraca po zamknięciu gry."),
            {}, settings_.hide_while_playing});
        section(tr(L"Kopie zapisów"));
        {
            const auto program = ludusaviProgram();
            add({Id::save_backups, Control::toggle, tr(L"Kopia zapisów po każdej grze"),
                 tr(L"Ludusavi kopiuje zapisy gry, gdy ją zamkniesz. Przywrócisz je z menu gry → Zapisy."), {}, settings_.save_backup_after_game});
            Row row{Id::ludusavi, Control::action, L"Ludusavi",
                program.empty() ? std::wstring(tr(L"Program do kopii zapisów: wie, gdzie tysiące gier trzymają zapisy (PCGamingWiki). Pobierany z GitHuba."))
                                : tr(L"Zainstalowany: ") + program.wstring() + L"\n" + tr(L"Folder kopii i chmurę ustawisz w jego oknie."),
                ludusavi_busy_ ? tr(L"Pobieranie…") : program.empty() ? tr(L"Pobierz") : tr(L"Otwórz")};
            row.locked = ludusavi_busy_;
            add(std::move(row));
        }
        {
            // Each game's own shortcuts: its menu → Skróty. Here: every installed game still without one.
            const auto available = [&](bool steam_only) {
                return std::any_of(grouped_library_.begin(), grouped_library_.end(), [&](const Game& game) {
                    return isGame(game) && game.installed && !gameKey(game).empty() && (!steam_only || game.source != GameSource::steam);
                });
            };
            const bool menu = shortcutAvailableAnywhere(ShortcutPlace::menu) && available(false);
            const bool steam = shortcutAvailableAnywhere(ShortcutPlace::steam) && available(true);
            if (menu || steam) section(tr(L"Skróty gier"));
            if (menu)
                add({Id::shortcuts_all_menu, Control::action, shortcutPlaceName(ShortcutPlace::menu),
                     tr(L"Dodaj tam wszystkie zainstalowane gry, które jeszcze nie mają skrótu. Każda startuje przez Launchme."), tr(L"Dodaj wszystkie")});
            if (steam)
                add({Id::shortcuts_all_steam, Control::action, L"Steam",
                     tr(L"Gry spoza Steam jako skróty w bibliotece Steam (także w trybie gry na Steam Decku). Każda startuje przez Launchme."), tr(L"Dodaj wszystkie")});
        }
        break;
    case settings_ui::Category::media: {
        if (settings_state_.at(Page::screenscraper)) {
            // ScreenScraper page (library/screenscraper_config.hpp): one row per request parameter.
            for (const auto& field : screenscraper::fields) {
                const auto current = field.secret ? std::wstring{} : screenscraper::value(field.field); // Passwords are never read for display.
                Row row{Id::screenscraper, field.secret ? Control::secret : Control::text, tr(field.title), tr(field.description),
                    field.secret ? (screenscraper::stored(field.field) ? tr(L"Hasło zapisane") : tr(L"Wpisz hasło")) :
                                   (current.empty() ? tr(L"Wpisz") : current)};
                row.key = std::string(field.key);
                add(std::move(row));
            }
            add({Id::screenscraper_test, Control::action, tr(L"Sprawdź połączenie"),
                screenscraper_status_.empty() ? std::wstring(tr(L"Jedno zapytanie: czy dane działają i ile zapytań zostało na dziś.")) : screenscraper_status_,
                screenscraper_testing_ ? tr(L"Sprawdzanie…") : tr(L"Sprawdź")});
            add({Id::screenscraper_clear, Control::action, tr(L"Wyczyść dane ScreenScraper"),
                tr(L"Zapomina identyfikacje ROM-ów i przerwy; przy następnym starcie gry są sprawdzane od nowa."), tr(L"Wyczyść")});
            break;
        }
        const int delay_index = std::clamp(static_cast<int>(std::find(video_delays.begin(), video_delays.end(), settings_.video_delay) - video_delays.begin()),
                                           0, static_cast<int>(video_delays.size()) - 1);
        if (settings_state_.at(Page::videos)) {
            // When a background video starts, the trailers' disk space and the folder they are kept in.
            Row delay{Id::video_delay, Control::choice, tr(L"Opóźnienie filmu"), tr(L"Jak długo gra musi być zaznaczona, zanim ruszy film.")};
            for (const int seconds : video_delays) delay.choices.push_back({std::to_wstring(seconds) + L" s"});
            delay.choice = delay_index;
            delay.value = delay.choices[delay.choice].label;
            add(std::move(delay));
            if (settings_.steam_trailers) {
                Row space{Id::trailer_space, Control::choice, tr(L"Miejsce na zwiastuny"), tr(L"Po przekroczeniu znikają najdawniej oglądane.")};
                for (const int megabytes : trailer_spaces) space.choices.push_back({megabytes >= 1000 ? std::to_wstring(megabytes / 1000) + L" GB" : std::to_wstring(megabytes) + L" MB"});
                space.choice = std::clamp(static_cast<int>(std::find(trailer_spaces.begin(), trailer_spaces.end(), settings_.trailer_space_mb) - trailer_spaces.begin()),
                                          0, static_cast<int>(trailer_spaces.size()) - 1);
                space.value = space.choices[space.choice].label;
                add(std::move(space));
            }
            // Downloaded videos (app/game_video.cpp): ES-DE's own play where they are and are never copied.
            add({Id::video_folder, Control::action, tr(L"Folder filmów"),
                videoFolder().wstring() + tr(L" · filmy ScreenScraper i zwiastuny Steam; układ jak downloaded_media ES-DE."),
                tr(L"Zmień")});
            if (!settings_.video_folder.empty())
                add({Id::video_folder_reset, Control::action, tr(L"Domyślny folder filmów"),
                    (userConfigDirectory() / L"media").wstring(), tr(L"Przywróć")});
            break;
        }
        if (settings_state_.at(Page::service_keys)) {
            add({Id::steam_key, Control::secret, tr(L"Klucz Steam Web API"),
                tr(L"Znajomi, nazwy niezainstalowanych gier i pełna lista osiągnięć."),
                storedUserSecret(L"steam_web_api_key.txt") ? tr(L"Klucz zapisany") : tr(L"Wpisz klucz")});
            add({Id::sgdb_key, Control::secret, tr(L"Klucz SteamGridDB"),
                tr(L"Okładki, tła, logo, plakaty i przeglądarka grafik."),
                storedUserSecret(L"steamgriddb_api_key.txt") ? tr(L"Klucz zapisany") : tr(L"Wpisz klucz")});
            add({Id::rawg_key, Control::secret, tr(L"Klucz RAWG"),
                tr(L"Oceny przy starcie i identyfikacja metadanych gier spoza Steam."),
                storedUserSecret(L"rawg_api_key.txt") ? tr(L"Klucz zapisany") : tr(L"Wpisz klucz")});
            add({Id::thegamesdb_key, Control::secret, tr(L"Klucz TheGamesDB"),
                tr(L"Pudełka, tła, logo i zrzuty z TheGamesDB w „Zmień grafikę” (darmowy klucz z forum TheGamesDB)."),
                storedUserSecret(L"thegamesdb_api_key.txt") ? tr(L"Klucz zapisany") : tr(L"Wpisz klucz")});
            // ScreenScraper: several fields of its own, one level further.
            const bool ready = screenscraper::load().ready();
            Row scraper{Id::screenscraper_open, Control::page, L"ScreenScraper",
                tr(L"Identyfikacja ROM-ów, opisy, grafiki i filmy gier z emulatorów."), ready ? tr(L"Gotowe") : tr(L"Wpisz dane")};
            scraper.page = Page::screenscraper;
            add(std::move(scraper));
            break;
        }
        if (settings_state_.at(Page::artwork_sources)) {
            // The galleries "Zmień grafikę" offers in its "Źródło" list; those needing a key say so.
            for (const auto provider : artwork_sources::all) {
                const auto slug = std::string(artwork_sources::slug(provider));
                std::wstring description = tr(artwork_sources::description(provider));
                if (artwork_sources::needsKey(provider)) description += L" · " + std::wstring(tr(L"wymaga klucza"));
                Row row{Id::artwork_source, Control::toggle, artwork_sources::name(provider), std::move(description), {},
                        !settings_.artwork_sources_off.contains(slug)};
                row.key = slug;
                add(std::move(row));
            }
            break;
        }
        section(tr(L"Usługi"));
        {
            const int stored = static_cast<int>(storedUserSecret(L"steam_web_api_key.txt")) + static_cast<int>(storedUserSecret(L"steamgriddb_api_key.txt")) +
                               static_cast<int>(storedUserSecret(L"rawg_api_key.txt")) + static_cast<int>(screenscraper::load().ready()) +
                               static_cast<int>(storedUserSecret(L"thegamesdb_api_key.txt"));
            pageRow(Page::service_keys, tr(L"Klucze usług"), tr(L"Steam Web API, SteamGridDB, RAWG, TheGamesDB i ScreenScraper: znajomi, grafiki, oceny i gry z emulatorów."),
                    tr(L"Zapisane: ") + std::to_wstring(stored) + L"/5");
        }
        section(tr(L"Okładki"));
        add({Id::bulk_covers, Control::action, tr(L"Pobierz brakujące okładki"),
            tr(L"Kwadratowe, pionowe i poziome. Tylko brakujące."), tr(L"Otwórz")});
        {
            const auto enabled = std::count_if(artwork_sources::all.begin(), artwork_sources::all.end(), [&](ArtworkProvider provider) {
                return !settings_.artwork_sources_off.contains(artwork_sources::slug(provider));
            });
            pageRow(Page::artwork_sources, tr(L"Źródła grafik"),
                    tr(L"SteamGridDB, Steam, GOG, Xbox, Nintendo eShop, GameTDB, TitleDB, TheGamesDB, Wikimedia, ScreenScraper i ES-DE w „Zmień grafikę”."),
                    tr(L"Włączone: ") + std::to_wstring(enabled) + L"/" + std::to_wstring(artwork_sources::all.size()));
        }
        section(tr(L"Filmy w tle"));
        add({Id::game_videos, Control::toggle, tr(L"Autoodtwarzanie filmów z emulatorów"),
            tr(L"Po chwili na grze z emulatora jej film z ES-DE lub ScreenScraper zastępuje tło, a muzyka cichnie."), {}, settings_.game_videos});
        add({Id::steam_trailers, Control::toggle, tr(L"Zwiastuny gier Steam"),
            tr(L"To samo dla gier Steam i gier PC z innych sklepów (zwiastun tej samej gry ze Steam): 30 s ze sklepu (od 10. sekundy, 480p, bez dźwięku), pobierane raz."), {}, settings_.steam_trailers});
        pageRow(Page::videos, tr(L"Opcje filmów"),
                settings_.steam_trailers ? std::wstring(tr(L"Opóźnienie, miejsce na zwiastuny i folder filmów.")) : std::wstring(tr(L"Opóźnienie i folder filmów.")),
                std::to_wstring(video_delays[delay_index]) + L" s");
        // Names and artwork of emulated games: Biblioteka → Emulacja (emulation_settings.cpp).
        break;
    }
    case settings_ui::Category::audio: {
        const auto& data = settings_devices_;
        const bool ready = settings_devices_ready_;
        // The sound folder's audio files follow the built-in choices, by file name.
        sound_files_ = soundFolderFiles(settings_.sound_folder);
        const auto withFiles = [&](Row& row, const std::wstring& file) {
            const int builtin = static_cast<int>(row.choices.size());
            for (const auto& name : sound_files_) row.choices.push_back({name});
            // A file chosen earlier counts only while the folder holds it; otherwise the built-in
            // choice shows (and plays), with no stale name in the list.
            const auto found = std::find(sound_files_.begin(), sound_files_.end(), file);
            if (file.empty() || found == sound_files_.end()) return;
            row.choice = builtin + static_cast<int>(found - sound_files_.begin());
            row.value = file;
        };
        const auto soundRow = [&](Id id, const wchar_t* title, const wchar_t* description, int selected, const auto& options, const std::wstring* file) {
            const int index = std::clamp(selected, 0, static_cast<int>(options.size()) - 1);
            Row row{id, Control::choice, title, description, tr(options[index].name)};
            row.choice = index;
            for (const auto& option : options) row.choices.push_back({tr(option.name)});
            if (file) withFiles(row, *file);
            add(std::move(row));
        };
        if (settings_state_.at(Page::sounds)) {
            // The interface's own sounds: moving between items, notifications and the start.
            soundRow(Id::ui_sound, tr(L"Zmiana elementu"), tr(L"Dźwięk przy przejściu między elementami interfejsu."), settings_.ui_sound, ui_sounds, nullptr);
            soundRow(Id::toast_sound, tr(L"Powiadomienie"), tr(L"Dźwięk przy powiadomieniu."), settings_.toast_sound, toast_sounds, nullptr);
            soundRow(Id::boot_sound, tr(L"Dźwięk startu"), tr(L"Odtwarzany przy uruchomieniu launchera."), settings_.boot_sound, boot_sounds, &settings_.boot_file);
            break;
        }
        // Two tiles at the top: the default output and input; A opens the list of devices.
        section(tr(L"Urządzenia audio"));
        Row sources{Id::audio_sources, Control::sources, tr(L"Urządzenia audio")};
        for (const bool input : {false, true}) {
            Tile tile;
            tile.caption = input ? tr(L"Nagrywanie") : tr(L"Odtwarzanie");
            tile.input = input;
            tile.title = !ready ? tr(L"Wczytywanie…") : input ? tr(L"Brak wejścia audio") : tr(L"Brak wyjścia audio");
            for (const auto& endpoint : data.endpoints) {
                if (endpoint.input != input) continue;
                tile.choices.push_back({endpoint.name, 0, false, endpoint.id});
                if (endpoint.active) { tile.choice = static_cast<int>(tile.choices.size()) - 1; tile.title = endpoint.name; }
            }
            const auto& channel = data.audio[input];
            if (channel.available) { tile.level = channel.volume; tile.muted = channel.muted; }
            const size_t n = tile.choices.size();
            tile.value = !n ? std::wstring{} : std::to_wstring(n) + (n == 1 ? tr(L" urządzenie") :
                n % 10 >= 2 && n % 10 <= 4 && (n % 100 < 12 || n % 100 > 14) ? tr(L" urządzenia") : tr(L" urządzeń"));
            tile.action = tile.choices.size() > 1 ? tr(L"Zmień") : L"";
            sources.tiles.push_back(std::move(tile));
        }
        add(std::move(sources));
        section(tr(L"Głośność"));
        if (data.audio[0].available)
            add({Id::system_volume, Control::slider, tr(L"Głośność systemu"), tr(L"Domyślne wyjście Windows."),
                percent(data.audio[0].volume), false, std::clamp(data.audio[0].volume, 0.0f, 1.0f)});
        add({Id::music, Control::slider, tr(L"Muzyka w tle"),
            tr(L"0% wycisza muzykę. Dźwięki nawigacji pozostają aktywne."), percent(settings_.music_volume), false,
            settings_.music_volume / 0.4f});
        choiceRow(Id::video_volume, tr(L"Dźwięk filmów"), tr(L"Głośność filmów w tle; muzyka cichnie na ich czas, chyba że są wyciszone."),
                  video_volumes, settings_.video_volume);
        section(tr(L"Muzyka i dźwięki"));
        const int theme_index = std::clamp(settings_.music_theme, 0, static_cast<int>(music_themes.size()) - 1);
        Row theme{Id::music_theme, Control::choice, tr(L"Motyw muzyczny"), tr(music_status_), tr(music_themes[theme_index].name)};
        theme.choice = theme_index;
        for (const auto& music : music_themes) theme.choices.push_back({tr(music.name)});
        withFiles(theme, settings_.music_file);
        add(std::move(theme));
        // No value: one of its three sounds would read as the state of all of them.
        pageRow(Page::sounds, tr(L"Dźwięki interfejsu"), tr(L"Zmiana elementu, powiadomienie i dźwięk startu."), L"");
        add({Id::sound_folder, Control::action, tr(L"Folder dźwięków"),
            settings_.sound_folder.empty() ? std::wstring(tr(L"Pliki .mp3, .flac, .wav do motywu muzycznego i dźwięku startu.")) :
                                             settings_.sound_folder.wstring() + L" · " + std::to_wstring(sound_files_.size()) + tr(L" plików"),
            tr(L"Wybierz")});
        // Bluetooth headphones and speakers seen by the device watcher: paired ones first.
        section(L"Bluetooth");
        std::vector<const devices::BluetoothDevice*> audio_devices;
        for (const auto& device : data.bluetooth) if (device.audio) audio_devices.push_back(&device);
        std::stable_sort(audio_devices.begin(), audio_devices.end(), [](const auto* a, const auto* b) {
            return (a->connected ? 2 : a->paired ? 1 : 0) > (b->connected ? 2 : b->paired ? 1 : 0);
        });
        for (const auto* device : audio_devices) {
            Row row{Id::bt_audio, Control::action, device->name,
                device->connected ? tr(L"Połączono") : device->paired ? tr(L"Sparowano · niepołączone") : tr(L"W pobliżu"),
                device->paired ? tr(L"Ustawienia") : tr(L"Sparuj")};
            row.checked = device->paired;
            row.locked = data.busy;
            row.key = text_encoding::utf8Lenient(device->id);
            add(std::move(row));
        }
        add({Id::bt_devices, Control::action, tr(L"Urządzenia Bluetooth"),
            !data.status.empty() ? data.status : audio_devices.empty() && ready ? std::wstring(tr(L"Słuchawki i głośniki pojawią się tutaj w trybie parowania.")) :
                                                std::wstring(tr(L"Wszystkie urządzenia w pobliżu i parowanie.")),
            tr(L"Otwórz")});
        add({Id::windows_sound, Control::action, tr(L"Dźwięk w Windows"), tr(L"Ustawienia urządzeń, formaty i dźwięk przestrzenny."), tr(L"Otwórz")});
        break;
    }
    case settings_ui::Category::controllers: {
        // Gallery: every pad with its picture (downloaded or drawn), battery and Bluetooth action.
        section(tr(L"Połączone"));
        Row pads{Id::controller_pads, Control::pads, tr(L"Kontrolery")};
        const auto& list = settings_devices_ready_ && settings_device_panel_ == devices::Panel::controllers ? settings_devices_.controllers : connected_pads_;
        for (const auto& pad : list) {
            Tile tile;
            tile.caption = pad.connection;
            tile.title = pad.name;
            tile.picture = pad.picture;
            tile.connected = pad.connected;
            tile.battery = pad.connected ? pad.battery : std::nullopt;
            tile.charging = pad.charging;
            tile.value = pad.connected ? devices::batteryLabel(pad) : std::wstring(pad.radio_off ? tr(L"wyłączony") : tr(L"Rozłączono"));
            tile.key = pad.bluetooth_id;
            if (!pad.bluetooth_id.empty()) tile.action = pad.connected ? tr(L"Rozłącz") : tr(L"Połącz");
            else if (pad.connection == L"Bluetooth LE" || pad.radio_off) tile.action = tr(L"Ustawienia"); // Windows' Bluetooth settings.
            pads.tiles.push_back(std::move(tile));
        }
        Tile pair;
        pair.pair = true;
        pair.title = tr(L"Sparuj kontroler");
        pair.caption = L"Bluetooth";
        pair.value = list.empty() ? std::wstring(tr(L"Brak podłączonych kontrolerów")) : std::wstring{};
        pair.action = tr(L"Sparuj");
        pads.tiles.push_back(std::move(pair));
        add(std::move(pads));
        section(tr(L"Przycisk Guide"));
        add({Id::guide_menu, Control::toggle, tr(L"Przycisk Guide / PS"),
            gameBarUsesGuideButton() ? tr(L"Menu, przytrzymanie: zasilanie. Xbox Game Bar też go używa: Ustawienia Windows → Granie.") :
                                       tr(L"Menu, przytrzymanie: zasilanie. Wyłącz, jeśli Steam przejmuje ten przycisk."), {}, settings_.guide_menu});
        add({Id::guide_overlay, Control::toggle, tr(L"Guide w grach"),
            tr(L"Wyłącz Guide w Xbox Game Bar oraz Guide Button Focuses Steam w ustawieniach kontrolera Steam. Gry w oknie lub bez ramek."), {}, settings_.guide_overlay});
        section(tr(L"Zaawansowane"));
        add({Id::steam_controller, Control::toggle, tr(L"Steam Controller bez Steam"),
            tr(L"Kontroler Steam (2026) bezpośrednio przez HID, bez Steam Input i ViGEm. Gdy Steam działa, steruje nim Steam."), {},
            settings_.steam_controller});
        break;
    }
    case settings_ui::Category::plugins:
        pluginSettingsRows();
        break;
    case settings_ui::Category::companion:
        for (auto& row : companionSettingsRows()) add(std::move(row)); // Her rows bring their own sections.
        break;
    case settings_ui::Category::system: {
        if (settings_state_.at(Page::screensaver)) {
            add({Id::screensaver, Control::toggle, tr(L"Wygaszacz ekranu"),
                tr(L"Grafiki gier zapisane na dysku, gdy launcher jest bezczynny."), {}, settings_.screensaver});
            choiceRow(Id::screensaver_style, tr(L"Styl"), tr(L"Tła gier wymagają grafik tła, pudełka i płyty okładek."),
                      screensaver_styles, static_cast<int>(settings_.screensaver_style));
            Row delay{Id::screensaver_minutes, Control::choice, tr(L"Po czasie"), tr(L"Bez klawiatury, myszy i kontrolera.")};
            for (const int minutes : screensaver::minute_choices) {
                delay.choices.push_back({std::to_wstring(minutes) + tr(L" min")});
                if (minutes == screensaver::normalizedMinutes(settings_.screensaver_minutes))
                    delay.choice = static_cast<int>(delay.choices.size()) - 1;
            }
            delay.value = delay.choices[delay.choice].label;
            add(std::move(delay));
            add({Id::screensaver_start, Control::action, tr(L"Uruchom teraz"), tr(L"Podgląd, także gdy wygaszacz jest wyłączony."), tr(L"Uruchom")});
            break;
        }
        section(tr(L"Aktualizacje"));
        {
            const auto [title, help, button] = updateRow();
            add({Id::update, Control::action, title, help, button});
        }
        add({Id::update_check, Control::toggle, tr(L"Sprawdzaj aktualizacje"),
            tr(L"Co kilka godzin pyta GitHub o nowe wydanie i powiadamia o nim."), {}, settings_.update_check});
        section(tr(L"Okno i język"));
        add({Id::windowed, Control::toggle, tr(L"Tryb TV w oknie"), tr(L"Zamiast pełnego ekranu. Także F11."), {}, settings_.windowed});
        {
            Row language{Id::language, Control::choice, tr(L"Język"), tr(L"Systemowy: zgodnie z językiem Windows.")};
            language.choices = {{tr(L"Systemowy")}, {L"Polski"}, {L"English"}, {L"Português (Brasil)"}, {L"Español"}};
            language.choice = std::clamp(static_cast<int>(settings_.language), 0, 4);
            language.value = language.choices[language.choice].label;
            add(std::move(language));
        }
        add({Id::autostart, Control::toggle, tr(L"Uruchamiaj z systemem Windows"),
            tr(L"Po zalogowaniu, zminimalizowany na pasku zadań."), {}, settings_.autostart});
        section(tr(L"Funkcje"));
        add({Id::glance, Control::toggle, tr(L"Glance — hub gamingowy"),
            tr(L"Nowości, premiery i artykuły. Kafel na ekranie głównym (w Flix pasek u góry): kliknięcie, G lub R2/RT."), {}, settings_.glance});
        add({Id::tarot, Control::toggle, tr(L"Tarot — wróżba z kart"),
            tr(L"Kilka pytań w kartach i do trzech gier na koniec: z Twojej biblioteki albo do kupienia na Steam. T, przycisk View/Select albo ikona w oknie desktopowym."),
            {}, settings_.tarot});
        pageRow(Page::screensaver, tr(L"Wygaszacz ekranu"), tr(L"Grafiki gier, gdy launcher jest bezczynny: styl, czas i podgląd."),
                settings_.screensaver ? tr(L"Po ") + std::to_wstring(screensaver::normalizedMinutes(settings_.screensaver_minutes)) + tr(L" min") :
                                        std::wstring(tr(L"Wyłączony")));
        // Experimental features are features too: the same list, their descriptions say what is new.
        add({Id::quick_resume, Control::toggle, tr(L"Szybkie wznawianie"),
            tr(L"Wstrzymaj grę w jej menu i wróć do niej później. Gry online mogą się rozłączyć."), {}, settings_.quick_resume});
        {
            static constexpr std::array<const wchar_t*, 3> fps{trKey(L"Wyłączony"), trKey(L"Średnie FPS"), trKey(L"FPS i 1% low")};
            choiceRow(Id::game_fps, tr(L"Widget FPS gry"),
                      tr(L"Eksperymentalny pomiar PresentMon / ETW. Wymaga uprawnień do śledzenia wydajności. 1% low: najwolniejsze klatki z ostatnich 60 sekund."),
                      fps, !settings_.game_fps ? 0 : settings_.game_fps_low ? 2 : 1);
        }
        if (settings_.game_fps) {
            // The trace needs the user in "Performance Log Users" once; the launcher itself stays unelevated.
            switch (etw_access::state()) {
            case etw_access::State::missing:
                add({Id::fps_access, Control::action, tr(L"Uprawnienia pomiaru FPS"),
                    tr(L"Dodaje Twoje konto do grupy Użytkownicy dzienników wydajności (jedno potwierdzenie administratora). Launchme nadal działa bez podniesionych uprawnień."),
                    tr(L"Nadaj")});
                break;
            case etw_access::State::pending_sign_in:
                add({Id::fps_access, Control::action, tr(L"Uprawnienia pomiaru FPS"),
                    tr(L"Nadane. Wyloguj się z Windows i zaloguj ponownie, aby pomiar zadziałał."), L""});
                break;
            default: break;
            }
        }
        section(tr(L"Wydajność"));
        add({Id::low_memory, Control::toggle, tr(L"Tryb niskiego zużycia RAM"),
            tr(L"Grafiki w rozmiarze z ekranu i mniej trzymanych w pamięci; gdy gra działa, launcher oddaje pamięć."),
            {}, settings_.low_memory});
        add({Id::texture_compression, Control::toggle, tr(L"Kompresja grafik (BC1)"),
            tr(L"Okładki i plakaty w pamięci karty graficznej 8 razy mniejsze, loga 4 razy, kosztem odrobiny jakości. W trybie niskiego zużycia RAM także tła."),
            {}, settings_.texture_compression});
        section(tr(L"Pomoc"));
        // The version, and the folder with launchme.log and crash reports a user can send.
        add({Id::about, Control::action, L"Launchme " + diagnostics::version(),
            tr(L"Dane, dziennik (launchme.log) i raporty awarii (crashes): ") + userConfigDirectory().wstring(), tr(L"Otwórz folder")});
        if (!desktop_mode_) { // The walkthrough is Tryb TV's (its panels, menus and layouts).
            // Resets the first-run state and shows the walkthrough again (src/app/tour.cpp).
            add({Id::tour, Control::action, tr(L"Przewodnik"),
                tr(L"Wprowadzenie z pierwszego uruchomienia: panele, menu gry i wybór układu."), tr(L"Pokaż")});
        }
        break;
    }
    }
    // A page closed while its rows were built (its plugin or its last game went away): the parent's
    // rows are here, focused on the row the page was opened from.
    if (settings_state_.reopen) {
        selected_row = std::move(settings_state_.reopen); reopened = true;
        settings_state_.reopen.reset();
        settings_rows_page_ = settingsPage();
    }
    if (selected_row) {
        const auto found = std::find_if(settings_rows_.begin(), settings_rows_.end(), [&](const Row& row) { return selected_row->matches(row); });
        if (found != settings_rows_.end()) settings_state_.selected[settings_state_.category] = static_cast<int>(found - settings_rows_.begin());
        else if (!reopened) {
            // A removed folder/game: stay at the same place in the list rather than jumping to the top;
            // a platform page that lost its row (signed out) starts again at its first row.
            if (settings_state_.platform() && selected_row->key.empty()) settings_state_.selected[settings_state_.category] = 0;
            settings_state_.editing = false;
        }
    }
    if (!settings_rows_.empty() && tiled(settings_rows_[std::clamp(settings_state_.row(), 0, static_cast<int>(settings_rows_.size()) - 1)].control)) {
        const auto& tiles = settings_rows_[std::clamp(settings_state_.row(), 0, static_cast<int>(settings_rows_.size()) - 1)].tiles;
        settings_state_.tile = std::clamp(settings_state_.tile, 0, std::max(0, static_cast<int>(tiles.size()) - 1));
    }
    settings_state_.clamp(static_cast<int>(settings_rows_.size()));
    renderer_.fitSettingsDescriptions(settings_rows_, settings_state_, desktop_mode_); // Heights before scrolling to the row.
    ensureSettingVisible();
    InvalidateRect(hwnd_, nullptr, FALSE);
}
