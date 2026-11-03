#pragma once
// Linux only: the DirectWrite port's private types, shared by dwrite_*.cpp. Impeller's typography
// lays text out (HarfBuzz shapes it, ICU breaks lines, fontconfig picks fonts and fallbacks) in
// DIPs, so a layout measures the same under any transform it is drawn with. This port places the
// lines as DirectWrite does (alignment without trailing whitespace, paragraph alignment, line
// spacing, trimming) and draws the paragraph, a drawing effect's brush per range. The paragraph
// counts UTF-16 units; the API counts wchar_t (UTF-32) units.
#include "gfx_internal.hpp"
#include "../win32/dwrite_3.h"
#include "../win32/wrl/client.h"

#include <memory>
#include <string>
#include <vector>

namespace lm_dwrite {
using Microsoft::WRL::ComPtr;
namespace gx = lm_impeller;

// ---- Fonts (dwrite_fonts.cpp) -------------------------------------------------------------

// Changes whenever application fonts are registered: layouts built before lay themselves out again.
unsigned fontGeneration();

// The family to ask for `family` (DirectWrite matches family names ignoring case): the
// collection's own family, an installed one, or the Linux stand-in for a Windows family.
std::string resolveFamily(const std::wstring& family, IDWriteFontCollection* collection);
// The system collection (not AddRef'd): installed families and the aliased Windows ones.
IDWriteFontCollection* systemFontCollection();

// IDWriteFactory3's font sets: files loaded as application fonts (every face of a file).
HRESULT createFontFaceReference(std::string path, IDWriteFontFaceReference** reference);
HRESULT createFontSetBuilder(IDWriteFontSetBuilder** builder);
HRESULT createFontCollection(IDWriteFontSet* set, IDWriteFontCollection1** collection);

// UTF-8 of a code point; surrogates and values past U+10FFFF become U+FFFD.
void appendUtf8(std::string& out, char32_t c);
std::string toUtf8(const std::wstring& text);

// ---- Formats (dwrite_factory.cpp) ---------------------------------------------------------

// A format's font, fixed at CreateTextFormat and shared by the layouts made from it.
struct FontSpec {
    std::wstring family;
    ComPtr<IDWriteFontCollection> collection;
    DWRITE_FONT_WEIGHT weight;
    DWRITE_FONT_STYLE style;
    DWRITE_FONT_STRETCH stretch;
    float size;
    std::string resolved; // The family asked of the font collection.
    std::string locale;   // BCP 47; empty for none.
};

struct Paragraph {
    DWRITE_TEXT_ALIGNMENT alignment = DWRITE_TEXT_ALIGNMENT_LEADING;
    DWRITE_PARAGRAPH_ALIGNMENT vertical = DWRITE_PARAGRAPH_ALIGNMENT_NEAR;
    DWRITE_WORD_WRAPPING wrapping = DWRITE_WORD_WRAPPING_WRAP;
    DWRITE_TRIMMING trimming{DWRITE_TRIMMING_GRANULARITY_NONE, 0, 0};
    ComPtr<IDWriteInlineObject> sign;
    DWRITE_LINE_SPACING_METHOD spacing_method = DWRITE_LINE_SPACING_METHOD_DEFAULT;
    float spacing = 0, baseline = 0;
    // Trims overflowing text with "…". Without a sign DirectWrite cuts it unmarked; this port
    // leaves that text to the draw call's clip.
    bool trims() const { return trimming.granularity != DWRITE_TRIMMING_GRANULARITY_NONE && sign; }
};

// What a format (or a layout, which is one) carries; reachable from any IDWriteTextFormat.
class FormatState {
public:
    static const FormatState* from(IDWriteTextFormat* format);
    const std::shared_ptr<const FontSpec>& font() const { return font_; }
    const Paragraph& paragraph() const { return paragraph_; }
    // Changes with every paragraph setter (DrawText's layout cache).
    uint64_t revision() const { return revision_; }
protected:
    FormatState(std::shared_ptr<const FontSpec> font, Paragraph paragraph) : font_(std::move(font)), paragraph_(std::move(paragraph)) {}
    std::shared_ptr<const FontSpec> font_;
    Paragraph paragraph_;
    uint64_t revision_ = 0;
};

// IDWriteTextFormat's methods, for formats and layouts; `changed` follows each paragraph setter.
template <class Interface>
class FormatBase : public lm_com::Object<Interface>, public FormatState {
public:
    HRESULT STDMETHODCALLTYPE SetTextAlignment(DWRITE_TEXT_ALIGNMENT textAlignment) override;
    HRESULT STDMETHODCALLTYPE SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT paragraphAlignment) override;
    HRESULT STDMETHODCALLTYPE SetWordWrapping(DWRITE_WORD_WRAPPING wordWrapping) override;
    HRESULT STDMETHODCALLTYPE SetTrimming(const DWRITE_TRIMMING* trimmingOptions, IDWriteInlineObject* trimmingSign) override;
    HRESULT STDMETHODCALLTYPE SetLineSpacing(DWRITE_LINE_SPACING_METHOD lineSpacingMethod, FLOAT lineSpacing, FLOAT baseline) override;
    DWRITE_TEXT_ALIGNMENT STDMETHODCALLTYPE GetTextAlignment() override { return paragraph_.alignment; }
    DWRITE_PARAGRAPH_ALIGNMENT STDMETHODCALLTYPE GetParagraphAlignment() override { return paragraph_.vertical; }
    DWRITE_WORD_WRAPPING STDMETHODCALLTYPE GetWordWrapping() override { return paragraph_.wrapping; }
    DWRITE_READING_DIRECTION STDMETHODCALLTYPE GetReadingDirection() override { return DWRITE_READING_DIRECTION_LEFT_TO_RIGHT; }
    DWRITE_FLOW_DIRECTION STDMETHODCALLTYPE GetFlowDirection() override { return DWRITE_FLOW_DIRECTION_TOP_TO_BOTTOM; }
    FLOAT STDMETHODCALLTYPE GetIncrementalTabStop() override { return font_->size * 4; }
    HRESULT STDMETHODCALLTYPE GetTrimming(DWRITE_TRIMMING* trimmingOptions, IDWriteInlineObject** trimmingSign) override;
    HRESULT STDMETHODCALLTYPE GetLineSpacing(DWRITE_LINE_SPACING_METHOD* lineSpacingMethod, FLOAT* lineSpacing, FLOAT* baseline) override;
    HRESULT STDMETHODCALLTYPE GetFontCollection(IDWriteFontCollection** fontCollection) override;
    HRESULT STDMETHODCALLTYPE GetFontFamilyName(WCHAR* fontFamilyName, UINT32 nameSize) override;
    DWRITE_FONT_WEIGHT STDMETHODCALLTYPE GetFontWeight() override { return font_->weight; }
    DWRITE_FONT_STYLE STDMETHODCALLTYPE GetFontStyle() override { return font_->style; }
    DWRITE_FONT_STRETCH STDMETHODCALLTYPE GetFontStretch() override { return font_->stretch; }
    FLOAT STDMETHODCALLTYPE GetFontSize() override { return font_->size; }
protected:
    using FormatState::FormatState;
    virtual void changed() {}
private:
    void edited() { ++revision_; changed(); }
};
extern template class FormatBase<IDWriteTextFormat>;
extern template class FormatBase<IDWriteTextLayout1>;

