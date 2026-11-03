// The built-in Direct2D effects the launcher uses: Gaussian blur, colour matrix, saturation,
// blend, 2D affine transform, arithmetic composite, point-specular lighting and composite. The
// first ones run on the GPU with Impeller's own filters and blend modes; arithmetic composite,
// point-specular lighting and a colour matrix on premultiplied values are per-pixel maths on the
// CPU. An effect keeps its last result until a property or an input changes or another
// resolution is requested.
#include "d2d_internal.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace lm_d2d {
namespace {
enum class Kind { blur, color_matrix, saturation, blend, affine, arithmetic, specular, composite };

// A pixel region in the evaluation grid (pixels at the requested scale).
gx::IRect areaOf(const ImageData& image) {
    if (!image.image) return {};
    return gx::IRect::XYWH(image.x, image.y, image.image->width(), image.image->height());
}

gx::IRect unite(gx::IRect a, const gx::IRect& b) {
    a.join(b);
    return a;
}

// What `draw` records over `area` (drawn in the grid's coordinates), rendered on the GPU.
template <class Draw>
ImageData renderArea(const gx::IRect& area, Draw draw) {
    if (area.empty() || !gx::active() || area.width() > 16384 || area.height() > 16384) return {};
    gx::Canvas canvas(area.width(), area.height());
    canvas.translate(static_cast<float>(-area.left), static_cast<float>(-area.top));
    draw(canvas);
    auto result = canvas.snapshot();
    return result ? ImageData{std::move(result), area.left, area.top} : ImageData{};
}

void drawPlaced(gx::Canvas& canvas, const ImageData& data, const gx::Paint& paint) {
    if (!data.image) return;
    const gx::Rect from = gx::Rect::XYWH(0, 0, static_cast<float>(data.image->width()), static_cast<float>(data.image->height()));
    canvas.drawImageRect(data.image, from, from.offset(static_cast<float>(data.x), static_cast<float>(data.y)), gx::Sampling{false}, &paint);
}

ImpellerBlendMode blendFor(D2D1_BLEND_MODE mode) {
    switch (mode) {
    case D2D1_BLEND_MODE_MULTIPLY: return kImpellerBlendModeMultiply;
    case D2D1_BLEND_MODE_SCREEN: return kImpellerBlendModeScreen;
    case D2D1_BLEND_MODE_DARKEN: return kImpellerBlendModeDarken;
    case D2D1_BLEND_MODE_LIGHTEN: return kImpellerBlendModeLighten;
    default: return kImpellerBlendModeSourceOver; // Other modes: the source over the destination.
    }
}

ImpellerBlendMode blendFor(D2D1_COMPOSITE_MODE mode) {
    switch (mode) {
    case D2D1_COMPOSITE_MODE_DESTINATION_OVER: return kImpellerBlendModeDestinationOver;
    case D2D1_COMPOSITE_MODE_SOURCE_IN: return kImpellerBlendModeSourceIn;
    case D2D1_COMPOSITE_MODE_DESTINATION_IN: return kImpellerBlendModeDestinationIn;
    case D2D1_COMPOSITE_MODE_SOURCE_OUT: return kImpellerBlendModeSourceOut;
    case D2D1_COMPOSITE_MODE_DESTINATION_OUT: return kImpellerBlendModeDestinationOut;
    case D2D1_COMPOSITE_MODE_SOURCE_ATOP: return kImpellerBlendModeSourceATop;
    case D2D1_COMPOSITE_MODE_DESTINATION_ATOP: return kImpellerBlendModeDestinationATop;
    case D2D1_COMPOSITE_MODE_XOR: return kImpellerBlendModeXor;
    case D2D1_COMPOSITE_MODE_PLUS: return kImpellerBlendModePlus;
    case D2D1_COMPOSITE_MODE_SOURCE_COPY:
    case D2D1_COMPOSITE_MODE_BOUNDED_SOURCE_COPY: return kImpellerBlendModeSource;
    default: return kImpellerBlendModeSourceOver;
    }
}

// ---- CPU kernels (premultiplied BGRA bytes over a grid region) ------------------------------

struct Pixels {
    std::shared_ptr<const std::vector<uint8_t>> bytes;
    gx::IRect area;
    // The premultiplied channels (b, g, r, a) in 0..1 at a grid position; transparent outside.
    std::array<float, 4> at(int x, int y) const {
        if (!bytes || x < area.left || y < area.top || x >= area.right || y >= area.bottom) return {0, 0, 0, 0};
        const uint8_t* p = bytes->data() + (static_cast<size_t>(y - area.top) * area.width() + (x - area.left)) * 4;
        return {p[0] / 255.0f, p[1] / 255.0f, p[2] / 255.0f, p[3] / 255.0f};
    }
};

Pixels pixelsOf(const ImageData& data) {
    if (!data.image) return {};
    return {data.image->pixels(), areaOf(data)};
}

uint8_t byteOf(float value) { return static_cast<uint8_t>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255)); }

