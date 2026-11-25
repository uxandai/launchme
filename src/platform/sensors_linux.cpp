// Linux side of sensors.hpp: hwmon (Documentation/hwmon/sysfs-interface.rst) and, for NVIDIA's
// own driver, which has no hwmon device, NVML. sensors.cpp has the shared conversions.
#include "sensors.hpp"

#include <dlfcn.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <fstream>
#include <iterator>
#include <string_view>
#include <vector>

namespace sensors {
namespace {
std::string firstLine(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    std::string line;
    std::getline(input, line);
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
    return line;
}

struct Temperature { std::string label; int64_t millidegrees = 0; };
struct Device { std::string name; std::vector<Temperature> temperatures; };

// Every hwmon device with its readable temp<N>_input values.
std::vector<Device> devices(const std::filesystem::path& hwmon) {
    std::vector<Device> result;
    std::error_code error;
    for (std::filesystem::directory_iterator entry(hwmon, error), end; !error && entry != end; entry.increment(error)) {
        Device device{firstLine(entry->path() / "name")};
        std::error_code inner;
        for (std::filesystem::directory_iterator file(entry->path(), inner), stop; !inner && file != stop; file.increment(inner)) {
            const auto name = file->path().filename().string();
            if (!name.starts_with("temp") || !name.ends_with("_input")) continue;
            const auto text = firstLine(file->path());
            int64_t value = 0;
            const auto [at, failed] = std::from_chars(text.data(), text.data() + text.size(), value);
            if (failed != std::errc{} || at == text.data()) continue; // Asleep (EIO/ENODATA) or no sensor.
            const auto stem = name.substr(0, name.size() - std::string_view("_input").size());
            device.temperatures.push_back({firstLine(entry->path() / (stem + "_label")), value});
        }
        if (!device.name.empty() && !device.temperatures.empty()) result.push_back(std::move(device));
    }
    return result;
}

// The first labelled reading of `labels` (in order of preference), else the hottest of `device`.
std::optional<int> preferred(const Device& device, std::initializer_list<std::string_view> labels, int minimum,
                             bool prefix = false) {
    for (const auto label : labels) {
        std::vector<std::optional<int>> matches;
        for (const auto& temperature : device.temperatures)
            if (prefix ? temperature.label.starts_with(label) : temperature.label == label)
                matches.push_back(celsiusFromMilli(temperature.millidegrees, minimum));
        if (!matches.empty()) return hottest(matches);
    }
    std::vector<std::optional<int>> all;
    for (const auto& temperature : device.temperatures) all.push_back(celsiusFromMilli(temperature.millidegrees, minimum));
    return hottest(all);
}
} // namespace

std::optional<int> hwmonCpu(const std::filesystem::path& hwmon) {
    std::vector<std::optional<int>> cpus, zones;
    for (const auto& device : devices(hwmon)) {
        // Tdie is the real die temperature where Tctl carries a fan-control offset (first Zen parts).
        if (device.name == "k10temp" || device.name == "zenpower") cpus.push_back(preferred(device, {"Tdie", "Tctl"}, cpu_plausible_min));
        else if (device.name == "coretemp") cpus.push_back(preferred(device, {"Package id"}, cpu_plausible_min, true));
        else if (device.name == "cpu_thermal" || device.name == "cpu-thermal") cpus.push_back(preferred(device, {}, cpu_plausible_min));
        // The ACPI thermal zones Windows reads: only without a CPU driver (often a fixed placeholder).
        else if (device.name == "acpitz") zones.push_back(preferred(device, {}, cpu_plausible_min));
    }
    return cpus.empty() ? hottest(zones) : hottest(cpus);
}

std::optional<int> hwmonGpu(const std::filesystem::path& hwmon) {
    std::vector<std::optional<int>> gpus;
    for (const auto& device : devices(hwmon)) {
        if (device.name == "amdgpu" || device.name == "radeon") gpus.push_back(preferred(device, {"edge", "junction"}, 1));
        else if (device.name == "xe") gpus.push_back(preferred(device, {"pkg"}, 1));
        else if (device.name == "nouveau" || device.name == "i915") gpus.push_back(preferred(device, {}, 1));
    }
    return hottest(gpus);
}

std::optional<std::pair<int, int>> meminfoUsage(std::string_view text) {
    const auto field = [&](std::string_view name) -> std::optional<uint64_t> {
        for (size_t at = 0; at < text.size();) {
            const auto end = std::min(text.find('\n', at), text.size());
            auto line = text.substr(at, end - at);
            at = end + 1;
            if (!line.starts_with(name) || line.size() <= name.size() || line[name.size()] != ':') continue;
            line.remove_prefix(name.size() + 1);
            while (!line.empty() && (line.front() == ' ' || line.front() == '\t')) line.remove_prefix(1);
            uint64_t kilobytes = 0;
            const auto [ptr, error] = std::from_chars(line.data(), line.data() + line.size(), kilobytes);
            if (error != std::errc{} || ptr == line.data()) return std::nullopt;
            return kilobytes;
        }
        return std::nullopt;
    };
    const auto total = field("MemTotal"), available = field("MemAvailable");
    if (!total || !available || !*total || *available > *total) return std::nullopt;
    return std::pair{static_cast<int>((*total - *available) / 1024), static_cast<int>(*total / 1024)};
}

std::optional<std::pair<int, int>> drmVram(const std::filesystem::path& drm) {
    std::optional<std::pair<int, int>> best;
    std::error_code error;
    for (std::filesystem::directory_iterator entry(drm, error), end; !error && entry != end; entry.increment(error)) {
        const auto name = entry->path().filename().string();
        if (!name.starts_with("card") || name.find('-') != std::string::npos) continue;
        const auto device = entry->path() / "device";
        if (firstLine(device / "power" / "runtime_status") == "suspended") continue;
        uint64_t used = 0, total = 0;
        const auto used_text = firstLine(device / "mem_info_vram_used"), total_text = firstLine(device / "mem_info_vram_total");
        if (std::from_chars(used_text.data(), used_text.data() + used_text.size(), used).ec != std::errc{} ||
            std::from_chars(total_text.data(), total_text.data() + total_text.size(), total).ec != std::errc{} || !total || used > total) continue;
        const std::pair reading{static_cast<int>(used / (1024 * 1024)), static_cast<int>(total / (1024 * 1024))};
        if (!best || reading.second > best->second) best = reading;
    }
    return best;
}

namespace {
// The few NVML declarations used here (nvml.h), resolved at run time: libnvidia-ml.so.1 ships
// with NVIDIA's driver only.
using NvmlDevice = struct NvmlDeviceOpaque*;
using NvmlInit = int (*)();
using NvmlShutdown = int (*)();
using NvmlCount = int (*)(unsigned*);
using NvmlHandle = int (*)(unsigned, NvmlDevice*);
using NvmlTemperature = int (*)(NvmlDevice, int, unsigned*);
struct NvmlMemoryInfo { unsigned long long total, free, used; };
using NvmlMemory = int (*)(NvmlDevice, NvmlMemoryInfo*);
constexpr int nvml_success = 0, nvml_temperature_gpu = 0;

// True when an NVIDIA card is awake: NVML would otherwise power up a laptop's sleeping dGPU.
bool nvidiaAwake() {
    std::error_code error;
    for (std::filesystem::directory_iterator entry("/sys/class/drm", error), end; !error && entry != end; entry.increment(error)) {
        const auto name = entry->path().filename().string();
        if (!name.starts_with("card") || name.find('-') != std::string::npos) continue;
        if (firstLine(entry->path() / "device" / "vendor") != "0x10de") continue;
        if (firstLine(entry->path() / "device" / "power" / "runtime_status") != "suspended") return true;
    }
    return false;
}
} // namespace

struct Reader::State {
    void* nvml = nullptr;
    bool nvml_tried = false, nvml_ready = false;
    NvmlCount count = nullptr;
    NvmlHandle handle = nullptr;
    NvmlTemperature temperature = nullptr;
    NvmlMemory memory = nullptr;
    std::optional<std::pair<int, int>> nvidia_vram; // The last nvidia() poll's card with the most memory.

