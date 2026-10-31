// The GPU behind every window: one Skia GPU context (Ganesh) on Vulkan, else OpenGL, else none
// (Skia draws in memory and frames go to SDL's window surface). Each window's frames are drawn
// into an offscreen surface of the window's size (lm_gfx::windowSurface), which a present copies
// into the swapchain (Vulkan), the default framebuffer (OpenGL) or the window's own buffer.
//   LAUNCHME_RENDERER=vulkan|gl|cpu  picks one (default: Vulkan on a real GPU, then OpenGL).
//   LAUNCHME_VSYNC=1                 waits for the display's refresh at present (default off: the
//                                    launcher paces its frames itself, FrameClock).
// Vulkan's own types first: SDL's Vulkan header then uses them instead of declaring its own.
#include "include/core/SkCanvas.h"
#include "include/core/SkColorSpace.h"
#include "include/core/SkData.h"
#include "include/core/SkImage.h"
#include "include/core/SkSurface.h"
#include "include/gpu/GpuTypes.h"
#include "include/gpu/MutableTextureState.h"
#include "include/gpu/ganesh/GrBackendSemaphore.h"
#include "include/gpu/ganesh/GrBackendSurface.h"
#include "include/gpu/ganesh/GrContextOptions.h"
#include "include/gpu/ganesh/GrDirectContext.h"
#include "include/gpu/ganesh/SkSurfaceGanesh.h"
#include "include/gpu/ganesh/gl/GrGLAssembleInterface.h"
#include "include/gpu/ganesh/gl/GrGLBackendSurface.h"
#include "include/gpu/ganesh/gl/GrGLDirectContext.h"
#include "include/gpu/ganesh/gl/GrGLInterface.h"
#include "include/gpu/ganesh/gl/GrGLTypes.h"
#include "include/gpu/ganesh/vk/GrVkBackendSemaphore.h"
#include "include/gpu/ganesh/vk/GrVkBackendSurface.h"
#include "include/gpu/ganesh/vk/GrVkDirectContext.h"
#include "include/gpu/ganesh/vk/GrVkTypes.h"
#include "include/gpu/vk/VulkanBackendContext.h"
#include "include/gpu/vk/VulkanExtensions.h"
#include "include/gpu/vk/VulkanMutableTextureState.h"
#include "include/gpu/vk/VulkanPreferredFeatures.h"
#include "src/gpu/GpuTypesPriv.h"
#include "src/gpu/vk/VulkanInterface.h"
#include "src/gpu/vk/vulkanmemoryallocator/VulkanAMDMemoryAllocator.h"

#include "window_internal.hpp"
#include "gpu.hpp"
#include "../gfx/gfx_internal.hpp"
#include "../impeller/backend.hpp"

#include <SDL3/SDL_vulkan.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <thread>
#include <vector>

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

