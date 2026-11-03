#pragma once
// Linux only: Direct2D's core interfaces with the Windows SDK's names, values and signatures,
// implemented on Impeller (src/linux/gfx/d2d_*.cpp). Only what the launcher calls is declared;
// the vtables are this port's own, not Windows'.
#include "dcommon.h"

#include <cfloat>
#include <cmath>

struct IWICBitmapSource;
struct IWICBitmap;
struct IDWriteTextFormat;
struct IDWriteTextLayout;
struct IDWriteRenderingParams;

using D2D1_POINT_2F = D2D_POINT_2F;
using D2D1_POINT_2U = D2D_POINT_2U;
using D2D1_POINT_2L = D2D_POINT_2L;
using D2D1_RECT_F = D2D_RECT_F;
using D2D1_RECT_U = D2D_RECT_U;
using D2D1_RECT_L = D2D_RECT_L;
using D2D1_SIZE_F = D2D_SIZE_F;
using D2D1_SIZE_U = D2D_SIZE_U;
using D2D1_COLOR_F = D2D_COLOR_F;
using D2D1_MATRIX_3X2_F = D2D_MATRIX_3X2_F;
using D2D1_MATRIX_4X3_F = D2D_MATRIX_4X3_F;
using D2D1_MATRIX_4X4_F = D2D_MATRIX_4X4_F;
using D2D1_MATRIX_5X4_F = D2D_MATRIX_5X4_F;
using D2D1_VECTOR_2F = D2D_VECTOR_2F;
using D2D1_VECTOR_3F = D2D_VECTOR_3F;
using D2D1_VECTOR_4F = D2D_VECTOR_4F;
using D2D1_TAG = UINT64;

#define D2D1_DEFAULT_FLATTENING_TOLERANCE (0.25f)

// Status codes (winerror.h on Windows).
#ifndef D2DERR_WRONG_STATE
#define D2DERR_WRONG_STATE ((HRESULT)0x88990001)
#define D2DERR_NOT_INITIALIZED ((HRESULT)0x88990002)
#define D2DERR_UNSUPPORTED_OPERATION ((HRESULT)0x88990003)
#define D2DERR_INVALID_CALL ((HRESULT)0x8899000A)
#define D2DERR_RECREATE_TARGET ((HRESULT)0x8899000C)
#define D2DERR_PUSH_POP_UNBALANCED ((HRESULT)0x88990016)
#define D2DERR_BITMAP_CANNOT_DRAW ((HRESULT)0x88990021)
#endif
#ifndef D2DERR_UNSUPPORTED_PIXEL_FORMAT
#define D2DERR_UNSUPPORTED_PIXEL_FORMAT ((HRESULT)0x88982F80) // WINCODEC_ERR_UNSUPPORTEDPIXELFORMAT
#endif

enum D2D1_GAMMA : UINT32 { D2D1_GAMMA_2_2 = 0, D2D1_GAMMA_1_0 = 1 };
enum D2D1_OPACITY_MASK_CONTENT : UINT32 {
    D2D1_OPACITY_MASK_CONTENT_GRAPHICS = 0, D2D1_OPACITY_MASK_CONTENT_TEXT_NATURAL = 1,
    D2D1_OPACITY_MASK_CONTENT_TEXT_GDI_COMPATIBLE = 2,
};
enum D2D1_EXTEND_MODE : UINT32 { D2D1_EXTEND_MODE_CLAMP = 0, D2D1_EXTEND_MODE_WRAP = 1, D2D1_EXTEND_MODE_MIRROR = 2 };
enum D2D1_ANTIALIAS_MODE : UINT32 { D2D1_ANTIALIAS_MODE_PER_PRIMITIVE = 0, D2D1_ANTIALIAS_MODE_ALIASED = 1 };
enum D2D1_TEXT_ANTIALIAS_MODE : UINT32 {
    D2D1_TEXT_ANTIALIAS_MODE_DEFAULT = 0, D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE = 1,
    D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE = 2, D2D1_TEXT_ANTIALIAS_MODE_ALIASED = 3,
};
enum D2D1_BITMAP_INTERPOLATION_MODE : UINT32 {
    D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR = 0, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR = 1,
};
enum D2D1_DRAW_TEXT_OPTIONS : UINT32 {
    D2D1_DRAW_TEXT_OPTIONS_NONE = 0, D2D1_DRAW_TEXT_OPTIONS_NO_SNAP = 1, D2D1_DRAW_TEXT_OPTIONS_CLIP = 2,
    D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT = 4, D2D1_DRAW_TEXT_OPTIONS_DISABLE_COLOR_BITMAP_SNAPPING = 8,
};
enum D2D1_ARC_SIZE : UINT32 { D2D1_ARC_SIZE_SMALL = 0, D2D1_ARC_SIZE_LARGE = 1 };
enum D2D1_CAP_STYLE : UINT32 { D2D1_CAP_STYLE_FLAT = 0, D2D1_CAP_STYLE_SQUARE = 1, D2D1_CAP_STYLE_ROUND = 2, D2D1_CAP_STYLE_TRIANGLE = 3 };
enum D2D1_DASH_STYLE : UINT32 {
    D2D1_DASH_STYLE_SOLID = 0, D2D1_DASH_STYLE_DASH = 1, D2D1_DASH_STYLE_DOT = 2, D2D1_DASH_STYLE_DASH_DOT = 3,
    D2D1_DASH_STYLE_DASH_DOT_DOT = 4, D2D1_DASH_STYLE_CUSTOM = 5,
};
enum D2D1_LINE_JOIN : UINT32 { D2D1_LINE_JOIN_MITER = 0, D2D1_LINE_JOIN_BEVEL = 1, D2D1_LINE_JOIN_ROUND = 2, D2D1_LINE_JOIN_MITER_OR_BEVEL = 3 };
enum D2D1_COMBINE_MODE : UINT32 { D2D1_COMBINE_MODE_UNION = 0, D2D1_COMBINE_MODE_INTERSECT = 1, D2D1_COMBINE_MODE_XOR = 2, D2D1_COMBINE_MODE_EXCLUDE = 3 };
enum D2D1_FIGURE_BEGIN : UINT32 { D2D1_FIGURE_BEGIN_FILLED = 0, D2D1_FIGURE_BEGIN_HOLLOW = 1 };
enum D2D1_FIGURE_END : UINT32 { D2D1_FIGURE_END_OPEN = 0, D2D1_FIGURE_END_CLOSED = 1 };
enum D2D1_SWEEP_DIRECTION : UINT32 { D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE = 0, D2D1_SWEEP_DIRECTION_CLOCKWISE = 1 };
enum D2D1_FILL_MODE : UINT32 { D2D1_FILL_MODE_ALTERNATE = 0, D2D1_FILL_MODE_WINDING = 1 };
enum D2D1_LAYER_OPTIONS : UINT32 { D2D1_LAYER_OPTIONS_NONE = 0, D2D1_LAYER_OPTIONS_INITIALIZE_FOR_CLEARTYPE = 1 };
enum D2D1_RENDER_TARGET_TYPE : UINT32 {
    D2D1_RENDER_TARGET_TYPE_DEFAULT = 0, D2D1_RENDER_TARGET_TYPE_SOFTWARE = 1, D2D1_RENDER_TARGET_TYPE_HARDWARE = 2,
};
enum D2D1_FEATURE_LEVEL : UINT32 { D2D1_FEATURE_LEVEL_DEFAULT = 0, D2D1_FEATURE_LEVEL_9 = 0x9100, D2D1_FEATURE_LEVEL_10 = 0xa000 };
enum D2D1_RENDER_TARGET_USAGE : UINT32 {
    D2D1_RENDER_TARGET_USAGE_NONE = 0, D2D1_RENDER_TARGET_USAGE_FORCE_BITMAP_REMOTING = 1,
    D2D1_RENDER_TARGET_USAGE_GDI_COMPATIBLE = 2,
};
enum D2D1_PRESENT_OPTIONS : UINT32 {
    D2D1_PRESENT_OPTIONS_NONE = 0, D2D1_PRESENT_OPTIONS_RETAIN_CONTENTS = 1, D2D1_PRESENT_OPTIONS_IMMEDIATELY = 2,
};
enum D2D1_COMPATIBLE_RENDER_TARGET_OPTIONS : UINT32 {
    D2D1_COMPATIBLE_RENDER_TARGET_OPTIONS_NONE = 0, D2D1_COMPATIBLE_RENDER_TARGET_OPTIONS_GDI_COMPATIBLE = 1,
};
enum D2D1_FACTORY_TYPE : UINT32 { D2D1_FACTORY_TYPE_SINGLE_THREADED = 0, D2D1_FACTORY_TYPE_MULTI_THREADED = 1 };
enum D2D1_DEBUG_LEVEL : UINT32 {
    D2D1_DEBUG_LEVEL_NONE = 0, D2D1_DEBUG_LEVEL_ERROR = 1, D2D1_DEBUG_LEVEL_WARNING = 2, D2D1_DEBUG_LEVEL_INFORMATION = 3,
};