ImageData fromFloats(const gx::IRect& area, const std::vector<float>& channels) {
    std::vector<uint8_t> bytes(channels.size());
    for (size_t i = 0; i < channels.size(); i += 4) {
        const float a = std::clamp(channels[i + 3], 0.0f, 1.0f);
        for (int c = 0; c < 3; ++c) bytes[i + c] = byteOf(std::min(channels[i + c], a));
        bytes[i + 3] = byteOf(a);
    }
    auto image = gx::Image::fromPixels(area.width(), area.height(), std::move(bytes));
    return image ? ImageData{std::move(image), area.left, area.top} : ImageData{};
}

// k1*in1*in0 + k2*in1 + k3*in0 + k4 on premultiplied values (in1 the foreground), as the launcher has always composed it.
ImageData arithmetic(const ImageData& first, const ImageData& second, const D2D1_VECTOR_4F& k) {
    const gx::IRect area = unite(areaOf(first), areaOf(second));
    if (area.empty()) return {};
    const Pixels a = pixelsOf(first), b = pixelsOf(second);
    if ((first.image && !a.bytes) || (second.image && !b.bytes)) return {};
    std::vector<float> out(static_cast<size_t>(area.width()) * area.height() * 4);
    size_t i = 0;
    for (int y = area.top; y < area.bottom; ++y)
        for (int x = area.left; x < area.right; ++x, i += 4) {
            const auto d = a.at(x, y), s = b.at(x, y);
            for (int c = 0; c < 4; ++c) out[i + c] = std::clamp(k.x * s[c] * d[c] + k.y * s[c] + k.z * d[c] + k.w, 0.0f, 1.0f);
        }
    return fromFloats(area, out);
}

// A 5x4 matrix (Direct2D's layout) on the premultiplied values as they are.
ImageData premultipliedMatrix(const ImageData& in, const D2D1_MATRIX_5X4_F& m, bool clamp) {
    const Pixels p = pixelsOf(in);
    const gx::IRect area = areaOf(in);
    if (!p.bytes) return {};
    std::vector<float> out(static_cast<size_t>(area.width()) * area.height() * 4);
    size_t i = 0;
    for (int y = area.top; y < area.bottom; ++y)
        for (int x = area.left; x < area.right; ++x, i += 4) {
            const auto c = p.at(x, y); // b, g, r, a
            const float r = c[2], g = c[1], b = c[0], a = c[3];
            float nr = r * m._11 + g * m._21 + b * m._31 + a * m._41 + m._51;
            float ng = r * m._12 + g * m._22 + b * m._32 + a * m._42 + m._52;
            float nb = r * m._13 + g * m._23 + b * m._33 + a * m._43 + m._53;
            float na = r * m._14 + g * m._24 + b * m._34 + a * m._44 + m._54;
            if (clamp) { nr = std::clamp(nr, 0.0f, 1.0f); ng = std::clamp(ng, 0.0f, 1.0f); nb = std::clamp(nb, 0.0f, 1.0f); na = std::clamp(na, 0.0f, 1.0f); }
            out[i] = nb; out[i + 1] = ng; out[i + 2] = nr; out[i + 3] = na;
        }
    return fromFloats(area, out);
}

