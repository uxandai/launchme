// Desktop mode's dialogs over its pages (src/ui/desktop_sheet.hpp): Kolekcje (and Wersje), Dodaj /
// Edytuj grę and Zidentyfikuj grę as compact desktop dialogs. Their state and keys are Tryb TV's
// (library_manager_view.hpp, custom_game_view.hpp, identify_view.hpp); drawn in desktop DIPs, every
// control recording the area that answers for it (Renderer::desktopModalHotspots).
#include "renderer.hpp"
#include "renderer_internal.hpp"
#include "exit_prompt.hpp"
#include "../store/epic_account.hpp"
#include "../store/amazon.hpp"
#include "../store/xbox_account.hpp"

#include <algorithm>
#include <array>
#include <cmath>

using namespace desktop_ui;
using Kind = desktop_ui::SheetHit::Kind;

namespace {
using palette::gray;
D2D1_COLOR_F rgb(float r, float g, float b, float a = 1) { return D2D1::ColorF(r, g, b, a); }
float middle(Rect r) { return (r.top + r.bottom) * 0.5f; }
Rect inflate(Rect r, float by) { return {r.left - by, r.top - by, r.right + by, r.bottom + by}; }
const D2D1_COLOR_F dialog_bg = rgb(0.086f, 0.092f, 0.106f);
bool caretOn() { return (GetTickCount64() / 530) % 2 == 0; }
}

bool Renderer::desktopDialogs() const {
    return context_ && context_->desktop_window && (context_->desktop || context_->desktop_sheet);
}

// The dim, the panel centred in the window, its title and close button (and Back on inner pages).
Rect Renderer::desktopDialogFrame(const Viewport& view, float width, float height, const std::wstring& title, bool back) {
    desktop_modal_hotspots_.clear(); // Only the topmost dialog answers the pointer.
    // Dimmed as Windows dialogs are: the window behind stays recognisable but no longer competes.
    brush(rgb(0.01f, 0.012f, 0.016f, 0.36f));
    target_->FillRectangle(D2D1::RectF(0, 0, view.width, view.height), solid_.Get());
    width = std::min(width, view.width - 48);
    height = std::min(height, view.height - 48);
    const float left = std::round((view.width - width) * 0.5f), top = std::round((view.height - height) * 0.5f);
    const Rect p{left, top, left + width, top + height};
    desktopShadow(p, 14, 1.4f);
    desktopFill(p, 14, dialog_bg);
    desktopStroke(p, 14, gray(1, 0.1f));
    desktopModalHot({Kind::dialog}, p);
    const auto& hover = context_->desktop_dialog_hover;
    float title_left = p.left + 22;
    if (back) {
        const Rect b{p.left + 12, p.top + 10, p.left + 48, p.top + 46};
        if (hover.kind == Kind::back) desktopFill(b, 8, gray(1, 0.08f));
        desktopGlyph(0xE72B, (b.left + b.right) * 0.5f, middle(b), 14, gray(hover.kind == Kind::back ? 0.98f : 0.8f));
        desktopModalHot({Kind::back}, b);
        title_left = b.right + 8;
    }
    textLine(title, title_left, p.top + 28, p.right - title_left - 60, desktop_title_font_.Get(), palette::text());
    const Rect c{p.right - 48, p.top + 10, p.right - 12, p.top + 46};
    if (hover.kind == Kind::close) desktopFill(c, 8, gray(1, 0.08f));
    desktopGlyph(glyph::close, (c.left + c.right) * 0.5f, middle(c), 12, gray(hover.kind == Kind::close ? 0.98f : 0.72f));
    desktopModalHot({Kind::close}, c);
    brush(gray(1, 0.06f));
    target_->FillRectangle(D2D1::RectF(p.left + 1, p.top + sheet_header - 1, p.right - 1, p.top + sheet_header), solid_.Get());
    return p;
}

// A one-line text field: its text (or placeholder), the selection and the caret.
size_t Renderer::desktopCaretAt(const std::wstring& value, float x, bool title_font) {
    auto* format = title_font ? desktop_title_font_.Get() : desktop_font_.Get();
    if (x <= 0 || value.empty()) return 0;
    float previous = 0;
    for (size_t i = 1; i <= value.size(); ++i) {
        const float at = textAdvance(value.substr(0, i), format);
        if (x < (previous + at) * 0.5f) return i - 1;
        previous = at;
    }
    return value.size();
}

void Renderer::desktopSelection(const std::wstring& value, float left, float cy, float width, IDWriteTextFormat* format) {
    const size_t begin = context_ ? context_->desktop_text_begin : 0, end = context_ ? context_->desktop_text_end : 0;
    if (begin >= end || end > value.size()) return;
    const float from = textAdvance(value.substr(0, begin), format), to = textAdvance(value.substr(0, end), format);
    const float x0 = left + std::min(from, width), x1 = left + std::min(to, width);
    if (x1 > x0) desktopFill({x0 - 1, cy - 11, x1 + 1, cy + 11}, 3, uiAccent(0.35f));
}