// ---- Layouts (dwrite_layout.cpp, drawn by dwrite_render.cpp) -------------------------------

// A character's formatting beyond the format's (SetFontWeight, SetFontSize, SetUnderline,
// SetDrawingEffect, SetCharacterSpacing).
struct CharStyle {
    float size = 0;
    DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_NORMAL;
    float spacing = 0; // Leading plus trailing spacing.
    bool underline = false;
    ComPtr<IUnknown> effect;
    bool operator==(const CharStyle& other) const {
        return size == other.size && weight == other.weight && spacing == other.spacing && underline == other.underline &&
               effect.Get() == other.effect.Get();
    }
};

class TextLayout final : public FormatBase<IDWriteTextLayout1> {
public:
    static HRESULT create(const WCHAR* text, UINT32 length, IDWriteTextFormat* format, FLOAT maxWidth, FLOAT maxHeight,
                          ComPtr<TextLayout>& layout);
    static TextLayout* from(IDWriteTextLayout* layout);

    HRESULT STDMETHODCALLTYPE SetMaxWidth(FLOAT maxWidth) override;
    HRESULT STDMETHODCALLTYPE SetFontWeight(DWRITE_FONT_WEIGHT fontWeight, DWRITE_TEXT_RANGE textRange) override;
    HRESULT STDMETHODCALLTYPE SetFontSize(FLOAT fontSize, DWRITE_TEXT_RANGE textRange) override;
    HRESULT STDMETHODCALLTYPE SetUnderline(BOOL hasUnderline, DWRITE_TEXT_RANGE textRange) override;
    HRESULT STDMETHODCALLTYPE SetDrawingEffect(IUnknown* drawingEffect, DWRITE_TEXT_RANGE textRange) override;
    HRESULT STDMETHODCALLTYPE SetCharacterSpacing(FLOAT leadingSpacing, FLOAT trailingSpacing, FLOAT minimumAdvanceWidth,
                                                  DWRITE_TEXT_RANGE textRange) override;
    HRESULT STDMETHODCALLTYPE GetLineMetrics(DWRITE_LINE_METRICS* lineMetrics, UINT32 maxLineCount, UINT32* actualLineCount) override;
    HRESULT STDMETHODCALLTYPE GetMetrics(DWRITE_TEXT_METRICS* textMetrics) override;
    HRESULT STDMETHODCALLTYPE HitTestTextRange(UINT32 textPosition, UINT32 textLength, FLOAT originX, FLOAT originY,
                                               DWRITE_HIT_TEST_METRICS* hitTestMetrics, UINT32 maxHitTestMetricsCount,
                                               UINT32* actualHitTestMetricsCount) override;