// SVG's feSpecularLighting with a point light: the alpha channel is the height map. The light is in grid pixels.
ImageData pointSpecular(const ImageData& in, float lx, float ly, float lz, const D2D1_VECTOR_3F& color, float surface_scale, float constant,
                        float exponent) {
    const Pixels p = pixelsOf(in);
    const gx::IRect area = areaOf(in);
    if (!p.bytes) return {};
    const auto height = [&](int x, int y) {
        x = std::clamp(x, area.left, area.right - 1);
        y = std::clamp(y, area.top, area.bottom - 1);
        return p.at(x, y)[3];
    };
    std::vector<float> out(static_cast<size_t>(area.width()) * area.height() * 4);
    size_t i = 0;
    for (int y = area.top; y < area.bottom; ++y)
        for (int x = area.left; x < area.right; ++x, i += 4) {
            const float nx = -surface_scale * 0.25f * ((height(x + 1, y - 1) + 2 * height(x + 1, y) + height(x + 1, y + 1)) -
                                                       (height(x - 1, y - 1) + 2 * height(x - 1, y) + height(x - 1, y + 1)));
            const float ny = -surface_scale * 0.25f * ((height(x - 1, y + 1) + 2 * height(x, y + 1) + height(x + 1, y + 1)) -
                                                       (height(x - 1, y - 1) + 2 * height(x, y - 1) + height(x + 1, y - 1)));
            const float normal_length = std::sqrt(nx * nx + ny * ny + 1);
            float tx = lx - x, ty = ly - y, tz = lz - surface_scale * height(x, y);
            const float to_light = std::sqrt(tx * tx + ty * ty + tz * tz);
            if (to_light > 0) { tx /= to_light; ty /= to_light; tz /= to_light; }
            float hx = tx, hy = ty, hz = tz + 1;
            const float half_length = std::sqrt(hx * hx + hy * hy + hz * hz);
            float shine = 0;
            if (half_length > 0) {
                hx /= half_length; hy /= half_length; hz /= half_length;
                const float dot = std::max(0.0f, (nx * hx + ny * hy + hz) / normal_length);
                shine = constant * std::pow(dot, exponent);
            }
            const float r = std::clamp(shine * color.x, 0.0f, 1.0f), g = std::clamp(shine * color.y, 0.0f, 1.0f), b = std::clamp(shine * color.z, 0.0f, 1.0f);
            out[i] = b; out[i + 1] = g; out[i + 2] = r; out[i + 3] = std::max({r, g, b});
        }
    return fromFloats(area, out);
}

template <class T>
bool read(const BYTE* data, UINT32 size, T& value) {
    if (!data || size != sizeof(T)) return false;
    std::memcpy(&value, data, sizeof(T));
    return true;
}

class Effect final : public lm_com::Object<ID2D1Effect> {
public:
    explicit Effect(Kind kind) : kind_(kind), inputs_(kind == Kind::blend || kind == Kind::arithmetic || kind == Kind::composite ? 2 : 1) {}
    HRESULT STDMETHODCALLTYPE SetValue(UINT32 index, D2D1_PROPERTY_TYPE, const BYTE* data, UINT32 size) override;
    void STDMETHODCALLTYPE SetInput(UINT32 index, ID2D1Image* input, BOOL) override {
        if (index >= inputs_.size() || inputs_[index].Get() == input) return;
        inputs_[index] = input;
        version_ = newVersion();
        cached_ = {}; // The old result's memory goes with the old input.
    }
    void STDMETHODCALLTYPE GetOutput(ID2D1Image** output) const override;

    ImageData render(double scale) {
        const uint64_t current = version();
        if (current != cached_version_ || scale != cached_scale_) {
            cached_ = compute(scale);
            cached_version_ = current;
            cached_scale_ = scale;
        }
        return cached_;
    }
    // Own settings and every input's content.
    uint64_t version() const {
        uint64_t hash = version_;
        for (const auto& input : inputs_) {
            const auto* source = ImageSource::from(input.Get());
            hash = (hash ^ (source ? source->version() : 0)) * 0x100000001B3ull + 0x9E3779B97F4A7C15ull;
        }
        return hash;
    }

private:
    template <class T>
    HRESULT store(T& field, const BYTE* data, UINT32 size) {
        T value;
        if (!read(data, size, value)) return E_INVALIDARG;
        if (std::memcmp(&value, &field, sizeof(T)) != 0) { field = value; version_ = newVersion(); }
        return S_OK;
    }
    ImageData input(size_t index, double scale) const {
        auto* source = index < inputs_.size() ? ImageSource::from(inputs_[index].Get()) : nullptr;
        return source ? source->render(scale) : ImageData{};
    }
    ImageData compute(double scale) const;
    ImageData twoInputs(const ImageData& destination, const ImageData& source) const;