void Renderer::desktopTextField(Rect f, const std::wstring& value, const std::wstring& placeholder, size_t caret, bool focused, bool selected, float hover) {
    desktopFill(f, 8, gray(0, 0.22f));
    if (focused) desktopStroke(inflate(f, 1), 9, uiAccent(0.9f), 1.5f);
    else desktopStroke(f, 8, gray(1, 0.1f + 0.08f * hover));
    const auto m = typeMetrics(desktop_font_.Get());
    const float left = f.left + 12, width = f.right - f.left - 24;
    if (value.empty()) {
        textLine(placeholder, left, middle(f), width, desktop_font_.Get(), palette::muted(0.8f));
        if (focused && caretOn()) text(L"│", left - 1, middle(f) - m.line * 0.5f, 8, m.line, desktop_font_.Get(), palette::text());
        return;
    }
    if (focused && selected) {
        const float w = std::min(textAdvance(value, desktop_font_.Get()), width);
        desktopFill({left - 2, middle(f) - 11, left + w + 2, middle(f) + 11}, 3, uiAccent(0.35f));
    } else if (focused) desktopSelection(value, left, middle(f), width, desktop_font_.Get());
    fieldText(value, std::min(caret, value.size()), focused && !selected && caretOn(), left, middle(f) - m.line * 0.5f, width, m.line,
              desktop_font_.Get(), palette::text());
}

// ---- Kolekcje / Wersje ---------------------------------------------------------------------------

