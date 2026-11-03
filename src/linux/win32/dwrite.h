#pragma once
// Linux only: DirectWrite's core interfaces with the Windows SDK's names, values and signatures,
// implemented on Impeller typography and fontconfig (src/linux/gfx/dwrite_*.cpp). Only
// what the launcher calls is declared; the vtables are this port's own, not Windows'. Text
// positions and lengths count wchar_t units, which are UTF-32 code points here.
#include "dcommon.h"

#ifndef E_NOT_SUFFICIENT_BUFFER
#define E_NOT_SUFFICIENT_BUFFER ((HRESULT)0x8007007A) // HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER)
#endif

enum DWRITE_FACTORY_TYPE { DWRITE_FACTORY_TYPE_SHARED = 0, DWRITE_FACTORY_TYPE_ISOLATED = 1 };

enum DWRITE_FONT_WEIGHT {
    DWRITE_FONT_WEIGHT_THIN = 100, DWRITE_FONT_WEIGHT_EXTRA_LIGHT = 200, DWRITE_FONT_WEIGHT_ULTRA_LIGHT = 200,
    DWRITE_FONT_WEIGHT_LIGHT = 300, DWRITE_FONT_WEIGHT_SEMI_LIGHT = 350, DWRITE_FONT_WEIGHT_NORMAL = 400,
    DWRITE_FONT_WEIGHT_REGULAR = 400, DWRITE_FONT_WEIGHT_MEDIUM = 500, DWRITE_FONT_WEIGHT_DEMI_BOLD = 600,
    DWRITE_FONT_WEIGHT_SEMI_BOLD = 600, DWRITE_FONT_WEIGHT_BOLD = 700, DWRITE_FONT_WEIGHT_EXTRA_BOLD = 800,
    DWRITE_FONT_WEIGHT_ULTRA_BOLD = 800, DWRITE_FONT_WEIGHT_BLACK = 900, DWRITE_FONT_WEIGHT_HEAVY = 900,
    DWRITE_FONT_WEIGHT_EXTRA_BLACK = 950, DWRITE_FONT_WEIGHT_ULTRA_BLACK = 950,
};
enum DWRITE_FONT_STRETCH {
    DWRITE_FONT_STRETCH_UNDEFINED = 0, DWRITE_FONT_STRETCH_ULTRA_CONDENSED = 1, DWRITE_FONT_STRETCH_EXTRA_CONDENSED = 2,
    DWRITE_FONT_STRETCH_CONDENSED = 3, DWRITE_FONT_STRETCH_SEMI_CONDENSED = 4, DWRITE_FONT_STRETCH_NORMAL = 5,
    DWRITE_FONT_STRETCH_MEDIUM = 5, DWRITE_FONT_STRETCH_SEMI_EXPANDED = 6, DWRITE_FONT_STRETCH_EXPANDED = 7,
    DWRITE_FONT_STRETCH_EXTRA_EXPANDED = 8, DWRITE_FONT_STRETCH_ULTRA_EXPANDED = 9,
};
enum DWRITE_FONT_STYLE { DWRITE_FONT_STYLE_NORMAL = 0, DWRITE_FONT_STYLE_OBLIQUE = 1, DWRITE_FONT_STYLE_ITALIC = 2 };
enum DWRITE_FONT_SIMULATIONS { DWRITE_FONT_SIMULATIONS_NONE = 0, DWRITE_FONT_SIMULATIONS_BOLD = 1, DWRITE_FONT_SIMULATIONS_OBLIQUE = 2 };

enum DWRITE_TEXT_ALIGNMENT {
    DWRITE_TEXT_ALIGNMENT_LEADING = 0, DWRITE_TEXT_ALIGNMENT_TRAILING = 1, DWRITE_TEXT_ALIGNMENT_CENTER = 2,
    DWRITE_TEXT_ALIGNMENT_JUSTIFIED = 3,
};
enum DWRITE_PARAGRAPH_ALIGNMENT { DWRITE_PARAGRAPH_ALIGNMENT_NEAR = 0, DWRITE_PARAGRAPH_ALIGNMENT_FAR = 1, DWRITE_PARAGRAPH_ALIGNMENT_CENTER = 2 };
enum DWRITE_WORD_WRAPPING {
    DWRITE_WORD_WRAPPING_WRAP = 0, DWRITE_WORD_WRAPPING_NO_WRAP = 1, DWRITE_WORD_WRAPPING_EMERGENCY_BREAK = 2,
    DWRITE_WORD_WRAPPING_WHOLE_WORD = 3, DWRITE_WORD_WRAPPING_CHARACTER = 4,
};
enum DWRITE_READING_DIRECTION {
    DWRITE_READING_DIRECTION_LEFT_TO_RIGHT = 0, DWRITE_READING_DIRECTION_RIGHT_TO_LEFT = 1,
    DWRITE_READING_DIRECTION_TOP_TO_BOTTOM = 2, DWRITE_READING_DIRECTION_BOTTOM_TO_TOP = 3,
};
enum DWRITE_FLOW_DIRECTION {
    DWRITE_FLOW_DIRECTION_TOP_TO_BOTTOM = 0, DWRITE_FLOW_DIRECTION_BOTTOM_TO_TOP = 1,
    DWRITE_FLOW_DIRECTION_LEFT_TO_RIGHT = 2, DWRITE_FLOW_DIRECTION_RIGHT_TO_LEFT = 3,
};
enum DWRITE_TRIMMING_GRANULARITY { DWRITE_TRIMMING_GRANULARITY_NONE = 0, DWRITE_TRIMMING_GRANULARITY_CHARACTER = 1, DWRITE_TRIMMING_GRANULARITY_WORD = 2 };
enum DWRITE_LINE_SPACING_METHOD { DWRITE_LINE_SPACING_METHOD_DEFAULT = 0, DWRITE_LINE_SPACING_METHOD_UNIFORM = 1, DWRITE_LINE_SPACING_METHOD_PROPORTIONAL = 2 };

