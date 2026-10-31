#include "internal.hpp"
#include "include/gpu/vk/VulkanBackendContext.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <cstdlib>
#include <cstdio>
#include <cstring>

namespace lm_impeller {
namespace {
bool wanted = false;
struct Window {
    std::shared_ptr<Canvas> canvas;
    Swapchain swapchain;
    int width = 0, height = 0;
};
std::unordered_map<SDL_Window*, Window> windows;

bool compatible() {
    if (LaunchmeImpellerGetAbiVersion() != LAUNCHME_IMPELLER_ABI || ImpellerGetVersion() != IMPELLER_VERSION) {
        std::fprintf(stderr, "launchme gpu: incompatible Impeller SDK\n");
        return false;
    }
    return true;
}
} // namespace

Resources& resources() { static Resources value; return value; }
bool compiled() { return true; }
void request(bool enabled) { wanted = enabled; }
bool requested() {
    const char* override = std::getenv("LAUNCHME_IMPELLER");
    if (override && !std::strcmp(override, "0")) return false;
    if (override && !std::strcmp(override, "1")) return true;
    return wanted;
}
bool active() { return resources().context && resources().thread == std::this_thread::get_id(); }

bool tryVulkan(bool allowSoftware) {
    if (!compatible() || !SDL_Vulkan_LoadLibrary(nullptr)) return false;
    const auto proc = reinterpret_cast<PFN_vkGetInstanceProcAddr>(SDL_Vulkan_GetVkGetInstanceProcAddr());
    if (!proc) return false;
    ImpellerContextVulkanSettings settings{};
    settings.proc_address_callback = [](void* instance, const char* name, void*) -> void* {
        const auto get = reinterpret_cast<PFN_vkGetInstanceProcAddr>(SDL_Vulkan_GetVkGetInstanceProcAddr());
        return get ? reinterpret_cast<void*>(get(static_cast<VkInstance>(instance), name)) : nullptr;
    };
    settings.enable_vulkan_validation = std::getenv("LAUNCHME_IMPELLER_VALIDATION") != nullptr;
    Context context(ImpellerContextCreateVulkanNew(IMPELLER_VERSION, &settings));
    ImpellerContextVulkanInfo info{};
    if (!context || !ImpellerContextGetVulkanInfo(context.get(), &info)) return false;
    const auto instance = static_cast<VkInstance>(info.vk_instance);
    const auto physical = static_cast<VkPhysicalDevice>(info.vk_physical_device);
    const auto getProperties = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(proc(instance, "vkGetPhysicalDeviceProperties"));
    VkPhysicalDeviceProperties properties{};
    if (!getProperties) return false;
    getProperties(physical, &properties);
    if ((!allowSoftware && properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU) ||
        !SDL_Vulkan_GetPresentationSupport(instance, physical, info.graphics_queue_family_index)) return false;
    auto& r = resources();
    r.context = std::move(context);
    r.thread = std::this_thread::get_id();
    r.vulkan = true;
    std::fprintf(stderr, "launchme gpu: Impeller Vulkan: %s (BC1/BC3=%s)\n", properties.deviceName, supportsCompression(1) ? "yes" : "no");
    return true;
}

bool tryOpenGl() {
    if (!compatible()) return false;
    Context context(ImpellerContextCreateOpenGLESNew(IMPELLER_VERSION, [](const char* name, void*) -> void* {
        if (!std::strncmp(name, "egl", 3)) return nullptr;
        return reinterpret_cast<void*>(SDL_GL_GetProcAddress(name));
    }, nullptr));
    if (!context) return false;
    auto& r = resources();
    r.context = std::move(context);
    r.thread = std::this_thread::get_id();
    r.vulkan = false;
    std::fprintf(stderr, "launchme gpu: Impeller OpenGL (BC1/BC3=%s)\n", supportsCompression(1) ? "yes" : "no");
    return true;
}

std::shared_ptr<SkCanvas> beginFrame(SDL_Window* window, int width, int height) {
    if (!active() || !window || width <= 0 || height <= 0 || width > 16384 || height > 16384) return nullptr;
    auto& r = resources();
    auto& w = windows[window];
    w.canvas.reset();
    ++r.frame;
    r.trim();
    if (r.vulkan && !w.swapchain) {
        ImpellerContextVulkanInfo info{};
        VkSurfaceKHR surface = VK_NULL_HANDLE;
        if (!ImpellerContextGetVulkanInfo(r.context.get(), &info) ||
            !SDL_Vulkan_CreateSurface(window, static_cast<VkInstance>(info.vk_instance), nullptr, &surface)) return nullptr;
        // Ownership of VkSurfaceKHR passes to Impeller.
        w.swapchain.reset(ImpellerVulkanSwapchainCreateNew(r.context.get(), reinterpret_cast<void*>(surface)));
        if (!w.swapchain) return nullptr;
    }
    w.width = width; w.height = height;
    w.canvas = std::make_shared<Canvas>(width, height);
    return w.canvas->healthy() ? w.canvas : nullptr;
}

bool present(SDL_Window* window) {
    const auto it = windows.find(window);
    if (!active() || it == windows.end() || !it->second.canvas) return false;
    auto& w = it->second;
    const auto list = w.canvas->finish();
    if (!list) { std::fprintf(stderr, "launchme gpu: Impeller frame recording failed\n"); return false; }
    int width = 0, height = 0;
    SDL_GetWindowSizeInPixels(window, &width, &height);
    if (width <= 0 || height <= 0 || (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED)) return true;
    Surface surface;
    if (resources().vulkan) surface.reset(ImpellerVulkanSwapchainAcquireNextSurfaceNew(w.swapchain.get()));
    else {
        const ImpellerISize size{width, height};
        surface.reset(ImpellerSurfaceCreateWrappedFBONew(resources().context.get(), 0, kImpellerPixelFormatRGBA8888, &size));
    }
    if (!surface || !ImpellerSurfaceDrawDisplayList(surface.get(), list)) return false;
    return resources().vulkan ? ImpellerSurfacePresent(surface.get()) : SDL_GL_SwapWindow(window);
}
void releaseWindow(SDL_Window* window) { windows.erase(window); }
void shutdown() {
    windows.clear();
    auto& r = resources();
    r.texts.clear();
    r.images.clear();
    r.context.reset();
}
} // namespace lm_impeller