    Kind kind_;
    std::vector<ComPtr<ID2D1Image>> inputs_;
    uint64_t version_ = newVersion();
    // Properties, with Direct2D's defaults.
    float deviation_ = 3;
    UINT32 optimization_ = D2D1_GAUSSIANBLUR_OPTIMIZATION_BALANCED; // Accepted; Impeller picks its own blur method.
    D2D1_BORDER_MODE border_ = D2D1_BORDER_MODE_SOFT;
    D2D1_MATRIX_5X4_F matrix_{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0};
    D2D1_COLORMATRIX_ALPHA_MODE alpha_mode_ = D2D1_COLORMATRIX_ALPHA_MODE_PREMULTIPLIED;
    BOOL clamp_ = FALSE;
    float saturation_ = 0.5f;
    D2D1_BLEND_MODE blend_ = D2D1_BLEND_MODE_MULTIPLY;
    D2D1_2DAFFINETRANSFORM_INTERPOLATION_MODE interpolation_ = D2D1_2DAFFINETRANSFORM_INTERPOLATION_MODE_LINEAR;
    D2D1_MATRIX_3X2_F transform_ = D2D1::IdentityMatrix();
    float sharpness_ = 1; // Accepted; cubic sharpness is not modelled.
    D2D1_VECTOR_4F coefficients_{1, 0, 0, 0};
    D2D1_VECTOR_3F light_{0, 0, 0}, color_{1, 1, 1};
    float exponent_ = 1, constant_ = 1, surface_scale_ = 1;
    D2D1_VECTOR_2F kernel_{1, 1}; // Accepted; the lighting samples one pixel apart.
    UINT32 scale_mode_ = D2D1_POINTSPECULAR_SCALE_MODE_LINEAR; // Accepted; the height map is never rescaled.
    D2D1_COMPOSITE_MODE composite_mode_ = D2D1_COMPOSITE_MODE_SOURCE_OVER;

    uint64_t cached_version_ = 0;
    double cached_scale_ = 0;
    ImageData cached_;
};

// Its effect's result; holds the effect alive (SetInputEffect keeps the output only).
class EffectOutput final : public lm_com::Object<ID2D1Image>, public ImageSource {
public:
    explicit EffectOutput(ComPtr<Effect> effect) : effect_(std::move(effect)) {}
    ImageData render(double scale) override { return effect_->render(scale); }
    uint64_t version() const override { return effect_->version(); }
private:
    ComPtr<Effect> effect_;
};

void STDMETHODCALLTYPE Effect::GetOutput(ID2D1Image** output) const {
    if (output) *output = new EffectOutput(ComPtr<Effect>(const_cast<Effect*>(this)));
}