struct DWRITE_TRIMMING { DWRITE_TRIMMING_GRANULARITY granularity; UINT32 delimiter; UINT32 delimiterCount; };
struct DWRITE_TEXT_RANGE { UINT32 startPosition; UINT32 length; };
struct DWRITE_FONT_METRICS {
    UINT16 designUnitsPerEm, ascent, descent;
    INT16 lineGap;
    UINT16 capHeight, xHeight;
    INT16 underlinePosition;
    UINT16 underlineThickness;
    INT16 strikethroughPosition;
    UINT16 strikethroughThickness;
};
struct DWRITE_TEXT_METRICS {
    FLOAT left, top, width, widthIncludingTrailingWhitespace, height, layoutWidth, layoutHeight;
    UINT32 maxBidiReorderingDepth, lineCount;
};
struct DWRITE_LINE_METRICS {
    UINT32 length, trailingWhitespaceLength, newlineLength;
    FLOAT height, baseline;
    BOOL isTrimmed;
};
struct DWRITE_HIT_TEST_METRICS {
    UINT32 textPosition, length;
    FLOAT left, top, width, height;
    UINT32 bidiLevel;
    BOOL isText, isTrimmed;
};

struct IDWriteFontCollection;
struct IDWriteFontFamily;

// ---- Fonts --------------------------------------------------------------------------------

struct IDWriteFont : public IUnknown {
    LM_IID(0xacd16696, 0x8c14, 0x4f5d, 0x87, 0x7e, 0xfe, 0x3f, 0xc1, 0xd3, 0x27, 0x37);
    using Base = IUnknown;
    virtual void STDMETHODCALLTYPE GetMetrics(DWRITE_FONT_METRICS* fontMetrics) = 0;
};

struct IDWriteFontList : public IUnknown {
    LM_IID(0x1a0d8438, 0x1d97, 0x4ec1, 0xae, 0xf9, 0xa2, 0xfb, 0x86, 0xed, 0x6a, 0xcb);
    using Base = IUnknown;
};

struct IDWriteFontFamily : public IDWriteFontList {
    LM_IID(0xda20d8ef, 0x812a, 0x4c43, 0x98, 0x02, 0x62, 0xec, 0x4a, 0xbd, 0x7a, 0xdd);
    using Base = IDWriteFontList;
    virtual HRESULT STDMETHODCALLTYPE GetFirstMatchingFont(DWRITE_FONT_WEIGHT weight, DWRITE_FONT_STRETCH stretch, DWRITE_FONT_STYLE style,
                                                           IDWriteFont** matchingFont) = 0;
};

struct IDWriteFontCollection : public IUnknown {
    LM_IID(0xa84cee02, 0x3eea, 0x4eee, 0xa8, 0x27, 0x87, 0xc1, 0xa0, 0x2a, 0x0f, 0xcc);
    using Base = IUnknown;
    virtual HRESULT STDMETHODCALLTYPE GetFontFamily(UINT32 index, IDWriteFontFamily** fontFamily) = 0;
    virtual HRESULT STDMETHODCALLTYPE FindFamilyName(const WCHAR* familyName, UINT32* index, BOOL* exists) = 0;
};

// ---- Text ---------------------------------------------------------------------------------

// A trimming sign (CreateEllipsisTrimmingSign); this port draws it as "…".
struct IDWriteInlineObject : public IUnknown {
    LM_IID(0x8339fde3, 0x106f, 0x47ab, 0x83, 0x73, 0x1c, 0x62, 0x95, 0xeb, 0x10, 0xb3);
    using Base = IUnknown;
};

