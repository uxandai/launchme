// Drawer dashboard temperatures: unit conversions, aggregation and "no sensor" handling,
// plus a smoke read of this machine's real sensors (values may legitimately be absent).
#include "../src/platform/sensors.hpp"

#include <array>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>

int main() {
    using sensors::celsiusFromDeci;
    using sensors::celsiusFromKelvin;
    // Thermal zones report Kelvin; rounded to whole degrees.
    if (celsiusFromKelvin(300.15) != 27 || celsiusFromKelvin(320.0) != 47 || celsiusFromKelvin(330.4) != 57 ||
        celsiusFromKelvin(372.15) != 99 || celsiusFromKelvin(353.0) != 80) return 1;
    // Nothing usable: zero, negative, non-finite, a desktop firmware placeholder (290 K = 17 °C) or absurd heat.
    if (celsiusFromKelvin(0) || celsiusFromKelvin(-5) || celsiusFromKelvin(std::numeric_limits<double>::quiet_NaN()) ||
        celsiusFromKelvin(std::numeric_limits<double>::infinity()) || celsiusFromKelvin(290.0) ||
        celsiusFromKelvin(273.15 + sensors::cpu_plausible_min - 1) || !celsiusFromKelvin(273.15 + sensors::cpu_plausible_min) ||
        celsiusFromKelvin(273.15 + sensors::plausible_max + 1) || celsiusFromKelvin(700)) return 2;
    // Adapter perf data reports tenths of °C; 0 = the driver does not report a temperature.
    if (celsiusFromDeci(545) != 55 || celsiusFromDeci(544) != 54 || celsiusFromDeci(371) != 37 ||
        celsiusFromDeci(1) != 0 || celsiusFromDeci(1300) != 130) return 3;
    if (celsiusFromDeci(0) || celsiusFromDeci(1306) || celsiusFromDeci(UINT64_MAX)) return 4;
    // Hottest usable sensor wins; unusable ones never hide a real reading.
    const std::array<std::optional<int>, 4> zones{std::nullopt, 41, 63, std::nullopt};
    const std::array<std::optional<int>, 2> none{};
    const std::array<std::optional<int>, 3> adapters{celsiusFromDeci(0), celsiusFromDeci(655), celsiusFromDeci(482)};
    if (sensors::hottest(zones) != 63 || sensors::hottest(none) || sensors::hottest(std::span<const std::optional<int>>{}) ||
        sensors::hottest(adapters) != 66) return 5;
    // Display: whole degrees or an em dash.
    if (sensors::format(54) != L"54 \u00B0C" || sensors::format(std::nullopt) != L"\u2014") return 6;
    // Readings compare by value (the UI repaints only on change).
    const sensors::Reading a{true, 50, std::nullopt};
    auto b = a;
    if (!(a == b) || sensors::Reading{} == a) return 7;
    b.gpu = 60;
    if (a == b) return 7;

    // MSI Afterburner's shared memory: a header and entries; only the CPU temperature is taken from it.
    {
        constexpr size_t entry = 5 * 260 + 4 * 4, header = 20, count = 3;
        std::array<unsigned char, header + count * entry> block{};
        const auto put32 = [&](size_t at, uint32_t value) { std::memcpy(block.data() + at, &value, 4); };
        put32(0, 0x4D41484D); put32(4, 0x00020000); put32(8, header); put32(12, count); put32(16, entry);
        const auto source = [&](size_t i, const char* name, float value) {
            std::memcpy(block.data() + header + i * entry, name, std::strlen(name));
            std::memcpy(block.data() + header + i * entry + 5 * 260, &value, 4);
        };
        source(0, "GPU temperature", 61.0f);
        source(1, "CPU1 temperature", 48.0f);
        source(2, "CPU temperature", 57.0f);
        sensors::Reading reading;
        if (!sensors::parseAfterburner(block.data(), block.size(), reading) || reading.cpu != 57 || reading.gpu) return 13;
        put32(0, 0xDEAD); // Afterburner closed.
        sensors::Reading closed;
        if (sensors::parseAfterburner(block.data(), block.size(), closed) || closed.cpu) return 14;
        put32(0, 0x4D41484D); put32(12, 1000); // Entries beyond the block.
        if (sensors::parseAfterburner(block.data(), block.size(), closed)) return 15;
        if (sensors::parseAfterburner(block.data(), 10, closed) || sensors::parseAfterburner(nullptr, 100, closed)) return 16;
    }

    // The real reader must never throw or report implausible values; missing sensors are fine.
    sensors::Reader reader;
    for (int poll = 0; poll < 2; ++poll) {
        const auto reading = reader.read();
        if (!reading.sampled) return 8;
        for (const auto& value : {reading.cpu, reading.gpu})
            if (value && (*value < 0 || *value > sensors::plausible_max)) return 9;
        // Memory in use: gigabytes with a decimal below ten, whole above; a dash without both numbers.
        if (sensors::formatMemory(5222, 8192) != L"5.1 / 8 GB" || sensors::formatMemory(12000, 32768) != L"12 / 32 GB" ||
            sensors::formatMemory(std::nullopt, 8192) != L"—" || sensors::formatMemory(100, 0) != L"—") return 10;
        if (reading.ram_total && (!reading.ram_used || *reading.ram_used > *reading.ram_total)) return 11;
        if (reading.vram_total && (!reading.vram_used || *reading.vram_used > *reading.vram_total)) return 12;
        std::cout << "CPU " << (reading.cpu ? std::to_string(*reading.cpu) + " C" : "no sensor")
                  << ", GPU " << (reading.gpu ? std::to_string(*reading.gpu) + " C" : "no sensor") << '\n';
    }
    return 0;
}
