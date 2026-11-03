// Paths: construction, transforms, flattening, hit testing, measurement, dashes and boolean
// combinations (Clipper2 on flattened figures). Curves are quadratics and cubics; arcs and
// ellipses are built from cubics.
#include "kernel.hpp"

#include "clipper2/clipper.h"

#include <cfloat>
#include <numbers>

namespace lm_impeller {
bool Matrix::invert(Matrix* out) const {
    const float* m = v;
    const double c00 = static_cast<double>(m[4]) * m[8] - static_cast<double>(m[5]) * m[7];
    const double c01 = static_cast<double>(m[5]) * m[6] - static_cast<double>(m[3]) * m[8];
    const double c02 = static_cast<double>(m[3]) * m[7] - static_cast<double>(m[4]) * m[6];
    const double det = m[0] * c00 + m[1] * c01 + m[2] * c02;
    if (!std::isfinite(det) || std::abs(det) < 1e-18) return false;
    if (!out) return true;
    const double s = 1.0 / det;
    Matrix r;
    r.v[0] = static_cast<float>(c00 * s);
    r.v[1] = static_cast<float>((static_cast<double>(m[2]) * m[7] - static_cast<double>(m[1]) * m[8]) * s);
    r.v[2] = static_cast<float>((static_cast<double>(m[1]) * m[5] - static_cast<double>(m[2]) * m[4]) * s);
    r.v[3] = static_cast<float>(c01 * s);
    r.v[4] = static_cast<float>((static_cast<double>(m[0]) * m[8] - static_cast<double>(m[2]) * m[6]) * s);
    r.v[5] = static_cast<float>((static_cast<double>(m[2]) * m[3] - static_cast<double>(m[0]) * m[5]) * s);
    r.v[6] = static_cast<float>(c02 * s);
    r.v[7] = static_cast<float>((static_cast<double>(m[1]) * m[6] - static_cast<double>(m[0]) * m[7]) * s);
    r.v[8] = static_cast<float>((static_cast<double>(m[0]) * m[4] - static_cast<double>(m[1]) * m[3]) * s);
    *out = r;
    return true;
}

namespace {
constexpr float kappa = 0.5522847498f; // A quarter circle as a cubic.

Point last(const Path& p) { return p.points.empty() ? Point{} : p.points.back(); }
} // namespace

Path& Path::moveTo(float x, float y) {
    // A move right after a move replaces it.
    if (!verbs.empty() && verbs.back() == Verb::move) { points.back() = {x, y}; return *this; }
    verbs.push_back(Verb::move);
    points.push_back({x, y});
    return *this;
}

Path& Path::lineTo(float x, float y) {
    if (verbs.empty() || verbs.back() == Verb::close) moveTo(verbs.empty() ? x : last(*this).x, verbs.empty() ? y : last(*this).y);
    verbs.push_back(Verb::line);
    points.push_back({x, y});
    return *this;
}

Path& Path::quadTo(float cx, float cy, float x, float y) {
    if (verbs.empty()) moveTo(cx, cy);
    verbs.push_back(Verb::quad);
    points.push_back({cx, cy});
    points.push_back({x, y});
    return *this;
}

Path& Path::cubicTo(float x1, float y1, float x2, float y2, float x3, float y3) {
    if (verbs.empty()) moveTo(x1, y1);
    verbs.push_back(Verb::cubic);
    points.push_back({x1, y1});
    points.push_back({x2, y2});
    points.push_back({x3, y3});
    return *this;
}

Path& Path::close() {
    if (!verbs.empty() && verbs.back() != Verb::close && verbs.back() != Verb::move) verbs.push_back(Verb::close);
    return *this;
}

Path& Path::arcTo(float rx, float ry, float rotation_degrees, bool large, bool clockwise, float x, float y) {
    const Point from = last(*this);
    rx = std::abs(rx);
    ry = std::abs(ry);
    if (rx == 0 || ry == 0 || (from.x == x && from.y == y)) return lineTo(x, y);
    // SVG implementation notes F.6.5: endpoint to centre parameterisation.
    const double phi = rotation_degrees * std::numbers::pi / 180, cp = std::cos(phi), sp = std::sin(phi);
    const double dx = (from.x - x) / 2.0, dy = (from.y - y) / 2.0;
    const double x1 = cp * dx + sp * dy, y1 = -sp * dx + cp * dy;
    double arx = rx, ary = ry;
    const double lambda = x1 * x1 / (arx * arx) + y1 * y1 / (ary * ary);
    if (lambda > 1) { const double s = std::sqrt(lambda); arx *= s; ary *= s; }
    const double numerator = arx * arx * ary * ary - arx * arx * y1 * y1 - ary * ary * x1 * x1;
    const double denominator = arx * arx * y1 * y1 + ary * ary * x1 * x1;
    double factor = denominator == 0 ? 0 : std::sqrt(std::max(0.0, numerator / denominator));
    if (large == clockwise) factor = -factor;
    const double cx1 = factor * arx * y1 / ary, cy1 = -factor * ary * x1 / arx;
    const double cx = cp * cx1 - sp * cy1 + (from.x + x) / 2.0, cy = sp * cx1 + cp * cy1 + (from.y + y) / 2.0;
    const auto angle = [](double ux, double uy, double vx, double vy) {
        return std::atan2(ux * vy - uy * vx, ux * vx + uy * vy);
    };
    const double start = angle(1, 0, (x1 - cx1) / arx, (y1 - cy1) / ary);
    double sweep = angle((x1 - cx1) / arx, (y1 - cy1) / ary, (-x1 - cx1) / arx, (-y1 - cy1) / ary);
    if (!clockwise && sweep > 0) sweep -= 2 * std::numbers::pi;
    else if (clockwise && sweep < 0) sweep += 2 * std::numbers::pi;
    const int segments = std::max(1, static_cast<int>(std::ceil(std::abs(sweep) / (std::numbers::pi / 2) - 1e-9)));
    const double step = sweep / segments;
    const double k = 4.0 / 3.0 * std::tan(step / 4);
    double a = start;
    for (int i = 0; i < segments; ++i, a += step) {
        const double c0 = std::cos(a), s0 = std::sin(a), c1 = std::cos(a + step), s1 = std::sin(a + step);
        const auto place = [&](double ux, double uy) {
            const double ex = arx * ux, ey = ary * uy;
            return Point{static_cast<float>(cp * ex - sp * ey + cx), static_cast<float>(sp * ex + cp * ey + cy)};
        };
        const Point p1 = place(c0 - k * s0, s0 + k * c0), p2 = place(c1 + k * s1, s1 - k * c1);
        Point p3 = place(c1, s1);
        if (i == segments - 1) p3 = {x, y};
        cubicTo(p1.x, p1.y, p2.x, p2.y, p3.x, p3.y);
    }
    return *this;
}

Path& Path::addPath(const Path& other) {
    verbs.insert(verbs.end(), other.verbs.begin(), other.verbs.end());
    points.insert(points.end(), other.points.begin(), other.points.end());
    return *this;
}

Path& Path::addRect(const Rect& r) {
    moveTo(r.left, r.top);
    lineTo(r.right, r.top);
    lineTo(r.right, r.bottom);
    lineTo(r.left, r.bottom);
    return close();
}

Path& Path::addRoundRect(const Rect& r, float rx, float ry) {
    if (rx <= 0 || ry <= 0) return addRect(r);
    const float kx = rx * kappa, ky = ry * kappa;
    moveTo(r.left + rx, r.top);
    lineTo(r.right - rx, r.top);
    cubicTo(r.right - rx + kx, r.top, r.right, r.top + ry - ky, r.right, r.top + ry);
    lineTo(r.right, r.bottom - ry);
    cubicTo(r.right, r.bottom - ry + ky, r.right - rx + kx, r.bottom, r.right - rx, r.bottom);
    lineTo(r.left + rx, r.bottom);
    cubicTo(r.left + rx - kx, r.bottom, r.left, r.bottom - ry + ky, r.left, r.bottom - ry);
    lineTo(r.left, r.top + ry);
    cubicTo(r.left, r.top + ry - ky, r.left + rx - kx, r.top, r.left + rx, r.top);
    return close();
}

Path& Path::addOval(const Rect& r) {
    const float cx = (r.left + r.right) / 2, cy = (r.top + r.bottom) / 2, rx = r.width() / 2, ry = r.height() / 2;
    const float kx = rx * kappa, ky = ry * kappa;
    moveTo(r.right, cy);
    cubicTo(r.right, cy + ky, cx + kx, r.bottom, cx, r.bottom);
    cubicTo(cx - kx, r.bottom, r.left, cy + ky, r.left, cy);
    cubicTo(r.left, cy - ky, cx - kx, r.top, cx, r.top);
    cubicTo(cx + kx, r.top, r.right, cy - ky, r.right, cy);
    return close();
}

Path& Path::addLine(Point a, Point b) { return moveTo(a.x, a.y).lineTo(b.x, b.y); }

Path Path::transformed(const Matrix& m) const {
    Path result;
    result.even_odd = even_odd;
    result.verbs = verbs;
    result.points.reserve(points.size());
    for (const Point& p : points) result.points.push_back(m.map(p));
    return result;
}

Rect Path::bounds() const {
    if (points.empty()) return {};
    Rect r{points[0].x, points[0].y, points[0].x, points[0].y};
    for (const Point& p : points) { r.left = std::min(r.left, p.x); r.top = std::min(r.top, p.y); r.right = std::max(r.right, p.x); r.bottom = std::max(r.bottom, p.y); }
    return r;
}

std::vector<Polyline> Path::flatten(float tolerance) const {
    std::vector<Polyline> figures;
    size_t at = 0;
    Point start{}, current{};
    const auto open = [&] {
        if (figures.empty() || figures.back().closed || figures.back().points.empty() || !(figures.back().points.back() == current)) {
            if (figures.empty() || figures.back().closed || figures.back().points.empty()) figures.push_back({});
        }
        if (figures.back().points.empty()) figures.back().points.push_back(current);
    };
    for (const Verb verb : verbs) {
        switch (verb) {
        case Verb::move:
            current = start = points[at++];
            figures.push_back({});
            figures.back().points.push_back(current);
            break;
        case Verb::line:
            open();
            current = points[at++];
            figures.back().points.push_back(current);
            break;
        case Verb::quad: {
            open();
            const Point c = points[at], e = points[at + 1];
            at += 2;
            const float dd = std::hypot(current.x - 2 * c.x + e.x, current.y - 2 * c.y + e.y);
            const int n = std::clamp(static_cast<int>(std::ceil(std::sqrt(0.25f * dd / tolerance))), 1, 400);
            for (int i = 1; i <= n; ++i) {
                const float t = static_cast<float>(i) / n, u = 1 - t;
                figures.back().points.push_back({u * u * current.x + 2 * u * t * c.x + t * t * e.x, u * u * current.y + 2 * u * t * c.y + t * t * e.y});
            }
            current = e;
            break;
        }
        case Verb::cubic: {
            open();
            const Point c1 = points[at], c2 = points[at + 1], e = points[at + 2];
            at += 3;
            const float dd = std::max(std::hypot(current.x - 2 * c1.x + c2.x, current.y - 2 * c1.y + c2.y),
                                      std::hypot(c1.x - 2 * c2.x + e.x, c1.y - 2 * c2.y + e.y));
            const int n = std::clamp(static_cast<int>(std::ceil(std::sqrt(0.75f * dd / tolerance))), 1, 400);
            for (int i = 1; i <= n; ++i) {
                const float t = static_cast<float>(i) / n, u = 1 - t;
                const float a = u * u * u, b = 3 * u * u * t, c = 3 * u * t * t, d = t * t * t;
                figures.back().points.push_back({a * current.x + b * c1.x + c * c2.x + d * e.x, a * current.y + b * c1.y + c * c2.y + d * e.y});
            }
            current = e;
            break;
        }
        case Verb::close:
            if (!figures.empty()) figures.back().closed = true;
            current = start;
            break;
        }
    }
    return figures;
}

Rect Path::tightBounds() const {
    Rect r;
    bool any = false;
    for (const auto& figure : flatten())
        for (const Point& p : figure.points) {
            if (!any) { r = {p.x, p.y, p.x, p.y}; any = true; continue; }
            r.left = std::min(r.left, p.x); r.top = std::min(r.top, p.y); r.right = std::max(r.right, p.x); r.bottom = std::max(r.bottom, p.y);
        }
    return r;
}

bool Path::contains(float x, float y) const {
    int winding = 0;
    for (const auto& figure : flatten()) {
        const auto& pts = figure.points;
        if (pts.size() < 2) continue;
        for (size_t i = 0; i < pts.size(); ++i) {
            const Point a = pts[i], b = pts[(i + 1) % pts.size()];
            if (a.y <= y) {
                if (b.y > y && (b.x - a.x) * (y - a.y) - (x - a.x) * (b.y - a.y) > 0) ++winding;
            } else if (b.y <= y && (b.x - a.x) * (y - a.y) - (x - a.x) * (b.y - a.y) < 0) {
                --winding;
            }
        }
    }
    return even_odd ? (winding & 1) != 0 : winding != 0;
}

Path Path::dashed(const std::vector<float>& intervals, float phase) const {
    float period = 0;
    for (const float interval : intervals) {
        if (!(interval >= 0) || !std::isfinite(interval)) return *this;
        period += interval;
    }
    if (intervals.size() < 2 || !(period > 0)) return *this;
    std::vector<float> pattern = intervals;
    if (pattern.size() % 2) pattern.insert(pattern.end(), intervals.begin(), intervals.end());
    period = 0;
    for (const float interval : pattern) period += interval;
    Path result;
    for (auto figure : flatten()) {
        if (figure.closed && figure.points.size() > 1) figure.points.push_back(figure.points.front());
        if (figure.points.size() < 2) continue;
        // Where the pattern stands at the figure's start.
        float into = std::fmod(phase, period);
        if (into < 0) into += period;
        size_t index = 0;
        while (into >= pattern[index]) {
            into -= pattern[index];
            index = (index + 1) % pattern.size();
        }
        float left = pattern[index] - into;
        bool on = index % 2 == 0, drawing = false;
        for (size_t i = 0; i + 1 < figure.points.size(); ++i) {
            Point a = figure.points[i];
            const Point b = figure.points[i + 1];
            float remaining = std::hypot(b.x - a.x, b.y - a.y);
            if (remaining <= 0) continue;
            const Point direction{(b.x - a.x) / remaining, (b.y - a.y) / remaining};
            if (on && !drawing) { result.moveTo(a.x, a.y); drawing = true; }
            while (remaining > left) {
                a = {a.x + direction.x * left, a.y + direction.y * left};
                remaining -= left;
                if (on) result.lineTo(a.x, a.y);
                on = !on;
                index = (index + 1) % pattern.size();
                left = pattern[index];
                if (on) { result.moveTo(a.x, a.y); drawing = true; } else drawing = false;
            }
            left -= remaining;
            if (on) result.lineTo(b.x, b.y);
        }
    }
    return result;
}

namespace {
constexpr double clip_scale = 1024.0;

Clipper2Lib::Paths64 toClipper(const Path& path) {
    Clipper2Lib::Paths64 paths;
    for (const auto& figure : path.flatten(0.02f)) {
        if (figure.points.size() < 3) continue;
        Clipper2Lib::Path64 polygon;
        polygon.reserve(figure.points.size());
        for (const Point& p : figure.points)
            polygon.emplace_back(static_cast<int64_t>(std::llround(p.x * clip_scale)), static_cast<int64_t>(std::llround(p.y * clip_scale)));
        paths.push_back(std::move(polygon));
    }
    return Clipper2Lib::Union(paths, path.even_odd ? Clipper2Lib::FillRule::EvenOdd : Clipper2Lib::FillRule::NonZero);
}
} // namespace

std::optional<Path> Path::combine(const Path& a, const Path& b, BoolOp op) {
    const auto subject = toClipper(a), clip = toClipper(b);
    Clipper2Lib::ClipType type = Clipper2Lib::ClipType::Union;
    switch (op) {
    case BoolOp::intersect: type = Clipper2Lib::ClipType::Intersection; break;
    case BoolOp::exclusive_or: type = Clipper2Lib::ClipType::Xor; break;
    case BoolOp::difference: type = Clipper2Lib::ClipType::Difference; break;
    default: break;
    }
    const auto solution = Clipper2Lib::BooleanOp(type, Clipper2Lib::FillRule::NonZero, subject, clip);
    Path result;
    for (const auto& polygon : solution) {
        if (polygon.size() < 3) continue;
        result.moveTo(static_cast<float>(polygon[0].x / clip_scale), static_cast<float>(polygon[0].y / clip_scale));
        for (size_t i = 1; i < polygon.size(); ++i) result.lineTo(static_cast<float>(polygon[i].x / clip_scale), static_cast<float>(polygon[i].y / clip_scale));
        result.close();
    }
    return result;
}

PathMeasure::PathMeasure(const Path& path) {
    for (auto& figure : path.flatten(0.02f)) {
        if (figure.closed && figure.points.size() > 1) figure.points.push_back(figure.points.front());
        if (figure.points.empty()) continue;
        Figure own;
        own.points = std::move(figure.points);
        own.at.push_back(0);
        for (size_t i = 1; i < own.points.size(); ++i)
            own.at.push_back(own.at.back() + std::hypot(own.points[i].x - own.points[i - 1].x, own.points[i].y - own.points[i - 1].y));
        own.length = own.at.back();
        total_ += own.length;
        figures_.push_back(std::move(own));
    }
}

bool PathMeasure::positionAt(float distance, Point* position, Point* tangent) const {
    if (figures_.empty()) return false;
    float left = std::max(0.0f, distance);
    const Figure* chosen = &figures_.back();
    bool found = false;
    for (const Figure& figure : figures_) {
        if (left <= figure.length) { chosen = &figure; found = true; break; }
        left -= figure.length;
    }
    if (!found) left = chosen->length;
    const auto& pts = chosen->points;
    Point at = pts.front(), direction{1, 0};
    for (size_t i = 1; i < pts.size(); ++i) {
        const float segment = chosen->at[i] - chosen->at[i - 1];
        if (segment <= 0) continue;
        direction = {(pts[i].x - pts[i - 1].x) / segment, (pts[i].y - pts[i - 1].y) / segment};
        const float into = left - chosen->at[i - 1];
        at = {pts[i - 1].x + direction.x * std::min(into, segment), pts[i - 1].y + direction.y * std::min(into, segment)};
        if (left <= chosen->at[i]) break;
    }
    if (position) *position = at;
    if (tangent) *tangent = direction;
    return true;
}
} // namespace lm_impeller
