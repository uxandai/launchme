// Direct2D and WIC (wic_*.cpp): bitmaps from WIC bitmap sources and render targets drawing into a
// WIC bitmap, through the standard WIC methods only (GetSize, GetPixelFormat, CopyPixels, Lock).
// WIC targets draw on the GPU like every target and read the result back into the bitmap.
#include "d2d_internal.hpp"
#include "../win32/wincodec.h"

#include <cstring>
#include <vector>

namespace lm_d2d {
namespace {
// The alpha mode of a 32-bit BGRA WIC format; unknown for any other.
D2D1_ALPHA_MODE alphaOf(const WICPixelFormatGUID& format) {
    if (format == GUID_WICPixelFormat32bppPBGRA) return D2D1_ALPHA_MODE_PREMULTIPLIED;
    if (format == GUID_WICPixelFormat32bppBGRA) return D2D1_ALPHA_MODE_STRAIGHT;
    return D2D1_ALPHA_MODE_UNKNOWN;
}

// A render target whose frames start as the WIC bitmap's pixels and are written back into it
// (Lock) at EndDraw.
class WicTarget final : public RenderTarget<ID2D1DeviceContext1> {
public:
    WicTarget(ComPtr<IWICBitmap> bitmap, float dpi, std::shared_ptr<gx::Image> initial, int width, int height)
        : RenderTarget({DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED}, dpi), bitmap_(std::move(bitmap)), current_(std::move(initial)) {
        setSize(width, height);
    }
    HRESULT STDMETHODCALLTYPE EndDraw(D2D1_TAG* tag1, D2D1_TAG* tag2) override {
        const HRESULT result = RenderTarget::EndDraw(tag1, tag2);
        if (FAILED(result) && result != D2DERR_PUSH_POP_UNBALANCED) return result;
        const D2D1_SIZE_U size = pixelSize();
        auto image = snapshot();
        if (!image || !size.width || !size.height) return D2DERR_RECREATE_TARGET;
        const auto pixels = image->pixels();
        if (!pixels) return D2DERR_RECREATE_TARGET;
        const WICRect all{0, 0, static_cast<INT>(size.width), static_cast<INT>(size.height)};
        ComPtr<IWICBitmapLock> lock;
        UINT stride = 0, bytes = 0;
        BYTE* data = nullptr;
        if (FAILED(bitmap_->Lock(&all, WICBitmapLockWrite, lock.GetAddressOf())) || FAILED(lock->GetStride(&stride)) ||
            FAILED(lock->GetDataPointer(&bytes, &data)) || !data || bytes < stride * (size.height - 1) + size.width * 4u)
            return D2DERR_RECREATE_TARGET;
        for (UINT y = 0; y < size.height; ++y) std::memcpy(data + static_cast<size_t>(y) * stride, pixels->data() + static_cast<size_t>(y) * size.width * 4, size.width * 4u);
        current_ = std::move(image);
        return result;
    }
protected:
    void prepareFrame() override {
        attach(nullptr, static_cast<int>(pixelSize().width), static_cast<int>(pixelSize().height));
        if (!gx::active()) return;
        const int width = static_cast<int>(pixelSize().width), height = static_cast<int>(pixelSize().height);
        auto frame = std::make_shared<gx::Canvas>(width, height);
        if (!frame->healthy()) return;
        if (current_) {
            gx::Paint copy;
            copy.color = {0, 0, 0, 1};
            copy.blend = kImpellerBlendModeSource;
            const gx::Rect whole = gx::Rect::XYWH(0, 0, static_cast<float>(width), static_cast<float>(height));
            frame->drawImageRect(current_, whole, whole, gx::Sampling{true}, &copy);
        }
        attach(std::move(frame), width, height);
    }
private:
    ComPtr<IWICBitmap> bitmap_;
    std::shared_ptr<gx::Image> current_;
};
} // namespace

HRESULT bitmapFromWic(IWICBitmapSource* source, const D2D1_BITMAP_PROPERTIES* properties, ID2D1Bitmap** bitmap) {
    if (!bitmap) return E_POINTER;
    *bitmap = nullptr;
    if (!source) return E_INVALIDARG;
    UINT width = 0, height = 0;
    WICPixelFormatGUID format{};
    HRESULT result = source->GetSize(&width, &height);
    if (SUCCEEDED(result)) result = source->GetPixelFormat(&format);
    if (FAILED(result)) return result;
    const D2D1_ALPHA_MODE alpha = alphaOf(format);
    if (alpha == D2D1_ALPHA_MODE_UNKNOWN) return D2DERR_UNSUPPORTED_PIXEL_FORMAT;
    std::vector<BYTE> pixels(static_cast<size_t>(width) * height * 4);
    result = source->CopyPixels(nullptr, width * 4, static_cast<UINT>(pixels.size()), pixels.data());
    if (FAILED(result)) return result;
    ComPtr<Bitmap> created;
    result = Bitmap::create({width, height}, pixels.data(), width * 4, {DXGI_FORMAT_B8G8R8A8_UNORM, alpha},
                            properties ? properties->dpiX : 96, properties ? properties->dpiY : 96, created);
    if (SUCCEEDED(result)) *bitmap = created.Detach();
    return result;
}

HRESULT createWicTarget(IWICBitmap* target, const D2D1_RENDER_TARGET_PROPERTIES& properties, ID2D1RenderTarget** renderTarget) {
    if (!renderTarget) return E_POINTER;
    *renderTarget = nullptr;
    if (!target) return E_INVALIDARG;
    UINT width = 0, height = 0;
    WICPixelFormatGUID format{};
    HRESULT result = target->GetSize(&width, &height);
    if (SUCCEEDED(result)) result = target->GetPixelFormat(&format);
    if (FAILED(result)) return result;
    if (format != GUID_WICPixelFormat32bppPBGRA) return D2DERR_UNSUPPORTED_PIXEL_FORMAT;
    std::vector<uint8_t> pixels(static_cast<size_t>(width) * height * 4);
    result = target->CopyPixels(nullptr, width * 4, static_cast<UINT>(pixels.size()), pixels.data());
    if (FAILED(result)) return result;
    auto initial = gx::Image::fromPixels(static_cast<int>(width), static_cast<int>(height), std::move(pixels));
    if (!initial) return E_OUTOFMEMORY;
    ComPtr<WicTarget> created;
    created.Attach(new WicTarget(ComPtr<IWICBitmap>(target), properties.dpiX, std::move(initial), static_cast<int>(width), static_cast<int>(height)));
    *renderTarget = created.Detach();
    return S_OK;
}
} // namespace lm_d2d