HRESULT STDMETHODCALLTYPE Effect::SetValue(UINT32 index, D2D1_PROPERTY_TYPE, const BYTE* data, UINT32 size) {
    switch (kind_) {
    case Kind::blur:
        if (index == D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION) return store(deviation_, data, size);
        if (index == D2D1_GAUSSIANBLUR_PROP_OPTIMIZATION) return store(optimization_, data, size);
        if (index == D2D1_GAUSSIANBLUR_PROP_BORDER_MODE) return store(border_, data, size);
        break;
    case Kind::color_matrix:
        if (index == D2D1_COLORMATRIX_PROP_COLOR_MATRIX) return store(matrix_, data, size);
        if (index == D2D1_COLORMATRIX_PROP_ALPHA_MODE) return store(alpha_mode_, data, size);
        if (index == D2D1_COLORMATRIX_PROP_CLAMP_OUTPUT) return store(clamp_, data, size);
        break;
    case Kind::saturation:
        if (index == D2D1_SATURATION_PROP_SATURATION) return store(saturation_, data, size);
        break;
    case Kind::blend:
        if (index == D2D1_BLEND_PROP_MODE) return store(blend_, data, size);
        break;
    case Kind::affine:
        if (index == D2D1_2DAFFINETRANSFORM_PROP_INTERPOLATION_MODE) return store(interpolation_, data, size);
        if (index == D2D1_2DAFFINETRANSFORM_PROP_BORDER_MODE) return store(border_, data, size);
        if (index == D2D1_2DAFFINETRANSFORM_PROP_TRANSFORM_MATRIX) return store(transform_, data, size);
        if (index == D2D1_2DAFFINETRANSFORM_PROP_SHARPNESS) return store(sharpness_, data, size);
        break;
    case Kind::arithmetic:
        if (index == D2D1_ARITHMETICCOMPOSITE_PROP_COEFFICIENTS) return store(coefficients_, data, size);
        if (index == D2D1_ARITHMETICCOMPOSITE_PROP_CLAMP_OUTPUT) return store(clamp_, data, size);
        break;
    case Kind::specular:
        if (index == D2D1_POINTSPECULAR_PROP_LIGHT_POSITION) return store(light_, data, size);
        if (index == D2D1_POINTSPECULAR_PROP_SPECULAR_EXPONENT) return store(exponent_, data, size);
        if (index == D2D1_POINTSPECULAR_PROP_SPECULAR_CONSTANT) return store(constant_, data, size);
        if (index == D2D1_POINTSPECULAR_PROP_SURFACE_SCALE) return store(surface_scale_, data, size);
        if (index == D2D1_POINTSPECULAR_PROP_COLOR) return store(color_, data, size);
        if (index == D2D1_POINTSPECULAR_PROP_KERNEL_UNIT_LENGTH) return store(kernel_, data, size);
        if (index == D2D1_POINTSPECULAR_PROP_SCALE_MODE) return store(scale_mode_, data, size);
        break;
    case Kind::composite:
        if (index == D2D1_COMPOSITE_PROP_MODE) return store(composite_mode_, data, size);
        break;
    }
    return E_INVALIDARG;
}

// Blend, arithmetic composite and composite: input 0 the destination (background), 1 the source
// (foreground), over both inputs' area.
ImageData Effect::twoInputs(const ImageData& destination, const ImageData& source) const {
    const gx::IRect area = unite(areaOf(destination), areaOf(source));
    if (area.empty()) return {};
    if (kind_ == Kind::arithmetic) return arithmetic(destination, source, coefficients_);
    gx::Paint plain;
    plain.color = {0, 0, 0, 1};
    gx::Paint over = plain;
    over.blend = kind_ == Kind::blend ? blendFor(blend_) : blendFor(composite_mode_);
    return renderArea(area, [&](gx::Canvas& canvas) {
        drawPlaced(canvas, destination, plain);
        // Without a background the foreground is all there is.
        drawPlaced(canvas, source, destination.image ? over : plain);
    });
}

