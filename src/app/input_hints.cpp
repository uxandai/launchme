// Bottom-right button hints (Settings → Interfejs → Podpowiedzi przycisków): what the main buttons
// do on the current screen. After pad input they show that pad's buttons (by family, or the chosen
// symbols), after keys or the mouse the keys. Dialogs, the keyboard and text fields show none.
#include "launcher.hpp"
#include "../model/controller_models.hpp"

HintDevice Launcher::hintDevice() const {
    if (!pad_input_ || !pad_connected_) return HintDevice::keyboard;
    if (settings_.hint_glyphs == AppSettings::HintGlyphs::xbox) return HintDevice::xbox;
    if (settings_.hint_glyphs == AppSettings::HintGlyphs::playstation) return HintDevice::playstation;
    if (settings_.hint_glyphs == AppSettings::HintGlyphs::nintendo) return HintDevice::nintendo;
    // The active pad's family: its census entry, else its reported name.
    auto picture = devices::classifyController(0, 0, controller_name_);
    for (const auto& item : connected_pads_) if (item.id == controller_id_ && item.picture != devices::Picture::none) picture = item.picture;
    using devices::Picture;
    return picture == Picture::dualsense || picture == Picture::dualsense_edge || picture == Picture::dualshock4 ? HintDevice::playstation :
           picture == Picture::switch_pro || picture == Picture::joycon_left || picture == Picture::joycon_right ||
           picture == Picture::joycon_pair ? HintDevice::nintendo : HintDevice::xbox;
}

