#pragma once
// Linux only: the Direct2D port's private types, shared by d2d_*.cpp. Drawing goes through a
// Skia canvas (on the GPU for window targets and their offscreen targets, in memory for WIC
// targets and other threads); geometry is SkPath, brushes are shaders, bitmaps are SkImages.
#include "gfx_internal.hpp"
#include "../impeller/backend.hpp"
#include "../win32/d2d1_2.h"
#include "../win32/wrl/client.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkImage.h"
#include "include/core/SkMatrix.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPath.h"
#include "include/core/SkSamplingOptions.h"
#include "include/core/SkShader.h"
#include "include/core/SkSurface.h"

#include <cstdint>
#include <mutex>
#include <utility>
#include <vector>

namespace lm_d2d {
using Microsoft::WRL::ComPtr;

// A fresh stamp for content that changed (bitmap pixels, effect settings): never repeats.
uint64_t newVersion();

// Direct2D's row-vector matrix as Skia's.
inline SkMatrix toSk(const D2D1_MATRIX_3X2_F& m) { return SkMatrix::MakeAll(m._11, m._21, m._31, m._12, m._22, m._32, 0, 0, 1); }
inline SkRect toSk(const D2D1_RECT_F& r) { return SkRect::MakeLTRB(r.left, r.top, r.right, r.bottom); }
// Premultiplied BGRA (Direct2D's B8G8R8A8 in memory) at `width` x `height`.
inline SkImageInfo bgraInfo(int width, int height, SkAlphaType alpha = kPremul_SkAlphaType) {
    return SkImageInfo::Make(width, height, kBGRA_8888_SkColorType, alpha);
}
// The GPU context for drawing on this thread (null: in memory).
inline GrDirectContext* threadGpu() { return lm_gfx::gpu(); }
// A surface for offscreen drawing: on `gpu` when given, else in memory. Null when it cannot be made.
sk_sp<SkSurface> makeSurface(GrDirectContext* gpu, const SkImageInfo& info, bool mipmaps = false);
// `image` usable on a canvas of `gpu` (null: in memory): uploaded or read back as needed.
sk_sp<SkImage> imageFor(GrDirectContext* gpu, sk_sp<SkImage> image);

// ---- Geometry (d2d_geometry.cpp) ----------------------------------------------------------

// Every geometry object of this port: its figures as Skia paths in its own space.
class Geometry {
public:
    virtual ~Geometry() = default;
    // All figures (what a stroke draws).
    virtual const SkPath& outline() const = 0;
    // The filled figures only, with the fill rule (what a fill draws).
    virtual const SkPath& area() const = 0;
    static const Geometry* from(ID2D1Geometry* geometry);
};

HRESULT createRectangleGeometry(const D2D1_RECT_F& rect, ID2D1RectangleGeometry** geometry);
HRESULT createRoundedRectangleGeometry(const D2D1_ROUNDED_RECT& rounded, ID2D1RoundedRectangleGeometry** geometry);
HRESULT createEllipseGeometry(const D2D1_ELLIPSE& ellipse, ID2D1EllipseGeometry** geometry);
HRESULT createGeometryGroup(D2D1_FILL_MODE fillMode, ID2D1Geometry** geometries, UINT32 count, ID2D1GeometryGroup** group);
HRESULT createTransformedGeometry(ID2D1Geometry* source, const D2D1_MATRIX_3X2_F& transform, ID2D1TransformedGeometry** geometry);
HRESULT createPathGeometry(ID2D1PathGeometry** geometry);
// The shapes drawn directly (FillRoundedRectangle, DrawEllipse…), as their geometry objects have them.
SkPath roundedRectanglePath(const D2D1_ROUNDED_RECT& rounded);
SkPath ellipsePath(const D2D1_ELLIPSE& ellipse);

// ---- Stroke styles (d2d_factory.cpp) ------------------------------------------------------

class StrokeStyle final : public lm_com::Object<ID2D1StrokeStyle> {
public:
    StrokeStyle(const D2D1_STROKE_STYLE_PROPERTIES& properties, const FLOAT* dashes, UINT32 count);
    // Caps, join, miter limit and dashes for a stroke `width` wide.
    void apply(SkPaint& paint, float width) const;
    static const StrokeStyle* from(ID2D1StrokeStyle* style);
private:
    D2D1_STROKE_STYLE_PROPERTIES properties_;
    std::vector<float> dashes_; // In stroke widths.
};
// The defaults of a stroke without a style: flat caps, mitred joins.
void applyDefaultStroke(SkPaint& paint, float width);

// ---- Images (d2d_bitmap.cpp, d2d_effects.cpp) ---------------------------------------------

// Effect inputs and results: `image` placed at (x, y) in pixels of the resolution it was asked
// for (`scale` pixels per DIP).
struct ImageData {
    sk_sp<SkImage> image; // Empty for nothing.
    int x = 0, y = 0;
};
class ImageSource {
public:
    virtual ~ImageSource() = default;
    // The image at `scale` pixels per DIP, for a canvas of `gpu` (null: in memory).
    virtual ImageData render(double scale, GrDirectContext* gpu) = 0;
    // Changes whenever what render() returns would change.
    virtual uint64_t version() const = 0;
    static ImageSource* from(ID2D1Image* image);
};

// Pixels as an SkImage (in memory, or a GPU texture once drawn on the GPU), or a surface for a
// bitmap a render target draws into or pixels are copied into.
class Bitmap final : public lm_com::Object<ID2D1Bitmap1>, public ImageSource {
public:
    // Pixels from memory in `format` (nullptr: transparent). Fails for formats it cannot hold
    // (block-compressed ones among them: createCompressed).
    static HRESULT create(D2D1_SIZE_U size, const void* data, UINT32 pitch, D2D1_PIXEL_FORMAT format, float dpiX, float dpiY,
                          ComPtr<Bitmap>& out);
    // BC1 blocks straight into a GPU texture of `gpu` (blockCompressionSupported): an eighth of BGRA's
    // memory. Fails without such a GPU, or for another format.
    static HRESULT createCompressed(D2D1_SIZE_U size, const void* data, UINT32 pitch, D2D1_PIXEL_FORMAT format, float dpiX, float dpiY,
                                    GrDirectContext* gpu, ComPtr<Bitmap>& out, bool impeller = false);
    static Bitmap* from(ID2D1Image* image);

