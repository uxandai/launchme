// Direct2D bitmaps on Skia images: premultiplied BGRA (Direct2D's B8G8R8A8 in memory), or alpha
// only. Straight, ignored-alpha and R8G8B8A8 pixels are converted on upload; BC1 blocks go
// straight into a compressed GPU texture where the GPU samples them (createCompressed), other
// block-compressed formats are refused. Pixels from memory stay in memory until first drawn on the
// GPU, then live there as a mipmapped texture; a bitmap drawn into (a render target's, a copy's
// destination) is a surface, read through snapshots.
#include "d2d_internal.hpp"

#include "include/core/SkBitmap.h"
#include "include/core/SkData.h"
#include "include/core/SkPixmap.h"
#include "include/core/SkTextureCompressionType.h"
#include "include/gpu/GpuTypes.h"
#include "include/gpu/ganesh/GrBackendSurface.h"
#include "include/gpu/ganesh/GrDirectContext.h"
#include "include/gpu/ganesh/GrRecordingContext.h"
#include "include/gpu/ganesh/SkImageGanesh.h"
#include "include/gpu/ganesh/SkSurfaceGanesh.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>

namespace lm_d2d {
namespace {
bool supported(DXGI_FORMAT format) {
    return format == DXGI_FORMAT_B8G8R8A8_UNORM || format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB || format == DXGI_FORMAT_B8G8R8X8_UNORM ||
           format == DXGI_FORMAT_R8G8B8A8_UNORM || format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB || format == DXGI_FORMAT_A8_UNORM;
}

bool opaque(D2D1_PIXEL_FORMAT format) { return format.alphaMode == D2D1_ALPHA_MODE_IGNORE || format.format == DXGI_FORMAT_B8G8R8X8_UNORM; }

// One row of `format` pixels as premultiplied B8G8R8A8.
void convertRow(const uint8_t* source, uint8_t* target, int width, D2D1_PIXEL_FORMAT format) {
    const bool swap = format.format == DXGI_FORMAT_R8G8B8A8_UNORM || format.format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    const bool solid = opaque(format);
    const bool straight = format.alphaMode == D2D1_ALPHA_MODE_STRAIGHT;
    if (!swap && !solid && !straight) { std::memcpy(target, source, static_cast<size_t>(width) * 4); return; }
    for (int x = 0; x < width; ++x, source += 4, target += 4) {
        uint8_t b = source[0], g = source[1], r = source[2];
        if (swap) std::swap(b, r);
        const uint8_t a = solid ? 255 : source[3];
        if (straight && a != 255) {
            b = static_cast<uint8_t>((b * a + 127) / 255);
            g = static_cast<uint8_t>((g * a + 127) / 255);
            r = static_cast<uint8_t>((r * a + 127) / 255);
        }
        target[0] = b; target[1] = g; target[2] = r; target[3] = a;
    }
}

// Pictures large enough to be drawn reduced get mipmaps on the GPU.
bool wantsMipmaps(int width, int height) { return width >= 64 && height >= 64; }
} // namespace

uint64_t newVersion() {
    static std::atomic<uint64_t> counter{0};
    return ++counter;
}

sk_sp<SkSurface> makeSurface(GrDirectContext* gpu, const SkImageInfo& info, bool mipmaps) {
    if (info.width() <= 0 || info.height() <= 0) return nullptr;
    if (gpu) {
        if (auto surface = SkSurfaces::RenderTarget(gpu, skgpu::Budgeted::kYes, info, 0, kTopLeft_GrSurfaceOrigin, nullptr, mipmaps))
            return surface;
    }
    return SkSurfaces::Raster(info);
}

sk_sp<SkImage> imageFor(GrDirectContext* gpu, sk_sp<SkImage> image) {
    if (!image || gpu || !image->isTextureBacked()) return image; // A GPU canvas uploads memory images itself.
    // A texture on a canvas in memory: read back, on the thread that owns the GPU.
    GrDirectContext* owner = lm_gfx::gpu();
    return owner ? image->makeRasterImage(owner) : nullptr;
}

D2D1_PIXEL_FORMAT resolveFormat(D2D1_PIXEL_FORMAT format) {
    if (format.format == DXGI_FORMAT_UNKNOWN) format.format = DXGI_FORMAT_B8G8R8A8_UNORM;
    if (format.alphaMode == D2D1_ALPHA_MODE_UNKNOWN)
        format.alphaMode = format.format == DXGI_FORMAT_B8G8R8X8_UNORM ? D2D1_ALPHA_MODE_IGNORE : D2D1_ALPHA_MODE_PREMULTIPLIED;
    return format;
}

// Linear is bilinear, as Direct2D samples; the other smooth modes reduce through mipmaps
// (trilinear) where the image has them, Direct2D's high-quality reduction.
SkSamplingOptions samplingFor(D2D1_INTERPOLATION_MODE mode, bool reducing, bool mipmapped) {
    switch (mode) {
    case D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR: return SkSamplingOptions(SkFilterMode::kNearest);
    case D2D1_INTERPOLATION_MODE_LINEAR: return SkSamplingOptions(SkFilterMode::kLinear);
    default:
        if (reducing && mipmapped) return SkSamplingOptions(SkFilterMode::kLinear, SkMipmapMode::kLinear);
        return SkSamplingOptions(SkFilterMode::kLinear);
    }
}

Bitmap::Bitmap(int width, int height, D2D1_PIXEL_FORMAT format, float dpiX, float dpiY)
    : width_(width), height_(height), format_(format), dpi_x_(dpiX > 0 ? dpiX : 96), dpi_y_(dpiY > 0 ? dpiY : 96), version_(newVersion()) {}

SkImageInfo Bitmap::info() const {
    if (alphaOnly()) return SkImageInfo::MakeA8(width_, height_);
    return bgraInfo(width_, height_, opaque(format_) ? kOpaque_SkAlphaType : kPremul_SkAlphaType);
}

HRESULT Bitmap::create(D2D1_SIZE_U size, const void* data, UINT32 pitch, D2D1_PIXEL_FORMAT format, float dpiX, float dpiY,
                       ComPtr<Bitmap>& out) {
    format = resolveFormat(format);
    if (!supported(format.format)) return D2DERR_UNSUPPORTED_PIXEL_FORMAT;
    if (size.width > 16384 || size.height > 16384) return E_INVALIDARG; // The largest texture side GPUs promise.
    out.Attach(new Bitmap(static_cast<int>(size.width), static_cast<int>(size.height), format, dpiX, dpiY));
    // Without data the pixels are transparent: nothing is held until something is drawn or copied in.
    return data ? out->CopyFromMemory(nullptr, data, pitch) : S_OK;
}

bool blockCompressionSupported(GrDirectContext* gpu, DXGI_FORMAT format) {
    return gpu && format == DXGI_FORMAT_BC1_UNORM && gpu->compressedBackendFormat(SkTextureCompressionType::kBC1_RGBA8_UNORM).isValid();
}

HRESULT Bitmap::createCompressed(D2D1_SIZE_U size, const void* data, UINT32 pitch, D2D1_PIXEL_FORMAT format, float dpiX, float dpiY,
                                 GrDirectContext* gpu, ComPtr<Bitmap>& out, bool impeller) {
    if (impeller) {
        const unsigned compression = format.format == DXGI_FORMAT_BC1_UNORM ? 1 : format.format == DXGI_FORMAT_BC3_UNORM ? 3 : 0;
        if (!lm_impeller::supportsCompression(compression)) return D2DERR_UNSUPPORTED_PIXEL_FORMAT;
        if (!data || !size.width || !size.height || size.width % 4 || size.height % 4 || size.width > 16384 || size.height > 16384 ||
            pitch != size.width / 4 * (compression == 1 ? 8u : 16u)) return E_INVALIDARG;
        auto image = lm_impeller::compressedImage(compression, static_cast<int>(size.width), static_cast<int>(size.height),
            static_cast<const uint8_t*>(data), static_cast<size_t>(pitch) * (size.height / 4));
        if (!image) return E_FAIL;
        out.Attach(new Bitmap(static_cast<int>(size.width), static_cast<int>(size.height), {format.format, D2D1_ALPHA_MODE_PREMULTIPLIED}, dpiX, dpiY));
        out->image_ = std::move(image);
        return S_OK;
    }
    if (!blockCompressionSupported(gpu, format.format)) return D2DERR_UNSUPPORTED_PIXEL_FORMAT;
    if (!data || !size.width || !size.height || size.width % 4 || size.height % 4 || size.width > 16384 || size.height > 16384 ||
        pitch != size.width / 4 * 8) return E_INVALIDARG;
    const size_t bytes = static_cast<size_t>(size.width / 4) * (size.height / 4) * 8;
    sk_sp<SkImage> texture = SkImages::TextureFromCompressedTextureData(gpu, SkData::MakeWithCopy(data, bytes), static_cast<int>(size.width),
                                                                        static_cast<int>(size.height), SkTextureCompressionType::kBC1_RGBA8_UNORM,
                                                                        skgpu::Mipmapped::kNo);
    if (!texture) return D2DERR_UNSUPPORTED_PIXEL_FORMAT;
    out.Attach(new Bitmap(static_cast<int>(size.width), static_cast<int>(size.height), {format.format, D2D1_ALPHA_MODE_PREMULTIPLIED}, dpiX, dpiY));
    out->image_ = std::move(texture);
    return S_OK;
}

Bitmap* Bitmap::from(ID2D1Image* image) { return dynamic_cast<Bitmap*>(image); }
ImageSource* ImageSource::from(ID2D1Image* image) { return dynamic_cast<ImageSource*>(image); }

D2D1_SIZE_F STDMETHODCALLTYPE Bitmap::GetSize() const {
    return {static_cast<float>(width_ / scaleX()), static_cast<float>(height_ / scaleY())};
}
D2D1_SIZE_U STDMETHODCALLTYPE Bitmap::GetPixelSize() const { return {static_cast<UINT32>(width_), static_cast<UINT32>(height_)}; }

sk_sp<SkImage> Bitmap::image(GrDirectContext* gpu) {
    if (surface_) {
        if (!snapshot_) snapshot_ = surface_->makeImageSnapshot();
        return imageFor(gpu, snapshot_);
    }
    if (image_ && gpu && !image_->isTextureBacked() && gpu == lm_gfx::gpu()) {
        // Uploaded once; the memory copy goes.
        if (auto texture = SkImages::TextureFromImage(gpu, image_.get(), wantsMipmaps(width_, height_) ? skgpu::Mipmapped::kYes : skgpu::Mipmapped::kNo))
            image_ = std::move(texture);
    }
    return imageFor(gpu, image_);
}

sk_sp<SkSurface> Bitmap::surface(GrDirectContext* gpu) {
    if (surface_) return surface_;
    SkImageInfo surface_info = info();
    if (surface_info.alphaType() == kOpaque_SkAlphaType) surface_info = surface_info.makeAlphaType(kPremul_SkAlphaType);
    if (image_ && image_->isTextureBacked()) gpu = lm_gfx::gpu(); // The pixels are there already.
    surface_ = makeSurface(gpu, surface_info);
    if (!surface_) return nullptr;
    if (image_) {
        SkPaint copy;
        copy.setBlendMode(SkBlendMode::kSrc);
        surface_->getCanvas()->drawImage(imageFor(surface_->recordingContext() ? gpu : nullptr, image_), 0, 0, SkSamplingOptions(), &copy);
    } else {
        surface_->getCanvas()->clear(SK_ColorTRANSPARENT);
    }
    image_.reset();
    snapshot_.reset();
    return surface_;
}

HRESULT STDMETHODCALLTYPE Bitmap::CopyFromMemory(const D2D1_RECT_U* rect, const void* data, UINT32 pitch) {
    if (!data) return E_INVALIDARG;
    const D2D1_RECT_U area = rect ? *rect : D2D1_RECT_U{0, 0, static_cast<UINT32>(width_), static_cast<UINT32>(height_)};
    if (area.right > static_cast<UINT32>(width_) || area.bottom > static_cast<UINT32>(height_) || area.left > area.right || area.top > area.bottom)
        return E_INVALIDARG;
    const int columns = static_cast<int>(area.right - area.left), rows = static_cast<int>(area.bottom - area.top);
    if (!columns || !rows) return S_OK;
    // The rows in this bitmap's own layout.
    const SkImageInfo part = info().makeWH(columns, rows);
    SkBitmap pixels;
    if (!pixels.tryAllocPixels(part)) return E_OUTOFMEMORY;
    const auto* source = static_cast<const uint8_t*>(data);
    for (int y = 0; y < rows; ++y, source += pitch) {
        auto* row = static_cast<uint8_t*>(pixels.getAddr(0, y));
        if (alphaOnly()) std::memcpy(row, source, static_cast<size_t>(columns));
        else convertRow(source, row, columns, format_);
    }
    const bool whole = columns == width_ && rows == height_;
    const bool on_gpu = (image_ && image_->isTextureBacked()) || (surface_ && surface_->recordingContext());
    if (whole && !on_gpu && !surface_) {
        pixels.setImmutable();
        image_ = SkImages::RasterFromBitmap(pixels);
    } else if (on_gpu && lm_gfx::gpu() == nullptr) {
        return D2DERR_WRONG_STATE; // GPU pixels change on the thread that owns them.
    } else {
        // Into the pixels where they are (a texture is updated in place: video frames).
        sk_sp<SkSurface> target = surface(on_gpu ? lm_gfx::gpu() : nullptr);
        if (!target) return E_OUTOFMEMORY;
        target->writePixels(pixels.pixmap(), static_cast<int>(area.left), static_cast<int>(area.top));
    }
    touch();
    return S_OK;
}

HRESULT Bitmap::copyFrom(sk_sp<SkImage> source, const D2D1_POINT_2U* destPoint, const D2D1_RECT_U* srcRect) {
    if (!source) return E_INVALIDARG;
    const D2D1_RECT_U area = srcRect ? *srcRect : D2D1_RECT_U{0, 0, static_cast<UINT32>(source->width()), static_cast<UINT32>(source->height())};
    const D2D1_POINT_2U at = destPoint ? *destPoint : D2D1_POINT_2U{0, 0};
    if (area.left > area.right || area.top > area.bottom || area.right > static_cast<UINT32>(source->width()) ||
        area.bottom > static_cast<UINT32>(source->height()))
        return E_INVALIDARG;
    GrDirectContext* gpu = source->isTextureBacked() ? lm_gfx::gpu() : nullptr;
    sk_sp<SkSurface> target = surface(gpu);
    if (!target) return E_OUTOFMEMORY;
    SkPaint copy;
    copy.setBlendMode(SkBlendMode::kSrc);
    const SkRect from = SkRect::MakeLTRB(area.left, area.top, area.right, area.bottom);
    const SkRect to = SkRect::MakeXYWH(at.x, at.y, from.width(), from.height());
    GrRecordingContext* recording = target->recordingContext();
    target->getCanvas()->drawImageRect(imageFor(recording ? recording->asDirectContext() : nullptr, std::move(source)), from, to,
                                       SkSamplingOptions(), &copy, SkCanvas::kStrict_SrcRectConstraint);
    touch();
    return S_OK;
}

HRESULT STDMETHODCALLTYPE Bitmap::CopyFromBitmap(const D2D1_POINT_2U* destPoint, ID2D1Bitmap* bitmap, const D2D1_RECT_U* srcRect) {
    auto* source = Bitmap::from(bitmap);
    if (!source || source == this) return E_INVALIDARG;
    return copyFrom(source->image(lm_gfx::gpu()), destPoint, srcRect);
}

HRESULT STDMETHODCALLTYPE Bitmap::CopyFromRenderTarget(const D2D1_POINT_2U* destPoint, ID2D1RenderTarget* renderTarget,
                                                       const D2D1_RECT_U* srcRect) {
    auto* canvas = Canvas::from(renderTarget);
    return canvas ? copyFrom(canvas->snapshot(), destPoint, srcRect) : E_INVALIDARG;
}

// At another resolution: drawn smaller through mipmaps (or larger), kept until the pixels change.
ImageData Bitmap::render(double scale, GrDirectContext* gpu) {
    const int width = std::max(1, static_cast<int>(std::lround(width_ / scaleX() * scale)));
    const int height = std::max(1, static_cast<int>(std::lround(height_ / scaleY() * scale)));
    sk_sp<SkImage> pixels = image(gpu);
    if (!pixels) return {};
    if (width == width_ && height == height_ && !alphaOnly()) return {std::move(pixels), 0, 0};
    if (scaled_.image && scaled_version_ == version_ && scaled_scale_ == scale) return scaled_;
    sk_sp<SkSurface> surface = makeSurface(gpu, bgraInfo(width, height));
    if (!surface) return {};
    SkPaint paint;
    paint.setBlendMode(SkBlendMode::kSrc);
    const bool reducing = width < width_ || height < height_;
    surface->getCanvas()->drawImageRect(pixels, SkRect::MakeIWH(width, height), samplingFor(D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC, reducing, true),
                                        &paint);
    scaled_ = {surface->makeImageSnapshot(), 0, 0};
    scaled_version_ = version_;
    scaled_scale_ = scale;
    return scaled_;
}
} // namespace lm_d2d