struct ID2D1Geometry;
struct ID2D1Brush;
struct ID2D1Bitmap;
struct ID2D1RenderTarget;
struct ID2D1BitmapRenderTarget;
struct ID2D1Factory;

struct D2D1_BITMAP_PROPERTIES { D2D1_PIXEL_FORMAT pixelFormat; FLOAT dpiX; FLOAT dpiY; };
struct D2D1_GRADIENT_STOP { FLOAT position; D2D1_COLOR_F color; };
struct D2D1_BRUSH_PROPERTIES { FLOAT opacity; D2D1_MATRIX_3X2_F transform; };
struct D2D1_BITMAP_BRUSH_PROPERTIES {
    D2D1_EXTEND_MODE extendModeX; D2D1_EXTEND_MODE extendModeY; D2D1_BITMAP_INTERPOLATION_MODE interpolationMode;
};
struct D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES { D2D1_POINT_2F startPoint; D2D1_POINT_2F endPoint; };
struct D2D1_RADIAL_GRADIENT_BRUSH_PROPERTIES {
    D2D1_POINT_2F center; D2D1_POINT_2F gradientOriginOffset; FLOAT radiusX; FLOAT radiusY;
};
struct D2D1_BEZIER_SEGMENT { D2D1_POINT_2F point1; D2D1_POINT_2F point2; D2D1_POINT_2F point3; };
struct D2D1_QUADRATIC_BEZIER_SEGMENT { D2D1_POINT_2F point1; D2D1_POINT_2F point2; };
struct D2D1_ARC_SEGMENT {
    D2D1_POINT_2F point; D2D1_SIZE_F size; FLOAT rotationAngle; D2D1_SWEEP_DIRECTION sweepDirection; D2D1_ARC_SIZE arcSize;
};
struct D2D1_ELLIPSE { D2D1_POINT_2F point; FLOAT radiusX; FLOAT radiusY; };
struct D2D1_ROUNDED_RECT { D2D1_RECT_F rect; FLOAT radiusX; FLOAT radiusY; };
struct D2D1_STROKE_STYLE_PROPERTIES {
    D2D1_CAP_STYLE startCap; D2D1_CAP_STYLE endCap; D2D1_CAP_STYLE dashCap; D2D1_LINE_JOIN lineJoin;
    FLOAT miterLimit; D2D1_DASH_STYLE dashStyle; FLOAT dashOffset;
};
struct D2D1_LAYER_PARAMETERS {
    D2D1_RECT_F contentBounds; ID2D1Geometry* geometricMask; D2D1_ANTIALIAS_MODE maskAntialiasMode;
    D2D1_MATRIX_3X2_F maskTransform; FLOAT opacity; ID2D1Brush* opacityBrush; D2D1_LAYER_OPTIONS layerOptions;
};
struct D2D1_RENDER_TARGET_PROPERTIES {
    D2D1_RENDER_TARGET_TYPE type; D2D1_PIXEL_FORMAT pixelFormat; FLOAT dpiX; FLOAT dpiY;
    D2D1_RENDER_TARGET_USAGE usage; D2D1_FEATURE_LEVEL minLevel;
};
struct D2D1_HWND_RENDER_TARGET_PROPERTIES { HWND hwnd; D2D1_SIZE_U pixelSize; D2D1_PRESENT_OPTIONS presentOptions; };
struct D2D1_FACTORY_OPTIONS { D2D1_DEBUG_LEVEL debugLevel; };

// ---- Resources ---------------------------------------------------------------------------

struct ID2D1Resource : public IUnknown {
    LM_IID(0x2cd90691, 0x12e2, 0x11dc, 0x9f, 0xed, 0x00, 0x11, 0x43, 0xa0, 0x55, 0xf9);
    using Base = IUnknown;
};

// A producer of pixels: a bitmap or an effect's output.
struct ID2D1Image : public ID2D1Resource {
    LM_IID(0x65019f75, 0x8da2, 0x497c, 0xb3, 0x2c, 0xdf, 0xa3, 0x4e, 0x48, 0xed, 0xe6);
    using Base = ID2D1Resource;
};

struct ID2D1Bitmap : public ID2D1Image {
    LM_IID(0xa2296057, 0xea42, 0x4099, 0x98, 0x3b, 0x53, 0x9f, 0xb6, 0x50, 0x54, 0x26);
    using Base = ID2D1Image;
    virtual D2D1_SIZE_F STDMETHODCALLTYPE GetSize() const = 0;
    virtual D2D1_SIZE_U STDMETHODCALLTYPE GetPixelSize() const = 0;
    virtual D2D1_PIXEL_FORMAT STDMETHODCALLTYPE GetPixelFormat() const = 0;
    virtual HRESULT STDMETHODCALLTYPE CopyFromBitmap(const D2D1_POINT_2U* destPoint, ID2D1Bitmap* bitmap, const D2D1_RECT_U* srcRect) = 0;
    virtual HRESULT STDMETHODCALLTYPE CopyFromRenderTarget(const D2D1_POINT_2U* destPoint, ID2D1RenderTarget* renderTarget,
                                                           const D2D1_RECT_U* srcRect) = 0;
    virtual HRESULT STDMETHODCALLTYPE CopyFromMemory(const D2D1_RECT_U* dstRect, const void* srcData, UINT32 pitch) = 0;
};

struct ID2D1GradientStopCollection : public ID2D1Resource {
    LM_IID(0x2cd906a7, 0x12e2, 0x11dc, 0x9f, 0xed, 0x00, 0x11, 0x43, 0xa0, 0x55, 0xf9);
    using Base = ID2D1Resource;
    virtual UINT32 STDMETHODCALLTYPE GetGradientStopCount() const = 0;
};

