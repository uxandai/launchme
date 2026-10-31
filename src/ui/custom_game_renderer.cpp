#include "renderer.hpp"

#include <algorithm>

void Renderer::bindCustomPoster() {
    if (custom_poster_.pixels != context_->custom_poster) { custom_poster_ = {}; custom_poster_.pixels = context_->custom_poster; }
    if (custom_poster_.loaded() && !custom_poster_.bitmap) createBitmap(custom_poster_); // Uploaded once decoded.
}

// Settings → Library: add/edit panel of a user-added game, in the settings panel style.
void Renderer::drawCustomGameEditor() {
    if (!context_->custom_editor || !context_->custom_editor->open) { if (custom_poster_.loaded()) custom_poster_ = {}; return; }
    if (desktopDialogs()) { drawDesktopCustomEditor(); return; } // Desktop mode: desktop_dialogs_renderer.cpp.
    using namespace custom_game_ui;
    const auto& ui = *context_->custom_editor;
    const auto layout = Layout::of(ui, viewport_.width, viewport_.height);
    const auto accent = uiAccent();
    const auto gray = [](float value, float alpha = 1.0f) { return D2D1::ColorF(value, value, value, alpha); };
    const auto rect = [](settings_ui::Rect r) { return D2D1::RectF(r.left, r.top, r.right, r.bottom); };
    const auto fill = [&](settings_ui::Rect r, D2D1_COLOR_F ink, float radius) {
        brush(ink);
        target_->FillRoundedRectangle(D2D1::RoundedRect(rect(r), radius, radius), solid_.Get());
    };
    const auto outline = [&](settings_ui::Rect r, D2D1_COLOR_F ink, float thickness, float radius) {
        brush(ink);
        target_->DrawRoundedRectangle(D2D1::RoundedRect(rect(r), radius, radius), solid_.Get(), thickness);
    };
    const auto measure = [&](const std::wstring& value) {
        if (value.empty()) return 0.0f;
        Microsoft::WRL::ComPtr<IDWriteTextLayout> text_layout;
        DWRITE_TEXT_METRICS metrics{};
        if (FAILED(dwrite_->CreateTextLayout(value.c_str(), static_cast<UINT32>(value.size()), settings_body_font_.Get(),
                                             100000.0f, 60.0f, text_layout.GetAddressOf())) ||
            FAILED(text_layout->GetMetrics(&metrics))) return 0.0f;
        return metrics.widthIncludingTrailingWhitespace;
    };
    // A glass pill on the opaque panel (no frosting); danger: the caption in the warning colour while unfocused.
    const auto button = [&](settings_ui::Rect r, const std::wstring& label, bool focused, bool danger = false) {
        const auto ink = drawButton(rect(r), focused ? 1.0f : 0.0f, 1, false);
        textLine(label, r.left + 8, (r.top + r.bottom) * 0.5f, r.right - r.left - 16, settings_body_font_.Get(),
                 danger && !focused ? palette::danger() : ink, DWRITE_TEXT_ALIGNMENT_CENTER);
    };

    brush(gray(0, 0.80f));
    target_->FillRectangle(D2D1::RectF(0, 0, viewport_.width, viewport_.height), solid_.Get());
    const auto panel = layout.panel();
    fill(panel, gray(0.07f), 18);
    if (panel_borders_) outline(panel, gray(1, 0.16f), 1.0f, 18);
    // Modal title: its capitals start at the panel's padding line.
    const std::wstring heading = ui.launchOptions() ? std::wstring(tr(L"Opcje uruchamiania")) + L" · " + ui.title :
                                 std::wstring(ui.editing() ? tr(L"Edytuj grę") : tr(L"Dodaj grę"));
    textGroup(heading, {}, panel.left + Layout::margin, panel.top + Layout::margin,
              panel.right - panel.left - 2 * Layout::margin, category_font_.Get(), gray(0.96f), settings_body_font_.Get(), gray(0.62f), 1, true);

    const std::array<const wchar_t*, field_count> labels{tr(L"Nazwa"), tr(L"Plik"), tr(L"Opcje uruchamiania"), tr(L"Folder roboczy"),
                                                         L"WINEDLLOVERRIDES", tr(L"Opcje Protona i Wine (jak w Steamie)"), tr(L"Platforma")};
    for (int index = 0; index < field_count; ++index) {
        if (!ui.shows(index)) continue;
        const auto label = layout.label(index);
        text(emulatedLabel(ui, index, labels[index]), label.left + 2, label.top, label.right - label.left, label.bottom - label.top, settings_body_font_.Get(), gray(0.62f));
        const auto input = layout.input(index);
        const bool focused = field(ui.focus) == index;
        // A pill field; the accent outline only while it takes typing.
        const float field_radius = (input.bottom - input.top) * 0.5f;
        fill(input, gray(0.03f), field_radius);
        if (focused) outline(input, accent, 1.5f, field_radius);
        const auto& value = ui.fields[index];
        const float left = input.left + 17, visible = input.right - input.left - 34, y = (input.top + input.bottom) * 0.5f;
        // Focused text scrolls to keep the caret visible. Unfocused, a long path keeps its
        // end (file name) behind a leading ellipsis; other text ends with one.
        const float full = measure(value);
        const float caret = focused ? measure(value.substr(0, std::min(ui.caret, value.size()))) : 0.0f;
        const bool path = index == file_field || index == directory_field;
        const float offset = focused ? std::max(0.0f, caret - visible + 2) : 0.0f;
        std::wstring shown = value;
        if (!focused && path && full > visible) {
            size_t low = 1, high = value.size(); // Characters dropped from the start.
            while (low < high) {
                const size_t middle = (low + high) / 2;
                if (measure(L"…" + value.substr(middle)) <= visible) high = middle; else low = middle + 1;
            }
            shown = L"…" + value.substr(low);
        }
        if (value.empty()) {
            if (const auto* hint = emulatedHint(ui, index)) textLine(hint, left, y, visible, settings_body_font_.Get(), gray(0.38f));
            else if (index == directory_field)
                textLine(tr(L"Domyślnie: folder pliku"), left, y, visible, settings_body_font_.Get(), gray(0.38f));
            else if (index == arguments_field && ui.launchOptions())
                textLine(tr(L"Np. -windowed -skipintro"), left, y, visible, settings_body_font_.Get(), gray(0.38f));
            else if (index == dll_field) textLine(tr(L"Np. dinput8=n,b;winhttp=n,b"), left, y, visible, settings_body_font_.Get(), gray(0.38f));
            else if (index == wine_field) textLine(tr(L"Np. PROTON_USE_WINED3D=1 gamemoderun %command% -dx11"), left, y, visible, settings_body_font_.Get(), gray(0.38f));
        } else {
            target_->PushAxisAlignedClip(D2D1::RectF(left - 4, input.top + 2, left + visible + 4, input.bottom - 2), D2D1_ANTIALIAS_MODE_ALIASED);
            if (focused && ui.selected)
                fill({left - offset - 3, input.top + 12, left - offset + full + 3, input.bottom - 12}, uiAccent(0.32f), 4);
            textLine(shown, left - offset, y, focused ? full + 40 : visible, settings_body_font_.Get(), gray(0.90f));
            target_->PopAxisAlignedClip();
        }
        if (focused && !ui.selected) line(left + caret - offset, input.top + 14, left + caret - offset, input.bottom - 14, gray(0.93f), 2);
        if (browseRow(index))
            button(layout.browse(index), index == wine_field ? std::wstring(L"Proton / Wine…") : std::wstring(tr(L"Wybierz…")), ui.focus == browseItem(index));
    }
    if (ui.posterShown()) {
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
            auto* font = settings_body_font_.Get();
            const auto alignment = font->GetTextAlignment();
            font->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            const float width = box.right - box.left - 32, height = paragraph(caption, box.left + 16, 0, width, font, gray(0), 3, false);
            paragraph(caption, box.left + 16, (box.top + box.bottom - height) * 0.5f, width, font, gray(custom_poster_.bitmap ? 0.92f : 0.46f), 3);
            font->SetTextAlignment(alignment);
        }
        outline(box, gray(1, ui.focus == Item::identify ? 0.32f : 0.10f), 1.0f, radius);
        button(layout.identify(), tr(L"Zidentyfikuj grę"), ui.focus == Item::identify);
    }
    if (!ui.error.empty()) {
        const auto error = layout.error();
        text(ui.error, error.left + 2, error.top, error.right - error.left, error.bottom - error.top,
             settings_body_font_.Get(), palette::danger());
    }
    if (ui.editing()) // Only the entry goes; the game's file stays (red: it cannot be undone).
        button(layout.button(Item::remove), ui.confirm_remove ? tr(L"Potwierdź usunięcie") : tr(L"Usuń z biblioteki"),
               ui.focus == Item::remove, true);
    button(layout.button(Item::cancel), tr(L"Anuluj"), ui.focus == Item::cancel);
    button(layout.button(Item::save), tr(L"Zapisz"), ui.focus == Item::save);
}