    D2D1_SIZE_F STDMETHODCALLTYPE GetSize() const override;
    D2D1_SIZE_U STDMETHODCALLTYPE GetPixelSize() const override;
    D2D1_PIXEL_FORMAT STDMETHODCALLTYPE GetPixelFormat() const override { return format_; }
    HRESULT STDMETHODCALLTYPE CopyFromBitmap(const D2D1_POINT_2U* destPoint, ID2D1Bitmap* bitmap, const D2D1_RECT_U* srcRect) override;
    HRESULT STDMETHODCALLTYPE CopyFromRenderTarget(const D2D1_POINT_2U* destPoint, ID2D1RenderTarget* renderTarget,
                                                   const D2D1_RECT_U* srcRect) override;
    HRESULT STDMETHODCALLTYPE CopyFromMemory(const D2D1_RECT_U* dstRect, const void* srcData, UINT32 pitch) override;

    ImageData render(double scale, GrDirectContext* gpu) override;
    uint64_t version() const override { return version_; }

    // The pixels for a canvas of `gpu` (null: in memory); a GPU upload keeps mipmaps.
    sk_sp<SkImage> image(GrDirectContext* gpu);
    // The surface drawing goes into (made on `gpu` when there is none yet); the pixels so far stay.
    sk_sp<SkSurface> surface(GrDirectContext* gpu);
    int width() const { return width_; }
    int height() const { return height_; }
    // Pixels per DIP.
    double scaleX() const { return dpi_x_ / 96.0; }
    double scaleY() const { return dpi_y_ / 96.0; }
    bool alphaOnly() const { return format_.format == DXGI_FORMAT_A8_UNORM; }
    // The pixels changed (drawn into by its render target).
    void touch() { version_ = newVersion(); snapshot_.reset(); }
private:
    Bitmap(int width, int height, D2D1_PIXEL_FORMAT format, float dpiX, float dpiY);
    HRESULT copyFrom(sk_sp<SkImage> source, const D2D1_POINT_2U* destPoint, const D2D1_RECT_U* srcRect);
    SkImageInfo info() const;