void Renderer::drawDesktopCollectionsDialog() {
    using library_manager_ui::Page;
    const auto& ui = *context_->library_manager;
    const auto& hover = context_->desktop_dialog_hover;
    const auto view = desktopViewport();
    D2D1_MATRIX_3X2_F saved{};
    target_->GetTransform(&saved);
    target_->SetTransform(D2D1::Matrix3x2F::Scale(view.scale, view.scale));
    const bool versions = ui.page == Page::versions, field = ui.textField();
    const int count = versions ? static_cast<int>(ui.versions.size()) : static_cast<int>(ui.rows.size());
    const float line_h = 50, card_h = 92;
    const float chrome = sheet_header + 24 + (field ? 52.0f : 0.0f) + (ui.error.empty() ? 0.0f : 28.0f) + (versions ? 58.0f : 0.0f);
    const float most = std::min(660.0f, view.height - 80);
    const int capacity = std::max(2, static_cast<int>((most - chrome) / (versions ? card_h + 10 : line_h)));
    const int needed = versions ? (count + 1) / 2 : count;
    const int shown = std::clamp(needed, 1, capacity);
    const float height = chrome + static_cast<float>(shown) * (versions ? card_h + 10 : line_h);
    const auto p = desktopDialogFrame(view, versions ? 640.0f : 560.0f, height, ui.title, ui.page != Page::collections);
    float y = p.top + sheet_header + 14;
    if (field) {
        const Rect f{p.left + 20, y, p.right - 20, y + 40};
        desktopTextField(f, ui.input, ui.page == Page::name ? tr(L"Nazwa") : tr(L"Szukaj"), ui.input.size(), ui.focus == -2, ui.selected,
                         hover.kind == Kind::field ? 1.0f : 0.0f);
        desktopModalHot({Kind::field, -2}, f);
        y += 52;
    }
    if (versions) {
        // The game's copies as cards, two per row; then "Zapamiętaj wybór".
        int first = std::clamp(ui.scroll, 0, std::max(0, count - capacity * 2));
        if (ui.focus >= 0 && ui.focus < count) {
            if (ui.focus < first) first = ui.focus - ui.focus % 2;
            if (ui.focus >= first + capacity * 2) first = ui.focus - ui.focus % 2 - (capacity - 1) * 2;
        }
        const float w = (p.right - p.left - 40 - 12) * 0.5f;
        for (int i = first; i < std::min(count, first + capacity * 2); ++i) {
            const auto& game = ui.versions[static_cast<size_t>(i)];
            const float x = p.left + 20 + static_cast<float>((i - first) % 2) * (w + 12);
            const float top = y + static_cast<float>((i - first) / 2) * (card_h + 10);
            const Rect r{x, top, x + w, top + card_h};
            const bool focused = ui.focus == i, over = hover == SheetHit{Kind::row, i};
            desktopFill(r, 10, gray(1, focused ? 0.085f : over ? 0.06f : 0.035f));
            desktopStroke(r, 10, focused ? uiAccent(0.8f) : gray(1, 0.07f), focused ? 1.5f : 1.0f);
            std::wstring name = sourceName(game.source);
            if (!game.platform.empty() && game.platform != name) name += L"  ·  " + game.platform;
            textLine(name, r.left + 16, r.top + 30, r.right - r.left - 32, desktop_strong_font_.Get(), palette::text());
            textLine(game.name, r.left + 16, r.top + 52, r.right - r.left - 32, desktop_small_font_.Get(), palette::muted());
            textLine(game.installed ? std::wstring(tr(L"Graj")) : primaryActionLabel(game), r.left + 16, r.bottom - 18, r.right - r.left - 32,
                     desktop_small_font_.Get(), uiAccent(0.9f));
            desktopModalHot({Kind::row, i}, r);
        }
        const Rect remember{p.left + 20, p.bottom - 58, p.right - 20, p.bottom - 18};
        const int remember_index = count;
        const bool focused = ui.focus == remember_index, over = hover == SheetHit{Kind::row, remember_index};
        if (focused || over) desktopFill(remember, 8, gray(1, focused ? 0.07f : 0.045f));
        const Rect box{remember.left + 10, middle(remember) - 9, remember.left + 28, middle(remember) + 9};
        desktopFill(box, 4, ui.remember ? uiAccent() : gray(1, 0.06f));
        if (!ui.remember) desktopStroke(box, 4, gray(1, 0.3f));
        else desktopGlyph(glyph::check, (box.left + box.right) * 0.5f, middle(box), 10, gray(0.06f));
        textLine(tr(L"Zapamiętaj wybór"), box.right + 12, middle(remember), remember.right - box.right - 24, desktop_font_.Get(), palette::text());
        desktopModalHot({Kind::row, remember_index}, remember);
    } else {
        // Rows; the focused one (keys, pad or the last click) keeps its fill.
        int first = std::clamp(ui.scroll, 0, std::max(0, count - capacity));
        if (ui.focus >= 0 && ui.focus < count) {
            if (ui.focus < first) first = ui.focus;
            if (ui.focus >= first + capacity) first = ui.focus - capacity + 1;
        }
        for (int i = first; i < std::min(count, first + capacity); ++i) {
            const auto& row = ui.rows[static_cast<size_t>(i)];
            const float top = y + static_cast<float>(i - first) * line_h;
            const Rect r{p.left + 12, top, p.right - 12, top + line_h - 4};
            const bool focused = ui.focus == i, over = hover == SheetHit{Kind::row, i};
            const bool confirming = focused && ui.confirm;
            if (confirming) desktopFill(r, 8, rgb(0.86f, 0.26f, 0.22f, 0.16f));
            else if (focused || over) desktopFill(r, 8, gray(1, focused ? 0.075f : 0.045f));
            float right = r.right - 12;
            if (row.toggle) {
                const float cx = r.right - 22, cy = middle(r);
                if (row.radio) {
                    brush(row.checked ? uiAccent() : gray(1, 0.35f));
                    target_->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), 9, 9), solid_.Get(), 1.5f);
                    if (row.checked) target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), 5, 5), solid_.Get());
                } else {
                    const Rect box{cx - 9, cy - 9, cx + 9, cy + 9};
                    desktopFill(box, 4, row.checked ? uiAccent() : gray(1, 0.05f));
                    if (!row.checked) desktopStroke(box, 4, gray(1, 0.3f));
                    else {
                        const auto a = uiAccent();
                        desktopGlyph(glyph::check, cx, cy, 10, 0.2126f * a.r + 0.7152f * a.g + 0.0722f * a.b > 0.6f ? gray(0.06f) : gray(0.98f));
                    }
                }
                right = cx - 22;
            }
            const auto ink = confirming ? palette::danger() : palette::text();
            if (row.subtitle.empty()) textLine(row.title, r.left + 12, middle(r), right - r.left - 12, desktop_font_.Get(), ink);
            else {
                textLine(row.title, r.left + 12, r.top + 16, right - r.left - 12, desktop_font_.Get(), ink);
                textLine(row.subtitle, r.left + 12, r.top + 34, right - r.left - 12, desktop_small_font_.Get(), palette::muted());
            }
            desktopModalHot({Kind::row, i}, r);
        }
        if (count > capacity) {
            const float track = static_cast<float>(capacity) * line_h - 8, length = std::max(24.0f, track * static_cast<float>(capacity) / static_cast<float>(count));
            const float top = y + (track - length) * static_cast<float>(first) / static_cast<float>(count - capacity);
            desktopFill({p.right - 7, top, p.right - 4, top + length}, 1.5f, gray(1, 0.22f));
        }
        if (ui.page == Page::excluded && ui.rows.empty())
            textLine(ui.input.empty() ? tr(L"Brak wykluczonych gier") : tr(L"Brak wyników"), p.left + 24, y + 22, p.right - p.left - 48,
                     desktop_font_.Get(), palette::muted());
    }
    if (!ui.error.empty()) textLine(ui.error, p.left + 22, p.bottom - 20, p.right - p.left - 44, desktop_small_font_.Get(), palette::danger());
    desktop_modal_ = DialogKind::collections;
    target_->SetTransform(saved);
}

// ---- Dodaj / Edytuj grę --------------------------------------------------------------------------