struct ID2D1Brush : public ID2D1Resource {
    LM_IID(0x2cd906a8, 0x12e2, 0x11dc, 0x9f, 0xed, 0x00, 0x11, 0x43, 0xa0, 0x55, 0xf9);
    using Base = ID2D1Resource;
    virtual void STDMETHODCALLTYPE SetOpacity(FLOAT opacity) = 0;
    virtual void STDMETHODCALLTYPE SetTransform(const D2D1_MATRIX_3X2_F* transform) = 0;
    virtual FLOAT STDMETHODCALLTYPE GetOpacity() const = 0;
    virtual void STDMETHODCALLTYPE GetTransform(D2D1_MATRIX_3X2_F* transform) const = 0;
    void SetTransform(const D2D1_MATRIX_3X2_F& transform) { SetTransform(&transform); }
};

struct ID2D1BitmapBrush : public ID2D1Brush {
    LM_IID(0x2cd906aa, 0x12e2, 0x11dc, 0x9f, 0xed, 0x00, 0x11, 0x43, 0xa0, 0x55, 0xf9);
    using Base = ID2D1Brush;
    virtual void STDMETHODCALLTYPE SetExtendModeX(D2D1_EXTEND_MODE extendModeX) = 0;
    virtual void STDMETHODCALLTYPE SetExtendModeY(D2D1_EXTEND_MODE extendModeY) = 0;
    virtual void STDMETHODCALLTYPE SetInterpolationMode(D2D1_BITMAP_INTERPOLATION_MODE interpolationMode) = 0;
    virtual void STDMETHODCALLTYPE SetBitmap(ID2D1Bitmap* bitmap) = 0;
};

struct ID2D1SolidColorBrush : public ID2D1Brush {
    LM_IID(0x2cd906a9, 0x12e2, 0x11dc, 0x9f, 0xed, 0x00, 0x11, 0x43, 0xa0, 0x55, 0xf9);
    using Base = ID2D1Brush;
    virtual void STDMETHODCALLTYPE SetColor(const D2D1_COLOR_F* color) = 0;
    virtual D2D1_COLOR_F STDMETHODCALLTYPE GetColor() const = 0;
    void SetColor(const D2D1_COLOR_F& color) { SetColor(&color); }
};

struct ID2D1LinearGradientBrush : public ID2D1Brush {
    LM_IID(0x2cd906ab, 0x12e2, 0x11dc, 0x9f, 0xed, 0x00, 0x11, 0x43, 0xa0, 0x55, 0xf9);
    using Base = ID2D1Brush;
    virtual void STDMETHODCALLTYPE SetStartPoint(D2D1_POINT_2F startPoint) = 0;
    virtual void STDMETHODCALLTYPE SetEndPoint(D2D1_POINT_2F endPoint) = 0;
    virtual D2D1_POINT_2F STDMETHODCALLTYPE GetStartPoint() const = 0;
    virtual D2D1_POINT_2F STDMETHODCALLTYPE GetEndPoint() const = 0;
};

struct ID2D1RadialGradientBrush : public ID2D1Brush {
    LM_IID(0x2cd906ac, 0x12e2, 0x11dc, 0x9f, 0xed, 0x00, 0x11, 0x43, 0xa0, 0x55, 0xf9);
    using Base = ID2D1Brush;
    virtual void STDMETHODCALLTYPE SetCenter(D2D1_POINT_2F center) = 0;
    virtual void STDMETHODCALLTYPE SetGradientOriginOffset(D2D1_POINT_2F gradientOriginOffset) = 0;
    virtual void STDMETHODCALLTYPE SetRadiusX(FLOAT radiusX) = 0;
    virtual void STDMETHODCALLTYPE SetRadiusY(FLOAT radiusY) = 0;
    virtual D2D1_POINT_2F STDMETHODCALLTYPE GetCenter() const = 0;
    virtual D2D1_POINT_2F STDMETHODCALLTYPE GetGradientOriginOffset() const = 0;
    virtual FLOAT STDMETHODCALLTYPE GetRadiusX() const = 0;
    virtual FLOAT STDMETHODCALLTYPE GetRadiusY() const = 0;
};

struct ID2D1StrokeStyle : public ID2D1Resource {
    LM_IID(0x2cd9069d, 0x12e2, 0x11dc, 0x9f, 0xed, 0x00, 0x11, 0x43, 0xa0, 0x55, 0xf9);
    using Base = ID2D1Resource;
};

// ---- Geometry ----------------------------------------------------------------------------

struct ID2D1SimplifiedGeometrySink : public IUnknown {
    LM_IID(0x2cd9069e, 0x12e2, 0x11dc, 0x9f, 0xed, 0x00, 0x11, 0x43, 0xa0, 0x55, 0xf9);
    using Base = IUnknown;
    virtual void STDMETHODCALLTYPE SetFillMode(D2D1_FILL_MODE fillMode) = 0;
    virtual void STDMETHODCALLTYPE BeginFigure(D2D1_POINT_2F startPoint, D2D1_FIGURE_BEGIN figureBegin) = 0;
    virtual void STDMETHODCALLTYPE AddLines(const D2D1_POINT_2F* points, UINT32 pointsCount) = 0;
    virtual void STDMETHODCALLTYPE AddBeziers(const D2D1_BEZIER_SEGMENT* beziers, UINT32 beziersCount) = 0;
    virtual void STDMETHODCALLTYPE EndFigure(D2D1_FIGURE_END figureEnd) = 0;
    virtual HRESULT STDMETHODCALLTYPE Close() = 0;
};

struct ID2D1GeometrySink : public ID2D1SimplifiedGeometrySink {
    LM_IID(0x2cd9069f, 0x12e2, 0x11dc, 0x9f, 0xed, 0x00, 0x11, 0x43, 0xa0, 0x55, 0xf9);
    using Base = ID2D1SimplifiedGeometrySink;
    virtual void STDMETHODCALLTYPE AddLine(D2D1_POINT_2F point) = 0;
    virtual void STDMETHODCALLTYPE AddBezier(const D2D1_BEZIER_SEGMENT* bezier) = 0;
    virtual void STDMETHODCALLTYPE AddQuadraticBezier(const D2D1_QUADRATIC_BEZIER_SEGMENT* bezier) = 0;
    virtual void STDMETHODCALLTYPE AddQuadraticBeziers(const D2D1_QUADRATIC_BEZIER_SEGMENT* beziers, UINT32 beziersCount) = 0;
    virtual void STDMETHODCALLTYPE AddArc(const D2D1_ARC_SEGMENT* arc) = 0;
    void AddBezier(const D2D1_BEZIER_SEGMENT& bezier) { AddBezier(&bezier); }
    void AddQuadraticBezier(const D2D1_QUADRATIC_BEZIER_SEGMENT& bezier) { AddQuadraticBezier(&bezier); }
    void AddArc(const D2D1_ARC_SEGMENT& arc) { AddArc(&arc); }
};

