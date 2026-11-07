#include "launcher.hpp"
#include "../platform/text_encoding.hpp"
#include <shlobj.h>
#ifdef _WIN32
#include <shobjidl.h>
#else
#include "../platform/file_dialog.hpp"
#endif
#include <algorithm>

namespace {
std::filesystem::path knownFolder(REFKNOWNFOLDERID id) {
    PWSTR path = nullptr;
    if (FAILED(SHGetKnownFolderPath(id, 0, nullptr, &path))) return userConfigDirectory();
    std::filesystem::path result(path); CoTaskMemFree(path); return result;
}
std::filesystem::path pickFolder(HWND owner, const wchar_t* title, const std::filesystem::path& initial) {
#ifndef _WIN32
    return pickPath(owner, true, title ? title : L"", {}, {}, false, initial); // The desktop's folder dialog (a portal on Wayland).
#else
    Microsoft::WRL::ComPtr<IFileOpenDialog> dialog;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(dialog.GetAddressOf())))) return {};
    DWORD options = 0; dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | FOS_DONTADDTORECENT);
    dialog->SetTitle(title);
    Microsoft::WRL::ComPtr<IShellItem> folder;
    if (SUCCEEDED(SHCreateItemFromParsingName(initial.c_str(), nullptr, IID_PPV_ARGS(folder.GetAddressOf())))) dialog->SetFolder(folder.Get());
    if (FAILED(dialog->Show(owner))) return {};
    Microsoft::WRL::ComPtr<IShellItem> result;
    PWSTR path = nullptr;
    if (FAILED(dialog->GetResult(result.GetAddressOf())) || FAILED(result->GetDisplayName(SIGDN_FILESYSPATH, &path))) return {};
    std::filesystem::path selected(path); CoTaskMemFree(path); return selected;
