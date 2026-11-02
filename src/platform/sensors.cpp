#include "sensors.hpp"

#ifdef _WIN32
#include <Windows.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <dxgi.h>
#endif

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <string>
#include <vector>

namespace sensors {

std::optional<int> celsiusFromKelvin(double kelvin) {
    if (!std::isfinite(kelvin) || kelvin <= 0) return std::nullopt;
    const double celsius = std::round(kelvin - 273.15);
    if (celsius < cpu_plausible_min || celsius > plausible_max) return std::nullopt;
    return static_cast<int>(celsius);
}

std::optional<int> celsiusFromDeci(uint64_t tenths) {
    if (!tenths || tenths > static_cast<uint64_t>(plausible_max) * 10) return std::nullopt;
    return static_cast<int>((tenths + 5) / 10);
}

std::optional<int> hottest(std::span<const std::optional<int>> values) {
    std::optional<int> result;
    for (const auto& value : values)
        if (value && (!result || *value > *result)) result = value;
    return result;
}

namespace {
// MAHM_SHARED_MEMORY_HEADER: signature 'MAHM' (0xDEAD once Afterburner closed), version, header size, entry count, entry size.
// MAHM_SHARED_MEMORY_ENTRY: five MAX_PATH names (source, units, localized source, localized units, format), then the value.
constexpr uint32_t mahm_signature = 0x4D41484D;
constexpr size_t mahm_path = 260, mahm_value_offset = 5 * mahm_path;

uint32_t word(const unsigned char* at) { uint32_t value; std::memcpy(&value, at, sizeof(value)); return value; }

std::string lowered(const unsigned char* at, size_t limit) {
    std::string text;
    for (size_t i = 0; i < limit && at[i]; ++i) text += static_cast<char>(std::tolower(at[i]));
    return text;
}
} // namespace

bool parseAfterburner(const void* memory, size_t size, Reading& reading) {
    if (!memory || size < 20) return false;
    const auto* base = static_cast<const unsigned char*>(memory);
    if (word(base) != mahm_signature) return false;
    const size_t header = word(base + 8), count = word(base + 12), entry = word(base + 16);
    if (header < 20 || entry < mahm_value_offset + 4 || count > 512 || header + count * entry > size) return false;
    std::vector<std::optional<int>> cpu_zones;
    bool found = false;
    for (size_t i = 0; i < count; ++i) {
        const auto* item = base + header + i * entry;
        const auto name = lowered(item, mahm_path);
        float value = 0;
        std::memcpy(&value, item + mahm_value_offset, sizeof(value));
        if (!std::isfinite(value)) continue;
        const bool temperature = name.find("temperature") != std::string::npos;
        if (name == "cpu temperature" || (name.rfind("cpu", 0) == 0 && temperature)) {
            // One value for the package, or each core's: the hottest. Zero is a source with nothing to say.
            const auto celsius = value > 0 ? celsiusFromKelvin(value + 273.15) : std::nullopt;
            if (celsius) cpu_zones.push_back(celsius);
        }
    }
    if (const auto hottest_core = hottest(cpu_zones)) { reading.cpu = hottest_core; found = true; }
    return found;
}

std::wstring formatMemory(std::optional<int> used_mb, std::optional<int> total_mb) {
    if (!used_mb || !total_mb || *total_mb <= 0 || *used_mb < 0) return L"\u2014";
    const auto gigabytes = [](int mb) {
        const double value = mb / 1024.0;
        wchar_t text[32];
        swprintf(text, 32, value < 10 && std::fabs(value - std::round(value)) >= 0.05 ? L"%.1f" : L"%.0f", value);
        return std::wstring(text);
    };
    return gigabytes(*used_mb) + L" / " + gigabytes(*total_mb) + L" GB";
}

std::wstring format(std::optional<int> celsius) {
    return celsius ? std::to_wstring(*celsius) + L" \u00B0C" : std::wstring(L"\u2014");
}

std::optional<int> celsiusFromMilli(int64_t millidegrees, int minimum) {
    if (millidegrees <= 0 || millidegrees > static_cast<int64_t>(plausible_max) * 1000 + 499) return std::nullopt;
    const int celsius = static_cast<int>((millidegrees + 500) / 1000);
    if (celsius < std::max(minimum, 1)) return std::nullopt;
    return celsius;
}

#ifdef _WIN32
namespace {
// The few D3DKMT (gdi32) declarations used here, ABI-identical to d3dkmthk.h. They are
// resolved at run time, so the launcher needs neither the DDK headers nor an import
// that older Windows builds lack.
using KmtHandle = UINT;
struct KmtAdapterInfo { KmtHandle adapter; LUID luid; ULONG sources; BOOL precise_present; };
struct KmtEnumAdapters2 { ULONG count; KmtAdapterInfo* adapters; };
struct KmtQueryAdapterInfo { KmtHandle adapter; int type; void* data; UINT size; };
struct KmtCloseAdapter { KmtHandle adapter; };
struct KmtAdapterPerfData {
    UINT32 physical_index;
    ULONGLONG memory_frequency, max_memory_frequency, max_memory_frequency_oc, memory_bandwidth, pcie_bandwidth;
    ULONG fan_rpm, power, temperature; // temperature: tenths of °C.
    UCHAR power_state_override;
};
constexpr int kmt_adapter_type = 15;            // KMTQAITYPE_ADAPTERTYPE
constexpr int kmt_physical_adapter_count = 30;  // KMTQAITYPE_PHYSICALADAPTERCOUNT
constexpr int kmt_adapter_perf_data = 62;       // KMTQAITYPE_ADAPTERPERFDATA (WDDM 2.4+)
constexpr UINT kmt_software_device = 1u << 2;   // D3DKMT_ADAPTERTYPE::SoftwareDevice
using EnumAdapters2 = LONG(APIENTRY*)(KmtEnumAdapters2*);
using QueryAdapterInfo = LONG(APIENTRY*)(const KmtQueryAdapterInfo*);
using CloseAdapter = LONG(APIENTRY*)(const KmtCloseAdapter*);
static_assert(sizeof(KmtAdapterInfo) == 20 && sizeof(KmtAdapterPerfData) == 64);
} // namespace

struct Reader::State {
    PDH_HQUERY query = nullptr;
    PDH_HCOUNTER counter = nullptr;
    bool pdh_tried = false;
    PDH_HQUERY vram_query = nullptr;
    PDH_HCOUNTER vram_counter = nullptr;
    bool vram_tried = false;
    int vram_total_mb = 0; // The largest dedicated memory of a hardware adapter: the card the games run on.
    HMODULE gdi = nullptr;
    EnumAdapters2 enumerate = nullptr;
    QueryAdapterInfo query_adapter = nullptr;
    CloseAdapter close = nullptr;

