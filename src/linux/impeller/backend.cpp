// The Impeller context (Vulkan, else OpenGL) and the windows' frames. SDL owns the windows;
// Impeller owns the Vulkan swapchains and draws into the SDL OpenGL framebuffer.
#include "state.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include <dlfcn.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unordered_map>

namespace lm_impeller {
namespace {
struct Window {
    std::shared_ptr<Canvas> canvas;
    SwapchainHandle swapchain;
    VkSurfaceKHR surface = nullptr;
    int swapchain_width = 0, swapchain_height = 0;
    bool sized_by_window = false; // The surface has no size of its own (Wayland), see surfaceCapabilities.
    bool offscreen = false; // No surface to present to (a headless device).
};
std::unordered_map<SDL_Window*, Window> windows;
std::unordered_map<VkSurfaceKHR, Window*> surfaces;

// VkSurfaceCapabilitiesKHR up to the extents, which is all that is changed here.
struct SurfaceCapabilities {
    uint32_t min_image_count, max_image_count;
    uint32_t current_width, current_height;
    uint32_t min_width, min_height;
    uint32_t max_width, max_height;
};
constexpr uint32_t extent_undefined = 0xFFFFFFFF;
using GetSurfaceCapabilities = int32_t (*)(void* physical, VkSurfaceKHR surface, SurfaceCapabilities* capabilities);
GetSurfaceCapabilities real_surface_capabilities = nullptr;

// Impeller's C API creates every swapchain at 1x1 and clamps that to the surface's extents. X11
// gives the window's size as both limits; a Wayland surface has no size until a swapchain gives it
// one, so the window would show one stretched pixel. Here such a surface reports its window's size.
int32_t surfaceCapabilities(void* physical, VkSurfaceKHR surface, SurfaceCapabilities* capabilities) {
    const int32_t result = real_surface_capabilities(physical, surface, capabilities);
    const auto it = surfaces.find(surface);
    if (result != 0 || it == surfaces.end() || capabilities->current_width != extent_undefined) return result;
    Window& w = *it->second;
    w.sized_by_window = true;
    capabilities->current_width = capabilities->min_width = capabilities->max_width =
        std::clamp(static_cast<uint32_t>(w.swapchain_width), capabilities->min_width, capabilities->max_width);
    capabilities->current_height = capabilities->min_height = capabilities->max_height =
        std::clamp(static_cast<uint32_t>(w.swapchain_height), capabilities->min_height, capabilities->max_height);
    return result;
}

void destroySwapchain(Window& w) {
    w.swapchain.reset();
    surfaces.erase(w.surface);
    w.surface = nullptr;
}

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
        void* found = get ? get(instance, name) : nullptr;
        if (found && !std::strcmp(name, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR")) {
            real_surface_capabilities = reinterpret_cast<GetSurfaceCapabilities>(found);
            return reinterpret_cast<void*>(surfaceCapabilities);
        }
        return found;
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
    int pixel_width = 0, pixel_height = 0;
    SDL_GetWindowSizeInPixels(window, &pixel_width, &pixel_height);
    // Nothing tells Impeller that a surface sized by its window has resized: a new swapchain.
    if (w.swapchain && w.sized_by_window && pixel_width > 0 && pixel_height > 0 &&
        (pixel_width != w.swapchain_width || pixel_height != w.swapchain_height)) destroySwapchain(w);
    if (s.vulkan && !w.swapchain && !w.offscreen) {
        ImpellerContextVulkanInfo info{};
        VkSurfaceKHR surface = nullptr;
        if (!ImpellerContextGetVulkanInfo(s.context.get(), &info)) return nullptr;
        if (SDL_Vulkan_CreateSurface(window, static_cast<VkInstance>(info.vk_instance), nullptr, &surface)) {
            w.surface = surface;
            w.swapchain_width = pixel_width > 0 ? pixel_width : width;
            w.swapchain_height = pixel_height > 0 ? pixel_height : height;
            surfaces[surface] = &w;
            // Ownership of the VkSurfaceKHR passes to Impeller.
            w.swapchain.reset(ImpellerVulkanSwapchainCreateNew(s.context.get(), reinterpret_cast<void*>(surface)));
            if (!w.swapchain) { destroySwapchain(w); return nullptr; }
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

void releaseWindow(SDL_Window* window) {
    if (const auto it = windows.find(window); it != windows.end()) {
        destroySwapchain(it->second);
        windows.erase(it);
    }
}

void shutdown() {
    for (auto& [window, w] : windows) destroySwapchain(w);
    windows.clear();
    releaseAllImages();
    auto& s = state();
    s.context.reset();
    ++s.epoch;
}
} // namespace lm_impeller
