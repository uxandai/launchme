// Direct2D brushes as kernel paints. A brush lies in the render target's world space mapped by its
// own transform: the shader's local matrix is the brush transform, the canvas matrix the world
// transform. Shaders are kept until a property changes; the opacity is the paint's alpha.
#include "d2d_internal.hpp"

#include <algorithm>

namespace lm_d2d {
namespace {
ImpellerTileMode tileFor(D2D1_EXTEND_MODE mode) {
    switch (mode) {
    case D2D1_EXTEND_MODE_WRAP: return kImpellerTileModeRepeat;
    case D2D1_EXTEND_MODE_MIRROR: return kImpellerTileModeMirror;
    default: return kImpellerTileModeClamp;
    }
}

template <class Interface>
class BrushObject : public lm_com::Object<Interface>, public Brush {
public:
    void STDMETHODCALLTYPE SetOpacity(FLOAT opacity) override { opacity_ = opacity; }
    void STDMETHODCALLTYPE SetTransform(const D2D1_MATRIX_3X2_F* transform) override {
        if (transform) { transform_ = *transform; shader_.reset(); }
    }
    FLOAT STDMETHODCALLTYPE GetOpacity() const override { return opacity_; }
    void STDMETHODCALLTYPE GetTransform(D2D1_MATRIX_3X2_F* transform) const override { if (transform) *transform = transform_; }
protected:
    explicit BrushObject(const D2D1_BRUSH_PROPERTIES* properties) {
        if (properties) { opacity_ = properties->opacity; transform_ = properties->transform; }
    }
    float alpha(float extra) const { return std::clamp(opacity_ * extra, 0.0f, 1.0f); }
    float opacity_ = 1;
    D2D1_MATRIX_3X2_F transform_ = D2D1::IdentityMatrix();
    mutable gx::ShaderPtr shader_; // Reset by any setter that changes it.
};

class SolidColorBrush final : public BrushObject<ID2D1SolidColorBrush> {
public:
    SolidColorBrush(const D2D1_COLOR_F& color, const D2D1_BRUSH_PROPERTIES* properties) : BrushObject(properties), color_(color) {}
    void STDMETHODCALLTYPE SetColor(const D2D1_COLOR_F* color) override { if (color) color_ = *color; }
    D2D1_COLOR_F STDMETHODCALLTYPE GetColor() const override { return color_; }
    void paint(gx::Paint& paint, float extra) const override {
        paint.shader = nullptr;
        paint.color = {std::clamp(color_.r, 0.0f, 1.0f), std::clamp(color_.g, 0.0f, 1.0f), std::clamp(color_.b, 0.0f, 1.0f),
                       std::clamp(color_.a, 0.0f, 1.0f) * alpha(extra)};
    }
private:
    D2D1_COLOR_F color_;
};

class GradientStopCollection final : public lm_com::Object<ID2D1GradientStopCollection> {
public:
    GradientStopCollection(std::vector<D2D1_GRADIENT_STOP> stops, D2D1_EXTEND_MODE extend) : extend_(extend) {
        std::stable_sort(stops.begin(), stops.end(), [](const auto& a, const auto& b) { return a.position < b.position; });
        for (const auto& stop : stops) {
            colors_.push_back({std::clamp(stop.color.r, 0.0f, 1.0f), std::clamp(stop.color.g, 0.0f, 1.0f),
                               std::clamp(stop.color.b, 0.0f, 1.0f), std::clamp(stop.color.a, 0.0f, 1.0f)});
            positions_.push_back(std::clamp(stop.position, 0.0f, 1.0f));
        }
    }
    UINT32 STDMETHODCALLTYPE GetGradientStopCount() const override { return static_cast<UINT32>(colors_.size()); }
    // Interpolated in premultiplied colour, as Direct2D does (no dark fringe toward transparent stops).
    std::shared_ptr<gx::Shader> shader(gx::Shader::Kind kind) const {
        auto result = std::make_shared<gx::Shader>();
        result->kind = kind;
        result->colors = colors_;
        result->positions = positions_;
        result->premultiplied = true;
        result->tile_x = result->tile_y = tileFor(extend_);
        return result;
    }
    gx::Color last() const { return colors_.empty() ? gx::Color{} : colors_.back(); }
private:
    std::vector<gx::Color> colors_;
    std::vector<float> positions_;
    D2D1_EXTEND_MODE extend_;
};

// A gradient's shader, or its last colour when the gradient is degenerate.
void shaderPaint(gx::Paint& paint, const gx::ShaderPtr& shader, const GradientStopCollection& stops, float alpha) {
    if (shader) {
        paint.shader = shader;
        paint.color = {0, 0, 0, alpha};
    } else {
        gx::Color color = stops.last();
        color.a *= alpha;
        paint.shader = nullptr;
        paint.color = color;
    }
}

class LinearGradientBrush final : public BrushObject<ID2D1LinearGradientBrush> {
public:
    LinearGradientBrush(const D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES& gradient, const D2D1_BRUSH_PROPERTIES* properties,
                        ComPtr<GradientStopCollection> stops)
        : BrushObject(properties), gradient_(gradient), stops_(std::move(stops)) {}
    void STDMETHODCALLTYPE SetStartPoint(D2D1_POINT_2F point) override { gradient_.startPoint = point; shader_.reset(); }
    void STDMETHODCALLTYPE SetEndPoint(D2D1_POINT_2F point) override { gradient_.endPoint = point; shader_.reset(); }
    D2D1_POINT_2F STDMETHODCALLTYPE GetStartPoint() const override { return gradient_.startPoint; }
    D2D1_POINT_2F STDMETHODCALLTYPE GetEndPoint() const override { return gradient_.endPoint; }
    void paint(gx::Paint& paint, float extra) const override {
        if (!shader_) {
            const gx::Point a{gradient_.startPoint.x, gradient_.startPoint.y}, b{gradient_.endPoint.x, gradient_.endPoint.y};
            if (!(a == b)) {
                auto shader = stops_->shader(gx::Shader::Kind::linear);
                shader->p0 = a;
                shader->p1 = b;
                shader->local = toGx(transform_);
                shader_ = std::move(shader);
            }
        }
        shaderPaint(paint, shader_, *stops_.Get(), alpha(extra));
    }
private:
    D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES gradient_;
    ComPtr<GradientStopCollection> stops_;
};

// An elliptical gradient: a circular one of radius 1 in a space scaled by the radii around the
// centre; an origin offset is the focus of a two-point conical gradient.
class RadialGradientBrush final : public BrushObject<ID2D1RadialGradientBrush> {
public:
    RadialGradientBrush(const D2D1_RADIAL_GRADIENT_BRUSH_PROPERTIES& gradient, const D2D1_BRUSH_PROPERTIES* properties,
                        ComPtr<GradientStopCollection> stops)
        : BrushObject(properties), gradient_(gradient), stops_(std::move(stops)) {}
    void STDMETHODCALLTYPE SetCenter(D2D1_POINT_2F center) override { gradient_.center = center; shader_.reset(); }
    void STDMETHODCALLTYPE SetGradientOriginOffset(D2D1_POINT_2F offset) override { gradient_.gradientOriginOffset = offset; shader_.reset(); }
    void STDMETHODCALLTYPE SetRadiusX(FLOAT radius) override { gradient_.radiusX = radius; shader_.reset(); }
    void STDMETHODCALLTYPE SetRadiusY(FLOAT radius) override { gradient_.radiusY = radius; shader_.reset(); }
    D2D1_POINT_2F STDMETHODCALLTYPE GetCenter() const override { return gradient_.center; }
    D2D1_POINT_2F STDMETHODCALLTYPE GetGradientOriginOffset() const override { return gradient_.gradientOriginOffset; }
    FLOAT STDMETHODCALLTYPE GetRadiusX() const override { return gradient_.radiusX; }
    FLOAT STDMETHODCALLTYPE GetRadiusY() const override { return gradient_.radiusY; }
    void paint(gx::Paint& paint, float extra) const override {
        const float rx = std::abs(gradient_.radiusX), ry = std::abs(gradient_.radiusY);
        if (rx < 1e-6f || ry < 1e-6f) { paint.shader = nullptr; paint.color = {}; return; }
        if (!shader_) {
            gx::Matrix local = toGx(transform_);
            local.preTranslate(gradient_.center.x, gradient_.center.y);
            local.preScale(rx, ry);
            const gx::Point focus{gradient_.gradientOriginOffset.x / rx, gradient_.gradientOriginOffset.y / ry};
            std::shared_ptr<gx::Shader> shader;
            if (focus.x == 0 && focus.y == 0) {
                shader = stops_->shader(gx::Shader::Kind::radial);
                shader->p0 = {0, 0};
                shader->r0 = 1;
            } else {
                shader = stops_->shader(gx::Shader::Kind::conical);
                shader->p0 = focus;
                shader->r0 = 0;
                shader->p1 = {0, 0};
                shader->r1 = 1;
            }
            shader->local = local;
            shader_ = std::move(shader);
        }
        shaderPaint(paint, shader_, *stops_.Get(), alpha(extra));
    }
private:
    D2D1_RADIAL_GRADIENT_BRUSH_PROPERTIES gradient_;
    ComPtr<GradientStopCollection> stops_;
};

// The bitmap tiles its DIP space with an extend mode per axis.
class BitmapBrush final : public BrushObject<ID2D1BitmapBrush> {
public:
    BitmapBrush(ComPtr<Bitmap> bitmap, const D2D1_BITMAP_BRUSH_PROPERTIES* bitmapProperties, const D2D1_BRUSH_PROPERTIES* properties)
        : BrushObject(properties), bitmap_(std::move(bitmap)) {
        if (bitmapProperties) {
            extend_x_ = bitmapProperties->extendModeX;
            extend_y_ = bitmapProperties->extendModeY;
            interpolation_ = bitmapProperties->interpolationMode;
        }
    }
    void STDMETHODCALLTYPE SetExtendModeX(D2D1_EXTEND_MODE mode) override { extend_x_ = mode; }
    void STDMETHODCALLTYPE SetExtendModeY(D2D1_EXTEND_MODE mode) override { extend_y_ = mode; }
    void STDMETHODCALLTYPE SetInterpolationMode(D2D1_BITMAP_INTERPOLATION_MODE mode) override { interpolation_ = mode; }
    void STDMETHODCALLTYPE SetBitmap(ID2D1Bitmap* bitmap) override { bitmap_ = Bitmap::from(bitmap); }
    // Made per draw: the bitmap's pixels may have changed (a render target's).
    void paint(gx::Paint& paint, float extra) const override {
        std::shared_ptr<gx::Image> image = bitmap_ ? bitmap_->image() : nullptr;
        if (!image) { paint.shader = nullptr; paint.color = {}; return; }
        auto shader = std::make_shared<gx::Shader>();
        shader->kind = gx::Shader::Kind::image;
        shader->image = std::move(image);
        shader->local = toGx(transform_);
        shader->local.preScale(static_cast<float>(1 / bitmap_->scaleX()), static_cast<float>(1 / bitmap_->scaleY()));
        shader->tile_x = tileFor(extend_x_);
        shader->tile_y = tileFor(extend_y_);
        shader->nearest = interpolation_ == D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR;
        paint.shader = std::move(shader);
        paint.color = {0, 0, 0, alpha(extra)};
    }
private:
    ComPtr<Bitmap> bitmap_;
    D2D1_EXTEND_MODE extend_x_ = D2D1_EXTEND_MODE_CLAMP, extend_y_ = D2D1_EXTEND_MODE_CLAMP;
    D2D1_BITMAP_INTERPOLATION_MODE interpolation_ = D2D1_BITMAP_INTERPOLATION_MODE_LINEAR;
};

GradientStopCollection* stopsOf(ID2D1GradientStopCollection* collection) { return dynamic_cast<GradientStopCollection*>(collection); }

template <class Object, class Interface, class... Arguments>
HRESULT make(Interface** out, Arguments&&... arguments) {
    if (!out) return E_POINTER;
    *out = new Object(std::forward<Arguments>(arguments)...);
    return S_OK;
}
} // namespace

const Brush* Brush::from(IUnknown* brush) { return dynamic_cast<const Brush*>(brush); }

HRESULT createSolidColorBrush(const D2D1_COLOR_F& color, const D2D1_BRUSH_PROPERTIES* properties, ID2D1SolidColorBrush** brush) {
    return make<SolidColorBrush>(brush, color, properties);
}

HRESULT createGradientStopCollection(const D2D1_GRADIENT_STOP* stops, UINT32 count, D2D1_EXTEND_MODE extendMode,
                                     ID2D1GradientStopCollection** collection) {
    if (!stops || !count) return E_INVALIDARG;
    return make<GradientStopCollection>(collection, std::vector<D2D1_GRADIENT_STOP>(stops, stops + count), extendMode);
}

HRESULT createLinearGradientBrush(const D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES& gradient, const D2D1_BRUSH_PROPERTIES* properties,
                                  ID2D1GradientStopCollection* stops, ID2D1LinearGradientBrush** brush) {
    auto* collection = stopsOf(stops);
    if (!collection) return E_INVALIDARG;
    return make<LinearGradientBrush>(brush, gradient, properties, ComPtr<GradientStopCollection>(collection));
}

HRESULT createRadialGradientBrush(const D2D1_RADIAL_GRADIENT_BRUSH_PROPERTIES& gradient, const D2D1_BRUSH_PROPERTIES* properties,
                                  ID2D1GradientStopCollection* stops, ID2D1RadialGradientBrush** brush) {
    auto* collection = stopsOf(stops);
    if (!collection) return E_INVALIDARG;
    return make<RadialGradientBrush>(brush, gradient, properties, ComPtr<GradientStopCollection>(collection));
}

HRESULT createBitmapBrush(ID2D1Bitmap* bitmap, const D2D1_BITMAP_BRUSH_PROPERTIES* bitmapProperties, const D2D1_BRUSH_PROPERTIES* properties,
                          ID2D1BitmapBrush** brush) {
    return make<BitmapBrush>(brush, ComPtr<Bitmap>(Bitmap::from(bitmap)), bitmapProperties, properties);
}
} // namespace lm_d2d

namespace lm_gfx {
bool brushPaint(IUnknown* brush, lm_impeller::Paint& paint) {
    const auto* own = lm_d2d::Brush::from(brush);
    if (!own) return false;
    own->paint(paint, 1);
    return true;
}
} // namespace lm_gfx