    ~State() {
        if (query) PdhCloseQuery(query);
        if (vram_query) PdhCloseQuery(vram_query);
        if (gdi) FreeLibrary(gdi);
    }

    // Hottest ACPI thermal zone. English counter path: the localized names differ per
    // Windows display language ("Informacje o strefach termicznych" in Polish).
    std::optional<int> cpu() {
        if (!pdh_tried) {
            pdh_tried = true;
            if (PdhOpenQueryW(nullptr, 0, &query) != ERROR_SUCCESS) query = nullptr;
            else if (PdhAddEnglishCounterW(query, L"\\Thermal Zone Information(*)\\Temperature", 0, &counter) != ERROR_SUCCESS) {
                PdhCloseQuery(query);
                query = nullptr;
            }
        }
        if (!query || PdhCollectQueryData(query) != ERROR_SUCCESS) return std::nullopt;
        constexpr DWORD format = PDH_FMT_DOUBLE | PDH_FMT_NOCAP100;
        DWORD bytes = 0, count = 0;
        if (PdhGetFormattedCounterArrayW(counter, format, &bytes, &count, nullptr) != static_cast<PDH_STATUS>(PDH_MORE_DATA) || !bytes)
            return std::nullopt;
        // Items first, then their instance names; keep the buffer aligned for the doubles.
        std::vector<PDH_FMT_COUNTERVALUE_ITEM_W> items(bytes / sizeof(PDH_FMT_COUNTERVALUE_ITEM_W) + 1);
        if (PdhGetFormattedCounterArrayW(counter, format, &bytes, &count, items.data()) != ERROR_SUCCESS) return std::nullopt;
        std::vector<std::optional<int>> zones;
        for (DWORD i = 0; i < count && i < items.size(); ++i) {
            const auto& value = items[i].FmtValue;
            if (value.CStatus == PDH_CSTATUS_VALID_DATA || value.CStatus == PDH_CSTATUS_NEW_DATA)
                zones.push_back(celsiusFromKelvin(value.doubleValue));
        }
        return hottest(zones);
    }

    // Installed and used RAM (what Task Manager's Performance tab shows).
    void ram(Reading& reading) {
        MEMORYSTATUSEX status{sizeof(status)};
        if (!GlobalMemoryStatusEx(&status) || !status.ullTotalPhys) return;
        reading.ram_total = static_cast<int>(status.ullTotalPhys / (1024 * 1024));
        reading.ram_used = static_cast<int>((status.ullTotalPhys - status.ullAvailPhys) / (1024 * 1024));
    }

