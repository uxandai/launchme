#pragma once
// Does this PC run that game? A store page's minimum / recommended requirements (Steam's
// pc_requirements HTML, GOG's systemRequirements) read into a Spec, set against the PC's own
// hardware (platform/hardware_profile.hpp). RAM and disk space are compared exactly; the CPU and
// GPU through hardware_rating.hpp's rough scores. Whatever cannot be read or recognised stays
// unknown: nothing is guessed, and a verdict that rests on it is grey. Portable, no I/O.
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sysreq {
// One column of the store's table: the texts as written (tags removed) and the amounts read from them.
struct Spec {
    std::wstring cpu, gpu, ram, storage;
    std::optional<uint32_t> ram_mb, storage_mb;
    bool empty() const { return cpu.empty() && gpu.empty() && ram.empty() && storage.empty(); }
};
struct Requirements {
    std::optional<Spec> minimum, recommended;
    bool any() const { return minimum || recommended; }
};

// store.steampowered.com/api/appdetails (any language) for `app_id`; empty when the page has none.
Requirements fromSteam(uint32_t app_id, std::string_view json);
// api.gog.com/v2/games/<id>: the Windows entry.
Requirements fromGog(std::string_view json);

// "12 GB RAM" -> 12288, "512 MB" -> 512, "60 SSD GB available space" -> 61440; nullopt: no amount.
std::optional<uint32_t> megabytesIn(std::wstring_view text);

struct Machine {
    std::wstring cpu, gpu;
    uint32_t ram_mb = 0;   // Installed RAM; 0: unknown.
    uint32_t vram_mb = 0;  // The main GPU's own memory; 0: unknown.
    uint64_t free_mb = 0;  // Free disk space where a game would go; 0: unknown.
};

enum class Level { unknown, good, ok, poor };  // grey, green, yellow, red
struct Row {
    enum class Part { cpu, gpu, ram, storage } part = Part::cpu;
    Level level = Level::unknown;
    std::wstring yours, minimum, recommended;  // What the PC has, what each column asks.
};
struct Verdict {
    Level level = Level::unknown;
    std::vector<Row> rows;
};

// `installed`: the game is on disk already, so its disk space is not asked.
Verdict assess(const Machine& machine, const Requirements& requirements, bool installed);
}
