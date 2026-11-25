#include "renderer.hpp"
#include "../store/epic_account.hpp"
#include "../store/amazon.hpp"
#include "../store/xbox_account.hpp"
#include <cmath>

void Renderer::drawInstallDisc(float cx, float cy, float radius, float opacity, bool moving) {
    brush(D2D1::ColorF(0.015f, 0.02f, 0.03f, opacity * 0.84f));
    target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), radius + 5, radius + 5), solid_.Get());
    brush(D2D1::ColorF(0.82f, 0.87f, 0.91f, opacity));
    target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), radius, radius), solid_.Get());
    const float angle = moving ? elapsed_ * 3.4f : 0.5f;
    for (int spoke = 0; spoke < 2; ++spoke) {
        const float a = angle + spoke * 3.14159265f;
        line(cx + std::cos(a) * radius * 0.36f, cy + std::sin(a) * radius * 0.36f,
             cx + std::cos(a) * radius * 0.86f, cy + std::sin(a) * radius * 0.86f,
             D2D1::ColorF(0.35f, 0.66f, 0.76f, opacity), radius * 0.22f);
    }
    brush(D2D1::ColorF(0.05f, 0.07f, 0.09f, opacity));
    target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), radius * 0.25f, radius * 0.25f), solid_.Get());
}
void Renderer::drawGogInstall() {
    if (!context_->gog_install || !context_->gog_install->open) return;
    if (desktopDialogs()) { drawDesktopGogInstall(); return; } // Desktop mode: desktop_dialogs_renderer.cpp.
    const auto& ui = *context_->gog_install;
    const auto& state = ui.snapshot;
    const bool epic = gog_install_ui::helperInstall(ui.game.source), steam = ui.steam;
    const bool xbox = ui.game.source == GameSource::xbox;
    const bool amazon = ui.game.source == GameSource::amazon || xbox; // Nile and the Xbox helper report bytes, Legendary percent.
    const gog_install_ui::Layout layout{viewport_.width, viewport_.height, epic || steam};
    const auto panel = layout.panel();
    const auto rect = [](settings_ui::Rect r) { return D2D1::RectF(r.left, r.top, r.right, r.bottom); };
    brush(D2D1::ColorF(0, 0.82f)); target_->FillRectangle(D2D1::RectF(0, 0, viewport_.width, viewport_.height), solid_.Get());
    brush(D2D1::ColorF(0.055f, 0.065f, 0.075f)); target_->FillRoundedRectangle(D2D1::RoundedRect(rect(panel), 18, 18), solid_.Get());
    if (panel_borders_) { brush(D2D1::ColorF(1, 1, 1, 0.16f)); target_->DrawRoundedRectangle(D2D1::RoundedRect(rect(panel), 18, 18), solid_.Get(), 1.0f); }
    // Title with the status directly beneath; the capitals start at the 32 px padding line.
    textGroup(ui.game.name, steam ? gog_install_ui::steamInstallStatus(ui) : ui.game.source == GameSource::xbox ? xbox_account::installStatusText(state) : epic && !amazon ? epic::installStatusText(state) : gog::installStatusText(state),
              panel.left + 32, panel.top + 32, 770, category_font_.Get(), D2D1::ColorF(0xffffff),
              settings_body_font_.Get(), D2D1::ColorF(0xcbd3d9), 2, true);
    if (state.busy() || (steam && ui.steam_busy)) drawInstallDisc(panel.right - 48, panel.top + 48, 18, 1, true);
    if (state.state == gog::InstallState::downloading && state.total) {
        const float ratio = static_cast<float>(static_cast<double>(state.done) / state.total);
        const auto bar = layout.progress();
        brush(uiAccent()); target_->FillRectangle(D2D1::RectF(bar.left, bar.top, bar.left + (bar.right - bar.left) * std::clamp(ratio, 0.0f, 1.0f), bar.bottom), solid_.Get());
    }
    std::wstring variant = tr(L"Brak dostępnych wariantów");
    if (ui.variant >= 0 && ui.variant < static_cast<int>(state.choices.size())) {
        const auto& choice = state.choices[ui.variant];
        variant = choice.language + (choice.version.empty() ? L"" : L" · " + choice.version) + L" · " + std::to_wstring(choice.bytes / (1024 * 1024)) + L" MiB";
    }
    const std::array<std::wstring, 5> labels = steam ?
        std::array<std::wstring, 5>{tr(L"Biblioteka Steam"), tr(L"Okno instalacji Steam"), {}, gog_install_ui::action(ui), tr(L"Powrót")} :
        std::array<std::wstring, 5>{xbox ? std::wstring(L"Xbox · Windows (Xodus)") : amazon ? std::wstring(L"Amazon Games · Windows") : epic ? std::wstring(L"Epic Games · Windows") : std::wstring(tr(L"Język / wersja")), tr(L"Folder gry"),
                                    tr(L"Folder pobierania (tymczasowo)"), gog_install_ui::action(ui), tr(L"Powrót")};
    const std::array<std::wstring, 3> values = steam ?
        std::array<std::wstring, 3>{gog_install_ui::steamLibraryText(ui) + (ui.libraries.size() > 1 ? L"  ◀ ▶" : L""), tr(L"Wybór biblioteki i instalacja w samym Steamie"), {}} :
        std::array<std::wstring, 3>{xbox ? L"launchme-xbox " + std::wstring(xbox_account::helper_version) : amazon ? L"Nile " + std::wstring(amazon::helper_version) : epic ? std::wstring(L"Legendary 0.21.1") : variant, ui.request.install_directory.wstring(), ui.request.download_root.wstring()};
    for (int i = 0; i < 5; ++i) {
        if ((epic || steam) && i == 2) continue;
        const auto r = layout.row(i);
        // Focus: a lighter fill, no outline (rows taller than 64 px keep 16 px corners).
        brush(palette::gray(1, i == ui.row ? 0.10f : 0.035f)); target_->FillRoundedRectangle(D2D1::RoundedRect(rect(r), 16, 16), solid_.Get());
        // Same row type as the device lists: title (menu_font_) with its value directly beneath.
        textGroup(labels[i], i < 3 ? values[i] : std::wstring{}, r.left + 16, (r.top + r.bottom) * 0.5f, r.right - r.left - 32,
                  menu_font_.Get(), D2D1::ColorF(0xffffff), settings_body_font_.Get(), D2D1::ColorF(0xaeb9c0), 1);
    }
    if (state.state == gog::InstallState::downloading && (!epic || amazon || state.total))
        textLine(epic && !amazon ? std::to_wstring(state.done / 100) + L"%" : std::to_wstring(state.done / (1024 * 1024)) + L" / " + std::to_wstring(state.total / (1024 * 1024)) + L" MiB",
                 panel.left + 32, panel.bottom - 50, 836, settings_body_font_.Get(), D2D1::ColorF(0x98a4ac));
}
