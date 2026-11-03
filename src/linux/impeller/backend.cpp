// The Impeller context (Vulkan, else OpenGL) and the windows' frames. SDL owns the windows;
// Impeller owns the Vulkan swapchains and draws into the SDL OpenGL framebuffer.
#include "state.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include <dlfcn.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unordered_map>

namespace lm_impeller {
namespace {
struct Window {
    std::shared_ptr<Canvas> canvas;
    SwapchainHandle swapchain;
    bool offscreen = false; // No surface to present to (a headless device).
};
std::unordered_map<SDL_Window*, Window> windows;

// The head of VkPhysicalDeviceProperties: enough to name the device and tell software from hardware.
struct DeviceProperties {
    uint32_t api_version, driver_version, vendor_id, device_id, device_type;
    char name[256];
    unsigned char rest[1024];
};
constexpr uint32_t device_type_cpu = 4;
using GetProperties = void (*)(void* physical, DeviceProperties* properties);
using GetProc = void* (*)(void* instance, const char* name);

bool compatible() {
    if (LaunchmeImpellerGetAbiVersion() != LAUNCHME_IMPELLER_ABI || ImpellerGetVersion() != IMPELLER_VERSION) {
        std::fprintf(stderr, "launchme gpu: incompatible Impeller SDK\n");
        return false;
    }
    return true;
}

// Creates a Vulkan context through `get_proc`; `deviceOk` may veto the chosen device.
template <class Veto>
bool createVulkan(GetProc get_proc, bool want_validation, Veto veto, bool headless) {
    static GetProc loader = nullptr;
    loader = get_proc;
    ImpellerContextVulkanSettings settings{};
    settings.proc_address_callback = [](void* instance, const char* name, void*) -> void* { return loader(instance, name); };
    settings.enable_vulkan_validation = want_validation;
    ContextHandle created(ImpellerContextCreateVulkanNew(IMPELLER_VERSION, &settings));
    ImpellerContextVulkanInfo info{};
    if (!created || !ImpellerContextGetVulkanInfo(created.get(), &info)) return false;
    DeviceProperties properties{};
    const auto get = reinterpret_cast<GetProperties>(get_proc(info.vk_instance, "vkGetPhysicalDeviceProperties"));
    if (!get) return false;
    get(info.vk_physical_device, &properties);
    if (!veto(info, properties)) return false;
    auto& s = state();
    s.context = std::move(created);
    s.thread = std::this_thread::get_id();
    s.vulkan = true;
    s.headless = headless;
    ++s.epoch;
    std::fprintf(stderr, "launchme gpu: Impeller Vulkan: %s (BC1/BC3=%s)\n", properties.name, supportsCompression(1) ? "yes" : "no");
    return true;
}
} // namespace

State& state() { static State value; return value; }
ImpellerContext context() { return state().context.get(); }
bool active() { return state().context && state().thread == std::this_thread::get_id(); }

bool tryVulkan(bool allow_software) {
    if (!compatible() || !SDL_Vulkan_LoadLibrary(nullptr)) return false;
    if (!SDL_Vulkan_GetVkGetInstanceProcAddr()) return false;
    const GetProc get_proc = [](void* instance, const char* name) -> void* {
        const auto get = reinterpret_cast<void* (*)(void*, const char*)>(SDL_Vulkan_GetVkGetInstanceProcAddr());
        return get ? get(instance, name) : nullptr;
    };
    return createVulkan(get_proc, std::getenv("LAUNCHME_IMPELLER_VALIDATION") != nullptr, [&](const ImpellerContextVulkanInfo& info, const DeviceProperties& p) {
        if (!allow_software && p.device_type == device_type_cpu) return false;
        return SDL_Vulkan_GetPresentationSupport(static_cast<VkInstance>(info.vk_instance), static_cast<VkPhysicalDevice>(info.vk_physical_device),
                                                  info.graphics_queue_family_index);
    }, false);
}

bool tryHeadless() {
    if (!compatible()) return false;
    static void* library = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
    if (!library) return false;
    static const auto entry = reinterpret_cast<void* (*)(void*, const char*)>(dlsym(library, "vkGetInstanceProcAddr"));
    if (!entry) return false;
    return createVulkan(entry, false, [](const ImpellerContextVulkanInfo&, const DeviceProperties&) { return true; }, true);
}

bool tryOpenGl() {
    if (!compatible()) return false;
    ContextHandle created(ImpellerContextCreateOpenGLESNew(IMPELLER_VERSION, [](const char* name, void*) -> void* {
        if (!std::strncmp(name, "egl", 3)) return nullptr;
        return reinterpret_cast<void*>(SDL_GL_GetProcAddress(name));
    }, nullptr));
    if (!created) return false;
    auto& s = state();
    s.context = std::move(created);
    s.thread = std::this_thread::get_id();
    s.vulkan = false;
    s.headless = false;
    ++s.epoch;
    std::fprintf(stderr, "launchme gpu: Impeller OpenGL (BC1/BC3=%s)\n", supportsCompression(1) ? "yes" : "no");
    return true;
}

bool bgraNative() { return state().vulkan; }

bool supportsCompression(unsigned format) {
    if (!active() || (format != 1 && format != 3)) return false;
    return LaunchmeImpellerSupportsFormat(context(), format == 1 ? kLaunchmeImpellerBC1 : kLaunchmeImpellerBC3);
}

std::shared_ptr<Canvas> beginFrame(SDL_Window* window, int width, int height) {
    if (!active() || !window || width <= 0 || height <= 0 || width > 16384 || height > 16384) return nullptr;
    auto& s = state();
    auto& w = windows[window];
    w.canvas.reset();
    ++s.frame;
    trimImages(s.frame);
    if (s.vulkan && !w.swapchain && !w.offscreen) {
        ImpellerContextVulkanInfo info{};
        VkSurfaceKHR surface = nullptr;
        if (!ImpellerContextGetVulkanInfo(s.context.get(), &info)) return nullptr;
        if (SDL_Vulkan_CreateSurface(window, static_cast<VkInstance>(info.vk_instance), nullptr, &surface)) {
            // Ownership of the VkSurfaceKHR passes to Impeller.
            w.swapchain.reset(ImpellerVulkanSwapchainCreateNew(s.context.get(), reinterpret_cast<void*>(surface)));
            if (!w.swapchain) return nullptr;
        } else if (s.headless) {
            w.offscreen = true;
        } else {
            return nullptr;
        }
    }
    w.canvas = std::make_shared<Canvas>(width, height);
    return w.canvas->healthy() ? w.canvas : nullptr;
}

bool present(SDL_Window* window) {
    const auto it = windows.find(window);
    if (!active() || it == windows.end() || !it->second.canvas) return false;
    auto& w = it->second;
    const auto list = w.canvas->finish();
    if (!list) { std::fprintf(stderr, "launchme gpu: Impeller frame recording failed\n"); return false; }
    auto& s = state();
    if (w.offscreen) {
        // Nothing to show on: the frame is still executed (and dropped).
        TextureHandle rendered(LaunchmeImpellerRenderToTextureNew(s.context.get(), list, w.canvas->width(), w.canvas->height()));
        return rendered != nullptr;
    }
    int width = 0, height = 0;
    SDL_GetWindowSizeInPixels(window, &width, &height);
    if (width <= 0 || height <= 0 || (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED)) return true;
    SurfaceHandle surface;
    if (s.vulkan) {
        surface.reset(ImpellerVulkanSwapchainAcquireNextSurfaceNew(w.swapchain.get()));
    } else {
        const ImpellerISize size{width, height};
        surface.reset(ImpellerSurfaceCreateWrappedFBONew(s.context.get(), 0, kImpellerPixelFormatRGBA8888, &size));
    }
    if (!surface || !ImpellerSurfaceDrawDisplayList(surface.get(), list)) return false;
    return s.vulkan ? ImpellerSurfacePresent(surface.get()) : SDL_GL_SwapWindow(window);
}

void releaseWindow(SDL_Window* window) { windows.erase(window); }

void shutdown() {
    windows.clear();
    releaseAllImages();
    auto& s = state();
    s.context.reset();
    ++s.epoch;
}
} // namespace lm_impeller
