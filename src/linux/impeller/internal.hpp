#pragma once
#include "backend.hpp"
#include <impeller.h>
#include <launchme_impeller.h>
#include "include/core/SkCanvas.h"
#include "include/core/SkImage.h"
#include "include/core/SkPaint.h"
#include "include/core/SkTextBlob.h"
#include <memory>
#include <thread>
#include <unordered_map>
#include <vector>

namespace lm_impeller {
template <class T, void (*Release)(T)> struct ReleaseHandle {
    void operator()(std::remove_pointer_t<T>* value) const { if (value) Release(value); }
};
template <class T, void (*Release)(T)>
using Handle = std::unique_ptr<std::remove_pointer_t<T>, ReleaseHandle<T, Release>>;
using Context = Handle<ImpellerContext, ImpellerContextRelease>;
using Texture = Handle<ImpellerTexture, ImpellerTextureRelease>;
using Surface = Handle<ImpellerSurface, ImpellerSurfaceRelease>;
using Swapchain = Handle<ImpellerVulkanSwapchain, ImpellerVulkanSwapchainRelease>;
using DisplayList = Handle<ImpellerDisplayList, ImpellerDisplayListRelease>;
using Builder = Handle<ImpellerDisplayListBuilder, ImpellerDisplayListBuilderRelease>;
using Paint = Handle<ImpellerPaint, ImpellerPaintRelease>;
using ColorSource = Handle<ImpellerColorSource, ImpellerColorSourceRelease>;
using Path = Handle<ImpellerPath, ImpellerPathRelease>;
using PathBuilder = Handle<ImpellerPathBuilder, ImpellerPathBuilderRelease>;

struct ImageEntry {
    sk_sp<const SkImage> image;
    Texture texture;
    size_t bytes = 0;
    uint64_t used = 0;
};
struct TextEntry {
    sk_sp<const SkTextBlob> blob;
    SkPaint paint;
    float scale = 1;
    SkRect bounds;
    sk_sp<SkImage> image;
    uint64_t used = 0;
};
struct Resources {
    Context context;
    std::thread::id thread;
    bool vulkan = false;
    uint64_t frame = 0;
    std::unordered_map<uint32_t, ImageEntry> images;
    std::vector<TextEntry> texts;
    void trim();
};
Resources& resources();
ImpellerTexture textureFor(const SkImage* image);
Paint nativePaint(const SkPaint& paint);
Path nativePath(const SkPath& path);
ImpellerMatrix nativeMatrix(const SkMatrix& matrix);
inline ImpellerRect nativeRect(const SkRect& r) { return {r.x(), r.y(), r.width(), r.height()}; }
inline ImpellerColor nativeColor(const SkColor4f& c) { return {c.fR, c.fG, c.fB, c.fA, kImpellerColorSpaceSRGB}; }

class Canvas final : public SkCanvas {
public:
    Canvas(int width, int height);
    ~Canvas() override;
    ImpellerDisplayList finish(); // Requires balanced saves; keeps a reusable list and permits further drawing.
    sk_sp<SkImage> snapshot();
    bool healthy() const { return healthy_; }
protected:
    void willSave() override;
    SaveLayerStrategy getSaveLayerStrategy(const SaveLayerRec&) override;
    void willRestore() override;
    void didSetM44(const SkM44&) override;
    void didConcat44(const SkM44&) override;
    void didTranslate(SkScalar, SkScalar) override;
    void didScale(SkScalar, SkScalar) override;
    void onClipRect(const SkRect&, SkClipOp, ClipEdgeStyle) override;
    void onClipRRect(const SkRRect&, SkClipOp, ClipEdgeStyle) override;
    void onClipPath(const SkPath&, SkClipOp, ClipEdgeStyle) override;
    void onDrawPaint(const SkPaint&) override;
    void onDrawRect(const SkRect&, const SkPaint&) override;
    void onDrawRRect(const SkRRect&, const SkPaint&) override;
    void onDrawOval(const SkRect&, const SkPaint&) override;
    void onDrawPath(const SkPath&, const SkPaint&) override;
    void onDrawPoints(PointMode, size_t, const SkPoint[], const SkPaint&) override;
    void onDrawImage2(const SkImage*, SkScalar, SkScalar, const SkSamplingOptions&, const SkPaint*) override;
    void onDrawImageRect2(const SkImage*, const SkRect&, const SkRect&, const SkSamplingOptions&, const SkPaint*, SrcRectConstraint) override;
    void onDrawTextBlob(const SkTextBlob*, SkScalar, SkScalar, const SkPaint&) override;
private:
    void matrix();
    void rasterPath(const SkPath&, const SkPaint&);
    void drawTexture(const SkImage*, const SkRect&, const SkRect&, const SkSamplingOptions&, const SkPaint&);
    Builder builder_;
    DisplayList finished_;
    int width_, height_;
    bool healthy_ = true;
};
} // namespace lm_impeller
