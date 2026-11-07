// Settings → Dźwięk and Kontrolery: the device service (src/platform/device_service.cpp) runs while
// one of them is shown; its snapshots fill the source tiles, the Bluetooth audio rows and the
// controller gallery. The devices screen (quick bar) takes the service over while it is open.
#include "launcher.hpp"
#include "../platform/text_encoding.hpp"

#include <algorithm>
#include <cmath>

using namespace settings_ui;

std::optional<devices::Panel> Launcher::settingsDevicePanel() const {
    if (!settingsOpen() || settings_state_.subpage()) return {};
    if (settings_state_.category == static_cast<int>(settings_ui::Category::audio)) return devices::Panel::sound;
    if (settings_state_.category == static_cast<int>(settings_ui::Category::controllers)) return devices::Panel::controllers;
    return {};
}

void Launcher::syncSettingsDevices() {
    if (devicesOpen()) { settings_device_panel_.reset(); settings_devices_ready_ = false; return; } // Its own panel.
    const auto wanted = settingsDevicePanel();
    if (wanted == settings_device_panel_) return;
    if (!wanted) {
        if (device_service_) device_service_->close();
        settings_device_panel_.reset(); settings_devices_ready_ = false;
        return;
    }
    if (!device_service_ && runtime_started_) device_service_ = windowsDeviceService();
    if (!device_service_) return;
    device_service_->open(*wanted);
    settings_device_panel_ = wanted;
    settings_devices_ready_ = false;
    settings_device_poll_ = 0;
    // Everything up to the opening belongs to the previous panel: wait for the worker's next pass.
    auto opened = device_service_->snapshot();
    for (const auto& [kind, bytes] : opened.images)
        if (controller_images_.insert(static_cast<int>(kind)).second)
            renderer_.setPlatformBadge(L"controller:" + std::to_wstring(static_cast<int>(kind)), bytes);
    settings_devices_.revision = opened.revision;
}

void Launcher::pollSettingsDevices() {
    if (!settings_device_panel_ || !device_service_ || !settingsOpen()) return;
    const auto now = GetTickCount64();
    if (now < settings_device_poll_) return;
    settings_device_poll_ = now + 150;
    // A picker or a slider being edited keeps its list until it closes.
    if (settings_state_.editing || settings_dragging_) return;
    auto snapshot = device_service_->snapshot();
    for (const auto& [kind, bytes] : snapshot.images)
        if (controller_images_.insert(static_cast<int>(kind)).second)
            renderer_.setPlatformBadge(L"controller:" + std::to_wstring(static_cast<int>(kind)), bytes);
    if (snapshot.revision == settings_devices_.revision) return;
    controller_input_.enrich(snapshot.controllers);
    for (const auto& pad : snapshot.controllers) requestControllerPicture(pad.picture);
    // The volumes follow the slider / LB/RB until Windows acknowledged their last command.
    for (int channel = 0; channel < 2; ++channel) {
        auto& incoming = snapshot.audio[channel];
        if (settings_volume_pending_[channel] > incoming.acknowledged && incoming.available &&
            incoming.endpoint == settings_devices_.audio[channel].endpoint)
            incoming.volume = settings_devices_.audio[channel].volume;
        else settings_volume_pending_[channel] = 0;
    }
    snapshot.images.clear();
    settings_devices_ = std::move(snapshot);
    settings_devices_ready_ = true;
    buildSettingsRows();
}

bool Launcher::settingsTileActivate(const Row& row) {
    if (!tiled(row.control) || row.tiles.empty()) return false;
    const int index = std::clamp(settings_state_.tile, 0, static_cast<int>(row.tiles.size()) - 1);
    const auto& tile = row.tiles[index];
    if (row.control == Control::sources) {
        // A: the list of this direction's devices, the default one checked.
        if (tile.choices.empty()) { navigationBoundary(); return true; }
        settings_state_.choice = std::clamp(tile.choice, 0, static_cast<int>(tile.choices.size()) - 1);
        settings_state_.editing = true;
        audio_.context();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return true;
    }
    if (tile.pair) { openDevices(devices::Panel::bluetooth); return true; }
    if (!tile.key.empty() && device_service_ && !settings_devices_.busy) {
        device_service_->submit({tile.connected ? devices::Action::disconnect : devices::Action::connect, tile.key});
        settings_device_poll_ = 0;
        audio_.context();
    } else if (tile.key.empty() && !tile.action.empty()) {
        if (execute_(hwnd_, GameAction{L"ms-settings:bluetooth"}) <= 32)
            showNotification(NotificationKind::error, tr(L"Nie udało się otworzyć ustawień Windows."), L"Bluetooth");
    } else navigationBoundary();
    InvalidateRect(hwnd_, nullptr, FALSE);
    return true;
}

void Launcher::settingsPickSource(const Row& row, int choice) {
    if (row.control != Control::sources || row.tiles.empty()) return;
    const auto& tile = row.tiles[std::clamp(settings_state_.tile, 0, static_cast<int>(row.tiles.size()) - 1)];
    settings_state_.editing = false;
    if (choice >= 0 && choice < static_cast<int>(tile.choices.size()) && choice != tile.choice && device_service_) {
        const auto& id = tile.choices[choice].key;
        device_service_->submit({devices::Action::endpoint, id, 0, tile.input});
        // Shown at once; the next snapshot confirms it (or brings the old default back on failure).
        for (auto& endpoint : settings_devices_.endpoints) if (endpoint.input == tile.input) endpoint.active = endpoint.id == id;
        settings_device_poll_ = GetTickCount64() + 400;
        if (!tile.input) volume_monitor_.refresh();
    }
    audio_.context();
    buildSettingsRows();
}

void Launcher::settingsShoulder(int amount) {
    if (!settingsOpen() || settings_state_.editing || color_editor_.open || !amount) return;
    if (settings_state_.at(Page::collections) || settings_state_.at(Page::home_rows)) { settingsReorder(amount); return; }
    if (!settings_state_.options || settings_rows_.empty()) return;
    const auto& row = settings_rows_[settings_state_.row()];
    if (row.control != Control::sources || row.tiles.empty()) return;
    // The focused source tile's volume in 5% steps, like the Dźwięk panel's slider.
    const bool input = row.tiles[std::clamp(settings_state_.tile, 0, static_cast<int>(row.tiles.size()) - 1)].input;
    auto& channel = settings_devices_.audio[input];
    if (!device_service_ || !channel.available || settings_devices_.busy) { navigationBoundary(); return; }
    const float value = std::clamp(std::round(channel.volume * 20 + amount) / 20, 0.0f, 1.0f);
    if (std::abs(value - channel.volume) < 0.005f) { navigationBoundary(); return; }
    const auto sequence = ++audio_command_sequence_;
    settings_volume_pending_[input] = sequence;
    channel.volume = value;
    device_service_->submit({devices::Action::volume, channel.endpoint, value, input, sequence});
    if (input) showVolume(value, channel.muted, true); // The output shows through the volume monitor.
    audio_.navigate();
    buildSettingsRows();
}

void Launcher::openSettingsCollections() {
    openSettingsPage(Page::home_rows);
}