struct ID2D1Geometry : public ID2D1Resource {
    LM_IID(0x2cd906a1, 0x12e2, 0x11dc, 0x9f, 0xed, 0x00, 0x11, 0x43, 0xa0, 0x55, 0xf9);
    using Base = ID2D1Resource;
    virtual HRESULT STDMETHODCALLTYPE GetBounds(const D2D1_MATRIX_3X2_F* worldTransform, D2D1_RECT_F* bounds) const = 0;
    virtual HRESULT STDMETHODCALLTYPE FillContainsPoint(D2D1_POINT_2F point, const D2D1_MATRIX_3X2_F* worldTransform,
                                                        FLOAT flatteningTolerance, BOOL* contains) const = 0;
    virtual HRESULT STDMETHODCALLTYPE CombineWithGeometry(ID2D1Geometry* inputGeometry, D2D1_COMBINE_MODE combineMode,
                                                          const D2D1_MATRIX_3X2_F* inputGeometryTransform, FLOAT flatteningTolerance,
                                                          ID2D1SimplifiedGeometrySink* geometrySink) const = 0;
    virtual HRESULT STDMETHODCALLTYPE ComputeLength(const D2D1_MATRIX_3X2_F* worldTransform, FLOAT flatteningTolerance,
                                                    FLOAT* length) const = 0;
    virtual HRESULT STDMETHODCALLTYPE ComputePointAtLength(FLOAT length, const D2D1_MATRIX_3X2_F* worldTransform, FLOAT flatteningTolerance,
                                                           D2D1_POINT_2F* point, D2D1_POINT_2F* unitTangentVector) const = 0;

    HRESULT GetBounds(const D2D1_MATRIX_3X2_F& worldTransform, D2D1_RECT_F* bounds) const { return GetBounds(&worldTransform, bounds); }
    HRESULT FillContainsPoint(D2D1_POINT_2F point, const D2D1_MATRIX_3X2_F& worldTransform, FLOAT flatteningTolerance, BOOL* contains) const {
        return FillContainsPoint(point, &worldTransform, flatteningTolerance, contains);
    }
    HRESULT FillContainsPoint(D2D1_POINT_2F point, const D2D1_MATRIX_3X2_F* worldTransform, BOOL* contains) const {
        return FillContainsPoint(point, worldTransform, D2D1_DEFAULT_FLATTENING_TOLERANCE, contains);
    }
    HRESULT FillContainsPoint(D2D1_POINT_2F point, const D2D1_MATRIX_3X2_F& worldTransform, BOOL* contains) const {
        return FillContainsPoint(point, &worldTransform, D2D1_DEFAULT_FLATTENING_TOLERANCE, contains);
    }
    HRESULT CombineWithGeometry(ID2D1Geometry* inputGeometry, D2D1_COMBINE_MODE combineMode, const D2D1_MATRIX_3X2_F& inputGeometryTransform,
                                FLOAT flatteningTolerance, ID2D1SimplifiedGeometrySink* geometrySink) const {
        return CombineWithGeometry(inputGeometry, combineMode, &inputGeometryTransform, flatteningTolerance, geometrySink);
    }
    HRESULT CombineWithGeometry(ID2D1Geometry* inputGeometry, D2D1_COMBINE_MODE combineMode, const D2D1_MATRIX_3X2_F* inputGeometryTransform,
                                ID2D1SimplifiedGeometrySink* geometrySink) const {
        return CombineWithGeometry(inputGeometry, combineMode, inputGeometryTransform, D2D1_DEFAULT_FLATTENING_TOLERANCE, geometrySink);
    }
    HRESULT CombineWithGeometry(ID2D1Geometry* inputGeometry, D2D1_COMBINE_MODE combineMode, const D2D1_MATRIX_3X2_F& inputGeometryTransform,
                                ID2D1SimplifiedGeometrySink* geometrySink) const {
        return CombineWithGeometry(inputGeometry, combineMode, &inputGeometryTransform, D2D1_DEFAULT_FLATTENING_TOLERANCE, geometrySink);
    }
    HRESULT ComputeLength(const D2D1_MATRIX_3X2_F& worldTransform, FLOAT flatteningTolerance, FLOAT* length) const {
        return ComputeLength(&worldTransform, flatteningTolerance, length);
    }
    HRESULT ComputeLength(const D2D1_MATRIX_3X2_F* worldTransform, FLOAT* length) const {
        return ComputeLength(worldTransform, D2D1_DEFAULT_FLATTENING_TOLERANCE, length);
    }
    HRESULT ComputeLength(const D2D1_MATRIX_3X2_F& worldTransform, FLOAT* length) const {
        return ComputeLength(&worldTransform, D2D1_DEFAULT_FLATTENING_TOLERANCE, length);
    }
    HRESULT ComputePointAtLength(FLOAT length, const D2D1_MATRIX_3X2_F& worldTransform, FLOAT flatteningTolerance,
                                 D2D1_POINT_2F* point, D2D1_POINT_2F* unitTangentVector) const {
        return ComputePointAtLength(length, &worldTransform, flatteningTolerance, point, unitTangentVector);
    }
    HRESULT ComputePointAtLength(FLOAT length, const D2D1_MATRIX_3X2_F* worldTransform, D2D1_POINT_2F* point,
                                 D2D1_POINT_2F* unitTangentVector) const {
        return ComputePointAtLength(length, worldTransform, D2D1_DEFAULT_FLATTENING_TOLERANCE, point, unitTangentVector);
    }
    HRESULT ComputePointAtLength(FLOAT length, const D2D1_MATRIX_3X2_F& worldTransform, D2D1_POINT_2F* point,
                                 D2D1_POINT_2F* unitTangentVector) const {
        return ComputePointAtLength(length, &worldTransform, D2D1_DEFAULT_FLATTENING_TOLERANCE, point, unitTangentVector);
    }
};

struct ID2D1RectangleGeometry : public ID2D1Geometry {
    LM_IID(0x2cd906a2, 0x12e2, 0x11dc, 0x9f, 0xed, 0x00, 0x11, 0x43, 0xa0, 0x55, 0xf9);
    using Base = ID2D1Geometry;
};
struct ID2D1RoundedRectangleGeometry : public ID2D1Geometry {
    LM_IID(0x2cd906a3, 0x12e2, 0x11dc, 0x9f, 0xed, 0x00, 0x11, 0x43, 0xa0, 0x55, 0xf9);
    using Base = ID2D1Geometry;
};
struct ID2D1EllipseGeometry : public ID2D1Geometry {
    LM_IID(0x2cd906a4, 0x12e2, 0x11dc, 0x9f, 0xed, 0x00, 0x11, 0x43, 0xa0, 0x55, 0xf9);
    using Base = ID2D1Geometry;
};
struct ID2D1GeometryGroup : public ID2D1Geometry {
    LM_IID(0x2cd906a6, 0x12e2, 0x11dc, 0x9f, 0xed, 0x00, 0x11, 0x43, 0xa0, 0x55, 0xf9);
    using Base = ID2D1Geometry;
};
struct ID2D1TransformedGeometry : public ID2D1Geometry {
    LM_IID(0x2cd906bb, 0x12e2, 0x11dc, 0x9f, 0xed, 0x00, 0x11, 0x43, 0xa0, 0x55, 0xf9);
    using Base = ID2D1Geometry;
};
struct ID2D1PathGeometry : public ID2D1Geometry {
    LM_IID(0x2cd906a5, 0x12e2, 0x11dc, 0x9f, 0xed, 0x00, 0x11, 0x43, 0xa0, 0x55, 0xf9);
    using Base = ID2D1Geometry;
    virtual HRESULT STDMETHODCALLTYPE Open(ID2D1GeometrySink** geometrySink) = 0;
};

struct ID2D1Layer : public ID2D1Resource {
    LM_IID(0x2cd9069b, 0x12e2, 0x11dc, 0x9f, 0xed, 0x00, 0x11, 0x43, 0xa0, 0x55, 0xf9);
    using Base = ID2D1Resource;
};

// ---- Render targets ----------------------------------------------------------------------

