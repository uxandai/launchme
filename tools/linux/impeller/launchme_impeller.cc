// Built inside the pinned Flutter engine. Only plain C handles/bytes cross into Launchme;
// the engine's private Skia and C++ runtime remain inside libimpeller.so.
#include "impeller/toolkit/interop/launchme_impeller.h"

#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>

#include "impeller/core/device_buffer.h"
#include "impeller/display_list/dl_dispatcher.h"
#include "impeller/renderer/command_queue.h"
#include "impeller/renderer/backend/gles/context_gles.h"
#include "impeller/renderer/backend/gles/description_gles.h"
#include "impeller/renderer/render_target.h"
#include "impeller/toolkit/interop/context.h"
#include "impeller/toolkit/interop/dl.h"
#include "impeller/toolkit/interop/texture.h"

namespace {
using namespace impeller;
template <class T, class H> T* peer(H handle) { return reinterpret_cast<T*>(handle); }
PixelFormat pixelFormat(LaunchmeImpellerFormat format) {
    switch (format) {
    case kLaunchmeImpellerRGBA: return PixelFormat::kR8G8B8A8UNormInt;
    case kLaunchmeImpellerBGRA: return PixelFormat::kB8G8R8A8UNormInt;
    case kLaunchmeImpellerBC1: return PixelFormat::kBC1RGBAUNormInt;
    case kLaunchmeImpellerBC3: return PixelFormat::kBC3RGBAUNormInt;
    }
    return PixelFormat::kUnknown;
}
bool sizeValid(int32_t width, int32_t height) {
    return width > 0 && height > 0 && width <= 16384 && height <= 16384;
}
} // namespace

uint32_t LaunchmeImpellerGetAbiVersion() { return LAUNCHME_IMPELLER_ABI; }

bool LaunchmeImpellerSupportsFormat(ImpellerContext handle, LaunchmeImpellerFormat format) {
    if (!handle || pixelFormat(format) == PixelFormat::kUnknown) return false;
    const auto context = peer<interop::Context>(handle)->GetContext();
    if (!context || !context->IsValid()) return false;
    if (IsCompressed(pixelFormat(format)) && context->GetBackendType() == Context::BackendType::kOpenGLES) {
        // The core's family query also requires BC5/BC7 extensions. Launchme asks specifically
        // for BC1/BC3, which only need S3TC; desktop GL may expose BPTC as ARB rather than EXT.
        const auto& gl = ContextGLES::Cast(*context).GetReactor()->GetProcTable();
        const auto description = gl.GetDescription();
        return description && gl.CompressedTexImage2D.IsAvailable() &&
            (description->HasExtension("GL_EXT_texture_compression_s3tc") ||
             description->HasExtension(format == kLaunchmeImpellerBC1 ? "GL_EXT_texture_compression_dxt1" : "GL_ANGLE_texture_compression_dxt5"));
    }
    return !IsCompressed(pixelFormat(format)) ||
        context->GetCapabilities()->SupportsTextureCompression(CompressedTextureFamily::kBC);
}

ImpellerTexture LaunchmeImpellerTextureNew(ImpellerContext handle, LaunchmeImpellerFormat format,
    int32_t width, int32_t height, const uint8_t* data, uint64_t length) {
    if (!data || !sizeValid(width, height) || !LaunchmeImpellerSupportsFormat(handle, format)) return nullptr;
    const auto pixel_format = pixelFormat(format);
    if (IsCompressed(pixel_format) && (width % 4 || height % 4)) return nullptr;
    if (length != BytesForTextureRegion(pixel_format, width, height)) return nullptr;
    TextureDescriptor descriptor;
    descriptor.storage_mode = StorageMode::kHostVisible;
    descriptor.format = pixel_format;
    descriptor.size = {width, height};
    descriptor.mip_count = 1;
    descriptor.usage = TextureUsage::kShaderRead;
    auto texture = interop::Create<interop::Texture>(*peer<interop::Context>(handle), descriptor);
    if (!texture->IsValid() || !texture->SetContents(data, length)) return nullptr;
    return texture.Leak();
}

ImpellerTexture LaunchmeImpellerRenderToTextureNew(ImpellerContext handle, ImpellerDisplayList list,
    int32_t width, int32_t height) {
    if (!handle || !list || !sizeValid(width, height)) return nullptr;
    auto* interop_context = peer<interop::Context>(handle);
    const auto context = interop_context->GetContext();
    auto* display_list = peer<interop::DisplayList>(list);
    if (!context || !context->IsValid() || !display_list->IsValid()) return nullptr;
    RenderTargetAllocator allocator(context->GetResourceAllocator());
    auto target = allocator.CreateOffscreen(*context, {width, height}, 1, "Launchme capture",
        RenderTarget::kDefaultColorAttachmentConfig, RenderTarget::kDefaultStencilAttachmentConfig,
        nullptr, nullptr, PixelFormat::kR8G8B8A8UNormInt);
    if (!target.IsValid()) return nullptr;
    const bool rendered = RenderToTarget(interop_context->GetAiksContext().GetContentContext(), target,
        display_list->GetDisplayList(), Rect::MakeSize(ISize{width, height}), true);
    context->ResetThreadLocalState();
    if (!rendered) return nullptr;
    return interop::Create<interop::Texture>(context->GetBackendType(), target.GetRenderTargetTexture()).Leak();
}

bool LaunchmeImpellerTextureRead(ImpellerContext handle, ImpellerTexture texture_handle,
    uint8_t* destination, uint64_t length) {
    if (!handle || !texture_handle || !destination) return false;
    const auto context = peer<interop::Context>(handle)->GetContext();
    const auto texture = peer<interop::Texture>(texture_handle)->GetTexture();
    if (!context || !texture || !context->IsValid()) return false;
    const auto& descriptor = texture->GetTextureDescriptor();
    if (descriptor.format != PixelFormat::kR8G8B8A8UNormInt && descriptor.format != PixelFormat::kB8G8R8A8UNormInt) return false;
    const size_t bytes = descriptor.GetByteSizeOfBaseMipLevel();
    if (length != bytes) return false;
    auto buffer = context->GetResourceAllocator()->CreateBuffer({StorageMode::kHostVisible, bytes});
    auto commands = context->CreateCommandBuffer();
    if (!buffer || !commands) return false;
    auto blit = commands->CreateBlitPass();
    if (!blit || !blit->AddCopy(texture, buffer) || !blit->EncodeCommands()) return false;
    struct Completion {
        std::mutex mutex;
        std::condition_variable changed;
        CommandBuffer::Status status = CommandBuffer::Status::kPending;
    };
    auto completed = std::make_shared<Completion>();
    if (!context->GetCommandQueue()->Submit({commands}, [completed](CommandBuffer::Status status) {
        if (status == CommandBuffer::Status::kPending) return;
        std::lock_guard lock(completed->mutex);
        completed->status = status;
        completed->changed.notify_all();
    }).ok()) return false;
    // Vulkan's CommandBuffer::WaitUntilCompleted is a no-op in the pinned SDK.
    // Wait for the queue's fence completion, which also reports device errors.
    std::unique_lock lock(completed->mutex);
    if (!completed->changed.wait_for(lock, std::chrono::seconds(5), [&] {
        return completed->status != CommandBuffer::Status::kPending;
    }) || completed->status != CommandBuffer::Status::kCompleted) return false;
    lock.unlock();
    buffer->Invalidate();
    const auto* contents = buffer->OnGetContents();
    if (!contents) return false;
    std::memcpy(destination, contents, bytes);
    return true;
}
