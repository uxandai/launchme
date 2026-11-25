// Linux system services: /proc and fdinfo parsing, hwmon and power_supply fixture folders,
// logind answers, Discord's process names and IPC socket, Steam's content log and manifests.
// Then a smoke read of this machine (values may legitimately be absent). Nothing here sleeps,
// shuts down or changes a setting: power actions are only listed, never performed.
#include "../src/platform/discord.hpp"
#include "../src/platform/hardware_profile.hpp"
#include "../src/platform/memory_usage.hpp"
#include "../src/platform/power.hpp"
#include "../src/platform/sensors.hpp"
#include "../src/platform/steam_downloads.hpp"

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {
namespace fs = std::filesystem;
void write(const fs::path& path, const std::string& text) {
    fs::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << text;
}
void sensor(const fs::path& device, const std::string& name, int index, long millidegrees, const std::string& label = {}) {
    write(device / "name", name + "\n");
    write(device / ("temp" + std::to_string(index) + "_input"), std::to_string(millidegrees) + "\n");
    if (!label.empty()) write(device / ("temp" + std::to_string(index) + "_label"), label + "\n");
}
void battery(const fs::path& supply, const std::string& type, const std::string& scope, int capacity, const std::string& status,
             long now = 0, long full = 0) {
    write(supply / "type", type + "\n");
    if (!scope.empty()) write(supply / "scope", scope + "\n");
    write(supply / "present", "1\n");
    write(supply / "capacity", std::to_string(capacity) + "\n");
    write(supply / "status", status + "\n");
    if (full) { write(supply / "energy_now", std::to_string(now) + "\n"); write(supply / "energy_full", std::to_string(full) + "\n"); }
}
} // namespace