// Compiled shaders and pipelines between runs ($XDG_CACHE_HOME/launchme/skia), so the first
// frames of a screen do not wait for the driver's compiler again.
class ShaderCache final : public GrContextOptions::PersistentCache {
public:
    ShaderCache() {
        const std::string base = !environment("XDG_CACHE_HOME").empty() ? environment("XDG_CACHE_HOME")
                               : !environment("HOME").empty()           ? environment("HOME") + "/.cache" : std::string{};
        if (base.empty()) return;
        folder_ = std::filesystem::path(base) / "launchme" / "skia";
        std::error_code error;
        std::filesystem::create_directories(folder_, error);
        if (error) folder_.clear();
    }
    sk_sp<SkData> load(const SkData& key) override {
        if (folder_.empty()) return nullptr;
        std::ifstream input(file(key), std::ios::binary | std::ios::ate);
        if (!input || input.tellg() <= 0) return nullptr;
        const auto size = static_cast<size_t>(input.tellg());
        sk_sp<SkData> data = SkData::MakeUninitialized(size);
        input.seekg(0);
        if (!input.read(static_cast<char*>(data->writable_data()), static_cast<std::streamsize>(size))) return nullptr;
        return data;
    }
    void store(const SkData& key, const SkData& data, const SkString&) override {
        if (folder_.empty()) return;
        const std::filesystem::path target = file(key);
        const std::string temporary = target.string() + ".tmp";
        {
            std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
            output.write(static_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
            if (!output) return;
        }
        std::error_code error;
        std::filesystem::rename(temporary, target, error);
    }
private:
    std::filesystem::path file(const SkData& key) const {
        // FNV-1a over the key, twice (different seeds): 128 bits of name.
        uint64_t a = 0xcbf29ce484222325ull, b = 0x84222325cbf29ce4ull;
        const auto* bytes = static_cast<const uint8_t*>(key.data());
        for (size_t i = 0; i < key.size(); ++i) { a = (a ^ bytes[i]) * 0x100000001b3ull; b = (b ^ bytes[i]) * 0x100000001b3ull + 1; }
        char name[40];
        std::snprintf(name, sizeof(name), "%016llx%016llx", static_cast<unsigned long long>(a), static_cast<unsigned long long>(b));
        return folder_ / name;
    }
    std::filesystem::path folder_;
};

GrContextOptions contextOptions() {
    static ShaderCache cache;
    GrContextOptions options;
    options.fPersistentCache = &cache;
    options.fShaderCacheStrategy = GrContextOptions::ShaderCacheStrategy::kBackendBinary;
    return options;
}

// ---- Vulkan -------------------------------------------------------------------------------

struct Vulkan {
    PFN_vkGetInstanceProcAddr instance_proc = nullptr;
    PFN_vkGetDeviceProcAddr device_proc = nullptr;
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    uint32_t family = 0;
    uint32_t api = 0;
    std::vector<std::string> instance_extensions, device_extensions;
    skgpu::VulkanExtensions extensions;
    skgpu::VulkanPreferredFeatures preferred;
    VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    std::string name;
#define LM_VK(name) PFN_vk##name name = nullptr
    LM_VK(DestroyInstance); LM_VK(EnumeratePhysicalDevices); LM_VK(GetPhysicalDeviceProperties);
    LM_VK(GetPhysicalDeviceQueueFamilyProperties); LM_VK(EnumerateDeviceExtensionProperties); LM_VK(GetPhysicalDeviceFeatures2);
    LM_VK(CreateDevice); LM_VK(DestroyDevice); LM_VK(GetDeviceQueue); LM_VK(DeviceWaitIdle);
    LM_VK(DestroySurfaceKHR); LM_VK(GetPhysicalDeviceSurfaceSupportKHR); LM_VK(GetPhysicalDeviceSurfaceCapabilitiesKHR);
    LM_VK(GetPhysicalDeviceSurfaceFormatsKHR); LM_VK(GetPhysicalDeviceSurfacePresentModesKHR);
    LM_VK(CreateSwapchainKHR); LM_VK(DestroySwapchainKHR); LM_VK(GetSwapchainImagesKHR); LM_VK(AcquireNextImageKHR);
    LM_VK(QueuePresentKHR); LM_VK(CreateSemaphore); LM_VK(DestroySemaphore);
#undef LM_VK
    PFN_vkVoidFunction get(const char* name, VkInstance in, VkDevice dev) const {
        if (dev != VK_NULL_HANDLE && device_proc) return device_proc(dev, name);
        return instance_proc(in, name);
    }
};

struct State {
    Backend backend = Backend::none;
    bool impeller = false;
    bool decided = false;
    std::thread::id thread;
    Vulkan vulkan;
    SDL_GLContext gl = nullptr;
    sk_sp<GrDirectContext> context;
    bool vsync = false;
};
State& gpuState() { static State instance; return instance; }

bool createVulkan(Vulkan& vk, bool allow_cpu) {
    if (!SDL_Vulkan_LoadLibrary(nullptr)) { note("no Vulkan loader", SDL_GetError()); return false; }
    vk.instance_proc = reinterpret_cast<PFN_vkGetInstanceProcAddr>(SDL_Vulkan_GetVkGetInstanceProcAddr());
    if (!vk.instance_proc) return false;
    const auto global = [&](const char* name) { return vk.instance_proc(VK_NULL_HANDLE, name); };
    auto enumerateVersion = reinterpret_cast<PFN_vkEnumerateInstanceVersion>(global("vkEnumerateInstanceVersion"));
    auto enumerateInstanceExtensions = reinterpret_cast<PFN_vkEnumerateInstanceExtensionProperties>(global("vkEnumerateInstanceExtensionProperties"));
    auto createInstance = reinterpret_cast<PFN_vkCreateInstance>(global("vkCreateInstance"));
    uint32_t version = VK_API_VERSION_1_0;
    if (enumerateVersion) enumerateVersion(&version);
    if (!createInstance || !enumerateInstanceExtensions || version < VK_API_VERSION_1_1) { note("Vulkan 1.1 is missing"); return false; }
    vk.api = std::min(version, VK_API_VERSION_1_3);
    vk.preferred.init(vk.api);

    Uint32 sdl_count = 0;
    char const* const* sdl_extensions = SDL_Vulkan_GetInstanceExtensions(&sdl_count);
    uint32_t available_count = 0;
    enumerateInstanceExtensions(nullptr, &available_count, nullptr);
    std::vector<VkExtensionProperties> available(available_count);
    enumerateInstanceExtensions(nullptr, &available_count, available.data());
    std::vector<const char*> wanted(sdl_extensions, sdl_extensions + sdl_count);
    vk.preferred.addToInstanceExtensions(available.data(), available.size(), wanted);
    for (const char* name : wanted) vk.instance_extensions.emplace_back(name);
    std::vector<const char*> instance_names;
    for (const auto& name : vk.instance_extensions) instance_names.push_back(name.c_str());

    VkApplicationInfo application{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    application.pApplicationName = "launchme";
    application.pEngineName = "Skia";
    application.apiVersion = vk.api;
    VkInstanceCreateInfo instance_info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    instance_info.pApplicationInfo = &application;
    instance_info.enabledExtensionCount = static_cast<uint32_t>(instance_names.size());
    instance_info.ppEnabledExtensionNames = instance_names.data();
    if (createInstance(&instance_info, nullptr, &vk.instance) != VK_SUCCESS) { note("vkCreateInstance failed"); return false; }
#define LM_LOAD(name) vk.name = reinterpret_cast<PFN_vk##name>(vk.instance_proc(vk.instance, "vk" #name))
    LM_LOAD(DestroyInstance); LM_LOAD(EnumeratePhysicalDevices); LM_LOAD(GetPhysicalDeviceProperties);
    LM_LOAD(GetPhysicalDeviceQueueFamilyProperties); LM_LOAD(EnumerateDeviceExtensionProperties); LM_LOAD(GetPhysicalDeviceFeatures2);
    LM_LOAD(CreateDevice); LM_LOAD(DestroySurfaceKHR); LM_LOAD(GetPhysicalDeviceSurfaceSupportKHR);
    LM_LOAD(GetPhysicalDeviceSurfaceCapabilitiesKHR); LM_LOAD(GetPhysicalDeviceSurfaceFormatsKHR); LM_LOAD(GetPhysicalDeviceSurfacePresentModesKHR);
    vk.device_proc = reinterpret_cast<PFN_vkGetDeviceProcAddr>(vk.instance_proc(vk.instance, "vkGetDeviceProcAddr"));
    if (!vk.EnumeratePhysicalDevices || !vk.CreateDevice || !vk.device_proc || !vk.GetPhysicalDeviceFeatures2) return false;

    // The best device that draws and shows: a real GPU first, a CPU rasterizer only on request.
    uint32_t count = 0;
    vk.EnumeratePhysicalDevices(vk.instance, &count, nullptr);
    std::vector<VkPhysicalDevice> devices(count);
    vk.EnumeratePhysicalDevices(vk.instance, &count, devices.data());
    int best = -1;
    for (VkPhysicalDevice candidate : devices) {
        VkPhysicalDeviceProperties properties{};
        vk.GetPhysicalDeviceProperties(candidate, &properties);
        if (properties.apiVersion < VK_API_VERSION_1_1) continue;
        const int score = properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 4
                        : properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 3
                        : properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU ? 2
                        : properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU ? (allow_cpu ? 1 : -1) : 0;
        if (score <= best) continue;
        uint32_t families = 0;
        vk.GetPhysicalDeviceQueueFamilyProperties(candidate, &families, nullptr);
        std::vector<VkQueueFamilyProperties> queues(families);
        vk.GetPhysicalDeviceQueueFamilyProperties(candidate, &families, queues.data());
        for (uint32_t family = 0; family < families; ++family)
            if ((queues[family].queueFlags & VK_QUEUE_GRAPHICS_BIT) && SDL_Vulkan_GetPresentationSupport(vk.instance, candidate, family)) {
                best = score;
                vk.physical = candidate;
                vk.family = family;
                vk.name = properties.deviceName;
                break;
            }
    }
    if (!vk.physical) { note("no Vulkan GPU that can show windows"); return false; }
    VkPhysicalDeviceProperties properties{};
    vk.GetPhysicalDeviceProperties(vk.physical, &properties);
    const uint32_t device_api = std::min(vk.api, properties.apiVersion);

    uint32_t extension_count = 0;
    vk.EnumerateDeviceExtensionProperties(vk.physical, nullptr, &extension_count, nullptr);
    std::vector<VkExtensionProperties> device_available(extension_count);
    vk.EnumerateDeviceExtensionProperties(vk.physical, nullptr, &extension_count, device_available.data());
    vk.preferred.addFeaturesToQuery(device_available.data(), device_available.size(), vk.features);
    vk.GetPhysicalDeviceFeatures2(vk.physical, &vk.features);
    vk.features.features.robustBufferAccess = VK_FALSE; // Costs speed, buys nothing here.
    std::vector<const char*> device_wanted{VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    vk.preferred.addFeaturesToEnable(device_wanted, vk.features);
    for (const char* name : device_wanted) vk.device_extensions.emplace_back(name);
    std::vector<const char*> device_names;
    for (const auto& name : vk.device_extensions) device_names.push_back(name.c_str());

    const float priority = 1;
    VkDeviceQueueCreateInfo queue_info{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queue_info.queueFamilyIndex = vk.family;
    queue_info.queueCount = 1;
    queue_info.pQueuePriorities = &priority;
    VkDeviceCreateInfo device_info{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    device_info.pNext = &vk.features;
    device_info.queueCreateInfoCount = 1;
    device_info.pQueueCreateInfos = &queue_info;
    device_info.enabledExtensionCount = static_cast<uint32_t>(device_names.size());
    device_info.ppEnabledExtensionNames = device_names.data();
    if (vk.CreateDevice(vk.physical, &device_info, nullptr, &vk.device) != VK_SUCCESS) { note("vkCreateDevice failed", vk.name); return false; }
#define LM_DEVICE(name) vk.name = reinterpret_cast<PFN_vk##name>(vk.device_proc(vk.device, "vk" #name))
    LM_DEVICE(DestroyDevice); LM_DEVICE(GetDeviceQueue); LM_DEVICE(DeviceWaitIdle); LM_DEVICE(CreateSwapchainKHR);
    LM_DEVICE(DestroySwapchainKHR); LM_DEVICE(GetSwapchainImagesKHR); LM_DEVICE(AcquireNextImageKHR); LM_DEVICE(QueuePresentKHR);
    LM_DEVICE(CreateSemaphore); LM_DEVICE(DestroySemaphore);
#undef LM_DEVICE
#undef LM_LOAD
    vk.GetDeviceQueue(vk.device, vk.family, 0, &vk.queue);

    const skgpu::VulkanGetProc get = [&vk](const char* name, VkInstance in, VkDevice dev) { return vk.get(name, in, dev); };
    vk.extensions.init(get, vk.instance, vk.physical, static_cast<uint32_t>(instance_names.size()), instance_names.data(),
                       static_cast<uint32_t>(device_names.size()), device_names.data());
    skgpu::VulkanBackendContext backend;
    backend.fInstance = vk.instance;
    backend.fPhysicalDevice = vk.physical;
    backend.fDevice = vk.device;
    backend.fQueue = vk.queue;
    backend.fGraphicsQueueIndex = vk.family;
    backend.fMaxAPIVersion = vk.api;
    backend.fVkExtensions = &vk.extensions;
    backend.fDeviceFeatures2 = &vk.features;
    backend.fGetProc = get;
    const sk_sp<skgpu::VulkanInterface> interface(new skgpu::VulkanInterface(get, vk.instance, vk.device, vk.api, device_api, &vk.extensions));
    if (!interface->validate(vk.api, device_api, &vk.extensions)) { note("Vulkan functions missing", vk.name); return false; }
    backend.fMemoryAllocator = skgpu::VulkanAMDMemoryAllocator::Make(vk.instance, vk.physical, vk.device, &vk.extensions, interface.get(),
                                                                     skgpu::ThreadSafe::kNo);
    auto& s = gpuState();
    s.context = GrDirectContexts::MakeVulkan(backend, contextOptions());
    if (!s.context) { note("Skia refused the Vulkan device", vk.name); return false; }
    note("Vulkan", vk.name);
    return true;
}

void destroyVulkan(Vulkan& vk) {
    if (vk.device && vk.DeviceWaitIdle) vk.DeviceWaitIdle(vk.device);
    gpuState().context.reset();
    if (vk.device && vk.DestroyDevice) vk.DestroyDevice(vk.device, nullptr);
    if (vk.instance && vk.DestroyInstance) vk.DestroyInstance(vk.instance, nullptr);
    vk.device = VK_NULL_HANDLE;
    vk.instance = VK_NULL_HANDLE;
}

// ---- OpenGL -------------------------------------------------------------------------------

bool createGl(SDL_Window* window) {
    auto& s = gpuState();
    s.gl = SDL_GL_CreateContext(window);
    if (!s.gl) { s.impeller = false; note("no OpenGL context", SDL_GetError()); return false; }
    const auto fail = [&](const char* why) {
        note(why, SDL_GetError());
        SDL_GL_DestroyContext(s.gl);
        s.gl = nullptr;
        s.context.reset();
        s.impeller = false;
        return false;
    };
    if (!SDL_GL_MakeCurrent(window, s.gl)) return fail("the OpenGL context cannot be made current");
    // A context that answers nothing (no driver behind it) would crash Skia's probing.
    const auto get_string = reinterpret_cast<const unsigned char* (*)(unsigned)>(SDL_GL_GetProcAddress("glGetString"));
    const auto* version = get_string ? reinterpret_cast<const char*>(get_string(0x1F02)) : nullptr;    // GL_VERSION
    const auto* renderer = get_string ? reinterpret_cast<const char*>(get_string(0x1F01)) : nullptr;   // GL_RENDERER
    if (!version || !renderer) return fail("the OpenGL context has no driver");
    if (!environment("LAUNCHME_GPU_DEBUG").empty()) note("OpenGL probe", std::string(renderer) + " (" + version + ")");
    SDL_GL_SetSwapInterval(s.vsync ? 1 : 0);
    if (s.impeller) {
        if (lm_impeller::tryOpenGl()) return true;
        s.impeller = false;
        note("Impeller initialization failed; falling back to Skia OpenGL");
    }
    // EGL entry points are not asked of the GL loader: GLX hands out a stub for any name, and
    // Skia would call "eglQueryString" through it.
    auto interface = GrGLMakeAssembledInterface(nullptr, [](void*, const char name[]) -> GrGLFuncPtr {
        if (!std::strncmp(name, "egl", 3)) return nullptr;
        return reinterpret_cast<GrGLFuncPtr>(SDL_GL_GetProcAddress(name));
    });
    if (interface) s.context = GrDirectContexts::MakeGL(interface, contextOptions());
    if (!s.context) return fail("Skia refused the OpenGL context");
    note("OpenGL", std::string(renderer) + " (" + version + ")");
    return true;
}

void decide() {
    auto& s = gpuState();
    if (s.decided) return;
    s.decided = true;
    s.thread = std::this_thread::get_id();
    s.vsync = environment("LAUNCHME_VSYNC") == "1";
    const std::string wanted = environment("LAUNCHME_RENDERER");
    if (wanted == "cpu") { s.backend = Backend::none; note("drawing on the CPU (LAUNCHME_RENDERER=cpu)"); return; }
    s.impeller = lm_impeller::requested();
    if (s.impeller) {
        if (wanted != "gl" && lm_impeller::tryVulkan(wanted == "vulkan")) { s.backend = Backend::vulkan; return; }
    } else {
        if (wanted != "gl" && createVulkan(s.vulkan, wanted == "vulkan")) { s.backend = Backend::vulkan; return; }
        destroyVulkan(s.vulkan);
    }
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

// ---- Windows ------------------------------------------------------------------------------

SkSurfaceProps surfaceProps() { return SkSurfaceProps(0, kUnknown_SkPixelGeometry); }
} // namespace

struct GpuWindow {
    sk_sp<SkSurface> frame; // What the launcher draws into.
    int frame_width = 0, frame_height = 0;
    bool cpu = false;       // This window shows frames through SDL's window surface.
    // Vulkan
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_UNDEFINED;
    SkColorType color_type = kBGRA_8888_SkColorType;
    VkImageUsageFlags usage = 0;
    int width = 0, height = 0;
    std::vector<sk_sp<SkSurface>> images;
    std::vector<VkSemaphore> rendered;
    bool stale = false;
    // OpenGL
    sk_sp<SkSurface> framebuffer;
};

namespace {
void releaseSwapchain(GpuWindow& w, bool keep_swapchain) {
    auto& s = gpuState();
    auto& vk = s.vulkan;
    if (s.context) s.context->flushAndSubmit(GrSyncCpu::kYes);
    if (vk.device) vk.DeviceWaitIdle(vk.device);
    w.images.clear();
    for (VkSemaphore semaphore : w.rendered) vk.DestroySemaphore(vk.device, semaphore, nullptr);
    w.rendered.clear();
    if (!keep_swapchain && w.swapchain) { vk.DestroySwapchainKHR(vk.device, w.swapchain, nullptr); w.swapchain = VK_NULL_HANDLE; }
}

// The swapchain for the window's drawable size: mailbox, then immediate on Wayland (the
// compositor never tears), then FIFO (which waits for the display).
bool buildSwapchain(Window* target, GpuWindow& w) {
    auto& s = gpuState();
    auto& vk = s.vulkan;
    if (!w.surface) {
        if (!SDL_Vulkan_CreateSurface(target->sdl, vk.instance, nullptr, &w.surface)) { note("no Vulkan surface", SDL_GetError()); return false; }
        VkBool32 supported = VK_FALSE;
        vk.GetPhysicalDeviceSurfaceSupportKHR(vk.physical, vk.family, w.surface, &supported);
        if (!supported) { note("the GPU cannot show this window"); return false; }
    }
    VkSurfaceCapabilitiesKHR caps{};
    if (vk.GetPhysicalDeviceSurfaceCapabilitiesKHR(vk.physical, w.surface, &caps) != VK_SUCCESS) return false;
    int width = 0, height = 0;
    SDL_GetWindowSizeInPixels(target->sdl, &width, &height);
    VkExtent2D extent = caps.currentExtent;
    if (extent.width == 0xFFFFFFFF) extent = {static_cast<uint32_t>(std::max(1, width)), static_cast<uint32_t>(std::max(1, height))};
    extent.width = std::clamp(extent.width, caps.minImageExtent.width, std::max(caps.minImageExtent.width, caps.maxImageExtent.width));
    extent.height = std::clamp(extent.height, caps.minImageExtent.height, std::max(caps.minImageExtent.height, caps.maxImageExtent.height));
    if (!extent.width || !extent.height) return false; // Minimized.

    uint32_t count = 0;
    vk.GetPhysicalDeviceSurfaceFormatsKHR(vk.physical, w.surface, &count, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(count);
    vk.GetPhysicalDeviceSurfaceFormatsKHR(vk.physical, w.surface, &count, formats.data());
    w.format = VK_FORMAT_UNDEFINED;
    VkColorSpaceKHR color_space = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    for (const VkFormat wanted : {VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM})
        for (const auto& format : formats)
            if (w.format == VK_FORMAT_UNDEFINED && format.format == wanted && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
                w.format = wanted;
                color_space = format.colorSpace;
            }
    if (w.format == VK_FORMAT_UNDEFINED) { note("no 8-bit swapchain format"); return false; }
    w.color_type = w.format == VK_FORMAT_B8G8R8A8_UNORM ? kBGRA_8888_SkColorType : kRGBA_8888_SkColorType;

    vk.GetPhysicalDeviceSurfacePresentModesKHR(vk.physical, w.surface, &count, nullptr);
    std::vector<VkPresentModeKHR> modes(count);
    vk.GetPhysicalDeviceSurfacePresentModesKHR(vk.physical, w.surface, &count, modes.data());
    const bool wayland = SDL_strcmp(SDL_GetCurrentVideoDriver() ? SDL_GetCurrentVideoDriver() : "", "wayland") == 0;
    const auto has = [&](VkPresentModeKHR mode) { return std::find(modes.begin(), modes.end(), mode) != modes.end(); };
    VkPresentModeKHR mode = VK_PRESENT_MODE_FIFO_KHR;
    if (!s.vsync) {
        if (has(VK_PRESENT_MODE_MAILBOX_KHR)) mode = VK_PRESENT_MODE_MAILBOX_KHR;
        else if (wayland && has(VK_PRESENT_MODE_IMMEDIATE_KHR)) mode = VK_PRESENT_MODE_IMMEDIATE_KHR;
        else if (has(VK_PRESENT_MODE_FIFO_RELAXED_KHR)) mode = VK_PRESENT_MODE_FIFO_RELAXED_KHR;
    }

    w.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | (caps.supportedUsageFlags & (VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT));
    VkSwapchainCreateInfoKHR info{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    info.surface = w.surface;
    info.minImageCount = std::max(caps.minImageCount, 2u);
    if (caps.maxImageCount) info.minImageCount = std::min(std::max(caps.minImageCount + 1, info.minImageCount), caps.maxImageCount);
    info.imageFormat = w.format;
    info.imageColorSpace = color_space;
    info.imageExtent = extent;
    info.imageArrayLayers = 1;
    info.imageUsage = w.usage;
    info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.preTransform = (caps.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR) ? VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR : caps.currentTransform;
    info.compositeAlpha = (caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR) ? VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR
                        : (caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR) ? VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR
                        : static_cast<VkCompositeAlphaFlagBitsKHR>(caps.supportedCompositeAlpha & -caps.supportedCompositeAlpha);
    info.presentMode = mode;
    info.clipped = VK_TRUE;
    const VkSwapchainKHR old = w.swapchain;
    info.oldSwapchain = old;
    releaseSwapchain(w, true);
    VkSwapchainKHR created = VK_NULL_HANDLE;
    const VkResult result = vk.CreateSwapchainKHR(vk.device, &info, nullptr, &created);
    if (old) vk.DestroySwapchainKHR(vk.device, old, nullptr);
    w.swapchain = VK_NULL_HANDLE;
    if (result != VK_SUCCESS) { note("vkCreateSwapchainKHR failed"); return false; }
    w.swapchain = created;
    w.width = static_cast<int>(extent.width);
    w.height = static_cast<int>(extent.height);

    vk.GetSwapchainImagesKHR(vk.device, w.swapchain, &count, nullptr);
    std::vector<VkImage> images(count);
    vk.GetSwapchainImagesKHR(vk.device, w.swapchain, &count, images.data());
    const SkSurfaceProps props = surfaceProps();
    for (VkImage image : images) {
        GrVkImageInfo image_info;
        image_info.fImage = image;
        image_info.fImageTiling = VK_IMAGE_TILING_OPTIMAL;
        image_info.fImageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        image_info.fFormat = w.format;
        image_info.fImageUsageFlags = w.usage;
        image_info.fSampleCount = 1;
        image_info.fLevelCount = 1;
        image_info.fCurrentQueueFamily = vk.family;
        image_info.fSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        const GrBackendRenderTarget backend = GrBackendRenderTargets::MakeVk(w.width, w.height, image_info);
        w.images.push_back(SkSurfaces::WrapBackendRenderTarget(s.context.get(), backend, kTopLeft_GrSurfaceOrigin, w.color_type, nullptr, &props));
        VkSemaphoreCreateInfo semaphore_info{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        VkSemaphore semaphore = VK_NULL_HANDLE;
        vk.CreateSemaphore(vk.device, &semaphore_info, nullptr, &semaphore);
        w.rendered.push_back(semaphore);
        if (!w.images.back()) { note("Skia cannot draw into the swapchain"); return false; }
    }
    w.stale = false;
    static bool reported = false;
    if (!reported) {
        reported = true;
        const char* names[] = {"immediate", "mailbox", "fifo", "fifo-relaxed"};
        note("swapchain", std::to_string(w.width) + "x" + std::to_string(w.height) + " " + (mode <= 3 ? names[mode] : "other") + ", " +
                              std::to_string(count) + " images");
    }
    return true;
}

// The finished frame into the window's next swapchain image, then shown.
bool presentVulkan(Window* target, GpuWindow& w) {
    auto& s = gpuState();
    auto& vk = s.vulkan;
    int width = 0, height = 0;
    SDL_GetWindowSizeInPixels(target->sdl, &width, &height);
    if (!w.swapchain || w.stale || width != w.width || height != w.height)
        if (!buildSwapchain(target, w)) return !w.surface ? false : true; // A minimized window shows nothing for now.
    for (int attempt = 0; attempt < 2; ++attempt) {
        VkSemaphoreCreateInfo semaphore_info{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        VkSemaphore acquired = VK_NULL_HANDLE;
        if (vk.CreateSemaphore(vk.device, &semaphore_info, nullptr, &acquired) != VK_SUCCESS) return false;
        uint32_t index = 0;
        const VkResult result = vk.AcquireNextImageKHR(vk.device, w.swapchain, UINT64_MAX, acquired, VK_NULL_HANDLE, &index);
        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_ERROR_SURFACE_LOST_KHR) {
            vk.DestroySemaphore(vk.device, acquired, nullptr);
            if (!buildSwapchain(target, w)) return true;
            continue;
        }
        if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) { vk.DestroySemaphore(vk.device, acquired, nullptr); return false; }
        if (result == VK_SUBOPTIMAL_KHR) w.stale = true;
        SkSurface* image = w.images[index].get();
        const GrBackendSemaphore wait = GrBackendSemaphores::MakeVk(acquired);
        image->wait(1, &wait, true); // Skia deletes it after the wait.
        SkCanvas* canvas = image->getCanvas();
        if (w.frame_width < w.width || w.frame_height < w.height) canvas->clear(SK_ColorBLACK);
        SkPaint copy;
        copy.setBlendMode(SkBlendMode::kSrc);
        canvas->drawImage(w.frame->makeImageSnapshot(), 0, 0, SkSamplingOptions(), &copy);
        const GrBackendSemaphore signal = GrBackendSemaphores::MakeVk(w.rendered[index]);
        GrFlushInfo flush;
        flush.fNumSemaphores = 1;
        flush.fSignalSemaphores = const_cast<GrBackendSemaphore*>(&signal);
        const skgpu::MutableTextureState present_state = skgpu::MutableTextureStates::MakeVulkan(VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, vk.family);
        s.context->flush(image, flush, &present_state);
        s.context->submit();
        VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
        present.waitSemaphoreCount = 1;
        present.pWaitSemaphores = &w.rendered[index];
        present.swapchainCount = 1;
        present.pSwapchains = &w.swapchain;
        present.pImageIndices = &index;
        const VkResult shown = vk.QueuePresentKHR(vk.queue, &present);
        if (shown == VK_ERROR_OUT_OF_DATE_KHR || shown == VK_SUBOPTIMAL_KHR) w.stale = true;
        else if (shown != VK_SUCCESS) return false;
        return true;
    }
    return true;
}

bool presentGl(Window* target, GpuWindow& w) {
    auto& s = gpuState();
    if (!SDL_GL_MakeCurrent(target->sdl, s.gl)) return false;
    int width = 0, height = 0;
    SDL_GetWindowSizeInPixels(target->sdl, &width, &height);
    if (width <= 0 || height <= 0) return true;
    if (!w.framebuffer || w.framebuffer->width() != width || w.framebuffer->height() != height) {
        int stencil = 0;
        SDL_GL_GetAttribute(SDL_GL_STENCIL_SIZE, &stencil);
        const GrGLFramebufferInfo info{0, 0x8058}; // GL_RGBA8
        const GrBackendRenderTarget backend = GrBackendRenderTargets::MakeGL(width, height, 0, stencil, info);
        const SkSurfaceProps props = surfaceProps();
        w.framebuffer = SkSurfaces::WrapBackendRenderTarget(s.context.get(), backend, kBottomLeft_GrSurfaceOrigin, kRGBA_8888_SkColorType,
                                                            nullptr, &props);
        if (!w.framebuffer) { note("Skia cannot draw into the window's framebuffer"); return false; }
    }
    SkCanvas* canvas = w.framebuffer->getCanvas();
    if (w.frame_width < width || w.frame_height < height) canvas->clear(SK_ColorBLACK);
    SkPaint copy;
    copy.setBlendMode(SkBlendMode::kSrc);
    canvas->drawImage(w.frame->makeImageSnapshot(), 0, 0, SkSamplingOptions(), &copy);
    s.context->flushAndSubmit(w.framebuffer.get());
    return SDL_GL_SwapWindow(target->sdl);
}

// No GPU: the pixels straight into the window's own buffer (wl_shm on Wayland, XShm on X11).
bool presentCpu(Window* target, GpuWindow& w) {
    SDL_Surface* surface = SDL_GetWindowSurface(target->sdl);
    if (!surface || !surface->pixels) return false;
    const int width = std::min(surface->w, w.frame_width), height = std::min(surface->h, w.frame_height);
    if (width <= 0 || height <= 0) return true;
    if (surface->format == SDL_PIXELFORMAT_ARGB8888 || surface->format == SDL_PIXELFORMAT_XRGB8888) {
        w.frame->readPixels(SkImageInfo::Make(width, height, kBGRA_8888_SkColorType, kPremul_SkAlphaType), surface->pixels,
                            static_cast<size_t>(surface->pitch), 0, 0);
    } else if (surface->format == SDL_PIXELFORMAT_ABGR8888 || surface->format == SDL_PIXELFORMAT_XBGR8888) {
        w.frame->readPixels(SkImageInfo::Make(width, height, kRGBA_8888_SkColorType, kPremul_SkAlphaType), surface->pixels,
                            static_cast<size_t>(surface->pitch), 0, 0);
    } else {
        std::vector<uint8_t> pixels(static_cast<size_t>(width) * height * 4);
        w.frame->readPixels(SkImageInfo::Make(width, height, kBGRA_8888_SkColorType, kPremul_SkAlphaType), pixels.data(),
                            static_cast<size_t>(width) * 4, 0, 0);
        SDL_ConvertPixels(width, height, SDL_PIXELFORMAT_ARGB8888, pixels.data(), width * 4, surface->format, surface->pixels, surface->pitch);
    }
    return SDL_UpdateWindowSurface(target->sdl);
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
        note("drawing on the CPU");
    }
}

void release(Window* target) {
    if (target) lm_impeller::releaseWindow(target->sdl);
    if (!target || !target->gpu) return;
    auto& s = gpuState();
    GpuWindow& w = *target->gpu;
    if (s.backend == Backend::vulkan) {
        releaseSwapchain(w, false);
        if (w.surface) s.vulkan.DestroySurfaceKHR(s.vulkan.instance, w.surface, nullptr);
    }
    w.frame.reset();
    w.framebuffer.reset();
    delete target->gpu;
    target->gpu = nullptr;
}

void shutdown() {
    auto& s = gpuState();
    lm_impeller::shutdown();
    if (s.context) s.context->flushAndSubmit(GrSyncCpu::kYes);
    if (s.backend == Backend::vulkan) destroyVulkan(s.vulkan);
    s.context.reset();
    if (s.gl) { SDL_GL_DestroyContext(s.gl); s.gl = nullptr; }
}
} // namespace gpu
} // namespace lm_window::detail

using namespace lm_window::detail;

namespace lm_gfx {
GrDirectContext* gpu() {
    auto& s = gpuState();
    return s.context && std::this_thread::get_id() == s.thread ? s.context.get() : nullptr;
}

sk_sp<SkSurface> windowSurface(HWND handle, int width, int height) {
    if (gpuState().impeller) return nullptr;
    Window* target = window(handle);
    if (!target || width <= 0 || height <= 0) return nullptr;
    if (!target->gpu) target->gpu = new GpuWindow;
    GpuWindow& w = *target->gpu;
    if (w.frame && w.frame_width == width && w.frame_height == height) return w.frame;
    w.frame.reset();
    const SkSurfaceProps props = surfaceProps();
    if (GrDirectContext* context = gpu())
        w.frame = SkSurfaces::RenderTarget(context, skgpu::Budgeted::kNo, SkImageInfo::Make(width, height, kRGBA_8888_SkColorType, kPremul_SkAlphaType),
                                           0, kTopLeft_GrSurfaceOrigin, &props);
    if (!w.frame) {
        w.frame = SkSurfaces::Raster(SkImageInfo::Make(width, height, kBGRA_8888_SkColorType, kPremul_SkAlphaType), &props);
        w.cpu = true;
    }
    w.frame_width = width;
    w.frame_height = height;
    return w.frame;
}

bool presentWindow(HWND handle) {
    Window* target = window(handle);
    if (target && gpuState().impeller) {
        if (gpuState().gl && !SDL_GL_MakeCurrent(target->sdl, gpuState().gl)) return false;
        return lm_impeller::present(target->sdl);
    }
    if (!target || !target->gpu || !target->gpu->frame) return false;
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
    GpuWindow& w = *target->gpu;
    auto& s = gpuState();
    // LAUNCHME_PRESENT=copy: the GPU's frame read back into the window's own buffer instead of
    // the driver's swap (a comparison for drivers whose swap is a slow copy anyway).
    static const bool copy = environment("LAUNCHME_PRESENT") == "copy";
    if (copy && !w.cpu) return presentCpu(target, w);
    if (!w.cpu && s.backend == Backend::vulkan) {
        if (presentVulkan(target, w)) return true;
        note("Vulkan present failed: this window draws on the CPU from now");
    } else if (!w.cpu && s.backend == Backend::gl) {
        if (presentGl(target, w)) return true;
        note("OpenGL present failed");
        return false;
    }
    if (!w.cpu) {
        // The GPU frame read back once, then the window keeps to the CPU path.
        sk_sp<SkSurface> raster = SkSurfaces::Raster(SkImageInfo::Make(w.frame_width, w.frame_height, kBGRA_8888_SkColorType, kPremul_SkAlphaType));
        if (raster) raster->getCanvas()->drawImage(w.frame->makeImageSnapshot(), 0, 0);
        w.frame = raster;
        w.cpu = true;
        if (!w.frame) return false;
    }
    return presentCpu(target, w);
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

std::shared_ptr<SkCanvas> windowCanvas(HWND handle, int width, int height) {
    Window* target = window(handle);
    if (!target || !gpuState().impeller) return nullptr;
    if (gpuState().gl && !SDL_GL_MakeCurrent(target->sdl, gpuState().gl)) return nullptr;
    return lm_impeller::beginFrame(target->sdl, width, height);
}
} // namespace lm_gfx