ImageData Effect::compute(double scale) const {
    switch (kind_) {
    case Kind::blur: {
        const ImageData in = input(0, scale);
        const float sigma = static_cast<float>(deviation_ * scale);
        if (!in.image || sigma < 0.3f) return in;
        // A soft border grows the image by three deviations (fading into transparency); a hard
        // one keeps its size and mirrors its edges.
        const bool hard = border_ == D2D1_BORDER_MODE_HARD;
        const int pad = hard ? 0 : static_cast<int>(std::ceil(sigma * 3));
        const gx::IRect area = areaOf(in).outset(pad, pad);
        gx::Paint paint;
        paint.color = {0, 0, 0, 1};
        paint.blur_sigma = sigma;
        paint.blur_tile = hard ? kImpellerTileModeMirror : kImpellerTileModeDecal;
        return renderArea(area, [&](gx::Canvas& canvas) { drawPlaced(canvas, in, paint); });
    }
    case Kind::color_matrix:
    case Kind::saturation: {
        const ImageData in = input(0, scale);
        if (!in.image) return {};
        gx::Paint paint;
        paint.color = {0, 0, 0, 1};
        paint.has_color_matrix = true;
        if (kind_ == Kind::saturation) {
            const float s = std::clamp(saturation_, 0.0f, 1.0f);
            const float m[20] = {0.213f + 0.787f * s, 0.715f - 0.715f * s, 0.072f - 0.072f * s, 0, 0,
                                 0.213f - 0.213f * s, 0.715f + 0.285f * s, 0.072f - 0.072f * s, 0, 0,
                                 0.213f - 0.213f * s, 0.715f - 0.715f * s, 0.072f + 0.928f * s, 0, 0,
                                 0, 0, 0, 1, 0};
            std::copy(m, m + 20, paint.color_matrix);
        } else if (alpha_mode_ == D2D1_COLORMATRIX_ALPHA_MODE_PREMULTIPLIED) {
            // The filter works on straight colour and premultiplies the result, as this mode does.
            const auto& d = matrix_;
            const float m[20] = {d._11, d._21, d._31, d._41, d._51, d._12, d._22, d._32, d._42, d._52,
                                 d._13, d._23, d._33, d._43, d._53, d._14, d._24, d._34, d._44, d._54};
            std::copy(m, m + 20, paint.color_matrix);
        } else {
            return premultipliedMatrix(in, matrix_, clamp_ != FALSE);
        }
        return renderArea(areaOf(in), [&](gx::Canvas& canvas) { drawPlaced(canvas, in, paint); });
    }
    case Kind::blend:
    case Kind::arithmetic:
    case Kind::composite: return twoInputs(input(0, scale), input(1, scale));
    case Kind::affine: {
        const ImageData in = input(0, scale);
        if (!in.image) return {};
        // The matrix maps DIPs: in grid pixels it is scaled around it.
        gx::Matrix matrix = toGx(transform_);
        matrix.preScale(static_cast<float>(1 / scale), static_cast<float>(1 / scale));
        matrix.postScale(static_cast<float>(scale), static_cast<float>(scale));
        if (!matrix.invert(nullptr)) return {};
        const gx::Rect mapped = matrix.mapRect(areaOf(in).toRect());
        const bool hard = border_ == D2D1_BORDER_MODE_HARD;
        const gx::IRect area = hard ? gx::roundNearest(mapped) : gx::roundOut(mapped).outset(1, 1);
        gx::Paint paint;
        paint.color = {0, 0, 0, 1};
        const gx::Sampling sampling{interpolation_ == D2D1_2DAFFINETRANSFORM_INTERPOLATION_MODE_NEAREST_NEIGHBOR};
        return renderArea(area, [&](gx::Canvas& canvas) {
            canvas.concat(matrix);
            const gx::Rect from = gx::Rect::XYWH(0, 0, static_cast<float>(in.image->width()), static_cast<float>(in.image->height()));
            canvas.drawImageRect(in.image, from, from.offset(static_cast<float>(in.x), static_cast<float>(in.y)), sampling, &paint);
        });
    }
    case Kind::specular: {
        const ImageData in = input(0, scale);
        if (!in.image) return {};
        return pointSpecular(in, static_cast<float>(light_.x * scale), static_cast<float>(light_.y * scale), static_cast<float>(light_.z * scale),
                             color_, surface_scale_, constant_, std::clamp(exponent_, 1.0f, 128.0f));
    }
    }
    return {};
}
} // namespace

HRESULT createEffect(REFCLSID id, ID2D1Effect** effect) {
    if (!effect) return E_POINTER;
    *effect = nullptr;
    Kind kind;
    if (id == CLSID_D2D1GaussianBlur) kind = Kind::blur;
    else if (id == CLSID_D2D1ColorMatrix) kind = Kind::color_matrix;
    else if (id == CLSID_D2D1Saturation) kind = Kind::saturation;
    else if (id == CLSID_D2D1Blend) kind = Kind::blend;
    else if (id == CLSID_D2D12DAffineTransform) kind = Kind::affine;
    else if (id == CLSID_D2D1ArithmeticComposite) kind = Kind::arithmetic;
    else if (id == CLSID_D2D1PointSpecular) kind = Kind::specular;
    else if (id == CLSID_D2D1Composite) kind = Kind::composite;
    else return HRESULT_FROM_WIN32(1168); // ERROR_NOT_FOUND, as for an unregistered effect.
    *effect = new Effect(kind);
    return S_OK;
}
} // namespace lm_d2d