// winuser.h names DrawText a macro for DrawTextW: the method keeps its name here and answers
// both spellings.
#pragma push_macro("DrawText")
#undef DrawText
struct ID2D1RenderTarget : public ID2D1Resource {
    LM_IID(0x2cd90694, 0x12e2, 0x11dc, 0x9f, 0xed, 0x00, 0x11, 0x43, 0xa0, 0x55, 0xf9);
    using Base = ID2D1Resource;
    virtual HRESULT STDMETHODCALLTYPE CreateBitmap(D2D1_SIZE_U size, const void* srcData, UINT32 pitch,
                                                   const D2D1_BITMAP_PROPERTIES* bitmapProperties, ID2D1Bitmap** bitmap) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateBitmapFromWicBitmap(IWICBitmapSource* wicBitmapSource, const D2D1_BITMAP_PROPERTIES* bitmapProperties,
                                                                ID2D1Bitmap** bitmap) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateBitmapBrush(ID2D1Bitmap* bitmap, const D2D1_BITMAP_BRUSH_PROPERTIES* bitmapBrushProperties,
                                                        const D2D1_BRUSH_PROPERTIES* brushProperties, ID2D1BitmapBrush** bitmapBrush) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateSolidColorBrush(const D2D1_COLOR_F* color, const D2D1_BRUSH_PROPERTIES* brushProperties,
                                                            ID2D1SolidColorBrush** solidColorBrush) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateGradientStopCollection(const D2D1_GRADIENT_STOP* gradientStops, UINT32 gradientStopsCount,
                                                                   D2D1_GAMMA colorInterpolationGamma, D2D1_EXTEND_MODE extendMode,
                                                                   ID2D1GradientStopCollection** gradientStopCollection) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateLinearGradientBrush(const D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES* linearGradientBrushProperties,
                                                                const D2D1_BRUSH_PROPERTIES* brushProperties,
                                                                ID2D1GradientStopCollection* gradientStopCollection,
                                                                ID2D1LinearGradientBrush** linearGradientBrush) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateRadialGradientBrush(const D2D1_RADIAL_GRADIENT_BRUSH_PROPERTIES* radialGradientBrushProperties,
                                                                const D2D1_BRUSH_PROPERTIES* brushProperties,
                                                                ID2D1GradientStopCollection* gradientStopCollection,
                                                                ID2D1RadialGradientBrush** radialGradientBrush) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateCompatibleRenderTarget(const D2D1_SIZE_F* desiredSize, const D2D1_SIZE_U* desiredPixelSize,
                                                                   const D2D1_PIXEL_FORMAT* desiredFormat, D2D1_COMPATIBLE_RENDER_TARGET_OPTIONS options,
                                                                   ID2D1BitmapRenderTarget** bitmapRenderTarget) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateLayer(const D2D1_SIZE_F* size, ID2D1Layer** layer) = 0;
    virtual void STDMETHODCALLTYPE DrawLine(D2D1_POINT_2F point0, D2D1_POINT_2F point1, ID2D1Brush* brush, FLOAT strokeWidth = 1.0f,
                                            ID2D1StrokeStyle* strokeStyle = nullptr) = 0;
    virtual void STDMETHODCALLTYPE DrawRectangle(const D2D1_RECT_F* rect, ID2D1Brush* brush, FLOAT strokeWidth = 1.0f,
                                                 ID2D1StrokeStyle* strokeStyle = nullptr) = 0;
    virtual void STDMETHODCALLTYPE FillRectangle(const D2D1_RECT_F* rect, ID2D1Brush* brush) = 0;
    virtual void STDMETHODCALLTYPE DrawRoundedRectangle(const D2D1_ROUNDED_RECT* roundedRect, ID2D1Brush* brush, FLOAT strokeWidth = 1.0f,
                                                        ID2D1StrokeStyle* strokeStyle = nullptr) = 0;
    virtual void STDMETHODCALLTYPE FillRoundedRectangle(const D2D1_ROUNDED_RECT* roundedRect, ID2D1Brush* brush) = 0;
    virtual void STDMETHODCALLTYPE DrawEllipse(const D2D1_ELLIPSE* ellipse, ID2D1Brush* brush, FLOAT strokeWidth = 1.0f,
                                               ID2D1StrokeStyle* strokeStyle = nullptr) = 0;
    virtual void STDMETHODCALLTYPE FillEllipse(const D2D1_ELLIPSE* ellipse, ID2D1Brush* brush) = 0;
    virtual void STDMETHODCALLTYPE DrawGeometry(ID2D1Geometry* geometry, ID2D1Brush* brush, FLOAT strokeWidth = 1.0f,
                                                ID2D1StrokeStyle* strokeStyle = nullptr) = 0;
    virtual void STDMETHODCALLTYPE FillGeometry(ID2D1Geometry* geometry, ID2D1Brush* brush, ID2D1Brush* opacityBrush = nullptr) = 0;
    virtual void STDMETHODCALLTYPE FillOpacityMask(ID2D1Bitmap* opacityMask, ID2D1Brush* brush, D2D1_OPACITY_MASK_CONTENT content,
                                                   const D2D1_RECT_F* destinationRectangle = nullptr,
                                                   const D2D1_RECT_F* sourceRectangle = nullptr) = 0;
    virtual void STDMETHODCALLTYPE DrawBitmap(ID2D1Bitmap* bitmap, const D2D1_RECT_F* destinationRectangle = nullptr, FLOAT opacity = 1.0f,
                                              D2D1_BITMAP_INTERPOLATION_MODE interpolationMode = D2D1_BITMAP_INTERPOLATION_MODE_LINEAR,
                                              const D2D1_RECT_F* sourceRectangle = nullptr) = 0;
    virtual void STDMETHODCALLTYPE DrawText(const WCHAR* string, UINT32 stringLength, IDWriteTextFormat* textFormat,
                                            const D2D1_RECT_F* layoutRect, ID2D1Brush* defaultFillBrush,
                                            D2D1_DRAW_TEXT_OPTIONS options = D2D1_DRAW_TEXT_OPTIONS_NONE,
                                            DWRITE_MEASURING_MODE measuringMode = DWRITE_MEASURING_MODE_NATURAL) = 0;
    virtual void STDMETHODCALLTYPE DrawTextLayout(D2D1_POINT_2F origin, IDWriteTextLayout* textLayout, ID2D1Brush* defaultFillBrush,
                                                  D2D1_DRAW_TEXT_OPTIONS options = D2D1_DRAW_TEXT_OPTIONS_NONE) = 0;
    virtual void STDMETHODCALLTYPE SetTransform(const D2D1_MATRIX_3X2_F* transform) = 0;
    virtual void STDMETHODCALLTYPE GetTransform(D2D1_MATRIX_3X2_F* transform) const = 0;
    virtual void STDMETHODCALLTYPE SetAntialiasMode(D2D1_ANTIALIAS_MODE antialiasMode) = 0;
    virtual D2D1_ANTIALIAS_MODE STDMETHODCALLTYPE GetAntialiasMode() const = 0;
    virtual void STDMETHODCALLTYPE SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE textAntialiasMode) = 0;
    virtual D2D1_TEXT_ANTIALIAS_MODE STDMETHODCALLTYPE GetTextAntialiasMode() const = 0;
    virtual void STDMETHODCALLTYPE PushLayer(const D2D1_LAYER_PARAMETERS* layerParameters, ID2D1Layer* layer) = 0;
    virtual void STDMETHODCALLTYPE PopLayer() = 0;
    virtual void STDMETHODCALLTYPE PushAxisAlignedClip(const D2D1_RECT_F* clipRect, D2D1_ANTIALIAS_MODE antialiasMode) = 0;
    virtual void STDMETHODCALLTYPE PopAxisAlignedClip() = 0;
    virtual void STDMETHODCALLTYPE Clear(const D2D1_COLOR_F* clearColor = nullptr) = 0;
    virtual void STDMETHODCALLTYPE BeginDraw() = 0;
    virtual HRESULT STDMETHODCALLTYPE EndDraw(D2D1_TAG* tag1 = nullptr, D2D1_TAG* tag2 = nullptr) = 0;
    virtual D2D1_PIXEL_FORMAT STDMETHODCALLTYPE GetPixelFormat() const = 0;
    virtual void STDMETHODCALLTYPE GetDpi(FLOAT* dpiX, FLOAT* dpiY) const = 0;
    virtual D2D1_SIZE_F STDMETHODCALLTYPE GetSize() const = 0;
    virtual D2D1_SIZE_U STDMETHODCALLTYPE GetPixelSize() const = 0;

    HRESULT CreateBitmap(D2D1_SIZE_U size, const void* srcData, UINT32 pitch, const D2D1_BITMAP_PROPERTIES& bitmapProperties, ID2D1Bitmap** bitmap) {
        return CreateBitmap(size, srcData, pitch, &bitmapProperties, bitmap);
    }
    HRESULT CreateBitmap(D2D1_SIZE_U size, const D2D1_BITMAP_PROPERTIES& bitmapProperties, ID2D1Bitmap** bitmap) {
        return CreateBitmap(size, nullptr, 0, &bitmapProperties, bitmap);
    }
    HRESULT CreateBitmapFromWicBitmap(IWICBitmapSource* wicBitmapSource, const D2D1_BITMAP_PROPERTIES& bitmapProperties, ID2D1Bitmap** bitmap) {
        return CreateBitmapFromWicBitmap(wicBitmapSource, &bitmapProperties, bitmap);
    }
    HRESULT CreateBitmapFromWicBitmap(IWICBitmapSource* wicBitmapSource, ID2D1Bitmap** bitmap) {
        return CreateBitmapFromWicBitmap(wicBitmapSource, nullptr, bitmap);
    }
    HRESULT CreateBitmapBrush(ID2D1Bitmap* bitmap, ID2D1BitmapBrush** bitmapBrush) { return CreateBitmapBrush(bitmap, nullptr, nullptr, bitmapBrush); }
    HRESULT CreateBitmapBrush(ID2D1Bitmap* bitmap, const D2D1_BITMAP_BRUSH_PROPERTIES& bitmapBrushProperties, ID2D1BitmapBrush** bitmapBrush) {
        return CreateBitmapBrush(bitmap, &bitmapBrushProperties, nullptr, bitmapBrush);
    }
    HRESULT CreateBitmapBrush(ID2D1Bitmap* bitmap, const D2D1_BITMAP_BRUSH_PROPERTIES& bitmapBrushProperties,
                              const D2D1_BRUSH_PROPERTIES& brushProperties, ID2D1BitmapBrush** bitmapBrush) {
        return CreateBitmapBrush(bitmap, &bitmapBrushProperties, &brushProperties, bitmapBrush);
    }
    HRESULT CreateSolidColorBrush(const D2D1_COLOR_F& color, ID2D1SolidColorBrush** solidColorBrush) {
        return CreateSolidColorBrush(&color, nullptr, solidColorBrush);
    }
    HRESULT CreateSolidColorBrush(const D2D1_COLOR_F& color, const D2D1_BRUSH_PROPERTIES& brushProperties, ID2D1SolidColorBrush** solidColorBrush) {
        return CreateSolidColorBrush(&color, &brushProperties, solidColorBrush);
    }
    HRESULT CreateGradientStopCollection(const D2D1_GRADIENT_STOP* gradientStops, UINT32 gradientStopsCount,
                                         ID2D1GradientStopCollection** gradientStopCollection) {
        return CreateGradientStopCollection(gradientStops, gradientStopsCount, D2D1_GAMMA_2_2, D2D1_EXTEND_MODE_CLAMP, gradientStopCollection);
    }
    HRESULT CreateLinearGradientBrush(const D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES& linearGradientBrushProperties,
                                      ID2D1GradientStopCollection* gradientStopCollection, ID2D1LinearGradientBrush** linearGradientBrush) {
        return CreateLinearGradientBrush(&linearGradientBrushProperties, nullptr, gradientStopCollection, linearGradientBrush);
    }
    HRESULT CreateLinearGradientBrush(const D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES& linearGradientBrushProperties,
                                      const D2D1_BRUSH_PROPERTIES& brushProperties, ID2D1GradientStopCollection* gradientStopCollection,
                                      ID2D1LinearGradientBrush** linearGradientBrush) {
        return CreateLinearGradientBrush(&linearGradientBrushProperties, &brushProperties, gradientStopCollection, linearGradientBrush);
    }
    HRESULT CreateRadialGradientBrush(const D2D1_RADIAL_GRADIENT_BRUSH_PROPERTIES& radialGradientBrushProperties,
                                      ID2D1GradientStopCollection* gradientStopCollection, ID2D1RadialGradientBrush** radialGradientBrush) {
        return CreateRadialGradientBrush(&radialGradientBrushProperties, nullptr, gradientStopCollection, radialGradientBrush);
    }
    HRESULT CreateRadialGradientBrush(const D2D1_RADIAL_GRADIENT_BRUSH_PROPERTIES& radialGradientBrushProperties,
                                      const D2D1_BRUSH_PROPERTIES& brushProperties, ID2D1GradientStopCollection* gradientStopCollection,
                                      ID2D1RadialGradientBrush** radialGradientBrush) {
        return CreateRadialGradientBrush(&radialGradientBrushProperties, &brushProperties, gradientStopCollection, radialGradientBrush);
    }
    HRESULT CreateCompatibleRenderTarget(ID2D1BitmapRenderTarget** bitmapRenderTarget) {
        return CreateCompatibleRenderTarget(nullptr, nullptr, nullptr, D2D1_COMPATIBLE_RENDER_TARGET_OPTIONS_NONE, bitmapRenderTarget);
    }
    HRESULT CreateCompatibleRenderTarget(D2D1_SIZE_F desiredSize, ID2D1BitmapRenderTarget** bitmapRenderTarget) {
        return CreateCompatibleRenderTarget(&desiredSize, nullptr, nullptr, D2D1_COMPATIBLE_RENDER_TARGET_OPTIONS_NONE, bitmapRenderTarget);
    }
    HRESULT CreateCompatibleRenderTarget(D2D1_SIZE_F desiredSize, D2D1_SIZE_U desiredPixelSize, ID2D1BitmapRenderTarget** bitmapRenderTarget) {
        return CreateCompatibleRenderTarget(&desiredSize, &desiredPixelSize, nullptr, D2D1_COMPATIBLE_RENDER_TARGET_OPTIONS_NONE, bitmapRenderTarget);
    }
    HRESULT CreateCompatibleRenderTarget(D2D1_SIZE_F desiredSize, D2D1_SIZE_U desiredPixelSize, D2D1_PIXEL_FORMAT desiredFormat,
                                         ID2D1BitmapRenderTarget** bitmapRenderTarget) {
        return CreateCompatibleRenderTarget(&desiredSize, &desiredPixelSize, &desiredFormat, D2D1_COMPATIBLE_RENDER_TARGET_OPTIONS_NONE,
                                            bitmapRenderTarget);
    }
    HRESULT CreateCompatibleRenderTarget(D2D1_SIZE_F desiredSize, D2D1_SIZE_U desiredPixelSize, D2D1_PIXEL_FORMAT desiredFormat,
                                         D2D1_COMPATIBLE_RENDER_TARGET_OPTIONS options, ID2D1BitmapRenderTarget** bitmapRenderTarget) {
        return CreateCompatibleRenderTarget(&desiredSize, &desiredPixelSize, &desiredFormat, options, bitmapRenderTarget);
    }
    HRESULT CreateLayer(D2D1_SIZE_F size, ID2D1Layer** layer) { return CreateLayer(&size, layer); }
    HRESULT CreateLayer(ID2D1Layer** layer) { return CreateLayer(nullptr, layer); }
    void DrawRectangle(const D2D1_RECT_F& rect, ID2D1Brush* brush, FLOAT strokeWidth = 1.0f, ID2D1StrokeStyle* strokeStyle = nullptr) {
        DrawRectangle(&rect, brush, strokeWidth, strokeStyle);
    }
    void FillRectangle(const D2D1_RECT_F& rect, ID2D1Brush* brush) { FillRectangle(&rect, brush); }
    void DrawRoundedRectangle(const D2D1_ROUNDED_RECT& roundedRect, ID2D1Brush* brush, FLOAT strokeWidth = 1.0f,
                              ID2D1StrokeStyle* strokeStyle = nullptr) {
        DrawRoundedRectangle(&roundedRect, brush, strokeWidth, strokeStyle);
    }
    void FillRoundedRectangle(const D2D1_ROUNDED_RECT& roundedRect, ID2D1Brush* brush) { FillRoundedRectangle(&roundedRect, brush); }
    void DrawEllipse(const D2D1_ELLIPSE& ellipse, ID2D1Brush* brush, FLOAT strokeWidth = 1.0f, ID2D1StrokeStyle* strokeStyle = nullptr) {
        DrawEllipse(&ellipse, brush, strokeWidth, strokeStyle);
    }
    void FillEllipse(const D2D1_ELLIPSE& ellipse, ID2D1Brush* brush) { FillEllipse(&ellipse, brush); }
    void FillOpacityMask(ID2D1Bitmap* opacityMask, ID2D1Brush* brush, D2D1_OPACITY_MASK_CONTENT content,
                         const D2D1_RECT_F& destinationRectangle, const D2D1_RECT_F& sourceRectangle) {
        FillOpacityMask(opacityMask, brush, content, &destinationRectangle, &sourceRectangle);
    }
    void DrawBitmap(ID2D1Bitmap* bitmap, const D2D1_RECT_F& destinationRectangle, FLOAT opacity = 1.0f,
                    D2D1_BITMAP_INTERPOLATION_MODE interpolationMode = D2D1_BITMAP_INTERPOLATION_MODE_LINEAR,
                    const D2D1_RECT_F* sourceRectangle = nullptr) {
        DrawBitmap(bitmap, &destinationRectangle, opacity, interpolationMode, sourceRectangle);
    }
    void DrawBitmap(ID2D1Bitmap* bitmap, const D2D1_RECT_F& destinationRectangle, FLOAT opacity, D2D1_BITMAP_INTERPOLATION_MODE interpolationMode,
                    const D2D1_RECT_F& sourceRectangle) {
        DrawBitmap(bitmap, &destinationRectangle, opacity, interpolationMode, &sourceRectangle);
    }
    void SetTransform(const D2D1_MATRIX_3X2_F& transform) { SetTransform(&transform); }
    void PushLayer(const D2D1_LAYER_PARAMETERS& layerParameters, ID2D1Layer* layer) { PushLayer(&layerParameters, layer); }
    void PushAxisAlignedClip(const D2D1_RECT_F& clipRect, D2D1_ANTIALIAS_MODE antialiasMode) { PushAxisAlignedClip(&clipRect, antialiasMode); }
    void Clear(const D2D1_COLOR_F& clearColor) { Clear(&clearColor); }
    void DrawText(const WCHAR* string, UINT32 stringLength, IDWriteTextFormat* textFormat, const D2D1_RECT_F& layoutRect,
                  ID2D1Brush* defaultFillBrush, D2D1_DRAW_TEXT_OPTIONS options = D2D1_DRAW_TEXT_OPTIONS_NONE,
                  DWRITE_MEASURING_MODE measuringMode = DWRITE_MEASURING_MODE_NATURAL) {
        DrawText(string, stringLength, textFormat, &layoutRect, defaultFillBrush, options, measuringMode);
    }
    void DrawTextW(const WCHAR* string, UINT32 stringLength, IDWriteTextFormat* textFormat, const D2D1_RECT_F* layoutRect,
                   ID2D1Brush* defaultFillBrush, D2D1_DRAW_TEXT_OPTIONS options = D2D1_DRAW_TEXT_OPTIONS_NONE,
                   DWRITE_MEASURING_MODE measuringMode = DWRITE_MEASURING_MODE_NATURAL) {
        DrawText(string, stringLength, textFormat, layoutRect, defaultFillBrush, options, measuringMode);
    }
    void DrawTextW(const WCHAR* string, UINT32 stringLength, IDWriteTextFormat* textFormat, const D2D1_RECT_F& layoutRect,
                   ID2D1Brush* defaultFillBrush, D2D1_DRAW_TEXT_OPTIONS options = D2D1_DRAW_TEXT_OPTIONS_NONE,
                   DWRITE_MEASURING_MODE measuringMode = DWRITE_MEASURING_MODE_NATURAL) {
        DrawText(string, stringLength, textFormat, &layoutRect, defaultFillBrush, options, measuringMode);
    }
};
#pragma pop_macro("DrawText")

