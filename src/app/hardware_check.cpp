// Czy to u mnie zadziała: above Opis in the desktop pane, a PC game's requirements against this
// PC (src/library/system_requirements.hpp). A coloured dot and one line - green, yellow, red, or
// grey when the store gave nothing the check can read - that unfolds into the processor, the
// graphics card, RAM and disk space: what this PC has, the minimum and the recommended. Steam's
// store page is already cached by the pane (api/appdetails); GOG's requirements come from
// api.gog.com/v2/games and are kept for a month. Read on a worker, once per game.
#include "launcher.hpp"
#include "../net/http.hpp"
#include "../platform/atomic_file.hpp"
#include "../platform/user_config.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>

namespace {
std::string readText(const std::filesystem::path& file, size_t limit) {
    std::ifstream input(file, std::ios::binary | std::ios::ate);
    if (!input || input.tellg() <= 0 || static_cast<size_t>(input.tellg()) > limit) return {};
    std::string text(static_cast<size_t>(input.tellg()), '\0');
    input.seekg(0);
    if (!input.read(text.data(), static_cast<std::streamsize>(text.size()))) return {};
    return text;
}

// The store page the pane keeps in any language (the requirements are the same in all), else English once.
sysreq::Requirements steamNeeds(uint32_t app_id) {
    const auto folder = userConfigDirectory() / L"metadata";
    const auto id = std::to_wstring(app_id);
    bool cached = false;
    for (const auto* suffix : {L".en.details.json", L".details.json", L".pt-BR.details.json", L".es.details.json"}) {
        const auto text = readText(folder / (id + suffix), 2 * 1024 * 1024);
        if (text.empty()) continue;
        cached = true;
        if (auto needs = sysreq::fromSteam(app_id, text); needs.any()) return needs;
    }
    if (cached) return {};
    const auto reply = httpGet(L"https://store.steampowered.com/api/appdetails?appids=" + id + L"&l=english", L"Accept: application/json\r\n", 2 * 1024 * 1024);
    if (reply.status != 200) return {};
    const std::string text(reinterpret_cast<const char*>(reply.body.data()), reply.body.size());
    if (text.find("\"success\":true") != std::string::npos) atomic_file::write(folder / (id + L".en.details.json"), text);
    return sysreq::fromSteam(app_id, text);
}

sysreq::Requirements gogNeeds(const std::string& product) {
    const auto folder = userConfigDirectory() / L"requirements" / L"gog";
    std::wstring id(product.begin(), product.end());
    const auto file = folder / (id + L".json");
    std::error_code error;
    const auto written = std::filesystem::last_write_time(file, error);
    if (!error && std::filesystem::file_time_type::clock::now() - written < std::chrono::hours(24 * 30))
        return sysreq::fromGog(readText(file, 2 * 1024 * 1024));
    const auto reply = httpGet(L"https://api.gog.com/v2/games/" + id + L"?locale=en-US", L"Accept: application/json\r\n", 2 * 1024 * 1024);
    if (reply.status != 200) return {};
    const std::string text(reinterpret_cast<const char*>(reply.body.data()), reply.body.size());
    auto needs = sysreq::fromGog(text);
    std::filesystem::create_directories(folder, error);
    atomic_file::write(file, text);
    return needs;
}
}  // namespace

void Launcher::hardwareArrived(const std::string& key, HardwareData data) {
    hardware_machine_ = {data.machine.cpu, data.machine.gpu, data.machine.ram_mb, data.machine.vram_mb, data.machine.free_mb};
    auto& entry = hardware_[key];
    entry.needs = std::move(data.needs);
    entry.assessed_installed.reset();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::hardwareView(const Game& game, desktop_ui::Hardware& view) {
    if (!isGame(game) || game.kind == GameKind::nonsteam || game.kind == GameKind::catalog) return;
    const auto key = gameKey(game);
    const bool steam = steamArtwork(game) && game.app_id, gog = game.source == GameSource::gog && !game.source_id.empty();
    view.shown = true;
    view.open = desktop_hardware_open_;
    auto found = hardware_.find(key);
    if (found == hardware_.end()) {
        if (!steam && !gog) found = hardware_.emplace(key, HardwareEntry{}).first; // Other stores: nothing to read.
        else if (!hardware_reader_ && !runtime_started_) found = hardware_.emplace(key, HardwareEntry{}).first;
        else {
            if (hardware_asked_.insert(key).second) {
                HardwareAsk ask{key, steam ? game.app_id : 0, gog ? game.source_id : std::string{}};
                const auto reader = hardware_reader_ ? hardware_reader_ : HardwareReader([](const HardwareAsk& item) {
                    HardwareData data;
                    data.machine = readHardwareProfile();
                    data.needs = item.app_id ? steamNeeds(item.app_id) : gogNeeds(item.gog_id);
                    return data;
                });
                const auto alive = alive_; const auto window = hwnd_;
                if (!work_.start([ask, reader, alive, window] {
                        auto data = reader(ask);
                        if (!alive->load()) return;
                        auto message = std::make_unique<RemoteMessage>();
                        message->run = [key = ask.key, data = std::move(data)](Launcher& app) mutable { app.hardwareArrived(key, std::move(data)); };
                        if (PostMessageW(window, remote_message, 0, reinterpret_cast<LPARAM>(message.get()))) message.release();
                    }))
                    hardware_asked_.erase(key);
            }
            view.loading = true;
            view.summary = tr(L"Sprawdzam wymagania…");
            return;
        }
    }
    auto& entry = found->second;
    if (entry.assessed_installed != game.installed) {
        entry.verdict = sysreq::assess(hardware_machine_, entry.needs, game.installed);
        entry.assessed_installed = game.installed;
    }
    using sysreq::Level;
    view.level = static_cast<int>(entry.verdict.level);
    view.summary = !entry.needs.any() ? std::wstring(tr(L"Brak danych o wymaganiach")) :
                   entry.verdict.level == Level::good ? std::wstring(tr(L"Powinna działać płynnie")) :
                   entry.verdict.level == Level::ok ? std::wstring(tr(L"Powinna działać, ale poniżej zalecanych")) :
                   entry.verdict.level == Level::poor ? std::wstring(tr(L"Może nie działać na tym sprzęcie")) : std::wstring(tr(L"Za mało danych, by ocenić"));
    for (const auto& row : entry.verdict.rows) {
        using Part = sysreq::Row::Part;
        const std::wstring label = row.part == Part::cpu ? tr(L"Procesor") : row.part == Part::gpu ? tr(L"Karta graficzna") :
                                   row.part == Part::ram ? tr(L"Pamięć RAM") : tr(L"Wolne miejsce");
        view.rows.push_back({static_cast<int>(row.level), label, row.yours, row.minimum, row.recommended});
    }
}
