#pragma once
// What this PC is, for "will it run that game" (library/system_requirements.hpp): the processor's
// and the main graphics adapter's names, installed RAM, the adapter's own memory and the free disk
// space. Read on a worker; 0 / empty: unknown (the Linux build knows the processor and RAM only).
#include <cstdint>
#include <string>

struct HardwareProfile {
    std::wstring cpu, gpu;
    uint32_t ram_mb = 0, vram_mb = 0;
    uint64_t free_mb = 0; // The most free space on any fixed drive: where a game could go.
};

HardwareProfile readHardwareProfile();
