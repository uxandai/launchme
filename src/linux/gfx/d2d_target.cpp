// Direct2D render targets on Skia: the shared drawing (Canvas) and the window and bitmap targets.
// The canvas matrix is the world transform (times the DPI scale) for every primitive;
// axis-aligned clips and layers are on Skia's save stack, which Direct2D's strict nesting keeps
// in step. A layer is a Skia layer only when it has an opacity or an opacity brush; content
// bounds and geometric masks are clips.
#include "d2d_internal.hpp"

#include "include/core/SkM44.h"
#include "include/core/SkMaskFilter.h"
#include "include/core/SkRRect.h"
#include "include/effects/SkShaderMaskFilter.h"
#include "include/gpu/ganesh/GrDirectContext.h"
#include "include/gpu/ganesh/GrRecordingContext.h"

#include <algorithm>
#include <cmath>

namespace lm_d2d {
namespace {
class LayerResource final : public lm_com::Object<ID2D1Layer> {};

// A geometry and how it is drawn: Direct2D tessellates once; Skia draws (and caches) the path.
class Realization final : public lm_com::Object<ID2D1GeometryRealization> {
public:
    Realization(ComPtr<ID2D1Geometry> geometry, bool filled, float width, ComPtr<ID2D1StrokeStyle> style)
        : geometry(std::move(geometry)), filled(filled), width(width), style(std::move(style)) {}
    const ComPtr<ID2D1Geometry> geometry;
    const bool filled;
    const float width;
    const ComPtr<ID2D1StrokeStyle> style;
};

SkBlendMode blendFor(D2D1_PRIMITIVE_BLEND blend) {
    switch (blend) {
    case D2D1_PRIMITIVE_BLEND_COPY: return SkBlendMode::kSrc;
    case D2D1_PRIMITIVE_BLEND_MIN: return SkBlendMode::kDarken;
    case D2D1_PRIMITIVE_BLEND_ADD: return SkBlendMode::kPlus;
    case D2D1_PRIMITIVE_BLEND_MAX: return SkBlendMode::kLighten;
    default: return SkBlendMode::kSrcOver;
    }
}

SkBlendMode blendFor(D2D1_COMPOSITE_MODE mode) {
    switch (mode) {
    case D2D1_COMPOSITE_MODE_DESTINATION_OVER: return SkBlendMode::kDstOver;
    case D2D1_COMPOSITE_MODE_SOURCE_IN: return SkBlendMode::kSrcIn;
    case D2D1_COMPOSITE_MODE_DESTINATION_IN: return SkBlendMode::kDstIn;
    case D2D1_COMPOSITE_MODE_SOURCE_OUT: return SkBlendMode::kSrcOut;
    case D2D1_COMPOSITE_MODE_DESTINATION_OUT: return SkBlendMode::kDstOut;
    case D2D1_COMPOSITE_MODE_SOURCE_ATOP: return SkBlendMode::kSrcATop;
    case D2D1_COMPOSITE_MODE_DESTINATION_ATOP: return SkBlendMode::kDstATop;
    case D2D1_COMPOSITE_MODE_XOR: return SkBlendMode::kXor;
    case D2D1_COMPOSITE_MODE_PLUS: return SkBlendMode::kPlus;
    case D2D1_COMPOSITE_MODE_SOURCE_COPY:
    case D2D1_COMPOSITE_MODE_BOUNDED_SOURCE_COPY: return SkBlendMode::kSrc;
    case D2D1_COMPOSITE_MODE_MASK_INVERT: return SkBlendMode::kDifference;
    default: return SkBlendMode::kSrcOver;
    }
}

bool unbounded(const D2D1_RECT_F& r) {
    constexpr float huge = 1e30f;
    return r.left <= -huge || r.top <= -huge || r.right >= huge || r.bottom >= huge;
}

bool invertible(const SkMatrix& m) { return m.isFinite() && m.invert(nullptr); }

GrDirectContext* directOf(SkSurface* surface) {
    GrRecordingContext* recording = surface ? surface->recordingContext() : nullptr;
    return recording ? recording->asDirectContext() : nullptr;
}

// Direct2D's 3D DrawBitmap matrix (row vectors (x, y, 0, 1), divided by w) as a perspective SkMatrix.
SkMatrix perspectiveOf(const D2D1_MATRIX_4X4_F& m) {
    return SkMatrix::MakeAll(m._11, m._21, m._41, m._12, m._22, m._42, m._14, m._24, m._44);
}
} // namespace

Canvas::Canvas(D2D1_PIXEL_FORMAT format, float dpi) : format_(format), dpi_(dpi > 0 ? dpi : 96), transform_(D2D1::IdentityMatrix()) {}

Canvas::~Canvas() = default;

Canvas* Canvas::from(ID2D1RenderTarget* target) { return dynamic_cast<Canvas*>(target); }

void Canvas::attach(sk_sp<SkSurface> surface, int width, int height) {
    if (surface == surface_ && !impeller_) { width_ = width; height_ = height; return; }
    unwind();
    commands_.reset();
    surface_ = std::move(surface);
    canvas_ = surface_ ? surface_->getCanvas() : nullptr;
    gpu_ = directOf(surface_.get());
    impeller_ = false;
    width_ = width;
    height_ = height;
}

void Canvas::attachCommands(std::shared_ptr<SkCanvas> canvas, int width, int height) {
    unwind();
    surface_.reset();
    gpu_ = nullptr;
    commands_ = std::move(canvas);
    canvas_ = commands_.get();
    impeller_ = canvas_ != nullptr;
    width_ = width;
    height_ = height;
}

sk_sp<SkImage> Canvas::snapshot() {
    return impeller_ ? lm_impeller::snapshot(canvas_) : surface_ ? surface_->makeImageSnapshot() : nullptr;
}

SkMatrix Canvas::world() const {
    SkMatrix m = toSk(transform_);
    m.postScale(dpi_ / 96.0f, dpi_ / 96.0f);
    return m;
}

SkCanvas* Canvas::prepare() {
    if (!canvas_) return nullptr;
    const SkMatrix matrix = world();
    if (!invertible(matrix)) return nullptr; // A collapsed transform draws nothing.
    canvas_->setMatrix(matrix);
    return canvas_;
}

bool Canvas::brushPaint(ID2D1Brush* brush, SkPaint& paint) const {
    const auto* own = Brush::from(brush);
    if (!own) return false;
    own->paint(paint, gpu_, 1);
    paint.setAntiAlias(antialias_ != D2D1_ANTIALIAS_MODE_ALIASED);
    paint.setBlendMode(blendFor(blend_));
    return true;
}

void Canvas::fillPath(const SkPath& path, ID2D1Brush* brush) {
    SkPaint paint;
    SkCanvas* canvas = brushPaint(brush, paint) ? prepare() : nullptr;
    if (canvas) canvas->drawPath(path, paint);
}

void Canvas::strokePath(const SkPath& path, ID2D1Brush* brush, FLOAT width, ID2D1StrokeStyle* style) {
    if (!(width > 0)) return;
    SkPaint paint;
    SkCanvas* canvas = brushPaint(brush, paint) ? prepare() : nullptr;
    if (!canvas) return;
    if (const auto* own = StrokeStyle::from(style)) own->apply(paint, width);
    else applyDefaultStroke(paint, width);
    canvas->drawPath(path, paint);
}

// ---- Resources ----------------------------------------------------------------------------

HRESULT Canvas::createBitmap(D2D1_SIZE_U size, const void* data, UINT32 pitch, const D2D1_BITMAP_PROPERTIES* properties, ID2D1Bitmap** bitmap) {
    if (!bitmap) return E_POINTER;
    *bitmap = nullptr;
    const auto format = properties ? properties->pixelFormat : format_;
    ComPtr<Bitmap> created;
    const float dpi_x = properties ? properties->dpiX : dpi_, dpi_y = properties ? properties->dpiY : dpi_;
    const HRESULT result = (format.format == DXGI_FORMAT_BC1_UNORM || format.format == DXGI_FORMAT_BC3_UNORM) ?
        Bitmap::createCompressed(size, data, pitch, format, dpi_x, dpi_y, gpu_, created, impeller_) :
        Bitmap::create(size, data, pitch, format, dpi_x, dpi_y, created);
    if (SUCCEEDED(result)) *bitmap = created.Detach();
    return result;
}

HRESULT Canvas::createBitmap1(D2D1_SIZE_U size, const void* data, UINT32 pitch, const D2D1_BITMAP_PROPERTIES1* properties, ID2D1Bitmap1** bitmap) {
    if (!bitmap) return E_POINTER;
    *bitmap = nullptr;
    if (!properties) return E_INVALIDARG;
    ComPtr<Bitmap> created;
    // Compressed artwork into native GPU textures (BC1 on Skia, BC1/BC3 on Impeller).
    const HRESULT result = (properties->pixelFormat.format == DXGI_FORMAT_BC1_UNORM || properties->pixelFormat.format == DXGI_FORMAT_BC3_UNORM) ?
        Bitmap::createCompressed(size, data, pitch, properties->pixelFormat, properties->dpiX, properties->dpiY, gpu_, created, impeller_) :
        Bitmap::create(size, data, pitch, properties->pixelFormat, properties->dpiX, properties->dpiY, created);
    if (SUCCEEDED(result)) *bitmap = created.Detach();
    return result;
}

// Without a pixel size the target has the parent's DPI; with both sizes, the DPI they imply.
HRESULT Canvas::createCompatibleRenderTarget(const D2D1_SIZE_F* size, const D2D1_SIZE_U* pixels, const D2D1_PIXEL_FORMAT* format,
                                             ID2D1BitmapRenderTarget** target) {
    if (!target) return E_POINTER;
    *target = nullptr;
    D2D1_SIZE_U extent = pixelSize();
    float dpi = dpi_;
    if (pixels) {
        extent = *pixels;
        if (size && size->width > 0) dpi = static_cast<float>(pixels->width / size->width * 96.0);
    } else if (size) {
        extent = {static_cast<UINT32>(std::ceil(std::max(0.0f, size->width) * dpi_ / 96)), static_cast<UINT32>(std::ceil(std::max(0.0f, size->height) * dpi_ / 96))};
    }
    D2D1_PIXEL_FORMAT wanted = format ? *format : D2D1_PIXEL_FORMAT{format_.format, D2D1_ALPHA_MODE_PREMULTIPLIED};
    if (wanted.format == DXGI_FORMAT_UNKNOWN) wanted.format = format_.format;
    wanted = resolveFormat(wanted);
    if (wanted.format == DXGI_FORMAT_A8_UNORM) return D2DERR_UNSUPPORTED_PIXEL_FORMAT; // Targets draw in colour.
    ComPtr<Bitmap> bitmap;
    const HRESULT result = Bitmap::create(extent, nullptr, 0, wanted, dpi, dpi, bitmap);
    return FAILED(result) ? result : createBitmapTarget(std::move(bitmap), gpu_, target);
}

HRESULT Canvas::createLayer(ID2D1Layer** layer) {
    if (!layer) return E_POINTER;
    *layer = new LayerResource;
    return S_OK;
}

HRESULT Canvas::createFilledRealization(ID2D1Geometry* geometry, ID2D1GeometryRealization** realization) {
    if (!realization) return E_POINTER;
    *realization = nullptr;
    if (!Geometry::from(geometry)) return E_INVALIDARG;
    *realization = new Realization(ComPtr<ID2D1Geometry>(geometry), true, 0, nullptr);
    return S_OK;
}

HRESULT Canvas::createStrokedRealization(ID2D1Geometry* geometry, FLOAT width, ID2D1StrokeStyle* style, ID2D1GeometryRealization** realization) {
    if (!realization) return E_POINTER;
    *realization = nullptr;
    if (!Geometry::from(geometry)) return E_INVALIDARG;
    *realization = new Realization(ComPtr<ID2D1Geometry>(geometry), false, width, ComPtr<ID2D1StrokeStyle>(style));
    return S_OK;
}

// ---- Primitives ---------------------------------------------------------------------------

void Canvas::drawLine(D2D1_POINT_2F from, D2D1_POINT_2F to, ID2D1Brush* brush, FLOAT width, ID2D1StrokeStyle* style) {
    if (!(width > 0)) return;
    SkPaint paint;
    SkCanvas* canvas = brushPaint(brush, paint) ? prepare() : nullptr;
    if (!canvas) return;
    if (const auto* own = StrokeStyle::from(style)) own->apply(paint, width);
    else applyDefaultStroke(paint, width);
    canvas->drawLine(from.x, from.y, to.x, to.y, paint);
}

void Canvas::drawRectangle(const D2D1_RECT_F* rect, ID2D1Brush* brush, FLOAT width, ID2D1StrokeStyle* style) {
    if (!rect || !(width > 0)) return;
    SkPaint paint;
    SkCanvas* canvas = brushPaint(brush, paint) ? prepare() : nullptr;
    if (!canvas) return;
    if (const auto* own = StrokeStyle::from(style)) own->apply(paint, width);
    else applyDefaultStroke(paint, width);
    canvas->drawRect(toSk(*rect).makeSorted(), paint);
}

void Canvas::fillRectangle(const D2D1_RECT_F* rect, ID2D1Brush* brush) {
    SkPaint paint;
    SkCanvas* canvas = rect && brushPaint(brush, paint) ? prepare() : nullptr;
    if (canvas) canvas->drawRect(toSk(*rect).makeSorted(), paint);
}

void Canvas::drawRoundedRectangle(const D2D1_ROUNDED_RECT* rounded, ID2D1Brush* brush, FLOAT width, ID2D1StrokeStyle* style) {
    if (rounded) strokePath(roundedRectanglePath(*rounded), brush, width, style);
}

void Canvas::fillRoundedRectangle(const D2D1_ROUNDED_RECT* rounded, ID2D1Brush* brush) {
    if (!rounded) return;
    SkPaint paint;
    SkCanvas* canvas = brushPaint(brush, paint) ? prepare() : nullptr;
    if (!canvas) return;
    const SkRect rect = toSk(rounded->rect).makeSorted();
    const float rx = std::min(std::abs(rounded->radiusX), rect.width() * 0.5f), ry = std::min(std::abs(rounded->radiusY), rect.height() * 0.5f);
    if (rx <= 0 || ry <= 0) canvas->drawRect(rect, paint);
    else canvas->drawRRect(SkRRect::MakeRectXY(rect, rx, ry), paint);
}

void Canvas::drawEllipse(const D2D1_ELLIPSE* ellipse, ID2D1Brush* brush, FLOAT width, ID2D1StrokeStyle* style) {
    if (ellipse) strokePath(ellipsePath(*ellipse), brush, width, style);
}

void Canvas::fillEllipse(const D2D1_ELLIPSE* ellipse, ID2D1Brush* brush) {
    if (!ellipse) return;
    SkPaint paint;
    SkCanvas* canvas = brushPaint(brush, paint) ? prepare() : nullptr;
    if (!canvas) return;
    const float rx = std::abs(ellipse->radiusX), ry = std::abs(ellipse->radiusY);
    canvas->drawOval(SkRect::MakeLTRB(ellipse->point.x - rx, ellipse->point.y - ry, ellipse->point.x + rx, ellipse->point.y + ry), paint);
}

void Canvas::drawGeometry(ID2D1Geometry* geometry, ID2D1Brush* brush, FLOAT width, ID2D1StrokeStyle* style) {
    if (const auto* shape = Geometry::from(geometry)) strokePath(shape->outline(), brush, width, style);
}

void Canvas::fillGeometry(ID2D1Geometry* geometry, ID2D1Brush* brush, ID2D1Brush* opacityBrush) {
    const auto* shape = Geometry::from(geometry);
    if (!shape) return;
    const auto* opacity = Brush::from(opacityBrush);
    if (!opacity) { fillPath(shape->area(), brush); return; }
    // The opacity brush's alpha masks the fill: the fill in a layer, kept where the brush shows.
    SkPaint paint;
    SkCanvas* canvas = brushPaint(brush, paint) ? prepare() : nullptr;
    if (!canvas) return;
    const int restore_to = canvas->getSaveCount();
    const SkRect bounds = shape->area().getBounds();
    canvas->saveLayer(&bounds, nullptr);
    canvas->drawPath(shape->area(), paint);
    SkPaint mask;
    opacity->paint(mask, gpu_, 1);
    mask.setBlendMode(SkBlendMode::kDstIn);
    canvas->drawRect(bounds, mask);
    canvas->restoreToCount(restore_to);
}

void Canvas::drawGeometryRealization(ID2D1GeometryRealization* realization, ID2D1Brush* brush) {
    const auto* own = dynamic_cast<Realization*>(realization);
    if (!own) return;
    if (own->filled) fillGeometry(own->geometry.Get(), brush, nullptr);
    else drawGeometry(own->geometry.Get(), brush, own->width, own->style.Get());
}

// The brush where the mask bitmap's alpha (its source rectangle stretched over the destination) shows.
void Canvas::fillOpacityMask(ID2D1Bitmap* mask, ID2D1Brush* brush, const D2D1_RECT_F* destination, const D2D1_RECT_F* source) {
    auto* bitmap = Bitmap::from(mask);
    SkPaint paint;
    SkCanvas* canvas = bitmap && brushPaint(brush, paint) ? prepare() : nullptr;
    sk_sp<SkImage> alpha = canvas ? bitmap->image(gpu_) : nullptr;
    if (!alpha) return;
    const auto size = bitmap->GetSize();
    const D2D1_RECT_F whole{0, 0, size.width, size.height};
    const SkRect to = toSk(destination ? *destination : whole);
    const D2D1_RECT_F from = source ? *source : whole;
    const SkRect from_pixels = SkRect::MakeLTRB(from.left * bitmap->scaleX(), from.top * bitmap->scaleY(), from.right * bitmap->scaleX(),
                                                from.bottom * bitmap->scaleY());
    if (to.isEmpty() || from_pixels.isEmpty()) return;
    const SkMatrix local = SkMatrix::RectToRectOrIdentity(from_pixels, to);
    paint.setMaskFilter(SkShaderMaskFilter::Make(alpha->makeShader(SkTileMode::kClamp, SkTileMode::kClamp, SkSamplingOptions(SkFilterMode::kLinear), &local)));
    canvas->drawRect(to, paint);
}

void Canvas::drawBitmap(ID2D1Bitmap* bitmap, const D2D1_RECT_F* destination, FLOAT opacity, D2D1_INTERPOLATION_MODE mode,
                        const D2D1_RECT_F* source, const D2D1_MATRIX_4X4_F* perspective) {
    auto* own = Bitmap::from(bitmap);
    if (!own || !(opacity > 0)) return;
    SkCanvas* canvas = prepare();
    sk_sp<SkImage> image = canvas ? own->image(gpu_) : nullptr;
    if (!image) return;
    const auto size = own->GetSize();
    const D2D1_RECT_F whole{0, 0, size.width, size.height};
    const SkRect to = toSk(destination ? *destination : whole);
    const D2D1_RECT_F from = source ? *source : whole;
    const SkRect from_pixels = SkRect::MakeLTRB(from.left * own->scaleX(), from.top * own->scaleY(), from.right * own->scaleX(),
                                                from.bottom * own->scaleY());
    if (to.isEmpty() || from_pixels.isEmpty()) return;
    if (perspective) canvas->concat(perspectiveOf(*perspective));
    // Reduced when the destination covers fewer device pixels than the source has.
    const SkRect shown = canvas->getTotalMatrix().mapRect(to);
    const bool reducing = shown.width() * shown.height() < from_pixels.width() * from_pixels.height() * 0.9f;
    SkPaint paint;
    paint.setAlphaf(std::min(opacity, 1.0f));
    paint.setAntiAlias(antialias_ != D2D1_ANTIALIAS_MODE_ALIASED);
    paint.setBlendMode(blendFor(blend_));
    const bool mipmapped = image->hasMipmaps() || !image->isTextureBacked();
    const bool part = from_pixels != SkRect::MakeIWH(image->width(), image->height());
    canvas->drawImageRect(image, from_pixels, to, samplingFor(mode, reducing, mipmapped), &paint,
                          part ? SkCanvas::kStrict_SrcRectConstraint : SkCanvas::kFast_SrcRectConstraint);
}

// Effects are computed at the resolution they are shown at (never above the image's own): the
// world transform's scale, rounded up to quarter steps of a power of two so a resized drawing
// reuses an effect's cached result.
void Canvas::drawImage(ID2D1Image* image, const D2D1_POINT_2F* offset, const D2D1_RECT_F* imageRectangle, D2D1_INTERPOLATION_MODE mode,
                       D2D1_COMPOSITE_MODE composite) {
    SkCanvas* canvas = image ? prepare() : nullptr;
    if (!canvas) return;
    double scale = 1;
    ImageData data;
    if (auto* bitmap = Bitmap::from(image)) {
        scale = bitmap->scaleX();
        data = {bitmap->image(gpu_), 0, 0};
    } else if (auto* source = ImageSource::from(image)) {
        const SkMatrix w = world();
        const double device = std::sqrt(std::abs(w.getScaleX() * w.getScaleY() - w.getSkewX() * w.getSkewY()));
        if (device < 1) scale = std::max(1.0 / 16, std::exp2(std::ceil(std::log2(std::max(device, 1e-6)) * 4) / 4));
        data = source->render(scale, gpu_);
    }
    if (!data.image) return;
    const D2D1_POINT_2F at = offset ? *offset : D2D1_POINT_2F{0, 0};
    const int restore_to = canvas->save(); // The clip stays for this draw only.
    if (imageRectangle) {
        canvas->translate(at.x - imageRectangle->left, at.y - imageRectangle->top);
        canvas->clipRect(toSk(*imageRectangle), antialias_ != D2D1_ANTIALIAS_MODE_ALIASED);
    } else {
        canvas->translate(at.x, at.y);
    }
    canvas->scale(static_cast<float>(1 / scale), static_cast<float>(1 / scale));
    SkPaint paint;
    paint.setBlendMode(blendFor(composite));
    paint.setAntiAlias(antialias_ != D2D1_ANTIALIAS_MODE_ALIASED);
    canvas->drawImage(data.image, static_cast<float>(data.x), static_cast<float>(data.y), samplingFor(mode, false, false), &paint);
    canvas->restoreToCount(restore_to);
}

// ---- Clips and layers ---------------------------------------------------------------------

// The rectangle's device-space bounding box (a rotated rectangle clips to its bounds).
void Canvas::pushAxisAlignedClip(const D2D1_RECT_F* rect, D2D1_ANTIALIAS_MODE mode) {
    stack_.push_back({true, canvas_ ? canvas_->getSaveCount() : 1, world(), nullptr});
    if (!canvas_) return;
    canvas_->save();
    if (!rect) return;
    const SkRect device = world().mapRect(toSk(*rect).makeSorted());
    canvas_->resetMatrix();
    canvas_->clipRect(device, mode != D2D1_ANTIALIAS_MODE_ALIASED);
}

void Canvas::popAxisAlignedClip() {
    if (stack_.empty() || !stack_.back().clip) { unbalanced_ = true; return; }
    const int restore_to = stack_.back().restore_to;
    stack_.pop_back();
    if (canvas_) canvas_->restoreToCount(restore_to);
}

void Canvas::pushLayer(const D2D1_LAYER_PARAMETERS1& parameters) {
    const SkMatrix at = world();
    stack_.push_back({false, canvas_ ? canvas_->getSaveCount() : 1, at, ComPtr<ID2D1Brush>(parameters.opacityBrush)});
    if (!canvas_) return;
    canvas_->save();
    canvas_->setMatrix(at);
    if (!unbounded(parameters.contentBounds)) canvas_->clipRect(toSk(parameters.contentBounds).makeSorted(), true);
    if (const auto* mask = Geometry::from(parameters.geometricMask)) {
        const SkMatrix mask_transform = toSk(parameters.maskTransform);
        if (!invertible(mask_transform)) canvas_->clipRect(SkRect::MakeEmpty()); // A collapsed mask shows nothing.
        else canvas_->clipPath(mask->area().makeTransform(mask_transform), parameters.maskAntialiasMode != D2D1_ANTIALIAS_MODE_ALIASED);
    }
    const float opacity = std::clamp(parameters.opacity, 0.0f, 1.0f);
    if (opacity < 1 || stack_.back().opacity_brush) {
        SkPaint paint;
        paint.setAlphaf(opacity);
        canvas_->saveLayer(nullptr, opacity < 1 ? &paint : nullptr);
    }
}

void Canvas::popLayer() {
    if (stack_.empty() || stack_.back().clip) { unbalanced_ = true; return; }
    const Layer layer = std::move(stack_.back());
    stack_.pop_back();
    if (!canvas_) return;
    // The opacity brush (in the space the layer was pushed in) keeps the layer where it shows.
    if (const auto* brush = Brush::from(layer.opacity_brush.Get()); brush && invertible(layer.world)) {
        canvas_->setMatrix(layer.world);
        SkPaint mask;
        brush->paint(mask, gpu_, 1);
        mask.setBlendMode(SkBlendMode::kDstIn);
        canvas_->drawPaint(mask);
    }
    canvas_->restoreToCount(layer.restore_to);
}

void Canvas::unwind() {
    if (canvas_) canvas_->restoreToCount(1);
    stack_.clear();
}

// Ignores the transform; the clips and the current layer bound it. Targets without alpha keep
// their pixels opaque: the colour is laid over black.
void Canvas::clear(const D2D1_COLOR_F* color) {
    if (!canvas_) return;
    D2D1_COLOR_F c = color ? *color : D2D1_COLOR_F{0, 0, 0, 0};
    c.r = std::clamp(c.r, 0.0f, 1.0f); c.g = std::clamp(c.g, 0.0f, 1.0f); c.b = std::clamp(c.b, 0.0f, 1.0f); c.a = std::clamp(c.a, 0.0f, 1.0f);
    if (format_.alphaMode == D2D1_ALPHA_MODE_IGNORE) canvas_->clear(SkColor4f{c.r * c.a, c.g * c.a, c.b * c.a, 1});
    else canvas_->clear(SkColor4f{c.r, c.g, c.b, c.a});
}

void Canvas::beginDraw() {
    unbalanced_ = false;
    changed();
    prepareFrame();
    if (canvas_) {
        canvas_->restoreToCount(1);
        canvas_->resetMatrix();
    }
    stack_.clear();
}

HRESULT Canvas::endDraw() {
    const bool balanced = stack_.empty() && !unbalanced_;
    unwind();
    if (!canvas_) return D2DERR_RECREATE_TARGET;
    changed();
    return balanced ? S_OK : D2DERR_PUSH_POP_UNBALANCED;
}

void Canvas::getDpi(FLOAT* dpiX, FLOAT* dpiY) const {
    if (dpiX) *dpiX = dpi_;
    if (dpiY) *dpiY = dpi_;
}

D2D1_SIZE_F Canvas::size() const {
    return {static_cast<float>(width_ * 96.0 / dpi_), static_cast<float>(height_ * 96.0 / dpi_)};
}

// ---- Targets ------------------------------------------------------------------------------

namespace {
// A window's frames: the window layer's surface for each one, shown at EndDraw.
class HwndTarget final : public RenderTarget<ID2D1HwndRenderTarget, ID2D1DeviceContext1> {
public:
    HwndTarget(HWND window, D2D1_PIXEL_FORMAT format, float dpi) : RenderTarget(format, dpi), window_(window) {}
    HRESULT STDMETHODCALLTYPE Resize(const D2D1_SIZE_U* size) override {
        if (!size) return E_INVALIDARG;
        setSize(static_cast<int>(std::max(1u, size->width)), static_cast<int>(std::max(1u, size->height)));
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE EndDraw(D2D1_TAG* tag1, D2D1_TAG* tag2) override {
        const HRESULT result = RenderTarget::EndDraw(tag1, tag2);
        if (FAILED(result)) return result;
        return lm_gfx::presentWindow(window_) ? result : D2DERR_RECREATE_TARGET;
    }
protected:
    void prepareFrame() override {
        const D2D1_SIZE_U size = pixelSize();
        const int width = static_cast<int>(size.width), height = static_cast<int>(size.height);
        // Detach before beginFrame replaces the previous command canvas.
        attach(sk_sp<SkSurface>{}, width, height);
        if (auto commands = lm_gfx::windowCanvas(window_, width, height)) attachCommands(std::move(commands), width, height);
        else attach(lm_gfx::windowSurface(window_, width, height), width, height);
    }
private:
    HWND window_;
};

// CreateCompatibleRenderTarget: draws into its bitmap's own pixels (GetBitmap shares them).
class BitmapTarget final : public RenderTarget<ID2D1BitmapRenderTarget, ID2D1DeviceContext1> {
public:
    BitmapTarget(ComPtr<Bitmap> bitmap, GrDirectContext* gpu)
        : RenderTarget(bitmap->GetPixelFormat(), static_cast<float>(bitmap->scaleX() * 96)), bitmap_(std::move(bitmap)), parent_gpu_(gpu) {
        setSize(bitmap_->width(), bitmap_->height());
    }
    HRESULT STDMETHODCALLTYPE GetBitmap(ID2D1Bitmap** bitmap) override {
        if (!bitmap) return E_POINTER;
        *bitmap = bitmap_.Get();
        bitmap_->AddRef();
        return S_OK;
    }
protected:
    void changed() override { bitmap_->touch(); }
    // The bitmap's surface, made at the first BeginDraw (on the parent's GPU, else in memory).
    void prepareFrame() override { attach(bitmap_->surface(parent_gpu_), bitmap_->width(), bitmap_->height()); }
private:
    ComPtr<Bitmap> bitmap_;
    GrDirectContext* parent_gpu_;
};
} // namespace

HRESULT createBitmapTarget(ComPtr<Bitmap> bitmap, GrDirectContext* gpu, ID2D1BitmapRenderTarget** target) {
    if (!target) return E_POINTER;
    *target = nullptr;
    if (!bitmap || bitmap->alphaOnly()) return E_INVALIDARG;
    if (gpu != lm_gfx::gpu()) gpu = nullptr; // Another thread: in memory.
    *target = new BitmapTarget(std::move(bitmap), gpu);
    return S_OK;
}

HRESULT createHwndTarget(const D2D1_RENDER_TARGET_PROPERTIES& properties, const D2D1_HWND_RENDER_TARGET_PROPERTIES& window,
                         ID2D1HwndRenderTarget** target) {
    if (!target) return E_POINTER;
    *target = nullptr;
    D2D1_PIXEL_FORMAT format = properties.pixelFormat;
    if (format.format == DXGI_FORMAT_UNKNOWN) format.format = DXGI_FORMAT_B8G8R8A8_UNORM;
    if (format.alphaMode == D2D1_ALPHA_MODE_UNKNOWN) format.alphaMode = D2D1_ALPHA_MODE_IGNORE; // A window shows no alpha.
    if (format.format != DXGI_FORMAT_B8G8R8A8_UNORM || format.alphaMode == D2D1_ALPHA_MODE_STRAIGHT) return D2DERR_UNSUPPORTED_PIXEL_FORMAT;
    D2D1_SIZE_U size = window.pixelSize;
    if (!size.width || !size.height) lm_gfx::windowPixelSize(window.hwnd, size.width, size.height);
    ComPtr<HwndTarget> created;
    created.Attach(new HwndTarget(window.hwnd, format, properties.dpiX));
    const HRESULT result = created->Resize(&size);
    if (SUCCEEDED(result)) *target = created.Detach();
    return result;
}
} // namespace lm_d2d
