// Settings: the "Inny…" colour editor for the interface accent, the buttons and the cover accent. Hue,
// saturation and value sliders over the Settings screen; the colour shows live in the interface
// (or on the focused cover) while editing, and B restores the saved one.
#include "launcher.hpp"

#include <algorithm>

using namespace settings_ui;

void Launcher::openColorEditor(const Row& row) {
    auto& editor = color_editor_;
    const float hue = editor.hue;
    editor = {};
    editor.open = true;
    editor.target = row.id;
    editor.title = row.title;
    editor.hue = hue; // Kept for greys, which have no hue of their own.
    const uint32_t current = row.id == Id::desktop_accent ? settings_.desktop_accent_rgb :
                             row.id == Id::desktop_button ? (settings_.desktop_button_rgb ? settings_.desktop_button_rgb : settings_.desktop_accent_rgb) :
                             row.id == Id::ui_accent ? settings_.ui_accent_rgb :
                             row.id == Id::button_color ? (settings_.button_rgb ? settings_.button_rgb : settings_.ui_accent_rgb) : settings_.cover_accent_rgb;
    if (current) editor.set(current);
    else { editor.hue = 210; editor.saturation = 0.6f; editor.value = 0.9f; } // "Z okładki": a colour to start from.
    settings_state_.editing = false;
    colorEditorPreview();
    audio_.context();
}

// The accent and the buttons' fill of the mode on screen: the desktop window has its own pair.
void Launcher::applyAccentColors() {
    if (desktop_mode_) {
        renderer_.setUiAccent(settings_.desktop_accent_rgb);
        renderer_.setButtonTheme(settings_.desktop_button_rgb ? settings_.desktop_button_rgb : settings_.desktop_accent_rgb, settings_.button_idle);
    } else {
        renderer_.setUiAccent(settings_.ui_accent_rgb);
        renderer_.setButtonTheme(settings_.button_rgb, settings_.button_idle);
    }
}

void Launcher::colorEditorPreview() {
    const uint32_t rgb = color_editor_.rgb() ? color_editor_.rgb() : 0x010101; // 0 means "from the cover".
    if (color_editor_.target == Id::desktop_accent) renderer_.setUiAccent(rgb);
    else if (color_editor_.target == Id::desktop_button) renderer_.setButtonTheme(rgb, settings_.button_idle);
    else if (color_editor_.target == Id::ui_accent) renderer_.setUiAccent(rgb);
    else if (color_editor_.target == Id::button_color) renderer_.setButtonTheme(rgb, settings_.button_idle);
    else renderer_.setCoverAccent(rgb);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::closeColorEditor(bool save) {
    if (!color_editor_.open) return;
    const auto editor = color_editor_;
    color_editor_.open = color_editor_.dragging = false;
    if (editor.dragging && GetCapture() == hwnd_) ReleaseCapture();
    mouse_transition_until_ = GetTickCount64() + GetDoubleClickTime();
    if (save) {
        const uint32_t rgb = editor.rgb() ? editor.rgb() : 0x010101;
        if (editor.target == Id::desktop_accent) settings_.desktop_accent_rgb = rgb;
        else if (editor.target == Id::desktop_button) settings_.desktop_button_rgb = rgb;
        else if (editor.target == Id::ui_accent) settings_.ui_accent_rgb = rgb;
        else if (editor.target == Id::button_color) settings_.button_rgb = rgb;
        else settings_.cover_accent_rgb = rgb;
        settingsChanged(); // Applies it and lists it as "Własny · #RRGGBB".
    } else {
        applyAccentColors();
        renderer_.setCoverAccent(settings_.cover_accent_rgb);
    }
    audio_.context();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::colorEditorMove(int amount) {
    auto& editor = color_editor_;
    using settings_ui::ColorEditor;
    // Cancel shares the bottom line with Save: Up from either returns to the last slider.
    const int from = editor.focus == ColorEditor::cancel ? ColorEditor::save : editor.focus;
    const int next = editor.focus == ColorEditor::cancel && amount > 0 ? editor.focus : std::clamp(from + amount, 0, ColorEditor::save);
    if (next == editor.focus) { navigationBoundary(); return; }
    editor.focus = next;
    audio_.navigate();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::colorEditorAdjust(int amount) {
    auto& editor = color_editor_;
    if (editor.focus >= settings_ui::ColorEditor::sliders) {
        // The bottom line: Cancel on the left, Save on the right.
        const int next = amount < 0 ? settings_ui::ColorEditor::cancel : settings_ui::ColorEditor::save;
        if (next == editor.focus) { navigationBoundary(); return; }
        editor.focus = next; audio_.navigate(); InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    const float before = editor.level(editor.focus);
    editor.step(editor.focus, amount);
    if (editor.level(editor.focus) == before) { navigationBoundary(); return; }
    colorEditorPreview();
}

void Launcher::colorEditorClick(float x, float y) {
    auto& editor = color_editor_;
    const auto view = renderer_.viewport();
    const Layout layout{view.width, view.height};
    if (layout.colorSave().contains(x, y)) { closeColorEditor(true); return; }
    if (layout.colorCancel().contains(x, y) || !layout.colorPanel().contains(x, y)) { closeColorEditor(false); return; }
    for (int slider = 0; slider < settings_ui::ColorEditor::sliders; ++slider) {
        auto track = layout.colorTrack(slider);
        track.left -= 12; track.right += 12; // The knob reaches past the ends.
        if (!track.contains(x, y)) continue;
        editor.focus = slider;
        editor.dragging = true;
        SetCapture(hwnd_);
        colorEditorDrag(x);
        return;
    }
}

void Launcher::colorEditorDrag(float x) {
    auto& editor = color_editor_;
    if (editor.focus >= settings_ui::ColorEditor::sliders) return;
    const auto view = renderer_.viewport();
    const auto track = Layout{view.width, view.height}.colorTrack(editor.focus);
    editor.setLevel(editor.focus, (x - track.left) / (track.right - track.left));
    colorEditorPreview();
}
