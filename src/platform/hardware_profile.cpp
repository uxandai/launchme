#include "hardware_profile.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>

#ifdef _WIN32
#include <Windows.h>
#include <dxgi.h>
#else
#include <dlfcn.h>
#include <iterator>
#include <memory>
#include <sstream>
#include <vector>
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
namespace {
// The few Vulkan 1.0 declarations used (vulkan_core.h), resolved from libvulkan.so.1 at run time.
using VkFn = void (*)();
using GetProc = VkFn (*)(void*, const char*);
struct VkApplication { int type = 0; const void* next = nullptr; const char* name = nullptr; uint32_t version = 0; const char* engine = nullptr; uint32_t engine_version = 0; uint32_t api = 0; };
struct VkInstanceInfo { int type = 1; const void* next = nullptr; uint32_t flags = 0; const VkApplication* application = nullptr;
                        uint32_t layers = 0; const char* const* layer_names = nullptr; uint32_t extensions = 0; const char* const* extension_names = nullptr; };
struct VkProperties { uint32_t api, driver, vendor, device, type; char name[256]; uint8_t rest[2048]; }; // rest: pipelineCacheUUID, limits, sparse properties.
struct VkHeap { uint64_t size; uint32_t flags; };
struct VkMemory { uint32_t type_count; struct { uint32_t flags, heap; } types[32]; uint32_t heap_count; VkHeap heaps[16]; };
using CreateInstance = int (*)(const VkInstanceInfo*, const void*, void**);
using DestroyInstance = void (*)(void*, const void*);
using EnumerateDevices = int (*)(void*, uint32_t*, void**);
using GetProperties = void (*)(void*, VkProperties*);
using GetMemory = void (*)(void*, VkMemory*);
constexpr uint32_t vk_api_1_0 = 1u << 22, vk_heap_device_local = 1;
constexpr uint32_t vk_integrated = 1, vk_discrete = 2;

// The physical device a game would run on: a discrete one first, else an integrated one (never a
// software rasterizer such as llvmpipe).
void readVulkanGpu(HardwareProfile& profile) {
    void* library = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
    if (!library) return;
    const auto get = reinterpret_cast<GetProc>(dlsym(library, "vkGetInstanceProcAddr"));
    const auto create = get ? reinterpret_cast<CreateInstance>(get(nullptr, "vkCreateInstance")) : nullptr;
    VkApplication application; application.name = "launchme"; application.api = vk_api_1_0;
    VkInstanceInfo info; info.application = &application;
    void* instance = nullptr;
    if (create && create(&info, nullptr, &instance) == 0 && instance) {
        const auto destroy = reinterpret_cast<DestroyInstance>(get(instance, "vkDestroyInstance"));
        const auto enumerate = reinterpret_cast<EnumerateDevices>(get(instance, "vkEnumeratePhysicalDevices"));
        const auto properties = reinterpret_cast<GetProperties>(get(instance, "vkGetPhysicalDeviceProperties"));
        const auto memory = reinterpret_cast<GetMemory>(get(instance, "vkGetPhysicalDeviceMemoryProperties"));
        uint32_t count = 0;
        if (enumerate && properties && memory && enumerate(instance, &count, nullptr) == 0 && count) {
            std::vector<void*> devices(std::min(count, 16u));
            count = static_cast<uint32_t>(devices.size());
            if (enumerate(instance, &count, devices.data()) >= 0) {
                int best_rank = 0;
                for (uint32_t i = 0; i < count; ++i) {
                    auto described = std::make_unique<VkProperties>();
                    properties(devices[i], described.get());
                    const int rank = described->type == vk_discrete ? 2 : described->type == vk_integrated ? 1 : 0;
                    if (rank <= best_rank) continue;
                    auto heaps = std::make_unique<VkMemory>();
                    memory(devices[i], heaps.get());
                    uint64_t local = 0, shared = 0;
                    for (uint32_t h = 0; h < std::min(heaps->heap_count, 16u); ++h) {
                        auto& largest = heaps->heaps[h].flags & vk_heap_device_local ? local : shared;
                        largest = std::max(largest, heaps->heaps[h].size);
                    }
                    described->name[255] = 0;
                    best_rank = rank;
                    profile.gpu = hardware_profile::cleanGpuName(described->name);
                    profile.vram_mb = hardware_profile::vramMb(local, shared);
                }
            }
        }
        if (destroy) destroy(instance, nullptr);
    }
    dlclose(library);
}
}

