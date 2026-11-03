// The Direct2D factory: geometries, stroke styles and render targets.
#include "d2d_internal.hpp"

#include <algorithm>
#include <numeric>

namespace lm_d2d {
namespace {
ImpellerStrokeCap capFor(D2D1_CAP_STYLE cap) {
    switch (cap) {
    case D2D1_CAP_STYLE_SQUARE: return kImpellerStrokeCapSquare;
    case D2D1_CAP_STYLE_ROUND:
    case D2D1_CAP_STYLE_TRIANGLE: return kImpellerStrokeCapRound; // There is no pointed cap; round reaches as far.
    default: return kImpellerStrokeCapButt;
    }
}

class Factory final : public lm_com::Object<ID2D1Factory> {
public:
    HRESULT STDMETHODCALLTYPE CreateRectangleGeometry(const D2D1_RECT_F* rect, ID2D1RectangleGeometry** geometry) override {
        return rect ? createRectangleGeometry(*rect, geometry) : E_INVALIDARG;
    }
    HRESULT STDMETHODCALLTYPE CreateRoundedRectangleGeometry(const D2D1_ROUNDED_RECT* rounded, ID2D1RoundedRectangleGeometry** geometry) override {
        return rounded ? createRoundedRectangleGeometry(*rounded, geometry) : E_INVALIDARG;
    }
    HRESULT STDMETHODCALLTYPE CreateEllipseGeometry(const D2D1_ELLIPSE* ellipse, ID2D1EllipseGeometry** geometry) override {
        return ellipse ? createEllipseGeometry(*ellipse, geometry) : E_INVALIDARG;
    }
    HRESULT STDMETHODCALLTYPE CreateGeometryGroup(D2D1_FILL_MODE mode, ID2D1Geometry** geometries, UINT32 count, ID2D1GeometryGroup** group) override {
        return createGeometryGroup(mode, geometries, count, group);
    }
    HRESULT STDMETHODCALLTYPE CreateTransformedGeometry(ID2D1Geometry* source, const D2D1_MATRIX_3X2_F* transform,
                                                        ID2D1TransformedGeometry** geometry) override {
        return transform ? createTransformedGeometry(source, *transform, geometry) : E_INVALIDARG;
    }
    HRESULT STDMETHODCALLTYPE CreatePathGeometry(ID2D1PathGeometry** geometry) override { return createPathGeometry(geometry); }
    HRESULT STDMETHODCALLTYPE CreateStrokeStyle(const D2D1_STROKE_STYLE_PROPERTIES* properties, const FLOAT* dashes, UINT32 count,
                                                ID2D1StrokeStyle** style) override {
        if (!style) return E_POINTER;
        *style = nullptr;
        if (!properties || (properties->dashStyle == D2D1_DASH_STYLE_CUSTOM && (!dashes || !count))) return E_INVALIDARG;
        *style = new StrokeStyle(*properties, dashes, count);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE CreateWicBitmapRenderTarget(IWICBitmap* target, const D2D1_RENDER_TARGET_PROPERTIES* properties,
                                                          ID2D1RenderTarget** renderTarget) override {
        return properties ? createWicTarget(target, *properties, renderTarget) : E_INVALIDARG;
    }
    HRESULT STDMETHODCALLTYPE CreateHwndRenderTarget(const D2D1_RENDER_TARGET_PROPERTIES* properties,
                                                     const D2D1_HWND_RENDER_TARGET_PROPERTIES* window,
                                                     ID2D1HwndRenderTarget** target) override {
        return properties && window ? createHwndTarget(*properties, *window, target) : E_INVALIDARG;
    }
    // There are no GDI device contexts to draw into.
    HRESULT STDMETHODCALLTYPE CreateDCRenderTarget(const D2D1_RENDER_TARGET_PROPERTIES*, ID2D1DCRenderTarget** target) override {
        if (target) *target = nullptr;
        return E_NOTIMPL;
    }
};
} // namespace

StrokeStyle::StrokeStyle(const D2D1_STROKE_STYLE_PROPERTIES& properties, const FLOAT* dashes, UINT32 count) : properties_(properties) {
    switch (properties.dashStyle) {
    case D2D1_DASH_STYLE_DASH: dashes_ = {2, 2}; break;
    case D2D1_DASH_STYLE_DOT: dashes_ = {0, 2}; break;
    case D2D1_DASH_STYLE_DASH_DOT: dashes_ = {2, 2, 0, 2}; break;
    case D2D1_DASH_STYLE_DASH_DOT_DOT: dashes_ = {2, 2, 0, 2, 0, 2}; break;
    case D2D1_DASH_STYLE_CUSTOM: dashes_.assign(dashes, dashes + count); break;
    default: break;
    }
    // Negative or all-zero dashes: solid. An odd list repeats (dash and gap swap on the second pass).
    if (std::any_of(dashes_.begin(), dashes_.end(), [](float dash) { return dash < 0; }) ||
        std::accumulate(dashes_.begin(), dashes_.end(), 0.0f) <= 0)
        dashes_.clear();
    if (dashes_.size() % 2) { const auto copy = dashes_; dashes_.insert(dashes_.end(), copy.begin(), copy.end()); }
}

const StrokeStyle* StrokeStyle::from(ID2D1StrokeStyle* style) { return dynamic_cast<const StrokeStyle*>(style); }

// Direct2D's miter limit is in half stroke widths, the stroker's in stroke widths.
void StrokeStyle::apply(gx::Paint& paint, float width) const {
    const bool dashed = !dashes_.empty();
    paint.stroke = true;
    paint.width = width;
    paint.cap = capFor(dashed ? properties_.dashCap : properties_.startCap);
    switch (properties_.lineJoin) {
    case D2D1_LINE_JOIN_BEVEL: paint.join = kImpellerStrokeJoinBevel; break;
    case D2D1_LINE_JOIN_ROUND: paint.join = kImpellerStrokeJoinRound; break;
    default: paint.join = kImpellerStrokeJoinMiter; break;
    }
    paint.miter = std::max(1.0f, properties_.miterLimit) * 0.5f;
    paint.dashes.clear();
    if (!dashed || width <= 0) return;
    // Zero-length dashes are dots with round or square dash caps: a hair-length dash, taken from
    // the gap after it, so the stroker draws them.
    for (const float dash : dashes_) paint.dashes.push_back(dash * width);
    const float hair = width * 0.01f;
    for (size_t i = 0; i + 1 < paint.dashes.size() && properties_.dashCap != D2D1_CAP_STYLE_FLAT; i += 2)
        if (paint.dashes[i] == 0) { paint.dashes[i] = hair; paint.dashes[i + 1] = std::max(0.0f, paint.dashes[i + 1] - hair); }
    paint.dash_phase = properties_.dashOffset * width;
}

void applyDefaultStroke(gx::Paint& paint, float width) {
    paint.stroke = true;
    paint.width = width;
    paint.cap = kImpellerStrokeCapButt;
    paint.join = kImpellerStrokeJoinMiter;
    paint.miter = 5; // Direct2D's 10 half widths.
    paint.dashes.clear();
}
} // namespace lm_d2d

HRESULT WINAPI D2D1CreateFactory(D2D1_FACTORY_TYPE, REFIID riid, const D2D1_FACTORY_OPTIONS*, void** factory) {
    if (!factory) return E_POINTER;
    *factory = nullptr;
    lm_d2d::ComPtr<ID2D1Factory> created;
    created.Attach(new lm_d2d::Factory);
    return created->QueryInterface(riid, factory);
}