    // Draws the layout with its box's top-left corner at (x, y), in `paint` (drawing effects in
    // their brushes).
    void draw(gx::Canvas* canvas, float x, float y, const gx::Paint& paint);

private:
    // A laid-out line. Positions are DIPs; `left` is where the line starts in the layout box
    // (its alignment), `top` is below the text's first line (before paragraph alignment).
    struct Line {
        UINT32 first = 0, length = 0, trailing = 0, newline = 0; // Characters.
        float left = 0, top = 0, height = 0, baseline = 0;
        float width = 0, full_width = 0; // Without and with trailing whitespace.
        bool trimmed = false;
        float p_baseline = 0; // Where the paragraph placed the baseline.
    };
    // How the paragraph is shaped: the geometry every drawing of the layout shares.
    struct Shape {
        gx::TextAlign align = gx::TextAlign::left;
        unsigned max_lines = 0;
        bool ellipsis = false;
        float width = 0;
    };

    TextLayout(const FormatState& format, const WCHAR* text, UINT32 length, float maxWidth, float maxHeight);
    void changed() override { dirty_ = true; }

    // Applies `apply` to each character of the range; `relayout` when it changes the layout.
    template <class Apply> HRESULT style(DWRITE_TEXT_RANGE range, bool relayout, Apply apply);
    // Lays the text out on the calling thread's fonts when anything changed.
    void layOut();
    // The paragraph's runs: split where the layout changes (`fine`: also where the ink changes);
    // `ink` fills each run in turn.
    gx::ParagraphSpec specFor(const Shape& shape, bool fine, const gx::Paint* paint) const;
    std::unique_ptr<gx::TextParagraph> build(const Shape& shape) const;
    void measure(const gx::TextParagraph& paragraph);
    UINT32 characterOfUtf16(size_t unit) const;
    float layoutWidth() const;
    // Where the paragraph's first baseline sits against the line spacing's (the drawing's vertical shift).
    float paragraphDrop() const { return lines_.empty() ? 0 : lines_[0].baseline - lines_[0].p_baseline; }

    std::wstring text_;
    std::string utf8_;
    std::vector<uint32_t> offsets_;  // UTF-8 offset of each character, then the end.
    std::vector<uint32_t> utf16_;    // UTF-16 offset of each character, then the end.
    float max_width_, max_height_;
    std::vector<CharStyle> styles_;  // One per character; empty until a range setter runs.
    bool restyled_ = false;          // Effects or underlines changed: runs again, no new layout.

    unsigned fonts_ = 0;                     // fontGeneration() it was laid out with.
    Shape shape_;
    float shift_ = 0;                        // Horizontal offset of an unwrapped paragraph in its box.
    std::unique_ptr<gx::TextParagraph> laid_; // The layout (and its measuring).
    // The last drawing: its inks and paragraph.
    std::unique_ptr<gx::TextParagraph> drawn_;
    std::vector<gx::Color> drawn_inks_;
    bool dirty_ = true;
    UINT32 shown_ = 0; // Characters laid out (before trimmed text).
    std::vector<Line> lines_;
    DWRITE_TEXT_METRICS metrics_{};
};
} // namespace lm_dwrite
