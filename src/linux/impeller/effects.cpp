// The effect shaders: compiled offline by the pinned engine's impellerc (tools/linux/build-shaders.sh)
// into runtime stages for Vulkan and OpenGL ES, embedded here and made into fragment programs once.
#include "state.hpp"

#include <array>
#include <cstdio>

namespace lm_impeller {
namespace {
#include "effect_shaders.inc"

struct Blob { const unsigned char* data; size_t size; const char* name; };
constexpr std::array<Blob, 4> blobs{{
    {point_specular_shader, sizeof(point_specular_shader), "point_specular"},
    {premultiplied_matrix_shader, sizeof(premultiplied_matrix_shader), "premultiplied_matrix"},
    {arithmetic_shader, sizeof(arithmetic_shader), "arithmetic"},
    {upscale_shader, sizeof(upscale_shader), "upscale"},
}};

struct Loaded {
    ImpellerFragmentProgram program = nullptr;
    bool tried = false;
};
} // namespace

ImpellerFragmentProgram program(Program which) {
    // The programs hold no context state: made once, kept for the process.
    static std::array<Loaded, blobs.size()> loaded;
    const auto index = static_cast<size_t>(which);
    if (index >= blobs.size()) return nullptr;
    auto& entry = loaded[index];
    if (!entry.tried) {
        entry.tried = true;
        const ImpellerMapping mapping{blobs[index].data, blobs[index].size, nullptr};
        entry.program = ImpellerFragmentProgramNew(&mapping, nullptr);
        if (!entry.program) std::fprintf(stderr, "launchme gpu: effect shader %s unavailable\n", blobs[index].name);
    }
    return entry.program;
}
} // namespace lm_impeller