void Renderer::drawDesktopCustomEditor() {
    using custom_game_ui::Item;
    const auto& ui = *context_->custom_editor;
    const auto& hover = context_->desktop_dialog_hover;
    const auto view = desktopViewport();
    D2D1_MATRIX_3X2_F saved{};
    target_->GetTransform(&saved);
    target_->SetTransform(D2D1::Matrix3x2F::Scale(view.scale, view.scale));
    const float field_step = 74;
    int fields = 0;
    for (int index = 0; index < custom_game_ui::field_count; ++index) fields += ui.shows(index);
    // Launch options of a library game: its name under the panel's title.
    const float subtitle = ui.launchOptions() ? 24.0f : 0.0f;
    // A user-added game: its poster (matched by the typed name) and "Zidentyfikuj grę" on the right.
    const bool poster = ui.posterShown();
    constexpr float poster_w = 150, poster_column = poster_w + 22;
    // A new game: "Wskaż plik" / "Wskaż folder" above the fields.
    const float pills = ui.pills() ? 52.0f : 0.0f;
    const float height = sheet_header + 16 + subtitle + pills + static_cast<float>(fields) * field_step + (ui.error.empty() ? 0.0f : 26.0f) + 64;
    const float width = ui.compat ? 760.0f : 620.0f; // A Windows game on Linux: its Wine/Proton options are long.
    const auto p = desktopDialogFrame(view, poster ? width + poster_column : width, height,
                                      ui.launchOptions() ? tr(L"Opcje uruchamiania") : ui.editing() ? tr(L"Edytuj grę") : tr(L"Dodaj grę"), false);
    const float fields_right = p.right - 22 - (poster ? poster_column : 0.0f);
    const std::array<const wchar_t*, custom_game_ui::field_count> labels{trKey(L"Nazwa"), trKey(L"Plik"), trKey(L"Opcje uruchamiania"), trKey(L"Folder roboczy"),
                                                                         L"WINEDLLOVERRIDES", trKey(L"Opcje Protona i Wine (jak w Steamie)"), trKey(L"Platforma")};
    const int focused_field = custom_game_ui::field(ui.focus);
    float y = p.top + sheet_header + 14;
    if (poster) {
        bindCustomPoster();
        const Rect box{p.right - 22 - poster_w, y + 20, p.right - 22, y + 20 + poster_w * 1.5f};
        const bool loading = ui.poster == custom_game_ui::Poster::loading;
        desktopFill(box, 10, gray(1, 0.045f));
        if (custom_poster_.bitmap) fillPoster(custom_poster_, D2D1::RectF(box.left, box.top, box.right, box.bottom), 10, loading ? 0.45f : 1.0f, false, nullptr);
        const wchar_t* caption = loading ? tr(L"Szukanie okładki…") : ui.poster == custom_game_ui::Poster::missing ? tr(L"Nie znaleziono okładki") :
                                 ui.poster == custom_game_ui::Poster::none ? tr(L"Okładka według nazwy") : nullptr;
        if (caption && (!custom_poster_.bitmap || loading)) {
            auto* font = desktop_small_font_.Get();
            const auto alignment = font->GetTextAlignment();
            font->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            const float caption_width = box.right - box.left - 20, lines = paragraph(caption, box.left + 10, 0, caption_width, font, gray(0), 3, false);
            paragraph(caption, box.left + 10, middle(box) - lines * 0.5f, caption_width, font, custom_poster_.bitmap ? palette::text() : palette::muted(), 3);
            font->SetTextAlignment(alignment);
        }
        desktopStroke(box, 10, gray(1, hover == SheetHit{Kind::action, static_cast<int>(Item::identify)} ? 0.28f : 0.1f));
        desktopModalHot({Kind::action, static_cast<int>(Item::identify)}, box); // A click on it identifies the game too.
        const Rect b{box.left, box.bottom + 10, box.right, box.bottom + 46};
        desktopButton(b, 1, hover == SheetHit{Kind::action, static_cast<int>(Item::identify)} ? 1.0f : 0.0f, false, true, glyph::search, tr(L"Zidentyfikuj grę"));
        if (ui.focus == Item::identify) desktopFocusRing(b, 8);
        desktopModalHot({Kind::action, static_cast<int>(Item::identify)}, b);
    }
    if (ui.launchOptions()) { textLine(ui.title, p.left + 22, y + 6, p.right - p.left - 44, desktop_strong_font_.Get(), palette::text()); y += subtitle; }
    if (pills) {
        const float w = (fields_right - p.left - 22 - 10) * 0.5f;
        const auto pill = [&](Item item, float left, wchar_t icon, const wchar_t* label) {
            const Rect r{left, y + 8, left + w, y + 44};
            desktopButton(r, 1, hover == SheetHit{Kind::action, static_cast<int>(item)} ? 1.0f : 0.0f, false, true, icon, label);
            if (ui.focus == item) desktopFocusRing(r, 8);
            desktopModalHot({Kind::action, static_cast<int>(item)}, r);
        };
        pill(Item::pick_file, p.left + 22, glyph::folder_open, tr(L"Wskaż plik"));
        pill(Item::pick_folder, p.left + 22 + w + 10, glyph::folder_open, tr(L"Wskaż folder"));
        y += pills;
    }
    for (int index = 0; index < custom_game_ui::field_count; ++index) {
        if (!ui.shows(index)) continue;
        textLine(custom_game_ui::emulatedLabel(ui, index, tr(labels[static_cast<size_t>(index)])), p.left + 22, y + 8, fields_right - p.left - 22, desktop_label_font_.Get(), gray(0.62f));
        const bool browse = custom_game_ui::browseRow(index) && ui.browses(index);
        const bool page = index == custom_game_ui::wine_field; // "Proton / Wine…": the game's page.
        const float field_right = fields_right - (browse ? (page ? 170.0f : 120.0f) : 0.0f);
        const Rect f{p.left + 22, y + 20, field_right, y + 60};
        const wchar_t* placeholder = custom_game_ui::emulatedHint(ui, index) ? custom_game_ui::emulatedHint(ui, index) : index == custom_game_ui::directory_field ? tr(L"Domyślnie: folder pliku") :
            index == custom_game_ui::arguments_field && ui.launchOptions() ? tr(L"Np. -windowed -skipintro") :
            index == custom_game_ui::dll_field ? tr(L"Np. dinput8=n,b;winhttp=n,b") :
            page ? tr(L"Np. PROTON_USE_WINED3D=1 gamemoderun %command% -dx11") : L"";
        desktopTextField(f, ui.fields[static_cast<size_t>(index)], placeholder,
                         ui.caret, focused_field == index, ui.selected, hover == SheetHit{Kind::field, index} ? 1.0f : 0.0f);
        desktopModalHot({Kind::field, index}, f);
        if (browse) {
            const Rect b{field_right + 10, f.top, fields_right, f.bottom};
            const Item item = custom_game_ui::browseItem(index);
            desktopButton(b, 1, hover == SheetHit{Kind::tile_action, index} ? 1.0f : 0.0f, false, true, page ? glyph::settings : glyph::folder_open,
                          page ? std::wstring(L"Proton / Wine…") : std::wstring(tr(L"Wybierz")));
            if (ui.focus == item) desktopFocusRing(b, 8);
            desktopModalHot({Kind::tile_action, index}, b);
        }
        y += field_step;
    }
    if (!ui.error.empty()) { textLine(ui.error, p.left + 22, y + 6, fields_right - p.left - 22, desktop_small_font_.Get(), palette::danger()); y += 26; }
    const float by = p.bottom - 20 - 36;
    const auto button = [&](Item item, Rect r, int style, wchar_t icon, const std::wstring& label) {
        desktopButton(r, style, hover == SheetHit{Kind::action, static_cast<int>(item)} ? 1.0f : 0.0f, false, true, icon, label);
        if (ui.focus == item) desktopFocusRing(r, 8);
        desktopModalHot({Kind::action, static_cast<int>(item)}, r);
    };
    button(Item::save, {p.right - 22 - 120, by, p.right - 22, by + 36}, 2, 0, tr(L"Zapisz"));
    button(Item::cancel, {p.right - 22 - 120 - 10 - 110, by, p.right - 22 - 130, by + 36}, 1, 0, tr(L"Anuluj"));
    if (ui.editing()) { // Only the entry goes; the game's file stays.
        const auto label = ui.confirm_remove ? std::wstring(tr(L"Potwierdź usunięcie")) : std::wstring(tr(L"Usuń z biblioteki"));
        const Rect r{p.left + 22, by, p.left + 22 + std::max(110.0f, textAdvance(label, desktop_button_font_.Get()) + 52), by + 36};
        button(Item::remove, r, 1, glyph::remove, label);
        desktopStroke(r, 8, palette::danger(ui.confirm_remove ? 0.8f : 0.45f), ui.confirm_remove ? 1.5f : 1.0f);
    }
    desktop_modal_ = DialogKind::custom_editor;
    target_->SetTransform(saved);
}