    int width_, height_;
    D2D1_PIXEL_FORMAT format_;
    float dpi_x_, dpi_y_;
    uint64_t version_;
    sk_sp<SkImage> image_;      // The pixels, when there is no surface.
    sk_sp<SkSurface> surface_;  // The pixels of a drawn-into bitmap.
    sk_sp<SkImage> snapshot_;   // surface_'s pixels as last read.
    ImageData scaled_;          // render() at another size.
    uint64_t scaled_version_ = 0;
    double scaled_scale_ = 0;
};
// The pixel format a bitmap or render target gets for a requested one (unknown parts filled in).
D2D1_PIXEL_FORMAT resolveFormat(D2D1_PIXEL_FORMAT format);
// A block-compressed format `gpu` samples (BC1 on Vulkan or OpenGL drivers that have it); never in memory.
bool blockCompressionSupported(GrDirectContext* gpu, DXGI_FORMAT format);
// Interpolation modes as Skia sampling, for drawing `pixels` source pixels into `shown` device pixels.
SkSamplingOptions samplingFor(D2D1_INTERPOLATION_MODE mode, bool reducing, bool mipmapped);

// ---- Brushes (d2d_brush.cpp) --------------------------------------------------------------

class Brush {
public:
    virtual ~Brush() = default;
    // Sets the paint's colour or shader (in the render target's world space) and alpha, for a
    // canvas of `gpu` (null: in memory); `extra`: an opacity multiplied in (a layer's).
    virtual void paint(SkPaint& paint, GrDirectContext* gpu, float extra = 1) const = 0;
    static const Brush* from(IUnknown* brush);
};
HRESULT createSolidColorBrush(const D2D1_COLOR_F& color, const D2D1_BRUSH_PROPERTIES* properties, ID2D1SolidColorBrush** brush);
HRESULT createGradientStopCollection(const D2D1_GRADIENT_STOP* stops, UINT32 count, D2D1_EXTEND_MODE extendMode,
                                     ID2D1GradientStopCollection** collection);
HRESULT createLinearGradientBrush(const D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES& gradient, const D2D1_BRUSH_PROPERTIES* properties,
                                  ID2D1GradientStopCollection* stops, ID2D1LinearGradientBrush** brush);
HRESULT createRadialGradientBrush(const D2D1_RADIAL_GRADIENT_BRUSH_PROPERTIES& gradient, const D2D1_BRUSH_PROPERTIES* properties,
                                  ID2D1GradientStopCollection* stops, ID2D1RadialGradientBrush** brush);
HRESULT createBitmapBrush(ID2D1Bitmap* bitmap, const D2D1_BITMAP_BRUSH_PROPERTIES* bitmapProperties, const D2D1_BRUSH_PROPERTIES* properties,
                          ID2D1BitmapBrush** brush);

// ---- Effects (d2d_effects.cpp) ------------------------------------------------------------

HRESULT createEffect(REFCLSID effectId, ID2D1Effect** effect);

// ---- WIC interop (d2d_wic.cpp) ------------------------------------------------------------

HRESULT bitmapFromWic(IWICBitmapSource* source, const D2D1_BITMAP_PROPERTIES* properties, ID2D1Bitmap** bitmap);
HRESULT createWicTarget(IWICBitmap* target, const D2D1_RENDER_TARGET_PROPERTIES& properties, ID2D1RenderTarget** renderTarget);

// ---- Render targets (d2d_target.cpp, text in d2d_text.cpp) --------------------------------

// The drawing every render target shares: state, clips and layers over a Skia canvas on the
// target's surface. Method names follow ID2D1RenderTarget / ID2D1DeviceContext1's.
class Canvas {
public:
    virtual ~Canvas();
    static Canvas* from(ID2D1RenderTarget* target);
    // What was drawn so far (pushed clips and layers aside), as an image.
    sk_sp<SkImage> snapshot();
    // The GPU context the target draws with (null: in memory).
    GrDirectContext* gpu() const { return gpu_; }
    bool usesImpeller() const { return impeller_; }