struct ID2D1BitmapRenderTarget : public ID2D1RenderTarget {
    LM_IID(0x2cd90695, 0x12e2, 0x11dc, 0x9f, 0xed, 0x00, 0x11, 0x43, 0xa0, 0x55, 0xf9);
    using Base = ID2D1RenderTarget;
    virtual HRESULT STDMETHODCALLTYPE GetBitmap(ID2D1Bitmap** bitmap) = 0;
};

struct ID2D1HwndRenderTarget : public ID2D1RenderTarget {
    LM_IID(0x2cd90698, 0x12e2, 0x11dc, 0x9f, 0xed, 0x00, 0x11, 0x43, 0xa0, 0x55, 0xf9);
    using Base = ID2D1RenderTarget;
    virtual HRESULT STDMETHODCALLTYPE Resize(const D2D1_SIZE_U* pixelSize) = 0;
    HRESULT Resize(const D2D1_SIZE_U& pixelSize) { return Resize(&pixelSize); }
};

// GDI device contexts do not exist here: CreateDCRenderTarget fails with E_NOTIMPL.
struct ID2D1DCRenderTarget : public ID2D1RenderTarget {
    LM_IID(0x1c51bc64, 0xde61, 0x46fd, 0x98, 0x99, 0x63, 0xa5, 0xd8, 0xf0, 0x39, 0x50);
    using Base = ID2D1RenderTarget;
    virtual HRESULT STDMETHODCALLTYPE BindDC(const HDC hDC, const RECT* pSubRect) = 0;
};

