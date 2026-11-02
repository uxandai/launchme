#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>

// CPU and GPU temperatures for the drawer dashboard, read without admin rights or
// drivers: ACPI thermal zones through PDH ("\Thermal Zone Information(*)\Temperature",
// Kelvin) and the WDDM 2.4+ adapter performance data Task Manager shows
// (D3DKMT_ADAPTER_PERFDATA::Temperature, tenths of °C). On Linux: hwmon in /sys/class/hwmon
// (k10temp, zenpower, coretemp; amdgpu, nouveau, radeon, xe) and NVML for NVIDIA's driver.
// The conversions below are pure, so tests check them without touching the machine's sensors.
namespace sensors {

struct Reading {
    bool sampled = false;        // False until the first poll finished (nothing to show yet).
    std::optional<int> cpu, gpu; // Whole °C; empty = no usable sensor.
    // Memory in use and installed, MB: the system's RAM and the graphics card's own (Windows; empty elsewhere).
    std::optional<int> ram_used, ram_total, vram_used, vram_total;
    bool operator==(const Reading&) const = default;
};

// Firmware thermal zones on desktops often report a fixed placeholder (e.g. 290 K = 17 °C);
// a CPU is never colder than the room it runs in, so such values count as "no sensor".
inline constexpr int cpu_plausible_min = 20;
inline constexpr int plausible_max = 130;

// Thermal zone Kelvin -> rounded °C; empty for zero/negative/non-finite or implausible values.
std::optional<int> celsiusFromKelvin(double kelvin);
// Adapter tenths of °C -> rounded °C; 0 means the driver does not report a temperature.
std::optional<int> celsiusFromDeci(uint64_t tenths);
// hwmon millidegrees -> rounded °C; empty for zero/negative, below `minimum` or implausible values.
std::optional<int> celsiusFromMilli(int64_t millidegrees, int minimum = 1);
// Hottest usable sensor; empty when none is usable.
std::optional<int> hottest(std::span<const std::optional<int>> values);
// MSI Afterburner (when it runs) publishes its monitoring sources in the shared memory "MAHMSharedMemory"
// (the MAHM interface RivaTuner's tools use): a header, then one entry per source with its name and value.
// `memory` is that block as mapped; the layout is read from the header's own sizes. Takes only the CPU
// temperature ("CPU temperature", or the hottest "CPU… temperature"): the one number Windows does not give without a
// kernel driver. Everything else is read by launchme itself. Returns whether it found a value.
bool parseAfterburner(const void* memory, size_t size, Reading& reading);
// Opens the mapping, if Afterburner is running, and merges what it knows into `reading`. Windows; elsewhere false.
bool readAfterburner(Reading& reading);
// "54 °C", or "—" for no reading.
std::wstring format(std::optional<int> celsius);
// "5.1 / 8 GB" (MB in, one decimal below 10 GB, none above), or "—" without both numbers.
std::wstring formatMemory(std::optional<int> used_mb, std::optional<int> total_mb);

// Linux: the CPU package (Tdie/Tctl, "Package id N"; ACPI thermal zones only without a CPU
// driver) and the hottest GPU (amdgpu/radeon edge, else junction; nouveau, xe) under a
// /sys/class/hwmon folder (a fixture in tests).
std::optional<int> hwmonCpu(const std::filesystem::path& hwmon);
std::optional<int> hwmonGpu(const std::filesystem::path& hwmon);

// Keeps the PDH query and the gdi32 entry points (Linux: NVML) between polls. Not thread-safe:
// create and use one on the polling worker.
class Reader {
public:
    Reader();
    ~Reader();
    Reader(const Reader&) = delete;
    Reader& operator=(const Reader&) = delete;
    Reading read();

private:
    struct State;
    std::unique_ptr<State> state_;
};

} // namespace sensors