#endif
}
}
void Launcher::openGogInstall(const Game& game) {
    if (!gog_service_ || gog_state_.account.empty()) return;
    if (epic_service_ && epic_service_->busy()) {
        // A GOG install waits for the Epic download: name both, so the title matches the flow.
        showNotification(NotificationKind::error, tr(L"Instalacja GOG"), tr(L"Najpierw dokończ lub anuluj pobieranie w Epic Games.")); return;
    }
    if (gog_service_->installBusy() && gog_install_ui_.game.source_id != game.source_id) {
        showNotification(NotificationKind::error, tr(L"Instalacja GOG"), tr(L"Najpierw dokończ lub anuluj bieżące pobieranie.")); return;
    }
    dismissMenu(); closeQuick(); closeFriends(); closeCategoryMenu(); cancelEnterHold();
    if (gog_install_ui_.game.source != game.source || gog_install_ui_.game.source_id != game.source_id || gog_install_ui_.snapshot.state == gog::InstallState::none) {
        gog_install_ui_ = {};
        gog_install_ui_.game = game;
        auto& request = gog_install_ui_.request;
        request.product = game.source_id;
        request.download_root = knownFolder(FOLDERID_Downloads);
        request.install_directory = knownFolder(FOLDERID_Profile) / L"Games" / (L"GOG-" + text_encoding::wide(game.source_id));
        request.owner_window = reinterpret_cast<uintptr_t>(hwnd_);
        gog_install_ui_.snapshot.product = game.source_id;
        gog_install_ui_.snapshot.state = gog::InstallState::loading;
        gog_service_->install(request);
    }
    gog_install_ui_.open = true;
    InvalidateRect(hwnd_, nullptr, FALSE);
}
void Launcher::gogInstallKey(WPARAM key, bool repeated) {
    if (!gogInstallOpen()) return;
    if (key == VK_F8 && !repeated) { togglePerformance(); return; }
    if (key == VK_F11 && !repeated) { toggleFullscreen(); return; }
    if (key == VK_ESCAPE || key == VK_BACK) {
        gog_install_ui_.open = false; mouse_transition_until_ = GetTickCount64() + GetDoubleClickTime();
    } else if (key == VK_UP) {
        gog_install_ui_.row = std::max(0, gog_install_ui_.row - 1);
        if ((gog_install_ui::helperInstall(gog_install_ui_.game.source) || gog_install_ui_.steam) && gog_install_ui_.row == 2) gog_install_ui_.row = 1;
    } else if (key == VK_DOWN || key == VK_TAB) {
        gog_install_ui_.row = std::min(4, gog_install_ui_.row + 1);
        if ((gog_install_ui::helperInstall(gog_install_ui_.game.source) || gog_install_ui_.steam) && gog_install_ui_.row == 2) gog_install_ui_.row = 3;
    }
    else if ((key == VK_LEFT || key == VK_RIGHT) && gog_install_ui_.row == 0 && gog_install_ui_.steam) {
        // Steam: the next library folder.
        const int count = static_cast<int>(gog_install_ui_.libraries.size());
        if (count > 1 && !gog_install_ui_.steam_busy) gog_install_ui_.library = (gog_install_ui_.library + count + (key == VK_LEFT ? -1 : 1)) % count;
    }
    else if ((key == VK_LEFT || key == VK_RIGHT) && gog_install_ui_.row == 0 && !gog_install_ui_.snapshot.busy() && !gog_install_ui_.snapshot.choices.empty()) {
        const int count = static_cast<int>(gog_install_ui_.snapshot.choices.size());
        gog_install_ui_.variant = (gog_install_ui_.variant + count + (key == VK_LEFT ? -1 : 1)) % count;
    } else if ((key == VK_RETURN || key == VK_SPACE) && !repeated) gogInstallAccept();
    InvalidateRect(hwnd_, nullptr, FALSE);
}
void Launcher::gogInstallAccept() {
    auto& ui = gog_install_ui_;
    if (!ui.open) return;
    if (ui.steam) { steamInstallAccept(); InvalidateRect(hwnd_, nullptr, FALSE); return; } // src/app/steam_install.cpp
    const bool amazon = ui.game.source == GameSource::amazon, xbox = ui.game.source == GameSource::xbox;
    const bool epic = ui.game.source == GameSource::epic || amazon || xbox; // One game folder, the helper downloads.
    if (xbox ? !xbox_service_ : amazon ? !amazon_service_ : ui.game.source == GameSource::epic ? !epic_service_ : !gog_service_) return;
    if (ui.row == 4) { gogInstallKey(VK_ESCAPE, false); return; }
    if (ui.row == 3) {
        if (amazon) { amazonInstallAccept(); InvalidateRect(hwnd_, nullptr, FALSE); return; }
        if (xbox) { xboxInstallAccept(); InvalidateRect(hwnd_, nullptr, FALSE); return; }
        if (epic) { epicInstallAccept(); InvalidateRect(hwnd_, nullptr, FALSE); return; }
        if (ui.snapshot.state == gog::InstallState::installing) return;
        if (ui.snapshot.busy()) { gog_service_->cancelInstall(); return; }
        if (ui.snapshot.state == gog::InstallState::completed || ui.snapshot.error == gog::InstallError::cleanup) { gogInstallKey(VK_ESCAPE, false); return; }
        ui.request.variant = ui.snapshot.choices.empty() ? std::string{} : ui.snapshot.choices[ui.variant].id;
        if (gog_service_->install(ui.request)) {
            downloadQueued(ui.game.name);
            ui.snapshot.state = gog::InstallState::loading; ui.snapshot.error = gog::InstallError::none;
        }
    } else if (!ui.snapshot.busy()) {
        if (epic && (ui.row == 0 || ui.row == 2)) return;
        if (ui.row == 0) { gogInstallKey(VK_RIGHT, false); return; }
        const bool game = ui.row == 1;
        const std::wstring title = xbox ? tr(L"Wybierz folder nadrzędny gry — Launchme doda podfolder Xbox") : amazon ? tr(L"Wybierz folder nadrzędny gry — Launchme doda podfolder Amazon") :
                                   epic ? tr(L"Wybierz folder nadrzędny gry — Launchme doda podfolder Epic") :
                                   game ? tr(L"Wybierz folder nadrzędny gry — Launchme doda podfolder GOG") :
                                          tr(L"Wybierz folder na pobierane części instalatora");
        const auto initial = game ? ui.request.install_directory.parent_path() : ui.request.download_root;
        file_browser::Request request;
        request.mode = file_browser::Mode::folder;
        request.title = title;
        request.initial = initial;
        request.native = [this, title, initial] { return pickFolder(hwnd_, title.c_str(), initial); };
        const auto product = ui.request.product;
        const auto subfolder = xbox ? L"Xbox-" + xbox_account::folderName(ui.game.name, ui.game.xbox_product) :
                               amazon ? L"Amazon-" + amazon::folderName(ui.game.name, product) :
                               (epic ? L"Epic-" : L"GOG-") + text_encoding::wide(product);
        openFileBrowser(std::move(request), [this, game, product, subfolder](const std::filesystem::path& folder) {
            auto& install = gog_install_ui_;
            if (!install.open || install.request.product != product || install.snapshot.busy()) return;
            if (game) install.request.install_directory = folder / subfolder;
            else install.request.download_root = folder;
            InvalidateRect(hwnd_, nullptr, FALSE);
        });
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}
void Launcher::applyGogInstallProgress() {
    const auto& status = gog_install_ui_.snapshot;
    if (status.product.empty()) return;
    const auto update = [&](Game& game) {
        if (game.source != gog_install_ui_.game.source || game.source_id != status.product) return;
        game.installation = status.progress();
        if (game.installation.active()) game.installed = false;
    };
    for (auto* list : {&store_library_, &store_private_library_, &public_library_, &grouped_library_, &pc_library_, &search_results_, &detail_games_, &store_results_, &private_category_.games})
        for (auto& game : *list) update(game);
    for (auto* list : {&categories_, &library_categories_}) for (auto& category : *list) for (auto& game : category.games) update(game);
    for (auto& card : cards_) update(card.game);
    if (pending_launch_) update(pending_game_);
}
void Launcher::pollGogInstall() {
    if (!gog_service_) return;
    auto next = gog_service_->takeInstall();
    if (!next) return;
    if (gog_install_ui::helperInstall(gog_install_ui_.game.source) || gog_install_ui_.steam) return;
    const bool was_active = gog_install_ui_.snapshot.progress().active();
    if (next->product.empty()) {
        gog_install_ui_.snapshot.state = gog::InstallState::none;
        applyGogInstallProgress(); gog_install_ui_ = {}; return;
    }
    if (next->product != gog_install_ui_.game.source_id) return;
    const auto previous_state = gog_install_ui_.snapshot.state;
    gog_install_ui_.snapshot = std::move(*next);
    auto& status = gog_install_ui_.snapshot;
    gog_install_ui_.variant = std::clamp(gog_install_ui_.variant, 0, std::max(0, static_cast<int>(status.choices.size()) - 1));
    applyGogInstallProgress();
    if (was_active != status.progress().active()) {
        const auto selected = cards_.empty() ? std::string{} : gameKey(cards_[selected_].game);
        buildShelves();
        if (screen_ == ScreenMode::shelves) {
            rebuildCards();
            const auto found = std::find_if(cards_.begin(), cards_.end(), [&](const GameCard& card) { return gameKey(card.game) == selected; });
            if (found != cards_.end()) focusCard(static_cast<size_t>(found - cards_.begin()), false);
        }
    }
    if (status.state == gog::InstallState::completed || status.error == gog::InstallError::cleanup) {
        completed_pc_installs_.insert(gameKey(gog_install_ui_.game));
        refreshPcLibraries();
        if (status.error == gog::InstallError::cleanup) showNotification(NotificationKind::error, L"GOG", gog::installStatusText(status));
        else if (previous_state != gog::InstallState::completed) downloadFinished(gog_install_ui_.game.name, &gog_install_ui_.game);
    } else if (status.state == gog::InstallState::cancelled || status.state == gog::InstallState::error) refreshPcLibraries();
    InvalidateRect(hwnd_, nullptr, FALSE);
}
