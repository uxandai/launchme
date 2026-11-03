// Direct2D bitmaps on kernel images: premultiplied BGRA (Direct2D's B8G8R8A8 in memory), or alpha
// only (kept as BGRA with no colour). Straight, ignored-alpha and R8G8B8A8 pixels are converted
// on the way in; BC1/BC3 blocks go straight into a compressed GPU texture where the GPU samples
// them (createCompressed), other block-compressed formats are refused. A bitmap a render target
// drew into holds the render's GPU texture until something asks for its pixels.
#include "d2d_internal.hpp"

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
    if (format.format == DXGI_FORMAT_A8_UNORM) {
        for (int x = 0; x < width; ++x, target += 4) { target[0] = target[1] = target[2] = 0; target[3] = source[x]; }
        return;
    }
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
} // namespace

uint64_t newVersion() {
    static std::atomic<uint64_t> counter{0};
    return ++counter;
}

D2D1_PIXEL_FORMAT resolveFormat(D2D1_PIXEL_FORMAT format) {
    if (format.format == DXGI_FORMAT_UNKNOWN) format.format = DXGI_FORMAT_B8G8R8A8_UNORM;
    if (format.alphaMode == D2D1_ALPHA_MODE_UNKNOWN)
        format.alphaMode = format.format == DXGI_FORMAT_B8G8R8X8_UNORM ? D2D1_ALPHA_MODE_IGNORE : D2D1_ALPHA_MODE_PREMULTIPLIED;
    return format;
}

gx::Sampling samplingFor(D2D1_INTERPOLATION_MODE mode) { return {mode == D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR}; }

Bitmap::Bitmap(int width, int height, D2D1_PIXEL_FORMAT format, float dpiX, float dpiY)
    : width_(width), height_(height), format_(format), dpi_x_(dpiX > 0 ? dpiX : 96), dpi_y_(dpiY > 0 ? dpiY : 96), version_(newVersion()) {}

HRESULT Bitmap::create(D2D1_SIZE_U size, const void* data, UINT32 pitch, D2D1_PIXEL_FORMAT format, float dpiX, float dpiY,
                       ComPtr<Bitmap>& out) {
    format = resolveFormat(format);
    if (!supported(format.format)) return D2DERR_UNSUPPORTED_PIXEL_FORMAT;
    if (size.width > 16384 || size.height > 16384) return E_INVALIDARG; // The largest texture side GPUs promise.
    out.Attach(new Bitmap(static_cast<int>(size.width), static_cast<int>(size.height), format, dpiX, dpiY));
    // Without data the pixels are transparent: nothing is held until something is drawn or copied in.
    return data ? out->CopyFromMemory(nullptr, data, pitch) : S_OK;
}

bool blockCompressionSupported(DXGI_FORMAT format) {
    return (format == DXGI_FORMAT_BC1_UNORM && gx::supportsCompression(1)) || (format == DXGI_FORMAT_BC3_UNORM && gx::supportsCompression(3));
}

HRESULT Bitmap::createCompressed(D2D1_SIZE_U size, const void* data, UINT32 pitch, D2D1_PIXEL_FORMAT format, float dpiX, float dpiY,
                                 ComPtr<Bitmap>& out) {
    const unsigned compression = format.format == DXGI_FORMAT_BC1_UNORM ? 1 : format.format == DXGI_FORMAT_BC3_UNORM ? 3 : 0;
    if (!blockCompressionSupported(format.format)) return D2DERR_UNSUPPORTED_PIXEL_FORMAT;
    if (!data || !size.width || !size.height || size.width % 4 || size.height % 4 || size.width > 16384 || size.height > 16384 ||
        pitch != size.width / 4 * (compression == 1 ? 8u : 16u)) return E_INVALIDARG;
    auto image = gx::Image::fromCompressed(compression, static_cast<int>(size.width), static_cast<int>(size.height),
                                           static_cast<const uint8_t*>(data), static_cast<size_t>(pitch) * (size.height / 4));
    if (!image) return E_FAIL;
    out.Attach(new Bitmap(static_cast<int>(size.width), static_cast<int>(size.height), {format.format, D2D1_ALPHA_MODE_PREMULTIPLIED}, dpiX, dpiY));
    out->image_ = std::move(image);
    return S_OK;
}