struct IDWriteTextFormat : public IUnknown {
    LM_IID(0x9c906818, 0x31d7, 0x4fd3, 0xa1, 0x51, 0x7c, 0x5e, 0x22, 0x5d, 0xb5, 0x5a);
    using Base = IUnknown;
    virtual HRESULT STDMETHODCALLTYPE SetTextAlignment(DWRITE_TEXT_ALIGNMENT textAlignment) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT paragraphAlignment) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetWordWrapping(DWRITE_WORD_WRAPPING wordWrapping) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetTrimming(const DWRITE_TRIMMING* trimmingOptions, IDWriteInlineObject* trimmingSign) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetLineSpacing(DWRITE_LINE_SPACING_METHOD lineSpacingMethod, FLOAT lineSpacing, FLOAT baseline) = 0;
    virtual DWRITE_TEXT_ALIGNMENT STDMETHODCALLTYPE GetTextAlignment() = 0;
    virtual DWRITE_PARAGRAPH_ALIGNMENT STDMETHODCALLTYPE GetParagraphAlignment() = 0;
    virtual DWRITE_WORD_WRAPPING STDMETHODCALLTYPE GetWordWrapping() = 0;
    virtual DWRITE_READING_DIRECTION STDMETHODCALLTYPE GetReadingDirection() = 0;
    virtual DWRITE_FLOW_DIRECTION STDMETHODCALLTYPE GetFlowDirection() = 0;
    virtual FLOAT STDMETHODCALLTYPE GetIncrementalTabStop() = 0;
    virtual HRESULT STDMETHODCALLTYPE GetTrimming(DWRITE_TRIMMING* trimmingOptions, IDWriteInlineObject** trimmingSign) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetLineSpacing(DWRITE_LINE_SPACING_METHOD* lineSpacingMethod, FLOAT* lineSpacing, FLOAT* baseline) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetFontCollection(IDWriteFontCollection** fontCollection) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetFontFamilyName(WCHAR* fontFamilyName, UINT32 nameSize) = 0;
    virtual DWRITE_FONT_WEIGHT STDMETHODCALLTYPE GetFontWeight() = 0;
    virtual DWRITE_FONT_STYLE STDMETHODCALLTYPE GetFontStyle() = 0;
    virtual DWRITE_FONT_STRETCH STDMETHODCALLTYPE GetFontStretch() = 0;
    virtual FLOAT STDMETHODCALLTYPE GetFontSize() = 0;
};

// Range setters apply to [startPosition, startPosition + length) of the layout's text; the drawing
// effect is an ID2D1Brush when the layout is drawn by Direct2D.
struct IDWriteTextLayout : public IDWriteTextFormat {
    LM_IID(0x53737037, 0x6d14, 0x410b, 0x9b, 0xfe, 0x0b, 0x18, 0x2b, 0xb7, 0x09, 0x61);
    using Base = IDWriteTextFormat;
    virtual HRESULT STDMETHODCALLTYPE SetMaxWidth(FLOAT maxWidth) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetFontWeight(DWRITE_FONT_WEIGHT fontWeight, DWRITE_TEXT_RANGE textRange) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetFontSize(FLOAT fontSize, DWRITE_TEXT_RANGE textRange) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetUnderline(BOOL hasUnderline, DWRITE_TEXT_RANGE textRange) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetDrawingEffect(IUnknown* drawingEffect, DWRITE_TEXT_RANGE textRange) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetLineMetrics(DWRITE_LINE_METRICS* lineMetrics, UINT32 maxLineCount, UINT32* actualLineCount) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetMetrics(DWRITE_TEXT_METRICS* textMetrics) = 0;
    virtual HRESULT STDMETHODCALLTYPE HitTestTextRange(UINT32 textPosition, UINT32 textLength, FLOAT originX, FLOAT originY,
                                                       DWRITE_HIT_TEST_METRICS* hitTestMetrics, UINT32 maxHitTestMetricsCount,
                                                       UINT32* actualHitTestMetricsCount) = 0;
};

// ---- Factory ------------------------------------------------------------------------------

struct IDWriteFactory : public IUnknown {
    LM_IID(0xb859ee5a, 0xd838, 0x4b5b, 0xa2, 0xe8, 0x1a, 0xdc, 0x7d, 0x93, 0xdb, 0x48);
    using Base = IUnknown;
    virtual HRESULT STDMETHODCALLTYPE GetSystemFontCollection(IDWriteFontCollection** fontCollection, BOOL checkForUpdates = FALSE) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateTextFormat(const WCHAR* fontFamilyName, IDWriteFontCollection* fontCollection,
                                                       DWRITE_FONT_WEIGHT fontWeight, DWRITE_FONT_STYLE fontStyle, DWRITE_FONT_STRETCH fontStretch,
                                                       FLOAT fontSize, const WCHAR* localeName, IDWriteTextFormat** textFormat) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateTextLayout(const WCHAR* string, UINT32 stringLength, IDWriteTextFormat* textFormat,
                                                       FLOAT maxWidth, FLOAT maxHeight, IDWriteTextLayout** textLayout) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateEllipsisTrimmingSign(IDWriteTextFormat* textFormat, IDWriteInlineObject** trimmingSign) = 0;
};

HRESULT WINAPI DWriteCreateFactory(DWRITE_FACTORY_TYPE factoryType, REFIID iid, IUnknown** factory);
