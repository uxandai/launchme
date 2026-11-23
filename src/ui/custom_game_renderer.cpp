// Dodaj grę / Opcje uruchamiania in Tryb TV (custom_game_view.hpp), drawn as Settings' rows: a card per
// field, option or mod over the panel's width, its title on the left and its control on the right. The
// focus is an outline that springs between the items (ViewContext::editor_glide); nothing else lights up.
#include "renderer.hpp"

#include <algorithm>
#include <cmath>

void Renderer::bindCustomPoster() {
    if (custom_poster_.pixels != context_->custom_poster) { custom_poster_ = {}; custom_poster_.pixels = context_->custom_poster; }
    if (custom_poster_.loaded() && !custom_poster_.bitmap) createBitmap(custom_poster_); // Uploaded once decoded.
}

void Renderer::drawCustomGameEditor() {
    if (!context_->custom_editor || !context_->custom_editor->open) { if (custom_poster_.loaded()) custom_poster_ = {}; return; }
    if (desktopDialogs()) { drawDesktopCustomEditor(); return; } // Desktop mode: desktop_dialogs_renderer.cpp.
    using namespace custom_game_ui;
    using Rect = settings_ui::Rect;
    const auto& ui = *context_->custom_editor;
    const auto layout = Layout::of(ui, viewport_.width, viewport_.height);
    const auto accent = uiAccent();
    const auto gray = [](float value, float alpha = 1.0f) { return D2D1::ColorF(value, value, value, alpha); };
    const auto rect = [](Rect r) { return D2D1::RectF(r.left, r.top, r.right, r.bottom); };
    const auto middle = [](Rect r) { return (r.top + r.bottom) * 0.5f; };
    const auto fill = [&](Rect r, D2D1_COLOR_F ink, float radius) {
        brush(ink);
        target_->FillRoundedRectangle(D2D1::RoundedRect(rect(r), radius, radius), solid_.Get());
    };
    const auto outline = [&](Rect r, D2D1_COLOR_F ink, float thickness, float radius) {
        brush(ink);
        target_->DrawRoundedRectangle(D2D1::RoundedRect(rect(r), radius, radius), solid_.Get(), thickness);
    };
    auto* const title_font = category_font_.Get();
    auto* const body = settings_body_font_.Get();
    auto* const value_font = details_body_font_.Get();
    const auto measure = [&](const std::wstring& value) {
        if (value.empty()) return 0.0f;
        Microsoft::WRL::ComPtr<IDWriteTextLayout> text_layout;
        DWRITE_TEXT_METRICS metrics{};
        if (FAILED(dwrite_->CreateTextLayout(value.c_str(), static_cast<UINT32>(value.size()), value_font, 100000.0f, 60.0f, text_layout.GetAddressOf())) ||
            FAILED(text_layout->GetMetrics(&metrics))) return 0.0f;
        return metrics.widthIncludingTrailingWhitespace;
    };
    // A glass pill with its caption (no white fill: the focus is the outline); danger: the caption in the warning colour.
    const auto button = [&](Rect r, const std::wstring& label, bool enabled = true, bool danger = false) {
        const auto ink = drawButton(rect(r), 0, 1, false, false, enabled);
        textLine(label, r.left + 10, middle(r), r.right - r.left - 20, body, danger && enabled ? palette::danger() : ink, DWRITE_TEXT_ALIGNMENT_CENTER);
    };
    const auto chevronDown = [&](float x, float y, D2D1_COLOR_F ink) {
        line(x - 6, y - 3, x, y + 3, ink, 1.8f);
        line(x, y + 3, x + 6, y - 3, ink, 1.8f);
    };
    // A list's control: its value on a glass pill, a chevron pointing down (A or a click opens the list).
    const auto picker = [&](Rect r, const std::wstring& value, bool enabled = true) {
        const auto ink = drawButton(rect(r), 0, 1, false, false, enabled);
        textLine(value, r.left + 20, middle(r), r.right - r.left - 60, body, ink);
        chevronDown(r.right - 26, middle(r), ink);
    };
    // Settings' switch, its state written before it.
    const auto toggle = [&](float right, float y, bool on) {
        fill({right - 52, y - 14, right, y + 14}, on ? accent : gray(0.23f), 14);
        brush(gray(on ? 0.08f : 0.67f));
        target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(on ? right - 14 : right - 38, y), 10, 10), solid_.Get());
        textLine(on ? tr(L"Włączone") : tr(L"Wyłączone"), right - 66 - 148, y, 148, body, palette::stateLabel(false), DWRITE_TEXT_ALIGNMENT_TRAILING);
    };
    const auto card = [&](Rect r) { fill(r, gray(1, 0.04f), 16); };

    brush(gray(0, 0.80f));
    target_->FillRectangle(D2D1::RectF(0, 0, viewport_.width, viewport_.height), solid_.Get());
    const auto panel = layout.panel();
    fill(panel, gray(0.07f), 22);
    if (panel_borders_) outline(panel, gray(1, 0.16f), 1.0f, 22);
    // The heading as Settings' pages: the game (or "Dodaj grę"), under it what starts the game.
    const std::wstring heading = ui.launchOptions() ? ui.title : ui.editing() ? ui.art_name : std::wstring(tr(L"Dodaj grę"));
    std::wstring subtitle = ui.managed;
    if (subtitle.empty() && (ui.launchOptions() || ui.editing())) subtitle = tr(L"Opcje uruchamiania");
    textGroup(heading, subtitle, panel.left + Layout::pad, panel.top + 40, panel.right - panel.left - 2 * Layout::pad, settings_heading_font_.Get(), gray(0.96f),
              body, gray(0.58f), 1, true, heading_group_gap);

    if (layout.tabs) {
        // "Uruchamianie" / "Skrypty" / "Mody" in one track, the shown one on a lighter pill.
        const auto track = layout.tabTrack();
        fill(track, gray(1, 0.045f), (track.bottom - track.top) * 0.5f);
        for (int which = 0; which < layout.tab_count; ++which) {
            const auto r = layout.tab(which);
            const auto tab = which == 2 ? Tab::mods : which == 1 ? Tab::scripts : Tab::launch;
            const bool shown = ui.tab == tab;
            if (shown) fill(r, gray(1, 0.13f), (r.bottom - r.top) * 0.5f);
            textLine(which == 2 ? tr(L"Mody") : which ? tr(L"Skrypty") : tr(L"Uruchamianie"), r.left + 8, middle(r), r.right - r.left - 16,
                     menu_font_.Get(), shown ? gray(0.96f) : gray(0.60f), DWRITE_TEXT_ALIGNMENT_CENTER);
        }
    }

    if (layout.pills) {
        // A new game: what to point at, a file or a folder (one game, a ROM folder, a folder of games).
        button(layout.pill(0), tr(L"Wskaż plik"));
        button(layout.pill(1), tr(L"Wskaż folder"));
    }
    const std::array<const wchar_t*, field_count> labels{tr(L"Nazwa"), tr(L"Plik"), tr(L"Opcje uruchamiania"), tr(L"Folder roboczy"),
                                                         L"WINEDLLOVERRIDES", tr(L"Opcje Protona i Wine (jak w Steamie)"), tr(L"Platforma"),
                                                         tr(L"Przed uruchomieniem gry (Launchme czeka, aż skończy)"), tr(L"Po uruchomieniu gry"), tr(L"Po wyjściu z gry")};
    for (int index = 0; index < field_count; ++index) {
        if (!ui.shows(index)) continue;
        card(layout.fieldRow(index));
        // Its label; a note in brackets goes under it ("Launchme czeka, aż skończy").
        std::wstring title = emulatedLabel(ui, index, labels[index]), note;
        if (const auto open = title.find(L" ("); open != std::wstring::npos && title.back() == L')') {
            note = title.substr(open + 2, title.size() - open - 3);
            title.resize(open);
        }
        const auto label = layout.label(index);
        textGroup(title, note, label.left, middle(label), label.right - label.left - 12, title_font, gray(0.93f), body, gray(0.55f), 1);
        const auto input = layout.input(index);
        const bool focused = field(ui.focus) == index;
        const float field_radius = (input.bottom - input.top) * 0.5f;
        fill(input, gray(0.025f), field_radius);
        const auto& value = ui.fields[index];
        const float left = input.left + 18, visible = input.right - input.left - 36, y = middle(input);
        // Focused text scrolls to keep the caret visible. Unfocused, a long path keeps its end (the file
        // name) behind a leading ellipsis; other text ends with one.
        const float full = measure(value);
        const float caret = focused ? measure(value.substr(0, std::min(ui.caret, value.size()))) : 0.0f;
        const bool path = index == file_field || index == directory_field;
        const float offset = focused ? std::max(0.0f, caret - visible + 2) : 0.0f;
        std::wstring shown = value;
        if (!focused && path && full > visible) {
            size_t low = 1, high = value.size(); // Characters dropped from the start.
            while (low < high) {
                const size_t mid = (low + high) / 2;
                if (measure(L"…" + value.substr(mid)) <= visible) high = mid; else low = mid + 1;
            }
            shown = L"…" + value.substr(low);
        }
        if (value.empty()) {
            const wchar_t* hint = emulatedHint(ui, index);
            if (!hint) {
                if (index == directory_field) hint = tr(L"Domyślnie: folder pliku");
                else if (index == arguments_field && ui.launchOptions()) hint = tr(L"Np. -windowed -skipintro");
                else if (index == dll_field) hint = tr(L"Np. dinput8=n,b;winhttp=n,b");
                else if (index == wine_field) hint = tr(L"Np. PROTON_USE_WINED3D=1 gamemoderun %command% -dx11");
                else if (scriptField(index)) hint = scriptHint(index);
            }
            if (hint) textLine(hint, left, y, visible, body, gray(0.40f));
        } else {
            target_->PushAxisAlignedClip(D2D1::RectF(left - 4, input.top + 2, left + visible + 4, input.bottom - 2), D2D1_ANTIALIAS_MODE_ALIASED);
            if (focused && ui.selected) fill({left - offset - 3, input.top + 10, left - offset + full + 3, input.bottom - 10}, uiAccent(0.32f), 4);
            textLine(shown, left - offset, y, focused ? full + 40 : visible, value_font, gray(0.92f));
            target_->PopAxisAlignedClip();
        }
        if (focused && !ui.selected) line(left + caret - offset, input.top + 12, left + caret - offset, input.bottom - 12, accent, 2);
        if (layout.browses(index))
            button(layout.browse(index), index == wine_field ? std::wstring(L"Proton / Wine…") :
                   scriptField(index) ? std::wstring(ui.testing ? tr(L"Testowanie…") : tr(L"Testuj")) : std::wstring(tr(L"Wybierz…")));
    }

    for (int index = 0; index < ui.optionCount(); ++index) {
        // An option's card: its title, then a choice's list or a switch (and Aurelia's "…").
        const auto& option = ui.options[static_cast<size_t>(index)];
        const auto r = layout.option(index);
        card(r);
        const float y = middle(r);
        float right = r.right - 24;
        if (option.choice()) {
            const auto value = layout.optionValue(index);
            picker(value, option.value);
            right = value.left - 16;
        } else {
            if (option.more) {
                const auto more = layout.optionMore(index);
                drawButton(rect(more), 0, 1, false);
                brush(gray(0.86f));
                for (int dot = -1; dot <= 1; ++dot)
                    target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F((more.left + more.right) * 0.5f + 7.0f * static_cast<float>(dot), middle(more)), 2.2f, 2.2f), solid_.Get());
                right = more.left - 18;
            }
            toggle(right, y, option.on);
            right -= 66 + 148 + 12;
        }
        textLine(option.label, r.left + 24, y, right - r.left - 24, title_font, gray(0.93f));
    }

    if (layout.mods) {
        // Mody: where they go (an emulator's folder is its own), then a card per mod.
        const auto folder = layout.modsFolderRow();
        card(folder);
        const bool emulator = !ui.mod_target.empty();
        const float text_right = emulator ? folder.right - 24 : layout.modsFolderButton().left - 20;
        textGroup(emulator ? ui.mod_target : std::wstring(tr(L"Folder programu gry")), elideStart(ui.mod_folder, text_right - folder.left - 24, body),
                  folder.left + 24, middle(folder), text_right - folder.left - 24, title_font, gray(0.93f), body, gray(0.58f), 1);
        if (!emulator) button(layout.modsFolderButton(), tr(L"Zmień…"), !ui.mod_busy);
        for (int row = 0; row < ui.modCount(); ++row) {
            const auto& mod = ui.mod_rows[static_cast<size_t>(row)];
            const auto r = layout.modRow(row);
            card(r);
            const auto name = layout.modName(row);
            textGroup(mod.name, mod.status, name.left, middle(name), name.right - name.left, title_font,
                      mod.installed ? gray(0.96f) : gray(mod.available ? 0.88f : 0.48f), body,
                      mod.installed ? uiAccent(0.95f) : gray(mod.available ? 0.56f : 0.40f), 1);
            const auto method = layout.modMethod(row);
            if (mod.methods) picker(method, mod.method, !ui.mod_busy);
            else if (!mod.method.empty()) textLine(mod.method, method.left, middle(method), method.right - method.left - 16, body, gray(0.55f), DWRITE_TEXT_ALIGNMENT_TRAILING);
            button(layout.modAction(row), mod.action, mod.available && !ui.mod_busy);
            if (mod.removable) button(layout.modRemove(row), mod.confirm_remove ? tr(L"Potwierdź") : tr(L"Usuń"), !ui.mod_busy, true);
        }
    }

    if (ui.posterVisible()) {
        // The poster the typed name finds (or the game's own), "Zidentyfikuj grę" under it.
        bindCustomPoster();
        const auto box = layout.posterBox();
        const float radius = 14;
        fill(box, gray(0.03f), radius);
        const bool loading = ui.poster == Poster::loading;
        if (custom_poster_.bitmap) fillPoster(custom_poster_, rect(box), radius, loading ? 0.45f : 1.0f, false, nullptr);
        const wchar_t* caption = loading ? tr(L"Szukanie okładki…") : ui.poster == Poster::missing ? tr(L"Nie znaleziono okładki") :
                                 ui.poster == Poster::none ? tr(L"Okładka według nazwy") : nullptr;
        if (caption && (!custom_poster_.bitmap || loading)) {
            const auto alignment = body->GetTextAlignment();
            body->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            const float width = box.right - box.left - 32, height = paragraph(caption, box.left + 16, 0, width, body, gray(0), 3, false);
            paragraph(caption, box.left + 16, (box.top + box.bottom - height) * 0.5f, width, body, gray(custom_poster_.bitmap ? 0.92f : 0.46f), 3);
            body->SetTextAlignment(alignment);
        }
        outline(box, gray(1, 0.10f), 1.0f, radius);
        button(layout.identify(), tr(L"Zidentyfikuj grę"));
    }
    if (!ui.error.empty() || !ui.note.empty()) {
        // An error, else a script test's or a mod's outcome (red when it failed).
        const auto error = layout.error();
        text(ui.error.empty() ? ui.note : ui.error, error.left + 2, error.top, error.right - error.left, error.bottom - error.top,
             body, ui.error.empty() && !ui.note_failed ? gray(0.70f) : palette::danger());
    }
    if (ui.editing()) // Only the entry goes; the game's file stays (red: it cannot be undone).
        button(layout.button(Item::remove), ui.confirm_remove ? tr(L"Potwierdź usunięcie") : tr(L"Usuń z biblioteki"), true, true);
    button(layout.button(Item::cancel), tr(L"Anuluj"));
    button(layout.button(Item::save), tr(L"Zapisz"));

    if (ui.dropdown.open && !ui.dropdown.items.empty()) {
        // The list a choice or a method opened, over everything: the chosen value ticked.
        const auto box = layout.dropdownBox();
        for (int ring = 3; ring >= 1; --ring) { // A soft shadow.
            const float spread = 4.0f * static_cast<float>(ring);
            fill({box.left - spread, box.top - spread + 6, box.right + spread, box.bottom + spread + 6}, gray(0, 0.10f), 16 + spread);
        }
        fill(box, gray(0.12f), 16);
        outline(box, gray(1, 0.10f), 1.0f, 16);
        const int count = static_cast<int>(ui.dropdown.items.size()), first = layout.dropdownFirst();
        for (int item = first; item < std::min(count, first + Layout::drop_visible); ++item) {
            const auto r = layout.dropdownRow(item);
            if (item == ui.dropdown.focus) fill(r, gray(1, 0.08f), 12);
            const bool chosen = item == ui.dropdown.chosen;
            textLine(ui.dropdown.items[static_cast<size_t>(item)], r.left + 18, middle(r), r.right - r.left - 64, body, chosen ? gray(0.98f) : gray(0.80f));
            if (chosen) { // A tick.
                const float x = r.right - 28, y = middle(r);
                line(x - 7, y, x - 2, y + 5, accent, 2.2f);
                line(x - 2, y + 5, x + 7, y - 5, accent, 2.2f);
            }
        }
    }

    // The focus: one outline that springs from item to item (no fill, so nothing behind it flickers).
    std::optional<Rect> ring;
    if (const auto& glide = context_->editor_glide) ring = Rect{glide->left, glide->top, glide->right, glide->bottom};
    else ring = layout.focusRect(ui);
    if (ring) {
        const float h = ring->bottom - ring->top, radius = h <= 56 ? h * 0.5f : 16.0f;
        outline({ring->left - 4, ring->top - 4, ring->right + 4, ring->bottom + 4}, gray(0.96f), 2.5f, radius + 4);
    }
}