    // The busiest adapter's dedicated memory in use (the performance counter Task Manager reads), against
    // the largest dedicated memory DXGI reports; an integrated GPU, with none of its own, shows nothing.
    void vram(Reading& reading) {
        if (!vram_tried) {
            vram_tried = true;
            if (PdhOpenQueryW(nullptr, 0, &vram_query) != ERROR_SUCCESS) vram_query = nullptr;
            else if (PdhAddEnglishCounterW(vram_query, L"\\GPU Adapter Memory(*)\\Dedicated Usage", 0, &vram_counter) != ERROR_SUCCESS) {
                PdhCloseQuery(vram_query);
                vram_query = nullptr;
            }
            IDXGIFactory1* factory = nullptr;
            if (SUCCEEDED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory)))) {
                IDXGIAdapter1* adapter = nullptr;
                for (UINT i = 0; factory->EnumAdapters1(i, &adapter) == S_OK; ++i) {
                    DXGI_ADAPTER_DESC1 description{};
                    if (SUCCEEDED(adapter->GetDesc1(&description)) && !(description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE))
                        vram_total_mb = std::max(vram_total_mb, static_cast<int>(description.DedicatedVideoMemory / (1024 * 1024)));
                    adapter->Release();
                }
                factory->Release();
            }
        }
        if (!vram_query || vram_total_mb <= 0 || PdhCollectQueryData(vram_query) != ERROR_SUCCESS) return;
        constexpr DWORD format = PDH_FMT_LARGE;
        DWORD bytes = 0, count = 0;
        if (PdhGetFormattedCounterArrayW(vram_counter, format, &bytes, &count, nullptr) != static_cast<PDH_STATUS>(PDH_MORE_DATA) || !bytes) return;
        std::vector<PDH_FMT_COUNTERVALUE_ITEM_W> items(bytes / sizeof(PDH_FMT_COUNTERVALUE_ITEM_W) + 1);
        if (PdhGetFormattedCounterArrayW(vram_counter, format, &bytes, &count, items.data()) != ERROR_SUCCESS) return;
        long long used = -1;
        for (DWORD i = 0; i < count && i < items.size(); ++i) {
            const auto& value = items[i].FmtValue;
            if (value.CStatus == PDH_CSTATUS_VALID_DATA || value.CStatus == PDH_CSTATUS_NEW_DATA) used = std::max(used, value.largeValue);
        }
        if (used < 0) return;
        reading.vram_used = static_cast<int>(std::min<long long>(used / (1024 * 1024), vram_total_mb));
        reading.vram_total = vram_total_mb;
    }

    // Hottest hardware adapter, as reported by the WDDM 2.4+ driver (what Task Manager shows).
    std::optional<int> gpu() {
        if (!gdi) {
            gdi = LoadLibraryExW(L"gdi32.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
            if (!gdi) return std::nullopt;
            enumerate = reinterpret_cast<EnumAdapters2>(GetProcAddress(gdi, "D3DKMTEnumAdapters2"));
            query_adapter = reinterpret_cast<QueryAdapterInfo>(GetProcAddress(gdi, "D3DKMTQueryAdapterInfo"));
            close = reinterpret_cast<CloseAdapter>(GetProcAddress(gdi, "D3DKMTCloseAdapter"));
        }
        if (!enumerate || !query_adapter || !close) return std::nullopt;
        KmtEnumAdapters2 list{};
        if (enumerate(&list) < 0 || !list.count) return std::nullopt;
        std::vector<KmtAdapterInfo> adapters(list.count);
        list.adapters = adapters.data();
        if (enumerate(&list) < 0) return std::nullopt;
        adapters.resize(std::min<size_t>(adapters.size(), list.count));
        std::vector<std::optional<int>> readings;
        for (const auto& adapter : adapters) {
            UINT type = 0;
            const KmtQueryAdapterInfo type_query{adapter.adapter, kmt_adapter_type, &type, sizeof(type)};
            if (query_adapter(&type_query) >= 0 && !(type & kmt_software_device)) {
                UINT physical = 1;
                const KmtQueryAdapterInfo count_query{adapter.adapter, kmt_physical_adapter_count, &physical, sizeof(physical)};
                if (query_adapter(&count_query) < 0 || !physical) physical = 1;
                for (UINT index = 0; index < std::min<UINT>(physical, 8); ++index) {
                    KmtAdapterPerfData data{};
                    data.physical_index = index;
                    const KmtQueryAdapterInfo perf_query{adapter.adapter, kmt_adapter_perf_data, &data, sizeof(data)};
                    if (query_adapter(&perf_query) >= 0) readings.push_back(celsiusFromDeci(data.temperature));
                }
            }
            const KmtCloseAdapter closing{adapter.adapter};
            close(&closing);
        }
        return hottest(readings);
    }
};

Reader::Reader() : state_(std::make_unique<State>()) {}
Reader::~Reader() = default;

bool readAfterburner(Reading& reading) {
    const HANDLE mapping = OpenFileMappingW(FILE_MAP_READ, FALSE, L"MAHMSharedMemory");
    if (!mapping) return false;
    bool found = false;
    if (const void* view = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0)) {
        MEMORY_BASIC_INFORMATION info{};
        if (VirtualQuery(view, &info, sizeof(info))) found = parseAfterburner(view, info.RegionSize, reading);
        UnmapViewOfFile(view);
    }
    CloseHandle(mapping);
    return found;
}

Reading Reader::read() {
    Reading reading;
    reading.sampled = true;
    reading.cpu = state_->cpu();
    reading.gpu = state_->gpu();
    state_->ram(reading);
    state_->vram(reading);
    return reading;
}
#else
bool readAfterburner(Reading&) { return false; }
#endif // The Linux Reader is in sensors_linux.cpp.

} // namespace sensors
