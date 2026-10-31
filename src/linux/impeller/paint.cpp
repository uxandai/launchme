// Translate CPU Skia descriptions to native Impeller display-list primitives.
#include "internal.hpp"
#include "include/core/SkPath.h"
#include "include/core/SkM44.h"
#include "include/core/SkShader.h"
#include "src/shaders/SkShaderBase.h"
#include <algorithm>
#include <cmath>

namespace lm_impeller {
ImpellerMatrix nativeMatrix(const SkMatrix& matrix) {
    ImpellerMatrix result{};
    SkM44(matrix).getColMajor(result.m);
    return result;
}

Path nativePath(const SkPath& path) {
    PathBuilder builder(ImpellerPathBuilderNew());
    if (!builder) return {};
    SkPath::Iter it(path, false);
    SkPoint points[4]{};
    const auto point = [](SkPoint p) { return ImpellerPoint{p.x(), p.y()}; };
    for (SkPath::Verb verb; (verb = it.next(points)) != SkPath::kDone_Verb;) {
        const auto a = point(points[0]), b = point(points[1]), c = point(points[2]), d = point(points[3]);
        switch (verb) {
        case SkPath::kMove_Verb: ImpellerPathBuilderMoveTo(builder.get(), &a); break;
        case SkPath::kLine_Verb: ImpellerPathBuilderLineTo(builder.get(), &b); break;
        case SkPath::kQuad_Verb: ImpellerPathBuilderQuadraticCurveTo(builder.get(), &b, &c); break;
        case SkPath::kCubic_Verb: ImpellerPathBuilderCubicCurveTo(builder.get(), &b, &c, &d); break;
        case SkPath::kConic_Verb: {
            SkPoint quads[33];
            const int count = SkPath::ConvertConicToQuads(points[0], points[1], points[2], it.conicWeight(), quads, 4);
            for (int i = 0; i < count; ++i) {
                const auto control = point(quads[i * 2 + 1]), end = point(quads[i * 2 + 2]);
                ImpellerPathBuilderQuadraticCurveTo(builder.get(), &control, &end);
            }
            break;
        }
        case SkPath::kClose_Verb: ImpellerPathBuilderClose(builder.get()); break;
        default: break;
        }
    }
    return Path(ImpellerPathBuilderTakePathNew(builder.get(),
        path.getFillType() == SkPathFillType::kEvenOdd ? kImpellerFillTypeOdd : kImpellerFillTypeNonZero));
}

namespace {
ImpellerTileMode tile(SkTileMode mode) { return static_cast<ImpellerTileMode>(mode); }
// The standalone API interpolates straight colors; Direct2D's gradients interpolate premul.
// Subdivide only where the resulting quadratic interpolation error exceeds 1/1024 per
// premultiplied channel. This keeps gradients on the GPU without transparent-stop fringes.
std::pair<std::vector<ImpellerColor>, std::vector<float>> gradientStops(
    const std::vector<SkColor4f>& colors, const std::vector<float>& positions, bool premul) {
    std::pair<std::vector<ImpellerColor>, std::vector<float>> result;
    const auto push = [&](const SkColor4f& color, float position) {
        result.first.push_back(nativeColor(color)); result.second.push_back(position);
    };
    if (!premul || colors.size() < 2) {
        for (size_t i = 0; i < colors.size(); ++i) push(colors[i], positions[i]);
        return result;
    }
    for (size_t i = 1; i < colors.size(); ++i) {
        const auto a = colors[i - 1], b = colors[i];
        const auto at = [&](float t) {
            SkColor4f c{};
            c.fA = a.fA + (b.fA - a.fA) * t;
            for (int channel = 0; channel < 3; ++channel)
                c[channel] = c.fA > 0 ? (a[channel] * a.fA * (1 - t) + b[channel] * b.fA * t) / c.fA
                                      : a.fA > 0 ? a[channel] : b[channel];
            return c;
        };
        const auto append = [&](auto&& self, float left, float right, SkColor4f start, SkColor4f end, unsigned depth) -> void {
            float difference = 0;
            for (int c = 0; c < 3; ++c) difference = std::max(difference, std::abs(end[c] - start[c]));
            if (depth < 12 && difference * std::abs(end.fA - start.fA) * 0.25f > 1.0f / 1024) {
                const float middle = (left + right) * 0.5f;
                const auto color = at(middle);
                self(self, left, middle, start, color, depth + 1);
                self(self, middle, right, color, end, depth + 1);
            } else push(end, positions[i - 1] + (positions[i] - positions[i - 1]) * right);
        };
        push(at(0), positions[i - 1]);
        append(append, 0, 1, at(0), at(1), 0);
    }
    return result;
}

ColorSource source(const SkShader* shader) {
    if (!shader) return {};
    SkShaderBase::GradientInfo info{};
    SkMatrix local;
    const auto type = as_SB(shader)->asGradient(&info, &local);
    if (type != SkShaderBase::GradientType::kNone && info.fColorCount > 0) {
        std::vector<SkColor4f> colors(info.fColorCount);
        std::vector<float> positions(info.fColorCount);
        info.fColors = colors.data();
        info.fColorOffsets = positions.data();
        as_SB(shader)->asGradient(&info, &local);
        const auto [native, offsets] = gradientStops(colors, positions, info.fPremulInterp);
        const auto transform = nativeMatrix(local);
        const ImpellerPoint a{info.fPoint[0].x(), info.fPoint[0].y()}, b{info.fPoint[1].x(), info.fPoint[1].y()};
        const auto count = static_cast<uint32_t>(native.size());
        switch (type) {
        case SkShaderBase::GradientType::kLinear:
            return ColorSource(ImpellerColorSourceCreateLinearGradientNew(&a, &b, count, native.data(), offsets.data(), tile(info.fTileMode), &transform));
        case SkShaderBase::GradientType::kRadial:
            return ColorSource(ImpellerColorSourceCreateRadialGradientNew(&a, info.fRadius[0], count, native.data(), offsets.data(), tile(info.fTileMode), &transform));
        case SkShaderBase::GradientType::kConical:
            return ColorSource(ImpellerColorSourceCreateConicalGradientNew(&a, info.fRadius[0], &b, info.fRadius[1], count, native.data(), offsets.data(), tile(info.fTileMode), &transform));
        default: return {};
        }
    }
    SkTileMode modes[2];
    if (auto* image = shader->isAImage(&local, modes)) {
        auto texture = textureFor(image);
        if (!texture) return {};
        const auto transform = nativeMatrix(local);
        return ColorSource(ImpellerColorSourceCreateImageNew(texture, tile(modes[0]), tile(modes[1]), kImpellerTextureSamplingLinear, &transform));
    }
    return {};
}
} // namespace

Paint nativePaint(const SkPaint& paint) {
    // These cases are rasterized locally with Skia, preserving their full semantics.
    if (paint.getColorFilter() || paint.getImageFilter() || paint.getMaskFilter() || paint.getPathEffect()) return {};
    const auto blend = paint.asBlendMode();
    if (!blend) return {};
    Paint result(ImpellerPaintNew());
    if (!result) return {};
    const auto color = nativeColor(paint.getColor4f());
    ImpellerPaintSetColor(result.get(), &color);
    // Both APIs use the Porter-Duff modes followed by the same advanced blend modes.
    ImpellerPaintSetBlendMode(result.get(), static_cast<ImpellerBlendMode>(*blend));
    ImpellerPaintSetDrawStyle(result.get(), static_cast<ImpellerDrawStyle>(paint.getStyle()));
    ImpellerPaintSetStrokeWidth(result.get(), paint.getStrokeWidth());
    ImpellerPaintSetStrokeCap(result.get(), static_cast<ImpellerStrokeCap>(paint.getStrokeCap()));
    ImpellerPaintSetStrokeJoin(result.get(), static_cast<ImpellerStrokeJoin>(paint.getStrokeJoin()));
    ImpellerPaintSetStrokeMiter(result.get(), paint.getStrokeMiter());
    if (const auto* shader = paint.getShader()) {
        auto colors = source(shader);
        if (!colors) return {};
        ImpellerPaintSetColorSource(result.get(), colors.get());
    }
    return result;
}
} // namespace lm_impeller
