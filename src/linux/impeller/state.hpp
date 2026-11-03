#pragma once
// Private to src/linux/impeller: the process-wide context and the helpers its files share.
#include "kernel.hpp"

#include <thread>

namespace lm_impeller {
template <class T, void (*Release)(T)> struct ReleaseHandle {
    void operator()(std::remove_pointer_t<T>* value) const { if (value) Release(value); }
};
template <class T, void (*Release)(T)>
using Handle = std::unique_ptr<std::remove_pointer_t<T>, ReleaseHandle<T, Release>>;
using ContextHandle = Handle<ImpellerContext, ImpellerContextRelease>;
using TextureHandle = Handle<ImpellerTexture, ImpellerTextureRelease>;
using SurfaceHandle = Handle<ImpellerSurface, ImpellerSurfaceRelease>;
using SwapchainHandle = Handle<ImpellerVulkanSwapchain, ImpellerVulkanSwapchainRelease>;
using DisplayListHandle = Handle<ImpellerDisplayList, ImpellerDisplayListRelease>;
using BuilderHandle = Handle<ImpellerDisplayListBuilder, ImpellerDisplayListBuilderRelease>;
using PaintHandle = Handle<ImpellerPaint, ImpellerPaintRelease>;
using ColorSourceHandle = Handle<ImpellerColorSource, ImpellerColorSourceRelease>;
using PathHandle = Handle<ImpellerPath, ImpellerPathRelease>;
using PathBuilderHandle = Handle<ImpellerPathBuilder, ImpellerPathBuilderRelease>;

struct State {
    ContextHandle context;
    std::thread::id thread;
    bool vulkan = false;
    bool headless = false; // A device without windows: frames are rendered and dropped.
    uint64_t frame = 0;
    uint64_t epoch = 1;    // Changes whenever the context is replaced (textures of an older one are void).
};
State& state();

PaintHandle makeNativePaint(const Paint& paint); // canvas.cpp
void trimImages(uint64_t frame);
void releaseAllImages();
} // namespace lm_impeller
