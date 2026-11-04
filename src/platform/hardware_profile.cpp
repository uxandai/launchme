#include "hardware_profile.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>

#ifdef _WIN32
#include <Windows.h>
#include <dxgi.h>
#else
#include <sstream>
#endif

#ifdef _WIN32
HardwareProfile readHardwareProfile() {
    HardwareProfile profile;
    // The processor's marketing name, as Task Manager shows it.
    wchar_t name[256]{};
    DWORD size = sizeof(name) - sizeof(wchar_t);
    if (RegGetValueW(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", L"ProcessorNameString", RRF_RT_REG_SZ, nullptr, name, &size) == ERROR_SUCCESS)
        profile.cpu = name;
    MEMORYSTATUSEX memory{sizeof(memory)};
    if (GlobalMemoryStatusEx(&memory)) profile.ram_mb = static_cast<uint32_t>(std::min<unsigned long long>(memory.ullTotalPhys / (1024 * 1024), 0xffffffffULL));
    // The hardware adapter with the most memory of its own (an integrated one shares the system's).
    IDXGIFactory1* factory = nullptr;
    if (SUCCEEDED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory)))) {
        unsigned long long best = 0;
        IDXGIAdapter1* adapter = nullptr;
        for (UINT i = 0; factory->EnumAdapters1(i, &adapter) == S_OK; ++i) {
            DXGI_ADAPTER_DESC1 description{};
            if (SUCCEEDED(adapter->GetDesc1(&description)) && !(description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) {
                // Dedicated memory first; an adapter with little of its own counts half the memory it shares.
                const unsigned long long dedicated = description.DedicatedVideoMemory;
                const unsigned long long own = dedicated >= 1024ULL * 1024 * 1024 ? dedicated : std::max<unsigned long long>(dedicated, description.SharedSystemMemory / 2);
                if (profile.gpu.empty() || own > best) {
                    best = own;
                    profile.gpu = description.Description;
                    profile.vram_mb = static_cast<uint32_t>(std::min<unsigned long long>(own / (1024 * 1024), 0xffffffffULL));
                }
            }
            adapter->Release();
        }
        factory->Release();
    }
    const DWORD drives = GetLogicalDrives();
    for (int letter = 0; letter < 26; ++letter) {
        if (!(drives & (1u << letter))) continue;
        const wchar_t root[] = {static_cast<wchar_t>(L'A' + letter), L':', L'\\', 0};
        if (GetDriveTypeW(root) != DRIVE_FIXED) continue;
        ULARGE_INTEGER free{};
        if (GetDiskFreeSpaceExW(root, &free, nullptr, nullptr)) profile.free_mb = std::max<uint64_t>(profile.free_mb, free.QuadPart / (1024 * 1024));
    }
    return profile;
}
#else
HardwareProfile readHardwareProfile() {
    HardwareProfile profile;
    std::ifstream cpu("/proc/cpuinfo");
    for (std::string line; std::getline(cpu, line);) {
        if (line.rfind("model name", 0) != 0) continue;
        const auto colon = line.find(':');
        if (colon == std::string::npos) break;
        const auto text = line.substr(colon + 1);
        profile.cpu.assign(text.begin(), text.end());
        while (!profile.cpu.empty() && profile.cpu.front() == L' ') profile.cpu.erase(0, 1);
        break;
    }
    std::ifstream memory("/proc/meminfo");
    for (std::string line; std::getline(memory, line);) {
        if (line.rfind("MemTotal:", 0) != 0) continue;
        std::istringstream in(line.substr(9));
        unsigned long long kilobytes = 0;
        in >> kilobytes;
        profile.ram_mb = static_cast<uint32_t>(std::min<unsigned long long>(kilobytes / 1024, 0xffffffffULL));
        break;
    }
    std::error_code error;
    if (const char* home = std::getenv("HOME"))
        if (const auto space = std::filesystem::space(home, error); !error) profile.free_mb = space.available / (1024 * 1024);
    return profile;
}
#endif