    HRESULT createBitmap(D2D1_SIZE_U size, const void* data, UINT32 pitch, const D2D1_BITMAP_PROPERTIES* properties, ID2D1Bitmap** bitmap);
    HRESULT createBitmap1(D2D1_SIZE_U size, const void* data, UINT32 pitch, const D2D1_BITMAP_PROPERTIES1* properties, ID2D1Bitmap1** bitmap);
    HRESULT createCompatibleRenderTarget(const D2D1_SIZE_F* size, const D2D1_SIZE_U* pixelSize, const D2D1_PIXEL_FORMAT* format,
                                         ID2D1BitmapRenderTarget** target);
    HRESULT createLayer(ID2D1Layer** layer);
    HRESULT createFilledRealization(ID2D1Geometry* geometry, ID2D1GeometryRealization** realization);
    HRESULT createStrokedRealization(ID2D1Geometry* geometry, FLOAT width, ID2D1StrokeStyle* style, ID2D1GeometryRealization** realization);

    void drawLine(D2D1_POINT_2F from, D2D1_POINT_2F to, ID2D1Brush* brush, FLOAT width, ID2D1StrokeStyle* style);
    void drawRectangle(const D2D1_RECT_F* rect, ID2D1Brush* brush, FLOAT width, ID2D1StrokeStyle* style);
    void fillRectangle(const D2D1_RECT_F* rect, ID2D1Brush* brush);
    void drawRoundedRectangle(const D2D1_ROUNDED_RECT* rounded, ID2D1Brush* brush, FLOAT width, ID2D1StrokeStyle* style);
    void fillRoundedRectangle(const D2D1_ROUNDED_RECT* rounded, ID2D1Brush* brush);
    void drawEllipse(const D2D1_ELLIPSE* ellipse, ID2D1Brush* brush, FLOAT width, ID2D1StrokeStyle* style);
    void fillEllipse(const D2D1_ELLIPSE* ellipse, ID2D1Brush* brush);
    void drawGeometry(ID2D1Geometry* geometry, ID2D1Brush* brush, FLOAT width, ID2D1StrokeStyle* style);
    void fillGeometry(ID2D1Geometry* geometry, ID2D1Brush* brush, ID2D1Brush* opacityBrush);
    void drawGeometryRealization(ID2D1GeometryRealization* realization, ID2D1Brush* brush);
    void fillOpacityMask(ID2D1Bitmap* mask, ID2D1Brush* brush, const D2D1_RECT_F* destination, const D2D1_RECT_F* source);
    void drawBitmap(ID2D1Bitmap* bitmap, const D2D1_RECT_F* destination, FLOAT opacity, D2D1_INTERPOLATION_MODE mode,
                    const D2D1_RECT_F* source, const D2D1_MATRIX_4X4_F* perspective);
    void drawImage(ID2D1Image* image, const D2D1_POINT_2F* offset, const D2D1_RECT_F* imageRectangle, D2D1_INTERPOLATION_MODE mode,
                   D2D1_COMPOSITE_MODE composite);
    void drawText(const WCHAR* text, UINT32 length, IDWriteTextFormat* format, const D2D1_RECT_F* rect, ID2D1Brush* brush,
                  D2D1_DRAW_TEXT_OPTIONS options);
    void drawTextLayout(D2D1_POINT_2F origin, IDWriteTextLayout* layout, ID2D1Brush* brush, D2D1_DRAW_TEXT_OPTIONS options);

    void setTransform(const D2D1_MATRIX_3X2_F* transform) { if (transform) transform_ = *transform; }
    void getTransform(D2D1_MATRIX_3X2_F* transform) const { if (transform) *transform = transform_; }
    void setAntialiasMode(D2D1_ANTIALIAS_MODE mode) { antialias_ = mode; }
    D2D1_ANTIALIAS_MODE antialiasMode() const { return antialias_; }
    void setTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE mode) { text_antialias_ = mode; }
    D2D1_TEXT_ANTIALIAS_MODE textAntialiasMode() const { return text_antialias_; }
    void setPrimitiveBlend(D2D1_PRIMITIVE_BLEND blend) { blend_ = blend; }
    D2D1_PRIMITIVE_BLEND primitiveBlend() const { return blend_; }