// ---- Zidentyfikuj grę ----------------------------------------------------------------------------

void Renderer::drawDesktopIdentify() {
    const auto& state = *context_->identify;
    const auto& hover = context_->desktop_dialog_hover;
    const auto view = desktopViewport();
    D2D1_MATRIX_3X2_F saved{};
    target_->GetTransform(&saved);
    target_->SetTransform(D2D1::Matrix3x2F::Scale(view.scale, view.scale));
    const auto p = desktopDialogFrame(view, 660, std::min(640.0f, view.height - 80), tr(L"Zidentyfikuj grę"), false);
    // RAWG, or for emulated games the switch between RAWG and ScreenScraper.
    {
        const std::wstring label = !state.emulated ? L"RAWG ↗" : state.source == identify_ui::State::Source::screenscraper ? L"ScreenScraper ⇄" : L"RAWG ⇄";
        const float w = textAdvance(label, desktop_button_font_.Get()) + 28;
        const Rect r{p.right - 56 - w, p.top + 12, p.right - 56, p.top + 44};
        desktopButton(r, 1, hover.kind == Kind::toggle ? 1.0f : 0.0f, false, true, 0, label);
        desktopModalHot({Kind::toggle}, r);
    }
    float y = p.top + sheet_header + 14;
    if (!state.game.name.empty()) {
        textLine(state.game.name, p.left + 22, y + 8, p.right - p.left - 44, desktop_small_font_.Get(), palette::muted());
        y += 22;
    }
    if (const auto path = identify_ui::gamePath(state.game); !path.empty()) { // What it is on disk: its ROM, program or folder.
        textLine(elideStart(path, p.right - p.left - 44, desktop_small_font_.Get()), p.left + 22, y + 8, p.right - p.left - 44, desktop_small_font_.Get(), gray(0.5f));
        y += 22;
    }
    const Rect search{p.left + 20, y, p.right - 20 - 104, y + 40};
    desktopTextField(search, state.query, tr(L"Szukaj"), state.query.size(), state.query_focus, state.select_all, hover.kind == Kind::field ? 1.0f : 0.0f);
    desktopModalHot({Kind::field}, search);
    const Rect submit{p.right - 20 - 94, y, p.right - 20, y + 40};
    desktopButton(submit, 2, hover.kind == Kind::action ? 1.0f : 0.0f, false, !state.busy, glyph::search, tr(L"Szukaj"));
    if (!state.busy) desktopModalHot({Kind::action}, submit);
    y += 54;
    const float item_h = 58, bottom = p.bottom - (state.status.empty() ? 16.0f : 44.0f);
    const int count = static_cast<int>(state.games.size());
    const int capacity = std::max(1, static_cast<int>((bottom - y) / item_h));
    const int first = std::clamp(state.selected - capacity + 1, 0, std::max(0, count - capacity));
    for (int i = first; i < std::min(count, first + capacity); ++i) {
        const auto& game = state.games[static_cast<size_t>(i)];
        const Rect r{p.left + 12, y, p.right - 12, y + item_h - 6};
        const bool focused = !state.query_focus && !state.back_focus && state.selected == i, over = hover == SheetHit{Kind::row, i};
        if (focused || over) desktopFill(r, 8, gray(1, focused ? 0.08f : 0.045f));
        auto subtitle = game.released;
        if (!subtitle.empty() && !game.platforms.empty()) subtitle += L"  ·  ";
        subtitle += game.platforms;
        textLine(game.name, r.left + 12, r.top + 17, r.right - r.left - 24, desktop_strong_font_.Get(), palette::text());
        textLine(subtitle, r.left + 12, r.top + 36, r.right - r.left - 24, desktop_small_font_.Get(), palette::muted());
        if (!state.busy && !state.applying) desktopModalHot({Kind::row, i}, r);
        y += item_h;
    }
    if (!state.status.empty()) textLine(state.status, p.left + 22, p.bottom - 22, p.right - p.left - 44, desktop_small_font_.Get(), palette::muted());
    desktop_modal_ = DialogKind::identify;
    target_->SetTransform(saved);
}