void Launcher::inputHints(ViewContext& context) const {
    context.hints.clear();
    if (!settings_.input_hints || closing_ || tour_.open || screensaverActive() || launchAnimationActive() || pending_launch_) return;
    // Keep the corner clear until the bottom bar has finished sliding away, too.
    if (quick_open_ || quick_progress_ > 0.003f) return;
    if (topModal() != Modal::none || keyboard_.open || color_editor_.open || cropOpen() || artworkOpen() || achievementsOpen()) return;
    const bool pad = pad_input_ && pad_connected_;
    context.hint_device = hintDevice();
    auto& hints = context.hints;
    const auto add = [&](HintButton button, std::wstring label) { hints.push_back({button, std::move(label)}); };
    const auto selectAndClose = [&] { add(HintButton::accept, tr(L"Wybierz")); add(HintButton::back, tr(L"Zamknij")); };
    if (friendChatOpen()) { // A friend's conversation: Enter sends, Back returns to the list.
        const auto& chat = friend_chat_;
        if (chat.back_focus) add(HintButton::accept, tr(L"Wstecz"));
        else if (!chat.input.empty()) add(HintButton::accept, tr(L"Wyślij"));
        else if (pad) add(HintButton::accept, tr(L"Pisz"));
        if (pad) add(HintButton::shoulders, tr(L"Przewiń"));
        add(HintButton::back, tr(L"Wstecz"));
        return;
    }
    if (friends_open_ || menu_open_ || category_menu_open_) { selectAndClose(); return; }
    if (libraryControls() && library_controls_.open()) {
        const bool views = library_controls_.panel == library_ui::Panel::views;
        const bool close = views && library_controls_.view_row == library_ui::views::closeRow(libraryView());
        add(HintButton::accept, close ? tr(L"Zamknij") :
            views && !library_controls_.view_dropdown && library_controls_.view_row > 0 ? tr(L"Zmień") : tr(L"Wybierz"));
        if (!close) add(HintButton::back, library_controls_.view_dropdown ? tr(L"Wstecz") : tr(L"Zamknij")); // Not "Zamknij" twice.
        return;
    }
    const Game* game = selected_ < cards_.size() ? &cards_[selected_].game : nullptr;
    const auto play = [&] {
        if (searchBackFocused() || (libraryControls() && library_controls_.focus == library_ui::back_button)) { add(HintButton::accept, tr(L"Wstecz")); return; }
        if (!game) return;
        if (!isGame(*game)) { add(HintButton::accept, tr(L"Otwórz")); return; }
        // A focused button beside the games says what it does (Przegląd: "Szczegóły", the primary action).
        const int button = libraryControls() && libraryButtons() && !library_controls_.open() && library_controls_.focus < 0 ? library_controls_.action : -1;
        if (button >= 0) add(HintButton::accept, libraryOverview() ? (button == 0 ? std::wstring(tr(L"Szczegóły")) : actionLabel(*game)) :
                                                      button == 0 ? actionLabel(*game) : std::wstring(tr(L"Menu gry")));
        else add(HintButton::accept, settings_.accept_action == AppSettings::AcceptAction::details ? std::wstring(tr(L"Szczegóły")) : actionLabel(*game));
        if (pad) add(HintButton::menu, tr(L"Menu gry"));
    };
    switch (screen_) {
    case ScreenMode::glance:
        add(HintButton::accept, tr(L"Otwórz")); add(HintButton::shoulders, tr(L"Kategorie")); add(HintButton::back, tr(L"Wstecz"));
        return;
    case ScreenMode::companion: {
        const auto& chat = companion_.chat;
        if (chat.feed) {
            const auto& feed = companion_.feed.view;
            if (feed.ready()) {
                add(HintButton::accept, !feed.thread.empty() ? tr(L"Skomentuj") : feed.focus == 0 ? tr(L"Serduszko") : feed.focus == 1 ? tr(L"Komentarze") : tr(L"Wiadomość prywatna"));
                add(HintButton::shoulders, tr(L"Przewiń"));
                if (!feed.thread.empty() && pad) add(HintButton::search, tr(L"Klawiatura"));
            }
            add(HintButton::back, feed.thread.empty() ? tr(L"Zamknij") : tr(L"Wstecz")); return;
        }
        if (chat.viewer) { add(HintButton::back, tr(L"Zamknij")); return; } // Her enlarged photo.
        if (chat.confirm || (chat.mind && chat.mind_confirm)) { add(HintButton::accept, tr(L"Wybierz")); add(HintButton::back, tr(L"Anuluj")); return; }
        if (chat.mind) { // Umysł i pamięć: X / Delete removes the selected memory, LB/RB turn the sphere.
            hints.push_back({HintButton::menu, tr(L"Filtr grafu"), L"F"});
            const auto nodes = companionMindNodes();
            const int selected = std::clamp(chat.mind_selected, 0, std::max(0, static_cast<int>(nodes.size()) - 1));
            // A hidden thought: A / Enter shows it or masks it again.
            if (!nodes.empty() && nodes[static_cast<size_t>(selected)].revealable)
                add(HintButton::accept, nodes[static_cast<size_t>(selected)].masked ? tr(L"Pokaż myśl") : tr(L"Ukryj myśl"));
            if (!nodes.empty() && nodes[static_cast<size_t>(selected)].locked) hints.push_back({HintButton::search, tr(L"Odkryj"), L"Del"});
            else if (!nodes.empty() && nodes[static_cast<size_t>(selected)].deletable) hints.push_back({HintButton::search, tr(L"Usuń"), L"Del"});
            if (pad) add(HintButton::shoulders, tr(L"Obróć"));
            add(HintButton::back, tr(L"Zamknij"));
            return;
        }
        if (chat.menu) { add(HintButton::accept, tr(L"Wybierz")); add(HintButton::back, tr(L"Zamknij")); return; }
        if (chat.archive) { if (pad) add(HintButton::shoulders, tr(L"Przewiń")); add(HintButton::back, tr(L"Wstecz")); return; }
        if (chat.focus == companion_ui::focus_back) add(HintButton::accept, tr(L"Wstecz"));
        else if (chat.ended && chat.focus == companion_ui::focus_send) { if (!companionResting()) add(HintButton::accept, tr(L"Nowa rozmowa")); }
        else if (chat.ended && chat.focus == companion_ui::focus_input) {}
        else if (chat.focus == companion_ui::focus_more) add(HintButton::accept, tr(L"Opcje"));
        else if (chat.focus == companion_ui::focus_feed) add(HintButton::accept, L"Launchfeed");
        else if (chat.focus >= 0 && chat.focus < static_cast<int>(chat.chips.size()))
            add(HintButton::accept, chat.chips[static_cast<size_t>(chat.focus)].game.empty() ? tr(L"Zapytaj") : tr(L"Graj"));
        else if (!chat.input.empty()) add(HintButton::accept, tr(L"Wyślij"));
        else if (pad) add(HintButton::accept, tr(L"Pisz"));
        if (pad) { if (!chat.ended) add(HintButton::menu, tr(L"W co zagrać?")); add(HintButton::shoulders, tr(L"Przewiń")); }
        add(HintButton::back, tr(L"Zamknij"));
        return;
    }
    case ScreenMode::settings: {
        if (settings_rows_.empty() && settings_state_.options) { add(HintButton::back, tr(L"Wstecz")); return; }
        const auto* row = settings_state_.options && !settings_rows_.empty() ? &settings_rows_[settings_state_.row()] : nullptr;
        if (settings_state_.editing && row && row->picker()) { add(HintButton::accept, tr(L"Wybierz")); add(HintButton::back, tr(L"Anuluj")); return; }
        if (editingSecret()) {
            add(HintButton::accept, settings_secret_browse_ ? tr(L"Przeglądaj…") : settings_secret_cancel_ ? tr(L"Anuluj") : tr(L"Zapisz"));
            add(HintButton::back, tr(L"Anuluj"));
            if (pad) add(HintButton::search, tr(L"Klawiatura"));
            return;
        }
        if (!row) add(HintButton::accept, tr(L"Otwórz"));
        else if (row->control == settings_ui::Control::toggle || row->control == settings_ui::Control::collection) add(HintButton::accept, tr(L"Przełącz"));
        else if (row->control == settings_ui::Control::slider) add(HintButton::accept, settings_state_.editing ? tr(L"Gotowe") : tr(L"Zmień"));
        else if (row->control == settings_ui::Control::page || row->control == settings_ui::Control::count) add(HintButton::accept, tr(L"Otwórz"));
        else if (row->control == settings_ui::Control::fold) add(HintButton::accept, row->checked ? tr(L"Zwiń") : tr(L"Rozwiń"));
        else add(HintButton::accept, tr(L"Wybierz"));
        if (settings_state_.at(settings_ui::Page::home_rows) && row && row->id == settings_ui::Id::home_row && homeRowCollection(row->key))
            hints.push_back({HintButton::shoulders, tr(L"Przesuń"), L"Shift ↑↓"});
        if (row && !row->reset.empty()) hints.push_back({HintButton::search, tr(L"Przywróć domyślne"), L"Del"}); // A game's own value.
        if (row && row->removable) hints.push_back({HintButton::search, tr(L"Usuń kolekcję"), L"Del"}); // An own collection's question.
        if (row && row->control == settings_ui::Control::sources && !row->tiles.empty() &&
            row->tiles[std::clamp(settings_state_.tile, 0, static_cast<int>(row->tiles.size()) - 1)].level >= 0)
            add(HintButton::shoulders, tr(L"Głośność")); // The focused source's volume, LB/RB or PgUp/PgDn.
        if (!settings_state_.at(settings_ui::Page::settings_search)) hints.push_back({HintButton::search, tr(L"Szukaj"), L"F3"});
        add(HintButton::back, tr(L"Wstecz"));
        return;
    }
    case ScreenMode::plugin_page:
        if (pluginPageTyping()) { add(HintButton::accept, tr(L"Szukaj")); if (pad) add(HintButton::search, tr(L"Klawiatura")); }
        else {
            add(HintButton::accept, tr(L"Wybierz"));
            if (pad && !plugin_page_.rows.empty() && plugin_page_.rows.front().input) add(HintButton::search, tr(L"Szukaj"));
        }
        if (storeTabsShown() && plugin_menu_.size() > 1) add(HintButton::shoulders, tr(L"Sklepy"));
        add(HintButton::back, tr(L"Wstecz"));
        return;
    case ScreenMode::devices:
        add(HintButton::accept, tr(L"Wybierz")); add(HintButton::back, tr(L"Wstecz"));
        return;
    case ScreenMode::downloads:
        if (!downloads_state_.back && !downloads_games_.empty()) add(HintButton::accept, tr(L"Otwórz"));
        add(HintButton::back, tr(L"Wstecz"));
        return;
    case ScreenMode::details:
        add(HintButton::accept, tr(L"Wybierz"));
        if (pad) add(HintButton::menu, tr(L"Menu gry"));
        add(HintButton::back, tr(L"Wstecz"));
        return;
    case ScreenMode::library:
    case ScreenMode::search:
    case ScreenMode::store:
    case ScreenMode::platform_games:
        play();
        if (storeTabsShown() && plugin_menu_.size() > 1) add(HintButton::shoulders, tr(L"Sklepy"));
        if (pad) add(HintButton::search, tr(L"Szukaj"));
        else if (screen_ == ScreenMode::library) add(HintButton::type, tr(L"Szukaj"));
        if (libraryTabsShown()) add(HintButton::shoulders, tr(L"Kategorie"));
        add(HintButton::back, tr(L"Wstecz"));
        return;
    case ScreenMode::shelves:
        if (scanning_ || (cards_.empty() && libraryLoading())) return;
        // Enter / A as the focus target takes it: a category of the rail or a tab returns to its
        // tiles, PSfive's buttons and top bar act themselves; a tile its game's action.
        if (cards_.empty()) add(HintButton::accept, empty_library::label(empty_focus_)); // Empty library: its focused button.
        else if ((categoryRailActive() && category_rail_focus_) || metroTabsFocused()) add(HintButton::accept, tr(L"Wybierz"));
        else if (psfiveTopFocused()) add(HintButton::accept, psfive_top_ < static_cast<int>(psfiveTabs().size()) ? tr(L"Wybierz") : tr(L"Otwórz"));
        else if (psfiveActions()) {
            if (psfive_action_ == 0) { add(HintButton::accept, actionLabel(cards_[selected_].game)); if (pad) add(HintButton::menu, tr(L"Menu gry")); }
            else add(HintButton::accept, tr(L"Menu gry"));
        } else play();
        if (pad) add(HintButton::search, tr(L"Szukaj"));
        // LB/RB, Page Up/Down: Metro's pages, the categories everywhere else (Snitch has none).
        if (metroActive()) add(HintButton::shoulders, tr(L"Strony"));
        else if (!snitchActive() && categories_.size() > 1 && !cards_.empty()) add(HintButton::shoulders, tr(L"Kategorie"));
        add(HintButton::drawer, tr(L"Pulpit"));
        add(HintButton::home, tr(L"Menu"));
        return;
    default:
        return;
    }
}