    void pushLayer(const D2D1_LAYER_PARAMETERS1& parameters);
    void popLayer();
    void pushAxisAlignedClip(const D2D1_RECT_F* rect, D2D1_ANTIALIAS_MODE mode);
    void popAxisAlignedClip();
    void clear(const D2D1_COLOR_F* color);
    void beginDraw();
    HRESULT endDraw();

    D2D1_PIXEL_FORMAT pixelFormat() const { return format_; }
    void getDpi(FLOAT* dpiX, FLOAT* dpiY) const;
    D2D1_SIZE_F size() const;
    D2D1_SIZE_U pixelSize() const { return {static_cast<UINT32>(width_), static_cast<UINT32>(height_)}; }

protected:
    // `format`: resolved; an IGNORE alpha mode keeps the pixels opaque. `dpi`: 96 for one pixel per DIP.
    Canvas(D2D1_PIXEL_FORMAT format, float dpi);
    // The surface drawn into until the next attach (null: nothing to draw into).
    void attach(sk_sp<SkSurface> surface, int width, int height);
    void attachCommands(std::shared_ptr<SkCanvas> canvas, int width, int height);
    void setSize(int width, int height) { width_ = width; height_ = height; }
    SkSurface* surface() const { return surface_.get(); }
    // BeginDraw (before `prepareFrame`) and EndDraw: the target's pixels change.
    virtual void changed() {}
    // BeginDraw: a window target fetches its frame's surface here.
    virtual void prepareFrame() {}

private:
    struct Layer {
        bool clip = false;           // PushAxisAlignedClip; otherwise a layer.
        int restore_to = 1;          // Skia's save count before it.
        SkMatrix world;              // The transform when pushed (an opacity brush lies in it).
        ComPtr<ID2D1Brush> opacity_brush;
    };
    // The canvas set for a primitive (matrix = world transform); null when nothing can be drawn.
    SkCanvas* prepare();
    SkMatrix world() const;
    // A paint for the brush with the target's antialiasing and blend.
    bool brushPaint(ID2D1Brush* brush, SkPaint& paint) const;
    void fillPath(const SkPath& path, ID2D1Brush* brush);
    void strokePath(const SkPath& path, ID2D1Brush* brush, FLOAT width, ID2D1StrokeStyle* style);
    void unwind();

    sk_sp<SkSurface> surface_;
    SkCanvas* canvas_ = nullptr;
    std::shared_ptr<SkCanvas> commands_; // May outlive the HWND; never leave a dangling command canvas.
    GrDirectContext* gpu_ = nullptr;
    bool impeller_ = false;
    int width_ = 0, height_ = 0;
    D2D1_PIXEL_FORMAT format_;
    float dpi_;
    D2D1_MATRIX_3X2_F transform_{};
    D2D1_ANTIALIAS_MODE antialias_ = D2D1_ANTIALIAS_MODE_PER_PRIMITIVE;
    D2D1_TEXT_ANTIALIAS_MODE text_antialias_ = D2D1_TEXT_ANTIALIAS_MODE_DEFAULT;
    D2D1_PRIMITIVE_BLEND blend_ = D2D1_PRIMITIVE_BLEND_SOURCE_OVER;
    std::vector<Layer> stack_;
    bool unbalanced_ = false;
};