Bitmap* Bitmap::from(ID2D1Image* image) { return dynamic_cast<Bitmap*>(image); }
ImageSource* ImageSource::from(ID2D1Image* image) { return dynamic_cast<ImageSource*>(image); }

D2D1_SIZE_F STDMETHODCALLTYPE Bitmap::GetSize() const {
    return {static_cast<float>(width_ / scaleX()), static_cast<float>(height_ / scaleY())};
}
D2D1_SIZE_U STDMETHODCALLTYPE Bitmap::GetPixelSize() const { return {static_cast<UINT32>(width_), static_cast<UINT32>(height_)}; }

std::shared_ptr<gx::Image> Bitmap::image() {
    if (!image_) image_ = gx::Image::fromPixels(width_, height_, std::vector<uint8_t>(static_cast<size_t>(width_) * height_ * 4));
    return image_;
}

void Bitmap::setImage(std::shared_ptr<gx::Image> image) {
    image_ = std::move(image);
    version_ = newVersion();
    scaled_ = {};
}

HRESULT STDMETHODCALLTYPE Bitmap::CopyFromMemory(const D2D1_RECT_U* rect, const void* data, UINT32 pitch) {
    if (!data) return E_INVALIDARG;
    const D2D1_RECT_U area = rect ? *rect : D2D1_RECT_U{0, 0, static_cast<UINT32>(width_), static_cast<UINT32>(height_)};
    if (area.right > static_cast<UINT32>(width_) || area.bottom > static_cast<UINT32>(height_) || area.left > area.right || area.top > area.bottom)
        return E_INVALIDARG;
    const int columns = static_cast<int>(area.right - area.left), rows = static_cast<int>(area.bottom - area.top);
    if (!columns || !rows) return S_OK;
    const bool whole = columns == width_ && rows == height_;
    std::vector<uint8_t> pixels;
    if (whole) {
        pixels.resize(static_cast<size_t>(width_) * height_ * 4);
    } else {
        // Into the pixels so far (read back when they are on the GPU).
        const auto current = image_ ? image_->pixels() : nullptr;
        if (image_ && !current) return D2DERR_WRONG_STATE;
        pixels = current ? *current : std::vector<uint8_t>(static_cast<size_t>(width_) * height_ * 4);
    }
    const auto* source = static_cast<const uint8_t*>(data);
    for (int y = 0; y < rows; ++y, source += pitch) {
        uint8_t* row = pixels.data() + (static_cast<size_t>(area.top + y) * width_ + area.left) * 4;
        convertRow(source, row, columns, format_);
    }
    auto made = gx::Image::fromPixels(width_, height_, std::move(pixels));
    if (!made) return E_OUTOFMEMORY;
    setImage(std::move(made));
    return S_OK;
}