// ---- Zasilanie and the question before closing ------------------------------------------------------

void Renderer::drawDesktopPrompts() {
    const auto& hover = context_->desktop_dialog_hover;
    const auto view = desktopViewport();
    D2D1_MATRIX_3X2_F saved{};
    target_->GetTransform(&saved);
    target_->SetTransform(D2D1::Matrix3x2F::Scale(view.scale, view.scale));
    if (context_->power_menu >= 0) {
        // Zasilanie: one row per choice, the highlighted one (keys, pad) filled; the groups apart
        // (a hairline between them) and the PC's power actions in red.
        const auto& labels = context_->power_labels;
        const float item_h = 42, band = 13;
        const auto gap = [&](int i) { return i > 0 && i < static_cast<int>(context_->power_gaps.size()) && context_->power_gaps[static_cast<size_t>(i)]; };
        const auto danger = [&](int i) { return i >= 0 && i < static_cast<int>(context_->power_danger.size()) && context_->power_danger[static_cast<size_t>(i)]; };
        float bands = 0;
        for (int i = 0; i < static_cast<int>(labels.size()); ++i) if (gap(i)) bands += band;
        const auto p = desktopDialogFrame(view, 360, sheet_header + 20 + static_cast<float>(labels.size()) * item_h + bands, tr(L"Zasilanie"), false);
        float y = p.top + sheet_header + 10;
        for (int i = 0; i < static_cast<int>(labels.size()); ++i) {
            if (gap(i)) {
                line(p.left + 24, y + band * 0.5f - 2, p.right - 24, y + band * 0.5f - 2, gray(1, 0.08f), 1);
                y += band;
            }
            const Rect r{p.left + 10, y, p.right - 10, y + item_h - 4};
            const bool focused = context_->power_menu == i, over = hover == SheetHit{Kind::row, i};
            if (focused || over) {
                const auto red = palette::dangerStrong(focused ? 0.16f : 0.1f);
                desktopFill(r, 8, danger(i) ? red : gray(1, focused ? 0.08f : 0.05f));
            }
            textLine(labels[static_cast<size_t>(i)], r.left + 14, middle(r), r.right - r.left - 28, desktop_font_.Get(),
                     danger(i) ? palette::danger() : palette::text());
            desktopModalHot({Kind::row, i}, r);
            y += item_h;
        }
        desktop_modal_ = DialogKind::power_menu;
    } else if (context_->exit_prompt >= 0) {
        // The question, then its answers side by side; the first is the suggested one.
        const std::wstring question = context_->prompt_question.empty() ? std::wstring(tr(exit_prompt::question)) : context_->prompt_question;
        const bool body = !context_->prompt_body.empty();
        const auto p = desktopDialogFrame(view, 600, sheet_header + (body ? 150.0f : 110.0f), question, false);
        if (body) paragraph(context_->prompt_body, p.left + 22, p.top + sheet_header + 18, p.right - p.left - 44, desktop_font_.Get(), palette::muted(), 2);
        // Buttons as wide as their labels (equal thirds cut "Ukryj na ekranie gło…"), right-aligned
        // as Windows dialogs; equal widths again only when they would not fit.
        const auto labelOf = [&](int i) {
            return context_->prompt_question.empty() ? std::wstring(tr(exit_prompt::labels[static_cast<size_t>(i)])) : context_->prompt_labels[static_cast<size_t>(i)];
        };
        float widths[exit_prompt::count]{}, total = 10.0f * (exit_prompt::count - 1);
        for (int i = 0; i < exit_prompt::count; ++i) total += widths[i] = std::max(104.0f, textAdvance(labelOf(i), desktop_button_font_.Get()) + 40);
        const float available = p.right - p.left - 40;
        if (total > available) for (auto& w : widths) w = (available - 10.0f * (exit_prompt::count - 1)) / exit_prompt::count;
        float bx = total > available ? p.left + 20 : p.right - 20 - total;
        const float by = p.bottom - 24 - 38;
        for (int i = 0; i < exit_prompt::count; ++i) {
            const Rect r{bx, by, bx + widths[i], by + 38};
            bx += widths[i] + 10;
            const auto label = labelOf(i);
            if (context_->prompt_danger && i == 0) {
                // A destructive first answer (Usuń kolekcję): an outlined button with red text, never the primary one.
                desktopButton(r, 1, hover == SheetHit{Kind::row, i} ? 1.0f : 0.0f, false, true, 0, {});
                textLine(label, r.left + 8, (r.top + r.bottom) * 0.5f, r.right - r.left - 16, desktop_button_font_.Get(), palette::danger(), DWRITE_TEXT_ALIGNMENT_CENTER);
            } else desktopButton(r, i == 0 ? 2 : 1, hover == SheetHit{Kind::row, i} ? 1.0f : 0.0f, false, true, 0, label);
            if (context_->exit_prompt == i) desktopFocusRing(r, 8);
            desktopModalHot({Kind::row, i}, r);
        }
        desktop_modal_ = DialogKind::exit_prompt;
    }
    target_->SetTransform(saved);
}
// ---- Instalacja GOG / Epic --------------------------------------------------------------------------