HardwareProfile readHardwareProfile() {
    HardwareProfile profile;
    readVulkanGpu(profile);
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
    // Every disk, not only the home folder's: a library on a second drive or a Steam Deck's microSD card.
    std::ifstream mounts_file("/proc/mounts", std::ios::binary);
    const std::string mounts((std::istreambuf_iterator<char>(mounts_file)), std::istreambuf_iterator<char>());
    profile.free_mb = hardware_profile::largestFreeMb(mounts, [](const std::string& point) -> std::optional<uint64_t> {
        std::error_code error;
        const auto space = std::filesystem::space(point, error);
        return error ? std::nullopt : std::optional<uint64_t>(space.available);
    });
    if (!profile.free_mb) {
        std::error_code error;
        if (const char* home = std::getenv("HOME"))
            if (const auto space = std::filesystem::space(home, error); !error) profile.free_mb = space.available / (1024 * 1024);
    }
    return profile;
}
#endif

std::wstring hardware_profile::cleanGpuName(std::string_view name) {
    while (!name.empty() && (name.back() == ' ' || name.back() == '\n')) name.remove_suffix(1);
    if (name.ends_with(')')) {
        int depth = 0;
        for (size_t i = name.size(); i-- > 0;) {
            if (name[i] == ')') ++depth;
            else if (name[i] == '(' && --depth == 0) {
                if (i > 0 && name[i - 1] == ' ') name = name.substr(0, i);
                break;
            }
        }
    }
    while (!name.empty() && name.back() == ' ') name.remove_suffix(1);
    std::wstring out;
    for (const char c : name) out += static_cast<wchar_t>(static_cast<unsigned char>(c) < 0x80 ? c : L'?'); // Driver names are ASCII.
    return out;
}

uint32_t hardware_profile::vramMb(uint64_t local_bytes, uint64_t shared_bytes) {
    const uint64_t own = local_bytes >= 1024ULL * 1024 * 1024 ? local_bytes : std::max<uint64_t>(local_bytes, shared_bytes / 2);
    return static_cast<uint32_t>(std::min<uint64_t>(own / (1024 * 1024), 0xffffffffULL));
}

uint64_t hardware_profile::largestFreeMb(std::string_view mounts, const std::function<std::optional<uint64_t>(const std::string&)>& available) {
    static constexpr std::string_view disks[] = {"ext2", "ext3", "ext4", "btrfs", "xfs", "f2fs", "bcachefs", "zfs", "jfs", "reiserfs",
                                                 "ntfs", "ntfs3", "fuseblk", "exfat", "vfat"};
    uint64_t best = 0;
    while (!mounts.empty()) {
        const auto end = mounts.find('\n');
        const auto line = mounts.substr(0, end);
        mounts = end == std::string_view::npos ? std::string_view{} : mounts.substr(end + 1);
        // "device mountpoint type options 0 0"; spaces in the mount point are written as \040.
        const auto a = line.find(' ');
        const auto b = a == std::string_view::npos ? a : line.find(' ', a + 1);
        const auto c = b == std::string_view::npos ? b : line.find(' ', b + 1);
        if (c == std::string_view::npos) continue;
        const auto type = line.substr(b + 1, c - b - 1);
        if (std::find(std::begin(disks), std::end(disks), type) == std::end(disks)) continue;
        const auto device = line.substr(0, a);
        if (!device.starts_with("/dev/") && type != "zfs" && type != "fuseblk") continue; // Not a disk (a loop image is /dev/loop: skipped below).
        if (device.starts_with("/dev/loop")) continue;
        std::string point;
        const auto raw = line.substr(a + 1, b - a - 1);
        for (size_t i = 0; i < raw.size(); ++i) {
            if (raw[i] == '\\' && i + 3 < raw.size() && raw[i + 1] >= '0' && raw[i + 1] <= '3') {
                point += static_cast<char>((raw[i + 1] - '0') * 64 + (raw[i + 2] - '0') * 8 + (raw[i + 3] - '0'));
                i += 3;
            } else point += raw[i];
        }
        if (point.starts_with("/boot") || point.starts_with("/efi")) continue;
        if (const auto bytes = available(point)) best = std::max<uint64_t>(best, *bytes / (1024 * 1024));
    }
    return best;
}
