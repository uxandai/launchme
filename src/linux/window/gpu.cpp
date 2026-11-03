// The GPU behind every window: one Impeller context on Vulkan, else OpenGL, else none (windows
// stay blank: there is no CPU renderer). The context itself lives in src/linux/impeller; this file
// decides which backend runs and lets the windows reach it.
//   LAUNCHME_RENDERER=vulkan|gl  picks one (default: Vulkan on a real GPU, then OpenGL; "vulkan"
//                                also accepts a software device such as lavapipe).
//   LAUNCHME_VSYNC=1             waits for the display's refresh at present (default off: the
//                                launcher paces its frames itself, FrameClock).
//   LAUNCHME_GPU_DEBUG=1         frames shown per second and the time a present takes.
#include "window_internal.hpp"
#include "gpu.hpp"
#include "../gfx/gfx_internal.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace lm_window::detail {
namespace {
using gpu::Backend;

void note(const char* format, const std::string& detail = {}) {
    std::fprintf(stderr, "launchme gpu: %s%s%s\n", format, detail.empty() ? "" : ": ", detail.c_str());
}

std::string environment(const char* name) {
    const char* value = std::getenv(name);
    return value ? value : "";
}

struct State {
    Backend backend = Backend::none;
    bool decided = false;
    bool vsync = false;
    SDL_GLContext gl = nullptr;
};
State& gpuState() { static State instance; return instance; }

bool createGl(SDL_Window* window) {
    auto& s = gpuState();
    s.gl = SDL_GL_CreateContext(window);
    if (!s.gl) { note("no OpenGL context", SDL_GetError()); return false; }
    const auto fail = [&](const char* why) {
        note(why, SDL_GetError());
        SDL_GL_DestroyContext(s.gl);
        s.gl = nullptr;
        return false;
    };
    if (!SDL_GL_MakeCurrent(window, s.gl)) return fail("the OpenGL context cannot be made current");
    // A context that answers nothing (no driver behind it) would crash the probing.
    const auto get_string = reinterpret_cast<const unsigned char* (*)(unsigned)>(SDL_GL_GetProcAddress("glGetString"));
    const auto* version = get_string ? reinterpret_cast<const char*>(get_string(0x1F02)) : nullptr;  // GL_VERSION
    const auto* renderer = get_string ? reinterpret_cast<const char*>(get_string(0x1F01)) : nullptr; // GL_RENDERER
    if (!version || !renderer) return fail("the OpenGL context has no driver");
    SDL_GL_SetSwapInterval(s.vsync ? 1 : 0);
    if (!lm_impeller::tryOpenGl()) return fail("Impeller refused the OpenGL context");
    note("OpenGL", std::string(renderer) + " (" + version + ")");
    return true;
}

void decide() {
    auto& s = gpuState();
    if (s.decided) return;
    s.decided = true;
    s.vsync = environment("LAUNCHME_VSYNC") == "1";
    const std::string wanted = environment("LAUNCHME_RENDERER");
    if (wanted != "gl" && lm_impeller::tryVulkan(wanted == "vulkan")) { s.backend = Backend::vulkan; return; }
    // OpenGL needs a window for its context: created with the first one.
    s.backend = Backend::gl;
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);
    SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 0);
}
} // namespace

namespace gpu {
Backend backend() { decide(); return gpuState().backend; }

SDL_WindowFlags windowFlags() {
    switch (backend()) {
    case Backend::vulkan: return SDL_WINDOW_VULKAN;
    case Backend::gl: return SDL_WINDOW_OPENGL;
    default: return 0;
    }
}

void created(Window* target) {
    auto& s = gpuState();
    if (s.backend == Backend::gl && !s.gl && !createGl(target->sdl)) {
        s.backend = Backend::none;
        note("no GPU: windows stay blank");
    }
}

void release(Window* target) {
    if (target) lm_impeller::releaseWindow(target->sdl);
}

void shutdown() {
    auto& s = gpuState();
    lm_impeller::shutdown();
    if (s.gl) { SDL_GL_DestroyContext(s.gl); s.gl = nullptr; }
}
} // namespace gpu
} // namespace lm_window::detail

using namespace lm_window::detail;

namespace lm_gfx {
bool presentWindow(HWND handle) {
    Window* target = window(handle);
    if (!target) return false;
    auto& s = gpuState();
    if (s.gl && !SDL_GL_MakeCurrent(target->sdl, s.gl)) return false;
    // LAUNCHME_GPU_DEBUG=1: frames shown per second and the time a present takes, every five seconds.
    static const bool debug = !environment("LAUNCHME_GPU_DEBUG").empty();
    struct Timing {
        bool on;
        uint64_t started;
        ~Timing() {
            if (!on) return;
            static uint64_t since = SDL_GetTicksNS(), frames = 0, spent = 0;
            const uint64_t now = SDL_GetTicksNS();
            ++frames;
            spent += now - started;
            if (now - since >= 5'000'000'000ull) {
                note("frames per second / present ms",
                     std::to_string(frames * 1e9 / static_cast<double>(now - since)) + " / " + std::to_string(spent / 1e6 / static_cast<double>(frames)));
                since = now;
                frames = spent = 0;
            }
        }
    } timing{debug, debug ? SDL_GetTicksNS() : 0};
    return lm_impeller::present(target->sdl);
}

void windowPixelSize(HWND handle, UINT32& width, UINT32& height) {
    width = height = 0;
    Window* target = window(handle);
    if (!target) return;
    int w = 0, h = 0;
    SDL_GetWindowSizeInPixels(target->sdl, &w, &h);
    width = static_cast<UINT32>(std::max(0, w));
    height = static_cast<UINT32>(std::max(0, h));
}

std::shared_ptr<lm_impeller::Canvas> windowCanvas(HWND handle, int width, int height) {
    Window* target = window(handle);
    if (!target || width <= 0 || height <= 0) return nullptr;
    auto& s = gpuState();
    if (s.gl && !SDL_GL_MakeCurrent(target->sdl, s.gl)) return nullptr;
    return lm_impeller::beginFrame(target->sdl, width, height);
}
} // namespace lm_gfx