// ---- Factory -----------------------------------------------------------------------------

struct ID2D1Factory : public IUnknown {
    LM_IID(0x06152247, 0x6f50, 0x465a, 0x92, 0x45, 0x11, 0x8b, 0xfd, 0x3b, 0x60, 0x07);
    using Base = IUnknown;
    virtual HRESULT STDMETHODCALLTYPE CreateRectangleGeometry(const D2D1_RECT_F* rectangle, ID2D1RectangleGeometry** rectangleGeometry) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateRoundedRectangleGeometry(const D2D1_ROUNDED_RECT* roundedRectangle,
                                                                     ID2D1RoundedRectangleGeometry** roundedRectangleGeometry) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateEllipseGeometry(const D2D1_ELLIPSE* ellipse, ID2D1EllipseGeometry** ellipseGeometry) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateGeometryGroup(D2D1_FILL_MODE fillMode, ID2D1Geometry** geometries, UINT32 geometriesCount,
                                                          ID2D1GeometryGroup** geometryGroup) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateTransformedGeometry(ID2D1Geometry* sourceGeometry, const D2D1_MATRIX_3X2_F* transform,
                                                                ID2D1TransformedGeometry** transformedGeometry) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreatePathGeometry(ID2D1PathGeometry** pathGeometry) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateStrokeStyle(const D2D1_STROKE_STYLE_PROPERTIES* strokeStyleProperties, const FLOAT* dashes,
                                                        UINT32 dashesCount, ID2D1StrokeStyle** strokeStyle) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateWicBitmapRenderTarget(IWICBitmap* target, const D2D1_RENDER_TARGET_PROPERTIES* renderTargetProperties,
                                                                  ID2D1RenderTarget** renderTarget) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateHwndRenderTarget(const D2D1_RENDER_TARGET_PROPERTIES* renderTargetProperties,
                                                             const D2D1_HWND_RENDER_TARGET_PROPERTIES* hwndRenderTargetProperties,
                                                             ID2D1HwndRenderTarget** hwndRenderTarget) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateDCRenderTarget(const D2D1_RENDER_TARGET_PROPERTIES* renderTargetProperties,
                                                           ID2D1DCRenderTarget** dcRenderTarget) = 0;

