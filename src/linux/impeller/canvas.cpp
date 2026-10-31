// Records actual GPU primitives into Impeller. Skia remains the CPU geometry/text helper;
// unusual paints are rasterized as local images, never as a full-frame presentation backend.
#include "internal.hpp"
#include "include/core/SkM44.h"
#include "include/core/SkPath.h"
#include "include/core/SkPathUtils.h"
#include "include/core/SkPathEffect.h"
#include "include/core/SkRRect.h"
#include "include/core/SkSurface.h"
#include "include/core/SkShader.h"
#include <algorithm>
#include <cmath>

namespace lm_impeller {
Canvas::Canvas(int width, int height) : SkCanvas(width, height), width_(width), height_(height) {
    const ImpellerRect bounds{0, 0, static_cast<float>(width), static_cast<float>(height)};
    builder_.reset(ImpellerDisplayListBuilderNew(&bounds));
    healthy_ = builder_ != nullptr;
}
Canvas::~Canvas() { restoreToCount(1); }
void Canvas::matrix() {
    if (!builder_) return;
    const auto transform = nativeMatrix(getLocalToDeviceAs3x3());
    ImpellerDisplayListBuilderSetTransform(builder_.get(), &transform);
}
void Canvas::didSetM44(const SkM44&) { matrix(); }
void Canvas::didConcat44(const SkM44&) { matrix(); }
void Canvas::didTranslate(SkScalar, SkScalar) { matrix(); }
void Canvas::didScale(SkScalar, SkScalar) { matrix(); }
void Canvas::willSave() { if (builder_) ImpellerDisplayListBuilderSave(builder_.get()); }
void Canvas::willRestore() { if (builder_) ImpellerDisplayListBuilderRestore(builder_.get()); }

SkCanvas::SaveLayerStrategy Canvas::getSaveLayerStrategy(const SaveLayerRec& rec) {
    (void)SkCanvas::getSaveLayerStrategy(rec);
    Paint paint;
    if (rec.fPaint) paint = nativePaint(*rec.fPaint);
    if ((rec.fPaint && !paint) || rec.fBackdrop) healthy_ = false;
    SkRect bounds;
    if (rec.fBounds) bounds = *rec.fBounds;
    else if (!getLocalClipBounds(&bounds)) bounds = SkRect::MakeEmpty();
    const auto box = nativeRect(bounds);
    if (builder_) ImpellerDisplayListBuilderSaveLayer(builder_.get(), &box, paint.get(), nullptr);
    return kNoLayer_SaveLayerStrategy;
}

void Canvas::onClipRect(const SkRect& rect, SkClipOp op, ClipEdgeStyle edge) {
    SkCanvas::onClipRect(rect, op, edge);
    const auto box = nativeRect(rect);
    if (builder_) ImpellerDisplayListBuilderClipRect(builder_.get(), &box, static_cast<ImpellerClipOperation>(op));
}
void Canvas::onClipRRect(const SkRRect& rect, SkClipOp op, ClipEdgeStyle edge) {
    SkCanvas::onClipRRect(rect, op, edge);
    auto path = nativePath(SkPath::RRect(rect));
    if (builder_ && path) ImpellerDisplayListBuilderClipPath(builder_.get(), path.get(), static_cast<ImpellerClipOperation>(op));
    else healthy_ = false;
}
void Canvas::onClipPath(const SkPath& shape, SkClipOp op, ClipEdgeStyle edge) {
    SkCanvas::onClipPath(shape, op, edge);
    auto path = nativePath(shape);
    if (builder_ && path) ImpellerDisplayListBuilderClipPath(builder_.get(), path.get(), static_cast<ImpellerClipOperation>(op));
    else healthy_ = false;
}

void Canvas::onDrawPaint(const SkPaint& paint) {
    auto native = nativePaint(paint);
    if (native) ImpellerDisplayListBuilderDrawPaint(builder_.get(), native.get());
    else {
        SkRect bounds;
        if (getLocalClipBounds(&bounds)) rasterPath(SkPath::Rect(bounds), paint);
    }
}
void Canvas::onDrawRect(const SkRect& rect, const SkPaint& paint) {
    auto native = nativePaint(paint);
    const auto box = nativeRect(rect);
    if (native) ImpellerDisplayListBuilderDrawRect(builder_.get(), &box, native.get());
    else onDrawPath(SkPath::Rect(rect), paint);
}
void Canvas::onDrawRRect(const SkRRect& rect, const SkPaint& paint) { onDrawPath(SkPath::RRect(rect), paint); }
void Canvas::onDrawOval(const SkRect& rect, const SkPaint& paint) {
    auto native = nativePaint(paint);
    const auto box = nativeRect(rect);
    if (native) ImpellerDisplayListBuilderDrawOval(builder_.get(), &box, native.get());
    else onDrawPath(SkPath::Oval(rect), paint);
}
void Canvas::onDrawPath(const SkPath& path, const SkPaint& paint) {
    if (paint.getPathEffect()) {
        bool is_fill = false;
        const SkPath filled = skpathutils::FillPathWithPaint(path, paint, &is_fill);
        if (is_fill) {
            SkPaint clean = paint;
            clean.setPathEffect(nullptr);
            clean.setStyle(SkPaint::kFill_Style);
            onDrawPath(filled, clean);
            return;
        }
    }
    auto native = nativePaint(paint);
    if (!native || path.isInverseFillType()) { rasterPath(path, paint); return; }
    auto shape = nativePath(path);
    if (!shape) { healthy_ = false; return; }
    ImpellerDisplayListBuilderDrawPath(builder_.get(), shape.get(), native.get());
}
void Canvas::onDrawPoints(PointMode mode, size_t count, const SkPoint points[], const SkPaint& paint) {
    if (!count) return;
    const size_t step = mode == kLines_PointMode ? 2 : 1;
    auto native = nativePaint(paint);
    if (mode == kPoints_PointMode) {
        for (size_t i = 0; i < count; ++i) {
            const float radius = std::max(0.5f, paint.getStrokeWidth() * 0.5f);
            SkPaint fill = paint; fill.setStyle(SkPaint::kFill_Style);
            onDrawOval(SkRect::MakeLTRB(points[i].x() - radius, points[i].y() - radius, points[i].x() + radius, points[i].y() + radius), fill);
        }
        return;
    }
    for (size_t i = 0; i + 1 < count; i += step) {
        const ImpellerPoint a{points[i].x(), points[i].y()}, b{points[i + 1].x(), points[i + 1].y()};
        if (native) ImpellerDisplayListBuilderDrawLine(builder_.get(), &a, &b, native.get());
        else {
            SkPaint stroke = paint; stroke.setStyle(SkPaint::kStroke_Style);
            onDrawPath(SkPath::Line(points[i], points[i + 1]), stroke);
        }
    }
}

void Canvas::drawTexture(const SkImage* image, const SkRect& src, const SkRect& dst, const SkSamplingOptions& sampling, const SkPaint& paint) {
    const auto texture = textureFor(image);
    auto native = nativePaint(paint);
    if (!texture || !native) { healthy_ = false; return; }
    const auto from = nativeRect(src), to = nativeRect(dst);
    ImpellerDisplayListBuilderDrawTextureRect(builder_.get(), texture, &from, &to,
        sampling.filter == SkFilterMode::kNearest ? kImpellerTextureSamplingNearestNeighbor : kImpellerTextureSamplingLinear, native.get());
}
void Canvas::onDrawImage2(const SkImage* image, SkScalar x, SkScalar y, const SkSamplingOptions& sampling, const SkPaint* paint) {
    if (!image) return;
    const auto src = SkRect::MakeIWH(image->width(), image->height());
    onDrawImageRect2(image, src, src.makeOffset(x, y), sampling, paint, kFast_SrcRectConstraint);
}
void Canvas::onDrawImageRect2(const SkImage* image, const SkRect& src, const SkRect& dst, const SkSamplingOptions& sampling,
                             const SkPaint* paint, SrcRectConstraint) {
    if (!image || src.isEmpty() || dst.isEmpty()) return;
    drawTexture(image, src, dst, sampling, paint ? *paint : SkPaint{});
}

void Canvas::rasterPath(const SkPath& path, const SkPaint& paint) {
    SkRect bounds = path.getBounds();
    if (path.isInverseFillType() && !getLocalClipBounds(&bounds)) return;
    if (paint.canComputeFastBounds()) paint.computeFastBounds(bounds, &bounds);
    const auto transform = getLocalToDeviceAs3x3();
    SkIRect pixels = transform.mapRect(bounds).makeOutset(2, 2).roundOut();
    if (!pixels.intersect(SkIRect::MakeWH(width_, height_))) return;
    if (static_cast<uint64_t>(pixels.width()) * pixels.height() > 16u * 1024u * 1024u) { healthy_ = false; return; }
    auto surface = SkSurfaces::Raster(SkImageInfo::MakeN32Premul(pixels.width(), pixels.height()));
    if (!surface) { healthy_ = false; return; }
    auto* canvas = surface->getCanvas();
    canvas->clear(SK_ColorTRANSPARENT);
    canvas->translate(-pixels.left(), -pixels.top());
    canvas->concat(transform);
    SkPaint raster = paint;
    raster.setBlendMode(SkBlendMode::kSrcOver);
    canvas->drawPath(path, raster);
    auto image = surface->makeImageSnapshot();
    SkPaint copy;
    copy.setBlendMode(paint.asBlendMode().value_or(SkBlendMode::kSrcOver));
    ImpellerDisplayListBuilderResetTransform(builder_.get());
    drawTexture(image.get(), SkRect::MakeIWH(image->width(), image->height()), SkRect::Make(pixels), SkSamplingOptions(SkFilterMode::kNearest), copy);
    matrix();
}

void Canvas::onDrawTextBlob(const SkTextBlob* blob, SkScalar x, SkScalar y, const SkPaint& paint) {
    if (!blob || blob->bounds().isEmpty()) return;
    auto& r = resources();
    const float scale = std::clamp(std::ceil(std::max(1.0f, getLocalToDeviceAs3x3().getMaxScale()) * 4) / 4, 1.0f, 8.0f);
    const bool cacheable = !paint.getShader();
    TextEntry* found = nullptr;
    if (cacheable) for (auto& entry : r.texts)
        if (entry.blob->uniqueID() == blob->uniqueID() && entry.scale == scale && entry.paint == paint) { found = &entry; break; }
    TextEntry temporary;
    if (!found) {
        SkRect bounds = blob->bounds().makeOutset(2 / scale, 2 / scale);
        if (paint.canComputeFastBounds()) paint.computeFastBounds(bounds, &bounds);
        const SkIRect pixels = SkMatrix::Scale(scale, scale).mapRect(bounds).roundOut();
        if (pixels.isEmpty()) return;
        if (pixels.width() > 16384 || pixels.height() > 16384 || static_cast<uint64_t>(pixels.width()) * pixels.height() > 16u * 1024u * 1024u) { healthy_ = false; return; }
        auto surface = SkSurfaces::Raster(SkImageInfo::MakeN32Premul(pixels.width(), pixels.height()));
        if (!surface) { healthy_ = false; return; }
        auto* canvas = surface->getCanvas();
        canvas->clear(SK_ColorTRANSPARENT);
        canvas->translate(-pixels.left(), -pixels.top());
        canvas->scale(scale, scale);
        // A gradient text brush is in the caller's coordinates rather than in blob space.
        SkPaint raster = paint;
        raster.setBlendMode(SkBlendMode::kSrcOver);
        if (auto shader = raster.refShader()) raster.setShader(shader->makeWithLocalMatrix(SkMatrix::Translate(-x, -y)));
        canvas->drawTextBlob(blob, 0, 0, raster);
        temporary = {sk_ref_sp(blob), paint, scale, SkRect::MakeLTRB(pixels.left() / scale, pixels.top() / scale,
            pixels.right() / scale, pixels.bottom() / scale), surface->makeImageSnapshot(), r.frame};
        if (cacheable) { r.texts.push_back(std::move(temporary)); found = &r.texts.back(); }
        else found = &temporary;
    }
    found->used = r.frame;
    SkPaint copy;
    copy.setBlendMode(paint.asBlendMode().value_or(SkBlendMode::kSrcOver));
    drawTexture(found->image.get(), SkRect::MakeIWH(found->image->width(), found->image->height()), found->bounds.makeOffset(x, y),
        SkSamplingOptions(SkFilterMode::kLinear), copy);
}

ImpellerDisplayList Canvas::finish() {
    if (!healthy_ || !builder_ || getSaveCount() != 1) return nullptr;
    finished_.reset(ImpellerDisplayListBuilderCreateDisplayListNew(builder_.get()));
    if (!finished_) { healthy_ = false; return nullptr; }
    const ImpellerRect bounds{0, 0, static_cast<float>(width_), static_cast<float>(height_)};
    builder_.reset(ImpellerDisplayListBuilderNew(&bounds));
    if (!builder_) { healthy_ = false; return nullptr; }
    ImpellerDisplayListBuilderDrawDisplayList(builder_.get(), finished_.get(), 1);
    matrix();
    return finished_.get();
}

sk_sp<SkImage> Canvas::snapshot() {
    const auto list = finish();
    if (!list) return nullptr;
    Texture texture(LaunchmeImpellerRenderToTextureNew(resources().context.get(), list, width_, height_));
    if (!texture) return nullptr;
    auto surface = SkSurfaces::Raster(SkImageInfo::Make(width_, height_, kRGBA_8888_SkColorType, kPremul_SkAlphaType));
    SkPixmap pixels;
    if (!surface || !surface->peekPixels(&pixels) || pixels.rowBytes() != static_cast<size_t>(width_) * 4) return nullptr;
    if (!LaunchmeImpellerTextureRead(resources().context.get(), texture.get(), static_cast<uint8_t*>(pixels.writable_addr()),
        static_cast<uint64_t>(width_) * height_ * 4)) return nullptr;
    surface->notifyContentWillChange(SkSurface::kRetain_ContentChangeMode);
    return surface->makeImageSnapshot();
}
sk_sp<SkImage> snapshot(SkCanvas* canvas) {
    auto* own = dynamic_cast<Canvas*>(canvas);
    return own && active() ? own->snapshot() : nullptr;
}
} // namespace lm_impeller