void Renderer::drawDesktopGogInstall() {
    const auto& ui = *context_->gog_install;
    const auto& state = ui.snapshot;
    const auto& hover = context_->desktop_dialog_hover;
    const bool epic = gog_install_ui::helperInstall(ui.game.source), steam = ui.steam;
    const bool xbox = ui.game.source == GameSource::xbox;
    const bool amazon = ui.game.source == GameSource::amazon || xbox; // Nile and the Xbox helper report bytes, Legendary percent.
    const auto view = desktopViewport();
    D2D1_MATRIX_3X2_F saved{};
    target_->GetTransform(&saved);
    target_->SetTransform(D2D1::Matrix3x2F::Scale(view.scale, view.scale));
    const float item_h = 62;
    const int settings_rows = epic || steam ? 2 : 3;
    const bool downloading = state.state == gog::InstallState::downloading;
    const float height = sheet_header + 70 + static_cast<float>(settings_rows) * item_h + (downloading ? 44.0f : 0.0f) + 70;
    const auto p = desktopDialogFrame(view, 620, height, ui.game.name, false);
    float y = p.top + sheet_header + 14;
    // Its state, with a turning disc while the store works.
    const bool busy = state.busy() || (steam && ui.steam_busy);
    const auto status = steam ? gog_install_ui::steamInstallStatus(ui) : epic && !amazon ? epic::installStatusText(state) : gog::installStatusText(state);
    if (busy) desktopArc(p.left + 30, y + 14, 8, elapsed_ * 5.0f, 4.2f, uiAccent(), 2);
    paragraph(status, p.left + (busy ? 48.0f : 22.0f), y + 2, p.right - p.left - 70, desktop_font_.Get(), palette::muted(), 2);
    y += 56;
    // Language / version (Steam: its library), the game's folder (Steam: Steam's own install window)
    // and the download folder: a click changes each.
    std::wstring variant = tr(L"Brak dostępnych wariantów");
    if (ui.variant >= 0 && ui.variant < static_cast<int>(state.choices.size())) {
        const auto& choice = state.choices[static_cast<size_t>(ui.variant)];
        variant = choice.language + (choice.version.empty() ? L"" : L" · " + choice.version) + L" · " + std::to_wstring(choice.bytes / (1024 * 1024)) + L" MiB";
    }
    const std::array<std::wstring, 3> labels = steam ?
        std::array<std::wstring, 3>{tr(L"Biblioteka Steam"), tr(L"Okno instalacji Steam"), {}} :
        std::array<std::wstring, 3>{xbox ? std::wstring(L"Xbox · Windows (Xodus)") : amazon ? std::wstring(L"Amazon Games · Windows") : epic ? std::wstring(L"Epic Games · Windows") : std::wstring(tr(L"Język / wersja")), tr(L"Folder gry"), tr(L"Folder pobierania (tymczasowo)")};
    const std::array<std::wstring, 3> values = steam ?
        std::array<std::wstring, 3>{gog_install_ui::steamLibraryText(ui), tr(L"Wybór biblioteki i instalacja w samym Steamie"), {}} :
        std::array<std::wstring, 3>{xbox ? L"launchme-xbox " + std::wstring(xbox_account::helper_version) : amazon ? L"Nile " + std::wstring(amazon::helper_version) : epic ? std::wstring(L"Legendary 0.21.1") : variant, ui.request.install_directory.wstring(), ui.request.download_root.wstring()};
    for (int i = 0; i < 3; ++i) {
        if ((epic || steam) && i == 2) continue;
        const Rect r{p.left + 16, y, p.right - 16, y + item_h - 8};
        const bool focused = ui.row == i, over = hover == SheetHit{Kind::row, i};
        desktopFill(r, 10, gray(1, focused ? 0.075f : over ? 0.055f : 0.035f));
        textLine(labels[static_cast<size_t>(i)], r.left + 14, r.top + 18, r.right - r.left - 50, desktop_small_font_.Get(), palette::muted());
        textLine(values[static_cast<size_t>(i)], r.left + 14, r.top + 38, r.right - r.left - 50, desktop_font_.Get(), palette::text());
        desktopGlyph(i == 0 ? glyph::chevron_down : steam ? glyph::open : glyph::folder_open, r.right - 22, middle(r), i == 0 ? 9.0f : 13.0f, gray(0.65f));
        desktopModalHot({Kind::row, i}, r);
        y += item_h;
    }
    if (downloading) {
        const float ratio = state.total ? static_cast<float>(static_cast<double>(state.done) / static_cast<double>(state.total)) : 0.0f;
        desktopFill({p.left + 22, y + 8, p.right - 22, y + 12}, 2, gray(1, 0.1f));
        if (state.total) desktopFill({p.left + 22, y + 8, p.left + 22 + (p.right - p.left - 44) * std::clamp(ratio, 0.0f, 1.0f), y + 12}, 2, uiAccent());
        if (!epic || amazon || state.total)
            textLine(epic && !amazon ? std::to_wstring(state.done / 100) + L"%" : std::to_wstring(state.done / (1024 * 1024)) + L" / " + std::to_wstring(state.total / (1024 * 1024)) + L" MiB",
                     p.left + 22, y + 28, p.right - p.left - 44, desktop_small_font_.Get(), palette::muted());
        y += 44;
    }
    // The store's action (Zainstaluj, Przerwij, …) and Powrót.
    const float by = p.bottom - 20 - 38;
    const Rect action{p.right - 22 - 170, by, p.right - 22, by + 38}, back{action.left - 10 - 120, by, action.left - 10, by + 38};
    desktopButton(action, 2, hover == SheetHit{Kind::row, 3} ? 1.0f : 0.0f, false, true, 0, gog_install_ui::action(ui));
    desktopButton(back, 1, hover == SheetHit{Kind::row, 4} ? 1.0f : 0.0f, false, true, 0, tr(L"Powrót"));
    if (ui.row == 3) desktopFocusRing(action, 8);
    if (ui.row == 4) desktopFocusRing(back, 8);
    desktopModalHot({Kind::row, 3}, action);
    desktopModalHot({Kind::row, 4}, back);
    desktop_modal_ = DialogKind::gog_install;
    target_->SetTransform(saved);
}