// The COM face of a Canvas for one set of render target interfaces (always with
// ID2D1DeviceContext1, as Windows' targets answer it); every method forwards to Canvas.
template <class... Interfaces>
class RenderTarget : public lm_com::Object<Interfaces...>, public Canvas {
public:
    HRESULT STDMETHODCALLTYPE CreateBitmap(D2D1_SIZE_U size, const void* data, UINT32 pitch, const D2D1_BITMAP_PROPERTIES* properties,
                                           ID2D1Bitmap** bitmap) override {
        return createBitmap(size, data, pitch, properties, bitmap);
    }
    HRESULT STDMETHODCALLTYPE CreateBitmap(D2D1_SIZE_U size, const void* data, UINT32 pitch, const D2D1_BITMAP_PROPERTIES1* properties,
                                           ID2D1Bitmap1** bitmap) override {
        return createBitmap1(size, data, pitch, properties, bitmap);
    }
    HRESULT STDMETHODCALLTYPE CreateBitmapFromWicBitmap(IWICBitmapSource* source, const D2D1_BITMAP_PROPERTIES* properties,
                                                        ID2D1Bitmap** bitmap) override {
        return bitmapFromWic(source, properties, bitmap);
    }
    HRESULT STDMETHODCALLTYPE CreateBitmapBrush(ID2D1Bitmap* bitmap, const D2D1_BITMAP_BRUSH_PROPERTIES* bitmapProperties,
                                                const D2D1_BRUSH_PROPERTIES* properties, ID2D1BitmapBrush** brush) override {
        return createBitmapBrush(bitmap, bitmapProperties, properties, brush);
    }
    HRESULT STDMETHODCALLTYPE CreateSolidColorBrush(const D2D1_COLOR_F* color, const D2D1_BRUSH_PROPERTIES* properties,
                                                    ID2D1SolidColorBrush** brush) override {
        if (!color) return E_INVALIDARG;
        return createSolidColorBrush(*color, properties, brush);
    }
    HRESULT STDMETHODCALLTYPE CreateGradientStopCollection(const D2D1_GRADIENT_STOP* stops, UINT32 count, D2D1_GAMMA,
                                                           D2D1_EXTEND_MODE extendMode, ID2D1GradientStopCollection** collection) override {
        return createGradientStopCollection(stops, count, extendMode, collection);
    }
    HRESULT STDMETHODCALLTYPE CreateLinearGradientBrush(const D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES* gradient, const D2D1_BRUSH_PROPERTIES* properties,
                                                        ID2D1GradientStopCollection* stops, ID2D1LinearGradientBrush** brush) override {
        if (!gradient) return E_INVALIDARG;
        return createLinearGradientBrush(*gradient, properties, stops, brush);
    }
    HRESULT STDMETHODCALLTYPE CreateRadialGradientBrush(const D2D1_RADIAL_GRADIENT_BRUSH_PROPERTIES* gradient, const D2D1_BRUSH_PROPERTIES* properties,
                                                        ID2D1GradientStopCollection* stops, ID2D1RadialGradientBrush** brush) override {
        if (!gradient) return E_INVALIDARG;
        return createRadialGradientBrush(*gradient, properties, stops, brush);
    }
    HRESULT STDMETHODCALLTYPE CreateCompatibleRenderTarget(const D2D1_SIZE_F* size, const D2D1_SIZE_U* pixelSize, const D2D1_PIXEL_FORMAT* format,
                                                           D2D1_COMPATIBLE_RENDER_TARGET_OPTIONS, ID2D1BitmapRenderTarget** target) override {
        return createCompatibleRenderTarget(size, pixelSize, format, target);
    }
    HRESULT STDMETHODCALLTYPE CreateLayer(const D2D1_SIZE_F*, ID2D1Layer** layer) override { return createLayer(layer); }
    void STDMETHODCALLTYPE DrawLine(D2D1_POINT_2F from, D2D1_POINT_2F to, ID2D1Brush* brush, FLOAT width, ID2D1StrokeStyle* style) override {
        drawLine(from, to, brush, width, style);
    }
    void STDMETHODCALLTYPE DrawRectangle(const D2D1_RECT_F* rect, ID2D1Brush* brush, FLOAT width, ID2D1StrokeStyle* style) override {
        drawRectangle(rect, brush, width, style);
    }
    void STDMETHODCALLTYPE FillRectangle(const D2D1_RECT_F* rect, ID2D1Brush* brush) override { fillRectangle(rect, brush); }
    void STDMETHODCALLTYPE DrawRoundedRectangle(const D2D1_ROUNDED_RECT* rounded, ID2D1Brush* brush, FLOAT width, ID2D1StrokeStyle* style) override {
        drawRoundedRectangle(rounded, brush, width, style);
    }
    void STDMETHODCALLTYPE FillRoundedRectangle(const D2D1_ROUNDED_RECT* rounded, ID2D1Brush* brush) override { fillRoundedRectangle(rounded, brush); }
    void STDMETHODCALLTYPE DrawEllipse(const D2D1_ELLIPSE* ellipse, ID2D1Brush* brush, FLOAT width, ID2D1StrokeStyle* style) override {
        drawEllipse(ellipse, brush, width, style);
    }
    void STDMETHODCALLTYPE FillEllipse(const D2D1_ELLIPSE* ellipse, ID2D1Brush* brush) override { fillEllipse(ellipse, brush); }
    void STDMETHODCALLTYPE DrawGeometry(ID2D1Geometry* geometry, ID2D1Brush* brush, FLOAT width, ID2D1StrokeStyle* style) override {
        drawGeometry(geometry, brush, width, style);
    }
    void STDMETHODCALLTYPE FillGeometry(ID2D1Geometry* geometry, ID2D1Brush* brush, ID2D1Brush* opacityBrush) override {
        fillGeometry(geometry, brush, opacityBrush);
    }
    void STDMETHODCALLTYPE FillOpacityMask(ID2D1Bitmap* mask, ID2D1Brush* brush, D2D1_OPACITY_MASK_CONTENT, const D2D1_RECT_F* destination,
                                           const D2D1_RECT_F* source) override {
        fillOpacityMask(mask, brush, destination, source);
    }
    void STDMETHODCALLTYPE DrawBitmap(ID2D1Bitmap* bitmap, const D2D1_RECT_F* destination, FLOAT opacity, D2D1_BITMAP_INTERPOLATION_MODE mode,
                                      const D2D1_RECT_F* source) override {
        drawBitmap(bitmap, destination, opacity, mode == D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR ?
                   D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR : D2D1_INTERPOLATION_MODE_LINEAR, source, nullptr);
    }
    void STDMETHODCALLTYPE DrawBitmap(ID2D1Bitmap* bitmap, const D2D1_RECT_F* destination, FLOAT opacity, D2D1_INTERPOLATION_MODE mode,
                                      const D2D1_RECT_F* source, const D2D1_MATRIX_4X4_F* perspective) override {
        drawBitmap(bitmap, destination, opacity, mode, source, perspective);
    }
    void STDMETHODCALLTYPE DrawText(const WCHAR* text, UINT32 length, IDWriteTextFormat* format, const D2D1_RECT_F* rect, ID2D1Brush* brush,
                                    D2D1_DRAW_TEXT_OPTIONS options, DWRITE_MEASURING_MODE) override {
        drawText(text, length, format, rect, brush, options);
    }
    void STDMETHODCALLTYPE DrawTextLayout(D2D1_POINT_2F origin, IDWriteTextLayout* layout, ID2D1Brush* brush, D2D1_DRAW_TEXT_OPTIONS options) override {
        drawTextLayout(origin, layout, brush, options);
    }
    void STDMETHODCALLTYPE SetTransform(const D2D1_MATRIX_3X2_F* transform) override { setTransform(transform); }
    void STDMETHODCALLTYPE GetTransform(D2D1_MATRIX_3X2_F* transform) const override { getTransform(transform); }
    void STDMETHODCALLTYPE SetAntialiasMode(D2D1_ANTIALIAS_MODE mode) override { setAntialiasMode(mode); }
    D2D1_ANTIALIAS_MODE STDMETHODCALLTYPE GetAntialiasMode() const override { return antialiasMode(); }
    void STDMETHODCALLTYPE SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE mode) override { setTextAntialiasMode(mode); }
    D2D1_TEXT_ANTIALIAS_MODE STDMETHODCALLTYPE GetTextAntialiasMode() const override { return textAntialiasMode(); }
    void STDMETHODCALLTYPE PushLayer(const D2D1_LAYER_PARAMETERS* p, ID2D1Layer*) override {
        if (p) pushLayer({p->contentBounds, p->geometricMask, p->maskAntialiasMode, p->maskTransform, p->opacity, p->opacityBrush,
                          D2D1_LAYER_OPTIONS1_NONE});
    }
    void STDMETHODCALLTYPE PushLayer(const D2D1_LAYER_PARAMETERS1* parameters, ID2D1Layer*) override {
        if (parameters) pushLayer(*parameters);
    }
    void STDMETHODCALLTYPE PopLayer() override { popLayer(); }
    void STDMETHODCALLTYPE PushAxisAlignedClip(const D2D1_RECT_F* rect, D2D1_ANTIALIAS_MODE mode) override { pushAxisAlignedClip(rect, mode); }
    void STDMETHODCALLTYPE PopAxisAlignedClip() override { popAxisAlignedClip(); }
    void STDMETHODCALLTYPE Clear(const D2D1_COLOR_F* color) override { clear(color); }
    void STDMETHODCALLTYPE BeginDraw() override { beginDraw(); }
    HRESULT STDMETHODCALLTYPE EndDraw(D2D1_TAG* tag1, D2D1_TAG* tag2) override {
        if (tag1) *tag1 = 0;
        if (tag2) *tag2 = 0;
        return endDraw();
    }
    D2D1_PIXEL_FORMAT STDMETHODCALLTYPE GetPixelFormat() const override { return pixelFormat(); }
    void STDMETHODCALLTYPE GetDpi(FLOAT* dpiX, FLOAT* dpiY) const override { getDpi(dpiX, dpiY); }
    D2D1_SIZE_F STDMETHODCALLTYPE GetSize() const override { return size(); }
    D2D1_SIZE_U STDMETHODCALLTYPE GetPixelSize() const override { return pixelSize(); }

    HRESULT STDMETHODCALLTYPE CreateEffect(REFCLSID effectId, ID2D1Effect** effect) override { return createEffect(effectId, effect); }
    BOOL STDMETHODCALLTYPE IsDxgiFormatSupported(DXGI_FORMAT format) const override {
        return format == DXGI_FORMAT_B8G8R8A8_UNORM || format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB || format == DXGI_FORMAT_B8G8R8X8_UNORM ||
               format == DXGI_FORMAT_R8G8B8A8_UNORM || format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB || format == DXGI_FORMAT_A8_UNORM ||
                blockCompressionSupported(gpu(), format) ||
                (usesImpeller() && lm_impeller::supportsCompression(format == DXGI_FORMAT_BC1_UNORM ? 1 : format == DXGI_FORMAT_BC3_UNORM ? 3 : 0));
    }
    void STDMETHODCALLTYPE SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND blend) override { setPrimitiveBlend(blend); }
    D2D1_PRIMITIVE_BLEND STDMETHODCALLTYPE GetPrimitiveBlend() const override { return primitiveBlend(); }
    void STDMETHODCALLTYPE DrawImage(ID2D1Image* image, const D2D1_POINT_2F* offset, const D2D1_RECT_F* imageRectangle,
                                     D2D1_INTERPOLATION_MODE mode, D2D1_COMPOSITE_MODE composite) override {
        drawImage(image, offset, imageRectangle, mode, composite);
    }
    HRESULT STDMETHODCALLTYPE CreateFilledGeometryRealization(ID2D1Geometry* geometry, FLOAT, ID2D1GeometryRealization** realization) override {
        return createFilledRealization(geometry, realization);
    }
    HRESULT STDMETHODCALLTYPE CreateStrokedGeometryRealization(ID2D1Geometry* geometry, FLOAT, FLOAT width, ID2D1StrokeStyle* style,
                                                               ID2D1GeometryRealization** realization) override {
        return createStrokedRealization(geometry, width, style, realization);
    }
    void STDMETHODCALLTYPE DrawGeometryRealization(ID2D1GeometryRealization* realization, ID2D1Brush* brush) override {
        drawGeometryRealization(realization, brush);
    }

protected:
    RenderTarget(D2D1_PIXEL_FORMAT format, float dpi) : Canvas(format, dpi) {}
};

// A render target drawing into `bitmap`'s pixels (CreateCompatibleRenderTarget) with `gpu`'s context.
HRESULT createBitmapTarget(ComPtr<Bitmap> bitmap, GrDirectContext* gpu, ID2D1BitmapRenderTarget** target);
HRESULT createHwndTarget(const D2D1_RENDER_TARGET_PROPERTIES& properties, const D2D1_HWND_RENDER_TARGET_PROPERTIES& window,
                         ID2D1HwndRenderTarget** target);
} // namespace lm_d2d