int main() {
    const auto root = fs::temp_directory_path() / ("launchme-services-" + std::to_string(getpid()));
    fs::remove_all(root);

    // /proc/self/status: private = anonymous resident + swapped; working set = VmRSS.
    const auto status = memoryFromProcStatus("Name:\tlaunchme\nVmRSS:\t  204800 kB\nRssAnon:\t  102400 kB\nRssFile:\t 90000 kB\nVmSwap:\t    1024 kB\n");
    if (status.working_set != 204800u * 1024 || status.private_bytes != 103424u * 1024 || status.gpu_local) return 1;
    if (memoryFromProcStatus("").working_set || memoryFromProcStatus("VmRSS: lots kB\n").working_set) return 1;

    // DRM fdinfo: resident beats legacy "memory" beats total; vram/local0 local, gtt/system shared, cpu ignored.
    const auto amd = drmClientMemory("pos:\t0\nflags:\t02100002\ndrm-driver:\tamdgpu\ndrm-pdev:\t0000:03:00.0\ndrm-client-id:\t42\n"
        "drm-memory-vram:\t2048 KiB\ndrm-memory-gtt:\t512 KiB\ndrm-memory-cpu:\t64 KiB\ndrm-total-vram:\t4 MiB\ndrm-resident-vram:\t1024 KiB\n");
    if (!amd || amd->client != 42 || amd->pdev != "0000:03:00.0" || amd->local != 1024u * 1024 || amd->shared != 512u * 1024) return 2;
    const auto intel = drmClientMemory("drm-driver:\ti915\ndrm-client-id:\t7\ndrm-total-local0:\t1 MiB\ndrm-total-system0:\t300\n");
    if (!intel || intel->local != 1024u * 1024 || intel->shared != 300) return 2;
    if (drmClientMemory("pos:\t0\nflags:\t0100002\nmnt_id:\t15\n") || drmClientMemory("drm-total-vram:\t5 KiB\n")) return 2;

    // hwmon: Tdie over Tctl, coretemp packages, ACPI zones only without a CPU driver; a placeholder is no sensor.
    const auto hwmon = root / "hwmon";
    sensor(hwmon / "hwmon0", "acpitz", 1, 16800);
    sensor(hwmon / "hwmon1", "nvme", 1, 45850, "Composite");
    if (sensors::hwmonCpu(hwmon) || sensors::hwmonGpu(hwmon)) return 3;
    sensor(hwmon / "hwmon0", "acpitz", 2, 51000);
    if (sensors::hwmonCpu(hwmon) != 51) return 3;
    sensor(hwmon / "hwmon2", "k10temp", 1, 45125, "Tctl");
    sensor(hwmon / "hwmon2", "k10temp", 3, 60500, "Tccd1");
    if (sensors::hwmonCpu(hwmon) != 45) return 3;
    sensor(hwmon / "hwmon2", "k10temp", 2, 43600, "Tdie");
    sensor(hwmon / "hwmon3", "coretemp", 1, 70400, "Package id 0");
    sensor(hwmon / "hwmon3", "coretemp", 2, 90000, "Core 0");
    if (sensors::hwmonCpu(hwmon) != 70) return 3;
    sensor(hwmon / "hwmon4", "amdgpu", 1, 48000, "edge");
    sensor(hwmon / "hwmon4", "amdgpu", 2, 66000, "junction");
    sensor(hwmon / "hwmon5", "nouveau", 1, 52000);
    if (sensors::hwmonGpu(hwmon) != 52) return 4;
    write(hwmon / "hwmon5" / "temp1_input", "\n"); // An asleep card reads nothing.
    if (sensors::hwmonGpu(hwmon) != 48 || sensors::hwmonCpu(root / "missing")) return 4;
    if (sensors::celsiusFromMilli(45500) != 46 || sensors::celsiusFromMilli(0) || sensors::celsiusFromMilli(-1000) ||
        sensors::celsiusFromMilli(130499) != 130 || sensors::celsiusFromMilli(130500) || sensors::celsiusFromMilli(19000, sensors::cpu_plausible_min))
        return 4;

    // RAM in use from /proc/meminfo; VRAM of the awake card with the most of it (a sleeping one is not read).
    if (sensors::meminfoUsage("MemTotal:       16318288 kB\nMemFree:  100 kB\nMemAvailable:   8159144 kB\n") != std::pair{7967, 15935} ||
        sensors::meminfoUsage("MemTotal: 100 kB\n") || sensors::meminfoUsage("MemTotal: 1 kB\nMemAvailable: 9 kB\n")) return 10;
    const auto drm = root / "drm";
    write(drm / "card0" / "device" / "mem_info_vram_total", "1073741824\n");
    write(drm / "card0" / "device" / "mem_info_vram_used", "536870912\n");
    write(drm / "card1" / "device" / "mem_info_vram_total", "17179869184\n");
    write(drm / "card1" / "device" / "mem_info_vram_used", "2147483648\n");
    write(drm / "card1" / "device" / "power" / "runtime_status", "active\n");
    write(drm / "card1-DP-1" / "device" / "mem_info_vram_total", "99999999999\n"); // A connector, not a card.
    if (sensors::drmVram(drm) != std::pair{2048, 16384}) return 10;
    write(drm / "card1" / "device" / "power" / "runtime_status", "suspended\n");
    if (sensors::drmVram(drm) != std::pair{512, 1024} || sensors::drmVram(root / "missing")) return 10;

    // Will it run: the driver's GPU name without its note, an APU's memory, every disk file system.
    if (hardware_profile::cleanGpuName("AMD Radeon RX 6800 XT (RADV NAVI21)") != L"AMD Radeon RX 6800 XT" ||
        hardware_profile::cleanGpuName("Intel(R) Arc(tm) A770 Graphics (DG2)") != L"Intel(R) Arc(tm) A770 Graphics" ||
        hardware_profile::cleanGpuName("NVIDIA GeForce RTX 3080") != L"NVIDIA GeForce RTX 3080") return 11;
    constexpr uint64_t gib = 1024ull * 1024 * 1024;
    if (hardware_profile::vramMb(8 * gib, 16 * gib) != 8192 || hardware_profile::vramMb(gib / 2, 8 * gib) != 4096 ||
        hardware_profile::vramMb(gib / 2, 0) != 512) return 11;
    const std::string mounts = "/dev/nvme0n1p2 / ext4 rw 0 0\nproc /proc proc rw 0 0\ntmpfs /tmp tmpfs rw 0 0\n"
                               "/dev/loop3 /snap/core squashfs ro 0 0\n/dev/nvme0n1p1 /boot/efi vfat rw 0 0\n"
                               "/dev/mmcblk0p1 /run/media/deck/SD\\040Card ext4 rw 0 0\nserver:/x /mnt/nfs nfs4 rw 0 0\n";
    std::vector<std::string> asked;
    const auto free_mb = hardware_profile::largestFreeMb(mounts, [&](const std::string& point) -> std::optional<uint64_t> {
        asked.push_back(point);
        return point == "/" ? 40 * gib : point == "/run/media/deck/SD Card" ? 400 * gib : 900 * gib;
    });
    if (free_mb != 400 * 1024 || asked != std::vector<std::string>{"/", "/run/media/deck/SD Card"}) return 11;

    // power_supply: mains and a mouse don't count; two batteries weighted by energy.
    const auto supply = root / "power_supply";
    write(supply / "AC" / "type", "Mains\n");
    battery(supply / "hidpp_battery_0", "Battery", "Device", 15, "Discharging");
    if (batteryStatus(supply)) return 5;
    battery(supply / "BAT0", "Battery", "", 80, "Charging");
    if (batteryStatus(supply) != BatteryStatus{80, true}) return 5;
    battery(supply / "BAT0", "Battery", "System", 80, "Discharging", 40000000, 50000000);
    battery(supply / "BAT1", "Battery", "", 20, "Discharging", 2000000, 10000000);
    if (batteryStatus(supply) != BatteryStatus{70, false} || batteryStatus(root / "missing")) return 5;
    if (!logindAllows("yes") || !logindAllows("challenge") || logindAllows("no") || logindAllows("na") || logindAllows("")) return 5;

    // Discord: client process names and a listening (not a stale) IPC socket.
    if (!discord::clientProcessName("Discord\n") || !discord::clientProcessName("DiscordCanary") || !discord::clientProcessName("discordptb") ||
        discord::clientProcessName("discord-rpc") || discord::clientProcessName("Discordo")) return 6;
    const auto runtime = root / "runtime";
    fs::create_directories(runtime / "app" / "com.discordapp.Discord");
    if (discord::ipcListening(runtime) || discord::ipcListening({})) return 6;
    const auto socket_path = (runtime / "app" / "com.discordapp.Discord" / "discord-ipc-3").string();
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    socket_path.copy(address.sun_path, sizeof(address.sun_path) - 1);
    const int server = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server < 0 || bind(server, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0 || listen(server, 4) != 0) return 6;
    if (!discord::ipcListening(runtime)) return 6;
    close(server);
    if (discord::ipcListening(runtime)) return 6; // The file stays behind; nobody listens.

    // Steam's content log: phases, start counters, pauses, completion.
    SteamContentLog log;
    log.feed("[2026-10-26 01:10:01] AppID 275850 update started : download 642011216/3143770544, store 0/0, reuse 4701472403/4701472403, delta 0/0, stage 5345320263/8204703950 \n"
             "[2026-10-26 01:10:01] AppID 275850 App update changed : Running Update,Downloading,Staging,\n"
             "[2026-10-26 01:10:02] Current download rate: 0.000 Mbps\n"
             "[2026-10-26 01:10:03] AppID 10 App update changed : Running Update,Preallocating,\n"
             "[2026-10-26 01:10:04] AppID 20 App update changed : Running Update,Downlo");
    auto state = log.state(275850);
    if (!state || state->phase != InstallPhase::downloading || state->done != 642011216 || state->total != 3143770544) return 7;
    if (log.state(10)->phase != InstallPhase::allocating || log.state(20) || log.state(30)) return 7;
    log.feed("[2026-10-26 01:11:00] AppID 275850 App update changed : Running Update,Downloading,Staging,Stopping,\n"
             "[2026-10-26 01:11:00] AppID 275850 state changed : Update Required,Update Queued,Update Started, (Suspended)\n");
    if (log.state(275850)->phase != InstallPhase::paused) return 7;
    log.feed("[2026-10-26 01:12:00] AppID 275850 update started : download 700000000/3143770544, stage 5400000000/8204703950\n"
             "[2026-10-26 01:12:00] AppID 275850 App update changed : Running Update,Committing,\n");
    state = log.state(275850);
    if (!state || state->phase != InstallPhase::staging || state->done != 5400000000 || state->total != 8204703950) return 7;
    log.feed("[2026-10-26 01:13:00] AppID 275850 App update changed : None\n"
             "[2026-10-26 01:13:00] AppID 275850 state changed : Fully Installed,\n");
    if (log.state(275850)) return 7;
    const std::string acf = "\"AppState\"\n{\n\t\"appid\"\t\t\"275850\"\n\t\"BytesToDownload\"\t\t\"3143770544\"\n"
                            "\t\"BytesDownloaded\"\t\t\"900000000\"\n\t\"BytesToStage\"\t\t\"8204703950\"\n\t\"BytesStaged\"\t\t\"6000000000\"\n}\n";
    const auto download = steamManifestCounters(acf, false), stage = steamManifestCounters(acf, true);
    if (download.done != 900000000 || download.total != 3143770544 || stage.done != 6000000000 || stage.total != 8204703950) return 7;
    if (steamManifestCounters("\"BytesDownloaded\" \"9\"\n\"BytesToDownload\" \"5\"\n", false).total) return 7;

    // A Steam folder fixture: the live read merges the log's phase with the manifest's counters.
    const auto steam = root / "Steam";
    write(steam / "steamapps" / "appmanifest_275850.acf", acf);
    write(steam / "logs" / "content_log.txt", "[2026-10-26 01:10:01] AppID 275850 update started : download 642011216/3143770544, stage 0/8204703950\n"
                                              "[2026-10-26 01:10:01] AppID 275850 App update changed : Running Update,Downloading,Staging,\n");
    SteamDownloads downloads(steam);
    const auto live = downloads.read(275850);
    if (!downloads.connected() || downloads.appManagerConnected() || !live || live->phase != InstallPhase::downloading ||
        live->done != 900000000 || live->total != 3143770544 || downloads.read(570)) return 8;
    std::ofstream(steam / "logs" / "content_log.txt", std::ios::app) << "[2026-10-26 01:20:00] AppID 275850 App update changed : None\n"
                                                                     << "[2026-10-26 01:20:00] AppID 275850 state changed : Fully Installed,\n";
    if (downloads.read(275850)) return 8;
    fs::remove_all(root);

    // This machine, read only.
    const auto memory = currentMemoryUsage();
    if (!memory.working_set || !memory.private_bytes || memory.private_bytes > (size_t{1} << 40)) return 9;
    trimProcessMemory();
    const auto actions = availablePowerActions();
    if (actions.empty()) return 9;
    const auto own = batteryStatus();
    if (own && (own->percent < 0 || own->percent > 100)) return 9;
    const auto machine = readHardwareProfile();
    std::cout << "gpu=" << std::string(machine.gpu.begin(), machine.gpu.end()) << " vram=" << machine.vram_mb << " MB free=" << machine.free_mb << " MB\n";
    std::cout << "memory private=" << memory.private_bytes / 1048576 << " MiB rss=" << memory.working_set / 1048576
              << " MiB gpu=" << memory.gpu_local / 1048576 << '/' << memory.gpu_shared / 1048576 << " MiB; power actions=" << actions.size()
              << "; battery=" << (own ? std::to_string(own->percent) + (own->charging ? "% charging" : "%") : "none")
              << "; discord running=" << discord::running() << " installed=" << discord::launchAction().has_value() << '\n';
    return 0;
}
