#pragma once
// A rough speed for a CPU or GPU named in free text ("GeForce GTX 1060 6GB", "Core i7-6700",
// "Ryzen 5 1600", "Radeon HD 3850 512 MB"): relative numbers for telling "faster than" from
// "slower than", not benchmarks. GPU: a GTX 1060 6GB is 100. CPU: a Core i5-6600K is 100.
// std::nullopt: the name is not recognised, and nothing is guessed. Portable, no I/O.
#include <optional>
#include <string_view>

namespace hardware {
std::optional<float> gpuScore(std::wstring_view name);
std::optional<float> cpuScore(std::wstring_view name);
}
