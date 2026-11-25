#pragma once
// What this PC is, for "will it run that game" (library/system_requirements.hpp): the processor's
// and the main graphics adapter's names, installed RAM, the adapter's own memory and the free disk
// space. Read on a worker; 0 / empty: unknown.
// Windows: the registry, GlobalMemoryStatusEx, DXGI and the fixed drives. Linux: /proc/cpuinfo and
// /proc/meminfo, Vulkan's physical devices (libvulkan.so.1 loaded at run time: the name the driver
// gives and its device-local heap) and every mounted disk file system (/proc/mounts).
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

struct HardwareProfile {
    std::wstring cpu, gpu;
    uint32_t ram_mb = 0, vram_mb = 0;
    uint64_t free_mb = 0; // The most free space on any fixed drive: where a game could go.
};

HardwareProfile readHardwareProfile();

// The pure parts of the Linux reading, for tests.
namespace hardware_profile {
// A driver's device name without its trailing note: "AMD Radeon RX 6800 XT (RADV NAVI21)" →
// "AMD Radeon RX 6800 XT", "Intel(R) Arc(tm) A770 Graphics (DG2)" → "Intel(R) Arc(tm) A770 Graphics".
std::wstring cleanGpuName(std::string_view name);
// Memory a game can count on, MB: the largest device-local heap; one under 1 GiB (an integrated GPU's
// carve-out) counts half of the largest shared heap instead when that is more (as DXGI's shared memory).
uint32_t vramMb(uint64_t local_bytes, uint64_t shared_bytes);
// The most space available on a disk file system in /proc/mounts' text (ext4, btrfs, xfs, NTFS, exFAT…;
// not tmpfs, overlays, squashfs images or network shares), MB. `available` gives a mount point's bytes.
uint64_t largestFreeMb(std::string_view mounts, const std::function<std::optional<uint64_t>(const std::string&)>& available);
}