HRESULT Bitmap::copyFrom(std::shared_ptr<gx::Image> source, const D2D1_POINT_2U* destPoint, const D2D1_RECT_U* srcRect) {
    if (!source) return E_INVALIDARG;
    const D2D1_RECT_U area = srcRect ? *srcRect : D2D1_RECT_U{0, 0, static_cast<UINT32>(source->width()), static_cast<UINT32>(source->height())};
    const D2D1_POINT_2U at = destPoint ? *destPoint : D2D1_POINT_2U{0, 0};
    if (area.left > area.right || area.top > area.bottom || area.right > static_cast<UINT32>(source->width()) ||
        area.bottom > static_cast<UINT32>(source->height()))
        return E_INVALIDARG;
    if (area.left == area.right || area.top == area.bottom) return S_OK;
    const gx::Rect from = gx::Rect::LTRB(area.left, area.top, area.right, area.bottom);
    const gx::Rect to = gx::Rect::XYWH(static_cast<float>(at.x), static_cast<float>(at.y), from.width(), from.height());
    if (gx::active()) {
        // On the GPU: the pixels so far, then the copied part over them.
        gx::Canvas frame(width_, height_);
        gx::Paint copy;
        copy.color = {0, 0, 0, 1};
        copy.blend = kImpellerBlendModeSource;
        if (image_) {
            const gx::Rect whole = gx::Rect::XYWH(0, 0, static_cast<float>(width_), static_cast<float>(height_));
            frame.drawImageRect(image_, whole, whole, gx::Sampling{true}, &copy);
        }
        frame.save();
        frame.clipRect(to.offset(0, 0).sorted());
        frame.drawImageRect(source, from, to, gx::Sampling{true}, &copy);
        frame.restore();
        auto result = frame.snapshot();
        if (!result) return E_FAIL;
        setImage(std::move(result));
        return S_OK;
    }
    // In memory.
    const auto from_pixels = source->pixels();
    if (!from_pixels) return D2DERR_WRONG_STATE;
    const auto current = image_ ? image_->pixels() : nullptr;
    if (image_ && !current) return D2DERR_WRONG_STATE;
    std::vector<uint8_t> pixels = current ? *current : std::vector<uint8_t>(static_cast<size_t>(width_) * height_ * 4);
    const UINT32 columns = std::min(area.right - area.left, static_cast<UINT32>(width_) > at.x ? static_cast<UINT32>(width_) - at.x : 0u);
    const UINT32 rows = std::min(area.bottom - area.top, static_cast<UINT32>(height_) > at.y ? static_cast<UINT32>(height_) - at.y : 0u);
    for (UINT32 y = 0; y < rows; ++y)
        std::memcpy(pixels.data() + (static_cast<size_t>(at.y + y) * width_ + at.x) * 4,
                    from_pixels->data() + (static_cast<size_t>(area.top + y) * source->width() + area.left) * 4, static_cast<size_t>(columns) * 4);
    auto made = gx::Image::fromPixels(width_, height_, std::move(pixels));
    if (!made) return E_OUTOFMEMORY;
    setImage(std::move(made));
    return S_OK;
}

HRESULT STDMETHODCALLTYPE Bitmap::CopyFromBitmap(const D2D1_POINT_2U* destPoint, ID2D1Bitmap* bitmap, const D2D1_RECT_U* srcRect) {
    auto* source = Bitmap::from(bitmap);
    if (!source || source == this) return E_INVALIDARG;
    return copyFrom(source->image(), destPoint, srcRect);
}

HRESULT STDMETHODCALLTYPE Bitmap::CopyFromRenderTarget(const D2D1_POINT_2U* destPoint, ID2D1RenderTarget* renderTarget,
                                                       const D2D1_RECT_U* srcRect) {
    auto* canvas = Canvas::from(renderTarget);
    return canvas ? copyFrom(canvas->snapshot(), destPoint, srcRect) : E_INVALIDARG;
}

// At another resolution: drawn smaller through reductions (or larger), kept until the pixels change.
ImageData Bitmap::render(double scale) {
    const int width = std::max(1, static_cast<int>(std::lround(width_ / scaleX() * scale)));
    const int height = std::max(1, static_cast<int>(std::lround(height_ / scaleY() * scale)));
    std::shared_ptr<gx::Image> pixels = image();
    if (!pixels) return {};
    if (width == width_ && height == height_) return {std::move(pixels), 0, 0};
    if (scaled_.image && scaled_version_ == version_ && scaled_scale_ == scale) return scaled_;
    if (!gx::active()) return {};
    gx::Canvas frame(width, height);
    gx::Paint copy;
    copy.color = {0, 0, 0, 1};
    copy.blend = kImpellerBlendModeSource;
    frame.drawImageRect(pixels, gx::Rect::XYWH(0, 0, static_cast<float>(width_), static_cast<float>(height_)),
                        gx::Rect::XYWH(0, 0, static_cast<float>(width), static_cast<float>(height)), gx::Sampling{false}, &copy);
    auto result = frame.snapshot();
    if (!result) return {};
    scaled_ = {std::move(result), 0, 0};
    scaled_version_ = version_;
    scaled_scale_ = scale;
    return scaled_;
}
} // namespace lm_d2d