    HRESULT CreateRectangleGeometry(const D2D1_RECT_F& rectangle, ID2D1RectangleGeometry** rectangleGeometry) {
        return CreateRectangleGeometry(&rectangle, rectangleGeometry);
    }
    HRESULT CreateRoundedRectangleGeometry(const D2D1_ROUNDED_RECT& roundedRectangle, ID2D1RoundedRectangleGeometry** roundedRectangleGeometry) {
        return CreateRoundedRectangleGeometry(&roundedRectangle, roundedRectangleGeometry);
    }
    HRESULT CreateEllipseGeometry(const D2D1_ELLIPSE& ellipse, ID2D1EllipseGeometry** ellipseGeometry) {
        return CreateEllipseGeometry(&ellipse, ellipseGeometry);
    }
    HRESULT CreateTransformedGeometry(ID2D1Geometry* sourceGeometry, const D2D1_MATRIX_3X2_F& transform,
                                      ID2D1TransformedGeometry** transformedGeometry) {
        return CreateTransformedGeometry(sourceGeometry, &transform, transformedGeometry);
    }
    HRESULT CreateStrokeStyle(const D2D1_STROKE_STYLE_PROPERTIES& strokeStyleProperties, const FLOAT* dashes, UINT32 dashesCount,
                              ID2D1StrokeStyle** strokeStyle) {
        return CreateStrokeStyle(&strokeStyleProperties, dashes, dashesCount, strokeStyle);
    }
    HRESULT CreateWicBitmapRenderTarget(IWICBitmap* target, const D2D1_RENDER_TARGET_PROPERTIES& renderTargetProperties,
                                        ID2D1RenderTarget** renderTarget) {
        return CreateWicBitmapRenderTarget(target, &renderTargetProperties, renderTarget);
    }
    HRESULT CreateHwndRenderTarget(const D2D1_RENDER_TARGET_PROPERTIES& renderTargetProperties,
                                   const D2D1_HWND_RENDER_TARGET_PROPERTIES& hwndRenderTargetProperties, ID2D1HwndRenderTarget** hwndRenderTarget) {
        return CreateHwndRenderTarget(&renderTargetProperties, &hwndRenderTargetProperties, hwndRenderTarget);
    }
};

HRESULT WINAPI D2D1CreateFactory(D2D1_FACTORY_TYPE factoryType, REFIID riid, const D2D1_FACTORY_OPTIONS* pFactoryOptions, void** ppIFactory);

inline HRESULT D2D1CreateFactory(D2D1_FACTORY_TYPE factoryType, REFIID riid, void** factory) {
    return D2D1CreateFactory(factoryType, riid, nullptr, factory);
}
template <class Factory>
HRESULT D2D1CreateFactory(D2D1_FACTORY_TYPE factoryType, Factory** factory) {
    return D2D1CreateFactory(factoryType, __uuidof(Factory), reinterpret_cast<void**>(factory));
}
template <class Factory>
HRESULT D2D1CreateFactory(D2D1_FACTORY_TYPE factoryType, const D2D1_FACTORY_OPTIONS& factoryOptions, Factory** factory) {
    return D2D1CreateFactory(factoryType, __uuidof(Factory), &factoryOptions, reinterpret_cast<void**>(factory));
}

// d2d1.dll's matrix helpers, inline: the helper header (d2d1helper.h) needs no library.
// Angles in degrees; positive turns clockwise on the screen (y points down).
inline void D2D1MakeRotateMatrix(FLOAT angle, D2D1_POINT_2F center, D2D1_MATRIX_3X2_F* matrix) {
    const double radians = angle * 3.14159265358979323846 / 180.0;
    const float c = static_cast<float>(std::cos(radians)), s = static_cast<float>(std::sin(radians));
    matrix->_11 = c; matrix->_12 = s;
    matrix->_21 = -s; matrix->_22 = c;
    matrix->_31 = center.x - center.x * c + center.y * s;
    matrix->_32 = center.y - center.x * s - center.y * c;
}
inline void D2D1MakeSkewMatrix(FLOAT angleX, FLOAT angleY, D2D1_POINT_2F center, D2D1_MATRIX_3X2_F* matrix) {
    const float tx = static_cast<float>(std::tan(angleX * 3.14159265358979323846 / 180.0));
    const float ty = static_cast<float>(std::tan(angleY * 3.14159265358979323846 / 180.0));
    matrix->_11 = 1; matrix->_12 = ty;
    matrix->_21 = tx; matrix->_22 = 1;
    matrix->_31 = -center.y * tx; matrix->_32 = -center.x * ty;
}
inline BOOL D2D1IsMatrixInvertible(const D2D1_MATRIX_3X2_F* matrix) {
    const float determinant = matrix->_11 * matrix->_22 - matrix->_12 * matrix->_21;
    return determinant != 0 && std::isfinite(determinant);
}
inline BOOL D2D1InvertMatrix(D2D1_MATRIX_3X2_F* matrix) {
    if (!D2D1IsMatrixInvertible(matrix)) return FALSE;
    const D2D1_MATRIX_3X2_F m = *matrix;
    const float inverse = 1 / (m._11 * m._22 - m._12 * m._21);
    matrix->_11 = m._22 * inverse; matrix->_12 = -m._12 * inverse;
    matrix->_21 = -m._21 * inverse; matrix->_22 = m._11 * inverse;
    matrix->_31 = (m._21 * m._32 - m._22 * m._31) * inverse;
    matrix->_32 = (m._12 * m._31 - m._11 * m._32) * inverse;
    return TRUE;
}

#include "d2d1helper.h"