    ~State() {
        if (nvml_ready) if (const auto shutdown = reinterpret_cast<NvmlShutdown>(dlsym(nvml, "nvmlShutdown"))) shutdown();
        if (nvml) dlclose(nvml);
    }

    std::optional<int> nvidia() {
        if (!nvml_tried) {
            std::error_code error;
            if (!std::filesystem::exists("/proc/driver/nvidia/version", error) || !nvidiaAwake()) return std::nullopt;
            nvml_tried = true;
            if (!(nvml = dlopen("libnvidia-ml.so.1", RTLD_NOW | RTLD_LOCAL))) return std::nullopt;
            const auto init = reinterpret_cast<NvmlInit>(dlsym(nvml, "nvmlInit_v2"));
            count = reinterpret_cast<NvmlCount>(dlsym(nvml, "nvmlDeviceGetCount_v2"));
            handle = reinterpret_cast<NvmlHandle>(dlsym(nvml, "nvmlDeviceGetHandleByIndex_v2"));
            temperature = reinterpret_cast<NvmlTemperature>(dlsym(nvml, "nvmlDeviceGetTemperature"));
            memory = reinterpret_cast<NvmlMemory>(dlsym(nvml, "nvmlDeviceGetMemoryInfo"));
            nvml_ready = init && count && handle && temperature && init() == nvml_success;
        }
        nvidia_vram.reset();
        if (!nvml_ready || !nvidiaAwake()) return std::nullopt;
        unsigned devices = 0;
        if (count(&devices) != nvml_success) return std::nullopt;
        std::vector<std::optional<int>> readings;
        for (unsigned index = 0; index < std::min(devices, 8u); ++index) {
            NvmlDevice device = nullptr;
            unsigned celsius = 0;
            if (handle(index, &device) != nvml_success) continue;
            if (temperature(device, nvml_temperature_gpu, &celsius) == nvml_success) readings.push_back(celsiusFromMilli(int64_t{celsius} * 1000));
            NvmlMemoryInfo info{};
            if (memory && memory(device, &info) == nvml_success && info.total && info.used <= info.total) {
                const std::pair reading{static_cast<int>(info.used / (1024 * 1024)), static_cast<int>(info.total / (1024 * 1024))};
                if (!nvidia_vram || reading.second > nvidia_vram->second) nvidia_vram = reading;
            }
        }
        return hottest(readings);
    }
};

Reader::Reader() : state_(std::make_unique<State>()) {}
Reader::~Reader() = default;

Reading Reader::read() {
    Reading reading;
    reading.sampled = true;
    reading.cpu = hwmonCpu("/sys/class/hwmon");
    const std::array<std::optional<int>, 2> gpus{hwmonGpu("/sys/class/hwmon"), state_->nvidia()};
    reading.gpu = hottest(gpus);
    std::ifstream meminfo("/proc/meminfo", std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(meminfo)), std::istreambuf_iterator<char>());
    if (const auto ram = meminfoUsage(text)) { reading.ram_used = ram->first; reading.ram_total = ram->second; }
    // The card with the most memory of its own: amdgpu's counters, else NVIDIA's (read with the temperature).
    auto vram = drmVram("/sys/class/drm");
    if (state_->nvidia_vram && (!vram || state_->nvidia_vram->second > vram->second)) vram = state_->nvidia_vram;
    if (vram) { reading.vram_used = vram->first; reading.vram_total = vram->second; }
    return reading;
}

} // namespace sensors
